// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Linux/SPARC PROM Configuration Driver
 * Copyright (C) 1996 Thomas K. Dyas (tdyas@noc.rutgers.edu)
 * Copyright (C) 1996 Eddie C. Dost  (ecd@skynet.be)
 *
 * This character device driver allows user programs to access the
 * PROM device tree. It is compatible with the SunOS /dev/openprom
 * driver and the NetBSD /dev/openprom driver. The SunOS eeprom
 * utility works without any modifications.
 *
 * The driver uses a minor number under the misc device major. The
 * file read/write mode determines the type of access to the PROM.
 * Interrupts are disabled whenever the driver calls into the PROM for
 * sanity's sake.
 */


#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/mutex.h>
#include <linux/string.h>
#include <linux/miscdevice.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <asm/oplib.h>
#include <asm/prom.h>
#include <linux/uaccess.h>
#include <asm/openpromio.h>
#ifdef CONFIG_PCI
#include <linux/pci.h>
#endif

MODULE_AUTHOR("Thomas K. Dyas <tdyas@noc.rutgers.edu> and Eddie C. Dost <ecd@skynet.be>");
MODULE_DESCRIPTION("OPENPROM Configuration Driver");
MODULE_LICENSE("GPL");
MODULE_VERSION("1.0");
MODULE_ALIAS_MISCDEV(SUN_OPENPROM_MINOR);

/* Private data kept by the driver for each descriptor. */
typedef struct openprom_private_data
{
	struct device_node *current_node; /* Current node for SunOS ioctls. */
	struct device_node *lastnode; /* Last valid node used by BSD ioctls. */
} DATA;

/* ID of the PROM node containing all of the EEPROM options. */
static DEFINE_MUTEX(openprom_mutex);
static struct device_node *options_node;

/*
 * Copy an openpromio structure into kernel space from user space.
 * This routine does error checking to make sure that all memory
 * accesses are within bounds. A pointer to the allocated openpromio
 * structure will be placed in "*opp_p". Return value is the length
 * of the user supplied buffer.
 */
static int copyin(struct openpromio __user *info, struct openpromio **opp_p)
{
	unsigned int bufsize;

	if (!info || !opp_p)
		return -EFAULT;

	if (get_user(bufsize, &info->oprom_size))
		return -EFAULT;

	if (bufsize == 0)
		return -EINVAL;

	/* If the bufsize is too large, just limit it.
	 * Fix from Jason Rappleye.
	 */
	if (bufsize > OPROMMAXPARAM)
		bufsize = OPROMMAXPARAM;

	if (!(*opp_p = kzalloc(sizeof(int) + bufsize + 1, GFP_KERNEL)))
		return -ENOMEM;

	if (copy_from_user(&(*opp_p)->oprom_array,
			   &info->oprom_array, bufsize)) {
		kfree(*opp_p);
		return -EFAULT;
	}
	return bufsize;
}

static int getstrings(struct openpromio __user *info, struct openpromio **opp_p)
{
	int n, bufsize;
	char c;

	if (!info || !opp_p)
		return -EFAULT;

	if (!(*opp_p = kzalloc(sizeof(int) + OPROMMAXPARAM + 1, GFP_KERNEL)))
		return -ENOMEM;

	(*opp_p)->oprom_size = 0;

	n = bufsize = 0;
	while ((n < 2) && (bufsize < OPROMMAXPARAM)) {
		if (get_user(c, &info->oprom_array[bufsize])) {
			kfree(*opp_p);
			return -EFAULT;
		}
		if (c == '\0')
			n++;
		(*opp_p)->oprom_array[bufsize++] = c;
	}
	if (!n) {
		kfree(*opp_p);
		return -EINVAL;
	}
	return bufsize;
}

/*
 * Copy an openpromio structure in kernel space back to user space.
 */
static int copyout(void __user *info, struct openpromio *opp, int len)
{
	if (copy_to_user(info, opp, len))
		return -EFAULT;
	return 0;
}

