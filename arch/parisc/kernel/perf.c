/*
 *  Parisc performance counters
 *  Copyright (C) 2001 Randolph Chung <tausq@debian.org>
 *
 *  This code is derived, with permission, from HP/UX sources.
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2, or (at your option)
 *    any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 *  Edited comment from original sources:
 *
 *  This driver programs the PCX-U/PCX-W performance counters
 *  on the PA-RISC 2.0 chips.  The driver keeps all images now
 *  internally to the kernel to hopefully eliminate the possiblity
 *  of a bad image halting the CPU.  Also, there are different
 *  images for the PCX-W and later chips vs the PCX-U chips.
 *
 *  Only 1 process is allowed to access the driver at any time,
 *  so the only protection that is needed is at open and close.
 *  A variable "perf_enabled" is used to hold the state of the
 *  driver.  The spinlock "perf_lock" is used to protect the
 *  modification of the state during open/close operations so
 *  multiple processes don't get into the driver simultaneously.
 *
 *  This driver accesses the processor directly vs going through
 *  the PDC INTRIGUE calls.  This is done to eliminate bugs introduced
 *  in various PDC revisions.  The code is much more maintainable
 *  and reliable this way vs having to debug on every version of PDC
 *  on every box. 
 */

#include <linux/capability.h>
#include <linux/init.h>
#include <linux/proc_fs.h>
#include <linux/miscdevice.h>
#include <linux/spinlock.h>

#include <asm/uaccess.h>
#include <asm/perf.h>
#include <asm/parisc-device.h>
#include <asm/processor.h>
#include <asm/runway.h>
#include <asm/io.h>		/* for __raw_read() */

#include "perf_images.h"

#define MAX_RDR_WORDS	24
#define PERF_VERSION	2	/* derived from hpux's PI v2 interface */

/* definition of RDR regs */
struct rdr_tbl_ent {
	uint16_t	width;
	uint8_t		num_words;
	uint8_t		write_control;
};

static int perf_processor_interface __read_mostly = UNKNOWN_INTF;
static int perf_enabled __read_mostly;
static spinlock_t perf_lock;
struct parisc_device *cpu_device __read_mostly;

/* RDRs to write for PCX-W */
static const int perf_rdrs_W[] =
	{ 0, 1, 4, 5, 6, 15, 16, 17, 18, 20, 21, 22, 23, 24, 25, -1 };

/* RDRs to write for PCX-U */
static const int perf_rdrs_U[] =
	{ 0, 1, 4, 5, 6, 7, 16, 17, 18, 20, 21, 22, 23, 24, 25, -1 };

/* RDR register descriptions for PCX-W */
static const struct rdr_tbl_ent perf_rdr_tbl_W[] = {
	{ 19,	1,	8 },   /* RDR 0 */
	{ 16,	1,	16 },  /* RDR 1 */
	{ 72,	2,	0 },   /* RDR 2 */
	{ 81,	2,	0 },   /* RDR 3 */
	{ 328,	6,	0 },   /* RDR 4 */
	{ 160,	3,	0 },   /* RDR 5 */
	{ 336,	6,	0 },   /* RDR 6 */
	{ 164,	3,	0 },   /* RDR 7 */
	{ 0,	0,	0 },   /* RDR 8 */
	{ 35,	1,	0 },   /* RDR 9 */
	{ 6,	1,	0 },   /* RDR 10 */
	{ 18,	1,	0 },   /* RDR 11 */
	{ 13,	1,	0 },   /* RDR 12 */
	{ 8,	1,	0 },   /* RDR 13 */
	{ 8,	1,	0 },   /* RDR 14 */
	{ 8,	1,	0 },   /* RDR 15 */
	{ 1530,	24,	0 },   /* RDR 16 */
	{ 16,	1,	0 },   /* RDR 17 */
	{ 4,	1,	0 },   /* RDR 18 */
	{ 0,	0,	0 },   /* RDR 19 */
	{ 152,	3,	24 },  /* RDR 20 */
	{ 152,	3,	24 },  /* RDR 21 */
	{ 233,	4,	48 },  /* RDR 22 */
	{ 233,	4,	48 },  /* RDR 23 */
	{ 71,	2,	0 },   /* RDR 24 */
	{ 71,	2,	0 },   /* RDR 25 */
	{ 11,	1,	0 },   /* RDR 26 */
	{ 18,	1,	0 },   /* RDR 27 */
	{ 128,	2,	0 },   /* RDR 28 */
	{ 0,	0,	0 },   /* RDR 29 */
	{ 16,	1,	0 },   /* RDR 30 */
	{ 16,	1,	0 },   /* RDR 31 */
};

