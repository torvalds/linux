// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2016, The Linux Foundation. All rights reserved.
 */
#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/dma/qcom_adm.h>
#include <linux/dma/qcom_bam_dma.h>
#include <linux/module.h>
#include <linux/mtd/partitions.h>
#include <linux/mtd/rawnand.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/mtd/nand-qpic-common.h>

/*
 * NAND special boot partitions
 *
 * @page_offset:		offset of the partition where spare data is not protected
 *				by ECC (value in pages)
 * @page_offset:		size of the partition where spare data is not protected
 *				by ECC (value in pages)
 */
struct qcom_nand_boot_partition {
	u32 page_offset;
	u32 page_size;
};

/*
 * Qcom op for each exec_op transfer
 *
 * @data_instr:			data instruction pointer
 * @data_instr_idx:		data instruction index
 * @rdy_timeout_ms:		wait ready timeout in ms
 * @rdy_delay_ns:		Additional delay in ns
 * @addr1_reg:			Address1 register value
 * @addr2_reg:			Address2 register value
 * @cmd_reg:			CMD register value
 * @flag:			flag for misc instruction
 */
struct qcom_op {
	const struct nand_op_instr *data_instr;
	unsigned int data_instr_idx;
	unsigned int rdy_timeout_ms;
	unsigned int rdy_delay_ns;
	__le32 addr1_reg;
	__le32 addr2_reg;
	__le32 cmd_reg;
	u8 flag;
};

/*
 * NAND chip structure
 *
 * @boot_partitions:		array of boot partitions where offset and size of the
 *				boot partitions are stored
 *
 * @chip:			base NAND chip structure
 * @node:			list node to add itself to host_list in
 *				qcom_nand_controller
 *
 * @nr_boot_partitions:		count of the boot partitions where spare data is not
 *				protected by ECC
 *
 * @cs:				chip select value for this chip
 * @cw_size:			the number of bytes in a single step/codeword
 *				of a page, consisting of all data, ecc, spare
 *				and reserved bytes
 * @cw_data:			the number of bytes within a codeword protected
 *				by ECC
 * @ecc_bytes_hw:		ECC bytes used by controller hardware for this
 *				chip
 *
 * @last_command:		keeps track of last command on this chip. used
 *				for reading correct status
 *
 * @cfg0, cfg1, cfg0_raw..:	NANDc register configurations needed for
 *				ecc/non-ecc mode for the current nand flash
 *				device
 *
 * @status:			value to be returned if NAND_CMD_STATUS command
 *				is executed
 * @codeword_fixup:		keep track of the current layout used by
 *				the driver for read/write operation.
 * @use_ecc:			request the controller to use ECC for the
 *				upcoming read/write
 * @bch_enabled:		flag to tell whether BCH ECC mode is used
 */
struct qcom_nand_host {
	struct qcom_nand_boot_partition *boot_partitions;

	struct nand_chip chip;
	struct list_head node;

	int nr_boot_partitions;

	int cs;
	int cw_size;
	int cw_data;
	int ecc_bytes_hw;
	int spare_bytes;
	int bbm_size;

	int last_command;

	u32 cfg0, cfg1;
	u32 cfg0_raw, cfg1_raw;
	u32 ecc_buf_cfg;
	u32 ecc_bch_cfg;
	u32 clrflashstatus;
	u32 clrreadstatus;

	u8 status;
	bool codeword_fixup;
	bool use_ecc;
	bool bch_enabled;
};

static struct qcom_nand_host *to_qcom_nand_host(struct nand_chip *chip)
{
	return container_of(chip, struct qcom_nand_host, chip);
}

static struct qcom_nand_controller *
get_qcom_nand_controller(struct nand_chip *chip)
{
	return (struct qcom_nand_controller *)
		((u8 *)chip->controller - sizeof(struct qcom_nand_controller));
}

static u32 nandc_read(struct qcom_nand_controller *nandc, int offset)
{
	return ioread32(nandc->base + offset);
}

static void nandc_write(struct qcom_nand_controller *nandc, int offset,
			u32 val)
{
	iowrite32(val, nandc->base + offset);
}

/* Helper to check whether this is the last CW or not */
static bool qcom_nandc_is_last_cw(struct nand_ecc_ctrl *ecc, int cw)
{
	return cw == (ecc->steps - 1);
}

/**
 * nandc_set_read_loc_first() - to set read location first register
 * @chip:		NAND Private Flash Chip Data
 * @reg_base:		location register base
 * @cw_offset:		code word offset
 * @read_size:		code word read length
 * @is_last_read_loc:	is this the last read location
 *
 * This function will set location register value
 */
static void nandc_set_read_loc_first(struct nand_chip *chip,
				     int reg_base, u32 cw_offset,
				     u32 read_size, u32 is_last_read_loc)
{
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	__le32 locreg_val;
	u32 val = FIELD_PREP(READ_LOCATION_OFFSET_MASK, cw_offset) |
		  FIELD_PREP(READ_LOCATION_SIZE_MASK, read_size) |
		  FIELD_PREP(READ_LOCATION_LAST_MASK, is_last_read_loc);

	locreg_val = cpu_to_le32(val);

	if (reg_base == NAND_READ_LOCATION_0)
		nandc->regs->read_location0 = locreg_val;
	else if (reg_base == NAND_READ_LOCATION_1)
		nandc->regs->read_location1 = locreg_val;
	else if (reg_base == NAND_READ_LOCATION_2)
		nandc->regs->read_location2 = locreg_val;
	else if (reg_base == NAND_READ_LOCATION_3)
		nandc->regs->read_location3 = locreg_val;
}

/**
 * nandc_set_read_loc_last - to set read location last register
 * @chip:		NAND Private Flash Chip Data
 * @reg_base:		location register base
 * @cw_offset:		code word offset
 * @read_size:		code word read length
 * @is_last_read_loc:	is this the last read location
 *
 * This function will set location last register value
 */
static void nandc_set_read_loc_last(struct nand_chip *chip,
				    int reg_base, u32 cw_offset,
				    u32 read_size, u32 is_last_read_loc)
{
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	__le32 locreg_val;
	u32 val = FIELD_PREP(READ_LOCATION_OFFSET_MASK, cw_offset) |
		  FIELD_PREP(READ_LOCATION_SIZE_MASK, read_size) |
		  FIELD_PREP(READ_LOCATION_LAST_MASK, is_last_read_loc);

	locreg_val = cpu_to_le32(val);

	if (reg_base == NAND_READ_LOCATION_LAST_CW_0)
		nandc->regs->read_location_last0 = locreg_val;
	else if (reg_base == NAND_READ_LOCATION_LAST_CW_1)
		nandc->regs->read_location_last1 = locreg_val;
	else if (reg_base == NAND_READ_LOCATION_LAST_CW_2)
		nandc->regs->read_location_last2 = locreg_val;
	else if (reg_base == NAND_READ_LOCATION_LAST_CW_3)
		nandc->regs->read_location_last3 = locreg_val;
}

/* helper to configure location register values */
static void nandc_set_read_loc(struct nand_chip *chip, int cw, int reg,
			       u32 cw_offset, u32 read_size, u32 is_last_read_loc)
{
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	int reg_base = NAND_READ_LOCATION_0;

	if (nandc->props->qpic_version2 && qcom_nandc_is_last_cw(ecc, cw))
		reg_base = NAND_READ_LOCATION_LAST_CW_0;

	reg_base += reg * 4;

	if (nandc->props->qpic_version2 && qcom_nandc_is_last_cw(ecc, cw))
		return nandc_set_read_loc_last(chip, reg_base, cw_offset,
				read_size, is_last_read_loc);
	else
		return nandc_set_read_loc_first(chip, reg_base, cw_offset,
				read_size, is_last_read_loc);
}

/* helper to configure address register values */
static void set_address(struct qcom_nand_host *host, u16 column, int page)
{
	struct nand_chip *chip = &host->chip;
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);

	if (chip->options & NAND_BUSWIDTH_16)
		column >>= 1;

	nandc->regs->addr0 = cpu_to_le32(page << 16 | column);
	nandc->regs->addr1 = cpu_to_le32(page >> 16 & 0xff);
}

/*
 * update_rw_regs:	set up read/write register values, these will be
 *			written to the NAND controller registers via DMA
 *
 * @num_cw:		number of steps for the read/write operation
 * @read:		read or write operation
 * @cw	:		which code word
 */
static void update_rw_regs(struct qcom_nand_host *host, int num_cw, bool read, int cw)
{
	struct nand_chip *chip = &host->chip;
	__le32 cmd, cfg0, cfg1, ecc_bch_cfg;
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);

	if (read) {
		if (host->use_ecc)
			cmd = cpu_to_le32(OP_PAGE_READ_WITH_ECC | PAGE_ACC | LAST_PAGE);
		else
			cmd = cpu_to_le32(OP_PAGE_READ | PAGE_ACC | LAST_PAGE);
	} else {
		cmd = cpu_to_le32(OP_PROGRAM_PAGE | PAGE_ACC | LAST_PAGE);
	}

	if (host->use_ecc) {
		cfg0 = cpu_to_le32((host->cfg0 & ~CW_PER_PAGE_MASK) |
				   FIELD_PREP(CW_PER_PAGE_MASK, (num_cw - 1)));

		cfg1 = cpu_to_le32(host->cfg1);
		ecc_bch_cfg = cpu_to_le32(host->ecc_bch_cfg);
	} else {
		cfg0 = cpu_to_le32((host->cfg0_raw & ~CW_PER_PAGE_MASK) |
				   FIELD_PREP(CW_PER_PAGE_MASK, (num_cw - 1)));

		cfg1 = cpu_to_le32(host->cfg1_raw);
		ecc_bch_cfg = cpu_to_le32(ECC_CFG_ECC_DISABLE);
	}

	nandc->regs->cmd = cmd;
	nandc->regs->cfg0 = cfg0;
	nandc->regs->cfg1 = cfg1;
	nandc->regs->ecc_bch_cfg = ecc_bch_cfg;

	if (!nandc->props->qpic_version2)
		nandc->regs->ecc_buf_cfg = cpu_to_le32(host->ecc_buf_cfg);

	nandc->regs->clrflashstatus = cpu_to_le32(host->clrflashstatus);
	nandc->regs->clrreadstatus = cpu_to_le32(host->clrreadstatus);
	nandc->regs->exec = cpu_to_le32(1);

	if (read)
		nandc_set_read_loc(chip, cw, 0, 0, host->use_ecc ?
				   host->cw_data : host->cw_size, 1);
}

/*
 * Helper to prepare DMA descriptors for configuring registers
 * before reading a NAND page.
 */
