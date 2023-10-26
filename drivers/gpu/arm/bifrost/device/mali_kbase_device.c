// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2010-2023 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

/*
 * Base kernel device APIs
 */

#include <linux/debugfs.h>
#include <linux/dma-mapping.h>
#include <linux/seq_file.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of_platform.h>
#include <linux/types.h>
#include <linux/oom.h>

#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_hwaccess_instr.h>
#include <mali_kbase_hwaccess_time.h>
#include <mali_kbase_hw.h>
#include <mali_kbase_config_defaults.h>
#include <linux/priority_control_manager.h>

#include <tl/mali_kbase_timeline.h>
#include "mali_kbase_kinstr_prfcnt.h"
#include "mali_kbase_vinstr.h"
#include "hwcnt/mali_kbase_hwcnt_context.h"
#include "hwcnt/mali_kbase_hwcnt_virtualizer.h"

#include "mali_kbase_device.h"
#include "mali_kbase_device_internal.h"
#include "backend/gpu/mali_kbase_pm_internal.h"
#include "backend/gpu/mali_kbase_irq_internal.h"
#include "mali_kbase_regs_history_debugfs.h"
#include "mali_kbase_pbha.h"

#ifdef CONFIG_MALI_ARBITER_SUPPORT
#include "arbiter/mali_kbase_arbiter_pm.h"
#endif /* CONFIG_MALI_ARBITER_SUPPORT */

#if defined(CONFIG_DEBUG_FS) && !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)

/* Number of register accesses for the buffer that we allocate during
 * initialization time. The buffer size can be changed later via debugfs.
 */
#define KBASEP_DEFAULT_REGISTER_HISTORY_SIZE ((u16)512)

#endif /* defined(CONFIG_DEBUG_FS) && !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI) */

static DEFINE_MUTEX(kbase_dev_list_lock);
static LIST_HEAD(kbase_dev_list);
static int kbase_dev_nr;

struct kbase_device *kbase_device_alloc(void)
{
	return kzalloc(sizeof(struct kbase_device), GFP_KERNEL);
}

/**
 * kbase_device_all_as_init() - Initialise address space objects of the device.
 *
 * @kbdev: Pointer to kbase device.
 *
 * Return: 0 on success otherwise non-zero.
 */
static int kbase_device_all_as_init(struct kbase_device *kbdev)
{
	int i, err = 0;

	for (i = 0; i < kbdev->nr_hw_address_spaces; i++) {
		err = kbase_mmu_as_init(kbdev, i);
		if (err)
			break;
	}

	if (err) {
		while (i-- > 0)
			kbase_mmu_as_term(kbdev, i);
	}

	return err;
}

static void kbase_device_all_as_term(struct kbase_device *kbdev)
{
	int i;

	for (i = 0; i < kbdev->nr_hw_address_spaces; i++)
		kbase_mmu_as_term(kbdev, i);
}

int kbase_device_pcm_dev_init(struct kbase_device *const kbdev)
{
	int err = 0;

#if IS_ENABLED(CONFIG_OF)
	struct device_node *prio_ctrl_node;

	/* Check to see whether or not a platform specific priority control manager
	 * is available.
	 */
	prio_ctrl_node = of_parse_phandle(kbdev->dev->of_node,
			"priority-control-manager", 0);
	if (!prio_ctrl_node) {
		dev_info(kbdev->dev,
			"No priority control manager is configured");
	} else {
		struct platform_device *const pdev =
			of_find_device_by_node(prio_ctrl_node);

		if (!pdev) {
			dev_err(kbdev->dev,
				"The configured priority control manager was not found");
		} else {
			struct priority_control_manager_device *pcm_dev =
						platform_get_drvdata(pdev);
			if (!pcm_dev) {
				dev_info(kbdev->dev, "Priority control manager is not ready");
				err = -EPROBE_DEFER;
			} else if (!try_module_get(pcm_dev->owner)) {
				dev_err(kbdev->dev, "Failed to get priority control manager module");
				err = -ENODEV;
			} else {
				dev_info(kbdev->dev, "Priority control manager successfully loaded");
				kbdev->pcm_dev = pcm_dev;
			}
		}
		of_node_put(prio_ctrl_node);
	}
#endif /* CONFIG_OF */

	return err;
}

