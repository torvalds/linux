/*
 * linux/drivers/acorn/block/mfmhd.c
 *
 * Copyright (C) 1995, 1996 Russell King, Dave Alan Gilbert (gilbertd@cs.man.ac.uk)
 *
 * MFM hard drive code [experimental]
 */

/*
 * Change list:
 *
 *  3/2/96:DAG: Started a change list :-)
 *              Set the hardsect_size pointers up since we are running 256 byte
 *                sectors
 *              Added DMA code, put it into the rw_intr
 *              Moved RCAL out of generic interrupt code - don't want to do it
 *                while DMA'ing - its now in individual handlers.
 *              Took interrupt handlers off task queue lists and called
 *                directly - not sure of implications.
 *
 * 18/2/96:DAG: Well its reading OK I think, well enough for image file code
 *              to find the image file; but now I've discovered that I actually
 *              have to put some code in for image files.
 *
 *              Added stuff for image files; seems to work, but I've not
 *              got a multisegment image file (I don't think!).
 *              Put in a hack (yep a real hack) for multiple cylinder reads.
 *              Not convinced its working.
 *
 *  5/4/96:DAG: Added asm/hardware.h and use IOC_ macros
 *              Rewrote dma code in mfm.S (again!) - now takes a word at a time
 *              from main RAM for speed; still doesn't feel speedy!
 *
 * 20/4/96:DAG: After rewriting mfm.S a heck of a lot of times and speeding
 *              things up, I've finally figured out why its so damn slow.
 *              Linux is only reading a block at a time, and so you never
 *              get more than 1K per disc revoloution ~=60K/second.
 *
 * 27/4/96:DAG: On Russell's advice I change ll_rw_blk.c to ask it to
 *              join adjacent blocks together. Everything falls flat on its
 *              face.
 *              Four hours of debugging later; I hadn't realised that
 *              ll_rw_blk would be so generous as to join blocks whose
 *              results aren't going into consecutive buffers.
 * 
 *              OK; severe rehacking of mfm_rw_interrupt; now end_request's
 *              as soon as its DMA'd each request.  Odd thing is that
 *              we are sometimes getting interrupts where we are not transferring
 *              any data; why? Is that what happens when you miss? I doubt
 *              it; are we too fast? No - its just at command ends. Got 240K/s
 *              better than before, but RiscOS hits 480K/s
 *
 * 25/6/96:RMK: Fixed init code to allow the MFM podule to work.  Increased the
 *              number of errors for my Miniscribe drive (8425).
 *
 * 30/6/96:DAG: Russell suggested that a check drive 0 might turn the LEDs off
 *              - so in request_done just before it clears Busy it sends a
 *              check drive 0 - and the LEDs go off!!!!
 *
 *              Added test for mainboard controller. - Removes need for separate
 *              define.
 *
 * 13/7/96:DAG: Changed hardware sectore size to 512 in attempt to make
 *              IM drivers work.
 * 21/7/96:DAG: Took out old image file stuff (accessing it now produces an IO
 *              error.)
 *
 * 17/8/96:DAG: Ran through indent -kr -i8; evil - all my nice 2 character indents
 *              gone :-( Hand modified afterwards.
 *		Took out last remains of the older image map system.
 *
 * 22/9/96:DAG:	Changed mfm.S so it will carry on DMA'ing til; BSY is dropped
 *		Changed mfm_rw_intr so that it doesn't follow the error
 *		code until BSY is dropped. Nope - still broke. Problem
 *		may revolve around when it reads the results for the error
 *		number?
 *
 *16/11/96:DAG:	Modified for 2.0.18; request_irq changed
 *
 *17/12/96:RMK: Various cleanups, reorganisation, and the changes for new IO system.
 *		Improved probe for onboard MFM chip - it was hanging on my A5k.
 *		Added autodetect CHS code such that we don't rely on the presence
 *		of an ADFS boot block.  Added ioport resource manager calls so
 *		that we don't clash with already-running hardware (eg. RiscPC Ether
 *		card slots if someone tries this)!
 *
 * 17/1/97:RMK:	Upgraded to 2.1 kernels.
 *
 *  4/3/98:RMK:	Changed major number to 21.
 *
 * 27/6/98:RMK:	Changed asm/delay.h to linux/delay.h for mdelay().
 */

/*
 * Possible enhancements:
 *  Multi-thread the code so that it is possible that while one drive
 *  is seeking, the other one can be reading data/seeking as well.
 *  This would be a performance boost with dual drive systems.
 */

#include <linux/module.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/errno.h>
#include <linux/genhd.h>
#include <linux/major.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/blkpg.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/uaccess.h>
#include <asm/dma.h>
#include <asm/hardware.h>
#include <asm/ecard.h>
#include <asm/hardware/ioc.h>

static void (*do_mfm)(void) = NULL;
static struct request_queue *mfm_queue;
static DEFINE_SPINLOCK(mfm_lock);

#define MAJOR_NR	MFM_ACORN_MAJOR
#define QUEUE (mfm_queue)
#define CURRENT elv_next_request(mfm_queue)

/*
 * Configuration section
 *
 * This is the maximum number of drives that we accept
 */
#define MFM_MAXDRIVES 2
/*
 * Linux I/O address of onboard MFM controller or 0 to disable this
 */
#define ONBOARD_MFM_ADDRESS ((0x002d0000 >> 2) | 0x80000000)
/*
 * Uncomment this to enable debugging in the MFM driver...
 */
#ifndef DEBUG
/*#define DEBUG */
#endif
/*
 * End of configuration
 */

 
/*
 * This structure contains all information to do with a particular physical
 * device.
 */
struct mfm_info {
	unsigned char sectors;
	unsigned char heads;
	unsigned short cylinders;
	unsigned short lowcurrent;
	unsigned short precomp;
#define NO_TRACK -1
#define NEED_1_RECAL -2
#define NEED_2_RECAL -3
		 int cylinder;
	struct {
		char recal;
		char report;
		char abort;
	} errors;
} mfm_info[MFM_MAXDRIVES];

