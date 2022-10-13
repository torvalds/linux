// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2010-2022 ARM Limited. All rights reserved.
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

#include <mali_kbase.h>
#include <mali_kbase_config_defaults.h>
#include <gpu/mali_kbase_gpu_regmap.h>
#include <mali_kbase_gator.h>
#include <mali_kbase_mem_linux.h>
#ifdef CONFIG_MALI_BIFROST_DEVFREQ
#include <linux/devfreq.h>
#include <backend/gpu/mali_kbase_devfreq.h>
#if IS_ENABLED(CONFIG_DEVFREQ_THERMAL)
#include <ipa/mali_kbase_ipa_debugfs.h>
#endif /* CONFIG_DEVFREQ_THERMAL */
#endif /* CONFIG_MALI_BIFROST_DEVFREQ */
#if IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
#include "backend/gpu/mali_kbase_model_linux.h"
#include <backend/gpu/mali_kbase_model_dummy.h>
#endif /* CONFIG_MALI_BIFROST_NO_MALI */
#include "uapi/gpu/arm/bifrost/mali_kbase_mem_profile_debugfs_buf_size.h"
#include "mali_kbase_mem.h"
#include "mali_kbase_mem_pool_debugfs.h"
#include "mali_kbase_mem_pool_group.h"
#include "mali_kbase_debugfs_helper.h"
#include "mali_kbase_regs_history_debugfs.h"
#include <mali_kbase_hwaccess_backend.h>
#include <mali_kbase_hwaccess_time.h>
#if !MALI_USE_CSF
#include <mali_kbase_hwaccess_jm.h>
#endif /* !MALI_USE_CSF */
#ifdef CONFIG_MALI_PRFCNT_SET_SELECT_VIA_DEBUG_FS
#include <mali_kbase_hwaccess_instr.h>
#endif
#include <mali_kbase_reset_gpu.h>
#include <uapi/gpu/arm/bifrost/mali_kbase_ioctl.h>
#if !MALI_USE_CSF
#include "mali_kbase_kinstr_jm.h"
#endif
#include "hwcnt/mali_kbase_hwcnt_context.h"
#include "hwcnt/mali_kbase_hwcnt_virtualizer.h"
#include "mali_kbase_kinstr_prfcnt.h"
#include "mali_kbase_vinstr.h"
#if MALI_USE_CSF
#include "csf/mali_kbase_csf_firmware.h"
#include "csf/mali_kbase_csf_tiler_heap.h"
#include "csf/mali_kbase_csf_csg_debugfs.h"
#include "csf/mali_kbase_csf_cpu_queue_debugfs.h"
#include "csf/mali_kbase_csf_event.h"
#endif
#ifdef CONFIG_MALI_ARBITER_SUPPORT
#include "arbiter/mali_kbase_arbiter_pm.h"
#endif

#include "mali_kbase_cs_experimental.h"

#ifdef CONFIG_MALI_CINSTR_GWT
#include "mali_kbase_gwt.h"
#endif
#include "backend/gpu/mali_kbase_pm_internal.h"
#include "mali_kbase_dvfs_debugfs.h"
#if IS_ENABLED(CONFIG_DEBUG_FS)
#include "mali_kbase_pbha_debugfs.h"
#endif

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/platform_device.h>
#include <linux/of_platform.h>
#include <linux/miscdevice.h>
#include <linux/list.h>
#include <linux/semaphore.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/mm.h>
#include <linux/compat.h>	/* is_compat_task/in_compat_syscall */
#include <linux/mman.h>
#include <linux/version.h>
#include <linux/version_compat_defs.h>
#include <mali_kbase_hw.h>
#if IS_ENABLED(CONFIG_SYNC_FILE)
#include <mali_kbase_sync.h>
#endif /* CONFIG_SYNC_FILE */
#include <linux/clk.h>
#include <linux/clk-provider.h>
#include <linux/delay.h>
#include <linux/log2.h>

#include <mali_kbase_config.h>

#include <linux/pm_opp.h>
#include <soc/rockchip/rockchip_opp_select.h>
#include <linux/pm_runtime.h>

#include <tl/mali_kbase_timeline.h>

#include <mali_kbase_as_fault_debugfs.h>
#include <device/mali_kbase_device.h>
#include <context/mali_kbase_context.h>

#include <mali_kbase_caps.h>

#define KERNEL_SIDE_DDK_VERSION_STRING "K:" MALI_RELEASE_NAME "(GPL)"

/**
 * KBASE_API_VERSION - KBase API Version
 * @major: Kernel major version
 * @minor: Kernel minor version
 */
#define KBASE_API_VERSION(major, minor) ((((major) & 0xFFF) << 20)  | \
					 (((minor) & 0xFFF) << 8) | \
					 ((0 & 0xFF) << 0))

/**
 * struct mali_kbase_capability_def - kbase capabilities table
 *
 * @required_major: required major
 * @required_minor: required minor
 */
struct mali_kbase_capability_def {
	u16 required_major;
	u16 required_minor;
};

/*
 * This must be kept in-sync with mali_kbase_cap
 *
 * TODO: The alternative approach would be to embed the cap enum values
 * in the table. Less efficient but potentially safer.
 */
static const struct mali_kbase_capability_def kbase_caps_table[MALI_KBASE_NUM_CAPS] = {
#if MALI_USE_CSF
	{ 1, 0 },               /* SYSTEM_MONITOR */
	{ 1, 0 },               /* JIT_PRESSURE_LIMIT */
	{ 1, 0 },               /* MEM_GROW_ON_GPF */
	{ 1, 0 }                /* MEM_PROTECTED */
#else
	{ 11, 15 },             /* SYSTEM_MONITOR */
	{ 11, 25 },             /* JIT_PRESSURE_LIMIT */
	{ 11,  2 },             /* MEM_GROW_ON_GPF */
	{ 11,  2 }              /* MEM_PROTECTED */
#endif
};

#if (KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE)
/* Mutex to synchronize the probe of multiple kbase instances */
static struct mutex kbase_probe_mutex;
#endif

/**
 * mali_kbase_supports_cap - Query whether a kbase capability is supported
 *
 * @api_version: API version to convert
 * @cap:         Capability to query for - see mali_kbase_caps.h
 *
 * Return: true if the capability is supported
 */
bool mali_kbase_supports_cap(unsigned long api_version, enum mali_kbase_cap cap)
{
	bool supported = false;
	unsigned long required_ver;

	struct mali_kbase_capability_def const *cap_def;

	if (WARN_ON(cap < 0))
		return false;

	if (WARN_ON(cap >= MALI_KBASE_NUM_CAPS))
		return false;

	cap_def = &kbase_caps_table[(int)cap];
	required_ver = KBASE_API_VERSION(cap_def->required_major, cap_def->required_minor);
	supported = (api_version >= required_ver);

	return supported;
}

/**
 * kbase_file_new - Create an object representing a device file
 *
 * @kbdev:  An instance of the GPU platform device, allocated from the probe
 *          method of the driver.
 * @filp:   Pointer to the struct file corresponding to device file
 *          /dev/malixx instance, passed to the file's open method.
 *
 * In its initial state, the device file has no context (i.e. no GPU
 * address space) and no API version number. Both must be assigned before
 * kbase_file_get_kctx_if_setup_complete() can be used successfully.
 *
 * Return: Address of an object representing a simulated device file, or NULL
 *         on failure.
 */
static struct kbase_file *kbase_file_new(struct kbase_device *const kbdev,
	struct file *const filp)
{
	struct kbase_file *const kfile = kmalloc(sizeof(*kfile), GFP_KERNEL);

	if (kfile) {
		kfile->kbdev = kbdev;
		kfile->filp = filp;
		kfile->kctx = NULL;
		kfile->api_version = 0;
		atomic_set(&kfile->setup_state, KBASE_FILE_NEED_VSN);
	}
	return kfile;
}

/**
 * kbase_file_set_api_version - Set the application programmer interface version
 *
 * @kfile:  A device file created by kbase_file_new()
 * @major:  Major version number (must not exceed 12 bits)
 * @minor:  Major version number (must not exceed 12 bits)
 *
 * An application programmer interface (API) version must be specified
 * before calling kbase_file_create_kctx(), otherwise an error is returned.
 *
 * If a version number was already set for the given @kfile (or is in the
 * process of being set by another thread) then an error is returned.
 *
 * Return: 0 if successful, otherwise a negative error code.
 */
static int kbase_file_set_api_version(struct kbase_file *const kfile,
	u16 const major, u16 const minor)
{
	if (WARN_ON(!kfile))
		return -EINVAL;

	/* setup pending, try to signal that we'll do the setup,
	 * if setup was already in progress, err this call
	 */
	if (atomic_cmpxchg(&kfile->setup_state, KBASE_FILE_NEED_VSN,
		KBASE_FILE_VSN_IN_PROGRESS) != KBASE_FILE_NEED_VSN)
		return -EPERM;

	/* save the proposed version number for later use */
	kfile->api_version = KBASE_API_VERSION(major, minor);

	atomic_set(&kfile->setup_state, KBASE_FILE_NEED_CTX);
	return 0;
}

/**
 * kbase_file_get_api_version - Get the application programmer interface version
 *
 * @kfile:  A device file created by kbase_file_new()
 *
 * Return: The version number (encoded with KBASE_API_VERSION) or 0 if none has
 *         been set.
 */
static unsigned long kbase_file_get_api_version(struct kbase_file *const kfile)
{
	if (WARN_ON(!kfile))
		return 0;

	if (atomic_read(&kfile->setup_state) < KBASE_FILE_NEED_CTX)
		return 0;

	return kfile->api_version;
}

/**
 * kbase_file_create_kctx - Create a kernel base context
 *
 * @kfile:  A device file created by kbase_file_new()
 * @flags:  Flags to set, which can be any combination of
 *          BASEP_CONTEXT_CREATE_KERNEL_FLAGS.
 *
 * This creates a new context for the GPU platform device instance that was
 * specified when kbase_file_new() was called. Each context has its own GPU
 * address space. If a context was already created for the given @kfile (or is
 * in the process of being created for it by another thread) then an error is
 * returned.
 *
 * An API version number must have been set by kbase_file_set_api_version()
 * before calling this function, otherwise an error is returned.
 *
 * Return: 0 if a new context was created, otherwise a negative error code.
 */
static int kbase_file_create_kctx(struct kbase_file *kfile,
	base_context_create_flags flags);

/**
 * kbase_file_get_kctx_if_setup_complete - Get a kernel base context
 *                                         pointer from a device file
 *
 * @kfile: A device file created by kbase_file_new()
 *
 * This function returns NULL if no context has been created for the given @kfile.
 * This makes it safe to use in circumstances where the order of initialization
 * cannot be enforced, but only if the caller checks the return value.
 *
 * Return: Address of the kernel base context associated with the @kfile, or
 *         NULL if no context exists.
 */
static struct kbase_context *kbase_file_get_kctx_if_setup_complete(
	struct kbase_file *const kfile)
{
	if (WARN_ON(!kfile) ||
		atomic_read(&kfile->setup_state) != KBASE_FILE_COMPLETE ||
		WARN_ON(!kfile->kctx))
		return NULL;

	return kfile->kctx;
}

/**
 * kbase_file_delete - Destroy an object representing a device file
 *
 * @kfile: A device file created by kbase_file_new()
 *
 * If any context was created for the @kfile then it is destroyed.
 */
static void kbase_file_delete(struct kbase_file *const kfile)
{
	struct kbase_device *kbdev = NULL;

	if (WARN_ON(!kfile))
		return;

	kfile->filp->private_data = NULL;
	kbdev = kfile->kbdev;

	if (atomic_read(&kfile->setup_state) == KBASE_FILE_COMPLETE) {
		struct kbase_context *kctx = kfile->kctx;

#if IS_ENABLED(CONFIG_DEBUG_FS)
		kbasep_mem_profile_debugfs_remove(kctx);
#endif
		kbase_context_debugfs_term(kctx);

		kbase_destroy_context(kctx);

		dev_dbg(kbdev->dev, "deleted base context\n");
	}

	kbase_release_device(kbdev);

	kfree(kfile);
}

static int kbase_api_handshake(struct kbase_file *kfile,
			       struct kbase_ioctl_version_check *version)
{
	int err = 0;

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
		 * version
		 */
		version->major = BASE_UK_VERSION_MAJOR;
		version->minor = BASE_UK_VERSION_MINOR;
		break;
	}

	/* save the proposed version number for later use */
	err = kbase_file_set_api_version(kfile, version->major, version->minor);
	if (unlikely(err))
		return err;

	/* For backward compatibility, we may need to create the context before
	 * the flags have been set. Originally it was created on file open
	 * (with job submission disabled) but we don't support that usage.
	 */
	if (!mali_kbase_supports_system_monitor(kbase_file_get_api_version(kfile)))
		err = kbase_file_create_kctx(kfile,
			BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED);

	return err;
}

static int kbase_api_handshake_dummy(struct kbase_file *kfile,
		struct kbase_ioctl_version_check *version)
{
	return -EPERM;
}

static int kbase_api_kinstr_prfcnt_enum_info(
	struct kbase_file *kfile,
	struct kbase_ioctl_kinstr_prfcnt_enum_info *prfcnt_enum_info)
{
	return kbase_kinstr_prfcnt_enum_info(kfile->kbdev->kinstr_prfcnt_ctx,
					     prfcnt_enum_info);
}

static int kbase_api_kinstr_prfcnt_setup(
	struct kbase_file *kfile,
	union kbase_ioctl_kinstr_prfcnt_setup *prfcnt_setup)
{
	return kbase_kinstr_prfcnt_setup(kfile->kbdev->kinstr_prfcnt_ctx,
					 prfcnt_setup);
}

static struct kbase_device *to_kbase_device(struct device *dev)
{
	return dev_get_drvdata(dev);
}

int assign_irqs(struct kbase_device *kbdev)
{
	static const char *const irq_names_caps[] = { "JOB", "MMU", "GPU" };

#if IS_ENABLED(CONFIG_OF)
	static const char *const irq_names[] = { "job", "mmu", "gpu" };
#endif

	struct platform_device *pdev;
	int i;

	if (!kbdev)
		return -ENODEV;

	pdev = to_platform_device(kbdev->dev);

	for (i = 0; i < ARRAY_SIZE(irq_names_caps); i++) {
		int irq;

#if IS_ENABLED(CONFIG_OF)
		/* We recommend using Upper case for the irq names in dts, but if
		 * there are devices in the world using Lower case then we should
		 * avoid breaking support for them. So try using names in Upper case
		 * first then try using Lower case names. If both attempts fail then
		 * we assume there is no IRQ resource specified for the GPU.
		 */
		irq = platform_get_irq_byname(pdev, irq_names_caps[i]);
		if (irq < 0)
			irq = platform_get_irq_byname(pdev, irq_names[i]);
#else
		irq = platform_get_irq(pdev, i);
#endif /* CONFIG_OF */

		if (irq < 0) {
			dev_err(kbdev->dev, "No IRQ resource '%s'\n", irq_names_caps[i]);
			return irq;
		}

		kbdev->irqs[i].irq = irq;
		kbdev->irqs[i].flags = irqd_get_trigger_type(irq_get_irq_data(irq));
	}

	return 0;
}

/* Find a particular kbase device (as specified by minor number), or find the "first" device if -1 is specified */
struct kbase_device *kbase_find_device(int minor)
{
	struct kbase_device *kbdev = NULL;
	struct list_head *entry;
	const struct list_head *dev_list = kbase_device_get_list();

	list_for_each(entry, dev_list) {
		struct kbase_device *tmp;

		tmp = list_entry(entry, struct kbase_device, entry);
		if (tmp->mdev.minor == minor || minor == -1) {
			kbdev = tmp;
			get_device(kbdev->dev);
			break;
		}
	}
	kbase_device_put_list(dev_list);

	return kbdev;
}
EXPORT_SYMBOL(kbase_find_device);

void kbase_release_device(struct kbase_device *kbdev)
{
	put_device(kbdev->dev);
}
EXPORT_SYMBOL(kbase_release_device);

#if IS_ENABLED(CONFIG_DEBUG_FS)
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
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = write_ctx_infinite_cache,
	.read = read_ctx_infinite_cache,
};

static ssize_t write_ctx_force_same_va(struct file *f, const char __user *ubuf,
		size_t size, loff_t *off)
{
	struct kbase_context *kctx = f->private_data;
	int err;
	bool value;

	err = kstrtobool_from_user(ubuf, size, &value);
	if (err)
		return err;

	if (value) {
#if defined(CONFIG_64BIT)
		/* 32-bit clients cannot force SAME_VA */
		if (kbase_ctx_flag(kctx, KCTX_COMPAT))
			return -EINVAL;
		kbase_ctx_flag_set(kctx, KCTX_FORCE_SAME_VA);
#else /* defined(CONFIG_64BIT) */
		/* 32-bit clients cannot force SAME_VA */
		return -EINVAL;
#endif /* defined(CONFIG_64BIT) */
	} else {
		kbase_ctx_flag_clear(kctx, KCTX_FORCE_SAME_VA);
	}

	return size;
}

static ssize_t read_ctx_force_same_va(struct file *f, char __user *ubuf,
		size_t size, loff_t *off)
{
	struct kbase_context *kctx = f->private_data;
	char buf[32];
	int count;
	bool value;

	value = kbase_ctx_flag(kctx, KCTX_FORCE_SAME_VA);

	count = scnprintf(buf, sizeof(buf), "%s\n", value ? "Y" : "N");

	return simple_read_from_buffer(ubuf, size, off, buf, count);
}

static const struct file_operations kbase_force_same_va_fops = {
	.owner = THIS_MODULE,
	.open = simple_open,
	.write = write_ctx_force_same_va,
	.read = read_ctx_force_same_va,
};
#endif /* CONFIG_DEBUG_FS */

static int kbase_file_create_kctx(struct kbase_file *const kfile,
	base_context_create_flags const flags)
{
	struct kbase_device *kbdev = NULL;
	struct kbase_context *kctx = NULL;
#if IS_ENABLED(CONFIG_DEBUG_FS)
	char kctx_name[64];
#endif

	if (WARN_ON(!kfile))
		return -EINVAL;

	/* setup pending, try to signal that we'll do the setup,
	 * if setup was already in progress, err this call
	 */
	if (atomic_cmpxchg(&kfile->setup_state, KBASE_FILE_NEED_CTX,
		KBASE_FILE_CTX_IN_PROGRESS) != KBASE_FILE_NEED_CTX)
		return -EPERM;

	kbdev = kfile->kbdev;

	kctx = kbase_create_context(kbdev, in_compat_syscall(),
		flags, kfile->api_version, kfile->filp);

	/* if bad flags, will stay stuck in setup mode */
	if (!kctx)
		return -ENOMEM;

	if (kbdev->infinite_cache_active_default)
		kbase_ctx_flag_set(kctx, KCTX_INFINITE_CACHE);

#if IS_ENABLED(CONFIG_DEBUG_FS)
	snprintf(kctx_name, 64, "%d_%d", kctx->tgid, kctx->id);

	mutex_init(&kctx->mem_profile_lock);

	kctx->kctx_dentry = debugfs_create_dir(kctx_name,
			kbdev->debugfs_ctx_directory);

	if (IS_ERR_OR_NULL(kctx->kctx_dentry)) {
		/* we don't treat this as a fail - just warn about it */
		dev_warn(kbdev->dev, "couldn't create debugfs dir for kctx\n");
	} else {
		debugfs_create_file("infinite_cache", 0644, kctx->kctx_dentry,
			kctx, &kbase_infinite_cache_fops);
		debugfs_create_file("force_same_va", 0600, kctx->kctx_dentry,
			kctx, &kbase_force_same_va_fops);

		kbase_context_debugfs_init(kctx);
	}
#endif /* CONFIG_DEBUG_FS */

	dev_dbg(kbdev->dev, "created base context\n");

	kfile->kctx = kctx;
	atomic_set(&kfile->setup_state, KBASE_FILE_COMPLETE);

	return 0;
}

