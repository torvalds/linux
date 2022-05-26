// SPDX-License-Identifier: GPL-2.0-or-later
/***************************************************************************
 *   Copyright (C) 2006 by Hans Edgington <hans@edgington.nl>              *
 *   Copyright (C) 2007-2009 Hans de Goede <hdegoede@redhat.com>           *
 *   Copyright (C) 2010 Giel van Schijndel <me@mortis.eu>                  *
 *                                                                         *
 ***************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/err.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define DRVNAME "f71808e_wdt"

#define SIO_F71808FG_LD_WDT	0x07	/* Watchdog timer logical device */
#define SIO_UNLOCK_KEY		0x87	/* Key to enable Super-I/O */
#define SIO_LOCK_KEY		0xAA	/* Key to disable Super-I/O */

#define SIO_REG_LDSEL		0x07	/* Logical device select */
#define SIO_REG_DEVID		0x20	/* Device ID (2 bytes) */
#define SIO_REG_DEVREV		0x22	/* Device revision */
#define SIO_REG_MANID		0x23	/* Fintek ID (2 bytes) */
#define SIO_REG_CLOCK_SEL	0x26	/* Clock select */
#define SIO_REG_ROM_ADDR_SEL	0x27	/* ROM address select */
#define SIO_F81866_REG_PORT_SEL	0x27	/* F81866 Multi-Function Register */
#define SIO_REG_TSI_LEVEL_SEL	0x28	/* TSI Level select */
#define SIO_REG_MFUNCT1		0x29	/* Multi function select 1 */
#define SIO_REG_MFUNCT2		0x2a	/* Multi function select 2 */
#define SIO_REG_MFUNCT3		0x2b	/* Multi function select 3 */
#define SIO_F81866_REG_GPIO1	0x2c	/* F81866 GPIO1 Enable Register */
#define SIO_REG_ENABLE		0x30	/* Logical device enable */
#define SIO_REG_ADDR		0x60	/* Logical device address (2 bytes) */

#define SIO_FINTEK_ID		0x1934	/* Manufacturers ID */
#define SIO_F71808_ID		0x0901	/* Chipset ID */
#define SIO_F71858_ID		0x0507	/* Chipset ID */
#define SIO_F71862_ID		0x0601	/* Chipset ID */
#define SIO_F71868_ID		0x1106	/* Chipset ID */
#define SIO_F71869_ID		0x0814	/* Chipset ID */
#define SIO_F71869A_ID		0x1007	/* Chipset ID */
#define SIO_F71882_ID		0x0541	/* Chipset ID */
#define SIO_F71889_ID		0x0723	/* Chipset ID */
#define SIO_F81803_ID		0x1210	/* Chipset ID */
#define SIO_F81865_ID		0x0704	/* Chipset ID */
#define SIO_F81866_ID		0x1010	/* Chipset ID */
#define SIO_F81966_ID		0x1502  /* F81804 chipset ID, same for f81966 */

#define F71808FG_REG_WDO_CONF		0xf0
#define F71808FG_REG_WDT_CONF		0xf5
#define F71808FG_REG_WD_TIME		0xf6

#define F71808FG_FLAG_WDOUT_EN		7

#define F71808FG_FLAG_WDTMOUT_STS	6
#define F71808FG_FLAG_WD_EN		5
#define F71808FG_FLAG_WD_PULSE		4
#define F71808FG_FLAG_WD_UNIT		3

#define F81865_REG_WDO_CONF		0xfa
#define F81865_FLAG_WDOUT_EN		0

/* Default values */
#define WATCHDOG_TIMEOUT	60	/* 1 minute default timeout */
#define WATCHDOG_MAX_TIMEOUT	(60 * 255)
#define WATCHDOG_PULSE_WIDTH	125	/* 125 ms, default pulse width for
					   watchdog signal */
#define WATCHDOG_F71862FG_PIN	63	/* default watchdog reset output
					   pin number 63 */

