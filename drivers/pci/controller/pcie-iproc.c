// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2014 Hauke Mehrtens <hauke@hauke-m.de>
 * Copyright (C) 2015 Broadcom Corporation
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/msi.h>
#include <linux/clk.h>
#include <linux/module.h>
#include <linux/mbus.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irqchip/arm-gic-v3.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/of_irq.h>
#include <linux/of_platform.h>
#include <linux/phy/phy.h>

#include "pcie-iproc.h"

#define EP_PERST_SOURCE_SELECT_SHIFT	2
#define EP_PERST_SOURCE_SELECT		BIT(EP_PERST_SOURCE_SELECT_SHIFT)
#define EP_MODE_SURVIVE_PERST_SHIFT	1
#define EP_MODE_SURVIVE_PERST		BIT(EP_MODE_SURVIVE_PERST_SHIFT)
#define RC_PCIE_RST_OUTPUT_SHIFT	0
#define RC_PCIE_RST_OUTPUT		BIT(RC_PCIE_RST_OUTPUT_SHIFT)
#define PAXC_RESET_MASK			0x7f

#define GIC_V3_CFG_SHIFT		0
#define GIC_V3_CFG			BIT(GIC_V3_CFG_SHIFT)

#define MSI_ENABLE_CFG_SHIFT		0
#define MSI_ENABLE_CFG			BIT(MSI_ENABLE_CFG_SHIFT)

#define CFG_IND_ADDR_MASK		0x00001ffc

#define CFG_ADDR_BUS_NUM_SHIFT		20
#define CFG_ADDR_BUS_NUM_MASK		0x0ff00000
#define CFG_ADDR_DEV_NUM_SHIFT		15
#define CFG_ADDR_DEV_NUM_MASK		0x000f8000
#define CFG_ADDR_FUNC_NUM_SHIFT		12
#define CFG_ADDR_FUNC_NUM_MASK		0x00007000
#define CFG_ADDR_REG_NUM_SHIFT		2
#define CFG_ADDR_REG_NUM_MASK		0x00000ffc
#define CFG_ADDR_CFG_TYPE_SHIFT		0
#define CFG_ADDR_CFG_TYPE_MASK		0x00000003

#define SYS_RC_INTX_MASK		0xf

#define PCIE_PHYLINKUP_SHIFT		3
#define PCIE_PHYLINKUP			BIT(PCIE_PHYLINKUP_SHIFT)
#define PCIE_DL_ACTIVE_SHIFT		2
#define PCIE_DL_ACTIVE			BIT(PCIE_DL_ACTIVE_SHIFT)

#define APB_ERR_EN_SHIFT		0
#define APB_ERR_EN			BIT(APB_ERR_EN_SHIFT)

#define CFG_RD_SUCCESS			0
#define CFG_RD_UR			1
#define CFG_RD_CRS			2
#define CFG_RD_CA			3
#define CFG_RETRY_STATUS		0xffff0001
#define CFG_RETRY_STATUS_TIMEOUT_US	500000 /* 500 milliseconds */

/* derive the enum index of the outbound/inbound mapping registers */
#define MAP_REG(base_reg, index)	((base_reg) + (index) * 2)

/*
 * Maximum number of outbound mapping window sizes that can be supported by any
 * OARR/OMAP mapping pair
 */
#define MAX_NUM_OB_WINDOW_SIZES		4

#define OARR_VALID_SHIFT		0
#define OARR_VALID			BIT(OARR_VALID_SHIFT)
#define OARR_SIZE_CFG_SHIFT		1

/*
 * Maximum number of inbound mapping region sizes that can be supported by an
 * IARR
 */
#define MAX_NUM_IB_REGION_SIZES		9

#define IMAP_VALID_SHIFT		0
#define IMAP_VALID			BIT(IMAP_VALID_SHIFT)

#define IPROC_PCI_PM_CAP		0x48
#define IPROC_PCI_PM_CAP_MASK		0xffff
#define IPROC_PCI_EXP_CAP		0xac

#define IPROC_PCIE_REG_INVALID		0xffff

/**
 * iProc PCIe outbound mapping controller specific parameters
 *
 * @window_sizes: list of supported outbound mapping window sizes in MB
 * @nr_sizes: number of supported outbound mapping window sizes
 */
struct iproc_pcie_ob_map {
	resource_size_t window_sizes[MAX_NUM_OB_WINDOW_SIZES];
	unsigned int nr_sizes;
};

static const struct iproc_pcie_ob_map paxb_ob_map[] = {
	{
		/* OARR0/OMAP0 */
		.window_sizes = { 128, 256 },
		.nr_sizes = 2,
	},
	{
		/* OARR1/OMAP1 */
		.window_sizes = { 128, 256 },
		.nr_sizes = 2,
	},
};

static const struct iproc_pcie_ob_map paxb_v2_ob_map[] = {
	{
		/* OARR0/OMAP0 */
		.window_sizes = { 128, 256 },
		.nr_sizes = 2,
	},
	{
		/* OARR1/OMAP1 */
		.window_sizes = { 128, 256 },
		.nr_sizes = 2,
	},
	{
		/* OARR2/OMAP2 */
		.window_sizes = { 128, 256, 512, 1024 },
		.nr_sizes = 4,
	},
	{
		/* OARR3/OMAP3 */
		.window_sizes = { 128, 256, 512, 1024 },
		.nr_sizes = 4,
	},
};

/**
 * iProc PCIe inbound mapping type
 */
enum iproc_pcie_ib_map_type {
	/* for DDR memory */
	IPROC_PCIE_IB_MAP_MEM = 0,

	/* for device I/O memory */
	IPROC_PCIE_IB_MAP_IO,

	/* invalid or unused */
	IPROC_PCIE_IB_MAP_INVALID
};

/**
 * iProc PCIe inbound mapping controller specific parameters
 *
 * @type: inbound mapping region type
 * @size_unit: inbound mapping region size unit, could be SZ_1K, SZ_1M, or
 * SZ_1G
 * @region_sizes: list of supported inbound mapping region sizes in KB, MB, or
 * GB, depending on the size unit
 * @nr_sizes: number of supported inbound mapping region sizes
 * @nr_windows: number of supported inbound mapping windows for the region
 * @imap_addr_offset: register offset between the upper and lower 32-bit
 * IMAP address registers
 * @imap_window_offset: register offset between each IMAP window
 */
struct iproc_pcie_ib_map {
	enum iproc_pcie_ib_map_type type;
	unsigned int size_unit;
	resource_size_t region_sizes[MAX_NUM_IB_REGION_SIZES];
	unsigned int nr_sizes;
	unsigned int nr_windows;
	u16 imap_addr_offset;
	u16 imap_window_offset;
};

