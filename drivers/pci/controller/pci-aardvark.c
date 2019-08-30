// SPDX-License-Identifier: GPL-2.0
/*
 * Driver for the Aardvark PCIe controller, used on Marvell Armada
 * 3700.
 *
 * Copyright (C) 2016 Marvell
 *
 * Author: Hezi Shahmoon <hezi.shahmoon@marvell.com>
 */

#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>

#include "../pci.h"
#include "../pci-bridge-emul.h"

/* PCIe core registers */
#define PCIE_CORE_DEV_ID_REG					0x0
#define PCIE_CORE_CMD_STATUS_REG				0x4
#define     PCIE_CORE_CMD_IO_ACCESS_EN				BIT(0)
#define     PCIE_CORE_CMD_MEM_ACCESS_EN				BIT(1)
#define     PCIE_CORE_CMD_MEM_IO_REQ_EN				BIT(2)
#define PCIE_CORE_DEV_REV_REG					0x8
#define PCIE_CORE_PCIEXP_CAP					0xc0
#define PCIE_CORE_DEV_CTRL_STATS_REG				0xc8
#define     PCIE_CORE_DEV_CTRL_STATS_RELAX_ORDER_DISABLE	(0 << 4)
#define     PCIE_CORE_DEV_CTRL_STATS_MAX_PAYLOAD_SZ_SHIFT	5
#define     PCIE_CORE_DEV_CTRL_STATS_SNOOP_DISABLE		(0 << 11)
#define     PCIE_CORE_DEV_CTRL_STATS_MAX_RD_REQ_SIZE_SHIFT	12
#define     PCIE_CORE_DEV_CTRL_STATS_MAX_RD_REQ_SZ		0x2
#define PCIE_CORE_LINK_CTRL_STAT_REG				0xd0
#define     PCIE_CORE_LINK_L0S_ENTRY				BIT(0)
#define     PCIE_CORE_LINK_TRAINING				BIT(5)
#define     PCIE_CORE_LINK_WIDTH_SHIFT				20
#define PCIE_CORE_ERR_CAPCTL_REG				0x118
#define     PCIE_CORE_ERR_CAPCTL_ECRC_CHK_TX			BIT(5)
#define     PCIE_CORE_ERR_CAPCTL_ECRC_CHK_TX_EN			BIT(6)
#define     PCIE_CORE_ERR_CAPCTL_ECRC_CHCK			BIT(7)
#define     PCIE_CORE_ERR_CAPCTL_ECRC_CHCK_RCV			BIT(8)
#define     PCIE_CORE_INT_A_ASSERT_ENABLE			1
#define     PCIE_CORE_INT_B_ASSERT_ENABLE			2
#define     PCIE_CORE_INT_C_ASSERT_ENABLE			3
#define     PCIE_CORE_INT_D_ASSERT_ENABLE			4
/* PIO registers base address and register offsets */
#define PIO_BASE_ADDR				0x4000
#define PIO_CTRL				(PIO_BASE_ADDR + 0x0)
#define   PIO_CTRL_TYPE_MASK			GENMASK(3, 0)
#define   PIO_CTRL_ADDR_WIN_DISABLE		BIT(24)
#define PIO_STAT				(PIO_BASE_ADDR + 0x4)
#define   PIO_COMPLETION_STATUS_SHIFT		7
#define   PIO_COMPLETION_STATUS_MASK		GENMASK(9, 7)
#define   PIO_COMPLETION_STATUS_OK		0
#define   PIO_COMPLETION_STATUS_UR		1
#define   PIO_COMPLETION_STATUS_CRS		2
#define   PIO_COMPLETION_STATUS_CA		4
#define   PIO_NON_POSTED_REQ			BIT(0)
#define PIO_ADDR_LS				(PIO_BASE_ADDR + 0x8)
#define PIO_ADDR_MS				(PIO_BASE_ADDR + 0xc)
#define PIO_WR_DATA				(PIO_BASE_ADDR + 0x10)
#define PIO_WR_DATA_STRB			(PIO_BASE_ADDR + 0x14)
#define PIO_RD_DATA				(PIO_BASE_ADDR + 0x18)
#define PIO_START				(PIO_BASE_ADDR + 0x1c)
#define PIO_ISR					(PIO_BASE_ADDR + 0x20)
#define PIO_ISRM				(PIO_BASE_ADDR + 0x24)

