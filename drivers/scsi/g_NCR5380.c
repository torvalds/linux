/*
 * Generic Generic NCR5380 driver
 *	
 * Copyright 1993, Drew Eckhardt
 *	Visionary Computing
 *	(Unix and Linux consulting and custom programming)
 *	drew@colorado.edu
 *      +1 (303) 440-4894
 *
 * NCR53C400 extensions (c) 1994,1995,1996, Kevin Lentin
 *    K.Lentin@cs.monash.edu.au
 *
 * NCR53C400A extensions (c) 1996, Ingmar Baumgart
 *    ingmar@gonzo.schwaben.de
 *
 * DTC3181E extensions (c) 1997, Ronald van Cuijlenborg
 * ronald.van.cuijlenborg@tip.nl or nutty@dds.nl
 *
 * Added ISAPNP support for DTC436 adapters,
 * Thomas Sailer, sailer@ife.ee.ethz.ch
 *
 * See Documentation/scsi/g_NCR5380.txt for more info.
 */

#include <asm/io.h>
#include <linux/blkdev.h>
#include <linux/module.h>
#include <scsi/scsi_host.h>
#include "g_NCR5380.h"
#include "NCR5380.h"
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/isapnp.h>
#include <linux/interrupt.h>

static int ncr_irq;
static int ncr_dma;
static int ncr_addr;
static int ncr_5380;
static int ncr_53c400;
static int ncr_53c400a;
static int dtc_3181e;
static int hp_c2502;

static struct card {
	NCR5380_map_type NCR5380_map_name;
	int irq;
	int dma;
	int board;		/* Use NCR53c400, Ricoh, etc. extensions ? */
} card;

#ifndef SCSI_G_NCR5380_MEM
/*
 * Configure I/O address of 53C400A or DTC436 by writing magic numbers
 * to ports 0x779 and 0x379.
 */
static void magic_configure(int idx, u8 irq, u8 magic[])
{
	u8 cfg = 0;

	outb(magic[0], 0x779);
	outb(magic[1], 0x379);
	outb(magic[2], 0x379);
	outb(magic[3], 0x379);
	outb(magic[4], 0x379);

	/* allowed IRQs for HP C2502 */
	if (irq != 2 && irq != 3 && irq != 4 && irq != 5 && irq != 7)
		irq = 0;
	if (idx >= 0 && idx <= 7)
		cfg = 0x80 | idx | (irq << 4);
	outb(cfg, 0x379);
}
#endif

/**
 * 	generic_NCR5380_detect	-	look for NCR5380 controllers
 *	@tpnt: the scsi template
 *
 *	Scan for the present of NCR5380, NCR53C400, NCR53C400A, DTC3181E
 *	and DTC436(ISAPnP) controllers.
 *
 *	Locks: none
 */

