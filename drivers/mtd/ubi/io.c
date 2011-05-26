/*
 * Copyright (c) International Business Machines Corp., 2006
 * Copyright (c) Nokia Corporation, 2006, 2007
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
 * Author: Artem Bityutskiy (Битюцкий Артём)
 */

/*
 * UBI input/output sub-system.
 *
 * This sub-system provides a uniform way to work with all kinds of the
 * underlying MTD devices. It also implements handy functions for reading and
 * writing UBI headers.
 *
 * We are trying to have a paranoid mindset and not to trust to what we read
 * from the flash media in order to be more secure and robust. So this
 * sub-system validates every single header it reads from the flash media.
 *
 * Some words about how the eraseblock headers are stored.
 *
 * The erase counter header is always stored at offset zero. By default, the
 * VID header is stored after the EC header at the closest aligned offset
 * (i.e. aligned to the minimum I/O unit size). Data starts next to the VID
 * header at the closest aligned offset. But this default layout may be
 * changed. For example, for different reasons (e.g., optimization) UBI may be
 * asked to put the VID header at further offset, and even at an unaligned
 * offset. Of course, if the offset of the VID header is unaligned, UBI adds
 * proper padding in front of it. Data offset may also be changed but it has to
 * be aligned.
 *
 * About minimal I/O units. In general, UBI assumes flash device model where
 * there is only one minimal I/O unit size. E.g., in case of NOR flash it is 1,
 * in case of NAND flash it is a NAND page, etc. This is reported by MTD in the
 * @ubi->mtd->writesize field. But as an exception, UBI admits of using another
 * (smaller) minimal I/O unit size for EC and VID headers to make it possible
 * to do different optimizations.
 *
 * This is extremely useful in case of NAND flashes which admit of several
 * write operations to one NAND page. In this case UBI can fit EC and VID
 * headers at one NAND page. Thus, UBI may use "sub-page" size as the minimal
 * I/O unit for the headers (the @ubi->hdrs_min_io_size field). But it still
 * reports NAND page size (@ubi->min_io_size) as a minimal I/O unit for the UBI
 * users.
 *
 * Example: some Samsung NANDs with 2KiB pages allow 4x 512-byte writes, so
 * although the minimal I/O unit is 2K, UBI uses 512 bytes for EC and VID
 * headers.
 *
 * Q: why not just to treat sub-page as a minimal I/O unit of this flash
 * device, e.g., make @ubi->min_io_size = 512 in the example above?
 *
 * A: because when writing a sub-page, MTD still writes a full 2K page but the
 * bytes which are not relevant to the sub-page are 0xFF. So, basically,
 * writing 4x512 sub-pages is 4 times slower than writing one 2KiB NAND page.
 * Thus, we prefer to use sub-pages only for EC and VID headers.
 *
 * As it was noted above, the VID header may start at a non-aligned offset.
 * For example, in case of a 2KiB page NAND flash with a 512 bytes sub-page,
 * the VID header may reside at offset 1984 which is the last 64 bytes of the
 * last sub-page (EC header is always at offset zero). This causes some
 * difficulties when reading and writing VID headers.
 *
 * Suppose we have a 64-byte buffer and we read a VID header at it. We change
 * the data and want to write this VID header out. As we can only write in
 * 512-byte chunks, we have to allocate one more buffer and copy our VID header
 * to offset 448 of this buffer.
 *
 * The I/O sub-system does the following trick in order to avoid this extra
 * copy. It always allocates a @ubi->vid_hdr_alsize bytes buffer for the VID
 * header and returns a pointer to offset @ubi->vid_hdr_shift of this buffer.
 * When the VID header is being written out, it shifts the VID header pointer
 * back and writes the whole sub-page.
 */

#include <linux/crc32.h>
#include <linux/err.h>
#include <linux/slab.h>
#include "ubi.h"

#ifdef CONFIG_MTD_UBI_DEBUG
static int paranoid_check_not_bad(const struct ubi_device *ubi, int pnum);
static int paranoid_check_peb_ec_hdr(const struct ubi_device *ubi, int pnum);
static int paranoid_check_ec_hdr(const struct ubi_device *ubi, int pnum,
				 const struct ubi_ec_hdr *ec_hdr);
static int paranoid_check_peb_vid_hdr(const struct ubi_device *ubi, int pnum);
static int paranoid_check_vid_hdr(const struct ubi_device *ubi, int pnum,
				  const struct ubi_vid_hdr *vid_hdr);
#else
#define paranoid_check_not_bad(ubi, pnum) 0
#define paranoid_check_peb_ec_hdr(ubi, pnum)  0
#define paranoid_check_ec_hdr(ubi, pnum, ec_hdr)  0
#define paranoid_check_peb_vid_hdr(ubi, pnum) 0
#define paranoid_check_vid_hdr(ubi, pnum, vid_hdr) 0
#endif

/**
 * ubi_io_read - read data from a physical eraseblock.
 * @ubi: UBI device description object
 * @buf: buffer where to store the read data
 * @pnum: physical eraseblock number to read from
 * @offset: offset within the physical eraseblock from where to read
 * @len: how many bytes to read
 *
 * This function reads data from offset @offset of physical eraseblock @pnum
 * and stores the read data in the @buf buffer. The following return codes are
 * possible:
 *
 * o %0 if all the requested data were successfully read;
 * o %UBI_IO_BITFLIPS if all the requested data were successfully read, but
 *   correctable bit-flips were detected; this is harmless but may indicate
 *   that this eraseblock may become bad soon (but do not have to);
 * o %-EBADMSG if the MTD subsystem reported about data integrity problems, for
 *   example it can be an ECC error in case of NAND; this most probably means
 *   that the data is corrupted;
 * o %-EIO if some I/O error occurred;
 * o other negative error codes in case of other errors.
 */
