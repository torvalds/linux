// SPDX-License-Identifier: GPL-2.0
/*
 * Microchip AXI PCIe Bridge host controller driver
 *
 * Copyright (c) 2018 - 2020 Microchip Corporation. All rights reserved.
 *
 * Author: Daire McNamara <daire.mcnamara@microchip.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/irqchip/chained_irq.h>
#include <linux/irqdomain.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of_address.h>
#include <linux/of_pci.h>
#include <linux/pci-ecam.h>
#include <linux/platform_device.h>

#include "../../pci.h"
#include "pcie-plda.h"

/* PCIe Bridge Phy and Controller Phy offsets */
#define MC_PCIE1_BRIDGE_ADDR			0x00008000u
#define MC_PCIE1_CTRL_ADDR			0x0000a000u

#define MC_PCIE_BRIDGE_ADDR			(MC_PCIE1_BRIDGE_ADDR)
#define MC_PCIE_CTRL_ADDR			(MC_PCIE1_CTRL_ADDR)

/* PCIe Controller Phy Regs */
#define SEC_ERROR_EVENT_CNT			0x20
#define DED_ERROR_EVENT_CNT			0x24
#define SEC_ERROR_INT				0x28
#define  SEC_ERROR_INT_TX_RAM_SEC_ERR_INT	GENMASK(3, 0)
#define  SEC_ERROR_INT_RX_RAM_SEC_ERR_INT	GENMASK(7, 4)
#define  SEC_ERROR_INT_PCIE2AXI_RAM_SEC_ERR_INT	GENMASK(11, 8)
#define  SEC_ERROR_INT_AXI2PCIE_RAM_SEC_ERR_INT	GENMASK(15, 12)
#define  SEC_ERROR_INT_ALL_RAM_SEC_ERR_INT	GENMASK(15, 0)
#define  NUM_SEC_ERROR_INTS			(4)
#define SEC_ERROR_INT_MASK			0x2c
#define DED_ERROR_INT				0x30
#define  DED_ERROR_INT_TX_RAM_DED_ERR_INT	GENMASK(3, 0)
#define  DED_ERROR_INT_RX_RAM_DED_ERR_INT	GENMASK(7, 4)
#define  DED_ERROR_INT_PCIE2AXI_RAM_DED_ERR_INT	GENMASK(11, 8)
#define  DED_ERROR_INT_AXI2PCIE_RAM_DED_ERR_INT	GENMASK(15, 12)
#define  DED_ERROR_INT_ALL_RAM_DED_ERR_INT	GENMASK(15, 0)
#define  NUM_DED_ERROR_INTS			(4)
#define DED_ERROR_INT_MASK			0x34
#define ECC_CONTROL				0x38
#define  ECC_CONTROL_TX_RAM_INJ_ERROR_0		BIT(0)
#define  ECC_CONTROL_TX_RAM_INJ_ERROR_1		BIT(1)
#define  ECC_CONTROL_TX_RAM_INJ_ERROR_2		BIT(2)
#define  ECC_CONTROL_TX_RAM_INJ_ERROR_3		BIT(3)
#define  ECC_CONTROL_RX_RAM_INJ_ERROR_0		BIT(4)
#define  ECC_CONTROL_RX_RAM_INJ_ERROR_1		BIT(5)
#define  ECC_CONTROL_RX_RAM_INJ_ERROR_2		BIT(6)
#define  ECC_CONTROL_RX_RAM_INJ_ERROR_3		BIT(7)
#define  ECC_CONTROL_PCIE2AXI_RAM_INJ_ERROR_0	BIT(8)
#define  ECC_CONTROL_PCIE2AXI_RAM_INJ_ERROR_1	BIT(9)
#define  ECC_CONTROL_PCIE2AXI_RAM_INJ_ERROR_2	BIT(10)
#define  ECC_CONTROL_PCIE2AXI_RAM_INJ_ERROR_3	BIT(11)
#define  ECC_CONTROL_AXI2PCIE_RAM_INJ_ERROR_0	BIT(12)
#define  ECC_CONTROL_AXI2PCIE_RAM_INJ_ERROR_1	BIT(13)
#define  ECC_CONTROL_AXI2PCIE_RAM_INJ_ERROR_2	BIT(14)
#define  ECC_CONTROL_AXI2PCIE_RAM_INJ_ERROR_3	BIT(15)
#define  ECC_CONTROL_TX_RAM_ECC_BYPASS		BIT(24)
#define  ECC_CONTROL_RX_RAM_ECC_BYPASS		BIT(25)
#define  ECC_CONTROL_PCIE2AXI_RAM_ECC_BYPASS	BIT(26)
#define  ECC_CONTROL_AXI2PCIE_RAM_ECC_BYPASS	BIT(27)
#define PCIE_EVENT_INT				0x14c
#define  PCIE_EVENT_INT_L2_EXIT_INT		BIT(0)
#define  PCIE_EVENT_INT_HOTRST_EXIT_INT		BIT(1)
#define  PCIE_EVENT_INT_DLUP_EXIT_INT		BIT(2)
#define  PCIE_EVENT_INT_MASK			GENMASK(2, 0)
#define  PCIE_EVENT_INT_L2_EXIT_INT_MASK	BIT(16)
#define  PCIE_EVENT_INT_HOTRST_EXIT_INT_MASK	BIT(17)
#define  PCIE_EVENT_INT_DLUP_EXIT_INT_MASK	BIT(18)
#define  PCIE_EVENT_INT_ENB_MASK		GENMASK(18, 16)
#define  PCIE_EVENT_INT_ENB_SHIFT		16
#define  NUM_PCIE_EVENTS			(3)

