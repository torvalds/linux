/*
* Sony CDU-31A CDROM interface device driver.
*
* Corey Minyard (minyard@wf-rch.cirr.com)
*
* Colossians 3:17
*
*  See Documentation/cdrom/cdu31a for additional details about this driver.
* 
* The Sony interface device driver handles Sony interface CDROM
* drives and provides a complete block-level interface as well as an
* ioctl() interface compatible with the Sun (as specified in
* include/linux/cdrom.h).  With this interface, CDROMs can be
* accessed and standard audio CDs can be played back normally.
*
* WARNING - 	All autoprobes have been removed from the driver.
*		You MUST configure the CDU31A via a LILO config
*		at boot time or in lilo.conf.  I have the
*		following in my lilo.conf:
*
*                append="cdu31a=0x1f88,0,PAS"
*
*		The first number is the I/O base address of the
*		card.  The second is the interrupt (0 means none).
 *		The third should be "PAS" if on a Pro-Audio
 *		spectrum, or nothing if on something else.
 *
 * This interface is (unfortunately) a polled interface.  This is
 * because most Sony interfaces are set up with DMA and interrupts
 * disables.  Some (like mine) do not even have the capability to
 * handle interrupts or DMA.  For this reason you will see a lot of
 * the following:
 *
 *   retry_count = jiffies+ SONY_JIFFIES_TIMEOUT;
 *   while (time_before(jiffies, retry_count) && (! <some condition to wait for))
 *   {
 *      while (handle_sony_cd_attention())
 *         ;
 *
 *      sony_sleep();
 *   }
 *   if (the condition not met)
 *   {
 *      return an error;
 *   }
 *
 * This ugly hack waits for something to happen, sleeping a little
 * between every try.  it also handles attentions, which are
 * asynchronous events from the drive informing the driver that a disk
 * has been inserted, removed, etc.
 *
 * NEWS FLASH - The driver now supports interrupts but they are
 * turned off by default.  Use of interrupts is highly encouraged, it
 * cuts CPU usage down to a reasonable level.  I had DMA in for a while
 * but PC DMA is just too slow.  Better to just insb() it.
 *
 * One thing about these drives: They talk in MSF (Minute Second Frame) format.
 * There are 75 frames a second, 60 seconds a minute, and up to 75 minutes on a
 * disk.  The funny thing is that these are sent to the drive in BCD, but the
 * interface wants to see them in decimal.  A lot of conversion goes on.
 *
 * DRIVER SPECIAL FEATURES
 * -----------------------
 *
 * This section describes features beyond the normal audio and CD-ROM
 * functions of the drive.
 *
 * XA compatibility
 *
 * The driver should support XA disks for both the CDU31A and CDU33A.
 * It does this transparently, the using program doesn't need to set it.
 *
 * Multi-Session
 *
 * A multi-session disk looks just like a normal disk to the user.
 * Just mount one normally, and all the data should be there.
 * A special thanks to Koen for help with this!
 * 
 * Raw sector I/O
 *
 * Using the CDROMREADAUDIO it is possible to read raw audio and data
 * tracks.  Both operations return 2352 bytes per sector.  On the data
 * tracks, the first 12 bytes is not returned by the drive and the value
 * of that data is indeterminate.
 *
 *
 *  Copyright (C) 1993  Corey Minyard
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * TODO: 
 *       CDs with form1 and form2 sectors cause problems
 *       with current read-ahead strategy.
 *
 * Credits:
 *    Heiko Eissfeldt <heiko@colossus.escape.de>
 *         For finding abug in the return of the track numbers.
 *         TOC processing redone for proper multisession support.
 *
 *
 *  It probably a little late to be adding a history, but I guess I
 *  will start.
 *
 *  10/24/95 - Added support for disabling the eject button when the
 *             drive is open.  Note that there is a small problem
 *             still here, if the eject button is pushed while the
 *             drive light is flashing, the drive will return a bad
 *             status and be reset.  It recovers, though.
 *
 *  03/07/97 - Fixed a problem with timers.
 *
 *
 *  18 Spetember 1997 -- Ported to Uniform CD-ROM driver by 
 *                 Heiko Eissfeldt <heiko@colossus.escape.de> with additional
 *                 changes by Erik Andersen <andersee@debian.org>
 *
 *  24 January 1998 -- Removed the scd_disc_status() function, which was now
 *                     just dead code left over from the port.
 *                          Erik Andersen <andersee@debian.org>
 *
 *  16 July 1998 -- Drive donated to Erik Andersen by John Kodis
 *                   <kodis@jagunet.com>.  Work begun on fixing driver to
 *                   work under 2.1.X.  Added temporary extra printks
 *                   which seem to slow it down enough to work.
 *
 *  9 November 1999 -- Make kernel-parameter implementation work with 2.3.x 
 *	               Removed init_module & cleanup_module in favor of 
 *		       module_init & module_exit.
 *		       Torben Mathiasen <tmm@image.dk>
 *
 * 22 October 2004 -- Make the driver work in 2.6.X
 *		      Added workaround to fix hard lockups on eject
 *		      Fixed door locking problem after mounting empty drive
 *		      Set double-speed drives to double speed by default
 *		      Removed all readahead things - not needed anymore
 *			Ondrej Zary <rainbow@rainbow-software.org>
*/

#define DEBUG 1

#include <linux/major.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/timer.h>
#include <linux/fs.h>
#include <linux/kernel.h>
#include <linux/hdreg.h>
#include <linux/genhd.h>
#include <linux/ioport.h>
#include <linux/devfs_fs_kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/cdrom.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/dma.h>

#include "cdu31a.h"

#define MAJOR_NR CDU31A_CDROM_MAJOR
#include <linux/blkdev.h>

#define CDU31A_MAX_CONSECUTIVE_ATTENTIONS 10

#define PFX "CDU31A: "

/*
** Edit the following data to change interrupts, DMA channels, etc.
** Default is polled and no DMA.  DMA is not recommended for double-speed
** drives.
*/
static struct {
	unsigned short base;	/* I/O Base Address */
	short int_num;		/* Interrupt Number (-1 means scan for it,
				   0 means don't use) */
} cdu31a_addresses[] __initdata = {
	{0}
};

static int handle_sony_cd_attention(void);
static int read_subcode(void);
static void sony_get_toc(void);
static int scd_spinup(void);
/*static int scd_open(struct inode *inode, struct file *filp);*/
static int scd_open(struct cdrom_device_info *, int);
static void do_sony_cd_cmd(unsigned char cmd,
			   unsigned char *params,
			   unsigned int num_params,
			   unsigned char *result_buffer,
			   unsigned int *result_size);
static void size_to_buf(unsigned int size, unsigned char *buf);

/* Parameters for the read-ahead. */
static unsigned int sony_next_block;	/* Next 512 byte block offset */
static unsigned int sony_blocks_left = 0;	/* Number of 512 byte blocks left
						   in the current read command. */


/* The base I/O address of the Sony Interface.  This is a variable (not a
   #define) so it can be easily changed via some future ioctl() */
static unsigned int cdu31a_port = 0;
module_param(cdu31a_port, uint, 0);

/*
 * The following are I/O addresses of the various registers for the drive.  The
 * comment for the base address also applies here.
 */
static volatile unsigned short sony_cd_cmd_reg;
static volatile unsigned short sony_cd_param_reg;
static volatile unsigned short sony_cd_write_reg;
static volatile unsigned short sony_cd_control_reg;
static volatile unsigned short sony_cd_status_reg;
static volatile unsigned short sony_cd_result_reg;
static volatile unsigned short sony_cd_read_reg;
static volatile unsigned short sony_cd_fifost_reg;

static struct request_queue *cdu31a_queue;
static DEFINE_SPINLOCK(cdu31a_lock); /* queue lock */

static int sony_spun_up = 0;	/* Has the drive been spun up? */

static int sony_speed = 0;	/* Last wanted speed */

static int sony_xa_mode = 0;	/* Is an XA disk in the drive
				   and the drive a CDU31A? */

static int sony_raw_data_mode = 1;	/* 1 if data tracks, 0 if audio.
					   For raw data reads. */

static unsigned int sony_usage = 0;	/* How many processes have the
					   drive open. */

static int sony_pas_init = 0;	/* Initialize the Pro-Audio
				   Spectrum card? */

static struct s_sony_session_toc single_toc;	/* Holds the
						   table of
						   contents. */

static struct s_all_sessions_toc sony_toc;	/* entries gathered from all
						   sessions */

static int sony_toc_read = 0;	/* Has the TOC been read for
				   the drive? */

static struct s_sony_subcode last_sony_subcode;	/* Points to the last
						   subcode address read */

static DECLARE_MUTEX(sony_sem);		/* Semaphore for drive hardware access */

static int is_double_speed = 0;	/* does the drive support double speed ? */

static int is_auto_eject = 1;	/* Door has been locked? 1=No/0=Yes */

/*
 * The audio status uses the values from read subchannel data as specified
 * in include/linux/cdrom.h.
 */
static volatile int sony_audio_status = CDROM_AUDIO_NO_STATUS;

/*
 * The following are a hack for pausing and resuming audio play.  The drive
 * does not work as I would expect it, if you stop it then start it again,
 * the drive seeks back to the beginning and starts over.  This holds the
 * position during a pause so a resume can restart it.  It uses the
 * audio status variable above to tell if it is paused.
 */
static unsigned volatile char cur_pos_msf[3] = { 0, 0, 0 };
static unsigned volatile char final_pos_msf[3] = { 0, 0, 0 };

/* What IRQ is the drive using?  0 if none. */
static int cdu31a_irq = 0;
module_param(cdu31a_irq, int, 0);

/* The interrupt handler will wake this queue up when it gets an
   interrupts. */
static DECLARE_WAIT_QUEUE_HEAD(cdu31a_irq_wait);
static int irq_flag = 0;

static int curr_control_reg = 0;	/* Current value of the control register */

/* A disk changed variable.  When a disk change is detected, it will
   all be set to TRUE.  As the upper layers ask for disk_changed status
   it will be cleared. */
static char disk_changed;

/* This was readahead_buffer once... Now it's used only for audio reads */
static char audio_buffer[CD_FRAMESIZE_RAW];

/* Used to time a short period to abort an operation after the
   drive has been idle for a while.  This keeps the light on
   the drive from flashing for very long. */
static struct timer_list cdu31a_abort_timer;

/* Marks if the timeout has started an abort read.  This is used
   on entry to the drive to tell the code to read out the status
   from the abort read. */
static int abort_read_started = 0;

/*
 * Uniform cdrom interface function
 * report back, if disc has changed from time of last request.
 */
static int scd_media_changed(struct cdrom_device_info *cdi, int disc_nr)
{
	int retval;

	retval = disk_changed;
	disk_changed = 0;

	return retval;
}

/*
 * Uniform cdrom interface function
 * report back, if drive is ready
 */
static int scd_drive_status(struct cdrom_device_info *cdi, int slot_nr)
{
	if (CDSL_CURRENT != slot_nr)
		/* we have no changer support */
		return -EINVAL;
	if (sony_spun_up)
		return CDS_DISC_OK;
	if (down_interruptible(&sony_sem))
		return -ERESTARTSYS;
	if (scd_spinup() == 0)
		sony_spun_up = 1;
	up(&sony_sem);
	return sony_spun_up ? CDS_DISC_OK : CDS_DRIVE_NOT_READY;
}

static inline void enable_interrupts(void)
{
	curr_control_reg |= (SONY_ATTN_INT_EN_BIT
			     | SONY_RES_RDY_INT_EN_BIT
			     | SONY_DATA_RDY_INT_EN_BIT);
	outb(curr_control_reg, sony_cd_control_reg);
}

static inline void disable_interrupts(void)
{
	curr_control_reg &= ~(SONY_ATTN_INT_EN_BIT
			      | SONY_RES_RDY_INT_EN_BIT
			      | SONY_DATA_RDY_INT_EN_BIT);
	outb(curr_control_reg, sony_cd_control_reg);
}

/*
 * Wait a little while (used for polling the drive).  If in initialization,
 * setting a timeout doesn't work, so just loop for a while.
 */
static inline void sony_sleep(void)
{
	if (cdu31a_irq <= 0) {
		yield();
	} else {		/* Interrupt driven */
		DEFINE_WAIT(w);
		int first = 1;

		while (1) {
			prepare_to_wait(&cdu31a_irq_wait, &w,
					TASK_INTERRUPTIBLE);
			if (first) {
				enable_interrupts();
				first = 0;
			}

			if (irq_flag != 0)
				break;
			if (!signal_pending(current)) {
				schedule();
				continue;
			} else
				disable_interrupts();
			break;
		}
		finish_wait(&cdu31a_irq_wait, &w);
		irq_flag = 0;
	}
}


/*
 * The following are convenience routine to read various status and set
 * various conditions in the drive.
 */
static inline int is_attention(void)
{
	return (inb(sony_cd_status_reg) & SONY_ATTN_BIT) != 0;
}

static inline int is_busy(void)
{
	return (inb(sony_cd_status_reg) & SONY_BUSY_BIT) != 0;
}

