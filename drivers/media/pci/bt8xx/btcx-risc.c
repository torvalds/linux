// SPDX-License-Identifier: GPL-2.0-or-later
/*

    btcx-risc.c

    bt848/bt878/cx2388x risc code generator.

    (c) 2000-03 Gerd Knorr <kraxel@bytesex.org> [SuSE Labs]


*/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/videodev2.h>
#include <linux/pgtable.h>
#include <asm/page.h>

#include "btcx-risc.h"

static unsigned int btcx_debug;
module_param(btcx_debug, int, 0644);
MODULE_PARM_DESC(btcx_debug,"debug messages, default is 0 (no)");

#define dprintk(fmt, arg...) do {				\
	if (btcx_debug)						\
		printk(KERN_DEBUG pr_fmt("%s: " fmt),		\
		       __func__, ##arg);			\
} while (0)


/* ---------------------------------------------------------- */
/* allocate/free risc memory                                  */

static int memcnt;

void btcx_riscmem_free(struct pci_dev *pci,
		       struct btcx_riscmem *risc)
{
	if (NULL == risc->cpu)
		return;

	memcnt--;
	dprintk("btcx: riscmem free [%d] dma=%lx\n",
		memcnt, (unsigned long)risc->dma);

	dma_free_coherent(&pci->dev, risc->size, risc->cpu, risc->dma);
	memset(risc,0,sizeof(*risc));
}

int btcx_riscmem_alloc(struct pci_dev *pci,
		       struct btcx_riscmem *risc,
		       unsigned int size)
{
	__le32 *cpu;
	dma_addr_t dma = 0;

	if (NULL != risc->cpu && risc->size < size)
		btcx_riscmem_free(pci,risc);
	if (NULL == risc->cpu) {
		cpu = dma_alloc_coherent(&pci->dev, size, &dma, GFP_KERNEL);
		if (NULL == cpu)
			return -ENOMEM;
		risc->cpu  = cpu;
		risc->dma  = dma;
		risc->size = size;

		memcnt++;
		dprintk("btcx: riscmem alloc [%d] dma=%lx cpu=%p size=%d\n",
			memcnt, (unsigned long)dma, cpu, size);
	}
	return 0;
}
