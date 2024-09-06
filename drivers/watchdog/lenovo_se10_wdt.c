// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * WDT driver for Lenovo SE10.
 */

#include <linux/delay.h>
#include <linux/dmi.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/types.h>
#include <linux/watchdog.h>

#define STATUS_PORT	0x6C
#define CMD_PORT	0x6C
#define DATA_PORT	0x68
#define OUTBUF_FULL	0x01
#define INBUF_EMPTY	0x02
#define CFG_LDN		0x07
#define CFG_BRAM_LDN	0x10 /* for BRAM Base */
#define CFG_PORT	0x2E
#define CFG_SIZE           2
#define CMD_SIZE           4
#define BRAM_SIZE          2

#define UNLOCK_KEY	0x87
#define LOCK_KEY	0xAA

#define CUS_WDT_SWI	0x1A
#define CUS_WDT_CFG	0x1B
#define CUS_WDT_FEED	0xB0
#define CUS_WDT_CNT	0xB1

#define DRVNAME	"lenovo-se10-wdt"

/*The timeout range is 1-255 seconds*/
#define MIN_TIMEOUT 1
#define MAX_TIMEOUT 255
#define MAX_WAIT    10

#define WATCHDOG_TIMEOUT 60 /* 60 sec default timeout */
static unsigned short bram_base;
static struct platform_device *se10_pdev;

static int timeout; /* in seconds */
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,
		 "Watchdog timeout in seconds. 1 <= timeout <= 255, default="
		 __MODULE_STRING(WATCHDOG_TIMEOUT) ".");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

struct se10_wdt {
	struct watchdog_device wdd;
};

static int set_bram(unsigned char offset, unsigned char val)
{
	if (!request_muxed_region(bram_base, BRAM_SIZE, DRVNAME))
		return -EBUSY;
	outb(offset, bram_base);
	outb(val, bram_base + 1);
	release_region(bram_base, BRAM_SIZE);
	return 0;
}

static void wait_for_buffer(int condition)
{
	int loop = 0;

	while (1) {
		if (inb(STATUS_PORT) & condition || loop > MAX_WAIT)
			break;
		loop++;
		usleep_range(10, 125);
	}
}

static void send_cmd(unsigned char cmd)
{
	wait_for_buffer(INBUF_EMPTY);
	outb(cmd, CMD_PORT);
	wait_for_buffer(INBUF_EMPTY);
}

static void lpc_write(unsigned char index, unsigned char data)
{
	outb(index, CFG_PORT);
	outb(data, CFG_PORT + 1);
}

static unsigned char lpc_read(unsigned char index)
{
	outb(index, CFG_PORT);
	return inb(CFG_PORT + 1);
}

static int wdt_start(struct watchdog_device *wdog)
{
	return set_bram(CUS_WDT_SWI, 0x80);
}

static int wdt_set_timeout(struct watchdog_device *wdog, unsigned int timeout)
{
	wdog->timeout = timeout;
	return set_bram(CUS_WDT_CFG, wdog->timeout);
}

static int wdt_stop(struct watchdog_device *wdog)
{
	return set_bram(CUS_WDT_SWI, 0);
}

static unsigned int wdt_get_time(struct watchdog_device *wdog)
{
	unsigned char time;

	if (!request_muxed_region(CMD_PORT, CMD_SIZE, DRVNAME))
		return -EBUSY;
	send_cmd(CUS_WDT_CNT);
	wait_for_buffer(OUTBUF_FULL);
	time = inb(DATA_PORT);
	release_region(CMD_PORT, CMD_SIZE);
	return time;
}

static int wdt_ping(struct watchdog_device *wdog)
{
	if (!request_muxed_region(CMD_PORT, CMD_SIZE, DRVNAME))
		return -EBUSY;
	send_cmd(CUS_WDT_FEED);
	release_region(CMD_PORT, CMD_SIZE);
	return 0;
}

static const struct watchdog_info wdt_info = {
	.options = WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING | WDIOF_MAGICCLOSE,
	.identity = "Lenovo SE10 Watchdog",
};

static const struct watchdog_ops se10_wdt_ops = {
	.owner = THIS_MODULE,
	.start = wdt_start,
	.stop = wdt_stop,
	.ping = wdt_ping,
	.set_timeout = wdt_set_timeout,
	.get_timeleft = wdt_get_time,
};

