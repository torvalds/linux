// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * WDT driver for Lenovo SE30 device
 */

#define dev_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/io.h>
#include <linux/dmi.h>
#include <linux/delay.h>
#include <linux/iommu.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/platform_device.h>
#include <linux/watchdog.h>

#define IOREGION_OFFSET	4 /* Use EC port 1 */
#define IOREGION_LENGTH	4

#define WATCHDOG_TIMEOUT	60

#define MIN_TIMEOUT	1
#define MAX_TIMEOUT	255
#define MAX_WAIT	10

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

#define LNV_SE30_NAME	"lenovo-se30-wdt"
#define LNV_SE30_ID	0x0110
#define CHIPID_MASK	0xFFF0

#define CHIPID_REG	0x20
#define SIO_REG		0x2e
#define LDN_REG		0x07
#define UNLOCK_KEY	0x87
#define LOCK_KEY	0xAA
#define LD_NUM_SHM	0x0F
#define LD_BASE_ADDR	0xF8

#define WDT_MODULE	0x10
#define WDT_CFG_INDEX	0x15 /* WD configuration register */
#define WDT_CNT_INDEX	0x16 /* WD timer count register */
#define WDT_CFG_RESET	0x2

/* Host Interface WIN2 offset definition */
#define SHM_WIN_SIZE		0xFF
#define SHM_WIN_MOD_OFFSET	0x01
#define SHM_WIN_CMD_OFFSET	0x02
#define SHM_WIN_SEL_OFFSET	0x03
#define SHM_WIN_CTL_OFFSET	0x04
#define VAL_SHM_WIN_CTRL_WR	0x40
#define VAL_SHM_WIN_CTRL_RD	0x80
#define SHM_WIN_ID_OFFSET	0x08
#define SHM_WIN_DAT_OFFSET	0x10

struct nct6692_reg {
	unsigned char mod;
	unsigned char cmd;
	unsigned char sel;
	unsigned int idx;
};

/* Watchdog is based on NCT6692 device */
struct lenovo_se30_wdt {
	unsigned char __iomem *shm_base_addr;
	struct nct6692_reg wdt_cfg;
	struct nct6692_reg wdt_cnt;
	struct watchdog_device wdt;
};

static inline void superio_outb(int ioreg, int reg, int val)
{
	outb(reg, ioreg);
	outb(val, ioreg + 1);
}

static inline int superio_inb(int ioreg, int reg)
{
	outb(reg, ioreg);
	return inb(ioreg + 1);
}

static inline int superio_enter(int key, int addr, const char *name)
{
	if (!request_muxed_region(addr, 2, name)) {
		pr_err("I/O address 0x%04x already in use\n", addr);
		return -EBUSY;
	}
	outb(key, addr); /* Enter extended function mode */
	outb(key, addr); /* Again according to manual */

	return 0;
}

static inline void superio_exit(int key, int addr)
{
	outb(key, addr); /* Leave extended function mode */
	release_region(addr, 2);
}

static int shm_get_ready(unsigned char __iomem *shm_base_addr,
			 const struct nct6692_reg *reg)
{
	unsigned char pre_id, new_id;
	int loop = 0;

	iowrite8(reg->mod, shm_base_addr + SHM_WIN_MOD_OFFSET);
	iowrite8(reg->cmd, shm_base_addr + SHM_WIN_CMD_OFFSET);
	iowrite8(reg->sel, shm_base_addr + SHM_WIN_SEL_OFFSET);

	pre_id = ioread8(shm_base_addr + SHM_WIN_ID_OFFSET);
	iowrite8(VAL_SHM_WIN_CTRL_RD, shm_base_addr + SHM_WIN_CTL_OFFSET);

	/* Loop checking when interface is ready */
	while (loop < MAX_WAIT) {
		new_id = ioread8(shm_base_addr + SHM_WIN_ID_OFFSET);
		if (new_id != pre_id)
			return 0;
		loop++;
		usleep_range(10, 125);
	}
	return -ETIMEDOUT;
}

static int read_shm_win(unsigned char __iomem *shm_base_addr,
			const struct nct6692_reg *reg,
			unsigned char idx_offset,
			unsigned char *data)
{
	int err = shm_get_ready(shm_base_addr, reg);

	if (err)
		return err;
	*data = ioread8(shm_base_addr + SHM_WIN_DAT_OFFSET + reg->idx + idx_offset);
	return 0;
}

