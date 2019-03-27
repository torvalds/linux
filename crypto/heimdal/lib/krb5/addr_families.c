/*
 * Copyright (c) 1997-2007 Kungliga Tekniska Högskolan
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

struct addr_operations {
    int af;
    krb5_address_type atype;
    size_t max_sockaddr_size;
    krb5_error_code (*sockaddr2addr)(const struct sockaddr *, krb5_address *);
    krb5_error_code (*sockaddr2port)(const struct sockaddr *, int16_t *);
    void (*addr2sockaddr)(const krb5_address *, struct sockaddr *,
			  krb5_socklen_t *sa_size, int port);
    void (*h_addr2sockaddr)(const char *, struct sockaddr *, krb5_socklen_t *, int);
    krb5_error_code (*h_addr2addr)(const char *, krb5_address *);
    krb5_boolean (*uninteresting)(const struct sockaddr *);
    krb5_boolean (*is_loopback)(const struct sockaddr *);
    void (*anyaddr)(struct sockaddr *, krb5_socklen_t *, int);
    int (*print_addr)(const krb5_address *, char *, size_t);
    int (*parse_addr)(krb5_context, const char*, krb5_address *);
    int (*order_addr)(krb5_context, const krb5_address*, const krb5_address*);
    int (*free_addr)(krb5_context, krb5_address*);
    int (*copy_addr)(krb5_context, const krb5_address*, krb5_address*);
    int (*mask_boundary)(krb5_context, const krb5_address*, unsigned long,
			 krb5_address*, krb5_address*);
};

/*
 * AF_INET - aka IPv4 implementation
 */

static krb5_error_code
ipv4_sockaddr2addr (const struct sockaddr *sa, krb5_address *a)
{
    const struct sockaddr_in *sin4 = (const struct sockaddr_in *)sa;
    unsigned char buf[4];

    a->addr_type = KRB5_ADDRESS_INET;
    memcpy (buf, &sin4->sin_addr, 4);
    return krb5_data_copy(&a->address, buf, 4);
}

static krb5_error_code
ipv4_sockaddr2port (const struct sockaddr *sa, int16_t *port)
{
    const struct sockaddr_in *sin4 = (const struct sockaddr_in *)sa;

    *port = sin4->sin_port;
    return 0;
}

static void
ipv4_addr2sockaddr (const krb5_address *a,
		    struct sockaddr *sa,
		    krb5_socklen_t *sa_size,
		    int port)
{
    struct sockaddr_in tmp;

    memset (&tmp, 0, sizeof(tmp));
    tmp.sin_family = AF_INET;
    memcpy (&tmp.sin_addr, a->address.data, 4);
    tmp.sin_port = port;
    memcpy(sa, &tmp, min(sizeof(tmp), *sa_size));
    *sa_size = sizeof(tmp);
}

static void
ipv4_h_addr2sockaddr(const char *addr,
		     struct sockaddr *sa,
		     krb5_socklen_t *sa_size,
		     int port)
{
    struct sockaddr_in tmp;

    memset (&tmp, 0, sizeof(tmp));
    tmp.sin_family = AF_INET;
    tmp.sin_port   = port;
    tmp.sin_addr   = *((const struct in_addr *)addr);
    memcpy(sa, &tmp, min(sizeof(tmp), *sa_size));
    *sa_size = sizeof(tmp);
}

static krb5_error_code
ipv4_h_addr2addr (const char *addr,
		  krb5_address *a)
{
    unsigned char buf[4];

    a->addr_type = KRB5_ADDRESS_INET;
    memcpy(buf, addr, 4);
    return krb5_data_copy(&a->address, buf, 4);
}

/*
 * Are there any addresses that should be considered `uninteresting'?
 */

static krb5_boolean
ipv4_uninteresting (const struct sockaddr *sa)
{
    const struct sockaddr_in *sin4 = (const struct sockaddr_in *)sa;

    if (sin4->sin_addr.s_addr == INADDR_ANY)
	return TRUE;

    return FALSE;
}

static krb5_boolean
ipv4_is_loopback (const struct sockaddr *sa)
{
    const struct sockaddr_in *sin4 = (const struct sockaddr_in *)sa;

    if ((ntohl(sin4->sin_addr.s_addr) >> 24) == IN_LOOPBACKNET)
	return TRUE;

    return FALSE;
}

static void
ipv4_anyaddr (struct sockaddr *sa, krb5_socklen_t *sa_size, int port)
{
    struct sockaddr_in tmp;

    memset (&tmp, 0, sizeof(tmp));
    tmp.sin_family = AF_INET;
    tmp.sin_port   = port;
    tmp.sin_addr.s_addr = INADDR_ANY;
    memcpy(sa, &tmp, min(sizeof(tmp), *sa_size));
    *sa_size = sizeof(tmp);
}

static int
ipv4_print_addr (const krb5_address *addr, char *str, size_t len)
{
    struct in_addr ia;

    memcpy (&ia, addr->address.data, 4);

    return snprintf (str, len, "IPv4:%s", inet_ntoa(ia));
}

static int
ipv4_parse_addr (krb5_context context, const char *address, krb5_address *addr)
{
    const char *p;
    struct in_addr a;

    p = strchr(address, ':');
    if(p) {
	p++;
	if(strncasecmp(address, "ip:", p - address) != 0 &&
	   strncasecmp(address, "ip4:", p - address) != 0 &&
	   strncasecmp(address, "ipv4:", p - address) != 0 &&
	   strncasecmp(address, "inet:", p - address) != 0)
	    return -1;
    } else
	p = address;
    if(inet_aton(p, &a) == 0)
	return -1;
    addr->addr_type = KRB5_ADDRESS_INET;
    if(krb5_data_alloc(&addr->address, 4) != 0)
	return -1;
    _krb5_put_int(addr->address.data, ntohl(a.s_addr), addr->address.length);
    return 0;
}