#define MFM_DRV_INFO mfm_info[raw_cmd.dev]

/* Stuff from the assembly routines */
extern unsigned int hdc63463_baseaddress;	/* Controller base address */
extern unsigned int hdc63463_irqpolladdress;	/* Address to read to test for int */
extern unsigned int hdc63463_irqpollmask;	/* Mask for irq register */
extern unsigned int hdc63463_dataptr;	/* Pointer to kernel data space to DMA */
extern int hdc63463_dataleft;	/* Number of bytes left to transfer */




static int lastspecifieddrive;
static unsigned Busy;

static unsigned int PartFragRead;	/* The number of sectors which have been read
					   during a partial read split over two
					   cylinders.  If 0 it means a partial
					   read did not occur. */

static unsigned int PartFragRead_RestartBlock;	/* Where to restart on a split access */
static unsigned int PartFragRead_SectorsLeft;	/* Where to restart on a split access */

static int Sectors256LeftInCurrent;	/* i.e. 256 byte sectors left in current */
static int SectorsLeftInRequest;	/* i.e. blocks left in the thing mfm_request was called for */
static int Copy_Sector;		/* The 256 byte sector we are currently at - fragments need to know 
				   where to take over */
static char *Copy_buffer;


static void mfm_seek(void);
static void mfm_rerequest(void);
static void mfm_request(void);
static void mfm_specify (void);
static void issue_request(unsigned int block, unsigned int nsect,
			  struct request *req);

static unsigned int mfm_addr;		/* Controller address */
static unsigned int mfm_IRQPollLoc;	/* Address to read for IRQ information */
static unsigned int mfm_irqenable;	/* Podule IRQ enable location */
static unsigned char mfm_irq;		/* Interrupt number */
static int mfm_drives = 0;		/* drives available */
static int mfm_status = 0;		/* interrupt status */
static int *errors;

static struct rawcmd {
	unsigned int dev;
	unsigned int cylinder;
	unsigned int head;
	unsigned int sector;
	unsigned int cmdtype;
	unsigned int cmdcode;
	unsigned char cmddata[16];
	unsigned int cmdlen;
} raw_cmd;

static unsigned char result[16];

static struct cont {
	void (*interrupt) (void);	/* interrupt handler */
	void (*error) (void);	/* error handler */
	void (*redo) (void);	/* redo handler */
	void (*done) (int st);	/* done handler */
} *cont = NULL;

#if 0
static struct tq_struct mfm_tq = {0, 0, (void (*)(void *)) NULL, 0};
#endif

int number_mfm_drives = 1;

/* ------------------------------------------------------------------------------------------ */
/*
 * From the HD63463 data sheet from Hitachi Ltd.
 */

#define MFM_COMMAND (mfm_addr + 0)
#define MFM_DATAOUT (mfm_addr + 1)
#define MFM_STATUS  (mfm_addr + 8)
#define MFM_DATAIN  (mfm_addr + 9)

#define CMD_ABT		0xF0	/* Abort */
#define CMD_SPC		0xE8	/* Specify */
#define CMD_TST		0xE0	/* Test */
#define CMD_RCLB	0xC8	/* Recalibrate */
#define CMD_SEK		0xC0	/* Seek */
#define CMD_WFS		0xAB	/* Write Format Skew */
#define CMD_WFM		0xA3	/* Write Format */
#define CMD_MTB		0x90	/* Memory to buffer */
#define CMD_CMPD	0x88	/* Compare data */
#define CMD_WD		0x87	/* Write data */
#define CMD_RED		0x70	/* Read erroneous data */
#define CMD_RIS		0x68	/* Read ID skew */
#define CMD_FID		0x61	/* Find ID */
#define CMD_RID		0x60	/* Read ID */
#define CMD_BTM		0x50	/* Buffer to memory */
#define CMD_CKD		0x48	/* Check data */
#define CMD_RD		0x40	/* Read data */
#define CMD_OPBW	0x38	/* Open buffer write */
#define CMD_OPBR	0x30	/* Open buffer read */
#define CMD_CKV		0x28	/* Check drive */
#define CMD_CKE		0x20	/* Check ECC */
#define CMD_POD		0x18	/* Polling disable */
#define CMD_POL		0x10	/* Polling enable */
#define CMD_RCAL	0x08	/* Recall */

#define STAT_BSY	0x8000	/* Busy */
#define STAT_CPR	0x4000	/* Command Parameter Rejection */
#define STAT_CED	0x2000	/* Command end */
#define STAT_SED	0x1000	/* Seek end */
#define STAT_DER	0x0800	/* Drive error */
#define STAT_ABN	0x0400	/* Abnormal end */
#define STAT_POL	0x0200	/* Polling */

/* ------------------------------------------------------------------------------------------ */
#ifdef DEBUG
static void console_printf(const char *fmt,...)
{
	static char buffer[2048];	/* Arbitary! */
	extern void console_print(const char *);
	unsigned long flags;
	va_list ap;

	local_irq_save(flags);

	va_start(ap, fmt);
	vsprintf(buffer, fmt, ap);
	console_print(buffer);
	va_end(fmt);

	local_irq_restore(flags);
};	/* console_printf */

#define DBG(x...) console_printf(x)
#else
#define DBG(x...)
#endif

static void print_status(void)
{
	char *error;
	static char *errors[] = {
         "no error",
	 "command aborted",
	 "invalid command",
	 "parameter error",
	 "not initialised",
	 "rejected TEST",
	 "no useld",
	 "write fault",
	 "not ready",
	 "no scp",
	 "in seek",
	 "invalid NCA",
	 "invalid step rate",
	 "seek error",
	 "over run",
	 "invalid PHA",
	 "data field EEC error",
	 "data field CRC error",
	 "error corrected",
	 "data field fatal error",
	 "no data am",
	 "not hit",
	 "ID field CRC error",
	 "time over",
	 "no ID am",
	 "not writable"
	};
	if (result[1] < 0x65)
		error = errors[result[1] >> 2];
	else
		error = "unknown";
	printk("(");
	if (mfm_status & STAT_BSY) printk("BSY ");
	if (mfm_status & STAT_CPR) printk("CPR ");
	if (mfm_status & STAT_CED) printk("CED ");
	if (mfm_status & STAT_SED) printk("SED ");
	if (mfm_status & STAT_DER) printk("DER ");
	if (mfm_status & STAT_ABN) printk("ABN ");
	if (mfm_status & STAT_POL) printk("POL ");
	printk(") SSB = %X (%s)\n", result[1], error);

}

