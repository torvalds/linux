/* vi: set sw=4 ts=4: */
/*
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 *
 * Authors: Alexey Kuznetsov, <kuznet@ms2.inr.ac.ru>
 */
#include <net/if.h>  /* struct ifreq and co. */

#include "libbb.h"
#include "libnetlink.h"
#include "ll_map.h"

struct idxmap {
	struct idxmap *next;
	int            index;
	int            type;
	int            alen;
	unsigned       flags;
	unsigned char  addr[8];
	char           name[16];
};

static struct idxmap **idxmap; /* treat as *idxmap[16] */

static struct idxmap *find_by_index(int idx)
{
	struct idxmap *im;

	if (idxmap)
		for (im = idxmap[idx & 0xF]; im; im = im->next)
			if (im->index == idx)
				return im;
	return NULL;
}

int FAST_FUNC ll_remember_index(const struct sockaddr_nl *who UNUSED_PARAM,
		struct nlmsghdr *n,
		void *arg UNUSED_PARAM)
{
	int h;
	struct ifinfomsg *ifi = NLMSG_DATA(n);
	struct idxmap *im, **imp;
	struct rtattr *tb[IFLA_MAX+1];

	if (n->nlmsg_type != RTM_NEWLINK)
		return 0;

	if (n->nlmsg_len < NLMSG_LENGTH(sizeof(ifi)))
		return -1;

	//memset(tb, 0, sizeof(tb)); - parse_rtattr does this
	parse_rtattr(tb, IFLA_MAX, IFLA_RTA(ifi), IFLA_PAYLOAD(n));
	if (tb[IFLA_IFNAME] == NULL)
		return 0;

	if (!idxmap)
		idxmap = xzalloc(sizeof(idxmap[0]) * 16);

	h = ifi->ifi_index & 0xF;
	for (imp = &idxmap[h]; (im = *imp) != NULL; imp = &im->next)
		if (im->index == ifi->ifi_index)
			goto found;

	im = xmalloc(sizeof(*im));
	im->next = *imp;
	im->index = ifi->ifi_index;
	*imp = im;
 found:
	im->type = ifi->ifi_type;
	im->flags = ifi->ifi_flags;
	if (tb[IFLA_ADDRESS]) {
		int alen;
		im->alen = alen = RTA_PAYLOAD(tb[IFLA_ADDRESS]);
		if (alen > (int)sizeof(im->addr))
			alen = sizeof(im->addr);
		memcpy(im->addr, RTA_DATA(tb[IFLA_ADDRESS]), alen);
	} else {
		im->alen = 0;
		memset(im->addr, 0, sizeof(im->addr));
	}
	strcpy(im->name, RTA_DATA(tb[IFLA_IFNAME]));
	return 0;
}

static
const char FAST_FUNC *ll_idx_n2a(int idx/*, char *buf*/)
{
	struct idxmap *im;

	if (idx == 0)
		return "*";
	im = find_by_index(idx);
	if (im)
		return im->name;
	//snprintf(buf, 16, "if%d", idx);
	//return buf;
	return auto_string(xasprintf("if%d", idx));
}

const char FAST_FUNC *ll_index_to_name(int idx)
{
	//static char nbuf[16];
	return ll_idx_n2a(idx/*, nbuf*/);
}

#ifdef UNUSED
int ll_index_to_type(int idx)
{
	struct idxmap *im;

	if (idx == 0)
		return -1;
	im = find_by_index(idx);
	if (im)
		return im->type;
	return -1;
}
#endif

unsigned FAST_FUNC ll_index_to_flags(int idx)
{
	struct idxmap *im;

	if (idx == 0)
		return 0;
	im = find_by_index(idx);
	if (im)
		return im->flags;
	return 0;
}

int FAST_FUNC xll_name_to_index(const char *name)
{
	int ret = 0;

/* caching is not warranted - no users which repeatedly call it */
#ifdef UNUSED
	static char ncache[16];
	static int icache;

	struct idxmap *im;
	int i;

	if (name == NULL)
		goto out;
	if (icache && strcmp(name, ncache) == 0) {
		ret = icache;
		goto out;
	}
	if (idxmap) {
		for (i = 0; i < 16; i++) {
			for (im = idxmap[i]; im; im = im->next) {
				if (strcmp(im->name, name) == 0) {
					icache = im->index;
					strcpy(ncache, name);
					ret = im->index;
					goto out;
				}
			}
		}
	}
#endif
	ret = if_nametoindex(name);
/* out:*/
	if (ret <= 0)
		bb_error_msg_and_die("can't find device '%s'", name);
	return ret;
}

int FAST_FUNC ll_init_map(struct rtnl_handle *rth)
{
	xrtnl_wilddump_request(rth, AF_UNSPEC, RTM_GETLINK);
	xrtnl_dump_filter(rth, ll_remember_index, NULL);
	return 0;
}
