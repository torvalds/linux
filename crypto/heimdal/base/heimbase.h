/*
 * Copyright (c) 2010 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2010 Apple Inc. All rights reserved.
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

#ifndef HEIM_BASE_H
#define HEIM_BASE_H 1

#include <sys/types.h>
#include <krb5-types.h>
#include <stdarg.h>
#include <stdbool.h>

typedef void * heim_object_t;
typedef unsigned int heim_tid_t;
typedef heim_object_t heim_bool_t;
typedef heim_object_t heim_null_t;
#define HEIM_BASE_ONCE_INIT 0
typedef long heim_base_once_t; /* XXX arch dependant */

#if !defined(__has_extension)
#define __has_extension(x) 0
#endif

#define HEIM_REQUIRE_GNUC(m,n,p) \
    (((__GNUC__ * 10000) + (__GNUC_MINOR__ * 100) + __GNUC_PATCHLEVEL__) >= \
     (((m) * 10000) + ((n) * 100) + (p)))


#if __has_extension(__builtin_expect) || HEIM_REQUIRE_GNUC(3,0,0)
#define heim_builtin_expect(_op,_res) __builtin_expect(_op,_res)
#else
#define heim_builtin_expect(_op,_res) (_op)
#endif


void *	heim_retain(heim_object_t);
void	heim_release(heim_object_t);

typedef void (*heim_type_dealloc)(void *);

void *
heim_alloc(size_t size, const char *name, heim_type_dealloc dealloc);

heim_tid_t
heim_get_tid(heim_object_t object);

int
heim_cmp(heim_object_t a, heim_object_t b);

unsigned long
heim_get_hash(heim_object_t ptr);

void
heim_base_once_f(heim_base_once_t *, void *, void (*)(void *));

void
heim_abort(const char *fmt, ...)
    HEIMDAL_NORETURN_ATTRIBUTE
    HEIMDAL_PRINTF_ATTRIBUTE((printf, 1, 2));

void
heim_abortv(const char *fmt, va_list ap)
    HEIMDAL_NORETURN_ATTRIBUTE
    HEIMDAL_PRINTF_ATTRIBUTE((printf, 1, 0));

#define heim_assert(e,t) \
    (heim_builtin_expect(!(e), 0) ? heim_abort(t ":" #e) : (void)0)

/*
 *
 */

heim_null_t
heim_null_create(void);

heim_bool_t
heim_bool_create(int);

int
heim_bool_val(heim_bool_t);

/*
 * Array
 */

typedef struct heim_array_data *heim_array_t;

heim_array_t heim_array_create(void);
heim_tid_t heim_array_get_type_id(void);

typedef void (*heim_array_iterator_f_t)(heim_object_t, void *);

int	heim_array_append_value(heim_array_t, heim_object_t);
void	heim_array_iterate_f(heim_array_t, heim_array_iterator_f_t, void *);
#ifdef __BLOCKS__
void	heim_array_iterate(heim_array_t, void (^)(heim_object_t));
#endif
size_t	heim_array_get_length(heim_array_t);
heim_object_t
	heim_array_copy_value(heim_array_t, size_t);
void	heim_array_delete_value(heim_array_t, size_t);
#ifdef __BLOCKS__
void	heim_array_filter(heim_array_t, int (^)(heim_object_t));
#endif

/*
 * Dict
 */

typedef struct heim_dict_data *heim_dict_t;

heim_dict_t heim_dict_create(size_t size);
heim_tid_t heim_dict_get_type_id(void);

typedef void (*heim_dict_iterator_f_t)(heim_object_t, heim_object_t, void *);

int	heim_dict_add_value(heim_dict_t, heim_object_t, heim_object_t);
void	heim_dict_iterate_f(heim_dict_t, heim_dict_iterator_f_t, void *);
#ifdef __BLOCKS__
void	heim_dict_iterate(heim_dict_t, void (^)(heim_object_t, heim_object_t));
#endif

heim_object_t
	heim_dict_copy_value(heim_dict_t, heim_object_t);
void	heim_dict_delete_key(heim_dict_t, heim_object_t);

/*
 * String
 */

typedef struct heim_string_data *heim_string_t;

heim_string_t heim_string_create(const char *);
heim_tid_t heim_string_get_type_id(void);
const char * heim_string_get_utf8(heim_string_t);

/*
 * Number
 */

typedef struct heim_number_data *heim_number_t;

heim_number_t heim_number_create(int);
heim_tid_t heim_number_get_type_id(void);
int heim_number_get_int(heim_number_t);

/*
 *
 */

typedef struct heim_auto_release * heim_auto_release_t;

heim_auto_release_t heim_auto_release_create(void);
void heim_auto_release_drain(heim_auto_release_t);
void heim_auto_release(heim_object_t);

#endif /* HEIM_BASE_H */
