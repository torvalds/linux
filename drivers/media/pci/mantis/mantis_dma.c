/*
	Mantis PCI bridge driver

	Copyright (C) Manu Abraham (abraham.manu@gmail.com)

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

#include <linux/kernel.h>
#include <asm/page.h>
#include <linux/vmalloc.h>
#include <linux/pci.h>

#include <asm/irq.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/interrupt.h>

#include <media/dmxdev.h>
#include <media/dvbdev.h>
#include <media/dvb_demux.h>
#include <media/dvb_frontend.h>
#include <media/dvb_net.h>

#include "mantis_common.h"
#include "mantis_reg.h"
#include "mantis_dma.h"

#define RISC_WRITE		(0x01 << 28)
#define RISC_JUMP		(0x07 << 28)
#define RISC_IRQ		(0x01 << 24)

#define RISC_STATUS(status)	((((~status) & 0x0f) << 20) | ((status & 0x0f) << 16))
#define RISC_FLUSH(risc_pos)		(risc_pos = 0)
#define RISC_INSTR(risc_pos, opcode)	(mantis->risc_cpu[risc_pos++] = cpu_to_le32(opcode))

#define MANTIS_BUF_SIZE		(64 * 1024)
#define MANTIS_BLOCK_BYTES      (MANTIS_BUF_SIZE / 4)
#define MANTIS_DMA_TR_BYTES     (2 * 1024) /* upper limit: 4095 bytes. */
#define MANTIS_BLOCK_COUNT	(MANTIS_BUF_SIZE / MANTIS_BLOCK_BYTES)

#define MANTIS_DMA_TR_UNITS     (MANTIS_BLOCK_BYTES / MANTIS_DMA_TR_BYTES)
/* MANTIS_BUF_SIZE / MANTIS_DMA_TR_UNITS must not exceed MANTIS_RISC_SIZE (4k RISC cmd buffer) */
#define MANTIS_RISC_SIZE	PAGE_SIZE /* RISC program must fit here. */