static void config_nand_page_read(struct nand_chip *chip)
{
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);

	qcom_write_reg_dma(nandc, &nandc->regs->addr0, NAND_ADDR0, 2, 0);
	qcom_write_reg_dma(nandc, &nandc->regs->cfg0, NAND_DEV0_CFG0, 3, 0);
	if (!nandc->props->qpic_version2)
		qcom_write_reg_dma(nandc, &nandc->regs->ecc_buf_cfg, NAND_EBI2_ECC_BUF_CFG, 1, 0);
	qcom_write_reg_dma(nandc, &nandc->regs->erased_cw_detect_cfg_clr,
			   NAND_ERASED_CW_DETECT_CFG, 1, 0);
	qcom_write_reg_dma(nandc, &nandc->regs->erased_cw_detect_cfg_set,
			   NAND_ERASED_CW_DETECT_CFG, 1, NAND_ERASED_CW_SET | NAND_BAM_NEXT_SGL);
}

/*
 * Helper to prepare DMA descriptors for configuring registers
 * before reading each codeword in NAND page.
 */
static void
config_nand_cw_read(struct nand_chip *chip, bool use_ecc, int cw)
{
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;

	__le32 *reg = &nandc->regs->read_location0;

	if (nandc->props->qpic_version2 && qcom_nandc_is_last_cw(ecc, cw))
		reg = &nandc->regs->read_location_last0;

	if (nandc->props->supports_bam)
		qcom_write_reg_dma(nandc, reg, NAND_READ_LOCATION_0, 4, NAND_BAM_NEXT_SGL);

	qcom_write_reg_dma(nandc, &nandc->regs->cmd, NAND_FLASH_CMD, 1, NAND_BAM_NEXT_SGL);
	qcom_write_reg_dma(nandc, &nandc->regs->exec, NAND_EXEC_CMD, 1, NAND_BAM_NEXT_SGL);

	if (use_ecc) {
		qcom_read_reg_dma(nandc, NAND_FLASH_STATUS, 2, 0);
		qcom_read_reg_dma(nandc, NAND_ERASED_CW_DETECT_STATUS, 1,
				  NAND_BAM_NEXT_SGL);
	} else {
		qcom_read_reg_dma(nandc, NAND_FLASH_STATUS, 1, NAND_BAM_NEXT_SGL);
	}
}

/*
 * Helper to prepare dma descriptors to configure registers needed for reading a
 * single codeword in page
 */
static void
config_nand_single_cw_page_read(struct nand_chip *chip,
				bool use_ecc, int cw)
{
	config_nand_page_read(chip);
	config_nand_cw_read(chip, use_ecc, cw);
}

/*
 * Helper to prepare DMA descriptors used to configure registers needed for
 * before writing a NAND page.
 */
static void config_nand_page_write(struct nand_chip *chip)
{
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);

	qcom_write_reg_dma(nandc, &nandc->regs->addr0, NAND_ADDR0, 2, 0);
	qcom_write_reg_dma(nandc, &nandc->regs->cfg0, NAND_DEV0_CFG0, 3, 0);
	if (!nandc->props->qpic_version2)
		qcom_write_reg_dma(nandc, &nandc->regs->ecc_buf_cfg, NAND_EBI2_ECC_BUF_CFG, 1,
				   NAND_BAM_NEXT_SGL);
}

/*
 * Helper to prepare DMA descriptors for configuring registers
 * before writing each codeword in NAND page.
 */
static void config_nand_cw_write(struct nand_chip *chip)
{
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);

	qcom_write_reg_dma(nandc, &nandc->regs->cmd, NAND_FLASH_CMD, 1, NAND_BAM_NEXT_SGL);
	qcom_write_reg_dma(nandc, &nandc->regs->exec, NAND_EXEC_CMD, 1, NAND_BAM_NEXT_SGL);

	qcom_read_reg_dma(nandc, NAND_FLASH_STATUS, 1, NAND_BAM_NEXT_SGL);

	qcom_write_reg_dma(nandc, &nandc->regs->clrflashstatus, NAND_FLASH_STATUS, 1, 0);
	qcom_write_reg_dma(nandc, &nandc->regs->clrreadstatus, NAND_READ_STATUS, 1,
			   NAND_BAM_NEXT_SGL);
}

/*
 * when using BCH ECC, the HW flags an error in NAND_FLASH_STATUS if it read
 * an erased CW, and reports an erased CW in NAND_ERASED_CW_DETECT_STATUS.
 *
 * when using RS ECC, the HW reports the same erros when reading an erased CW,
 * but it notifies that it is an erased CW by placing special characters at
 * certain offsets in the buffer.
 *
 * verify if the page is erased or not, and fix up the page for RS ECC by
 * replacing the special characters with 0xff.
 */
static bool erased_chunk_check_and_fixup(u8 *data_buf, int data_len)
{
	u8 empty1, empty2;

	/*
	 * an erased page flags an error in NAND_FLASH_STATUS, check if the page
	 * is erased by looking for 0x54s at offsets 3 and 175 from the
	 * beginning of each codeword
	 */

	empty1 = data_buf[3];
	empty2 = data_buf[175];

	/*
	 * if the erased codework markers, if they exist override them with
	 * 0xffs
	 */
	if ((empty1 == 0x54 && empty2 == 0xff) ||
	    (empty1 == 0xff && empty2 == 0x54)) {
		data_buf[3] = 0xff;
		data_buf[175] = 0xff;
	}

	/*
	 * check if the entire chunk contains 0xffs or not. if it doesn't, then
	 * restore the original values at the special offsets
	 */
	if (memchr_inv(data_buf, 0xff, data_len)) {
		data_buf[3] = empty1;
		data_buf[175] = empty2;

		return false;
	}

	return true;
}

struct read_stats {
	__le32 flash;
	__le32 buffer;
	__le32 erased_cw;
};

/* reads back FLASH_STATUS register set by the controller */
static int check_flash_errors(struct qcom_nand_host *host, int cw_cnt)
{
	struct nand_chip *chip = &host->chip;
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	int i;

	qcom_nandc_dev_to_mem(nandc, true);

	for (i = 0; i < cw_cnt; i++) {
		u32 flash = le32_to_cpu(nandc->reg_read_buf[i]);

		if (flash & (FS_OP_ERR | FS_MPU_ERR))
			return -EIO;
	}

	return 0;
}

/* performs raw read for one codeword */
static int
qcom_nandc_read_cw_raw(struct mtd_info *mtd, struct nand_chip *chip,
		       u8 *data_buf, u8 *oob_buf, int page, int cw)
{
	struct qcom_nand_host *host = to_qcom_nand_host(chip);
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	int data_size1, data_size2, oob_size1, oob_size2;
	int ret, reg_off = FLASH_BUF_ACC, read_loc = 0;
	int raw_cw = cw;

	nand_read_page_op(chip, page, 0, NULL, 0);
	nandc->buf_count = 0;
	nandc->buf_start = 0;
	qcom_clear_read_regs(nandc);
	host->use_ecc = false;

	if (nandc->props->qpic_version2)
		raw_cw = ecc->steps - 1;

	qcom_clear_bam_transaction(nandc);
	set_address(host, host->cw_size * cw, page);
	update_rw_regs(host, 1, true, raw_cw);
	config_nand_page_read(chip);

	data_size1 = mtd->writesize - host->cw_size * (ecc->steps - 1);
	oob_size1 = host->bbm_size;

	if (qcom_nandc_is_last_cw(ecc, cw) && !host->codeword_fixup) {
		data_size2 = ecc->size - data_size1 -
			     ((ecc->steps - 1) * 4);
		oob_size2 = (ecc->steps * 4) + host->ecc_bytes_hw +
			    host->spare_bytes;
	} else {
		data_size2 = host->cw_data - data_size1;
		oob_size2 = host->ecc_bytes_hw + host->spare_bytes;
	}

	if (nandc->props->supports_bam) {
		nandc_set_read_loc(chip, cw, 0, read_loc, data_size1, 0);
		read_loc += data_size1;

		nandc_set_read_loc(chip, cw, 1, read_loc, oob_size1, 0);
		read_loc += oob_size1;

		nandc_set_read_loc(chip, cw, 2, read_loc, data_size2, 0);
		read_loc += data_size2;

		nandc_set_read_loc(chip, cw, 3, read_loc, oob_size2, 1);
	}

	config_nand_cw_read(chip, false, raw_cw);

	qcom_read_data_dma(nandc, reg_off, data_buf, data_size1, 0);
	reg_off += data_size1;

	qcom_read_data_dma(nandc, reg_off, oob_buf, oob_size1, 0);
	reg_off += oob_size1;

	qcom_read_data_dma(nandc, reg_off, data_buf + data_size1, data_size2, 0);
	reg_off += data_size2;

	qcom_read_data_dma(nandc, reg_off, oob_buf + oob_size1, oob_size2, 0);

	ret = qcom_submit_descs(nandc);
	if (ret) {
		dev_err(nandc->dev, "failure to read raw cw %d\n", cw);
		return ret;
	}

	return check_flash_errors(host, 1);
}

/*
 * Bitflips can happen in erased codewords also so this function counts the
 * number of 0 in each CW for which ECC engine returns the uncorrectable
 * error. The page will be assumed as erased if this count is less than or
 * equal to the ecc->strength for each CW.
 *
 * 1. Both DATA and OOB need to be checked for number of 0. The
 *    top-level API can be called with only data buf or OOB buf so use
 *    chip->data_buf if data buf is null and chip->oob_poi if oob buf
 *    is null for copying the raw bytes.
 * 2. Perform raw read for all the CW which has uncorrectable errors.
 * 3. For each CW, check the number of 0 in cw_data and usable OOB bytes.
 *    The BBM and spare bytes bit flip won’t affect the ECC so don’t check
 *    the number of bitflips in this area.
 */
static int
check_for_erased_page(struct qcom_nand_host *host, u8 *data_buf,
		      u8 *oob_buf, unsigned long uncorrectable_cws,
		      int page, unsigned int max_bitflips)
{
	struct nand_chip *chip = &host->chip;
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	u8 *cw_data_buf, *cw_oob_buf;
	int cw, data_size, oob_size, ret;

	if (!data_buf)
		data_buf = nand_get_data_buf(chip);

	if (!oob_buf) {
		nand_get_data_buf(chip);
		oob_buf = chip->oob_poi;
	}

	for_each_set_bit(cw, &uncorrectable_cws, ecc->steps) {
		if (qcom_nandc_is_last_cw(ecc, cw) && !host->codeword_fixup) {
			data_size = ecc->size - ((ecc->steps - 1) * 4);
			oob_size = (ecc->steps * 4) + host->ecc_bytes_hw;
		} else {
			data_size = host->cw_data;
			oob_size = host->ecc_bytes_hw;
		}

		/* determine starting buffer address for current CW */
		cw_data_buf = data_buf + (cw * host->cw_data);
		cw_oob_buf = oob_buf + (cw * ecc->bytes);

		ret = qcom_nandc_read_cw_raw(mtd, chip, cw_data_buf,
					     cw_oob_buf, page, cw);
		if (ret)
			return ret;

		/*
		 * make sure it isn't an erased page reported
		 * as not-erased by HW because of a few bitflips
		 */
		ret = nand_check_erased_ecc_chunk(cw_data_buf, data_size,
						  cw_oob_buf + host->bbm_size,
						  oob_size, NULL,
						  0, ecc->strength);
		if (ret < 0) {
			mtd->ecc_stats.failed++;
		} else {
			mtd->ecc_stats.corrected += ret;
			max_bitflips = max_t(unsigned int, max_bitflips, ret);
		}
	}

	return max_bitflips;
}

