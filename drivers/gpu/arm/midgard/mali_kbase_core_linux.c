
/*
 *
 * (C) COPYRIGHT ARM Limited. All rights reserved.
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




/**
 * @file mali_kbase_core_linux.c
 * Base kernel driver init.
 */

#include <mali_kbase.h>
#include <mali_kbase_config_defaults.h>
#include <mali_kbase_uku.h>
#include <mali_midg_regmap.h>
#include <mali_kbase_gator.h>
#include <mali_kbase_mem_linux.h>
#ifdef CONFIG_MALI_DEVFREQ
#include "mali_kbase_devfreq.h"
#endif /* CONFIG_MALI_DEVFREQ */
#include <mali_kbase_cpuprops.h>
#ifdef CONFIG_MALI_NO_MALI
#include "mali_kbase_model_linux.h"
#endif /* CONFIG_MALI_NO_MALI */
#include "mali_kbase_mem_profile_debugfs_buf_size.h"

#ifdef CONFIG_KDS
#include <linux/kds.h>
#include <linux/anon_inodes.h>
#include <linux/syscalls.h>
#endif /* CONFIG_KDS */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/semaphore.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mm.h>
#include <linux/compat.h>	/* is_compat_task */
#include <linux/version.h>
#include <mali_kbase_hw.h>
#include <platform/mali_kbase_platform_common.h>
#ifdef CONFIG_SYNC
#include <mali_kbase_sync.h>
#endif /* CONFIG_SYNC */
#ifdef CONFIG_PM_DEVFREQ
#include <linux/devfreq.h>
#endif /* CONFIG_PM_DEVFREQ */
#include <linux/clk.h>

/*
 * This file is included since when we support device tree we don't
 * use the platform fake code for registering the kbase config attributes.
 */
#ifdef CONFIG_OF
#include <mali_kbase_config.h>
#endif

#ifdef CONFIG_MACH_MANTA
#include <plat/devs.h>
#endif

/* GPU IRQ Tags */
#define	JOB_IRQ_TAG	0
#define MMU_IRQ_TAG	1
#define GPU_IRQ_TAG	2


struct kbase_irq_table {
	u32 tag;
	irq_handler_t handler;
};
#if MALI_UNIT_TEST
static struct kbase_exported_test_data shared_kernel_test_data;
EXPORT_SYMBOL(shared_kernel_test_data);
#endif /* MALI_UNIT_TEST */

#define KBASE_DRV_NAME "mali"
/** rk_ext : version of rk_ext on mali_ko, aka. rk_ko_ver. */
#define ROCKCHIP_VERSION    (12)

static const char kbase_drv_name[] = KBASE_DRV_NAME;

static int kbase_dev_nr;

static DEFINE_SEMAPHORE(kbase_dev_list_lock);
static LIST_HEAD(kbase_dev_list);

KBASE_EXPORT_TEST_API(kbase_dev_list_lock)
KBASE_EXPORT_TEST_API(kbase_dev_list)
#define KERNEL_SIDE_DDK_VERSION_STRING "K:" MALI_RELEASE_NAME "(GPL)"
static INLINE void __compile_time_asserts(void)
{
	CSTD_COMPILE_TIME_ASSERT(sizeof(KERNEL_SIDE_DDK_VERSION_STRING) <= KBASE_GET_VERSION_BUFFER_SIZE);
}

#ifdef CONFIG_KDS

struct kbasep_kds_resource_set_file_data {
	struct kds_resource_set *lock;
};

static int kds_resource_release(struct inode *inode, struct file *file);

static const struct file_operations kds_resource_fops = {
	.release = kds_resource_release
};

struct kbase_kds_resource_list_data {
	struct kds_resource **kds_resources;
	unsigned long *kds_access_bitmap;
	int num_elems;
};

static int kds_resource_release(struct inode *inode, struct file *file)
{
	struct kbasep_kds_resource_set_file_data *data;

	data = (struct kbasep_kds_resource_set_file_data *)file->private_data;
	if (NULL != data) {
		if (NULL != data->lock)
			kds_resource_set_release(&data->lock);

		kfree(data);
	}
	return 0;
}

static mali_error kbasep_kds_allocate_resource_list_data(struct kbase_context *kctx, struct base_external_resource *ext_res, int num_elems, struct kbase_kds_resource_list_data *resources_list)
{
	struct base_external_resource *res = ext_res;
	int res_id;

	/* assume we have to wait for all */

	KBASE_DEBUG_ASSERT(0 != num_elems);
	resources_list->kds_resources = kmalloc_array(num_elems,
			sizeof(struct kds_resource *), GFP_KERNEL);

	if (NULL == resources_list->kds_resources)
		return MALI_ERROR_OUT_OF_MEMORY;

	KBASE_DEBUG_ASSERT(0 != num_elems);
	resources_list->kds_access_bitmap = kzalloc(
			sizeof(unsigned long) *
			((num_elems + BITS_PER_LONG - 1) / BITS_PER_LONG),
			GFP_KERNEL);

	if (NULL == resources_list->kds_access_bitmap) {
		kfree(resources_list->kds_access_bitmap);
		return MALI_ERROR_OUT_OF_MEMORY;
	}

	kbase_gpu_vm_lock(kctx);
	for (res_id = 0; res_id < num_elems; res_id++, res++) {
		int exclusive;
		struct kbase_va_region *reg;
		struct kds_resource *kds_res = NULL;

		exclusive = res->ext_resource & BASE_EXT_RES_ACCESS_EXCLUSIVE;
		reg = kbase_region_tracker_find_region_enclosing_address(kctx, res->ext_resource & ~BASE_EXT_RES_ACCESS_EXCLUSIVE);

		/* did we find a matching region object? */
		if (NULL == reg || (reg->flags & KBASE_REG_FREE))
			break;

		/* no need to check reg->alloc as only regions with an alloc has
		 * a size, and kbase_region_tracker_find_region_enclosing_address
		 * only returns regions with size > 0 */
		switch (reg->alloc->type) {
#if defined(CONFIG_UMP) && defined(CONFIG_KDS)
		case KBASE_MEM_TYPE_IMPORTED_UMP:
			kds_res = ump_dd_kds_resource_get(reg->alloc->imported.ump_handle);
			break;
#endif /* defined(CONFIG_UMP) && defined(CONFIG_KDS) */
		default:
			break;
		}

		/* no kds resource for the region ? */
		if (!kds_res)
			break;

		resources_list->kds_resources[res_id] = kds_res;

		if (exclusive)
			set_bit(res_id, resources_list->kds_access_bitmap);
	}
	kbase_gpu_vm_unlock(kctx);

	/* did the loop run to completion? */
	if (res_id == num_elems)
		return MALI_ERROR_NONE;

	/* Clean up as the resource list is not valid. */
	kfree(resources_list->kds_resources);
	kfree(resources_list->kds_access_bitmap);

	return MALI_ERROR_FUNCTION_FAILED;
}

static mali_bool kbasep_validate_kbase_pointer(
		struct kbase_context *kctx, union kbase_pointer *p)
{
	if (kctx->is_compat) {
		if (p->compat_value == 0)
			return MALI_FALSE;
	} else {
		if (NULL == p->value)
			return MALI_FALSE;
	}
	return MALI_TRUE;
}

static mali_error kbase_external_buffer_lock(struct kbase_context *kctx, struct kbase_uk_ext_buff_kds_data *args, u32 args_size)
{
	struct base_external_resource *ext_res_copy;
	size_t ext_resource_size;
	mali_error return_error = MALI_ERROR_FUNCTION_FAILED;
	int fd;

	if (args_size != sizeof(struct kbase_uk_ext_buff_kds_data))
		return MALI_ERROR_FUNCTION_FAILED;

	/* Check user space has provided valid data */
	if (!kbasep_validate_kbase_pointer(kctx, &args->external_resource) ||
			!kbasep_validate_kbase_pointer(kctx, &args->file_descriptor) ||
			(0 == args->num_res) ||
			(args->num_res > KBASE_MAXIMUM_EXT_RESOURCES))
		return MALI_ERROR_FUNCTION_FAILED;

	ext_resource_size = sizeof(struct base_external_resource) * args->num_res;

	KBASE_DEBUG_ASSERT(0 != ext_resource_size);
	ext_res_copy = kmalloc(ext_resource_size, GFP_KERNEL);

	if (NULL != ext_res_copy) {
		struct base_external_resource __user *ext_res_user;
		int __user *file_descriptor_user;
#ifdef CONFIG_COMPAT
		if (kctx->is_compat) {
			ext_res_user = compat_ptr(args->external_resource.compat_value);
			file_descriptor_user = compat_ptr(args->file_descriptor.compat_value);
		} else {
#endif /* CONFIG_COMPAT */
			ext_res_user = args->external_resource.value;
			file_descriptor_user = args->file_descriptor.value;
#ifdef CONFIG_COMPAT
		}
#endif /* CONFIG_COMPAT */

		/* Copy the external resources to lock from user space */
		if (0 == copy_from_user(ext_res_copy, ext_res_user, ext_resource_size)) {
			struct kbasep_kds_resource_set_file_data *fdata;

			/* Allocate data to be stored in the file */
			fdata = kmalloc(sizeof(*fdata), GFP_KERNEL);

			if (NULL != fdata) {
				struct kbase_kds_resource_list_data resource_list_data;
				/* Parse given elements and create resource and access lists */
				return_error = kbasep_kds_allocate_resource_list_data(kctx, ext_res_copy, args->num_res, &resource_list_data);
				if (MALI_ERROR_NONE == return_error) {
					long err;

					fdata->lock = NULL;

					fd = anon_inode_getfd("kds_ext", &kds_resource_fops, fdata, 0);

					err = copy_to_user(file_descriptor_user, &fd, sizeof(fd));

					/* If the file descriptor was valid and we successfully copied it to user space, then we
					 * can try and lock the requested kds resources.
					 */
					if ((fd >= 0) && (0 == err)) {
						struct kds_resource_set *lock;

						lock = kds_waitall(args->num_res, resource_list_data.kds_access_bitmap,
								resource_list_data.kds_resources,
								KDS_WAIT_BLOCKING);

						if (IS_ERR_OR_NULL(lock)) {
							return_error = MALI_ERROR_FUNCTION_FAILED;
						} else {
							return_error = MALI_ERROR_NONE;
							fdata->lock = lock;
						}
					} else {
						return_error = MALI_ERROR_FUNCTION_FAILED;
					}

					kfree(resource_list_data.kds_resources);
					kfree(resource_list_data.kds_access_bitmap);
				}

				if (MALI_ERROR_NONE != return_error) {
					/* If the file was opened successfully then close it which will clean up
					 * the file data, otherwise we clean up the file data ourself. */
					if (fd >= 0)
						sys_close(fd);
					else
						kfree(fdata);
				}
			} else {
				return_error = MALI_ERROR_OUT_OF_MEMORY;
			}
		}
		kfree(ext_res_copy);
	}
	return return_error;
}
#endif /* CONFIG_KDS */

