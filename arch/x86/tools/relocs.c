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

static Elf_Ehdr		ehdr;
static unsigned long	shnum;
static unsigned int	shstrndx;

struct relocs {
	uint32_t	*offset;
	unsigned long	count;
	unsigned long	size;
};

static struct relocs relocs16;
static struct relocs relocs32;
#if ELF_BITS == 64
static struct relocs relocs32neg;
static struct relocs relocs64;
#endif

struct section {
	Elf_Shdr       shdr;
	struct section *link;
	Elf_Sym        *symtab;
	Elf_Rel        *reltab;
	char           *strtab;
};
static struct section *secs;

static const char * const sym_regex_kernel[S_NSYMTYPES] = {
/*
 * Following symbols have been audited. There values are constant and do
 * not change if bzImage is loaded at a different physical address than
 * the address for which it has been compiled. Don't warn user about
 * absolute relocations present w.r.t these symbols.
 */
	[S_ABS] =
	"^(xen_irq_disable_direct_reloc$|"
	"xen_save_fl_direct_reloc$|"
	"VDSO|"
	"__crc_)",

/*
 * These symbols are known to be relative, even if the linker marks them
 * as absolute (typically defined outside any section in the linker script.)
 */
	[S_REL] =
	"^(__init_(begin|end)|"
	"__x86_cpu_dev_(start|end)|"
	"(__parainstructions|__alt_instructions)(|_end)|"
	"(__iommu_table|__apicdrivers|__smp_locks)(|_end)|"
	"__(start|end)_pci_.*|"
	"__(start|end)_builtin_fw|"
	"__(start|stop)___ksymtab(|_gpl)|"
	"__(start|stop)___kcrctab(|_gpl)|"
	"__(start|stop)___param|"
	"__(start|stop)___modver|"
	"__(start|stop)___bug_table|"
	"__tracedata_(start|end)|"
	"__(start|stop)_notes|"
	"__end_rodata|"
	"__end_rodata_aligned|"
	"__initramfs_start|"
	"(jiffies|jiffies_64)|"
#if ELF_BITS == 64
	"__per_cpu_load|"
	"init_per_cpu__.*|"
	"__end_rodata_hpage_align|"
#endif
	"__vvar_page|"
	"_end)$"
};


static const char * const sym_regex_realmode[S_NSYMTYPES] = {
/*
 * These symbols are known to be relative, even if the linker marks them
 * as absolute (typically defined outside any section in the linker script.)
 */
	[S_REL] =
	"^pa_",

/*
 * These are 16-bit segment symbols when compiling 16-bit code.
 */
	[S_SEG] =
	"^real_mode_seg$",

/*
 * These are offsets belonging to segments, as opposed to linear addresses,
 * when compiling 16-bit code.
 */
	[S_LIN] =
	"^pa_",
};

static const char * const *sym_regex;

static regex_t sym_regex_c[S_NSYMTYPES];
static int is_reloc(enum symtype type, const char *sym_name)
{
	return sym_regex[type] &&
		!regexec(&sym_regex_c[type], sym_name, 0, NULL, 0);
}

static void regex_init(int use_real_mode)
{
        char errbuf[128];
        int err;
	int i;

	if (use_real_mode)
		sym_regex = sym_regex_realmode;
	else
		sym_regex = sym_regex_kernel;

	for (i = 0; i < S_NSYMTYPES; i++) {
		if (!sym_regex[i])
			continue;

		err = regcomp(&sym_regex_c[i], sym_regex[i],
			      REG_EXTENDED|REG_NOSUB);

		if (err) {
			regerror(err, &sym_regex_c[i], errbuf, sizeof(errbuf));
			die("%s", errbuf);
		}
        }
}

