/*
	Copyright (C) 2004 - 2007 rt2x00 SourceForge Project
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2x00lib
	Abstract: rt2x00 debugfs specific routines.
 */

/*
 * Set enviroment defines for rt2x00.h
 */
#define DRV_NAME "rt2x00lib"

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/uaccess.h>

#include "rt2x00.h"
#include "rt2x00lib.h"

#define PRINT_LINE_LEN_MAX 32

struct rt2x00debug_intf {
	/*
	 * Pointer to driver structure where
	 * this debugfs entry belongs to.
	 */
	struct rt2x00_dev *rt2x00dev;

	/*
	 * Reference to the rt2x00debug structure
	 * which can be used to communicate with
	 * the registers.
	 */
	const struct rt2x00debug *debug;

	/*
	 * Debugfs entries for:
	 * - driver folder
	 * - driver file
	 * - chipset file
	 * - device flags file
	 * - register offset/value files
	 * - eeprom offset/value files
	 * - bbp offset/value files
	 * - rf offset/value files
	 */
	struct dentry *driver_folder;
	struct dentry *driver_entry;
	struct dentry *chipset_entry;
	struct dentry *dev_flags;
	struct dentry *csr_off_entry;
	struct dentry *csr_val_entry;
	struct dentry *eeprom_off_entry;
	struct dentry *eeprom_val_entry;
	struct dentry *bbp_off_entry;
	struct dentry *bbp_val_entry;
	struct dentry *rf_off_entry;
	struct dentry *rf_val_entry;

	/*
	 * Driver and chipset files will use a data buffer
	 * that has been created in advance. This will simplify
	 * the code since we can use the debugfs functions.
	 */
	struct debugfs_blob_wrapper driver_blob;
	struct debugfs_blob_wrapper chipset_blob;

	/*
	 * Requested offset for each register type.
	 */
	unsigned int offset_csr;
	unsigned int offset_eeprom;
	unsigned int offset_bbp;
	unsigned int offset_rf;
};

static int rt2x00debug_file_open(struct inode *inode, struct file *file)
{
	struct rt2x00debug_intf *intf = inode->i_private;

	file->private_data = inode->i_private;

	if (!try_module_get(intf->debug->owner))
		return -EBUSY;

	return 0;
}

static int rt2x00debug_file_release(struct inode *inode, struct file *file)
{
	struct rt2x00debug_intf *intf = file->private_data;

	module_put(intf->debug->owner);

	return 0;
}