int ubi_io_read(const struct ubi_device *ubi, void *buf, int pnum, int offset,
		int len)
{
	int err, retries = 0;
	size_t read;
	loff_t addr;

	dbg_io("read %d bytes from PEB %d:%d", len, pnum, offset);

	ubi_assert(pnum >= 0 && pnum < ubi->peb_count);
	ubi_assert(offset >= 0 && offset + len <= ubi->peb_size);
	ubi_assert(len > 0);

	err = paranoid_check_not_bad(ubi, pnum);
	if (err)
		return err;

	/*
	 * Deliberately corrupt the buffer to improve robustness. Indeed, if we
	 * do not do this, the following may happen:
	 * 1. The buffer contains data from previous operation, e.g., read from
	 *    another PEB previously. The data looks like expected, e.g., if we
	 *    just do not read anything and return - the caller would not
	 *    notice this. E.g., if we are reading a VID header, the buffer may
	 *    contain a valid VID header from another PEB.
	 * 2. The driver is buggy and returns us success or -EBADMSG or
	 *    -EUCLEAN, but it does not actually put any data to the buffer.
	 *
	 * This may confuse UBI or upper layers - they may think the buffer
	 * contains valid data while in fact it is just old data. This is
	 * especially possible because UBI (and UBIFS) relies on CRC, and
	 * treats data as correct even in case of ECC errors if the CRC is
	 * correct.
	 *
	 * Try to prevent this situation by changing the first byte of the
	 * buffer.
	 */
	*((uint8_t *)buf) ^= 0xFF;

	addr = (loff_t)pnum * ubi->peb_size + offset;
retry:
	err = ubi->mtd->read(ubi->mtd, addr, len, &read, buf);
	if (err) {
		const char *errstr = (err == -EBADMSG) ? " (ECC error)" : "";

		if (err == -EUCLEAN) {
			/*
			 * -EUCLEAN is reported if there was a bit-flip which
			 * was corrected, so this is harmless.
			 *
			 * We do not report about it here unless debugging is
			 * enabled. A corresponding message will be printed
			 * later, when it is has been scrubbed.
			 */
			dbg_msg("fixable bit-flip detected at PEB %d", pnum);
			ubi_assert(len == read);
			return UBI_IO_BITFLIPS;
		}

		if (retries++ < UBI_IO_RETRIES) {
			dbg_io("error %d%s while reading %d bytes from PEB "
			       "%d:%d, read only %zd bytes, retry",
			       err, errstr, len, pnum, offset, read);
			yield();
			goto retry;
		}

		ubi_err("error %d%s while reading %d bytes from PEB %d:%d, "
			"read %zd bytes", err, errstr, len, pnum, offset, read);
		ubi_dbg_dump_stack();

		/*
		 * The driver should never return -EBADMSG if it failed to read
		 * all the requested data. But some buggy drivers might do
		 * this, so we change it to -EIO.
		 */
		if (read != len && err == -EBADMSG) {
			ubi_assert(0);
			err = -EIO;
		}
	} else {
		ubi_assert(len == read);

		if (ubi_dbg_is_bitflip()) {
			dbg_gen("bit-flip (emulated)");
			err = UBI_IO_BITFLIPS;
		}
	}

	return err;
}

/**
 * ubi_io_write - write data to a physical eraseblock.
 * @ubi: UBI device description object
 * @buf: buffer with the data to write
 * @pnum: physical eraseblock number to write to
 * @offset: offset within the physical eraseblock where to write
 * @len: how many bytes to write
 *
 * This function writes @len bytes of data from buffer @buf to offset @offset
 * of physical eraseblock @pnum. If all the data were successfully written,
 * zero is returned. If an error occurred, this function returns a negative
 * error code. If %-EIO is returned, the physical eraseblock most probably went
 * bad.
 *
 * Note, in case of an error, it is possible that something was still written
 * to the flash media, but may be some garbage.
 */
int ubi_io_write(struct ubi_device *ubi, const void *buf, int pnum, int offset,
		 int len)
{
	int err;
	size_t written;
	loff_t addr;

	dbg_io("write %d bytes to PEB %d:%d", len, pnum, offset);

	ubi_assert(pnum >= 0 && pnum < ubi->peb_count);
	ubi_assert(offset >= 0 && offset + len <= ubi->peb_size);
	ubi_assert(offset % ubi->hdrs_min_io_size == 0);
	ubi_assert(len > 0 && len % ubi->hdrs_min_io_size == 0);

	if (ubi->ro_mode) {
		ubi_err("read-only mode");
		return -EROFS;
	}

	/* The below has to be compiled out if paranoid checks are disabled */

	err = paranoid_check_not_bad(ubi, pnum);
	if (err)
		return err;

	/* The area we are writing to has to contain all 0xFF bytes */
	err = ubi_dbg_check_all_ff(ubi, pnum, offset, len);
	if (err)
		return err;

	if (offset >= ubi->leb_start) {
		/*
		 * We write to the data area of the physical eraseblock. Make
		 * sure it has valid EC and VID headers.
		 */
		err = paranoid_check_peb_ec_hdr(ubi, pnum);
		if (err)
			return err;
		err = paranoid_check_peb_vid_hdr(ubi, pnum);
		if (err)
			return err;
	}

	if (ubi_dbg_is_write_failure()) {
		dbg_err("cannot write %d bytes to PEB %d:%d "
			"(emulated)", len, pnum, offset);
		ubi_dbg_dump_stack();
		return -EIO;
	}

	addr = (loff_t)pnum * ubi->peb_size + offset;
	err = ubi->mtd->write(ubi->mtd, addr, len, &written, buf);
	if (err) {
		ubi_err("error %d while writing %d bytes to PEB %d:%d, written "
			"%zd bytes", err, len, pnum, offset, written);
		ubi_dbg_dump_stack();
		ubi_dbg_dump_flash(ubi, pnum, offset, len);
	} else
		ubi_assert(written == len);

	if (!err) {
		err = ubi_dbg_check_write(ubi, buf, pnum, offset, len);
		if (err)
			return err;

		/*
		 * Since we always write sequentially, the rest of the PEB has
		 * to contain only 0xFF bytes.
		 */
		offset += len;
		len = ubi->peb_size - offset;
		if (len)
			err = ubi_dbg_check_all_ff(ubi, pnum, offset, len);
	}

	return err;
}

/**
 * erase_callback - MTD erasure call-back.
 * @ei: MTD erase information object.
 *
 * Note, even though MTD erase interface is asynchronous, all the current
 * implementations are synchronous anyway.
 */
static void erase_callback(struct erase_info *ei)
{
	wake_up_interruptible((wait_queue_head_t *)ei->priv);
}

/**
 * do_sync_erase - synchronously erase a physical eraseblock.
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock number to erase
 *
 * This function synchronously erases physical eraseblock @pnum and returns
 * zero in case of success and a negative error code in case of failure. If
 * %-EIO is returned, the physical eraseblock most probably went bad.
 */
