/* SPDX-License-Identifier: GPL-2.0 OR MIT */
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#ifndef __LIMA_VM_H__
#define __LIMA_VM_H__

#include <drm/drm_mm.h>
#include <linux/kref.h>

#define LIMA_PAGE_SIZE    4096
#define LIMA_PAGE_MASK    (LIMA_PAGE_SIZE - 1)
#define LIMA_PAGE_ENT_NUM (LIMA_PAGE_SIZE / sizeof(u32))

#define LIMA_VM_NUM_PT_PER_BT_SHIFT 3
#define LIMA_VM_NUM_PT_PER_BT (1 << LIMA_VM_NUM_PT_PER_BT_SHIFT)
#define LIMA_VM_NUM_BT (LIMA_PAGE_ENT_NUM >> LIMA_VM_NUM_PT_PER_BT_SHIFT)

#define LIMA_VA_RESERVE_START  0x0FFF00000ULL
#define LIMA_VA_RESERVE_DLBU   LIMA_VA_RESERVE_START
#define LIMA_VA_RESERVE_END    0x100000000ULL

struct lima_device;

struct lima_vm_page {
	u32 *cpu;
	dma_addr_t dma;
};

struct lima_vm {
	struct mutex lock;
	struct kref refcount;

	struct drm_mm mm;

	struct lima_device *dev;

	struct lima_vm_page pd;
	struct lima_vm_page bts[LIMA_VM_NUM_BT];
};

int lima_vm_bo_add(struct lima_vm *vm, struct lima_bo *bo, bool create);
void lima_vm_bo_del(struct lima_vm *vm, struct lima_bo *bo);

u32 lima_vm_get_va(struct lima_vm *vm, struct lima_bo *bo);

struct lima_vm *lima_vm_create(struct lima_device *dev);
void lima_vm_release(struct kref *kref);

static inline struct lima_vm *lima_vm_get(struct lima_vm *vm)
{
	kref_get(&vm->refcount);
	return vm;
}

static inline void lima_vm_put(struct lima_vm *vm)
{
	kref_put(&vm->refcount, lima_vm_release);
}

void lima_vm_print(struct lima_vm *vm);

#endif
