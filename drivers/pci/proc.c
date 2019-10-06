// SPDX-License-Identifier: GPL-2.0
/*
 * Procfs interface for the PCI bus
 *
 * Copyright (c) 1997--1999 Martin Mares <mj@ucw.cz>
 */

#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <linux/capability.h>
#include <linux/uaccess.h>
#include <linux/security.h>
#include <asm/byteorder.h>
#include "pci.h"

static int proc_initialized;	/* = 0 */

static loff_t proc_bus_pci_lseek(struct file *file, loff_t off, int whence)
{
	struct pci_dev *dev = PDE_DATA(file_inode(file));
	return fixed_size_llseek(file, off, whence, dev->cfg_size);
}

static ssize_t proc_bus_pci_read(struct file *file, char __user *buf,
				 size_t nbytes, loff_t *ppos)
{
	struct pci_dev *dev = PDE_DATA(file_inode(file));
	unsigned int pos = *ppos;
	unsigned int cnt, size;

	/*
	 * Normal users can read only the standardized portion of the
	 * configuration space as several chips lock up when trying to read
	 * undefined locations (think of Intel PIIX4 as a typical example).
	 */

	if (capable(CAP_SYS_ADMIN))
		size = dev->cfg_size;
	else if (dev->hdr_type == PCI_HEADER_TYPE_CARDBUS)
		size = 128;
	else
		size = 64;

	if (pos >= size)
		return 0;
	if (nbytes >= size)
		nbytes = size;
	if (pos + nbytes > size)
		nbytes = size - pos;
	cnt = nbytes;

	if (!access_ok(buf, cnt))
		return -EINVAL;

	pci_config_pm_runtime_get(dev);

	if ((pos & 1) && cnt) {
		unsigned char val;
		pci_user_read_config_byte(dev, pos, &val);
		__put_user(val, buf);
		buf++;
		pos++;
		cnt--;
	}

	if ((pos & 3) && cnt > 2) {
		unsigned short val;
		pci_user_read_config_word(dev, pos, &val);
		__put_user(cpu_to_le16(val), (__le16 __user *) buf);
		buf += 2;
		pos += 2;
		cnt -= 2;
	}

	while (cnt >= 4) {
		unsigned int val;
		pci_user_read_config_dword(dev, pos, &val);
		__put_user(cpu_to_le32(val), (__le32 __user *) buf);
		buf += 4;
		pos += 4;
		cnt -= 4;
	}

	if (cnt >= 2) {
		unsigned short val;
		pci_user_read_config_word(dev, pos, &val);
		__put_user(cpu_to_le16(val), (__le16 __user *) buf);
		buf += 2;
		pos += 2;
		cnt -= 2;
	}

	if (cnt) {
		unsigned char val;
		pci_user_read_config_byte(dev, pos, &val);
		__put_user(val, buf);
		buf++;
		pos++;
		cnt--;
	}

	pci_config_pm_runtime_put(dev);

	*ppos = pos;
	return nbytes;
}

static ssize_t proc_bus_pci_write(struct file *file, const char __user *buf,
				  size_t nbytes, loff_t *ppos)
{
	struct inode *ino = file_inode(file);
	struct pci_dev *dev = PDE_DATA(ino);
	int pos = *ppos;
	int size = dev->cfg_size;
	int cnt, ret;

	ret = security_locked_down(LOCKDOWN_PCI_ACCESS);
	if (ret)
		return ret;

	if (pos >= size)
		return 0;
	if (nbytes >= size)
		nbytes = size;
	if (pos + nbytes > size)
		nbytes = size - pos;
	cnt = nbytes;

	if (!access_ok(buf, cnt))
		return -EINVAL;

	pci_config_pm_runtime_get(dev);

	if ((pos & 1) && cnt) {
		unsigned char val;
		__get_user(val, buf);
		pci_user_write_config_byte(dev, pos, val);
		buf++;
		pos++;
		cnt--;
	}

	if ((pos & 3) && cnt > 2) {
		__le16 val;
		__get_user(val, (__le16 __user *) buf);
		pci_user_write_config_word(dev, pos, le16_to_cpu(val));
		buf += 2;
		pos += 2;
		cnt -= 2;
	}

	while (cnt >= 4) {
		__le32 val;
		__get_user(val, (__le32 __user *) buf);
		pci_user_write_config_dword(dev, pos, le32_to_cpu(val));
		buf += 4;
		pos += 4;
		cnt -= 4;
	}

	if (cnt >= 2) {
		__le16 val;
		__get_user(val, (__le16 __user *) buf);
		pci_user_write_config_word(dev, pos, le16_to_cpu(val));
		buf += 2;
		pos += 2;
		cnt -= 2;
	}

	if (cnt) {
		unsigned char val;
		__get_user(val, buf);
		pci_user_write_config_byte(dev, pos, val);
		buf++;
		pos++;
		cnt--;
	}

	pci_config_pm_runtime_put(dev);

	*ppos = pos;
	i_size_write(ino, dev->cfg_size);
	return nbytes;
}

