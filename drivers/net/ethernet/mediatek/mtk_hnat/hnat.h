/*   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   Copyright (C) 2014-2016 Sean Wang <sean.wang@mediatek.com>
 *   Copyright (C) 2016-2017 John Crispin <blogic@openwrt.org>
 */

#include <linux/debugfs.h>
#include <linux/string.h>
#include <linux/if.h>
#include <linux/if_ether.h>
#include <net/net_namespace.h>
/*--------------------------------------------------------------------------*/
/* Register Offset*/
/*--------------------------------------------------------------------------*/
#define PPE_GLO_CFG		0x00
#define PPE_FLOW_CFG		0x04
#define PPE_IP_PROT_CHK		0x08
#define PPE_IP_PROT_0		0x0C
#define PPE_IP_PROT_1		0x10
#define PPE_IP_PROT_2		0x14
#define PPE_IP_PROT_3		0x18
#define PPE_TB_CFG		0x1C
#define PPE_TB_BASE		0x20
#define PPE_TB_USED		0x24
#define PPE_BNDR		0x28
#define PPE_BIND_LMT_0		0x2C
#define PPE_BIND_LMT_1		0x30
#define PPE_KA			0x34
#define PPE_UNB_AGE		0x38
#define PPE_BND_AGE_0		0x3C
#define PPE_BND_AGE_1		0x40
#define PPE_HASH_SEED		0x44
#define PPE_DFT_CPORT		0x48
#define PPE_MCAST_PPSE		0x84
#define PPE_MCAST_L_0		0x88
#define PPE_MCAST_H_0		0x8C
#define PPE_MCAST_L_1		0x90
#define PPE_MCAST_H_1		0x94
#define PPE_MCAST_L_2		0x98
#define PPE_MCAST_H_2		0x9C
#define PPE_MCAST_L_3		0xA0
#define PPE_MCAST_H_3		0xA4
#define PPE_MCAST_L_4		0xA8
#define PPE_MCAST_H_4		0xAC
#define PPE_MCAST_L_5		0xB0
#define PPE_MCAST_H_5		0xB4
#define PPE_MCAST_L_6		0xBC
#define PPE_MCAST_H_6		0xC0
#define PPE_MCAST_L_7		0xC4
#define PPE_MCAST_H_7		0xC8
#define PPE_MCAST_L_8		0xCC
#define PPE_MCAST_H_8		0xD0
#define PPE_MCAST_L_9		0xD4
#define PPE_MCAST_H_9		0xD8
#define PPE_MCAST_L_A		0xDC
#define PPE_MCAST_H_A		0xE0
#define PPE_MCAST_L_B		0xE4
#define PPE_MCAST_H_B		0xE8
#define PPE_MCAST_L_C		0xEC
#define PPE_MCAST_H_C		0xF0
#define PPE_MCAST_L_D		0xF4
#define PPE_MCAST_H_D		0xF8
#define PPE_MCAST_L_E		0xFC
#define PPE_MCAST_H_E		0xE0
#define PPE_MCAST_L_F		0x100
#define PPE_MCAST_H_F		0x104
#define PPE_MTU_DRP		0x108
#define PPE_MTU_VLYR_0		0x10C
#define PPE_MTU_VLYR_1		0x110
#define PPE_MTU_VLYR_2		0x114
#define PPE_VPM_TPID		0x118
#define PPE_CAH_CTRL		0x120
#define PPE_CAH_TAG_SRH		0x124
#define PPE_CAH_LINE_RW		0x128
#define PPE_CAH_WDATA		0x12C
#define PPE_CAH_RDATA		0x130

