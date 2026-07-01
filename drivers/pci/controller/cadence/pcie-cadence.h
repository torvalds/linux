/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (c) 2017 Cadence
// Cadence PCIe controller driver.
// Author: Cyrille Pitchen <cyrille.pitchen@free-electrons.com>

#ifndef _PCIE_CADENCE_H
#define _PCIE_CADENCE_H

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pci-epf.h>
#include <linux/phy/phy.h>
#include "pcie-cadence-lga-regs.h"
#include "pcie-cadence-hpa-regs.h"

#define CDNS_PCIE_LGA_LTSSM_STATUS_MASK	GENMASK(29, 24)
#define CDNS_PCIE_HPA_LTSSM_STATUS_MASK	GENMASK(27, 20)

enum cdns_pcie_rp_bar {
	RP_BAR_UNDEFINED = -1,
	RP_BAR0,
	RP_BAR1,
	RP_NO_BAR
};

struct cdns_pcie_rp_ib_bar {
	u64 size;
	bool free;
};

struct cdns_pcie;
struct cdns_pcie_rc;

enum cdns_pcie_reg_bank {
	REG_BANK_RP,
	REG_BANK_IP_REG,
	REG_BANK_IP_CFG_CTRL_REG,
	REG_BANK_AXI_MASTER_COMMON,
	REG_BANK_AXI_MASTER,
	REG_BANK_AXI_SLAVE,
	REG_BANK_AXI_HLS,
	REG_BANK_AXI_RAS,
	REG_BANK_AXI_DTI,
	REG_BANKS_MAX,
};

enum cdns_pcie_lga_ltssm {
	CDNS_PCIE_LGA_LTSSM_DETECT_QUIET			= 0x00,
	CDNS_PCIE_LGA_LTSSM_DETECT_ACTIVE			= 0x01,
	CDNS_PCIE_LGA_LTSSM_POLLING_ACTIVE			= 0x02,
	CDNS_PCIE_LGA_LTSSM_POLLING_COMPLIANCE			= 0x03,
	CDNS_PCIE_LGA_LTSSM_POLLING_CONFIGURATION		= 0x04,
	CDNS_PCIE_LGA_LTSSM_CONFIGURATION_LINKWIDTH_START	= 0x05,
	CDNS_PCIE_LGA_LTSSM_CONFIGURATION_LINKWIDTH_ACCEPT	= 0x06,
	CDNS_PCIE_LGA_LTSSM_CONFIGURATION_LANENUM_ACCEPT	= 0x07,
	CDNS_PCIE_LGA_LTSSM_CONFIGURATION_LANENUM_WAIT		= 0x08,
	CDNS_PCIE_LGA_LTSSM_CONFIGURATION_COMPLETE		= 0x09,
	CDNS_PCIE_LGA_LTSSM_CONFIGURATION_IDLE			= 0x0A,
	CDNS_PCIE_LGA_LTSSM_RECOVERY_RCVRLOCK			= 0x0B,
	CDNS_PCIE_LGA_LTSSM_RECOVERY_SPEED			= 0x0C,
	CDNS_PCIE_LGA_LTSSM_RECOVERY_RCVRCFG			= 0x0D,
	CDNS_PCIE_LGA_LTSSM_RECOVERY_IDLE			= 0x0E,
	CDNS_PCIE_LGA_LTSSM_L0					= 0x10,
	CDNS_PCIE_LGA_LTSSM_RX_L0S_ENTRY			= 0x11,
	CDNS_PCIE_LGA_LTSSM_RX_L0S_IDLE				= 0x12,
	CDNS_PCIE_LGA_LTSSM_RX_L0S_FTS				= 0x13,
	CDNS_PCIE_LGA_LTSSM_TX_L0S_ENTRY			= 0x14,
	CDNS_PCIE_LGA_LTSSM_TX_L0S_IDLE				= 0x15,
	CDNS_PCIE_LGA_LTSSM_TX_L0S_FTS				= 0x16,
	CDNS_PCIE_LGA_LTSSM_L1_ENTRY				= 0x17,
	CDNS_PCIE_LGA_LTSSM_L1_IDLE				= 0x18,
	CDNS_PCIE_LGA_LTSSM_L2_IDLE				= 0x19,
	CDNS_PCIE_LGA_LTSSM_L2_TRANSMITWAKE			= 0x1A,
	CDNS_PCIE_LGA_LTSSM_DISABLED				= 0x20,
	CDNS_PCIE_LGA_LTSSM_LOOPBACK_ENTRY_MASTER		= 0x21,
	CDNS_PCIE_LGA_LTSSM_LOOPBACK_ACTIVE_MASTER		= 0x22,
	CDNS_PCIE_LGA_LTSSM_LOOPBACK_EXIT_MASTER		= 0x23,
	CDNS_PCIE_LGA_LTSSM_LOOPBACK_ENTRY_SLAVE		= 0x24,
	CDNS_PCIE_LGA_LTSSM_LOOPBACK_ACTIVE_SLAVE		= 0x25,
	CDNS_PCIE_LGA_LTSSM_LOOPBACK_EXIT_SLAVE			= 0x26,
	CDNS_PCIE_LGA_LTSSM_HOT_RESET				= 0x27,
	CDNS_PCIE_LGA_LTSSM_RECOVERY_EQUALIZATION_PHASE_0	= 0x28,
	CDNS_PCIE_LGA_LTSSM_RECOVERY_EQUALIZATION_PHASE_1	= 0x29,
	CDNS_PCIE_LGA_LTSSM_RECOVERY_EQUALIZATION_PHASE_2	= 0x2A,
	CDNS_PCIE_LGA_LTSSM_RECOVERY_EQUALIZATION_PHASE_3	= 0x2B,
	CDNS_PCIE_LGA_LTSSM_UNKNOWN				= 0xFFFFFFFF,
};

