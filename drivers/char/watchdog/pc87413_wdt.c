/*
 *      NS pc87413-wdt Watchdog Timer driver for Linux 2.6.x.x
 *
 *      This code is based on wdt.c with original copyright
 *
 *      (C) Copyright 2006 Marcus Junker, <junker@anduras.de>
 *                     and Sven Anders, <anders@anduras.de>
 *
 *      This program is free software; you can redistribute it and/or
 *      modify it under the terms of the GNU General Public License
 *      as published by the Free Software Foundation; either version
 *      2 of the License, or (at your option) any later version.
 *
 *      Neither Marcus Junker, Sven Anders nor ANDURAS AG
 *      admit liability nor provide warranty for any of this software.
 *      This material is provided "AS-IS" and at no charge.
 *
 *      Release 1.00.
 *
 */


/* #define DEBUG 1 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/miscdevice.h>
#include <linux/watchdog.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/notifier.h>
#include <linux/fs.h>
#include <linux/reboot.h>
#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/moduleparam.h>
#include <linux/version.h>

#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/system.h>

#define WATCHDOG_NAME "pc87413 WDT"
#define PFX WATCHDOG_NAME ": "
#define DPFX WATCHDOG_NAME " - DEBUG: "

#define WDT_INDEX_IO_PORT   (io+0)                /* */
#define WDT_DATA_IO_PORT    (WDT_INDEX_IO_PORT+1)
#define SWC_LDN              0x04
#define SIOCFG2              0x22                 /* Serial IO register */
#define WDCTL                0x10                 /* Watchdog-Timer-Controll-Register */
#define WDTO                 0x11                 /* Watchdog timeout register */
#define WDCFG                0x12                 /* Watchdog config register */

#define WD_TIMEOUT 1				   /* minutes (1 ... 255) */


static int pc87413_is_open=0;

/*
 *	You must set these - there is no sane way to probe for this board.
 *	You can use pc87413=x to set these now.
 */



/**
 *	module_params
 *
 *	Setup options. The board isn't really probe-able so we have to
 *	get the user to tell us the configuration.
 */

static int io=0x2E;		/* Normally used addres on Portwell Boards */
module_param(io, int, 0);
MODULE_PARM_DESC(wdt_io, WATCHDOG_NAME  " io port (default 0x2E)");

static int timeout = WD_TIMEOUT;  /* in minutes */
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout, "Watchdog timeout in minutes. 1<= timeout <=63, default=" __MODULE_STRING(WD_TIMEOUT) ".");


/******************************************
 *      Helper functions
 ******************************************/

static void
pc87413_select_wdt_out (void)
{

	unsigned int    cr_data=0;

	/* Select multiple pin,pin55,as WDT output */

	outb_p(SIOCFG2, WDT_INDEX_IO_PORT);

	cr_data = inb (WDT_DATA_IO_PORT);

	cr_data |= 0x80; /* Set Bit7 to 1*/
	outb_p(SIOCFG2, WDT_INDEX_IO_PORT);

	outb_p(cr_data, WDT_DATA_IO_PORT);


	#ifdef DEBUG
	printk(KERN_INFO DPFX "Select multiple pin,pin55,as WDT output: Bit7 to 1: %d\n", cr_data);
	#endif
}

static void
pc87413_enable_swc(void)
{

	unsigned int    cr_data=0;

	/* Step 2: Enable SWC functions */

	outb_p(0x07, WDT_INDEX_IO_PORT);        /* Point SWC_LDN (LDN=4) */
	outb_p(SWC_LDN, WDT_DATA_IO_PORT);

	outb_p(0x30, WDT_INDEX_IO_PORT);        /* Read Index 0x30 First */
	cr_data = inb (WDT_DATA_IO_PORT);
	cr_data |= 0x01;                        /* Set Bit0 to 1 */
	outb_p(0x30, WDT_INDEX_IO_PORT);
	outb_p(cr_data, WDT_DATA_IO_PORT);      /* Index0x30_bit0P1 */

	#ifdef DEBUG
	printk(KERN_INFO DPFX "pc87413 - Enable SWC functions\n");
	#endif
}

