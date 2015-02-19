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
#include <unistd.h>

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
static int verbose;
static int x86_64;

static void usage(void)
{
	fprintf(stderr, "Usage: objdump -d a.out | awk -f distill.awk |"
		" %s [-y|-n] [-v]\n", prog);
	fprintf(stderr, "\t-y	64bit mode\n");
	fprintf(stderr, "\t-n	32bit mode\n");
	fprintf(stderr, "\t-v	verbose mode\n");
	exit(1);
}

static void malformed_line(const char *line, int line_nr)
{
	fprintf(stderr, "%s: malformed line %d:\n%s", prog, line_nr, line);
	exit(3);
}

static void dump_field(FILE *fp, const char *name, const char *indent,
		       struct insn_field *field)
{
	fprintf(fp, "%s.%s = {\n", indent, name);
	fprintf(fp, "%s\t.value = %d, bytes[] = {%x, %x, %x, %x},\n",
		indent, field->value, field->bytes[0], field->bytes[1],
		field->bytes[2], field->bytes[3]);
	fprintf(fp, "%s\t.got = %d, .nbytes = %d},\n", indent,
		field->got, field->nbytes);
}

static void dump_insn(FILE *fp, struct insn *insn)
{
	fprintf(fp, "Instruction = {\n");
	dump_field(fp, "prefixes", "\t",	&insn->prefixes);
	dump_field(fp, "rex_prefix", "\t",	&insn->rex_prefix);
	dump_field(fp, "vex_prefix", "\t",	&insn->vex_prefix);
	dump_field(fp, "opcode", "\t",		&insn->opcode);
	dump_field(fp, "modrm", "\t",		&insn->modrm);
	dump_field(fp, "sib", "\t",		&insn->sib);
	dump_field(fp, "displacement", "\t",	&insn->displacement);
	dump_field(fp, "immediate1", "\t",	&insn->immediate1);
	dump_field(fp, "immediate2", "\t",	&insn->immediate2);
	fprintf(fp, "\t.attr = %x, .opnd_bytes = %d, .addr_bytes = %d,\n",
		insn->attr, insn->opnd_bytes, insn->addr_bytes);
	fprintf(fp, "\t.length = %d, .x86_64 = %d, .kaddr = %p}\n",
		insn->length, insn->x86_64, insn->kaddr);
}

static void parse_args(int argc, char **argv)
{
	int c;
	prog = argv[0];
	while ((c = getopt(argc, argv, "ynv")) != -1) {
		switch (c) {
		case 'y':
			x86_64 = 1;
			break;
		case 'n':
			x86_64 = 0;
			break;
		case 'v':
			verbose = 1;
			break;
		default:
			usage();
		}
	}
}

#define BUFSIZE 256

int main(int argc, char **argv)
{
	char line[BUFSIZE], sym[BUFSIZE] = "<unknown>";
	unsigned char insn_buf[16];
	struct insn insn;
	int insns = 0;
	int warnings = 0;

	parse_args(argc, argv);

	while (fgets(line, BUFSIZE, stdin)) {
		char copy[BUFSIZE], *s, *tab1, *tab2;
		int nb = 0;
		unsigned int b;

		if (line[0] == '<') {
			/* Symbol line */
			strcpy(sym, line);
			continue;
		}

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
		insn_init(&insn, insn_buf, sizeof(insn_buf), x86_64);
		insn_get_length(&insn);
		if (insn.length != nb) {
			warnings++;
			fprintf(stderr, "Warning: %s found difference at %s\n",
				prog, sym);
			fprintf(stderr, "Warning: %s", line);
			fprintf(stderr, "Warning: objdump says %d bytes, but "
				"insn_get_length() says %d\n", nb,
				insn.length);
			if (verbose)
				dump_insn(stderr, &insn);
		}
	}
	if (warnings)
		fprintf(stderr, "Warning: decoded and checked %d"
			" instructions with %d warnings\n", insns, warnings);
	else
		fprintf(stderr, "Succeed: decoded and checked %d"
			" instructions\n", insns);
	return 0;
}
