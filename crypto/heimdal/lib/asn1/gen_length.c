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
length_primitive (const char *typename,
		  const char *name,
		  const char *variable)
{
    fprintf (codefile, "%s += der_length_%s(%s);\n", variable, typename, name);
}

/* XXX same as der_length_tag */
static size_t
length_tag(unsigned int tag)
{
    size_t len = 0;

    if(tag <= 30)
	return 1;
    while(tag) {
	tag /= 128;
	len++;
    }
    return len + 1;
}


static int
length_type (const char *name, const Type *t,
	     const char *variable, const char *tmpstr)
{
    switch (t->type) {
    case TType:
#if 0
	length_type (name, t->symbol->type);
#endif
	fprintf (codefile, "%s += length_%s(%s);\n",
		 variable, t->symbol->gen_name, name);
	break;
    case TInteger:
	if(t->members) {
	    fprintf(codefile,
		    "{\n"
		    "int enumint = *%s;\n", name);
	    length_primitive ("integer", "&enumint", variable);
	    fprintf(codefile, "}\n");
	} else if (t->range == NULL) {
	    length_primitive ("heim_integer", name, variable);
	} else if (t->range->min == INT_MIN && t->range->max == INT_MAX) {
	    length_primitive ("integer", name, variable);
	} else if (t->range->min == 0 && t->range->max == UINT_MAX) {
	    length_primitive ("unsigned", name, variable);
	} else if (t->range->min == 0 && t->range->max == INT_MAX) {
	    length_primitive ("unsigned", name, variable);
	} else
	    errx(1, "%s: unsupported range %d -> %d",
		 name, t->range->min, t->range->max);

	break;
    case TBoolean:
	fprintf (codefile, "%s += 1;\n", variable);
	break;
    case TEnumerated :
	length_primitive ("enumerated", name, variable);
	break;
    case TOctetString:
	length_primitive ("octet_string", name, variable);
	break;
    case TBitString: {
	if (ASN1_TAILQ_EMPTY(t->members))
	    length_primitive("bit_string", name, variable);
	else {
	    if (!rfc1510_bitstring) {
		Member *m;
		int pos = ASN1_TAILQ_LAST(t->members, memhead)->val;

		fprintf(codefile,
			"do {\n");
		ASN1_TAILQ_FOREACH_REVERSE(m, t->members, memhead, members) {
		    while (m->val / 8 < pos / 8) {
			pos -= 8;
		    }
		    fprintf (codefile,
			     "if((%s)->%s) { %s += %d; break; }\n",
			     name, m->gen_name, variable, (pos + 8) / 8);
		}
		fprintf(codefile,
			"} while(0);\n");
		fprintf (codefile, "%s += 1;\n", variable);
	    } else {
		fprintf (codefile, "%s += 5;\n", variable);
	    }
	}
	break;
    }
    case TSet:
    case TSequence:
    case TChoice: {
	Member *m, *have_ellipsis = NULL;

	if (t->members == NULL)
	    break;

	if(t->type == TChoice)
	    fprintf (codefile, "switch((%s)->element) {\n", name);

	ASN1_TAILQ_FOREACH(m, t->members, members) {
	    char *s;

	    if (m->ellipsis) {
		have_ellipsis = m;
		continue;
	    }

	    if(t->type == TChoice)
		fprintf(codefile, "case %s:\n", m->label);

	    if (asprintf (&s, "%s(%s)->%s%s",
			  m->optional ? "" : "&", name,
			  t->type == TChoice ? "u." : "", m->gen_name) < 0 || s == NULL)
		errx(1, "malloc");
	    if (m->optional)
		fprintf (codefile, "if(%s)", s);
	    else if(m->defval)
		gen_compare_defval(s + 1, m->defval);
	    fprintf (codefile, "{\n"
		     "size_t %s_oldret = %s;\n"
		     "%s = 0;\n", tmpstr, variable, variable);
	    length_type (s, m->type, "ret", m->gen_name);
	    fprintf (codefile, "ret += %s_oldret;\n", tmpstr);
	    fprintf (codefile, "}\n");
	    free (s);
	    if(t->type == TChoice)
		fprintf(codefile, "break;\n");
	}
	if(t->type == TChoice) {
	    if (have_ellipsis)
		fprintf(codefile,
			"case %s:\n"
			"ret += (%s)->u.%s.length;\n"
			"break;\n",
			have_ellipsis->label,
			name,
			have_ellipsis->gen_name);
	    fprintf (codefile, "}\n"); /* switch */
	}
	break;
    }
    case TSetOf:
    case TSequenceOf: {
	char *n = NULL;
	char *sname = NULL;

	fprintf (codefile,
		 "{\n"
		 "size_t %s_oldret = %s;\n"
		 "int i;\n"
		 "%s = 0;\n",
		 tmpstr, variable, variable);

	fprintf (codefile, "for(i = (%s)->len - 1; i >= 0; --i){\n", name);
	fprintf (codefile, "size_t %s_for_oldret = %s;\n"
		 "%s = 0;\n", tmpstr, variable, variable);
	if (asprintf (&n, "&(%s)->val[i]", name) < 0  || n == NULL)
	    errx(1, "malloc");
	if (asprintf (&sname, "%s_S_Of", tmpstr) < 0 || sname == NULL)
	    errx(1, "malloc");
	length_type(n, t->subtype, variable, sname);
	fprintf (codefile, "%s += %s_for_oldret;\n",
		 variable, tmpstr);
	fprintf (codefile, "}\n");

	fprintf (codefile,
		 "%s += %s_oldret;\n"
		 "}\n", variable, tmpstr);
	free(n);
	free(sname);
	break;
    }
    case TGeneralizedTime:
	length_primitive ("generalized_time", name, variable);
	break;
    case TGeneralString:
	length_primitive ("general_string", name, variable);
	break;
    case TTeletexString:
	length_primitive ("general_string", name, variable);
	break;
    case TUTCTime:
	length_primitive ("utctime", name, variable);
	break;
    case TUTF8String:
	length_primitive ("utf8string", name, variable);
	break;
    case TPrintableString:
	length_primitive ("printable_string", name, variable);
	break;
    case TIA5String:
	length_primitive ("ia5_string", name, variable);
	break;
    case TBMPString:
	length_primitive ("bmp_string", name, variable);
	break;
    case TUniversalString:
	length_primitive ("universal_string", name, variable);
	break;
    case TVisibleString:
	length_primitive ("visible_string", name, variable);
	break;
    case TNull:
	fprintf (codefile, "/* NULL */\n");
	break;
    case TTag:{
    	char *tname = NULL;
	if (asprintf(&tname, "%s_tag", tmpstr) < 0 || tname == NULL)
	    errx(1, "malloc");
	length_type (name, t->subtype, variable, tname);
	fprintf (codefile, "ret += %lu + der_length_len (ret);\n",
		 (unsigned long)length_tag(t->tag.tagvalue));
	free(tname);
	break;
    }
    case TOID:
	length_primitive ("oid", name, variable);
	break;
    default :
	abort ();
    }
    return 0;
}

void
generate_type_length (const Symbol *s)
{
    fprintf (codefile,
	     "size_t ASN1CALL\n"
	     "length_%s(const %s *data)\n"
	     "{\n"
	     "size_t ret = 0;\n",
	     s->gen_name, s->gen_name);

    length_type ("data", s->type, "ret", "Top");
    fprintf (codefile, "return ret;\n}\n\n");
}