/* Aardvark Control registers */
#define CONTROL_BASE_ADDR			0x4800
#define PCIE_CORE_CTRL0_REG			(CONTROL_BASE_ADDR + 0x0)
#define     PCIE_GEN_SEL_MSK			0x3
#define     PCIE_GEN_SEL_SHIFT			0x0
#define     SPEED_GEN_1				0
#define     SPEED_GEN_2				1
#define     SPEED_GEN_3				2
#define     IS_RC_MSK				1
#define     IS_RC_SHIFT				2
#define     LANE_CNT_MSK			0x18
#define     LANE_CNT_SHIFT			0x3
#define     LANE_COUNT_1			(0 << LANE_CNT_SHIFT)
#define     LANE_COUNT_2			(1 << LANE_CNT_SHIFT)
#define     LANE_COUNT_4			(2 << LANE_CNT_SHIFT)
#define     LANE_COUNT_8			(3 << LANE_CNT_SHIFT)
#define     LINK_TRAINING_EN			BIT(6)
#define     LEGACY_INTA				BIT(28)
#define     LEGACY_INTB				BIT(29)
#define     LEGACY_INTC				BIT(30)
#define     LEGACY_INTD				BIT(31)
#define PCIE_CORE_CTRL1_REG			(CONTROL_BASE_ADDR + 0x4)
#define     HOT_RESET_GEN			BIT(0)
#define PCIE_CORE_CTRL2_REG			(CONTROL_BASE_ADDR + 0x8)
#define     PCIE_CORE_CTRL2_RESERVED		0x7
#define     PCIE_CORE_CTRL2_TD_ENABLE		BIT(4)
#define     PCIE_CORE_CTRL2_STRICT_ORDER_ENABLE	BIT(5)
#define     PCIE_CORE_CTRL2_OB_WIN_ENABLE	BIT(6)
#define     PCIE_CORE_CTRL2_MSI_ENABLE		BIT(10)
#define PCIE_MSG_LOG_REG			(CONTROL_BASE_ADDR + 0x30)
#define PCIE_ISR0_REG				(CONTROL_BASE_ADDR + 0x40)
#define PCIE_MSG_PM_PME_MASK			BIT(7)
#define PCIE_ISR0_MASK_REG			(CONTROL_BASE_ADDR + 0x44)
#define     PCIE_ISR0_MSI_INT_PENDING		BIT(24)
#define     PCIE_ISR0_INTX_ASSERT(val)		BIT(16 + (val))
#define     PCIE_ISR0_INTX_DEASSERT(val)	BIT(20 + (val))
#define	    PCIE_ISR0_ALL_MASK			GENMASK(26, 0)
#define PCIE_ISR1_REG				(CONTROL_BASE_ADDR + 0x48)
#define PCIE_ISR1_MASK_REG			(CONTROL_BASE_ADDR + 0x4C)
#define     PCIE_ISR1_POWER_STATE_CHANGE	BIT(4)
#define     PCIE_ISR1_FLUSH			BIT(5)
#define     PCIE_ISR1_INTX_ASSERT(val)		BIT(8 + (val))
#define     PCIE_ISR1_ALL_MASK			GENMASK(11, 4)
#define PCIE_MSI_ADDR_LOW_REG			(CONTROL_BASE_ADDR + 0x50)
#define PCIE_MSI_ADDR_HIGH_REG			(CONTROL_BASE_ADDR + 0x54)
#define PCIE_MSI_STATUS_REG			(CONTROL_BASE_ADDR + 0x58)
#define PCIE_MSI_MASK_REG			(CONTROL_BASE_ADDR + 0x5C)
#define PCIE_MSI_PAYLOAD_REG			(CONTROL_BASE_ADDR + 0x9C)

/* LMI registers base address and register offsets */
#define LMI_BASE_ADDR				0x6000
#define CFG_REG					(LMI_BASE_ADDR + 0x0)
#define     LTSSM_SHIFT				24
#define     LTSSM_MASK				0x3f
#define     LTSSM_L0				0x10
#define     RC_BAR_CONFIG			0x300

/* PCIe core controller registers */
#define CTRL_CORE_BASE_ADDR			0x18000
#define CTRL_CONFIG_REG				(CTRL_CORE_BASE_ADDR + 0x0)
#define     CTRL_MODE_SHIFT			0x0
#define     CTRL_MODE_MASK			0x1
#define     PCIE_CORE_MODE_DIRECT		0x0
#define     PCIE_CORE_MODE_COMMAND		0x1

/* PCIe Central Interrupts Registers */
#define CENTRAL_INT_BASE_ADDR			0x1b000
#define HOST_CTRL_INT_STATUS_REG		(CENTRAL_INT_BASE_ADDR + 0x0)
#define HOST_CTRL_INT_MASK_REG			(CENTRAL_INT_BASE_ADDR + 0x4)
#define     PCIE_IRQ_CMDQ_INT			BIT(0)
#define     PCIE_IRQ_MSI_STATUS_INT		BIT(1)
#define     PCIE_IRQ_CMD_SENT_DONE		BIT(3)
#define     PCIE_IRQ_DMA_INT			BIT(4)
#define     PCIE_IRQ_IB_DXFERDONE		BIT(5)
#define     PCIE_IRQ_OB_DXFERDONE		BIT(6)
#define     PCIE_IRQ_OB_RXFERDONE		BIT(7)
#define     PCIE_IRQ_COMPQ_INT			BIT(12)
#define     PCIE_IRQ_DIR_RD_DDR_DET		BIT(13)
#define     PCIE_IRQ_DIR_WR_DDR_DET		BIT(14)
#define     PCIE_IRQ_CORE_INT			BIT(16)
#define     PCIE_IRQ_CORE_INT_PIO		BIT(17)
#define     PCIE_IRQ_DPMU_INT			BIT(18)
#define     PCIE_IRQ_PCIE_MIS_INT		BIT(19)
#define     PCIE_IRQ_MSI_INT1_DET		BIT(20)
#define     PCIE_IRQ_MSI_INT2_DET		BIT(21)
#define     PCIE_IRQ_RC_DBELL_DET		BIT(22)
#define     PCIE_IRQ_EP_STATUS			BIT(23)
#define     PCIE_IRQ_ALL_MASK			0xfff0fb
#define     PCIE_IRQ_ENABLE_INTS_MASK		PCIE_IRQ_CORE_INT

/* Transaction types */
#define PCIE_CONFIG_RD_TYPE0			0x8
#define PCIE_CONFIG_RD_TYPE1			0x9
#define PCIE_CONFIG_WR_TYPE0			0xa
#define PCIE_CONFIG_WR_TYPE1			0xb

