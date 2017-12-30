// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright IBM Corp. 2004, 2010
 * Interface implementation for communication with the z/VM control program
 *
 * Author(s): Christian Borntraeger <borntraeger@de.ibm.com>
 *
 * z/VMs CP offers the possibility to issue commands via the diagnose code 8
 * this driver implements a character device that issues these commands and
 * returns the answer of CP.
 *
 * The idea of this driver is based on cpint from Neale Ferguson and #CP in CMS
 */

#include <linux/fs.h>
#include <linux/init.h>
#include <linux/compat.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/export.h>
#include <linux/mutex.h>
#include <linux/cma.h>
#include <linux/mm.h>
#include <asm/compat.h>
#include <asm/cpcmd.h>
#include <asm/debug.h>
#include <asm/vmcp.h>

struct vmcp_session {
	char *response;
	unsigned int bufsize;
	unsigned int cma_alloc : 1;
	int resp_size;
	int resp_code;
	struct mutex mutex;
};

static debug_info_t *vmcp_debug;

static unsigned long vmcp_cma_size __initdata = CONFIG_VMCP_CMA_SIZE * 1024 * 1024;
static struct cma *vmcp_cma;

static int __init early_parse_vmcp_cma(char *p)
{
	vmcp_cma_size = ALIGN(memparse(p, NULL), PAGE_SIZE);
	return 0;
}
early_param("vmcp_cma", early_parse_vmcp_cma);

void __init vmcp_cma_reserve(void)
{
	if (!MACHINE_IS_VM)
		return;
	cma_declare_contiguous(0, vmcp_cma_size, 0, 0, 0, false, "vmcp", &vmcp_cma);
}

static void vmcp_response_alloc(struct vmcp_session *session)
{
	struct page *page = NULL;
	int nr_pages, order;

	order = get_order(session->bufsize);
	nr_pages = ALIGN(session->bufsize, PAGE_SIZE) >> PAGE_SHIFT;
	/*
	 * For anything below order 3 allocations rely on the buddy
	 * allocator. If such low-order allocations can't be handled
	 * anymore the system won't work anyway.
	 */
	if (order > 2)
		page = cma_alloc(vmcp_cma, nr_pages, 0, GFP_KERNEL);
	if (page) {
		session->response = (char *)page_to_phys(page);
		session->cma_alloc = 1;
		return;
	}
	session->response = (char *)__get_free_pages(GFP_KERNEL | __GFP_RETRY_MAYFAIL, order);
}

static void vmcp_response_free(struct vmcp_session *session)
{
	int nr_pages, order;
	struct page *page;

	if (!session->response)
		return;
	order = get_order(session->bufsize);
	nr_pages = ALIGN(session->bufsize, PAGE_SIZE) >> PAGE_SHIFT;
	if (session->cma_alloc) {
		page = phys_to_page((unsigned long)session->response);
		cma_release(vmcp_cma, page, nr_pages);
		session->cma_alloc = 0;
	} else {
		free_pages((unsigned long)session->response, order);
	}
	session->response = NULL;
}

static int vmcp_open(struct inode *inode, struct file *file)
{
	struct vmcp_session *session;

	if (!capable(CAP_SYS_ADMIN))
		return -EPERM;

	session = kmalloc(sizeof(*session), GFP_KERNEL);
	if (!session)
		return -ENOMEM;

	session->bufsize = PAGE_SIZE;
	session->response = NULL;
	session->resp_size = 0;
	mutex_init(&session->mutex);
	file->private_data = session;
	return nonseekable_open(inode, file);
}

static int vmcp_release(struct inode *inode, struct file *file)
{
	struct vmcp_session *session;

	session = file->private_data;
	file->private_data = NULL;
	vmcp_response_free(session);
	kfree(session);
	return 0;
}

