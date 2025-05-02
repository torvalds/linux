/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) Meta Platforms, Inc. and affiliates. */

#ifndef _FBNIC_RPC_H_
#define _FBNIC_RPC_H_

#include <uapi/linux/in6.h>
#include <linux/bitfield.h>

struct in_addr;

/*  The TCAM state definitions follow an expected ordering.
 *  They start out disabled, then move through the following states:
 *  Disabled  0	-> Add	      2
 *  Add	      2	-> Valid      1
 *
 *  Valid     1	-> Add/Update 2
 *  Add	      2	-> Valid      1
 *
 *  Valid     1	-> Delete     3
 *  Delete    3	-> Disabled   0
 */
enum {
	FBNIC_TCAM_S_DISABLED	= 0,
	FBNIC_TCAM_S_VALID	= 1,
	FBNIC_TCAM_S_ADD	= 2,
	FBNIC_TCAM_S_UPDATE	= FBNIC_TCAM_S_ADD,
	FBNIC_TCAM_S_DELETE	= 3,
};

/* 32 MAC Destination Address TCAM Entries
 * 4 registers DA[1:0], DA[3:2], DA[5:4], Validate
 */
#define FBNIC_RPC_TCAM_MACDA_WORD_LEN		3
#define FBNIC_RPC_TCAM_MACDA_NUM_ENTRIES	32

/* 8 IPSRC and IPDST TCAM Entries each
 * 8 registers, Validate each
 */
#define FBNIC_RPC_TCAM_IP_ADDR_WORD_LEN		8
#define FBNIC_RPC_TCAM_IP_ADDR_NUM_ENTRIES	8

#define FBNIC_RPC_TCAM_ACT_WORD_LEN		11
#define FBNIC_RPC_TCAM_ACT_NUM_ENTRIES		64

#define FBNIC_TCE_TCAM_WORD_LEN			3
#define FBNIC_TCE_TCAM_NUM_ENTRIES		8

struct fbnic_mac_addr {
	union {
		unsigned char addr8[ETH_ALEN];
		__be16 addr16[FBNIC_RPC_TCAM_MACDA_WORD_LEN];
	} mask, value;
	unsigned char state;
	DECLARE_BITMAP(act_tcam, FBNIC_RPC_TCAM_ACT_NUM_ENTRIES);
};

struct fbnic_ip_addr {
	struct in6_addr mask, value;
	unsigned char version;
	unsigned char state;
	DECLARE_BITMAP(act_tcam, FBNIC_RPC_TCAM_ACT_NUM_ENTRIES);
};

struct fbnic_act_tcam {
	struct {
		u16 tcam[FBNIC_RPC_TCAM_ACT_WORD_LEN];
	} mask, value;
	unsigned char state;
	u16 rss_en_mask;
	u32 dest;
};

enum {
	FBNIC_RSS_EN_HOST_UDP6,
	FBNIC_RSS_EN_HOST_UDP4,
	FBNIC_RSS_EN_HOST_TCP6,
	FBNIC_RSS_EN_HOST_TCP4,
	FBNIC_RSS_EN_HOST_IP6,
	FBNIC_RSS_EN_HOST_IP4,
	FBNIC_RSS_EN_HOST_ETHER,
	FBNIC_RSS_EN_XCAST_UDP6,
#define FBNIC_RSS_EN_NUM_UNICAST FBNIC_RSS_EN_XCAST_UDP6
	FBNIC_RSS_EN_XCAST_UDP4,
	FBNIC_RSS_EN_XCAST_TCP6,
	FBNIC_RSS_EN_XCAST_TCP4,
	FBNIC_RSS_EN_XCAST_IP6,
	FBNIC_RSS_EN_XCAST_IP4,
	FBNIC_RSS_EN_XCAST_ETHER,
	FBNIC_RSS_EN_NUM_ENTRIES
};

/* Reserve the first 2 entries for the use by the BMC so that we can
 * avoid allowing rules to get in the way of BMC unicast traffic.
 */
#define FBNIC_RPC_ACT_TBL_BMC_OFFSET		0
#define FBNIC_RPC_ACT_TBL_BMC_ALL_MULTI_OFFSET	1

