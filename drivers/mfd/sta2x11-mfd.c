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

/* This function is exported and is not expected to fail */
u32 __sta2x11_mfd_mask(struct pci_dev *pdev, u32 reg, u32 mask, u32 val,
		       enum sta2x11_mfd_plat_dev index)
{
	struct sta2x11_mfd *mfd = sta2x11_mfd_find(pdev);
	u32 r;
	unsigned long flags;
	void __iomem *regs = mfd->regs[index];

	if (!mfd) {
		dev_warn(&pdev->dev, ": can't access sctl regs\n");
		return 0;
	}
	if (!regs) {
		dev_warn(&pdev->dev, ": system ctl not initialized\n");
		return 0;
	}
	spin_lock_irqsave(&mfd->lock, flags);
	r = readl(regs + reg);
	r &= ~mask;
	r |= val;
	if (mask)
		writel(r, regs + reg);
	spin_unlock_irqrestore(&mfd->lock, flags);
	return r;
}
EXPORT_SYMBOL(__sta2x11_mfd_mask);

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

#define REG(regname) {.name = #regname, .offset = regname}
static struct debugfs_reg32 sta2x11_apb_soc_regs_regs[] = {
	REG(PCIE_EP1_FUNC3_0_INTR_REG), REG(PCIE_EP1_FUNC7_4_INTR_REG),
	REG(PCIE_EP2_FUNC3_0_INTR_REG), REG(PCIE_EP2_FUNC7_4_INTR_REG),
	REG(PCIE_EP3_FUNC3_0_INTR_REG), REG(PCIE_EP3_FUNC7_4_INTR_REG),
	REG(PCIE_EP4_FUNC3_0_INTR_REG), REG(PCIE_EP4_FUNC7_4_INTR_REG),
	REG(PCIE_INTR_ENABLE0_REG), REG(PCIE_INTR_ENABLE1_REG),
	REG(PCIE_EP1_FUNC_TC_REG), REG(PCIE_EP2_FUNC_TC_REG),
	REG(PCIE_EP3_FUNC_TC_REG), REG(PCIE_EP4_FUNC_TC_REG),
	REG(PCIE_EP1_FUNC_F_REG), REG(PCIE_EP2_FUNC_F_REG),
	REG(PCIE_EP3_FUNC_F_REG), REG(PCIE_EP4_FUNC_F_REG),
	REG(PCIE_PAB_AMBA_SW_RST_REG), REG(PCIE_PM_STATUS_0_PORT_0_4),
	REG(PCIE_PM_STATUS_7_0_EP1), REG(PCIE_PM_STATUS_7_0_EP2),
	REG(PCIE_PM_STATUS_7_0_EP3), REG(PCIE_PM_STATUS_7_0_EP4),
	REG(PCIE_DEV_ID_0_EP1_REG), REG(PCIE_CC_REV_ID_0_EP1_REG),
	REG(PCIE_DEV_ID_1_EP1_REG), REG(PCIE_CC_REV_ID_1_EP1_REG),
	REG(PCIE_DEV_ID_2_EP1_REG), REG(PCIE_CC_REV_ID_2_EP1_REG),
	REG(PCIE_DEV_ID_3_EP1_REG), REG(PCIE_CC_REV_ID_3_EP1_REG),
	REG(PCIE_DEV_ID_4_EP1_REG), REG(PCIE_CC_REV_ID_4_EP1_REG),
	REG(PCIE_DEV_ID_5_EP1_REG), REG(PCIE_CC_REV_ID_5_EP1_REG),
	REG(PCIE_DEV_ID_6_EP1_REG), REG(PCIE_CC_REV_ID_6_EP1_REG),
	REG(PCIE_DEV_ID_7_EP1_REG), REG(PCIE_CC_REV_ID_7_EP1_REG),
	REG(PCIE_DEV_ID_0_EP2_REG), REG(PCIE_CC_REV_ID_0_EP2_REG),
	REG(PCIE_DEV_ID_1_EP2_REG), REG(PCIE_CC_REV_ID_1_EP2_REG),
	REG(PCIE_DEV_ID_2_EP2_REG), REG(PCIE_CC_REV_ID_2_EP2_REG),
	REG(PCIE_DEV_ID_3_EP2_REG), REG(PCIE_CC_REV_ID_3_EP2_REG),
	REG(PCIE_DEV_ID_4_EP2_REG), REG(PCIE_CC_REV_ID_4_EP2_REG),
	REG(PCIE_DEV_ID_5_EP2_REG), REG(PCIE_CC_REV_ID_5_EP2_REG),
	REG(PCIE_DEV_ID_6_EP2_REG), REG(PCIE_CC_REV_ID_6_EP2_REG),
	REG(PCIE_DEV_ID_7_EP2_REG), REG(PCIE_CC_REV_ID_7_EP2_REG),
	REG(PCIE_DEV_ID_0_EP3_REG), REG(PCIE_CC_REV_ID_0_EP3_REG),
	REG(PCIE_DEV_ID_1_EP3_REG), REG(PCIE_CC_REV_ID_1_EP3_REG),
	REG(PCIE_DEV_ID_2_EP3_REG), REG(PCIE_CC_REV_ID_2_EP3_REG),
	REG(PCIE_DEV_ID_3_EP3_REG), REG(PCIE_CC_REV_ID_3_EP3_REG),
	REG(PCIE_DEV_ID_4_EP3_REG), REG(PCIE_CC_REV_ID_4_EP3_REG),
	REG(PCIE_DEV_ID_5_EP3_REG), REG(PCIE_CC_REV_ID_5_EP3_REG),
	REG(PCIE_DEV_ID_6_EP3_REG), REG(PCIE_CC_REV_ID_6_EP3_REG),
	REG(PCIE_DEV_ID_7_EP3_REG), REG(PCIE_CC_REV_ID_7_EP3_REG),
	REG(PCIE_DEV_ID_0_EP4_REG), REG(PCIE_CC_REV_ID_0_EP4_REG),
	REG(PCIE_DEV_ID_1_EP4_REG), REG(PCIE_CC_REV_ID_1_EP4_REG),
	REG(PCIE_DEV_ID_2_EP4_REG), REG(PCIE_CC_REV_ID_2_EP4_REG),
	REG(PCIE_DEV_ID_3_EP4_REG), REG(PCIE_CC_REV_ID_3_EP4_REG),
	REG(PCIE_DEV_ID_4_EP4_REG), REG(PCIE_CC_REV_ID_4_EP4_REG),
	REG(PCIE_DEV_ID_5_EP4_REG), REG(PCIE_CC_REV_ID_5_EP4_REG),
	REG(PCIE_DEV_ID_6_EP4_REG), REG(PCIE_CC_REV_ID_6_EP4_REG),
	REG(PCIE_DEV_ID_7_EP4_REG), REG(PCIE_CC_REV_ID_7_EP4_REG),
	REG(PCIE_SUBSYS_VEN_ID_REG), REG(PCIE_COMMON_CLOCK_CONFIG_0_4_0),
	REG(PCIE_MIPHYP_SSC_EN_REG), REG(PCIE_MIPHYP_ADDR_REG),
	REG(PCIE_L1_ASPM_READY_REG), REG(PCIE_EXT_CFG_RDY_REG),
	REG(PCIE_SoC_INT_ROUTER_STATUS0_REG),
	REG(PCIE_SoC_INT_ROUTER_STATUS1_REG),
	REG(PCIE_SoC_INT_ROUTER_STATUS2_REG),
	REG(PCIE_SoC_INT_ROUTER_STATUS3_REG),
	REG(DMA_IP_CTRL_REG), REG(DISP_BRIDGE_PU_PD_CTRL_REG),
	REG(VIP_PU_PD_CTRL_REG), REG(USB_MLB_PU_PD_CTRL_REG),
	REG(SDIO_PU_PD_MISCFUNC_CTRL_REG1), REG(SDIO_PU_PD_MISCFUNC_CTRL_REG2),
	REG(UART_PU_PD_CTRL_REG), REG(ARM_Lock), REG(SYS_IO_CHAR_REG1),
	REG(SYS_IO_CHAR_REG2), REG(SATA_CORE_ID_REG), REG(SATA_CTRL_REG),
	REG(I2C_HSFIX_MISC_REG), REG(SPARE2_RESERVED), REG(SPARE3_RESERVED),
	REG(MASTER_LOCK_REG), REG(SYSTEM_CONFIG_STATUS_REG),
	REG(MSP_CLK_CTRL_REG), REG(COMPENSATION_REG1), REG(COMPENSATION_REG2),
	REG(COMPENSATION_REG3), REG(TEST_CTL_REG),
};
#undef REG

