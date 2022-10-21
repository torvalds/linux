// SPDX-License-Identifier: GPL-2.0
/* This is included from relocs_32/64.c */

#define ElfW(type)		_ElfW(ELF_BITS, type)
#define _ElfW(bits, type)	__ElfW(bits, type)
#define __ElfW(bits, type)	Elf##bits##_##type

#define Elf_Addr		ElfW(Addr)
#define Elf_Ehdr		ElfW(Ehdr)
#define Elf_Phdr		ElfW(Phdr)
#define Elf_Shdr		ElfW(Shdr)
#define Elf_Sym			ElfW(Sym)

static Elf_Ehdr ehdr;

struct relocs {
	uint32_t	*offset;
	unsigned long	count;
	unsigned long	size;
};

static struct relocs relocs;

struct section {
	Elf_Shdr       shdr;
	struct section *link;
	Elf_Sym        *symtab;
	Elf_Rel        *reltab;
	char           *strtab;
	long           shdr_offset;
};
static struct section *secs;

static const char * const regex_sym_kernel = {
/* Symbols matching these regex's should never be relocated */
	"^(__crc_)",
};

static regex_t sym_regex_c;

static int regex_skip_reloc(const char *sym_name)
{
	return !regexec(&sym_regex_c, sym_name, 0, NULL, 0);
}

static void regex_init(void)
{
	char errbuf[128];
	int err;

	err = regcomp(&sym_regex_c, regex_sym_kernel,
			REG_EXTENDED|REG_NOSUB);

	if (err) {
		regerror(err, &sym_regex_c, errbuf, sizeof(errbuf));
		die("%s", errbuf);
	}
}

static const char *rel_type(unsigned type)
{
	static const char * const type_name[] = {
#define REL_TYPE(X)[X] = #X
		REL_TYPE(R_MIPS_NONE),
		REL_TYPE(R_MIPS_16),
		REL_TYPE(R_MIPS_32),
		REL_TYPE(R_MIPS_REL32),
		REL_TYPE(R_MIPS_26),
		REL_TYPE(R_MIPS_HI16),
		REL_TYPE(R_MIPS_LO16),
		REL_TYPE(R_MIPS_GPREL16),
		REL_TYPE(R_MIPS_LITERAL),
		REL_TYPE(R_MIPS_GOT16),
		REL_TYPE(R_MIPS_PC16),
		REL_TYPE(R_MIPS_CALL16),
		REL_TYPE(R_MIPS_GPREL32),
		REL_TYPE(R_MIPS_64),
		REL_TYPE(R_MIPS_HIGHER),
		REL_TYPE(R_MIPS_HIGHEST),
		REL_TYPE(R_MIPS_PC21_S2),
		REL_TYPE(R_MIPS_PC26_S2),
#undef REL_TYPE
	};
	const char *name = "unknown type rel type name";

	if (type < ARRAY_SIZE(type_name) && type_name[type])
		name = type_name[type];
	return name;
}

static const char *sec_name(unsigned shndx)
{
	const char *sec_strtab;
	const char *name;

	sec_strtab = secs[ehdr.e_shstrndx].strtab;
	if (shndx < ehdr.e_shnum)
		name = sec_strtab + secs[shndx].shdr.sh_name;
	else if (shndx == SHN_ABS)
		name = "ABSOLUTE";
	else if (shndx == SHN_COMMON)
		name = "COMMON";
	else
		name = "<noname>";
	return name;
}

static struct section *sec_lookup(const char *secname)
{
	int i;

	for (i = 0; i < ehdr.e_shnum; i++)
		if (strcmp(secname, sec_name(i)) == 0)
			return &secs[i];

	return NULL;
}

static const char *sym_name(const char *sym_strtab, Elf_Sym *sym)
{
	const char *name;

	if (sym->st_name)
		name = sym_strtab + sym->st_name;
	else
		name = sec_name(sym->st_shndx);
	return name;
}

#if BYTE_ORDER == LITTLE_ENDIAN
#define le16_to_cpu(val) (val)
#define le32_to_cpu(val) (val)
#define le64_to_cpu(val) (val)
#define be16_to_cpu(val) bswap_16(val)
#define be32_to_cpu(val) bswap_32(val)
#define be64_to_cpu(val) bswap_64(val)

