/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */

#include "amdgpu_ras_eeprom.h"
#include "amdgpu.h"
#include "amdgpu_ras.h"
#include <linux/bits.h>
#include "atom.h"
#include "amdgpu_eeprom.h"
#include "amdgpu_atomfirmware.h"
#include <linux/debugfs.h>
#include <linux/uaccess.h>

#define EEPROM_I2C_MADDR_VEGA20         0x0
#define EEPROM_I2C_MADDR_ARCTURUS       0x40000
#define EEPROM_I2C_MADDR_ARCTURUS_D342  0x0
#define EEPROM_I2C_MADDR_SIENNA_CICHLID 0x0
#define EEPROM_I2C_MADDR_ALDEBARAN      0x0

/*
 * The 2 macros bellow represent the actual size in bytes that
 * those entities occupy in the EEPROM memory.
 * RAS_TABLE_RECORD_SIZE is different than sizeof(eeprom_table_record) which
 * uses uint64 to store 6b fields such as retired_page.
 */
#define RAS_TABLE_HEADER_SIZE   20
#define RAS_TABLE_RECORD_SIZE   24

/* Table hdr is 'AMDR' */
#define RAS_TABLE_HDR_VAL       0x414d4452
#define RAS_TABLE_VER           0x00010000

/* Bad GPU tag ‘BADG’ */
#define RAS_TABLE_HDR_BAD       0x42414447

/* Assume 2-Mbit size EEPROM and take up the whole space. */
#define RAS_TBL_SIZE_BYTES      (256 * 1024)
#define RAS_TABLE_START         0
#define RAS_HDR_START           RAS_TABLE_START
#define RAS_RECORD_START        (RAS_HDR_START + RAS_TABLE_HEADER_SIZE)
#define RAS_MAX_RECORD_COUNT    ((RAS_TBL_SIZE_BYTES - RAS_TABLE_HEADER_SIZE) \
				 / RAS_TABLE_RECORD_SIZE)

/* Given a zero-based index of an EEPROM RAS record, yields the EEPROM
 * offset off of RAS_TABLE_START.  That is, this is something you can
 * add to control->i2c_address, and then tell I2C layer to read
 * from/write to there. _N is the so called absolute index,
 * because it starts right after the table header.
 */
#define RAS_INDEX_TO_OFFSET(_C, _N) ((_C)->ras_record_offset + \
				     (_N) * RAS_TABLE_RECORD_SIZE)

#define RAS_OFFSET_TO_INDEX(_C, _O) (((_O) - \
				      (_C)->ras_record_offset) / RAS_TABLE_RECORD_SIZE)

/* Given a 0-based relative record index, 0, 1, 2, ..., etc., off
 * of "fri", return the absolute record index off of the end of
 * the table header.
 */
#define RAS_RI_TO_AI(_C, _I) (((_I) + (_C)->ras_fri) % \
			      (_C)->ras_max_record_count)

#define RAS_NUM_RECS(_tbl_hdr)  (((_tbl_hdr)->tbl_size - \
				  RAS_TABLE_HEADER_SIZE) / RAS_TABLE_RECORD_SIZE)

#define to_amdgpu_device(x) (container_of(x, struct amdgpu_ras, eeprom_control))->adev

static bool __is_ras_eeprom_supported(struct amdgpu_device *adev)
{
	return  adev->asic_type == CHIP_VEGA20 ||
		adev->asic_type == CHIP_ARCTURUS ||
		adev->asic_type == CHIP_SIENNA_CICHLID ||
		adev->asic_type == CHIP_ALDEBARAN;
}

static bool __get_eeprom_i2c_addr_arct(struct amdgpu_device *adev,
				       struct amdgpu_ras_eeprom_control *control)
{
	struct atom_context *atom_ctx = adev->mode_info.atom_context;

	if (!control || !atom_ctx)
		return false;

	if (strnstr(atom_ctx->vbios_version,
	            "D342",
		    sizeof(atom_ctx->vbios_version)))
		control->i2c_address = EEPROM_I2C_MADDR_ARCTURUS_D342;
	else
		control->i2c_address = EEPROM_I2C_MADDR_ARCTURUS;

	return true;
}

static bool __get_eeprom_i2c_addr(struct amdgpu_device *adev,
				  struct amdgpu_ras_eeprom_control *control)
{
	u8 i2c_addr;

	if (!control)
		return false;

	if (amdgpu_atomfirmware_ras_rom_addr(adev, &i2c_addr)) {
		/* The address given by VBIOS is an 8-bit, wire-format
		 * address, i.e. the most significant byte.
		 *
		 * Normalize it to a 19-bit EEPROM address. Remove the
		 * device type identifier and make it a 7-bit address;
		 * then make it a 19-bit EEPROM address. See top of
		 * amdgpu_eeprom.c.
		 */
		i2c_addr = (i2c_addr & 0x0F) >> 1;
		control->i2c_address = ((u32) i2c_addr) << 16;

		return true;
	}

