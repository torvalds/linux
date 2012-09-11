/*
 * Copyright (c) 2009-2011 Wind River Systems, Inc.
 * Copyright (c) 2011 ST Microelectronics (Alessandro Rubini)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/device.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/pci.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/sta2x11-mfd.h>

#include <asm/sta2x11.h>

/* This describes STA2X11 MFD chip for us, we may have several */
struct sta2x11_mfd {
	struct sta2x11_instance *instance;
	spinlock_t lock;
	struct list_head list;
	void __iomem *sctl_regs;
	void __iomem *apbreg_regs;
};

static LIST_HEAD(sta2x11_mfd_list);

/* Three functions to act on the list */
static struct sta2x11_mfd *sta2x11_mfd_find(struct pci_dev *pdev)
{
	struct sta2x11_instance *instance;
	struct sta2x11_mfd *mfd;

	if (!pdev && !list_empty(&sta2x11_mfd_list)) {
		pr_warning("%s: Unspecified device, "
			    "using first instance\n", __func__);
		return list_entry(sta2x11_mfd_list.next,
				  struct sta2x11_mfd, list);
	}

	instance = sta2x11_get_instance(pdev);
	if (!instance)
		return NULL;
	list_for_each_entry(mfd, &sta2x11_mfd_list, list) {
		if (mfd->instance == instance)
			return mfd;
	}
	return NULL;
}

static int __devinit sta2x11_mfd_add(struct pci_dev *pdev, gfp_t flags)
{
	struct sta2x11_mfd *mfd = sta2x11_mfd_find(pdev);
	struct sta2x11_instance *instance;

	if (mfd)
		return -EBUSY;
	instance = sta2x11_get_instance(pdev);
	if (!instance)
		return -EINVAL;
	mfd = kzalloc(sizeof(*mfd), flags);
	if (!mfd)
		return -ENOMEM;
	INIT_LIST_HEAD(&mfd->list);
	spin_lock_init(&mfd->lock);
	mfd->instance = instance;
	list_add(&mfd->list, &sta2x11_mfd_list);
	return 0;
}

static int __devexit mfd_remove(struct pci_dev *pdev)
{
	struct sta2x11_mfd *mfd = sta2x11_mfd_find(pdev);

	if (!mfd)
		return -ENODEV;
	list_del(&mfd->list);
	kfree(mfd);
	return 0;
}

/* These two functions are exported and are not expected to fail */
u32 sta2x11_sctl_mask(struct pci_dev *pdev, u32 reg, u32 mask, u32 val)
{
	struct sta2x11_mfd *mfd = sta2x11_mfd_find(pdev);
	u32 r;
	unsigned long flags;

	if (!mfd) {
		dev_warn(&pdev->dev, ": can't access sctl regs\n");
		return 0;
	}
	if (!mfd->sctl_regs) {
		dev_warn(&pdev->dev, ": system ctl not initialized\n");
		return 0;
	}
	spin_lock_irqsave(&mfd->lock, flags);
	r = readl(mfd->sctl_regs + reg);
	r &= ~mask;
	r |= val;
	if (mask)
		writel(r, mfd->sctl_regs + reg);
	spin_unlock_irqrestore(&mfd->lock, flags);
	return r;
}
EXPORT_SYMBOL(sta2x11_sctl_mask);

u32 sta2x11_apbreg_mask(struct pci_dev *pdev, u32 reg, u32 mask, u32 val)
{
	struct sta2x11_mfd *mfd = sta2x11_mfd_find(pdev);
	u32 r;
	unsigned long flags;

	if (!mfd) {
		dev_warn(&pdev->dev, ": can't access apb regs\n");
		return 0;
	}
	if (!mfd->apbreg_regs) {
		dev_warn(&pdev->dev, ": apb bridge not initialized\n");
		return 0;
	}
	spin_lock_irqsave(&mfd->lock, flags);
	r = readl(mfd->apbreg_regs + reg);
	r &= ~mask;
	r |= val;
	if (mask)
		writel(r, mfd->apbreg_regs + reg);
	spin_unlock_irqrestore(&mfd->lock, flags);
	return r;
}
EXPORT_SYMBOL(sta2x11_apbreg_mask);

/* Two debugfs files, for our registers (FIXME: one instance only) */
#define REG(regname) {.name = #regname, .offset = SCTL_ ## regname}
static struct debugfs_reg32 sta2x11_sctl_regs[] = {
	REG(SCCTL), REG(ARMCFG), REG(SCPLLCTL), REG(SCPLLFCTRL),
	REG(SCRESFRACT), REG(SCRESCTRL1), REG(SCRESXTRL2), REG(SCPEREN0),
	REG(SCPEREN1), REG(SCPEREN2), REG(SCGRST), REG(SCPCIPMCR1),
	REG(SCPCIPMCR2), REG(SCPCIPMSR1), REG(SCPCIPMSR2), REG(SCPCIPMSR3),
	REG(SCINTREN), REG(SCRISR), REG(SCCLKSTAT0), REG(SCCLKSTAT1),
	REG(SCCLKSTAT2), REG(SCRSTSTA),
};
#undef REG

