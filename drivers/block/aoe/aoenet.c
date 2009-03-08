/* Copyright (c) 2007 Coraid, Inc.  See COPYING for GPL terms. */
/*
 * aoenet.c
 * Ethernet portion of AoE driver
 */

#include <linux/hdreg.h>
#include <linux/blkdev.h>
#include <linux/netdevice.h>
#include <linux/moduleparam.h>
#include <net/net_namespace.h>
#include <asm/unaligned.h>
#include "aoe.h"

#define NECODES 5

static char *aoe_errlist[] =
{
	"no such error",
	"unrecognized command code",
	"bad argument parameter",
	"device unavailable",
	"config string present",
	"unsupported version"
};

enum {
	IFLISTSZ = 1024,
};

static char aoe_iflist[IFLISTSZ];
module_param_string(aoe_iflist, aoe_iflist, IFLISTSZ, 0600);
MODULE_PARM_DESC(aoe_iflist, "aoe_iflist=\"dev1 [dev2 ...]\"");

#ifndef MODULE
static int __init aoe_iflist_setup(char *str)
{
	strncpy(aoe_iflist, str, IFLISTSZ);
	aoe_iflist[IFLISTSZ - 1] = '\0';
	return 1;
}

__setup("aoe_iflist=", aoe_iflist_setup);
#endif

int
is_aoe_netif(struct net_device *ifp)
{
	register char *p, *q;
	register int len;

	if (aoe_iflist[0] == '\0')
		return 1;

	p = aoe_iflist + strspn(aoe_iflist, WHITESPACE);
	for (; *p; p = q + strspn(q, WHITESPACE)) {
		q = p + strcspn(p, WHITESPACE);
		if (q != p)
			len = q - p;
		else
			len = strlen(p); /* last token in aoe_iflist */

		if (strlen(ifp->name) == len && !strncmp(ifp->name, p, len))
			return 1;
		if (q == p)
			break;
	}

	return 0;
}

int
set_aoe_iflist(const char __user *user_str, size_t size)
{
	if (size >= IFLISTSZ)
		return -EINVAL;

	if (copy_from_user(aoe_iflist, user_str, size)) {
		printk(KERN_INFO "aoe: copy from user failed\n");
		return -EFAULT;
	}
	aoe_iflist[size] = 0x00;
	return 0;
}

void
aoenet_xmit(struct sk_buff_head *queue)
{
	struct sk_buff *skb, *tmp;

	skb_queue_walk_safe(queue, skb, tmp) {
		__skb_unlink(skb, queue);
		dev_queue_xmit(skb);
	}
}

/* 
 * (1) len doesn't include the header by default.  I want this. 
 */
static int
aoenet_rcv(struct sk_buff *skb, struct net_device *ifp, struct packet_type *pt, struct net_device *orig_dev)
{
	struct aoe_hdr *h;
	u32 n;

	if (dev_net(ifp) != &init_net)
		goto exit;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (skb == NULL)
		return 0;
	if (skb_linearize(skb))
		goto exit;
	if (!is_aoe_netif(ifp))
		goto exit;
	skb_push(skb, ETH_HLEN);	/* (1) */

	h = (struct aoe_hdr *) skb_mac_header(skb);
	n = get_unaligned_be32(&h->tag);
	if ((h->verfl & AOEFL_RSP) == 0 || (n & 1<<31))
		goto exit;

	if (h->verfl & AOEFL_ERR) {
		n = h->err;
		if (n > NECODES)
			n = 0;
		if (net_ratelimit())
			printk(KERN_ERR
				"%s%d.%d@%s; ecode=%d '%s'\n",
				"aoe: error packet from ",
				get_unaligned_be16(&h->major),
				h->minor, skb->dev->name,
				h->err, aoe_errlist[n]);
		goto exit;
	}

	switch (h->cmd) {
	case AOECMD_ATA:
		aoecmd_ata_rsp(skb);
		break;
	case AOECMD_CFG:
		aoecmd_cfg_rsp(skb);
		break;
	default:
		if (h->cmd >= AOECMD_VEND_MIN)
			break;	/* don't complain about vendor commands */
		printk(KERN_INFO "aoe: unknown cmd %d\n", h->cmd);
	}
exit:
	dev_kfree_skb(skb);
	return 0;
}

static struct packet_type aoe_pt = {
	.type = __constant_htons(ETH_P_AOE),
	.func = aoenet_rcv,
};

int __init
aoenet_init(void)
{
	dev_add_pack(&aoe_pt);
	return 0;
}

void
aoenet_exit(void)
{
	dev_remove_pack(&aoe_pt);
}

