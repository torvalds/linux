/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright © 2024 Intel Corporation
 */

#undef TRACE_SYSTEM
#define TRACE_SYSTEM xe

#if !defined(_XE_TRACE_BO_H_) || defined(TRACE_HEADER_MULTI_READ)
#define _XE_TRACE_BO_H_

#include <linux/tracepoint.h>
#include <linux/types.h>

#include "xe_bo.h"
#include "xe_bo_types.h"
#include "xe_vm.h"

#define __dev_name_bo(bo)	dev_name(xe_bo_device(bo)->drm.dev)
#define __dev_name_vm(vm)	dev_name((vm)->xe->drm.dev)
#define __dev_name_vma(vma)	__dev_name_vm(xe_vma_vm(vma))

DECLARE_EVENT_CLASS(xe_bo,
		    TP_PROTO(struct xe_bo *bo),
		    TP_ARGS(bo),

		    TP_STRUCT__entry(
			     __string(dev, __dev_name_bo(bo))
			     __field(size_t, size)
			     __field(u32, flags)
			     __field(struct xe_vm *, vm)
			     ),

		    TP_fast_assign(
			   __assign_str(dev);
			   __entry->size = xe_bo_size(bo);
			   __entry->flags = bo->flags;
			   __entry->vm = bo->vm;
			   ),

		    TP_printk("dev=%s, size=%zu, flags=0x%02x, vm=%p",
			      __get_str(dev), __entry->size,
			      __entry->flags, __entry->vm)
);

DEFINE_EVENT(xe_bo, xe_bo_cpu_fault,
	     TP_PROTO(struct xe_bo *bo),
	     TP_ARGS(bo)
);

DEFINE_EVENT(xe_bo, xe_bo_validate,
	     TP_PROTO(struct xe_bo *bo),
	     TP_ARGS(bo)
);

DEFINE_EVENT(xe_bo, xe_bo_create,
	     TP_PROTO(struct xe_bo *bo),
	     TP_ARGS(bo)
);

TRACE_EVENT(xe_bo_move,
	    TP_PROTO(struct xe_bo *bo, uint32_t new_placement, uint32_t old_placement,
		     bool move_lacks_source),
	    TP_ARGS(bo, new_placement, old_placement, move_lacks_source),
	    TP_STRUCT__entry(
		     __field(struct xe_bo *, bo)
		     __field(size_t, size)
		     __string(new_placement_name, xe_mem_type_to_name[new_placement])
		     __string(old_placement_name, xe_mem_type_to_name[old_placement])
		     __string(device_id, __dev_name_bo(bo))
		     __field(bool, move_lacks_source)
			),

	    TP_fast_assign(
		   __entry->bo      = bo;
		   __entry->size = xe_bo_size(bo);
		   __assign_str(new_placement_name);
		   __assign_str(old_placement_name);
		   __assign_str(device_id);
		   __entry->move_lacks_source = move_lacks_source;
		   ),
	    TP_printk("move_lacks_source:%s, migrate object %p [size %zu] from %s to %s device_id:%s",
		      __entry->move_lacks_source ? "yes" : "no", __entry->bo, __entry->size,
		      __get_str(old_placement_name),
		      __get_str(new_placement_name), __get_str(device_id))
);

DECLARE_EVENT_CLASS(xe_vma,
		    TP_PROTO(struct xe_vma *vma),
		    TP_ARGS(vma),

		    TP_STRUCT__entry(
			     __string(dev, __dev_name_vma(vma))
			     __field(struct xe_vma *, vma)
			     __field(struct xe_vm *, vm)
			     __field(u32, asid)
			     __field(u64, start)
			     __field(u64, end)
			     __field(u64, ptr)
			     ),

		    TP_fast_assign(
			   __assign_str(dev);
			   __entry->vma = vma;
			   __entry->vm = xe_vma_vm(vma);
			   __entry->asid = xe_vma_vm(vma)->usm.asid;
			   __entry->start = xe_vma_start(vma);
			   __entry->end = xe_vma_end(vma) - 1;
			   __entry->ptr = xe_vma_userptr(vma);
			   ),

		    TP_printk("dev=%s, vma=%p, vm=%p, asid=0x%05x, start=0x%012llx, end=0x%012llx, userptr=0x%012llx",
			      __get_str(dev), __entry->vma, __entry->vm,
			      __entry->asid, __entry->start,
			      __entry->end, __entry->ptr)
)

DEFINE_EVENT(xe_vma, xe_vma_flush,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_pagefault,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_acc,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_bind,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_pf_bind,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_unbind,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_userptr_rebind_worker,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_userptr_rebind_exec,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_rebind_worker,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_rebind_exec,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_userptr_invalidate,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_invalidate,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_evict,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DEFINE_EVENT(xe_vma, xe_vma_userptr_invalidate_complete,
	     TP_PROTO(struct xe_vma *vma),
	     TP_ARGS(vma)
);

DECLARE_EVENT_CLASS(xe_vm,
		    TP_PROTO(struct xe_vm *vm),
		    TP_ARGS(vm),

		    TP_STRUCT__entry(
			     __string(dev, __dev_name_vm(vm))
			     __field(struct xe_vm *, vm)
			     __field(u32, asid)
			     __field(u32, flags)
			     ),

		    TP_fast_assign(
			   __assign_str(dev);
			   __entry->vm = vm;
			   __entry->asid = vm->usm.asid;
			   __entry->flags = vm->flags;
			   ),

		    TP_printk("dev=%s, vm=%p, asid=0x%05x, vm flags=0x%05x",
			      __get_str(dev), __entry->vm, __entry->asid,
			      __entry->flags)
);

DEFINE_EVENT(xe_vm, xe_vm_kill,
	     TP_PROTO(struct xe_vm *vm),
	     TP_ARGS(vm)
);

DEFINE_EVENT(xe_vm, xe_vm_create,
	     TP_PROTO(struct xe_vm *vm),
	     TP_ARGS(vm)
);

DEFINE_EVENT(xe_vm, xe_vm_free,
	     TP_PROTO(struct xe_vm *vm),
	     TP_ARGS(vm)
);

DEFINE_EVENT(xe_vm, xe_vm_cpu_bind,
	     TP_PROTO(struct xe_vm *vm),
	     TP_ARGS(vm)
);

DEFINE_EVENT(xe_vm, xe_vm_restart,
	     TP_PROTO(struct xe_vm *vm),
	     TP_ARGS(vm)
);

DEFINE_EVENT(xe_vm, xe_vm_rebind_worker_enter,
	     TP_PROTO(struct xe_vm *vm),
	     TP_ARGS(vm)
);

DEFINE_EVENT(xe_vm, xe_vm_rebind_worker_retry,
	     TP_PROTO(struct xe_vm *vm),
	     TP_ARGS(vm)
);

DEFINE_EVENT(xe_vm, xe_vm_rebind_worker_exit,
	     TP_PROTO(struct xe_vm *vm),
	     TP_ARGS(vm)
);

DEFINE_EVENT(xe_vm, xe_vm_ops_fail,
	     TP_PROTO(struct xe_vm *vm),
	     TP_ARGS(vm)
);

#endif

/* This part must be outside protection */
#undef TRACE_INCLUDE_PATH
#undef TRACE_INCLUDE_FILE
#define TRACE_INCLUDE_PATH ../../drivers/gpu/drm/xe
#define TRACE_INCLUDE_FILE xe_trace_bo
#include <trace/define_trace.h>