static const char *sym_type(unsigned type)
{
	static const char *type_name[] = {
#define SYM_TYPE(X) [X] = #X
		SYM_TYPE(STT_NOTYPE),
		SYM_TYPE(STT_OBJECT),
		SYM_TYPE(STT_FUNC),
		SYM_TYPE(STT_SECTION),
		SYM_TYPE(STT_FILE),
		SYM_TYPE(STT_COMMON),
		SYM_TYPE(STT_TLS),
#undef SYM_TYPE
	};
	const char *name = "unknown sym type name";
	if (type < ARRAY_SIZE(type_name)) {
		name = type_name[type];
	}
	return name;
}

static const char *sym_bind(unsigned bind)
{
	static const char *bind_name[] = {
#define SYM_BIND(X) [X] = #X
		SYM_BIND(STB_LOCAL),
		SYM_BIND(STB_GLOBAL),
		SYM_BIND(STB_WEAK),
#undef SYM_BIND
	};
	const char *name = "unknown sym bind name";
	if (bind < ARRAY_SIZE(bind_name)) {
		name = bind_name[bind];
	}
	return name;
}

static const char *sym_visibility(unsigned visibility)
{
	static const char *visibility_name[] = {
#define SYM_VISIBILITY(X) [X] = #X
		SYM_VISIBILITY(STV_DEFAULT),
		SYM_VISIBILITY(STV_INTERNAL),
		SYM_VISIBILITY(STV_HIDDEN),
		SYM_VISIBILITY(STV_PROTECTED),
#undef SYM_VISIBILITY
	};
	const char *name = "unknown sym visibility name";
	if (visibility < ARRAY_SIZE(visibility_name)) {
		name = visibility_name[visibility];
	}
	return name;
}

static const char *rel_type(unsigned type)
{
	static const char *type_name[] = {
#define REL_TYPE(X) [X] = #X
#if ELF_BITS == 64
		REL_TYPE(R_X86_64_NONE),
		REL_TYPE(R_X86_64_64),
		REL_TYPE(R_X86_64_PC64),
		REL_TYPE(R_X86_64_PC32),
		REL_TYPE(R_X86_64_GOT32),
		REL_TYPE(R_X86_64_PLT32),
		REL_TYPE(R_X86_64_COPY),
		REL_TYPE(R_X86_64_GLOB_DAT),
		REL_TYPE(R_X86_64_JUMP_SLOT),
		REL_TYPE(R_X86_64_RELATIVE),
		REL_TYPE(R_X86_64_GOTPCREL),
		REL_TYPE(R_X86_64_32),
		REL_TYPE(R_X86_64_32S),
		REL_TYPE(R_X86_64_16),
		REL_TYPE(R_X86_64_PC16),
		REL_TYPE(R_X86_64_8),
		REL_TYPE(R_X86_64_PC8),
#else
		REL_TYPE(R_386_NONE),
		REL_TYPE(R_386_32),
		REL_TYPE(R_386_PC32),
		REL_TYPE(R_386_GOT32),
		REL_TYPE(R_386_PLT32),
		REL_TYPE(R_386_COPY),
		REL_TYPE(R_386_GLOB_DAT),
		REL_TYPE(R_386_JMP_SLOT),
		REL_TYPE(R_386_RELATIVE),
		REL_TYPE(R_386_GOTOFF),
		REL_TYPE(R_386_GOTPC),
		REL_TYPE(R_386_8),
		REL_TYPE(R_386_PC8),
		REL_TYPE(R_386_16),
		REL_TYPE(R_386_PC16),
#endif
#undef REL_TYPE
	};
	const char *name = "unknown type rel type name";
	if (type < ARRAY_SIZE(type_name) && type_name[type]) {
		name = type_name[type];
	}
	return name;
}

static const char *sec_name(unsigned shndx)
{
	const char *sec_strtab;
	const char *name;
	sec_strtab = secs[shstrndx].strtab;
	name = "<noname>";
	if (shndx < shnum) {
		name = sec_strtab + secs[shndx].shdr.sh_name;
	}
	else if (shndx == SHN_ABS) {
		name = "ABSOLUTE";
	}
	else if (shndx == SHN_COMMON) {
		name = "COMMON";
	}
	return name;
}

