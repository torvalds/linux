/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#ifndef PVR_MMU_H
#define PVR_MMU_H

#include <linux/memory.h>
#include <linux/types.h>

/* Forward declaration from "pvr_device.h" */
struct pvr_device;

/* Forward declaration from "pvr_mmu.c" */
struct pvr_mmu_context;
struct pvr_mmu_op_context;

/* Forward declaration from "pvr_vm.c" */
struct pvr_vm_context;

/* Forward declaration from <linux/scatterlist.h> */
struct sg_table;

/**
 * DOC: Public API (constants)
 *
 * .. c:macro:: PVR_DEVICE_PAGE_SIZE
 *
 *    Fixed page size referenced by leaf nodes in the page table tree
 *    structure. In the current implementation, this value is pegged to the
 *    CPU page size (%PAGE_SIZE). It is therefore an error to specify a CPU
 *    page size which is not also a supported device page size. The supported
 *    device page sizes are: 4KiB, 16KiB, 64KiB, 256KiB, 1MiB and 2MiB.
 *
 * .. c:macro:: PVR_DEVICE_PAGE_SHIFT
 *
 *    Shift value used to efficiently multiply or divide by
 *    %PVR_DEVICE_PAGE_SIZE.
 *
 *    This value is derived from %PVR_DEVICE_PAGE_SIZE.
 *
 * .. c:macro:: PVR_DEVICE_PAGE_MASK
 *
 *    Mask used to round a value down to the nearest multiple of
 *    %PVR_DEVICE_PAGE_SIZE. When bitwise negated, it will indicate whether a
 *    value is already a multiple of %PVR_DEVICE_PAGE_SIZE.
 *
 *    This value is derived from %PVR_DEVICE_PAGE_SIZE.
 */

/* PVR_DEVICE_PAGE_SIZE determines the page size */
#define PVR_DEVICE_PAGE_SIZE (PAGE_SIZE)
#define PVR_DEVICE_PAGE_SHIFT (PAGE_SHIFT)
#define PVR_DEVICE_PAGE_MASK (PAGE_MASK)

/**
 * DOC: Page table index utilities (constants)
 *
 * .. c:macro:: PVR_PAGE_TABLE_ADDR_SPACE_SIZE
 *
 *    Size of device-virtual address space which can be represented in the page
 *    table structure.
 *
 *    This value is checked at runtime against
 *    &pvr_device_features.virtual_address_space_bits by
 *    pvr_vm_create_context(), which will return an error if the feature value
 *    does not match this constant.
 *
 *    .. admonition:: Future work
 *
 *       It should be possible to support other values of
 *       &pvr_device_features.virtual_address_space_bits, but so far no
 *       hardware has been created which advertises an unsupported value.
 *
 * .. c:macro:: PVR_PAGE_TABLE_ADDR_BITS
 *
 *    Number of bits needed to represent any value less than
 *    %PVR_PAGE_TABLE_ADDR_SPACE_SIZE exactly.
 *
 * .. c:macro:: PVR_PAGE_TABLE_ADDR_MASK
 *
 *    Bitmask of device-virtual addresses which are valid in the page table
 *    structure.
 *
 *    This value is derived from %PVR_PAGE_TABLE_ADDR_SPACE_SIZE, so the same
 *    notes on that constant apply here.
 */
#define PVR_PAGE_TABLE_ADDR_SPACE_SIZE SZ_1T
#define PVR_PAGE_TABLE_ADDR_BITS __ffs(PVR_PAGE_TABLE_ADDR_SPACE_SIZE)
#define PVR_PAGE_TABLE_ADDR_MASK (PVR_PAGE_TABLE_ADDR_SPACE_SIZE - 1)

void pvr_mmu_flush_request_all(struct pvr_device *pvr_dev);
int pvr_mmu_flush_exec(struct pvr_device *pvr_dev, bool wait);

struct pvr_mmu_context *pvr_mmu_context_create(struct pvr_device *pvr_dev);
void pvr_mmu_context_destroy(struct pvr_mmu_context *ctx);

dma_addr_t pvr_mmu_get_root_table_dma_addr(struct pvr_mmu_context *ctx);

void pvr_mmu_op_context_destroy(struct pvr_mmu_op_context *op_ctx);
struct pvr_mmu_op_context *
pvr_mmu_op_context_create(struct pvr_mmu_context *ctx,
			  struct sg_table *sgt, u64 sgt_offset, u64 size);

int pvr_mmu_map(struct pvr_mmu_op_context *op_ctx, u64 size, u64 flags,
		u64 device_addr);
int pvr_mmu_unmap(struct pvr_mmu_op_context *op_ctx, u64 device_addr, u64 size);

#endif /* PVR_MMU_H */
