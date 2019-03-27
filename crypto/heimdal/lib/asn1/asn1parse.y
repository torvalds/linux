/*
 * Copyright (c) 1997 - 2007 Kungliga Tekniska Högskolan
 * (Royal Institute of Technology, Stockholm, Sweden).
 * All rights reserved.
 *
 * Portions Copyright (c) 2009 Apple Inc. All rights reserved.
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

%{

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "symbol.h"
#include "lex.h"
#include "gen_locl.h"
#include "der.h"

RCSID("$Id$");

static Type *new_type (Typetype t);
static struct constraint_spec *new_constraint_spec(enum ctype);
static Type *new_tag(int tagclass, int tagvalue, int tagenv, Type *oldtype);
void yyerror (const char *);
static struct objid *new_objid(const char *label, int value);
static void add_oid_to_tail(struct objid *, struct objid *);
static void fix_labels(Symbol *s);

struct string_list {
    char *string;
    struct string_list *next;
};

/* Declarations for Bison */
#define YYMALLOC malloc
#define YYFREE   free

%}

%union {
    int constant;
    struct value *value;
    struct range *range;
    char *name;
    Type *type;
    Member *member;
    struct objid *objid;
    char *defval;
    struct string_list *sl;
    struct tagtype tag;
    struct memhead *members;
    struct constraint_spec *constraint_spec;
}

%token kw_ABSENT
%token kw_ABSTRACT_SYNTAX
%token kw_ALL
%token kw_APPLICATION
%token kw_AUTOMATIC
%token kw_BEGIN
%token kw_BIT
%token kw_BMPString
%token kw_BOOLEAN
%token kw_BY
%token kw_CHARACTER
%token kw_CHOICE
%token kw_CLASS
%token kw_COMPONENT
%token kw_COMPONENTS
%token kw_CONSTRAINED
%token kw_CONTAINING
%token kw_DEFAULT
%token kw_DEFINITIONS
%token kw_EMBEDDED
%token kw_ENCODED
%token kw_END
%token kw_ENUMERATED
%token kw_EXCEPT
%token kw_EXPLICIT
%token kw_EXPORTS
%token kw_EXTENSIBILITY
%token kw_EXTERNAL
%token kw_FALSE
%token kw_FROM
%token kw_GeneralString
%token kw_GeneralizedTime
%token kw_GraphicString
%token kw_IA5String
%token kw_IDENTIFIER
%token kw_IMPLICIT
%token kw_IMPLIED
%token kw_IMPORTS
%token kw_INCLUDES
%token kw_INSTANCE
%token kw_INTEGER
%token kw_INTERSECTION
%token kw_ISO646String
%token kw_MAX
%token kw_MIN
%token kw_MINUS_INFINITY
%token kw_NULL
%token kw_NumericString
%token kw_OBJECT
%token kw_OCTET
%token kw_OF
%token kw_OPTIONAL
%token kw_ObjectDescriptor
%token kw_PATTERN
%token kw_PDV
%token kw_PLUS_INFINITY
%token kw_PRESENT
%token kw_PRIVATE
%token kw_PrintableString
%token kw_REAL
%token kw_RELATIVE_OID
%token kw_SEQUENCE
%token kw_SET
%token kw_SIZE
%token kw_STRING
%token kw_SYNTAX
%token kw_T61String
%token kw_TAGS
%token kw_TRUE
%token kw_TYPE_IDENTIFIER
%token kw_TeletexString
%token kw_UNION
%token kw_UNIQUE
%token kw_UNIVERSAL
%token kw_UTCTime
%token kw_UTF8String
%token kw_UniversalString
%token kw_VideotexString
%token kw_VisibleString
%token kw_WITH

%token RANGE
%token EEQUAL
%token ELLIPSIS

%token <name> IDENTIFIER  referencename
%token <name> STRING

%token <constant> NUMBER
%type <constant> SignedNumber
%type <constant> Class tagenv

