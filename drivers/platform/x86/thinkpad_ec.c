/*
 *  thinkpad_ec.c - ThinkPad embedded controller LPC3 functions
 *
 *  The embedded controller on ThinkPad laptops has a non-standard interface,
 *  where LPC channel 3 of the H8S EC chip is hooked up to IO ports
 *  0x1600-0x161F and implements (a special case of) the H8S LPC protocol.
 *  The EC LPC interface provides various system management services (currently
 *  known: battery information and accelerometer readouts). This driver
 *  provides access and mutual exclusion for the EC interface.
*
 *  The LPC protocol and terminology are documented here:
 *  "H8S/2104B Group Hardware Manual",
 *  http://documentation.renesas.com/eng/products/mpumcu/rej09b0300_2140bhm.pdf
 *
 *  Copyright (C) 2006-2007 Shem Multinymous <multinymous@gmail.com>
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
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/dmi.h>
#include <linux/ioport.h>
#include <linux/delay.h>
#include <linux/thinkpad_ec.h>
#include <linux/jiffies.h>
#include <asm/io.h>

#include <linux/version.h>
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,26)
	#include <asm/semaphore.h>
#else
	#include <linux/semaphore.h>
#endif

#define TP_VERSION "0.41"

MODULE_AUTHOR("Shem Multinymous");
MODULE_DESCRIPTION("ThinkPad embedded controller hardware access");
MODULE_VERSION(TP_VERSION);
MODULE_LICENSE("GPL");

/* IO ports used by embedded controller LPC channel 3: */
#define TPC_BASE_PORT 0x1600
#define TPC_NUM_PORTS 0x20
#define TPC_STR3_PORT 0x1604  /* Reads H8S EC register STR3 */
#define TPC_TWR0_PORT  0x1610 /* Mapped to H8S EC register TWR0MW/SW  */
#define TPC_TWR15_PORT 0x161F /* Mapped to H8S EC register TWR15. */
  /* (and port TPC_TWR0_PORT+i is mapped to H8S reg TWRi for 0<i<16) */

/* H8S STR3 status flags (see "H8S/2104B Group Hardware Manual" p.549) */
#define H8S_STR3_IBF3B 0x80  /* Bidi. Data Register Input Buffer Full */
#define H8S_STR3_OBF3B 0x40  /* Bidi. Data Register Output Buffer Full */
#define H8S_STR3_MWMF  0x20  /* Master Write Mode Flag */
#define H8S_STR3_SWMF  0x10  /* Slave Write Mode Flag */
#define H8S_STR3_MASK  0xF0  /* All bits we care about in STR3 */

/* Timeouts and retries */
#define TPC_READ_RETRIES     150
#define TPC_READ_NDELAY      500
#define TPC_REQUEST_RETRIES 1000
#define TPC_REQUEST_NDELAY    10
#define TPC_PREFETCH_TIMEOUT   (HZ/10)  /* invalidate prefetch after 0.1sec */

/* A few macros for printk()ing: */
#define MSG_FMT(fmt, args...) \
  "thinkpad_ec: %s: " fmt "\n", __func__, ## args
#define REQ_FMT(msg, code) \
  MSG_FMT("%s: (0x%02x:0x%02x)->0x%02x", \
	  msg, args->val[0x0], args->val[0xF], code)

/* State of request prefetching: */
static u8 prefetch_arg0, prefetch_argF;           /* Args of last prefetch */
static u64 prefetch_jiffies;                      /* time of prefetch, or: */
#define TPC_PREFETCH_NONE   INITIAL_JIFFIES       /*   No prefetch */
#define TPC_PREFETCH_JUNK   (INITIAL_JIFFIES+1)   /*   Ignore prefetch */

/* Locking: */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
static DECLARE_MUTEX(thinkpad_ec_mutex);
#else
static DEFINE_SEMAPHORE(thinkpad_ec_mutex);
#endif

/* Kludge in case the ACPI DSDT reserves the ports we need. */
static int force_io;    /* Willing to do IO to ports we couldn't reserve? */
static int reserved_io; /* Successfully reserved the ports? */
module_param_named(force_io, force_io, bool, 0600);
MODULE_PARM_DESC(force_io, "Force IO even if region already reserved (0=off, 1=on)");

