/*
 *
 * (C) COPYRIGHT 2010-2017 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
 *
 * A copy of the licence is included with the program, and can also be obtained
 * from Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
 * Boston, MA  02110-1301, USA.
 *
 */



#include <mali_kbase.h>
#include <mali_kbase_config_defaults.h>
#include <mali_kbase_uku.h>
#include <mali_midg_regmap.h>
#include <mali_kbase_gator.h>
#include <mali_kbase_mem_linux.h>
#ifdef CONFIG_MALI_BIFROST_DEVFREQ
#include <linux/devfreq.h>
#include <backend/gpu/mali_kbase_devfreq.h>
#ifdef CONFIG_DEVFREQ_THERMAL
#include <ipa/mali_kbase_ipa_debugfs.h>
#endif /* CONFIG_DEVFREQ_THERMAL */
#endif /* CONFIG_MALI_BIFROST_DEVFREQ */
#ifdef CONFIG_MALI_BIFROST_NO_MALI
#include "mali_kbase_model_linux.h"
#endif /* CONFIG_MALI_BIFROST_NO_MALI */
#include "mali_kbase_mem_profile_debugfs_buf_size.h"
#include "mali_kbase_debug_mem_view.h"
#include "mali_kbase_mem.h"
#include "mali_kbase_mem_pool_debugfs.h"
#if !MALI_CUSTOMER_RELEASE
#include "mali_kbase_regs_dump_debugfs.h"
#endif /* !MALI_CUSTOMER_RELEASE */
#include "mali_kbase_regs_history_debugfs.h"
#include <mali_kbase_hwaccess_backend.h>
#include <mali_kbase_hwaccess_jm.h>
#include <mali_kbase_ctx_sched.h>
#include <backend/gpu/mali_kbase_device_internal.h>
#include "mali_kbase_ioctl.h"

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/semaphore.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/mm.h>
#include <linux/compat.h>	/* is_compat_task */
#include <linux/mman.h>
#include <linux/version.h>
#include <mali_kbase_hw.h>
#include <platform/mali_kbase_platform_common.h>
#if defined(CONFIG_SYNC) || defined(CONFIG_SYNC_FILE)
#include <mali_kbase_sync.h>
#endif /* CONFIG_SYNC || CONFIG_SYNC_FILE */
#include <linux/clk.h>
#include <linux/delay.h>

#include <mali_kbase_config.h>


#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 13, 0))
#include <linux/pm_opp.h>
#else
#include <linux/opp.h>
#endif

#include <mali_kbase_tlstream.h>

#include <mali_kbase_as_fault_debugfs.h>

/* GPU IRQ Tags */
#define	JOB_IRQ_TAG	0
#define MMU_IRQ_TAG	1
#define GPU_IRQ_TAG	2

#if MALI_UNIT_TEST
static struct kbase_exported_test_data shared_kernel_test_data;
EXPORT_SYMBOL(shared_kernel_test_data);
#endif /* MALI_UNIT_TEST */

static int kbase_dev_nr;

static DEFINE_MUTEX(kbase_dev_list_lock);
static LIST_HEAD(kbase_dev_list);

#define KERNEL_SIDE_DDK_VERSION_STRING "K:" MALI_RELEASE_NAME "(GPL)"
static inline void __compile_time_asserts(void)
{
	CSTD_COMPILE_TIME_ASSERT(sizeof(KERNEL_SIDE_DDK_VERSION_STRING) <= KBASE_GET_VERSION_BUFFER_SIZE);
}

static int kbase_api_handshake(struct kbase_context *kctx,
		struct kbase_ioctl_version_check *version)
{
	switch (version->major) {
	case BASE_UK_VERSION_MAJOR:
		/* set minor to be the lowest common */
		version->minor = min_t(int, BASE_UK_VERSION_MINOR,
				(int)version->minor);
		break;
	default:
		/* We return our actual version regardless if it
		 * matches the version returned by userspace -
		 * userspace can bail if it can't handle this
		 * version */
		version->major = BASE_UK_VERSION_MAJOR;
		version->minor = BASE_UK_VERSION_MINOR;
		break;
	}

	/* save the proposed version number for later use */
	kctx->api_version = KBASE_API_VERSION(version->major, version->minor);

	return 0;
}

/**
 * enum mali_error - Mali error codes shared with userspace
 *
 * This is subset of those common Mali errors that can be returned to userspace.
 * Values of matching user and kernel space enumerators MUST be the same.
 * MALI_ERROR_NONE is guaranteed to be 0.
 *
 * @MALI_ERROR_NONE: Success
 * @MALI_ERROR_OUT_OF_GPU_MEMORY: Not used in the kernel driver
 * @MALI_ERROR_OUT_OF_MEMORY: Memory allocation failure
 * @MALI_ERROR_FUNCTION_FAILED: Generic error code
 */
enum mali_error {
	MALI_ERROR_NONE = 0,
	MALI_ERROR_OUT_OF_GPU_MEMORY,
	MALI_ERROR_OUT_OF_MEMORY,
	MALI_ERROR_FUNCTION_FAILED,
};

enum {
	inited_mem = (1u << 0),
	inited_js = (1u << 1),
	inited_pm_runtime_init = (1u << 2),
#ifdef CONFIG_MALI_BIFROST_DEVFREQ
	inited_devfreq = (1u << 3),
#endif /* CONFIG_MALI_BIFROST_DEVFREQ */
	inited_tlstream = (1u << 4),
	inited_backend_early = (1u << 5),
	inited_backend_late = (1u << 6),
	inited_device = (1u << 7),
	inited_vinstr = (1u << 8),

	inited_job_fault = (1u << 10),
	inited_sysfs_group = (1u << 11),
	inited_misc_register = (1u << 12),
	inited_get_device = (1u << 13),
	inited_dev_list = (1u << 14),
	inited_debugfs = (1u << 15),
	inited_gpu_device = (1u << 16),
	inited_registers_map = (1u << 17),
	inited_io_history = (1u << 18),
	inited_power_control = (1u << 19),
	inited_buslogger = (1u << 20),
	inited_protected = (1u << 21),
	inited_ctx_sched = (1u << 22)
};


#ifdef CONFIG_MALI_BIFROST_DEBUG
#define INACTIVE_WAIT_MS (5000)

void kbase_set_driver_inactive(struct kbase_device *kbdev, bool inactive)
{
	kbdev->driver_inactive = inactive;
	wake_up(&kbdev->driver_inactive_wait);

	/* Wait for any running IOCTLs to complete */
	if (inactive)
		msleep(INACTIVE_WAIT_MS);
}
KBASE_EXPORT_TEST_API(kbase_set_driver_inactive);
#endif /* CONFIG_MALI_BIFROST_DEBUG */

/**
 * kbase_legacy_dispatch - UKK dispatch function
 *
 * This is the dispatch function for the legacy UKK ioctl interface. No new
 * ioctls should be added to this function, see kbase_ioctl instead.
 *
 * @kctx: The kernel context structure
 * @args: Pointer to the data structure passed from/to user space
 * @args_size: Size of the data structure
 */
static int kbase_legacy_dispatch(struct kbase_context *kctx,
		void * const args, u32 args_size)
{
	struct kbase_device *kbdev;
	union uk_header *ukh = args;
	u32 id;
	int ret = 0;

	KBASE_DEBUG_ASSERT(ukh != NULL);

	kbdev = kctx->kbdev;
	id = ukh->id;
	ukh->ret = MALI_ERROR_NONE; /* Be optimistic */

#ifdef CONFIG_MALI_BIFROST_DEBUG
	wait_event(kbdev->driver_inactive_wait,
			kbdev->driver_inactive == false);
#endif /* CONFIG_MALI_BIFROST_DEBUG */

	if (UKP_FUNC_ID_CHECK_VERSION == id) {
		struct uku_version_check_args *version_check;
		struct kbase_ioctl_version_check version;

		if (args_size != sizeof(struct uku_version_check_args)) {
			ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			return 0;
		}
		version_check = (struct uku_version_check_args *)args;
		version.minor = version_check->minor;
		version.major = version_check->major;

		kbase_api_handshake(kctx, &version);

		version_check->minor = version.minor;
		version_check->major = version.major;
		ukh->ret = MALI_ERROR_NONE;
		return 0;
	}

	/* block calls until version handshake */
	if (kctx->api_version == 0)
		return -EINVAL;

	if (!atomic_read(&kctx->setup_complete)) {
		struct kbase_uk_set_flags *kbase_set_flags;

		/* setup pending, try to signal that we'll do the setup,
		 * if setup was already in progress, err this call
		 */
		if (atomic_cmpxchg(&kctx->setup_in_progress, 0, 1) != 0)
			return -EINVAL;

		/* if unexpected call, will stay stuck in setup mode
		 * (is it the only call we accept?)
		 */
		if (id != KBASE_FUNC_SET_FLAGS)
			return -EINVAL;

		kbase_set_flags = (struct kbase_uk_set_flags *)args;

		/* if not matching the expected call, stay in setup mode */
		if (sizeof(*kbase_set_flags) != args_size)
			goto bad_size;

		/* if bad flags, will stay stuck in setup mode */
		if (kbase_context_set_create_flags(kctx,
				kbase_set_flags->create_flags) != 0)
			ukh->ret = MALI_ERROR_FUNCTION_FAILED;

		atomic_set(&kctx->setup_complete, 1);
		return 0;
	}

	/* setup complete, perform normal operation */
	switch (id) {
	case KBASE_FUNC_MEM_JIT_INIT:
		{
			struct kbase_uk_mem_jit_init *jit_init = args;

			if (sizeof(*jit_init) != args_size)
				goto bad_size;

			if (kbase_region_tracker_init_jit(kctx,
					jit_init->va_pages))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}
	case KBASE_FUNC_MEM_ALLOC:
		{
			struct kbase_uk_mem_alloc *mem = args;
			struct kbase_va_region *reg;

			if (sizeof(*mem) != args_size)
				goto bad_size;

#if defined(CONFIG_64BIT)
			if (!kbase_ctx_flag(kctx, KCTX_COMPAT)) {
				/* force SAME_VA if a 64-bit client */
				mem->flags |= BASE_MEM_SAME_VA;
			}
#endif

			reg = kbase_mem_alloc(kctx, mem->va_pages,
					mem->commit_pages, mem->extent,
					&mem->flags, &mem->gpu_va);
			mem->va_alignment = 0;

			if (!reg)
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}
	case KBASE_FUNC_MEM_IMPORT: {
			struct kbase_uk_mem_import *mem_import = args;
			void __user *phandle;

			if (sizeof(*mem_import) != args_size)
				goto bad_size;
#ifdef CONFIG_COMPAT
			if (kbase_ctx_flag(kctx, KCTX_COMPAT))
				phandle = compat_ptr(mem_import->phandle);
			else
#endif
				phandle = u64_to_user_ptr(mem_import->phandle);

			if (mem_import->type == BASE_MEM_IMPORT_TYPE_INVALID) {
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				break;
			}

			if (kbase_mem_import(kctx,
					(enum base_mem_import_type)
					mem_import->type,
					phandle,
					0,
					&mem_import->gpu_va,
					&mem_import->va_pages,
					&mem_import->flags)) {
				mem_import->type = BASE_MEM_IMPORT_TYPE_INVALID;
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			}
			break;
	}
	case KBASE_FUNC_MEM_ALIAS: {
			struct kbase_uk_mem_alias *alias = args;
			struct base_mem_aliasing_info __user *user_ai;
			struct base_mem_aliasing_info *ai;

			if (sizeof(*alias) != args_size)
				goto bad_size;

			if (alias->nents > 2048) {
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				break;
			}
			if (!alias->nents) {
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				break;
			}

#ifdef CONFIG_COMPAT
			if (kbase_ctx_flag(kctx, KCTX_COMPAT))
				user_ai = compat_ptr(alias->ai);
			else
#endif
				user_ai = u64_to_user_ptr(alias->ai);

			ai = vmalloc(sizeof(*ai) * alias->nents);

			if (!ai) {
				ukh->ret = MALI_ERROR_OUT_OF_MEMORY;
				break;
			}

			if (copy_from_user(ai, user_ai,
					   sizeof(*ai) * alias->nents)) {
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				goto copy_failed;
			}

			alias->gpu_va = kbase_mem_alias(kctx, &alias->flags,
							alias->stride,
							alias->nents, ai,
							&alias->va_pages);
			if (!alias->gpu_va) {
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				goto no_alias;
			}
no_alias:
copy_failed:
			vfree(ai);
			break;
		}
	case KBASE_FUNC_MEM_COMMIT:
		{
			struct kbase_uk_mem_commit *commit = args;
			int ret;

			if (sizeof(*commit) != args_size)
				goto bad_size;

			ret = kbase_mem_commit(kctx, commit->gpu_addr,
					commit->pages);

			ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			commit->result_subcode =
				BASE_BACKING_THRESHOLD_ERROR_INVALID_ARGUMENTS;

			if (ret == 0) {
				ukh->ret = MALI_ERROR_NONE;
				commit->result_subcode =
					BASE_BACKING_THRESHOLD_OK;
			} else if (ret == -ENOMEM) {
				commit->result_subcode =
					BASE_BACKING_THRESHOLD_ERROR_OOM;
			}

			break;
		}

	case KBASE_FUNC_MEM_QUERY:
		{
			struct kbase_uk_mem_query *query = args;

			if (sizeof(*query) != args_size)
				goto bad_size;

			if (kbase_mem_query(kctx, query->gpu_addr,
					query->query, &query->value) != 0)
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			else
				ukh->ret = MALI_ERROR_NONE;
			break;
		}
		break;

	case KBASE_FUNC_MEM_FLAGS_CHANGE:
		{
			struct kbase_uk_mem_flags_change *fc = args;

			if (sizeof(*fc) != args_size)
				goto bad_size;

			if (kbase_mem_flags_change(kctx, fc->gpu_va,
					fc->flags, fc->mask) != 0)
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;

			break;
		}
	case KBASE_FUNC_MEM_FREE:
		{
			struct kbase_uk_mem_free *mem = args;

			if (sizeof(*mem) != args_size)
				goto bad_size;

			if (kbase_mem_free(kctx, mem->gpu_addr) != 0)
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}

	case KBASE_FUNC_JOB_SUBMIT:
		{
			struct kbase_uk_job_submit *job = args;
			char __user *user_buf;

			if (sizeof(*job) != args_size)
				goto bad_size;

#ifdef CONFIG_COMPAT
			if (kbase_ctx_flag(kctx, KCTX_COMPAT))
				user_buf = compat_ptr(job->addr);
			else
#endif
				user_buf = u64_to_user_ptr(job->addr);

			if (kbase_jd_submit(kctx, user_buf,
						job->nr_atoms,
						job->stride,
						false) != 0)
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}

	case KBASE_FUNC_SYNC:
		{
			struct kbase_uk_sync_now *sn = args;

			if (sizeof(*sn) != args_size)
				goto bad_size;

			if (kbase_sync_now(kctx, &sn->sset.basep_sset) != 0)
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}

	case KBASE_FUNC_DISJOINT_QUERY:
		{
			struct kbase_uk_disjoint_query *dquery = args;

			if (sizeof(*dquery) != args_size)
				goto bad_size;

			/* Get the disjointness counter value. */
			dquery->counter = kbase_disjoint_event_get(kctx->kbdev);
			break;
		}

	case KBASE_FUNC_POST_TERM:
		{
			kbase_event_close(kctx);
			break;
		}

	case KBASE_FUNC_HWCNT_SETUP:
		{
			struct kbase_uk_hwcnt_setup *setup = args;

			if (sizeof(*setup) != args_size)
				goto bad_size;

			mutex_lock(&kctx->vinstr_cli_lock);
			if (kbase_vinstr_legacy_hwc_setup(kbdev->vinstr_ctx,
					&kctx->vinstr_cli, setup) != 0)
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			mutex_unlock(&kctx->vinstr_cli_lock);
			break;
		}

	case KBASE_FUNC_HWCNT_DUMP:
		{
			/* args ignored */
			mutex_lock(&kctx->vinstr_cli_lock);
			if (kbase_vinstr_hwc_dump(kctx->vinstr_cli,
					BASE_HWCNT_READER_EVENT_MANUAL) != 0)
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			mutex_unlock(&kctx->vinstr_cli_lock);
			break;
		}

	case KBASE_FUNC_HWCNT_CLEAR:
		{
			/* args ignored */
			mutex_lock(&kctx->vinstr_cli_lock);
			if (kbase_vinstr_hwc_clear(kctx->vinstr_cli) != 0)
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			mutex_unlock(&kctx->vinstr_cli_lock);
			break;
		}

	case KBASE_FUNC_HWCNT_READER_SETUP:
		{
			struct kbase_uk_hwcnt_reader_setup *setup = args;

			if (sizeof(*setup) != args_size)
				goto bad_size;

			mutex_lock(&kctx->vinstr_cli_lock);
			if (kbase_vinstr_hwcnt_reader_setup(kbdev->vinstr_ctx,
					setup) != 0)
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			mutex_unlock(&kctx->vinstr_cli_lock);
			break;
		}

	case KBASE_FUNC_GPU_PROPS_REG_DUMP:
		{
			struct kbase_uk_gpuprops *setup = args;

			if (sizeof(*setup) != args_size)
				goto bad_size;

			if (kbase_gpuprops_uk_get_props(kctx, setup) != 0)
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}
	case KBASE_FUNC_FIND_CPU_OFFSET:
		{
			struct kbase_uk_find_cpu_offset *find = args;

			if (sizeof(*find) != args_size)
				goto bad_size;

			if (find->gpu_addr & ~PAGE_MASK) {
				dev_warn(kbdev->dev, "kbase_legacy_dispatch case KBASE_FUNC_FIND_CPU_OFFSET: find->gpu_addr: passed parameter is invalid");
				goto out_bad;
			}

			if (find->size > SIZE_MAX || find->cpu_addr > ULONG_MAX) {
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			} else {
				int err;

				err = kbasep_find_enclosing_cpu_mapping_offset(
						kctx,
						find->cpu_addr,
						find->size,
						&find->offset);

				if (err)
					ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			}
			break;
		}
	case KBASE_FUNC_GET_VERSION:
		{
			struct kbase_uk_get_ddk_version *get_version = (struct kbase_uk_get_ddk_version *)args;

			if (sizeof(*get_version) != args_size)
				goto bad_size;

			/* version buffer size check is made in compile time assert */
			memcpy(get_version->version_buffer, KERNEL_SIDE_DDK_VERSION_STRING, sizeof(KERNEL_SIDE_DDK_VERSION_STRING));
			get_version->version_string_size = sizeof(KERNEL_SIDE_DDK_VERSION_STRING);
			break;
		}

	case KBASE_FUNC_STREAM_CREATE:
		{
#if defined(CONFIG_SYNC) || defined(CONFIG_SYNC_FILE)
			struct kbase_uk_stream_create *screate = (struct kbase_uk_stream_create *)args;

			if (sizeof(*screate) != args_size)
				goto bad_size;

			if (strnlen(screate->name, sizeof(screate->name)) >= sizeof(screate->name)) {
				/* not NULL terminated */
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				break;
			}

			if (kbase_sync_fence_stream_create(screate->name,
							   &screate->fd) != 0)
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			else
				ukh->ret = MALI_ERROR_NONE;
#else /* CONFIG_SYNC || CONFIG_SYNC_FILE */
			ukh->ret = MALI_ERROR_FUNCTION_FAILED;
#endif /* CONFIG_SYNC || CONFIG_SYNC_FILE */
			break;
		}
	case KBASE_FUNC_FENCE_VALIDATE:
		{
#if defined(CONFIG_SYNC) || defined(CONFIG_SYNC_FILE)
			struct kbase_uk_fence_validate *fence_validate = (struct kbase_uk_fence_validate *)args;

			if (sizeof(*fence_validate) != args_size)
				goto bad_size;

			if (kbase_sync_fence_validate(fence_validate->fd) != 0)
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			else
				ukh->ret = MALI_ERROR_NONE;
#endif /* CONFIG_SYNC || CONFIG_SYNC_FILE */
			break;
		}

	case KBASE_FUNC_SET_TEST_DATA:
		{
#if MALI_UNIT_TEST
			struct kbase_uk_set_test_data *set_data = args;

			shared_kernel_test_data = set_data->test_data;
			shared_kernel_test_data.kctx = (uintptr_t)kctx;
			shared_kernel_test_data.mm = (uintptr_t)current->mm;
			ukh->ret = MALI_ERROR_NONE;
#endif /* MALI_UNIT_TEST */
			break;
		}

	case KBASE_FUNC_INJECT_ERROR:
		{
#ifdef CONFIG_MALI_BIFROST_ERROR_INJECT
			unsigned long flags;
			struct kbase_error_params params = ((struct kbase_uk_error_params *)args)->params;

			/*mutex lock */
			spin_lock_irqsave(&kbdev->reg_op_lock, flags);
			if (job_atom_inject_error(&params) != 0)
				ukh->ret = MALI_ERROR_OUT_OF_MEMORY;
			else
				ukh->ret = MALI_ERROR_NONE;
			spin_unlock_irqrestore(&kbdev->reg_op_lock, flags);
			/*mutex unlock */
#endif /* CONFIG_MALI_BIFROST_ERROR_INJECT */
			break;
		}

	case KBASE_FUNC_MODEL_CONTROL:
		{
#ifdef CONFIG_MALI_BIFROST_NO_MALI
			unsigned long flags;
			struct kbase_model_control_params params =
					((struct kbase_uk_model_control_params *)args)->params;

			/*mutex lock */
			spin_lock_irqsave(&kbdev->reg_op_lock, flags);
			if (gpu_model_control(kbdev->model, &params) != 0)
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			else
				ukh->ret = MALI_ERROR_NONE;
			spin_unlock_irqrestore(&kbdev->reg_op_lock, flags);
			/*mutex unlock */
#endif /* CONFIG_MALI_BIFROST_NO_MALI */
			break;
		}

	case KBASE_FUNC_GET_PROFILING_CONTROLS:
		{
			struct kbase_uk_profiling_controls *controls =
					(struct kbase_uk_profiling_controls *)args;
			u32 i;

			if (sizeof(*controls) != args_size)
				goto bad_size;

			for (i = FBDUMP_CONTROL_MIN; i < FBDUMP_CONTROL_MAX; i++)
				controls->profiling_controls[i] =
					kbdev->kbase_profiling_controls[i];

			break;
		}

	/* used only for testing purposes; these controls are to be set by gator through gator API */
	case KBASE_FUNC_SET_PROFILING_CONTROLS:
		{
			struct kbase_uk_profiling_controls *controls =
					(struct kbase_uk_profiling_controls *)args;
			u32 i;

			if (sizeof(*controls) != args_size)
				goto bad_size;

			for (i = FBDUMP_CONTROL_MIN; i < FBDUMP_CONTROL_MAX; i++)
				_mali_profiling_control(i, controls->profiling_controls[i]);

			break;
		}

	case KBASE_FUNC_DEBUGFS_MEM_PROFILE_ADD:
		{
			struct kbase_uk_debugfs_mem_profile_add *add_data =
					(struct kbase_uk_debugfs_mem_profile_add *)args;
			char *buf;
			char __user *user_buf;

			if (sizeof(*add_data) != args_size)
				goto bad_size;

			if (add_data->len > KBASE_MEM_PROFILE_MAX_BUF_SIZE) {
				dev_err(kbdev->dev, "buffer too big\n");
				goto out_bad;
			}

#ifdef CONFIG_COMPAT
			if (kbase_ctx_flag(kctx, KCTX_COMPAT))
				user_buf = compat_ptr(add_data->buf);
			else
#endif
				user_buf = u64_to_user_ptr(add_data->buf);

			buf = kmalloc(add_data->len, GFP_KERNEL);
			if (ZERO_OR_NULL_PTR(buf))
				goto out_bad;

			if (0 != copy_from_user(buf, user_buf, add_data->len)) {
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				kfree(buf);
				goto out_bad;
			}

			if (kbasep_mem_profile_debugfs_insert(kctx, buf,
							add_data->len)) {
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				goto out_bad;
			}

			break;
		}

#ifdef CONFIG_MALI_BIFROST_NO_MALI
	case KBASE_FUNC_SET_PRFCNT_VALUES:
		{

			struct kbase_uk_prfcnt_values *params =
			  ((struct kbase_uk_prfcnt_values *)args);
			gpu_model_set_dummy_prfcnt_sample(params->data,
					params->size);

			break;
		}
#endif /* CONFIG_MALI_BIFROST_NO_MALI */
#ifdef BASE_LEGACY_UK10_4_SUPPORT
	case KBASE_FUNC_TLSTREAM_ACQUIRE_V10_4:
		{
			struct kbase_uk_tlstream_acquire_v10_4 *tlstream_acquire
					= args;
			int ret;

			if (sizeof(*tlstream_acquire) != args_size)
				goto bad_size;

			ret = kbase_tlstream_acquire(
						kctx, 0);
			if (ret < 0)
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			else
				tlstream_acquire->fd = ret;
			break;
		}
#endif /* BASE_LEGACY_UK10_4_SUPPORT */
	case KBASE_FUNC_TLSTREAM_ACQUIRE:
		{
			struct kbase_uk_tlstream_acquire *tlstream_acquire =
				args;
			int ret;

			if (sizeof(*tlstream_acquire) != args_size)
				goto bad_size;

			if (tlstream_acquire->flags & ~BASE_TLSTREAM_FLAGS_MASK)
				goto out_bad;

			ret = kbase_tlstream_acquire(
					kctx, tlstream_acquire->flags);
			if (ret < 0)
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			else
				tlstream_acquire->fd = ret;
			break;
		}
	case KBASE_FUNC_TLSTREAM_FLUSH:
		{
			struct kbase_uk_tlstream_flush *tlstream_flush =
				args;

			if (sizeof(*tlstream_flush) != args_size)
				goto bad_size;

			kbase_tlstream_flush_streams();
			break;
		}
#if MALI_UNIT_TEST
	case KBASE_FUNC_TLSTREAM_TEST:
		{
			struct kbase_uk_tlstream_test *tlstream_test = args;

			if (sizeof(*tlstream_test) != args_size)
				goto bad_size;

			kbase_tlstream_test(
					tlstream_test->tpw_count,
					tlstream_test->msg_delay,
					tlstream_test->msg_count,
					tlstream_test->aux_msg);
			break;
		}
	case KBASE_FUNC_TLSTREAM_STATS:
		{
			struct kbase_uk_tlstream_stats *tlstream_stats = args;

			if (sizeof(*tlstream_stats) != args_size)
				goto bad_size;

			kbase_tlstream_stats(
					&tlstream_stats->bytes_collected,
					&tlstream_stats->bytes_generated);
			break;
		}
#endif /* MALI_UNIT_TEST */

	case KBASE_FUNC_GET_CONTEXT_ID:
		{
			struct kbase_uk_context_id *info = args;

			info->id = kctx->id;
			break;
		}

	case KBASE_FUNC_SOFT_EVENT_UPDATE:
		{
			struct kbase_uk_soft_event_update *update = args;

			if (sizeof(*update) != args_size)
				goto bad_size;

			if (((update->new_status != BASE_JD_SOFT_EVENT_SET) &&
			    (update->new_status != BASE_JD_SOFT_EVENT_RESET)) ||
			    (update->flags != 0))
				goto out_bad;

			if (kbase_soft_event_update(kctx, update->evt,
						update->new_status))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;

			break;
		}

	default:
		dev_err(kbdev->dev, "unknown ioctl %u\n", id);
		goto out_bad;
	}

	return ret;

 bad_size:
	dev_err(kbdev->dev, "Wrong syscall size (%d) for %08x\n", args_size, id);
 out_bad:
	return -EINVAL;
}

