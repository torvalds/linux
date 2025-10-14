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

#define PCI_VENDOR_ID_PENSANDO			0x1dd8

#define PCI_DEVICE_ID_PENSANDO_IONIC_ETH_PF	0x1002
#define PCI_DEVICE_ID_PENSANDO_IONIC_ETH_VF	0x1003

#define DEVCMD_TIMEOUT			5
#define IONIC_ADMINQ_TIME_SLICE		msecs_to_jiffies(100)

#define IONIC_PHC_UPDATE_NS	10000000000	    /* 10s in nanoseconds */
#define NORMAL_PPB		1000000000	    /* one billion parts per billion */
#define SCALED_PPM		(1000000ull << 16)  /* 2^16 million parts per 2^16 million */

struct ionic_vf {
	u16	 index;
	u8	 macaddr[6];
	__le32	 maxrate;
	__le16	 vlanid;
	u8	 spoofchk;
	u8	 trusted;
	u8	 linkstate;
	dma_addr_t       stats_pa;
	struct ionic_lif_stats stats;
};

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
	struct workqueue_struct *wq;
	struct ionic_lif *lif;
	unsigned int nnqs_per_lif;
	unsigned int neqs_per_lif;
	unsigned int ntxqs_per_lif;
	unsigned int nrxqs_per_lif;
	unsigned int nintrs;
	DECLARE_BITMAP(intrs, IONIC_INTR_CTRL_REGS_MAX);
	cpumask_var_t *affinity_masks;
	struct delayed_work doorbell_check_dwork;
	struct notifier_block nb;
	struct rw_semaphore vf_op_lock;	/* lock for VF operations */
	struct ionic_vf *vfs;
	int num_vfs;
	struct timer_list watchdog_timer;
	int watchdog_period;
};

int ionic_adminq_post(struct ionic_lif *lif, struct ionic_admin_ctx *ctx);
int ionic_adminq_wait(struct ionic_lif *lif, struct ionic_admin_ctx *ctx,
		      const int err, const bool do_msg);
int ionic_adminq_post_wait_nomsg(struct ionic_lif *lif, struct ionic_admin_ctx *ctx);
void ionic_adminq_netdev_err_print(struct ionic_lif *lif, u8 opcode,
				   u8 status, int err);
bool ionic_notifyq_service(struct ionic_cq *cq);
bool ionic_adminq_service(struct ionic_cq *cq);

int ionic_dev_cmd_wait(struct ionic *ionic, unsigned long max_wait);
int ionic_dev_cmd_wait_nomsg(struct ionic *ionic, unsigned long max_wait);
void ionic_dev_cmd_dev_err_print(struct ionic *ionic, u8 opcode, u8 status,
				 int err);
int ionic_setup(struct ionic *ionic);

int ionic_identify(struct ionic *ionic);
int ionic_init(struct ionic *ionic);
int ionic_reset(struct ionic *ionic);

int ionic_port_identify(struct ionic *ionic);
int ionic_port_init(struct ionic *ionic);
int ionic_port_reset(struct ionic *ionic);

bool ionic_doorbell_wa(struct ionic *ionic);

#endif /* _IONIC_H_ */
