/* Definitions for the 3Com 3c503 Etherlink 2. */
/* This file is distributed under the GPL.
   Many of these names and comments are directly from the Crynwr packet
   drivers, which are released under the GPL. */

#define EL2H (dev->base_addr + 0x400)
#define EL2L (dev->base_addr)

/* Vendor unique hardware addr. prefix. 3Com has 2 because they ran
   out of available addresses on the first one... */

#define OLD_3COM_ID	0x02608c
#define NEW_3COM_ID	0x0020af

/* Shared memory management parameters. NB: The 8 bit cards have only
   one bank (MB1) which serves both Tx and Rx packet space. The 16bit
   cards have 2 banks, MB0 for Tx packets, and MB1 for Rx packets. 
   You choose which bank appears in the sh. mem window with EGACFR_MBSn */

#define EL2_MB0_START_PG	(0x00)	/* EL2/16 Tx packets go in bank 0 */
#define EL2_MB1_START_PG	(0x20)	/* First page of bank 1 */
#define EL2_MB1_STOP_PG		(0x40)	/* Last page +1 of bank 1 */

/* 3Com 3c503 ASIC registers */
#define E33G_STARTPG	(EL2H+0)	/* Start page, matching EN0_STARTPG */
#define E33G_STOPPG	(EL2H+1)	/* Stop page, must match EN0_STOPPG */
#define E33G_DRQCNT	(EL2H+2)	/* DMA burst count */
#define E33G_IOBASE	(EL2H+3)	/* Read of I/O base jumpers. */
	/* (non-useful, but it also appears at the end of EPROM space) */
#define E33G_ROMBASE	(EL2H+4)	/* Read of memory base jumpers. */
#define E33G_GACFR	(EL2H+5)	/* Config/setup bits for the ASIC GA */
#define E33G_CNTRL	(EL2H+6)	/* Board's main control register */
#define E33G_STATUS	(EL2H+7)	/* Status on completions. */
#define E33G_IDCFR	(EL2H+8)	/* Interrupt/DMA config register */
				/* (Which IRQ to assert, DMA chan to use) */
#define E33G_DMAAH	(EL2H+9)	/* High byte of DMA address reg */
#define E33G_DMAAL	(EL2H+10)	/* Low byte of DMA address reg */
/* "Vector pointer" - if this address matches a read, the EPROM (rather than
   shared RAM) is mapped into memory space. */
#define E33G_VP2	(EL2H+11)
#define E33G_VP1	(EL2H+12)
#define E33G_VP0	(EL2H+13)
#define E33G_FIFOH	(EL2H+14)	/* FIFO for programmed I/O moves */
#define E33G_FIFOL	(EL2H+15)	/* ... low byte of above. */

/* Bits in E33G_CNTRL register: */

#define ECNTRL_RESET	(0x01)	/* Software reset of the ASIC and 8390 */
#define ECNTRL_THIN	(0x02)	/* Onboard xcvr enable, AUI disable */
#define ECNTRL_AUI	(0x00)	/* Onboard xcvr disable, AUI enable */
#define ECNTRL_SAPROM	(0x04)	/* Map the station address prom */
#define ECNTRL_DBLBFR	(0x20)	/* FIFO configuration bit */
#define ECNTRL_OUTPUT	(0x40)	/* PC-to-3C503 direction if 1 */
#define ECNTRL_INPUT	(0x00)	/* 3C503-to-PC direction if 0 */
#define ECNTRL_START	(0x80)	/* Start the DMA logic */

/* Bits in E33G_STATUS register: */

#define ESTAT_DPRDY	(0x80)	/* Data port (of FIFO) ready */
#define ESTAT_UFLW	(0x40)	/* Tried to read FIFO when it was empty */
#define ESTAT_OFLW	(0x20)	/* Tried to write FIFO when it was full */
#define ESTAT_DTC	(0x10)	/* Terminal Count from PC bus DMA logic */
#define ESTAT_DIP	(0x08)	/* DMA In Progress */

/* Bits in E33G_GACFR register: */

#define EGACFR_NIM	(0x80)	/* NIC interrupt mask */
#define EGACFR_TCM	(0x40)	/* DMA term. count interrupt mask */
#define EGACFR_RSEL	(0x08)	/* Map a bank of card mem into system mem */
#define EGACFR_MBS2	(0x04)	/* Memory bank select, bit 2. */
#define EGACFR_MBS1	(0x02)	/* Memory bank select, bit 1. */
#define EGACFR_MBS0	(0x01)	/* Memory bank select, bit 0. */

#define EGACFR_NORM	(0x49)	/* TCM | RSEL | MBS0 */
#define EGACFR_IRQOFF	(0xc9)	/* TCM | RSEL | MBS0 | NIM */

/*
	MBS2	MBS1	MBS0	Sh. mem windows card mem at:
	----	----	----	-----------------------------
	0	0	0	0x0000 -- bank 0
	0	0	1	0x2000 -- bank 1 (only choice for 8bit card)
	0	1	0	0x4000 -- bank 2, not used
	0	1	1	0x6000 -- bank 3, not used

There was going to be a 32k card that used bank 2 and 3, but it 
never got produced.

*/


/* End of 3C503 parameter definitions */
