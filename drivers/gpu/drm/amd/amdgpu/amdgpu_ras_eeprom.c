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
#define RAS_HDR_START           0
#define RAS_RECORD_START        (RAS_HDR_START + RAS_TABLE_HEADER_SIZE)
#define RAS_MAX_RECORD_NUM      ((RAS_TBL_SIZE_BYTES - RAS_TABLE_HEADER_SIZE) \
				 / RAS_TABLE_RECORD_SIZE)

#define to_amdgpu_device(x) (container_of(x, struct amdgpu_ras, eeprom_control))->adev

static bool __is_ras_eeprom_supported(struct amdgpu_device *adev)
{
	if ((adev->asic_type == CHIP_VEGA20) ||
	    (adev->asic_type == CHIP_ARCTURUS) ||
	    (adev->asic_type == CHIP_SIENNA_CICHLID) ||
	    (adev->asic_type == CHIP_ALDEBARAN))
		return true;

	return false;
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
	if (!control)
		return false;

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

static void __encode_table_header_to_buff(struct amdgpu_ras_eeprom_table_header *hdr,
					  unsigned char *buff)
{
	uint32_t *pp = (uint32_t *) buff;

	pp[0] = cpu_to_le32(hdr->header);
	pp[1] = cpu_to_le32(hdr->version);
	pp[2] = cpu_to_le32(hdr->first_rec_offset);
	pp[3] = cpu_to_le32(hdr->tbl_size);
	pp[4] = cpu_to_le32(hdr->checksum);
}

static void __decode_table_header_from_buff(struct amdgpu_ras_eeprom_table_header *hdr,
					  unsigned char *buff)
{
	uint32_t *pp = (uint32_t *)buff;

	hdr->header	      = le32_to_cpu(pp[0]);
	hdr->version	      = le32_to_cpu(pp[1]);
	hdr->first_rec_offset = le32_to_cpu(pp[2]);
	hdr->tbl_size	      = le32_to_cpu(pp[3]);
	hdr->checksum	      = le32_to_cpu(pp[4]);
}

static int __update_table_header(struct amdgpu_ras_eeprom_control *control,
				 unsigned char *buff)
{
	int ret = 0;
	struct amdgpu_device *adev = to_amdgpu_device(control);

	__encode_table_header_to_buff(&control->tbl_hdr, buff);

	/* i2c may be unstable in gpu reset */
	down_read(&adev->reset_sem);
	ret = amdgpu_eeprom_write(&adev->pm.smu_i2c,
				  control->i2c_address + RAS_HDR_START,
				  buff, RAS_TABLE_HEADER_SIZE);
	up_read(&adev->reset_sem);

	if (ret < 1)
		DRM_ERROR("Failed to write EEPROM table header, ret:%d", ret);

	return ret;
}

static uint32_t  __calc_hdr_byte_sum(struct amdgpu_ras_eeprom_control *control)
{
	int i;
	uint32_t tbl_sum = 0;

	/* Header checksum, skip checksum field in the calculation */
	for (i = 0; i < sizeof(control->tbl_hdr) - sizeof(control->tbl_hdr.checksum); i++)
		tbl_sum += *(((unsigned char *)&control->tbl_hdr) + i);

	return tbl_sum;
}

static uint32_t  __calc_recs_byte_sum(struct eeprom_table_record *records,
				      int num)
{
	int i, j;
	uint32_t tbl_sum = 0;

	/* Records checksum */
	for (i = 0; i < num; i++) {
		struct eeprom_table_record *record = &records[i];

		for (j = 0; j < sizeof(*record); j++) {
			tbl_sum += *(((unsigned char *)record) + j);
		}
	}

	return tbl_sum;
}

static inline uint32_t  __calc_tbl_byte_sum(struct amdgpu_ras_eeprom_control *control,
				  struct eeprom_table_record *records, int num)
{
	return __calc_hdr_byte_sum(control) + __calc_recs_byte_sum(records, num);
}

/* Checksum = 256 -((sum of all table entries) mod 256) */
static void __update_tbl_checksum(struct amdgpu_ras_eeprom_control *control,
				  struct eeprom_table_record *records, int num,
				  uint32_t old_hdr_byte_sum)
{
	/*
	 * This will update the table sum with new records.
	 *
	 * TODO: What happens when the EEPROM table is to be wrapped around
	 * and old records from start will get overridden.
	 */

	/* need to recalculate updated header byte sum */
	control->tbl_byte_sum -= old_hdr_byte_sum;
	control->tbl_byte_sum += __calc_tbl_byte_sum(control, records, num);

	control->tbl_hdr.checksum = 256 - (control->tbl_byte_sum % 256);
}

/* table sum mod 256 + checksum must equals 256 */
static bool __validate_tbl_checksum(struct amdgpu_ras_eeprom_control *control,
			    struct eeprom_table_record *records, int num)
{
	control->tbl_byte_sum = __calc_tbl_byte_sum(control, records, num);

	if (control->tbl_hdr.checksum + (control->tbl_byte_sum % 256) != 256) {
		DRM_WARN("Checksum mismatch, checksum: %u ", control->tbl_hdr.checksum);
		return false;
	}

	return true;
}

static int amdgpu_ras_eeprom_correct_header_tag(
				struct amdgpu_ras_eeprom_control *control,
				uint32_t header)
{
	unsigned char buff[RAS_TABLE_HEADER_SIZE];
	struct amdgpu_ras_eeprom_table_header *hdr = &control->tbl_hdr;
	int ret = 0;

	memset(buff, 0, RAS_TABLE_HEADER_SIZE);

	mutex_lock(&control->tbl_mutex);
	hdr->header = header;
	ret = __update_table_header(control, buff);
	mutex_unlock(&control->tbl_mutex);

	return ret;
}

int amdgpu_ras_eeprom_reset_table(struct amdgpu_ras_eeprom_control *control)
{
	unsigned char buff[RAS_TABLE_HEADER_SIZE] = { 0 };
	struct amdgpu_ras_eeprom_table_header *hdr = &control->tbl_hdr;
	int ret = 0;

	mutex_lock(&control->tbl_mutex);

	hdr->header = RAS_TABLE_HDR_VAL;
	hdr->version = RAS_TABLE_VER;
	hdr->first_rec_offset = RAS_RECORD_START;
	hdr->tbl_size = RAS_TABLE_HEADER_SIZE;

	control->tbl_byte_sum = 0;
	__update_tbl_checksum(control, NULL, 0, 0);
	control->next_addr = RAS_RECORD_START;

	ret = __update_table_header(control, buff);

	mutex_unlock(&control->tbl_mutex);

	return ret;

}

int amdgpu_ras_eeprom_init(struct amdgpu_ras_eeprom_control *control,
			   bool *exceed_err_limit)
{
	int ret = 0;
	struct amdgpu_device *adev = to_amdgpu_device(control);
	unsigned char buff[RAS_TABLE_HEADER_SIZE] = { 0 };
	struct amdgpu_ras_eeprom_table_header *hdr = &control->tbl_hdr;
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

	*exceed_err_limit = false;

	if (!__is_ras_eeprom_supported(adev))
		return 0;

	/* Verify i2c adapter is initialized */
	if (!adev->pm.smu_i2c.algo)
		return -ENOENT;

	if (!__get_eeprom_i2c_addr(adev, control))
		return -EINVAL;

	mutex_init(&control->tbl_mutex);

	/* Read/Create table header from EEPROM address 0 */
	ret = amdgpu_eeprom_read(&adev->pm.smu_i2c,
				 control->i2c_address + RAS_HDR_START,
				 buff, RAS_TABLE_HEADER_SIZE);
	if (ret < 1) {
		DRM_ERROR("Failed to read EEPROM table header, ret:%d", ret);
		return ret;
	}

	__decode_table_header_from_buff(hdr, &buff[2]);

	if (hdr->header == RAS_TABLE_HDR_VAL) {
		control->num_recs = (hdr->tbl_size - RAS_TABLE_HEADER_SIZE) /
				    RAS_TABLE_RECORD_SIZE;
		control->tbl_byte_sum = __calc_hdr_byte_sum(control);
		control->next_addr = RAS_RECORD_START;

		DRM_DEBUG_DRIVER("Found existing EEPROM table with %d records",
				 control->num_recs);

	} else if ((hdr->header == RAS_TABLE_HDR_BAD) &&
			(amdgpu_bad_page_threshold != 0)) {
		if (ras->bad_page_cnt_threshold > control->num_recs) {
			dev_info(adev->dev, "Using one valid bigger bad page "
				"threshold and correcting eeprom header tag.\n");
			ret = amdgpu_ras_eeprom_correct_header_tag(control,
							RAS_TABLE_HDR_VAL);
		} else {
			*exceed_err_limit = true;
			dev_err(adev->dev, "Exceeding the bad_page_threshold parameter, "
				"disabling the GPU.\n");
		}
	} else {
		DRM_INFO("Creating new EEPROM table");

		ret = amdgpu_ras_eeprom_reset_table(control);
	}

	return ret > 0 ? 0 : -EIO;
}

static void __encode_table_record_to_buff(struct amdgpu_ras_eeprom_control *control,
					  struct eeprom_table_record *record,
					  unsigned char *buff)
{
	__le64 tmp = 0;
	int i = 0;

	/* Next are all record fields according to EEPROM page spec in LE foramt */
	buff[i++] = record->err_type;

	buff[i++] = record->bank;

	tmp = cpu_to_le64(record->ts);
	memcpy(buff + i, &tmp, 8);
	i += 8;

	tmp = cpu_to_le64((record->offset & 0xffffffffffff));
	memcpy(buff + i, &tmp, 6);
	i += 6;

	buff[i++] = record->mem_channel;
	buff[i++] = record->mcumc_id;

	tmp = cpu_to_le64((record->retired_page & 0xffffffffffff));
	memcpy(buff + i, &tmp, 6);
}

static void __decode_table_record_from_buff(struct amdgpu_ras_eeprom_control *control,
					    struct eeprom_table_record *record,
					    unsigned char *buff)
{
	__le64 tmp = 0;
	int i =  0;

	/* Next are all record fields according to EEPROM page spec in LE foramt */
	record->err_type = buff[i++];

	record->bank = buff[i++];

	memcpy(&tmp, buff + i, 8);
	record->ts = le64_to_cpu(tmp);
	i += 8;

	memcpy(&tmp, buff + i, 6);
	record->offset = (le64_to_cpu(tmp) & 0xffffffffffff);
	i += 6;

	record->mem_channel = buff[i++];
	record->mcumc_id = buff[i++];

	memcpy(&tmp, buff + i,  6);
	record->retired_page = (le64_to_cpu(tmp) & 0xffffffffffff);
}

/*
 * When reaching end of EEPROM memory jump back to 0 record address
 */
static uint32_t __correct_eeprom_dest_address(uint32_t curr_address)
{
	u32 next_address = curr_address + RAS_TABLE_RECORD_SIZE;

	/* When all EEPROM memory used jump back to 0 address */
	if (next_address >= RAS_TBL_SIZE_BYTES) {
		DRM_INFO("Reached end of EEPROM memory, wrap around to 0.");
		return RAS_RECORD_START;
	}

	return curr_address;
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
		dev_warn(adev->dev, "Please retire it or setting one bigger "
				"threshold value when reloading driver.\n");
		return true;
	}

	return false;
}

