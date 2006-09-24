/*
 * sgiseeq.h: Defines for the Seeq8003 ethernet controller.
 *
 * Copyright (C) 1996 David S. Miller (dm@engr.sgi.com)
 */
#ifndef _SGISEEQ_H
#define _SGISEEQ_H

struct sgiseeq_wregs {
	volatile unsigned int multicase_high[2];
	volatile unsigned int frame_gap;
	volatile unsigned int control;
};

struct sgiseeq_rregs {
	volatile unsigned int collision_tx[2];
	volatile unsigned int collision_all[2];
	volatile unsigned int _unused0;
	volatile unsigned int rflags;
};

struct sgiseeq_regs {
	union {
		volatile unsigned int eth_addr[6];
		volatile unsigned int multicast_low[6];
		struct sgiseeq_wregs wregs;
		struct sgiseeq_rregs rregs;
	} rw;
	volatile unsigned int rstat;
	volatile unsigned int tstat;
};

/* Seeq8003 receive status register */
#define SEEQ_RSTAT_OVERF   0x001 /* Overflow */
#define SEEQ_RSTAT_CERROR  0x002 /* CRC error */
#define SEEQ_RSTAT_DERROR  0x004 /* Dribble error */
#define SEEQ_RSTAT_SFRAME  0x008 /* Short frame */
#define SEEQ_RSTAT_REOF    0x010 /* Received end of frame */
#define SEEQ_RSTAT_FIG     0x020 /* Frame is good */
#define SEEQ_RSTAT_TIMEO   0x040 /* Timeout, or late receive */
#define SEEQ_RSTAT_WHICH   0x080 /* Which status, 1=old 0=new */
#define SEEQ_RSTAT_LITTLE  0x100 /* DMA is done in little endian format */
#define SEEQ_RSTAT_SDMA    0x200 /* DMA has started */
#define SEEQ_RSTAT_ADMA    0x400 /* DMA is active */
#define SEEQ_RSTAT_ROVERF  0x800 /* Receive buffer overflow */

/* Seeq8003 receive command register */
#define SEEQ_RCMD_RDISAB   0x000 /* Disable receiver on the Seeq8003 */
#define SEEQ_RCMD_IOVERF   0x001 /* IRQ on buffer overflows */
#define SEEQ_RCMD_ICRC     0x002 /* IRQ on CRC errors */
#define SEEQ_RCMD_IDRIB    0x004 /* IRQ on dribble errors */
#define SEEQ_RCMD_ISHORT   0x008 /* IRQ on short frames */
#define SEEQ_RCMD_IEOF     0x010 /* IRQ on end of frame */
#define SEEQ_RCMD_IGOOD    0x020 /* IRQ on good frames */
#define SEEQ_RCMD_RANY     0x040 /* Receive any frame */
#define SEEQ_RCMD_RBCAST   0x080 /* Receive broadcasts */
#define SEEQ_RCMD_RBMCAST  0x0c0 /* Receive broadcasts/multicasts */

/* Seeq8003 transmit status register */
#define SEEQ_TSTAT_UFLOW   0x001 /* Transmit buffer underflow */
#define SEEQ_TSTAT_CLS     0x002 /* Collision detected */
#define SEEQ_TSTAT_R16     0x004 /* Did 16 retries to tx a frame */
#define SEEQ_TSTAT_PTRANS  0x008 /* Packet was transmitted ok */
#define SEEQ_TSTAT_LCLS    0x010 /* Late collision occurred */
#define SEEQ_TSTAT_WHICH   0x080 /* Which status, 1=old 0=new */
#define SEEQ_TSTAT_TLE     0x100 /* DMA is done in little endian format */
#define SEEQ_TSTAT_SDMA    0x200 /* DMA has started */
#define SEEQ_TSTAT_ADMA    0x400 /* DMA is active */

/* Seeq8003 transmit command register */
#define SEEQ_TCMD_RB0      0x00 /* Register bank zero w/station addr */
#define SEEQ_TCMD_IUF      0x01 /* IRQ on tx underflow */
#define SEEQ_TCMD_IC       0x02 /* IRQ on collisions */
#define SEEQ_TCMD_I16      0x04 /* IRQ after 16 failed attempts to tx frame */
#define SEEQ_TCMD_IPT      0x08 /* IRQ when packet successfully transmitted */
#define SEEQ_TCMD_RB1      0x20 /* Register bank one w/multi-cast low byte */
#define SEEQ_TCMD_RB2      0x40 /* Register bank two w/multi-cast high byte */

/* Seeq8003 control register */
#define SEEQ_CTRL_XCNT     0x01
#define SEEQ_CTRL_ACCNT    0x02
#define SEEQ_CTRL_SFLAG    0x04
#define SEEQ_CTRL_EMULTI   0x08
#define SEEQ_CTRL_ESHORT   0x10
#define SEEQ_CTRL_ENCARR   0x20

/* Seeq8003 control registers on the SGI Hollywood HPC. */
#define SEEQ_HPIO_P1BITS  0x00000001 /* cycles to stay in P1 phase for PIO */
#define SEEQ_HPIO_P2BITS  0x00000060 /* cycles to stay in P2 phase for PIO */
#define SEEQ_HPIO_P3BITS  0x00000100 /* cycles to stay in P3 phase for PIO */
#define SEEQ_HDMA_D1BITS  0x00000006 /* cycles to stay in D1 phase for DMA */
#define SEEQ_HDMA_D2BITS  0x00000020 /* cycles to stay in D2 phase for DMA */
#define SEEQ_HDMA_D3BITS  0x00000000 /* cycles to stay in D3 phase for DMA */
#define SEEQ_HDMA_TIMEO   0x00030000 /* cycles for DMA timeout */
#define SEEQ_HCTL_NORM    0x00000000 /* Normal operation mode */
#define SEEQ_HCTL_RESET   0x00000001 /* Reset Seeq8003 and HPC interface */
#define SEEQ_HCTL_IPEND   0x00000002 /* IRQ is pending for the chip */
#define SEEQ_HCTL_IPG     0x00001000 /* Inter-packet gap */
#define SEEQ_HCTL_RFIX    0x00002000 /* At rxdc, clear end-of-packet */
#define SEEQ_HCTL_EFIX    0x00004000 /* fixes intr status bit settings */
#define SEEQ_HCTL_IFIX    0x00008000 /* enable startup timeouts */

#endif /* !(_SGISEEQ_H) */
