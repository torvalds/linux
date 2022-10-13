// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
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

#if IS_ENABLED(CONFIG_DEBUG_FS)

/**
 * kbasep_fault_occurred - Check if fault occurred.
 *
 * @kbdev:  Device pointer
 *
 * Return: true if a fault occurred.
 */
static bool kbasep_fault_occurred(struct kbase_device *kbdev)
{
	unsigned long flags;
	bool ret;

	spin_lock_irqsave(&kbdev->csf.dof.lock, flags);
	ret = (kbdev->csf.dof.error_code != DF_NO_ERROR);
	spin_unlock_irqrestore(&kbdev->csf.dof.lock, flags);

	return ret;
}

void kbase_debug_csf_fault_wait_completion(struct kbase_device *kbdev)
{
	if (likely(!kbase_debug_csf_fault_dump_enabled(kbdev))) {
		dev_dbg(kbdev->dev, "No userspace client for dumping exists");
		return;
	}

	wait_event(kbdev->csf.dof.dump_wait_wq, kbase_debug_csf_fault_dump_complete(kbdev));
}
KBASE_EXPORT_TEST_API(kbase_debug_csf_fault_wait_completion);

/**
 * kbase_debug_csf_fault_wakeup - Wake up a waiting user space client.
 *
 * @kbdev:   Kbase device
 */
static void kbase_debug_csf_fault_wakeup(struct kbase_device *kbdev)
{
	wake_up_interruptible(&kbdev->csf.dof.fault_wait_wq);
}

bool kbase_debug_csf_fault_notify(struct kbase_device *kbdev,
	struct kbase_context *kctx, enum dumpfault_error_type error)
{
	unsigned long flags;

	if (likely(!kbase_debug_csf_fault_dump_enabled(kbdev)))
		return false;

	if (WARN_ON(error == DF_NO_ERROR))
		return false;

	if (kctx && kbase_ctx_flag(kctx, KCTX_DYING)) {
		dev_info(kbdev->dev, "kctx %d_%d is dying when error %d is reported",
			kctx->tgid, kctx->id, error);
		kctx = NULL;
	}

	spin_lock_irqsave(&kbdev->csf.dof.lock, flags);

	/* Only one fault at a time can be processed */
	if (kbdev->csf.dof.error_code) {
		dev_info(kbdev->dev, "skip this fault as there's a pending fault");
		goto unlock;
	}

	kbdev->csf.dof.kctx_tgid = kctx ? kctx->tgid : 0;
	kbdev->csf.dof.kctx_id = kctx ? kctx->id : 0;
	kbdev->csf.dof.error_code = error;
	kbase_debug_csf_fault_wakeup(kbdev);

unlock:
	spin_unlock_irqrestore(&kbdev->csf.dof.lock, flags);
	return true;
}

static ssize_t debug_csf_fault_read(struct file *file, char __user *buffer, size_t size,
				    loff_t *f_pos)
{
#define BUF_SIZE 64
	struct kbase_device *kbdev;
	unsigned long flags;
	int count;
	char buf[BUF_SIZE];
	u32 tgid, ctx_id;
	enum dumpfault_error_type error_code;

	if (unlikely(!file)) {
		pr_warn("%s: file is NULL", __func__);
		return -EINVAL;
	}

	kbdev = file->private_data;
	if (unlikely(!buffer)) {
		dev_warn(kbdev->dev, "%s: buffer is NULL", __func__);
		return -EINVAL;
	}

	if (unlikely(*f_pos < 0)) {
		dev_warn(kbdev->dev, "%s: f_pos is negative", __func__);
		return -EINVAL;
	}

	if (size < sizeof(buf)) {
		dev_warn(kbdev->dev, "%s: buffer is too small", __func__);
		return -EINVAL;
	}

	if (wait_event_interruptible(kbdev->csf.dof.fault_wait_wq, kbasep_fault_occurred(kbdev)))
		return -ERESTARTSYS;

	spin_lock_irqsave(&kbdev->csf.dof.lock, flags);
	tgid = kbdev->csf.dof.kctx_tgid;
	ctx_id = kbdev->csf.dof.kctx_id;
	error_code = kbdev->csf.dof.error_code;
	BUILD_BUG_ON(sizeof(buf) < (sizeof(tgid) + sizeof(ctx_id) + sizeof(error_code)));
	count = scnprintf(buf, sizeof(buf), "%u_%u_%u\n", tgid, ctx_id, error_code);
	spin_unlock_irqrestore(&kbdev->csf.dof.lock, flags);

	dev_info(kbdev->dev, "debug csf fault info read");
	return simple_read_from_buffer(buffer, size, f_pos, buf, count);
}

