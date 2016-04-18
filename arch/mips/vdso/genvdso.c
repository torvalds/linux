/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Alex Smith <alex.smith@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

/*
 * This tool is used to generate the real VDSO images from the raw image. It
 * first patches up the MIPS ABI flags and GNU attributes sections defined in
 * elf.S to have the correct name and type. It then generates a C source file
 * to be compiled into the kernel containing the VDSO image data and a
 * mips_vdso_image struct for it, including symbol offsets extracted from the
 * image.
 *
 * We need to be passed both a stripped and unstripped VDSO image. The stripped
 * image is compiled into the kernel, but we must also patch up the unstripped
 * image's ABI flags sections so that it can be installed and used for
 * debugging.
 */

#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <byteswap.h>
#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Define these in case the system elf.h is not new enough to have them. */
#ifndef SHT_GNU_ATTRIBUTES
# define SHT_GNU_ATTRIBUTES	0x6ffffff5
#endif
#ifndef SHT_MIPS_ABIFLAGS
# define SHT_MIPS_ABIFLAGS	0x7000002a
#endif

enum {
	ABI_O32 = (1 << 0),
	ABI_N32 = (1 << 1),
	ABI_N64 = (1 << 2),

	ABI_ALL = ABI_O32 | ABI_N32 | ABI_N64,
};

/* Symbols the kernel requires offsets for. */
static struct {
	const char *name;
	const char *offset_name;
	unsigned int abis;
} vdso_symbols[] = {
	{ "__vdso_sigreturn", "off_sigreturn", ABI_O32 },
	{ "__vdso_rt_sigreturn", "off_rt_sigreturn", ABI_ALL },
	{}
};

static const char *program_name;
static const char *vdso_name;
static unsigned char elf_class;
static unsigned int elf_abi;
static bool need_swap;
static FILE *out_file;

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
# define HOST_ORDER		ELFDATA2LSB
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
# define HOST_ORDER		ELFDATA2MSB
#endif

#define BUILD_SWAP(bits)						\
	static uint##bits##_t swap_uint##bits(uint##bits##_t val)	\
	{								\
		return need_swap ? bswap_##bits(val) : val;		\
	}

BUILD_SWAP(16)
BUILD_SWAP(32)
BUILD_SWAP(64)

#define __FUNC(name, bits) name##bits
#define _FUNC(name, bits) __FUNC(name, bits)
#define FUNC(name) _FUNC(name, ELF_BITS)

#define __ELF(x, bits) Elf##bits##_##x
#define _ELF(x, bits) __ELF(x, bits)
#define ELF(x) _ELF(x, ELF_BITS)

/*
 * Include genvdso.h twice with ELF_BITS defined differently to get functions
 * for both ELF32 and ELF64.
 */

#define ELF_BITS 64
#include "genvdso.h"
#undef ELF_BITS

#define ELF_BITS 32
#include "genvdso.h"
#undef ELF_BITS

static void *map_vdso(const char *path, size_t *_size)
{
	int fd;
	struct stat stat;
	void *addr;
	const Elf32_Ehdr *ehdr;

	fd = open(path, O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "%s: Failed to open '%s': %s\n", program_name,
			path, strerror(errno));
		return NULL;
	}

	if (fstat(fd, &stat) != 0) {
		fprintf(stderr, "%s: Failed to stat '%s': %s\n", program_name,
			path, strerror(errno));
		return NULL;
	}

	addr = mmap(NULL, stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd,
		    0);
	if (addr == MAP_FAILED) {
		fprintf(stderr, "%s: Failed to map '%s': %s\n", program_name,
			path, strerror(errno));
		return NULL;
	}

	/* ELF32/64 header formats are the same for the bits we're checking. */
	ehdr = addr;

	if (memcmp(ehdr->e_ident, ELFMAG, SELFMAG) != 0) {
		fprintf(stderr, "%s: '%s' is not an ELF file\n", program_name,
			path);
		return NULL;
	}

	elf_class = ehdr->e_ident[EI_CLASS];
	switch (elf_class) {
	case ELFCLASS32:
	case ELFCLASS64:
		break;
	default:
		fprintf(stderr, "%s: '%s' has invalid ELF class\n",
			program_name, path);
		return NULL;
	}

	switch (ehdr->e_ident[EI_DATA]) {
	case ELFDATA2LSB:
	case ELFDATA2MSB:
		need_swap = ehdr->e_ident[EI_DATA] != HOST_ORDER;
		break;
	default:
		fprintf(stderr, "%s: '%s' has invalid ELF data order\n",
			program_name, path);
		return NULL;
	}

	if (swap_uint16(ehdr->e_machine) != EM_MIPS) {
		fprintf(stderr,
			"%s: '%s' has invalid ELF machine (expected EM_MIPS)\n",
			program_name, path);
		return NULL;
	} else if (swap_uint16(ehdr->e_type) != ET_DYN) {
		fprintf(stderr,
			"%s: '%s' has invalid ELF type (expected ET_DYN)\n",
			program_name, path);
		return NULL;
	}

	*_size = stat.st_size;
	return addr;
}

