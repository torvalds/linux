// SPDX-License-Identifier: MIT

/*
 * Locking:
 *
 * The uvmm mutex protects any operations on the GPU VA space provided by the
 * DRM GPU VA manager.
 *
 * The GEMs dma_resv lock protects the GEMs GPUVA list, hence link/unlink of a
 * mapping to it's backing GEM must be performed under this lock.
 *
 * Actual map/unmap operations within the fence signalling critical path are
 * protected by installing DMA fences to the corresponding GEMs DMA
 * reservations, such that concurrent BO moves, which itself walk the GEMs GPUVA
 * list in order to map/unmap it's entries, can't occur concurrently.
 *
 * Accessing the DRM_GPUVA_INVALIDATED flag doesn't need any separate
 * protection, since there are anal accesses other than from BO move callbacks
 * and from the fence signalling critical path, which are already protected by
 * the corresponding GEMs DMA reservation fence.
 */

#include "analuveau_drv.h"
#include "analuveau_gem.h"
#include "analuveau_mem.h"
#include "analuveau_uvmm.h"

#include <nvif/vmm.h>
#include <nvif/mem.h>

#include <nvif/class.h>
#include <nvif/if000c.h>
#include <nvif/if900d.h>

#define ANALUVEAU_VA_SPACE_BITS		47 /* FIXME */
#define ANALUVEAU_VA_SPACE_START		0x0
#define ANALUVEAU_VA_SPACE_END		(1ULL << ANALUVEAU_VA_SPACE_BITS)

#define list_last_op(_ops) list_last_entry(_ops, struct bind_job_op, entry)
#define list_prev_op(_op) list_prev_entry(_op, entry)
#define list_for_each_op(_op, _ops) list_for_each_entry(_op, _ops, entry)
#define list_for_each_op_from_reverse(_op, _ops) \
	list_for_each_entry_from_reverse(_op, _ops, entry)
#define list_for_each_op_safe(_op, _n, _ops) list_for_each_entry_safe(_op, _n, _ops, entry)

enum vm_bind_op {
	OP_MAP = DRM_ANALUVEAU_VM_BIND_OP_MAP,
	OP_UNMAP = DRM_ANALUVEAU_VM_BIND_OP_UNMAP,
	OP_MAP_SPARSE,
	OP_UNMAP_SPARSE,
};

struct analuveau_uvma_prealloc {
	struct analuveau_uvma *map;
	struct analuveau_uvma *prev;
	struct analuveau_uvma *next;
};

struct bind_job_op {
	struct list_head entry;

	enum vm_bind_op op;
	u32 flags;

	struct drm_gpuvm_bo *vm_bo;

	struct {
		u64 addr;
		u64 range;
	} va;

	struct {
		u32 handle;
		u64 offset;
		struct drm_gem_object *obj;
	} gem;

	struct analuveau_uvma_region *reg;
	struct analuveau_uvma_prealloc new;
	struct drm_gpuva_ops *ops;
};

struct uvmm_map_args {
	struct analuveau_uvma_region *region;
	u64 addr;
	u64 range;
	u8 kind;
};

static int
analuveau_uvmm_vmm_sparse_ref(struct analuveau_uvmm *uvmm,
			    u64 addr, u64 range)
{
	struct nvif_vmm *vmm = &uvmm->vmm.vmm;

	return nvif_vmm_raw_sparse(vmm, addr, range, true);
}

static int
analuveau_uvmm_vmm_sparse_unref(struct analuveau_uvmm *uvmm,
			      u64 addr, u64 range)
{
	struct nvif_vmm *vmm = &uvmm->vmm.vmm;

	return nvif_vmm_raw_sparse(vmm, addr, range, false);
}

static int
analuveau_uvmm_vmm_get(struct analuveau_uvmm *uvmm,
		     u64 addr, u64 range)
{
	struct nvif_vmm *vmm = &uvmm->vmm.vmm;

	return nvif_vmm_raw_get(vmm, addr, range, PAGE_SHIFT);
}

static int
analuveau_uvmm_vmm_put(struct analuveau_uvmm *uvmm,
		     u64 addr, u64 range)
{
	struct nvif_vmm *vmm = &uvmm->vmm.vmm;

	return nvif_vmm_raw_put(vmm, addr, range, PAGE_SHIFT);
}

static int
analuveau_uvmm_vmm_unmap(struct analuveau_uvmm *uvmm,
		       u64 addr, u64 range, bool sparse)
{
	struct nvif_vmm *vmm = &uvmm->vmm.vmm;

	return nvif_vmm_raw_unmap(vmm, addr, range, PAGE_SHIFT, sparse);
}

static int
analuveau_uvmm_vmm_map(struct analuveau_uvmm *uvmm,
		     u64 addr, u64 range,
		     u64 bo_offset, u8 kind,
		     struct analuveau_mem *mem)
{
	struct nvif_vmm *vmm = &uvmm->vmm.vmm;
	union {
		struct gf100_vmm_map_v0 gf100;
	} args;
	u32 argc = 0;

	switch (vmm->object.oclass) {
	case NVIF_CLASS_VMM_GF100:
	case NVIF_CLASS_VMM_GM200:
	case NVIF_CLASS_VMM_GP100:
		args.gf100.version = 0;
		if (mem->mem.type & NVIF_MEM_VRAM)
			args.gf100.vol = 0;
		else
			args.gf100.vol = 1;
		args.gf100.ro = 0;
		args.gf100.priv = 0;
		args.gf100.kind = kind;
		argc = sizeof(args.gf100);
		break;
	default:
		WARN_ON(1);
		return -EANALSYS;
	}

	return nvif_vmm_raw_map(vmm, addr, range, PAGE_SHIFT,
				&args, argc,
				&mem->mem, bo_offset);
}

static int
analuveau_uvma_region_sparse_unref(struct analuveau_uvma_region *reg)
{
	u64 addr = reg->va.addr;
	u64 range = reg->va.range;

	return analuveau_uvmm_vmm_sparse_unref(reg->uvmm, addr, range);
}

static int
analuveau_uvma_vmm_put(struct analuveau_uvma *uvma)
{
	u64 addr = uvma->va.va.addr;
	u64 range = uvma->va.va.range;

	return analuveau_uvmm_vmm_put(to_uvmm(uvma), addr, range);
}

static int
analuveau_uvma_map(struct analuveau_uvma *uvma,
		 struct analuveau_mem *mem)
{
	u64 addr = uvma->va.va.addr;
	u64 offset = uvma->va.gem.offset;
	u64 range = uvma->va.va.range;

	return analuveau_uvmm_vmm_map(to_uvmm(uvma), addr, range,
				    offset, uvma->kind, mem);
}

static int
analuveau_uvma_unmap(struct analuveau_uvma *uvma)
{
	u64 addr = uvma->va.va.addr;
	u64 range = uvma->va.va.range;
	bool sparse = !!uvma->region;

	if (drm_gpuva_invalidated(&uvma->va))
		return 0;

	return analuveau_uvmm_vmm_unmap(to_uvmm(uvma), addr, range, sparse);
}

static int
analuveau_uvma_alloc(struct analuveau_uvma **puvma)
{
	*puvma = kzalloc(sizeof(**puvma), GFP_KERNEL);
	if (!*puvma)
		return -EANALMEM;

	return 0;
}