static inline int is_data_ready(void)
{
	return (inb(sony_cd_status_reg) & SONY_DATA_RDY_BIT) != 0;
}

static inline int is_data_requested(void)
{
	return (inb(sony_cd_status_reg) & SONY_DATA_REQUEST_BIT) != 0;
}

static inline int is_result_ready(void)
{
	return (inb(sony_cd_status_reg) & SONY_RES_RDY_BIT) != 0;
}

static inline int is_param_write_rdy(void)
{
	return (inb(sony_cd_fifost_reg) & SONY_PARAM_WRITE_RDY_BIT) != 0;
}

static inline int is_result_reg_not_empty(void)
{
	return (inb(sony_cd_fifost_reg) & SONY_RES_REG_NOT_EMP_BIT) != 0;
}

static inline void reset_drive(void)
{
	curr_control_reg = 0;
	sony_toc_read = 0;
	outb(SONY_DRIVE_RESET_BIT, sony_cd_control_reg);
}

/*
 * Uniform cdrom interface function
 * reset drive and return when it is ready
 */
static int scd_reset(struct cdrom_device_info *cdi)
{
	unsigned long retry_count;

	if (down_interruptible(&sony_sem))
		return -ERESTARTSYS;
	reset_drive();

	retry_count = jiffies + SONY_RESET_TIMEOUT;
	while (time_before(jiffies, retry_count) && (!is_attention())) {
		sony_sleep();
	}

	up(&sony_sem);
	return 0;
}

static inline void clear_attention(void)
{
	outb(curr_control_reg | SONY_ATTN_CLR_BIT, sony_cd_control_reg);
}

static inline void clear_result_ready(void)
{
	outb(curr_control_reg | SONY_RES_RDY_CLR_BIT, sony_cd_control_reg);
}

static inline void clear_data_ready(void)
{
	outb(curr_control_reg | SONY_DATA_RDY_CLR_BIT,
	     sony_cd_control_reg);
}

static inline void clear_param_reg(void)
{
	outb(curr_control_reg | SONY_PARAM_CLR_BIT, sony_cd_control_reg);
}

static inline unsigned char read_status_register(void)
{
	return inb(sony_cd_status_reg);
}

static inline unsigned char read_result_register(void)
{
	return inb(sony_cd_result_reg);
}

static inline unsigned char read_data_register(void)
{
	return inb(sony_cd_read_reg);
}

static inline void write_param(unsigned char param)
{
	outb(param, sony_cd_param_reg);
}

static inline void write_cmd(unsigned char cmd)
{
	outb(curr_control_reg | SONY_RES_RDY_INT_EN_BIT,
	     sony_cd_control_reg);
	outb(cmd, sony_cd_cmd_reg);
}

static irqreturn_t cdu31a_interrupt(int irq, void *dev_id, struct pt_regs *regs)
{
	unsigned char val;

	if (abort_read_started) {
		/* We might be waiting for an abort to finish.  Don't
		   disable interrupts yet, though, because we handle
		   this one here. */
		/* Clear out the result registers. */
		while (is_result_reg_not_empty()) {
			val = read_result_register();
		}
		clear_data_ready();
		clear_result_ready();

		/* Clear out the data */
		while (is_data_requested()) {
			val = read_data_register();
		}
		abort_read_started = 0;

		/* If something was waiting, wake it up now. */
		if (waitqueue_active(&cdu31a_irq_wait)) {
			disable_interrupts();
			irq_flag = 1;
			wake_up_interruptible(&cdu31a_irq_wait);
		}
	} else if (waitqueue_active(&cdu31a_irq_wait)) {
		disable_interrupts();
		irq_flag = 1;
		wake_up_interruptible(&cdu31a_irq_wait);
	} else {
		disable_interrupts();
		printk(KERN_NOTICE PFX
				"Got an interrupt but nothing was waiting\n");
	}
	return IRQ_HANDLED;
}

/*
 * give more verbose error messages
 */
static unsigned char *translate_error(unsigned char err_code)
{
	static unsigned char errbuf[80];

	switch (err_code) {
		case 0x10: return "illegal command ";
		case 0x11: return "illegal parameter ";

		case 0x20: return "not loaded ";
		case 0x21: return "no disc ";
		case 0x22: return "not spinning ";
		case 0x23: return "spinning ";
		case 0x25: return "spindle servo ";
		case 0x26: return "focus servo ";
		case 0x29: return "eject mechanism ";
		case 0x2a: return "audio playing ";
		case 0x2c: return "emergency eject ";

		case 0x30: return "focus ";
		case 0x31: return "frame sync ";
		case 0x32: return "subcode address ";
		case 0x33: return "block sync ";
		case 0x34: return "header address ";

		case 0x40: return "illegal track read ";
		case 0x41: return "mode 0 read ";
		case 0x42: return "illegal mode read ";
		case 0x43: return "illegal block size read ";
		case 0x44: return "mode read ";
		case 0x45: return "form read ";
		case 0x46: return "leadout read ";
		case 0x47: return "buffer overrun ";

		case 0x53: return "unrecoverable CIRC ";
		case 0x57: return "unrecoverable LECC ";

		case 0x60: return "no TOC ";
		case 0x61: return "invalid subcode data ";
		case 0x63: return "focus on TOC read ";
		case 0x64: return "frame sync on TOC read ";
		case 0x65: return "TOC data ";

		case 0x70: return "hardware failure ";
		case 0x91: return "leadin ";
		case 0x92: return "leadout ";
		case 0x93: return "data track ";
	}
	sprintf(errbuf, "unknown 0x%02x ", err_code);
	return errbuf;
}

/*
 * Set the drive parameters so the drive will auto-spin-up when a
 * disk is inserted.
 */
static void set_drive_params(int want_doublespeed)
{
	unsigned char res_reg[12];
	unsigned int res_size;
	unsigned char params[3];


	params[0] = SONY_SD_AUTO_SPIN_DOWN_TIME;
	params[1] = 0x00;	/* Never spin down the drive. */
	do_sony_cd_cmd(SONY_SET_DRIVE_PARAM_CMD,
		       params, 2, res_reg, &res_size);
	if ((res_size < 2) || ((res_reg[0] & 0xf0) == 0x20)) {
		printk(KERN_NOTICE PFX
			"Unable to set spin-down time: 0x%2.2x\n", res_reg[1]);
	}

	params[0] = SONY_SD_MECH_CONTROL;
	params[1] = SONY_AUTO_SPIN_UP_BIT;	/* Set auto spin up */

	if (is_auto_eject)
		params[1] |= SONY_AUTO_EJECT_BIT;

	if (is_double_speed && want_doublespeed) {
		params[1] |= SONY_DOUBLE_SPEED_BIT;	/* Set the drive to double speed if 
							   possible */
	}
	do_sony_cd_cmd(SONY_SET_DRIVE_PARAM_CMD,
		       params, 2, res_reg, &res_size);
	if ((res_size < 2) || ((res_reg[0] & 0xf0) == 0x20)) {
		printk(KERN_NOTICE PFX "Unable to set mechanical "
				"parameters: 0x%2.2x\n", res_reg[1]);
	}
}

/*
 * Uniform cdrom interface function
 * select reading speed for data access
 */
static int scd_select_speed(struct cdrom_device_info *cdi, int speed)
{
	if (speed == 0)
		sony_speed = 1;
	else
		sony_speed = speed - 1;

	if (down_interruptible(&sony_sem))
		return -ERESTARTSYS;
	set_drive_params(sony_speed);
	up(&sony_sem);
	return 0;
}

/*
 * Uniform cdrom interface function
 * lock or unlock eject button
 */
static int scd_lock_door(struct cdrom_device_info *cdi, int lock)
{
	if (lock == 0) {
		is_auto_eject = 1;
	} else {
		is_auto_eject = 0;
	}
	if (down_interruptible(&sony_sem))
		return -ERESTARTSYS;
	set_drive_params(sony_speed);
	up(&sony_sem);
	return 0;
}

/*
 * This code will reset the drive and attempt to restore sane parameters.
 */
static void restart_on_error(void)
{
	unsigned char res_reg[12];
	unsigned int res_size;
	unsigned long retry_count;


	printk(KERN_NOTICE PFX "Resetting drive on error\n");
	reset_drive();
	retry_count = jiffies + SONY_RESET_TIMEOUT;
	while (time_before(jiffies, retry_count) && (!is_attention())) {
		sony_sleep();
	}
	set_drive_params(sony_speed);
	do_sony_cd_cmd(SONY_SPIN_UP_CMD, NULL, 0, res_reg, &res_size);
	if ((res_size < 2) || ((res_reg[0] & 0xf0) == 0x20)) {
		printk(KERN_NOTICE PFX "Unable to spin up drive: 0x%2.2x\n",
		       res_reg[1]);
	}

	msleep(2000);

	sony_get_toc();
}

/*
 * This routine writes data to the parameter register.  Since this should
 * happen fairly fast, it is polled with no OS waits between.
 */
static int write_params(unsigned char *params, int num_params)
{
	unsigned int retry_count;


	retry_count = SONY_READY_RETRIES;
	while ((retry_count > 0) && (!is_param_write_rdy())) {
		retry_count--;
	}
	if (!is_param_write_rdy()) {
		return -EIO;
	}

	while (num_params > 0) {
		write_param(*params);
		params++;
		num_params--;
	}

	return 0;
}


/*
 * The following reads data from the command result register.  It is a
 * fairly complex routine, all status info flows back through this
 * interface.  The algorithm is stolen directly from the flowcharts in
 * the drive manual.
 */
static void
get_result(unsigned char *result_buffer, unsigned int *result_size)
{
	unsigned char a, b;
	int i;
	unsigned long retry_count;


	while (handle_sony_cd_attention());
	/* Wait for the result data to be ready */
	retry_count = jiffies + SONY_JIFFIES_TIMEOUT;
	while (time_before(jiffies, retry_count)
	       && (is_busy() || (!(is_result_ready())))) {
		sony_sleep();

		while (handle_sony_cd_attention());
	}
	if (is_busy() || (!(is_result_ready()))) {
		pr_debug(PFX "timeout out %d\n", __LINE__);
		result_buffer[0] = 0x20;
		result_buffer[1] = SONY_TIMEOUT_OP_ERR;
		*result_size = 2;
		return;
	}

	/*
	 * Get the first two bytes.  This determines what else needs
	 * to be done.
	 */
	clear_result_ready();
	a = read_result_register();
	*result_buffer = a;
	result_buffer++;

	/* Check for block error status result. */
	if ((a & 0xf0) == 0x50) {
		*result_size = 1;
		return;
	}

	b = read_result_register();
	*result_buffer = b;
	result_buffer++;
	*result_size = 2;

	/*
	 * 0x20 means an error occurred.  Byte 2 will have the error code.
	 * Otherwise, the command succeeded, byte 2 will have the count of
	 * how many more status bytes are coming.
	 *
	 * The result register can be read 10 bytes at a time, a wait for
	 * result ready to be asserted must be done between every 10 bytes.
	 */
	if ((a & 0xf0) != 0x20) {
		if (b > 8) {
			for (i = 0; i < 8; i++) {
				*result_buffer = read_result_register();
				result_buffer++;
				(*result_size)++;
			}
			b = b - 8;

			while (b > 10) {
				retry_count = SONY_READY_RETRIES;
				while ((retry_count > 0)
				       && (!is_result_ready())) {
					retry_count--;
				}
				if (!is_result_ready()) {
					pr_debug(PFX "timeout out %d\n",
					       __LINE__);
					result_buffer[0] = 0x20;
					result_buffer[1] =
					    SONY_TIMEOUT_OP_ERR;
					*result_size = 2;
					return;
				}

				clear_result_ready();

				for (i = 0; i < 10; i++) {
					*result_buffer =
					    read_result_register();
					result_buffer++;
					(*result_size)++;
				}
				b = b - 10;
			}

			if (b > 0) {
				retry_count = SONY_READY_RETRIES;
				while ((retry_count > 0)
				       && (!is_result_ready())) {
					retry_count--;
				}
				if (!is_result_ready()) {
					pr_debug(PFX "timeout out %d\n",
					       __LINE__);
					result_buffer[0] = 0x20;
					result_buffer[1] =
					    SONY_TIMEOUT_OP_ERR;
					*result_size = 2;
					return;
				}
			}
		}

		while (b > 0) {
			*result_buffer = read_result_register();
			result_buffer++;
			(*result_size)++;
			b--;
		}
	}
}

/*
 * Do a command that does not involve data transfer.  This routine must
 * be re-entrant from the same task to support being called from the
 * data operation code when an error occurs.
 */
