/*
 * Copyright (c) 2007 Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

/*
 * This program is a CGI application. It outputs directory index page.
 * Put it into cgi-bin/index.cgi and chmod 0755.
 */

/* Build a-la
i486-linux-uclibc-gcc \
-static -static-libgcc \
-D_LARGEFILE_SOURCE -D_LARGEFILE64_SOURCE -D_FILE_OFFSET_BITS=64 \
-Wall -Wshadow -Wwrite-strings -Wundef -Wstrict-prototypes -Werror \
-Wold-style-definition -Wdeclaration-after-statement -Wno-pointer-sign \
-Wmissing-prototypes -Wmissing-declarations \
-Os -fno-builtin-strlen -finline-limit=0 -fomit-frame-pointer \
-ffunction-sections -fdata-sections -fno-guess-branch-probability \
-funsigned-char \
-falign-functions=1 -falign-jumps=1 -falign-labels=1 -falign-loops=1 \
-march=i386 -mpreferred-stack-boundary=2 \
-Wl,-Map -Wl,link.map -Wl,--warn-common -Wl,--sort-common -Wl,--gc-sections \
httpd_indexcgi.c -o index.cgi
*/

/* We don't use printf, as it pulls in >12 kb of code from uclibc (i386). */
/* Currently malloc machinery is the biggest part of libc we pull in. */
/* We have only one realloc and one strdup, any idea how to do without? */

/* Size (i386, static uclibc, approximate):
 *   text    data     bss     dec     hex filename
 *  13036      44    3052   16132    3f04 index.cgi
 *   2576       4    2048    4628    1214 index.cgi.o
 */

#define _GNU_SOURCE 1  /* for strchrnul */
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <dirent.h>
#include <time.h>

/* Appearance of the table is controlled by style sheet *ONLY*,
 * formatting code uses <TAG class=CLASS> to apply style
 * to elements. Edit stylesheet to your liking and recompile. */

#define STYLE_STR \
"<style>"                                               "\n"\
"table {"                                               "\n"\
  "width:100%;"                                         "\n"\
  "background-color:#fff5ee;"                           "\n"\
  "border-width:1px;" /* 1px 1px 1px 1px; */            "\n"\
  "border-spacing:2px;"                                 "\n"\
  "border-style:solid;" /* solid solid solid solid; */  "\n"\
  "border-color:black;" /* black black black black; */  "\n"\
  "border-collapse:collapse;"                           "\n"\
"}"                                                     "\n"\
"th {"                                                  "\n"\
  "border-width:1px;" /* 1px 1px 1px 1px; */            "\n"\
  "padding:1px;" /* 1px 1px 1px 1px; */                 "\n"\
  "border-style:solid;" /* solid solid solid solid; */  "\n"\
  "border-color:black;" /* black black black black; */  "\n"\
"}"                                                     "\n"\
"td {"                                                  "\n"\
             /* top right bottom left */                    \
  "border-width:0px 1px 0px 1px;"                       "\n"\
  "padding:1px;" /* 1px 1px 1px 1px; */                 "\n"\
  "border-style:solid;" /* solid solid solid solid; */  "\n"\
  "border-color:black;" /* black black black black; */  "\n"\
  "white-space:nowrap;"                                 "\n"\
"}"                                                     "\n"\
"tr.hdr { background-color:#eee5de; }"                  "\n"\
"tr.o { background-color:#ffffff; }"                    "\n"\
/* tr.e { ... } - for even rows (currently none) */         \
"tr.foot { background-color:#eee5de; }"                 "\n"\
"th.cnt { text-align:left; }"                           "\n"\
"th.sz { text-align:right; }"                           "\n"\
"th.dt { text-align:right; }"                           "\n"\
"td.sz { text-align:right; }"                           "\n"\
"td.dt { text-align:right; }"                           "\n"\
"col.nm { width:98%; }"                                 "\n"\
"col.sz { width:1%; }"                                  "\n"\
"col.dt { width:1%; }"                                  "\n"\
"</style>"                                              "\n"\

