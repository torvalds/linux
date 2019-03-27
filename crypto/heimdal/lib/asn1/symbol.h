/*
 * Copyright (c) 1997 - 2005 Kungliga Tekniska Högskolan
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

#ifndef _SYMBOL_H
#define _SYMBOL_H

#include "asn1_queue.h"

enum typetype {
    TBitString,
    TBoolean,
    TChoice,
    TEnumerated,
    TGeneralString,
    TTeletexString,
    TGeneralizedTime,
    TIA5String,
    TInteger,
    TNull,
    TOID,
    TOctetString,
    TPrintableString,
    TSequence,
    TSequenceOf,
    TSet,
    TSetOf,
    TTag,
    TType,
    TUTCTime,
    TUTF8String,
    TBMPString,
    TUniversalString,
    TVisibleString
};

typedef enum typetype Typetype;

struct type;

struct value {
    enum { booleanvalue,
	   nullvalue,
	   integervalue,
	   stringvalue,
	   objectidentifiervalue
    } type;
    union {
	int booleanvalue;
	int integervalue;
	char *stringvalue;
	struct objid *objectidentifiervalue;
    } u;
};

struct member {
    char *name;
    char *gen_name;
    char *label;
    int val;
    int optional;
    int ellipsis;
    struct type *type;
    ASN1_TAILQ_ENTRY(member) members;
    struct value *defval;
};

typedef struct member Member;

ASN1_TAILQ_HEAD(memhead, member);

struct symbol;

struct tagtype {
    int tagclass;
    int tagvalue;
    enum { TE_IMPLICIT, TE_EXPLICIT } tagenv;
};

struct range {
    int min;
    int max;
};

enum ctype { CT_CONTENTS, CT_USER } ;

struct constraint_spec;

struct type {
    Typetype type;
    struct memhead *members;
    struct symbol *symbol;
    struct type *subtype;
    struct tagtype tag;
    struct range *range;
    struct constraint_spec *constraint;
};

typedef struct type Type;

struct constraint_spec {
    enum ctype ctype;
    union {
	struct {
	    Type *type;
	    struct value *encoding;
	} content;
    } u;
};

struct objid {
    const char *label;
    int value;
    struct objid *next;
};

struct symbol {
    char *name;
    char *gen_name;
    enum { SUndefined, SValue, Stype } stype;
    struct value *value;
    Type *type;
};

typedef struct symbol Symbol;

void initsym (void);
Symbol *addsym (char *);
void output_name (char *);
int checkundefined(void);
#endif