	switch (adev->asic_type) {
	case CHIP_VEGA20:
		control->i2c_address = EEPROM_I2C_MADDR_VEGA20;
		break;

	case CHIP_ARCTURUS:
		return __get_eeprom_i2c_addr_arct(adev, control);

	case CHIP_SIENNA_CICHLID:
		control->i2c_address = EEPROM_I2C_MADDR_SIENNA_CICHLID;
		break;

	case CHIP_ALDEBARAN:
		control->i2c_address = EEPROM_I2C_MADDR_ALDEBARAN;
		break;

	default:
		return false;
	}

	return true;
}

static void
__encode_table_header_to_buf(struct amdgpu_ras_eeprom_table_header *hdr,
			     unsigned char *buf)
{
	u32 *pp = (uint32_t *)buf;

	pp[0] = cpu_to_le32(hdr->header);
	pp[1] = cpu_to_le32(hdr->version);
	pp[2] = cpu_to_le32(hdr->first_rec_offset);
	pp[3] = cpu_to_le32(hdr->tbl_size);
	pp[4] = cpu_to_le32(hdr->checksum);
}

static void
__decode_table_header_from_buf(struct amdgpu_ras_eeprom_table_header *hdr,
			       unsigned char *buf)
{
	u32 *pp = (uint32_t *)buf;

	hdr->header	      = le32_to_cpu(pp[0]);
	hdr->version	      = le32_to_cpu(pp[1]);
	hdr->first_rec_offset = le32_to_cpu(pp[2]);
	hdr->tbl_size	      = le32_to_cpu(pp[3]);
	hdr->checksum	      = le32_to_cpu(pp[4]);
}

static int __write_table_header(struct amdgpu_ras_eeprom_control *control)
{
	u8 buf[RAS_TABLE_HEADER_SIZE];
	struct amdgpu_device *adev = to_amdgpu_device(control);
	int res;

	memset(buf, 0, sizeof(buf));
	__encode_table_header_to_buf(&control->tbl_hdr, buf);

	/* i2c may be unstable in gpu reset */
	down_read(&adev->reset_sem);
	res = amdgpu_eeprom_write(adev->pm.ras_eeprom_i2c_bus,
				  control->i2c_address +
				  control->ras_header_offset,
				  buf, RAS_TABLE_HEADER_SIZE);
	up_read(&adev->reset_sem);

	if (res < 0) {
		DRM_ERROR("Failed to write EEPROM table header:%d", res);
	} else if (res < RAS_TABLE_HEADER_SIZE) {
		DRM_ERROR("Short write:%d out of %d\n",
			  res, RAS_TABLE_HEADER_SIZE);
		res = -EIO;
	} else {
		res = 0;
	}

	return res;
}

static u8 __calc_hdr_byte_sum(const struct amdgpu_ras_eeprom_control *control)
{
	int ii;
	u8  *pp, csum;
	size_t sz;

	/* Header checksum, skip checksum field in the calculation */
	sz = sizeof(control->tbl_hdr) - sizeof(control->tbl_hdr.checksum);
	pp = (u8 *) &control->tbl_hdr;
	csum = 0;
	for (ii = 0; ii < sz; ii++, pp++)
		csum += *pp;

	return csum;
}

static int amdgpu_ras_eeprom_correct_header_tag(
	struct amdgpu_ras_eeprom_control *control,
	uint32_t header)
{
	struct amdgpu_ras_eeprom_table_header *hdr = &control->tbl_hdr;
	u8 *hh;
	int res;
	u8 csum;

	csum = -hdr->checksum;

	hh = (void *) &hdr->header;
	csum -= (hh[0] + hh[1] + hh[2] + hh[3]);
	hh = (void *) &header;
	csum += hh[0] + hh[1] + hh[2] + hh[3];
	csum = -csum;
	mutex_lock(&control->ras_tbl_mutex);
	hdr->header = header;
	hdr->checksum = csum;
	res = __write_table_header(control);
	mutex_unlock(&control->ras_tbl_mutex);

	return res;
}

/**
 * amdgpu_ras_eeprom_reset_table -- Reset the RAS EEPROM table
 * @control: pointer to control structure
 *
 * Reset the contents of the header of the RAS EEPROM table.
 * Return 0 on success, -errno on error.
 */
int amdgpu_ras_eeprom_reset_table(struct amdgpu_ras_eeprom_control *control)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	struct amdgpu_ras_eeprom_table_header *hdr = &control->tbl_hdr;
	u8 csum;
	int res;

	mutex_lock(&control->ras_tbl_mutex);

	hdr->header = RAS_TABLE_HDR_VAL;
	hdr->version = RAS_TABLE_VER;
	hdr->first_rec_offset = RAS_RECORD_START;
	hdr->tbl_size = RAS_TABLE_HEADER_SIZE;

	csum = __calc_hdr_byte_sum(control);
	csum = -csum;
	hdr->checksum = csum;
	res = __write_table_header(control);

	control->ras_num_recs = 0;
	control->ras_fri = 0;

	amdgpu_dpm_send_hbm_bad_pages_num(adev, control->ras_num_recs);

	amdgpu_ras_debugfs_set_ret_size(control);

	mutex_unlock(&control->ras_tbl_mutex);

	return res;
}

