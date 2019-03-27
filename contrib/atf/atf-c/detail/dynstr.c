/* Copyright (c) 2008 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE NETBSD FOUNDATION, INC. AND
 * CONTRIBUTORS ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES,
 * INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE
 * GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
 * IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.  */

#include "atf-c/detail/dynstr.h"

#include <errno.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "atf-c/detail/sanity.h"
#include "atf-c/detail/text.h"
#include "atf-c/error.h"

/* ---------------------------------------------------------------------
 * Auxiliary functions.
 * --------------------------------------------------------------------- */

static
atf_error_t
resize(atf_dynstr_t *ad, size_t newsize)
{
    char *newdata;
    atf_error_t err;

    PRE(newsize > ad->m_datasize);

    newdata = (char *)malloc(newsize);
    if (newdata == NULL) {
        err = atf_no_memory_error();
    } else {
        strcpy(newdata, ad->m_data);
        free(ad->m_data);
        ad->m_data = newdata;
        ad->m_datasize = newsize;
        err = atf_no_error();
    }

    return err;
}

static
atf_error_t
prepend_or_append(atf_dynstr_t *ad, const char *fmt, va_list ap,
                  bool prepend)
{
    char *aux;
    atf_error_t err;
    size_t newlen;
    va_list ap2;

    va_copy(ap2, ap);
    err = atf_text_format_ap(&aux, fmt, ap2);
    va_end(ap2);
    if (atf_is_error(err))
        goto out;
    newlen = ad->m_length + strlen(aux);

    if (newlen + sizeof(char) > ad->m_datasize) {
        err = resize(ad, newlen + sizeof(char));
        if (atf_is_error(err))
            goto out_free;
    }

    if (prepend) {
        memmove(ad->m_data + strlen(aux), ad->m_data, ad->m_length + 1);
        memcpy(ad->m_data, aux, strlen(aux));
    } else
        strcpy(ad->m_data + ad->m_length, aux);
    ad->m_length = newlen;
    err = atf_no_error();

out_free:
    free(aux);
out:
    return err;
}

/* ---------------------------------------------------------------------
 * The "atf_dynstr" type.
 * --------------------------------------------------------------------- */

/*
 * Constants.
 */

const size_t atf_dynstr_npos = SIZE_MAX;

/*
 * Constructors and destructors.
 */

atf_error_t
atf_dynstr_init(atf_dynstr_t *ad)
{
    atf_error_t err;

    ad->m_data = (char *)malloc(sizeof(char));
    if (ad->m_data == NULL) {
        err = atf_no_memory_error();
        goto out;
    }

    ad->m_data[0] = '\0';
    ad->m_datasize = 1;
    ad->m_length = 0;
    err = atf_no_error();

out:
    return err;
}

atf_error_t
atf_dynstr_init_ap(atf_dynstr_t *ad, const char *fmt, va_list ap)
{
    atf_error_t err;

    ad->m_datasize = strlen(fmt) + 1;
    ad->m_length = 0;

    do {
        va_list ap2;
        int ret;

        ad->m_datasize *= 2;
        ad->m_data = (char *)malloc(ad->m_datasize);
        if (ad->m_data == NULL) {
            err = atf_no_memory_error();
            goto out;
        }

        va_copy(ap2, ap);
        ret = vsnprintf(ad->m_data, ad->m_datasize, fmt, ap2);
        va_end(ap2);
        if (ret < 0) {
            free(ad->m_data);
            err = atf_libc_error(errno, "Cannot format string");
            goto out;
        }

        INV(ret >= 0);
        if ((size_t)ret >= ad->m_datasize) {
            free(ad->m_data);
            ad->m_data = NULL;
        }
        ad->m_length = ret;
    } while (ad->m_length >= ad->m_datasize);

    err = atf_no_error();
out:
    POST(atf_is_error(err) || ad->m_data != NULL);
    return err;
}

atf_error_t
atf_dynstr_init_fmt(atf_dynstr_t *ad, const char *fmt, ...)
{
    va_list ap;
    atf_error_t err;

    va_start(ap, fmt);
    err = atf_dynstr_init_ap(ad, fmt, ap);
    va_end(ap);

    return err;
}

atf_error_t
atf_dynstr_init_raw(atf_dynstr_t *ad, const void *mem, size_t memlen)
{
    atf_error_t err;

    if (memlen >= SIZE_MAX - 1) {
        err = atf_no_memory_error();
        goto out;
    }

    ad->m_data = (char *)malloc(memlen + 1);
    if (ad->m_data == NULL) {
        err = atf_no_memory_error();
        goto out;
    }

    ad->m_datasize = memlen + 1;
    memcpy(ad->m_data, mem, memlen);
    ad->m_data[memlen] = '\0';
    ad->m_length = strlen(ad->m_data);
    INV(ad->m_length <= memlen);
    err = atf_no_error();

out:
    return err;
}