static int do_sync_erase(struct ubi_device *ubi, int pnum)
{
	int err, retries = 0;
	struct erase_info ei;
	wait_queue_head_t wq;

	dbg_io("erase PEB %d", pnum);
	ubi_assert(pnum >= 0 && pnum < ubi->peb_count);

	if (ubi->ro_mode) {
		ubi_err("read-only mode");
		return -EROFS;
	}

retry:
	init_waitqueue_head(&wq);
	memset(&ei, 0, sizeof(struct erase_info));

	ei.mtd      = ubi->mtd;
	ei.addr     = (loff_t)pnum * ubi->peb_size;
	ei.len      = ubi->peb_size;
	ei.callback = erase_callback;
	ei.priv     = (unsigned long)&wq;

	err = ubi->mtd->erase(ubi->mtd, &ei);
	if (err) {
		if (retries++ < UBI_IO_RETRIES) {
			dbg_io("error %d while erasing PEB %d, retry",
			       err, pnum);
			yield();
			goto retry;
		}
		ubi_err("cannot erase PEB %d, error %d", pnum, err);
		ubi_dbg_dump_stack();
		return err;
	}

	err = wait_event_interruptible(wq, ei.state == MTD_ERASE_DONE ||
					   ei.state == MTD_ERASE_FAILED);
	if (err) {
		ubi_err("interrupted PEB %d erasure", pnum);
		return -EINTR;
	}

	if (ei.state == MTD_ERASE_FAILED) {
		if (retries++ < UBI_IO_RETRIES) {
			dbg_io("error while erasing PEB %d, retry", pnum);
			yield();
			goto retry;
		}
		ubi_err("cannot erase PEB %d", pnum);
		ubi_dbg_dump_stack();
		return -EIO;
	}

	err = ubi_dbg_check_all_ff(ubi, pnum, 0, ubi->peb_size);
	if (err)
		return err;

	if (ubi_dbg_is_erase_failure()) {
		dbg_err("cannot erase PEB %d (emulated)", pnum);
		return -EIO;
	}

	return 0;
}

/* Patterns to write to a physical eraseblock when torturing it */
static uint8_t patterns[] = {0xa5, 0x5a, 0x0};

/**
 * torture_peb - test a supposedly bad physical eraseblock.
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock number to test
 *
 * This function returns %-EIO if the physical eraseblock did not pass the
 * test, a positive number of erase operations done if the test was
 * successfully passed, and other negative error codes in case of other errors.
 */
static int torture_peb(struct ubi_device *ubi, int pnum)
{
	int err, i, patt_count;

	ubi_msg("run torture test for PEB %d", pnum);
	patt_count = ARRAY_SIZE(patterns);
	ubi_assert(patt_count > 0);

	mutex_lock(&ubi->buf_mutex);
	for (i = 0; i < patt_count; i++) {
		err = do_sync_erase(ubi, pnum);
		if (err)
			goto out;

		/* Make sure the PEB contains only 0xFF bytes */
		err = ubi_io_read(ubi, ubi->peb_buf1, pnum, 0, ubi->peb_size);
		if (err)
			goto out;

		err = ubi_check_pattern(ubi->peb_buf1, 0xFF, ubi->peb_size);
		if (err == 0) {
			ubi_err("erased PEB %d, but a non-0xFF byte found",
				pnum);
			err = -EIO;
			goto out;
		}

		/* Write a pattern and check it */
		memset(ubi->peb_buf1, patterns[i], ubi->peb_size);
		err = ubi_io_write(ubi, ubi->peb_buf1, pnum, 0, ubi->peb_size);
		if (err)
			goto out;

		memset(ubi->peb_buf1, ~patterns[i], ubi->peb_size);
		err = ubi_io_read(ubi, ubi->peb_buf1, pnum, 0, ubi->peb_size);
		if (err)
			goto out;

		err = ubi_check_pattern(ubi->peb_buf1, patterns[i],
					ubi->peb_size);
		if (err == 0) {
			ubi_err("pattern %x checking failed for PEB %d",
				patterns[i], pnum);
			err = -EIO;
			goto out;
		}
	}

	err = patt_count;
	ubi_msg("PEB %d passed torture test, do not mark it as bad", pnum);

out:
	mutex_unlock(&ubi->buf_mutex);
	if (err == UBI_IO_BITFLIPS || err == -EBADMSG) {
		/*
		 * If a bit-flip or data integrity error was detected, the test
		 * has not passed because it happened on a freshly erased
		 * physical eraseblock which means something is wrong with it.
		 */
		ubi_err("read problems on freshly erased PEB %d, must be bad",
			pnum);
		err = -EIO;
	}
	return err;
}

/**
 * nor_erase_prepare - prepare a NOR flash PEB for erasure.
 * @ubi: UBI device description object
 * @pnum: physical eraseblock number to prepare
 *
 * NOR flash, or at least some of them, have peculiar embedded PEB erasure
 * algorithm: the PEB is first filled with zeroes, then it is erased. And
 * filling with zeroes starts from the end of the PEB. This was observed with
 * Spansion S29GL512N NOR flash.
 *
 * This means that in case of a power cut we may end up with intact data at the
 * beginning of the PEB, and all zeroes at the end of PEB. In other words, the
 * EC and VID headers are OK, but a large chunk of data at the end of PEB is
 * zeroed. This makes UBI mistakenly treat this PEB as used and associate it
 * with an LEB, which leads to subsequent failures (e.g., UBIFS fails).
 *
 * This function is called before erasing NOR PEBs and it zeroes out EC and VID
 * magic numbers in order to invalidate them and prevent the failures. Returns
 * zero in case of success and a negative error code in case of failure.
 */