static mali_error kbase_dispatch(struct kbase_context *kctx, void * const args, u32 args_size)
{
	struct kbase_device *kbdev;
	union uk_header *ukh = args;
	u32 id;

	KBASE_DEBUG_ASSERT(ukh != NULL);

	kbdev = kctx->kbdev;
	id = ukh->id;
	ukh->ret = MALI_ERROR_NONE;	/* Be optimistic */

	if (UKP_FUNC_ID_CHECK_VERSION == id) {
		if (args_size == sizeof(struct uku_version_check_args)) {
			struct uku_version_check_args *version_check = (struct uku_version_check_args *)args;

			switch (version_check->major) {
#ifdef BASE_LEGACY_UK6_SUPPORT
			case 6:
				/* We are backwards compatible with version 6,
				 * so pretend to be the old version */
				version_check->major = 6;
				version_check->minor = 1;
				break;
#endif /* BASE_LEGACY_UK6_SUPPORT */
#ifdef BASE_LEGACY_UK7_SUPPORT
			case 7:
				/* We are backwards compatible with version 7,
				 * so pretend to be the old version */
				version_check->major = 7;
				version_check->minor = 1;
				break;
#endif /* BASE_LEGACY_UK7_SUPPORT */
			default:
				/* We return our actual version regardless if it
				 * matches the version returned by userspace -
				 * userspace can bail if it can't handle this
				 * version */
				version_check->major = BASE_UK_VERSION_MAJOR;
				version_check->minor = BASE_UK_VERSION_MINOR;
			}

			ukh->ret = MALI_ERROR_NONE;
		} else {
			ukh->ret = MALI_ERROR_FUNCTION_FAILED;
		}
		return MALI_ERROR_NONE;
	}


	if (!atomic_read(&kctx->setup_complete)) {
		/* setup pending, try to signal that we'll do the setup */
		if (atomic_cmpxchg(&kctx->setup_in_progress, 0, 1)) {
			/* setup was already in progress, err this call */
			return MALI_ERROR_FUNCTION_FAILED;
		}

		/* we're the one doing setup */

		/* is it the only call we accept? */
		if (id == KBASE_FUNC_SET_FLAGS) {
			struct kbase_uk_set_flags *kbase_set_flags = (struct kbase_uk_set_flags *)args;

			if (sizeof(*kbase_set_flags) != args_size) {
				/* not matching the expected call, stay stuck in setup mode */
				goto bad_size;
			}

			if (MALI_ERROR_NONE != kbase_context_set_create_flags(kctx, kbase_set_flags->create_flags)) {
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				/* bad flags, will stay stuck in setup mode */
				return MALI_ERROR_NONE;
			} else {
				/* we've done the setup, all OK */
				atomic_set(&kctx->setup_complete, 1);
				return MALI_ERROR_NONE;
			}
		} else {
			/* unexpected call, will stay stuck in setup mode */
			return MALI_ERROR_FUNCTION_FAILED;
		}
	}

	/* setup complete, perform normal operation */
	switch (id) {
	case KBASE_FUNC_MEM_ALLOC:
		{
			struct kbase_uk_mem_alloc *mem = args;
			struct kbase_va_region *reg;

			if (sizeof(*mem) != args_size)
				goto bad_size;

			reg = kbase_mem_alloc(kctx, mem->va_pages, mem->commit_pages, mem->extent, &mem->flags, &mem->gpu_va, &mem->va_alignment);
			if (!reg)
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}
	case KBASE_FUNC_MEM_IMPORT:
		{
			struct kbase_uk_mem_import *mem_import = args;
			int __user *phandle;
			int handle;

			if (sizeof(*mem_import) != args_size)
				goto bad_size;
#ifdef CONFIG_COMPAT
			if (kctx->is_compat)
				phandle = compat_ptr(mem_import->phandle.compat_value);
			else
#endif
				phandle = mem_import->phandle.value;

			switch (mem_import->type) {
			case BASE_MEM_IMPORT_TYPE_UMP:
				get_user(handle, phandle);
				break;
			case BASE_MEM_IMPORT_TYPE_UMM:
				get_user(handle, phandle);
				break;
			default:
				goto bad_type;
				break;
			}

			if (kbase_mem_import(kctx, mem_import->type, handle, &mem_import->gpu_va, &mem_import->va_pages, &mem_import->flags)) {
bad_type:
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

#ifdef CONFIG_COMPAT
			if (kctx->is_compat)
				user_ai = compat_ptr(alias->ai.compat_value);
			else
#endif
				user_ai = alias->ai.value;

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

			if (sizeof(*commit) != args_size)
				goto bad_size;

			if (commit->gpu_addr & ~PAGE_MASK) {
				dev_warn(kbdev->dev, "kbase_dispatch case KBASE_FUNC_MEM_COMMIT: commit->gpu_addr: passed parameter is invalid");
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				break;
			}

			if (kbase_mem_commit(kctx, commit->gpu_addr,
					commit->pages,
					(base_backing_threshold_status *)&commit->result_subcode))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;

			break;
		}

	case KBASE_FUNC_MEM_QUERY:
		{
			struct kbase_uk_mem_query *query = args;

			if (sizeof(*query) != args_size)
				goto bad_size;

			if (query->gpu_addr & ~PAGE_MASK) {
				dev_warn(kbdev->dev, "kbase_dispatch case KBASE_FUNC_MEM_QUERY: query->gpu_addr: passed parameter is invalid");
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				break;
			}
			if (query->query != KBASE_MEM_QUERY_COMMIT_SIZE &&
			    query->query != KBASE_MEM_QUERY_VA_SIZE &&
				query->query != KBASE_MEM_QUERY_FLAGS) {
				dev_warn(kbdev->dev, "kbase_dispatch case KBASE_FUNC_MEM_QUERY: query->query = %lld unknown", (unsigned long long)query->query);
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				break;
			}

			ukh->ret = kbase_mem_query(kctx, query->gpu_addr, query->query, &query->value);
			break;
		}
		break;

	case KBASE_FUNC_MEM_FLAGS_CHANGE:
		{
			struct kbase_uk_mem_flags_change *fc = args;

			if (sizeof(*fc) != args_size)
				goto bad_size;

			if ((fc->gpu_va & ~PAGE_MASK) && (fc->gpu_va >= PAGE_SIZE)) {
				dev_warn(kbdev->dev, "kbase_dispatch case KBASE_FUNC_MEM_FLAGS_CHANGE: mem->gpu_va: passed parameter is invalid");
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				break;
			}

			if (kbase_mem_flags_change(kctx, fc->gpu_va, fc->flags, fc->mask))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;

			break;
		}
	case KBASE_FUNC_MEM_FREE:
		{
			struct kbase_uk_mem_free *mem = args;

			if (sizeof(*mem) != args_size)
				goto bad_size;

			if ((mem->gpu_addr & ~PAGE_MASK) && (mem->gpu_addr >= PAGE_SIZE)) {
				dev_warn(kbdev->dev, "kbase_dispatch case KBASE_FUNC_MEM_FREE: mem->gpu_addr: passed parameter is invalid");
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				break;
			}

			if (kbase_mem_free(kctx, mem->gpu_addr))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}

	case KBASE_FUNC_JOB_SUBMIT:
		{
			struct kbase_uk_job_submit *job = args;

			if (sizeof(*job) != args_size)
				goto bad_size;

#ifdef BASE_LEGACY_UK6_SUPPORT
			if (MALI_ERROR_NONE != kbase_jd_submit(kctx, job, 0))
#else
			if (MALI_ERROR_NONE != kbase_jd_submit(kctx, job))
#endif /* BASE_LEGACY_UK6_SUPPORT */
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}

#ifdef BASE_LEGACY_UK6_SUPPORT
	case KBASE_FUNC_JOB_SUBMIT_UK6:
		{
			struct kbase_uk_job_submit *job = args;

			if (sizeof(*job) != args_size)
				goto bad_size;

			if (MALI_ERROR_NONE != kbase_jd_submit(kctx, job, 1))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}
#endif

	case KBASE_FUNC_SYNC:
		{
			struct kbase_uk_sync_now *sn = args;

			if (sizeof(*sn) != args_size)
				goto bad_size;

			if (sn->sset.basep_sset.mem_handle & ~PAGE_MASK) {
				dev_warn(kbdev->dev, "kbase_dispatch case KBASE_FUNC_SYNC: sn->sset.basep_sset.mem_handle: passed parameter is invalid");
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				break;
			}

			if (MALI_ERROR_NONE != kbase_sync_now(kctx, &sn->sset))
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

			if (MALI_ERROR_NONE != kbase_instr_hwcnt_setup(kctx, setup))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}

	case KBASE_FUNC_HWCNT_DUMP:
		{
			/* args ignored */
			if (MALI_ERROR_NONE != kbase_instr_hwcnt_dump(kctx))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}

	case KBASE_FUNC_HWCNT_CLEAR:
		{
			/* args ignored */
			if (MALI_ERROR_NONE != kbase_instr_hwcnt_clear(kctx))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}

#ifdef BASE_LEGACY_UK7_SUPPORT
	case KBASE_FUNC_CPU_PROPS_REG_DUMP_OBSOLETE:
		{
			struct kbase_uk_cpuprops *setup = args;

			if (sizeof(*setup) != args_size)
				goto bad_size;

			if (MALI_ERROR_NONE != kbase_cpuprops_uk_get_props(kctx, setup))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}
#endif /* BASE_LEGACY_UK7_SUPPORT */

	case KBASE_FUNC_GPU_PROPS_REG_DUMP:
		{
			struct kbase_uk_gpuprops *setup = args;

			if (sizeof(*setup) != args_size)
				goto bad_size;

			if (MALI_ERROR_NONE != kbase_gpuprops_uk_get_props(kctx, setup))
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}
	case KBASE_FUNC_FIND_CPU_OFFSET:
		{
			struct kbase_uk_find_cpu_offset *find = args;

			if (sizeof(*find) != args_size)
				goto bad_size;

			if (find->gpu_addr & ~PAGE_MASK) {
				dev_warn(kbdev->dev, "kbase_dispatch case KBASE_FUNC_FIND_CPU_OFFSET: find->gpu_addr: passed parameter is invalid");
				goto out_bad;
			}

			if (find->size > SIZE_MAX || find->cpu_addr > ULONG_MAX) {
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			} else {
				mali_error err;

				err = kbasep_find_enclosing_cpu_mapping_offset(
						kctx,
						find->gpu_addr,
						(uintptr_t) find->cpu_addr,
						(size_t) find->size,
						&find->offset);

				if (err != MALI_ERROR_NONE)
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
			get_version->rk_version = ROCKCHIP_VERSION;
			break;
		}

	case KBASE_FUNC_STREAM_CREATE:
		{
#ifdef CONFIG_SYNC
			struct kbase_uk_stream_create *screate = (struct kbase_uk_stream_create *)args;

			if (sizeof(*screate) != args_size)
				goto bad_size;

			if (strnlen(screate->name, sizeof(screate->name)) >= sizeof(screate->name)) {
				/* not NULL terminated */
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				break;
			}

			ukh->ret = kbase_stream_create(screate->name, &screate->fd);
#else /* CONFIG_SYNC */
			ukh->ret = MALI_ERROR_FUNCTION_FAILED;
#endif /* CONFIG_SYNC */
			break;
		}
	case KBASE_FUNC_FENCE_VALIDATE:
		{
#ifdef CONFIG_SYNC
			struct kbase_uk_fence_validate *fence_validate = (struct kbase_uk_fence_validate *)args;

			if (sizeof(*fence_validate) != args_size)
				goto bad_size;

			ukh->ret = kbase_fence_validate(fence_validate->fd);
#endif /* CONFIG_SYNC */
			break;
		}

	case KBASE_FUNC_EXT_BUFFER_LOCK:
		{
#ifdef CONFIG_KDS
			ukh->ret = kbase_external_buffer_lock(kctx, (struct kbase_uk_ext_buff_kds_data *)args, args_size);
#endif /* CONFIG_KDS */
			break;
		}

	case KBASE_FUNC_SET_TEST_DATA:
		{
#if MALI_UNIT_TEST
			struct kbase_uk_set_test_data *set_data = args;

			shared_kernel_test_data = set_data->test_data;
			shared_kernel_test_data.kctx.value = (void __user *)kctx;
			shared_kernel_test_data.mm.value = (void __user *)current->mm;
			ukh->ret = MALI_ERROR_NONE;
#endif /* MALI_UNIT_TEST */
			break;
		}

	case KBASE_FUNC_INJECT_ERROR:
		{
#ifdef CONFIG_MALI_ERROR_INJECT
			unsigned long flags;
			struct kbase_error_params params = ((struct kbase_uk_error_params *)args)->params;

			/*mutex lock */
			spin_lock_irqsave(&kbdev->reg_op_lock, flags);
			ukh->ret = job_atom_inject_error(&params);
			spin_unlock_irqrestore(&kbdev->reg_op_lock, flags);
			/*mutex unlock */
#endif /* CONFIG_MALI_ERROR_INJECT */
			break;
		}

	case KBASE_FUNC_MODEL_CONTROL:
		{
#ifdef CONFIG_MALI_NO_MALI
			unsigned long flags;
			struct kbase_model_control_params params =
					((struct kbase_uk_model_control_params *)args)->params;

			/*mutex lock */
			spin_lock_irqsave(&kbdev->reg_op_lock, flags);
			ukh->ret = midg_model_control(kbdev->model, &params);
			spin_unlock_irqrestore(&kbdev->reg_op_lock, flags);
			/*mutex unlock */
#endif /* CONFIG_MALI_NO_MALI */
			break;
		}

	case KBASE_FUNC_KEEP_GPU_POWERED:
		{
			struct kbase_uk_keep_gpu_powered *kgp =
					(struct kbase_uk_keep_gpu_powered *)args;

			/* A suspend won't happen here, because we're in a syscall from a
			 * userspace thread.
			 *
			 * Nevertheless, we'd get the wrong pm_context_active/idle counting
			 * here if a suspend did happen, so let's assert it won't: */
			KBASE_DEBUG_ASSERT(!kbase_pm_is_suspending(kbdev));

			if (kgp->enabled && !kctx->keep_gpu_powered) {
				kbase_pm_context_active(kbdev);
				atomic_inc(&kbdev->keep_gpu_powered_count);
				kctx->keep_gpu_powered = MALI_TRUE;
			} else if (!kgp->enabled && kctx->keep_gpu_powered) {
				atomic_dec(&kbdev->keep_gpu_powered_count);
				kbase_pm_context_idle(kbdev);
				kctx->keep_gpu_powered = MALI_FALSE;
			}

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
				controls->profiling_controls[i] = kbase_get_profiling_control(kbdev, i);

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
				dev_err(kbdev->dev, "buffer too big");
				goto out_bad;
			}

#ifdef CONFIG_COMPAT
			if (kctx->is_compat)
				user_buf = compat_ptr(add_data->buf.compat_value);
			else
#endif
				user_buf = add_data->buf.value;

			buf = kmalloc(add_data->len, GFP_KERNEL);
			if (!buf)
				goto out_bad;

			if (0 != copy_from_user(buf, user_buf, add_data->len)) {
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
				kfree(buf);
				goto out_bad;
			}
			kbasep_mem_profile_debugfs_insert(kctx, buf,
					add_data->len);

			break;
		}

	default:
		dev_err(kbdev->dev, "unknown ioctl %u", id);
		goto out_bad;
	}

	return MALI_ERROR_NONE;

 bad_size:
	dev_err(kbdev->dev, "Wrong syscall size (%d) for %08x\n", args_size, id);
 out_bad:
	return MALI_ERROR_FUNCTION_FAILED;
}

static struct kbase_device *to_kbase_device(struct device *dev)
{
	return dev_get_drvdata(dev);
}

/*
 * API to acquire device list semaphore and
 * return pointer to the device list head
 */
const struct list_head *kbase_dev_list_get(void)
{
	down(&kbase_dev_list_lock);
	return &kbase_dev_list;
}

/* API to release the device list semaphore */
void kbase_dev_list_put(const struct list_head *dev_list)
{
	up(&kbase_dev_list_lock);
}

/* Find a particular kbase device (as specified by minor number), or find the "first" device if -1 is specified */
struct kbase_device *kbase_find_device(int minor)
{
	struct kbase_device *kbdev = NULL;
	struct list_head *entry;

	down(&kbase_dev_list_lock);
	list_for_each(entry, &kbase_dev_list) {
		struct kbase_device *tmp;

		tmp = list_entry(entry, struct kbase_device, entry);
		if (tmp->mdev.minor == minor || minor == -1) {
			kbdev = tmp;
			get_device(kbdev->dev);
			break;
		}
	}
	up(&kbase_dev_list_lock);

	return kbdev;
}
EXPORT_SYMBOL(kbase_find_device);

void kbase_release_device(struct kbase_device *kbdev)
{
	put_device(kbdev->dev);
}
EXPORT_SYMBOL(kbase_release_device);

static int kbase_open(struct inode *inode, struct file *filp)
{
	struct kbase_device *kbdev = NULL;
	struct kbase_context *kctx;
	int ret = 0;

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

	dev_dbg(kbdev->dev, "created base context\n");

	{
		struct kbasep_kctx_list_element *element;

		element = kzalloc(sizeof(*element), GFP_KERNEL);
		if (element) {
			mutex_lock(&kbdev->kctx_list_lock);
			element->kctx = kctx;
			list_add(&element->link, &kbdev->kctx_list);
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
	mali_bool found_element = MALI_FALSE;

	mutex_lock(&kbdev->kctx_list_lock);
	list_for_each_entry_safe(element, tmp, &kbdev->kctx_list, link) {
		if (element->kctx == kctx) {
			list_del(&element->link);
			kfree(element);
			found_element = MALI_TRUE;
		}
	}
	mutex_unlock(&kbdev->kctx_list_lock);
	if (!found_element)
		dev_warn(kbdev->dev, "kctx not in kctx_list\n");

	filp->private_data = NULL;
	kbase_destroy_context(kctx);

	dev_dbg(kbdev->dev, "deleted base context\n");
	kbase_release_device(kbdev);
	return 0;
}

#define CALL_MAX_SIZE 536

static long kbase_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
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

	if (MALI_ERROR_NONE != kbase_dispatch(kctx, &msg, size))
		return -EFAULT;

	if (0 != copy_to_user((void __user *)arg, &msg, size)) {
		dev_err(kctx->kbdev->dev, "failed to copy results of UK call back to user space\n");
		return -EFAULT;
	}
	return 0;
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

			if (wait_event_interruptible(kctx->event_queue, kbase_event_pending(kctx)))
				return -ERESTARTSYS;
		}
		if (uevent.event_code == BASE_JD_EVENT_DRV_TERMINATED) {
			if (out_count == 0)
				return -EPIPE;
			goto out;
		}

		if (copy_to_user(buf, &uevent, sizeof(uevent)))
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

KBASE_EXPORT_TEST_API(kbase_event_wakeup)

static int kbase_check_flags(int flags)
{
	/* Enforce that the driver keeps the O_CLOEXEC flag so that execve() always
	 * closes the file descriptor in a child process.
	 */
	if (0 == (flags & O_CLOEXEC))
		return -EINVAL;

	return 0;
}

static unsigned long kbase_get_unmapped_area(struct file *filp,
		const unsigned long addr, const unsigned long len,
		const unsigned long pgoff, const unsigned long flags)
{
#ifdef CONFIG_64BIT
	/* based on get_unmapped_area, but simplified slightly due to that some
	 * values are known in advance */
	struct kbase_context *kctx = filp->private_data;

	if (!kctx->is_compat && !addr &&
		kbase_hw_has_feature(kctx->kbdev, BASE_HW_FEATURE_33BIT_VA)) {
		struct mm_struct *mm = current->mm;
		struct vm_area_struct *vma;
		unsigned long low_limit, high_limit, gap_start, gap_end;

		/* Hardware has smaller VA than userspace, ensure the page
		 * comes from a VA which can be used on the GPU */

		gap_end = (1UL<<33);
		if (gap_end < len)
			return -ENOMEM;
		high_limit = gap_end - len;
		low_limit = PAGE_SIZE + len;

		gap_start = mm->highest_vm_end;
		if (gap_start <= high_limit)
			goto found_highest;

		if (RB_EMPTY_ROOT(&mm->mm_rb))
			return -ENOMEM;
		vma = rb_entry(mm->mm_rb.rb_node, struct vm_area_struct, vm_rb);
		if (vma->rb_subtree_gap < len)
			return -ENOMEM;

		while (true) {
			gap_start = vma->vm_prev ? vma->vm_prev->vm_end : 0;
			if (gap_start <= high_limit && vma->vm_rb.rb_right) {
				struct vm_area_struct *right =
					rb_entry(vma->vm_rb.rb_right,
						 struct vm_area_struct, vm_rb);
				if (right->rb_subtree_gap >= len) {
					vma = right;
					continue;
				}
			}
check_current:
			gap_end = vma->vm_start;
			if (gap_end < low_limit)
				return -ENOMEM;
			if (gap_start <= high_limit &&
			    gap_end - gap_start >= len)
				goto found;

			if (vma->vm_rb.rb_left) {
				struct vm_area_struct *left =
					rb_entry(vma->vm_rb.rb_left,
						 struct vm_area_struct, vm_rb);

				if (left->rb_subtree_gap >= len) {
					vma = left;
					continue;
				}
			}
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

found:
		if (gap_end > (1UL<<33))
			gap_end = (1UL<<33);

found_highest:
		gap_end -= len;

		VM_BUG_ON(gap_end < PAGE_SIZE);
		VM_BUG_ON(gap_end < gap_start);
		return gap_end;
	}
#endif
	/* No special requirements - fallback to the default version */
	return current->mm->get_unmapped_area(filp, addr, len, pgoff, flags);
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

#ifndef CONFIG_MALI_NO_MALI
void kbase_os_reg_write(struct kbase_device *kbdev, u16 offset, u32 value)
{
	writel(value, kbdev->reg + offset);
}

u32 kbase_os_reg_read(struct kbase_device *kbdev, u16 offset)
{
	return readl(kbdev->reg + offset);
}
#endif

#ifndef CONFIG_MALI_NO_MALI

static void *kbase_tag(void *ptr, u32 tag)
{
	return (void *)(((uintptr_t) ptr) | tag);
}

static void *kbase_untag(void *ptr)
{
	return (void *)(((uintptr_t) ptr) & ~3);
}




static irqreturn_t kbase_job_irq_handler(int irq, void *data)
{
	unsigned long flags;
	struct kbase_device *kbdev = kbase_untag(data);
	u32 val;

	spin_lock_irqsave(&kbdev->pm.gpu_powered_lock, flags);

	if (!kbdev->pm.gpu_powered) {
		/* GPU is turned off - IRQ is not for us */
		spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);
		return IRQ_NONE;
	}

	val = kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_STATUS), NULL);

#ifdef CONFIG_MALI_DEBUG
	if (!kbdev->pm.driver_ready_for_irqs)
		dev_warn(kbdev->dev, "%s: irq %d irqstatus 0x%x before driver is ready\n",
				__func__, irq, val);
#endif /* CONFIG_MALI_DEBUG */
	spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);

	if (!val)
		return IRQ_NONE;

	dev_dbg(kbdev->dev, "%s: irq %d irqstatus 0x%x\n", __func__, irq, val);

	kbase_job_done(kbdev, val);

	return IRQ_HANDLED;
}

