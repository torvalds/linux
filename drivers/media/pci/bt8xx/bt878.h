/*
    bt878.h - Bt878 audio module (register offsets)

    Copyright (C) 2002 Peter Hettkamp <peter.hettkamp@htp-tel.de>

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef _BT878_H_
#define _BT878_H_

#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/sched.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>

#include "bt848.h"
#include "bttv.h"

#define BT878_VERSION_CODE 0x000000

#define BT878_AINT_STAT		0x100
#define BT878_ARISCS		(0xf<<28)
#define BT878_ARISC_EN		(1<<27)
#define BT878_ASCERR		(1<<19)
#define BT878_AOCERR		(1<<18)
#define BT878_APABORT		(1<<17)
#define BT878_ARIPERR		(1<<16)
#define BT878_APPERR		(1<<15)
#define BT878_AFDSR		(1<<14)
#define BT878_AFTRGT		(1<<13)
#define BT878_AFBUS		(1<<12)
#define BT878_ARISCI		(1<<11)
#define BT878_AOFLOW		(1<<3)

#define BT878_AINT_MASK		0x104

#define BT878_AGPIO_DMA_CTL	0x10c
#define BT878_A_GAIN		(0xf<<28)
#define BT878_A_G2X		(1<<27)
#define BT878_A_PWRDN		(1<<26)
#define BT878_A_SEL		(3<<24)
#define BT878_DA_SCE		(1<<23)
#define BT878_DA_LRI		(1<<22)
#define BT878_DA_MLB		(1<<21)
#define BT878_DA_LRD		(0x1f<<16)
#define BT878_DA_DPM		(1<<15)
#define BT878_DA_SBR		(1<<14)
#define BT878_DA_ES2		(1<<13)
#define BT878_DA_LMT		(1<<12)
#define BT878_DA_SDR		(0xf<<8)
#define BT878_DA_IOM		(3<<6)
#define BT878_DA_APP		(1<<5)
#define BT878_ACAP_EN		(1<<4)
#define BT878_PKTP		(3<<2)
#define BT878_RISC_EN		(1<<1)
#define BT878_FIFO_EN		1

#define BT878_APACK_LEN		0x110
#define BT878_AFP_LEN		(0xff<<16)
#define BT878_ALP_LEN		0xfff

#define BT878_ARISC_START	0x114

#define BT878_ARISC_PC		0x120

/* BT878 FUNCTION 0 REGISTERS */
#define BT878_GPIO_DMA_CTL	0x10c

/* Interrupt register */
#define BT878_INT_STAT		0x100
#define BT878_INT_MASK		0x104
#define BT878_I2CRACK		(1<<25)
#define BT878_I2CDONE		(1<<8)

#define BT878_MAX 4

#define BT878_RISC_SYNC_MASK	(1 << 15)


#define BTTV_BOARD_UNKNOWN                 0x00
#define BTTV_BOARD_PINNACLESAT             0x5e
#define BTTV_BOARD_NEBULA_DIGITV           0x68
#define BTTV_BOARD_PC_HDTV                 0x70
#define BTTV_BOARD_TWINHAN_DST             0x71
#define BTTV_BOARD_AVDVBT_771              0x7b
#define BTTV_BOARD_AVDVBT_761              0x7c
#define BTTV_BOARD_DVICO_DVBT_LITE         0x80
#define BTTV_BOARD_DVICO_FUSIONHDTV_5_LITE 0x87

extern int bt878_num;

struct bt878 {
	struct mutex gpio_lock;
	unsigned int nr;
	unsigned int bttv_nr;
	struct i2c_adapter *adapter;
	struct pci_dev *dev;
	unsigned int id;
	unsigned int TS_Size;
	unsigned char revision;
	unsigned int irq;
	unsigned long bt878_adr;
	volatile void __iomem *bt878_mem; /* function 1 */

	volatile u32 finished_block;
	volatile u32 last_block;
	u32 block_count;
	u32 block_bytes;
	u32 line_bytes;
	u32 line_count;

	u32 buf_size;
	u8 *buf_cpu;
	dma_addr_t buf_dma;

	u32 risc_size;
	__le32 *risc_cpu;
	dma_addr_t risc_dma;
	u32 risc_pos;

	struct tasklet_struct tasklet;
	int shutdown;
};

extern struct bt878 bt878[BT878_MAX];

void bt878_start(struct bt878 *bt, u32 controlreg, u32 op_sync_orin,
		u32 irq_err_ignore);
void bt878_stop(struct bt878 *bt);

#define bmtwrite(dat,adr)  writel((dat), (adr))
#define bmtread(adr)       readl(adr)

#endif
