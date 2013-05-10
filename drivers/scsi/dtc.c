
#define AUTOSENSE
#define PSEUDO_DMA
#define DONT_USE_INTR
#define UNSAFE			/* Leave interrupts enabled during pseudo-dma I/O */
#define xNDEBUG (NDEBUG_INTR+NDEBUG_RESELECTION+\
		 NDEBUG_SELECTION+NDEBUG_ARBITRATION)
#define DMA_WORKS_RIGHT


/*
 * DTC 3180/3280 driver, by
 *	Ray Van Tassle	rayvt@comm.mot.com
 *
 *	taken from ...
 *	Trantor T128/T128F/T228 driver by...
 *
 * 	Drew Eckhardt
 *	Visionary Computing
 *	(Unix and Linux consulting and custom programming)
 *	drew@colorado.edu
 *      +1 (303) 440-4894
 *
 * DISTRIBUTION RELEASE 1.
 *
 * For more information, please consult 
 *
 * NCR 5380 Family
 * SCSI Protocol Controller
 * Databook
*/

/*
 * Options : 
 * AUTOSENSE - if defined, REQUEST SENSE will be performed automatically
 *      for commands that return with a CHECK CONDITION status. 
 *
 * PSEUDO_DMA - enables PSEUDO-DMA hardware, should give a 3-4X performance
 * increase compared to polled I/O.
 *
 * PARITY - enable parity checking.  Not supported.
 *
 * UNSAFE - leave interrupts enabled during pseudo-DMA transfers. 
 *		You probably want this.
 *
 * The card is detected and initialized in one of several ways : 
 * 1.  Autoprobe (default) - since the board is memory mapped, 
 *     a BIOS signature is scanned for to locate the registers.
 *     An interrupt is triggered to autoprobe for the interrupt
 *     line.
 *
 * 2.  With command line overrides - dtc=address,irq may be 
 *     used on the LILO command line to override the defaults.
 * 
*/

/*----------------------------------------------------------------*/
/* the following will set the monitor border color (useful to find
 where something crashed or gets stuck at */
/* 1 = blue
 2 = green
 3 = cyan
 4 = red
 5 = magenta
 6 = yellow
 7 = white
*/
#if 0
#define rtrc(i) {inb(0x3da); outb(0x31, 0x3c0); outb((i), 0x3c0);}
#else
#define rtrc(i) {}
#endif


#include <asm/system.h>
#include <linux/module.h>
#include <linux/signal.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/stat.h>
#include <linux/string.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include "scsi.h"
#include <scsi/scsi_host.h>
#include "dtc.h"
#define AUTOPROBE_IRQ
#include "NCR5380.h"


#define DTC_PUBLIC_RELEASE 2

/*
 * The DTC3180 & 3280 boards are memory mapped.
 * 
 */

/*
 */
/* Offset from DTC_5380_OFFSET */
#define DTC_CONTROL_REG		0x100	/* rw */
#define D_CR_ACCESS		0x80	/* ro set=can access 3280 registers */
#define CSR_DIR_READ		0x40	/* rw direction, 1 = read 0 = write */

#define CSR_RESET              0x80	/* wo  Resets 53c400 */
#define CSR_5380_REG           0x80	/* ro  5380 registers can be accessed */
#define CSR_TRANS_DIR          0x40	/* rw  Data transfer direction */
#define CSR_SCSI_BUFF_INTR     0x20	/* rw  Enable int on transfer ready */
#define CSR_5380_INTR          0x10	/* rw  Enable 5380 interrupts */
#define CSR_SHARED_INTR        0x08	/* rw  Interrupt sharing */
#define CSR_HOST_BUF_NOT_RDY   0x04	/* ro  Host buffer not ready */
#define CSR_SCSI_BUF_RDY       0x02	/* ro  SCSI buffer ready */
#define CSR_GATED_5380_IRQ     0x01	/* ro  Last block xferred */
#define CSR_INT_BASE (CSR_SCSI_BUFF_INTR | CSR_5380_INTR)


#define DTC_BLK_CNT		0x101	/* rw 
					 * # of 128-byte blocks to transfer */


#define D_CR_ACCESS             0x80	/* ro set=can access 3280 registers */

#define DTC_SWITCH_REG		0x3982	/* ro - DIP switches */
#define DTC_RESUME_XFER		0x3982	/* wo - resume data xfer 
					 * after disconnect/reconnect*/