/* This should leave us with 48 total entries in the TCAM that can be used
 * for NFC after also deducting the 14 needed for RSS table programming.
 */
#define FBNIC_RPC_ACT_TBL_NFC_OFFSET		2

/* We reserve the last 14 entries for RSS rules on the host. The BMC
 * unicast rule will need to be populated above these and is expected to
 * use MACDA TCAM entry 23 to store the BMC MAC address.
 */
#define FBNIC_RPC_ACT_TBL_RSS_OFFSET \
	(FBNIC_RPC_ACT_TBL_NUM_ENTRIES - FBNIC_RSS_EN_NUM_ENTRIES)

#define FBNIC_RPC_ACT_TBL_NFC_ENTRIES \
	(FBNIC_RPC_ACT_TBL_RSS_OFFSET - FBNIC_RPC_ACT_TBL_NFC_OFFSET)

/* Flags used to identify the owner for this MAC filter. Note that any
 * flags set for Broadcast thru Promisc indicate that the rule belongs
 * to the RSS filters for the host.
 */
enum {
	FBNIC_MAC_ADDR_T_BMC            = 0,
	FBNIC_MAC_ADDR_T_BROADCAST	= FBNIC_RPC_ACT_TBL_RSS_OFFSET,
#define FBNIC_MAC_ADDR_T_HOST_START	FBNIC_MAC_ADDR_T_BROADCAST
	FBNIC_MAC_ADDR_T_MULTICAST,
	FBNIC_MAC_ADDR_T_UNICAST,
	FBNIC_MAC_ADDR_T_ALLMULTI,	/* BROADCAST ... MULTICAST*/
	FBNIC_MAC_ADDR_T_PROMISC,	/* BROADCAST ... UNICAST */
	FBNIC_MAC_ADDR_T_HOST_LAST
};

#define FBNIC_MAC_ADDR_T_HOST_LEN \
	(FBNIC_MAC_ADDR_T_HOST_LAST - FBNIC_MAC_ADDR_T_HOST_START)

#define FBNIC_RPC_TCAM_ACT0_IPSRC_IDX		CSR_GENMASK(2, 0)
#define FBNIC_RPC_TCAM_ACT0_IPSRC_VALID		CSR_BIT(3)
#define FBNIC_RPC_TCAM_ACT0_IPDST_IDX		CSR_GENMASK(6, 4)
#define FBNIC_RPC_TCAM_ACT0_IPDST_VALID		CSR_BIT(7)
#define FBNIC_RPC_TCAM_ACT0_OUTER_IPSRC_IDX	CSR_GENMASK(10, 8)
#define FBNIC_RPC_TCAM_ACT0_OUTER_IPSRC_VALID	CSR_BIT(11)
#define FBNIC_RPC_TCAM_ACT0_OUTER_IPDST_IDX	CSR_GENMASK(14, 12)
#define FBNIC_RPC_TCAM_ACT0_OUTER_IPDST_VALID	CSR_BIT(15)

#define FBNIC_RPC_TCAM_ACT1_L2_MACDA_IDX	CSR_GENMASK(9, 5)
#define FBNIC_RPC_TCAM_ACT1_L2_MACDA_VALID	CSR_BIT(10)
#define FBNIC_RPC_TCAM_ACT1_IP_IS_V6		CSR_BIT(11)
#define FBNIC_RPC_TCAM_ACT1_IP_VALID		CSR_BIT(12)
#define FBNIC_RPC_TCAM_ACT1_OUTER_IP_VALID	CSR_BIT(13)
#define FBNIC_RPC_TCAM_ACT1_L4_IS_UDP		CSR_BIT(14)
#define FBNIC_RPC_TCAM_ACT1_L4_VALID		CSR_BIT(15)

/* TCAM 0 - 3 reserved for BMC MAC addresses */
#define FBNIC_RPC_TCAM_MACDA_BMC_ADDR_IDX	0
/* TCAM 4 reserved for broadcast MAC address */
#define FBNIC_RPC_TCAM_MACDA_BROADCAST_IDX	4
/* TCAMs 5 - 30 will be used for multicast and unicast addresses. The
 * boundary between the two can be variable it is currently set to 24
 * on which the unicast addresses start. The general idea is that we will
 * always go top-down with unicast, and bottom-up with multicast so that
 * there should be free-space in the middle between the two.
 *
 * The entry at MADCA_DEFAULT_BOUNDARY is a special case as it can be used
 * for the ALL MULTI address if the list is full, or the BMC has requested
 * it.
 */
