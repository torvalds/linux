// SPDX-License-Identifier: GPL-2.0+
/*
 *	exar_wdt.c - Driver for the watchdog present in some
 *		     Exar/MaxLinear UART chips like the XR28V38x.
 *
 *	(c) Copyright 2022 D. Müller <d.mueller@elsoft.ch>.
 *
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/list.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/watchdog.h>

#define DRV_NAME	"exar_wdt"

static const unsigned short sio_config_ports[] = { 0x2e, 0x4e };
static const unsigned char sio_enter_keys[] = { 0x67, 0x77, 0x87, 0xA0 };
#define EXAR_EXIT_KEY	0xAA

#define EXAR_LDN	0x07
#define EXAR_DID	0x20
#define EXAR_VID	0x23
#define EXAR_WDT	0x26
#define EXAR_ACT	0x30
#define EXAR_RTBASE	0x60

#define EXAR_WDT_LDEV	0x08

#define EXAR_VEN_ID	0x13A8
#define EXAR_DEV_382	0x0382
#define EXAR_DEV_384	0x0384

/* WDT runtime registers */
#define WDT_CTRL	0x00
#define WDT_VAL		0x01

#define WDT_UNITS_10MS	0x0	/* the 10 millisec unit of the HW is not used */
#define WDT_UNITS_SEC	0x2
#define WDT_UNITS_MIN	0x4

/* default WDT control for WDTOUT signal activ / rearm by read */
#define EXAR_WDT_DEF_CONF	0

struct wdt_pdev_node {
	struct list_head list;
	struct platform_device *pdev;
	const char name[16];
};

struct wdt_priv {
	/* the lock for WDT io operations */
	spinlock_t io_lock;
	struct resource wdt_res;
	struct watchdog_device wdt_dev;
	unsigned short did;
	unsigned short config_port;
	unsigned char enter_key;
	unsigned char unit;
	unsigned char timeout;
};

#define WATCHDOG_TIMEOUT 60

static int timeout = WATCHDOG_TIMEOUT;
module_param(timeout, int, 0);
MODULE_PARM_DESC(timeout,
		 "Watchdog timeout in seconds. 1<=timeout<=15300, default="
		 __MODULE_STRING(WATCHDOG_TIMEOUT) ".");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout,
		 "Watchdog cannot be stopped once started (default="
		 __MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

static int exar_sio_enter(const unsigned short config_port,
			  const unsigned char key)
{
	if (!request_muxed_region(config_port, 2, DRV_NAME))
		return -EBUSY;

	/* write the ENTER-KEY twice */
	outb(key, config_port);
	outb(key, config_port);

	return 0;
}

static void exar_sio_exit(const unsigned short config_port)
{
	outb(EXAR_EXIT_KEY, config_port);
	release_region(config_port, 2);
}

static unsigned char exar_sio_read(const unsigned short config_port,
				   const unsigned char reg)
{
	outb(reg, config_port);
	return inb(config_port + 1);
}

static void exar_sio_write(const unsigned short config_port,
			   const unsigned char reg, const unsigned char val)
{
	outb(reg, config_port);
	outb(val, config_port + 1);
}

static unsigned short exar_sio_read16(const unsigned short config_port,
				      const unsigned char reg)
{
	unsigned char msb, lsb;

	msb = exar_sio_read(config_port, reg);
	lsb = exar_sio_read(config_port, reg + 1);

	return (msb << 8) | lsb;
}

static void exar_sio_select_wdt(const unsigned short config_port)
{
	exar_sio_write(config_port, EXAR_LDN, EXAR_WDT_LDEV);
}

static void exar_wdt_arm(const struct wdt_priv *priv)
{
	unsigned short rt_base = priv->wdt_res.start;

	/* write timeout value twice to arm watchdog */
	outb(priv->timeout, rt_base + WDT_VAL);
	outb(priv->timeout, rt_base + WDT_VAL);
}

static void exar_wdt_disarm(const struct wdt_priv *priv)
{
	unsigned short rt_base = priv->wdt_res.start;

	/*
	 * use two accesses with different values to make sure
	 * that a combination of a previous single access and
	 * the ones below with the same value are not falsely
	 * interpreted as "arm watchdog"
	 */
	outb(0xFF, rt_base + WDT_VAL);
	outb(0, rt_base + WDT_VAL);
}