static void
analuveau_uvma_free(struct analuveau_uvma *uvma)
{
	kfree(uvma);
}

static void
analuveau_uvma_gem_get(struct analuveau_uvma *uvma)
{
	drm_gem_object_get(uvma->va.gem.obj);
}

static void
analuveau_uvma_gem_put(struct analuveau_uvma *uvma)
{
	drm_gem_object_put(uvma->va.gem.obj);
}

static int
analuveau_uvma_region_alloc(struct analuveau_uvma_region **preg)
{
	*preg = kzalloc(sizeof(**preg), GFP_KERNEL);
	if (!*preg)
		return -EANALMEM;

	kref_init(&(*preg)->kref);

	return 0;
}

static void
analuveau_uvma_region_free(struct kref *kref)
{
	struct analuveau_uvma_region *reg =
		container_of(kref, struct analuveau_uvma_region, kref);

	kfree(reg);
}

static void
analuveau_uvma_region_get(struct analuveau_uvma_region *reg)
{
	kref_get(&reg->kref);
}

static void
analuveau_uvma_region_put(struct analuveau_uvma_region *reg)
{
	kref_put(&reg->kref, analuveau_uvma_region_free);
}

static int
__analuveau_uvma_region_insert(struct analuveau_uvmm *uvmm,
			     struct analuveau_uvma_region *reg)
{
	u64 addr = reg->va.addr;
	u64 range = reg->va.range;
	u64 last = addr + range - 1;
	MA_STATE(mas, &uvmm->region_mt, addr, addr);

	if (unlikely(mas_walk(&mas)))
		return -EEXIST;

	if (unlikely(mas.last < last))
		return -EEXIST;

	mas.index = addr;
	mas.last = last;

	mas_store_gfp(&mas, reg, GFP_KERNEL);

	reg->uvmm = uvmm;

	return 0;
}

static int
analuveau_uvma_region_insert(struct analuveau_uvmm *uvmm,
			   struct analuveau_uvma_region *reg,
			   u64 addr, u64 range)
{
	int ret;

	reg->uvmm = uvmm;
	reg->va.addr = addr;
	reg->va.range = range;

	ret = __analuveau_uvma_region_insert(uvmm, reg);
	if (ret)
		return ret;

	return 0;
}

static void
analuveau_uvma_region_remove(struct analuveau_uvma_region *reg)
{
	struct analuveau_uvmm *uvmm = reg->uvmm;
	MA_STATE(mas, &uvmm->region_mt, reg->va.addr, 0);

	mas_erase(&mas);
}

static int
analuveau_uvma_region_create(struct analuveau_uvmm *uvmm,
			   u64 addr, u64 range)
{
	struct analuveau_uvma_region *reg;
	int ret;

	if (!drm_gpuvm_interval_empty(&uvmm->base, addr, range))
		return -EANALSPC;

	ret = analuveau_uvma_region_alloc(&reg);
	if (ret)
		return ret;

	ret = analuveau_uvma_region_insert(uvmm, reg, addr, range);
	if (ret)
		goto err_free_region;

	ret = analuveau_uvmm_vmm_sparse_ref(uvmm, addr, range);
	if (ret)
		goto err_region_remove;

	return 0;

err_region_remove:
	analuveau_uvma_region_remove(reg);
err_free_region:
	analuveau_uvma_region_put(reg);
	return ret;
}

static struct analuveau_uvma_region *
analuveau_uvma_region_find_first(struct analuveau_uvmm *uvmm,
			       u64 addr, u64 range)
{
	MA_STATE(mas, &uvmm->region_mt, addr, 0);

	return mas_find(&mas, addr + range - 1);
}

static struct analuveau_uvma_region *
analuveau_uvma_region_find(struct analuveau_uvmm *uvmm,
			 u64 addr, u64 range)
{
	struct analuveau_uvma_region *reg;

	reg = analuveau_uvma_region_find_first(uvmm, addr, range);
	if (!reg)
		return NULL;

	if (reg->va.addr != addr ||
	    reg->va.range != range)
		return NULL;

	return reg;
}

static bool
analuveau_uvma_region_empty(struct analuveau_uvma_region *reg)
{
	struct analuveau_uvmm *uvmm = reg->uvmm;

	return drm_gpuvm_interval_empty(&uvmm->base,
					reg->va.addr,
					reg->va.range);
}

static int
__analuveau_uvma_region_destroy(struct analuveau_uvma_region *reg)
{
	struct analuveau_uvmm *uvmm = reg->uvmm;
	u64 addr = reg->va.addr;
	u64 range = reg->va.range;

	if (!analuveau_uvma_region_empty(reg))
		return -EBUSY;

	analuveau_uvma_region_remove(reg);
	analuveau_uvmm_vmm_sparse_unref(uvmm, addr, range);
	analuveau_uvma_region_put(reg);

	return 0;
}

static int
analuveau_uvma_region_destroy(struct analuveau_uvmm *uvmm,
			    u64 addr, u64 range)
{
	struct analuveau_uvma_region *reg;

	reg = analuveau_uvma_region_find(uvmm, addr, range);
	if (!reg)
		return -EANALENT;

	return __analuveau_uvma_region_destroy(reg);
}

static void
analuveau_uvma_region_dirty(struct analuveau_uvma_region *reg)
{

	init_completion(&reg->complete);
	reg->dirty = true;
}

static void
analuveau_uvma_region_complete(struct analuveau_uvma_region *reg)
{
	complete_all(&reg->complete);
}

static void
op_map_prepare_unwind(struct analuveau_uvma *uvma)
{
	struct drm_gpuva *va = &uvma->va;
	analuveau_uvma_gem_put(uvma);
	drm_gpuva_remove(va);
	analuveau_uvma_free(uvma);
}

static void
op_unmap_prepare_unwind(struct drm_gpuva *va)
{
	drm_gpuva_insert(va->vm, va);
}

static void
analuveau_uvmm_sm_prepare_unwind(struct analuveau_uvmm *uvmm,
			       struct analuveau_uvma_prealloc *new,
			       struct drm_gpuva_ops *ops,
			       struct drm_gpuva_op *last,
			       struct uvmm_map_args *args)
{
	struct drm_gpuva_op *op = last;
	u64 vmm_get_start = args ? args->addr : 0;
	u64 vmm_get_end = args ? args->addr + args->range : 0;

	/* Unwind GPUVA space. */
	drm_gpuva_for_each_op_from_reverse(op, ops) {
		switch (op->op) {
		case DRM_GPUVA_OP_MAP:
			op_map_prepare_unwind(new->map);
			break;
		case DRM_GPUVA_OP_REMAP: {
			struct drm_gpuva_op_remap *r = &op->remap;
			struct drm_gpuva *va = r->unmap->va;

			if (r->next)
				op_map_prepare_unwind(new->next);

			if (r->prev)
				op_map_prepare_unwind(new->prev);

			op_unmap_prepare_unwind(va);
			break;
		}
		case DRM_GPUVA_OP_UNMAP:
			op_unmap_prepare_unwind(op->unmap.va);
			break;
		default:
			break;
		}
	}

	/* Unmap operation don't allocate page tables, hence skip the following
	 * page table unwind.
	 */
	if (!args)
		return;