static unsigned short force_id;
module_param(force_id, ushort, 0);
MODULE_PARM_DESC(force_id, "Override the detected device ID");

static int timeout = WATCHDOG_TIMEOUT;	/* default timeout in seconds */
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,
	"Watchdog timeout in seconds. 1<= timeout <="
			__MODULE_STRING(WATCHDOG_MAX_TIMEOUT) " (default="
			__MODULE_STRING(WATCHDOG_TIMEOUT) ")");

static unsigned int pulse_width = WATCHDOG_PULSE_WIDTH;
module_param(pulse_width, uint, 0);
MODULE_PARM_DESC(pulse_width,
	"Watchdog signal pulse width. 0(=level), 1, 25, 30, 125, 150, 5000 or 6000 ms"
			" (default=" __MODULE_STRING(WATCHDOG_PULSE_WIDTH) ")");

static unsigned int f71862fg_pin = WATCHDOG_F71862FG_PIN;
module_param(f71862fg_pin, uint, 0);
MODULE_PARM_DESC(f71862fg_pin,
	"Watchdog f71862fg reset output pin configuration. Choose pin 56 or 63"
			" (default=" __MODULE_STRING(WATCHDOG_F71862FG_PIN)")");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0444);
MODULE_PARM_DESC(nowayout, "Disable watchdog shutdown on close");

static unsigned int start_withtimeout;
module_param(start_withtimeout, uint, 0);
MODULE_PARM_DESC(start_withtimeout, "Start watchdog timer on module load with"
	" given initial timeout. Zero (default) disables this feature.");

enum chips { f71808fg, f71858fg, f71862fg, f71868, f71869, f71882fg, f71889fg,
	     f81803, f81865, f81866, f81966};

static const char * const fintek_wdt_names[] = {
	"f71808fg",
	"f71858fg",
	"f71862fg",
	"f71868",
	"f71869",
	"f71882fg",
	"f71889fg",
	"f81803",
	"f81865",
	"f81866",
	"f81966"
};

/* Super-I/O Function prototypes */
static inline int superio_inb(int base, int reg);
static inline int superio_inw(int base, int reg);
static inline void superio_outb(int base, int reg, u8 val);
static inline void superio_set_bit(int base, int reg, int bit);
static inline void superio_clear_bit(int base, int reg, int bit);
static inline int superio_enter(int base);
static inline void superio_select(int base, int ld);
static inline void superio_exit(int base);

struct fintek_wdt {
	struct watchdog_device wdd;
	unsigned short	sioaddr;
	enum chips	type;
	struct watchdog_info ident;

	u8		timer_val;	/* content for the wd_time register */
	char		minutes_mode;
	u8		pulse_val;	/* pulse width flag */
	char		pulse_mode;	/* enable pulse output mode? */
};

struct fintek_wdt_pdata {
	enum chips	type;
};

/* Super I/O functions */
static inline int superio_inb(int base, int reg)
{
	outb(reg, base);
	return inb(base + 1);
}

static int superio_inw(int base, int reg)
{
	int val;
	val  = superio_inb(base, reg) << 8;
	val |= superio_inb(base, reg + 1);
	return val;
}

static inline void superio_outb(int base, int reg, u8 val)
{
	outb(reg, base);
	outb(val, base + 1);
}

static inline void superio_set_bit(int base, int reg, int bit)
{
	unsigned long val = superio_inb(base, reg);
	__set_bit(bit, &val);
	superio_outb(base, reg, val);
}

static inline void superio_clear_bit(int base, int reg, int bit)
{
	unsigned long val = superio_inb(base, reg);
	__clear_bit(bit, &val);
	superio_outb(base, reg, val);
}

static inline int superio_enter(int base)
{
	/* Don't step on other drivers' I/O space by accident */
	if (!request_muxed_region(base, 2, DRVNAME)) {
		pr_err("I/O address 0x%04x already in use\n", (int)base);
		return -EBUSY;
	}

	/* according to the datasheet the key must be sent twice! */
	outb(SIO_UNLOCK_KEY, base);
	outb(SIO_UNLOCK_KEY, base);

	return 0;
}