/* PCIe Config space MSI capability structure */
#define MC_MSI_CAP_CTRL_OFFSET			0xe0u

/* Events */
#define EVENT_PCIE_L2_EXIT			0
#define EVENT_PCIE_HOTRST_EXIT			1
#define EVENT_PCIE_DLUP_EXIT			2
#define EVENT_SEC_TX_RAM_SEC_ERR		3
#define EVENT_SEC_RX_RAM_SEC_ERR		4
#define EVENT_SEC_PCIE2AXI_RAM_SEC_ERR		5
#define EVENT_SEC_AXI2PCIE_RAM_SEC_ERR		6
#define EVENT_DED_TX_RAM_DED_ERR		7
#define EVENT_DED_RX_RAM_DED_ERR		8
#define EVENT_DED_PCIE2AXI_RAM_DED_ERR		9
#define EVENT_DED_AXI2PCIE_RAM_DED_ERR		10
#define EVENT_LOCAL_DMA_END_ENGINE_0		11
#define EVENT_LOCAL_DMA_END_ENGINE_1		12
#define EVENT_LOCAL_DMA_ERROR_ENGINE_0		13
#define EVENT_LOCAL_DMA_ERROR_ENGINE_1		14
#define NUM_MC_EVENTS				15
#define EVENT_LOCAL_A_ATR_EVT_POST_ERR		(NUM_MC_EVENTS + PLDA_AXI_POST_ERR)
#define EVENT_LOCAL_A_ATR_EVT_FETCH_ERR		(NUM_MC_EVENTS + PLDA_AXI_FETCH_ERR)
#define EVENT_LOCAL_A_ATR_EVT_DISCARD_ERR	(NUM_MC_EVENTS + PLDA_AXI_DISCARD_ERR)
#define EVENT_LOCAL_A_ATR_EVT_DOORBELL		(NUM_MC_EVENTS + PLDA_AXI_DOORBELL)
#define EVENT_LOCAL_P_ATR_EVT_POST_ERR		(NUM_MC_EVENTS + PLDA_PCIE_POST_ERR)
#define EVENT_LOCAL_P_ATR_EVT_FETCH_ERR		(NUM_MC_EVENTS + PLDA_PCIE_FETCH_ERR)
#define EVENT_LOCAL_P_ATR_EVT_DISCARD_ERR	(NUM_MC_EVENTS + PLDA_PCIE_DISCARD_ERR)
#define EVENT_LOCAL_P_ATR_EVT_DOORBELL		(NUM_MC_EVENTS + PLDA_PCIE_DOORBELL)
#define EVENT_LOCAL_PM_MSI_INT_INTX		(NUM_MC_EVENTS + PLDA_INTX)
#define EVENT_LOCAL_PM_MSI_INT_MSI		(NUM_MC_EVENTS + PLDA_MSI)
#define EVENT_LOCAL_PM_MSI_INT_AER_EVT		(NUM_MC_EVENTS + PLDA_AER_EVENT)
#define EVENT_LOCAL_PM_MSI_INT_EVENTS		(NUM_MC_EVENTS + PLDA_MISC_EVENTS)
#define EVENT_LOCAL_PM_MSI_INT_SYS_ERR		(NUM_MC_EVENTS + PLDA_SYS_ERR)
#define NUM_EVENTS				(NUM_MC_EVENTS + PLDA_INT_EVENT_NUM)

