// SPDX-License-Identifier: GPL-2.0

#include <stdio.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <elf.h>
#include <byteswap.h>
#define USE_BSD
#include <endian.h>

#define ELF_BITS 64

#define ELF_MACHINE		EM_S390
#define ELF_MACHINE_NAME	"IBM S/390"
#define SHT_REL_TYPE		SHT_RELA
#define Elf_Rel			Elf64_Rela

#define ELF_CLASS		ELFCLASS64
#define ELF_ENDIAN		ELFDATA2MSB
#define ELF_R_SYM(val)		ELF64_R_SYM(val)
#define ELF_R_TYPE(val)		ELF64_R_TYPE(val)
#define ELF_ST_TYPE(o)		ELF64_ST_TYPE(o)
#define ELF_ST_BIND(o)		ELF64_ST_BIND(o)
#define ELF_ST_VISIBILITY(o)	ELF64_ST_VISIBILITY(o)

#define ElfW(type)		_ElfW(ELF_BITS, type)
#define _ElfW(bits, type)	__ElfW(bits, type)
#define __ElfW(bits, type)	Elf##bits##_##type

#define Elf_Addr		ElfW(Addr)
#define Elf_Ehdr		ElfW(Ehdr)
#define Elf_Phdr		ElfW(Phdr)
#define Elf_Shdr		ElfW(Shdr)
#define Elf_Sym			ElfW(Sym)

static Elf_Ehdr		ehdr;
static unsigned long	shnum;
static unsigned int	shstrndx;

struct relocs {
	uint32_t	*offset;
	unsigned long	count;
	unsigned long	size;
};

static struct relocs relocs64;
#define FMT PRIu64

struct section {
	Elf_Shdr	shdr;
	struct section	*link;
	Elf_Rel		*reltab;
};

static struct section *secs;

#if BYTE_ORDER == LITTLE_ENDIAN
#define le16_to_cpu(val)	(val)
#define le32_to_cpu(val)	(val)
#define le64_to_cpu(val)	(val)
#define be16_to_cpu(val)	bswap_16(val)
#define be32_to_cpu(val)	bswap_32(val)
#define be64_to_cpu(val)	bswap_64(val)
#endif

#if BYTE_ORDER == BIG_ENDIAN
#define le16_to_cpu(val)	bswap_16(val)
#define le32_to_cpu(val)	bswap_32(val)
#define le64_to_cpu(val)	bswap_64(val)
#define be16_to_cpu(val)	(val)
#define be32_to_cpu(val)	(val)
#define be64_to_cpu(val)	(val)
#endif

static uint16_t elf16_to_cpu(uint16_t val)
{
	if (ehdr.e_ident[EI_DATA] == ELFDATA2LSB)
		return le16_to_cpu(val);
	else
		return be16_to_cpu(val);
}

static uint32_t elf32_to_cpu(uint32_t val)
{
	if (ehdr.e_ident[EI_DATA] == ELFDATA2LSB)
		return le32_to_cpu(val);
	else
		return be32_to_cpu(val);
}

#define elf_half_to_cpu(x)	elf16_to_cpu(x)
#define elf_word_to_cpu(x)	elf32_to_cpu(x)

static uint64_t elf64_to_cpu(uint64_t val)
{
	return be64_to_cpu(val);
}

#define elf_addr_to_cpu(x)	elf64_to_cpu(x)
#define elf_off_to_cpu(x)	elf64_to_cpu(x)
#define elf_xword_to_cpu(x)	elf64_to_cpu(x)

static void die(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(1);
}