static const struct iproc_pcie_ib_map paxb_v2_ib_map[] = {
	{
		/* IARR0/IMAP0 */
		.type = IPROC_PCIE_IB_MAP_IO,
		.size_unit = SZ_1K,
		.region_sizes = { 32 },
		.nr_sizes = 1,
		.nr_windows = 8,
		.imap_addr_offset = 0x40,
		.imap_window_offset = 0x4,
	},
	{
		/* IARR1/IMAP1 (currently unused) */
		.type = IPROC_PCIE_IB_MAP_INVALID,
	},
	{
		/* IARR2/IMAP2 */
		.type = IPROC_PCIE_IB_MAP_MEM,
		.size_unit = SZ_1M,
		.region_sizes = { 64, 128, 256, 512, 1024, 2048, 4096, 8192,
				  16384 },
		.nr_sizes = 9,
		.nr_windows = 1,
		.imap_addr_offset = 0x4,
		.imap_window_offset = 0x8,
	},
	{
		/* IARR3/IMAP3 */
		.type = IPROC_PCIE_IB_MAP_MEM,
		.size_unit = SZ_1G,
		.region_sizes = { 1, 2, 4, 8, 16, 32 },
		.nr_sizes = 6,
		.nr_windows = 8,
		.imap_addr_offset = 0x4,
		.imap_window_offset = 0x8,
	},
	{
		/* IARR4/IMAP4 */
		.type = IPROC_PCIE_IB_MAP_MEM,
		.size_unit = SZ_1G,
		.region_sizes = { 32, 64, 128, 256, 512 },
		.nr_sizes = 5,
		.nr_windows = 8,
		.imap_addr_offset = 0x4,
		.imap_window_offset = 0x8,
	},
};

/*
 * iProc PCIe host registers
 */
enum iproc_pcie_reg {
	/* clock/reset signal control */
	IPROC_PCIE_CLK_CTRL = 0,

	/*
	 * To allow MSI to be steered to an external MSI controller (e.g., ARM
	 * GICv3 ITS)
	 */
	IPROC_PCIE_MSI_GIC_MODE,

	/*
	 * IPROC_PCIE_MSI_BASE_ADDR and IPROC_PCIE_MSI_WINDOW_SIZE define the
	 * window where the MSI posted writes are written, for the writes to be
	 * interpreted as MSI writes.
	 */
	IPROC_PCIE_MSI_BASE_ADDR,
	IPROC_PCIE_MSI_WINDOW_SIZE,

	/*
	 * To hold the address of the register where the MSI writes are
	 * programed.  When ARM GICv3 ITS is used, this should be programmed
	 * with the address of the GITS_TRANSLATER register.
	 */
	IPROC_PCIE_MSI_ADDR_LO,
	IPROC_PCIE_MSI_ADDR_HI,

	/* enable MSI */
	IPROC_PCIE_MSI_EN_CFG,

	/* allow access to root complex configuration space */
	IPROC_PCIE_CFG_IND_ADDR,
	IPROC_PCIE_CFG_IND_DATA,

	/* allow access to device configuration space */
	IPROC_PCIE_CFG_ADDR,
	IPROC_PCIE_CFG_DATA,

	/* enable INTx */
	IPROC_PCIE_INTX_EN,

	/* outbound address mapping */
	IPROC_PCIE_OARR0,
	IPROC_PCIE_OMAP0,
	IPROC_PCIE_OARR1,
	IPROC_PCIE_OMAP1,
	IPROC_PCIE_OARR2,
	IPROC_PCIE_OMAP2,
	IPROC_PCIE_OARR3,
	IPROC_PCIE_OMAP3,

	/* inbound address mapping */
	IPROC_PCIE_IARR0,
	IPROC_PCIE_IMAP0,
	IPROC_PCIE_IARR1,
	IPROC_PCIE_IMAP1,
	IPROC_PCIE_IARR2,
	IPROC_PCIE_IMAP2,
	IPROC_PCIE_IARR3,
	IPROC_PCIE_IMAP3,
	IPROC_PCIE_IARR4,
	IPROC_PCIE_IMAP4,

	/* config read status */
	IPROC_PCIE_CFG_RD_STATUS,

	/* link status */
	IPROC_PCIE_LINK_STATUS,

	/* enable APB error for unsupported requests */
	IPROC_PCIE_APB_ERR_EN,

	/* total number of core registers */
	IPROC_PCIE_MAX_NUM_REG,
};

/* iProc PCIe PAXB BCMA registers */
static const u16 iproc_pcie_reg_paxb_bcma[] = {
	[IPROC_PCIE_CLK_CTRL]		= 0x000,
	[IPROC_PCIE_CFG_IND_ADDR]	= 0x120,
	[IPROC_PCIE_CFG_IND_DATA]	= 0x124,
	[IPROC_PCIE_CFG_ADDR]		= 0x1f8,
	[IPROC_PCIE_CFG_DATA]		= 0x1fc,
	[IPROC_PCIE_INTX_EN]		= 0x330,
	[IPROC_PCIE_LINK_STATUS]	= 0xf0c,
};

/* iProc PCIe PAXB registers */
static const u16 iproc_pcie_reg_paxb[] = {
	[IPROC_PCIE_CLK_CTRL]		= 0x000,
	[IPROC_PCIE_CFG_IND_ADDR]	= 0x120,
	[IPROC_PCIE_CFG_IND_DATA]	= 0x124,
	[IPROC_PCIE_CFG_ADDR]		= 0x1f8,
	[IPROC_PCIE_CFG_DATA]		= 0x1fc,
	[IPROC_PCIE_INTX_EN]		= 0x330,
	[IPROC_PCIE_OARR0]		= 0xd20,
	[IPROC_PCIE_OMAP0]		= 0xd40,
	[IPROC_PCIE_OARR1]		= 0xd28,
	[IPROC_PCIE_OMAP1]		= 0xd48,
	[IPROC_PCIE_LINK_STATUS]	= 0xf0c,
	[IPROC_PCIE_APB_ERR_EN]		= 0xf40,
};

/* iProc PCIe PAXB v2 registers */
static const u16 iproc_pcie_reg_paxb_v2[] = {
	[IPROC_PCIE_CLK_CTRL]		= 0x000,
	[IPROC_PCIE_CFG_IND_ADDR]	= 0x120,
	[IPROC_PCIE_CFG_IND_DATA]	= 0x124,
	[IPROC_PCIE_CFG_ADDR]		= 0x1f8,
	[IPROC_PCIE_CFG_DATA]		= 0x1fc,
	[IPROC_PCIE_INTX_EN]		= 0x330,
	[IPROC_PCIE_OARR0]		= 0xd20,
	[IPROC_PCIE_OMAP0]		= 0xd40,
	[IPROC_PCIE_OARR1]		= 0xd28,
	[IPROC_PCIE_OMAP1]		= 0xd48,
	[IPROC_PCIE_OARR2]		= 0xd60,
	[IPROC_PCIE_OMAP2]		= 0xd68,
	[IPROC_PCIE_OARR3]		= 0xdf0,
	[IPROC_PCIE_OMAP3]		= 0xdf8,
	[IPROC_PCIE_IARR0]		= 0xd00,
	[IPROC_PCIE_IMAP0]		= 0xc00,
	[IPROC_PCIE_IARR2]		= 0xd10,
	[IPROC_PCIE_IMAP2]		= 0xcc0,
	[IPROC_PCIE_IARR3]		= 0xe00,
	[IPROC_PCIE_IMAP3]		= 0xe08,
	[IPROC_PCIE_IARR4]		= 0xe68,
	[IPROC_PCIE_IMAP4]		= 0xe70,
	[IPROC_PCIE_CFG_RD_STATUS]	= 0xee0,
	[IPROC_PCIE_LINK_STATUS]	= 0xf0c,
	[IPROC_PCIE_APB_ERR_EN]		= 0xf40,
};

/* iProc PCIe PAXC v1 registers */
static const u16 iproc_pcie_reg_paxc[] = {
	[IPROC_PCIE_CLK_CTRL]		= 0x000,
	[IPROC_PCIE_CFG_IND_ADDR]	= 0x1f0,
	[IPROC_PCIE_CFG_IND_DATA]	= 0x1f4,
	[IPROC_PCIE_CFG_ADDR]		= 0x1f8,
	[IPROC_PCIE_CFG_DATA]		= 0x1fc,
};