#define cpu_to_le16(val) (val)
#define cpu_to_le32(val) (val)
#define cpu_to_le64(val) (val)
#define cpu_to_be16(val) bswap_16(val)
#define cpu_to_be32(val) bswap_32(val)
#define cpu_to_be64(val) bswap_64(val)
#endif
#if BYTE_ORDER == BIG_ENDIAN
#define le16_to_cpu(val) bswap_16(val)
#define le32_to_cpu(val) bswap_32(val)
#define le64_to_cpu(val) bswap_64(val)
#define be16_to_cpu(val) (val)
#define be32_to_cpu(val) (val)
#define be64_to_cpu(val) (val)

#define cpu_to_le16(val) bswap_16(val)
#define cpu_to_le32(val) bswap_32(val)
#define cpu_to_le64(val) bswap_64(val)
#define cpu_to_be16(val) (val)
#define cpu_to_be32(val) (val)
#define cpu_to_be64(val) (val)
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

static uint32_t cpu_to_elf32(uint32_t val)
{
	if (ehdr.e_ident[EI_DATA] == ELFDATA2LSB)
		return cpu_to_le32(val);
	else
		return cpu_to_be32(val);
}

#define elf_half_to_cpu(x)	elf16_to_cpu(x)
#define elf_word_to_cpu(x)	elf32_to_cpu(x)

#if ELF_BITS == 64
static uint64_t elf64_to_cpu(uint64_t val)
{
	if (ehdr.e_ident[EI_DATA] == ELFDATA2LSB)
		return le64_to_cpu(val);
	else
		return be64_to_cpu(val);
}
#define elf_addr_to_cpu(x)	elf64_to_cpu(x)
#define elf_off_to_cpu(x)	elf64_to_cpu(x)
#define elf_xword_to_cpu(x)	elf64_to_cpu(x)
#else
#define elf_addr_to_cpu(x)	elf32_to_cpu(x)
#define elf_off_to_cpu(x)	elf32_to_cpu(x)
#define elf_xword_to_cpu(x)	elf32_to_cpu(x)
#endif

static void read_ehdr(FILE *fp)
{
	if (fread(&ehdr, sizeof(ehdr), 1, fp) != 1)
		die("Cannot read ELF header: %s\n", strerror(errno));

	if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0)
		die("No ELF magic\n");

	if (ehdr.e_ident[EI_CLASS] != ELF_CLASS)
		die("Not a %d bit executable\n", ELF_BITS);

	if ((ehdr.e_ident[EI_DATA] != ELFDATA2LSB) &&
	    (ehdr.e_ident[EI_DATA] != ELFDATA2MSB))
		die("Unknown ELF Endianness\n");

	if (ehdr.e_ident[EI_VERSION] != EV_CURRENT)
		die("Unknown ELF version\n");

	/* Convert the fields to native endian */
	ehdr.e_type      = elf_half_to_cpu(ehdr.e_type);
	ehdr.e_machine   = elf_half_to_cpu(ehdr.e_machine);
	ehdr.e_version   = elf_word_to_cpu(ehdr.e_version);
	ehdr.e_entry     = elf_addr_to_cpu(ehdr.e_entry);
	ehdr.e_phoff     = elf_off_to_cpu(ehdr.e_phoff);
	ehdr.e_shoff     = elf_off_to_cpu(ehdr.e_shoff);
	ehdr.e_flags     = elf_word_to_cpu(ehdr.e_flags);
	ehdr.e_ehsize    = elf_half_to_cpu(ehdr.e_ehsize);
	ehdr.e_phentsize = elf_half_to_cpu(ehdr.e_phentsize);
	ehdr.e_phnum     = elf_half_to_cpu(ehdr.e_phnum);
	ehdr.e_shentsize = elf_half_to_cpu(ehdr.e_shentsize);
	ehdr.e_shnum     = elf_half_to_cpu(ehdr.e_shnum);
	ehdr.e_shstrndx  = elf_half_to_cpu(ehdr.e_shstrndx);

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

	if (ehdr.e_shstrndx >= ehdr.e_shnum)
		die("String table index out of bounds\n");
}

