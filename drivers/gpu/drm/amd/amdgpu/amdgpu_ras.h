/*
 * Copyright 2018 Advanced Micro Devices, Inc.
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
 *
 */
#ifndef _AMDGPU_RAS_H
#define _AMDGPU_RAS_H

#include <linux/debugfs.h>
#include <linux/list.h>
#include "amdgpu.h"
#include "amdgpu_psp.h"
#include "ta_ras_if.h"

enum amdgpu_ras_block {
	AMDGPU_RAS_BLOCK__UMC = 0,
	AMDGPU_RAS_BLOCK__SDMA,
	AMDGPU_RAS_BLOCK__GFX,
	AMDGPU_RAS_BLOCK__MMHUB,
	AMDGPU_RAS_BLOCK__ATHUB,
	AMDGPU_RAS_BLOCK__PCIE_BIF,
	AMDGPU_RAS_BLOCK__HDP,
	AMDGPU_RAS_BLOCK__XGMI_WAFL,
	AMDGPU_RAS_BLOCK__DF,
	AMDGPU_RAS_BLOCK__SMN,
	AMDGPU_RAS_BLOCK__SEM,
	AMDGPU_RAS_BLOCK__MP0,
	AMDGPU_RAS_BLOCK__MP1,
	AMDGPU_RAS_BLOCK__FUSE,

	AMDGPU_RAS_BLOCK__LAST
};

#define AMDGPU_RAS_BLOCK_COUNT	AMDGPU_RAS_BLOCK__LAST
#define AMDGPU_RAS_BLOCK_MASK	((1ULL << AMDGPU_RAS_BLOCK_COUNT) - 1)

enum amdgpu_ras_error_type {
	AMDGPU_RAS_ERROR__NONE							= 0,
	AMDGPU_RAS_ERROR__PARITY						= 1,
	AMDGPU_RAS_ERROR__SINGLE_CORRECTABLE					= 2,
	AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE					= 4,
	AMDGPU_RAS_ERROR__POISON						= 8,
};

enum amdgpu_ras_ret {
	AMDGPU_RAS_SUCCESS = 0,
	AMDGPU_RAS_FAIL,
	AMDGPU_RAS_UE,
	AMDGPU_RAS_CE,
	AMDGPU_RAS_PT,
};

struct ras_common_if {
	enum amdgpu_ras_block block;
	enum amdgpu_ras_error_type type;
	uint32_t sub_block_index;
	/* block name */
	char name[32];
};

typedef int (*ras_ih_cb)(struct amdgpu_device *adev,
		struct amdgpu_iv_entry *entry);

struct amdgpu_ras {
	/* ras infrastructure */
	/* for ras itself. */
	uint32_t hw_supported;
	/* for IP to check its ras ability. */
	uint32_t supported;
	uint32_t features;
	struct list_head head;
	/* debugfs */
	struct dentry *dir;
	/* debugfs ctrl */
	struct dentry *ent;
	/* sysfs */
	struct device_attribute features_attr;
	/* block array */
	struct ras_manager *objs;

	/* gpu recovery */
	struct work_struct recovery_work;
	atomic_t in_recovery;
	struct amdgpu_device *adev;
	/* error handler data */
	struct ras_err_handler_data *eh_data;
	struct mutex recovery_lock;

	uint32_t flags;
};

/* interfaces for IP */

struct ras_fs_if {
	struct ras_common_if head;
	char sysfs_name[32];
	char debugfs_name[32];
};

struct ras_query_if {
	struct ras_common_if head;
	unsigned long ue_count;
	unsigned long ce_count;
};

struct ras_inject_if {
	struct ras_common_if head;
	uint64_t address;
	uint64_t value;
};

struct ras_cure_if {
	struct ras_common_if head;
	uint64_t address;
};

struct ras_ih_if {
	struct ras_common_if head;
	ras_ih_cb cb;
};

struct ras_dispatch_if {
	struct ras_common_if head;
	struct amdgpu_iv_entry *entry;
};

struct ras_debug_if {
	union {
		struct ras_common_if head;
		struct ras_inject_if inject;
	};
	int op;
};
/* work flow
 * vbios
 * 1: ras feature enable (enabled by default)
 * psp
 * 2: ras framework init (in ip_init)
 * IP
 * 3: IH add
 * 4: debugfs/sysfs create
 * 5: query/inject
 * 6: debugfs/sysfs remove
 * 7: IH remove
 * 8: feature disable
 */

#define amdgpu_ras_get_context(adev)		((adev)->psp.ras.ras)
#define amdgpu_ras_set_context(adev, ras_con)	((adev)->psp.ras.ras = (ras_con))

/* check if ras is supported on block, say, sdma, gfx */
static inline int amdgpu_ras_is_supported(struct amdgpu_device *adev,
		unsigned int block)
{
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

	return ras && (ras->supported & (1 << block));
}

int amdgpu_ras_query_error_count(struct amdgpu_device *adev,
		bool is_ce);