static struct debugfs_regset32 sctl_regset = {
	.regs = sta2x11_sctl_regs,
	.nregs = ARRAY_SIZE(sta2x11_sctl_regs),
};

#define REG(regname) {.name = #regname, .offset = regname}
static struct debugfs_reg32 sta2x11_apbreg_regs[] = {
	REG(APBREG_BSR), REG(APBREG_PAER), REG(APBREG_PWAC), REG(APBREG_PRAC),
	REG(APBREG_PCG), REG(APBREG_PUR), REG(APBREG_EMU_PCG),
};
#undef REG

static struct debugfs_regset32 apbreg_regset = {
	.regs = sta2x11_apbreg_regs,
	.nregs = ARRAY_SIZE(sta2x11_apbreg_regs),
};

static struct dentry *sta2x11_sctl_debugfs;
static struct dentry *sta2x11_apbreg_debugfs;

/* Probe for the two platform devices */
static int sta2x11_sctl_probe(struct platform_device *dev)
{
	struct pci_dev **pdev;
	struct sta2x11_mfd *mfd;
	struct resource *res;

	pdev = dev->dev.platform_data;
	mfd = sta2x11_mfd_find(*pdev);
	if (!mfd)
		return -ENODEV;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOMEM;

	if (!request_mem_region(res->start, resource_size(res),
				"sta2x11-sctl"))
		return -EBUSY;

	mfd->sctl_regs = ioremap(res->start, resource_size(res));
	if (!mfd->sctl_regs) {
		release_mem_region(res->start, resource_size(res));
		return -ENOMEM;
	}
	sctl_regset.base = mfd->sctl_regs;
	sta2x11_sctl_debugfs = debugfs_create_regset32("sta2x11-sctl",
						  S_IFREG | S_IRUGO,
						  NULL, &sctl_regset);
	return 0;
}

static int sta2x11_apbreg_probe(struct platform_device *dev)
{
	struct pci_dev **pdev;
	struct sta2x11_mfd *mfd;
	struct resource *res;

	pdev = dev->dev.platform_data;
	dev_dbg(&dev->dev, "%s: pdata is %p\n", __func__, pdev);
	dev_dbg(&dev->dev, "%s: *pdata is %p\n", __func__, *pdev);

	mfd = sta2x11_mfd_find(*pdev);
	if (!mfd)
		return -ENODEV;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOMEM;

	if (!request_mem_region(res->start, resource_size(res),
				"sta2x11-apbreg"))
		return -EBUSY;

	mfd->apbreg_regs = ioremap(res->start, resource_size(res));
	if (!mfd->apbreg_regs) {
		release_mem_region(res->start, resource_size(res));
		return -ENOMEM;
	}
	dev_dbg(&dev->dev, "%s: regbase %p\n", __func__, mfd->apbreg_regs);

	apbreg_regset.base = mfd->apbreg_regs;
	sta2x11_apbreg_debugfs = debugfs_create_regset32("sta2x11-apbreg",
						  S_IFREG | S_IRUGO,
						  NULL, &apbreg_regset);
	return 0;
}

/* The two platform drivers */
static struct platform_driver sta2x11_sctl_platform_driver = {
	.driver = {
		.name	= "sta2x11-sctl",
		.owner	= THIS_MODULE,
	},
	.probe		= sta2x11_sctl_probe,
};

static int __init sta2x11_sctl_init(void)
{
	pr_info("%s\n", __func__);
	return platform_driver_register(&sta2x11_sctl_platform_driver);
}

static struct platform_driver sta2x11_platform_driver = {
	.driver = {
		.name	= "sta2x11-apbreg",
		.owner	= THIS_MODULE,
	},
	.probe		= sta2x11_apbreg_probe,
};

static int __init sta2x11_apbreg_init(void)
{
	pr_info("%s\n", __func__);
	return platform_driver_register(&sta2x11_platform_driver);
}

/*
 * What follows is the PCI device that hosts the above two pdevs.
 * Each logic block is 4kB and they are all consecutive: we use this info.
 */

/* Bar 0 */
enum bar0_cells {
	STA2X11_GPIO_0 = 0,
	STA2X11_GPIO_1,
	STA2X11_GPIO_2,
	STA2X11_GPIO_3,
	STA2X11_SCTL,
	STA2X11_SCR,
	STA2X11_TIME,
};
/* Bar 1 */
enum bar1_cells {
	STA2X11_APBREG = 0,
};
#define CELL_4K(_name, _cell) { \
		.name = _name, \
		.start = _cell * 4096, .end = _cell * 4096 + 4095, \
		.flags = IORESOURCE_MEM, \
		}

static const __devinitconst struct resource gpio_resources[] = {
	{
		.name = "sta2x11_gpio", /* 4 consecutive cells, 1 driver */
		.start = 0,
		.end = (4 * 4096) - 1,
		.flags = IORESOURCE_MEM,
	}
};
static const __devinitconst struct resource sctl_resources[] = {
	CELL_4K("sta2x11-sctl", STA2X11_SCTL),
};
static const __devinitconst struct resource scr_resources[] = {
	CELL_4K("sta2x11-scr", STA2X11_SCR),
};
static const __devinitconst struct resource time_resources[] = {
	CELL_4K("sta2x11-time", STA2X11_TIME),
};

