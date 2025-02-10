// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2025, SaluteDevices. All Rights Reserved.
 *
 * Author: Martin Kurbanov <mmkurbanov@salutedevices.com>
 */

#include <linux/mtd/mtd.h>
#include <linux/mtd/spinand.h>

/**
 * spinand_otp_page_size() - Get SPI-NAND OTP page size
 * @spinand: the spinand device
 *
 * Return: the OTP page size.
 */
size_t spinand_otp_page_size(struct spinand_device *spinand)
{
	struct nand_device *nand = spinand_to_nand(spinand);

	return nanddev_page_size(nand) + nanddev_per_page_oobsize(nand);
}

static size_t spinand_otp_size(struct spinand_device *spinand,
			       const struct spinand_otp_layout *layout)
{
	return layout->npages * spinand_otp_page_size(spinand);
}

/**
 * spinand_fact_otp_size() - Get SPI-NAND factory OTP area size
 * @spinand: the spinand device
 *
 * Return: the OTP size.
 */
size_t spinand_fact_otp_size(struct spinand_device *spinand)
{
	return spinand_otp_size(spinand, &spinand->fact_otp->layout);
}

/**
 * spinand_user_otp_size() - Get SPI-NAND user OTP area size
 * @spinand: the spinand device
 *
 * Return: the OTP size.
 */
size_t spinand_user_otp_size(struct spinand_device *spinand)
{
	return spinand_otp_size(spinand, &spinand->user_otp->layout);
}

static int spinand_otp_check_bounds(struct spinand_device *spinand, loff_t ofs,
				    size_t len,
				    const struct spinand_otp_layout *layout)
{
	if (ofs < 0 || ofs + len > spinand_otp_size(spinand, layout))
		return -EINVAL;

	return 0;
}

static int spinand_user_otp_check_bounds(struct spinand_device *spinand,
					 loff_t ofs, size_t len)
{
	return spinand_otp_check_bounds(spinand, ofs, len,
					&spinand->user_otp->layout);
}

static int spinand_otp_rw(struct spinand_device *spinand, loff_t ofs,
			  size_t len, size_t *retlen, u8 *buf, bool is_write,
			  const struct spinand_otp_layout *layout)
{
	struct nand_page_io_req req = {};
	unsigned long long page;
	size_t copied = 0;
	size_t otp_pagesize = spinand_otp_page_size(spinand);
	int ret;

	if (!len)
		return 0;

	ret = spinand_otp_check_bounds(spinand, ofs, len, layout);
	if (ret)
		return ret;

	ret = spinand_upd_cfg(spinand, CFG_OTP_ENABLE, CFG_OTP_ENABLE);
	if (ret)
		return ret;

	page = ofs;
	req.dataoffs = do_div(page, otp_pagesize);
	req.pos.page = page + layout->start_page;
	req.type = is_write ? NAND_PAGE_WRITE : NAND_PAGE_READ;
	req.mode = MTD_OPS_RAW;
	req.databuf.in = buf;

	while (copied < len) {
		req.datalen = min_t(unsigned int,
				    otp_pagesize - req.dataoffs,
				    len - copied);

		if (is_write)
			ret = spinand_write_page(spinand, &req);
		else
			ret = spinand_read_page(spinand, &req);

		if (ret < 0)
			break;

		req.databuf.in += req.datalen;
		req.pos.page++;
		req.dataoffs = 0;
		copied += req.datalen;
	}

	*retlen = copied;

	if (spinand_upd_cfg(spinand, CFG_OTP_ENABLE, 0)) {
		dev_warn(&spinand_to_mtd(spinand)->dev,
			 "Can not disable OTP mode\n");
		ret = -EIO;
	}

	return ret;
}

/**
 * spinand_fact_otp_read() - Read from OTP area
 * @spinand: the spinand device
 * @ofs: the offset to read
 * @len: the number of data bytes to read
 * @retlen: the pointer to variable to store the number of read bytes
 * @buf: the buffer to store the read data
 *
 * Return: 0 on success, an error code otherwise.
 */
int spinand_fact_otp_read(struct spinand_device *spinand, loff_t ofs,
			  size_t len, size_t *retlen, u8 *buf)
{
	return spinand_otp_rw(spinand, ofs, len, retlen, buf, false,
			      &spinand->fact_otp->layout);
}

/**
 * spinand_user_otp_read() - Read from OTP area
 * @spinand: the spinand device
 * @ofs: the offset to read
 * @len: the number of data bytes to read
 * @retlen: the pointer to variable to store the number of read bytes
 * @buf: the buffer to store the read data
 *
 * Return: 0 on success, an error code otherwise.
 */
int spinand_user_otp_read(struct spinand_device *spinand, loff_t ofs,
			  size_t len, size_t *retlen, u8 *buf)
{
	return spinand_otp_rw(spinand, ofs, len, retlen, buf, false,
			      &spinand->user_otp->layout);
}

/**
 * spinand_user_otp_write() - Write to OTP area
 * @spinand:  the spinand device
 * @ofs: the offset to write to
 * @len: the number of bytes to write
 * @retlen: the pointer to variable to store the number of written bytes
 * @buf: the buffer with data to write
 *
 * Return: 0 on success, an error code otherwise.
 */
int spinand_user_otp_write(struct spinand_device *spinand, loff_t ofs,
			   size_t len, size_t *retlen, const u8 *buf)
{
	return spinand_otp_rw(spinand, ofs, len, retlen, (u8 *)buf, true,
			      &spinand->user_otp->layout);
}