static const char *sym_name(const char *sym_strtab, Elf_Sym *sym)
{
	const char *name;
	name = "<noname>";
	if (sym->st_name) {
		name = sym_strtab + sym->st_name;
	}
	else {
		name = sec_name(sym->st_shndx);
	}
	return name;
}

static Elf_Sym *sym_lookup(const char *symname)
{
	int i;
	for (i = 0; i < shnum; i++) {
		struct section *sec = &secs[i];
		long nsyms;
		char *strtab;
		Elf_Sym *symtab;
		Elf_Sym *sym;

		if (sec->shdr.sh_type != SHT_SYMTAB)
			continue;

		nsyms = sec->shdr.sh_size/sizeof(Elf_Sym);
		symtab = sec->symtab;
		strtab = sec->link->strtab;

		for (sym = symtab; --nsyms >= 0; sym++) {
			if (!sym->st_name)
				continue;
			if (strcmp(symname, strtab + sym->st_name) == 0)
				return sym;
		}
	}
	return 0;
}

#if BYTE_ORDER == LITTLE_ENDIAN
#define le16_to_cpu(val) (val)
#define le32_to_cpu(val) (val)
#define le64_to_cpu(val) (val)
#endif
#if BYTE_ORDER == BIG_ENDIAN
#define le16_to_cpu(val) bswap_16(val)
#define le32_to_cpu(val) bswap_32(val)
#define le64_to_cpu(val) bswap_64(val)
#endif

static uint16_t elf16_to_cpu(uint16_t val)
{
	return le16_to_cpu(val);
}

static uint32_t elf32_to_cpu(uint32_t val)
{
	return le32_to_cpu(val);
}

#define elf_half_to_cpu(x)	elf16_to_cpu(x)
#define elf_word_to_cpu(x)	elf32_to_cpu(x)