atf_error_t
atf_dynstr_init_rep(atf_dynstr_t *ad, size_t len, char ch)
{
    atf_error_t err;

    if (len == SIZE_MAX) {
        err = atf_no_memory_error();
        goto out;
    }

    ad->m_datasize = (len + 1) * sizeof(char);
    ad->m_data = (char *)malloc(ad->m_datasize);
    if (ad->m_data == NULL) {
        err = atf_no_memory_error();
        goto out;
    }

    memset(ad->m_data, ch, len);
    ad->m_data[len] = '\0';
    ad->m_length = len;
    err = atf_no_error();

out:
    return err;
}

atf_error_t
atf_dynstr_init_substr(atf_dynstr_t *ad, const atf_dynstr_t *src,
                       size_t beg, size_t end)
{
    if (beg > src->m_length)
        beg = src->m_length;

    if (end == atf_dynstr_npos || end > src->m_length)
        end = src->m_length;

    return atf_dynstr_init_raw(ad, src->m_data + beg, end - beg);
}

atf_error_t
atf_dynstr_copy(atf_dynstr_t *dest, const atf_dynstr_t *src)
{
    atf_error_t err;

    dest->m_data = (char *)malloc(src->m_datasize);
    if (dest->m_data == NULL)
        err = atf_no_memory_error();
    else {
        memcpy(dest->m_data, src->m_data, src->m_datasize);
        dest->m_datasize = src->m_datasize;
        dest->m_length = src->m_length;
        err = atf_no_error();
    }

    return err;
}

void
atf_dynstr_fini(atf_dynstr_t *ad)
{
    INV(ad->m_data != NULL);
    free(ad->m_data);
}

char *
atf_dynstr_fini_disown(atf_dynstr_t *ad)
{
    INV(ad->m_data != NULL);
    return ad->m_data;
}

/*
 * Getters.
 */

const char *
atf_dynstr_cstring(const atf_dynstr_t *ad)
{
    return ad->m_data;
}

size_t
atf_dynstr_length(const atf_dynstr_t *ad)
{
    return ad->m_length;
}

size_t
atf_dynstr_rfind_ch(const atf_dynstr_t *ad, char ch)
{
    size_t pos;

    for (pos = ad->m_length; pos > 0 && ad->m_data[pos - 1] != ch; pos--)
        ;

    return pos == 0 ? atf_dynstr_npos : pos - 1;
}

/*
 * Modifiers.
 */

atf_error_t
atf_dynstr_append_ap(atf_dynstr_t *ad, const char *fmt, va_list ap)
{
    atf_error_t err;
    va_list ap2;

    va_copy(ap2, ap);
    err = prepend_or_append(ad, fmt, ap2, false);
    va_end(ap2);

    return err;
}

atf_error_t
atf_dynstr_append_fmt(atf_dynstr_t *ad, const char *fmt, ...)
{
    va_list ap;
    atf_error_t err;

    va_start(ap, fmt);
    err = prepend_or_append(ad, fmt, ap, false);
    va_end(ap);

    return err;
}

void
atf_dynstr_clear(atf_dynstr_t *ad)
{
    ad->m_data[0] = '\0';
    ad->m_length = 0;
}

atf_error_t
atf_dynstr_prepend_ap(atf_dynstr_t *ad, const char *fmt, va_list ap)
{
    atf_error_t err;
    va_list ap2;

    va_copy(ap2, ap);
    err = prepend_or_append(ad, fmt, ap2, true);
    va_end(ap2);

    return err;
}

atf_error_t
atf_dynstr_prepend_fmt(atf_dynstr_t *ad, const char *fmt, ...)
{
    va_list ap;
    atf_error_t err;

    va_start(ap, fmt);
    err = prepend_or_append(ad, fmt, ap, true);
    va_end(ap);

    return err;
}

/*
 * Operators.
 */

bool
atf_equal_dynstr_cstring(const atf_dynstr_t *ad, const char *str)
{
    return strcmp(ad->m_data, str) == 0;
}

bool
atf_equal_dynstr_dynstr(const atf_dynstr_t *s1, const atf_dynstr_t *s2)
{
    return strcmp(s1->m_data, s2->m_data) == 0;
}