static int nor_erase_prepare(struct ubi_device *ubi, int pnum)
{
	int err, err1;
	size_t written;
	loff_t addr;
	uint32_t data = 0;
	/*
	 * Note, we cannot generally define VID header buffers on stack,
	 * because of the way we deal with these buffers (see the header
	 * comment in this file). But we know this is a NOR-specific piece of
	 * code, so we can do this. But yes, this is error-prone and we should
	 * (pre-)allocate VID header buffer instead.
	 */
	struct ubi_vid_hdr vid_hdr;

	/*
	 * It is important to first invalidate the EC header, and then the VID
	 * header. Otherwise a power cut may lead to valid EC header and
	 * invalid VID header, in which case UBI will treat this PEB as
	 * corrupted and will try to preserve it, and print scary warnings (see
	 * the header comment in scan.c for more information).
	 */
	addr = (loff_t)pnum * ubi->peb_size;
	err = ubi->mtd->write(ubi->mtd, addr, 4, &written, (void *)&data);
	if (!err) {
		addr += ubi->vid_hdr_aloffset;
		err = ubi->mtd->write(ubi->mtd, addr, 4, &written,
				      (void *)&data);
		if (!err)
			return 0;
	}

	/*
	 * We failed to write to the media. This was observed with Spansion
	 * S29GL512N NOR flash. Most probably the previously eraseblock erasure
	 * was interrupted at a very inappropriate moment, so it became
	 * unwritable. In this case we probably anyway have garbage in this
	 * PEB.
	 */
	err1 = ubi_io_read_vid_hdr(ubi, pnum, &vid_hdr, 0);
	if (err1 == UBI_IO_BAD_HDR_EBADMSG || err1 == UBI_IO_BAD_HDR ||
	    err1 == UBI_IO_FF) {
		struct ubi_ec_hdr ec_hdr;

		err1 = ubi_io_read_ec_hdr(ubi, pnum, &ec_hdr, 0);
		if (err1 == UBI_IO_BAD_HDR_EBADMSG || err1 == UBI_IO_BAD_HDR ||
		    err1 == UBI_IO_FF)
			/*
			 * Both VID and EC headers are corrupted, so we can
			 * safely erase this PEB and not afraid that it will be
			 * treated as a valid PEB in case of an unclean reboot.
			 */
			return 0;
	}

	/*
	 * The PEB contains a valid VID header, but we cannot invalidate it.
	 * Supposedly the flash media or the driver is screwed up, so return an
	 * error.
	 */
	ubi_err("cannot invalidate PEB %d, write returned %d read returned %d",
		pnum, err, err1);
	ubi_dbg_dump_flash(ubi, pnum, 0, ubi->peb_size);
	return -EIO;
}

/**
 * ubi_io_sync_erase - synchronously erase a physical eraseblock.
 * @ubi: UBI device description object
 * @pnum: physical eraseblock number to erase
 * @torture: if this physical eraseblock has to be tortured
 *
 * This function synchronously erases physical eraseblock @pnum. If @torture
 * flag is not zero, the physical eraseblock is checked by means of writing
 * different patterns to it and reading them back. If the torturing is enabled,
 * the physical eraseblock is erased more than once.
 *
 * This function returns the number of erasures made in case of success, %-EIO
 * if the erasure failed or the torturing test failed, and other negative error
 * codes in case of other errors. Note, %-EIO means that the physical
 * eraseblock is bad.
 */
int ubi_io_sync_erase(struct ubi_device *ubi, int pnum, int torture)
{
	int err, ret = 0;

	ubi_assert(pnum >= 0 && pnum < ubi->peb_count);

	err = paranoid_check_not_bad(ubi, pnum);
	if (err != 0)
		return err;

	if (ubi->ro_mode) {
		ubi_err("read-only mode");
		return -EROFS;
	}

	if (ubi->nor_flash) {
		err = nor_erase_prepare(ubi, pnum);
		if (err)
			return err;
	}

	if (torture) {
		ret = torture_peb(ubi, pnum);
		if (ret < 0)
			return ret;
	}

	err = do_sync_erase(ubi, pnum);
	if (err)
		return err;

	return ret + 1;
}

/**
 * ubi_io_is_bad - check if a physical eraseblock is bad.
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock number to check
 *
 * This function returns a positive number if the physical eraseblock is bad,
 * zero if not, and a negative error code if an error occurred.
 */
int ubi_io_is_bad(const struct ubi_device *ubi, int pnum)
{
	struct mtd_info *mtd = ubi->mtd;

	ubi_assert(pnum >= 0 && pnum < ubi->peb_count);

	if (ubi->bad_allowed) {
		int ret;

		ret = mtd->block_isbad(mtd, (loff_t)pnum * ubi->peb_size);
		if (ret < 0)
			ubi_err("error %d while checking if PEB %d is bad",
				ret, pnum);
		else if (ret)
			dbg_io("PEB %d is bad", pnum);
		return ret;
	}

	return 0;
}

/**
 * ubi_io_mark_bad - mark a physical eraseblock as bad.
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock number to mark
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure.
 */
int ubi_io_mark_bad(const struct ubi_device *ubi, int pnum)
{
	int err;
	struct mtd_info *mtd = ubi->mtd;

	ubi_assert(pnum >= 0 && pnum < ubi->peb_count);

	if (ubi->ro_mode) {
		ubi_err("read-only mode");
		return -EROFS;
	}

	if (!ubi->bad_allowed)
		return 0;

	err = mtd->block_markbad(mtd, (loff_t)pnum * ubi->peb_size);
	if (err)
		ubi_err("cannot mark PEB %d bad, error %d", pnum, err);
	return err;
}

/**
 * validate_ec_hdr - validate an erase counter header.
 * @ubi: UBI device description object
 * @ec_hdr: the erase counter header to check
 *
 * This function returns zero if the erase counter header is OK, and %1 if
 * not.
 */
static int validate_ec_hdr(const struct ubi_device *ubi,
			   const struct ubi_ec_hdr *ec_hdr)
{
	long long ec;
	int vid_hdr_offset, leb_start;

	ec = be64_to_cpu(ec_hdr->ec);
	vid_hdr_offset = be32_to_cpu(ec_hdr->vid_hdr_offset);
	leb_start = be32_to_cpu(ec_hdr->data_offset);

	if (ec_hdr->version != UBI_VERSION) {
		ubi_err("node with incompatible UBI version found: "
			"this UBI version is %d, image version is %d",
			UBI_VERSION, (int)ec_hdr->version);
		goto bad;
	}

	if (vid_hdr_offset != ubi->vid_hdr_offset) {
		ubi_err("bad VID header offset %d, expected %d",
			vid_hdr_offset, ubi->vid_hdr_offset);
		goto bad;
	}

	if (leb_start != ubi->leb_start) {
		ubi_err("bad data offset %d, expected %d",
			leb_start, ubi->leb_start);
		goto bad;
	}

	if (ec < 0 || ec > UBI_MAX_ERASECOUNTER) {
		ubi_err("bad erase counter %lld", ec);
		goto bad;
	}

	return 0;

bad:
	ubi_err("bad EC header");
	ubi_dbg_dump_ec_hdr(ec_hdr);
	ubi_dbg_dump_stack();
	return 1;
}

