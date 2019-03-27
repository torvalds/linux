/*
 * Copyright (c) 2000-2004, 2010 Proofpoint, Inc. and its suppliers.
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

#include <sendmail.h>
#if DNSMAP
# if NAMED_BIND
#  if NETINET
#   include <netinet/in_systm.h>
#   include <netinet/ip.h>
#  endif /* NETINET */
#  include "sm_resolve.h"

SM_RCSID("$Id: sm_resolve.c,v 8.40 2013-11-22 20:51:56 ca Exp $")

static struct stot
{
	const char	*st_name;
	int		st_type;
} stot[] =
{
#  if NETINET
	{	"A",		T_A		},
#  endif /* NETINET */
#  if NETINET6
	{	"AAAA",		T_AAAA		},
#  endif /* NETINET6 */
	{	"NS",		T_NS		},
	{	"CNAME",	T_CNAME		},
	{	"PTR",		T_PTR		},
	{	"MX",		T_MX		},
	{	"TXT",		T_TXT		},
	{	"AFSDB",	T_AFSDB		},
	{	"SRV",		T_SRV		},
	{	NULL,		0		}
};

static DNS_REPLY_T *parse_dns_reply __P((unsigned char *, int));

/*
**  DNS_STRING_TO_TYPE -- convert resource record name into type
**
**	Parameters:
**		name -- name of resource record type
**
**	Returns:
**		type if succeeded.
**		-1 otherwise.
*/

int
dns_string_to_type(name)
	const char *name;
{
	struct stot *p = stot;

	for (p = stot; p->st_name != NULL; p++)
		if (sm_strcasecmp(name, p->st_name) == 0)
			return p->st_type;
	return -1;
}

/*
**  DNS_TYPE_TO_STRING -- convert resource record type into name
**
**	Parameters:
**		type -- resource record type
**
**	Returns:
**		name if succeeded.
**		NULL otherwise.
*/

const char *
dns_type_to_string(type)
	int type;
{
	struct stot *p = stot;

	for (p = stot; p->st_name != NULL; p++)
		if (type == p->st_type)
			return p->st_name;
	return NULL;
}

/*
**  DNS_FREE_DATA -- free all components of a DNS_REPLY_T
**
**	Parameters:
**		r -- pointer to DNS_REPLY_T
**
**	Returns:
**		none.
*/

void
dns_free_data(r)
	DNS_REPLY_T *r;
{
	RESOURCE_RECORD_T *rr;

	if (r->dns_r_q.dns_q_domain != NULL)
		sm_free(r->dns_r_q.dns_q_domain);
	for (rr = r->dns_r_head; rr != NULL; )
	{
		RESOURCE_RECORD_T *tmp = rr;

		if (rr->rr_domain != NULL)
			sm_free(rr->rr_domain);
		if (rr->rr_u.rr_data != NULL)
			sm_free(rr->rr_u.rr_data);
		rr = rr->rr_next;
		sm_free(tmp);
	}
	sm_free(r);
}

/*
**  PARSE_DNS_REPLY -- parse DNS reply data.
**
**	Parameters:
**		data -- pointer to dns data
**		len -- len of data
**
**	Returns:
**		pointer to DNS_REPLY_T if succeeded.
**		NULL otherwise.
*/

