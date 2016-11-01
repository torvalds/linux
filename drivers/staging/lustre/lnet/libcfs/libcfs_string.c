/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2012, 2015 Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * String manipulation functions.
 *
 * libcfs/libcfs/libcfs_string.c
 *
 * Author: Nathan Rutman <nathan.rutman@sun.com>
 */

#include "../../include/linux/libcfs/libcfs.h"

/* Convert a text string to a bitmask */
int cfs_str2mask(const char *str, const char *(*bit2str)(int bit),
		 int *oldmask, int minmask, int allmask)
{
	const char *debugstr;
	char op = '\0';
	int newmask = minmask, i, len, found = 0;

	/* <str> must be a list of tokens separated by whitespace
	 * and optionally an operator ('+' or '-').  If an operator
	 * appears first in <str>, '*oldmask' is used as the starting point
	 * (relative), otherwise minmask is used (absolute).  An operator
	 * applies to all following tokens up to the next operator.
	 */
	while (*str != '\0') {
		while (isspace(*str))
			str++;
		if (*str == '\0')
			break;
		if (*str == '+' || *str == '-') {
			op = *str++;
			if (!found)
				/* only if first token is relative */
				newmask = *oldmask;
			while (isspace(*str))
				str++;
			if (*str == '\0')  /* trailing op */
				return -EINVAL;
		}

		/* find token length */
		len = 0;
		while (str[len] != '\0' && !isspace(str[len]) &&
		       str[len] != '+' && str[len] != '-')
			len++;

		/* match token */
		found = 0;
		for (i = 0; i < 32; i++) {
			debugstr = bit2str(i);
			if (debugstr && strlen(debugstr) == len &&
			    strncasecmp(str, debugstr, len) == 0) {
				if (op == '-')
					newmask &= ~(1 << i);
				else
					newmask |= (1 << i);
				found = 1;
				break;
			}
		}
		if (!found && len == 3 &&
		    (strncasecmp(str, "ALL", len) == 0)) {
			if (op == '-')
				newmask = minmask;
			else
				newmask = allmask;
			found = 1;
		}
		if (!found) {
			CWARN("unknown mask '%.*s'.\n"
			      "mask usage: [+|-]<all|type> ...\n", len, str);
			return -EINVAL;
		}
		str += len;
	}

	*oldmask = newmask;
	return 0;
}

/* get the first string out of @str */
char *cfs_firststr(char *str, size_t size)
{
	size_t i = 0;
	char  *end;

	/* trim leading spaces */
	while (i < size && *str && isspace(*str)) {
		++i;
		++str;
	}

	/* string with all spaces */
	if (*str == '\0')
		goto out;

	end = str;
	while (i < size && *end != '\0' && !isspace(*end)) {
		++i;
		++end;
	}

	*end = '\0';
out:
	return str;
}
EXPORT_SYMBOL(cfs_firststr);

char *
cfs_trimwhite(char *str)
{
	char *end;

	while (isspace(*str))
		str++;

	end = str + strlen(str);
	while (end > str) {
		if (!isspace(end[-1]))
			break;
		end--;
	}

	*end = 0;
	return str;
}
EXPORT_SYMBOL(cfs_trimwhite);

/**
 * Extracts tokens from strings.
 *
 * Looks for \a delim in string \a next, sets \a res to point to
 * substring before the delimiter, sets \a next right after the found
 * delimiter.
 *
 * \retval 1 if \a res points to a string of non-whitespace characters
 * \retval 0 otherwise
 */
int
cfs_gettok(struct cfs_lstr *next, char delim, struct cfs_lstr *res)
{
	char *end;

	if (!next->ls_str)
		return 0;

	/* skip leading white spaces */
	while (next->ls_len) {
		if (!isspace(*next->ls_str))
			break;
		next->ls_str++;
		next->ls_len--;
	}

	if (next->ls_len == 0) /* whitespaces only */
		return 0;

	if (*next->ls_str == delim) {
		/* first non-writespace is the delimiter */
		return 0;
	}

	res->ls_str = next->ls_str;
	end = memchr(next->ls_str, delim, next->ls_len);
	if (!end) {
		/* there is no the delimeter in the string */
		end = next->ls_str + next->ls_len;
		next->ls_str = NULL;
	} else {
		next->ls_str = end + 1;
		next->ls_len -= (end - res->ls_str + 1);
	}

	/* skip ending whitespaces */
	while (--end != res->ls_str) {
		if (!isspace(*end))
			break;
	}

	res->ls_len = end - res->ls_str + 1;
	return 1;
}
EXPORT_SYMBOL(cfs_gettok);

/**
 * Converts string to integer.
 *
 * Accepts decimal and hexadecimal number recordings.
 *
 * \retval 1 if first \a nob chars of \a str convert to decimal or
 * hexadecimal integer in the range [\a min, \a max]
 * \retval 0 otherwise
 */
