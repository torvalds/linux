/*****************************************************************************
 *                                                                           *
 * File: common.h                                                            *
 * $Revision: 1.21 $                                                         *
 * $Date: 2005/06/22 00:43:25 $                                              *
 * Description:                                                              *
 *  part of the Chelsio 10Gb Ethernet Driver.                                *
 *                                                                           *
 * This program is free software; you can redistribute it and/or modify      *
 * it under the terms of the GNU General Public License, version 2, as       *
 * published by the Free Software Foundation.                                *
 *                                                                           *
 * You should have received a copy of the GNU General Public License along   *
 * with this program; if not, write to the Free Software Foundation, Inc.,   *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.                 *
 *                                                                           *
 * THIS SOFTWARE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR IMPLIED    *
 * WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED WARRANTIES OF      *
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.                     *
 *                                                                           *
 * http://www.chelsio.com                                                    *
 *                                                                           *
 * Copyright (c) 2003 - 2005 Chelsio Communications, Inc.                    *
 * All rights reserved.                                                      *
 *                                                                           *
 * Maintainers: maintainers@chelsio.com                                      *
 *                                                                           *
 * Authors: Dimitrios Michailidis   <dm@chelsio.com>                         *
 *          Tina Yang               <tainay@chelsio.com>                     *
 *          Felix Marti             <felix@chelsio.com>                      *
 *          Scott Bardone           <sbardone@chelsio.com>                   *
 *          Kurt Ottaway            <kottaway@chelsio.com>                   *
 *          Frank DiMambro          <frank@chelsio.com>                      *
 *                                                                           *
 * History:                                                                  *
 *                                                                           *
 ****************************************************************************/

#ifndef _CXGB_COMMON_H_
#define _CXGB_COMMON_H_

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/ethtool.h>
#include <linux/mii.h>
#include <linux/crc32.h>
#include <linux/init.h>
#include <asm/io.h>
#include <linux/pci_ids.h>

#define DRV_DESCRIPTION "Chelsio 10Gb Ethernet Driver"
#define DRV_NAME "cxgb"
#define DRV_VERSION "2.1.1"
#define PFX      DRV_NAME ": "

#define CH_ERR(fmt, ...)   printk(KERN_ERR PFX fmt, ## __VA_ARGS__)
#define CH_WARN(fmt, ...)  printk(KERN_WARNING PFX fmt, ## __VA_ARGS__)
#define CH_ALERT(fmt, ...) printk(KERN_ALERT PFX fmt, ## __VA_ARGS__)

#define CH_DEVICE(devid, ssid, idx) \
	{ PCI_VENDOR_ID_CHELSIO, devid, PCI_ANY_ID, ssid, 0, 0, idx }

#define SUPPORTED_PAUSE       (1 << 13)
#define SUPPORTED_LOOPBACK    (1 << 15)

#define ADVERTISED_PAUSE      (1 << 13)
#define ADVERTISED_ASYM_PAUSE (1 << 14)

typedef struct adapter adapter_t;

void t1_elmer0_ext_intr(adapter_t *adapter);
void t1_link_changed(adapter_t *adapter, int port_id, int link_status,
			int speed, int duplex, int fc);

struct t1_rx_mode {
	struct net_device *dev;
	u32 idx;
	struct dev_mc_list *list;
};

#define t1_rx_mode_promisc(rm)	(rm->dev->flags & IFF_PROMISC)
#define t1_rx_mode_allmulti(rm)	(rm->dev->flags & IFF_ALLMULTI)
#define t1_rx_mode_mc_cnt(rm)	(rm->dev->mc_count)

static inline u8 *t1_get_next_mcaddr(struct t1_rx_mode *rm)
{
	u8 *addr = NULL;

	if (rm->idx++ < rm->dev->mc_count) {
		addr = rm->list->dmi_addr;
		rm->list = rm->list->next;
	}
	return addr;
}

#define	MAX_NPORTS 4

#define SPEED_INVALID 0xffff
#define DUPLEX_INVALID 0xff

enum {
	CHBT_BOARD_N110,
	CHBT_BOARD_N210
};

enum {
	CHBT_TERM_T1,
	CHBT_TERM_T2
};

enum {
	CHBT_MAC_PM3393,
};

enum {
	CHBT_PHY_88X2010,
};

enum {
	PAUSE_RX      = 1 << 0,
	PAUSE_TX      = 1 << 1,
	PAUSE_AUTONEG = 1 << 2
};

/* Revisions of T1 chip */
enum {
	TERM_T1A   = 0,
	TERM_T1B   = 1,
	TERM_T2    = 3
};

struct sge_params {
	unsigned int cmdQ_size[2];
	unsigned int freelQ_size[2];
	unsigned int large_buf_capacity;
	unsigned int rx_coalesce_usecs;
	unsigned int last_rx_coalesce_raw;
	unsigned int default_rx_coalesce_usecs;
	unsigned int sample_interval_usecs;
	unsigned int coalesce_enable;
	unsigned int polling;
};

struct chelsio_pci_params {
	unsigned short speed;
	unsigned char  width;
	unsigned char  is_pcix;
};

struct adapter_params {
	struct sge_params sge;
	struct chelsio_pci_params pci;

	const struct board_info *brd_info;

	unsigned int   nports;          /* # of ethernet ports */
	unsigned int   stats_update_period;
	unsigned short chip_revision;
	unsigned char  chip_version;
};