static DNS_REPLY_T *
parse_dns_reply(data, len)
	unsigned char *data;
	int len;
{
	unsigned char *p;
	unsigned short ans_cnt, ui;
	int status;
	size_t l;
	char host[MAXHOSTNAMELEN];
	DNS_REPLY_T *r;
	RESOURCE_RECORD_T **rr;

	r = (DNS_REPLY_T *) sm_malloc(sizeof(*r));
	if (r == NULL)
		return NULL;
	memset(r, 0, sizeof(*r));

	p = data;

	/* doesn't work on Crays? */
	memcpy(&r->dns_r_h, p, sizeof(r->dns_r_h));
	p += sizeof(r->dns_r_h);
	status = dn_expand(data, data + len, p, host, sizeof(host));
	if (status < 0)
	{
		dns_free_data(r);
		return NULL;
	}
	r->dns_r_q.dns_q_domain = sm_strdup(host);
	if (r->dns_r_q.dns_q_domain == NULL)
	{
		dns_free_data(r);
		return NULL;
	}

	ans_cnt = ntohs((unsigned short) r->dns_r_h.ancount);

	p += status;
	GETSHORT(r->dns_r_q.dns_q_type, p);
	GETSHORT(r->dns_r_q.dns_q_class, p);
	rr = &r->dns_r_head;
	ui = 0;
	while (p < data + len && ui < ans_cnt)
	{
		int type, class, ttl, size, txtlen;

		status = dn_expand(data, data + len, p, host, sizeof(host));
		if (status < 0)
		{
			dns_free_data(r);
			return NULL;
		}
		++ui;
		p += status;
		GETSHORT(type, p);
		GETSHORT(class, p);
		GETLONG(ttl, p);
		GETSHORT(size, p);
		if (p + size > data + len)
		{
			/*
			**  announced size of data exceeds length of
			**  data paket: someone is cheating.
			*/

			if (LogLevel > 5)
				sm_syslog(LOG_WARNING, NOQID,
					  "ERROR: DNS RDLENGTH=%d > data len=%d",
					  size, len - (int)(p - data));
			dns_free_data(r);
			return NULL;
		}
		*rr = (RESOURCE_RECORD_T *) sm_malloc(sizeof(**rr));
		if (*rr == NULL)
		{
			dns_free_data(r);
			return NULL;
		}
		memset(*rr, 0, sizeof(**rr));
		(*rr)->rr_domain = sm_strdup(host);
		if ((*rr)->rr_domain == NULL)
		{
			dns_free_data(r);
			return NULL;
		}
		(*rr)->rr_type = type;
		(*rr)->rr_class = class;
		(*rr)->rr_ttl = ttl;
		(*rr)->rr_size = size;
		switch (type)
		{
		  case T_NS:
		  case T_CNAME:
		  case T_PTR:
			status = dn_expand(data, data + len, p, host,
					   sizeof(host));
			if (status < 0)
			{
				dns_free_data(r);
				return NULL;
			}
			(*rr)->rr_u.rr_txt = sm_strdup(host);
			if ((*rr)->rr_u.rr_txt == NULL)
			{
				dns_free_data(r);
				return NULL;
			}
			break;

		  case T_MX:
		  case T_AFSDB:
			status = dn_expand(data, data + len, p + 2, host,
					   sizeof(host));
			if (status < 0)
			{
				dns_free_data(r);
				return NULL;
			}
			l = strlen(host) + 1;
			(*rr)->rr_u.rr_mx = (MX_RECORD_T *)
				sm_malloc(sizeof(*((*rr)->rr_u.rr_mx)) + l);
			if ((*rr)->rr_u.rr_mx == NULL)
			{
				dns_free_data(r);
				return NULL;
			}
			(*rr)->rr_u.rr_mx->mx_r_preference = (p[0] << 8) | p[1];
			(void) sm_strlcpy((*rr)->rr_u.rr_mx->mx_r_domain,
					  host, l);
			break;

		  case T_SRV:
			status = dn_expand(data, data + len, p + 6, host,
					   sizeof(host));
			if (status < 0)
			{
				dns_free_data(r);
				return NULL;
			}
			l = strlen(host) + 1;
			(*rr)->rr_u.rr_srv = (SRV_RECORDT_T*)
				sm_malloc(sizeof(*((*rr)->rr_u.rr_srv)) + l);
			if ((*rr)->rr_u.rr_srv == NULL)
			{
				dns_free_data(r);
				return NULL;
			}
			(*rr)->rr_u.rr_srv->srv_r_priority = (p[0] << 8) | p[1];
			(*rr)->rr_u.rr_srv->srv_r_weight = (p[2] << 8) | p[3];
			(*rr)->rr_u.rr_srv->srv_r_port = (p[4] << 8) | p[5];
			(void) sm_strlcpy((*rr)->rr_u.rr_srv->srv_r_target,
					  host, l);
			break;

		  case T_TXT:

			/*
			**  The TXT record contains the length as
			**  leading byte, hence the value is restricted
			**  to 255, which is less than the maximum value
			**  of RDLENGTH (size). Nevertheless, txtlen
			**  must be less than size because the latter
			**  specifies the length of the entire TXT
			**  record.
			*/

			txtlen = *p;
			if (txtlen >= size)
			{
				if (LogLevel > 5)
					sm_syslog(LOG_WARNING, NOQID,
						  "ERROR: DNS TXT record size=%d <= text len=%d",
						  size, txtlen);
				dns_free_data(r);
				return NULL;
			}
			(*rr)->rr_u.rr_txt = (char *) sm_malloc(txtlen + 1);
			if ((*rr)->rr_u.rr_txt == NULL)
			{
				dns_free_data(r);
				return NULL;
			}
			(void) sm_strlcpy((*rr)->rr_u.rr_txt, (char*) p + 1,
					  txtlen + 1);
			break;

		  default:
			(*rr)->rr_u.rr_data = (unsigned char*) sm_malloc(size);
			if ((*rr)->rr_u.rr_data == NULL)
			{
				dns_free_data(r);
				return NULL;
			}
			(void) memcpy((*rr)->rr_u.rr_data, p, size);
			break;
		}
		p += size;
		rr = &(*rr)->rr_next;
	}
	*rr = NULL;
	return r;
}