static int amdgpu_ras_eeprom_xfer(struct amdgpu_ras_eeprom_control *control,
				  struct eeprom_table_record *records,
				  const u32 num, bool write)
{
	int i, ret = 0;
	unsigned char *buffs, *buff;
	struct eeprom_table_record *record;
	struct amdgpu_device *adev = to_amdgpu_device(control);
	struct amdgpu_ras *ras = amdgpu_ras_get_context(adev);

	if (!__is_ras_eeprom_supported(adev))
		return 0;

	buffs = kcalloc(num, RAS_TABLE_RECORD_SIZE, GFP_KERNEL);
	if (!buffs)
		return -ENOMEM;

	mutex_lock(&control->tbl_mutex);

	/*
	 * If saved bad pages number exceeds the bad page threshold for
	 * the whole VRAM, update table header to mark the BAD GPU tag
	 * and schedule one ras recovery after eeprom write is done,
	 * this can avoid the missing for latest records.
	 *
	 * This new header will be picked up and checked in the bootup
	 * by ras recovery, which may break bootup process to notify
	 * user this GPU is in bad state and to retire such GPU for
	 * further check.
	 */
	if (write && (amdgpu_bad_page_threshold != 0) &&
		((control->num_recs + num) >= ras->bad_page_cnt_threshold)) {
		dev_warn(adev->dev,
			"Saved bad pages(%d) reaches threshold value(%d).\n",
			control->num_recs + num, ras->bad_page_cnt_threshold);
		control->tbl_hdr.header = RAS_TABLE_HDR_BAD;
	}

	/* In case of overflow just start from beginning to not lose newest records */
	if (write &&
	    (control->next_addr +
	     RAS_TABLE_RECORD_SIZE * num >= RAS_TBL_SIZE_BYTES))
		control->next_addr = RAS_RECORD_START;

	/*
	 * TODO Currently makes EEPROM writes for each record, this creates
	 * internal fragmentation. Optimized the code to do full page write of
	 * 256b
	 */
	for (i = 0; i < num; i++) {
		buff = &buffs[i * RAS_TABLE_RECORD_SIZE];
		record = &records[i];

		control->next_addr = __correct_eeprom_dest_address(control->next_addr);

		/* EEPROM table content is stored in LE format */
		if (write)
			__encode_table_record_to_buff(control, record, buff);

		/* i2c may be unstable in gpu reset */
		down_read(&adev->reset_sem);
		ret = amdgpu_eeprom_xfer(&adev->pm.smu_i2c,
					 control->i2c_address + control->next_addr,
					 buff, RAS_TABLE_RECORD_SIZE, !write);
		up_read(&adev->reset_sem);

		if (ret < 1) {
			DRM_ERROR("Failed to process EEPROM table records, ret:%d", ret);

			/* TODO Restore prev next EEPROM address ? */
			goto free_buff;
		}
		/*
		 * The destination EEPROM address might need to be corrected to account
		 * for page or entire memory wrapping
		 */
		control->next_addr += RAS_TABLE_RECORD_SIZE;
	}

	if (!write) {
		for (i = 0; i < num; i++) {
			buff = &buffs[i * RAS_TABLE_RECORD_SIZE];
			record = &records[i];

			__decode_table_record_from_buff(control, record, buff);
		}
	}

	if (write) {
		uint32_t old_hdr_byte_sum = __calc_hdr_byte_sum(control);

		/*
		 * Update table header with size and CRC and account for table
		 * wrap around where the assumption is that we treat it as empty
		 * table
		 *
		 * TODO - Check the assumption is correct
		 */
		control->num_recs += num;
		control->num_recs %= RAS_MAX_RECORD_NUM;
		control->tbl_hdr.tbl_size += RAS_TABLE_RECORD_SIZE * num;
		if (control->tbl_hdr.tbl_size > RAS_TBL_SIZE_BYTES)
			control->tbl_hdr.tbl_size = RAS_TABLE_HEADER_SIZE +
			control->num_recs * RAS_TABLE_RECORD_SIZE;

		__update_tbl_checksum(control, records, num, old_hdr_byte_sum);

		__update_table_header(control, buffs);
	} else if (!__validate_tbl_checksum(control, records, num)) {
		DRM_WARN("EEPROM Table checksum mismatch!");
		/* TODO Uncomment when EEPROM read/write is relliable */
		/* ret = -EIO; */
	}

free_buff:
	kfree(buffs);

	mutex_unlock(&control->tbl_mutex);

	return ret == num ? 0 : -EIO;
}