/**
 * thinkpad_ec_lock - get lock on the ThinkPad EC
 *
 * Get exclusive lock for accesing the ThinkPad embedded controller LPC3
 * interface. Returns 0 iff lock acquired.
 */
int thinkpad_ec_lock(void)
{
	int ret;
	ret = down_interruptible(&thinkpad_ec_mutex);
	return ret;
}
EXPORT_SYMBOL_GPL(thinkpad_ec_lock);

/**
 * thinkpad_ec_try_lock - try getting lock on the ThinkPad EC
 *
 * Try getting an exclusive lock for accesing the ThinkPad embedded
 * controller LPC3. Returns immediately if lock is not available; neither
 * blocks nor sleeps. Returns 0 iff lock acquired .
 */
int thinkpad_ec_try_lock(void)
{
	return down_trylock(&thinkpad_ec_mutex);
}
EXPORT_SYMBOL_GPL(thinkpad_ec_try_lock);

/**
 * thinkpad_ec_unlock - release lock on ThinkPad EC
 *
 * Release a previously acquired exclusive lock on the ThinkPad ebmedded
 * controller LPC3 interface.
 */
void thinkpad_ec_unlock(void)
{
	up(&thinkpad_ec_mutex);
}
EXPORT_SYMBOL_GPL(thinkpad_ec_unlock);

/**
 * thinkpad_ec_request_row - tell embedded controller to prepare a row
 * @args Input register arguments
 *
 * Requests a data row by writing to H8S LPC registers TRW0 through TWR15 (or
 * a subset thereof) following the protocol prescribed by the "H8S/2104B Group
 * Hardware Manual". Does sanity checks via status register STR3.
 */
static int thinkpad_ec_request_row(const struct thinkpad_ec_row *args)
{
	u8 str3;
	int i;

	/* EC protocol requires write to TWR0 (function code): */
	if (!(args->mask & 0x0001)) {
		printk(KERN_ERR MSG_FMT("bad args->mask=0x%02x", args->mask));
		return -EINVAL;
	}

	/* Check initial STR3 status: */
	str3 = inb(TPC_STR3_PORT) & H8S_STR3_MASK;
	if (str3 & H8S_STR3_OBF3B) { /* data already pending */
		inb(TPC_TWR15_PORT); /* marks end of previous transaction */
		if (prefetch_jiffies == TPC_PREFETCH_NONE)
			printk(KERN_WARNING REQ_FMT(
			       "EC has result from unrequested transaction",
			       str3));
		return -EBUSY; /* EC will be ready in a few usecs */
	} else if (str3 == H8S_STR3_SWMF) { /* busy with previous request */
		if (prefetch_jiffies == TPC_PREFETCH_NONE)
			printk(KERN_WARNING REQ_FMT(
			       "EC is busy with unrequested transaction",
			       str3));
		return -EBUSY; /* data will be pending in a few usecs */
	} else if (str3 != 0x00) { /* unexpected status? */
		printk(KERN_WARNING REQ_FMT("unexpected initial STR3", str3));
		return -EIO;
	}

	/* Send TWR0MW: */
	outb(args->val[0], TPC_TWR0_PORT);
	str3 = inb(TPC_STR3_PORT) & H8S_STR3_MASK;
	if (str3 != H8S_STR3_MWMF) { /* not accepted? */
		printk(KERN_WARNING REQ_FMT("arg0 rejected", str3));
		return -EIO;
	}

	/* Send TWR1 through TWR14: */
	for (i = 1; i < TP_CONTROLLER_ROW_LEN-1; i++)
		if ((args->mask>>i)&1)
			outb(args->val[i], TPC_TWR0_PORT+i);

	/* Send TWR15 (default to 0x01). This marks end of command. */
	outb((args->mask & 0x8000) ? args->val[0xF] : 0x01, TPC_TWR15_PORT);

	/* Wait until EC starts writing its reply (~60ns on average).
	 * Releasing locks before this happens may cause an EC hang
	 * due to firmware bug!
	 */
	for (i = 0; i < TPC_REQUEST_RETRIES; i++) {
		str3 = inb(TPC_STR3_PORT) & H8S_STR3_MASK;
		if (str3 & H8S_STR3_SWMF) /* EC started replying */
			return 0;
		else if (!(str3 & ~(H8S_STR3_IBF3B|H8S_STR3_MWMF)))
			/* Normal progress (the EC hasn't seen the request
			 * yet, or is processing it). Wait it out. */
			ndelay(TPC_REQUEST_NDELAY);
		else { /* weird EC status */
			printk(KERN_WARNING
			       REQ_FMT("bad end STR3", str3));
			return -EIO;
		}
	}
	printk(KERN_WARNING REQ_FMT("EC is mysteriously silent", str3));
	return -EIO;
}

