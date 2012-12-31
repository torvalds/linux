/*
 *
 * (C) COPYRIGHT 2010-2012 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



/**
 * @file mali_kbase_core_linux.c
 * Base kernel driver init.
 */

#include <osk/mali_osk.h>
#include <kbase/src/common/mali_kbase.h>
#include <kbase/src/common/mali_kbase_uku.h>
#include <kbase/src/common/mali_midg_regmap.h>
#include <kbase/src/linux/mali_kbase_mem_linux.h>
#include <kbase/src/linux/mali_kbase_config_linux.h>
#include <uk/mali_ukk.h>
#if MALI_NO_MALI
#include "mali_kbase_model_linux.h"
#endif

#include <linux/module.h>
#include <linux/init.h>
#include <linux/poll.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#if MALI_LICENSE_IS_GPL
#include <linux/platform_device.h>
#include <linux/pci.h>
#include <linux/miscdevice.h>
#endif
#include <linux/list.h>
#include <linux/semaphore.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#if BASE_HW_ISSUE_8401
#include <kbase/src/common/mali_kbase_8401_workaround.h>
#endif
#ifdef CONFIG_VITHAR
#include <mach/map.h>
#include <kbase/src/platform/mali_kbase_platform.h>
#include <kbase/src/platform/mali_kbase_runtime_pm.h>
#include <kbase/src/platform/mali_kbase_dvfs.h>
#endif

#define	JOB_IRQ_TAG	0
#define MMU_IRQ_TAG	1
#define GPU_IRQ_TAG	2

struct kbase_irq_table
{
	u32		tag;
	irq_handler_t	handler;
};
#if MALI_DEBUG
kbase_exported_test_data shared_kernel_test_data;
EXPORT_SYMBOL(shared_kernel_test_data);
#endif /* MALI_DEBUG */

static const char kbase_drv_name[] = KBASE_DRV_NAME;

static int kbase_dev_nr;

#if MALI_LICENSE_IS_GPL
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,36)
static DEFINE_SEMAPHORE(kbase_dev_list_lock);
#else
static DECLARE_MUTEX(kbase_dev_list_lock);
#endif
static LIST_HEAD(kbase_dev_list);

KBASE_EXPORT_TEST_API(kbase_dev_list_lock)
KBASE_EXPORT_TEST_API(kbase_dev_list)
#endif

#if MALI_LICENSE_IS_GPL == 0
#include <linux/cdev.h>              /* character device definitions */

/* By default the module uses any available major, but it's possible to set it at load time to a specific number */
int mali_major = 0;
module_param(mali_major, int, S_IRUGO); /* r--r--r-- */
MODULE_PARM_DESC(mali_major, "Device major number");

struct mali_linux_device
{
    struct cdev cdev;
};

/* The global variable containing the global device data */
static struct mali_linux_device mali_linux_device;

static char mali_dev_name[] = KBASE_DRV_NAME; /* should be const, but the functions we call requires non-cost */

#undef dev_err
#undef dev_info
#undef dev_dbg
#define dev_err(dev,msg,...)  do { printk(KERN_ERR   KBASE_DRV_NAME " error: "); printk(msg, ## __VA_ARGS__); } while(0)
#define dev_info(dev,msg,...) do { printk(KERN_INFO  KBASE_DRV_NAME " info: ");  printk(msg, ## __VA_ARGS__); } while(0)
#define dev_dbg(dev,msg,...)  do { printk(KERN_DEBUG KBASE_DRV_NAME " debug: "); printk(msg, ## __VA_ARGS__); } while(0)
#define dev_name(dev) "MALI"

/* STATIC */ struct kbase_device     *g_kbdev;
KBASE_EXPORT_TEST_API(g_kbdev);

#endif


#if MALI_LICENSE_IS_GPL
#define KERNEL_SIDE_DDK_VERSION_STRING "K:" MALI_RELEASE_NAME "(GPL)"
#else
#define KERNEL_SIDE_DDK_VERSION_STRING "K:" MALI_RELEASE_NAME
#endif /* MALI_LICENSE_IS_GPL */

static INLINE void __compile_time_asserts( void )
{
	CSTD_COMPILE_TIME_ASSERT( sizeof(KERNEL_SIDE_DDK_VERSION_STRING) <= KBASE_GET_VERSION_BUFFER_SIZE);
}