static void
do_sony_cd_cmd(unsigned char cmd,
	       unsigned char *params,
	       unsigned int num_params,
	       unsigned char *result_buffer, unsigned int *result_size)
{
	unsigned long retry_count;
	int num_retries = 0;

retry_cd_operation:

	while (handle_sony_cd_attention());

	retry_count = jiffies + SONY_JIFFIES_TIMEOUT;
	while (time_before(jiffies, retry_count) && (is_busy())) {
		sony_sleep();

		while (handle_sony_cd_attention());
	}
	if (is_busy()) {
		pr_debug(PFX "timeout out %d\n", __LINE__);
		result_buffer[0] = 0x20;
		result_buffer[1] = SONY_TIMEOUT_OP_ERR;
		*result_size = 2;
	} else {
		clear_result_ready();
		clear_param_reg();

		write_params(params, num_params);
		write_cmd(cmd);

		get_result(result_buffer, result_size);
	}

	if (((result_buffer[0] & 0xf0) == 0x20)
	    && (num_retries < MAX_CDU31A_RETRIES)) {
		num_retries++;
		msleep(100);
		goto retry_cd_operation;
	}
}


/*
 * Handle an attention from the drive.  This will return 1 if it found one
 * or 0 if not (if one is found, the caller might want to call again).
 *
 * This routine counts the number of consecutive times it is called
 * (since this is always called from a while loop until it returns
 * a 0), and returns a 0 if it happens too many times.  This will help
 * prevent a lockup.
 */
static int handle_sony_cd_attention(void)
{
	unsigned char atten_code;
	static int num_consecutive_attentions = 0;
	volatile int val;


#if 0
	pr_debug(PFX "Entering %s\n", __FUNCTION__);
#endif
	if (is_attention()) {
		if (num_consecutive_attentions >
		    CDU31A_MAX_CONSECUTIVE_ATTENTIONS) {
			printk(KERN_NOTICE PFX "Too many consecutive "
				"attentions: %d\n", num_consecutive_attentions);
			num_consecutive_attentions = 0;
			pr_debug(PFX "Leaving %s at %d\n", __FUNCTION__,
			       __LINE__);
			return 0;
		}

		clear_attention();
		atten_code = read_result_register();

		switch (atten_code) {
			/* Someone changed the CD.  Mark it as changed */
		case SONY_MECH_LOADED_ATTN:
			disk_changed = 1;
			sony_toc_read = 0;
			sony_audio_status = CDROM_AUDIO_NO_STATUS;
			sony_blocks_left = 0;
			break;

		case SONY_SPIN_DOWN_COMPLETE_ATTN:
			/* Mark the disk as spun down. */
			sony_spun_up = 0;
			break;

		case SONY_AUDIO_PLAY_DONE_ATTN:
			sony_audio_status = CDROM_AUDIO_COMPLETED;
			read_subcode();
			break;

		case SONY_EJECT_PUSHED_ATTN:
			if (is_auto_eject) {
				sony_audio_status = CDROM_AUDIO_INVALID;
			}
			break;

		case SONY_LEAD_IN_ERR_ATTN:
		case SONY_LEAD_OUT_ERR_ATTN:
		case SONY_DATA_TRACK_ERR_ATTN:
		case SONY_AUDIO_PLAYBACK_ERR_ATTN:
			sony_audio_status = CDROM_AUDIO_ERROR;
			break;
		}

		num_consecutive_attentions++;
		pr_debug(PFX "Leaving %s at %d\n", __FUNCTION__, __LINE__);
		return 1;
	} else if (abort_read_started) {
		while (is_result_reg_not_empty()) {
			val = read_result_register();
		}
		clear_data_ready();
		clear_result_ready();
		/* Clear out the data */
		while (is_data_requested()) {
			val = read_data_register();
		}
		abort_read_started = 0;
		pr_debug(PFX "Leaving %s at %d\n", __FUNCTION__, __LINE__);
		return 1;
	}

	num_consecutive_attentions = 0;
#if 0
	pr_debug(PFX "Leaving %s at %d\n", __FUNCTION__, __LINE__);
#endif
	return 0;
}


/* Convert from an integer 0-99 to BCD */
static inline unsigned int int_to_bcd(unsigned int val)
{
	int retval;


	retval = (val / 10) << 4;
	retval = retval | val % 10;
	return retval;
}


/* Convert from BCD to an integer from 0-99 */
static unsigned int bcd_to_int(unsigned int bcd)
{
	return (((bcd >> 4) & 0x0f) * 10) + (bcd & 0x0f);
}


/*
 * Convert a logical sector value (like the OS would want to use for
 * a block device) to an MSF format.
 */
static void log_to_msf(unsigned int log, unsigned char *msf)
{
	log = log + LOG_START_OFFSET;
	msf[0] = int_to_bcd(log / 4500);
	log = log % 4500;
	msf[1] = int_to_bcd(log / 75);
	msf[2] = int_to_bcd(log % 75);
}


/*
 * Convert an MSF format to a logical sector.
 */
static unsigned int msf_to_log(unsigned char *msf)
{
	unsigned int log;


	log = msf[2];
	log += msf[1] * 75;
	log += msf[0] * 4500;
	log = log - LOG_START_OFFSET;

	return log;
}


/*
 * Take in integer size value and put it into a buffer like
 * the drive would want to see a number-of-sector value.
 */
static void size_to_buf(unsigned int size, unsigned char *buf)
{
	buf[0] = size / 65536;
	size = size % 65536;
	buf[1] = size / 256;
	buf[2] = size % 256;
}

/* Starts a read operation. Returns 0 on success and 1 on failure. 
   The read operation used here allows multiple sequential sectors 
   to be read and status returned for each sector.  The driver will
   read the output one at a time as the requests come and abort the
   operation if the requested sector is not the next one from the
   drive. */
static int
start_request(unsigned int sector, unsigned int nsect)
{
	unsigned char params[6];
	unsigned long retry_count;


	pr_debug(PFX "Entering %s\n", __FUNCTION__);
	log_to_msf(sector, params);
	size_to_buf(nsect, &params[3]);

	/*
	 * Clear any outstanding attentions and wait for the drive to
	 * complete any pending operations.
	 */
	while (handle_sony_cd_attention());

	retry_count = jiffies + SONY_JIFFIES_TIMEOUT;
	while (time_before(jiffies, retry_count) && (is_busy())) {
		sony_sleep();

		while (handle_sony_cd_attention());
	}

	if (is_busy()) {
		printk(KERN_NOTICE PFX "Timeout while waiting "
				"to issue command\n");
		pr_debug(PFX "Leaving %s at %d\n", __FUNCTION__, __LINE__);
		return 1;
	} else {
		/* Issue the command */
		clear_result_ready();
		clear_param_reg();

		write_params(params, 6);
		write_cmd(SONY_READ_BLKERR_STAT_CMD);

		sony_blocks_left = nsect * 4;
		sony_next_block = sector * 4;
		pr_debug(PFX "Leaving %s at %d\n", __FUNCTION__, __LINE__);
		return 0;
	}
	pr_debug(PFX "Leaving %s at %d\n", __FUNCTION__, __LINE__);
}

/* Abort a pending read operation.  Clear all the drive status variables. */
static void abort_read(void)
{
	unsigned char result_reg[2];
	int result_size;
	volatile int val;


	do_sony_cd_cmd(SONY_ABORT_CMD, NULL, 0, result_reg, &result_size);
	if ((result_reg[0] & 0xf0) == 0x20) {
		printk(KERN_ERR PFX "Aborting read, %s error\n",
		       translate_error(result_reg[1]));
	}

	while (is_result_reg_not_empty()) {
		val = read_result_register();
	}
	clear_data_ready();
	clear_result_ready();
	/* Clear out the data */
	while (is_data_requested()) {
		val = read_data_register();
	}

	sony_blocks_left = 0;
}

/* Called when the timer times out.  This will abort the
   pending read operation. */
static void handle_abort_timeout(unsigned long data)
{
	pr_debug(PFX "Entering %s\n", __FUNCTION__);
	/* If it is in use, ignore it. */
	if (down_trylock(&sony_sem) == 0) {
		/* We can't use abort_read(), because it will sleep
		   or schedule in the timer interrupt.  Just start
		   the operation, finish it on the next access to
		   the drive. */
		clear_result_ready();
		clear_param_reg();
		write_cmd(SONY_ABORT_CMD);

		sony_blocks_left = 0;
		abort_read_started = 1;
		up(&sony_sem);
	}
	pr_debug(PFX "Leaving %s\n", __FUNCTION__);
}

/* Actually get one sector of data from the drive. */
static void
input_data_sector(char *buffer)
{
	pr_debug(PFX "Entering %s\n", __FUNCTION__);

	/* If an XA disk on a CDU31A, skip the first 12 bytes of data from
	   the disk.  The real data is after that. We can use audio_buffer. */
	if (sony_xa_mode)
		insb(sony_cd_read_reg, audio_buffer, CD_XA_HEAD);

	clear_data_ready();

	insb(sony_cd_read_reg, buffer, 2048);

	/* If an XA disk, we have to clear out the rest of the unused
	   error correction data. We can use audio_buffer for that. */
	if (sony_xa_mode)
		insb(sony_cd_read_reg, audio_buffer, CD_XA_TAIL);

	pr_debug(PFX "Leaving %s\n", __FUNCTION__);
}

/* read data from the drive.  Note the nsect must be <= 4. */
static void
read_data_block(char *buffer,
		unsigned int block,
		unsigned int nblocks,
		unsigned char res_reg[], int *res_size)
{
	unsigned long retry_count;

	pr_debug(PFX "Entering %s\n", __FUNCTION__);

	res_reg[0] = 0;
	res_reg[1] = 0;
	*res_size = 0;

	/* Wait for the drive to tell us we have something */
	retry_count = jiffies + SONY_JIFFIES_TIMEOUT;
	while (time_before(jiffies, retry_count) && !(is_data_ready())) {
		while (handle_sony_cd_attention());

		sony_sleep();
	}
	if (!(is_data_ready())) {
		if (is_result_ready()) {
			get_result(res_reg, res_size);
			if ((res_reg[0] & 0xf0) != 0x20) {
				printk(KERN_NOTICE PFX "Got result that should"
					" have been error: %d\n", res_reg[0]);
				res_reg[0] = 0x20;
				res_reg[1] = SONY_BAD_DATA_ERR;
				*res_size = 2;
			}
			abort_read();
		} else {
			pr_debug(PFX "timeout out %d\n", __LINE__);
			res_reg[0] = 0x20;
			res_reg[1] = SONY_TIMEOUT_OP_ERR;
			*res_size = 2;
			abort_read();
		}
	} else {
		input_data_sector(buffer);
		sony_blocks_left -= nblocks;
		sony_next_block += nblocks;

		/* Wait for the status from the drive. */
		retry_count = jiffies + SONY_JIFFIES_TIMEOUT;
		while (time_before(jiffies, retry_count)
		       && !(is_result_ready())) {
			while (handle_sony_cd_attention());

			sony_sleep();
		}

		if (!is_result_ready()) {
			pr_debug(PFX "timeout out %d\n", __LINE__);
			res_reg[0] = 0x20;
			res_reg[1] = SONY_TIMEOUT_OP_ERR;
			*res_size = 2;
			abort_read();
		} else {
			get_result(res_reg, res_size);

			/* If we got a buffer status, handle that. */
			if ((res_reg[0] & 0xf0) == 0x50) {

				if ((res_reg[0] ==
				     SONY_NO_CIRC_ERR_BLK_STAT)
				    || (res_reg[0] ==
					SONY_NO_LECC_ERR_BLK_STAT)
				    || (res_reg[0] ==
					SONY_RECOV_LECC_ERR_BLK_STAT)) {
					/* nothing here */
				} else {
					printk(KERN_ERR PFX "Data block "
						"error: 0x%x\n", res_reg[0]);
					res_reg[0] = 0x20;
					res_reg[1] = SONY_BAD_DATA_ERR;
					*res_size = 2;
				}

				/* Final transfer is done for read command, get final result. */
				if (sony_blocks_left == 0) {
					get_result(res_reg, res_size);
				}
			} else if ((res_reg[0] & 0xf0) != 0x20) {
				/* The drive gave me bad status, I don't know what to do.
				   Reset the driver and return an error. */
				printk(KERN_ERR PFX "Invalid block "
					"status: 0x%x\n", res_reg[0]);
				restart_on_error();
				res_reg[0] = 0x20;
				res_reg[1] = SONY_BAD_DATA_ERR;
				*res_size = 2;
			}
		}
	}
	pr_debug(PFX "Leaving %s at %d\n", __FUNCTION__, __LINE__);
}


/*
 * The OS calls this to perform a read or write operation to the drive.
 * Write obviously fail.  Reads to a read ahead of sony_buffer_size
 * bytes to help speed operations.  This especially helps since the OS
 * uses 1024 byte blocks and the drive uses 2048 byte blocks.  Since most
 * data access on a CD is done sequentially, this saves a lot of operations.
 */
