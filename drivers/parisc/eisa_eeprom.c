/* 
 *    EISA "eeprom" support routines
 *
 *    Copyright (C) 2001 Thomas Bogendoerfer <tsbogend at parisc-linux.org>
 *
 *
 *    This program is free software; you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation; either version 2 of the License, or
 *    (at your option) any later version.
 *
 *    This program is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with this program; if not, write to the Free Software
 *    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/miscdevice.h>
#include <linux/slab.h>
#include <linux/fs.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/eisa_eeprom.h>

#define 	EISA_EEPROM_MINOR 241

static loff_t eisa_eeprom_llseek(struct file *file, loff_t offset, int origin )
{
	switch (origin) {
	  case 0:
		/* nothing to do */
		break;
	  case 1:
		offset += file->f_pos;
		break;
	  case 2:
		offset += HPEE_MAX_LENGTH;
		break;
	}
	return (offset >= 0 && offset < HPEE_MAX_LENGTH) ? (file->f_pos = offset) : -EINVAL;
}

static ssize_t eisa_eeprom_read(struct file * file,
			      char *buf, size_t count, loff_t *ppos )
{
	unsigned char *tmp;
	ssize_t ret;
	int i;
	
	if (*ppos >= HPEE_MAX_LENGTH)
		return 0;
	
	count = *ppos + count < HPEE_MAX_LENGTH ? count : HPEE_MAX_LENGTH - *ppos;
	tmp = kmalloc(count, GFP_KERNEL);
	if (tmp) {
		for (i = 0; i < count; i++)
			tmp[i] = readb(eisa_eeprom_addr+(*ppos)++);

		if (copy_to_user (buf, tmp, count))
			ret = -EFAULT;
		else
			ret = count;
		kfree (tmp);
	} else
		ret = -ENOMEM;
	
	return ret;
}

static int eisa_eeprom_ioctl(struct inode *inode, struct file *file, 
			   unsigned int cmd,
			   unsigned long arg)
{
	return -ENOTTY;
}

static int eisa_eeprom_open(struct inode *inode, struct file *file)
{
	if (file->f_mode & 2)
		return -EINVAL;
   
	return 0;
}

static int eisa_eeprom_release(struct inode *inode, struct file *file)
{
	return 0;
}

/*
 *	The various file operations we support.
 */
static struct file_operations eisa_eeprom_fops = {
	.owner =	THIS_MODULE,
	.llseek =	eisa_eeprom_llseek,
	.read =		eisa_eeprom_read,
	.ioctl =	eisa_eeprom_ioctl,
	.open =		eisa_eeprom_open,
	.release =	eisa_eeprom_release,
};

static struct miscdevice eisa_eeprom_dev = {
	EISA_EEPROM_MINOR,
	"eisa_eeprom",
	&eisa_eeprom_fops
};

static int __init eisa_eeprom_init(void)
{
	int retval;

	if (!eisa_eeprom_addr)
		return -ENODEV;

	retval = misc_register(&eisa_eeprom_dev);
	if (retval < 0) {
		printk(KERN_ERR "EISA EEPROM: cannot register misc device.\n");
		return retval;
	}

	printk(KERN_INFO "EISA EEPROM at 0x%p\n", eisa_eeprom_addr);
	return 0;
}

MODULE_LICENSE("GPL");

module_init(eisa_eeprom_init);
