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
#include <tools/le_byteshift.h>

void die(char *fmt, ...);

#define ARRAY_SIZE(x) (sizeof(x) / sizeof((x)[0]))

enum symtype {
	S_ABS,
	S_REL,
	S_SEG,
	S_LIN,
	S_NSYMTYPES
};

void process_32(FILE *fp, int use_real_mode, int as_text,
		int show_absolute_syms, int show_absolute_relocs,
		int show_reloc_info);
void process_64(FILE *fp, int use_real_mode, int as_text,
		int show_absolute_syms, int show_absolute_relocs,
		int show_reloc_info);
#endif /* RELOCS_H */
