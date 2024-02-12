/* SPDX-License-Identifier: GPL-2.0 */
/* Marvell Octeon EP (EndPoint) VF Ethernet Driver
 *
 * Copyright (C) 2020 Marvell.
 *
 */

#ifndef _OCTEP_VF_MAIN_H_
#define _OCTEP_VF_MAIN_H_

#include "octep_vf_tx.h"
#include "octep_vf_rx.h"
#include "octep_vf_mbox.h"

#define OCTEP_VF_DRV_NAME	"octeon_ep_vf"
#define OCTEP_VF_DRV_STRING	"Marvell Octeon EndPoint NIC VF Driver"

#define  OCTEP_PCI_DEVICE_ID_CN93_VF   0xB203    //93xx VF
#define  OCTEP_PCI_DEVICE_ID_CNF95N_VF 0xB403    //95N VF
#define  OCTEP_PCI_DEVICE_ID_CN98_VF	0xB103
#define  OCTEP_PCI_DEVICE_ID_CN10KA_VF  0xB903
#define  OCTEP_PCI_DEVICE_ID_CNF10KA_VF 0xBA03
#define  OCTEP_PCI_DEVICE_ID_CNF10KB_VF 0xBC03
#define  OCTEP_PCI_DEVICE_ID_CN10KB_VF  0xBD03

#define  OCTEP_VF_MAX_QUEUES   63
#define  OCTEP_VF_MAX_IQ       OCTEP_VF_MAX_QUEUES
#define  OCTEP_VF_MAX_OQ       OCTEP_VF_MAX_QUEUES

#define OCTEP_VF_MAX_MSIX_VECTORS OCTEP_VF_MAX_OQ

#define  OCTEP_VF_IQ_INTR_RESEND_BIT  59
#define  OCTEP_VF_OQ_INTR_RESEND_BIT  59

#define  IQ_INSTR_PENDING(iq)  ({ typeof(iq) iq__ = (iq); \
				  ((iq__)->host_write_index - (iq__)->flush_index) & \
				  (iq__)->ring_size_mask; \
				})
#define  IQ_INSTR_SPACE(iq)    ({ typeof(iq) iq_ = (iq); \
				  (iq_)->max_count - IQ_INSTR_PENDING(iq_); \
				})

/* PCI address space mapping information.
 * Each of the 3 address spaces given by BAR0, BAR2 and BAR4 of
 * Octeon gets mapped to different physical address spaces in
 * the kernel.
 */
struct octep_vf_mmio {
	/* The physical address to which the PCI address space is mapped. */
	u8 __iomem *hw_addr;

	/* Flag indicating the mapping was successful. */
	int mapped;
};

struct octep_vf_hw_ops {
	void (*setup_iq_regs)(struct octep_vf_device *oct, int q);
	void (*setup_oq_regs)(struct octep_vf_device *oct, int q);
	void (*setup_mbox_regs)(struct octep_vf_device *oct, int mbox);

	irqreturn_t (*non_ioq_intr_handler)(void *ioq_vector);
	irqreturn_t (*ioq_intr_handler)(void *ioq_vector);
	void (*reinit_regs)(struct octep_vf_device *oct);
	u32  (*update_iq_read_idx)(struct octep_vf_iq *iq);

	void (*enable_interrupts)(struct octep_vf_device *oct);
	void (*disable_interrupts)(struct octep_vf_device *oct);

	void (*enable_io_queues)(struct octep_vf_device *oct);
	void (*disable_io_queues)(struct octep_vf_device *oct);
	void (*enable_iq)(struct octep_vf_device *oct, int q);
	void (*disable_iq)(struct octep_vf_device *oct, int q);
	void (*enable_oq)(struct octep_vf_device *oct, int q);
	void (*disable_oq)(struct octep_vf_device *oct, int q);
	void (*reset_io_queues)(struct octep_vf_device *oct);
	void (*dump_registers)(struct octep_vf_device *oct);
};

/* Octeon mailbox data */
struct octep_vf_mbox_data {
	/* Holds the offset of received data via mailbox. */
	u32 data_index;

	/* Holds the received data via mailbox. */
	u8 recv_data[OCTEP_PFVF_MBOX_MAX_DATA_BUF_SIZE];
};

/* wrappers around work structs */
struct octep_vf_mbox_wk {
	struct work_struct work;
	void *ctxptr;
};

/* Octeon device mailbox */
struct octep_vf_mbox {
	/* A mutex to protect access to this q_mbox. */
	struct mutex lock;

	u32 state;

	/* SLI_MAC_PF_MBOX_INT for PF, SLI_PKT_MBOX_INT for VF. */
	u8 __iomem *mbox_int_reg;

