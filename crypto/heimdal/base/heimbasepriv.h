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

typedef void (*heim_type_init)(void *);
typedef heim_object_t (*heim_type_copy)(void *);
typedef int (*heim_type_cmp)(void *, void *);
typedef unsigned long (*heim_type_hash)(void *);

typedef struct heim_type_data *heim_type_t;

enum {
    HEIM_TID_NUMBER = 0,
    HEIM_TID_NULL = 1,
    HEIM_TID_BOOL = 2,
    HEIM_TID_TAGGED_UNUSED2 = 3,
    HEIM_TID_TAGGED_UNUSED3 = 4,
    HEIM_TID_TAGGED_UNUSED4 = 5,
    HEIM_TID_TAGGED_UNUSED5 = 6,
    HEIM_TID_TAGGED_UNUSED6 = 7,
    HEIM_TID_MEMORY = 128,
    HEIM_TID_ARRAY = 129,
    HEIM_TID_DICT = 130,
    HEIM_TID_STRING = 131,
    HEIM_TID_AUTORELEASE = 132,
    HEIM_TID_USER = 255

};

struct heim_type_data {
    heim_tid_t tid;
    const char *name;
    heim_type_init init;
    heim_type_dealloc dealloc;
    heim_type_copy copy;
    heim_type_cmp cmp;
    heim_type_hash hash;
};

heim_type_t _heim_get_isa(heim_object_t);

heim_type_t
_heim_create_type(const char *name,
		  heim_type_init init,
		  heim_type_dealloc dealloc,
		  heim_type_copy copy,
		  heim_type_cmp cmp,
		  heim_type_hash hash);

heim_object_t
_heim_alloc_object(heim_type_t type, size_t size);

heim_tid_t
_heim_type_get_tid(heim_type_t type);

/* tagged tid */
extern struct heim_type_data _heim_null_object;
extern struct heim_type_data _heim_bool_object;
extern struct heim_type_data _heim_number_object;
extern struct heim_type_data _heim_string_object;
