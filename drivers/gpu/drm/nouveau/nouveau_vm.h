/*
 * Copyright 2010 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Ben Skeggs
 */

#ifndef __NOUVEAU_VM_H__
#define __NOUVEAU_VM_H__

#include "drmP.h"

#include "nouveau_drv.h"
#include "nouveau_mm.h"

struct nouveau_vm_pgt {
	struct nouveau_gpuobj *obj;
	u32 page_shift;
	u32 refcount;
};

struct nouveau_vm_pgd {
	struct list_head head;
	struct nouveau_gpuobj *obj;
};

struct nouveau_vma {
	struct nouveau_vm *vm;
	struct nouveau_mm_node *node;
	u64 offset;
	u32 access;
};

struct nouveau_vm {
	struct drm_device *dev;
	struct nouveau_mm *mm;
	int refcount;

	struct list_head pgd_list;
	atomic_t pgraph_refs;
	atomic_t pcrypt_refs;

	struct nouveau_vm_pgt *pgt;
	u32 fpde;
	u32 lpde;

	u32 pgt_bits;
	u8  spg_shift;
	u8  lpg_shift;

	void (*map_pgt)(struct nouveau_gpuobj *pgd, u32 type, u32 pde,
			struct nouveau_gpuobj *pgt);
	void (*unmap_pgt)(struct nouveau_gpuobj *pgd, u32 pde);
	void (*map)(struct nouveau_vma *, struct nouveau_gpuobj *,
		    struct nouveau_vram *, u32 pte, u32 cnt, u64 phys);
	void (*map_sg)(struct nouveau_vma *, struct nouveau_gpuobj *,
		       u32 pte, dma_addr_t *, u32 cnt);
	void (*unmap)(struct nouveau_gpuobj *pgt, u32 pte, u32 cnt);
	void (*flush)(struct nouveau_vm *);
};

/* nouveau_vm.c */
int  nouveau_vm_new(struct drm_device *, u64 offset, u64 length, u64 mm_offset,
		    u8 pgt_bits, u8 spg_shift, u8 lpg_shift,
		    struct nouveau_vm **);
int  nouveau_vm_ref(struct nouveau_vm *, struct nouveau_vm **,
		    struct nouveau_gpuobj *pgd);
int  nouveau_vm_get(struct nouveau_vm *, u64 size, u32 page_shift,
		    u32 access, struct nouveau_vma *);
void nouveau_vm_put(struct nouveau_vma *);
void nouveau_vm_map(struct nouveau_vma *, struct nouveau_vram *);
void nouveau_vm_map_at(struct nouveau_vma *, u64 offset, struct nouveau_vram *);
void nouveau_vm_unmap(struct nouveau_vma *);
void nouveau_vm_unmap_at(struct nouveau_vma *, u64 offset, u64 length);
void nouveau_vm_map_sg(struct nouveau_vma *, u64 offset, u64 length,
		       dma_addr_t *);

/* nv50_vm.c */
void nv50_vm_map_pgt(struct nouveau_gpuobj *pgd, u32 type, u32 pde,
		     struct nouveau_gpuobj *pgt);
void nv50_vm_unmap_pgt(struct nouveau_gpuobj *pgd, u32 pde);
void nv50_vm_map(struct nouveau_vma *, struct nouveau_gpuobj *,
		 struct nouveau_vram *, u32 pte, u32 cnt, u64 phys);
void nv50_vm_map_sg(struct nouveau_vma *, struct nouveau_gpuobj *,
		    u32 pte, dma_addr_t *, u32 cnt);
void nv50_vm_unmap(struct nouveau_gpuobj *, u32 pte, u32 cnt);
void nv50_vm_flush(struct nouveau_vm *);
void nv50_vm_flush_engine(struct drm_device *, int engine);

#endif
