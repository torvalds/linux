// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * x86 decoder sanity test - based on test_get_insn.c
 *
 * Copyright (C) IBM Corporation, 2009
 * Copyright (C) Hitachi, Ltd., 2011
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#define unlikely(cond) (cond)
#define ARRAY_SIZE(a)	(sizeof(a)/sizeof(a[0]))

#include <asm/insn.h>
#include <inat.c>
#include <insn.c>

/*
 * Test of instruction analysis against tampering.
 * Feed random binary to instruction decoder and ensure not to
 * access out-of-instruction-buffer.
 */

#define DEFAULT_MAX_ITER	10000
#define INSN_NOP 0x90

static const char	*prog;		/* Program name */
static int		verbose;	/* Verbosity */
static int		x86_64;		/* x86-64 bit mode flag */
static unsigned int	seed;		/* Random seed */
static unsigned long	iter_start;	/* Start of iteration number */
static unsigned long	iter_end = DEFAULT_MAX_ITER;	/* End of iteration number */
static FILE		*input_file;	/* Input file name */

static void usage(const char *err)
{
	if (err)
		fprintf(stderr, "%s: Error: %s\n\n", prog, err);
	fprintf(stderr, "Usage: %s [-y|-n|-v] [-s seed[,no]] [-m max] [-i input]\n", prog);
	fprintf(stderr, "\t-y	64bit mode\n");
	fprintf(stderr, "\t-n	32bit mode\n");
	fprintf(stderr, "\t-v	Verbosity(-vv dumps any decoded result)\n");
	fprintf(stderr, "\t-s	Give a random seed (and iteration number)\n");
	fprintf(stderr, "\t-m	Give a maximum iteration number\n");
	fprintf(stderr, "\t-i	Give an input file with decoded binary\n");
	exit(1);
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

static void dump_stream(FILE *fp, const char *msg, unsigned long nr_iter,
			unsigned char *insn_buf, struct insn *insn)
{
	int i;

	fprintf(fp, "%s:\n", msg);

	dump_insn(fp, insn);

	fprintf(fp, "You can reproduce this with below command(s);\n");

	/* Input a decoded instruction sequence directly */
	fprintf(fp, " $ echo ");
	for (i = 0; i < MAX_INSN_SIZE; i++)
		fprintf(fp, " %02x", insn_buf[i]);
	fprintf(fp, " | %s -i -\n", prog);

	if (!input_file) {
		fprintf(fp, "Or \n");
		/* Give a seed and iteration number */
		fprintf(fp, " $ %s -s 0x%x,%lu\n", prog, seed, nr_iter);
	}
}

static void init_random_seed(void)
{
	int fd;

	fd = open("/dev/urandom", O_RDONLY);
	if (fd < 0)
		goto fail;

	if (read(fd, &seed, sizeof(seed)) != sizeof(seed))
		goto fail;

	close(fd);
	return;
fail:
	usage("Failed to open /dev/urandom");
}

/* Read given instruction sequence from the input file */
static int read_next_insn(unsigned char *insn_buf)
{
	char buf[256]  = "", *tmp;
	int i;

	tmp = fgets(buf, ARRAY_SIZE(buf), input_file);
	if (tmp == NULL || feof(input_file))
		return 0;

	for (i = 0; i < MAX_INSN_SIZE; i++) {
		insn_buf[i] = (unsigned char)strtoul(tmp, &tmp, 16);
		if (*tmp != ' ')
			break;
	}

	return i;
}

static int generate_insn(unsigned char *insn_buf)
{
	int i;

	if (input_file)
		return read_next_insn(insn_buf);

	/* Fills buffer with random binary up to MAX_INSN_SIZE */
	for (i = 0; i < MAX_INSN_SIZE - 1; i += 2)
		*(unsigned short *)(&insn_buf[i]) = random() & 0xffff;

	while (i < MAX_INSN_SIZE)
		insn_buf[i++] = random() & 0xff;

	return i;
}

static void parse_args(int argc, char **argv)
{
	int c;
	char *tmp = NULL;
	int set_seed = 0;

	prog = argv[0];
	while ((c = getopt(argc, argv, "ynvs:m:i:")) != -1) {
		switch (c) {
		case 'y':
			x86_64 = 1;
			break;
		case 'n':
			x86_64 = 0;
			break;
		case 'v':
			verbose++;
			break;
		case 'i':
			if (strcmp("-", optarg) == 0)
				input_file = stdin;
			else
				input_file = fopen(optarg, "r");
			if (!input_file)
				usage("Failed to open input file");
			break;
		case 's':
			seed = (unsigned int)strtoul(optarg, &tmp, 0);
			if (*tmp == ',') {
				optarg = tmp + 1;
				iter_start = strtoul(optarg, &tmp, 0);
			}
			if (*tmp != '\0' || tmp == optarg)
				usage("Failed to parse seed");
			set_seed = 1;
			break;
		case 'm':
			iter_end = strtoul(optarg, &tmp, 0);
			if (*tmp != '\0' || tmp == optarg)
				usage("Failed to parse max_iter");
			break;
		default:
			usage(NULL);
		}
	}

	/* Check errors */
	if (iter_end < iter_start)
		usage("Max iteration number must be bigger than iter-num");

	if (set_seed && input_file)
		usage("Don't use input file (-i) with random seed (-s)");

	/* Initialize random seed */
	if (!input_file) {
		if (!set_seed)	/* No seed is given */
			init_random_seed();
		srand(seed);
	}
}

int main(int argc, char **argv)
{
	struct insn insn;
	int insns = 0;
	int errors = 0;
	unsigned long i;
	unsigned char insn_buf[MAX_INSN_SIZE * 2];

	parse_args(argc, argv);

	/* Prepare stop bytes with NOPs */
	memset(insn_buf + MAX_INSN_SIZE, INSN_NOP, MAX_INSN_SIZE);

	for (i = 0; i < iter_end; i++) {
		if (generate_insn(insn_buf) <= 0)
			break;

		if (i < iter_start)	/* Skip to given iteration number */
			continue;

		/* Decode an instruction */
		insn_init(&insn, insn_buf, sizeof(insn_buf), x86_64);
		insn_get_length(&insn);

		if (insn.next_byte <= insn.kaddr ||
		    insn.kaddr + MAX_INSN_SIZE < insn.next_byte) {
			/* Access out-of-range memory */
			dump_stream(stderr, "Error: Found an access violation", i, insn_buf, &insn);
			errors++;
		} else if (verbose && !insn_complete(&insn))
			dump_stream(stdout, "Info: Found an undecodable input", i, insn_buf, &insn);
		else if (verbose >= 2)
			dump_insn(stdout, &insn);
		insns++;
	}

	fprintf((errors) ? stderr : stdout,
		"%s: %s: decoded and checked %d %s instructions with %d errors (seed:0x%x)\n",
		prog,
		(errors) ? "Failure" : "Success",
		insns,
		(input_file) ? "given" : "random",
		errors,
		seed);

	return errors ? 1 : 0;
}