static void
__encode_table_record_to_buf(struct amdgpu_ras_eeprom_control *control,
			     struct eeprom_table_record *record,
			     unsigned char *buf)
{
	__le64 tmp = 0;
	int i = 0;

	/* Next are all record fields according to EEPROM page spec in LE foramt */
	buf[i++] = record->err_type;

	buf[i++] = record->bank;

	tmp = cpu_to_le64(record->ts);
	memcpy(buf + i, &tmp, 8);
	i += 8;

	tmp = cpu_to_le64((record->offset & 0xffffffffffff));
	memcpy(buf + i, &tmp, 6);
	i += 6;

	buf[i++] = record->mem_channel;
	buf[i++] = record->mcumc_id;

	tmp = cpu_to_le64((record->retired_page & 0xffffffffffff));
	memcpy(buf + i, &tmp, 6);
}

static void
__decode_table_record_from_buf(struct amdgpu_ras_eeprom_control *control,
			       struct eeprom_table_record *record,
			       unsigned char *buf)
{
	__le64 tmp = 0;
	int i =  0;

	/* Next are all record fields according to EEPROM page spec in LE foramt */
	record->err_type = buf[i++];

	record->bank = buf[i++];

	memcpy(&tmp, buf + i, 8);
	record->ts = le64_to_cpu(tmp);
	i += 8;

	memcpy(&tmp, buf + i, 6);
	record->offset = (le64_to_cpu(tmp) & 0xffffffffffff);
	i += 6;

	record->mem_channel = buf[i++];
	record->mcumc_id = buf[i++];

	memcpy(&tmp, buf + i,  6);
	record->retired_page = (le64_to_cpu(tmp) & 0xffffffffffff);
}

bool amdgpu_ras_eeprom_check_err_threshold(struct amdgpu_device *adev)
{
	struct amdgpu_ras *con = amdgpu_ras_get_context(adev);

	if (!__is_ras_eeprom_supported(adev))
		return false;

	/* skip check eeprom table for VEGA20 Gaming */
	if (!con)
		return false;
	else
		if (!(con->features & BIT(AMDGPU_RAS_BLOCK__UMC)))
			return false;

	if (con->eeprom_control.tbl_hdr.header == RAS_TABLE_HDR_BAD) {
		dev_warn(adev->dev, "This GPU is in BAD status.");
		dev_warn(adev->dev, "Please retire it or set a larger "
			 "threshold value when reloading driver.\n");
		return true;
	}

	return false;
}

/**
 * __amdgpu_ras_eeprom_write -- write indexed from buffer to EEPROM
 * @control: pointer to control structure
 * @buf: pointer to buffer containing data to write
 * @fri: start writing at this index
 * @num: number of records to write
 *
 * The caller must hold the table mutex in @control.
 * Return 0 on success, -errno otherwise.
 */
static int __amdgpu_ras_eeprom_write(struct amdgpu_ras_eeprom_control *control,
				     u8 *buf, const u32 fri, const u32 num)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	u32 buf_size;
	int res;

	/* i2c may be unstable in gpu reset */
	down_read(&adev->reset_sem);
	buf_size = num * RAS_TABLE_RECORD_SIZE;
	res = amdgpu_eeprom_write(adev->pm.ras_eeprom_i2c_bus,
				  control->i2c_address +
				  RAS_INDEX_TO_OFFSET(control, fri),
				  buf, buf_size);
	up_read(&adev->reset_sem);
	if (res < 0) {
		DRM_ERROR("Writing %d EEPROM table records error:%d",
			  num, res);
	} else if (res < buf_size) {
		/* Short write, return error.
		 */
		DRM_ERROR("Wrote %d records out of %d",
			  res / RAS_TABLE_RECORD_SIZE, num);
		res = -EIO;
	} else {
		res = 0;
	}

	return res;
}

