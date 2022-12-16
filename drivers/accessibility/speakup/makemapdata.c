// SPDX-License-Identifier: GPL-2.0+
/* makemapdata.c
 * originally written by: Kirk Reiser.
 *
 ** Copyright (C) 2002  Kirk Reiser.
 *  Copyright (C) 2003  David Borowski.
 */

#include <stdlib.h>
#include <stdio.h>
#include <libgen.h>
#include <string.h>
#include <linux/version.h>
#include <ctype.h>
#include "utils.h"

static char buffer[256];

static int get_define(void)
{
	char *c;

	while (fgets(buffer, sizeof(buffer)-1, infile)) {
		lc++;
		if (strncmp(buffer, "#define", 7))
			continue;
		c = buffer + 7;
		while (*c == ' ' || *c == '\t')
			c++;
		def_name = c;
		while (*c && *c != ' ' && *c != '\t' && *c != '\n')
			c++;
		if (!*c || *c == '\n')
			continue;
		*c++ = '\0';
		while (*c == ' ' || *c == '\t' || *c == '(')
			c++;
		def_val = c;
		while (*c && *c != '\n' && *c != ')')
			c++;
		*c++ = '\0';
		return 1;
	}
	fclose(infile);
	infile = 0;
	return 0;
}

int
main(int argc, char *argv[])
{
	int value, i;
	struct st_key *this;
	const char *dir_name, *spk_dir_name;
	char *cp;

	dir_name = getenv("TOPDIR");
	if (!dir_name)
		dir_name = ".";
	spk_dir_name = getenv("SPKDIR");
	if (!spk_dir_name)
		spk_dir_name = "drivers/accessibility/speakup";
	bzero(key_table, sizeof(key_table));
	add_key("shift",	1, is_shift);
	add_key("altgr",	2, is_shift);
	add_key("ctrl",	4, is_shift);
	add_key("alt",	8, is_shift);
	add_key("spk", 16, is_shift);
	add_key("double", 32, is_shift);

	open_input(dir_name, "include/linux/input.h");
	while (get_define()) {
		if (strncmp(def_name, "KEY_", 4))
			continue;
		value = atoi(def_val);
		if (value > 0 && value < MAXKEYVAL)
			add_key(def_name, value, is_input);
	}

	open_input(dir_name, "include/uapi/linux/input-event-codes.h");
	while (get_define()) {
		if (strncmp(def_name, "KEY_", 4))
			continue;
		value = atoi(def_val);
		if (value > 0 && value < MAXKEYVAL)
			add_key(def_name, value, is_input);
	}

	open_input(spk_dir_name, "spk_priv_keyinfo.h");
	while (get_define()) {
		if (strlen(def_val) > 5) {
			//if (def_val[0] == '(')
			//	def_val++;
			cp = strchr(def_val, '+');
			if (!cp)
				continue;
			if (cp[-1] == ' ')
				cp[-1] = '\0';
			*cp++ = '\0';
			this = find_key(def_val);
			while (*cp == ' ')
				cp++;
			if (!this || *cp < '0' || *cp > '9')
				continue;
			value = this->value+atoi(cp);
		} else if (!strncmp(def_val, "0x", 2))
			sscanf(def_val+2, "%x", &value);
		else if (*def_val >= '0' && *def_val <= '9')
			value = atoi(def_val);
		else
			continue;
		add_key(def_name, value, is_spk);
	}

	printf("struct st_key_init init_key_data[] = {\n");
	for (i = 0; i < HASHSIZE; i++) {
		this = &key_table[i];
		if (!this->name)
			continue;
		do {
			printf("\t{ \"%s\", %d, %d, },\n", this->name, this->value, this->shift);
			this = this->next;
		} while (this);
	}
	printf("\t{ \".\", 0, 0 }\n};\n");

	exit(0);
}