static int __init generic_NCR5380_detect(struct scsi_host_template *tpnt)
{
	unsigned int *ports;
	u8 *magic = NULL;
#ifndef SCSI_G_NCR5380_MEM
	int i;
	int port_idx = -1;
	unsigned long region_size;
#endif
	static unsigned int __initdata ncr_53c400a_ports[] = {
		0x280, 0x290, 0x300, 0x310, 0x330, 0x340, 0x348, 0x350, 0
	};
	static unsigned int __initdata dtc_3181e_ports[] = {
		0x220, 0x240, 0x280, 0x2a0, 0x2c0, 0x300, 0x320, 0x340, 0
	};
	static u8 ncr_53c400a_magic[] __initdata = {	/* 53C400A & DTC436 */
		0x59, 0xb9, 0xc5, 0xae, 0xa6
	};
	static u8 hp_c2502_magic[] __initdata = {	/* HP C2502 */
		0x0f, 0x22, 0xf0, 0x20, 0x80
	};
	int flags;
	struct Scsi_Host *instance;
	struct NCR5380_hostdata *hostdata;
#ifdef SCSI_G_NCR5380_MEM
	unsigned long base;
	void __iomem *iomem;
	resource_size_t iomem_size;
#endif

	if (ncr_irq)
		card.irq = ncr_irq;
	if (ncr_dma)
		card.dma = ncr_dma;
	if (ncr_addr)
		card.NCR5380_map_name = (NCR5380_map_type) ncr_addr;
	if (ncr_5380)
		card.board = BOARD_NCR5380;
	else if (ncr_53c400)
		card.board = BOARD_NCR53C400;
	else if (ncr_53c400a)
		card.board = BOARD_NCR53C400A;
	else if (dtc_3181e)
		card.board = BOARD_DTC3181E;
	else if (hp_c2502)
		card.board = BOARD_HP_C2502;
#ifndef SCSI_G_NCR5380_MEM
	if (isapnp_present()) {
		struct pnp_dev *dev = NULL;
		while ((dev = pnp_find_dev(NULL, ISAPNP_VENDOR('D', 'T', 'C'), ISAPNP_FUNCTION(0x436e), dev))) {
			if (pnp_device_attach(dev) < 0)
				continue;
			if (pnp_activate_dev(dev) < 0) {
				printk(KERN_ERR "dtc436e probe: activate failed\n");
				pnp_device_detach(dev);
				continue;
			}
			if (!pnp_port_valid(dev, 0)) {
				printk(KERN_ERR "dtc436e probe: no valid port\n");
				pnp_device_detach(dev);
				continue;
			}
			if (pnp_irq_valid(dev, 0))
				card.irq = pnp_irq(dev, 0);
			else
				card.irq = NO_IRQ;
			if (pnp_dma_valid(dev, 0))
				card.dma = pnp_dma(dev, 0);
			else
				card.dma = DMA_NONE;
			card.NCR5380_map_name = (NCR5380_map_type) pnp_port_start(dev, 0);
			card.board = BOARD_DTC3181E;
			break;
		}
	}
#endif

	if (!(card.NCR5380_map_name))
		return 0;

	ports = NULL;
	flags = 0;
	switch (card.board) {
	case BOARD_NCR5380:
		flags = FLAG_NO_PSEUDO_DMA | FLAG_DMA_FIXUP;
		break;
	case BOARD_NCR53C400A:
		ports = ncr_53c400a_ports;
		magic = ncr_53c400a_magic;
		break;
	case BOARD_HP_C2502:
		ports = ncr_53c400a_ports;
		magic = hp_c2502_magic;
		break;
	case BOARD_DTC3181E:
		ports = dtc_3181e_ports;
		magic = ncr_53c400a_magic;
		break;
	}

#ifndef SCSI_G_NCR5380_MEM
	if (ports && magic) {
		/* wakeup sequence for the NCR53C400A and DTC3181E */

		/* Disable the adapter and look for a free io port */
		magic_configure(-1, 0, magic);

		region_size = 16;

		if (card.NCR5380_map_name != PORT_AUTO)
			for (i = 0; ports[i]; i++) {
				if (!request_region(ports[i], region_size, "ncr53c80"))
					continue;
				if (card.NCR5380_map_name == ports[i])
					break;
				release_region(ports[i], region_size);
		} else
			for (i = 0; ports[i]; i++) {
				if (!request_region(ports[i], region_size, "ncr53c80"))
					continue;
				if (inb(ports[i]) == 0xff)
					break;
				release_region(ports[i], region_size);
			}
		if (ports[i]) {
			/* At this point we have our region reserved */
			magic_configure(i, 0, magic); /* no IRQ yet */
			outb(0xc0, ports[i] + 9);
			if (inb(ports[i] + 9) != 0x80)
				return 0;
			card.NCR5380_map_name = ports[i];
			port_idx = i;
		} else
			return 0;
	}
	else
	{
		/* Not a 53C400A style setup - just grab */
		region_size = 8;
		if (!request_region(card.NCR5380_map_name,
		                    region_size, "ncr5380"))
			return 0;
	}
#else
	base = card.NCR5380_map_name;
	iomem_size = NCR53C400_region_size;
	if (!request_mem_region(base, iomem_size, "ncr5380"))
		return 0;
	iomem = ioremap(base, iomem_size);
	if (!iomem) {
		release_mem_region(base, iomem_size);
		return 0;
	}
#endif
	instance = scsi_register(tpnt, sizeof(struct NCR5380_hostdata));
	if (instance == NULL)
		goto out_release;
	hostdata = shost_priv(instance);

#ifndef SCSI_G_NCR5380_MEM
	instance->io_port = card.NCR5380_map_name;
	instance->n_io_port = region_size;
	hostdata->io_width = 1; /* 8-bit PDMA by default */

	/*
	 * On NCR53C400 boards, NCR5380 registers are mapped 8 past
	 * the base address.
	 */
	switch (card.board) {
	case BOARD_NCR53C400:
		instance->io_port += 8;
		hostdata->c400_ctl_status = 0;
		hostdata->c400_blk_cnt = 1;
		hostdata->c400_host_buf = 4;
		break;
	case BOARD_DTC3181E:
		hostdata->io_width = 2;	/* 16-bit PDMA */
		/* fall through */
	case BOARD_NCR53C400A:
	case BOARD_HP_C2502:
		hostdata->c400_ctl_status = 9;
		hostdata->c400_blk_cnt = 10;
		hostdata->c400_host_buf = 8;
		break;
	}
#else
	instance->base = card.NCR5380_map_name;
	hostdata->iomem = iomem;
	hostdata->iomem_size = iomem_size;
	switch (card.board) {
	case BOARD_NCR53C400:
		hostdata->c400_ctl_status = 0x100;
		hostdata->c400_blk_cnt = 0x101;
		hostdata->c400_host_buf = 0x104;
		break;
	case BOARD_DTC3181E:
	case BOARD_NCR53C400A:
	case BOARD_HP_C2502:
		pr_err(DRV_MODULE_NAME ": unknown register offsets\n");
		goto out_unregister;
	}
#endif

	if (NCR5380_init(instance, flags | FLAG_LATE_DMA_SETUP))
		goto out_unregister;

	switch (card.board) {
	case BOARD_NCR53C400:
	case BOARD_DTC3181E:
	case BOARD_NCR53C400A:
	case BOARD_HP_C2502:
		NCR5380_write(hostdata->c400_ctl_status, CSR_BASE);
	}

	NCR5380_maybe_reset_bus(instance);

	if (card.irq != IRQ_AUTO)
		instance->irq = card.irq;
	else
		instance->irq = NCR5380_probe_irq(instance, 0xffff);

	/* Compatibility with documented NCR5380 kernel parameters */
	if (instance->irq == 255)
		instance->irq = NO_IRQ;

	if (instance->irq != NO_IRQ) {
#ifndef SCSI_G_NCR5380_MEM
		/* set IRQ for HP C2502 */
		if (card.board == BOARD_HP_C2502)
			magic_configure(port_idx, instance->irq, magic);
#endif
		if (request_irq(instance->irq, generic_NCR5380_intr,
				0, "NCR5380", instance)) {
			printk(KERN_WARNING "scsi%d : IRQ%d not free, interrupts disabled\n", instance->host_no, instance->irq);
			instance->irq = NO_IRQ;
		}
	}

	if (instance->irq == NO_IRQ) {
		printk(KERN_INFO "scsi%d : interrupts not enabled. for better interactive performance,\n", instance->host_no);
		printk(KERN_INFO "scsi%d : please jumper the board for a free IRQ.\n", instance->host_no);
	}

	return 1;

out_unregister:
	scsi_unregister(instance);
out_release:
#ifndef SCSI_G_NCR5380_MEM
	release_region(card.NCR5380_map_name, region_size);
#else
	iounmap(iomem);
	release_mem_region(base, iomem_size);
#endif
	return 0;
}

