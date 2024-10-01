/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (c) 2021, MediaTek Inc.
 * Copyright (c) 2021-2022, Intel Corporation.
 *
 * Authors:
 *  Haijun Liu <haijun.liu@mediatek.com>
 *  Eliot Lee <eliot.lee@intel.com>
 *  Moises Veleta <moises.veleta@intel.com>
 *  Ricardo Martinez <ricardo.martinez@linux.intel.com>
 *
 * Contributors:
 *  Amir Hanania <amir.hanania@intel.com>
 *  Chiranjeevi Rapolu <chiranjeevi.rapolu@intel.com>
 *  Sreehari Kancharla <sreehari.kancharla@intel.com>
 */

#ifndef __T7XX_MODEM_OPS_H__
#define __T7XX_MODEM_OPS_H__

#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/workqueue.h>

#include "t7xx_hif_cldma.h"
#include "t7xx_pci.h"

#define FEATURE_COUNT		64

/**
 * enum hif_ex_stage -	HIF exception handshake stages with the HW.
 * @HIF_EX_INIT:        Disable and clear TXQ.
 * @HIF_EX_INIT_DONE:   Polling for initialization to be done.
 * @HIF_EX_CLEARQ_DONE: Disable RX, flush TX/RX workqueues and clear RX.
 * @HIF_EX_ALLQ_RESET:  HW is back in safe mode for re-initialization and restart.
 */
enum hif_ex_stage {
	HIF_EX_INIT,
	HIF_EX_INIT_DONE,
	HIF_EX_CLEARQ_DONE,
	HIF_EX_ALLQ_RESET,
};

struct mtk_runtime_feature {
	u8				feature_id;
	u8				support_info;
	u8				reserved[2];
	__le32				data_len;
	__le32				data[];
};

enum md_event_id {
	FSM_PRE_START,
	FSM_START,
	FSM_READY,
};

struct t7xx_sys_info {
	bool				ready;
	bool				handshake_ongoing;
	u8				feature_set[FEATURE_COUNT];
	struct t7xx_port		*ctl_port;
};

struct t7xx_modem {
	struct cldma_ctrl		*md_ctrl[CLDMA_NUM];
	struct t7xx_pci_dev		*t7xx_dev;
	struct t7xx_sys_info		core_md;
	struct t7xx_sys_info		core_ap;
	bool				md_init_finish;
	bool				rgu_irq_asserted;
	struct workqueue_struct		*handshake_wq;
	struct work_struct		handshake_work;
	struct work_struct		ap_handshake_work;
	struct t7xx_fsm_ctl		*fsm_ctl;
	struct port_proxy		*port_prox;
	unsigned int			exp_id;
	spinlock_t			exp_lock; /* Protects exception events */
};

enum reset_type {
	FLDR,
	PLDR,
	FASTBOOT,
};

void t7xx_md_exception_handshake(struct t7xx_modem *md);
void t7xx_md_event_notify(struct t7xx_modem *md, enum md_event_id evt_id);
int t7xx_md_reset(struct t7xx_pci_dev *t7xx_dev);
int t7xx_md_init(struct t7xx_pci_dev *t7xx_dev);
void t7xx_md_exit(struct t7xx_pci_dev *t7xx_dev);
void t7xx_clear_rgu_irq(struct t7xx_pci_dev *t7xx_dev);
int t7xx_reset_device(struct t7xx_pci_dev *t7xx_dev, enum reset_type type);
int t7xx_pci_mhccif_isr(struct t7xx_pci_dev *t7xx_dev);

#endif	/* __T7XX_MODEM_OPS_H__ */