/**
 * ubi_io_read_ec_hdr - read and check an erase counter header.
 * @ubi: UBI device description object
 * @pnum: physical eraseblock to read from
 * @ec_hdr: a &struct ubi_ec_hdr object where to store the read erase counter
 * header
 * @verbose: be verbose if the header is corrupted or was not found
 *
 * This function reads erase counter header from physical eraseblock @pnum and
 * stores it in @ec_hdr. This function also checks CRC checksum of the read
 * erase counter header. The following codes may be returned:
 *
 * o %0 if the CRC checksum is correct and the header was successfully read;
 * o %UBI_IO_BITFLIPS if the CRC is correct, but bit-flips were detected
 *   and corrected by the flash driver; this is harmless but may indicate that
 *   this eraseblock may become bad soon (but may be not);
 * o %UBI_IO_BAD_HDR if the erase counter header is corrupted (a CRC error);
 * o %UBI_IO_BAD_HDR_EBADMSG is the same as %UBI_IO_BAD_HDR, but there also was
 *   a data integrity error (uncorrectable ECC error in case of NAND);
 * o %UBI_IO_FF if only 0xFF bytes were read (the PEB is supposedly empty)
 * o a negative error code in case of failure.
 */
int ubi_io_read_ec_hdr(struct ubi_device *ubi, int pnum,
		       struct ubi_ec_hdr *ec_hdr, int verbose)
{
	int err, read_err;
	uint32_t crc, magic, hdr_crc;

	dbg_io("read EC header from PEB %d", pnum);
	ubi_assert(pnum >= 0 && pnum < ubi->peb_count);

	read_err = ubi_io_read(ubi, ec_hdr, pnum, 0, UBI_EC_HDR_SIZE);
	if (read_err) {
		if (read_err != UBI_IO_BITFLIPS && read_err != -EBADMSG)
			return read_err;

		/*
		 * We read all the data, but either a correctable bit-flip
		 * occurred, or MTD reported a data integrity error
		 * (uncorrectable ECC error in case of NAND). The former is
		 * harmless, the later may mean that the read data is
		 * corrupted. But we have a CRC check-sum and we will detect
		 * this. If the EC header is still OK, we just report this as
		 * there was a bit-flip, to force scrubbing.
		 */
	}

	magic = be32_to_cpu(ec_hdr->magic);
	if (magic != UBI_EC_HDR_MAGIC) {
		if (read_err == -EBADMSG)
			return UBI_IO_BAD_HDR_EBADMSG;

		/*
		 * The magic field is wrong. Let's check if we have read all
		 * 0xFF. If yes, this physical eraseblock is assumed to be
		 * empty.
		 */
		if (ubi_check_pattern(ec_hdr, 0xFF, UBI_EC_HDR_SIZE)) {
			/* The physical eraseblock is supposedly empty */
			if (verbose)
				ubi_warn("no EC header found at PEB %d, "
					 "only 0xFF bytes", pnum);
			dbg_bld("no EC header found at PEB %d, "
				"only 0xFF bytes", pnum);
			if (!read_err)
				return UBI_IO_FF;
			else
				return UBI_IO_FF_BITFLIPS;
		}

		/*
		 * This is not a valid erase counter header, and these are not
		 * 0xFF bytes. Report that the header is corrupted.
		 */
		if (verbose) {
			ubi_warn("bad magic number at PEB %d: %08x instead of "
				 "%08x", pnum, magic, UBI_EC_HDR_MAGIC);
			ubi_dbg_dump_ec_hdr(ec_hdr);
		}
		dbg_bld("bad magic number at PEB %d: %08x instead of "
			"%08x", pnum, magic, UBI_EC_HDR_MAGIC);
		return UBI_IO_BAD_HDR;
	}

	crc = crc32(UBI_CRC32_INIT, ec_hdr, UBI_EC_HDR_SIZE_CRC);
	hdr_crc = be32_to_cpu(ec_hdr->hdr_crc);

	if (hdr_crc != crc) {
		if (verbose) {
			ubi_warn("bad EC header CRC at PEB %d, calculated "
				 "%#08x, read %#08x", pnum, crc, hdr_crc);
			ubi_dbg_dump_ec_hdr(ec_hdr);
		}
		dbg_bld("bad EC header CRC at PEB %d, calculated "
			"%#08x, read %#08x", pnum, crc, hdr_crc);

		if (!read_err)
			return UBI_IO_BAD_HDR;
		else
			return UBI_IO_BAD_HDR_EBADMSG;
	}

	/* And of course validate what has just been read from the media */
	err = validate_ec_hdr(ubi, ec_hdr);
	if (err) {
		ubi_err("validation failed for PEB %d", pnum);
		return -EINVAL;
	}

	/*
	 * If there was %-EBADMSG, but the header CRC is still OK, report about
	 * a bit-flip to force scrubbing on this PEB.
	 */
	return read_err ? UBI_IO_BITFLIPS : 0;
}

/**
 * ubi_io_write_ec_hdr - write an erase counter header.
 * @ubi: UBI device description object
 * @pnum: physical eraseblock to write to
 * @ec_hdr: the erase counter header to write
 *
 * This function writes erase counter header described by @ec_hdr to physical
 * eraseblock @pnum. It also fills most fields of @ec_hdr before writing, so
 * the caller do not have to fill them. Callers must only fill the @ec_hdr->ec
 * field.
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure. If %-EIO is returned, the physical eraseblock most probably
 * went bad.
 */
int ubi_io_write_ec_hdr(struct ubi_device *ubi, int pnum,
			struct ubi_ec_hdr *ec_hdr)
{
	int err;
	uint32_t crc;

	dbg_io("write EC header to PEB %d", pnum);
	ubi_assert(pnum >= 0 &&  pnum < ubi->peb_count);

	ec_hdr->magic = cpu_to_be32(UBI_EC_HDR_MAGIC);
	ec_hdr->version = UBI_VERSION;
	ec_hdr->vid_hdr_offset = cpu_to_be32(ubi->vid_hdr_offset);
	ec_hdr->data_offset = cpu_to_be32(ubi->leb_start);
	ec_hdr->image_seq = cpu_to_be32(ubi->image_seq);
	crc = crc32(UBI_CRC32_INIT, ec_hdr, UBI_EC_HDR_SIZE_CRC);
	ec_hdr->hdr_crc = cpu_to_be32(crc);

	err = paranoid_check_ec_hdr(ubi, pnum, ec_hdr);
	if (err)
		return err;

	err = ubi_io_write(ubi, ec_hdr, pnum, 0, ubi->ec_hdr_alsize);
	return err;
}