static int write_shm_win(unsigned char __iomem *shm_base_addr,
			 const struct nct6692_reg *reg,
			 unsigned char idx_offset,
			 unsigned char val)
{
	int err = shm_get_ready(shm_base_addr, reg);

	if (err)
		return err;
	iowrite8(val, shm_base_addr + SHM_WIN_DAT_OFFSET + reg->idx + idx_offset);
	iowrite8(VAL_SHM_WIN_CTRL_WR, shm_base_addr + SHM_WIN_CTL_OFFSET);
	err = shm_get_ready(shm_base_addr, reg);
	return err;
}

static int lenovo_se30_wdt_enable(struct lenovo_se30_wdt *data, unsigned int timeout)
{
	if (timeout) {
		int err = write_shm_win(data->shm_base_addr, &data->wdt_cfg, 0, WDT_CFG_RESET);

		if (err)
			return err;
	}
	return write_shm_win(data->shm_base_addr, &data->wdt_cnt, 0, timeout);
}

static int lenovo_se30_wdt_start(struct watchdog_device *wdog)
{
	struct lenovo_se30_wdt *data = watchdog_get_drvdata(wdog);

	return lenovo_se30_wdt_enable(data, wdog->timeout);
}

static int lenovo_se30_wdt_stop(struct watchdog_device *wdog)
{
	struct lenovo_se30_wdt *data = watchdog_get_drvdata(wdog);

	return lenovo_se30_wdt_enable(data, 0);
}

static unsigned int lenovo_se30_wdt_get_timeleft(struct watchdog_device *wdog)
{
	struct lenovo_se30_wdt *data = watchdog_get_drvdata(wdog);
	unsigned char timeleft;
	int err;

	err = read_shm_win(data->shm_base_addr, &data->wdt_cnt, 0, &timeleft);
	if (err)
		return 0;
	return timeleft;
}

static int lenovo_se30_wdt_ping(struct watchdog_device *wdt)
{
	struct lenovo_se30_wdt *data = watchdog_get_drvdata(wdt);
	int err = 0;

	/*
	 * Device does not support refreshing WDT_TIMER_REG register when
	 * the watchdog is active.  Need to disable, feed and enable again
	 */
	err = lenovo_se30_wdt_enable(data, 0);
	if (err)
		return err;

	err = write_shm_win(data->shm_base_addr, &data->wdt_cnt, 0, wdt->timeout);
	if (!err)
		err = lenovo_se30_wdt_enable(data, wdt->timeout);

	return err;
}

static const struct watchdog_info lenovo_se30_wdt_info = {
	.options	= WDIOF_SETTIMEOUT | WDIOF_KEEPALIVEPING |
			  WDIOF_MAGICCLOSE,
	.identity	= "Lenovo SE30 watchdog",
};

static const struct watchdog_ops lenovo_se30_wdt_ops = {
	.owner		= THIS_MODULE,
	.start		= lenovo_se30_wdt_start,
	.stop		= lenovo_se30_wdt_stop,
	.ping		= lenovo_se30_wdt_ping,
	.get_timeleft	= lenovo_se30_wdt_get_timeleft,
};

