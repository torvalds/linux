// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020 - Google LLC
 * Author: David Brazdil <dbrazdil@google.com>
 *
 * Generates relocation information used by the kernel to convert
 * absolute addresses in hyp data from kernel VAs to hyp VAs.
 *
 * This is necessary because hyp code is linked into the same binary
 * as the kernel but executes under different memory mappings.
 * If the compiler used absolute addressing, those addresses need to
 * be converted before they are used by hyp code.
 *
 * The input of this program is the relocatable ELF object containing
 * all hyp code/data, not yet linked into vmlinux. Hyp section names
 * should have been prefixed with `.hyp` at this point.
 *
 * The output (printed to stdout) is an assembly file containing
 * an array of 32-bit integers and static relocations that instruct
 * the linker of `vmlinux` to populate the array entries with offsets
 * to positions in the kernel binary containing VAs used by hyp code.
 *
 * Note that dynamic relocations could be used for the same purpose.
 * However, those are only generated if CONFIG_RELOCATABLE=y.
 */

#include <elf.h>
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <generated/autoconf.h>

#define HYP_SECTION_PREFIX		".hyp"
#define HYP_RELOC_SECTION		".hyp.reloc"
#define HYP_SECTION_SYMBOL_PREFIX	"__hyp_section_"

/*
 * AArch64 relocation type constants.
 * Included in case these are not defined in the host toolchain.
 */
#ifndef R_AARCH64_ABS64
#define R_AARCH64_ABS64			257
#endif
#ifndef R_AARCH64_LD_PREL_LO19
#define R_AARCH64_LD_PREL_LO19		273
#endif
#ifndef R_AARCH64_ADR_PREL_LO21
#define R_AARCH64_ADR_PREL_LO21		274
#endif
#ifndef R_AARCH64_ADR_PREL_PG_HI21
#define R_AARCH64_ADR_PREL_PG_HI21	275
#endif
#ifndef R_AARCH64_ADR_PREL_PG_HI21_NC
#define R_AARCH64_ADR_PREL_PG_HI21_NC	276
#endif
#ifndef R_AARCH64_ADD_ABS_LO12_NC
#define R_AARCH64_ADD_ABS_LO12_NC	277
#endif
#ifndef R_AARCH64_LDST8_ABS_LO12_NC
#define R_AARCH64_LDST8_ABS_LO12_NC	278
#endif
#ifndef R_AARCH64_TSTBR14
#define R_AARCH64_TSTBR14		279
#endif
#ifndef R_AARCH64_CONDBR19
#define R_AARCH64_CONDBR19		280
#endif
#ifndef R_AARCH64_JUMP26
#define R_AARCH64_JUMP26		282
#endif
#ifndef R_AARCH64_CALL26
#define R_AARCH64_CALL26		283
#endif
#ifndef R_AARCH64_LDST16_ABS_LO12_NC
#define R_AARCH64_LDST16_ABS_LO12_NC	284
#endif
#ifndef R_AARCH64_LDST32_ABS_LO12_NC
#define R_AARCH64_LDST32_ABS_LO12_NC	285
#endif
#ifndef R_AARCH64_LDST64_ABS_LO12_NC
#define R_AARCH64_LDST64_ABS_LO12_NC	286
#endif
#ifndef R_AARCH64_MOVW_PREL_G0
#define R_AARCH64_MOVW_PREL_G0		287
#endif
#ifndef R_AARCH64_MOVW_PREL_G0_NC
#define R_AARCH64_MOVW_PREL_G0_NC	288
#endif
#ifndef R_AARCH64_MOVW_PREL_G1
#define R_AARCH64_MOVW_PREL_G1		289
#endif
#ifndef R_AARCH64_MOVW_PREL_G1_NC
#define R_AARCH64_MOVW_PREL_G1_NC	290
#endif
#ifndef R_AARCH64_MOVW_PREL_G2
#define R_AARCH64_MOVW_PREL_G2		291
#endif
#ifndef R_AARCH64_MOVW_PREL_G2_NC
#define R_AARCH64_MOVW_PREL_G2_NC	292
#endif
#ifndef R_AARCH64_MOVW_PREL_G3
#define R_AARCH64_MOVW_PREL_G3		293
#endif
#ifndef R_AARCH64_LDST128_ABS_LO12_NC
#define R_AARCH64_LDST128_ABS_LO12_NC	299
#endif