%type <value> Value
%type <value> BuiltinValue
%type <value> IntegerValue
%type <value> BooleanValue
%type <value> ObjectIdentifierValue
%type <value> CharacterStringValue
%type <value> NullValue
%type <value> DefinedValue
%type <value> ReferencedValue
%type <value> Valuereference

%type <type> Type
%type <type> BuiltinType
%type <type> BitStringType
%type <type> BooleanType
%type <type> ChoiceType
%type <type> ConstrainedType
%type <type> EnumeratedType
%type <type> IntegerType
%type <type> NullType
%type <type> OctetStringType
%type <type> SequenceType
%type <type> SequenceOfType
%type <type> SetType
%type <type> SetOfType
%type <type> TaggedType
%type <type> ReferencedType
%type <type> DefinedType
%type <type> UsefulType
%type <type> ObjectIdentifierType
%type <type> CharacterStringType
%type <type> RestrictedCharactedStringType

%type <tag> Tag

%type <member> ComponentType
%type <member> NamedBit
%type <member> NamedNumber
%type <member> NamedType
%type <members> ComponentTypeList
%type <members> Enumerations
%type <members> NamedBitList
%type <members> NamedNumberList

%type <objid> objid objid_list objid_element objid_opt
%type <range> range size

%type <sl> referencenames

%type <constraint_spec> Constraint
%type <constraint_spec> ConstraintSpec
%type <constraint_spec> GeneralConstraint
%type <constraint_spec> ContentsConstraint
%type <constraint_spec> UserDefinedConstraint



%start ModuleDefinition

%%

ModuleDefinition: IDENTIFIER objid_opt kw_DEFINITIONS TagDefault ExtensionDefault
			EEQUAL kw_BEGIN ModuleBody kw_END
		{
			checkundefined();
		}
		;

TagDefault	: kw_EXPLICIT kw_TAGS
		| kw_IMPLICIT kw_TAGS
		      { lex_error_message("implicit tagging is not supported"); }
		| kw_AUTOMATIC kw_TAGS
		      { lex_error_message("automatic tagging is not supported"); }
		| /* empty */
		;

ExtensionDefault: kw_EXTENSIBILITY kw_IMPLIED
		      { lex_error_message("no extensibility options supported"); }
		| /* empty */
		;

ModuleBody	: Exports Imports AssignmentList
		| /* empty */
		;

Imports		: kw_IMPORTS SymbolsImported ';'
		| /* empty */
		;

SymbolsImported	: SymbolsFromModuleList
		| /* empty */
		;

SymbolsFromModuleList: SymbolsFromModule
		| SymbolsFromModuleList SymbolsFromModule
		;

SymbolsFromModule: referencenames kw_FROM IDENTIFIER objid_opt
		{
		    struct string_list *sl;
		    for(sl = $1; sl != NULL; sl = sl->next) {
			Symbol *s = addsym(sl->string);
			s->stype = Stype;
			gen_template_import(s);
		    }
		    add_import($3);
		}
		;

Exports		: kw_EXPORTS referencenames ';'
		{
		    struct string_list *sl;
		    for(sl = $2; sl != NULL; sl = sl->next)
			add_export(sl->string);
		}
		| kw_EXPORTS kw_ALL
		| /* empty */
		;

AssignmentList	: Assignment
		| Assignment AssignmentList
		;

Assignment	: TypeAssignment
		| ValueAssignment
		;

referencenames	: IDENTIFIER ',' referencenames
		{
		    $$ = emalloc(sizeof(*$$));
		    $$->string = $1;
		    $$->next = $3;
		}
		| IDENTIFIER
		{
		    $$ = emalloc(sizeof(*$$));
		    $$->string = $1;
		    $$->next = NULL;
		}
		;

TypeAssignment	: IDENTIFIER EEQUAL Type
		{
		    Symbol *s = addsym ($1);
		    s->stype = Stype;
		    s->type = $3;
		    fix_labels(s);
		    generate_type (s);
		}
		;

