/*
 *
 * (C) COPYRIGHT 2008-2011 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 * 
 */



#if UMP_LICENSE_IS_GPL
#define UMP_KERNEL_LINUX_LICENSE "GPL"
#else
#define UMP_KERNEL_LINUX_LICENSE "Proprietary"
#endif

#include <ump/src/ump_ioctl.h>
#include <ump/ump_kernel_interface.h>

#include <asm/uaccess.h>	         /* copy_*_user */
#include <linux/module.h>            /* kernel module definitions */
#include <linux/fs.h>                /* file system operations */
#include <linux/cdev.h>              /* character device definitions */
#include <linux/ioport.h>            /* request_mem_region */

#if UMP_LICENSE_IS_GPL
#include <linux/device.h>            /* class registration support */
#endif

#include <common/ump_kernel_core.h>

#include "ump_kernel_linux_mem.h"
#include <ump_arch.h>


struct ump_linux_device
{
	struct cdev cdev;
#if UMP_LICENSE_IS_GPL
	struct class * ump_class;
#endif
};

/* Name of the UMP device driver */
static char ump_dev_name[] = "ump"; /* should be const, but the functions we call requires non-cost */

/* Module parameter to control log level */
int ump_debug_level = 2;
module_param(ump_debug_level, int, S_IRUSR | S_IWUSR | S_IWGRP | S_IRGRP | S_IROTH); /* rw-rw-r-- */
MODULE_PARM_DESC(ump_debug_level, "Higher number, more dmesg output");

/* By default the module uses any available major, but it's possible to set it at load time to a specific number */
int ump_major = 0;
module_param(ump_major, int, S_IRUGO); /* r--r--r-- */
MODULE_PARM_DESC(ump_major, "Device major number");

char * ump_revision = UMP_SVN_REV_STRING;
module_param(ump_revision, charp, S_IRUGO); /* r--r--r-- */
MODULE_PARM_DESC(ump_revision, "Revision info");

static int umpp_linux_open(struct inode *inode, struct file *filp);
static int umpp_linux_release(struct inode *inode, struct file *filp);
#ifdef HAVE_UNLOCKED_IOCTL
static long umpp_linux_ioctl(struct file *filp, unsigned int cmd, unsigned long arg);
#else
static int umpp_linux_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg);
#endif

/* This variable defines the file operations this UMP device driver offers */
static struct file_operations ump_fops =
{
	.owner   = THIS_MODULE,
	.open    = umpp_linux_open,
	.release = umpp_linux_release,
#ifdef HAVE_UNLOCKED_IOCTL
	.unlocked_ioctl   = umpp_linux_ioctl,
#else
	.ioctl   = umpp_linux_ioctl,
#endif
	.compat_ioctl = umpp_linux_ioctl,
	.mmap = umpp_linux_mmap
};

/* import module handling */
DEFINE_MUTEX(import_list_lock);
struct ump_import_handler *  import_handlers[UMPP_EXTERNAL_MEM_COUNT];

/* The global variable containing the global device data */
static struct ump_linux_device ump_linux_device;

#define DBG_MSG(level, ...) do { \
if ((level) <=  ump_debug_level)\
{\
printk(KERN_DEBUG "UMP<" #level ">:\n" __VA_ARGS__);\
} \
} while (0)

#define MSG_ERR(...) do{ \
printk(KERN_ERR "UMP: ERR: %s\n           %s()%4d\n", __FILE__, __func__  , __LINE__) ; \
printk(KERN_ERR __VA_ARGS__); \
printk(KERN_ERR "\n"); \
} while(0)

#define MSG(...) do{ \
printk(KERN_INFO "UMP: " __VA_ARGS__);\
} while (0)

/*
 * This function is called by Linux to initialize this module.
 * All we do is initialize the UMP device driver.
 */
