// SPDX-License-Identifier: GPL-2.0
/*
 *
 * (C) COPYRIGHT 2010-2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
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

#include <mali_kbase.h>
#include <mali_kbase_defs.h>
#include <mali_kbase_hwaccess_instr.h>
#include <mali_kbase_hw.h>
#include <mali_kbase_config_defaults.h>

#include <tl/mali_kbase_timeline.h>
#include "mali_kbase_vinstr.h"
#include "mali_kbase_hwcnt_context.h"
#include "mali_kbase_hwcnt_virtualizer.h"

#include "mali_kbase_device.h"
#include "backend/gpu/mali_kbase_pm_internal.h"
#include "backend/gpu/mali_kbase_irq_internal.h"
#include "mali_kbase_regs_history_debugfs.h"

#ifdef CONFIG_MALI_ARBITER_SUPPORT
#include "arbiter/mali_kbase_arbiter_pm.h"
#endif /* CONFIG_MALI_ARBITER_SUPPORT */

/* NOTE: Magic - 0x45435254 (TRCE in ASCII).
 * Supports tracing feature provided in the base module.
 * Please keep it in sync with the value of base module.
 */
#define TRACE_BUFFER_HEADER_SPECIAL 0x45435254

/* Number of register accesses for the buffer that we allocate during
 * initialization time. The buffer size can be changed later via debugfs.
 */
#define KBASEP_DEFAULT_REGISTER_HISTORY_SIZE ((u16)512)

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

int kbase_device_misc_init(struct kbase_device * const kbdev)
{
	int err;
#ifdef CONFIG_ARM64
	struct device_node *np = NULL;
#endif /* CONFIG_ARM64 */

	spin_lock_init(&kbdev->mmu_mask_change);
	mutex_init(&kbdev->mmu_hw_mutex);
#ifdef CONFIG_ARM64
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

	/* On Linux 4.0+, dma coherency is determined from device tree */
#if defined(CONFIG_ARM64) && LINUX_VERSION_CODE < KERNEL_VERSION(4, 0, 0)
	set_dma_ops(kbdev->dev, &noncoherent_swiotlb_dma_ops);
#endif

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

	kbdev->nr_hw_address_spaces = kbdev->gpu_props.num_address_spaces;

	err = kbase_device_all_as_init(kbdev);
	if (err)
		goto dma_set_mask_failed;

	spin_lock_init(&kbdev->hwcnt.lock);

	err = kbase_ktrace_init(kbdev);
	if (err)
		goto term_as;

	init_waitqueue_head(&kbdev->cache_clean_wait);

	kbase_debug_assert_register_hook(&kbase_ktrace_hook_wrapper, kbdev);

	atomic_set(&kbdev->ctx_num, 0);

	err = kbase_instr_backend_init(kbdev);
	if (err)
		goto term_trace;

	kbdev->pm.dvfs_period = DEFAULT_PM_DVFS_PERIOD;

	kbdev->reset_timeout_ms = DEFAULT_RESET_TIMEOUT_MS;

	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_AARCH64_MMU))
		kbdev->mmu_mode = kbase_mmu_mode_get_aarch64();
	else
		kbdev->mmu_mode = kbase_mmu_mode_get_lpae();

	mutex_init(&kbdev->kctx_list_lock);
	INIT_LIST_HEAD(&kbdev->kctx_list);

	spin_lock_init(&kbdev->hwaccess_lock);

	return 0;
term_trace:
	kbase_ktrace_term(kbdev);
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

	kbase_instr_backend_term(kbdev);

	kbase_ktrace_term(kbdev);

	kbase_device_all_as_term(kbdev);
}

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

int kbase_device_hwcnt_backend_jm_init(struct kbase_device *kbdev)
{
	return kbase_hwcnt_backend_jm_create(kbdev, &kbdev->hwcnt_gpu_iface);
}

void kbase_device_hwcnt_backend_jm_term(struct kbase_device *kbdev)
{
	kbase_hwcnt_backend_jm_destroy(&kbdev->hwcnt_gpu_iface);
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

	err = kbasep_platform_device_init(kbdev);
	if (err)
		return err;

	err = kbase_pm_runtime_init(kbdev);
	if (err)
		goto fail_runtime_pm;

	/* Ensure we can access the GPU registers */
	kbase_pm_register_access_enable(kbdev);

	/* Find out GPU properties based on the GPU feature registers */
	kbase_gpuprops_set(kbdev);

	/* We're done accessing the GPU registers for now. */
	kbase_pm_register_access_disable(kbdev);

	err = kbase_install_interrupts(kbdev);
	if (err)
		goto fail_interrupts;

	return 0;

fail_interrupts:
	kbase_pm_runtime_term(kbdev);
fail_runtime_pm:
	kbasep_platform_device_term(kbdev);

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
}
