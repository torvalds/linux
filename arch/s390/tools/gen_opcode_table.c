/*
 * Generate opcode table initializers for the in-kernel disassembler.
 *
 *    Copyright IBM Corp. 2017
 *
 */

#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>

#define STRING_SIZE_MAX 20

struct insn_type {
	unsigned char byte;
	unsigned char mask;
	char **format;
};

struct insn {
	struct insn_type *type;
	char opcode[STRING_SIZE_MAX];
	char name[STRING_SIZE_MAX];
	char upper[STRING_SIZE_MAX];
	char format[STRING_SIZE_MAX];
	unsigned int name_len;
};

struct insn_group {
	struct insn_type *type;
	int offset;
	int count;
	char opcode[2];
};

struct insn_format {
	char *format;
	int type;
};

struct gen_opcode {
	struct insn *insn;
	int nr;
	struct insn_group *group;
	int nr_groups;
};

/*
 * Table of instruction format types. Each opcode is defined with at
 * least one byte (two nibbles), three nibbles, or two bytes (four
 * nibbles).
 * The byte member of each instruction format type entry defines
 * within which byte of an instruction the third (and fourth) nibble
 * of an opcode can be found. The mask member is the and-mask that
 * needs to be applied on this byte in order to get the third (and
 * fourth) nibble of the opcode.
 * The format array defines all instruction formats (as defined in the
 * Principles of Operation) which have the same position of the opcode
 * nibbles.
 * A special case are instruction formats with 1-byte opcodes. In this
 * case the byte member always is zero, so that the mask is applied on
 * the (only) byte that contains the opcode.
 */
static struct insn_type insn_type_table[] = {
	{
		.byte = 0,
		.mask = 0xff,
		.format = (char *[]) {
			"MII",
			"RR",
			"RS",
			"RSI",
			"RX",
			"SI",
			"SMI",
			"SS",
			NULL,
		},
	},
	{
		.byte = 1,
		.mask = 0x0f,
		.format = (char *[]) {
			"RI",
			"RIL",
			"SSF",
			NULL,
		},
	},
	{
		.byte = 1,
		.mask = 0xff,
		.format = (char *[]) {
			"E",
			"IE",
			"RRE",
			"RRF",
			"RRR",
			"S",
			"SIL",
			"SSE",
			NULL,
		},
	},
	{
		.byte = 5,
		.mask = 0xff,
		.format = (char *[]) {
			"RIE",
			"RIS",
			"RRS",
			"RSE",
			"RSL",
			"RSY",
			"RXE",
			"RXF",
			"RXY",
			"SIY",
			"VRI",
			"VRR",
			"VRS",
			"VRV",
			"VRX",
			"VSI",
			NULL,
		},
	},
};

static struct insn_type *insn_format_to_type(char *format)
{
	char tmp[STRING_SIZE_MAX];
	char *base_format, **ptr;
	int i;

	strcpy(tmp, format);
	base_format = tmp;
	base_format = strsep(&base_format, "_");
	for (i = 0; i < sizeof(insn_type_table) / sizeof(insn_type_table[0]); i++) {
		ptr = insn_type_table[i].format;
		while (*ptr) {
			if (!strcmp(base_format, *ptr))
				return &insn_type_table[i];
			ptr++;
		}
	}
	exit(EXIT_FAILURE);
}

static void read_instructions(struct gen_opcode *desc)
{
	struct insn insn;
	int rc, i;

	while (1) {
		rc = scanf("%s %s %s", insn.opcode, insn.name, insn.format);
		if (rc == EOF)
			break;
		if (rc != 3)
			exit(EXIT_FAILURE);
		insn.type = insn_format_to_type(insn.format);
		insn.name_len = strlen(insn.name);
		for (i = 0; i <= insn.name_len; i++)
			insn.upper[i] = toupper((unsigned char)insn.name[i]);
		desc->nr++;
		desc->insn = realloc(desc->insn, desc->nr * sizeof(*desc->insn));
		if (!desc->insn)
			exit(EXIT_FAILURE);
		desc->insn[desc->nr - 1] = insn;
	}
}

static int cmpformat(const void *a, const void *b)
{
	return strcmp(((struct insn *)a)->format, ((struct insn *)b)->format);
}

static void print_formats(struct gen_opcode *desc)
{
	char *format;
	int i, count;

	qsort(desc->insn, desc->nr, sizeof(*desc->insn), cmpformat);
	format = "";
	count = 0;
	printf("enum {\n");
	for (i = 0; i < desc->nr; i++) {
		if (!strcmp(format, desc->insn[i].format))
			continue;
		count++;
		format = desc->insn[i].format;
		printf("\tINSTR_%s,\n", format);
	}
	printf("}; /* %d */\n\n", count);
}

