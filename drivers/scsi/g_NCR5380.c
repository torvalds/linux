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
 */

/* 
 * TODO : flesh out DMA support, find some one actually using this (I have
 * 	a memory mapped Trantor board that works fine)
 */

/*
 * The card is detected and initialized in one of several ways : 
 * 1.  With command line overrides - NCR5380=port,irq may be 
 *     used on the LILO command line to override the defaults.
 *
 * 2.  With the GENERIC_NCR5380_OVERRIDE compile time define.  This is 
 *     specified as an array of address, irq, dma, board tuples.  Ie, for
 *     one board at 0x350, IRQ5, no dma, I could say  
 *     -DGENERIC_NCR5380_OVERRIDE={{0xcc000, 5, DMA_NONE, BOARD_NCR5380}}
 * 
 * -1 should be specified for no or DMA interrupt, -2 to autoprobe for an 
 * 	IRQ line if overridden on the command line.
 *
 * 3.  When included as a module, with arguments passed on the command line:
 *         ncr_irq=xx	the interrupt
 *         ncr_addr=xx  the port or base address (for port or memory
 *              	mapped, resp.)
 *         ncr_dma=xx	the DMA
 *         ncr_5380=1	to set up for a NCR5380 board
 *         ncr_53c400=1	to set up for a NCR53C400 board
 *     e.g.
 *     modprobe g_NCR5380 ncr_irq=5 ncr_addr=0x350 ncr_5380=1
 *       for a port mapped NCR5380 board or
 *     modprobe g_NCR5380 ncr_irq=255 ncr_addr=0xc8000 ncr_53c400=1
 *       for a memory mapped NCR53C400 board with interrupts disabled.
 * 
 * 255 should be specified for no or DMA interrupt, 254 to autoprobe for an 
 * 	IRQ line if overridden on the command line.
 *     
 */

/* settings for DTC3181E card with only Mustek scanner attached */
#define USLEEP_POLL	msecs_to_jiffies(10)
#define USLEEP_SLEEP	msecs_to_jiffies(200)
#define USLEEP_WAITLONG	msecs_to_jiffies(5000)

#define AUTOPROBE_IRQ

#ifdef CONFIG_SCSI_GENERIC_NCR53C400
#define NCR53C400_PSEUDO_DMA 1
#define PSEUDO_DMA
#define NCR53C400
#endif

#include <asm/io.h>
#include <linux/signal.h>
#include <linux/blkdev.h>
#include <scsi/scsi_host.h>
#include "g_NCR5380.h"
#include "NCR5380.h"
#include <linux/stat.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/isapnp.h>
#include <linux/delay.h>
#include <linux/interrupt.h>

#define NCR_NOT_SET 0
static int ncr_irq = NCR_NOT_SET;
static int ncr_dma = NCR_NOT_SET;
static int ncr_addr = NCR_NOT_SET;
static int ncr_5380 = NCR_NOT_SET;
static int ncr_53c400 = NCR_NOT_SET;
static int ncr_53c400a = NCR_NOT_SET;
static int dtc_3181e = NCR_NOT_SET;

static struct override {
	NCR5380_map_type NCR5380_map_name;
	int irq;
	int dma;
	int board;		/* Use NCR53c400, Ricoh, etc. extensions ? */
} overrides
#ifdef GENERIC_NCR5380_OVERRIDE
[] __initdata = GENERIC_NCR5380_OVERRIDE;
#else
[1] __initdata = { { 0,},};
#endif

#define NO_OVERRIDES ARRAY_SIZE(overrides)

#ifndef MODULE

/**
 *	internal_setup		-	handle lilo command string override
 *	@board:	BOARD_* identifier for the board
 *	@str: unused
 *	@ints: numeric parameters
 *
 * 	Do LILO command line initialization of the overrides array. Display
 *	errors when needed
 *
 *	Locks: none
 */