/* RDR register descriptions for PCX-U */
static const struct rdr_tbl_ent perf_rdr_tbl_U[] = {
	{ 19,	1,	8 },              /* RDR 0 */
	{ 32,	1,	16 },             /* RDR 1 */
	{ 20,	1,	0 },              /* RDR 2 */
	{ 0,	0,	0 },              /* RDR 3 */
	{ 344,	6,	0 },              /* RDR 4 */
	{ 176,	3,	0 },              /* RDR 5 */
	{ 336,	6,	0 },              /* RDR 6 */
	{ 0,	0,	0 },              /* RDR 7 */
	{ 0,	0,	0 },              /* RDR 8 */
	{ 0,	0,	0 },              /* RDR 9 */
	{ 28,	1,	0 },              /* RDR 10 */
	{ 33,	1,	0 },              /* RDR 11 */
	{ 0,	0,	0 },              /* RDR 12 */
	{ 230,	4,	0 },              /* RDR 13 */
	{ 32,	1,	0 },              /* RDR 14 */
	{ 128,	2,	0 },              /* RDR 15 */
	{ 1494,	24,	0 },              /* RDR 16 */
	{ 18,	1,	0 },              /* RDR 17 */
	{ 4,	1,	0 },              /* RDR 18 */
	{ 0,	0,	0 },              /* RDR 19 */
	{ 158,	3,	24 },             /* RDR 20 */
	{ 158,	3,	24 },             /* RDR 21 */
	{ 194,	4,	48 },             /* RDR 22 */
	{ 194,	4,	48 },             /* RDR 23 */
	{ 71,	2,	0 },              /* RDR 24 */
	{ 71,	2,	0 },              /* RDR 25 */
	{ 28,	1,	0 },              /* RDR 26 */
	{ 33,	1,	0 },              /* RDR 27 */
	{ 88,	2,	0 },              /* RDR 28 */
	{ 32,	1,	0 },              /* RDR 29 */
	{ 24,	1,	0 },              /* RDR 30 */
	{ 16,	1,	0 },              /* RDR 31 */
};

/*
 * A non-zero write_control in the above tables is a byte offset into
 * this array.
 */
static const uint64_t perf_bitmasks[] = {
	0x0000000000000000ul,     /* first dbl word must be zero */
	0xfdffe00000000000ul,     /* RDR0 bitmask */
	0x003f000000000000ul,     /* RDR1 bitmask */
	0x00fffffffffffffful,     /* RDR20-RDR21 bitmask (152 bits) */
	0xfffffffffffffffful,
	0xfffffffc00000000ul,
	0xfffffffffffffffful,     /* RDR22-RDR23 bitmask (233 bits) */
	0xfffffffffffffffful,
	0xfffffffffffffffcul,
	0xff00000000000000ul
};

/*
 * Write control bitmasks for Pa-8700 processor given
 * somethings have changed slightly.
 */
static const uint64_t perf_bitmasks_piranha[] = {
	0x0000000000000000ul,     /* first dbl word must be zero */
	0xfdffe00000000000ul,     /* RDR0 bitmask */
	0x003f000000000000ul,     /* RDR1 bitmask */
	0x00fffffffffffffful,     /* RDR20-RDR21 bitmask (158 bits) */
	0xfffffffffffffffful,
	0xfffffffc00000000ul,
	0xfffffffffffffffful,     /* RDR22-RDR23 bitmask (210 bits) */
	0xfffffffffffffffful,
	0xfffffffffffffffful,
	0xfffc000000000000ul
};

static const uint64_t *bitmask_array;   /* array of bitmasks to use */

/******************************************************************************
 * Function Prototypes
 *****************************************************************************/