static int kbase_open(struct inode *inode, struct file *filp)
{
	struct kbase_device *kbdev = NULL;
	struct kbase_file *kfile;
	int ret = 0;

	kbdev = kbase_find_device(iminor(inode));

	if (!kbdev)
		return -ENODEV;

	/* Set address space operation for page migration */
	kbase_mem_migrate_set_address_space_ops(kbdev, filp);

	/* Device-wide firmware load is moved here from probing to comply with
	 * Android GKI vendor guideline.
	 */
	ret = kbase_device_firmware_init_once(kbdev);
	if (ret)
		goto out;

	kfile = kbase_file_new(kbdev, filp);
	if (!kfile) {
		ret = -ENOMEM;
		goto out;
	}

	filp->private_data = kfile;
	filp->f_mode |= FMODE_UNSIGNED_OFFSET;

	return 0;

out:
	kbase_release_device(kbdev);
	return ret;
}

static int kbase_release(struct inode *inode, struct file *filp)
{
	struct kbase_file *const kfile = filp->private_data;

	kbase_file_delete(kfile);
	return 0;
}

static int kbase_api_set_flags(struct kbase_file *kfile,
		struct kbase_ioctl_set_flags *flags)
{
	int err = 0;
	unsigned long const api_version = kbase_file_get_api_version(kfile);
	struct kbase_context *kctx = NULL;

	/* Validate flags */
	if (flags->create_flags !=
		(flags->create_flags & BASEP_CONTEXT_CREATE_KERNEL_FLAGS))
		return -EINVAL;

	/* For backward compatibility, the context may have been created before
	 * the flags were set.
	 */
	if (mali_kbase_supports_system_monitor(api_version)) {
		err = kbase_file_create_kctx(kfile, flags->create_flags);
	} else {
#if !MALI_USE_CSF
		struct kbasep_js_kctx_info *js_kctx_info = NULL;
		unsigned long irq_flags = 0;
#endif

		/* If setup is incomplete (e.g. because the API version
		 * wasn't set) then we have to give up.
		 */
		kctx = kbase_file_get_kctx_if_setup_complete(kfile);
		if (unlikely(!kctx))
			return -EPERM;

#if MALI_USE_CSF
		/* On CSF GPUs Job Manager interface isn't used to submit jobs
		 * (there are no job slots). So the legacy job manager path to
		 * submit jobs needs to remain disabled for CSF GPUs.
		 */
#else
		js_kctx_info = &kctx->jctx.sched_info;
		mutex_lock(&js_kctx_info->ctx.jsctx_mutex);
		spin_lock_irqsave(&kctx->kbdev->hwaccess_lock, irq_flags);
		/* Translate the flags */
		if ((flags->create_flags &
			BASE_CONTEXT_SYSTEM_MONITOR_SUBMIT_DISABLED) == 0)
			kbase_ctx_flag_clear(kctx, KCTX_SUBMIT_DISABLED);


		spin_unlock_irqrestore(&kctx->kbdev->hwaccess_lock, irq_flags);
		mutex_unlock(&js_kctx_info->ctx.jsctx_mutex);
#endif
	}

	return err;
}

#if !MALI_USE_CSF
static int kbase_api_job_submit(struct kbase_context *kctx,
		struct kbase_ioctl_job_submit *submit)
{
	return kbase_jd_submit(kctx, u64_to_user_ptr(submit->addr),
			submit->nr_atoms,
			submit->stride, false);
}
#endif /* !MALI_USE_CSF */

static int kbase_api_get_gpuprops(struct kbase_file *kfile,
		struct kbase_ioctl_get_gpuprops *get_props)
{
	struct kbase_gpu_props *kprops = &kfile->kbdev->gpu_props;
	int err;

	if (get_props->flags != 0) {
		dev_err(kfile->kbdev->dev, "Unsupported flags to get_gpuprops");
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

#if !MALI_USE_CSF
static int kbase_api_post_term(struct kbase_context *kctx)
{
	kbase_event_close(kctx);
	return 0;
}
#endif /* !MALI_USE_CSF */

#if MALI_USE_CSF
static int kbase_api_mem_alloc_ex(struct kbase_context *kctx,
				  union kbase_ioctl_mem_alloc_ex *alloc_ex)
{
	struct kbase_va_region *reg;
	u64 flags = alloc_ex->in.flags;
	u64 gpu_va;

	/* Calls to this function are inherently asynchronous, with respect to
	 * MMU operations.
	 */
	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;

	bool gpu_executable = (flags & BASE_MEM_PROT_GPU_EX) && kbase_has_exec_va_zone(kctx);
	bool fixed_or_fixable = (flags & (BASE_MEM_FIXED | BASE_MEM_FIXABLE));

	if (!kbase_mem_allow_alloc(kctx))
		return -EINVAL;

	/* The driver counts the number of FIXABLE and FIXED allocations because
	 * they're not supposed to happen at the same time. However, that is not
	 * a security concern: nothing bad happens if the two types of allocations
	 * are made at the same time. The only reason why the driver is guarding
	 * against them is because there's no client use case that is supposed
	 * to need both of them at the same time, and the driver wants to help
	 * the user space catch some obvious mistake.
	 *
	 * The driver is able to switch from FIXABLE allocations to FIXED and
	 * vice versa, if all the allocations of one kind are freed before trying
	 * to create allocations of a different kind.
	 */
	if ((flags & BASE_MEM_FIXED) && (atomic64_read(&kctx->num_fixable_allocs) > 0))
		return -EINVAL;

	if ((flags & BASE_MEM_FIXABLE) && (atomic64_read(&kctx->num_fixed_allocs) > 0))
		return -EINVAL;

	if (flags & BASEP_MEM_FLAGS_KERNEL_ONLY)
		return -ENOMEM;

	/* The fixed_address parameter must be either a non-zero, page-aligned
	 * value for FIXED allocations or zero for any other kind of allocation.
	 */
	if (flags & BASE_MEM_FIXED) {
		u64 aligned_fixed_address = alloc_ex->in.fixed_address & PAGE_MASK;

		if ((aligned_fixed_address == 0) ||
		    (aligned_fixed_address != alloc_ex->in.fixed_address))
			return -EINVAL;

		gpu_va = aligned_fixed_address;
	} else if (alloc_ex->in.fixed_address != 0) {
		return -EINVAL;
	}

	/* For 64-bit clients, force SAME_VA up to 2^(47)-1.
	 * For 32-bit clients, force SAME_VA up to 2^(32)-1.
	 *
	 * In both cases, the executable and fixed/fixable zones, and
	 * the executable+fixed/fixable zone, are all above this range.
	 */
	if ((!kbase_ctx_flag(kctx, KCTX_COMPAT)) &&
			kbase_ctx_flag(kctx, KCTX_FORCE_SAME_VA)) {
		if (!gpu_executable && !fixed_or_fixable)
			flags |= BASE_MEM_SAME_VA;
	}

	/* If CSF event memory allocation, need to force certain flags.
	 * SAME_VA - GPU address needs to be used as a CPU address, explicit
	 * mmap has to be avoided.
	 * CACHED_CPU - Frequent access to the event memory by CPU.
	 * COHERENT_SYSTEM - No explicit cache maintenance around the access
	 * to event memory so need to leverage the coherency support.
	 */
	if (flags & BASE_MEM_CSF_EVENT) {
		/* We cannot honor this request */
		if (gpu_executable || fixed_or_fixable)
			return -ENOMEM;

		flags |= (BASE_MEM_SAME_VA |
			  BASE_MEM_CACHED_CPU |
			  BASE_MEM_COHERENT_SYSTEM);
	}

	reg = kbase_mem_alloc(kctx, alloc_ex->in.va_pages, alloc_ex->in.commit_pages,
			      alloc_ex->in.extension, &flags, &gpu_va, mmu_sync_info);

	if (!reg)
		return -ENOMEM;

	alloc_ex->out.flags = flags;
	alloc_ex->out.gpu_va = gpu_va;

	return 0;
}

static int kbase_api_mem_alloc(struct kbase_context *kctx, union kbase_ioctl_mem_alloc *alloc)
{
	int ret;
	union kbase_ioctl_mem_alloc_ex mem_alloc_ex = { { 0 } };

	mem_alloc_ex.in.va_pages = alloc->in.va_pages;
	mem_alloc_ex.in.commit_pages = alloc->in.commit_pages;
	mem_alloc_ex.in.extension = alloc->in.extension;
	mem_alloc_ex.in.flags = alloc->in.flags;
	mem_alloc_ex.in.fixed_address = 0;

	ret = kbase_api_mem_alloc_ex(kctx, &mem_alloc_ex);

	alloc->out.flags = mem_alloc_ex.out.flags;
	alloc->out.gpu_va = mem_alloc_ex.out.gpu_va;

	return ret;
}
#else
static int kbase_api_mem_alloc(struct kbase_context *kctx, union kbase_ioctl_mem_alloc *alloc)
{
	struct kbase_va_region *reg;
	u64 flags = alloc->in.flags;
	u64 gpu_va;

	/* Calls to this function are inherently asynchronous, with respect to
	 * MMU operations.
	 */
	const enum kbase_caller_mmu_sync_info mmu_sync_info = CALLER_MMU_ASYNC;

	if (!kbase_mem_allow_alloc(kctx))
		return -EINVAL;

	if (flags & BASEP_MEM_FLAGS_KERNEL_ONLY)
		return -ENOMEM;

	/* Force SAME_VA if a 64-bit client.
	 * The only exception is GPU-executable memory if an EXEC_VA zone
	 * has been initialized. In that case, GPU-executable memory may
	 * or may not be SAME_VA.
	 */
	if ((!kbase_ctx_flag(kctx, KCTX_COMPAT)) && kbase_ctx_flag(kctx, KCTX_FORCE_SAME_VA)) {
		if (!(flags & BASE_MEM_PROT_GPU_EX) || !kbase_has_exec_va_zone(kctx))
			flags |= BASE_MEM_SAME_VA;
	}

	reg = kbase_mem_alloc(kctx, alloc->in.va_pages, alloc->in.commit_pages, alloc->in.extension,
			      &flags, &gpu_va, mmu_sync_info);

	if (!reg)
		return -ENOMEM;

	alloc->out.flags = flags;
	alloc->out.gpu_va = gpu_va;

	return 0;
}
#endif /* MALI_USE_CSF */

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

#if !MALI_USE_CSF
static int kbase_api_kinstr_jm_fd(struct kbase_context *kctx,
				  union kbase_kinstr_jm_fd *arg)
{
	return kbase_kinstr_jm_get_fd(kctx->kinstr_jm, arg);
}
#endif

static int kbase_api_hwcnt_reader_setup(struct kbase_context *kctx,
		struct kbase_ioctl_hwcnt_reader_setup *setup)
{
	return kbase_vinstr_hwcnt_reader_setup(kctx->kbdev->vinstr_ctx, setup);
}

static int kbase_api_get_cpu_gpu_timeinfo(struct kbase_context *kctx,
		union kbase_ioctl_get_cpu_gpu_timeinfo *timeinfo)
{
	u32 flags = timeinfo->in.request_flags;
	struct timespec64 ts = { 0 };
	u64 timestamp = 0;
	u64 cycle_cnt = 0;

	kbase_pm_context_active(kctx->kbdev);

	kbase_backend_get_gpu_time(kctx->kbdev,
		(flags & BASE_TIMEINFO_CYCLE_COUNTER_FLAG) ? &cycle_cnt : NULL,
		(flags & BASE_TIMEINFO_TIMESTAMP_FLAG) ? &timestamp : NULL,
		(flags & BASE_TIMEINFO_MONOTONIC_FLAG) ? &ts : NULL);

	if (flags & BASE_TIMEINFO_TIMESTAMP_FLAG)
		timeinfo->out.timestamp = timestamp;

	if (flags & BASE_TIMEINFO_CYCLE_COUNTER_FLAG)
		timeinfo->out.cycle_counter = cycle_cnt;

	if (flags & BASE_TIMEINFO_MONOTONIC_FLAG) {
		timeinfo->out.sec = ts.tv_sec;
		timeinfo->out.nsec = ts.tv_nsec;
	}

	kbase_pm_context_idle(kctx->kbdev);

	return 0;
}

#if IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
static int kbase_api_hwcnt_set(struct kbase_context *kctx,
		struct kbase_ioctl_hwcnt_values *values)
{
	return gpu_model_set_dummy_prfcnt_user_sample(u64_to_user_ptr(values->data), values->size);
}
#endif /* CONFIG_MALI_BIFROST_NO_MALI */

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
	int i;

	for (i = 0; i < sizeof(jit_init->padding); i++) {
		/* Ensure all padding bytes are 0 for potential future
		 * extension
		 */
		if (jit_init->padding[i])
			return -EINVAL;
	}

	return kbase_region_tracker_init_jit(kctx, jit_init->va_pages,
			jit_init->max_allocations, jit_init->trim_level,
			jit_init->group_id, jit_init->phys_pages);
}

static int kbase_api_mem_exec_init(struct kbase_context *kctx,
		struct kbase_ioctl_mem_exec_init *exec_init)
{
	return kbase_region_tracker_init_exec(kctx, exec_init->va_pages);
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

static int kbase_api_mem_find_gpu_start_and_offset(struct kbase_context *kctx,
		union kbase_ioctl_mem_find_gpu_start_and_offset *find)
{
	return kbasep_find_enclosing_gpu_mapping_start_and_offset(
			kctx,
			find->in.gpu_addr,
			find->in.size,
			&find->out.start,
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
	return kbase_timeline_io_acquire(kctx->kbdev, acquire->flags);
}

static int kbase_api_tlstream_flush(struct kbase_context *kctx)
{
	kbase_timeline_streams_flush(kctx->kbdev->timeline);

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

	if (alias->in.nents == 0 || alias->in.nents > BASE_MEM_ALIAS_MAX_ENTS)
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
	if (flags & BASEP_MEM_FLAGS_KERNEL_ONLY) {
		vfree(ai);
		return -EINVAL;
	}

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

	if (flags & BASEP_MEM_FLAGS_KERNEL_ONLY)
		return -ENOMEM;

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
	if (change->flags & BASEP_MEM_FLAGS_KERNEL_ONLY)
		return -ENOMEM;

	return kbase_mem_flags_change(kctx, change->gpu_va,
			change->flags, change->mask);
}

static int kbase_api_stream_create(struct kbase_context *kctx,
		struct kbase_ioctl_stream_create *stream)
{
#if IS_ENABLED(CONFIG_SYNC_FILE)
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
#if IS_ENABLED(CONFIG_SYNC_FILE)
	return kbase_sync_fence_validate(validate->fd);
#else
	return -ENOENT;
#endif
}

static int kbase_api_mem_profile_add(struct kbase_context *kctx,
		struct kbase_ioctl_mem_profile_add *data)
{
	char *buf;
	int err;

	if (data->len > KBASE_MEM_PROFILE_MAX_BUF_SIZE) {
		dev_err(kctx->kbdev->dev, "mem_profile_add: buffer too big");
		return -EINVAL;
	}

	if (!data->len) {
		dev_err(kctx->kbdev->dev, "mem_profile_add: buffer size is 0");
		/* Should return -EINVAL, but returning -ENOMEM for backwards compat */
		return -ENOMEM;
	}

	buf = kmalloc(data->len, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	err = copy_from_user(buf, u64_to_user_ptr(data->buffer),
			data->len);
	if (err) {
		kfree(buf);
		return -EFAULT;
	}

	return kbasep_mem_profile_debugfs_insert(kctx, buf, data->len);
}

#if !MALI_USE_CSF
static int kbase_api_soft_event_update(struct kbase_context *kctx,
		struct kbase_ioctl_soft_event_update *update)
{
	if (update->flags != 0)
		return -EINVAL;

	return kbase_soft_event_update(kctx, update->event, update->new_status);
}
#endif /* !MALI_USE_CSF */

static int kbase_api_sticky_resource_map(struct kbase_context *kctx,
		struct kbase_ioctl_sticky_resource_map *map)
{
	int ret;
	u64 i;
	u64 gpu_addr[BASE_EXT_RES_COUNT_MAX];

	if (!map->count || map->count > BASE_EXT_RES_COUNT_MAX)
		return -EOVERFLOW;

	ret = copy_from_user(gpu_addr, u64_to_user_ptr(map->address),
			sizeof(u64) * map->count);

	if (ret != 0)
		return -EFAULT;

	kbase_gpu_vm_lock(kctx);

	for (i = 0; i < map->count; i++) {
		if (!kbase_sticky_resource_acquire(kctx, gpu_addr[i])) {
			/* Invalid resource */
			ret = -EINVAL;
			break;
		}
	}

	if (ret != 0) {
		while (i > 0) {
			i--;
			kbase_sticky_resource_release_force(kctx, NULL, gpu_addr[i]);
		}
	}

	kbase_gpu_vm_unlock(kctx);

	return ret;
}

static int kbase_api_sticky_resource_unmap(struct kbase_context *kctx,
		struct kbase_ioctl_sticky_resource_unmap *unmap)
{
	int ret;
	u64 i;
	u64 gpu_addr[BASE_EXT_RES_COUNT_MAX];

	if (!unmap->count || unmap->count > BASE_EXT_RES_COUNT_MAX)
		return -EOVERFLOW;

	ret = copy_from_user(gpu_addr, u64_to_user_ptr(unmap->address),
			sizeof(u64) * unmap->count);

	if (ret != 0)
		return -EFAULT;

	kbase_gpu_vm_lock(kctx);

	for (i = 0; i < unmap->count; i++) {
		if (!kbase_sticky_resource_release_force(kctx, NULL, gpu_addr[i])) {
			/* Invalid resource, but we keep going anyway */
			ret = -EINVAL;
		}
	}

	kbase_gpu_vm_unlock(kctx);

	return ret;
}

#if MALI_UNIT_TEST

static int kbase_api_tlstream_stats(struct kbase_context *kctx,
		struct kbase_ioctl_tlstream_stats *stats)
{
	kbase_timeline_stats(kctx->kbdev->timeline,
			&stats->bytes_collected,
			&stats->bytes_generated);

	return 0;
}
#endif /* MALI_UNIT_TEST */

#if MALI_USE_CSF
static int kbasep_cs_event_signal(struct kbase_context *kctx)
{
	kbase_csf_event_signal_notify_gpu(kctx);
	return 0;
}

static int kbasep_cs_queue_register(struct kbase_context *kctx,
			      struct kbase_ioctl_cs_queue_register *reg)
{
	kctx->jit_group_id = BASE_MEM_GROUP_DEFAULT;

	return kbase_csf_queue_register(kctx, reg);
}

static int kbasep_cs_queue_register_ex(struct kbase_context *kctx,
			      struct kbase_ioctl_cs_queue_register_ex *reg)
{
	kctx->jit_group_id = BASE_MEM_GROUP_DEFAULT;

	return kbase_csf_queue_register_ex(kctx, reg);
}

static int kbasep_cs_queue_terminate(struct kbase_context *kctx,
			       struct kbase_ioctl_cs_queue_terminate *term)
{
	kbase_csf_queue_terminate(kctx, term);