/* ------------------------------------------------------------------------------------- */

static void issue_command(int command, unsigned char *cmdb, int len)
{
	int status;
#ifdef DEBUG
	int i;
	console_printf("issue_command: %02X: ", command);
	for (i = 0; i < len; i++)
		console_printf("%02X ", cmdb[i]);
	console_printf("\n");
#endif

	do {
		status = inw(MFM_STATUS);
	} while (status & (STAT_BSY | STAT_POL));
	DBG("issue_command: status after pol/bsy loop: %02X:\n ", status >> 8);

	if (status & (STAT_CPR | STAT_CED | STAT_SED | STAT_DER | STAT_ABN)) {
		outw(CMD_RCAL, MFM_COMMAND);
		while (inw(MFM_STATUS) & STAT_BSY);
	}
	status = inw(MFM_STATUS);
	DBG("issue_command: status before parameter issue: %02X:\n ", status >> 8);

	while (len > 0) {
		outw(cmdb[1] | (cmdb[0] << 8), MFM_DATAOUT);
		len -= 2;
		cmdb += 2;
	}
	status = inw(MFM_STATUS);
	DBG("issue_command: status before command issue: %02X:\n ", status >> 8);

	outw(command, MFM_COMMAND);
	status = inw(MFM_STATUS);
	DBG("issue_command: status immediately after command issue: %02X:\n ", status >> 8);
}

static void wait_for_completion(void)
{
	while ((mfm_status = inw(MFM_STATUS)) & STAT_BSY);
}

static void wait_for_command_end(void)
{
	int i;

	while (!((mfm_status = inw(MFM_STATUS)) & STAT_CED));

	for (i = 0; i < 16;) {
		int in;
		in = inw(MFM_DATAIN);
		result[i++] = in >> 8;
		result[i++] = in;
	}
	outw (CMD_RCAL, MFM_COMMAND);
}

/* ------------------------------------------------------------------------------------- */

static void mfm_rw_intr(void)
{
	int old_status;		/* Holds status on entry, we read to see if the command just finished */
#ifdef DEBUG
	console_printf("mfm_rw_intr...dataleft=%d\n", hdc63463_dataleft);
	print_status();
#endif

  /* Now don't handle the error until BSY drops */
	if ((mfm_status & (STAT_DER | STAT_ABN)) && ((mfm_status&STAT_BSY)==0)) {
		/* Something has gone wrong - let's try that again */
		outw(CMD_RCAL, MFM_COMMAND);	/* Clear interrupt condition */
		if (cont) {
			DBG("mfm_rw_intr: DER/ABN err\n");
			cont->error();
			cont->redo();
		};
		return;
	};

	/* OK so what ever happened it's not an error, now I reckon we are left between
	   a choice of command end or some data which is ready to be collected */
	/* I think we have to transfer data while the interrupt line is on and its
	   not any other type of interrupt */
	if (CURRENT->cmd == WRITE) {
		extern void hdc63463_writedma(void);
		if ((hdc63463_dataleft <= 0) && (!(mfm_status & STAT_CED))) {
			printk("mfm_rw_intr: Apparent DMA write request when no more to DMA\n");
			if (cont) {
				cont->error();
				cont->redo();
			};
			return;
		};
		hdc63463_writedma();
	} else {
		extern void hdc63463_readdma(void);
		if ((hdc63463_dataleft <= 0) && (!(mfm_status & STAT_CED))) {
			printk("mfm_rw_intr: Apparent DMA read request when no more to DMA\n");
			if (cont) {
				cont->error();
				cont->redo();
			};
			return;
		};
		DBG("Going to try read dma..............status=0x%x, buffer=%p\n", mfm_status, hdc63463_dataptr);
		hdc63463_readdma();
	};			/* Read */

	if (hdc63463_dataptr != ((unsigned int) Copy_buffer + 256)) {
		/* If we didn't actually manage to get any data on this interrupt - but why? We got the interrupt */
		/* Ah - well looking at the status its just when we get command end; so no problem */
		/*console_printf("mfm: dataptr mismatch. dataptr=0x%08x Copy_buffer+256=0x%08p\n",
		   hdc63463_dataptr,Copy_buffer+256);
		   print_status(); */
	} else {
		Sectors256LeftInCurrent--;
		Copy_buffer += 256;
		Copy_Sector++;

		/* We have come to the end of this request */
		if (!Sectors256LeftInCurrent) {
			DBG("mfm: end_request for CURRENT=0x%p CURRENT(sector=%d current_nr_sectors=%d nr_sectors=%d)\n",
				       CURRENT, CURRENT->sector, CURRENT->current_nr_sectors, CURRENT->nr_sectors);

			CURRENT->nr_sectors -= CURRENT->current_nr_sectors;
			CURRENT->sector += CURRENT->current_nr_sectors;
			SectorsLeftInRequest -= CURRENT->current_nr_sectors;

			end_request(CURRENT, 1);
			if (SectorsLeftInRequest) {
				hdc63463_dataptr = (unsigned int) CURRENT->buffer;
				Copy_buffer = CURRENT->buffer;
				Sectors256LeftInCurrent = CURRENT->current_nr_sectors * 2;
				errors = &(CURRENT->errors);
				/* These should match the present calculations of the next logical sector
				   on the device
				   Copy_Sector=CURRENT->sector*2; */

				if (Copy_Sector != CURRENT->sector * 2)
#ifdef DEBUG
					/*console_printf*/printk("mfm: Copy_Sector mismatch. Copy_Sector=%d CURRENT->sector*2=%d\n",
					Copy_Sector, CURRENT->sector * 2);
#else
					printk("mfm: Copy_Sector mismatch! Eek!\n");
#endif
			};	/* CURRENT */
		};	/* Sectors256LeftInCurrent */
	};

	old_status = mfm_status;
	mfm_status = inw(MFM_STATUS);
	if (mfm_status & (STAT_DER | STAT_ABN)) {
		/* Something has gone wrong - let's try that again */
		if (cont) {
			DBG("mfm_rw_intr: DER/ABN error\n");
			cont->error();
			cont->redo();
		};
		return;
	};

	/* If this code wasn't entered due to command_end but there is
	   now a command end we must read the command results out. If it was
	   entered like this then mfm_interrupt_handler would have done the
	   job. */
	if ((!((old_status & (STAT_CPR | STAT_BSY)) == STAT_CPR)) &&
	    ((mfm_status & (STAT_CPR | STAT_BSY)) == STAT_CPR)) {
		int len = 0;
		while (len < 16) {
			int in;
			in = inw(MFM_DATAIN);
			result[len++] = in >> 8;
			result[len++] = in;
		};
	};			/* Result read */

	/*console_printf ("mfm_rw_intr nearexit [%02X]\n", __raw_readb(mfm_IRQPollLoc)); */

	/* If end of command move on */
	if (mfm_status & (STAT_CED)) {
		outw(CMD_RCAL, MFM_COMMAND);	/* Clear interrupt condition */
		/* End of command - trigger the next command */
		if (cont) {
			cont->done(1);
		}
		DBG("mfm_rw_intr: returned from cont->done\n");
	} else {
		/* Its going to generate another interrupt */
		do_mfm = mfm_rw_intr;
	};
}

