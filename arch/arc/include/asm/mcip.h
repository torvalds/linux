/*
 * ARConnect IP Support (Multi core enabler: Cross core IPI, RTC ...)
 *
 * Copyright (C) 2014-15 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_MCIP_H
#define __ASM_MCIP_H

#ifdef CONFIG_ISA_ARCV2

#include <asm/arcregs.h>

#define ARC_REG_MCIP_BCR	0x0d0
#define ARC_REG_MCIP_CMD	0x600
#define ARC_REG_MCIP_WDATA	0x601
#define ARC_REG_MCIP_READBACK	0x602

struct mcip_cmd {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int pad:8, param:16, cmd:8;
#else
	unsigned int cmd:8, param:16, pad:8;
#endif

#define CMD_INTRPT_GENERATE_IRQ		0x01
#define CMD_INTRPT_GENERATE_ACK		0x02
#define CMD_INTRPT_READ_STATUS		0x03
#define CMD_INTRPT_CHECK_SOURCE		0x04

/* Semaphore Commands */
#define CMD_SEMA_CLAIM_AND_READ		0x11
#define CMD_SEMA_RELEASE		0x12

#define CMD_DEBUG_SET_MASK		0x34
#define CMD_DEBUG_SET_SELECT		0x36

#define CMD_GRTC_READ_LO		0x42
#define CMD_GRTC_READ_HI		0x43

#define CMD_IDU_ENABLE			0x71
#define CMD_IDU_DISABLE			0x72
#define CMD_IDU_SET_MODE		0x74
#define CMD_IDU_SET_DEST		0x76
#define CMD_IDU_SET_MASK		0x7C

#define IDU_M_TRIG_LEVEL		0x0
#define IDU_M_TRIG_EDGE			0x1

#define IDU_M_DISTRI_RR			0x0
#define IDU_M_DISTRI_DEST		0x2
};

/*
 * MCIP programming model
 *
 * - Simple commands write {cmd:8,param:16} to MCIP_CMD aux reg
 *   (param could be irq, common_irq, core_id ...)
 * - More involved commands setup MCIP_WDATA with cmd specific data
 *   before invoking the simple command
 */
static inline void __mcip_cmd(unsigned int cmd, unsigned int param)
{
	struct mcip_cmd buf;

	buf.pad = 0;
	buf.cmd = cmd;
	buf.param = param;

	WRITE_AUX(ARC_REG_MCIP_CMD, buf);
}

/*
 * Setup additional data for a cmd
 * Callers need to lock to ensure atomicity
 */
static inline void __mcip_cmd_data(unsigned int cmd, unsigned int param,
				   unsigned int data)
{
	write_aux_reg(ARC_REG_MCIP_WDATA, data);

	__mcip_cmd(cmd, param);
}

extern void mcip_init_early_smp(void);
extern void mcip_init_smp(unsigned int cpu);

#endif

#endif