static int spinand_mtd_otp_info(struct mtd_info *mtd, size_t len,
				size_t *retlen, struct otp_info *buf,
				bool is_fact)
{
	struct spinand_device *spinand = mtd_to_spinand(mtd);
	int ret;

	*retlen = 0;

	mutex_lock(&spinand->lock);

	if (is_fact)
		ret = spinand->fact_otp->ops->info(spinand, len, buf, retlen);
	else
		ret = spinand->user_otp->ops->info(spinand, len, buf, retlen);

	mutex_unlock(&spinand->lock);

	return ret;
}

static int spinand_mtd_fact_otp_info(struct mtd_info *mtd, size_t len,
				     size_t *retlen, struct otp_info *buf)
{
	return spinand_mtd_otp_info(mtd, len, retlen, buf, true);
}

static int spinand_mtd_user_otp_info(struct mtd_info *mtd, size_t len,
				     size_t *retlen, struct otp_info *buf)
{
	return spinand_mtd_otp_info(mtd, len, retlen, buf, false);
}

static int spinand_mtd_otp_read(struct mtd_info *mtd, loff_t ofs, size_t len,
				size_t *retlen, u8 *buf, bool is_fact)
{
	struct spinand_device *spinand = mtd_to_spinand(mtd);
	int ret;

	*retlen = 0;

	if (!len)
		return 0;

	ret = spinand_otp_check_bounds(spinand, ofs, len,
				       is_fact ? &spinand->fact_otp->layout :
						 &spinand->user_otp->layout);
	if (ret)
		return ret;

	mutex_lock(&spinand->lock);

	if (is_fact)
		ret = spinand->fact_otp->ops->read(spinand, ofs, len, retlen,
						   buf);
	else
		ret = spinand->user_otp->ops->read(spinand, ofs, len, retlen,
						   buf);

	mutex_unlock(&spinand->lock);

	return ret;
}

static int spinand_mtd_fact_otp_read(struct mtd_info *mtd, loff_t ofs,
				     size_t len, size_t *retlen, u8 *buf)
{
	return spinand_mtd_otp_read(mtd, ofs, len, retlen, buf, true);
}

static int spinand_mtd_user_otp_read(struct mtd_info *mtd, loff_t ofs,
				     size_t len, size_t *retlen, u8 *buf)
{
	return spinand_mtd_otp_read(mtd, ofs, len, retlen, buf, false);
}

static int spinand_mtd_user_otp_write(struct mtd_info *mtd, loff_t ofs,
				      size_t len, size_t *retlen, const u8 *buf)
{
	struct spinand_device *spinand = mtd_to_spinand(mtd);
	const struct spinand_user_otp_ops *ops = spinand->user_otp->ops;
	int ret;

	*retlen = 0;

	if (!len)
		return 0;

	ret = spinand_user_otp_check_bounds(spinand, ofs, len);
	if (ret)
		return ret;

	mutex_lock(&spinand->lock);
	ret = ops->write(spinand, ofs, len, retlen, buf);
	mutex_unlock(&spinand->lock);

	return ret;
}

static int spinand_mtd_user_otp_erase(struct mtd_info *mtd, loff_t ofs,
				      size_t len)
{
	struct spinand_device *spinand = mtd_to_spinand(mtd);
	const struct spinand_user_otp_ops *ops = spinand->user_otp->ops;
	int ret;

	if (!len)
		return 0;

	ret = spinand_user_otp_check_bounds(spinand, ofs, len);
	if (ret)
		return ret;

	mutex_lock(&spinand->lock);
	ret = ops->erase(spinand, ofs, len);
	mutex_unlock(&spinand->lock);

	return ret;
}

static int spinand_mtd_user_otp_lock(struct mtd_info *mtd, loff_t ofs,
				     size_t len)
{
	struct spinand_device *spinand = mtd_to_spinand(mtd);
	const struct spinand_user_otp_ops *ops = spinand->user_otp->ops;
	int ret;

	if (!len)
		return 0;

	ret = spinand_user_otp_check_bounds(spinand, ofs, len);
	if (ret)
		return ret;

	mutex_lock(&spinand->lock);
	ret = ops->lock(spinand, ofs, len);
	mutex_unlock(&spinand->lock);

	return ret;
}

/**
 * spinand_set_mtd_otp_ops() - Setup OTP methods
 * @spinand: the spinand device
 *
 * Setup OTP methods.
 *
 * Return: 0 on success, a negative error code otherwise.
 */
int spinand_set_mtd_otp_ops(struct spinand_device *spinand)
{
	struct mtd_info *mtd = spinand_to_mtd(spinand);
	const struct spinand_fact_otp_ops *fact_ops = spinand->fact_otp->ops;
	const struct spinand_user_otp_ops *user_ops = spinand->user_otp->ops;

	if (!user_ops && !fact_ops)
		return -EINVAL;

	if (user_ops) {
		if (user_ops->info)
			mtd->_get_user_prot_info = spinand_mtd_user_otp_info;

		if (user_ops->read)
			mtd->_read_user_prot_reg = spinand_mtd_user_otp_read;

		if (user_ops->write)
			mtd->_write_user_prot_reg = spinand_mtd_user_otp_write;

		if (user_ops->lock)
			mtd->_lock_user_prot_reg = spinand_mtd_user_otp_lock;

		if (user_ops->erase)
			mtd->_erase_user_prot_reg = spinand_mtd_user_otp_erase;
	}

	if (fact_ops) {
		if (fact_ops->info)
			mtd->_get_fact_prot_info = spinand_mtd_fact_otp_info;

		if (fact_ops->read)
			mtd->_read_fact_prot_reg = spinand_mtd_fact_otp_read;
	}

	return 0;
}
