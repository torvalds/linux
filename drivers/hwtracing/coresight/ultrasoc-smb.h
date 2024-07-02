/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
/*
 * Siemens System Memory Buffer driver.
 * Copyright(c) 2022, HiSilicon Limited.
 */

#ifndef _ULTRASOC_SMB_H
#define _ULTRASOC_SMB_H

#include <linux/miscdevice.h>
#include <linux/spinlock.h>

/* Offset of SMB global registers */
#define SMB_GLB_CFG_REG		0x00
#define SMB_GLB_EN_REG		0x04
#define SMB_GLB_INT_REG		0x08

/* Offset of SMB logical buffer registers */
#define SMB_LB_CFG_LO_REG	0x40
#define SMB_LB_CFG_HI_REG	0x44
#define SMB_LB_INT_CTRL_REG	0x48
#define SMB_LB_INT_STS_REG	0x4c
#define SMB_LB_RD_ADDR_REG	0x5c
#define SMB_LB_WR_ADDR_REG	0x60
#define SMB_LB_PURGE_REG	0x64

/* Set global config register */
#define SMB_GLB_CFG_BURST_LEN_MSK	GENMASK(11, 4)
#define SMB_GLB_CFG_IDLE_PRD_MSK	GENMASK(15, 12)
#define SMB_GLB_CFG_MEM_WR_MSK		GENMASK(21, 16)
#define SMB_GLB_CFG_MEM_RD_MSK		GENMASK(27, 22)
#define SMB_GLB_CFG_DEFAULT	(FIELD_PREP(SMB_GLB_CFG_BURST_LEN_MSK, 0xf) | \
				 FIELD_PREP(SMB_GLB_CFG_IDLE_PRD_MSK, 0xf) | \
				 FIELD_PREP(SMB_GLB_CFG_MEM_WR_MSK, 0x3) | \
				 FIELD_PREP(SMB_GLB_CFG_MEM_RD_MSK, 0x1b))

#define SMB_GLB_EN_HW_ENABLE	BIT(0)

/* Set global interrupt control register */
#define SMB_GLB_INT_EN		BIT(0)
#define SMB_GLB_INT_PULSE	BIT(1) /* Interrupt type: 1 - Pulse */
#define SMB_GLB_INT_ACT_H	BIT(2) /* Interrupt polarity: 1 - Active high */
#define SMB_GLB_INT_CFG		(SMB_GLB_INT_EN | SMB_GLB_INT_PULSE | \
				 SMB_GLB_INT_ACT_H)

/* Set logical buffer config register lower 32 bits */
#define SMB_LB_CFG_LO_EN		BIT(0)
#define SMB_LB_CFG_LO_SINGLE_END	BIT(1)
#define SMB_LB_CFG_LO_INIT		BIT(8)
#define SMB_LB_CFG_LO_CONT		BIT(11)
#define SMB_LB_CFG_LO_FLOW_MSK		GENMASK(19, 16)
#define SMB_LB_CFG_LO_DEFAULT	(SMB_LB_CFG_LO_EN | SMB_LB_CFG_LO_SINGLE_END | \
				 SMB_LB_CFG_LO_INIT | SMB_LB_CFG_LO_CONT | \
				 FIELD_PREP(SMB_LB_CFG_LO_FLOW_MSK, 0xf))

/* Set logical buffer config register upper 32 bits */
#define SMB_LB_CFG_HI_RANGE_UP_MSK	GENMASK(15, 8)
#define SMB_LB_CFG_HI_DEFAULT	FIELD_PREP(SMB_LB_CFG_HI_RANGE_UP_MSK, 0xff)

/*
 * Set logical buffer interrupt control register.
 * The register control the validity of both real-time events and
 * interrupts. When logical buffer status changes causes to issue
 * an interrupt at the same time as it issues a real-time event.
 * Real-time events are used in SMB driver, which needs to get the buffer
 * status. Interrupts are used in debugger mode.
 * SMB_LB_INT_CTRL_BUF_NOTE_MASK control which events flags or interrupts
 * are valid.
 */
#define SMB_LB_INT_CTRL_EN		BIT(0)
#define SMB_LB_INT_CTRL_BUF_NOTE_MSK	GENMASK(11, 8)
#define SMB_LB_INT_CTRL_CFG	(SMB_LB_INT_CTRL_EN | \
				 FIELD_PREP(SMB_LB_INT_CTRL_BUF_NOTE_MSK, 0xf))

/* Set logical buffer interrupt status register */
#define SMB_LB_INT_STS_NOT_EMPTY_MSK	BIT(0)
#define SMB_LB_INT_STS_BUF_RESET_MSK	GENMASK(3, 0)
#define SMB_LB_INT_STS_RESET	FIELD_PREP(SMB_LB_INT_STS_BUF_RESET_MSK, 0xf)

#define SMB_LB_PURGE_PURGED	BIT(0)

#define SMB_REG_ADDR_RES	0
#define SMB_BUF_ADDR_RES	1
#define SMB_BUF_ADDR_LO_MSK	GENMASK(31, 0)

/**
 * struct smb_data_buffer - Details of the buffer used by SMB
 * @buf_base:	Memory mapped base address of SMB.
 * @buf_hw_base:	SMB buffer start Physical base address, only used 32bits.
 * @buf_size:	Size of the buffer.
 * @data_size:	Size of the available trace data for SMB.
 * @buf_rdptr:	Current read position (index) within the buffer.
 */
struct smb_data_buffer {
	void *buf_base;
	u32 buf_hw_base;
	unsigned long buf_size;
	unsigned long data_size;
	unsigned long buf_rdptr;
};

/**
 * struct smb_drv_data - specifics associated to an SMB component
 * @base:	Memory mapped base address for SMB component.
 * @csdev:	Component vitals needed by the framework.
 * @sdb:	Data buffer for SMB.
 * @miscdev:	Specifics to handle "/dev/xyz.smb" entry.
 * @spinlock:	Control data access to one at a time.
 * @reading:	Synchronise user space access to SMB buffer.
 * @pid:	Process ID of the process being monitored by the
 *		session that is using this component.
 */
struct smb_drv_data {
	void __iomem *base;
	struct coresight_device	*csdev;
	struct smb_data_buffer sdb;
	struct miscdevice miscdev;
	spinlock_t spinlock;
	bool reading;
	pid_t pid;
};

#endif