typedef struct dir_list_t {
	char  *dl_name;
	mode_t dl_mode;
	off_t  dl_size;
	time_t dl_mtime;
} dir_list_t;

static int compare_dl(dir_list_t *a, dir_list_t *b)
{
	/* ".." is 'less than' any other dir entry */
	if (strcmp(a->dl_name, "..") == 0) {
		return -1;
	}
	if (strcmp(b->dl_name, "..") == 0) {
		return 1;
	}
	if (S_ISDIR(a->dl_mode) != S_ISDIR(b->dl_mode)) {
		/* 1 if b is a dir (and thus a is 'after' b, a > b),
		 * else -1 (a < b) */
		return (S_ISDIR(b->dl_mode) != 0) ? 1 : -1;
	}
	return strcmp(a->dl_name, b->dl_name);
}

static char buffer[2*1024 > sizeof(STYLE_STR) ? 2*1024 : sizeof(STYLE_STR)];
static char *dst = buffer;
enum {
	BUFFER_SIZE = sizeof(buffer),
	HEADROOM = 64,
};

/* After this call, you have at least size + HEADROOM bytes available
 * ahead of dst */
static void guarantee(int size)
{
	if (buffer + (BUFFER_SIZE-HEADROOM) - dst >= size)
		return;
	write(STDOUT_FILENO, buffer, dst - buffer);
	dst = buffer;
}

/* NB: formatters do not store terminating NUL! */

/* HEADROOM bytes are available after dst after this call */
static void fmt_str(/*char *dst,*/ const char *src)
{
	unsigned len = strlen(src);
	guarantee(len);
	memcpy(dst, src, len);
	dst += len;
}

/* HEADROOM bytes after dst are available after this call */
static void fmt_url(/*char *dst,*/ const char *name)
{
	while (*name) {
		unsigned c = *name++;
		guarantee(3);
		*dst = c;
		if ((c - '0') > 9 /* not a digit */
		 && ((c|0x20) - 'a') > ('z' - 'a') /* not A-Z or a-z */
		 && !strchr("._-+@", c)
		) {
			*dst++ = '%';
			*dst++ = "0123456789ABCDEF"[c >> 4];
			*dst = "0123456789ABCDEF"[c & 0xf];
		}
		dst++;
	}
}

/* HEADROOM bytes are available after dst after this call */
static void fmt_html(/*char *dst,*/ const char *name)
{
	while (*name) {
		char c = *name++;
		if (c == '<')
			fmt_str("&lt;");
		else if (c == '>')
			fmt_str("&gt;");
		else if (c == '&') {
			fmt_str("&amp;");
		} else {
			guarantee(1);
			*dst++ = c;
			continue;
		}
	}
}

/* HEADROOM bytes are available after dst after this call */
static void fmt_ull(/*char *dst,*/ unsigned long long n)
{
	char buf[sizeof(n)*3 + 2];
	char *p;

	p = buf + sizeof(buf) - 1;
	*p = '\0';
	do {
		*--p = (n % 10) + '0';
		n /= 10;
	} while (n);
	fmt_str(/*dst,*/ p);
}

/* Does not call guarantee - eats into headroom instead */
static void fmt_02u(/*char *dst,*/ unsigned n)
{
	/* n %= 100; - not needed, callers don't pass big n */
	dst[0] = (n / 10) + '0';
	dst[1] = (n % 10) + '0';
	dst += 2;
}

/* Does not call guarantee - eats into headroom instead */
static void fmt_04u(/*char *dst,*/ unsigned n)
{
	/* n %= 10000; - not needed, callers don't pass big n */
	fmt_02u(n / 100);
	fmt_02u(n % 100);
}

