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
 * UBI input/output unit.
 *
 * This unit provides a uniform way to work with all kinds of the underlying
 * MTD devices. It also implements handy functions for reading and writing UBI
 * headers.
 *
 * We are trying to have a paranoid mindset and not to trust to what we read
 * from the flash media in order to be more secure and robust. So this unit
 * validates every single header it reads from the flash media.
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
 * bytes which are no relevant to the sub-page are 0xFF. So, basically, writing
 * 4x512 sub-pages is 4 times slower then writing one 2KiB NAND page. Thus, we
 * prefer to use sub-pages only for EV and VID headers.
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
 * The I/O unit does the following trick in order to avoid this extra copy.
 * It always allocates a @ubi->vid_hdr_alsize bytes buffer for the VID header
 * and returns a pointer to offset @ubi->vid_hdr_shift of this buffer. When the
 * VID header is being written out, it shifts the VID header pointer back and
 * writes the whole sub-page.
 */

#include <linux/crc32.h>
#include <linux/err.h>
#include "ubi.h"

#ifdef CONFIG_MTD_UBI_DEBUG_PARANOID
static int paranoid_check_not_bad(const struct ubi_device *ubi, int pnum);
static int paranoid_check_peb_ec_hdr(const struct ubi_device *ubi, int pnum);
static int paranoid_check_ec_hdr(const struct ubi_device *ubi, int pnum,
				 const struct ubi_ec_hdr *ec_hdr);
static int paranoid_check_peb_vid_hdr(const struct ubi_device *ubi, int pnum);
static int paranoid_check_vid_hdr(const struct ubi_device *ubi, int pnum,
				  const struct ubi_vid_hdr *vid_hdr);
static int paranoid_check_all_ff(struct ubi_device *ubi, int pnum, int offset,
				 int len);
#else
#define paranoid_check_not_bad(ubi, pnum) 0
#define paranoid_check_peb_ec_hdr(ubi, pnum)  0
#define paranoid_check_ec_hdr(ubi, pnum, ec_hdr)  0
#define paranoid_check_peb_vid_hdr(ubi, pnum) 0
#define paranoid_check_vid_hdr(ubi, pnum, vid_hdr) 0
#define paranoid_check_all_ff(ubi, pnum, offset, len) 0
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
		return err > 0 ? -EINVAL : err;

	addr = (loff_t)pnum * ubi->peb_size + offset;