Type		: BuiltinType
		| ReferencedType
		| ConstrainedType
		;

BuiltinType	: BitStringType
		| BooleanType
		| CharacterStringType
		| ChoiceType
		| EnumeratedType
		| IntegerType
		| NullType
		| ObjectIdentifierType
		| OctetStringType
		| SequenceType
		| SequenceOfType
		| SetType
		| SetOfType
		| TaggedType
		;

BooleanType	: kw_BOOLEAN
		{
			$$ = new_tag(ASN1_C_UNIV, UT_Boolean,
				     TE_EXPLICIT, new_type(TBoolean));
		}
		;

range		: '(' Value RANGE Value ')'
		{
		    if($2->type != integervalue)
			lex_error_message("Non-integer used in first part of range");
		    if($2->type != integervalue)
			lex_error_message("Non-integer in second part of range");
		    $$ = ecalloc(1, sizeof(*$$));
		    $$->min = $2->u.integervalue;
		    $$->max = $4->u.integervalue;
		}
		| '(' Value RANGE kw_MAX ')'
		{
		    if($2->type != integervalue)
			lex_error_message("Non-integer in first part of range");
		    $$ = ecalloc(1, sizeof(*$$));
		    $$->min = $2->u.integervalue;
		    $$->max = $2->u.integervalue - 1;
		}
		| '(' kw_MIN RANGE Value ')'
		{
		    if($4->type != integervalue)
			lex_error_message("Non-integer in second part of range");
		    $$ = ecalloc(1, sizeof(*$$));
		    $$->min = $4->u.integervalue + 2;
		    $$->max = $4->u.integervalue;
		}
		| '(' Value ')'
		{
		    if($2->type != integervalue)
			lex_error_message("Non-integer used in limit");
		    $$ = ecalloc(1, sizeof(*$$));
		    $$->min = $2->u.integervalue;
		    $$->max = $2->u.integervalue;
		}
		;


IntegerType	: kw_INTEGER
		{
			$$ = new_tag(ASN1_C_UNIV, UT_Integer,
				     TE_EXPLICIT, new_type(TInteger));
		}
		| kw_INTEGER range
		{
			$$ = new_type(TInteger);
			$$->range = $2;
			$$ = new_tag(ASN1_C_UNIV, UT_Integer, TE_EXPLICIT, $$);
		}
		| kw_INTEGER '{' NamedNumberList '}'
		{
		  $$ = new_type(TInteger);
		  $$->members = $3;
		  $$ = new_tag(ASN1_C_UNIV, UT_Integer, TE_EXPLICIT, $$);
		}
		;

NamedNumberList	: NamedNumber
		{
			$$ = emalloc(sizeof(*$$));
			ASN1_TAILQ_INIT($$);
			ASN1_TAILQ_INSERT_HEAD($$, $1, members);
		}
		| NamedNumberList ',' NamedNumber
		{
			ASN1_TAILQ_INSERT_TAIL($1, $3, members);
			$$ = $1;
		}
		| NamedNumberList ',' ELLIPSIS
			{ $$ = $1; } /* XXX used for Enumerations */
		;

NamedNumber	: IDENTIFIER '(' SignedNumber ')'
		{
			$$ = emalloc(sizeof(*$$));
			$$->name = $1;
			$$->gen_name = estrdup($1);
			output_name ($$->gen_name);
			$$->val = $3;
			$$->optional = 0;
			$$->ellipsis = 0;
			$$->type = NULL;
		}
		;

EnumeratedType	: kw_ENUMERATED '{' Enumerations '}'
		{
		  $$ = new_type(TInteger);
		  $$->members = $3;
		  $$ = new_tag(ASN1_C_UNIV, UT_Enumerated, TE_EXPLICIT, $$);
		}
		;

Enumerations	: NamedNumberList /* XXX */
		;

