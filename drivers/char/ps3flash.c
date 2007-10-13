/*
 * PS3 FLASH ROM Storage Driver
 *
 * Copyright (C) 2007 Sony Computer Entertainment Inc.
 * Copyright 2007 Sony Corp.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published
 * by the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>

#include <asm/lv1call.h>
#include <asm/ps3stor.h>


#define DEVICE_NAME		"ps3flash"

#define FLASH_BLOCK_SIZE	(256*1024)


struct ps3flash_private {
	struct mutex mutex;	/* Bounce buffer mutex */
};

static struct ps3_storage_device *ps3flash_dev;

static ssize_t ps3flash_read_write_sectors(struct ps3_storage_device *dev,
					   u64 lpar, u64 start_sector,
					   u64 sectors, int write)
{
	u64 res = ps3stor_read_write_sectors(dev, lpar, start_sector, sectors,
					     write);
	if (res) {
		dev_err(&dev->sbd.core, "%s:%u: %s failed 0x%lx\n", __func__,
			__LINE__, write ? "write" : "read", res);
		return -EIO;
	}
	return sectors;
}

static ssize_t ps3flash_read_sectors(struct ps3_storage_device *dev,
				     u64 start_sector, u64 sectors,
				     unsigned int sector_offset)
{
	u64 max_sectors, lpar;

	max_sectors = dev->bounce_size / dev->blk_size;
	if (sectors > max_sectors) {
		dev_dbg(&dev->sbd.core, "%s:%u Limiting sectors to %lu\n",
			__func__, __LINE__, max_sectors);
		sectors = max_sectors;
	}

	lpar = dev->bounce_lpar + sector_offset * dev->blk_size;
	return ps3flash_read_write_sectors(dev, lpar, start_sector, sectors,
					   0);
}

static ssize_t ps3flash_write_chunk(struct ps3_storage_device *dev,
				    u64 start_sector)
{
       u64 sectors = dev->bounce_size / dev->blk_size;
       return ps3flash_read_write_sectors(dev, dev->bounce_lpar, start_sector,
					  sectors, 1);
}

static loff_t ps3flash_llseek(struct file *file, loff_t offset, int origin)
{
	struct ps3_storage_device *dev = ps3flash_dev;
	loff_t res;

	mutex_lock(&file->f_mapping->host->i_mutex);
	switch (origin) {
	case 1:
		offset += file->f_pos;
		break;
	case 2:
		offset += dev->regions[dev->region_idx].size*dev->blk_size;
		break;
	}
	if (offset < 0) {
		res = -EINVAL;
		goto out;
	}

	file->f_pos = offset;
	res = file->f_pos;

out:
	mutex_unlock(&file->f_mapping->host->i_mutex);
	return res;
}

static ssize_t ps3flash_read(struct file *file, char __user *buf, size_t count,
			     loff_t *pos)
{
	struct ps3_storage_device *dev = ps3flash_dev;
	struct ps3flash_private *priv = dev->sbd.core.driver_data;
	u64 size, start_sector, end_sector, offset;
	ssize_t sectors_read;
	size_t remaining, n;

	dev_dbg(&dev->sbd.core,
		"%s:%u: Reading %zu bytes at position %lld to user 0x%p\n",
		__func__, __LINE__, count, *pos, buf);

	size = dev->regions[dev->region_idx].size*dev->blk_size;
	if (*pos >= size || !count)
		return 0;

	if (*pos + count > size) {
		dev_dbg(&dev->sbd.core,
			"%s:%u Truncating count from %zu to %llu\n", __func__,
			__LINE__, count, size - *pos);
		count = size - *pos;
	}

	start_sector = *pos / dev->blk_size;
	offset = *pos % dev->blk_size;
	end_sector = DIV_ROUND_UP(*pos + count, dev->blk_size);

	remaining = count;
	do {
		mutex_lock(&priv->mutex);

		sectors_read = ps3flash_read_sectors(dev, start_sector,
						     end_sector-start_sector,
						     0);
		if (sectors_read < 0) {
			mutex_unlock(&priv->mutex);
			goto fail;
		}

		n = min(remaining, sectors_read*dev->blk_size-offset);
		dev_dbg(&dev->sbd.core,
			"%s:%u: copy %lu bytes from 0x%p to user 0x%p\n",
			__func__, __LINE__, n, dev->bounce_buf+offset, buf);
		if (copy_to_user(buf, dev->bounce_buf+offset, n)) {
			mutex_unlock(&priv->mutex);
			sectors_read = -EFAULT;
			goto fail;
		}

		mutex_unlock(&priv->mutex);

		*pos += n;
		buf += n;
		remaining -= n;
		start_sector += sectors_read;
		offset = 0;
	} while (remaining > 0);

	return count;

fail:
	return sectors_read;
}