static void do_cdu31a_request(request_queue_t * q)
{
	struct request *req;
	int block, nblock, num_retries;
	unsigned char res_reg[12];
	unsigned int res_size;

	pr_debug(PFX "Entering %s\n", __FUNCTION__);

	spin_unlock_irq(q->queue_lock);
	if (down_interruptible(&sony_sem)) {
		spin_lock_irq(q->queue_lock);
		return;
	}

	/* Get drive status before doing anything. */
	while (handle_sony_cd_attention());

	/* Make sure we have a valid TOC. */
	sony_get_toc();


	/* Make sure the timer is cancelled. */
	del_timer(&cdu31a_abort_timer);

	while (1) {
		/*
		 * The beginning here is stolen from the hard disk driver.  I hope
		 * it's right.
		 */
		req = elv_next_request(q);
		if (!req)
			goto end_do_cdu31a_request;

		if (!sony_spun_up)
			scd_spinup();

		block = req->sector;
		nblock = req->nr_sectors;
		pr_debug(PFX "request at block %d, length %d blocks\n",
			block, nblock);
		if (!sony_toc_read) {
			printk(KERN_NOTICE PFX "TOC not read\n");
			end_request(req, 0);
			continue;
		}

		/* WTF??? */
		if (!(req->flags & REQ_CMD))
			continue;
		if (rq_data_dir(req) == WRITE) {
			end_request(req, 0);
			continue;
		}

		/*
		 * If the block address is invalid or the request goes beyond the end of
		 * the media, return an error.
		 */
		if (((block + nblock) / 4) >= sony_toc.lead_out_start_lba) {
			printk(KERN_NOTICE PFX "Request past end of media\n");
			end_request(req, 0);
			continue;
		}

		if (nblock > 4)
			nblock = 4;
		num_retries = 0;

	try_read_again:
		while (handle_sony_cd_attention());

		if (!sony_toc_read) {
			printk(KERN_NOTICE PFX "TOC not read\n");
			end_request(req, 0);
			continue;
		}

		/* If no data is left to be read from the drive, start the
		   next request. */
		if (sony_blocks_left == 0) {
			if (start_request(block / 4, nblock / 4)) {
				end_request(req, 0);
				continue;
			}
		}
		/* If the requested block is not the next one waiting in
		   the driver, abort the current operation and start a
		   new one. */
		else if (block != sony_next_block) {
			pr_debug(PFX "Read for block %d, expected %d\n",
				 block, sony_next_block);
			abort_read();
			if (!sony_toc_read) {
				printk(KERN_NOTICE PFX "TOC not read\n");
				end_request(req, 0);
				continue;
			}
			if (start_request(block / 4, nblock / 4)) {
				printk(KERN_NOTICE PFX "start request failed\n");
				end_request(req, 0);
				continue;
			}
		}

		read_data_block(req->buffer, block, nblock, res_reg, &res_size);

		if (res_reg[0] != 0x20) {
			if (!end_that_request_first(req, 1, nblock)) {
				spin_lock_irq(q->queue_lock);
				blkdev_dequeue_request(req);
				end_that_request_last(req, 1);
				spin_unlock_irq(q->queue_lock);
			}
			continue;
		}

		if (num_retries > MAX_CDU31A_RETRIES) {
			end_request(req, 0);
			continue;
		}

		num_retries++;
		if (res_reg[1] == SONY_NOT_SPIN_ERR) {
			do_sony_cd_cmd(SONY_SPIN_UP_CMD, NULL, 0, res_reg,
					&res_size);
		} else {
			printk(KERN_NOTICE PFX "%s error for block %d, nblock %d\n",
				 translate_error(res_reg[1]), block, nblock);
		}
		goto try_read_again;
	}
      end_do_cdu31a_request:
#if 0
	/* After finished, cancel any pending operations. */
	abort_read();
#else
	/* Start a timer to time out after a while to disable
	   the read. */
	cdu31a_abort_timer.expires = jiffies + 2 * HZ;	/* Wait 2 seconds */
	add_timer(&cdu31a_abort_timer);
#endif

	up(&sony_sem);
	spin_lock_irq(q->queue_lock);
	pr_debug(PFX "Leaving %s at %d\n", __FUNCTION__, __LINE__);
}


/*
 * Read the table of contents from the drive and set up TOC if
 * successful.
 */
static void sony_get_toc(void)
{
	unsigned char res_reg[2];
	unsigned int res_size;
	unsigned char parms[1];
	int session;
	int num_spin_ups;
	int totaltracks = 0;
	int mint = 99;
	int maxt = 0;

	pr_debug(PFX "Entering %s\n", __FUNCTION__);

	num_spin_ups = 0;
	if (!sony_toc_read) {
	      respinup_on_gettoc:
		/* Ignore the result, since it might error if spinning already. */
		do_sony_cd_cmd(SONY_SPIN_UP_CMD, NULL, 0, res_reg,
			       &res_size);

		do_sony_cd_cmd(SONY_READ_TOC_CMD, NULL, 0, res_reg,
			       &res_size);

		/* The drive sometimes returns error 0.  I don't know why, but ignore
		   it.  It seems to mean the drive has already done the operation. */
		if ((res_size < 2)
		    || ((res_reg[0] != 0) && (res_reg[1] != 0))) {
			/* If the drive is already playing, it's ok.  */
			if ((res_reg[1] == SONY_AUDIO_PLAYING_ERR)
			    || (res_reg[1] == 0)) {
				goto gettoc_drive_spinning;
			}

			/* If the drive says it is not spun up (even though we just did it!)
			   then retry the operation at least a few times. */
			if ((res_reg[1] == SONY_NOT_SPIN_ERR)
			    && (num_spin_ups < MAX_CDU31A_RETRIES)) {
				num_spin_ups++;
				goto respinup_on_gettoc;
			}

			printk("cdu31a: Error reading TOC: %x %s\n",
			       res_reg[0], translate_error(res_reg[1]));
			return;
		}

	      gettoc_drive_spinning:

		/* The idea here is we keep asking for sessions until the command
		   fails.  Then we know what the last valid session on the disk is.
		   No need to check session 0, since session 0 is the same as session
		   1; the command returns different information if you give it 0. 
		 */
#if DEBUG
		memset(&sony_toc, 0x0e, sizeof(sony_toc));
		memset(&single_toc, 0x0f, sizeof(single_toc));
#endif
		session = 1;
		while (1) {
/* This seems to slow things down enough to make it work.  This
 * appears to be a problem in do_sony_cd_cmd.  This printk seems 
 * to address the symptoms...  -Erik */
			pr_debug(PFX "Trying session %d\n", session);
			parms[0] = session;
			do_sony_cd_cmd(SONY_READ_TOC_SPEC_CMD,
				       parms, 1, res_reg, &res_size);

			pr_debug(PFX "%2.2x %2.2x\n", res_reg[0], res_reg[1]);

			if ((res_size < 2)
			    || ((res_reg[0] & 0xf0) == 0x20)) {
				/* An error reading the TOC, this must be past the last session. */
				if (session == 1)
					printk
					    ("Yikes! Couldn't read any sessions!");
				break;
			}
			pr_debug(PFX "Reading session %d\n", session);

			parms[0] = session;
			do_sony_cd_cmd(SONY_REQ_TOC_DATA_SPEC_CMD,
				       parms,
				       1,
				       (unsigned char *) &single_toc,
				       &res_size);
			if ((res_size < 2)
			    || ((single_toc.exec_status[0] & 0xf0) ==
				0x20)) {
				printk(KERN_ERR PFX "Error reading "
						"session %d: %x %s\n",
				     session, single_toc.exec_status[0],
				     translate_error(single_toc.
						     exec_status[1]));
				/* An error reading the TOC.  Return without sony_toc_read
				   set. */
				return;
			}
			pr_debug(PFX "add0 %01x, con0 %01x, poi0 %02x, "
					"1st trk %d, dsktyp %x, dum0 %x\n",
			     single_toc.address0, single_toc.control0,
			     single_toc.point0,
			     bcd_to_int(single_toc.first_track_num),
			     single_toc.disk_type, single_toc.dummy0);
			pr_debug(PFX "add1 %01x, con1 %01x, poi1 %02x, "
					"lst trk %d, dummy1 %x, dum2 %x\n",
			     single_toc.address1, single_toc.control1,
			     single_toc.point1,
			     bcd_to_int(single_toc.last_track_num),
			     single_toc.dummy1, single_toc.dummy2);
			pr_debug(PFX "add2 %01x, con2 %01x, poi2 %02x "
				"leadout start min %d, sec %d, frame %d\n",
			     single_toc.address2, single_toc.control2,
			     single_toc.point2,
			     bcd_to_int(single_toc.lead_out_start_msf[0]),
			     bcd_to_int(single_toc.lead_out_start_msf[1]),
			     bcd_to_int(single_toc.lead_out_start_msf[2]));
			if (res_size > 18 && single_toc.pointb0 > 0xaf)
				pr_debug(PFX "addb0 %01x, conb0 %01x, poib0 %02x, nextsession min %d, sec %d, frame %d\n"
				     "#mode5_ptrs %02d, max_start_outer_leadout_msf min %d, sec %d, frame %d\n",
				     single_toc.addressb0,
				     single_toc.controlb0,
				     single_toc.pointb0,
				     bcd_to_int(single_toc.
						next_poss_prog_area_msf
						[0]),
				     bcd_to_int(single_toc.
						next_poss_prog_area_msf
						[1]),
				     bcd_to_int(single_toc.
						next_poss_prog_area_msf
						[2]),
				     single_toc.num_mode_5_pointers,
				     bcd_to_int(single_toc.
						max_start_outer_leadout_msf
						[0]),
				     bcd_to_int(single_toc.
						max_start_outer_leadout_msf
						[1]),
				     bcd_to_int(single_toc.
						max_start_outer_leadout_msf
						[2]));
			if (res_size > 27 && single_toc.pointb1 > 0xaf)
				pr_debug(PFX "addb1 %01x, conb1 %01x, poib1 %02x, %x %x %x %x #skipint_ptrs %d, #skiptrkassign %d %x\n",
				     single_toc.addressb1,
				     single_toc.controlb1,
				     single_toc.pointb1,
				     single_toc.dummyb0_1[0],
				     single_toc.dummyb0_1[1],
				     single_toc.dummyb0_1[2],
				     single_toc.dummyb0_1[3],
				     single_toc.num_skip_interval_pointers,
				     single_toc.num_skip_track_assignments,
				     single_toc.dummyb0_2);
			if (res_size > 36 && single_toc.pointb2 > 0xaf)
				pr_debug(PFX "addb2 %01x, conb2 %01x, poib2 %02x, %02x %02x %02x %02x %02x %02x %02x\n",
				     single_toc.addressb2,
				     single_toc.controlb2,
				     single_toc.pointb2,
				     single_toc.tracksb2[0],
				     single_toc.tracksb2[1],
				     single_toc.tracksb2[2],
				     single_toc.tracksb2[3],
				     single_toc.tracksb2[4],
				     single_toc.tracksb2[5],
				     single_toc.tracksb2[6]);
			if (res_size > 45 && single_toc.pointb3 > 0xaf)
				pr_debug(PFX "addb3 %01x, conb3 %01x, poib3 %02x, %02x %02x %02x %02x %02x %02x %02x\n",
				     single_toc.addressb3,
				     single_toc.controlb3,
				     single_toc.pointb3,
				     single_toc.tracksb3[0],
				     single_toc.tracksb3[1],
				     single_toc.tracksb3[2],
				     single_toc.tracksb3[3],
				     single_toc.tracksb3[4],
				     single_toc.tracksb3[5],
				     single_toc.tracksb3[6]);
			if (res_size > 54 && single_toc.pointb4 > 0xaf)
				pr_debug(PFX "addb4 %01x, conb4 %01x, poib4 %02x, %02x %02x %02x %02x %02x %02x %02x\n",
				     single_toc.addressb4,
				     single_toc.controlb4,
				     single_toc.pointb4,
				     single_toc.tracksb4[0],
				     single_toc.tracksb4[1],
				     single_toc.tracksb4[2],
				     single_toc.tracksb4[3],
				     single_toc.tracksb4[4],
				     single_toc.tracksb4[5],
				     single_toc.tracksb4[6]);
			if (res_size > 63 && single_toc.pointc0 > 0xaf)
				pr_debug(PFX "addc0 %01x, conc0 %01x, poic0 %02x, %02x %02x %02x %02x %02x %02x %02x\n",
				     single_toc.addressc0,
				     single_toc.controlc0,
				     single_toc.pointc0,
				     single_toc.dummyc0[0],
				     single_toc.dummyc0[1],
				     single_toc.dummyc0[2],
				     single_toc.dummyc0[3],
				     single_toc.dummyc0[4],
				     single_toc.dummyc0[5],
				     single_toc.dummyc0[6]);
#undef DEBUG
#define DEBUG 0

			sony_toc.lead_out_start_msf[0] =
			    bcd_to_int(single_toc.lead_out_start_msf[0]);
			sony_toc.lead_out_start_msf[1] =
			    bcd_to_int(single_toc.lead_out_start_msf[1]);
			sony_toc.lead_out_start_msf[2] =
			    bcd_to_int(single_toc.lead_out_start_msf[2]);
			sony_toc.lead_out_start_lba =
			    single_toc.lead_out_start_lba =
			    msf_to_log(sony_toc.lead_out_start_msf);

			/* For points that do not exist, move the data over them
			   to the right location. */
			if (single_toc.pointb0 != 0xb0) {
				memmove(((char *) &single_toc) + 27,
					((char *) &single_toc) + 18,
					res_size - 18);
				res_size += 9;
			} else if (res_size > 18) {
				sony_toc.lead_out_start_msf[0] =
				    bcd_to_int(single_toc.
					       max_start_outer_leadout_msf
					       [0]);
				sony_toc.lead_out_start_msf[1] =
				    bcd_to_int(single_toc.
					       max_start_outer_leadout_msf
					       [1]);
				sony_toc.lead_out_start_msf[2] =
				    bcd_to_int(single_toc.
					       max_start_outer_leadout_msf
					       [2]);
				sony_toc.lead_out_start_lba =
				    msf_to_log(sony_toc.
					       lead_out_start_msf);
			}
			if (single_toc.pointb1 != 0xb1) {
				memmove(((char *) &single_toc) + 36,
					((char *) &single_toc) + 27,
					res_size - 27);
				res_size += 9;
			}
			if (single_toc.pointb2 != 0xb2) {
				memmove(((char *) &single_toc) + 45,
					((char *) &single_toc) + 36,
					res_size - 36);
				res_size += 9;
			}
			if (single_toc.pointb3 != 0xb3) {
				memmove(((char *) &single_toc) + 54,
					((char *) &single_toc) + 45,
					res_size - 45);
				res_size += 9;
			}
			if (single_toc.pointb4 != 0xb4) {
				memmove(((char *) &single_toc) + 63,
					((char *) &single_toc) + 54,
					res_size - 54);
				res_size += 9;
			}
			if (single_toc.pointc0 != 0xc0) {
				memmove(((char *) &single_toc) + 72,
					((char *) &single_toc) + 63,
					res_size - 63);
				res_size += 9;
			}
#if DEBUG
			printk(PRINT_INFO PFX "start track lba %u,  "
					"leadout start lba %u\n",
			     single_toc.start_track_lba,
			     single_toc.lead_out_start_lba);
			{
				int i;
				for (i = 0;
				     i <
				     1 +
				     bcd_to_int(single_toc.last_track_num)
				     -
				     bcd_to_int(single_toc.
						first_track_num); i++) {
					printk(KERN_INFO PFX "trk %02d: add 0x%01x, con 0x%01x,  track %02d, start min %02d, sec %02d, frame %02d\n",
					     i,
					     single_toc.tracks[i].address,
					     single_toc.tracks[i].control,
					     bcd_to_int(single_toc.
							tracks[i].track),
					     bcd_to_int(single_toc.
							tracks[i].
							track_start_msf
							[0]),
					     bcd_to_int(single_toc.
							tracks[i].
							track_start_msf
							[1]),
					     bcd_to_int(single_toc.
							tracks[i].
							track_start_msf
							[2]));
					if (mint >
					    bcd_to_int(single_toc.
						       tracks[i].track))
						mint =
						    bcd_to_int(single_toc.
							       tracks[i].
							       track);
					if (maxt <
					    bcd_to_int(single_toc.
						       tracks[i].track))
						maxt =
						    bcd_to_int(single_toc.
							       tracks[i].
							       track);
				}
				printk(KERN_INFO PFX "min track number %d,  "
						"max track number %d\n",
				     mint, maxt);
			}
#endif

			/* prepare a special table of contents for a CD-I disc. They don't have one. */
			if (single_toc.disk_type == 0x10 &&
			    single_toc.first_track_num == 2 &&
			    single_toc.last_track_num == 2 /* CD-I */ ) {
				sony_toc.tracks[totaltracks].address = 1;
				sony_toc.tracks[totaltracks].control = 4;	/* force data tracks */
				sony_toc.tracks[totaltracks].track = 1;
				sony_toc.tracks[totaltracks].
				    track_start_msf[0] = 0;
				sony_toc.tracks[totaltracks].
				    track_start_msf[1] = 2;
				sony_toc.tracks[totaltracks].
				    track_start_msf[2] = 0;
				mint = maxt = 1;
				totaltracks++;
			} else
				/* gather track entries from this session */
			{
				int i;
				for (i = 0;
				     i <
				     1 +
				     bcd_to_int(single_toc.last_track_num)
				     -
				     bcd_to_int(single_toc.
						first_track_num);
				     i++, totaltracks++) {
					sony_toc.tracks[totaltracks].
					    address =
					    single_toc.tracks[i].address;
					sony_toc.tracks[totaltracks].
					    control =
					    single_toc.tracks[i].control;
					sony_toc.tracks[totaltracks].
					    track =
					    bcd_to_int(single_toc.
						       tracks[i].track);
					sony_toc.tracks[totaltracks].
					    track_start_msf[0] =
					    bcd_to_int(single_toc.
						       tracks[i].
						       track_start_msf[0]);
					sony_toc.tracks[totaltracks].
					    track_start_msf[1] =
					    bcd_to_int(single_toc.
						       tracks[i].
						       track_start_msf[1]);
					sony_toc.tracks[totaltracks].
					    track_start_msf[2] =
					    bcd_to_int(single_toc.
						       tracks[i].
						       track_start_msf[2]);
					if (i == 0)
						single_toc.
						    start_track_lba =
						    msf_to_log(sony_toc.
							       tracks
							       [totaltracks].
							       track_start_msf);
					if (mint >
					    sony_toc.tracks[totaltracks].
					    track)
						mint =
						    sony_toc.
						    tracks[totaltracks].
						    track;
					if (maxt <
					    sony_toc.tracks[totaltracks].
					    track)
						maxt =
						    sony_toc.
						    tracks[totaltracks].
						    track;
				}
			}
			sony_toc.first_track_num = mint;
			sony_toc.last_track_num = maxt;
			/* Disk type of last session wins. For example:
			   CD-Extra has disk type 0 for the first session, so
			   a dumb HiFi CD player thinks it is a plain audio CD.
			   We are interested in the disk type of the last session,
			   which is 0x20 (XA) for CD-Extra, so we can access the
			   data track ... */
			sony_toc.disk_type = single_toc.disk_type;
			sony_toc.sessions = session;

			/* don't believe everything :-) */
			if (session == 1)
				single_toc.start_track_lba = 0;
			sony_toc.start_track_lba =
			    single_toc.start_track_lba;

			if (session > 1 && single_toc.pointb0 == 0xb0 &&
			    sony_toc.lead_out_start_lba ==
			    single_toc.lead_out_start_lba) {
				break;
			}

			/* Let's not get carried away... */
			if (session > 40) {
				printk(KERN_NOTICE PFX "too many sessions: "
						"%d\n", session);
				break;
			}
			session++;
		}
		sony_toc.track_entries = totaltracks;
		/* add one entry for the LAST track with track number CDROM_LEADOUT */
		sony_toc.tracks[totaltracks].address = single_toc.address2;
		sony_toc.tracks[totaltracks].control = single_toc.control2;
		sony_toc.tracks[totaltracks].track = CDROM_LEADOUT;
		sony_toc.tracks[totaltracks].track_start_msf[0] =
		    sony_toc.lead_out_start_msf[0];
		sony_toc.tracks[totaltracks].track_start_msf[1] =
		    sony_toc.lead_out_start_msf[1];
		sony_toc.tracks[totaltracks].track_start_msf[2] =
		    sony_toc.lead_out_start_msf[2];

		sony_toc_read = 1;

		pr_debug(PFX "Disk session %d, start track: %d, "
				"stop track: %d\n",
		     session, single_toc.start_track_lba,
		     single_toc.lead_out_start_lba);
	}
	pr_debug(PFX "Leaving %s\n", __FUNCTION__);
}


