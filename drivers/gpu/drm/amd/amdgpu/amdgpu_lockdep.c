// SPDX-License-Identifier: MIT
/*
 * Copyright 2024 Advanced Micro Devices, Inc.
 *
 * Lockdep annotation for AMDGPU lock ordering
 *
 * This module teaches lockdep the correct lock ordering to catch
 * potential deadlocks at development time rather than runtime.
 *
 * Based on dma-resv lockdep approach from:
 * drivers/dma-buf/dma-resv.c:dma_resv_lockdep()
 */

#include "amdgpu.h"
#include "amdgpu_reset.h"

#ifdef CONFIG_LOCKDEP

struct amdgpu_lockdep_dummy_locks {
	struct mutex reset_lock;
	struct mutex userq_sch_mutex;
	struct mutex userq_mutex;
	struct mutex notifier_lock;
	struct mutex vram_lock;
	struct mutex srbm_mutex;
	struct mutex grbm_idx_mutex;
	spinlock_t mmio_idx_lock;
};

/* Lock class keys for associating with real driver locks */
static struct lock_class_key amdgpu_userq_sch_mutex_key;
static struct lock_class_key amdgpu_userq_mutex_key;
static struct lock_class_key amdgpu_notifier_lock_key;
static struct lock_class_key amdgpu_vram_lock_key;
static struct lock_class_key amdgpu_reset_sem_key;
static struct lock_class_key amdgpu_reset_lock_key;
static struct lock_class_key amdgpu_srbm_lock_key;
static struct lock_class_key amdgpu_grbm_lock_key;
static struct lock_class_key amdgpu_mmio_lock_key;

/**
 * amdgpu_lockdep_set_class - Associate lock class keys with real locks
 * @adev: AMDGPU device
 *
 * Call during device init to associate lock classes with actual locks
 * so lockdep can track them properly.
 */
void amdgpu_lockdep_set_class(struct amdgpu_device *adev)
{
	lockdep_set_class(&adev->gfx.userq_sch_mutex,
			  &amdgpu_userq_sch_mutex_key);
	lockdep_set_class(&adev->notifier_lock, &amdgpu_notifier_lock_key);
	lockdep_set_class(&adev->srbm_mutex, &amdgpu_srbm_lock_key);
	lockdep_set_class(&adev->grbm_idx_mutex, &amdgpu_grbm_lock_key);
	lockdep_set_class(&adev->mmio_idx_lock, &amdgpu_mmio_lock_key);

	if (adev->reset_domain)
		lockdep_set_class(&adev->reset_domain->sem,
				  &amdgpu_reset_sem_key);
}

/**
 * amdgpu_lockdep_init - Teach lockdep the correct lock ordering
 *
 * Instantiates dummy objects and takes locks in the correct order to
 * train lockdep. This helps catch lock ordering violations during
 * development.
 *
 * Lock ordering hierarchy (outermost to innermost):
 *
 * 1. userq_sch_mutex     - Global userq scheduler (enforce_isolation)
 * 2. userq_mutex         - Per-context userq (held across queue create/destroy)
 * 3. notifier_lock       - MMU notifier lock
 * 4. vram_lock           - VRAM allocator lock
 * 5. reset_domain->sem   - GPU reset synchronization
 * 6. reset_lock          - Reset control lock
 * 7. srbm_mutex          - SRBM register access
 * 8. grbm_idx_mutex      - GRBM index access
 * 9. mmio_idx_lock       - MMIO index access (spinlock)
 *
 * Evidence:
 * - userq_sch_mutex -> userq_mutex: amdgpu_gfx_kfd_sch_ctrl() calls
 *   amdgpu_userq_stop_sched_for_enforce_isolation() which takes userq_mutex
 * - userq_mutex -> notifier_lock: userq paths may trigger MMU notifier
 *   invalidation which acquires notifier_lock
 * - notifier_lock -> reset_domain->sem: HMM invalidation callback holds
 *   notifier_lock and can wait for GPU reset completion, so notifier_lock
 *   must be outer to reset_domain->sem
 * - vram_lock -> reset_domain->sem: VRAM management paths may need to
 *   wait for ongoing reset to complete
 *
 * Note: mmap_lock ordering relative to GPU locks is already taught
 * by dma-resv (drivers/dma-buf/dma-resv.c).
 */
