/*
 * Copyright (c) 1997 - 2005 Kungliga Tekniska Högskolan
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

#include "gen_locl.h"

static const char *symbol_name(const char *, const Type *);
static void generate_template_type(const char *, const char **, const char *, const char *, const char *,
				   Type *, int, int, int);

static const char *
ttype_symbol(const char *basename, const Type *t)
{
    return t->symbol->gen_name;
}

static const char *
integer_symbol(const char *basename, const Type *t)
{
    if (t->members)
	return "int"; /* XXX enum foo */
    else if (t->range == NULL)
	return "heim_integer";
    else if (t->range->min == INT_MIN && t->range->max == INT_MAX)
	return "int";
    else if (t->range->min == 0 && t->range->max == UINT_MAX)
	return "unsigned";
    else if (t->range->min == 0 && t->range->max == INT_MAX)
	return "unsigned";
    else {
	abort();
        UNREACHABLE(return NULL);
    }
}

static const char *
boolean_symbol(const char *basename, const Type *t)
{
    return "int";
}


static const char *
octetstring_symbol(const char *basename, const Type *t)
{
    return "heim_octet_string";
}

static const char *
sequence_symbol(const char *basename, const Type *t)
{
    return basename;
}

static const char *
time_symbol(const char *basename, const Type *t)
{
    return "time_t";
}

static const char *
tag_symbol(const char *basename, const Type *t)
{
    return symbol_name(basename, t->subtype);
}

static const char *
generalstring_symbol(const char *basename, const Type *t)
{
    return "heim_general_string";
}

static const char *
printablestring_symbol(const char *basename, const Type *t)
{
    return "heim_printable_string";
}

static const char *
ia5string_symbol(const char *basename, const Type *t)
{
    return "heim_ia5_string";
}

static const char *
visiblestring_symbol(const char *basename, const Type *t)
{
    return "heim_visible_string";
}

static const char *
utf8string_symbol(const char *basename, const Type *t)
{
    return "heim_utf8_string";
}

static const char *
bmpstring_symbol(const char *basename, const Type *t)
{
    return "heim_bmp_string";
}

static const char *
universalstring_symbol(const char *basename, const Type *t)
{
    return "heim_universal_string";
}

static const char *
oid_symbol(const char *basename, const Type *t)
{
    return "heim_oid";
}

static const char *
bitstring_symbol(const char *basename, const Type *t)
{
    if (t->members)
	return basename;
    return "heim_bit_string";
}



struct {
    enum typetype type;
    const char *(*symbol_name)(const char *, const Type *);
    int is_struct;
} types[] =  {
    { TBMPString, bmpstring_symbol, 0 },
    { TBitString, bitstring_symbol, 0 },
    { TBoolean, boolean_symbol, 0 },
    { TGeneralString, generalstring_symbol, 0 },
    { TGeneralizedTime, time_symbol, 0 },
    { TIA5String, ia5string_symbol, 0 },
    { TInteger, integer_symbol, 0 },
    { TOID, oid_symbol, 0 },
    { TOctetString, octetstring_symbol, 0 },
    { TPrintableString, printablestring_symbol, 0 },
    { TSequence, sequence_symbol, 1 },
    { TSequenceOf, tag_symbol, 1 },
    { TSetOf, tag_symbol, 1 },
    { TTag, tag_symbol, 1 },
    { TType, ttype_symbol, 1 },
    { TUTCTime, time_symbol, 0 },
    { TUniversalString, universalstring_symbol, 0 },
    { TVisibleString,  visiblestring_symbol, 0 },
    { TUTF8String, utf8string_symbol, 0 },
    { TChoice, sequence_symbol, 1 },
    { TNull, integer_symbol, 1 }
};

static FILE *
get_code_file(void)
{
    if (!one_code_file)
	return templatefile;
    return codefile;
}


static int
is_supported_type_p(const Type *t)
{
    size_t i;

    for (i = 0; i < sizeof(types)/sizeof(types[0]); i++)
	if (t->type == types[i].type)
	    return 1;
    return 0;
}

int
is_template_compat (const Symbol *s)
{
    return is_supported_type_p(s->type);
}

static const char *
symbol_name(const char *basename, const Type *t)
{
    size_t i;

    for (i = 0; i < sizeof(types)/sizeof(types[0]); i++)
	if (t->type == types[i].type)
	    return (types[i].symbol_name)(basename, t);
    printf("unknown der type: %d\n", t->type);
    exit(1);
}