/*
 * Uniform cdrom interface function
 * return multisession offset and sector information
 */
static int scd_get_last_session(struct cdrom_device_info *cdi,
				struct cdrom_multisession *ms_info)
{
	if (ms_info == NULL)
		return 1;

	if (!sony_toc_read) {
		if (down_interruptible(&sony_sem))
			return -ERESTARTSYS;
		sony_get_toc();
		up(&sony_sem);
	}

	ms_info->addr_format = CDROM_LBA;
	ms_info->addr.lba = sony_toc.start_track_lba;
	ms_info->xa_flag = sony_toc.disk_type == SONY_XA_DISK_TYPE ||
	    sony_toc.disk_type == 0x10 /* CDI */ ;

	return 0;
}

/*
 * Search for a specific track in the table of contents.
 */
static int find_track(int track)
{
	int i;

	for (i = 0; i <= sony_toc.track_entries; i++) {
		if (sony_toc.tracks[i].track == track) {
			return i;
		}
	}

	return -1;
}


/*
 * Read the subcode and put it in last_sony_subcode for future use.
 */
static int read_subcode(void)
{
	unsigned int res_size;


	do_sony_cd_cmd(SONY_REQ_SUBCODE_ADDRESS_CMD,
		       NULL,
		       0, (unsigned char *) &last_sony_subcode, &res_size);
	if ((res_size < 2)
	    || ((last_sony_subcode.exec_status[0] & 0xf0) == 0x20)) {
		printk(KERN_ERR PFX "Sony CDROM error %s (read_subcode)\n",
		       translate_error(last_sony_subcode.exec_status[1]));
		return -EIO;
	}

	last_sony_subcode.track_num =
	    bcd_to_int(last_sony_subcode.track_num);
	last_sony_subcode.index_num =
	    bcd_to_int(last_sony_subcode.index_num);
	last_sony_subcode.abs_msf[0] =
	    bcd_to_int(last_sony_subcode.abs_msf[0]);
	last_sony_subcode.abs_msf[1] =
	    bcd_to_int(last_sony_subcode.abs_msf[1]);
	last_sony_subcode.abs_msf[2] =
	    bcd_to_int(last_sony_subcode.abs_msf[2]);

	last_sony_subcode.rel_msf[0] =
	    bcd_to_int(last_sony_subcode.rel_msf[0]);
	last_sony_subcode.rel_msf[1] =
	    bcd_to_int(last_sony_subcode.rel_msf[1]);
	last_sony_subcode.rel_msf[2] =
	    bcd_to_int(last_sony_subcode.rel_msf[2]);
	return 0;
}

/*
 * Uniform cdrom interface function
 * return the media catalog number found on some older audio cds
 */
static int
scd_get_mcn(struct cdrom_device_info *cdi, struct cdrom_mcn *mcn)
{
	unsigned char resbuffer[2 + 14];
	unsigned char *mcnp = mcn->medium_catalog_number;
	unsigned char *resp = resbuffer + 3;
	unsigned int res_size;

	memset(mcn->medium_catalog_number, 0, 14);
	if (down_interruptible(&sony_sem))
		return -ERESTARTSYS;
	do_sony_cd_cmd(SONY_REQ_UPC_EAN_CMD,
		       NULL, 0, resbuffer, &res_size);
	up(&sony_sem);
	if ((res_size < 2) || ((resbuffer[0] & 0xf0) == 0x20));
	else {
		/* packed bcd to single ASCII digits */
		*mcnp++ = (*resp >> 4) + '0';
		*mcnp++ = (*resp++ & 0x0f) + '0';
		*mcnp++ = (*resp >> 4) + '0';
		*mcnp++ = (*resp++ & 0x0f) + '0';
		*mcnp++ = (*resp >> 4) + '0';
		*mcnp++ = (*resp++ & 0x0f) + '0';
		*mcnp++ = (*resp >> 4) + '0';
		*mcnp++ = (*resp++ & 0x0f) + '0';
		*mcnp++ = (*resp >> 4) + '0';
		*mcnp++ = (*resp++ & 0x0f) + '0';
		*mcnp++ = (*resp >> 4) + '0';
		*mcnp++ = (*resp++ & 0x0f) + '0';
		*mcnp++ = (*resp >> 4) + '0';
	}
	*mcnp = '\0';
	return 0;
}


/*
 * Get the subchannel info like the CDROMSUBCHNL command wants to see it.  If
 * the drive is playing, the subchannel needs to be read (since it would be
 * changing).  If the drive is paused or completed, the subcode information has
 * already been stored, just use that.  The ioctl call wants things in decimal
 * (not BCD), so all the conversions are done.
 */
static int sony_get_subchnl_info(struct cdrom_subchnl *schi)
{
	/* Get attention stuff */
	while (handle_sony_cd_attention());

	sony_get_toc();
	if (!sony_toc_read) {
		return -EIO;
	}

	switch (sony_audio_status) {
	case CDROM_AUDIO_NO_STATUS:
	case CDROM_AUDIO_PLAY:
		if (read_subcode() < 0) {
			return -EIO;
		}
		break;

	case CDROM_AUDIO_PAUSED:
	case CDROM_AUDIO_COMPLETED:
		break;

#if 0
	case CDROM_AUDIO_NO_STATUS:
		schi->cdsc_audiostatus = sony_audio_status;
		return 0;
		break;
#endif
	case CDROM_AUDIO_INVALID:
	case CDROM_AUDIO_ERROR:
	default:
		return -EIO;
	}

	schi->cdsc_audiostatus = sony_audio_status;
	schi->cdsc_adr = last_sony_subcode.address;
	schi->cdsc_ctrl = last_sony_subcode.control;
	schi->cdsc_trk = last_sony_subcode.track_num;
	schi->cdsc_ind = last_sony_subcode.index_num;
	if (schi->cdsc_format == CDROM_MSF) {
		schi->cdsc_absaddr.msf.minute =
		    last_sony_subcode.abs_msf[0];
		schi->cdsc_absaddr.msf.second =
		    last_sony_subcode.abs_msf[1];
		schi->cdsc_absaddr.msf.frame =
		    last_sony_subcode.abs_msf[2];

		schi->cdsc_reladdr.msf.minute =
		    last_sony_subcode.rel_msf[0];
		schi->cdsc_reladdr.msf.second =
		    last_sony_subcode.rel_msf[1];
		schi->cdsc_reladdr.msf.frame =
		    last_sony_subcode.rel_msf[2];
	} else if (schi->cdsc_format == CDROM_LBA) {
		schi->cdsc_absaddr.lba =
		    msf_to_log(last_sony_subcode.abs_msf);
		schi->cdsc_reladdr.lba =
		    msf_to_log(last_sony_subcode.rel_msf);
	}

	return 0;
}