#define RT2X00DEBUGFS_OPS_READ(__name, __format, __type)	\
static ssize_t rt2x00debug_read_##__name(struct file *file,	\
					 char __user *buf,	\
					 size_t length,		\
					 loff_t *offset)	\
{								\
	struct rt2x00debug_intf *intf =	file->private_data;	\
	const struct rt2x00debug *debug = intf->debug;		\
	char line[16];						\
	size_t size;						\
	__type value;						\
								\
	if (*offset)						\
		return 0;					\
								\
	if (intf->offset_##__name >= debug->__name.word_count)	\
		return -EINVAL;					\
								\
	debug->__name.read(intf->rt2x00dev,			\
			   intf->offset_##__name, &value);	\
								\
	size = sprintf(line, __format, value);			\
								\
	if (copy_to_user(buf, line, size))			\
		return -EFAULT;					\
								\
	*offset += size;					\
	return size;						\
}

#define RT2X00DEBUGFS_OPS_WRITE(__name, __type)			\
static ssize_t rt2x00debug_write_##__name(struct file *file,	\
					  const char __user *buf,\
					  size_t length,	\
					  loff_t *offset)	\
{								\
	struct rt2x00debug_intf *intf =	file->private_data;	\
	const struct rt2x00debug *debug = intf->debug;		\
	char line[16];						\
	size_t size;						\
	__type value;						\
								\
	if (*offset)						\
		return 0;					\
								\
	if (!capable(CAP_NET_ADMIN))				\
		return -EPERM;					\
								\
	if (intf->offset_##__name >= debug->__name.word_count)	\
		return -EINVAL;					\
								\
	if (copy_from_user(line, buf, length))			\
		return -EFAULT;					\
								\
	size = strlen(line);					\
	value = simple_strtoul(line, NULL, 0);			\
								\
	debug->__name.write(intf->rt2x00dev,			\
			    intf->offset_##__name, value);	\
								\
	*offset += size;					\
	return size;						\
}

#define RT2X00DEBUGFS_OPS(__name, __format, __type)		\
RT2X00DEBUGFS_OPS_READ(__name, __format, __type);		\
RT2X00DEBUGFS_OPS_WRITE(__name, __type);			\
								\
static const struct file_operations rt2x00debug_fop_##__name = {\
	.owner		= THIS_MODULE,				\
	.read		= rt2x00debug_read_##__name,		\
	.write		= rt2x00debug_write_##__name,		\
	.open		= rt2x00debug_file_open,		\
	.release	= rt2x00debug_file_release,		\
};

RT2X00DEBUGFS_OPS(csr, "0x%.8x\n", u32);
RT2X00DEBUGFS_OPS(eeprom, "0x%.4x\n", u16);
RT2X00DEBUGFS_OPS(bbp, "0x%.2x\n", u8);
RT2X00DEBUGFS_OPS(rf, "0x%.8x\n", u32);

static ssize_t rt2x00debug_read_dev_flags(struct file *file,
					  char __user *buf,
					  size_t length,
					  loff_t *offset)
{
	struct rt2x00debug_intf *intf =	file->private_data;
	char line[16];
	size_t size;

	if (*offset)
		return 0;

	size = sprintf(line, "0x%.8x\n", (unsigned int)intf->rt2x00dev->flags);

	if (copy_to_user(buf, line, size))
		return -EFAULT;

	*offset += size;
	return size;
}

static const struct file_operations rt2x00debug_fop_dev_flags = {
	.owner		= THIS_MODULE,
	.read		= rt2x00debug_read_dev_flags,
	.open		= rt2x00debug_file_open,
	.release	= rt2x00debug_file_release,
};

static struct dentry *rt2x00debug_create_file_driver(const char *name,
						     struct rt2x00debug_intf
						     *intf,
						     struct debugfs_blob_wrapper
						     *blob)
{
	char *data;

	data = kzalloc(3 * PRINT_LINE_LEN_MAX, GFP_KERNEL);
	if (!data)
		return NULL;

	blob->data = data;
	data += sprintf(data, "driver: %s\n", intf->rt2x00dev->ops->name);
	data += sprintf(data, "version: %s\n", DRV_VERSION);
	data += sprintf(data, "compiled: %s %s\n", __DATE__, __TIME__);
	blob->size = strlen(blob->data);

	return debugfs_create_blob(name, S_IRUGO, intf->driver_folder, blob);
}

static struct dentry *rt2x00debug_create_file_chipset(const char *name,
						      struct rt2x00debug_intf
						      *intf,
						      struct
						      debugfs_blob_wrapper
						      *blob)
{
	const struct rt2x00debug *debug = intf->debug;
	char *data;

	data = kzalloc(4 * PRINT_LINE_LEN_MAX, GFP_KERNEL);
	if (!data)
		return NULL;

	blob->data = data;
	data += sprintf(data, "csr length: %d\n", debug->csr.word_count);
	data += sprintf(data, "eeprom length: %d\n", debug->eeprom.word_count);
	data += sprintf(data, "bbp length: %d\n", debug->bbp.word_count);
	data += sprintf(data, "rf length: %d\n", debug->rf.word_count);
	blob->size = strlen(blob->data);

	return debugfs_create_blob(name, S_IRUGO, intf->driver_folder, blob);
}

void rt2x00debug_register(struct rt2x00_dev *rt2x00dev)
{
	const struct rt2x00debug *debug = rt2x00dev->ops->debugfs;
	struct rt2x00debug_intf *intf;

	intf = kzalloc(sizeof(struct rt2x00debug_intf), GFP_KERNEL);
	if (!intf) {
		ERROR(rt2x00dev, "Failed to allocate debug handler.\n");
		return;
	}

	intf->debug = debug;
	intf->rt2x00dev = rt2x00dev;
	rt2x00dev->debugfs_intf = intf;

	intf->driver_folder =
	    debugfs_create_dir(intf->rt2x00dev->ops->name,
			       rt2x00dev->hw->wiphy->debugfsdir);
	if (IS_ERR(intf->driver_folder))
		goto exit;

	intf->driver_entry =
	    rt2x00debug_create_file_driver("driver", intf, &intf->driver_blob);
	if (IS_ERR(intf->driver_entry))
		goto exit;

	intf->chipset_entry =
	    rt2x00debug_create_file_chipset("chipset",
					    intf, &intf->chipset_blob);
	if (IS_ERR(intf->chipset_entry))
		goto exit;

	intf->dev_flags = debugfs_create_file("dev_flags", S_IRUGO,
					      intf->driver_folder, intf,
					      &rt2x00debug_fop_dev_flags);
	if (IS_ERR(intf->dev_flags))
		goto exit;

#define RT2X00DEBUGFS_CREATE_ENTRY(__intf, __name)		\
({								\
	(__intf)->__name##_off_entry =				\
	    debugfs_create_u32(__stringify(__name) "_offset",	\
			       S_IRUGO | S_IWUSR,		\
			       (__intf)->driver_folder,		\
			       &(__intf)->offset_##__name);	\
	if (IS_ERR((__intf)->__name##_off_entry))		\
		goto exit;					\
								\
	(__intf)->__name##_val_entry =				\
	    debugfs_create_file(__stringify(__name) "_value",	\
				S_IRUGO | S_IWUSR,		\
				(__intf)->driver_folder,	\
				(__intf), &rt2x00debug_fop_##__name);\
	if (IS_ERR((__intf)->__name##_val_entry))		\
		goto exit;					\
})

	RT2X00DEBUGFS_CREATE_ENTRY(intf, csr);
	RT2X00DEBUGFS_CREATE_ENTRY(intf, eeprom);
	RT2X00DEBUGFS_CREATE_ENTRY(intf, bbp);
	RT2X00DEBUGFS_CREATE_ENTRY(intf, rf);

#undef RT2X00DEBUGFS_CREATE_ENTRY

	return;

exit:
	rt2x00debug_deregister(rt2x00dev);
	ERROR(rt2x00dev, "Failed to register debug handler.\n");

	return;
}

void rt2x00debug_deregister(struct rt2x00_dev *rt2x00dev)
{
	const struct rt2x00debug_intf *intf = rt2x00dev->debugfs_intf;

	if (unlikely(!intf))
		return;

	debugfs_remove(intf->rf_val_entry);
	debugfs_remove(intf->rf_off_entry);
	debugfs_remove(intf->bbp_val_entry);
	debugfs_remove(intf->bbp_off_entry);
	debugfs_remove(intf->eeprom_val_entry);
	debugfs_remove(intf->eeprom_off_entry);
	debugfs_remove(intf->csr_val_entry);
	debugfs_remove(intf->csr_off_entry);
	debugfs_remove(intf->dev_flags);
	debugfs_remove(intf->chipset_entry);
	debugfs_remove(intf->driver_entry);
	debugfs_remove(intf->driver_folder);
	kfree(intf->chipset_blob.data);
	kfree(intf->driver_blob.data);
	kfree(intf);

	rt2x00dev->debugfs_intf = NULL;
}
