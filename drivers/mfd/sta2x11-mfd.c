/*
 * Copyright (c) 2009-2011 Wind River Systems, Inc.
 * Copyright (c) 2011 ST Microelectronics (Alessandro Rubini, Davide Ciminaghi)
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
#include <linux/seq_file.h>
#include <linux/platform_device.h>
#include <linux/mfd/core.h>
#include <linux/mfd/sta2x11-mfd.h>
#include <linux/regmap.h>

#include <asm/sta2x11.h>

static inline int __reg_within_range(unsigned int r,
				     unsigned int start,
				     unsigned int end)
{
	return ((r >= start) && (r <= end));
}

/* This describes STA2X11 MFD chip for us, we may have several */
struct sta2x11_mfd {
	struct sta2x11_instance *instance;
	struct regmap *regmap[sta2x11_n_mfd_plat_devs];
	spinlock_t lock[sta2x11_n_mfd_plat_devs];
	struct list_head list;
	void __iomem *regs[sta2x11_n_mfd_plat_devs];
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

static int sta2x11_mfd_add(struct pci_dev *pdev, gfp_t flags)
{
	int i;
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
	for (i = 0; i < ARRAY_SIZE(mfd->lock); i++)
		spin_lock_init(&mfd->lock[i]);
	mfd->instance = instance;
	list_add(&mfd->list, &sta2x11_mfd_list);
	return 0;
}

/* This function is exported and is not expected to fail */
u32 __sta2x11_mfd_mask(struct pci_dev *pdev, u32 reg, u32 mask, u32 val,
		       enum sta2x11_mfd_plat_dev index)
{
	struct sta2x11_mfd *mfd = sta2x11_mfd_find(pdev);
	u32 r;
	unsigned long flags;
	void __iomem *regs;

	if (!mfd) {
		dev_warn(&pdev->dev, ": can't access sctl regs\n");
		return 0;
	}

	regs = mfd->regs[index];
	if (!regs) {
		dev_warn(&pdev->dev, ": system ctl not initialized\n");
		return 0;
	}
	spin_lock_irqsave(&mfd->lock[index], flags);
	r = readl(regs + reg);
	r &= ~mask;
	r |= val;
	if (mask)
		writel(r, regs + reg);
	spin_unlock_irqrestore(&mfd->lock[index], flags);
	return r;
}
EXPORT_SYMBOL(__sta2x11_mfd_mask);

int sta2x11_mfd_get_regs_data(struct platform_device *dev,
			      enum sta2x11_mfd_plat_dev index,
			      void __iomem **regs,
			      spinlock_t **lock)
{
	struct pci_dev *pdev = *(struct pci_dev **)dev_get_platdata(&dev->dev);
	struct sta2x11_mfd *mfd;

	if (!pdev)
		return -ENODEV;
	mfd = sta2x11_mfd_find(pdev);
	if (!mfd)
		return -ENODEV;
	if (index >= sta2x11_n_mfd_plat_devs)
		return -ENODEV;
	*regs = mfd->regs[index];
	*lock = &mfd->lock[index];
	pr_debug("%s %d *regs = %p\n", __func__, __LINE__, *regs);
	return *regs ? 0 : -ENODEV;
}
EXPORT_SYMBOL(sta2x11_mfd_get_regs_data);

/*
 * Special sta2x11-mfd regmap lock/unlock functions
 */

static void sta2x11_regmap_lock(void *__lock)
{
	spinlock_t *lock = __lock;
	spin_lock(lock);
}

static void sta2x11_regmap_unlock(void *__lock)
{
	spinlock_t *lock = __lock;
	spin_unlock(lock);
}

/* OTP (one time programmable registers do not require locking */
static void sta2x11_regmap_nolock(void *__lock)
{
}

static const char *sta2x11_mfd_names[sta2x11_n_mfd_plat_devs] = {
	[sta2x11_sctl] = STA2X11_MFD_SCTL_NAME,
	[sta2x11_apbreg] = STA2X11_MFD_APBREG_NAME,
	[sta2x11_apb_soc_regs] = STA2X11_MFD_APB_SOC_REGS_NAME,
	[sta2x11_scr] = STA2X11_MFD_SCR_NAME,
};

static bool sta2x11_sctl_writeable_reg(struct device *dev, unsigned int reg)
{
	return !__reg_within_range(reg, SCTL_SCPCIECSBRST, SCTL_SCRSTSTA);
}

static struct regmap_config sta2x11_sctl_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.lock = sta2x11_regmap_lock,
	.unlock = sta2x11_regmap_unlock,
	.max_register = SCTL_SCRSTSTA,
	.writeable_reg = sta2x11_sctl_writeable_reg,
};

static bool sta2x11_scr_readable_reg(struct device *dev, unsigned int reg)
{
	return (reg == STA2X11_SECR_CR) ||
		__reg_within_range(reg, STA2X11_SECR_FVR0, STA2X11_SECR_FVR1);
}