#define GDMA1_FWD_CFG		0x500
#define GDMA2_FWD_CFG		0x1500
/*--------------------------------------------------------------------------*/
/* Register Mask*/
/*--------------------------------------------------------------------------*/
/* PPE_TB_CFG mask */
#define TB_ETRY_NUM		(0x7 << 0)	/* RW */
#define TB_ENTRY_SIZE		(0x1 << 3)	/* RW */
#define SMA			(0x3 << 4)		/* RW */
#define NTU_AGE			(0x1 << 7)	/* RW */
#define UNBD_AGE		(0x1 << 8)	/* RW */
#define TCP_AGE			(0x1 << 9)	/* RW */
#define UDP_AGE			(0x1 << 10)	/* RW */
#define FIN_AGE			(0x1 << 11)	/* RW */
#define KA_CFG			(0x3<< 12)
#define HASH_MODE		(0x3 << 14)	/* RW */
#define XMODE			(0x3 << 18)	/* RW */

/*PPE_CAH_CTRL mask*/
#define CAH_EN			(0x1 << 0)	/* RW */
#define CAH_X_MODE		(0x1 << 9)	/* RW */

/*PPE_UNB_AGE mask*/
#define UNB_DLTA		(0xff << 0)	/* RW */
#define UNB_MNP			(0xffff << 16)	/* RW */

/*PPE_BND_AGE_0 mask*/
#define UDP_DLTA		(0xffff << 0)	/* RW */
#define NTU_DLTA		(0xffff << 16)	/* RW */

/*PPE_BND_AGE_1 mask*/
#define TCP_DLTA		(0xffff << 0)	/* RW */
#define FIN_DLTA		(0xffff << 16)	/* RW */

/*PPE_KA mask*/
#define KA_T			(0xffff << 0)	/* RW */
#define TCP_KA			(0xff << 16)	/* RW */
#define UDP_KA			(0xff << 24)	/* RW */

/*PPE_BIND_LMT_0 mask*/
#define QURT_LMT		(0x3ff << 0)	/* RW */
#define HALF_LMT		(0x3ff << 16)	/* RW */

/*PPE_BIND_LMT_1 mask*/
#define FULL_LMT		(0x3fff << 0)	/* RW */
#define NTU_KA			(0xff << 16)	/* RW */

/*PPE_BNDR mask*/
#define BIND_RATE		(0xffff << 0)	/* RW */
#define PBND_RD_PRD		(0xffff << 16)	/* RW */

/*PPE_GLO_CFG mask*/
#define PPE_EN			(0x1 << 0)	/* RW */
#define TTL0_DRP		(0x1 << 4)	/* RW */

/*GDMA1_FWD_CFG mask */
#define GDM1_UFRC_MASK		(0x7 << 12)	/* RW */
#define GDM1_BFRC_MASK		(0x7 << 8) /*RW*/
#define GDM1_MFRC_MASK		(0x7 << 4) /*RW*/
#define GDM1_OFRC_MASK		(0x7 << 0) /*RW*/
#define GDM1_ALL_FRC_MASK	(GDM1_UFRC_MASK | GDM1_BFRC_MASK | GDM1_MFRC_MASK | GDM1_OFRC_MASK)

#define GDM2_UFRC_MASK		(0x7 << 12)	/* RW */
#define GDM2_BFRC_MASK		(0x7 << 8) /*RW*/
#define GDM2_MFRC_MASK		(0x7 << 4) /*RW*/
#define GDM2_OFRC_MASK		(0x7 << 0) /*RW*/
#define GDM2_ALL_FRC_MASK	(GDM2_UFRC_MASK | GDM2_BFRC_MASK | GDM2_MFRC_MASK | GDM2_OFRC_MASK)

/*--------------------------------------------------------------------------*/
/* Descriptor Structure */
/*--------------------------------------------------------------------------*/
#define HNAT_SKB_CB(__skb) ((struct hnat_skb_cb *)&((__skb)->cb[40]))
struct hnat_skb_cb {
	__u16 iif;
};

struct hnat_unbind_info_blk {
	u32 time_stamp:8;
	u32 pcnt:16;		/* packet count */
	u32 preb:1;
	u32 pkt_type:3;
	u32 state:2;
	u32 udp:1;
	u32 sta:1;		/* static entry */
} __attribute__ ((packed));