/**
 * thinkpad_ec_read_data - read pre-requested row-data from EC
 * @args Input register arguments of pre-requested rows
 * @data Output register values
 *
 * Reads current row data from the controller, assuming it's already
 * requested. Follows the H8S spec for register access and status checks.
 */
static int thinkpad_ec_read_data(const struct thinkpad_ec_row *args,
				 struct thinkpad_ec_row *data)
{
	int i;
	u8 str3 = inb(TPC_STR3_PORT) & H8S_STR3_MASK;
	/* Once we make a request, STR3 assumes the sequence of values listed
	 * in the following 'if' as it reads the request and writes its data.
	 * It takes about a few dozen nanosecs total, with very high variance.
	 */
	if (str3 == (H8S_STR3_IBF3B|H8S_STR3_MWMF) ||
	    str3 == 0x00 ||  /* the 0x00 is indistinguishable from idle EC! */
	    str3 == H8S_STR3_SWMF)
		return -EBUSY; /* not ready yet */
	/* Finally, the EC signals output buffer full: */
	if (str3 != (H8S_STR3_OBF3B|H8S_STR3_SWMF)) {
		printk(KERN_WARNING
		       REQ_FMT("bad initial STR3", str3));
		return -EIO;
	}

	/* Read first byte (signals start of read transactions): */
	data->val[0] = inb(TPC_TWR0_PORT);
	/* Optionally read 14 more bytes: */
	for (i = 1; i < TP_CONTROLLER_ROW_LEN-1; i++)
		if ((data->mask >> i)&1)
			data->val[i] = inb(TPC_TWR0_PORT+i);
	/* Read last byte from 0x161F (signals end of read transaction): */
	data->val[0xF] = inb(TPC_TWR15_PORT);

	/* Readout still pending? */
	str3 = inb(TPC_STR3_PORT) & H8S_STR3_MASK;
	if (str3 & H8S_STR3_OBF3B)
		printk(KERN_WARNING
		       REQ_FMT("OBF3B=1 after read", str3));
	/* If port 0x161F returns 0x80 too often, the EC may lock up. Warn: */
	if (data->val[0xF] == 0x80)
		printk(KERN_WARNING
		       REQ_FMT("0x161F reports error", data->val[0xF]));
	return 0;
}

/**
 * thinkpad_ec_is_row_fetched - is the given row currently prefetched?
 *
 * To keep things simple we compare only the first and last args;
 * this suffices for all known cases.
 */
static int thinkpad_ec_is_row_fetched(const struct thinkpad_ec_row *args)
{
	return (prefetch_jiffies != TPC_PREFETCH_NONE) &&
	       (prefetch_jiffies != TPC_PREFETCH_JUNK) &&
	       (prefetch_arg0 == args->val[0]) &&
	       (prefetch_argF == args->val[0xF]) &&
	       (get_jiffies_64() < prefetch_jiffies + TPC_PREFETCH_TIMEOUT);
}

