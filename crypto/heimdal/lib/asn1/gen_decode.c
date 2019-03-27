/*
 * Copyright (c) 1997 - 2006 Kungliga Tekniska Högskolan
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
#include "lex.h"

RCSID("$Id$");

static void
decode_primitive (const char *typename, const char *name, const char *forwstr)
{
#if 0
    fprintf (codefile,
	     "e = decode_%s(p, len, %s, &l);\n"
	     "%s;\n",
	     typename,
	     name,
	     forwstr);
#else
    fprintf (codefile,
	     "e = der_get_%s(p, len, %s, &l);\n"
	     "if(e) %s;\np += l; len -= l; ret += l;\n",
	     typename,
	     name,
	     forwstr);
#endif
}

static void
find_tag (const Type *t,
	  Der_class *cl, Der_type *ty, unsigned *tag)
{
    switch (t->type) {
    case TBitString:
	*cl  = ASN1_C_UNIV;
	*ty  = PRIM;
	*tag = UT_BitString;
	break;
    case TBoolean:
	*cl  = ASN1_C_UNIV;
	*ty  = PRIM;
	*tag = UT_Boolean;
	break;
    case TChoice:
	errx(1, "Cannot have recursive CHOICE");
    case TEnumerated:
	*cl  = ASN1_C_UNIV;
	*ty  = PRIM;
	*tag = UT_Enumerated;
	break;
    case TGeneralString:
	*cl  = ASN1_C_UNIV;
	*ty  = PRIM;
	*tag = UT_GeneralString;
	break;
    case TTeletexString:
	*cl  = ASN1_C_UNIV;
	*ty  = PRIM;
	*tag = UT_TeletexString;
	break;
    case TGeneralizedTime:
	*cl  = ASN1_C_UNIV;
	*ty  = PRIM;
	*tag = UT_GeneralizedTime;
	break;
    case TIA5String:
	*cl  = ASN1_C_UNIV;
	*ty  = PRIM;
	*tag = UT_IA5String;
	break;
    case TInteger:
	*cl  = ASN1_C_UNIV;
	*ty  = PRIM;
	*tag = UT_Integer;
	break;
    case TNull:
	*cl  = ASN1_C_UNIV;
	*ty  = PRIM;
	*tag = UT_Null;
	break;
    case TOID:
	*cl  = ASN1_C_UNIV;
	*ty  = PRIM;
	*tag = UT_OID;
	break;
    case TOctetString:
	*cl  = ASN1_C_UNIV;
	*ty  = PRIM;
	*tag = UT_OctetString;
	break;
    case TPrintableString:
	*cl  = ASN1_C_UNIV;
	*ty  = PRIM;
	*tag = UT_PrintableString;
	break;
    case TSequence:
    case TSequenceOf:
	*cl  = ASN1_C_UNIV;
	*ty  = CONS;
	*tag = UT_Sequence;
	break;
    case TSet:
    case TSetOf:
	*cl  = ASN1_C_UNIV;
	*ty  = CONS;
	*tag = UT_Set;
	break;
    case TTag:
	*cl  = t->tag.tagclass;
	*ty  = is_primitive_type(t->subtype->type) ? PRIM : CONS;
	*tag = t->tag.tagvalue;
	break;
    case TType:
	if ((t->symbol->stype == Stype && t->symbol->type == NULL)
	    || t->symbol->stype == SUndefined) {
	    lex_error_message("%s is imported or still undefined, "
			      " can't generate tag checking data in CHOICE "
			      "without this information",
			      t->symbol->name);
	    exit(1);
	}
	find_tag(t->symbol->type, cl, ty, tag);
	return;
    case TUTCTime:
	*cl  = ASN1_C_UNIV;
	*ty  = PRIM;
	*tag = UT_UTCTime;
	break;
    case TUTF8String:
	*cl  = ASN1_C_UNIV;
	*ty  = PRIM;
	*tag = UT_UTF8String;
	break;
    case TBMPString:
	*cl  = ASN1_C_UNIV;
	*ty  = PRIM;
	*tag = UT_BMPString;
	break;
    case TUniversalString:
	*cl  = ASN1_C_UNIV;
	*ty  = PRIM;
	*tag = UT_UniversalString;
	break;
    case TVisibleString:
	*cl  = ASN1_C_UNIV;
	*ty  = PRIM;
	*tag = UT_VisibleString;
	break;
    default:
	abort();
    }
}

static void
range_check(const char *name,
	    const char *length,
	    const char *forwstr,
	    struct range *r)
{
    if (r->min == r->max + 2 || r->min < r->max)
	fprintf (codefile,
		 "if ((%s)->%s > %d) {\n"
		 "e = ASN1_MAX_CONSTRAINT; %s;\n"
		 "}\n",
		 name, length, r->max, forwstr);
    if (r->min - 1 == r->max || r->min < r->max)
	fprintf (codefile,
		 "if ((%s)->%s < %d) {\n"
		 "e = ASN1_MIN_CONSTRAINT; %s;\n"
		 "}\n",
		 name, length, r->min, forwstr);
    if (r->max == r->min)
	fprintf (codefile,
		 "if ((%s)->%s != %d) {\n"
		 "e = ASN1_EXACT_CONSTRAINT; %s;\n"
		 "}\n",
		 name, length, r->min, forwstr);
}

static int
decode_type (const char *name, const Type *t, int optional,
	     const char *forwstr, const char *tmpstr, const char *dertype,
	     unsigned int depth)
{
    switch (t->type) {
    case TType: {
	if (optional)
	    fprintf(codefile,
		    "%s = calloc(1, sizeof(*%s));\n"
		    "if (%s == NULL) %s;\n",
		    name, name, name, forwstr);
	fprintf (codefile,
		 "e = decode_%s(p, len, %s, &l);\n",
		 t->symbol->gen_name, name);
	if (optional) {
	    fprintf (codefile,
		     "if(e) {\n"
		     "free(%s);\n"
		     "%s = NULL;\n"
		     "} else {\n"
		     "p += l; len -= l; ret += l;\n"
		     "}\n",
		     name, name);
	} else {
	    fprintf (codefile,
		     "if(e) %s;\n",
		     forwstr);
	    fprintf (codefile,
		     "p += l; len -= l; ret += l;\n");
	}
	break;
    }
    case TInteger:
	if(t->members) {
	    fprintf(codefile,
		    "{\n"
		    "int enumint;\n");
	    decode_primitive ("integer", "&enumint", forwstr);
	    fprintf(codefile,
		    "*%s = enumint;\n"
		    "}\n",
		    name);
	} else if (t->range == NULL) {
	    decode_primitive ("heim_integer", name, forwstr);
	} else if (t->range->min == INT_MIN && t->range->max == INT_MAX) {
	    decode_primitive ("integer", name, forwstr);
	} else if (t->range->min == 0 && t->range->max == UINT_MAX) {
	    decode_primitive ("unsigned", name, forwstr);
	} else if (t->range->min == 0 && t->range->max == INT_MAX) {
	    decode_primitive ("unsigned", name, forwstr);
	} else
	    errx(1, "%s: unsupported range %d -> %d",
		 name, t->range->min, t->range->max);
	break;
    case TBoolean:
      decode_primitive ("boolean", name, forwstr);
      break;
    case TEnumerated:
	decode_primitive ("enumerated", name, forwstr);
	break;
    case TOctetString:
	if (dertype) {
	    fprintf(codefile,
		    "if (%s == CONS) {\n",
		    dertype);
	    decode_primitive("octet_string_ber", name, forwstr);
	    fprintf(codefile,
		    "} else {\n");
	}
	decode_primitive ("octet_string", name, forwstr);
	if (dertype)
	    fprintf(codefile, "}\n");
	if (t->range)
	    range_check(name, "length", forwstr, t->range);
	break;
    case TBitString: {
	Member *m;
	int pos = 0;

	if (ASN1_TAILQ_EMPTY(t->members)) {
	    decode_primitive ("bit_string", name, forwstr);
	    break;
	}
	fprintf(codefile,
		"if (len < 1) return ASN1_OVERRUN;\n"
		"p++; len--; ret++;\n");
	fprintf(codefile,
		"do {\n"
		"if (len < 1) break;\n");
	ASN1_TAILQ_FOREACH(m, t->members, members) {
	    while (m->val / 8 > pos / 8) {
		fprintf (codefile,
			 "p++; len--; ret++;\n"
			 "if (len < 1) break;\n");
		pos += 8;
	    }
	    fprintf (codefile,
		     "(%s)->%s = (*p >> %d) & 1;\n",
		     name, m->gen_name, 7 - m->val % 8);
	}
	fprintf(codefile,
		"} while(0);\n");
	fprintf (codefile,
		 "p += len; ret += len;\n");
	break;
    }
    case TSequence: {
	Member *m;

	if (t->members == NULL)
	    break;

	ASN1_TAILQ_FOREACH(m, t->members, members) {
	    char *s = NULL;

	    if (m->ellipsis)
		continue;

	    if (asprintf (&s, "%s(%s)->%s", m->optional ? "" : "&",
			  name, m->gen_name) < 0 || s == NULL)
		errx(1, "malloc");
	    decode_type (s, m->type, m->optional, forwstr, m->gen_name, NULL,
		depth + 1);
	    free (s);
	}

	break;
    }
    case TSet: {
	Member *m;
	unsigned int memno;

	if(t->members == NULL)
	    break;

	fprintf(codefile, "{\n");
	fprintf(codefile, "unsigned int members = 0;\n");
	fprintf(codefile, "while(len > 0) {\n");
	fprintf(codefile,
		"Der_class class;\n"
		"Der_type type;\n"
		"int tag;\n"
		"e = der_get_tag (p, len, &class, &type, &tag, NULL);\n"
		"if(e) %s;\n", forwstr);
	fprintf(codefile, "switch (MAKE_TAG(class, type, tag)) {\n");
	memno = 0;
	ASN1_TAILQ_FOREACH(m, t->members, members) {
	    char *s;

	    assert(m->type->type == TTag);

	    fprintf(codefile, "case MAKE_TAG(%s, %s, %s):\n",
		    classname(m->type->tag.tagclass),
		    is_primitive_type(m->type->subtype->type) ? "PRIM" : "CONS",
		    valuename(m->type->tag.tagclass, m->type->tag.tagvalue));

	    if (asprintf (&s, "%s(%s)->%s", m->optional ? "" : "&", name, m->gen_name) < 0 || s == NULL)
		errx(1, "malloc");
	    if(m->optional)
		fprintf(codefile,
			"%s = calloc(1, sizeof(*%s));\n"
			"if (%s == NULL) { e = ENOMEM; %s; }\n",
			s, s, s, forwstr);
	    decode_type (s, m->type, 0, forwstr, m->gen_name, NULL, depth + 1);
	    free (s);

	    fprintf(codefile, "members |= (1 << %d);\n", memno);
	    memno++;
	    fprintf(codefile, "break;\n");
	}
	fprintf(codefile,
		"default:\n"
		"return ASN1_MISPLACED_FIELD;\n"
		"break;\n");
	fprintf(codefile, "}\n");
	fprintf(codefile, "}\n");
	memno = 0;
	ASN1_TAILQ_FOREACH(m, t->members, members) {
	    char *s;

	    if (asprintf (&s, "%s->%s", name, m->gen_name) < 0 || s == NULL)
		errx(1, "malloc");
	    fprintf(codefile, "if((members & (1 << %d)) == 0)\n", memno);
	    if(m->optional)
		fprintf(codefile, "%s = NULL;\n", s);
	    else if(m->defval)
		gen_assign_defval(s, m->defval);
	    else
		fprintf(codefile, "return ASN1_MISSING_FIELD;\n");
	    free(s);
	    memno++;
	}
	fprintf(codefile, "}\n");
	break;
    }
    case TSetOf:
    case TSequenceOf: {
	char *n = NULL;
	char *sname = NULL;

	fprintf (codefile,
		 "{\n"
		 "size_t %s_origlen = len;\n"
		 "size_t %s_oldret = ret;\n"
		 "size_t %s_olen = 0;\n"
		 "void *%s_tmp;\n"
		 "ret = 0;\n"
		 "(%s)->len = 0;\n"
		 "(%s)->val = NULL;\n",
		 tmpstr,
		 tmpstr,
		 tmpstr,
		 tmpstr,
		 name,
		 name);

	fprintf (codefile,
		 "while(ret < %s_origlen) {\n"
		 "size_t %s_nlen = %s_olen + sizeof(*((%s)->val));\n"
		 "if (%s_olen > %s_nlen) { e = ASN1_OVERFLOW; %s; }\n"
		 "%s_olen = %s_nlen;\n"
		 "%s_tmp = realloc((%s)->val, %s_olen);\n"
		 "if (%s_tmp == NULL) { e = ENOMEM; %s; }\n"
		 "(%s)->val = %s_tmp;\n",
		 tmpstr,
		 tmpstr, tmpstr, name,
		 tmpstr, tmpstr, forwstr,
		 tmpstr, tmpstr,
		 tmpstr, name, tmpstr,
		 tmpstr, forwstr,
		 name, tmpstr);

	if (asprintf (&n, "&(%s)->val[(%s)->len]", name, name) < 0 || n == NULL)
	    errx(1, "malloc");
	if (asprintf (&sname, "%s_s_of", tmpstr) < 0 || sname == NULL)
	    errx(1, "malloc");
	decode_type (n, t->subtype, 0, forwstr, sname, NULL, depth + 1);
	fprintf (codefile,
		 "(%s)->len++;\n"
		 "len = %s_origlen - ret;\n"
		 "}\n"
		 "ret += %s_oldret;\n"
		 "}\n",
		 name,
		 tmpstr, tmpstr);
	if (t->range)
	    range_check(name, "len", forwstr, t->range);
	free (n);
	free (sname);
	break;
    }
    case TGeneralizedTime:
	decode_primitive ("generalized_time", name, forwstr);
	break;
    case TGeneralString:
	decode_primitive ("general_string", name, forwstr);
	break;
    case TTeletexString:
	decode_primitive ("general_string", name, forwstr);
	break;
    case TTag:{
    	char *tname = NULL, *typestring = NULL;
	char *ide = NULL;

	if (asprintf(&typestring, "%s_type", tmpstr) < 0 || typestring == NULL)
	    errx(1, "malloc");

	fprintf(codefile,
		"{\n"
		"size_t %s_datalen, %s_oldlen;\n"
		"Der_type %s;\n",
		tmpstr, tmpstr, typestring);
	if(support_ber)
	    fprintf(codefile,
		    "int is_indefinite%u;\n", depth);

	fprintf(codefile, "e = der_match_tag_and_length(p, len, %s, &%s, %s, "
		"&%s_datalen, &l);\n",
		classname(t->tag.tagclass),
		typestring,
		valuename(t->tag.tagclass, t->tag.tagvalue),
		tmpstr);

	/* XXX hardcode for now */
	if (support_ber && t->subtype->type == TOctetString) {
	    ide = typestring;
	} else {
	    fprintf(codefile,
		    "if (e == 0 && %s != %s) { e = ASN1_BAD_ID; }\n",
		    typestring,
		    is_primitive_type(t->subtype->type) ? "PRIM" : "CONS");
	}

	if(optional) {
	    fprintf(codefile,
		    "if(e) {\n"
		    "%s = NULL;\n"
		    "} else {\n"
		     "%s = calloc(1, sizeof(*%s));\n"
		     "if (%s == NULL) { e = ENOMEM; %s; }\n",
		     name, name, name, name, forwstr);
	} else {
	    fprintf(codefile, "if(e) %s;\n", forwstr);
	}
	fprintf (codefile,
		 "p += l; len -= l; ret += l;\n"
		 "%s_oldlen = len;\n",
		 tmpstr);
	if(support_ber)
	    fprintf (codefile,
		     "if((is_indefinite%u = _heim_fix_dce(%s_datalen, &len)) < 0)\n"
		     "{ e = ASN1_BAD_FORMAT; %s; }\n"
		     "if (is_indefinite%u) { if (len < 2) { e = ASN1_OVERRUN; %s; } len -= 2; }",
		     depth, tmpstr, forwstr, depth, forwstr);
	else
	    fprintf(codefile,
		    "if (%s_datalen > len) { e = ASN1_OVERRUN; %s; }\n"
		    "len = %s_datalen;\n", tmpstr, forwstr, tmpstr);
	if (asprintf (&tname, "%s_Tag", tmpstr) < 0 || tname == NULL)
	    errx(1, "malloc");
	decode_type (name, t->subtype, 0, forwstr, tname, ide, depth + 1);
	if(support_ber)
	    fprintf(codefile,
		    "if(is_indefinite%u){\n"
		    "len += 2;\n"
		    "e = der_match_tag_and_length(p, len, "
		    "(Der_class)0, &%s, UT_EndOfContent, "
		    "&%s_datalen, &l);\n"
		    "if(e) %s;\n"
		    "p += l; len -= l; ret += l;\n"
		    "if (%s != (Der_type)0) { e = ASN1_BAD_ID; %s; }\n"
		    "} else \n",
		    depth,
		    typestring,
		    tmpstr,
		    forwstr,
		    typestring, forwstr);
	fprintf(codefile,
		"len = %s_oldlen - %s_datalen;\n",
		tmpstr, tmpstr);
	if(optional)
	    fprintf(codefile,
		    "}\n");
	fprintf(codefile,
		"}\n");
	free(tname);
	free(typestring);
	break;
    }
    case TChoice: {
	Member *m, *have_ellipsis = NULL;
	const char *els = "";

	if (t->members == NULL)
	    break;

	ASN1_TAILQ_FOREACH(m, t->members, members) {
	    const Type *tt = m->type;
	    char *s = NULL;
	    Der_class cl;
	    Der_type  ty;
	    unsigned  tag;

	    if (m->ellipsis) {
		have_ellipsis = m;
		continue;
	    }

	    find_tag(tt, &cl, &ty, &tag);

	    fprintf(codefile,
		    "%sif (der_match_tag(p, len, %s, %s, %s, NULL) == 0) {\n",
		    els,
		    classname(cl),
		    ty ? "CONS" : "PRIM",
		    valuename(cl, tag));
	    if (asprintf (&s, "%s(%s)->u.%s", m->optional ? "" : "&",
			  name, m->gen_name) < 0 || s == NULL)
		errx(1, "malloc");
	    decode_type (s, m->type, m->optional, forwstr, m->gen_name, NULL,
		depth + 1);
	    fprintf(codefile,
		    "(%s)->element = %s;\n",
		    name, m->label);
	    free(s);
	    fprintf(codefile,
		    "}\n");
	    els = "else ";
	}
	if (have_ellipsis) {
	    fprintf(codefile,
		    "else {\n"
		    "(%s)->u.%s.data = calloc(1, len);\n"
		    "if ((%s)->u.%s.data == NULL) {\n"
		    "e = ENOMEM; %s;\n"
		    "}\n"
		    "(%s)->u.%s.length = len;\n"
		    "memcpy((%s)->u.%s.data, p, len);\n"
		    "(%s)->element = %s;\n"
		    "p += len;\n"
		    "ret += len;\n"
		    "len = 0;\n"
		    "}\n",
		    name, have_ellipsis->gen_name,
		    name, have_ellipsis->gen_name,
		    forwstr,
		    name, have_ellipsis->gen_name,
		    name, have_ellipsis->gen_name,
		    name, have_ellipsis->label);
	} else {
	    fprintf(codefile,
		    "else {\n"
		    "e = ASN1_PARSE_ERROR;\n"
		    "%s;\n"
		    "}\n",
		    forwstr);
	}
	break;
    }
    case TUTCTime:
	decode_primitive ("utctime", name, forwstr);
	break;
    case TUTF8String:
	decode_primitive ("utf8string", name, forwstr);
	break;
    case TPrintableString:
	decode_primitive ("printable_string", name, forwstr);
	break;
    case TIA5String:
	decode_primitive ("ia5_string", name, forwstr);
	break;
    case TBMPString:
	decode_primitive ("bmp_string", name, forwstr);
	break;
    case TUniversalString:
	decode_primitive ("universal_string", name, forwstr);
	break;
    case TVisibleString:
	decode_primitive ("visible_string", name, forwstr);
	break;
    case TNull:
	fprintf (codefile, "/* NULL */\n");
	break;
    case TOID:
	decode_primitive ("oid", name, forwstr);
	break;
    default :
	abort ();
    }
    return 0;
}