int
cfs_str2num_check(char *str, int nob, unsigned *num,
		  unsigned min, unsigned max)
{
	bool all_numbers = true;
	char *endp, cache;
	int rc;

	str = cfs_trimwhite(str);

	/**
	 * kstrouint can only handle strings composed
	 * of only numbers. We need to scan the string
	 * passed in for the first non-digit character
	 * and end the string at that location. If we
	 * don't find any non-digit character we still
	 * need to place a '\0' at position nob since
	 * we are not interested in the rest of the
	 * string which is longer than nob in size.
	 * After we are done the character at the
	 * position we placed '\0' must be restored.
	 */
	for (endp = str; endp < str + nob; endp++) {
		if (!isdigit(*endp)) {
			all_numbers = false;
			break;
		}
	}
	cache = *endp;
	*endp = '\0';

	rc = kstrtouint(str, 10, num);
	*endp = cache;
	if (rc || !all_numbers)
		return 0;

	return (*num >= min && *num <= max);
}
EXPORT_SYMBOL(cfs_str2num_check);

/**
 * Parses \<range_expr\> token of the syntax. If \a bracketed is false,
 * \a src should only have a single token which can be \<number\> or  \*
 *
 * \retval pointer to allocated range_expr and initialized
 * range_expr::re_lo, range_expr::re_hi and range_expr:re_stride if \a
 `* src parses to
 * \<number\> |
 * \<number\> '-' \<number\> |
 * \<number\> '-' \<number\> '/' \<number\>
 * \retval 0 will be returned if it can be parsed, otherwise -EINVAL or
 * -ENOMEM will be returned.
 */
static int
cfs_range_expr_parse(struct cfs_lstr *src, unsigned min, unsigned max,
		     int bracketed, struct cfs_range_expr **expr)
{
	struct cfs_range_expr	*re;
	struct cfs_lstr		tok;

	LIBCFS_ALLOC(re, sizeof(*re));
	if (!re)
		return -ENOMEM;

	if (src->ls_len == 1 && src->ls_str[0] == '*') {
		re->re_lo = min;
		re->re_hi = max;
		re->re_stride = 1;
		goto out;
	}

	if (cfs_str2num_check(src->ls_str, src->ls_len,
			      &re->re_lo, min, max)) {
		/* <number> is parsed */
		re->re_hi = re->re_lo;
		re->re_stride = 1;
		goto out;
	}

	if (!bracketed || !cfs_gettok(src, '-', &tok))
		goto failed;

	if (!cfs_str2num_check(tok.ls_str, tok.ls_len,
			       &re->re_lo, min, max))
		goto failed;

	/* <number> - */
	if (cfs_str2num_check(src->ls_str, src->ls_len,
			      &re->re_hi, min, max)) {
		/* <number> - <number> is parsed */
		re->re_stride = 1;
		goto out;
	}

	/* go to check <number> '-' <number> '/' <number> */
	if (cfs_gettok(src, '/', &tok)) {
		if (!cfs_str2num_check(tok.ls_str, tok.ls_len,
				       &re->re_hi, min, max))
			goto failed;

		/* <number> - <number> / ... */
		if (cfs_str2num_check(src->ls_str, src->ls_len,
				      &re->re_stride, min, max)) {
			/* <number> - <number> / <number> is parsed */
			goto out;
		}
	}

 out:
	*expr = re;
	return 0;

 failed:
	LIBCFS_FREE(re, sizeof(*re));
	return -EINVAL;
}

/**
 * Print the range expression \a re into specified \a buffer.
 * If \a bracketed is true, expression does not need additional
 * brackets.
 *
 * \retval number of characters written
 */
static int
cfs_range_expr_print(char *buffer, int count, struct cfs_range_expr *expr,
		     bool bracketed)
{
	int i;
	char s[] = "[";
	char e[] = "]";

	if (bracketed) {
		s[0] = '\0';
		e[0] = '\0';
	}

	if (expr->re_lo == expr->re_hi)
		i = scnprintf(buffer, count, "%u", expr->re_lo);
	else if (expr->re_stride == 1)
		i = scnprintf(buffer, count, "%s%u-%u%s",
			      s, expr->re_lo, expr->re_hi, e);
	else
		i = scnprintf(buffer, count, "%s%u-%u/%u%s",
			      s, expr->re_lo, expr->re_hi, expr->re_stride, e);
	return i;
}

/**
 * Print a list of range expressions (\a expr_list) into specified \a buffer.
 * If the list contains several expressions, separate them with comma
 * and surround the list with brackets.
 *
 * \retval number of characters written
 */
int
cfs_expr_list_print(char *buffer, int count, struct cfs_expr_list *expr_list)
{
	struct cfs_range_expr *expr;
	int i = 0, j = 0;
	int numexprs = 0;

	if (count <= 0)
		return 0;

	list_for_each_entry(expr, &expr_list->el_exprs, re_link)
		numexprs++;

	if (numexprs > 1)
		i += scnprintf(buffer + i, count - i, "[");

	list_for_each_entry(expr, &expr_list->el_exprs, re_link) {
		if (j++ != 0)
			i += scnprintf(buffer + i, count - i, ",");
		i += cfs_range_expr_print(buffer + i, count - i, expr,
					  numexprs > 1);
	}

	if (numexprs > 1)
		i += scnprintf(buffer + i, count - i, "]");

	return i;
}
EXPORT_SYMBOL(cfs_expr_list_print);