static unsigned int
pc87413_get_swc_base(void)
{
	unsigned int    swc_base_addr = 0;
	unsigned char   addr_l, addr_h = 0;

	/* Step 3: Read SWC I/O Base Address */

	outb_p(0x60, WDT_INDEX_IO_PORT);        /* Read Index 0x60 */
	addr_h = inb (WDT_DATA_IO_PORT);

	outb_p(0x61, WDT_INDEX_IO_PORT);        /* Read Index 0x61 */

	addr_l = inb (WDT_DATA_IO_PORT);


	swc_base_addr = (addr_h << 8) + addr_l;

	#ifdef DEBUG
	printk(KERN_INFO DPFX "Read SWC I/O Base Address:  low %d, high %d, res %d\n", addr_l, addr_h, swc_base_addr);
	#endif

	return swc_base_addr;
}

static void
pc87413_swc_bank3(unsigned int swc_base_addr)
{
	/* Step 4: Select Bank3 of SWC */

	outb_p (inb (swc_base_addr + 0x0f) | 0x03, swc_base_addr + 0x0f);

	#ifdef DEBUG
	printk(KERN_INFO DPFX "Select Bank3 of SWC\n");
	#endif

}

static void
pc87413_programm_wdto (unsigned int swc_base_addr, char pc87413_time)
{
	/* Step 5: Programm WDTO, Twd. */

	outb_p (pc87413_time, swc_base_addr + WDTO);

	#ifdef DEBUG
	printk(KERN_INFO DPFX "Set WDTO to %d minutes\n", pc87413_time);
	#endif
}

static void
pc87413_enable_wden (unsigned int swc_base_addr)
{
	/* Step 6: Enable WDEN */

	outb_p(inb (swc_base_addr + WDCTL) | 0x01, swc_base_addr + WDCTL);

	#ifdef DEBUG
	printk(KERN_INFO DPFX "Enable WDEN\n");
	#endif
}

static void
pc87413_enable_sw_wd_tren (unsigned int swc_base_addr)
{
	/* Enable SW_WD_TREN */

	outb_p(inb (swc_base_addr + WDCFG) | 0x80, swc_base_addr + WDCFG);

	#ifdef DEBUG
	printk(KERN_INFO DPFX "Enable SW_WD_TREN\n");
	#endif
}

static void
pc87413_disable_sw_wd_tren (unsigned int swc_base_addr)
{
	/* Disable SW_WD_TREN */

	outb_p(inb (swc_base_addr + WDCFG) & 0x7f, swc_base_addr + WDCFG);

	#ifdef DEBUG
	printk(KERN_INFO DPFX "pc87413 - Disable SW_WD_TREN\n");
	#endif
}


static void
pc87413_enable_sw_wd_trg (unsigned int swc_base_addr)
{

	/* Enable SW_WD_TRG */

	outb_p(inb (swc_base_addr + WDCTL) | 0x80, swc_base_addr + WDCTL);

	#ifdef DEBUG
	printk(KERN_INFO DPFX "pc87413 - Enable SW_WD_TRG\n");
	#endif
}

static void
pc87413_disable_sw_wd_trg (unsigned int swc_base_addr)
{

	/* Disable SW_WD_TRG */

	outb_p(inb (swc_base_addr + WDCTL) & 0x7f, swc_base_addr + WDCTL);

	#ifdef DEBUG
	printk(KERN_INFO DPFX "Disable SW_WD_TRG\n");
	#endif
}



static void
pc87413_disable(void)
{
	unsigned int    swc_base_addr;

	pc87413_select_wdt_out();
	pc87413_enable_swc();
	swc_base_addr = pc87413_get_swc_base();
	pc87413_swc_bank3(swc_base_addr);
	pc87413_disable_sw_wd_tren(swc_base_addr);
	pc87413_disable_sw_wd_trg(swc_base_addr);
	pc87413_programm_wdto(swc_base_addr, 0);
}


static void
pc87413_refresh(char pc87413_time)
{
	unsigned int    swc_base_addr;

	pc87413_select_wdt_out();
	pc87413_enable_swc();
	swc_base_addr = pc87413_get_swc_base();
	pc87413_swc_bank3(swc_base_addr);
	pc87413_disable_sw_wd_tren(swc_base_addr);
	pc87413_disable_sw_wd_trg(swc_base_addr);
	pc87413_programm_wdto(swc_base_addr, pc87413_time);
	pc87413_enable_wden(swc_base_addr);
	pc87413_enable_sw_wd_tren(swc_base_addr);
	pc87413_enable_sw_wd_trg(swc_base_addr);
}


