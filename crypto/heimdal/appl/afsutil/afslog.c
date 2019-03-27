/*
 * Copyright (c) 1997-2003 Kungliga Tekniska Högskolan
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

#ifdef HAVE_CONFIG_H
#include <config.h>
RCSID("$Id$");
#endif
#include <ctype.h>
#ifdef KRB5
#include <krb5.h>
#endif
#include <kafs.h>
#include <roken.h>
#include <getarg.h>
#include <err.h>

static int help_flag;
static int version_flag;
static getarg_strings cells;
static char *realm;
static getarg_strings files;
static int unlog_flag;
static int verbose;
#ifdef KRB5
static char *client_string;
static char *cache_string;
static int use_krb5 = 1;
#endif

struct getargs args[] = {
    { "cell",	'c', arg_strings, &cells, "cells to get tokens for", "cell" },
    { "file",	'p', arg_strings, &files, "files to get tokens for", "path" },
    { "realm",	'k', arg_string, &realm, "realm for afs cell", "realm" },
    { "unlog",	'u', arg_flag, &unlog_flag, "remove tokens" },
#ifdef KRB5
    { "principal",'P',arg_string,&client_string,"principal to use","principal"},
    { "cache",   0,  arg_string, &cache_string, "ccache to use", "cache"},
    { "v5",	 0,  arg_negative_flag, &use_krb5, "don't use Kerberos 5" },
#endif
    { "verbose",'v', arg_flag, &verbose },
    { "version", 0,  arg_flag, &version_flag },
    { "help",	'h', arg_flag, &help_flag },
};

static int num_args = sizeof(args) / sizeof(args[0]);

#ifdef KRB5
krb5_context context;
krb5_ccache id;
#endif

static const char *
expand_one_file(FILE *f, const char *cell)
{
    static char buf[1024];
    char *p;

    while (fgets (buf, sizeof(buf), f) != NULL) {
	if(buf[0] == '>') {
	    for(p = buf; *p && !isspace((unsigned char)*p) && *p != '#'; p++)
		;
	    *p = '\0';
	    if(strncmp(buf + 1, cell, strlen(cell)) == 0)
		return buf + 1;
	}
	buf[0] = '\0';
    }
    return NULL;
}

static const char *
expand_cell_name(const char *cell)
{
    FILE *f;
    const char *c;
    const char **fn, *files[] = { _PATH_CELLSERVDB,
				  _PATH_ARLA_CELLSERVDB,
				  _PATH_OPENAFS_DEBIAN_CELLSERVDB,
				  _PATH_ARLA_DEBIAN_CELLSERVDB,
				  NULL };
    for(fn = files; *fn; fn++) {
	f = fopen(*fn, "r");
	if(f == NULL)
	    continue;
	c = expand_one_file(f, cell);
	fclose(f);
	if(c)
	    return c;
    }
    return cell;
}

static void
usage(int ecode)
{
    arg_printusage(args, num_args, NULL, "[cell|path]...");
    exit(ecode);
}

struct cell_list {
    char *cell;
    struct cell_list *next;
} *cell_list;

static int
afslog_cell(const char *cell, int expand)
{
    struct cell_list *p, **q;
    const char *c = cell;
    if(expand){
	c = expand_cell_name(cell);
	if(c == NULL){
	    warnx("No cell matching \"%s\" found.", cell);
	    return -1;
	}
	if(verbose && strcmp(c, cell) != 0)
	    warnx("Cell \"%s\" expanded to \"%s\"", cell, c);
    }
    /* add to list of cells to get tokens for, and also remove
       duplicates; the actual afslog takes place later */
    for(p = cell_list, q = &cell_list; p; q = &p->next, p = p->next)
	if(strcmp(p->cell, c) == 0)
	    return 0;
    p = malloc(sizeof(*p));
    if(p == NULL)
	return -1;
    p->cell = strdup(c);
    if(p->cell == NULL) {
	free(p);
	return -1;
    }
    p->next = NULL;
    *q = p;
    return 0;
}

static int
afslog_file(const char *path)
{
    char cell[64];
    if(k_afs_cell_of_file(path, cell, sizeof(cell))){
	warnx("No cell found for file \"%s\".", path);
	return -1;
    }
    if(verbose)
	warnx("File \"%s\" lives in cell \"%s\"", path, cell);
    return afslog_cell(cell, 0);
}

static int
do_afslog(const char *cell)
{
    int k5ret;

    k5ret = 0;

#ifdef KRB5
    if(context != NULL && id != NULL && use_krb5) {
	k5ret = krb5_afslog(context, id, cell, realm);
	if(k5ret == 0)
	    return 0;
    }
#endif
    if (cell == NULL)
	cell = "<default cell>";
#ifdef KRB5
    if (k5ret)
	krb5_warn(context, k5ret, "krb5_afslog(%s)", cell);
#endif
    if (k5ret)
	return 1;
    return 0;
}

static void
log_func(void *ctx, const char *str)
{
    fprintf(stderr, "%s\n", str);
}

int
main(int argc, char **argv)
{
    int optind = 0;
    int i;
    int num;
    int ret = 0;
    int failed = 0;
    struct cell_list *p;

    setprogname(argv[0]);

    if(getarg(args, num_args, argc, argv, &optind))
	usage(1);
    if(help_flag)
	usage(0);
    if(version_flag) {
	print_version(NULL);
	exit(0);
    }

    if(!k_hasafs())
	errx(1, "AFS does not seem to be present on this machine");

    if(unlog_flag){
	k_unlog();
	exit(0);
    }
#ifdef KRB5
    ret = krb5_init_context(&context);
    if (ret) {
	context = NULL;
    } else {
	if (client_string) {
	    krb5_principal client;

	    ret = krb5_parse_name(context, client_string, &client);
	    if (ret == 0)
		ret = krb5_cc_cache_match(context, client, &id);
	    if (ret)
		id = NULL;
	}
	if (id == NULL && cache_string) {
	    if(krb5_cc_resolve(context, cache_string, &id) != 0) {
		krb5_warnx(context, "failed to open kerberos 5 cache '%s'",
			   cache_string);
		id = NULL;
	    }
	}
	if (id == NULL)
	    if(krb5_cc_default(context, &id) != 0)
		id = NULL;
    }
#endif

    if (verbose)
	kafs_set_verbose(log_func, NULL);

    num = 0;
    for(i = 0; i < files.num_strings; i++){
	afslog_file(files.strings[i]);
	num++;
    }
    free_getarg_strings (&files);
    for(i = 0; i < cells.num_strings; i++){
	afslog_cell(cells.strings[i], 1);
	num++;
    }
    free_getarg_strings (&cells);
    for(i = optind; i < argc; i++){
	num++;
	if(strcmp(argv[i], ".") == 0 ||
	   strcmp(argv[i], "..") == 0 ||
	   strchr(argv[i], '/') ||
	   access(argv[i], F_OK) == 0)
	    afslog_file(argv[i]);
	else
	    afslog_cell(argv[i], 1);
    }
    if(num == 0) {
	if(do_afslog(NULL))
	    failed++;
    } else
	for(p = cell_list; p; p = p->next) {
	    if(verbose)
		warnx("Getting tokens for cell \"%s\"", p->cell);
	    if(do_afslog(p->cell))
		failed++;
    }

    return failed;
}
