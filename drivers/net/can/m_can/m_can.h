/* SPDX-License-Identifier: GPL-2.0 */
/* CAN bus driver for Bosch M_CAN controller
 * Copyright (C) 2018 Texas Instruments Incorporated - http://www.ti.com/
 */

#ifndef _CAN_M_CAN_H_
#define _CAN_M_CAN_H_

#include <linux/can/core.h>
#include <linux/can/dev.h>
#include <linux/can/rx-offload.h>
#include <linux/clk.h>
#include <linux/completion.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/freezer.h>
#include <linux/hrtimer.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/of.h>
#include <linux/phy/phy.h>
#include <linux/pinctrl/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/slab.h>
#include <linux/uaccess.h>

/* m_can lec values */
enum m_can_lec_type {
	LEC_NO_ERROR = 0,
	LEC_STUFF_ERROR,
	LEC_FORM_ERROR,
	LEC_ACK_ERROR,
	LEC_BIT1_ERROR,
	LEC_BIT0_ERROR,
	LEC_CRC_ERROR,
	LEC_NO_CHANGE,
};

enum m_can_mram_cfg {
	MRAM_SIDF = 0,
	MRAM_XIDF,
	MRAM_RXF0,
	MRAM_RXF1,
	MRAM_RXB,
	MRAM_TXE,
	MRAM_TXB,
	MRAM_CFG_NUM,
};

/* address offset and element number for each FIFO/Buffer in the Message RAM */
struct mram_cfg {
	u16 off;
	u8  num;
};

struct m_can_classdev;
struct m_can_ops {
	/* Device specific call backs */
	int (*clear_interrupts)(struct m_can_classdev *cdev);
	u32 (*read_reg)(struct m_can_classdev *cdev, int reg);
	int (*write_reg)(struct m_can_classdev *cdev, int reg, int val);
	int (*read_fifo)(struct m_can_classdev *cdev, int addr_offset, void *val, size_t val_count);
	int (*write_fifo)(struct m_can_classdev *cdev, int addr_offset,
			  const void *val, size_t val_count);
	int (*init)(struct m_can_classdev *cdev);
	int (*deinit)(struct m_can_classdev *cdev);
};

struct m_can_tx_op {
	struct m_can_classdev *cdev;
	struct work_struct work;
	struct sk_buff *skb;
	bool submit;
};

struct m_can_classdev {
	struct can_priv can;
	struct can_rx_offload offload;
	struct napi_struct napi;
	struct net_device *net;
	struct device *dev;
	struct clk *hclk;
	struct clk *cclk;

	struct workqueue_struct *tx_wq;
	struct phy *transceiver;

	ktime_t irq_timer_wait;

	const struct m_can_ops *ops;

	int version;
	u32 irqstatus;

	int pm_clock_support;
	int pm_wake_source;
	int is_peripheral;
	bool irq_edge_triggered;

	// Cached M_CAN_IE register content
	u32 active_interrupts;
	u32 rx_max_coalesced_frames_irq;
	u32 rx_coalesce_usecs_irq;
	u32 tx_max_coalesced_frames;
	u32 tx_max_coalesced_frames_irq;
	u32 tx_coalesce_usecs_irq;

	// Store this internally to avoid fetch delays on peripheral chips
	u32 tx_fifo_putidx;

	/* Protects shared state between start_xmit and m_can_isr */
	spinlock_t tx_handling_spinlock;
	int tx_fifo_in_flight;

	struct m_can_tx_op *tx_ops;
	int tx_fifo_size;
	int next_tx_op;

	int nr_txs_without_submit;
	/* bitfield of fifo elements that will be submitted together */
	u32 tx_peripheral_submit;

	struct mram_cfg mcfg[MRAM_CFG_NUM];

	struct hrtimer hrtimer;
};

struct m_can_classdev *m_can_class_allocate_dev(struct device *dev, int sizeof_priv);
void m_can_class_free_dev(struct net_device *net);
int m_can_class_register(struct m_can_classdev *cdev);
void m_can_class_unregister(struct m_can_classdev *cdev);
int m_can_class_get_clocks(struct m_can_classdev *cdev);
int m_can_init_ram(struct m_can_classdev *priv);
int m_can_check_mram_cfg(struct m_can_classdev *cdev, u32 mram_max_size);

int m_can_class_suspend(struct device *dev);
int m_can_class_resume(struct device *dev);
#endif	/* _CAN_M_H_ */