struct hnat_bind_info_blk {
	u32 time_stamp:15;
	u32 ka:1;		/* keep alive */
	u32 vlan_layer:3;
	u32 psn:1;		/* egress packet has PPPoE session */
	u32 vpm:1;		/* 0:ethertype remark, 1:0x8100(CR default) */
	u32 ps:1;		/* packet sampling */
	u32 cah:1;		/* cacheable flag */
	u32 rmt:1;		/* remove tunnel ip header (6rd/dslite only) */
	u32 ttl:1;
	u32 pkt_type:3;
	u32 state:2;
	u32 udp:1;
	u32 sta:1;		/* static entry */
} __attribute__ ((packed));

struct hnat_info_blk2 {
	u32 qid:4;		/* QID in Qos Port */
	u32 fqos:1;		/* force to PSE QoS port */
	u32 dp:3;		/* force to PSE port x 
				 0:PSE,1:GSW, 2:GMAC,4:PPE,5:QDMA,7=DROP */
	u32 mcast:1;		/* multicast this packet to CPU */
	u32 pcpl:1;		/* OSBN */
	u32 mlen:1;		/* 0:post 1:pre packet length in meter */
	u32 alen:1;		/* 0:post 1:pre packet length in accounting */
	u32 port_mg:6;		/* port meter group */
	u32 port_ag:6;		/* port account group */
	u32 dscp:8;		/* DSCP value */
} __attribute__ ((packed));

struct hnat_ipv4_hnapt {
	union {
		struct hnat_bind_info_blk bfib1;
		struct hnat_unbind_info_blk udib1;
		u32 info_blk1;
	};
	u32 sip;
	u32 dip;
	u16 dport;
	u16 sport;
	union {
		struct hnat_info_blk2 iblk2;
		u32 info_blk2;
	};
	u32 new_sip;
	u32 new_dip;
	u16 new_dport;
	u16 new_sport;
	u32 resv1;
	u32 resv2;
	u32 resv3:26;
	u32 act_dp:6;		/* UDF */
	u16 vlan1;
	u16 etype;
	u32 dmac_hi;
	u16 vlan2;
	u16 dmac_lo;
	u32 smac_hi;
	u16 pppoe_id;
	u16 smac_lo;
} __attribute__ ((packed));

struct foe_entry {
	union {
		struct hnat_unbind_info_blk udib1;
		struct hnat_bind_info_blk bfib1;
		struct hnat_ipv4_hnapt ipv4_hnapt;
	};
};

#define HNAT_AC_BYTE_LO(x)		(0x2000 + (x * 16))
#define HNAT_AC_BYTE_HI(x)		(0x2004 + (x * 16))
#define HNAT_AC_PACKET(x)		(0x2008 + (x * 16))
#define HNAT_COUNTER_MAX		64
#define HNAT_AC_TIMER_INTERVAL		(HZ)

struct hnat_accounting {
	u64 bytes;
	u64 packets;
};

struct hnat_priv {
	struct device *dev;
	void __iomem *fe_base;
	void __iomem *ppe_base;
	struct foe_entry *foe_table_cpu;
	dma_addr_t foe_table_dev;
	u8 enable;
	u8 enable1;
	struct dentry *root;
	struct debugfs_regset32 *regset;

	struct timer_list ac_timer;
	struct hnat_accounting acct[HNAT_COUNTER_MAX];

	/*devices we plays for*/
	char wan[IFNAMSIZ];

	struct reset_control	*rstc;
};

enum FoeEntryState {
	INVALID = 0,
	UNBIND = 1,
	BIND = 2,
	FIN = 3
};
/*--------------------------------------------------------------------------*/
/* Common Definition*/
/*--------------------------------------------------------------------------*/

#define FOE_4TB_SIZ		4096
#define HASH_SEED_KEY		0x12345678