BitStringType	: kw_BIT kw_STRING
		{
		  $$ = new_type(TBitString);
		  $$->members = emalloc(sizeof(*$$->members));
		  ASN1_TAILQ_INIT($$->members);
		  $$ = new_tag(ASN1_C_UNIV, UT_BitString, TE_EXPLICIT, $$);
		}
		| kw_BIT kw_STRING '{' NamedBitList '}'
		{
		  $$ = new_type(TBitString);
		  $$->members = $4;
		  $$ = new_tag(ASN1_C_UNIV, UT_BitString, TE_EXPLICIT, $$);
		}
		;

ObjectIdentifierType: kw_OBJECT kw_IDENTIFIER
		{
			$$ = new_tag(ASN1_C_UNIV, UT_OID,
				     TE_EXPLICIT, new_type(TOID));
		}
		;
OctetStringType	: kw_OCTET kw_STRING size
		{
		    Type *t = new_type(TOctetString);
		    t->range = $3;
		    $$ = new_tag(ASN1_C_UNIV, UT_OctetString,
				 TE_EXPLICIT, t);
		}
		;

NullType	: kw_NULL
		{
			$$ = new_tag(ASN1_C_UNIV, UT_Null,
				     TE_EXPLICIT, new_type(TNull));
		}
		;

size		:
		{ $$ = NULL; }
		| kw_SIZE range
		{ $$ = $2; }
		;


SequenceType	: kw_SEQUENCE '{' /* ComponentTypeLists */ ComponentTypeList '}'
		{
		  $$ = new_type(TSequence);
		  $$->members = $3;
		  $$ = new_tag(ASN1_C_UNIV, UT_Sequence, TE_EXPLICIT, $$);
		}
		| kw_SEQUENCE '{' '}'
		{
		  $$ = new_type(TSequence);
		  $$->members = NULL;
		  $$ = new_tag(ASN1_C_UNIV, UT_Sequence, TE_EXPLICIT, $$);
		}
		;

SequenceOfType	: kw_SEQUENCE size kw_OF Type
		{
		  $$ = new_type(TSequenceOf);
		  $$->range = $2;
		  $$->subtype = $4;
		  $$ = new_tag(ASN1_C_UNIV, UT_Sequence, TE_EXPLICIT, $$);
		}
		;

SetType		: kw_SET '{' /* ComponentTypeLists */ ComponentTypeList '}'
		{
		  $$ = new_type(TSet);
		  $$->members = $3;
		  $$ = new_tag(ASN1_C_UNIV, UT_Set, TE_EXPLICIT, $$);
		}
		| kw_SET '{' '}'
		{
		  $$ = new_type(TSet);
		  $$->members = NULL;
		  $$ = new_tag(ASN1_C_UNIV, UT_Set, TE_EXPLICIT, $$);
		}
		;

SetOfType	: kw_SET kw_OF Type
		{
		  $$ = new_type(TSetOf);
		  $$->subtype = $3;
		  $$ = new_tag(ASN1_C_UNIV, UT_Set, TE_EXPLICIT, $$);
		}
		;

ChoiceType	: kw_CHOICE '{' /* AlternativeTypeLists */ ComponentTypeList '}'
		{
		  $$ = new_type(TChoice);
		  $$->members = $3;
		}
		;

ReferencedType	: DefinedType
		| UsefulType
		;

DefinedType	: IDENTIFIER
		{
		  Symbol *s = addsym($1);
		  $$ = new_type(TType);
		  if(s->stype != Stype && s->stype != SUndefined)
		    lex_error_message ("%s is not a type\n", $1);
		  else
		    $$->symbol = s;
		}
		;

UsefulType	: kw_GeneralizedTime
		{
			$$ = new_tag(ASN1_C_UNIV, UT_GeneralizedTime,
				     TE_EXPLICIT, new_type(TGeneralizedTime));
		}
		| kw_UTCTime
		{
			$$ = new_tag(ASN1_C_UNIV, UT_UTCTime,
				     TE_EXPLICIT, new_type(TUTCTime));
		}
		;