KBASE_EXPORT_TEST_API(kbase_job_irq_handler);

static irqreturn_t kbase_mmu_irq_handler(int irq, void *data)
{
	unsigned long flags;
	struct kbase_device *kbdev = kbase_untag(data);
	u32 val;

	spin_lock_irqsave(&kbdev->pm.gpu_powered_lock, flags);

	if (!kbdev->pm.gpu_powered) {
		/* GPU is turned off - IRQ is not for us */
		spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);
		return IRQ_NONE;
	}

	val = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_STATUS), NULL);

#ifdef CONFIG_MALI_DEBUG
	if (!kbdev->pm.driver_ready_for_irqs)
		dev_warn(kbdev->dev, "%s: irq %d irqstatus 0x%x before driver is ready\n",
				__func__, irq, val);
#endif /* CONFIG_MALI_DEBUG */
	spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);

	if (!val)
		return IRQ_NONE;

	dev_dbg(kbdev->dev, "%s: irq %d irqstatus 0x%x\n", __func__, irq, val);

	kbase_mmu_interrupt(kbdev, val);

	return IRQ_HANDLED;
}

static irqreturn_t kbase_gpu_irq_handler(int irq, void *data)
{
	unsigned long flags;
	struct kbase_device *kbdev = kbase_untag(data);
	u32 val;

	spin_lock_irqsave(&kbdev->pm.gpu_powered_lock, flags);

	if (!kbdev->pm.gpu_powered) {
		/* GPU is turned off - IRQ is not for us */
		spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);
		return IRQ_NONE;
	}

	val = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_STATUS), NULL);