static void
pc87413_enable(char pc87413_time)
{
	unsigned int    swc_base_addr;

	pc87413_select_wdt_out();
	pc87413_enable_swc();
	swc_base_addr = pc87413_get_swc_base();
	pc87413_swc_bank3(swc_base_addr);
	pc87413_programm_wdto(swc_base_addr, pc87413_time);
	pc87413_enable_wden(swc_base_addr);
	pc87413_enable_sw_wd_tren(swc_base_addr);
	pc87413_enable_sw_wd_trg(swc_base_addr);

}


/*******************************************
 *	Kernel methods.
 *******************************************/


/**
 *	pc87413_status:
 *
 *	Extract the status information from a WDT watchdog device. There are
 *	several board variants so we have to know which bits are valid. Some
 *	bits default to one and some to zero in order to be maximally painful.
 *
 *	we then map the bits onto the status ioctl flags.
 */

static int pc87413_status(void)
{
	/* Not supported */

	return 1;
}

static long long pc87413_llseek(struct file *file, long long offset, int origin)
{
	return -ESPIPE;
}

/**
 *	pc87413_write:
 *	@file: file handle to the watchdog
 *	@buf: buffer to write (unused as data does not matter here
 *	@count: count of bytes
 *	@ppos: pointer to the position to write. No seeks allowed
 *
 *	A write to a watchdog device is defined as a keepalive signal. Any
 *	write of data will do, as we we don't define content meaning.
 */

static ssize_t
pc87413_write (struct file *file, const char __user *buf, size_t count, loff_t *ppos)
{
	if (count) {
	        pc87413_refresh (WD_TIMEOUT);
	        #ifdef DEBUG
	        printk(KERN_INFO DPFX "Write\n");
	        #endif
	}
	return count;
}

/*
 *      Read reports the temperature in degrees Fahrenheit.
 */
static ssize_t
pc87413_read(struct file *file, char *buf, size_t count, loff_t *ptr)
{

//	char timeout;
//
//	outb_p(0x08, WDT_EFER);		/* Select locical device 8 */
//	outb_p(0x0F6, WDT_EFER);		/* Select CRF6 */
//	timeout = inb(WDT_EFDR);	/* Read Timeout counter from CRF6 */


//	if(copy_to_user(buf,&timeout,1))
//		return -EFAULT;
	return 1;


}

 /**
 *	pc87413_ioctl:
 *	@inode: inode of the device
 *	@file: file handle to the device
 *	@cmd: watchdog command
 *	@arg: argument pointer
 *
 *	The watchdog API defines a common set of functions for all watchdogs
 *	according to their available features. We only actually usefully support
 *	querying capabilities and current status.
 */

static int
pc87413_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
	unsigned long arg)
{
	static struct watchdog_info ident=
	{
	        .options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT | WDIOF_MAGICCLOSE,
	        .firmware_version = 1,
		.identity = "pc87413(HF/F)"
	};

	ident.options=1;	/* Mask down to the card we have */

	switch(cmd)
	{
		default:
			return -ENOIOCTLCMD;
		case WDIOC_GETSUPPORT:
			return copy_to_user((struct watchdog_info *)arg, &ident, sizeof(ident))?-EFAULT:0;
		case WDIOC_GETSTATUS:
			return put_user(pc87413_status(),(int *)arg);
		case WDIOC_GETBOOTSTATUS:
			return put_user(0, (int *)arg);
		case WDIOC_KEEPALIVE:
			pc87413_refresh(WD_TIMEOUT);
			#ifdef DEBUG
			printk(KERN_INFO DPFX "keepalive\n");
			#endif
			return 0;
	}
}

/**
 *	pc87413_open:
 *	@inode: inode of device
 *	@file: file handle to device
 *
 *	One of our two misc devices has been opened. The watchdog device is
 *	single open and on opening we load the counters. Counter zero is a
 *	100Hz cascade, into counter 1 which downcounts to reboot. When the
 *	counter triggers counter 2 downcounts the length of the reset pulse
 *	which set set to be as long as possible.
 */

static int
pc87413_open(struct inode *inode, struct file *file)
{
	switch(MINOR(inode->i_rdev))
	{
		case WATCHDOG_MINOR:
			if(pc87413_is_open)
				return -EBUSY;
			/*
			 *	Activate
			 */

			pc87413_is_open=1;
			pc87413_refresh(WD_TIMEOUT);
			#ifdef DEBUG
			printk(KERN_INFO DPFX "Open\n");
			#endif
			return 0;
		case TEMP_MINOR:
			return 0;
		default:
			return -ENODEV;
	}
}