static int
ipv4_mask_boundary(krb5_context context, const krb5_address *inaddr,
		   unsigned long len, krb5_address *low, krb5_address *high)
{
    unsigned long ia;
    uint32_t l, h, m = 0xffffffff;

    if (len > 32) {
	krb5_set_error_message(context, KRB5_PROG_ATYPE_NOSUPP,
			       N_("IPv4 prefix too large (%ld)", "len"), len);
	return KRB5_PROG_ATYPE_NOSUPP;
    }
    m = m << (32 - len);

    _krb5_get_int(inaddr->address.data, &ia, inaddr->address.length);

    l = ia & m;
    h = l | ~m;

    low->addr_type = KRB5_ADDRESS_INET;
    if(krb5_data_alloc(&low->address, 4) != 0)
	return -1;
    _krb5_put_int(low->address.data, l, low->address.length);

    high->addr_type = KRB5_ADDRESS_INET;
    if(krb5_data_alloc(&high->address, 4) != 0) {
	krb5_free_address(context, low);
	return -1;
    }
    _krb5_put_int(high->address.data, h, high->address.length);

    return 0;
}


/*
 * AF_INET6 - aka IPv6 implementation
 */

#ifdef HAVE_IPV6

static krb5_error_code
ipv6_sockaddr2addr (const struct sockaddr *sa, krb5_address *a)
{
    const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;

    if (IN6_IS_ADDR_V4MAPPED(&sin6->sin6_addr)) {
	unsigned char buf[4];

	a->addr_type      = KRB5_ADDRESS_INET;
#ifndef IN6_ADDR_V6_TO_V4
#ifdef IN6_EXTRACT_V4ADDR
#define IN6_ADDR_V6_TO_V4(x) (&IN6_EXTRACT_V4ADDR(x))
#else
#define IN6_ADDR_V6_TO_V4(x) ((const struct in_addr *)&(x)->s6_addr[12])
#endif
#endif
	memcpy (buf, IN6_ADDR_V6_TO_V4(&sin6->sin6_addr), 4);
	return krb5_data_copy(&a->address, buf, 4);
    } else {
	a->addr_type = KRB5_ADDRESS_INET6;
	return krb5_data_copy(&a->address,
			      &sin6->sin6_addr,
			      sizeof(sin6->sin6_addr));
    }
}

static krb5_error_code
ipv6_sockaddr2port (const struct sockaddr *sa, int16_t *port)
{
    const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;

    *port = sin6->sin6_port;
    return 0;
}

static void
ipv6_addr2sockaddr (const krb5_address *a,
		    struct sockaddr *sa,
		    krb5_socklen_t *sa_size,
		    int port)
{
    struct sockaddr_in6 tmp;

    memset (&tmp, 0, sizeof(tmp));
    tmp.sin6_family = AF_INET6;
    memcpy (&tmp.sin6_addr, a->address.data, sizeof(tmp.sin6_addr));
    tmp.sin6_port = port;
    memcpy(sa, &tmp, min(sizeof(tmp), *sa_size));
    *sa_size = sizeof(tmp);
}

static void
ipv6_h_addr2sockaddr(const char *addr,
		     struct sockaddr *sa,
		     krb5_socklen_t *sa_size,
		     int port)
{
    struct sockaddr_in6 tmp;

    memset (&tmp, 0, sizeof(tmp));
    tmp.sin6_family = AF_INET6;
    tmp.sin6_port   = port;
    tmp.sin6_addr   = *((const struct in6_addr *)addr);
    memcpy(sa, &tmp, min(sizeof(tmp), *sa_size));
    *sa_size = sizeof(tmp);
}

static krb5_error_code
ipv6_h_addr2addr (const char *addr,
		  krb5_address *a)
{
    a->addr_type = KRB5_ADDRESS_INET6;
    return krb5_data_copy(&a->address, addr, sizeof(struct in6_addr));
}

/*
 *
 */

static krb5_boolean
ipv6_uninteresting (const struct sockaddr *sa)
{
    const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
    const struct in6_addr *in6 = (const struct in6_addr *)&sin6->sin6_addr;

    return IN6_IS_ADDR_LINKLOCAL(in6)
	|| IN6_IS_ADDR_V4COMPAT(in6);
}

static krb5_boolean
ipv6_is_loopback (const struct sockaddr *sa)
{
    const struct sockaddr_in6 *sin6 = (const struct sockaddr_in6 *)sa;
    const struct in6_addr *in6 = (const struct in6_addr *)&sin6->sin6_addr;

    return (IN6_IS_ADDR_LOOPBACK(in6));
}

static void
ipv6_anyaddr (struct sockaddr *sa, krb5_socklen_t *sa_size, int port)
{
    struct sockaddr_in6 tmp;

    memset (&tmp, 0, sizeof(tmp));
    tmp.sin6_family = AF_INET6;
    tmp.sin6_port   = port;
    tmp.sin6_addr   = in6addr_any;
    *sa_size = sizeof(tmp);
}