void kbase_device_pcm_dev_term(struct kbase_device *const kbdev)
{
	if (kbdev->pcm_dev)
		module_put(kbdev->pcm_dev->owner);
}

#define KBASE_PAGES_TO_KIB(pages) (((unsigned int)pages) << (PAGE_SHIFT - 10))

/**
 * mali_oom_notifier_handler - Mali driver out-of-memory handler
 *
 * @nb: notifier block - used to retrieve kbdev pointer
 * @action: action (unused)
 * @data: data pointer (unused)
 *
 * This function simply lists memory usage by the Mali driver, per GPU device,
 * for diagnostic purposes.
 *
 * Return: NOTIFY_OK on success, NOTIFY_BAD otherwise.
 */
static int mali_oom_notifier_handler(struct notifier_block *nb,
				     unsigned long action, void *data)
{
	struct kbase_device *kbdev;
	struct kbase_context *kctx = NULL;
	unsigned long kbdev_alloc_total;

	if (WARN_ON(nb == NULL))
		return NOTIFY_BAD;

	kbdev = container_of(nb, struct kbase_device, oom_notifier_block);

	kbdev_alloc_total =
		KBASE_PAGES_TO_KIB(atomic_read(&(kbdev->memdev.used_pages)));

	dev_err(kbdev->dev, "OOM notifier: dev %s  %lu kB\n", kbdev->devname,
		kbdev_alloc_total);

	mutex_lock(&kbdev->kctx_list_lock);

	list_for_each_entry(kctx, &kbdev->kctx_list, kctx_list_link) {
		struct pid *pid_struct;
		struct task_struct *task;
		struct pid *tgid_struct;
		struct task_struct *tgid_task;

		unsigned long task_alloc_total =
			KBASE_PAGES_TO_KIB(atomic_read(&(kctx->used_pages)));

		rcu_read_lock();
		pid_struct = find_get_pid(kctx->pid);
		task = pid_task(pid_struct, PIDTYPE_PID);
		tgid_struct = find_get_pid(kctx->tgid);
		tgid_task = pid_task(tgid_struct, PIDTYPE_PID);

		dev_err(kbdev->dev,
			"OOM notifier: tsk %s:%s  tgid (%u)  pid (%u) %lu kB\n",
			tgid_task ? tgid_task->comm : "[null task]",
			task ? task->comm : "[null comm]", kctx->tgid,
			kctx->pid, task_alloc_total);

		put_pid(pid_struct);
		rcu_read_unlock();
	}

	mutex_unlock(&kbdev->kctx_list_lock);
	return NOTIFY_OK;
}