/*
 * reads back status registers set by the controller to notify page read
 * errors. this is equivalent to what 'ecc->correct()' would do.
 */
static int parse_read_errors(struct qcom_nand_host *host, u8 *data_buf,
			     u8 *oob_buf, int page)
{
	struct nand_chip *chip = &host->chip;
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	unsigned int max_bitflips = 0, uncorrectable_cws = 0;
	struct read_stats *buf;
	bool flash_op_err = false, erased;
	int i;
	u8 *data_buf_start = data_buf, *oob_buf_start = oob_buf;

	buf = (struct read_stats *)nandc->reg_read_buf;
	qcom_nandc_dev_to_mem(nandc, true);

	for (i = 0; i < ecc->steps; i++, buf++) {
		u32 flash, buffer, erased_cw;
		int data_len, oob_len;

		if (qcom_nandc_is_last_cw(ecc, i)) {
			data_len = ecc->size - ((ecc->steps - 1) << 2);
			oob_len = ecc->steps << 2;
		} else {
			data_len = host->cw_data;
			oob_len = 0;
		}

		flash = le32_to_cpu(buf->flash);
		buffer = le32_to_cpu(buf->buffer);
		erased_cw = le32_to_cpu(buf->erased_cw);

		/*
		 * Check ECC failure for each codeword. ECC failure can
		 * happen in either of the following conditions
		 * 1. If number of bitflips are greater than ECC engine
		 *    capability.
		 * 2. If this codeword contains all 0xff for which erased
		 *    codeword detection check will be done.
		 */
		if ((flash & FS_OP_ERR) && (buffer & BS_UNCORRECTABLE_BIT)) {
			/*
			 * For BCH ECC, ignore erased codeword errors, if
			 * ERASED_CW bits are set.
			 */
			if (host->bch_enabled) {
				erased = (erased_cw & ERASED_CW) == ERASED_CW;
			/*
			 * For RS ECC, HW reports the erased CW by placing
			 * special characters at certain offsets in the buffer.
			 * These special characters will be valid only if
			 * complete page is read i.e. data_buf is not NULL.
			 */
			} else if (data_buf) {
				erased = erased_chunk_check_and_fixup(data_buf,
								      data_len);
			} else {
				erased = false;
			}

			if (!erased)
				uncorrectable_cws |= BIT(i);
		/*
		 * Check if MPU or any other operational error (timeout,
		 * device failure, etc.) happened for this codeword and
		 * make flash_op_err true. If flash_op_err is set, then
		 * EIO will be returned for page read.
		 */
		} else if (flash & (FS_OP_ERR | FS_MPU_ERR)) {
			flash_op_err = true;
		/*
		 * No ECC or operational errors happened. Check the number of
		 * bits corrected and update the ecc_stats.corrected.
		 */
		} else {
			unsigned int stat;

			stat = buffer & BS_CORRECTABLE_ERR_MSK;
			mtd->ecc_stats.corrected += stat;
			max_bitflips = max(max_bitflips, stat);
		}

		if (data_buf)
			data_buf += data_len;
		if (oob_buf)
			oob_buf += oob_len + ecc->bytes;
	}

	if (flash_op_err)
		return -EIO;

	if (!uncorrectable_cws)
		return max_bitflips;

	return check_for_erased_page(host, data_buf_start, oob_buf_start,
				     uncorrectable_cws, page,
				     max_bitflips);
}

/*
 * helper to perform the actual page read operation, used by ecc->read_page(),
 * ecc->read_oob()
 */
static int read_page_ecc(struct qcom_nand_host *host, u8 *data_buf,
			 u8 *oob_buf, int page)
{
	struct nand_chip *chip = &host->chip;
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	u8 *data_buf_start = data_buf, *oob_buf_start = oob_buf;
	int i, ret;

	config_nand_page_read(chip);

	/* queue cmd descs for each codeword */
	for (i = 0; i < ecc->steps; i++) {
		int data_size, oob_size;

		if (qcom_nandc_is_last_cw(ecc, i) && !host->codeword_fixup) {
			data_size = ecc->size - ((ecc->steps - 1) << 2);
			oob_size = (ecc->steps << 2) + host->ecc_bytes_hw +
				   host->spare_bytes;
		} else {
			data_size = host->cw_data;
			oob_size = host->ecc_bytes_hw + host->spare_bytes;
		}

		if (nandc->props->supports_bam) {
			if (data_buf && oob_buf) {
				nandc_set_read_loc(chip, i, 0, 0, data_size, 0);
				nandc_set_read_loc(chip, i, 1, data_size,
						   oob_size, 1);
			} else if (data_buf) {
				nandc_set_read_loc(chip, i, 0, 0, data_size, 1);
			} else {
				nandc_set_read_loc(chip, i, 0, data_size,
						   oob_size, 1);
			}
		}

		config_nand_cw_read(chip, true, i);

		if (data_buf)
			qcom_read_data_dma(nandc, FLASH_BUF_ACC, data_buf,
					   data_size, 0);

		/*
		 * when ecc is enabled, the controller doesn't read the real
		 * or dummy bad block markers in each chunk. To maintain a
		 * consistent layout across RAW and ECC reads, we just
		 * leave the real/dummy BBM offsets empty (i.e, filled with
		 * 0xffs)
		 */
		if (oob_buf) {
			int j;

			for (j = 0; j < host->bbm_size; j++)
				*oob_buf++ = 0xff;

			qcom_read_data_dma(nandc, FLASH_BUF_ACC + data_size,
					   oob_buf, oob_size, 0);
		}

		if (data_buf)
			data_buf += data_size;
		if (oob_buf)
			oob_buf += oob_size;
	}

	ret = qcom_submit_descs(nandc);
	if (ret) {
		dev_err(nandc->dev, "failure to read page/oob\n");
		return ret;
	}

	return parse_read_errors(host, data_buf_start, oob_buf_start, page);
}

/*
 * a helper that copies the last step/codeword of a page (containing free oob)
 * into our local buffer
 */
static int copy_last_cw(struct qcom_nand_host *host, int page)
{
	struct nand_chip *chip = &host->chip;
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	int size;
	int ret;

	qcom_clear_read_regs(nandc);

	size = host->use_ecc ? host->cw_data : host->cw_size;

	/* prepare a clean read buffer */
	memset(nandc->data_buffer, 0xff, size);

	set_address(host, host->cw_size * (ecc->steps - 1), page);
	update_rw_regs(host, 1, true, ecc->steps - 1);

	config_nand_single_cw_page_read(chip, host->use_ecc, ecc->steps - 1);

	qcom_read_data_dma(nandc, FLASH_BUF_ACC, nandc->data_buffer, size, 0);

	ret = qcom_submit_descs(nandc);
	if (ret)
		dev_err(nandc->dev, "failed to copy last codeword\n");

	return ret;
}

static bool qcom_nandc_is_boot_partition(struct qcom_nand_host *host, int page)
{
	struct qcom_nand_boot_partition *boot_partition;
	u32 start, end;
	int i;

	/*
	 * Since the frequent access will be to the non-boot partitions like rootfs,
	 * optimize the page check by:
	 *
	 * 1. Checking if the page lies after the last boot partition.
	 * 2. Checking from the boot partition end.
	 */

	/* First check the last boot partition */
	boot_partition = &host->boot_partitions[host->nr_boot_partitions - 1];
	start = boot_partition->page_offset;
	end = start + boot_partition->page_size;

	/* Page is after the last boot partition end. This is NOT a boot partition */
	if (page > end)
		return false;

	/* Actually check if it's a boot partition */
	if (page < end && page >= start)
		return true;

	/* Check the other boot partitions starting from the second-last partition */
	for (i = host->nr_boot_partitions - 2; i >= 0; i--) {
		boot_partition = &host->boot_partitions[i];
		start = boot_partition->page_offset;
		end = start + boot_partition->page_size;

		if (page < end && page >= start)
			return true;
	}

	return false;
}

static void qcom_nandc_codeword_fixup(struct qcom_nand_host *host, int page)
{
	bool codeword_fixup = qcom_nandc_is_boot_partition(host, page);

	/* Skip conf write if we are already in the correct mode */
	if (codeword_fixup == host->codeword_fixup)
		return;

	host->codeword_fixup = codeword_fixup;

	host->cw_data = codeword_fixup ? 512 : 516;
	host->spare_bytes = host->cw_size - host->ecc_bytes_hw -
			    host->bbm_size - host->cw_data;

	host->cfg0 &= ~(SPARE_SIZE_BYTES_MASK | UD_SIZE_BYTES_MASK);
	host->cfg0 |= FIELD_PREP(SPARE_SIZE_BYTES_MASK, host->spare_bytes) |
		      FIELD_PREP(UD_SIZE_BYTES_MASK, host->cw_data);

	host->ecc_bch_cfg &= ~ECC_NUM_DATA_BYTES_MASK;
	host->ecc_bch_cfg |= FIELD_PREP(ECC_NUM_DATA_BYTES_MASK, host->cw_data);
	host->ecc_buf_cfg = FIELD_PREP(NUM_STEPS_MASK, host->cw_data - 1);
}

/* implements ecc->read_page() */
static int qcom_nandc_read_page(struct nand_chip *chip, u8 *buf,
				int oob_required, int page)
{
	struct qcom_nand_host *host = to_qcom_nand_host(chip);
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	u8 *data_buf, *oob_buf = NULL;

	if (host->nr_boot_partitions)
		qcom_nandc_codeword_fixup(host, page);

	nand_read_page_op(chip, page, 0, NULL, 0);
	nandc->buf_count = 0;
	nandc->buf_start = 0;
	host->use_ecc = true;
	qcom_clear_read_regs(nandc);
	set_address(host, 0, page);
	update_rw_regs(host, ecc->steps, true, 0);

	data_buf = buf;
	oob_buf = oob_required ? chip->oob_poi : NULL;

	qcom_clear_bam_transaction(nandc);

	return read_page_ecc(host, data_buf, oob_buf, page);
}

