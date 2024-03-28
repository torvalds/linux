/* SPDX-License-Identifier: GPL-2.0 */
/*
 * PLDA PCIe host controller driver
 */

#ifndef _PCIE_PLDA_H
#define _PCIE_PLDA_H

/* Number of MSI IRQs */
#define PLDA_MAX_NUM_MSI_IRQS			32

/* PCIe Bridge Phy Regs */
#define PCIE_PCI_IRQ_DW0			0xa8
#define  MSIX_CAP_MASK				BIT(31)
#define  NUM_MSI_MSGS_MASK			GENMASK(6, 4)
#define  NUM_MSI_MSGS_SHIFT			4

#define IMASK_LOCAL				0x180
#define  DMA_END_ENGINE_0_MASK			0x00000000u
#define  DMA_END_ENGINE_0_SHIFT			0
#define  DMA_END_ENGINE_1_MASK			0x00000000u
#define  DMA_END_ENGINE_1_SHIFT			1
#define  DMA_ERROR_ENGINE_0_MASK		0x00000100u
#define  DMA_ERROR_ENGINE_0_SHIFT		8
#define  DMA_ERROR_ENGINE_1_MASK		0x00000200u
#define  DMA_ERROR_ENGINE_1_SHIFT		9
#define  A_ATR_EVT_POST_ERR_MASK		0x00010000u
#define  A_ATR_EVT_POST_ERR_SHIFT		16
#define  A_ATR_EVT_FETCH_ERR_MASK		0x00020000u
#define  A_ATR_EVT_FETCH_ERR_SHIFT		17
#define  A_ATR_EVT_DISCARD_ERR_MASK		0x00040000u
#define  A_ATR_EVT_DISCARD_ERR_SHIFT		18
#define  A_ATR_EVT_DOORBELL_MASK		0x00000000u
#define  A_ATR_EVT_DOORBELL_SHIFT		19
#define  P_ATR_EVT_POST_ERR_MASK		0x00100000u
#define  P_ATR_EVT_POST_ERR_SHIFT		20
#define  P_ATR_EVT_FETCH_ERR_MASK		0x00200000u
#define  P_ATR_EVT_FETCH_ERR_SHIFT		21
#define  P_ATR_EVT_DISCARD_ERR_MASK		0x00400000u
#define  P_ATR_EVT_DISCARD_ERR_SHIFT		22
#define  P_ATR_EVT_DOORBELL_MASK		0x00000000u
#define  P_ATR_EVT_DOORBELL_SHIFT		23
#define  PM_MSI_INT_INTA_MASK			0x01000000u
#define  PM_MSI_INT_INTA_SHIFT			24
#define  PM_MSI_INT_INTB_MASK			0x02000000u
#define  PM_MSI_INT_INTB_SHIFT			25
#define  PM_MSI_INT_INTC_MASK			0x04000000u
#define  PM_MSI_INT_INTC_SHIFT			26
#define  PM_MSI_INT_INTD_MASK			0x08000000u
#define  PM_MSI_INT_INTD_SHIFT			27
#define  PM_MSI_INT_INTX_MASK			0x0f000000u
#define  PM_MSI_INT_INTX_SHIFT			24
#define  PM_MSI_INT_MSI_MASK			0x10000000u
#define  PM_MSI_INT_MSI_SHIFT			28
#define  PM_MSI_INT_AER_EVT_MASK		0x20000000u
#define  PM_MSI_INT_AER_EVT_SHIFT		29
#define  PM_MSI_INT_EVENTS_MASK			0x40000000u
#define  PM_MSI_INT_EVENTS_SHIFT		30
#define  PM_MSI_INT_SYS_ERR_MASK		0x80000000u
#define  PM_MSI_INT_SYS_ERR_SHIFT		31
#define  NUM_LOCAL_EVENTS			15
#define ISTATUS_LOCAL				0x184
#define IMASK_HOST				0x188
#define ISTATUS_HOST				0x18c
#define IMSI_ADDR				0x190
#define ISTATUS_MSI				0x194

/* PCIe Master table init defines */
#define ATR0_PCIE_WIN0_SRCADDR_PARAM		0x600u
#define  ATR0_PCIE_ATR_SIZE			0x25
#define  ATR0_PCIE_ATR_SIZE_SHIFT		1
#define ATR0_PCIE_WIN0_SRC_ADDR			0x604u
#define ATR0_PCIE_WIN0_TRSL_ADDR_LSB		0x608u
#define ATR0_PCIE_WIN0_TRSL_ADDR_UDW		0x60cu
#define ATR0_PCIE_WIN0_TRSL_PARAM		0x610u

/* PCIe AXI slave table init defines */
#define ATR0_AXI4_SLV0_SRCADDR_PARAM		0x800u
#define  ATR_SIZE_SHIFT				1
#define  ATR_IMPL_ENABLE			1
#define ATR0_AXI4_SLV0_SRC_ADDR			0x804u
#define ATR0_AXI4_SLV0_TRSL_ADDR_LSB		0x808u
#define ATR0_AXI4_SLV0_TRSL_ADDR_UDW		0x80cu
#define ATR0_AXI4_SLV0_TRSL_PARAM		0x810u
#define  PCIE_TX_RX_INTERFACE			0x00000000u
#define  PCIE_CONFIG_INTERFACE			0x00000001u

#define ATR_ENTRY_SIZE				32

enum plda_int_event {
	PLDA_AXI_POST_ERR,
	PLDA_AXI_FETCH_ERR,
	PLDA_AXI_DISCARD_ERR,
	PLDA_AXI_DOORBELL,
	PLDA_PCIE_POST_ERR,
	PLDA_PCIE_FETCH_ERR,
	PLDA_PCIE_DISCARD_ERR,
	PLDA_PCIE_DOORBELL,
	PLDA_INTX,
	PLDA_MSI,
	PLDA_AER_EVENT,
	PLDA_MISC_EVENTS,
	PLDA_SYS_ERR,
	PLDA_INT_EVENT_NUM
};

#define PLDA_NUM_DMA_EVENTS			16

#define PLDA_MAX_EVENT_NUM			(PLDA_NUM_DMA_EVENTS + PLDA_INT_EVENT_NUM)

struct plda_msi {
	struct mutex lock;		/* Protect used bitmap */
	struct irq_domain *msi_domain;
	struct irq_domain *dev_domain;
	u32 num_vectors;
	u64 vector_phy;
	DECLARE_BITMAP(used, PLDA_MAX_NUM_MSI_IRQS);
};

struct plda_pcie_rp {
	struct device *dev;
	struct irq_domain *intx_domain;
	struct irq_domain *event_domain;
	raw_spinlock_t lock;
	struct plda_msi msi;
	void __iomem *bridge_addr;
};

#endif