static inline void superio_select(int base, int ld)
{
	outb(SIO_REG_LDSEL, base);
	outb(ld, base + 1);
}

static inline void superio_exit(int base)
{
	outb(SIO_LOCK_KEY, base);
	release_region(base, 2);
}

static int fintek_wdt_set_timeout(struct watchdog_device *wdd, unsigned int timeout)
{
	struct fintek_wdt *wd = watchdog_get_drvdata(wdd);

	if (timeout > 0xff) {
		wd->timer_val = DIV_ROUND_UP(timeout, 60);
		wd->minutes_mode = true;
		timeout = wd->timer_val * 60;
	} else {
		wd->timer_val = timeout;
		wd->minutes_mode = false;
	}

	wdd->timeout = timeout;

	return 0;
}

static int fintek_wdt_set_pulse_width(struct fintek_wdt *wd, unsigned int pw)
{
	unsigned int t1 = 25, t2 = 125, t3 = 5000;

	if (wd->type == f71868) {
		t1 = 30;
		t2 = 150;
		t3 = 6000;
	}

	if        (pw <=  1) {
		wd->pulse_val = 0;
	} else if (pw <= t1) {
		wd->pulse_val = 1;
	} else if (pw <= t2) {
		wd->pulse_val = 2;
	} else if (pw <= t3) {
		wd->pulse_val = 3;
	} else {
		pr_err("pulse width out of range\n");
		return -EINVAL;
	}

	wd->pulse_mode = pw;

	return 0;
}

static int fintek_wdt_keepalive(struct watchdog_device *wdd)
{
	struct fintek_wdt *wd = watchdog_get_drvdata(wdd);
	int err;

	err = superio_enter(wd->sioaddr);
	if (err)
		return err;
	superio_select(wd->sioaddr, SIO_F71808FG_LD_WDT);

	if (wd->minutes_mode)
		/* select minutes for timer units */
		superio_set_bit(wd->sioaddr, F71808FG_REG_WDT_CONF,
				F71808FG_FLAG_WD_UNIT);
	else
		/* select seconds for timer units */
		superio_clear_bit(wd->sioaddr, F71808FG_REG_WDT_CONF,
				F71808FG_FLAG_WD_UNIT);

	/* Set timer value */
	superio_outb(wd->sioaddr, F71808FG_REG_WD_TIME,
			   wd->timer_val);

	superio_exit(wd->sioaddr);

	return 0;
}