enum cdns_pcie_hpa_ltssm {
	CDNS_PCIE_HPA_LTSSM_DETECT_QUIET		= 0,
	CDNS_PCIE_HPA_LTSSM_DETECT_QUIET_ENTRY		= 1,
	CDNS_PCIE_HPA_LTSSM_DETECT_ACTIVE		= 2,
	CDNS_PCIE_HPA_LTSSM_DETECT_ACTIVE_1		= 3,
	CDNS_PCIE_HPA_LTSSM_DETECT_ACTIVE_2		= 4,
	CDNS_PCIE_HPA_LTSSM_DETECT_ACTIVE_3		= 5,
	CDNS_PCIE_HPA_LTSSM_RCVR_DETECTED_ST		= 6,
	CDNS_PCIE_HPA_LTSSM_RCVR_DETECTED_1		= 7,
	CDNS_PCIE_HPA_LTSSM_POLLING_ACTIVE		= 8,
	CDNS_PCIE_HPA_LTSSM_POLLING_ACTIVE_1		= 9,
	CDNS_PCIE_HPA_LTSSM_POLLING_ACTIVE_2		= 10,
	CDNS_PCIE_HPA_LTSSM_POLLING_ACTIVE_3		= 11,
	CDNS_PCIE_HPA_LTSSM_POLLING_COMPLIANCE		= 12,
	CDNS_PCIE_HPA_LTSSM_POLLING_COMPLIANCE_1	= 13,
	CDNS_PCIE_HPA_LTSSM_POLLING_CONFIG		= 14,
	CDNS_PCIE_HPA_LTSSM_POLLING_CONFIG_1		= 15,
	CDNS_PCIE_HPA_LTSSM_POLLING_CONFIG_2		= 16,
	CDNS_PCIE_HPA_LTSSM_CONFIG_LW_START_RC		= 17,
	CDNS_PCIE_HPA_LTSSM_CONFIG_LW_START_RC_1	= 18,
	CDNS_PCIE_HPA_LTSSM_CONFIG_LW_START_RC_2	= 19,
	CDNS_PCIE_HPA_LTSSM_CONFIG_LW_ACC_RC		= 20,
	CDNS_PCIE_HPA_LTSSM_CONFIG_LANENUM_WAIT_RC	= 21,
	CDNS_PCIE_HPA_LTSSM_CONFIG_LANENUM_WAIT_RC_1	= 22,
	CDNS_PCIE_HPA_LTSSM_CONFIG_LANENUM_ACC_RC	= 23,
	CDNS_PCIE_HPA_LTSSM_CONFIG_LW_START_EP		= 24,
	CDNS_PCIE_HPA_LTSSM_CONFIG_LW_START_EP_1	= 25,
	CDNS_PCIE_HPA_LTSSM_CONFIG_LW_START_EP_2	= 26,
	CDNS_PCIE_HPA_LTSSM_CONFIG_LW_ACC_EP		= 27,
	CDNS_PCIE_HPA_LTSSM_CONFIG_LANENUM_WAIT_EP	= 28,
	CDNS_PCIE_HPA_LTSSM_CONFIG_LANENUM_WAIT_EP_1	= 29,
	CDNS_PCIE_HPA_LTSSM_CONFIG_LANENUM_ACC_EP	= 30,
	CDNS_PCIE_HPA_LTSSM_CONFIG_LANENUM_ACC_EP_1	= 31,
	CDNS_PCIE_HPA_LTSSM_DUMMY_STATE_1		= 32,
	CDNS_PCIE_HPA_LTSSM_CONFIG_COMPLETE		= 33,
	CDNS_PCIE_HPA_LTSSM_CONFIG_COMPLETE_1		= 34,
	CDNS_PCIE_HPA_LTSSM_CONFIG_COMPLETE_2		= 35,
	CDNS_PCIE_HPA_LTSSM_CONFIG_IDLE			= 36,
	CDNS_PCIE_HPA_LTSSM_CONFIG_IDLE_1		= 37,
	CDNS_PCIE_HPA_LTSSM_DUMMY_STATE_2		= 38,
	CDNS_PCIE_HPA_LTSSM_DUMMY_STATE_3		= 39,
	CDNS_PCIE_HPA_LTSSM_DUMMY_STATE_4		= 40,
	CDNS_PCIE_HPA_LTSSM_L0_STATE			= 41,
	CDNS_PCIE_HPA_LTSSM_RECOVERY_RCVR_LOCK		= 42,
	CDNS_PCIE_HPA_LTSSM_RECOVERY_RCVR_LOCK_1	= 43,
	CDNS_PCIE_HPA_LTSSM_RECOVERY_RCVR_CFG		= 44,
	CDNS_PCIE_HPA_LTSSM_RECOVERY_RCVR_CFG_1		= 45,
	CDNS_PCIE_HPA_LTSSM_RECOVERY_IDLE		= 46,
	CDNS_PCIE_HPA_LTSSM_RECOVERY_IDLE_1		= 47,
	CDNS_PCIE_HPA_LTSSM_DISABLE_LINK		= 48,
	CDNS_PCIE_HPA_LTSSM_DISABLE_LINK_1		= 49,
	CDNS_PCIE_HPA_LTSSM_DISABLE_LINK_2		= 50,
	CDNS_PCIE_HPA_LTSSM_DISABLE_LINK_3		= 51,
	CDNS_PCIE_HPA_LTSSM_DISABLE_LINK_4		= 52,
	CDNS_PCIE_HPA_LTSSM_DISABLE_LINK_5		= 53,
	CDNS_PCIE_HPA_LTSSM_DISABLE_LINK_6		= 54,
	CDNS_PCIE_HPA_LTSSM_DISABLE_LINK_7		= 55,
	CDNS_PCIE_HPA_LTSSM_HOT_RESET			= 56,
	CDNS_PCIE_HPA_LTSSM_HOT_RESET_1			= 57,
	CDNS_PCIE_HPA_LTSSM_HOT_RESET_2			= 58,
	CDNS_PCIE_HPA_LTSSM_HOT_RESET_3			= 59,
	CDNS_PCIE_HPA_LTSSM_L0S_ENTRY			= 60,
	CDNS_PCIE_HPA_LTSSM_L0S_1			= 61,
	CDNS_PCIE_HPA_LTSSM_L0S_2			= 62,
	CDNS_PCIE_HPA_LTSSM_L0S_3			= 63,
	CDNS_PCIE_HPA_LTSSM_L0S_4			= 64,
	CDNS_PCIE_HPA_LTSSM_L0S_5			= 65,
	CDNS_PCIE_HPA_LTSSM_WAIT_FOR_LINK_TX		= 66,
	CDNS_PCIE_HPA_LTSSM_TX_FTS_ENTRY		= 67,
	CDNS_PCIE_HPA_LTSSM_TX_FTS_1			= 68,
	CDNS_PCIE_HPA_LTSSM_TX_FTS_2			= 69,
	CDNS_PCIE_HPA_LTSSM_TX_ELEC_IDLE_ST		= 70,
	CDNS_PCIE_HPA_LTSSM_TX_ELEC_IDLE_1		= 71,
	CDNS_PCIE_HPA_LTSSM_TX_ELEC_IDLE_2		= 72,
	CDNS_PCIE_HPA_LTSSM_TX_ELEC_IDLE_3		= 73,
	CDNS_PCIE_HPA_LTSSM_RECOVERY_SPEED		= 74,
	CDNS_PCIE_HPA_LTSSM_RECOVERY_SPEED_1		= 75,
	CDNS_PCIE_HPA_LTSSM_RECOVERY_SPEED_2		= 76,
	CDNS_PCIE_HPA_LTSSM_RECOVERY_SPEED_3		= 77,
	CDNS_PCIE_HPA_LTSSM_POLLING_COMPLIANCE_GEN23	= 78,
	CDNS_PCIE_HPA_LTSSM_POLLING_COMPLIANCE_GEN23_1	= 79,
	CDNS_PCIE_HPA_LTSSM_POLLING_COMPLIANCE_GEN23_2	= 80,
	CDNS_PCIE_HPA_LTSSM_POLLING_COMPLIANCE_GEN23_3	= 81,
	CDNS_PCIE_HPA_LTSSM_POLLING_COMPLIANCE_GEN23_4	= 82,
	CDNS_PCIE_HPA_LTSSM_POLLING_COMPLIANCE_GEN23_5	= 83,
	CDNS_PCIE_HPA_LTSSM_POLLING_COMPLIANCE_GEN23_6	= 84,
	CDNS_PCIE_HPA_LTSSM_POLLING_COMPLIANCE_GEN23_7	= 85,
	CDNS_PCIE_HPA_LTSSM_POLLING_COMPLIANCE_GEN23_8	= 86,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_SLAVE_ENTRY	= 87,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_SLAVE_ENTRY_FROM_RECOVERY = 88,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_SLAVE_EXIT_1	= 89,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_SLAVE_EXIT		= 90,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_SLAVE_GEN2_1	= 91,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_SLAVE_GEN2_2	= 92,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_SLAVE_GEN2_3	= 93,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_SLAVE_GEN2_4	= 94,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_SLAVE_GEN2_5	= 95,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_SLAVE_ACTIVE	= 96,
	CDNS_PCIE_HPA_LTSSM_L1_ENTRY			= 97,
	CDNS_PCIE_HPA_LTSSM_L1_1			= 98,
	CDNS_PCIE_HPA_LTSSM_L1_2			= 99,
	CDNS_PCIE_HPA_LTSSM_L1_3			= 100,
	CDNS_PCIE_HPA_LTSSM_L1_4			= 101,
	CDNS_PCIE_HPA_LTSSM_L1_IDLE			= 102,
	CDNS_PCIE_HPA_LTSSM_L1_EXIT			= 103,
	CDNS_PCIE_HPA_LTSSM_L2_ENTRY			= 104,
	CDNS_PCIE_HPA_LTSSM_L2_1			= 105,
	CDNS_PCIE_HPA_LTSSM_L2_2			= 106,
	CDNS_PCIE_HPA_LTSSM_L2_3			= 107,
	CDNS_PCIE_HPA_LTSSM_L2_4			= 108,
	CDNS_PCIE_HPA_LTSSM_L2_5			= 109,
	CDNS_PCIE_HPA_LTSSM_L2_IDLE			= 110,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_MASTER_ENTRY	= 111,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_MASTER_ENTRY_1	= 112,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_MASTER_ENTRY_2	= 113,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_MASTER_ENTRY_3	= 114,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_MASTER_ENTRY_4	= 115,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_MASTER_ENTRY_5	= 116,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_MASTER_ENTRY_FROM_RECOVERY = 117,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_MASTER_ACTIVE	= 118,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_MASTER_EXIT	= 119,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_MASTER_EXIT_1	= 120,
	CDNS_PCIE_HPA_LTSSM_LOOPBACK_MASTER_EXIT_2	= 121,
	CDNS_PCIE_HPA_LTSSM_RECOVERY_EQUALIZATION_PHASE0 = 122,
	CDNS_PCIE_HPA_LTSSM_RECOVERY_EQUALIZATION_PHASE1 = 123,
	CDNS_PCIE_HPA_LTSSM_RECOVERY_EQUALIZATION_PHASE2_1 = 124,
	CDNS_PCIE_HPA_LTSSM_RECOVERY_EQUALIZATION_PHASE2_2 = 125,
	CDNS_PCIE_HPA_LTSSM_RECOVERY_EQUALIZATION_PHASE3_1 = 126,
	CDNS_PCIE_HPA_LTSSM_RECOVERY_EQUALIZATION_PHASE3_2 = 127,
	CDNS_PCIE_HPA_LTSSM_UNKNOWN			= 0xFFFFFFFF,
};