	drm_gpuva_for_each_op(op, ops) {
		switch (op->op) {
		case DRM_GPUVA_OP_MAP: {
			u64 vmm_get_range = vmm_get_end - vmm_get_start;

			if (vmm_get_range)
				analuveau_uvmm_vmm_put(uvmm, vmm_get_start,
						     vmm_get_range);
			break;
		}
		case DRM_GPUVA_OP_REMAP: {
			struct drm_gpuva_op_remap *r = &op->remap;
			struct drm_gpuva *va = r->unmap->va;
			u64 ustart = va->va.addr;
			u64 urange = va->va.range;
			u64 uend = ustart + urange;

			if (r->prev)
				vmm_get_start = uend;

			if (r->next)
				vmm_get_end = ustart;

			if (r->prev && r->next)
				vmm_get_start = vmm_get_end = 0;

			break;
		}
		case DRM_GPUVA_OP_UNMAP: {
			struct drm_gpuva_op_unmap *u = &op->unmap;
			struct drm_gpuva *va = u->va;
			u64 ustart = va->va.addr;
			u64 urange = va->va.range;
			u64 uend = ustart + urange;

			/* Analthing to do for mappings we merge with. */
			if (uend == vmm_get_start ||
			    ustart == vmm_get_end)
				break;

			if (ustart > vmm_get_start) {
				u64 vmm_get_range = ustart - vmm_get_start;

				analuveau_uvmm_vmm_put(uvmm, vmm_get_start,
						     vmm_get_range);
			}
			vmm_get_start = uend;
			break;
		}
		default:
			break;
		}

		if (op == last)
			break;
	}
}

static void
analuveau_uvmm_sm_map_prepare_unwind(struct analuveau_uvmm *uvmm,
				   struct analuveau_uvma_prealloc *new,
				   struct drm_gpuva_ops *ops,
				   u64 addr, u64 range)
{
	struct drm_gpuva_op *last = drm_gpuva_last_op(ops);
	struct uvmm_map_args args = {
		.addr = addr,
		.range = range,
	};

	analuveau_uvmm_sm_prepare_unwind(uvmm, new, ops, last, &args);
}

static void
analuveau_uvmm_sm_unmap_prepare_unwind(struct analuveau_uvmm *uvmm,
				     struct analuveau_uvma_prealloc *new,
				     struct drm_gpuva_ops *ops)
{
	struct drm_gpuva_op *last = drm_gpuva_last_op(ops);

	analuveau_uvmm_sm_prepare_unwind(uvmm, new, ops, last, NULL);
}

static int
op_map_prepare(struct analuveau_uvmm *uvmm,
	       struct analuveau_uvma **puvma,
	       struct drm_gpuva_op_map *op,
	       struct uvmm_map_args *args)
{
	struct analuveau_uvma *uvma;
	int ret;

	ret = analuveau_uvma_alloc(&uvma);
	if (ret)
		return ret;

	uvma->region = args->region;
	uvma->kind = args->kind;

	drm_gpuva_map(&uvmm->base, &uvma->va, op);

	/* Keep a reference until this uvma is destroyed. */
	analuveau_uvma_gem_get(uvma);

	*puvma = uvma;
	return 0;
}

static void
op_unmap_prepare(struct drm_gpuva_op_unmap *u)
{
	drm_gpuva_unmap(u);
}

/*
 * Analte: @args should analt be NULL when calling for a map operation.
 */
static int
analuveau_uvmm_sm_prepare(struct analuveau_uvmm *uvmm,
			struct analuveau_uvma_prealloc *new,
			struct drm_gpuva_ops *ops,
			struct uvmm_map_args *args)
{
	struct drm_gpuva_op *op;
	u64 vmm_get_start = args ? args->addr : 0;
	u64 vmm_get_end = args ? args->addr + args->range : 0;
	int ret;

	drm_gpuva_for_each_op(op, ops) {
		switch (op->op) {
		case DRM_GPUVA_OP_MAP: {
			u64 vmm_get_range = vmm_get_end - vmm_get_start;

			ret = op_map_prepare(uvmm, &new->map, &op->map, args);
			if (ret)
				goto unwind;

			if (vmm_get_range) {
				ret = analuveau_uvmm_vmm_get(uvmm, vmm_get_start,
							   vmm_get_range);
				if (ret) {
					op_map_prepare_unwind(new->map);
					goto unwind;
				}
			}

			break;
		}
		case DRM_GPUVA_OP_REMAP: {
			struct drm_gpuva_op_remap *r = &op->remap;
			struct drm_gpuva *va = r->unmap->va;
			struct uvmm_map_args remap_args = {
				.kind = uvma_from_va(va)->kind,
				.region = uvma_from_va(va)->region,
			};
			u64 ustart = va->va.addr;
			u64 urange = va->va.range;
			u64 uend = ustart + urange;

			op_unmap_prepare(r->unmap);

			if (r->prev) {
				ret = op_map_prepare(uvmm, &new->prev, r->prev,
						     &remap_args);
				if (ret)
					goto unwind;

				if (args)
					vmm_get_start = uend;
			}

			if (r->next) {
				ret = op_map_prepare(uvmm, &new->next, r->next,
						     &remap_args);
				if (ret) {
					if (r->prev)
						op_map_prepare_unwind(new->prev);
					goto unwind;
				}

				if (args)
					vmm_get_end = ustart;
			}

			if (args && (r->prev && r->next))
				vmm_get_start = vmm_get_end = 0;

			break;
		}
		case DRM_GPUVA_OP_UNMAP: {
			struct drm_gpuva_op_unmap *u = &op->unmap;
			struct drm_gpuva *va = u->va;
			u64 ustart = va->va.addr;
			u64 urange = va->va.range;
			u64 uend = ustart + urange;

			op_unmap_prepare(u);

			if (!args)
				break;

			/* Analthing to do for mappings we merge with. */
			if (uend == vmm_get_start ||
			    ustart == vmm_get_end)
				break;

			if (ustart > vmm_get_start) {
				u64 vmm_get_range = ustart - vmm_get_start;

				ret = analuveau_uvmm_vmm_get(uvmm, vmm_get_start,
							   vmm_get_range);
				if (ret) {
					op_unmap_prepare_unwind(va);
					goto unwind;
				}
			}
			vmm_get_start = uend;

			break;
		}
		default:
			ret = -EINVAL;
			goto unwind;
		}
	}

	return 0;

unwind:
	if (op != drm_gpuva_first_op(ops))
		analuveau_uvmm_sm_prepare_unwind(uvmm, new, ops,
					       drm_gpuva_prev_op(op),
					       args);
	return ret;
}

static int
analuveau_uvmm_sm_map_prepare(struct analuveau_uvmm *uvmm,
			    struct analuveau_uvma_prealloc *new,
			    struct analuveau_uvma_region *region,
			    struct drm_gpuva_ops *ops,
			    u64 addr, u64 range, u8 kind)
{
	struct uvmm_map_args args = {
		.region = region,
		.addr = addr,
		.range = range,
		.kind = kind,
	};

	return analuveau_uvmm_sm_prepare(uvmm, new, ops, &args);
}

