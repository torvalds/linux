/*
 * Copyright (c) 2005 - 2006 Kungliga Tekniska Högskolan
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

int
_hx509_map_file_os(const char *fn, heim_octet_string *os)
{
    size_t length;
    void *data;
    int ret;

    ret = rk_undumpdata(fn, &data, &length);

    os->data = data;
    os->length = length;

    return ret;
}

void
_hx509_unmap_file_os(heim_octet_string *os)
{
    rk_xfree(os->data);
}

int
_hx509_write_file(const char *fn, const void *data, size_t length)
{
    rk_dumpdata(fn, data, length);
    return 0;
}

/*
 *
 */

static void
print_pem_stamp(FILE *f, const char *type, const char *str)
{
    fprintf(f, "-----%s %s-----\n", type, str);
}

int
hx509_pem_write(hx509_context context, const char *type,
		hx509_pem_header *headers, FILE *f,
		const void *data, size_t size)
{
    const char *p = data;
    size_t length;
    char *line;

#define ENCODE_LINE_LENGTH	54

    print_pem_stamp(f, "BEGIN", type);

    while (headers) {
	fprintf(f, "%s: %s\n%s",
		headers->header, headers->value,
		headers->next ? "" : "\n");
	headers = headers->next;
    }

    while (size > 0) {
	ssize_t l;

	length = size;
	if (length > ENCODE_LINE_LENGTH)
	    length = ENCODE_LINE_LENGTH;

	l = base64_encode(p, length, &line);
	if (l < 0) {
	    hx509_set_error_string(context, 0, ENOMEM,
				   "malloc - out of memory");
	    return ENOMEM;
	}
	size -= length;
	fprintf(f, "%s\n", line);
	p += length;
	free(line);
    }

    print_pem_stamp(f, "END", type);

    return 0;
}

/*
 *
 */

int
hx509_pem_add_header(hx509_pem_header **headers,
		     const char *header, const char *value)
{
    hx509_pem_header *h;

    h = calloc(1, sizeof(*h));
    if (h == NULL)
	return ENOMEM;
    h->header = strdup(header);
    if (h->header == NULL) {
	free(h);
	return ENOMEM;
    }
    h->value = strdup(value);
    if (h->value == NULL) {
	free(h->header);
	free(h);
	return ENOMEM;
    }

    h->next = *headers;
    *headers = h;

    return 0;
}

void
hx509_pem_free_header(hx509_pem_header *headers)
{
    hx509_pem_header *h;
    while (headers) {
	h = headers;
	headers = headers->next;
	free(h->header);
	free(h->value);
	free(h);
    }
}

/*
 *
 */

const char *
hx509_pem_find_header(const hx509_pem_header *h, const char *header)
{
    while(h) {
	if (strcmp(header, h->header) == 0)
	    return h->value;
	h = h->next;
    }
    return NULL;
}


/*
 *
 */

int
hx509_pem_read(hx509_context context,
	       FILE *f,
	       hx509_pem_read_func func,
	       void *ctx)
{
    hx509_pem_header *headers = NULL;
    char *type = NULL;
    void *data = NULL;
    size_t len = 0;
    char buf[1024];
    int ret = HX509_PARSING_KEY_FAILED;

    enum { BEFORE, SEARCHHEADER, INHEADER, INDATA, DONE } where;

    where = BEFORE;

    while (fgets(buf, sizeof(buf), f) != NULL) {
	char *p;
	int i;

	i = strcspn(buf, "\n");
	if (buf[i] == '\n') {
	    buf[i] = '\0';
	    if (i > 0)
		i--;
	}
	if (buf[i] == '\r') {
	    buf[i] = '\0';
	    if (i > 0)
		i--;
	}

	switch (where) {
	case BEFORE:
	    if (strncmp("-----BEGIN ", buf, 11) == 0) {
		type = strdup(buf + 11);
		if (type == NULL)
		    break;
		p = strchr(type, '-');
		if (p)
		    *p = '\0';
		where = SEARCHHEADER;
	    }
	    break;
	case SEARCHHEADER:
	    p = strchr(buf, ':');
	    if (p == NULL) {
		where = INDATA;
		goto indata;
	    }
	    /* FALLTHOUGH */
	case INHEADER:
	    if (buf[0] == '\0') {
		where = INDATA;
		break;
	    }
	    p = strchr(buf, ':');
	    if (p) {
		*p++ = '\0';
		while (isspace((int)*p))
		    p++;
		ret = hx509_pem_add_header(&headers, buf, p);
		if (ret)
		    abort();
	    }
	    break;
	case INDATA:
	indata:

	    if (strncmp("-----END ", buf, 9) == 0) {
		where = DONE;
		break;
	    }

	    p = emalloc(i);
	    i = base64_decode(buf, p);
	    if (i < 0) {
		free(p);
		goto out;
	    }

	    data = erealloc(data, len + i);
	    memcpy(((char *)data) + len, p, i);
	    free(p);
	    len += i;
	    break;
	case DONE:
	    abort();
	}

	if (where == DONE) {
	    ret = (*func)(context, type, headers, data, len, ctx);
	out:
	    free(data);
	    data = NULL;
	    len = 0;
	    free(type);
	    type = NULL;
	    where = BEFORE;
	    hx509_pem_free_header(headers);
	    headers = NULL;
	    if (ret)
		break;
	}
    }

    if (where != BEFORE) {
	hx509_set_error_string(context, 0, HX509_PARSING_KEY_FAILED,
			       "File ends before end of PEM end tag");
	ret = HX509_PARSING_KEY_FAILED;
    }
    if (data)
	free(data);
    if (type)
	free(type);
    if (headers)
	hx509_pem_free_header(headers);

    return ret;
}