	return 0;
}

static int kbasep_cs_queue_bind(struct kbase_context *kctx,
				union kbase_ioctl_cs_queue_bind *bind)
{
	return kbase_csf_queue_bind(kctx, bind);
}

static int kbasep_cs_queue_kick(struct kbase_context *kctx,
				struct kbase_ioctl_cs_queue_kick *kick)
{
	return kbase_csf_queue_kick(kctx, kick);
}

static int kbasep_cs_queue_group_create_1_6(
	struct kbase_context *kctx,
	union kbase_ioctl_cs_queue_group_create_1_6 *create)
{
	union kbase_ioctl_cs_queue_group_create
		new_create = { .in = {
				       .tiler_mask = create->in.tiler_mask,
				       .fragment_mask =
					       create->in.fragment_mask,
				       .compute_mask = create->in.compute_mask,
				       .cs_min = create->in.cs_min,
				       .priority = create->in.priority,
				       .tiler_max = create->in.tiler_max,
				       .fragment_max = create->in.fragment_max,
				       .compute_max = create->in.compute_max,
			       } };

	int ret = kbase_csf_queue_group_create(kctx, &new_create);

	create->out.group_handle = new_create.out.group_handle;
	create->out.group_uid = new_create.out.group_uid;

	return ret;
}
static int kbasep_cs_queue_group_create(struct kbase_context *kctx,
			     union kbase_ioctl_cs_queue_group_create *create)
{
	return kbase_csf_queue_group_create(kctx, create);
}

static int kbasep_cs_queue_group_terminate(struct kbase_context *kctx,
		struct kbase_ioctl_cs_queue_group_term *term)
{
	kbase_csf_queue_group_terminate(kctx, term->group_handle);

	return 0;
}

static int kbasep_kcpu_queue_new(struct kbase_context *kctx,
		struct kbase_ioctl_kcpu_queue_new *new)
{
	return kbase_csf_kcpu_queue_new(kctx, new);
}

static int kbasep_kcpu_queue_delete(struct kbase_context *kctx,
		struct kbase_ioctl_kcpu_queue_delete *delete)
{
	return kbase_csf_kcpu_queue_delete(kctx, delete);
}

static int kbasep_kcpu_queue_enqueue(struct kbase_context *kctx,
		struct kbase_ioctl_kcpu_queue_enqueue *enqueue)
{
	return kbase_csf_kcpu_queue_enqueue(kctx, enqueue);
}

static int kbasep_cs_tiler_heap_init(struct kbase_context *kctx,
		union kbase_ioctl_cs_tiler_heap_init *heap_init)
{
	kctx->jit_group_id = heap_init->in.group_id;

	return kbase_csf_tiler_heap_init(kctx, heap_init->in.chunk_size,
					 heap_init->in.initial_chunks, heap_init->in.max_chunks,
					 heap_init->in.target_in_flight, heap_init->in.buf_desc_va,
					 &heap_init->out.gpu_heap_va,
					 &heap_init->out.first_chunk_va);
}

static int kbasep_cs_tiler_heap_init_1_13(struct kbase_context *kctx,
					  union kbase_ioctl_cs_tiler_heap_init_1_13 *heap_init)
{
	kctx->jit_group_id = heap_init->in.group_id;

	return kbase_csf_tiler_heap_init(kctx, heap_init->in.chunk_size,
					 heap_init->in.initial_chunks, heap_init->in.max_chunks,
					 heap_init->in.target_in_flight, 0,
					 &heap_init->out.gpu_heap_va,
					 &heap_init->out.first_chunk_va);
}

static int kbasep_cs_tiler_heap_term(struct kbase_context *kctx,
		struct kbase_ioctl_cs_tiler_heap_term *heap_term)
{
	return kbase_csf_tiler_heap_term(kctx, heap_term->gpu_heap_va);
}

static int kbase_ioctl_cs_get_glb_iface(struct kbase_context *kctx,
		union kbase_ioctl_cs_get_glb_iface *param)
{
	struct basep_cs_stream_control *stream_data = NULL;
	struct basep_cs_group_control *group_data = NULL;
	void __user *user_groups, *user_streams;
	int err = 0;
	u32 const max_group_num = param->in.max_group_num;
	u32 const max_total_stream_num = param->in.max_total_stream_num;

	if (max_group_num > MAX_SUPPORTED_CSGS)
		return -EINVAL;

	if (max_total_stream_num >
		MAX_SUPPORTED_CSGS * MAX_SUPPORTED_STREAMS_PER_GROUP)
		return -EINVAL;

	user_groups = u64_to_user_ptr(param->in.groups_ptr);
	user_streams = u64_to_user_ptr(param->in.streams_ptr);

	if (max_group_num > 0) {
		if (!user_groups)
			err = -EINVAL;
		else {
			group_data = kcalloc(max_group_num,
				sizeof(*group_data), GFP_KERNEL);
			if (!group_data)
				err = -ENOMEM;
		}
	}

	if (max_total_stream_num > 0) {
		if (!user_streams)
			err = -EINVAL;
		else {
			stream_data = kcalloc(max_total_stream_num,
				sizeof(*stream_data), GFP_KERNEL);
			if (!stream_data)
				err = -ENOMEM;
		}
	}

	if (!err) {
		param->out.total_stream_num = kbase_csf_firmware_get_glb_iface(
			kctx->kbdev, group_data, max_group_num, stream_data,
			max_total_stream_num, &param->out.glb_version,
			&param->out.features, &param->out.group_num,
			&param->out.prfcnt_size, &param->out.instr_features);

		if (copy_to_user(user_groups, group_data,
			MIN(max_group_num, param->out.group_num) *
				sizeof(*group_data)))
			err = -EFAULT;
	}

	if (!err)
		if (copy_to_user(user_streams, stream_data,
			MIN(max_total_stream_num, param->out.total_stream_num) *
				sizeof(*stream_data)))
			err = -EFAULT;

	kfree(group_data);
	kfree(stream_data);
	return err;
}

static int kbasep_ioctl_cs_cpu_queue_dump(struct kbase_context *kctx,
			struct kbase_ioctl_cs_cpu_queue_info *cpu_queue_info)
{
	return kbase_csf_cpu_queue_dump(kctx, cpu_queue_info->buffer,
					cpu_queue_info->size);
}

#define POWER_DOWN_LATEST_FLUSH_VALUE ((u32)1)
static int kbase_ioctl_read_user_page(struct kbase_context *kctx,
				      union kbase_ioctl_read_user_page *user_page)
{
	struct kbase_device *kbdev = kctx->kbdev;
	unsigned long flags;

	/* As of now, only LATEST_FLUSH is supported */
	if (unlikely(user_page->in.offset != LATEST_FLUSH))
		return -EINVAL;

	/* Validating padding that must be zero */
	if (unlikely(user_page->in.padding != 0))
		return -EINVAL;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);
	if (!kbdev->pm.backend.gpu_powered)
		user_page->out.val_lo = POWER_DOWN_LATEST_FLUSH_VALUE;
	else
		user_page->out.val_lo = kbase_reg_read(kbdev, USER_REG(LATEST_FLUSH));
	user_page->out.val_hi = 0;
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return 0;
}
#endif /* MALI_USE_CSF */

static int kbasep_ioctl_context_priority_check(struct kbase_context *kctx,
			struct kbase_ioctl_context_priority_check *priority_check)
{
#if MALI_USE_CSF
	priority_check->priority = kbase_csf_priority_check(kctx->kbdev, priority_check->priority);
#else
	base_jd_prio req_priority = (base_jd_prio)priority_check->priority;

	priority_check->priority = (u8)kbase_js_priority_check(kctx->kbdev, req_priority);
#endif
	return 0;
}

#define KBASE_HANDLE_IOCTL(cmd, function, arg)                                 \
	do {                                                                   \
		int ret;                                                       \
		BUILD_BUG_ON(_IOC_DIR(cmd) != _IOC_NONE);                      \
		dev_dbg(arg->kbdev->dev, "Enter ioctl %s\n", #function);       \
		ret = function(arg);                                           \
		dev_dbg(arg->kbdev->dev, "Return %d from ioctl %s\n", ret,     \
			#function);                                            \
		return ret;                                                    \
	} while (0)

#define KBASE_HANDLE_IOCTL_IN(cmd, function, type, arg)                        \
	do {                                                                   \
		type param;                                                    \
		int ret, err;                                                  \
		dev_dbg(arg->kbdev->dev, "Enter ioctl %s\n", #function);       \
		BUILD_BUG_ON(_IOC_DIR(cmd) != _IOC_WRITE);                     \
		BUILD_BUG_ON(sizeof(param) != _IOC_SIZE(cmd));                 \
		err = copy_from_user(&param, uarg, sizeof(param));             \
		if (err)                                                       \
			return -EFAULT;                                        \
		ret = function(arg, &param);                                   \
		dev_dbg(arg->kbdev->dev, "Return %d from ioctl %s\n", ret,     \
			#function);                                            \
		return ret;                                                    \
	} while (0)

#define KBASE_HANDLE_IOCTL_OUT(cmd, function, type, arg)                       \
	do {                                                                   \
		type param;                                                    \
		int ret, err;                                                  \
		dev_dbg(arg->kbdev->dev, "Enter ioctl %s\n", #function);       \
		BUILD_BUG_ON(_IOC_DIR(cmd) != _IOC_READ);                      \
		BUILD_BUG_ON(sizeof(param) != _IOC_SIZE(cmd));                 \
		memset(&param, 0, sizeof(param));                              \
		ret = function(arg, &param);                                   \
		err = copy_to_user(uarg, &param, sizeof(param));               \
		if (err)                                                       \
			return -EFAULT;                                        \
		dev_dbg(arg->kbdev->dev, "Return %d from ioctl %s\n", ret,     \
			#function);                                            \
		return ret;                                                    \
	} while (0)

#define KBASE_HANDLE_IOCTL_INOUT(cmd, function, type, arg)                     \
	do {                                                                   \
		type param;                                                    \
		int ret, err;                                                  \
		dev_dbg(arg->kbdev->dev, "Enter ioctl %s\n", #function);       \
		BUILD_BUG_ON(_IOC_DIR(cmd) != (_IOC_WRITE | _IOC_READ));       \
		BUILD_BUG_ON(sizeof(param) != _IOC_SIZE(cmd));                 \
		err = copy_from_user(&param, uarg, sizeof(param));             \
		if (err)                                                       \
			return -EFAULT;                                        \
		ret = function(arg, &param);                                   \
		err = copy_to_user(uarg, &param, sizeof(param));               \
		if (err)                                                       \
			return -EFAULT;                                        \
		dev_dbg(arg->kbdev->dev, "Return %d from ioctl %s\n", ret,     \
			#function);                                            \
		return ret;                                                    \
	} while (0)

static int kbasep_ioctl_set_limited_core_count(struct kbase_context *kctx,
			struct kbase_ioctl_set_limited_core_count *set_limited_core_count)
{
	const u64 shader_core_mask =
		kbase_pm_get_present_cores(kctx->kbdev, KBASE_PM_CORE_SHADER);
	const u64 limited_core_mask =
		((u64)1 << (set_limited_core_count->max_core_count)) - 1;

	if ((shader_core_mask & limited_core_mask) == 0) {
		/* At least one shader core must be available after applying the mask */
		return -EINVAL;
	}

	kctx->limited_core_mask = limited_core_mask;
	return 0;
}

static long kbase_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct kbase_file *const kfile = filp->private_data;
	struct kbase_context *kctx = NULL;
	struct kbase_device *kbdev = kfile->kbdev;
	void __user *uarg = (void __user *)arg;

	/* Only these ioctls are available until setup is complete */
	switch (cmd) {
	case KBASE_IOCTL_VERSION_CHECK:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_VERSION_CHECK,
				kbase_api_handshake,
				struct kbase_ioctl_version_check,
				kfile);
		break;

	case KBASE_IOCTL_VERSION_CHECK_RESERVED:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_VERSION_CHECK_RESERVED,
				kbase_api_handshake_dummy,
				struct kbase_ioctl_version_check,
				kfile);
		break;

	case KBASE_IOCTL_SET_FLAGS:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_SET_FLAGS,
				kbase_api_set_flags,
				struct kbase_ioctl_set_flags,
				kfile);
		break;

	case KBASE_IOCTL_KINSTR_PRFCNT_ENUM_INFO:
		KBASE_HANDLE_IOCTL_INOUT(
			KBASE_IOCTL_KINSTR_PRFCNT_ENUM_INFO,
			kbase_api_kinstr_prfcnt_enum_info,
			struct kbase_ioctl_kinstr_prfcnt_enum_info, kfile);
		break;

	case KBASE_IOCTL_KINSTR_PRFCNT_SETUP:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_KINSTR_PRFCNT_SETUP,
					 kbase_api_kinstr_prfcnt_setup,
					 union kbase_ioctl_kinstr_prfcnt_setup,
					 kfile);
		break;
	case KBASE_IOCTL_GET_GPUPROPS:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_GET_GPUPROPS, kbase_api_get_gpuprops,
				      struct kbase_ioctl_get_gpuprops, kfile);
		break;
	}

	kctx = kbase_file_get_kctx_if_setup_complete(kfile);
	if (unlikely(!kctx))
		return -EPERM;

	/* Normal ioctls */
	switch (cmd) {
#if !MALI_USE_CSF
	case KBASE_IOCTL_JOB_SUBMIT:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_JOB_SUBMIT,
				kbase_api_job_submit,
				struct kbase_ioctl_job_submit,
				kctx);
		break;
#endif /* !MALI_USE_CSF */
#if !MALI_USE_CSF
	case KBASE_IOCTL_POST_TERM:
		KBASE_HANDLE_IOCTL(KBASE_IOCTL_POST_TERM,
				kbase_api_post_term,
				kctx);
		break;
#endif /* !MALI_USE_CSF */
	case KBASE_IOCTL_MEM_ALLOC:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_ALLOC,
				kbase_api_mem_alloc,
				union kbase_ioctl_mem_alloc,
				kctx);
		break;
#if MALI_USE_CSF
	case KBASE_IOCTL_MEM_ALLOC_EX:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_ALLOC_EX, kbase_api_mem_alloc_ex,
					 union kbase_ioctl_mem_alloc_ex, kctx);
		break;
#endif
	case KBASE_IOCTL_MEM_QUERY:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_QUERY,
				kbase_api_mem_query,
				union kbase_ioctl_mem_query,
				kctx);
		break;
	case KBASE_IOCTL_MEM_FREE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_FREE,
				kbase_api_mem_free,
				struct kbase_ioctl_mem_free,
				kctx);
		break;
	case KBASE_IOCTL_DISJOINT_QUERY:
		KBASE_HANDLE_IOCTL_OUT(KBASE_IOCTL_DISJOINT_QUERY,
				kbase_api_disjoint_query,
				struct kbase_ioctl_disjoint_query,
				kctx);
		break;
	case KBASE_IOCTL_GET_DDK_VERSION:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_GET_DDK_VERSION,
				kbase_api_get_ddk_version,
				struct kbase_ioctl_get_ddk_version,
				kctx);
		break;
	case KBASE_IOCTL_MEM_JIT_INIT:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_JIT_INIT,
				kbase_api_mem_jit_init,
				struct kbase_ioctl_mem_jit_init,
				kctx);
		break;
	case KBASE_IOCTL_MEM_EXEC_INIT:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_EXEC_INIT,
				kbase_api_mem_exec_init,
				struct kbase_ioctl_mem_exec_init,
				kctx);
		break;
	case KBASE_IOCTL_MEM_SYNC:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_SYNC,
				kbase_api_mem_sync,
				struct kbase_ioctl_mem_sync,
				kctx);
		break;
	case KBASE_IOCTL_MEM_FIND_CPU_OFFSET:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_FIND_CPU_OFFSET,
				kbase_api_mem_find_cpu_offset,
				union kbase_ioctl_mem_find_cpu_offset,
				kctx);
		break;
	case KBASE_IOCTL_MEM_FIND_GPU_START_AND_OFFSET:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_FIND_GPU_START_AND_OFFSET,
				kbase_api_mem_find_gpu_start_and_offset,
				union kbase_ioctl_mem_find_gpu_start_and_offset,
				kctx);
		break;
	case KBASE_IOCTL_GET_CONTEXT_ID:
		KBASE_HANDLE_IOCTL_OUT(KBASE_IOCTL_GET_CONTEXT_ID,
				kbase_api_get_context_id,
				struct kbase_ioctl_get_context_id,
				kctx);
		break;
	case KBASE_IOCTL_TLSTREAM_ACQUIRE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_TLSTREAM_ACQUIRE,
				kbase_api_tlstream_acquire,
				struct kbase_ioctl_tlstream_acquire,
				kctx);
		break;
	case KBASE_IOCTL_TLSTREAM_FLUSH:
		KBASE_HANDLE_IOCTL(KBASE_IOCTL_TLSTREAM_FLUSH,
				kbase_api_tlstream_flush,
				kctx);
		break;
	case KBASE_IOCTL_MEM_COMMIT:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_COMMIT,
				kbase_api_mem_commit,
				struct kbase_ioctl_mem_commit,
				kctx);
		break;
	case KBASE_IOCTL_MEM_ALIAS:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_ALIAS,
				kbase_api_mem_alias,
				union kbase_ioctl_mem_alias,
				kctx);
		break;
	case KBASE_IOCTL_MEM_IMPORT:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_MEM_IMPORT,
				kbase_api_mem_import,
				union kbase_ioctl_mem_import,
				kctx);
		break;
	case KBASE_IOCTL_MEM_FLAGS_CHANGE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_FLAGS_CHANGE,
				kbase_api_mem_flags_change,
				struct kbase_ioctl_mem_flags_change,
				kctx);
		break;
	case KBASE_IOCTL_STREAM_CREATE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_STREAM_CREATE,
				kbase_api_stream_create,
				struct kbase_ioctl_stream_create,
				kctx);
		break;
	case KBASE_IOCTL_FENCE_VALIDATE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_FENCE_VALIDATE,
				kbase_api_fence_validate,
				struct kbase_ioctl_fence_validate,
				kctx);
		break;
	case KBASE_IOCTL_MEM_PROFILE_ADD:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_MEM_PROFILE_ADD,
				kbase_api_mem_profile_add,
				struct kbase_ioctl_mem_profile_add,
				kctx);
		break;

#if !MALI_USE_CSF
	case KBASE_IOCTL_SOFT_EVENT_UPDATE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_SOFT_EVENT_UPDATE,
				kbase_api_soft_event_update,
				struct kbase_ioctl_soft_event_update,
				kctx);
		break;
#endif /* !MALI_USE_CSF */

	case KBASE_IOCTL_STICKY_RESOURCE_MAP:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_STICKY_RESOURCE_MAP,
				kbase_api_sticky_resource_map,
				struct kbase_ioctl_sticky_resource_map,
				kctx);
		break;
	case KBASE_IOCTL_STICKY_RESOURCE_UNMAP:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_STICKY_RESOURCE_UNMAP,
				kbase_api_sticky_resource_unmap,
				struct kbase_ioctl_sticky_resource_unmap,
				kctx);
		break;

	/* Instrumentation. */
#if !MALI_USE_CSF
	case KBASE_IOCTL_KINSTR_JM_FD:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_KINSTR_JM_FD,
				kbase_api_kinstr_jm_fd,
				union kbase_kinstr_jm_fd,
				kctx);
		break;
