/*
 * Replacements for common but usually nonstandard functions that aren't
 * supplied by all platforms.
 *
 * Copyright (C) 2009 by Dan Fandrich <dan@coneharvesters.com>, et. al.
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"

#ifndef HAVE_STRCHRNUL
char* FAST_FUNC strchrnul(const char *s, int c)
{
	while (*s != '\0' && *s != c)
		s++;
	return (char*)s;
}
#endif

#ifndef HAVE_USLEEP
int FAST_FUNC usleep(unsigned usec)
{
	struct timespec ts;
	ts.tv_sec = usec / 1000000u;
	ts.tv_nsec = (usec % 1000000u) * 1000u;
	/*
	 * If a signal has non-default handler, nanosleep returns early.
	 * Our version of usleep doesn't return early
	 * if interrupted by such signals:
	 *
	 */
	while (nanosleep(&ts, &ts) != 0)
		continue;
	return 0;
}
#endif

#ifndef HAVE_VASPRINTF
int FAST_FUNC vasprintf(char **string_ptr, const char *format, va_list p)
{
	int r;
	va_list p2;
	char buf[128];

	va_copy(p2, p);
	r = vsnprintf(buf, 128, format, p);
	va_end(p);

	/* Note: can't use xstrdup/xmalloc, they call vasprintf (us) on failure! */

	if (r < 128) {
		va_end(p2);
		*string_ptr = strdup(buf);
		return (*string_ptr ? r : -1);
	}

	*string_ptr = malloc(r+1);
	r = (*string_ptr ? vsnprintf(*string_ptr, r+1, format, p2) : -1);
	va_end(p2);

	return r;
}
#endif

#ifndef HAVE_DPRINTF
/* dprintf is now part of POSIX.1, but was only added in 2008 */
int dprintf(int fd, const char *format, ...)
{
	va_list p;
	int r;
	char *string_ptr;

	va_start(p, format);
	r = vasprintf(&string_ptr, format, p);
	va_end(p);
	if (r >= 0) {
		r = full_write(fd, string_ptr, r);
		free(string_ptr);
	}
	return r;
}
#endif

#ifndef HAVE_MEMRCHR
/* Copyright (C) 2005 Free Software Foundation, Inc.
 * memrchr() is a GNU function that might not be available everywhere.
 * It's basically the inverse of memchr() - search backwards in a
 * memory block for a particular character.
 */
void* FAST_FUNC memrchr(const void *s, int c, size_t n)
{
	const char *start = s, *end = s;

	end += n - 1;

	while (end >= start) {
		if (*end == (char)c)
			return (void *) end;
		end--;
	}

	return NULL;
}
#endif

#ifndef HAVE_MKDTEMP
/* This is now actually part of POSIX.1, but was only added in 2008 */
char* FAST_FUNC mkdtemp(char *template)
{
	if (mktemp(template) == NULL || mkdir(template, 0700) != 0)
		return NULL;
	return template;
}
#endif

#ifndef HAVE_STRCASESTR
/* Copyright (c) 1999, 2000 The ht://Dig Group */
char* FAST_FUNC strcasestr(const char *s, const char *pattern)
{
	int length = strlen(pattern);

	while (*s) {
		if (strncasecmp(s, pattern, length) == 0)
			return (char *)s;
		s++;
	}
	return 0;
}
#endif

#ifndef HAVE_STRSEP
/* Copyright (C) 2004 Free Software Foundation, Inc. */
char* FAST_FUNC strsep(char **stringp, const char *delim)
{
	char *start = *stringp;
	char *ptr;

	if (!start)
		return NULL;

	if (!*delim)
		ptr = start + strlen(start);
	else {
		ptr = strpbrk(start, delim);
		if (!ptr) {
			*stringp = NULL;
			return start;
		}
	}

	*ptr = '\0';
	*stringp = ptr + 1;

	return start;
}
#endif

#ifndef HAVE_STPCPY
char* FAST_FUNC stpcpy(char *p, const char *to_add)
{
	while ((*p = *to_add) != '\0') {
		p++;
		to_add++;
	}
	return p;
}
#endif

#ifndef HAVE_GETLINE
ssize_t FAST_FUNC getline(char **lineptr, size_t *n, FILE *stream)
{
	int ch;
	char *line = *lineptr;
	size_t alloced = *n;
	size_t len = 0;

	do {
		ch = fgetc(stream);
		if (ch == EOF)
			break;
		if (len + 1 >= alloced) {
			alloced += alloced/4 + 64;
			line = xrealloc(line, alloced);
		}
		line[len++] = ch;
	} while (ch != '\n');

	if (len == 0)
		return -1;

	line[len] = '\0';
	*lineptr = line;
	*n = alloced;
	return len;
}
#endif

#ifndef HAVE_TTYNAME_R
int ttyname_r(int fd, char *buf, size_t buflen)
{
	int r;
	char path[sizeof("/proc/self/fd/%d") + sizeof(int)*3];

	if (!isatty(fd))
		return errno == EINVAL ? ENOTTY : errno;
	sprintf(path, "/proc/self/fd/%d", fd);
	r = readlink(path, buf, buflen);
	if (r < 0)
		return errno;
	if (r >= buflen)
		return ERANGE;
	buf[r] = '\0';
	return 0;
}
#endif
