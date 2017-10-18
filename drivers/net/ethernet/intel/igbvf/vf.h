/*******************************************************************************

  Intel(R) 82576 Virtual Function Linux driver
  Copyright(c) 2009 - 2012 Intel Corporation.

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, see <http://www.gnu.org/licenses/>.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Contact Information:
  e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
  Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497

*******************************************************************************/

#ifndef _E1000_VF_H_
#define _E1000_VF_H_

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>

#include "regs.h"
#include "defines.h"

struct e1000_hw;

#define E1000_DEV_ID_82576_VF		0x10CA
#define E1000_DEV_ID_I350_VF		0x1520
#define E1000_REVISION_0	0
#define E1000_REVISION_1	1
#define E1000_REVISION_2	2
#define E1000_REVISION_3	3
#define E1000_REVISION_4	4

#define E1000_FUNC_0	0
#define E1000_FUNC_1	1

/* Receive Address Register Count
 * Number of high/low register pairs in the RAR.  The RAR (Receive Address
 * Registers) holds the directed and multicast addresses that we monitor.
 * These entries are also used for MAC-based filtering.
 */
#define E1000_RAR_ENTRIES_VF	1

/* Receive Descriptor - Advanced */
union e1000_adv_rx_desc {
	struct {
		u64 pkt_addr; /* Packet buffer address */
		u64 hdr_addr; /* Header buffer address */
	} read;
	struct {
		struct {
			union {
				u32 data;
				struct {
					u16 pkt_info; /* RSS/Packet type */
					/* Split Header, hdr buffer length */
					u16 hdr_info;
				} hs_rss;
			} lo_dword;
			union {
				u32 rss; /* RSS Hash */
				struct {
					u16 ip_id; /* IP id */
					u16 csum;  /* Packet Checksum */
				} csum_ip;
			} hi_dword;
		} lower;
		struct {
			u32 status_error; /* ext status/error */
			u16 length; /* Packet length */
			u16 vlan;   /* VLAN tag */
		} upper;
	} wb;  /* writeback */
};

#define E1000_RXDADV_HDRBUFLEN_MASK	0x7FE0
#define E1000_RXDADV_HDRBUFLEN_SHIFT	5

/* Transmit Descriptor - Advanced */
union e1000_adv_tx_desc {
	struct {
		u64 buffer_addr; /* Address of descriptor's data buf */
		u32 cmd_type_len;
		u32 olinfo_status;
	} read;
	struct {
		u64 rsvd; /* Reserved */
		u32 nxtseq_seed;
		u32 status;
	} wb;
};

/* Adv Transmit Descriptor Config Masks */
#define E1000_ADVTXD_DTYP_CTXT	0x00200000 /* Advanced Context Descriptor */
#define E1000_ADVTXD_DTYP_DATA	0x00300000 /* Advanced Data Descriptor */
#define E1000_ADVTXD_DCMD_EOP	0x01000000 /* End of Packet */
#define E1000_ADVTXD_DCMD_IFCS	0x02000000 /* Insert FCS (Ethernet CRC) */
#define E1000_ADVTXD_DCMD_RS	0x08000000 /* Report Status */
#define E1000_ADVTXD_DCMD_DEXT	0x20000000 /* Descriptor extension (1=Adv) */
#define E1000_ADVTXD_DCMD_VLE	0x40000000 /* VLAN pkt enable */
#define E1000_ADVTXD_DCMD_TSE	0x80000000 /* TCP Seg enable */
#define E1000_ADVTXD_PAYLEN_SHIFT	14 /* Adv desc PAYLEN shift */

/* Context descriptors */
struct e1000_adv_tx_context_desc {
	u32 vlan_macip_lens;
	u32 seqnum_seed;
	u32 type_tucmd_mlhl;
	u32 mss_l4len_idx;
};

#define E1000_ADVTXD_MACLEN_SHIFT	9  /* Adv ctxt desc mac len shift */
#define E1000_ADVTXD_TUCMD_IPV4		0x00000400 /* IP Packet Type: 1=IPv4 */
#define E1000_ADVTXD_TUCMD_L4T_TCP	0x00000800 /* L4 Packet TYPE of TCP */
#define E1000_ADVTXD_TUCMD_L4T_SCTP	0x00001000 /* L4 packet TYPE of SCTP */
#define E1000_ADVTXD_L4LEN_SHIFT	8  /* Adv ctxt L4LEN shift */
#define E1000_ADVTXD_MSS_SHIFT		16 /* Adv ctxt MSS shift */

