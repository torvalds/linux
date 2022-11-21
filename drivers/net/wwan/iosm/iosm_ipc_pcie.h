/* SPDX-License-Identifier: GPL-2.0-only
 *
 * Copyright (C) 2020-21 Intel Corporation.
 */

#ifndef IOSM_IPC_PCIE_H
#define IOSM_IPC_PCIE_H

#include <linux/device.h>
#include <linux/pci.h>
#include <linux/skbuff.h>

#include "iosm_ipc_irq.h"

/* Device ID */
#define INTEL_CP_DEVICE_7560_ID 0x7560
#define INTEL_CP_DEVICE_7360_ID 0x7360

/* Define for BAR area usage */
#define IPC_DOORBELL_BAR0 0
#define IPC_SCRATCHPAD_BAR2 2

/* Defines for DOORBELL registers information */
#define IPC_DOORBELL_CH_OFFSET BIT(5)
#define IPC_WRITE_PTR_REG_0 BIT(4)
#define IPC_CAPTURE_PTR_REG_0 BIT(3)

/* Number of MSI used for IPC */
#define IPC_MSI_VECTORS 1

/* Total number of Maximum IPC IRQ vectors used for IPC */
#define IPC_IRQ_VECTORS IPC_MSI_VECTORS

/**
 * enum ipc_pcie_sleep_state - Enum type to different sleep state transitions
 * @IPC_PCIE_D0L12:	Put the sleep state in D0L12
 * @IPC_PCIE_D3L2:	Put the sleep state in D3L2
 */
enum ipc_pcie_sleep_state {
	IPC_PCIE_D0L12,
	IPC_PCIE_D3L2,
};

/**
 * struct iosm_pcie - IPC_PCIE struct.
 * @pci:			Address of the device description
 * @dev:			Pointer to generic device structure
 * @ipc_regs:			Remapped CP doorbell address of the irq register
 *				set, to fire the doorbell irq.
 * @scratchpad:			Remapped CP scratchpad address, to send the
 *				configuration. tuple and the IPC descriptors
 *				to CP in the ROM phase. The config tuple
 *				information are saved on the MSI scratchpad.
 * @imem:			Pointer to imem data struct
 * @ipc_regs_bar_nr:		BAR number to be used for IPC doorbell
 * @scratchpad_bar_nr:		BAR number to be used for Scratchpad
 * @nvec:			number of requested irq vectors
 * @doorbell_reg_offset:	doorbell_reg_offset
 * @doorbell_write:		doorbell write register
 * @doorbell_capture:		doorbell capture resgister
 * @suspend:			S2IDLE sleep/active
 * @d3l2_support:		Read WWAN RTD3 BIOS setting for D3L2 support
 */
struct iosm_pcie {
	struct pci_dev *pci;
	struct device *dev;
	void __iomem *ipc_regs;
	void __iomem *scratchpad;
	struct iosm_imem *imem;
	int ipc_regs_bar_nr;
	int scratchpad_bar_nr;
	int nvec;
	u32 doorbell_reg_offset;
	u32 doorbell_write;
	u32 doorbell_capture;
	unsigned long suspend;
	enum ipc_pcie_sleep_state d3l2_support;
};

/**
 * struct ipc_skb_cb - Struct definition of the socket buffer which is mapped to
 *		       the cb field of sbk
 * @mapping:	Store physical or IOVA mapped address of skb virtual add.
 * @direction:	DMA direction
 * @len:	Length of the DMA mapped region
 * @op_type:    Expected values are defined about enum ipc_ul_usr_op.
 */
struct ipc_skb_cb {
	dma_addr_t mapping;
	int direction;
	int len;
	u8 op_type;
};

/**
 * enum ipc_ul_usr_op - Control operation to execute the right action on
 *			the user interface.
 * @UL_USR_OP_BLOCKED:	The uplink app was blocked until CP confirms that the
 *			uplink buffer was consumed triggered by the IRQ.
 * @UL_MUX_OP_ADB:	In MUX mode the UL ADB shall be addedd to the free list.
 * @UL_DEFAULT:		SKB in non muxing mode
 */
enum ipc_ul_usr_op {
	UL_USR_OP_BLOCKED,
	UL_MUX_OP_ADB,
	UL_DEFAULT,
};