struct cdns_pcie_ops {
	int     (*start_link)(struct cdns_pcie *pcie);
	void    (*stop_link)(struct cdns_pcie *pcie);
	bool    (*link_up)(struct cdns_pcie *pcie);
	u64     (*cpu_addr_fixup)(struct cdns_pcie *pcie, u64 cpu_addr);
};

/**
 * struct cdns_plat_pcie_of_data - Register bank offset for a platform
 * @is_rc: controller is a RC
 * @ip_reg_bank_offset: ip register bank start offset
 * @ip_cfg_ctrl_reg_offset: ip config control register start offset
 * @axi_mstr_common_offset: AXI master common register start offset
 * @axi_slave_offset: AXI slave start offset
 * @axi_master_offset: AXI master start offset
 * @axi_hls_offset: AXI HLS offset start
 * @axi_ras_offset: AXI RAS offset
 * @axi_dti_offset: AXI DTI offset
 */
struct cdns_plat_pcie_of_data {
	u32 is_rc:1;
	u32 ip_reg_bank_offset;
	u32 ip_cfg_ctrl_reg_offset;
	u32 axi_mstr_common_offset;
	u32 axi_slave_offset;
	u32 axi_master_offset;
	u32 axi_hls_offset;
	u32 axi_ras_offset;
	u32 axi_dti_offset;
};