#endif
	case KBASE_IOCTL_HWCNT_READER_SETUP:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_HWCNT_READER_SETUP,
				kbase_api_hwcnt_reader_setup,
				struct kbase_ioctl_hwcnt_reader_setup,
				kctx);
		break;
	case KBASE_IOCTL_GET_CPU_GPU_TIMEINFO:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_GET_CPU_GPU_TIMEINFO,
				kbase_api_get_cpu_gpu_timeinfo,
				union kbase_ioctl_get_cpu_gpu_timeinfo,
				kctx);
		break;
#if IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
	case KBASE_IOCTL_HWCNT_SET:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_HWCNT_SET,
				kbase_api_hwcnt_set,
				struct kbase_ioctl_hwcnt_values,
				kctx);
		break;
#endif /* CONFIG_MALI_BIFROST_NO_MALI */
#ifdef CONFIG_MALI_CINSTR_GWT
	case KBASE_IOCTL_CINSTR_GWT_START:
		KBASE_HANDLE_IOCTL(KBASE_IOCTL_CINSTR_GWT_START,
				kbase_gpu_gwt_start,
				kctx);
		break;
	case KBASE_IOCTL_CINSTR_GWT_STOP:
		KBASE_HANDLE_IOCTL(KBASE_IOCTL_CINSTR_GWT_STOP,
				kbase_gpu_gwt_stop,
				kctx);
		break;
	case KBASE_IOCTL_CINSTR_GWT_DUMP:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_CINSTR_GWT_DUMP,
				kbase_gpu_gwt_dump,
				union kbase_ioctl_cinstr_gwt_dump,
				kctx);
		break;
#endif
#if MALI_USE_CSF
	case KBASE_IOCTL_CS_EVENT_SIGNAL:
		KBASE_HANDLE_IOCTL(KBASE_IOCTL_CS_EVENT_SIGNAL,
				kbasep_cs_event_signal,
				kctx);
		break;
	case KBASE_IOCTL_CS_QUEUE_REGISTER:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_CS_QUEUE_REGISTER,
				kbasep_cs_queue_register,
				struct kbase_ioctl_cs_queue_register,
				kctx);
		break;
	case KBASE_IOCTL_CS_QUEUE_REGISTER_EX:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_CS_QUEUE_REGISTER_EX,
				kbasep_cs_queue_register_ex,
				struct kbase_ioctl_cs_queue_register_ex,
				kctx);
		break;
	case KBASE_IOCTL_CS_QUEUE_TERMINATE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_CS_QUEUE_TERMINATE,
				kbasep_cs_queue_terminate,
				struct kbase_ioctl_cs_queue_terminate,
				kctx);
		break;
	case KBASE_IOCTL_CS_QUEUE_BIND:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_CS_QUEUE_BIND,
				kbasep_cs_queue_bind,
				union kbase_ioctl_cs_queue_bind,
				kctx);
		break;
	case KBASE_IOCTL_CS_QUEUE_KICK:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_CS_QUEUE_KICK,
				kbasep_cs_queue_kick,
				struct kbase_ioctl_cs_queue_kick,
				kctx);
		break;
	case KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_6:
		KBASE_HANDLE_IOCTL_INOUT(
			KBASE_IOCTL_CS_QUEUE_GROUP_CREATE_1_6,
			kbasep_cs_queue_group_create_1_6,
			union kbase_ioctl_cs_queue_group_create_1_6, kctx);
		break;
	case KBASE_IOCTL_CS_QUEUE_GROUP_CREATE:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_CS_QUEUE_GROUP_CREATE,
				kbasep_cs_queue_group_create,
				union kbase_ioctl_cs_queue_group_create,
				kctx);
		break;
	case KBASE_IOCTL_CS_QUEUE_GROUP_TERMINATE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_CS_QUEUE_GROUP_TERMINATE,
				kbasep_cs_queue_group_terminate,
				struct kbase_ioctl_cs_queue_group_term,
				kctx);
		break;
	case KBASE_IOCTL_KCPU_QUEUE_CREATE:
		KBASE_HANDLE_IOCTL_OUT(KBASE_IOCTL_KCPU_QUEUE_CREATE,
				kbasep_kcpu_queue_new,
				struct kbase_ioctl_kcpu_queue_new,
				kctx);
		break;
	case KBASE_IOCTL_KCPU_QUEUE_DELETE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_KCPU_QUEUE_DELETE,
				kbasep_kcpu_queue_delete,
				struct kbase_ioctl_kcpu_queue_delete,
				kctx);
		break;
	case KBASE_IOCTL_KCPU_QUEUE_ENQUEUE:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_KCPU_QUEUE_ENQUEUE,
				kbasep_kcpu_queue_enqueue,
				struct kbase_ioctl_kcpu_queue_enqueue,
				kctx);
		break;
	case KBASE_IOCTL_CS_TILER_HEAP_INIT:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_CS_TILER_HEAP_INIT,
				kbasep_cs_tiler_heap_init,
				union kbase_ioctl_cs_tiler_heap_init,
				kctx);
		break;
	case KBASE_IOCTL_CS_TILER_HEAP_INIT_1_13:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_CS_TILER_HEAP_INIT_1_13,
					 kbasep_cs_tiler_heap_init_1_13,
					 union kbase_ioctl_cs_tiler_heap_init_1_13, kctx);
		break;
	case KBASE_IOCTL_CS_TILER_HEAP_TERM:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_CS_TILER_HEAP_TERM,
				kbasep_cs_tiler_heap_term,
				struct kbase_ioctl_cs_tiler_heap_term,
				kctx);
		break;
	case KBASE_IOCTL_CS_GET_GLB_IFACE:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_CS_GET_GLB_IFACE,
				kbase_ioctl_cs_get_glb_iface,
				union kbase_ioctl_cs_get_glb_iface,
				kctx);
		break;
	case KBASE_IOCTL_CS_CPU_QUEUE_DUMP:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_CS_CPU_QUEUE_DUMP,
				kbasep_ioctl_cs_cpu_queue_dump,
				struct kbase_ioctl_cs_cpu_queue_info,
				kctx);
		break;
	case KBASE_IOCTL_READ_USER_PAGE:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_READ_USER_PAGE, kbase_ioctl_read_user_page,
					 union kbase_ioctl_read_user_page, kctx);
		break;
#endif /* MALI_USE_CSF */
#if MALI_UNIT_TEST
	case KBASE_IOCTL_TLSTREAM_STATS:
		KBASE_HANDLE_IOCTL_OUT(KBASE_IOCTL_TLSTREAM_STATS,
				kbase_api_tlstream_stats,
				struct kbase_ioctl_tlstream_stats,
				kctx);
		break;
#endif /* MALI_UNIT_TEST */
	case KBASE_IOCTL_CONTEXT_PRIORITY_CHECK:
		KBASE_HANDLE_IOCTL_INOUT(KBASE_IOCTL_CONTEXT_PRIORITY_CHECK,
				kbasep_ioctl_context_priority_check,
				struct kbase_ioctl_context_priority_check,
				kctx);
		break;
	case KBASE_IOCTL_SET_LIMITED_CORE_COUNT:
		KBASE_HANDLE_IOCTL_IN(KBASE_IOCTL_SET_LIMITED_CORE_COUNT,
				kbasep_ioctl_set_limited_core_count,
				struct kbase_ioctl_set_limited_core_count,
				kctx);
		break;
	}

	dev_warn(kbdev->dev, "Unknown ioctl 0x%x nr:%d", cmd, _IOC_NR(cmd));

	return -ENOIOCTLCMD;
}

#if MALI_USE_CSF
static ssize_t kbase_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct kbase_file *const kfile = filp->private_data;
	struct kbase_context *const kctx =
		kbase_file_get_kctx_if_setup_complete(kfile);
	struct base_csf_notification event_data = {
		.type = BASE_CSF_NOTIFICATION_EVENT };
	const size_t data_size = sizeof(event_data);
	bool read_event = false, read_error = false;

	if (unlikely(!kctx))
		return -EPERM;

	if (count < data_size)
		return -ENOBUFS;

	if (atomic_read(&kctx->event_count))
		read_event = true;
	else
		read_error = kbase_csf_event_read_error(kctx, &event_data);

	if (!read_event && !read_error) {
		bool dump = kbase_csf_cpu_queue_read_dump_req(kctx,
							&event_data);
		/* This condition is not treated as an error.
		 * It is possible that event handling thread was woken up due
		 * to a fault/error that occurred for a queue group, but before
		 * the corresponding fault data was read by the thread the
		 * queue group was already terminated by the userspace.
		 */
		if (!dump)
			dev_dbg(kctx->kbdev->dev,
				"Neither event nor error signaled");
	}

	if (copy_to_user(buf, &event_data, data_size) != 0) {
		dev_warn(kctx->kbdev->dev,
			"Failed to copy data\n");
		return -EFAULT;
	}

	if (read_event)
		atomic_set(&kctx->event_count, 0);

	return data_size;
}
#else /* MALI_USE_CSF */
static ssize_t kbase_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct kbase_file *const kfile = filp->private_data;
	struct kbase_context *const kctx =
		kbase_file_get_kctx_if_setup_complete(kfile);
	struct base_jd_event_v2 uevent;
	int out_count = 0;

	if (unlikely(!kctx))
		return -EPERM;

	if (count < sizeof(uevent))
		return -ENOBUFS;

	memset(&uevent, 0, sizeof(uevent));

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
#endif /* MALI_USE_CSF */

static __poll_t kbase_poll(struct file *filp, poll_table *wait)
{
	struct kbase_file *const kfile = filp->private_data;
	struct kbase_context *const kctx =
		kbase_file_get_kctx_if_setup_complete(kfile);

	if (unlikely(!kctx)) {
#if (KERNEL_VERSION(4, 19, 0) > LINUX_VERSION_CODE)
		return POLLERR;
#else
		return EPOLLERR;
#endif
	}

	poll_wait(filp, &kctx->event_queue, wait);
	if (kbase_event_pending(kctx)) {
#if (KERNEL_VERSION(4, 19, 0) > LINUX_VERSION_CODE)
		return POLLIN | POLLRDNORM;
#else
		return EPOLLIN | EPOLLRDNORM;
#endif
	}

	return 0;
}

void kbase_event_wakeup(struct kbase_context *kctx)
{
	KBASE_DEBUG_ASSERT(kctx);
	dev_dbg(kctx->kbdev->dev, "Waking event queue for context %pK\n",
		(void *)kctx);
	wake_up_interruptible(&kctx->event_queue);
}

KBASE_EXPORT_TEST_API(kbase_event_wakeup);

#if MALI_USE_CSF
int kbase_event_pending(struct kbase_context *ctx)
{
	WARN_ON_ONCE(!ctx);

	return (atomic_read(&ctx->event_count) != 0) ||
		kbase_csf_event_error_pending(ctx) ||
		kbase_csf_cpu_queue_dump_needed(ctx);
}
#else
int kbase_event_pending(struct kbase_context *ctx)
{
	KBASE_DEBUG_ASSERT(ctx);

	return (atomic_read(&ctx->event_count) != 0) ||
		(atomic_read(&ctx->event_closed) != 0);
}
#endif

KBASE_EXPORT_TEST_API(kbase_event_pending);

static int kbase_mmap(struct file *const filp, struct vm_area_struct *const vma)
{
	struct kbase_file *const kfile = filp->private_data;
	struct kbase_context *const kctx =
		kbase_file_get_kctx_if_setup_complete(kfile);

	if (unlikely(!kctx))
		return -EPERM;

	return kbase_context_mmap(kctx, vma);
}

static int kbase_check_flags(int flags)
{
	/* Enforce that the driver keeps the O_CLOEXEC flag so that execve() always
	 * closes the file descriptor in a child process.
	 */
	if (0 == (flags & O_CLOEXEC))
		return -EINVAL;

	return 0;
}

static unsigned long kbase_get_unmapped_area(struct file *const filp,
		const unsigned long addr, const unsigned long len,
		const unsigned long pgoff, const unsigned long flags)
{
	struct kbase_file *const kfile = filp->private_data;
	struct kbase_context *const kctx =
		kbase_file_get_kctx_if_setup_complete(kfile);

	if (unlikely(!kctx))
		return -EPERM;

	return kbase_context_get_unmapped_area(kctx, addr, len, pgoff, flags);
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

/**
 * power_policy_show - Show callback for the power_policy sysfs file.
 *
 * @dev:	The device this sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The output buffer for the sysfs file contents
 *
 * This function is called to get the contents of the power_policy sysfs
 * file. This is a list of the available policies with the currently active one
 * surrounded by square brackets.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t power_policy_show(struct device *dev, struct device_attribute *attr, char *const buf)
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

	policy_count = kbase_pm_list_policies(kbdev, &policy_list);

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
 * power_policy_store - Store callback for the power_policy sysfs file.
 *
 * @dev:	The device with sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The value written to the sysfs file
 * @count:	The number of bytes to write to the sysfs file
 *
 * This function is called when the power_policy sysfs file is written to.
 * It matches the requested policy against the available policies and if a
 * matching policy is found calls kbase_pm_set_policy() to change the
 * policy.
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t power_policy_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	const struct kbase_pm_policy *new_policy = NULL;
	const struct kbase_pm_policy *const *policy_list;
	int policy_count;
	int i;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	policy_count = kbase_pm_list_policies(kbdev, &policy_list);

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
static DEVICE_ATTR_RW(power_policy);

/*
 * core_mask_show - Show callback for the core_mask sysfs file.
 *
 * @dev:	The device this sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The output buffer for the sysfs file contents
 *
 * This function is called to get the contents of the core_mask sysfs file.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t core_mask_show(struct device *dev, struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	unsigned long flags;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

#if MALI_USE_CSF
	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			 "Current debug core mask : 0x%llX\n",
			 kbdev->pm.debug_core_mask);
	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			 "Current desired core mask : 0x%llX\n",
			 kbase_pm_ca_get_core_mask(kbdev));
	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			 "Current in use core mask : 0x%llX\n",
			 kbdev->pm.backend.shaders_avail);
#else
	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			"Current core mask (JS0) : 0x%llX\n",
			kbdev->pm.debug_core_mask[0]);
	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			"Current core mask (JS1) : 0x%llX\n",
			kbdev->pm.debug_core_mask[1]);
	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			"Current core mask (JS2) : 0x%llX\n",
			kbdev->pm.debug_core_mask[2]);
#endif /* MALI_USE_CSF */

	ret += scnprintf(buf + ret, PAGE_SIZE - ret,
			"Available core mask : 0x%llX\n",
			kbdev->gpu_props.props.raw_props.shader_present);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return ret;
}

/**
 * core_mask_store - Store callback for the core_mask sysfs file.
 *
 * @dev:	The device with sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The value written to the sysfs file
 * @count:	The number of bytes to write to the sysfs file
 *
 * This function is called when the core_mask sysfs file is written to.
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t core_mask_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
#if MALI_USE_CSF
	u64 new_core_mask;
#else
	u64 new_core_mask[3];
	u64 group0_core_mask;
	int i;
#endif /* MALI_USE_CSF */

	int items;
	ssize_t err = count;
	unsigned long flags;
	u64 shader_present;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

#if MALI_USE_CSF
	items = sscanf(buf, "%llx", &new_core_mask);

	if (items != 1) {
		dev_err(kbdev->dev,
			"Couldn't process core mask write operation.\n"
			"Use format <core_mask>\n");
		err = -EINVAL;
		goto end;
	}
#else
	items = sscanf(buf, "%llx %llx %llx",
			&new_core_mask[0], &new_core_mask[1],
			&new_core_mask[2]);

	if (items != 1 && items != 3) {
		dev_err(kbdev->dev, "Couldn't process core mask write operation.\n"
			"Use format <core_mask>\n"
			"or <core_mask_js0> <core_mask_js1> <core_mask_js2>\n");
		err = -EINVAL;
		goto end;
	}

	if (items == 1)
		new_core_mask[1] = new_core_mask[2] = new_core_mask[0];
#endif

	mutex_lock(&kbdev->pm.lock);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	shader_present = kbdev->gpu_props.props.raw_props.shader_present;

#if MALI_USE_CSF
	if ((new_core_mask & shader_present) != new_core_mask) {
		dev_err(dev,
			"Invalid core mask 0x%llX: Includes non-existent cores (present = 0x%llX)",
			new_core_mask, shader_present);
		err = -EINVAL;
		goto unlock;

	} else if (!(new_core_mask & shader_present &
		     kbdev->pm.backend.ca_cores_enabled)) {
		dev_err(dev,
			"Invalid core mask 0x%llX: No intersection with currently available cores (present = 0x%llX, CA enabled = 0x%llX\n",
			new_core_mask,
			kbdev->gpu_props.props.raw_props.shader_present,
			kbdev->pm.backend.ca_cores_enabled);
		err = -EINVAL;
		goto unlock;
	}

	if (kbdev->pm.debug_core_mask != new_core_mask)
		kbase_pm_set_debug_core_mask(kbdev, new_core_mask);
#else
	group0_core_mask = kbdev->gpu_props.props.coherency_info.group[0].core_mask;

	for (i = 0; i < 3; ++i) {
		if ((new_core_mask[i] & shader_present) != new_core_mask[i]) {
			dev_err(dev, "Invalid core mask 0x%llX for JS %d: Includes non-existent cores (present = 0x%llX)",
					new_core_mask[i], i, shader_present);
			err = -EINVAL;
			goto unlock;

		} else if (!(new_core_mask[i] & shader_present & kbdev->pm.backend.ca_cores_enabled)) {
			dev_err(dev, "Invalid core mask 0x%llX for JS %d: No intersection with currently available cores (present = 0x%llX, CA enabled = 0x%llX\n",
					new_core_mask[i], i,
					kbdev->gpu_props.props.raw_props.shader_present,
					kbdev->pm.backend.ca_cores_enabled);
			err = -EINVAL;
			goto unlock;

		} else if (!(new_core_mask[i] & group0_core_mask)) {
			dev_err(dev, "Invalid core mask 0x%llX for JS %d: No intersection with group 0 core mask 0x%llX\n",
					new_core_mask[i], i, group0_core_mask);
			err = -EINVAL;
			goto unlock;
		} else if (!(new_core_mask[i] & kbdev->gpu_props.curr_config.shader_present)) {
			dev_err(dev, "Invalid core mask 0x%llX for JS %d: No intersection with current core mask 0x%llX\n",
					new_core_mask[i], i, kbdev->gpu_props.curr_config.shader_present);
			err = -EINVAL;
			goto unlock;
		}
	}

	if (kbdev->pm.debug_core_mask[0] != new_core_mask[0] ||
			kbdev->pm.debug_core_mask[1] !=
					new_core_mask[1] ||
			kbdev->pm.debug_core_mask[2] !=
					new_core_mask[2]) {

		kbase_pm_set_debug_core_mask(kbdev, new_core_mask[0],
				new_core_mask[1], new_core_mask[2]);
	}
#endif /* MALI_USE_CSF */

unlock:
	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->pm.lock);
end:
	return err;
}

/*
 * The sysfs file core_mask.
 *
 * This is used to restrict shader core availability for debugging purposes.
 * Reading it will show the current core mask and the mask of cores available.
 * Writing to it will set the current core mask.
 */
static DEVICE_ATTR_RW(core_mask);

#if !MALI_USE_CSF
/**
 * soft_job_timeout_store - Store callback for the soft_job_timeout sysfs
 * file.
 *
 * @dev: The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf: The value written to the sysfs file.
 * @count: The number of bytes to write to the sysfs file.
 *
 * This allows setting the timeout for software jobs. Waiting soft event wait
 * jobs will be cancelled after this period expires, while soft fence wait jobs
 * will print debug information if the fence debug feature is enabled.
 *
 * This is expressed in milliseconds.
 *
 * Return: count if the function succeeded. An error code on failure.
 */