	/* SLI_PKT_PF_VF_MBOX_SIG(0) for PF,
	 * SLI_PKT_PF_VF_MBOX_SIG(1) for VF.
	 */
	u8 __iomem *mbox_write_reg;

	/* SLI_PKT_PF_VF_MBOX_SIG(1) for PF,
	 * SLI_PKT_PF_VF_MBOX_SIG(0) for VF.
	 */
	u8 __iomem *mbox_read_reg;

	/* Octeon mailbox data */
	struct octep_vf_mbox_data mbox_data;

	/* Octeon mailbox work handler to process Mbox messages */
	struct octep_vf_mbox_wk wk;
};

/* Tx/Rx queue vector per interrupt. */
struct octep_vf_ioq_vector {
	char name[OCTEP_VF_MSIX_NAME_SIZE];
	struct napi_struct napi;
	struct octep_vf_device *octep_vf_dev;
	struct octep_vf_iq *iq;
	struct octep_vf_oq *oq;
	cpumask_t affinity_mask;
};

/* Octeon hardware/firmware offload capability flags. */
#define OCTEP_VF_CAP_TX_CHECKSUM BIT(0)
#define OCTEP_VF_CAP_RX_CHECKSUM BIT(1)
#define OCTEP_VF_CAP_TSO         BIT(2)

/* Link modes */
enum octep_vf_link_mode_bit_indices {
	OCTEP_VF_LINK_MODE_10GBASE_T    = 0,
	OCTEP_VF_LINK_MODE_10GBASE_R,
	OCTEP_VF_LINK_MODE_10GBASE_CR,
	OCTEP_VF_LINK_MODE_10GBASE_KR,
	OCTEP_VF_LINK_MODE_10GBASE_LR,
	OCTEP_VF_LINK_MODE_10GBASE_SR,
	OCTEP_VF_LINK_MODE_25GBASE_CR,
	OCTEP_VF_LINK_MODE_25GBASE_KR,
	OCTEP_VF_LINK_MODE_25GBASE_SR,
	OCTEP_VF_LINK_MODE_40GBASE_CR4,
	OCTEP_VF_LINK_MODE_40GBASE_KR4,
	OCTEP_VF_LINK_MODE_40GBASE_LR4,
	OCTEP_VF_LINK_MODE_40GBASE_SR4,
	OCTEP_VF_LINK_MODE_50GBASE_CR2,
	OCTEP_VF_LINK_MODE_50GBASE_KR2,
	OCTEP_VF_LINK_MODE_50GBASE_SR2,
	OCTEP_VF_LINK_MODE_50GBASE_CR,
	OCTEP_VF_LINK_MODE_50GBASE_KR,
	OCTEP_VF_LINK_MODE_50GBASE_LR,
	OCTEP_VF_LINK_MODE_50GBASE_SR,
	OCTEP_VF_LINK_MODE_100GBASE_CR4,
	OCTEP_VF_LINK_MODE_100GBASE_KR4,
	OCTEP_VF_LINK_MODE_100GBASE_LR4,
	OCTEP_VF_LINK_MODE_100GBASE_SR4,
	OCTEP_VF_LINK_MODE_NBITS
};

/* Hardware interface link state information. */
struct octep_vf_iface_link_info {
	/* Bitmap of Supported link speeds/modes. */
	u64 supported_modes;

	/* Bitmap of Advertised link speeds/modes. */
	u64 advertised_modes;

	/* Negotiated link speed in Mbps. */
	u32 speed;

	/* MTU */
	u16 mtu;

	/* Autonegotiation state. */
#define OCTEP_VF_LINK_MODE_AUTONEG_SUPPORTED   BIT(0)
#define OCTEP_VF_LINK_MODE_AUTONEG_ADVERTISED  BIT(1)
	u8 autoneg;

	/* Pause frames setting. */
#define OCTEP_VF_LINK_MODE_PAUSE_SUPPORTED   BIT(0)
#define OCTEP_VF_LINK_MODE_PAUSE_ADVERTISED  BIT(1)
	u8 pause;

	/* Admin state of the link (ifconfig <iface> up/down */
	u8  admin_up;

	/* Operational state of the link: physical link is up down */
	u8  oper_up;
};

/* Hardware interface stats information. */
struct octep_vf_iface_rxtx_stats {
	/* Hardware Interface Rx statistics */
	struct octep_vf_iface_rx_stats iface_rx_stats;

	/* Hardware Interface Tx statistics */
	struct octep_vf_iface_tx_stats iface_tx_stats;
};

struct octep_vf_fw_info {
	/* pkind value to be used in every Tx hardware descriptor */
	u8 pkind;
	/* front size data */
	u8 fsz;
	/* supported rx offloads OCTEP_VF_RX_OFFLOAD_* */
	u16 rx_ol_flags;
	/* supported tx offloads OCTEP_VF_TX_OFFLOAD_* */
	u16 tx_ol_flags;
};