int amdgpu_lockdep_init(void)
{
	struct amdgpu_reset_domain *reset_domain = NULL;
	struct amdgpu_lockdep_dummy_locks *locks;
	unsigned long flags;

	locks = kzalloc(sizeof(*locks), GFP_KERNEL);
	if (!locks)
		return -ENOMEM;

	/*
	 * Initialize dummy reset domain
	 */
	reset_domain = amdgpu_reset_create_reset_domain(SINGLE_DEVICE,
							"lockdep_test");
	if (!reset_domain) {
		kfree(locks);
		return -ENOMEM;
	}
	/* Initialize dummy locks */
	mutex_init(&locks->userq_sch_mutex);
	mutex_init(&locks->userq_mutex);
	mutex_init(&locks->notifier_lock);
	mutex_init(&locks->vram_lock);
	mutex_init(&locks->reset_lock);
	mutex_init(&locks->srbm_mutex);
	mutex_init(&locks->grbm_idx_mutex);
	spin_lock_init(&locks->mmio_idx_lock);

	/*
	 * Associate dummy locks with the same class keys used for real
	 * driver locks. This ensures lockdep connects the ordering learned
	 * here with the actual locks used at runtime.
	 */
	lockdep_set_class(&locks->userq_sch_mutex, &amdgpu_userq_sch_mutex_key);
	lockdep_set_class(&locks->userq_mutex, &amdgpu_userq_mutex_key);
	lockdep_set_class(&locks->notifier_lock, &amdgpu_notifier_lock_key);
	lockdep_set_class(&locks->vram_lock, &amdgpu_vram_lock_key);
	lockdep_set_class(&reset_domain->sem, &amdgpu_reset_sem_key);
	lockdep_set_class(&locks->reset_lock, &amdgpu_reset_lock_key);
	lockdep_set_class(&locks->srbm_mutex, &amdgpu_srbm_lock_key);
	lockdep_set_class(&locks->grbm_idx_mutex, &amdgpu_grbm_lock_key);
	lockdep_set_class(&locks->mmio_idx_lock, &amdgpu_mmio_lock_key);
	/*
	 * Take locks in the correct order to train lockdep.
	 * This establishes the dependency chain.
	 */

	/* Level 1: Global userq scheduler mutex (outermost) */
	mutex_lock(&locks->userq_sch_mutex);

	/* Level 2: Per-context userq mutex */
	mutex_lock(&locks->userq_mutex);
	/* Level 3: MMU notifier lock */
	mutex_lock(&locks->notifier_lock);
	/* Level 4: VRAM allocator lock */
	mutex_lock(&locks->vram_lock);
	/* Level 5: Reset domain semaphore */
	down_read(&reset_domain->sem);

	/* Level 6: Reset control lock */
	mutex_lock(&locks->reset_lock);
	/*
	 * Mark potential memory reclaim boundary.
	 * GPU operations might trigger memory allocation/reclaim.
	 */
	fs_reclaim_acquire(GFP_KERNEL);

	/* Level 7: SRBM register access */
	mutex_lock(&locks->srbm_mutex);
	/* Level 8: GRBM index access */
	mutex_lock(&locks->grbm_idx_mutex);

	/* Level 9: MMIO index access (innermost lock, spinlock) */
	spin_lock_irqsave(&locks->mmio_idx_lock, flags);
	/*
	 * All locks acquired in order.
	 * Lockdep has now learned the valid dependency chain.
	 */

	/* Release in reverse order */
	spin_unlock_irqrestore(&locks->mmio_idx_lock, flags);
	mutex_unlock(&locks->grbm_idx_mutex);
	mutex_unlock(&locks->srbm_mutex);
	fs_reclaim_release(GFP_KERNEL);

	mutex_unlock(&locks->reset_lock);
	up_read(&reset_domain->sem);

	mutex_unlock(&locks->vram_lock);
	mutex_unlock(&locks->notifier_lock);
	mutex_unlock(&locks->userq_mutex);
	mutex_unlock(&locks->userq_sch_mutex);

	/* Cleanup */
	amdgpu_reset_put_reset_domain(reset_domain);

	kfree(locks);
	pr_info("AMDGPU: Lockdep annotations initialized (9 lock levels)\n");

	return 0;
}

#endif /* CONFIG_LOCKDEP */