static int perf_config(uint32_t *image_ptr);
static int perf_release(struct inode *inode, struct file *file);
static int perf_open(struct inode *inode, struct file *file);
static ssize_t perf_read(struct file *file, char __user *buf, size_t cnt, loff_t *ppos);
static ssize_t perf_write(struct file *file, const char __user *buf, size_t count, 
	loff_t *ppos);
static long perf_ioctl(struct file *file, unsigned int cmd, unsigned long arg);
static void perf_start_counters(void);
static int perf_stop_counters(uint32_t *raddr);
static const struct rdr_tbl_ent * perf_rdr_get_entry(uint32_t rdr_num);
static int perf_rdr_read_ubuf(uint32_t	rdr_num, uint64_t *buffer);
static int perf_rdr_clear(uint32_t rdr_num);
static int perf_write_image(uint64_t *memaddr);
static void perf_rdr_write(uint32_t rdr_num, uint64_t *buffer);

/* External Assembly Routines */
extern uint64_t perf_rdr_shift_in_W (uint32_t rdr_num, uint16_t width);
extern uint64_t perf_rdr_shift_in_U (uint32_t rdr_num, uint16_t width);
extern void perf_rdr_shift_out_W (uint32_t rdr_num, uint64_t buffer);
extern void perf_rdr_shift_out_U (uint32_t rdr_num, uint64_t buffer);
extern void perf_intrigue_enable_perf_counters (void);
extern void perf_intrigue_disable_perf_counters (void);

/******************************************************************************
 * Function Definitions
 *****************************************************************************/


/*
 * configure:
 *
 * Configure the cpu with a given data image.  First turn off the counters, 
 * then download the image, then turn the counters back on.
 */
static int perf_config(uint32_t *image_ptr)
{
	long error;
	uint32_t raddr[4];

	/* Stop the counters*/
	error = perf_stop_counters(raddr);
	if (error != 0) {
		printk("perf_config: perf_stop_counters = %ld\n", error);
		return -EINVAL; 
	}

printk("Preparing to write image\n");
	/* Write the image to the chip */
	error = perf_write_image((uint64_t *)image_ptr);
	if (error != 0) {
		printk("perf_config: DOWNLOAD = %ld\n", error);
		return -EINVAL; 
	}

printk("Preparing to start counters\n");

	/* Start the counters */
	perf_start_counters();

	return sizeof(uint32_t);
}

/*
 * Open the device and initialize all of its memory.  The device is only 
 * opened once, but can be "queried" by multiple processes that know its
 * file descriptor.
 */
static int perf_open(struct inode *inode, struct file *file)
{
	spin_lock(&perf_lock);
	if (perf_enabled) {
		spin_unlock(&perf_lock);
		return -EBUSY;
	}
	perf_enabled = 1;
 	spin_unlock(&perf_lock);

	return 0;
}

/*
 * Close the device.
 */
static int perf_release(struct inode *inode, struct file *file)
{
	spin_lock(&perf_lock);
	perf_enabled = 0;
	spin_unlock(&perf_lock);

	return 0;
}

/*
 * Read does nothing for this driver
 */
static ssize_t perf_read(struct file *file, char __user *buf, size_t cnt, loff_t *ppos)
{
	return 0;
}

/*
 * write:
 *
 * This routine downloads the image to the chip.  It must be
 * called on the processor that the download should happen
 * on.
 */
static ssize_t perf_write(struct file *file, const char __user *buf, size_t count, 
	loff_t *ppos)
{
	int err;
	size_t image_size;
	uint32_t image_type;
	uint32_t interface_type;
	uint32_t test;

	if (perf_processor_interface == ONYX_INTF) 
		image_size = PCXU_IMAGE_SIZE;
	else if (perf_processor_interface == CUDA_INTF) 
		image_size = PCXW_IMAGE_SIZE;
	else 
		return -EFAULT;

	if (!capable(CAP_SYS_ADMIN))
		return -EACCES;

	if (count != sizeof(uint32_t))
		return -EIO;

	if ((err = copy_from_user(&image_type, buf, sizeof(uint32_t))) != 0) 
		return err;

	/* Get the interface type and test type */
   	interface_type = (image_type >> 16) & 0xffff;
	test           = (image_type & 0xffff);

	/* Make sure everything makes sense */

	/* First check the machine type is correct for
	   the requested image */
        if (((perf_processor_interface == CUDA_INTF) &&
		       (interface_type != CUDA_INTF)) ||
	    ((perf_processor_interface == ONYX_INTF) &&
	               (interface_type != ONYX_INTF))) 
		return -EINVAL;

	/* Next check to make sure the requested image
	   is valid */
	if (((interface_type == CUDA_INTF) && 
		       (test >= MAX_CUDA_IMAGES)) ||
	    ((interface_type == ONYX_INTF) && 
		       (test >= MAX_ONYX_IMAGES))) 
		return -EINVAL;

	/* Copy the image into the processor */
	if (interface_type == CUDA_INTF) 
		return perf_config(cuda_images[test]);
	else
		return perf_config(onyx_images[test]);

	return count;
}