ConstrainedType	: Type Constraint
		{
		    /* if (Constraint.type == contentConstrant) {
		       assert(Constraint.u.constraint.type == octetstring|bitstring-w/o-NamedBitList); // remember to check type reference too
		       if (Constraint.u.constraint.type) {
		         assert((Constraint.u.constraint.type.length % 8) == 0);
		       }
		      }
		      if (Constraint.u.constraint.encoding) {
		        type == der-oid|ber-oid
		      }
		    */
		}
		;


Constraint	: '(' ConstraintSpec ')'
		{
		    $$ = $2;
		}
		;

ConstraintSpec	: GeneralConstraint
		;

GeneralConstraint: ContentsConstraint
		| UserDefinedConstraint
		;

ContentsConstraint: kw_CONTAINING Type
		{
		    $$ = new_constraint_spec(CT_CONTENTS);
		    $$->u.content.type = $2;
		    $$->u.content.encoding = NULL;
		}
		| kw_ENCODED kw_BY Value
		{
		    if ($3->type != objectidentifiervalue)
			lex_error_message("Non-OID used in ENCODED BY constraint");
		    $$ = new_constraint_spec(CT_CONTENTS);
		    $$->u.content.type = NULL;
		    $$->u.content.encoding = $3;
		}
		| kw_CONTAINING Type kw_ENCODED kw_BY Value
		{
		    if ($5->type != objectidentifiervalue)
			lex_error_message("Non-OID used in ENCODED BY constraint");
		    $$ = new_constraint_spec(CT_CONTENTS);
		    $$->u.content.type = $2;
		    $$->u.content.encoding = $5;
		}
		;

UserDefinedConstraint: kw_CONSTRAINED kw_BY '{' '}'
		{
		    $$ = new_constraint_spec(CT_USER);
		}
		;

TaggedType	: Tag tagenv Type
		{
			$$ = new_type(TTag);
			$$->tag = $1;
			$$->tag.tagenv = $2;
			if($3->type == TTag && $2 == TE_IMPLICIT) {
				$$->subtype = $3->subtype;
				free($3);
			} else
				$$->subtype = $3;
		}
		;

Tag		: '[' Class NUMBER ']'
		{
			$$.tagclass = $2;
			$$.tagvalue = $3;
			$$.tagenv = TE_EXPLICIT;
		}
		;

Class		: /* */
		{
			$$ = ASN1_C_CONTEXT;
		}
		| kw_UNIVERSAL
		{
			$$ = ASN1_C_UNIV;
		}
		| kw_APPLICATION
		{
			$$ = ASN1_C_APPL;
		}
		| kw_PRIVATE
		{
			$$ = ASN1_C_PRIVATE;
		}
		;

tagenv		: /* */
		{
			$$ = TE_EXPLICIT;
		}
		| kw_EXPLICIT
		{
			$$ = TE_EXPLICIT;
		}
		| kw_IMPLICIT
		{
			$$ = TE_IMPLICIT;
		}
		;


ValueAssignment	: IDENTIFIER Type EEQUAL Value
		{
			Symbol *s;
			s = addsym ($1);

			s->stype = SValue;
			s->value = $4;
			generate_constant (s);
		}
		;

CharacterStringType: RestrictedCharactedStringType
		;