int mantis_dma_exit(struct mantis_pci *mantis)
{
	if (mantis->buf_cpu) {
		dprintk(MANTIS_ERROR, 1,
			"DMA=0x%lx cpu=0x%p size=%d",
			(unsigned long) mantis->buf_dma,
			 mantis->buf_cpu,
			 MANTIS_BUF_SIZE);

		pci_free_consistent(mantis->pdev, MANTIS_BUF_SIZE,
				    mantis->buf_cpu, mantis->buf_dma);

		mantis->buf_cpu = NULL;
	}
	if (mantis->risc_cpu) {
		dprintk(MANTIS_ERROR, 1,
			"RISC=0x%lx cpu=0x%p size=%lx",
			(unsigned long) mantis->risc_dma,
			mantis->risc_cpu,
			MANTIS_RISC_SIZE);

		pci_free_consistent(mantis->pdev, MANTIS_RISC_SIZE,
				    mantis->risc_cpu, mantis->risc_dma);

		mantis->risc_cpu = NULL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mantis_dma_exit);

static inline int mantis_alloc_buffers(struct mantis_pci *mantis)
{
	if (!mantis->buf_cpu) {
		mantis->buf_cpu = pci_alloc_consistent(mantis->pdev,
						       MANTIS_BUF_SIZE,
						       &mantis->buf_dma);
		if (!mantis->buf_cpu) {
			dprintk(MANTIS_ERROR, 1,
				"DMA buffer allocation failed");

			goto err;
		}
		dprintk(MANTIS_ERROR, 1,
			"DMA=0x%lx cpu=0x%p size=%d",
			(unsigned long) mantis->buf_dma,
			mantis->buf_cpu, MANTIS_BUF_SIZE);
	}
	if (!mantis->risc_cpu) {
		mantis->risc_cpu = pci_alloc_consistent(mantis->pdev,
							MANTIS_RISC_SIZE,
							&mantis->risc_dma);

		if (!mantis->risc_cpu) {
			dprintk(MANTIS_ERROR, 1,
				"RISC program allocation failed");

			mantis_dma_exit(mantis);

			goto err;
		}
		dprintk(MANTIS_ERROR, 1,
			"RISC=0x%lx cpu=0x%p size=%lx",
			(unsigned long) mantis->risc_dma,
			mantis->risc_cpu, MANTIS_RISC_SIZE);
	}

	return 0;
err:
	dprintk(MANTIS_ERROR, 1, "Out of memory (?) .....");
	return -ENOMEM;
}

int mantis_dma_init(struct mantis_pci *mantis)
{
	int err;

	dprintk(MANTIS_DEBUG, 1, "Mantis DMA init");
	err = mantis_alloc_buffers(mantis);
	if (err < 0) {
		dprintk(MANTIS_ERROR, 1, "Error allocating DMA buffer");

		/* Stop RISC Engine */
		mmwrite(0, MANTIS_DMA_CTL);

		return err;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(mantis_dma_init);

static inline void mantis_risc_program(struct mantis_pci *mantis)
{
	u32 buf_pos = 0;
	u32 line, step;
	u32 risc_pos;

	dprintk(MANTIS_DEBUG, 1, "Mantis create RISC program");
	RISC_FLUSH(risc_pos);

	dprintk(MANTIS_DEBUG, 1, "risc len lines %u, bytes per line %u, bytes per DMA tr %u",
		MANTIS_BLOCK_COUNT, MANTIS_BLOCK_BYTES, MANTIS_DMA_TR_BYTES);

	for (line = 0; line < MANTIS_BLOCK_COUNT; line++) {
		for (step = 0; step < MANTIS_DMA_TR_UNITS; step++) {
			dprintk(MANTIS_DEBUG, 1, "RISC PROG line=[%d], step=[%d]", line, step);
			if (step == 0) {
				RISC_INSTR(risc_pos, RISC_WRITE	|
					   RISC_IRQ	|
					   RISC_STATUS(line) |
					   MANTIS_DMA_TR_BYTES);
			} else {
				RISC_INSTR(risc_pos, RISC_WRITE | MANTIS_DMA_TR_BYTES);
			}
			RISC_INSTR(risc_pos, mantis->buf_dma + buf_pos);
			buf_pos += MANTIS_DMA_TR_BYTES;
		  }
	}
	RISC_INSTR(risc_pos, RISC_JUMP);
	RISC_INSTR(risc_pos, mantis->risc_dma);
}

void mantis_dma_start(struct mantis_pci *mantis)
{
	dprintk(MANTIS_DEBUG, 1, "Mantis Start DMA engine");

	mantis_risc_program(mantis);
	mmwrite(mantis->risc_dma, MANTIS_RISC_START);
	mmwrite(mmread(MANTIS_GPIF_ADDR) | MANTIS_GPIF_HIFRDWRN, MANTIS_GPIF_ADDR);

	mmwrite(0, MANTIS_DMA_CTL);
	mantis->last_block = mantis->busy_block = 0;

	mantis_unmask_ints(mantis, MANTIS_INT_RISCI);

	mmwrite(MANTIS_FIFO_EN | MANTIS_DCAP_EN
			       | MANTIS_RISC_EN, MANTIS_DMA_CTL);

}

void mantis_dma_stop(struct mantis_pci *mantis)
{
	dprintk(MANTIS_DEBUG, 1, "Mantis Stop DMA engine");

	mmwrite((mmread(MANTIS_GPIF_ADDR) & (~(MANTIS_GPIF_HIFRDWRN))), MANTIS_GPIF_ADDR);

	mmwrite((mmread(MANTIS_DMA_CTL) & ~(MANTIS_FIFO_EN |
					    MANTIS_DCAP_EN |
					    MANTIS_RISC_EN)), MANTIS_DMA_CTL);

	mmwrite(mmread(MANTIS_INT_STAT), MANTIS_INT_STAT);

	mantis_mask_ints(mantis, MANTIS_INT_RISCI | MANTIS_INT_RISCEN);
}


void mantis_dma_xfer(unsigned long data)
{
	struct mantis_pci *mantis = (struct mantis_pci *) data;
	struct mantis_hwconfig *config = mantis->hwconfig;

	while (mantis->last_block != mantis->busy_block) {
		dprintk(MANTIS_DEBUG, 1, "last block=[%d] finished block=[%d]",
			mantis->last_block, mantis->busy_block);

		(config->ts_size ? dvb_dmx_swfilter_204 : dvb_dmx_swfilter)
		(&mantis->demux, &mantis->buf_cpu[mantis->last_block * MANTIS_BLOCK_BYTES], MANTIS_BLOCK_BYTES);
		mantis->last_block = (mantis->last_block + 1) % MANTIS_BLOCK_COUNT;
	}
}