static int
ipv6_print_addr (const krb5_address *addr, char *str, size_t len)
{
    char buf[128], buf2[3];
    if(inet_ntop(AF_INET6, addr->address.data, buf, sizeof(buf)) == NULL)
	{
	    /* XXX this is pretty ugly, but better than abort() */
	    size_t i;
	    unsigned char *p = addr->address.data;
	    buf[0] = '\0';
	    for(i = 0; i < addr->address.length; i++) {
		snprintf(buf2, sizeof(buf2), "%02x", p[i]);
		if(i > 0 && (i & 1) == 0)
		    strlcat(buf, ":", sizeof(buf));
		strlcat(buf, buf2, sizeof(buf));
	    }
	}
    return snprintf(str, len, "IPv6:%s", buf);
}

static int
ipv6_parse_addr (krb5_context context, const char *address, krb5_address *addr)
{
    int ret;
    struct in6_addr in6;
    const char *p;

    p = strchr(address, ':');
    if(p) {
	p++;
	if(strncasecmp(address, "ip6:", p - address) == 0 ||
	   strncasecmp(address, "ipv6:", p - address) == 0 ||
	   strncasecmp(address, "inet6:", p - address) == 0)
	    address = p;
    }

    ret = inet_pton(AF_INET6, address, &in6.s6_addr);
    if(ret == 1) {
	addr->addr_type = KRB5_ADDRESS_INET6;
	ret = krb5_data_alloc(&addr->address, sizeof(in6.s6_addr));
	if (ret)
	    return -1;
	memcpy(addr->address.data, in6.s6_addr, sizeof(in6.s6_addr));
	return 0;
    }
    return -1;
}

static int
ipv6_mask_boundary(krb5_context context, const krb5_address *inaddr,
		   unsigned long len, krb5_address *low, krb5_address *high)
{
    struct in6_addr addr, laddr, haddr;
    uint32_t m;
    int i, sub_len;

    if (len > 128) {
	krb5_set_error_message(context, KRB5_PROG_ATYPE_NOSUPP,
			       N_("IPv6 prefix too large (%ld)", "length"), len);
	return KRB5_PROG_ATYPE_NOSUPP;
    }

    if (inaddr->address.length != sizeof(addr)) {
	krb5_set_error_message(context, KRB5_PROG_ATYPE_NOSUPP,
			       N_("IPv6 addr bad length", ""));
	return KRB5_PROG_ATYPE_NOSUPP;
    }

    memcpy(&addr, inaddr->address.data, inaddr->address.length);

    for (i = 0; i < 16; i++) {
	sub_len = min(8, len);

	m = 0xff << (8 - sub_len);

	laddr.s6_addr[i] = addr.s6_addr[i] & m;
	haddr.s6_addr[i] = (addr.s6_addr[i] & m) | ~m;

	if (len > 8)
	    len -= 8;
	else
	    len = 0;
    }

    low->addr_type = KRB5_ADDRESS_INET6;
    if (krb5_data_alloc(&low->address, sizeof(laddr.s6_addr)) != 0)
	return -1;
    memcpy(low->address.data, laddr.s6_addr, sizeof(laddr.s6_addr));

    high->addr_type = KRB5_ADDRESS_INET6;
    if (krb5_data_alloc(&high->address, sizeof(haddr.s6_addr)) != 0) {
	krb5_free_address(context, low);
	return -1;
    }
    memcpy(high->address.data, haddr.s6_addr, sizeof(haddr.s6_addr));

    return 0;
}

#endif /* IPv6 */

#ifndef HEIMDAL_SMALLER

/*
 * table
 */

#define KRB5_ADDRESS_ARANGE	(-100)

struct arange {
    krb5_address low;
    krb5_address high;
};

static int
arange_parse_addr (krb5_context context,
		   const char *address, krb5_address *addr)
{
    char buf[1024], *p;
    krb5_address low0, high0;
    struct arange *a;
    krb5_error_code ret;

    if(strncasecmp(address, "RANGE:", 6) != 0)
	return -1;

    address += 6;

    p = strrchr(address, '/');
    if (p) {
	krb5_addresses addrmask;
	char *q;
	long num;

	if (strlcpy(buf, address, sizeof(buf)) > sizeof(buf))
	    return -1;
	buf[p - address] = '\0';
	ret = krb5_parse_address(context, buf, &addrmask);
	if (ret)
	    return ret;
	if(addrmask.len != 1) {
	    krb5_free_addresses(context, &addrmask);
	    return -1;
	}

	address += p - address + 1;

	num = strtol(address, &q, 10);
	if (q == address || *q != '\0' || num < 0) {
	    krb5_free_addresses(context, &addrmask);
	    return -1;
	}

	ret = krb5_address_prefixlen_boundary(context, &addrmask.val[0], num,
					      &low0, &high0);
	krb5_free_addresses(context, &addrmask);
	if (ret)
	    return ret;

    } else {
	krb5_addresses low, high;

	strsep_copy(&address, "-", buf, sizeof(buf));
	ret = krb5_parse_address(context, buf, &low);
	if(ret)
	    return ret;
	if(low.len != 1) {
	    krb5_free_addresses(context, &low);
	    return -1;
	}

	strsep_copy(&address, "-", buf, sizeof(buf));
	ret = krb5_parse_address(context, buf, &high);
	if(ret) {
	    krb5_free_addresses(context, &low);
	    return ret;
	}

	if(high.len != 1 && high.val[0].addr_type != low.val[0].addr_type) {
	    krb5_free_addresses(context, &low);
	    krb5_free_addresses(context, &high);
	    return -1;
	}

	ret = krb5_copy_address(context, &high.val[0], &high0);
	if (ret == 0) {
	    ret = krb5_copy_address(context, &low.val[0], &low0);
	    if (ret)
		krb5_free_address(context, &high0);
	}
	krb5_free_addresses(context, &low);
	krb5_free_addresses(context, &high);
	if (ret)
	    return ret;
    }

    krb5_data_alloc(&addr->address, sizeof(*a));
    addr->addr_type = KRB5_ADDRESS_ARANGE;
    a = addr->address.data;

    if(krb5_address_order(context, &low0, &high0) < 0) {
	a->low = low0;
	a->high = high0;
    } else {
	a->low = high0;
	a->high = low0;
    }
    return 0;
}