static char *
partial_offset(const char *basetype, const char *name, int need_offset)
{
    char *str;
    if (name == NULL || need_offset == 0)
	return strdup("0");
    if (asprintf(&str, "offsetof(struct %s, %s)", basetype, name) < 0 || str == NULL)
	errx(1, "malloc");
    return str;
}

struct template {
    char *line;
    char *tt;
    char *offset;
    char *ptr;
    ASN1_TAILQ_ENTRY(template) members;
};

ASN1_TAILQ_HEAD(templatehead, template);

struct tlist {
    char *name;
    char *header;
    struct templatehead template;
    ASN1_TAILQ_ENTRY(tlist) tmembers;
};

ASN1_TAILQ_HEAD(tlisthead, tlist);

static void tlist_header(struct tlist *, const char *, ...) __attribute__((__format__(__printf__, 2, 3)));
static struct template *
    add_line(struct templatehead *, const char *, ...) __attribute__((__format__(__printf__, 2, 3)));
static int tlist_cmp(const struct tlist *, const struct tlist *);

static void add_line_pointer(struct templatehead *, const char *, const char *, const char *, ...)
    __attribute__((__format__(__printf__, 4, 5)));


static struct tlisthead tlistmaster = ASN1_TAILQ_HEAD_INITIALIZER(tlistmaster);
static unsigned long numdups = 0;

static struct tlist *
tlist_new(const char *name)
{
    struct tlist *tl = calloc(1, sizeof(*tl));
    tl->name = strdup(name);
    ASN1_TAILQ_INIT(&tl->template);
    return tl;
}

static void
tlist_header(struct tlist *t, const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    if (vasprintf(&t->header, fmt, ap) < 0 || t->header == NULL)
	errx(1, "malloc");
    va_end(ap);
}

static unsigned long
tlist_count(struct tlist *tl)
{
    unsigned int count = 0;
    struct template *q;

    ASN1_TAILQ_FOREACH(q, &tl->template, members) {
	count++;
    }
    return count;
}

static void
tlist_add(struct tlist *tl)
{
    ASN1_TAILQ_INSERT_TAIL(&tlistmaster, tl, tmembers);
}

static void
tlist_print(struct tlist *tl)
{
    struct template *q;
    unsigned int i = 1;
    FILE *f = get_code_file();

    fprintf(f, "static const struct asn1_template asn1_%s[] = {\n", tl->name);
    fprintf(f, "/* 0 */ %s,\n", tl->header);
    ASN1_TAILQ_FOREACH(q, &tl->template, members) {
	int last = (ASN1_TAILQ_LAST(&tl->template, templatehead) == q);
	fprintf(f, "/* %lu */ %s%s\n", (unsigned long)i++, q->line, last ? "" : ",");
    }
    fprintf(f, "};\n");
}

static struct tlist *
tlist_find_by_name(const char *name)
{
    struct tlist *ql;
    ASN1_TAILQ_FOREACH(ql, &tlistmaster, tmembers) {
	if (strcmp(ql->name, name) == 0)
	    return ql;
    }
    return NULL;
}

static int
tlist_cmp_name(const char *tname, const char *qname)
{
    struct tlist *tl = tlist_find_by_name(tname);
    struct tlist *ql = tlist_find_by_name(qname);
    return tlist_cmp(tl, ql);
}

static int
tlist_cmp(const struct tlist *tl, const struct tlist *ql)
{
    int ret;
    struct template *t, *q;

    ret = strcmp(tl->header, ql->header);
    if (ret) return ret;

    q = ASN1_TAILQ_FIRST(&ql->template);
    ASN1_TAILQ_FOREACH(t, &tl->template, members) {
	if (q == NULL) return 1;

	if (t->ptr == NULL || q->ptr == NULL) {
	    ret = strcmp(t->line, q->line);
	    if (ret) return ret;
	} else {
	    ret = strcmp(t->tt, q->tt);
	    if (ret) return ret;

	    ret = strcmp(t->offset, q->offset);
	    if (ret) return ret;

	    if ((ret = strcmp(t->ptr, q->ptr)) != 0 ||
		(ret = tlist_cmp_name(t->ptr, q->ptr)) != 0)
		return ret;
	}
	q = ASN1_TAILQ_NEXT(q, members);
    }
    if (q != NULL) return -1;
    return 0;
}


