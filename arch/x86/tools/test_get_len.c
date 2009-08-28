/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 *
 * Copyright (C) IBM Corporation, 2009
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>

#ifdef __x86_64__
#define CONFIG_X86_64
#else
#define CONFIG_X86_32
#endif
#define unlikely(cond) (cond)

#include <asm/insn.h>
#include <inat.c>
#include <insn.c>

/*
 * Test of instruction analysis in general and insn_get_length() in
 * particular.  See if insn_get_length() and the disassembler agree
 * on the length of each instruction in an elf disassembly.
 *
 * Usage: objdump -d a.out | awk -f distill.awk | ./test_get_len
 */

const char *prog;

static void usage(void)
{
	fprintf(stderr, "Usage: objdump -d a.out | awk -f distill.awk |"
		" %s [y|n](64bit flag)\n", prog);
	exit(1);
}

static void malformed_line(const char *line, int line_nr)
{
	fprintf(stderr, "%s: malformed line %d:\n%s", prog, line_nr, line);
	exit(3);
}

#define BUFSIZE 256

int main(int argc, char **argv)
{
	char line[BUFSIZE];
	unsigned char insn_buf[16];
	struct insn insn;
	int insns = 0;
	int x86_64 = 0;

	prog = argv[0];
	if (argc > 2)
		usage();

	if (argc == 2 && argv[1][0] == 'y')
		x86_64 = 1;

	while (fgets(line, BUFSIZE, stdin)) {
		char copy[BUFSIZE], *s, *tab1, *tab2;
		int nb = 0;
		unsigned int b;

		insns++;
		memset(insn_buf, 0, 16);
		strcpy(copy, line);
		tab1 = strchr(copy, '\t');
		if (!tab1)
			malformed_line(line, insns);
		s = tab1 + 1;
		s += strspn(s, " ");
		tab2 = strchr(s, '\t');
		if (!tab2)
			malformed_line(line, insns);
		*tab2 = '\0';	/* Characters beyond tab2 aren't examined */
		while (s < tab2) {
			if (sscanf(s, "%x", &b) == 1) {
				insn_buf[nb++] = (unsigned char) b;
				s += 3;
			} else
				break;
		}
		/* Decode an instruction */
		insn_init(&insn, insn_buf, x86_64);
		insn_get_length(&insn);
		if (insn.length != nb) {
			fprintf(stderr, "Error: %s", line);
			fprintf(stderr, "Error: objdump says %d bytes, but "
				"insn_get_length() says %d (attr:%x)\n", nb,
				insn.length, insn.attr);
			exit(2);
		}
	}
	fprintf(stderr, "Succeed: decoded and checked %d instructions\n",
		insns);
	return 0;
}