/* error handling functions */
int amdgpu_ras_add_bad_pages(struct amdgpu_device *adev,
		unsigned long *bps, int pages);

int amdgpu_ras_reserve_bad_pages(struct amdgpu_device *adev);

static inline int amdgpu_ras_reset_gpu(struct amdgpu_device *adev,
		bool is_baco)
{
	/* remove me when gpu reset works on vega20 A1. */
#if 0
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

	if (atomic_cmpxchg(&ras->in_recovery, 0, 1) == 0)
		schedule_work(&ras->recovery_work);
#endif
	return 0;
}

static inline enum ta_ras_block
amdgpu_ras_block_to_ta(enum amdgpu_ras_block block) {
	switch (block) {
	case AMDGPU_RAS_BLOCK__UMC:
		return TA_RAS_BLOCK__UMC;
	case AMDGPU_RAS_BLOCK__SDMA:
		return TA_RAS_BLOCK__SDMA;
	case AMDGPU_RAS_BLOCK__GFX:
		return TA_RAS_BLOCK__GFX;
	case AMDGPU_RAS_BLOCK__MMHUB:
		return TA_RAS_BLOCK__MMHUB;
	case AMDGPU_RAS_BLOCK__ATHUB:
		return TA_RAS_BLOCK__ATHUB;
	case AMDGPU_RAS_BLOCK__PCIE_BIF:
		return TA_RAS_BLOCK__PCIE_BIF;
	case AMDGPU_RAS_BLOCK__HDP:
		return TA_RAS_BLOCK__HDP;
	case AMDGPU_RAS_BLOCK__XGMI_WAFL:
		return TA_RAS_BLOCK__XGMI_WAFL;
	case AMDGPU_RAS_BLOCK__DF:
		return TA_RAS_BLOCK__DF;
	case AMDGPU_RAS_BLOCK__SMN:
		return TA_RAS_BLOCK__SMN;
	case AMDGPU_RAS_BLOCK__SEM:
		return TA_RAS_BLOCK__SEM;
	case AMDGPU_RAS_BLOCK__MP0:
		return TA_RAS_BLOCK__MP0;
	case AMDGPU_RAS_BLOCK__MP1:
		return TA_RAS_BLOCK__MP1;
	case AMDGPU_RAS_BLOCK__FUSE:
		return TA_RAS_BLOCK__FUSE;
	default:
		WARN_ONCE(1, "RAS ERROR: unexpected block id %d\n", block);
		return TA_RAS_BLOCK__UMC;
	}
}

static inline enum ta_ras_error_type
amdgpu_ras_error_to_ta(enum amdgpu_ras_error_type error) {
	switch (error) {
	case AMDGPU_RAS_ERROR__NONE:
		return TA_RAS_ERROR__NONE;
	case AMDGPU_RAS_ERROR__PARITY:
		return TA_RAS_ERROR__PARITY;
	case AMDGPU_RAS_ERROR__SINGLE_CORRECTABLE:
		return TA_RAS_ERROR__SINGLE_CORRECTABLE;
	case AMDGPU_RAS_ERROR__MULTI_UNCORRECTABLE:
		return TA_RAS_ERROR__MULTI_UNCORRECTABLE;
	case AMDGPU_RAS_ERROR__POISON:
		return TA_RAS_ERROR__POISON;
	default:
		WARN_ONCE(1, "RAS ERROR: unexpected error type %d\n", error);
		return TA_RAS_ERROR__NONE;
	}
}

/* called in ip_init and ip_fini */
int amdgpu_ras_init(struct amdgpu_device *adev);
void amdgpu_ras_post_init(struct amdgpu_device *adev);
int amdgpu_ras_fini(struct amdgpu_device *adev);
int amdgpu_ras_pre_fini(struct amdgpu_device *adev);

int amdgpu_ras_feature_enable(struct amdgpu_device *adev,
		struct ras_common_if *head, bool enable);

int amdgpu_ras_sysfs_create(struct amdgpu_device *adev,
		struct ras_fs_if *head);

int amdgpu_ras_sysfs_remove(struct amdgpu_device *adev,
		struct ras_common_if *head);

int amdgpu_ras_debugfs_create(struct amdgpu_device *adev,
		struct ras_fs_if *head);

int amdgpu_ras_debugfs_remove(struct amdgpu_device *adev,
		struct ras_common_if *head);

int amdgpu_ras_error_query(struct amdgpu_device *adev,
		struct ras_query_if *info);

int amdgpu_ras_error_inject(struct amdgpu_device *adev,
		struct ras_inject_if *info);

int amdgpu_ras_interrupt_add_handler(struct amdgpu_device *adev,
		struct ras_ih_if *info);

int amdgpu_ras_interrupt_remove_handler(struct amdgpu_device *adev,
		struct ras_ih_if *info);

int amdgpu_ras_interrupt_dispatch(struct amdgpu_device *adev,
		struct ras_dispatch_if *info);
#endif