static struct kbase_device *to_kbase_device(struct device *dev)
{
	return dev_get_drvdata(dev);
}

static int assign_irqs(struct platform_device *pdev)
{
	struct kbase_device *kbdev = to_kbase_device(&pdev->dev);
	int i;

	if (!kbdev)
		return -ENODEV;

	/* 3 IRQ resources */
	for (i = 0; i < 3; i++) {
		struct resource *irq_res;
		int irqtag;

		irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, i);
		if (!irq_res) {
			dev_err(kbdev->dev, "No IRQ resource at index %d\n", i);
			return -ENOENT;
		}

#ifdef CONFIG_OF
		if (!strncmp(irq_res->name, "JOB", 4)) {
			irqtag = JOB_IRQ_TAG;
		} else if (!strncmp(irq_res->name, "MMU", 4)) {
			irqtag = MMU_IRQ_TAG;
		} else if (!strncmp(irq_res->name, "GPU", 4)) {
			irqtag = GPU_IRQ_TAG;
		} else {
			dev_err(&pdev->dev, "Invalid irq res name: '%s'\n",
				irq_res->name);
			return -EINVAL;
		}
#else
		irqtag = i;
#endif /* CONFIG_OF */
		kbdev->irqs[irqtag].irq = irq_res->start;
		kbdev->irqs[irqtag].flags = irq_res->flags & IRQF_TRIGGER_MASK;
	}

	return 0;
}

/*
 * API to acquire device list mutex and
 * return pointer to the device list head
 */
const struct list_head *kbase_dev_list_get(void)
{
	mutex_lock(&kbase_dev_list_lock);
	return &kbase_dev_list;
}
KBASE_EXPORT_TEST_API(kbase_dev_list_get);

/* API to release the device list mutex */
void kbase_dev_list_put(const struct list_head *dev_list)
{
	mutex_unlock(&kbase_dev_list_lock);
}
KBASE_EXPORT_TEST_API(kbase_dev_list_put);

/* Find a particular kbase device (as specified by minor number), or find the "first" device if -1 is specified */
struct kbase_device *kbase_find_device(int minor)
{
	struct kbase_device *kbdev = NULL;
	struct list_head *entry;
	const struct list_head *dev_list = kbase_dev_list_get();

	list_for_each(entry, dev_list) {
		struct kbase_device *tmp;

		tmp = list_entry(entry, struct kbase_device, entry);
		if (tmp->mdev.minor == minor || minor == -1) {
			kbdev = tmp;
			get_device(kbdev->dev);
			break;
		}
	}
	kbase_dev_list_put(dev_list);

	return kbdev;
}
EXPORT_SYMBOL(kbase_find_device);

void kbase_release_device(struct kbase_device *kbdev)
{
	put_device(kbdev->dev);
}
EXPORT_SYMBOL(kbase_release_device);

#if LINUX_VERSION_CODE < KERNEL_VERSION(4, 6, 0) && \
		!(LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 28) && \
		LINUX_VERSION_CODE < KERNEL_VERSION(4, 5, 0))
/*
 * Older versions, before v4.6, of the kernel doesn't have
 * kstrtobool_from_user(), except longterm 4.4.y which had it added in 4.4.28
 */
static int kstrtobool_from_user(const char __user *s, size_t count, bool *res)
{
	char buf[32];

	count = min(sizeof(buf), count);

	if (copy_from_user(buf, s, count))
		return -EFAULT;
	buf[count] = '\0';

	return strtobool(buf, res);
}
#endif

static ssize_t write_ctx_infinite_cache(struct file *f, const char __user *ubuf, size_t size, loff_t *off)
{
	struct kbase_context *kctx = f->private_data;
	int err;
	bool value;

	err = kstrtobool_from_user(ubuf, size, &value);
	if (err)
		return err;

	if (value)
		kbase_ctx_flag_set(kctx, KCTX_INFINITE_CACHE);
	else
		kbase_ctx_flag_clear(kctx, KCTX_INFINITE_CACHE);

	return size;
}

static ssize_t read_ctx_infinite_cache(struct file *f, char __user *ubuf, size_t size, loff_t *off)
{
	struct kbase_context *kctx = f->private_data;
	char buf[32];
	int count;
	bool value;

	value = kbase_ctx_flag(kctx, KCTX_INFINITE_CACHE);

	count = scnprintf(buf, sizeof(buf), "%s\n", value ? "Y" : "N");

	return simple_read_from_buffer(ubuf, size, off, buf, count);
}

static const struct file_operations kbase_infinite_cache_fops = {
	.open = simple_open,
	.write = write_ctx_infinite_cache,
	.read = read_ctx_infinite_cache,
};

static int kbase_open(struct inode *inode, struct file *filp)
{
	struct kbase_device *kbdev = NULL;
	struct kbase_context *kctx;
	int ret = 0;
#ifdef CONFIG_DEBUG_FS
	char kctx_name[64];
#endif

	kbdev = kbase_find_device(iminor(inode));

	if (!kbdev)
		return -ENODEV;

	kctx = kbase_create_context(kbdev, is_compat_task());
	if (!kctx) {
		ret = -ENOMEM;
		goto out;
	}

	init_waitqueue_head(&kctx->event_queue);
	filp->private_data = kctx;
	kctx->filp = filp;

	if (kbdev->infinite_cache_active_default)
		kbase_ctx_flag_set(kctx, KCTX_INFINITE_CACHE);

#ifdef CONFIG_DEBUG_FS
	snprintf(kctx_name, 64, "%d_%d", kctx->tgid, kctx->id);

	kctx->kctx_dentry = debugfs_create_dir(kctx_name,
			kbdev->debugfs_ctx_directory);

	if (IS_ERR_OR_NULL(kctx->kctx_dentry)) {
		ret = -ENOMEM;
		goto out;
	}

	debugfs_create_file("infinite_cache", 0644, kctx->kctx_dentry,
			    kctx, &kbase_infinite_cache_fops);

	mutex_init(&kctx->mem_profile_lock);

	kbasep_jd_debugfs_ctx_init(kctx);
	kbase_debug_mem_view_init(filp);

	kbase_debug_job_fault_context_init(kctx);

	kbase_mem_pool_debugfs_init(kctx->kctx_dentry, &kctx->mem_pool, &kctx->lp_mem_pool);

	kbase_jit_debugfs_init(kctx);
#endif /* CONFIG_DEBUG_FS */

	dev_dbg(kbdev->dev, "created base context\n");

	{
		struct kbasep_kctx_list_element *element;

		element = kzalloc(sizeof(*element), GFP_KERNEL);
		if (element) {
			mutex_lock(&kbdev->kctx_list_lock);
			element->kctx = kctx;
			list_add(&element->link, &kbdev->kctx_list);
			KBASE_TLSTREAM_TL_NEW_CTX(
					element->kctx,
					element->kctx->id,
					(u32)(element->kctx->tgid));
			mutex_unlock(&kbdev->kctx_list_lock);
		} else {
			/* we don't treat this as a fail - just warn about it */
			dev_warn(kbdev->dev, "couldn't add kctx to kctx_list\n");
		}
	}
	return 0;

 out:
	kbase_release_device(kbdev);
	return ret;
}

static int kbase_release(struct inode *inode, struct file *filp)
{
	struct kbase_context *kctx = filp->private_data;
	struct kbase_device *kbdev = kctx->kbdev;
	struct kbasep_kctx_list_element *element, *tmp;
	bool found_element = false;

	KBASE_TLSTREAM_TL_DEL_CTX(kctx);

#ifdef CONFIG_DEBUG_FS
	kbasep_mem_profile_debugfs_remove(kctx);
	kbase_debug_job_fault_context_term(kctx);
#endif

	mutex_lock(&kbdev->kctx_list_lock);
	list_for_each_entry_safe(element, tmp, &kbdev->kctx_list, link) {
		if (element->kctx == kctx) {
			list_del(&element->link);
			kfree(element);
			found_element = true;
		}
	}
	mutex_unlock(&kbdev->kctx_list_lock);
	if (!found_element)
		dev_warn(kbdev->dev, "kctx not in kctx_list\n");

	filp->private_data = NULL;

	mutex_lock(&kctx->vinstr_cli_lock);
	/* If this client was performing hwcnt dumping and did not explicitly
	 * detach itself, remove it from the vinstr core now */
	if (kctx->vinstr_cli) {
		struct kbase_uk_hwcnt_setup setup;

		setup.dump_buffer = 0llu;
		kbase_vinstr_legacy_hwc_setup(
				kbdev->vinstr_ctx, &kctx->vinstr_cli, &setup);
	}
	mutex_unlock(&kctx->vinstr_cli_lock);

	kbase_destroy_context(kctx);

	dev_dbg(kbdev->dev, "deleted base context\n");
	kbase_release_device(kbdev);
	return 0;
}

#define CALL_MAX_SIZE 536

static long kbase_legacy_ioctl(struct file *filp, unsigned int cmd,
		unsigned long arg)
{
	u64 msg[(CALL_MAX_SIZE + 7) >> 3] = { 0xdeadbeefdeadbeefull };	/* alignment fixup */
	u32 size = _IOC_SIZE(cmd);
	struct kbase_context *kctx = filp->private_data;

	if (size > CALL_MAX_SIZE)
		return -ENOTTY;

	if (0 != copy_from_user(&msg, (void __user *)arg, size)) {
		dev_err(kctx->kbdev->dev, "failed to copy ioctl argument into kernel space\n");
		return -EFAULT;
	}

	if (kbase_legacy_dispatch(kctx, &msg, size) != 0)
		return -EFAULT;

	if (0 != copy_to_user((void __user *)arg, &msg, size)) {
		dev_err(kctx->kbdev->dev, "failed to copy results of UK call back to user space\n");
		return -EFAULT;
	}
	return 0;
}

static int kbase_api_set_flags(struct kbase_context *kctx,
		struct kbase_ioctl_set_flags *flags)
{
	int err;

