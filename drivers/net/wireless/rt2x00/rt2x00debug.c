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

#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/poll.h>
#include <linux/uaccess.h>

#include "rt2x00.h"
#include "rt2x00lib.h"
#include "rt2x00dump.h"

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
	 *   - driver file
	 *   - chipset file
	 *   - device flags file
	 *   - register folder
	 *     - csr offset/value files
	 *     - eeprom offset/value files
	 *     - bbp offset/value files
	 *     - rf offset/value files
	 *   - frame dump folder
	 *     - frame dump file
	 */
	struct dentry *driver_folder;
	struct dentry *driver_entry;
	struct dentry *chipset_entry;
	struct dentry *dev_flags;
	struct dentry *register_folder;
	struct dentry *csr_off_entry;
	struct dentry *csr_val_entry;
	struct dentry *eeprom_off_entry;
	struct dentry *eeprom_val_entry;
	struct dentry *bbp_off_entry;
	struct dentry *bbp_val_entry;
	struct dentry *rf_off_entry;
	struct dentry *rf_val_entry;
	struct dentry *frame_folder;
	struct dentry *frame_dump_entry;

	/*
	 * The frame dump file only allows a single reader,
	 * so we need to store the current state here.
	 */
	unsigned long frame_dump_flags;
#define FRAME_DUMP_FILE_OPEN	1

	/*
	 * We queue each frame before dumping it to the user,
	 * per read command we will pass a single skb structure
	 * so we should be prepared to queue multiple sk buffers
	 * before sending it to userspace.
	 */
	struct sk_buff_head frame_dump_skbqueue;
	wait_queue_head_t frame_dump_waitqueue;

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

void rt2x00debug_dump_frame(struct rt2x00_dev *rt2x00dev,
			    struct sk_buff *skb)
{
	struct rt2x00debug_intf *intf = rt2x00dev->debugfs_intf;
	struct skb_desc *desc = get_skb_desc(skb);
	struct sk_buff *skbcopy;
	struct rt2x00dump_hdr *dump_hdr;
	struct timeval timestamp;

	do_gettimeofday(&timestamp);

	if (!test_bit(FRAME_DUMP_FILE_OPEN, &intf->frame_dump_flags))
		return;

	if (skb_queue_len(&intf->frame_dump_skbqueue) > 20) {
		DEBUG(rt2x00dev, "txrx dump queue length exceeded.\n");
		return;
	}

	skbcopy = alloc_skb(sizeof(*dump_hdr) + desc->desc_len + desc->data_len,
			    GFP_ATOMIC);
	if (!skbcopy) {
		DEBUG(rt2x00dev, "Failed to copy skb for dump.\n");
		return;
	}

	dump_hdr = (struct rt2x00dump_hdr *)skb_put(skbcopy, sizeof(*dump_hdr));
	dump_hdr->version = cpu_to_le32(DUMP_HEADER_VERSION);
	dump_hdr->header_length = cpu_to_le32(sizeof(*dump_hdr));
	dump_hdr->desc_length = cpu_to_le32(desc->desc_len);
	dump_hdr->data_length = cpu_to_le32(desc->data_len);
	dump_hdr->chip_rt = cpu_to_le16(rt2x00dev->chip.rt);
	dump_hdr->chip_rf = cpu_to_le16(rt2x00dev->chip.rf);
	dump_hdr->chip_rev = cpu_to_le32(rt2x00dev->chip.rev);
	dump_hdr->type = cpu_to_le16(desc->frame_type);
	dump_hdr->ring_index = desc->ring->queue_idx;
	dump_hdr->entry_index = desc->entry->entry_idx;
	dump_hdr->timestamp_sec = cpu_to_le32(timestamp.tv_sec);
	dump_hdr->timestamp_usec = cpu_to_le32(timestamp.tv_usec);

	memcpy(skb_put(skbcopy, desc->desc_len), desc->desc, desc->desc_len);
	memcpy(skb_put(skbcopy, desc->data_len), desc->data, desc->data_len);

	skb_queue_tail(&intf->frame_dump_skbqueue, skbcopy);
	wake_up_interruptible(&intf->frame_dump_waitqueue);

	/*
	 * Verify that the file has not been closed while we were working.
	 */
	if (!test_bit(FRAME_DUMP_FILE_OPEN, &intf->frame_dump_flags))
		skb_queue_purge(&intf->frame_dump_skbqueue);
}

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

static int rt2x00debug_open_ring_dump(struct inode *inode, struct file *file)
{
	struct rt2x00debug_intf *intf = inode->i_private;
	int retval;

	retval = rt2x00debug_file_open(inode, file);
	if (retval)
		return retval;

	if (test_and_set_bit(FRAME_DUMP_FILE_OPEN, &intf->frame_dump_flags)) {
		rt2x00debug_file_release(inode, file);
		return -EBUSY;
	}

	return 0;
}

static int rt2x00debug_release_ring_dump(struct inode *inode, struct file *file)
{
	struct rt2x00debug_intf *intf = inode->i_private;

	skb_queue_purge(&intf->frame_dump_skbqueue);

	clear_bit(FRAME_DUMP_FILE_OPEN, &intf->frame_dump_flags);

	return rt2x00debug_file_release(inode, file);
}