struct pci_filp_private {
	enum pci_mmap_state mmap_state;
	int write_combine;
};

static long proc_bus_pci_ioctl(struct file *file, unsigned int cmd,
			       unsigned long arg)
{
	struct pci_dev *dev = PDE_DATA(file_inode(file));
#ifdef HAVE_PCI_MMAP
	struct pci_filp_private *fpriv = file->private_data;
#endif /* HAVE_PCI_MMAP */
	int ret = 0;

	ret = security_locked_down(LOCKDOWN_PCI_ACCESS);
	if (ret)
		return ret;

	switch (cmd) {
	case PCIIOC_CONTROLLER:
		ret = pci_domain_nr(dev->bus);
		break;

#ifdef HAVE_PCI_MMAP
	case PCIIOC_MMAP_IS_IO:
		if (!arch_can_pci_mmap_io())
			return -EINVAL;
		fpriv->mmap_state = pci_mmap_io;
		break;

	case PCIIOC_MMAP_IS_MEM:
		fpriv->mmap_state = pci_mmap_mem;
		break;

	case PCIIOC_WRITE_COMBINE:
		if (arch_can_pci_mmap_wc()) {
			if (arg)
				fpriv->write_combine = 1;
			else
				fpriv->write_combine = 0;
			break;
		}
		/* If arch decided it can't, fall through... */
#endif /* HAVE_PCI_MMAP */
		/* fall through */
	default:
		ret = -EINVAL;
		break;
	}

	return ret;
}

#ifdef HAVE_PCI_MMAP
static int proc_bus_pci_mmap(struct file *file, struct vm_area_struct *vma)
{
	struct pci_dev *dev = PDE_DATA(file_inode(file));
	struct pci_filp_private *fpriv = file->private_data;
	int i, ret, write_combine = 0, res_bit = IORESOURCE_MEM;

	if (!capable(CAP_SYS_RAWIO) ||
	    security_locked_down(LOCKDOWN_PCI_ACCESS))
		return -EPERM;

	if (fpriv->mmap_state == pci_mmap_io) {
		if (!arch_can_pci_mmap_io())
			return -EINVAL;
		res_bit = IORESOURCE_IO;
	}

	/* Make sure the caller is mapping a real resource for this device */
	for (i = 0; i < PCI_ROM_RESOURCE; i++) {
		if (dev->resource[i].flags & res_bit &&
		    pci_mmap_fits(dev, i, vma,  PCI_MMAP_PROCFS))
			break;
	}

	if (i >= PCI_ROM_RESOURCE)
		return -ENODEV;

	if (fpriv->mmap_state == pci_mmap_mem &&
	    fpriv->write_combine) {
		if (dev->resource[i].flags & IORESOURCE_PREFETCH)
			write_combine = 1;
		else
			return -EINVAL;
	}
	ret = pci_mmap_page_range(dev, i, vma,
				  fpriv->mmap_state, write_combine);
	if (ret < 0)
		return ret;

	return 0;
}

static int proc_bus_pci_open(struct inode *inode, struct file *file)
{
	struct pci_filp_private *fpriv = kmalloc(sizeof(*fpriv), GFP_KERNEL);

	if (!fpriv)
		return -ENOMEM;

	fpriv->mmap_state = pci_mmap_io;
	fpriv->write_combine = 0;

	file->private_data = fpriv;

	return 0;
}

static int proc_bus_pci_release(struct inode *inode, struct file *file)
{
	kfree(file->private_data);
	file->private_data = NULL;

	return 0;
}
#endif /* HAVE_PCI_MMAP */