static void __init internal_setup(int board, char *str, int *ints)
{
	static int commandline_current = 0;
	switch (board) {
	case BOARD_NCR5380:
		if (ints[0] != 2 && ints[0] != 3) {
			printk(KERN_ERR "generic_NCR5380_setup : usage ncr5380=" STRVAL(NCR5380_map_name) ",irq,dma\n");
			return;
		}
		break;
	case BOARD_NCR53C400:
		if (ints[0] != 2) {
			printk(KERN_ERR "generic_NCR53C400_setup : usage ncr53c400=" STRVAL(NCR5380_map_name) ",irq\n");
			return;
		}
		break;
	case BOARD_NCR53C400A:
		if (ints[0] != 2) {
			printk(KERN_ERR "generic_NCR53C400A_setup : usage ncr53c400a=" STRVAL(NCR5380_map_name) ",irq\n");
			return;
		}
		break;
	case BOARD_DTC3181E:
		if (ints[0] != 2) {
			printk("generic_DTC3181E_setup : usage dtc3181e=" STRVAL(NCR5380_map_name) ",irq\n");
			return;
		}
		break;
	}

	if (commandline_current < NO_OVERRIDES) {
		overrides[commandline_current].NCR5380_map_name = (NCR5380_map_type) ints[1];
		overrides[commandline_current].irq = ints[2];
		if (ints[0] == 3)
			overrides[commandline_current].dma = ints[3];
		else
			overrides[commandline_current].dma = DMA_NONE;
		overrides[commandline_current].board = board;
		++commandline_current;
	}
}


/**
 * 	do_NCR53C80_setup		-	set up entry point
 *	@str: unused
 *
 *	Setup function invoked at boot to parse the ncr5380= command
 *	line.
 */

static int __init do_NCR5380_setup(char *str)
{
	int ints[10];

	get_options(str, ARRAY_SIZE(ints), ints);
	internal_setup(BOARD_NCR5380, str, ints);
	return 1;
}

/**
 * 	do_NCR53C400_setup		-	set up entry point
 *	@str: unused
 *	@ints: integer parameters from kernel setup code
 *
 *	Setup function invoked at boot to parse the ncr53c400= command
 *	line.
 */

static int __init do_NCR53C400_setup(char *str)
{
	int ints[10];

	get_options(str, ARRAY_SIZE(ints), ints);
	internal_setup(BOARD_NCR53C400, str, ints);
	return 1;
}

/**
 * 	do_NCR53C400A_setup	-	set up entry point
 *	@str: unused
 *	@ints: integer parameters from kernel setup code
 *
 *	Setup function invoked at boot to parse the ncr53c400a= command
 *	line.
 */

static int __init do_NCR53C400A_setup(char *str)
{
	int ints[10];

	get_options(str, ARRAY_SIZE(ints), ints);
	internal_setup(BOARD_NCR53C400A, str, ints);
	return 1;
}

/**
 * 	do_DTC3181E_setup	-	set up entry point
 *	@str: unused
 *	@ints: integer parameters from kernel setup code
 *
 *	Setup function invoked at boot to parse the dtc3181e= command
 *	line.
 */

static int __init do_DTC3181E_setup(char *str)
{
	int ints[10];

	get_options(str, ARRAY_SIZE(ints), ints);
	internal_setup(BOARD_DTC3181E, str, ints);
	return 1;
}

#endif

/**
 * 	generic_NCR5380_detect	-	look for NCR5380 controllers
 *	@tpnt: the scsi template
 *
 *	Scan for the present of NCR5380, NCR53C400, NCR53C400A, DTC3181E
 *	and DTC436(ISAPnP) controllers. If overrides have been set we use
 *	them.
 *
 *	The caller supplied NCR5380_init function is invoked from here, before
 *	the interrupt line is taken.
 *
 *	Locks: none
 */