static void mfm_setup_rw(void)
{
	DBG("setting up for rw...\n");

	do_mfm = mfm_rw_intr;
	issue_command(raw_cmd.cmdcode, raw_cmd.cmddata, raw_cmd.cmdlen);
}

static void mfm_recal_intr(void)
{
#ifdef DEBUG
	console_printf("recal intr - status = ");
	print_status();
#endif
	outw(CMD_RCAL, MFM_COMMAND);	/* Clear interrupt condition */
	if (mfm_status & (STAT_DER | STAT_ABN)) {
		printk("recal failed\n");
		MFM_DRV_INFO.cylinder = NEED_2_RECAL;
		if (cont) {
			cont->error();
			cont->redo();
		}
		return;
	}
	/* Thats seek end - we are finished */
	if (mfm_status & STAT_SED) {
		issue_command(CMD_POD, NULL, 0);
		MFM_DRV_INFO.cylinder = 0;
		mfm_seek();
		return;
	}
	/* Command end without seek end (see data sheet p.20) for parallel seek
	   - we have to send a POL command to wait for the seek */
	if (mfm_status & STAT_CED) {
		do_mfm = mfm_recal_intr;
		issue_command(CMD_POL, NULL, 0);
		return;
	}
	printk("recal: unknown status\n");
}

static void mfm_seek_intr(void)
{
#ifdef DEBUG
	console_printf("seek intr - status = ");
	print_status();
#endif
	outw(CMD_RCAL, MFM_COMMAND);	/* Clear interrupt condition */
	if (mfm_status & (STAT_DER | STAT_ABN)) {
		printk("seek failed\n");
		MFM_DRV_INFO.cylinder = NEED_2_RECAL;
		if (cont) {
			cont->error();
			cont->redo();
		}
		return;
	}
	if (mfm_status & STAT_SED) {
		issue_command(CMD_POD, NULL, 0);
		MFM_DRV_INFO.cylinder = raw_cmd.cylinder;
		mfm_seek();
		return;
	}
	if (mfm_status & STAT_CED) {
		do_mfm = mfm_seek_intr;
		issue_command(CMD_POL, NULL, 0);
		return;
	}
	printk("seek: unknown status\n");
}

/* IDEA2 seems to work better - its what RiscOS sets my
 * disc to - on its SECOND call to specify!
 */
#define IDEA2
#ifndef IDEA2
#define SPEC_SL 0x16
#define SPEC_SH 0xa9		/* Step pulse high=21, Record Length=001 (256 bytes) */
#else
#define SPEC_SL 0x00		/* OM2 - SL - step pulse low */
#define SPEC_SH 0x21		/* Step pulse high=4, Record Length=001 (256 bytes) */
#endif

static void mfm_setupspecify (int drive, unsigned char *cmdb)
{
	cmdb[0]  = 0x1f;		/* OM0 - !SECT,!MOD,!DIF,PADP,ECD,CRCP,CRCI,ACOR */
	cmdb[1]  = 0xc3;		/* OM1 - DTM,BRST,!CEDM,!SEDM,!DERM,0,AMEX,PSK */
	cmdb[2]  = SPEC_SL;		/* OM2 - SL - step pulse low */
	cmdb[3]  = (number_mfm_drives == 1) ? 0x02 : 0x06;	/* 1 or 2 drives */
	cmdb[4]  = 0xfc | ((mfm_info[drive].cylinders - 1) >> 8);/* RW time over/high part of number of cylinders */
	cmdb[5]  = mfm_info[drive].cylinders - 1;		/* low part of number of cylinders */
	cmdb[6]  = mfm_info[drive].heads - 1;			/* Number of heads */
	cmdb[7]  = mfm_info[drive].sectors - 1;			/* Number of sectors */
	cmdb[8]  = SPEC_SH;
	cmdb[9]  = 0x0a;		/* gap length 1 */
	cmdb[10] = 0x0d;		/* gap length 2 */
	cmdb[11] = 0x0c;		/* gap length 3 */
	cmdb[12] = (mfm_info[drive].precomp - 1) >> 8;	/* pre comp cylinder */
	cmdb[13] = mfm_info[drive].precomp - 1;
	cmdb[14] = (mfm_info[drive].lowcurrent - 1) >> 8;	/* Low current cylinder */
	cmdb[15] = mfm_info[drive].lowcurrent - 1;
}