/**
 *	generic_NCR5380_release_resources	-	free resources
 *	@instance: host adapter to clean up 
 *
 *	Free the generic interface resources from this adapter.
 *
 *	Locks: none
 */
 
static int generic_NCR5380_release_resources(struct Scsi_Host *instance)
{
	if (instance->irq != NO_IRQ)
		free_irq(instance->irq, instance);
	NCR5380_exit(instance);
#ifndef SCSI_G_NCR5380_MEM
	release_region(instance->io_port, instance->n_io_port);
#else
	{
		struct NCR5380_hostdata *hostdata = shost_priv(instance);

		iounmap(hostdata->iomem);
		release_mem_region(instance->base, hostdata->iomem_size);
	}
#endif
	return 0;
}

/**
 *	generic_NCR5380_pread - pseudo DMA read
 *	@instance: adapter to read from
 *	@dst: buffer to read into
 *	@len: buffer length
 *
 *	Perform a pseudo DMA mode read from an NCR53C400 or equivalent
 *	controller
 */
 
static inline int generic_NCR5380_pread(struct Scsi_Host *instance,
                                        unsigned char *dst, int len)
{
	struct NCR5380_hostdata *hostdata = shost_priv(instance);
	int blocks = len / 128;
	int start = 0;

	NCR5380_write(hostdata->c400_ctl_status, CSR_BASE | CSR_TRANS_DIR);
	NCR5380_write(hostdata->c400_blk_cnt, blocks);
	while (1) {
		if (NCR5380_read(hostdata->c400_blk_cnt) == 0)
			break;
		if (NCR5380_read(hostdata->c400_ctl_status) & CSR_GATED_53C80_IRQ) {
			printk(KERN_ERR "53C400r: Got 53C80_IRQ start=%d, blocks=%d\n", start, blocks);
			return -1;
		}
		while (NCR5380_read(hostdata->c400_ctl_status) & CSR_HOST_BUF_NOT_RDY)
			; /* FIXME - no timeout */

#ifndef SCSI_G_NCR5380_MEM
		if (hostdata->io_width == 2)
			insw(instance->io_port + hostdata->c400_host_buf,
							dst + start, 64);
		else
			insb(instance->io_port + hostdata->c400_host_buf,
							dst + start, 128);
#else
		/* implies SCSI_G_NCR5380_MEM */
		memcpy_fromio(dst + start,
		              hostdata->iomem + NCR53C400_host_buffer, 128);
#endif
		start += 128;
		blocks--;
	}

	if (blocks) {
		while (NCR5380_read(hostdata->c400_ctl_status) & CSR_HOST_BUF_NOT_RDY)
			; /* FIXME - no timeout */

#ifndef SCSI_G_NCR5380_MEM
		if (hostdata->io_width == 2)
			insw(instance->io_port + hostdata->c400_host_buf,
							dst + start, 64);
		else
			insb(instance->io_port + hostdata->c400_host_buf,
							dst + start, 128);
#else
		/* implies SCSI_G_NCR5380_MEM */
		memcpy_fromio(dst + start,
		              hostdata->iomem + NCR53C400_host_buffer, 128);
#endif
		start += 128;
		blocks--;
	}

	if (!(NCR5380_read(hostdata->c400_ctl_status) & CSR_GATED_53C80_IRQ))
		printk("53C400r: no 53C80 gated irq after transfer");

	/* wait for 53C80 registers to be available */
	while (!(NCR5380_read(hostdata->c400_ctl_status) & CSR_53C80_REG))
		;

	if (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_END_DMA_TRANSFER))
		printk(KERN_ERR "53C400r: no end dma signal\n");
		
	return 0;
}