static int __init generic_NCR5380_detect(struct scsi_host_template *tpnt)
{
	static int current_override = 0;
	int count;
	unsigned int *ports;
#ifndef SCSI_G_NCR5380_MEM
	int i;
	unsigned long region_size = 16;
#endif
	static unsigned int __initdata ncr_53c400a_ports[] = {
		0x280, 0x290, 0x300, 0x310, 0x330, 0x340, 0x348, 0x350, 0
	};
	static unsigned int __initdata dtc_3181e_ports[] = {
		0x220, 0x240, 0x280, 0x2a0, 0x2c0, 0x300, 0x320, 0x340, 0
	};
	int flags = 0;
	struct Scsi_Host *instance;
#ifdef SCSI_G_NCR5380_MEM
	unsigned long base;
	void __iomem *iomem;
#endif

	if (ncr_irq != NCR_NOT_SET)
		overrides[0].irq = ncr_irq;
	if (ncr_dma != NCR_NOT_SET)
		overrides[0].dma = ncr_dma;
	if (ncr_addr != NCR_NOT_SET)
		overrides[0].NCR5380_map_name = (NCR5380_map_type) ncr_addr;
	if (ncr_5380 != NCR_NOT_SET)
		overrides[0].board = BOARD_NCR5380;
	else if (ncr_53c400 != NCR_NOT_SET)
		overrides[0].board = BOARD_NCR53C400;
	else if (ncr_53c400a != NCR_NOT_SET)
		overrides[0].board = BOARD_NCR53C400A;
	else if (dtc_3181e != NCR_NOT_SET)
		overrides[0].board = BOARD_DTC3181E;
#ifndef SCSI_G_NCR5380_MEM
	if (!current_override && isapnp_present()) {
		struct pnp_dev *dev = NULL;
		count = 0;
		while ((dev = pnp_find_dev(NULL, ISAPNP_VENDOR('D', 'T', 'C'), ISAPNP_FUNCTION(0x436e), dev))) {
			if (count >= NO_OVERRIDES)
				break;
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
				overrides[count].irq = pnp_irq(dev, 0);
			else
				overrides[count].irq = NO_IRQ;
			if (pnp_dma_valid(dev, 0))
				overrides[count].dma = pnp_dma(dev, 0);
			else
				overrides[count].dma = DMA_NONE;
			overrides[count].NCR5380_map_name = (NCR5380_map_type) pnp_port_start(dev, 0);
			overrides[count].board = BOARD_DTC3181E;
			count++;
		}
	}
#endif
	tpnt->proc_name = "g_NCR5380";

	for (count = 0; current_override < NO_OVERRIDES; ++current_override) {
		if (!(overrides[current_override].NCR5380_map_name))
			continue;

		ports = NULL;
		switch (overrides[current_override].board) {
		case BOARD_NCR5380:
			flags = FLAG_NO_PSEUDO_DMA;
			break;
		case BOARD_NCR53C400:
			flags = FLAG_NCR53C400;
			break;
		case BOARD_NCR53C400A:
			flags = FLAG_NO_PSEUDO_DMA;
			ports = ncr_53c400a_ports;
			break;
		case BOARD_DTC3181E:
			flags = FLAG_NO_PSEUDO_DMA | FLAG_DTC3181E;
			ports = dtc_3181e_ports;
			break;
		}

#ifndef SCSI_G_NCR5380_MEM
		if (ports) {
			/* wakeup sequence for the NCR53C400A and DTC3181E */

			/* Disable the adapter and look for a free io port */
			outb(0x59, 0x779);
			outb(0xb9, 0x379);
			outb(0xc5, 0x379);
			outb(0xae, 0x379);
			outb(0xa6, 0x379);
			outb(0x00, 0x379);

			if (overrides[current_override].NCR5380_map_name != PORT_AUTO)
				for (i = 0; ports[i]; i++) {
					if (!request_region(ports[i],  16, "ncr53c80"))
						continue;
					if (overrides[current_override].NCR5380_map_name == ports[i])
						break;
					release_region(ports[i], 16);
			} else
				for (i = 0; ports[i]; i++) {
					if (!request_region(ports[i],  16, "ncr53c80"))
						continue;
					if (inb(ports[i]) == 0xff)
						break;
					release_region(ports[i], 16);
				}
			if (ports[i]) {
				/* At this point we have our region reserved */
				outb(0x59, 0x779);
				outb(0xb9, 0x379);
				outb(0xc5, 0x379);
				outb(0xae, 0x379);
				outb(0xa6, 0x379);
				outb(0x80 | i, 0x379);	/* set io port to be used */
				outb(0xc0, ports[i] + 9);
				if (inb(ports[i] + 9) != 0x80)
					continue;
				else
					overrides[current_override].NCR5380_map_name = ports[i];
			} else
				continue;
		}
		else
		{
			/* Not a 53C400A style setup - just grab */
			if(!(request_region(overrides[current_override].NCR5380_map_name, NCR5380_region_size, "ncr5380")))
				continue;
			region_size = NCR5380_region_size;
		}
#else
		base = overrides[current_override].NCR5380_map_name;
		if (!request_mem_region(base, NCR5380_region_size, "ncr5380"))
			continue;
		iomem = ioremap(base, NCR5380_region_size);
		if (!iomem) {
			release_mem_region(base, NCR5380_region_size);
			continue;
		}
#endif
		instance = scsi_register(tpnt, sizeof(struct NCR5380_hostdata));
		if (instance == NULL) {
#ifndef SCSI_G_NCR5380_MEM
			release_region(overrides[current_override].NCR5380_map_name, region_size);
#else
			iounmap(iomem);
			release_mem_region(base, NCR5380_region_size);
#endif
			continue;
		}

		instance->NCR5380_instance_name = overrides[current_override].NCR5380_map_name;
#ifndef SCSI_G_NCR5380_MEM
		instance->n_io_port = region_size;
#else
		((struct NCR5380_hostdata *)instance->hostdata)->iomem = iomem;
#endif

		NCR5380_init(instance, flags);

		if (overrides[current_override].irq != IRQ_AUTO)
			instance->irq = overrides[current_override].irq;
		else
			instance->irq = NCR5380_probe_irq(instance, 0xffff);

		/* Compatibility with documented NCR5380 kernel parameters */
		if (instance->irq == 255)
			instance->irq = NO_IRQ;

		if (instance->irq != NO_IRQ)
			if (request_irq(instance->irq, generic_NCR5380_intr,
					0, "NCR5380", instance)) {
				printk(KERN_WARNING "scsi%d : IRQ%d not free, interrupts disabled\n", instance->host_no, instance->irq);
				instance->irq = NO_IRQ;
			}

		if (instance->irq == NO_IRQ) {
			printk(KERN_INFO "scsi%d : interrupts not enabled. for better interactive performance,\n", instance->host_no);
			printk(KERN_INFO "scsi%d : please jumper the board for a free IRQ.\n", instance->host_no);
		}

		++current_override;
		++count;
	}
	return count;
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
	NCR5380_local_declare();
	NCR5380_setup(instance);
	
	if (instance->irq != NO_IRQ)
		free_irq(instance->irq, instance);
	NCR5380_exit(instance);

#ifndef SCSI_G_NCR5380_MEM
	release_region(instance->NCR5380_instance_name, instance->n_io_port);
#else
	iounmap(((struct NCR5380_hostdata *)instance->hostdata)->iomem);
	release_mem_region(instance->NCR5380_instance_name, NCR5380_region_size);
#endif


	return 0;
}