static int
amdgpu_ras_eeprom_append_table(struct amdgpu_ras_eeprom_control *control,
			       struct eeprom_table_record *record,
			       const u32 num)
{
	u32 a, b, i;
	u8 *buf, *pp;
	int res;

	buf = kcalloc(num, RAS_TABLE_RECORD_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Encode all of them in one go.
	 */
	pp = buf;
	for (i = 0; i < num; i++, pp += RAS_TABLE_RECORD_SIZE)
		__encode_table_record_to_buf(control, &record[i], pp);

	/* a, first record index to write into.
	 * b, last record index to write into.
	 * a = first index to read (fri) + number of records in the table,
	 * b = a + @num - 1.
	 * Let N = control->ras_max_num_record_count, then we have,
	 * case 0: 0 <= a <= b < N,
	 *   just append @num records starting at a;
	 * case 1: 0 <= a < N <= b,
	 *   append (N - a) records starting at a, and
	 *   append the remainder,  b % N + 1, starting at 0.
	 * case 2: 0 <= fri < N <= a <= b, then modulo N we get two subcases,
	 * case 2a: 0 <= a <= b < N
	 *   append num records starting at a; and fix fri if b overwrote it,
	 *   and since a <= b, if b overwrote it then a must've also,
	 *   and if b didn't overwrite it, then a didn't also.
	 * case 2b: 0 <= b < a < N
	 *   write num records starting at a, which wraps around 0=N
	 *   and overwrite fri unconditionally. Now from case 2a,
	 *   this means that b eclipsed fri to overwrite it and wrap
	 *   around 0 again, i.e. b = 2N+r pre modulo N, so we unconditionally
	 *   set fri = b + 1 (mod N).
	 * Now, since fri is updated in every case, except the trivial case 0,
	 * the number of records present in the table after writing, is,
	 * num_recs - 1 = b - fri (mod N), and we take the positive value,
	 * by adding an arbitrary multiple of N before taking the modulo N
	 * as shown below.
	 */
	a = control->ras_fri + control->ras_num_recs;
	b = a + num  - 1;
	if (b < control->ras_max_record_count) {
		res = __amdgpu_ras_eeprom_write(control, buf, a, num);
	} else if (a < control->ras_max_record_count) {
		u32 g0, g1;

		g0 = control->ras_max_record_count - a;
		g1 = b % control->ras_max_record_count + 1;
		res = __amdgpu_ras_eeprom_write(control, buf, a, g0);
		if (res)
			goto Out;
		res = __amdgpu_ras_eeprom_write(control,
						buf + g0 * RAS_TABLE_RECORD_SIZE,
						0, g1);
		if (res)
			goto Out;
		if (g1 > control->ras_fri)
			control->ras_fri = g1 % control->ras_max_record_count;
	} else {
		a %= control->ras_max_record_count;
		b %= control->ras_max_record_count;

		if (a <= b) {
			/* Note that, b - a + 1 = num. */
			res = __amdgpu_ras_eeprom_write(control, buf, a, num);
			if (res)
				goto Out;
			if (b >= control->ras_fri)
				control->ras_fri = (b + 1) % control->ras_max_record_count;
		} else {
			u32 g0, g1;

			/* b < a, which means, we write from
			 * a to the end of the table, and from
			 * the start of the table to b.
			 */
			g0 = control->ras_max_record_count - a;
			g1 = b + 1;
			res = __amdgpu_ras_eeprom_write(control, buf, a, g0);
			if (res)
				goto Out;
			res = __amdgpu_ras_eeprom_write(control,
							buf + g0 * RAS_TABLE_RECORD_SIZE,
							0, g1);
			if (res)
				goto Out;
			control->ras_fri = g1 % control->ras_max_record_count;
		}
	}
	control->ras_num_recs = 1 + (control->ras_max_record_count + b
				     - control->ras_fri)
		% control->ras_max_record_count;
Out:
	kfree(buf);
	return res;
}

static int
amdgpu_ras_eeprom_update_header(struct amdgpu_ras_eeprom_control *control)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);
	u8 *buf, *pp, csum;
	u32 buf_size;
	int res;

	/* Modify the header if it exceeds.
	 */
	if (amdgpu_bad_page_threshold != 0 &&
	    control->ras_num_recs >= ras->bad_page_cnt_threshold) {
		dev_warn(adev->dev,
			"Saved bad pages %d reaches threshold value %d\n",
			control->ras_num_recs, ras->bad_page_cnt_threshold);
		control->tbl_hdr.header = RAS_TABLE_HDR_BAD;
	}

	control->tbl_hdr.version = RAS_TABLE_VER;
	control->tbl_hdr.first_rec_offset = RAS_INDEX_TO_OFFSET(control, control->ras_fri);
	control->tbl_hdr.tbl_size = RAS_TABLE_HEADER_SIZE + control->ras_num_recs * RAS_TABLE_RECORD_SIZE;
	control->tbl_hdr.checksum = 0;

	buf_size = control->ras_num_recs * RAS_TABLE_RECORD_SIZE;
	buf = kcalloc(control->ras_num_recs, RAS_TABLE_RECORD_SIZE, GFP_KERNEL);
	if (!buf) {
		DRM_ERROR("allocating memory for table of size %d bytes failed\n",
			  control->tbl_hdr.tbl_size);
		res = -ENOMEM;
		goto Out;
	}

	down_read(&adev->reset_sem);
	res = amdgpu_eeprom_read(adev->pm.ras_eeprom_i2c_bus,
				 control->i2c_address +
				 control->ras_record_offset,
				 buf, buf_size);
	up_read(&adev->reset_sem);
	if (res < 0) {
		DRM_ERROR("EEPROM failed reading records:%d\n",
			  res);
		goto Out;
	} else if (res < buf_size) {
		DRM_ERROR("EEPROM read %d out of %d bytes\n",
			  res, buf_size);
		res = -EIO;
		goto Out;
	}

	/* Recalc the checksum.
	 */
	csum = 0;
	for (pp = buf; pp < buf + buf_size; pp++)
		csum += *pp;

	csum += __calc_hdr_byte_sum(control);
	/* avoid sign extension when assigning to "checksum" */
	csum = -csum;
	control->tbl_hdr.checksum = csum;
	res = __write_table_header(control);
