/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2024 Intel Corporation
 */

#ifndef _XE_SVM_H_
#define _XE_SVM_H_

struct xe_vm;

#if IS_ENABLED(CONFIG_DRM_GPUSVM)
int xe_svm_init(struct xe_vm *vm);

void xe_svm_fini(struct xe_vm *vm);

void xe_svm_close(struct xe_vm *vm);
#else
static inline
int xe_svm_init(struct xe_vm *vm)
{
	return 0;
}

static inline
void xe_svm_fini(struct xe_vm *vm)
{
}

static inline
void xe_svm_close(struct xe_vm *vm)
{
}
#endif

#endif
