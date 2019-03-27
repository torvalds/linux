/*
 * Copyright (c) 1997 - 2001 Kungliga Tekniska Högskolan
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

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_read_message (krb5_context context,
		   krb5_pointer p_fd,
		   krb5_data *data)
{
    krb5_error_code ret;
    uint32_t len;
    uint8_t buf[4];

    krb5_data_zero(data);

    ret = krb5_net_read (context, p_fd, buf, 4);
    if(ret == -1) {
	ret = errno;
	krb5_clear_error_message (context);
	return ret;
    }
    if(ret < 4) {
	krb5_clear_error_message(context);
	return HEIM_ERR_EOF;
    }
    len = (buf[0] << 24) | (buf[1] << 16) | (buf[2] << 8) | buf[3];
    ret = krb5_data_alloc (data, len);
    if (ret) {
	krb5_clear_error_message(context);
	return ret;
    }
    if (krb5_net_read (context, p_fd, data->data, len) != len) {
	ret = errno;
	krb5_data_free (data);
	krb5_clear_error_message (context);
	return ret;
    }
    return 0;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_read_priv_message(krb5_context context,
		       krb5_auth_context ac,
		       krb5_pointer p_fd,
		       krb5_data *data)
{
    krb5_error_code ret;
    krb5_data packet;

    ret = krb5_read_message(context, p_fd, &packet);
    if(ret)
	return ret;
    ret = krb5_rd_priv (context, ac, &packet, data, NULL);
    krb5_data_free(&packet);
    return ret;
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_read_safe_message(krb5_context context,
		       krb5_auth_context ac,
		       krb5_pointer p_fd,
		       krb5_data *data)
{
    krb5_error_code ret;
    krb5_data packet;

    ret = krb5_read_message(context, p_fd, &packet);
    if(ret)
	return ret;
    ret = krb5_rd_safe (context, ac, &packet, data, NULL);
    krb5_data_free(&packet);
    return ret;
}