static unsigned int get_chipID(void)
{
	unsigned char msb, lsb;

	outb(UNLOCK_KEY, CFG_PORT);
	outb(0x01, CFG_PORT);
	outb(0x55, CFG_PORT);
	outb(0x55, CFG_PORT);
	msb = lpc_read(0x20);
	lsb = lpc_read(0x21);
	outb(LOCK_KEY, CFG_PORT);
	return (msb * 256 + lsb);
}

static int se10_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct se10_wdt *priv;
	unsigned int chip_id;
	int ret;

	if (!request_muxed_region(CFG_PORT, CFG_SIZE, DRVNAME))
		return -EBUSY;

	chip_id = get_chipID();
	if (chip_id != 0x5632) {
		release_region(CFG_PORT, CFG_SIZE);
		return -ENODEV;
	}

	lpc_write(CFG_LDN, CFG_BRAM_LDN);
	bram_base = (lpc_read(0x60) << 8) | lpc_read(0x61);
	release_region(CFG_PORT, CFG_SIZE);

	dev_info(dev, "Found Lenovo SE10 0x%x\n", chip_id);

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	watchdog_set_drvdata(&priv->wdd, priv);

	priv->wdd.parent = dev;
	priv->wdd.info = &wdt_info;
	priv->wdd.ops = &se10_wdt_ops;
	priv->wdd.timeout = WATCHDOG_TIMEOUT; /* Set default timeout */
	priv->wdd.min_timeout = MIN_TIMEOUT;
	priv->wdd.max_timeout = MAX_TIMEOUT;

	set_bram(CUS_WDT_CFG, WATCHDOG_TIMEOUT); /* Set time to default */

	watchdog_init_timeout(&priv->wdd, timeout, dev);
	watchdog_set_nowayout(&priv->wdd, nowayout);
	watchdog_stop_on_reboot(&priv->wdd);
	watchdog_stop_on_unregister(&priv->wdd);

	ret = devm_watchdog_register_device(dev, &priv->wdd);

	dev_dbg(&pdev->dev, "initialized. timeout=%d sec (nowayout=%d)\n",
		priv->wdd.timeout, nowayout);

	return ret;
}

static struct platform_driver se10_wdt_driver = {
	.driver = {
		.name = DRVNAME,
	},
	.probe  = se10_wdt_probe,
};

static int se10_create_platform_device(const struct dmi_system_id *id)
{
	int err;

	se10_pdev = platform_device_alloc("lenovo-se10-wdt", -1);
	if (!se10_pdev)
		return -ENOMEM;

	err = platform_device_add(se10_pdev);
	if (err)
		platform_device_put(se10_pdev);

	return err;
}

static const struct dmi_system_id se10_dmi_table[] __initconst = {
	{
		.ident = "LENOVO-SE10",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "12NH"),
		},
		.callback = se10_create_platform_device,
	},
	{
		.ident = "LENOVO-SE10",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "12NJ"),
		},
		.callback = se10_create_platform_device,
	},
	{
		.ident = "LENOVO-SE10",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "12NK"),
		},
		.callback = se10_create_platform_device,
	},
	{
		.ident = "LENOVO-SE10",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "12NL"),
		},
		.callback = se10_create_platform_device,
	},
	{
		.ident = "LENOVO-SE10",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "12NM"),
		},
		.callback = se10_create_platform_device,
	},
	{}
};
MODULE_DEVICE_TABLE(dmi, se10_dmi_table);

static int __init se10_wdt_init(void)
{
	if (!dmi_check_system(se10_dmi_table))
		return -ENODEV;

	return platform_driver_register(&se10_wdt_driver);
}

static void __exit se10_wdt_exit(void)
{
	if (se10_pdev)
		platform_device_unregister(se10_pdev);
	platform_driver_unregister(&se10_wdt_driver);
}

module_init(se10_wdt_init);
module_exit(se10_wdt_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("David Ober<dober@lenovo.com>");
MODULE_AUTHOR("Mark Pearson <mpearson-lenovo@squebb.ca>");
MODULE_DESCRIPTION("WDT driver for Lenovo SE10");