static void read_shdrs(FILE *fp)
{
	int i;
	Elf_Shdr shdr;

	secs = calloc(ehdr.e_shnum, sizeof(struct section));
	if (!secs)
		die("Unable to allocate %d section headers\n", ehdr.e_shnum);

	if (fseek(fp, ehdr.e_shoff, SEEK_SET) < 0)
		die("Seek to %d failed: %s\n", ehdr.e_shoff, strerror(errno));

	for (i = 0; i < ehdr.e_shnum; i++) {
		struct section *sec = &secs[i];

		sec->shdr_offset = ftell(fp);
		if (fread(&shdr, sizeof(shdr), 1, fp) != 1)
			die("Cannot read ELF section headers %d/%d: %s\n",
			    i, ehdr.e_shnum, strerror(errno));
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
		if (sec->shdr.sh_link < ehdr.e_shnum)
			sec->link = &secs[sec->shdr.sh_link];
	}
}

static void read_strtabs(FILE *fp)
{
	int i;

	for (i = 0; i < ehdr.e_shnum; i++) {
		struct section *sec = &secs[i];

		if (sec->shdr.sh_type != SHT_STRTAB)
			continue;

		sec->strtab = malloc(sec->shdr.sh_size);
		if (!sec->strtab)
			die("malloc of %d bytes for strtab failed\n",
			    sec->shdr.sh_size);

		if (fseek(fp, sec->shdr.sh_offset, SEEK_SET) < 0)
			die("Seek to %d failed: %s\n",
			    sec->shdr.sh_offset, strerror(errno));

		if (fread(sec->strtab, 1, sec->shdr.sh_size, fp) !=
		    sec->shdr.sh_size)
			die("Cannot read symbol table: %s\n", strerror(errno));
	}
}

static void read_symtabs(FILE *fp)
{
	int i, j;

	for (i = 0; i < ehdr.e_shnum; i++) {
		struct section *sec = &secs[i];
		if (sec->shdr.sh_type != SHT_SYMTAB)
			continue;

		sec->symtab = malloc(sec->shdr.sh_size);
		if (!sec->symtab)
			die("malloc of %d bytes for symtab failed\n",
			    sec->shdr.sh_size);

		if (fseek(fp, sec->shdr.sh_offset, SEEK_SET) < 0)
			die("Seek to %d failed: %s\n",
			    sec->shdr.sh_offset, strerror(errno));

		if (fread(sec->symtab, 1, sec->shdr.sh_size, fp) !=
		    sec->shdr.sh_size)
			die("Cannot read symbol table: %s\n", strerror(errno));

		for (j = 0; j < sec->shdr.sh_size/sizeof(Elf_Sym); j++) {
			Elf_Sym *sym = &sec->symtab[j];

			sym->st_name  = elf_word_to_cpu(sym->st_name);
			sym->st_value = elf_addr_to_cpu(sym->st_value);
			sym->st_size  = elf_xword_to_cpu(sym->st_size);
			sym->st_shndx = elf_half_to_cpu(sym->st_shndx);
		}
	}
}

static void read_relocs(FILE *fp)
{
	static unsigned long base;
	int i, j;

	if (!base) {
		struct section *sec = sec_lookup(".text");

		if (!sec)
			die("Could not find .text section\n");

		base = sec->shdr.sh_addr;
	}

	for (i = 0; i < ehdr.e_shnum; i++) {
		struct section *sec = &secs[i];

		if (sec->shdr.sh_type != SHT_REL_TYPE)
			continue;

		sec->reltab = malloc(sec->shdr.sh_size);
		if (!sec->reltab)
			die("malloc of %d bytes for relocs failed\n",
			    sec->shdr.sh_size);

		if (fseek(fp, sec->shdr.sh_offset, SEEK_SET) < 0)
			die("Seek to %d failed: %s\n",
			    sec->shdr.sh_offset, strerror(errno));

		if (fread(sec->reltab, 1, sec->shdr.sh_size, fp) !=
		    sec->shdr.sh_size)
			die("Cannot read symbol table: %s\n", strerror(errno));

		for (j = 0; j < sec->shdr.sh_size/sizeof(Elf_Rel); j++) {
			Elf_Rel *rel = &sec->reltab[j];

			rel->r_offset = elf_addr_to_cpu(rel->r_offset);
			/* Set offset into kernel image */
			rel->r_offset -= base;
#if (ELF_BITS == 32)
			rel->r_info   = elf_xword_to_cpu(rel->r_info);
#else
			/* Convert MIPS64 RELA format - only the symbol
			 * index needs converting to native endianness
			 */
			rel->r_info   = rel->r_info;
			ELF_R_SYM(rel->r_info) = elf32_to_cpu(ELF_R_SYM(rel->r_info));
#endif
#if (SHT_REL_TYPE == SHT_RELA)
			rel->r_addend = elf_xword_to_cpu(rel->r_addend);
#endif
		}
	}
}