static ssize_t soft_job_timeout_store(struct device *dev,
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
 * soft_job_timeout_show - Show callback for the soft_job_timeout sysfs
 * file.
 *
 * @dev: The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf: The output buffer for the sysfs file contents.
 *
 * This will return the timeout for the software jobs.
 *
 * Return: The number of bytes output to buf.
 */
static ssize_t soft_job_timeout_show(struct device *dev,
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

static DEVICE_ATTR_RW(soft_job_timeout);

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
 * js_timeouts_store - Store callback for the js_timeouts sysfs file.
 *
 * @dev:	The device with sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The value written to the sysfs file
 * @count:	The number of bytes to write to the sysfs file
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
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t js_timeouts_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
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
				DEFAULT_JS_HARD_STOP_TICKS_SS);
		UPDATE_TIMEOUT(hard_stop_ticks_cl, js_hard_stop_ms_cl,
				DEFAULT_JS_HARD_STOP_TICKS_CL);
		UPDATE_TIMEOUT(hard_stop_ticks_dumping,
				js_hard_stop_ms_dumping,
				DEFAULT_JS_HARD_STOP_TICKS_DUMPING);
		UPDATE_TIMEOUT(gpu_reset_ticks_ss, js_reset_ms_ss,
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
 * js_timeouts_show - Show callback for the js_timeouts sysfs file.
 *
 * @dev:	The device this sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The output buffer for the sysfs file contents
 *
 * This function is called to get the contents of the js_timeouts sysfs
 * file. It returns the last set values written to the js_timeouts sysfs file.
 * If the file didn't get written yet, the values will be current setting in
 * use.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t js_timeouts_show(struct device *dev, struct device_attribute *attr, char * const buf)
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
static DEVICE_ATTR_RW(js_timeouts);

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
 * js_scheduling_period_store - Store callback for the js_scheduling_period sysfs
 *                            file
 * @dev:   The device the sysfs file is for
 * @attr:  The attributes of the sysfs file
 * @buf:   The value written to the sysfs file
 * @count: The number of bytes to write to the sysfs file
 *
 * This function is called when the js_scheduling_period sysfs file is written
 * to. It checks the data written, and if valid updates the js_scheduling_period
 * value
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t js_scheduling_period_store(struct device *dev,
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
	 * update is pending and use the new values if necessary.
	 */

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
 * js_scheduling_period_show - Show callback for the js_scheduling_period sysfs
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
static ssize_t js_scheduling_period_show(struct device *dev,
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

static DEVICE_ATTR_RW(js_scheduling_period);


#ifdef CONFIG_MALI_BIFROST_DEBUG
static ssize_t js_softstop_always_store(struct device *dev,
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

static ssize_t js_softstop_always_show(struct device *dev,
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
static DEVICE_ATTR_RW(js_softstop_always);
#endif /* CONFIG_MALI_BIFROST_DEBUG */
#endif /* !MALI_USE_CSF */

#ifdef CONFIG_MALI_BIFROST_DEBUG
typedef void kbasep_debug_command_func(struct kbase_device *);

enum kbasep_debug_command_code {
	KBASEP_DEBUG_COMMAND_DUMPTRACE,

	/* This must be the last enum */
	KBASEP_DEBUG_COMMAND_COUNT
};

struct kbasep_debug_command {
	char *str;
	kbasep_debug_command_func *func;
};

static void kbasep_ktrace_dump_wrapper(struct kbase_device *kbdev)
{
	KBASE_KTRACE_DUMP(kbdev);
}

/* Debug commands supported by the driver */
static const struct kbasep_debug_command debug_commands[] = {
	{
		.str = "dumptrace",
		.func = &kbasep_ktrace_dump_wrapper,
	}
};

/**
 * debug_command_show - Show callback for the debug_command sysfs file.
 *
 * @dev:	The device this sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The output buffer for the sysfs file contents
 *
 * This function is called to get the contents of the debug_command sysfs
 * file. This is a list of the available debug commands, separated by newlines.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t debug_command_show(struct device *dev, struct device_attribute *attr, char * const buf)
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
 * debug_command_store - Store callback for the debug_command sysfs file.
 *
 * @dev:	The device with sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The value written to the sysfs file
 * @count:	The number of bytes written to the sysfs file
 *
 * This function is called when the debug_command sysfs file is written to.
 * It matches the requested command against the available commands, and if
 * a matching command is found calls the associated function from
 * @debug_commands to issue the command.
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t debug_command_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
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
static DEVICE_ATTR_RW(debug_command);
#endif /* CONFIG_MALI_BIFROST_DEBUG */

/**
 * gpuinfo_show - Show callback for the gpuinfo sysfs entry.
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
static ssize_t gpuinfo_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	static const struct gpu_product_id_name {
		unsigned int id;
		char *name;
	} gpu_product_id_names[] = {
		{ .id = GPU_ID2_PRODUCT_TMIX >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G71" },
		{ .id = GPU_ID2_PRODUCT_THEX >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G72" },
		{ .id = GPU_ID2_PRODUCT_TSIX >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G51" },
		{ .id = GPU_ID2_PRODUCT_TNOX >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G76" },
		{ .id = GPU_ID2_PRODUCT_TDVX >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G31" },
		{ .id = GPU_ID2_PRODUCT_TGOX >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G52" },
		{ .id = GPU_ID2_PRODUCT_TTRX >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G77" },
		{ .id = GPU_ID2_PRODUCT_TBEX >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G78" },
		{ .id = GPU_ID2_PRODUCT_TBAX >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G78AE" },
		{ .id = GPU_ID2_PRODUCT_LBEX >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G68" },
		{ .id = GPU_ID2_PRODUCT_TNAX >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G57" },
		{ .id = GPU_ID2_PRODUCT_TODX >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G710" },
		{ .id = GPU_ID2_PRODUCT_LODX >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G610" },
		{ .id = GPU_ID2_PRODUCT_TGRX >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G510" },
		{ .id = GPU_ID2_PRODUCT_TVAX >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-G310" },
		{ .id = GPU_ID2_PRODUCT_TTIX >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-TTIX" },
		{ .id = GPU_ID2_PRODUCT_LTIX >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT,
		  .name = "Mali-LTIX" },
	};
	const char *product_name = "(Unknown Mali GPU)";
	struct kbase_device *kbdev;
	u32 gpu_id;
	unsigned int product_id, product_id_mask;
	unsigned int i;
	struct kbase_gpu_props *gpu_props;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	gpu_props = &kbdev->gpu_props;
	gpu_id = gpu_props->props.raw_props.gpu_id;
	product_id = gpu_id >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT;
	product_id_mask = GPU_ID2_PRODUCT_MODEL >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT;

	for (i = 0; i < ARRAY_SIZE(gpu_product_id_names); ++i) {
		const struct gpu_product_id_name *p = &gpu_product_id_names[i];

		if ((p->id & product_id_mask) ==
		    (product_id & product_id_mask)) {
			product_name = p->name;
			break;
		}
	}

#if MALI_USE_CSF
	if ((product_id & product_id_mask) ==
	    ((GPU_ID2_PRODUCT_TTUX >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT) & product_id_mask)) {
		const bool rt_supported =
			GPU_FEATURES_RAY_TRACING_GET(gpu_props->props.raw_props.gpu_features);
		const u8 nr_cores = gpu_props->num_cores;

		/* Mali-G715-Immortalis if 10 < number of cores with ray tracing supproted.
		 * Mali-G715 if 10 < number of cores without ray tracing supported.
		 * Mali-G715 if 7 <= number of cores <= 10 regardless ray tracing.
		 * Mali-G615 if number of cores < 7.
		 */
		if ((nr_cores > 10) && rt_supported)
			product_name = "Mali-G715-Immortalis";
		else if (nr_cores >= 7)
			product_name = "Mali-G715";

		if (nr_cores < 7) {
			dev_warn(kbdev->dev, "nr_cores(%u) GPU ID must be G615", nr_cores);
			product_name = "Mali-G615";
		} else
			dev_dbg(kbdev->dev, "GPU ID_Name: %s, nr_cores(%u)\n", product_name,
				nr_cores);
	}
#endif /* MALI_USE_CSF */

	return scnprintf(buf, PAGE_SIZE, "%s %d cores r%dp%d 0x%04X\n", product_name,
			 kbdev->gpu_props.num_cores,
			 (gpu_id & GPU_ID_VERSION_MAJOR) >> KBASE_GPU_ID_VERSION_MAJOR_SHIFT,
			 (gpu_id & GPU_ID_VERSION_MINOR) >> KBASE_GPU_ID_VERSION_MINOR_SHIFT,
			 product_id);
}
static DEVICE_ATTR_RO(gpuinfo);

/**
 * dvfs_period_store - Store callback for the dvfs_period sysfs file.
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
static ssize_t dvfs_period_store(struct device *dev,
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
 * dvfs_period_show - Show callback for the dvfs_period sysfs entry.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the GPU information.
 *
 * This function is called to get the current period used for the DVFS sample
 * timer.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t dvfs_period_show(struct device *dev,
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

static DEVICE_ATTR_RW(dvfs_period);

int kbase_pm_lowest_gpu_freq_init(struct kbase_device *kbdev)
{
	/* Uses default reference frequency defined in below macro */
	u64 lowest_freq_khz = DEFAULT_REF_TIMEOUT_FREQ_KHZ;

	/* Only check lowest frequency in cases when OPPs are used and
	 * present in the device tree.
	 */
#ifdef CONFIG_PM_OPP
	struct dev_pm_opp *opp_ptr;
	unsigned long found_freq = 0;

	/* find lowest frequency OPP */
	opp_ptr = dev_pm_opp_find_freq_ceil(kbdev->dev, &found_freq);
	if (IS_ERR(opp_ptr)) {
		dev_err(kbdev->dev, "No OPPs found in device tree! Scaling timeouts using %llu kHz",
			(unsigned long long)lowest_freq_khz);
	} else {
#if KERNEL_VERSION(4, 11, 0) <= LINUX_VERSION_CODE
		dev_pm_opp_put(opp_ptr); /* decrease OPP refcount */
#endif
		/* convert found frequency to KHz */
		found_freq /= 1000;

		/* If lowest frequency in OPP table is still higher
		 * than the reference, then keep the reference frequency
		 * as the one to use for scaling .
		 */
		if (found_freq < lowest_freq_khz)
			lowest_freq_khz = found_freq;
	}
#else
	dev_err(kbdev->dev, "No operating-points-v2 node or operating-points property in DT");
#endif

	kbdev->lowest_gpu_freq_khz = lowest_freq_khz;
	dev_dbg(kbdev->dev, "Lowest frequency identified is %llu kHz", kbdev->lowest_gpu_freq_khz);
	return 0;
}

/**
 * pm_poweroff_store - Store callback for the pm_poweroff sysfs file.
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
static ssize_t pm_poweroff_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	struct kbasep_pm_tick_timer_state *stt;
	int items;
	u64 gpu_poweroff_time;
	unsigned int poweroff_shader_ticks, poweroff_gpu_ticks;
	unsigned long flags;

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

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	stt = &kbdev->pm.backend.shader_tick_timer;
	stt->configured_interval = HR_TIMER_DELAY_NSEC(gpu_poweroff_time);
	stt->default_ticks = poweroff_shader_ticks;
	stt->configured_ticks = stt->default_ticks;

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	if (poweroff_gpu_ticks != 0)
		dev_warn(kbdev->dev, "Separate GPU poweroff delay no longer supported.\n");

	return count;
}

/**
 * pm_poweroff_show - Show callback for the pm_poweroff sysfs entry.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the GPU information.
 *
 * This function is called to get the current period used for the DVFS sample
 * timer.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t pm_poweroff_show(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	struct kbasep_pm_tick_timer_state *stt;
	ssize_t ret;
	unsigned long flags;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	stt = &kbdev->pm.backend.shader_tick_timer;
	ret = scnprintf(buf, PAGE_SIZE, "%llu %u 0\n",
			ktime_to_ns(stt->configured_interval),
			stt->default_ticks);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);

	return ret;
}

static DEVICE_ATTR_RW(pm_poweroff);

/**
 * reset_timeout_store - Store callback for the reset_timeout sysfs file.
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
static ssize_t reset_timeout_store(struct device *dev,
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
 * reset_timeout_show - Show callback for the reset_timeout sysfs entry.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the GPU information.
 *
 * This function is called to get the current reset timeout.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t reset_timeout_show(struct device *dev,
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

static DEVICE_ATTR_RW(reset_timeout);

static ssize_t mem_pool_size_show(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	return kbase_debugfs_helper_get_attr_to_string(buf, PAGE_SIZE,
		kbdev->mem_pools.small, MEMORY_GROUP_MANAGER_NR_GROUPS,
		kbase_mem_pool_debugfs_size);
}

static ssize_t mem_pool_size_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);
	int err;

	if (!kbdev)
		return -ENODEV;

	err = kbase_debugfs_helper_set_attr_from_string(buf,
		kbdev->mem_pools.small, MEMORY_GROUP_MANAGER_NR_GROUPS,
		kbase_mem_pool_debugfs_trim);

	return err ? err : count;
}

static DEVICE_ATTR_RW(mem_pool_size);

static ssize_t mem_pool_max_size_show(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	return kbase_debugfs_helper_get_attr_to_string(buf, PAGE_SIZE,
		kbdev->mem_pools.small, MEMORY_GROUP_MANAGER_NR_GROUPS,
		kbase_mem_pool_debugfs_max_size);
}

static ssize_t mem_pool_max_size_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);
	int err;

	if (!kbdev)
		return -ENODEV;

	err = kbase_debugfs_helper_set_attr_from_string(buf,
		kbdev->mem_pools.small, MEMORY_GROUP_MANAGER_NR_GROUPS,
		kbase_mem_pool_debugfs_set_max_size);

	return err ? err : count;
}

static DEVICE_ATTR_RW(mem_pool_max_size);

/**
 * lp_mem_pool_size_show - Show size of the large memory pages pool.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the pool size.
 *
 * This function is called to get the number of large memory pages which currently populate the kbdev pool.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t lp_mem_pool_size_show(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	return kbase_debugfs_helper_get_attr_to_string(buf, PAGE_SIZE,
		kbdev->mem_pools.large, MEMORY_GROUP_MANAGER_NR_GROUPS,
		kbase_mem_pool_debugfs_size);
}

/**
 * lp_mem_pool_size_store - Set size of the large memory pages pool.
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
static ssize_t lp_mem_pool_size_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);
	int err;

	if (!kbdev)
		return -ENODEV;

	err = kbase_debugfs_helper_set_attr_from_string(buf,
		kbdev->mem_pools.large, MEMORY_GROUP_MANAGER_NR_GROUPS,
		kbase_mem_pool_debugfs_trim);

	return err ? err : count;
}

static DEVICE_ATTR_RW(lp_mem_pool_size);

/**
 * lp_mem_pool_max_size_show - Show maximum size of the large memory pages pool.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the pool size.
 *
 * This function is called to get the maximum number of large memory pages that the kbdev pool can possibly contain.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t lp_mem_pool_max_size_show(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	return kbase_debugfs_helper_get_attr_to_string(buf, PAGE_SIZE,
		kbdev->mem_pools.large, MEMORY_GROUP_MANAGER_NR_GROUPS,
		kbase_mem_pool_debugfs_max_size);
}

/**
 * lp_mem_pool_max_size_store - Set maximum size of the large memory pages pool.
 * @dev:   The device this sysfs file is for.
 * @attr:  The attributes of the sysfs file.
 * @buf:   The value written to the sysfs file.
 * @count: The number of bytes written to the sysfs file.
 *
 * This function is called to set the maximum number of large memory pages that the kbdev pool can possibly contain.
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t lp_mem_pool_max_size_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);
	int err;

	if (!kbdev)
		return -ENODEV;

	err = kbase_debugfs_helper_set_attr_from_string(buf,
		kbdev->mem_pools.large, MEMORY_GROUP_MANAGER_NR_GROUPS,
		kbase_mem_pool_debugfs_set_max_size);

	return err ? err : count;
}

static DEVICE_ATTR_RW(lp_mem_pool_max_size);

/**
 * show_simplified_mem_pool_max_size - Show the maximum size for the memory
 *                                     pool 0 of small (4KiB) pages.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the max size.
 *
 * This function is called to get the maximum size for the memory pool 0 of
 * small (4KiB) pages. It is assumed that the maximum size value is same for
 * all the pools.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t show_simplified_mem_pool_max_size(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	return kbase_debugfs_helper_get_attr_to_string(buf, PAGE_SIZE,
		kbdev->mem_pools.small, 1, kbase_mem_pool_debugfs_max_size);
}

/**
 * set_simplified_mem_pool_max_size - Set the same maximum size for all the
 *                                    memory pools of small (4KiB) pages.
 * @dev:   The device with sysfs file is for
 * @attr:  The attributes of the sysfs file
 * @buf:   The value written to the sysfs file
 * @count: The number of bytes written to the sysfs file
 *
 * This function is called to set the same maximum size for all the memory
 * pools of small (4KiB) pages.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t set_simplified_mem_pool_max_size(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);
	unsigned long new_size;
	int gid;
	int err;

	if (!kbdev)
		return -ENODEV;

	err = kstrtoul(buf, 0, &new_size);
	if (err)
		return -EINVAL;

	for (gid = 0; gid < MEMORY_GROUP_MANAGER_NR_GROUPS; ++gid)
		kbase_mem_pool_debugfs_set_max_size(
			kbdev->mem_pools.small, gid, (size_t)new_size);

	return count;
}

static DEVICE_ATTR(max_size, 0600, show_simplified_mem_pool_max_size,
		set_simplified_mem_pool_max_size);

/**
 * show_simplified_lp_mem_pool_max_size - Show the maximum size for the memory
 *                                        pool 0 of large (2MiB) pages.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the total current pool size.
 *
 * This function is called to get the maximum size for the memory pool 0 of
 * large (2MiB) pages. It is assumed that the maximum size value is same for
 * all the pools.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t show_simplified_lp_mem_pool_max_size(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	return kbase_debugfs_helper_get_attr_to_string(buf, PAGE_SIZE,
		kbdev->mem_pools.large, 1, kbase_mem_pool_debugfs_max_size);
}

/**
 * set_simplified_lp_mem_pool_max_size - Set the same maximum size for all the
 *                                       memory pools of large (2MiB) pages.
 * @dev:   The device with sysfs file is for
 * @attr:  The attributes of the sysfs file
 * @buf:   The value written to the sysfs file
 * @count: The number of bytes written to the sysfs file
 *
 * This function is called to set the same maximum size for all the memory
 * pools of large (2MiB) pages.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t set_simplified_lp_mem_pool_max_size(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *const kbdev = to_kbase_device(dev);
	unsigned long new_size;
	int gid;
	int err;

	if (!kbdev)
		return -ENODEV;

	err = kstrtoul(buf, 0, &new_size);
	if (err)
		return -EINVAL;

	for (gid = 0; gid < MEMORY_GROUP_MANAGER_NR_GROUPS; ++gid)
		kbase_mem_pool_debugfs_set_max_size(
			kbdev->mem_pools.large, gid, (size_t)new_size);

	return count;
}

static DEVICE_ATTR(lp_max_size, 0600, show_simplified_lp_mem_pool_max_size,
		set_simplified_lp_mem_pool_max_size);

/**
 * show_simplified_ctx_default_max_size - Show the default maximum size for the
 *                                        memory pool 0 of small (4KiB) pages.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the pool size.
 *
 * This function is called to get the default ctx maximum size for the memory
 * pool 0 of small (4KiB) pages. It is assumed that maximum size value is same
 * for all the pools. The maximum size for the pool of large (2MiB) pages will
 * be same as max size of the pool of small (4KiB) pages in terms of bytes.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t show_simplified_ctx_default_max_size(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev = to_kbase_device(dev);
	size_t max_size;

	if (!kbdev)
		return -ENODEV;

	max_size = kbase_mem_pool_config_debugfs_max_size(
			kbdev->mem_pool_defaults.small, 0);

	return scnprintf(buf, PAGE_SIZE, "%zu\n", max_size);
}

/**
 * set_simplified_ctx_default_max_size - Set the same default maximum size for
 *                                       all the pools created for new
 *                                       contexts. This covers the pool of
 *                                       large pages as well and its max size
 *                                       will be same as max size of the pool
 *                                       of small pages in terms of bytes.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The value written to the sysfs file.
 * @count: The number of bytes written to the sysfs file.
 *
 * This function is called to set the same maximum size for all pools created
 * for new contexts.
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t set_simplified_ctx_default_max_size(struct device *dev,
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
		return -EINVAL;

	kbase_mem_pool_group_config_set_max_size(
		&kbdev->mem_pool_defaults, (size_t)new_size);

	return count;
}

static DEVICE_ATTR(ctx_default_max_size, 0600,
		show_simplified_ctx_default_max_size,
		set_simplified_ctx_default_max_size);

#if !MALI_USE_CSF
/**
 * js_ctx_scheduling_mode_show - Show callback for js_ctx_scheduling_mode sysfs
 *                               entry.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the context scheduling mode information.
 *
 * This function is called to get the context scheduling mode being used by JS.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t js_ctx_scheduling_mode_show(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	return scnprintf(buf, PAGE_SIZE, "%u\n", kbdev->js_ctx_scheduling_mode);
}

/**
 * js_ctx_scheduling_mode_store - Set callback for js_ctx_scheduling_mode sysfs
 *                              entry.
 * @dev:   The device this sysfs file is for.
 * @attr:  The attributes of the sysfs file.
 * @buf:   The value written to the sysfs file.
 * @count: The number of bytes written to the sysfs file.
 *
 * This function is called when the js_ctx_scheduling_mode sysfs file is written
 * to. It checks the data written, and if valid updates the ctx scheduling mode
 * being by JS.
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t js_ctx_scheduling_mode_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_context *kctx;
	u32 new_js_ctx_scheduling_mode;
	struct kbase_device *kbdev;
	unsigned long flags;
	int ret;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = kstrtouint(buf, 0, &new_js_ctx_scheduling_mode);
	if (ret || new_js_ctx_scheduling_mode >= KBASE_JS_PRIORITY_MODE_COUNT) {
		dev_err(kbdev->dev, "Couldn't process js_ctx_scheduling_mode"
				" write operation.\n"
				"Use format <js_ctx_scheduling_mode>\n");
		return -EINVAL;
	}

	if (new_js_ctx_scheduling_mode == kbdev->js_ctx_scheduling_mode)
		return count;

	mutex_lock(&kbdev->kctx_list_lock);
	spin_lock_irqsave(&kbdev->hwaccess_lock, flags);

	/* Update the context priority mode */
	kbdev->js_ctx_scheduling_mode = new_js_ctx_scheduling_mode;

	/* Adjust priority of all the contexts as per the new mode */
	list_for_each_entry(kctx, &kbdev->kctx_list, kctx_list_link)
		kbase_js_update_ctx_priority(kctx);

	spin_unlock_irqrestore(&kbdev->hwaccess_lock, flags);
	mutex_unlock(&kbdev->kctx_list_lock);

	dev_dbg(kbdev->dev, "JS ctx scheduling mode: %u\n", new_js_ctx_scheduling_mode);

	return count;
}

static DEVICE_ATTR_RW(js_ctx_scheduling_mode);

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
 * update_serialize_jobs_setting - Update the serialization setting for the
 *                                 submission of GPU jobs.
 *
 * @kbdev:  An instance of the GPU platform device, allocated from the probe
 *          method of the driver.
 * @buf:    Buffer containing the value written to the sysfs/debugfs file.
 * @count:  The number of bytes to write to the sysfs/debugfs file.
 *
 * This function is called when the serialize_jobs sysfs/debugfs file is
 * written to. It matches the requested setting against the available settings
 * and if a matching setting is found updates kbdev->serialize_jobs.
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t update_serialize_jobs_setting(struct kbase_device *kbdev,
					     const char *buf, size_t count)
{
	int i;
	bool valid = false;

	for (i = 0; i < NR_SERIALIZE_JOBS_SETTINGS; i++) {
		if (sysfs_streq(serialize_jobs_settings[i].name, buf)) {
			kbdev->serialize_jobs =
				serialize_jobs_settings[i].setting;
			valid = true;
			break;
		}
	}

	if (!valid) {
		dev_err(kbdev->dev, "serialize_jobs: invalid setting");
		return -EINVAL;
	}

	return count;
}

#if IS_ENABLED(CONFIG_DEBUG_FS)
/**
 * kbasep_serialize_jobs_seq_debugfs_show - Show callback for the serialize_jobs
 *					    debugfs file
 * @sfile: seq_file pointer
 * @data:  Private callback data
 *
 * This function is called to get the contents of the serialize_jobs debugfs
 * file. This is a list of the available settings with the currently active one
 * surrounded by square brackets.
 *
 * Return: 0 on success, or an error code on error
 */
static int kbasep_serialize_jobs_seq_debugfs_show(struct seq_file *sfile,
						  void *data)
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

	CSTD_UNUSED(ppos);

	count = min_t(size_t, sizeof(buf) - 1, count);
	if (copy_from_user(buf, ubuf, count))
		return -EFAULT;

	buf[count] = 0;

	return update_serialize_jobs_setting(kbdev, buf, count);
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
	return single_open(file, kbasep_serialize_jobs_seq_debugfs_show,
			   in->i_private);
}

static const struct file_operations kbasep_serialize_jobs_debugfs_fops = {
	.owner = THIS_MODULE,
	.open = kbasep_serialize_jobs_debugfs_open,
	.read = seq_read,
	.write = kbasep_serialize_jobs_debugfs_write,
	.llseek = seq_lseek,
	.release = single_release,
};

#endif /* CONFIG_DEBUG_FS */

/**
 * show_serialize_jobs_sysfs - Show callback for serialize_jobs sysfs file.
 *
 * @dev:	The device this sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The output buffer for the sysfs file contents
 *
 * This function is called to get the contents of the serialize_jobs sysfs
 * file. This is a list of the available settings with the currently active
 * one surrounded by square brackets.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t show_serialize_jobs_sysfs(struct device *dev,
					 struct device_attribute *attr,
					 char *buf)
{
	struct kbase_device *kbdev = to_kbase_device(dev);
	ssize_t ret = 0;
	int i;

	for (i = 0; i < NR_SERIALIZE_JOBS_SETTINGS; i++) {
		if (kbdev->serialize_jobs ==
				serialize_jobs_settings[i].setting)
			ret += scnprintf(buf + ret, PAGE_SIZE - ret, "[%s]",
					 serialize_jobs_settings[i].name);
		else
			ret += scnprintf(buf + ret, PAGE_SIZE - ret, "%s ",
					 serialize_jobs_settings[i].name);
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
 * store_serialize_jobs_sysfs - Store callback for serialize_jobs sysfs file.
 *
 * @dev:	The device this sysfs file is for
 * @attr:	The attributes of the sysfs file
 * @buf:	The value written to the sysfs file
 * @count:	The number of bytes to write to the sysfs file
 *
 * This function is called when the serialize_jobs sysfs file is written to.
 * It matches the requested setting against the available settings and if a
 * matching setting is found updates kbdev->serialize_jobs.
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t store_serialize_jobs_sysfs(struct device *dev,
					  struct device_attribute *attr,
					  const char *buf, size_t count)
{
	return update_serialize_jobs_setting(to_kbase_device(dev), buf, count);
}

static DEVICE_ATTR(serialize_jobs, 0600, show_serialize_jobs_sysfs,
		   store_serialize_jobs_sysfs);
#endif /* !MALI_USE_CSF */

static void kbasep_protected_mode_hwcnt_disable_worker(struct work_struct *data)
{
	struct kbase_device *kbdev = container_of(data, struct kbase_device,
		protected_mode_hwcnt_disable_work);
	spinlock_t *backend_lock;
	unsigned long flags;

	bool do_disable;

#if MALI_USE_CSF
	backend_lock = &kbdev->csf.scheduler.interrupt_lock;
#else
	backend_lock = &kbdev->hwaccess_lock;
#endif

	spin_lock_irqsave(backend_lock, flags);
	do_disable = !kbdev->protected_mode_hwcnt_desired &&
		!kbdev->protected_mode_hwcnt_disabled;
	spin_unlock_irqrestore(backend_lock, flags);

	if (!do_disable)
		return;

	kbase_hwcnt_context_disable(kbdev->hwcnt_gpu_ctx);

	spin_lock_irqsave(backend_lock, flags);
	do_disable = !kbdev->protected_mode_hwcnt_desired &&
		!kbdev->protected_mode_hwcnt_disabled;

	if (do_disable) {
		/* Protected mode state did not change while we were doing the
		 * disable, so commit the work we just performed and continue
		 * the state machine.
		 */
		kbdev->protected_mode_hwcnt_disabled = true;
#if !MALI_USE_CSF
		kbase_backend_slot_update(kbdev);
#endif /* !MALI_USE_CSF */
	} else {
		/* Protected mode state was updated while we were doing the
		 * disable, so we need to undo the disable we just performed.
		 */
		kbase_hwcnt_context_enable(kbdev->hwcnt_gpu_ctx);
	}

	spin_unlock_irqrestore(backend_lock, flags);
}

#ifndef PLATFORM_PROTECTED_CALLBACKS
static int kbasep_protected_mode_enable(struct protected_mode_device *pdev)
{
	struct kbase_device *kbdev = pdev->data;

	return kbase_pm_protected_mode_enable(kbdev);
}

static int kbasep_protected_mode_disable(struct protected_mode_device *pdev)
{
	struct kbase_device *kbdev = pdev->data;

	return kbase_pm_protected_mode_disable(kbdev);
}

static const struct protected_mode_ops kbasep_native_protected_ops = {
	.protected_mode_enable = kbasep_protected_mode_enable,
	.protected_mode_disable = kbasep_protected_mode_disable
};

#define PLATFORM_PROTECTED_CALLBACKS (&kbasep_native_protected_ops)
#endif /* PLATFORM_PROTECTED_CALLBACKS */

int kbase_protected_mode_init(struct kbase_device *kbdev)
{
	/* Use native protected ops */
	kbdev->protected_dev = kzalloc(sizeof(*kbdev->protected_dev),
			GFP_KERNEL);
	if (!kbdev->protected_dev)
		return -ENOMEM;
	kbdev->protected_dev->data = kbdev;
	kbdev->protected_ops = PLATFORM_PROTECTED_CALLBACKS;
	INIT_WORK(&kbdev->protected_mode_hwcnt_disable_work,
		kbasep_protected_mode_hwcnt_disable_worker);
	kbdev->protected_mode_hwcnt_desired = true;
	kbdev->protected_mode_hwcnt_disabled = false;
	return 0;
}

void kbase_protected_mode_term(struct kbase_device *kbdev)
{
	cancel_work_sync(&kbdev->protected_mode_hwcnt_disable_work);
	kfree(kbdev->protected_dev);
}

#if IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
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

int registers_map(struct kbase_device * const kbdev)
{
	/* the first memory resource is the physical address of the GPU
	 * registers.
	 */
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

#if MALI_USE_CSF
	if (kbdev->reg_size <
		(CSF_HW_DOORBELL_PAGE_OFFSET +
		 CSF_NUM_DOORBELL * CSF_HW_DOORBELL_PAGE_SIZE)) {
		dev_err(kbdev->dev, "Insufficient register space, will override to the required size\n");
		kbdev->reg_size = CSF_HW_DOORBELL_PAGE_OFFSET +
				CSF_NUM_DOORBELL * CSF_HW_DOORBELL_PAGE_SIZE;
	}
#endif

	err = kbase_common_reg_map(kbdev);
	if (err) {
		dev_err(kbdev->dev, "Failed to map registers\n");
		return err;
	}

	return 0;
}

void registers_unmap(struct kbase_device *kbdev)
{
	kbase_common_reg_unmap(kbdev);
}

#if defined(CONFIG_MALI_ARBITER_SUPPORT) && defined(CONFIG_OF)

static bool kbase_is_pm_enabled(const struct device_node *gpu_node)
{
	const struct device_node *power_model_node;
	const void *cooling_cells_node;
	const void *operating_point_node;
	bool is_pm_enable = false;

	power_model_node = of_get_child_by_name(gpu_node,
		"power_model");
	if (power_model_node)
		is_pm_enable = true;

	cooling_cells_node = of_get_property(gpu_node,
		"#cooling-cells", NULL);
	if (cooling_cells_node)
		is_pm_enable = true;

	operating_point_node = of_get_property(gpu_node,
		"operating-points", NULL);
	if (operating_point_node)
		is_pm_enable = true;

	return is_pm_enable;
}

static bool kbase_is_pv_enabled(const struct device_node *gpu_node)
{
	const void *arbiter_if_node;

	arbiter_if_node = of_get_property(gpu_node,
		"arbiter_if", NULL);

	return arbiter_if_node ? true : false;
}

static bool kbase_is_full_coherency_enabled(const struct device_node *gpu_node)
{
	const void *coherency_dts;
	u32 coherency;

	coherency_dts = of_get_property(gpu_node,
					"system-coherency",
					NULL);
	if (coherency_dts) {
		coherency = be32_to_cpup(coherency_dts);
		if (coherency == COHERENCY_ACE)
			return true;
	}
	return false;
}

#endif /* CONFIG_MALI_ARBITER_SUPPORT && CONFIG_OF */

int kbase_device_pm_init(struct kbase_device *kbdev)
{
	int err = 0;

#if defined(CONFIG_MALI_ARBITER_SUPPORT) && defined(CONFIG_OF)

	u32 gpu_id;
	u32 product_id;
	u32 gpu_model_id;

	if (kbase_is_pv_enabled(kbdev->dev->of_node)) {
		dev_info(kbdev->dev, "Arbitration interface enabled\n");
		if (kbase_is_pm_enabled(kbdev->dev->of_node)) {
			/* Arbitration AND power management invalid */
			dev_err(kbdev->dev, "Invalid combination of arbitration AND power management\n");
			return -EPERM;
		}
		if (kbase_is_full_coherency_enabled(kbdev->dev->of_node)) {
			/* Arbitration AND full coherency invalid */
			dev_err(kbdev->dev, "Invalid combination of arbitration AND full coherency\n");
			return -EPERM;
		}
		err = kbase_arbiter_pm_early_init(kbdev);
		if (err == 0) {
			/* Check if Arbitration is running on
			 * supported GPU platform
			 */
			kbase_pm_register_access_enable(kbdev);
			gpu_id = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_ID));
			kbase_pm_register_access_disable(kbdev);
			product_id =
				KBASE_UBFX32(gpu_id, KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT, 16);
			gpu_model_id = GPU_ID2_MODEL_MATCH_VALUE(product_id);

			if (gpu_model_id != GPU_ID2_PRODUCT_TGOX
				&& gpu_model_id != GPU_ID2_PRODUCT_TNOX
				&& gpu_model_id != GPU_ID2_PRODUCT_TBAX) {
				kbase_arbiter_pm_early_term(kbdev);
				dev_err(kbdev->dev, "GPU platform not suitable for arbitration\n");
				return -EPERM;
			}
		}
	} else {
		kbdev->arb.arb_if = NULL;
		kbdev->arb.arb_dev = NULL;
		err = power_control_init(kbdev);
	}
