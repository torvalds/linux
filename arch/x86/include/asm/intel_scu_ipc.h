/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_X86_INTEL_SCU_IPC_H_
#define  _ASM_X86_INTEL_SCU_IPC_H_

#include <linux/ioport.h>

struct device;
struct intel_scu_ipc_dev;

/**
 * struct intel_scu_ipc_data - Data used to configure SCU IPC
 * @mem: Base address of SCU IPC MMIO registers
 * @irq: The IRQ number used for SCU (optional)
 */
struct intel_scu_ipc_data {
	struct resource mem;
	int irq;
};

struct intel_scu_ipc_dev *
intel_scu_ipc_register(struct device *parent,
		       const struct intel_scu_ipc_data *scu_data);

#include <asm/intel_scu_ipc_legacy.h>

#endif