/**
 * struct cdns_pcie - private data for Cadence PCIe controller drivers
 * @reg_base: IO mapped register base
 * @mem_res: start/end offsets in the physical system memory to map PCI accesses
 * @msg_res: Region for send message to map PCI accesses
 * @dev: PCIe controller
 * @is_rc: tell whether the PCIe controller mode is Root Complex or Endpoint.
 * @is_hpa: indicates if the architecture is HPA
 * @phy_count: number of supported PHY devices
 * @phy: list of pointers to specific PHY control blocks
 * @link: list of pointers to corresponding device link representations
 * @ops: Platform-specific ops to control various inputs from Cadence PCIe
 *       wrapper
 * @cdns_pcie_reg_offsets: Register bank offsets for different SoC
 * @max_link_speed: Maximum supported link speed
 * @debug_dir: debugfs node
 */
struct cdns_pcie {
	void __iomem		             *reg_base;
	struct resource		             *mem_res;
	struct resource                      *msg_res;
	struct device		             *dev;
	bool			             is_rc;
	bool				     is_hpa;
	int			             phy_count;
	struct phy		             **phy;
	struct device_link	             **link;
	const  struct cdns_pcie_ops          *ops;
	const  struct cdns_plat_pcie_of_data *cdns_pcie_reg_offsets;
	int				     max_link_speed;
	struct dentry			     *debug_dir;
};