Out:
	kfree(buf);
	return res;
}

/**
 * amdgpu_ras_eeprom_append -- append records to the EEPROM RAS table
 * @control: pointer to control structure
 * @record: array of records to append
 * @num: number of records in @record array
 *
 * Append @num records to the table, calculate the checksum and write
 * the table back to EEPROM. The maximum number of records that
 * can be appended is between 1 and control->ras_max_record_count,
 * regardless of how many records are already stored in the table.
 *
 * Return 0 on success or if EEPROM is not supported, -errno on error.
 */
int amdgpu_ras_eeprom_append(struct amdgpu_ras_eeprom_control *control,
			     struct eeprom_table_record *record,
			     const u32 num)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	int res;

	if (!__is_ras_eeprom_supported(adev))
		return 0;

	if (num == 0) {
		DRM_ERROR("will not append 0 records\n");
		return -EINVAL;
	} else if (num > control->ras_max_record_count) {
		DRM_ERROR("cannot append %d records than the size of table %d\n",
			  num, control->ras_max_record_count);
		return -EINVAL;
	}

	mutex_lock(&control->ras_tbl_mutex);

	res = amdgpu_ras_eeprom_append_table(control, record, num);
	if (!res)
		res = amdgpu_ras_eeprom_update_header(control);
	if (!res)
		amdgpu_ras_debugfs_set_ret_size(control);

	mutex_unlock(&control->ras_tbl_mutex);
	return res;
}

/**
 * __amdgpu_ras_eeprom_read -- read indexed from EEPROM into buffer
 * @control: pointer to control structure
 * @buf: pointer to buffer to read into
 * @fri: first record index, start reading at this index, absolute index
 * @num: number of records to read
 *
 * The caller must hold the table mutex in @control.
 * Return 0 on success, -errno otherwise.
 */
static int __amdgpu_ras_eeprom_read(struct amdgpu_ras_eeprom_control *control,
				    u8 *buf, const u32 fri, const u32 num)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	u32 buf_size;
	int res;

	/* i2c may be unstable in gpu reset */
	down_read(&adev->reset_sem);
	buf_size = num * RAS_TABLE_RECORD_SIZE;
	res = amdgpu_eeprom_read(adev->pm.ras_eeprom_i2c_bus,
				 control->i2c_address +
				 RAS_INDEX_TO_OFFSET(control, fri),
				 buf, buf_size);
	up_read(&adev->reset_sem);
	if (res < 0) {
		DRM_ERROR("Reading %d EEPROM table records error:%d",
			  num, res);
	} else if (res < buf_size) {
		/* Short read, return error.
		 */
		DRM_ERROR("Read %d records out of %d",
			  res / RAS_TABLE_RECORD_SIZE, num);
		res = -EIO;
	} else {
		res = 0;
	}

	return res;
}

/**
 * amdgpu_ras_eeprom_read -- read EEPROM
 * @control: pointer to control structure
 * @record: array of records to read into
 * @num: number of records in @record
 *
 * Reads num records from the RAS table in EEPROM and
 * writes the data into @record array.
 *
 * Returns 0 on success, -errno on error.
 */
int amdgpu_ras_eeprom_read(struct amdgpu_ras_eeprom_control *control,
			   struct eeprom_table_record *record,
			   const u32 num)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	int i, res;
	u8 *buf, *pp;
	u32 g0, g1;

	if (!__is_ras_eeprom_supported(adev))
		return 0;

	if (num == 0) {
		DRM_ERROR("will not read 0 records\n");
		return -EINVAL;
	} else if (num > control->ras_num_recs) {
		DRM_ERROR("too many records to read:%d available:%d\n",
			  num, control->ras_num_recs);
		return -EINVAL;
	}

	buf = kcalloc(num, RAS_TABLE_RECORD_SIZE, GFP_KERNEL);
	if (!buf)
		return -ENOMEM;

	/* Determine how many records to read, from the first record
	 * index, fri, to the end of the table, and from the beginning
	 * of the table, such that the total number of records is
	 * @num, and we handle wrap around when fri > 0 and
	 * fri + num > RAS_MAX_RECORD_COUNT.
	 *
	 * First we compute the index of the last element
	 * which would be fetched from each region,
	 * g0 is in [fri, fri + num - 1], and
	 * g1 is in [0, RAS_MAX_RECORD_COUNT - 1].
	 * Then, if g0 < RAS_MAX_RECORD_COUNT, the index of
	 * the last element to fetch, we set g0 to _the number_
	 * of elements to fetch, @num, since we know that the last
	 * indexed to be fetched does not exceed the table.
	 *
	 * If, however, g0 >= RAS_MAX_RECORD_COUNT, then
	 * we set g0 to the number of elements to read
	 * until the end of the table, and g1 to the number of
	 * elements to read from the beginning of the table.
	 */
	g0 = control->ras_fri + num - 1;
	g1 = g0 % control->ras_max_record_count;
	if (g0 < control->ras_max_record_count) {
		g0 = num;
		g1 = 0;
	} else {
		g0 = control->ras_max_record_count - control->ras_fri;
		g1 += 1;
	}

	mutex_lock(&control->ras_tbl_mutex);
	res = __amdgpu_ras_eeprom_read(control, buf, control->ras_fri, g0);
	if (res)
		goto Out;
	if (g1) {
		res = __amdgpu_ras_eeprom_read(control,
					       buf + g0 * RAS_TABLE_RECORD_SIZE,
					       0, g1);
		if (res)
			goto Out;
	}

	res = 0;

	/* Read up everything? Then transform.
	 */
	pp = buf;
	for (i = 0; i < num; i++, pp += RAS_TABLE_RECORD_SIZE)
		__decode_table_record_from_buf(control, &record[i], pp);
