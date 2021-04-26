// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2021 - Google LLC
 * Author: Ard Biesheuvel <ardb@google.com>
 *
 * This is a host tool that is intended to be used to take the HMAC digest of
 * the .text and .rodata sections of the fips140.ko module, and store it inside
 * the module. The module will perform an integrity selfcheck at module_init()
 * time, by recalculating the digest and comparing it with the value calculated
 * here.
 *
 * Note that the peculiar way an HMAC is being used as a digest with a public
 * key rather than as a symmetric key signature is mandated by FIPS 140-2.
 */

#include <elf.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include <openssl/hmac.h>

static Elf64_Ehdr *ehdr;
static Elf64_Shdr *shdr;
static int num_shdr;
static const char *strtab;
static Elf64_Sym *syms;
static int num_syms;

static Elf64_Shdr *find_symtab_section(void)
{
	int i;

	for (i = 0; i < num_shdr; i++)
		if (shdr[i].sh_type == SHT_SYMTAB)
			return &shdr[i];
	return NULL;
}

static void *get_sym_addr(const char *sym_name)
{
	int i;

	for (i = 0; i < num_syms; i++)
		if (!strcmp(strtab + syms[i].st_name, sym_name))
			return (void *)ehdr + shdr[syms[i].st_shndx].sh_offset +
			       syms[i].st_value;
	return NULL;
}

static void hmac_section(HMAC_CTX *hmac, const char *start, const char *end)
{
	void *start_addr = get_sym_addr(start);
	void *end_addr = get_sym_addr(end);

	HMAC_Update(hmac, start_addr, end_addr - start_addr);
}

int main(int argc, char **argv)
{
	Elf64_Shdr *symtab_shdr;
	const char *hmac_key;
	unsigned char *dg;
	unsigned int dglen;
	struct stat stat;
	HMAC_CTX *hmac;
	int fd, ret;

	if (argc < 2) {
		fprintf(stderr, "file argument missing\n");
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

	shdr = (void *)ehdr + ehdr->e_shoff;
	num_shdr = ehdr->e_shnum;

	symtab_shdr = find_symtab_section();

	syms = (void *)ehdr + symtab_shdr->sh_offset;
	num_syms = symtab_shdr->sh_size / sizeof(Elf64_Sym);

	strtab = (void *)ehdr + shdr[symtab_shdr->sh_link].sh_offset;

	hmac_key = get_sym_addr("fips140_integ_hmac_key");
	if (!hmac_key) {
		fprintf(stderr, "failed to locate HMAC key in binary\n");
		exit(EXIT_FAILURE);
	}

	dg = get_sym_addr("fips140_integ_hmac_digest");
	if (!dg) {
		fprintf(stderr, "failed to locate HMAC digest in binary\n");
		exit(EXIT_FAILURE);
	}

	hmac = HMAC_CTX_new();
	HMAC_Init_ex(hmac, hmac_key, strlen(hmac_key), EVP_sha256(), NULL);

	hmac_section(hmac, "__fips140_text_start", "__fips140_text_end");
	hmac_section(hmac, "__fips140_rodata_start", "__fips140_rodata_end");

	HMAC_Final(hmac, dg, &dglen);

	close(fd);
	return 0;
}