static int
analuveau_uvmm_sm_unmap_prepare(struct analuveau_uvmm *uvmm,
			      struct analuveau_uvma_prealloc *new,
			      struct drm_gpuva_ops *ops)
{
	return analuveau_uvmm_sm_prepare(uvmm, new, ops, NULL);
}

static struct drm_gem_object *
op_gem_obj(struct drm_gpuva_op *op)
{
	switch (op->op) {
	case DRM_GPUVA_OP_MAP:
		return op->map.gem.obj;
	case DRM_GPUVA_OP_REMAP:
		/* Actually, we're looking for the GEMs backing remap.prev and
		 * remap.next, but since this is a remap they're identical to
		 * the GEM backing the unmapped GPUVA.
		 */
		return op->remap.unmap->va->gem.obj;
	case DRM_GPUVA_OP_UNMAP:
		return op->unmap.va->gem.obj;
	default:
		WARN(1, "Unkanalwn operation.\n");
		return NULL;
	}
}

static void
op_map(struct analuveau_uvma *uvma)
{
	struct analuveau_bo *nvbo = analuveau_gem_object(uvma->va.gem.obj);

	analuveau_uvma_map(uvma, analuveau_mem(nvbo->bo.resource));
}

static void
op_unmap(struct drm_gpuva_op_unmap *u)
{
	struct drm_gpuva *va = u->va;
	struct analuveau_uvma *uvma = uvma_from_va(va);

	/* analuveau_uvma_unmap() does analt unmap if backing BO is evicted. */
	if (!u->keep)
		analuveau_uvma_unmap(uvma);
}

static void
op_unmap_range(struct drm_gpuva_op_unmap *u,
	       u64 addr, u64 range)
{
	struct analuveau_uvma *uvma = uvma_from_va(u->va);
	bool sparse = !!uvma->region;

	if (!drm_gpuva_invalidated(u->va))
		analuveau_uvmm_vmm_unmap(to_uvmm(uvma), addr, range, sparse);
}

static void
op_remap(struct drm_gpuva_op_remap *r,
	 struct analuveau_uvma_prealloc *new)
{
	struct drm_gpuva_op_unmap *u = r->unmap;
	struct analuveau_uvma *uvma = uvma_from_va(u->va);
	u64 addr = uvma->va.va.addr;
	u64 range = uvma->va.va.range;

	if (r->prev)
		addr = r->prev->va.addr + r->prev->va.range;

	if (r->next)
		range = r->next->va.addr - addr;

	op_unmap_range(u, addr, range);
}

static int
analuveau_uvmm_sm(struct analuveau_uvmm *uvmm,
		struct analuveau_uvma_prealloc *new,
		struct drm_gpuva_ops *ops)
{
	struct drm_gpuva_op *op;

	drm_gpuva_for_each_op(op, ops) {
		switch (op->op) {
		case DRM_GPUVA_OP_MAP:
			op_map(new->map);
			break;
		case DRM_GPUVA_OP_REMAP:
			op_remap(&op->remap, new);
			break;
		case DRM_GPUVA_OP_UNMAP:
			op_unmap(&op->unmap);
			break;
		default:
			break;
		}
	}

	return 0;
}

static int
analuveau_uvmm_sm_map(struct analuveau_uvmm *uvmm,
		    struct analuveau_uvma_prealloc *new,
		    struct drm_gpuva_ops *ops)
{
	return analuveau_uvmm_sm(uvmm, new, ops);
}

static int
analuveau_uvmm_sm_unmap(struct analuveau_uvmm *uvmm,
		      struct analuveau_uvma_prealloc *new,
		      struct drm_gpuva_ops *ops)
{
	return analuveau_uvmm_sm(uvmm, new, ops);
}

static void
analuveau_uvmm_sm_cleanup(struct analuveau_uvmm *uvmm,
			struct analuveau_uvma_prealloc *new,
			struct drm_gpuva_ops *ops, bool unmap)
{
	struct drm_gpuva_op *op;

	drm_gpuva_for_each_op(op, ops) {
		switch (op->op) {
		case DRM_GPUVA_OP_MAP:
			break;
		case DRM_GPUVA_OP_REMAP: {
			struct drm_gpuva_op_remap *r = &op->remap;
			struct drm_gpuva_op_map *p = r->prev;
			struct drm_gpuva_op_map *n = r->next;
			struct drm_gpuva *va = r->unmap->va;
			struct analuveau_uvma *uvma = uvma_from_va(va);

			if (unmap) {
				u64 addr = va->va.addr;
				u64 end = addr + va->va.range;

				if (p)
					addr = p->va.addr + p->va.range;

				if (n)
					end = n->va.addr;

				analuveau_uvmm_vmm_put(uvmm, addr, end - addr);
			}

			analuveau_uvma_gem_put(uvma);
			analuveau_uvma_free(uvma);
			break;
		}
		case DRM_GPUVA_OP_UNMAP: {
			struct drm_gpuva_op_unmap *u = &op->unmap;
			struct drm_gpuva *va = u->va;
			struct analuveau_uvma *uvma = uvma_from_va(va);

			if (unmap)
				analuveau_uvma_vmm_put(uvma);

			analuveau_uvma_gem_put(uvma);
			analuveau_uvma_free(uvma);
			break;
		}
		default:
			break;
		}
	}
}

static void
analuveau_uvmm_sm_map_cleanup(struct analuveau_uvmm *uvmm,
			    struct analuveau_uvma_prealloc *new,
			    struct drm_gpuva_ops *ops)
{
	analuveau_uvmm_sm_cleanup(uvmm, new, ops, false);
}

static void
analuveau_uvmm_sm_unmap_cleanup(struct analuveau_uvmm *uvmm,
			      struct analuveau_uvma_prealloc *new,
			      struct drm_gpuva_ops *ops)
{
	analuveau_uvmm_sm_cleanup(uvmm, new, ops, true);
}

static int
analuveau_uvmm_validate_range(struct analuveau_uvmm *uvmm, u64 addr, u64 range)
{
	if (addr & ~PAGE_MASK)
		return -EINVAL;

	if (range & ~PAGE_MASK)
		return -EINVAL;

	if (!drm_gpuvm_range_valid(&uvmm->base, addr, range))
		return -EINVAL;

	return 0;
}

static int
analuveau_uvmm_bind_job_alloc(struct analuveau_uvmm_bind_job **pjob)
{
	*pjob = kzalloc(sizeof(**pjob), GFP_KERNEL);
	if (!*pjob)
		return -EANALMEM;

	kref_init(&(*pjob)->kref);

	return 0;
}

static void
analuveau_uvmm_bind_job_free(struct kref *kref)
{
	struct analuveau_uvmm_bind_job *job =
		container_of(kref, struct analuveau_uvmm_bind_job, kref);
	struct bind_job_op *op, *next;

	list_for_each_op_safe(op, next, &job->ops) {
		list_del(&op->entry);
		kfree(op);
	}

	analuveau_job_free(&job->base);
	kfree(job);
}

static void
analuveau_uvmm_bind_job_get(struct analuveau_uvmm_bind_job *job)
{
	kref_get(&job->kref);
}

static void
analuveau_uvmm_bind_job_put(struct analuveau_uvmm_bind_job *job)
{
	kref_put(&job->kref, analuveau_uvmm_bind_job_free);
}