static const char *
tlist_find_dup(const struct tlist *tl)
{
    struct tlist *ql;

    ASN1_TAILQ_FOREACH(ql, &tlistmaster, tmembers) {
	if (tlist_cmp(ql, tl) == 0) {
	    numdups++;
	    return ql->name;
	}
    }
    return NULL;
}


/*
 *
 */

static struct template *
add_line(struct templatehead *t, const char *fmt, ...)
{
    struct template *q = calloc(1, sizeof(*q));
    va_list ap;
    va_start(ap, fmt);
    if (vasprintf(&q->line, fmt, ap) < 0 || q->line == NULL)
	errx(1, "malloc");
    va_end(ap);
    ASN1_TAILQ_INSERT_TAIL(t, q, members);
    return q;
}

static void
add_line_pointer(struct templatehead *t,
		 const char *ptr,
		 const char *offset,
		 const char *ttfmt,
		 ...)
{
    struct template *q;
    va_list ap;
    char *tt = NULL;

    va_start(ap, ttfmt);
    if (vasprintf(&tt, ttfmt, ap) < 0 || tt == NULL)
	errx(1, "malloc");
    va_end(ap);

    q = add_line(t, "{ %s, %s, asn1_%s }", tt, offset, ptr);
    q->tt = tt;
    q->offset = strdup(offset);
    q->ptr = strdup(ptr);
}

static int
use_extern(const Symbol *s)
{
    if (s->type == NULL)
	return 1;
    return 0;
}

static int
is_struct(Type *t, int isstruct)
{
    size_t i;

    if (t->type == TType)
	return 0;
    if (t->type == TSequence || t->type == TSet || t->type == TChoice)
	return 1;
    if (t->type == TTag)
	return is_struct(t->subtype, isstruct);

    for (i = 0; i < sizeof(types)/sizeof(types[0]); i++) {
	if (t->type == types[i].type) {
	    if (types[i].is_struct == 0)
		return 0;
	    else
		break;
	}
    }

    return isstruct;
}

static const Type *
compact_tag(const Type *t)
{
    while (t->type == TTag)
	t = t->subtype;
    return t;
}

