/* SPDX-License-Identifier: GPL-2.0 */
#ifndef RELOCS_H
#define RELOCS_H

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
#include <regex.h>

void die(char *fmt, ...);

/*
 * Introduced for MIPSr6
 */
#ifndef R_MIPS_PC21_S2
#define R_MIPS_PC21_S2		60
#endif

#ifndef R_MIPS_PC26_S2
#define R_MIPS_PC26_S2		61
#endif

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

enum symtype {
	S_ABS,
	S_REL,
	S_SEG,
	S_LIN,
	S_NSYMTYPES
};

void process_32(FILE *fp, int as_text, int as_bin,
		int show_reloc_info, int keep_relocs);
void process_64(FILE *fp, int as_text, int as_bin,
		int show_reloc_info, int keep_relocs);
#endif /* RELOCS_H */