/* Get audio data from the drive.  This is fairly complex because I
   am looking for status and data at the same time, but if I get status
   then I just look for data.  I need to get the status immediately so
   the switch from audio to data tracks will happen quickly. */
static void
read_audio_data(char *buffer, unsigned char res_reg[], int *res_size)
{
	unsigned long retry_count;
	int result_read;


	res_reg[0] = 0;
	res_reg[1] = 0;
	*res_size = 0;
	result_read = 0;

	/* Wait for the drive to tell us we have something */
	retry_count = jiffies + SONY_JIFFIES_TIMEOUT;
      continue_read_audio_wait:
	while (time_before(jiffies, retry_count) && !(is_data_ready())
	       && !(is_result_ready() || result_read)) {
		while (handle_sony_cd_attention());

		sony_sleep();
	}
	if (!(is_data_ready())) {
		if (is_result_ready() && !result_read) {
			get_result(res_reg, res_size);

			/* Read block status and continue waiting for data. */
			if ((res_reg[0] & 0xf0) == 0x50) {
				result_read = 1;
				goto continue_read_audio_wait;
			}
			/* Invalid data from the drive.  Shut down the operation. */
			else if ((res_reg[0] & 0xf0) != 0x20) {
				printk(KERN_WARNING PFX "Got result that "
						"should have been error: %d\n",
				     res_reg[0]);
				res_reg[0] = 0x20;
				res_reg[1] = SONY_BAD_DATA_ERR;
				*res_size = 2;
			}
			abort_read();
		} else {
			pr_debug(PFX "timeout out %d\n", __LINE__);
			res_reg[0] = 0x20;
			res_reg[1] = SONY_TIMEOUT_OP_ERR;
			*res_size = 2;
			abort_read();
		}
	} else {
		clear_data_ready();

		/* If data block, then get 2340 bytes offset by 12. */
		if (sony_raw_data_mode) {
			insb(sony_cd_read_reg, buffer + CD_XA_HEAD,
			     CD_FRAMESIZE_RAW1);
		} else {
			/* Audio gets the whole 2352 bytes. */
			insb(sony_cd_read_reg, buffer, CD_FRAMESIZE_RAW);
		}

		/* If I haven't already gotten the result, get it now. */
		if (!result_read) {
			/* Wait for the drive to tell us we have something */
			retry_count = jiffies + SONY_JIFFIES_TIMEOUT;
			while (time_before(jiffies, retry_count)
			       && !(is_result_ready())) {
				while (handle_sony_cd_attention());

				sony_sleep();
			}

			if (!is_result_ready()) {
				pr_debug(PFX "timeout out %d\n", __LINE__);
				res_reg[0] = 0x20;
				res_reg[1] = SONY_TIMEOUT_OP_ERR;
				*res_size = 2;
				abort_read();
				return;
			} else {
				get_result(res_reg, res_size);
			}
		}

		if ((res_reg[0] & 0xf0) == 0x50) {
			if ((res_reg[0] == SONY_NO_CIRC_ERR_BLK_STAT)
			    || (res_reg[0] == SONY_NO_LECC_ERR_BLK_STAT)
			    || (res_reg[0] == SONY_RECOV_LECC_ERR_BLK_STAT)
			    || (res_reg[0] == SONY_NO_ERR_DETECTION_STAT)) {
				/* Ok, nothing to do. */
			} else {
				printk(KERN_ERR PFX "Data block error: 0x%x\n",
				       res_reg[0]);
				res_reg[0] = 0x20;
				res_reg[1] = SONY_BAD_DATA_ERR;
				*res_size = 2;
			}
		} else if ((res_reg[0] & 0xf0) != 0x20) {
			/* The drive gave me bad status, I don't know what to do.
			   Reset the driver and return an error. */
			printk(KERN_NOTICE PFX "Invalid block status: 0x%x\n",
			       res_reg[0]);
			restart_on_error();
			res_reg[0] = 0x20;
			res_reg[1] = SONY_BAD_DATA_ERR;
			*res_size = 2;
		}
	}
}

/* Perform a raw data read.  This will automatically detect the
   track type and read the proper data (audio or data). */
static int read_audio(struct cdrom_read_audio *ra)
{
	int retval;
	unsigned char params[2];
	unsigned char res_reg[12];
	unsigned int res_size;
	unsigned int cframe;

	if (down_interruptible(&sony_sem))
		return -ERESTARTSYS;
	if (!sony_spun_up)
		scd_spinup();

	/* Set the drive to do raw operations. */
	params[0] = SONY_SD_DECODE_PARAM;
	params[1] = 0x06 | sony_raw_data_mode;
	do_sony_cd_cmd(SONY_SET_DRIVE_PARAM_CMD,
		       params, 2, res_reg, &res_size);
	if ((res_size < 2) || ((res_reg[0] & 0xf0) == 0x20)) {
		printk(KERN_ERR PFX "Unable to set decode params: 0x%2.2x\n",
		       res_reg[1]);
		retval = -EIO;
		goto out_up;
	}

	/* From here down, we have to goto exit_read_audio instead of returning
	   because the drive parameters have to be set back to data before
	   return. */

	retval = 0;
	if (start_request(ra->addr.lba, ra->nframes)) {
		retval = -EIO;
		goto exit_read_audio;
	}

	/* For every requested frame. */
	cframe = 0;
	while (cframe < ra->nframes) {
		read_audio_data(audio_buffer, res_reg, &res_size);
		if ((res_reg[0] & 0xf0) == 0x20) {
			if (res_reg[1] == SONY_BAD_DATA_ERR) {
				printk(KERN_ERR PFX "Data error on audio "
						"sector %d\n",
				     ra->addr.lba + cframe);
			} else if (res_reg[1] == SONY_ILL_TRACK_R_ERR) {
				/* Illegal track type, change track types and start over. */
				sony_raw_data_mode =
				    (sony_raw_data_mode) ? 0 : 1;

				/* Set the drive mode. */
				params[0] = SONY_SD_DECODE_PARAM;
				params[1] = 0x06 | sony_raw_data_mode;
				do_sony_cd_cmd(SONY_SET_DRIVE_PARAM_CMD,
					       params,
					       2, res_reg, &res_size);
				if ((res_size < 2)
				    || ((res_reg[0] & 0xf0) == 0x20)) {
					printk(KERN_ERR PFX "Unable to set "
						"decode params: 0x%2.2x\n",
					     res_reg[1]);
					retval = -EIO;
					goto exit_read_audio;
				}

				/* Restart the request on the current frame. */
				if (start_request
				    (ra->addr.lba + cframe,
				     ra->nframes - cframe)) {
					retval = -EIO;
					goto exit_read_audio;
				}

				/* Don't go back to the top because don't want to get into
				   and infinite loop.  A lot of code gets duplicated, but
				   that's no big deal, I don't guess. */
				read_audio_data(audio_buffer, res_reg,
						&res_size);
				if ((res_reg[0] & 0xf0) == 0x20) {
					if (res_reg[1] ==
					    SONY_BAD_DATA_ERR) {
						printk(KERN_ERR PFX "Data error"
							" on audio sector %d\n",
						     ra->addr.lba +
						     cframe);
					} else {
						printk(KERN_ERR PFX "Error reading audio data on sector %d: %s\n",
						     ra->addr.lba + cframe,
						     translate_error
						     (res_reg[1]));
						retval = -EIO;
						goto exit_read_audio;
					}
				} else if (copy_to_user(ra->buf +
							       (CD_FRAMESIZE_RAW
								* cframe),
						        audio_buffer,
							CD_FRAMESIZE_RAW)) {
					retval = -EFAULT;
					goto exit_read_audio;
				}
			} else {
				printk(KERN_ERR PFX "Error reading audio "
						"data on sector %d: %s\n",
				     ra->addr.lba + cframe,
				     translate_error(res_reg[1]));
				retval = -EIO;
				goto exit_read_audio;
			}
		} else if (copy_to_user(ra->buf + (CD_FRAMESIZE_RAW * cframe),
					(char *)audio_buffer,
					CD_FRAMESIZE_RAW)) {
			retval = -EFAULT;
			goto exit_read_audio;
		}

		cframe++;
	}

	get_result(res_reg, &res_size);
	if ((res_reg[0] & 0xf0) == 0x20) {
		printk(KERN_ERR PFX "Error return from audio read: %s\n",
		       translate_error(res_reg[1]));
		retval = -EIO;
		goto exit_read_audio;
	}

      exit_read_audio:

	/* Set the drive mode back to the proper one for the disk. */
	params[0] = SONY_SD_DECODE_PARAM;
	if (!sony_xa_mode) {
		params[1] = 0x0f;
	} else {
		params[1] = 0x07;
	}
	do_sony_cd_cmd(SONY_SET_DRIVE_PARAM_CMD,
		       params, 2, res_reg, &res_size);
	if ((res_size < 2) || ((res_reg[0] & 0xf0) == 0x20)) {
		printk(KERN_ERR PFX "Unable to reset decode params: 0x%2.2x\n",
		       res_reg[1]);
		retval = -EIO;
	}

 out_up:
	up(&sony_sem);

	return retval;
}

static int
do_sony_cd_cmd_chk(const char *name,
		   unsigned char cmd,
		   unsigned char *params,
		   unsigned int num_params,
		   unsigned char *result_buffer, unsigned int *result_size)
{
	do_sony_cd_cmd(cmd, params, num_params, result_buffer,
		       result_size);
	if ((*result_size < 2) || ((result_buffer[0] & 0xf0) == 0x20)) {
		printk(KERN_ERR PFX "Error %s (CDROM%s)\n",
		       translate_error(result_buffer[1]), name);
		return -EIO;
	}
	return 0;
}

/*
 * Uniform cdrom interface function
 * open the tray
 */
static int scd_tray_move(struct cdrom_device_info *cdi, int position)
{
	int retval;

	if (down_interruptible(&sony_sem))
		return -ERESTARTSYS;
	if (position == 1 /* open tray */ ) {
		unsigned char res_reg[12];
		unsigned int res_size;

		do_sony_cd_cmd(SONY_AUDIO_STOP_CMD, NULL, 0, res_reg,
			       &res_size);
		do_sony_cd_cmd(SONY_SPIN_DOWN_CMD, NULL, 0, res_reg,
			       &res_size);

		sony_audio_status = CDROM_AUDIO_INVALID;
		retval = do_sony_cd_cmd_chk("EJECT", SONY_EJECT_CMD, NULL, 0,
					  res_reg, &res_size);
	} else {
		if (0 == scd_spinup())
			sony_spun_up = 1;
		retval = 0;
	}
	up(&sony_sem);
	return retval;
}

/*
 * The big ugly ioctl handler.
 */
