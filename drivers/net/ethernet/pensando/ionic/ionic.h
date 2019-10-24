/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2017 - 2019 Pensando Systems, Inc */

#ifndef _IONIC_H_
#define _IONIC_H_

struct ionic_lif;

#include "ionic_if.h"
#include "ionic_dev.h"
#include "ionic_devlink.h"

#define IONIC_DRV_NAME		"ionic"
#define IONIC_DRV_DESCRIPTION	"Pensando Ethernet NIC Driver"
#define IONIC_DRV_VERSION	"0.18.0-k"

#define PCI_VENDOR_ID_PENSANDO			0x1dd8

#define PCI_DEVICE_ID_PENSANDO_IONIC_ETH_PF	0x1002
#define PCI_DEVICE_ID_PENSANDO_IONIC_ETH_VF	0x1003

#define IONIC_SUBDEV_ID_NAPLES_25	0x4000
#define IONIC_SUBDEV_ID_NAPLES_100_4	0x4001
#define IONIC_SUBDEV_ID_NAPLES_100_8	0x4002

#define DEVCMD_TIMEOUT  10

struct ionic {
	struct pci_dev *pdev;
	struct device *dev;
	struct devlink_port dl_port;
	struct ionic_dev idev;
	struct mutex dev_cmd_lock;	/* lock for dev_cmd operations */
	struct dentry *dentry;
	struct ionic_dev_bar bars[IONIC_BARS_MAX];
	unsigned int num_bars;
	struct ionic_identity ident;
	struct list_head lifs;
	struct ionic_lif *master_lif;
	unsigned int nnqs_per_lif;
	unsigned int neqs_per_lif;
	unsigned int ntxqs_per_lif;
	unsigned int nrxqs_per_lif;
	DECLARE_BITMAP(lifbits, IONIC_LIFS_MAX);
	unsigned int nintrs;
	DECLARE_BITMAP(intrs, IONIC_INTR_CTRL_REGS_MAX);
	struct work_struct nb_work;
	struct notifier_block nb;
	struct timer_list watchdog_timer;
	int watchdog_period;
};

struct ionic_admin_ctx {
	struct completion work;
	union ionic_adminq_cmd cmd;
	union ionic_adminq_comp comp;
};

int ionic_napi(struct napi_struct *napi, int budget, ionic_cq_cb cb,
	       ionic_cq_done_cb done_cb, void *done_arg);

int ionic_adminq_post_wait(struct ionic_lif *lif, struct ionic_admin_ctx *ctx);
int ionic_dev_cmd_wait(struct ionic *ionic, unsigned long max_wait);
int ionic_set_dma_mask(struct ionic *ionic);
int ionic_setup(struct ionic *ionic);

int ionic_identify(struct ionic *ionic);
int ionic_init(struct ionic *ionic);
int ionic_reset(struct ionic *ionic);

int ionic_port_identify(struct ionic *ionic);
int ionic_port_init(struct ionic *ionic);
int ionic_port_reset(struct ionic *ionic);

#endif /* _IONIC_H_ */