RestrictedCharactedStringType: kw_GeneralString
		{
			$$ = new_tag(ASN1_C_UNIV, UT_GeneralString,
				     TE_EXPLICIT, new_type(TGeneralString));
		}
		| kw_TeletexString
		{
			$$ = new_tag(ASN1_C_UNIV, UT_TeletexString,
				     TE_EXPLICIT, new_type(TTeletexString));
		}
		| kw_UTF8String
		{
			$$ = new_tag(ASN1_C_UNIV, UT_UTF8String,
				     TE_EXPLICIT, new_type(TUTF8String));
		}
		| kw_PrintableString
		{
			$$ = new_tag(ASN1_C_UNIV, UT_PrintableString,
				     TE_EXPLICIT, new_type(TPrintableString));
		}
		| kw_VisibleString
		{
			$$ = new_tag(ASN1_C_UNIV, UT_VisibleString,
				     TE_EXPLICIT, new_type(TVisibleString));
		}
		| kw_IA5String
		{
			$$ = new_tag(ASN1_C_UNIV, UT_IA5String,
				     TE_EXPLICIT, new_type(TIA5String));
		}
		| kw_BMPString
		{
			$$ = new_tag(ASN1_C_UNIV, UT_BMPString,
				     TE_EXPLICIT, new_type(TBMPString));
		}
		| kw_UniversalString
		{
			$$ = new_tag(ASN1_C_UNIV, UT_UniversalString,
				     TE_EXPLICIT, new_type(TUniversalString));
		}

		;

ComponentTypeList: ComponentType
		{
			$$ = emalloc(sizeof(*$$));
			ASN1_TAILQ_INIT($$);
			ASN1_TAILQ_INSERT_HEAD($$, $1, members);
		}
		| ComponentTypeList ',' ComponentType
		{
			ASN1_TAILQ_INSERT_TAIL($1, $3, members);
			$$ = $1;
		}
		| ComponentTypeList ',' ELLIPSIS
		{
		        struct member *m = ecalloc(1, sizeof(*m));
			m->name = estrdup("...");
			m->gen_name = estrdup("asn1_ellipsis");
			m->ellipsis = 1;
			ASN1_TAILQ_INSERT_TAIL($1, m, members);
			$$ = $1;
		}
		;

NamedType	: IDENTIFIER Type
		{
		  $$ = emalloc(sizeof(*$$));
		  $$->name = $1;
		  $$->gen_name = estrdup($1);
		  output_name ($$->gen_name);
		  $$->type = $2;
		  $$->ellipsis = 0;
		}
		;

ComponentType	: NamedType
		{
			$$ = $1;
			$$->optional = 0;
			$$->defval = NULL;
		}
		| NamedType kw_OPTIONAL
		{
			$$ = $1;
			$$->optional = 1;
			$$->defval = NULL;
		}
		| NamedType kw_DEFAULT Value
		{
			$$ = $1;
			$$->optional = 0;
			$$->defval = $3;
		}
		;

NamedBitList	: NamedBit
		{
			$$ = emalloc(sizeof(*$$));
			ASN1_TAILQ_INIT($$);
			ASN1_TAILQ_INSERT_HEAD($$, $1, members);
		}
		| NamedBitList ',' NamedBit
		{
			ASN1_TAILQ_INSERT_TAIL($1, $3, members);
			$$ = $1;
		}
		;

NamedBit	: IDENTIFIER '(' NUMBER ')'
		{
		  $$ = emalloc(sizeof(*$$));
		  $$->name = $1;
		  $$->gen_name = estrdup($1);
		  output_name ($$->gen_name);
		  $$->val = $3;
		  $$->optional = 0;
		  $$->ellipsis = 0;
		  $$->type = NULL;
		}
		;

objid_opt	: objid
		| /* empty */ { $$ = NULL; }
		;

objid		: '{' objid_list '}'
		{
			$$ = $2;
		}
		;

objid_list	:  /* empty */
		{
			$$ = NULL;
		}
		| objid_element objid_list
		{
		        if ($2) {
				$$ = $2;
				add_oid_to_tail($2, $1);
			} else {
				$$ = $1;
			}
		}
		;

objid_element	: IDENTIFIER '(' NUMBER ')'
		{
			$$ = new_objid($1, $3);
		}
		| IDENTIFIER
		{
		    Symbol *s = addsym($1);
		    if(s->stype != SValue ||
		       s->value->type != objectidentifiervalue) {
			lex_error_message("%s is not an object identifier\n",
				      s->name);
			exit(1);
		    }
		    $$ = s->value->u.objectidentifiervalue;
		}
		| NUMBER
		{
		    $$ = new_objid(NULL, $1);
		}
		;

