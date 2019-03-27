/*
 * Copyright (c) 1997-2004 Kungliga Tekniska Högskolan
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

#include "ktutil_locl.h"

RCSID("$Id$");


static krb5_boolean
compare_keyblock(const krb5_keyblock *a, const krb5_keyblock *b)
{
    if(a->keytype != b->keytype ||
       a->keyvalue.length != b->keyvalue.length ||
       memcmp(a->keyvalue.data, b->keyvalue.data, a->keyvalue.length) != 0)
	return FALSE;
    return TRUE;
}

int
kt_copy (void *opt, int argc, char **argv)
{
    krb5_error_code ret;
    krb5_keytab src_keytab, dst_keytab;
    krb5_kt_cursor cursor;
    krb5_keytab_entry entry, dummy;
    const char *from = argv[0];
    const char *to = argv[1];

    ret = krb5_kt_resolve (context, from, &src_keytab);
    if (ret) {
	krb5_warn (context, ret, "resolving src keytab `%s'", from);
	return 1;
    }

    ret = krb5_kt_resolve (context, to, &dst_keytab);
    if (ret) {
	krb5_kt_close (context, src_keytab);
	krb5_warn (context, ret, "resolving dst keytab `%s'", to);
	return 1;
    }

    ret = krb5_kt_start_seq_get (context, src_keytab, &cursor);
    if (ret) {
	krb5_warn (context, ret, "krb5_kt_start_seq_get %s", keytab_string);
	goto out;
    }

    if (verbose_flag)
	fprintf(stderr, "copying %s to %s\n", from, to);

    while((ret = krb5_kt_next_entry(context, src_keytab,
				    &entry, &cursor)) == 0) {
	char *name_str;
	char *etype_str;
	ret = krb5_unparse_name (context, entry.principal, &name_str);
	if(ret) {
	    krb5_warn(context, ret, "krb5_unparse_name");
	    name_str = NULL; /* XXX */
	}
	ret = krb5_enctype_to_string(context, entry.keyblock.keytype, &etype_str);
	if(ret) {
	    krb5_warn(context, ret, "krb5_enctype_to_string");
	    etype_str = NULL; /* XXX */
	}
	ret = krb5_kt_get_entry(context, dst_keytab,
				entry.principal,
				entry.vno,
				entry.keyblock.keytype,
				&dummy);
	if(ret == 0) {
	    /* this entry is already in the new keytab, so no need to
               copy it; if the keyblocks are not the same, something
               is weird, so complain about that */
	    if(!compare_keyblock(&entry.keyblock, &dummy.keyblock)) {
		krb5_warnx(context, "entry with different keyvalue "
			   "already exists for %s, keytype %s, kvno %d",
			   name_str, etype_str, entry.vno);
	    }
	    krb5_kt_free_entry(context, &dummy);
	    krb5_kt_free_entry (context, &entry);
	    free(name_str);
	    free(etype_str);
	    continue;
	} else if(ret != KRB5_KT_NOTFOUND) {
	    krb5_warn (context, ret, "%s: fetching %s/%s/%u",
		       to, name_str, etype_str, entry.vno);
	    krb5_kt_free_entry (context, &entry);
	    free(name_str);
	    free(etype_str);
	    break;
	}
	if (verbose_flag)
	    fprintf (stderr, "copying %s, keytype %s, kvno %d\n", name_str,
		     etype_str, entry.vno);
	ret = krb5_kt_add_entry (context, dst_keytab, &entry);
	krb5_kt_free_entry (context, &entry);
	if (ret) {
	    krb5_warn (context, ret, "%s: adding %s/%s/%u",
		       to, name_str, etype_str, entry.vno);
	    free(name_str);
	    free(etype_str);
	    break;
	}
	free(name_str);
	free(etype_str);
    }
    krb5_kt_end_seq_get (context, src_keytab, &cursor);

  out:
    krb5_kt_close (context, src_keytab);
    krb5_kt_close (context, dst_keytab);
    return ret != 0;
}