#ifdef BIOSPARAM
/**
 *	generic_NCR5380_biosparam
 *	@disk: disk to compute geometry for
 *	@dev: device identifier for this disk
 *	@ip: sizes to fill in
 *
 *	Generates a BIOS / DOS compatible H-C-S mapping for the specified 
 *	device / size.
 * 
 * 	XXX Most SCSI boards use this mapping, I could be incorrect.  Someone
 *	using hard disks on a trantor should verify that this mapping
 *	corresponds to that used by the BIOS / ASPI driver by running the linux
 *	fdisk program and matching the H_C_S coordinates to what DOS uses.
 *
 *	Locks: none
 */

static int
generic_NCR5380_biosparam(struct scsi_device *sdev, struct block_device *bdev,
			  sector_t capacity, int *ip)
{
	ip[0] = 64;
	ip[1] = 32;
	ip[2] = capacity >> 11;
	return 0;
}
#endif

#ifdef NCR53C400_PSEUDO_DMA

/**
 *	NCR5380_pread		-	pseudo DMA read
 *	@instance: adapter to read from
 *	@dst: buffer to read into
 *	@len: buffer length
 *
 *	Perform a pseudo DMA mode read from an NCR53C400 or equivalent
 *	controller
 */
 
static inline int NCR5380_pread(struct Scsi_Host *instance, unsigned char *dst, int len)
{
	int blocks = len / 128;
	int start = 0;
	int bl;

	NCR5380_local_declare();
	NCR5380_setup(instance);

	NCR5380_write(C400_CONTROL_STATUS_REG, CSR_BASE | CSR_TRANS_DIR);
	NCR5380_write(C400_BLOCK_COUNTER_REG, blocks);
	while (1) {
		if ((bl = NCR5380_read(C400_BLOCK_COUNTER_REG)) == 0) {
			break;
		}
		if (NCR5380_read(C400_CONTROL_STATUS_REG) & CSR_GATED_53C80_IRQ) {
			printk(KERN_ERR "53C400r: Got 53C80_IRQ start=%d, blocks=%d\n", start, blocks);
			return -1;
		}
		while (NCR5380_read(C400_CONTROL_STATUS_REG) & CSR_HOST_BUF_NOT_RDY);

#ifndef SCSI_G_NCR5380_MEM
		{
			int i;
			for (i = 0; i < 128; i++)
				dst[start + i] = NCR5380_read(C400_HOST_BUFFER);
		}
#else
		/* implies SCSI_G_NCR5380_MEM */
		memcpy_fromio(dst + start, iomem + NCR53C400_host_buffer, 128);
#endif
		start += 128;
		blocks--;
	}

	if (blocks) {
		while (NCR5380_read(C400_CONTROL_STATUS_REG) & CSR_HOST_BUF_NOT_RDY)
		{
			// FIXME - no timeout
		}

#ifndef SCSI_G_NCR5380_MEM
		{
			int i;	
			for (i = 0; i < 128; i++)
				dst[start + i] = NCR5380_read(C400_HOST_BUFFER);
		}
#else
		/* implies SCSI_G_NCR5380_MEM */
		memcpy_fromio(dst + start, iomem + NCR53C400_host_buffer, 128);
#endif
		start += 128;
		blocks--;
	}

	if (!(NCR5380_read(C400_CONTROL_STATUS_REG) & CSR_GATED_53C80_IRQ))
		printk("53C400r: no 53C80 gated irq after transfer");

#if 0
	/*
	 *	DON'T DO THIS - THEY NEVER ARRIVE!
	 */
	printk("53C400r: Waiting for 53C80 registers\n");
	while (NCR5380_read(C400_CONTROL_STATUS_REG) & CSR_53C80_REG)
		;
#endif
	if (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_END_DMA_TRANSFER))
		printk(KERN_ERR "53C400r: no end dma signal\n");
		
	NCR5380_write(MODE_REG, MR_BASE);
	NCR5380_read(RESET_PARITY_INTERRUPT_REG);
	return 0;
}