#ifdef CONFIG_MALI_DEBUG
	if (!kbdev->pm.driver_ready_for_irqs)
		dev_dbg(kbdev->dev, "%s: irq %d irqstatus 0x%x before driver is ready\n",
				__func__, irq, val);
#endif /* CONFIG_MALI_DEBUG */
	spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);

	if (!val)
		return IRQ_NONE;

	dev_dbg(kbdev->dev, "%s: irq %d irqstatus 0x%x\n", __func__, irq, val);

	kbase_gpu_interrupt(kbdev, val);

	return IRQ_HANDLED;
}
static irq_handler_t kbase_handler_table[] = {
	[JOB_IRQ_TAG] = kbase_job_irq_handler,
	[MMU_IRQ_TAG] = kbase_mmu_irq_handler,
	[GPU_IRQ_TAG] = kbase_gpu_irq_handler,
};


#ifdef CONFIG_MALI_DEBUG
#define  JOB_IRQ_HANDLER JOB_IRQ_TAG
#define  MMU_IRQ_HANDLER MMU_IRQ_TAG
#define  GPU_IRQ_HANDLER GPU_IRQ_TAG

/**
 * @brief Registers given interrupt handler for requested interrupt type
 *        Case irq handler is not specified default handler shall be registered
 *
 * @param[in] kbdev           - Device for which the handler is to be registered
 * @param[in] custom_handler  - Handler to be registered
 * @param[in] irq_type        - Interrupt type
 * @return	MALI_ERROR_NONE case success, MALI_ERROR_FUNCTION_FAILED otherwise
 */
static mali_error kbase_set_custom_irq_handler(struct kbase_device *kbdev, irq_handler_t custom_handler, int irq_type)
{
	mali_error result = MALI_ERROR_NONE;
	irq_handler_t requested_irq_handler = NULL;

	KBASE_DEBUG_ASSERT((JOB_IRQ_HANDLER <= irq_type) && (GPU_IRQ_HANDLER >= irq_type));

	/* Release previous handler */
	if (kbdev->irqs[irq_type].irq)
		free_irq(kbdev->irqs[irq_type].irq, kbase_tag(kbdev, irq_type));

	requested_irq_handler = (NULL != custom_handler) ? custom_handler : kbase_handler_table[irq_type];

	if (0 != request_irq(kbdev->irqs[irq_type].irq,
			requested_irq_handler,
			kbdev->irqs[irq_type].flags | IRQF_SHARED,
			dev_name(kbdev->dev), kbase_tag(kbdev, irq_type))) {
		result = MALI_ERROR_FUNCTION_FAILED;
		dev_err(kbdev->dev, "Can't request interrupt %d (index %d)\n", kbdev->irqs[irq_type].irq, irq_type);
#ifdef CONFIG_SPARSE_IRQ
		dev_err(kbdev->dev, "You have CONFIG_SPARSE_IRQ support enabled - is the interrupt number correct for this configuration?\n");
#endif /* CONFIG_SPARSE_IRQ */
	}

	return result;
}

KBASE_EXPORT_TEST_API(kbase_set_custom_irq_handler)

/* test correct interrupt assigment and reception by cpu */
struct kbasep_irq_test {
	struct hrtimer timer;
	wait_queue_head_t wait;
	int triggered;
	u32 timeout;
};

static struct kbasep_irq_test kbasep_irq_test_data;

#define IRQ_TEST_TIMEOUT    500

static irqreturn_t kbase_job_irq_test_handler(int irq, void *data)
{
	unsigned long flags;
	struct kbase_device *kbdev = kbase_untag(data);
	u32 val;

	spin_lock_irqsave(&kbdev->pm.gpu_powered_lock, flags);

	if (!kbdev->pm.gpu_powered) {
		/* GPU is turned off - IRQ is not for us */
		spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);
		return IRQ_NONE;
	}

	val = kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_STATUS), NULL);

	spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);

	if (!val)
		return IRQ_NONE;

	dev_dbg(kbdev->dev, "%s: irq %d irqstatus 0x%x\n", __func__, irq, val);

	kbasep_irq_test_data.triggered = 1;
	wake_up(&kbasep_irq_test_data.wait);

	kbase_reg_write(kbdev, JOB_CONTROL_REG(JOB_IRQ_CLEAR), val, NULL);

	return IRQ_HANDLED;
}

static irqreturn_t kbase_mmu_irq_test_handler(int irq, void *data)
{
	unsigned long flags;
	struct kbase_device *kbdev = kbase_untag(data);
	u32 val;

	spin_lock_irqsave(&kbdev->pm.gpu_powered_lock, flags);

	if (!kbdev->pm.gpu_powered) {
		/* GPU is turned off - IRQ is not for us */
		spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);
		return IRQ_NONE;
	}

	val = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_STATUS), NULL);

	spin_unlock_irqrestore(&kbdev->pm.gpu_powered_lock, flags);

	if (!val)
		return IRQ_NONE;

	dev_dbg(kbdev->dev, "%s: irq %d irqstatus 0x%x\n", __func__, irq, val);

	kbasep_irq_test_data.triggered = 1;
	wake_up(&kbasep_irq_test_data.wait);

	kbase_reg_write(kbdev, MMU_REG(MMU_IRQ_CLEAR), val, NULL);

	return IRQ_HANDLED;
}

static enum hrtimer_restart kbasep_test_interrupt_timeout(struct hrtimer *timer)
{
	struct kbasep_irq_test *test_data = container_of(timer, struct kbasep_irq_test, timer);

	test_data->timeout = 1;
	test_data->triggered = 1;
	wake_up(&test_data->wait);
	return HRTIMER_NORESTART;
}

