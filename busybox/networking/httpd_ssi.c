/*
 * Copyright (c) 2009 Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */

/*
 * This program is a CGI application. It processes server-side includes:
 * <!--#include file="file.html" -->
 *
 * Usage: put these lines in httpd.conf:
 *
 * *.html:/bin/httpd_ssi
 * *.htm:/bin/httpd_ssi
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
httpd_ssi.c -o httpd_ssi
*/

/* Size (i386, static uclibc, approximate):
 * text    data     bss     dec     hex filename
 * 9487     160   68552   78199   13177 httpd_ssi
 *
 * Note: it wouldn't be too hard to get rid of stdio and strdup,
 * (especially that fgets() mangles NULs...)
 */

#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <dirent.h>
#include <time.h>

static char* skip_whitespace(char *s)
{
	while (*s == ' ' || *s == '\t') ++s;

	return s;
}

static char line[64 * 1024];

static void process_includes(const char *filename)
{
	int curdir_fd;
	char *end;
	FILE *fp = fopen(filename, "r");
	if (!fp)
		exit(1);

	/* Ensure that nested includes are relative:
	 * if we include a/1.htm and it includes b/2.htm,
	 * we need to include a/b/2.htm, not b/2.htm
	 */
	curdir_fd = -1;
	end = strrchr(filename, '/');
	if (end) {
		curdir_fd = open(".", O_RDONLY);
		/* *end = '\0' would mishandle "/file.htm" */
		end[1] = '\0';
		chdir(filename);
	}

#define INCLUDE "<!--#include"
	while (fgets(line, sizeof(line), fp)) {
		unsigned preceding_len;
		char *include_directive;

		include_directive = strstr(line, INCLUDE);
		if (!include_directive) {
			fputs(line, stdout);
			continue;
		}
		preceding_len = include_directive - line;
		if (memchr(line, '\"', preceding_len)
		 || memchr(line, '\'', preceding_len)
		) {
			/* INCLUDE string may be inside "str" or 'str',
			 * ignore it */
			fputs(line, stdout);
			continue;
		}
		/* Small bug: we accept #includefile="file" too */
		include_directive = skip_whitespace(include_directive + sizeof(INCLUDE)-1);
		if (strncmp(include_directive, "file=\"", 6) != 0) {
			/* "<!--#include virtual=..."? - not supported */
			fputs(line, stdout);
			continue;
		}
		include_directive += 6; /* now it points to file name */
		end = strchr(include_directive, '\"');
		if (!end) {
			fputs(line, stdout);
			continue;
		}
		/* We checked that this is a valid include directive */

		/* Print everything before directive */
		if (preceding_len) {
			line[preceding_len] = '\0';
			fputs(line, stdout);
		}
		/* Save everything after directive */
		*end++ = '\0';
		end = strchr(end, '>');
		if (end)
			end = strdup(end + 1);

		/* FIXME:
		 * (1) are relative paths with /../ etc ok?
		 * (2) what to do with absolute paths?
		 * are they relative to doc root or to real root?
		 */
		process_includes(include_directive);

		/* Print everything after directive */
		if (end) {
			fputs(end, stdout);
			free(end);
		}
	}
	if (curdir_fd >= 0)
		fchdir(curdir_fd);
	fclose(fp);
}

int main(int argc, char *argv[])
{
	if (!argv[1])
		return 1;

	/* Seen from busybox.net's Apache:
	 * HTTP/1.1 200 OK
	 * Date: Thu, 10 Sep 2009 18:23:28 GMT
	 * Server: Apache
	 * Accept-Ranges: bytes
	 * Connection: close
	 * Content-Type: text/html
	 */
	fputs(
		/* "Date: Thu, 10 Sep 2009 18:23:28 GMT\r\n" */
		/* "Server: Apache\r\n" */
		/* "Accept-Ranges: bytes\r\n" - do we really accept bytes?! */
		"Connection: close\r\n"
		"Content-Type: text/html\r\n"
		"\r\n",
		stdout
	);
	process_includes(argv[1]);
	return 0;
}