static int __init umpp_linux_initialize_module(void)
{
	ump_result err;

	DBG_MSG(2, "Inserting UMP device driver. Compiled: %s, time: %s\n", __DATE__, __TIME__);

	err = umpp_core_constructor();
	if (UMP_OK != err)
	{
		MSG_ERR("UMP device driver init failed\n");
		return -ENOTTY;
	}

	MSG("UMP device driver %s loaded\n", UMP_SVN_REV_STRING);

	return 0;
}



/*
 * This function is called by Linux to unload/terminate/exit/cleanup this module.
 * All we do is terminate the UMP device driver.
 */
static void __exit umpp_linux_cleanup_module(void)
{
	DBG_MSG(2, "Unloading UMP device driver\n");
	umpp_core_destructor();
	DBG_MSG(2, "Module unloaded\n");
}



/*
 * Initialize the UMP device driver.
 */
ump_result umpp_device_initialize(void)
{
	int err;
	dev_t dev = 0;

	if (0 == ump_major)
	{
		/* auto select a major */
		err = alloc_chrdev_region(&dev, 0, 1, ump_dev_name);
		ump_major = MAJOR(dev);
	}
	else
	{
		/* use load time defined major number */
		dev = MKDEV(ump_major, 0);
		err = register_chrdev_region(dev, 1, ump_dev_name);
	}

	if (0 == err)
	{
		memset(&ump_linux_device, 0, sizeof(ump_linux_device));

		/* initialize our char dev data */
		cdev_init(&ump_linux_device.cdev, &ump_fops);
		ump_linux_device.cdev.owner = THIS_MODULE;
		ump_linux_device.cdev.ops = &ump_fops;

		/* register char dev with the kernel */
		err = cdev_add(&ump_linux_device.cdev, dev, 1/*count*/);
		if (0 == err)
		{

#if UMP_LICENSE_IS_GPL
			ump_linux_device.ump_class = class_create(THIS_MODULE, ump_dev_name);
			if (IS_ERR(ump_linux_device.ump_class))
			{
				err = PTR_ERR(ump_linux_device.ump_class);
			}
			else
			{
				struct device * mdev;
				mdev = device_create(ump_linux_device.ump_class, NULL, dev, NULL, ump_dev_name);
				if (!IS_ERR(mdev))
				{
					return UMP_OK;
				}

				err = PTR_ERR(mdev);
				class_destroy(ump_linux_device.ump_class);
			}
			cdev_del(&ump_linux_device.cdev);
#else
			return UMP_OK;
#endif
		}

		unregister_chrdev_region(dev, 1);
	}

	return UMP_ERROR;
}



/*
 * Terminate the UMP device driver
 */
void umpp_device_terminate(void)
{
	dev_t dev = MKDEV(ump_major, 0);

#if UMP_LICENSE_IS_GPL
	device_destroy(ump_linux_device.ump_class, dev);
	class_destroy(ump_linux_device.ump_class);
#endif

	/* unregister char device */
	cdev_del(&ump_linux_device.cdev);

	/* free major */
	unregister_chrdev_region(dev, 1);
}


static int umpp_linux_open(struct inode *inode, struct file *filp)
{
	umpp_session *session;
	
	session = umpp_core_session_start();
	if (NULL == session) 
	{
		return -EFAULT;
	}
	
	filp->private_data = session;

	return 0;
}

static int umpp_linux_release(struct inode *inode, struct file *filp)
{
	umpp_session *session;
	
	session = filp->private_data;

	umpp_core_session_end(session);

	filp->private_data = NULL;

	return 0;
}

