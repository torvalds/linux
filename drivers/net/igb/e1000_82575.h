/*******************************************************************************

  Intel(R) Gigabit Ethernet Linux driver
  Copyright(c) 2007-2009 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _E1000_82575_H_
#define _E1000_82575_H_

extern void igb_shutdown_serdes_link_82575(struct e1000_hw *hw);
extern void igb_power_up_serdes_link_82575(struct e1000_hw *hw);
extern void igb_power_down_phy_copper_82575(struct e1000_hw *hw);
extern void igb_rx_fifo_flush_82575(struct e1000_hw *hw);

#define ID_LED_DEFAULT_82575_SERDES ((ID_LED_DEF1_DEF2 << 12) | \
                                     (ID_LED_DEF1_DEF2 <<  8) | \
                                     (ID_LED_DEF1_DEF2 <<  4) | \
                                     (ID_LED_OFF1_ON2))

#define E1000_RAR_ENTRIES_82575   16
#define E1000_RAR_ENTRIES_82576   24
#define E1000_RAR_ENTRIES_82580   24

#define E1000_SW_SYNCH_MB              0x00000100
#define E1000_STAT_DEV_RST_SET         0x00100000
#define E1000_CTRL_DEV_RST             0x20000000

/* SRRCTL bit definitions */
#define E1000_SRRCTL_BSIZEPKT_SHIFT                     10 /* Shift _right_ */
#define E1000_SRRCTL_BSIZEHDRSIZE_SHIFT                 2  /* Shift _left_ */
#define E1000_SRRCTL_DESCTYPE_ADV_ONEBUF                0x02000000
#define E1000_SRRCTL_DESCTYPE_HDR_SPLIT_ALWAYS          0x0A000000
#define E1000_SRRCTL_DROP_EN                            0x80000000

#define E1000_MRQC_ENABLE_RSS_4Q            0x00000002
#define E1000_MRQC_ENABLE_VMDQ              0x00000003
#define E1000_MRQC_ENABLE_VMDQ_RSS_2Q       0x00000005
#define E1000_MRQC_RSS_FIELD_IPV4_UDP       0x00400000
#define E1000_MRQC_RSS_FIELD_IPV6_UDP       0x00800000
#define E1000_MRQC_RSS_FIELD_IPV6_UDP_EX    0x01000000

#define E1000_EICR_TX_QUEUE ( \
    E1000_EICR_TX_QUEUE0 |    \
    E1000_EICR_TX_QUEUE1 |    \
    E1000_EICR_TX_QUEUE2 |    \
    E1000_EICR_TX_QUEUE3)

#define E1000_EICR_RX_QUEUE ( \
    E1000_EICR_RX_QUEUE0 |    \
    E1000_EICR_RX_QUEUE1 |    \
    E1000_EICR_RX_QUEUE2 |    \
    E1000_EICR_RX_QUEUE3)

/* Immediate Interrupt Rx (A.K.A. Low Latency Interrupt) */
#define E1000_IMIREXT_SIZE_BP     0x00001000  /* Packet size bypass */
#define E1000_IMIREXT_CTRL_BP     0x00080000  /* Bypass check of ctrl bits */