static int
arange_free (krb5_context context, krb5_address *addr)
{
    struct arange *a;
    a = addr->address.data;
    krb5_free_address(context, &a->low);
    krb5_free_address(context, &a->high);
    krb5_data_free(&addr->address);
    return 0;
}


static int
arange_copy (krb5_context context, const krb5_address *inaddr,
	     krb5_address *outaddr)
{
    krb5_error_code ret;
    struct arange *i, *o;

    outaddr->addr_type = KRB5_ADDRESS_ARANGE;
    ret = krb5_data_alloc(&outaddr->address, sizeof(*o));
    if(ret)
	return ret;
    i = inaddr->address.data;
    o = outaddr->address.data;
    ret = krb5_copy_address(context, &i->low, &o->low);
    if(ret) {
	krb5_data_free(&outaddr->address);
	return ret;
    }
    ret = krb5_copy_address(context, &i->high, &o->high);
    if(ret) {
	krb5_free_address(context, &o->low);
	krb5_data_free(&outaddr->address);
	return ret;
    }
    return 0;
}

static int
arange_print_addr (const krb5_address *addr, char *str, size_t len)
{
    struct arange *a;
    krb5_error_code ret;
    size_t l, size, ret_len;

    a = addr->address.data;

    l = strlcpy(str, "RANGE:", len);
    ret_len = l;
    if (l > len)
	l = len;
    size = l;

    ret = krb5_print_address (&a->low, str + size, len - size, &l);
    if (ret)
	return ret;
    ret_len += l;
    if (len - size > l)
	size += l;
    else
	size = len;

    l = strlcat(str + size, "-", len - size);
    ret_len += l;
    if (len - size > l)
	size += l;
    else
	size = len;

    ret = krb5_print_address (&a->high, str + size, len - size, &l);
    if (ret)
	return ret;
    ret_len += l;

    return ret_len;
}

static int
arange_order_addr(krb5_context context,
		  const krb5_address *addr1,
		  const krb5_address *addr2)
{
    int tmp1, tmp2, sign;
    struct arange *a;
    const krb5_address *a2;

    if(addr1->addr_type == KRB5_ADDRESS_ARANGE) {
	a = addr1->address.data;
	a2 = addr2;
	sign = 1;
    } else if(addr2->addr_type == KRB5_ADDRESS_ARANGE) {
	a = addr2->address.data;
	a2 = addr1;
	sign = -1;
    } else {
	abort();
        UNREACHABLE(return 0);
    }

    if(a2->addr_type == KRB5_ADDRESS_ARANGE) {
	struct arange *b = a2->address.data;
	tmp1 = krb5_address_order(context, &a->low, &b->low);
	if(tmp1 != 0)
	    return sign * tmp1;
	return sign * krb5_address_order(context, &a->high, &b->high);
    } else if(a2->addr_type == a->low.addr_type) {
	tmp1 = krb5_address_order(context, &a->low, a2);
	if(tmp1 > 0)
	    return sign;
	tmp2 = krb5_address_order(context, &a->high, a2);
	if(tmp2 < 0)
	    return -sign;
	return 0;
    } else {
	return sign * (addr1->addr_type - addr2->addr_type);
    }
}

#endif /* HEIMDAL_SMALLER */

static int
addrport_print_addr (const krb5_address *addr, char *str, size_t len)
{
    krb5_error_code ret;
    krb5_address addr1, addr2;
    uint16_t port = 0;
    size_t ret_len = 0, l, size = 0;
    krb5_storage *sp;

    sp = krb5_storage_from_data((krb5_data*)rk_UNCONST(&addr->address));
    if (sp == NULL)
        return ENOMEM;

    /* for totally obscure reasons, these are not in network byteorder */
    krb5_storage_set_byteorder(sp, KRB5_STORAGE_BYTEORDER_LE);

    krb5_storage_seek(sp, 2, SEEK_CUR); /* skip first two bytes */
    krb5_ret_address(sp, &addr1);

    krb5_storage_seek(sp, 2, SEEK_CUR); /* skip two bytes */
    krb5_ret_address(sp, &addr2);
    krb5_storage_free(sp);
    if(addr2.addr_type == KRB5_ADDRESS_IPPORT && addr2.address.length == 2) {
	unsigned long value;
	_krb5_get_int(addr2.address.data, &value, 2);
	port = value;
    }
    l = strlcpy(str, "ADDRPORT:", len);
    ret_len += l;
    if (len > l)
	size += l;
    else
	size = len;

    ret = krb5_print_address(&addr1, str + size, len - size, &l);
    if (ret)
	return ret;
    ret_len += l;
    if (len - size > l)
	size += l;
    else
	size = len;

    ret = snprintf(str + size, len - size, ",PORT=%u", port);
    if (ret < 0)
	return EINVAL;
    ret_len += ret;
    return ret_len;
}