/**
 * ipc_pcie_addr_map - Maps the kernel's virtual address to either IOVA
 *		       address space or Physical address space, the mapping is
 *		       stored in the skb's cb.
 * @ipc_pcie:	Pointer to struct iosm_pcie
 * @data:	Skb mem containing data
 * @size:	Data size
 * @mapping:	Dma mapping address
 * @direction:	Data direction
 *
 * Returns: 0 on success and failure value on error
 */
int ipc_pcie_addr_map(struct iosm_pcie *ipc_pcie, unsigned char *data,
		      size_t size, dma_addr_t *mapping, int direction);

/**
 * ipc_pcie_addr_unmap - Unmaps the skb memory region from IOVA address space
 * @ipc_pcie:	Pointer to struct iosm_pcie
 * @size:	Data size
 * @mapping:	Dma mapping address
 * @direction:	Data direction
 */
void ipc_pcie_addr_unmap(struct iosm_pcie *ipc_pcie, size_t size,
			 dma_addr_t mapping, int direction);

/**
 * ipc_pcie_alloc_skb - Allocate an uplink SKB for the given size.
 * @ipc_pcie:	Pointer to struct iosm_pcie
 * @size:	Size of the SKB required.
 * @flags:	Allocation flags
 * @mapping:	Copies either mapped IOVA add. or converted Phy address
 * @direction:	DMA data direction
 * @headroom:	Header data offset
 *
 * Returns: Pointer to ipc_skb on Success, NULL on failure.
 */
struct sk_buff *ipc_pcie_alloc_skb(struct iosm_pcie *ipc_pcie, size_t size,
				   gfp_t flags, dma_addr_t *mapping,
				   int direction, size_t headroom);

/**
 * ipc_pcie_alloc_local_skb - Allocate a local SKB for the given size.
 * @ipc_pcie:	Pointer to struct iosm_pcie
 * @flags:	Allocation flags
 * @size:	Size of the SKB required.
 *
 * Returns: Pointer to ipc_skb on Success, NULL on failure.
 */
struct sk_buff *ipc_pcie_alloc_local_skb(struct iosm_pcie *ipc_pcie,
					 gfp_t flags, size_t size);

/**
 * ipc_pcie_kfree_skb - Free skb allocated by ipc_pcie_alloc_*_skb().
 * @ipc_pcie:	Pointer to struct iosm_pcie
 * @skb:	Pointer to the skb
 */
void ipc_pcie_kfree_skb(struct iosm_pcie *ipc_pcie, struct sk_buff *skb);

/**
 * ipc_pcie_check_data_link_active - Check Data Link Layer Active
 * @ipc_pcie:	Pointer to struct iosm_pcie
 *
 * Returns: true if active, otherwise false
 */
bool ipc_pcie_check_data_link_active(struct iosm_pcie *ipc_pcie);

/**
 * ipc_pcie_suspend - Callback invoked by pm_runtime_suspend. It decrements
 *		     the device's usage count then, carry out a suspend,
 *		     either synchronous or asynchronous.
 * @ipc_pcie:	Pointer to struct iosm_pcie
 *
 * Returns: 0 on success and failure value on error
 */
int ipc_pcie_suspend(struct iosm_pcie *ipc_pcie);

/**
 * ipc_pcie_resume - Callback invoked by pm_runtime_resume. It increments
 *		    the device's usage count then, carry out a resume,
 *		    either synchronous or asynchronous.
 * @ipc_pcie:	Pointer to struct iosm_pcie
 *
 * Returns: 0 on success and failure value on error
 */
int ipc_pcie_resume(struct iosm_pcie *ipc_pcie);

/**
 * ipc_pcie_check_aspm_enabled - Check if ASPM L1 is already enabled
 * @ipc_pcie:			 Pointer to struct iosm_pcie
 * @parent:			 True if checking ASPM L1 for parent else false
 *
 * Returns: true if ASPM is already enabled else false
 */
bool ipc_pcie_check_aspm_enabled(struct iosm_pcie *ipc_pcie,
				 bool parent);
/**
 * ipc_pcie_config_aspm - Configure ASPM L1
 * @ipc_pcie:	Pointer to struct iosm_pcie
 */
void ipc_pcie_config_aspm(struct iosm_pcie *ipc_pcie);

#endif
