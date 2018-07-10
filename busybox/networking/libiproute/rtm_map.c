/* vi: set sw=4 ts=4: */
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Authors: Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */
#include "libbb.h"
#include "rt_names.h"
#include "utils.h"

const char* FAST_FUNC rtnl_rtntype_n2a(int id)
{
	switch (id) {
	case RTN_UNSPEC:
		return "none";
	case RTN_UNICAST:
		return "unicast";
	case RTN_LOCAL:
		return "local";
	case RTN_BROADCAST:
		return "broadcast";
	case RTN_ANYCAST:
		return "anycast";
	case RTN_MULTICAST:
		return "multicast";
	case RTN_BLACKHOLE:
		return "blackhole";
	case RTN_UNREACHABLE:
		return "unreachable";
	case RTN_PROHIBIT:
		return "prohibit";
	case RTN_THROW:
		return "throw";
	case RTN_NAT:
		return "nat";
	case RTN_XRESOLVE:
		return "xresolve";
	default:
		return itoa(id);
	}
}

int FAST_FUNC rtnl_rtntype_a2n(int *id, char *arg)
{
	static const char keywords[] ALIGN1 =
		"local\0""nat\0""broadcast\0""brd\0""anycast\0"
		"multicast\0""prohibit\0""unreachable\0""blackhole\0"
		"xresolve\0""unicast\0""throw\0";
	enum {
		ARG_local = 1, ARG_nat, ARG_broadcast, ARG_brd, ARG_anycast,
		ARG_multicast, ARG_prohibit, ARG_unreachable, ARG_blackhole,
		ARG_xresolve, ARG_unicast, ARG_throw
	};
	const smalluint key = index_in_substrings(keywords, arg) + 1;
	char *end;
	unsigned long res;

	if (key == ARG_local)
		res = RTN_LOCAL;
	else if (key == ARG_nat)
		res = RTN_NAT;
	else if (key == ARG_broadcast || key == ARG_brd)
		res = RTN_BROADCAST;
	else if (key == ARG_anycast)
		res = RTN_ANYCAST;
	else if (key == ARG_multicast)
		res = RTN_MULTICAST;
	else if (key == ARG_prohibit)
		res = RTN_PROHIBIT;
	else if (key == ARG_unreachable)
		res = RTN_UNREACHABLE;
	else if (key == ARG_blackhole)
		res = RTN_BLACKHOLE;
	else if (key == ARG_xresolve)
		res = RTN_XRESOLVE;
	else if (key == ARG_unicast)
		res = RTN_UNICAST;
	else if (key == ARG_throw)
		res = RTN_THROW;
	else {
		res = strtoul(arg, &end, 0);
		if (end == arg || *end || res > 255)
			return -1;
	}
	*id = res;
	return 0;
}

int FAST_FUNC get_rt_realms(uint32_t *realms, char *arg)
{
	uint32_t realm = 0;
	char *p = strchr(arg, '/');

	*realms = 0;
	if (p) {
		*p = 0;
		if (rtnl_rtrealm_a2n(realms, arg)) {
			*p = '/';
			return -1;
		}
		*realms <<= 16;
		*p = '/';
		arg = p+1;
	}
	if (*arg && rtnl_rtrealm_a2n(&realm, arg))
		return -1;
	*realms |= realm;
	return 0;
}