static struct addr_operations at[] = {
    {
	AF_INET,	KRB5_ADDRESS_INET, sizeof(struct sockaddr_in),
	ipv4_sockaddr2addr,
	ipv4_sockaddr2port,
	ipv4_addr2sockaddr,
	ipv4_h_addr2sockaddr,
	ipv4_h_addr2addr,
	ipv4_uninteresting,
	ipv4_is_loopback,
	ipv4_anyaddr,
	ipv4_print_addr,
	ipv4_parse_addr,
	NULL,
	NULL,
	NULL,
     ipv4_mask_boundary
    },
#ifdef HAVE_IPV6
    {
	AF_INET6,	KRB5_ADDRESS_INET6, sizeof(struct sockaddr_in6),
	ipv6_sockaddr2addr,
	ipv6_sockaddr2port,
	ipv6_addr2sockaddr,
	ipv6_h_addr2sockaddr,
	ipv6_h_addr2addr,
	ipv6_uninteresting,
	ipv6_is_loopback,
	ipv6_anyaddr,
	ipv6_print_addr,
	ipv6_parse_addr,
	NULL,
	NULL,
	NULL,
	ipv6_mask_boundary
    } ,
#endif
#ifndef HEIMDAL_SMALLER
    /* fake address type */
    {
	KRB5_ADDRESS_ARANGE, KRB5_ADDRESS_ARANGE, sizeof(struct arange),
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	arange_print_addr,
	arange_parse_addr,
	arange_order_addr,
	arange_free,
	arange_copy,
	NULL
    },
#endif
    {
	KRB5_ADDRESS_ADDRPORT, KRB5_ADDRESS_ADDRPORT, 0,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	NULL,
	addrport_print_addr,
	NULL,
	NULL,
	NULL,
	NULL
    }
};

static int num_addrs = sizeof(at) / sizeof(at[0]);

static size_t max_sockaddr_size = 0;

/*
 * generic functions
 */

static struct addr_operations *
find_af(int af)
{
    struct addr_operations *a;

    for (a = at; a < at + num_addrs; ++a)
	if (af == a->af)
	    return a;
    return NULL;
}

static struct addr_operations *
find_atype(krb5_address_type atype)
{
    struct addr_operations *a;

    for (a = at; a < at + num_addrs; ++a)
	if (atype == a->atype)
	    return a;
    return NULL;
}