static void remove_relocs(FILE *fp)
{
	int i;
	Elf_Shdr shdr;

	for (i = 0; i < ehdr.e_shnum; i++) {
		struct section *sec = &secs[i];

		if (sec->shdr.sh_type != SHT_REL_TYPE)
			continue;

		if (fseek(fp, sec->shdr_offset, SEEK_SET) < 0)
			die("Seek to %d failed: %s\n",
			    sec->shdr_offset, strerror(errno));

		if (fread(&shdr, sizeof(shdr), 1, fp) != 1)
			die("Cannot read ELF section headers %d/%d: %s\n",
			    i, ehdr.e_shnum, strerror(errno));

		/* Set relocation section size to 0, effectively removing it.
		 * This is necessary due to lack of support for relocations
		 * in objcopy when creating 32bit elf from 64bit elf.
		 */
		shdr.sh_size = 0;

		if (fseek(fp, sec->shdr_offset, SEEK_SET) < 0)
			die("Seek to %d failed: %s\n",
			    sec->shdr_offset, strerror(errno));

		if (fwrite(&shdr, sizeof(shdr), 1, fp) != 1)
			die("Cannot write ELF section headers %d/%d: %s\n",
			    i, ehdr.e_shnum, strerror(errno));
	}
}

static void add_reloc(struct relocs *r, uint32_t offset, unsigned type)
{
	/* Relocation representation in binary table:
	 * |76543210|76543210|76543210|76543210|
	 * |  Type  |  offset from _text >> 2  |
	 */
	offset >>= 2;
	if (offset > 0x00FFFFFF)
		die("Kernel image exceeds maximum size for relocation!\n");

	offset = (offset & 0x00FFFFFF) | ((type & 0xFF) << 24);

	if (r->count == r->size) {
		unsigned long newsize = r->size + 50000;
		void *mem = realloc(r->offset, newsize * sizeof(r->offset[0]));

		if (!mem)
			die("realloc failed\n");

		r->offset = mem;
		r->size = newsize;
	}
	r->offset[r->count++] = offset;
}

static void walk_relocs(int (*process)(struct section *sec, Elf_Rel *rel,
			Elf_Sym *sym, const char *symname))
{
	int i;

	/* Walk through the relocations */
	for (i = 0; i < ehdr.e_shnum; i++) {
		char *sym_strtab;
		Elf_Sym *sh_symtab;
		struct section *sec_applies, *sec_symtab;
		int j;
		struct section *sec = &secs[i];

		if (sec->shdr.sh_type != SHT_REL_TYPE)
			continue;

		sec_symtab  = sec->link;
		sec_applies = &secs[sec->shdr.sh_info];
		if (!(sec_applies->shdr.sh_flags & SHF_ALLOC))
			continue;

		sh_symtab = sec_symtab->symtab;
		sym_strtab = sec_symtab->link->strtab;
		for (j = 0; j < sec->shdr.sh_size/sizeof(Elf_Rel); j++) {
			Elf_Rel *rel = &sec->reltab[j];
			Elf_Sym *sym = &sh_symtab[ELF_R_SYM(rel->r_info)];
			const char *symname = sym_name(sym_strtab, sym);

			process(sec, rel, sym, symname);
		}
	}
}

static int do_reloc(struct section *sec, Elf_Rel *rel, Elf_Sym *sym,
		      const char *symname)
{
	unsigned r_type = ELF_R_TYPE(rel->r_info);
	unsigned bind = ELF_ST_BIND(sym->st_info);

	if ((bind == STB_WEAK) && (sym->st_value == 0)) {
		/* Don't relocate weak symbols without a target */
		return 0;
	}

	if (regex_skip_reloc(symname))
		return 0;

	switch (r_type) {
	case R_MIPS_NONE:
	case R_MIPS_REL32:
	case R_MIPS_PC16:
	case R_MIPS_PC21_S2:
	case R_MIPS_PC26_S2:
		/*
		 * NONE can be ignored and PC relative relocations don't
		 * need to be adjusted.
		 */
	case R_MIPS_HIGHEST:
	case R_MIPS_HIGHER:
		/* We support relocating within the same 4Gb segment only,
		 * thus leaving the top 32bits unchanged
		 */
	case R_MIPS_LO16:
		/* We support relocating by 64k jumps only
		 * thus leaving the bottom 16bits unchanged
		 */
		break;

	case R_MIPS_64:
	case R_MIPS_32:
	case R_MIPS_26:
	case R_MIPS_HI16:
		add_reloc(&relocs, rel->r_offset, r_type);
		break;

	default:
		die("Unsupported relocation type: %s (%d)\n",
		    rel_type(r_type), r_type);
		break;
	}

	return 0;
}