retry:
	err = ubi->mtd->read(ubi->mtd, addr, len, &read, buf);
	if (err) {
		if (err == -EUCLEAN) {
			/*
			 * -EUCLEAN is reported if there was a bit-flip which
			 * was corrected, so this is harmless.
			 */
			ubi_msg("fixable bit-flip detected at PEB %d", pnum);
			ubi_assert(len == read);
			return UBI_IO_BITFLIPS;
		}

		if (read != len && retries++ < UBI_IO_RETRIES) {
			dbg_io("error %d while reading %d bytes from PEB %d:%d, "
			       "read only %zd bytes, retry",
			       err, len, pnum, offset, read);
			yield();
			goto retry;
		}

		ubi_err("error %d while reading %d bytes from PEB %d:%d, "
			"read %zd bytes", err, len, pnum, offset, read);
		ubi_dbg_dump_stack();
	} else {
		ubi_assert(len == read);

		if (ubi_dbg_is_bitflip()) {
			dbg_msg("bit-flip (emulated)");
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
		return err > 0 ? -EINVAL : err;

	/* The area we are writing to has to contain all 0xFF bytes */
	err = paranoid_check_all_ff(ubi, pnum, offset, len);
	if (err)
		return err > 0 ? -EINVAL : err;

	if (offset >= ubi->leb_start) {
		/*
		 * We write to the data area of the physical eraseblock. Make
		 * sure it has valid EC and VID headers.
		 */
		err = paranoid_check_peb_ec_hdr(ubi, pnum);
		if (err)
			return err > 0 ? -EINVAL : err;
		err = paranoid_check_peb_vid_hdr(ubi, pnum);
		if (err)
			return err > 0 ? -EINVAL : err;
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
		ubi_err("error %d while writing %d bytes to PEB %d:%d, written"
			" %zd bytes", err, len, pnum, offset, written);
		ubi_dbg_dump_stack();
	} else
		ubi_assert(written == len);

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

	err = paranoid_check_all_ff(ubi, pnum, 0, ubi->peb_size);
	if (err)
		return err > 0 ? -EINVAL : err;

	if (ubi_dbg_is_erase_failure() && !err) {
		dbg_err("cannot erase PEB %d (emulated)", pnum);
		return -EIO;
	}

	return 0;
}

/**
 * check_pattern - check if buffer contains only a certain byte pattern.
 * @buf: buffer to check
 * @patt: the pattern to check
 * @size: buffer size in bytes
 *
 * This function returns %1 in there are only @patt bytes in @buf, and %0 if
 * something else was also found.
 */
static int check_pattern(const void *buf, uint8_t patt, int size)
{
	int i;

	for (i = 0; i < size; i++)
		if (((const uint8_t *)buf)[i] != patt)
			return 0;
	return 1;
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

		err = check_pattern(ubi->peb_buf1, 0xFF, ubi->peb_size);
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

		err = check_pattern(ubi->peb_buf1, patterns[i], ubi->peb_size);
		if (err == 0) {
			ubi_err("pattern %x checking failed for PEB %d",
				patterns[i], pnum);
			err = -EIO;
			goto out;
		}
	}

	err = patt_count;

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
 * ubi_io_sync_erase - synchronously erase a physical eraseblock.
 * @ubi: UBI device description object
 * @pnum: physical eraseblock number to erase
 * @torture: if this physical eraseblock has to be tortured
 *
 * This function synchronously erases physical eraseblock @pnum. If @torture
 * flag is not zero, the physical eraseblock is checked by means of writing
 * different patterns to it and reading them back. If the torturing is enabled,
 * the physical eraseblock is erased more then once.
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
		return err > 0 ? -EINVAL : err;

	if (ubi->ro_mode) {
		ubi_err("read-only mode");
		return -EROFS;
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
 * o %UBI_IO_BAD_EC_HDR if the erase counter header is corrupted (a CRC error);
 * o %UBI_IO_PEB_EMPTY if the physical eraseblock is empty;
 * o a negative error code in case of failure.
 */
int ubi_io_read_ec_hdr(struct ubi_device *ubi, int pnum,
		       struct ubi_ec_hdr *ec_hdr, int verbose)
{
	int err, read_err = 0;
	uint32_t crc, magic, hdr_crc;

	dbg_io("read EC header from PEB %d", pnum);
	ubi_assert(pnum >= 0 && pnum < ubi->peb_count);

	err = ubi_io_read(ubi, ec_hdr, pnum, 0, UBI_EC_HDR_SIZE);
	if (err) {
		if (err != UBI_IO_BITFLIPS && err != -EBADMSG)
			return err;

		/*
		 * We read all the data, but either a correctable bit-flip
		 * occurred, or MTD reported about some data integrity error,
		 * like an ECC error in case of NAND. The former is harmless,
		 * the later may mean that the read data is corrupted. But we
		 * have a CRC check-sum and we will detect this. If the EC
		 * header is still OK, we just report this as there was a
		 * bit-flip.
		 */
		read_err = err;
	}

	magic = be32_to_cpu(ec_hdr->magic);
	if (magic != UBI_EC_HDR_MAGIC) {
		/*
		 * The magic field is wrong. Let's check if we have read all
		 * 0xFF. If yes, this physical eraseblock is assumed to be
		 * empty.
		 *
		 * But if there was a read error, we do not test it for all
		 * 0xFFs. Even if it does contain all 0xFFs, this error
		 * indicates that something is still wrong with this physical
		 * eraseblock and we anyway cannot treat it as empty.
		 */
		if (read_err != -EBADMSG &&
		    check_pattern(ec_hdr, 0xFF, UBI_EC_HDR_SIZE)) {
			/* The physical eraseblock is supposedly empty */

			/*
			 * The below is just a paranoid check, it has to be
			 * compiled out if paranoid checks are disabled.
			 */
			err = paranoid_check_all_ff(ubi, pnum, 0,
						    ubi->peb_size);
			if (err)
				return err > 0 ? UBI_IO_BAD_EC_HDR : err;

			if (verbose)
				ubi_warn("no EC header found at PEB %d, "
					 "only 0xFF bytes", pnum);
			return UBI_IO_PEB_EMPTY;
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
		return UBI_IO_BAD_EC_HDR;
	}

	crc = crc32(UBI_CRC32_INIT, ec_hdr, UBI_EC_HDR_SIZE_CRC);
	hdr_crc = be32_to_cpu(ec_hdr->hdr_crc);

	if (hdr_crc != crc) {
		if (verbose) {
			ubi_warn("bad EC header CRC at PEB %d, calculated %#08x,"
				 " read %#08x", pnum, crc, hdr_crc);
			ubi_dbg_dump_ec_hdr(ec_hdr);
		}
		return UBI_IO_BAD_EC_HDR;
	}

	/* And of course validate what has just been read from the media */
	err = validate_ec_hdr(ubi, ec_hdr);
	if (err) {
		ubi_err("validation failed for PEB %d", pnum);
		return -EINVAL;
	}

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
	crc = crc32(UBI_CRC32_INIT, ec_hdr, UBI_EC_HDR_SIZE_CRC);
	ec_hdr->hdr_crc = cpu_to_be32(crc);

	err = paranoid_check_ec_hdr(ubi, pnum, ec_hdr);
	if (err)
		return -EINVAL;

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
 * volume identifier header. The following codes may be returned:
 *
 * o %0 if the CRC checksum is correct and the header was successfully read;
 * o %UBI_IO_BITFLIPS if the CRC is correct, but bit-flips were detected
 *   and corrected by the flash driver; this is harmless but may indicate that
 *   this eraseblock may become bad soon;
 * o %UBI_IO_BAD_VID_HRD if the volume identifier header is corrupted (a CRC
 *   error detected);
 * o %UBI_IO_PEB_FREE if the physical eraseblock is free (i.e., there is no VID
 *   header there);
 * o a negative error code in case of failure.
 */
int ubi_io_read_vid_hdr(struct ubi_device *ubi, int pnum,
			struct ubi_vid_hdr *vid_hdr, int verbose)
{
	int err, read_err = 0;
	uint32_t crc, magic, hdr_crc;
	void *p;

	dbg_io("read VID header from PEB %d", pnum);
	ubi_assert(pnum >= 0 &&  pnum < ubi->peb_count);

	p = (char *)vid_hdr - ubi->vid_hdr_shift;
	err = ubi_io_read(ubi, p, pnum, ubi->vid_hdr_aloffset,
			  ubi->vid_hdr_alsize);
	if (err) {
		if (err != UBI_IO_BITFLIPS && err != -EBADMSG)
			return err;

		/*
		 * We read all the data, but either a correctable bit-flip
		 * occurred, or MTD reported about some data integrity error,
		 * like an ECC error in case of NAND. The former is harmless,
		 * the later may mean the read data is corrupted. But we have a
		 * CRC check-sum and we will identify this. If the VID header is
		 * still OK, we just report this as there was a bit-flip.
		 */
		read_err = err;
	}

	magic = be32_to_cpu(vid_hdr->magic);
	if (magic != UBI_VID_HDR_MAGIC) {
		/*
		 * If we have read all 0xFF bytes, the VID header probably does
		 * not exist and the physical eraseblock is assumed to be free.
		 *
		 * But if there was a read error, we do not test the data for
		 * 0xFFs. Even if it does contain all 0xFFs, this error
		 * indicates that something is still wrong with this physical
		 * eraseblock and it cannot be regarded as free.
		 */
		if (read_err != -EBADMSG &&
		    check_pattern(vid_hdr, 0xFF, UBI_VID_HDR_SIZE)) {
			/* The physical eraseblock is supposedly free */

			/*
			 * The below is just a paranoid check, it has to be
			 * compiled out if paranoid checks are disabled.
			 */
			err = paranoid_check_all_ff(ubi, pnum, ubi->leb_start,
						    ubi->leb_size);
			if (err)
				return err > 0 ? UBI_IO_BAD_VID_HDR : err;

			if (verbose)
				ubi_warn("no VID header found at PEB %d, "
					 "only 0xFF bytes", pnum);
			return UBI_IO_PEB_FREE;
		}

		/*
		 * This is not a valid VID header, and these are not 0xFF
		 * bytes. Report that the header is corrupted.
		 */
		if (verbose) {
			ubi_warn("bad magic number at PEB %d: %08x instead of "
				 "%08x", pnum, magic, UBI_VID_HDR_MAGIC);
			ubi_dbg_dump_vid_hdr(vid_hdr);
		}
		return UBI_IO_BAD_VID_HDR;
	}

	crc = crc32(UBI_CRC32_INIT, vid_hdr, UBI_VID_HDR_SIZE_CRC);
	hdr_crc = be32_to_cpu(vid_hdr->hdr_crc);

	if (hdr_crc != crc) {
		if (verbose) {
			ubi_warn("bad CRC at PEB %d, calculated %#08x, "
				 "read %#08x", pnum, crc, hdr_crc);
			ubi_dbg_dump_vid_hdr(vid_hdr);
		}
		return UBI_IO_BAD_VID_HDR;
	}

	/* Validate the VID header that we have just read */
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
		return err > 0 ? -EINVAL: err;

	vid_hdr->magic = cpu_to_be32(UBI_VID_HDR_MAGIC);
	vid_hdr->version = UBI_VERSION;
	crc = crc32(UBI_CRC32_INIT, vid_hdr, UBI_VID_HDR_SIZE_CRC);
	vid_hdr->hdr_crc = cpu_to_be32(crc);

	err = paranoid_check_vid_hdr(ubi, pnum, vid_hdr);
	if (err)
		return -EINVAL;

	p = (char *)vid_hdr - ubi->vid_hdr_shift;
	err = ubi_io_write(ubi, p, pnum, ubi->vid_hdr_aloffset,
			   ubi->vid_hdr_alsize);
	return err;
}

#ifdef CONFIG_MTD_UBI_DEBUG_PARANOID

/**
 * paranoid_check_not_bad - ensure that a physical eraseblock is not bad.
 * @ubi: UBI device description object
 * @pnum: physical eraseblock number to check
 *
 * This function returns zero if the physical eraseblock is good, a positive
 * number if it is bad and a negative error code if an error occurred.
 */
static int paranoid_check_not_bad(const struct ubi_device *ubi, int pnum)
{
	int err;

	err = ubi_io_is_bad(ubi, pnum);
	if (!err)
		return err;

	ubi_err("paranoid check failed for PEB %d", pnum);
	ubi_dbg_dump_stack();
	return err;
}

/**
 * paranoid_check_ec_hdr - check if an erase counter header is all right.
 * @ubi: UBI device description object
 * @pnum: physical eraseblock number the erase counter header belongs to
 * @ec_hdr: the erase counter header to check
 *
 * This function returns zero if the erase counter header contains valid
 * values, and %1 if not.
 */
static int paranoid_check_ec_hdr(const struct ubi_device *ubi, int pnum,
				 const struct ubi_ec_hdr *ec_hdr)
{
	int err;
	uint32_t magic;

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
	return 1;
}

/**
 * paranoid_check_peb_ec_hdr - check that the erase counter header of a
 * physical eraseblock is in-place and is all right.
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock number to check
 *
 * This function returns zero if the erase counter header is all right, %1 if
 * not, and a negative error code if an error occurred.
 */
static int paranoid_check_peb_ec_hdr(const struct ubi_device *ubi, int pnum)
{
	int err;
	uint32_t crc, hdr_crc;
	struct ubi_ec_hdr *ec_hdr;

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
		err = 1;
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
 * %1 if not.
 */
static int paranoid_check_vid_hdr(const struct ubi_device *ubi, int pnum,
				  const struct ubi_vid_hdr *vid_hdr)
{
	int err;
	uint32_t magic;

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
	return 1;

}

/**
 * paranoid_check_peb_vid_hdr - check that the volume identifier header of a
 * physical eraseblock is in-place and is all right.
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock number to check
 *
 * This function returns zero if the volume identifier header is all right,
 * %1 if not, and a negative error code if an error occurred.
 */
static int paranoid_check_peb_vid_hdr(const struct ubi_device *ubi, int pnum)
{
	int err;
	uint32_t crc, hdr_crc;
	struct ubi_vid_hdr *vid_hdr;
	void *p;

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
		err = 1;
		goto exit;
	}

	err = paranoid_check_vid_hdr(ubi, pnum, vid_hdr);

exit:
	ubi_free_vid_hdr(ubi, vid_hdr);
	return err;
}

/**
 * paranoid_check_all_ff - check that a region of flash is empty.
 * @ubi: UBI device description object
 * @pnum: the physical eraseblock number to check
 * @offset: the starting offset within the physical eraseblock to check
 * @len: the length of the region to check
 *
 * This function returns zero if only 0xFF bytes are present at offset
 * @offset of the physical eraseblock @pnum, %1 if not, and a negative error
 * code if an error occurred.
 */
static int paranoid_check_all_ff(struct ubi_device *ubi, int pnum, int offset,
				 int len)
{
	size_t read;
	int err;
	loff_t addr = (loff_t)pnum * ubi->peb_size + offset;

	mutex_lock(&ubi->dbg_buf_mutex);
	err = ubi->mtd->read(ubi->mtd, addr, len, &read, ubi->dbg_peb_buf);
	if (err && err != -EUCLEAN) {
		ubi_err("error %d while reading %d bytes from PEB %d:%d, "
			"read %zd bytes", err, len, pnum, offset, read);
		goto error;
	}

	err = check_pattern(ubi->dbg_peb_buf, 0xFF, len);
	if (err == 0) {
		ubi_err("flash region at PEB %d:%d, length %d does not "
			"contain all 0xFF bytes", pnum, offset, len);
		goto fail;
	}
	mutex_unlock(&ubi->dbg_buf_mutex);

	return 0;

fail:
	ubi_err("paranoid check failed for PEB %d", pnum);
	dbg_msg("hex dump of the %d-%d region", offset, offset + len);
	print_hex_dump(KERN_DEBUG, "", DUMP_PREFIX_OFFSET, 32, 1,
		       ubi->dbg_peb_buf, len, 1);
	err = 1;
error:
	ubi_dbg_dump_stack();
	mutex_unlock(&ubi->dbg_buf_mutex);
	return err;
}

#endif /* CONFIG_MTD_UBI_DEBUG_PARANOID */
