/*
 *	Sky CPU State Driver
 *
 *	Copyright (C) 2002 Brian Waite
 *
 *	This driver allows use of the CPU state bits
 *	It exports the /dev/sky_cpustate and also
 *	/proc/sky_cpustate pseudo-file for status information.
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/spinlock.h>
#include <linux/miscdevice.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/platform_device.h>
#include <asm/uaccess.h>
#include <linux/hdpu_features.h>

#define SKY_CPUSTATE_VERSION		"1.1"

static int hdpu_cpustate_probe(struct platform_device *pdev);
static int hdpu_cpustate_remove(struct platform_device *pdev);

struct cpustate_t cpustate;

static int cpustate_get_ref(int excl)
{

	int retval = -EBUSY;

	spin_lock(&cpustate.lock);

	if (cpustate.excl)
		goto out_busy;

	if (excl) {
		if (cpustate.open_count)
			goto out_busy;
		cpustate.excl = 1;
	}

	cpustate.open_count++;
	retval = 0;

      out_busy:
	spin_unlock(&cpustate.lock);
	return retval;
}

static int cpustate_free_ref(void)
{

	spin_lock(&cpustate.lock);

	cpustate.excl = 0;
	cpustate.open_count--;

	spin_unlock(&cpustate.lock);
	return 0;
}

unsigned char cpustate_get_state(void)
{

	return cpustate.cached_val;
}

void cpustate_set_state(unsigned char new_state)
{
	unsigned int state = (new_state << 21);

#ifdef DEBUG_CPUSTATE
	printk("CPUSTATE -> 0x%x\n", new_state);
#endif
	spin_lock(&cpustate.lock);
	cpustate.cached_val = new_state;
	writel((0xff << 21), cpustate.clr_addr);
	writel(state, cpustate.set_addr);
	spin_unlock(&cpustate.lock);
}

/*
 *	Now all the various file operations that we export.
 */

static ssize_t cpustate_read(struct file *file, char *buf,
			     size_t count, loff_t * ppos)
{
	unsigned char data;

	if (count < 0)
		return -EFAULT;
	if (count == 0)
		return 0;

	data = cpustate_get_state();
	if (copy_to_user(buf, &data, sizeof(unsigned char)))
		return -EFAULT;
	return sizeof(unsigned char);
}

static ssize_t cpustate_write(struct file *file, const char *buf,
			      size_t count, loff_t * ppos)
{
	unsigned char data;

	if (count < 0)
		return -EFAULT;

	if (count == 0)
		return 0;

	if (copy_from_user((unsigned char *)&data, buf, sizeof(unsigned char)))
		return -EFAULT;

	cpustate_set_state(data);
	return sizeof(unsigned char);
}

static int cpustate_open(struct inode *inode, struct file *file)
{
	return cpustate_get_ref((file->f_flags & O_EXCL));
}

static int cpustate_release(struct inode *inode, struct file *file)
{
	return cpustate_free_ref();
}

/*
 *	Info exported via "/proc/sky_cpustate".
 */
static int cpustate_read_proc(char *page, char **start, off_t off,
			      int count, int *eof, void *data)
{
	char *p = page;
	int len = 0;

	p += sprintf(p, "CPU State: %04x\n", cpustate_get_state());
	len = p - page;

	if (len <= off + count)
		*eof = 1;
	*start = page + off;
	len -= off;
	if (len > count)
		len = count;
	if (len < 0)
		len = 0;
	return len;
}

static struct platform_driver hdpu_cpustate_driver = {
	.probe = hdpu_cpustate_probe,
	.remove = hdpu_cpustate_remove,
	.driver = {
		.name = HDPU_CPUSTATE_NAME,
	},
};

/*
 *	The various file operations we support.
 */
static struct file_operations cpustate_fops = {
      owner:THIS_MODULE,
      open:cpustate_open,
      release:cpustate_release,
      read:cpustate_read,
      write:cpustate_write,
      fasync:NULL,
      poll:NULL,
      ioctl:NULL,
      llseek:no_llseek,

};

static struct miscdevice cpustate_dev = {
	MISC_DYNAMIC_MINOR,
	"sky_cpustate",
	&cpustate_fops
};

static int hdpu_cpustate_probe(struct platform_device *pdev)
{
	struct resource *res;
	struct proc_dir_entry *proc_de;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	cpustate.set_addr = (unsigned long *)res->start;
	cpustate.clr_addr = (unsigned long *)res->end - 1;

	ret = misc_register(&cpustate_dev);
	if (ret) {
		printk(KERN_WARNING "sky_cpustate: Unable to register misc "
					"device.\n");
		cpustate.set_addr = NULL;
		cpustate.clr_addr = NULL;
		return ret;
	}

	proc_de = create_proc_read_entry("sky_cpustate", 0, 0,
					cpustate_read_proc, NULL);
	if (proc_de == NULL)
		printk(KERN_WARNING "sky_cpustate: Unable to create proc "
					"dir entry\n");

	printk(KERN_INFO "Sky CPU State Driver v" SKY_CPUSTATE_VERSION "\n");
	return 0;
}

static int hdpu_cpustate_remove(struct platform_device *pdev)
{

	cpustate.set_addr = NULL;
	cpustate.clr_addr = NULL;

	remove_proc_entry("sky_cpustate", NULL);
	misc_deregister(&cpustate_dev);
	return 0;

}

static int __init cpustate_init(void)
{
	int rc;
	rc = platform_driver_register(&hdpu_cpustate_driver);
	return rc;
}

static void __exit cpustate_exit(void)
{
	platform_driver_unregister(&hdpu_cpustate_driver);
}

module_init(cpustate_init);
module_exit(cpustate_exit);

MODULE_AUTHOR("Brian Waite");
MODULE_LICENSE("GPL");
