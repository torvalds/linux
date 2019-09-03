/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#ifndef _IONIC_DEV_H_
#define _IONIC_DEV_H_

#include <linux/mutex.h>
#include <linux/workqueue.h>

#include "ionic_if.h"
#include "ionic_regs.h"

#define IONIC_LIFS_MAX			1024

struct ionic_dev_bar {
	void __iomem *vaddr;
	phys_addr_t bus_addr;
	unsigned long len;
	int res_index;
};

/* Registers */
static_assert(sizeof(struct ionic_intr) == 32);

static_assert(sizeof(struct ionic_doorbell) == 8);
static_assert(sizeof(struct ionic_intr_status) == 8);
static_assert(sizeof(union ionic_dev_regs) == 4096);
static_assert(sizeof(union ionic_dev_info_regs) == 2048);
static_assert(sizeof(union ionic_dev_cmd_regs) == 2048);
static_assert(sizeof(struct ionic_lif_stats) == 1024);

static_assert(sizeof(struct ionic_admin_cmd) == 64);
static_assert(sizeof(struct ionic_admin_comp) == 16);
static_assert(sizeof(struct ionic_nop_cmd) == 64);
static_assert(sizeof(struct ionic_nop_comp) == 16);

/* Device commands */
static_assert(sizeof(struct ionic_dev_identify_cmd) == 64);
static_assert(sizeof(struct ionic_dev_identify_comp) == 16);
static_assert(sizeof(struct ionic_dev_init_cmd) == 64);
static_assert(sizeof(struct ionic_dev_init_comp) == 16);
static_assert(sizeof(struct ionic_dev_reset_cmd) == 64);
static_assert(sizeof(struct ionic_dev_reset_comp) == 16);
static_assert(sizeof(struct ionic_dev_getattr_cmd) == 64);
static_assert(sizeof(struct ionic_dev_getattr_comp) == 16);
static_assert(sizeof(struct ionic_dev_setattr_cmd) == 64);
static_assert(sizeof(struct ionic_dev_setattr_comp) == 16);

/* Port commands */
static_assert(sizeof(struct ionic_port_identify_cmd) == 64);
static_assert(sizeof(struct ionic_port_identify_comp) == 16);
static_assert(sizeof(struct ionic_port_init_cmd) == 64);
static_assert(sizeof(struct ionic_port_init_comp) == 16);
static_assert(sizeof(struct ionic_port_reset_cmd) == 64);
static_assert(sizeof(struct ionic_port_reset_comp) == 16);
static_assert(sizeof(struct ionic_port_getattr_cmd) == 64);
static_assert(sizeof(struct ionic_port_getattr_comp) == 16);
static_assert(sizeof(struct ionic_port_setattr_cmd) == 64);
static_assert(sizeof(struct ionic_port_setattr_comp) == 16);

/* LIF commands */
static_assert(sizeof(struct ionic_lif_init_cmd) == 64);
static_assert(sizeof(struct ionic_lif_init_comp) == 16);
static_assert(sizeof(struct ionic_lif_reset_cmd) == 64);
static_assert(sizeof(ionic_lif_reset_comp) == 16);
static_assert(sizeof(struct ionic_lif_getattr_cmd) == 64);
static_assert(sizeof(struct ionic_lif_getattr_comp) == 16);
static_assert(sizeof(struct ionic_lif_setattr_cmd) == 64);
static_assert(sizeof(struct ionic_lif_setattr_comp) == 16);

static_assert(sizeof(struct ionic_q_init_cmd) == 64);
static_assert(sizeof(struct ionic_q_init_comp) == 16);
static_assert(sizeof(struct ionic_q_control_cmd) == 64);
static_assert(sizeof(ionic_q_control_comp) == 16);

static_assert(sizeof(struct ionic_rx_mode_set_cmd) == 64);
static_assert(sizeof(ionic_rx_mode_set_comp) == 16);
static_assert(sizeof(struct ionic_rx_filter_add_cmd) == 64);
static_assert(sizeof(struct ionic_rx_filter_add_comp) == 16);
static_assert(sizeof(struct ionic_rx_filter_del_cmd) == 64);
static_assert(sizeof(ionic_rx_filter_del_comp) == 16);