static int fintek_wdt_start(struct watchdog_device *wdd)
{
	struct fintek_wdt *wd = watchdog_get_drvdata(wdd);
	int err;
	u8 tmp;

	/* Make sure we don't die as soon as the watchdog is enabled below */
	err = fintek_wdt_keepalive(wdd);
	if (err)
		return err;

	err = superio_enter(wd->sioaddr);
	if (err)
		return err;
	superio_select(wd->sioaddr, SIO_F71808FG_LD_WDT);

	/* Watchdog pin configuration */
	switch (wd->type) {
	case f71808fg:
		/* Set pin 21 to GPIO23/WDTRST#, then to WDTRST# */
		superio_clear_bit(wd->sioaddr, SIO_REG_MFUNCT2, 3);
		superio_clear_bit(wd->sioaddr, SIO_REG_MFUNCT3, 3);
		break;

	case f71862fg:
		if (f71862fg_pin == 63) {
			/* SPI must be disabled first to use this pin! */
			superio_clear_bit(wd->sioaddr, SIO_REG_ROM_ADDR_SEL, 6);
			superio_set_bit(wd->sioaddr, SIO_REG_MFUNCT3, 4);
		} else if (f71862fg_pin == 56) {
			superio_set_bit(wd->sioaddr, SIO_REG_MFUNCT1, 1);
		}
		break;

	case f71868:
	case f71869:
		/* GPIO14 --> WDTRST# */
		superio_clear_bit(wd->sioaddr, SIO_REG_MFUNCT1, 4);
		break;

	case f71882fg:
		/* Set pin 56 to WDTRST# */
		superio_set_bit(wd->sioaddr, SIO_REG_MFUNCT1, 1);
		break;

	case f71889fg:
		/* set pin 40 to WDTRST# */
		superio_outb(wd->sioaddr, SIO_REG_MFUNCT3,
			superio_inb(wd->sioaddr, SIO_REG_MFUNCT3) & 0xcf);
		break;

	case f81803:
		/* Enable TSI Level register bank */
		superio_clear_bit(wd->sioaddr, SIO_REG_CLOCK_SEL, 3);
		/* Set pin 27 to WDTRST# */
		superio_outb(wd->sioaddr, SIO_REG_TSI_LEVEL_SEL, 0x5f &
			superio_inb(wd->sioaddr, SIO_REG_TSI_LEVEL_SEL));
		break;

	case f81865:
		/* Set pin 70 to WDTRST# */
		superio_clear_bit(wd->sioaddr, SIO_REG_MFUNCT3, 5);
		break;

	case f81866:
	case f81966:
		/*
		 * GPIO1 Control Register when 27h BIT3:2 = 01 & BIT0 = 0.
		 * The PIN 70(GPIO15/WDTRST) is controlled by 2Ch:
		 *     BIT5: 0 -> WDTRST#
		 *           1 -> GPIO15
		 */
		tmp = superio_inb(wd->sioaddr, SIO_F81866_REG_PORT_SEL);
		tmp &= ~(BIT(3) | BIT(0));
		tmp |= BIT(2);
		superio_outb(wd->sioaddr, SIO_F81866_REG_PORT_SEL, tmp);

		superio_clear_bit(wd->sioaddr, SIO_F81866_REG_GPIO1, 5);
		break;

	default:
		/*
		 * 'default' label to shut up the compiler and catch
		 * programmer errors
		 */
		err = -ENODEV;
		goto exit_superio;
	}

	superio_select(wd->sioaddr, SIO_F71808FG_LD_WDT);
	superio_set_bit(wd->sioaddr, SIO_REG_ENABLE, 0);

	if (wd->type == f81865 || wd->type == f81866 || wd->type == f81966)
		superio_set_bit(wd->sioaddr, F81865_REG_WDO_CONF,
				F81865_FLAG_WDOUT_EN);
	else
		superio_set_bit(wd->sioaddr, F71808FG_REG_WDO_CONF,
				F71808FG_FLAG_WDOUT_EN);

	superio_set_bit(wd->sioaddr, F71808FG_REG_WDT_CONF,
			F71808FG_FLAG_WD_EN);

	if (wd->pulse_mode) {
		/* Select "pulse" output mode with given duration */
		u8 wdt_conf = superio_inb(wd->sioaddr,
				F71808FG_REG_WDT_CONF);

		/* Set WD_PSWIDTH bits (1:0) */
		wdt_conf = (wdt_conf & 0xfc) | (wd->pulse_val & 0x03);
		/* Set WD_PULSE to "pulse" mode */
		wdt_conf |= BIT(F71808FG_FLAG_WD_PULSE);

		superio_outb(wd->sioaddr, F71808FG_REG_WDT_CONF,
				wdt_conf);
	} else {
		/* Select "level" output mode */
		superio_clear_bit(wd->sioaddr, F71808FG_REG_WDT_CONF,
				F71808FG_FLAG_WD_PULSE);
	}

exit_superio:
	superio_exit(wd->sioaddr);

	return err;
}

