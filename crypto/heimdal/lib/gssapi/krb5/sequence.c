/*
 * Copyright (c) 2003 - 2006 Kungliga Tekniska Högskolan
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

#include "gsskrb5_locl.h"

#define DEFAULT_JITTER_WINDOW 20

struct gss_msg_order {
    OM_uint32 flags;
    OM_uint32 start;
    OM_uint32 length;
    OM_uint32 jitter_window;
    OM_uint32 first_seq;
    OM_uint32 elem[1];
};


/*
 *
 */

static OM_uint32
msg_order_alloc(OM_uint32 *minor_status,
		struct gss_msg_order **o,
		OM_uint32 jitter_window)
{
    size_t len;

    len = jitter_window * sizeof((*o)->elem[0]);
    len += sizeof(**o);
    len -= sizeof((*o)->elem[0]);

    *o = calloc(1, len);
    if (*o == NULL) {
	*minor_status = ENOMEM;
	return GSS_S_FAILURE;
    }

    *minor_status = 0;
    return GSS_S_COMPLETE;
}

/*
 *
 */

OM_uint32
_gssapi_msg_order_create(OM_uint32 *minor_status,
			 struct gss_msg_order **o,
			 OM_uint32 flags,
			 OM_uint32 seq_num,
			 OM_uint32 jitter_window,
			 int use_64)
{
    OM_uint32 ret;

    if (jitter_window == 0)
	jitter_window = DEFAULT_JITTER_WINDOW;

    ret = msg_order_alloc(minor_status, o, jitter_window);
    if(ret != GSS_S_COMPLETE)
        return ret;

    (*o)->flags = flags;
    (*o)->length = 0;
    (*o)->first_seq = seq_num;
    (*o)->jitter_window = jitter_window;
    (*o)->elem[0] = seq_num - 1;

    *minor_status = 0;
    return GSS_S_COMPLETE;
}

OM_uint32
_gssapi_msg_order_destroy(struct gss_msg_order **m)
{
    free(*m);
    *m = NULL;
    return GSS_S_COMPLETE;
}

static void
elem_set(struct gss_msg_order *o, unsigned int slot, OM_uint32 val)
{
    o->elem[slot % o->jitter_window] = val;
}

static void
elem_insert(struct gss_msg_order *o,
	    unsigned int after_slot,
	    OM_uint32 seq_num)
{
    assert(o->jitter_window > after_slot);

    if (o->length > after_slot)
	memmove(&o->elem[after_slot + 1], &o->elem[after_slot],
		(o->length - after_slot - 1) * sizeof(o->elem[0]));

    elem_set(o, after_slot, seq_num);

    if (o->length < o->jitter_window)
	o->length++;
}

/* rule 1: expected sequence number */
/* rule 2: > expected sequence number */
/* rule 3: seqnum < seqnum(first) */
/* rule 4+5: seqnum in [seqnum(first),seqnum(last)]  */

OM_uint32
_gssapi_msg_order_check(struct gss_msg_order *o, OM_uint32 seq_num)
{
    OM_uint32 r;
    size_t i;

    if (o == NULL)
	return GSS_S_COMPLETE;

    if ((o->flags & (GSS_C_REPLAY_FLAG|GSS_C_SEQUENCE_FLAG)) == 0)
	return GSS_S_COMPLETE;

    /* check if the packet is the next in order */
    if (o->elem[0] == seq_num - 1) {
	elem_insert(o, 0, seq_num);
	return GSS_S_COMPLETE;
    }

    r = (o->flags & (GSS_C_REPLAY_FLAG|GSS_C_SEQUENCE_FLAG))==GSS_C_REPLAY_FLAG;

    /* sequence number larger then largest sequence number
     * or smaller then the first sequence number */
    if (seq_num > o->elem[0]
	|| seq_num < o->first_seq
	|| o->length == 0)
    {
	elem_insert(o, 0, seq_num);
	if (r) {
	    return GSS_S_COMPLETE;
	} else {
	    return GSS_S_GAP_TOKEN;
	}
    }

    assert(o->length > 0);

    /* sequence number smaller the first sequence number */
    if (seq_num < o->elem[o->length - 1]) {
	if (r)
	    return(GSS_S_OLD_TOKEN);
	else
	    return(GSS_S_UNSEQ_TOKEN);
    }

    if (seq_num == o->elem[o->length - 1]) {
	return GSS_S_DUPLICATE_TOKEN;
    }

    for (i = 0; i < o->length - 1; i++) {
	if (o->elem[i] == seq_num)
	    return GSS_S_DUPLICATE_TOKEN;
	if (o->elem[i + 1] < seq_num && o->elem[i] < seq_num) {
	    elem_insert(o, i, seq_num);
	    if (r)
		return GSS_S_COMPLETE;
	    else
		return GSS_S_UNSEQ_TOKEN;
	}
    }

    return GSS_S_FAILURE;
}

OM_uint32
_gssapi_msg_order_f(OM_uint32 flags)
{
    return flags & (GSS_C_SEQUENCE_FLAG|GSS_C_REPLAY_FLAG);
}

/*
 * Translate `o` into inter-process format and export in to `sp'.
 */

krb5_error_code
_gssapi_msg_order_export(krb5_storage *sp, struct gss_msg_order *o)
{
    krb5_error_code kret;
    OM_uint32 i;

    kret = krb5_store_int32(sp, o->flags);
    if (kret)
        return kret;
    kret = krb5_store_int32(sp, o->start);
    if (kret)
        return kret;
    kret = krb5_store_int32(sp, o->length);
    if (kret)
        return kret;
    kret = krb5_store_int32(sp, o->jitter_window);
    if (kret)
        return kret;
    kret = krb5_store_int32(sp, o->first_seq);
    if (kret)
        return kret;

    for (i = 0; i < o->jitter_window; i++) {
        kret = krb5_store_int32(sp, o->elem[i]);
	if (kret)
	    return kret;
    }

    return 0;
}

OM_uint32
_gssapi_msg_order_import(OM_uint32 *minor_status,
			 krb5_storage *sp,
			 struct gss_msg_order **o)
{
    OM_uint32 ret;
    krb5_error_code kret;
    int32_t i, flags, start, length, jitter_window, first_seq;

    kret = krb5_ret_int32(sp, &flags);
    if (kret)
	goto failed;
    kret = krb5_ret_int32(sp, &start);
    if (kret)
	goto failed;
    kret = krb5_ret_int32(sp, &length);
    if (kret)
	goto failed;
    kret = krb5_ret_int32(sp, &jitter_window);
    if (kret)
	goto failed;
    kret = krb5_ret_int32(sp, &first_seq);
    if (kret)
	goto failed;

    ret = msg_order_alloc(minor_status, o, jitter_window);
    if (ret != GSS_S_COMPLETE)
        return ret;

    (*o)->flags = flags;
    (*o)->start = start;
    (*o)->length = length;
    (*o)->jitter_window = jitter_window;
    (*o)->first_seq = first_seq;

    for( i = 0; i < jitter_window; i++ ) {
        kret = krb5_ret_int32(sp, (int32_t*)&((*o)->elem[i]));
	if (kret)
	    goto failed;
    }

    *minor_status = 0;
    return GSS_S_COMPLETE;

failed:
    _gssapi_msg_order_destroy(o);
    *minor_status = kret;
    return GSS_S_FAILURE;
}