static int scd_audio_ioctl(struct cdrom_device_info *cdi,
			   unsigned int cmd, void *arg)
{
	unsigned char res_reg[12];
	unsigned int res_size;
	unsigned char params[7];
	int i, retval;

	if (down_interruptible(&sony_sem))
		return -ERESTARTSYS;
	switch (cmd) {
	case CDROMSTART:	/* Spin up the drive */
		retval = do_sony_cd_cmd_chk("START", SONY_SPIN_UP_CMD, NULL,
					  0, res_reg, &res_size);
		break;

	case CDROMSTOP:	/* Spin down the drive */
		do_sony_cd_cmd(SONY_AUDIO_STOP_CMD, NULL, 0, res_reg,
			       &res_size);

		/*
		 * Spin the drive down, ignoring the error if the disk was
		 * already not spinning.
		 */
		sony_audio_status = CDROM_AUDIO_NO_STATUS;
		retval = do_sony_cd_cmd_chk("STOP", SONY_SPIN_DOWN_CMD, NULL,
					  0, res_reg, &res_size);
		break;

	case CDROMPAUSE:	/* Pause the drive */
		if (do_sony_cd_cmd_chk
		    ("PAUSE", SONY_AUDIO_STOP_CMD, NULL, 0, res_reg,
		     &res_size)) {
			retval = -EIO;
			break;
		}
		/* Get the current position and save it for resuming */
		if (read_subcode() < 0) {
			retval = -EIO;
			break;
		}
		cur_pos_msf[0] = last_sony_subcode.abs_msf[0];
		cur_pos_msf[1] = last_sony_subcode.abs_msf[1];
		cur_pos_msf[2] = last_sony_subcode.abs_msf[2];
		sony_audio_status = CDROM_AUDIO_PAUSED;
		retval = 0;
		break;

	case CDROMRESUME:	/* Start the drive after being paused */
		if (sony_audio_status != CDROM_AUDIO_PAUSED) {
			retval = -EINVAL;
			break;
		}

		do_sony_cd_cmd(SONY_SPIN_UP_CMD, NULL, 0, res_reg,
			       &res_size);

		/* Start the drive at the saved position. */
		params[1] = int_to_bcd(cur_pos_msf[0]);
		params[2] = int_to_bcd(cur_pos_msf[1]);
		params[3] = int_to_bcd(cur_pos_msf[2]);
		params[4] = int_to_bcd(final_pos_msf[0]);
		params[5] = int_to_bcd(final_pos_msf[1]);
		params[6] = int_to_bcd(final_pos_msf[2]);
		params[0] = 0x03;
		if (do_sony_cd_cmd_chk
		    ("RESUME", SONY_AUDIO_PLAYBACK_CMD, params, 7, res_reg,
		     &res_size) < 0) {
			retval = -EIO;
			break;
		}
		sony_audio_status = CDROM_AUDIO_PLAY;
		retval = 0;
		break;

	case CDROMPLAYMSF:	/* Play starting at the given MSF address. */
		do_sony_cd_cmd(SONY_SPIN_UP_CMD, NULL, 0, res_reg,
			       &res_size);

		/* The parameters are given in int, must be converted */
		for (i = 1; i < 7; i++) {
			params[i] =
			    int_to_bcd(((unsigned char *) arg)[i - 1]);
		}
		params[0] = 0x03;
		if (do_sony_cd_cmd_chk
		    ("PLAYMSF", SONY_AUDIO_PLAYBACK_CMD, params, 7,
		     res_reg, &res_size) < 0) {
			retval = -EIO;
			break;
		}

		/* Save the final position for pauses and resumes */
		final_pos_msf[0] = bcd_to_int(params[4]);
		final_pos_msf[1] = bcd_to_int(params[5]);
		final_pos_msf[2] = bcd_to_int(params[6]);
		sony_audio_status = CDROM_AUDIO_PLAY;
		retval = 0;
		break;

	case CDROMREADTOCHDR:	/* Read the table of contents header */
		{
			struct cdrom_tochdr *hdr;

			sony_get_toc();
			if (!sony_toc_read) {
				retval = -EIO;
				break;
			}

			hdr = (struct cdrom_tochdr *) arg;
			hdr->cdth_trk0 = sony_toc.first_track_num;
			hdr->cdth_trk1 = sony_toc.last_track_num;
		}
		retval = 0;
		break;

	case CDROMREADTOCENTRY:	/* Read a given table of contents entry */
		{
			struct cdrom_tocentry *entry;
			int track_idx;
			unsigned char *msf_val = NULL;

			sony_get_toc();
			if (!sony_toc_read) {
				retval = -EIO;
				break;
			}

			entry = (struct cdrom_tocentry *) arg;

			track_idx = find_track(entry->cdte_track);
			if (track_idx < 0) {
				retval = -EINVAL;
				break;
			}

			entry->cdte_adr =
			    sony_toc.tracks[track_idx].address;
			entry->cdte_ctrl =
			    sony_toc.tracks[track_idx].control;
			msf_val =
			    sony_toc.tracks[track_idx].track_start_msf;

			/* Logical buffer address or MSF format requested? */
			if (entry->cdte_format == CDROM_LBA) {
				entry->cdte_addr.lba = msf_to_log(msf_val);
			} else if (entry->cdte_format == CDROM_MSF) {
				entry->cdte_addr.msf.minute = *msf_val;
				entry->cdte_addr.msf.second =
				    *(msf_val + 1);
				entry->cdte_addr.msf.frame =
				    *(msf_val + 2);
			}
		}
		retval = 0;
		break;

	case CDROMPLAYTRKIND:	/* Play a track.  This currently ignores index. */
		{
			struct cdrom_ti *ti = (struct cdrom_ti *) arg;
			int track_idx;

			sony_get_toc();
			if (!sony_toc_read) {
				retval = -EIO;
				break;
			}

			if ((ti->cdti_trk0 < sony_toc.first_track_num)
			    || (ti->cdti_trk0 > sony_toc.last_track_num)
			    || (ti->cdti_trk1 < ti->cdti_trk0)) {
				retval = -EINVAL;
				break;
			}

			track_idx = find_track(ti->cdti_trk0);
			if (track_idx < 0) {
				retval = -EINVAL;
				break;
			}
			params[1] =
			    int_to_bcd(sony_toc.tracks[track_idx].
				       track_start_msf[0]);
			params[2] =
			    int_to_bcd(sony_toc.tracks[track_idx].
				       track_start_msf[1]);
			params[3] =
			    int_to_bcd(sony_toc.tracks[track_idx].
				       track_start_msf[2]);

			/*
			 * If we want to stop after the last track, use the lead-out
			 * MSF to do that.
			 */
			if (ti->cdti_trk1 >= sony_toc.last_track_num) {
				track_idx = find_track(CDROM_LEADOUT);
			} else {
				track_idx = find_track(ti->cdti_trk1 + 1);
			}
			if (track_idx < 0) {
				retval = -EINVAL;
				break;
			}
			params[4] =
			    int_to_bcd(sony_toc.tracks[track_idx].
				       track_start_msf[0]);
			params[5] =
			    int_to_bcd(sony_toc.tracks[track_idx].
				       track_start_msf[1]);
			params[6] =
			    int_to_bcd(sony_toc.tracks[track_idx].
				       track_start_msf[2]);
			params[0] = 0x03;

			do_sony_cd_cmd(SONY_SPIN_UP_CMD, NULL, 0, res_reg,
				       &res_size);

			do_sony_cd_cmd(SONY_AUDIO_PLAYBACK_CMD, params, 7,
				       res_reg, &res_size);

			if ((res_size < 2)
			    || ((res_reg[0] & 0xf0) == 0x20)) {
				printk(KERN_ERR PFX
					"Params: %x %x %x %x %x %x %x\n",
				       params[0], params[1], params[2],
				       params[3], params[4], params[5],
				       params[6]);
				printk(KERN_ERR PFX
					"Error %s (CDROMPLAYTRKIND)\n",
				     translate_error(res_reg[1]));
				retval = -EIO;
				break;
			}

			/* Save the final position for pauses and resumes */
			final_pos_msf[0] = bcd_to_int(params[4]);
			final_pos_msf[1] = bcd_to_int(params[5]);
			final_pos_msf[2] = bcd_to_int(params[6]);
			sony_audio_status = CDROM_AUDIO_PLAY;
			retval = 0;
			break;
		}

	case CDROMVOLCTRL:	/* Volume control.  What volume does this change, anyway? */
		{
			struct cdrom_volctrl *volctrl =
			    (struct cdrom_volctrl *) arg;

			params[0] = SONY_SD_AUDIO_VOLUME;
			params[1] = volctrl->channel0;
			params[2] = volctrl->channel1;
			retval = do_sony_cd_cmd_chk("VOLCTRL",
						  SONY_SET_DRIVE_PARAM_CMD,
						  params, 3, res_reg,
						  &res_size);
			break;
		}
	case CDROMSUBCHNL:	/* Get subchannel info */
		retval = sony_get_subchnl_info((struct cdrom_subchnl *) arg);
		break;

	default:
		retval = -EINVAL;
		break;
	}
	up(&sony_sem);
	return retval;
}

static int scd_read_audio(struct cdrom_device_info *cdi,
			 unsigned int cmd, unsigned long arg)
{
	void __user *argp = (void __user *)arg;
	int retval;

	if (down_interruptible(&sony_sem))
		return -ERESTARTSYS;
	switch (cmd) {
	case CDROMREADAUDIO:	/* Read 2352 byte audio tracks and 2340 byte
				   raw data tracks. */
		{
			struct cdrom_read_audio ra;


			sony_get_toc();
			if (!sony_toc_read) {
				retval = -EIO;
				break;
			}

			if (copy_from_user(&ra, argp, sizeof(ra))) {
				retval = -EFAULT;
				break;
			}

			if (ra.nframes == 0) {
				retval = 0;
				break;
			}

			if (!access_ok(VERIFY_WRITE, ra.buf,
					CD_FRAMESIZE_RAW * ra.nframes))
				return -EFAULT;

			if (ra.addr_format == CDROM_LBA) {
				if ((ra.addr.lba >=
				     sony_toc.lead_out_start_lba)
				    || (ra.addr.lba + ra.nframes >=
					sony_toc.lead_out_start_lba)) {
					retval = -EINVAL;
					break;
				}
			} else if (ra.addr_format == CDROM_MSF) {
				if ((ra.addr.msf.minute >= 75)
				    || (ra.addr.msf.second >= 60)
				    || (ra.addr.msf.frame >= 75)) {
					retval = -EINVAL;
					break;
				}

				ra.addr.lba = ((ra.addr.msf.minute * 4500)
					       + (ra.addr.msf.second * 75)
					       + ra.addr.msf.frame);
				if ((ra.addr.lba >=
				     sony_toc.lead_out_start_lba)
				    || (ra.addr.lba + ra.nframes >=
					sony_toc.lead_out_start_lba)) {
					retval = -EINVAL;
					break;
				}

				/* I know, this can go negative on an unsigned.  However,
				   the first thing done to the data is to add this value,
				   so this should compensate and allow direct msf access. */
				ra.addr.lba -= LOG_START_OFFSET;
			} else {
				retval = -EINVAL;
				break;
			}

			retval = read_audio(&ra);
			break;
		}
		retval = 0;
		break;

	default:
		retval = -EINVAL;
	}
	up(&sony_sem);
	return retval;
}

static int scd_spinup(void)
{
	unsigned char res_reg[12];
	unsigned int res_size;
	int num_spin_ups;

	num_spin_ups = 0;

      respinup_on_open:
	do_sony_cd_cmd(SONY_SPIN_UP_CMD, NULL, 0, res_reg, &res_size);

	/* The drive sometimes returns error 0.  I don't know why, but ignore
	   it.  It seems to mean the drive has already done the operation. */
	if ((res_size < 2) || ((res_reg[0] != 0) && (res_reg[1] != 0))) {
		printk(KERN_ERR PFX "%s error (scd_open, spin up)\n",
		       translate_error(res_reg[1]));
		return 1;
	}

	do_sony_cd_cmd(SONY_READ_TOC_CMD, NULL, 0, res_reg, &res_size);

	/* The drive sometimes returns error 0.  I don't know why, but ignore
	   it.  It seems to mean the drive has already done the operation. */
	if ((res_size < 2) || ((res_reg[0] != 0) && (res_reg[1] != 0))) {
		/* If the drive is already playing, it's ok.  */
		if ((res_reg[1] == SONY_AUDIO_PLAYING_ERR)
		    || (res_reg[1] == 0)) {
			return 0;
		}

		/* If the drive says it is not spun up (even though we just did it!)
		   then retry the operation at least a few times. */
		if ((res_reg[1] == SONY_NOT_SPIN_ERR)
		    && (num_spin_ups < MAX_CDU31A_RETRIES)) {
			num_spin_ups++;
			goto respinup_on_open;
		}

		printk(KERN_ERR PFX "Error %s (scd_open, read toc)\n",
		       translate_error(res_reg[1]));
		do_sony_cd_cmd(SONY_SPIN_DOWN_CMD, NULL, 0, res_reg,
			       &res_size);
		return 1;
	}
	return 0;
}

/*
 * Open the drive for operations.  Spin the drive up and read the table of
 * contents if these have not already been done.
 */
static int scd_open(struct cdrom_device_info *cdi, int purpose)
{
	unsigned char res_reg[12];
	unsigned int res_size;
	unsigned char params[2];

	if (purpose == 1) {
		/* Open for IOCTLs only - no media check */
		sony_usage++;
		return 0;
	}

	if (sony_usage == 0) {
		if (scd_spinup() != 0)
			return -EIO;
		sony_get_toc();
		if (!sony_toc_read) {
			do_sony_cd_cmd(SONY_SPIN_DOWN_CMD, NULL, 0,
				       res_reg, &res_size);
			return -EIO;
		}

		/* For XA on the CDU31A only, we have to do special reads.
		   The CDU33A handles XA automagically. */
		/* if (   (sony_toc.disk_type == SONY_XA_DISK_TYPE) */
		if ((sony_toc.disk_type != 0x00)
		    && (!is_double_speed)) {
			params[0] = SONY_SD_DECODE_PARAM;
			params[1] = 0x07;
			do_sony_cd_cmd(SONY_SET_DRIVE_PARAM_CMD,
				       params, 2, res_reg, &res_size);
			if ((res_size < 2)
			    || ((res_reg[0] & 0xf0) == 0x20)) {
				printk(KERN_WARNING PFX "Unable to set "
					"XA params: 0x%2.2x\n", res_reg[1]);
			}
			sony_xa_mode = 1;
		}
		/* A non-XA disk.  Set the parms back if necessary. */
		else if (sony_xa_mode) {
			params[0] = SONY_SD_DECODE_PARAM;
			params[1] = 0x0f;
			do_sony_cd_cmd(SONY_SET_DRIVE_PARAM_CMD,
				       params, 2, res_reg, &res_size);
			if ((res_size < 2)
			    || ((res_reg[0] & 0xf0) == 0x20)) {
				printk(KERN_WARNING PFX "Unable to reset "
					"XA params: 0x%2.2x\n", res_reg[1]);
			}
			sony_xa_mode = 0;
		}

		sony_spun_up = 1;
	}

	sony_usage++;

	return 0;
}