static bool sta2x11_scr_writeable_reg(struct device *dev, unsigned int reg)
{
	return false;
}

static struct regmap_config sta2x11_scr_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.lock = sta2x11_regmap_nolock,
	.unlock = sta2x11_regmap_nolock,
	.max_register = STA2X11_SECR_FVR1,
	.readable_reg = sta2x11_scr_readable_reg,
	.writeable_reg = sta2x11_scr_writeable_reg,
};

static bool sta2x11_apbreg_readable_reg(struct device *dev, unsigned int reg)
{
	/* Two blocks (CAN and MLB, SARAC) 0x100 bytes apart */
	if (reg >= APBREG_BSR_SARAC)
		reg -= APBREG_BSR_SARAC;
	switch (reg) {
	case APBREG_BSR:
	case APBREG_PAER:
	case APBREG_PWAC:
	case APBREG_PRAC:
	case APBREG_PCG:
	case APBREG_PUR:
	case APBREG_EMU_PCG:
		return true;
	default:
		return false;
	}
}

static bool sta2x11_apbreg_writeable_reg(struct device *dev, unsigned int reg)
{
	if (reg >= APBREG_BSR_SARAC)
		reg -= APBREG_BSR_SARAC;
	if (!sta2x11_apbreg_readable_reg(dev, reg))
		return false;
	return reg != APBREG_PAER;
}

static struct regmap_config sta2x11_apbreg_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.lock = sta2x11_regmap_lock,
	.unlock = sta2x11_regmap_unlock,
	.max_register = APBREG_EMU_PCG_SARAC,
	.readable_reg = sta2x11_apbreg_readable_reg,
	.writeable_reg = sta2x11_apbreg_writeable_reg,
};

static bool sta2x11_apb_soc_regs_readable_reg(struct device *dev,
					      unsigned int reg)
{
	return reg <= PCIE_SoC_INT_ROUTER_STATUS3_REG ||
		__reg_within_range(reg, DMA_IP_CTRL_REG, SPARE3_RESERVED) ||
		__reg_within_range(reg, MASTER_LOCK_REG,
				   SYSTEM_CONFIG_STATUS_REG) ||
		reg == MSP_CLK_CTRL_REG ||
		__reg_within_range(reg, COMPENSATION_REG1, TEST_CTL_REG);
}

static bool sta2x11_apb_soc_regs_writeable_reg(struct device *dev,
					       unsigned int reg)
{
	if (!sta2x11_apb_soc_regs_readable_reg(dev, reg))
		return false;
	switch (reg) {
	case PCIE_COMMON_CLOCK_CONFIG_0_4_0:
	case SYSTEM_CONFIG_STATUS_REG:
	case COMPENSATION_REG1:
	case PCIE_SoC_INT_ROUTER_STATUS0_REG...PCIE_SoC_INT_ROUTER_STATUS3_REG:
	case PCIE_PM_STATUS_0_PORT_0_4...PCIE_PM_STATUS_7_0_EP4:
		return false;
	default:
		return true;
	}
}

static struct regmap_config sta2x11_apb_soc_regs_regmap_config = {
	.reg_bits = 32,
	.reg_stride = 4,
	.val_bits = 32,
	.lock = sta2x11_regmap_lock,
	.unlock = sta2x11_regmap_unlock,
	.max_register = TEST_CTL_REG,
	.readable_reg = sta2x11_apb_soc_regs_readable_reg,
	.writeable_reg = sta2x11_apb_soc_regs_writeable_reg,
};

static struct regmap_config *
sta2x11_mfd_regmap_configs[sta2x11_n_mfd_plat_devs] = {
	[sta2x11_sctl] = &sta2x11_sctl_regmap_config,
	[sta2x11_apbreg] = &sta2x11_apbreg_regmap_config,
	[sta2x11_apb_soc_regs] = &sta2x11_apb_soc_regs_regmap_config,
	[sta2x11_scr] = &sta2x11_scr_regmap_config,
};

/* Probe for the four platform devices */

static int sta2x11_mfd_platform_probe(struct platform_device *dev,
				      enum sta2x11_mfd_plat_dev index)
{
	struct pci_dev **pdev;
	struct sta2x11_mfd *mfd;
	struct resource *res;
	const char *name = sta2x11_mfd_names[index];
	struct regmap_config *regmap_config = sta2x11_mfd_regmap_configs[index];

	pdev = dev_get_platdata(&dev->dev);
	mfd = sta2x11_mfd_find(*pdev);
	if (!mfd)
		return -ENODEV;
	if (!regmap_config)
		return -ENODEV;