#define DTC_5380_OFFSET		0x3880	/* 8 registers here, see NCR5380.h */

/*!!!! for dtc, it's a 128 byte buffer at 3900 !!! */
#define DTC_DATA_BUF		0x3900	/* rw 128 bytes long */

static struct override {
	unsigned int address;
	int irq;
} overrides
#ifdef OVERRIDE
[] __initdata = OVERRIDE;
#else
[4] __initdata = {
	{ 0, IRQ_AUTO }, { 0, IRQ_AUTO }, { 0, IRQ_AUTO }, { 0, IRQ_AUTO }
};
#endif

#define NO_OVERRIDES ARRAY_SIZE(overrides)

static struct base {
	unsigned long address;
	int noauto;
} bases[] __initdata = {
	{ 0xcc000, 0 },
	{ 0xc8000, 0 },
	{ 0xdc000, 0 },
	{ 0xd8000, 0 }
};

#define NO_BASES ARRAY_SIZE(bases)

static const struct signature {
	const char *string;
	int offset;
} signatures[] = {
	{"DATA TECHNOLOGY CORPORATION BIOS", 0x25},
};

#define NO_SIGNATURES ARRAY_SIZE(signatures)

#ifndef MODULE
/*
 * Function : dtc_setup(char *str, int *ints)
 *
 * Purpose : LILO command line initialization of the overrides array,
 *
 * Inputs : str - unused, ints - array of integer parameters with ints[0]
 *	equal to the number of ints.
 *
 */

static void __init dtc_setup(char *str, int *ints)
{
	static int commandline_current = 0;
	int i;
	if (ints[0] != 2)
		printk("dtc_setup: usage dtc=address,irq\n");
	else if (commandline_current < NO_OVERRIDES) {
		overrides[commandline_current].address = ints[1];
		overrides[commandline_current].irq = ints[2];
		for (i = 0; i < NO_BASES; ++i)
			if (bases[i].address == ints[1]) {
				bases[i].noauto = 1;
				break;
			}
		++commandline_current;
	}
}
#endif

/* 
 * Function : int dtc_detect(struct scsi_host_template * tpnt)
 *
 * Purpose : detects and initializes DTC 3180/3280 controllers
 *	that were autoprobed, overridden on the LILO command line, 
 *	or specified at compile time.
 *
 * Inputs : tpnt - template for this SCSI adapter.
 * 
 * Returns : 1 if a host adapter was found, 0 if not.
 *
*/

