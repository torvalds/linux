/* SPDX-License-Identifier: GPL-2.0+ */
/* utils.h
 * originally written by: Kirk Reiser.
 *
 ** Copyright (C) 2002  Kirk Reiser.
 *  Copyright (C) 2003  David Borowski.
 */

#include <stdio.h>

#define MAXKEYS 512
#define MAXKEYVAL 160
#define HASHSIZE 101
#define is_shift -3
#define is_spk -2
#define is_input -1

struct st_key {
	char *name;
	struct st_key *next;
	int value, shift;
};

struct st_key key_table[MAXKEYS];
struct st_key *extra_keys = key_table+HASHSIZE;
char *def_name, *def_val;
FILE *infile;
int lc;

char filename[256];

static inline void open_input(const char *dir_name, const char *name)
{
	if (dir_name)
		snprintf(filename, sizeof(filename), "%s/%s", dir_name, name);
	else
		snprintf(filename, sizeof(filename), "%s", name);
	infile = fopen(filename, "r");
	if (infile == 0) {
		fprintf(stderr, "can't open %s\n", filename);
		exit(1);
	}
	lc = 0;
}

static inline int oops(const char *msg, const char *info)
{
	if (info == NULL)
		info = "";
	fprintf(stderr, "error: file %s line %d\n", filename, lc);
	fprintf(stderr, "%s %s\n", msg, info);
	exit(1);
}

static inline struct st_key *hash_name(char *name)
{
	unsigned char *pn = (unsigned char *)name;
	int hash = 0;

	while (*pn) {
		hash = (hash * 17) & 0xfffffff;
		if (isupper(*pn))
			*pn = tolower(*pn);
		hash += (int)*pn;
		pn++;
	}
	hash %= HASHSIZE;
	return &key_table[hash];
}

static inline struct st_key *find_key(char *name)
{
	struct st_key *this = hash_name(name);

	while (this) {
		if (this->name && !strcmp(name, this->name))
			return this;
		this = this->next;
	}
	return this;
}

static inline struct st_key *add_key(char *name, int value, int shift)
{
	struct st_key *this = hash_name(name);

	if (extra_keys-key_table >= MAXKEYS)
		oops("out of key table space, enlarge MAXKEYS", NULL);
	if (this->name != NULL) {
		while (this->next) {
			if (!strcmp(name, this->name))
				oops("attempt to add duplicate key", name);
			this = this->next;
		}
		this->next = extra_keys++;
		this = this->next;
	}
	this->name = strdup(name);
	this->value = value;
	this->shift = shift;
	return this;
}
