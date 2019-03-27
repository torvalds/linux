/*
 * Copyright (c) 1998 - 2001 Kungliga Tekniska Högskolan
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

#ifndef HEIMDAL_SMALLER

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decode_EncTicketPart (krb5_context context,
			   const void *data,
			   size_t length,
			   EncTicketPart *t,
			   size_t *len)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return decode_EncTicketPart(data, length, t, len);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encode_EncTicketPart (krb5_context context,
			   void *data,
			   size_t length,
			   EncTicketPart *t,
			   size_t *len)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return encode_EncTicketPart(data, length, t, len);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decode_EncASRepPart (krb5_context context,
			  const void *data,
			  size_t length,
			  EncASRepPart *t,
			  size_t *len)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return decode_EncASRepPart(data, length, t, len);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encode_EncASRepPart (krb5_context context,
			  void *data,
			  size_t length,
			  EncASRepPart *t,
			  size_t *len)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return encode_EncASRepPart(data, length, t, len);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decode_EncTGSRepPart (krb5_context context,
			   const void *data,
			   size_t length,
			   EncTGSRepPart *t,
			   size_t *len)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return decode_EncTGSRepPart(data, length, t, len);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encode_EncTGSRepPart (krb5_context context,
			   void *data,
			   size_t length,
			   EncTGSRepPart *t,
			   size_t *len)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return encode_EncTGSRepPart(data, length, t, len);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decode_EncAPRepPart (krb5_context context,
			  const void *data,
			  size_t length,
			  EncAPRepPart *t,
			  size_t *len)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return decode_EncAPRepPart(data, length, t, len);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encode_EncAPRepPart (krb5_context context,
			  void *data,
			  size_t length,
			  EncAPRepPart *t,
			  size_t *len)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return encode_EncAPRepPart(data, length, t, len);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decode_Authenticator (krb5_context context,
			   const void *data,
			   size_t length,
			   Authenticator *t,
			   size_t *len)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return decode_Authenticator(data, length, t, len);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encode_Authenticator (krb5_context context,
			   void *data,
			   size_t length,
			   Authenticator *t,
			   size_t *len)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return encode_Authenticator(data, length, t, len);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decode_EncKrbCredPart (krb5_context context,
			    const void *data,
			    size_t length,
			    EncKrbCredPart *t,
			    size_t *len)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return decode_EncKrbCredPart(data, length, t, len);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encode_EncKrbCredPart (krb5_context context,
			    void *data,
			    size_t length,
			    EncKrbCredPart *t,
			    size_t *len)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return encode_EncKrbCredPart (data, length, t, len);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decode_ETYPE_INFO (krb5_context context,
			const void *data,
			size_t length,
			ETYPE_INFO *t,
			size_t *len)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return decode_ETYPE_INFO(data, length, t, len);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encode_ETYPE_INFO (krb5_context context,
			void *data,
			size_t length,
			ETYPE_INFO *t,
			size_t *len)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return encode_ETYPE_INFO (data, length, t, len);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_decode_ETYPE_INFO2 (krb5_context context,
			const void *data,
			size_t length,
			ETYPE_INFO2 *t,
			size_t *len)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return decode_ETYPE_INFO2(data, length, t, len);
}

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_encode_ETYPE_INFO2 (krb5_context context,
			 void *data,
			 size_t length,
			 ETYPE_INFO2 *t,
			 size_t *len)
    KRB5_DEPRECATED_FUNCTION("Use X instead")
{
    return encode_ETYPE_INFO2 (data, length, t, len);
}

#endif /* HEIMDAL_SMALLER */