static ssize_t ps3flash_write(struct file *file, const char __user *buf,
			      size_t count, loff_t *pos)
{
	struct ps3_storage_device *dev = ps3flash_dev;
	struct ps3flash_private *priv = dev->sbd.core.driver_data;
	u64 size, chunk_sectors, start_write_sector, end_write_sector,
	    end_read_sector, start_read_sector, head, tail, offset;
	ssize_t res;
	size_t remaining, n;
	unsigned int sec_off;

	dev_dbg(&dev->sbd.core,
		"%s:%u: Writing %zu bytes at position %lld from user 0x%p\n",
		__func__, __LINE__, count, *pos, buf);

	size = dev->regions[dev->region_idx].size*dev->blk_size;
	if (*pos >= size || !count)
		return 0;

	if (*pos + count > size) {
		dev_dbg(&dev->sbd.core,
			"%s:%u Truncating count from %zu to %llu\n", __func__,
			__LINE__, count, size - *pos);
		count = size - *pos;
	}

	chunk_sectors = dev->bounce_size / dev->blk_size;

	start_write_sector = *pos / dev->bounce_size * chunk_sectors;
	offset = *pos % dev->bounce_size;
	end_write_sector = DIV_ROUND_UP(*pos + count, dev->bounce_size) *
			   chunk_sectors;

	end_read_sector = DIV_ROUND_UP(*pos, dev->blk_size);
	start_read_sector = (*pos + count) / dev->blk_size;

	/*
	 * As we have to write in 256 KiB chunks, while we can read in blk_size
	 * (usually 512 bytes) chunks, we perform the following steps:
	 *   1. Read from start_write_sector to end_read_sector ("head")
	 *   2. Read from start_read_sector to end_write_sector ("tail")
	 *   3. Copy data to buffer
	 *   4. Write from start_write_sector to end_write_sector
	 * All of this is complicated by using only one 256 KiB bounce buffer.
	 */

	head = end_read_sector - start_write_sector;
	tail = end_write_sector - start_read_sector;

	remaining = count;
	do {
		mutex_lock(&priv->mutex);

		if (end_read_sector >= start_read_sector) {
			/* Merge head and tail */
			dev_dbg(&dev->sbd.core,
				"Merged head and tail: %lu sectors at %lu\n",
				chunk_sectors, start_write_sector);
			res = ps3flash_read_sectors(dev, start_write_sector,
						    chunk_sectors, 0);
			if (res < 0)
				goto fail;
		} else {
			if (head) {
				/* Read head */
				dev_dbg(&dev->sbd.core,
					"head: %lu sectors at %lu\n", head,
					start_write_sector);
				res = ps3flash_read_sectors(dev,
							    start_write_sector,
							    head, 0);
				if (res < 0)
					goto fail;
			}
			if (start_read_sector <
			    start_write_sector+chunk_sectors) {
				/* Read tail */
				dev_dbg(&dev->sbd.core,
					"tail: %lu sectors at %lu\n", tail,
					start_read_sector);
				sec_off = start_read_sector-start_write_sector;
				res = ps3flash_read_sectors(dev,
							    start_read_sector,
							    tail, sec_off);
				if (res < 0)
					goto fail;
			}
		}

		n = min(remaining, dev->bounce_size-offset);
		dev_dbg(&dev->sbd.core,
			"%s:%u: copy %lu bytes from user 0x%p to 0x%p\n",
			__func__, __LINE__, n, buf, dev->bounce_buf+offset);
		if (copy_from_user(dev->bounce_buf+offset, buf, n)) {
			res = -EFAULT;
			goto fail;
		}

		res = ps3flash_write_chunk(dev, start_write_sector);
		if (res < 0)
			goto fail;

		mutex_unlock(&priv->mutex);

		*pos += n;
		buf += n;
		remaining -= n;
		start_write_sector += chunk_sectors;
		head = 0;
		offset = 0;
	} while (remaining > 0);

	return count;

fail:
	mutex_unlock(&priv->mutex);
	return res;
}