/**
 *	NCR5380_write		-	pseudo DMA write
 *	@instance: adapter to read from
 *	@dst: buffer to read into
 *	@len: buffer length
 *
 *	Perform a pseudo DMA mode read from an NCR53C400 or equivalent
 *	controller
 */

static inline int NCR5380_pwrite(struct Scsi_Host *instance, unsigned char *src, int len)
{
	int blocks = len / 128;
	int start = 0;
	int bl;
	int i;

	NCR5380_local_declare();
	NCR5380_setup(instance);

	NCR5380_write(C400_CONTROL_STATUS_REG, CSR_BASE);
	NCR5380_write(C400_BLOCK_COUNTER_REG, blocks);
	while (1) {
		if (NCR5380_read(C400_CONTROL_STATUS_REG) & CSR_GATED_53C80_IRQ) {
			printk(KERN_ERR "53C400w: Got 53C80_IRQ start=%d, blocks=%d\n", start, blocks);
			return -1;
		}

		if ((bl = NCR5380_read(C400_BLOCK_COUNTER_REG)) == 0) {
			break;
		}
		while (NCR5380_read(C400_CONTROL_STATUS_REG) & CSR_HOST_BUF_NOT_RDY)
			; // FIXME - timeout
#ifndef SCSI_G_NCR5380_MEM
		{
			for (i = 0; i < 128; i++)
				NCR5380_write(C400_HOST_BUFFER, src[start + i]);
		}
#else
		/* implies SCSI_G_NCR5380_MEM */
		memcpy_toio(iomem + NCR53C400_host_buffer, src + start, 128);
#endif
		start += 128;
		blocks--;
	}
	if (blocks) {
		while (NCR5380_read(C400_CONTROL_STATUS_REG) & CSR_HOST_BUF_NOT_RDY)
			; // FIXME - no timeout

#ifndef SCSI_G_NCR5380_MEM
		{
			for (i = 0; i < 128; i++)
				NCR5380_write(C400_HOST_BUFFER, src[start + i]);
		}
#else
		/* implies SCSI_G_NCR5380_MEM */
		memcpy_toio(iomem + NCR53C400_host_buffer, src + start, 128);
#endif
		start += 128;
		blocks--;
	}

#if 0
	printk("53C400w: waiting for registers to be available\n");
	THEY NEVER DO ! while (NCR5380_read(C400_CONTROL_STATUS_REG) & CSR_53C80_REG);
	printk("53C400w: Got em\n");
#endif

	/* Let's wait for this instead - could be ugly */
	/* All documentation says to check for this. Maybe my hardware is too
	 * fast. Waiting for it seems to work fine! KLL
	 */
	while (!(i = NCR5380_read(C400_CONTROL_STATUS_REG) & CSR_GATED_53C80_IRQ))
		;	// FIXME - no timeout

	/*
	 * I know. i is certainly != 0 here but the loop is new. See previous
	 * comment.
	 */
	if (i) {
		if (!((i = NCR5380_read(BUS_AND_STATUS_REG)) & BASR_END_DMA_TRANSFER))
			printk(KERN_ERR "53C400w: No END OF DMA bit - WHOOPS! BASR=%0x\n", i);
	} else
		printk(KERN_ERR "53C400w: no 53C80 gated irq after transfer (last block)\n");

#if 0
	if (!(NCR5380_read(BUS_AND_STATUS_REG) & BASR_END_DMA_TRANSFER)) {
		printk(KERN_ERR "53C400w: no end dma signal\n");
	}
#endif
	while (!(NCR5380_read(TARGET_COMMAND_REG) & TCR_LAST_BYTE_SENT))
		; 	// TIMEOUT
	return 0;
}
#endif				/* PSEUDO_DMA */