	res = platform_get_resource(dev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENOMEM;

	if (!request_mem_region(res->start, resource_size(res), name))
		return -EBUSY;

	mfd->regs[index] = ioremap(res->start, resource_size(res));
	if (!mfd->regs[index]) {
		release_mem_region(res->start, resource_size(res));
		return -ENOMEM;
	}
	regmap_config->lock_arg = &mfd->lock;
	/*
	   No caching, registers could be reached both via regmap and via
	   void __iomem *
	*/
	regmap_config->cache_type = REGCACHE_NONE;
	mfd->regmap[index] = devm_regmap_init_mmio(&dev->dev, mfd->regs[index],
						   regmap_config);
	WARN_ON(!mfd->regmap[index]);

	return 0;
}

static int sta2x11_sctl_probe(struct platform_device *dev)
{
	return sta2x11_mfd_platform_probe(dev, sta2x11_sctl);
}

static int sta2x11_apbreg_probe(struct platform_device *dev)
{
	return sta2x11_mfd_platform_probe(dev, sta2x11_apbreg);
}

static int sta2x11_apb_soc_regs_probe(struct platform_device *dev)
{
	return sta2x11_mfd_platform_probe(dev, sta2x11_apb_soc_regs);
}

static int sta2x11_scr_probe(struct platform_device *dev)
{
	return sta2x11_mfd_platform_probe(dev, sta2x11_scr);
}

/* The three platform drivers */
static struct platform_driver sta2x11_sctl_platform_driver = {
	.driver = {
		.name	= STA2X11_MFD_SCTL_NAME,
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
		.name	= STA2X11_MFD_APBREG_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= sta2x11_apbreg_probe,
};

static int __init sta2x11_apbreg_init(void)
{
	pr_info("%s\n", __func__);
	return platform_driver_register(&sta2x11_platform_driver);
}

static struct platform_driver sta2x11_apb_soc_regs_platform_driver = {
	.driver = {
		.name	= STA2X11_MFD_APB_SOC_REGS_NAME,
		.owner	= THIS_MODULE,
	},
	.probe		= sta2x11_apb_soc_regs_probe,
};

static int __init sta2x11_apb_soc_regs_init(void)
{
	pr_info("%s\n", __func__);
	return platform_driver_register(&sta2x11_apb_soc_regs_platform_driver);
}

static struct platform_driver sta2x11_scr_platform_driver = {
	.driver = {
		.name = STA2X11_MFD_SCR_NAME,
		.owner = THIS_MODULE,
	},
	.probe = sta2x11_scr_probe,
};

static int __init sta2x11_scr_init(void)
{
	pr_info("%s\n", __func__);
	return platform_driver_register(&sta2x11_scr_platform_driver);
}


/*
 * What follows are the PCI devices that host the above pdevs.
 * Each logic block is 4kB and they are all consecutive: we use this info.
 */

/* Mfd 0 device */

/* Mfd 0, Bar 0 */
enum mfd0_bar0_cells {
	STA2X11_GPIO_0 = 0,
	STA2X11_GPIO_1,
	STA2X11_GPIO_2,
	STA2X11_GPIO_3,
	STA2X11_SCTL,
	STA2X11_SCR,
	STA2X11_TIME,
};
/* Mfd 0 , Bar 1 */
enum mfd0_bar1_cells {
	STA2X11_APBREG = 0,
};
#define CELL_4K(_name, _cell) { \
		.name = _name, \
		.start = _cell * 4096, .end = _cell * 4096 + 4095, \
		.flags = IORESOURCE_MEM, \
		}

static const struct resource gpio_resources[] = {
	{
		/* 4 consecutive cells, 1 driver */
		.name = STA2X11_MFD_GPIO_NAME,
		.start = 0,
		.end = (4 * 4096) - 1,
		.flags = IORESOURCE_MEM,
	}
};
static const struct resource sctl_resources[] = {
	CELL_4K(STA2X11_MFD_SCTL_NAME, STA2X11_SCTL),
};
static const struct resource scr_resources[] = {
	CELL_4K(STA2X11_MFD_SCR_NAME, STA2X11_SCR),
};
static const struct resource time_resources[] = {
	CELL_4K(STA2X11_MFD_TIME_NAME, STA2X11_TIME),
};

static const struct resource apbreg_resources[] = {
	CELL_4K(STA2X11_MFD_APBREG_NAME, STA2X11_APBREG),
};

#define DEV(_name, _r) \
	{ .name = _name, .num_resources = ARRAY_SIZE(_r), .resources = _r, }

static struct mfd_cell sta2x11_mfd0_bar0[] = {
	/* offset 0: we add pdata later */
	DEV(STA2X11_MFD_GPIO_NAME, gpio_resources),
	DEV(STA2X11_MFD_SCTL_NAME, sctl_resources),
	DEV(STA2X11_MFD_SCR_NAME,  scr_resources),
	DEV(STA2X11_MFD_TIME_NAME, time_resources),
};

static struct mfd_cell sta2x11_mfd0_bar1[] = {
	DEV(STA2X11_MFD_APBREG_NAME, apbreg_resources),
};

/* Mfd 1 devices */

/* Mfd 1, Bar 0 */
enum mfd1_bar0_cells {
	STA2X11_VIC = 0,
};

/* Mfd 1, Bar 1 */
enum mfd1_bar1_cells {
	STA2X11_APB_SOC_REGS = 0,
};

static const struct resource vic_resources[] = {
	CELL_4K(STA2X11_MFD_VIC_NAME, STA2X11_VIC),
};

static const struct resource apb_soc_regs_resources[] = {
	CELL_4K(STA2X11_MFD_APB_SOC_REGS_NAME, STA2X11_APB_SOC_REGS),
};

static struct mfd_cell sta2x11_mfd1_bar0[] = {
	DEV(STA2X11_MFD_VIC_NAME, vic_resources),
};

static struct mfd_cell sta2x11_mfd1_bar1[] = {
	DEV(STA2X11_MFD_APB_SOC_REGS_NAME, apb_soc_regs_resources),
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

struct sta2x11_mfd_bar_setup_data {
	struct mfd_cell *cells;
	int ncells;
};

struct sta2x11_mfd_setup_data {
	struct sta2x11_mfd_bar_setup_data bars[2];
};

#define STA2X11_MFD0 0
#define STA2X11_MFD1 1

static struct sta2x11_mfd_setup_data mfd_setup_data[] = {
	/* Mfd 0: gpio, sctl, scr, timers / apbregs */
	[STA2X11_MFD0] = {
		.bars = {
			[0] = {
				.cells = sta2x11_mfd0_bar0,
				.ncells = ARRAY_SIZE(sta2x11_mfd0_bar0),
			},
			[1] = {
				.cells = sta2x11_mfd0_bar1,
				.ncells = ARRAY_SIZE(sta2x11_mfd0_bar1),
			},
		},
	},
	/* Mfd 1: vic / apb-soc-regs */
	[STA2X11_MFD1] = {
		.bars = {
			[0] = {
				.cells = sta2x11_mfd1_bar0,
				.ncells = ARRAY_SIZE(sta2x11_mfd1_bar0),
			},
			[1] = {
				.cells = sta2x11_mfd1_bar1,
				.ncells = ARRAY_SIZE(sta2x11_mfd1_bar1),
			},
		},
	},
};

static void sta2x11_mfd_setup(struct pci_dev *pdev,
			      struct sta2x11_mfd_setup_data *sd)
{
	int i, j;
	for (i = 0; i < ARRAY_SIZE(sd->bars); i++)
		for (j = 0; j < sd->bars[i].ncells; j++) {
			sd->bars[i].cells[j].pdata_size = sizeof(pdev);
			sd->bars[i].cells[j].platform_data = &pdev;
		}
}

static int sta2x11_mfd_probe(struct pci_dev *pdev,
			     const struct pci_device_id *pci_id)
{
	int err, i;
	struct sta2x11_mfd_setup_data *setup_data;

	dev_info(&pdev->dev, "%s\n", __func__);

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "Can't enable device.\n");
		return err;
	}