/**
 * struct cdns_pcie_rc - private data for this PCIe Root Complex driver
 * @pcie: Cadence PCIe controller
 * @cfg_res: start/end offsets in the physical system memory to map PCI
 *           configuration space accesses
 * @cfg_base: IO mapped window to access the PCI configuration space of a
 *            single function at a time
 * @vendor_id: PCI vendor ID
 * @device_id: PCI device ID
 * @avail_ib_bar: Status of RP_BAR0, RP_BAR1 and RP_NO_BAR if it's free or
 *                available
 * @quirk_retrain_flag: Retrain link as quirk for PCIe Gen2
 * @quirk_detect_quiet_flag: LTSSM Detect Quiet min delay set as quirk
 * @ecam_supported: Whether the ECAM is supported
 * @no_inbound_map: Whether inbound mapping is supported
 * @quirk_broken_aspm_l0s: Disable ASPM L0s support as quirk
 * @quirk_broken_aspm_l1: Disable ASPM L1 support as quirk
 */
struct cdns_pcie_rc {
	struct cdns_pcie	pcie;
	struct resource		*cfg_res;
	void __iomem		*cfg_base;
	u32			vendor_id;
	u32			device_id;
	bool			avail_ib_bar[CDNS_PCIE_RP_MAX_IB];
	unsigned int		quirk_retrain_flag:1;
	unsigned int		quirk_detect_quiet_flag:1;
	unsigned int            ecam_supported:1;
	unsigned int            no_inbound_map:1;
	unsigned int            quirk_broken_aspm_l0s:1;
	unsigned int            quirk_broken_aspm_l1:1;
};

/**
 * struct cdns_pcie_epf - Structure to hold info about endpoint function
 * @epf: Info about virtual functions attached to the physical function
 * @epf_bar: reference to the pci_epf_bar for the six Base Address Registers
 */
struct cdns_pcie_epf {
	struct cdns_pcie_epf *epf;
	struct pci_epf_bar *epf_bar[PCI_STD_NUM_BARS];
};

/**
 * struct cdns_pcie_ep - private data for this PCIe endpoint controller driver
 * @pcie: Cadence PCIe controller
 * @max_regions: maximum number of regions supported by hardware
 * @ob_region_map: bitmask of mapped outbound regions
 * @ob_addr: base addresses in the AXI bus where the outbound regions start
 * @irq_phys_addr: base address on the AXI bus where the MSI/INTX IRQ
 *		   dedicated outbound regions is mapped.
 * @irq_cpu_addr: base address in the CPU space where a write access triggers
 *		  the sending of a memory write (MSI) / normal message (INTX
 *		  IRQ) TLP through the PCIe bus.
 * @irq_pci_addr: used to save the current mapping of the MSI/INTX IRQ
 *		  dedicated outbound region.
 * @irq_pci_fn: the latest PCI function that has updated the mapping of
 *		the MSI/INTX IRQ dedicated outbound region.
 * @irq_pending: bitmask of asserted INTX IRQs.
 * @lock: spin lock to disable interrupts while modifying PCIe controller
 *        registers fields (RMW) accessible by both remote RC and EP to
 *        minimize time between read and write
 * @epf: Structure to hold info about endpoint function
 * @quirk_detect_quiet_flag: LTSSM Detect Quiet min delay set as quirk
 * @quirk_disable_flr: Disable FLR (Function Level Reset) quirk flag
 */