/* Receive Descriptor - Advanced */
union e1000_adv_rx_desc {
	struct {
		__le64 pkt_addr;             /* Packet buffer address */
		__le64 hdr_addr;             /* Header buffer address */
	} read;
	struct {
		struct {
			struct {
				__le16 pkt_info;   /* RSS type, Packet type */
				__le16 hdr_info;   /* Split Header,
						    * header buffer length */
			} lo_dword;
			union {
				__le32 rss;          /* RSS Hash */
				struct {
					__le16 ip_id;    /* IP id */
					__le16 csum;     /* Packet Checksum */
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			__le32 status_error;     /* ext status/error */
			__le16 length;           /* Packet length */
			__le16 vlan;             /* VLAN tag */
		} upper;
	} wb;  /* writeback */
};

#define E1000_RXDADV_HDRBUFLEN_MASK      0x7FE0
#define E1000_RXDADV_HDRBUFLEN_SHIFT     5
#define E1000_RXDADV_STAT_TS             0x10000 /* Pkt was time stamped */

/* Transmit Descriptor - Advanced */
union e1000_adv_tx_desc {
	struct {
		__le64 buffer_addr;    /* Address of descriptor's data buf */
		__le32 cmd_type_len;
		__le32 olinfo_status;
	} read;
	struct {
		__le64 rsvd;       /* Reserved */
		__le32 nxtseq_seed;
		__le32 status;
	} wb;
};

/* Adv Transmit Descriptor Config Masks */
#define E1000_ADVTXD_MAC_TSTAMP   0x00080000 /* IEEE1588 Timestamp packet */
#define E1000_ADVTXD_DTYP_CTXT    0x00200000 /* Advanced Context Descriptor */
#define E1000_ADVTXD_DTYP_DATA    0x00300000 /* Advanced Data Descriptor */
#define E1000_ADVTXD_DCMD_IFCS    0x02000000 /* Insert FCS (Ethernet CRC) */
#define E1000_ADVTXD_DCMD_DEXT    0x20000000 /* Descriptor extension (1=Adv) */
#define E1000_ADVTXD_DCMD_VLE     0x40000000 /* VLAN pkt enable */
#define E1000_ADVTXD_DCMD_TSE     0x80000000 /* TCP Seg enable */
#define E1000_ADVTXD_PAYLEN_SHIFT    14 /* Adv desc PAYLEN shift */

/* Context descriptors */
struct e1000_adv_tx_context_desc {
	__le32 vlan_macip_lens;
	__le32 seqnum_seed;
	__le32 type_tucmd_mlhl;
	__le32 mss_l4len_idx;
};

#define E1000_ADVTXD_MACLEN_SHIFT    9  /* Adv ctxt desc mac len shift */
#define E1000_ADVTXD_TUCMD_IPV4    0x00000400  /* IP Packet Type: 1=IPv4 */
#define E1000_ADVTXD_TUCMD_L4T_TCP 0x00000800  /* L4 Packet TYPE of TCP */
#define E1000_ADVTXD_TUCMD_L4T_SCTP 0x00001000 /* L4 packet TYPE of SCTP */
/* IPSec Encrypt Enable for ESP */
#define E1000_ADVTXD_L4LEN_SHIFT     8  /* Adv ctxt L4LEN shift */
#define E1000_ADVTXD_MSS_SHIFT      16  /* Adv ctxt MSS shift */
/* Adv ctxt IPSec SA IDX mask */
/* Adv ctxt IPSec ESP len mask */

/* Additional Transmit Descriptor Control definitions */
#define E1000_TXDCTL_QUEUE_ENABLE  0x02000000 /* Enable specific Tx Queue */
/* Tx Queue Arbitration Priority 0=low, 1=high */

/* Additional Receive Descriptor Control definitions */
#define E1000_RXDCTL_QUEUE_ENABLE  0x02000000 /* Enable specific Rx Queue */

/* Direct Cache Access (DCA) definitions */
#define E1000_DCA_CTRL_DCA_MODE_DISABLE 0x01 /* DCA Disable */
#define E1000_DCA_CTRL_DCA_MODE_CB2     0x02 /* DCA Mode CB2 */

#define E1000_DCA_RXCTRL_CPUID_MASK 0x0000001F /* Rx CPUID Mask */
#define E1000_DCA_RXCTRL_DESC_DCA_EN (1 << 5) /* DCA Rx Desc enable */
#define E1000_DCA_RXCTRL_HEAD_DCA_EN (1 << 6) /* DCA Rx Desc header enable */
#define E1000_DCA_RXCTRL_DATA_DCA_EN (1 << 7) /* DCA Rx Desc payload enable */

#define E1000_DCA_TXCTRL_CPUID_MASK 0x0000001F /* Tx CPUID Mask */
#define E1000_DCA_TXCTRL_DESC_DCA_EN (1 << 5) /* DCA Tx Desc enable */
#define E1000_DCA_TXCTRL_TX_WB_RO_EN (1 << 11) /* Tx Desc writeback RO bit */

/* Additional DCA related definitions, note change in position of CPUID */
#define E1000_DCA_TXCTRL_CPUID_MASK_82576 0xFF000000 /* Tx CPUID Mask */
#define E1000_DCA_RXCTRL_CPUID_MASK_82576 0xFF000000 /* Rx CPUID Mask */
#define E1000_DCA_TXCTRL_CPUID_SHIFT 24 /* Tx CPUID now in the last byte */
#define E1000_DCA_RXCTRL_CPUID_SHIFT 24 /* Rx CPUID now in the last byte */

/* ETQF register bit definitions */
#define E1000_ETQF_FILTER_ENABLE   (1 << 26)
#define E1000_ETQF_1588            (1 << 30)

/* FTQF register bit definitions */
#define E1000_FTQF_VF_BP               0x00008000
#define E1000_FTQF_1588_TIME_STAMP     0x08000000
#define E1000_FTQF_MASK                0xF0000000
#define E1000_FTQF_MASK_PROTO_BP       0x10000000
#define E1000_FTQF_MASK_SOURCE_PORT_BP 0x80000000

#define E1000_NVM_APME_82575          0x0400
#define MAX_NUM_VFS                   8

#define E1000_DTXSWC_VMDQ_LOOPBACK_EN (1 << 31)  /* global VF LB enable */

/* Easy defines for setting default pool, would normally be left a zero */
#define E1000_VT_CTL_DEFAULT_POOL_SHIFT 7
#define E1000_VT_CTL_DEFAULT_POOL_MASK  (0x7 << E1000_VT_CTL_DEFAULT_POOL_SHIFT)

/* Other useful VMD_CTL register defines */
#define E1000_VT_CTL_IGNORE_MAC         (1 << 28)
#define E1000_VT_CTL_DISABLE_DEF_POOL   (1 << 29)
#define E1000_VT_CTL_VM_REPL_EN         (1 << 30)

/* Per VM Offload register setup */
#define E1000_VMOLR_RLPML_MASK 0x00003FFF /* Long Packet Maximum Length mask */
#define E1000_VMOLR_LPE        0x00010000 /* Accept Long packet */
#define E1000_VMOLR_RSSE       0x00020000 /* Enable RSS */
#define E1000_VMOLR_AUPE       0x01000000 /* Accept untagged packets */
#define E1000_VMOLR_ROMPE      0x02000000 /* Accept overflow multicast */
#define E1000_VMOLR_ROPE       0x04000000 /* Accept overflow unicast */
#define E1000_VMOLR_BAM        0x08000000 /* Accept Broadcast packets */
#define E1000_VMOLR_MPME       0x10000000 /* Multicast promiscuous mode */
#define E1000_VMOLR_STRVLAN    0x40000000 /* Vlan stripping enable */
#define E1000_VMOLR_STRCRC     0x80000000 /* CRC stripping enable */

#define E1000_VLVF_ARRAY_SIZE     32
#define E1000_VLVF_VLANID_MASK    0x00000FFF
#define E1000_VLVF_POOLSEL_SHIFT  12
#define E1000_VLVF_POOLSEL_MASK   (0xFF << E1000_VLVF_POOLSEL_SHIFT)
#define E1000_VLVF_LVLAN          0x00100000
#define E1000_VLVF_VLANID_ENABLE  0x80000000

#define E1000_VMVIR_VLANA_DEFAULT      0x40000000 /* Always use default VLAN */
#define E1000_VMVIR_VLANA_NEVER        0x80000000 /* Never insert VLAN tag */

#define E1000_IOVCTL 0x05BBC
#define E1000_IOVCTL_REUSE_VFQ 0x00000001

#define E1000_RPLOLR_STRVLAN   0x40000000
#define E1000_RPLOLR_STRCRC    0x80000000

#define E1000_DTXCTL_8023LL     0x0004
#define E1000_DTXCTL_VLAN_ADDED 0x0008
#define E1000_DTXCTL_OOS_ENABLE 0x0010
#define E1000_DTXCTL_MDP_EN     0x0020
#define E1000_DTXCTL_SPOOF_INT  0x0040

#define ALL_QUEUES   0xFFFF

/* RX packet buffer size defines */
#define E1000_RXPBS_SIZE_MASK_82576  0x0000007F
void igb_vmdq_set_loopback_pf(struct e1000_hw *, bool);
void igb_vmdq_set_replication_pf(struct e1000_hw *, bool);
u16 igb_rxpbs_adjust_82580(u32 data);

#endif
