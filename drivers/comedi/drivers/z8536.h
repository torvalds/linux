/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Z8536 CIO Internal registers
 */

#ifndef _Z8536_H
#define _Z8536_H

/* Master Interrupt Control register */
#define Z8536_INT_CTRL_REG		0x00
#define Z8536_INT_CTRL_MIE		BIT(7)	/* Master Interrupt Enable */
#define Z8536_INT_CTRL_DLC		BIT(6)	/* Disable Lower Chain */
#define Z8536_INT_CTRL_NV		BIT(5)	/* No Vector */
#define Z8536_INT_CTRL_PA_VIS		BIT(4)	/* Port A Vect Inc Status */
#define Z8536_INT_CTRL_PB_VIS		BIT(3)	/* Port B Vect Inc Status */
#define Z8536_INT_CTRL_VT_VIS		BIT(2)	/* C/T Vect Inc Status */
#define Z8536_INT_CTRL_RJA		BIT(1)	/* Right Justified Addresses */
#define Z8536_INT_CTRL_RESET		BIT(0)	/* Reset */

/* Master Configuration Control register */
#define Z8536_CFG_CTRL_REG		0x01
#define Z8536_CFG_CTRL_PBE		BIT(7)	/* Port B Enable */
#define Z8536_CFG_CTRL_CT1E		BIT(6)	/* C/T 1 Enable */
#define Z8536_CFG_CTRL_CT2E		BIT(5)	/* C/T 2 Enable */
#define Z8536_CFG_CTRL_PCE_CT3E		BIT(4)	/* Port C & C/T 3 Enable */
#define Z8536_CFG_CTRL_PLC		BIT(3)	/* Port A/B Link Control */
#define Z8536_CFG_CTRL_PAE		BIT(2)	/* Port A Enable */
#define Z8536_CFG_CTRL_LC(x)		(((x) & 0x3) << 0)  /* Link Control */
#define Z8536_CFG_CTRL_LC_INDEP		Z8536_CFG_CTRL_LC(0)/* Independent */
#define Z8536_CFG_CTRL_LC_GATE		Z8536_CFG_CTRL_LC(1)/* 1 Gates 2 */
#define Z8536_CFG_CTRL_LC_TRIG		Z8536_CFG_CTRL_LC(2)/* 1 Triggers 2 */
#define Z8536_CFG_CTRL_LC_CLK		Z8536_CFG_CTRL_LC(3)/* 1 Clocks 2 */
#define Z8536_CFG_CTRL_LC_MASK		Z8536_CFG_CTRL_LC(3)

/* Interrupt Vector registers */
#define Z8536_PA_INT_VECT_REG		0x02
#define Z8536_PB_INT_VECT_REG		0x03
#define Z8536_CT_INT_VECT_REG		0x04
#define Z8536_CURR_INT_VECT_REG		0x1f

/* Port A/B & Counter/Timer 1/2/3 Command and Status registers */
#define Z8536_PA_CMDSTAT_REG		0x08
#define Z8536_PB_CMDSTAT_REG		0x09
#define Z8536_CT1_CMDSTAT_REG		0x0a
#define Z8536_CT2_CMDSTAT_REG		0x0b
#define Z8536_CT3_CMDSTAT_REG		0x0c
#define Z8536_CT_CMDSTAT_REG(x)		(0x0a + (x))
#define Z8536_CMD(x)			(((x) & 0x7) << 5)
#define Z8536_CMD_NULL			Z8536_CMD(0)	/* Null Code */
#define Z8536_CMD_CLR_IP_IUS		Z8536_CMD(1)	/* Clear IP & IUS */
#define Z8536_CMD_SET_IUS		Z8536_CMD(2)	/* Set IUS */
#define Z8536_CMD_CLR_IUS		Z8536_CMD(3)	/* Clear IUS */
#define Z8536_CMD_SET_IP		Z8536_CMD(4)	/* Set IP */
#define Z8536_CMD_CLR_IP		Z8536_CMD(5)	/* Clear IP */
#define Z8536_CMD_SET_IE		Z8536_CMD(6)	/* Set IE */
#define Z8536_CMD_CLR_IE		Z8536_CMD(7)	/* Clear IE */
#define Z8536_CMD_MASK			Z8536_CMD(7)

#define Z8536_STAT_IUS			BIT(7)	/* Interrupt Under Service */
#define Z8536_STAT_IE			BIT(6)	/* Interrupt Enable */
#define Z8536_STAT_IP			BIT(5)	/* Interrupt Pending */
#define Z8536_STAT_ERR			BIT(4)	/* Interrupt Error */
#define Z8536_STAT_IE_IP		(Z8536_STAT_IE | Z8536_STAT_IP)

#define Z8536_PAB_STAT_ORE		BIT(3)	/* Output Register Empty */
#define Z8536_PAB_STAT_IRF		BIT(2)	/* Input Register Full */
#define Z8536_PAB_STAT_PMF		BIT(1)	/* Pattern Match Flag */
#define Z8536_PAB_CMDSTAT_IOE		BIT(0)	/* Interrupt On Error */

