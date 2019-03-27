/*
 * Copyright (c) 1997, 1999, 2000, 2003 - 2005 Kungliga Tekniska Högskolan
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

RCSID("$Id$");

static void
generate_2int (const Type *t, const char *gen_name)
{
    Member *m;

    fprintf (headerfile,
	     "unsigned %s2int(%s);\n",
	     gen_name, gen_name);

    fprintf (codefile,
	     "unsigned %s2int(%s f)\n"
	     "{\n"
	     "unsigned r = 0;\n",
	     gen_name, gen_name);

    ASN1_TAILQ_FOREACH(m, t->members, members) {
	fprintf (codefile, "if(f.%s) r |= (1U << %d);\n",
		 m->gen_name, m->val);
    }
    fprintf (codefile, "return r;\n"
	     "}\n\n");
}

static void
generate_int2 (const Type *t, const char *gen_name)
{
    Member *m;

    fprintf (headerfile,
	     "%s int2%s(unsigned);\n",
	     gen_name, gen_name);

    fprintf (codefile,
	     "%s int2%s(unsigned n)\n"
	     "{\n"
	     "\t%s flags;\n\n"
	     "\tmemset(&flags, 0, sizeof(flags));\n\n",
	     gen_name, gen_name, gen_name);

    if(t->members) {
	ASN1_TAILQ_FOREACH(m, t->members, members) {
	    fprintf (codefile, "\tflags.%s = (n >> %d) & 1;\n",
		     m->gen_name, m->val);
	}
    }
    fprintf (codefile, "\treturn flags;\n"
	     "}\n\n");
}

/*
 * This depends on the bit string being declared in increasing order
 */

static void
generate_units (const Type *t, const char *gen_name)
{
    Member *m;

    if (template_flag) {
	fprintf (headerfile,
		 "extern const struct units *asn1_%s_table_units;\n",
		 gen_name);
	fprintf (headerfile, "#define asn1_%s_units() (asn1_%s_table_units)\n",
		 gen_name, gen_name);
    } else {
	fprintf (headerfile,
		 "const struct units * asn1_%s_units(void);\n",
		 gen_name);
    }

    fprintf (codefile,
	     "static struct units %s_units[] = {\n",
	     gen_name);

    if(t->members) {
	ASN1_TAILQ_FOREACH_REVERSE(m, t->members, memhead, members) {
	    fprintf (codefile,
		     "\t{\"%s\",\t1U << %d},\n", m->name, m->val);
	}
    }

    fprintf (codefile,
	     "\t{NULL,\t0}\n"
	     "};\n\n");

    if (template_flag)
	fprintf (codefile,
		 "const struct units * asn1_%s_table_units = %s_units;\n",
		 gen_name, gen_name);
    else
	fprintf (codefile,
		 "const struct units * asn1_%s_units(void){\n"
		 "return %s_units;\n"
		 "}\n\n",
		 gen_name, gen_name);


}

void
generate_glue (const Type *t, const char *gen_name)
{
    switch(t->type) {
    case TTag:
	generate_glue(t->subtype, gen_name);
	break;
    case TBitString :
	if (!ASN1_TAILQ_EMPTY(t->members)) {
	    generate_2int (t, gen_name);
	    generate_int2 (t, gen_name);
	    generate_units (t, gen_name);
	}
	break;
    default :
	break;
    }
}