static mali_error kbasep_common_test_interrupt(struct kbase_device * const kbdev, u32 tag)
{
	mali_error err = MALI_ERROR_NONE;
	irq_handler_t test_handler;

	u32 old_mask_val;
	u16 mask_offset;
	u16 rawstat_offset;

	switch (tag) {
	case JOB_IRQ_TAG:
		test_handler = kbase_job_irq_test_handler;
		rawstat_offset = JOB_CONTROL_REG(JOB_IRQ_RAWSTAT);
		mask_offset = JOB_CONTROL_REG(JOB_IRQ_MASK);
		break;
	case MMU_IRQ_TAG:
		test_handler = kbase_mmu_irq_test_handler;
		rawstat_offset = MMU_REG(MMU_IRQ_RAWSTAT);
		mask_offset = MMU_REG(MMU_IRQ_MASK);
		break;
	case GPU_IRQ_TAG:
		/* already tested by pm_driver - bail out */
	default:
		return MALI_ERROR_NONE;
	}

	/* store old mask */
	old_mask_val = kbase_reg_read(kbdev, mask_offset, NULL);
	/* mask interrupts */
	kbase_reg_write(kbdev, mask_offset, 0x0, NULL);

	if (kbdev->irqs[tag].irq) {
		/* release original handler and install test handler */
		if (MALI_ERROR_NONE != kbase_set_custom_irq_handler(kbdev, test_handler, tag)) {
			err = MALI_ERROR_FUNCTION_FAILED;
		} else {
			kbasep_irq_test_data.timeout = 0;
			hrtimer_init(&kbasep_irq_test_data.timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
			kbasep_irq_test_data.timer.function = kbasep_test_interrupt_timeout;

			/* trigger interrupt */
			kbase_reg_write(kbdev, mask_offset, 0x1, NULL);
			kbase_reg_write(kbdev, rawstat_offset, 0x1, NULL);

			hrtimer_start(&kbasep_irq_test_data.timer, HR_TIMER_DELAY_MSEC(IRQ_TEST_TIMEOUT), HRTIMER_MODE_REL);

			wait_event(kbasep_irq_test_data.wait, kbasep_irq_test_data.triggered != 0);

			if (kbasep_irq_test_data.timeout != 0) {
				dev_err(kbdev->dev, "Interrupt %d (index %d) didn't reach CPU.\n", kbdev->irqs[tag].irq, tag);
				err = MALI_ERROR_FUNCTION_FAILED;
			} else {
				dev_dbg(kbdev->dev, "Interrupt %d (index %d) reached CPU.\n", kbdev->irqs[tag].irq, tag);
			}

			hrtimer_cancel(&kbasep_irq_test_data.timer);
			kbasep_irq_test_data.triggered = 0;

			/* mask interrupts */
			kbase_reg_write(kbdev, mask_offset, 0x0, NULL);

			/* release test handler */
			free_irq(kbdev->irqs[tag].irq, kbase_tag(kbdev, tag));
		}

		/* restore original interrupt */
		if (request_irq(kbdev->irqs[tag].irq, kbase_handler_table[tag], kbdev->irqs[tag].flags | IRQF_SHARED, dev_name(kbdev->dev), kbase_tag(kbdev, tag))) {
			dev_err(kbdev->dev, "Can't restore original interrupt %d (index %d)\n", kbdev->irqs[tag].irq, tag);
			err = MALI_ERROR_FUNCTION_FAILED;
		}
	}
	/* restore old mask */
	kbase_reg_write(kbdev, mask_offset, old_mask_val, NULL);

	return err;
}

static mali_error kbasep_common_test_interrupt_handlers(struct kbase_device * const kbdev)
{
	mali_error err;

	init_waitqueue_head(&kbasep_irq_test_data.wait);
	kbasep_irq_test_data.triggered = 0;

	/* A suspend won't happen during startup/insmod */
	kbase_pm_context_active(kbdev);

	err = kbasep_common_test_interrupt(kbdev, JOB_IRQ_TAG);
	if (MALI_ERROR_NONE != err) {
		dev_err(kbdev->dev, "Interrupt JOB_IRQ didn't reach CPU. Check interrupt assignments.\n");
		goto out;
	}

	err = kbasep_common_test_interrupt(kbdev, MMU_IRQ_TAG);
	if (MALI_ERROR_NONE != err) {
		dev_err(kbdev->dev, "Interrupt MMU_IRQ didn't reach CPU. Check interrupt assignments.\n");
		goto out;
	}

	dev_dbg(kbdev->dev, "Interrupts are correctly assigned.\n");

 out:
	kbase_pm_context_idle(kbdev);

	return err;
}
#endif /* CONFIG_MALI_DEBUG */

static int kbase_install_interrupts(struct kbase_device *kbdev)
{
	u32 nr = ARRAY_SIZE(kbase_handler_table);
	int err;
	u32 i;

	for (i = 0; i < nr; i++) {
		err = request_irq(kbdev->irqs[i].irq, kbase_handler_table[i], kbdev->irqs[i].flags | IRQF_SHARED, dev_name(kbdev->dev), kbase_tag(kbdev, i));
		if (err) {
			dev_err(kbdev->dev, "Can't request interrupt %d (index %d)\n", kbdev->irqs[i].irq, i);
#ifdef CONFIG_SPARSE_IRQ
			dev_err(kbdev->dev, "You have CONFIG_SPARSE_IRQ support enabled - is the interrupt number correct for this configuration?\n");
#endif /* CONFIG_SPARSE_IRQ */
			goto release;
		}
	}

	return 0;

 release:
	while (i-- > 0)
		free_irq(kbdev->irqs[i].irq, kbase_tag(kbdev, i));

	return err;
}

static void kbase_release_interrupts(struct kbase_device *kbdev)
{
	u32 nr = ARRAY_SIZE(kbase_handler_table);
	u32 i;

	for (i = 0; i < nr; i++) {
		if (kbdev->irqs[i].irq)
			free_irq(kbdev->irqs[i].irq, kbase_tag(kbdev, i));
	}
}

void kbase_synchronize_irqs(struct kbase_device *kbdev)
{
	u32 nr = ARRAY_SIZE(kbase_handler_table);
	u32 i;

	for (i = 0; i < nr; i++) {
		if (kbdev->irqs[i].irq)
			synchronize_irq(kbdev->irqs[i].irq);
	}
}
#endif /* CONFIG_MALI_NO_MALI */

#if KBASE_PM_EN
/** Show callback for the @c power_policy sysfs file.
 *
 * This function is called to get the contents of the @c power_policy sysfs
 * file. This is a list of the available policies with the currently active one
 * surrounded by square brackets.
 *
 * @param dev	The device this sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The output buffer for the sysfs file contents
 *
 * @return The number of bytes output to @c buf.
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

/** Store callback for the @c power_policy sysfs file.
 *
 * This function is called when the @c power_policy sysfs file is written to.
 * It matches the requested policy against the available policies and if a
 * matching policy is found calls @ref kbase_pm_set_policy to change the
 * policy.
 *
 * @param dev	The device with sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The value written to the sysfs file
 * @param count	The number of bytes written to the sysfs file
 *
 * @return @c count if the function succeeded. An error code on failure.
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

/** The sysfs file @c power_policy.
 *
 * This is used for obtaining information about the available policies,
 * determining which policy is currently active, and changing the active
 * policy.
 */
static DEVICE_ATTR(power_policy, S_IRUGO | S_IWUSR, show_policy, set_policy);

/** Show callback for the @c core_availability_policy sysfs file.
 *
 * This function is called to get the contents of the @c core_availability_policy
 * sysfs file. This is a list of the available policies with the currently
 * active one surrounded by square brackets.
 *
 * @param dev	The device this sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The output buffer for the sysfs file contents
 *
 * @return The number of bytes output to @c buf.
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

/** Store callback for the @c core_availability_policy sysfs file.
 *
 * This function is called when the @c core_availability_policy sysfs file is
 * written to. It matches the requested policy against the available policies
 * and if a matching policy is found calls @ref kbase_pm_set_policy to change
 * the policy.
 *
 * @param dev	The device with sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The value written to the sysfs file
 * @param count	The number of bytes written to the sysfs file
 *
 * @return @c count if the function succeeded. An error code on failure.
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

/** The sysfs file @c core_availability_policy
 *
 * This is used for obtaining information about the available policies,
 * determining which policy is currently active, and changing the active
 * policy.
 */
static DEVICE_ATTR(core_availability_policy, S_IRUGO | S_IWUSR, show_ca_policy, set_ca_policy);

/** Show callback for the @c core_mask sysfs file.
 *
 * This function is called to get the contents of the @c core_mask sysfs
 * file.
 *
 * @param dev	The device this sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The output buffer for the sysfs file contents
 *
 * @return The number of bytes output to @c buf.
 */
static ssize_t show_core_mask(struct device *dev, struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	ret += scnprintf(buf + ret, PAGE_SIZE - ret, "Current core mask : 0x%llX\n", kbdev->pm.debug_core_mask);
	ret += scnprintf(buf + ret, PAGE_SIZE - ret, "Available core mask : 0x%llX\n", kbdev->shader_present_bitmap);

	return ret;
}

/** Store callback for the @c core_mask sysfs file.
 *
 * This function is called when the @c core_mask sysfs file is written to.
 *
 * @param dev	The device with sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The value written to the sysfs file
 * @param count	The number of bytes written to the sysfs file
 *
 * @return @c count if the function succeeded. An error code on failure.
 */
static ssize_t set_core_mask(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	u64 new_core_mask;
	int rc;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	rc = kstrtoull(buf, 16, &new_core_mask);
	if (rc)
		return rc;

	if ((new_core_mask & kbdev->shader_present_bitmap) != new_core_mask ||
	    !(new_core_mask & kbdev->gpu_props.props.coherency_info.group[0].core_mask)) {
		dev_err(dev, "power_policy: invalid core specification\n");
		return -EINVAL;
	}

	if (kbdev->pm.debug_core_mask != new_core_mask) {
		unsigned long flags;

		spin_lock_irqsave(&kbdev->pm.power_change_lock, flags);

		kbdev->pm.debug_core_mask = new_core_mask;
		kbase_pm_update_cores_state_nolock(kbdev);

		spin_unlock_irqrestore(&kbdev->pm.power_change_lock, flags);
	}

	return count;
}

/** The sysfs file @c core_mask.
 *
 * This is used to restrict shader core availability for debugging purposes.
 * Reading it will show the current core mask and the mask of cores available.
 * Writing to it will set the current core mask.
 */
static DEVICE_ATTR(core_mask, S_IRUGO | S_IWUSR, show_core_mask, set_core_mask);
#endif /* KBASE_PM_EN */

#ifdef CONFIG_MALI_DEBUG_SHADER_SPLIT_FS
/* Import the external affinity mask variables */
extern u64 mali_js0_affinity_mask;
extern u64 mali_js1_affinity_mask;
extern u64 mali_js2_affinity_mask;

/**
 * Structure containing a single shader affinity split configuration.
 */
struct sc_split_config {
	char const *tag;
	char const *human_readable;
	u64          js0_mask;
	u64          js1_mask;
	u64          js2_mask;
};

/**
 * Array of available shader affinity split configurations.
 */
static struct sc_split_config const sc_split_configs[] = {
	/* All must be the first config (default). */
	{
		"all", "All cores",
		0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL, 0xFFFFFFFFFFFFFFFFULL
	},
	{
		"mp1", "MP1 shader core",
		0x1, 0x1, 0x1
	},
	{
		"mp2", "MP2 shader core",
		0x3, 0x3, 0x3
	},
	{
		"mp4", "MP4 shader core",
		0xF, 0xF, 0xF
	},
	{
		"mp1_vf", "MP1 vertex + MP1 fragment shader core",
		0x2, 0x1, 0xFFFFFFFFFFFFFFFFULL
	},
	{
		"mp2_vf", "MP2 vertex + MP2 fragment shader core",
		0xA, 0x5, 0xFFFFFFFFFFFFFFFFULL
	},
	/* This must be the last config. */
	{
		NULL, NULL,
		0x0, 0x0, 0x0
	},
};

/* Pointer to the currently active shader split configuration. */
static struct sc_split_config const *current_sc_split_config = &sc_split_configs[0];

/** Show callback for the @c sc_split sysfs file
 *
 * Returns the current shader core affinity policy.
 */
static ssize_t show_split(struct device *dev, struct device_attribute *attr, char * const buf)
{
	ssize_t ret;
	/* We know we are given a buffer which is PAGE_SIZE long. Our strings are all guaranteed
	 * to be shorter than that at this time so no length check needed. */
	ret = scnprintf(buf, PAGE_SIZE, "Current sc_split: '%s'\n", current_sc_split_config->tag);
	return ret;
}

/** Store callback for the @c sc_split sysfs file.
 *
 * This function is called when the @c sc_split sysfs file is written to
 * It modifies the system shader core affinity configuration to allow
 * system profiling with different hardware configurations.
 *
 * @param dev	The device with sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The value written to the sysfs file
 * @param count	The number of bytes written to the sysfs file
 *
 * @return @c count if the function succeeded. An error code on failure.
 */
static ssize_t set_split(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct sc_split_config const *config = &sc_split_configs[0];

	/* Try to match: loop until we hit the last "NULL" entry */
	while (config->tag) {
		if (sysfs_streq(config->tag, buf)) {
			current_sc_split_config = config;
			mali_js0_affinity_mask  = config->js0_mask;
			mali_js1_affinity_mask  = config->js1_mask;
			mali_js2_affinity_mask  = config->js2_mask;
			dev_dbg(dev, "Setting sc_split: '%s'\n", config->tag);
			return count;
		}
		config++;
	}

	/* No match found in config list */
	dev_err(dev, "sc_split: invalid value\n");
	dev_err(dev, "  Possible settings: mp[1|2|4], mp[1|2]_vf\n");
	return -ENOENT;
}

/** The sysfs file @c sc_split
 *
 * This is used for configuring/querying the current shader core work affinity
 * configuration.
 */
static DEVICE_ATTR(sc_split, S_IRUGO|S_IWUSR, show_split, set_split);
#endif /* CONFIG_MALI_DEBUG_SHADER_SPLIT_FS */


#if !MALI_CUSTOMER_RELEASE
/** Store callback for the @c js_timeouts sysfs file.
 *
 * This function is called to get the contents of the @c js_timeouts sysfs
 * file. This file contains five values separated by whitespace. The values
 * are basically the same as KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS,
 * KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS, KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS,
 * KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS, BASE_CONFIG_ATTR_JS_RESET_TICKS_NSS
 * configuration values (in that order), with the difference that the js_timeout
 * valus are expressed in MILLISECONDS.
 *
 * The js_timeouts sysfile file allows the current values in
 * use by the job scheduler to get override. Note that a value needs to
 * be other than 0 for it to override the current job scheduler value.
 *
 * @param dev	The device with sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The value written to the sysfs file
 * @param count	The number of bytes written to the sysfs file
 *
 * @return @c count if the function succeeded. An error code on failure.
 */
static ssize_t set_js_timeouts(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int items;
	unsigned long js_soft_stop_ms;
	unsigned long js_soft_stop_ms_cl;
	unsigned long js_hard_stop_ms_ss;
	unsigned long js_hard_stop_ms_cl;
	unsigned long js_hard_stop_ms_nss;
	unsigned long js_reset_ms_ss;
	unsigned long js_reset_ms_cl;
	unsigned long js_reset_ms_nss;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	items = sscanf(buf, "%lu %lu %lu %lu %lu %lu %lu %lu",
			&js_soft_stop_ms, &js_soft_stop_ms_cl,
			&js_hard_stop_ms_ss, &js_hard_stop_ms_cl,
			&js_hard_stop_ms_nss, &js_reset_ms_ss,
			&js_reset_ms_cl, &js_reset_ms_nss);

	if (items == 8) {
		u64 ticks;

		ticks = js_soft_stop_ms * 1000000ULL;
		do_div(ticks, kbdev->js_data.scheduling_tick_ns);
		kbdev->js_soft_stop_ticks = ticks;

		ticks = js_soft_stop_ms_cl * 1000000ULL;
		do_div(ticks, kbdev->js_data.scheduling_tick_ns);
		kbdev->js_soft_stop_ticks_cl = ticks;

		ticks = js_hard_stop_ms_ss * 1000000ULL;
		do_div(ticks, kbdev->js_data.scheduling_tick_ns);
		kbdev->js_hard_stop_ticks_ss = ticks;

		ticks = js_hard_stop_ms_cl * 1000000ULL;
		do_div(ticks, kbdev->js_data.scheduling_tick_ns);
		kbdev->js_hard_stop_ticks_cl = ticks;

		ticks = js_hard_stop_ms_nss * 1000000ULL;
		do_div(ticks, kbdev->js_data.scheduling_tick_ns);
		kbdev->js_hard_stop_ticks_nss = ticks;

		ticks = js_reset_ms_ss * 1000000ULL;
		do_div(ticks, kbdev->js_data.scheduling_tick_ns);
		kbdev->js_reset_ticks_ss = ticks;

		ticks = js_reset_ms_cl * 1000000ULL;
		do_div(ticks, kbdev->js_data.scheduling_tick_ns);
		kbdev->js_reset_ticks_cl = ticks;

		ticks = js_reset_ms_nss * 1000000ULL;
		do_div(ticks, kbdev->js_data.scheduling_tick_ns);
		kbdev->js_reset_ticks_nss = ticks;

		dev_dbg(kbdev->dev, "Overriding KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS with %lu ticks (%lu ms)\n", (unsigned long)kbdev->js_soft_stop_ticks, js_soft_stop_ms);
		dev_dbg(kbdev->dev, "Overriding KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS_CL with %lu ticks (%lu ms)\n", (unsigned long)kbdev->js_soft_stop_ticks_cl, js_soft_stop_ms_cl);
		dev_dbg(kbdev->dev, "Overriding KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS with %lu ticks (%lu ms)\n", (unsigned long)kbdev->js_hard_stop_ticks_ss, js_hard_stop_ms_ss);
		dev_dbg(kbdev->dev, "Overriding KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_CL with %lu ticks (%lu ms)\n", (unsigned long)kbdev->js_hard_stop_ticks_cl, js_hard_stop_ms_cl);
		dev_dbg(kbdev->dev, "Overriding KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS with %lu ticks (%lu ms)\n", (unsigned long)kbdev->js_hard_stop_ticks_nss, js_hard_stop_ms_nss);
		dev_dbg(kbdev->dev, "Overriding KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS with %lu ticks (%lu ms)\n", (unsigned long)kbdev->js_reset_ticks_ss, js_reset_ms_ss);
		dev_dbg(kbdev->dev, "Overriding KBASE_CONFIG_ATTR_JS_RESET_TICKS_CL with %lu ticks (%lu ms)\n", (unsigned long)kbdev->js_reset_ticks_cl, js_reset_ms_cl);
		dev_dbg(kbdev->dev, "Overriding KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS with %lu ticks (%lu ms)\n", (unsigned long)kbdev->js_reset_ticks_nss, js_reset_ms_nss);

		return count;
	} else {
		dev_err(kbdev->dev, "Couldn't process js_timeouts write operation.\nUse format " "<soft_stop_ms> <hard_stop_ms_ss> <hard_stop_ms_nss> <reset_ms_ss> <reset_ms_nss>\n");
		return -EINVAL;
	}
}

/** Show callback for the @c js_timeouts sysfs file.
 *
 * This function is called to get the contents of the @c js_timeouts sysfs
 * file. It returns the last set values written to the js_timeouts sysfs file.
 * If the file didn't get written yet, the values will be 0.
 *
 * @param dev	The device this sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The output buffer for the sysfs file contents
 *
 * @return The number of bytes output to @c buf.
 */
static ssize_t show_js_timeouts(struct device *dev, struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;
	u64 ms;
	unsigned long js_soft_stop_ms;
	unsigned long js_soft_stop_ms_cl;
	unsigned long js_hard_stop_ms_ss;
	unsigned long js_hard_stop_ms_cl;
	unsigned long js_hard_stop_ms_nss;
	unsigned long js_reset_ms_ss;
	unsigned long js_reset_ms_cl;
	unsigned long js_reset_ms_nss;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ms = (u64) kbdev->js_soft_stop_ticks * kbdev->js_data.scheduling_tick_ns;
	do_div(ms, 1000000UL);
	js_soft_stop_ms = (unsigned long)ms;

	ms = (u64) kbdev->js_soft_stop_ticks_cl * kbdev->js_data.scheduling_tick_ns;
	do_div(ms, 1000000UL);
	js_soft_stop_ms_cl = (unsigned long)ms;

	ms = (u64) kbdev->js_hard_stop_ticks_ss * kbdev->js_data.scheduling_tick_ns;
	do_div(ms, 1000000UL);
	js_hard_stop_ms_ss = (unsigned long)ms;

	ms = (u64) kbdev->js_hard_stop_ticks_cl * kbdev->js_data.scheduling_tick_ns;
	do_div(ms, 1000000UL);
	js_hard_stop_ms_cl = (unsigned long)ms;

	ms = (u64) kbdev->js_hard_stop_ticks_nss * kbdev->js_data.scheduling_tick_ns;
	do_div(ms, 1000000UL);
	js_hard_stop_ms_nss = (unsigned long)ms;

	ms = (u64) kbdev->js_reset_ticks_ss * kbdev->js_data.scheduling_tick_ns;
	do_div(ms, 1000000UL);
	js_reset_ms_ss = (unsigned long)ms;

	ms = (u64) kbdev->js_reset_ticks_cl * kbdev->js_data.scheduling_tick_ns;
	do_div(ms, 1000000UL);
	js_reset_ms_cl = (unsigned long)ms;

	ms = (u64) kbdev->js_reset_ticks_nss * kbdev->js_data.scheduling_tick_ns;
	do_div(ms, 1000000UL);
	js_reset_ms_nss = (unsigned long)ms;

	ret = scnprintf(buf, PAGE_SIZE, "%lu %lu %lu %lu %lu %lu %lu %lu\n",
			js_soft_stop_ms, js_soft_stop_ms_cl,
			js_hard_stop_ms_ss, js_hard_stop_ms_cl,
			js_hard_stop_ms_nss, js_reset_ms_ss,
			js_reset_ms_cl, js_reset_ms_nss);

	if (ret >= PAGE_SIZE) {
		buf[PAGE_SIZE - 2] = '\n';
		buf[PAGE_SIZE - 1] = '\0';
		ret = PAGE_SIZE - 1;
	}

	return ret;
}

/** The sysfs file @c js_timeouts.
 *
 * This is used to override the current job scheduler values for
 * KBASE_CONFIG_ATTR_JS_STOP_STOP_TICKS_SS
 * KBASE_CONFIG_ATTR_JS_STOP_STOP_TICKS_CL
 * KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS
 * KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_CL
 * KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS
 * KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS
 * KBASE_CONFIG_ATTR_JS_RESET_TICKS_CL
 * KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS.
 */
static DEVICE_ATTR(js_timeouts, S_IRUGO | S_IWUSR, show_js_timeouts, set_js_timeouts);



/** Store callback for the @c force_replay sysfs file.
 *
 * @param dev	The device with sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The value written to the sysfs file
 * @param count	The number of bytes written to the sysfs file
 *
 * @return @c count if the function succeeded. An error code on failure.
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
			kbdev->force_replay_random = MALI_FALSE;
			kbdev->force_replay_limit = force_replay_limit;
			kbdev->force_replay_count = 0;

			return count;
		}
	} else if (!strncmp("random_limit", buf, MIN(12, count))) {
		kbdev->force_replay_random = MALI_TRUE;
		kbdev->force_replay_count = 0;

		return count;
	} else if (!strncmp("norandom_limit", buf, MIN(14, count))) {
		kbdev->force_replay_random = MALI_FALSE;
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

/** Show callback for the @c force_replay sysfs file.
 *
 * This function is called to get the contents of the @c force_replay sysfs
 * file. It returns the last set value written to the force_replay sysfs file.
 * If the file didn't get written yet, the values will be 0.
 *
 * @param dev	The device this sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The output buffer for the sysfs file contents
 *
 * @return The number of bytes output to @c buf.
 */
static ssize_t show_force_replay(struct device *dev, struct device_attribute *attr, char * const buf)
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

/** The sysfs file @c force_replay.
 *
 */
static DEVICE_ATTR(force_replay, S_IRUGO | S_IWUSR, show_force_replay, set_force_replay);
#endif /* !MALI_CUSTOMER_RELEASE */

#ifdef CONFIG_MALI_DEBUG
static ssize_t set_js_softstop_always(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int items;
	int softstop_always;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	items = sscanf(buf, "%d", &softstop_always);
	if ((items == 1) && ((softstop_always == 0) || (softstop_always == 1))) {
		kbdev->js_data.softstop_always = (mali_bool) softstop_always;
		dev_dbg(kbdev->dev, "Support for softstop on a single context: %s\n", (kbdev->js_data.softstop_always == MALI_FALSE) ? "Disabled" : "Enabled");
		return count;
	} else {
		dev_err(kbdev->dev, "Couldn't process js_softstop_always write operation.\nUse format " "<soft_stop_always>\n");
		return -EINVAL;
	}
}

static ssize_t show_js_softstop_always(struct device *dev, struct device_attribute *attr, char * const buf)
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

/**
 * By default, soft-stops are disabled when only a single context is present. The ability to
 * enable soft-stop when only a single context is present can be used for debug and unit-testing purposes.
 * (see CL t6xx_stress_1 unit-test as an example whereby this feature is used.)
 */
static DEVICE_ATTR(js_softstop_always, S_IRUGO | S_IWUSR, show_js_softstop_always, set_js_softstop_always);
#endif /* CONFIG_MALI_DEBUG */

#ifdef CONFIG_MALI_DEBUG
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

/** Debug commands supported by the driver */
static const struct kbasep_debug_command debug_commands[] = {
	{
	 .str = "dumptrace",
	 .func = &kbasep_trace_dump,
	 }
};

/** Show callback for the @c debug_command sysfs file.
 *
 * This function is called to get the contents of the @c debug_command sysfs
 * file. This is a list of the available debug commands, separated by newlines.
 *
 * @param dev	The device this sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The output buffer for the sysfs file contents
 *
 * @return The number of bytes output to @c buf.
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

/** Store callback for the @c debug_command sysfs file.
 *
 * This function is called when the @c debug_command sysfs file is written to.
 * It matches the requested command against the available commands, and if
 * a matching command is found calls the associated function from
 * @ref debug_commands to issue the command.
 *
 * @param dev	The device with sysfs file is for
 * @param attr	The attributes of the sysfs file
 * @param buf	The value written to the sysfs file
 * @param count	The number of bytes written to the sysfs file
 *
 * @return @c count if the function succeeded. An error code on failure.
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

/** The sysfs file @c debug_command.
 *
 * This is used to issue general debug commands to the device driver.
 * Reading it will produce a list of debug commands, separated by newlines.
 * Writing to it with one of those commands will issue said command.
 */
static DEVICE_ATTR(debug_command, S_IRUGO | S_IWUSR, show_debug, issue_debug);
#endif /* CONFIG_MALI_DEBUG */


#ifdef CONFIG_MALI_NO_MALI
static int kbase_common_reg_map(struct kbase_device *kbdev)
{
	return 0;
}
static void kbase_common_reg_unmap(struct kbase_device * const kbdev)
{
}
#else /* CONFIG_MALI_NO_MALI */
static int kbase_common_reg_map(struct kbase_device *kbdev)
{
	int err = -ENOMEM;

	kbdev->reg_res = request_mem_region(kbdev->reg_start, kbdev->reg_size, dev_name(kbdev->dev));
	if (!kbdev->reg_res) {
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

	return 0;

 out_ioremap:
	release_resource(kbdev->reg_res);
	kfree(kbdev->reg_res);
 out_region:
	return err;
}

static void kbase_common_reg_unmap(struct kbase_device * const kbdev)
{
	iounmap(kbdev->reg);
	release_resource(kbdev->reg_res);
	kfree(kbdev->reg_res);
}
#endif /* CONFIG_MALI_NO_MALI */

static int kbase_common_device_init(struct kbase_device *kbdev)
{
	int err = -ENOMEM;
	mali_error mali_err;
	enum {
		inited_mem = (1u << 0),
		inited_job_slot = (1u << 1),
		inited_pm = (1u << 2),
		inited_js = (1u << 3),
		inited_irqs = (1u << 4),
		inited_debug = (1u << 5),
		inited_js_softstop = (1u << 6),
#if !MALI_CUSTOMER_RELEASE
		inited_js_timeouts = (1u << 7),
		inited_force_replay = (1u << 13),
#endif /* !MALI_CUSTOMER_RELEASE */
		inited_pm_runtime_init = (1u << 8),
#ifdef CONFIG_DEBUG_FS
		inited_gpu_memory = (1u << 9),
#endif /* CONFIG_DEBUG_FS */
#ifdef CONFIG_MALI_DEBUG_SHADER_SPLIT_FS
		inited_sc_split = (1u << 11),
#endif /* CONFIG_MALI_DEBUG_SHADER_SPLIT_FS */
#ifdef CONFIG_MALI_TRACE_TIMELINE
		inited_timeline = (1u << 12),
#endif /* CONFIG_MALI_TRACE_LINE */
		inited_pm_powerup = (1u << 14),
	};

	int inited = 0;

	dev_set_drvdata(kbdev->dev, kbdev);

	kbdev->mdev.minor = MISC_DYNAMIC_MINOR;
	kbdev->mdev.name = kbdev->devname;
	kbdev->mdev.fops = &kbase_fops;
	kbdev->mdev.parent = get_device(kbdev->dev);

	kbase_disjoint_init(kbdev);

	scnprintf(kbdev->devname, DEVNAME_SIZE, "%s%d", kbase_drv_name, kbase_dev_nr++);

	if (misc_register(&kbdev->mdev)) {
		dev_err(kbdev->dev, "Couldn't register misc dev %s\n", kbdev->devname);
		err = -EINVAL;
		goto out_misc;
	}
#if KBASE_PM_EN
	if (device_create_file(kbdev->dev, &dev_attr_power_policy)) {
		dev_err(kbdev->dev, "Couldn't create power_policy sysfs file\n");
		goto out_file;
	}

	if (device_create_file(kbdev->dev, &dev_attr_core_availability_policy)) {
		dev_err(kbdev->dev, "Couldn't create core_availability_policy sysfs file\n");
		goto out_file_core_availability_policy;
	}

	if (device_create_file(kbdev->dev, &dev_attr_core_mask)) {
		dev_err(kbdev->dev, "Couldn't create core_mask sysfs file\n");
		goto out_file_core_mask;
	}
#endif /* KBASE_PM_EN */
	down(&kbase_dev_list_lock);
	list_add(&kbdev->entry, &kbase_dev_list);
	up(&kbase_dev_list_lock);
	dev_info(kbdev->dev, "Probed as %s\n", dev_name(kbdev->mdev.this_device));

	mali_err = kbase_pm_init(kbdev);
	if (MALI_ERROR_NONE != mali_err)
		goto out_partial;

	inited |= inited_pm;

	if (kbdev->pm.callback_power_runtime_init) {
		mali_err = kbdev->pm.callback_power_runtime_init(kbdev);
		if (MALI_ERROR_NONE != mali_err)
			goto out_partial;

		inited |= inited_pm_runtime_init;
	}

	mali_err = kbase_mem_init(kbdev);
	if (MALI_ERROR_NONE != mali_err)
		goto out_partial;

	inited |= inited_mem;

	mali_err = kbase_job_slot_init(kbdev);
	if (MALI_ERROR_NONE != mali_err)
		goto out_partial;

	inited |= inited_job_slot;

	mali_err = kbasep_js_devdata_init(kbdev);
	if (MALI_ERROR_NONE != mali_err)
		goto out_partial;

	inited |= inited_js;

	err = kbase_install_interrupts(kbdev);
	if (err)
		goto out_partial;

	inited |= inited_irqs;

#ifdef CONFIG_MALI_DEBUG_SHADER_SPLIT_FS
	if (device_create_file(kbdev->dev, &dev_attr_sc_split)) {
		dev_err(kbdev->dev, "Couldn't create sc_split sysfs file\n");
		goto out_partial;
	}

	inited |= inited_sc_split;
#endif /* CONFIG_MALI_DEBUG_SHADER_SPLIT_FS */

#ifdef CONFIG_DEBUG_FS
	if (kbasep_gpu_memory_debugfs_init(kbdev)) {
		dev_err(kbdev->dev, "Couldn't create gpu_memory debugfs file\n");
		goto out_partial;
	}
	inited |= inited_gpu_memory;
#endif /* CONFIG_DEBUG_FS */

#ifdef CONFIG_MALI_DEBUG

	if (device_create_file(kbdev->dev, &dev_attr_debug_command)) {
		dev_err(kbdev->dev, "Couldn't create debug_command sysfs file\n");
		goto out_partial;
	}
	inited |= inited_debug;

	if (device_create_file(kbdev->dev, &dev_attr_js_softstop_always)) {
		dev_err(kbdev->dev, "Couldn't create js_softstop_always sysfs file\n");
		goto out_partial;
	}
	inited |= inited_js_softstop;
#endif /* CONFIG_MALI_DEBUG */

#if !MALI_CUSTOMER_RELEASE
	if (device_create_file(kbdev->dev, &dev_attr_js_timeouts)) {
		dev_err(kbdev->dev, "Couldn't create js_timeouts sysfs file\n");
		goto out_partial;
	}
	inited |= inited_js_timeouts;

	if (device_create_file(kbdev->dev, &dev_attr_force_replay)) {
		dev_err(kbdev->dev, "Couldn't create force_replay sysfs file\n");
		goto out_partial;
	}
	inited |= inited_force_replay;
#endif /* !MALI_CUSTOMER_RELEASE */

#ifdef CONFIG_MALI_TRACE_TIMELINE
	if (kbasep_trace_timeline_debugfs_init(kbdev)) {
		dev_err(kbdev->dev, "Couldn't create mali_timeline_defs debugfs file\n");
		goto out_partial;
	}
	inited |= inited_timeline;
#endif /* CONFIG_MALI_TRACE_TIMELINE */

#ifdef CONFIG_MALI_DEVFREQ
	kbase_devfreq_init(kbdev);
#endif

	mali_err = kbase_pm_powerup(kbdev);
	if (MALI_ERROR_NONE == mali_err) {
		inited |= inited_pm_powerup;
#ifdef CONFIG_MALI_DEBUG
#if !defined(CONFIG_MALI_NO_MALI)
		if (MALI_ERROR_NONE != kbasep_common_test_interrupt_handlers(kbdev)) {
			dev_err(kbdev->dev, "Interrupt assigment check failed.\n");
			err = -EINVAL;
			goto out_partial;
		}
#endif /* CONFIG_MALI_NO_MALI */
#endif /* CONFIG_MALI_DEBUG */
		/* intialise the kctx list */
		mutex_init(&kbdev->kctx_list_lock);
		INIT_LIST_HEAD(&kbdev->kctx_list);
		return 0;
	} else {
		/* Failed to power up the GPU. */
		dev_err(kbdev->dev, "GPU power up failed.\n");
		err = -ENODEV;
	}

 out_partial:
#ifdef CONFIG_MALI_TRACE_TIMELINE
	if (inited & inited_timeline)
		kbasep_trace_timeline_debugfs_term(kbdev);
#endif /* CONFIG_MALI_TRACE_TIMELINE */
#if !MALI_CUSTOMER_RELEASE
	if (inited & inited_force_replay)
		device_remove_file(kbdev->dev, &dev_attr_force_replay);
	if (inited & inited_js_timeouts)
		device_remove_file(kbdev->dev, &dev_attr_js_timeouts);
#endif /* !MALI_CUSTOMER_RELEASE */
#ifdef CONFIG_MALI_DEBUG
	if (inited & inited_js_softstop)
		device_remove_file(kbdev->dev, &dev_attr_js_softstop_always);

	if (inited & inited_debug)
		device_remove_file(kbdev->dev, &dev_attr_debug_command);

#endif /* CONFIG_MALI_DEBUG */

#ifdef CONFIG_DEBUG_FS
	if (inited & inited_gpu_memory)
		kbasep_gpu_memory_debugfs_term(kbdev);
#endif /* CONFIG_DEBUG_FS */

#ifdef CONFIG_MALI_DEBUG_SHADER_SPLIT_FS
	if (inited & inited_sc_split)
		device_remove_file(kbdev->dev, &dev_attr_sc_split);
#endif /* CONFIG_MALI_DEBUG_SHADER_SPLIT_FS */

	if (inited & inited_js)
		kbasep_js_devdata_halt(kbdev);

	if (inited & inited_job_slot)
		kbase_job_slot_halt(kbdev);

	if (inited & inited_mem)
		kbase_mem_halt(kbdev);

	if (inited & inited_pm_powerup)
		kbase_pm_halt(kbdev);

	if (inited & inited_irqs)
		kbase_release_interrupts(kbdev);

	if (inited & inited_js)
		kbasep_js_devdata_term(kbdev);

	if (inited & inited_job_slot)
		kbase_job_slot_term(kbdev);

	if (inited & inited_mem)
		kbase_mem_term(kbdev);

	if (inited & inited_pm_runtime_init) {
		if (kbdev->pm.callback_power_runtime_term)
			kbdev->pm.callback_power_runtime_term(kbdev);
	}

	if (inited & inited_pm)
		kbase_pm_term(kbdev);

	down(&kbase_dev_list_lock);
	list_del(&kbdev->entry);
	up(&kbase_dev_list_lock);
#if KBASE_PM_EN
	device_remove_file(kbdev->dev, &dev_attr_core_mask);
 out_file_core_mask:
	device_remove_file(kbdev->dev, &dev_attr_core_availability_policy);
 out_file_core_availability_policy:
	device_remove_file(kbdev->dev, &dev_attr_power_policy);
 out_file:
#endif /*KBASE_PM_EN*/
	misc_deregister(&kbdev->mdev);
 out_misc:
	put_device(kbdev->dev);
	return err;
}


static int kbase_platform_device_probe(struct platform_device *pdev)
{
	struct kbase_device *kbdev;
	struct resource *reg_res;
	struct kbase_attribute *platform_data;
	int err;
	int i;
	struct mali_base_gpu_core_props *core_props;
#ifdef CONFIG_MALI_NO_MALI
	mali_error mali_err;
#endif /* CONFIG_MALI_NO_MALI */
#ifdef CONFIG_OF
	struct kbase_platform_config *config;
	int attribute_count;

	printk(KERN_INFO "arm_release_ver of this mali_ko is '%s', rk_ko_ver is '%d', built at '%s', on '%s'.",
           MALI_RELEASE_NAME,
           ROCKCHIP_VERSION,
           __TIME__,
           __DATE__);

	config = kbase_get_platform_config();
	attribute_count = kbasep_get_config_attribute_count(config->attributes);

	err = platform_device_add_data(pdev, config->attributes,
			attribute_count * sizeof(config->attributes[0]));
	if (err)
		return err;
#endif /* CONFIG_OF */

	kbdev = kbase_device_alloc();
	if (!kbdev) {
		dev_err(&pdev->dev, "Can't allocate device\n");
		err = -ENOMEM;
		goto out;
	}
#ifdef CONFIG_MALI_NO_MALI
	mali_err = midg_device_create(kbdev);
	if (MALI_ERROR_NONE != mali_err) {
		dev_err(&pdev->dev, "Can't initialize dummy model\n");
		err = -ENOMEM;
		goto out_midg;
	}
#endif /* CONFIG_MALI_NO_MALI */

	kbdev->dev = &pdev->dev;
	platform_data = (struct kbase_attribute *)kbdev->dev->platform_data;

	if (NULL == platform_data) {
		dev_err(kbdev->dev, "Platform data not specified\n");
		err = -ENOENT;
		goto out_free_dev;
	}

	if (MALI_TRUE != kbasep_validate_configuration_attributes(kbdev, platform_data)) {
		dev_err(kbdev->dev, "Configuration attributes failed to validate\n");
		err = -EINVAL;
		goto out_free_dev;
	}
	kbdev->config_attributes = platform_data;

	/* 3 IRQ resources */
	for (i = 0; i < 3; i++) {
		struct resource *irq_res;
		int irqtag;

		irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, i);
		if (!irq_res) {
			dev_err(kbdev->dev, "No IRQ resource at index %d\n", i);
			err = -ENOENT;
			goto out_free_dev;
		}

#ifdef CONFIG_OF
		if (!strcmp(irq_res->name, "JOB")) {
			irqtag = JOB_IRQ_TAG;
		} else if (!strcmp(irq_res->name, "MMU")) {
			irqtag = MMU_IRQ_TAG;
		} else if (!strcmp(irq_res->name, "GPU")) {
			irqtag = GPU_IRQ_TAG;
		} else {
			dev_err(&pdev->dev, "Invalid irq res name: '%s'\n",
				irq_res->name);
			err = -EINVAL;
			goto out_free_dev;
		}
#else
		irqtag = i;
#endif /* CONFIG_OF */
		kbdev->irqs[irqtag].irq = irq_res->start;
		kbdev->irqs[irqtag].flags = (irq_res->flags & IRQF_TRIGGER_MASK);
	}

	/* the first memory resource is the physical address of the GPU registers */
	reg_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!reg_res) {
		dev_err(kbdev->dev, "Invalid register resource\n");
		err = -ENOENT;
		goto out_free_dev;
	}

	kbdev->reg_start = reg_res->start;
	kbdev->reg_size = resource_size(reg_res);

	err = kbase_common_reg_map(kbdev);
	if (err)
		goto out_free_dev;

	kbdev->clock = clk_get(kbdev->dev, "clk_mali");
	if (IS_ERR_OR_NULL(kbdev->clock)) {
		dev_info(kbdev->dev, "Continuing without Mali clock control\n");
		kbdev->clock = NULL;
		/* Allow probe to continue without clock. */
	} else {
		err = clk_prepare_enable(kbdev->clock);
		if (err) {
			dev_err(kbdev->dev,
				"Failed to prepare and enable clock (%d)\n", err);
			goto out_clock_get;
		}
	}

#ifdef CONFIG_DEBUG_FS
	kbdev->mali_debugfs_directory = debugfs_create_dir("mali", NULL);
	if (NULL == kbdev->mali_debugfs_directory) {
		dev_err(kbdev->dev, "Couldn't create mali debugfs directory\n");
		goto out_clock_enable;
	}
	kbdev->memory_profile_directory = debugfs_create_dir("mem",
			kbdev->mali_debugfs_directory);
	if (NULL == kbdev->memory_profile_directory) {
		dev_err(kbdev->dev, "Couldn't create mali mem debugfs directory\n");
		goto out_mali_debugfs_remove;
	}
	if (kbasep_jd_debugfs_init(kbdev)) {
		dev_err(kbdev->dev, "Couldn't create mali jd debugfs entries\n");
		goto out_mem_profile_remove;
	}
#endif /* CONFIG_DEBUG_FS */


	if (MALI_ERROR_NONE != kbase_device_init(kbdev)) {
		dev_err(kbdev->dev, "Can't initialize device\n");

		err = -ENOMEM;
		goto out_debugfs_remove;
	}

	/* obtain min/max configured gpu frequencies */
	core_props = &(kbdev->gpu_props.props.core_props);
	core_props->gpu_freq_khz_min = GPU_FREQ_KHZ_MIN;
	core_props->gpu_freq_khz_max = GPU_FREQ_KHZ_MAX;
	kbdev->gpu_props.irq_throttle_time_us = DEFAULT_IRQ_THROTTLE_TIME_US;

	err = kbase_common_device_init(kbdev);
	if (err) {
		dev_err(kbdev->dev, "Failed kbase_common_device_init\n");
		goto out_term_dev;
	}
	return 0;

out_term_dev:
	kbase_device_term(kbdev);
out_debugfs_remove:
#ifdef CONFIG_DEBUG_FS
	kbasep_jd_debugfs_term(kbdev);
out_mem_profile_remove:
	debugfs_remove(kbdev->memory_profile_directory);
out_mali_debugfs_remove:
	debugfs_remove(kbdev->mali_debugfs_directory);
out_clock_enable:
#endif /* CONFIG_DEBUG_FS */
	clk_disable_unprepare(kbdev->clock);
out_clock_get:
	clk_put(kbdev->clock);
	kbase_common_reg_unmap(kbdev);
out_free_dev:
#ifdef CONFIG_MALI_NO_MALI
	midg_device_destroy(kbdev);
out_midg:
#endif /* CONFIG_MALI_NO_MALI */
	kbase_device_free(kbdev);
out:
	return err;
}