/*
 * Patch the images that need to know the IVA addresses.
 */
static void perf_patch_images(void)
{
#if 0 /* FIXME!! */
/* 
 * NOTE:  this routine is VERY specific to the current TLB image.
 * If the image is changed, this routine might also need to be changed.
 */
	extern void $i_itlb_miss_2_0();
	extern void $i_dtlb_miss_2_0();
	extern void PA2_0_iva();

	/* 
	 * We can only use the lower 32-bits, the upper 32-bits should be 0
	 * anyway given this is in the kernel 
	 */
	uint32_t itlb_addr  = (uint32_t)&($i_itlb_miss_2_0);
	uint32_t dtlb_addr  = (uint32_t)&($i_dtlb_miss_2_0);
	uint32_t IVAaddress = (uint32_t)&PA2_0_iva;

	if (perf_processor_interface == ONYX_INTF) {
		/* clear last 2 bytes */
		onyx_images[TLBMISS][15] &= 0xffffff00;  
		/* set 2 bytes */
		onyx_images[TLBMISS][15] |= (0x000000ff&((dtlb_addr) >> 24));
		onyx_images[TLBMISS][16] = (dtlb_addr << 8)&0xffffff00;
		onyx_images[TLBMISS][17] = itlb_addr;

		/* clear last 2 bytes */
		onyx_images[TLBHANDMISS][15] &= 0xffffff00;  
		/* set 2 bytes */
		onyx_images[TLBHANDMISS][15] |= (0x000000ff&((dtlb_addr) >> 24));
		onyx_images[TLBHANDMISS][16] = (dtlb_addr << 8)&0xffffff00;
		onyx_images[TLBHANDMISS][17] = itlb_addr;

		/* clear last 2 bytes */
		onyx_images[BIG_CPI][15] &= 0xffffff00;  
		/* set 2 bytes */
		onyx_images[BIG_CPI][15] |= (0x000000ff&((dtlb_addr) >> 24));
		onyx_images[BIG_CPI][16] = (dtlb_addr << 8)&0xffffff00;
		onyx_images[BIG_CPI][17] = itlb_addr;

	    onyx_images[PANIC][15] &= 0xffffff00;  /* clear last 2 bytes */
	 	onyx_images[PANIC][15] |= (0x000000ff&((IVAaddress) >> 24)); /* set 2 bytes */
		onyx_images[PANIC][16] = (IVAaddress << 8)&0xffffff00;


	} else if (perf_processor_interface == CUDA_INTF) {
		/* Cuda interface */
		cuda_images[TLBMISS][16] =  
			(cuda_images[TLBMISS][16]&0xffff0000) |
			((dtlb_addr >> 8)&0x0000ffff);
		cuda_images[TLBMISS][17] = 
			((dtlb_addr << 24)&0xff000000) | ((itlb_addr >> 16)&0x000000ff);
		cuda_images[TLBMISS][18] = (itlb_addr << 16)&0xffff0000;

		cuda_images[TLBHANDMISS][16] = 
			(cuda_images[TLBHANDMISS][16]&0xffff0000) |
			((dtlb_addr >> 8)&0x0000ffff);
		cuda_images[TLBHANDMISS][17] = 
			((dtlb_addr << 24)&0xff000000) | ((itlb_addr >> 16)&0x000000ff);
		cuda_images[TLBHANDMISS][18] = (itlb_addr << 16)&0xffff0000;

		cuda_images[BIG_CPI][16] = 
			(cuda_images[BIG_CPI][16]&0xffff0000) |
			((dtlb_addr >> 8)&0x0000ffff);
		cuda_images[BIG_CPI][17] = 
			((dtlb_addr << 24)&0xff000000) | ((itlb_addr >> 16)&0x000000ff);
		cuda_images[BIG_CPI][18] = (itlb_addr << 16)&0xffff0000;
	} else {
		/* Unknown type */
	}
#endif
}