#define PCIE_CONF_BUS(bus)			(((bus) & 0xff) << 20)
#define PCIE_CONF_DEV(dev)			(((dev) & 0x1f) << 15)
#define PCIE_CONF_FUNC(fun)			(((fun) & 0x7)	<< 12)
#define PCIE_CONF_REG(reg)			((reg) & 0xffc)
#define PCIE_CONF_ADDR(bus, devfn, where)	\
	(PCIE_CONF_BUS(bus) | PCIE_CONF_DEV(PCI_SLOT(devfn))	| \
	 PCIE_CONF_FUNC(PCI_FUNC(devfn)) | PCIE_CONF_REG(where))

#define PIO_TIMEOUT_MS			1

#define LINK_WAIT_MAX_RETRIES		10
#define LINK_WAIT_USLEEP_MIN		90000
#define LINK_WAIT_USLEEP_MAX		100000

#define MSI_IRQ_NUM			32

struct advk_pcie {
	struct platform_device *pdev;
	void __iomem *base;
	struct list_head resources;
	struct irq_domain *irq_domain;
	struct irq_chip irq_chip;
	struct irq_domain *msi_domain;
	struct irq_domain *msi_inner_domain;
	struct irq_chip msi_bottom_irq_chip;
	struct irq_chip msi_irq_chip;
	struct msi_domain_info msi_domain_info;
	DECLARE_BITMAP(msi_used, MSI_IRQ_NUM);
	struct mutex msi_used_lock;
	u16 msi_msg;
	int root_bus_nr;
	struct pci_bridge_emul bridge;
};

static inline void advk_writel(struct advk_pcie *pcie, u32 val, u64 reg)
{
	writel(val, pcie->base + reg);
}

static inline u32 advk_readl(struct advk_pcie *pcie, u64 reg)
{
	return readl(pcie->base + reg);
}

static int advk_pcie_link_up(struct advk_pcie *pcie)
{
	u32 val, ltssm_state;

	val = advk_readl(pcie, CFG_REG);
	ltssm_state = (val >> LTSSM_SHIFT) & LTSSM_MASK;
	return ltssm_state >= LTSSM_L0;
}

static int advk_pcie_wait_for_link(struct advk_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	int retries;

	/* check if the link is up or not */
	for (retries = 0; retries < LINK_WAIT_MAX_RETRIES; retries++) {
		if (advk_pcie_link_up(pcie)) {
			dev_info(dev, "link up\n");
			return 0;
		}

		usleep_range(LINK_WAIT_USLEEP_MIN, LINK_WAIT_USLEEP_MAX);
	}

	dev_err(dev, "link never came up\n");
	return -ETIMEDOUT;
}

