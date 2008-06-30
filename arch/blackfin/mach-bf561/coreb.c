/*
 * File:         arch/blackfin/mach-bf561/coreb.c
 * Based on:
 * Author:
 *
 * Created:
 * Description:  Handle CoreB on a BF561
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/mm.h>
#include <linux/miscdevice.h>
#include <linux/device.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/uaccess.h>
#include <linux/fs.h>
#include <asm/dma.h>
#include <asm/cacheflush.h>

#define MODULE_VER		"v0.1"

static spinlock_t coreb_lock;
static wait_queue_head_t coreb_dma_wait;

#define COREB_IS_OPEN		0x00000001
#define COREB_IS_RUNNING	0x00000010

#define CMD_COREB_INDEX		1
#define CMD_COREB_START		2
#define CMD_COREB_STOP		3
#define CMD_COREB_RESET		4

#define COREB_MINOR		229

static unsigned long coreb_status = 0;
static unsigned long coreb_base = 0xff600000;
static unsigned long coreb_size = 0x4000;
int coreb_dma_done;

static loff_t coreb_lseek(struct file *file, loff_t offset, int origin);
static ssize_t coreb_read(struct file *file, char *buf, size_t count,
			  loff_t * ppos);
static ssize_t coreb_write(struct file *file, const char *buf, size_t count,
			   loff_t * ppos);
static int coreb_ioctl(struct inode *inode, struct file *file, unsigned int cmd,
		       unsigned long arg);
static int coreb_open(struct inode *inode, struct file *file);
static int coreb_release(struct inode *inode, struct file *file);

static irqreturn_t coreb_dma_interrupt(int irq, void *dev_id)
{
	clear_dma_irqstat(CH_MEM_STREAM2_DEST);
	coreb_dma_done = 1;
	wake_up_interruptible(&coreb_dma_wait);
	return IRQ_HANDLED;
}

static ssize_t coreb_write(struct file *file, const char *buf, size_t count,
			   loff_t * ppos)
{
	unsigned long p = *ppos;
	ssize_t wrote = 0;

	if (p + count > coreb_size)
		return -EFAULT;

	while (count > 0) {
		int len = count;

		if (len > PAGE_SIZE)
			len = PAGE_SIZE;

		coreb_dma_done = 0;

		flush_dcache_range((unsigned long)buf, (unsigned long)(buf+len));
		/* Source Channel */
		set_dma_start_addr(CH_MEM_STREAM2_SRC, (unsigned long)buf);
		set_dma_x_count(CH_MEM_STREAM2_SRC, len);
		set_dma_x_modify(CH_MEM_STREAM2_SRC, sizeof(char));
		set_dma_config(CH_MEM_STREAM2_SRC, 0);
		/* Destination Channel */
		set_dma_start_addr(CH_MEM_STREAM2_DEST, coreb_base + p);
		set_dma_x_count(CH_MEM_STREAM2_DEST, len);
		set_dma_x_modify(CH_MEM_STREAM2_DEST, sizeof(char));
		set_dma_config(CH_MEM_STREAM2_DEST, WNR | RESTART | DI_EN);

		enable_dma(CH_MEM_STREAM2_SRC);
		enable_dma(CH_MEM_STREAM2_DEST);

		wait_event_interruptible(coreb_dma_wait, coreb_dma_done);

		disable_dma(CH_MEM_STREAM2_SRC);
		disable_dma(CH_MEM_STREAM2_DEST);

		count -= len;
		wrote += len;
		buf += len;
		p += len;
	}
	*ppos = p;
	return wrote;
}

static ssize_t coreb_read(struct file *file, char *buf, size_t count,
			  loff_t * ppos)
{
	unsigned long p = *ppos;
	ssize_t read = 0;

	if ((p + count) > coreb_size)
		return -EFAULT;

	while (count > 0) {
		int len = count;

		if (len > PAGE_SIZE)
			len = PAGE_SIZE;

		coreb_dma_done = 0;

		invalidate_dcache_range((unsigned long)buf, (unsigned long)(buf+len));
		/* Source Channel */
		set_dma_start_addr(CH_MEM_STREAM2_SRC, coreb_base + p);
		set_dma_x_count(CH_MEM_STREAM2_SRC, len);
		set_dma_x_modify(CH_MEM_STREAM2_SRC, sizeof(char));
		set_dma_config(CH_MEM_STREAM2_SRC, 0);
		/* Destination Channel */
		set_dma_start_addr(CH_MEM_STREAM2_DEST, (unsigned long)buf);
		set_dma_x_count(CH_MEM_STREAM2_DEST, len);
		set_dma_x_modify(CH_MEM_STREAM2_DEST, sizeof(char));
		set_dma_config(CH_MEM_STREAM2_DEST, WNR | RESTART | DI_EN);

		enable_dma(CH_MEM_STREAM2_SRC);
		enable_dma(CH_MEM_STREAM2_DEST);

		wait_event_interruptible(coreb_dma_wait, coreb_dma_done);

		disable_dma(CH_MEM_STREAM2_SRC);
		disable_dma(CH_MEM_STREAM2_DEST);

		count -= len;
		read += len;
		buf += len;
		p += len;
	}

	return read;
}

