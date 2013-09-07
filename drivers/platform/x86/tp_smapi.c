/*
 *  tp_smapi.c - ThinkPad SMAPI support
 *
 *  This driver exposes some features of the System Management Application
 *  Program Interface (SMAPI) BIOS found on ThinkPad laptops. It works on
 *  models in which the SMAPI BIOS runs in SMM and is invoked by writing
 *  to the APM control port 0xB2.
 *  It also exposes battery status information, obtained from the ThinkPad
 *  embedded controller (via the thinkpad_ec module).
 *  Ancient ThinkPad models use a different interface, supported by the
 *  "thinkpad" module from "tpctl".
 *
 *  Many of the battery status values obtained from the EC simply mirror
 *  values provided by the battery's Smart Battery System (SBS) interface, so
 *  their meaning is defined by the Smart Battery Data Specification (see
 *  http://sbs-forum.org/specs/sbdat110.pdf). References to this SBS spec
 *  are given in the code where relevant.
 *
 *  Copyright (C) 2006 Shem Multinymous <multinymous@gmail.com>.
 *  SMAPI access code based on the mwave driver by Mike Sullivan.
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
#include <linux/init.h>
#include <linux/types.h>
#include <linux/proc_fs.h>
#include <linux/mc146818rtc.h>	/* CMOS defines */
#include <linux/delay.h>
#include <linux/version.h>
#include <linux/thinkpad_ec.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <asm/io.h>

#define TP_VERSION "0.41"
#define TP_DESC "ThinkPad SMAPI Support"
#define TP_DIR "smapi"

MODULE_AUTHOR("Shem Multinymous");
MODULE_DESCRIPTION(TP_DESC);
MODULE_VERSION(TP_VERSION);
MODULE_LICENSE("GPL");

static struct platform_device *pdev;

static int tp_debug;
module_param_named(debug, tp_debug, int, 0600);
MODULE_PARM_DESC(debug, "Debug level (0=off, 1=on)");

