/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 1999-2004 by Erik Andersen <andersen@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//kbuild:lib-y += replace.o

#include "libbb.h"

unsigned FAST_FUNC count_strstr(const char *str, const char *sub)
{
	size_t sub_len = strlen(sub);
	unsigned count = 0;

	while ((str = strstr(str, sub)) != NULL) {
		count++;
		str += sub_len;
	}
	return count;
}

char* FAST_FUNC xmalloc_substitute_string(const char *src, int count, const char *sub, const char *repl)
{
	char *buf, *dst, *end;
	size_t sub_len = strlen(sub);
	size_t repl_len = strlen(repl);

	//dbg_msg("subst(s:'%s',count:%d,sub:'%s',repl:'%s'", src, count, sub, repl);

	buf = dst = xmalloc(strlen(src) + count * ((int)repl_len - (int)sub_len) + 1);
	/* we replace each sub with repl */
	while ((end = strstr(src, sub)) != NULL) {
		dst = mempcpy(dst, src, end - src);
		dst = mempcpy(dst, repl, repl_len);
		/*src = end + 1; - GNU findutils 4.5.10 doesn't do this... */
		src = end + sub_len; /* but this. Try "xargs -Iaa echo aaa" */
	}
	strcpy(dst, src);
	//dbg_msg("subst9:'%s'", buf);
	return buf;
}