/**************************/
/*ioctl specific functions*/
/**************************/
static int do_ump_dd_allocate(umpp_session * session, ump_k_allocate * params)
{
	ump_dd_handle new_allocation;
	new_allocation = ump_dd_allocate_64(params->size, params->alloc_flags, NULL, NULL, NULL);

	if (UMP_DD_INVALID_MEMORY_HANDLE != new_allocation)
	{
		umpp_session_memory_usage * tracker;

		tracker = kmalloc(sizeof(*tracker), GFP_KERNEL | __GFP_HARDWALL);
		if (NULL != tracker)
		{
			/* update the return struct with the new ID */
			params->secure_id = ump_dd_secure_id_get(new_allocation);

			tracker->mem = new_allocation;
			tracker->id = params->secure_id;
			atomic_set(&tracker->process_usage_count, 1);

			/* link it into the session in-use list */
			mutex_lock(&session->session_lock);
			list_add(&tracker->link, &session->memory_usage);
			mutex_unlock(&session->session_lock);

			return 0;
		}
		ump_dd_release(new_allocation);
	}

	printk(KERN_WARNING "UMP: Allocation FAILED\n");
	return -ENOMEM;
}

static int do_ump_dd_retain(umpp_session * session, ump_k_retain * params)
{
	umpp_session_memory_usage * it;

	mutex_lock(&session->session_lock);

	/* try to find it on the session usage list */
	list_for_each_entry(it, &session->memory_usage, link)
	{
		if (it->id == params->secure_id)
		{
			/* found to already be in use */
			/* check for overflow */
			while(1)
			{
				int refcnt = atomic_read(&it->process_usage_count);
				if (refcnt + 1 > 0)
				{
					/* add a process local ref */
					if(atomic_cmpxchg(&it->process_usage_count, refcnt, refcnt + 1) == refcnt)
					{
						mutex_unlock(&session->session_lock);
						return 0;
					}
				}
				else
				{
					/* maximum usage cap reached */
					mutex_unlock(&session->session_lock);
					return -EBUSY;
				}
			}
		}
	}
	/* try to look it up globally */

	it = kmalloc(sizeof(*it), GFP_KERNEL);

	if (NULL != it)
	{
		it->mem = ump_dd_from_secure_id(params->secure_id);
		if (UMP_DD_INVALID_MEMORY_HANDLE != it->mem)
		{
			/* found, add it to the session usage list */
			it->id = params->secure_id;
			atomic_set(&it->process_usage_count, 1);
			list_add(&it->link, &session->memory_usage);
		}
		else
		{
			/* not found */
			kfree(it);
			it = NULL;
		}
	}

	mutex_unlock(&session->session_lock);

	return (NULL != it) ? 0 : -ENODEV;
}


static int do_ump_dd_release(umpp_session * session, ump_k_release * params)
{
	umpp_session_memory_usage * it;
	int result = -ENODEV;

	mutex_lock(&session->session_lock);

	/* only do a release if found on the session list */
	list_for_each_entry(it, &session->memory_usage, link)
	{
		if (it->id == params->secure_id)
		{
			/* found, a valid call */
			result = 0;

			if (0 == atomic_sub_return(1, &it->process_usage_count))
			{
				/* last ref in this process remove from the usage list and remove the underlying ref */
				list_del(&it->link);
				ump_dd_release(it->mem);
				kfree(it);
			}

			break;
		}
	}
	mutex_unlock(&session->session_lock);

	return result;
}

static int do_ump_dd_sizequery(umpp_session * session, ump_k_sizequery * params)
{
	umpp_session_memory_usage * it;
	int result = -ENODEV;

	mutex_lock(&session->session_lock);

	/* only valid if found on the session list */
	list_for_each_entry(it, &session->memory_usage, link)
	{
		if (it->id == params->secure_id)
		{
			/* found, a valid call */
			params->size = ump_dd_size_get_64(it->mem);
			result = 0;
			break;
		}

	}
	mutex_unlock(&session->session_lock);

	return result;
}

static int do_ump_dd_allocation_flags_get(umpp_session * session, ump_k_allocation_flags * params)
{
	umpp_session_memory_usage * it;
	int result = -ENODEV;

	mutex_lock(&session->session_lock);

	/* only valid if found on the session list */
	list_for_each_entry(it, &session->memory_usage, link)
	{
		if (it->id == params->secure_id)
		{
			/* found, a valid call */
			params->alloc_flags = ump_dd_allocation_flags_get(it->mem);
			result = 0;
			break;
		}

	}
	mutex_unlock(&session->session_lock);

	return result;
}

