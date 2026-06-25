/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2025 Intel Corporation */

#ifndef _IXD_LAN_REGS_H_
#define _IXD_LAN_REGS_H_

/* Control Plane Function PCI ID */
#define IXD_DEV_ID_CPF			0x1efe

/* Control Queue (Mailbox) */
#define PF_FW_MBX_REG_LEN		4096
#define PF_FW_MBX			0x08400000

#define PF_FW_ARQBAL			(PF_FW_MBX)
#define PF_FW_ARQBAH			(PF_FW_MBX + 0x4)
#define PF_FW_ARQLEN			(PF_FW_MBX + 0x8)
#define PF_FW_ARQLEN_ARQLEN_M		GENMASK(12, 0)
#define PF_FW_ARQLEN_ARQENABLE_S	31
#define PF_FW_ARQLEN_ARQENABLE_M	BIT(PF_FW_ARQLEN_ARQENABLE_S)
#define PF_FW_ARQH_ARQH_M		GENMASK(12, 0)
#define PF_FW_ARQH			(PF_FW_MBX + 0xC)
#define PF_FW_ARQT			(PF_FW_MBX + 0x10)

#define PF_FW_ATQBAL			(PF_FW_MBX + 0x14)
#define PF_FW_ATQBAH			(PF_FW_MBX + 0x18)
#define PF_FW_ATQLEN			(PF_FW_MBX + 0x1C)
#define PF_FW_ATQLEN_ATQLEN_M		GENMASK(9, 0)
#define PF_FW_ATQLEN_ATQENABLE_S	31
#define PF_FW_ATQLEN_ATQENABLE_M	BIT(PF_FW_ATQLEN_ATQENABLE_S)
#define PF_FW_ATQH_ATQH_M		GENMASK(9, 0)
#define PF_FW_ATQH			(PF_FW_MBX + 0x20)
#define PF_FW_ATQT			(PF_FW_MBX + 0x24)

/* Reset registers */
#define PFGEN_RTRIG_REG_LEN		2048
#define PFGEN_RTRIG			0x08407000	/* Device resets */
#define PFGEN_RSTAT			0x08407008	/* PFR status */
#define PFGEN_RSTAT_PFR_STATE_M		GENMASK(1, 0)
#define PFGEN_CTRL			0x0840700C	/* PFR trigger */
#define PFGEN_CTRL_PFSWR		BIT(0)

/**
 * struct ixd_bar_region - BAR region description
 * @offset: BAR region offset
 * @size: BAR region size
 */
struct ixd_bar_region {
	resource_size_t offset;
	resource_size_t size;
};

/**
 * struct ixd_reset_reg - structure for reset registers
 * @rstat: offset of status in register
 * @rstat_m: status mask
 * @rstat_ok_v: value that indicates PFR completed status
 * @rtrigger: offset of reset trigger in register
 * @rtrigger_m: reset trigger mask
 */
struct ixd_reset_reg {
	u32	rstat;
	u32	rstat_m;
	u32	rstat_ok_v;
	u32	rtrigger;
	u32	rtrigger_m;
};

#endif /* _IXD_LAN_REGS_H_ */
