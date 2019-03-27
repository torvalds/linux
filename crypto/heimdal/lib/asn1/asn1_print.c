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

#include "der_locl.h"
#include <com_err.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <getarg.h>
#include <err.h>
#include <der.h>

static int indent_flag = 1;
static int inner_flag = 0;

static unsigned long indefinite_form_loop;
static unsigned long indefinite_form_loop_max = 10000;

static size_t
loop (unsigned char *buf, size_t len, int indent)
{
    unsigned char *start_buf = buf;

    while (len > 0) {
	int ret;
	Der_class class;
	Der_type type;
	unsigned int tag;
	size_t sz;
	size_t length;
	size_t loop_length = 0;
	int end_tag = 0;
	const char *tagname;

	ret = der_get_tag (buf, len, &class, &type, &tag, &sz);
	if (ret)
	    errx (1, "der_get_tag: %s", error_message (ret));
	if (sz > len)
	    errx (1, "unreasonable length (%u) > %u",
		  (unsigned)sz, (unsigned)len);
	buf += sz;
	len -= sz;
	if (indent_flag) {
	    int i;
	    for (i = 0; i < indent; ++i)
		printf (" ");
	}
	printf ("%s %s ", der_get_class_name(class), der_get_type_name(type));
	tagname = der_get_tag_name(tag);
	if (class == ASN1_C_UNIV && tagname != NULL)
	    printf ("%s = ", tagname);
	else
	    printf ("tag %d = ", tag);
	ret = der_get_length (buf, len, &length, &sz);
	if (ret)
	    errx (1, "der_get_tag: %s", error_message (ret));
	if (sz > len)
	    errx (1, "unreasonable tag length (%u) > %u",
		  (unsigned)sz, (unsigned)len);
	buf += sz;
	len -= sz;
	if (length == ASN1_INDEFINITE) {
	    if ((class == ASN1_C_UNIV && type == PRIM && tag == UT_OctetString) ||
		(class == ASN1_C_CONTEXT && type == CONS) ||
		(class == ASN1_C_UNIV && type == CONS && tag == UT_Sequence) ||
		(class == ASN1_C_UNIV && type == CONS && tag == UT_Set)) {
		printf("*INDEFINITE FORM*");
	    } else {
		fflush(stdout);
		errx(1, "indef form used on unsupported object");
	    }
	    end_tag = 1;
	    if (indefinite_form_loop > indefinite_form_loop_max)
		errx(1, "indefinite form used recursively more then %lu "
		     "times, aborting", indefinite_form_loop_max);
	    indefinite_form_loop++;
	    length = len;
	} else if (length > len) {
	    printf("\n");
	    fflush(stdout);
	    errx (1, "unreasonable inner length (%u) > %u",
		  (unsigned)length, (unsigned)len);
	}
	if (class == ASN1_C_CONTEXT || class == ASN1_C_APPL) {
	    printf ("%lu bytes [%u]", (unsigned long)length, tag);
	    if (type == CONS) {
		printf("\n");
		loop_length = loop (buf, length, indent + 2);
	    } else {
		printf(" IMPLICIT content\n");
	    }
	} else if (class == ASN1_C_UNIV) {
	    switch (tag) {
	    case UT_EndOfContent:
		printf (" INDEFINITE length was %lu\n",
			(unsigned long)(buf - start_buf));
		break;
	    case UT_Set :
	    case UT_Sequence :
		printf ("%lu bytes {\n", (unsigned long)length);
		loop_length = loop (buf, length, indent + 2);
		if (indent_flag) {
		    int i;
		    for (i = 0; i < indent; ++i)
			printf (" ");
		    printf ("}\n");
		} else
		    printf ("} indent = %d\n", indent / 2);
		break;
	    case UT_Integer : {
		int val;

		if (length <= sizeof(val)) {
		    ret = der_get_integer (buf, length, &val, NULL);
		    if (ret)
			errx (1, "der_get_integer: %s", error_message (ret));
		    printf ("integer %d\n", val);
		} else {
		    heim_integer vali;
		    char *p;

		    ret = der_get_heim_integer(buf, length, &vali, NULL);
		    if (ret)
			errx (1, "der_get_heim_integer: %s",
			      error_message (ret));
		    ret = der_print_hex_heim_integer(&vali, &p);
		    if (ret)
			errx (1, "der_print_hex_heim_integer: %s",
			      error_message (ret));
		    printf ("BIG NUM integer: length %lu %s\n",
			    (unsigned long)length, p);
		    free(p);
		}
		break;
	    }
	    case UT_OctetString : {
		heim_octet_string str;
		size_t i;

		ret = der_get_octet_string (buf, length, &str, NULL);
		if (ret)
		    errx (1, "der_get_octet_string: %s", error_message (ret));
		printf ("(length %lu), ", (unsigned long)length);

		if (inner_flag) {
		    Der_class class;
		    Der_type type;
		    unsigned int tag;

		    ret = der_get_tag(str.data, str.length,
				      &class, &type, &tag, &sz);
		    if (ret || sz > str.length ||
			type != CONS || tag != UT_Sequence)
			goto just_an_octet_string;

		    printf("{\n");
		    loop (str.data, str.length, indent + 2);
		    for (i = 0; i < indent; ++i)
			printf (" ");
		    printf ("}\n");

		} else {
		    unsigned char *uc;

		just_an_octet_string:
		    uc = (unsigned char *)str.data;
		    for (i = 0; i < min(16,length); ++i)
			printf ("%02x", uc[i]);
		    printf ("\n");
		}
		free (str.data);
		break;
	    }
	    case UT_IA5String :
	    case UT_PrintableString : {
		heim_printable_string str;
		unsigned char *s;
		size_t n;

		memset(&str, 0, sizeof(str));

		ret = der_get_printable_string (buf, length, &str, NULL);
		if (ret)
		    errx (1, "der_get_general_string: %s",
			  error_message (ret));
		s = str.data;
		printf("\"");
		for (n = 0; n < str.length; n++) {
		    if (isprint((int)s[n]))
			printf ("%c", s[n]);
		    else
			printf ("#%02x", s[n]);
		}
		printf("\"\n");
		der_free_printable_string(&str);
		break;
	    }
	    case UT_GeneralizedTime :
	    case UT_GeneralString :
	    case UT_VisibleString :
	    case UT_UTF8String : {
		heim_general_string str;

		ret = der_get_general_string (buf, length, &str, NULL);
		if (ret)
		    errx (1, "der_get_general_string: %s",
			  error_message (ret));
		printf ("\"%s\"\n", str);
		free (str);
		break;
	    }
	    case UT_OID: {
		heim_oid o;
		char *p;

		ret = der_get_oid(buf, length, &o, NULL);
		if (ret)
		    errx (1, "der_get_oid: %s", error_message (ret));
		ret = der_print_heim_oid(&o, '.', &p);
		der_free_oid(&o);
		if (ret)
		    errx (1, "der_print_heim_oid: %s", error_message (ret));
		printf("%s\n", p);
		free(p);

		break;
	    }
	    case UT_Enumerated: {
		int num;

		ret = der_get_integer (buf, length, &num, NULL);
		if (ret)
		    errx (1, "der_get_enum: %s", error_message (ret));

		printf("%u\n", num);
		break;
	    }
	    default :
		printf ("%lu bytes\n", (unsigned long)length);
		break;
	    }
	}
	if (end_tag) {
	    if (loop_length == 0)
		errx(1, "zero length INDEFINITE data ? indent = %d\n",
		     indent / 2);
	    if (loop_length < length)
		length = loop_length;
	    if (indefinite_form_loop == 0)
		errx(1, "internal error in indefinite form loop detection");
	    indefinite_form_loop--;
	} else if (loop_length)
	    errx(1, "internal error for INDEFINITE form");
	buf += length;
	len -= length;
    }
    return 0;
}