static struct debugfs_regset32 apb_soc_regs_regset = {
	.regs = sta2x11_apb_soc_regs_regs,
	.nregs = ARRAY_SIZE(sta2x11_apb_soc_regs_regs),
};


static struct dentry *sta2x11_mfd_debugfs[sta2x11_n_mfd_plat_devs];

static struct debugfs_regset32 *sta2x11_mfd_regset[sta2x11_n_mfd_plat_devs] = {
	[sta2x11_sctl] = &sctl_regset,
	[sta2x11_apbreg] = &apbreg_regset,
	[sta2x11_apb_soc_regs] = &apb_soc_regs_regset,
};

static const char *sta2x11_mfd_names[sta2x11_n_mfd_plat_devs] = {
	[sta2x11_sctl] = "sta2x11-sctl",
	[sta2x11_apbreg] = "sta2x11-apbreg",
	[sta2x11_apb_soc_regs] = "sta2x11-apb-soc-regs",
};

/* Probe for the three platform devices */

static int sta2x11_mfd_platform_probe(struct platform_device *dev,
				      enum sta2x11_mfd_plat_dev index)
{
	struct pci_dev **pdev;
	struct sta2x11_mfd *mfd;
	struct resource *res;
	const char *name = sta2x11_mfd_names[index];
	struct debugfs_regset32 *regset = sta2x11_mfd_regset[index];

