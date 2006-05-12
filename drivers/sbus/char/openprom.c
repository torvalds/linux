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

/* This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA. 
 */

#define PROMLIB_INTERNAL

#include <linux/config.h>
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/string.h>
#include <linux/miscdevice.h>
#include <linux/smp_lock.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/openpromio.h>
#ifdef CONFIG_PCI
#include <linux/pci.h>
#include <asm/pbm.h>
#endif

/* Private data kept by the driver for each descriptor. */
typedef struct openprom_private_data
{
	int current_node;	/* Current node for SunOS ioctls. */
	int lastnode;		/* Last valid node used by BSD ioctls. */
} DATA;

/* ID of the PROM node containing all of the EEPROM options. */
static int options_node = 0;

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

	if (!(*opp_p = kmalloc(sizeof(int) + bufsize + 1, GFP_KERNEL)))
		return -ENOMEM;
	memset(*opp_p, 0, sizeof(int) + bufsize + 1);

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

	if (!(*opp_p = kmalloc(sizeof(int) + OPROMMAXPARAM + 1, GFP_KERNEL)))
		return -ENOMEM;

	memset(*opp_p, 0, sizeof(int) + OPROMMAXPARAM + 1);
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

/*
 *	SunOS and Solaris /dev/openprom ioctl calls.
 */