static void mfm_specify (void)
{
	unsigned char cmdb[16];

	DBG("specify...dev=%d lastspecified=%d\n", raw_cmd.dev, lastspecifieddrive);
	mfm_setupspecify (raw_cmd.dev, cmdb);

	issue_command (CMD_SPC, cmdb, 16);
	/* Ensure that we will do another specify if we move to the other drive */
	lastspecifieddrive = raw_cmd.dev;
	wait_for_completion();
}

static void mfm_seek(void)
{
	unsigned char cmdb[4];

	DBG("seeking...\n");
	if (MFM_DRV_INFO.cylinder < 0) {
		do_mfm = mfm_recal_intr;
		DBG("mfm_seek: about to call specify\n");
		mfm_specify ();	/* DAG added this */

		cmdb[0] = raw_cmd.dev + 1;
		cmdb[1] = 0;

		issue_command(CMD_RCLB, cmdb, 2);
		return;
	}
	if (MFM_DRV_INFO.cylinder != raw_cmd.cylinder) {
		cmdb[0] = raw_cmd.dev + 1;
		cmdb[1] = 0;	/* raw_cmd.head; DAG: My data sheet says this should be 0 */
		cmdb[2] = raw_cmd.cylinder >> 8;
		cmdb[3] = raw_cmd.cylinder;

		do_mfm = mfm_seek_intr;
		issue_command(CMD_SEK, cmdb, 4);
	} else
		mfm_setup_rw();
}

static void mfm_initialise(void)
{
	DBG("init...\n");
	mfm_seek();
}

static void request_done(int uptodate)
{
	DBG("mfm:request_done\n");
	if (uptodate) {
		unsigned char block[2] = {0, 0};

		/* Apparently worked - let's check bytes left to DMA */
		if (hdc63463_dataleft != (PartFragRead_SectorsLeft * 256)) {
			printk("mfm: request_done - dataleft=%d - should be %d - Eek!\n", hdc63463_dataleft, PartFragRead_SectorsLeft * 256);
			end_request(CURRENT, 0);
			Busy = 0;
		};
		/* Potentially this means that we've done; but we might be doing
		   a partial access, (over two cylinders) or we may have a number
		   of fragments in an image file.  First let's deal with partial accesss
		 */
		if (PartFragRead) {
			/* Yep - a partial access */

			/* and issue the remainder */
			issue_request(PartFragRead_RestartBlock, PartFragRead_SectorsLeft, CURRENT);
			return;
		}

		/* ah well - perhaps there is another fragment to go */

		/* Increment pointers/counts to start of next fragment */
		if (SectorsLeftInRequest > 0) printk("mfm: SectorsLeftInRequest>0 - Eek! Shouldn't happen!\n");

		/* No - its the end of the line */
		/* end_request's should have happened at the end of sector DMAs */
		/* Turns Drive LEDs off - may slow it down? */
		if (!elv_next_request(QUEUE))
			issue_command(CMD_CKV, block, 2);

		Busy = 0;
		DBG("request_done: About to mfm_request\n");
		/* Next one please */
		mfm_request();	/* Moved from mfm_rw_intr */
		DBG("request_done: returned from mfm_request\n");
	} else {
		printk("mfm:request_done: update=0\n");
		end_request(CURRENT, 0);
		Busy = 0;
	}
}

static void error_handler(void)
{
	printk("error detected... status = ");
	print_status();
	(*errors)++;
	if (*errors > MFM_DRV_INFO.errors.abort)
		cont->done(0);
	if (*errors > MFM_DRV_INFO.errors.recal)
		MFM_DRV_INFO.cylinder = NEED_2_RECAL;
}

static void rw_interrupt(void)
{
	printk("rw_interrupt\n");
}

static struct cont rw_cont =
{
	rw_interrupt,
	error_handler,
	mfm_rerequest,
	request_done
};

/*
 * Actually gets round to issuing the request - note everything at this
 * point is in 256 byte sectors not Linux 512 byte blocks
 */
static void issue_request(unsigned int block, unsigned int nsect,
			  struct request *req)
{
	struct gendisk *disk = req->rq_disk;
	struct mfm_info *p = disk->private_data;
	int track, start_head, start_sector;
	int sectors_to_next_cyl;
	dev = p - mfm_info;

	track = block / p->sectors;
	start_sector = block % p->sectors;
	start_head = track % p->heads;

	/* First get the number of whole tracks which are free before the next
	   track */
	sectors_to_next_cyl = (p->heads - (start_head + 1)) * p->sectors;
	/* Then add in the number of sectors left on this track */
	sectors_to_next_cyl += (p->sectors - start_sector);

	DBG("issue_request: mfm_info[dev].sectors=%d track=%d\n", p->sectors, track);

	raw_cmd.dev = dev;
	raw_cmd.sector = start_sector;
	raw_cmd.head = start_head;
	raw_cmd.cylinder = track / p->heads;
	raw_cmd.cmdtype = CURRENT->cmd;
	raw_cmd.cmdcode = CURRENT->cmd == WRITE ? CMD_WD : CMD_RD;
	raw_cmd.cmddata[0] = dev + 1;	/* DAG: +1 to get US */
	raw_cmd.cmddata[1] = raw_cmd.head;
	raw_cmd.cmddata[2] = raw_cmd.cylinder >> 8;
	raw_cmd.cmddata[3] = raw_cmd.cylinder;
	raw_cmd.cmddata[4] = raw_cmd.head;
	raw_cmd.cmddata[5] = raw_cmd.sector;

	/* Was == and worked - how the heck??? */
	if (lastspecifieddrive != raw_cmd.dev)
		mfm_specify ();

	if (nsect <= sectors_to_next_cyl) {
		raw_cmd.cmddata[6] = nsect >> 8;
		raw_cmd.cmddata[7] = nsect;
		PartFragRead = 0;	/* All in one */
		PartFragRead_SectorsLeft = 0;	/* Must set this - used in DMA calcs */
	} else {
		raw_cmd.cmddata[6] = sectors_to_next_cyl >> 8;
		raw_cmd.cmddata[7] = sectors_to_next_cyl;
		PartFragRead = sectors_to_next_cyl;	/* only do this many this time */
		PartFragRead_RestartBlock = block + sectors_to_next_cyl;	/* Where to restart from */
		PartFragRead_SectorsLeft = nsect - sectors_to_next_cyl;
	}
	raw_cmd.cmdlen = 8;