static void
template_members(struct templatehead *temp, const char *basetype, const char *name, const Type *t, int optional, int isstruct, int need_offset)
{
    char *poffset = NULL;

    if (optional && t->type != TTag && t->type != TType)
	errx(1, "%s...%s is optional and not a (TTag or TType)", basetype, name);

    poffset = partial_offset(basetype, name, need_offset);

    switch (t->type) {
    case TType:
	if (use_extern(t->symbol)) {
	    add_line(temp, "{ A1_OP_TYPE_EXTERN %s, %s, &asn1_extern_%s}",
		     optional ? "|A1_FLAG_OPTIONAL" : "",
		     poffset, t->symbol->gen_name);
	} else {
	    add_line_pointer(temp, t->symbol->gen_name, poffset,
			     "A1_OP_TYPE %s", optional ? "|A1_FLAG_OPTIONAL" : "");
	}
	break;
    case TInteger: {
	char *itype = NULL;

	if (t->members)
	    itype = "IMEMBER";
	else if (t->range == NULL)
	    itype = "HEIM_INTEGER";
	else if (t->range->min == INT_MIN && t->range->max == INT_MAX)
	    itype = "INTEGER";
	else if (t->range->min == 0 && t->range->max == UINT_MAX)
	    itype = "UNSIGNED";
	else if (t->range->min == 0 && t->range->max == INT_MAX)
	    itype = "UNSIGNED";
	else
	    errx(1, "%s: unsupported range %d -> %d",
		 name, t->range->min, t->range->max);

	add_line(temp, "{ A1_PARSE_T(A1T_%s), %s, NULL }", itype, poffset);
	break;
    }
    case TGeneralString:
	add_line(temp, "{ A1_PARSE_T(A1T_GENERAL_STRING), %s, NULL }", poffset);
	break;
    case TTeletexString:
	add_line(temp, "{ A1_PARSE_T(A1T_TELETEX_STRING), %s, NULL }", poffset);
	break;
    case TPrintableString:
	add_line(temp, "{ A1_PARSE_T(A1T_PRINTABLE_STRING), %s, NULL }", poffset);
	break;
    case TOctetString:
	add_line(temp, "{ A1_PARSE_T(A1T_OCTET_STRING), %s, NULL }", poffset);
	break;
    case TIA5String:
	add_line(temp, "{ A1_PARSE_T(A1T_IA5_STRING), %s, NULL }", poffset);
	break;
    case TBMPString:
	add_line(temp, "{ A1_PARSE_T(A1T_BMP_STRING), %s, NULL }", poffset);
	break;
    case TUniversalString:
	add_line(temp, "{ A1_PARSE_T(A1T_UNIVERSAL_STRING), %s, NULL }", poffset);
	break;
    case TVisibleString:
	add_line(temp, "{ A1_PARSE_T(A1T_VISIBLE_STRING), %s, NULL }", poffset);
	break;
    case TUTF8String:
	add_line(temp, "{ A1_PARSE_T(A1T_UTF8_STRING), %s, NULL }", poffset);
	break;
    case TGeneralizedTime:
	add_line(temp, "{ A1_PARSE_T(A1T_GENERALIZED_TIME), %s, NULL }", poffset);
	break;
    case TUTCTime:
	add_line(temp, "{ A1_PARSE_T(A1T_UTC_TIME), %s, NULL }", poffset);
	break;
    case TBoolean:
	add_line(temp, "{ A1_PARSE_T(A1T_BOOLEAN), %s, NULL }", poffset);
	break;
    case TOID:
	add_line(temp, "{ A1_PARSE_T(A1T_OID), %s, NULL }", poffset);
	break;
    case TNull:
	break;
    case TBitString: {
	struct templatehead template = ASN1_TAILQ_HEAD_INITIALIZER(template);
	struct template *q;
	Member *m;
	size_t count = 0, i;
	char *bname = NULL;
	FILE *f = get_code_file();

	if (ASN1_TAILQ_EMPTY(t->members)) {
	    add_line(temp, "{ A1_PARSE_T(A1T_HEIM_BIT_STRING), %s, NULL }", poffset);
	    break;
	}

	if (asprintf(&bname, "bmember_%s_%p", name ? name : "", t) < 0 || bname == NULL)
	    errx(1, "malloc");
	output_name(bname);

	ASN1_TAILQ_FOREACH(m, t->members, members) {
	    add_line(&template, "{ 0, %d, 0 } /* %s */", m->val, m->gen_name);
	}

	ASN1_TAILQ_FOREACH(q, &template, members) {
	    count++;
	}

	fprintf(f, "static const struct asn1_template asn1_%s_%s[] = {\n", basetype, bname);
	fprintf(f, "/* 0 */ { 0%s, sizeof(%s), ((void *)%lu) },\n",
		rfc1510_bitstring ? "|A1_HBF_RFC1510" : "",
		basetype, (unsigned long)count);
	i = 1;
	ASN1_TAILQ_FOREACH(q, &template, members) {
	    int last = (ASN1_TAILQ_LAST(&template, templatehead) == q);
	    fprintf(f, "/* %lu */ %s%s\n", (unsigned long)i++, q->line, last ? "" : ",");
	}
	fprintf(f, "};\n");

	add_line(temp, "{ A1_OP_BMEMBER, %s, asn1_%s_%s }", poffset, basetype, bname);

	free(bname);

	break;
    }
    case TSequence: {
	Member *m;

	ASN1_TAILQ_FOREACH(m, t->members, members) {
	    char *newbasename = NULL;

	    if (m->ellipsis)
		continue;

	    if (name) {
		if (asprintf(&newbasename, "%s_%s", basetype, name) < 0)
		    errx(1, "malloc");
	    } else
		newbasename = strdup(basetype);
	    if (newbasename == NULL)
		errx(1, "malloc");

	    template_members(temp, newbasename, m->gen_name, m->type, m->optional, isstruct, 1);

	    free(newbasename);
	}

	break;
    }
    case TTag: {
	char *tname = NULL, *elname = NULL;
	const char *sename, *dupname;
	int subtype_is_struct = is_struct(t->subtype, isstruct);

	if (subtype_is_struct)
	    sename = basetype;
	else
	    sename = symbol_name(basetype, t->subtype);

	if (asprintf(&tname, "tag_%s_%p", name ? name : "", t) < 0 || tname == NULL)
	    errx(1, "malloc");
	output_name(tname);

	if (asprintf(&elname, "%s_%s", basetype, tname) < 0 || elname == NULL)
	    errx(1, "malloc");

	generate_template_type(elname, &dupname, NULL, sename, name,
			       t->subtype, 0, subtype_is_struct, 0);

	add_line_pointer(temp, dupname, poffset,
			 "A1_TAG_T(%s,%s,%s)%s",
			 classname(t->tag.tagclass),
			 is_primitive_type(t->subtype->type)  ? "PRIM" : "CONS",
			 valuename(t->tag.tagclass, t->tag.tagvalue),
			 optional ? "|A1_FLAG_OPTIONAL" : "");

	free(tname);
	free(elname);

	break;
    }
    case TSetOf:
    case TSequenceOf: {
	const char *type = NULL, *tname, *dupname;
	char *sename = NULL, *elname = NULL;
	int subtype_is_struct = is_struct(t->subtype, 0);

	if (name && subtype_is_struct) {
	    tname = "seofTstruct";
	    if (asprintf(&sename, "%s_%s_val", basetype, name) < 0)
		errx(1, "malloc");
	} else if (subtype_is_struct) {
	    tname = "seofTstruct";
	    if (asprintf(&sename, "%s_val", symbol_name(basetype, t->subtype)) < 0)
		errx(1, "malloc");
	} else {
	    if (name)
		tname = name;
	    else
		tname = "seofTstruct";
	    sename = strdup(symbol_name(basetype, t->subtype));
	}
	if (sename == NULL)
	    errx(1, "malloc");

	if (t->type == TSetOf) type = "A1_OP_SETOF";
	else if (t->type == TSequenceOf) type = "A1_OP_SEQOF";
	else abort();

	if (asprintf(&elname, "%s_%s_%p", basetype, tname, t) < 0 || elname == NULL)
	    errx(1, "malloc");

	generate_template_type(elname, &dupname, NULL, sename, NULL, t->subtype,
			       0, subtype_is_struct, need_offset);

	add_line(temp, "{ %s, %s, asn1_%s }", type, poffset, dupname);
	free(sename);
	break;
    }
    case TChoice: {
	struct templatehead template = ASN1_TAILQ_HEAD_INITIALIZER(template);
	struct template *q;
	size_t count = 0, i;
	char *tname = NULL;
	FILE *f = get_code_file();
	Member *m;
	int ellipsis = 0;
	char *e;

	if (asprintf(&tname, "asn1_choice_%s_%s%x",
		     basetype, name ? name : "", (unsigned int)(uintptr_t)t) < 0 || tname == NULL)
	    errx(1, "malloc");

	ASN1_TAILQ_FOREACH(m, t->members, members) {
	    const char *dupname;
	    char *elname = NULL;
	    char *newbasename = NULL;
	    int subtype_is_struct;

	    if (m->ellipsis) {
		ellipsis = 1;
		continue;
	    }

	    subtype_is_struct = is_struct(m->type, 0);

	    if (asprintf(&elname, "%s_choice_%s", basetype, m->gen_name) < 0 || elname == NULL)
		errx(1, "malloc");

	    if (subtype_is_struct) {
		if (asprintf(&newbasename, "%s_%s", basetype, m->gen_name) < 0)
		    errx(1, "malloc");
	    } else
		newbasename = strdup(basetype);

	    if (newbasename == NULL)
		errx(1, "malloc");


	    generate_template_type(elname, &dupname, NULL,
				   symbol_name(newbasename, m->type),
				   NULL, m->type, 0, subtype_is_struct, 1);

	    add_line(&template, "{ %s, offsetof(%s%s, u.%s), asn1_%s }",
		     m->label, isstruct ? "struct " : "",
		     basetype, m->gen_name,
		     dupname);

	    free(elname);
	    free(newbasename);
	}

	e = NULL;
	if (ellipsis) {
	    if (asprintf(&e, "offsetof(%s%s, u.asn1_ellipsis)", isstruct ? "struct " : "", basetype) < 0 || e == NULL)
		errx(1, "malloc");
	}

	ASN1_TAILQ_FOREACH(q, &template, members) {
	    count++;
	}

	fprintf(f, "static const struct asn1_template %s[] = {\n", tname);
	fprintf(f, "/* 0 */ { %s, offsetof(%s%s, element), ((void *)%lu) },\n",
		e ? e : "0", isstruct ? "struct " : "", basetype, (unsigned long)count);
	i = 1;
	ASN1_TAILQ_FOREACH(q, &template, members) {
	    int last = (ASN1_TAILQ_LAST(&template, templatehead) == q);
	    fprintf(f, "/* %lu */ %s%s\n", (unsigned long)i++, q->line, last ? "" : ",");
	}
	fprintf(f, "};\n");

	add_line(temp, "{ A1_OP_CHOICE, %s, %s }", poffset, tname);

	free(e);
	free(tname);
	break;
    }
    default:
	abort ();
    }
    if (poffset)
	free(poffset);
}