Value		: BuiltinValue
		| ReferencedValue
		;

BuiltinValue	: BooleanValue
		| CharacterStringValue
		| IntegerValue
		| ObjectIdentifierValue
		| NullValue
		;

ReferencedValue	: DefinedValue
		;

DefinedValue	: Valuereference
		;

Valuereference	: IDENTIFIER
		{
			Symbol *s = addsym($1);
			if(s->stype != SValue)
				lex_error_message ("%s is not a value\n",
						s->name);
			else
				$$ = s->value;
		}
		;

CharacterStringValue: STRING
		{
			$$ = emalloc(sizeof(*$$));
			$$->type = stringvalue;
			$$->u.stringvalue = $1;
		}
		;

BooleanValue	: kw_TRUE
		{
			$$ = emalloc(sizeof(*$$));
			$$->type = booleanvalue;
			$$->u.booleanvalue = 0;
		}
		| kw_FALSE
		{
			$$ = emalloc(sizeof(*$$));
			$$->type = booleanvalue;
			$$->u.booleanvalue = 0;
		}
		;

IntegerValue	: SignedNumber
		{
			$$ = emalloc(sizeof(*$$));
			$$->type = integervalue;
			$$->u.integervalue = $1;
		}
		;

SignedNumber	: NUMBER
		;

NullValue	: kw_NULL
		{
		}
		;

ObjectIdentifierValue: objid
		{
			$$ = emalloc(sizeof(*$$));
			$$->type = objectidentifiervalue;
			$$->u.objectidentifiervalue = $1;
		}
		;

%%

void
yyerror (const char *s)
{
     lex_error_message ("%s\n", s);
}

static Type *
new_tag(int tagclass, int tagvalue, int tagenv, Type *oldtype)
{
    Type *t;
    if(oldtype->type == TTag && oldtype->tag.tagenv == TE_IMPLICIT) {
	t = oldtype;
	oldtype = oldtype->subtype; /* XXX */
    } else
	t = new_type (TTag);

    t->tag.tagclass = tagclass;
    t->tag.tagvalue = tagvalue;
    t->tag.tagenv = tagenv;
    t->subtype = oldtype;
    return t;
}

static struct objid *
new_objid(const char *label, int value)
{
    struct objid *s;
    s = emalloc(sizeof(*s));
    s->label = label;
    s->value = value;
    s->next = NULL;
    return s;
}

static void
add_oid_to_tail(struct objid *head, struct objid *tail)
{
    struct objid *o;
    o = head;
    while (o->next)
	o = o->next;
    o->next = tail;
}

static Type *
new_type (Typetype tt)
{
    Type *t = ecalloc(1, sizeof(*t));
    t->type = tt;
    return t;
}

static struct constraint_spec *
new_constraint_spec(enum ctype ct)
{
    struct constraint_spec *c = ecalloc(1, sizeof(*c));
    c->ctype = ct;
    return c;
}

static void fix_labels2(Type *t, const char *prefix);
static void fix_labels1(struct memhead *members, const char *prefix)
{
    Member *m;

    if(members == NULL)
	return;
    ASN1_TAILQ_FOREACH(m, members, members) {
	if (asprintf(&m->label, "%s_%s", prefix, m->gen_name) < 0)
	    errx(1, "malloc");
	if (m->label == NULL)
	    errx(1, "malloc");
	if(m->type != NULL)
	    fix_labels2(m->type, m->label);
    }
}

static void fix_labels2(Type *t, const char *prefix)
{
    for(; t; t = t->subtype)
	fix_labels1(t->members, prefix);
}

static void
fix_labels(Symbol *s)
{
    char *p = NULL;
    if (asprintf(&p, "choice_%s", s->gen_name) < 0 || p == NULL)
	errx(1, "malloc");
    fix_labels2(s->type, p);
    free(p);
}
