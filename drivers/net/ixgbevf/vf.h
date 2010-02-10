/*******************************************************************************

  Intel 82599 Virtual Function driver
  Copyright(c) 1999 - 2009 Intel Corporation.

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

#ifndef __IXGBE_VF_H__
#define __IXGBE_VF_H__

#include <linux/pci.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/if_ether.h>

#include "defines.h"
#include "regs.h"
#include "mbx.h"

struct ixgbe_hw;

/* iterator type for walking multicast address lists */
typedef u8* (*ixgbe_mc_addr_itr) (struct ixgbe_hw *hw, u8 **mc_addr_ptr,
				  u32 *vmdq);
struct ixgbe_mac_operations {
	s32 (*init_hw)(struct ixgbe_hw *);
	s32 (*reset_hw)(struct ixgbe_hw *);
	s32 (*start_hw)(struct ixgbe_hw *);
	s32 (*clear_hw_cntrs)(struct ixgbe_hw *);
	enum ixgbe_media_type (*get_media_type)(struct ixgbe_hw *);
	u32 (*get_supported_physical_layer)(struct ixgbe_hw *);
	s32 (*get_mac_addr)(struct ixgbe_hw *, u8 *);
	s32 (*stop_adapter)(struct ixgbe_hw *);
	s32 (*get_bus_info)(struct ixgbe_hw *);

	/* Link */
	s32 (*setup_link)(struct ixgbe_hw *, ixgbe_link_speed, bool, bool);
	s32 (*check_link)(struct ixgbe_hw *, ixgbe_link_speed *, bool *, bool);
	s32 (*get_link_capabilities)(struct ixgbe_hw *, ixgbe_link_speed *,
				     bool *);

	/* RAR, Multicast, VLAN */
	s32 (*set_rar)(struct ixgbe_hw *, u32, u8 *, u32);
	s32 (*init_rx_addrs)(struct ixgbe_hw *);
	s32 (*update_mc_addr_list)(struct ixgbe_hw *, u8 *, u32,
				   ixgbe_mc_addr_itr);
	s32 (*enable_mc)(struct ixgbe_hw *);
	s32 (*disable_mc)(struct ixgbe_hw *);
	s32 (*clear_vfta)(struct ixgbe_hw *);
	s32 (*set_vfta)(struct ixgbe_hw *, u32, u32, bool);
};

enum ixgbe_mac_type {
	ixgbe_mac_unknown = 0,
	ixgbe_mac_82599_vf,
	ixgbe_num_macs
};

struct ixgbe_mac_info {
	struct ixgbe_mac_operations ops;
	u8 addr[6];
	u8 perm_addr[6];

	enum ixgbe_mac_type type;

	s32  mc_filter_type;

	bool get_link_status;
	u32  max_tx_queues;
	u32  max_rx_queues;
	u32  max_msix_vectors;
};

struct ixgbe_mbx_operations {
	s32 (*init_params)(struct ixgbe_hw *hw);
	s32 (*read)(struct ixgbe_hw *, u32 *, u16);
	s32 (*write)(struct ixgbe_hw *, u32 *, u16);
	s32 (*read_posted)(struct ixgbe_hw *, u32 *, u16);
	s32 (*write_posted)(struct ixgbe_hw *, u32 *, u16);
	s32 (*check_for_msg)(struct ixgbe_hw *);
	s32 (*check_for_ack)(struct ixgbe_hw *);
	s32 (*check_for_rst)(struct ixgbe_hw *);
};

struct ixgbe_mbx_stats {
	u32 msgs_tx;
	u32 msgs_rx;

	u32 acks;
	u32 reqs;
	u32 rsts;
};

struct ixgbe_mbx_info {
	struct ixgbe_mbx_operations ops;
	struct ixgbe_mbx_stats stats;
	u32 timeout;
	u32 udelay;
	u32 v2p_mailbox;
	u16 size;
};

struct ixgbe_hw {
	void *back;

	u8 __iomem *hw_addr;
	u8 *flash_address;
	unsigned long io_base;

	struct ixgbe_mac_info mac;
	struct ixgbe_mbx_info mbx;

	u16 device_id;
	u16 subsystem_vendor_id;
	u16 subsystem_device_id;
	u16 vendor_id;

	u8  revision_id;
	bool adapter_stopped;
};

struct ixgbevf_hw_stats {
	u64 base_vfgprc;
	u64 base_vfgptc;
	u64 base_vfgorc;
	u64 base_vfgotc;
	u64 base_vfmprc;

	u64 last_vfgprc;
	u64 last_vfgptc;
	u64 last_vfgorc;
	u64 last_vfgotc;
	u64 last_vfmprc;

	u64 vfgprc;
	u64 vfgptc;
	u64 vfgorc;
	u64 vfgotc;
	u64 vfmprc;
};

struct ixgbevf_info {
	enum ixgbe_mac_type		mac;
	struct ixgbe_mac_operations	*mac_ops;
};

#endif /* __IXGBE_VF_H__ */