#define Z8536_CT_CMD_RCC		BIT(3)	/* Read Counter Control */
#define Z8536_CT_CMDSTAT_GCB		BIT(2)	/* Gate Command Bit */
#define Z8536_CT_CMD_TCB		BIT(1)	/* Trigger Command Bit */
#define Z8536_CT_STAT_CIP		BIT(0)	/* Count In Progress */

/* Port Data registers */
#define Z8536_PA_DATA_REG		0x0d
#define Z8536_PB_DATA_REG		0x0e
#define Z8536_PC_DATA_REG		0x0f

/* Counter/Timer 1/2/3 Current Count registers */
#define Z8536_CT1_VAL_MSB_REG		0x10
#define Z8536_CT1_VAL_LSB_REG		0x11
#define Z8536_CT2_VAL_MSB_REG		0x12
#define Z8536_CT2_VAL_LSB_REG		0x13
#define Z8536_CT3_VAL_MSB_REG		0x14
#define Z8536_CT3_VAL_LSB_REG		0x15
#define Z8536_CT_VAL_MSB_REG(x)		(0x10 + ((x) * 2))
#define Z8536_CT_VAL_LSB_REG(x)		(0x11 + ((x) * 2))

/* Counter/Timer 1/2/3 Time Constant registers */
#define Z8536_CT1_RELOAD_MSB_REG	0x16
#define Z8536_CT1_RELOAD_LSB_REG	0x17
#define Z8536_CT2_RELOAD_MSB_REG	0x18
#define Z8536_CT2_RELOAD_LSB_REG	0x19
#define Z8536_CT3_RELOAD_MSB_REG	0x1a
#define Z8536_CT3_RELOAD_LSB_REG	0x1b
#define Z8536_CT_RELOAD_MSB_REG(x)	(0x16 + ((x) * 2))
#define Z8536_CT_RELOAD_LSB_REG(x)	(0x17 + ((x) * 2))

/* Counter/Timer 1/2/3 Mode Specification registers */
#define Z8536_CT1_MODE_REG		0x1c
#define Z8536_CT2_MODE_REG		0x1d
#define Z8536_CT3_MODE_REG		0x1e
#define Z8536_CT_MODE_REG(x)		(0x1c + (x))
#define Z8536_CT_MODE_CSC		BIT(7)	/* Continuous/Single Cycle */
#define Z8536_CT_MODE_EOE		BIT(6)	/* External Output Enable */
#define Z8536_CT_MODE_ECE		BIT(5)	/* External Count Enable */
#define Z8536_CT_MODE_ETE		BIT(4)	/* External Trigger Enable */
#define Z8536_CT_MODE_EGE		BIT(3)	/* External Gate Enable */
#define Z8536_CT_MODE_REB		BIT(2)	/* Retrigger Enable Bit */
#define Z8536_CT_MODE_DCS(x)		(((x) & 0x3) << 0)   /* Duty Cycle */
#define Z8536_CT_MODE_DCS_PULSE		Z8536_CT_MODE_DCS(0) /* Pulse */
#define Z8536_CT_MODE_DCS_ONESHOT	Z8536_CT_MODE_DCS(1) /* One-Shot */
#define Z8536_CT_MODE_DCS_SQRWAVE	Z8536_CT_MODE_DCS(2) /* Square Wave */
#define Z8536_CT_MODE_DCS_DO_NOT_USE	Z8536_CT_MODE_DCS(3) /* Do Not Use */
#define Z8536_CT_MODE_DCS_MASK		Z8536_CT_MODE_DCS(3)

/* Port A/B Mode Specification registers */
#define Z8536_PA_MODE_REG		0x20
#define Z8536_PB_MODE_REG		0x28
#define Z8536_PAB_MODE_PTS(x)		(((x) & 0x3) << 6)	/* Port type */
#define Z8536_PAB_MODE_PTS_BIT		Z8536_PAB_MODE_PTS(0 << 6)/* Bit */
#define Z8536_PAB_MODE_PTS_INPUT	Z8536_PAB_MODE_PTS(1 << 6)/* Input */
#define Z8536_PAB_MODE_PTS_OUTPUT	Z8536_PAB_MODE_PTS(2 << 6)/* Output */
#define Z8536_PAB_MODE_PTS_BIDIR	Z8536_PAB_MODE_PTS(3 << 6)/* Bidir */
#define Z8536_PAB_MODE_PTS_MASK		Z8536_PAB_MODE_PTS(3 << 6)
#define Z8536_PAB_MODE_ITB		BIT(5)	/* Interrupt on Two Bytes */
#define Z8536_PAB_MODE_SB		BIT(4)	/* Single Buffered mode */
#define Z8536_PAB_MODE_IMO		BIT(3)	/* Interrupt on Match Only */
#define Z8536_PAB_MODE_PMS(x)		(((x) & 0x3) << 1) /* Pattern Mode */
#define Z8536_PAB_MODE_PMS_DISABLE	Z8536_PAB_MODE_PMS(0)/* Disabled */
#define Z8536_PAB_MODE_PMS_AND		Z8536_PAB_MODE_PMS(1)/* "AND" */
#define Z8536_PAB_MODE_PMS_OR		Z8536_PAB_MODE_PMS(2)/* "OR" */
#define Z8536_PAB_MODE_PMS_OR_PEV	Z8536_PAB_MODE_PMS(3)/* "OR-Priority" */
#define Z8536_PAB_MODE_PMS_MASK		Z8536_PAB_MODE_PMS(3)
#define Z8536_PAB_MODE_LPM		BIT(0)	/* Latch on Pattern Match */
#define Z8536_PAB_MODE_DTE		BIT(0)	/* Deskew Timer Enabled */