/* iProc PCIe PAXC v2 registers */
static const u16 iproc_pcie_reg_paxc_v2[] = {
	[IPROC_PCIE_MSI_GIC_MODE]	= 0x050,
	[IPROC_PCIE_MSI_BASE_ADDR]	= 0x074,
	[IPROC_PCIE_MSI_WINDOW_SIZE]	= 0x078,
	[IPROC_PCIE_MSI_ADDR_LO]	= 0x07c,
	[IPROC_PCIE_MSI_ADDR_HI]	= 0x080,
	[IPROC_PCIE_MSI_EN_CFG]		= 0x09c,
	[IPROC_PCIE_CFG_IND_ADDR]	= 0x1f0,
	[IPROC_PCIE_CFG_IND_DATA]	= 0x1f4,
	[IPROC_PCIE_CFG_ADDR]		= 0x1f8,
	[IPROC_PCIE_CFG_DATA]		= 0x1fc,
};

/*
 * List of device IDs of controllers that have corrupted capability list that
 * require SW fixup
 */
static const u16 iproc_pcie_corrupt_cap_did[] = {
	0x16cd,
	0x16f0,
	0xd802,
	0xd804
};

static inline struct iproc_pcie *iproc_data(struct pci_bus *bus)
{
	struct iproc_pcie *pcie = bus->sysdata;
	return pcie;
}

static inline bool iproc_pcie_reg_is_invalid(u16 reg_offset)
{
	return !!(reg_offset == IPROC_PCIE_REG_INVALID);
}

static inline u16 iproc_pcie_reg_offset(struct iproc_pcie *pcie,
					enum iproc_pcie_reg reg)
{
	return pcie->reg_offsets[reg];
}

static inline u32 iproc_pcie_read_reg(struct iproc_pcie *pcie,
				      enum iproc_pcie_reg reg)
{
	u16 offset = iproc_pcie_reg_offset(pcie, reg);

	if (iproc_pcie_reg_is_invalid(offset))
		return 0;

	return readl(pcie->base + offset);
}

static inline void iproc_pcie_write_reg(struct iproc_pcie *pcie,
					enum iproc_pcie_reg reg, u32 val)
{
	u16 offset = iproc_pcie_reg_offset(pcie, reg);

	if (iproc_pcie_reg_is_invalid(offset))
		return;

	writel(val, pcie->base + offset);
}

/**
 * APB error forwarding can be disabled during access of configuration
 * registers of the endpoint device, to prevent unsupported requests
 * (typically seen during enumeration with multi-function devices) from
 * triggering a system exception.
 */
static inline void iproc_pcie_apb_err_disable(struct pci_bus *bus,
					      bool disable)
{
	struct iproc_pcie *pcie = iproc_data(bus);
	u32 val;

	if (bus->number && pcie->has_apb_err_disable) {
		val = iproc_pcie_read_reg(pcie, IPROC_PCIE_APB_ERR_EN);
		if (disable)
			val &= ~APB_ERR_EN;
		else
			val |= APB_ERR_EN;
		iproc_pcie_write_reg(pcie, IPROC_PCIE_APB_ERR_EN, val);
	}
}

static void __iomem *iproc_pcie_map_ep_cfg_reg(struct iproc_pcie *pcie,
					       unsigned int busno,
					       unsigned int slot,
					       unsigned int fn,
					       int where)
{
	u16 offset;
	u32 val;

	/* EP device access */
	val = (busno << CFG_ADDR_BUS_NUM_SHIFT) |
		(slot << CFG_ADDR_DEV_NUM_SHIFT) |
		(fn << CFG_ADDR_FUNC_NUM_SHIFT) |
		(where & CFG_ADDR_REG_NUM_MASK) |
		(1 & CFG_ADDR_CFG_TYPE_MASK);

	iproc_pcie_write_reg(pcie, IPROC_PCIE_CFG_ADDR, val);
	offset = iproc_pcie_reg_offset(pcie, IPROC_PCIE_CFG_DATA);

	if (iproc_pcie_reg_is_invalid(offset))
		return NULL;

	return (pcie->base + offset);
}

static unsigned int iproc_pcie_cfg_retry(struct iproc_pcie *pcie,
					 void __iomem *cfg_data_p)
{
	int timeout = CFG_RETRY_STATUS_TIMEOUT_US;
	unsigned int data;
	u32 status;

	/*
	 * As per PCIe spec r3.1, sec 2.3.2, CRS Software Visibility only
	 * affects config reads of the Vendor ID.  For config writes or any
	 * other config reads, the Root may automatically reissue the
	 * configuration request again as a new request.
	 *
	 * For config reads, this hardware returns CFG_RETRY_STATUS data
	 * when it receives a CRS completion, regardless of the address of
	 * the read or the CRS Software Visibility Enable bit.  As a
	 * partial workaround for this, we retry in software any read that
	 * returns CFG_RETRY_STATUS.
	 *
	 * Note that a non-Vendor ID config register may have a value of
	 * CFG_RETRY_STATUS.  If we read that, we can't distinguish it from
	 * a CRS completion, so we will incorrectly retry the read and
	 * eventually return the wrong data (0xffffffff).
	 */
	data = readl(cfg_data_p);
	while (data == CFG_RETRY_STATUS && timeout--) {
		/*
		 * CRS state is set in CFG_RD status register
		 * This will handle the case where CFG_RETRY_STATUS is
		 * valid config data.
		 */
		status = iproc_pcie_read_reg(pcie, IPROC_PCIE_CFG_RD_STATUS);
		if (status != CFG_RD_CRS)
			return data;

		udelay(1);
		data = readl(cfg_data_p);
	}

	if (data == CFG_RETRY_STATUS)
		data = 0xffffffff;

	return data;
}

static void iproc_pcie_fix_cap(struct iproc_pcie *pcie, int where, u32 *val)
{
	u32 i, dev_id;

	switch (where & ~0x3) {
	case PCI_VENDOR_ID:
		dev_id = *val >> 16;

		/*
		 * Activate fixup for those controllers that have corrupted
		 * capability list registers
		 */
		for (i = 0; i < ARRAY_SIZE(iproc_pcie_corrupt_cap_did); i++)
			if (dev_id == iproc_pcie_corrupt_cap_did[i])
				pcie->fix_paxc_cap = true;
		break;

	case IPROC_PCI_PM_CAP:
		if (pcie->fix_paxc_cap) {
			/* advertise PM, force next capability to PCIe */
			*val &= ~IPROC_PCI_PM_CAP_MASK;
			*val |= IPROC_PCI_EXP_CAP << 8 | PCI_CAP_ID_PM;
		}
		break;

	case IPROC_PCI_EXP_CAP:
		if (pcie->fix_paxc_cap) {
			/* advertise root port, version 2, terminate here */
			*val = (PCI_EXP_TYPE_ROOT_PORT << 4 | 2) << 16 |
				PCI_CAP_ID_EXP;
		}
		break;

	case IPROC_PCI_EXP_CAP + PCI_EXP_RTCTL:
		/* Don't advertise CRS SV support */
		*val &= ~(PCI_EXP_RTCAP_CRSVIS << 16);
		break;

	default:
		break;
	}
}