Out:
	kfree(buf);
	mutex_unlock(&control->ras_tbl_mutex);

	return res;
}

uint32_t amdgpu_ras_eeprom_max_record_count(void)
{
	return RAS_MAX_RECORD_COUNT;
}

static ssize_t
amdgpu_ras_debugfs_eeprom_size_read(struct file *f, char __user *buf,
				    size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)file_inode(f)->i_private;
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);
	struct amdgpu_ras_eeprom_control *control = ras ? &ras->eeprom_control : NULL;
	u8 data[50];
	int res;

	if (!size)
		return size;

	if (!ras || !control) {
		res = snprintf(data, sizeof(data), "Not supported\n");
	} else {
		res = snprintf(data, sizeof(data), "%d bytes or %d records\n",
			       RAS_TBL_SIZE_BYTES, control->ras_max_record_count);
	}

	if (*pos >= res)
		return 0;

	res -= *pos;
	res = min_t(size_t, res, size);

	if (copy_to_user(buf, &data[*pos], res))
		return -EFAULT;

	*pos += res;

	return res;
}

const struct file_operations amdgpu_ras_debugfs_eeprom_size_ops = {
	.owner = THIS_MODULE,
	.read = amdgpu_ras_debugfs_eeprom_size_read,
	.write = NULL,
	.llseek = default_llseek,
};

static const char *tbl_hdr_str = " Signature    Version  FirstOffs       Size   Checksum\n";
static const char *tbl_hdr_fmt = "0x%08X 0x%08X 0x%08X 0x%08X 0x%08X\n";
#define tbl_hdr_fmt_size (5 * (2+8) + 4 + 1)
static const char *rec_hdr_str = "Index  Offset ErrType Bank/CU          TimeStamp      Offs/Addr MemChl MCUMCID    RetiredPage\n";
static const char *rec_hdr_fmt = "%5d 0x%05X %7s    0x%02X 0x%016llX 0x%012llX   0x%02X    0x%02X 0x%012llX\n";
#define rec_hdr_fmt_size (5 + 1 + 7 + 1 + 7 + 1 + 7 + 1 + 18 + 1 + 14 + 1 + 6 + 1 + 7 + 1 + 14 + 1)

static const char *record_err_type_str[AMDGPU_RAS_EEPROM_ERR_COUNT] = {
	"ignore",
	"re",
	"ue",
};

static loff_t amdgpu_ras_debugfs_table_size(struct amdgpu_ras_eeprom_control *control)
{
	return strlen(tbl_hdr_str) + tbl_hdr_fmt_size +
		strlen(rec_hdr_str) + rec_hdr_fmt_size * control->ras_num_recs;
}

void amdgpu_ras_debugfs_set_ret_size(struct amdgpu_ras_eeprom_control *control)
{
	struct amdgpu_ras *ras = container_of(control, struct amdgpu_ras,
					      eeprom_control);
	struct dentry *de = ras->de_ras_eeprom_table;

	if (de)
		d_inode(de)->i_size = amdgpu_ras_debugfs_table_size(control);
}