static int exar_wdt_start(struct watchdog_device *wdog)
{
	struct wdt_priv *priv = watchdog_get_drvdata(wdog);
	unsigned short rt_base = priv->wdt_res.start;

	spin_lock(&priv->io_lock);

	exar_wdt_disarm(priv);
	outb(priv->unit, rt_base + WDT_CTRL);
	exar_wdt_arm(priv);

	spin_unlock(&priv->io_lock);
	return 0;
}

static int exar_wdt_stop(struct watchdog_device *wdog)
{
	struct wdt_priv *priv = watchdog_get_drvdata(wdog);

	spin_lock(&priv->io_lock);

	exar_wdt_disarm(priv);

	spin_unlock(&priv->io_lock);
	return 0;
}

static int exar_wdt_keepalive(struct watchdog_device *wdog)
{
	struct wdt_priv *priv = watchdog_get_drvdata(wdog);
	unsigned short rt_base = priv->wdt_res.start;

	spin_lock(&priv->io_lock);

	/* reading the WDT_VAL reg will feed the watchdog */
	inb(rt_base + WDT_VAL);

	spin_unlock(&priv->io_lock);
	return 0;
}

static int exar_wdt_set_timeout(struct watchdog_device *wdog, unsigned int t)
{
	struct wdt_priv *priv = watchdog_get_drvdata(wdog);
	bool unit_min = false;

	/*
	 * if new timeout is bigger then 255 seconds, change the
	 * unit to minutes and round the timeout up to the next whole minute
	 */
	if (t > 255) {
		unit_min = true;
		t = DIV_ROUND_UP(t, 60);
	}

	/* save for later use in exar_wdt_start() */
	priv->unit = unit_min ? WDT_UNITS_MIN : WDT_UNITS_SEC;
	priv->timeout = t;

	wdog->timeout = unit_min ? t * 60 : t;

	if (watchdog_hw_running(wdog))
		exar_wdt_start(wdog);

	return 0;
}

static const struct watchdog_info exar_wdt_info = {
	.options	= WDIOF_KEEPALIVEPING |
			  WDIOF_SETTIMEOUT |
			  WDIOF_MAGICCLOSE,
	.identity	= "Exar/MaxLinear XR28V38x Watchdog",
};

static const struct watchdog_ops exar_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= exar_wdt_start,
	.stop		= exar_wdt_stop,
	.ping		= exar_wdt_keepalive,
	.set_timeout	= exar_wdt_set_timeout,
};

static int exar_wdt_config(struct watchdog_device *wdog,
			   const unsigned char conf)
{
	struct wdt_priv *priv = watchdog_get_drvdata(wdog);
	int ret;

	ret = exar_sio_enter(priv->config_port, priv->enter_key);
	if (ret)
		return ret;

	exar_sio_select_wdt(priv->config_port);
	exar_sio_write(priv->config_port, EXAR_WDT, conf);

	exar_sio_exit(priv->config_port);

	return 0;
}

static int __init exar_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct wdt_priv *priv = dev->platform_data;
	struct watchdog_device *wdt_dev = &priv->wdt_dev;
	struct resource *res;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_IO, 0);
	if (!res)
		return -ENXIO;

	spin_lock_init(&priv->io_lock);

	wdt_dev->info = &exar_wdt_info;
	wdt_dev->ops = &exar_wdt_ops;
	wdt_dev->min_timeout = 1;
	wdt_dev->max_timeout = 255 * 60;

	watchdog_init_timeout(wdt_dev, timeout, NULL);
	watchdog_set_nowayout(wdt_dev, nowayout);
	watchdog_stop_on_reboot(wdt_dev);
	watchdog_stop_on_unregister(wdt_dev);
	watchdog_set_drvdata(wdt_dev, priv);

	ret = exar_wdt_config(wdt_dev, EXAR_WDT_DEF_CONF);
	if (ret)
		return ret;

	exar_wdt_set_timeout(wdt_dev, timeout);
	/* Make sure that the watchdog is not running */
	exar_wdt_stop(wdt_dev);

	ret = devm_watchdog_register_device(dev, wdt_dev);
	if (ret)
		return ret;

	dev_info(dev, "XR28V%X WDT initialized. timeout=%d sec (nowayout=%d)\n",
		 priv->did, timeout, nowayout);

	return 0;
}

