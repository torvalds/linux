/*
 * Copyright (c) International Business Machines Corp., 2006
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See
 * the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 * Author: Artem Bityutskiy (Битюцкий Артём), Joern Engel
 */

/*
 * This is a small driver which implements fake MTD devices on top of UBI
 * volumes. This sounds strange, but it is in fact quite useful to make
 * MTD-oriented software (including all the legacy software) work on top of
 * UBI.
 *
 * Gluebi emulates MTD devices of "MTD_UBIVOLUME" type. Their minimal I/O unit
 * size (@mtd->writesize) is equivalent to the UBI minimal I/O unit. The
 * eraseblock size is equivalent to the logical eraseblock size of the volume.
 */

#include <linux/err.h>
#include <linux/list.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/math64.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/mtd/ubi.h>
#include <linux/mtd/mtd.h>
#include "ubi-media.h"

#define err_msg(fmt, ...)                                   \
	pr_err("gluebi (pid %d): %s: " fmt "\n",            \
	       current->pid, __func__, ##__VA_ARGS__)

/**
 * struct gluebi_device - a gluebi device description data structure.
 * @mtd: emulated MTD device description object
 * @refcnt: gluebi device reference count
 * @desc: UBI volume descriptor
 * @ubi_num: UBI device number this gluebi device works on
 * @vol_id: ID of UBI volume this gluebi device works on
 * @list: link in a list of gluebi devices
 */
struct gluebi_device {
	struct mtd_info mtd;
	int refcnt;
	struct ubi_volume_desc *desc;
	int ubi_num;
	int vol_id;
	struct list_head list;
};

/* List of all gluebi devices */
static LIST_HEAD(gluebi_devices);
static DEFINE_MUTEX(devices_mutex);

/**
 * find_gluebi_nolock - find a gluebi device.
 * @ubi_num: UBI device number
 * @vol_id: volume ID
 *
 * This function seraches for gluebi device corresponding to UBI device
 * @ubi_num and UBI volume @vol_id. Returns the gluebi device description
 * object in case of success and %NULL in case of failure. The caller has to
 * have the &devices_mutex locked.
 */
static struct gluebi_device *find_gluebi_nolock(int ubi_num, int vol_id)
{
	struct gluebi_device *gluebi;

	list_for_each_entry(gluebi, &gluebi_devices, list)
		if (gluebi->ubi_num == ubi_num && gluebi->vol_id == vol_id)
			return gluebi;
	return NULL;
}

/**
 * gluebi_get_device - get MTD device reference.
 * @mtd: the MTD device description object
 *
 * This function is called every time the MTD device is being opened and
 * implements the MTD get_device() operation. Returns zero in case of success
 * and a negative error code in case of failure.
 */
static int gluebi_get_device(struct mtd_info *mtd)
{
	struct gluebi_device *gluebi;
	int ubi_mode = UBI_READONLY;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	if (mtd->flags & MTD_WRITEABLE)
		ubi_mode = UBI_READWRITE;

	gluebi = container_of(mtd, struct gluebi_device, mtd);
	mutex_lock(&devices_mutex);
	if (gluebi->refcnt > 0) {
		/*
		 * The MTD device is already referenced and this is just one
		 * more reference. MTD allows many users to open the same
		 * volume simultaneously and do not distinguish between
		 * readers/writers/exclusive openers as UBI does. So we do not
		 * open the UBI volume again - just increase the reference
		 * counter and return.
		 */
		gluebi->refcnt += 1;
		mutex_unlock(&devices_mutex);
		return 0;
	}

	/*
	 * This is the first reference to this UBI volume via the MTD device
	 * interface. Open the corresponding volume in read-write mode.
	 */
	gluebi->desc = ubi_open_volume(gluebi->ubi_num, gluebi->vol_id,
				       ubi_mode);
	if (IS_ERR(gluebi->desc)) {
		mutex_unlock(&devices_mutex);
		module_put(THIS_MODULE);
		return PTR_ERR(gluebi->desc);
	}
	gluebi->refcnt += 1;
	mutex_unlock(&devices_mutex);
	return 0;
}

/**
 * gluebi_put_device - put MTD device reference.
 * @mtd: the MTD device description object
 *
 * This function is called every time the MTD device is being put. Returns
 * zero in case of success and a negative error code in case of failure.
 */
static void gluebi_put_device(struct mtd_info *mtd)
{
	struct gluebi_device *gluebi;

	gluebi = container_of(mtd, struct gluebi_device, mtd);
	mutex_lock(&devices_mutex);
	gluebi->refcnt -= 1;
	if (gluebi->refcnt == 0)
		ubi_close_volume(gluebi->desc);
	module_put(THIS_MODULE);
	mutex_unlock(&devices_mutex);
}

/**
 * gluebi_read - read operation of emulated MTD devices.
 * @mtd: MTD device description object
 * @from: absolute offset from where to read
 * @len: how many bytes to read
 * @retlen: count of read bytes is returned here
 * @buf: buffer to store the read data
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
static int gluebi_read(struct mtd_info *mtd, loff_t from, size_t len,
		       size_t *retlen, unsigned char *buf)
{
	int err = 0, lnum, offs, total_read;
	struct gluebi_device *gluebi;

	gluebi = container_of(mtd, struct gluebi_device, mtd);
	lnum = div_u64_rem(from, mtd->erasesize, &offs);
	total_read = len;
	while (total_read) {
		size_t to_read = mtd->erasesize - offs;

		if (to_read > total_read)
			to_read = total_read;

		err = ubi_read(gluebi->desc, lnum, buf, offs, to_read);
		if (err)
			break;

		lnum += 1;
		offs = 0;
		total_read -= to_read;
		buf += to_read;
	}

	*retlen = len - total_read;
	return err;
}

/**
 * gluebi_write - write operation of emulated MTD devices.
 * @mtd: MTD device description object
 * @to: absolute offset where to write
 * @len: how many bytes to write
 * @retlen: count of written bytes is returned here
 * @buf: buffer with data to write
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
static int gluebi_write(struct mtd_info *mtd, loff_t to, size_t len,
			size_t *retlen, const u_char *buf)
{
	int err = 0, lnum, offs, total_written;
	struct gluebi_device *gluebi;

	gluebi = container_of(mtd, struct gluebi_device, mtd);
	lnum = div_u64_rem(to, mtd->erasesize, &offs);

	if (len % mtd->writesize || offs % mtd->writesize)
		return -EINVAL;

	total_written = len;
	while (total_written) {
		size_t to_write = mtd->erasesize - offs;

		if (to_write > total_written)
			to_write = total_written;

		err = ubi_leb_write(gluebi->desc, lnum, buf, offs, to_write);
		if (err)
			break;

		lnum += 1;
		offs = 0;
		total_written -= to_write;
		buf += to_write;
	}

	*retlen = len - total_written;
	return err;
}

/**
 * gluebi_erase - erase operation of emulated MTD devices.
 * @mtd: the MTD device description object
 * @instr: the erase operation description
 *
 * This function calls the erase callback when finishes. Returns zero in case
 * of success and a negative error code in case of failure.
 */
static int gluebi_erase(struct mtd_info *mtd, struct erase_info *instr)
{
	int err, i, lnum, count;
	struct gluebi_device *gluebi;

	if (mtd_mod_by_ws(instr->addr, mtd) || mtd_mod_by_ws(instr->len, mtd))
		return -EINVAL;

	lnum = mtd_div_by_eb(instr->addr, mtd);
	count = mtd_div_by_eb(instr->len, mtd);
	gluebi = container_of(mtd, struct gluebi_device, mtd);

	for (i = 0; i < count - 1; i++) {
		err = ubi_leb_unmap(gluebi->desc, lnum + i);
		if (err)
			goto out_err;
	}
	/*
	 * MTD erase operations are synchronous, so we have to make sure the
	 * physical eraseblock is wiped out.
	 *
	 * Thus, perform leb_erase instead of leb_unmap operation - leb_erase
	 * will wait for the end of operations
	 */
	err = ubi_leb_erase(gluebi->desc, lnum + i);
	if (err)
		goto out_err;

	instr->state = MTD_ERASE_DONE;
	mtd_erase_callback(instr);
	return 0;

out_err:
	instr->state = MTD_ERASE_FAILED;
	instr->fail_addr = (long long)lnum * mtd->erasesize;
	return err;
}

/**
 * gluebi_create - create a gluebi device for an UBI volume.
 * @di: UBI device description object
 * @vi: UBI volume description object
 *
 * This function is called when a new UBI volume is created in order to create
 * corresponding fake MTD device. Returns zero in case of success and a
 * negative error code in case of failure.
 */
static int gluebi_create(struct ubi_device_info *di,
			 struct ubi_volume_info *vi)
{
	struct gluebi_device *gluebi, *g;
	struct mtd_info *mtd;

	gluebi = kzalloc(sizeof(struct gluebi_device), GFP_KERNEL);
	if (!gluebi)
		return -ENOMEM;

	mtd = &gluebi->mtd;
	mtd->name = kmemdup(vi->name, vi->name_len + 1, GFP_KERNEL);
	if (!mtd->name) {
		kfree(gluebi);
		return -ENOMEM;
	}

	gluebi->vol_id = vi->vol_id;
	gluebi->ubi_num = vi->ubi_num;
	mtd->type = MTD_UBIVOLUME;
	if (!di->ro_mode)
		mtd->flags = MTD_WRITEABLE;
	mtd->owner      = THIS_MODULE;
	mtd->writesize  = di->min_io_size;
	mtd->erasesize  = vi->usable_leb_size;
	mtd->_read       = gluebi_read;
	mtd->_write      = gluebi_write;
	mtd->_erase      = gluebi_erase;
	mtd->_get_device = gluebi_get_device;
	mtd->_put_device = gluebi_put_device;

	/*
	 * In case of dynamic a volume, MTD device size is just volume size. In
	 * case of a static volume the size is equivalent to the amount of data
	 * bytes.
	 */
	if (vi->vol_type == UBI_DYNAMIC_VOLUME)
		mtd->size = (unsigned long long)vi->usable_leb_size * vi->size;
	else
		mtd->size = vi->used_bytes;

	/* Just a sanity check - make sure this gluebi device does not exist */
	mutex_lock(&devices_mutex);
	g = find_gluebi_nolock(vi->ubi_num, vi->vol_id);
	if (g)
		err_msg("gluebi MTD device %d form UBI device %d volume %d already exists",
			g->mtd.index, vi->ubi_num, vi->vol_id);
	mutex_unlock(&devices_mutex);

	if (mtd_device_register(mtd, NULL, 0)) {
		err_msg("cannot add MTD device");
		kfree(mtd->name);
		kfree(gluebi);
		return -ENFILE;
	}

	mutex_lock(&devices_mutex);
	list_add_tail(&gluebi->list, &gluebi_devices);
	mutex_unlock(&devices_mutex);
	return 0;
}

/**
 * gluebi_remove - remove a gluebi device.
 * @vi: UBI volume description object
 *
 * This function is called when an UBI volume is removed and it removes
 * corresponding fake MTD device. Returns zero in case of success and a
 * negative error code in case of failure.
 */
static int gluebi_remove(struct ubi_volume_info *vi)
{
	int err = 0;
	struct mtd_info *mtd;
	struct gluebi_device *gluebi;

	mutex_lock(&devices_mutex);
	gluebi = find_gluebi_nolock(vi->ubi_num, vi->vol_id);
	if (!gluebi) {
		err_msg("got remove notification for unknown UBI device %d volume %d",
			vi->ubi_num, vi->vol_id);
		err = -ENOENT;
	} else if (gluebi->refcnt)
		err = -EBUSY;
	else
		list_del(&gluebi->list);
	mutex_unlock(&devices_mutex);
	if (err)
		return err;

	mtd = &gluebi->mtd;
	err = mtd_device_unregister(mtd);
	if (err) {
		err_msg("cannot remove fake MTD device %d, UBI device %d, volume %d, error %d",
			mtd->index, gluebi->ubi_num, gluebi->vol_id, err);
		mutex_lock(&devices_mutex);
		list_add_tail(&gluebi->list, &gluebi_devices);
		mutex_unlock(&devices_mutex);
		return err;
	}

	kfree(mtd->name);
	kfree(gluebi);
	return 0;
}

/**
 * gluebi_updated - UBI volume was updated notifier.
 * @vi: volume info structure
 *
 * This function is called every time an UBI volume is updated. It does nothing
 * if te volume @vol is dynamic, and changes MTD device size if the
 * volume is static. This is needed because static volumes cannot be read past
 * data they contain. This function returns zero in case of success and a
 * negative error code in case of error.
 */
static int gluebi_updated(struct ubi_volume_info *vi)
{
	struct gluebi_device *gluebi;

	mutex_lock(&devices_mutex);
	gluebi = find_gluebi_nolock(vi->ubi_num, vi->vol_id);
	if (!gluebi) {
		mutex_unlock(&devices_mutex);
		err_msg("got update notification for unknown UBI device %d volume %d",
			vi->ubi_num, vi->vol_id);
		return -ENOENT;
	}

	if (vi->vol_type == UBI_STATIC_VOLUME)
		gluebi->mtd.size = vi->used_bytes;
	mutex_unlock(&devices_mutex);
	return 0;
}

/**
 * gluebi_resized - UBI volume was re-sized notifier.
 * @vi: volume info structure
 *
 * This function is called every time an UBI volume is re-size. It changes the
 * corresponding fake MTD device size. This function returns zero in case of
 * success and a negative error code in case of error.
 */
static int gluebi_resized(struct ubi_volume_info *vi)
{
	struct gluebi_device *gluebi;

	mutex_lock(&devices_mutex);
	gluebi = find_gluebi_nolock(vi->ubi_num, vi->vol_id);
	if (!gluebi) {
		mutex_unlock(&devices_mutex);
		err_msg("got update notification for unknown UBI device %d volume %d",
			vi->ubi_num, vi->vol_id);
		return -ENOENT;
	}
	gluebi->mtd.size = vi->used_bytes;
	mutex_unlock(&devices_mutex);
	return 0;
}

/**
 * gluebi_notify - UBI notification handler.
 * @nb: registered notifier block
 * @l: notification type
 * @ptr: pointer to the &struct ubi_notification object
 */
static int gluebi_notify(struct notifier_block *nb, unsigned long l,
			 void *ns_ptr)
{
	struct ubi_notification *nt = ns_ptr;

	switch (l) {
	case UBI_VOLUME_ADDED:
		gluebi_create(&nt->di, &nt->vi);
		break;
	case UBI_VOLUME_REMOVED:
		gluebi_remove(&nt->vi);
		break;
	case UBI_VOLUME_RESIZED:
		gluebi_resized(&nt->vi);
		break;
	case UBI_VOLUME_UPDATED:
		gluebi_updated(&nt->vi);
		break;
	default:
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block gluebi_notifier = {
	.notifier_call	= gluebi_notify,
};

static int __init ubi_gluebi_init(void)
{
	return ubi_register_volume_notifier(&gluebi_notifier, 0);
}

static void __exit ubi_gluebi_exit(void)
{
	struct gluebi_device *gluebi, *g;

	list_for_each_entry_safe(gluebi, g, &gluebi_devices, list) {
		int err;
		struct mtd_info *mtd = &gluebi->mtd;

		err = mtd_device_unregister(mtd);
		if (err)
			err_msg("error %d while removing gluebi MTD device %d, UBI device %d, volume %d - ignoring",
				err, mtd->index, gluebi->ubi_num,
				gluebi->vol_id);
		kfree(mtd->name);
		kfree(gluebi);
	}
	ubi_unregister_volume_notifier(&gluebi_notifier);
}

module_init(ubi_gluebi_init);
module_exit(ubi_gluebi_exit);
MODULE_DESCRIPTION("MTD emulation layer over UBI volumes");
MODULE_AUTHOR("Artem Bityutskiy, Joern Engel");
MODULE_LICENSE("GPL");