/**
 *	pc87413_close:
 *	@inode: inode to board
 *	@file: file handle to board
 *
 *	The watchdog has a configurable API. There is a religious dispute
 *	between people who want their watchdog to be able to shut down and
 *	those who want to be sure if the watchdog manager dies the machine
 *	reboots. In the former case we disable the counters, in the latter
 *	case you have to open it again very soon.
 */

static int
pc87413_release(struct inode *inode, struct file *file)
{
	if(MINOR(inode->i_rdev)==WATCHDOG_MINOR)
	{
#ifndef CONFIG_WATCHDOG_NOWAYOUT
		pc87413_disable();
#endif
		pc87413_is_open=0;
		#ifdef DEBUG
		printk(KERN_INFO DPFX "Release\n");
		#endif
	}
	return 0;
}

/**
 *	notify_sys:
 *	@this: our notifier block
 *	@code: the event being reported
 *	@unused: unused
 *
 *	Our notifier is called on system shutdowns. We want to turn the card
 *	off at reboot otherwise the machine will reboot again during memory
 *	test or worse yet during the following fsck. This would suck, in fact
 *	trust me - if it happens it does suck.
 */

static int
pc87413_notify_sys(struct notifier_block *this, unsigned long code,
	void *unused)
{
	if(code==SYS_DOWN || code==SYS_HALT)
	{
		/* Turn the card off */
		pc87413_disable();
	}
	return NOTIFY_DONE;
}


/*****************************************************
 *	Kernel Interfaces
 *****************************************************/


static struct file_operations pc87413_fops = {
	.owner		= THIS_MODULE,
	.llseek		= pc87413_llseek,
	.read		= pc87413_read,
	.write		= pc87413_write,
	.ioctl		= pc87413_ioctl,
	.open		= pc87413_open,
	.release	= pc87413_release,
};

static struct miscdevice pc87413_miscdev=
{
	.minor = WATCHDOG_MINOR,
	.name = "watchdog",
	.fops = &pc87413_fops
};

/*
 *	The WDT card needs to learn about soft shutdowns in order to
 *	turn the timebomb registers off.
 */

static struct notifier_block pc87413_notifier=
{
	pc87413_notify_sys,
	NULL,
	0
};


/**
 *	pc87413_exit:
 *
 *	Unload the watchdog. You cannot do this with any file handles open.
 *	If your watchdog is set to continue ticking on close and you unload
 *	it, well it keeps ticking. We won't get the interrupt but the board
 *	will not touch PC memory so all is fine. You just have to load a new
 *	module in 60 seconds or reboot.
 */

static void
pc87413_exit(void)
{
	pc87413_disable();
	misc_deregister(&pc87413_miscdev);
	unregister_reboot_notifier(&pc87413_notifier);
	/* release_region(io,2); */
}


/**
 * 	pc87413_init:
 *
 *	Set up the WDT watchdog board. All we have to do is grab the
 *	resources we require and bitch if anyone beat us to them.
 *	The open() function will actually kick the board off.
 */

static int
pc87413_init(void)
{
	int ret;

	printk(KERN_INFO PFX "Version 1.00 at io 0x%X\n", WDT_INDEX_IO_PORT);


	/* request_region(io, 2, "pc87413"); */


	ret = register_reboot_notifier(&pc87413_notifier);
	if (ret != 0) {
	        printk (KERN_ERR PFX "cannot register reboot notifier (err=%d)\n",
	                ret);
	}



	ret = misc_register(&pc87413_miscdev);

	if (ret != 0) {
	        printk (KERN_ERR PFX "cannot register miscdev on minor=%d (err=%d)\n",
	                WATCHDOG_MINOR, ret);
		unregister_reboot_notifier(&pc87413_notifier);
		return ret;
	}

	printk (KERN_INFO PFX "initialized. timeout=%d min \n", timeout);


	pc87413_enable(WD_TIMEOUT);

	return 0;
}


module_init(pc87413_init);
module_exit(pc87413_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Marcus Junker <junker@anduras.de>, Sven Anders <anders@anduras.de>");
MODULE_DESCRIPTION("PC87413 WDT driver");
MODULE_ALIAS_MISCDEV(WATCHDOG_MINOR);