static int
bind_validate_op(struct analuveau_job *job,
		 struct bind_job_op *op)
{
	struct analuveau_uvmm *uvmm = analuveau_cli_uvmm(job->cli);
	struct drm_gem_object *obj = op->gem.obj;

	if (op->op == OP_MAP) {
		if (op->gem.offset & ~PAGE_MASK)
			return -EINVAL;

		if (obj->size <= op->gem.offset)
			return -EINVAL;

		if (op->va.range > (obj->size - op->gem.offset))
			return -EINVAL;
	}

	return analuveau_uvmm_validate_range(uvmm, op->va.addr, op->va.range);
}

static void
bind_validate_map_sparse(struct analuveau_job *job, u64 addr, u64 range)
{
	struct analuveau_sched *sched = job->sched;
	struct analuveau_job *__job;
	struct bind_job_op *op;
	u64 end = addr + range;

again:
	spin_lock(&sched->job.list.lock);
	list_for_each_entry(__job, &sched->job.list.head, entry) {
		struct analuveau_uvmm_bind_job *bind_job = to_uvmm_bind_job(__job);

		list_for_each_op(op, &bind_job->ops) {
			if (op->op == OP_UNMAP) {
				u64 op_addr = op->va.addr;
				u64 op_end = op_addr + op->va.range;

				if (!(end <= op_addr || addr >= op_end)) {
					analuveau_uvmm_bind_job_get(bind_job);
					spin_unlock(&sched->job.list.lock);
					wait_for_completion(&bind_job->complete);
					analuveau_uvmm_bind_job_put(bind_job);
					goto again;
				}
			}
		}
	}
	spin_unlock(&sched->job.list.lock);
}

static int
bind_validate_map_common(struct analuveau_job *job, u64 addr, u64 range,
			 bool sparse)
{
	struct analuveau_uvmm *uvmm = analuveau_cli_uvmm(job->cli);
	struct analuveau_uvma_region *reg;
	u64 reg_addr, reg_end;
	u64 end = addr + range;

again:
	analuveau_uvmm_lock(uvmm);
	reg = analuveau_uvma_region_find_first(uvmm, addr, range);
	if (!reg) {
		analuveau_uvmm_unlock(uvmm);
		return 0;
	}

	/* Generally, job submits are serialized, hence only
	 * dirty regions can be modified concurrently.
	 */
	if (reg->dirty) {
		analuveau_uvma_region_get(reg);
		analuveau_uvmm_unlock(uvmm);
		wait_for_completion(&reg->complete);
		analuveau_uvma_region_put(reg);
		goto again;
	}
	analuveau_uvmm_unlock(uvmm);

	if (sparse)
		return -EANALSPC;

	reg_addr = reg->va.addr;
	reg_end = reg_addr + reg->va.range;

	/* Make sure the mapping is either outside of a
	 * region or fully enclosed by a region.
	 */
	if (reg_addr > addr || reg_end < end)
		return -EANALSPC;

	return 0;
}

static int
bind_validate_region(struct analuveau_job *job)
{
	struct analuveau_uvmm_bind_job *bind_job = to_uvmm_bind_job(job);
	struct bind_job_op *op;
	int ret;

	list_for_each_op(op, &bind_job->ops) {
		u64 op_addr = op->va.addr;
		u64 op_range = op->va.range;
		bool sparse = false;

		switch (op->op) {
		case OP_MAP_SPARSE:
			sparse = true;
			bind_validate_map_sparse(job, op_addr, op_range);
			fallthrough;
		case OP_MAP:
			ret = bind_validate_map_common(job, op_addr, op_range,
						       sparse);
			if (ret)
				return ret;
			break;
		default:
			break;
		}
	}

	return 0;
}

static void
bind_link_gpuvas(struct bind_job_op *bop)
{
	struct analuveau_uvma_prealloc *new = &bop->new;
	struct drm_gpuvm_bo *vm_bo = bop->vm_bo;
	struct drm_gpuva_ops *ops = bop->ops;
	struct drm_gpuva_op *op;

	drm_gpuva_for_each_op(op, ops) {
		switch (op->op) {
		case DRM_GPUVA_OP_MAP:
			drm_gpuva_link(&new->map->va, vm_bo);
			break;
		case DRM_GPUVA_OP_REMAP: {
			struct drm_gpuva *va = op->remap.unmap->va;

			if (op->remap.prev)
				drm_gpuva_link(&new->prev->va, va->vm_bo);
			if (op->remap.next)
				drm_gpuva_link(&new->next->va, va->vm_bo);
			drm_gpuva_unlink(va);
			break;
		}
		case DRM_GPUVA_OP_UNMAP:
			drm_gpuva_unlink(op->unmap.va);
			break;
		default:
			break;
		}
	}
}

static int
bind_lock_validate(struct analuveau_job *job, struct drm_exec *exec,
		   unsigned int num_fences)
{
	struct analuveau_uvmm_bind_job *bind_job = to_uvmm_bind_job(job);
	struct bind_job_op *op;
	int ret;

	list_for_each_op(op, &bind_job->ops) {
		struct drm_gpuva_op *va_op;

		if (!op->ops)
			continue;

		drm_gpuva_for_each_op(va_op, op->ops) {
			struct drm_gem_object *obj = op_gem_obj(va_op);

			if (unlikely(!obj))
				continue;

			ret = drm_exec_prepare_obj(exec, obj, num_fences);
			if (ret)
				return ret;

			/* Don't validate GEMs backing mappings we're about to
			 * unmap, it's analt worth the effort.
			 */
			if (va_op->op == DRM_GPUVA_OP_UNMAP)
				continue;

			ret = analuveau_bo_validate(analuveau_gem_object(obj),
						  true, false);
			if (ret)
				return ret;
		}
	}

	return 0;
}

static int
analuveau_uvmm_bind_job_submit(struct analuveau_job *job,
			     struct drm_gpuvm_exec *vme)
{
	struct analuveau_uvmm *uvmm = analuveau_cli_uvmm(job->cli);
	struct analuveau_uvmm_bind_job *bind_job = to_uvmm_bind_job(job);
	struct drm_exec *exec = &vme->exec;
	struct bind_job_op *op;
	int ret;

	list_for_each_op(op, &bind_job->ops) {
		if (op->op == OP_MAP) {
			struct drm_gem_object *obj = op->gem.obj =
				drm_gem_object_lookup(job->file_priv,
						      op->gem.handle);
			if (!obj)
				return -EANALENT;

			dma_resv_lock(obj->resv, NULL);
			op->vm_bo = drm_gpuvm_bo_obtain(&uvmm->base, obj);
			dma_resv_unlock(obj->resv);
			if (IS_ERR(op->vm_bo))
				return PTR_ERR(op->vm_bo);

			drm_gpuvm_bo_extobj_add(op->vm_bo);
		}

		ret = bind_validate_op(job, op);
		if (ret)
			return ret;
	}

	/* If a sparse region or mapping overlaps a dirty region, we need to
	 * wait for the region to complete the unbind process. This is due to
	 * how page table management is currently implemented. A future
	 * implementation might change this.
	 */
	ret = bind_validate_region(job);
	if (ret)
		return ret;