static int openprom_sunos_ioctl(struct inode * inode, struct file * file,
				unsigned int cmd, unsigned long arg, int node)
{
	DATA *data = (DATA *) file->private_data;
	char buffer[OPROMMAXPARAM+1], *buf;
	struct openpromio *opp;
	int bufsize, len, error = 0;
	static int cnt;
	void __user *argp = (void __user *)arg;

	if (cmd == OPROMSETOPT)
		bufsize = getstrings(argp, &opp);
	else
		bufsize = copyin(argp, &opp);

	if (bufsize < 0)
		return bufsize;

	switch (cmd) {
	case OPROMGETOPT:
	case OPROMGETPROP:
		len = prom_getproplen(node, opp->oprom_array);

		if (len <= 0 || len > bufsize) {
			error = copyout(argp, opp, sizeof(int));
			break;
		}

		len = prom_getproperty(node, opp->oprom_array, buffer, bufsize);

		memcpy(opp->oprom_array, buffer, len);
		opp->oprom_array[len] = '\0';
		opp->oprom_size = len;

		error = copyout(argp, opp, sizeof(int) + bufsize);
		break;

	case OPROMNXTOPT:
	case OPROMNXTPROP:
		buf = prom_nextprop(node, opp->oprom_array, buffer);

		len = strlen(buf);
		if (len == 0 || len + 1 > bufsize) {
			error = copyout(argp, opp, sizeof(int));
			break;
		}

		memcpy(opp->oprom_array, buf, len);
		opp->oprom_array[len] = '\0';
		opp->oprom_size = ++len;

		error = copyout(argp, opp, sizeof(int) + bufsize);
		break;

	case OPROMSETOPT:
	case OPROMSETOPT2:
		buf = opp->oprom_array + strlen(opp->oprom_array) + 1;
		len = opp->oprom_array + bufsize - buf;

		error = prom_setprop(options_node, opp->oprom_array,
				     buf, len);

		if (error < 0)
			error = -EINVAL;
		break;

	case OPROMNEXT:
	case OPROMCHILD:
	case OPROMSETCUR:
		if (bufsize < sizeof(int)) {
			error = -EINVAL;
			break;
		}

		node = *((int *) opp->oprom_array);

		switch (cmd) {
		case OPROMNEXT: node = __prom_getsibling(node); break;
		case OPROMCHILD: node = __prom_getchild(node); break;
		case OPROMSETCUR: break;
		}

		data->current_node = node;
		*((int *)opp->oprom_array) = node;
		opp->oprom_size = sizeof(int);

		error = copyout(argp, opp, bufsize + sizeof(int));
		break;

	case OPROMPCI2NODE:
		error = -EINVAL;

		if (bufsize >= 2*sizeof(int)) {
#ifdef CONFIG_PCI
			struct pci_dev *pdev;
			struct pcidev_cookie *pcp;
			pdev = pci_find_slot (((int *) opp->oprom_array)[0],
					      ((int *) opp->oprom_array)[1]);

			pcp = pdev->sysdata;
			if (pcp != NULL && pcp->prom_node != -1 && pcp->prom_node) {
				node = pcp->prom_node;
				data->current_node = node;
				*((int *)opp->oprom_array) = node;
				opp->oprom_size = sizeof(int);
				error = copyout(argp, opp, bufsize + sizeof(int));
			}
#endif
		}
		break;

	case OPROMPATH2NODE:
		node = prom_finddevice(opp->oprom_array);
		data->current_node = node;
		*((int *)opp->oprom_array) = node;
		opp->oprom_size = sizeof(int);

		error = copyout(argp, opp, bufsize + sizeof(int));
		break;

	case OPROMGETBOOTARGS:
		buf = saved_command_line;

		len = strlen(buf);

		if (len > bufsize) {
			error = -EINVAL;
			break;
		}

		strcpy(opp->oprom_array, buf);
		opp->oprom_size = len;

		error = copyout(argp, opp, bufsize + sizeof(int));
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
	return error;
}


/* Return nonzero if a specific node is in the PROM device tree. */
static int intree(int root, int node)
{
	for (; root != 0; root = prom_getsibling(root))
		if (root == node || intree(prom_getchild(root),node))
			return 1;
	return 0;
}

/* Return nonzero if a specific node is "valid". */
static int goodnode(int n, DATA *data)
{
	if (n == data->lastnode || n == prom_root_node || n == options_node)
		return 1;
	if (n == 0 || n == -1 || !intree(prom_root_node,n))
		return 0;
	data->lastnode = n;
	return 1;
}

/* Copy in a whole string from userspace into kernelspace. */
static int copyin_string(char __user *user, size_t len, char **ptr)
{
	char *tmp;

	if ((ssize_t)len < 0 || (ssize_t)(len + 1) < 0)
		return -EINVAL;

	tmp = kmalloc(len + 1, GFP_KERNEL);
	if (!tmp)
		return -ENOMEM;

	if(copy_from_user(tmp, user, len)) {
		kfree(tmp);
		return -EFAULT;
	}

	tmp[len] = '\0';

	*ptr = tmp;

	return 0;
}

/*
 *	NetBSD /dev/openprom ioctl calls.
 */
static int openprom_bsd_ioctl(struct inode * inode, struct file * file,
			      unsigned int cmd, unsigned long arg)
{
	DATA *data = (DATA *) file->private_data;
	void __user *argp = (void __user *)arg;
	struct opiocdesc op;
	int error, node, len;
	char *str, *tmp;
	char buffer[64];
	static int cnt;

	switch (cmd) {
	case OPIOCGET:
		if (copy_from_user(&op, argp, sizeof(op)))
			return -EFAULT;

		if (!goodnode(op.op_nodeid,data))
			return -EINVAL;

		error = copyin_string(op.op_name, op.op_namelen, &str);
		if (error)
			return error;

		len = prom_getproplen(op.op_nodeid,str);

		if (len > op.op_buflen) {
			kfree(str);
			return -ENOMEM;
		}

		op.op_buflen = len;

		if (len <= 0) {
			kfree(str);
			/* Verified by the above copy_from_user */
			if (__copy_to_user(argp, &op,
				       sizeof(op)))
				return -EFAULT;
			return 0;
		}

		tmp = kmalloc(len + 1, GFP_KERNEL);
		if (!tmp) {
			kfree(str);
			return -ENOMEM;
		}

		cnt = prom_getproperty(op.op_nodeid, str, tmp, len);
		if (cnt <= 0) {
			error = -EINVAL;
		} else {
			tmp[len] = '\0';

			if (__copy_to_user(argp, &op, sizeof(op)) != 0 ||
			    copy_to_user(op.op_buf, tmp, len) != 0)
				error = -EFAULT;
		}

		kfree(tmp);
		kfree(str);

		return error;

	case OPIOCNEXTPROP:
		if (copy_from_user(&op, argp, sizeof(op)))
			return -EFAULT;

		if (!goodnode(op.op_nodeid,data))
			return -EINVAL;

		error = copyin_string(op.op_name, op.op_namelen, &str);
		if (error)
			return error;

		tmp = prom_nextprop(op.op_nodeid,str,buffer);

		if (tmp) {
			len = strlen(tmp);
			if (len > op.op_buflen)
				len = op.op_buflen;
			else
				op.op_buflen = len;
		} else {
			len = op.op_buflen = 0;
		}

		if (!access_ok(VERIFY_WRITE, argp, sizeof(op))) {
			kfree(str);
			return -EFAULT;
		}

		if (!access_ok(VERIFY_WRITE, op.op_buf, len)) {
			kfree(str);
			return -EFAULT;
		}

		error = __copy_to_user(argp, &op, sizeof(op));
		if (!error) error = __copy_to_user(op.op_buf, tmp, len);

		kfree(str);

		return error;

	case OPIOCSET:
		if (copy_from_user(&op, argp, sizeof(op)))
			return -EFAULT;

		if (!goodnode(op.op_nodeid,data))
			return -EINVAL;

		error = copyin_string(op.op_name, op.op_namelen, &str);
		if (error)
			return error;

		error = copyin_string(op.op_buf, op.op_buflen, &tmp);
		if (error) {
			kfree(str);
			return error;
		}

		len = prom_setprop(op.op_nodeid,str,tmp,op.op_buflen+1);

		if (len != op.op_buflen)
			return -EINVAL;

		kfree(str);
		kfree(tmp);

		return 0;

	case OPIOCGETOPTNODE:
		if (copy_to_user(argp, &options_node, sizeof(int)))
			return -EFAULT;
		return 0;

	case OPIOCGETNEXT:
	case OPIOCGETCHILD:
		if (copy_from_user(&node, argp, sizeof(int)))
			return -EFAULT;

		if (cmd == OPIOCGETNEXT)
			node = __prom_getsibling(node);
		else
			node = __prom_getchild(node);

		if (__copy_to_user(argp, &node, sizeof(int)))
			return -EFAULT;

		return 0;

	default:
		if (cnt++ < 10)
			printk(KERN_INFO "openprom_bsd_ioctl: cmd 0x%X\n", cmd);
		return -EINVAL;

	}
}


/*
 *	Handoff control to the correct ioctl handler.
 */
static int openprom_ioctl(struct inode * inode, struct file * file,
			  unsigned int cmd, unsigned long arg)
{
	DATA *data = (DATA *) file->private_data;
	static int cnt;

	switch (cmd) {
	case OPROMGETOPT:
	case OPROMNXTOPT:
		if ((file->f_mode & FMODE_READ) == 0)
			return -EPERM;
		return openprom_sunos_ioctl(inode, file, cmd, arg,
					    options_node);

	case OPROMSETOPT:
	case OPROMSETOPT2:
		if ((file->f_mode & FMODE_WRITE) == 0)
			return -EPERM;
		return openprom_sunos_ioctl(inode, file, cmd, arg,
					    options_node);

	case OPROMNEXT:
	case OPROMCHILD:
	case OPROMGETPROP:
	case OPROMNXTPROP:
		if ((file->f_mode & FMODE_READ) == 0)
			return -EPERM;
		return openprom_sunos_ioctl(inode, file, cmd, arg,
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
		return openprom_sunos_ioctl(inode, file, cmd, arg, 0);

	case OPIOCGET:
	case OPIOCNEXTPROP:
	case OPIOCGETOPTNODE:
	case OPIOCGETNEXT:
	case OPIOCGETCHILD:
		if ((file->f_mode & FMODE_READ) == 0)
			return -EBADF;
		return openprom_bsd_ioctl(inode,file,cmd,arg);

	case OPIOCSET:
		if ((file->f_mode & FMODE_WRITE) == 0)
			return -EBADF;
		return openprom_bsd_ioctl(inode,file,cmd,arg);

	default:
		if (cnt++ < 10)
			printk("openprom_ioctl: cmd 0x%X, arg 0x%lX\n", cmd, arg);
		return -EINVAL;
	}
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
		lock_kernel();
		rval = openprom_ioctl(file->f_dentry->d_inode, file, cmd, arg);
		lock_kernel();
		break;
	}

	return rval;
}

static int openprom_open(struct inode * inode, struct file * file)
{
	DATA *data;

	data = (DATA *) kmalloc(sizeof(DATA), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->current_node = prom_root_node;
	data->lastnode = prom_root_node;
	file->private_data = (void *)data;

	return 0;
}

static int openprom_release(struct inode * inode, struct file * file)
{
	kfree(file->private_data);
	return 0;
}

static struct file_operations openprom_fops = {
	.owner =	THIS_MODULE,
	.llseek =	no_llseek,
	.ioctl =	openprom_ioctl,
	.compat_ioctl =	openprom_compat_ioctl,
	.open =		openprom_open,
	.release =	openprom_release,
};

static struct miscdevice openprom_dev = {
	SUN_OPENPROM_MINOR, "openprom", &openprom_fops
};

static int __init openprom_init(void)
{
	int error;

	error = misc_register(&openprom_dev);
	if (error) {
		printk(KERN_ERR "openprom: unable to get misc minor\n");
		return error;
	}

	options_node = prom_getchild(prom_root_node);
	options_node = prom_searchsiblings(options_node,"options");

	if (options_node == 0 || options_node == -1) {
		printk(KERN_ERR "openprom: unable to find options node\n");
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
MODULE_LICENSE("GPL");
