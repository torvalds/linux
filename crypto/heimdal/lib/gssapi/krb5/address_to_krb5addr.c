/*
 * Copyright (c) 2000 - 2001 Kungliga Tekniska Högskolan
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

#include <roken.h>

krb5_error_code
_gsskrb5i_address_to_krb5addr(krb5_context context,
			      OM_uint32 gss_addr_type,
			      gss_buffer_desc *gss_addr,
			      int16_t port,
			      krb5_address *address)
{
   int addr_type;
   struct sockaddr sa;
   krb5_socklen_t sa_size = sizeof(sa);
   krb5_error_code problem;

   if (gss_addr == NULL)
      return GSS_S_FAILURE;

   switch (gss_addr_type) {
#ifdef HAVE_IPV6
      case GSS_C_AF_INET6: addr_type = AF_INET6;
                           break;
#endif /* HAVE_IPV6 */

      case GSS_C_AF_INET:  addr_type = AF_INET;
                           break;
      default:
                           return GSS_S_FAILURE;
   }

   problem = krb5_h_addr2sockaddr (context,
				   addr_type,
                                   gss_addr->value,
                                   &sa,
                                   &sa_size,
                                   port);
   if (problem)
      return GSS_S_FAILURE;

   problem = krb5_sockaddr2address (context, &sa, address);

   return problem;
}