/**
 * validate_vid_hdr - validate a volume identifier header.
 * @ubi: UBI device description object
 * @vid_hdr: the volume identifier header to check
 *
 * This function checks that data stored in the volume identifier header
 * @vid_hdr. Returns zero if the VID header is OK and %1 if not.
 */
static int validate_vid_hdr(const struct ubi_device *ubi,
			    const struct ubi_vid_hdr *vid_hdr)
{
	int vol_type = vid_hdr->vol_type;
	int copy_flag = vid_hdr->copy_flag;
	int vol_id = be32_to_cpu(vid_hdr->vol_id);
	int lnum = be32_to_cpu(vid_hdr->lnum);
	int compat = vid_hdr->compat;
	int data_size = be32_to_cpu(vid_hdr->data_size);
	int used_ebs = be32_to_cpu(vid_hdr->used_ebs);
	int data_pad = be32_to_cpu(vid_hdr->data_pad);
	int data_crc = be32_to_cpu(vid_hdr->data_crc);
	int usable_leb_size = ubi->leb_size - data_pad;

	if (copy_flag != 0 && copy_flag != 1) {
		dbg_err("bad copy_flag");
		goto bad;
	}

	if (vol_id < 0 || lnum < 0 || data_size < 0 || used_ebs < 0 ||
	    data_pad < 0) {
		dbg_err("negative values");
		goto bad;
	}

	if (vol_id >= UBI_MAX_VOLUMES && vol_id < UBI_INTERNAL_VOL_START) {
		dbg_err("bad vol_id");
		goto bad;
	}

	if (vol_id < UBI_INTERNAL_VOL_START && compat != 0) {
		dbg_err("bad compat");
		goto bad;
	}

	if (vol_id >= UBI_INTERNAL_VOL_START && compat != UBI_COMPAT_DELETE &&
	    compat != UBI_COMPAT_RO && compat != UBI_COMPAT_PRESERVE &&
	    compat != UBI_COMPAT_REJECT) {
		dbg_err("bad compat");
		goto bad;
	}

	if (vol_type != UBI_VID_DYNAMIC && vol_type != UBI_VID_STATIC) {
		dbg_err("bad vol_type");
		goto bad;
	}

	if (data_pad >= ubi->leb_size / 2) {
		dbg_err("bad data_pad");
		goto bad;
	}

	if (vol_type == UBI_VID_STATIC) {
		/*
		 * Although from high-level point of view static volumes may
		 * contain zero bytes of data, but no VID headers can contain
		 * zero at these fields, because they empty volumes do not have
		 * mapped logical eraseblocks.
		 */
		if (used_ebs == 0) {
			dbg_err("zero used_ebs");
			goto bad;
		}
		if (data_size == 0) {
			dbg_err("zero data_size");
			goto bad;
		}
		if (lnum < used_ebs - 1) {
			if (data_size != usable_leb_size) {
				dbg_err("bad data_size");
				goto bad;
			}
		} else if (lnum == used_ebs - 1) {
			if (data_size == 0) {
				dbg_err("bad data_size at last LEB");
				goto bad;
			}
		} else {
			dbg_err("too high lnum");
			goto bad;
		}
	} else {
		if (copy_flag == 0) {
			if (data_crc != 0) {
				dbg_err("non-zero data CRC");
				goto bad;
			}
			if (data_size != 0) {
				dbg_err("non-zero data_size");
				goto bad;
			}
		} else {
			if (data_size == 0) {
				dbg_err("zero data_size of copy");
				goto bad;
			}
		}
		if (used_ebs != 0) {
			dbg_err("bad used_ebs");
			goto bad;
		}
	}

	return 0;

bad:
	ubi_err("bad VID header");
	ubi_dbg_dump_vid_hdr(vid_hdr);
	ubi_dbg_dump_stack();
	return 1;
}

/**
 * ubi_io_read_vid_hdr - read and check a volume identifier header.
 * @ubi: UBI device description object
 * @pnum: physical eraseblock number to read from
 * @vid_hdr: &struct ubi_vid_hdr object where to store the read volume
 * identifier header
 * @verbose: be verbose if the header is corrupted or wasn't found
 *
 * This function reads the volume identifier header from physical eraseblock
 * @pnum and stores it in @vid_hdr. It also checks CRC checksum of the read
 * volume identifier header. The error codes are the same as in
 * 'ubi_io_read_ec_hdr()'.
 *
 * Note, the implementation of this function is also very similar to
 * 'ubi_io_read_ec_hdr()', so refer commentaries in 'ubi_io_read_ec_hdr()'.
 */
int ubi_io_read_vid_hdr(struct ubi_device *ubi, int pnum,
			struct ubi_vid_hdr *vid_hdr, int verbose)
{
	int err, read_err;
	uint32_t crc, magic, hdr_crc;
	void *p;

	dbg_io("read VID header from PEB %d", pnum);
	ubi_assert(pnum >= 0 &&  pnum < ubi->peb_count);

	p = (char *)vid_hdr - ubi->vid_hdr_shift;
	read_err = ubi_io_read(ubi, p, pnum, ubi->vid_hdr_aloffset,
			  ubi->vid_hdr_alsize);
	if (read_err && read_err != UBI_IO_BITFLIPS && read_err != -EBADMSG)
		return read_err;

	magic = be32_to_cpu(vid_hdr->magic);
	if (magic != UBI_VID_HDR_MAGIC) {
		if (read_err == -EBADMSG)
			return UBI_IO_BAD_HDR_EBADMSG;

		if (ubi_check_pattern(vid_hdr, 0xFF, UBI_VID_HDR_SIZE)) {
			if (verbose)
				ubi_warn("no VID header found at PEB %d, "
					 "only 0xFF bytes", pnum);
			dbg_bld("no VID header found at PEB %d, "
				"only 0xFF bytes", pnum);
			if (!read_err)
				return UBI_IO_FF;
			else
				return UBI_IO_FF_BITFLIPS;
		}

		if (verbose) {
			ubi_warn("bad magic number at PEB %d: %08x instead of "
				 "%08x", pnum, magic, UBI_VID_HDR_MAGIC);
			ubi_dbg_dump_vid_hdr(vid_hdr);
		}
		dbg_bld("bad magic number at PEB %d: %08x instead of "
			"%08x", pnum, magic, UBI_VID_HDR_MAGIC);
		return UBI_IO_BAD_HDR;
	}

	crc = crc32(UBI_CRC32_INIT, vid_hdr, UBI_VID_HDR_SIZE_CRC);
	hdr_crc = be32_to_cpu(vid_hdr->hdr_crc);