struct cdns_pcie_ep {
	struct cdns_pcie	pcie;
	u32			max_regions;
	unsigned long		ob_region_map;
	phys_addr_t		*ob_addr;
	phys_addr_t		irq_phys_addr;
	void __iomem		*irq_cpu_addr;
	u64			irq_pci_addr;
	u8			irq_pci_fn;
	u8			irq_pending;
	/* protect writing to PCI_STATUS while raising INTX interrupts */
	spinlock_t		lock;
	struct cdns_pcie_epf	*epf;
	unsigned int		quirk_detect_quiet_flag:1;
	unsigned int		quirk_disable_flr:1;
};

static inline u32 cdns_reg_bank_to_off(struct cdns_pcie *pcie, enum cdns_pcie_reg_bank bank)
{
	u32 offset = 0x0;

	switch (bank) {
	case REG_BANK_RP:
		offset = 0;
		break;
	case REG_BANK_IP_REG:
		offset = pcie->cdns_pcie_reg_offsets->ip_reg_bank_offset;
		break;
	case REG_BANK_IP_CFG_CTRL_REG:
		offset = pcie->cdns_pcie_reg_offsets->ip_cfg_ctrl_reg_offset;
		break;
	case REG_BANK_AXI_MASTER_COMMON:
		offset = pcie->cdns_pcie_reg_offsets->axi_mstr_common_offset;
		break;
	case REG_BANK_AXI_MASTER:
		offset = pcie->cdns_pcie_reg_offsets->axi_master_offset;
		break;
	case REG_BANK_AXI_SLAVE:
		offset = pcie->cdns_pcie_reg_offsets->axi_slave_offset;
		break;
	case REG_BANK_AXI_HLS:
		offset = pcie->cdns_pcie_reg_offsets->axi_hls_offset;
		break;
	case REG_BANK_AXI_RAS:
		offset = pcie->cdns_pcie_reg_offsets->axi_ras_offset;
		break;
	case REG_BANK_AXI_DTI:
		offset = pcie->cdns_pcie_reg_offsets->axi_dti_offset;
		break;
	default:
		break;
	}
	return offset;
}

/* Register access */
static inline void cdns_pcie_writel(struct cdns_pcie *pcie, u32 reg, u32 value)
{
	writel(value, pcie->reg_base + reg);
}

static inline u32 cdns_pcie_readl(struct cdns_pcie *pcie, u32 reg)
{
	return readl(pcie->reg_base + reg);
}

static inline void cdns_pcie_hpa_writel(struct cdns_pcie *pcie,
					enum cdns_pcie_reg_bank bank,
					u32 reg,
					u32 value)
{
	u32 offset = cdns_reg_bank_to_off(pcie, bank);

	reg += offset;
	writel(value, pcie->reg_base + reg);
}

static inline u32 cdns_pcie_hpa_readl(struct cdns_pcie *pcie,
				      enum cdns_pcie_reg_bank bank,
				      u32 reg)
{
	u32 offset = cdns_reg_bank_to_off(pcie, bank);

	reg += offset;
	return readl(pcie->reg_base + reg);
}

static inline u32 cdns_pcie_read_sz(void __iomem *addr, int size)
{
	void __iomem *aligned_addr = PTR_ALIGN_DOWN(addr, 0x4);
	unsigned int offset = (unsigned long)addr & 0x3;
	u32 val = readl(aligned_addr);

	if (!IS_ALIGNED((uintptr_t)addr, size)) {
		pr_warn("Address %p and size %d are not aligned\n", addr, size);
		return 0;
	}

	if (size > 2)
		return val;

	return (val >> (8 * offset)) & ((1 << (size * 8)) - 1);
}

static inline void cdns_pcie_write_sz(void __iomem *addr, int size, u32 value)
{
	void __iomem *aligned_addr = PTR_ALIGN_DOWN(addr, 0x4);
	unsigned int offset = (unsigned long)addr & 0x3;
	u32 mask;
	u32 val;

	if (!IS_ALIGNED((uintptr_t)addr, size)) {
		pr_warn("Address %p and size %d are not aligned\n", addr, size);
		return;
	}

	if (size > 2) {
		writel(value, addr);
		return;
	}

	mask = ~(((1 << (size * 8)) - 1) << (offset * 8));
	val = readl(aligned_addr) & mask;
	val |= value << (offset * 8);
	writel(val, aligned_addr);
}