int kbase_device_misc_init(struct kbase_device * const kbdev)
{
	int err;
#if IS_ENABLED(CONFIG_ARM64)
	struct device_node *np = NULL;
#endif /* CONFIG_ARM64 */

	spin_lock_init(&kbdev->mmu_mask_change);
	mutex_init(&kbdev->mmu_hw_mutex);
#if IS_ENABLED(CONFIG_ARM64)
	kbdev->cci_snoop_enabled = false;
	np = kbdev->dev->of_node;
	if (np != NULL) {
		if (of_property_read_u32(np, "snoop_enable_smc",
					&kbdev->snoop_enable_smc))
			kbdev->snoop_enable_smc = 0;
		if (of_property_read_u32(np, "snoop_disable_smc",
					&kbdev->snoop_disable_smc))
			kbdev->snoop_disable_smc = 0;
		/* Either both or none of the calls should be provided. */
		if (!((kbdev->snoop_disable_smc == 0
			&& kbdev->snoop_enable_smc == 0)
			|| (kbdev->snoop_disable_smc != 0
			&& kbdev->snoop_enable_smc != 0))) {
			WARN_ON(1);
			err = -EINVAL;
			goto fail;
		}
	}
#endif /* CONFIG_ARM64 */

	/* Get the list of workarounds for issues on the current HW
	 * (identified by the GPU_ID register)
	 */
	err = kbase_hw_set_issues_mask(kbdev);
	if (err)
		goto fail;

	/* Set the list of features available on the current HW
	 * (identified by the GPU_ID register)
	 */
	kbase_hw_set_features_mask(kbdev);

	err = kbase_gpuprops_set_features(kbdev);
	if (err)
		goto fail;

	/* Workaround a pre-3.13 Linux issue, where dma_mask is NULL when our
	 * device structure was created by device-tree
	 */
	if (!kbdev->dev->dma_mask)
		kbdev->dev->dma_mask = &kbdev->dev->coherent_dma_mask;

	err = dma_set_mask(kbdev->dev,
			DMA_BIT_MASK(kbdev->gpu_props.mmu.pa_bits));
	if (err)
		goto dma_set_mask_failed;

	err = dma_set_coherent_mask(kbdev->dev,
			DMA_BIT_MASK(kbdev->gpu_props.mmu.pa_bits));
	if (err)
		goto dma_set_mask_failed;


	/* There is no limit for Mali, so set to max. */
	if (kbdev->dev->dma_parms)
		err = dma_set_max_seg_size(kbdev->dev, UINT_MAX);
	if (err)
		goto dma_set_mask_failed;

	kbdev->nr_hw_address_spaces = kbdev->gpu_props.num_address_spaces;

	err = kbase_device_all_as_init(kbdev);
	if (err)
		goto dma_set_mask_failed;

	err = kbase_pbha_read_dtb(kbdev);
	if (err)
		goto term_as;

	init_waitqueue_head(&kbdev->cache_clean_wait);

	kbase_debug_assert_register_hook(&kbase_ktrace_hook_wrapper, kbdev);

	atomic_set(&kbdev->ctx_num, 0);

	kbdev->pm.dvfs_period = DEFAULT_PM_DVFS_PERIOD;

#if MALI_USE_CSF
	kbdev->reset_timeout_ms = kbase_get_timeout_ms(kbdev, CSF_CSG_SUSPEND_TIMEOUT);
#else
	kbdev->reset_timeout_ms = JM_DEFAULT_RESET_TIMEOUT_MS;
#endif /* MALI_USE_CSF */

	kbdev->mmu_mode = kbase_mmu_mode_get_aarch64();
	kbdev->mmu_as_inactive_wait_time_ms =
		kbase_get_timeout_ms(kbdev, MMU_AS_INACTIVE_WAIT_TIMEOUT);
	mutex_init(&kbdev->kctx_list_lock);
	INIT_LIST_HEAD(&kbdev->kctx_list);

	dev_dbg(kbdev->dev, "Registering mali_oom_notifier_handlern");
	kbdev->oom_notifier_block.notifier_call = mali_oom_notifier_handler;
	err = register_oom_notifier(&kbdev->oom_notifier_block);

	if (err) {
		dev_err(kbdev->dev,
			"Unable to register OOM notifier for Mali - but will continue\n");
		kbdev->oom_notifier_block.notifier_call = NULL;
	}

#if !MALI_USE_CSF
	spin_lock_init(&kbdev->quick_reset_lock);
	kbdev->quick_reset_enabled = true;
	kbdev->num_of_atoms_hw_completed = 0;
#endif

#if MALI_USE_CSF && IS_ENABLED(CONFIG_SYNC_FILE)
	atomic_set(&kbdev->live_fence_metadata, 0);
#endif
	return 0;

term_as:
	kbase_device_all_as_term(kbdev);
dma_set_mask_failed:
fail:
	return err;
}

