
#include <stdio.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <endian.h>
#include <elf.h>

#include "relocs.h"

void die(char *fmt, ...)
{
	va_list ap;

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(1);
}

static void usage(void)
{
	die("relocs [--reloc-info|--text|--bin|--keep] vmlinux\n");
}

int main(int argc, char **argv)
{
	int show_reloc_info, as_text, as_bin, keep_relocs;
	const char *fname;
	FILE *fp;
	int i;
	unsigned char e_ident[EI_NIDENT];

	show_reloc_info = 0;
	as_text = 0;
	as_bin = 0;
	keep_relocs = 0;
	fname = NULL;
	for (i = 1; i < argc; i++) {
		char *arg = argv[i];

		if (*arg == '-') {
			if (strcmp(arg, "--reloc-info") == 0) {
				show_reloc_info = 1;
				continue;
			}
			if (strcmp(arg, "--text") == 0) {
				as_text = 1;
				continue;
			}
			if (strcmp(arg, "--bin") == 0) {
				as_bin = 1;
				continue;
			}
			if (strcmp(arg, "--keep") == 0) {
				keep_relocs = 1;
				continue;
			}
		} else if (!fname) {
			fname = arg;
			continue;
		}
		usage();
	}
	if (!fname)
		usage();

	fp = fopen(fname, "r+");
	if (!fp)
		die("Cannot open %s: %s\n", fname, strerror(errno));

	if (fread(&e_ident, 1, EI_NIDENT, fp) != EI_NIDENT)
		die("Cannot read %s: %s", fname, strerror(errno));

	rewind(fp);
	if (e_ident[EI_CLASS] == ELFCLASS64)
		process_64(fp, as_text,  as_bin, show_reloc_info, keep_relocs);
	else
		process_32(fp, as_text, as_bin, show_reloc_info, keep_relocs);
	fclose(fp);
	return 0;
}