	/* setup pending, try to signal that we'll do the setup,
	 * if setup was already in progress, err this call
	 */
	if (atomic_cmpxchg(&kctx->setup_in_progress, 0, 1) != 0)
		return -EINVAL;

	err = kbase_context_set_create_flags(kctx, flags->create_flags);
	/* if bad flags, will stay stuck in setup mode */
	if (err)
		return err;

	atomic_set(&kctx->setup_complete, 1);
	return 0;
}

static int kbase_api_job_submit(struct kbase_context *kctx,
		struct kbase_ioctl_job_submit *submit)
{
	return kbase_jd_submit(kctx, u64_to_user_ptr(submit->addr),
			submit->nr_atoms,
			submit->stride, false);
}

static int kbase_api_get_gpuprops(struct kbase_context *kctx,
		struct kbase_ioctl_get_gpuprops *get_props)
{
	struct kbase_gpu_props *kprops = &kctx->kbdev->gpu_props;
	int err;

	if (get_props->flags != 0) {
		dev_err(kctx->kbdev->dev, "Unsupported flags to get_gpuprops");
		return -EINVAL;
	}

	if (get_props->size == 0)
		return kprops->prop_buffer_size;
	if (get_props->size < kprops->prop_buffer_size)
		return -EINVAL;

	err = copy_to_user(u64_to_user_ptr(get_props->buffer),
			kprops->prop_buffer,
			kprops->prop_buffer_size);
	if (err)
		return -EFAULT;
	return kprops->prop_buffer_size;
}

static int kbase_api_post_term(struct kbase_context *kctx)
{
	kbase_event_close(kctx);
	return 0;
}

static int kbase_api_mem_alloc(struct kbase_context *kctx,
		union kbase_ioctl_mem_alloc *alloc)
{
	struct kbase_va_region *reg;
	u64 flags = alloc->in.flags;
	u64 gpu_va;

#if defined(CONFIG_64BIT)
	if (!kbase_ctx_flag(kctx, KCTX_COMPAT)) {
		/* force SAME_VA if a 64-bit client */
		flags |= BASE_MEM_SAME_VA;
	}
#endif

	reg = kbase_mem_alloc(kctx, alloc->in.va_pages,
			alloc->in.commit_pages,
			alloc->in.extent,
			&flags, &gpu_va);

	if (!reg)
		return -ENOMEM;

	alloc->out.flags = flags;
	alloc->out.gpu_va = gpu_va;

	return 0;
}

static int kbase_api_mem_query(struct kbase_context *kctx,
		union kbase_ioctl_mem_query *query)
{
	return kbase_mem_query(kctx, query->in.gpu_addr,
			query->in.query, &query->out.value);
}

static int kbase_api_mem_free(struct kbase_context *kctx,
		struct kbase_ioctl_mem_free *free)
{
	return kbase_mem_free(kctx, free->gpu_addr);
}

static int kbase_api_hwcnt_reader_setup(struct kbase_context *kctx,
		struct kbase_ioctl_hwcnt_reader_setup *setup)
{
	int ret;
	struct kbase_uk_hwcnt_reader_setup args = {
		.buffer_count = setup->buffer_count,
		.jm_bm = setup->jm_bm,
		.shader_bm = setup->shader_bm,
		.tiler_bm = setup->tiler_bm,
		.mmu_l2_bm = setup->mmu_l2_bm
	};

	mutex_lock(&kctx->vinstr_cli_lock);
	ret = kbase_vinstr_hwcnt_reader_setup(kctx->kbdev->vinstr_ctx, &args);
	mutex_unlock(&kctx->vinstr_cli_lock);

	if (ret)
		return ret;
	return args.fd;
}

static int kbase_api_hwcnt_enable(struct kbase_context *kctx,
		struct kbase_ioctl_hwcnt_enable *enable)
{
	int ret;
	struct kbase_uk_hwcnt_setup args = {
		.dump_buffer = enable->dump_buffer,
		.jm_bm = enable->jm_bm,
		.shader_bm = enable->shader_bm,
		.tiler_bm = enable->tiler_bm,
		.mmu_l2_bm = enable->mmu_l2_bm
	};

	mutex_lock(&kctx->vinstr_cli_lock);
	ret = kbase_vinstr_legacy_hwc_setup(kctx->kbdev->vinstr_ctx,
			&kctx->vinstr_cli, &args);
	mutex_unlock(&kctx->vinstr_cli_lock);

	return ret;
}

static int kbase_api_hwcnt_dump(struct kbase_context *kctx)
{
	int ret;

	mutex_lock(&kctx->vinstr_cli_lock);
	ret = kbase_vinstr_hwc_dump(kctx->vinstr_cli,
			BASE_HWCNT_READER_EVENT_MANUAL);
	mutex_unlock(&kctx->vinstr_cli_lock);

	return ret;
}

static int kbase_api_hwcnt_clear(struct kbase_context *kctx)
{
	int ret;

	mutex_lock(&kctx->vinstr_cli_lock);
	ret = kbase_vinstr_hwc_clear(kctx->vinstr_cli);
	mutex_unlock(&kctx->vinstr_cli_lock);

	return ret;
}

static int kbase_api_disjoint_query(struct kbase_context *kctx,
		struct kbase_ioctl_disjoint_query *query)
{
	query->counter = kbase_disjoint_event_get(kctx->kbdev);

	return 0;
}

static int kbase_api_get_ddk_version(struct kbase_context *kctx,
		struct kbase_ioctl_get_ddk_version *version)
{
	int ret;
	int len = sizeof(KERNEL_SIDE_DDK_VERSION_STRING);

	if (version->version_buffer == 0)
		return len;

	if (version->size < len)
		return -EOVERFLOW;

	ret = copy_to_user(u64_to_user_ptr(version->version_buffer),
			KERNEL_SIDE_DDK_VERSION_STRING,
			sizeof(KERNEL_SIDE_DDK_VERSION_STRING));

	if (ret)
		return -EFAULT;

	return len;
}

static int kbase_api_mem_jit_init(struct kbase_context *kctx,
		struct kbase_ioctl_mem_jit_init *jit_init)
{
	return kbase_region_tracker_init_jit(kctx, jit_init->va_pages);
}

static int kbase_api_mem_sync(struct kbase_context *kctx,
		struct kbase_ioctl_mem_sync *sync)
{
	struct basep_syncset sset = {
		.mem_handle.basep.handle = sync->handle,
		.user_addr = sync->user_addr,
		.size = sync->size,
		.type = sync->type
	};

	return kbase_sync_now(kctx, &sset);
}

static int kbase_api_mem_find_cpu_offset(struct kbase_context *kctx,
		union kbase_ioctl_mem_find_cpu_offset *find)
{
	return kbasep_find_enclosing_cpu_mapping_offset(
			kctx,
			find->in.cpu_addr,
			find->in.size,
			&find->out.offset);
}

static int kbase_api_get_context_id(struct kbase_context *kctx,
		struct kbase_ioctl_get_context_id *info)
{
	info->id = kctx->id;

	return 0;
}

static int kbase_api_tlstream_acquire(struct kbase_context *kctx,
		struct kbase_ioctl_tlstream_acquire *acquire)
{
	return kbase_tlstream_acquire(kctx, acquire->flags);
}

static int kbase_api_tlstream_flush(struct kbase_context *kctx)
{
	kbase_tlstream_flush_streams();

	return 0;
}

static int kbase_api_mem_commit(struct kbase_context *kctx,
		struct kbase_ioctl_mem_commit *commit)
{
	return kbase_mem_commit(kctx, commit->gpu_addr, commit->pages);
}

static int kbase_api_mem_alias(struct kbase_context *kctx,
		union kbase_ioctl_mem_alias *alias)
{
	struct base_mem_aliasing_info *ai;
	u64 flags;
	int err;

	if (alias->in.nents == 0 || alias->in.nents > 2048)
		return -EINVAL;

	ai = vmalloc(sizeof(*ai) * alias->in.nents);
	if (!ai)
		return -ENOMEM;

	err = copy_from_user(ai,
			u64_to_user_ptr(alias->in.aliasing_info),
			sizeof(*ai) * alias->in.nents);
	if (err) {
		vfree(ai);
		return -EFAULT;
	}

	flags = alias->in.flags;

	alias->out.gpu_va = kbase_mem_alias(kctx, &flags,
			alias->in.stride, alias->in.nents,
			ai, &alias->out.va_pages);

	alias->out.flags = flags;

	vfree(ai);

	if (alias->out.gpu_va == 0)
		return -ENOMEM;

	return 0;
}

static int kbase_api_mem_import(struct kbase_context *kctx,
		union kbase_ioctl_mem_import *import)
{
	int ret;
	u64 flags = import->in.flags;

	ret = kbase_mem_import(kctx,
			import->in.type,
			u64_to_user_ptr(import->in.phandle),
			import->in.padding,
			&import->out.gpu_va,
			&import->out.va_pages,
			&flags);

	import->out.flags = flags;

	return ret;
}

static int kbase_api_mem_flags_change(struct kbase_context *kctx,
		struct kbase_ioctl_mem_flags_change *change)
{
	return kbase_mem_flags_change(kctx, change->gpu_va,
			change->flags, change->mask);
}

static int kbase_api_stream_create(struct kbase_context *kctx,
		struct kbase_ioctl_stream_create *stream)
{
#if defined(CONFIG_SYNC) || defined(CONFIG_SYNC_FILE)
	int fd, ret;

	/* Name must be NULL-terminated and padded with NULLs, so check last
	 * character is NULL
	 */
	if (stream->name[sizeof(stream->name)-1] != 0)
		return -EINVAL;

	ret = kbase_sync_fence_stream_create(stream->name, &fd);

	if (ret)
		return ret;
	return fd;
#else
	return -ENOENT;
#endif
}

static int kbase_api_fence_validate(struct kbase_context *kctx,
		struct kbase_ioctl_fence_validate *validate)
{
#if defined(CONFIG_SYNC) || defined(CONFIG_SYNC_FILE)
	return kbase_sync_fence_validate(validate->fd);
#else
	return -ENOENT;
#endif
}

static int kbase_api_get_profiling_controls(struct kbase_context *kctx,
		struct kbase_ioctl_get_profiling_controls *controls)
{
	int ret;

	if (controls->count > (FBDUMP_CONTROL_MAX - FBDUMP_CONTROL_MIN))
		return -EINVAL;

	ret = copy_to_user(u64_to_user_ptr(controls->buffer),
			&kctx->kbdev->kbase_profiling_controls[
				FBDUMP_CONTROL_MIN],
			controls->count * sizeof(u32));

	if (ret)
		return -EFAULT;
	return 0;
}

static int kbase_api_mem_profile_add(struct kbase_context *kctx,
		struct kbase_ioctl_mem_profile_add *data)
{
	char *buf;
	int err;

	if (data->len > KBASE_MEM_PROFILE_MAX_BUF_SIZE) {
		dev_err(kctx->kbdev->dev, "mem_profile_add: buffer too big\n");
		return -EINVAL;
	}

	buf = kmalloc(data->len, GFP_KERNEL);
	if (ZERO_OR_NULL_PTR(buf))
		return -ENOMEM;

	err = copy_from_user(buf, u64_to_user_ptr(data->buffer),
			data->len);
	if (err) {
		kfree(buf);
		return -EFAULT;
	}

	return kbasep_mem_profile_debugfs_insert(kctx, buf, data->len);
}

static int kbase_api_soft_event_update(struct kbase_context *kctx,
		struct kbase_ioctl_soft_event_update *update)
{
	if (update->flags != 0)
		return -EINVAL;

	return kbase_soft_event_update(kctx, update->event, update->new_status);
}

#if MALI_UNIT_TEST
static int kbase_api_tlstream_test(struct kbase_context *kctx,
		struct kbase_ioctl_tlstream_test *test)
{
	kbase_tlstream_test(
			test->tpw_count,
			test->msg_delay,
			test->msg_count,
			test->aux_msg);

	return 0;
}

static int kbase_api_tlstream_stats(struct kbase_context *kctx,
		struct kbase_ioctl_tlstream_stats *stats)
{
	kbase_tlstream_stats(
			&stats->bytes_collected,
			&stats->bytes_generated);

	return 0;
}
#endif /* MALI_UNIT_TEST */

#define KBASE_HANDLE_IOCTL(cmd, function)                          \
	case cmd:                                                  \
	do {                                                       \
		BUILD_BUG_ON(_IOC_DIR(cmd) != _IOC_NONE);          \
		return function(kctx);                             \
	} while (0)

#define KBASE_HANDLE_IOCTL_IN(cmd, function, type)                 \
	case cmd:                                                  \
	do {                                                       \
		type param;                                        \
		int err;                                           \
		BUILD_BUG_ON(_IOC_DIR(cmd) != _IOC_WRITE);         \
		BUILD_BUG_ON(sizeof(param) != _IOC_SIZE(cmd));     \
		err = copy_from_user(&param, uarg, sizeof(param)); \
		if (err)                                           \
			return -EFAULT;                            \
		return function(kctx, &param);                     \
	} while (0)

#define KBASE_HANDLE_IOCTL_OUT(cmd, function, type)                \
	case cmd:                                                  \
	do {                                                       \
		type param;                                        \
		int ret, err;                                      \
		BUILD_BUG_ON(_IOC_DIR(cmd) != _IOC_READ);          \
		BUILD_BUG_ON(sizeof(param) != _IOC_SIZE(cmd));     \
		ret = function(kctx, &param);                      \
		err = copy_to_user(uarg, &param, sizeof(param));   \
		if (err)                                           \
			return -EFAULT;                            \
		return ret;                                        \
	} while (0)

#define KBASE_HANDLE_IOCTL_INOUT(cmd, function, type)                  \
	case cmd:                                                      \
	do {                                                           \
		type param;                                            \
		int ret, err;                                          \
		BUILD_BUG_ON(_IOC_DIR(cmd) != (_IOC_WRITE|_IOC_READ)); \
		BUILD_BUG_ON(sizeof(param) != _IOC_SIZE(cmd));         \
		err = copy_from_user(&param, uarg, sizeof(param));     \
		if (err)                                               \
			return -EFAULT;                                \
		ret = function(kctx, &param);                          \
		err = copy_to_user(uarg, &param, sizeof(param));       \
		if (err)                                               \
			return -EFAULT;                                \
		return ret;                                            \
	} while (0)

static long kbase_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct kbase_context *kctx = filp->private_data;
	struct kbase_device *kbdev = kctx->kbdev;
	void __user *uarg = (void __user *)arg;

	/* The UK ioctl values overflow the cmd field causing the type to be
	 * incremented
	 */
	if (_IOC_TYPE(cmd) == LINUX_UK_BASE_MAGIC+2)
		return kbase_legacy_ioctl(filp, cmd, arg);

	/* The UK version check IOCTL doesn't overflow the cmd field, so is
	 * handled separately here
	 */
	if (cmd == _IOC(_IOC_READ|_IOC_WRITE, LINUX_UK_BASE_MAGIC,
				UKP_FUNC_ID_CHECK_VERSION,
				sizeof(struct uku_version_check_args)))
		return kbase_legacy_ioctl(filp, cmd, arg);

	/* Only these ioctls are available until setup is complete */
	switch (cmd) {
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_VERSION_CHECK,
				kbase_api_handshake,
				struct kbase_ioctl_version_check);
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_SET_FLAGS,
				kbase_api_set_flags,
				struct kbase_ioctl_set_flags);
	}

	/* Block call until version handshake and setup is complete */
	if (kctx->api_version == 0 || !atomic_read(&kctx->setup_complete))
		return -EINVAL;

	/* Normal ioctls */
	switch (cmd) {
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_JOB_SUBMIT,
				kbase_api_job_submit,
				struct kbase_ioctl_job_submit);
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_GET_GPUPROPS,
				kbase_api_get_gpuprops,
				struct kbase_ioctl_get_gpuprops);
		KBASE_HANDLE_IOCTL(KBASE_IOCTL_POST_TERM,
				kbase_api_post_term);
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_ALLOC,
				kbase_api_mem_alloc,
				union kbase_ioctl_mem_alloc);
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_QUERY,
				kbase_api_mem_query,
				union kbase_ioctl_mem_query);
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_FREE,
				kbase_api_mem_free,
				struct kbase_ioctl_mem_free);
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_HWCNT_READER_SETUP,
				kbase_api_hwcnt_reader_setup,
				struct kbase_ioctl_hwcnt_reader_setup);
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_HWCNT_ENABLE,
				kbase_api_hwcnt_enable,
				struct kbase_ioctl_hwcnt_enable);
		KBASE_HANDLE_IOCTL(KBASE_IOCTL_HWCNT_DUMP,
				kbase_api_hwcnt_dump);
		KBASE_HANDLE_IOCTL(KBASE_IOCTL_HWCNT_CLEAR,
				kbase_api_hwcnt_clear);
		KBASE_HANDLE_IOCTL_OUT(KBASE_IOCTL_DISJOINT_QUERY,
				kbase_api_disjoint_query,
				struct kbase_ioctl_disjoint_query);
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_GET_DDK_VERSION,
				kbase_api_get_ddk_version,
				struct kbase_ioctl_get_ddk_version);
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_JIT_INIT,
				kbase_api_mem_jit_init,
				struct kbase_ioctl_mem_jit_init);
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_SYNC,
				kbase_api_mem_sync,
				struct kbase_ioctl_mem_sync);
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_FIND_CPU_OFFSET,
				kbase_api_mem_find_cpu_offset,
				union kbase_ioctl_mem_find_cpu_offset);
		KBASE_HANDLE_IOCTL_OUT(KBASE_IOCTL_GET_CONTEXT_ID,
				kbase_api_get_context_id,
				struct kbase_ioctl_get_context_id);
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_TLSTREAM_ACQUIRE,
				kbase_api_tlstream_acquire,
				struct kbase_ioctl_tlstream_acquire);
		KBASE_HANDLE_IOCTL(KBASE_IOCTL_TLSTREAM_FLUSH,
				kbase_api_tlstream_flush);
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_COMMIT,
				kbase_api_mem_commit,
				struct kbase_ioctl_mem_commit);
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_ALIAS,
				kbase_api_mem_alias,
				union kbase_ioctl_mem_alias);
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_IMPORT,
				kbase_api_mem_import,
				union kbase_ioctl_mem_import);
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_FLAGS_CHANGE,
				kbase_api_mem_flags_change,
				struct kbase_ioctl_mem_flags_change);
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_STREAM_CREATE,
				kbase_api_stream_create,
				struct kbase_ioctl_stream_create);
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_FENCE_VALIDATE,
				kbase_api_fence_validate,
				struct kbase_ioctl_fence_validate);
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_GET_PROFILING_CONTROLS,
				kbase_api_get_profiling_controls,
				struct kbase_ioctl_get_profiling_controls);
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_PROFILE_ADD,
				kbase_api_mem_profile_add,
				struct kbase_ioctl_mem_profile_add);
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_SOFT_EVENT_UPDATE,
				kbase_api_soft_event_update,
				struct kbase_ioctl_soft_event_update);