static void
gen_extern_stubs(FILE *f, const char *name)
{
    fprintf(f,
	    "static const struct asn1_type_func asn1_extern_%s = {\n"
	    "\t(asn1_type_encode)encode_%s,\n"
	    "\t(asn1_type_decode)decode_%s,\n"
	    "\t(asn1_type_length)length_%s,\n"
	    "\t(asn1_type_copy)copy_%s,\n"
	    "\t(asn1_type_release)free_%s,\n"
	    "\tsizeof(%s)\n"
	    "};\n",
	    name, name, name, name,
	    name, name, name);
}

void
gen_template_import(const Symbol *s)
{
    FILE *f = get_code_file();

    if (template_flag == 0)
	return;

    gen_extern_stubs(f, s->gen_name);
}

static void
generate_template_type(const char *varname,
		       const char **dupname,
		       const char *symname,
		       const char *basetype,
		       const char *name,
		       Type *type,
		       int optional, int isstruct, int need_offset)
{
    struct tlist *tl;
    const char *dup;
    int have_ellipsis = 0;

    tl = tlist_new(varname);

    template_members(&tl->template, basetype, name, type, optional, isstruct, need_offset);

    /* if its a sequence or set type, check if there is a ellipsis */
    if (type->type == TSequence || type->type == TSet) {
	Member *m;
	ASN1_TAILQ_FOREACH(m, type->members, members) {
	    if (m->ellipsis)
		have_ellipsis = 1;
	}
    }

    if (ASN1_TAILQ_EMPTY(&tl->template) && compact_tag(type)->type != TNull)
	errx(1, "Tag %s...%s with no content ?", basetype, name ? name : "");

    tlist_header(tl, "{ 0%s%s, sizeof(%s%s), ((void *)%lu) }",
		 (symname && preserve_type(symname)) ? "|A1_HF_PRESERVE" : "",
		 have_ellipsis ? "|A1_HF_ELLIPSIS" : "",
		 isstruct ? "struct " : "", basetype, tlist_count(tl));

    dup = tlist_find_dup(tl);
    if (dup) {
	if (strcmp(dup, tl->name) == 0)
	    errx(1, "found dup of ourself");
	*dupname = dup;
    } else {
	*dupname = tl->name;
	tlist_print(tl);
	tlist_add(tl);
    }
}