static bool patch_vdso(const char *path, void *vdso)
{
	if (elf_class == ELFCLASS64)
		return patch_vdso64(path, vdso);
	else
		return patch_vdso32(path, vdso);
}

static bool get_symbols(const char *path, void *vdso)
{
	if (elf_class == ELFCLASS64)
		return get_symbols64(path, vdso);
	else
		return get_symbols32(path, vdso);
}

int main(int argc, char **argv)
{
	const char *dbg_vdso_path, *vdso_path, *out_path;
	void *dbg_vdso, *vdso;
	size_t dbg_vdso_size, vdso_size, i;

	program_name = argv[0];

	if (argc < 4 || argc > 5) {
		fprintf(stderr,
			"Usage: %s <debug VDSO> <stripped VDSO> <output file> [<name>]\n",
			program_name);
		return EXIT_FAILURE;
	}

	dbg_vdso_path = argv[1];
	vdso_path = argv[2];
	out_path = argv[3];
	vdso_name = (argc > 4) ? argv[4] : "";

	dbg_vdso = map_vdso(dbg_vdso_path, &dbg_vdso_size);
	if (!dbg_vdso)
		return EXIT_FAILURE;

	vdso = map_vdso(vdso_path, &vdso_size);
	if (!vdso)
		return EXIT_FAILURE;

	/* Patch both the VDSOs' ABI flags sections. */
	if (!patch_vdso(dbg_vdso_path, dbg_vdso))
		return EXIT_FAILURE;
	if (!patch_vdso(vdso_path, vdso))
		return EXIT_FAILURE;

	if (msync(dbg_vdso, dbg_vdso_size, MS_SYNC) != 0) {
		fprintf(stderr, "%s: Failed to sync '%s': %s\n", program_name,
			dbg_vdso_path, strerror(errno));
		return EXIT_FAILURE;
	} else if (msync(vdso, vdso_size, MS_SYNC) != 0) {
		fprintf(stderr, "%s: Failed to sync '%s': %s\n", program_name,
			vdso_path, strerror(errno));
		return EXIT_FAILURE;
	}

	out_file = fopen(out_path, "w");
	if (!out_file) {
		fprintf(stderr, "%s: Failed to open '%s': %s\n", program_name,
			out_path, strerror(errno));
		return EXIT_FAILURE;
	}

	fprintf(out_file, "/* Automatically generated - do not edit */\n");
	fprintf(out_file, "#include <linux/linkage.h>\n");
	fprintf(out_file, "#include <linux/mm.h>\n");
	fprintf(out_file, "#include <asm/vdso.h>\n");

	/* Write out the stripped VDSO data. */
	fprintf(out_file,
		"static unsigned char vdso_data[PAGE_ALIGN(%zu)] __page_aligned_data = {\n\t",
		vdso_size);
	for (i = 0; i < vdso_size; i++) {
		if (!(i % 10))
			fprintf(out_file, "\n\t");
		fprintf(out_file, "0x%02x, ", ((unsigned char *)vdso)[i]);
	}
	fprintf(out_file, "\n};\n");

	/* Preallocate a page array. */
	fprintf(out_file,
		"static struct page *vdso_pages[PAGE_ALIGN(%zu) / PAGE_SIZE];\n",
		vdso_size);

	fprintf(out_file, "struct mips_vdso_image vdso_image%s%s = {\n",
		(vdso_name[0]) ? "_" : "", vdso_name);
	fprintf(out_file, "\t.data = vdso_data,\n");
	fprintf(out_file, "\t.size = PAGE_ALIGN(%zu),\n", vdso_size);
	fprintf(out_file, "\t.mapping = {\n");
	fprintf(out_file, "\t\t.name = \"[vdso]\",\n");
	fprintf(out_file, "\t\t.pages = vdso_pages,\n");
	fprintf(out_file, "\t},\n");

	/* Calculate and write symbol offsets to <output file> */
	if (!get_symbols(dbg_vdso_path, dbg_vdso)) {
		unlink(out_path);
		return EXIT_FAILURE;
	}

	fprintf(out_file, "};\n");

	return EXIT_SUCCESS;
}