#if MALI_UNIT_TEST
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_TLSTREAM_TEST,
				kbase_api_tlstream_test,
				struct kbase_ioctl_tlstream_test);
		KBASE_HANDLE_IOCTL_OUT(KBASE_IOCTL_TLSTREAM_STATS,
				kbase_api_tlstream_stats,
				struct kbase_ioctl_tlstream_stats);
#endif
	}

	dev_warn(kbdev->dev, "Unknown ioctl 0x%x nr:%d", cmd, _IOC_NR(cmd));

	return -ENOIOCTLCMD;
}

static ssize_t kbase_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct kbase_context *kctx = filp->private_data;
	struct base_jd_event_v2 uevent;
	int out_count = 0;

	if (count < sizeof(uevent))
		return -ENOBUFS;

	do {
		while (kbase_event_dequeue(kctx, &uevent)) {
			if (out_count > 0)
				goto out;

			if (filp->f_flags & O_NONBLOCK)
				return -EAGAIN;

			if (wait_event_interruptible(kctx->event_queue,
					kbase_event_pending(kctx)) != 0)
				return -ERESTARTSYS;
		}
		if (uevent.event_code == BASE_JD_EVENT_DRV_TERMINATED) {
			if (out_count == 0)
				return -EPIPE;
			goto out;
		}

		if (copy_to_user(buf, &uevent, sizeof(uevent)) != 0)
			return -EFAULT;

		buf += sizeof(uevent);
		out_count++;
		count -= sizeof(uevent);
	} while (count >= sizeof(uevent));

 out:
	return out_count * sizeof(uevent);
}

static unsigned int kbase_poll(struct file *filp, poll_table *wait)
{
	struct kbase_context *kctx = filp->private_data;

	poll_wait(filp, &kctx->event_queue, wait);
	if (kbase_event_pending(kctx))
		return POLLIN | POLLRDNORM;

	return 0;
}

void kbase_event_wakeup(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx);

	wake_up_interruptible(&kctx->event_queue);
}

KBASE_EXPORT_TEST_API(kbase_event_wakeup);

static int kbase_check_flags(int flags)
{
	/* Enforce that the driver keeps the O_CLOEXEC flag so that execve() always
	 * closes the file descriptor in a child process.
	 */
	if (0 == (flags & O_CLOEXEC))
		return -EINVAL;

	return 0;
}


/**
 * align_and_check - Align the specified pointer to the provided alignment and
 *                   check that it is still in range.
 * @gap_end:        Highest possible start address for allocation (end of gap in
 *                  address space)
 * @gap_start:      Start address of current memory area / gap in address space
 * @info:           vm_unmapped_area_info structure passed to caller, containing
 *                  alignment, length and limits for the allocation
 * @is_shader_code: True if the allocation is for shader code (which has
 *                  additional alignment requirements)
 *
 * Return: true if gap_end is now aligned correctly and is still in range,
 *         false otherwise
 */
static bool align_and_check(unsigned long *gap_end, unsigned long gap_start,
		struct vm_unmapped_area_info *info, bool is_shader_code)
{
	/* Compute highest gap address at the desired alignment */
	(*gap_end) -= info->length;
	(*gap_end) -= (*gap_end - info->align_offset) & info->align_mask;

	if (is_shader_code) {
		/* Check for 4GB boundary */
		if (0 == (*gap_end & BASE_MEM_MASK_4GB))
			(*gap_end) -= (info->align_offset ? info->align_offset :
					info->length);
		if (0 == ((*gap_end + info->length) & BASE_MEM_MASK_4GB))
			(*gap_end) -= (info->align_offset ? info->align_offset :
					info->length);

		if (!(*gap_end & BASE_MEM_MASK_4GB) || !((*gap_end +
				info->length) & BASE_MEM_MASK_4GB))
			return false;
	}


	if ((*gap_end < info->low_limit) || (*gap_end < gap_start))
		return false;


	return true;
}

/* The following function is taken from the kernel and just
 * renamed. As it's not exported to modules we must copy-paste it here.
 */

static unsigned long kbase_unmapped_area_topdown(struct vm_unmapped_area_info
		*info, bool is_shader_code)
{
	struct mm_struct *mm = current->mm;
	struct vm_area_struct *vma;
	unsigned long length, low_limit, high_limit, gap_start, gap_end;

	/* Adjust search length to account for worst case alignment overhead */
	length = info->length + info->align_mask;
	if (length < info->length)
		return -ENOMEM;

	/*
	 * Adjust search limits by the desired length.
	 * See implementation comment at top of unmapped_area().
	 */
	gap_end = info->high_limit;
	if (gap_end < length)
		return -ENOMEM;
	high_limit = gap_end - length;

	if (info->low_limit > high_limit)
		return -ENOMEM;
	low_limit = info->low_limit + length;

	/* Check highest gap, which does not precede any rbtree node */
	gap_start = mm->highest_vm_end;
	if (gap_start <= high_limit) {
		if (align_and_check(&gap_end, gap_start, info, is_shader_code))
			return gap_end;
	}

	/* Check if rbtree root looks promising */
	if (RB_EMPTY_ROOT(&mm->mm_rb))
		return -ENOMEM;
	vma = rb_entry(mm->mm_rb.rb_node, struct vm_area_struct, vm_rb);
	if (vma->rb_subtree_gap < length)
		return -ENOMEM;

	while (true) {
		/* Visit right subtree if it looks promising */
		gap_start = vma->vm_prev ? vma->vm_prev->vm_end : 0;
		if (gap_start <= high_limit && vma->vm_rb.rb_right) {
			struct vm_area_struct *right =
				rb_entry(vma->vm_rb.rb_right,
					 struct vm_area_struct, vm_rb);
			if (right->rb_subtree_gap >= length) {
				vma = right;
				continue;
			}
		}

check_current:
		/* Check if current node has a suitable gap */
		gap_end = vma->vm_start;
		if (gap_end < low_limit)
			return -ENOMEM;
		if (gap_start <= high_limit && gap_end - gap_start >= length) {
			/* We found a suitable gap. Clip it with the original
			 * high_limit. */
			if (gap_end > info->high_limit)
				gap_end = info->high_limit;

			if (align_and_check(&gap_end, gap_start, info,
					is_shader_code))
				return gap_end;
		}

		/* Visit left subtree if it looks promising */
		if (vma->vm_rb.rb_left) {
			struct vm_area_struct *left =
				rb_entry(vma->vm_rb.rb_left,
					 struct vm_area_struct, vm_rb);
			if (left->rb_subtree_gap >= length) {
				vma = left;
				continue;
			}
		}

		/* Go back up the rbtree to find next candidate node */
		while (true) {
			struct rb_node *prev = &vma->vm_rb;
			if (!rb_parent(prev))
				return -ENOMEM;
			vma = rb_entry(rb_parent(prev),
				       struct vm_area_struct, vm_rb);
			if (prev == vma->vm_rb.rb_right) {
				gap_start = vma->vm_prev ?
					vma->vm_prev->vm_end : 0;
				goto check_current;
			}
		}
	}

	return -ENOMEM;
}

static unsigned long kbase_get_unmapped_area(struct file *filp,
		const unsigned long addr, const unsigned long len,
		const unsigned long pgoff, const unsigned long flags)
{
	/* based on get_unmapped_area, but simplified slightly due to that some
	 * values are known in advance */
	struct kbase_context *kctx = filp->private_data;
	struct mm_struct *mm = current->mm;
	struct vm_unmapped_area_info info;
	unsigned long align_offset = 0;
	unsigned long align_mask = 0;
	unsigned long high_limit = mm->mmap_base;
	unsigned long low_limit = PAGE_SIZE;
	int cpu_va_bits = BITS_PER_LONG;
	int gpu_pc_bits =
	      kctx->kbdev->gpu_props.props.core_props.log2_program_counter_size;
	bool is_shader_code = false;
	unsigned long ret;

	/* err on fixed address */
	if ((flags & MAP_FIXED) || addr)
		return -EINVAL;

#ifdef CONFIG_64BIT
	/* too big? */
	if (len > TASK_SIZE - SZ_2M)
		return -ENOMEM;

	if (!kbase_ctx_flag(kctx, KCTX_COMPAT)) {

		if (kbase_hw_has_feature(kctx->kbdev,
						BASE_HW_FEATURE_33BIT_VA)) {
			high_limit = kctx->same_va_end << PAGE_SHIFT;
		} else {
			high_limit = min_t(unsigned long, mm->mmap_base,
					(kctx->same_va_end << PAGE_SHIFT));
			if (len >= SZ_2M) {
				align_offset = SZ_2M;
				align_mask = SZ_2M - 1;
			}
		}

		low_limit = SZ_2M;
	} else {
		cpu_va_bits = 32;
	}
#endif /* CONFIG_64BIT */
	if ((PFN_DOWN(BASE_MEM_COOKIE_BASE) <= pgoff) &&
		(PFN_DOWN(BASE_MEM_FIRST_FREE_ADDRESS) > pgoff)) {
			int cookie = pgoff - PFN_DOWN(BASE_MEM_COOKIE_BASE);

			if (!kctx->pending_regions[cookie])
				return -EINVAL;

			if (!(kctx->pending_regions[cookie]->flags &
							KBASE_REG_GPU_NX)) {
				if (cpu_va_bits > gpu_pc_bits) {
					align_offset = 1ULL << gpu_pc_bits;
					align_mask = align_offset - 1;
					is_shader_code = true;
				}
			}
#ifndef CONFIG_64BIT
	} else {
		return current->mm->get_unmapped_area(filp, addr, len, pgoff,
						      flags);
#endif
	}

	info.flags = 0;
	info.length = len;
	info.low_limit = low_limit;
	info.high_limit = high_limit;
	info.align_offset = align_offset;
	info.align_mask = align_mask;

	ret = kbase_unmapped_area_topdown(&info, is_shader_code);

	if (IS_ERR_VALUE(ret) && high_limit == mm->mmap_base &&
			high_limit < (kctx->same_va_end << PAGE_SHIFT)) {
		/* Retry above mmap_base */
		info.low_limit = mm->mmap_base;
		info.high_limit = min_t(u64, TASK_SIZE,
					(kctx->same_va_end << PAGE_SHIFT));

		ret = kbase_unmapped_area_topdown(&info, is_shader_code);
	}

	return ret;
}

static const struct file_operations kbase_fops = {
	.owner = THIS_MODULE,
	.open = kbase_open,
	.release = kbase_release,
	.read = kbase_read,
	.poll = kbase_poll,
	.unlocked_ioctl = kbase_ioctl,
	.compat_ioctl = kbase_ioctl,
	.mmap = kbase_mmap,
	.check_flags = kbase_check_flags,
	.get_unmapped_area = kbase_get_unmapped_area,
};

#ifndef CONFIG_MALI_BIFROST_NO_MALI
void kbase_os_reg_write(struct kbase_device *kbdev, u16 offset, u32 value)
{
	writel(value, kbdev->reg + offset);
}

u32 kbase_os_reg_read(struct kbase_device *kbdev, u16 offset)
{
	return readl(kbdev->reg + offset);
}
#endif /* !CONFIG_MALI_BIFROST_NO_MALI */

/**
 * show_policy - Show callback for the power_policy sysfs file.
 *
 * This function is called to get the contents of the power_policy sysfs
 * file. This is a list of the available policies with the currently active one
 * surrounded by square brackets.
 *
 * @dev:	The device this sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The output buffer for the sysfs file contents
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t show_policy(struct device *dev, struct device_attribute *attr, char *const buf)
{
	struct kbase_device *kbdev;
	const struct kbase_pm_policy *current_policy;
	const struct kbase_pm_policy *const *policy_list;
	int policy_count;
	int i;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	current_policy = kbase_pm_get_policy(kbdev);

	policy_count = kbase_pm_list_policies(&policy_list);

	for (i = 0; i < policy_count && ret < PAGE_SIZE; i++) {
		if (policy_list[i] == current_policy)
			ret += scnprintf(buf + ret, PAGE_SIZE - ret, "[%s] ", policy_list[i]->name);
		else
			ret += scnprintf(buf + ret, PAGE_SIZE - ret, "%s ", policy_list[i]->name);
	}

	if (ret < PAGE_SIZE - 1) {
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

/**
 * set_policy - Store callback for the power_policy sysfs file.
 *
 * This function is called when the power_policy sysfs file is written to.
 * It matches the requested policy against the available policies and if a
 * matching policy is found calls kbase_pm_set_policy() to change the
 * policy.
 *
 * @dev:	The device with sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The value written to the sysfs file
 * @count:	The number of bytes written to the sysfs file
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t set_policy(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	const struct kbase_pm_policy *new_policy = NULL;
	const struct kbase_pm_policy *const *policy_list;
	int policy_count;
	int i;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	policy_count = kbase_pm_list_policies(&policy_list);

	for (i = 0; i < policy_count; i++) {
		if (sysfs_streq(policy_list[i]->name, buf)) {
			new_policy = policy_list[i];
			break;
		}
	}

	if (!new_policy) {
		dev_err(dev, "power_policy: policy not found\n");
		return -EINVAL;
	}

	kbase_pm_set_policy(kbdev, new_policy);

	return count;
}

/*
 * The sysfs file power_policy.
 *
 * This is used for obtaining information about the available policies,
 * determining which policy is currently active, and changing the active
 * policy.
 */
static DEVICE_ATTR(power_policy, S_IRUGO | S_IWUSR, show_policy, set_policy);

/**
 * show_ca_policy - Show callback for the core_availability_policy sysfs file.
 *
 * This function is called to get the contents of the core_availability_policy
 * sysfs file. This is a list of the available policies with the currently
 * active one surrounded by square brackets.
 *
 * @dev:	The device this sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The output buffer for the sysfs file contents
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t show_ca_policy(struct device *dev, struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	const struct kbase_pm_ca_policy *current_policy;
	const struct kbase_pm_ca_policy *const *policy_list;
	int policy_count;
	int i;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	current_policy = kbase_pm_ca_get_policy(kbdev);

	policy_count = kbase_pm_ca_list_policies(&policy_list);

	for (i = 0; i < policy_count && ret < PAGE_SIZE; i++) {
		if (policy_list[i] == current_policy)
			ret += scnprintf(buf + ret, PAGE_SIZE - ret, "[%s] ", policy_list[i]->name);
		else
			ret += scnprintf(buf + ret, PAGE_SIZE - ret, "%s ", policy_list[i]->name);
	}

	if (ret < PAGE_SIZE - 1) {
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "\n");
	} else {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

/**
 * set_ca_policy - Store callback for the core_availability_policy sysfs file.
 *
 * This function is called when the core_availability_policy sysfs file is
 * written to. It matches the requested policy against the available policies
 * and if a matching policy is found calls kbase_pm_set_policy() to change
 * the policy.
 *
 * @dev:	The device with sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The value written to the sysfs file
 * @count:	The number of bytes written to the sysfs file
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t set_ca_policy(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	const struct kbase_pm_ca_policy *new_policy = NULL;
	const struct kbase_pm_ca_policy *const *policy_list;
	int policy_count;
	int i;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	policy_count = kbase_pm_ca_list_policies(&policy_list);

	for (i = 0; i < policy_count; i++) {
		if (sysfs_streq(policy_list[i]->name, buf)) {
			new_policy = policy_list[i];
			break;
		}
	}

	if (!new_policy) {
		dev_err(dev, "core_availability_policy: policy not found\n");
		return -EINVAL;
	}

	kbase_pm_ca_set_policy(kbdev, new_policy);

	return count;
}

/*
 * The sysfs file core_availability_policy
 *
 * This is used for obtaining information about the available policies,
 * determining which policy is currently active, and changing the active
 * policy.
 */
static DEVICE_ATTR(core_availability_policy, S_IRUGO | S_IWUSR, show_ca_policy, set_ca_policy);

/*
 * show_core_mask - Show callback for the core_mask sysfs file.
 *
 * This function is called to get the contents of the core_mask sysfs file.
 *
 * @dev:	The device this sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The output buffer for the sysfs file contents
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t show_core_mask(struct device *dev, struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			"Current core mask (JS0) : 0x%llX\n",
			kbdev->pm.debug_core_mask[0]);
	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			"Current core mask (JS1) : 0x%llX\n",
			kbdev->pm.debug_core_mask[1]);
	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			"Current core mask (JS2) : 0x%llX\n",
			kbdev->pm.debug_core_mask[2]);
	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			"Available core mask : 0x%llX\n",
			kbdev->gpu_props.props.raw_props.shader_present);

	return ret;
}

/**
 * set_core_mask - Store callback for the core_mask sysfs file.
 *
 * This function is called when the core_mask sysfs file is written to.
 *
 * @dev:	The device with sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The value written to the sysfs file
 * @count:	The number of bytes written to the sysfs file
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t set_core_mask(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	u64 new_core_mask[3];
	int items;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	items = sscanf(buf, "%llx %llx %llx",
			&new_core_mask[0], &new_core_mask[1],
			&new_core_mask[2]);

	if (items == 1)
		new_core_mask[1] = new_core_mask[2] = new_core_mask[0];

	if (items == 1 || items == 3) {
		u64 shader_present =
				kbdev->gpu_props.props.raw_props.shader_present;
		u64 group0_core_mask =
				kbdev->gpu_props.props.coherency_info.group[0].
				core_mask;

		if ((new_core_mask[0] & shader_present) != new_core_mask[0] ||
				!(new_core_mask[0] & group0_core_mask) ||
			(new_core_mask[1] & shader_present) !=
						new_core_mask[1] ||
				!(new_core_mask[1] & group0_core_mask) ||
			(new_core_mask[2] & shader_present) !=
						new_core_mask[2] ||
				!(new_core_mask[2] & group0_core_mask)) {
			dev_err(dev, "power_policy: invalid core specification\n");
			return -EINVAL;
		}

		if (kbdev->pm.debug_core_mask[0] != new_core_mask[0] ||
				kbdev->pm.debug_core_mask[1] !=
						new_core_mask[1] ||
				kbdev->pm.debug_core_mask[2] !=
						new_core_mask[2]) {
			unsigned long flags;

			spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

			kbase_pm_set_debug_core_mask(kbdev, new_core_mask[0],
					new_core_mask[1], new_core_mask[2]);

			spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
		}

		return count;
	}

	dev_err(kbdev->dev, "Couldn't process set_core_mask write operation.\n"
		"Use format <core_mask>\n"
		"or <core_mask_js0> <core_mask_js1> <core_mask_js2>\n");
	return -EINVAL;
}

/*
 * The sysfs file core_mask.
 *
 * This is used to restrict shader core availability for debugging purposes.
 * Reading it will show the current core mask and the mask of cores available.
 * Writing to it will set the current core mask.
 */