static ssize_t
vmcp_read(struct file *file, char __user *buff, size_t count, loff_t *ppos)
{
	ssize_t ret;
	size_t size;
	struct vmcp_session *session;

	session = file->private_data;
	if (mutex_lock_interruptible(&session->mutex))
		return -ERESTARTSYS;
	if (!session->response) {
		mutex_unlock(&session->mutex);
		return 0;
	}
	size = min_t(size_t, session->resp_size, session->bufsize);
	ret = simple_read_from_buffer(buff, count, ppos,
					session->response, size);

	mutex_unlock(&session->mutex);

	return ret;
}

static ssize_t
vmcp_write(struct file *file, const char __user *buff, size_t count,
	   loff_t *ppos)
{
	char *cmd;
	struct vmcp_session *session;

	if (count > 240)
		return -EINVAL;
	cmd = memdup_user_nul(buff, count);
	if (IS_ERR(cmd))
		return PTR_ERR(cmd);
	session = file->private_data;
	if (mutex_lock_interruptible(&session->mutex)) {
		kfree(cmd);
		return -ERESTARTSYS;
	}
	if (!session->response)
		vmcp_response_alloc(session);
	if (!session->response) {
		mutex_unlock(&session->mutex);
		kfree(cmd);
		return -ENOMEM;
	}
	debug_text_event(vmcp_debug, 1, cmd);
	session->resp_size = cpcmd(cmd, session->response, session->bufsize,
				   &session->resp_code);
	mutex_unlock(&session->mutex);
	kfree(cmd);
	*ppos = 0;		/* reset the file pointer after a command */
	return count;
}


/*
 * These ioctls are available, as the semantics of the diagnose 8 call
 * does not fit very well into a Linux call. Diagnose X'08' is described in
 * CP Programming Services SC24-6084-00
 *
 * VMCP_GETCODE: gives the CP return code back to user space
 * VMCP_SETBUF: sets the response buffer for the next write call. diagnose 8
 * expects adjacent pages in real storage and to make matters worse, we
 * dont know the size of the response. Therefore we default to PAGESIZE and
 * let userspace to change the response size, if userspace expects a bigger
 * response
 */
static long vmcp_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	struct vmcp_session *session;
	int ret = -ENOTTY;
	int __user *argp;

	session = file->private_data;
	if (is_compat_task())
		argp = compat_ptr(arg);
	else
		argp = (int __user *)arg;
	if (mutex_lock_interruptible(&session->mutex))
		return -ERESTARTSYS;
	switch (cmd) {
	case VMCP_GETCODE:
		ret = put_user(session->resp_code, argp);
		break;
	case VMCP_SETBUF:
		vmcp_response_free(session);
		ret = get_user(session->bufsize, argp);
		if (ret)
			session->bufsize = PAGE_SIZE;
		if (!session->bufsize || get_order(session->bufsize) > 8) {
			session->bufsize = PAGE_SIZE;
			ret = -EINVAL;
		}
		break;
	case VMCP_GETSIZE:
		ret = put_user(session->resp_size, argp);
		break;
	default:
		break;
	}
	mutex_unlock(&session->mutex);
	return ret;
}

static const struct file_operations vmcp_fops = {
	.owner		= THIS_MODULE,
	.open		= vmcp_open,
	.release	= vmcp_release,
	.read		= vmcp_read,
	.write		= vmcp_write,
	.unlocked_ioctl	= vmcp_ioctl,
	.compat_ioctl	= vmcp_ioctl,
	.llseek		= no_llseek,
};

static struct miscdevice vmcp_dev = {
	.name	= "vmcp",
	.minor	= MISC_DYNAMIC_MINOR,
	.fops	= &vmcp_fops,
};

static int __init vmcp_init(void)
{
	int ret;

	if (!MACHINE_IS_VM)
		return 0;

	vmcp_debug = debug_register("vmcp", 1, 1, 240);
	if (!vmcp_debug)
		return -ENOMEM;

	ret = debug_register_view(vmcp_debug, &debug_hex_ascii_view);
	if (ret) {
		debug_unregister(vmcp_debug);
		return ret;
	}

	ret = misc_register(&vmcp_dev);
	if (ret)
		debug_unregister(vmcp_debug);
	return ret;
}
device_initcall(vmcp_init);
