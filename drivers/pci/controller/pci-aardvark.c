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
#include <linux/gpio/consumer.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/irqdomain.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/phy/phy.h>
#include <linux/platform_device.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_gpio.h>
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
#define PCIE_CORE_REF_CLK_REG			(CONTROL_BASE_ADDR + 0x14)
#define     PCIE_CORE_REF_CLK_TX_ENABLE		BIT(1)
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

#define PIO_RETRY_CNT			500
#define PIO_RETRY_DELAY			2 /* 2 us*/

#define LINK_WAIT_MAX_RETRIES		10
#define LINK_WAIT_USLEEP_MIN		90000
#define LINK_WAIT_USLEEP_MAX		100000
#define RETRAIN_WAIT_MAX_RETRIES	10
#define RETRAIN_WAIT_USLEEP_US		2000

#define MSI_IRQ_NUM			32

struct advk_pcie {
	struct platform_device *pdev;
	void __iomem *base;
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
	int link_gen;
	struct pci_bridge_emul bridge;
	struct gpio_desc *reset_gpio;
	struct phy *phy;
};

static inline void advk_writel(struct advk_pcie *pcie, u32 val, u64 reg)
{
	writel(val, pcie->base + reg);
}

static inline u32 advk_readl(struct advk_pcie *pcie, u64 reg)
{
	return readl(pcie->base + reg);
}

static inline u16 advk_read16(struct advk_pcie *pcie, u64 reg)
{
	return advk_readl(pcie, (reg & ~0x3)) >> ((reg & 0x3) * 8);
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
	int retries;

	/* check if the link is up or not */
	for (retries = 0; retries < LINK_WAIT_MAX_RETRIES; retries++) {
		if (advk_pcie_link_up(pcie))
			return 0;

		usleep_range(LINK_WAIT_USLEEP_MIN, LINK_WAIT_USLEEP_MAX);
	}

	return -ETIMEDOUT;
}

static void advk_pcie_wait_for_retrain(struct advk_pcie *pcie)
{
	size_t retries;

	for (retries = 0; retries < RETRAIN_WAIT_MAX_RETRIES; ++retries) {
		if (!advk_pcie_link_up(pcie))
			break;
		udelay(RETRAIN_WAIT_USLEEP_US);
	}
}

static void advk_pcie_issue_perst(struct advk_pcie *pcie)
{
	u32 reg;

	if (!pcie->reset_gpio)
		return;

	/* PERST does not work for some cards when link training is enabled */
	reg = advk_readl(pcie, PCIE_CORE_CTRL0_REG);
	reg &= ~LINK_TRAINING_EN;
	advk_writel(pcie, reg, PCIE_CORE_CTRL0_REG);

	/* 10ms delay is needed for some cards */
	dev_info(&pcie->pdev->dev, "issuing PERST via reset GPIO for 10ms\n");
	gpiod_set_value_cansleep(pcie->reset_gpio, 1);
	usleep_range(10000, 11000);
	gpiod_set_value_cansleep(pcie->reset_gpio, 0);
}

static int advk_pcie_train_at_gen(struct advk_pcie *pcie, int gen)
{
	int ret, neg_gen;
	u32 reg;

	/* Setup link speed */
	reg = advk_readl(pcie, PCIE_CORE_CTRL0_REG);
	reg &= ~PCIE_GEN_SEL_MSK;
	if (gen == 3)
		reg |= SPEED_GEN_3;
	else if (gen == 2)
		reg |= SPEED_GEN_2;
	else
		reg |= SPEED_GEN_1;
	advk_writel(pcie, reg, PCIE_CORE_CTRL0_REG);

	/*
	 * Enable link training. This is not needed in every call to this
	 * function, just once suffices, but it does not break anything either.
	 */
	reg = advk_readl(pcie, PCIE_CORE_CTRL0_REG);
	reg |= LINK_TRAINING_EN;
	advk_writel(pcie, reg, PCIE_CORE_CTRL0_REG);

	/*
	 * Start link training immediately after enabling it.
	 * This solves problems for some buggy cards.
	 */
	reg = advk_readl(pcie, PCIE_CORE_PCIEXP_CAP + PCI_EXP_LNKCTL);
	reg |= PCI_EXP_LNKCTL_RL;
	advk_writel(pcie, reg, PCIE_CORE_PCIEXP_CAP + PCI_EXP_LNKCTL);

	ret = advk_pcie_wait_for_link(pcie);
	if (ret)
		return ret;

	reg = advk_read16(pcie, PCIE_CORE_PCIEXP_CAP + PCI_EXP_LNKSTA);
	neg_gen = reg & PCI_EXP_LNKSTA_CLS;

	return neg_gen;
}

