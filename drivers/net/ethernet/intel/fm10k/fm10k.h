/* Intel Ethernet Switch Host Interface Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 */

#ifndef _FM10K_H_
#define _FM10K_H_

#include <linux/types.h>
#include <linux/etherdevice.h>
#include <linux/rtnetlink.h>
#include <linux/if_vlan.h>
#include <linux/pci.h>

#include "fm10k_pf.h"

#define FM10K_MAX_JUMBO_FRAME_SIZE	15358	/* Maximum supported size 15K */

enum fm10k_ring_f_enum {
	RING_F_RSS,
	RING_F_QOS,
	RING_F_ARRAY_SIZE  /* must be last in enum set */
};

struct fm10k_ring_feature {
	u16 limit;	/* upper limit on feature indices */
	u16 indices;	/* current value of indices */
	u16 mask;	/* Mask used for feature to ring mapping */
	u16 offset;	/* offset to start of feature */
};

#define fm10k_vxlan_port_for_each(vp, intfc) \
	list_for_each_entry(vp, &(intfc)->vxlan_port, list)
struct fm10k_vxlan_port {
	struct list_head	list;
	sa_family_t		sa_family;
	__be16			port;
};

struct fm10k_intfc {
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];
	struct net_device *netdev;
	struct pci_dev *pdev;
	unsigned long state;

	u32 flags;
#define FM10K_FLAG_RESET_REQUESTED		(u32)(1 << 0)
#define FM10K_FLAG_RSS_FIELD_IPV4_UDP		(u32)(1 << 1)
#define FM10K_FLAG_RSS_FIELD_IPV6_UDP		(u32)(1 << 2)
#define FM10K_FLAG_RX_TS_ENABLED		(u32)(1 << 3)
#define FM10K_FLAG_SWPRI_CONFIG			(u32)(1 << 4)
	int xcast_mode;

	u64 rx_overrun_pf;
	u64 rx_overrun_vf;

	struct fm10k_ring_feature ring_feature[RING_F_ARRAY_SIZE];

	struct fm10k_hw_stats stats;
	struct fm10k_hw hw;
	u32 __iomem *uc_addr;
	u16 msg_enable;

	u32 reta[FM10K_RETA_SIZE];
	u32 rssrk[FM10K_RSSRK_SIZE];

	/* VXLAN port tracking information */
	struct list_head vxlan_port;

#if defined(HAVE_DCBNL_IEEE) && defined(CONFIG_DCB)
	u8 pfc_en;
#endif
	u8 rx_pause;

	/* GLORT resources in use by PF */
	u16 glort;
	u16 glort_count;

	/* VLAN ID for updating multicast/unicast lists */
	u16 vid;
};

enum fm10k_state_t {
	__FM10K_RESETTING,
	__FM10K_DOWN,
	__FM10K_MBX_LOCK,
	__FM10K_LINK_DOWN,
};

static inline void fm10k_mbx_lock(struct fm10k_intfc *interface)
{
	/* busy loop if we cannot obtain the lock as some calls
	 * such as ndo_set_rx_mode may be made in atomic context
	 */
	while (test_and_set_bit(__FM10K_MBX_LOCK, &interface->state))
		udelay(20);
}

static inline void fm10k_mbx_unlock(struct fm10k_intfc *interface)
{
	/* flush memory to make sure state is correct */
	smp_mb__before_atomic();
	clear_bit(__FM10K_MBX_LOCK, &interface->state);
}

static inline int fm10k_mbx_trylock(struct fm10k_intfc *interface)
{
	return !test_and_set_bit(__FM10K_MBX_LOCK, &interface->state);
}

/* main */
extern char fm10k_driver_name[];
extern const char fm10k_driver_version[];

/* PCI */
int fm10k_register_pci_driver(void);
void fm10k_unregister_pci_driver(void);

/* Netdev */
struct net_device *fm10k_alloc_netdev(void);
void fm10k_restore_rx_state(struct fm10k_intfc *);
void fm10k_reset_rx_state(struct fm10k_intfc *);
#endif /* _FM10K_H_ */
