/*
 * Copyright (c) 2001-2002 Kungliga Tekniska Högskolan
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

#include "krb5_locl.h"

struct any_data {
    krb5_keytab kt;
    char *name;
    struct any_data *next;
};

static void
free_list (krb5_context context, struct any_data *a)
{
    struct any_data *next;

    for (; a != NULL; a = next) {
	next = a->next;
	free (a->name);
	if(a->kt)
	    krb5_kt_close(context, a->kt);
	free (a);
    }
}

static krb5_error_code KRB5_CALLCONV
any_resolve(krb5_context context, const char *name, krb5_keytab id)
{
    struct any_data *a, *a0 = NULL, *prev = NULL;
    krb5_error_code ret;
    char buf[256];

    while (strsep_copy(&name, ",", buf, sizeof(buf)) != -1) {
	a = calloc(1, sizeof(*a));
	if (a == NULL) {
	    ret = ENOMEM;
	    goto fail;
	}
	if (a0 == NULL) {
	    a0 = a;
	    a->name = strdup(buf);
	    if (a->name == NULL) {
		ret = ENOMEM;
		krb5_set_error_message(context, ret, N_("malloc: out of memory", ""));
		goto fail;
	    }
	} else
	    a->name = NULL;
	if (prev != NULL)
	    prev->next = a;
	a->next = NULL;
	ret = krb5_kt_resolve (context, buf, &a->kt);
	if (ret)
	    goto fail;
	prev = a;
    }
    if (a0 == NULL) {
	krb5_set_error_message(context, ENOENT, N_("empty ANY: keytab", ""));
	return ENOENT;
    }
    id->data = a0;
    return 0;
 fail:
    free_list (context, a0);
    return ret;
}

static krb5_error_code KRB5_CALLCONV
any_get_name (krb5_context context,
	      krb5_keytab id,
	      char *name,
	      size_t namesize)
{
    struct any_data *a = id->data;
    strlcpy(name, a->name, namesize);
    return 0;
}

static krb5_error_code KRB5_CALLCONV
any_close (krb5_context context,
	   krb5_keytab id)
{
    struct any_data *a = id->data;

    free_list (context, a);
    return 0;
}

struct any_cursor_extra_data {
    struct any_data *a;
    krb5_kt_cursor cursor;
};

static krb5_error_code KRB5_CALLCONV
any_start_seq_get(krb5_context context,
		  krb5_keytab id,
		  krb5_kt_cursor *c)
{
    struct any_data *a = id->data;
    struct any_cursor_extra_data *ed;
    krb5_error_code ret;

    c->data = malloc (sizeof(struct any_cursor_extra_data));
    if(c->data == NULL){
	krb5_set_error_message(context, ENOMEM, N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    ed = (struct any_cursor_extra_data *)c->data;
    for (ed->a = a; ed->a != NULL; ed->a = ed->a->next) {
	ret = krb5_kt_start_seq_get(context, ed->a->kt, &ed->cursor);
	if (ret == 0)
	    break;
    }
    if (ed->a == NULL) {
	free (c->data);
	c->data = NULL;
	krb5_clear_error_message (context);
	return KRB5_KT_END;
    }
    return 0;
}

static krb5_error_code KRB5_CALLCONV
any_next_entry (krb5_context context,
		krb5_keytab id,
		krb5_keytab_entry *entry,
		krb5_kt_cursor *cursor)
{
    krb5_error_code ret, ret2;
    struct any_cursor_extra_data *ed;

    ed = (struct any_cursor_extra_data *)cursor->data;
    do {
	ret = krb5_kt_next_entry(context, ed->a->kt, entry, &ed->cursor);
	if (ret == 0)
	    return 0;
	else if (ret != KRB5_KT_END)
	    return ret;

	ret2 = krb5_kt_end_seq_get (context, ed->a->kt, &ed->cursor);
	if (ret2)
	    return ret2;
	while ((ed->a = ed->a->next) != NULL) {
	    ret2 = krb5_kt_start_seq_get(context, ed->a->kt, &ed->cursor);
	    if (ret2 == 0)
		break;
	}
	if (ed->a == NULL) {
	    krb5_clear_error_message (context);
	    return KRB5_KT_END;
	}
    } while (1);
}

static krb5_error_code KRB5_CALLCONV
any_end_seq_get(krb5_context context,
		krb5_keytab id,
		krb5_kt_cursor *cursor)
{
    krb5_error_code ret = 0;
    struct any_cursor_extra_data *ed;

    ed = (struct any_cursor_extra_data *)cursor->data;
    if (ed->a != NULL)
	ret = krb5_kt_end_seq_get(context, ed->a->kt, &ed->cursor);
    free (ed);
    cursor->data = NULL;
    return ret;
}

static krb5_error_code KRB5_CALLCONV
any_add_entry(krb5_context context,
	      krb5_keytab id,
	      krb5_keytab_entry *entry)
{
    struct any_data *a = id->data;
    krb5_error_code ret;
    while(a != NULL) {
	ret = krb5_kt_add_entry(context, a->kt, entry);
	if(ret != 0 && ret != KRB5_KT_NOWRITE) {
	    krb5_set_error_message(context, ret,
				   N_("failed to add entry to %s", ""),
				   a->name);
	    return ret;
	}
	a = a->next;
    }
    return 0;
}

static krb5_error_code KRB5_CALLCONV
any_remove_entry(krb5_context context,
		 krb5_keytab id,
		 krb5_keytab_entry *entry)
{
    struct any_data *a = id->data;
    krb5_error_code ret;
    int found = 0;
    while(a != NULL) {
	ret = krb5_kt_remove_entry(context, a->kt, entry);
	if(ret == 0)
	    found++;
	else {
	    if(ret != KRB5_KT_NOWRITE && ret != KRB5_KT_NOTFOUND) {
		krb5_set_error_message(context, ret,
				       N_("Failed to remove keytab "
					  "entry from %s", "keytab name"),
				       a->name);
		return ret;
	    }
	}
	a = a->next;
    }
    if(!found)
	return KRB5_KT_NOTFOUND;
    return 0;
}

const krb5_kt_ops krb5_any_ops = {
    "ANY",
    any_resolve,
    any_get_name,
    any_close,
    NULL, /* destroy */
    NULL, /* get */
    any_start_seq_get,
    any_next_entry,
    any_end_seq_get,
    any_add_entry,
    any_remove_entry
};