static void advk_pcie_train_link(struct advk_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	int neg_gen = -1, gen;

	/*
	 * Reset PCIe card via PERST# signal. Some cards are not detected
	 * during link training when they are in some non-initial state.
	 */
	advk_pcie_issue_perst(pcie);

	/*
	 * PERST# signal could have been asserted by pinctrl subsystem before
	 * probe() callback has been called or issued explicitly by reset gpio
	 * function advk_pcie_issue_perst(), making the endpoint going into
	 * fundamental reset. As required by PCI Express spec a delay for at
	 * least 100ms after such a reset before link training is needed.
	 */
	msleep(PCI_PM_D3COLD_WAIT);

	/*
	 * Try link training at link gen specified by device tree property
	 * 'max-link-speed'. If this fails, iteratively train at lower gen.
	 */
	for (gen = pcie->link_gen; gen > 0; --gen) {
		neg_gen = advk_pcie_train_at_gen(pcie, gen);
		if (neg_gen > 0)
			break;
	}

	if (neg_gen < 0)
		goto err;

	/*
	 * After successful training if negotiated gen is lower than requested,
	 * train again on negotiated gen. This solves some stability issues for
	 * some buggy gen1 cards.
	 */
	if (neg_gen < gen) {
		gen = neg_gen;
		neg_gen = advk_pcie_train_at_gen(pcie, gen);
	}

	if (neg_gen == gen) {
		dev_info(dev, "link up at gen %i\n", gen);
		return;
	}

err:
	dev_err(dev, "link never came up\n");
}

