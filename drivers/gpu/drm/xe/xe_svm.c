// SPDX-License-Identifier: MIT
/*
 * Copyright © 2024 Intel Corporation
 */

#include "xe_svm.h"
#include "xe_vm.h"
#include "xe_vm_types.h"

static void xe_svm_invalidate(struct drm_gpusvm *gpusvm,
			      struct drm_gpusvm_notifier *notifier,
			      const struct mmu_notifier_range *mmu_range)
{
	/* TODO: Implement */
}

static const struct drm_gpusvm_ops gpusvm_ops = {
	.invalidate = xe_svm_invalidate,
};

static const unsigned long fault_chunk_sizes[] = {
	SZ_2M,
	SZ_64K,
	SZ_4K,
};

/**
 * xe_svm_init() - SVM initialize
 * @vm: The VM.
 *
 * Initialize SVM state which is embedded within the VM.
 *
 * Return: 0 on success, negative error code on error.
 */
int xe_svm_init(struct xe_vm *vm)
{
	int err;

	err = drm_gpusvm_init(&vm->svm.gpusvm, "Xe SVM", &vm->xe->drm,
			      current->mm, NULL, 0, vm->size,
			      SZ_512M, &gpusvm_ops, fault_chunk_sizes,
			      ARRAY_SIZE(fault_chunk_sizes));
	if (err)
		return err;

	drm_gpusvm_driver_set_lock(&vm->svm.gpusvm, &vm->lock);

	return 0;
}

/**
 * xe_svm_close() - SVM close
 * @vm: The VM.
 *
 * Close SVM state (i.e., stop and flush all SVM actions).
 */
void xe_svm_close(struct xe_vm *vm)
{
	xe_assert(vm->xe, xe_vm_is_closed(vm));
}

/**
 * xe_svm_fini() - SVM finalize
 * @vm: The VM.
 *
 * Finalize SVM state which is embedded within the VM.
 */
void xe_svm_fini(struct xe_vm *vm)
{
	xe_assert(vm->xe, xe_vm_is_closed(vm));

	drm_gpusvm_fini(&vm->svm.gpusvm);
}
