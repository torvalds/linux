/**********************************************************************
* Author: Cavium, Inc.
*
* Contact: support@cavium.com
*          Please include "LiquidIO" in the subject.
*
* Copyright (c) 2003-2015 Cavium, Inc.
*
* This file is free software; you can redistribute it and/or modify
* it under the terms of the GNU General Public License, Version 2, as
* published by the Free Software Foundation.
*
* This file is distributed in the hope that it will be useful, but
* AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
* of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
* NONINFRINGEMENT.  See the GNU General Public License for more
* details.
*
* This file may also be available under a different license from Cavium.
* Contact Cavium, Inc. for more information
**********************************************************************/

/*! \file  cn66xx_device.h
 *  \brief Host Driver: Routines that perform CN66XX specific operations.
 */

#ifndef __CN66XX_DEVICE_H__
#define  __CN66XX_DEVICE_H__

/* Register address and configuration for a CN6XXX devices.
 * If device specific changes need to be made then add a struct to include
 * device specific fields as shown in the commented section
 */
struct octeon_cn6xxx {
	/** PCI interrupt summary register */
	u8 __iomem *intr_sum_reg64;

	/** PCI interrupt enable register */
	u8 __iomem *intr_enb_reg64;

	/** The PCI interrupt mask used by interrupt handler */
	u64 intr_mask64;

	struct octeon_config *conf;

	/* Example additional fields - not used currently
	 *  struct {
	 *  }cn6xyz;
	 */

	/* For the purpose of atomic access to interrupt enable reg */
	spinlock_t lock_for_droq_int_enb_reg;

};

enum octeon_pcie_mps {
	PCIE_MPS_DEFAULT = -1,	/* Use the default setup by BIOS */
	PCIE_MPS_128B = 0,
	PCIE_MPS_256B = 1
};

enum octeon_pcie_mrrs {
	PCIE_MRRS_DEFAULT = -1,	/* Use the default setup by BIOS */
	PCIE_MRRS_128B = 0,
	PCIE_MRRS_256B = 1,
	PCIE_MRRS_512B = 2,
	PCIE_MRRS_1024B = 3,
	PCIE_MRRS_2048B = 4,
	PCIE_MRRS_4096B = 5
};

/* Common functions for 66xx and 68xx */
int lio_cn6xxx_soft_reset(struct octeon_device *oct);
void lio_cn6xxx_enable_error_reporting(struct octeon_device *oct);
void lio_cn6xxx_setup_pcie_mps(struct octeon_device *oct,
			       enum octeon_pcie_mps mps);
void lio_cn6xxx_setup_pcie_mrrs(struct octeon_device *oct,
				enum octeon_pcie_mrrs mrrs);
void lio_cn6xxx_setup_global_input_regs(struct octeon_device *oct);
void lio_cn6xxx_setup_global_output_regs(struct octeon_device *oct);
void lio_cn6xxx_setup_iq_regs(struct octeon_device *oct, u32 iq_no);
void lio_cn6xxx_setup_oq_regs(struct octeon_device *oct, u32 oq_no);
void lio_cn6xxx_enable_io_queues(struct octeon_device *oct);
void lio_cn6xxx_disable_io_queues(struct octeon_device *oct);
void lio_cn6xxx_process_pcie_error_intr(struct octeon_device *oct, u64 intr64);
int lio_cn6xxx_process_droq_intr_regs(struct octeon_device *oct);
irqreturn_t lio_cn6xxx_process_interrupt_regs(void *dev);
void lio_cn6xxx_reinit_regs(struct octeon_device *oct);
void lio_cn6xxx_bar1_idx_setup(struct octeon_device *oct, u64 core_addr,
			       u32 idx, int valid);
void lio_cn6xxx_bar1_idx_write(struct octeon_device *oct, u32 idx, u32 mask);
u32 lio_cn6xxx_bar1_idx_read(struct octeon_device *oct, u32 idx);
u32
lio_cn6xxx_update_read_index(struct octeon_device *oct __attribute__((unused)),
			     struct octeon_instr_queue *iq);
void lio_cn6xxx_enable_interrupt(void *chip);
void lio_cn6xxx_disable_interrupt(void *chip);
void cn6xxx_get_pcie_qlmport(struct octeon_device *oct);
void lio_cn6xxx_setup_reg_address(struct octeon_device *oct, void *chip,
				  struct octeon_reg_list *reg_list);
u32 lio_cn6xxx_coprocessor_clock(struct octeon_device *oct);
u32 lio_cn6xxx_get_oq_ticks(struct octeon_device *oct, u32 time_intr_in_us);
int lio_setup_cn66xx_octeon_device(struct octeon_device *);
int lio_validate_cn6xxx_config_info(struct octeon_device *oct,
				    struct octeon_config *);

#endif
