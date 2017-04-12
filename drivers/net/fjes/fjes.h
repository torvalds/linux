/*
 *  FUJITSU Extended Socket Network Device driver
 *  Copyright (c) 2015 FUJITSU LIMITED
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
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 */

#ifndef FJES_H_
#define FJES_H_

#include <linux/acpi.h>

#include "fjes_hw.h"

#define FJES_ACPI_SYMBOL	"Extended Socket"
#define FJES_MAX_QUEUES		1
#define FJES_TX_RETRY_INTERVAL	(20 * HZ)
#define FJES_TX_RETRY_TIMEOUT	(100)
#define FJES_TX_TX_STALL_TIMEOUT	(FJES_TX_RETRY_INTERVAL / 2)
#define FJES_OPEN_ZONE_UPDATE_WAIT	(300) /* msec */
#define FJES_IRQ_WATCH_DELAY	(HZ)

/* board specific private data structure */
struct fjes_adapter {
	struct net_device *netdev;
	struct platform_device *plat_dev;

	struct napi_struct napi;
	struct rtnl_link_stats64 stats64;

	unsigned int tx_retry_count;
	unsigned long tx_start_jiffies;
	unsigned long rx_last_jiffies;
	bool unset_rx_last;

	struct work_struct force_close_task;
	bool force_reset;
	bool open_guard;

	bool irq_registered;

	struct workqueue_struct *txrx_wq;
	struct workqueue_struct *control_wq;

	struct work_struct tx_stall_task;
	struct work_struct raise_intr_rxdata_task;

	struct work_struct unshare_watch_task;
	unsigned long unshare_watch_bitmask;

	struct delayed_work interrupt_watch_task;
	bool interrupt_watch_enable;

	struct fjes_hw hw;

#ifdef CONFIG_DEBUG_FS
	struct dentry *dbg_adapter;
#endif
};

extern char fjes_driver_name[];
extern char fjes_driver_version[];
extern const u32 fjes_support_mtu[];

void fjes_set_ethtool_ops(struct net_device *);

#ifdef CONFIG_DEBUG_FS
void fjes_dbg_adapter_init(struct fjes_adapter *adapter);
void fjes_dbg_adapter_exit(struct fjes_adapter *adapter);
void fjes_dbg_init(void);
void fjes_dbg_exit(void);
#else
static inline void fjes_dbg_adapter_init(struct fjes_adapter *adapter) {}
static inline void fjes_dbg_adapter_exit(struct fjes_adapter *adapter) {}
static inline void fjes_dbg_init(void) {}
static inline void fjes_dbg_exit(void) {}
#endif /* CONFIG_DEBUG_FS */

#endif /* FJES_H_ */
