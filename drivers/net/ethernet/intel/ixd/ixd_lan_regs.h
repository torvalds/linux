/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef _IXD_LAN_REGS_H_
#define _IXD_LAN_REGS_H_

/* Control Plane Function PCI ID */
#define IXD_DEV_ID_CPF			0x1efe

/* Control Queue (Mailbox) */
#define PF_FW_MBX_REG_LEN		4096
#define PF_FW_MBX			0x08400000

/* Reset registers */
#define PFGEN_RTRIG_REG_LEN		2048
#define PFGEN_RTRIG			0x08407000	/* Device resets */

/**
 * struct ixd_bar_region - BAR region description
 * @offset: BAR region offset
 * @size: BAR region size
 */
struct ixd_bar_region {
	resource_size_t offset;
	resource_size_t size;
};

#endif /* _IXD_LAN_REGS_H_ */