/**
 * krb5_sockaddr2address stores a address a "struct sockaddr" sa in
 * the krb5_address addr.
 *
 * @param context a Keberos context
 * @param sa a struct sockaddr to extract the address from
 * @param addr an Kerberos 5 address to store the address in.
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_sockaddr2address (krb5_context context,
		       const struct sockaddr *sa, krb5_address *addr)
{
    struct addr_operations *a = find_af(sa->sa_family);
    if (a == NULL) {
	krb5_set_error_message (context, KRB5_PROG_ATYPE_NOSUPP,
				N_("Address family %d not supported", ""),
				sa->sa_family);
	return KRB5_PROG_ATYPE_NOSUPP;
    }
    return (*a->sockaddr2addr)(sa, addr);
}

/**
 * krb5_sockaddr2port extracts a port (if possible) from a "struct
 * sockaddr.
 *
 * @param context a Keberos context
 * @param sa a struct sockaddr to extract the port from
 * @param port a pointer to an int16_t store the port in.
 *
 * @return Return an error code or 0. Will return
 * KRB5_PROG_ATYPE_NOSUPP in case address type is not supported.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_sockaddr2port (krb5_context context,
		    const struct sockaddr *sa, int16_t *port)
{
    struct addr_operations *a = find_af(sa->sa_family);
    if (a == NULL) {
	krb5_set_error_message (context, KRB5_PROG_ATYPE_NOSUPP,
				N_("Address family %d not supported", ""),
				sa->sa_family);
	return KRB5_PROG_ATYPE_NOSUPP;
    }
    return (*a->sockaddr2port)(sa, port);
}

/**
 * krb5_addr2sockaddr sets the "struct sockaddr sockaddr" from addr
 * and port. The argument sa_size should initially contain the size of
 * the sa and after the call, it will contain the actual length of the
 * address. In case of the sa is too small to fit the whole address,
 * the up to *sa_size will be stored, and then *sa_size will be set to
 * the required length.
 *
 * @param context a Keberos context
 * @param addr the address to copy the from
 * @param sa the struct sockaddr that will be filled in
 * @param sa_size pointer to length of sa, and after the call, it will
 * contain the actual length of the address.
 * @param port set port in sa.
 *
 * @return Return an error code or 0. Will return
 * KRB5_PROG_ATYPE_NOSUPP in case address type is not supported.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_addr2sockaddr (krb5_context context,
		    const krb5_address *addr,
		    struct sockaddr *sa,
		    krb5_socklen_t *sa_size,
		    int port)
{
    struct addr_operations *a = find_atype(addr->addr_type);

    if (a == NULL) {
	krb5_set_error_message (context, KRB5_PROG_ATYPE_NOSUPP,
				N_("Address type %d not supported",
				   "krb5_address type"),
				addr->addr_type);
	return KRB5_PROG_ATYPE_NOSUPP;
    }
    if (a->addr2sockaddr == NULL) {
	krb5_set_error_message (context,
				KRB5_PROG_ATYPE_NOSUPP,
				N_("Can't convert address type %d to sockaddr", ""),
				addr->addr_type);
	return KRB5_PROG_ATYPE_NOSUPP;
    }
    (*a->addr2sockaddr)(addr, sa, sa_size, port);
    return 0;
}

/**
 * krb5_max_sockaddr_size returns the max size of the .Li struct
 * sockaddr that the Kerberos library will return.
 *
 * @return Return an size_t of the maximum struct sockaddr.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION size_t KRB5_LIB_CALL
krb5_max_sockaddr_size (void)
{
    if (max_sockaddr_size == 0) {
	struct addr_operations *a;

	for(a = at; a < at + num_addrs; ++a)
	    max_sockaddr_size = max(max_sockaddr_size, a->max_sockaddr_size);
    }
    return max_sockaddr_size;
}

/**
 * krb5_sockaddr_uninteresting returns TRUE for all .Fa sa that the
 * kerberos library thinks are uninteresting.  One example are link
 * local addresses.
 *
 * @param sa pointer to struct sockaddr that might be interesting.
 *
 * @return Return a non zero for uninteresting addresses.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_sockaddr_uninteresting(const struct sockaddr *sa)
{
    struct addr_operations *a = find_af(sa->sa_family);
    if (a == NULL || a->uninteresting == NULL)
	return TRUE;
    return (*a->uninteresting)(sa);
}

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_sockaddr_is_loopback(const struct sockaddr *sa)
{
    struct addr_operations *a = find_af(sa->sa_family);
    if (a == NULL || a->is_loopback == NULL)
	return TRUE;
    return (*a->is_loopback)(sa);
}

/**
 * krb5_h_addr2sockaddr initializes a "struct sockaddr sa" from af and
 * the "struct hostent" (see gethostbyname(3) ) h_addr_list
 * component. The argument sa_size should initially contain the size
 * of the sa, and after the call, it will contain the actual length of
 * the address.
 *
 * @param context a Keberos context
 * @param af addresses
 * @param addr address
 * @param sa returned struct sockaddr
 * @param sa_size size of sa
 * @param port port to set in sa.
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_h_addr2sockaddr (krb5_context context,
		      int af,
		      const char *addr, struct sockaddr *sa,
		      krb5_socklen_t *sa_size,
		      int port)
{
    struct addr_operations *a = find_af(af);
    if (a == NULL) {
	krb5_set_error_message (context, KRB5_PROG_ATYPE_NOSUPP,
				"Address family %d not supported", af);
	return KRB5_PROG_ATYPE_NOSUPP;
    }
    (*a->h_addr2sockaddr)(addr, sa, sa_size, port);
    return 0;
}

/**
 * krb5_h_addr2addr works like krb5_h_addr2sockaddr with the exception
 * that it operates on a krb5_address instead of a struct sockaddr.
 *
 * @param context a Keberos context
 * @param af address family
 * @param haddr host address from struct hostent.
 * @param addr returned krb5_address.
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_h_addr2addr (krb5_context context,
		  int af,
		  const char *haddr, krb5_address *addr)
{
    struct addr_operations *a = find_af(af);
    if (a == NULL) {
	krb5_set_error_message (context, KRB5_PROG_ATYPE_NOSUPP,
				N_("Address family %d not supported", ""), af);
	return KRB5_PROG_ATYPE_NOSUPP;
    }
    return (*a->h_addr2addr)(haddr, addr);
}

/**
 * krb5_anyaddr fills in a "struct sockaddr sa" that can be used to
 * bind(2) to.  The argument sa_size should initially contain the size
 * of the sa, and after the call, it will contain the actual length
 * of the address.
 *
 * @param context a Keberos context
 * @param af address family
 * @param sa sockaddr
 * @param sa_size lenght of sa.
 * @param port for to fill into sa.
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_anyaddr (krb5_context context,
	      int af,
	      struct sockaddr *sa,
	      krb5_socklen_t *sa_size,
	      int port)
{
    struct addr_operations *a = find_af (af);

    if (a == NULL) {
	krb5_set_error_message (context, KRB5_PROG_ATYPE_NOSUPP,
				N_("Address family %d not supported", ""), af);
	return KRB5_PROG_ATYPE_NOSUPP;
    }

    (*a->anyaddr)(sa, sa_size, port);
    return 0;
}

/**
 * krb5_print_address prints the address in addr to the string string
 * that have the length len. If ret_len is not NULL, it will be filled
 * with the length of the string if size were unlimited (not including
 * the final NUL) .
 *
 * @param addr address to be printed
 * @param str pointer string to print the address into
 * @param len length that will fit into area pointed to by "str".
 * @param ret_len return length the str.
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_print_address (const krb5_address *addr,
		    char *str, size_t len, size_t *ret_len)
{
    struct addr_operations *a = find_atype(addr->addr_type);
    int ret;

    if (a == NULL || a->print_addr == NULL) {
	char *s;
	int l;
	size_t i;

	s = str;
	l = snprintf(s, len, "TYPE_%d:", addr->addr_type);
	if (l < 0 || (size_t)l >= len)
	    return EINVAL;
	s += l;
	len -= l;
	for(i = 0; i < addr->address.length; i++) {
	    l = snprintf(s, len, "%02x", ((char*)addr->address.data)[i]);
	    if (l < 0 || (size_t)l >= len)
		return EINVAL;
	    len -= l;
	    s += l;
	}
	if(ret_len != NULL)
	    *ret_len = s - str;
	return 0;
    }
    ret = (*a->print_addr)(addr, str, len);
    if (ret < 0)
	return EINVAL;
    if(ret_len != NULL)
	*ret_len = ret;
    return 0;
}

/**
 * krb5_parse_address returns the resolved hostname in string to the
 * krb5_addresses addresses .
 *
 * @param context a Keberos context
 * @param string
 * @param addresses
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_parse_address(krb5_context context,
		   const char *string,
		   krb5_addresses *addresses)
{
    int i, n;
    struct addrinfo *ai, *a;
    int error;
    int save_errno;

    addresses->len = 0;
    addresses->val = NULL;

    for(i = 0; i < num_addrs; i++) {
	if(at[i].parse_addr) {
	    krb5_address addr;
	    if((*at[i].parse_addr)(context, string, &addr) == 0) {
		ALLOC_SEQ(addresses, 1);
		if (addresses->val == NULL) {
		    krb5_set_error_message(context, ENOMEM,
					   N_("malloc: out of memory", ""));
		    return ENOMEM;
		}
		addresses->val[0] = addr;
		return 0;
	    }
	}
    }

    error = getaddrinfo (string, NULL, NULL, &ai);
    if (error) {
	krb5_error_code ret2;
	save_errno = errno;
	ret2 = krb5_eai_to_heim_errno(error, save_errno);
	krb5_set_error_message (context, ret2, "%s: %s",
				string, gai_strerror(error));
	return ret2;
    }

    n = 0;
    for (a = ai; a != NULL; a = a->ai_next)
	++n;

    ALLOC_SEQ(addresses, n);
    if (addresses->val == NULL) {
	krb5_set_error_message(context, ENOMEM,
			       N_("malloc: out of memory", ""));
	freeaddrinfo(ai);
	return ENOMEM;
    }

    addresses->len = 0;
    for (a = ai, i = 0; a != NULL; a = a->ai_next) {
	if (krb5_sockaddr2address (context, ai->ai_addr, &addresses->val[i]))
	    continue;
	if(krb5_address_search(context, &addresses->val[i], addresses)) {
	    krb5_free_address(context, &addresses->val[i]);
	    continue;
	}
	i++;
	addresses->len = i;
    }
    freeaddrinfo (ai);
    return 0;
}

/**
 * krb5_address_order compares the addresses addr1 and addr2 so that
 * it can be used for sorting addresses. If the addresses are the same
 * address krb5_address_order will return 0. Behavies like memcmp(2).
 *
 * @param context a Keberos context
 * @param addr1 krb5_address to compare
 * @param addr2 krb5_address to compare
 *
 * @return < 0 if address addr1 in "less" then addr2. 0 if addr1 and
 * addr2 is the same address, > 0 if addr2 is "less" then addr1.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION int KRB5_LIB_CALL
krb5_address_order(krb5_context context,
		   const krb5_address *addr1,
		   const krb5_address *addr2)
{
    /* this sucks; what if both addresses have order functions, which
       should we call? this works for now, though */
    struct addr_operations *a;
    a = find_atype(addr1->addr_type);
    if(a == NULL) {
	krb5_set_error_message (context, KRB5_PROG_ATYPE_NOSUPP,
				N_("Address family %d not supported", ""),
				addr1->addr_type);
	return KRB5_PROG_ATYPE_NOSUPP;
    }
    if(a->order_addr != NULL)
	return (*a->order_addr)(context, addr1, addr2);
    a = find_atype(addr2->addr_type);
    if(a == NULL) {
	krb5_set_error_message (context, KRB5_PROG_ATYPE_NOSUPP,
				N_("Address family %d not supported", ""),
				addr2->addr_type);
	return KRB5_PROG_ATYPE_NOSUPP;
    }
    if(a->order_addr != NULL)
	return (*a->order_addr)(context, addr1, addr2);

    if(addr1->addr_type != addr2->addr_type)
	return addr1->addr_type - addr2->addr_type;
    if(addr1->address.length != addr2->address.length)
	return addr1->address.length - addr2->address.length;
    return memcmp (addr1->address.data,
		   addr2->address.data,
		   addr1->address.length);
}