static DEVICE_ATTR(core_mask, S_IRUGO | S_IWUSR, show_core_mask, set_core_mask);

/**
 * set_soft_job_timeout - Store callback for the soft_job_timeout sysfs
 * file.
 *
 * @dev: The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf: The value written to the sysfs file.
 * @count: The number of bytes written to the sysfs file.
 *
 * This allows setting the timeout for software jobs. Waiting soft event wait
 * jobs will be cancelled after this period expires, while soft fence wait jobs
 * will print debug information if the fence debug feature is enabled.
 *
 * This is expressed in milliseconds.
 *
 * Return: count if the function succeeded. An error code on failure.
 */
static ssize_t set_soft_job_timeout(struct device *dev,
				      struct device_attribute *attr,
				      const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int soft_job_timeout_ms;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	if ((kstrtoint(buf, 0, &soft_job_timeout_ms) != 0) ||
	    (soft_job_timeout_ms <= 0))
		return -EINVAL;

	atomic_set(&kbdev->js_data.soft_job_timeout_ms,
		   soft_job_timeout_ms);

	return count;
}

/**
 * show_soft_job_timeout - Show callback for the soft_job_timeout sysfs
 * file.
 *
 * This will return the timeout for the software jobs.
 *
 * @dev: The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf: The output buffer for the sysfs file contents.
 *
 * Return: The number of bytes output to buf.
 */
static ssize_t show_soft_job_timeout(struct device *dev,
				       struct device_attribute *attr,
				       char * const buf)
{
	struct kbase_device *kbdev;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%i\n",
			 atomic_read(&kbdev->js_data.soft_job_timeout_ms));
}

static DEVICE_ATTR(soft_job_timeout, S_IRUGO | S_IWUSR,
		   show_soft_job_timeout, set_soft_job_timeout);

static u32 timeout_ms_to_ticks(struct kbase_device *kbdev, long timeout_ms,
				int default_ticks, u32 old_ticks)
{
	if (timeout_ms > 0) {
		u64 ticks = timeout_ms * 1000000ULL;
		do_div(ticks, kbdev->js_data.scheduling_period_ns);
		if (!ticks)
			return 1;
		return ticks;
	} else if (timeout_ms < 0) {
		return default_ticks;
	} else {
		return old_ticks;
	}
}

/**
 * set_js_timeouts - Store callback for the js_timeouts sysfs file.
 *
 * This function is called to get the contents of the js_timeouts sysfs
 * file. This file contains five values separated by whitespace. The values
 * are basically the same as %JS_SOFT_STOP_TICKS, %JS_HARD_STOP_TICKS_SS,
 * %JS_HARD_STOP_TICKS_DUMPING, %JS_RESET_TICKS_SS, %JS_RESET_TICKS_DUMPING
 * configuration values (in that order), with the difference that the js_timeout
 * values are expressed in MILLISECONDS.
 *
 * The js_timeouts sysfile file allows the current values in
 * use by the job scheduler to get override. Note that a value needs to
 * be other than 0 for it to override the current job scheduler value.
 *
 * @dev:	The device with sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The value written to the sysfs file
 * @count:	The number of bytes written to the sysfs file
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t set_js_timeouts(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int items;
	long js_soft_stop_ms;
	long js_soft_stop_ms_cl;
	long js_hard_stop_ms_ss;
	long js_hard_stop_ms_cl;
	long js_hard_stop_ms_dumping;
	long js_reset_ms_ss;
	long js_reset_ms_cl;
	long js_reset_ms_dumping;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	items = sscanf(buf, "%ld %ld %ld %ld %ld %ld %ld %ld",
			&js_soft_stop_ms, &js_soft_stop_ms_cl,
			&js_hard_stop_ms_ss, &js_hard_stop_ms_cl,
			&js_hard_stop_ms_dumping, &js_reset_ms_ss,
			&js_reset_ms_cl, &js_reset_ms_dumping);

	if (items == 8) {
		struct kbasep_js_device_data *js_data = &kbdev->js_data;
		unsigned long flags;

		spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

#define UPDATE_TIMEOUT(ticks_name, ms_name, default) do {\
	js_data->ticks_name = timeout_ms_to_ticks(kbdev, ms_name, \
			default, js_data->ticks_name); \
	dev_dbg(kbdev->dev, "Overriding " #ticks_name \
			" with %lu ticks (%lu ms)\n", \
			(unsigned long)js_data->ticks_name, \
			ms_name); \
	} while (0)

		UPDATE_TIMEOUT(soft_stop_ticks, js_soft_stop_ms,
				DEFAULT_JS_SOFT_STOP_TICKS);
		UPDATE_TIMEOUT(soft_stop_ticks_cl, js_soft_stop_ms_cl,
				DEFAULT_JS_SOFT_STOP_TICKS_CL);
		UPDATE_TIMEOUT(hard_stop_ticks_ss, js_hard_stop_ms_ss,
				kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8408) ?
				DEFAULT_JS_HARD_STOP_TICKS_SS_8408 :
				DEFAULT_JS_HARD_STOP_TICKS_SS);
		UPDATE_TIMEOUT(hard_stop_ticks_cl, js_hard_stop_ms_cl,
				DEFAULT_JS_HARD_STOP_TICKS_CL);
		UPDATE_TIMEOUT(hard_stop_ticks_dumping,
				js_hard_stop_ms_dumping,
				DEFAULT_JS_HARD_STOP_TICKS_DUMPING);
		UPDATE_TIMEOUT(gpu_reset_ticks_ss, js_reset_ms_ss,
				kbase_hw_has_issue(kbdev, BASE_HW_ISSUE_8408) ?
				DEFAULT_JS_RESET_TICKS_SS_8408 :
				DEFAULT_JS_RESET_TICKS_SS);
		UPDATE_TIMEOUT(gpu_reset_ticks_cl, js_reset_ms_cl,
				DEFAULT_JS_RESET_TICKS_CL);
		UPDATE_TIMEOUT(gpu_reset_ticks_dumping, js_reset_ms_dumping,
				DEFAULT_JS_RESET_TICKS_DUMPING);

		kbase_js_set_timeouts(kbdev);

		spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

		return count;
	}

	dev_err(kbdev->dev, "Couldn't process js_timeouts write operation.\n"
			"Use format <soft_stop_ms> <soft_stop_ms_cl> <hard_stop_ms_ss> <hard_stop_ms_cl> <hard_stop_ms_dumping> <reset_ms_ss> <reset_ms_cl> <reset_ms_dumping>\n"
			"Write 0 for no change, -1 to restore default timeout\n");
	return -EINVAL;
}

static unsigned long get_js_timeout_in_ms(
		u32 scheduling_period_ns,
		u32 ticks)
{
	u64 ms = (u64)ticks * scheduling_period_ns;

	do_div(ms, 1000000UL);
	return ms;
}

/**
 * show_js_timeouts - Show callback for the js_timeouts sysfs file.
 *
 * This function is called to get the contents of the js_timeouts sysfs
 * file. It returns the last set values written to the js_timeouts sysfs file.
 * If the file didn't get written yet, the values will be current setting in
 * use.
 * @dev:	The device this sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The output buffer for the sysfs file contents
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t show_js_timeouts(struct device *dev, struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;
	unsigned long js_soft_stop_ms;
	unsigned long js_soft_stop_ms_cl;
	unsigned long js_hard_stop_ms_ss;
	unsigned long js_hard_stop_ms_cl;
	unsigned long js_hard_stop_ms_dumping;
	unsigned long js_reset_ms_ss;
	unsigned long js_reset_ms_cl;
	unsigned long js_reset_ms_dumping;
	u32 scheduling_period_ns;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	scheduling_period_ns = kbdev->js_data.scheduling_period_ns;

#define GET_TIMEOUT(name) get_js_timeout_in_ms(\
		scheduling_period_ns, \
		kbdev->js_data.name)

	js_soft_stop_ms = GET_TIMEOUT(soft_stop_ticks);
	js_soft_stop_ms_cl = GET_TIMEOUT(soft_stop_ticks_cl);
	js_hard_stop_ms_ss = GET_TIMEOUT(hard_stop_ticks_ss);
	js_hard_stop_ms_cl = GET_TIMEOUT(hard_stop_ticks_cl);
	js_hard_stop_ms_dumping = GET_TIMEOUT(hard_stop_ticks_dumping);
	js_reset_ms_ss = GET_TIMEOUT(gpu_reset_ticks_ss);
	js_reset_ms_cl = GET_TIMEOUT(gpu_reset_ticks_cl);
	js_reset_ms_dumping = GET_TIMEOUT(gpu_reset_ticks_dumping);

#undef GET_TIMEOUT

	ret = scnprintf(buf, PAGE_SIZE, "%lu %lu %lu %lu %lu %lu %lu %lu\n",
			js_soft_stop_ms, js_soft_stop_ms_cl,
			js_hard_stop_ms_ss, js_hard_stop_ms_cl,
			js_hard_stop_ms_dumping, js_reset_ms_ss,
			js_reset_ms_cl, js_reset_ms_dumping);

	if (ret >= PAGE_SIZE) {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

/*
 * The sysfs file js_timeouts.
 *
 * This is used to override the current job scheduler values for
 * JS_STOP_STOP_TICKS_SS
 * JS_STOP_STOP_TICKS_CL
 * JS_HARD_STOP_TICKS_SS
 * JS_HARD_STOP_TICKS_CL
 * JS_HARD_STOP_TICKS_DUMPING
 * JS_RESET_TICKS_SS
 * JS_RESET_TICKS_CL
 * JS_RESET_TICKS_DUMPING.
 */
static DEVICE_ATTR(js_timeouts, S_IRUGO | S_IWUSR, show_js_timeouts, set_js_timeouts);

static u32 get_new_js_timeout(
		u32 old_period,
		u32 old_ticks,
		u32 new_scheduling_period_ns)
{
	u64 ticks = (u64)old_period * (u64)old_ticks;
	do_div(ticks, new_scheduling_period_ns);
	return ticks?ticks:1;
}

/**
 * set_js_scheduling_period - Store callback for the js_scheduling_period sysfs
 *                            file
 * @dev:   The device the sysfs file is for
 * @attr:  The attributes of the sysfs file
 * @buf:   The value written to the sysfs file
 * @count: The number of bytes written to the sysfs file
 *
 * This function is called when the js_scheduling_period sysfs file is written
 * to. It checks the data written, and if valid updates the js_scheduling_period
 * value
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t set_js_scheduling_period(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	unsigned int js_scheduling_period;
	u32 new_scheduling_period_ns;
	u32 old_period;
	struct kbasep_js_device_data *js_data;
	unsigned long flags;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	js_data = &kbdev->js_data;

	ret = kstrtouint(buf, 0, &js_scheduling_period);
	if (ret || !js_scheduling_period) {
		dev_err(kbdev->dev, "Couldn't process js_scheduling_period write operation.\n"
				"Use format <js_scheduling_period_ms>\n");
		return -EINVAL;
	}

	new_scheduling_period_ns = js_scheduling_period * 1000000;

	/* Update scheduling timeouts */
	mutex_lock(&js_data->runpool_mutex);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	/* If no contexts have been scheduled since js_timeouts was last written
	 * to, the new timeouts might not have been latched yet. So check if an
	 * update is pending and use the new values if necessary. */

	/* Use previous 'new' scheduling period as a base if present. */
	old_period = js_data->scheduling_period_ns;

#define SET_TIMEOUT(name) \
		(js_data->name = get_new_js_timeout(\
				old_period, \
				kbdev->js_data.name, \
				new_scheduling_period_ns))

	SET_TIMEOUT(soft_stop_ticks);
	SET_TIMEOUT(soft_stop_ticks_cl);
	SET_TIMEOUT(hard_stop_ticks_ss);
	SET_TIMEOUT(hard_stop_ticks_cl);
	SET_TIMEOUT(hard_stop_ticks_dumping);
	SET_TIMEOUT(gpu_reset_ticks_ss);
	SET_TIMEOUT(gpu_reset_ticks_cl);
	SET_TIMEOUT(gpu_reset_ticks_dumping);

#undef SET_TIMEOUT

	js_data->scheduling_period_ns = new_scheduling_period_ns;

	kbase_js_set_timeouts(kbdev);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&js_data->runpool_mutex);

	dev_dbg(kbdev->dev, "JS scheduling period: %dms\n",
			js_scheduling_period);

	return count;
}

/**
 * show_js_scheduling_period - Show callback for the js_scheduling_period sysfs
 *                             entry.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the GPU information.
 *
 * This function is called to get the current period used for the JS scheduling
 * period.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t show_js_scheduling_period(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	u32 period;
	ssize_t ret;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	period = kbdev->js_data.scheduling_period_ns;

	ret = scnprintf(buf, PAGE_SIZE, "%d\n",
			period / 1000000);

	return ret;
}

static DEVICE_ATTR(js_scheduling_period, S_IRUGO | S_IWUSR,
		show_js_scheduling_period, set_js_scheduling_period);

#if !MALI_CUSTOMER_RELEASE
/**
 * set_force_replay - Store callback for the force_replay sysfs file.
 *
 * @dev:	The device with sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The value written to the sysfs file
 * @count:	The number of bytes written to the sysfs file
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t set_force_replay(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	if (!strncmp("limit=", buf, MIN(6, count))) {
		int force_replay_limit;
		int items = sscanf(buf, "limit=%u", &force_replay_limit);

		if (items == 1) {
			kbdev->force_replay_random = false;
			kbdev->force_replay_limit = force_replay_limit;
			kbdev->force_replay_count = 0;

			return count;
		}
	} else if (!strncmp("random_limit", buf, MIN(12, count))) {
		kbdev->force_replay_random = true;
		kbdev->force_replay_count = 0;

		return count;
	} else if (!strncmp("norandom_limit", buf, MIN(14, count))) {
		kbdev->force_replay_random = false;
		kbdev->force_replay_limit = KBASEP_FORCE_REPLAY_DISABLED;
		kbdev->force_replay_count = 0;

		return count;
	} else if (!strncmp("core_req=", buf, MIN(9, count))) {
		unsigned int core_req;
		int items = sscanf(buf, "core_req=%x", &core_req);

		if (items == 1) {
			kbdev->force_replay_core_req = (base_jd_core_req)core_req;

			return count;
		}
	}
	dev_err(kbdev->dev, "Couldn't process force_replay write operation.\nPossible settings: limit=<limit>, random_limit, norandom_limit, core_req=<core_req>\n");
	return -EINVAL;
}

/**
 * show_force_replay - Show callback for the force_replay sysfs file.
 *
 * This function is called to get the contents of the force_replay sysfs
 * file. It returns the last set value written to the force_replay sysfs file.
 * If the file didn't get written yet, the values will be 0.
 *
 * @dev:	The device this sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The output buffer for the sysfs file contents
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t show_force_replay(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	if (kbdev->force_replay_random)
		ret = scnprintf(buf, PAGE_SIZE,
				"limit=0\nrandom_limit\ncore_req=%x\n",
				kbdev->force_replay_core_req);
	else
		ret = scnprintf(buf, PAGE_SIZE,
				"limit=%u\nnorandom_limit\ncore_req=%x\n",
				kbdev->force_replay_limit,
				kbdev->force_replay_core_req);

	if (ret >= PAGE_SIZE) {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

/*
 * The sysfs file force_replay.
 */
static DEVICE_ATTR(force_replay, S_IRUGO | S_IWUSR, show_force_replay,
		set_force_replay);
#endif /* !MALI_CUSTOMER_RELEASE */

#ifdef CONFIG_MALI_BIFROST_DEBUG
static ssize_t set_js_softstop_always(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	int softstop_always;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = kstrtoint(buf, 0, &softstop_always);
	if (ret || ((softstop_always != 0) && (softstop_always != 1))) {
		dev_err(kbdev->dev, "Couldn't process js_softstop_always write operation.\n"
				"Use format <soft_stop_always>\n");
		return -EINVAL;
	}

	kbdev->js_data.softstop_always = (bool) softstop_always;
	dev_dbg(kbdev->dev, "Support for softstop on a single context: %s\n",
			(kbdev->js_data.softstop_always) ?
			"Enabled" : "Disabled");
	return count;
}