/*PPE_TB_CFG value*/
#define ENTRY_80B		1
#define ENTRY_64B		0
#define TABLE_1K		0
#define TABLE_2K		1
#define TABLE_4K		2
#define TABLE_8K		3
#define TABLE_16K		4
#define SMA_DROP		0		/* Drop the packet */
#define SMA_DROP2		1		/* Drop the packet */
#define SMA_ONLY_FWD_CPU	2	/* Only Forward to CPU */
#define SMA_FWD_CPU_BUILD_ENTRY	3	/* Forward to CPU and build new FOE entry */
#define HASH_MODE_0		0
#define HASH_MODE_1		1
#define HASH_MODE_2		2
#define HASH_MODE_3		3

/*PPE_FLOW_CFG*/
#define BIT_FUC_FOE		BIT(2)
#define BIT_FMC_FOE		BIT(1)
#define BIT_FBC_FOE		BIT(0)
#define BIT_IPV4_NAT_EN		BIT(12)
#define BIT_IPV4_NAPT_EN	BIT(13)
#define BIT_IPV4_NAT_FRAG_EN	BIT(17)
#define BIT_IPV4_HASH_GREK	BIT(19)

/*GDMA1_FWD_CFG value */
#define BITS_GDM1_UFRC_P_PPE	(NR_PPE_PORT << 12)
#define BITS_GDM1_BFRC_P_PPE	(NR_PPE_PORT << 8)
#define BITS_GDM1_MFRC_P_PPE	(NR_PPE_PORT << 4)
#define BITS_GDM1_OFRC_P_PPE	(NR_PPE_PORT << 0)
#define BITS_GDM1_ALL_FRC_P_PPE	(BITS_GDM1_UFRC_P_PPE | BITS_GDM1_BFRC_P_PPE | BITS_GDM1_MFRC_P_PPE | BITS_GDM1_OFRC_P_PPE)

#define BITS_GDM1_UFRC_P_CPU_PDMA	(NR_PDMA_PORT << 12)
#define BITS_GDM1_BFRC_P_CPU_PDMA	(NR_PDMA_PORT << 8)
#define BITS_GDM1_MFRC_P_CPU_PDMA	(NR_PDMA_PORT << 4)
#define BITS_GDM1_OFRC_P_CPU_PDMA	(NR_PDMA_PORT << 0)
#define BITS_GDM1_ALL_FRC_P_CPU_PDMA	(BITS_GDM1_UFRC_P_CPU_PDMA | BITS_GDM1_BFRC_P_CPU_PDMA | BITS_GDM1_MFRC_P_CPU_PDMA | BITS_GDM1_OFRC_P_CPU_PDMA)

#define BITS_GDM1_UFRC_P_CPU_QDMA	(NR_QDMA_PORT << 12)
#define BITS_GDM1_BFRC_P_CPU_QDMA	(NR_QDMA_PORT << 8)
#define BITS_GDM1_MFRC_P_CPU_QDMA	(NR_QDMA_PORT << 4)
#define BITS_GDM1_OFRC_P_CPU_QDMA	(NR_QDMA_PORT << 0)
#define BITS_GDM1_ALL_FRC_P_CPU_QDMA	(BITS_GDM1_UFRC_P_CPU_QDMA | BITS_GDM1_BFRC_P_CPU_QDMA | BITS_GDM1_MFRC_P_CPU_QDMA | BITS_GDM1_OFRC_P_CPU_QDMA)

#define BITS_GDM1_UFRC_P_DISCARD	(NR_DISCARD << 12)
#define BITS_GDM1_BFRC_P_DISCARD	(NR_DISCARD << 8)
#define BITS_GDM1_MFRC_P_DISCARD	(NR_DISCARD << 4)
#define BITS_GDM1_OFRC_P_DISCARD	(NR_DISCARD << 0)
#define BITS_GDM1_ALL_FRC_P_DISCARD	(BITS_GDM1_UFRC_P_DISCARD | BITS_GDM1_BFRC_P_DISCARD | BITS_GDM1_MFRC_P_DISCARD | BITS_GDM1_OFRC_P_DISCARD)