/*
 * ioctl routine
 * All routines effect the processor that they are executed on.  Thus you 
 * must be running on the processor that you wish to change.
 */

static long perf_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long error_start;
	uint32_t raddr[4];
	int error = 0;

	switch (cmd) {

	    case PA_PERF_ON:
			/* Start the counters */
			perf_start_counters();
			break;

	    case PA_PERF_OFF:
			error_start = perf_stop_counters(raddr);
			if (error_start != 0) {
				printk(KERN_ERR "perf_off: perf_stop_counters = %ld\n", error_start);
				error = -EFAULT;
				break;
			}

			/* copy out the Counters */
			if (copy_to_user((void __user *)arg, raddr, 
					sizeof (raddr)) != 0) {
				error =  -EFAULT;
				break;
			}
			break;

	    case PA_PERF_VERSION:
  	  		/* Return the version # */
			error = put_user(PERF_VERSION, (int *)arg);
			break;

	    default:
  	 		error = -ENOTTY;
	}

	return error;
}

static struct file_operations perf_fops = {
	.llseek = no_llseek,
	.read = perf_read,
	.write = perf_write,
	.unlocked_ioctl = perf_ioctl,
	.compat_ioctl = perf_ioctl,
	.open = perf_open,
	.release = perf_release
};
	
static struct miscdevice perf_dev = {
	MISC_DYNAMIC_MINOR,
	PA_PERF_DEV,
	&perf_fops
};

/*
 * Initialize the module
 */
static int __init perf_init(void)
{
	int ret;

	/* Determine correct processor interface to use */
	bitmask_array = perf_bitmasks;

	if (boot_cpu_data.cpu_type == pcxu ||
	    boot_cpu_data.cpu_type == pcxu_) {
		perf_processor_interface = ONYX_INTF;
	} else if (boot_cpu_data.cpu_type == pcxw ||
		 boot_cpu_data.cpu_type == pcxw_ ||
		 boot_cpu_data.cpu_type == pcxw2 ||
		 boot_cpu_data.cpu_type == mako) {
		perf_processor_interface = CUDA_INTF;
		if (boot_cpu_data.cpu_type == pcxw2 ||
		    boot_cpu_data.cpu_type == mako) 
			bitmask_array = perf_bitmasks_piranha;
	} else {
		perf_processor_interface = UNKNOWN_INTF;
		printk("Performance monitoring counters not supported on this processor\n");
		return -ENODEV;
	}

	ret = misc_register(&perf_dev);
	if (ret) {
		printk(KERN_ERR "Performance monitoring counters: "
			"cannot register misc device.\n");
		return ret;
	}

	/* Patch the images to match the system */
    	perf_patch_images();

	spin_lock_init(&perf_lock);

	/* TODO: this only lets us access the first cpu.. what to do for SMP? */
	cpu_device = cpu_data[0].dev;
	printk("Performance monitoring counters enabled for %s\n",
		cpu_data[0].dev->name);

	return 0;
}

/*
 * perf_start_counters(void)
 *
 * Start the counters.
 */
static void perf_start_counters(void)
{
	/* Enable performance monitor counters */
	perf_intrigue_enable_perf_counters();
}

/*
 * perf_stop_counters
 *
 * Stop the performance counters and save counts
 * in a per_processor array.
 */