static const struct file_operations proc_bus_pci_operations = {
	.owner		= THIS_MODULE,
	.llseek		= proc_bus_pci_lseek,
	.read		= proc_bus_pci_read,
	.write		= proc_bus_pci_write,
	.unlocked_ioctl	= proc_bus_pci_ioctl,
	.compat_ioctl	= proc_bus_pci_ioctl,
#ifdef HAVE_PCI_MMAP
	.open		= proc_bus_pci_open,
	.release	= proc_bus_pci_release,
	.mmap		= proc_bus_pci_mmap,
#ifdef HAVE_ARCH_PCI_GET_UNMAPPED_AREA
	.get_unmapped_area = get_pci_unmapped_area,
#endif /* HAVE_ARCH_PCI_GET_UNMAPPED_AREA */
#endif /* HAVE_PCI_MMAP */
};

/* iterator */
static void *pci_seq_start(struct seq_file *m, loff_t *pos)
{
	struct pci_dev *dev = NULL;
	loff_t n = *pos;

	for_each_pci_dev(dev) {
		if (!n--)
			break;
	}
	return dev;
}

static void *pci_seq_next(struct seq_file *m, void *v, loff_t *pos)
{
	struct pci_dev *dev = v;

	(*pos)++;
	dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev);
	return dev;
}

static void pci_seq_stop(struct seq_file *m, void *v)
{
	if (v) {
		struct pci_dev *dev = v;
		pci_dev_put(dev);
	}
}

static int show_device(struct seq_file *m, void *v)
{
	const struct pci_dev *dev = v;
	const struct pci_driver *drv;
	int i;

	if (dev == NULL)
		return 0;

	drv = pci_dev_driver(dev);
	seq_printf(m, "%02x%02x\t%04x%04x\t%x",
			dev->bus->number,
			dev->devfn,
			dev->vendor,
			dev->device,
			dev->irq);

	/* only print standard and ROM resources to preserve compatibility */
	for (i = 0; i <= PCI_ROM_RESOURCE; i++) {
		resource_size_t start, end;
		pci_resource_to_user(dev, i, &dev->resource[i], &start, &end);
		seq_printf(m, "\t%16llx",
			(unsigned long long)(start |
			(dev->resource[i].flags & PCI_REGION_FLAG_MASK)));
	}
	for (i = 0; i <= PCI_ROM_RESOURCE; i++) {
		resource_size_t start, end;
		pci_resource_to_user(dev, i, &dev->resource[i], &start, &end);
		seq_printf(m, "\t%16llx",
			dev->resource[i].start < dev->resource[i].end ?
			(unsigned long long)(end - start) + 1 : 0);
	}
	seq_putc(m, '\t');
	if (drv)
		seq_puts(m, drv->name);
	seq_putc(m, '\n');
	return 0;
}

static const struct seq_operations proc_bus_pci_devices_op = {
	.start	= pci_seq_start,
	.next	= pci_seq_next,
	.stop	= pci_seq_stop,
	.show	= show_device
};

static struct proc_dir_entry *proc_bus_pci_dir;

int pci_proc_attach_device(struct pci_dev *dev)
{
	struct pci_bus *bus = dev->bus;
	struct proc_dir_entry *e;
	char name[16];

	if (!proc_initialized)
		return -EACCES;

	if (!bus->procdir) {
		if (pci_proc_domain(bus)) {
			sprintf(name, "%04x:%02x", pci_domain_nr(bus),
					bus->number);
		} else {
			sprintf(name, "%02x", bus->number);
		}
		bus->procdir = proc_mkdir(name, proc_bus_pci_dir);
		if (!bus->procdir)
			return -ENOMEM;
	}

	sprintf(name, "%02x.%x", PCI_SLOT(dev->devfn), PCI_FUNC(dev->devfn));
	e = proc_create_data(name, S_IFREG | S_IRUGO | S_IWUSR, bus->procdir,
			     &proc_bus_pci_operations, dev);
	if (!e)
		return -ENOMEM;
	proc_set_size(e, dev->cfg_size);
	dev->procent = e;

	return 0;
}

int pci_proc_detach_device(struct pci_dev *dev)
{
	proc_remove(dev->procent);
	dev->procent = NULL;
	return 0;
}

int pci_proc_detach_bus(struct pci_bus *bus)
{
	proc_remove(bus->procdir);
	return 0;
}

static int __init pci_proc_init(void)
{
	struct pci_dev *dev = NULL;
	proc_bus_pci_dir = proc_mkdir("bus/pci", NULL);
	proc_create_seq("devices", 0, proc_bus_pci_dir,
		    &proc_bus_pci_devices_op);
	proc_initialized = 1;
	for_each_pci_dev(dev)
		pci_proc_attach_device(dev);

	return 0;
}
device_initcall(pci_proc_init);
