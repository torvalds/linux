// SPDX-License-Identifier: MIT
/*
 * Copyright © 2023 Intel Corporation
 */

#define _GNU_SOURCE
#include <ctype.h>
#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>

#define HEADER \
	"// SPDX-License-Identifier: MIT\n" \
	"\n" \
	"/*\n" \
	" * DO NOT MODIFY.\n" \
	" *\n" \
	" * This file was generated from rules: %s\n" \
	" */\n" \
	"#ifndef _GENERATED_XE_WA_OOB_\n" \
	"#define _GENERATED_XE_WA_OOB_\n" \
	"\n" \
	"enum {\n"

#define FOOTER \
	"};\n" \
	"\n" \
	"#endif\n"

static void print_usage(FILE *f)
{
	fprintf(f, "usage: %s <input-rule-file> <generated-c-source-file> <generated-c-header-file>\n",
		program_invocation_short_name);
}

static void print_parse_error(const char *err_msg, const char *line,
			      unsigned int lineno)
{
	fprintf(stderr, "ERROR: %s\nERROR: %u: %.60s\n",
		err_msg, lineno, line);
}

static char *strip(char *line, size_t linelen)
{
	while (isspace(*(line + linelen)))
		linelen--;

	line[linelen - 1] = '\0';

	return  line + strspn(line, " \f\n\r\t\v");
}

#define MAX_LINE_LEN 4096
static int parse(FILE *input, FILE *csource, FILE *cheader)
{
	char line[MAX_LINE_LEN + 1];
	char *name, *prev_name = NULL, *rules;
	unsigned int lineno = 0, idx = 0;

	while (fgets(line, sizeof(line), input)) {
		size_t linelen;
		bool is_continuation;

		if (line[0] == '\0' || line[0] == '#' || line[0] == '\n') {
			lineno++;
			continue;
		}

		linelen = strlen(line);
		if (linelen == MAX_LINE_LEN) {
			print_parse_error("line too long", line, lineno);
			return -EINVAL;
		}

		is_continuation = isspace(line[0]);
		name = strip(line, linelen);

		if (!is_continuation) {
			name = strtok(name, " \t");
			rules = strtok(NULL, "");
		} else {
			if (!prev_name) {
				print_parse_error("invalid rule continuation",
						  line, lineno);
				return -EINVAL;
			}

			rules = name;
			name = NULL;
		}

		if (rules[0] == '\0') {
			print_parse_error("invalid empty rule\n", line, lineno);
			return -EINVAL;
		}

		if (name) {
			fprintf(cheader, "\tXE_WA_OOB_%s = %u,\n", name, idx);

			/* Close previous entry before starting a new one */
			if (idx)
				fprintf(csource, ") },\n");

			fprintf(csource, "{ XE_RTP_NAME(\"%s\"),\n  XE_RTP_RULES(%s",
				name, rules);
			idx++;
		} else {
			fprintf(csource, ", OR,\n\t%s", rules);
		}

		lineno++;
		if (!is_continuation)
			prev_name = name;
	}

	/* Close last entry */
	if (idx)
		fprintf(csource, ") },\n");

	fprintf(cheader, "\t_XE_WA_OOB_COUNT = %u\n", idx);

	return 0;
}

int main(int argc, const char *argv[])
{
	enum {
		ARGS_INPUT,
		ARGS_CSOURCE,
		ARGS_CHEADER,
		_ARGS_COUNT
	};
	struct {
		const char *fn;
		const char *mode;
		FILE *f;
	} args[] = {
		[ARGS_INPUT] = { .fn = argv[1], .mode = "r" },
		[ARGS_CSOURCE] = { .fn = argv[2], .mode = "w" },
		[ARGS_CHEADER] = { .fn = argv[3], .mode = "w" },
	};
	int ret = 1;

	if (argc < 3) {
		fprintf(stderr, "ERROR: wrong arguments\n");
		print_usage(stderr);
		return 1;
	}

	for (int i = 0; i < _ARGS_COUNT; i++) {
		args[i].f = fopen(args[i].fn, args[i].mode);
		if (!args[i].f) {
			fprintf(stderr, "ERROR: Can't open %s: %m\n",
				args[i].fn);
			goto err;
		}
	}

	fprintf(args[ARGS_CHEADER].f, HEADER, args[ARGS_INPUT].fn);
	ret = parse(args[ARGS_INPUT].f, args[ARGS_CSOURCE].f,
		    args[ARGS_CHEADER].f);
	if (!ret)
		fprintf(args[ARGS_CHEADER].f, FOOTER);

err:
	for (int i = 0; i < _ARGS_COUNT; i++) {
		if (args[i].f)
			fclose(args[i].f);
	}

	return ret;
}