static int opromgetprop(void __user *argp, struct device_node *dp, struct openpromio *op, int bufsize)
{
	const void *pval;
	int len;

	if (!dp ||
	    !(pval = of_get_property(dp, op->oprom_array, &len)) ||
	    len <= 0 || len > bufsize)
		return copyout(argp, op, sizeof(int));

	memcpy(op->oprom_array, pval, len);
	op->oprom_array[len] = '\0';
	op->oprom_size = len;

	return copyout(argp, op, sizeof(int) + bufsize);
}

static int opromnxtprop(void __user *argp, struct device_node *dp, struct openpromio *op, int bufsize)
{
	struct property *prop;
	int len;

	if (!dp)
		return copyout(argp, op, sizeof(int));
	if (op->oprom_array[0] == '\0') {
		prop = dp->properties;
		if (!prop)
			return copyout(argp, op, sizeof(int));
		len = strlen(prop->name);
	} else {
		prop = of_find_property(dp, op->oprom_array, NULL);

		if (!prop ||
		    !prop->next ||
		    (len = strlen(prop->next->name)) + 1 > bufsize)
			return copyout(argp, op, sizeof(int));

		prop = prop->next;
	}

	memcpy(op->oprom_array, prop->name, len);
	op->oprom_array[len] = '\0';
	op->oprom_size = ++len;

	return copyout(argp, op, sizeof(int) + bufsize);
}

static int opromsetopt(struct device_node *dp, struct openpromio *op, int bufsize)
{
	char *buf = op->oprom_array + strlen(op->oprom_array) + 1;
	int len = op->oprom_array + bufsize - buf;

	return of_set_property(options_node, op->oprom_array, buf, len);
}

static int opromnext(void __user *argp, unsigned int cmd, struct device_node *dp, struct openpromio *op, int bufsize, DATA *data)
{
	phandle ph;

	BUILD_BUG_ON(sizeof(phandle) != sizeof(int));

	if (bufsize < sizeof(phandle))
		return -EINVAL;

	ph = *((int *) op->oprom_array);
	if (ph) {
		dp = of_find_node_by_phandle(ph);
		if (!dp)
			return -EINVAL;

		switch (cmd) {
		case OPROMNEXT:
			dp = dp->sibling;
			break;

		case OPROMCHILD:
			dp = dp->child;
			break;

		case OPROMSETCUR:
		default:
			break;
		}
	} else {
		/* Sibling of node zero is the root node.  */
		if (cmd != OPROMNEXT)
			return -EINVAL;

		dp = of_find_node_by_path("/");
	}

	ph = 0;
	if (dp)
		ph = dp->phandle;

	data->current_node = dp;
	*((int *) op->oprom_array) = ph;
	op->oprom_size = sizeof(phandle);

	return copyout(argp, op, bufsize + sizeof(int));
}

static int oprompci2node(void __user *argp, struct device_node *dp, struct openpromio *op, int bufsize, DATA *data)
{
	int err = -EINVAL;

	if (bufsize >= 2*sizeof(int)) {
#ifdef CONFIG_PCI
		struct pci_dev *pdev;
		struct device_node *dp;

		pdev = pci_get_domain_bus_and_slot(0,
						((int *) op->oprom_array)[0],
						((int *) op->oprom_array)[1]);

		dp = pci_device_to_OF_node(pdev);
		data->current_node = dp;
		*((int *)op->oprom_array) = dp->phandle;
		op->oprom_size = sizeof(int);
		err = copyout(argp, op, bufsize + sizeof(int));

		pci_dev_put(pdev);
#endif
	}

	return err;
}

static int oprompath2node(void __user *argp, struct device_node *dp, struct openpromio *op, int bufsize, DATA *data)
{
	phandle ph = 0;

	dp = of_find_node_by_path(op->oprom_array);
	if (dp)
		ph = dp->phandle;
	data->current_node = dp;
	*((int *)op->oprom_array) = ph;
	op->oprom_size = sizeof(int);

	return copyout(argp, op, bufsize + sizeof(int));
}

