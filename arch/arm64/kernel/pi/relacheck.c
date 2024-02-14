// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2023 - Google LLC
 * Author: Ard Biesheuvel <ardb@google.com>
 */

#include <elf.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
#define HOST_ORDER ELFDATA2LSB
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define HOST_ORDER ELFDATA2MSB
#endif

static Elf64_Ehdr *ehdr;
static Elf64_Shdr *shdr;
static const char *strtab;
static bool swap;

static uint64_t swab_elfxword(uint64_t val)
{
	return swap ? __builtin_bswap64(val) : val;
}

static uint32_t swab_elfword(uint32_t val)
{
	return swap ? __builtin_bswap32(val) : val;
}

static uint16_t swab_elfhword(uint16_t val)
{
	return swap ? __builtin_bswap16(val) : val;
}

int main(int argc, char *argv[])
{
	struct stat stat;
	int fd, ret;

	if (argc < 3) {
		fprintf(stderr, "file arguments missing\n");
		exit(EXIT_FAILURE);
	}

	fd = open(argv[1], O_RDWR);
	if (fd < 0) {
		fprintf(stderr, "failed to open %s\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	ret = fstat(fd, &stat);
	if (ret < 0) {
		fprintf(stderr, "failed to stat() %s\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	ehdr = mmap(0, stat.st_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (ehdr == MAP_FAILED) {
		fprintf(stderr, "failed to mmap() %s\n", argv[1]);
		exit(EXIT_FAILURE);
	}

	swap = ehdr->e_ident[EI_DATA] != HOST_ORDER;
	shdr = (void *)ehdr + swab_elfxword(ehdr->e_shoff);
	strtab = (void *)ehdr +
		 swab_elfxword(shdr[swab_elfhword(ehdr->e_shstrndx)].sh_offset);

	for (int i = 0; i < swab_elfhword(ehdr->e_shnum); i++) {
		unsigned long info, flags;
		bool prel64 = false;
		Elf64_Rela *rela;
		int numrela;

		if (swab_elfword(shdr[i].sh_type) != SHT_RELA)
			continue;

		/* only consider RELA sections operating on data */
		info = swab_elfword(shdr[i].sh_info);
		flags = swab_elfxword(shdr[info].sh_flags);
		if ((flags & (SHF_ALLOC | SHF_EXECINSTR)) != SHF_ALLOC)
			continue;

		/*
		 * We generally don't permit ABS64 relocations in the code that
		 * runs before relocation processing occurs. If statically
		 * initialized absolute symbol references are unavoidable, they
		 * may be emitted into a *.rodata.prel64 section and they will
		 * be converted to place-relative 64-bit references. This
		 * requires special handling in the referring code.
		 */
		if (strstr(strtab + swab_elfword(shdr[info].sh_name),
			   ".rodata.prel64")) {
			prel64 = true;
		}

		rela = (void *)ehdr + swab_elfxword(shdr[i].sh_offset);
		numrela = swab_elfxword(shdr[i].sh_size) / sizeof(*rela);

		for (int j = 0; j < numrela; j++) {
			uint64_t info = swab_elfxword(rela[j].r_info);

			if (ELF64_R_TYPE(info) != R_AARCH64_ABS64)
				continue;

			if (prel64) {
				/* convert ABS64 into PREL64 */
				info ^= R_AARCH64_ABS64 ^ R_AARCH64_PREL64;
				rela[j].r_info = swab_elfxword(info);
			} else {
				fprintf(stderr,
					"Unexpected absolute relocations detected in %s\n",
					argv[2]);
				close(fd);
				unlink(argv[1]);
				exit(EXIT_FAILURE);
			}
		}
	}
	close(fd);
	return 0;
}