struct link_config {
	unsigned int   supported;        /* link capabilities */
	unsigned int   advertising;      /* advertised capabilities */
	unsigned short requested_speed;  /* speed user has requested */
	unsigned short speed;            /* actual link speed */
	unsigned char  requested_duplex; /* duplex user has requested */
	unsigned char  duplex;           /* actual link duplex */
	unsigned char  requested_fc;     /* flow control user has requested */
	unsigned char  fc;               /* actual link flow control */
	unsigned char  autoneg;          /* autonegotiating? */
};

struct cmac;
struct cphy;

struct port_info {
	struct net_device *dev;
	struct cmac *mac;
	struct cphy *phy;
	struct link_config link_config;
	struct net_device_stats netstats;
};

struct sge;
struct peespi;

struct adapter {
	u8 __iomem *regs;
	struct pci_dev *pdev;
	unsigned long registered_device_map;
	unsigned long open_device_map;
	unsigned long flags;

	const char *name;
	int msg_enable;
	u32 mmio_len;

	struct work_struct ext_intr_handler_task;
	struct adapter_params params;

	struct vlan_group *vlan_grp;

	/* Terminator modules. */
	struct sge    *sge;
	struct peespi *espi;

	struct port_info port[MAX_NPORTS];
	struct work_struct stats_update_task;
	struct timer_list stats_update_timer;

	spinlock_t tpi_lock;
	spinlock_t work_lock;
	/* guards async operations */
	spinlock_t async_lock ____cacheline_aligned;
	u32 slow_intr_mask;
};

enum {                                           /* adapter flags */
	FULL_INIT_DONE        = 1 << 0,
	TSO_CAPABLE           = 1 << 2,
	TCP_CSUM_CAPABLE      = 1 << 3,
	UDP_CSUM_CAPABLE      = 1 << 4,
	VLAN_ACCEL_CAPABLE    = 1 << 5,
	RX_CSUM_ENABLED       = 1 << 6,
};

struct mdio_ops;
struct gmac;
struct gphy;

struct board_info {
	unsigned char           board;
	unsigned char           port_number;
	unsigned long           caps;
	unsigned char           chip_term;
	unsigned char           chip_mac;
	unsigned char           chip_phy;
	unsigned int            clock_core;
	unsigned int            clock_mc3;
	unsigned int            clock_mc4;
	unsigned int            espi_nports;
	unsigned int            clock_cspi;
	unsigned int            clock_elmer0;
	unsigned char           mdio_mdien;
	unsigned char           mdio_mdiinv;
	unsigned char           mdio_mdc;
	unsigned char           mdio_phybaseaddr;
	struct gmac            *gmac;
	struct gphy            *gphy;
	struct mdio_ops	       *mdio_ops;
	const char             *desc;
};

extern struct pci_device_id t1_pci_tbl[];

static inline int adapter_matches_type(const adapter_t *adapter,
				       int version, int revision)
{
	return adapter->params.chip_version == version &&
	       adapter->params.chip_revision == revision;
}

#define t1_is_T1B(adap) adapter_matches_type(adap, CHBT_TERM_T1, TERM_T1B)
#define is_T2(adap)     adapter_matches_type(adap, CHBT_TERM_T2, TERM_T2)

/* Returns true if an adapter supports VLAN acceleration and TSO */
static inline int vlan_tso_capable(const adapter_t *adapter)
{
	return !t1_is_T1B(adapter);
}

#define for_each_port(adapter, iter) \
	for (iter = 0; iter < (adapter)->params.nports; ++iter)

#define board_info(adapter) ((adapter)->params.brd_info)
#define is_10G(adapter) (board_info(adapter)->caps & SUPPORTED_10000baseT_Full)

static inline unsigned int core_ticks_per_usec(const adapter_t *adap)
{
	return board_info(adap)->clock_core / 1000000;
}

extern int t1_tpi_write(adapter_t *adapter, u32 addr, u32 value);
extern int t1_tpi_read(adapter_t *adapter, u32 addr, u32 *value);

extern void t1_interrupts_enable(adapter_t *adapter);
extern void t1_interrupts_disable(adapter_t *adapter);
extern void t1_interrupts_clear(adapter_t *adapter);
extern int elmer0_ext_intr_handler(adapter_t *adapter);
extern int t1_slow_intr_handler(adapter_t *adapter);

extern int t1_link_start(struct cphy *phy, struct cmac *mac, struct link_config *lc);
extern const struct board_info *t1_get_board_info(unsigned int board_id);
extern const struct board_info *t1_get_board_info_from_ids(unsigned int devid,
						    unsigned short ssid);
extern int t1_seeprom_read(adapter_t *adapter, u32 addr, u32 *data);
extern int t1_get_board_rev(adapter_t *adapter, const struct board_info *bi,
		     struct adapter_params *p);
extern int t1_init_hw_modules(adapter_t *adapter);
extern int t1_init_sw_modules(adapter_t *adapter, const struct board_info *bi);
extern void t1_free_sw_modules(adapter_t *adapter);
extern void t1_fatal_err(adapter_t *adapter);

extern void t1_tp_set_udp_checksum_offload(adapter_t *adapter, int enable);
extern void t1_tp_set_tcp_checksum_offload(adapter_t *adapter, int enable);
extern void t1_tp_set_ip_checksum_offload(adapter_t *adapter, int enable);

#endif /* _CXGB_COMMON_H_ */