static int kbase_common_device_remove(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_DEVFREQ
	kbase_devfreq_term(kbdev);
#endif

	if (kbdev->pm.callback_power_runtime_term)
		kbdev->pm.callback_power_runtime_term(kbdev);
#if KBASE_PM_EN
	/* Remove the sys power policy file */
	device_remove_file(kbdev->dev, &dev_attr_power_policy);
	device_remove_file(kbdev->dev, &dev_attr_core_availability_policy);
	device_remove_file(kbdev->dev, &dev_attr_core_mask);
#endif
#ifdef CONFIG_MALI_TRACE_TIMELINE
	kbasep_trace_timeline_debugfs_term(kbdev);
#endif /* CONFIG_MALI_TRACE_TIMELINE */

#ifdef CONFIG_MALI_DEBUG
	device_remove_file(kbdev->dev, &dev_attr_js_softstop_always);
	device_remove_file(kbdev->dev, &dev_attr_debug_command);
#endif /* CONFIG_MALI_DEBUG */
#if !MALI_CUSTOMER_RELEASE
	device_remove_file(kbdev->dev, &dev_attr_js_timeouts);
	device_remove_file(kbdev->dev, &dev_attr_force_replay);
#endif /* !MALI_CUSTOMER_RELEASE */
#ifdef CONFIG_DEBUG_FS
	kbasep_gpu_memory_debugfs_term(kbdev);
#endif

#ifdef CONFIG_MALI_DEBUG_SHADER_SPLIT_FS
	device_remove_file(kbdev->dev, &dev_attr_sc_split);
#endif /* CONFIG_MALI_DEBUG_SHADER_SPLIT_FS */

	kbasep_js_devdata_halt(kbdev);
	kbase_job_slot_halt(kbdev);
	kbase_mem_halt(kbdev);
	kbase_pm_halt(kbdev);

	kbase_release_interrupts(kbdev);

	kbasep_js_devdata_term(kbdev);
	kbase_job_slot_term(kbdev);
	kbase_mem_term(kbdev);
	kbase_pm_term(kbdev);

	down(&kbase_dev_list_lock);
	list_del(&kbdev->entry);
	up(&kbase_dev_list_lock);

	misc_deregister(&kbdev->mdev);
	put_device(kbdev->dev);
	kbase_common_reg_unmap(kbdev);
	kbase_device_term(kbdev);
#ifdef CONFIG_DEBUG_FS
	kbasep_jd_debugfs_term(kbdev);
	debugfs_remove(kbdev->memory_profile_directory);
	debugfs_remove(kbdev->mali_debugfs_directory);
#endif /* CONFIG_DEBUG_FS */
	if (kbdev->clock) {
		clk_disable_unprepare(kbdev->clock);
		clk_put(kbdev->clock);
		kbdev->clock = NULL;
	}
#ifdef CONFIG_MALI_NO_MALI
	midg_device_destroy(kbdev);
#endif /* CONFIG_MALI_NO_MALI */
	kbase_device_free(kbdev);

	return 0;
}

