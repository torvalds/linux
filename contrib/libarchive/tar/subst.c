/*-
 * Copyright (c) 2008 Joerg Sonnenberger
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR(S) ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR(S) BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "bsdtar_platform.h"
__FBSDID("$FreeBSD$");

#if defined(HAVE_REGEX_H) || defined(HAVE_PCREPOSIX_H)
#include "bsdtar.h"

#include <errno.h>
#ifdef HAVE_PCREPOSIX_H
#include <pcreposix.h>
#else
#include <regex.h>
#endif
#include <stdlib.h>
#include <string.h>

#ifndef REG_BASIC
#define	REG_BASIC 0
#endif

#include "err.h"

struct subst_rule {
	struct subst_rule *next;
	regex_t re;
	char *result;
	unsigned int global:1, print:1, regular:1, symlink:1, hardlink:1;
};

struct substitution {
	struct subst_rule *first_rule, *last_rule;
};

static void
init_substitution(struct bsdtar *bsdtar)
{
	struct substitution *subst;

	bsdtar->substitution = subst = malloc(sizeof(*subst));
	if (subst == NULL)
		lafe_errc(1, errno, "Out of memory");
	subst->first_rule = subst->last_rule = NULL;
}

void
add_substitution(struct bsdtar *bsdtar, const char *rule_text)
{
	struct subst_rule *rule;
	struct substitution *subst;
	const char *end_pattern, *start_subst;
	char *pattern;
	int r;

	if ((subst = bsdtar->substitution) == NULL) {
		init_substitution(bsdtar);
		subst = bsdtar->substitution;
	}

	rule = malloc(sizeof(*rule));
	if (rule == NULL)
		lafe_errc(1, errno, "Out of memory");
	rule->next = NULL;
	rule->result = NULL;

	if (subst->last_rule == NULL)
		subst->first_rule = rule;
	else
		subst->last_rule->next = rule;
	subst->last_rule = rule;

	if (*rule_text == '\0')
		lafe_errc(1, 0, "Empty replacement string");
	end_pattern = strchr(rule_text + 1, *rule_text);
	if (end_pattern == NULL)
		lafe_errc(1, 0, "Invalid replacement string");

	pattern = malloc(end_pattern - rule_text);
	if (pattern == NULL)
		lafe_errc(1, errno, "Out of memory");
	memcpy(pattern, rule_text + 1, end_pattern - rule_text - 1);
	pattern[end_pattern - rule_text - 1] = '\0';

	if ((r = regcomp(&rule->re, pattern, REG_BASIC)) != 0) {
		char buf[80];
		regerror(r, &rule->re, buf, sizeof(buf));
		lafe_errc(1, 0, "Invalid regular expression: %s", buf);
	}
	free(pattern);

	start_subst = end_pattern + 1;
	end_pattern = strchr(start_subst, *rule_text);
	if (end_pattern == NULL)
		lafe_errc(1, 0, "Invalid replacement string");

	rule->result = malloc(end_pattern - start_subst + 1);
	if (rule->result == NULL)
		lafe_errc(1, errno, "Out of memory");
	memcpy(rule->result, start_subst, end_pattern - start_subst);
	rule->result[end_pattern - start_subst] = '\0';

	/* Defaults */
	rule->global = 0; /* Don't do multiple replacements. */
	rule->print = 0; /* Don't print. */
	rule->regular = 1; /* Rewrite regular filenames. */
	rule->symlink = 1; /* Rewrite symlink targets. */
	rule->hardlink = 1; /* Rewrite hardlink targets. */

	while (*++end_pattern) {
		switch (*end_pattern) {
		case 'g':
		case 'G':
			rule->global = 1;
			break;
		case 'h':
			rule->hardlink = 1;
			break;
		case 'H':
			rule->hardlink = 0;
			break;
		case 'p':
		case 'P':
			rule->print = 1;
			break;
		case 'r':
			rule->regular = 1;
			break;
		case 'R':
			rule->regular = 0;
			break;
		case 's':
			rule->symlink = 1;
			break;
		case 'S':
			rule->symlink = 0;
			break;
		default:
			lafe_errc(1, 0, "Invalid replacement flag %c", *end_pattern);
		}
	}
}

