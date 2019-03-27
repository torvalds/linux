/*
 * Copyright (c) 2000-2001 Proofpoint, Inc. and its suppliers.
 *	All rights reserved.
 *
 * By using this file, you agree to the terms and conditions set
 * forth in the LICENSE file which can be found at the top level of
 * the sendmail distribution.
 *
 */

/*
 * Copyright (c) 1995, 1996, 1997, 1998, 1999 Kungliga Tekniska Högskolan
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

/* $Id: sm_resolve.h,v 8.9 2013-11-22 20:51:56 ca Exp $ */

#if DNSMAP
# ifndef __ROKEN_RESOLVE_H__
#  define __ROKEN_RESOLVE_H__

/* We use these, but they are not always present in <arpa/nameser.h> */

#  ifndef T_TXT
#   define T_TXT		16
#  endif /* ! T_TXT */
#  ifndef T_AFSDB
#   define T_AFSDB		18
#  endif /* ! T_AFSDB */
#  ifndef T_SRV
#   define T_SRV		33
#  endif /* ! T_SRV */
#  ifndef T_NAPTR
#   define T_NAPTR		35
#  endif /* ! T_NAPTR */

typedef struct
{
	char		*dns_q_domain;
	unsigned int	dns_q_type;
	unsigned int	dns_q_class;
} DNS_QUERY_T;

typedef struct
{
	unsigned int	mx_r_preference;
	char		mx_r_domain[1];
} MX_RECORD_T;

typedef struct
{
	unsigned int	srv_r_priority;
	unsigned int	srv_r_weight;
	unsigned int	srv_r_port;
	char		srv_r_target[1];
} SRV_RECORDT_T;


typedef struct resource_record RESOURCE_RECORD_T;

struct resource_record
{
	char			*rr_domain;
	unsigned int		rr_type;
	unsigned int		rr_class;
	unsigned int		rr_ttl;
	unsigned int		rr_size;
	union
	{
		void		*rr_data;
		MX_RECORD_T	*rr_mx;
		MX_RECORD_T	*rr_afsdb; /* mx and afsdb are identical */
		SRV_RECORDT_T	*rr_srv;
#  if NETINET
		struct in_addr	*rr_a;
#  endif /* NETINET */
#  if NETINET6
		struct in6_addr *rr_aaaa;
#  endif /* NETINET6 */
		char		*rr_txt;
	} rr_u;
	RESOURCE_RECORD_T *rr_next;
};

#  if !defined(T_A) && !defined(T_AAAA)
/* XXX if <arpa/nameser.h> isn't included */
typedef int HEADER; /* will never be used */
#  endif /* !defined(T_A) && !defined(T_AAAA)  */

typedef struct
{
	HEADER			dns_r_h;
	DNS_QUERY_T		dns_r_q;
	RESOURCE_RECORD_T	*dns_r_head;
} DNS_REPLY_T;


extern void		dns_free_data __P((DNS_REPLY_T *));
extern int		dns_string_to_type __P((const char *));
extern const char	*dns_type_to_string __P((int));
extern DNS_REPLY_T	*dns_lookup_int __P((const char *,
				int,
				int,
				time_t,
				int));
#  if 0
extern DNS_REPLY_T	*dns_lookup __P((const char *domain,
				const char *type_name,
				time_t retrans,
				int retry));
#  endif /* 0 */

# endif /* ! __ROKEN_RESOLVE_H__ */
#endif /* DNSMAP */
