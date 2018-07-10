/* vi: set sw=4 ts=4: */
/*
 * Mini weak password checker implementation for busybox
 *
 * Copyright (C) 2006 Tito Ragusa <farmatito@tiscali.it>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */

/*	A good password:
	1)	should contain at least six characters (man passwd);
	2)	empty passwords are not permitted;
	3)	should contain a mix of four different types of characters
		upper case letters,
		lower case letters,
		numbers,
		special characters such as !@#$%^&*,;".
	This password types should not  be permitted:
	a)	pure numbers: birthdates, social security number, license plate, phone numbers;
	b)	words and all letters only passwords (uppercase, lowercase or mixed)
		as palindromes, consecutive or repetitive letters
		or adjacent letters on your keyboard;
	c)	username, real name, company name or (e-mail?) address
		in any form (as-is, reversed, capitalized, doubled, etc.).
		(we can check only against username, gecos and hostname)
	d)	common and obvious letter-number replacements
		(e.g. replace the letter O with number 0)
		such as "M1cr0$0ft" or "P@ssw0rd" (CAVEAT: we cannot check for them
		without the use of a dictionary).

	For each missing type of characters an increase of password length is
	requested.

	If user is root we warn only.

	CAVEAT: some older versions of crypt() truncates passwords to 8 chars,
	so that aaaaaaaa1Q$ is equal to aaaaaaaa making it possible to fool
	some of our checks. We don't test for this special case as newer versions
	of crypt do not truncate passwords.
*/

#include "libbb.h"

static int string_checker_helper(const char *p1, const char *p2) __attribute__ ((__pure__));

static int string_checker_helper(const char *p1, const char *p2)
{
	/* as sub-string */
	if (strcasestr(p2, p1) != NULL
	/* invert in case haystack is shorter than needle */
	 || strcasestr(p1, p2) != NULL
	/* as-is or capitalized */
	/* || strcasecmp(p1, p2) == 0 - 1st strcasestr should catch this too */
	) {
		return 1;
	}
	return 0;
}

static int string_checker(const char *p1, const char *p2)
{
	int size, i;
	/* check string */
	int ret = string_checker_helper(p1, p2);
	/* make our own copy */
	char *p = xstrdup(p1);

	/* reverse string */
	i = size = strlen(p1);
	while (--i >= 0) {
		*p++ = p1[i];
	}
	p -= size; /* restore pointer */

	/* check reversed string */
	ret |= string_checker_helper(p, p2);

	/* clean up */
	nuke_str(p);
	free(p);

	return ret;
}

#define CATEGORIES  4

#define LOWERCASE   1
#define UPPERCASE   2
#define NUMBERS     4
#define SPECIAL     8

#define LAST_CAT    8

static const char *obscure_msg(const char *old_p, const char *new_p, const struct passwd *pw)
{
	unsigned length;
	unsigned size;
	unsigned mixed;
	unsigned c;
	unsigned i;
	const char *p;
	char *hostname;

	/* size */
	if (!new_p || (length = strlen(new_p)) < CONFIG_PASSWORD_MINLEN)
		return "too short";

	/* no username as-is, as sub-string, reversed, capitalized, doubled */
	if (string_checker(new_p, pw->pw_name)) {
		return "similar to username";
	}
#ifndef __BIONIC__
	/* no gecos as-is, as sub-string, reversed, capitalized, doubled */
	if (pw->pw_gecos[0] && string_checker(new_p, pw->pw_gecos)) {
		return "similar to gecos";
	}
#endif
	/* hostname as-is, as sub-string, reversed, capitalized, doubled */
	hostname = safe_gethostname();
	i = string_checker(new_p, hostname);
	free(hostname);
	if (i)
		return "similar to hostname";

	/* Should / Must contain a mix of: */
	mixed = 0;
	for (i = 0; i < length; i++) {
		if (islower(new_p[i])) {        /* a-z */
			mixed |= LOWERCASE;
		} else if (isupper(new_p[i])) { /* A-Z */
			mixed |= UPPERCASE;
		} else if (isdigit(new_p[i])) { /* 0-9 */
			mixed |= NUMBERS;
		} else  {                       /* special characters */
			mixed |= SPECIAL;
		}
		/* Count i'th char */
		c = 0;
		p = new_p;
		while (1) {
			p = strchr(p, new_p[i]);
			if (p == NULL) {
				break;
			}
			c++;
			p++;
			if (!*p) {
				break;
			}
		}
		/* More than 50% similar characters ? */
		if (c*2 >= length) {
			return "too many similar characters";
		}
	}

	size = CONFIG_PASSWORD_MINLEN + 2*CATEGORIES;
	for (i = 1; i <= LAST_CAT; i <<= 1)
		if (mixed & i)
			size -= 2;
	if (length < size)
		return "too weak";

	if (old_p && old_p[0]) {
		/* check vs. old password */
		if (string_checker(new_p, old_p)) {
			return "similar to old password";
		}
	}

	return NULL;
}

int FAST_FUNC obscure(const char *old, const char *newval, const struct passwd *pw)
{
	const char *msg;

	msg = obscure_msg(old, newval, pw);
	if (msg) {
		printf("Bad password: %s\n", msg);
		return 1;
	}
	return 0;
}

#if ENABLE_UNIT_TEST

/* Test obscure_msg() instead of obscure() in order not to print anything. */

static const struct passwd pw = {
	.pw_name = (char *)"johndoe",
	.pw_gecos = (char *)"John Doe",
};

BBUNIT_DEFINE_TEST(obscure_weak_pass)
{
	/* Empty password */
	BBUNIT_ASSERT_NOTNULL(obscure_msg("Ad4#21?'S|", "", &pw));
	/* Pure numbers */
	BBUNIT_ASSERT_NOTNULL(obscure_msg("Ad4#21?'S|", "23577315", &pw));
	/* Similar to pw_name */
	BBUNIT_ASSERT_NOTNULL(obscure_msg("Ad4#21?'S|", "johndoe123%", &pw));
	/* Similar to pw_gecos, reversed */
	BBUNIT_ASSERT_NOTNULL(obscure_msg("Ad4#21?'S|", "eoD nhoJ^44@", &pw));
	/* Similar to the old password */
	BBUNIT_ASSERT_NOTNULL(obscure_msg("Ad4#21?'S|", "d4#21?'S", &pw));
	/* adjacent letters */
	BBUNIT_ASSERT_NOTNULL(obscure_msg("Ad4#21?'S|", "qwerty123", &pw));
	/* Many similar chars */
	BBUNIT_ASSERT_NOTNULL(obscure_msg("Ad4#21?'S|", "^33Daaaaaa1", &pw));

	BBUNIT_ENDTEST;
}

BBUNIT_DEFINE_TEST(obscure_strong_pass)
{
	BBUNIT_ASSERT_NULL(obscure_msg("Rt4##2&:'|", "}(^#rrSX3S*22", &pw));

	BBUNIT_ENDTEST;
}

#endif /* ENABLE_UNIT_TEST */
