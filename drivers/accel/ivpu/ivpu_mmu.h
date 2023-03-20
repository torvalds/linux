/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020-2023 Intel Corporation
 */

#ifndef __IVPU_MMU_H__
#define __IVPU_MMU_H__

struct ivpu_device;

struct ivpu_mmu_cdtab {
	void *base;
	dma_addr_t dma;
};

struct ivpu_mmu_strtab {
	void *base;
	dma_addr_t dma;
	u64 dma_q;
	u32 base_cfg;
};

struct ivpu_mmu_queue {
	void *base;
	dma_addr_t dma;
	u64 dma_q;
	u32 prod;
	u32 cons;
};

struct ivpu_mmu_info {
	struct mutex lock; /* Protects cdtab, strtab, cmdq, on */
	struct ivpu_mmu_cdtab cdtab;
	struct ivpu_mmu_strtab strtab;
	struct ivpu_mmu_queue cmdq;
	struct ivpu_mmu_queue evtq;
	bool on;
};

int ivpu_mmu_init(struct ivpu_device *vdev);
void ivpu_mmu_disable(struct ivpu_device *vdev);
int ivpu_mmu_enable(struct ivpu_device *vdev);
int ivpu_mmu_set_pgtable(struct ivpu_device *vdev, int ssid, struct ivpu_mmu_pgtable *pgtable);
void ivpu_mmu_clear_pgtable(struct ivpu_device *vdev, int ssid);
int ivpu_mmu_invalidate_tlb(struct ivpu_device *vdev, u16 ssid);

void ivpu_mmu_irq_evtq_handler(struct ivpu_device *vdev);
void ivpu_mmu_irq_gerr_handler(struct ivpu_device *vdev);

#endif /* __IVPU_MMU_H__ */