static ssize_t amdgpu_ras_debugfs_table_read(struct file *f, char __user *buf,
					     size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)file_inode(f)->i_private;
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);
	struct amdgpu_ras_eeprom_control *control = &ras->eeprom_control;
	const size_t orig_size = size;
	int res = -EFAULT;
	size_t data_len;

	mutex_lock(&control->ras_tbl_mutex);

	/* We want *pos - data_len > 0, which means there's
	 * bytes to be printed from data.
	 */
	data_len = strlen(tbl_hdr_str);
	if (*pos < data_len) {
		data_len -= *pos;
		data_len = min_t(size_t, data_len, size);
		if (copy_to_user(buf, &tbl_hdr_str[*pos], data_len))
			goto Out;
		buf += data_len;
		size -= data_len;
		*pos += data_len;
	}

	data_len = strlen(tbl_hdr_str) + tbl_hdr_fmt_size;
	if (*pos < data_len && size > 0) {
		u8 data[tbl_hdr_fmt_size + 1];
		loff_t lpos;

		snprintf(data, sizeof(data), tbl_hdr_fmt,
			 control->tbl_hdr.header,
			 control->tbl_hdr.version,
			 control->tbl_hdr.first_rec_offset,
			 control->tbl_hdr.tbl_size,
			 control->tbl_hdr.checksum);

		data_len -= *pos;
		data_len = min_t(size_t, data_len, size);
		lpos = *pos - strlen(tbl_hdr_str);
		if (copy_to_user(buf, &data[lpos], data_len))
			goto Out;
		buf += data_len;
		size -= data_len;
		*pos += data_len;
	}

	data_len = strlen(tbl_hdr_str) + tbl_hdr_fmt_size + strlen(rec_hdr_str);
	if (*pos < data_len && size > 0) {
		loff_t lpos;

		data_len -= *pos;
		data_len = min_t(size_t, data_len, size);
		lpos = *pos - strlen(tbl_hdr_str) - tbl_hdr_fmt_size;
		if (copy_to_user(buf, &rec_hdr_str[lpos], data_len))
			goto Out;
		buf += data_len;
		size -= data_len;
		*pos += data_len;
	}

	data_len = amdgpu_ras_debugfs_table_size(control);
	if (*pos < data_len && size > 0) {
		u8 dare[RAS_TABLE_RECORD_SIZE];
		u8 data[rec_hdr_fmt_size + 1];
		struct eeprom_table_record record;
		int s, r;

		/* Find the starting record index
		 */
		s = *pos - strlen(tbl_hdr_str) - tbl_hdr_fmt_size -
			strlen(rec_hdr_str);
		s = s / rec_hdr_fmt_size;
		r = *pos - strlen(tbl_hdr_str) - tbl_hdr_fmt_size -
			strlen(rec_hdr_str);
		r = r % rec_hdr_fmt_size;

		for ( ; size > 0 && s < control->ras_num_recs; s++) {
			u32 ai = RAS_RI_TO_AI(control, s);
			/* Read a single record
			 */
			res = __amdgpu_ras_eeprom_read(control, dare, ai, 1);
			if (res)
				goto Out;
			__decode_table_record_from_buf(control, &record, dare);
			snprintf(data, sizeof(data), rec_hdr_fmt,
				 s,
				 RAS_INDEX_TO_OFFSET(control, ai),
				 record_err_type_str[record.err_type],
				 record.bank,
				 record.ts,
				 record.offset,
				 record.mem_channel,
				 record.mcumc_id,
				 record.retired_page);

			data_len = min_t(size_t, rec_hdr_fmt_size - r, size);
			if (copy_to_user(buf, &data[r], data_len)) {
				res = -EFAULT;
				goto Out;
			}
			buf += data_len;
			size -= data_len;
			*pos += data_len;
			r = 0;
		}
	}
	res = 0;
Out:
	mutex_unlock(&control->ras_tbl_mutex);
	return res < 0 ? res : orig_size - size;
}

static ssize_t
amdgpu_ras_debugfs_eeprom_table_read(struct file *f, char __user *buf,
				     size_t size, loff_t *pos)
{
	struct amdgpu_device *adev = (struct amdgpu_device *)file_inode(f)->i_private;
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);
	struct amdgpu_ras_eeprom_control *control = ras ? &ras->eeprom_control : NULL;
	u8 data[81];
	int res;

	if (!size)
		return size;

	if (!ras || !control) {
		res = snprintf(data, sizeof(data), "Not supported\n");
		if (*pos >= res)
			return 0;

		res -= *pos;
		res = min_t(size_t, res, size);

		if (copy_to_user(buf, &data[*pos], res))
			return -EFAULT;

		*pos += res;

		return res;
	} else {
		return amdgpu_ras_debugfs_table_read(f, buf, size, pos);
	}
}

const struct file_operations amdgpu_ras_debugfs_eeprom_table_ops = {
	.owner = THIS_MODULE,
	.read = amdgpu_ras_debugfs_eeprom_table_read,
	.write = NULL,
	.llseek = default_llseek,
};

/**
 * __verify_ras_table_checksum -- verify the RAS EEPROM table checksum
 * @control: pointer to control structure
 *
 * Check the checksum of the stored in EEPROM RAS table.
 *
 * Return 0 if the checksum is correct,
 * positive if it is not correct, and
 * -errno on I/O error.
 */