enum e1000_mac_type {
	e1000_undefined = 0,
	e1000_vfadapt,
	e1000_vfadapt_i350,
	e1000_num_macs  /* List is 1-based, so subtract 1 for true count. */
};

struct e1000_vf_stats {
	u64 base_gprc;
	u64 base_gptc;
	u64 base_gorc;
	u64 base_gotc;
	u64 base_mprc;
	u64 base_gotlbc;
	u64 base_gptlbc;
	u64 base_gorlbc;
	u64 base_gprlbc;

	u32 last_gprc;
	u32 last_gptc;
	u32 last_gorc;
	u32 last_gotc;
	u32 last_mprc;
	u32 last_gotlbc;
	u32 last_gptlbc;
	u32 last_gorlbc;
	u32 last_gprlbc;

	u64 gprc;
	u64 gptc;
	u64 gorc;
	u64 gotc;
	u64 mprc;
	u64 gotlbc;
	u64 gptlbc;
	u64 gorlbc;
	u64 gprlbc;
};

#include "mbx.h"

struct e1000_mac_operations {
	/* Function pointers for the MAC. */
	s32  (*init_params)(struct e1000_hw *);
	s32  (*check_for_link)(struct e1000_hw *);
	void (*clear_vfta)(struct e1000_hw *);
	s32  (*get_bus_info)(struct e1000_hw *);
	s32  (*get_link_up_info)(struct e1000_hw *, u16 *, u16 *);
	void (*update_mc_addr_list)(struct e1000_hw *, u8 *, u32, u32, u32);
	s32  (*set_uc_addr)(struct e1000_hw *, u32, u8 *);
	s32  (*reset_hw)(struct e1000_hw *);
	s32  (*init_hw)(struct e1000_hw *);
	s32  (*setup_link)(struct e1000_hw *);
	void (*write_vfta)(struct e1000_hw *, u32, u32);
	void (*mta_set)(struct e1000_hw *, u32);
	void (*rar_set)(struct e1000_hw *, u8*, u32);
	s32  (*read_mac_addr)(struct e1000_hw *);
	s32  (*set_vfta)(struct e1000_hw *, u16, bool);
};

struct e1000_mac_info {
	struct e1000_mac_operations ops;
	u8 addr[6];
	u8 perm_addr[6];

	enum e1000_mac_type type;

	u16 mta_reg_count;
	u16 rar_entry_count;

	bool get_link_status;
};

struct e1000_mbx_operations {
	s32 (*init_params)(struct e1000_hw *hw);
	s32 (*read)(struct e1000_hw *, u32 *, u16);
	s32 (*write)(struct e1000_hw *, u32 *, u16);
	s32 (*read_posted)(struct e1000_hw *, u32 *, u16);
	s32 (*write_posted)(struct e1000_hw *, u32 *, u16);
	s32 (*check_for_msg)(struct e1000_hw *);
	s32 (*check_for_ack)(struct e1000_hw *);
	s32 (*check_for_rst)(struct e1000_hw *);
};

struct e1000_mbx_stats {
	u32 msgs_tx;
	u32 msgs_rx;

	u32 acks;
	u32 reqs;
	u32 rsts;
};

struct e1000_mbx_info {
	struct e1000_mbx_operations ops;
	struct e1000_mbx_stats stats;
	u32 timeout;
	u32 usec_delay;
	u16 size;
};

struct e1000_dev_spec_vf {
	u32 vf_number;
	u32 v2p_mailbox;
};

struct e1000_hw {
	void *back;

	u8 __iomem *hw_addr;
	u8 __iomem *flash_address;
	unsigned long io_base;

	struct e1000_mac_info  mac;
	struct e1000_mbx_info mbx;

	union {
		struct e1000_dev_spec_vf vf;
	} dev_spec;

	u16 device_id;
	u16 subsystem_vendor_id;
	u16 subsystem_device_id;
	u16 vendor_id;

	u8  revision_id;
};

/* These functions must be implemented by drivers */
void e1000_rlpml_set_vf(struct e1000_hw *, u16);
void e1000_init_function_pointers_vf(struct e1000_hw *hw);

#endif /* _E1000_VF_H_ */
