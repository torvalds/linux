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

#include <core/object.h>
#include <core/subdev.h>
#include <core/device.h>
#include <core/mm.h>

struct nouveau_vm_pgt {
	struct nouveau_gpuobj *obj[2];
	u32 refcount[2];
};

struct nouveau_vm_pgd {
	struct list_head head;
	struct nouveau_gpuobj *obj;
};

struct nouveau_gpuobj;
struct nouveau_mem;

struct nouveau_vma {
	struct list_head head;
	int refcount;
	struct nouveau_vm *vm;
	struct nouveau_mm_node *node;
	u64 offset;
	u32 access;
};

struct nouveau_vm {
	struct nouveau_vmmgr *vmm;
	struct nouveau_mm mm;
	int refcount;

	struct list_head pgd_list;
	atomic_t engref[NVDEV_SUBDEV_NR];

	struct nouveau_vm_pgt *pgt;
	u32 fpde;
	u32 lpde;
};

struct nouveau_vmmgr {
	struct nouveau_subdev base;

	u64 limit;
	u8  dma_bits;
	u32 pgt_bits;
	u8  spg_shift;
	u8  lpg_shift;

	int  (*create)(struct nouveau_vmmgr *, u64 offset, u64 length,
		       u64 mm_offset, struct nouveau_vm **);

	void (*map_pgt)(struct nouveau_gpuobj *pgd, u32 pde,
			struct nouveau_gpuobj *pgt[2]);
	void (*map)(struct nouveau_vma *, struct nouveau_gpuobj *,
		    struct nouveau_mem *, u32 pte, u32 cnt,
		    u64 phys, u64 delta);
	void (*map_sg)(struct nouveau_vma *, struct nouveau_gpuobj *,
		       struct nouveau_mem *, u32 pte, u32 cnt, dma_addr_t *);
	void (*unmap)(struct nouveau_gpuobj *pgt, u32 pte, u32 cnt);
	void (*flush)(struct nouveau_vm *);
};

static inline struct nouveau_vmmgr *
nouveau_vmmgr(void *obj)
{
	return (void *)nv_device(obj)->subdev[NVDEV_SUBDEV_VM];
}

#define nouveau_vmmgr_create(p,e,o,i,f,d)                                      \
	nouveau_subdev_create((p), (e), (o), 0, (i), (f), (d))
#define nouveau_vmmgr_destroy(p)                                               \
	nouveau_subdev_destroy(&(p)->base)
#define nouveau_vmmgr_init(p)                                                  \
	nouveau_subdev_init(&(p)->base)
#define nouveau_vmmgr_fini(p,s)                                                \
	nouveau_subdev_fini(&(p)->base, (s))

#define _nouveau_vmmgr_dtor _nouveau_subdev_dtor
#define _nouveau_vmmgr_init _nouveau_subdev_init
#define _nouveau_vmmgr_fini _nouveau_subdev_fini

extern struct nouveau_oclass nv04_vmmgr_oclass;
extern struct nouveau_oclass nv41_vmmgr_oclass;
extern struct nouveau_oclass nv44_vmmgr_oclass;
extern struct nouveau_oclass nv50_vmmgr_oclass;
extern struct nouveau_oclass nvc0_vmmgr_oclass;

int  nv04_vm_create(struct nouveau_vmmgr *, u64, u64, u64,
		    struct nouveau_vm **);
void nv04_vmmgr_dtor(struct nouveau_object *);

void nv50_vm_flush_engine(struct nouveau_subdev *, int engine);
void nvc0_vm_flush_engine(struct nouveau_subdev *, u64 addr, int type);

/* nouveau_vm.c */
int  nouveau_vm_create(struct nouveau_vmmgr *, u64 offset, u64 length,
		       u64 mm_offset, u32 block, struct nouveau_vm **);
int  nouveau_vm_new(struct nouveau_device *, u64 offset, u64 length,
		    u64 mm_offset, struct nouveau_vm **);
int  nouveau_vm_ref(struct nouveau_vm *, struct nouveau_vm **,
		    struct nouveau_gpuobj *pgd);
int  nouveau_vm_get(struct nouveau_vm *, u64 size, u32 page_shift,
		    u32 access, struct nouveau_vma *);
void nouveau_vm_put(struct nouveau_vma *);
void nouveau_vm_map(struct nouveau_vma *, struct nouveau_mem *);
void nouveau_vm_map_at(struct nouveau_vma *, u64 offset, struct nouveau_mem *);
void nouveau_vm_unmap(struct nouveau_vma *);
void nouveau_vm_unmap_at(struct nouveau_vma *, u64 offset, u64 length);
void nouveau_vm_map_sg(struct nouveau_vma *, u64 offset, u64 length,
		       struct nouveau_mem *);
void nouveau_vm_map_sg_table(struct nouveau_vma *vma, u64 delta, u64 length,
		     struct nouveau_mem *mem);

#endif
