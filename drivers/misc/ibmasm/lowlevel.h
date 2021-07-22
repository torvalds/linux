/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * IBM ASM Service Processor Device Driver
 *
 * Copyright (C) IBM Corporation, 2004
 *
 * Author: Max Asböck <amax@us.ibm.com>
 */

/* Condor service processor specific hardware definitions */

#ifndef __IBMASM_CONDOR_H__
#define __IBMASM_CONDOR_H__

#include <asm/io.h>

#define VENDORID_IBM	0x1014
#define DEVICEID_RSA	0x010F

#define GET_MFA_ADDR(x)  (x & 0xFFFFFF00)

#define MAILBOX_FULL(x)  (x & 0x00000001)

#define NO_MFAS_AVAILABLE     0xFFFFFFFF


#define INBOUND_QUEUE_PORT   0x40  /* contains address of next free MFA */
#define OUTBOUND_QUEUE_PORT  0x44  /* contains address of posted MFA    */

#define SP_INTR_MASK	0x00000008
#define UART_INTR_MASK	0x00000010

#define INTR_STATUS_REGISTER   0x13A0
#define INTR_CONTROL_REGISTER  0x13A4

#define SCOUT_COM_A_BASE         0x0000
#define SCOUT_COM_B_BASE         0x0100
#define SCOUT_COM_C_BASE         0x0200
#define SCOUT_COM_D_BASE         0x0300

static inline int sp_interrupt_pending(void __iomem *base_address)
{
	return SP_INTR_MASK & readl(base_address + INTR_STATUS_REGISTER);
}

static inline int uart_interrupt_pending(void __iomem *base_address)
{
	return UART_INTR_MASK & readl(base_address + INTR_STATUS_REGISTER);
}

static inline void ibmasm_enable_interrupts(void __iomem *base_address, int mask)
{
	void __iomem *ctrl_reg = base_address + INTR_CONTROL_REGISTER;
	writel( readl(ctrl_reg) & ~mask, ctrl_reg);
}

static inline void ibmasm_disable_interrupts(void __iomem *base_address, int mask)
{
	void __iomem *ctrl_reg = base_address + INTR_CONTROL_REGISTER;
	writel( readl(ctrl_reg) | mask, ctrl_reg);
}

static inline void enable_sp_interrupts(void __iomem *base_address)
{
	ibmasm_enable_interrupts(base_address, SP_INTR_MASK);
}

static inline void disable_sp_interrupts(void __iomem *base_address)
{
	ibmasm_disable_interrupts(base_address, SP_INTR_MASK);
}

static inline void enable_uart_interrupts(void __iomem *base_address)
{
	ibmasm_enable_interrupts(base_address, UART_INTR_MASK);
}

static inline void disable_uart_interrupts(void __iomem *base_address)
{
	ibmasm_disable_interrupts(base_address, UART_INTR_MASK);
}

#define valid_mfa(mfa)	( (mfa) != NO_MFAS_AVAILABLE )

static inline u32 get_mfa_outbound(void __iomem *base_address)
{
	int retry;
	u32 mfa;

	for (retry=0; retry<=10; retry++) {
		mfa = readl(base_address + OUTBOUND_QUEUE_PORT);
		if (valid_mfa(mfa))
			break;
	}
	return mfa;
}

static inline void set_mfa_outbound(void __iomem *base_address, u32 mfa)
{
	writel(mfa, base_address + OUTBOUND_QUEUE_PORT);
}

static inline u32 get_mfa_inbound(void __iomem *base_address)
{
	u32 mfa = readl(base_address + INBOUND_QUEUE_PORT);

	if (MAILBOX_FULL(mfa))
		return 0;

	return mfa;
}

static inline void set_mfa_inbound(void __iomem *base_address, u32 mfa)
{
	writel(mfa, base_address + INBOUND_QUEUE_PORT);
}

static inline struct i2o_message *get_i2o_message(void __iomem *base_address, u32 mfa)
{
	return (struct i2o_message *)(GET_MFA_ADDR(mfa) + base_address);
}

#endif /* __IBMASM_CONDOR_H__ */