/**
 * thinkpad_ec_read_row - request and read data from ThinkPad EC
 * @args Input register arguments
 * @data Output register values
 *
 * Read a data row from the ThinkPad embedded controller LPC3 interface.
 * Does fetching and retrying if needed. The row is specified by an
 * array of 16 bytes, some of which may be undefined (but the first is
 * mandatory). These bytes are given in @args->val[], where @args->val[i] is
 * used iff (@args->mask>>i)&1). The resulting row data is stored in
 * @data->val[], but is only guaranteed to be valid for indices corresponding
 * to set bit in @data->mask. That is, if @data->mask&(1<<i)==0 then
 * @data->val[i] is undefined.
 *
 * Returns -EBUSY on transient error and -EIO on abnormal condition.
 * Caller must hold controller lock.
 */
int thinkpad_ec_read_row(const struct thinkpad_ec_row *args,
			 struct thinkpad_ec_row *data)
{
	int retries, ret;

	if (thinkpad_ec_is_row_fetched(args))
		goto read_row; /* already requested */

	/* Request the row */
	for (retries = 0; retries < TPC_READ_RETRIES; ++retries) {
		ret = thinkpad_ec_request_row(args);
		if (!ret)
			goto read_row;
		if (ret != -EBUSY)
			break;
		ndelay(TPC_READ_NDELAY);
	}
	printk(KERN_ERR REQ_FMT("failed requesting row", ret));
	goto out;

read_row:
	/* Read the row's data */
	for (retries = 0; retries < TPC_READ_RETRIES; ++retries) {
		ret = thinkpad_ec_read_data(args, data);
		if (!ret)
			goto out;
		if (ret != -EBUSY)
			break;
		ndelay(TPC_READ_NDELAY);
	}

	printk(KERN_ERR REQ_FMT("failed waiting for data", ret));

out:
	prefetch_jiffies = TPC_PREFETCH_JUNK;
	return ret;
}
EXPORT_SYMBOL_GPL(thinkpad_ec_read_row);

/**
 * thinkpad_ec_try_read_row - try reading prefetched data from ThinkPad EC
 * @args Input register arguments
 * @data Output register values
 *
 * Try reading a data row from the ThinkPad embedded controller LPC3
 * interface, if this raw was recently prefetched using
 * thinkpad_ec_prefetch_row(). Does not fetch, retry or block.
 * The parameters have the same meaning as in thinkpad_ec_read_row().
 *
 * Returns -EBUSY is data not ready and -ENODATA if row not prefetched.
 * Caller must hold controller lock.
 */
int thinkpad_ec_try_read_row(const struct thinkpad_ec_row *args,
			     struct thinkpad_ec_row *data)
{
	int ret;
	if (!thinkpad_ec_is_row_fetched(args)) {
		ret = -ENODATA;
	} else {
		ret = thinkpad_ec_read_data(args, data);
		if (!ret)
			prefetch_jiffies = TPC_PREFETCH_NONE; /* eaten up */
	}
	return ret;
}
EXPORT_SYMBOL_GPL(thinkpad_ec_try_read_row);

/**
 * thinkpad_ec_prefetch_row - prefetch data from ThinkPad EC
 * @args Input register arguments
 *
 * Prefetch a data row from the ThinkPad embedded controller LCP3
 * interface. A subsequent call to thinkpad_ec_read_row() with the
 * same arguments will be faster, and a subsequent call to
 * thinkpad_ec_try_read_row() stands a good chance of succeeding if
 * done neither too soon nor too late. See
 * thinkpad_ec_read_row() for the meaning of @args.
 *
 * Returns -EBUSY on transient error and -EIO on abnormal condition.
 * Caller must hold controller lock.
 */
int thinkpad_ec_prefetch_row(const struct thinkpad_ec_row *args)
{
	int ret;
	ret = thinkpad_ec_request_row(args);
	if (ret) {
		prefetch_jiffies = TPC_PREFETCH_JUNK;
	} else {
		prefetch_jiffies = get_jiffies_64();
		prefetch_arg0 = args->val[0x0];
		prefetch_argF = args->val[0xF];
	}
	return ret;
}
EXPORT_SYMBOL_GPL(thinkpad_ec_prefetch_row);

/**
 * thinkpad_ec_invalidate - invalidate prefetched ThinkPad EC data
 *
 * Invalidate the data prefetched via thinkpad_ec_prefetch_row() from the
 * ThinkPad embedded controller LPC3 interface.
 * Must be called before unlocking by any code that accesses the controller
 * ports directly.
 */