/* The Octeon device specific private data structure.
 * Each Octeon device has this structure to represent all its components.
 */
struct octep_vf_device {
	struct octep_vf_config *conf;

	/* Octeon Chip type. */
	u16 chip_id;
	u16 rev_id;

	/* Device capabilities enabled */
	u64 caps_enabled;
	/* Device capabilities supported */
	u64 caps_supported;

	/* Pointer to basic Linux device */
	struct device *dev;
	/* Linux PCI device pointer */
	struct pci_dev *pdev;
	/* Netdev corresponding to the Octeon device */
	struct net_device *netdev;

	/* memory mapped io range */
	struct octep_vf_mmio mmio;

	/* MAC address */
	u8 mac_addr[ETH_ALEN];

	/* Tx queues (IQ: Instruction Queue) */
	u16 num_iqs;
	/* Pointers to Octeon Tx queues */
	struct octep_vf_iq *iq[OCTEP_VF_MAX_IQ];

	/* Rx queues (OQ: Output Queue) */
	u16 num_oqs;
	/* Pointers to Octeon Rx queues */
	struct octep_vf_oq *oq[OCTEP_VF_MAX_OQ];

	/* Hardware port number of the PCIe interface */
	u16 pcie_port;

	/* Hardware operations */
	struct octep_vf_hw_ops hw_ops;

	/* IRQ info */
	u16 num_irqs;
	u16 num_non_ioq_irqs;
	char *non_ioq_irq_names;
	struct msix_entry *msix_entries;
	/* IOq information of it's corresponding MSI-X interrupt. */
	struct octep_vf_ioq_vector *ioq_vector[OCTEP_VF_MAX_QUEUES];

	/* Hardware Interface Tx statistics */
	struct octep_vf_iface_tx_stats iface_tx_stats;
	/* Hardware Interface Rx statistics */
	struct octep_vf_iface_rx_stats iface_rx_stats;

	/* Hardware Interface Link info like supported modes, aneg support */
	struct octep_vf_iface_link_info link_info;

	/* Mailbox to talk to VFs */
	struct octep_vf_mbox *mbox;

	/* Work entry to handle Tx timeout */
	struct work_struct tx_timeout_task;

	/* offset for iface stats */
	u32 ctrl_mbox_ifstats_offset;

	/* Negotiated Mbox version */
	u32 mbox_neg_ver;

	/* firmware info */
	struct octep_vf_fw_info fw_info;
};

static inline u16 OCTEP_VF_MAJOR_REV(struct octep_vf_device *oct)
{
	u16 rev = (oct->rev_id & 0xC) >> 2;

	return (rev == 0) ? 1 : rev;
}

static inline u16 OCTEP_VF_MINOR_REV(struct octep_vf_device *oct)
{
	return (oct->rev_id & 0x3);
}

/* Octeon CSR read/write access APIs */
#define octep_vf_write_csr(octep_vf_dev, reg_off, value) \
	writel(value, (octep_vf_dev)->mmio.hw_addr + (reg_off))

#define octep_vf_write_csr64(octep_vf_dev, reg_off, val64) \
	writeq(val64, (octep_vf_dev)->mmio.hw_addr + (reg_off))

#define octep_vf_read_csr(octep_vf_dev, reg_off)         \
	readl((octep_vf_dev)->mmio.hw_addr + (reg_off))

#define octep_vf_read_csr64(octep_vf_dev, reg_off)         \
	readq((octep_vf_dev)->mmio.hw_addr + (reg_off))

extern struct workqueue_struct *octep_vf_wq;

int octep_vf_device_setup(struct octep_vf_device *oct);
int octep_vf_setup_iqs(struct octep_vf_device *oct);
void octep_vf_free_iqs(struct octep_vf_device *oct);
void octep_vf_clean_iqs(struct octep_vf_device *oct);
int octep_vf_setup_oqs(struct octep_vf_device *oct);
void octep_vf_free_oqs(struct octep_vf_device *oct);
void octep_vf_oq_dbell_init(struct octep_vf_device *oct);
void octep_vf_device_setup_cn93(struct octep_vf_device *oct);
void octep_vf_device_setup_cnxk(struct octep_vf_device *oct);
int octep_vf_iq_process_completions(struct octep_vf_iq *iq, u16 budget);
int octep_vf_oq_process_rx(struct octep_vf_oq *oq, int budget);
void octep_vf_set_ethtool_ops(struct net_device *netdev);
int octep_vf_get_link_info(struct octep_vf_device *oct);
int octep_vf_get_if_stats(struct octep_vf_device *oct);
void octep_vf_mbox_work(struct work_struct *work);
#endif /* _OCTEP_VF_MAIN_H_ */
