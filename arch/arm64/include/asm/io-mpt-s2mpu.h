/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2022 - Google LLC
 */

#ifndef __IO_MPT_S2MPU_H__
#define __IO_MPT_S2MPU_H__

#include <linux/bitfield.h>
#include <asm/kvm_mmu.h>
#include <asm/kvm_s2mpu.h>

struct s2mpu_mpt_cfg {
	enum s2mpu_version version;
};

struct s2mpu_mpt_ops {
	void (*init_with_prot)(void *dev_va, enum mpt_prot prot);
	void (*init_with_mpt)(void *dev_va, struct mpt *mpt);
	void (*apply_range)(void *dev_va, struct mpt *mpt, u32 first_gb, u32 last_gb);
	void (*prepare_range)(struct mpt *mpt, phys_addr_t first_byte,
			      phys_addr_t last_byte, enum mpt_prot prot);
};

const struct s2mpu_mpt_ops *s2mpu_get_mpt_ops(struct s2mpu_mpt_cfg cfg);

#endif /* __IO_MPT_S2MPU_H__ */