#else
	err = power_control_init(kbdev);
#endif /* CONFIG_MALI_ARBITER_SUPPORT && CONFIG_OF */
	return err;
}

void kbase_device_pm_term(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_ARBITER_SUPPORT
#if IS_ENABLED(CONFIG_OF)
	if (kbase_is_pv_enabled(kbdev->dev->of_node))
		kbase_arbiter_pm_early_term(kbdev);
	else
		power_control_term(kbdev);
#endif /* CONFIG_OF */
#else
	power_control_term(kbdev);
#endif
}

int power_control_init(struct kbase_device *kbdev)
{
#ifndef CONFIG_OF
	/* Power control initialization requires at least the capability to get
	 * regulators and clocks from the device tree, as well as parsing
	 * arrays of unsigned integer values.
	 *
	 * The whole initialization process shall simply be skipped if the
	 * minimum capability is not available.
	 */
	return 0;
#else
	struct platform_device *pdev;
	int err = 0;
	unsigned int i;
#if defined(CONFIG_REGULATOR)
	static const char * const regulator_names[] = {
		"mali", "mem"
	};
#endif /* CONFIG_REGULATOR */

	if (!kbdev)
		return -ENODEV;

	pdev = to_platform_device(kbdev->dev);

#if defined(CONFIG_REGULATOR)
	/* Since the error code EPROBE_DEFER causes the entire probing
	 * procedure to be restarted from scratch at a later time,
	 * all regulators will be released before returning.
	 *
	 * Any other error is ignored and the driver will continue
	 * operating with a partial initialization of regulators.
	 */
	for (i = 0; i < ARRAY_SIZE(regulator_names); i++) {
		kbdev->regulators[i] = regulator_get_optional(kbdev->dev,
			regulator_names[i]);
		if (IS_ERR(kbdev->regulators[i])) {
			err = PTR_ERR(kbdev->regulators[i]);
			kbdev->regulators[i] = NULL;
			break;
		}
	}
	if (err == -EPROBE_DEFER) {
		while (i > 0)
			regulator_put(kbdev->regulators[--i]);
		return err;
	}

	kbdev->nr_regulators = i;
	dev_dbg(&pdev->dev, "Regulators probed: %u\n", kbdev->nr_regulators);
#endif

	/* Having more clocks than regulators is acceptable, while the
	 * opposite shall not happen.
	 *
	 * Since the error code EPROBE_DEFER causes the entire probing
	 * procedure to be restarted from scratch at a later time,
	 * all clocks and regulators will be released before returning.
	 *
	 * Any other error is ignored and the driver will continue
	 * operating with a partial initialization of clocks.
	 */
	for (i = 0; i < BASE_MAX_NR_CLOCKS_REGULATORS; i++) {
		kbdev->clocks[i] = of_clk_get(kbdev->dev->of_node, i);
		if (IS_ERR(kbdev->clocks[i])) {
			err = PTR_ERR(kbdev->clocks[i]);
			kbdev->clocks[i] = NULL;
			break;
		}

		err = clk_prepare(kbdev->clocks[i]);
		if (err) {
			dev_err(kbdev->dev,
				"Failed to prepare and enable clock (%d)\n",
				err);
			clk_put(kbdev->clocks[i]);
			break;
		}
	}
	if (err == -EPROBE_DEFER) {
		while (i > 0) {
			clk_disable_unprepare(kbdev->clocks[--i]);
			clk_put(kbdev->clocks[i]);
		}
		goto clocks_probe_defer;
	}

	kbdev->nr_clocks = i;
	dev_dbg(&pdev->dev, "Clocks probed: %u\n", kbdev->nr_clocks);

	/* Any error in parsing the OPP table from the device file
	 * shall be ignored. The fact that the table may be absent or wrong
	 * on the device tree of the platform shouldn't prevent the driver
	 * from completing its initialization.
	 */
#if defined(CONFIG_PM_OPP)
#if ((KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE) && \
	defined(CONFIG_REGULATOR))
	if (kbdev->nr_regulators > 0) {
		kbdev->opp_table =
			dev_pm_opp_set_regulators(kbdev->dev, regulator_names,
						  kbdev->nr_regulators);
		if (IS_ERR(kbdev->opp_table)) {
			dev_err(kbdev->dev, "Failed to set regulators\n");
			return 0;
		}
		kbdev->opp_table =
			dev_pm_opp_register_set_opp_helper(kbdev->dev,
							   kbase_devfreq_opp_helper);
		if (IS_ERR(kbdev->opp_table)) {
			dev_pm_opp_put_regulators(kbdev->opp_table);
			kbdev->opp_table = NULL;
			dev_err(kbdev->dev, "Failed to set opp helper\n");
			return 0;
		}
	}