static irqreturn_t ps3flash_interrupt(int irq, void *data)
{
	struct ps3_storage_device *dev = data;
	int res;
	u64 tag, status;

	res = lv1_storage_get_async_status(dev->sbd.dev_id, &tag, &status);

	if (tag != dev->tag)
		dev_err(&dev->sbd.core,
			"%s:%u: tag mismatch, got %lx, expected %lx\n",
			__func__, __LINE__, tag, dev->tag);

	if (res) {
		dev_err(&dev->sbd.core, "%s:%u: res=%d status=0x%lx\n",
			__func__, __LINE__, res, status);
	} else {
		dev->lv1_status = status;
		complete(&dev->done);
	}
	return IRQ_HANDLED;
}


static const struct file_operations ps3flash_fops = {
	.owner	= THIS_MODULE,
	.llseek	= ps3flash_llseek,
	.read	= ps3flash_read,
	.write	= ps3flash_write,
};

static struct miscdevice ps3flash_misc = {
	.minor	= MISC_DYNAMIC_MINOR,
	.name	= DEVICE_NAME,
	.fops	= &ps3flash_fops,
};

static int __devinit ps3flash_probe(struct ps3_system_bus_device *_dev)
{
	struct ps3_storage_device *dev = to_ps3_storage_device(&_dev->core);
	struct ps3flash_private *priv;
	int error;
	unsigned long tmp;

	tmp = dev->regions[dev->region_idx].start*dev->blk_size;
	if (tmp % FLASH_BLOCK_SIZE) {
		dev_err(&dev->sbd.core,
			"%s:%u region start %lu is not aligned\n", __func__,
			__LINE__, tmp);
		return -EINVAL;
	}
	tmp = dev->regions[dev->region_idx].size*dev->blk_size;
	if (tmp % FLASH_BLOCK_SIZE) {
		dev_err(&dev->sbd.core,
			"%s:%u region size %lu is not aligned\n", __func__,
			__LINE__, tmp);
		return -EINVAL;
	}

	/* use static buffer, kmalloc cannot allocate 256 KiB */
	if (!ps3flash_bounce_buffer.address)
		return -ENODEV;

	if (ps3flash_dev) {
		dev_err(&dev->sbd.core,
			"Only one FLASH device is supported\n");
		return -EBUSY;
	}

	ps3flash_dev = dev;

	priv = kzalloc(sizeof(*priv), GFP_KERNEL);
	if (!priv) {
		error = -ENOMEM;
		goto fail;
	}

	dev->sbd.core.driver_data = priv;
	mutex_init(&priv->mutex);

	dev->bounce_size = ps3flash_bounce_buffer.size;
	dev->bounce_buf = ps3flash_bounce_buffer.address;

	error = ps3stor_setup(dev, ps3flash_interrupt);
	if (error)
		goto fail_free_priv;

	ps3flash_misc.parent = &dev->sbd.core;
	error = misc_register(&ps3flash_misc);
	if (error) {
		dev_err(&dev->sbd.core, "%s:%u: misc_register failed %d\n",
			__func__, __LINE__, error);
		goto fail_teardown;
	}

	dev_info(&dev->sbd.core, "%s:%u: registered misc device %d\n",
		 __func__, __LINE__, ps3flash_misc.minor);
	return 0;

fail_teardown:
	ps3stor_teardown(dev);
fail_free_priv:
	kfree(priv);
	dev->sbd.core.driver_data = NULL;
fail:
	ps3flash_dev = NULL;
	return error;
}

static int ps3flash_remove(struct ps3_system_bus_device *_dev)
{
	struct ps3_storage_device *dev = to_ps3_storage_device(&_dev->core);

	misc_deregister(&ps3flash_misc);
	ps3stor_teardown(dev);
	kfree(dev->sbd.core.driver_data);
	dev->sbd.core.driver_data = NULL;
	ps3flash_dev = NULL;
	return 0;
}


static struct ps3_system_bus_driver ps3flash = {
	.match_id	= PS3_MATCH_ID_STOR_FLASH,
	.core.name	= DEVICE_NAME,
	.core.owner	= THIS_MODULE,
	.probe		= ps3flash_probe,
	.remove		= ps3flash_remove,
	.shutdown	= ps3flash_remove,
};


static int __init ps3flash_init(void)
{
	return ps3_system_bus_driver_register(&ps3flash);
}

static void __exit ps3flash_exit(void)
{
	ps3_system_bus_driver_unregister(&ps3flash);
}

module_init(ps3flash_init);
module_exit(ps3flash_exit);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("PS3 FLASH ROM Storage Driver");
MODULE_AUTHOR("Sony Corporation");
MODULE_ALIAS(PS3_MODULE_ALIAS_STOR_FLASH);