static loff_t coreb_lseek(struct file *file, loff_t offset, int origin)
{
	loff_t ret;

	mutex_lock(&file->f_dentry->d_inode->i_mutex);

	switch (origin) {
	case 0 /* SEEK_SET */ :
		if (offset < coreb_size) {
			file->f_pos = offset;
			ret = file->f_pos;
		} else
			ret = -EINVAL;
		break;
	case 1 /* SEEK_CUR */ :
		if ((offset + file->f_pos) < coreb_size) {
			file->f_pos += offset;
			ret = file->f_pos;
		} else
			ret = -EINVAL;
	default:
		ret = -EINVAL;
	}
	mutex_unlock(&file->f_dentry->d_inode->i_mutex);
	return ret;
}

/* No BKL needed here */
static int coreb_open(struct inode *inode, struct file *file)
{
	spin_lock_irq(&coreb_lock);

	if (coreb_status & COREB_IS_OPEN)
		goto out_busy;

	coreb_status |= COREB_IS_OPEN;

	spin_unlock_irq(&coreb_lock);
	return 0;

 out_busy:
	spin_unlock_irq(&coreb_lock);
	return -EBUSY;
}

static int coreb_release(struct inode *inode, struct file *file)
{
	spin_lock_irq(&coreb_lock);
	coreb_status &= ~COREB_IS_OPEN;
	spin_unlock_irq(&coreb_lock);
	return 0;
}

static int coreb_ioctl(struct inode *inode, struct file *file,
		       unsigned int cmd, unsigned long arg)
{
	int retval = 0;
	int coreb_index = 0;

	switch (cmd) {
	case CMD_COREB_INDEX:
		if (copy_from_user(&coreb_index, (int *)arg, sizeof(int))) {
			retval = -EFAULT;
			break;
		}

		spin_lock_irq(&coreb_lock);
		switch (coreb_index) {
		case 0:
			coreb_base = 0xff600000;
			coreb_size = 0x4000;
			break;
		case 1:
			coreb_base = 0xff610000;
			coreb_size = 0x4000;
			break;
		case 2:
			coreb_base = 0xff500000;
			coreb_size = 0x8000;
			break;
		case 3:
			coreb_base = 0xff400000;
			coreb_size = 0x8000;
			break;
		default:
			retval = -EINVAL;
			break;
		}
		spin_unlock_irq(&coreb_lock);

		mutex_lock(&file->f_dentry->d_inode->i_mutex);
		file->f_pos = 0;
		mutex_unlock(&file->f_dentry->d_inode->i_mutex);
		break;
	case CMD_COREB_START:
		spin_lock_irq(&coreb_lock);
		if (coreb_status & COREB_IS_RUNNING) {
			retval = -EBUSY;
			break;
		}
		printk(KERN_INFO "Starting Core B\n");
		coreb_status |= COREB_IS_RUNNING;
		bfin_write_SICA_SYSCR(bfin_read_SICA_SYSCR() & ~0x0020);
		SSYNC();
		spin_unlock_irq(&coreb_lock);
		break;
#if defined(CONFIG_BF561_COREB_RESET)
	case CMD_COREB_STOP:
		spin_lock_irq(&coreb_lock);
		printk(KERN_INFO "Stopping Core B\n");
		bfin_write_SICA_SYSCR(bfin_read_SICA_SYSCR() | 0x0020);
		bfin_write_SICB_SYSCR(bfin_read_SICB_SYSCR() | 0x0080);
		coreb_status &= ~COREB_IS_RUNNING;
		spin_unlock_irq(&coreb_lock);
		break;
	case CMD_COREB_RESET:
		printk(KERN_INFO "Resetting Core B\n");
		bfin_write_SICB_SYSCR(bfin_read_SICB_SYSCR() | 0x0080);
		break;
#endif
	}

	return retval;
}