static void advk_pcie_setup_hw(struct advk_pcie *pcie)
{
	u32 reg;

	/* Set to Direct mode */
	reg = advk_readl(pcie, CTRL_CONFIG_REG);
	reg &= ~(CTRL_MODE_MASK << CTRL_MODE_SHIFT);
	reg |= ((PCIE_CORE_MODE_DIRECT & CTRL_MODE_MASK) << CTRL_MODE_SHIFT);
	advk_writel(pcie, reg, CTRL_CONFIG_REG);

	/* Set PCI global control register to RC mode */
	reg = advk_readl(pcie, PCIE_CORE_CTRL0_REG);
	reg |= (IS_RC_MSK << IS_RC_SHIFT);
	advk_writel(pcie, reg, PCIE_CORE_CTRL0_REG);

	/* Set Advanced Error Capabilities and Control PF0 register */
	reg = PCIE_CORE_ERR_CAPCTL_ECRC_CHK_TX |
		PCIE_CORE_ERR_CAPCTL_ECRC_CHK_TX_EN |
		PCIE_CORE_ERR_CAPCTL_ECRC_CHCK |
		PCIE_CORE_ERR_CAPCTL_ECRC_CHCK_RCV;
	advk_writel(pcie, reg, PCIE_CORE_ERR_CAPCTL_REG);

	/* Set PCIe Device Control and Status 1 PF0 register */
	reg = PCIE_CORE_DEV_CTRL_STATS_RELAX_ORDER_DISABLE |
		(7 << PCIE_CORE_DEV_CTRL_STATS_MAX_PAYLOAD_SZ_SHIFT) |
		PCIE_CORE_DEV_CTRL_STATS_SNOOP_DISABLE |
		(PCIE_CORE_DEV_CTRL_STATS_MAX_RD_REQ_SZ <<
		 PCIE_CORE_DEV_CTRL_STATS_MAX_RD_REQ_SIZE_SHIFT);
	advk_writel(pcie, reg, PCIE_CORE_DEV_CTRL_STATS_REG);

	/* Program PCIe Control 2 to disable strict ordering */
	reg = PCIE_CORE_CTRL2_RESERVED |
		PCIE_CORE_CTRL2_TD_ENABLE;
	advk_writel(pcie, reg, PCIE_CORE_CTRL2_REG);

	/* Set GEN2 */
	reg = advk_readl(pcie, PCIE_CORE_CTRL0_REG);
	reg &= ~PCIE_GEN_SEL_MSK;
	reg |= SPEED_GEN_2;
	advk_writel(pcie, reg, PCIE_CORE_CTRL0_REG);

	/* Set lane X1 */
	reg = advk_readl(pcie, PCIE_CORE_CTRL0_REG);
	reg &= ~LANE_CNT_MSK;
	reg |= LANE_COUNT_1;
	advk_writel(pcie, reg, PCIE_CORE_CTRL0_REG);

	/* Enable link training */
	reg = advk_readl(pcie, PCIE_CORE_CTRL0_REG);
	reg |= LINK_TRAINING_EN;
	advk_writel(pcie, reg, PCIE_CORE_CTRL0_REG);

	/* Enable MSI */
	reg = advk_readl(pcie, PCIE_CORE_CTRL2_REG);
	reg |= PCIE_CORE_CTRL2_MSI_ENABLE;
	advk_writel(pcie, reg, PCIE_CORE_CTRL2_REG);

	/* Clear all interrupts */
	advk_writel(pcie, PCIE_ISR0_ALL_MASK, PCIE_ISR0_REG);
	advk_writel(pcie, PCIE_ISR1_ALL_MASK, PCIE_ISR1_REG);
	advk_writel(pcie, PCIE_IRQ_ALL_MASK, HOST_CTRL_INT_STATUS_REG);

	/* Disable All ISR0/1 Sources */
	reg = PCIE_ISR0_ALL_MASK;
	reg &= ~PCIE_ISR0_MSI_INT_PENDING;
	advk_writel(pcie, reg, PCIE_ISR0_MASK_REG);

	advk_writel(pcie, PCIE_ISR1_ALL_MASK, PCIE_ISR1_MASK_REG);

	/* Unmask all MSI's */
	advk_writel(pcie, 0, PCIE_MSI_MASK_REG);

	/* Enable summary interrupt for GIC SPI source */
	reg = PCIE_IRQ_ALL_MASK & (~PCIE_IRQ_ENABLE_INTS_MASK);
	advk_writel(pcie, reg, HOST_CTRL_INT_MASK_REG);

	reg = advk_readl(pcie, PCIE_CORE_CTRL2_REG);
	reg |= PCIE_CORE_CTRL2_OB_WIN_ENABLE;
	advk_writel(pcie, reg, PCIE_CORE_CTRL2_REG);

	/* Bypass the address window mapping for PIO */
	reg = advk_readl(pcie, PIO_CTRL);
	reg |= PIO_CTRL_ADDR_WIN_DISABLE;
	advk_writel(pcie, reg, PIO_CTRL);

	/* Start link training */
	reg = advk_readl(pcie, PCIE_CORE_LINK_CTRL_STAT_REG);
	reg |= PCIE_CORE_LINK_TRAINING;
	advk_writel(pcie, reg, PCIE_CORE_LINK_CTRL_STAT_REG);

	advk_pcie_wait_for_link(pcie);

	reg = PCIE_CORE_LINK_L0S_ENTRY |
		(1 << PCIE_CORE_LINK_WIDTH_SHIFT);
	advk_writel(pcie, reg, PCIE_CORE_LINK_CTRL_STAT_REG);

	reg = advk_readl(pcie, PCIE_CORE_CMD_STATUS_REG);
	reg |= PCIE_CORE_CMD_MEM_ACCESS_EN |
		PCIE_CORE_CMD_IO_ACCESS_EN |
		PCIE_CORE_CMD_MEM_IO_REQ_EN;
	advk_writel(pcie, reg, PCIE_CORE_CMD_STATUS_REG);
}

static void advk_pcie_check_pio_status(struct advk_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	u32 reg;
	unsigned int status;
	char *strcomp_status, *str_posted;

	reg = advk_readl(pcie, PIO_STAT);
	status = (reg & PIO_COMPLETION_STATUS_MASK) >>
		PIO_COMPLETION_STATUS_SHIFT;

	if (!status)
		return;

	switch (status) {
	case PIO_COMPLETION_STATUS_UR:
		strcomp_status = "UR";
		break;
	case PIO_COMPLETION_STATUS_CRS:
		strcomp_status = "CRS";
		break;
	case PIO_COMPLETION_STATUS_CA:
		strcomp_status = "CA";
		break;
	default:
		strcomp_status = "Unknown";
		break;
	}

	if (reg & PIO_NON_POSTED_REQ)
		str_posted = "Non-posted";
	else
		str_posted = "Posted";

	dev_err(dev, "%s PIO Response Status: %s, %#x @ %#x\n",
		str_posted, strcomp_status, reg, advk_readl(pcie, PIO_ADDR_LS));
}

static int advk_pcie_wait_pio(struct advk_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	unsigned long timeout;

	timeout = jiffies + msecs_to_jiffies(PIO_TIMEOUT_MS);

	while (time_before(jiffies, timeout)) {
		u32 start, isr;

		start = advk_readl(pcie, PIO_START);
		isr = advk_readl(pcie, PIO_ISR);
		if (!start && isr)
			return 0;
	}

	dev_err(dev, "config read/write timed out\n");
	return -ETIMEDOUT;
}


