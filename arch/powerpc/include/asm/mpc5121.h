/*
 * MPC5121 Prototypes and definitions
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.
 */

#ifndef __ASM_POWERPC_MPC5121_H__
#define __ASM_POWERPC_MPC5121_H__

/* MPC512x Reset module registers */
struct mpc512x_reset_module {
	u32	rcwlr;	/* Reset Configuration Word Low Register */
	u32	rcwhr;	/* Reset Configuration Word High Register */
	u32	reserved1;
	u32	reserved2;
	u32	rsr;	/* Reset Status Register */
	u32	rmr;	/* Reset Mode Register */
	u32	rpr;	/* Reset Protection Register */
	u32	rcr;	/* Reset Control Register */
	u32	rcer;	/* Reset Control Enable Register */
};

/*
 * Clock Control Module
 */
struct mpc512x_ccm {
	u32	spmr;	/* System PLL Mode Register */
	u32	sccr1;	/* System Clock Control Register 1 */
	u32	sccr2;	/* System Clock Control Register 2 */
	u32	scfr1;	/* System Clock Frequency Register 1 */
	u32	scfr2;	/* System Clock Frequency Register 2 */
	u32	scfr2s;	/* System Clock Frequency Shadow Register 2 */
	u32	bcr;	/* Bread Crumb Register */
	u32	p0ccr;	/* PSC0 Clock Control Register */
	u32	p1ccr;	/* PSC1 CCR */
	u32	p2ccr;	/* PSC2 CCR */
	u32	p3ccr;	/* PSC3 CCR */
	u32	p4ccr;	/* PSC4 CCR */
	u32	p5ccr;	/* PSC5 CCR */
	u32	p6ccr;	/* PSC6 CCR */
	u32	p7ccr;	/* PSC7 CCR */
	u32	p8ccr;	/* PSC8 CCR */
	u32	p9ccr;	/* PSC9 CCR */
	u32	p10ccr;	/* PSC10 CCR */
	u32	p11ccr;	/* PSC11 CCR */
	u32	spccr;	/* SPDIF Clock Control Register */
	u32	cccr;	/* CFM Clock Control Register */
	u32	dccr;	/* DIU Clock Control Register */
	u32	m1ccr;	/* MSCAN1 CCR */
	u32	m2ccr;	/* MSCAN2 CCR */
	u32	m3ccr;	/* MSCAN3 CCR */
	u32	m4ccr;	/* MSCAN4 CCR */
	u8	res[0x98]; /* Reserved */
};

/*
 * LPC Module
 */
struct mpc512x_lpc {
	u32	cs_cfg[8];	/* CS config */
	u32	cs_ctrl;	/* CS Control Register */
	u32	cs_status;	/* CS Status Register */
	u32	burst_ctrl;	/* CS Burst Control Register */
	u32	deadcycle_ctrl;	/* CS Deadcycle Control Register */
	u32	holdcycle_ctrl;	/* CS Holdcycle Control Register */
	u32	alt;		/* Address Latch Timing Register */
};

int mpc512x_cs_config(unsigned int cs, u32 val);

#endif /* __ASM_POWERPC_MPC5121_H__ */
