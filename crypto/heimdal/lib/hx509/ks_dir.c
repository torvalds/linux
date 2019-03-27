/*
 * Copyright (c) 2006 Kungliga Tekniska Högskolan
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

#include "hx_locl.h"
#include <dirent.h>

/*
 * The DIR keyset module is strange compared to the other modules
 * since it does lazy evaluation and really doesn't keep any local
 * state except for the directory iteration and cert iteration of
 * files. DIR ignores most errors so that the consumer doesn't get
 * failes for stray files in directories.
 */

struct dircursor {
    DIR *dir;
    hx509_certs certs;
    void *iter;
};

/*
 *
 */

static int
dir_init(hx509_context context,
	 hx509_certs certs, void **data, int flags,
	 const char *residue, hx509_lock lock)
{
    *data = NULL;

    {
	struct stat sb;
	int ret;

	ret = stat(residue, &sb);
	if (ret == -1) {
	    hx509_set_error_string(context, 0, ENOENT,
				   "No such file %s", residue);
	    return ENOENT;
	}

	if (!S_ISDIR(sb.st_mode)) {
	    hx509_set_error_string(context, 0, ENOTDIR,
				   "%s is not a directory", residue);
	    return ENOTDIR;
	}
    }

    *data = strdup(residue);
    if (*data == NULL) {
	hx509_clear_error_string(context);
	return ENOMEM;
    }

    return 0;
}

static int
dir_free(hx509_certs certs, void *data)
{
    free(data);
    return 0;
}

static int
dir_iter_start(hx509_context context,
	       hx509_certs certs, void *data, void **cursor)
{
    struct dircursor *d;

    *cursor = NULL;

    d = calloc(1, sizeof(*d));
    if (d == NULL) {
	hx509_clear_error_string(context);
	return ENOMEM;
    }

    d->dir = opendir(data);
    if (d->dir == NULL) {
	hx509_clear_error_string(context);
	free(d);
	return errno;
    }
    rk_cloexec_dir(d->dir);
    d->certs = NULL;
    d->iter = NULL;

    *cursor = d;
    return 0;
}

static int
dir_iter(hx509_context context,
	 hx509_certs certs, void *data, void *iter, hx509_cert *cert)
{
    struct dircursor *d = iter;
    int ret = 0;

    *cert = NULL;

    do {
	struct dirent *dir;
	char *fn;

	if (d->certs) {
	    ret = hx509_certs_next_cert(context, d->certs, d->iter, cert);
	    if (ret) {
		hx509_certs_end_seq(context, d->certs, d->iter);
		d->iter = NULL;
		hx509_certs_free(&d->certs);
		return ret;
	    }
	    if (*cert) {
		ret = 0;
		break;
	    }
	    hx509_certs_end_seq(context, d->certs, d->iter);
	    d->iter = NULL;
	    hx509_certs_free(&d->certs);
	}

	dir = readdir(d->dir);
	if (dir == NULL) {
	    ret = 0;
	    break;
	}
	if (strcmp(dir->d_name, ".") == 0 || strcmp(dir->d_name, "..") == 0)
	    continue;

	if (asprintf(&fn, "FILE:%s/%s", (char *)data, dir->d_name) == -1)
	    return ENOMEM;

	ret = hx509_certs_init(context, fn, 0, NULL, &d->certs);
	if (ret == 0) {

	    ret = hx509_certs_start_seq(context, d->certs, &d->iter);
	    if (ret)
	    hx509_certs_free(&d->certs);
	}
	/* ignore errors */
	if (ret) {
	    d->certs = NULL;
	    ret = 0;
	}

	free(fn);
    } while(ret == 0);

    return ret;
}


static int
dir_iter_end(hx509_context context,
	     hx509_certs certs,
	     void *data,
	     void *cursor)
{
    struct dircursor *d = cursor;

    if (d->certs) {
	hx509_certs_end_seq(context, d->certs, d->iter);
	d->iter = NULL;
	hx509_certs_free(&d->certs);
    }
    closedir(d->dir);
    free(d);
    return 0;
}


static struct hx509_keyset_ops keyset_dir = {
    "DIR",
    0,
    dir_init,
    NULL,
    dir_free,
    NULL,
    NULL,
    dir_iter_start,
    dir_iter,
    dir_iter_end
};

void
_hx509_ks_dir_register(hx509_context context)
{
    _hx509_ks_register(context, &keyset_dir);
}