/* RDMA commands */
static_assert(sizeof(struct ionic_rdma_reset_cmd) == 64);
static_assert(sizeof(struct ionic_rdma_queue_cmd) == 64);

/* Events */
static_assert(sizeof(struct ionic_notifyq_cmd) == 4);
static_assert(sizeof(union ionic_notifyq_comp) == 64);
static_assert(sizeof(struct ionic_notifyq_event) == 64);
static_assert(sizeof(struct ionic_link_change_event) == 64);
static_assert(sizeof(struct ionic_reset_event) == 64);
static_assert(sizeof(struct ionic_heartbeat_event) == 64);
static_assert(sizeof(struct ionic_log_event) == 64);

/* I/O */
static_assert(sizeof(struct ionic_txq_desc) == 16);
static_assert(sizeof(struct ionic_txq_sg_desc) == 128);
static_assert(sizeof(struct ionic_txq_comp) == 16);

static_assert(sizeof(struct ionic_rxq_desc) == 16);
static_assert(sizeof(struct ionic_rxq_sg_desc) == 128);
static_assert(sizeof(struct ionic_rxq_comp) == 16);

struct ionic_devinfo {
	u8 asic_type;
	u8 asic_rev;
	char fw_version[IONIC_DEVINFO_FWVERS_BUFLEN + 1];
	char serial_num[IONIC_DEVINFO_SERIAL_BUFLEN + 1];
};

struct ionic_dev {
	union ionic_dev_info_regs __iomem *dev_info_regs;
	union ionic_dev_cmd_regs __iomem *dev_cmd_regs;

	u64 __iomem *db_pages;
	dma_addr_t phy_db_pages;

	struct ionic_intr __iomem *intr_ctrl;
	u64 __iomem *intr_status;

	u32 port_info_sz;
	struct ionic_port_info *port_info;
	dma_addr_t port_info_pa;

	struct ionic_devinfo dev_info;
};

struct ionic;

void ionic_init_devinfo(struct ionic *ionic);
int ionic_dev_setup(struct ionic *ionic);
void ionic_dev_teardown(struct ionic *ionic);

void ionic_dev_cmd_go(struct ionic_dev *idev, union ionic_dev_cmd *cmd);
u8 ionic_dev_cmd_status(struct ionic_dev *idev);
bool ionic_dev_cmd_done(struct ionic_dev *idev);
void ionic_dev_cmd_comp(struct ionic_dev *idev, union ionic_dev_cmd_comp *comp);

void ionic_dev_cmd_identify(struct ionic_dev *idev, u8 ver);
void ionic_dev_cmd_init(struct ionic_dev *idev);
void ionic_dev_cmd_reset(struct ionic_dev *idev);

void ionic_dev_cmd_port_identify(struct ionic_dev *idev);
void ionic_dev_cmd_port_init(struct ionic_dev *idev);
void ionic_dev_cmd_port_reset(struct ionic_dev *idev);
void ionic_dev_cmd_port_state(struct ionic_dev *idev, u8 state);
void ionic_dev_cmd_port_speed(struct ionic_dev *idev, u32 speed);
void ionic_dev_cmd_port_autoneg(struct ionic_dev *idev, u8 an_enable);
void ionic_dev_cmd_port_fec(struct ionic_dev *idev, u8 fec_type);
void ionic_dev_cmd_port_pause(struct ionic_dev *idev, u8 pause_type);

void ionic_dev_cmd_lif_identify(struct ionic_dev *idev, u8 type, u8 ver);
void ionic_dev_cmd_lif_init(struct ionic_dev *idev, u16 lif_index,
			    dma_addr_t addr);
void ionic_dev_cmd_lif_reset(struct ionic_dev *idev, u16 lif_index);

#endif /* _IONIC_DEV_H_ */
