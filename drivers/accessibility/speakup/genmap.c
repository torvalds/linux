// SPDX-License-Identifier: GPL-2.0+
/* genmap.c
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

struct st_key_init {
	char *name;
	int value, shift;
};

static unsigned char key_data[MAXKEYVAL][16], *kp;

#include "mapdata.h"

static const char delims[] = "\t\n ";
static char *cp;
static int map_ver = 119; /* an arbitrary number so speakup can check */
static int shift_table[17];
static int max_states = 1, flags;
/* flags reserved for later, maybe for individual console maps */

static int get_shift_value(int state)
{
	int i;

	for (i = 0; shift_table[i] != state; i++) {
		if (shift_table[i] == -1) {
			if (i >= 16)
				oops("too many shift states", NULL);
			shift_table[i] = state;
			max_states = i+1;
		break;
	}
	}
	return i;
}

int
main(int argc, char *argv[])
{
	int value, shift_state, i, spk_val = 0, lock_val = 0;
	int max_key_used = 0, num_keys_used = 0;
	struct st_key *this;
	struct st_key_init *p_init;
	char buffer[256];

	bzero(key_table, sizeof(key_table));
	bzero(key_data, sizeof(key_data));

	shift_table[0] = 0;
	for (i = 1; i <= 16; i++)
		shift_table[i] = -1;

	if (argc < 2) {
		fputs("usage: genmap filename\n", stderr);
		exit(1);
	}

	for (p_init = init_key_data; p_init->name[0] != '.'; p_init++)
		add_key(p_init->name, p_init->value, p_init->shift);

	open_input(NULL, argv[1]);
	while (fgets(buffer, sizeof(buffer), infile)) {
		lc++;
		value = shift_state = 0;

		cp = strtok(buffer, delims);
		if (*cp == '#')
			continue;

		while (cp) {
			if (*cp == '=')
				break;
			this = find_key(cp);
			if (this == NULL)
				oops("unknown key/modifier", cp);
			if (this->shift == is_shift) {
				if (value)
					oops("modifiers must come first", cp);
				shift_state += this->value;
			} else if (this->shift == is_input)
				value = this->value;
			else
				oops("bad modifier or key", cp);
			cp = strtok(0, delims);
		}
		if (!cp)
			oops("no = found", NULL);

		cp = strtok(0, delims);
		if (!cp)
			oops("no speakup function after =", NULL);

		this = find_key(cp);
		if (this == NULL || this->shift != is_spk)
			oops("invalid speakup function", cp);

		i = get_shift_value(shift_state);
		if (key_data[value][i]) {
			while (--cp > buffer)
				if (!*cp)
					*cp = ' ';
			oops("two functions on same key combination", cp);
		}
		key_data[value][i] = (char)this->value;
		if (value > max_key_used)
			max_key_used = value;
	}
	fclose(infile);

	this = find_key("spk_key");
	if (this)
		spk_val = this->value;

	this = find_key("spk_lock");
	if (this)
		lock_val = this->value;

	for (lc = 1; lc <= max_key_used; lc++) {
		kp = key_data[lc];
		if (!memcmp(key_data[0], kp, 16))
			continue;
		num_keys_used++;
		for (i = 0; i < max_states; i++) {
			if (kp[i] != spk_val && kp[i] != lock_val)
				continue;
			shift_state = shift_table[i];
			if (shift_state&16)
				continue;
			shift_state = get_shift_value(shift_state+16);
			kp[shift_state] = kp[i];
			/* fill in so we can process the key up, as spk bit will be set */
		}
	}

	printf("\t%d, %d, %d,\n\t", map_ver, num_keys_used, max_states);
	for (i = 0; i < max_states; i++)
		printf("%d, ", shift_table[i]);
	printf("%d,", flags);
	for (lc = 1; lc <= max_key_used; lc++) {
		kp = key_data[lc];
		if (!memcmp(key_data[0], kp, 16))
			continue;
		printf("\n\t%d,", lc);
		for (i = 0; i < max_states; i++)
			printf(" %d,", (unsigned int)kp[i]);
	}
	printf("\n\t0, %d\n", map_ver);

	exit(0);
}