void kbase_device_misc_term(struct kbase_device *kbdev)
{
	KBASE_DEBUG_ASSERT(kbdev);

	WARN_ON(!list_empty(&kbdev->kctx_list));

#if KBASE_KTRACE_ENABLE
	kbase_debug_assert_register_hook(NULL, NULL);
#endif
	kbase_device_all_as_term(kbdev);


	if (kbdev->oom_notifier_block.notifier_call)
		unregister_oom_notifier(&kbdev->oom_notifier_block);

#if MALI_USE_CSF && IS_ENABLED(CONFIG_SYNC_FILE)
	if (atomic_read(&kbdev->live_fence_metadata) > 0)
		dev_warn(kbdev->dev, "Terminating Kbase device with live fence metadata!");
#endif
}

#if !MALI_USE_CSF
void kbase_enable_quick_reset(struct kbase_device *kbdev)
{
	spin_lock(&kbdev->quick_reset_lock);

	kbdev->quick_reset_enabled = true;
	kbdev->num_of_atoms_hw_completed = 0;

	spin_unlock(&kbdev->quick_reset_lock);
}

void kbase_disable_quick_reset(struct kbase_device *kbdev)
{
	spin_lock(&kbdev->quick_reset_lock);

	kbdev->quick_reset_enabled = false;
	kbdev->num_of_atoms_hw_completed = 0;

	spin_unlock(&kbdev->quick_reset_lock);
}

bool kbase_is_quick_reset_enabled(struct kbase_device *kbdev)
{
	return kbdev->quick_reset_enabled;
}
#endif

void kbase_device_free(struct kbase_device *kbdev)
{
	kfree(kbdev);
}

void kbase_device_id_init(struct kbase_device *kbdev)
{
	scnprintf(kbdev->devname, DEVNAME_SIZE, "%s%d", kbase_drv_name,
			kbase_dev_nr);
	kbdev->id = kbase_dev_nr;
}

void kbase_increment_device_id(void)
{
	kbase_dev_nr++;
}

int kbase_device_hwcnt_context_init(struct kbase_device *kbdev)
{
	return kbase_hwcnt_context_init(&kbdev->hwcnt_gpu_iface,
			&kbdev->hwcnt_gpu_ctx);
}

void kbase_device_hwcnt_context_term(struct kbase_device *kbdev)
{
	kbase_hwcnt_context_term(kbdev->hwcnt_gpu_ctx);
}

int kbase_device_hwcnt_virtualizer_init(struct kbase_device *kbdev)
{
	return kbase_hwcnt_virtualizer_init(kbdev->hwcnt_gpu_ctx,
			KBASE_HWCNT_GPU_VIRTUALIZER_DUMP_THRESHOLD_NS,
			&kbdev->hwcnt_gpu_virt);
}

void kbase_device_hwcnt_virtualizer_term(struct kbase_device *kbdev)
{
	kbase_hwcnt_virtualizer_term(kbdev->hwcnt_gpu_virt);
}

int kbase_device_timeline_init(struct kbase_device *kbdev)
{
	atomic_set(&kbdev->timeline_flags, 0);
	return kbase_timeline_init(&kbdev->timeline, &kbdev->timeline_flags);
}

void kbase_device_timeline_term(struct kbase_device *kbdev)
{
	kbase_timeline_term(kbdev->timeline);
}

int kbase_device_vinstr_init(struct kbase_device *kbdev)
{
	return kbase_vinstr_init(kbdev->hwcnt_gpu_virt, &kbdev->vinstr_ctx);
}

void kbase_device_vinstr_term(struct kbase_device *kbdev)
{
	kbase_vinstr_term(kbdev->vinstr_ctx);
}

int kbase_device_kinstr_prfcnt_init(struct kbase_device *kbdev)
{
	return kbase_kinstr_prfcnt_init(kbdev->hwcnt_gpu_virt,
					&kbdev->kinstr_prfcnt_ctx);
}

void kbase_device_kinstr_prfcnt_term(struct kbase_device *kbdev)
{
	kbase_kinstr_prfcnt_term(kbdev->kinstr_prfcnt_ctx);
}