/**
 *	generic_NCR5380_pwrite - pseudo DMA write
 *	@instance: adapter to read from
 *	@dst: buffer to read into
 *	@len: buffer length
 *
 *	Perform a pseudo DMA mode read from an NCR53C400 or equivalent
 *	controller
 */

static inline int generic_NCR5380_pwrite(struct Scsi_Host *instance,
                                         unsigned char *src, int len)
{
	struct NCR5380_hostdata *hostdata = shost_priv(instance);
	int blocks = len / 128;
	int start = 0;

	NCR5380_write(hostdata->c400_ctl_status, CSR_BASE);
	NCR5380_write(hostdata->c400_blk_cnt, blocks);
	while (1) {
		if (NCR5380_read(hostdata->c400_ctl_status) & CSR_GATED_53C80_IRQ) {
			printk(KERN_ERR "53C400w: Got 53C80_IRQ start=%d, blocks=%d\n", start, blocks);
			return -1;
		}

		if (NCR5380_read(hostdata->c400_blk_cnt) == 0)
			break;
		while (NCR5380_read(hostdata->c400_ctl_status) & CSR_HOST_BUF_NOT_RDY)
			; // FIXME - timeout
#ifndef SCSI_G_NCR5380_MEM
		if (hostdata->io_width == 2)
			outsw(instance->io_port + hostdata->c400_host_buf,
							src + start, 64);
		else
			outsb(instance->io_port + hostdata->c400_host_buf,
							src + start, 128);
#else
		/* implies SCSI_G_NCR5380_MEM */
		memcpy_toio(hostdata->iomem + NCR53C400_host_buffer,
		            src + start, 128);
#endif
		start += 128;
		blocks--;
	}
	if (blocks) {
		while (NCR5380_read(hostdata->c400_ctl_status) & CSR_HOST_BUF_NOT_RDY)
			; // FIXME - no timeout

#ifndef SCSI_G_NCR5380_MEM
		if (hostdata->io_width == 2)
			outsw(instance->io_port + hostdata->c400_host_buf,
							src + start, 64);
		else
			outsb(instance->io_port + hostdata->c400_host_buf,
							src + start, 128);
#else
		/* implies SCSI_G_NCR5380_MEM */
		memcpy_toio(hostdata->iomem + NCR53C400_host_buffer,
		            src + start, 128);
#endif
		start += 128;
		blocks--;
	}

	/* wait for 53C80 registers to be available */
	while (!(NCR5380_read(hostdata->c400_ctl_status) & CSR_53C80_REG)) {
		udelay(4); /* DTC436 chip hangs without this */
		/* FIXME - no timeout */
	}

	if (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_END_DMA_TRANSFER)) {
		printk(KERN_ERR "53C400w: no end dma signal\n");
	}

	while (!(NCR5380_read(TARGET_COMMAND_REG) & TCR_LAST_BYTE_SENT))
		; 	// TIMEOUT
	return 0;
}