static inline int cdns_pcie_read_cfg_byte(struct cdns_pcie *pcie, int where,
					  u8 *val)
{
	void __iomem *addr = pcie->reg_base + where;

	*val = cdns_pcie_read_sz(addr, 0x1);
	return PCIBIOS_SUCCESSFUL;
}

static inline int cdns_pcie_read_cfg_word(struct cdns_pcie *pcie, int where,
					  u16 *val)
{
	void __iomem *addr = pcie->reg_base + where;

	*val = cdns_pcie_read_sz(addr, 0x2);
	return PCIBIOS_SUCCESSFUL;
}

static inline int cdns_pcie_read_cfg_dword(struct cdns_pcie *pcie, int where,
					   u32 *val)
{
	*val = cdns_pcie_readl(pcie, where);
	return PCIBIOS_SUCCESSFUL;
}

/* Root Port register access */
static inline void cdns_pcie_rp_writeb(struct cdns_pcie *pcie,
				       u32 reg, u8 value)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_RP_BASE + reg;

	cdns_pcie_write_sz(addr, 0x1, value);
}

static inline void cdns_pcie_rp_writew(struct cdns_pcie *pcie,
				       u32 reg, u16 value)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_RP_BASE + reg;

	cdns_pcie_write_sz(addr, 0x2, value);
}

static inline u16 cdns_pcie_rp_readw(struct cdns_pcie *pcie, u32 reg)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_RP_BASE + reg;

	return cdns_pcie_read_sz(addr, 0x2);
}

static inline void cdns_pcie_rp_writel(struct cdns_pcie *pcie,
				       u32 reg, u32 value)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_RP_BASE + reg;

	cdns_pcie_write_sz(addr, 0x4, value);
}

static inline u32 cdns_pcie_rp_readl(struct cdns_pcie *pcie, u32 reg)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_RP_BASE + reg;

	return cdns_pcie_read_sz(addr, 0x4);
}

static inline void cdns_pcie_hpa_rp_writeb(struct cdns_pcie *pcie,
					   u32 reg, u8 value)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_HPA_RP_BASE + reg;

	cdns_pcie_write_sz(addr, 0x1, value);
}

static inline void cdns_pcie_hpa_rp_writew(struct cdns_pcie *pcie,
					   u32 reg, u16 value)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_HPA_RP_BASE + reg;

	cdns_pcie_write_sz(addr, 0x2, value);
}

static inline u16 cdns_pcie_hpa_rp_readw(struct cdns_pcie *pcie, u32 reg)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_HPA_RP_BASE + reg;

	return cdns_pcie_read_sz(addr, 0x2);
}

/* Endpoint Function register access */
static inline void cdns_pcie_ep_fn_writeb(struct cdns_pcie *pcie, u8 fn,
					  u32 reg, u8 value)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_EP_FUNC_BASE(fn) + reg;

	cdns_pcie_write_sz(addr, 0x1, value);
}

static inline void cdns_pcie_ep_fn_writew(struct cdns_pcie *pcie, u8 fn,
					  u32 reg, u16 value)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_EP_FUNC_BASE(fn) + reg;

	cdns_pcie_write_sz(addr, 0x2, value);
}

static inline void cdns_pcie_ep_fn_writel(struct cdns_pcie *pcie, u8 fn,
					  u32 reg, u32 value)
{
	writel(value, pcie->reg_base + CDNS_PCIE_EP_FUNC_BASE(fn) + reg);
}

static inline u16 cdns_pcie_ep_fn_readw(struct cdns_pcie *pcie, u8 fn, u32 reg)
{
	void __iomem *addr = pcie->reg_base + CDNS_PCIE_EP_FUNC_BASE(fn) + reg;

	return cdns_pcie_read_sz(addr, 0x2);
}

static inline u32 cdns_pcie_ep_fn_readl(struct cdns_pcie *pcie, u8 fn, u32 reg)
{
	return readl(pcie->reg_base + CDNS_PCIE_EP_FUNC_BASE(fn) + reg);
}

static inline int cdns_pcie_start_link(struct cdns_pcie *pcie)
{
	if (pcie->ops && pcie->ops->start_link)
		return pcie->ops->start_link(pcie);

	return 0;
}

static inline void cdns_pcie_stop_link(struct cdns_pcie *pcie)
{
	if (pcie->ops && pcie->ops->stop_link)
		pcie->ops->stop_link(pcie);
}

static inline bool cdns_pcie_link_up(struct cdns_pcie *pcie)
{
	if (pcie->ops && pcie->ops->link_up)
		return pcie->ops->link_up(pcie);

	return true;
}