	if (hdr_crc != crc) {
		if (verbose) {
			ubi_warn("bad CRC at PEB %d, calculated %#08x, "
				 "read %#08x", pnum, crc, hdr_crc);
			ubi_dbg_dump_vid_hdr(vid_hdr);
		}
		dbg_bld("bad CRC at PEB %d, calculated %#08x, "
			"read %#08x", pnum, crc, hdr_crc);
		if (!read_err)
			return UBI_IO_BAD_HDR;
		else
			return UBI_IO_BAD_HDR_EBADMSG;
	}

	err = validate_vid_hdr(ubi, vid_hdr);
	if (err) {
		ubi_err("validation failed for PEB %d", pnum);
		return -EINVAL;
	}

	return read_err ? UBI_IO_BITFLIPS : 0;
}

/**
 * ubi_io_write_vid_hdr - write a volume identifier header.
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock number to write to
 * @vid_hdr: the volume identifier header to write
 *
 * This function writes the volume identifier header described by @vid_hdr to
 * physical eraseblock @pnum. This function automatically fills the
 * @vid_hdr->magic and the @vid_hdr->version fields, as well as calculates
 * header CRC checksum and stores it at vid_hdr->hdr_crc.
 *
 * This function returns zero in case of success and a negative error code in
 * case of failure. If %-EIO is returned, the physical eraseblock probably went
 * bad.
 */
int ubi_io_write_vid_hdr(struct ubi_device *ubi, int pnum,
			 struct ubi_vid_hdr *vid_hdr)
{
	int err;
	uint32_t crc;
	void *p;

	dbg_io("write VID header to PEB %d", pnum);
	ubi_assert(pnum >= 0 &&  pnum < ubi->peb_count);

	err = paranoid_check_peb_ec_hdr(ubi, pnum);
	if (err)
		return err;

	vid_hdr->magic = cpu_to_be32(UBI_VID_HDR_MAGIC);
	vid_hdr->version = UBI_VERSION;
	crc = crc32(UBI_CRC32_INIT, vid_hdr, UBI_VID_HDR_SIZE_CRC);
	vid_hdr->hdr_crc = cpu_to_be32(crc);

	err = paranoid_check_vid_hdr(ubi, pnum, vid_hdr);
	if (err)
		return err;

	p = (char *)vid_hdr - ubi->vid_hdr_shift;
	err = ubi_io_write(ubi, p, pnum, ubi->vid_hdr_aloffset,
			   ubi->vid_hdr_alsize);
	return err;
}

#ifdef CONFIG_MTD_UBI_DEBUG

/**
 * paranoid_check_not_bad - ensure that a physical eraseblock is not bad.
 * @ubi: UBI device description object
 * @pnum: physical eraseblock number to check
 *
 * This function returns zero if the physical eraseblock is good, %-EINVAL if
 * it is bad and a negative error code if an error occurred.
 */
static int paranoid_check_not_bad(const struct ubi_device *ubi, int pnum)
{
	int err;

	if (!(ubi_chk_flags & UBI_CHK_IO))
		return 0;

	err = ubi_io_is_bad(ubi, pnum);
	if (!err)
		return err;

	ubi_err("paranoid check failed for PEB %d", pnum);
	ubi_dbg_dump_stack();
	return err > 0 ? -EINVAL : err;
}

/**
 * paranoid_check_ec_hdr - check if an erase counter header is all right.
 * @ubi: UBI device description object
 * @pnum: physical eraseblock number the erase counter header belongs to
 * @ec_hdr: the erase counter header to check
 *
 * This function returns zero if the erase counter header contains valid
 * values, and %-EINVAL if not.
 */
static int paranoid_check_ec_hdr(const struct ubi_device *ubi, int pnum,
				 const struct ubi_ec_hdr *ec_hdr)
{
	int err;
	uint32_t magic;

	if (!(ubi_chk_flags & UBI_CHK_IO))
		return 0;

	magic = be32_to_cpu(ec_hdr->magic);
	if (magic != UBI_EC_HDR_MAGIC) {
		ubi_err("bad magic %#08x, must be %#08x",
			magic, UBI_EC_HDR_MAGIC);
		goto fail;
	}

	err = validate_ec_hdr(ubi, ec_hdr);
	if (err) {
		ubi_err("paranoid check failed for PEB %d", pnum);
		goto fail;
	}

	return 0;

fail:
	ubi_dbg_dump_ec_hdr(ec_hdr);
	ubi_dbg_dump_stack();
	return -EINVAL;
}

/**
 * paranoid_check_peb_ec_hdr - check erase counter header.
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock number to check
 *
 * This function returns zero if the erase counter header is all right and and
 * a negative error code if not or if an error occurred.
 */
static int paranoid_check_peb_ec_hdr(const struct ubi_device *ubi, int pnum)
{
	int err;
	uint32_t crc, hdr_crc;
	struct ubi_ec_hdr *ec_hdr;

	if (!(ubi_chk_flags & UBI_CHK_IO))
		return 0;

	ec_hdr = kzalloc(ubi->ec_hdr_alsize, GFP_NOFS);
	if (!ec_hdr)
		return -ENOMEM;

	err = ubi_io_read(ubi, ec_hdr, pnum, 0, UBI_EC_HDR_SIZE);
	if (err && err != UBI_IO_BITFLIPS && err != -EBADMSG)
		goto exit;

	crc = crc32(UBI_CRC32_INIT, ec_hdr, UBI_EC_HDR_SIZE_CRC);
	hdr_crc = be32_to_cpu(ec_hdr->hdr_crc);
	if (hdr_crc != crc) {
		ubi_err("bad CRC, calculated %#08x, read %#08x", crc, hdr_crc);
		ubi_err("paranoid check failed for PEB %d", pnum);
		ubi_dbg_dump_ec_hdr(ec_hdr);
		ubi_dbg_dump_stack();
		err = -EINVAL;
		goto exit;
	}

	err = paranoid_check_ec_hdr(ubi, pnum, ec_hdr);

exit:
	kfree(ec_hdr);
	return err;
}

/**
 * paranoid_check_vid_hdr - check that a volume identifier header is all right.
 * @ubi: UBI device description object
 * @pnum: physical eraseblock number the volume identifier header belongs to
 * @vid_hdr: the volume identifier header to check
 *
 * This function returns zero if the volume identifier header is all right, and
 * %-EINVAL if not.
 */