#define BITS_GDM2_UFRC_P_PPE	(NR_PPE_PORT << 12)
#define BITS_GDM2_BFRC_P_PPE	(NR_PPE_PORT << 8)
#define BITS_GDM2_MFRC_P_PPE	(NR_PPE_PORT << 4)
#define BITS_GDM2_OFRC_P_PPE	(NR_PPE_PORT << 0)
#define BITS_GDM2_ALL_FRC_P_PPE	(BITS_GDM2_UFRC_P_PPE | BITS_GDM2_BFRC_P_PPE | BITS_GDM2_MFRC_P_PPE | BITS_GDM2_OFRC_P_PPE)

#define BITS_GDM2_UFRC_P_CPU_PDMA	(NR_PDMA_PORT << 12)
#define BITS_GDM2_BFRC_P_CPU_PDMA	(NR_PDMA_PORT << 8)
#define BITS_GDM2_MFRC_P_CPU_PDMA	(NR_PDMA_PORT << 4)
#define BITS_GDM2_OFRC_P_CPU_PDMA	(NR_PDMA_PORT << 0)
#define BITS_GDM2_ALL_FRC_P_CPU_PDMA	(BITS_GDM2_UFRC_P_CPU_PDMA | BITS_GDM2_BFRC_P_CPU_PDMA | BITS_GDM2_MFRC_P_CPU_PDMA | BITS_GDM2_OFRC_P_CPU_PDMA)

#define BITS_GDM2_UFRC_P_CPU_QDMA	(NR_QDMA_PORT << 12)
#define BITS_GDM2_BFRC_P_CPU_QDMA	(NR_QDMA_PORT << 8)
#define BITS_GDM2_MFRC_P_CPU_QDMA	(NR_QDMA_PORT << 4)
#define BITS_GDM2_OFRC_P_CPU_QDMA	(NR_QDMA_PORT << 0)
#define BITS_GDM2_ALL_FRC_P_CPU_QDMA	(BITS_GDM2_UFRC_P_CPU_QDMA | BITS_GDM2_BFRC_P_CPU_QDMA | BITS_GDM2_MFRC_P_CPU_QDMA | BITS_GDM2_OFRC_P_CPU_QDMA)

#define BITS_GDM2_UFRC_P_DISCARD	(NR_DISCARD << 12)
#define BITS_GDM2_BFRC_P_DISCARD	(NR_DISCARD << 8)
#define BITS_GDM2_MFRC_P_DISCARD	(NR_DISCARD << 4)
#define BITS_GDM2_OFRC_P_DISCARD	(NR_DISCARD << 0)
#define BITS_GDM2_ALL_FRC_P_DISCARD	(BITS_GDM2_UFRC_P_DISCARD | BITS_GDM2_BFRC_P_DISCARD | BITS_GDM2_MFRC_P_DISCARD | BITS_GDM2_OFRC_P_DISCARD)

#define hnat_is_enabled(host)	(host->enable)
#define hnat_enabled(host)	(host->enable = 1)
#define hnat_disabled(host)	(host->enable = 0)
#define hnat_is_enabled1(host)	(host->enable1)
#define hnat_enabled1(host)	(host->enable1 = 1)
#define hnat_disabled1(host)	(host->enable1 = 0)

#define entry_hnat_is_bound(e)	(e->bfib1.state == BIND)
#define entry_hnat_state(e)	(e->bfib1.state)

