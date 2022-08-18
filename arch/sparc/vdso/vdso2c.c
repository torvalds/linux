// SPDX-License-Identifier: GPL-2.0-only
/*
 * vdso2c - A vdso image preparation tool
 * Copyright (c) 2014 Andy Lutomirski and others
 *
 * vdso2c requires stripped and unstripped input.  It would be trivial
 * to fully strip the input in here, but, for reasons described below,
 * we need to write a section table.  Doing this is more or less
 * equivalent to dropping all non-allocatable sections, but it's
 * easier to let objcopy handle that instead of doing it ourselves.
 * If we ever need to do something fancier than what objcopy provides,
 * it would be straightforward to add here.
 *
 * We keep a section table for a few reasons:
 *
 * Binutils has issues debugging the vDSO: it reads the section table to
 * find SHT_NOTE; it won't look at PT_NOTE for the in-memory vDSO, which
 * would break build-id if we removed the section table.  Binutils
 * also requires that shstrndx != 0.  See:
 * https://sourceware.org/bugzilla/show_bug.cgi?id=17064
 *
 * elfutils might not look for PT_NOTE if there is a section table at
 * all.  I don't know whether this matters for any practical purpose.
 *
 * For simplicity, rather than hacking up a partial section table, we
 * just write a mostly complete one.  We omit non-dynamic symbols,
 * though, since they're rather large.
 *
 * Once binutils gets fixed, we might be able to drop this for all but
 * the 64-bit vdso, since build-id only works in kernel RPMs, and
 * systems that update to new enough kernel RPMs will likely update
 * binutils in sync.  build-id has never worked for home-built kernel
 * RPMs without manual symlinking, and I suspect that no one ever does
 * that.
 */

/*
 * Copyright (c) 2017 Oracle and/or its affiliates. All rights reserved.
 */

#include <inttypes.h>
#include <stdint.h>
#include <unistd.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <err.h>

#include <sys/mman.h>
#include <sys/types.h>
#include <tools/be_byteshift.h>

#include <linux/elf.h>
#include <linux/types.h>
#include <linux/kernel.h>

const char *outfilename;

/* Symbols that we need in vdso2c. */
enum {
	sym_vvar_start,
	sym_VDSO_FAKE_SECTION_TABLE_START,
	sym_VDSO_FAKE_SECTION_TABLE_END,
};

struct vdso_sym {
	const char *name;
	int export;
};

struct vdso_sym required_syms[] = {
	[sym_vvar_start] = {"vvar_start", 1},
	[sym_VDSO_FAKE_SECTION_TABLE_START] = {
		"VDSO_FAKE_SECTION_TABLE_START", 0
	},
	[sym_VDSO_FAKE_SECTION_TABLE_END] = {
		"VDSO_FAKE_SECTION_TABLE_END", 0
	},
};

__attribute__((format(printf, 1, 2))) __attribute__((noreturn))
static void fail(const char *format, ...)
{
	va_list ap;

	va_start(ap, format);
	fprintf(stderr, "Error: ");
	vfprintf(stderr, format, ap);
	if (outfilename)
		unlink(outfilename);
	exit(1);
	va_end(ap);
}

/*
 * Evil macros for big-endian reads and writes
 */
#define GBE(x, bits, ifnot)						\
	__builtin_choose_expr(						\
		(sizeof(*(x)) == bits/8),				\
		(__typeof__(*(x)))get_unaligned_be##bits(x), ifnot)

#define LAST_GBE(x)							\
	__builtin_choose_expr(sizeof(*(x)) == 1, *(x), (void)(0))

#define GET_BE(x)							\
	GBE(x, 64, GBE(x, 32, GBE(x, 16, LAST_GBE(x))))

#define PBE(x, val, bits, ifnot)					\
	__builtin_choose_expr(						\
		(sizeof(*(x)) == bits/8),				\
		put_unaligned_be##bits((val), (x)), ifnot)

#define LAST_PBE(x, val)						\
	__builtin_choose_expr(sizeof(*(x)) == 1, *(x) = (val), (void)(0))

#define PUT_BE(x, val)					\
	PBE(x, val, 64, PBE(x, val, 32, PBE(x, val, 16, LAST_PBE(x, val))))

#define NSYMS ARRAY_SIZE(required_syms)

#define BITSFUNC3(name, bits, suffix) name##bits##suffix
#define BITSFUNC2(name, bits, suffix) BITSFUNC3(name, bits, suffix)
#define BITSFUNC(name) BITSFUNC2(name, ELF_BITS, )

#define INT_BITS BITSFUNC2(int, ELF_BITS, _t)

#define ELF_BITS_XFORM2(bits, x) Elf##bits##_##x
#define ELF_BITS_XFORM(bits, x) ELF_BITS_XFORM2(bits, x)
#define ELF(x) ELF_BITS_XFORM(ELF_BITS, x)

#define ELF_BITS 64
#include "vdso2c.h"
#undef ELF_BITS

#define ELF_BITS 32
#include "vdso2c.h"
#undef ELF_BITS

static void go(void *raw_addr, size_t raw_len,
	       void *stripped_addr, size_t stripped_len,
	       FILE *outfile, const char *name)
{
	Elf64_Ehdr *hdr = (Elf64_Ehdr *)raw_addr;

	if (hdr->e_ident[EI_CLASS] == ELFCLASS64) {
		go64(raw_addr, raw_len, stripped_addr, stripped_len,
		     outfile, name);
	} else if (hdr->e_ident[EI_CLASS] == ELFCLASS32) {
		go32(raw_addr, raw_len, stripped_addr, stripped_len,
		     outfile, name);
	} else {
		fail("unknown ELF class\n");
	}
}

static void map_input(const char *name, void **addr, size_t *len, int prot)
{
	off_t tmp_len;

	int fd = open(name, O_RDONLY);

	if (fd == -1)
		err(1, "%s", name);

	tmp_len = lseek(fd, 0, SEEK_END);
	if (tmp_len == (off_t)-1)
		err(1, "lseek");
	*len = (size_t)tmp_len;

	*addr = mmap(NULL, tmp_len, prot, MAP_PRIVATE, fd, 0);
	if (*addr == MAP_FAILED)
		err(1, "mmap");

	close(fd);
}

int main(int argc, char **argv)
{
	size_t raw_len, stripped_len;
	void *raw_addr, *stripped_addr;
	FILE *outfile;
	char *name, *tmp;
	int namelen;

	if (argc != 4) {
		printf("Usage: vdso2c RAW_INPUT STRIPPED_INPUT OUTPUT\n");
		return 1;
	}

	/*
	 * Figure out the struct name.  If we're writing to a .so file,
	 * generate raw output insted.
	 */
	name = strdup(argv[3]);
	namelen = strlen(name);
	if (namelen >= 3 && !strcmp(name + namelen - 3, ".so")) {
		name = NULL;
	} else {
		tmp = strrchr(name, '/');
		if (tmp)
			name = tmp + 1;
		tmp = strchr(name, '.');
		if (tmp)
			*tmp = '\0';
		for (tmp = name; *tmp; tmp++)
			if (*tmp == '-')
				*tmp = '_';
	}

	map_input(argv[1], &raw_addr, &raw_len, PROT_READ);
	map_input(argv[2], &stripped_addr, &stripped_len, PROT_READ);

	outfilename = argv[3];
	outfile = fopen(outfilename, "w");
	if (!outfile)
		err(1, "%s", argv[2]);

	go(raw_addr, raw_len, stripped_addr, stripped_len, outfile, name);

	munmap(raw_addr, raw_len);
	munmap(stripped_addr, stripped_len);
	fclose(outfile);

	return 0;
}