#if IS_ENABLED(CONFIG_PCIE_CADENCE_HOST)
int cdns_pcie_host_link_setup(struct cdns_pcie_rc *rc);
int cdns_pcie_host_init(struct cdns_pcie_rc *rc);
int cdns_pcie_host_setup(struct cdns_pcie_rc *rc);
void cdns_pcie_host_disable(struct cdns_pcie_rc *rc);
void __iomem *cdns_pci_map_bus(struct pci_bus *bus, unsigned int devfn,
			       int where);
int cdns_pcie_hpa_host_setup(struct cdns_pcie_rc *rc);
void cdns_pcie_hpa_host_disable(struct cdns_pcie_rc *rc);
#else
static inline int cdns_pcie_host_link_setup(struct cdns_pcie_rc *rc)
{
	return 0;
}

static inline int cdns_pcie_host_init(struct cdns_pcie_rc *rc)
{
	return 0;
}

static inline int cdns_pcie_host_setup(struct cdns_pcie_rc *rc)
{
	return 0;
}

static inline int cdns_pcie_hpa_host_setup(struct cdns_pcie_rc *rc)
{
	return 0;
}

static inline void cdns_pcie_host_disable(struct cdns_pcie_rc *rc)
{
}

static inline void cdns_pcie_hpa_host_disable(struct cdns_pcie_rc *rc)
{
}

static inline void __iomem *cdns_pci_map_bus(struct pci_bus *bus, unsigned int devfn,
					     int where)
{
	return NULL;
}
#endif

#if IS_ENABLED(CONFIG_PCIE_CADENCE_EP)
int cdns_pcie_ep_setup(struct cdns_pcie_ep *ep);
void cdns_pcie_ep_disable(struct cdns_pcie_ep *ep);
int cdns_pcie_hpa_ep_setup(struct cdns_pcie_ep *ep);
#else
static inline int cdns_pcie_ep_setup(struct cdns_pcie_ep *ep)
{
	return 0;
}

static inline void cdns_pcie_ep_disable(struct cdns_pcie_ep *ep)
{
}

static inline int cdns_pcie_hpa_ep_setup(struct cdns_pcie_ep *ep)
{
	return 0;
}

#endif

u8   cdns_pcie_find_capability(struct cdns_pcie *pcie, u8 cap);
u16  cdns_pcie_find_ext_capability(struct cdns_pcie *pcie, u8 cap);
bool cdns_pcie_linkup(struct cdns_pcie *pcie);

void cdns_pcie_detect_quiet_min_delay_set(struct cdns_pcie *pcie);

void cdns_pcie_set_outbound_region(struct cdns_pcie *pcie, u8 busnr, u8 fn,
				   u32 r, bool is_io,
				   u64 cpu_addr, u64 pci_addr, size_t size);

void cdns_pcie_set_outbound_region_for_normal_msg(struct cdns_pcie *pcie,
						  u8 busnr, u8 fn,
						  u32 r, u64 cpu_addr);

void cdns_pcie_reset_outbound_region(struct cdns_pcie *pcie, u32 r);
void cdns_pcie_disable_phy(struct cdns_pcie *pcie);
int  cdns_pcie_enable_phy(struct cdns_pcie *pcie);
int  cdns_pcie_init_phy(struct device *dev, struct cdns_pcie *pcie);
void cdns_pcie_hpa_detect_quiet_min_delay_set(struct cdns_pcie *pcie);
void cdns_pcie_hpa_set_outbound_region(struct cdns_pcie *pcie, u8 busnr, u8 fn,
				       u32 r, bool is_io,
				       u64 cpu_addr, u64 pci_addr, size_t size);
void cdns_pcie_hpa_set_outbound_region_for_normal_msg(struct cdns_pcie *pcie,
						      u8 busnr, u8 fn,
						      u32 r, u64 cpu_addr);
int  cdns_pcie_hpa_host_link_setup(struct cdns_pcie_rc *rc);
void __iomem *cdns_pci_hpa_map_bus(struct pci_bus *bus, unsigned int devfn,
				   int where);
int  cdns_pcie_hpa_host_start_link(struct cdns_pcie_rc *rc);
int  cdns_pcie_hpa_start_link(struct cdns_pcie *pcie);
void cdns_pcie_hpa_stop_link(struct cdns_pcie *pcie);
bool cdns_pcie_hpa_link_up(struct cdns_pcie *pcie);

extern const struct dev_pm_ops cdns_pcie_pm_ops;

#if IS_ENABLED(CONFIG_PCIE_CADENCE_DEBUGFS)
void cdns_pcie_debugfs_deinit(struct cdns_pcie *pci);
void cdns_pcie_debugfs_init(struct cdns_pcie *pci);
#else
static inline void cdns_pcie_debugfs_deinit(struct cdns_pcie *pci)
{
}
static inline void cdns_pcie_debugfs_init(struct cdns_pcie *pci)
{
}
#endif

#endif /* _PCIE_CADENCE_H */