static int __init dtc_detect(struct scsi_host_template * tpnt)
{
	static int current_override = 0, current_base = 0;
	struct Scsi_Host *instance;
	unsigned int addr;
	void __iomem *base;
	int sig, count;

	tpnt->proc_name = "dtc3x80";
	tpnt->proc_info = &dtc_proc_info;

	for (count = 0; current_override < NO_OVERRIDES; ++current_override) {
		addr = 0;
		base = NULL;

		if (overrides[current_override].address) {
			addr = overrides[current_override].address;
			base = ioremap(addr, 0x2000);
			if (!base)
				addr = 0;
		} else
			for (; !addr && (current_base < NO_BASES); ++current_base) {
#if (DTCDEBUG & DTCDEBUG_INIT)
				printk(KERN_DEBUG "scsi-dtc : probing address %08x\n", bases[current_base].address);
#endif
				if (bases[current_base].noauto)
					continue;
				base = ioremap(bases[current_base].address, 0x2000);
				if (!base)
					continue;
				for (sig = 0; sig < NO_SIGNATURES; ++sig) {
					if (check_signature(base + signatures[sig].offset, signatures[sig].string, strlen(signatures[sig].string))) {
						addr = bases[current_base].address;
#if (DTCDEBUG & DTCDEBUG_INIT)
						printk(KERN_DEBUG "scsi-dtc : detected board.\n");
#endif
						goto found;
					}
				}
				iounmap(base);
			}

#if defined(DTCDEBUG) && (DTCDEBUG & DTCDEBUG_INIT)
		printk(KERN_DEBUG "scsi-dtc : base = %08x\n", addr);
#endif

		if (!addr)
			break;

found:
		instance = scsi_register(tpnt, sizeof(struct NCR5380_hostdata));
		if (instance == NULL)
			break;

		instance->base = addr;
		((struct NCR5380_hostdata *)(instance)->hostdata)->base = base;

		NCR5380_init(instance, 0);

		NCR5380_write(DTC_CONTROL_REG, CSR_5380_INTR);	/* Enable int's */
		if (overrides[current_override].irq != IRQ_AUTO)
			instance->irq = overrides[current_override].irq;
		else
			instance->irq = NCR5380_probe_irq(instance, DTC_IRQS);

#ifndef DONT_USE_INTR
		/* With interrupts enabled, it will sometimes hang when doing heavy
		 * reads. So better not enable them until I finger it out. */
		if (instance->irq != SCSI_IRQ_NONE)
			if (request_irq(instance->irq, dtc_intr, IRQF_DISABLED,
					"dtc", instance)) {
				printk(KERN_ERR "scsi%d : IRQ%d not free, interrupts disabled\n", instance->host_no, instance->irq);
				instance->irq = SCSI_IRQ_NONE;
			}

		if (instance->irq == SCSI_IRQ_NONE) {
			printk(KERN_WARNING "scsi%d : interrupts not enabled. for better interactive performance,\n", instance->host_no);
			printk(KERN_WARNING "scsi%d : please jumper the board for a free IRQ.\n", instance->host_no);
		}
#else
		if (instance->irq != SCSI_IRQ_NONE)
			printk(KERN_WARNING "scsi%d : interrupts not used. Might as well not jumper it.\n", instance->host_no);
		instance->irq = SCSI_IRQ_NONE;
#endif
#if defined(DTCDEBUG) && (DTCDEBUG & DTCDEBUG_INIT)
		printk("scsi%d : irq = %d\n", instance->host_no, instance->irq);
#endif

		printk(KERN_INFO "scsi%d : at 0x%05X", instance->host_no, (int) instance->base);
		if (instance->irq == SCSI_IRQ_NONE)
			printk(" interrupts disabled");
		else
			printk(" irq %d", instance->irq);
		printk(" options CAN_QUEUE=%d  CMD_PER_LUN=%d release=%d", CAN_QUEUE, CMD_PER_LUN, DTC_PUBLIC_RELEASE);
		NCR5380_print_options(instance);
		printk("\n");

		++current_override;
		++count;
	}
	return count;
}

/*
 * Function : int dtc_biosparam(Disk * disk, struct block_device *dev, int *ip)
 *
 * Purpose : Generates a BIOS / DOS compatible H-C-S mapping for 
 *	the specified device / size.
 * 
 * Inputs : size = size of device in sectors (512 bytes), dev = block device
 *	major / minor, ip[] = {heads, sectors, cylinders}  
 *
 * Returns : always 0 (success), initializes ip
 *	
*/

/* 
 * XXX Most SCSI boards use this mapping, I could be incorrect.  Some one
 * using hard disks on a trantor should verify that this mapping corresponds
 * to that used by the BIOS / ASPI driver by running the linux fdisk program
 * and matching the H_C_S coordinates to what DOS uses.
*/

static int dtc_biosparam(struct scsi_device *sdev, struct block_device *dev,
			 sector_t capacity, int *ip)
{
	int size = capacity;

	ip[0] = 64;
	ip[1] = 32;
	ip[2] = size >> 11;
	return 0;
}


/****************************************************************
 * Function : int NCR5380_pread (struct Scsi_Host *instance, 
 *	unsigned char *dst, int len)
 *
 * Purpose : Fast 5380 pseudo-dma read function, reads len bytes to 
 *	dst
 * 
 * Inputs : dst = destination, len = length in bytes
 *
 * Returns : 0 on success, non zero on a failure such as a watchdog 
 * 	timeout.
*/

static int dtc_maxi = 0;
static int dtc_wmaxi = 0;