static int cmp_long_insn(const void *a, const void *b)
{
	return strcmp(((struct insn *)a)->name, ((struct insn *)b)->name);
}

static void print_long_insn(struct gen_opcode *desc)
{
	struct insn *insn;
	int i, count;

	qsort(desc->insn, desc->nr, sizeof(*desc->insn), cmp_long_insn);
	count = 0;
	printf("enum {\n");
	for (i = 0; i < desc->nr; i++) {
		insn = &desc->insn[i];
		if (insn->name_len < 6)
			continue;
		printf("\tLONG_INSN_%s,\n", insn->upper);
		count++;
	}
	printf("}; /* %d */\n\n", count);

	printf("#define LONG_INSN_INITIALIZER { \\\n");
	for (i = 0; i < desc->nr; i++) {
		insn = &desc->insn[i];
		if (insn->name_len < 6)
			continue;
		printf("\t[LONG_INSN_%s] = \"%s\", \\\n", insn->upper, insn->name);
	}
	printf("}\n\n");
}

static void print_opcode(struct insn *insn, int nr)
{
	char *opcode;

	opcode = insn->opcode;
	if (insn->type->byte != 0)
		opcode += 2;
	printf("\t[%4d] = { .opfrag = 0x%s, .format = INSTR_%s, ", nr, opcode, insn->format);
	if (insn->name_len < 6)
		printf(".name = \"%s\" ", insn->name);
	else
		printf(".offset = LONG_INSN_%s ", insn->upper);
	printf("}, \\\n");
}

static void add_to_group(struct gen_opcode *desc, struct insn *insn, int offset)
{
	struct insn_group *group;

	group = desc->group ? &desc->group[desc->nr_groups - 1] : NULL;
	if (group && (!strncmp(group->opcode, insn->opcode, 2) || group->type->byte == 0)) {
		group->count++;
		return;
	}
	desc->nr_groups++;
	desc->group = realloc(desc->group, desc->nr_groups * sizeof(*desc->group));
	if (!desc->group)
		exit(EXIT_FAILURE);
	group = &desc->group[desc->nr_groups - 1];
	strncpy(group->opcode, insn->opcode, 2);
	group->type = insn->type;
	group->offset = offset;
	group->count = 1;
}

static int cmpopcode(const void *a, const void *b)
{
	return strcmp(((struct insn *)a)->opcode, ((struct insn *)b)->opcode);
}

static void print_opcode_table(struct gen_opcode *desc)
{
	char opcode[2] = "";
	struct insn *insn;
	int i, offset;

	qsort(desc->insn, desc->nr, sizeof(*desc->insn), cmpopcode);
	printf("#define OPCODE_TABLE_INITIALIZER { \\\n");
	offset = 0;
	for (i = 0; i < desc->nr; i++) {
		insn = &desc->insn[i];
		if (insn->type->byte == 0)
			continue;
		add_to_group(desc, insn, offset);
		if (strncmp(opcode, insn->opcode, 2)) {
			strncpy(opcode, insn->opcode, 2);
			printf("\t/* %.2s */ \\\n", opcode);
		}
		print_opcode(insn, offset);
		offset++;
	}
	printf("\t/* 1-byte opcode instructions */ \\\n");
	for (i = 0; i < desc->nr; i++) {
		insn = &desc->insn[i];
		if (insn->type->byte != 0)
			continue;
		add_to_group(desc, insn, offset);
		print_opcode(insn, offset);
		offset++;
	}
	printf("}\n\n");
}

static void print_opcode_table_offsets(struct gen_opcode *desc)
{
	struct insn_group *group;
	int i;

	printf("#define OPCODE_OFFSET_INITIALIZER { \\\n");
	for (i = 0; i < desc->nr_groups; i++) {
		group = &desc->group[i];
		printf("\t{ .opcode = 0x%.2s, .mask = 0x%02x, .byte = %d, .offset = %d, .count = %d }, \\\n",
		       group->opcode, group->type->mask, group->type->byte, group->offset, group->count);
	}
	printf("}\n\n");
}

int main(int argc, char **argv)
{
	struct gen_opcode _desc = { 0 };
	struct gen_opcode *desc = &_desc;

	read_instructions(desc);
	printf("#ifndef __S390_GENERATED_DIS_H__\n");
	printf("#define __S390_GENERATED_DIS_H__\n");
	printf("/*\n");
	printf(" * DO NOT MODIFY.\n");
	printf(" *\n");
	printf(" * This file was generated by %s\n", __FILE__);
	printf(" */\n\n");
	print_formats(desc);
	print_long_insn(desc);
	print_opcode_table(desc);
	print_opcode_table_offsets(desc);
	printf("#endif\n");
	exit(EXIT_SUCCESS);
}