static ssize_t show_js_softstop_always(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", kbdev->js_data.softstop_always);

	if (ret >= PAGE_SIZE) {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

/*
 * By default, soft-stops are disabled when only a single context is present.
 * The ability to enable soft-stop when only a single context is present can be
 * used for debug and unit-testing purposes.
 * (see CL t6xx_stress_1 unit-test as an example whereby this feature is used.)
 */
static DEVICE_ATTR(js_softstop_always, S_IRUGO | S_IWUSR, show_js_softstop_always, set_js_softstop_always);
#endif /* CONFIG_MALI_BIFROST_DEBUG */

#ifdef CONFIG_MALI_BIFROST_DEBUG
typedef void (kbasep_debug_command_func) (struct kbase_device *);

enum kbasep_debug_command_code {
	KBASEP_DEBUG_COMMAND_DUMPTRACE,

	/* This must be the last enum */
	KBASEP_DEBUG_COMMAND_COUNT
};

struct kbasep_debug_command {
	char *str;
	kbasep_debug_command_func *func;
};

/* Debug commands supported by the driver */
static const struct kbasep_debug_command debug_commands[] = {
	{
	 .str = "dumptrace",
	 .func = &kbasep_trace_dump,
	 }
};

/**
 * show_debug - Show callback for the debug_command sysfs file.
 *
 * This function is called to get the contents of the debug_command sysfs
 * file. This is a list of the available debug commands, separated by newlines.
 *
 * @dev:	The device this sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The output buffer for the sysfs file contents
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t show_debug(struct device *dev, struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	int i;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	for (i = 0; i < KBASEP_DEBUG_COMMAND_COUNT && ret < PAGE_SIZE; i++)
		ret += scnprintf(buf + ret, PAGE_SIZE - ret, "%s\n", debug_commands[i].str);

	if (ret >= PAGE_SIZE) {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

/**
 * issue_debug - Store callback for the debug_command sysfs file.
 *
 * This function is called when the debug_command sysfs file is written to.
 * It matches the requested command against the available commands, and if
 * a matching command is found calls the associated function from
 * @debug_commands to issue the command.
 *
 * @dev:	The device with sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The value written to the sysfs file
 * @count:	The number of bytes written to the sysfs file
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t issue_debug(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int i;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	for (i = 0; i < KBASEP_DEBUG_COMMAND_COUNT; i++) {
		if (sysfs_streq(debug_commands[i].str, buf)) {
			debug_commands[i].func(kbdev);
			return count;
		}
	}

	/* Debug Command not found */
	dev_err(dev, "debug_command: command not known\n");
	return -EINVAL;
}

/* The sysfs file debug_command.
 *
 * This is used to issue general debug commands to the device driver.
 * Reading it will produce a list of debug commands, separated by newlines.
 * Writing to it with one of those commands will issue said command.
 */
static DEVICE_ATTR(debug_command, S_IRUGO | S_IWUSR, show_debug, issue_debug);
#endif /* CONFIG_MALI_BIFROST_DEBUG */

/**
 * kbase_show_gpuinfo - Show callback for the gpuinfo sysfs entry.
 * @dev: The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf: The output buffer to receive the GPU information.
 *
 * This function is called to get a description of the present Mali
 * GPU via the gpuinfo sysfs entry.  This includes the GPU family, the
 * number of cores, the hardware version and the raw product id.  For
 * example
 *
 *    Mali-T60x MP4 r0p0 0x6956
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t kbase_show_gpuinfo(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	static const struct gpu_product_id_name {
		unsigned id;
		char *name;
	} gpu_product_id_names[] = {
		{ .id = GPU_ID_PI_T60X, .name = "Mali-T60x" },
		{ .id = GPU_ID_PI_T62X, .name = "Mali-T62x" },
		{ .id = GPU_ID_PI_T72X, .name = "Mali-T72x" },
		{ .id = GPU_ID_PI_T76X, .name = "Mali-T76x" },
		{ .id = GPU_ID_PI_T82X, .name = "Mali-T82x" },
		{ .id = GPU_ID_PI_T83X, .name = "Mali-T83x" },
		{ .id = GPU_ID_PI_T86X, .name = "Mali-T86x" },
		{ .id = GPU_ID_PI_TFRX, .name = "Mali-T88x" },
		{ .id = GPU_ID2_PRODUCT_TMIX >> GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G71" },
		{ .id = GPU_ID2_PRODUCT_THEX >> GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G72" },
		{ .id = GPU_ID2_PRODUCT_TSIX >> GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G51" },
		{ .id = GPU_ID2_PRODUCT_TDVX >> GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G31" },
	};
	const char *product_name = "(Unknown Mali GPU)";
	struct kbase_device *kbdev;
	u32 gpu_id;
	unsigned product_id, product_id_mask;
	unsigned i;
	bool is_new_format;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	gpu_id = kbdev->gpu_props.props.raw_props.gpu_id;
	product_id = gpu_id >> GPU_ID_VERSION_PRODUCT_ID_SHIFT;
	is_new_format = GPU_ID_IS_NEW_FORMAT(product_id);
	product_id_mask =
		(is_new_format ?
			GPU_ID2_PRODUCT_MODEL :
			GPU_ID_VERSION_PRODUCT_ID) >>
		GPU_ID_VERSION_PRODUCT_ID_SHIFT;

	for (i = 0; i < ARRAY_SIZE(gpu_product_id_names); ++i) {
		const struct gpu_product_id_name *p = &gpu_product_id_names[i];

		if ((GPU_ID_IS_NEW_FORMAT(p->id) == is_new_format) &&
		    (p->id & product_id_mask) ==
		    (product_id & product_id_mask)) {
			product_name = p->name;
			break;
		}
	}

	return scnprintf(buf, PAGE_SIZE, "%s %d cores 2EE r%dp%d 0x%04X\n",
		product_name, kbdev->gpu_props.num_cores,
		(gpu_id & GPU_ID_VERSION_MAJOR) >> GPU_ID_VERSION_MAJOR_SHIFT,
		(gpu_id & GPU_ID_VERSION_MINOR) >> GPU_ID_VERSION_MINOR_SHIFT,
		product_id);
}
static DEVICE_ATTR(gpuinfo, S_IRUGO, kbase_show_gpuinfo, NULL);

/**
 * set_dvfs_period - Store callback for the dvfs_period sysfs file.
 * @dev:   The device with sysfs file is for
 * @attr:  The attributes of the sysfs file
 * @buf:   The value written to the sysfs file
 * @count: The number of bytes written to the sysfs file
 *
 * This function is called when the dvfs_period sysfs file is written to. It
 * checks the data written, and if valid updates the DVFS period variable,
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t set_dvfs_period(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	int dvfs_period;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = kstrtoint(buf, 0, &dvfs_period);
	if (ret || dvfs_period <= 0) {
		dev_err(kbdev->dev, "Couldn't process dvfs_period write operation.\n"
				"Use format <dvfs_period_ms>\n");
		return -EINVAL;
	}

	kbdev->pm.dvfs_period = dvfs_period;
	dev_dbg(kbdev->dev, "DVFS period: %dms\n", dvfs_period);

	return count;
}

/**
 * show_dvfs_period - Show callback for the dvfs_period sysfs entry.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the GPU information.
 *
 * This function is called to get the current period used for the DVFS sample
 * timer.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t show_dvfs_period(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", kbdev->pm.dvfs_period);

	return ret;
}

static DEVICE_ATTR(dvfs_period, S_IRUGO | S_IWUSR, show_dvfs_period,
		set_dvfs_period);

/**
 * set_pm_poweroff - Store callback for the pm_poweroff sysfs file.
 * @dev:   The device with sysfs file is for
 * @attr:  The attributes of the sysfs file
 * @buf:   The value written to the sysfs file
 * @count: The number of bytes written to the sysfs file
 *
 * This function is called when the pm_poweroff sysfs file is written to.
 *
 * This file contains three values separated by whitespace. The values
 * are gpu_poweroff_time (the period of the poweroff timer, in ns),
 * poweroff_shader_ticks (the number of poweroff timer ticks before an idle
 * shader is powered off), and poweroff_gpu_ticks (the number of poweroff timer
 * ticks before the GPU is powered off), in that order.
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t set_pm_poweroff(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int items;
	s64 gpu_poweroff_time;
	int poweroff_shader_ticks, poweroff_gpu_ticks;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	items = sscanf(buf, "%llu %u %u", &gpu_poweroff_time,
			&poweroff_shader_ticks,
			&poweroff_gpu_ticks);
	if (items != 3) {
		dev_err(kbdev->dev, "Couldn't process pm_poweroff write operation.\n"
				"Use format <gpu_poweroff_time_ns> <poweroff_shader_ticks> <poweroff_gpu_ticks>\n");
		return -EINVAL;
	}

	kbdev->pm.gpu_poweroff_time = HR_TIMER_DELAY_NSEC(gpu_poweroff_time);
	kbdev->pm.poweroff_shader_ticks = poweroff_shader_ticks;
	kbdev->pm.poweroff_gpu_ticks = poweroff_gpu_ticks;

	return count;
}

/**
 * show_pm_poweroff - Show callback for the pm_poweroff sysfs entry.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the GPU information.
 *
 * This function is called to get the current period used for the DVFS sample
 * timer.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t show_pm_poweroff(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = scnprintf(buf, PAGE_SIZE, "%llu %u %u\n",
			ktime_to_ns(kbdev->pm.gpu_poweroff_time),
			kbdev->pm.poweroff_shader_ticks,
			kbdev->pm.poweroff_gpu_ticks);

	return ret;
}

static DEVICE_ATTR(pm_poweroff, S_IRUGO | S_IWUSR, show_pm_poweroff,
		set_pm_poweroff);

/**
 * set_reset_timeout - Store callback for the reset_timeout sysfs file.
 * @dev:   The device with sysfs file is for
 * @attr:  The attributes of the sysfs file
 * @buf:   The value written to the sysfs file
 * @count: The number of bytes written to the sysfs file
 *
 * This function is called when the reset_timeout sysfs file is written to. It
 * checks the data written, and if valid updates the reset timeout.
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t set_reset_timeout(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	int reset_timeout;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = kstrtoint(buf, 0, &reset_timeout);
	if (ret || reset_timeout <= 0) {
		dev_err(kbdev->dev, "Couldn't process reset_timeout write operation.\n"
				"Use format <reset_timeout_ms>\n");
		return -EINVAL;
	}

	kbdev->reset_timeout_ms = reset_timeout;
	dev_dbg(kbdev->dev, "Reset timeout: %dms\n", reset_timeout);

	return count;
}

/**
 * show_reset_timeout - Show callback for the reset_timeout sysfs entry.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the GPU information.
 *
 * This function is called to get the current reset timeout.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t show_reset_timeout(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = scnprintf(buf, PAGE_SIZE, "%d\n", kbdev->reset_timeout_ms);

	return ret;
}

static DEVICE_ATTR(reset_timeout, S_IRUGO | S_IWUSR, show_reset_timeout,
		set_reset_timeout);



static ssize_t show_mem_pool_size(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = scnprintf(buf, PAGE_SIZE, "%zu\n",
			kbase_mem_pool_size(&kbdev->mem_pool));

	return ret;
}

static ssize_t set_mem_pool_size(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	size_t new_size;
	int err;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	err = kstrtoul(buf, 0, (unsigned long *)&new_size);
	if (err)
		return err;

	kbase_mem_pool_trim(&kbdev->mem_pool, new_size);

	return count;
}

static DEVICE_ATTR(mem_pool_size, S_IRUGO | S_IWUSR, show_mem_pool_size,
		set_mem_pool_size);

static ssize_t show_mem_pool_max_size(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = scnprintf(buf, PAGE_SIZE, "%zu\n",
			kbase_mem_pool_max_size(&kbdev->mem_pool));

	return ret;
}

static ssize_t set_mem_pool_max_size(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	size_t new_max_size;
	int err;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	err = kstrtoul(buf, 0, (unsigned long *)&new_max_size);
	if (err)
		return -EINVAL;

	kbase_mem_pool_set_max_size(&kbdev->mem_pool, new_max_size);

	return count;
}

static DEVICE_ATTR(mem_pool_max_size, S_IRUGO | S_IWUSR, show_mem_pool_max_size,
		set_mem_pool_max_size);

/**
 * show_lp_mem_pool_size - Show size of the large memory pages pool.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the pool size.
 *
 * This function is called to get the number of large memory pages which currently populate the kbdev pool.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t show_lp_mem_pool_size(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%zu\n", kbase_mem_pool_size(&kbdev->lp_mem_pool));
}

/**
 * set_lp_mem_pool_size - Set size of the large memory pages pool.
 * @dev:   The device this sysfs file is for.
 * @attr:  The attributes of the sysfs file.
 * @buf:   The value written to the sysfs file.
 * @count: The number of bytes written to the sysfs file.
 *
 * This function is called to set the number of large memory pages which should populate the kbdev pool.
 * This may cause existing pages to be removed from the pool, or new pages to be created and then added to the pool.
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t set_lp_mem_pool_size(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	unsigned long new_size;
	int err;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	err = kstrtoul(buf, 0, &new_size);
	if (err)
		return err;

	kbase_mem_pool_trim(&kbdev->lp_mem_pool, new_size);

	return count;
}

static DEVICE_ATTR(lp_mem_pool_size, S_IRUGO | S_IWUSR, show_lp_mem_pool_size,
		set_lp_mem_pool_size);

/**
 * show_lp_mem_pool_max_size - Show maximum size of the large memory pages pool.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the pool size.
 *
 * This function is called to get the maximum number of large memory pages that the kbdev pool can possibly contain.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t show_lp_mem_pool_max_size(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%zu\n", kbase_mem_pool_max_size(&kbdev->lp_mem_pool));
}

/**
 * set_lp_mem_pool_max_size - Set maximum size of the large memory pages pool.
 * @dev:   The device this sysfs file is for.
 * @attr:  The attributes of the sysfs file.
 * @buf:   The value written to the sysfs file.
 * @count: The number of bytes written to the sysfs file.
 *
 * This function is called to set the maximum number of large memory pages that the kbdev pool can possibly contain.
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t set_lp_mem_pool_max_size(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	unsigned long new_max_size;
	int err;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	err = kstrtoul(buf, 0, &new_max_size);
	if (err)
		return -EINVAL;

	kbase_mem_pool_set_max_size(&kbdev->lp_mem_pool, new_max_size);

	return count;
}

static DEVICE_ATTR(lp_mem_pool_max_size, S_IRUGO | S_IWUSR, show_lp_mem_pool_max_size,
		set_lp_mem_pool_max_size);

#ifdef CONFIG_DEBUG_FS

/* Number of entries in serialize_jobs_settings[] */
#define NR_SERIALIZE_JOBS_SETTINGS 5
/* Maximum string length in serialize_jobs_settings[].name */
#define MAX_SERIALIZE_JOBS_NAME_LEN 16

static struct
{
	char *name;
	u8 setting;
} serialize_jobs_settings[NR_SERIALIZE_JOBS_SETTINGS] = {
	{"none", 0},
	{"intra-slot", KBASE_SERIALIZE_INTRA_SLOT},
	{"inter-slot", KBASE_SERIALIZE_INTER_SLOT},
	{"full", KBASE_SERIALIZE_INTRA_SLOT | KBASE_SERIALIZE_INTER_SLOT},
	{"full-reset", KBASE_SERIALIZE_INTRA_SLOT | KBASE_SERIALIZE_INTER_SLOT |
			KBASE_SERIALIZE_RESET}
};

/**
 * kbasep_serialize_jobs_seq_show - Show callback for the serialize_jobs debugfs
 *                                  file
 * @sfile: seq_file pointer
 * @data:  Private callback data
 *
 * This function is called to get the contents of the serialize_jobs debugfs
 * file. This is a list of the available settings with the currently active one
 * surrounded by square brackets.
 *
 * Return: 0 on success, or an error code on error
 */
static int kbasep_serialize_jobs_seq_show(struct seq_file *sfile, void *data)
{
	struct kbase_device *kbdev = sfile->private;
	int i;

	CSTD_UNUSED(data);

	for (i = 0; i < NR_SERIALIZE_JOBS_SETTINGS; i++) {
		if (kbdev->serialize_jobs == serialize_jobs_settings[i].setting)
			seq_printf(sfile, "[%s] ",
					serialize_jobs_settings[i].name);
		else
			seq_printf(sfile, "%s ",
					serialize_jobs_settings[i].name);
	}

	seq_puts(sfile, "\n");

	return 0;
}

/**
 * kbasep_serialize_jobs_debugfs_write - Store callback for the serialize_jobs
 *                                       debugfs file.
 * @file:  File pointer
 * @ubuf:  User buffer containing data to store
 * @count: Number of bytes in user buffer
 * @ppos:  File position
 *
 * This function is called when the serialize_jobs debugfs file is written to.
 * It matches the requested setting against the available settings and if a
 * matching setting is found updates kbdev->serialize_jobs.
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t kbasep_serialize_jobs_debugfs_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	struct seq_file *s = file->private_data;
	struct kbase_device *kbdev = s->private;
	char buf[MAX_SERIALIZE_JOBS_NAME_LEN];
	int i;
	bool valid = false;

	CSTD_UNUSED(ppos);

	count = min_t(size_t, sizeof(buf) - 1, count);
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;

	buf[count] = 0;

	for (i = 0; i < NR_SERIALIZE_JOBS_SETTINGS; i++) {
		if (sysfs_streq(serialize_jobs_settings[i].name, buf)) {
			kbdev->serialize_jobs =
					serialize_jobs_settings[i].setting;
			valid = true;
			break;
		}
	}

	if (!valid) {
		dev_err(kbdev->dev, "serialize_jobs: invalid setting\n");
		return -EINVAL;
	}

	return count;
}

/**
 * kbasep_serialize_jobs_debugfs_open - Open callback for the serialize_jobs
 *                                     debugfs file
 * @in:   inode pointer
 * @file: file pointer
 *
 * Return: Zero on success, error code on failure
 */
static int kbasep_serialize_jobs_debugfs_open(struct inode *in,
		struct file *file)
{
	return single_open(file, kbasep_serialize_jobs_seq_show, in->i_private);
}

static const struct file_operations kbasep_serialize_jobs_debugfs_fops = {
	.open = kbasep_serialize_jobs_debugfs_open,
	.read = seq_read,
	.write = kbasep_serialize_jobs_debugfs_write,
	.llseek = seq_lseek,
	.release = single_release,
};

#endif /* CONFIG_DEBUG_FS */

static int kbasep_protected_mode_init(struct kbase_device *kbdev)
{
#ifdef CONFIG_OF
	struct device_node *protected_node;
	struct platform_device *pdev;
	struct protected_mode_device *protected_dev;
#endif

	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_PROTECTED_MODE)) {
		/* Use native protected ops */
		kbdev->protected_dev = kzalloc(sizeof(*kbdev->protected_dev),
				GFP_KERNEL);
		if (!kbdev->protected_dev)
			return -ENOMEM;
		kbdev->protected_dev->data = kbdev;
		kbdev->protected_ops = &kbase_native_protected_ops;
		kbdev->protected_mode_support = true;
		return 0;
	}

	kbdev->protected_mode_support = false;

#ifdef CONFIG_OF
	protected_node = of_parse_phandle(kbdev->dev->of_node,
			"protected-mode-switcher", 0);

	if (!protected_node)
		protected_node = of_parse_phandle(kbdev->dev->of_node,
				"secure-mode-switcher", 0);

	if (!protected_node) {
		/* If protected_node cannot be looked up then we assume
		 * protected mode is not supported on this platform. */
		dev_info(kbdev->dev, "Protected mode not available\n");
		return 0;
	}

	pdev = of_find_device_by_node(protected_node);
	if (!pdev)
		return -EINVAL;

	protected_dev = platform_get_drvdata(pdev);
	if (!protected_dev)
		return -EPROBE_DEFER;

	kbdev->protected_ops = &protected_dev->ops;
	kbdev->protected_dev = protected_dev;

	if (kbdev->protected_ops) {
		int err;

		/* Make sure protected mode is disabled on startup */
		mutex_lock(&kbdev->pm.lock);
		err = kbdev->protected_ops->protected_mode_disable(
				kbdev->protected_dev);
		mutex_unlock(&kbdev->pm.lock);

		/* protected_mode_disable() returns -EINVAL if not supported */
		kbdev->protected_mode_support = (err != -EINVAL);
	}
#endif
	return 0;
}

static void kbasep_protected_mode_term(struct kbase_device *kbdev)
{
	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_PROTECTED_MODE))
		kfree(kbdev->protected_dev);
}

#ifdef CONFIG_MALI_BIFROST_NO_MALI
static int kbase_common_reg_map(struct kbase_device *kbdev)
{
	return 0;
}
static void kbase_common_reg_unmap(struct kbase_device * const kbdev)
{
}
#else /* CONFIG_MALI_BIFROST_NO_MALI */
static int kbase_common_reg_map(struct kbase_device *kbdev)
{
	int err = 0;

	if (!request_mem_region(kbdev->reg_start, kbdev->reg_size, dev_name(kbdev->dev))) {
		dev_err(kbdev->dev, "Register window unavailable\n");
		err = -EIO;
		goto out_region;
	}

	kbdev->reg = ioremap(kbdev->reg_start, kbdev->reg_size);
	if (!kbdev->reg) {
		dev_err(kbdev->dev, "Can't remap register window\n");
		err = -EINVAL;
		goto out_ioremap;
	}

	return err;

 out_ioremap:
	release_mem_region(kbdev->reg_start, kbdev->reg_size);
 out_region:
	return err;
}