/**
 * Matches value (\a value) against ranges expression list \a expr_list.
 *
 * \retval 1 if \a value matches
 * \retval 0 otherwise
 */
int
cfs_expr_list_match(__u32 value, struct cfs_expr_list *expr_list)
{
	struct cfs_range_expr	*expr;

	list_for_each_entry(expr, &expr_list->el_exprs, re_link) {
		if (value >= expr->re_lo && value <= expr->re_hi &&
		    ((value - expr->re_lo) % expr->re_stride) == 0)
			return 1;
	}

	return 0;
}
EXPORT_SYMBOL(cfs_expr_list_match);

/**
 * Convert express list (\a expr_list) to an array of all matched values
 *
 * \retval N N is total number of all matched values
 * \retval 0 if expression list is empty
 * \retval < 0 for failure
 */
int
cfs_expr_list_values(struct cfs_expr_list *expr_list, int max, __u32 **valpp)
{
	struct cfs_range_expr	*expr;
	__u32			*val;
	int			count = 0;
	int			i;

	list_for_each_entry(expr, &expr_list->el_exprs, re_link) {
		for (i = expr->re_lo; i <= expr->re_hi; i++) {
			if (((i - expr->re_lo) % expr->re_stride) == 0)
				count++;
		}
	}

	if (count == 0) /* empty expression list */
		return 0;

	if (count > max) {
		CERROR("Number of values %d exceeds max allowed %d\n",
		       max, count);
		return -EINVAL;
	}

	LIBCFS_ALLOC(val, sizeof(val[0]) * count);
	if (!val)
		return -ENOMEM;

	count = 0;
	list_for_each_entry(expr, &expr_list->el_exprs, re_link) {
		for (i = expr->re_lo; i <= expr->re_hi; i++) {
			if (((i - expr->re_lo) % expr->re_stride) == 0)
				val[count++] = i;
		}
	}

	*valpp = val;
	return count;
}
EXPORT_SYMBOL(cfs_expr_list_values);

/**
 * Frees cfs_range_expr structures of \a expr_list.
 *
 * \retval none
 */
void
cfs_expr_list_free(struct cfs_expr_list *expr_list)
{
	while (!list_empty(&expr_list->el_exprs)) {
		struct cfs_range_expr *expr;

		expr = list_entry(expr_list->el_exprs.next,
				  struct cfs_range_expr, re_link);
		list_del(&expr->re_link);
		LIBCFS_FREE(expr, sizeof(*expr));
	}

	LIBCFS_FREE(expr_list, sizeof(*expr_list));
}
EXPORT_SYMBOL(cfs_expr_list_free);

/**
 * Parses \<cfs_expr_list\> token of the syntax.
 *
 * \retval 0 if \a str parses to \<number\> | \<expr_list\>
 * \retval -errno otherwise
 */
int
cfs_expr_list_parse(char *str, int len, unsigned min, unsigned max,
		    struct cfs_expr_list **elpp)
{
	struct cfs_expr_list	*expr_list;
	struct cfs_range_expr	*expr;
	struct cfs_lstr		src;
	int			rc;

	LIBCFS_ALLOC(expr_list, sizeof(*expr_list));
	if (!expr_list)
		return -ENOMEM;

	src.ls_str = str;
	src.ls_len = len;

	INIT_LIST_HEAD(&expr_list->el_exprs);

	if (src.ls_str[0] == '[' &&
	    src.ls_str[src.ls_len - 1] == ']') {
		src.ls_str++;
		src.ls_len -= 2;

		rc = -EINVAL;
		while (src.ls_str) {
			struct cfs_lstr tok;

			if (!cfs_gettok(&src, ',', &tok)) {
				rc = -EINVAL;
				break;
			}

			rc = cfs_range_expr_parse(&tok, min, max, 1, &expr);
			if (rc != 0)
				break;

			list_add_tail(&expr->re_link, &expr_list->el_exprs);
		}
	} else {
		rc = cfs_range_expr_parse(&src, min, max, 0, &expr);
		if (rc == 0)
			list_add_tail(&expr->re_link, &expr_list->el_exprs);
	}

	if (rc != 0)
		cfs_expr_list_free(expr_list);
	else
		*elpp = expr_list;

	return rc;
}
EXPORT_SYMBOL(cfs_expr_list_parse);

/**
 * Frees cfs_expr_list structures of \a list.
 *
 * For each struct cfs_expr_list structure found on \a list it frees
 * range_expr list attached to it and frees the cfs_expr_list itself.
 *
 * \retval none
 */
void
cfs_expr_list_free_list(struct list_head *list)
{
	struct cfs_expr_list *el;

	while (!list_empty(list)) {
		el = list_entry(list->next, struct cfs_expr_list, el_link);
		list_del(&el->el_link);
		cfs_expr_list_free(el);
	}
}
EXPORT_SYMBOL(cfs_expr_list_free_list);