static void
realloc_strncat(char **str, const char *append, size_t len)
{
	char *new_str;
	size_t old_len;

	if (*str == NULL)
		old_len = 0;
	else
		old_len = strlen(*str);

	new_str = malloc(old_len + len + 1);
	if (new_str == NULL)
		lafe_errc(1, errno, "Out of memory");
	if (*str != NULL)
		memcpy(new_str, *str, old_len);
	memcpy(new_str + old_len, append, len);
	new_str[old_len + len] = '\0';
	free(*str);
	*str = new_str;
}

static void
realloc_strcat(char **str, const char *append)
{
	char *new_str;
	size_t old_len;

	if (*str == NULL)
		old_len = 0;
	else
		old_len = strlen(*str);

	new_str = malloc(old_len + strlen(append) + 1);
	if (new_str == NULL)
		lafe_errc(1, errno, "Out of memory");
	if (*str != NULL)
		memcpy(new_str, *str, old_len);
	strcpy(new_str + old_len, append);
	free(*str);
	*str = new_str;
}

int
apply_substitution(struct bsdtar *bsdtar, const char *name, char **result,
    int symlink_target, int hardlink_target)
{
	const char *path = name;
	regmatch_t matches[10];
	size_t i, j;
	struct subst_rule *rule;
	struct substitution *subst;
	int c, got_match, print_match;

	*result = NULL;

	if ((subst = bsdtar->substitution) == NULL)
		return 0;

	got_match = 0;
	print_match = 0;

	for (rule = subst->first_rule; rule != NULL; rule = rule->next) {
		if (symlink_target) {
			if (!rule->symlink)
				continue;
		} else if (hardlink_target) {
			if (!rule->hardlink)
				continue;
		} else { /* Regular filename. */
			if (!rule->regular)
				continue;
		}

		while (1) {
			if (regexec(&rule->re, name, 10, matches, 0))
				break;

			got_match = 1;
			print_match |= rule->print;
			realloc_strncat(result, name, matches[0].rm_so);

			for (i = 0, j = 0; rule->result[i] != '\0'; ++i) {
				if (rule->result[i] == '~') {
					realloc_strncat(result, rule->result + j, i - j);
					realloc_strncat(result,
					    name + matches[0].rm_so,
					    matches[0].rm_eo - matches[0].rm_so);
					j = i + 1;
					continue;
				}
				if (rule->result[i] != '\\')
					continue;

				++i;
				c = rule->result[i];
				switch (c) {
				case '~':
				case '\\':
					realloc_strncat(result, rule->result + j, i - j - 1);
					j = i;
					break;
				case '1':
				case '2':
				case '3':
				case '4':
				case '5':
				case '6':
				case '7':
				case '8':
				case '9':
					realloc_strncat(result, rule->result + j, i - j - 1);
					if ((size_t)(c - '0') > (size_t)(rule->re.re_nsub)) {
						free(*result);
						*result = NULL;
						return -1;
					}
					realloc_strncat(result, name + matches[c - '0'].rm_so, matches[c - '0'].rm_eo - matches[c - '0'].rm_so);
					j = i + 1;
					break;
				default:
					/* Just continue; */
					break;
				}

			}

			realloc_strcat(result, rule->result + j);

			name += matches[0].rm_eo;

			if (!rule->global)
				break;
		}
	}

	if (got_match)
		realloc_strcat(result, name);

	if (print_match)
		fprintf(stderr, "%s >> %s\n", path, *result);

	return got_match;
}

void
cleanup_substitution(struct bsdtar *bsdtar)
{
	struct subst_rule *rule;
	struct substitution *subst;

	if ((subst = bsdtar->substitution) == NULL)
		return;

	while ((rule = subst->first_rule) != NULL) {
		subst->first_rule = rule->next;
		free(rule->result);
		free(rule);
	}
	free(subst);
}
#endif /* defined(HAVE_REGEX_H) || defined(HAVE_PCREPOSIX_H) */