static void kbase_common_reg_unmap(struct kbase_device * const kbdev)
{
	if (kbdev->reg) {
		iounmap(kbdev->reg);
		release_mem_region(kbdev->reg_start, kbdev->reg_size);
		kbdev->reg = NULL;
		kbdev->reg_start = 0;
		kbdev->reg_size = 0;
	}
}
#endif /* CONFIG_MALI_BIFROST_NO_MALI */

static int registers_map(struct kbase_device * const kbdev)
{

		/* the first memory resource is the physical address of the GPU
		 * registers */
		struct platform_device *pdev = to_platform_device(kbdev->dev);
		struct resource *reg_res;
		int err;

		reg_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
		if (!reg_res) {
			dev_err(kbdev->dev, "Invalid register resource\n");
			return -ENOENT;
		}

		kbdev->reg_start = reg_res->start;
		kbdev->reg_size = resource_size(reg_res);

		err = kbase_common_reg_map(kbdev);
		if (err) {
			dev_err(kbdev->dev, "Failed to map registers\n");
			return err;
		}

	return 0;
}

static void registers_unmap(struct kbase_device *kbdev)
{
	kbase_common_reg_unmap(kbdev);
}

static int power_control_init(struct platform_device *pdev)
{
	struct kbase_device *kbdev = to_kbase_device(&pdev->dev);
	int err = 0;

	if (!kbdev)
		return -ENODEV;

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)) && defined(CONFIG_OF) \
			&& defined(CONFIG_REGULATOR)
	kbdev->regulator = regulator_get_optional(kbdev->dev, "mali");
	if (IS_ERR_OR_NULL(kbdev->regulator)) {
		err = PTR_ERR(kbdev->regulator);
		kbdev->regulator = NULL;
		if (err == -EPROBE_DEFER) {
			dev_err(&pdev->dev, "Failed to get regulator\n");
			return err;
		}
		dev_info(kbdev->dev,
			"Continuing without Mali regulator control\n");
		/* Allow probe to continue without regulator */
	}
#endif /* LINUX_VERSION_CODE >= 3, 12, 0 */

	kbdev->clock = clk_get(kbdev->dev, "clk_mali");
	if (IS_ERR_OR_NULL(kbdev->clock)) {
		err = PTR_ERR(kbdev->clock);
		kbdev->clock = NULL;
		if (err == -EPROBE_DEFER) {
			dev_err(&pdev->dev, "Failed to get clock\n");
			goto fail;
		}
		dev_info(kbdev->dev, "Continuing without Mali clock control\n");
		/* Allow probe to continue without clock. */
	} else {
		err = clk_prepare(kbdev->clock);
		if (err) {
			dev_err(kbdev->dev,
				"Failed to prepare and enable clock (%d)\n",
				err);
			goto fail;
		}
	}

#if defined(CONFIG_OF) && defined(CONFIG_PM_OPP)
	/* Register the OPPs if they are available in device tree */
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)) \
	|| defined(LSK_OPPV2_BACKPORT)
	err = dev_pm_opp_of_add_table(kbdev->dev);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 7, 0))
	err = of_init_opp_table(kbdev->dev);
#else
	err = 0;
#endif /* LINUX_VERSION_CODE */
	if (err)
		dev_dbg(kbdev->dev, "OPP table not found\n");
#endif /* CONFIG_OF && CONFIG_PM_OPP */

	return 0;

fail:

if (kbdev->clock != NULL) {
	clk_put(kbdev->clock);
	kbdev->clock = NULL;
}

#ifdef CONFIG_REGULATOR
	if (NULL != kbdev->regulator) {
		regulator_put(kbdev->regulator);
		kbdev->regulator = NULL;
	}
#endif

	return err;
}

static void power_control_term(struct kbase_device *kbdev)
{
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(4, 4, 0)) || \
		defined(LSK_OPPV2_BACKPORT)
	dev_pm_opp_of_remove_table(kbdev->dev);
#elif (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 19, 0))
	of_free_opp_table(kbdev->dev);
#endif

	if (kbdev->clock) {
		clk_unprepare(kbdev->clock);
		clk_put(kbdev->clock);
		kbdev->clock = NULL;
	}

#if (LINUX_VERSION_CODE >= KERNEL_VERSION(3, 12, 0)) && defined(CONFIG_OF) \
			&& defined(CONFIG_REGULATOR)
	if (kbdev->regulator) {
		regulator_put(kbdev->regulator);
		kbdev->regulator = NULL;
	}
#endif /* LINUX_VERSION_CODE >= 3, 12, 0 */
}

#ifdef CONFIG_DEBUG_FS

#if KBASE_GPU_RESET_EN
#include <mali_kbase_hwaccess_jm.h>

static void trigger_quirks_reload(struct kbase_device *kbdev)
{
	kbase_pm_context_active(kbdev);
	if (kbase_prepare_to_reset_gpu(kbdev))
		kbase_reset_gpu(kbdev);
	kbase_pm_context_idle(kbdev);
}

#define MAKE_QUIRK_ACCESSORS(type) \
static int type##_quirks_set(void *data, u64 val) \
{ \
	struct kbase_device *kbdev; \
	kbdev = (struct kbase_device *)data; \
	kbdev->hw_quirks_##type = (u32)val; \
	trigger_quirks_reload(kbdev); \
	return 0;\
} \
\
static int type##_quirks_get(void *data, u64 *val) \
{ \
	struct kbase_device *kbdev;\
	kbdev = (struct kbase_device *)data;\
	*val = kbdev->hw_quirks_##type;\
	return 0;\
} \
DEFINE_SIMPLE_ATTRIBUTE(fops_##type##_quirks, type##_quirks_get,\
		type##_quirks_set, "%llu\n")

MAKE_QUIRK_ACCESSORS(sc);
MAKE_QUIRK_ACCESSORS(tiler);
MAKE_QUIRK_ACCESSORS(mmu);
MAKE_QUIRK_ACCESSORS(jm);

#endif /* KBASE_GPU_RESET_EN */

/**
 * debugfs_protected_debug_mode_read - "protected_debug_mode" debugfs read
 * @file: File object to read is for
 * @buf:  User buffer to populate with data
 * @len:  Length of user buffer
 * @ppos: Offset within file object
 *
 * Retrieves the current status of protected debug mode
 * (0 = disabled, 1 = enabled)
 *
 * Return: Number of bytes added to user buffer
 */
static ssize_t debugfs_protected_debug_mode_read(struct file *file,
				char __user *buf, size_t len, loff_t *ppos)
{
	struct kbase_device *kbdev = (struct kbase_device *)file->private_data;
	u32 gpu_status;
	ssize_t ret_val;

	kbase_pm_context_active(kbdev);
	gpu_status = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_STATUS), NULL);
	kbase_pm_context_idle(kbdev);

	if (gpu_status & GPU_DBGEN)
		ret_val = simple_read_from_buffer(buf, len, ppos, "1\n", 2);
	else
		ret_val = simple_read_from_buffer(buf, len, ppos, "0\n", 2);

	return ret_val;
}

/*
 * struct fops_protected_debug_mode - "protected_debug_mode" debugfs fops
 *
 * Contains the file operations for the "protected_debug_mode" debugfs file
 */
static const struct file_operations fops_protected_debug_mode = {
	.open = simple_open,
	.read = debugfs_protected_debug_mode_read,
	.llseek = default_llseek,
};

static int kbase_device_debugfs_init(struct kbase_device *kbdev)
{
	struct dentry *debugfs_ctx_defaults_directory;
	int err;

	kbdev->mali_debugfs_directory = debugfs_create_dir(kbdev->devname,
			NULL);
	if (!kbdev->mali_debugfs_directory) {
		dev_err(kbdev->dev, "Couldn't create mali debugfs directory\n");
		err = -ENOMEM;
		goto out;
	}

	kbdev->debugfs_ctx_directory = debugfs_create_dir("ctx",
			kbdev->mali_debugfs_directory);
	if (!kbdev->debugfs_ctx_directory) {
		dev_err(kbdev->dev, "Couldn't create mali debugfs ctx directory\n");
		err = -ENOMEM;
		goto out;
	}

	debugfs_ctx_defaults_directory = debugfs_create_dir("defaults",
			kbdev->debugfs_ctx_directory);
	if (!debugfs_ctx_defaults_directory) {
		dev_err(kbdev->dev, "Couldn't create mali debugfs ctx defaults directory\n");
		err = -ENOMEM;
		goto out;
	}

#if !MALI_CUSTOMER_RELEASE
	kbasep_regs_dump_debugfs_init(kbdev);
#endif /* !MALI_CUSTOMER_RELEASE */
	kbasep_regs_history_debugfs_init(kbdev);

	kbase_debug_job_fault_debugfs_init(kbdev);
	kbasep_gpu_memory_debugfs_init(kbdev);
	kbase_as_fault_debugfs_init(kbdev);
#if KBASE_GPU_RESET_EN
	/* fops_* variables created by invocations of macro
	 * MAKE_QUIRK_ACCESSORS() above. */
	debugfs_create_file("quirks_sc", 0644,
			kbdev->mali_debugfs_directory, kbdev,
			&fops_sc_quirks);
	debugfs_create_file("quirks_tiler", 0644,
			kbdev->mali_debugfs_directory, kbdev,
			&fops_tiler_quirks);
	debugfs_create_file("quirks_mmu", 0644,
			kbdev->mali_debugfs_directory, kbdev,
			&fops_mmu_quirks);
	debugfs_create_file("quirks_jm", 0644,
			kbdev->mali_debugfs_directory, kbdev,
			&fops_jm_quirks);
#endif /* KBASE_GPU_RESET_EN */

	debugfs_create_bool("infinite_cache", 0644,
			debugfs_ctx_defaults_directory,
			&kbdev->infinite_cache_active_default);

	debugfs_create_size_t("mem_pool_max_size", 0644,
			debugfs_ctx_defaults_directory,
			&kbdev->mem_pool_max_size_default);

	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_PROTECTED_DEBUG_MODE)) {
		debugfs_create_file("protected_debug_mode", S_IRUGO,
				kbdev->mali_debugfs_directory, kbdev,
				&fops_protected_debug_mode);
	}

#if KBASE_TRACE_ENABLE
	kbasep_trace_debugfs_init(kbdev);
#endif /* KBASE_TRACE_ENABLE */

#ifdef CONFIG_MALI_BIFROST_TRACE_TIMELINE
	kbasep_trace_timeline_debugfs_init(kbdev);
#endif /* CONFIG_MALI_BIFROST_TRACE_TIMELINE */

#ifdef CONFIG_MALI_BIFROST_DEVFREQ
#ifdef CONFIG_DEVFREQ_THERMAL
	if (kbdev->inited_subsys & inited_devfreq)
		kbase_ipa_debugfs_init(kbdev);
#endif /* CONFIG_DEVFREQ_THERMAL */
#endif /* CONFIG_MALI_BIFROST_DEVFREQ */

#ifdef CONFIG_DEBUG_FS
	debugfs_create_file("serialize_jobs", S_IRUGO | S_IWUSR,
			kbdev->mali_debugfs_directory, kbdev,
			&kbasep_serialize_jobs_debugfs_fops);
#endif /* CONFIG_DEBUG_FS */

	return 0;

out:
	debugfs_remove_recursive(kbdev->mali_debugfs_directory);
	return err;
}

static void kbase_device_debugfs_term(struct kbase_device *kbdev)
{
	debugfs_remove_recursive(kbdev->mali_debugfs_directory);
}

#else /* CONFIG_DEBUG_FS */
static inline int kbase_device_debugfs_init(struct kbase_device *kbdev)
{
	return 0;
}

static inline void kbase_device_debugfs_term(struct kbase_device *kbdev) { }
#endif /* CONFIG_DEBUG_FS */

static void kbase_device_coherency_init(struct kbase_device *kbdev,
		unsigned prod_id)
{
#ifdef CONFIG_OF
	u32 supported_coherency_bitmap =
		kbdev->gpu_props.props.raw_props.coherency_mode;
	const void *coherency_override_dts;
	u32 override_coherency;

	/* Only for tMIx :
	 * (COHERENCY_ACE_LITE | COHERENCY_ACE) was incorrectly
	 * documented for tMIx so force correct value here.
	 */
	if (GPU_ID_IS_NEW_FORMAT(prod_id) &&
		   (GPU_ID2_MODEL_MATCH_VALUE(prod_id) ==
				   GPU_ID2_PRODUCT_TMIX))
		if (supported_coherency_bitmap ==
				COHERENCY_FEATURE_BIT(COHERENCY_ACE))
			supported_coherency_bitmap |=
				COHERENCY_FEATURE_BIT(COHERENCY_ACE_LITE);

#endif /* CONFIG_OF */

	kbdev->system_coherency = COHERENCY_NONE;

	/* device tree may override the coherency */
#ifdef CONFIG_OF
	coherency_override_dts = of_get_property(kbdev->dev->of_node,
						"system-coherency",
						NULL);
	if (coherency_override_dts) {

		override_coherency = be32_to_cpup(coherency_override_dts);

		if ((override_coherency <= COHERENCY_NONE) &&
			(supported_coherency_bitmap &
			 COHERENCY_FEATURE_BIT(override_coherency))) {

			kbdev->system_coherency = override_coherency;

			dev_info(kbdev->dev,
				"Using coherency mode %u set from dtb",
				override_coherency);
		} else
			dev_warn(kbdev->dev,
				"Ignoring unsupported coherency mode %u set from dtb",
				override_coherency);
	}

#endif /* CONFIG_OF */

	kbdev->gpu_props.props.raw_props.coherency_mode =
		kbdev->system_coherency;
}

#ifdef CONFIG_MALI_FPGA_BUS_LOGGER

/* Callback used by the kbase bus logger client, to initiate a GPU reset
 * when the bus log is restarted.  GPU reset is used as reference point
 * in HW bus log analyses.
 */
static void kbase_logging_started_cb(void *data)
{
	struct kbase_device *kbdev = (struct kbase_device *)data;

	if (kbase_prepare_to_reset_gpu(kbdev))
		kbase_reset_gpu(kbdev);
	dev_info(kbdev->dev, "KBASE - Bus logger restarted\n");
}
#endif

static struct attribute *kbase_attrs[] = {
#ifdef CONFIG_MALI_BIFROST_DEBUG
	&dev_attr_debug_command.attr,
	&dev_attr_js_softstop_always.attr,
#endif
#if !MALI_CUSTOMER_RELEASE
	&dev_attr_force_replay.attr,
#endif
	&dev_attr_js_timeouts.attr,
	&dev_attr_soft_job_timeout.attr,
	&dev_attr_gpuinfo.attr,
	&dev_attr_dvfs_period.attr,
	&dev_attr_pm_poweroff.attr,
	&dev_attr_reset_timeout.attr,
	&dev_attr_js_scheduling_period.attr,
	&dev_attr_power_policy.attr,
	&dev_attr_core_availability_policy.attr,
	&dev_attr_core_mask.attr,
	&dev_attr_mem_pool_size.attr,
	&dev_attr_mem_pool_max_size.attr,
	&dev_attr_lp_mem_pool_size.attr,
	&dev_attr_lp_mem_pool_max_size.attr,
	NULL
};

static const struct attribute_group kbase_attr_group = {
	.attrs = kbase_attrs,
};

static int kbase_platform_device_remove(struct platform_device *pdev)
{
	struct kbase_device *kbdev = to_kbase_device(&pdev->dev);
	const struct list_head *dev_list;

	if (!kbdev)
		return -ENODEV;

	kfree(kbdev->gpu_props.prop_buffer);

#ifdef CONFIG_MALI_FPGA_BUS_LOGGER
	if (kbdev->inited_subsys & inited_buslogger) {
		bl_core_client_unregister(kbdev->buslogger);
		kbdev->inited_subsys &= ~inited_buslogger;
	}
#endif


	if (kbdev->inited_subsys & inited_dev_list) {
		dev_list = kbase_dev_list_get();
		list_del(&kbdev->entry);
		kbase_dev_list_put(dev_list);
		kbdev->inited_subsys &= ~inited_dev_list;
	}

	if (kbdev->inited_subsys & inited_misc_register) {
		misc_deregister(&kbdev->mdev);
		kbdev->inited_subsys &= ~inited_misc_register;
	}

	if (kbdev->inited_subsys & inited_sysfs_group) {
		sysfs_remove_group(&kbdev->dev->kobj, &kbase_attr_group);
		kbdev->inited_subsys &= ~inited_sysfs_group;
	}

	if (kbdev->inited_subsys & inited_get_device) {
		put_device(kbdev->dev);
		kbdev->inited_subsys &= ~inited_get_device;
	}

	if (kbdev->inited_subsys & inited_debugfs) {
		kbase_device_debugfs_term(kbdev);
		kbdev->inited_subsys &= ~inited_debugfs;
	}

	if (kbdev->inited_subsys & inited_job_fault) {
		kbase_debug_job_fault_dev_term(kbdev);
		kbdev->inited_subsys &= ~inited_job_fault;
	}
	if (kbdev->inited_subsys & inited_vinstr) {
		kbase_vinstr_term(kbdev->vinstr_ctx);
		kbdev->inited_subsys &= ~inited_vinstr;
	}

#ifdef CONFIG_MALI_BIFROST_DEVFREQ
	if (kbdev->inited_subsys & inited_devfreq) {
		kbase_devfreq_term(kbdev);
		kbdev->inited_subsys &= ~inited_devfreq;
	}
#endif

	if (kbdev->inited_subsys & inited_backend_late) {
		kbase_backend_late_term(kbdev);
		kbdev->inited_subsys &= ~inited_backend_late;
	}

	if (kbdev->inited_subsys & inited_tlstream) {
		kbase_tlstream_term();
		kbdev->inited_subsys &= ~inited_tlstream;
	}

	/* Bring job and mem sys to a halt before we continue termination */

	if (kbdev->inited_subsys & inited_js)
		kbasep_js_devdata_halt(kbdev);

	if (kbdev->inited_subsys & inited_mem)
		kbase_mem_halt(kbdev);

	if (kbdev->inited_subsys & inited_protected) {
		kbasep_protected_mode_term(kbdev);
		kbdev->inited_subsys &= ~inited_protected;
	}

	if (kbdev->inited_subsys & inited_js) {
		kbasep_js_devdata_term(kbdev);
		kbdev->inited_subsys &= ~inited_js;
	}

	if (kbdev->inited_subsys & inited_mem) {
		kbase_mem_term(kbdev);
		kbdev->inited_subsys &= ~inited_mem;
	}

	if (kbdev->inited_subsys & inited_pm_runtime_init) {
		kbdev->pm.callback_power_runtime_term(kbdev);
		kbdev->inited_subsys &= ~inited_pm_runtime_init;
	}

	if (kbdev->inited_subsys & inited_ctx_sched) {
		kbase_ctx_sched_term(kbdev);
		kbdev->inited_subsys &= ~inited_ctx_sched;
	}

	if (kbdev->inited_subsys & inited_device) {
		kbase_device_term(kbdev);
		kbdev->inited_subsys &= ~inited_device;
	}

	if (kbdev->inited_subsys & inited_backend_early) {
		kbase_backend_early_term(kbdev);
		kbdev->inited_subsys &= ~inited_backend_early;
	}

	if (kbdev->inited_subsys & inited_io_history) {
		kbase_io_history_term(&kbdev->io_history);
		kbdev->inited_subsys &= ~inited_io_history;
	}

	if (kbdev->inited_subsys & inited_power_control) {
		power_control_term(kbdev);
		kbdev->inited_subsys &= ~inited_power_control;
	}

	if (kbdev->inited_subsys & inited_registers_map) {
		registers_unmap(kbdev);
		kbdev->inited_subsys &= ~inited_registers_map;
	}

#ifdef CONFIG_MALI_BIFROST_NO_MALI
	if (kbdev->inited_subsys & inited_gpu_device) {
		gpu_device_destroy(kbdev);
		kbdev->inited_subsys &= ~inited_gpu_device;
	}
#endif /* CONFIG_MALI_BIFROST_NO_MALI */

	if (kbdev->inited_subsys != 0)
		dev_err(kbdev->dev, "Missing sub system termination\n");

	kbase_device_free(kbdev);

	return 0;
}