static int kbase_platform_device_remove(struct platform_device *pdev)
{
	struct kbase_device *kbdev = to_kbase_device(&pdev->dev);

	if (!kbdev)
		return -ENODEV;

	return kbase_common_device_remove(kbdev);
}

/** Suspend callback from the OS.
 *
 * This is called by Linux when the device should suspend.
 *
 * @param dev  The device to suspend
 *
 * @return A standard Linux error code
 */
static int kbase_device_suspend(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

#if defined(CONFIG_PM_DEVFREQ) && \
		(LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	devfreq_suspend_device(kbdev->devfreq);
#endif

	kbase_pm_suspend(kbdev);
	return 0;
}

/** Resume callback from the OS.
 *
 * This is called by Linux when the device should resume from suspension.
 *
 * @param dev  The device to resume
 *
 * @return A standard Linux error code
 */
static int kbase_device_resume(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	kbase_pm_resume(kbdev);

#if defined(CONFIG_PM_DEVFREQ) && \
		(LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	devfreq_resume_device(kbdev->devfreq);
#endif
	return 0;
}

/** Runtime suspend callback from the OS.
 *
 * This is called by Linux when the device should prepare for a condition in which it will
 * not be able to communicate with the CPU(s) and RAM due to power management.
 *
 * @param dev  The device to suspend
 *
 * @return A standard Linux error code
 */
#ifdef CONFIG_PM_RUNTIME
static int kbase_device_runtime_suspend(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

#if defined(CONFIG_PM_DEVFREQ) && \
		(LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	devfreq_suspend_device(kbdev->devfreq);
#endif

	if (kbdev->pm.callback_power_runtime_off) {
		kbdev->pm.callback_power_runtime_off(kbdev);
		dev_dbg(dev, "runtime suspend\n");
	}
	return 0;
}
#endif /* CONFIG_PM_RUNTIME */

/** Runtime resume callback from the OS.
 *
 * This is called by Linux when the device should go into a fully active state.
 *
 * @param dev  The device to suspend
 *
 * @return A standard Linux error code
 */

#ifdef CONFIG_PM_RUNTIME
int kbase_device_runtime_resume(struct device *dev)
{
	int ret = 0;
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	if (kbdev->pm.callback_power_runtime_on) {
		ret = kbdev->pm.callback_power_runtime_on(kbdev);
		dev_dbg(dev, "runtime resume\n");
	}

#if defined(CONFIG_PM_DEVFREQ) && \
		(LINUX_VERSION_CODE >= KERNEL_VERSION(3, 8, 0))
	devfreq_resume_device(kbdev->devfreq);
#endif

	return ret;
}
#endif /* CONFIG_PM_RUNTIME */

/** Runtime idle callback from the OS.
 *
 * This is called by Linux when the device appears to be inactive and it might be
 * placed into a low power state
 *
 * @param dev  The device to suspend
 *
 * @return A standard Linux error code
 */

#ifdef CONFIG_PM_RUNTIME
static int kbase_device_runtime_idle(struct device *dev)
{
	/* Avoid pm_runtime_suspend being called */
	return 1;
}
#endif /* CONFIG_PM_RUNTIME */

/** The power management operations for the platform driver.
 */
static const struct dev_pm_ops kbase_pm_ops = {
	.suspend = kbase_device_suspend,
	.resume = kbase_device_resume,
#ifdef CONFIG_PM_RUNTIME
	.runtime_suspend = kbase_device_runtime_suspend,
	.runtime_resume = kbase_device_runtime_resume,
	.runtime_idle = kbase_device_runtime_idle,
#endif /* CONFIG_PM_RUNTIME */
};

#ifdef CONFIG_OF
static const struct of_device_id kbase_dt_ids[] = {
	{ .compatible = "arm,malit7xx" },
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
#if 0
module_platform_driver(kbase_platform_driver);
#else 
static int __init rockchip_gpu_init_driver(void)
{
	return platform_driver_register(&kbase_platform_driver);
}

late_initcall(rockchip_gpu_init_driver);
#endif
#else
#ifdef CONFIG_MALI_PLATFORM_FAKE
extern int kbase_platform_fake_register(void);
extern void kbase_platform_fake_unregister(void);
#endif

static int __init kbase_driver_init(void)
{
	int ret;

	ret = kbase_platform_early_init();
	if (ret)
		return ret;

#ifdef CONFIG_MALI_PLATFORM_FAKE
	ret = kbase_platform_fake_register();
	if (ret)
		return ret;
#endif
	ret = platform_driver_register(&kbase_platform_driver);
#ifdef CONFIG_MALI_PLATFORM_FAKE
	if (ret)
		kbase_platform_fake_unregister();
#endif

	return ret;
}

static void __exit kbase_driver_exit(void)
{
	platform_driver_unregister(&kbase_platform_driver);
#ifdef CONFIG_MALI_PLATFORM_FAKE
	kbase_platform_fake_unregister();
#endif
}

module_init(kbase_driver_init);
module_exit(kbase_driver_exit);

#endif /* CONFIG_OF */

MODULE_LICENSE("GPL");
MODULE_VERSION(MALI_RELEASE_NAME);

#if defined(CONFIG_MALI_GATOR_SUPPORT) || defined(CONFIG_MALI_SYSTEM_TRACE)
#define CREATE_TRACE_POINTS
#endif

#ifdef CONFIG_MALI_GATOR_SUPPORT
/* Create the trace points (otherwise we just get code to call a tracepoint) */
#include "mali_linux_trace.h"

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
#endif /* CONFIG_MALI_GATOR_SUPPORT */
#ifdef CONFIG_MALI_SYSTEM_TRACE
#include "mali_linux_kbase_trace.h"
#endif
