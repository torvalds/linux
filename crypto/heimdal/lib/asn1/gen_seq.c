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

RCSID("$Id$");

void
generate_type_seq (const Symbol *s)
{
    char *subname;
    Type *type;

    if (!seq_type(s->name))
	return;
    type = s->type;
    while(type->type == TTag)
	type = type->subtype;

    if (type->type != TSequenceOf && type->type != TSetOf) {
	fprintf(stderr, "%s not seq of %d\n", s->name, (int)type->type);
	return;
    }

    /*
     * Require the subtype to be a type so we can name it and use
     * copy_/free_
     */

    if (type->subtype->type != TType) {
	fprintf(stderr, "%s subtype is not a type, can't generate "
	       "sequence code for this case: %d\n",
		s->name, (int)type->subtype->type);
	exit(1);
    }

    subname = type->subtype->symbol->gen_name;

    fprintf (headerfile,
	     "ASN1EXP int   ASN1CALL add_%s  (%s *, const %s *);\n"
	     "ASN1EXP int   ASN1CALL remove_%s  (%s *, unsigned int);\n",
	     s->gen_name, s->gen_name, subname,
	     s->gen_name, s->gen_name);

    fprintf (codefile, "int ASN1CALL\n"
	     "add_%s(%s *data, const %s *element)\n"
	     "{\n",
	     s->gen_name, s->gen_name, subname);

    fprintf (codefile,
	     "int ret;\n"
	     "void *ptr;\n"
	     "\n"
	     "ptr = realloc(data->val, \n"
	     "\t(data->len + 1) * sizeof(data->val[0]));\n"
	     "if (ptr == NULL) return ENOMEM;\n"
	     "data->val = ptr;\n\n"
	     "ret = copy_%s(element, &data->val[data->len]);\n"
	     "if (ret) return ret;\n"
	     "data->len++;\n"
	     "return 0;\n",
	     subname);

    fprintf (codefile, "}\n\n");

    fprintf (codefile, "int ASN1CALL\n"
	     "remove_%s(%s *data, unsigned int element)\n"
	     "{\n",
	     s->gen_name, s->gen_name);

    fprintf (codefile,
	     "void *ptr;\n"
	     "\n"
	     "if (data->len == 0 || element >= data->len)\n"
	     "\treturn ASN1_OVERRUN;\n"
	     "free_%s(&data->val[element]);\n"
	     "data->len--;\n"
	     /* don't move if its the last element */
	     "if (element < data->len)\n"
	     "\tmemmove(&data->val[element], &data->val[element + 1], \n"
	     "\t\tsizeof(data->val[0]) * (data->len - element));\n"
	     /* resize but don't care about failures since it doesn't matter */
	     "ptr = realloc(data->val, data->len * sizeof(data->val[0]));\n"
	     "if (ptr != NULL || data->len == 0) data->val = ptr;\n"
	     "return 0;\n",
	     subname);

    fprintf (codefile, "}\n\n");
}