static int do_ump_dd_msync_now(umpp_session * session, ump_k_msync * params)
{
	umpp_session_memory_usage * it;
	int result = -ENODEV;

	mutex_lock(&session->session_lock);

	/* only valid if found on the session list */
	list_for_each_entry(it, &session->memory_usage, link)
	{
		if (it->id == params->secure_id)
		{
			/* found, do the cache op */
#if 0
			/* waiting for OSK api as requested in MIDBASE-515 */
#if defined CONFIG_64BIT && CONFIG_64BIT
			if (is_compat_task())
			{
				umpp_dd_cpu_msync_now(it->mem, params->cache_operation, params->mapped_ptr.compat_value, params->size);
				result = 0;
			}
			else
			{
#endif
#endif
				umpp_dd_cpu_msync_now(it->mem, params->cache_operation, params->mapped_ptr.value, params->size);
				result = 0;
#if 0
				/* waiting for OSK api as requested in MIDBASE-515 */
#if defined CONFIG_64BIT && CONFIG_64BIT
			}
#endif
#endif
			break;
		}
	}
	mutex_unlock(&session->session_lock);

	return result;
}


void umpp_import_handlers_init(umpp_session * session)
{
	int i;
	mutex_lock(&import_list_lock);
	for ( i = 1; i < UMPP_EXTERNAL_MEM_COUNT; i++ )
	{
		if (import_handlers[i])
		{
			import_handlers[i]->session_begin(&session->import_handler_data[i]);
			/* It is OK if session_begin returned an error.
			 * We won't do any import calls if so */
		}
	}
	mutex_unlock(&import_list_lock);
}

void umpp_import_handlers_term(umpp_session * session)
{
	int i;
	mutex_lock(&import_list_lock);
	for ( i = 1; i < UMPP_EXTERNAL_MEM_COUNT; i++ )
	{
		/* only call if session_begin succeeded */
		if (session->import_handler_data[i] != NULL)
		{
			/* if session_beging succeeded the handler
			 * should not have unregistered with us */
			BUG_ON(!import_handlers[i]);
			import_handlers[i]->session_end(session->import_handler_data[i]);
			session->import_handler_data[i] = NULL;
		}
	}
	mutex_unlock(&import_list_lock);
}

int ump_import_module_register(enum ump_external_memory_type type, struct ump_import_handler * handler)
{
	int res = -EEXIST;

	/* validate input */
	BUG_ON(type == 0 || type >= UMPP_EXTERNAL_MEM_COUNT);
	BUG_ON(!handler);
#ifndef CONFIG_VITHAR
	BUG_ON(!handler->linux_module);
#endif
	BUG_ON(!handler->session_begin);
	BUG_ON(!handler->session_end);
	BUG_ON(!handler->import);

	mutex_lock(&import_list_lock);

	if (!import_handlers[type])
	{
		import_handlers[type] = handler;
		res = 0;
	}

	mutex_unlock(&import_list_lock);

	return res;
}

void ump_import_module_unregister(enum ump_external_memory_type type)
{
	BUG_ON(type == 0 || type >= UMPP_EXTERNAL_MEM_COUNT);

	mutex_lock(&import_list_lock);
	/* an error to call this if ump_import_module_register didn't succeed */
	BUG_ON(!import_handlers[type]);
	import_handlers[type] = NULL;
	mutex_unlock(&import_list_lock);
}

static struct ump_import_handler * import_handler_get(int type_id)
{
	enum ump_external_memory_type type;
	struct ump_import_handler * handler;

	/* validate and convert input */
	/* handle bad data here, not just BUG_ON */
	if (type_id == 0 || type_id >= UMPP_EXTERNAL_MEM_COUNT)
		return NULL;

	type = (enum ump_external_memory_type)type_id;

	/* find the handler */
	mutex_lock(&import_list_lock);

	handler = import_handlers[type];

	if (handler)
	{
		if (!try_module_get(handler->linux_module))
		{
			handler = NULL;
		}
	}