#if ELF_BITS == 64
static uint64_t elf64_to_cpu(uint64_t val)
{
        return le64_to_cpu(val);
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
	if (fread(&ehdr, sizeof(ehdr), 1, fp) != 1) {
		die("Cannot read ELF header: %s\n",
			strerror(errno));
	}
	if (memcmp(ehdr.e_ident, ELFMAG, SELFMAG) != 0) {
		die("No ELF magic\n");
	}
	if (ehdr.e_ident[EI_CLASS] != ELF_CLASS) {
		die("Not a %d bit executable\n", ELF_BITS);
	}
	if (ehdr.e_ident[EI_DATA] != ELFDATA2LSB) {
		die("Not a LSB ELF executable\n");
	}
	if (ehdr.e_ident[EI_VERSION] != EV_CURRENT) {
		die("Unknown ELF version\n");
	}
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
			die("Seek to %d failed: %s\n", ehdr.e_shoff, strerror(errno));

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
	int i;
	Elf_Shdr shdr;

	secs = calloc(shnum, sizeof(struct section));
	if (!secs) {
		die("Unable to allocate %d section headers\n",
		    shnum);
	}
	if (fseek(fp, ehdr.e_shoff, SEEK_SET) < 0) {
		die("Seek to %d failed: %s\n",
			ehdr.e_shoff, strerror(errno));
	}
	for (i = 0; i < shnum; i++) {
		struct section *sec = &secs[i];
		if (fread(&shdr, sizeof(shdr), 1, fp) != 1)
			die("Cannot read ELF section headers %d/%d: %s\n",
			    i, shnum, strerror(errno));
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

static void read_strtabs(FILE *fp)
{
	int i;
	for (i = 0; i < shnum; i++) {
		struct section *sec = &secs[i];
		if (sec->shdr.sh_type != SHT_STRTAB) {
			continue;
		}
		sec->strtab = malloc(sec->shdr.sh_size);
		if (!sec->strtab) {
			die("malloc of %d bytes for strtab failed\n",
				sec->shdr.sh_size);
		}
		if (fseek(fp, sec->shdr.sh_offset, SEEK_SET) < 0) {
			die("Seek to %d failed: %s\n",
				sec->shdr.sh_offset, strerror(errno));
		}
		if (fread(sec->strtab, 1, sec->shdr.sh_size, fp)
		    != sec->shdr.sh_size) {
			die("Cannot read symbol table: %s\n",
				strerror(errno));
		}
	}
}

static void read_symtabs(FILE *fp)
{
	int i,j;
	for (i = 0; i < shnum; i++) {
		struct section *sec = &secs[i];
		if (sec->shdr.sh_type != SHT_SYMTAB) {
			continue;
		}
		sec->symtab = malloc(sec->shdr.sh_size);
		if (!sec->symtab) {
			die("malloc of %d bytes for symtab failed\n",
				sec->shdr.sh_size);
		}
		if (fseek(fp, sec->shdr.sh_offset, SEEK_SET) < 0) {
			die("Seek to %d failed: %s\n",
				sec->shdr.sh_offset, strerror(errno));
		}
		if (fread(sec->symtab, 1, sec->shdr.sh_size, fp)
		    != sec->shdr.sh_size) {
			die("Cannot read symbol table: %s\n",
				strerror(errno));
		}
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
	int i,j;
	for (i = 0; i < shnum; i++) {
		struct section *sec = &secs[i];
		if (sec->shdr.sh_type != SHT_REL_TYPE) {
			continue;
		}
		sec->reltab = malloc(sec->shdr.sh_size);
		if (!sec->reltab) {
			die("malloc of %d bytes for relocs failed\n",
				sec->shdr.sh_size);
		}
		if (fseek(fp, sec->shdr.sh_offset, SEEK_SET) < 0) {
			die("Seek to %d failed: %s\n",
				sec->shdr.sh_offset, strerror(errno));
		}
		if (fread(sec->reltab, 1, sec->shdr.sh_size, fp)
		    != sec->shdr.sh_size) {
			die("Cannot read symbol table: %s\n",
				strerror(errno));
		}
		for (j = 0; j < sec->shdr.sh_size/sizeof(Elf_Rel); j++) {
			Elf_Rel *rel = &sec->reltab[j];
			rel->r_offset = elf_addr_to_cpu(rel->r_offset);
			rel->r_info   = elf_xword_to_cpu(rel->r_info);
#if (SHT_REL_TYPE == SHT_RELA)
			rel->r_addend = elf_xword_to_cpu(rel->r_addend);
#endif
		}
	}
}


static void print_absolute_symbols(void)
{
	int i;
	const char *format;

	if (ELF_BITS == 64)
		format = "%5d %016"PRIx64" %5"PRId64" %10s %10s %12s %s\n";
	else
		format = "%5d %08"PRIx32"  %5"PRId32" %10s %10s %12s %s\n";

	printf("Absolute symbols\n");
	printf(" Num:    Value Size  Type       Bind        Visibility  Name\n");
	for (i = 0; i < shnum; i++) {
		struct section *sec = &secs[i];
		char *sym_strtab;
		int j;

		if (sec->shdr.sh_type != SHT_SYMTAB) {
			continue;
		}
		sym_strtab = sec->link->strtab;
		for (j = 0; j < sec->shdr.sh_size/sizeof(Elf_Sym); j++) {
			Elf_Sym *sym;
			const char *name;
			sym = &sec->symtab[j];
			name = sym_name(sym_strtab, sym);
			if (sym->st_shndx != SHN_ABS) {
				continue;
			}
			printf(format,
				j, sym->st_value, sym->st_size,
				sym_type(ELF_ST_TYPE(sym->st_info)),
				sym_bind(ELF_ST_BIND(sym->st_info)),
				sym_visibility(ELF_ST_VISIBILITY(sym->st_other)),
				name);
		}
	}
	printf("\n");
}

static void print_absolute_relocs(void)
{
	int i, printed = 0;
	const char *format;

	if (ELF_BITS == 64)
		format = "%016"PRIx64" %016"PRIx64" %10s %016"PRIx64"  %s\n";
	else
		format = "%08"PRIx32" %08"PRIx32" %10s %08"PRIx32"  %s\n";

	for (i = 0; i < shnum; i++) {
		struct section *sec = &secs[i];
		struct section *sec_applies, *sec_symtab;
		char *sym_strtab;
		Elf_Sym *sh_symtab;
		int j;
		if (sec->shdr.sh_type != SHT_REL_TYPE) {
			continue;
		}
		sec_symtab  = sec->link;
		sec_applies = &secs[sec->shdr.sh_info];
		if (!(sec_applies->shdr.sh_flags & SHF_ALLOC)) {
			continue;
		}
		sh_symtab  = sec_symtab->symtab;
		sym_strtab = sec_symtab->link->strtab;
		for (j = 0; j < sec->shdr.sh_size/sizeof(Elf_Rel); j++) {
			Elf_Rel *rel;
			Elf_Sym *sym;
			const char *name;
			rel = &sec->reltab[j];
			sym = &sh_symtab[ELF_R_SYM(rel->r_info)];
			name = sym_name(sym_strtab, sym);
			if (sym->st_shndx != SHN_ABS) {
				continue;
			}

			/* Absolute symbols are not relocated if bzImage is
			 * loaded at a non-compiled address. Display a warning
			 * to user at compile time about the absolute
			 * relocations present.
			 *
			 * User need to audit the code to make sure
			 * some symbols which should have been section
			 * relative have not become absolute because of some
			 * linker optimization or wrong programming usage.
			 *
			 * Before warning check if this absolute symbol
			 * relocation is harmless.
			 */
			if (is_reloc(S_ABS, name) || is_reloc(S_REL, name))
				continue;

			if (!printed) {
				printf("WARNING: Absolute relocations"
					" present\n");
				printf("Offset     Info     Type     Sym.Value "
					"Sym.Name\n");
				printed = 1;
			}

			printf(format,
				rel->r_offset,
				rel->r_info,
				rel_type(ELF_R_TYPE(rel->r_info)),
				sym->st_value,
				name);
		}
	}

	if (printed)
		printf("\n");
}

static void add_reloc(struct relocs *r, uint32_t offset)
{
	if (r->count == r->size) {
		unsigned long newsize = r->size + 50000;
		void *mem = realloc(r->offset, newsize * sizeof(r->offset[0]));

		if (!mem)
			die("realloc of %ld entries for relocs failed\n",
                                newsize);
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
	for (i = 0; i < shnum; i++) {
		char *sym_strtab;
		Elf_Sym *sh_symtab;
		struct section *sec_applies, *sec_symtab;
		int j;
		struct section *sec = &secs[i];

		if (sec->shdr.sh_type != SHT_REL_TYPE) {
			continue;
		}
		sec_symtab  = sec->link;
		sec_applies = &secs[sec->shdr.sh_info];
		if (!(sec_applies->shdr.sh_flags & SHF_ALLOC)) {
			continue;
		}
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

/*
 * The .data..percpu section is a special case for x86_64 SMP kernels.
 * It is used to initialize the actual per_cpu areas and to provide
 * definitions for the per_cpu variables that correspond to their offsets
 * within the percpu area. Since the values of all of the symbols need
 * to be offsets from the start of the per_cpu area the virtual address
 * (sh_addr) of .data..percpu is 0 in SMP kernels.
 *
 * This means that:
 *
 *	Relocations that reference symbols in the per_cpu area do not
 *	need further relocation (since the value is an offset relative
 *	to the start of the per_cpu area that does not change).
 *
 *	Relocations that apply to the per_cpu area need to have their
 *	offset adjusted by by the value of __per_cpu_load to make them
 *	point to the correct place in the loaded image (because the
 *	virtual address of .data..percpu is 0).
 *
 * For non SMP kernels .data..percpu is linked as part of the normal
 * kernel data and does not require special treatment.
 *
 */
static int per_cpu_shndx	= -1;
static Elf_Addr per_cpu_load_addr;

static void percpu_init(void)
{
	int i;
	for (i = 0; i < shnum; i++) {
		ElfW(Sym) *sym;
		if (strcmp(sec_name(i), ".data..percpu"))
			continue;

		if (secs[i].shdr.sh_addr != 0)	/* non SMP kernel */
			return;

		sym = sym_lookup("__per_cpu_load");
		if (!sym)
			die("can't find __per_cpu_load\n");

		per_cpu_shndx = i;
		per_cpu_load_addr = sym->st_value;
		return;
	}
}

#if ELF_BITS == 64

/*
 * Check to see if a symbol lies in the .data..percpu section.
 *
 * The linker incorrectly associates some symbols with the
 * .data..percpu section so we also need to check the symbol
 * name to make sure that we classify the symbol correctly.
 *
 * The GNU linker incorrectly associates:
 *	__init_begin
 *	__per_cpu_load
 *
 * The "gold" linker incorrectly associates:
 *	init_per_cpu__fixed_percpu_data
 *	init_per_cpu__gdt_page
 */
static int is_percpu_sym(ElfW(Sym) *sym, const char *symname)
{
	return (sym->st_shndx == per_cpu_shndx) &&
		strcmp(symname, "__init_begin") &&
		strcmp(symname, "__per_cpu_load") &&
		strncmp(symname, "init_per_cpu_", 13);
}


static int do_reloc64(struct section *sec, Elf_Rel *rel, ElfW(Sym) *sym,
		      const char *symname)
{
	unsigned r_type = ELF64_R_TYPE(rel->r_info);
	ElfW(Addr) offset = rel->r_offset;
	int shn_abs = (sym->st_shndx == SHN_ABS) && !is_reloc(S_REL, symname);

	if (sym->st_shndx == SHN_UNDEF)
		return 0;

	/*
	 * Adjust the offset if this reloc applies to the percpu section.
	 */
	if (sec->shdr.sh_info == per_cpu_shndx)
		offset += per_cpu_load_addr;

	switch (r_type) {
	case R_X86_64_NONE:
		/* NONE can be ignored. */
		break;

	case R_X86_64_PC32:
	case R_X86_64_PLT32:
		/*
		 * PC relative relocations don't need to be adjusted unless
		 * referencing a percpu symbol.
		 *
		 * NB: R_X86_64_PLT32 can be treated as R_X86_64_PC32.
		 */
		if (is_percpu_sym(sym, symname))
			add_reloc(&relocs32neg, offset);
		break;

	case R_X86_64_PC64:
		/*
		 * Only used by jump labels
		 */
		if (is_percpu_sym(sym, symname))
			die("Invalid R_X86_64_PC64 relocation against per-CPU symbol %s\n",
			    symname);
		break;

	case R_X86_64_32:
	case R_X86_64_32S:
	case R_X86_64_64:
		/*
		 * References to the percpu area don't need to be adjusted.
		 */
		if (is_percpu_sym(sym, symname))
			break;

		if (shn_abs) {
			/*
			 * Whitelisted absolute symbols do not require
			 * relocation.
			 */
			if (is_reloc(S_ABS, symname))
				break;

			die("Invalid absolute %s relocation: %s\n",
			    rel_type(r_type), symname);
			break;
		}

		/*
		 * Relocation offsets for 64 bit kernels are output
		 * as 32 bits and sign extended back to 64 bits when
		 * the relocations are processed.
		 * Make sure that the offset will fit.
		 */
		if ((int32_t)offset != (int64_t)offset)
			die("Relocation offset doesn't fit in 32 bits\n");

		if (r_type == R_X86_64_64)
			add_reloc(&relocs64, offset);
		else
			add_reloc(&relocs32, offset);
		break;

	default:
		die("Unsupported relocation type: %s (%d)\n",
		    rel_type(r_type), r_type);
		break;
	}

	return 0;
}

#else

static int do_reloc32(struct section *sec, Elf_Rel *rel, Elf_Sym *sym,
		      const char *symname)
{
	unsigned r_type = ELF32_R_TYPE(rel->r_info);
	int shn_abs = (sym->st_shndx == SHN_ABS) && !is_reloc(S_REL, symname);

	switch (r_type) {
	case R_386_NONE:
	case R_386_PC32:
	case R_386_PC16:
	case R_386_PC8:
	case R_386_PLT32:
		/*
		 * NONE can be ignored and PC relative relocations don't need
		 * to be adjusted. Because sym must be defined, R_386_PLT32 can
		 * be treated the same way as R_386_PC32.
		 */
		break;

	case R_386_32:
		if (shn_abs) {
			/*
			 * Whitelisted absolute symbols do not require
			 * relocation.
			 */
			if (is_reloc(S_ABS, symname))
				break;

			die("Invalid absolute %s relocation: %s\n",
			    rel_type(r_type), symname);
			break;
		}

		add_reloc(&relocs32, rel->r_offset);
		break;

	default:
		die("Unsupported relocation type: %s (%d)\n",
		    rel_type(r_type), r_type);
		break;
	}

	return 0;
}

static int do_reloc_real(struct section *sec, Elf_Rel *rel, Elf_Sym *sym,
			 const char *symname)
{
	unsigned r_type = ELF32_R_TYPE(rel->r_info);
	int shn_abs = (sym->st_shndx == SHN_ABS) && !is_reloc(S_REL, symname);

	switch (r_type) {
	case R_386_NONE:
	case R_386_PC32:
	case R_386_PC16:
	case R_386_PC8:
	case R_386_PLT32:
		/*
		 * NONE can be ignored and PC relative relocations don't need
		 * to be adjusted. Because sym must be defined, R_386_PLT32 can
		 * be treated the same way as R_386_PC32.
		 */
		break;

	case R_386_16:
		if (shn_abs) {
			/*
			 * Whitelisted absolute symbols do not require
			 * relocation.
			 */
			if (is_reloc(S_ABS, symname))
				break;

			if (is_reloc(S_SEG, symname)) {
				add_reloc(&relocs16, rel->r_offset);
				break;
			}
		} else {
			if (!is_reloc(S_LIN, symname))
				break;
		}
		die("Invalid %s %s relocation: %s\n",
		    shn_abs ? "absolute" : "relative",
		    rel_type(r_type), symname);
		break;

	case R_386_32:
		if (shn_abs) {
			/*
			 * Whitelisted absolute symbols do not require
			 * relocation.
			 */
			if (is_reloc(S_ABS, symname))
				break;

			if (is_reloc(S_REL, symname)) {
				add_reloc(&relocs32, rel->r_offset);
				break;
			}
		} else {
			if (is_reloc(S_LIN, symname))
				add_reloc(&relocs32, rel->r_offset);
			break;
		}
		die("Invalid %s %s relocation: %s\n",
		    shn_abs ? "absolute" : "relative",
		    rel_type(r_type), symname);
		break;

	default:
		die("Unsupported relocation type: %s (%d)\n",
		    rel_type(r_type), r_type);
		break;
	}

	return 0;
}

#endif

static int cmp_relocs(const void *va, const void *vb)
{
	const uint32_t *a, *b;
	a = va; b = vb;
	return (*a == *b)? 0 : (*a > *b)? 1 : -1;
}

static void sort_relocs(struct relocs *r)
{
	qsort(r->offset, r->count, sizeof(r->offset[0]), cmp_relocs);
}

static int write32(uint32_t v, FILE *f)
{
	unsigned char buf[4];

	put_unaligned_le32(v, buf);
	return fwrite(buf, 1, 4, f) == 4 ? 0 : -1;
}

static int write32_as_text(uint32_t v, FILE *f)
{
	return fprintf(f, "\t.long 0x%08"PRIx32"\n", v) > 0 ? 0 : -1;
}

static void emit_relocs(int as_text, int use_real_mode)
{
	int i;
	int (*write_reloc)(uint32_t, FILE *) = write32;
	int (*do_reloc)(struct section *sec, Elf_Rel *rel, Elf_Sym *sym,
			const char *symname);

#if ELF_BITS == 64
	if (!use_real_mode)
		do_reloc = do_reloc64;
	else
		die("--realmode not valid for a 64-bit ELF file");
#else
	if (!use_real_mode)
		do_reloc = do_reloc32;
	else
		do_reloc = do_reloc_real;
#endif

	/* Collect up the relocations */
	walk_relocs(do_reloc);

	if (relocs16.count && !use_real_mode)
		die("Segment relocations found but --realmode not specified\n");

	/* Order the relocations for more efficient processing */
	sort_relocs(&relocs32);
#if ELF_BITS == 64
	sort_relocs(&relocs32neg);
	sort_relocs(&relocs64);
#else
	sort_relocs(&relocs16);
#endif

	/* Print the relocations */
	if (as_text) {
		/* Print the relocations in a form suitable that
		 * gas will like.
		 */
		printf(".section \".data.reloc\",\"a\"\n");
		printf(".balign 4\n");
		write_reloc = write32_as_text;
	}

	if (use_real_mode) {
		write_reloc(relocs16.count, stdout);
		for (i = 0; i < relocs16.count; i++)
			write_reloc(relocs16.offset[i], stdout);

		write_reloc(relocs32.count, stdout);
		for (i = 0; i < relocs32.count; i++)
			write_reloc(relocs32.offset[i], stdout);
	} else {
#if ELF_BITS == 64
		/* Print a stop */
		write_reloc(0, stdout);

		/* Now print each relocation */
		for (i = 0; i < relocs64.count; i++)
			write_reloc(relocs64.offset[i], stdout);

		/* Print a stop */
		write_reloc(0, stdout);

		/* Now print each inverse 32-bit relocation */
		for (i = 0; i < relocs32neg.count; i++)
			write_reloc(relocs32neg.offset[i], stdout);
#endif

		/* Print a stop */
		write_reloc(0, stdout);

		/* Now print each relocation */
		for (i = 0; i < relocs32.count; i++)
			write_reloc(relocs32.offset[i], stdout);
	}
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
	printf("%s\t%s\t%s\t%s\n",
		sec_name(sec->shdr.sh_info),
		rel_type(ELF_R_TYPE(rel->r_info)),
		symname,
		sec_name(sym->st_shndx));
	return 0;
}

static void print_reloc_info(void)
{
	printf("reloc section\treloc type\tsymbol\tsymbol section\n");
	walk_relocs(do_reloc_info);
}

#if ELF_BITS == 64
# define process process_64
#else
# define process process_32
#endif

void process(FILE *fp, int use_real_mode, int as_text,
	     int show_absolute_syms, int show_absolute_relocs,
	     int show_reloc_info)
{
	regex_init(use_real_mode);
	read_ehdr(fp);
	read_shdrs(fp);
	read_strtabs(fp);
	read_symtabs(fp);
	read_relocs(fp);
	if (ELF_BITS == 64)
		percpu_init();
	if (show_absolute_syms) {
		print_absolute_symbols();
		return;
	}
	if (show_absolute_relocs) {
		print_absolute_relocs();
		return;
	}
	if (show_reloc_info) {
		print_reloc_info();
		return;
	}
	emit_relocs(as_text, use_real_mode);
}
