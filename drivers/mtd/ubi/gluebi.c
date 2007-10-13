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
 * This file includes implementation of fake MTD devices for each UBI volume.
 * This sounds strange, but it is in fact quite useful to make MTD-oriented
 * software (including all the legacy software) to work on top of UBI.
 *
 * Gluebi emulates MTD devices of "MTD_UBIVOLUME" type. Their minimal I/O unit
 * size (mtd->writesize) is equivalent to the UBI minimal I/O unit. The
 * eraseblock size is equivalent to the logical eraseblock size of the volume.
 */

#include <asm/div64.h>
#include "ubi.h"

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
	struct ubi_volume *vol;

	vol = container_of(mtd, struct ubi_volume, gluebi_mtd);

	/*
	 * We do not introduce locks for gluebi reference count because the
	 * get_device()/put_device() calls are already serialized at MTD.
	 */
	if (vol->gluebi_refcount > 0) {
		/*
		 * The MTD device is already referenced and this is just one
		 * more reference. MTD allows many users to open the same
		 * volume simultaneously and do not distinguish between
		 * readers/writers/exclusive openers as UBI does. So we do not
		 * open the UBI volume again - just increase the reference
		 * counter and return.
		 */
		vol->gluebi_refcount += 1;
		return 0;
	}

	/*
	 * This is the first reference to this UBI volume via the MTD device
	 * interface. Open the corresponding volume in read-write mode.
	 */
	vol->gluebi_desc = ubi_open_volume(vol->ubi->ubi_num, vol->vol_id,
					   UBI_READWRITE);
	if (IS_ERR(vol->gluebi_desc))
		return PTR_ERR(vol->gluebi_desc);
	vol->gluebi_refcount += 1;
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
	struct ubi_volume *vol;

	vol = container_of(mtd, struct ubi_volume, gluebi_mtd);
	vol->gluebi_refcount -= 1;
	ubi_assert(vol->gluebi_refcount >= 0);
	if (vol->gluebi_refcount == 0)
		ubi_close_volume(vol->gluebi_desc);
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
	struct ubi_volume *vol;
	struct ubi_device *ubi;
	uint64_t tmp = from;

	dbg_msg("read %zd bytes from offset %lld", len, from);

	if (len < 0 || from < 0 || from + len > mtd->size)
		return -EINVAL;

	vol = container_of(mtd, struct ubi_volume, gluebi_mtd);
	ubi = vol->ubi;

	offs = do_div(tmp, mtd->erasesize);
	lnum = tmp;

	total_read = len;
	while (total_read) {
		size_t to_read = mtd->erasesize - offs;

		if (to_read > total_read)
			to_read = total_read;

		err = ubi_eba_read_leb(ubi, vol->vol_id, lnum, buf, offs,
				       to_read, 0);
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
	struct ubi_volume *vol;
	struct ubi_device *ubi;
	uint64_t tmp = to;

	dbg_msg("write %zd bytes to offset %lld", len, to);

	if (len < 0 || to < 0 || len + to > mtd->size)
		return -EINVAL;

	vol = container_of(mtd, struct ubi_volume, gluebi_mtd);
	ubi = vol->ubi;

	if (ubi->ro_mode)
		return -EROFS;

	offs = do_div(tmp, mtd->erasesize);
	lnum = tmp;

	if (len % mtd->writesize || offs % mtd->writesize)
		return -EINVAL;

	total_written = len;
	while (total_written) {
		size_t to_write = mtd->erasesize - offs;

		if (to_write > total_written)
			to_write = total_written;

		err = ubi_eba_write_leb(ubi, vol->vol_id, lnum, buf, offs,
					to_write, UBI_UNKNOWN);
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
	struct ubi_volume *vol;
	struct ubi_device *ubi;

	dbg_msg("erase %u bytes at offset %u", instr->len, instr->addr);

	if (instr->addr < 0 || instr->addr > mtd->size - mtd->erasesize)
		return -EINVAL;

	if (instr->len < 0 || instr->addr + instr->len > mtd->size)
		return -EINVAL;

	if (instr->addr % mtd->writesize || instr->len % mtd->writesize)
		return -EINVAL;

	lnum = instr->addr / mtd->erasesize;
	count = instr->len / mtd->erasesize;

	vol = container_of(mtd, struct ubi_volume, gluebi_mtd);
	ubi = vol->ubi;

	if (ubi->ro_mode)
		return -EROFS;

	for (i = 0; i < count; i++) {
		err = ubi_eba_unmap_leb(ubi, vol->vol_id, lnum + i);
		if (err)
			goto out_err;
	}

	/*
	 * MTD erase operations are synchronous, so we have to make sure the
	 * physical eraseblock is wiped out.
	 */
	err = ubi_wl_flush(ubi);
	if (err)
		goto out_err;

        instr->state = MTD_ERASE_DONE;
        mtd_erase_callback(instr);
	return 0;

out_err:
	instr->state = MTD_ERASE_FAILED;
	instr->fail_addr = lnum * mtd->erasesize;
	return err;
}

/**
 * ubi_create_gluebi - initialize gluebi for an UBI volume.
 * @ubi: UBI device description object
 * @vol: volume description object
 *
 * This function is called when an UBI volume is created in order to create
 * corresponding fake MTD device. Returns zero in case of success and a
 * negative error code in case of failure.
 */
int ubi_create_gluebi(struct ubi_device *ubi, struct ubi_volume *vol)
{
	struct mtd_info *mtd = &vol->gluebi_mtd;

	mtd->name = kmemdup(vol->name, vol->name_len + 1, GFP_KERNEL);
	if (!mtd->name)
		return -ENOMEM;

	mtd->type = MTD_UBIVOLUME;
	if (!ubi->ro_mode)
		mtd->flags = MTD_WRITEABLE;
	mtd->writesize  = ubi->min_io_size;
	mtd->owner      = THIS_MODULE;
	mtd->erasesize  = vol->usable_leb_size;
	mtd->read       = gluebi_read;
	mtd->write      = gluebi_write;
	mtd->erase      = gluebi_erase;
	mtd->get_device = gluebi_get_device;
	mtd->put_device = gluebi_put_device;

	/*
	 * In case of dynamic volume, MTD device size is just volume size. In
	 * case of a static volume the size is equivalent to the amount of data
	 * bytes, which is zero at this moment and will be changed after volume
	 * update.
	 */
	if (vol->vol_type == UBI_DYNAMIC_VOLUME)
		mtd->size = vol->usable_leb_size * vol->reserved_pebs;

	if (add_mtd_device(mtd)) {
		ubi_err("cannot not add MTD device\n");
		kfree(mtd->name);
		return -ENFILE;
	}

	dbg_msg("added mtd%d (\"%s\"), size %u, EB size %u",
		mtd->index, mtd->name, mtd->size, mtd->erasesize);
	return 0;
}

/**
 * ubi_destroy_gluebi - close gluebi for an UBI volume.
 * @vol: volume description object
 *
 * This function is called when an UBI volume is removed in order to remove
 * corresponding fake MTD device. Returns zero in case of success and a
 * negative error code in case of failure.
 */
int ubi_destroy_gluebi(struct ubi_volume *vol)
{
	int err;
	struct mtd_info *mtd = &vol->gluebi_mtd;

	dbg_msg("remove mtd%d", mtd->index);
	err = del_mtd_device(mtd);
	if (err)
		return err;
	kfree(mtd->name);
	return 0;
}

/**
 * ubi_gluebi_updated - UBI volume was updated notifier.
 * @vol: volume description object
 *
 * This function is called every time an UBI volume is updated. This function
 * does nothing if volume @vol is dynamic, and changes MTD device size if the
 * volume is static. This is needed because static volumes cannot be read past
 * data they contain.
 */
void ubi_gluebi_updated(struct ubi_volume *vol)
{
	struct mtd_info *mtd = &vol->gluebi_mtd;

	if (vol->vol_type == UBI_STATIC_VOLUME)
		mtd->size = vol->used_bytes;
}
