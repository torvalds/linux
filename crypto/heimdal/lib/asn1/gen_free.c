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

#include "gen_locl.h"

RCSID("$Id$");

static void
free_primitive (const char *typename, const char *name)
{
    fprintf (codefile, "der_free_%s(%s);\n", typename, name);
}

static void
free_type (const char *name, const Type *t, int preserve)
{
    switch (t->type) {
    case TType:
#if 0
	free_type (name, t->symbol->type, preserve);
#endif
	fprintf (codefile, "free_%s(%s);\n", t->symbol->gen_name, name);
	break;
    case TInteger:
	if (t->range == NULL && t->members == NULL) {
	    free_primitive ("heim_integer", name);
	    break;
	}
    case TBoolean:
    case TEnumerated :
    case TNull:
    case TGeneralizedTime:
    case TUTCTime:
	break;
    case TBitString:
	if (ASN1_TAILQ_EMPTY(t->members))
	    free_primitive("bit_string", name);
	break;
    case TOctetString:
	free_primitive ("octet_string", name);
	break;
    case TChoice:
    case TSet:
    case TSequence: {
	Member *m, *have_ellipsis = NULL;

	if (t->members == NULL)
	    break;

	if ((t->type == TSequence || t->type == TChoice) && preserve)
	    fprintf(codefile, "der_free_octet_string(&data->_save);\n");

	if(t->type == TChoice)
	    fprintf(codefile, "switch((%s)->element) {\n", name);

	ASN1_TAILQ_FOREACH(m, t->members, members) {
	    char *s;

	    if (m->ellipsis){
		have_ellipsis = m;
		continue;
	    }

	    if(t->type == TChoice)
		fprintf(codefile, "case %s:\n", m->label);
	    if (asprintf (&s, "%s(%s)->%s%s",
			  m->optional ? "" : "&", name,
			  t->type == TChoice ? "u." : "", m->gen_name) < 0 || s == NULL)
		errx(1, "malloc");
	    if(m->optional)
		fprintf(codefile, "if(%s) {\n", s);
	    free_type (s, m->type, FALSE);
	    if(m->optional)
		fprintf(codefile,
			"free(%s);\n"
			"%s = NULL;\n"
			"}\n",s, s);
	    free (s);
	    if(t->type == TChoice)
		fprintf(codefile, "break;\n");
	}

	if(t->type == TChoice) {
	    if (have_ellipsis)
		fprintf(codefile,
			"case %s:\n"
			"der_free_octet_string(&(%s)->u.%s);\n"
			"break;",
			have_ellipsis->label,
			name, have_ellipsis->gen_name);
	    fprintf(codefile, "}\n");
	}
	break;
    }
    case TSetOf:
    case TSequenceOf: {
	char *n;

	fprintf (codefile, "while((%s)->len){\n", name);
	if (asprintf (&n, "&(%s)->val[(%s)->len-1]", name, name) < 0 || n == NULL)
	    errx(1, "malloc");
	free_type(n, t->subtype, FALSE);
	fprintf(codefile,
		"(%s)->len--;\n"
		"}\n",
		name);
	fprintf(codefile,
		"free((%s)->val);\n"
		"(%s)->val = NULL;\n", name, name);
	free(n);
	break;
    }
    case TGeneralString:
	free_primitive ("general_string", name);
	break;
    case TTeletexString:
	free_primitive ("general_string", name);
	break;
    case TUTF8String:
	free_primitive ("utf8string", name);
	break;
    case TPrintableString:
	free_primitive ("printable_string", name);
	break;
    case TIA5String:
	free_primitive ("ia5_string", name);
	break;
    case TBMPString:
	free_primitive ("bmp_string", name);
	break;
    case TUniversalString:
	free_primitive ("universal_string", name);
	break;
    case TVisibleString:
	free_primitive ("visible_string", name);
	break;
    case TTag:
	free_type (name, t->subtype, preserve);
	break;
    case TOID :
	free_primitive ("oid", name);
	break;
    default :
	abort ();
    }
}

void
generate_type_free (const Symbol *s)
{
    int preserve = preserve_type(s->name) ? TRUE : FALSE;

    fprintf (codefile, "void ASN1CALL\n"
	     "free_%s(%s *data)\n"
	     "{\n",
	     s->gen_name, s->gen_name);

    free_type ("data", s->type, preserve);
    fprintf (codefile, "}\n\n");
}