static int opromgetbootargs(void __user *argp, struct openpromio *op, int bufsize)
{
	char *buf = saved_command_line;
	int len = strlen(buf);

	if (len > bufsize)
		return -EINVAL;

	strcpy(op->oprom_array, buf);
	op->oprom_size = len;

	return copyout(argp, op, bufsize + sizeof(int));
}

/*
 *	SunOS and Solaris /dev/openprom ioctl calls.
 */
static long openprom_sunos_ioctl(struct file * file,
				 unsigned int cmd, unsigned long arg,
				 struct device_node *dp)
{
	DATA *data = file->private_data;
	struct openpromio *opp = NULL;
	int bufsize, error = 0;
	static int cnt;
	void __user *argp = (void __user *)arg;

	if (cmd == OPROMSETOPT)
		bufsize = getstrings(argp, &opp);
	else
		bufsize = copyin(argp, &opp);

	if (bufsize < 0)
		return bufsize;

	mutex_lock(&openprom_mutex);

	switch (cmd) {
	case OPROMGETOPT:
	case OPROMGETPROP:
		error = opromgetprop(argp, dp, opp, bufsize);
		break;

	case OPROMNXTOPT:
	case OPROMNXTPROP:
		error = opromnxtprop(argp, dp, opp, bufsize);
		break;

	case OPROMSETOPT:
	case OPROMSETOPT2:
		error = opromsetopt(dp, opp, bufsize);
		break;

	case OPROMNEXT:
	case OPROMCHILD:
	case OPROMSETCUR:
		error = opromnext(argp, cmd, dp, opp, bufsize, data);
		break;

	case OPROMPCI2NODE:
		error = oprompci2node(argp, dp, opp, bufsize, data);
		break;

	case OPROMPATH2NODE:
		error = oprompath2node(argp, dp, opp, bufsize, data);
		break;

	case OPROMGETBOOTARGS:
		error = opromgetbootargs(argp, opp, bufsize);
		break;

	case OPROMU2P:
	case OPROMGETCONS:
	case OPROMGETFBNAME:
		if (cnt++ < 10)
			printk(KERN_INFO "openprom_sunos_ioctl: unimplemented ioctl\n");
		error = -EINVAL;
		break;
	default:
		if (cnt++ < 10)
			printk(KERN_INFO "openprom_sunos_ioctl: cmd 0x%X, arg 0x%lX\n", cmd, arg);
		error = -EINVAL;
		break;
	}

	kfree(opp);
	mutex_unlock(&openprom_mutex);

	return error;
}

static struct device_node *get_node(phandle n, DATA *data)
{
	struct device_node *dp = of_find_node_by_phandle(n);

	if (dp)
		data->lastnode = dp;

	return dp;
}

/* Copy in a whole string from userspace into kernelspace. */
static char * copyin_string(char __user *user, size_t len)
{
	if ((ssize_t)len < 0 || (ssize_t)(len + 1) < 0)
		return ERR_PTR(-EINVAL);

	return memdup_user_nul(user, len);
}

/*
 *	NetBSD /dev/openprom ioctl calls.
 */
static int opiocget(void __user *argp, DATA *data)
{
	struct opiocdesc op;
	struct device_node *dp;
	char *str;
	const void *pval;
	int err, len;

	if (copy_from_user(&op, argp, sizeof(op)))
		return -EFAULT;

	dp = get_node(op.op_nodeid, data);

	str = copyin_string(op.op_name, op.op_namelen);
	if (IS_ERR(str))
		return PTR_ERR(str);

	pval = of_get_property(dp, str, &len);
	err = 0;
	if (!pval || len > op.op_buflen) {
		err = -EINVAL;
	} else {
		op.op_buflen = len;
		if (copy_to_user(argp, &op, sizeof(op)) ||
		    copy_to_user(op.op_buf, pval, len))
			err = -EFAULT;
	}
	kfree(str);

	return err;
}