static const __devinitconst struct resource apbreg_resources[] = {
	CELL_4K("sta2x11-apbreg", STA2X11_APBREG),
};

#define DEV(_name, _r) \
	{ .name = _name, .num_resources = ARRAY_SIZE(_r), .resources = _r, }

static __devinitdata struct mfd_cell sta2x11_mfd_bar0[] = {
	DEV("sta2x11-gpio", gpio_resources), /* offset 0: we add pdata later */
	DEV("sta2x11-sctl", sctl_resources),
	DEV("sta2x11-scr", scr_resources),
	DEV("sta2x11-time", time_resources),
};

static __devinitdata struct mfd_cell sta2x11_mfd_bar1[] = {
	DEV("sta2x11-apbreg", apbreg_resources),
};

static int sta2x11_mfd_suspend(struct pci_dev *pdev, pm_message_t state)
{
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, pci_choose_state(pdev, state));

	return 0;
}

static int sta2x11_mfd_resume(struct pci_dev *pdev)
{
	int err;

	pci_set_power_state(pdev, 0);
	err = pci_enable_device(pdev);
	if (err)
		return err;
	pci_restore_state(pdev);

	return 0;
}

static int __devinit sta2x11_mfd_probe(struct pci_dev *pdev,
				       const struct pci_device_id *pci_id)
{
	int err, i;
	struct sta2x11_gpio_pdata *gpio_data;

	dev_info(&pdev->dev, "%s\n", __func__);

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Can't enable device.\n");
		return err;
	}

	err = pci_enable_msi(pdev);
	if (err)
		dev_info(&pdev->dev, "Enable msi failed\n");

	/* Read gpio config data as pci device's platform data */
	gpio_data = dev_get_platdata(&pdev->dev);
	if (!gpio_data)
		dev_warn(&pdev->dev, "no gpio configuration\n");

	dev_dbg(&pdev->dev, "%s, gpio_data = %p (%p)\n", __func__,
		gpio_data, &gpio_data);
	dev_dbg(&pdev->dev, "%s, pdev = %p (%p)\n", __func__,
		pdev, &pdev);

	/* platform data is the pci device for all of them */
	for (i = 0; i < ARRAY_SIZE(sta2x11_mfd_bar0); i++) {
		sta2x11_mfd_bar0[i].pdata_size = sizeof(pdev);
		sta2x11_mfd_bar0[i].platform_data = &pdev;
	}
	sta2x11_mfd_bar1[0].pdata_size = sizeof(pdev);
	sta2x11_mfd_bar1[0].platform_data = &pdev;

	/* Record this pdev before mfd_add_devices: their probe looks for it */
	sta2x11_mfd_add(pdev, GFP_ATOMIC);


	err = mfd_add_devices(&pdev->dev, -1,
			      sta2x11_mfd_bar0,
			      ARRAY_SIZE(sta2x11_mfd_bar0),
			      &pdev->resource[0],
			      0, NULL);
	if (err) {
		dev_err(&pdev->dev, "mfd_add_devices[0] failed: %d\n", err);
		goto err_disable;
	}

	err = mfd_add_devices(&pdev->dev, -1,
			      sta2x11_mfd_bar1,
			      ARRAY_SIZE(sta2x11_mfd_bar1),
			      &pdev->resource[1],
			      0, NULL);
	if (err) {
		dev_err(&pdev->dev, "mfd_add_devices[1] failed: %d\n", err);
		goto err_disable;
	}

	return 0;

err_disable:
	mfd_remove_devices(&pdev->dev);
	pci_disable_device(pdev);
	pci_disable_msi(pdev);
	return err;
}

static DEFINE_PCI_DEVICE_TABLE(sta2x11_mfd_tbl) = {
	{PCI_DEVICE(PCI_VENDOR_ID_STMICRO, PCI_DEVICE_ID_STMICRO_GPIO)},
	{0,},
};

static struct pci_driver sta2x11_mfd_driver = {
	.name =		"sta2x11-mfd",
	.id_table =	sta2x11_mfd_tbl,
	.probe =	sta2x11_mfd_probe,
	.suspend =	sta2x11_mfd_suspend,
	.resume =	sta2x11_mfd_resume,
};

static int __init sta2x11_mfd_init(void)
{
	pr_info("%s\n", __func__);
	return pci_register_driver(&sta2x11_mfd_driver);
}

/*
 * All of this must be ready before "normal" devices like MMCI appear.
 * But MFD (the pci device) can't be too early. The following choice
 * prepares platform drivers very early and probe the PCI device later,
 * but before other PCI devices.
 */
subsys_initcall(sta2x11_apbreg_init);
subsys_initcall(sta2x11_sctl_init);
rootfs_initcall(sta2x11_mfd_init);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Wind River");
MODULE_DESCRIPTION("STA2x11 mfd for GPIO, SCTL and APBREG");
MODULE_DEVICE_TABLE(pci, sta2x11_mfd_tbl);
