/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License. See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * ioc.h: Definitions for SGI I/O Controller
 *
 * Copyright (C) 1996 David S. Miller
 * Copyright (C) 1997, 1998, 1999, 2000 Ralf Baechle
 * Copyright (C) 2001, 2003 Ladislav Michl
 */

#ifndef _SGI_IOC_H
#define _SGI_IOC_H

#include <linux/types.h>
#include <asm/sgi/pi1.h>

/*
 * All registers are 8-bit wide alligned on 32-bit boundary. Bad things
 * happen if you try word access them. You have been warned.
 */

struct sgioc_uart_regs {
	u8 _ctrl1[3];
	volatile u8 ctrl1;
	u8 _data1[3];
	volatile u8 data1;
	u8 _ctrl2[3];
	volatile u8 ctrl2;
	u8 _data2[3];
	volatile u8 data2;
};

struct sgioc_keyb_regs {
	u8 _data[3];
	volatile u8 data;
	u8 _command[3];
	volatile u8 command;
};

struct sgint_regs {
	u8 _istat0[3];
	volatile u8 istat0;		/* Interrupt status zero */
#define SGINT_ISTAT0_FFULL	0x01
#define SGINT_ISTAT0_SCSI0	0x02
#define SGINT_ISTAT0_SCSI1	0x04
#define SGINT_ISTAT0_ENET	0x08
#define SGINT_ISTAT0_GFXDMA	0x10
#define SGINT_ISTAT0_PPORT	0x20
#define SGINT_ISTAT0_HPC2	0x40
#define SGINT_ISTAT0_LIO2	0x80
	u8 _imask0[3];
	volatile u8 imask0;		/* Interrupt mask zero */
	u8 _istat1[3];
	volatile u8 istat1;		/* Interrupt status one */
#define SGINT_ISTAT1_ISDNI	0x01
#define SGINT_ISTAT1_PWR	0x02
#define SGINT_ISTAT1_ISDNH	0x04
#define SGINT_ISTAT1_LIO3	0x08
#define SGINT_ISTAT1_HPC3	0x10
#define SGINT_ISTAT1_AFAIL	0x20
#define SGINT_ISTAT1_VIDEO	0x40
#define SGINT_ISTAT1_GIO2	0x80
	u8 _imask1[3];
	volatile u8 imask1;		/* Interrupt mask one */
	u8 _vmeistat[3];
	volatile u8 vmeistat;		/* VME interrupt status */
	u8 _cmeimask0[3];
	volatile u8 cmeimask0;		/* VME interrupt mask zero */
	u8 _cmeimask1[3];
	volatile u8 cmeimask1;		/* VME interrupt mask one */
	u8 _cmepol[3];
	volatile u8 cmepol;		/* VME polarity */
	u8 _tclear[3];
	volatile u8 tclear;
	u8 _errstat[3];
	volatile u8 errstat;	/* Error status reg, reserved on INT2 */
	u32 _unused0[2];
	u8 _tcnt0[3];
	volatile u8 tcnt0;		/* counter 0 */
	u8 _tcnt1[3];
	volatile u8 tcnt1;		/* counter 1 */
	u8 _tcnt2[3];
	volatile u8 tcnt2;		/* counter 2 */
	u8 _tcword[3];
	volatile u8 tcword;		/* control word */
#define SGINT_TCWORD_BCD	0x01	/* Use BCD mode for counters */
#define SGINT_TCWORD_MMASK	0x0e	/* Mode bitmask. */
#define SGINT_TCWORD_MITC	0x00	/* IRQ on terminal count (doesn't work) */
#define SGINT_TCWORD_MOS	0x02	/* One-shot IRQ mode. */
#define SGINT_TCWORD_MRGEN	0x04	/* Normal rate generation */
#define SGINT_TCWORD_MSWGEN	0x06	/* Square wave generator mode */
#define SGINT_TCWORD_MSWST	0x08	/* Software strobe */
#define SGINT_TCWORD_MHWST	0x0a	/* Hardware strobe */
#define SGINT_TCWORD_CMASK	0x30	/* Command mask */
#define SGINT_TCWORD_CLAT	0x00	/* Latch command */
#define SGINT_TCWORD_CLSB	0x10	/* LSB read/write */
#define SGINT_TCWORD_CMSB	0x20	/* MSB read/write */
#define SGINT_TCWORD_CALL	0x30	/* Full counter read/write */
#define SGINT_TCWORD_CNT0	0x00	/* Select counter zero */
#define SGINT_TCWORD_CNT1	0x40	/* Select counter one */
#define SGINT_TCWORD_CNT2	0x80	/* Select counter two */
#define SGINT_TCWORD_CRBCK	0xc0	/* Readback command */
};

/*
 * The timer is the good old 8254.  Unlike in PCs it's clocked at exactly 1MHz
 */
#define SGINT_TIMER_CLOCK	1000000

/*
 * This is the constant we're using for calibrating the counter.
 */