#define PCIE_EVENT_CAUSE(x, s)	\
	[EVENT_PCIE_ ## x] = { __stringify(x), s }

#define SEC_ERROR_CAUSE(x, s) \
	[EVENT_SEC_ ## x] = { __stringify(x), s }

#define DED_ERROR_CAUSE(x, s) \
	[EVENT_DED_ ## x] = { __stringify(x), s }

#define LOCAL_EVENT_CAUSE(x, s) \
	[EVENT_LOCAL_ ## x] = { __stringify(x), s }

#define PCIE_EVENT(x) \
	.base = MC_PCIE_CTRL_ADDR, \
	.offset = PCIE_EVENT_INT, \
	.mask_offset = PCIE_EVENT_INT, \
	.mask_high = 1, \
	.mask = PCIE_EVENT_INT_ ## x ## _INT, \
	.enb_mask = PCIE_EVENT_INT_ENB_MASK

#define SEC_EVENT(x) \
	.base = MC_PCIE_CTRL_ADDR, \
	.offset = SEC_ERROR_INT, \
	.mask_offset = SEC_ERROR_INT_MASK, \
	.mask = SEC_ERROR_INT_ ## x ## _INT, \
	.mask_high = 1, \
	.enb_mask = 0

#define DED_EVENT(x) \
	.base = MC_PCIE_CTRL_ADDR, \
	.offset = DED_ERROR_INT, \
	.mask_offset = DED_ERROR_INT_MASK, \
	.mask_high = 1, \
	.mask = DED_ERROR_INT_ ## x ## _INT, \
	.enb_mask = 0

#define LOCAL_EVENT(x) \
	.base = MC_PCIE_BRIDGE_ADDR, \
	.offset = ISTATUS_LOCAL, \
	.mask_offset = IMASK_LOCAL, \
	.mask_high = 0, \
	.mask = x ## _MASK, \
	.enb_mask = 0

#define PCIE_EVENT_TO_EVENT_MAP(x) \
	{ PCIE_EVENT_INT_ ## x ## _INT, EVENT_PCIE_ ## x }

#define SEC_ERROR_TO_EVENT_MAP(x) \
	{ SEC_ERROR_INT_ ## x ## _INT, EVENT_SEC_ ## x }

#define DED_ERROR_TO_EVENT_MAP(x) \
	{ DED_ERROR_INT_ ## x ## _INT, EVENT_DED_ ## x }

#define LOCAL_STATUS_TO_EVENT_MAP(x) \
	{ x ## _MASK, EVENT_LOCAL_ ## x }

struct event_map {
	u32 reg_mask;
	u32 event_bit;
};


struct mc_pcie {
	struct plda_pcie_rp plda;
	void __iomem *axi_base_addr;
};

struct cause {
	const char *sym;
	const char *str;
};

static const struct cause event_cause[NUM_EVENTS] = {
	PCIE_EVENT_CAUSE(L2_EXIT, "L2 exit event"),
	PCIE_EVENT_CAUSE(HOTRST_EXIT, "Hot reset exit event"),
	PCIE_EVENT_CAUSE(DLUP_EXIT, "DLUP exit event"),
	SEC_ERROR_CAUSE(TX_RAM_SEC_ERR,  "sec error in tx buffer"),
	SEC_ERROR_CAUSE(RX_RAM_SEC_ERR,  "sec error in rx buffer"),
	SEC_ERROR_CAUSE(PCIE2AXI_RAM_SEC_ERR,  "sec error in pcie2axi buffer"),
	SEC_ERROR_CAUSE(AXI2PCIE_RAM_SEC_ERR,  "sec error in axi2pcie buffer"),
	DED_ERROR_CAUSE(TX_RAM_DED_ERR,  "ded error in tx buffer"),
	DED_ERROR_CAUSE(RX_RAM_DED_ERR,  "ded error in rx buffer"),
	DED_ERROR_CAUSE(PCIE2AXI_RAM_DED_ERR,  "ded error in pcie2axi buffer"),
	DED_ERROR_CAUSE(AXI2PCIE_RAM_DED_ERR,  "ded error in axi2pcie buffer"),
	LOCAL_EVENT_CAUSE(DMA_ERROR_ENGINE_0, "dma engine 0 error"),
	LOCAL_EVENT_CAUSE(DMA_ERROR_ENGINE_1, "dma engine 1 error"),
	LOCAL_EVENT_CAUSE(A_ATR_EVT_POST_ERR, "axi write request error"),
	LOCAL_EVENT_CAUSE(A_ATR_EVT_FETCH_ERR, "axi read request error"),
	LOCAL_EVENT_CAUSE(A_ATR_EVT_DISCARD_ERR, "axi read timeout"),
	LOCAL_EVENT_CAUSE(P_ATR_EVT_POST_ERR, "pcie write request error"),
	LOCAL_EVENT_CAUSE(P_ATR_EVT_FETCH_ERR, "pcie read request error"),
	LOCAL_EVENT_CAUSE(P_ATR_EVT_DISCARD_ERR, "pcie read timeout"),
	LOCAL_EVENT_CAUSE(PM_MSI_INT_AER_EVT, "aer event"),
	LOCAL_EVENT_CAUSE(PM_MSI_INT_EVENTS, "pm/ltr/hotplug event"),
	LOCAL_EVENT_CAUSE(PM_MSI_INT_SYS_ERR, "system error"),
};

static struct event_map pcie_event_to_event[] = {
	PCIE_EVENT_TO_EVENT_MAP(L2_EXIT),
	PCIE_EVENT_TO_EVENT_MAP(HOTRST_EXIT),
	PCIE_EVENT_TO_EVENT_MAP(DLUP_EXIT),
};

static struct event_map sec_error_to_event[] = {
	SEC_ERROR_TO_EVENT_MAP(TX_RAM_SEC_ERR),
	SEC_ERROR_TO_EVENT_MAP(RX_RAM_SEC_ERR),
	SEC_ERROR_TO_EVENT_MAP(PCIE2AXI_RAM_SEC_ERR),
	SEC_ERROR_TO_EVENT_MAP(AXI2PCIE_RAM_SEC_ERR),
};

static struct event_map ded_error_to_event[] = {
	DED_ERROR_TO_EVENT_MAP(TX_RAM_DED_ERR),
	DED_ERROR_TO_EVENT_MAP(RX_RAM_DED_ERR),
	DED_ERROR_TO_EVENT_MAP(PCIE2AXI_RAM_DED_ERR),
	DED_ERROR_TO_EVENT_MAP(AXI2PCIE_RAM_DED_ERR),
};

static struct event_map local_status_to_event[] = {
	LOCAL_STATUS_TO_EVENT_MAP(DMA_END_ENGINE_0),
	LOCAL_STATUS_TO_EVENT_MAP(DMA_END_ENGINE_1),
	LOCAL_STATUS_TO_EVENT_MAP(DMA_ERROR_ENGINE_0),
	LOCAL_STATUS_TO_EVENT_MAP(DMA_ERROR_ENGINE_1),
	LOCAL_STATUS_TO_EVENT_MAP(A_ATR_EVT_POST_ERR),
	LOCAL_STATUS_TO_EVENT_MAP(A_ATR_EVT_FETCH_ERR),
	LOCAL_STATUS_TO_EVENT_MAP(A_ATR_EVT_DISCARD_ERR),
	LOCAL_STATUS_TO_EVENT_MAP(A_ATR_EVT_DOORBELL),
	LOCAL_STATUS_TO_EVENT_MAP(P_ATR_EVT_POST_ERR),
	LOCAL_STATUS_TO_EVENT_MAP(P_ATR_EVT_FETCH_ERR),
	LOCAL_STATUS_TO_EVENT_MAP(P_ATR_EVT_DISCARD_ERR),
	LOCAL_STATUS_TO_EVENT_MAP(P_ATR_EVT_DOORBELL),
	LOCAL_STATUS_TO_EVENT_MAP(PM_MSI_INT_INTX),
	LOCAL_STATUS_TO_EVENT_MAP(PM_MSI_INT_MSI),
	LOCAL_STATUS_TO_EVENT_MAP(PM_MSI_INT_AER_EVT),
	LOCAL_STATUS_TO_EVENT_MAP(PM_MSI_INT_EVENTS),
	LOCAL_STATUS_TO_EVENT_MAP(PM_MSI_INT_SYS_ERR),
};

static struct {
	u32 base;
	u32 offset;
	u32 mask;
	u32 shift;
	u32 enb_mask;
	u32 mask_high;
	u32 mask_offset;
} event_descs[] = {
	{ PCIE_EVENT(L2_EXIT) },
	{ PCIE_EVENT(HOTRST_EXIT) },
	{ PCIE_EVENT(DLUP_EXIT) },
	{ SEC_EVENT(TX_RAM_SEC_ERR) },
	{ SEC_EVENT(RX_RAM_SEC_ERR) },
	{ SEC_EVENT(PCIE2AXI_RAM_SEC_ERR) },
	{ SEC_EVENT(AXI2PCIE_RAM_SEC_ERR) },
	{ DED_EVENT(TX_RAM_DED_ERR) },
	{ DED_EVENT(RX_RAM_DED_ERR) },
	{ DED_EVENT(PCIE2AXI_RAM_DED_ERR) },
	{ DED_EVENT(AXI2PCIE_RAM_DED_ERR) },
	{ LOCAL_EVENT(DMA_END_ENGINE_0) },
	{ LOCAL_EVENT(DMA_END_ENGINE_1) },
	{ LOCAL_EVENT(DMA_ERROR_ENGINE_0) },
	{ LOCAL_EVENT(DMA_ERROR_ENGINE_1) },
	{ LOCAL_EVENT(A_ATR_EVT_POST_ERR) },
	{ LOCAL_EVENT(A_ATR_EVT_FETCH_ERR) },
	{ LOCAL_EVENT(A_ATR_EVT_DISCARD_ERR) },
	{ LOCAL_EVENT(A_ATR_EVT_DOORBELL) },
	{ LOCAL_EVENT(P_ATR_EVT_POST_ERR) },
	{ LOCAL_EVENT(P_ATR_EVT_FETCH_ERR) },
	{ LOCAL_EVENT(P_ATR_EVT_DISCARD_ERR) },
	{ LOCAL_EVENT(P_ATR_EVT_DOORBELL) },
	{ LOCAL_EVENT(PM_MSI_INT_INTX) },
	{ LOCAL_EVENT(PM_MSI_INT_MSI) },
	{ LOCAL_EVENT(PM_MSI_INT_AER_EVT) },
	{ LOCAL_EVENT(PM_MSI_INT_EVENTS) },
	{ LOCAL_EVENT(PM_MSI_INT_SYS_ERR) },
};

static char poss_clks[][5] = { "fic0", "fic1", "fic2", "fic3" };

static struct mc_pcie *port;

static void mc_pcie_enable_msi(struct mc_pcie *port, void __iomem *ecam)
{
	struct plda_msi *msi = &port->plda.msi;
	u16 reg;
	u8 queue_size;

	/* Fixup MSI enable flag */
	reg = readw_relaxed(ecam + MC_MSI_CAP_CTRL_OFFSET + PCI_MSI_FLAGS);
	reg |= PCI_MSI_FLAGS_ENABLE;
	writew_relaxed(reg, ecam + MC_MSI_CAP_CTRL_OFFSET + PCI_MSI_FLAGS);

	/* Fixup PCI MSI queue flags */
	queue_size = FIELD_GET(PCI_MSI_FLAGS_QMASK, reg);
	reg |= FIELD_PREP(PCI_MSI_FLAGS_QSIZE, queue_size);
	writew_relaxed(reg, ecam + MC_MSI_CAP_CTRL_OFFSET + PCI_MSI_FLAGS);

	/* Fixup MSI addr fields */
	writel_relaxed(lower_32_bits(msi->vector_phy),
		       ecam + MC_MSI_CAP_CTRL_OFFSET + PCI_MSI_ADDRESS_LO);
	writel_relaxed(upper_32_bits(msi->vector_phy),
		       ecam + MC_MSI_CAP_CTRL_OFFSET + PCI_MSI_ADDRESS_HI);
}

static inline u32 reg_to_event(u32 reg, struct event_map field)
{
	return (reg & field.reg_mask) ? BIT(field.event_bit) : 0;
}

static u32 pcie_events(struct mc_pcie *port)
{
	void __iomem *ctrl_base_addr = port->axi_base_addr + MC_PCIE_CTRL_ADDR;
	u32 reg = readl_relaxed(ctrl_base_addr + PCIE_EVENT_INT);
	u32 val = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(pcie_event_to_event); i++)
		val |= reg_to_event(reg, pcie_event_to_event[i]);

	return val;
}

static u32 sec_errors(struct mc_pcie *port)
{
	void __iomem *ctrl_base_addr = port->axi_base_addr + MC_PCIE_CTRL_ADDR;
	u32 reg = readl_relaxed(ctrl_base_addr + SEC_ERROR_INT);
	u32 val = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(sec_error_to_event); i++)
		val |= reg_to_event(reg, sec_error_to_event[i]);

	return val;
}

static u32 ded_errors(struct mc_pcie *port)
{
	void __iomem *ctrl_base_addr = port->axi_base_addr + MC_PCIE_CTRL_ADDR;
	u32 reg = readl_relaxed(ctrl_base_addr + DED_ERROR_INT);
	u32 val = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(ded_error_to_event); i++)
		val |= reg_to_event(reg, ded_error_to_event[i]);

	return val;
}

static u32 local_events(struct mc_pcie *port)
{
	void __iomem *bridge_base_addr = port->axi_base_addr + MC_PCIE_BRIDGE_ADDR;
	u32 reg = readl_relaxed(bridge_base_addr + ISTATUS_LOCAL);
	u32 val = 0;
	int i;

	for (i = 0; i < ARRAY_SIZE(local_status_to_event); i++)
		val |= reg_to_event(reg, local_status_to_event[i]);

	return val;
}

static u32 mc_get_events(struct plda_pcie_rp *port)
{
	struct mc_pcie *mc_port = container_of(port, struct mc_pcie, plda);
	u32 events = 0;

	events |= pcie_events(mc_port);
	events |= sec_errors(mc_port);
	events |= ded_errors(mc_port);
	events |= local_events(mc_port);

	return events;
}

static irqreturn_t mc_event_handler(int irq, void *dev_id)
{
	struct plda_pcie_rp *port = dev_id;
	struct device *dev = port->dev;
	struct irq_data *data;

	data = irq_domain_get_irq_data(port->event_domain, irq);

	if (event_cause[data->hwirq].str)
		dev_err_ratelimited(dev, "%s\n", event_cause[data->hwirq].str);
	else
		dev_err_ratelimited(dev, "bad event IRQ %ld\n", data->hwirq);

	return IRQ_HANDLED;
}

static void mc_ack_event_irq(struct irq_data *data)
{
	struct plda_pcie_rp *port = irq_data_get_irq_chip_data(data);
	struct mc_pcie *mc_port = container_of(port, struct mc_pcie, plda);
	u32 event = data->hwirq;
	void __iomem *addr;
	u32 mask;

	addr = mc_port->axi_base_addr + event_descs[event].base +
		event_descs[event].offset;
	mask = event_descs[event].mask;
	mask |= event_descs[event].enb_mask;

	writel_relaxed(mask, addr);
}

static void mc_mask_event_irq(struct irq_data *data)
{
	struct plda_pcie_rp *port = irq_data_get_irq_chip_data(data);
	struct mc_pcie *mc_port = container_of(port, struct mc_pcie, plda);
	u32 event = data->hwirq;
	void __iomem *addr;
	u32 mask;
	u32 val;

	addr = mc_port->axi_base_addr + event_descs[event].base +
		event_descs[event].mask_offset;
	mask = event_descs[event].mask;
	if (event_descs[event].enb_mask) {
		mask <<= PCIE_EVENT_INT_ENB_SHIFT;
		mask &= PCIE_EVENT_INT_ENB_MASK;
	}

	if (!event_descs[event].mask_high)
		mask = ~mask;

	raw_spin_lock(&port->lock);
	val = readl_relaxed(addr);
	if (event_descs[event].mask_high)
		val |= mask;
	else
		val &= mask;

	writel_relaxed(val, addr);
	raw_spin_unlock(&port->lock);
}

static void mc_unmask_event_irq(struct irq_data *data)
{
	struct plda_pcie_rp *port = irq_data_get_irq_chip_data(data);
	struct mc_pcie *mc_port = container_of(port, struct mc_pcie, plda);
	u32 event = data->hwirq;
	void __iomem *addr;
	u32 mask;
	u32 val;

	addr = mc_port->axi_base_addr + event_descs[event].base +
		event_descs[event].mask_offset;
	mask = event_descs[event].mask;

	if (event_descs[event].enb_mask)
		mask <<= PCIE_EVENT_INT_ENB_SHIFT;

	if (event_descs[event].mask_high)
		mask = ~mask;

	if (event_descs[event].enb_mask)
		mask &= PCIE_EVENT_INT_ENB_MASK;

	raw_spin_lock(&port->lock);
	val = readl_relaxed(addr);
	if (event_descs[event].mask_high)
		val &= mask;
	else
		val |= mask;
	writel_relaxed(val, addr);
	raw_spin_unlock(&port->lock);
}

static struct irq_chip mc_event_irq_chip = {
	.name = "Microchip PCIe EVENT",
	.irq_ack = mc_ack_event_irq,
	.irq_mask = mc_mask_event_irq,
	.irq_unmask = mc_unmask_event_irq,
};

static inline void mc_pcie_deinit_clk(void *data)
{
	struct clk *clk = data;

	clk_disable_unprepare(clk);
}

static inline struct clk *mc_pcie_init_clk(struct device *dev, const char *id)
{
	struct clk *clk;
	int ret;

	clk = devm_clk_get_optional(dev, id);
	if (IS_ERR(clk))
		return clk;
	if (!clk)
		return clk;

	ret = clk_prepare_enable(clk);
	if (ret)
		return ERR_PTR(ret);

	devm_add_action_or_reset(dev, mc_pcie_deinit_clk, clk);

	return clk;
}

static int mc_pcie_init_clks(struct device *dev)
{
	int i;
	struct clk *fic;

	/*
	 * PCIe may be clocked via Fabric Interface using between 1 and 4
	 * clocks. Scan DT for clocks and enable them if present
	 */
	for (i = 0; i < ARRAY_SIZE(poss_clks); i++) {
		fic = mc_pcie_init_clk(dev, poss_clks[i]);
		if (IS_ERR(fic))
			return PTR_ERR(fic);
	}

	return 0;
}

static int mc_request_event_irq(struct plda_pcie_rp *plda, int event_irq,
				int event)
{
	return devm_request_irq(plda->dev, event_irq, mc_event_handler,
				0, event_cause[event].sym, plda);
}

static const struct plda_event_ops mc_event_ops = {
	.get_events = mc_get_events,
};

static const struct plda_event mc_event = {
	.request_event_irq = mc_request_event_irq,
	.intx_event        = EVENT_LOCAL_PM_MSI_INT_INTX,
	.msi_event         = EVENT_LOCAL_PM_MSI_INT_MSI,
};

static inline void mc_clear_secs(struct mc_pcie *port)
{
	void __iomem *ctrl_base_addr = port->axi_base_addr + MC_PCIE_CTRL_ADDR;

	writel_relaxed(SEC_ERROR_INT_ALL_RAM_SEC_ERR_INT, ctrl_base_addr +
		       SEC_ERROR_INT);
	writel_relaxed(0, ctrl_base_addr + SEC_ERROR_EVENT_CNT);
}

static inline void mc_clear_deds(struct mc_pcie *port)
{
	void __iomem *ctrl_base_addr = port->axi_base_addr + MC_PCIE_CTRL_ADDR;

	writel_relaxed(DED_ERROR_INT_ALL_RAM_DED_ERR_INT, ctrl_base_addr +
		       DED_ERROR_INT);
	writel_relaxed(0, ctrl_base_addr + DED_ERROR_EVENT_CNT);
}

static void mc_disable_interrupts(struct mc_pcie *port)
{
	void __iomem *bridge_base_addr = port->axi_base_addr + MC_PCIE_BRIDGE_ADDR;
	void __iomem *ctrl_base_addr = port->axi_base_addr + MC_PCIE_CTRL_ADDR;
	u32 val;

	/* Ensure ECC bypass is enabled */
	val = ECC_CONTROL_TX_RAM_ECC_BYPASS |
	      ECC_CONTROL_RX_RAM_ECC_BYPASS |
	      ECC_CONTROL_PCIE2AXI_RAM_ECC_BYPASS |
	      ECC_CONTROL_AXI2PCIE_RAM_ECC_BYPASS;
	writel_relaxed(val, ctrl_base_addr + ECC_CONTROL);

	/* Disable SEC errors and clear any outstanding */
	writel_relaxed(SEC_ERROR_INT_ALL_RAM_SEC_ERR_INT, ctrl_base_addr +
		       SEC_ERROR_INT_MASK);
	mc_clear_secs(port);

	/* Disable DED errors and clear any outstanding */
	writel_relaxed(DED_ERROR_INT_ALL_RAM_DED_ERR_INT, ctrl_base_addr +
		       DED_ERROR_INT_MASK);
	mc_clear_deds(port);

	/* Disable local interrupts and clear any outstanding */
	writel_relaxed(0, bridge_base_addr + IMASK_LOCAL);
	writel_relaxed(GENMASK(31, 0), bridge_base_addr + ISTATUS_LOCAL);
	writel_relaxed(GENMASK(31, 0), bridge_base_addr + ISTATUS_MSI);

	/* Disable PCIe events and clear any outstanding */
	val = PCIE_EVENT_INT_L2_EXIT_INT |
	      PCIE_EVENT_INT_HOTRST_EXIT_INT |
	      PCIE_EVENT_INT_DLUP_EXIT_INT |
	      PCIE_EVENT_INT_L2_EXIT_INT_MASK |
	      PCIE_EVENT_INT_HOTRST_EXIT_INT_MASK |
	      PCIE_EVENT_INT_DLUP_EXIT_INT_MASK;
	writel_relaxed(val, ctrl_base_addr + PCIE_EVENT_INT);

	/* Disable host interrupts and clear any outstanding */
	writel_relaxed(0, bridge_base_addr + IMASK_HOST);
	writel_relaxed(GENMASK(31, 0), bridge_base_addr + ISTATUS_HOST);
}

static int mc_platform_init(struct pci_config_window *cfg)
{
	struct device *dev = cfg->parent;
	struct platform_device *pdev = to_platform_device(dev);
	struct pci_host_bridge *bridge = platform_get_drvdata(pdev);
	void __iomem *bridge_base_addr =
		port->axi_base_addr + MC_PCIE_BRIDGE_ADDR;
	int ret;

	/* Configure address translation table 0 for PCIe config space */
	plda_pcie_setup_window(bridge_base_addr, 0, cfg->res.start,
			       cfg->res.start,
			       resource_size(&cfg->res));

	/* Need some fixups in config space */
	mc_pcie_enable_msi(port, cfg->win);

	/* Configure non-config space outbound ranges */
	ret = plda_pcie_setup_iomems(bridge, &port->plda);
	if (ret)
		return ret;

	port->plda.event_ops = &mc_event_ops;
	port->plda.event_irq_chip = &mc_event_irq_chip;
	port->plda.events_bitmap = GENMASK(NUM_EVENTS - 1, 0);

	/* Address translation is up; safe to enable interrupts */
	ret = plda_init_interrupts(pdev, &port->plda, &mc_event);
	if (ret)
		return ret;

	return 0;
}

static int mc_host_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	void __iomem *bridge_base_addr;
	struct plda_pcie_rp *plda;
	int ret;
	u32 val;

	port = devm_kzalloc(dev, sizeof(*port), GFP_KERNEL);
	if (!port)
		return -ENOMEM;

	plda = &port->plda;
	plda->dev = dev;

	port->axi_base_addr = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(port->axi_base_addr))
		return PTR_ERR(port->axi_base_addr);

	mc_disable_interrupts(port);

	bridge_base_addr = port->axi_base_addr + MC_PCIE_BRIDGE_ADDR;
	plda->bridge_addr = bridge_base_addr;
	plda->num_events = NUM_EVENTS;

	/* Allow enabling MSI by disabling MSI-X */
	val = readl(bridge_base_addr + PCIE_PCI_IRQ_DW0);
	val &= ~MSIX_CAP_MASK;
	writel(val, bridge_base_addr + PCIE_PCI_IRQ_DW0);

	/* Pick num vectors from bitfile programmed onto FPGA fabric */
	val = readl(bridge_base_addr + PCIE_PCI_IRQ_DW0);
	val &= NUM_MSI_MSGS_MASK;
	val >>= NUM_MSI_MSGS_SHIFT;

	plda->msi.num_vectors = 1 << val;

	/* Pick vector address from design */
	plda->msi.vector_phy = readl_relaxed(bridge_base_addr + IMSI_ADDR);

	ret = mc_pcie_init_clks(dev);
	if (ret) {
		dev_err(dev, "failed to get clock resources, error %d\n", ret);
		return -ENODEV;
	}

	return pci_host_common_probe(pdev);
}

static const struct pci_ecam_ops mc_ecam_ops = {
	.init = mc_platform_init,
	.pci_ops = {
		.map_bus = pci_ecam_map_bus,
		.read = pci_generic_config_read,
		.write = pci_generic_config_write,
	}
};

static const struct of_device_id mc_pcie_of_match[] = {
	{
		.compatible = "microchip,pcie-host-1.0",
		.data = &mc_ecam_ops,
	},
	{},
};

MODULE_DEVICE_TABLE(of, mc_pcie_of_match);

static struct platform_driver mc_pcie_driver = {
	.probe = mc_host_probe,
	.driver = {
		.name = "microchip-pcie",
		.of_match_table = mc_pcie_of_match,
		.suppress_bind_attrs = true,
	},
};

builtin_platform_driver(mc_pcie_driver);
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Microchip PCIe host controller driver");
MODULE_AUTHOR("Daire McNamara <daire.mcnamara@microchip.com>");