/*
**  DNS_LOOKUP_INT -- perform dns map lookup (internal helper routine)
**
**	Parameters:
**		domain -- name to lookup
**		rr_class -- resource record class
**		rr_type -- resource record type
**		retrans -- retransmission timeout
**		retry -- number of retries
**
**	Returns:
**		result of lookup if succeeded.
**		NULL otherwise.
*/

DNS_REPLY_T *
dns_lookup_int(domain, rr_class, rr_type, retrans, retry)
	const char *domain;
	int rr_class;
	int rr_type;
	time_t retrans;
	int retry;
{
	int len;
	unsigned long old_options = 0;
	time_t save_retrans = 0;
	int save_retry = 0;
	DNS_REPLY_T *r = NULL;
	querybuf reply_buf;
	unsigned char *reply;

#define SMRBSIZE sizeof(reply_buf)
#ifndef IP_MAXPACKET
# define IP_MAXPACKET	65535
#endif

	if (tTd(8, 16))
	{
		old_options = _res.options;
		_res.options |= RES_DEBUG;
		sm_dprintf("dns_lookup(%s, %d, %s)\n", domain,
			   rr_class, dns_type_to_string(rr_type));
	}
	if (retrans > 0)
	{
		save_retrans = _res.retrans;
		_res.retrans = retrans;
	}
	if (retry > 0)
	{
		save_retry = _res.retry;
		_res.retry = retry;
	}
	errno = 0;
	SM_SET_H_ERRNO(0);
	reply = (unsigned char *)&reply_buf;
	len = res_search(domain, rr_class, rr_type, reply, SMRBSIZE);
	if (len >= SMRBSIZE)
	{
		if (len >= IP_MAXPACKET)
		{
			if (tTd(8, 4))
				sm_dprintf("dns_lookup: domain=%s, length=%d, default_size=%d, max=%d, status=response too long\n",
					   domain, len, (int) SMRBSIZE,
					   IP_MAXPACKET);
		}
		else
		{
			if (tTd(8, 6))
				sm_dprintf("dns_lookup: domain=%s, length=%d, default_size=%d, max=%d, status=response longer than default size, resizing\n",
					   domain, len, (int) SMRBSIZE,
					   IP_MAXPACKET);
			reply = (unsigned char *)sm_malloc(IP_MAXPACKET);
			if (reply == NULL)
				SM_SET_H_ERRNO(TRY_AGAIN);
			else
				len = res_search(domain, rr_class, rr_type,
						 reply, IP_MAXPACKET);
		}
	}
	if (tTd(8, 16))
	{
		_res.options = old_options;
		sm_dprintf("dns_lookup(%s, %d, %s) --> %d\n",
			   domain, rr_class, dns_type_to_string(rr_type), len);
	}
	if (len >= 0 && len < IP_MAXPACKET && reply != NULL)
		r = parse_dns_reply(reply, len);
	if (reply != (unsigned char *)&reply_buf && reply != NULL)
	{
		sm_free(reply);
		reply = NULL;
	}
	if (retrans > 0)
		_res.retrans = save_retrans;
	if (retry > 0)
		_res.retry = save_retry;
	return r;
}

#  if 0
DNS_REPLY_T *
dns_lookup(domain, type_name, retrans, retry)
	const char *domain;
	const char *type_name;
	time_t retrans;
	int retry;
{
	int type;

	type = dns_string_to_type(type_name);
	if (type == -1)
	{
		if (tTd(8, 16))
			sm_dprintf("dns_lookup: unknown resource type: `%s'\n",
				type_name);
		return NULL;
	}
	return dns_lookup_int(domain, C_IN, type, retrans, retry);
}
#  endif /* 0 */
# endif /* NAMED_BIND */
#endif /* DNSMAP */
