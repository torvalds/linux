/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef	_PCI_CORE_H_
#define	_PCI_CORE_H_

#ifndef _LANGUAGE_ASSEMBLY

/* cpp contortions to concatenate w/arg prescan */
#ifndef PAD
#define	_PADLINE(line)	pad ## line
#define	_XSTR(line)	_PADLINE(line)
#define	PAD		_XSTR(__LINE__)
#endif

/* Sonics side: PCI core and host control registers */
struct sbpciregs {
	u32 control;		/* PCI control */
	u32 PAD[3];
	u32 arbcontrol;	/* PCI arbiter control */
	u32 clkrun;		/* Clkrun Control (>=rev11) */
	u32 PAD[2];
	u32 intstatus;	/* Interrupt status */
	u32 intmask;		/* Interrupt mask */
	u32 sbtopcimailbox;	/* Sonics to PCI mailbox */
	u32 PAD[9];
	u32 bcastaddr;	/* Sonics broadcast address */
	u32 bcastdata;	/* Sonics broadcast data */
	u32 PAD[2];
	u32 gpioin;		/* ro: gpio input (>=rev2) */
	u32 gpioout;		/* rw: gpio output (>=rev2) */
	u32 gpioouten;	/* rw: gpio output enable (>= rev2) */
	u32 gpiocontrol;	/* rw: gpio control (>= rev2) */
	u32 PAD[36];
	u32 sbtopci0;	/* Sonics to PCI translation 0 */
	u32 sbtopci1;	/* Sonics to PCI translation 1 */
	u32 sbtopci2;	/* Sonics to PCI translation 2 */
	u32 PAD[189];
	u32 pcicfg[4][64];	/* 0x400 - 0x7FF, PCI Cfg Space (>=rev8) */
	u16 sprom[36];	/* SPROM shadow Area */
	u32 PAD[46];
};

#endif				/* _LANGUAGE_ASSEMBLY */

/* PCI control */
#define PCI_RST_OE	0x01	/* When set, drives PCI_RESET out to pin */
#define PCI_RST		0x02	/* Value driven out to pin */
#define PCI_CLK_OE	0x04	/* When set, drives clock as gated by PCI_CLK out to pin */
#define PCI_CLK		0x08	/* Gate for clock driven out to pin */

/* PCI arbiter control */
#define PCI_INT_ARB	0x01	/* When set, use an internal arbiter */
#define PCI_EXT_ARB	0x02	/* When set, use an external arbiter */
/* ParkID - for PCI corerev >= 8 */
#define PCI_PARKID_MASK		0x1c	/* Selects which agent is parked on an idle bus */
#define PCI_PARKID_SHIFT	2
#define PCI_PARKID_EXT0		0	/* External master 0 */
#define PCI_PARKID_EXT1		1	/* External master 1 */
#define PCI_PARKID_EXT2		2	/* External master 2 */
#define PCI_PARKID_EXT3		3	/* External master 3 (rev >= 11) */
#define PCI_PARKID_INT		3	/* Internal master (rev < 11) */
#define PCI11_PARKID_INT	4	/* Internal master (rev >= 11) */
#define PCI_PARKID_LAST		4	/* Last active master (rev < 11) */
#define PCI11_PARKID_LAST	5	/* Last active master (rev >= 11) */

#define PCI_CLKRUN_DSBL	0x8000	/* Bit 15 forceClkrun */

/* Interrupt status/mask */
#define PCI_INTA	0x01	/* PCI INTA# is asserted */
#define PCI_INTB	0x02	/* PCI INTB# is asserted */
#define PCI_SERR	0x04	/* PCI SERR# has been asserted (write one to clear) */
#define PCI_PERR	0x08	/* PCI PERR# has been asserted (write one to clear) */
#define PCI_PME		0x10	/* PCI PME# is asserted */

/* (General) PCI/SB mailbox interrupts, two bits per pci function */
#define	MAILBOX_F0_0	0x100	/* function 0, int 0 */
#define	MAILBOX_F0_1	0x200	/* function 0, int 1 */
#define	MAILBOX_F1_0	0x400	/* function 1, int 0 */
#define	MAILBOX_F1_1	0x800	/* function 1, int 1 */
#define	MAILBOX_F2_0	0x1000	/* function 2, int 0 */
#define	MAILBOX_F2_1	0x2000	/* function 2, int 1 */
#define	MAILBOX_F3_0	0x4000	/* function 3, int 0 */
#define	MAILBOX_F3_1	0x8000	/* function 3, int 1 */

/* Sonics broadcast address */
#define BCAST_ADDR_MASK	0xff	/* Broadcast register address */

/* Sonics to PCI translation types */
#define SBTOPCI0_MASK	0xfc000000
#define SBTOPCI1_MASK	0xfc000000
#define SBTOPCI2_MASK	0xc0000000
#define SBTOPCI_MEM	0
#define SBTOPCI_IO	1
#define SBTOPCI_CFG0	2
#define SBTOPCI_CFG1	3
#define	SBTOPCI_PREF	0x4	/* prefetch enable */
#define	SBTOPCI_BURST	0x8	/* burst enable */
#define	SBTOPCI_RC_MASK		0x30	/* read command (>= rev11) */
#define	SBTOPCI_RC_READ		0x00	/* memory read */
#define	SBTOPCI_RC_READLINE	0x10	/* memory read line */
#define	SBTOPCI_RC_READMULTI	0x20	/* memory read multiple */

/* PCI core index in SROM shadow area */
#define SRSH_PI_OFFSET	0	/* first word */
#define SRSH_PI_MASK	0xf000	/* bit 15:12 */
#define SRSH_PI_SHIFT	12	/* bit 15:12 */

#endif				/* _PCI_CORE_H_ */