void thinkpad_ec_invalidate(void)
{
	prefetch_jiffies = TPC_PREFETCH_JUNK;
}
EXPORT_SYMBOL_GPL(thinkpad_ec_invalidate);


/*** Checking for EC hardware ***/

/**
 * thinkpad_ec_test - verify the EC is present and follows protocol
 *
 * Ensure the EC LPC3 channel really works on this machine by making
 * an EC request and seeing if the EC follows the documented H8S protocol.
 * The requested row just reads battery status, so it should be harmless to
 * access it (on a correct EC).
 * This test writes to IO ports, so execute only after checking DMI.
 */
static int __init thinkpad_ec_test(void)
{
	int ret;
	const struct thinkpad_ec_row args = /* battery 0 basic status */
	  { .mask = 0x8001, .val = {0x01,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0x00} };
	struct thinkpad_ec_row data = { .mask = 0x0000 };
	ret = thinkpad_ec_lock();
	if (ret)
		return ret;
	ret = thinkpad_ec_read_row(&args, &data);
	thinkpad_ec_unlock();
	return ret;
}

/* Search all DMI device names of a given type for a substring */
static int __init dmi_find_substring(int type, const char *substr)
{
	const struct dmi_device *dev = NULL;
	while ((dev = dmi_find_device(type, NULL, dev))) {
		if (strstr(dev->name, substr))
			return 1;
	}
	return 0;
}

#define TP_DMI_MATCH(vendor,model)	{		\
	.ident = vendor " " model,			\
	.matches = {					\
		DMI_MATCH(DMI_BOARD_VENDOR, vendor),	\
		DMI_MATCH(DMI_PRODUCT_VERSION, model)	\
	}						\
}

/* Check DMI for existence of ThinkPad embedded controller */
static int __init check_dmi_for_ec(void)
{
	/* A few old models that have a good EC but don't report it in DMI */
	struct dmi_system_id tp_whitelist[] = {
		TP_DMI_MATCH("IBM", "ThinkPad A30"),
		TP_DMI_MATCH("IBM", "ThinkPad T23"),
		TP_DMI_MATCH("IBM", "ThinkPad X24"),
		TP_DMI_MATCH("LENOVO", "ThinkPad"),
		{ .ident = NULL }
	};
	return dmi_find_substring(DMI_DEV_TYPE_OEM_STRING,
				  "IBM ThinkPad Embedded Controller") ||
	       dmi_check_system(tp_whitelist);
}

/*** Init and cleanup ***/

static int __init thinkpad_ec_init(void)
{
	if (!check_dmi_for_ec()) {
		printk(KERN_WARNING
		       "thinkpad_ec: no ThinkPad embedded controller!\n");
		return -ENODEV;
	}

	if (request_region(TPC_BASE_PORT, TPC_NUM_PORTS, "thinkpad_ec")) {
		reserved_io = 1;
	} else {
		printk(KERN_ERR "thinkpad_ec: cannot claim IO ports %#x-%#x... ",
		       TPC_BASE_PORT,
		       TPC_BASE_PORT + TPC_NUM_PORTS - 1);
		if (force_io) {
			printk("forcing use of unreserved IO ports.\n");
		} else {
			printk("consider using force_io=1.\n");
			return -ENXIO;
		}
	}
	prefetch_jiffies = TPC_PREFETCH_JUNK;
	if (thinkpad_ec_test()) {
		printk(KERN_ERR "thinkpad_ec: initial ec test failed\n");
		if (reserved_io)
			release_region(TPC_BASE_PORT, TPC_NUM_PORTS);
		return -ENXIO;
	}
	printk(KERN_INFO "thinkpad_ec: thinkpad_ec " TP_VERSION " loaded.\n");
	return 0;
}

static void __exit thinkpad_ec_exit(void)
{
	if (reserved_io)
		release_region(TPC_BASE_PORT, TPC_NUM_PORTS);
	printk(KERN_INFO "thinkpad_ec: unloaded.\n");
}

module_init(thinkpad_ec_init);
module_exit(thinkpad_ec_exit);
