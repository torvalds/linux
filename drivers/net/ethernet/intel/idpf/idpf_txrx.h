/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (C) 2023 Intel Corporation */

#ifndef _IDPF_TXRX_H_
#define _IDPF_TXRX_H_

/* Default vector sharing */
#define IDPF_MBX_Q_VEC		1
#define IDPF_MIN_Q_VEC		1

/**
 * struct idpf_intr_reg
 * @dyn_ctl: Dynamic control interrupt register
 * @dyn_ctl_intena_m: Mask for dyn_ctl interrupt enable
 * @dyn_ctl_itridx_m: Mask for ITR index
 * @icr_ena: Interrupt cause register offset
 * @icr_ena_ctlq_m: Mask for ICR
 */
struct idpf_intr_reg {
	void __iomem *dyn_ctl;
	u32 dyn_ctl_intena_m;
	u32 dyn_ctl_itridx_m;
	void __iomem *icr_ena;
	u32 icr_ena_ctlq_m;
};

/**
 * struct idpf_q_vector
 * @v_idx: Vector index
 * @intr_reg: See struct idpf_intr_reg
 * @name: Queue vector name
 */
struct idpf_q_vector {
	u16 v_idx;
	struct idpf_intr_reg intr_reg;
	char *name;
};
#endif /* !_IDPF_TXRX_H_ */