static int __verify_ras_table_checksum(struct amdgpu_ras_eeprom_control *control)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	int buf_size, res;
	u8  csum, *buf, *pp;

	buf_size = RAS_TABLE_HEADER_SIZE +
		control->ras_num_recs * RAS_TABLE_RECORD_SIZE;
	buf = kzalloc(buf_size, GFP_KERNEL);
	if (!buf) {
		DRM_ERROR("Out of memory checking RAS table checksum.\n");
		return -ENOMEM;
	}

	res = amdgpu_eeprom_read(adev->pm.ras_eeprom_i2c_bus,
				 control->i2c_address +
				 control->ras_header_offset,
				 buf, buf_size);
	if (res < buf_size) {
		DRM_ERROR("Partial read for checksum, res:%d\n", res);
		/* On partial reads, return -EIO.
		 */
		if (res >= 0)
			res = -EIO;
		goto Out;
	}

	csum = 0;
	for (pp = buf; pp < buf + buf_size; pp++)
		csum += *pp;
Out:
	kfree(buf);
	return res < 0 ? res : csum;
}

int amdgpu_ras_eeprom_init(struct amdgpu_ras_eeprom_control *control,
			   bool *exceed_err_limit)
{
	struct amdgpu_device *adev = to_amdgpu_device(control);
	unsigned char buf[RAS_TABLE_HEADER_SIZE] = { 0 };
	struct amdgpu_ras_eeprom_table_header *hdr = &control->tbl_hdr;
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);
	int res;

	*exceed_err_limit = false;

	if (!__is_ras_eeprom_supported(adev))
		return 0;

	/* Verify i2c adapter is initialized */
	if (!adev->pm.ras_eeprom_i2c_bus || !adev->pm.ras_eeprom_i2c_bus->algo)
		return -ENOENT;

	if (!__get_eeprom_i2c_addr(adev, control))
		return -EINVAL;

	control->ras_header_offset = RAS_HDR_START;
	control->ras_record_offset = RAS_RECORD_START;
	control->ras_max_record_count  = RAS_MAX_RECORD_COUNT;
	mutex_init(&control->ras_tbl_mutex);

	/* Read the table header from EEPROM address */
	res = amdgpu_eeprom_read(adev->pm.ras_eeprom_i2c_bus,
				 control->i2c_address + control->ras_header_offset,
				 buf, RAS_TABLE_HEADER_SIZE);
	if (res < RAS_TABLE_HEADER_SIZE) {
		DRM_ERROR("Failed to read EEPROM table header, res:%d", res);
		return res >= 0 ? -EIO : res;
	}

	__decode_table_header_from_buf(hdr, buf);

	control->ras_num_recs = RAS_NUM_RECS(hdr);
	control->ras_fri = RAS_OFFSET_TO_INDEX(control, hdr->first_rec_offset);

	if (hdr->header == RAS_TABLE_HDR_VAL) {
		DRM_DEBUG_DRIVER("Found existing EEPROM table with %d records",
				 control->ras_num_recs);
		res = __verify_ras_table_checksum(control);
		if (res)
			DRM_ERROR("RAS table incorrect checksum or error:%d\n",
				  res);

		/* Warn if we are at 90% of the threshold or above
		 */
		if (10 * control->ras_num_recs >= 9 * ras->bad_page_cnt_threshold)
			dev_warn(adev->dev, "RAS records:%u exceeds 90%% of threshold:%d",
					control->ras_num_recs,
					ras->bad_page_cnt_threshold);
	} else if (hdr->header == RAS_TABLE_HDR_BAD &&
		   amdgpu_bad_page_threshold != 0) {
		res = __verify_ras_table_checksum(control);
		if (res)
			DRM_ERROR("RAS Table incorrect checksum or error:%d\n",
				  res);
		if (ras->bad_page_cnt_threshold > control->ras_num_recs) {
			/* This means that, the threshold was increased since
			 * the last time the system was booted, and now,
			 * ras->bad_page_cnt_threshold - control->num_recs > 0,
			 * so that at least one more record can be saved,
			 * before the page count threshold is reached.
			 */
			dev_info(adev->dev,
				 "records:%d threshold:%d, resetting "
				 "RAS table header signature",
				 control->ras_num_recs,
				 ras->bad_page_cnt_threshold);
			res = amdgpu_ras_eeprom_correct_header_tag(control,
								   RAS_TABLE_HDR_VAL);
		} else {
			dev_err(adev->dev, "RAS records:%d exceed threshold:%d",
				control->ras_num_recs, ras->bad_page_cnt_threshold);
			if (amdgpu_bad_page_threshold == -2) {
				dev_warn(adev->dev, "GPU will be initialized due to bad_page_threshold = -2.");
				res = 0;
			} else {
				*exceed_err_limit = true;
				dev_err(adev->dev,
					"RAS records:%d exceed threshold:%d, "
					"GPU will not be initialized. Replace this GPU or increase the threshold",
					control->ras_num_recs, ras->bad_page_cnt_threshold);
			}
		}
	} else {
		DRM_INFO("Creating a new EEPROM table");

		res = amdgpu_ras_eeprom_reset_table(control);
	}

	return res < 0 ? res : 0;
}
