/* $Id: mstring.c,v 1.7 2016/12/02 17:57:21 tom Exp $ */

#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <ctype.h>
#include <string.h>
#include "defs.h"

/* parameters about string length.  HEAD is the starting size and
** HEAD+TAIL should be a power of two */
#define HEAD	24
#define TAIL	8

#if defined(YYBTYACC)

static char *buf_ptr;
static size_t buf_len;

void
msprintf(struct mstring *s, const char *fmt,...)
{
    va_list args;
    size_t len;
#ifdef HAVE_VSNPRINTF
    int changed;
#endif

    if (!s || !s->base)
	return;

    if (buf_len == 0)
    {
	buf_ptr = malloc(buf_len = 4096);
    }
    if (buf_ptr == 0)
    {
	return;
    }

#ifdef HAVE_VSNPRINTF
    do
    {
	va_start(args, fmt);
	len = (size_t) vsnprintf(buf_ptr, buf_len, fmt, args);
	va_end(args);
	if ((changed = (len > buf_len)) != 0)
	{
	    char *new_ptr = realloc(buf_ptr, (buf_len * 3) / 2);
	    if (new_ptr == 0)
	    {
		free(buf_ptr);
		buf_ptr = 0;
		return;
	    }
	    buf_ptr = new_ptr;
	}
    }
    while (changed);
#else
    va_start(args, fmt);
    len = (size_t) vsprintf(buf_ptr, fmt, args);
    va_end(args);
    if (len >= buf_len)
	return;
#endif

    if (len > (size_t) (s->end - s->ptr))
    {
	char *new_base;
	size_t cp = (size_t) (s->ptr - s->base);
	size_t cl = (size_t) (s->end - s->base);
	size_t nl = cl;
	while (len > (nl - cp))
	    nl = nl + nl + TAIL;
	if ((new_base = realloc(s->base, nl)))
	{
	    s->base = new_base;
	    s->ptr = s->base + cp;
	    s->end = s->base + nl;
	}
	else
	{
	    free(s->base);
	    s->base = 0;
	    s->ptr = 0;
	    s->end = 0;
	    return;
	}
    }
    memcpy(s->ptr, buf_ptr, len);
    s->ptr += len;
}
#endif

int
mputchar(struct mstring *s, int ch)
{
    if (!s || !s->base)
	return ch;
    if (s->ptr == s->end)
    {
	size_t len = (size_t) (s->end - s->base);
	if ((s->base = realloc(s->base, len + len + TAIL)))
	{
	    s->ptr = s->base + len;
	    s->end = s->base + len + len + TAIL;
	}
	else
	{
	    s->ptr = s->end = 0;
	    return ch;
	}
    }
    *s->ptr++ = (char)ch;
    return ch;
}

struct mstring *
msnew(void)
{
    struct mstring *n = TMALLOC(struct mstring, 1);

    if (n)
    {
	if ((n->base = n->ptr = MALLOC(HEAD)) != 0)
	{
	    n->end = n->base + HEAD;
	}
	else
	{
	    free(n);
	    n = 0;
	}
    }
    return n;
}

char *
msdone(struct mstring *s)
{
    char *r = 0;
    if (s)
    {
	mputc(s, 0);
	r = s->base;
	free(s);
    }
    return r;
}

#if defined(YYBTYACC)
/* compare two strings, ignoring whitespace, except between two letters or
** digits (and treat all of these as equal) */
int
strnscmp(const char *a, const char *b)
{
    while (1)
    {
	while (isspace(UCH(*a)))
	    a++;
	while (isspace(UCH(*b)))
	    b++;
	while (*a && *a == *b)
	    a++, b++;
	if (isspace(UCH(*a)))
	{
	    if (isalnum(UCH(a[-1])) && isalnum(UCH(*b)))
		break;
	}
	else if (isspace(UCH(*b)))
	{
	    if (isalnum(UCH(b[-1])) && isalnum(UCH(*a)))
		break;
	}
	else
	    break;
    }
    return *a - *b;
}

unsigned int
strnshash(const char *s)
{
    unsigned int h = 0;

    while (*s)
    {
	if (!isspace(UCH(*s)))
	    h = (h << 5) - h + (unsigned char)*s;
	s++;
    }
    return h;
}
#endif

#ifdef NO_LEAKS
void
mstring_leaks(void)
{
#if defined(YYBTYACC)
    free(buf_ptr);
    buf_ptr = 0;
    buf_len = 0;
#endif
}
#endif
