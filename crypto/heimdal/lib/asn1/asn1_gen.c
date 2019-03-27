/*
 * Copyright (c) 2005 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * 3. Neither the name of the Institute nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include "der_locl.h"
#include <com_err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <ctype.h>
#include <getarg.h>
#include <hex.h>
#include <err.h>

RCSID("$Id$");

static int
doit(const char *fn)
{
    char buf[2048];
    char *fnout = NULL;
    const char *bname;
    unsigned long line = 0;
    FILE *f, *fout;
    size_t offset = 0;

    f = fopen(fn, "r");
    if (f == NULL)
	err(1, "fopen");

    bname = strrchr(fn, '/');
    if (bname)
	bname++;
    else
	bname = fn;

    if (asprintf(&fnout, "%s.out", bname) < 0 || fnout == NULL)
	errx(1, "malloc");

    fout = fopen(fnout, "w");
    if (fout == NULL)
	err(1, "fopen: output file");

    while (fgets(buf, sizeof(buf), f) != NULL) {
	char *ptr, *class, *type, *tag, *length, *data, *foo;
	int ret, l, c, ty, ta;
	unsigned char p[6], *pdata;
	size_t sz;

	line++;

	buf[strcspn(buf, "\r\n")] = '\0';
	if (buf[0] == '#' || buf[0] == '\0')
	    continue;

	ptr = buf;
	while (isspace((unsigned char)*ptr))
	       ptr++;

	class = strtok_r(ptr, " \t\n", &foo);
	if (class == NULL) errx(1, "class missing on line %lu", line);
	type = strtok_r(NULL, " \t\n", &foo);
	if (type == NULL) errx(1, "type missing on line %lu", line);
	tag = strtok_r(NULL, " \t\n", &foo);
	if (tag == NULL) errx(1, "tag missing on line %lu", line);
	length = strtok_r(NULL, " \t\n", &foo);
	if (length == NULL) errx(1, "length missing on line %lu", line);
	data = strtok_r(NULL, " \t\n", &foo);

	c = der_get_class_num(class);
	if (c == -1) errx(1, "no valid class on line %lu", line);
	ty = der_get_type_num(type);
	if (ty == -1) errx(1, "no valid type on line %lu", line);
	ta = der_get_tag_num(tag);
	if (ta == -1)
	    ta = atoi(tag);

	l = atoi(length);

	printf("line: %3lu offset: %3lu class: %d type: %d "
	       "tag: %3d length: %3d %s\n",
	       line, (unsigned long)offset, c, ty, ta, l,
	       data ? "<have data>" : "<no data>");

	ret = der_put_length_and_tag(p + sizeof(p) - 1, sizeof(p),
				     l,
				     c,
				     ty,
				     ta,
				     &sz);
	if (ret)
	    errx(1, "der_put_length_and_tag: %d", ret);

	if (fwrite(p + sizeof(p) - sz , sz, 1, fout) != 1)
	    err(1, "fwrite length/tag failed");
	offset += sz;

	if (data) {
	    size_t datalen;

	    datalen = strlen(data) / 2;
	    pdata = emalloc(sz);

	    if (hex_decode(data, pdata, datalen) != datalen)
		errx(1, "failed to decode data");

	    if (fwrite(pdata, datalen, 1, fout) != 1)
		err(1, "fwrite data failed");
	    offset += datalen;

	    free(pdata);
	}
    }
    printf("line: eof offset: %lu\n", (unsigned long)offset);

    fclose(fout);
    fclose(f);
    return 0;
}


static int version_flag;
static int help_flag;
struct getargs args[] = {
    { "version", 0, arg_flag, &version_flag },
    { "help", 0, arg_flag, &help_flag }
};
int num_args = sizeof(args) / sizeof(args[0]);

static void
usage(int code)
{
    arg_printusage(args, num_args, NULL, "parse-file");
    exit(code);
}

int
main(int argc, char **argv)
{
    int optidx = 0;

    setprogname (argv[0]);

    if(getarg(args, num_args, argc, argv, &optidx))
	usage(1);
    if(help_flag)
	usage(0);
    if(version_flag) {
	print_version(NULL);
	exit(0);
    }
    argv += optidx;
    argc -= optidx;
    if (argc != 1)
	usage (1);

    return doit (argv[0]);
}