int kbase_device_io_history_init(struct kbase_device *kbdev)
{
	return kbase_io_history_init(&kbdev->io_history,
			KBASEP_DEFAULT_REGISTER_HISTORY_SIZE);
}

void kbase_device_io_history_term(struct kbase_device *kbdev)
{
	kbase_io_history_term(&kbdev->io_history);
}

int kbase_device_misc_register(struct kbase_device *kbdev)
{
	return misc_register(&kbdev->mdev);
}

void kbase_device_misc_deregister(struct kbase_device *kbdev)
{
	misc_deregister(&kbdev->mdev);
}

int kbase_device_list_init(struct kbase_device *kbdev)
{
	const struct list_head *dev_list;

	dev_list = kbase_device_get_list();
	list_add(&kbdev->entry, &kbase_dev_list);
	kbase_device_put_list(dev_list);

	return 0;
}

void kbase_device_list_term(struct kbase_device *kbdev)
{
	const struct list_head *dev_list;

	dev_list = kbase_device_get_list();
	list_del(&kbdev->entry);
	kbase_device_put_list(dev_list);
}

const struct list_head *kbase_device_get_list(void)
{
	mutex_lock(&kbase_dev_list_lock);
	return &kbase_dev_list;
}
KBASE_EXPORT_TEST_API(kbase_device_get_list);

void kbase_device_put_list(const struct list_head *dev_list)
{
	mutex_unlock(&kbase_dev_list_lock);
}
KBASE_EXPORT_TEST_API(kbase_device_put_list);

int kbase_device_early_init(struct kbase_device *kbdev)
{
	int err;

	err = kbase_ktrace_init(kbdev);
	if (err)
		return err;


	err = kbasep_platform_device_init(kbdev);
	if (err)
		goto ktrace_term;

	err = kbase_pm_runtime_init(kbdev);
	if (err)
		goto fail_runtime_pm;

	/* This spinlock is initialized before doing the first access to GPU
	 * registers and installing interrupt handlers.
	 */
	spin_lock_init(&kbdev->hwaccess_lock);

	/* Ensure we can access the GPU registers */
	kbase_pm_register_access_enable(kbdev);

	/*
	 * Find out GPU properties based on the GPU feature registers.
	 * Note that this does not populate the few properties that depend on
	 * hw_features being initialized. Those are set by kbase_gpuprops_set_features
	 * soon after this in the init process.
	 */
	kbase_gpuprops_set(kbdev);

	/* We're done accessing the GPU registers for now. */
	kbase_pm_register_access_disable(kbdev);

#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbdev->arb.arb_if)
		err = kbase_arbiter_pm_install_interrupts(kbdev);
	else
		err = kbase_install_interrupts(kbdev);
#else
	err = kbase_install_interrupts(kbdev);
#endif
	if (err)
		goto fail_interrupts;

	return 0;

fail_interrupts:
	kbase_pm_runtime_term(kbdev);
fail_runtime_pm:
	kbasep_platform_device_term(kbdev);
ktrace_term:
	kbase_ktrace_term(kbdev);

	return err;
}

void kbase_device_early_term(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_ARBITER_SUPPORT
	if (kbdev->arb.arb_if)
		kbase_arbiter_pm_release_interrupts(kbdev);
	else
		kbase_release_interrupts(kbdev);
#else
	kbase_release_interrupts(kbdev);
#endif /* CONFIG_MALI_ARBITER_SUPPORT */
	kbase_pm_runtime_term(kbdev);
	kbasep_platform_device_term(kbdev);
	kbase_ktrace_term(kbdev);
}

int kbase_device_late_init(struct kbase_device *kbdev)
{
	int err;

	err = kbasep_platform_device_late_init(kbdev);

	return err;
}

void kbase_device_late_term(struct kbase_device *kbdev)
{
	kbasep_platform_device_late_term(kbdev);
}