static mali_error kbase_dispatch(ukk_call_context * const ukk_ctx, void * const args, u32 args_size)
{
	struct kbase_context *kctx;
	struct kbase_device *kbdev;
	uk_header *ukh = args;
	u32 id;

	OSKP_ASSERT( ukh != NULL );

	kctx = CONTAINER_OF(ukk_session_get(ukk_ctx), kbase_context, ukk_session);
	kbdev = kctx->kbdev;
	id = ukh->id;
	ukh->ret = MALI_ERROR_NONE; /* Be optimistic */

	switch(id)
	{
		case KBASE_FUNC_TMEM_ALLOC:
		{
			kbase_uk_tmem_alloc *tmem = args;
			struct kbase_va_region *reg;

			if (sizeof(*tmem) != args_size)
			{
				goto bad_size;
			}

			reg = kbase_tmem_alloc(kctx, tmem->vsize, tmem->psize,
					       tmem->extent, tmem->flags, tmem->is_growable);
			if (reg)
			{
				tmem->gpu_addr	= reg->start_pfn << 12;
			}
			else
			{
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			}
			break;
		}

		case KBASE_FUNC_TMEM_FROM_UMP:
#if MALI_USE_UMP
		{
			kbase_uk_tmem_from_ump * tmem_ump = args;
			struct kbase_va_region *reg;

			if (sizeof(*tmem_ump) != args_size)
			{
				goto bad_size;
			}

			reg = kbase_tmem_from_ump(kctx, tmem_ump->id, &tmem_ump->pages);
			if (reg)
			{
				tmem_ump->gpu_addr = reg->start_pfn << 12;
			}
			else
			{
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			}
			break;
		}
#else
		{
			ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			break;
		}
#endif

		case KBASE_FUNC_PMEM_ALLOC:
		{
			kbase_uk_pmem_alloc *pmem = args;
			struct kbase_va_region *reg;

			if (sizeof(*pmem) != args_size)
			{
				goto bad_size;
			}

			reg = kbase_pmem_alloc(kctx, pmem->vsize, pmem->flags,
					       &pmem->cookie);
			if (!reg)
			{
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			}
			break;
		}

		case KBASE_FUNC_MEM_FREE:
		{
			kbase_uk_mem_free *mem = args;

			if (sizeof(*mem) != args_size)
			{
				goto bad_size;
			}

			if (kbase_mem_free(kctx, mem->gpu_addr))
			{
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			}
			break;
		}

		case KBASE_FUNC_JOB_SUBMIT:
		{
			kbase_uk_job_submit * job = args;
			
			if (sizeof(*job) != args_size)
			{
				goto bad_size;
			}

			if (MALI_ERROR_NONE != kbase_jd_submit(kctx, job))
			{
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			}
			break;
		}

		case KBASE_FUNC_SYNC:
		{
			kbase_uk_sync_now *sn = args;

			if (sizeof(*sn) != args_size)
			{
				goto bad_size;
			}

			if (MALI_ERROR_NONE != kbase_sync_now(kctx, &sn->sset))
			{
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			}
			break;
		}

		case KBASE_FUNC_POST_TERM:
		{
			kbase_event_close(kctx);
			break;
		}

		case KBASE_FUNC_HWCNT_SETUP:
		{
			kbase_uk_hwcnt_setup * setup = args;

			if (sizeof(*setup) != args_size)
			{
				goto bad_size;
			}

			if (MALI_ERROR_NONE != kbase_instr_hwcnt_setup(kctx, setup))
			{
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			}
			break;
		}

		case KBASE_FUNC_HWCNT_DUMP:
		{
			/* args ignored */
			if (MALI_ERROR_NONE != kbase_instr_hwcnt_dump(kctx))
			{
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			}
			break;
		}

		case KBASE_FUNC_HWCNT_CLEAR:
		{
			/* args ignored */
			if (MALI_ERROR_NONE != kbase_instr_hwcnt_clear(kctx))
			{
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			}
			break;
		}

		case KBASE_FUNC_GPU_PROPS_REG_DUMP:
		{
			kbase_uk_gpuprops * setup = args;

			if (sizeof(*setup) != args_size)
			{
				goto bad_size;
			}

			if (MALI_ERROR_NONE != kbase_gpuprops_uk_get_props(kctx, setup))
			{
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			}
			break;
		}

		case KBASE_FUNC_TMEM_RESIZE:
		{
			kbase_uk_tmem_resize *resize = args;
			if (sizeof(*resize) != args_size)
			{
				goto bad_size;
			}

			ukh->ret = kbase_tmem_resize(kctx, resize->gpu_addr, resize->delta, &resize->size, &resize->result_subcode);
			break;
		}

		case KBASE_FUNC_FIND_CPU_MAPPING:
		{
			kbase_uk_find_cpu_mapping *find = args;
			struct kbase_cpu_mapping *map;

			if (sizeof(*find) != args_size)
			{
				goto bad_size;
			}

			OSKP_ASSERT( find != NULL );
			if ( find->size > SIZE_MAX || find->cpu_addr > UINTPTR_MAX )
			{
				map = NULL;
			}
			else
			{
				map = kbasep_find_enclosing_cpu_mapping( kctx,
				                                         find->gpu_addr,
				                                         (osk_virt_addr)(uintptr_t)find->cpu_addr,
				                                         (size_t)find->size );
			}

			if ( NULL != map )
			{
				find->uaddr = PTR_TO_U64( map->uaddr );
				find->nr_pages = map->nr_pages;
				find->page_off = map->page_off;
			}
			else
			{
				ukh->ret = MALI_ERROR_FUNCTION_FAILED;
			}
			break;
		}
		case KBASE_FUNC_GET_VERSION:
		{
			kbase_uk_get_ddk_version *get_version = (kbase_uk_get_ddk_version *)args;

			if (sizeof(*get_version) != args_size)
			{
				goto bad_size;
			}

			/* version buffer size check is made in compile time assert */
			OSK_MEMCPY(get_version->version_buffer, KERNEL_SIDE_DDK_VERSION_STRING,
				sizeof(KERNEL_SIDE_DDK_VERSION_STRING));
			get_version->version_string_size = sizeof(KERNEL_SIDE_DDK_VERSION_STRING);
			break;
		}

#if MALI_DEBUG
		case KBASE_FUNC_SET_TEST_DATA:
		{
			kbase_uk_set_test_data *set_data = args;

			shared_kernel_test_data = set_data->test_data;
			shared_kernel_test_data.kctx = kctx;
			shared_kernel_test_data.mm = (void*)current->mm;
			ukh->ret = MALI_ERROR_NONE;
			break;
		}
#endif /* MALI_DEBUG */
#if MALI_ERROR_INJECT_ON
		case KBASE_FUNC_INJECT_ERROR:
		{
			kbase_error_params params = ((kbase_uk_error_params*)args)->params;
			/*mutex lock*/
			osk_spinlock_lock(&kbdev->osdev.reg_op_lock);
			ukh->ret = job_atom_inject_error(&params);
			osk_spinlock_unlock(&kbdev->osdev.reg_op_lock);
			/*mutex unlock*/

			break;
		}
#endif /*MALI_ERROR_INJECT_ON*/
#if MALI_NO_MALI
		case KBASE_FUNC_MODEL_CONTROL:
		{
			kbase_model_control_params params = ((kbase_uk_model_control_params*)args)->params;
			/*mutex lock*/
			osk_spinlock_lock(&kbdev->osdev.reg_op_lock);
			ukh->ret = midg_model_control(kbdev->osdev.model, &params);
			osk_spinlock_unlock(&kbdev->osdev.reg_op_lock);
			/*mutex unlock*/
			break;
		}
#endif /* MALI_NO_MALI */
		default:
			dev_err(kbdev->osdev.dev, "unknown syscall %08x", ukh->id);
			goto out_bad;
	}

	return MALI_ERROR_NONE;

bad_size:
	dev_err(kbdev->osdev.dev, "Wrong syscall size (%d) for %08x\n", args_size, ukh->id);
out_bad:
	return MALI_ERROR_FUNCTION_FAILED;
}

#if MALI_LICENSE_IS_GPL
static struct kbase_device *to_kbase_device(struct device *dev)
{
	return dev_get_drvdata(dev);
}
#endif /* MALI_LICENSE_IS_GPL */

/* Find a particular kbase device (as specified by minor number), or find the "first" device if -1 is specified */
struct kbase_device *kbase_find_device(int minor)
{
	struct kbase_device *kbdev = NULL;
#if MALI_LICENSE_IS_GPL
	struct list_head *entry;

	down(&kbase_dev_list_lock);
	list_for_each(entry, &kbase_dev_list)
	{
		struct kbase_device *tmp;

		tmp = list_entry(entry, struct kbase_device, osdev.entry);
		if (tmp->osdev.mdev.minor == minor || minor == -1)
		{
			kbdev = tmp;
			get_device(kbdev->osdev.dev);
			break;
		}
	}
	up(&kbase_dev_list_lock);
#else
	kbdev = g_kbdev;
#endif

	return kbdev;
}

EXPORT_SYMBOL(kbase_find_device);

void kbase_release_device(struct kbase_device *kbdev)
{
#if MALI_LICENSE_IS_GPL
	put_device(kbdev->osdev.dev);
#endif
}

EXPORT_SYMBOL(kbase_release_device);