static inline int NCR5380_pread(struct Scsi_Host *instance, unsigned char *dst, int len)
{
	unsigned char *d = dst;
	int i;			/* For counting time spent in the poll-loop */
	NCR5380_local_declare();
	NCR5380_setup(instance);

	i = 0;
	NCR5380_read(RESET_PARITY_INTERRUPT_REG);
	NCR5380_write(MODE_REG, MR_ENABLE_EOP_INTR | MR_DMA_MODE);
	if (instance->irq == SCSI_IRQ_NONE)
		NCR5380_write(DTC_CONTROL_REG, CSR_DIR_READ);
	else
		NCR5380_write(DTC_CONTROL_REG, CSR_DIR_READ | CSR_INT_BASE);
	NCR5380_write(DTC_BLK_CNT, len >> 7);	/* Block count */
	rtrc(1);
	while (len > 0) {
		rtrc(2);
		while (NCR5380_read(DTC_CONTROL_REG) & CSR_HOST_BUF_NOT_RDY)
			++i;
		rtrc(3);
		memcpy_fromio(d, base + DTC_DATA_BUF, 128);
		d += 128;
		len -= 128;
		rtrc(7);
		/*** with int's on, it sometimes hangs after here.
		 * Looks like something makes HBNR go away. */
	}
	rtrc(4);
	while (!(NCR5380_read(DTC_CONTROL_REG) & D_CR_ACCESS))
		++i;
	NCR5380_write(MODE_REG, 0);	/* Clear the operating mode */
	rtrc(0);
	NCR5380_read(RESET_PARITY_INTERRUPT_REG);
	if (i > dtc_maxi)
		dtc_maxi = i;
	return (0);
}

/****************************************************************
 * Function : int NCR5380_pwrite (struct Scsi_Host *instance, 
 *	unsigned char *src, int len)
 *
 * Purpose : Fast 5380 pseudo-dma write function, transfers len bytes from
 *	src
 * 
 * Inputs : src = source, len = length in bytes
 *
 * Returns : 0 on success, non zero on a failure such as a watchdog 
 * 	timeout.
*/

static inline int NCR5380_pwrite(struct Scsi_Host *instance, unsigned char *src, int len)
{
	int i;
	NCR5380_local_declare();
	NCR5380_setup(instance);

	NCR5380_read(RESET_PARITY_INTERRUPT_REG);
	NCR5380_write(MODE_REG, MR_ENABLE_EOP_INTR | MR_DMA_MODE);
	/* set direction (write) */
	if (instance->irq == SCSI_IRQ_NONE)
		NCR5380_write(DTC_CONTROL_REG, 0);
	else
		NCR5380_write(DTC_CONTROL_REG, CSR_5380_INTR);
	NCR5380_write(DTC_BLK_CNT, len >> 7);	/* Block count */
	for (i = 0; len > 0; ++i) {
		rtrc(5);
		/* Poll until the host buffer can accept data. */
		while (NCR5380_read(DTC_CONTROL_REG) & CSR_HOST_BUF_NOT_RDY)
			++i;
		rtrc(3);
		memcpy_toio(base + DTC_DATA_BUF, src, 128);
		src += 128;
		len -= 128;
	}
	rtrc(4);
	while (!(NCR5380_read(DTC_CONTROL_REG) & D_CR_ACCESS))
		++i;
	rtrc(6);
	/* Wait until the last byte has been sent to the disk */
	while (!(NCR5380_read(TARGET_COMMAND_REG) & TCR_LAST_BYTE_SENT))
		++i;
	rtrc(7);
	/* Check for parity error here. fixme. */
	NCR5380_write(MODE_REG, 0);	/* Clear the operating mode */
	rtrc(0);
	if (i > dtc_wmaxi)
		dtc_wmaxi = i;
	return (0);
}

MODULE_LICENSE("GPL");

#include "NCR5380.c"

static int dtc_release(struct Scsi_Host *shost)
{
	NCR5380_local_declare();
	NCR5380_setup(shost);
	if (shost->irq)
		free_irq(shost->irq, shost);
	NCR5380_exit(shost);
	if (shost->io_port && shost->n_io_port)
		release_region(shost->io_port, shost->n_io_port);
	scsi_unregister(shost);
	iounmap(base);
	return 0;
}

static struct scsi_host_template driver_template = {
	.name				= "DTC 3180/3280 ",
	.detect				= dtc_detect,
	.release			= dtc_release,
	.queuecommand			= dtc_queue_command,
	.eh_abort_handler		= dtc_abort,
	.eh_bus_reset_handler		= dtc_bus_reset,
	.bios_param     		= dtc_biosparam,
	.can_queue      		= CAN_QUEUE,
	.this_id        		= 7,
	.sg_tablesize   		= SG_ALL,
	.cmd_per_lun    		= CMD_PER_LUN,
	.use_clustering 		= DISABLE_CLUSTERING,
};
#include "scsi_module.c"