	mutex_unlock(&import_list_lock);

	return handler;
}

static void import_handler_put(struct ump_import_handler * handler)
{
	module_put(handler->linux_module);
}

static int do_ump_dd_import(umpp_session * session, ump_k_import * params)
{
	ump_dd_handle new_allocation = UMP_DD_INVALID_MEMORY_HANDLE;
	struct ump_import_handler * handler;

	handler = import_handler_get(params->type);

	if (handler)
	{
		/* try late binding if not already bound */
		if (!session->import_handler_data[params->type])
		{
			handler->session_begin(&session->import_handler_data[params->type]);
		}

		/* do we have a bound session? */
		if (session->import_handler_data[params->type])
		{
			new_allocation = handler->import( session->import_handler_data[params->type],
		                                      params->phandle.value,
		                                      params->alloc_flags);
		}

		/* done with the handler */
		import_handler_put(handler);
	}

	/* did the import succeed? */
	if (UMP_DD_INVALID_MEMORY_HANDLE != new_allocation)
	{
		umpp_session_memory_usage * tracker;

		tracker = kmalloc(sizeof(*tracker), GFP_KERNEL | __GFP_HARDWALL);
		if (NULL != tracker)
		{
			/* update the return struct with the new ID */
			params->secure_id = ump_dd_secure_id_get(new_allocation);

			tracker->mem = new_allocation;
			tracker->id = params->secure_id;
			atomic_set(&tracker->process_usage_count, 1);

			/* link it into the session in-use list */
			mutex_lock(&session->session_lock);
			list_add(&tracker->link, &session->memory_usage);
			mutex_unlock(&session->session_lock);

			return 0;
		}
		ump_dd_release(new_allocation);
	}

	return -ENOMEM;

}