static int opiocnextprop(void __user *argp, DATA *data)
{
	struct opiocdesc op;
	struct device_node *dp;
	struct property *prop;
	char *str;
	int len;

	if (copy_from_user(&op, argp, sizeof(op)))
		return -EFAULT;

	dp = get_node(op.op_nodeid, data);
	if (!dp)
		return -EINVAL;

	str = copyin_string(op.op_name, op.op_namelen);
	if (IS_ERR(str))
		return PTR_ERR(str);

	if (str[0] == '\0') {
		prop = dp->properties;
	} else {
		prop = of_find_property(dp, str, NULL);
		if (prop)
			prop = prop->next;
	}
	kfree(str);

	if (!prop)
		len = 0;
	else
		len = prop->length;

	if (len > op.op_buflen)
		len = op.op_buflen;

	if (copy_to_user(argp, &op, sizeof(op)))
		return -EFAULT;

	if (len &&
	    copy_to_user(op.op_buf, prop->value, len))
		return -EFAULT;

	return 0;
}

static int opiocset(void __user *argp, DATA *data)
{
	struct opiocdesc op;
	struct device_node *dp;
	char *str, *tmp;
	int err;

	if (copy_from_user(&op, argp, sizeof(op)))
		return -EFAULT;

	dp = get_node(op.op_nodeid, data);
	if (!dp)
		return -EINVAL;

	str = copyin_string(op.op_name, op.op_namelen);
	if (IS_ERR(str))
		return PTR_ERR(str);

	tmp = copyin_string(op.op_buf, op.op_buflen);
	if (IS_ERR(tmp)) {
		kfree(str);
		return PTR_ERR(tmp);
	}

	err = of_set_property(dp, str, tmp, op.op_buflen);

	kfree(str);
	kfree(tmp);

	return err;
}

static int opiocgetnext(unsigned int cmd, void __user *argp)
{
	struct device_node *dp;
	phandle nd;

	BUILD_BUG_ON(sizeof(phandle) != sizeof(int));

	if (copy_from_user(&nd, argp, sizeof(phandle)))
		return -EFAULT;

	if (nd == 0) {
		if (cmd != OPIOCGETNEXT)
			return -EINVAL;
		dp = of_find_node_by_path("/");
	} else {
		dp = of_find_node_by_phandle(nd);
		nd = 0;
		if (dp) {
			if (cmd == OPIOCGETNEXT)
				dp = dp->sibling;
			else
				dp = dp->child;
		}
	}
	if (dp)
		nd = dp->phandle;
	if (copy_to_user(argp, &nd, sizeof(phandle)))
		return -EFAULT;

	return 0;
}

static int openprom_bsd_ioctl(struct file * file,
			      unsigned int cmd, unsigned long arg)
{
	DATA *data = file->private_data;
	void __user *argp = (void __user *)arg;
	int err;

	mutex_lock(&openprom_mutex);
	switch (cmd) {
	case OPIOCGET:
		err = opiocget(argp, data);
		break;

	case OPIOCNEXTPROP:
		err = opiocnextprop(argp, data);
		break;

	case OPIOCSET:
		err = opiocset(argp, data);
		break;

	case OPIOCGETOPTNODE:
		BUILD_BUG_ON(sizeof(phandle) != sizeof(int));

		err = 0;
		if (copy_to_user(argp, &options_node->phandle, sizeof(phandle)))
			err = -EFAULT;
		break;

	case OPIOCGETNEXT:
	case OPIOCGETCHILD:
		err = opiocgetnext(cmd, argp);
		break;

	default:
		err = -EINVAL;
		break;
	}
	mutex_unlock(&openprom_mutex);

	return err;
}


/*
 *	Handoff control to the correct ioctl handler.
 */
