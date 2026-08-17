/* SPDX-License-Identifier: MIT */
/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * Lockdep annotation interface for AMDGPU
 */

#ifndef __AMDGPU_LOCKDEP_H__
#define __AMDGPU_LOCKDEP_H__

#include <linux/lockdep.h>

struct amdgpu_device;

#ifdef CONFIG_LOCKDEP

/**
 * amdgpu_lockdep_init - Train lockdep on correct lock ordering
 *
 * Call once during module init to establish the lock dependency chain.
 */
int amdgpu_lockdep_init(void);

/**
 * amdgpu_lockdep_set_class - Associate lock class keys with real locks
 * @adev: AMDGPU device
 *
 * Call during device init to associate lock classes with actual locks.
 */
void amdgpu_lockdep_set_class(struct amdgpu_device *adev);

#else /* !CONFIG_LOCKDEP */

static inline int amdgpu_lockdep_init(void) { return 0; }
static inline void amdgpu_lockdep_set_class(struct amdgpu_device *adev) {}

#endif /* CONFIG_LOCKDEP */

#endif /* __AMDGPU_LOCKDEP_H__ */