static struct file_operations coreb_fops = {
	.owner = THIS_MODULE,
	.llseek = coreb_lseek,
	.read = coreb_read,
	.write = coreb_write,
	.ioctl = coreb_ioctl,
	.open = coreb_open,
	.release = coreb_release
};

static struct miscdevice coreb_dev = {
	COREB_MINOR,
	"coreb",
	&coreb_fops
};

static ssize_t coreb_show_status(struct device *dev, struct device_attribute *attr, char *buf)
{
	return sprintf(buf,
		       "Base Address:\t0x%08lx\n"
		       "Core B is %s\n"
		       "SICA_SYSCR:\t%04x\n"
		       "SICB_SYSCR:\t%04x\n"
		       "\n"
		       "IRQ Status:\tCore A\t\tCore B\n"
		       "ISR0:\t\t%08x\t\t%08x\n"
		       "ISR1:\t\t%08x\t\t%08x\n"
		       "IMASK0:\t\t%08x\t\t%08x\n"
		       "IMASK1:\t\t%08x\t\t%08x\n",
		       coreb_base,
		       coreb_status & COREB_IS_RUNNING ? "running" : "stalled",
		       bfin_read_SICA_SYSCR(), bfin_read_SICB_SYSCR(),
		       bfin_read_SICA_ISR0(), bfin_read_SICB_ISR0(),
		       bfin_read_SICA_ISR1(), bfin_read_SICB_ISR0(),
		       bfin_read_SICA_IMASK0(), bfin_read_SICB_IMASK0(),
		       bfin_read_SICA_IMASK1(), bfin_read_SICB_IMASK1());
}

static DEVICE_ATTR(coreb_status, S_IRUGO, coreb_show_status, NULL);

int __init bf561_coreb_init(void)
{
	init_waitqueue_head(&coreb_dma_wait);

	spin_lock_init(&coreb_lock);
	/* Request the core memory regions for Core B */
	if (request_mem_region(0xff600000, 0x4000,
			       "Core B - Instruction SRAM") == NULL)
		goto exit;

	if (request_mem_region(0xFF610000, 0x4000,
			       "Core B - Instruction SRAM") == NULL)
		goto release_instruction_a_sram;

	if (request_mem_region(0xFF500000, 0x8000,
			       "Core B - Data Bank B SRAM") == NULL)
		goto release_instruction_b_sram;

	if (request_mem_region(0xff400000, 0x8000,
			       "Core B - Data Bank A SRAM") == NULL)
		goto release_data_b_sram;

	if (request_dma(CH_MEM_STREAM2_DEST, "Core B - DMA Destination") < 0)
		goto release_data_a_sram;

	if (request_dma(CH_MEM_STREAM2_SRC, "Core B - DMA Source") < 0)
		goto release_dma_dest;

	set_dma_callback(CH_MEM_STREAM2_DEST, coreb_dma_interrupt, NULL);

	misc_register(&coreb_dev);

	if (device_create_file(coreb_dev.this_device, &dev_attr_coreb_status))
		goto release_dma_src;

	printk(KERN_INFO "BF561 Core B driver %s initialized.\n", MODULE_VER);
	return 0;

 release_dma_src:
	free_dma(CH_MEM_STREAM2_SRC);
 release_dma_dest:
	free_dma(CH_MEM_STREAM2_DEST);
 release_data_a_sram:
	release_mem_region(0xff400000, 0x8000);
 release_data_b_sram:
	release_mem_region(0xff500000, 0x8000);
 release_instruction_b_sram:
	release_mem_region(0xff610000, 0x4000);
 release_instruction_a_sram:
	release_mem_region(0xff600000, 0x4000);
 exit:
	return -ENOMEM;
}

void __exit bf561_coreb_exit(void)
{
	device_remove_file(coreb_dev.this_device, &dev_attr_coreb_status);
	misc_deregister(&coreb_dev);

	release_mem_region(0xff610000, 0x4000);
	release_mem_region(0xff600000, 0x4000);
	release_mem_region(0xff500000, 0x8000);
	release_mem_region(0xff400000, 0x8000);

	free_dma(CH_MEM_STREAM2_DEST);
	free_dma(CH_MEM_STREAM2_SRC);
}

module_init(bf561_coreb_init);
module_exit(bf561_coreb_exit);

MODULE_AUTHOR("Bas Vermeulen <bvermeul@blackstar.xs4all.nl>");
MODULE_DESCRIPTION("BF561 Core B Support");