void
generate_type_decode (const Symbol *s)
{
    int preserve = preserve_type(s->name) ? TRUE : FALSE;

    fprintf (codefile, "int ASN1CALL\n"
	     "decode_%s(const unsigned char *p HEIMDAL_UNUSED_ATTRIBUTE,"
	     " size_t len HEIMDAL_UNUSED_ATTRIBUTE, %s *data, size_t *size)\n"
	     "{\n",
	     s->gen_name, s->gen_name);

    switch (s->type->type) {
    case TInteger:
    case TBoolean:
    case TOctetString:
    case TOID:
    case TGeneralizedTime:
    case TGeneralString:
    case TTeletexString:
    case TUTF8String:
    case TPrintableString:
    case TIA5String:
    case TBMPString:
    case TUniversalString:
    case TVisibleString:
    case TUTCTime:
    case TNull:
    case TEnumerated:
    case TBitString:
    case TSequence:
    case TSequenceOf:
    case TSet:
    case TSetOf:
    case TTag:
    case TType:
    case TChoice:
	fprintf (codefile,
		 "size_t ret = 0;\n"
		 "size_t l HEIMDAL_UNUSED_ATTRIBUTE;\n"
		 "int e HEIMDAL_UNUSED_ATTRIBUTE;\n");
	if (preserve)
	    fprintf (codefile, "const unsigned char *begin = p;\n");

	fprintf (codefile, "\n");
	fprintf (codefile, "memset(data, 0, sizeof(*data));\n"); /* hack to avoid `unused variable' */

	decode_type ("data", s->type, 0, "goto fail", "Top", NULL, 1);
	if (preserve)
	    fprintf (codefile,
		     "data->_save.data = calloc(1, ret);\n"
		     "if (data->_save.data == NULL) { \n"
		     "e = ENOMEM; goto fail; \n"
		     "}\n"
		     "data->_save.length = ret;\n"
		     "memcpy(data->_save.data, begin, ret);\n");
	fprintf (codefile,
		 "if(size) *size = ret;\n"
		 "return 0;\n");
	fprintf (codefile,
		 "fail:\n"
		 "free_%s(data);\n"
		 "return e;\n",
		 s->gen_name);
	break;
    default:
	abort ();
    }
    fprintf (codefile, "}\n\n");
}