	/* Setup DMA pointers */
	hdc63463_dataptr = (unsigned int) Copy_buffer;
	hdc63463_dataleft = nsect * 256;	/* Better way? */

	DBG("mfm%c: %sing: CHS=%d/%d/%d, sectors=%d, buffer=0x%08lx (%p)\n",
	     raw_cmd.dev + 'a', (CURRENT->cmd == READ) ? "read" : "writ",
		       raw_cmd.cylinder,
		       raw_cmd.head,
	    raw_cmd.sector, nsect, (unsigned long) Copy_buffer, CURRENT);

	cont = &rw_cont;
	errors = &(CURRENT->errors);
#if 0
	mfm_tq.routine = (void (*)(void *)) mfm_initialise;
	queue_task(&mfm_tq, &tq_immediate);
	mark_bh(IMMEDIATE_BH);
#else
	mfm_initialise();
#endif
}				/* issue_request */

/*
 * Called when an error has just happened - need to trick mfm_request
 * into thinking we weren't busy
 *
 * Turn off ints - mfm_request expects them this way
 */
static void mfm_rerequest(void)
{
	DBG("mfm_rerequest\n");
	cli();
	Busy = 0;
	mfm_request();
}

static struct gendisk *mfm_gendisk[2];

static void mfm_request(void)
{
	DBG("mfm_request CURRENT=%p Busy=%d\n", CURRENT, Busy);

	/* If we are still processing then return; we will get called again */
	if (Busy) {
		/* Again seems to be common in 1.3.45 */
		/*DBG*/printk("mfm_request: Exiting due to busy\n");
		return;
	}
	Busy = 1;

	while (1) {
		unsigned int block, nsect;
		struct gendisk *disk;

		DBG("mfm_request: loop start\n");
		sti();

		DBG("mfm_request: before !CURRENT\n");

		if (!CURRENT) {
			printk("mfm_request: Exiting due to empty queue (pre)\n");
			do_mfm = NULL;
			Busy = 0;
			return;
		}

		DBG("mfm_request:                 before arg extraction\n");

		disk = CURRENT->rq_disk;
		block = CURRENT->sector;
		nsect = CURRENT->nr_sectors;
		if (block >= get_capacity(disk) ||
		    block+nsect > get_capacity(disk)) {
			printk("%s: bad access: block=%d, count=%d, nr_sects=%ld\n",
			       disk->disk_name, block, nsect, get_capacity(disk));
			printk("mfm: continue 1\n");
			end_request(CURRENT, 0);
			Busy = 0;
			continue;
		}

		/* DAG: Linux doesn't cope with this - even though it has an array telling
		   it the hardware block size - silly */
		block <<= 1;	/* Now in 256 byte sectors */
		nsect <<= 1;	/* Ditto */

		SectorsLeftInRequest = nsect >> 1;
		Sectors256LeftInCurrent = CURRENT->current_nr_sectors * 2;
		Copy_buffer = CURRENT->buffer;
		Copy_Sector = CURRENT->sector << 1;

		DBG("mfm_request: block after offset=%d\n", block);

		if (CURRENT->cmd != READ && CURRENT->cmd != WRITE) {
			printk("unknown mfm-command %d\n", CURRENT->cmd);
			end_request(CURRENT, 0);
			Busy = 0;
			printk("mfm: continue 4\n");
			continue;
		}
		issue_request(block, nsect, CURRENT);

		break;
	}
	DBG("mfm_request: Dropping out bottom\n");
}

static void do_mfm_request(request_queue_t *q)
{
	DBG("do_mfm_request: about to mfm_request\n");
	mfm_request();
}

static void mfm_interrupt_handler(int unused, void *dev_id)
{
	void (*handler) (void) = do_mfm;

	do_mfm = NULL;

	DBG("mfm_interrupt_handler (handler=0x%p)\n", handler);

	mfm_status = inw(MFM_STATUS);

	/* If CPR (Command Parameter Reject) and not busy it means that the command
	   has some return message to give us */
	if ((mfm_status & (STAT_CPR | STAT_BSY)) == STAT_CPR) {
		int len = 0;
		while (len < 16) {
			int in;
			in = inw(MFM_DATAIN);
			result[len++] = in >> 8;
			result[len++] = in;
		}
	}
	if (handler) {
		handler();
		return;
	}
	outw (CMD_RCAL, MFM_COMMAND);	/* Clear interrupt condition */
	printk ("mfm: unexpected interrupt - status = ");
	print_status ();
	while (1);
}





/*
 * Tell the user about the drive if we decided it exists.
 */
static void mfm_geometry(int drive)
{
	struct mfm_info *p = mfm_info + drive;
	struct gendisk *disk = mfm_gendisk[drive];
	disk->private_data = p;
	if (p->cylinders)
		printk ("%s: %dMB CHS=%d/%d/%d LCC=%d RECOMP=%d\n",
			disk->disk_name,
			p->cylinders * p->heads * p->sectors / 4096,
			p->cylinders, p->heads, p->sectors,
			p->lowcurrent, p->precomp);
	set_capacity(disk, p->cylinders * p->heads * p->sectors / 2);
}

#ifdef CONFIG_BLK_DEV_MFM_AUTODETECT
/*
 * Attempt to detect a drive and find its geometry.  The drive has already been
 * specified...
 *
 * We first recalibrate the disk, then try to probe sectors, heads and then
 * cylinders.  NOTE! the cylinder probe may break drives.  The xd disk driver
 * does something along these lines, so I assume that most drives are up to
 * this mistreatment...
 */