void
generate_template(const Symbol *s)
{
    FILE *f = get_code_file();
    const char *dupname;

    if (use_extern(s)) {
	gen_extern_stubs(f, s->gen_name);
	return;
    }

    generate_template_type(s->gen_name, &dupname, s->name, s->gen_name, NULL, s->type, 0, 0, 1);

    fprintf(f,
	    "\n"
	    "int\n"
	    "decode_%s(const unsigned char *p, size_t len, %s *data, size_t *size)\n"
	    "{\n"
	    "    return _asn1_decode_top(asn1_%s, 0|%s, p, len, data, size);\n"
	    "}\n"
	    "\n",
	    s->gen_name,
	    s->gen_name,
	    dupname,
	    support_ber ? "A1_PF_ALLOW_BER" : "0");

    fprintf(f,
	    "\n"
	    "int\n"
	    "encode_%s(unsigned char *p, size_t len, const %s *data, size_t *size)\n"
	    "{\n"
	    "    return _asn1_encode(asn1_%s, p, len, data, size);\n"
	    "}\n"
	    "\n",
	    s->gen_name,
	    s->gen_name,
	    dupname);

    fprintf(f,
	    "\n"
	    "size_t\n"
	    "length_%s(const %s *data)\n"
	    "{\n"
	    "    return _asn1_length(asn1_%s, data);\n"
	    "}\n"
	    "\n",
	    s->gen_name,
	    s->gen_name,
	    dupname);


    fprintf(f,
	    "\n"
	    "void\n"
	    "free_%s(%s *data)\n"
	    "{\n"
	    "    _asn1_free(asn1_%s, data);\n"
	    "}\n"
	    "\n",
	    s->gen_name,
	    s->gen_name,
	    dupname);

    fprintf(f,
	    "\n"
	    "int\n"
	    "copy_%s(const %s *from, %s *to)\n"
	    "{\n"
	    "    return _asn1_copy_top(asn1_%s, from, to);\n"
	    "}\n"
	    "\n",
	    s->gen_name,
	    s->gen_name,
	    s->gen_name,
	    dupname);
}