static void read_ehdr(FILE *fp)
{
	if (fread(&ehdr, sizeof(ehdr), 1, fp) != 1)
		die("Cannot read ELF header: %s\n", strerror(errno));
	if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0)
		die("No ELF magic\n");
	if (ehdr.e_ident[EI_CLASS] != ELF_CLASS)
		die("Not a %d bit executable\n", ELF_BITS);
	if (ehdr.e_ident[EI_DATA] != ELF_ENDIAN)
		die("ELF endian mismatch\n");
	if (ehdr.e_ident[EI_VERSION] != EV_CURRENT)
		die("Unknown ELF version\n");

	/* Convert the fields to native endian */
	ehdr.e_type	 = elf_half_to_cpu(ehdr.e_type);
	ehdr.e_machine	 = elf_half_to_cpu(ehdr.e_machine);
	ehdr.e_version	 = elf_word_to_cpu(ehdr.e_version);
	ehdr.e_entry	 = elf_addr_to_cpu(ehdr.e_entry);
	ehdr.e_phoff	 = elf_off_to_cpu(ehdr.e_phoff);
	ehdr.e_shoff	 = elf_off_to_cpu(ehdr.e_shoff);
	ehdr.e_flags	 = elf_word_to_cpu(ehdr.e_flags);
	ehdr.e_ehsize	 = elf_half_to_cpu(ehdr.e_ehsize);
	ehdr.e_phentsize = elf_half_to_cpu(ehdr.e_phentsize);
	ehdr.e_phnum	 = elf_half_to_cpu(ehdr.e_phnum);
	ehdr.e_shentsize = elf_half_to_cpu(ehdr.e_shentsize);
	ehdr.e_shnum	 = elf_half_to_cpu(ehdr.e_shnum);
	ehdr.e_shstrndx  = elf_half_to_cpu(ehdr.e_shstrndx);

	shnum = ehdr.e_shnum;
	shstrndx = ehdr.e_shstrndx;

	if ((ehdr.e_type != ET_EXEC) && (ehdr.e_type != ET_DYN))
		die("Unsupported ELF header type\n");
	if (ehdr.e_machine != ELF_MACHINE)
		die("Not for %s\n", ELF_MACHINE_NAME);
	if (ehdr.e_version != EV_CURRENT)
		die("Unknown ELF version\n");
	if (ehdr.e_ehsize != sizeof(Elf_Ehdr))
		die("Bad Elf header size\n");
	if (ehdr.e_phentsize != sizeof(Elf_Phdr))
		die("Bad program header entry\n");
	if (ehdr.e_shentsize != sizeof(Elf_Shdr))
		die("Bad section header entry\n");

	if (shnum == SHN_UNDEF || shstrndx == SHN_XINDEX) {
		Elf_Shdr shdr;

		if (fseek(fp, ehdr.e_shoff, SEEK_SET) < 0)
			die("Seek to %" FMT " failed: %s\n", ehdr.e_shoff, strerror(errno));

		if (fread(&shdr, sizeof(shdr), 1, fp) != 1)
			die("Cannot read initial ELF section header: %s\n", strerror(errno));

		if (shnum == SHN_UNDEF)
			shnum = elf_xword_to_cpu(shdr.sh_size);

		if (shstrndx == SHN_XINDEX)
			shstrndx = elf_word_to_cpu(shdr.sh_link);
	}

	if (shstrndx >= shnum)
		die("String table index out of bounds\n");
}

static void read_shdrs(FILE *fp)
{
	Elf_Shdr shdr;
	int i;

	secs = calloc(shnum, sizeof(struct section));
	if (!secs)
		die("Unable to allocate %ld section headers\n", shnum);

	if (fseek(fp, ehdr.e_shoff, SEEK_SET) < 0)
		die("Seek to %" FMT " failed: %s\n", ehdr.e_shoff, strerror(errno));

	for (i = 0; i < shnum; i++) {
		struct section *sec = &secs[i];

		if (fread(&shdr, sizeof(shdr), 1, fp) != 1) {
			die("Cannot read ELF section headers %d/%ld: %s\n",
			    i, shnum, strerror(errno));
		}

		sec->shdr.sh_name      = elf_word_to_cpu(shdr.sh_name);
		sec->shdr.sh_type      = elf_word_to_cpu(shdr.sh_type);
		sec->shdr.sh_flags     = elf_xword_to_cpu(shdr.sh_flags);
		sec->shdr.sh_addr      = elf_addr_to_cpu(shdr.sh_addr);
		sec->shdr.sh_offset    = elf_off_to_cpu(shdr.sh_offset);
		sec->shdr.sh_size      = elf_xword_to_cpu(shdr.sh_size);
		sec->shdr.sh_link      = elf_word_to_cpu(shdr.sh_link);
		sec->shdr.sh_info      = elf_word_to_cpu(shdr.sh_info);
		sec->shdr.sh_addralign = elf_xword_to_cpu(shdr.sh_addralign);
		sec->shdr.sh_entsize   = elf_xword_to_cpu(shdr.sh_entsize);

		if (sec->shdr.sh_link < shnum)
			sec->link = &secs[sec->shdr.sh_link];
	}

}