static int kbase_open(struct inode *inode, struct file *filp)
{
	struct kbase_device *kbdev = NULL;
	struct kbase_context *kctx;
	int ret = 0;

	/* Enforce that the driver is opened with O_CLOEXEC so that execve() automatically
	 * closes the file descriptor in a child process. 
	 */
	if (0 == (filp->f_flags & O_CLOEXEC))
	{
		printk(KERN_ERR   KBASE_DRV_NAME " error: O_CLOEXEC flag not set\n");
		return -EINVAL;
	}

	kbdev = kbase_find_device(iminor(inode));

	if (!kbdev)
		return -ENODEV;

	kctx = kbase_create_context(kbdev);
	if (!kctx)
	{
		ret = -ENOMEM;
		goto out;
	}
    
	if (MALI_ERROR_NONE != ukk_session_init(&kctx->ukk_session, kbase_dispatch, BASE_UK_VERSION_MAJOR, BASE_UK_VERSION_MINOR))
	{
		kbase_destroy_context(kctx);
		ret = -EFAULT;
		goto out;
	}

	init_waitqueue_head(&kctx->osctx.event_queue);
	filp->private_data = kctx;

	dev_dbg(kbdev->osdev.dev, "created base context\n");
	return 0;

out:
	kbase_release_device(kbdev);
	return ret;
}

static int kbase_release(struct inode *inode, struct file *filp)
{
	struct kbase_context *kctx = filp->private_data;
	struct kbase_device *kbdev = kctx->kbdev;

	ukk_session_term(&kctx->ukk_session);
	filp->private_data = NULL;
	kbase_destroy_context(kctx);

	dev_dbg(kbdev->osdev.dev, "deleted base context\n");
	kbase_release_device(kbdev);
	return 0;
}

static long kbase_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	u64 msg[(UKK_CALL_MAX_SIZE+7)>>3]; /* alignment fixup */
	u32 size = _IOC_SIZE(cmd);
	ukk_call_context ukk_ctx;
	struct kbase_context *kctx = filp->private_data;

	if (size > UKK_CALL_MAX_SIZE) return -ENOTTY;

	if (0 != copy_from_user(&msg, (void *)arg, size))
	{
		return -EFAULT;
	}

	ukk_call_prepare(&ukk_ctx, &kctx->ukk_session, NULL);

	if (MALI_ERROR_NONE != ukk_dispatch(&ukk_ctx, &msg, size))
	{
		return -EFAULT;
	}

	if (0 != copy_to_user((void *)arg, &msg, size))
	{
		pr_err("failed to copy results of UK call back to user space\n");
		return -EFAULT;
	}
	return 0;
}

static ssize_t kbase_read(struct file *filp, char __user *buf,
			  size_t count, loff_t *f_pos)
{
	struct kbase_context *kctx = filp->private_data;
	base_jd_event uevent;

	if (count < sizeof(uevent))
	{
		return -ENOBUFS;
	}

	while (kbase_event_dequeue(kctx, &uevent))
	{
		if (filp->f_flags & O_NONBLOCK)
		{
			return -EAGAIN;
		}

		if (wait_event_interruptible(kctx->osctx.event_queue,
					     kbase_event_pending(kctx)))
		{
			return -ERESTARTSYS;
		}
	}

	if (copy_to_user(buf, &uevent, sizeof(uevent)))
	{
		return -EFAULT;
	}

	return sizeof(uevent);
}

static unsigned int kbase_poll(struct file *filp, poll_table *wait)
{
	struct kbase_context *kctx = filp->private_data;

	poll_wait(filp, &kctx->osctx.event_queue, wait);
	if (kbase_event_pending(kctx))
	{
		return POLLIN | POLLRDNORM;
	}

	return 0;
}

void kbase_event_wakeup(kbase_context *kctx)
{
	OSK_ASSERT(kctx);

	wake_up_interruptible(&kctx->osctx.event_queue);
}
KBASE_EXPORT_TEST_API(kbase_event_wakeup)

int kbase_check_flags(int flags)
{
	/* Enforce that the driver keeps the O_CLOEXEC flag so that execve() always
	 * closes the file descriptor in a child process. 
	 */
	if (0 == (flags & O_CLOEXEC))
	{
		return -EINVAL; 
	}

	return 0;
}

static const struct file_operations kbase_fops =
{
	.owner		= THIS_MODULE,
	.open		= kbase_open,
	.release	= kbase_release,
	.read		= kbase_read,
	.poll		= kbase_poll,
	.unlocked_ioctl	= kbase_ioctl,
	.mmap		= kbase_mmap,
	.check_flags    = kbase_check_flags,
};

#if !MALI_NO_MALI
void kbase_os_reg_write(kbase_device *kbdev, u16 offset, u32 value)
{
	writel(value, kbdev->osdev.reg + offset);
}

u32 kbase_os_reg_read(kbase_device *kbdev, u16 offset)
{
	return readl(kbdev->osdev.reg + offset);
}

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
	struct kbase_device *kbdev	= kbase_untag(data);
	u32 val;

	val = kbase_reg_read(kbdev, JOB_CONTROL_REG(JOB_IRQ_RAWSTAT), NULL);
	if (!val)
	{
		return IRQ_NONE;
	}

	dev_dbg(kbdev->osdev.dev, "%s: irq %d rawstat 0x%x\n", __func__, irq, val);

	kbase_job_done(kbdev, val);

	return IRQ_HANDLED;
}

static irqreturn_t kbase_mmu_irq_handler(int irq, void *data)
{
	struct kbase_device *kbdev	= kbase_untag(data);
	u32 val;

	val = kbase_reg_read(kbdev, MMU_REG(MMU_IRQ_STATUS), NULL);
	if (!val)
	{
		return IRQ_NONE;
	}

	dev_dbg(kbdev->osdev.dev, "%s: irq %d irqstatus 0x%x\n", __func__, irq, val);

	kbase_mmu_interrupt(kbdev, val);

	return IRQ_HANDLED;
}

static irqreturn_t kbase_gpu_irq_handler(int irq, void *data)
{
	struct kbase_device *kbdev	= kbase_untag(data);
	u32 val;

	val = kbase_reg_read(kbdev, GPU_CONTROL_REG(GPU_IRQ_STATUS), NULL);

	if (!val)
	{
		return IRQ_NONE;
	}

	dev_dbg(kbdev->osdev.dev, "%s: irq %d rawstat 0x%x\n", __func__, irq, val);

	kbase_gpu_interrupt(kbdev, val);

	return IRQ_HANDLED;
}

static irq_handler_t kbase_handler_table[] = {
	[JOB_IRQ_TAG] = kbase_job_irq_handler,
	[MMU_IRQ_TAG] = kbase_mmu_irq_handler,
	[GPU_IRQ_TAG] = kbase_gpu_irq_handler,
};

static int kbase_install_interrupts(kbase_device *kbdev)
{
	struct kbase_os_device *osdev = &kbdev->osdev;
	int nr = ARRAY_SIZE(kbase_handler_table);
	int err;
	u32 i;

	BUG_ON(nr >= 4);	/* Only 3 interrupts! */

	for (i = 0; i < nr; i++)
	{
		err = request_irq(osdev->irqs[i].irq,
				  kbase_handler_table[i],
				  osdev->irqs[i].flags | IRQF_SHARED,
				  dev_name(osdev->dev), 
				  kbase_tag(kbdev, i));
		if (err)
		{
			dev_err(osdev->dev, "Can't request interrupt %d (index %d)\n", osdev->irqs[i].irq, i);
			goto release;
		}
	}

	return 0;

release:
	while (i-- > 0)
	{
		free_irq(osdev->irqs[i].irq, kbase_tag(kbdev, i));
	}

	return err;
}

