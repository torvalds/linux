/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2024 Loongson Technology Corporation Limited
 */

#ifndef __ASM_KVM_IPI_H
#define __ASM_KVM_IPI_H

#include <kvm/iodev.h>

#define LARCH_INT_IPI			12

struct loongarch_ipi {
	spinlock_t lock;
	struct kvm *kvm;
	struct kvm_io_device device;
};

struct ipi_state {
	spinlock_t lock;
	uint32_t status;
	uint32_t en;
	uint32_t set;
	uint32_t clear;
	uint64_t buf[4];
};

#define IOCSR_IPI_BASE		0x1000
#define IOCSR_IPI_SIZE		0x160

int kvm_loongarch_register_ipi_device(void);

#endif