static void advk_pcie_setup_hw(struct advk_pcie *pcie)
{
	u32 reg;

	/* Enable TX */
	reg = advk_readl(pcie, PCIE_CORE_REF_CLK_REG);
	reg |= PCIE_CORE_REF_CLK_TX_ENABLE;
	advk_writel(pcie, reg, PCIE_CORE_REF_CLK_REG);

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

	/* Set PCIe Device Control register */
	reg = advk_readl(pcie, PCIE_CORE_PCIEXP_CAP + PCI_EXP_DEVCTL);
	reg &= ~PCI_EXP_DEVCTL_RELAX_EN;
	reg &= ~PCI_EXP_DEVCTL_NOSNOOP_EN;
	reg &= ~PCI_EXP_DEVCTL_READRQ;
	reg |= PCI_EXP_DEVCTL_PAYLOAD; /* Set max payload size */
	reg |= PCI_EXP_DEVCTL_READRQ_512B;
	advk_writel(pcie, reg, PCIE_CORE_PCIEXP_CAP + PCI_EXP_DEVCTL);

	/* Program PCIe Control 2 to disable strict ordering */
	reg = PCIE_CORE_CTRL2_RESERVED |
		PCIE_CORE_CTRL2_TD_ENABLE;
	advk_writel(pcie, reg, PCIE_CORE_CTRL2_REG);

	/* Set lane X1 */
	reg = advk_readl(pcie, PCIE_CORE_CTRL0_REG);
	reg &= ~LANE_CNT_MSK;
	reg |= LANE_COUNT_1;
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

	/* Unmask all MSIs */
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

	advk_pcie_train_link(pcie);

	/*
	 * FIXME: The following register update is suspicious. This register is
	 * applicable only when the PCI controller is configured for Endpoint
	 * mode, not as a Root Complex. But apparently when this code is
	 * removed, some cards stop working. This should be investigated and
	 * a comment explaining this should be put here.
	 */
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
	int i;

	for (i = 0; i < PIO_RETRY_CNT; i++) {
		u32 start, isr;

		start = advk_readl(pcie, PIO_START);
		isr = advk_readl(pcie, PIO_ISR);
		if (!start && isr)
			return 0;
		udelay(PIO_RETRY_DELAY);
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
		*value = (val & PCIE_MSG_PM_PME_MASK) ? 0 : PCI_EXP_RTCTL_PMEIE;
		return PCI_BRIDGE_EMUL_HANDLED;
	}

	case PCI_EXP_RTSTA: {
		u32 isr0 = advk_readl(pcie, PCIE_ISR0_REG);
		u32 msglog = advk_readl(pcie, PCIE_MSG_LOG_REG);
		*value = (isr0 & PCIE_MSG_PM_PME_MASK) << 16 | (msglog >> 16);
		return PCI_BRIDGE_EMUL_HANDLED;
	}

	case PCI_EXP_LNKCTL: {
		/* u32 contains both PCI_EXP_LNKCTL and PCI_EXP_LNKSTA */
		u32 val = advk_readl(pcie, PCIE_CORE_PCIEXP_CAP + reg) &
			~(PCI_EXP_LNKSTA_LT << 16);
		if (!advk_pcie_link_up(pcie))
			val |= (PCI_EXP_LNKSTA_LT << 16);
		*value = val;
		return PCI_BRIDGE_EMUL_HANDLED;
	}

	case PCI_CAP_LIST_ID:
	case PCI_EXP_DEVCAP:
	case PCI_EXP_DEVCTL:
	case PCI_EXP_LNKCAP:
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
		advk_writel(pcie, new, PCIE_CORE_PCIEXP_CAP + reg);
		break;

	case PCI_EXP_LNKCTL:
		advk_writel(pcie, new, PCIE_CORE_PCIEXP_CAP + reg);
		if (new & PCI_EXP_LNKCTL_RL)
			advk_pcie_wait_for_retrain(pcie);
		break;

	case PCI_EXP_RTCTL: {
		/* Only mask/unmask PME interrupt */
		u32 val = advk_readl(pcie, PCIE_ISR0_MASK_REG) &
			~PCIE_MSG_PM_PME_MASK;
		if ((new & PCI_EXP_RTCTL_PMEIE) == 0)
			val |= PCIE_MSG_PM_PME_MASK;
		advk_writel(pcie, val, PCIE_ISR0_MASK_REG);
		break;
	}

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
static int advk_sw_pci_bridge_init(struct advk_pcie *pcie)
{
	struct pci_bridge_emul *bridge = &pcie->bridge;

	bridge->conf.vendor =
		cpu_to_le16(advk_readl(pcie, PCIE_CORE_DEV_ID_REG) & 0xffff);
	bridge->conf.device =
		cpu_to_le16(advk_readl(pcie, PCIE_CORE_DEV_ID_REG) >> 16);
	bridge->conf.class_revision =
		cpu_to_le32(advk_readl(pcie, PCIE_CORE_DEV_REV_REG) & 0xff);

	/* Support 32 bits I/O addressing */
	bridge->conf.iobase = PCI_IO_RANGE_TYPE_32;
	bridge->conf.iolimit = PCI_IO_RANGE_TYPE_32;

	/* Support 64 bits memory pref */
	bridge->conf.pref_mem_base = cpu_to_le16(PCI_PREF_RANGE_TYPE_64);
	bridge->conf.pref_mem_limit = cpu_to_le16(PCI_PREF_RANGE_TYPE_64);

	/* Support interrupt A for MSI feature */
	bridge->conf.intpin = PCIE_CORE_INT_A_ASSERT_ENABLE;

	bridge->has_pcie = true;
	bridge->data = pcie;
	bridge->ops = &advk_pci_bridge_emul_ops;

	return pci_bridge_emul_init(bridge, 0);
}

static bool advk_pcie_valid_device(struct advk_pcie *pcie, struct pci_bus *bus,
				  int devfn)
{
	if (pci_is_root_bus(bus) && PCI_SLOT(devfn) != 0)
		return false;

	/*
	 * If the link goes down after we check for link-up, nothing bad
	 * happens but the config access times out.
	 */
	if (!pci_is_root_bus(bus) && !advk_pcie_link_up(pcie))
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

	if (pci_is_root_bus(bus))
		return pci_bridge_emul_conf_read(&pcie->bridge, where,
						 size, val);

	/* Start PIO */
	advk_writel(pcie, 0, PIO_START);
	advk_writel(pcie, 1, PIO_ISR);

	/* Program the control register */
	reg = advk_readl(pcie, PIO_CTRL);
	reg &= ~PIO_CTRL_TYPE_MASK;
	if (pci_is_root_bus(bus->parent))
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
	if (ret < 0) {
		*val = 0xffffffff;
		return PCIBIOS_SET_FAILED;
	}

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

	if (pci_is_root_bus(bus))
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
	if (pci_is_root_bus(bus->parent))
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

static void __maybe_unused advk_pcie_disable_phy(struct advk_pcie *pcie)
{
	phy_power_off(pcie->phy);
	phy_exit(pcie->phy);
}

static int advk_pcie_enable_phy(struct advk_pcie *pcie)
{
	int ret;

	if (!pcie->phy)
		return 0;

	ret = phy_init(pcie->phy);
	if (ret)
		return ret;

	ret = phy_set_mode(pcie->phy, PHY_MODE_PCIE);
	if (ret) {
		phy_exit(pcie->phy);
		return ret;
	}

	ret = phy_power_on(pcie->phy);
	if (ret == -EOPNOTSUPP) {
		dev_warn(&pcie->pdev->dev, "PHY unsupported by firmware\n");
	} else if (ret) {
		phy_exit(pcie->phy);
		return ret;
	}

	return 0;
}

static int advk_pcie_setup_phy(struct advk_pcie *pcie)
{
	struct device *dev = &pcie->pdev->dev;
	struct device_node *node = dev->of_node;
	int ret = 0;

	pcie->phy = devm_of_phy_get(dev, node, NULL);
	if (IS_ERR(pcie->phy) && (PTR_ERR(pcie->phy) == -EPROBE_DEFER))
		return PTR_ERR(pcie->phy);

	/* Old bindings miss the PHY handle */
	if (IS_ERR(pcie->phy)) {
		dev_warn(dev, "PHY unavailable (%ld)\n", PTR_ERR(pcie->phy));
		pcie->phy = NULL;
		return 0;
	}

	ret = advk_pcie_enable_phy(pcie);
	if (ret)
		dev_err(dev, "Failed to initialize PHY (%d)\n", ret);

	return ret;
}

static int advk_pcie_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct advk_pcie *pcie;
	struct pci_host_bridge *bridge;
	int ret, irq;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(struct advk_pcie));
	if (!bridge)
		return -ENOMEM;

	pcie = pci_host_bridge_priv(bridge);
	pcie->pdev = pdev;
	platform_set_drvdata(pdev, pcie);

	pcie->base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(pcie->base))
		return PTR_ERR(pcie->base);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_irq(dev, irq, advk_pcie_irq_handler,
			       IRQF_SHARED | IRQF_NO_THREAD, "advk-pcie",
			       pcie);
	if (ret) {
		dev_err(dev, "Failed to register interrupt\n");
		return ret;
	}

	pcie->reset_gpio = devm_gpiod_get_from_of_node(dev, dev->of_node,
						       "reset-gpios", 0,
						       GPIOD_OUT_LOW,
						       "pcie1-reset");
	ret = PTR_ERR_OR_ZERO(pcie->reset_gpio);
	if (ret) {
		if (ret == -ENOENT) {
			pcie->reset_gpio = NULL;
		} else {
			if (ret != -EPROBE_DEFER)
				dev_err(dev, "Failed to get reset-gpio: %i\n",
					ret);
			return ret;
		}
	}

	ret = of_pci_get_max_link_speed(dev->of_node);
	if (ret <= 0 || ret > 3)
		pcie->link_gen = 3;
	else
		pcie->link_gen = ret;

	ret = advk_pcie_setup_phy(pcie);
	if (ret)
		return ret;

	advk_pcie_setup_hw(pcie);

	ret = advk_sw_pci_bridge_init(pcie);
	if (ret) {
		dev_err(dev, "Failed to register emulated root PCI bridge\n");
		return ret;
	}

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

	bridge->sysdata = pcie;
	bridge->ops = &advk_pcie_ops;

	ret = pci_host_probe(bridge);
	if (ret < 0) {
		advk_pcie_remove_msi_irq_domain(pcie);
		advk_pcie_remove_irq_domain(pcie);
		return ret;
	}

	return 0;
}

static int advk_pcie_remove(struct platform_device *pdev)
{
	struct advk_pcie *pcie = platform_get_drvdata(pdev);
	struct pci_host_bridge *bridge = pci_host_bridge_from_priv(pcie);

	pci_lock_rescan_remove();
	pci_stop_root_bus(bridge->bus);
	pci_remove_root_bus(bridge->bus);
	pci_unlock_rescan_remove();

	advk_pcie_remove_msi_irq_domain(pcie);
	advk_pcie_remove_irq_domain(pcie);

	return 0;
}

static const struct of_device_id advk_pcie_of_match_table[] = {
	{ .compatible = "marvell,armada-3700-pcie", },
	{},
};
MODULE_DEVICE_TABLE(of, advk_pcie_of_match_table);

static struct platform_driver advk_pcie_driver = {
	.driver = {
		.name = "advk-pcie",
		.of_match_table = advk_pcie_of_match_table,
	},
	.probe = advk_pcie_probe,
	.remove = advk_pcie_remove,
};
module_platform_driver(advk_pcie_driver);

MODULE_DESCRIPTION("Aardvark PCIe controller");
MODULE_LICENSE("GPL v2");