/* implements ecc->read_page_raw() */
static int qcom_nandc_read_page_raw(struct nand_chip *chip, u8 *buf,
				    int oob_required, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct qcom_nand_host *host = to_qcom_nand_host(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	int cw, ret;
	u8 *data_buf = buf, *oob_buf = chip->oob_poi;

	if (host->nr_boot_partitions)
		qcom_nandc_codeword_fixup(host, page);

	for (cw = 0; cw < ecc->steps; cw++) {
		ret = qcom_nandc_read_cw_raw(mtd, chip, data_buf, oob_buf,
					     page, cw);
		if (ret)
			return ret;

		data_buf += host->cw_data;
		oob_buf += ecc->bytes;
	}

	return 0;
}

/* implements ecc->read_oob() */
static int qcom_nandc_read_oob(struct nand_chip *chip, int page)
{
	struct qcom_nand_host *host = to_qcom_nand_host(chip);
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;

	if (host->nr_boot_partitions)
		qcom_nandc_codeword_fixup(host, page);

	qcom_clear_read_regs(nandc);
	qcom_clear_bam_transaction(nandc);

	host->use_ecc = true;
	set_address(host, 0, page);
	update_rw_regs(host, ecc->steps, true, 0);

	return read_page_ecc(host, NULL, chip->oob_poi, page);
}

/* implements ecc->write_page() */
static int qcom_nandc_write_page(struct nand_chip *chip, const u8 *buf,
				 int oob_required, int page)
{
	struct qcom_nand_host *host = to_qcom_nand_host(chip);
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	u8 *data_buf, *oob_buf;
	int i, ret;

	if (host->nr_boot_partitions)
		qcom_nandc_codeword_fixup(host, page);

	nand_prog_page_begin_op(chip, page, 0, NULL, 0);

	set_address(host, 0, page);
	nandc->buf_count = 0;
	nandc->buf_start = 0;
	qcom_clear_read_regs(nandc);
	qcom_clear_bam_transaction(nandc);

	data_buf = (u8 *)buf;
	oob_buf = chip->oob_poi;

	host->use_ecc = true;
	update_rw_regs(host, ecc->steps, false, 0);
	config_nand_page_write(chip);

	for (i = 0; i < ecc->steps; i++) {
		int data_size, oob_size;

		if (qcom_nandc_is_last_cw(ecc, i) && !host->codeword_fixup) {
			data_size = ecc->size - ((ecc->steps - 1) << 2);
			oob_size = (ecc->steps << 2) + host->ecc_bytes_hw +
				   host->spare_bytes;
		} else {
			data_size = host->cw_data;
			oob_size = ecc->bytes;
		}

		qcom_write_data_dma(nandc, FLASH_BUF_ACC, data_buf, data_size,
				    i == (ecc->steps - 1) ? NAND_BAM_NO_EOT : 0);

		/*
		 * when ECC is enabled, we don't really need to write anything
		 * to oob for the first n - 1 codewords since these oob regions
		 * just contain ECC bytes that's written by the controller
		 * itself. For the last codeword, we skip the bbm positions and
		 * write to the free oob area.
		 */
		if (qcom_nandc_is_last_cw(ecc, i)) {
			oob_buf += host->bbm_size;

			qcom_write_data_dma(nandc, FLASH_BUF_ACC + data_size,
					    oob_buf, oob_size, 0);
		}

		config_nand_cw_write(chip);

		data_buf += data_size;
		oob_buf += oob_size;
	}

	ret = qcom_submit_descs(nandc);
	if (ret) {
		dev_err(nandc->dev, "failure to write page\n");
		return ret;
	}

	return nand_prog_page_end_op(chip);
}

/* implements ecc->write_page_raw() */
static int qcom_nandc_write_page_raw(struct nand_chip *chip,
				     const u8 *buf, int oob_required,
				     int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct qcom_nand_host *host = to_qcom_nand_host(chip);
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	u8 *data_buf, *oob_buf;
	int i, ret;

	if (host->nr_boot_partitions)
		qcom_nandc_codeword_fixup(host, page);

	nand_prog_page_begin_op(chip, page, 0, NULL, 0);
	qcom_clear_read_regs(nandc);
	qcom_clear_bam_transaction(nandc);

	data_buf = (u8 *)buf;
	oob_buf = chip->oob_poi;

	host->use_ecc = false;
	update_rw_regs(host, ecc->steps, false, 0);
	config_nand_page_write(chip);

	for (i = 0; i < ecc->steps; i++) {
		int data_size1, data_size2, oob_size1, oob_size2;
		int reg_off = FLASH_BUF_ACC;

		data_size1 = mtd->writesize - host->cw_size * (ecc->steps - 1);
		oob_size1 = host->bbm_size;

		if (qcom_nandc_is_last_cw(ecc, i) && !host->codeword_fixup) {
			data_size2 = ecc->size - data_size1 -
				     ((ecc->steps - 1) << 2);
			oob_size2 = (ecc->steps << 2) + host->ecc_bytes_hw +
				    host->spare_bytes;
		} else {
			data_size2 = host->cw_data - data_size1;
			oob_size2 = host->ecc_bytes_hw + host->spare_bytes;
		}

		qcom_write_data_dma(nandc, reg_off, data_buf, data_size1,
				    NAND_BAM_NO_EOT);
		reg_off += data_size1;
		data_buf += data_size1;

		qcom_write_data_dma(nandc, reg_off, oob_buf, oob_size1,
				    NAND_BAM_NO_EOT);
		reg_off += oob_size1;
		oob_buf += oob_size1;

		qcom_write_data_dma(nandc, reg_off, data_buf, data_size2,
				    NAND_BAM_NO_EOT);
		reg_off += data_size2;
		data_buf += data_size2;

		qcom_write_data_dma(nandc, reg_off, oob_buf, oob_size2, 0);
		oob_buf += oob_size2;

		config_nand_cw_write(chip);
	}

	ret = qcom_submit_descs(nandc);
	if (ret) {
		dev_err(nandc->dev, "failure to write raw page\n");
		return ret;
	}

	return nand_prog_page_end_op(chip);
}

/*
 * implements ecc->write_oob()
 *
 * the NAND controller cannot write only data or only OOB within a codeword
 * since ECC is calculated for the combined codeword. So update the OOB from
 * chip->oob_poi, and pad the data area with OxFF before writing.
 */
static int qcom_nandc_write_oob(struct nand_chip *chip, int page)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct qcom_nand_host *host = to_qcom_nand_host(chip);
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	u8 *oob = chip->oob_poi;
	int data_size, oob_size;
	int ret;

	if (host->nr_boot_partitions)
		qcom_nandc_codeword_fixup(host, page);

	host->use_ecc = true;
	qcom_clear_bam_transaction(nandc);

	/* calculate the data and oob size for the last codeword/step */
	data_size = ecc->size - ((ecc->steps - 1) << 2);
	oob_size = mtd->oobavail;

	memset(nandc->data_buffer, 0xff, host->cw_data);
	/* override new oob content to last codeword */
	mtd_ooblayout_get_databytes(mtd, nandc->data_buffer + data_size, oob,
				    0, mtd->oobavail);

	set_address(host, host->cw_size * (ecc->steps - 1), page);
	update_rw_regs(host, 1, false, 0);

	config_nand_page_write(chip);
	qcom_write_data_dma(nandc, FLASH_BUF_ACC,
			    nandc->data_buffer, data_size + oob_size, 0);
	config_nand_cw_write(chip);

	ret = qcom_submit_descs(nandc);
	if (ret) {
		dev_err(nandc->dev, "failure to write oob\n");
		return ret;
	}

	return nand_prog_page_end_op(chip);
}

static int qcom_nandc_block_bad(struct nand_chip *chip, loff_t ofs)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct qcom_nand_host *host = to_qcom_nand_host(chip);
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	int page, ret, bbpos, bad = 0;

	page = (int)(ofs >> chip->page_shift) & chip->pagemask;

	/*
	 * configure registers for a raw sub page read, the address is set to
	 * the beginning of the last codeword, we don't care about reading ecc
	 * portion of oob. we just want the first few bytes from this codeword
	 * that contains the BBM
	 */
	host->use_ecc = false;

	qcom_clear_bam_transaction(nandc);
	ret = copy_last_cw(host, page);
	if (ret)
		goto err;

	if (check_flash_errors(host, 1)) {
		dev_warn(nandc->dev, "error when trying to read BBM\n");
		goto err;
	}

	bbpos = mtd->writesize - host->cw_size * (ecc->steps - 1);

	bad = nandc->data_buffer[bbpos] != 0xff;

	if (chip->options & NAND_BUSWIDTH_16)
		bad = bad || (nandc->data_buffer[bbpos + 1] != 0xff);
err:
	return bad;
}

static int qcom_nandc_block_markbad(struct nand_chip *chip, loff_t ofs)
{
	struct qcom_nand_host *host = to_qcom_nand_host(chip);
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	int page, ret;

	qcom_clear_read_regs(nandc);
	qcom_clear_bam_transaction(nandc);

	/*
	 * to mark the BBM as bad, we flash the entire last codeword with 0s.
	 * we don't care about the rest of the content in the codeword since
	 * we aren't going to use this block again
	 */
	memset(nandc->data_buffer, 0x00, host->cw_size);

	page = (int)(ofs >> chip->page_shift) & chip->pagemask;

	/* prepare write */
	host->use_ecc = false;
	set_address(host, host->cw_size * (ecc->steps - 1), page);
	update_rw_regs(host, 1, false, ecc->steps - 1);

	config_nand_page_write(chip);
	qcom_write_data_dma(nandc, FLASH_BUF_ACC,
			    nandc->data_buffer, host->cw_size, 0);
	config_nand_cw_write(chip);

	ret = qcom_submit_descs(nandc);
	if (ret) {
		dev_err(nandc->dev, "failure to update BBM\n");
		return ret;
	}

	return nand_prog_page_end_op(chip);
}

/*
 * NAND controller page layout info
 *
 * Layout with ECC enabled:
 *
 * |----------------------|  |---------------------------------|
 * |           xx.......yy|  |             *********xx.......yy|
 * |    DATA   xx..ECC..yy|  |    DATA     **SPARE**xx..ECC..yy|
 * |   (516)   xx.......yy|  |  (516-n*4)  **(n*4)**xx.......yy|
 * |           xx.......yy|  |             *********xx.......yy|
 * |----------------------|  |---------------------------------|
 *     codeword 1,2..n-1                  codeword n
 *  <---(528/532 Bytes)-->    <-------(528/532 Bytes)--------->
 *
 * n = Number of codewords in the page
 * . = ECC bytes
 * * = Spare/free bytes
 * x = Unused byte(s)
 * y = Reserved byte(s)
 *
 * 2K page: n = 4, spare = 16 bytes
 * 4K page: n = 8, spare = 32 bytes
 * 8K page: n = 16, spare = 64 bytes
 *
 * the qcom nand controller operates at a sub page/codeword level. each
 * codeword is 528 and 532 bytes for 4 bit and 8 bit ECC modes respectively.
 * the number of ECC bytes vary based on the ECC strength and the bus width.
 *
 * the first n - 1 codewords contains 516 bytes of user data, the remaining
 * 12/16 bytes consist of ECC and reserved data. The nth codeword contains
 * both user data and spare(oobavail) bytes that sum up to 516 bytes.
 *
 * When we access a page with ECC enabled, the reserved bytes(s) are not
 * accessible at all. When reading, we fill up these unreadable positions
 * with 0xffs. When writing, the controller skips writing the inaccessible
 * bytes.
 *
 * Layout with ECC disabled:
 *
 * |------------------------------|  |---------------------------------------|
 * |         yy          xx.......|  |         bb          *********xx.......|
 * |  DATA1  yy  DATA2   xx..ECC..|  |  DATA1  bb  DATA2   **SPARE**xx..ECC..|
 * | (size1) yy (size2)  xx.......|  | (size1) bb (size2)  **(n*4)**xx.......|
 * |         yy          xx.......|  |         bb          *********xx.......|
 * |------------------------------|  |---------------------------------------|
 *         codeword 1,2..n-1                        codeword n
 *  <-------(528/532 Bytes)------>    <-----------(528/532 Bytes)----------->
 *
 * n = Number of codewords in the page
 * . = ECC bytes
 * * = Spare/free bytes
 * x = Unused byte(s)
 * y = Dummy Bad Bock byte(s)
 * b = Real Bad Block byte(s)
 * size1/size2 = function of codeword size and 'n'
 *
 * when the ECC block is disabled, one reserved byte (or two for 16 bit bus
 * width) is now accessible. For the first n - 1 codewords, these are dummy Bad
 * Block Markers. In the last codeword, this position contains the real BBM
 *
 * In order to have a consistent layout between RAW and ECC modes, we assume
 * the following OOB layout arrangement:
 *
 * |-----------|  |--------------------|
 * |yyxx.......|  |bb*********xx.......|
 * |yyxx..ECC..|  |bb*FREEOOB*xx..ECC..|
 * |yyxx.......|  |bb*********xx.......|
 * |yyxx.......|  |bb*********xx.......|
 * |-----------|  |--------------------|
 *  first n - 1       nth OOB region
 *  OOB regions
 *
 * n = Number of codewords in the page
 * . = ECC bytes
 * * = FREE OOB bytes
 * y = Dummy bad block byte(s) (inaccessible when ECC enabled)
 * x = Unused byte(s)
 * b = Real bad block byte(s) (inaccessible when ECC enabled)
 *
 * This layout is read as is when ECC is disabled. When ECC is enabled, the
 * inaccessible Bad Block byte(s) are ignored when we write to a page/oob,
 * and assumed as 0xffs when we read a page/oob. The ECC, unused and
 * dummy/real bad block bytes are grouped as ecc bytes (i.e, ecc->bytes is
 * the sum of the three).
 */