static int generic_NCR5380_dma_xfer_len(struct Scsi_Host *instance,
                                        struct scsi_cmnd *cmd)
{
	struct NCR5380_hostdata *hostdata = shost_priv(instance);
	int transfersize = cmd->transfersize;

	if (hostdata->flags & FLAG_NO_PSEUDO_DMA)
		return 0;

	/* Limit transfers to 32K, for xx400 & xx406
	 * pseudoDMA that transfers in 128 bytes blocks.
	 */
	if (transfersize > 32 * 1024 && cmd->SCp.this_residual &&
	    !(cmd->SCp.this_residual % transfersize))
		transfersize = 32 * 1024;

	/* 53C400 datasheet: non-modulo-128-byte transfers should use PIO */
	if (transfersize % 128)
		transfersize = 0;

	return transfersize;
}

/*
 *	Include the NCR5380 core code that we build our driver around	
 */
 
#include "NCR5380.c"

static struct scsi_host_template driver_template = {
	.proc_name		= DRV_MODULE_NAME,
	.name			= "Generic NCR5380/NCR53C400 SCSI",
	.detect			= generic_NCR5380_detect,
	.release		= generic_NCR5380_release_resources,
	.info			= generic_NCR5380_info,
	.queuecommand		= generic_NCR5380_queue_command,
	.eh_abort_handler	= generic_NCR5380_abort,
	.eh_bus_reset_handler	= generic_NCR5380_bus_reset,
	.can_queue		= 16,
	.this_id		= 7,
	.sg_tablesize		= SG_ALL,
	.cmd_per_lun		= 2,
	.use_clustering		= DISABLE_CLUSTERING,
	.cmd_size		= NCR5380_CMD_SIZE,
	.max_sectors		= 128,
};

#include "scsi_module.c"

module_param(ncr_irq, int, 0);
module_param(ncr_dma, int, 0);
module_param(ncr_addr, int, 0);
module_param(ncr_5380, int, 0);
module_param(ncr_53c400, int, 0);
module_param(ncr_53c400a, int, 0);
module_param(dtc_3181e, int, 0);
module_param(hp_c2502, int, 0);
MODULE_LICENSE("GPL");

#if !defined(SCSI_G_NCR5380_MEM) && defined(MODULE)
static struct isapnp_device_id id_table[] = {
	{
	 ISAPNP_ANY_ID, ISAPNP_ANY_ID,
	 ISAPNP_VENDOR('D', 'T', 'C'), ISAPNP_FUNCTION(0x436e),
	 0},
	{0}
};

MODULE_DEVICE_TABLE(isapnp, id_table);
#endif