static int perf_stop_counters(uint32_t *raddr)
{
	uint64_t userbuf[MAX_RDR_WORDS];

	/* Disable performance counters */
	perf_intrigue_disable_perf_counters();

	if (perf_processor_interface == ONYX_INTF) {
		uint64_t tmp64;
		/*
		 * Read the counters
		 */
		if (!perf_rdr_read_ubuf(16, userbuf))
			return -13;

		/* Counter0 is bits 1398 thru 1429 */
		tmp64 =  (userbuf[21] << 22) & 0x00000000ffc00000;
		tmp64 |= (userbuf[22] >> 42) & 0x00000000003fffff;
		/* OR sticky0 (bit 1430) to counter0 bit 32 */
		tmp64 |= (userbuf[22] >> 10) & 0x0000000080000000;
		raddr[0] = (uint32_t)tmp64;

		/* Counter1 is bits 1431 thru 1462 */
		tmp64 =  (userbuf[22] >> 9) & 0x00000000ffffffff;
		/* OR sticky1 (bit 1463) to counter1 bit 32 */
		tmp64 |= (userbuf[22] << 23) & 0x0000000080000000;
		raddr[1] = (uint32_t)tmp64;

		/* Counter2 is bits 1464 thru 1495 */
		tmp64 =  (userbuf[22] << 24) & 0x00000000ff000000;
		tmp64 |= (userbuf[23] >> 40) & 0x0000000000ffffff;
		/* OR sticky2 (bit 1496) to counter2 bit 32 */
		tmp64 |= (userbuf[23] >> 8) & 0x0000000080000000;
		raddr[2] = (uint32_t)tmp64;
		
		/* Counter3 is bits 1497 thru 1528 */
		tmp64 =  (userbuf[23] >> 7) & 0x00000000ffffffff;
		/* OR sticky3 (bit 1529) to counter3 bit 32 */
		tmp64 |= (userbuf[23] << 25) & 0x0000000080000000;
		raddr[3] = (uint32_t)tmp64;

		/*
		 * Zero out the counters
		 */

		/*
		 * The counters and sticky-bits comprise the last 132 bits
		 * (1398 - 1529) of RDR16 on a U chip.  We'll zero these
		 * out the easy way: zero out last 10 bits of dword 21,
		 * all of dword 22 and 58 bits (plus 6 don't care bits) of
		 * dword 23.
		 */
		userbuf[21] &= 0xfffffffffffffc00ul;	/* 0 to last 10 bits */
		userbuf[22] = 0;
		userbuf[23] = 0;

		/* 
		 * Write back the zero'ed bytes + the image given
		 * the read was destructive.
		 */
		perf_rdr_write(16, userbuf);
	} else {

		/*
		 * Read RDR-15 which contains the counters and sticky bits 
		 */
		if (!perf_rdr_read_ubuf(15, userbuf)) {
			return -13;
		}

		/* 
		 * Clear out the counters
		 */
		perf_rdr_clear(15);

		/*
		 * Copy the counters 
		 */
		raddr[0] = (uint32_t)((userbuf[0] >> 32) & 0x00000000ffffffffUL);
		raddr[1] = (uint32_t)(userbuf[0] & 0x00000000ffffffffUL);
		raddr[2] = (uint32_t)((userbuf[1] >> 32) & 0x00000000ffffffffUL);
		raddr[3] = (uint32_t)(userbuf[1] & 0x00000000ffffffffUL);
	}
 
	return 0;
}

/*
 * perf_rdr_get_entry
 *
 * Retrieve a pointer to the description of what this
 * RDR contains.
 */
static const struct rdr_tbl_ent * perf_rdr_get_entry(uint32_t rdr_num)
{
	if (perf_processor_interface == ONYX_INTF) {
		return &perf_rdr_tbl_U[rdr_num];
	} else {
		return &perf_rdr_tbl_W[rdr_num];
	}
}

/*
 * perf_rdr_read_ubuf
 *
 * Read the RDR value into the buffer specified.
 */
static int perf_rdr_read_ubuf(uint32_t	rdr_num, uint64_t *buffer)
{
	uint64_t	data, data_mask = 0;
	uint32_t	width, xbits, i;
	const struct rdr_tbl_ent *tentry;

	tentry = perf_rdr_get_entry(rdr_num);
	if ((width = tentry->width) == 0)
		return 0;

	/* Clear out buffer */
	i = tentry->num_words;
	while (i--) {
		buffer[i] = 0;
	}	

	/* Check for bits an even number of 64 */
	if ((xbits = width & 0x03f) != 0) {
		data_mask = 1;
		data_mask <<= (64 - xbits);
		data_mask--;
	}

	/* Grab all of the data */
	i = tentry->num_words;
	while (i--) {

		if (perf_processor_interface == ONYX_INTF) {
			data = perf_rdr_shift_in_U(rdr_num, width);
		} else {
			data = perf_rdr_shift_in_W(rdr_num, width);
		}
		if (xbits) {
			buffer[i] |= (data << (64 - xbits));
			if (i) {
				buffer[i-1] |= ((data >> xbits) & data_mask);
			}
		} else {
			buffer[i] = data;
		}
	}

	return 1;
}