	pdev = dev->dev.platform_data;
	mfd = sta2x11_mfd_find(*pdev);
	if (!mfd)
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
	regset->base = mfd->regs[index];
	sta2x11_mfd_debugfs[index] = debugfs_create_regset32(name,
							     S_IFREG | S_IRUGO,
							     NULL, regset);
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

/* The three platform drivers */
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

static struct platform_driver sta2x11_apb_soc_regs_platform_driver = {
	.driver = {
		.name	= "sta2x11-apb-soc-regs",
		.owner	= THIS_MODULE,
	},
	.probe		= sta2x11_apb_soc_regs_probe,
};

static int __init sta2x11_apb_soc_regs_init(void)
{
	pr_info("%s\n", __func__);
	return platform_driver_register(&sta2x11_apb_soc_regs_platform_driver);
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

static __devinitdata struct mfd_cell sta2x11_mfd0_bar0[] = {
	DEV("sta2x11-gpio", gpio_resources), /* offset 0: we add pdata later */
	DEV("sta2x11-sctl", sctl_resources),
	DEV("sta2x11-scr", scr_resources),
	DEV("sta2x11-time", time_resources),
};

static __devinitdata struct mfd_cell sta2x11_mfd0_bar1[] = {
	DEV("sta2x11-apbreg", apbreg_resources),
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

static const __devinitconst struct resource vic_resources[] = {
	CELL_4K("sta2x11-vic", STA2X11_VIC),
};

static const __devinitconst struct resource apb_soc_regs_resources[] = {
	CELL_4K("sta2x11-apb-soc-regs", STA2X11_APB_SOC_REGS),
};

static __devinitdata struct mfd_cell sta2x11_mfd1_bar0[] = {
	DEV("sta2x11-vic", vic_resources),
};

static __devinitdata struct mfd_cell sta2x11_mfd1_bar1[] = {
	DEV("sta2x11-apb-soc-regs", apb_soc_regs_resources),
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

static void __devinit sta2x11_mfd_setup(struct pci_dev *pdev,
					struct sta2x11_mfd_setup_data *sd)
{
	int i, j;
	for (i = 0; i < ARRAY_SIZE(sd->bars); i++)
		for (j = 0; j < sd->bars[i].ncells; j++) {
			sd->bars[i].cells[j].pdata_size = sizeof(pdev);
			sd->bars[i].cells[j].platform_data = &pdev;
		}
}

static int __devinit sta2x11_mfd_probe(struct pci_dev *pdev,
				       const struct pci_device_id *pci_id)
{
	int err, i;
	struct sta2x11_mfd_setup_data *setup_data;
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

	setup_data = pci_id->device == PCI_DEVICE_ID_STMICRO_GPIO ?
		&mfd_setup_data[STA2X11_MFD0] :
		&mfd_setup_data[STA2X11_MFD1];

	/* Read gpio config data as pci device's platform data */
	gpio_data = dev_get_platdata(&pdev->dev);
	if (!gpio_data)
		dev_warn(&pdev->dev, "no gpio configuration\n");

	dev_dbg(&pdev->dev, "%s, gpio_data = %p (%p)\n", __func__,
		gpio_data, &gpio_data);
	dev_dbg(&pdev->dev, "%s, pdev = %p (%p)\n", __func__,
		pdev, &pdev);

	/* platform data is the pci device for all of them */
	sta2x11_mfd_setup(pdev, setup_data);

	/* Record this pdev before mfd_add_devices: their probe looks for it */
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
rootfs_initcall(sta2x11_mfd_init);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Wind River");
MODULE_DESCRIPTION("STA2x11 mfd for GPIO, SCTL and APBREG");
MODULE_DEVICE_TABLE(pci, sta2x11_mfd_tbl);
