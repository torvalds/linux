/*
 * Copyright (c) 2000-2002 by Solar Designer. See LICENSE.
 */

#ifndef _PASSWDQC_H
#define _PASSWDQC_H

#include <pwd.h>

typedef struct {
	int min[5], max;
	int passphrase_words;
	int match_length;
	int similar_deny;
	int random_bits;
} passwdqc_params_t;

extern char _passwdqc_wordset_4k[0x1000][6];

extern const char *_passwdqc_check(passwdqc_params_t *params,
    const char *newpass, const char *oldpass, struct passwd *pw);
extern char *_passwdqc_random(passwdqc_params_t *params);

#endif