static void kbase_release_interrupts(kbase_device *kbdev)
{
	struct kbase_os_device *osdev = &kbdev->osdev;
	int nr = ARRAY_SIZE(kbase_handler_table);
	u32 i;

	for (i = 0; i < nr; i++)
	{
		if (osdev->irqs[i].irq)
		{
			free_irq(osdev->irqs[i].irq, kbase_tag(kbdev, i));
		}
	}
}
#endif

#if MALI_LICENSE_IS_GPL

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
static ssize_t show_policy(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	const struct kbase_pm_policy *current_policy;
	const struct kbase_pm_policy * const *policy_list;
	int policy_count;
	int i;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}

	current_policy = kbase_pm_get_policy(kbdev);

	policy_count = kbase_pm_list_policies(&policy_list);

	for(i=0; i<policy_count && ret<PAGE_SIZE; i++)
	{
		if (policy_list[i] == current_policy)
		{
			ret += scnprintf(buf+ret, PAGE_SIZE - ret, "[%s] ", policy_list[i]->name);
		}
		else
		{
			ret += scnprintf(buf+ret, PAGE_SIZE - ret, "%s ", policy_list[i]->name);
		}
	}

	if (ret < PAGE_SIZE - 1)
	{
		ret += scnprintf(buf+ret, PAGE_SIZE-ret, "\n");
	}
	else
	{
		buf[PAGE_SIZE-2] = '\n';
		buf[PAGE_SIZE-1] = '\0';
		ret = PAGE_SIZE-1;
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
	const struct kbase_pm_policy * const *policy_list;
	int policy_count;
	int i;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}

	policy_count = kbase_pm_list_policies(&policy_list);

	for(i=0; i<policy_count; i++)
	{
		if (sysfs_streq(policy_list[i]->name, buf))
		{
			new_policy = policy_list[i];
			break;
		}
	}

	if (!new_policy)
	{
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
DEVICE_ATTR(power_policy, S_IRUGO|S_IWUSR, show_policy, set_policy);

#if  MALI_LICENSE_IS_GPL && (MALI_CUSTOMER_RELEASE == 0)
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
	unsigned long js_hard_stop_ms_ss;
	unsigned long js_hard_stop_ms_nss;
	unsigned long js_reset_ms_ss;
	unsigned long js_reset_ms_nss;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
	{
		return -ENODEV;
	}

	items = sscanf(buf, "%lu %lu %lu %lu %lu", &js_soft_stop_ms, &js_hard_stop_ms_ss, &js_hard_stop_ms_nss, &js_reset_ms_ss, &js_reset_ms_nss);
	if (items == 5)
	{
		u64 ticks;

		ticks = js_soft_stop_ms * 1000000ULL;
		osk_divmod6432(&ticks, kbdev->js_data.scheduling_tick_ns);
		kbdev->js_soft_stop_ticks = ticks;

		ticks = js_hard_stop_ms_ss * 1000000ULL;
		osk_divmod6432(&ticks, kbdev->js_data.scheduling_tick_ns);
		kbdev->js_hard_stop_ticks_ss = ticks;

		ticks = js_hard_stop_ms_nss * 1000000ULL;
		osk_divmod6432(&ticks, kbdev->js_data.scheduling_tick_ns);
		kbdev->js_hard_stop_ticks_nss = ticks;

		ticks = js_reset_ms_ss * 1000000ULL;
		osk_divmod6432(&ticks, kbdev->js_data.scheduling_tick_ns);
		kbdev->js_reset_ticks_ss = ticks;

		ticks = js_reset_ms_nss * 1000000ULL;
		osk_divmod6432(&ticks, kbdev->js_data.scheduling_tick_ns);
		kbdev->js_reset_ticks_nss = ticks;

		dev_info(kbdev->osdev.dev, "Overriding KBASE_CONFIG_ATTR_JS_SOFT_STOP_TICKS with %lu ticks (%lu ms)\n", (unsigned long)kbdev->js_soft_stop_ticks, js_soft_stop_ms);
		dev_info(kbdev->osdev.dev, "Overriding KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS with %lu ticks (%lu ms)\n", (unsigned long)kbdev->js_hard_stop_ticks_ss, js_hard_stop_ms_ss);
		dev_info(kbdev->osdev.dev, "Overriding KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS with %lu ticks (%lu ms)\n", (unsigned long)kbdev->js_hard_stop_ticks_nss, js_hard_stop_ms_nss);
		dev_info(kbdev->osdev.dev, "Overriding KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS with %lu ticks (%lu ms)\n", (unsigned long)kbdev->js_reset_ticks_ss, js_reset_ms_ss);
		dev_info(kbdev->osdev.dev, "Overriding KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS with %lu ticks (%lu ms)\n", (unsigned long)kbdev->js_reset_ticks_nss, js_reset_ms_nss);

		return count;
	}
	else
	{
		dev_err(kbdev->osdev.dev, "Couldn't process js_timeouts write operation.\nUse format "
		                          "<soft_stop_ms> <hard_stop_ms_ss> <hard_stop_ms_nss> <reset_ms_ss> <reset_ms_nss>\n");
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
static ssize_t show_js_timeouts(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	ssize_t ret;
	u64 ms;
	unsigned long js_soft_stop_ms;
	unsigned long js_hard_stop_ms_ss;
	unsigned long js_hard_stop_ms_nss;
	unsigned long js_reset_ms_ss;
	unsigned long js_reset_ms_nss;

	kbdev = to_kbase_device(dev);
	if (!kbdev)
	{
		return -ENODEV;
	}

	ms = (u64)kbdev->js_soft_stop_ticks * kbdev->js_data.scheduling_tick_ns;
	osk_divmod6432(&ms, 1000000UL);
	js_soft_stop_ms = (unsigned long)ms;

	ms = (u64)kbdev->js_hard_stop_ticks_ss * kbdev->js_data.scheduling_tick_ns;
	osk_divmod6432(&ms, 1000000UL);
	js_hard_stop_ms_ss = (unsigned long)ms;

	ms = (u64)kbdev->js_hard_stop_ticks_nss * kbdev->js_data.scheduling_tick_ns;
	osk_divmod6432(&ms, 1000000UL);
	js_hard_stop_ms_nss = (unsigned long)ms;

	ms = (u64)kbdev->js_reset_ticks_ss * kbdev->js_data.scheduling_tick_ns;
	osk_divmod6432(&ms, 1000000UL);
	js_reset_ms_ss = (unsigned long)ms;

	ms = (u64)kbdev->js_reset_ticks_nss * kbdev->js_data.scheduling_tick_ns;
	osk_divmod6432(&ms, 1000000UL);
	js_reset_ms_nss = (unsigned long)ms;

	ret = scnprintf(buf, PAGE_SIZE, "%lu %lu %lu %lu %lu\n", js_soft_stop_ms, js_hard_stop_ms_ss, js_hard_stop_ms_nss, js_reset_ms_ss, js_reset_ms_nss);

	if (ret >= PAGE_SIZE )
	{
		buf[PAGE_SIZE-2] = '\n';
		buf[PAGE_SIZE-1] = '\0';
		ret = PAGE_SIZE-1;
	}

	return ret;
}
/** The sysfs file @c js_timeouts.
 *
 * This is used to override the current job scheduler values for
 * KBASE_CONFIG_ATTR_JS_STOP_STOP_TICKS_SS
 * KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_SS
 * KBASE_CONFIG_ATTR_JS_HARD_STOP_TICKS_NSS
 * KBASE_CONFIG_ATTR_JS_RESET_TICKS_SS
 * KBASE_CONFIG_ATTR_JS_RESET_TICKS_NSS.
 */
DEVICE_ATTR(js_timeouts, S_IRUGO|S_IWUSR, show_js_timeouts, set_js_timeouts);
#endif  /* MALI_LICENSE_IS_GPL && (MALI_CUSTOMER_RELEASE == 0) */

#if MALI_DEBUG
typedef void (kbasep_debug_command_func)( kbase_device * );

typedef enum
{
	KBASEP_DEBUG_COMMAND_DUMPTRACE,

	/* This must be the last enum */
	KBASEP_DEBUG_COMMAND_COUNT
} kbasep_debug_command_code;

typedef struct kbasep_debug_command
{
	char *str;
	kbasep_debug_command_func *func;
} kbasep_debug_command;

/** Debug commands supported by the driver */
static const kbasep_debug_command debug_commands[] =
{
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
static ssize_t show_debug(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct kbase_device *kbdev;
	int i;
	ssize_t ret = 0;

	kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}

	for(i=0; i<KBASEP_DEBUG_COMMAND_COUNT && ret<PAGE_SIZE; i++)
	{
		ret += scnprintf(buf+ret, PAGE_SIZE - ret, "%s\n", debug_commands[i].str);
	}

	if (ret >= PAGE_SIZE )
	{
		buf[PAGE_SIZE-2] = '\n';
		buf[PAGE_SIZE-1] = '\0';
		ret = PAGE_SIZE-1;
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
	{
		return -ENODEV;
	}

	for(i=0; i<KBASEP_DEBUG_COMMAND_COUNT; i++)
	{
		if (sysfs_streq(debug_commands[i].str, buf))
		{
			debug_commands[i].func( kbdev );
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
DEVICE_ATTR(debug_command, S_IRUGO|S_IWUSR, show_debug, issue_debug);
#endif /*MALI_DEBUG*/
#endif /*MALI_LICENSE_IS_GPL*/

static int kbase_common_device_init(kbase_device *kbdev)
{
	struct kbase_os_device	*osdev = &kbdev->osdev;
	int err = -ENOMEM;
	mali_error mali_err;
	enum
	{
		 inited_mem         = (1u << 0),
		 inited_job_slot    = (1u << 1),
		 inited_pm          = (1u << 2),
		 inited_js          = (1u << 3),
		 inited_irqs        = (1u << 4)
#if MALI_LICENSE_IS_GPL
		,inited_debug       = (1u << 5)
#endif
#if MALI_CUSTOMER_RELEASE == 0
		,inited_js_timeouts = (1u << 6)
#endif
#if BASE_HW_ISSUE_8401
		,inited_workaround  = (1u << 7)
#endif
	};

	int inited = 0;
	
	osdev->reg_res = request_mem_region(osdev->reg_start,
					    osdev->reg_size,
#if MALI_LICENSE_IS_GPL
					    dev_name(osdev->dev)
#else
					    mali_dev_name
#endif
					);
	if (!osdev->reg_res)
	{
		dev_err(osdev->dev, "Register window unavailable\n");
		err = -EIO;
		goto out_region;
	}

	osdev->reg = ioremap(osdev->reg_start, osdev->reg_size);
	if (!osdev->reg)
	{
		dev_err(osdev->dev, "Can't remap register window\n");
		err = -EINVAL;
		goto out_ioremap;
	}
#if MALI_LICENSE_IS_GPL
	dev_set_drvdata(osdev->dev, kbdev);
	osdev->mdev.minor	= MISC_DYNAMIC_MINOR;
	osdev->mdev.name	= osdev->devname;
	osdev->mdev.fops	= &kbase_fops;
	osdev->mdev.parent	= get_device(osdev->dev);
#endif

	scnprintf(osdev->devname, DEVNAME_SIZE, "%s%d", kbase_drv_name, kbase_dev_nr++);

#if MALI_LICENSE_IS_GPL
	if (misc_register(&osdev->mdev))
	{
		dev_err(osdev->dev, "Couldn't register misc dev %s\n", osdev->devname);
		err = -EINVAL;
		goto out_misc;
	}

	if (device_create_file(osdev->dev, &dev_attr_power_policy))
	{
		dev_err(osdev->dev, "Couldn't create power_policy sysfs file\n");
		goto out_file;
	}

#ifdef CONFIG_VITHAR
	if(kbase_platform_init(osdev->dev))
	{
		dev_err(osdev->dev, "kbase_platform_init failed\n");
		goto out_file;
	}
#endif
	down(&kbase_dev_list_lock);
	list_add(&osdev->entry, &kbase_dev_list);
	up(&kbase_dev_list_lock);
	dev_info(osdev->dev, "Probed as %s\n", dev_name(osdev->mdev.this_device));
#endif

#if MALI_NO_MALI
	mali_err = midg_device_create(kbdev);
	if (MALI_ERROR_NONE != mali_err)
	{
		goto out_kbdev;
	}
#endif

	mali_err = kbase_pm_init(kbdev);
	if (MALI_ERROR_NONE != mali_err)
	{
		goto out_partial;
	}
	inited |= inited_pm;

	mali_err = kbase_mem_init(kbdev);
	if (MALI_ERROR_NONE != mali_err)
	{
		goto out_partial;
	}
	inited |= inited_mem;

	mali_err = kbase_job_slot_init(kbdev);
	if (MALI_ERROR_NONE != mali_err)
	{
		goto out_partial;
	}
	inited |= inited_job_slot;

	mali_err = kbasep_js_devdata_init(kbdev);
	if (MALI_ERROR_NONE != mali_err)
	{
		goto out_partial;
	}
	inited |= inited_js;

	err = kbase_install_interrupts(kbdev);
	if (err)
	{
		goto out_partial;
	}
	inited |= inited_irqs;

#if MALI_LICENSE_IS_GPL
#if MALI_DEBUG
	if (device_create_file(osdev->dev, &dev_attr_debug_command))
	{
		dev_err(osdev->dev, "Couldn't create debug_command sysfs file\n");
		goto out_partial;
	}
	inited |= inited_debug;

#endif
#if MALI_CUSTOMER_RELEASE == 0
	if (device_create_file(osdev->dev, &dev_attr_js_timeouts))
	{
		dev_err(osdev->dev, "Couldn't create js_timeouts sysfs file\n");
		goto out_partial;
	}
	inited |= inited_js_timeouts;
#endif
#endif /*MALI_LICENSE_IS_GPL*/

#if BASE_HW_ISSUE_8401
	if (MALI_ERROR_NONE != kbasep_8401_workaround_init(kbdev))
	{
		goto out_partial;
	}
	inited |= inited_workaround;
#endif

	mali_err = kbase_pm_powerup(kbdev);
	if (MALI_ERROR_NONE == mali_err)
	{
		kbase_gpuprops_set(kbdev);
		return 0;
	}

out_partial:
#if BASE_HW_ISSUE_8401
	if (inited & inited_workaround)
	{
		kbasep_8401_workaround_term(kbdev);
	}
#endif

#if MALI_LICENSE_IS_GPL
#if MALI_CUSTOMER_RELEASE == 0
	if (inited & inited_js_timeouts)
	{
		device_remove_file(kbdev->osdev.dev, &dev_attr_js_timeouts);
	}
#endif
#if MALI_DEBUG
	if (inited & inited_debug)
	{
		device_remove_file(kbdev->osdev.dev, &dev_attr_debug_command);
	}
#endif
#endif /*MALI_LICENSE_IS_GPL*/

	if (inited & inited_js)
	{
		kbasep_js_devdata_halt(kbdev);
	}
	if (inited & inited_job_slot)
	{
		kbase_job_slot_halt(kbdev);
	}
	if (inited & inited_mem)
	{
		kbase_mem_halt(kbdev);
	}
	if (inited & inited_pm)
	{
		kbase_pm_halt(kbdev);
	}

	if (inited & inited_irqs)
	{
		kbase_release_interrupts(kbdev);
	}

	if (inited & inited_js)
	{
		kbasep_js_devdata_term(kbdev);
	}
	if (inited & inited_job_slot)
	{
		kbase_job_slot_term(kbdev);
	}
	if (inited & inited_mem)
	{
		kbase_mem_term(kbdev);
	}
	if (inited & inited_pm)
	{
		kbase_pm_term(kbdev);
	}

#if MALI_NO_MALI
	midg_device_destroy(kbdev);
out_kbdev:
#endif

#if MALI_LICENSE_IS_GPL
	down(&kbase_dev_list_lock);
	list_del(&osdev->entry);
	up(&kbase_dev_list_lock);

	device_remove_file(kbdev->osdev.dev, &dev_attr_power_policy);
#ifdef CONFIG_VITHAR
	kbase_platform_remove_sysfs_file(kbdev->osdev.dev);
#endif
out_file:
	misc_deregister(&kbdev->osdev.mdev);
out_misc:
	put_device(osdev->dev);
#endif
	iounmap(osdev->reg);
out_ioremap:
	release_resource(osdev->reg_res);
	kfree(osdev->reg_res);
out_region:
	return err;
}

#if MALI_LICENSE_IS_GPL

static kbase_attribute pci_attributes[] =
{
	{
		KBASE_CONFIG_ATTR_MEMORY_PER_PROCESS_LIMIT,
		512 * 1024 * 1024UL /* 512MB */
	},
	{
		KBASE_CONFIG_ATTR_UMP_DEVICE,
		UMP_DEVICE_Z_SHIFT
	},
	{
		KBASE_CONFIG_ATTR_MEMORY_OS_SHARED_MAX,
		768 * 1024 * 1024UL /* 768MB */
	},
	{
		KBASE_CONFIG_ATTR_END,
		0
	}
};

static int kbase_pci_device_probe(struct pci_dev *pdev,
				  const struct pci_device_id *pci_id)
{
	const kbase_device_info	*dev_info;
	kbase_device		*kbdev;
	kbase_os_device		*osdev;
	kbase_attribute     *platform_data;
	int err;

	dev_info = &kbase_dev_info[pci_id->driver_data];
	kbdev = kbase_device_create(dev_info);
	if (!kbdev)
	{
		dev_err(&pdev->dev, "Can't allocate device\n");
		err = -ENOMEM;
		goto out;
	}

	osdev = &kbdev->osdev;
	osdev->dev = &pdev->dev;
	platform_data = (kbase_attribute *)osdev->dev->platform_data;

	err = pci_enable_device(pdev);
	if (err)
	{
		goto out_free_dev;
	}

	osdev->reg_start = pci_resource_start(pdev, 0);
	osdev->reg_size = pci_resource_len(pdev, 0);
	if (!(pci_resource_flags(pdev, 0) & IORESOURCE_MEM))
	{
		err = -EINVAL;
		goto out_disable;
	}

	osdev->irqs[0].irq = pdev->irq;
	osdev->irqs[1].irq = pdev->irq;
	osdev->irqs[2].irq = pdev->irq;

	pci_set_master(pdev);

	if (MALI_TRUE != kbasep_validate_configuration_attributes(pci_attributes))
	{
		err = -EINVAL;
		goto out_disable;
	}
	/* Use the master passed in instead of the pci attributes */
	kbdev->config_attributes = platform_data;

	kbdev->memdev.ump_device_id = kbasep_get_config_value(pci_attributes,
			KBASE_CONFIG_ATTR_UMP_DEVICE);
	kbdev->memdev.per_process_memory_limit = kbasep_get_config_value(pci_attributes,
			KBASE_CONFIG_ATTR_MEMORY_PER_PROCESS_LIMIT);

	err = kbase_register_memory_regions(kbdev, pci_attributes);
	if (err)
	{
		goto out_disable;
	}

	/* obtain min/max configured gpu frequencies */
	{
		struct mali_base_gpu_core_props *core_props = &(kbdev->gpu_props.props.core_props);
		core_props->gpu_freq_khz_min = kbasep_get_config_value(platform_data,
															   KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MIN);
		core_props->gpu_freq_khz_max = kbasep_get_config_value(platform_data,
															   KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MAX);
	}

	err = kbase_common_device_init(kbdev);
	if (err)
	{
		goto out_disable;
	}

	return 0;

out_disable:
	pci_disable_device(pdev);
out_free_dev:
	kbase_device_destroy(kbdev);
out:
	return err;
}

static int kbase_platform_device_probe(struct platform_device *pdev)
{
	struct kbase_device	*kbdev;
	kbase_device_info	*dev_info;
	struct kbase_os_device	*osdev;
	struct resource		*reg_res;
	kbase_attribute     *platform_data;
	int			err;
	int			i;

	dev_info = (kbase_device_info *)pdev->id_entry->driver_data;
	kbdev = kbase_device_create(dev_info);

	if (!kbdev)
	{
		dev_err(&pdev->dev, "Can't allocate device\n");
		err = -ENOMEM;
		goto out;
	}

	osdev = &kbdev->osdev;
	osdev->dev = &pdev->dev;
	platform_data = (kbase_attribute *)osdev->dev->platform_data;

	if (NULL == platform_data)
	{
		dev_err(osdev->dev, "Platform data not specified\n");
		err = -ENOENT;
		goto out_free_dev;
	}

	if (MALI_TRUE != kbasep_validate_configuration_attributes(platform_data))
	{
		dev_err(osdev->dev, "Configuration attributes failed to validate\n");
		err = -EINVAL;
		goto out_free_dev;
	}
	kbdev->config_attributes = platform_data;

	kbdev->memdev.ump_device_id = kbasep_get_config_value(platform_data,
			KBASE_CONFIG_ATTR_UMP_DEVICE);
	kbdev->memdev.per_process_memory_limit = kbasep_get_config_value(platform_data,
			KBASE_CONFIG_ATTR_MEMORY_PER_PROCESS_LIMIT);

	/* obtain min/max configured gpu frequencies */
	{
		struct mali_base_gpu_core_props *core_props = &(kbdev->gpu_props.props.core_props);
		core_props->gpu_freq_khz_min = kbasep_get_config_value(platform_data,
															   KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MIN);
		core_props->gpu_freq_khz_max = kbasep_get_config_value(platform_data,
															   KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MAX);
	}

	/* 3 IRQ resources */
	for (i = 0; i < 3; i++)
	{
		struct resource	*irq_res;

		irq_res = platform_get_resource(pdev, IORESOURCE_IRQ, i);
		if (!irq_res)
		{
			dev_err(osdev->dev, "No IRQ resource at index %d\n", i);
			err = -ENOENT;
			goto out_free_dev;
		}

		osdev->irqs[i].irq = irq_res->start;
		osdev->irqs[i].flags = (irq_res->flags & IRQF_TRIGGER_MASK);
	}

	/* the first memory resource is the physical address of the GPU registers */
	reg_res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!reg_res)
	{
		dev_err(&pdev->dev, "Invalid register resource\n");
		err = -ENOENT;
		goto out_free_dev;
	}

	osdev->reg_start = reg_res->start;
	osdev->reg_size = resource_size(reg_res);

	err = kbase_register_memory_regions(kbdev, (kbase_attribute *)osdev->dev->platform_data);
	if (err)
	{
		dev_err(osdev->dev, "Failed to register memory regions\n");
		goto out_free_dev;
	}

	err = kbase_common_device_init(kbdev);
	if (err)
	{
		dev_err(osdev->dev, "Failed kbase_common_device_init\n");
		goto out_free_dev;
	}
	return 0;

out_free_dev:
	kbase_device_destroy(kbdev);
out:
	return err;
}
#endif /* MALI_LICENSE_IS_GPL */

static int kbase_common_device_remove(struct kbase_device *kbdev)
{
#if BASE_HW_ISSUE_8401
	kbasep_8401_workaround_term(kbdev);
#endif
#if MALI_LICENSE_IS_GPL
	/* Remove the sys power policy file */
	device_remove_file(kbdev->osdev.dev, &dev_attr_power_policy);
#if MALI_DEBUG
	device_remove_file(kbdev->osdev.dev, &dev_attr_debug_command);
#endif
#endif
#ifdef CONFIG_VITHAR
	kbase_platform_remove_sysfs_file(kbdev->osdev.dev);
	kbase_platform_term(kbdev->osdev.dev);
#endif
	kbasep_js_devdata_halt(kbdev);
	kbase_job_slot_halt(kbdev);
	kbase_mem_halt(kbdev);
	kbase_pm_halt(kbdev);

	kbase_release_interrupts(kbdev);

	kbasep_js_devdata_term(kbdev);
	kbase_job_slot_term(kbdev);
	kbase_mem_term(kbdev);
	kbase_pm_term(kbdev);

#if MALI_NO_MALI
	midg_device_destroy(kbdev);
#endif

#if MALI_LICENSE_IS_GPL
	down(&kbase_dev_list_lock);
	list_del(&kbdev->osdev.entry);
	up(&kbase_dev_list_lock);
	misc_deregister(&kbdev->osdev.mdev);
	put_device(kbdev->osdev.dev);
#endif
	iounmap(kbdev->osdev.reg);
	release_resource(kbdev->osdev.reg_res);
	kfree(kbdev->osdev.reg_res);

	kbase_device_destroy(kbdev);

	return 0;
}


#if MALI_LICENSE_IS_GPL
static void kbase_pci_device_remove(struct pci_dev *pdev)
{
	struct kbase_device *kbdev = to_kbase_device(&pdev->dev);

	if (!kbdev)
	{
		return;
	}

	kbase_common_device_remove(kbdev);
	pci_disable_device(pdev);
}

static int kbase_platform_device_remove(struct platform_device *pdev)
{
	struct kbase_device *kbdev = to_kbase_device(&pdev->dev);

	if (!kbdev)
	{
		return -ENODEV;
	}

	return kbase_common_device_remove(kbdev);
}

/** Suspend callback from the OS.
 *
 * This is called by Linux when the device should suspend.
 *
 * @param dev	The device to suspend
 *
 * @return A standard Linux error code
 */
static int kbase_device_suspend(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}

	/* Send the event to the power policy */
	kbase_pm_send_event(kbdev, KBASE_PM_EVENT_SYSTEM_SUSPEND);

	/* Wait for the policy to suspend the device */
	kbase_pm_wait_for_power_down(kbdev);

#ifdef CONFIG_VITHAR
	kbase_platform_cmu_pmu_control(dev, 0);
#endif

	return 0;
}

/** Resume callback from the OS.
 *
 * This is called by Linux when the device should resume from suspension.
 *
 * @param dev	The device to resume
 *
 * @return A standard Linux error code
 */
static int kbase_device_resume(struct device *dev)
{
	struct kbase_device *kbdev = to_kbase_device(dev);

	if (!kbdev)
	{
		return -ENODEV;
	}
#ifdef CONFIG_VITHAR
	kbase_platform_cmu_pmu_control(dev, 1);
#endif

	/* Send the event to the power policy */
	kbase_pm_send_event(kbdev, KBASE_PM_EVENT_SYSTEM_RESUME);

	/* Wait for the policy to resume the device */
	kbase_pm_wait_for_power_up(kbdev);

	return 0;
}

#define kbdev_info(x) ((kernel_ulong_t)&kbase_dev_info[(x)])

static struct platform_device_id kbase_platform_id_table[] =
{
	{
		.name		= "mali-t6xm",
		.driver_data	= kbdev_info(KBASE_MALI_T6XM),
	},
	{
		.name		= "mali-t6f1",
		.driver_data	= kbdev_info(KBASE_MALI_T6F1),
	},
	{
		.name		= "mali-t601",
		.driver_data	= kbdev_info(KBASE_MALI_T601),
	},
	{
		.name		= "mali-t604",
		.driver_data	= kbdev_info(KBASE_MALI_T604),
	},
	{
		.name		= "mali-t608",
		.driver_data	= kbdev_info(KBASE_MALI_T608),
	},
	{},
};

MODULE_DEVICE_TABLE(platform, kbase_platform_id_table);

static DEFINE_PCI_DEVICE_TABLE(kbase_pci_id_table) =
{
	{ PCI_DEVICE(0x13b5, 0x6956), 0, 0, KBASE_MALI_T6XM },
	{},
};
MODULE_DEVICE_TABLE(pci, kbase_pci_id_table);

/** The power management operations for the platform driver.
 */
static struct dev_pm_ops kbase_pm_ops =
{
	.suspend	= kbase_device_suspend,
	.resume		= kbase_device_resume,
#ifdef CONFIG_VITHAR_RT_PM
	.runtime_suspend	= kbase_device_runtime_suspend,
	.runtime_resume		= kbase_device_runtime_resume,
#endif
};

static struct platform_driver kbase_platform_driver =
{
	.probe		= kbase_platform_device_probe,
	.remove		= kbase_platform_device_remove,
	.driver		=
	{
		.name		= kbase_drv_name,
		.owner		= THIS_MODULE,
		.pm 		= &kbase_pm_ops,
	},
	.id_table	= kbase_platform_id_table,
};

static struct pci_driver kbase_pci_driver =
{
	.name		= KBASE_DRV_NAME,
	.probe		= kbase_pci_device_probe,
	.remove		= kbase_pci_device_remove,
	.id_table	= kbase_pci_id_table,
};
#endif /* MALI_LICENSE_IS_GPL */

#if MALI_LICENSE_IS_GPL && MALI_FAKE_PLATFORM_DEVICE
static struct platform_device *mali_device;
#endif /* MALI_LICENSE_IS_GPL && MALI_FAKE_PLATFORM_DEVICE */

#if MALI_LICENSE_IS_GPL
static int __init kbase_driver_init(void)
{
	int err;
#if MALI_FAKE_PLATFORM_DEVICE
	kbase_platform_config *config;
	int attribute_count;
	struct resource resources[PLATFORM_CONFIG_RESOURCE_COUNT];

	config = kbasep_get_platform_config();
	attribute_count = kbasep_get_config_attribute_count(config->attributes);
	mali_device = platform_device_alloc( kbasep_midgard_type_to_string(config->midgard_type), 0);
	if (mali_device == NULL)
	{
		return -ENOMEM;
	}

	kbasep_config_parse_io_resources(config->io_resources, resources);
	err = platform_device_add_resources(mali_device, resources, PLATFORM_CONFIG_RESOURCE_COUNT);
	if (err)
	{
		platform_device_put(mali_device);
		mali_device = NULL;
		return err;
	}

	err = platform_device_add_data(mali_device, config->attributes, attribute_count * sizeof(config->attributes[0]));
	if (err)
	{
		platform_device_unregister(mali_device);
		mali_device = NULL;
		return err;
	}

	err = platform_device_add(mali_device);
	if (err)
	{
		platform_device_unregister(mali_device);
		mali_device = NULL;
		return err;
	}

#endif /* MALI_FAKE_PLATFORM_DEVICE */
	err = platform_driver_register(&kbase_platform_driver);
	if (err)
	{
		return err;
	}

	err = pci_register_driver(&kbase_pci_driver);
	if (err)
	{
		platform_driver_unregister(&kbase_platform_driver);
		return err;
	}

	return 0;
}
#else
static int __init kbase_driver_init(void)
{
	kbase_platform_config   *config;
	struct kbase_device     *kbdev;
	const kbase_device_info *dev_info;
	kbase_os_device         *osdev;
	int                     err;
	dev_t                   dev = 0;

	if (0 == mali_major)
	{
		/* auto select a major */
		err = alloc_chrdev_region(&dev, 0, 1, mali_dev_name);
		mali_major = MAJOR(dev);
	}
	else
	{
		/* use load time defined major number */
		dev = MKDEV(mali_major, 0);
		err = register_chrdev_region(dev, 1, mali_dev_name);
	}

	if (0 != err)
	{
		goto out_region;
	}

	memset(&mali_linux_device, 0, sizeof(mali_linux_device));

	/* initialize our char dev data */
	cdev_init(&mali_linux_device.cdev, &kbase_fops);
	mali_linux_device.cdev.owner = THIS_MODULE;
	mali_linux_device.cdev.ops = &kbase_fops;

	/* register char dev with the kernel */
	err = cdev_add(&mali_linux_device.cdev, dev, 1/*count*/);
	if (0 != err)
	{
		goto out_cdev_add;
	}
	config = kbasep_get_platform_config();
	if (MALI_TRUE != kbasep_validate_configuration_attributes(config->attributes))
	{
		err = -EINVAL;
		goto out_validate_attributes;
	}

	dev_info = &kbase_dev_info[config->midgard_type];
	kbdev = kbase_device_create(dev_info);
	if (!kbdev)
	{
		dev_err(&pdev->dev, "Can't allocate device\n");
		err = -ENOMEM;
		goto out_device_create;
	}
	kbdev->config_attributes = config->attributes;


	osdev = &kbdev->osdev;
	osdev->dev = &mali_linux_device.cdev;
	osdev->reg_start   = config->io_resources->io_memory_region.start;
	osdev->reg_size    = config->io_resources->io_memory_region.end - config->io_resources->io_memory_region.start + 1;
	osdev->irqs[0].irq = config->io_resources->job_irq_number;
	osdev->irqs[1].irq = config->io_resources->mmu_irq_number;
	osdev->irqs[2].irq = config->io_resources->gpu_irq_number;

	kbdev->memdev.per_process_memory_limit = kbasep_get_config_value(config->attributes,
			KBASE_CONFIG_ATTR_MEMORY_PER_PROCESS_LIMIT);
	kbdev->memdev.ump_device_id = kbasep_get_config_value(config->attributes, KBASE_CONFIG_ATTR_UMP_DEVICE);

	/* obtain min/max configured gpu frequencies */
	{
		struct mali_base_gpu_core_props *core_props = &(kbdev->gpu_props.props.core_props);
		core_props->gpu_freq_khz_min = kbasep_get_config_value(config->attributes,
															  KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MIN);
		core_props->gpu_freq_khz_max = kbasep_get_config_value(config->attributes,
															  KBASE_CONFIG_ATTR_GPU_FREQ_KHZ_MAX);
	}

	err = kbase_register_memory_regions(kbdev, config->attributes);
	if (err)
	{
		goto out_device_init;
	}

	err = kbase_common_device_init(kbdev);
	if (0 != err)
	{
		goto out_device_init;
	}

	g_kbdev = kbdev;

	return 0;

out_device_init:
	kbase_device_destroy(kbdev);
	g_kbdev = NULL;
out_validate_attributes:
out_device_create:
	cdev_del(&mali_linux_device.cdev);
out_cdev_add:
	unregister_chrdev_region(dev, 1);
out_region:
	return err;
}

#endif /* MALI_LICENSE_IS_GPL */

static void __exit kbase_driver_exit(void)
{
#if MALI_LICENSE_IS_GPL
	pci_unregister_driver(&kbase_pci_driver);
	platform_driver_unregister(&kbase_platform_driver);
#if MALI_FAKE_PLATFORM_DEVICE
	if (mali_device)
	{
		platform_device_unregister(mali_device);
	}
#endif
#else
	dev_t dev = MKDEV(mali_major, 0);
	struct kbase_device *kbdev = g_kbdev;

	if (!kbdev)
	{
		return;
	}

	kbase_common_device_remove(kbdev);

	/* unregister char device */
	cdev_del(&mali_linux_device.cdev);

	/* free major */
	unregister_chrdev_region(dev, 1);
#endif
}

module_init(kbase_driver_init);
module_exit(kbase_driver_exit);

#if MALI_LICENSE_IS_GPL
MODULE_LICENSE("GPL");
#else
MODULE_LICENSE("Proprietary");
#endif

#if MALI_GATOR_SUPPORT
/* Create the trace points (otherwise we just get code to call a tracepoint) */
#define CREATE_TRACE_POINTS
#include "mali_linux_trace.h"

void kbase_trace_mali_timeline_event(u32 event)
{
	trace_mali_timeline_event(event);
}
#endif