int main(int argc, char *argv[])
{
	dir_list_t *dir_list;
	dir_list_t *cdir;
	unsigned dir_list_count;
	unsigned count_dirs;
	unsigned count_files;
	unsigned long long size_total;
	int odd;
	DIR *dirp;
	char *location;

	location = getenv("REQUEST_URI");
	if (!location)
		return 1;

	/* drop URL arguments if any */
	strchrnul(location, '?')[0] = '\0';

	if (location[0] != '/'
	 || strstr(location, "//")
	 || strstr(location, "/../")
	 || strcmp(strrchr(location, '/'), "/..") == 0
	) {
		return 1;
	}

	if (chdir("..")
	 || (location[1] && chdir(location + 1))
	) {
		return 1;
	}

	dirp = opendir(".");
	if (!dirp)
		return 1;
	dir_list = NULL;
	dir_list_count = 0;
	while (1) {
		struct dirent *dp;
		struct stat sb;

		dp = readdir(dirp);
		if (!dp)
			break;
		if (dp->d_name[0] == '.' && !dp->d_name[1])
			continue;
		if (stat(dp->d_name, &sb) != 0)
			continue;
		dir_list = realloc(dir_list, (dir_list_count + 1) * sizeof(dir_list[0]));
		dir_list[dir_list_count].dl_name = strdup(dp->d_name);
		dir_list[dir_list_count].dl_mode = sb.st_mode;
		dir_list[dir_list_count].dl_size = sb.st_size;
		dir_list[dir_list_count].dl_mtime = sb.st_mtime;
		dir_list_count++;
	}
	closedir(dirp);

	qsort(dir_list, dir_list_count, sizeof(dir_list[0]), (void*)compare_dl);

	fmt_str(
		"" /* Additional headers (currently none) */
		"\r\n" /* Mandatory empty line after headers */
		"<html><head><title>Index of ");
	/* Guard against directories with &, > etc */
	fmt_html(location);
	fmt_str(
		"</title>\n"
		STYLE_STR
		"</head>" "\n"
		"<body>" "\n"
		"<h1>Index of ");
	fmt_html(location);
	fmt_str(
		"</h1>" "\n"
		"<table>" "\n"
		"<col class=nm><col class=sz><col class=dt>" "\n"
		"<tr class=hdr><th class=cnt>Name<th class=sz>Size<th class=dt>Last modified" "\n");

	odd = 0;
	count_dirs = 0;
	count_files = 0;
	size_total = 0;
	cdir = dir_list;
	while (dir_list_count--) {
		struct tm *ptm;

		if (S_ISDIR(cdir->dl_mode)) {
			count_dirs++;
		} else if (S_ISREG(cdir->dl_mode)) {
			count_files++;
			size_total += cdir->dl_size;
		} else
			goto next;

		fmt_str("<tr class=");
		*dst++ = (odd ? 'o' : 'e');
		fmt_str("><td class=nm><a href='");
		fmt_url(cdir->dl_name); /* %20 etc */
		if (S_ISDIR(cdir->dl_mode))
			*dst++ = '/';
		fmt_str("'>");
		fmt_html(cdir->dl_name); /* &lt; etc */
		if (S_ISDIR(cdir->dl_mode))
			*dst++ = '/';
		fmt_str("</a><td class=sz>");
		if (S_ISREG(cdir->dl_mode))
			fmt_ull(cdir->dl_size);
		fmt_str("<td class=dt>");
		ptm = gmtime(&cdir->dl_mtime);
		fmt_04u(1900 + ptm->tm_year); *dst++ = '-';
		fmt_02u(ptm->tm_mon + 1); *dst++ = '-';
		fmt_02u(ptm->tm_mday); *dst++ = ' ';
		fmt_02u(ptm->tm_hour); *dst++ = ':';
		fmt_02u(ptm->tm_min); *dst++ = ':';
		fmt_02u(ptm->tm_sec);
		*dst++ = '\n';

		odd = 1 - odd;
 next:
		cdir++;
	}

	fmt_str("<tr class=foot><th class=cnt>Files: ");
	fmt_ull(count_files);
	/* count_dirs - 1: we don't want to count ".." */
	fmt_str(", directories: ");
	fmt_ull(count_dirs - 1);
	fmt_str("<th class=sz>");
	fmt_ull(size_total);
	fmt_str("<th class=dt>\n");
	/* "</table></body></html>" - why bother? */
	guarantee(BUFFER_SIZE * 2); /* flush */

	return 0;
}