/**
 * krb5_address_compare compares the addresses  addr1 and addr2.
 * Returns TRUE if the two addresses are the same.
 *
 * @param context a Keberos context
 * @param addr1 address to compare
 * @param addr2 address to compare
 *
 * @return Return an TRUE is the address are the same FALSE if not
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_address_compare(krb5_context context,
		     const krb5_address *addr1,
		     const krb5_address *addr2)
{
    return krb5_address_order (context, addr1, addr2) == 0;
}

/**
 * krb5_address_search checks if the address addr is a member of the
 * address set list addrlist .
 *
 * @param context a Keberos context.
 * @param addr address to search for.
 * @param addrlist list of addresses to look in for addr.
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION krb5_boolean KRB5_LIB_CALL
krb5_address_search(krb5_context context,
		    const krb5_address *addr,
		    const krb5_addresses *addrlist)
{
    size_t i;

    for (i = 0; i < addrlist->len; ++i)
	if (krb5_address_compare (context, addr, &addrlist->val[i]))
	    return TRUE;
    return FALSE;
}

/**
 * krb5_free_address frees the data stored in the address that is
 * alloced with any of the krb5_address functions.
 *
 * @param context a Keberos context
 * @param address addresss to be freed.
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_free_address(krb5_context context,
		  krb5_address *address)
{
    struct addr_operations *a = find_atype (address->addr_type);
    if(a != NULL && a->free_addr != NULL)
	return (*a->free_addr)(context, address);
    krb5_data_free (&address->address);
    memset(address, 0, sizeof(*address));
    return 0;
}

/**
 * krb5_free_addresses frees the data stored in the address that is
 * alloced with any of the krb5_address functions.
 *
 * @param context a Keberos context
 * @param addresses addressses to be freed.
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_free_addresses(krb5_context context,
		    krb5_addresses *addresses)
{
    size_t i;
    for(i = 0; i < addresses->len; i++)
	krb5_free_address(context, &addresses->val[i]);
    free(addresses->val);
    addresses->len = 0;
    addresses->val = NULL;
    return 0;
}

/**
 * krb5_copy_address copies the content of address
 * inaddr to outaddr.
 *
 * @param context a Keberos context
 * @param inaddr pointer to source address
 * @param outaddr pointer to destination address
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_address(krb5_context context,
		  const krb5_address *inaddr,
		  krb5_address *outaddr)
{
    struct addr_operations *a = find_af (inaddr->addr_type);
    if(a != NULL && a->copy_addr != NULL)
	return (*a->copy_addr)(context, inaddr, outaddr);
    return copy_HostAddress(inaddr, outaddr);
}

/**
 * krb5_copy_addresses copies the content of addresses
 * inaddr to outaddr.
 *
 * @param context a Keberos context
 * @param inaddr pointer to source addresses
 * @param outaddr pointer to destination addresses
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_copy_addresses(krb5_context context,
		    const krb5_addresses *inaddr,
		    krb5_addresses *outaddr)
{
    size_t i;
    ALLOC_SEQ(outaddr, inaddr->len);
    if(inaddr->len > 0 && outaddr->val == NULL)
	return ENOMEM;
    for(i = 0; i < inaddr->len; i++)
	krb5_copy_address(context, &inaddr->val[i], &outaddr->val[i]);
    return 0;
}

/**
 * krb5_append_addresses adds the set of addresses in source to
 * dest. While copying the addresses, duplicates are also sorted out.
 *
 * @param context a Keberos context
 * @param dest destination of copy operation
 * @param source adresses that are going to be added to dest
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_append_addresses(krb5_context context,
		      krb5_addresses *dest,
		      const krb5_addresses *source)
{
    krb5_address *tmp;
    krb5_error_code ret;
    size_t i;
    if(source->len > 0) {
	tmp = realloc(dest->val, (dest->len + source->len) * sizeof(*tmp));
	if(tmp == NULL) {
	    krb5_set_error_message (context, ENOMEM,
				    N_("malloc: out of memory", ""));
	    return ENOMEM;
	}
	dest->val = tmp;
	for(i = 0; i < source->len; i++) {
	    /* skip duplicates */
	    if(krb5_address_search(context, &source->val[i], dest))
		continue;
	    ret = krb5_copy_address(context,
				    &source->val[i],
				    &dest->val[dest->len]);
	    if(ret)
		return ret;
	    dest->len++;
	}
    }
    return 0;
}

