/*
 * Copyright © 2009 - Maxim Levitsky
 * driver for Ricoh xD readers
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/pci.h>
#include <linux/completion.h>
#include <linux/workqueue.h>
#include <linux/mtd/rawnand.h>
#include <linux/spinlock.h>


/* nand interface + ecc
   byte write/read does one cycle on nand data lines.
   dword write/read does 4 cycles
   if R852_CTL_ECC_ACCESS is set in R852_CTL, then dword read reads
   results of ecc correction, if DMA read was done before.
   If write was done two dword reads read generated ecc checksums
*/
#define	R852_DATALINE		0x00

/* control register */
#define R852_CTL		0x04
#define R852_CTL_COMMAND 	0x01	/* send command (#CLE)*/
#define R852_CTL_DATA		0x02	/* read/write data (#ALE)*/
#define R852_CTL_ON		0x04	/* only seem to controls the hd led, */
					/* but has to be set on start...*/
#define R852_CTL_RESET		0x08	/* unknown, set only on start once*/
#define R852_CTL_CARDENABLE	0x10	/* probably (#CE) - always set*/
#define R852_CTL_ECC_ENABLE	0x20	/* enable ecc engine */
#define R852_CTL_ECC_ACCESS	0x40	/* read/write ecc via reg #0*/
#define R852_CTL_WRITE		0x80	/* set when performing writes (#WP) */

/* card detection status */
#define R852_CARD_STA		0x05

#define R852_CARD_STA_CD	0x01	/* state of #CD line, same as 0x04 */
#define R852_CARD_STA_RO	0x02	/* card is readonly */
#define R852_CARD_STA_PRESENT	0x04	/* card is present (#CD) */
#define R852_CARD_STA_ABSENT	0x08	/* card is absent */
#define R852_CARD_STA_BUSY	0x80	/* card is busy - (#R/B) */

/* card detection irq status & enable*/
#define R852_CARD_IRQ_STA	0x06	/* IRQ status */
#define R852_CARD_IRQ_ENABLE	0x07	/* IRQ enable */

#define R852_CARD_IRQ_CD	0x01	/* fire when #CD lights, same as 0x04*/
#define R852_CARD_IRQ_REMOVE	0x04	/* detect card removal */
#define R852_CARD_IRQ_INSERT	0x08	/* detect card insert */
#define R852_CARD_IRQ_UNK1	0x10	/* unknown */
#define R852_CARD_IRQ_GENABLE	0x80	/* general enable */
#define R852_CARD_IRQ_MASK	0x1D



/* hardware enable */
#define R852_HW			0x08
#define R852_HW_ENABLED		0x01	/* hw enabled */
#define R852_HW_UNKNOWN		0x80


/* dma capabilities */
#define R852_DMA_CAP		0x09
#define R852_SMBIT		0x20	/* if set with bit #6 or bit #7, then */
					/* hw is smartmedia */
#define R852_DMA1		0x40	/* if set w/bit #7, dma is supported */
#define R852_DMA2		0x80	/* if set w/bit #6, dma is supported */


/* physical DMA address - 32 bit value*/
#define R852_DMA_ADDR		0x0C


/* dma settings */
#define R852_DMA_SETTINGS	0x10
#define R852_DMA_MEMORY		0x01	/* (memory <-> internal hw buffer) */
#define R852_DMA_READ		0x02	/* 0 = write, 1 = read */
#define R852_DMA_INTERNAL	0x04	/* (internal hw buffer <-> card) */

/* dma IRQ status */
#define R852_DMA_IRQ_STA		0x14

/* dma IRQ enable */
#define R852_DMA_IRQ_ENABLE	0x18

#define R852_DMA_IRQ_MEMORY	0x01	/* (memory <-> internal hw buffer) */
#define R852_DMA_IRQ_ERROR	0x02	/* error did happen */
#define R852_DMA_IRQ_INTERNAL	0x04	/* (internal hw buffer <-> card) */
#define R852_DMA_IRQ_MASK	0x07	/* mask of all IRQ bits */


/* ECC syndrome format - read from reg #0 will return two copies of these for
   each half of the page.
   first byte is error byte location, and second, bit location + flags */
#define R852_ECC_ERR_BIT_MSK	0x07	/* error bit location */
#define R852_ECC_CORRECT		0x10	/* no errors - (guessed) */
#define R852_ECC_CORRECTABLE	0x20	/* correctable error exist */
#define R852_ECC_FAIL		0x40	/* non correctable error detected */

#define R852_DMA_LEN		512

#define DMA_INTERNAL	0
#define DMA_MEMORY	1

struct r852_device {
	void __iomem *mmio;		/* mmio */
	struct nand_chip *chip;		/* nand chip backpointer */
	struct pci_dev *pci_dev;	/* pci backpointer */

	/* dma area */
	dma_addr_t phys_dma_addr;	/* bus address of buffer*/
	struct completion dma_done;	/* data transfer done */

	dma_addr_t phys_bounce_buffer;	/* bus address of bounce buffer */
	uint8_t *bounce_buffer;		/* virtual address of bounce buffer */

	int dma_dir;			/* 1 = read, 0 = write */
	int dma_stage;			/* 0 - idle, 1 - first step,
					   2 - second step */

	int dma_state;			/* 0 = internal, 1 = memory */
	int dma_error;			/* dma errors */
	int dma_usable;			/* is it possible to use dma */

	/* card status area */
	struct delayed_work card_detect_work;
	struct workqueue_struct *card_workqueue;
	int card_registred;		/* card registered with mtd */
	int card_detected;		/* card detected in slot */
	int card_unstable;		/* whenever the card is inserted,
					   is not known yet */
	int readonly;			/* card is readonly */
	int sm;				/* Is card smartmedia */

	/* interrupt handling */
	spinlock_t irqlock;		/* IRQ protecting lock */
	int irq;			/* irq num */
	/* misc */
	void *tmp_buffer;		/* temporary buffer */
	uint8_t ctlreg;			/* cached contents of control reg */
};

#define dbg(format, ...) \
	if (debug) \
		pr_debug(format "\n", ## __VA_ARGS__)

#define dbg_verbose(format, ...) \
	if (debug > 1) \
		pr_debug(format "\n", ## __VA_ARGS__)


#define message(format, ...) \
	pr_info(format "\n", ## __VA_ARGS__)
