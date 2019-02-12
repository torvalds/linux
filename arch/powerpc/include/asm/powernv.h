/*
 * Copyright 2017 IBM Corp.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#ifndef _ASM_POWERNV_H
#define _ASM_POWERNV_H

#ifdef CONFIG_PPC_POWERNV
#define NPU2_WRITE 1
extern void powernv_set_nmmu_ptcr(unsigned long ptcr);
extern struct npu_context *pnv_npu2_init_context(struct pci_dev *gpdev,
			unsigned long flags,
			void (*cb)(struct npu_context *, void *),
			void *priv);
extern void pnv_npu2_destroy_context(struct npu_context *context,
				struct pci_dev *gpdev);
extern int pnv_npu2_handle_fault(struct npu_context *context, uintptr_t *ea,
				unsigned long *flags, unsigned long *status,
				int count);

void pnv_program_cpu_hotplug_lpcr(unsigned int cpu, u64 lpcr_val);

void pnv_tm_init(void);
#else
static inline void powernv_set_nmmu_ptcr(unsigned long ptcr) { }
static inline struct npu_context *pnv_npu2_init_context(struct pci_dev *gpdev,
			unsigned long flags,
			struct npu_context *(*cb)(struct npu_context *, void *),
			void *priv) { return ERR_PTR(-ENODEV); }
static inline void pnv_npu2_destroy_context(struct npu_context *context,
					struct pci_dev *gpdev) { }

static inline int pnv_npu2_handle_fault(struct npu_context *context,
					uintptr_t *ea, unsigned long *flags,
					unsigned long *status, int count) {
	return -ENODEV;
}

static inline void pnv_tm_init(void) { }
static inline void pnv_power9_force_smt4(void) { }
#endif

#endif /* _ASM_POWERNV_H */