/**
 * Create an address of type KRB5_ADDRESS_ADDRPORT from (addr, port)
 *
 * @param context a Keberos context
 * @param res built address from addr/port
 * @param addr address to use
 * @param port port to use
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_make_addrport (krb5_context context,
		    krb5_address **res, const krb5_address *addr, int16_t port)
{
    krb5_error_code ret;
    size_t len = addr->address.length + 2 + 4 * 4;
    u_char *p;

    *res = malloc (sizeof(**res));
    if (*res == NULL) {
	krb5_set_error_message (context, ENOMEM,
				N_("malloc: out of memory", ""));
	return ENOMEM;
    }
    (*res)->addr_type = KRB5_ADDRESS_ADDRPORT;
    ret = krb5_data_alloc (&(*res)->address, len);
    if (ret) {
	krb5_set_error_message (context, ret,
				N_("malloc: out of memory", ""));
	free (*res);
	*res = NULL;
	return ret;
    }
    p = (*res)->address.data;
    *p++ = 0;
    *p++ = 0;
    *p++ = (addr->addr_type     ) & 0xFF;
    *p++ = (addr->addr_type >> 8) & 0xFF;

    *p++ = (addr->address.length      ) & 0xFF;
    *p++ = (addr->address.length >>  8) & 0xFF;
    *p++ = (addr->address.length >> 16) & 0xFF;
    *p++ = (addr->address.length >> 24) & 0xFF;

    memcpy (p, addr->address.data, addr->address.length);
    p += addr->address.length;

    *p++ = 0;
    *p++ = 0;
    *p++ = (KRB5_ADDRESS_IPPORT     ) & 0xFF;
    *p++ = (KRB5_ADDRESS_IPPORT >> 8) & 0xFF;

    *p++ = (2      ) & 0xFF;
    *p++ = (2 >>  8) & 0xFF;
    *p++ = (2 >> 16) & 0xFF;
    *p++ = (2 >> 24) & 0xFF;

    memcpy (p, &port, 2);

    return 0;
}

/**
 * Calculate the boundary addresses of `inaddr'/`prefixlen' and store
 * them in `low' and `high'.
 *
 * @param context a Keberos context
 * @param inaddr address in prefixlen that the bondery searched
 * @param prefixlen width of boundery
 * @param low lowest address
 * @param high highest address
 *
 * @return Return an error code or 0.
 *
 * @ingroup krb5_address
 */

KRB5_LIB_FUNCTION krb5_error_code KRB5_LIB_CALL
krb5_address_prefixlen_boundary(krb5_context context,
				const krb5_address *inaddr,
				unsigned long prefixlen,
				krb5_address *low,
				krb5_address *high)
{
    struct addr_operations *a = find_atype (inaddr->addr_type);
    if(a != NULL && a->mask_boundary != NULL)
	return (*a->mask_boundary)(context, inaddr, prefixlen, low, high);
    krb5_set_error_message(context, KRB5_PROG_ATYPE_NOSUPP,
			   N_("Address family %d doesn't support "
			      "address mask operation", ""),
			   inaddr->addr_type);
    return KRB5_PROG_ATYPE_NOSUPP;
}