/* Port A/B Handshake Specification registers */
#define Z8536_PA_HANDSHAKE_REG		0x21
#define Z8536_PB_HANDSHAKE_REG		0x29
#define Z8536_PAB_HANDSHAKE_HST(x)	(((x) & 0x3) << 6) /* Handshake Type */
#define Z8536_PAB_HANDSHAKE_HST_INTER	Z8536_PAB_HANDSHAKE_HST(0)/*Interlock*/
#define Z8536_PAB_HANDSHAKE_HST_STROBED	Z8536_PAB_HANDSHAKE_HST(1)/* Strobed */
#define Z8536_PAB_HANDSHAKE_HST_PULSED	Z8536_PAB_HANDSHAKE_HST(2)/* Pulsed */
#define Z8536_PAB_HANDSHAKE_HST_3WIRE	Z8536_PAB_HANDSHAKE_HST(3)/* 3-Wire */
#define Z8536_PAB_HANDSHAKE_HST_MASK	Z8536_PAB_HANDSHAKE_HST(3)
#define Z8536_PAB_HANDSHAKE_RWS(x)	(((x) & 0x7) << 3)	/* Req/Wait */
#define Z8536_PAB_HANDSHAKE_RWS_DISABLE	Z8536_PAB_HANDSHAKE_RWS(0)/* Disabled */
#define Z8536_PAB_HANDSHAKE_RWS_OUTWAIT	Z8536_PAB_HANDSHAKE_RWS(1)/* Out Wait */
#define Z8536_PAB_HANDSHAKE_RWS_INWAIT	Z8536_PAB_HANDSHAKE_RWS(3)/* In Wait */
#define Z8536_PAB_HANDSHAKE_RWS_SPREQ	Z8536_PAB_HANDSHAKE_RWS(4)/* Special */
#define Z8536_PAB_HANDSHAKE_RWS_OUTREQ	Z8536_PAB_HANDSHAKE_RWS(5)/* Out Req */
#define Z8536_PAB_HANDSHAKE_RWS_INREQ	Z8536_PAB_HANDSHAKE_RWS(7)/* In Req */
#define Z8536_PAB_HANDSHAKE_RWS_MASK	Z8536_PAB_HANDSHAKE_RWS(7)
#define Z8536_PAB_HANDSHAKE_DESKEW(x)	((x) << 0)/* Deskew Time */
#define Z8536_PAB_HANDSHAKE_DESKEW_MASK	(3 << 0)/* Deskew Time mask */

/*
 * Port A/B/C Data Path Polarity registers
 *
 *	0 = Non-Inverting
 *	1 = Inverting
 */
#define Z8536_PA_DPP_REG		0x22
#define Z8536_PB_DPP_REG		0x2a
#define Z8536_PC_DPP_REG		0x05

/*
 * Port A/B/C Data Direction registers
 *
 *	0 = Output bit
 *	1 = Input bit
 */
#define Z8536_PA_DD_REG			0x23
#define Z8536_PB_DD_REG			0x2b
#define Z8536_PC_DD_REG			0x06

/*
 * Port A/B/C Special I/O Control registers
 *
 *	0 = Normal Input or Output
 *	1 = Output with open drain or Input with 1's catcher
 */
#define Z8536_PA_SIO_REG		0x24
#define Z8536_PB_SIO_REG		0x2c
#define Z8536_PC_SIO_REG		0x07

/*
 * Port A/B Pattern Polarity/Transition/Mask registers
 *
 *	PM PT PP  Pattern Specification
 *	-- -- --  -------------------------------------
 *	 0  0  x  Bit masked off
 *	 0  1  x  Any transition
 *	 1  0  0  Zero (low-level)
 *	 1  0  1  One (high-level)
 *	 1  1  0  One-to-zero transition (falling-edge)
 *	 1  1  1  Zero-to-one transition (rising-edge)
 */
#define Z8536_PA_PP_REG			0x25
#define Z8536_PB_PP_REG			0x2d

#define Z8536_PA_PT_REG			0x26
#define Z8536_PB_PT_REG			0x2e

#define Z8536_PA_PM_REG			0x27
#define Z8536_PB_PM_REG			0x2f

#endif	/* _Z8536_H */