#define skb_hnat_is_hashed(skb)	(skb_hnat_entry(skb)!=0x3fff && skb_hnat_entry(skb)< FOE_4TB_SIZ)
#define FROM_GE_LAN(skb)	(HNAT_SKB_CB(skb)->iif == FOE_MAGIC_GE_LAN)
#define FROM_GE_WAN(skb)	(HNAT_SKB_CB(skb)->iif == FOE_MAGIC_GE_WAN)
#define FROM_GE_PPD(skb)	(HNAT_SKB_CB(skb)->iif == FOE_MAGIC_GE_PPD)
#define FOE_MAGIC_GE_WAN	0x7273
#define FOE_MAGIC_GE_LAN	0x7272
#define FOE_INVALID		0xffff

#define TCP_FIN_SYN_RST		0x0C /* Ingress packet is TCP fin/syn/rst (for IPv4 NAPT/DS-Lite or IPv6 5T-route/6RD) */
#define UN_HIT			0x0D/* FOE Un-hit */
#define HIT_UNBIND		0x0E/* FOE Hit unbind */
#define HIT_UNBIND_RATE_REACH	0xf
#define HNAT_HIT_BIND_OLD_DUP_HDR	0x15
#define HNAT_HIT_BIND_FORCE_TO_CPU	0x16

#define HIT_BIND_KEEPALIVE_MC_NEW_HDR	0x14
#define HIT_BIND_KEEPALIVE_DUP_OLD_HDR	0x15
#define IPV4_HNAPT			0
#define IPV4_HNAT			1
#define IP_FORMAT(addr) \
		((unsigned char *)&addr)[3], \
		((unsigned char *)&addr)[2], \
		((unsigned char *)&addr)[1], \
		((unsigned char *)&addr)[0]

/*PSE Ports*/
#define NR_PDMA_PORT		0
#define NR_GMAC1_PORT		1
#define NR_GMAC2_PORT		2
#define NR_PPE_PORT		4
#define NR_QDMA_PORT		5
#define NR_DISCARD		7
#define IS_LAN(dev)		(!strncmp(dev->name, "lan", 3))
#define IS_WAN(dev)		(!strcmp(dev->name, host->wan))
#define IS_BR(dev)		(!strncmp(dev->name, "br", 2))
#define IS_IPV4_HNAPT(x)	(((x)->bfib1.pkt_type == IPV4_HNAPT) ? 1: 0)
#define IS_IPV4_HNAT(x)		(((x)->bfib1.pkt_type == IPV4_HNAT) ? 1 : 0)
#define IS_IPV4_GRP(x)		(IS_IPV4_HNAPT(x) | IS_IPV4_HNAT(x))

#define es(entry)		(entry_state[entry->bfib1.state])
#define ei(entry, end)		(FOE_4TB_SIZ - (int)(end - entry))
#define pt(entry)		(packet_type[entry->ipv4_hnapt.bfib1.pkt_type])
#define ipv4_smac(mac,e)	({mac[0]=e->ipv4_hnapt.smac_hi[3]; mac[1]=e->ipv4_hnapt.smac_hi[2];\
				 mac[2]=e->ipv4_hnapt.smac_hi[1]; mac[3]=e->ipv4_hnapt.smac_hi[0];\
				 mac[4]=e->ipv4_hnapt.smac_lo[1]; mac[5]=e->ipv4_hnapt.smac_lo[0];})
#define ipv4_dmac(mac,e)	({mac[0]=e->ipv4_hnapt.dmac_hi[3]; mac[1]=e->ipv4_hnapt.dmac_hi[2];\
				 mac[2]=e->ipv4_hnapt.dmac_hi[1]; mac[3]=e->ipv4_hnapt.dmac_hi[0];\
				 mac[4]=e->ipv4_hnapt.dmac_lo[1]; mac[5]=e->ipv4_hnapt.dmac_lo[0];})

extern struct hnat_priv *host;

extern void hnat_deinit_debugfs(struct hnat_priv *h);
extern int __init hnat_init_debugfs(struct hnat_priv *h);
//extern int hnat_register_nf_hooks(void);
//extern void hnat_unregister_nf_hooks(void);
extern int hnat_register_nf_hooks(struct net *net);
extern void hnat_unregister_nf_hooks(struct net *net);

