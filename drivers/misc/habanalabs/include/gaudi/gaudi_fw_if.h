/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2019-2020 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

#ifndef GAUDI_FW_IF_H
#define GAUDI_FW_IF_H

#include <linux/types.h>

#define GAUDI_EVENT_QUEUE_MSI_IDX	8
#define GAUDI_NIC_PORT1_MSI_IDX		10
#define GAUDI_NIC_PORT3_MSI_IDX		12
#define GAUDI_NIC_PORT5_MSI_IDX		14
#define GAUDI_NIC_PORT7_MSI_IDX		16
#define GAUDI_NIC_PORT9_MSI_IDX		18

#define UBOOT_FW_OFFSET			0x100000	/* 1MB in SRAM */
#define LINUX_FW_OFFSET			0x800000	/* 8MB in HBM */

enum gaudi_nic_axi_error {
	RXB,
	RXE,
	TXS,
	TXE,
	QPC_RESP,
	NON_AXI_ERR,
};

/*
 * struct eq_nic_sei_event - describes an AXI error cause.
 * @axi_error_cause: one of the events defined in enum gaudi_nic_axi_error.
 * @id: can be either 0 or 1, to further describe unit with interrupt cause
 *      (i.e. TXE0 or TXE1).
 * @pad[6]: padding structure to 64bit.
 */
struct eq_nic_sei_event {
	__u8 axi_error_cause;
	__u8 id;
	__u8 pad[6];
};

#define GAUDI_PLL_FREQ_LOW		200000000 /* 200 MHz */

#endif /* GAUDI_FW_IF_H */