/* A few macros for printk()ing: */
#define TPRINTK(level, fmt, args...) \
  dev_printk(level, &(pdev->dev), "%s: " fmt "\n", __func__, ## args)
#define DPRINTK(fmt, args...) \
  do { if (tp_debug) TPRINTK(KERN_DEBUG, fmt, ## args); } while (0)

/*********************************************************************
 * SMAPI interface
 */

/* SMAPI functions (register BX when making the SMM call). */
#define SMAPI_GET_INHIBIT_CHARGE                0x2114
#define SMAPI_SET_INHIBIT_CHARGE                0x2115
#define SMAPI_GET_THRESH_START                  0x2116
#define SMAPI_SET_THRESH_START                  0x2117
#define SMAPI_GET_FORCE_DISCHARGE               0x2118
#define SMAPI_SET_FORCE_DISCHARGE               0x2119
#define SMAPI_GET_THRESH_STOP                   0x211a
#define SMAPI_SET_THRESH_STOP                   0x211b

/* SMAPI error codes (see ThinkPad 770 Technical Reference Manual p.83 at
 http://www-307.ibm.com/pc/support/site.wss/document.do?lndocid=PFAN-3TUQQD */
#define SMAPI_RETCODE_EOF 0xff
static struct { u8 rc; char *msg; int ret; } smapi_retcode[] =
{
	{0x00, "OK", 0},
	{0x53, "SMAPI function is not available", -ENXIO},
	{0x81, "Invalid parameter", -EINVAL},
	{0x86, "Function is not supported by SMAPI BIOS", -EOPNOTSUPP},
	{0x90, "System error", -EIO},
	{0x91, "System is invalid", -EIO},
	{0x92, "System is busy, -EBUSY"},
	{0xa0, "Device error (disk read error)", -EIO},
	{0xa1, "Device is busy", -EBUSY},
	{0xa2, "Device is not attached", -ENXIO},
	{0xa3, "Device is disbled", -EIO},
	{0xa4, "Request parameter is out of range", -EINVAL},
	{0xa5, "Request parameter is not accepted", -EINVAL},
	{0xa6, "Transient error", -EBUSY}, /* ? */
	{SMAPI_RETCODE_EOF, "Unknown error code", -EIO}
};


#define SMAPI_MAX_RETRIES 10
#define SMAPI_PORT2 0x4F           /* fixed port, meaning unclear */
static unsigned short smapi_port;  /* APM control port, normally 0xB2 */

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,37)
static DECLARE_MUTEX(smapi_mutex);
#else
static DEFINE_SEMAPHORE(smapi_mutex);
#endif

/**
 * find_smapi_port - read SMAPI port from NVRAM
 */
static int __init find_smapi_port(void)
{
	u16 smapi_id = 0;
	unsigned short port = 0;
	unsigned long flags;

	spin_lock_irqsave(&rtc_lock, flags);
	smapi_id = CMOS_READ(0x7C);
	smapi_id |= (CMOS_READ(0x7D) << 8);
	spin_unlock_irqrestore(&rtc_lock, flags);

	if (smapi_id != 0x5349) {
		printk(KERN_ERR "SMAPI not supported (ID=0x%x)\n", smapi_id);
		return -ENXIO;
	}
	spin_lock_irqsave(&rtc_lock, flags);
	port = CMOS_READ(0x7E);
	port |= (CMOS_READ(0x7F) << 8);
	spin_unlock_irqrestore(&rtc_lock, flags);
	if (port == 0) {
		printk(KERN_ERR "unable to read SMAPI port number\n");
		return -ENXIO;
	}
	return port;
}

/**
 * smapi_request - make a SMAPI call
 * @inEBX, @inECX, @inEDI, @inESI: input registers
 * @outEBX, @outECX, @outEDX, @outEDI, @outESI: outputs registers
 * @msg: textual error message
 * Invokes the SMAPI SMBIOS with the given input and outpu args.
 * All outputs are optional (can be %NULL).
 * Returns 0 when successful, and a negative errno constant
 * (see smapi_retcode above) upon failure.
 */
static int smapi_request(u32 inEBX, u32 inECX,
			 u32 inEDI, u32 inESI,
			 u32 *outEBX, u32 *outECX, u32 *outEDX,
			 u32 *outEDI, u32 *outESI, const char **msg)
{
	int ret = 0;
	int i;
	int retries;
	u8 rc;
	/* Must use local vars for output regs, due to reg pressure. */
	u32 tmpEAX, tmpEBX, tmpECX, tmpEDX, tmpEDI, tmpESI;

	for (retries = 0; retries < SMAPI_MAX_RETRIES; ++retries) {
		DPRINTK("req_in: BX=%x CX=%x DI=%x SI=%x",
			inEBX, inECX, inEDI, inESI);

		/* SMAPI's SMBIOS call and thinkpad_ec end up using use
		 * different interfaces to the same chip, so play it safe. */
		ret = thinkpad_ec_lock();
		if (ret)
			return ret;

		__asm__ __volatile__(
			"movl  $0x00005380,%%eax\n\t"
			"movl  %6,%%ebx\n\t"
			"movl  %7,%%ecx\n\t"
			"movl  %8,%%edi\n\t"
			"movl  %9,%%esi\n\t"
			"xorl  %%edx,%%edx\n\t"
			"movw  %10,%%dx\n\t"
			"out   %%al,%%dx\n\t"  /* trigger SMI to SMBIOS */
			"out   %%al,$0x4F\n\t"
			"movl  %%eax,%0\n\t"
			"movl  %%ebx,%1\n\t"
			"movl  %%ecx,%2\n\t"
			"movl  %%edx,%3\n\t"
			"movl  %%edi,%4\n\t"
			"movl  %%esi,%5\n\t"
			:"=m"(tmpEAX),
			 "=m"(tmpEBX),
			 "=m"(tmpECX),
			 "=m"(tmpEDX),
			 "=m"(tmpEDI),
			 "=m"(tmpESI)
			:"m"(inEBX), "m"(inECX), "m"(inEDI), "m"(inESI),
			 "m"((u16)smapi_port)
			:"%eax", "%ebx", "%ecx", "%edx", "%edi",
			 "%esi");

		thinkpad_ec_invalidate();
		thinkpad_ec_unlock();

		/* Don't let the next SMAPI access happen too quickly,
		 * may case problems. (We're hold smapi_mutex).       */
		msleep(50);

		if (outEBX) *outEBX = tmpEBX;
		if (outECX) *outECX = tmpECX;
		if (outEDX) *outEDX = tmpEDX;
		if (outESI) *outESI = tmpESI;
		if (outEDI) *outEDI = tmpEDI;

		/* Look up error code */
		rc = (tmpEAX>>8)&0xFF;
		for (i = 0; smapi_retcode[i].rc != SMAPI_RETCODE_EOF &&
			    smapi_retcode[i].rc != rc; ++i) {}
		ret = smapi_retcode[i].ret;
		if (msg)
			*msg = smapi_retcode[i].msg;

		DPRINTK("req_out: AX=%x BX=%x CX=%x DX=%x DI=%x SI=%x r=%d",
			 tmpEAX, tmpEBX, tmpECX, tmpEDX, tmpEDI, tmpESI, ret);
		if (ret)
			TPRINTK(KERN_NOTICE, "SMAPI error: %s (func=%x)",
				smapi_retcode[i].msg, inEBX);

		if (ret != -EBUSY)
			return ret;
	}
	return ret;
}

/* Convenience wrapper: discard output arguments */
static int smapi_write(u32 inEBX, u32 inECX,
		       u32 inEDI, u32 inESI, const char **msg)
{
	return smapi_request(inEBX, inECX, inEDI, inESI,
			     NULL, NULL, NULL, NULL, NULL, msg);
}


/*********************************************************************
 * Specific SMAPI services
 * All of these functions return 0 upon success, and a negative errno
 * constant (see smapi_retcode) on failure.
 */

enum thresh_type {
	THRESH_STOP  = 0, /* the code assumes this is 0 for brevity */
	THRESH_START
};
#define THRESH_NAME(which) ((which == THRESH_START) ? "start" : "stop")

/**
 * __get_real_thresh - read battery charge start/stop threshold from SMAPI
 * @bat:    battery number (0 or 1)
 * @which:  THRESH_START or THRESH_STOP
 * @thresh: 1..99, 0=default 1..99, 0=default (pass this as-is to SMAPI)
 * @outEDI: some additional state that needs to be preserved, meaning unknown
 * @outESI: some additional state that needs to be preserved, meaning unknown
 */
static int __get_real_thresh(int bat, enum thresh_type which, int *thresh,
			     u32 *outEDI, u32 *outESI)
{
	u32 ebx = (which == THRESH_START) ? SMAPI_GET_THRESH_START
					  : SMAPI_GET_THRESH_STOP;
	u32 ecx = (bat+1)<<8;
	const char *msg;
	int ret = smapi_request(ebx, ecx, 0, 0, NULL,
				&ecx, NULL, outEDI, outESI, &msg);
	if (ret) {
		TPRINTK(KERN_NOTICE, "cannot get %s_thresh of bat=%d: %s",
			THRESH_NAME(which), bat, msg);
		return ret;
	}
	if (!(ecx&0x00000100)) {
		TPRINTK(KERN_NOTICE, "cannot get %s_thresh of bat=%d: ecx=0%x",
			THRESH_NAME(which), bat, ecx);
		return -EIO;
	}
	if (thresh)
		*thresh = ecx&0xFF;
	return 0;
}

/**
 * get_real_thresh - read battery charge start/stop threshold from SMAPI
 * @bat:    battery number (0 or 1)
 * @which:  THRESH_START or THRESH_STOP
 * @thresh: 1..99, 0=default (passes as-is to SMAPI)
 */
static int get_real_thresh(int bat, enum thresh_type which, int *thresh)
{
	return __get_real_thresh(bat, which, thresh, NULL, NULL);
}

/**
 * set_real_thresh - write battery start/top charge threshold to SMAPI
 * @bat:    battery number (0 or 1)
 * @which:  THRESH_START or THRESH_STOP
 * @thresh: 1..99, 0=default (passes as-is to SMAPI)
 */
static int set_real_thresh(int bat, enum thresh_type which, int thresh)
{
	u32 ebx = (which == THRESH_START) ? SMAPI_SET_THRESH_START
					  : SMAPI_SET_THRESH_STOP;
	u32 ecx = ((bat+1)<<8) + thresh;
	u32 getDI, getSI;
	const char *msg;
	int ret;

	/* verify read before writing */
	ret = __get_real_thresh(bat, which, NULL, &getDI, &getSI);
	if (ret)
		return ret;

	ret = smapi_write(ebx, ecx, getDI, getSI, &msg);
	if (ret)
		TPRINTK(KERN_NOTICE, "set %s to %d for bat=%d failed: %s",
			THRESH_NAME(which), thresh, bat, msg);
	else
		TPRINTK(KERN_INFO, "set %s to %d for bat=%d",
			THRESH_NAME(which), thresh, bat);
	return ret;
}

/**
 * __get_inhibit_charge_minutes - get inhibit charge period from SMAPI
 * @bat:     battery number (0 or 1)
 * @minutes: period in minutes (1..65535 minutes, 0=disabled)
 * @outECX: some additional state that needs to be preserved, meaning unknown
 * Note that @minutes is the originally set value, it does not count down.
 */
static int __get_inhibit_charge_minutes(int bat, int *minutes, u32 *outECX)
{
	u32 ecx = (bat+1)<<8;
	u32 esi;
	const char *msg;
	int ret = smapi_request(SMAPI_GET_INHIBIT_CHARGE, ecx, 0, 0,
				NULL, &ecx, NULL, NULL, &esi, &msg);
	if (ret) {
		TPRINTK(KERN_NOTICE, "failed for bat=%d: %s", bat, msg);
		return ret;
	}
	if (!(ecx&0x0100)) {
		TPRINTK(KERN_NOTICE, "bad ecx=0x%x for bat=%d", ecx, bat);
		return -EIO;
	}
	if (minutes)
		*minutes = (ecx&0x0001)?esi:0;
	if (outECX)
		*outECX = ecx;
	return 0;
}

/**
 * get_inhibit_charge_minutes - get inhibit charge period from SMAPI
 * @bat:     battery number (0 or 1)
 * @minutes: period in minutes (1..65535 minutes, 0=disabled)
 * Note that @minutes is the originally set value, it does not count down.
 */
static int get_inhibit_charge_minutes(int bat, int *minutes)
{
	return __get_inhibit_charge_minutes(bat, minutes, NULL);
}

/**
 * set_inhibit_charge_minutes - write inhibit charge period to SMAPI
 * @bat:     battery number (0 or 1)
 * @minutes: period in minutes (1..65535 minutes, 0=disabled)
 */
static int set_inhibit_charge_minutes(int bat, int minutes)
{
	u32 ecx;
	const char *msg;
	int ret;

	/* verify read before writing */
	ret = __get_inhibit_charge_minutes(bat, NULL, &ecx);
	if (ret)
		return ret;

	ecx = ((bat+1)<<8) | (ecx&0x00FE) | (minutes > 0 ? 0x0001 : 0x0000);
	if (minutes > 0xFFFF)
		minutes = 0xFFFF;
	ret = smapi_write(SMAPI_SET_INHIBIT_CHARGE, ecx, 0, minutes, &msg);
	if (ret)
		TPRINTK(KERN_NOTICE,
			"set to %d failed for bat=%d: %s", minutes, bat, msg);
	else
		TPRINTK(KERN_INFO, "set to %d for bat=%d\n", minutes, bat);
	return ret;
}


/**
 * get_force_discharge - get status of forced discharging from SMAPI
 * @bat:     battery number (0 or 1)
 * @enabled: 1 if forced discharged is enabled, 0 if not
 */
static int get_force_discharge(int bat, int *enabled)
{
	u32 ecx = (bat+1)<<8;
	const char *msg;
	int ret = smapi_request(SMAPI_GET_FORCE_DISCHARGE, ecx, 0, 0,
				NULL, &ecx, NULL, NULL, NULL, &msg);
	if (ret) {
		TPRINTK(KERN_NOTICE, "failed for bat=%d: %s", bat, msg);
		return ret;
	}
	*enabled = (!(ecx&0x00000100) && (ecx&0x00000001))?1:0;
	return 0;
}

/**
 * set_force_discharge - write status of forced discharging to SMAPI
 * @bat:     battery number (0 or 1)
 * @enabled: 1 if forced discharged is enabled, 0 if not
 */
static int set_force_discharge(int bat, int enabled)
{
	u32 ecx = (bat+1)<<8;
	const char *msg;
	int ret = smapi_request(SMAPI_GET_FORCE_DISCHARGE, ecx, 0, 0,
				NULL, &ecx, NULL, NULL, NULL, &msg);
	if (ret) {
		TPRINTK(KERN_NOTICE, "get failed for bat=%d: %s", bat, msg);
		return ret;
	}
	if (ecx&0x00000100) {
		TPRINTK(KERN_NOTICE, "cannot force discharge bat=%d", bat);
		return -EIO;
	}

	ecx = ((bat+1)<<8) | (ecx&0x000000FA) | (enabled?0x00000001:0);
	ret = smapi_write(SMAPI_SET_FORCE_DISCHARGE, ecx, 0, 0, &msg);
	if (ret)
		TPRINTK(KERN_NOTICE, "set to %d failed for bat=%d: %s",
			enabled, bat, msg);
	else
		TPRINTK(KERN_INFO, "set to %d for bat=%d", enabled, bat);
	return ret;
}


/*********************************************************************
 * Wrappers to threshold-related SMAPI functions, which handle default
 * thresholds and related quirks.
 */

/* Minimum, default and minimum difference for battery charging thresholds: */
#define MIN_THRESH_DELTA      4  /* Min delta between start and stop thresh */
#define MIN_THRESH_START      2
#define MAX_THRESH_START      (100-MIN_THRESH_DELTA)
#define MIN_THRESH_STOP       (MIN_THRESH_START + MIN_THRESH_DELTA)
#define MAX_THRESH_STOP       100
#define DEFAULT_THRESH_START  MAX_THRESH_START
#define DEFAULT_THRESH_STOP   MAX_THRESH_STOP

/* The GUI of IBM's Battery Maximizer seems to show a start threshold that
 * is 1 more than the value we set/get via SMAPI. Since the threshold is
 * maintained across reboot, this can be confusing. So we kludge our
 * interface for interoperability: */
#define BATMAX_FIX   1

/* Get charge start/stop threshold (1..100),
 * substituting default values if needed and applying BATMAT_FIX. */
static int get_thresh(int bat, enum thresh_type which, int *thresh)
{
	int ret = get_real_thresh(bat, which, thresh);
	if (ret)
		return ret;
	if (*thresh == 0)
		*thresh = (which == THRESH_START) ? DEFAULT_THRESH_START
						  : DEFAULT_THRESH_STOP;
	else if (which == THRESH_START)
		*thresh += BATMAX_FIX;
	return 0;
}


/* Set charge start/stop threshold (1..100),
 * substituting default values if needed and applying BATMAT_FIX. */
static int set_thresh(int bat, enum thresh_type which, int thresh)
{
	if (which == THRESH_STOP && thresh == DEFAULT_THRESH_STOP)
		thresh = 0; /* 100 is out of range, but default means 100 */
	if (which == THRESH_START)
		thresh -= BATMAX_FIX;
	return set_real_thresh(bat, which, thresh);
}

/*********************************************************************
 * ThinkPad embedded controller readout and basic functions
 */

/**
 * read_tp_ec_row - read data row from the ThinkPad embedded controller
 * @arg0: EC command code
 * @bat: battery number, 0 or 1
 * @j: the byte value to be used for "junk" (unused) input/outputs
 * @dataval: result vector
 */
static int read_tp_ec_row(u8 arg0, int bat, u8 j, u8 *dataval)
{
	int ret;
	const struct thinkpad_ec_row args = { .mask = 0xFFFF,
		.val = {arg0, j,j,j,j,j,j,j,j,j,j,j,j,j,j, (u8)bat} };
	struct thinkpad_ec_row data = { .mask = 0xFFFF };

	ret = thinkpad_ec_lock();
	if (ret)
		return ret;
	ret = thinkpad_ec_read_row(&args, &data);
	thinkpad_ec_unlock();
	memcpy(dataval, &data.val, TP_CONTROLLER_ROW_LEN);
	return ret;
}

/**
 * power_device_present - check for presence of battery or AC power
 * @bat: 0 for battery 0, 1 for battery 1, otherwise AC power
 * Returns 1 if present, 0 if not present, negative if error.
 */
static int power_device_present(int bat)
{
	u8 row[TP_CONTROLLER_ROW_LEN];
	u8 test;
	int ret = read_tp_ec_row(1, bat, 0, row);
	if (ret)
		return ret;
	switch (bat) {
	case 0:  test = 0x40; break; /* battery 0 */
	case 1:  test = 0x20; break; /* battery 1 */
	default: test = 0x80;        /* AC power */
	}
	return (row[0] & test) ? 1 : 0;
}

/**
 * bat_has_status - check if battery can report detailed status
 * @bat: 0 for battery 0, 1 for battery 1
 * Returns 1 if yes, 0 if no, negative if error.
 */
static int bat_has_status(int bat)
{
	u8 row[TP_CONTROLLER_ROW_LEN];
	int ret = read_tp_ec_row(1, bat, 0, row);
	if (ret)
		return ret;
	if ((row[0] & (bat?0x20:0x40)) == 0) /* no battery */
		return 0;
	if ((row[1] & (0x60)) == 0) /* no status */
		return 0;
	return 1;
}

/**
 * get_tp_ec_bat_16 - read a 16-bit value from EC battery status data
 * @arg0: first argument to EC
 * @off: offset in row returned from EC
 * @bat: battery (0 or 1)
 * @val: the 16-bit value obtained
 * Returns nonzero on error.
 */
static int get_tp_ec_bat_16(u8 arg0, int offset, int bat, u16 *val)
{
	u8 row[TP_CONTROLLER_ROW_LEN];
	int ret;
	if (bat_has_status(bat) != 1)
		return -ENXIO;
	ret = read_tp_ec_row(arg0, bat, 0, row);
	if (ret)
		return ret;
	*val = *(u16 *)(row+offset);
	return 0;
}

/*********************************************************************
 * sysfs attributes for batteries -
 * definitions and helper functions
 */

/* A custom device attribute struct which holds a battery number */
struct bat_device_attribute {
	struct device_attribute dev_attr;
	int bat;
};

/**
 * attr_get_bat - get the battery to which the attribute belongs
 */
static int attr_get_bat(struct device_attribute *attr)
{
	return container_of(attr, struct bat_device_attribute, dev_attr)->bat;
}

/**
 * show_tp_ec_bat_u16 - show an unsigned 16-bit battery attribute
 * @arg0: specified 1st argument of EC raw to read
 * @offset: byte offset in EC raw data
 * @mul: correction factor to multiply by
 * @na_msg: string to output is value not available (0xFFFFFFFF)
 * @attr: battery attribute
 * @buf: output buffer
 * The 16-bit value is read from the EC, treated as unsigned,
 * transformed as x->mul*x, and printed to the buffer.
 * If the value is 0xFFFFFFFF and na_msg!=%NULL, na_msg is printed instead.
 */
static ssize_t show_tp_ec_bat_u16(u8 arg0, int offset, int mul,
			      const char *na_msg,
			      struct device_attribute *attr, char *buf)
{
	u16 val;
	int ret = get_tp_ec_bat_16(arg0, offset, attr_get_bat(attr), &val);
	if (ret)
		return ret;
	if (na_msg && val == 0xFFFF)
		return sprintf(buf, "%s\n", na_msg);
	else
		return sprintf(buf, "%u\n", mul*(unsigned int)val);
}

/**
 * show_tp_ec_bat_s16 - show an signed 16-bit battery attribute
 * @arg0: specified 1st argument of EC raw to read
 * @offset: byte offset in EC raw data
 * @mul: correction factor to multiply by
 * @add: correction term to add after multiplication
 * @attr: battery attribute
 * @buf: output buffer
 * The 16-bit value is read from the EC, treated as signed,
 * transformed as x->mul*x+add, and printed to the buffer.
 */
static ssize_t show_tp_ec_bat_s16(u8 arg0, int offset, int mul, int add,
			      struct device_attribute *attr, char *buf)
{
	u16 val;
	int ret = get_tp_ec_bat_16(arg0, offset, attr_get_bat(attr), &val);
	if (ret)
		return ret;
	return sprintf(buf, "%d\n", mul*(s16)val+add);
}

/**
 * show_tp_ec_bat_str - show a string from EC battery status data
 * @arg0: specified 1st argument of EC raw to read
 * @offset: byte offset in EC raw data
 * @maxlen: maximum string length
 * @attr: battery attribute
 * @buf: output buffer
 */
static ssize_t show_tp_ec_bat_str(u8 arg0, int offset, int maxlen,
			      struct device_attribute *attr, char *buf)
{
	int bat = attr_get_bat(attr);
	u8 row[TP_CONTROLLER_ROW_LEN];
	int ret;
	if (bat_has_status(bat) != 1)
		return -ENXIO;
	ret = read_tp_ec_row(arg0, bat, 0, row);
	if (ret)
		return ret;
	strncpy(buf, (char *)row+offset, maxlen);
	buf[maxlen] = 0;
	strcat(buf, "\n");
	return strlen(buf);
}

/**
 * show_tp_ec_bat_power - show a power readout from EC battery status data
 * @arg0: specified 1st argument of EC raw to read
 * @offV: byte offset of voltage in EC raw data
 * @offI: byte offset of current in EC raw data
 * @attr: battery attribute
 * @buf: output buffer
 * Computes the power as current*voltage from the two given readout offsets.
 */
static ssize_t show_tp_ec_bat_power(u8 arg0, int offV, int offI,
				struct device_attribute *attr, char *buf)
{
	u8 row[TP_CONTROLLER_ROW_LEN];
	int milliamp, millivolt, ret;
	int bat = attr_get_bat(attr);
	if (bat_has_status(bat) != 1)
		return -ENXIO;
	ret = read_tp_ec_row(1, bat, 0, row);
	if (ret)
		return ret;
	millivolt = *(u16 *)(row+offV);
	milliamp = *(s16 *)(row+offI);
	return sprintf(buf, "%d\n", milliamp*millivolt/1000); /* units: mW */
}

/**
 * show_tp_ec_bat_date - decode and show a date from EC battery status data
 * @arg0: specified 1st argument of EC raw to read
 * @offset: byte offset in EC raw data
 * @attr: battery attribute
 * @buf: output buffer
 */
static ssize_t show_tp_ec_bat_date(u8 arg0, int offset,
			       struct device_attribute *attr, char *buf)
{
	u8 row[TP_CONTROLLER_ROW_LEN];
	u16 v;
	int ret;
	int day, month, year;
	int bat = attr_get_bat(attr);
	if (bat_has_status(bat) != 1)
		return -ENXIO;
	ret = read_tp_ec_row(arg0, bat, 0, row);
	if (ret)
		return ret;

	/* Decode bit-packed: v = day | (month<<5) | ((year-1980)<<9) */
	v = *(u16 *)(row+offset);
	day = v & 0x1F;
	month = (v >> 5) & 0xF;
	year = (v >> 9) + 1980;

	return sprintf(buf, "%04d-%02d-%02d\n", year, month, day);
}


/*********************************************************************
 * sysfs attribute I/O for batteries -
 * the actual attribute show/store functions
 */

static ssize_t show_battery_start_charge_thresh(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int thresh;
	int bat = attr_get_bat(attr);
	int ret = get_thresh(bat, THRESH_START, &thresh);
	if (ret)
		return ret;
	return sprintf(buf, "%d\n", thresh);  /* units: percent */
}

static ssize_t show_battery_stop_charge_thresh(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int thresh;
	int bat = attr_get_bat(attr);
	int ret = get_thresh(bat, THRESH_STOP, &thresh);
	if (ret)
		return ret;
	return sprintf(buf, "%d\n", thresh);  /* units: percent */
}

/**
 * store_battery_start_charge_thresh - store battery_start_charge_thresh attr
 * Since this is a kernel<->user interface, we ensure a valid state for
 * the hardware. We do this by clamping the requested threshold to the
 * valid range and, if necessary, moving the other threshold so that
 * it's MIN_THRESH_DELTA away from this one.
 */
static ssize_t store_battery_start_charge_thresh(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int thresh, other_thresh, ret;
	int bat = attr_get_bat(attr);

	if (sscanf(buf, "%d", &thresh) != 1 || thresh < 1 || thresh > 100)
		return -EINVAL;

	if (thresh < MIN_THRESH_START) /* clamp up to MIN_THRESH_START */
		thresh = MIN_THRESH_START;
	if (thresh > MAX_THRESH_START) /* clamp down to MAX_THRESH_START */
		thresh = MAX_THRESH_START;

	down(&smapi_mutex);
	ret = get_thresh(bat, THRESH_STOP, &other_thresh);
	if (ret != -EOPNOTSUPP && ret != -ENXIO) {
		if (ret) /* other threshold is set? */
			goto out;
		ret = get_real_thresh(bat, THRESH_START, NULL);
		if (ret) /* this threshold is set? */
			goto out;
		if (other_thresh < thresh+MIN_THRESH_DELTA) {
			/* move other thresh to keep it above this one */
			ret = set_thresh(bat, THRESH_STOP,
					 thresh+MIN_THRESH_DELTA);
			if (ret)
				goto out;
		}
	}
	ret = set_thresh(bat, THRESH_START, thresh);
out:
	up(&smapi_mutex);
	return count;

}

/**
 * store_battery_stop_charge_thresh - store battery_stop_charge_thresh attr
 * Since this is a kernel<->user interface, we ensure a valid state for
 * the hardware. We do this by clamping the requested threshold to the
 * valid range and, if necessary, moving the other threshold so that
 * it's MIN_THRESH_DELTA away from this one.
 */
static ssize_t store_battery_stop_charge_thresh(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int thresh, other_thresh, ret;
	int bat = attr_get_bat(attr);

	if (sscanf(buf, "%d", &thresh) != 1 || thresh < 1 || thresh > 100)
		return -EINVAL;

	if (thresh < MIN_THRESH_STOP) /* clamp up to MIN_THRESH_STOP */
		thresh = MIN_THRESH_STOP;

	down(&smapi_mutex);
	ret = get_thresh(bat, THRESH_START, &other_thresh);
	if (ret != -EOPNOTSUPP && ret != -ENXIO) { /* other threshold exists? */
		if (ret)
			goto out;
		/* this threshold exists? */
		ret = get_real_thresh(bat, THRESH_STOP, NULL);
		if (ret)
			goto out;
		if (other_thresh >= thresh-MIN_THRESH_DELTA) {
			 /* move other thresh to be below this one */
			ret = set_thresh(bat, THRESH_START,
					 thresh-MIN_THRESH_DELTA);
			if (ret)
				goto out;
		}
	}
	ret = set_thresh(bat, THRESH_STOP, thresh);
out:
	up(&smapi_mutex);
	return count;
}

static ssize_t show_battery_inhibit_charge_minutes(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int minutes;
	int bat = attr_get_bat(attr);
	int ret = get_inhibit_charge_minutes(bat, &minutes);
	if (ret)
		return ret;
	return sprintf(buf, "%d\n", minutes);  /* units: minutes */
}

static ssize_t store_battery_inhibit_charge_minutes(struct device *dev,
				struct device_attribute *attr,
				const char *buf, size_t count)
{
	int ret;
	int minutes;
	int bat = attr_get_bat(attr);
	if (sscanf(buf, "%d", &minutes) != 1 || minutes < 0) {
		TPRINTK(KERN_ERR, "inhibit_charge_minutes: "
			      "must be a non-negative integer");
		return -EINVAL;
	}
	ret = set_inhibit_charge_minutes(bat, minutes);
	if (ret)
		return ret;
	return count;
}

static ssize_t show_battery_force_discharge(struct device *dev,
	struct device_attribute *attr, char *buf)
{
	int enabled;
	int bat = attr_get_bat(attr);
	int ret = get_force_discharge(bat, &enabled);
	if (ret)
		return ret;
	return sprintf(buf, "%d\n", enabled);  /* type: boolean */
}

static ssize_t store_battery_force_discharge(struct device *dev,
	struct device_attribute *attr, const char *buf, size_t count)
{
	int ret;
	int enabled;
	int bat = attr_get_bat(attr);
	if (sscanf(buf, "%d", &enabled) != 1 || enabled < 0 || enabled > 1)
		return -EINVAL;
	ret = set_force_discharge(bat, enabled);
	if (ret)
		return ret;
	return count;
}

static ssize_t show_battery_installed(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	int bat = attr_get_bat(attr);
	int ret = power_device_present(bat);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", ret); /* type: boolean */
}

static ssize_t show_battery_state(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	u8 row[TP_CONTROLLER_ROW_LEN];
	const char *txt;
	int ret;
	int bat = attr_get_bat(attr);
	if (bat_has_status(bat) != 1)
		return sprintf(buf, "none\n");
	ret = read_tp_ec_row(1, bat, 0, row);
	if (ret)
		return ret;
	switch (row[1] & 0xf0) {
	case 0xc0: txt = "idle"; break;
	case 0xd0: txt = "discharging"; break;
	case 0xe0: txt = "charging"; break;
	default:   return sprintf(buf, "unknown (0x%x)\n", row[1]);
	}
	return sprintf(buf, "%s\n", txt);  /* type: string from fixed set */
}

static ssize_t show_battery_manufacturer(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* type: string. SBS spec v1.1 p34: ManufacturerName() */
	return show_tp_ec_bat_str(4, 2, TP_CONTROLLER_ROW_LEN-2, attr, buf);
}

static ssize_t show_battery_model(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* type: string. SBS spec v1.1 p34: DeviceName() */
	return show_tp_ec_bat_str(5, 2, TP_CONTROLLER_ROW_LEN-2, attr, buf);
}

static ssize_t show_battery_barcoding(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* type: string */
	return show_tp_ec_bat_str(7, 2, TP_CONTROLLER_ROW_LEN-2, attr, buf);
}

static ssize_t show_battery_chemistry(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* type: string. SBS spec v1.1 p34-35: DeviceChemistry() */
	return show_tp_ec_bat_str(6, 2, 5, attr, buf);
}

static ssize_t show_battery_voltage(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: mV. SBS spec v1.1 p24: Voltage() */
	return show_tp_ec_bat_u16(1, 6, 1, NULL, attr, buf);
}

static ssize_t show_battery_design_voltage(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: mV. SBS spec v1.1 p32: DesignVoltage() */
	return show_tp_ec_bat_u16(3, 4, 1, NULL, attr, buf);
}

static ssize_t show_battery_charging_max_voltage(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: mV. SBS spec v1.1 p37,39: ChargingVoltage() */
	return show_tp_ec_bat_u16(9, 8, 1, NULL, attr, buf);
}

static ssize_t show_battery_group0_voltage(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: mV */
	return show_tp_ec_bat_u16(0xA, 12, 1, NULL, attr, buf);
}

static ssize_t show_battery_group1_voltage(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: mV */
	return show_tp_ec_bat_u16(0xA, 10, 1, NULL, attr, buf);
}

static ssize_t show_battery_group2_voltage(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: mV */
	return show_tp_ec_bat_u16(0xA, 8, 1, NULL, attr, buf);
}

static ssize_t show_battery_group3_voltage(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: mV */
	return show_tp_ec_bat_u16(0xA, 6, 1, NULL, attr, buf);
}

static ssize_t show_battery_current_now(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: mA. SBS spec v1.1 p24: Current() */
	return show_tp_ec_bat_s16(1, 8, 1, 0, attr, buf);
}

static ssize_t show_battery_current_avg(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: mA. SBS spec v1.1 p24: AverageCurrent() */
	return show_tp_ec_bat_s16(1, 10, 1, 0, attr, buf);
}

static ssize_t show_battery_charging_max_current(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: mA. SBS spec v1.1 p36,38: ChargingCurrent() */
	return show_tp_ec_bat_s16(9, 6, 1, 0, attr, buf);
}

static ssize_t show_battery_power_now(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: mW. SBS spec v1.1: Voltage()*Current() */
	return show_tp_ec_bat_power(1, 6, 8, attr, buf);
}

static ssize_t show_battery_power_avg(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: mW. SBS spec v1.1: Voltage()*AverageCurrent() */
	return show_tp_ec_bat_power(1, 6, 10, attr, buf);
}

static ssize_t show_battery_remaining_percent(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: percent. SBS spec v1.1 p25: RelativeStateOfCharge() */
	return show_tp_ec_bat_u16(1, 12, 1, NULL, attr, buf);
}

static ssize_t show_battery_remaining_percent_error(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: percent. SBS spec v1.1 p25: MaxError() */
	return show_tp_ec_bat_u16(9, 4, 1, NULL, attr, buf);
}

static ssize_t show_battery_remaining_charging_time(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: minutes. SBS spec v1.1 p27: AverageTimeToFull() */
	return show_tp_ec_bat_u16(2, 8, 1, "not_charging", attr, buf);
}

static ssize_t show_battery_remaining_running_time(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: minutes. SBS spec v1.1 p27: RunTimeToEmpty() */
	return show_tp_ec_bat_u16(2, 6, 1, "not_discharging", attr, buf);
}

static ssize_t show_battery_remaining_running_time_now(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: minutes. SBS spec v1.1 p27: RunTimeToEmpty() */
	return show_tp_ec_bat_u16(2, 4, 1, "not_discharging", attr, buf);
}

static ssize_t show_battery_remaining_capacity(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: mWh. SBS spec v1.1 p26. */
	return show_tp_ec_bat_u16(1, 14, 10, "", attr, buf);
}

static ssize_t show_battery_last_full_capacity(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: mWh. SBS spec v1.1 p26: FullChargeCapacity() */
	return show_tp_ec_bat_u16(2, 2, 10, "", attr, buf);
}

static ssize_t show_battery_design_capacity(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: mWh. SBS spec v1.1 p32: DesignCapacity() */
	return show_tp_ec_bat_u16(3, 2, 10, "", attr, buf);
}

static ssize_t show_battery_cycle_count(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: ordinal. SBS spec v1.1 p32: CycleCount() */
	return show_tp_ec_bat_u16(2, 12, 1, "", attr, buf);
}

static ssize_t show_battery_temperature(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* units: millicelsius. SBS spec v1.1: Temperature()*10 */
	return show_tp_ec_bat_s16(1, 4, 100, -273100, attr, buf);
}

static ssize_t show_battery_serial(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* type: int. SBS spec v1.1 p34: SerialNumber() */
	return show_tp_ec_bat_u16(3, 10, 1, "", attr, buf);
}

static ssize_t show_battery_manufacture_date(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* type: YYYY-MM-DD. SBS spec v1.1 p34: ManufactureDate() */
	return show_tp_ec_bat_date(3, 8, attr, buf);
}

static ssize_t show_battery_first_use_date(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	/* type: YYYY-MM-DD */
	return show_tp_ec_bat_date(8, 2, attr, buf);
}

/**
 * show_battery_dump - show the battery's dump attribute
 * The dump attribute gives a hex dump of all EC readouts related to a
 * battery. Some of the enumerated values don't really exist (i.e., the
 * EC function just leaves them untouched); we use a kludge to detect and
 * denote these.
 */
#define MIN_DUMP_ARG0 0x00
#define MAX_DUMP_ARG0 0x0a /* 0x0b is useful too but hangs old EC firmware */
static ssize_t show_battery_dump(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	int i;
	char *p = buf;
	int bat = attr_get_bat(attr);
	u8 arg0; /* first argument to EC */
	u8 rowa[TP_CONTROLLER_ROW_LEN],
	   rowb[TP_CONTROLLER_ROW_LEN];
	const u8 junka = 0xAA,
		 junkb = 0x55; /* junk values for testing changes */
	int ret;

	for (arg0 = MIN_DUMP_ARG0; arg0 <= MAX_DUMP_ARG0; ++arg0) {
		if ((p-buf) > PAGE_SIZE-TP_CONTROLLER_ROW_LEN*5)
			return -ENOMEM; /* don't overflow sysfs buf */
		/* Read raw twice with different junk values,
		 * to detect unused output bytes which are left unchaged: */
		ret = read_tp_ec_row(arg0, bat, junka, rowa);
		if (ret)
			return ret;
		ret = read_tp_ec_row(arg0, bat, junkb, rowb);
		if (ret)
			return ret;
		for (i = 0; i < TP_CONTROLLER_ROW_LEN; i++) {
			if (rowa[i] == junka && rowb[i] == junkb)
				p += sprintf(p, "-- "); /* unused by EC */
			else
				p += sprintf(p, "%02x ", rowa[i]);
		}
		p += sprintf(p, "\n");
	}
	return p-buf;
}


/*********************************************************************
 * sysfs attribute I/O, other than batteries
 */

static ssize_t show_ac_connected(
	struct device *dev, struct device_attribute *attr, char *buf)
{
	int ret = power_device_present(0xFF);
	if (ret < 0)
		return ret;
	return sprintf(buf, "%d\n", ret);  /* type: boolean */
}

/*********************************************************************
 * The the "smapi_request" sysfs attribute executes a raw SMAPI call.
 * You write to make a request and read to get the result. The state
 * is saved globally rather than per fd (sysfs limitation), so
 * simultaenous requests may get each other's results! So this is for
 * development and debugging only.
 */
#define MAX_SMAPI_ATTR_ANSWER_LEN   128
static char smapi_attr_answer[MAX_SMAPI_ATTR_ANSWER_LEN] = "";

static ssize_t show_smapi_request(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	int ret = snprintf(buf, PAGE_SIZE, "%s", smapi_attr_answer);
	smapi_attr_answer[0] = '\0';
	return ret;
}

static ssize_t store_smapi_request(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	unsigned int inEBX, inECX, inEDI, inESI;
	u32 outEBX, outECX, outEDX, outEDI, outESI;
	const char *msg;
	int ret;
	if (sscanf(buf, "%x %x %x %x", &inEBX, &inECX, &inEDI, &inESI) != 4) {
		smapi_attr_answer[0] = '\0';
		return -EINVAL;
	}
	ret = smapi_request(
		   inEBX, inECX, inEDI, inESI,
		   &outEBX, &outECX, &outEDX, &outEDI, &outESI, &msg);
	snprintf(smapi_attr_answer, MAX_SMAPI_ATTR_ANSWER_LEN,
		 "%x %x %x %x %x %d '%s'\n",
		 (unsigned int)outEBX, (unsigned int)outECX,
		 (unsigned int)outEDX, (unsigned int)outEDI,
		 (unsigned int)outESI, ret, msg);
	if (ret)
		return ret;
	else
		return count;
}

/*********************************************************************
 * Power management: the embedded controller forgets the battery
 * thresholds when the system is suspended to disk and unplugged from
 * AC and battery, so we restore it upon resume.
 */

static int saved_threshs[4] = {-1, -1, -1, -1};  /* -1 = don't know */

static int tp_suspend(struct platform_device *dev, pm_message_t state)
{
	int restore = (state.event == PM_EVENT_HIBERNATE ||
	               state.event == PM_EVENT_FREEZE);
	if (!restore || get_real_thresh(0, THRESH_STOP , &saved_threshs[0]))
		saved_threshs[0] = -1;
	if (!restore || get_real_thresh(0, THRESH_START, &saved_threshs[1]))
		saved_threshs[1] = -1;
	if (!restore || get_real_thresh(1, THRESH_STOP , &saved_threshs[2]))
		saved_threshs[2] = -1;
	if (!restore || get_real_thresh(1, THRESH_START, &saved_threshs[3]))
		saved_threshs[3] = -1;
	DPRINTK("suspend saved: %d %d %d %d", saved_threshs[0],
		saved_threshs[1], saved_threshs[2], saved_threshs[3]);
	return 0;
}

static int tp_resume(struct platform_device *dev)
{
	DPRINTK("resume restoring: %d %d %d %d", saved_threshs[0],
		saved_threshs[1], saved_threshs[2], saved_threshs[3]);
	if (saved_threshs[0] >= 0)
		set_real_thresh(0, THRESH_STOP , saved_threshs[0]);
	if (saved_threshs[1] >= 0)
		set_real_thresh(0, THRESH_START, saved_threshs[1]);
	if (saved_threshs[2] >= 0)
		set_real_thresh(1, THRESH_STOP , saved_threshs[2]);
	if (saved_threshs[3] >= 0)
		set_real_thresh(1, THRESH_START, saved_threshs[3]);
	return 0;
}


/*********************************************************************
 * Driver model
 */

static struct platform_driver tp_driver = {
	.suspend = tp_suspend,
	.resume = tp_resume,
	.driver = {
		.name = "smapi",
		.owner = THIS_MODULE
	},
};


/*********************************************************************
 * Sysfs device model
 */

/* Attributes in /sys/devices/platform/smapi/ */

static DEVICE_ATTR(ac_connected, 0444, show_ac_connected, NULL);
static DEVICE_ATTR(smapi_request, 0600, show_smapi_request,
					store_smapi_request);

static struct attribute *tp_root_attributes[] = {
	&dev_attr_ac_connected.attr,
	&dev_attr_smapi_request.attr,
	NULL
};
static struct attribute_group tp_root_attribute_group = {
	.attrs = tp_root_attributes
};

/* Attributes under /sys/devices/platform/smapi/BAT{0,1}/ :
 * Every attribute needs to be defined (i.e., statically allocated) for
 * each battery, and then referenced in the attribute list of each battery.
 * We use preprocessor voodoo to avoid duplicating the list of attributes 4
 * times. The preprocessor output is just normal sysfs attributes code.
 */

/**
 * FOREACH_BAT_ATTR - invoke the given macros on all our battery attributes
 * @_BAT:     battery number (0 or 1)
 * @_ATTR_RW: macro to invoke for each read/write attribute
 * @_ATTR_R:  macro to invoke for each read-only  attribute
 */
#define FOREACH_BAT_ATTR(_BAT, _ATTR_RW, _ATTR_R) \
	_ATTR_RW(_BAT, start_charge_thresh) \
	_ATTR_RW(_BAT, stop_charge_thresh) \
	_ATTR_RW(_BAT, inhibit_charge_minutes) \
	_ATTR_RW(_BAT, force_discharge) \
	_ATTR_R(_BAT, installed) \
	_ATTR_R(_BAT, state) \
	_ATTR_R(_BAT, manufacturer) \
	_ATTR_R(_BAT, model) \
	_ATTR_R(_BAT, barcoding) \
	_ATTR_R(_BAT, chemistry) \
	_ATTR_R(_BAT, voltage) \
	_ATTR_R(_BAT, group0_voltage) \
	_ATTR_R(_BAT, group1_voltage) \
	_ATTR_R(_BAT, group2_voltage) \
	_ATTR_R(_BAT, group3_voltage) \
	_ATTR_R(_BAT, current_now) \
	_ATTR_R(_BAT, current_avg) \
	_ATTR_R(_BAT, charging_max_current) \
	_ATTR_R(_BAT, power_now) \
	_ATTR_R(_BAT, power_avg) \
	_ATTR_R(_BAT, remaining_percent) \
	_ATTR_R(_BAT, remaining_percent_error) \
	_ATTR_R(_BAT, remaining_charging_time) \
	_ATTR_R(_BAT, remaining_running_time) \
	_ATTR_R(_BAT, remaining_running_time_now) \
	_ATTR_R(_BAT, remaining_capacity) \
	_ATTR_R(_BAT, last_full_capacity) \
	_ATTR_R(_BAT, design_voltage) \
	_ATTR_R(_BAT, charging_max_voltage) \
	_ATTR_R(_BAT, design_capacity) \
	_ATTR_R(_BAT, cycle_count) \
	_ATTR_R(_BAT, temperature) \
	_ATTR_R(_BAT, serial) \
	_ATTR_R(_BAT, manufacture_date) \
	_ATTR_R(_BAT, first_use_date) \
	_ATTR_R(_BAT, dump)

/* Define several macros we will feed into FOREACH_BAT_ATTR: */

#define DEFINE_BAT_ATTR_RW(_BAT,_NAME) \
	static struct bat_device_attribute dev_attr_##_NAME##_##_BAT = {  \
		.dev_attr = __ATTR(_NAME, 0644, show_battery_##_NAME,   \
						store_battery_##_NAME), \
		.bat = _BAT \
	};

#define DEFINE_BAT_ATTR_R(_BAT,_NAME) \
	static struct bat_device_attribute dev_attr_##_NAME##_##_BAT = {    \
		.dev_attr = __ATTR(_NAME, 0644, show_battery_##_NAME, 0), \
		.bat = _BAT \
	};

#define REF_BAT_ATTR(_BAT,_NAME) \
	&dev_attr_##_NAME##_##_BAT.dev_attr.attr,

/* This provide all attributes for one battery: */

#define PROVIDE_BAT_ATTRS(_BAT) \
	FOREACH_BAT_ATTR(_BAT, DEFINE_BAT_ATTR_RW, DEFINE_BAT_ATTR_R) \
	static struct attribute *tp_bat##_BAT##_attributes[] = { \
		FOREACH_BAT_ATTR(_BAT, REF_BAT_ATTR, REF_BAT_ATTR) \
		NULL \
	}; \
	static struct attribute_group tp_bat##_BAT##_attribute_group = { \
		.name  = "BAT" #_BAT, \
		.attrs = tp_bat##_BAT##_attributes \
	};