static long openprom_ioctl(struct file * file,
			   unsigned int cmd, unsigned long arg)
{
	DATA *data = file->private_data;

	switch (cmd) {
	case OPROMGETOPT:
	case OPROMNXTOPT:
		if ((file->f_mode & FMODE_READ) == 0)
			return -EPERM;
		return openprom_sunos_ioctl(file, cmd, arg,
					    options_node);

	case OPROMSETOPT:
	case OPROMSETOPT2:
		if ((file->f_mode & FMODE_WRITE) == 0)
			return -EPERM;
		return openprom_sunos_ioctl(file, cmd, arg,
					    options_node);

	case OPROMNEXT:
	case OPROMCHILD:
	case OPROMGETPROP:
	case OPROMNXTPROP:
		if ((file->f_mode & FMODE_READ) == 0)
			return -EPERM;
		return openprom_sunos_ioctl(file, cmd, arg,
					    data->current_node);

	case OPROMU2P:
	case OPROMGETCONS:
	case OPROMGETFBNAME:
	case OPROMGETBOOTARGS:
	case OPROMSETCUR:
	case OPROMPCI2NODE:
	case OPROMPATH2NODE:
		if ((file->f_mode & FMODE_READ) == 0)
			return -EPERM;
		return openprom_sunos_ioctl(file, cmd, arg, NULL);

	case OPIOCGET:
	case OPIOCNEXTPROP:
	case OPIOCGETOPTNODE:
	case OPIOCGETNEXT:
	case OPIOCGETCHILD:
		if ((file->f_mode & FMODE_READ) == 0)
			return -EBADF;
		return openprom_bsd_ioctl(file,cmd,arg);

	case OPIOCSET:
		if ((file->f_mode & FMODE_WRITE) == 0)
			return -EBADF;
		return openprom_bsd_ioctl(file,cmd,arg);

	default:
		return -EINVAL;
	};
}

static long openprom_compat_ioctl(struct file *file, unsigned int cmd,
		unsigned long arg)
{
	long rval = -ENOTTY;

	/*
	 * SunOS/Solaris only, the NetBSD one's have embedded pointers in
	 * the arg which we'd need to clean up...
	 */
	switch (cmd) {
	case OPROMGETOPT:
	case OPROMSETOPT:
	case OPROMNXTOPT:
	case OPROMSETOPT2:
	case OPROMNEXT:
	case OPROMCHILD:
	case OPROMGETPROP:
	case OPROMNXTPROP:
	case OPROMU2P:
	case OPROMGETCONS:
	case OPROMGETFBNAME:
	case OPROMGETBOOTARGS:
	case OPROMSETCUR:
	case OPROMPCI2NODE:
	case OPROMPATH2NODE:
		rval = openprom_ioctl(file, cmd, arg);
		break;
	}

	return rval;
}

static int openprom_open(struct inode * inode, struct file * file)
{
	DATA *data;

	data = kmalloc(sizeof(DATA), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	mutex_lock(&openprom_mutex);
	data->current_node = of_find_node_by_path("/");
	data->lastnode = data->current_node;
	file->private_data = (void *) data;
	mutex_unlock(&openprom_mutex);

	return 0;
}

static int openprom_release(struct inode * inode, struct file * file)
{
	kfree(file->private_data);
	return 0;
}

static const struct file_operations openprom_fops = {
	.owner =	THIS_MODULE,
	.unlocked_ioctl = openprom_ioctl,
	.compat_ioctl =	openprom_compat_ioctl,
	.open =		openprom_open,
	.release =	openprom_release,
};

static struct miscdevice openprom_dev = {
	.minor		= SUN_OPENPROM_MINOR,
	.name		= "openprom",
	.fops		= &openprom_fops,
};

static int __init openprom_init(void)
{
	int err;

	err = misc_register(&openprom_dev);
	if (err)
		return err;

	options_node = of_get_child_by_name(of_find_node_by_path("/"), "options");
	if (!options_node) {
		misc_deregister(&openprom_dev);
		return -EIO;
	}

	return 0;
}

static void __exit openprom_cleanup(void)
{
	misc_deregister(&openprom_dev);
}

module_init(openprom_init);
module_exit(openprom_cleanup);