#define FBNIC_RPC_TCAM_MACDA_MULTICAST_IDX	5
#define FBNIC_RPC_TCAM_MACDA_DEFAULT_BOUNDARY	24
#define FBNIC_RPC_TCAM_MACDA_HOST_ADDR_IDX	30
/* Reserved for use to record Multicast promisc, or Promiscuous */
#define FBNIC_RPC_TCAM_MACDA_PROMISC_IDX	31

enum {
	FBNIC_UDP6_HASH_OPT,
	FBNIC_UDP4_HASH_OPT,
	FBNIC_TCP6_HASH_OPT,
	FBNIC_TCP4_HASH_OPT,
#define FBNIC_L4_HASH_OPT FBNIC_TCP4_HASH_OPT
	FBNIC_IPV6_HASH_OPT,
	FBNIC_IPV4_HASH_OPT,
#define FBNIC_IP_HASH_OPT FBNIC_IPV4_HASH_OPT
	FBNIC_ETHER_HASH_OPT,
	FBNIC_NUM_HASH_OPT,
};

struct fbnic_dev;
struct fbnic_net;

void fbnic_bmc_rpc_init(struct fbnic_dev *fbd);
void fbnic_bmc_rpc_all_multi_config(struct fbnic_dev *fbd, bool enable_host);

void fbnic_reset_indir_tbl(struct fbnic_net *fbn);
void fbnic_rss_key_fill(u32 *buffer);
void fbnic_rss_init_en_mask(struct fbnic_net *fbn);
void fbnic_rss_disable_hw(struct fbnic_dev *fbd);
void fbnic_rss_reinit_hw(struct fbnic_dev *fbd, struct fbnic_net *fbn);
void fbnic_rss_reinit(struct fbnic_dev *fbd, struct fbnic_net *fbn);
u16 fbnic_flow_hash_2_rss_en_mask(struct fbnic_net *fbn, int flow_type);

int __fbnic_xc_unsync(struct fbnic_mac_addr *mac_addr, unsigned int tcam_idx);
struct fbnic_mac_addr *__fbnic_uc_sync(struct fbnic_dev *fbd,
				       const unsigned char *addr);
struct fbnic_mac_addr *__fbnic_mc_sync(struct fbnic_dev *fbd,
				       const unsigned char *addr);
void fbnic_sift_macda(struct fbnic_dev *fbd);
void fbnic_write_macda(struct fbnic_dev *fbd);

struct fbnic_ip_addr *__fbnic_ip4_sync(struct fbnic_dev *fbd,
				       struct fbnic_ip_addr *ip_addr,
				       const struct in_addr *addr,
				       const struct in_addr *mask);
struct fbnic_ip_addr *__fbnic_ip6_sync(struct fbnic_dev *fbd,
				       struct fbnic_ip_addr *ip_addr,
				       const struct in6_addr *addr,
				       const struct in6_addr *mask);
int __fbnic_ip_unsync(struct fbnic_ip_addr *ip_addr, unsigned int tcam_idx);
void fbnic_write_ip_addr(struct fbnic_dev *fbd);

static inline int __fbnic_uc_unsync(struct fbnic_mac_addr *mac_addr)
{
	return __fbnic_xc_unsync(mac_addr, FBNIC_MAC_ADDR_T_UNICAST);
}

static inline int __fbnic_mc_unsync(struct fbnic_mac_addr *mac_addr)
{
	return __fbnic_xc_unsync(mac_addr, FBNIC_MAC_ADDR_T_MULTICAST);
}

void fbnic_clear_rules(struct fbnic_dev *fbd);
void fbnic_write_rules(struct fbnic_dev *fbd);
void fbnic_write_tce_tcam(struct fbnic_dev *fbd);
#endif /* _FBNIC_RPC_H_ */