static void read_relocs(FILE *fp)
{
	int i, j;

	for (i = 0; i < shnum; i++) {
		struct section *sec = &secs[i];

		if (sec->shdr.sh_type != SHT_REL_TYPE)
			continue;

		sec->reltab = malloc(sec->shdr.sh_size);
		if (!sec->reltab)
			die("malloc of %" FMT " bytes for relocs failed\n", sec->shdr.sh_size);

		if (fseek(fp, sec->shdr.sh_offset, SEEK_SET) < 0)
			die("Seek to %" FMT " failed: %s\n", sec->shdr.sh_offset, strerror(errno));

		if (fread(sec->reltab, 1, sec->shdr.sh_size, fp) != sec->shdr.sh_size)
			die("Cannot read symbol table: %s\n", strerror(errno));

		for (j = 0; j < sec->shdr.sh_size / sizeof(Elf_Rel); j++) {
			Elf_Rel *rel = &sec->reltab[j];

			rel->r_offset = elf_addr_to_cpu(rel->r_offset);
			rel->r_info   = elf_xword_to_cpu(rel->r_info);
#if (SHT_REL_TYPE == SHT_RELA)
			rel->r_addend = elf_xword_to_cpu(rel->r_addend);
#endif
		}
	}
}

static void add_reloc(struct relocs *r, uint32_t offset)
{
	if (r->count == r->size) {
		unsigned long newsize = r->size + 50000;
		void *mem = realloc(r->offset, newsize * sizeof(r->offset[0]));

		if (!mem)
			die("realloc of %ld entries for relocs failed\n", newsize);

		r->offset = mem;
		r->size = newsize;
	}
	r->offset[r->count++] = offset;
}

static int do_reloc(struct section *sec, Elf_Rel *rel)
{
	unsigned int r_type = ELF64_R_TYPE(rel->r_info);
	ElfW(Addr) offset = rel->r_offset;

	switch (r_type) {
	case R_390_NONE:
	case R_390_PC32:
	case R_390_PC64:
	case R_390_PC16DBL:
	case R_390_PC32DBL:
	case R_390_PLT32DBL:
	case R_390_GOTENT:
	case R_390_GOTPCDBL:
	case R_390_GOTOFF64:
		break;
	case R_390_64:
		add_reloc(&relocs64, offset - ehdr.e_entry);
		break;
	default:
		die("Unsupported relocation type: %d\n", r_type);
		break;
	}

	return 0;
}

static void walk_relocs(void)
{
	int i;

	/* Walk through the relocations */
	for (i = 0; i < shnum; i++) {
		struct section *sec_applies;
		int j;
		struct section *sec = &secs[i];

		if (sec->shdr.sh_type != SHT_REL_TYPE)
			continue;

		sec_applies = &secs[sec->shdr.sh_info];
		if (!(sec_applies->shdr.sh_flags & SHF_ALLOC))
			continue;

		for (j = 0; j < sec->shdr.sh_size / sizeof(Elf_Rel); j++) {
			Elf_Rel *rel = &sec->reltab[j];

			do_reloc(sec, rel);
		}
	}
}

static int cmp_relocs(const void *va, const void *vb)
{
	const uint32_t *a, *b;

	a = va; b = vb;
	return (*a == *b) ? 0 : (*a > *b) ? 1 : -1;
}

static void sort_relocs(struct relocs *r)
{
	qsort(r->offset, r->count, sizeof(r->offset[0]), cmp_relocs);
}

static int print_reloc(uint32_t v)
{
	return fprintf(stdout, "\t.long 0x%08"PRIx32"\n", v) > 0 ? 0 : -1;
}

static void emit_relocs(void)
{
	int i;

	walk_relocs();
	sort_relocs(&relocs64);

	printf(".section \".vmlinux.relocs_64\",\"a\"\n");
	for (i = 0; i < relocs64.count; i++)
		print_reloc(relocs64.offset[i]);
}

static void process(FILE *fp)
{
	read_ehdr(fp);
	read_shdrs(fp);
	read_relocs(fp);
	emit_relocs();
}

static void usage(void)
{
	die("relocs vmlinux\n");
}

int main(int argc, char **argv)
{
	unsigned char e_ident[EI_NIDENT];
	const char *fname;
	FILE *fp;

	fname = NULL;

	if (argc != 2)
		usage();

	fname = argv[1];

	fp = fopen(fname, "r");
	if (!fp)
		die("Cannot open %s: %s\n", fname, strerror(errno));

	if (fread(&e_ident, 1, EI_NIDENT, fp) != EI_NIDENT)
		die("Cannot read %s: %s", fname, strerror(errno));

	rewind(fp);

	process(fp);

	fclose(fp);
	return 0;
}