#endif /* (KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE */
#ifdef CONFIG_ARCH_ROCKCHIP
       err = kbase_platform_rk_init_opp_table(kbdev);
       if (err)
               dev_err(kbdev->dev, "Failed to init_opp_table (%d)\n", err);
#else
	err = dev_pm_opp_of_add_table(kbdev->dev);
	CSTD_UNUSED(err);
#endif
#endif /* CONFIG_PM_OPP */
	return 0;

#if defined(CONFIG_PM_OPP) &&                                                                      \
	((KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE) && defined(CONFIG_REGULATOR))
	for (i = 0; i < BASE_MAX_NR_CLOCKS_REGULATORS; i++) {
		if (kbdev->clocks[i]) {
			if (__clk_is_enabled(kbdev->clocks[i]))
				clk_disable_unprepare(kbdev->clocks[i]);
			clk_put(kbdev->clocks[i]);
			kbdev->clocks[i] = NULL;
		} else
			break;
	}
#endif

clocks_probe_defer:
#if defined(CONFIG_REGULATOR)
	for (i = 0; i < BASE_MAX_NR_CLOCKS_REGULATORS; i++)
		regulator_put(kbdev->regulators[i]);
#endif
	return err;
#endif /* CONFIG_OF */
}

void power_control_term(struct kbase_device *kbdev)
{
	unsigned int i;

#if defined(CONFIG_PM_OPP)
	dev_pm_opp_of_remove_table(kbdev->dev);
#if ((KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE) && \
	defined(CONFIG_REGULATOR))
	if (!IS_ERR_OR_NULL(kbdev->opp_table)) {
		dev_pm_opp_unregister_set_opp_helper(kbdev->opp_table);
		dev_pm_opp_put_regulators(kbdev->opp_table);
	}
#endif /* (KERNEL_VERSION(4, 10, 0) <= LINUX_VERSION_CODE */
#endif /* CONFIG_PM_OPP */

	for (i = 0; i < BASE_MAX_NR_CLOCKS_REGULATORS; i++) {
		if (kbdev->clocks[i]) {
			clk_unprepare(kbdev->clocks[i]);
			clk_put(kbdev->clocks[i]);
			kbdev->clocks[i] = NULL;
		} else
			break;
	}

#if defined(CONFIG_OF) && defined(CONFIG_REGULATOR)
	for (i = 0; i < BASE_MAX_NR_CLOCKS_REGULATORS; i++) {
		if (kbdev->regulators[i]) {
			regulator_put(kbdev->regulators[i]);
			kbdev->regulators[i] = NULL;
		}
	}
#endif
}

#if IS_ENABLED(CONFIG_DEBUG_FS)

static void trigger_reset(struct kbase_device *kbdev)
{
	kbase_pm_context_active(kbdev);
	if (kbase_prepare_to_reset_gpu(kbdev, RESET_FLAGS_NONE))
		kbase_reset_gpu(kbdev);
	kbase_pm_context_idle(kbdev);
}

#define MAKE_QUIRK_ACCESSORS(type) \
static int type##_quirks_set(void *data, u64 val) \
{ \
	struct kbase_device *kbdev; \
	kbdev = (struct kbase_device *)data; \
	kbdev->hw_quirks_##type = (u32)val; \
	trigger_reset(kbdev); \
	return 0; \
} \
\
static int type##_quirks_get(void *data, u64 *val) \
{ \
	struct kbase_device *kbdev; \
	kbdev = (struct kbase_device *)data; \
	*val = kbdev->hw_quirks_##type; \
	return 0; \
} \
DEFINE_DEBUGFS_ATTRIBUTE(fops_##type##_quirks, type##_quirks_get, \
			 type##_quirks_set, "%llu\n")

MAKE_QUIRK_ACCESSORS(sc);
MAKE_QUIRK_ACCESSORS(tiler);
MAKE_QUIRK_ACCESSORS(mmu);
MAKE_QUIRK_ACCESSORS(gpu);

/**
 * kbase_device_debugfs_reset_write() - Reset the GPU
 *
 * @data:           Pointer to the Kbase device.
 * @wait_for_reset: Value written to the file.
 *
 * This function will perform the GPU reset, and if the value written to
 * the file is 1 it will also wait for the reset to complete.
 *
 * Return: 0 in case of no error otherwise a negative value.
 */
static int kbase_device_debugfs_reset_write(void *data, u64 wait_for_reset)
{
	struct kbase_device *kbdev = data;

	trigger_reset(kbdev);

	if (wait_for_reset == 1)
		return kbase_reset_gpu_wait(kbdev);

	return 0;
}

DEFINE_DEBUGFS_ATTRIBUTE(fops_trigger_reset, NULL, &kbase_device_debugfs_reset_write, "%llu\n");

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
	gpu_status = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_STATUS));
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
	.owner = THIS_MODULE,
	.open = simple_open,
	.read = debugfs_protected_debug_mode_read,
	.llseek = default_llseek,
};

static int kbase_device_debugfs_mem_pool_max_size_show(struct seq_file *sfile,
	void *data)
{
	CSTD_UNUSED(data);
	return kbase_debugfs_helper_seq_read(sfile,
		MEMORY_GROUP_MANAGER_NR_GROUPS,
		kbase_mem_pool_config_debugfs_max_size);
}

static ssize_t kbase_device_debugfs_mem_pool_max_size_write(struct file *file,
		const char __user *ubuf, size_t count, loff_t *ppos)
{
	int err = 0;

	CSTD_UNUSED(ppos);
	err = kbase_debugfs_helper_seq_write(file, ubuf, count,
		MEMORY_GROUP_MANAGER_NR_GROUPS,
		kbase_mem_pool_config_debugfs_set_max_size);

	return err ? err : count;
}

static int kbase_device_debugfs_mem_pool_max_size_open(struct inode *in,
	struct file *file)
{
	return single_open(file, kbase_device_debugfs_mem_pool_max_size_show,
		in->i_private);
}

static const struct file_operations
	kbase_device_debugfs_mem_pool_max_size_fops = {
	.owner = THIS_MODULE,
	.open = kbase_device_debugfs_mem_pool_max_size_open,
	.read = seq_read,
	.write = kbase_device_debugfs_mem_pool_max_size_write,
	.llseek = seq_lseek,
	.release = single_release,
};

/**
 * debugfs_ctx_defaults_init - Create the default configuration of new contexts in debugfs
 * @kbdev: An instance of the GPU platform device, allocated from the probe method of the driver.
 * Return: A pointer to the last dentry that it tried to create, whether successful or not.
 *         Could be NULL or encode another error value.
 */
static struct dentry *debugfs_ctx_defaults_init(struct kbase_device *const kbdev)
{
	/* prevent unprivileged use of debug file system
	 * in old kernel version
	 */
	const mode_t mode = 0644;
	struct dentry *dentry = debugfs_create_dir("defaults", kbdev->debugfs_ctx_directory);
	struct dentry *debugfs_ctx_defaults_directory = dentry;

	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Couldn't create mali debugfs ctx defaults directory\n");
		return dentry;
	}

	debugfs_create_bool("infinite_cache", mode,
			debugfs_ctx_defaults_directory,
			&kbdev->infinite_cache_active_default);

	dentry = debugfs_create_file("mem_pool_max_size", mode, debugfs_ctx_defaults_directory,
				   &kbdev->mem_pool_defaults.small,
				   &kbase_device_debugfs_mem_pool_max_size_fops);
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Unable to create mem_pool_max_size debugfs entry\n");
		return dentry;
	}

	dentry = debugfs_create_file("lp_mem_pool_max_size", mode, debugfs_ctx_defaults_directory,
				   &kbdev->mem_pool_defaults.large,
				   &kbase_device_debugfs_mem_pool_max_size_fops);
	if (IS_ERR_OR_NULL(dentry))
		dev_err(kbdev->dev, "Unable to create lp_mem_pool_max_size debugfs entry\n");

	return dentry;
}

/**
 * init_debugfs - Create device-wide debugfs directories and files for the Mali driver
 * @kbdev: An instance of the GPU platform device, allocated from the probe method of the driver.
 * Return: A pointer to the last dentry that it tried to create, whether successful or not.
 *         Could be NULL or encode another error value.
 */
static struct dentry *init_debugfs(struct kbase_device *kbdev)
{
	struct dentry *dentry = debugfs_create_dir(kbdev->devname, NULL);

	kbdev->mali_debugfs_directory = dentry;
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev,
			"Couldn't create mali debugfs directory: %s\n",
			kbdev->devname);
		return dentry;
	}

	dentry = debugfs_create_dir("ctx", kbdev->mali_debugfs_directory);
	kbdev->debugfs_ctx_directory = dentry;
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Couldn't create mali debugfs ctx directory\n");
		return dentry;
	}

	dentry = debugfs_create_dir("instrumentation", kbdev->mali_debugfs_directory);
	kbdev->debugfs_instr_directory = dentry;
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Couldn't create mali debugfs instrumentation directory\n");
		return dentry;
	}

	kbasep_regs_history_debugfs_init(kbdev);

#if MALI_USE_CSF
	kbase_debug_csf_fault_debugfs_init(kbdev);
#else /* MALI_USE_CSF */
	kbase_debug_job_fault_debugfs_init(kbdev);
#endif /* !MALI_USE_CSF */

	kbasep_gpu_memory_debugfs_init(kbdev);
	kbase_as_fault_debugfs_init(kbdev);
#ifdef CONFIG_MALI_PRFCNT_SET_SELECT_VIA_DEBUG_FS
	kbase_instr_backend_debugfs_init(kbdev);
#endif
	kbase_pbha_debugfs_init(kbdev);

	/* fops_* variables created by invocations of macro
	 * MAKE_QUIRK_ACCESSORS() above.
	 */
	dentry = debugfs_create_file("quirks_sc", 0644,
			kbdev->mali_debugfs_directory, kbdev,
			&fops_sc_quirks);
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Unable to create quirks_sc debugfs entry\n");
		return dentry;
	}

	dentry = debugfs_create_file("quirks_tiler", 0644,
			kbdev->mali_debugfs_directory, kbdev,
			&fops_tiler_quirks);
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Unable to create quirks_tiler debugfs entry\n");
		return dentry;
	}

	dentry = debugfs_create_file("quirks_mmu", 0644,
			kbdev->mali_debugfs_directory, kbdev,
			&fops_mmu_quirks);
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Unable to create quirks_mmu debugfs entry\n");
		return dentry;
	}

	dentry = debugfs_create_file("quirks_gpu", 0644, kbdev->mali_debugfs_directory,
			    kbdev, &fops_gpu_quirks);
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Unable to create quirks_gpu debugfs entry\n");
		return dentry;
	}

	dentry = debugfs_ctx_defaults_init(kbdev);
	if (IS_ERR_OR_NULL(dentry))
		return dentry;

	if (kbase_hw_has_feature(kbdev, BASE_HW_FEATURE_PROTECTED_DEBUG_MODE)) {
		dentry = debugfs_create_file("protected_debug_mode", 0444,
				kbdev->mali_debugfs_directory, kbdev,
				&fops_protected_debug_mode);
		if (IS_ERR_OR_NULL(dentry)) {
			dev_err(kbdev->dev, "Unable to create protected_debug_mode debugfs entry\n");
			return dentry;
		}
	}

	dentry = debugfs_create_file("reset", 0644,
			kbdev->mali_debugfs_directory, kbdev,
			&fops_trigger_reset);
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Unable to create reset debugfs entry\n");
		return dentry;
	}

	kbase_ktrace_debugfs_init(kbdev);

#ifdef CONFIG_MALI_BIFROST_DEVFREQ
#if IS_ENABLED(CONFIG_DEVFREQ_THERMAL)
	if (kbdev->devfreq && !kbdev->model_data &&
	    !kbdev->dfc_power.dyn_power_coeff)
		kbase_ipa_debugfs_init(kbdev);
#endif /* CONFIG_DEVFREQ_THERMAL */
#endif /* CONFIG_MALI_BIFROST_DEVFREQ */

#if !MALI_USE_CSF
	dentry = debugfs_create_file("serialize_jobs", 0644,
			kbdev->mali_debugfs_directory, kbdev,
			&kbasep_serialize_jobs_debugfs_fops);
	if (IS_ERR_OR_NULL(dentry)) {
		dev_err(kbdev->dev, "Unable to create serialize_jobs debugfs entry\n");
		return dentry;
	}
	kbase_timeline_io_debugfs_init(kbdev);
#endif
	kbase_dvfs_status_debugfs_init(kbdev);


	return dentry;
}

int kbase_device_debugfs_init(struct kbase_device *kbdev)
{
	struct dentry *dentry = init_debugfs(kbdev);

	if (IS_ERR_OR_NULL(dentry)) {
		debugfs_remove_recursive(kbdev->mali_debugfs_directory);
		return IS_ERR(dentry) ? PTR_ERR(dentry) : -ENOMEM;
	}
	return 0;
}

void kbase_device_debugfs_term(struct kbase_device *kbdev)
{
	debugfs_remove_recursive(kbdev->mali_debugfs_directory);
}
#endif /* CONFIG_DEBUG_FS */

int kbase_device_coherency_init(struct kbase_device *kbdev)
{
#if IS_ENABLED(CONFIG_OF)
	u32 supported_coherency_bitmap =
		kbdev->gpu_props.props.raw_props.coherency_mode;
	const void *coherency_override_dts;
	bool dma_coherent;
	u32 override_coherency, gpu_id;
	unsigned int prod_id;

	gpu_id = kbdev->gpu_props.props.raw_props.gpu_id;
	gpu_id &= GPU_ID_VERSION_PRODUCT_ID;
	prod_id = gpu_id >> KBASE_GPU_ID_VERSION_PRODUCT_ID_SHIFT;

	/* Only for tMIx :
	 * (COHERENCY_ACE_LITE | COHERENCY_ACE) was incorrectly
	 * documented for tMIx so force correct value here.
	 */
	if (GPU_ID2_MODEL_MATCH_VALUE(prod_id) ==
			GPU_ID2_PRODUCT_TMIX)
		if (supported_coherency_bitmap ==
				COHERENCY_FEATURE_BIT(COHERENCY_ACE))
			supported_coherency_bitmap |=
				COHERENCY_FEATURE_BIT(COHERENCY_ACE_LITE);

#endif /* CONFIG_OF */

	kbdev->system_coherency = COHERENCY_NONE;

	/* device tree may override the coherency */
#if IS_ENABLED(CONFIG_OF)
	/* treat "dma-coherency" as a synonym for ACE-lite */
	dma_coherent = of_dma_is_coherent(kbdev->dev->of_node);
	coherency_override_dts = of_get_property(kbdev->dev->of_node,
						"system-coherency",
						NULL);
	if (coherency_override_dts || dma_coherent) {
		if (coherency_override_dts) {
			override_coherency = be32_to_cpup(coherency_override_dts);
			if (dma_coherent && override_coherency != COHERENCY_ACE_LITE) {
				dev_err(kbdev->dev,
					"system-coherency needs to be 0 when dma-coherent is set\n");
				return -EINVAL;
			}
		} else {
			/* dma-coherent set and system-coherency not specified */
			override_coherency = COHERENCY_ACE_LITE;
		}

#if MALI_USE_CSF && !IS_ENABLED(CONFIG_MALI_BIFROST_NO_MALI)
		/* ACE coherency mode is not supported by Driver on CSF GPUs.
		 * Return an error to signal the invalid device tree configuration.
		 */
		if (override_coherency == COHERENCY_ACE) {
			dev_err(kbdev->dev,
				"ACE coherency not supported, wrong DT configuration");
			return -EINVAL;
		}
#endif

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

	return 0;
}


#if MALI_USE_CSF
/**
 * csg_scheduling_period_store - Store callback for the csg_scheduling_period
 * sysfs file.
 * @dev:   The device with sysfs file is for
 * @attr:  The attributes of the sysfs file
 * @buf:   The value written to the sysfs file
 * @count: The number of bytes written to the sysfs file
 *
 * This function is called when the csg_scheduling_period sysfs file is written
 * to. It checks the data written, and if valid updates the reset timeout.
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t csg_scheduling_period_store(struct device *dev,
					   struct device_attribute *attr,
					   const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	unsigned int csg_scheduling_period;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = kstrtouint(buf, 0, &csg_scheduling_period);
	if (ret || csg_scheduling_period == 0) {
		dev_err(kbdev->dev,
			"Couldn't process csg_scheduling_period write operation.\n"
			"Use format 'csg_scheduling_period_ms', and csg_scheduling_period_ms > 0\n");
		return -EINVAL;
	}

	kbase_csf_scheduler_lock(kbdev);
	kbdev->csf.scheduler.csg_scheduling_period_ms = csg_scheduling_period;
	dev_dbg(kbdev->dev, "CSG scheduling period: %ums\n",
		csg_scheduling_period);
	kbase_csf_scheduler_unlock(kbdev);

	return count;
}

/**
 * csg_scheduling_period_show - Show callback for the csg_scheduling_period
 * sysfs entry.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the GPU information.
 *
 * This function is called to get the current reset timeout.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t csg_scheduling_period_show(struct device *dev,
					  struct device_attribute *attr,
					  char *const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = scnprintf(buf, PAGE_SIZE, "%u\n",
			kbdev->csf.scheduler.csg_scheduling_period_ms);

	return ret;
}

static DEVICE_ATTR_RW(csg_scheduling_period);

/**
 * fw_timeout_store - Store callback for the fw_timeout sysfs file.
 * @dev:   The device with sysfs file is for
 * @attr:  The attributes of the sysfs file
 * @buf:   The value written to the sysfs file
 * @count: The number of bytes written to the sysfs file
 *
 * This function is called when the fw_timeout sysfs file is written to. It
 * checks the data written, and if valid updates the reset timeout.
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t fw_timeout_store(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	struct kbase_device *kbdev;
	int ret;
	unsigned int fw_timeout;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = kstrtouint(buf, 0, &fw_timeout);
	if (ret || fw_timeout == 0) {
		dev_err(kbdev->dev,
			"Couldn't process fw_timeout write operation.\n"
			"Use format 'fw_timeout_ms', and fw_timeout_ms > 0\n"
			"Default fw_timeout: %u",
			kbase_get_timeout_ms(kbdev, CSF_FIRMWARE_PING_TIMEOUT));
		return -EINVAL;
	}

	kbase_csf_scheduler_lock(kbdev);
	kbdev->csf.fw_timeout_ms = fw_timeout;
	kbase_csf_scheduler_unlock(kbdev);
	dev_dbg(kbdev->dev, "Firmware timeout: %ums\n", fw_timeout);

	return count;
}

/**
 * fw_timeout_show - Show callback for the firmware timeout sysfs entry.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the GPU information.
 *
 * This function is called to get the current reset timeout.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t fw_timeout_show(struct device *dev,
			       struct device_attribute *attr, char *const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	ret = scnprintf(buf, PAGE_SIZE, "%u\n", kbdev->csf.fw_timeout_ms);

	return ret;
}

static DEVICE_ATTR_RW(fw_timeout);

/**
 * idle_hysteresis_time_store - Store callback for CSF idle_hysteresis_time
 *                            sysfs file.
 * @dev:   The device with sysfs file is for
 * @attr:  The attributes of the sysfs file
 * @buf:   The value written to the sysfs file
 * @count: The number of bytes written to the sysfs file
 *
 * This function is called when the idle_hysteresis_time sysfs file is
 * written to.
 *
 * This file contains values of the idle hysteresis duration.
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t idle_hysteresis_time_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t count)
{
	struct kbase_device *kbdev;
	u32 dur = 0;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	if (kstrtou32(buf, 0, &dur)) {
		dev_err(kbdev->dev, "Couldn't process idle_hysteresis_time write operation.\n"
				"Use format <idle_hysteresis_time>\n");
		return -EINVAL;
	}

	kbase_csf_firmware_set_gpu_idle_hysteresis_time(kbdev, dur);

	return count;
}

/**
 * idle_hysteresis_time_show - Show callback for CSF idle_hysteresis_time
 *                             sysfs entry.
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer to receive the GPU information.
 *
 * This function is called to get the current idle hysteresis duration in ms.
 *
 * Return: The number of bytes output to @buf.
 */
static ssize_t idle_hysteresis_time_show(struct device *dev,
		struct device_attribute *attr, char * const buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;
	u32 dur;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
		return -ENODEV;

	dur = kbase_csf_firmware_get_gpu_idle_hysteresis_time(kbdev);
	ret = scnprintf(buf, PAGE_SIZE, "%u\n", dur);

	return ret;
}

static DEVICE_ATTR_RW(idle_hysteresis_time);

/**
 * mcu_shader_pwroff_timeout_show - Get the MCU shader Core power-off time value.
 *
 * @dev:  The device this sysfs file is for.
 * @attr: The attributes of the sysfs file.
 * @buf:  The output buffer for the sysfs file contents
 *
 * Get the internally recorded MCU shader Core power-off (nominal) timeout value.
 * The unit of the value is in micro-seconds.
 *
 * Return: The number of bytes output to @buf if the
 *         function succeeded. A Negative value on failure.
 */
static ssize_t mcu_shader_pwroff_timeout_show(struct device *dev, struct device_attribute *attr,
					      char *const buf)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	u32 pwroff;

	if (!kbdev)
		return -ENODEV;

	pwroff = kbase_csf_firmware_get_mcu_core_pwroff_time(kbdev);
	return scnprintf(buf, PAGE_SIZE, "%u\n", pwroff);
}