/*
 * Close the drive.  Spin it down if no task is using it.  The spin
 * down will fail if playing audio, so audio play is OK.
 */
static void scd_release(struct cdrom_device_info *cdi)
{
	if (sony_usage == 1) {
		unsigned char res_reg[12];
		unsigned int res_size;

		do_sony_cd_cmd(SONY_SPIN_DOWN_CMD, NULL, 0, res_reg,
			       &res_size);

		sony_spun_up = 0;
	}
	sony_usage--;
}

static struct cdrom_device_ops scd_dops = {
	.open			= scd_open,
	.release		= scd_release,
	.drive_status		= scd_drive_status,
	.media_changed		= scd_media_changed,
	.tray_move		= scd_tray_move,
	.lock_door		= scd_lock_door,
	.select_speed		= scd_select_speed,
	.get_last_session	= scd_get_last_session,
	.get_mcn		= scd_get_mcn,
	.reset			= scd_reset,
	.audio_ioctl		= scd_audio_ioctl,
	.capability		= CDC_OPEN_TRAY | CDC_CLOSE_TRAY | CDC_LOCK |
				  CDC_SELECT_SPEED | CDC_MULTI_SESSION |
				  CDC_MCN | CDC_MEDIA_CHANGED | CDC_PLAY_AUDIO |
				  CDC_RESET | CDC_DRIVE_STATUS,
	.n_minors		= 1,
};

static struct cdrom_device_info scd_info = {
	.ops		= &scd_dops,
	.speed		= 2,
	.capacity	= 1,
	.name		= "cdu31a"
};

static int scd_block_open(struct inode *inode, struct file *file)
{
	return cdrom_open(&scd_info, inode, file);
}

static int scd_block_release(struct inode *inode, struct file *file)
{
	return cdrom_release(&scd_info, file);
}

static int scd_block_ioctl(struct inode *inode, struct file *file,
				unsigned cmd, unsigned long arg)
{
	int retval;

	/* The eject and close commands should be handled by Uniform CD-ROM
	 * driver - but I always got hard lockup instead of eject
	 * until I put this here.
	 */
	switch (cmd) {
		case CDROMEJECT:
			scd_lock_door(&scd_info, 0);
			retval = scd_tray_move(&scd_info, 1);
			break;
		case CDROMCLOSETRAY:
			retval = scd_tray_move(&scd_info, 0);
			break;
		case CDROMREADAUDIO:
			retval = scd_read_audio(&scd_info, CDROMREADAUDIO, arg);
			break;
		default:
			retval = cdrom_ioctl(file, &scd_info, inode, cmd, arg);
	}
	return retval;
}

static int scd_block_media_changed(struct gendisk *disk)
{
	return cdrom_media_changed(&scd_info);
}

static struct block_device_operations scd_bdops =
{
	.owner		= THIS_MODULE,
	.open		= scd_block_open,
	.release	= scd_block_release,
	.ioctl		= scd_block_ioctl,
	.media_changed	= scd_block_media_changed,
};

static struct gendisk *scd_gendisk;

/* The different types of disc loading mechanisms supported */
static char *load_mech[] __initdata =
    { "caddy", "tray", "pop-up", "unknown" };

static int __init
get_drive_configuration(unsigned short base_io,
			unsigned char res_reg[], unsigned int *res_size)
{
	unsigned long retry_count;


	if (!request_region(base_io, 4, "cdu31a"))
		return 0;

	/* Set the base address */
	cdu31a_port = base_io;

	/* Set up all the register locations */
	sony_cd_cmd_reg = cdu31a_port + SONY_CMD_REG_OFFSET;
	sony_cd_param_reg = cdu31a_port + SONY_PARAM_REG_OFFSET;
	sony_cd_write_reg = cdu31a_port + SONY_WRITE_REG_OFFSET;
	sony_cd_control_reg = cdu31a_port + SONY_CONTROL_REG_OFFSET;
	sony_cd_status_reg = cdu31a_port + SONY_STATUS_REG_OFFSET;
	sony_cd_result_reg = cdu31a_port + SONY_RESULT_REG_OFFSET;
	sony_cd_read_reg = cdu31a_port + SONY_READ_REG_OFFSET;
	sony_cd_fifost_reg = cdu31a_port + SONY_FIFOST_REG_OFFSET;

	/*
	 * Check to see if anything exists at the status register location.
	 * I don't know if this is a good way to check, but it seems to work
	 * ok for me.
	 */
	if (read_status_register() != 0xff) {
		/*
		 * Reset the drive and wait for attention from it (to say it's reset).
		 * If you don't wait, the next operation will probably fail.
		 */
		reset_drive();
		retry_count = jiffies + SONY_RESET_TIMEOUT;
		while (time_before(jiffies, retry_count)
		       && (!is_attention())) {
			sony_sleep();
		}

#if 0
		/* If attention is never seen probably not a CDU31a present */
		if (!is_attention()) {
			res_reg[0] = 0x20;
			goto out_err;
		}
#endif

		/*
		 * Get the drive configuration.
		 */
		do_sony_cd_cmd(SONY_REQ_DRIVE_CONFIG_CMD,
			       NULL,
			       0, (unsigned char *) res_reg, res_size);
		if (*res_size <= 2 || (res_reg[0] & 0xf0) != 0)
			goto out_err;
		return 1;
	}

	/* Return an error */
	res_reg[0] = 0x20;
out_err:
	release_region(cdu31a_port, 4);
	cdu31a_port = 0;
	return 0;
}

#ifndef MODULE
/*
 * Set up base I/O and interrupts, called from main.c.
 */

static int __init cdu31a_setup(char *strings)
{
	int ints[4];

	(void) get_options(strings, ARRAY_SIZE(ints), ints);

	if (ints[0] > 0) {
		cdu31a_port = ints[1];
	}
	if (ints[0] > 1) {
		cdu31a_irq = ints[2];
	}
	if ((strings != NULL) && (*strings != '\0')) {
		if (strcmp(strings, "PAS") == 0) {
			sony_pas_init = 1;
		} else {
			printk(KERN_NOTICE PFX "Unknown interface type: %s\n",
			       strings);
		}
	}

	return 1;
}

__setup("cdu31a=", cdu31a_setup);

#endif

/*
 * Initialize the driver.
 */
int __init cdu31a_init(void)
{
	struct s_sony_drive_config drive_config;
	struct gendisk *disk;
	int deficiency = 0;
	unsigned int res_size;
	char msg[255];
	char buf[40];
	int i;
	int tmp_irq;

	/*
	 * According to Alex Freed (freed@europa.orion.adobe.com), this is
	 * required for the Fusion CD-16 package.  If the sound driver is
	 * loaded, it should work fine, but just in case...
	 *
	 * The following turn on the CD-ROM interface for a Fusion CD-16.
	 */
	if (sony_pas_init) {
		outb(0xbc, 0x9a01);
		outb(0xe2, 0x9a01);
	}

	/* Setting the base I/O address to 0xffff will disable it. */
	if (cdu31a_port == 0xffff)
		goto errout3;

	if (cdu31a_port != 0) {
		/* Need IRQ 0 because we can't sleep here. */
		tmp_irq = cdu31a_irq;
		cdu31a_irq = 0;
		if (!get_drive_configuration(cdu31a_port,
					    drive_config.exec_status,
					    &res_size))
			goto errout3;
		cdu31a_irq = tmp_irq;
	} else {
		cdu31a_irq = 0;
		for (i = 0; cdu31a_addresses[i].base; i++) {
			if (get_drive_configuration(cdu31a_addresses[i].base,
						     drive_config.exec_status,
						     &res_size)) {
				cdu31a_irq = cdu31a_addresses[i].int_num;
				break;
			}
		}
		if (!cdu31a_port)
			goto errout3;
	}

	if (register_blkdev(MAJOR_NR, "cdu31a"))
		goto errout2;

	disk = alloc_disk(1);
	if (!disk)
		goto errout1;
	disk->major = MAJOR_NR;
	disk->first_minor = 0;
	sprintf(disk->disk_name, "cdu31a");
	disk->fops = &scd_bdops;
	disk->flags = GENHD_FL_CD;

	if (SONY_HWC_DOUBLE_SPEED(drive_config))
		is_double_speed = 1;

	tmp_irq = cdu31a_irq;	/* Need IRQ 0 because we can't sleep here. */
	cdu31a_irq = 0;

	sony_speed = is_double_speed; /* Set 2X drives to 2X by default */
	set_drive_params(sony_speed);

	cdu31a_irq = tmp_irq;

	if (cdu31a_irq > 0) {
		if (request_irq
		    (cdu31a_irq, cdu31a_interrupt, SA_INTERRUPT,
		     "cdu31a", NULL)) {
			printk(KERN_WARNING PFX "Unable to grab IRQ%d for "
					"the CDU31A driver\n", cdu31a_irq);
			cdu31a_irq = 0;
		}
	}

	sprintf(msg, "Sony I/F CDROM : %8.8s %16.16s %8.8s\n",
		drive_config.vendor_id,
		drive_config.product_id,
		drive_config.product_rev_level);
	sprintf(buf, "  Capabilities: %s",
		load_mech[SONY_HWC_GET_LOAD_MECH(drive_config)]);
	strcat(msg, buf);
	if (SONY_HWC_AUDIO_PLAYBACK(drive_config))
		strcat(msg, ", audio");
	else
		deficiency |= CDC_PLAY_AUDIO;
	if (SONY_HWC_EJECT(drive_config))
		strcat(msg, ", eject");
	else
		deficiency |= CDC_OPEN_TRAY;
	if (SONY_HWC_LED_SUPPORT(drive_config))
		strcat(msg, ", LED");
	if (SONY_HWC_ELECTRIC_VOLUME(drive_config))
		strcat(msg, ", elec. Vol");
	if (SONY_HWC_ELECTRIC_VOLUME_CTL(drive_config))
		strcat(msg, ", sep. Vol");
	if (is_double_speed)
		strcat(msg, ", double speed");
	else
		deficiency |= CDC_SELECT_SPEED;
	if (cdu31a_irq > 0) {
		sprintf(buf, ", irq %d", cdu31a_irq);
		strcat(msg, buf);
	}
	strcat(msg, "\n");
	printk(KERN_INFO PFX "%s",msg);

	cdu31a_queue = blk_init_queue(do_cdu31a_request, &cdu31a_lock);
	if (!cdu31a_queue)
		goto errout0;
	blk_queue_hardsect_size(cdu31a_queue, 2048);

	init_timer(&cdu31a_abort_timer);
	cdu31a_abort_timer.function = handle_abort_timeout;

	scd_info.mask = deficiency;
	scd_gendisk = disk;
	if (register_cdrom(&scd_info))
		goto err;
	disk->queue = cdu31a_queue;
	add_disk(disk);

	disk_changed = 1;
	return 0;

err:
	blk_cleanup_queue(cdu31a_queue);
errout0:
	if (cdu31a_irq)
		free_irq(cdu31a_irq, NULL);
	printk(KERN_ERR PFX "Unable to register with Uniform cdrom driver\n");
	put_disk(disk);
errout1:
	if (unregister_blkdev(MAJOR_NR, "cdu31a")) {
		printk(KERN_WARNING PFX "Can't unregister block device\n");
	}
errout2:
	release_region(cdu31a_port, 4);
errout3:
	return -EIO;
}


static void __exit cdu31a_exit(void)
{
	del_gendisk(scd_gendisk);
	put_disk(scd_gendisk);
	if (unregister_cdrom(&scd_info)) {
		printk(KERN_WARNING PFX "Can't unregister from Uniform "
				"cdrom driver\n");
		return;
	}
	if ((unregister_blkdev(MAJOR_NR, "cdu31a") == -EINVAL)) {
		printk(KERN_WARNING PFX "Can't unregister\n");
		return;
	}

	blk_cleanup_queue(cdu31a_queue);

	if (cdu31a_irq > 0)
		free_irq(cdu31a_irq, NULL);

	release_region(cdu31a_port, 4);
	printk(KERN_INFO PFX "module released.\n");
}

#ifdef MODULE
module_init(cdu31a_init);
#endif
module_exit(cdu31a_exit);

MODULE_LICENSE("GPL");
MODULE_ALIAS_BLOCKDEV_MAJOR(CDU31A_CDROM_MAJOR);
