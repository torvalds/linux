/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _LINUX_NEXTHOP_H
#define _LINUX_NEXTHOP_H

#include <linux/types.h>

struct nhmsg {
	unsigned char	nh_family;
	unsigned char	nh_scope;     /* return only */
	unsigned char	nh_protocol;  /* Routing protocol that installed nh */
	unsigned char	resvd;
	unsigned int	nh_flags;     /* RTNH_F flags */
};

/* entry in a nexthop group */
struct nexthop_grp {
	__u32	id;	  /* nexthop id - must exist */
	__u8	weight;   /* weight of this nexthop */
	__u8	resvd1;
	__u16	resvd2;
};

enum {
	NEXTHOP_GRP_TYPE_MPATH,  /* hash-threshold nexthop group
				  * default type if not specified
				  */
	NEXTHOP_GRP_TYPE_RES,    /* resilient nexthop group */
	__NEXTHOP_GRP_TYPE_MAX,
};

#define NEXTHOP_GRP_TYPE_MAX (__NEXTHOP_GRP_TYPE_MAX - 1)

enum {
	NHA_UNSPEC,
	NHA_ID,		/* u32; id for nexthop. id == 0 means auto-assign */

	NHA_GROUP,	/* array of nexthop_grp */
	NHA_GROUP_TYPE,	/* u16 one of NEXTHOP_GRP_TYPE */
	/* if NHA_GROUP attribute is added, no other attributes can be set */

	NHA_BLACKHOLE,	/* flag; nexthop used to blackhole packets */
	/* if NHA_BLACKHOLE is added, OIF, GATEWAY, ENCAP can not be set */

	NHA_OIF,	/* u32; nexthop device */
	NHA_GATEWAY,	/* be32 (IPv4) or in6_addr (IPv6) gw address */
	NHA_ENCAP_TYPE, /* u16; lwt encap type */
	NHA_ENCAP,	/* lwt encap data */

	/* NHA_OIF can be appended to dump request to return only
	 * nexthops using given device
	 */
	NHA_GROUPS,	/* flag; only return nexthop groups in dump */
	NHA_MASTER,	/* u32;  only return nexthops with given master dev */

	NHA_FDB,	/* flag; nexthop belongs to a bridge fdb */
	/* if NHA_FDB is added, OIF, BLACKHOLE, ENCAP cannot be set */

	/* nested; resilient nexthop group attributes */
	NHA_RES_GROUP,
	/* nested; nexthop bucket attributes */
	NHA_RES_BUCKET,

	__NHA_MAX,
};

#define NHA_MAX	(__NHA_MAX - 1)

enum {
	NHA_RES_GROUP_UNSPEC,
	/* Pad attribute for 64-bit alignment. */
	NHA_RES_GROUP_PAD = NHA_RES_GROUP_UNSPEC,

	/* u16; number of nexthop buckets in a resilient nexthop group */
	NHA_RES_GROUP_BUCKETS,
	/* clock_t as u32; nexthop bucket idle timer (per-group) */
	NHA_RES_GROUP_IDLE_TIMER,
	/* clock_t as u32; nexthop unbalanced timer */
	NHA_RES_GROUP_UNBALANCED_TIMER,
	/* clock_t as u64; nexthop unbalanced time */
	NHA_RES_GROUP_UNBALANCED_TIME,

	__NHA_RES_GROUP_MAX,
};

#define NHA_RES_GROUP_MAX	(__NHA_RES_GROUP_MAX - 1)

enum {
	NHA_RES_BUCKET_UNSPEC,
	/* Pad attribute for 64-bit alignment. */
	NHA_RES_BUCKET_PAD = NHA_RES_BUCKET_UNSPEC,

	/* u16; nexthop bucket index */
	NHA_RES_BUCKET_INDEX,
	/* clock_t as u64; nexthop bucket idle time */
	NHA_RES_BUCKET_IDLE_TIME,
	/* u32; nexthop id assigned to the nexthop bucket */
	NHA_RES_BUCKET_NH_ID,

	__NHA_RES_BUCKET_MAX,
};

#define NHA_RES_BUCKET_MAX	(__NHA_RES_BUCKET_MAX - 1)

#endif