static int mfm_detectdrive (int drive)
{
	unsigned int mingeo[3], maxgeo[3];
	unsigned int attribute, need_recal = 1;
	unsigned char cmdb[8];

	memset (mingeo, 0, sizeof (mingeo));
	maxgeo[0] = mfm_info[drive].sectors;
	maxgeo[1] = mfm_info[drive].heads;
	maxgeo[2] = mfm_info[drive].cylinders;

	cmdb[0] = drive + 1;
	cmdb[6] = 0;
	cmdb[7] = 1;
	for (attribute = 0; attribute < 3; attribute++) {
		while (mingeo[attribute] != maxgeo[attribute]) {
			unsigned int variable;

			variable = (maxgeo[attribute] + mingeo[attribute]) >> 1;
			cmdb[1] = cmdb[2] = cmdb[3] = cmdb[4] = cmdb[5] = 0;

			if (need_recal) {
				int tries = 5;

				do {
					issue_command (CMD_RCLB, cmdb, 2);
					wait_for_completion ();
					wait_for_command_end ();
					if  (result[1] == 0x20)
						break;
				} while (result[1] && --tries);
				if (result[1]) {
					outw (CMD_RCAL, MFM_COMMAND);
					return 0;
				}
				need_recal = 0;
			}

			switch (attribute) {
			case 0:
				cmdb[5] = variable;
				issue_command (CMD_CMPD, cmdb, 8);
				break;
			case 1:
				cmdb[1] = variable;
				cmdb[4] = variable;
				issue_command (CMD_CMPD, cmdb, 8);
				break;
			case 2:
				cmdb[2] = variable >> 8;
				cmdb[3] = variable;
				issue_command (CMD_SEK, cmdb, 4);
				break;
			}
			wait_for_completion ();
			wait_for_command_end ();

			switch (result[1]) {
			case 0x00:
			case 0x50:
				mingeo[attribute] = variable + 1;
				break;

			case 0x20:
				outw (CMD_RCAL, MFM_COMMAND);
				return 0;

			case 0x24:
				need_recal = 1;
			default:
				maxgeo[attribute] = variable;
				break;
			}
		}
	}
	mfm_info[drive].cylinders  = mingeo[2];
	mfm_info[drive].lowcurrent = mingeo[2];
	mfm_info[drive].precomp    = mingeo[2] / 2;
	mfm_info[drive].heads 	   = mingeo[1];
	mfm_info[drive].sectors	   = mingeo[0];
	outw (CMD_RCAL, MFM_COMMAND);
	return 1;
}
#endif

/*
 * Initialise all drive information for this controller.
 */
static int mfm_initdrives(void)
{
	int drive;

	if (number_mfm_drives > MFM_MAXDRIVES) {
		number_mfm_drives = MFM_MAXDRIVES;
		printk("No. of ADFS MFM drives is greater than MFM_MAXDRIVES - you can't have that many!\n");
	}

	for (drive = 0; drive < number_mfm_drives; drive++) {
		mfm_info[drive].lowcurrent = 1;
		mfm_info[drive].precomp    = 1;
		mfm_info[drive].cylinder   = -1;
		mfm_info[drive].errors.recal  = 0;
		mfm_info[drive].errors.report = 0;
		mfm_info[drive].errors.abort  = 4;

#ifdef CONFIG_BLK_DEV_MFM_AUTODETECT
		mfm_info[drive].cylinders  = 1024;
		mfm_info[drive].heads	   = 8;
		mfm_info[drive].sectors	   = 64;
		{
			unsigned char cmdb[16];

			mfm_setupspecify (drive, cmdb);
			cmdb[1] &= ~0x81;
			issue_command (CMD_SPC, cmdb, 16);
			wait_for_completion ();
			if (!mfm_detectdrive (drive)) {
				mfm_info[drive].cylinders = 0;
				mfm_info[drive].heads     = 0;
				mfm_info[drive].sectors   = 0;
			}
			cmdb[0] = cmdb[1] = 0;
			issue_command (CMD_CKV, cmdb, 2);
		}
#else
		mfm_info[drive].cylinders  = 1;	/* its going to have to figure it out from the partition info */
		mfm_info[drive].heads      = 4;
		mfm_info[drive].sectors    = 32;
#endif
	}
	return number_mfm_drives;
}



/*
 * The 'front' end of the mfm driver follows...
 */

static int mfm_getgeo(struct block_device *bdev, struct hd_geometry *geo)
{
	struct mfm_info *p = bdev->bd_disk->private_data;

	geo->heads = p->heads;
	geo->sectors = p->sectors;
	geo->cylinders = p->cylinders;
	return 0;
}

/*
 * This is to handle various kernel command line parameters
 * specific to this driver.
 */
void mfm_setup(char *str, int *ints)
{
	return;
}

/*
 * Set the CHS from the ADFS boot block if it is present.  This is not ideal
 * since if there are any non-ADFS partitions on the disk, this won't work!
 * Hence, I want to get rid of this...
 */
void xd_set_geometry(struct block_device *bdev, unsigned char secsptrack,
			unsigned char heads, unsigned int secsize)
{
	struct mfm_info *p = bdev->bd_disk->private_data;
	int drive = p - mfm_info;
	unsigned long disksize = bdev->bd_inode->i_size;

	if (p->cylinders == 1) {
		p->sectors = secsptrack;
		p->heads = heads;
		p->cylinders = discsize / (secsptrack * heads * secsize);

		if ((heads < 1) || (p->cylinders > 1024)) {
			printk("%s: Insane disc shape! Setting to 512/4/32\n",
				bdev->bd_disk->disk_name);

			/* These values are fairly arbitary, but are there so that if your
			 * lucky you can pick apart your disc to find out what is going on -
			 * I reckon these figures won't hurt MOST drives
			 */
			p->sectors = 32;
			p->heads = 4;
			p->cylinders = 512;
		}
		if (raw_cmd.dev == drive)
			mfm_specify ();
		mfm_geometry (drive);
	}
}

static struct block_device_operations mfm_fops =
{
	.owner		= THIS_MODULE,
	.getgeo		= mfm_getgeo,
};