/*
 *	Include the NCR5380 core code that we build our driver around	
 */
 
#include "NCR5380.c"

static struct scsi_host_template driver_template = {
	.show_info      	= generic_NCR5380_show_info,
	.name           	= "Generic NCR5380/NCR53C400 SCSI",
	.detect         	= generic_NCR5380_detect,
	.release        	= generic_NCR5380_release_resources,
	.info           	= generic_NCR5380_info,
	.queuecommand   	= generic_NCR5380_queue_command,
	.eh_abort_handler	= generic_NCR5380_abort,
	.eh_bus_reset_handler	= generic_NCR5380_bus_reset,
	.bios_param     	= NCR5380_BIOSPARAM,
	.can_queue      	= CAN_QUEUE,
        .this_id        	= 7,
        .sg_tablesize   	= SG_ALL,
	.cmd_per_lun    	= CMD_PER_LUN,
        .use_clustering		= DISABLE_CLUSTERING,
};
#include <linux/module.h>
#include "scsi_module.c"

module_param(ncr_irq, int, 0);
module_param(ncr_dma, int, 0);
module_param(ncr_addr, int, 0);
module_param(ncr_5380, int, 0);
module_param(ncr_53c400, int, 0);
module_param(ncr_53c400a, int, 0);
module_param(dtc_3181e, int, 0);
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

__setup("ncr5380=", do_NCR5380_setup);
__setup("ncr53c400=", do_NCR53C400_setup);
__setup("ncr53c400a=", do_NCR53C400A_setup);
__setup("dtc3181e=", do_DTC3181E_setup);
