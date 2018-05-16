/* SPDX-License-Identifier: GPL-2.0 */

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#ifndef _SFC_H
#define _SFC_H

#define SFC_VER_3		0x3 /* ver 3, else ver 1 */

#define SFC_MAX_IOSIZE		(1024 * 8)    /* 8K byte */
#define SFC_EN_INT		(0)         /* enable interrupt */
#define SFC_EN_DMA		(1)         /* enable dma */
#define SFC_FIFO_DEPTH		(0x10)      /* 16 words */

/* FIFO watermark */
#define SFC_RX_WMARK		(SFC_FIFO_DEPTH)	/* RX watermark level */
#define SFC_TX_WMARK		(SFC_FIFO_DEPTH)	/* TX watermark level */
#define SFC_RX_WMARK_SHIFT	(8)
#define SFC_TX_WMARK_SHIFT	(0)

/*return value*/
#define SFC_OK                      (0)
#define SFC_ERROR                   (-1)
#define SFC_PARAM_ERR               (-2)
#define SFC_TX_TIMEOUT              (-3)
#define SFC_RX_TIMEOUT              (-4)
#define SFC_WAIT_TIMEOUT            (-5)
#define SFC_BUSY_TIMEOUT            (-6)
#define SFC_ECC_FAIL                (-7)
#define SFC_PROG_FAIL               (-8)
#define SFC_ERASE_FAIL              (-9)

/* SFC_CMD Register */
#define SFC_ADDR_0BITS              (0)
#define SFC_ADDR_24BITS             (1)
#define SFC_ADDR_32BITS             (2)
#define SFC_ADDR_XBITS              (3)

#define SFC_WRITE                   (1)
#define SFC_READ                    (0)

/* SFC_CTRL Register */
#define SFC_1BITS_LINE              (0)
#define SFC_2BITS_LINE              (1)
#define SFC_4BITS_LINE              (2)

#define SFC_ENABLE_DMA              BIT(14)
#define sfc_delay(us)	udelay(us)

#define DMA_INT		BIT(7)      /* dma interrupt */
#define NSPIERR_INT	BIT(6)      /* Nspi error interrupt */
#define AHBERR_INT	BIT(5)      /* Ahb bus error interrupt */
#define FINISH_INT	BIT(4)      /* Transfer finish interrupt */
#define TXEMPTY_INT	BIT(3)      /* Tx fifo empty interrupt */
#define TXOF_INT	BIT(2)      /* Tx fifo overflow interrupt */
#define RXUF_INT	BIT(1)      /* Rx fifo underflow interrupt */
#define RXFULL_INT	BIT(0)      /* Rx fifo full interrupt */

/* SFC_FSR Register*/
#define SFC_RXFULL	BIT(3)      /* rx fifo full */
#define SFC_RXEMPTY	BIT(2)      /* rx fifo empty */
#define SFC_TXEMPTY	BIT(1)      /* tx fifo empty */
#define SFC_TXFULL	BIT(0)      /* tx fifo full */

/* SFC_RCVR Register */
#define SFC_RESET	BIT(0)     /* controller reset */

/* SFC_SR Register */
/* sfc busy flag. When busy, don't try to set the control register */
#define SFC_BUSY	BIT(0)

/* SFC_DMA_TRIGGER Register */
/* Dma start trigger signal. Auto cleared after write */
#define SFC_DMA_START	BIT(0)

#define SFC_CTRL	0x00
#define SFC_IMR		0x04
#define SFC_ICLR	0x08
#define SFC_FTLR	0x0C
#define SFC_RCVR	0x10
#define SFC_AX		0x14
#define SFC_ABIT	0x18
#define SFC_MASKISR	0x1C
#define SFC_FSR		0x20
#define SFC_SR		0x24
#define SFC_RAWISR	0x28
#define SFC_VER		0x2C
#define SFC_QOP		0x30
#define SFC_DMA_TRIGGER	0x80
#define SFC_DMA_ADDR	0x84
#define SFC_CMD		0x100
#define SFC_ADDR	0x104
#define SFC_DATA	0x108

union SFCFSR_DATA {
	u32 d32;
	struct {
		unsigned txempty : 1;
		unsigned txfull :  1;
		unsigned rxempty : 1;
		unsigned rxfull :  1;
		unsigned reserved7_4 : 4;
		unsigned txlevel : 5;
		unsigned reserved15_13 : 3;
		unsigned rxlevel : 5;
		unsigned reserved31_21 : 11;
	} b;
};

/*------------------------------ Global Typedefs -----------------------------*/
enum SFC_DATA_LINES {
	DATA_LINES_X1 = 0,
	DATA_LINES_X2,
	DATA_LINES_X4
};

union SFCCTRL_DATA {
	/* raw register data */
	u32 d32;
	/* register bits */
	struct {
		/* spi mode select */
		unsigned mode : 1;
		/*
		 * Shift in phase selection
		 * 0: shift in the flash data at posedge sclk_out
		 * 1: shift in the flash data at negedge sclk_out
		 */
		unsigned sps : 1;
		unsigned reserved3_2 : 2;
		/* sclk_idle_level_cycles */
		unsigned scic : 4;
		/* Cmd bits number */
		unsigned cmdlines : 2;
		/* Address bits number */
		unsigned addrlines : 2;
		/* Data bits number */
		unsigned datalines : 2;
		/* this bit is not exit in regiseter, just use for code param */
		unsigned enbledma : 1;
		unsigned reserved15 : 1;
		unsigned addrbits : 5;
		unsigned reserved31_21 : 11;
	} b;
};

union SFCCMD_DATA {
	/* raw register data */
	u32 d32;
	/* register bits */
	struct {
		/* Command that will send to Serial Flash */
		unsigned cmd : 8;
		/* Dummy bits number */
		unsigned dummybits : 4;
		/* 0: read, 1: write */
		unsigned rw : 1;
		/* Continuous read mode */
		unsigned readmode : 1;
		/* Address bits number */
		unsigned addrbits : 2;
		/* Transferred bytes number */
		unsigned datasize : 14;
		/* Chip select */
		unsigned cs : 2;
	} b;
};

int sfc_init(void __iomem *reg_addr);
int sfc_request(u32 sfcmd, u32 sfctrl, u32 addr, void *data);
u16 sfc_get_version(void);
void sfc_clean_irq(void);
int rksfc_get_reg_addr(unsigned long *p_sfc_addr);
void sfc_handle_irq(void);
unsigned long rksfc_dma_map_single(unsigned long ptr, int size, int dir);
void rksfc_dma_unmap_single(unsigned long ptr, int size, int dir);
void rksfc_irq_flag_init(void);
void rksfc_wait_for_irq_completed(void);
#endif