/* Number of register accesses for the buffer that we allocate during
 * initialization time. The buffer size can be changed later via debugfs. */
#define KBASEP_DEFAULT_REGISTER_HISTORY_SIZE ((u16)512)

static int kbase_platform_device_probe(struct platform_device *pdev)
{
	struct kbase_device *kbdev;
	struct mali_base_gpu_core_props *core_props;
	u32 gpu_id;
	unsigned prod_id;
	const struct list_head *dev_list;
	int err = 0;

#ifdef CONFIG_OF
	err = kbase_platform_early_init();
	if (err) {
		dev_err(&pdev->dev, "Early platform initialization failed\n");
		kbase_platform_device_remove(pdev);
		return err;
	}
#endif
	kbdev = kbase_device_alloc();
	if (!kbdev) {
		dev_err(&pdev->dev, "Allocate device failed\n");
		kbase_platform_device_remove(pdev);
		return -ENOMEM;
	}

	kbdev->dev = &pdev->dev;
	dev_set_drvdata(kbdev->dev, kbdev);

#ifdef CONFIG_MALI_BIFROST_NO_MALI
	err = gpu_device_create(kbdev);
	if (err) {
		dev_err(&pdev->dev, "Dummy model initialization failed\n");
		kbase_platform_device_remove(pdev);
		return err;
	}
	kbdev->inited_subsys |= inited_gpu_device;
#endif /* CONFIG_MALI_BIFROST_NO_MALI */

	err = assign_irqs(pdev);
	if (err) {
		dev_err(&pdev->dev, "IRQ search failed\n");
		kbase_platform_device_remove(pdev);
		return err;
	}

	err = registers_map(kbdev);
	if (err) {
		dev_err(&pdev->dev, "Register map failed\n");
		kbase_platform_device_remove(pdev);
		return err;
	}
	kbdev->inited_subsys |= inited_registers_map;

	err = power_control_init(pdev);
	if (err) {
		dev_err(&pdev->dev, "Power control initialization failed\n");
		kbase_platform_device_remove(pdev);
		return err;
	}
	kbdev->inited_subsys |= inited_power_control;

	err = kbase_io_history_init(&kbdev->io_history,
			KBASEP_DEFAULT_REGISTER_HISTORY_SIZE);
	if (err) {
		dev_err(&pdev->dev, "Register access history initialization failed\n");
		kbase_platform_device_remove(pdev);
		return -ENOMEM;
	}
	kbdev->inited_subsys |= inited_io_history;

	err = kbase_backend_early_init(kbdev);
	if (err) {
		dev_err(kbdev->dev, "Early backend initialization failed\n");
		kbase_platform_device_remove(pdev);
		return err;
	}
	kbdev->inited_subsys |= inited_backend_early;

	scnprintf(kbdev->devname, DEVNAME_SIZE, "%s%d", kbase_drv_name,
			kbase_dev_nr);

	kbase_disjoint_init(kbdev);

	/* obtain min/max configured gpu frequencies */
	core_props = &(kbdev->gpu_props.props.core_props);
	core_props->gpu_freq_khz_min = GPU_FREQ_KHZ_MIN;
	core_props->gpu_freq_khz_max = GPU_FREQ_KHZ_MAX;

	err = kbase_device_init(kbdev);
	if (err) {
		dev_err(kbdev->dev, "Device initialization failed (%d)\n", err);
		kbase_platform_device_remove(pdev);
		return err;
	}
	kbdev->inited_subsys |= inited_device;

	err = kbase_ctx_sched_init(kbdev);
	if (err) {
		dev_err(kbdev->dev, "Context scheduler initialization failed (%d)\n",
				err);
		kbase_platform_device_remove(pdev);
		return err;
	}
	kbdev->inited_subsys |= inited_ctx_sched;

	if (kbdev->pm.callback_power_runtime_init) {
		err = kbdev->pm.callback_power_runtime_init(kbdev);
		if (err) {
			dev_err(kbdev->dev,
				"Runtime PM initialization failed\n");
			kbase_platform_device_remove(pdev);
			return err;
		}
		kbdev->inited_subsys |= inited_pm_runtime_init;
	}

	err = kbase_mem_init(kbdev);
	if (err) {
		dev_err(kbdev->dev, "Memory subsystem initialization failed\n");
		kbase_platform_device_remove(pdev);
		return err;
	}
	kbdev->inited_subsys |= inited_mem;

	gpu_id = kbdev->gpu_props.props.raw_props.gpu_id;
	gpu_id &= GPU_ID_VERSION_PRODUCT_ID;
	prod_id = gpu_id >> GPU_ID_VERSION_PRODUCT_ID_SHIFT;

	kbase_device_coherency_init(kbdev, prod_id);

	err = kbasep_protected_mode_init(kbdev);
	if (err) {
		dev_err(kbdev->dev, "Protected mode subsystem initialization failed\n");
		kbase_platform_device_remove(pdev);
		return err;
	}
	kbdev->inited_subsys |= inited_protected;

	dev_list = kbase_dev_list_get();
	list_add(&kbdev->entry, &kbase_dev_list);
	kbase_dev_list_put(dev_list);
	kbdev->inited_subsys |= inited_dev_list;

	err = kbasep_js_devdata_init(kbdev);
	if (err) {
		dev_err(kbdev->dev, "Job JS devdata initialization failed\n");
		kbase_platform_device_remove(pdev);
		return err;
	}
	kbdev->inited_subsys |= inited_js;

	err = kbase_tlstream_init();
	if (err) {
		dev_err(kbdev->dev, "Timeline stream initialization failed\n");
		kbase_platform_device_remove(pdev);
		return err;
	}
	kbdev->inited_subsys |= inited_tlstream;

	err = kbase_backend_late_init(kbdev);
	if (err) {
		dev_err(kbdev->dev, "Late backend initialization failed\n");
		kbase_platform_device_remove(pdev);
		return err;
	}
	kbdev->inited_subsys |= inited_backend_late;

	/* Initialize the kctx list. This is used by vinstr. */
	mutex_init(&kbdev->kctx_list_lock);
	INIT_LIST_HEAD(&kbdev->kctx_list);

	kbdev->vinstr_ctx = kbase_vinstr_init(kbdev);
	if (!kbdev->vinstr_ctx) {
		dev_err(kbdev->dev,
			"Virtual instrumentation initialization failed\n");
		kbase_platform_device_remove(pdev);
		return -EINVAL;
	}
	kbdev->inited_subsys |= inited_vinstr;

#ifdef CONFIG_MALI_BIFROST_DEVFREQ
	/* Devfreq uses vinstr, so must be initialized after it. */
	err = kbase_devfreq_init(kbdev);
	if (!err)
		kbdev->inited_subsys |= inited_devfreq;
	else
		dev_err(kbdev->dev, "Continuing without devfreq\n");
#endif /* CONFIG_MALI_BIFROST_DEVFREQ */

	err = kbase_debug_job_fault_dev_init(kbdev);
	if (err) {
		dev_err(kbdev->dev, "Job fault debug initialization failed\n");
		kbase_platform_device_remove(pdev);
		return err;
	}
	kbdev->inited_subsys |= inited_job_fault;

	err = kbase_device_debugfs_init(kbdev);
	if (err) {
		dev_err(kbdev->dev, "DebugFS initialization failed");
		kbase_platform_device_remove(pdev);
		return err;
	}
	kbdev->inited_subsys |= inited_debugfs;

	kbdev->mdev.minor = MISC_DYNAMIC_MINOR;
	kbdev->mdev.name = kbdev->devname;
	kbdev->mdev.fops = &kbase_fops;
	kbdev->mdev.parent = get_device(kbdev->dev);
	kbdev->inited_subsys |= inited_get_device;

	/* This needs to happen before registering the device with misc_register(),
	 * otherwise it causes a race condition between registering the device and a
	 * uevent event being generated for userspace, causing udev rules to run
	 * which might expect certain sysfs attributes present. As a result of the
	 * race condition we avoid, some Mali sysfs entries may have appeared to
	 * udev to not exist.

	 * For more information, see
	 * https://www.kernel.org/doc/Documentation/driver-model/device.txt, the
	 * paragraph that starts with "Word of warning", currently the second-last
	 * paragraph.
	 */
	err = sysfs_create_group(&kbdev->dev->kobj, &kbase_attr_group);
	if (err) {
		dev_err(&pdev->dev, "SysFS group creation failed\n");
		kbase_platform_device_remove(pdev);
		return err;
	}
	kbdev->inited_subsys |= inited_sysfs_group;

	err = misc_register(&kbdev->mdev);
	if (err) {
		dev_err(kbdev->dev, "Misc device registration failed for %s\n",
			kbdev->devname);
		kbase_platform_device_remove(pdev);
		return err;
	}
	kbdev->inited_subsys |= inited_misc_register;


#ifdef CONFIG_MALI_FPGA_BUS_LOGGER
	err = bl_core_client_register(kbdev->devname,
						kbase_logging_started_cb,
						kbdev, &kbdev->buslogger,
						THIS_MODULE, NULL);
	if (err == 0) {
		kbdev->inited_subsys |= inited_buslogger;
		bl_core_set_threshold(kbdev->buslogger, 1024*1024*1024);
	} else {
		dev_warn(kbdev->dev, "Bus log client registration failed\n");
		err = 0;
	}
#endif

	err = kbase_gpuprops_populate_user_buffer(kbdev);
	if (err) {
		dev_err(&pdev->dev, "GPU property population failed");
		kbase_platform_device_remove(pdev);
		return err;
	}

	dev_info(kbdev->dev,
			"Probed as %s\n", dev_name(kbdev->mdev.this_device));

	kbase_dev_nr++;

	return err;
}

#undef KBASEP_DEFAULT_REGISTER_HISTORY_SIZE

/**
 * kbase_device_suspend - Suspend callback from the OS.
 *
 * This is called by Linux when the device should suspend.
 *
 * @dev:  The device to suspend
 *
 * Return: A standard Linux error code
 */
static int kbase_device_suspend(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

#if defined(CONFIG_MALI_BIFROST_DEVFREQ) && \
		(LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	if (kbdev->inited_subsys & inited_devfreq)
		devfreq_suspend_device(kbdev->devfreq);
#endif

	kbase_pm_suspend(kbdev);
	return 0;
}

/**
 * kbase_device_resume - Resume callback from the OS.
 *
 * This is called by Linux when the device should resume from suspension.
 *
 * @dev:  The device to resume
 *
 * Return: A standard Linux error code
 */
static int kbase_device_resume(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	kbase_pm_resume(kbdev);

#if defined(CONFIG_MALI_BIFROST_DEVFREQ) && \
		(LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	if (kbdev->inited_subsys & inited_devfreq)
		devfreq_resume_device(kbdev->devfreq);
#endif
	return 0;
}

/**
 * kbase_device_runtime_suspend - Runtime suspend callback from the OS.
 *
 * This is called by Linux when the device should prepare for a condition in
 * which it will not be able to communicate with the CPU(s) and RAM due to
 * power management.
 *
 * @dev:  The device to suspend
 *
 * Return: A standard Linux error code
 */
#ifdef KBASE_PM_RUNTIME
static int kbase_device_runtime_suspend(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

#if defined(CONFIG_MALI_BIFROST_DEVFREQ) && \
		(LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	if (kbdev->inited_subsys & inited_devfreq)
		devfreq_suspend_device(kbdev->devfreq);
#endif

	if (kbdev->pm.backend.callback_power_runtime_off) {
		kbdev->pm.backend.callback_power_runtime_off(kbdev);
		dev_dbg(dev, "runtime suspend\n");
	}
	return 0;
}
#endif /* KBASE_PM_RUNTIME */

/**
 * kbase_device_runtime_resume - Runtime resume callback from the OS.
 *
 * This is called by Linux when the device should go into a fully active state.
 *
 * @dev:  The device to suspend
 *
 * Return: A standard Linux error code
 */

#ifdef KBASE_PM_RUNTIME
static int kbase_device_runtime_resume(struct device *dev)
{
	int ret = 0;
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	if (kbdev->pm.backend.callback_power_runtime_on) {
		ret = kbdev->pm.backend.callback_power_runtime_on(kbdev);
		dev_dbg(dev, "runtime resume\n");
	}

#if defined(CONFIG_MALI_BIFROST_DEVFREQ) && \
		(LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	if (kbdev->inited_subsys & inited_devfreq)
		devfreq_resume_device(kbdev->devfreq);
#endif

	return ret;
}
#endif /* KBASE_PM_RUNTIME */


#ifdef KBASE_PM_RUNTIME
/**
 * kbase_device_runtime_idle - Runtime idle callback from the OS.
 * @dev: The device to suspend
 *
 * This is called by Linux when the device appears to be inactive and it might
 * be placed into a low power state.
 *
 * Return: 0 if device can be suspended, non-zero to avoid runtime autosuspend,
 * otherwise a standard Linux error code
 */
static int kbase_device_runtime_idle(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	/* Use platform specific implementation if it exists. */
	if (kbdev->pm.backend.callback_power_runtime_idle)
		return kbdev->pm.backend.callback_power_runtime_idle(kbdev);

	return 0;
}
#endif /* KBASE_PM_RUNTIME */

/* The power management operations for the platform driver.
 */
static const struct dev_pm_ops kbase_pm_ops = {
	.suspend = kbase_device_suspend,
	.resume = kbase_device_resume,
#ifdef KBASE_PM_RUNTIME
	.runtime_suspend = kbase_device_runtime_suspend,
	.runtime_resume = kbase_device_runtime_resume,
	.runtime_idle = kbase_device_runtime_idle,
#endif /* KBASE_PM_RUNTIME */
};

#ifdef CONFIG_OF
static const struct of_device_id kbase_dt_ids[] = {
	{ .compatible = "arm,malit6xx" },
	{ .compatible = "arm,mali-midgard" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, kbase_dt_ids);
#endif

static struct platform_driver kbase_platform_driver = {
	.probe = kbase_platform_device_probe,
	.remove = kbase_platform_device_remove,
	.driver = {
		   .name = kbase_drv_name,
		   .owner = THIS_MODULE,
		   .pm = &kbase_pm_ops,
		   .of_match_table = of_match_ptr(kbase_dt_ids),
	},
};

/*
 * The driver will not provide a shortcut to create the Mali platform device
 * anymore when using Device Tree.
 */
#ifdef CONFIG_OF
module_platform_driver(kbase_platform_driver);
#else

static int __init kbase_driver_init(void)
{
	int ret;

	ret = kbase_platform_early_init();
	if (ret)
		return ret;

	ret = kbase_platform_register();
	if (ret)
		return ret;

	ret = platform_driver_register(&kbase_platform_driver);

	if (ret)
		kbase_platform_unregister();

	return ret;
}

static void __exit kbase_driver_exit(void)
{
	platform_driver_unregister(&kbase_platform_driver);
	kbase_platform_unregister();
}

module_init(kbase_driver_init);
module_exit(kbase_driver_exit);

#endif /* CONFIG_OF */

MODULE_LICENSE("GPL");
MODULE_VERSION(MALI_RELEASE_NAME " (UK version " \
		__stringify(BASE_UK_VERSION_MAJOR) "." \
		__stringify(BASE_UK_VERSION_MINOR) ")");

#if defined(CONFIG_MALI_BIFROST_GATOR_SUPPORT) || defined(CONFIG_MALI_BIFROST_SYSTEM_TRACE)
#define CREATE_TRACE_POINTS
#endif

#ifdef CONFIG_MALI_BIFROST_GATOR_SUPPORT
/* Create the trace points (otherwise we just get code to call a tracepoint) */
#include "mali_linux_trace.h"

EXPORT_TRACEPOINT_SYMBOL_GPL(mali_job_slots_event);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_pm_status);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_pm_power_on);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_pm_power_off);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_page_fault_insert_pages);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_mmu_as_in_use);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_mmu_as_released);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_total_alloc_pages_change);

void kbase_trace_mali_pm_status(u32 event, u64 value)
{
	trace_mali_pm_status(event, value);
}

void kbase_trace_mali_pm_power_off(u32 event, u64 value)
{
	trace_mali_pm_power_off(event, value);
}

void kbase_trace_mali_pm_power_on(u32 event, u64 value)
{
	trace_mali_pm_power_on(event, value);
}

void kbase_trace_mali_job_slots_event(u32 event, const struct kbase_context *kctx, u8 atom_id)
{
	trace_mali_job_slots_event(event, (kctx != NULL ? kctx->tgid : 0), (kctx != NULL ? kctx->pid : 0), atom_id);
}

void kbase_trace_mali_page_fault_insert_pages(int event, u32 value)
{
	trace_mali_page_fault_insert_pages(event, value);
}

void kbase_trace_mali_mmu_as_in_use(int event)
{
	trace_mali_mmu_as_in_use(event);
}

void kbase_trace_mali_mmu_as_released(int event)
{
	trace_mali_mmu_as_released(event);
}

void kbase_trace_mali_total_alloc_pages_change(long long int event)
{
	trace_mali_total_alloc_pages_change(event);
}
#endif /* CONFIG_MALI_BIFROST_GATOR_SUPPORT */
#ifdef CONFIG_MALI_BIFROST_SYSTEM_TRACE
#include "mali_linux_kbase_trace.h"
#endif
