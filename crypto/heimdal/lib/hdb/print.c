/*
 * Copyright (c) 1999-2005 Kungliga Tekniska Högskolan
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
 * 3. Neither the name of KTH nor the names of its contributors may be
 *    used to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY KTH AND ITS CONTRIBUTORS ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL KTH OR ITS CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE. */

#include "hdb_locl.h"
#include <hex.h>
#include <ctype.h>

/*
   This is the present contents of a dump line. This might change at
   any time. Fields are separated by white space.

  principal
  keyblock
  	kvno
	keys...
		mkvno
		enctype
		keyvalue
		salt (- means use normal salt)
  creation date and principal
  modification date and principal
  principal valid from date (not used)
  principal valid end date (not used)
  principal key expires (not used)
  max ticket life
  max renewable life
  flags
  generation number
  */

static krb5_error_code
append_string(krb5_context context, krb5_storage *sp, const char *fmt, ...)
{
    krb5_error_code ret;
    char *s;
    va_list ap;
    va_start(ap, fmt);
    vasprintf(&s, fmt, ap);
    va_end(ap);
    if(s == NULL) {
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }
    ret = krb5_storage_write(sp, s, strlen(s));
    free(s);
    return ret;
}

static krb5_error_code
append_hex(krb5_context context, krb5_storage *sp, krb5_data *data)
{
    int printable = 1;
    size_t i;
    char *p;

    p = data->data;
    for(i = 0; i < data->length; i++)
	if(!isalnum((unsigned char)p[i]) && p[i] != '.'){
	    printable = 0;
	    break;
	}
    if(printable)
	return append_string(context, sp, "\"%.*s\"",
			     data->length, data->data);
    hex_encode(data->data, data->length, &p);
    append_string(context, sp, "%s", p);
    free(p);
    return 0;
}

static char *
time2str(time_t t)
{
    static char buf[128];
    strftime(buf, sizeof(buf), "%Y%m%d%H%M%S", gmtime(&t));
    return buf;
}

static krb5_error_code
append_event(krb5_context context, krb5_storage *sp, Event *ev)
{
    char *pr = NULL;
    krb5_error_code ret;
    if(ev == NULL)
	return append_string(context, sp, "- ");
    if (ev->principal != NULL) {
       ret = krb5_unparse_name(context, ev->principal, &pr);
       if(ret)
           return ret;
    }
    ret = append_string(context, sp, "%s:%s ",
			time2str(ev->time), pr ? pr : "UNKNOWN");
    free(pr);
    return ret;
}

static krb5_error_code
entry2string_int (krb5_context context, krb5_storage *sp, hdb_entry *ent)
{
    char *p;
    size_t i;
    krb5_error_code ret;

    /* --- principal */
    ret = krb5_unparse_name(context, ent->principal, &p);
    if(ret)
	return ret;
    append_string(context, sp, "%s ", p);
    free(p);
    /* --- kvno */
    append_string(context, sp, "%d", ent->kvno);
    /* --- keys */
    for(i = 0; i < ent->keys.len; i++){
	/* --- mkvno, keytype */
	if(ent->keys.val[i].mkvno)
	    append_string(context, sp, ":%d:%d:",
			  *ent->keys.val[i].mkvno,
			  ent->keys.val[i].key.keytype);
	else
	    append_string(context, sp, "::%d:",
			  ent->keys.val[i].key.keytype);
	/* --- keydata */
	append_hex(context, sp, &ent->keys.val[i].key.keyvalue);
	append_string(context, sp, ":");
	/* --- salt */
	if(ent->keys.val[i].salt){
	    append_string(context, sp, "%u/", ent->keys.val[i].salt->type);
	    append_hex(context, sp, &ent->keys.val[i].salt->salt);
	}else
	    append_string(context, sp, "-");
    }
    append_string(context, sp, " ");
    /* --- created by */
    append_event(context, sp, &ent->created_by);
    /* --- modified by */
    append_event(context, sp, ent->modified_by);

    /* --- valid start */
    if(ent->valid_start)
	append_string(context, sp, "%s ", time2str(*ent->valid_start));
    else
	append_string(context, sp, "- ");

    /* --- valid end */
    if(ent->valid_end)
	append_string(context, sp, "%s ", time2str(*ent->valid_end));
    else
	append_string(context, sp, "- ");

    /* --- password ends */
    if(ent->pw_end)
	append_string(context, sp, "%s ", time2str(*ent->pw_end));
    else
	append_string(context, sp, "- ");

    /* --- max life */
    if(ent->max_life)
	append_string(context, sp, "%d ", *ent->max_life);
    else
	append_string(context, sp, "- ");

    /* --- max renewable life */
    if(ent->max_renew)
	append_string(context, sp, "%d ", *ent->max_renew);
    else
	append_string(context, sp, "- ");

    /* --- flags */
    append_string(context, sp, "%d ", HDBFlags2int(ent->flags));

    /* --- generation number */
    if(ent->generation) {
	append_string(context, sp, "%s:%d:%d ", time2str(ent->generation->time),
		      ent->generation->usec,
		      ent->generation->gen);
    } else
	append_string(context, sp, "- ");

    /* --- extensions */
    if(ent->extensions && ent->extensions->len > 0) {
	for(i = 0; i < ent->extensions->len; i++) {
	    void *d;
	    size_t size, sz = 0;

	    ASN1_MALLOC_ENCODE(HDB_extension, d, size,
			       &ent->extensions->val[i], &sz, ret);
	    if (ret) {
		krb5_clear_error_message(context);
		return ret;
	    }
	    if(size != sz)
		krb5_abortx(context, "internal asn.1 encoder error");

	    if (hex_encode(d, size, &p) < 0) {
		free(d);
		krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
		return ENOMEM;
	    }

	    free(d);
	    append_string(context, sp, "%s%s", p,
			  ent->extensions->len - 1 != i ? ":" : "");
	    free(p);
	}
    } else
	append_string(context, sp, "-");


    return 0;
}

krb5_error_code
hdb_entry2string (krb5_context context, hdb_entry *ent, char **str)
{
    krb5_error_code ret;
    krb5_data data;
    krb5_storage *sp;

    sp = krb5_storage_emem();
    if(sp == NULL) {
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }

    ret = entry2string_int(context, sp, ent);
    if(ret) {
	krb5_storage_free(sp);
	return ret;
    }

    krb5_storage_write(sp, "\0", 1);
    krb5_storage_to_data(sp, &data);
    krb5_storage_free(sp);
    *str = data.data;
    return 0;
}

/* print a hdb_entry to (FILE*)data; suitable for hdb_foreach */

krb5_error_code
hdb_print_entry(krb5_context context, HDB *db, hdb_entry_ex *entry, void *data)
{
    krb5_error_code ret;
    krb5_storage *sp;

    FILE *f = data;

    fflush(f);
    sp = krb5_storage_from_fd(fileno(f));
    if(sp == NULL) {
	krb5_set_error_message(context, ENOMEM, "malloc: out of memory");
	return ENOMEM;
    }

    ret = entry2string_int(context, sp, &entry->entry);
    if(ret) {
	krb5_storage_free(sp);
	return ret;
    }

    krb5_storage_write(sp, "\n", 1);
    krb5_storage_free(sp);
    return 0;
}