static unsigned short __init exar_detect(const unsigned short config_port,
					 const unsigned char key,
					 unsigned short *rt_base)
{
	int ret;
	unsigned short base = 0;
	unsigned short vid, did;

	ret = exar_sio_enter(config_port, key);
	if (ret)
		return 0;

	vid = exar_sio_read16(config_port, EXAR_VID);
	did = exar_sio_read16(config_port, EXAR_DID);

	/* check for the vendor and device IDs we currently know about */
	if (vid == EXAR_VEN_ID &&
	    (did == EXAR_DEV_382 ||
	     did == EXAR_DEV_384)) {
		exar_sio_select_wdt(config_port);
		/* is device active? */
		if (exar_sio_read(config_port, EXAR_ACT) == 0x01)
			base = exar_sio_read16(config_port, EXAR_RTBASE);
	}

	exar_sio_exit(config_port);

	if (base) {
		pr_debug("Found a XR28V%X WDT (conf: 0x%x / rt: 0x%04x)\n",
			 did, config_port, base);
		*rt_base = base;
		return did;
	}

	return 0;
}

static struct platform_driver exar_wdt_driver = {
	.driver = {
		.name = DRV_NAME,
	},
};

static LIST_HEAD(pdev_list);

static int __init exar_wdt_register(struct wdt_priv *priv, const int idx)
{
	struct wdt_pdev_node *n;

	n = kzalloc(sizeof(*n), GFP_KERNEL);
	if (!n)
		return -ENOMEM;

	INIT_LIST_HEAD(&n->list);

	scnprintf((char *)n->name, sizeof(n->name), DRV_NAME ".%d", idx);
	priv->wdt_res.name = n->name;

	n->pdev = platform_device_register_resndata(NULL, DRV_NAME, idx,
						    &priv->wdt_res, 1,
						    priv, sizeof(*priv));
	if (IS_ERR(n->pdev)) {
		int err = PTR_ERR(n->pdev);

		kfree(n);
		return err;
	}

	list_add_tail(&n->list, &pdev_list);

	return 0;
}

static void exar_wdt_unregister(void)
{
	struct wdt_pdev_node *n, *t;

	list_for_each_entry_safe(n, t, &pdev_list, list) {
		platform_device_unregister(n->pdev);
		list_del(&n->list);
		kfree(n);
	}
}

static int __init exar_wdt_init(void)
{
	int ret, i, j, idx = 0;

	/* search for active Exar watchdogs on all possible locations */
	for (i = 0; i < ARRAY_SIZE(sio_config_ports); i++) {
		for (j = 0; j < ARRAY_SIZE(sio_enter_keys); j++) {
			unsigned short did, rt_base = 0;

			did = exar_detect(sio_config_ports[i],
					  sio_enter_keys[j],
					  &rt_base);

			if (did) {
				struct wdt_priv priv = {
					.wdt_res = DEFINE_RES_IO(rt_base, 2),
					.did = did,
					.config_port = sio_config_ports[i],
					.enter_key = sio_enter_keys[j],
				};

				ret = exar_wdt_register(&priv, idx);
				if (!ret)
					idx++;
			}
		}
	}

	if (!idx)
		return -ENODEV;

	ret = platform_driver_probe(&exar_wdt_driver, exar_wdt_probe);
	if (ret)
		exar_wdt_unregister();

	return ret;
}

static void __exit exar_wdt_exit(void)
{
	exar_wdt_unregister();
	platform_driver_unregister(&exar_wdt_driver);
}

module_init(exar_wdt_init);
module_exit(exar_wdt_exit);

MODULE_AUTHOR("David Müller <d.mueller@elsoft.ch>");
MODULE_DESCRIPTION("Exar/MaxLinear Watchdog Driver");
MODULE_LICENSE("GPL");