static pci_bridge_emul_read_status_t
advk_pci_bridge_emul_pcie_conf_read(struct pci_bridge_emul *bridge,
				    int reg, u32 *value)
{
	struct advk_pcie *pcie = bridge->data;


	switch (reg) {
	case PCI_EXP_SLTCTL:
		*value = PCI_EXP_SLTSTA_PDS << 16;
		return PCI_BRIDGE_EMUL_HANDLED;

	case PCI_EXP_RTCTL: {
		u32 val = advk_readl(pcie, PCIE_ISR0_MASK_REG);
		*value = (val & PCIE_MSG_PM_PME_MASK) ? PCI_EXP_RTCTL_PMEIE : 0;
		return PCI_BRIDGE_EMUL_HANDLED;
	}

	case PCI_EXP_RTSTA: {
		u32 isr0 = advk_readl(pcie, PCIE_ISR0_REG);
		u32 msglog = advk_readl(pcie, PCIE_MSG_LOG_REG);
		*value = (isr0 & PCIE_MSG_PM_PME_MASK) << 16 | (msglog >> 16);
		return PCI_BRIDGE_EMUL_HANDLED;
	}

	case PCI_CAP_LIST_ID:
	case PCI_EXP_DEVCAP:
	case PCI_EXP_DEVCTL:
	case PCI_EXP_LNKCAP:
	case PCI_EXP_LNKCTL:
		*value = advk_readl(pcie, PCIE_CORE_PCIEXP_CAP + reg);
		return PCI_BRIDGE_EMUL_HANDLED;
	default:
		return PCI_BRIDGE_EMUL_NOT_HANDLED;
	}

}

static void
advk_pci_bridge_emul_pcie_conf_write(struct pci_bridge_emul *bridge,
				     int reg, u32 old, u32 new, u32 mask)
{
	struct advk_pcie *pcie = bridge->data;

	switch (reg) {
	case PCI_EXP_DEVCTL:
	case PCI_EXP_LNKCTL:
		advk_writel(pcie, new, PCIE_CORE_PCIEXP_CAP + reg);
		break;

	case PCI_EXP_RTCTL:
		new = (new & PCI_EXP_RTCTL_PMEIE) << 3;
		advk_writel(pcie, new, PCIE_ISR0_MASK_REG);
		break;

	case PCI_EXP_RTSTA:
		new = (new & PCI_EXP_RTSTA_PME) >> 9;
		advk_writel(pcie, new, PCIE_ISR0_REG);
		break;

	default:
		break;
	}
}

static struct pci_bridge_emul_ops advk_pci_bridge_emul_ops = {
	.read_pcie = advk_pci_bridge_emul_pcie_conf_read,
	.write_pcie = advk_pci_bridge_emul_pcie_conf_write,
};

/*
 * Initialize the configuration space of the PCI-to-PCI bridge
 * associated with the given PCIe interface.
 */
static void advk_sw_pci_bridge_init(struct advk_pcie *pcie)
{
	struct pci_bridge_emul *bridge = &pcie->bridge;

	bridge->conf.vendor = advk_readl(pcie, PCIE_CORE_DEV_ID_REG) & 0xffff;
	bridge->conf.device = advk_readl(pcie, PCIE_CORE_DEV_ID_REG) >> 16;
	bridge->conf.class_revision =
		advk_readl(pcie, PCIE_CORE_DEV_REV_REG) & 0xff;

	/* Support 32 bits I/O addressing */
	bridge->conf.iobase = PCI_IO_RANGE_TYPE_32;
	bridge->conf.iolimit = PCI_IO_RANGE_TYPE_32;

	/* Support 64 bits memory pref */
	bridge->conf.pref_mem_base = PCI_PREF_RANGE_TYPE_64;
	bridge->conf.pref_mem_limit = PCI_PREF_RANGE_TYPE_64;

	/* Support interrupt A for MSI feature */
	bridge->conf.intpin = PCIE_CORE_INT_A_ASSERT_ENABLE;

	bridge->has_pcie = true;
	bridge->data = pcie;
	bridge->ops = &advk_pci_bridge_emul_ops;

	pci_bridge_emul_init(bridge, 0);

}

static bool advk_pcie_valid_device(struct advk_pcie *pcie, struct pci_bus *bus,
				  int devfn)
{
	if ((bus->number == pcie->root_bus_nr) && PCI_SLOT(devfn) != 0)
		return false;

	return true;
}

