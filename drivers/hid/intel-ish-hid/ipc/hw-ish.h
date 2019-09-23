/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * H/W layer of ISHTP provider device (ISH)
 *
 * Copyright (c) 2014-2016, Intel Corporation.
 */

#ifndef _ISHTP_HW_ISH_H_
#define _ISHTP_HW_ISH_H_

#include <linux/pci.h>
#include <linux/interrupt.h>
#include "hw-ish-regs.h"
#include "ishtp-dev.h"

#define CHV_DEVICE_ID		0x22D8
#define BXT_Ax_DEVICE_ID	0x0AA2
#define BXT_Bx_DEVICE_ID	0x1AA2
#define APL_Ax_DEVICE_ID	0x5AA2
#define SPT_Ax_DEVICE_ID	0x9D35
#define CNL_Ax_DEVICE_ID	0x9DFC
#define GLK_Ax_DEVICE_ID	0x31A2
#define CNL_H_DEVICE_ID		0xA37C
#define ICL_MOBILE_DEVICE_ID	0x34FC
#define SPT_H_DEVICE_ID		0xA135
#define CML_LP_DEVICE_ID	0x02FC
#define EHL_Ax_DEVICE_ID	0x4BB3

#define	REVISION_ID_CHT_A0	0x6
#define	REVISION_ID_CHT_Ax_SI	0x0
#define	REVISION_ID_CHT_Bx_SI	0x10
#define	REVISION_ID_CHT_Kx_SI	0x20
#define	REVISION_ID_CHT_Dx_SI	0x30
#define	REVISION_ID_CHT_B0	0xB0
#define	REVISION_ID_SI_MASK	0x70

struct ipc_rst_payload_type {
	uint16_t	reset_id;
	uint16_t	reserved;
};

struct time_sync_format {
	uint8_t ts1_source;
	uint8_t ts2_source;
	uint16_t reserved;
} __packed;

struct ipc_time_update_msg {
	uint64_t primary_host_time;
	struct time_sync_format sync_info;
	uint64_t secondary_host_time;
} __packed;

enum {
	HOST_UTC_TIME_USEC = 0,
	HOST_SYSTEM_TIME_USEC = 1
};

struct ish_hw {
	void __iomem *mem_addr;
};

/*
 * ISH FW status type
 */
enum {
	FWSTS_AFTER_RESET		= 0,
	FWSTS_WAIT_FOR_HOST		= 4,
	FWSTS_START_KERNEL_DMA		= 5,
	FWSTS_FW_IS_RUNNING		= 7,
	FWSTS_SENSOR_APP_LOADED		= 8,
	FWSTS_SENSOR_APP_RUNNING	= 15
};

#define to_ish_hw(dev) (struct ish_hw *)((dev)->hw)

irqreturn_t ish_irq_handler(int irq, void *dev_id);
struct ishtp_device *ish_dev_init(struct pci_dev *pdev);
int ish_hw_start(struct ishtp_device *dev);
void ish_device_disable(struct ishtp_device *dev);

#endif /* _ISHTP_HW_ISH_H_ */
