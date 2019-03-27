/*
 * Copyright (c) 1997-2005 Kungliga Tekniska Högskolan
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

/* $Id$ */

#ifndef __GEN_LOCL_H__
#define __GEN_LOCL_H__

#include <config.h>

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <ctype.h>
#include <time.h>
#include <errno.h>
#include <err.h>
#include <roken.h>
#include "hash.h"
#include "symbol.h"
#include "asn1-common.h"
#include "der.h"
#include "der-private.h"

void generate_type (const Symbol *);
void generate_constant (const Symbol *);
void generate_type_encode (const Symbol *);
void generate_type_decode (const Symbol *);
void generate_type_free (const Symbol *);
void generate_type_length (const Symbol *);
void generate_type_copy (const Symbol *);
void generate_type_seq (const Symbol *);
void generate_glue (const Type *, const char*);

const char *classname(Der_class);
const char *valuename(Der_class, int);

void gen_compare_defval(const char *, struct value *);
void gen_assign_defval(const char *, struct value *);


void init_generate (const char *, const char *);
const char *get_filename (void);
void close_generate(void);
void add_import(const char *);
void add_export(const char *);
int is_export(const char *);
int yyparse(void);
int is_primitive_type(int);

int preserve_type(const char *);
int seq_type(const char *);

void generate_header_of_codefile(const char *);
void close_codefile(void);

int is_template_compat (const Symbol *);
void generate_template(const Symbol *);
void gen_template_import(const Symbol *);


extern FILE *privheaderfile, *headerfile, *codefile, *logfile, *templatefile;
extern int support_ber;
extern int template_flag;
extern int rfc1510_bitstring;
extern int one_code_file;

extern int error_flag;

#endif /* __GEN_LOCL_H__ */