/* Finally genereate the attributes: */

PROVIDE_BAT_ATTRS(0)
PROVIDE_BAT_ATTRS(1)

/* List of attribute groups */

static struct attribute_group *attr_groups[] = {
	&tp_root_attribute_group,
	&tp_bat0_attribute_group,
	&tp_bat1_attribute_group,
	NULL
};


/*********************************************************************
 * Init and cleanup
 */

static struct attribute_group **next_attr_group; /* next to register */

static int __init tp_init(void)
{
	int ret;
	printk(KERN_INFO "tp_smapi " TP_VERSION " loading...\n");

	ret = find_smapi_port();
	if (ret < 0)
		goto err;
	else
		smapi_port = ret;

	if (!request_region(smapi_port, 1, "smapi")) {
		printk(KERN_ERR "tp_smapi cannot claim port 0x%x\n",
		       smapi_port);
		ret = -ENXIO;
		goto err;
	}

	if (!request_region(SMAPI_PORT2, 1, "smapi")) {
		printk(KERN_ERR "tp_smapi cannot claim port 0x%x\n",
		       SMAPI_PORT2);
		ret = -ENXIO;
		goto err_port1;
	}

	ret = platform_driver_register(&tp_driver);
	if (ret)
		goto err_port2;

	pdev = platform_device_alloc("smapi", -1);
	if (!pdev) {
		ret = -ENOMEM;
		goto err_driver;
	}

	ret = platform_device_add(pdev);
	if (ret)
		goto err_device_free;

	for (next_attr_group = attr_groups; *next_attr_group;
	     ++next_attr_group) {
		ret = sysfs_create_group(&pdev->dev.kobj, *next_attr_group);
		if (ret)
			goto err_attr;
	}

	printk(KERN_INFO "tp_smapi successfully loaded (smapi_port=0x%x).\n",
	       smapi_port);
	return 0;

err_attr:
	while (--next_attr_group >= attr_groups)
		sysfs_remove_group(&pdev->dev.kobj, *next_attr_group);
	platform_device_unregister(pdev);
err_device_free:
	platform_device_put(pdev);
err_driver:
	platform_driver_unregister(&tp_driver);
err_port2:
	release_region(SMAPI_PORT2, 1);
err_port1:
	release_region(smapi_port, 1);
err:
	printk(KERN_ERR "tp_smapi init failed (ret=%d)!\n", ret);
	return ret;
}

static void __exit tp_exit(void)
{
	while (next_attr_group && --next_attr_group >= attr_groups)
		sysfs_remove_group(&pdev->dev.kobj, *next_attr_group);
	platform_device_unregister(pdev);
	platform_driver_unregister(&tp_driver);
	release_region(SMAPI_PORT2, 1);
	if (smapi_port)
		release_region(smapi_port, 1);

	printk(KERN_INFO "tp_smapi unloaded.\n");
}

module_init(tp_init);
module_exit(tp_exit);
