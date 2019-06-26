// SPDX-License-Identifier: GPL-2.0+
#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>   /* printk() */
#include <linux/slab.h>     /* kmalloc() */
#include <linux/fs.h>       /* everything... */
#include <linux/errno.h>    /* error codes */
#include <linux/types.h>    /* size_t */
#include <linux/cdev.h>
#include <linux/uaccess.h>    /* copy_*_user */
#include <linux/rwsem.h>
#include <linux/idr.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <linux/device.h>
#include <linux/sched.h>
#include "pcie.h"
#include "uapi.h"

int  kp2000_cdev_open(struct inode *inode, struct file *filp)
{
	struct kp2000_device *pcard = container_of(filp->private_data, struct kp2000_device, miscdev);

	dev_dbg(&pcard->pdev->dev, "kp2000_cdev_open(filp = [%p], pcard = [%p])\n", filp, pcard);

	filp->private_data = pcard; /* so other methods can access it */

	return 0;
}

int  kp2000_cdev_close(struct inode *inode, struct file *filp)
{
	struct kp2000_device *pcard = filp->private_data;

	dev_dbg(&pcard->pdev->dev, "kp2000_cdev_close(filp = [%p], pcard = [%p])\n", filp, pcard);
	return 0;
}


ssize_t  kp2000_cdev_read(struct file *filp, char __user *buf, size_t count, loff_t *f_pos)
{
	struct kp2000_device *pcard = filp->private_data;
	int cnt = 0;
	int ret;
#define BUFF_CNT  1024
	char buff[BUFF_CNT] = {0}; //NOTE: Increase this so it is at least as large as all the scnprintfs.  And don't use unbounded strings. "%s"
	//NOTE: also, this is a really shitty way to implement the read() call, but it will work for any size 'count'.

	if (WARN(NULL == buf, "kp2000_cdev_read: buf is a NULL pointer!\n"))
		return -EINVAL;

	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "Card ID                 : 0x%08x\n", pcard->card_id);
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "Build Version           : 0x%08x\n", pcard->build_version);
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "Build Date              : 0x%08x\n", pcard->build_datestamp);
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "Build Time              : 0x%08x\n", pcard->build_timestamp);
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "Core Table Offset       : 0x%08x\n", pcard->core_table_offset);
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "Core Table Length       : 0x%08x\n", pcard->core_table_length);
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "Hardware Revision       : 0x%08x\n", pcard->hardware_revision);
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "SSID                    : 0x%016llx\n", pcard->ssid);
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "DDNA                    : 0x%016llx\n", pcard->ddna);
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "IRQ Mask                : 0x%016llx\n", readq(pcard->sysinfo_regs_base + REG_INTERRUPT_MASK));
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "IRQ Active              : 0x%016llx\n", readq(pcard->sysinfo_regs_base + REG_INTERRUPT_ACTIVE));
	cnt += scnprintf(buff+cnt, BUFF_CNT-cnt, "CPLD                    : 0x%016llx\n", readq(pcard->sysinfo_regs_base + REG_CPLD_CONFIG));

	if (*f_pos >= cnt)
		return 0;

	if (count > cnt)
		count = cnt;

	ret = copy_to_user(buf, buff + *f_pos, count);
	if (ret)
		return -EFAULT;
	*f_pos += count;
	return count;
}

ssize_t  kp2000_cdev_write(struct file *filp, const char __user *buf, size_t count, loff_t *f_pos)
{
	return -EINVAL;
}

long  kp2000_cdev_ioctl(struct file *filp, unsigned int ioctl_num, unsigned long ioctl_param)
{
	struct kp2000_device *pcard = filp->private_data;

	dev_dbg(&pcard->pdev->dev, "kp2000_cdev_ioctl(filp = [%p], ioctl_num = 0x%08x, ioctl_param = 0x%016lx) pcard = [%p]\n", filp, ioctl_num, ioctl_param, pcard);

	switch (ioctl_num){
	case KP2000_IOCTL_GET_CPLD_REG:             return readq(pcard->sysinfo_regs_base + REG_CPLD_CONFIG);
	case KP2000_IOCTL_GET_PCIE_ERROR_REG:       return readq(pcard->sysinfo_regs_base + REG_PCIE_ERROR_COUNT);
    
	case KP2000_IOCTL_GET_EVERYTHING: {
		struct kp2000_regs temp;
		int ret;

		memset(&temp, 0, sizeof(temp));
		temp.card_id = pcard->card_id;
		temp.build_version = pcard->build_version;
		temp.build_datestamp = pcard->build_datestamp;
		temp.build_timestamp = pcard->build_timestamp;
		temp.hw_rev = pcard->hardware_revision;
		temp.ssid = pcard->ssid;
		temp.ddna = pcard->ddna;
		temp.cpld_reg = readq(pcard->sysinfo_regs_base + REG_CPLD_CONFIG);

		ret = copy_to_user((void*)ioctl_param, (void*)&temp, sizeof(temp));
		if (ret)
			return -EFAULT;

		return sizeof(temp);
		}

	default:
		return -ENOTTY;
	}
	return -ENOTTY;
}


struct file_operations  kp2000_fops = {
	.owner      = THIS_MODULE,
	.open       = kp2000_cdev_open,
	.release    = kp2000_cdev_close,
	.read       = kp2000_cdev_read,
	//.write      = kp2000_cdev_write,
	//.poll       = kp2000_cdev_poll,
	//.fasync     = kp2000_cdev_fasync,
	.llseek     = noop_llseek,
	.unlocked_ioctl = kp2000_cdev_ioctl,
};