static int qcom_nand_ooblayout_ecc(struct mtd_info *mtd, int section,
				   struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct qcom_nand_host *host = to_qcom_nand_host(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;

	if (section > 1)
		return -ERANGE;

	if (!section) {
		oobregion->length = (ecc->bytes * (ecc->steps - 1)) +
				    host->bbm_size;
		oobregion->offset = 0;
	} else {
		oobregion->length = host->ecc_bytes_hw + host->spare_bytes;
		oobregion->offset = mtd->oobsize - oobregion->length;
	}

	return 0;
}

static int qcom_nand_ooblayout_free(struct mtd_info *mtd, int section,
				    struct mtd_oob_region *oobregion)
{
	struct nand_chip *chip = mtd_to_nand(mtd);
	struct qcom_nand_host *host = to_qcom_nand_host(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;

	if (section)
		return -ERANGE;

	oobregion->length = ecc->steps * 4;
	oobregion->offset = ((ecc->steps - 1) * ecc->bytes) + host->bbm_size;

	return 0;
}

static const struct mtd_ooblayout_ops qcom_nand_ooblayout_ops = {
	.ecc = qcom_nand_ooblayout_ecc,
	.free = qcom_nand_ooblayout_free,
};

static int
qcom_nandc_calc_ecc_bytes(int step_size, int strength)
{
	return strength == 4 ? 12 : 16;
}

NAND_ECC_CAPS_SINGLE(qcom_nandc_ecc_caps, qcom_nandc_calc_ecc_bytes,
		     NANDC_STEP_SIZE, 4, 8);

static int qcom_nand_attach_chip(struct nand_chip *chip)
{
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct qcom_nand_host *host = to_qcom_nand_host(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	int cwperpage, bad_block_byte, ret;
	bool wide_bus;
	int ecc_mode = ECC_MODE_8BIT;

	/* controller only supports 512 bytes data steps */
	ecc->size = NANDC_STEP_SIZE;
	wide_bus = chip->options & NAND_BUSWIDTH_16 ? true : false;
	cwperpage = mtd->writesize / NANDC_STEP_SIZE;

	/*
	 * Each CW has 4 available OOB bytes which will be protected with ECC
	 * so remaining bytes can be used for ECC.
	 */
	ret = nand_ecc_choose_conf(chip, &qcom_nandc_ecc_caps,
				   mtd->oobsize - (cwperpage * 4));
	if (ret) {
		dev_err(nandc->dev, "No valid ECC settings possible\n");
		return ret;
	}

	if (ecc->strength >= 8) {
		/* 8 bit ECC defaults to BCH ECC on all platforms */
		host->bch_enabled = true;
		ecc_mode = ECC_MODE_8BIT;

		if (wide_bus) {
			host->ecc_bytes_hw = 14;
			host->spare_bytes = 0;
			host->bbm_size = 2;
		} else {
			host->ecc_bytes_hw = 13;
			host->spare_bytes = 2;
			host->bbm_size = 1;
		}
	} else {
		/*
		 * if the controller supports BCH for 4 bit ECC, the controller
		 * uses lesser bytes for ECC. If RS is used, the ECC bytes is
		 * always 10 bytes
		 */
		if (nandc->props->ecc_modes & ECC_BCH_4BIT) {
			/* BCH */
			host->bch_enabled = true;
			ecc_mode = ECC_MODE_4BIT;

			if (wide_bus) {
				host->ecc_bytes_hw = 8;
				host->spare_bytes = 2;
				host->bbm_size = 2;
			} else {
				host->ecc_bytes_hw = 7;
				host->spare_bytes = 4;
				host->bbm_size = 1;
			}
		} else {
			/* RS */
			host->ecc_bytes_hw = 10;

			if (wide_bus) {
				host->spare_bytes = 0;
				host->bbm_size = 2;
			} else {
				host->spare_bytes = 1;
				host->bbm_size = 1;
			}
		}
	}

	/*
	 * we consider ecc->bytes as the sum of all the non-data content in a
	 * step. It gives us a clean representation of the oob area (even if
	 * all the bytes aren't used for ECC).It is always 16 bytes for 8 bit
	 * ECC and 12 bytes for 4 bit ECC
	 */
	ecc->bytes = host->ecc_bytes_hw + host->spare_bytes + host->bbm_size;

	ecc->read_page		= qcom_nandc_read_page;
	ecc->read_page_raw	= qcom_nandc_read_page_raw;
	ecc->read_oob		= qcom_nandc_read_oob;
	ecc->write_page		= qcom_nandc_write_page;
	ecc->write_page_raw	= qcom_nandc_write_page_raw;
	ecc->write_oob		= qcom_nandc_write_oob;

	ecc->engine_type = NAND_ECC_ENGINE_TYPE_ON_HOST;

	mtd_set_ooblayout(mtd, &qcom_nand_ooblayout_ops);
	/* Free the initially allocated BAM transaction for reading the ONFI params */
	if (nandc->props->supports_bam)
		qcom_free_bam_transaction(nandc);

	nandc->max_cwperpage = max_t(unsigned int, nandc->max_cwperpage,
				     cwperpage);

	/* Now allocate the BAM transaction based on updated max_cwperpage */
	if (nandc->props->supports_bam) {
		nandc->bam_txn = qcom_alloc_bam_transaction(nandc);
		if (!nandc->bam_txn) {
			dev_err(nandc->dev,
				"failed to allocate bam transaction\n");
			return -ENOMEM;
		}
	}

	/*
	 * DATA_UD_BYTES varies based on whether the read/write command protects
	 * spare data with ECC too. We protect spare data by default, so we set
	 * it to main + spare data, which are 512 and 4 bytes respectively.
	 */
	host->cw_data = 516;

	/*
	 * total bytes in a step, either 528 bytes for 4 bit ECC, or 532 bytes
	 * for 8 bit ECC
	 */
	host->cw_size = host->cw_data + ecc->bytes;
	bad_block_byte = mtd->writesize - host->cw_size * (cwperpage - 1) + 1;

	host->cfg0 = FIELD_PREP(CW_PER_PAGE_MASK, (cwperpage - 1)) |
		     FIELD_PREP(UD_SIZE_BYTES_MASK, host->cw_data) |
		     FIELD_PREP(DISABLE_STATUS_AFTER_WRITE, 0) |
		     FIELD_PREP(NUM_ADDR_CYCLES_MASK, 5) |
		     FIELD_PREP(ECC_PARITY_SIZE_BYTES_RS, host->ecc_bytes_hw) |
		     FIELD_PREP(STATUS_BFR_READ, 0) |
		     FIELD_PREP(SET_RD_MODE_AFTER_STATUS, 1) |
		     FIELD_PREP(SPARE_SIZE_BYTES_MASK, host->spare_bytes);

	host->cfg1 = FIELD_PREP(NAND_RECOVERY_CYCLES_MASK, 7) |
		     FIELD_PREP(BAD_BLOCK_BYTE_NUM_MASK, bad_block_byte) |
		     FIELD_PREP(BAD_BLOCK_IN_SPARE_AREA, 0) |
		     FIELD_PREP(WR_RD_BSY_GAP_MASK, 2) |
		     FIELD_PREP(WIDE_FLASH, wide_bus) |
		     FIELD_PREP(ENABLE_BCH_ECC, host->bch_enabled);

	host->cfg0_raw = FIELD_PREP(CW_PER_PAGE_MASK, (cwperpage - 1)) |
			 FIELD_PREP(UD_SIZE_BYTES_MASK, host->cw_size) |
			 FIELD_PREP(NUM_ADDR_CYCLES_MASK, 5) |
			 FIELD_PREP(SPARE_SIZE_BYTES_MASK, 0);

	host->cfg1_raw = FIELD_PREP(NAND_RECOVERY_CYCLES_MASK, 7) |
			 FIELD_PREP(CS_ACTIVE_BSY, 0) |
			 FIELD_PREP(BAD_BLOCK_BYTE_NUM_MASK, 17) |
			 FIELD_PREP(BAD_BLOCK_IN_SPARE_AREA, 1) |
			 FIELD_PREP(WR_RD_BSY_GAP_MASK, 2) |
			 FIELD_PREP(WIDE_FLASH, wide_bus) |
			 FIELD_PREP(DEV0_CFG1_ECC_DISABLE, 1);

	host->ecc_bch_cfg = FIELD_PREP(ECC_CFG_ECC_DISABLE, !host->bch_enabled) |
			    FIELD_PREP(ECC_SW_RESET, 0) |
			    FIELD_PREP(ECC_NUM_DATA_BYTES_MASK, host->cw_data) |
			    FIELD_PREP(ECC_FORCE_CLK_OPEN, 1) |
			    FIELD_PREP(ECC_MODE_MASK, ecc_mode) |
			    FIELD_PREP(ECC_PARITY_SIZE_BYTES_BCH_MASK, host->ecc_bytes_hw);

	if (!nandc->props->qpic_version2)
		host->ecc_buf_cfg = FIELD_PREP(NUM_STEPS_MASK, 0x203);

	host->clrflashstatus = FS_READY_BSY_N;
	host->clrreadstatus = 0xc0;
	nandc->regs->erased_cw_detect_cfg_clr =
		cpu_to_le32(CLR_ERASED_PAGE_DET);
	nandc->regs->erased_cw_detect_cfg_set =
		cpu_to_le32(SET_ERASED_PAGE_DET);

	dev_dbg(nandc->dev,
		"cfg0 %x cfg1 %x ecc_buf_cfg %x ecc_bch cfg %x cw_size %d cw_data %d strength %d parity_bytes %d steps %d\n",
		host->cfg0, host->cfg1, host->ecc_buf_cfg, host->ecc_bch_cfg,
		host->cw_size, host->cw_data, ecc->strength, ecc->bytes,
		cwperpage);

	return 0;
}

static int qcom_op_cmd_mapping(struct nand_chip *chip, u8 opcode,
			       struct qcom_op *q_op)
{
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	struct qcom_nand_host *host = to_qcom_nand_host(chip);
	int cmd;

	switch (opcode) {
	case NAND_CMD_RESET:
		cmd = OP_RESET_DEVICE;
		break;
	case NAND_CMD_READID:
		cmd = OP_FETCH_ID;
		break;
	case NAND_CMD_PARAM:
		if (nandc->props->qpic_version2)
			cmd = OP_PAGE_READ_ONFI_READ;
		else
			cmd = OP_PAGE_READ;
		break;
	case NAND_CMD_ERASE1:
	case NAND_CMD_ERASE2:
		cmd = OP_BLOCK_ERASE;
		break;
	case NAND_CMD_STATUS:
		cmd = OP_CHECK_STATUS;
		break;
	case NAND_CMD_PAGEPROG:
		cmd = OP_PROGRAM_PAGE;
		q_op->flag = OP_PROGRAM_PAGE;
		nandc->exec_opwrite = true;
		break;
	case NAND_CMD_READ0:
	case NAND_CMD_READSTART:
		if (host->use_ecc)
			cmd = OP_PAGE_READ_WITH_ECC;
		else
			cmd = OP_PAGE_READ;
		break;
	default:
		dev_err(nandc->dev, "Opcode not supported: %u\n", opcode);
		return -EOPNOTSUPP;
	}

	return cmd;
}

/* NAND framework ->exec_op() hooks and related helpers */
static int qcom_parse_instructions(struct nand_chip *chip,
				    const struct nand_subop *subop,
				    struct qcom_op *q_op)
{
	const struct nand_op_instr *instr = NULL;
	unsigned int op_id;
	int i, ret;

	for (op_id = 0; op_id < subop->ninstrs; op_id++) {
		unsigned int offset, naddrs;
		const u8 *addrs;

		instr = &subop->instrs[op_id];

		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			ret = qcom_op_cmd_mapping(chip, instr->ctx.cmd.opcode, q_op);
			if (ret < 0)
				return ret;

			q_op->cmd_reg = cpu_to_le32(ret);
			q_op->rdy_delay_ns = instr->delay_ns;
			break;

		case NAND_OP_ADDR_INSTR:
			offset = nand_subop_get_addr_start_off(subop, op_id);
			naddrs = nand_subop_get_num_addr_cyc(subop, op_id);
			addrs = &instr->ctx.addr.addrs[offset];

			for (i = 0; i < min_t(unsigned int, 4, naddrs); i++)
				q_op->addr1_reg |= cpu_to_le32(addrs[i] << (i * 8));

			if (naddrs > 4)
				q_op->addr2_reg |= cpu_to_le32(addrs[4]);

			q_op->rdy_delay_ns = instr->delay_ns;
			break;

		case NAND_OP_DATA_IN_INSTR:
			q_op->data_instr = instr;
			q_op->data_instr_idx = op_id;
			q_op->rdy_delay_ns = instr->delay_ns;
			fallthrough;
		case NAND_OP_DATA_OUT_INSTR:
			q_op->rdy_delay_ns = instr->delay_ns;
			break;

		case NAND_OP_WAITRDY_INSTR:
			q_op->rdy_timeout_ms = instr->ctx.waitrdy.timeout_ms;
			q_op->rdy_delay_ns = instr->delay_ns;
			break;
		}
	}

	return 0;
}

static void qcom_delay_ns(unsigned int ns)
{
	if (!ns)
		return;

	if (ns < 10000)
		ndelay(ns);
	else
		udelay(DIV_ROUND_UP(ns, 1000));
}

static int qcom_wait_rdy_poll(struct nand_chip *chip, unsigned int time_ms)
{
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	unsigned long start = jiffies + msecs_to_jiffies(time_ms);
	u32 flash;

	qcom_nandc_dev_to_mem(nandc, true);

	do {
		flash = le32_to_cpu(nandc->reg_read_buf[0]);
		if (flash & FS_READY_BSY_N)
			return 0;
		cpu_relax();
	} while (time_after(start, jiffies));

	dev_err(nandc->dev, "Timeout waiting for device to be ready:0x%08x\n", flash);

	return -ETIMEDOUT;
}

static int qcom_read_status_exec(struct nand_chip *chip,
				 const struct nand_subop *subop)
{
	struct qcom_nand_host *host = to_qcom_nand_host(chip);
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	struct nand_ecc_ctrl *ecc = &chip->ecc;
	struct qcom_op q_op = {};
	const struct nand_op_instr *instr = NULL;
	unsigned int op_id = 0;
	unsigned int len = 0;
	int ret, num_cw, i;
	u32 flash_status;

	host->status = NAND_STATUS_READY | NAND_STATUS_WP;

	ret = qcom_parse_instructions(chip, subop, &q_op);
	if (ret)
		return ret;

	num_cw = nandc->exec_opwrite ? ecc->steps : 1;
	nandc->exec_opwrite = false;

	nandc->buf_count = 0;
	nandc->buf_start = 0;
	host->use_ecc = false;

	qcom_clear_read_regs(nandc);
	qcom_clear_bam_transaction(nandc);

	nandc->regs->cmd = q_op.cmd_reg;
	nandc->regs->exec = cpu_to_le32(1);

	qcom_write_reg_dma(nandc, &nandc->regs->cmd, NAND_FLASH_CMD, 1, NAND_BAM_NEXT_SGL);
	qcom_write_reg_dma(nandc, &nandc->regs->exec, NAND_EXEC_CMD, 1, NAND_BAM_NEXT_SGL);
	qcom_read_reg_dma(nandc, NAND_FLASH_STATUS, 1, NAND_BAM_NEXT_SGL);

	ret = qcom_submit_descs(nandc);
	if (ret) {
		dev_err(nandc->dev, "failure in submitting status descriptor\n");
		goto err_out;
	}

	qcom_nandc_dev_to_mem(nandc, true);

	for (i = 0; i < num_cw; i++) {
		flash_status = le32_to_cpu(nandc->reg_read_buf[i]);

		if (flash_status & FS_MPU_ERR)
			host->status &= ~NAND_STATUS_WP;

		if (flash_status & FS_OP_ERR ||
		    (i == (num_cw - 1) && (flash_status & FS_DEVICE_STS_ERR)))
			host->status |= NAND_STATUS_FAIL;
	}

	flash_status = host->status;
	instr = q_op.data_instr;
	op_id = q_op.data_instr_idx;
	len = nand_subop_get_data_len(subop, op_id);
	memcpy(instr->ctx.data.buf.in, &flash_status, len);

err_out:
	return ret;
}

static int qcom_read_id_type_exec(struct nand_chip *chip, const struct nand_subop *subop)
{
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	struct qcom_nand_host *host = to_qcom_nand_host(chip);
	struct qcom_op q_op = {};
	const struct nand_op_instr *instr = NULL;
	unsigned int op_id = 0;
	unsigned int len = 0;
	int ret;

	ret = qcom_parse_instructions(chip, subop, &q_op);
	if (ret)
		return ret;

	nandc->buf_count = 0;
	nandc->buf_start = 0;
	host->use_ecc = false;

	qcom_clear_read_regs(nandc);
	qcom_clear_bam_transaction(nandc);

	nandc->regs->cmd = q_op.cmd_reg;
	nandc->regs->addr0 = q_op.addr1_reg;
	nandc->regs->addr1 = q_op.addr2_reg;
	nandc->regs->chip_sel = cpu_to_le32(nandc->props->supports_bam ? 0 : DM_EN);
	nandc->regs->exec = cpu_to_le32(1);

	qcom_write_reg_dma(nandc, &nandc->regs->cmd, NAND_FLASH_CMD, 4, NAND_BAM_NEXT_SGL);
	qcom_write_reg_dma(nandc, &nandc->regs->exec, NAND_EXEC_CMD, 1, NAND_BAM_NEXT_SGL);

	qcom_read_reg_dma(nandc, NAND_READ_ID, 1, NAND_BAM_NEXT_SGL);

	ret = qcom_submit_descs(nandc);
	if (ret) {
		dev_err(nandc->dev, "failure in submitting read id descriptor\n");
		goto err_out;
	}

	instr = q_op.data_instr;
	op_id = q_op.data_instr_idx;
	len = nand_subop_get_data_len(subop, op_id);

	qcom_nandc_dev_to_mem(nandc, true);
	memcpy(instr->ctx.data.buf.in, nandc->reg_read_buf, len);

err_out:
	return ret;
}

static int qcom_misc_cmd_type_exec(struct nand_chip *chip, const struct nand_subop *subop)
{
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	struct qcom_nand_host *host = to_qcom_nand_host(chip);
	struct qcom_op q_op = {};
	int ret;
	int instrs = 1;

	ret = qcom_parse_instructions(chip, subop, &q_op);
	if (ret)
		return ret;

	if (q_op.flag == OP_PROGRAM_PAGE) {
		goto wait_rdy;
	} else if (q_op.cmd_reg == cpu_to_le32(OP_BLOCK_ERASE)) {
		q_op.cmd_reg |= cpu_to_le32(PAGE_ACC | LAST_PAGE);
		nandc->regs->addr0 = q_op.addr1_reg;
		nandc->regs->addr1 = q_op.addr2_reg;
		nandc->regs->cfg0 = cpu_to_le32(host->cfg0_raw & ~CW_PER_PAGE_MASK);
		nandc->regs->cfg1 = cpu_to_le32(host->cfg1_raw);
		instrs = 3;
	} else if (q_op.cmd_reg != cpu_to_le32(OP_RESET_DEVICE)) {
		return 0;
	}

	nandc->buf_count = 0;
	nandc->buf_start = 0;
	host->use_ecc = false;

	qcom_clear_read_regs(nandc);
	qcom_clear_bam_transaction(nandc);

	nandc->regs->cmd = q_op.cmd_reg;
	nandc->regs->exec = cpu_to_le32(1);

	qcom_write_reg_dma(nandc, &nandc->regs->cmd, NAND_FLASH_CMD, instrs, NAND_BAM_NEXT_SGL);
	if (q_op.cmd_reg == cpu_to_le32(OP_BLOCK_ERASE))
		qcom_write_reg_dma(nandc, &nandc->regs->cfg0, NAND_DEV0_CFG0, 2, NAND_BAM_NEXT_SGL);

	qcom_write_reg_dma(nandc, &nandc->regs->exec, NAND_EXEC_CMD, 1, NAND_BAM_NEXT_SGL);
	qcom_read_reg_dma(nandc, NAND_FLASH_STATUS, 1, NAND_BAM_NEXT_SGL);

	ret = qcom_submit_descs(nandc);
	if (ret) {
		dev_err(nandc->dev, "failure in submitting misc descriptor\n");
		goto err_out;
	}

wait_rdy:
	qcom_delay_ns(q_op.rdy_delay_ns);
	ret = qcom_wait_rdy_poll(chip, q_op.rdy_timeout_ms);

err_out:
	return ret;
}

static int qcom_param_page_type_exec(struct nand_chip *chip,  const struct nand_subop *subop)
{
	struct qcom_nand_host *host = to_qcom_nand_host(chip);
	struct qcom_nand_controller *nandc = get_qcom_nand_controller(chip);
	struct qcom_op q_op = {};
	const struct nand_op_instr *instr = NULL;
	unsigned int op_id = 0;
	unsigned int len = 0;
	int ret, reg_base;

	reg_base = NAND_READ_LOCATION_0;

	if (nandc->props->qpic_version2)
		reg_base = NAND_READ_LOCATION_LAST_CW_0;

	ret = qcom_parse_instructions(chip, subop, &q_op);
	if (ret)
		return ret;

	q_op.cmd_reg |= cpu_to_le32(PAGE_ACC | LAST_PAGE);

	nandc->buf_count = 0;
	nandc->buf_start = 0;
	host->use_ecc = false;
	qcom_clear_read_regs(nandc);
	qcom_clear_bam_transaction(nandc);

	nandc->regs->cmd = q_op.cmd_reg;
	nandc->regs->addr0 = 0;
	nandc->regs->addr1 = 0;

	nandc->regs->cfg0 = cpu_to_le32(FIELD_PREP(CW_PER_PAGE_MASK, 0) |
					FIELD_PREP(UD_SIZE_BYTES_MASK, 512) |
					FIELD_PREP(NUM_ADDR_CYCLES_MASK, 5) |
					FIELD_PREP(SPARE_SIZE_BYTES_MASK, 0));

	nandc->regs->cfg1 = cpu_to_le32(FIELD_PREP(NAND_RECOVERY_CYCLES_MASK, 7) |
					FIELD_PREP(BAD_BLOCK_BYTE_NUM_MASK, 17) |
					FIELD_PREP(CS_ACTIVE_BSY, 0) |
					FIELD_PREP(BAD_BLOCK_IN_SPARE_AREA, 1) |
					FIELD_PREP(WR_RD_BSY_GAP_MASK, 2) |
					FIELD_PREP(WIDE_FLASH, 0) |
					FIELD_PREP(DEV0_CFG1_ECC_DISABLE, 1));

	if (!nandc->props->qpic_version2)
		nandc->regs->ecc_buf_cfg = cpu_to_le32(ECC_CFG_ECC_DISABLE);

	/* configure CMD1 and VLD for ONFI param probing in QPIC v1 */
	if (!nandc->props->qpic_version2) {
		nandc->regs->vld = cpu_to_le32((nandc->vld & ~READ_START_VLD));
		nandc->regs->cmd1 = cpu_to_le32((nandc->cmd1 & ~READ_ADDR_MASK) |
						FIELD_PREP(READ_ADDR_MASK, NAND_CMD_PARAM));
	}

	nandc->regs->exec = cpu_to_le32(1);

	if (!nandc->props->qpic_version2) {
		nandc->regs->orig_cmd1 = cpu_to_le32(nandc->cmd1);
		nandc->regs->orig_vld = cpu_to_le32(nandc->vld);
	}

	instr = q_op.data_instr;
	op_id = q_op.data_instr_idx;
	len = nand_subop_get_data_len(subop, op_id);

	if (nandc->props->qpic_version2)
		nandc_set_read_loc_last(chip, reg_base, 0, len, 1);
	else
		nandc_set_read_loc_first(chip, reg_base, 0, len, 1);

	if (!nandc->props->qpic_version2) {
		qcom_write_reg_dma(nandc, &nandc->regs->vld, NAND_DEV_CMD_VLD, 1, 0);
		qcom_write_reg_dma(nandc, &nandc->regs->cmd1, NAND_DEV_CMD1, 1, NAND_BAM_NEXT_SGL);
	}

	nandc->buf_count = 512;
	memset(nandc->data_buffer, 0xff, nandc->buf_count);

	config_nand_single_cw_page_read(chip, false, 0);

	qcom_read_data_dma(nandc, FLASH_BUF_ACC, nandc->data_buffer,
			   nandc->buf_count, 0);

	/* restore CMD1 and VLD regs */
	if (!nandc->props->qpic_version2) {
		qcom_write_reg_dma(nandc, &nandc->regs->orig_cmd1, NAND_DEV_CMD1_RESTORE, 1, 0);
		qcom_write_reg_dma(nandc, &nandc->regs->orig_vld, NAND_DEV_CMD_VLD_RESTORE, 1,
				   NAND_BAM_NEXT_SGL);
	}

	ret = qcom_submit_descs(nandc);
	if (ret) {
		dev_err(nandc->dev, "failure in submitting param page descriptor\n");
		goto err_out;
	}

	ret = qcom_wait_rdy_poll(chip, q_op.rdy_timeout_ms);
	if (ret)
		goto err_out;

	memcpy(instr->ctx.data.buf.in, nandc->data_buffer, len);

err_out:
	return ret;
}

static const struct nand_op_parser qcom_op_parser = NAND_OP_PARSER(
		NAND_OP_PARSER_PATTERN(
			qcom_read_id_type_exec,
			NAND_OP_PARSER_PAT_CMD_ELEM(false),
			NAND_OP_PARSER_PAT_ADDR_ELEM(false, MAX_ADDRESS_CYCLE),
			NAND_OP_PARSER_PAT_DATA_IN_ELEM(false, 8)),
		NAND_OP_PARSER_PATTERN(
			qcom_read_status_exec,
			NAND_OP_PARSER_PAT_CMD_ELEM(false),
			NAND_OP_PARSER_PAT_DATA_IN_ELEM(false, 1)),
		NAND_OP_PARSER_PATTERN(
			qcom_param_page_type_exec,
			NAND_OP_PARSER_PAT_CMD_ELEM(false),
			NAND_OP_PARSER_PAT_ADDR_ELEM(false, MAX_ADDRESS_CYCLE),
			NAND_OP_PARSER_PAT_WAITRDY_ELEM(true),
			NAND_OP_PARSER_PAT_DATA_IN_ELEM(false, 512)),
		NAND_OP_PARSER_PATTERN(
			qcom_misc_cmd_type_exec,
			NAND_OP_PARSER_PAT_CMD_ELEM(false),
			NAND_OP_PARSER_PAT_ADDR_ELEM(true, MAX_ADDRESS_CYCLE),
			NAND_OP_PARSER_PAT_CMD_ELEM(true),
			NAND_OP_PARSER_PAT_WAITRDY_ELEM(false)),
		);

static int qcom_check_op(struct nand_chip *chip,
			 const struct nand_operation *op)
{
	const struct nand_op_instr *instr;
	int op_id;

	for (op_id = 0; op_id < op->ninstrs; op_id++) {
		instr = &op->instrs[op_id];

		switch (instr->type) {
		case NAND_OP_CMD_INSTR:
			if (instr->ctx.cmd.opcode != NAND_CMD_RESET  &&
			    instr->ctx.cmd.opcode != NAND_CMD_READID &&
			    instr->ctx.cmd.opcode != NAND_CMD_PARAM  &&
			    instr->ctx.cmd.opcode != NAND_CMD_ERASE1 &&
			    instr->ctx.cmd.opcode != NAND_CMD_ERASE2 &&
			    instr->ctx.cmd.opcode != NAND_CMD_STATUS &&
			    instr->ctx.cmd.opcode != NAND_CMD_PAGEPROG &&
			    instr->ctx.cmd.opcode != NAND_CMD_READ0 &&
			    instr->ctx.cmd.opcode != NAND_CMD_READSTART)
				return -EOPNOTSUPP;
			break;
		default:
			break;
		}
	}

	return 0;
}

static int qcom_nand_exec_op(struct nand_chip *chip,
			     const struct nand_operation *op, bool check_only)
{
	if (check_only)
		return qcom_check_op(chip, op);

	return nand_op_parser_exec_op(chip, &qcom_op_parser, op, check_only);
}

static const struct nand_controller_ops qcom_nandc_ops = {
	.attach_chip = qcom_nand_attach_chip,
	.exec_op = qcom_nand_exec_op,
};

/* one time setup of a few nand controller registers */
static int qcom_nandc_setup(struct qcom_nand_controller *nandc)
{
	u32 nand_ctrl;

	nand_controller_init(nandc->controller);
	nandc->controller->ops = &qcom_nandc_ops;

	/* kill onenand */
	if (!nandc->props->nandc_part_of_qpic)
		nandc_write(nandc, SFLASHC_BURST_CFG, 0);

	if (!nandc->props->qpic_version2)
		nandc_write(nandc, dev_cmd_reg_addr(nandc, NAND_DEV_CMD_VLD),
			    NAND_DEV_CMD_VLD_VAL);

	/* enable ADM or BAM DMA */
	if (nandc->props->supports_bam) {
		nand_ctrl = nandc_read(nandc, NAND_CTRL);

		/*
		 *NAND_CTRL is an operational registers, and CPU
		 * access to operational registers are read only
		 * in BAM mode. So update the NAND_CTRL register
		 * only if it is not in BAM mode. In most cases BAM
		 * mode will be enabled in bootloader
		 */
		if (!(nand_ctrl & BAM_MODE_EN))
			nandc_write(nandc, NAND_CTRL, nand_ctrl | BAM_MODE_EN);
	} else {
		nandc_write(nandc, NAND_FLASH_CHIP_SELECT, DM_EN);
	}

	/* save the original values of these registers */
	if (!nandc->props->qpic_version2) {
		nandc->cmd1 = nandc_read(nandc, dev_cmd_reg_addr(nandc, NAND_DEV_CMD1));
		nandc->vld = NAND_DEV_CMD_VLD_VAL;
	}

	return 0;
}

static const char * const probes[] = { "cmdlinepart", "ofpart", "qcomsmem", NULL };

static int qcom_nand_host_parse_boot_partitions(struct qcom_nand_controller *nandc,
						struct qcom_nand_host *host,
						struct device_node *dn)
{
	struct nand_chip *chip = &host->chip;
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct qcom_nand_boot_partition *boot_partition;
	struct device *dev = nandc->dev;
	int partitions_count, i, j, ret;

	if (!of_property_present(dn, "qcom,boot-partitions"))
		return 0;

	partitions_count = of_property_count_u32_elems(dn, "qcom,boot-partitions");
	if (partitions_count <= 0) {
		dev_err(dev, "Error parsing boot partition\n");
		return partitions_count ? partitions_count : -EINVAL;
	}

	host->nr_boot_partitions = partitions_count / 2;
	host->boot_partitions = devm_kcalloc(dev, host->nr_boot_partitions,
					     sizeof(*host->boot_partitions), GFP_KERNEL);
	if (!host->boot_partitions) {
		host->nr_boot_partitions = 0;
		return -ENOMEM;
	}

	for (i = 0, j = 0; i < host->nr_boot_partitions; i++, j += 2) {
		boot_partition = &host->boot_partitions[i];

		ret = of_property_read_u32_index(dn, "qcom,boot-partitions", j,
						 &boot_partition->page_offset);
		if (ret) {
			dev_err(dev, "Error parsing boot partition offset at index %d\n", i);
			host->nr_boot_partitions = 0;
			return ret;
		}

		if (boot_partition->page_offset % mtd->writesize) {
			dev_err(dev, "Boot partition offset not multiple of writesize at index %i\n",
				i);
			host->nr_boot_partitions = 0;
			return -EINVAL;
		}
		/* Convert offset to nand pages */
		boot_partition->page_offset /= mtd->writesize;

		ret = of_property_read_u32_index(dn, "qcom,boot-partitions", j + 1,
						 &boot_partition->page_size);
		if (ret) {
			dev_err(dev, "Error parsing boot partition size at index %d\n", i);
			host->nr_boot_partitions = 0;
			return ret;
		}

		if (boot_partition->page_size % mtd->writesize) {
			dev_err(dev, "Boot partition size not multiple of writesize at index %i\n",
				i);
			host->nr_boot_partitions = 0;
			return -EINVAL;
		}
		/* Convert size to nand pages */
		boot_partition->page_size /= mtd->writesize;
	}

	return 0;
}

static int qcom_nand_host_init_and_register(struct qcom_nand_controller *nandc,
					    struct qcom_nand_host *host,
					    struct device_node *dn)
{
	struct nand_chip *chip = &host->chip;
	struct mtd_info *mtd = nand_to_mtd(chip);
	struct device *dev = nandc->dev;
	int ret;

	ret = of_property_read_u32(dn, "reg", &host->cs);
	if (ret) {
		dev_err(dev, "can't get chip-select\n");
		return -ENXIO;
	}

	nand_set_flash_node(chip, dn);
	mtd->name = devm_kasprintf(dev, GFP_KERNEL, "qcom_nand.%d", host->cs);
	if (!mtd->name)
		return -ENOMEM;

	mtd->owner = THIS_MODULE;
	mtd->dev.parent = dev;

	/*
	 * the bad block marker is readable only when we read the last codeword
	 * of a page with ECC disabled. currently, the nand_base and nand_bbt
	 * helpers don't allow us to read BB from a nand chip with ECC
	 * disabled (MTD_OPS_PLACE_OOB is set by default). use the block_bad
	 * and block_markbad helpers until we permanently switch to using
	 * MTD_OPS_RAW for all drivers (with the help of badblockbits)
	 */
	chip->legacy.block_bad		= qcom_nandc_block_bad;
	chip->legacy.block_markbad	= qcom_nandc_block_markbad;

	chip->controller = nandc->controller;
	chip->options |= NAND_NO_SUBPAGE_WRITE | NAND_USES_DMA |
			 NAND_SKIP_BBTSCAN;

	/* set up initial status value */
	host->status = NAND_STATUS_READY | NAND_STATUS_WP;

	ret = nand_scan(chip, 1);
	if (ret)
		return ret;

	ret = mtd_device_parse_register(mtd, probes, NULL, NULL, 0);
	if (ret)
		goto err;

	if (nandc->props->use_codeword_fixup) {
		ret = qcom_nand_host_parse_boot_partitions(nandc, host, dn);
		if (ret)
			goto err;
	}

	return 0;

err:
	nand_cleanup(chip);
	return ret;
}

static int qcom_probe_nand_devices(struct qcom_nand_controller *nandc)
{
	struct device *dev = nandc->dev;
	struct device_node *dn = dev->of_node, *child;
	struct qcom_nand_host *host;
	int ret = -ENODEV;

	for_each_available_child_of_node(dn, child) {
		host = devm_kzalloc(dev, sizeof(*host), GFP_KERNEL);
		if (!host) {
			of_node_put(child);
			return -ENOMEM;
		}

		ret = qcom_nand_host_init_and_register(nandc, host, child);
		if (ret) {
			devm_kfree(dev, host);
			continue;
		}

		list_add_tail(&host->node, &nandc->host_list);
	}

	return ret;
}

/* parse custom DT properties here */
static int qcom_nandc_parse_dt(struct platform_device *pdev)
{
	struct qcom_nand_controller *nandc = platform_get_drvdata(pdev);
	struct device_node *np = nandc->dev->of_node;
	int ret;

	if (!nandc->props->supports_bam) {
		ret = of_property_read_u32(np, "qcom,cmd-crci",
					   &nandc->cmd_crci);
		if (ret) {
			dev_err(nandc->dev, "command CRCI unspecified\n");
			return ret;
		}

		ret = of_property_read_u32(np, "qcom,data-crci",
					   &nandc->data_crci);
		if (ret) {
			dev_err(nandc->dev, "data CRCI unspecified\n");
			return ret;
		}
	}

	return 0;
}

static int qcom_nandc_probe(struct platform_device *pdev)
{
	struct qcom_nand_controller *nandc;
	struct nand_controller *controller;
	const void *dev_data;
	struct device *dev = &pdev->dev;
	struct resource *res;
	int ret;

	nandc = devm_kzalloc(&pdev->dev, sizeof(*nandc) + sizeof(*controller),
			     GFP_KERNEL);
	if (!nandc)
		return -ENOMEM;
	controller = (struct nand_controller *)&nandc[1];

	platform_set_drvdata(pdev, nandc);
	nandc->dev = dev;
	nandc->controller = controller;

	dev_data = of_device_get_match_data(dev);
	if (!dev_data) {
		dev_err(&pdev->dev, "failed to get device data\n");
		return -ENODEV;
	}

	nandc->props = dev_data;

	nandc->core_clk = devm_clk_get(dev, "core");
	if (IS_ERR(nandc->core_clk))
		return PTR_ERR(nandc->core_clk);

	nandc->aon_clk = devm_clk_get(dev, "aon");
	if (IS_ERR(nandc->aon_clk))
		return PTR_ERR(nandc->aon_clk);

	ret = qcom_nandc_parse_dt(pdev);
	if (ret)
		return ret;

	nandc->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(nandc->base))
		return PTR_ERR(nandc->base);

	nandc->base_phys = res->start;
	nandc->base_dma = dma_map_resource(dev, res->start,
					   resource_size(res),
					   DMA_BIDIRECTIONAL, 0);
	if (dma_mapping_error(dev, nandc->base_dma))
		return -ENXIO;

	ret = clk_prepare_enable(nandc->core_clk);
	if (ret)
		goto err_core_clk;

	ret = clk_prepare_enable(nandc->aon_clk);
	if (ret)
		goto err_aon_clk;

	ret = qcom_nandc_alloc(nandc);
	if (ret)
		goto err_nandc_alloc;

	ret = qcom_nandc_setup(nandc);
	if (ret)
		goto err_setup;

	ret = qcom_probe_nand_devices(nandc);
	if (ret)
		goto err_setup;

	return 0;

err_setup:
	qcom_nandc_unalloc(nandc);
err_nandc_alloc:
	clk_disable_unprepare(nandc->aon_clk);
err_aon_clk:
	clk_disable_unprepare(nandc->core_clk);
err_core_clk:
	dma_unmap_resource(dev, nandc->base_dma, resource_size(res),
			   DMA_BIDIRECTIONAL, 0);
	return ret;
}

static void qcom_nandc_remove(struct platform_device *pdev)
{
	struct qcom_nand_controller *nandc = platform_get_drvdata(pdev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	struct qcom_nand_host *host;
	struct nand_chip *chip;
	int ret;

	list_for_each_entry(host, &nandc->host_list, node) {
		chip = &host->chip;
		ret = mtd_device_unregister(nand_to_mtd(chip));
		WARN_ON(ret);
		nand_cleanup(chip);
	}

	qcom_nandc_unalloc(nandc);

	clk_disable_unprepare(nandc->aon_clk);
	clk_disable_unprepare(nandc->core_clk);

	dma_unmap_resource(&pdev->dev, nandc->base_dma, resource_size(res),
			   DMA_BIDIRECTIONAL, 0);
}

static const struct qcom_nandc_props ipq806x_nandc_props = {
	.ecc_modes = (ECC_RS_4BIT | ECC_BCH_8BIT),
	.supports_bam = false,
	.use_codeword_fixup = true,
	.dev_cmd_reg_start = 0x0,
	.bam_offset = 0x30000,
};

static const struct qcom_nandc_props ipq4019_nandc_props = {
	.ecc_modes = (ECC_BCH_4BIT | ECC_BCH_8BIT),
	.supports_bam = true,
	.nandc_part_of_qpic = true,
	.dev_cmd_reg_start = 0x0,
	.bam_offset = 0x30000,
};

static const struct qcom_nandc_props ipq8074_nandc_props = {
	.ecc_modes = (ECC_BCH_4BIT | ECC_BCH_8BIT),
	.supports_bam = true,
	.nandc_part_of_qpic = true,
	.dev_cmd_reg_start = 0x7000,
	.bam_offset = 0x30000,
};

static const struct qcom_nandc_props sdx55_nandc_props = {
	.ecc_modes = (ECC_BCH_4BIT | ECC_BCH_8BIT),
	.supports_bam = true,
	.nandc_part_of_qpic = true,
	.qpic_version2 = true,
	.dev_cmd_reg_start = 0x7000,
	.bam_offset = 0x30000,
};

/*
 * data will hold a struct pointer containing more differences once we support
 * more controller variants
 */
static const struct of_device_id qcom_nandc_of_match[] = {
	{
		.compatible = "qcom,ipq806x-nand",
		.data = &ipq806x_nandc_props,
	},
	{
		.compatible = "qcom,ipq4019-nand",
		.data = &ipq4019_nandc_props,
	},
	{
		.compatible = "qcom,ipq6018-nand",
		.data = &ipq8074_nandc_props,
	},
	{
		.compatible = "qcom,ipq8074-nand",
		.data = &ipq8074_nandc_props,
	},
	{
		.compatible = "qcom,sdx55-nand",
		.data = &sdx55_nandc_props,
	},
	{}
};
MODULE_DEVICE_TABLE(of, qcom_nandc_of_match);

static struct platform_driver qcom_nandc_driver = {
	.driver = {
		.name = "qcom-nandc",
		.of_match_table = qcom_nandc_of_match,
	},
	.probe = qcom_nandc_probe,
	.remove = qcom_nandc_remove,
};
module_platform_driver(qcom_nandc_driver);

MODULE_AUTHOR("Archit Taneja <architt@codeaurora.org>");
MODULE_DESCRIPTION("Qualcomm NAND Controller driver");
MODULE_LICENSE("GPL v2");