/*
 * perf_rdr_clear
 *
 * Zero out the given RDR register
 */
static int perf_rdr_clear(uint32_t	rdr_num)
{
	const struct rdr_tbl_ent *tentry;
	int32_t		i;

	tentry = perf_rdr_get_entry(rdr_num);

	if (tentry->width == 0) {
		return -1;
	}

	i = tentry->num_words;
	while (i--) {
		if (perf_processor_interface == ONYX_INTF) {
			perf_rdr_shift_out_U(rdr_num, 0UL);
		} else {
			perf_rdr_shift_out_W(rdr_num, 0UL);
		}
	}

	return 0;
}


/*
 * perf_write_image
 *
 * Write the given image out to the processor
 */
static int perf_write_image(uint64_t *memaddr)
{
	uint64_t buffer[MAX_RDR_WORDS];
	uint64_t *bptr;
	uint32_t dwords;
	const uint32_t *intrigue_rdr;
	const uint64_t *intrigue_bitmask;
	uint64_t tmp64;
	void __iomem *runway;
	const struct rdr_tbl_ent *tentry;
	int i;

	/* Clear out counters */
	if (perf_processor_interface == ONYX_INTF) {

		perf_rdr_clear(16);

		/* Toggle performance monitor */
		perf_intrigue_enable_perf_counters();
		perf_intrigue_disable_perf_counters();

		intrigue_rdr = perf_rdrs_U;
	} else {
		perf_rdr_clear(15);
		intrigue_rdr = perf_rdrs_W;
	}

	/* Write all RDRs */
	while (*intrigue_rdr != -1) {
		tentry = perf_rdr_get_entry(*intrigue_rdr);
		perf_rdr_read_ubuf(*intrigue_rdr, buffer);
		bptr   = &buffer[0];
		dwords = tentry->num_words;
		if (tentry->write_control) {
			intrigue_bitmask = &bitmask_array[tentry->write_control >> 3];
			while (dwords--) {
				tmp64 = *intrigue_bitmask & *memaddr++;
				tmp64 |= (~(*intrigue_bitmask++)) & *bptr;
				*bptr++ = tmp64;
			}
		} else {
			while (dwords--) {
				*bptr++ = *memaddr++;
			}
		}

		perf_rdr_write(*intrigue_rdr, buffer);
		intrigue_rdr++;
	}

	/*
	 * Now copy out the Runway stuff which is not in RDRs
	 */

	if (cpu_device == NULL)
	{
		printk(KERN_ERR "write_image: cpu_device not yet initialized!\n");
		return -1;
	}

	runway = ioremap_nocache(cpu_device->hpa.start, 4096);

	/* Merge intrigue bits into Runway STATUS 0 */
	tmp64 = __raw_readq(runway + RUNWAY_STATUS) & 0xffecfffffffffffful;
	__raw_writeq(tmp64 | (*memaddr++ & 0x0013000000000000ul), 
		     runway + RUNWAY_STATUS);
	
	/* Write RUNWAY DEBUG registers */
	for (i = 0; i < 8; i++) {
		__raw_writeq(*memaddr++, runway + RUNWAY_DEBUG);
	}

	return 0; 
}

/*
 * perf_rdr_write
 *
 * Write the given RDR register with the contents
 * of the given buffer.
 */
static void perf_rdr_write(uint32_t rdr_num, uint64_t *buffer)
{
	const struct rdr_tbl_ent *tentry;
	int32_t		i;

printk("perf_rdr_write\n");
	tentry = perf_rdr_get_entry(rdr_num);
	if (tentry->width == 0) { return; }

	i = tentry->num_words;
	while (i--) {
		if (perf_processor_interface == ONYX_INTF) {
			perf_rdr_shift_out_U(rdr_num, buffer[i]);
		} else {
			perf_rdr_shift_out_W(rdr_num, buffer[i]);
		}	
	}
printk("perf_rdr_write done\n");
}

module_init(perf_init);