static int iproc_pcie_config_read(struct pci_bus *bus, unsigned int devfn,
				  int where, int size, u32 *val)
{
	struct iproc_pcie *pcie = iproc_data(bus);
	unsigned int slot = PCI_SLOT(devfn);
	unsigned int fn = PCI_FUNC(devfn);
	unsigned int busno = bus->number;
	void __iomem *cfg_data_p;
	unsigned int data;
	int ret;

	/* root complex access */
	if (busno == 0) {
		ret = pci_generic_config_read32(bus, devfn, where, size, val);
		if (ret == PCIBIOS_SUCCESSFUL)
			iproc_pcie_fix_cap(pcie, where, val);

		return ret;
	}

	cfg_data_p = iproc_pcie_map_ep_cfg_reg(pcie, busno, slot, fn, where);

	if (!cfg_data_p)
		return PCIBIOS_DEVICE_NOT_FOUND;

	data = iproc_pcie_cfg_retry(pcie, cfg_data_p);

	*val = data;
	if (size <= 2)
		*val = (data >> (8 * (where & 3))) & ((1 << (size * 8)) - 1);

	/*
	 * For PAXC and PAXCv2, the total number of PFs that one can enumerate
	 * depends on the firmware configuration. Unfortunately, due to an ASIC
	 * bug, unconfigured PFs cannot be properly hidden from the root
	 * complex. As a result, write access to these PFs will cause bus lock
	 * up on the embedded processor
	 *
	 * Since all unconfigured PFs are left with an incorrect, staled device
	 * ID of 0x168e (PCI_DEVICE_ID_NX2_57810), we try to catch those access
	 * early here and reject them all
	 */
#define DEVICE_ID_MASK     0xffff0000
#define DEVICE_ID_SHIFT    16
	if (pcie->rej_unconfig_pf &&
	    (where & CFG_ADDR_REG_NUM_MASK) == PCI_VENDOR_ID)
		if ((*val & DEVICE_ID_MASK) ==
		    (PCI_DEVICE_ID_NX2_57810 << DEVICE_ID_SHIFT))
			return PCIBIOS_FUNC_NOT_SUPPORTED;

	return PCIBIOS_SUCCESSFUL;
}

/**
 * Note access to the configuration registers are protected at the higher layer
 * by 'pci_lock' in drivers/pci/access.c
 */
static void __iomem *iproc_pcie_map_cfg_bus(struct iproc_pcie *pcie,
					    int busno, unsigned int devfn,
					    int where)
{
	unsigned slot = PCI_SLOT(devfn);
	unsigned fn = PCI_FUNC(devfn);
	u16 offset;

	/* root complex access */
	if (busno == 0) {
		if (slot > 0 || fn > 0)
			return NULL;

		iproc_pcie_write_reg(pcie, IPROC_PCIE_CFG_IND_ADDR,
				     where & CFG_IND_ADDR_MASK);
		offset = iproc_pcie_reg_offset(pcie, IPROC_PCIE_CFG_IND_DATA);
		if (iproc_pcie_reg_is_invalid(offset))
			return NULL;
		else
			return (pcie->base + offset);
	}

	return iproc_pcie_map_ep_cfg_reg(pcie, busno, slot, fn, where);
}

static void __iomem *iproc_pcie_bus_map_cfg_bus(struct pci_bus *bus,
						unsigned int devfn,
						int where)
{
	return iproc_pcie_map_cfg_bus(iproc_data(bus), bus->number, devfn,
				      where);
}