static int debug_csf_fault_open(struct inode *in, struct file *file)
{
	struct kbase_device *kbdev;

	if (unlikely(!in)) {
		pr_warn("%s: inode is NULL", __func__);
		return -EINVAL;
	}

	kbdev = in->i_private;
	if (unlikely(!file)) {
		dev_warn(kbdev->dev, "%s: file is NULL", __func__);
		return -EINVAL;
	}

	if (atomic_cmpxchg(&kbdev->csf.dof.enabled, 0, 1) == 1) {
		dev_warn(kbdev->dev, "Only one client is allowed for dump on fault");
		return -EBUSY;
	}

	dev_info(kbdev->dev, "debug csf fault file open");

	return simple_open(in, file);
}

static ssize_t debug_csf_fault_write(struct file *file, const char __user *ubuf, size_t count,
				     loff_t *ppos)
{
	struct kbase_device *kbdev;
	unsigned long flags;

	if (unlikely(!file)) {
		pr_warn("%s: file is NULL", __func__);
		return -EINVAL;
	}

	kbdev = file->private_data;
	spin_lock_irqsave(&kbdev->csf.dof.lock, flags);
	kbdev->csf.dof.error_code = DF_NO_ERROR;
	kbdev->csf.dof.kctx_tgid = 0;
	kbdev->csf.dof.kctx_id = 0;
	dev_info(kbdev->dev, "debug csf fault dump complete");
	spin_unlock_irqrestore(&kbdev->csf.dof.lock, flags);

	/* User space finished the dump.
	 * Wake up blocked kernel threads to proceed.
	 */
	wake_up(&kbdev->csf.dof.dump_wait_wq);

	return count;
}

static int debug_csf_fault_release(struct inode *in, struct file *file)
{
	struct kbase_device *kbdev;
	unsigned long flags;

	if (unlikely(!in)) {
		pr_warn("%s: inode is NULL", __func__);
		return -EINVAL;
	}

	kbdev = in->i_private;
	spin_lock_irqsave(&kbdev->csf.dof.lock, flags);
	kbdev->csf.dof.kctx_tgid = 0;
	kbdev->csf.dof.kctx_id = 0;
	kbdev->csf.dof.error_code = DF_NO_ERROR;
	spin_unlock_irqrestore(&kbdev->csf.dof.lock, flags);

	atomic_set(&kbdev->csf.dof.enabled, 0);
	dev_info(kbdev->dev, "debug csf fault file close");

	/* User space closed the debugfs file.
	 * Wake up blocked kernel threads to resume.
	 */
	wake_up(&kbdev->csf.dof.dump_wait_wq);

	return 0;
}

static const struct file_operations kbasep_debug_csf_fault_fops = {
	.owner = THIS_MODULE,
	.open = debug_csf_fault_open,
	.read = debug_csf_fault_read,
	.write = debug_csf_fault_write,
	.llseek = default_llseek,
	.release = debug_csf_fault_release,
};

void kbase_debug_csf_fault_debugfs_init(struct kbase_device *kbdev)
{
	const char *fname = "csf_fault";

	if (unlikely(!kbdev)) {
		pr_warn("%s: kbdev is NULL", __func__);
		return;
	}

	debugfs_create_file(fname, 0600, kbdev->mali_debugfs_directory, kbdev,
			    &kbasep_debug_csf_fault_fops);
}

int kbase_debug_csf_fault_init(struct kbase_device *kbdev)
{
	if (unlikely(!kbdev)) {
		pr_warn("%s: kbdev is NULL", __func__);
		return -EINVAL;
	}

	init_waitqueue_head(&(kbdev->csf.dof.fault_wait_wq));
	init_waitqueue_head(&(kbdev->csf.dof.dump_wait_wq));
	spin_lock_init(&kbdev->csf.dof.lock);
	kbdev->csf.dof.kctx_tgid = 0;
	kbdev->csf.dof.kctx_id = 0;
	kbdev->csf.dof.error_code = DF_NO_ERROR;
	atomic_set(&kbdev->csf.dof.enabled, 0);

	return 0;
}

void kbase_debug_csf_fault_term(struct kbase_device *kbdev)
{
}
#endif /* CONFIG_DEBUG_FS */