static int
doit (const char *filename)
{
    int fd = open (filename, O_RDONLY);
    struct stat sb;
    unsigned char *buf;
    size_t len;
    int ret;

    if(fd < 0)
	err (1, "opening %s for read", filename);
    if (fstat (fd, &sb) < 0)
	err (1, "stat %s", filename);
    len = sb.st_size;
    buf = emalloc (len);
    if (read (fd, buf, len) != len)
	errx (1, "read failed");
    close (fd);
    ret = loop (buf, len, 0);
    free (buf);
    return ret;
}


static int version_flag;
static int help_flag;
struct getargs args[] = {
    { "indent", 0, arg_negative_flag, &indent_flag },
    { "inner", 0, arg_flag, &inner_flag, "try to parse inner structures of OCTET STRING" },
    { "version", 0, arg_flag, &version_flag },
    { "help", 0, arg_flag, &help_flag }
};
int num_args = sizeof(args) / sizeof(args[0]);

static void
usage(int code)
{
    arg_printusage(args, num_args, NULL, "dump-file");
    exit(code);
}

int
main(int argc, char **argv)
{
    int optidx = 0;

    setprogname (argv[0]);
    initialize_asn1_error_table ();
    if(getarg(args, num_args, argc, argv, &optidx))
	usage(1);
    if(help_flag)
	usage(0);
    if(version_flag) {
	print_version(NULL);
	exit(0);
    }
    argv += optidx;
    argc -= optidx;
    if (argc != 1)
	usage (1);
    return doit (argv[0]);
}