static int write_reloc_as_bin(uint32_t v, FILE *f)
{
	unsigned char buf[4];

	v = cpu_to_elf32(v);

	memcpy(buf, &v, sizeof(uint32_t));
	return fwrite(buf, 1, 4, f);
}

static int write_reloc_as_text(uint32_t v, FILE *f)
{
	int res;

	res = fprintf(f, "\t.long 0x%08"PRIx32"\n", v);
	if (res < 0)
		return res;
	else
		return sizeof(uint32_t);
}

static void emit_relocs(int as_text, int as_bin, FILE *outf)
{
	int i;
	int (*write_reloc)(uint32_t, FILE *) = write_reloc_as_bin;
	int size = 0;
	int size_reserved;
	struct section *sec_reloc;

	sec_reloc = sec_lookup(".data.reloc");
	if (!sec_reloc)
		die("Could not find relocation section\n");

	size_reserved = sec_reloc->shdr.sh_size;

	/* Collect up the relocations */
	walk_relocs(do_reloc);

	/* Print the relocations */
	if (as_text) {
		/* Print the relocations in a form suitable that
		 * gas will like.
		 */
		printf(".section \".data.reloc\",\"a\"\n");
		printf(".balign 4\n");
		/* Output text to stdout */
		write_reloc = write_reloc_as_text;
		outf = stdout;
	} else if (as_bin) {
		/* Output raw binary to stdout */
		outf = stdout;
	} else {
		/* Seek to offset of the relocation section.
		* Each relocation is then written into the
		* vmlinux kernel image.
		*/
		if (fseek(outf, sec_reloc->shdr.sh_offset, SEEK_SET) < 0) {
			die("Seek to %d failed: %s\n",
				sec_reloc->shdr.sh_offset, strerror(errno));
		}
	}

	for (i = 0; i < relocs.count; i++)
		size += write_reloc(relocs.offset[i], outf);

	/* Print a stop, but only if we've actually written some relocs */
	if (size)
		size += write_reloc(0, outf);

	if (size > size_reserved)
		/* Die, but suggest a value for CONFIG_RELOCATION_TABLE_SIZE
		 * which will fix this problem and allow a bit of headroom
		 * if more kernel features are enabled
		 */
		die("Relocations overflow available space!\n" \
		    "Please adjust CONFIG_RELOCATION_TABLE_SIZE " \
		    "to at least 0x%08x\n", (size + 0x1000) & ~0xFFF);
}

/*
 * As an aid to debugging problems with different linkers
 * print summary information about the relocs.
 * Since different linkers tend to emit the sections in
 * different orders we use the section names in the output.
 */
static int do_reloc_info(struct section *sec, Elf_Rel *rel, ElfW(Sym) *sym,
				const char *symname)
{
	printf("%16s  0x%08x  %16s  %40s  %16s\n",
		sec_name(sec->shdr.sh_info),
		(unsigned int)rel->r_offset,
		rel_type(ELF_R_TYPE(rel->r_info)),
		symname,
		sec_name(sym->st_shndx));
	return 0;
}

static void print_reloc_info(void)
{
	printf("%16s  %10s  %16s  %40s  %16s\n",
		"reloc section",
		"offset",
		"reloc type",
		"symbol",
		"symbol section");
	walk_relocs(do_reloc_info);
}

#if ELF_BITS == 64
# define process process_64
#else
# define process process_32
#endif

void process(FILE *fp, int as_text, int as_bin,
	     int show_reloc_info, int keep_relocs)
{
	regex_init();
	read_ehdr(fp);
	read_shdrs(fp);
	read_strtabs(fp);
	read_symtabs(fp);
	read_relocs(fp);
	if (show_reloc_info) {
		print_reloc_info();
		return;
	}
	emit_relocs(as_text, as_bin, fp);
	if (!keep_relocs)
		remove_relocs(fp);
}