static int fintek_wdt_stop(struct watchdog_device *wdd)
{
	struct fintek_wdt *wd = watchdog_get_drvdata(wdd);
	int err;

	err = superio_enter(wd->sioaddr);
	if (err)
		return err;
	superio_select(wd->sioaddr, SIO_F71808FG_LD_WDT);

	superio_clear_bit(wd->sioaddr, F71808FG_REG_WDT_CONF,
			F71808FG_FLAG_WD_EN);

	superio_exit(wd->sioaddr);

	return 0;
}

static bool fintek_wdt_is_running(struct fintek_wdt *wd, u8 wdt_conf)
{
	return (superio_inb(wd->sioaddr, SIO_REG_ENABLE) & BIT(0))
		&& (wdt_conf & BIT(F71808FG_FLAG_WD_EN));
}

static const struct watchdog_ops fintek_wdt_ops = {
	.owner = THIS_MODULE,
	.start = fintek_wdt_start,
	.stop = fintek_wdt_stop,
	.ping = fintek_wdt_keepalive,
	.set_timeout = fintek_wdt_set_timeout,
};

static int fintek_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct fintek_wdt_pdata *pdata;
	struct watchdog_device *wdd;
	struct fintek_wdt *wd;
	int wdt_conf, err = 0;
	struct resource *res;
	int sioaddr;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res)
		return -ENXIO;

	sioaddr = res->start;

	wd = devm_kzalloc(dev, sizeof(*wd), GFP_KERNEL);
	if (!wd)
		return -ENOMEM;

	pdata = dev->platform_data;

	wd->type = pdata->type;
	wd->sioaddr = sioaddr;
	wd->ident.options = WDIOF_SETTIMEOUT
			| WDIOF_MAGICCLOSE
			| WDIOF_KEEPALIVEPING
			| WDIOF_CARDRESET;

	snprintf(wd->ident.identity,
		sizeof(wd->ident.identity), "%s watchdog",
		fintek_wdt_names[wd->type]);

	err = superio_enter(sioaddr);
	if (err)
		return err;
	superio_select(wd->sioaddr, SIO_F71808FG_LD_WDT);

	wdt_conf = superio_inb(sioaddr, F71808FG_REG_WDT_CONF);

	/*
	 * We don't want WDTMOUT_STS to stick around till regular reboot.
	 * Write 1 to the bit to clear it to zero.
	 */
	superio_outb(sioaddr, F71808FG_REG_WDT_CONF,
		     wdt_conf | BIT(F71808FG_FLAG_WDTMOUT_STS));

	wdd = &wd->wdd;

	if (fintek_wdt_is_running(wd, wdt_conf))
		set_bit(WDOG_HW_RUNNING, &wdd->status);

	superio_exit(sioaddr);

	wdd->parent		= dev;
	wdd->info               = &wd->ident;
	wdd->ops                = &fintek_wdt_ops;
	wdd->min_timeout        = 1;
	wdd->max_timeout        = WATCHDOG_MAX_TIMEOUT;

	watchdog_set_drvdata(wdd, wd);
	watchdog_set_nowayout(wdd, nowayout);
	watchdog_stop_on_unregister(wdd);
	watchdog_stop_on_reboot(wdd);
	watchdog_init_timeout(wdd, start_withtimeout ?: timeout, NULL);

	if (wdt_conf & BIT(F71808FG_FLAG_WDTMOUT_STS))
		wdd->bootstatus = WDIOF_CARDRESET;

	/*
	 * WATCHDOG_HANDLE_BOOT_ENABLED can result in keepalive being directly
	 * called without a set_timeout before, so it needs to be done here
	 * unconditionally.
	 */
	fintek_wdt_set_timeout(wdd, wdd->timeout);
	fintek_wdt_set_pulse_width(wd, pulse_width);

	if (start_withtimeout) {
		err = fintek_wdt_start(wdd);
		if (err) {
			dev_err(dev, "cannot start watchdog timer\n");
			return err;
		}

		set_bit(WDOG_HW_RUNNING, &wdd->status);
		dev_info(dev, "watchdog started with initial timeout of %u sec\n",
			 start_withtimeout);
	}

	return devm_watchdog_register_device(dev, wdd);
}