#ifdef HAVE_UNLOCKED_IOCTL
static long umpp_linux_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
#else
static int umpp_linux_ioctl(struct inode *inode, struct file *filp, unsigned int cmd, unsigned long arg)
#endif
{
	int ret;
	uint64_t msg[(UMP_CALL_MAX_SIZE+7)>>3]; /* alignment fixup */
	uint32_t size = _IOC_SIZE(cmd);
	struct umpp_session *session = filp->private_data;

#ifndef HAVE_UNLOCKED_IOCTL
	(void)inode; /* unused arg */
#endif

	/*
	 * extract the type and number bitfields, and don't decode
	 * wrong cmds: return ENOTTY (inappropriate ioctl) before access_ok()
	 */
	if (_IOC_TYPE(cmd) != UMP_IOC_MAGIC)
	{
		return -ENOTTY;

	}
	if (_IOC_NR(cmd) > UMP_IOC_MAXNR)
	{
		return -ENOTTY;
	}

	switch(cmd)
	{
		case UMP_FUNC_ALLOCATE:
			if (size != sizeof(ump_k_allocate))
			{
				return -ENOTTY;
			}
			if (copy_from_user(&msg, (void __user *)arg, size))
			{
				return -EFAULT;
			}
			ret = do_ump_dd_allocate(session, (ump_k_allocate *)&msg);
			if (ret)
			{
				return ret;
			}
			if (copy_to_user((void *)arg, &msg, size))
			{
				return -EFAULT;
			}
			return 0;
		case UMP_FUNC_SIZEQUERY:
			if (size != sizeof(ump_k_sizequery))
			{
				return -ENOTTY;
			}
			if (copy_from_user(&msg, (void __user *)arg, size))
			{
				return -EFAULT;
			}
			ret = do_ump_dd_sizequery(session,(ump_k_sizequery*) &msg);
			if (ret)
			{
				return ret;
			}
			if (copy_to_user((void *)arg, &msg, size))
			{
				return -EFAULT;
			}
			return 0;
		case UMP_FUNC_MSYNC:
			if (size != sizeof(ump_k_msync))
			{
				return -ENOTTY;
			}
			if (copy_from_user(&msg, (void __user *)arg, size))
			{
				return -EFAULT;
			}
			ret = do_ump_dd_msync_now(session,(ump_k_msync*) &msg);
			if (ret)
			{
				return ret;
			}
			if (copy_to_user((void *)arg, &msg, size))
			{
				return -EFAULT;
			}
			return 0;
		case UMP_FUNC_IMPORT:
			if (size != sizeof(ump_k_import))
			{
				return -ENOTTY;
			}
			if (copy_from_user(&msg, (void __user*)arg, size))
			{
				return -EFAULT;
			}
			ret = do_ump_dd_import(session, (ump_k_import*) &msg);
			if (ret)
			{
				return ret;
			}
			if (copy_to_user((void *)arg, &msg, size))
			{
				return -EFAULT;
			}
			return 0;
		/* used only by v1 API */
		case UMP_FUNC_ALLOCATION_FLAGS_GET:
			if (size != sizeof(ump_k_allocation_flags))
			{
				return -ENOTTY;
			}
			if (copy_from_user(&msg, (void __user *)arg, size))
			{
				return -EFAULT;
			}
			ret = do_ump_dd_allocation_flags_get(session,(ump_k_allocation_flags*) &msg);
			if (ret)
			{
				return ret;
			}
			if (copy_to_user((void *)arg, &msg, size))
			{
				return -EFAULT;
			}
			return 0;
		case UMP_FUNC_RETAIN:
			if (size != sizeof(ump_k_retain))
			{
				return -ENOTTY;
			}
			if (copy_from_user(&msg, (void __user *)arg, size))
			{
				return -EFAULT;
			}
			ret = do_ump_dd_retain(session,(ump_k_retain*) &msg);
			if (ret)
			{
				return ret;
			}
			return 0;
		case UMP_FUNC_RELEASE:
			if (size != sizeof(ump_k_release))
			{
				return -ENOTTY;
			}
			if (copy_from_user(&msg, (void __user *)arg, size))
			{
				return -EFAULT;
			}
			ret = do_ump_dd_release(session,(ump_k_release*) &msg);
			if (ret)
			{
				return ret;
			}
			return 0;
		default:
			/* not ours */
			return -ENOTTY;
	}
	/*redundant below*/
	return -ENOTTY;
}


/* Export UMP kernel space API functions */
EXPORT_SYMBOL(ump_dd_allocate_64);
EXPORT_SYMBOL(ump_dd_allocation_flags_get);
EXPORT_SYMBOL(ump_dd_secure_id_get);
EXPORT_SYMBOL(ump_dd_from_secure_id);
EXPORT_SYMBOL(ump_dd_phys_blocks_get_64);
EXPORT_SYMBOL(ump_dd_size_get_64);
EXPORT_SYMBOL(ump_dd_retain);
EXPORT_SYMBOL(ump_dd_release);
EXPORT_SYMBOL(ump_dd_create_from_phys_blocks_64);

/* import API */
EXPORT_SYMBOL(ump_import_module_register);
EXPORT_SYMBOL(ump_import_module_unregister);



/* V1 API */
EXPORT_SYMBOL(ump_dd_handle_create_from_secure_id);
EXPORT_SYMBOL(ump_dd_phys_block_count_get);
EXPORT_SYMBOL(ump_dd_phys_block_get);
EXPORT_SYMBOL(ump_dd_phys_blocks_get);
EXPORT_SYMBOL(ump_dd_size_get);
EXPORT_SYMBOL(ump_dd_reference_add);
EXPORT_SYMBOL(ump_dd_reference_release);
EXPORT_SYMBOL(ump_dd_handle_create_from_phys_blocks);


/* Setup init and exit functions for this module */
module_init(umpp_linux_initialize_module);
module_exit(umpp_linux_cleanup_module);

/* And some module informatio */
MODULE_LICENSE(UMP_KERNEL_LINUX_LICENSE);
MODULE_AUTHOR("ARM Ltd.");
MODULE_VERSION(UMP_SVN_REV_STRING);
