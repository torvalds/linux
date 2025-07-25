/* SPDX-License-Identifier: GPL-2.0 OR BSD-2-Clause */
/* kvaser_pciefd common definitions and declarations
 *
 * Copyright (C) 2025 KVASER AB, Sweden. All rights reserved.
 */

#ifndef _KVASER_PCIEFD_H
#define _KVASER_PCIEFD_H

#include <linux/can/dev.h>
#include <linux/completion.h>
#include <linux/pci.h>
#include <linux/spinlock.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <net/devlink.h>

#define KVASER_PCIEFD_MAX_CAN_CHANNELS 8UL
#define KVASER_PCIEFD_DMA_COUNT 2U
#define KVASER_PCIEFD_DMA_SIZE (4U * 1024U)
#define KVASER_PCIEFD_CAN_TX_MAX_COUNT 17U

struct kvaser_pciefd;

struct kvaser_pciefd_address_offset {
	u32 serdes;
	u32 pci_ien;
	u32 pci_irq;
	u32 sysid;
	u32 loopback;
	u32 kcan_srb_fifo;
	u32 kcan_srb;
	u32 kcan_ch0;
	u32 kcan_ch1;
};

struct kvaser_pciefd_irq_mask {
	u32 kcan_rx0;
	u32 kcan_tx[KVASER_PCIEFD_MAX_CAN_CHANNELS];
	u32 all;
};

struct kvaser_pciefd_dev_ops {
	void (*kvaser_pciefd_write_dma_map)(struct kvaser_pciefd *pcie,
					    dma_addr_t addr, int index);
};

struct kvaser_pciefd_driver_data {
	const struct kvaser_pciefd_address_offset *address_offset;
	const struct kvaser_pciefd_irq_mask *irq_mask;
	const struct kvaser_pciefd_dev_ops *ops;
};

struct kvaser_pciefd_fw_version {
	u8 major;
	u8 minor;
	u16 build;
};

struct kvaser_pciefd_can {
	struct can_priv can;
	struct devlink_port devlink_port;
	struct kvaser_pciefd *kv_pcie;
	void __iomem *reg_base;
	struct can_berr_counter bec;
	u32 ioc;
	u8 cmd_seq;
	u8 tx_max_count;
	u8 tx_idx;
	u8 ack_idx;
	int err_rep_cnt;
	unsigned int completed_tx_pkts;
	unsigned int completed_tx_bytes;
	spinlock_t lock; /* Locks sensitive registers (e.g. MODE) */
	struct timer_list bec_poll_timer;
	struct completion start_comp, flush_comp;
};

struct kvaser_pciefd {
	struct pci_dev *pci;
	void __iomem *reg_base;
	struct kvaser_pciefd_can *can[KVASER_PCIEFD_MAX_CAN_CHANNELS];
	const struct kvaser_pciefd_driver_data *driver_data;
	void *dma_data[KVASER_PCIEFD_DMA_COUNT];
	u8 nr_channels;
	u32 bus_freq;
	u32 freq;
	u32 freq_to_ticks_div;
	struct kvaser_pciefd_fw_version fw_version;
};

extern const struct devlink_ops kvaser_pciefd_devlink_ops;

int kvaser_pciefd_devlink_port_register(struct kvaser_pciefd_can *can);
void kvaser_pciefd_devlink_port_unregister(struct kvaser_pciefd_can *can);
#endif /* _KVASER_PCIEFD_H */