/**
 * mcu_shader_pwroff_timeout_store - Set the MCU shader core power-off time value.
 *
 * @dev:   The device with sysfs file is for
 * @attr:  The attributes of the sysfs file
 * @buf:   The value written to the sysfs file
 * @count: The number of bytes to write to the sysfs file
 *
 * The duration value (unit: micro-seconds) for configuring MCU Shader Core
 * timer, when the shader cores' power transitions are delegated to the
 * MCU (normal operational mode)
 *
 * Return: @count if the function succeeded. An error code on failure.
 */
static ssize_t mcu_shader_pwroff_timeout_store(struct device *dev, struct device_attribute *attr,
					       const char *buf, size_t count)
{
	struct kbase_device *kbdev = dev_get_drvdata(dev);
	u32 dur;

	if (!kbdev)
		return -ENODEV;

	if (kstrtouint(buf, 0, &dur))
		return -EINVAL;

	kbase_csf_firmware_set_mcu_core_pwroff_time(kbdev, dur);

	return count;
}

static DEVICE_ATTR_RW(mcu_shader_pwroff_timeout);

#endif /* MALI_USE_CSF */

static struct attribute *kbase_scheduling_attrs[] = {
#if !MALI_USE_CSF
	&dev_attr_serialize_jobs.attr,
#endif /* !MALI_USE_CSF */
	NULL
};

static struct attribute *kbase_attrs[] = {
#ifdef CONFIG_MALI_BIFROST_DEBUG
	&dev_attr_debug_command.attr,
#if !MALI_USE_CSF
	&dev_attr_js_softstop_always.attr,
#endif /* !MALI_USE_CSF */
#endif
#if !MALI_USE_CSF
	&dev_attr_js_timeouts.attr,
	&dev_attr_soft_job_timeout.attr,
#endif /* !MALI_USE_CSF */
	&dev_attr_gpuinfo.attr,
	&dev_attr_dvfs_period.attr,
	&dev_attr_pm_poweroff.attr,
	&dev_attr_reset_timeout.attr,
#if !MALI_USE_CSF
	&dev_attr_js_scheduling_period.attr,
#else
	&dev_attr_csg_scheduling_period.attr,
	&dev_attr_fw_timeout.attr,
	&dev_attr_idle_hysteresis_time.attr,
	&dev_attr_mcu_shader_pwroff_timeout.attr,
#endif /* !MALI_USE_CSF */
	&dev_attr_power_policy.attr,
	&dev_attr_core_mask.attr,
	&dev_attr_mem_pool_size.attr,
	&dev_attr_mem_pool_max_size.attr,
	&dev_attr_lp_mem_pool_size.attr,
	&dev_attr_lp_mem_pool_max_size.attr,
#if !MALI_USE_CSF
	&dev_attr_js_ctx_scheduling_mode.attr,
#endif /* !MALI_USE_CSF */
	NULL
};

static struct attribute *kbase_mempool_attrs[] = {
	&dev_attr_max_size.attr,
	&dev_attr_lp_max_size.attr,
	&dev_attr_ctx_default_max_size.attr,
	NULL
};

#define SYSFS_SCHEDULING_GROUP "scheduling"
static const struct attribute_group kbase_scheduling_attr_group = {
	.name = SYSFS_SCHEDULING_GROUP,
	.attrs = kbase_scheduling_attrs,
};

#define SYSFS_MEMPOOL_GROUP "mempool"
static const struct attribute_group kbase_mempool_attr_group = {
	.name = SYSFS_MEMPOOL_GROUP,
	.attrs = kbase_mempool_attrs,
};

static const struct attribute_group kbase_attr_group = {
	.attrs = kbase_attrs,
};

int kbase_sysfs_init(struct kbase_device *kbdev)
{
	int err = 0;

	kbdev->mdev.minor = MISC_DYNAMIC_MINOR;
	kbdev->mdev.name = kbdev->devname;
	kbdev->mdev.fops = &kbase_fops;
	kbdev->mdev.parent = get_device(kbdev->dev);
	kbdev->mdev.mode = 0666;

	err = sysfs_create_group(&kbdev->dev->kobj, &kbase_attr_group);
	if (err)
		return err;

	err = sysfs_create_group(&kbdev->dev->kobj,
			&kbase_scheduling_attr_group);
	if (err) {
		dev_err(kbdev->dev, "Creation of %s sysfs group failed",
			SYSFS_SCHEDULING_GROUP);
		sysfs_remove_group(&kbdev->dev->kobj,
			&kbase_attr_group);
		return err;
	}

	err = sysfs_create_group(&kbdev->dev->kobj,
			&kbase_mempool_attr_group);
	if (err) {
		dev_err(kbdev->dev, "Creation of %s sysfs group failed",
			SYSFS_MEMPOOL_GROUP);
		sysfs_remove_group(&kbdev->dev->kobj,
			&kbase_scheduling_attr_group);
		sysfs_remove_group(&kbdev->dev->kobj,
			&kbase_attr_group);
	}

	return err;
}

void kbase_sysfs_term(struct kbase_device *kbdev)
{
	sysfs_remove_group(&kbdev->dev->kobj, &kbase_mempool_attr_group);
	sysfs_remove_group(&kbdev->dev->kobj, &kbase_scheduling_attr_group);
	sysfs_remove_group(&kbdev->dev->kobj, &kbase_attr_group);
	put_device(kbdev->dev);
}

static int kbase_platform_device_remove(struct platform_device *pdev)
{
	struct kbase_device *kbdev = to_kbase_device(&pdev->dev);

	if (!kbdev)
		return -ENODEV;

	kbase_device_term(kbdev);
	dev_set_drvdata(kbdev->dev, NULL);
	kbase_device_free(kbdev);

	return 0;
}

void kbase_backend_devfreq_term(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_BIFROST_DEVFREQ
	if (kbdev->devfreq)
		kbase_devfreq_term(kbdev);
#endif
}

int kbase_backend_devfreq_init(struct kbase_device *kbdev)
{
#ifdef CONFIG_MALI_BIFROST_DEVFREQ
	/* Devfreq uses hardware counters, so must be initialized after it. */
	int err = kbase_devfreq_init(kbdev);

	if (err)
		dev_err(kbdev->dev, "Continuing without devfreq\n");
#endif /* CONFIG_MALI_BIFROST_DEVFREQ */
	return 0;
}

static int kbase_platform_device_probe(struct platform_device *pdev)
{
	struct kbase_device *kbdev;
	int err = 0;

	mali_kbase_print_cs_experimental();

	kbdev = kbase_device_alloc();
	if (!kbdev) {
		dev_err(&pdev->dev, "Allocate device failed\n");
		return -ENOMEM;
	}

	kbdev->dev = &pdev->dev;
	dev_set_drvdata(kbdev->dev, kbdev);
#if (KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE)
	mutex_lock(&kbase_probe_mutex);
#endif
	err = kbase_device_init(kbdev);

	if (err) {
		if (err == -EPROBE_DEFER)
			dev_info(kbdev->dev,
				"Device initialization Deferred\n");
		else
			dev_err(kbdev->dev, "Device initialization failed\n");

		dev_set_drvdata(kbdev->dev, NULL);
		kbase_device_free(kbdev);
#if (KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE)
		mutex_unlock(&kbase_probe_mutex);
#endif
	} else {
		dev_info(kbdev->dev,
			"Probed as %s\n", dev_name(kbdev->mdev.this_device));
		kbase_increment_device_id();
#if (KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE)
		mutex_unlock(&kbase_probe_mutex);
#endif
#ifdef CONFIG_MALI_ARBITER_SUPPORT
		mutex_lock(&kbdev->pm.lock);
		kbase_arbiter_pm_vm_event(kbdev, KBASE_VM_GPU_INITIALIZED_EVT);
		mutex_unlock(&kbdev->pm.lock);
#endif
	}

	return err;
}

#undef KBASEP_DEFAULT_REGISTER_HISTORY_SIZE

/**
 * kbase_device_suspend - Suspend callback from the OS.
 *
 * @dev:  The device to suspend
 *
 * This is called by Linux when the device should suspend.
 *
 * Return: A standard Linux error code on failure, 0 otherwise.
 */
static int kbase_device_suspend(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

	if (kbase_pm_suspend(kbdev)) {
		dev_warn(kbdev->dev, "Abort suspend as GPU suspension failed");
		return -EBUSY;
	}

#ifdef CONFIG_MALI_BIFROST_DVFS
	kbase_pm_metrics_stop(kbdev);
#endif

#ifdef CONFIG_MALI_BIFROST_DEVFREQ
	dev_dbg(dev, "Callback %s\n", __func__);
	if (kbdev->devfreq) {
		kbase_devfreq_enqueue_work(kbdev, DEVFREQ_WORK_SUSPEND);
		flush_workqueue(kbdev->devfreq_queue.workq);
	}
#endif

#ifdef CONFIG_ARCH_ROCKCHIP
	kbase_platform_rk_enable_regulator(kbdev);
#endif

#ifdef KBASE_PM_RUNTIME
	if (kbdev->is_runtime_resumed) {
		if (kbdev->pm.backend.callback_power_runtime_off)
			kbdev->pm.backend.callback_power_runtime_off(kbdev);
	}
#endif /* KBASE_PM_RUNTIME */

	return 0;
}

/**
 * kbase_device_resume - Resume callback from the OS.
 *
 * @dev:  The device to resume
 *
 * This is called by Linux when the device should resume from suspension.
 *
 * Return: A standard Linux error code
 */
static int kbase_device_resume(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
		return -ENODEV;

#ifdef KBASE_PM_RUNTIME
	if (kbdev->is_runtime_resumed) {
		if (kbdev->pm.backend.callback_power_runtime_on)
			kbdev->pm.backend.callback_power_runtime_on(kbdev);
	}
#endif /* KBASE_PM_RUNTIME */

	kbase_pm_resume(kbdev);

#ifdef CONFIG_MALI_BIFROST_DVFS
	kbase_pm_metrics_start(kbdev);
#endif

#ifdef CONFIG_MALI_BIFROST_DEVFREQ
	dev_dbg(dev, "Callback %s\n", __func__);
	if (kbdev->devfreq)
		kbase_devfreq_enqueue_work(kbdev, DEVFREQ_WORK_RESUME);
#endif

#if !MALI_USE_CSF
	kbase_enable_quick_reset(kbdev);
#endif

	return 0;
}

/**
 * kbase_device_runtime_suspend - Runtime suspend callback from the OS.
 *
 * @dev:  The device to suspend
 *
 * This is called by Linux when the device should prepare for a condition in
 * which it will not be able to communicate with the CPU(s) and RAM due to
 * power management.
 *
 * Return: A standard Linux error code
 */
#ifdef KBASE_PM_RUNTIME
static int kbase_device_runtime_suspend(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);
	int ret = 0;

	if (!kbdev)
		return -ENODEV;

	dev_dbg(dev, "Callback %s\n", __func__);
	KBASE_KTRACE_ADD(kbdev, PM_RUNTIME_SUSPEND_CALLBACK, NULL, 0);

#if MALI_USE_CSF
	ret = kbase_pm_handle_runtime_suspend(kbdev);
	if (ret)
		return ret;
#endif

#ifdef CONFIG_MALI_BIFROST_DVFS
	kbase_pm_metrics_stop(kbdev);
#endif

#ifdef CONFIG_MALI_BIFROST_DEVFREQ
	if (kbdev->devfreq)
		kbase_devfreq_enqueue_work(kbdev, DEVFREQ_WORK_SUSPEND);
#endif

	if (kbdev->pm.backend.callback_power_runtime_off) {
		kbdev->pm.backend.callback_power_runtime_off(kbdev);
		kbdev->is_runtime_resumed = false;
		dev_dbg(dev, "runtime suspend\n");
	}
	return ret;
}
#endif /* KBASE_PM_RUNTIME */

/**
 * kbase_device_runtime_resume - Runtime resume callback from the OS.
 *
 * @dev:  The device to suspend
 *
 * This is called by Linux when the device should go into a fully active state.
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

	dev_dbg(dev, "Callback %s\n", __func__);
	// KBASE_KTRACE_ADD(kbdev, PM_RUNTIME_RESUME_CALLBACK, NULL, 0);
	if (kbdev->pm.backend.callback_power_runtime_on) {
		ret = kbdev->pm.backend.callback_power_runtime_on(kbdev);
		kbdev->is_runtime_resumed = true;
		dev_dbg(dev, "runtime resume\n");
	}

#ifdef CONFIG_MALI_BIFROST_DVFS
	kbase_pm_metrics_start(kbdev);
#endif

#ifdef CONFIG_MALI_BIFROST_DEVFREQ
	if (kbdev->devfreq)
		kbase_devfreq_enqueue_work(kbdev, DEVFREQ_WORK_RESUME);
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

	dev_dbg(dev, "Callback %s\n", __func__);
	/* Use platform specific implementation if it exists. */
	if (kbdev->pm.backend.callback_power_runtime_idle)
		return kbdev->pm.backend.callback_power_runtime_idle(kbdev);

	/* Just need to update the device's last busy mark. Kernel will respect
	 * the autosuspend delay and so won't suspend the device immediately.
	 */
	pm_runtime_mark_last_busy(kbdev->dev);
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

#if IS_ENABLED(CONFIG_OF)
static const struct of_device_id kbase_dt_ids[] = { { .compatible = "arm,malit6xx" },
						    { .compatible = "arm,mali-midgard" },
						    { .compatible = "arm,mali-bifrost" },
						    { .compatible = "arm,mali-valhall" },
						    { /* sentinel */ } };
MODULE_DEVICE_TABLE(of, kbase_dt_ids);
#endif

static struct platform_driver kbase_platform_driver = {
	.probe = kbase_platform_device_probe,
	.remove = kbase_platform_device_remove,
	.driver = {
		   .name = kbase_drv_name,
		   .pm = &kbase_pm_ops,
		   .of_match_table = of_match_ptr(kbase_dt_ids),
		   .probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
};

#if (KERNEL_VERSION(5, 3, 0) > LINUX_VERSION_CODE) && IS_ENABLED(CONFIG_OF)
module_platform_driver(kbase_platform_driver);
#else
static int __init kbase_driver_init(void)
{
	int ret;

#if (KERNEL_VERSION(5, 3, 0) <= LINUX_VERSION_CODE)
	mutex_init(&kbase_probe_mutex);
#endif

#ifndef CONFIG_OF
	ret = kbase_platform_register();
	if (ret)
		return ret;
#endif
	ret = platform_driver_register(&kbase_platform_driver);
#ifndef CONFIG_OF
	if (ret) {
		kbase_platform_unregister();
		return ret;
	}
#endif

	return ret;
}

static void __exit kbase_driver_exit(void)
{
	platform_driver_unregister(&kbase_platform_driver);
#ifndef CONFIG_OF
	kbase_platform_unregister();
#endif
}

module_init(kbase_driver_init);
module_exit(kbase_driver_exit);
#endif
MODULE_LICENSE("GPL");
MODULE_VERSION(MALI_RELEASE_NAME " (UK version " \
		__stringify(BASE_UK_VERSION_MAJOR) "." \
		__stringify(BASE_UK_VERSION_MINOR) ")");
MODULE_SOFTDEP("pre: memory_group_manager");
MODULE_INFO(import_ns, "DMA_BUF");

#define CREATE_TRACE_POINTS
/* Create the trace points (otherwise we just get code to call a tracepoint) */
#include "mali_linux_trace.h"

#ifdef CONFIG_MALI_BIFROST_GATOR_SUPPORT
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_job_slots_event);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_pm_status);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_page_fault_insert_pages);
EXPORT_TRACEPOINT_SYMBOL_GPL(mali_total_alloc_pages_change);

void kbase_trace_mali_pm_status(u32 dev_id, u32 event, u64 value)
{
	trace_mali_pm_status(dev_id, event, value);
}

void kbase_trace_mali_job_slots_event(u32 dev_id, u32 event, const struct kbase_context *kctx, u8 atom_id)
{
	trace_mali_job_slots_event(dev_id, event,
		(kctx != NULL ? kctx->tgid : 0),
		(kctx != NULL ? kctx->pid : 0),
		atom_id);
}

void kbase_trace_mali_page_fault_insert_pages(u32 dev_id, int event, u32 value)
{
	trace_mali_page_fault_insert_pages(dev_id, event, value);
}

void kbase_trace_mali_total_alloc_pages_change(u32 dev_id, long long event)
{
	trace_mali_total_alloc_pages_change(dev_id, event);
}
#endif /* CONFIG_MALI_BIFROST_GATOR_SUPPORT */