	err = pci_enable_msi(pdev);
	if (err)
		dev_info(&pdev->dev, "Enable msi failed\n");

	setup_data = pci_id->device == PCI_DEVICE_ID_STMICRO_GPIO ?
		&mfd_setup_data[STA2X11_MFD0] :
		&mfd_setup_data[STA2X11_MFD1];

	/* platform data is the pci device for all of them */
	sta2x11_mfd_setup(pdev, setup_data);

	/* Record this pdev before mfd_add_devices: their probe looks for it */
	if (!sta2x11_mfd_find(pdev))
		sta2x11_mfd_add(pdev, GFP_ATOMIC);

	/* Just 2 bars for all mfd's at present */
	for (i = 0; i < 2; i++) {
		err = mfd_add_devices(&pdev->dev, -1,
				      setup_data->bars[i].cells,
				      setup_data->bars[i].ncells,
				      &pdev->resource[i],
				      0, NULL);
		if (err) {
			dev_err(&pdev->dev,
				"mfd_add_devices[%d] failed: %d\n", i, err);
			goto err_disable;
		}
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
	{PCI_DEVICE(PCI_VENDOR_ID_STMICRO, PCI_DEVICE_ID_STMICRO_VIC)},
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
subsys_initcall(sta2x11_apb_soc_regs_init);
subsys_initcall(sta2x11_scr_init);
rootfs_initcall(sta2x11_mfd_init);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Wind River");
MODULE_DESCRIPTION("STA2x11 mfd for GPIO, SCTL and APBREG");
MODULE_DEVICE_TABLE(pci, sta2x11_mfd_tbl);