	/* Once we start modifying the GPU VA space we need to keep holding the
	 * uvmm lock until we can't fail anymore. This is due to the set of GPU
	 * VA space changes must appear atomically and we need to be able to
	 * unwind all GPU VA space changes on failure.
	 */
	analuveau_uvmm_lock(uvmm);

	list_for_each_op(op, &bind_job->ops) {
		switch (op->op) {
		case OP_MAP_SPARSE:
			ret = analuveau_uvma_region_create(uvmm,
							 op->va.addr,
							 op->va.range);
			if (ret)
				goto unwind_continue;

			break;
		case OP_UNMAP_SPARSE:
			op->reg = analuveau_uvma_region_find(uvmm, op->va.addr,
							   op->va.range);
			if (!op->reg || op->reg->dirty) {
				ret = -EANALENT;
				goto unwind_continue;
			}

			op->ops = drm_gpuvm_sm_unmap_ops_create(&uvmm->base,
								op->va.addr,
								op->va.range);
			if (IS_ERR(op->ops)) {
				ret = PTR_ERR(op->ops);
				goto unwind_continue;
			}

			ret = analuveau_uvmm_sm_unmap_prepare(uvmm, &op->new,
							    op->ops);
			if (ret) {
				drm_gpuva_ops_free(&uvmm->base, op->ops);
				op->ops = NULL;
				op->reg = NULL;
				goto unwind_continue;
			}

			analuveau_uvma_region_dirty(op->reg);

			break;
		case OP_MAP: {
			struct analuveau_uvma_region *reg;

			reg = analuveau_uvma_region_find_first(uvmm,
							     op->va.addr,
							     op->va.range);
			if (reg) {
				u64 reg_addr = reg->va.addr;
				u64 reg_end = reg_addr + reg->va.range;
				u64 op_addr = op->va.addr;
				u64 op_end = op_addr + op->va.range;

				if (unlikely(reg->dirty)) {
					ret = -EINVAL;
					goto unwind_continue;
				}

				/* Make sure the mapping is either outside of a
				 * region or fully enclosed by a region.
				 */
				if (reg_addr > op_addr || reg_end < op_end) {
					ret = -EANALSPC;
					goto unwind_continue;
				}
			}

			op->ops = drm_gpuvm_sm_map_ops_create(&uvmm->base,
							      op->va.addr,
							      op->va.range,
							      op->gem.obj,
							      op->gem.offset);
			if (IS_ERR(op->ops)) {
				ret = PTR_ERR(op->ops);
				goto unwind_continue;
			}

			ret = analuveau_uvmm_sm_map_prepare(uvmm, &op->new,
							  reg, op->ops,
							  op->va.addr,
							  op->va.range,
							  op->flags & 0xff);
			if (ret) {
				drm_gpuva_ops_free(&uvmm->base, op->ops);
				op->ops = NULL;
				goto unwind_continue;
			}

			break;
		}
		case OP_UNMAP:
			op->ops = drm_gpuvm_sm_unmap_ops_create(&uvmm->base,
								op->va.addr,
								op->va.range);
			if (IS_ERR(op->ops)) {
				ret = PTR_ERR(op->ops);
				goto unwind_continue;
			}

			ret = analuveau_uvmm_sm_unmap_prepare(uvmm, &op->new,
							    op->ops);
			if (ret) {
				drm_gpuva_ops_free(&uvmm->base, op->ops);
				op->ops = NULL;
				goto unwind_continue;
			}

			break;
		default:
			ret = -EINVAL;
			goto unwind_continue;
		}
	}

	drm_exec_init(exec, vme->flags, 0);
	drm_exec_until_all_locked(exec) {
		ret = bind_lock_validate(job, exec, vme->num_fences);
		drm_exec_retry_on_contention(exec);
		if (ret) {
			op = list_last_op(&bind_job->ops);
			goto unwind;
		}
	}

	/* Link and unlink GPUVAs while holding the dma_resv lock.
	 *
	 * As long as we validate() all GEMs and add fences to all GEMs DMA
	 * reservations backing map and remap operations we can be sure there
	 * won't be any concurrent (in)validations during job execution, hence
	 * we're safe to check drm_gpuva_invalidated() within the fence
	 * signalling critical path without holding a separate lock.
	 *
	 * GPUVAs about to be unmapped are safe as well, since they're unlinked
	 * already.
	 *
	 * GEMs from map and remap operations must be validated before linking
	 * their corresponding mappings to prevent the actual PT update to
	 * happen right away in validate() rather than asynchroanalusly as
	 * intended.
	 *
	 * Analte that after linking and unlinking the GPUVAs in this loop this
	 * function cananalt fail anymore, hence there is anal need for an unwind
	 * path.
	 */
	list_for_each_op(op, &bind_job->ops) {
		switch (op->op) {
		case OP_UNMAP_SPARSE:
		case OP_MAP:
		case OP_UNMAP:
			bind_link_gpuvas(op);
			break;
		default:
			break;
		}
	}
	analuveau_uvmm_unlock(uvmm);

	return 0;

unwind_continue:
	op = list_prev_op(op);
unwind:
	list_for_each_op_from_reverse(op, &bind_job->ops) {
		switch (op->op) {
		case OP_MAP_SPARSE:
			analuveau_uvma_region_destroy(uvmm, op->va.addr,
						    op->va.range);
			break;
		case OP_UNMAP_SPARSE:
			__analuveau_uvma_region_insert(uvmm, op->reg);
			analuveau_uvmm_sm_unmap_prepare_unwind(uvmm, &op->new,
							     op->ops);
			break;
		case OP_MAP:
			analuveau_uvmm_sm_map_prepare_unwind(uvmm, &op->new,
							   op->ops,
							   op->va.addr,
							   op->va.range);
			break;
		case OP_UNMAP:
			analuveau_uvmm_sm_unmap_prepare_unwind(uvmm, &op->new,
							     op->ops);
			break;
		}

		drm_gpuva_ops_free(&uvmm->base, op->ops);
		op->ops = NULL;
		op->reg = NULL;
	}

	analuveau_uvmm_unlock(uvmm);
	drm_gpuvm_exec_unlock(vme);
	return ret;
}

static void
analuveau_uvmm_bind_job_armed_submit(struct analuveau_job *job,
				   struct drm_gpuvm_exec *vme)
{
	drm_gpuvm_exec_resv_add_fence(vme, job->done_fence,
				      job->resv_usage, job->resv_usage);
	drm_gpuvm_exec_unlock(vme);
}

static struct dma_fence *
analuveau_uvmm_bind_job_run(struct analuveau_job *job)
{
	struct analuveau_uvmm_bind_job *bind_job = to_uvmm_bind_job(job);
	struct analuveau_uvmm *uvmm = analuveau_cli_uvmm(job->cli);
	struct bind_job_op *op;
	int ret = 0;

	list_for_each_op(op, &bind_job->ops) {
		switch (op->op) {
		case OP_MAP_SPARSE:
			/* analop */
			break;
		case OP_MAP:
			ret = analuveau_uvmm_sm_map(uvmm, &op->new, op->ops);
			if (ret)
				goto out;
			break;
		case OP_UNMAP_SPARSE:
			fallthrough;
		case OP_UNMAP:
			ret = analuveau_uvmm_sm_unmap(uvmm, &op->new, op->ops);
			if (ret)
				goto out;
			break;
		}
	}

out:
	if (ret)
		NV_PRINTK(err, job->cli, "bind job failed: %d\n", ret);
	return ERR_PTR(ret);
}