static int lenovo_se30_wdt_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct lenovo_se30_wdt *priv;
	unsigned long base_phys;
	unsigned short val;
	int err;

	err = superio_enter(UNLOCK_KEY, SIO_REG, LNV_SE30_NAME);
	if (err)
		return err;

	val = superio_inb(SIO_REG, CHIPID_REG) << 8;
	val |= superio_inb(SIO_REG, CHIPID_REG + 1);

	if ((val & CHIPID_MASK) != LNV_SE30_ID) {
		superio_exit(LOCK_KEY, SIO_REG);
		return -ENODEV;
	}

	superio_outb(SIO_REG, LDN_REG, LD_NUM_SHM);
	base_phys = (superio_inb(SIO_REG, LD_BASE_ADDR) |
			 (superio_inb(SIO_REG, LD_BASE_ADDR + 1) << 8) |
			 (superio_inb(SIO_REG, LD_BASE_ADDR + 2) << 16) |
			 (superio_inb(SIO_REG, LD_BASE_ADDR + 3) << 24)) &
			0xFFFFFFFF;

	superio_exit(LOCK_KEY, SIO_REG);
	if (base_phys == 0xFFFFFFFF || base_phys == 0)
		return -ENODEV;

	priv = devm_kzalloc(dev, sizeof(*priv), GFP_KERNEL);
	if (!priv)
		return -ENOMEM;

	if (!devm_request_mem_region(dev, base_phys, SHM_WIN_SIZE, LNV_SE30_NAME))
		return -EBUSY;

	priv->shm_base_addr = devm_ioremap(dev, base_phys, SHM_WIN_SIZE);

	priv->wdt_cfg.mod = WDT_MODULE;
	priv->wdt_cfg.idx = WDT_CFG_INDEX;
	priv->wdt_cnt.mod = WDT_MODULE;
	priv->wdt_cnt.idx = WDT_CNT_INDEX;

	priv->wdt.ops = &lenovo_se30_wdt_ops;
	priv->wdt.info = &lenovo_se30_wdt_info;
	priv->wdt.timeout = WATCHDOG_TIMEOUT; /* Set default timeout */
	priv->wdt.min_timeout = MIN_TIMEOUT;
	priv->wdt.max_timeout = MAX_TIMEOUT;
	priv->wdt.parent = dev;

	watchdog_init_timeout(&priv->wdt, timeout, dev);
	watchdog_set_drvdata(&priv->wdt, priv);
	watchdog_set_nowayout(&priv->wdt, nowayout);
	watchdog_stop_on_reboot(&priv->wdt);
	watchdog_stop_on_unregister(&priv->wdt);

	return devm_watchdog_register_device(dev, &priv->wdt);
}

static struct platform_device *pdev;

static struct platform_driver lenovo_se30_wdt_driver = {
	.driver = {
		.name = LNV_SE30_NAME,
	},
	.probe  = lenovo_se30_wdt_probe,
};

static int lenovo_se30_create_platform_device(const struct dmi_system_id *id)
{
	int err;

	pdev = platform_device_alloc(LNV_SE30_NAME, -1);
	if (!pdev)
		return -ENOMEM;

	err = platform_device_add(pdev);
	if (err)
		platform_device_put(pdev);

	return err;
}

static const struct dmi_system_id lenovo_se30_wdt_dmi_table[] __initconst = {
	{
		.ident = "LENOVO-SE30",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "11NA"),
		},
		.callback = lenovo_se30_create_platform_device,
	},
	{
		.ident = "LENOVO-SE30",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "11NB"),
		},
		.callback = lenovo_se30_create_platform_device,
	},
	{
		.ident = "LENOVO-SE30",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "11NC"),
		},
		.callback = lenovo_se30_create_platform_device,
	},
	{
		.ident = "LENOVO-SE30",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "11NH"),
		},
		.callback = lenovo_se30_create_platform_device,
	},
	{
		.ident = "LENOVO-SE30",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "11NJ"),
		},
		.callback = lenovo_se30_create_platform_device,
	},
	{
		.ident = "LENOVO-SE30",
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "LENOVO"),
			DMI_MATCH(DMI_PRODUCT_NAME, "11NK"),
		},
		.callback = lenovo_se30_create_platform_device,
	},
	{}
};
MODULE_DEVICE_TABLE(dmi, lenovo_se30_wdt_dmi_table);

static int __init lenovo_se30_wdt_init(void)
{
	if (!dmi_check_system(lenovo_se30_wdt_dmi_table))
		return -ENODEV;

	return platform_driver_register(&lenovo_se30_wdt_driver);
}

static void __exit lenovo_se30_wdt_exit(void)
{
	if (pdev)
		platform_device_unregister(pdev);
	platform_driver_unregister(&lenovo_se30_wdt_driver);
}

module_init(lenovo_se30_wdt_init);
module_exit(lenovo_se30_wdt_exit);

MODULE_AUTHOR("Mark Pearson <mpearson-lenovo@squebb.ca>");
MODULE_AUTHOR("David Ober <dober@lenovo.com>");
MODULE_DESCRIPTION("Lenovo SE30 watchdog driver");
MODULE_LICENSE("GPL");