/* Global state of the processed ELF. */
static struct {
	const char	*path;
	char		*begin;
	size_t		size;
	Elf64_Ehdr	*ehdr;
	Elf64_Shdr	*sh_table;
	const char	*sh_string;
} elf;

#if defined(CONFIG_CPU_LITTLE_ENDIAN)

#define elf16toh(x)	le16toh(x)
#define elf32toh(x)	le32toh(x)
#define elf64toh(x)	le64toh(x)

#define ELFENDIAN	ELFDATA2LSB

#elif defined(CONFIG_CPU_BIG_ENDIAN)

#define elf16toh(x)	be16toh(x)
#define elf32toh(x)	be32toh(x)
#define elf64toh(x)	be64toh(x)

#define ELFENDIAN	ELFDATA2MSB

#else

#error PDP-endian sadly unsupported...

#endif

#define fatal_error(fmt, ...)						\
	({								\
		fprintf(stderr, "error: %s: " fmt "\n",			\
			elf.path, ## __VA_ARGS__);			\
		exit(EXIT_FAILURE);					\
		__builtin_unreachable();				\
	})

#define fatal_perror(msg)						\
	({								\
		fprintf(stderr, "error: %s: " msg ": %s\n",		\
			elf.path, strerror(errno));			\
		exit(EXIT_FAILURE);					\
		__builtin_unreachable();				\
	})

#define assert_op(lhs, rhs, fmt, op)					\
	({								\
		typeof(lhs) _lhs = (lhs);				\
		typeof(rhs) _rhs = (rhs);				\
									\
		if (!(_lhs op _rhs)) {					\
			fatal_error("assertion " #lhs " " #op " " #rhs	\
				" failed (lhs=" fmt ", rhs=" fmt	\
				", line=%d)", _lhs, _rhs, __LINE__);	\
		}							\
	})

#define assert_eq(lhs, rhs, fmt)	assert_op(lhs, rhs, fmt, ==)
#define assert_ne(lhs, rhs, fmt)	assert_op(lhs, rhs, fmt, !=)
#define assert_lt(lhs, rhs, fmt)	assert_op(lhs, rhs, fmt, <)
#define assert_ge(lhs, rhs, fmt)	assert_op(lhs, rhs, fmt, >=)

/*
 * Return a pointer of a given type at a given offset from
 * the beginning of the ELF file.
 */
#define elf_ptr(type, off) ((type *)(elf.begin + (off)))

/* Iterate over all sections in the ELF. */
#define for_each_section(var) \
	for (var = elf.sh_table; var < elf.sh_table + elf16toh(elf.ehdr->e_shnum); ++var)

/* Iterate over all Elf64_Rela relocations in a given section. */
#define for_each_rela(shdr, var)					\
	for (var = elf_ptr(Elf64_Rela, elf64toh(shdr->sh_offset));	\
	     var < elf_ptr(Elf64_Rela, elf64toh(shdr->sh_offset) + elf64toh(shdr->sh_size)); var++)

/* True if a string starts with a given prefix. */
static inline bool starts_with(const char *str, const char *prefix)
{
	return memcmp(str, prefix, strlen(prefix)) == 0;
}

/* Returns a string containing the name of a given section. */
static inline const char *section_name(Elf64_Shdr *shdr)
{
	return elf.sh_string + elf32toh(shdr->sh_name);
}

/* Returns a pointer to the first byte of section data. */
static inline const char *section_begin(Elf64_Shdr *shdr)
{
	return elf_ptr(char, elf64toh(shdr->sh_offset));
}

/* Find a section by its offset from the beginning of the file. */
static inline Elf64_Shdr *section_by_off(Elf64_Off off)
{
	assert_ne(off, 0UL, "%lu");
	return elf_ptr(Elf64_Shdr, off);
}

/* Find a section by its index. */
static inline Elf64_Shdr *section_by_idx(uint16_t idx)
{
	assert_ne(idx, SHN_UNDEF, "%u");
	return &elf.sh_table[idx];
}

/*
 * Memory-map the given ELF file, perform sanity checks, and
 * populate global state.
 */
static void init_elf(const char *path)
{
	int fd, ret;
	struct stat stat;

	/* Store path in the global struct for error printing. */
	elf.path = path;

	/* Open the ELF file. */
	fd = open(path, O_RDONLY);
	if (fd < 0)
		fatal_perror("Could not open ELF file");

	/* Get status of ELF file to obtain its size. */
	ret = fstat(fd, &stat);
	if (ret < 0) {
		close(fd);
		fatal_perror("Could not get status of ELF file");
	}

	/* mmap() the entire ELF file read-only at an arbitrary address. */
	elf.begin = mmap(0, stat.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	if (elf.begin == MAP_FAILED) {
		close(fd);
		fatal_perror("Could not mmap ELF file");
	}

	/* mmap() was successful, close the FD. */
	close(fd);

	/* Get pointer to the ELF header. */
	assert_ge(stat.st_size, sizeof(*elf.ehdr), "%lu");
	elf.ehdr = elf_ptr(Elf64_Ehdr, 0);

	/* Check the ELF magic. */
	assert_eq(elf.ehdr->e_ident[EI_MAG0], ELFMAG0, "0x%x");
	assert_eq(elf.ehdr->e_ident[EI_MAG1], ELFMAG1, "0x%x");
	assert_eq(elf.ehdr->e_ident[EI_MAG2], ELFMAG2, "0x%x");
	assert_eq(elf.ehdr->e_ident[EI_MAG3], ELFMAG3, "0x%x");

	/* Sanity check that this is an ELF64 relocatable object for AArch64. */
	assert_eq(elf.ehdr->e_ident[EI_CLASS], ELFCLASS64, "%u");
	assert_eq(elf.ehdr->e_ident[EI_DATA], ELFENDIAN, "%u");
	assert_eq(elf16toh(elf.ehdr->e_type), ET_REL, "%u");
	assert_eq(elf16toh(elf.ehdr->e_machine), EM_AARCH64, "%u");

	/* Populate fields of the global struct. */
	elf.sh_table = section_by_off(elf64toh(elf.ehdr->e_shoff));
	elf.sh_string = section_begin(section_by_idx(elf16toh(elf.ehdr->e_shstrndx)));
}

/* Print the prologue of the output ASM file. */
static void emit_prologue(void)
{
	printf(".data\n"
	       ".pushsection " HYP_RELOC_SECTION ", \"a\"\n");
}

/* Print ASM statements needed as a prologue to a processed hyp section. */
static void emit_section_prologue(const char *sh_orig_name)
{
	/* Declare the hyp section symbol. */
	printf(".global %s%s\n", HYP_SECTION_SYMBOL_PREFIX, sh_orig_name);
}

/*
 * Print ASM statements to create a hyp relocation entry for a given
 * R_AARCH64_ABS64 relocation.
 *
 * The linker of vmlinux will populate the position given by `rela` with
 * an absolute 64-bit kernel VA. If the kernel is relocatable, it will
 * also generate a dynamic relocation entry so that the kernel can shift
 * the address at runtime for KASLR.
 *
 * Emit a 32-bit offset from the current address to the position given
 * by `rela`. This way the kernel can iterate over all kernel VAs used
 * by hyp at runtime and convert them to hyp VAs. However, that offset
 * will not be known until linking of `vmlinux`, so emit a PREL32
 * relocation referencing a symbol that the hyp linker script put at
 * the beginning of the relocated section + the offset from `rela`.
 */
static void emit_rela_abs64(Elf64_Rela *rela, const char *sh_orig_name)
{
	/* Offset of this reloc from the beginning of HYP_RELOC_SECTION. */
	static size_t reloc_offset;

	/* Create storage for the 32-bit offset. */
	printf(".word 0\n");

	/*
	 * Create a PREL32 relocation which instructs the linker of `vmlinux`
	 * to insert offset to position <base> + <offset>, where <base> is
	 * a symbol at the beginning of the relocated section, and <offset>
	 * is `rela->r_offset`.
	 */
	printf(".reloc %lu, R_AARCH64_PREL32, %s%s + 0x%lx\n",
	       reloc_offset, HYP_SECTION_SYMBOL_PREFIX, sh_orig_name,
	       elf64toh(rela->r_offset));

	reloc_offset += 4;
}

/* Print the epilogue of the output ASM file. */
static void emit_epilogue(void)
{
	printf(".popsection\n");
}

/*
 * Iterate over all RELA relocations in a given section and emit
 * hyp relocation data for all absolute addresses in hyp code/data.
 *
 * Static relocations that generate PC-relative-addressing are ignored.
 * Failure is reported for unexpected relocation types.
 */
static void emit_rela_section(Elf64_Shdr *sh_rela)
{
	Elf64_Shdr *sh_orig = &elf.sh_table[elf32toh(sh_rela->sh_info)];
	const char *sh_orig_name = section_name(sh_orig);
	Elf64_Rela *rela;

	/* Skip all non-hyp sections. */
	if (!starts_with(sh_orig_name, HYP_SECTION_PREFIX))
		return;

	emit_section_prologue(sh_orig_name);

	for_each_rela(sh_rela, rela) {
		uint32_t type = (uint32_t)elf64toh(rela->r_info);

		/* Check that rela points inside the relocated section. */
		assert_lt(elf64toh(rela->r_offset), elf64toh(sh_orig->sh_size), "0x%lx");

		switch (type) {
		/*
		 * Data relocations to generate absolute addressing.
		 * Emit a hyp relocation.
		 */
		case R_AARCH64_ABS64:
			emit_rela_abs64(rela, sh_orig_name);
			break;
		/* Allow relocations to generate PC-relative addressing. */
		case R_AARCH64_LD_PREL_LO19:
		case R_AARCH64_ADR_PREL_LO21:
		case R_AARCH64_ADR_PREL_PG_HI21:
		case R_AARCH64_ADR_PREL_PG_HI21_NC:
		case R_AARCH64_ADD_ABS_LO12_NC:
		case R_AARCH64_LDST8_ABS_LO12_NC:
		case R_AARCH64_LDST16_ABS_LO12_NC:
		case R_AARCH64_LDST32_ABS_LO12_NC:
		case R_AARCH64_LDST64_ABS_LO12_NC:
		case R_AARCH64_LDST128_ABS_LO12_NC:
			break;
		/* Allow relative relocations for control-flow instructions. */
		case R_AARCH64_TSTBR14:
		case R_AARCH64_CONDBR19:
		case R_AARCH64_JUMP26:
		case R_AARCH64_CALL26:
			break;
		/* Allow group relocations to create PC-relative offset inline. */
		case R_AARCH64_MOVW_PREL_G0:
		case R_AARCH64_MOVW_PREL_G0_NC:
		case R_AARCH64_MOVW_PREL_G1:
		case R_AARCH64_MOVW_PREL_G1_NC:
		case R_AARCH64_MOVW_PREL_G2:
		case R_AARCH64_MOVW_PREL_G2_NC:
		case R_AARCH64_MOVW_PREL_G3:
			break;
		default:
			fatal_error("Unexpected RELA type %u", type);
		}
	}
}

/* Iterate over all sections and emit hyp relocation data for RELA sections. */
static void emit_all_relocs(void)
{
	Elf64_Shdr *shdr;

	for_each_section(shdr) {
		switch (elf32toh(shdr->sh_type)) {
		case SHT_REL:
			fatal_error("Unexpected SHT_REL section \"%s\"",
				section_name(shdr));
		case SHT_RELA:
			emit_rela_section(shdr);
			break;
		}
	}
}

int main(int argc, const char **argv)
{
	if (argc != 2) {
		fprintf(stderr, "Usage: %s <elf_input>\n", argv[0]);
		return EXIT_FAILURE;
	}

	init_elf(argv[1]);

	emit_prologue();
	emit_all_relocs();
	emit_epilogue();

	return EXIT_SUCCESS;
}
