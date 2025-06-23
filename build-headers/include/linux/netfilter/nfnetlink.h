/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _NFNETLINK_H
#define _NFNETLINK_H
#include <linux/types.h>
#include <linux/netfilter/nfnetlink_compat.h>

enum nfnetlink_groups {
	NFNLGRP_NONE,
#define NFNLGRP_NONE			NFNLGRP_NONE
	NFNLGRP_CONNTRACK_NEW,
#define NFNLGRP_CONNTRACK_NEW		NFNLGRP_CONNTRACK_NEW
	NFNLGRP_CONNTRACK_UPDATE,
#define NFNLGRP_CONNTRACK_UPDATE	NFNLGRP_CONNTRACK_UPDATE
	NFNLGRP_CONNTRACK_DESTROY,
#define NFNLGRP_CONNTRACK_DESTROY	NFNLGRP_CONNTRACK_DESTROY
	NFNLGRP_CONNTRACK_EXP_NEW,
#define	NFNLGRP_CONNTRACK_EXP_NEW	NFNLGRP_CONNTRACK_EXP_NEW
	NFNLGRP_CONNTRACK_EXP_UPDATE,
#define NFNLGRP_CONNTRACK_EXP_UPDATE	NFNLGRP_CONNTRACK_EXP_UPDATE
	NFNLGRP_CONNTRACK_EXP_DESTROY,
#define NFNLGRP_CONNTRACK_EXP_DESTROY	NFNLGRP_CONNTRACK_EXP_DESTROY
	NFNLGRP_NFTABLES,
#define NFNLGRP_NFTABLES                NFNLGRP_NFTABLES
	NFNLGRP_ACCT_QUOTA,
#define NFNLGRP_ACCT_QUOTA		NFNLGRP_ACCT_QUOTA
	NFNLGRP_NFTRACE,
#define NFNLGRP_NFTRACE			NFNLGRP_NFTRACE
	__NFNLGRP_MAX,
};
#define NFNLGRP_MAX	(__NFNLGRP_MAX - 1)

/* General form of address family dependent message.
 */
struct nfgenmsg {
	__u8  nfgen_family;		/* AF_xxx */
	__u8  version;		/* nfnetlink version */
	__be16    res_id;		/* resource id */
};

#define NFNETLINK_V0	0

/* netfilter netlink message types are split in two pieces:
 * 8 bit subsystem, 8bit operation.
 */

#define NFNL_SUBSYS_ID(x)	((x & 0xff00) >> 8)
#define NFNL_MSG_TYPE(x)	(x & 0x00ff)

/* No enum here, otherwise __stringify() trick of MODULE_ALIAS_NFNL_SUBSYS()
 * won't work anymore */
#define NFNL_SUBSYS_NONE 		0
#define NFNL_SUBSYS_CTNETLINK		1
#define NFNL_SUBSYS_CTNETLINK_EXP	2
#define NFNL_SUBSYS_QUEUE		3
#define NFNL_SUBSYS_ULOG		4
#define NFNL_SUBSYS_OSF			5
#define NFNL_SUBSYS_IPSET		6
#define NFNL_SUBSYS_ACCT		7
#define NFNL_SUBSYS_CTNETLINK_TIMEOUT	8
#define NFNL_SUBSYS_CTHELPER		9
#define NFNL_SUBSYS_NFTABLES		10
#define NFNL_SUBSYS_NFT_COMPAT		11
#define NFNL_SUBSYS_HOOK		12
#define NFNL_SUBSYS_COUNT		13

/* Reserved control nfnetlink messages */
#define NFNL_MSG_BATCH_BEGIN		NLMSG_MIN_TYPE
#define NFNL_MSG_BATCH_END		NLMSG_MIN_TYPE+1

/**
 * enum nfnl_batch_attributes - nfnetlink batch netlink attributes
 *
 * @NFNL_BATCH_GENID: generation ID for this changeset (NLA_U32)
 */
enum nfnl_batch_attributes {
        NFNL_BATCH_UNSPEC,
        NFNL_BATCH_GENID,
        __NFNL_BATCH_MAX
};
#define NFNL_BATCH_MAX			(__NFNL_BATCH_MAX - 1)

#endif /* _NFNETLINK_H */