static ssize_t rt2x00debug_read_ring_dump(struct file *file,
					  char __user *buf,
					  size_t length,
					  loff_t *offset)
{
	struct rt2x00debug_intf *intf = file->private_data;
	struct sk_buff *skb;
	size_t status;
	int retval;

	if (file->f_flags & O_NONBLOCK)
		return -EAGAIN;

	retval =
	    wait_event_interruptible(intf->frame_dump_waitqueue,
				     (skb =
				     skb_dequeue(&intf->frame_dump_skbqueue)));
	if (retval)
		return retval;

	status = min((size_t)skb->len, length);
	if (copy_to_user(buf, skb->data, status)) {
		status = -EFAULT;
		goto exit;
	}

	*offset += status;

exit:
	kfree_skb(skb);

	return status;
}

static unsigned int rt2x00debug_poll_ring_dump(struct file *file,
					       poll_table *wait)
{
	struct rt2x00debug_intf *intf = file->private_data;

	poll_wait(file, &intf->frame_dump_waitqueue, wait);

	if (!skb_queue_empty(&intf->frame_dump_skbqueue))
		return POLLOUT | POLLWRNORM;

	return 0;
}

static const struct file_operations rt2x00debug_fop_ring_dump = {
	.owner		= THIS_MODULE,
	.read		= rt2x00debug_read_ring_dump,
	.poll		= rt2x00debug_poll_ring_dump,
	.open		= rt2x00debug_open_ring_dump,
	.release	= rt2x00debug_release_ring_dump,
};

#define RT2X00DEBUGFS_OPS_READ(__name, __format, __type)	\
static ssize_t rt2x00debug_read_##__name(struct file *file,	\
					 char __user *buf,	\
					 size_t length,		\
					 loff_t *offset)	\
{								\
	struct rt2x00debug_intf *intf = file->private_data;	\
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
	struct rt2x00debug_intf *intf = file->private_data;	\
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

	data = kzalloc(8 * PRINT_LINE_LEN_MAX, GFP_KERNEL);
	if (!data)
		return NULL;

	blob->data = data;
	data += sprintf(data, "rt chip: %04x\n", intf->rt2x00dev->chip.rt);
	data += sprintf(data, "rf chip: %04x\n", intf->rt2x00dev->chip.rf);
	data += sprintf(data, "revision:%08x\n", intf->rt2x00dev->chip.rev);
	data += sprintf(data, "\n");
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

	intf->register_folder =
	    debugfs_create_dir("register", intf->driver_folder);
	if (IS_ERR(intf->register_folder))
		goto exit;

#define RT2X00DEBUGFS_CREATE_REGISTER_ENTRY(__intf, __name)	\
({								\
	(__intf)->__name##_off_entry =				\
	    debugfs_create_u32(__stringify(__name) "_offset",	\
			       S_IRUGO | S_IWUSR,		\
			       (__intf)->register_folder,	\
			       &(__intf)->offset_##__name);	\
	if (IS_ERR((__intf)->__name##_off_entry))		\
		goto exit;					\
								\
	(__intf)->__name##_val_entry =				\
	    debugfs_create_file(__stringify(__name) "_value",	\
				S_IRUGO | S_IWUSR,		\
				(__intf)->register_folder,	\
				(__intf), &rt2x00debug_fop_##__name);\
	if (IS_ERR((__intf)->__name##_val_entry))		\
		goto exit;					\
})

	RT2X00DEBUGFS_CREATE_REGISTER_ENTRY(intf, csr);
	RT2X00DEBUGFS_CREATE_REGISTER_ENTRY(intf, eeprom);
	RT2X00DEBUGFS_CREATE_REGISTER_ENTRY(intf, bbp);
	RT2X00DEBUGFS_CREATE_REGISTER_ENTRY(intf, rf);

#undef RT2X00DEBUGFS_CREATE_REGISTER_ENTRY

	intf->frame_folder =
	    debugfs_create_dir("frame", intf->driver_folder);
	if (IS_ERR(intf->frame_folder))
		goto exit;

	intf->frame_dump_entry =
	    debugfs_create_file("dump", S_IRUGO, intf->frame_folder,
				intf, &rt2x00debug_fop_ring_dump);
	if (IS_ERR(intf->frame_dump_entry))
		goto exit;

	skb_queue_head_init(&intf->frame_dump_skbqueue);
	init_waitqueue_head(&intf->frame_dump_waitqueue);

	return;

exit:
	rt2x00debug_deregister(rt2x00dev);
	ERROR(rt2x00dev, "Failed to register debug handler.\n");

	return;
}

void rt2x00debug_deregister(struct rt2x00_dev *rt2x00dev)
{
	struct rt2x00debug_intf *intf = rt2x00dev->debugfs_intf;

	if (unlikely(!intf))
		return;

	skb_queue_purge(&intf->frame_dump_skbqueue);

	debugfs_remove(intf->frame_dump_entry);
	debugfs_remove(intf->frame_folder);
	debugfs_remove(intf->rf_val_entry);
	debugfs_remove(intf->rf_off_entry);
	debugfs_remove(intf->bbp_val_entry);
	debugfs_remove(intf->bbp_off_entry);
	debugfs_remove(intf->eeprom_val_entry);
	debugfs_remove(intf->eeprom_off_entry);
	debugfs_remove(intf->csr_val_entry);
	debugfs_remove(intf->csr_off_entry);
	debugfs_remove(intf->register_folder);
	debugfs_remove(intf->dev_flags);
	debugfs_remove(intf->chipset_entry);
	debugfs_remove(intf->driver_entry);
	debugfs_remove(intf->driver_folder);
	kfree(intf->chipset_blob.data);
	kfree(intf->driver_blob.data);
	kfree(intf);

	rt2x00dev->debugfs_intf = NULL;
}