static int __init fintek_wdt_find(int sioaddr)
{
	enum chips type;
	u16 devid;
	int err = superio_enter(sioaddr);
	if (err)
		return err;

	devid = superio_inw(sioaddr, SIO_REG_MANID);
	if (devid != SIO_FINTEK_ID) {
		pr_debug("Not a Fintek device\n");
		err = -ENODEV;
		goto exit;
	}

	devid = force_id ? force_id : superio_inw(sioaddr, SIO_REG_DEVID);
	switch (devid) {
	case SIO_F71808_ID:
		type = f71808fg;
		break;
	case SIO_F71862_ID:
		type = f71862fg;
		break;
	case SIO_F71868_ID:
		type = f71868;
		break;
	case SIO_F71869_ID:
	case SIO_F71869A_ID:
		type = f71869;
		break;
	case SIO_F71882_ID:
		type = f71882fg;
		break;
	case SIO_F71889_ID:
		type = f71889fg;
		break;
	case SIO_F71858_ID:
		/* Confirmed (by datasheet) not to have a watchdog. */
		err = -ENODEV;
		goto exit;
	case SIO_F81803_ID:
		type = f81803;
		break;
	case SIO_F81865_ID:
		type = f81865;
		break;
	case SIO_F81866_ID:
		type = f81866;
		break;
	case SIO_F81966_ID:
		type = f81966;
		break;
	default:
		pr_info("Unrecognized Fintek device: %04x\n",
			(unsigned int)devid);
		err = -ENODEV;
		goto exit;
	}

	pr_info("Found %s watchdog chip, revision %d\n",
		fintek_wdt_names[type],
		(int)superio_inb(sioaddr, SIO_REG_DEVREV));

exit:
	superio_exit(sioaddr);
	return err ? err : type;
}

static struct platform_driver fintek_wdt_driver = {
	.probe          = fintek_wdt_probe,
	.driver         = {
		.name   = DRVNAME,
	},
};

static struct platform_device *fintek_wdt_pdev;

static int __init fintek_wdt_init(void)
{
	static const unsigned short addrs[] = { 0x2e, 0x4e };
	struct fintek_wdt_pdata pdata;
	struct resource wdt_res = {};
	int ret;
	int i;

	if (f71862fg_pin != 63 && f71862fg_pin != 56) {
		pr_err("Invalid argument f71862fg_pin=%d\n", f71862fg_pin);
		return -EINVAL;
	}

	for (i = 0; i < ARRAY_SIZE(addrs); i++) {
		ret = fintek_wdt_find(addrs[i]);
		if (ret >= 0)
			break;
	}
	if (i == ARRAY_SIZE(addrs))
		return ret;

	pdata.type = ret;

	ret = platform_driver_register(&fintek_wdt_driver);
	if (ret)
		return ret;

	wdt_res.name = "superio port";
	wdt_res.flags = IORESOURCE_IO;
	wdt_res.start = addrs[i];
	wdt_res.end   = addrs[i] + 1;

	fintek_wdt_pdev = platform_device_register_resndata(NULL, DRVNAME, -1,
							    &wdt_res, 1,
							    &pdata, sizeof(pdata));
	if (IS_ERR(fintek_wdt_pdev)) {
		platform_driver_unregister(&fintek_wdt_driver);
		return PTR_ERR(fintek_wdt_pdev);
	}

	return 0;
}

static void __exit fintek_wdt_exit(void)
{
	platform_device_unregister(fintek_wdt_pdev);
	platform_driver_unregister(&fintek_wdt_driver);
}

MODULE_DESCRIPTION("F71808E Watchdog Driver");
MODULE_AUTHOR("Giel van Schijndel <me@mortis.eu>");
MODULE_LICENSE("GPL");

module_init(fintek_wdt_init);
module_exit(fintek_wdt_exit);