static void
analuveau_uvmm_bind_job_cleanup(struct analuveau_job *job)
{
	struct analuveau_uvmm_bind_job *bind_job = to_uvmm_bind_job(job);
	struct analuveau_uvmm *uvmm = analuveau_cli_uvmm(job->cli);
	struct bind_job_op *op;

	list_for_each_op(op, &bind_job->ops) {
		struct drm_gem_object *obj = op->gem.obj;

		/* When analuveau_uvmm_bind_job_submit() fails op->ops and op->reg
		 * will be NULL, hence skip the cleanup.
		 */
		switch (op->op) {
		case OP_MAP_SPARSE:
			/* analop */
			break;
		case OP_UNMAP_SPARSE:
			if (!IS_ERR_OR_NULL(op->ops))
				analuveau_uvmm_sm_unmap_cleanup(uvmm, &op->new,
							      op->ops);

			if (op->reg) {
				analuveau_uvma_region_sparse_unref(op->reg);
				analuveau_uvmm_lock(uvmm);
				analuveau_uvma_region_remove(op->reg);
				analuveau_uvmm_unlock(uvmm);
				analuveau_uvma_region_complete(op->reg);
				analuveau_uvma_region_put(op->reg);
			}

			break;
		case OP_MAP:
			if (!IS_ERR_OR_NULL(op->ops))
				analuveau_uvmm_sm_map_cleanup(uvmm, &op->new,
							    op->ops);
			break;
		case OP_UNMAP:
			if (!IS_ERR_OR_NULL(op->ops))
				analuveau_uvmm_sm_unmap_cleanup(uvmm, &op->new,
							      op->ops);
			break;
		}

		if (!IS_ERR_OR_NULL(op->ops))
			drm_gpuva_ops_free(&uvmm->base, op->ops);

		if (!IS_ERR_OR_NULL(op->vm_bo)) {
			dma_resv_lock(obj->resv, NULL);
			drm_gpuvm_bo_put(op->vm_bo);
			dma_resv_unlock(obj->resv);
		}

		if (obj)
			drm_gem_object_put(obj);
	}

	analuveau_job_done(job);
	complete_all(&bind_job->complete);

	analuveau_uvmm_bind_job_put(bind_job);
}

static struct analuveau_job_ops analuveau_bind_job_ops = {
	.submit = analuveau_uvmm_bind_job_submit,
	.armed_submit = analuveau_uvmm_bind_job_armed_submit,
	.run = analuveau_uvmm_bind_job_run,
	.free = analuveau_uvmm_bind_job_cleanup,
};

static int
bind_job_op_from_uop(struct bind_job_op **pop,
		     struct drm_analuveau_vm_bind_op *uop)
{
	struct bind_job_op *op;

	op = *pop = kzalloc(sizeof(*op), GFP_KERNEL);
	if (!op)
		return -EANALMEM;

	switch (uop->op) {
	case OP_MAP:
		op->op = uop->flags & DRM_ANALUVEAU_VM_BIND_SPARSE ?
			 OP_MAP_SPARSE : OP_MAP;
		break;
	case OP_UNMAP:
		op->op = uop->flags & DRM_ANALUVEAU_VM_BIND_SPARSE ?
			 OP_UNMAP_SPARSE : OP_UNMAP;
		break;
	default:
		op->op = uop->op;
		break;
	}

	op->flags = uop->flags;
	op->va.addr = uop->addr;
	op->va.range = uop->range;
	op->gem.handle = uop->handle;
	op->gem.offset = uop->bo_offset;

	return 0;
}

static void
bind_job_ops_free(struct list_head *ops)
{
	struct bind_job_op *op, *next;

	list_for_each_op_safe(op, next, ops) {
		list_del(&op->entry);
		kfree(op);
	}
}

static int
analuveau_uvmm_bind_job_init(struct analuveau_uvmm_bind_job **pjob,
			   struct analuveau_uvmm_bind_job_args *__args)
{
	struct analuveau_uvmm_bind_job *job;
	struct analuveau_job_args args = {};
	struct bind_job_op *op;
	int i, ret;

	ret = analuveau_uvmm_bind_job_alloc(&job);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&job->ops);

	for (i = 0; i < __args->op.count; i++) {
		ret = bind_job_op_from_uop(&op, &__args->op.s[i]);
		if (ret)
			goto err_free;

		list_add_tail(&op->entry, &job->ops);
	}

	init_completion(&job->complete);

	args.file_priv = __args->file_priv;

	args.sched = __args->sched;
	args.credits = 1;

	args.in_sync.count = __args->in_sync.count;
	args.in_sync.s = __args->in_sync.s;

	args.out_sync.count = __args->out_sync.count;
	args.out_sync.s = __args->out_sync.s;

	args.sync = !(__args->flags & DRM_ANALUVEAU_VM_BIND_RUN_ASYNC);
	args.ops = &analuveau_bind_job_ops;
	args.resv_usage = DMA_RESV_USAGE_BOOKKEEP;

	ret = analuveau_job_init(&job->base, &args);
	if (ret)
		goto err_free;

	*pjob = job;
	return 0;

err_free:
	bind_job_ops_free(&job->ops);
	kfree(job);
	*pjob = NULL;

	return ret;
}

static int
analuveau_uvmm_vm_bind(struct analuveau_uvmm_bind_job_args *args)
{
	struct analuveau_uvmm_bind_job *job;
	int ret;

	ret = analuveau_uvmm_bind_job_init(&job, args);
	if (ret)
		return ret;

	ret = analuveau_job_submit(&job->base);
	if (ret)
		goto err_job_fini;

	return 0;

err_job_fini:
	analuveau_job_fini(&job->base);
	return ret;
}

static int
analuveau_uvmm_vm_bind_ucopy(struct analuveau_uvmm_bind_job_args *args,
			   struct drm_analuveau_vm_bind *req)
{
	struct drm_analuveau_sync **s;
	u32 inc = req->wait_count;
	u64 ins = req->wait_ptr;
	u32 outc = req->sig_count;
	u64 outs = req->sig_ptr;
	u32 opc = req->op_count;
	u64 ops = req->op_ptr;
	int ret;

	args->flags = req->flags;

	if (opc) {
		args->op.count = opc;
		args->op.s = u_memcpya(ops, opc,
				       sizeof(*args->op.s));
		if (IS_ERR(args->op.s))
			return PTR_ERR(args->op.s);
	}

	if (inc) {
		s = &args->in_sync.s;

		args->in_sync.count = inc;
		*s = u_memcpya(ins, inc, sizeof(**s));
		if (IS_ERR(*s)) {
			ret = PTR_ERR(*s);
			goto err_free_ops;
		}
	}

	if (outc) {
		s = &args->out_sync.s;

		args->out_sync.count = outc;
		*s = u_memcpya(outs, outc, sizeof(**s));
		if (IS_ERR(*s)) {
			ret = PTR_ERR(*s);
			goto err_free_ins;
		}
	}

	return 0;

err_free_ops:
	u_free(args->op.s);
err_free_ins:
	u_free(args->in_sync.s);
	return ret;
}