static int advk_pcie_rd_conf(struct pci_bus *bus, u32 devfn,
			     int where, int size, u32 *val)
{
	struct advk_pcie *pcie = bus->sysdata;
	u32 reg;
	int ret;

	if (!advk_pcie_valid_device(pcie, bus, devfn)) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	if (bus->number == pcie->root_bus_nr)
		return pci_bridge_emul_conf_read(&pcie->bridge, where,
						 size, val);

	/* Start PIO */
	advk_writel(pcie, 0, PIO_START);
	advk_writel(pcie, 1, PIO_ISR);

	/* Program the control register */
	reg = advk_readl(pcie, PIO_CTRL);
	reg &= ~PIO_CTRL_TYPE_MASK;
	if (bus->primary ==  pcie->root_bus_nr)
		reg |= PCIE_CONFIG_RD_TYPE0;
	else
		reg |= PCIE_CONFIG_RD_TYPE1;
	advk_writel(pcie, reg, PIO_CTRL);

	/* Program the address registers */
	reg = PCIE_CONF_ADDR(bus->number, devfn, where);
	advk_writel(pcie, reg, PIO_ADDR_LS);
	advk_writel(pcie, 0, PIO_ADDR_MS);

	/* Program the data strobe */
	advk_writel(pcie, 0xf, PIO_WR_DATA_STRB);

	/* Start the transfer */
	advk_writel(pcie, 1, PIO_START);

	ret = advk_pcie_wait_pio(pcie);
	if (ret < 0)
		return PCIBIOS_SET_FAILED;

	advk_pcie_check_pio_status(pcie);

	/* Get the read result */
	*val = advk_readl(pcie, PIO_RD_DATA);
	if (size == 1)
		*val = (*val >> (8 * (where & 3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (8 * (where & 3))) & 0xffff;

	return PCIBIOS_SUCCESSFUL;
}

static int advk_pcie_wr_conf(struct pci_bus *bus, u32 devfn,
				int where, int size, u32 val)
{
	struct advk_pcie *pcie = bus->sysdata;
	u32 reg;
	u32 data_strobe = 0x0;
	int offset;
	int ret;

	if (!advk_pcie_valid_device(pcie, bus, devfn))
		return PCIBIOS_DEVICE_NOT_FOUND;

	if (bus->number == pcie->root_bus_nr)
		return pci_bridge_emul_conf_write(&pcie->bridge, where,
						  size, val);

	if (where % size)
		return PCIBIOS_SET_FAILED;

	/* Start PIO */
	advk_writel(pcie, 0, PIO_START);
	advk_writel(pcie, 1, PIO_ISR);

	/* Program the control register */
	reg = advk_readl(pcie, PIO_CTRL);
	reg &= ~PIO_CTRL_TYPE_MASK;
	if (bus->primary == pcie->root_bus_nr)
		reg |= PCIE_CONFIG_WR_TYPE0;
	else
		reg |= PCIE_CONFIG_WR_TYPE1;
	advk_writel(pcie, reg, PIO_CTRL);

	/* Program the address registers */
	reg = PCIE_CONF_ADDR(bus->number, devfn, where);
	advk_writel(pcie, reg, PIO_ADDR_LS);
	advk_writel(pcie, 0, PIO_ADDR_MS);

	/* Calculate the write strobe */
	offset      = where & 0x3;
	reg         = val << (8 * offset);
	data_strobe = GENMASK(size - 1, 0) << offset;

	/* Program the data register */
	advk_writel(pcie, reg, PIO_WR_DATA);

	/* Program the data strobe */
	advk_writel(pcie, data_strobe, PIO_WR_DATA_STRB);

	/* Start the transfer */
	advk_writel(pcie, 1, PIO_START);

	ret = advk_pcie_wait_pio(pcie);
	if (ret < 0)
		return PCIBIOS_SET_FAILED;

	advk_pcie_check_pio_status(pcie);

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops advk_pcie_ops = {
	.read = advk_pcie_rd_conf,
	.write = advk_pcie_wr_conf,
};

static void advk_msi_irq_compose_msi_msg(struct irq_data *data,
					 struct msi_msg *msg)
{
	struct advk_pcie *pcie = irq_data_get_irq_chip_data(data);
	phys_addr_t msi_msg = virt_to_phys(&pcie->msi_msg);

	msg->address_lo = lower_32_bits(msi_msg);
	msg->address_hi = upper_32_bits(msi_msg);
	msg->data = data->irq;
}

static int advk_msi_set_affinity(struct irq_data *irq_data,
				 const struct cpumask *mask, bool force)
{
	return -EINVAL;
}

static int advk_msi_irq_domain_alloc(struct irq_domain *domain,
				     unsigned int virq,
				     unsigned int nr_irqs, void *args)
{
	struct advk_pcie *pcie = domain->host_data;
	int hwirq, i;

	mutex_lock(&pcie->msi_used_lock);
	hwirq = bitmap_find_next_zero_area(pcie->msi_used, MSI_IRQ_NUM,
					   0, nr_irqs, 0);
	if (hwirq >= MSI_IRQ_NUM) {
		mutex_unlock(&pcie->msi_used_lock);
		return -ENOSPC;
	}

	bitmap_set(pcie->msi_used, hwirq, nr_irqs);
	mutex_unlock(&pcie->msi_used_lock);

	for (i = 0; i < nr_irqs; i++)
		irq_domain_set_info(domain, virq + i, hwirq + i,
				    &pcie->msi_bottom_irq_chip,
				    domain->host_data, handle_simple_irq,
				    NULL, NULL);

	return hwirq;
}

static void advk_msi_irq_domain_free(struct irq_domain *domain,
				     unsigned int virq, unsigned int nr_irqs)
{
	struct irq_data *d = irq_domain_get_irq_data(domain, virq);
	struct advk_pcie *pcie = domain->host_data;

	mutex_lock(&pcie->msi_used_lock);
	bitmap_clear(pcie->msi_used, d->hwirq, nr_irqs);
	mutex_unlock(&pcie->msi_used_lock);
}

static const struct irq_domain_ops advk_msi_domain_ops = {
	.alloc = advk_msi_irq_domain_alloc,
	.free = advk_msi_irq_domain_free,
};

static void advk_pcie_irq_mask(struct irq_data *d)
{
	struct advk_pcie *pcie = d->domain->host_data;
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	u32 mask;

	mask = advk_readl(pcie, PCIE_ISR1_MASK_REG);
	mask |= PCIE_ISR1_INTX_ASSERT(hwirq);
	advk_writel(pcie, mask, PCIE_ISR1_MASK_REG);
}

static void advk_pcie_irq_unmask(struct irq_data *d)
{
	struct advk_pcie *pcie = d->domain->host_data;
	irq_hw_number_t hwirq = irqd_to_hwirq(d);
	u32 mask;

	mask = advk_readl(pcie, PCIE_ISR1_MASK_REG);
	mask &= ~PCIE_ISR1_INTX_ASSERT(hwirq);
	advk_writel(pcie, mask, PCIE_ISR1_MASK_REG);
}

static int advk_pcie_irq_map(struct irq_domain *h,
			     unsigned int virq, irq_hw_number_t hwirq)
{
	struct advk_pcie *pcie = h->host_data;

	advk_pcie_irq_mask(irq_get_irq_data(virq));
	irq_set_status_flags(virq, IRQ_LEVEL);
	irq_set_chip_and_handler(virq, &pcie->irq_chip,
				 handle_level_irq);
	irq_set_chip_data(virq, pcie);

	return 0;
}

static const struct irq_domain_ops advk_pcie_irq_domain_ops = {
	.map = advk_pcie_irq_map,
	.xlate = irq_domain_xlate_onecell,
};

static int advk_pcie_init_msi_irq_domain(struct advk_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	struct device_node *node = dev->of_node;
	struct irq_chip *bottom_ic, *msi_ic;
	struct msi_domain_info *msi_di;
	phys_addr_t msi_msg_phys;

	mutex_init(&pcie->msi_used_lock);

	bottom_ic = &pcie->msi_bottom_irq_chip;

	bottom_ic->name = "MSI";
	bottom_ic->irq_compose_msi_msg = advk_msi_irq_compose_msi_msg;
	bottom_ic->irq_set_affinity = advk_msi_set_affinity;

	msi_ic = &pcie->msi_irq_chip;
	msi_ic->name = "advk-MSI";

	msi_di = &pcie->msi_domain_info;
	msi_di->flags = MSI_FLAG_USE_DEF_DOM_OPS | MSI_FLAG_USE_DEF_CHIP_OPS |
		MSI_FLAG_MULTI_PCI_MSI;
	msi_di->chip = msi_ic;

	msi_msg_phys = virt_to_phys(&pcie->msi_msg);

	advk_writel(pcie, lower_32_bits(msi_msg_phys),
		    PCIE_MSI_ADDR_LOW_REG);
	advk_writel(pcie, upper_32_bits(msi_msg_phys),
		    PCIE_MSI_ADDR_HIGH_REG);

	pcie->msi_inner_domain =
		irq_domain_add_linear(NULL, MSI_IRQ_NUM,
				      &advk_msi_domain_ops, pcie);
	if (!pcie->msi_inner_domain)
		return -ENOMEM;

	pcie->msi_domain =
		pci_msi_create_irq_domain(of_node_to_fwnode(node),
					  msi_di, pcie->msi_inner_domain);
	if (!pcie->msi_domain) {
		irq_domain_remove(pcie->msi_inner_domain);
		return -ENOMEM;
	}

	return 0;
}

static void advk_pcie_remove_msi_irq_domain(struct advk_pcie *pcie)
{
	irq_domain_remove(pcie->msi_domain);
	irq_domain_remove(pcie->msi_inner_domain);
}

static int advk_pcie_init_irq_domain(struct advk_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	struct device_node *node = dev->of_node;
	struct device_node *pcie_intc_node;
	struct irq_chip *irq_chip;
	int ret = 0;

	pcie_intc_node =  of_get_next_child(node, NULL);
	if (!pcie_intc_node) {
		dev_err(dev, "No PCIe Intc node found\n");
		return -ENODEV;
	}

	irq_chip = &pcie->irq_chip;

	irq_chip->name = devm_kasprintf(dev, GFP_KERNEL, "%s-irq",
					dev_name(dev));
	if (!irq_chip->name) {
		ret = -ENOMEM;
		goto out_put_node;
	}

	irq_chip->irq_mask = advk_pcie_irq_mask;
	irq_chip->irq_mask_ack = advk_pcie_irq_mask;
	irq_chip->irq_unmask = advk_pcie_irq_unmask;

	pcie->irq_domain =
		irq_domain_add_linear(pcie_intc_node, PCI_NUM_INTX,
				      &advk_pcie_irq_domain_ops, pcie);
	if (!pcie->irq_domain) {
		dev_err(dev, "Failed to get a INTx IRQ domain\n");
		ret = -ENOMEM;
		goto out_put_node;
	}

out_put_node:
	of_node_put(pcie_intc_node);
	return ret;
}

static void advk_pcie_remove_irq_domain(struct advk_pcie *pcie)
{
	irq_domain_remove(pcie->irq_domain);
}

static void advk_pcie_handle_msi(struct advk_pcie *pcie)
{
	u32 msi_val, msi_mask, msi_status, msi_idx;
	u16 msi_data;

	msi_mask = advk_readl(pcie, PCIE_MSI_MASK_REG);
	msi_val = advk_readl(pcie, PCIE_MSI_STATUS_REG);
	msi_status = msi_val & ~msi_mask;

	for (msi_idx = 0; msi_idx < MSI_IRQ_NUM; msi_idx++) {
		if (!(BIT(msi_idx) & msi_status))
			continue;

		advk_writel(pcie, BIT(msi_idx), PCIE_MSI_STATUS_REG);
		msi_data = advk_readl(pcie, PCIE_MSI_PAYLOAD_REG) & 0xFF;
		generic_handle_irq(msi_data);
	}

	advk_writel(pcie, PCIE_ISR0_MSI_INT_PENDING,
		    PCIE_ISR0_REG);
}

static void advk_pcie_handle_int(struct advk_pcie *pcie)
{
	u32 isr0_val, isr0_mask, isr0_status;
	u32 isr1_val, isr1_mask, isr1_status;
	int i, virq;

	isr0_val = advk_readl(pcie, PCIE_ISR0_REG);
	isr0_mask = advk_readl(pcie, PCIE_ISR0_MASK_REG);
	isr0_status = isr0_val & ((~isr0_mask) & PCIE_ISR0_ALL_MASK);

	isr1_val = advk_readl(pcie, PCIE_ISR1_REG);
	isr1_mask = advk_readl(pcie, PCIE_ISR1_MASK_REG);
	isr1_status = isr1_val & ((~isr1_mask) & PCIE_ISR1_ALL_MASK);

	if (!isr0_status && !isr1_status) {
		advk_writel(pcie, isr0_val, PCIE_ISR0_REG);
		advk_writel(pcie, isr1_val, PCIE_ISR1_REG);
		return;
	}

	/* Process MSI interrupts */
	if (isr0_status & PCIE_ISR0_MSI_INT_PENDING)
		advk_pcie_handle_msi(pcie);

	/* Process legacy interrupts */
	for (i = 0; i < PCI_NUM_INTX; i++) {
		if (!(isr1_status & PCIE_ISR1_INTX_ASSERT(i)))
			continue;

		advk_writel(pcie, PCIE_ISR1_INTX_ASSERT(i),
			    PCIE_ISR1_REG);

		virq = irq_find_mapping(pcie->irq_domain, i);
		generic_handle_irq(virq);
	}
}

static irqreturn_t advk_pcie_irq_handler(int irq, void *arg)
{
	struct advk_pcie *pcie = arg;
	u32 status;

	status = advk_readl(pcie, HOST_CTRL_INT_STATUS_REG);
	if (!(status & PCIE_IRQ_CORE_INT))
		return IRQ_NONE;

	advk_pcie_handle_int(pcie);

	/* Clear interrupt */
	advk_writel(pcie, PCIE_IRQ_CORE_INT, HOST_CTRL_INT_STATUS_REG);

	return IRQ_HANDLED;
}

static int advk_pcie_parse_request_of_pci_ranges(struct advk_pcie *pcie)
{
	int err, res_valid = 0;
	struct device *dev = &pcie->pdev->dev;
	struct resource_entry *win, *tmp;
	resource_size_t iobase;

	INIT_LIST_HEAD(&pcie->resources);

	err = devm_of_pci_get_host_bridge_resources(dev, 0, 0xff,
						    &pcie->resources, &iobase);
	if (err)
		return err;

	err = devm_request_pci_bus_resources(dev, &pcie->resources);
	if (err)
		goto out_release_res;

	resource_list_for_each_entry_safe(win, tmp, &pcie->resources) {
		struct resource *res = win->res;

		switch (resource_type(res)) {
		case IORESOURCE_IO:
			err = devm_pci_remap_iospace(dev, res, iobase);
			if (err) {
				dev_warn(dev, "error %d: failed to map resource %pR\n",
					 err, res);
				resource_list_destroy_entry(win);
			}
			break;
		case IORESOURCE_MEM:
			res_valid |= !(res->flags & IORESOURCE_PREFETCH);
			break;
		case IORESOURCE_BUS:
			pcie->root_bus_nr = res->start;
			break;
		}
	}

	if (!res_valid) {
		dev_err(dev, "non-prefetchable memory resource required\n");
		err = -EINVAL;
		goto out_release_res;
	}

	return 0;

out_release_res:
	pci_free_resource_list(&pcie->resources);
	return err;
}

static int advk_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct advk_pcie *pcie;
	struct resource *res;
	struct pci_host_bridge *bridge;
	int ret, irq;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(struct advk_pcie));
	if (!bridge)
		return -ENOMEM;

	pcie = pci_host_bridge_priv(bridge);
	pcie->pdev = pdev;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	pcie->base = devm_ioremap_resource(dev, res);
	if (IS_ERR(pcie->base))
		return PTR_ERR(pcie->base);

	irq = platform_get_irq(pdev, 0);
	ret = devm_request_irq(dev, irq, advk_pcie_irq_handler,
			       IRQF_SHARED | IRQF_NO_THREAD, "advk-pcie",
			       pcie);
	if (ret) {
		dev_err(dev, "Failed to register interrupt\n");
		return ret;
	}

	ret = advk_pcie_parse_request_of_pci_ranges(pcie);
	if (ret) {
		dev_err(dev, "Failed to parse resources\n");
		return ret;
	}

	advk_pcie_setup_hw(pcie);

	advk_sw_pci_bridge_init(pcie);

	ret = advk_pcie_init_irq_domain(pcie);
	if (ret) {
		dev_err(dev, "Failed to initialize irq\n");
		return ret;
	}

	ret = advk_pcie_init_msi_irq_domain(pcie);
	if (ret) {
		dev_err(dev, "Failed to initialize irq\n");
		advk_pcie_remove_irq_domain(pcie);
		return ret;
	}

	list_splice_init(&pcie->resources, &bridge->windows);
	bridge->dev.parent = dev;
	bridge->sysdata = pcie;
	bridge->busnr = 0;
	bridge->ops = &advk_pcie_ops;
	bridge->map_irq = of_irq_parse_and_map_pci;
	bridge->swizzle_irq = pci_common_swizzle;

	ret = pci_host_probe(bridge);
	if (ret < 0) {
		advk_pcie_remove_msi_irq_domain(pcie);
		advk_pcie_remove_irq_domain(pcie);
		return ret;
	}

	return 0;
}

static const struct of_device_id advk_pcie_of_match_table[] = {
	{ .compatible = "marvell,armada-3700-pcie", },
	{},
};

static struct platform_driver advk_pcie_driver = {
	.driver = {
		.name = "advk-pcie",
		.of_match_table = advk_pcie_of_match_table,
		/* Driver unloading/unbinding currently not supported */
		.suppress_bind_attrs = true,
	},
	.probe = advk_pcie_probe,
};
builtin_platform_driver(advk_pcie_driver);