#define SGINT_TCSAMP_COUNTER	((SGINT_TIMER_CLOCK / HZ) + 255)

/* We need software copies of these because they are write only. */
extern u8 sgi_ioc_reset, sgi_ioc_write;

struct sgioc_regs {
	struct pi1_regs pport;
	u32 _unused0[2];
	struct sgioc_uart_regs uart;
	struct sgioc_keyb_regs kbdmouse;
	u8 _gcsel[3];
	volatile u8 gcsel;
	u8 _genctrl[3];
	volatile u8 genctrl;
	u8 _panel[3];
	volatile u8 panel;
#define SGIOC_PANEL_POWERON	0x01
#define SGIOC_PANEL_POWERINTR	0x02
#define SGIOC_PANEL_VOLDNINTR	0x10
#define SGIOC_PANEL_VOLDNHOLD	0x20
#define SGIOC_PANEL_VOLUPINTR	0x40
#define SGIOC_PANEL_VOLUPHOLD	0x80
	u32 _unused1;
	u8 _sysid[3];
	volatile u8 sysid;
#define SGIOC_SYSID_FULLHOUSE	0x01
#define SGIOC_SYSID_BOARDREV(x)	(((x) & 0x1e) >> 1)
#define SGIOC_SYSID_CHIPREV(x)	(((x) & 0xe0) >> 5)
	u32 _unused2;
	u8 _read[3];
	volatile u8 read;
	u32 _unused3;
	u8 _dmasel[3];
	volatile u8 dmasel;
#define SGIOC_DMASEL_SCLK10MHZ	0x00	/* use 10MHZ serial clock */
#define SGIOC_DMASEL_ISDNB	0x01	/* enable isdn B */
#define SGIOC_DMASEL_ISDNA	0x02	/* enable isdn A */
#define SGIOC_DMASEL_PPORT	0x04	/* use parallel DMA */
#define SGIOC_DMASEL_SCLK667MHZ	0x10	/* use 6.67MHZ serial clock */
#define SGIOC_DMASEL_SCLKEXT	0x20	/* use external serial clock */
	u32 _unused4;
	u8 _reset[3];
	volatile u8 reset;
#define SGIOC_RESET_PPORT	0x01	/* 0=parport reset, 1=nornal */
#define SGIOC_RESET_KBDMOUSE	0x02	/* 0=kbdmouse reset, 1=normal */
#define SGIOC_RESET_EISA	0x04	/* 0=eisa reset, 1=normal */
#define SGIOC_RESET_ISDN	0x08	/* 0=isdn reset, 1=normal */
#define SGIOC_RESET_LC0OFF	0x10	/* guiness: turn led off (red, else green) */
#define SGIOC_RESET_LC1OFF	0x20	/* guiness: turn led off (green, else amber) */
	u32 _unused5;
	u8 _write[3];
	volatile u8 write;
#define SGIOC_WRITE_NTHRESH	0x01	/* use 4.5db threshhold */
#define SGIOC_WRITE_TPSPEED	0x02	/* use 100ohm TP speed */
#define SGIOC_WRITE_EPSEL	0x04	/* force cable mode: 1=AUI 0=TP */
#define SGIOC_WRITE_EASEL	0x08	/* 1=autoselect 0=manual cable selection */
#define SGIOC_WRITE_U1AMODE	0x10	/* 1=PC 0=MAC UART mode */
#define SGIOC_WRITE_U0AMODE	0x20	/* 1=PC 0=MAC UART mode */
#define SGIOC_WRITE_MLO		0x40	/* 1=4.75V 0=+5V */
#define SGIOC_WRITE_MHI		0x80	/* 1=5.25V 0=+5V */
	u32 _unused6;
	struct sgint_regs int3;
	u32 _unused7[16];
	volatile u32 extio;		/* FullHouse only */
#define EXTIO_S0_IRQ_3		0x8000	/* S0: vid.vsync */
#define EXTIO_S0_IRQ_2		0x4000	/* S0: gfx.fifofull */
#define EXTIO_S0_IRQ_1		0x2000	/* S0: gfx.int */
#define EXTIO_S0_RETRACE	0x1000
#define EXTIO_SG_IRQ_3		0x0800	/* SG: vid.vsync */
#define EXTIO_SG_IRQ_2		0x0400	/* SG: gfx.fifofull */
#define EXTIO_SG_IRQ_1		0x0200	/* SG: gfx.int */
#define EXTIO_SG_RETRACE	0x0100
#define EXTIO_GIO_33MHZ		0x0080
#define EXTIO_EISA_BUSERR	0x0040
#define EXTIO_MC_BUSERR		0x0020
#define EXTIO_HPC3_BUSERR	0x0010
#define EXTIO_S0_STAT_1		0x0008
#define EXTIO_S0_STAT_0		0x0004
#define EXTIO_SG_STAT_1		0x0002
#define EXTIO_SG_STAT_0		0x0001
};

extern struct sgioc_regs *sgioc;
extern struct sgint_regs *sgint;

#endif