/*
 * See if there is a controller at the address presently at mfm_addr
 *
 * We check to see if the controller is busy - if it is, we abort it first,
 * and check that the chip is no longer busy after at least 180 clock cycles.
 * We then issue a command and check that the BSY or CPR bits are set.
 */
static int mfm_probecontroller (unsigned int mfm_addr)
{
	if (inw (MFM_STATUS) & STAT_BSY) {
		outw (CMD_ABT, MFM_COMMAND);
		udelay (50);
		if (inw (MFM_STATUS) & STAT_BSY)
			return 0;
	}

	if (inw (MFM_STATUS) & STAT_CED)
		outw (CMD_RCAL, MFM_COMMAND);

	outw (CMD_SEK, MFM_COMMAND);

	if (inw (MFM_STATUS) & (STAT_BSY | STAT_CPR)) {
		unsigned int count = 2000;
		while (inw (MFM_STATUS) & STAT_BSY) {
			udelay (500);
			if (!--count)
				return 0;
		}

		outw (CMD_RCAL, MFM_COMMAND);
	}
	return 1;
}

static int mfm_do_init(unsigned char irqmask)
{
	int i, ret;

	printk("mfm: found at address %08X, interrupt %d\n", mfm_addr, mfm_irq);

	ret = -EBUSY;
	if (!request_region (mfm_addr, 10, "mfm"))
		goto out1;

	ret = register_blkdev(MAJOR_NR, "mfm");
	if (ret)
		goto out2;

	/* Stuff for the assembler routines to get to */
	hdc63463_baseaddress	= ioaddr(mfm_addr);
	hdc63463_irqpolladdress	= mfm_IRQPollLoc;
	hdc63463_irqpollmask	= irqmask;

	mfm_queue = blk_init_queue(do_mfm_request, &mfm_lock);
	if (!mfm_queue)
		goto out2a;

	Busy = 0;
	lastspecifieddrive = -1;

	mfm_drives = mfm_initdrives();
	if (!mfm_drives) {
		ret = -ENODEV;
		goto out3;
	}
	
	for (i = 0; i < mfm_drives; i++) {
		struct gendisk *disk = alloc_disk(64);
		if (!disk)
			goto Enomem;
		disk->major = MAJOR_NR;
		disk->first_minor = i << 6;
		disk->fops = &mfm_fops;
		sprintf(disk->disk_name, "mfm%c", 'a'+i);
		mfm_gendisk[i] = disk;
	}

	printk("mfm: detected %d hard drive%s\n", mfm_drives,
				mfm_drives == 1 ? "" : "s");
	ret = request_irq(mfm_irq, mfm_interrupt_handler, IRQF_DISABLED, "MFM harddisk", NULL);
	if (ret) {
		printk("mfm: unable to get IRQ%d\n", mfm_irq);
		goto out4;
	}

	if (mfm_irqenable)
		outw(0x80, mfm_irqenable);	/* Required to enable IRQs from MFM podule */

	for (i = 0; i < mfm_drives; i++) {
		mfm_geometry(i);
		mfm_gendisk[i]->queue = mfm_queue;
		add_disk(mfm_gendisk[i]);
	}
	return 0;

out4:
	for (i = 0; i < mfm_drives; i++)
		put_disk(mfm_gendisk[i]);
out3:
	blk_cleanup_queue(mfm_queue);
out2a:
	unregister_blkdev(MAJOR_NR, "mfm");
out2:
	release_region(mfm_addr, 10);
out1:
	return ret;
Enomem:
	while (i--)
		put_disk(mfm_gendisk[i]);
	goto out3;
}

static void mfm_do_exit(void)
{
	int i;

	free_irq(mfm_irq, NULL);
	for (i = 0; i < mfm_drives; i++) {
		del_gendisk(mfm_gendisk[i]);
		put_disk(mfm_gendisk[i]);
	}
	blk_cleanup_queue(mfm_queue);
	unregister_blkdev(MAJOR_NR, "mfm");
	if (mfm_addr)
		release_region(mfm_addr, 10);
}

static int __devinit mfm_probe(struct expansion_card *ec, struct ecard_id *id)
{
	if (mfm_addr)
		return -EBUSY;

	mfm_addr	= ecard_address(ec, ECARD_IOC, ECARD_MEDIUM) + 0x800;
	mfm_IRQPollLoc	= ioaddr(mfm_addr + 0x400);
	mfm_irqenable	= mfm_IRQPollLoc;
	mfm_irq		= ec->irq;

	return mfm_do_init(0x08);
}

static void __devexit mfm_remove(struct expansion_card *ec)
{
	outw (0, mfm_irqenable);	/* Required to enable IRQs from MFM podule */
	mfm_do_exit();
}

static const struct ecard_id mfm_cids[] = {
	{ MANU_ACORN, PROD_ACORN_MFM },
	{ 0xffff, 0xffff },
};

static struct ecard_driver mfm_driver = {
	.probe		= mfm_probe,
	.remove		= __devexit(mfm_remove),
	.id_table	= mfm_cids,
	.drv = {
		.name	= "mfm",
	},
};

/*
 * Look for a MFM controller - first check the motherboard, then the podules
 * The podules have an extra interrupt enable that needs to be played with
 *
 * The HDC is accessed at MEDIUM IOC speeds.
 */
static int __init mfm_init (void)
{
	unsigned char irqmask;

	if (mfm_probecontroller(ONBOARD_MFM_ADDRESS)) {
		mfm_addr	= ONBOARD_MFM_ADDRESS;
		mfm_IRQPollLoc	= IOC_IRQSTATB;
		mfm_irqenable	= 0;
		mfm_irq		= IRQ_HARDDISK;
		return mfm_do_init(0x08);	/* IL3 pin */
	} else {
		return ecard_register_driver(&mfm_driver);
	}
}

static void __exit mfm_exit(void)
{
	if (mfm_addr == ONBOARD_MFM_ADDRESS)
		mfm_do_exit();
	else
		ecard_unregister_driver(&mfm_driver);
}

module_init(mfm_init)
module_exit(mfm_exit)
MODULE_LICENSE("GPL");
