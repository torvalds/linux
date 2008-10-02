/* dvma.c:  Routines that are used to access DMA on the Sparc SBus.
 *
 * Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/string.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/delay.h>

#include <asm/oplib.h>
#include <asm/io.h>
#include <asm/dma.h>
#include <asm/sbus.h>

struct sbus_dma *dma_chain;

static void __init init_one_dvma(struct sbus_dma *dma, int num_dma)
{
	printk("dma%d: ", num_dma);
	
	dma->next = NULL;
	dma->running = 0;      /* No transfers going on as of yet */
	dma->allocated = 0;    /* No one has allocated us yet */
	switch(sbus_readl(dma->regs + DMA_CSR)&DMA_DEVICE_ID) {
	case DMA_VERS0:
		dma->revision = dvmarev0;
		printk("Revision 0 ");
		break;
	case DMA_ESCV1:
		dma->revision = dvmaesc1;
		printk("ESC Revision 1 ");
		break;
	case DMA_VERS1:
		dma->revision = dvmarev1;
		printk("Revision 1 ");
		break;
	case DMA_VERS2:
		dma->revision = dvmarev2;
		printk("Revision 2 ");
		break;
	case DMA_VERHME:
		dma->revision = dvmahme;
		printk("HME DVMA gate array ");
		break;
	case DMA_VERSPLUS:
		dma->revision = dvmarevplus;
		printk("Revision 1 PLUS ");
		break;
	default:
		printk("unknown dma version %08x",
		       sbus_readl(dma->regs + DMA_CSR) & DMA_DEVICE_ID);
		dma->allocated = 1;
		break;
	}
	printk("\n");
}

/* Probe this SBus DMA module(s) */
void __init dvma_init(struct sbus_bus *sbus)
{
	struct sbus_dev *this_dev;
	struct sbus_dma *dma;
	struct sbus_dma *dchain;
	static int num_dma = 0;

	for_each_sbusdev(this_dev, sbus) {
		char *name = this_dev->prom_name;
		int hme = 0;

		if(!strcmp(name, "SUNW,fas"))
			hme = 1;
		else if(strcmp(name, "dma") &&
			strcmp(name, "ledma") &&
			strcmp(name, "espdma"))
			continue;

		/* Found one... */
		dma = kmalloc(sizeof(struct sbus_dma), GFP_ATOMIC);

		dma->sdev = this_dev;

		/* Put at end of dma chain */
		dchain = dma_chain;
		if(dchain) {
			while(dchain->next)
				dchain = dchain->next;
			dchain->next = dma;
		} else {
			/* We're the first in line */
			dma_chain = dma;
		}

		dma->regs = sbus_ioremap(&dma->sdev->resource[0], 0,
					 dma->sdev->resource[0].end - dma->sdev->resource[0].start + 1,
					 "dma");

		dma->node = dma->sdev->prom_node;
		
		init_one_dvma(dma, num_dma++);
	}
}

#ifdef CONFIG_SUN4

#include <asm/sun4paddr.h>

void __init sun4_dvma_init(void)
{
	struct sbus_dma *dma;
	struct resource r;

	if(sun4_dma_physaddr) {
		dma = kmalloc(sizeof(struct sbus_dma), GFP_ATOMIC);

		/* No SBUS */
		dma->sdev = NULL;

		/* Only one DMA device */
		dma_chain = dma;

		memset(&r, 0, sizeof(r));
		r.start = sun4_dma_physaddr;
		dma->regs = sbus_ioremap(&r, 0, PAGE_SIZE, "dma");

		/* No prom node */
		dma->node = 0x0;

		init_one_dvma(dma, 0);
	} else {
	  	dma_chain = NULL;
	}
}

#endif