static int paranoid_check_vid_hdr(const struct ubi_device *ubi, int pnum,
				  const struct ubi_vid_hdr *vid_hdr)
{
	int err;
	uint32_t magic;

	if (!(ubi_chk_flags & UBI_CHK_IO))
		return 0;

	magic = be32_to_cpu(vid_hdr->magic);
	if (magic != UBI_VID_HDR_MAGIC) {
		ubi_err("bad VID header magic %#08x at PEB %d, must be %#08x",
			magic, pnum, UBI_VID_HDR_MAGIC);
		goto fail;
	}

	err = validate_vid_hdr(ubi, vid_hdr);
	if (err) {
		ubi_err("paranoid check failed for PEB %d", pnum);
		goto fail;
	}

	return err;

fail:
	ubi_err("paranoid check failed for PEB %d", pnum);
	ubi_dbg_dump_vid_hdr(vid_hdr);
	ubi_dbg_dump_stack();
	return -EINVAL;

}

/**
 * paranoid_check_peb_vid_hdr - check volume identifier header.
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock number to check
 *
 * This function returns zero if the volume identifier header is all right,
 * and a negative error code if not or if an error occurred.
 */
static int paranoid_check_peb_vid_hdr(const struct ubi_device *ubi, int pnum)
{
	int err;
	uint32_t crc, hdr_crc;
	struct ubi_vid_hdr *vid_hdr;
	void *p;

	if (!(ubi_chk_flags & UBI_CHK_IO))
		return 0;

	vid_hdr = ubi_zalloc_vid_hdr(ubi, GFP_NOFS);
	if (!vid_hdr)
		return -ENOMEM;

	p = (char *)vid_hdr - ubi->vid_hdr_shift;
	err = ubi_io_read(ubi, p, pnum, ubi->vid_hdr_aloffset,
			  ubi->vid_hdr_alsize);
	if (err && err != UBI_IO_BITFLIPS && err != -EBADMSG)
		goto exit;

	crc = crc32(UBI_CRC32_INIT, vid_hdr, UBI_EC_HDR_SIZE_CRC);
	hdr_crc = be32_to_cpu(vid_hdr->hdr_crc);
	if (hdr_crc != crc) {
		ubi_err("bad VID header CRC at PEB %d, calculated %#08x, "
			"read %#08x", pnum, crc, hdr_crc);
		ubi_err("paranoid check failed for PEB %d", pnum);
		ubi_dbg_dump_vid_hdr(vid_hdr);
		ubi_dbg_dump_stack();
		err = -EINVAL;
		goto exit;
	}

	err = paranoid_check_vid_hdr(ubi, pnum, vid_hdr);

exit:
	ubi_free_vid_hdr(ubi, vid_hdr);
	return err;
}

/**
 * ubi_dbg_check_write - make sure write succeeded.
 * @ubi: UBI device description object
 * @buf: buffer with data which were written
 * @pnum: physical eraseblock number the data were written to
 * @offset: offset within the physical eraseblock the data were written to
 * @len: how many bytes were written
 *
 * This functions reads data which were recently written and compares it with
 * the original data buffer - the data have to match. Returns zero if the data
 * match and a negative error code if not or in case of failure.
 */
int ubi_dbg_check_write(struct ubi_device *ubi, const void *buf, int pnum,
			int offset, int len)
{
	int err, i;
	size_t read;
	void *buf1;
	loff_t addr = (loff_t)pnum * ubi->peb_size + offset;

	if (!(ubi_chk_flags & UBI_CHK_IO))
		return 0;

	buf1 = __vmalloc(len, GFP_NOFS, PAGE_KERNEL);
	if (!buf1) {
		ubi_err("cannot allocate memory to check writes");
		return 0;
	}

	err = ubi->mtd->read(ubi->mtd, addr, len, &read, buf1);
	if (err && err != -EUCLEAN)
		goto out_free;

	for (i = 0; i < len; i++) {
		uint8_t c = ((uint8_t *)buf)[i];
		uint8_t c1 = ((uint8_t *)buf1)[i];
		int dump_len;

		if (c == c1)
			continue;

		ubi_err("paranoid check failed for PEB %d:%d, len %d",
			pnum, offset, len);
		ubi_msg("data differ at position %d", i);
		dump_len = max_t(int, 128, len - i);
		ubi_msg("hex dump of the original buffer from %d to %d",
			i, i + dump_len);
		print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 32, 1,
			       buf + i, dump_len, 1);
		ubi_msg("hex dump of the read buffer from %d to %d",
			i, i + dump_len);
		print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 32, 1,
			       buf1 + i, dump_len, 1);
		ubi_dbg_dump_stack();
		err = -EINVAL;
		goto out_free;
	}

	vfree(buf1);
	return 0;

out_free:
	vfree(buf1);
	return err;
}

/**
 * ubi_dbg_check_all_ff - check that a region of flash is empty.
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock number to check
 * @offset: the starting offset within the physical eraseblock to check
 * @len: the length of the region to check
 *
 * This function returns zero if only 0xFF bytes are present at offset
 * @offset of the physical eraseblock @pnum, and a negative error code if not
 * or if an error occurred.
 */
int ubi_dbg_check_all_ff(struct ubi_device *ubi, int pnum, int offset, int len)
{
	size_t read;
	int err;
	void *buf;
	loff_t addr = (loff_t)pnum * ubi->peb_size + offset;

	if (!(ubi_chk_flags & UBI_CHK_IO))
		return 0;

	buf = __vmalloc(len, GFP_NOFS, PAGE_KERNEL);
	if (!buf) {
		ubi_err("cannot allocate memory to check for 0xFFs");
		return 0;
	}

	err = ubi->mtd->read(ubi->mtd, addr, len, &read, buf);
	if (err && err != -EUCLEAN) {
		ubi_err("error %d while reading %d bytes from PEB %d:%d, "
			"read %zd bytes", err, len, pnum, offset, read);
		goto error;
	}

	err = ubi_check_pattern(buf, 0xFF, len);
	if (err == 0) {
		ubi_err("flash region at PEB %d:%d, length %d does not "
			"contain all 0xFF bytes", pnum, offset, len);
		goto fail;
	}

	vfree(buf);
	return 0;

fail:
	ubi_err("paranoid check failed for PEB %d", pnum);
	ubi_msg("hex dump of the %d-%d region", offset, offset + len);
	print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 32, 1, buf, len, 1);
	err = -EINVAL;
error:
	ubi_dbg_dump_stack();
	vfree(buf);
	return err;
}

#endif /* CONFIG_MTD_UBI_DEBUG */