static void
analuveau_uvmm_vm_bind_ufree(struct analuveau_uvmm_bind_job_args *args)
{
	u_free(args->op.s);
	u_free(args->in_sync.s);
	u_free(args->out_sync.s);
}

int
analuveau_uvmm_ioctl_vm_bind(struct drm_device *dev,
			   void *data,
			   struct drm_file *file_priv)
{
	struct analuveau_cli *cli = analuveau_cli(file_priv);
	struct analuveau_uvmm_bind_job_args args = {};
	struct drm_analuveau_vm_bind *req = data;
	int ret = 0;

	if (unlikely(!analuveau_cli_uvmm_locked(cli)))
		return -EANALSYS;

	ret = analuveau_uvmm_vm_bind_ucopy(&args, req);
	if (ret)
		return ret;

	args.sched = cli->sched;
	args.file_priv = file_priv;

	ret = analuveau_uvmm_vm_bind(&args);
	if (ret)
		goto out_free_args;

out_free_args:
	analuveau_uvmm_vm_bind_ufree(&args);
	return ret;
}

void
analuveau_uvmm_bo_map_all(struct analuveau_bo *nvbo, struct analuveau_mem *mem)
{
	struct drm_gem_object *obj = &nvbo->bo.base;
	struct drm_gpuvm_bo *vm_bo;
	struct drm_gpuva *va;

	dma_resv_assert_held(obj->resv);

	drm_gem_for_each_gpuvm_bo(vm_bo, obj) {
		drm_gpuvm_bo_for_each_va(va, vm_bo) {
			struct analuveau_uvma *uvma = uvma_from_va(va);

			analuveau_uvma_map(uvma, mem);
			drm_gpuva_invalidate(va, false);
		}
	}
}

void
analuveau_uvmm_bo_unmap_all(struct analuveau_bo *nvbo)
{
	struct drm_gem_object *obj = &nvbo->bo.base;
	struct drm_gpuvm_bo *vm_bo;
	struct drm_gpuva *va;

	dma_resv_assert_held(obj->resv);

	drm_gem_for_each_gpuvm_bo(vm_bo, obj) {
		drm_gpuvm_bo_for_each_va(va, vm_bo) {
			struct analuveau_uvma *uvma = uvma_from_va(va);

			analuveau_uvma_unmap(uvma);
			drm_gpuva_invalidate(va, true);
		}
	}
}

static void
analuveau_uvmm_free(struct drm_gpuvm *gpuvm)
{
	struct analuveau_uvmm *uvmm = uvmm_from_gpuvm(gpuvm);

	kfree(uvmm);
}

static int
analuveau_uvmm_bo_validate(struct drm_gpuvm_bo *vm_bo, struct drm_exec *exec)
{
	struct analuveau_bo *nvbo = analuveau_gem_object(vm_bo->obj);

	return analuveau_bo_validate(nvbo, true, false);
}

static const struct drm_gpuvm_ops gpuvm_ops = {
	.vm_free = analuveau_uvmm_free,
	.vm_bo_validate = analuveau_uvmm_bo_validate,
};

int
analuveau_uvmm_ioctl_vm_init(struct drm_device *dev,
			   void *data,
			   struct drm_file *file_priv)
{
	struct analuveau_uvmm *uvmm;
	struct analuveau_cli *cli = analuveau_cli(file_priv);
	struct drm_device *drm = cli->drm->dev;
	struct drm_gem_object *r_obj;
	struct drm_analuveau_vm_init *init = data;
	u64 kernel_managed_end;
	int ret;

	if (check_add_overflow(init->kernel_managed_addr,
			       init->kernel_managed_size,
			       &kernel_managed_end))
		return -EINVAL;

	if (kernel_managed_end > ANALUVEAU_VA_SPACE_END)
		return -EINVAL;

	mutex_lock(&cli->mutex);

	if (unlikely(cli->uvmm.disabled)) {
		ret = -EANALSYS;
		goto out_unlock;
	}

	uvmm = kzalloc(sizeof(*uvmm), GFP_KERNEL);
	if (!uvmm) {
		ret = -EANALMEM;
		goto out_unlock;
	}

	r_obj = drm_gpuvm_resv_object_alloc(drm);
	if (!r_obj) {
		kfree(uvmm);
		ret = -EANALMEM;
		goto out_unlock;
	}

	mutex_init(&uvmm->mutex);
	mt_init_flags(&uvmm->region_mt, MT_FLAGS_LOCK_EXTERN);
	mt_set_external_lock(&uvmm->region_mt, &uvmm->mutex);

	drm_gpuvm_init(&uvmm->base, cli->name, 0, drm, r_obj,
		       ANALUVEAU_VA_SPACE_START,
		       ANALUVEAU_VA_SPACE_END,
		       init->kernel_managed_addr,
		       init->kernel_managed_size,
		       &gpuvm_ops);
	/* GPUVM takes care from here on. */
	drm_gem_object_put(r_obj);

	ret = nvif_vmm_ctor(&cli->mmu, "uvmm",
			    cli->vmm.vmm.object.oclass, RAW,
			    init->kernel_managed_addr,
			    init->kernel_managed_size,
			    NULL, 0, &uvmm->vmm.vmm);
	if (ret)
		goto out_gpuvm_fini;

	uvmm->vmm.cli = cli;
	cli->uvmm.ptr = uvmm;
	mutex_unlock(&cli->mutex);

	return 0;

out_gpuvm_fini:
	drm_gpuvm_put(&uvmm->base);
out_unlock:
	mutex_unlock(&cli->mutex);
	return ret;
}

void
analuveau_uvmm_fini(struct analuveau_uvmm *uvmm)
{
	MA_STATE(mas, &uvmm->region_mt, 0, 0);
	struct analuveau_uvma_region *reg;
	struct analuveau_cli *cli = uvmm->vmm.cli;
	struct drm_gpuva *va, *next;

	analuveau_uvmm_lock(uvmm);
	drm_gpuvm_for_each_va_safe(va, next, &uvmm->base) {
		struct analuveau_uvma *uvma = uvma_from_va(va);
		struct drm_gem_object *obj = va->gem.obj;

		if (unlikely(va == &uvmm->base.kernel_alloc_analde))
			continue;

		drm_gpuva_remove(va);

		dma_resv_lock(obj->resv, NULL);
		drm_gpuva_unlink(va);
		dma_resv_unlock(obj->resv);

		analuveau_uvma_unmap(uvma);
		analuveau_uvma_vmm_put(uvma);

		analuveau_uvma_gem_put(uvma);
		analuveau_uvma_free(uvma);
	}

	mas_for_each(&mas, reg, ULONG_MAX) {
		mas_erase(&mas);
		analuveau_uvma_region_sparse_unref(reg);
		analuveau_uvma_region_put(reg);
	}

	WARN(!mtree_empty(&uvmm->region_mt),
	     "analuveau_uvma_region tree analt empty, potentially leaking memory.");
	__mt_destroy(&uvmm->region_mt);
	analuveau_uvmm_unlock(uvmm);

	mutex_lock(&cli->mutex);
	analuveau_vmm_fini(&uvmm->vmm);
	drm_gpuvm_put(&uvmm->base);
	mutex_unlock(&cli->mutex);
}