int amdgpu_ras_eeprom_read(struct amdgpu_ras_eeprom_control *control,
			   struct eeprom_table_record *records,
			   const u32 num)
{
	return amdgpu_ras_eeprom_xfer(control, records, num, false);
}

int amdgpu_ras_eeprom_write(struct amdgpu_ras_eeprom_control *control,
			    struct eeprom_table_record *records,
			    const u32 num)
{
	return amdgpu_ras_eeprom_xfer(control, records, num, true);
}

inline uint32_t amdgpu_ras_eeprom_get_record_max_length(void)
{
	return RAS_MAX_RECORD_NUM;
}

/* Used for testing if bugs encountered */
#if 0
void amdgpu_ras_eeprom_test(struct amdgpu_ras_eeprom_control *control)
{
	int i;
	struct eeprom_table_record *recs = kcalloc(1, sizeof(*recs), GFP_KERNEL);

	if (!recs)
		return;

	for (i = 0; i < 1 ; i++) {
		recs[i].address = 0xdeadbeef;
		recs[i].retired_page = i;
	}

	if (!amdgpu_ras_eeprom_write(control, recs, 1)) {

		memset(recs, 0, sizeof(*recs) * 1);

		control->next_addr = RAS_RECORD_START;

		if (!amdgpu_ras_eeprom_read(control, recs)) {
			for (i = 0; i < 1; i++)
				DRM_INFO("rec.address :0x%llx, rec.retired_page :%llu",
					 recs[i].address, recs[i].retired_page);
		} else
			DRM_ERROR("Failed in reading from table");

	} else
		DRM_ERROR("Failed in writing to table");
}
#endif