static int iproc_pci_raw_config_read32(struct iproc_pcie *pcie,
				       unsigned int devfn, int where,
				       int size, u32 *val)
{
	void __iomem *addr;

	addr = iproc_pcie_map_cfg_bus(pcie, 0, devfn, where & ~0x3);
	if (!addr) {
		*val = ~0;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	*val = readl(addr);

	if (size <= 2)
		*val = (*val >> (8 * (where & 3))) & ((1 << (size * 8)) - 1);

	return PCIBIOS_SUCCESSFUL;
}

static int iproc_pci_raw_config_write32(struct iproc_pcie *pcie,
					unsigned int devfn, int where,
					int size, u32 val)
{
	void __iomem *addr;
	u32 mask, tmp;

	addr = iproc_pcie_map_cfg_bus(pcie, 0, devfn, where & ~0x3);
	if (!addr)
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (size == 4) {
		writel(val, addr);
		return PCIBIOS_SUCCESSFUL;
	}

	mask = ~(((1 << (size * 8)) - 1) << ((where & 0x3) * 8));
	tmp = readl(addr) & mask;
	tmp |= val << ((where & 0x3) * 8);
	writel(tmp, addr);

	return PCIBIOS_SUCCESSFUL;
}

static int iproc_pcie_config_read32(struct pci_bus *bus, unsigned int devfn,
				    int where, int size, u32 *val)
{
	int ret;
	struct iproc_pcie *pcie = iproc_data(bus);

	iproc_pcie_apb_err_disable(bus, true);
	if (pcie->iproc_cfg_read)
		ret = iproc_pcie_config_read(bus, devfn, where, size, val);
	else
		ret = pci_generic_config_read32(bus, devfn, where, size, val);
	iproc_pcie_apb_err_disable(bus, false);

	return ret;
}

static int iproc_pcie_config_write32(struct pci_bus *bus, unsigned int devfn,
				     int where, int size, u32 val)
{
	int ret;

	iproc_pcie_apb_err_disable(bus, true);
	ret = pci_generic_config_write32(bus, devfn, where, size, val);
	iproc_pcie_apb_err_disable(bus, false);

	return ret;
}

static struct pci_ops iproc_pcie_ops = {
	.map_bus = iproc_pcie_bus_map_cfg_bus,
	.read = iproc_pcie_config_read32,
	.write = iproc_pcie_config_write32,
};

static void iproc_pcie_perst_ctrl(struct iproc_pcie *pcie, bool assert)
{
	u32 val;

	/*
	 * PAXC and the internal emulated endpoint device downstream should not
	 * be reset.  If firmware has been loaded on the endpoint device at an
	 * earlier boot stage, reset here causes issues.
	 */
	if (pcie->ep_is_internal)
		return;

	if (assert) {
		val = iproc_pcie_read_reg(pcie, IPROC_PCIE_CLK_CTRL);
		val &= ~EP_PERST_SOURCE_SELECT & ~EP_MODE_SURVIVE_PERST &
			~RC_PCIE_RST_OUTPUT;
		iproc_pcie_write_reg(pcie, IPROC_PCIE_CLK_CTRL, val);
		udelay(250);
	} else {
		val = iproc_pcie_read_reg(pcie, IPROC_PCIE_CLK_CTRL);
		val |= RC_PCIE_RST_OUTPUT;
		iproc_pcie_write_reg(pcie, IPROC_PCIE_CLK_CTRL, val);
		msleep(100);
	}
}

int iproc_pcie_shutdown(struct iproc_pcie *pcie)
{
	iproc_pcie_perst_ctrl(pcie, true);
	msleep(500);

	return 0;
}
EXPORT_SYMBOL_GPL(iproc_pcie_shutdown);

static int iproc_pcie_check_link(struct iproc_pcie *pcie)
{
	struct device *dev = pcie->dev;
	u32 hdr_type, link_ctrl, link_status, class, val;
	bool link_is_active = false;

	/*
	 * PAXC connects to emulated endpoint devices directly and does not
	 * have a Serdes.  Therefore skip the link detection logic here.
	 */
	if (pcie->ep_is_internal)
		return 0;

	val = iproc_pcie_read_reg(pcie, IPROC_PCIE_LINK_STATUS);
	if (!(val & PCIE_PHYLINKUP) || !(val & PCIE_DL_ACTIVE)) {
		dev_err(dev, "PHY or data link is INACTIVE!\n");
		return -ENODEV;
	}

	/* make sure we are not in EP mode */
	iproc_pci_raw_config_read32(pcie, 0, PCI_HEADER_TYPE, 1, &hdr_type);
	if ((hdr_type & 0x7f) != PCI_HEADER_TYPE_BRIDGE) {
		dev_err(dev, "in EP mode, hdr=%#02x\n", hdr_type);
		return -EFAULT;
	}

	/* force class to PCI_CLASS_BRIDGE_PCI (0x0604) */
#define PCI_BRIDGE_CTRL_REG_OFFSET	0x43c
#define PCI_CLASS_BRIDGE_MASK		0xffff00
#define PCI_CLASS_BRIDGE_SHIFT		8
	iproc_pci_raw_config_read32(pcie, 0, PCI_BRIDGE_CTRL_REG_OFFSET,
				    4, &class);
	class &= ~PCI_CLASS_BRIDGE_MASK;
	class |= (PCI_CLASS_BRIDGE_PCI << PCI_CLASS_BRIDGE_SHIFT);
	iproc_pci_raw_config_write32(pcie, 0, PCI_BRIDGE_CTRL_REG_OFFSET,
				     4, class);

	/* check link status to see if link is active */
	iproc_pci_raw_config_read32(pcie, 0, IPROC_PCI_EXP_CAP + PCI_EXP_LNKSTA,
				    2, &link_status);
	if (link_status & PCI_EXP_LNKSTA_NLW)
		link_is_active = true;

	if (!link_is_active) {
		/* try GEN 1 link speed */
#define PCI_TARGET_LINK_SPEED_MASK	0xf
#define PCI_TARGET_LINK_SPEED_GEN2	0x2
#define PCI_TARGET_LINK_SPEED_GEN1	0x1
		iproc_pci_raw_config_read32(pcie, 0,
					    IPROC_PCI_EXP_CAP + PCI_EXP_LNKCTL2,
					    4, &link_ctrl);
		if ((link_ctrl & PCI_TARGET_LINK_SPEED_MASK) ==
		    PCI_TARGET_LINK_SPEED_GEN2) {
			link_ctrl &= ~PCI_TARGET_LINK_SPEED_MASK;
			link_ctrl |= PCI_TARGET_LINK_SPEED_GEN1;
			iproc_pci_raw_config_write32(pcie, 0,
					IPROC_PCI_EXP_CAP + PCI_EXP_LNKCTL2,
					4, link_ctrl);
			msleep(100);

			iproc_pci_raw_config_read32(pcie, 0,
					IPROC_PCI_EXP_CAP + PCI_EXP_LNKSTA,
					2, &link_status);
			if (link_status & PCI_EXP_LNKSTA_NLW)
				link_is_active = true;
		}
	}

	dev_info(dev, "link: %s\n", link_is_active ? "UP" : "DOWN");

	return link_is_active ? 0 : -ENODEV;
}

static void iproc_pcie_enable(struct iproc_pcie *pcie)
{
	iproc_pcie_write_reg(pcie, IPROC_PCIE_INTX_EN, SYS_RC_INTX_MASK);
}

static inline bool iproc_pcie_ob_is_valid(struct iproc_pcie *pcie,
					  int window_idx)
{
	u32 val;

	val = iproc_pcie_read_reg(pcie, MAP_REG(IPROC_PCIE_OARR0, window_idx));

	return !!(val & OARR_VALID);
}

static inline int iproc_pcie_ob_write(struct iproc_pcie *pcie, int window_idx,
				      int size_idx, u64 axi_addr, u64 pci_addr)
{
	struct device *dev = pcie->dev;
	u16 oarr_offset, omap_offset;

	/*
	 * Derive the OARR/OMAP offset from the first pair (OARR0/OMAP0) based
	 * on window index.
	 */
	oarr_offset = iproc_pcie_reg_offset(pcie, MAP_REG(IPROC_PCIE_OARR0,
							  window_idx));
	omap_offset = iproc_pcie_reg_offset(pcie, MAP_REG(IPROC_PCIE_OMAP0,
							  window_idx));
	if (iproc_pcie_reg_is_invalid(oarr_offset) ||
	    iproc_pcie_reg_is_invalid(omap_offset))
		return -EINVAL;

	/*
	 * Program the OARR registers.  The upper 32-bit OARR register is
	 * always right after the lower 32-bit OARR register.
	 */
	writel(lower_32_bits(axi_addr) | (size_idx << OARR_SIZE_CFG_SHIFT) |
	       OARR_VALID, pcie->base + oarr_offset);
	writel(upper_32_bits(axi_addr), pcie->base + oarr_offset + 4);

	/* now program the OMAP registers */
	writel(lower_32_bits(pci_addr), pcie->base + omap_offset);
	writel(upper_32_bits(pci_addr), pcie->base + omap_offset + 4);

	dev_dbg(dev, "ob window [%d]: offset 0x%x axi %pap pci %pap\n",
		window_idx, oarr_offset, &axi_addr, &pci_addr);
	dev_dbg(dev, "oarr lo 0x%x oarr hi 0x%x\n",
		readl(pcie->base + oarr_offset),
		readl(pcie->base + oarr_offset + 4));
	dev_dbg(dev, "omap lo 0x%x omap hi 0x%x\n",
		readl(pcie->base + omap_offset),
		readl(pcie->base + omap_offset + 4));

	return 0;
}

/**
 * Some iProc SoCs require the SW to configure the outbound address mapping
 *
 * Outbound address translation:
 *
 * iproc_pcie_address = axi_address - axi_offset
 * OARR = iproc_pcie_address
 * OMAP = pci_addr
 *
 * axi_addr -> iproc_pcie_address -> OARR -> OMAP -> pci_address
 */
static int iproc_pcie_setup_ob(struct iproc_pcie *pcie, u64 axi_addr,
			       u64 pci_addr, resource_size_t size)
{
	struct iproc_pcie_ob *ob = &pcie->ob;
	struct device *dev = pcie->dev;
	int ret = -EINVAL, window_idx, size_idx;

	if (axi_addr < ob->axi_offset) {
		dev_err(dev, "axi address %pap less than offset %pap\n",
			&axi_addr, &ob->axi_offset);
		return -EINVAL;
	}

	/*
	 * Translate the AXI address to the internal address used by the iProc
	 * PCIe core before programming the OARR
	 */
	axi_addr -= ob->axi_offset;

	/* iterate through all OARR/OMAP mapping windows */
	for (window_idx = ob->nr_windows - 1; window_idx >= 0; window_idx--) {
		const struct iproc_pcie_ob_map *ob_map =
			&pcie->ob_map[window_idx];

		/*
		 * If current outbound window is already in use, move on to the
		 * next one.
		 */
		if (iproc_pcie_ob_is_valid(pcie, window_idx))
			continue;

		/*
		 * Iterate through all supported window sizes within the
		 * OARR/OMAP pair to find a match.  Go through the window sizes
		 * in a descending order.
		 */
		for (size_idx = ob_map->nr_sizes - 1; size_idx >= 0;
		     size_idx--) {
			resource_size_t window_size =
				ob_map->window_sizes[size_idx] * SZ_1M;

			/*
			 * Keep iterating until we reach the last window and
			 * with the minimal window size at index zero. In this
			 * case, we take a compromise by mapping it using the
			 * minimum window size that can be supported
			 */
			if (size < window_size) {
				if (size_idx > 0 || window_idx > 0)
					continue;

				/*
				 * For the corner case of reaching the minimal
				 * window size that can be supported on the
				 * last window
				 */
				axi_addr = ALIGN_DOWN(axi_addr, window_size);
				pci_addr = ALIGN_DOWN(pci_addr, window_size);
				size = window_size;
			}

			if (!IS_ALIGNED(axi_addr, window_size) ||
			    !IS_ALIGNED(pci_addr, window_size)) {
				dev_err(dev,
					"axi %pap or pci %pap not aligned\n",
					&axi_addr, &pci_addr);
				return -EINVAL;
			}

			/*
			 * Match found!  Program both OARR and OMAP and mark
			 * them as a valid entry.
			 */
			ret = iproc_pcie_ob_write(pcie, window_idx, size_idx,
						  axi_addr, pci_addr);
			if (ret)
				goto err_ob;

			size -= window_size;
			if (size == 0)
				return 0;

			/*
			 * If we are here, we are done with the current window,
			 * but not yet finished all mappings.  Need to move on
			 * to the next window.
			 */
			axi_addr += window_size;
			pci_addr += window_size;
			break;
		}
	}

err_ob:
	dev_err(dev, "unable to configure outbound mapping\n");
	dev_err(dev,
		"axi %pap, axi offset %pap, pci %pap, res size %pap\n",
		&axi_addr, &ob->axi_offset, &pci_addr, &size);

	return ret;
}

static int iproc_pcie_map_ranges(struct iproc_pcie *pcie,
				 struct list_head *resources)
{
	struct device *dev = pcie->dev;
	struct resource_entry *window;
	int ret;

	resource_list_for_each_entry(window, resources) {
		struct resource *res = window->res;
		u64 res_type = resource_type(res);

		switch (res_type) {
		case IORESOURCE_IO:
		case IORESOURCE_BUS:
			break;
		case IORESOURCE_MEM:
			ret = iproc_pcie_setup_ob(pcie, res->start,
						  res->start - window->offset,
						  resource_size(res));
			if (ret)
				return ret;
			break;
		default:
			dev_err(dev, "invalid resource %pR\n", res);
			return -EINVAL;
		}
	}

	return 0;
}

static inline bool iproc_pcie_ib_is_in_use(struct iproc_pcie *pcie,
					   int region_idx)
{
	const struct iproc_pcie_ib_map *ib_map = &pcie->ib_map[region_idx];
	u32 val;

	val = iproc_pcie_read_reg(pcie, MAP_REG(IPROC_PCIE_IARR0, region_idx));

	return !!(val & (BIT(ib_map->nr_sizes) - 1));
}

static inline bool iproc_pcie_ib_check_type(const struct iproc_pcie_ib_map *ib_map,
					    enum iproc_pcie_ib_map_type type)
{
	return !!(ib_map->type == type);
}

static int iproc_pcie_ib_write(struct iproc_pcie *pcie, int region_idx,
			       int size_idx, int nr_windows, u64 axi_addr,
			       u64 pci_addr, resource_size_t size)
{
	struct device *dev = pcie->dev;
	const struct iproc_pcie_ib_map *ib_map = &pcie->ib_map[region_idx];
	u16 iarr_offset, imap_offset;
	u32 val;
	int window_idx;

	iarr_offset = iproc_pcie_reg_offset(pcie,
				MAP_REG(IPROC_PCIE_IARR0, region_idx));
	imap_offset = iproc_pcie_reg_offset(pcie,
				MAP_REG(IPROC_PCIE_IMAP0, region_idx));
	if (iproc_pcie_reg_is_invalid(iarr_offset) ||
	    iproc_pcie_reg_is_invalid(imap_offset))
		return -EINVAL;

	dev_dbg(dev, "ib region [%d]: offset 0x%x axi %pap pci %pap\n",
		region_idx, iarr_offset, &axi_addr, &pci_addr);

	/*
	 * Program the IARR registers.  The upper 32-bit IARR register is
	 * always right after the lower 32-bit IARR register.
	 */
	writel(lower_32_bits(pci_addr) | BIT(size_idx),
	       pcie->base + iarr_offset);
	writel(upper_32_bits(pci_addr), pcie->base + iarr_offset + 4);

	dev_dbg(dev, "iarr lo 0x%x iarr hi 0x%x\n",
		readl(pcie->base + iarr_offset),
		readl(pcie->base + iarr_offset + 4));

	/*
	 * Now program the IMAP registers.  Each IARR region may have one or
	 * more IMAP windows.
	 */
	size >>= ilog2(nr_windows);
	for (window_idx = 0; window_idx < nr_windows; window_idx++) {
		val = readl(pcie->base + imap_offset);
		val |= lower_32_bits(axi_addr) | IMAP_VALID;
		writel(val, pcie->base + imap_offset);
		writel(upper_32_bits(axi_addr),
		       pcie->base + imap_offset + ib_map->imap_addr_offset);

		dev_dbg(dev, "imap window [%d] lo 0x%x hi 0x%x\n",
			window_idx, readl(pcie->base + imap_offset),
			readl(pcie->base + imap_offset +
			      ib_map->imap_addr_offset));

		imap_offset += ib_map->imap_window_offset;
		axi_addr += size;
	}

	return 0;
}

static int iproc_pcie_setup_ib(struct iproc_pcie *pcie,
			       struct resource_entry *entry,
			       enum iproc_pcie_ib_map_type type)
{
	struct device *dev = pcie->dev;
	struct iproc_pcie_ib *ib = &pcie->ib;
	int ret;
	unsigned int region_idx, size_idx;
	u64 axi_addr = entry->res->start;
	u64 pci_addr = entry->res->start - entry->offset;
	resource_size_t size = resource_size(entry->res);

	/* iterate through all IARR mapping regions */
	for (region_idx = 0; region_idx < ib->nr_regions; region_idx++) {
		const struct iproc_pcie_ib_map *ib_map =
			&pcie->ib_map[region_idx];

		/*
		 * If current inbound region is already in use or not a
		 * compatible type, move on to the next.
		 */
		if (iproc_pcie_ib_is_in_use(pcie, region_idx) ||
		    !iproc_pcie_ib_check_type(ib_map, type))
			continue;

		/* iterate through all supported region sizes to find a match */
		for (size_idx = 0; size_idx < ib_map->nr_sizes; size_idx++) {
			resource_size_t region_size =
			ib_map->region_sizes[size_idx] * ib_map->size_unit;

			if (size != region_size)
				continue;

			if (!IS_ALIGNED(axi_addr, region_size) ||
			    !IS_ALIGNED(pci_addr, region_size)) {
				dev_err(dev,
					"axi %pap or pci %pap not aligned\n",
					&axi_addr, &pci_addr);
				return -EINVAL;
			}

			/* Match found!  Program IARR and all IMAP windows. */
			ret = iproc_pcie_ib_write(pcie, region_idx, size_idx,
						  ib_map->nr_windows, axi_addr,
						  pci_addr, size);
			if (ret)
				goto err_ib;
			else
				return 0;

		}
	}
	ret = -EINVAL;

err_ib:
	dev_err(dev, "unable to configure inbound mapping\n");
	dev_err(dev, "axi %pap, pci %pap, res size %pap\n",
		&axi_addr, &pci_addr, &size);

	return ret;
}

static int iproc_pcie_map_dma_ranges(struct iproc_pcie *pcie)
{
	struct pci_host_bridge *host = pci_host_bridge_from_priv(pcie);
	struct resource_entry *entry;
	int ret = 0;

	resource_list_for_each_entry(entry, &host->dma_ranges) {
		/* Each range entry corresponds to an inbound mapping region */
		ret = iproc_pcie_setup_ib(pcie, entry, IPROC_PCIE_IB_MAP_MEM);
		if (ret)
			break;
	}

	return ret;
}

static void iproc_pcie_invalidate_mapping(struct iproc_pcie *pcie)
{
	struct iproc_pcie_ib *ib = &pcie->ib;
	struct iproc_pcie_ob *ob = &pcie->ob;
	int idx;

	if (pcie->ep_is_internal)
		return;

	if (pcie->need_ob_cfg) {
		/* iterate through all OARR mapping regions */
		for (idx = ob->nr_windows - 1; idx >= 0; idx--) {
			iproc_pcie_write_reg(pcie,
					     MAP_REG(IPROC_PCIE_OARR0, idx), 0);
		}
	}

	if (pcie->need_ib_cfg) {
		/* iterate through all IARR mapping regions */
		for (idx = 0; idx < ib->nr_regions; idx++) {
			iproc_pcie_write_reg(pcie,
					     MAP_REG(IPROC_PCIE_IARR0, idx), 0);
		}
	}
}

static int iproce_pcie_get_msi(struct iproc_pcie *pcie,
			       struct device_node *msi_node,
			       u64 *msi_addr)
{
	struct device *dev = pcie->dev;
	int ret;
	struct resource res;

	/*
	 * Check if 'msi-map' points to ARM GICv3 ITS, which is the only
	 * supported external MSI controller that requires steering.
	 */
	if (!of_device_is_compatible(msi_node, "arm,gic-v3-its")) {
		dev_err(dev, "unable to find compatible MSI controller\n");
		return -ENODEV;
	}

	/* derive GITS_TRANSLATER address from GICv3 */
	ret = of_address_to_resource(msi_node, 0, &res);
	if (ret < 0) {
		dev_err(dev, "unable to obtain MSI controller resources\n");
		return ret;
	}

	*msi_addr = res.start + GITS_TRANSLATER;
	return 0;
}

static int iproc_pcie_paxb_v2_msi_steer(struct iproc_pcie *pcie, u64 msi_addr)
{
	int ret;
	struct resource_entry entry;

	memset(&entry, 0, sizeof(entry));
	entry.res = &entry.__res;

	msi_addr &= ~(SZ_32K - 1);
	entry.res->start = msi_addr;
	entry.res->end = msi_addr + SZ_32K - 1;

	ret = iproc_pcie_setup_ib(pcie, &entry, IPROC_PCIE_IB_MAP_IO);
	return ret;
}

static void iproc_pcie_paxc_v2_msi_steer(struct iproc_pcie *pcie, u64 msi_addr,
					 bool enable)
{
	u32 val;

	if (!enable) {
		/*
		 * Disable PAXC MSI steering. All write transfers will be
		 * treated as non-MSI transfers
		 */
		val = iproc_pcie_read_reg(pcie, IPROC_PCIE_MSI_EN_CFG);
		val &= ~MSI_ENABLE_CFG;
		iproc_pcie_write_reg(pcie, IPROC_PCIE_MSI_EN_CFG, val);
		return;
	}

	/*
	 * Program bits [43:13] of address of GITS_TRANSLATER register into
	 * bits [30:0] of the MSI base address register.  In fact, in all iProc
	 * based SoCs, all I/O register bases are well below the 32-bit
	 * boundary, so we can safely assume bits [43:32] are always zeros.
	 */
	iproc_pcie_write_reg(pcie, IPROC_PCIE_MSI_BASE_ADDR,
			     (u32)(msi_addr >> 13));

	/* use a default 8K window size */
	iproc_pcie_write_reg(pcie, IPROC_PCIE_MSI_WINDOW_SIZE, 0);

	/* steering MSI to GICv3 ITS */
	val = iproc_pcie_read_reg(pcie, IPROC_PCIE_MSI_GIC_MODE);
	val |= GIC_V3_CFG;
	iproc_pcie_write_reg(pcie, IPROC_PCIE_MSI_GIC_MODE, val);

	/*
	 * Program bits [43:2] of address of GITS_TRANSLATER register into the
	 * iProc MSI address registers.
	 */
	msi_addr >>= 2;
	iproc_pcie_write_reg(pcie, IPROC_PCIE_MSI_ADDR_HI,
			     upper_32_bits(msi_addr));
	iproc_pcie_write_reg(pcie, IPROC_PCIE_MSI_ADDR_LO,
			     lower_32_bits(msi_addr));

	/* enable MSI */
	val = iproc_pcie_read_reg(pcie, IPROC_PCIE_MSI_EN_CFG);
	val |= MSI_ENABLE_CFG;
	iproc_pcie_write_reg(pcie, IPROC_PCIE_MSI_EN_CFG, val);
}

static int iproc_pcie_msi_steer(struct iproc_pcie *pcie,
				struct device_node *msi_node)
{
	struct device *dev = pcie->dev;
	int ret;
	u64 msi_addr;

	ret = iproce_pcie_get_msi(pcie, msi_node, &msi_addr);
	if (ret < 0) {
		dev_err(dev, "msi steering failed\n");
		return ret;
	}

	switch (pcie->type) {
	case IPROC_PCIE_PAXB_V2:
		ret = iproc_pcie_paxb_v2_msi_steer(pcie, msi_addr);
		if (ret)
			return ret;
		break;
	case IPROC_PCIE_PAXC_V2:
		iproc_pcie_paxc_v2_msi_steer(pcie, msi_addr, true);
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int iproc_pcie_msi_enable(struct iproc_pcie *pcie)
{
	struct device_node *msi_node;
	int ret;

	/*
	 * Either the "msi-parent" or the "msi-map" phandle needs to exist
	 * for us to obtain the MSI node.
	 */

	msi_node = of_parse_phandle(pcie->dev->of_node, "msi-parent", 0);
	if (!msi_node) {
		const __be32 *msi_map = NULL;
		int len;
		u32 phandle;

		msi_map = of_get_property(pcie->dev->of_node, "msi-map", &len);
		if (!msi_map)
			return -ENODEV;

		phandle = be32_to_cpup(msi_map + 1);
		msi_node = of_find_node_by_phandle(phandle);
		if (!msi_node)
			return -ENODEV;
	}

	/*
	 * Certain revisions of the iProc PCIe controller require additional
	 * configurations to steer the MSI writes towards an external MSI
	 * controller.
	 */
	if (pcie->need_msi_steer) {
		ret = iproc_pcie_msi_steer(pcie, msi_node);
		if (ret)
			goto out_put_node;
	}

	/*
	 * If another MSI controller is being used, the call below should fail
	 * but that is okay
	 */
	ret = iproc_msi_init(pcie, msi_node);

out_put_node:
	of_node_put(msi_node);
	return ret;
}

static void iproc_pcie_msi_disable(struct iproc_pcie *pcie)
{
	iproc_msi_exit(pcie);
}

static int iproc_pcie_rev_init(struct iproc_pcie *pcie)
{
	struct device *dev = pcie->dev;
	unsigned int reg_idx;
	const u16 *regs;

	switch (pcie->type) {
	case IPROC_PCIE_PAXB_BCMA:
		regs = iproc_pcie_reg_paxb_bcma;
		break;
	case IPROC_PCIE_PAXB:
		regs = iproc_pcie_reg_paxb;
		pcie->has_apb_err_disable = true;
		if (pcie->need_ob_cfg) {
			pcie->ob_map = paxb_ob_map;
			pcie->ob.nr_windows = ARRAY_SIZE(paxb_ob_map);
		}
		break;
	case IPROC_PCIE_PAXB_V2:
		regs = iproc_pcie_reg_paxb_v2;
		pcie->iproc_cfg_read = true;
		pcie->has_apb_err_disable = true;
		if (pcie->need_ob_cfg) {
			pcie->ob_map = paxb_v2_ob_map;
			pcie->ob.nr_windows = ARRAY_SIZE(paxb_v2_ob_map);
		}
		pcie->ib.nr_regions = ARRAY_SIZE(paxb_v2_ib_map);
		pcie->ib_map = paxb_v2_ib_map;
		pcie->need_msi_steer = true;
		dev_warn(dev, "reads of config registers that contain %#x return incorrect data\n",
			 CFG_RETRY_STATUS);
		break;
	case IPROC_PCIE_PAXC:
		regs = iproc_pcie_reg_paxc;
		pcie->ep_is_internal = true;
		pcie->iproc_cfg_read = true;
		pcie->rej_unconfig_pf = true;
		break;
	case IPROC_PCIE_PAXC_V2:
		regs = iproc_pcie_reg_paxc_v2;
		pcie->ep_is_internal = true;
		pcie->iproc_cfg_read = true;
		pcie->rej_unconfig_pf = true;
		pcie->need_msi_steer = true;
		break;
	default:
		dev_err(dev, "incompatible iProc PCIe interface\n");
		return -EINVAL;
	}

	pcie->reg_offsets = devm_kcalloc(dev, IPROC_PCIE_MAX_NUM_REG,
					 sizeof(*pcie->reg_offsets),
					 GFP_KERNEL);
	if (!pcie->reg_offsets)
		return -ENOMEM;

	/* go through the register table and populate all valid registers */
	pcie->reg_offsets[0] = (pcie->type == IPROC_PCIE_PAXC_V2) ?
		IPROC_PCIE_REG_INVALID : regs[0];
	for (reg_idx = 1; reg_idx < IPROC_PCIE_MAX_NUM_REG; reg_idx++)
		pcie->reg_offsets[reg_idx] = regs[reg_idx] ?
			regs[reg_idx] : IPROC_PCIE_REG_INVALID;

	return 0;
}

int iproc_pcie_setup(struct iproc_pcie *pcie, struct list_head *res)
{
	struct device *dev;
	int ret;
	struct pci_host_bridge *host = pci_host_bridge_from_priv(pcie);

	dev = pcie->dev;

	ret = iproc_pcie_rev_init(pcie);
	if (ret) {
		dev_err(dev, "unable to initialize controller parameters\n");
		return ret;
	}

	ret = phy_init(pcie->phy);
	if (ret) {
		dev_err(dev, "unable to initialize PCIe PHY\n");
		return ret;
	}

	ret = phy_power_on(pcie->phy);
	if (ret) {
		dev_err(dev, "unable to power on PCIe PHY\n");
		goto err_exit_phy;
	}

	iproc_pcie_perst_ctrl(pcie, true);
	iproc_pcie_perst_ctrl(pcie, false);

	iproc_pcie_invalidate_mapping(pcie);

	if (pcie->need_ob_cfg) {
		ret = iproc_pcie_map_ranges(pcie, res);
		if (ret) {
			dev_err(dev, "map failed\n");
			goto err_power_off_phy;
		}
	}

	if (pcie->need_ib_cfg) {
		ret = iproc_pcie_map_dma_ranges(pcie);
		if (ret && ret != -ENOENT)
			goto err_power_off_phy;
	}

	ret = iproc_pcie_check_link(pcie);
	if (ret) {
		dev_err(dev, "no PCIe EP device detected\n");
		goto err_power_off_phy;
	}

	iproc_pcie_enable(pcie);

	if (IS_ENABLED(CONFIG_PCI_MSI))
		if (iproc_pcie_msi_enable(pcie))
			dev_info(dev, "not using iProc MSI\n");

	host->ops = &iproc_pcie_ops;
	host->sysdata = pcie;
	host->map_irq = pcie->map_irq;

	ret = pci_host_probe(host);
	if (ret < 0) {
		dev_err(dev, "failed to scan host: %d\n", ret);
		goto err_power_off_phy;
	}

	return 0;

err_power_off_phy:
	phy_power_off(pcie->phy);
err_exit_phy:
	phy_exit(pcie->phy);
	return ret;
}
EXPORT_SYMBOL(iproc_pcie_setup);

int iproc_pcie_remove(struct iproc_pcie *pcie)
{
	struct pci_host_bridge *host = pci_host_bridge_from_priv(pcie);

	pci_stop_root_bus(host->bus);
	pci_remove_root_bus(host->bus);

	iproc_pcie_msi_disable(pcie);

	phy_power_off(pcie->phy);
	phy_exit(pcie->phy);

	return 0;
}
EXPORT_SYMBOL(iproc_pcie_remove);

/*
 * The MSI parsing logic in certain revisions of Broadcom PAXC based root
 * complex does not work and needs to be disabled
 */
static void quirk_paxc_disable_msi_parsing(struct pci_dev *pdev)
{
	struct iproc_pcie *pcie = iproc_data(pdev->bus);

	if (pdev->hdr_type == PCI_HEADER_TYPE_BRIDGE)
		iproc_pcie_paxc_v2_msi_steer(pcie, 0, false);
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_BROADCOM, 0x16f0,
			quirk_paxc_disable_msi_parsing);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_BROADCOM, 0xd802,
			quirk_paxc_disable_msi_parsing);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_BROADCOM, 0xd804,
			quirk_paxc_disable_msi_parsing);

static void quirk_paxc_bridge(struct pci_dev *pdev)
{
	/*
	 * The PCI config space is shared with the PAXC root port and the first
	 * Ethernet device.  So, we need to workaround this by telling the PCI
	 * code that the bridge is not an Ethernet device.
	 */
	if (pdev->hdr_type == PCI_HEADER_TYPE_BRIDGE)
		pdev->class = PCI_CLASS_BRIDGE_PCI << 8;

	/*
	 * MPSS is not being set properly (as it is currently 0).  This is
	 * because that area of the PCI config space is hard coded to zero, and
	 * is not modifiable by firmware.  Set this to 2 (e.g., 512 byte MPS)
	 * so that the MPS can be set to the real max value.
	 */
	pdev->pcie_mpss = 2;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_BROADCOM, 0x16cd, quirk_paxc_bridge);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_BROADCOM, 0x16f0, quirk_paxc_bridge);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_BROADCOM, 0xd750, quirk_paxc_bridge);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_BROADCOM, 0xd802, quirk_paxc_bridge);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_BROADCOM, 0xd804, quirk_paxc_bridge);

MODULE_AUTHOR("Ray Jui <rjui@broadcom.com>");
MODULE_DESCRIPTION("Broadcom iPROC PCIe common driver");
MODULE_LICENSE("GPL v2");
