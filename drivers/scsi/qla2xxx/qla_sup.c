// SPDX-License-Identifier: GPL-2.0-only
/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 */
#include "qla_def.h"

#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>
#include <linux/uaccess.h>

/*
 * NVRAM support routines
 */

/**
 * qla2x00_lock_nvram_access() -
 * @ha: HA context
 */
static void
qla2x00_lock_nvram_access(struct qla_hw_data *ha)
{
	uint16_t data;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	if (!IS_QLA2100(ha) && !IS_QLA2200(ha) && !IS_QLA2300(ha)) {
		data = rd_reg_word(&reg->nvram);
		while (data & NVR_BUSY) {
			udelay(100);
			data = rd_reg_word(&reg->nvram);
		}

		/* Lock resource */
		wrt_reg_word(&reg->u.isp2300.host_semaphore, 0x1);
		rd_reg_word(&reg->u.isp2300.host_semaphore);
		udelay(5);
		data = rd_reg_word(&reg->u.isp2300.host_semaphore);
		while ((data & BIT_0) == 0) {
			/* Lock failed */
			udelay(100);
			wrt_reg_word(&reg->u.isp2300.host_semaphore, 0x1);
			rd_reg_word(&reg->u.isp2300.host_semaphore);
			udelay(5);
			data = rd_reg_word(&reg->u.isp2300.host_semaphore);
		}
	}
}

/**
 * qla2x00_unlock_nvram_access() -
 * @ha: HA context
 */
static void
qla2x00_unlock_nvram_access(struct qla_hw_data *ha)
{
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	if (!IS_QLA2100(ha) && !IS_QLA2200(ha) && !IS_QLA2300(ha)) {
		wrt_reg_word(&reg->u.isp2300.host_semaphore, 0);
		rd_reg_word(&reg->u.isp2300.host_semaphore);
	}
}

/**
 * qla2x00_nv_write() - Prepare for NVRAM read/write operation.
 * @ha: HA context
 * @data: Serial interface selector
 */
static void
qla2x00_nv_write(struct qla_hw_data *ha, uint16_t data)
{
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	wrt_reg_word(&reg->nvram, data | NVR_SELECT | NVR_WRT_ENABLE);
	rd_reg_word(&reg->nvram);		/* PCI Posting. */
	NVRAM_DELAY();
	wrt_reg_word(&reg->nvram, data | NVR_SELECT | NVR_CLOCK |
	    NVR_WRT_ENABLE);
	rd_reg_word(&reg->nvram);		/* PCI Posting. */
	NVRAM_DELAY();
	wrt_reg_word(&reg->nvram, data | NVR_SELECT | NVR_WRT_ENABLE);
	rd_reg_word(&reg->nvram);		/* PCI Posting. */
	NVRAM_DELAY();
}

/**
 * qla2x00_nvram_request() - Sends read command to NVRAM and gets data from
 *	NVRAM.
 * @ha: HA context
 * @nv_cmd: NVRAM command
 *
 * Bit definitions for NVRAM command:
 *
 *	Bit 26     = start bit
 *	Bit 25, 24 = opcode
 *	Bit 23-16  = address
 *	Bit 15-0   = write data
 *
 * Returns the word read from nvram @addr.
 */
static uint16_t
qla2x00_nvram_request(struct qla_hw_data *ha, uint32_t nv_cmd)
{
	uint8_t		cnt;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	uint16_t	data = 0;
	uint16_t	reg_data;

	/* Send command to NVRAM. */
	nv_cmd <<= 5;
	for (cnt = 0; cnt < 11; cnt++) {
		if (nv_cmd & BIT_31)
			qla2x00_nv_write(ha, NVR_DATA_OUT);
		else
			qla2x00_nv_write(ha, 0);
		nv_cmd <<= 1;
	}

	/* Read data from NVRAM. */
	for (cnt = 0; cnt < 16; cnt++) {
		wrt_reg_word(&reg->nvram, NVR_SELECT | NVR_CLOCK);
		rd_reg_word(&reg->nvram);	/* PCI Posting. */
		NVRAM_DELAY();
		data <<= 1;
		reg_data = rd_reg_word(&reg->nvram);
		if (reg_data & NVR_DATA_IN)
			data |= BIT_0;
		wrt_reg_word(&reg->nvram, NVR_SELECT);
		rd_reg_word(&reg->nvram);	/* PCI Posting. */
		NVRAM_DELAY();
	}

	/* Deselect chip. */
	wrt_reg_word(&reg->nvram, NVR_DESELECT);
	rd_reg_word(&reg->nvram);		/* PCI Posting. */
	NVRAM_DELAY();

	return data;
}


/**
 * qla2x00_get_nvram_word() - Calculates word position in NVRAM and calls the
 *	request routine to get the word from NVRAM.
 * @ha: HA context
 * @addr: Address in NVRAM to read
 *
 * Returns the word read from nvram @addr.
 */
static uint16_t
qla2x00_get_nvram_word(struct qla_hw_data *ha, uint32_t addr)
{
	uint16_t	data;
	uint32_t	nv_cmd;

	nv_cmd = addr << 16;
	nv_cmd |= NV_READ_OP;
	data = qla2x00_nvram_request(ha, nv_cmd);

	return (data);
}

/**
 * qla2x00_nv_deselect() - Deselect NVRAM operations.
 * @ha: HA context
 */
static void
qla2x00_nv_deselect(struct qla_hw_data *ha)
{
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	wrt_reg_word(&reg->nvram, NVR_DESELECT);
	rd_reg_word(&reg->nvram);		/* PCI Posting. */
	NVRAM_DELAY();
}

/**
 * qla2x00_write_nvram_word() - Write NVRAM data.
 * @ha: HA context
 * @addr: Address in NVRAM to write
 * @data: word to program
 */
static void
qla2x00_write_nvram_word(struct qla_hw_data *ha, uint32_t addr, __le16 data)
{
	int count;
	uint16_t word;
	uint32_t nv_cmd, wait_cnt;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	qla2x00_nv_write(ha, NVR_DATA_OUT);
	qla2x00_nv_write(ha, 0);
	qla2x00_nv_write(ha, 0);

	for (word = 0; word < 8; word++)
		qla2x00_nv_write(ha, NVR_DATA_OUT);

	qla2x00_nv_deselect(ha);

	/* Write data */
	nv_cmd = (addr << 16) | NV_WRITE_OP;
	nv_cmd |= (__force u16)data;
	nv_cmd <<= 5;
	for (count = 0; count < 27; count++) {
		if (nv_cmd & BIT_31)
			qla2x00_nv_write(ha, NVR_DATA_OUT);
		else
			qla2x00_nv_write(ha, 0);

		nv_cmd <<= 1;
	}

	qla2x00_nv_deselect(ha);

	/* Wait for NVRAM to become ready */
	wrt_reg_word(&reg->nvram, NVR_SELECT);
	rd_reg_word(&reg->nvram);		/* PCI Posting. */
	wait_cnt = NVR_WAIT_CNT;
	do {
		if (!--wait_cnt) {
			ql_dbg(ql_dbg_user, vha, 0x708d,
			    "NVRAM didn't go ready...\n");
			break;
		}
		NVRAM_DELAY();
		word = rd_reg_word(&reg->nvram);
	} while ((word & NVR_DATA_IN) == 0);

	qla2x00_nv_deselect(ha);

	/* Disable writes */
	qla2x00_nv_write(ha, NVR_DATA_OUT);
	for (count = 0; count < 10; count++)
		qla2x00_nv_write(ha, 0);

	qla2x00_nv_deselect(ha);
}

static int
qla2x00_write_nvram_word_tmo(struct qla_hw_data *ha, uint32_t addr,
			     __le16 data, uint32_t tmo)
{
	int ret, count;
	uint16_t word;
	uint32_t nv_cmd;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	ret = QLA_SUCCESS;

	qla2x00_nv_write(ha, NVR_DATA_OUT);
	qla2x00_nv_write(ha, 0);
	qla2x00_nv_write(ha, 0);

	for (word = 0; word < 8; word++)
		qla2x00_nv_write(ha, NVR_DATA_OUT);

	qla2x00_nv_deselect(ha);

	/* Write data */
	nv_cmd = (addr << 16) | NV_WRITE_OP;
	nv_cmd |= (__force u16)data;
	nv_cmd <<= 5;
	for (count = 0; count < 27; count++) {
		if (nv_cmd & BIT_31)
			qla2x00_nv_write(ha, NVR_DATA_OUT);
		else
			qla2x00_nv_write(ha, 0);

		nv_cmd <<= 1;
	}

	qla2x00_nv_deselect(ha);

	/* Wait for NVRAM to become ready */
	wrt_reg_word(&reg->nvram, NVR_SELECT);
	rd_reg_word(&reg->nvram);		/* PCI Posting. */
	do {
		NVRAM_DELAY();
		word = rd_reg_word(&reg->nvram);
		if (!--tmo) {
			ret = QLA_FUNCTION_FAILED;
			break;
		}
	} while ((word & NVR_DATA_IN) == 0);

	qla2x00_nv_deselect(ha);

	/* Disable writes */
	qla2x00_nv_write(ha, NVR_DATA_OUT);
	for (count = 0; count < 10; count++)
		qla2x00_nv_write(ha, 0);

	qla2x00_nv_deselect(ha);

	return ret;
}

/**
 * qla2x00_clear_nvram_protection() -
 * @ha: HA context
 */
static int
qla2x00_clear_nvram_protection(struct qla_hw_data *ha)
{
	int ret, stat;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	uint32_t word, wait_cnt;
	__le16 wprot, wprot_old;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	/* Clear NVRAM write protection. */
	ret = QLA_FUNCTION_FAILED;

	wprot_old = cpu_to_le16(qla2x00_get_nvram_word(ha, ha->nvram_base));
	stat = qla2x00_write_nvram_word_tmo(ha, ha->nvram_base,
					    cpu_to_le16(0x1234), 100000);
	wprot = cpu_to_le16(qla2x00_get_nvram_word(ha, ha->nvram_base));
	if (stat != QLA_SUCCESS || wprot != cpu_to_le16(0x1234)) {
		/* Write enable. */
		qla2x00_nv_write(ha, NVR_DATA_OUT);
		qla2x00_nv_write(ha, 0);
		qla2x00_nv_write(ha, 0);
		for (word = 0; word < 8; word++)
			qla2x00_nv_write(ha, NVR_DATA_OUT);

		qla2x00_nv_deselect(ha);

		/* Enable protection register. */
		qla2x00_nv_write(ha, NVR_PR_ENABLE | NVR_DATA_OUT);
		qla2x00_nv_write(ha, NVR_PR_ENABLE);
		qla2x00_nv_write(ha, NVR_PR_ENABLE);
		for (word = 0; word < 8; word++)
			qla2x00_nv_write(ha, NVR_DATA_OUT | NVR_PR_ENABLE);

		qla2x00_nv_deselect(ha);

		/* Clear protection register (ffff is cleared). */
		qla2x00_nv_write(ha, NVR_PR_ENABLE | NVR_DATA_OUT);
		qla2x00_nv_write(ha, NVR_PR_ENABLE | NVR_DATA_OUT);
		qla2x00_nv_write(ha, NVR_PR_ENABLE | NVR_DATA_OUT);
		for (word = 0; word < 8; word++)
			qla2x00_nv_write(ha, NVR_DATA_OUT | NVR_PR_ENABLE);

		qla2x00_nv_deselect(ha);

		/* Wait for NVRAM to become ready. */
		wrt_reg_word(&reg->nvram, NVR_SELECT);
		rd_reg_word(&reg->nvram);	/* PCI Posting. */
		wait_cnt = NVR_WAIT_CNT;
		do {
			if (!--wait_cnt) {
				ql_dbg(ql_dbg_user, vha, 0x708e,
				    "NVRAM didn't go ready...\n");
				break;
			}
			NVRAM_DELAY();
			word = rd_reg_word(&reg->nvram);
		} while ((word & NVR_DATA_IN) == 0);

		if (wait_cnt)
			ret = QLA_SUCCESS;
	} else
		qla2x00_write_nvram_word(ha, ha->nvram_base, wprot_old);

	return ret;
}

static void
qla2x00_set_nvram_protection(struct qla_hw_data *ha, int stat)
{
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	uint32_t word, wait_cnt;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	if (stat != QLA_SUCCESS)
		return;

	/* Set NVRAM write protection. */
	/* Write enable. */
	qla2x00_nv_write(ha, NVR_DATA_OUT);
	qla2x00_nv_write(ha, 0);
	qla2x00_nv_write(ha, 0);
	for (word = 0; word < 8; word++)
		qla2x00_nv_write(ha, NVR_DATA_OUT);

	qla2x00_nv_deselect(ha);

	/* Enable protection register. */
	qla2x00_nv_write(ha, NVR_PR_ENABLE | NVR_DATA_OUT);
	qla2x00_nv_write(ha, NVR_PR_ENABLE);
	qla2x00_nv_write(ha, NVR_PR_ENABLE);
	for (word = 0; word < 8; word++)
		qla2x00_nv_write(ha, NVR_DATA_OUT | NVR_PR_ENABLE);

	qla2x00_nv_deselect(ha);

	/* Enable protection register. */
	qla2x00_nv_write(ha, NVR_PR_ENABLE | NVR_DATA_OUT);
	qla2x00_nv_write(ha, NVR_PR_ENABLE);
	qla2x00_nv_write(ha, NVR_PR_ENABLE | NVR_DATA_OUT);
	for (word = 0; word < 8; word++)
		qla2x00_nv_write(ha, NVR_PR_ENABLE);

	qla2x00_nv_deselect(ha);

	/* Wait for NVRAM to become ready. */
	wrt_reg_word(&reg->nvram, NVR_SELECT);
	rd_reg_word(&reg->nvram);		/* PCI Posting. */
	wait_cnt = NVR_WAIT_CNT;
	do {
		if (!--wait_cnt) {
			ql_dbg(ql_dbg_user, vha, 0x708f,
			    "NVRAM didn't go ready...\n");
			break;
		}
		NVRAM_DELAY();
		word = rd_reg_word(&reg->nvram);
	} while ((word & NVR_DATA_IN) == 0);
}


/*****************************************************************************/
/* Flash Manipulation Routines                                               */
/*****************************************************************************/

static inline uint32_t
flash_conf_addr(struct qla_hw_data *ha, uint32_t faddr)
{
	return ha->flash_conf_off + faddr;
}

static inline uint32_t
flash_data_addr(struct qla_hw_data *ha, uint32_t faddr)
{
	return ha->flash_data_off + faddr;
}

static inline uint32_t
nvram_conf_addr(struct qla_hw_data *ha, uint32_t naddr)
{
	return ha->nvram_conf_off + naddr;
}

static inline uint32_t
nvram_data_addr(struct qla_hw_data *ha, uint32_t naddr)
{
	return ha->nvram_data_off + naddr;
}

static int
qla24xx_read_flash_dword(struct qla_hw_data *ha, uint32_t addr, uint32_t *data)
{
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	ulong cnt = 30000;

	wrt_reg_dword(&reg->flash_addr, addr & ~FARX_DATA_FLAG);

	while (cnt--) {
		if (rd_reg_dword(&reg->flash_addr) & FARX_DATA_FLAG) {
			*data = rd_reg_dword(&reg->flash_data);
			return QLA_SUCCESS;
		}
		udelay(10);
		cond_resched();
	}

	ql_log(ql_log_warn, pci_get_drvdata(ha->pdev), 0x7090,
	    "Flash read dword at %x timeout.\n", addr);
	*data = 0xDEADDEAD;
	return QLA_FUNCTION_TIMEOUT;
}

int
qla24xx_read_flash_data(scsi_qla_host_t *vha, uint32_t *dwptr, uint32_t faddr,
    uint32_t dwords)
{
	ulong i;
	int ret = QLA_SUCCESS;
	struct qla_hw_data *ha = vha->hw;

	/* Dword reads to flash. */
	faddr =  flash_data_addr(ha, faddr);
	for (i = 0; i < dwords; i++, faddr++, dwptr++) {
		ret = qla24xx_read_flash_dword(ha, faddr, dwptr);
		if (ret != QLA_SUCCESS)
			break;
		cpu_to_le32s(dwptr);
	}

	return ret;
}

static int
qla24xx_write_flash_dword(struct qla_hw_data *ha, uint32_t addr, uint32_t data)
{
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	ulong cnt = 500000;

	wrt_reg_dword(&reg->flash_data, data);
	wrt_reg_dword(&reg->flash_addr, addr | FARX_DATA_FLAG);

	while (cnt--) {
		if (!(rd_reg_dword(&reg->flash_addr) & FARX_DATA_FLAG))
			return QLA_SUCCESS;
		udelay(10);
		cond_resched();
	}

	ql_log(ql_log_warn, pci_get_drvdata(ha->pdev), 0x7090,
	    "Flash write dword at %x timeout.\n", addr);
	return QLA_FUNCTION_TIMEOUT;
}

static void
qla24xx_get_flash_manufacturer(struct qla_hw_data *ha, uint8_t *man_id,
    uint8_t *flash_id)
{
	uint32_t faddr, ids = 0;

	*man_id = *flash_id = 0;

	faddr = flash_conf_addr(ha, 0x03ab);
	if (!qla24xx_read_flash_dword(ha, faddr, &ids)) {
		*man_id = LSB(ids);
		*flash_id = MSB(ids);
	}

	/* Check if man_id and flash_id are valid. */
	if (ids != 0xDEADDEAD && (*man_id == 0 || *flash_id == 0)) {
		/* Read information using 0x9f opcode
		 * Device ID, Mfg ID would be read in the format:
		 *   <Ext Dev Info><Device ID Part2><Device ID Part 1><Mfg ID>
		 * Example: ATMEL 0x00 01 45 1F
		 * Extract MFG and Dev ID from last two bytes.
		 */
		faddr = flash_conf_addr(ha, 0x009f);
		if (!qla24xx_read_flash_dword(ha, faddr, &ids)) {
			*man_id = LSB(ids);
			*flash_id = MSB(ids);
		}
	}
}

static int
qla2xxx_find_flt_start(scsi_qla_host_t *vha, uint32_t *start)
{
	const char *loc, *locations[] = { "DEF", "PCI" };
	uint32_t pcihdr, pcids;
	uint16_t cnt, chksum;
	__le16 *wptr;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req = ha->req_q_map[0];
	struct qla_flt_location *fltl = (void *)req->ring;
	uint32_t *dcode = (uint32_t *)req->ring;
	uint8_t *buf = (void *)req->ring, *bcode,  last_image;

	/*
	 * FLT-location structure resides after the last PCI region.
	 */

	/* Begin with sane defaults. */
	loc = locations[0];
	*start = 0;
	if (IS_QLA24XX_TYPE(ha))
		*start = FA_FLASH_LAYOUT_ADDR_24;
	else if (IS_QLA25XX(ha))
		*start = FA_FLASH_LAYOUT_ADDR;
	else if (IS_QLA81XX(ha))
		*start = FA_FLASH_LAYOUT_ADDR_81;
	else if (IS_P3P_TYPE(ha)) {
		*start = FA_FLASH_LAYOUT_ADDR_82;
		goto end;
	} else if (IS_QLA83XX(ha) || IS_QLA27XX(ha)) {
		*start = FA_FLASH_LAYOUT_ADDR_83;
		goto end;
	} else if (IS_QLA28XX(ha)) {
		*start = FA_FLASH_LAYOUT_ADDR_28;
		goto end;
	}

	/* Begin with first PCI expansion ROM header. */
	pcihdr = 0;
	do {
		/* Verify PCI expansion ROM header. */
		qla24xx_read_flash_data(vha, dcode, pcihdr >> 2, 0x20);
		bcode = buf + (pcihdr % 4);
		if (bcode[0x0] != 0x55 || bcode[0x1] != 0xaa)
			goto end;

		/* Locate PCI data structure. */
		pcids = pcihdr + ((bcode[0x19] << 8) | bcode[0x18]);
		qla24xx_read_flash_data(vha, dcode, pcids >> 2, 0x20);
		bcode = buf + (pcihdr % 4);

		/* Validate signature of PCI data structure. */
		if (bcode[0x0] != 'P' || bcode[0x1] != 'C' ||
		    bcode[0x2] != 'I' || bcode[0x3] != 'R')
			goto end;

		last_image = bcode[0x15] & BIT_7;

		/* Locate next PCI expansion ROM. */
		pcihdr += ((bcode[0x11] << 8) | bcode[0x10]) * 512;
	} while (!last_image);

	/* Now verify FLT-location structure. */
	qla24xx_read_flash_data(vha, dcode, pcihdr >> 2, sizeof(*fltl) >> 2);
	if (memcmp(fltl->sig, "QFLT", 4))
		goto end;

	wptr = (__force __le16 *)req->ring;
	cnt = sizeof(*fltl) / sizeof(*wptr);
	for (chksum = 0; cnt--; wptr++)
		chksum += le16_to_cpu(*wptr);
	if (chksum) {
		ql_log(ql_log_fatal, vha, 0x0045,
		    "Inconsistent FLTL detected: checksum=0x%x.\n", chksum);
		ql_dump_buffer(ql_dbg_init + ql_dbg_buffer, vha, 0x010e,
		    fltl, sizeof(*fltl));
		return QLA_FUNCTION_FAILED;
	}

	/* Good data.  Use specified location. */
	loc = locations[1];
	*start = (le16_to_cpu(fltl->start_hi) << 16 |
	    le16_to_cpu(fltl->start_lo)) >> 2;
end:
	ql_dbg(ql_dbg_init, vha, 0x0046,
	    "FLTL[%s] = 0x%x.\n",
	    loc, *start);
	return QLA_SUCCESS;
}

static void
qla2xxx_get_flt_info(scsi_qla_host_t *vha, uint32_t flt_addr)
{
	const char *locations[] = { "DEF", "FLT" }, *loc = locations[1];
	const uint32_t def_fw[] =
		{ FA_RISC_CODE_ADDR, FA_RISC_CODE_ADDR, FA_RISC_CODE_ADDR_81 };
	const uint32_t def_boot[] =
		{ FA_BOOT_CODE_ADDR, FA_BOOT_CODE_ADDR, FA_BOOT_CODE_ADDR_81 };
	const uint32_t def_vpd_nvram[] =
		{ FA_VPD_NVRAM_ADDR, FA_VPD_NVRAM_ADDR, FA_VPD_NVRAM_ADDR_81 };
	const uint32_t def_vpd0[] =
		{ 0, 0, FA_VPD0_ADDR_81 };
	const uint32_t def_vpd1[] =
		{ 0, 0, FA_VPD1_ADDR_81 };
	const uint32_t def_nvram0[] =
		{ 0, 0, FA_NVRAM0_ADDR_81 };
	const uint32_t def_nvram1[] =
		{ 0, 0, FA_NVRAM1_ADDR_81 };
	const uint32_t def_fdt[] =
		{ FA_FLASH_DESCR_ADDR_24, FA_FLASH_DESCR_ADDR,
			FA_FLASH_DESCR_ADDR_81 };
	const uint32_t def_npiv_conf0[] =
		{ FA_NPIV_CONF0_ADDR_24, FA_NPIV_CONF0_ADDR,
			FA_NPIV_CONF0_ADDR_81 };
	const uint32_t def_npiv_conf1[] =
		{ FA_NPIV_CONF1_ADDR_24, FA_NPIV_CONF1_ADDR,
			FA_NPIV_CONF1_ADDR_81 };
	const uint32_t fcp_prio_cfg0[] =
		{ FA_FCP_PRIO0_ADDR, FA_FCP_PRIO0_ADDR_25,
			0 };
	const uint32_t fcp_prio_cfg1[] =
		{ FA_FCP_PRIO1_ADDR, FA_FCP_PRIO1_ADDR_25,
			0 };

	struct qla_hw_data *ha = vha->hw;
	uint32_t def = IS_QLA81XX(ha) ? 2 : IS_QLA25XX(ha) ? 1 : 0;
	struct qla_flt_header *flt = ha->flt;
	struct qla_flt_region *region = &flt->region[0];
	__le16 *wptr;
	uint16_t cnt, chksum;
	uint32_t start;

	/* Assign FCP prio region since older adapters may not have FLT, or
	   FCP prio region in it's FLT.
	 */
	ha->flt_region_fcp_prio = (ha->port_no == 0) ?
	    fcp_prio_cfg0[def] : fcp_prio_cfg1[def];

	ha->flt_region_flt = flt_addr;
	wptr = (__force __le16 *)ha->flt;
	ha->isp_ops->read_optrom(vha, flt, flt_addr << 2,
	    (sizeof(struct qla_flt_header) + FLT_REGIONS_SIZE));

	if (le16_to_cpu(*wptr) == 0xffff)
		goto no_flash_data;
	if (flt->version != cpu_to_le16(1)) {
		ql_log(ql_log_warn, vha, 0x0047,
		    "Unsupported FLT detected: version=0x%x length=0x%x checksum=0x%x.\n",
		    le16_to_cpu(flt->version), le16_to_cpu(flt->length),
		    le16_to_cpu(flt->checksum));
		goto no_flash_data;
	}

	cnt = (sizeof(*flt) + le16_to_cpu(flt->length)) / sizeof(*wptr);
	for (chksum = 0; cnt--; wptr++)
		chksum += le16_to_cpu(*wptr);
	if (chksum) {
		ql_log(ql_log_fatal, vha, 0x0048,
		    "Inconsistent FLT detected: version=0x%x length=0x%x checksum=0x%x.\n",
		    le16_to_cpu(flt->version), le16_to_cpu(flt->length),
		    le16_to_cpu(flt->checksum));
		goto no_flash_data;
	}

	cnt = le16_to_cpu(flt->length) / sizeof(*region);
	for ( ; cnt; cnt--, region++) {
		/* Store addresses as DWORD offsets. */
		start = le32_to_cpu(region->start) >> 2;
		ql_dbg(ql_dbg_init, vha, 0x0049,
		    "FLT[%#x]: start=%#x end=%#x size=%#x.\n",
		    le16_to_cpu(region->code), start,
		    le32_to_cpu(region->end) >> 2,
		    le32_to_cpu(region->size) >> 2);
		if (region->attribute)
			ql_log(ql_dbg_init, vha, 0xffff,
			    "Region %x is secure\n", region->code);

		switch (le16_to_cpu(region->code)) {
		case FLT_REG_FCOE_FW:
			if (!IS_QLA8031(ha))
				break;
			ha->flt_region_fw = start;
			break;
		case FLT_REG_FW:
			if (IS_QLA8031(ha))
				break;
			ha->flt_region_fw = start;
			break;
		case FLT_REG_BOOT_CODE:
			ha->flt_region_boot = start;
			break;
		case FLT_REG_VPD_0:
			if (IS_QLA8031(ha))
				break;
			ha->flt_region_vpd_nvram = start;
			if (IS_P3P_TYPE(ha))
				break;
			if (ha->port_no == 0)
				ha->flt_region_vpd = start;
			break;
		case FLT_REG_VPD_1:
			if (IS_P3P_TYPE(ha) || IS_QLA8031(ha))
				break;
			if (ha->port_no == 1)
				ha->flt_region_vpd = start;
			break;
		case FLT_REG_VPD_2:
			if (!IS_QLA27XX(ha) && !IS_QLA28XX(ha))
				break;
			if (ha->port_no == 2)
				ha->flt_region_vpd = start;
			break;
		case FLT_REG_VPD_3:
			if (!IS_QLA27XX(ha) && !IS_QLA28XX(ha))
				break;
			if (ha->port_no == 3)
				ha->flt_region_vpd = start;
			break;
		case FLT_REG_NVRAM_0:
			if (IS_QLA8031(ha))
				break;
			if (ha->port_no == 0)
				ha->flt_region_nvram = start;
			break;
		case FLT_REG_NVRAM_1:
			if (IS_QLA8031(ha))
				break;
			if (ha->port_no == 1)
				ha->flt_region_nvram = start;
			break;
		case FLT_REG_NVRAM_2:
			if (!IS_QLA27XX(ha) && !IS_QLA28XX(ha))
				break;
			if (ha->port_no == 2)
				ha->flt_region_nvram = start;
			break;
		case FLT_REG_NVRAM_3:
			if (!IS_QLA27XX(ha) && !IS_QLA28XX(ha))
				break;
			if (ha->port_no == 3)
				ha->flt_region_nvram = start;
			break;
		case FLT_REG_FDT:
			ha->flt_region_fdt = start;
			break;
		case FLT_REG_NPIV_CONF_0:
			if (ha->port_no == 0)
				ha->flt_region_npiv_conf = start;
			break;
		case FLT_REG_NPIV_CONF_1:
			if (ha->port_no == 1)
				ha->flt_region_npiv_conf = start;
			break;
		case FLT_REG_GOLD_FW:
			ha->flt_region_gold_fw = start;
			break;
		case FLT_REG_FCP_PRIO_0:
			if (ha->port_no == 0)
				ha->flt_region_fcp_prio = start;
			break;
		case FLT_REG_FCP_PRIO_1:
			if (ha->port_no == 1)
				ha->flt_region_fcp_prio = start;
			break;
		case FLT_REG_BOOT_CODE_82XX:
			ha->flt_region_boot = start;
			break;
		case FLT_REG_BOOT_CODE_8044:
			if (IS_QLA8044(ha))
				ha->flt_region_boot = start;
			break;
		case FLT_REG_FW_82XX:
			ha->flt_region_fw = start;
			break;
		case FLT_REG_CNA_FW:
			if (IS_CNA_CAPABLE(ha))
				ha->flt_region_fw = start;
			break;
		case FLT_REG_GOLD_FW_82XX:
			ha->flt_region_gold_fw = start;
			break;
		case FLT_REG_BOOTLOAD_82XX:
			ha->flt_region_bootload = start;
			break;
		case FLT_REG_VPD_8XXX:
			if (IS_CNA_CAPABLE(ha))
				ha->flt_region_vpd = start;
			break;
		case FLT_REG_FCOE_NVRAM_0:
			if (!(IS_QLA8031(ha) || IS_QLA8044(ha)))
				break;
			if (ha->port_no == 0)
				ha->flt_region_nvram = start;
			break;
		case FLT_REG_FCOE_NVRAM_1:
			if (!(IS_QLA8031(ha) || IS_QLA8044(ha)))
				break;
			if (ha->port_no == 1)
				ha->flt_region_nvram = start;
			break;
		case FLT_REG_IMG_PRI_27XX:
			if (IS_QLA27XX(ha) && !IS_QLA28XX(ha))
				ha->flt_region_img_status_pri = start;
			break;
		case FLT_REG_IMG_SEC_27XX:
			if (IS_QLA27XX(ha) || IS_QLA28XX(ha))
				ha->flt_region_img_status_sec = start;
			break;
		case FLT_REG_FW_SEC_27XX:
			if (IS_QLA27XX(ha) || IS_QLA28XX(ha))
				ha->flt_region_fw_sec = start;
			break;
		case FLT_REG_BOOTLOAD_SEC_27XX:
			if (IS_QLA27XX(ha) || IS_QLA28XX(ha))
				ha->flt_region_boot_sec = start;
			break;
		case FLT_REG_AUX_IMG_PRI_28XX:
			if (IS_QLA27XX(ha) || IS_QLA28XX(ha))
				ha->flt_region_aux_img_status_pri = start;
			break;
		case FLT_REG_AUX_IMG_SEC_28XX:
			if (IS_QLA27XX(ha) || IS_QLA28XX(ha))
				ha->flt_region_aux_img_status_sec = start;
			break;
		case FLT_REG_NVRAM_SEC_28XX_0:
			if (IS_QLA27XX(ha) || IS_QLA28XX(ha))
				if (ha->port_no == 0)
					ha->flt_region_nvram_sec = start;
			break;
		case FLT_REG_NVRAM_SEC_28XX_1:
			if (IS_QLA27XX(ha) || IS_QLA28XX(ha))
				if (ha->port_no == 1)
					ha->flt_region_nvram_sec = start;
			break;
		case FLT_REG_NVRAM_SEC_28XX_2:
			if (IS_QLA27XX(ha) || IS_QLA28XX(ha))
				if (ha->port_no == 2)
					ha->flt_region_nvram_sec = start;
			break;
		case FLT_REG_NVRAM_SEC_28XX_3:
			if (IS_QLA27XX(ha) || IS_QLA28XX(ha))
				if (ha->port_no == 3)
					ha->flt_region_nvram_sec = start;
			break;
		case FLT_REG_VPD_SEC_27XX_0:
		case FLT_REG_VPD_SEC_28XX_0:
			if (IS_QLA27XX(ha) || IS_QLA28XX(ha)) {
				ha->flt_region_vpd_nvram_sec = start;
				if (ha->port_no == 0)
					ha->flt_region_vpd_sec = start;
			}
			break;
		case FLT_REG_VPD_SEC_27XX_1:
		case FLT_REG_VPD_SEC_28XX_1:
			if (IS_QLA27XX(ha) || IS_QLA28XX(ha))
				if (ha->port_no == 1)
					ha->flt_region_vpd_sec = start;
			break;
		case FLT_REG_VPD_SEC_27XX_2:
		case FLT_REG_VPD_SEC_28XX_2:
			if (IS_QLA27XX(ha) || IS_QLA28XX(ha))
				if (ha->port_no == 2)
					ha->flt_region_vpd_sec = start;
			break;
		case FLT_REG_VPD_SEC_27XX_3:
		case FLT_REG_VPD_SEC_28XX_3:
			if (IS_QLA27XX(ha) || IS_QLA28XX(ha))
				if (ha->port_no == 3)
					ha->flt_region_vpd_sec = start;
			break;
		}
	}
	goto done;

no_flash_data:
	/* Use hardcoded defaults. */
	loc = locations[0];
	ha->flt_region_fw = def_fw[def];
	ha->flt_region_boot = def_boot[def];
	ha->flt_region_vpd_nvram = def_vpd_nvram[def];
	ha->flt_region_vpd = (ha->port_no == 0) ?
	    def_vpd0[def] : def_vpd1[def];
	ha->flt_region_nvram = (ha->port_no == 0) ?
	    def_nvram0[def] : def_nvram1[def];
	ha->flt_region_fdt = def_fdt[def];
	ha->flt_region_npiv_conf = (ha->port_no == 0) ?
	    def_npiv_conf0[def] : def_npiv_conf1[def];
done:
	ql_dbg(ql_dbg_init, vha, 0x004a,
	    "FLT[%s]: boot=0x%x fw=0x%x vpd_nvram=0x%x vpd=0x%x nvram=0x%x "
	    "fdt=0x%x flt=0x%x npiv=0x%x fcp_prif_cfg=0x%x.\n",
	    loc, ha->flt_region_boot, ha->flt_region_fw,
	    ha->flt_region_vpd_nvram, ha->flt_region_vpd, ha->flt_region_nvram,
	    ha->flt_region_fdt, ha->flt_region_flt, ha->flt_region_npiv_conf,
	    ha->flt_region_fcp_prio);
}

static void
qla2xxx_get_fdt_info(scsi_qla_host_t *vha)
{
#define FLASH_BLK_SIZE_4K	0x1000
#define FLASH_BLK_SIZE_32K	0x8000
#define FLASH_BLK_SIZE_64K	0x10000
	const char *loc, *locations[] = { "MID", "FDT" };
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req = ha->req_q_map[0];
	uint16_t cnt, chksum;
	__le16 *wptr = (__force __le16 *)req->ring;
	struct qla_fdt_layout *fdt = (struct qla_fdt_layout *)req->ring;
	uint8_t	man_id, flash_id;
	uint16_t mid = 0, fid = 0;

	ha->isp_ops->read_optrom(vha, fdt, ha->flt_region_fdt << 2,
	    OPTROM_BURST_DWORDS);
	if (le16_to_cpu(*wptr) == 0xffff)
		goto no_flash_data;
	if (memcmp(fdt->sig, "QLID", 4))
		goto no_flash_data;

	for (cnt = 0, chksum = 0; cnt < sizeof(*fdt) >> 1; cnt++, wptr++)
		chksum += le16_to_cpu(*wptr);
	if (chksum) {
		ql_dbg(ql_dbg_init, vha, 0x004c,
		    "Inconsistent FDT detected:"
		    " checksum=0x%x id=%c version0x%x.\n", chksum,
		    fdt->sig[0], le16_to_cpu(fdt->version));
		ql_dump_buffer(ql_dbg_init + ql_dbg_buffer, vha, 0x0113,
		    fdt, sizeof(*fdt));
		goto no_flash_data;
	}

	loc = locations[1];
	mid = le16_to_cpu(fdt->man_id);
	fid = le16_to_cpu(fdt->id);
	ha->fdt_wrt_disable = fdt->wrt_disable_bits;
	ha->fdt_wrt_enable = fdt->wrt_enable_bits;
	ha->fdt_wrt_sts_reg_cmd = fdt->wrt_sts_reg_cmd;
	if (IS_QLA8044(ha))
		ha->fdt_erase_cmd = fdt->erase_cmd;
	else
		ha->fdt_erase_cmd =
		    flash_conf_addr(ha, 0x0300 | fdt->erase_cmd);
	ha->fdt_block_size = le32_to_cpu(fdt->block_size);
	if (fdt->unprotect_sec_cmd) {
		ha->fdt_unprotect_sec_cmd = flash_conf_addr(ha, 0x0300 |
		    fdt->unprotect_sec_cmd);
		ha->fdt_protect_sec_cmd = fdt->protect_sec_cmd ?
		    flash_conf_addr(ha, 0x0300 | fdt->protect_sec_cmd) :
		    flash_conf_addr(ha, 0x0336);
	}
	goto done;
no_flash_data:
	loc = locations[0];
	if (IS_P3P_TYPE(ha)) {
		ha->fdt_block_size = FLASH_BLK_SIZE_64K;
		goto done;
	}
	qla24xx_get_flash_manufacturer(ha, &man_id, &flash_id);
	mid = man_id;
	fid = flash_id;
	ha->fdt_wrt_disable = 0x9c;
	ha->fdt_erase_cmd = flash_conf_addr(ha, 0x03d8);
	switch (man_id) {
	case 0xbf: /* STT flash. */
		if (flash_id == 0x8e)
			ha->fdt_block_size = FLASH_BLK_SIZE_64K;
		else
			ha->fdt_block_size = FLASH_BLK_SIZE_32K;

		if (flash_id == 0x80)
			ha->fdt_erase_cmd = flash_conf_addr(ha, 0x0352);
		break;
	case 0x13: /* ST M25P80. */
		ha->fdt_block_size = FLASH_BLK_SIZE_64K;
		break;
	case 0x1f: /* Atmel 26DF081A. */
		ha->fdt_block_size = FLASH_BLK_SIZE_4K;
		ha->fdt_erase_cmd = flash_conf_addr(ha, 0x0320);
		ha->fdt_unprotect_sec_cmd = flash_conf_addr(ha, 0x0339);
		ha->fdt_protect_sec_cmd = flash_conf_addr(ha, 0x0336);
		break;
	default:
		/* Default to 64 kb sector size. */
		ha->fdt_block_size = FLASH_BLK_SIZE_64K;
		break;
	}
done:
	ql_dbg(ql_dbg_init, vha, 0x004d,
	    "FDT[%s]: (0x%x/0x%x) erase=0x%x "
	    "pr=%x wrtd=0x%x blk=0x%x.\n",
	    loc, mid, fid,
	    ha->fdt_erase_cmd, ha->fdt_protect_sec_cmd,
	    ha->fdt_wrt_disable, ha->fdt_block_size);

}

static void
qla2xxx_get_idc_param(scsi_qla_host_t *vha)
{
#define QLA82XX_IDC_PARAM_ADDR       0x003e885c
	__le32 *wptr;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req = ha->req_q_map[0];

	if (!(IS_P3P_TYPE(ha)))
		return;

	wptr = (__force __le32 *)req->ring;
	ha->isp_ops->read_optrom(vha, req->ring, QLA82XX_IDC_PARAM_ADDR, 8);

	if (*wptr == cpu_to_le32(0xffffffff)) {
		ha->fcoe_dev_init_timeout = QLA82XX_ROM_DEV_INIT_TIMEOUT;
		ha->fcoe_reset_timeout = QLA82XX_ROM_DRV_RESET_ACK_TIMEOUT;
	} else {
		ha->fcoe_dev_init_timeout = le32_to_cpu(*wptr);
		wptr++;
		ha->fcoe_reset_timeout = le32_to_cpu(*wptr);
	}
	ql_dbg(ql_dbg_init, vha, 0x004e,
	    "fcoe_dev_init_timeout=%d "
	    "fcoe_reset_timeout=%d.\n", ha->fcoe_dev_init_timeout,
	    ha->fcoe_reset_timeout);
	return;
}

int
qla2xxx_get_flash_info(scsi_qla_host_t *vha)
{
	int ret;
	uint32_t flt_addr;
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA24XX_TYPE(ha) && !IS_QLA25XX(ha) &&
	    !IS_CNA_CAPABLE(ha) && !IS_QLA2031(ha) &&
	    !IS_QLA27XX(ha) && !IS_QLA28XX(ha))
		return QLA_SUCCESS;

	ret = qla2xxx_find_flt_start(vha, &flt_addr);
	if (ret != QLA_SUCCESS)
		return ret;

	qla2xxx_get_flt_info(vha, flt_addr);
	qla2xxx_get_fdt_info(vha);
	qla2xxx_get_idc_param(vha);

	return QLA_SUCCESS;
}

void
qla2xxx_flash_npiv_conf(scsi_qla_host_t *vha)
{
#define NPIV_CONFIG_SIZE	(16*1024)
	void *data;
	__le16 *wptr;
	uint16_t cnt, chksum;
	int i;
	struct qla_npiv_header hdr;
	struct qla_npiv_entry *entry;
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA24XX_TYPE(ha) && !IS_QLA25XX(ha) &&
	    !IS_CNA_CAPABLE(ha) && !IS_QLA2031(ha))
		return;

	if (ha->flags.nic_core_reset_hdlr_active)
		return;

	if (IS_QLA8044(ha))
		return;

	ha->isp_ops->read_optrom(vha, &hdr, ha->flt_region_npiv_conf << 2,
	    sizeof(struct qla_npiv_header));
	if (hdr.version == cpu_to_le16(0xffff))
		return;
	if (hdr.version != cpu_to_le16(1)) {
		ql_dbg(ql_dbg_user, vha, 0x7090,
		    "Unsupported NPIV-Config "
		    "detected: version=0x%x entries=0x%x checksum=0x%x.\n",
		    le16_to_cpu(hdr.version), le16_to_cpu(hdr.entries),
		    le16_to_cpu(hdr.checksum));
		return;
	}

	data = kmalloc(NPIV_CONFIG_SIZE, GFP_KERNEL);
	if (!data) {
		ql_log(ql_log_warn, vha, 0x7091,
		    "Unable to allocate memory for data.\n");
		return;
	}

	ha->isp_ops->read_optrom(vha, data, ha->flt_region_npiv_conf << 2,
	    NPIV_CONFIG_SIZE);

	cnt = (sizeof(hdr) + le16_to_cpu(hdr.entries) * sizeof(*entry)) >> 1;
	for (wptr = data, chksum = 0; cnt--; wptr++)
		chksum += le16_to_cpu(*wptr);
	if (chksum) {
		ql_dbg(ql_dbg_user, vha, 0x7092,
		    "Inconsistent NPIV-Config "
		    "detected: version=0x%x entries=0x%x checksum=0x%x.\n",
		    le16_to_cpu(hdr.version), le16_to_cpu(hdr.entries),
		    le16_to_cpu(hdr.checksum));
		goto done;
	}

	entry = data + sizeof(struct qla_npiv_header);
	cnt = le16_to_cpu(hdr.entries);
	for (i = 0; cnt; cnt--, entry++, i++) {
		uint16_t flags;
		struct fc_vport_identifiers vid;
		struct fc_vport *vport;

		memcpy(&ha->npiv_info[i], entry, sizeof(struct qla_npiv_entry));

		flags = le16_to_cpu(entry->flags);
		if (flags == 0xffff)
			continue;
		if ((flags & BIT_0) == 0)
			continue;

		memset(&vid, 0, sizeof(vid));
		vid.roles = FC_PORT_ROLE_FCP_INITIATOR;
		vid.vport_type = FC_PORTTYPE_NPIV;
		vid.disable = false;
		vid.port_name = wwn_to_u64(entry->port_name);
		vid.node_name = wwn_to_u64(entry->node_name);

		ql_dbg(ql_dbg_user, vha, 0x7093,
		    "NPIV[%02x]: wwpn=%llx wwnn=%llx vf_id=%#x Q_qos=%#x F_qos=%#x.\n",
		    cnt, vid.port_name, vid.node_name,
		    le16_to_cpu(entry->vf_id),
		    entry->q_qos, entry->f_qos);

		if (i < QLA_PRECONFIG_VPORTS) {
			vport = fc_vport_create(vha->host, 0, &vid);
			if (!vport)
				ql_log(ql_log_warn, vha, 0x7094,
				    "NPIV-Config Failed to create vport [%02x]: wwpn=%llx wwnn=%llx.\n",
				    cnt, vid.port_name, vid.node_name);
		}
	}
done:
	kfree(data);
}

static int
qla24xx_unprotect_flash(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	if (ha->flags.fac_supported)
		return qla81xx_fac_do_write_enable(vha, 1);

	/* Enable flash write. */
	wrt_reg_dword(&reg->ctrl_status,
	    rd_reg_dword(&reg->ctrl_status) | CSRX_FLASH_ENABLE);
	rd_reg_dword(&reg->ctrl_status);	/* PCI Posting. */

	if (!ha->fdt_wrt_disable)
		goto done;

	/* Disable flash write-protection, first clear SR protection bit */
	qla24xx_write_flash_dword(ha, flash_conf_addr(ha, 0x101), 0);
	/* Then write zero again to clear remaining SR bits.*/
	qla24xx_write_flash_dword(ha, flash_conf_addr(ha, 0x101), 0);
done:
	return QLA_SUCCESS;
}

static int
qla24xx_protect_flash(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	ulong cnt = 300;
	uint32_t faddr, dword;

	if (ha->flags.fac_supported)
		return qla81xx_fac_do_write_enable(vha, 0);

	if (!ha->fdt_wrt_disable)
		goto skip_wrt_protect;

	/* Enable flash write-protection and wait for completion. */
	faddr = flash_conf_addr(ha, 0x101);
	qla24xx_write_flash_dword(ha, faddr, ha->fdt_wrt_disable);
	faddr = flash_conf_addr(ha, 0x5);
	while (cnt--) {
		if (!qla24xx_read_flash_dword(ha, faddr, &dword)) {
			if (!(dword & BIT_0))
				break;
		}
		udelay(10);
	}

skip_wrt_protect:
	/* Disable flash write. */
	wrt_reg_dword(&reg->ctrl_status,
	    rd_reg_dword(&reg->ctrl_status) & ~CSRX_FLASH_ENABLE);

	return QLA_SUCCESS;
}

static int
qla24xx_erase_sector(scsi_qla_host_t *vha, uint32_t fdata)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t start, finish;

	if (ha->flags.fac_supported) {
		start = fdata >> 2;
		finish = start + (ha->fdt_block_size >> 2) - 1;
		return qla81xx_fac_erase_sector(vha, flash_data_addr(ha,
		    start), flash_data_addr(ha, finish));
	}

	return qla24xx_write_flash_dword(ha, ha->fdt_erase_cmd,
	    (fdata & 0xff00) | ((fdata << 16) & 0xff0000) |
	    ((fdata >> 16) & 0xff));
}

static int
qla24xx_write_flash_data(scsi_qla_host_t *vha, __le32 *dwptr, uint32_t faddr,
    uint32_t dwords)
{
	int ret;
	ulong liter;
	ulong dburst = OPTROM_BURST_DWORDS; /* burst size in dwords */
	uint32_t sec_mask, rest_addr, fdata;
	dma_addr_t optrom_dma;
	void *optrom = NULL;
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA25XX(ha) && !IS_QLA81XX(ha) && !IS_QLA83XX(ha) &&
	    !IS_QLA27XX(ha) && !IS_QLA28XX(ha))
		goto next;

	/* Allocate dma buffer for burst write */
	optrom = dma_alloc_coherent(&ha->pdev->dev, OPTROM_BURST_SIZE,
	    &optrom_dma, GFP_KERNEL);
	if (!optrom) {
		ql_log(ql_log_warn, vha, 0x7095,
		    "Failed allocate burst (%x bytes)\n", OPTROM_BURST_SIZE);
	}

next:
	ql_log(ql_log_warn + ql_dbg_verbose, vha, 0x7095,
	    "Unprotect flash...\n");
	ret = qla24xx_unprotect_flash(vha);
	if (ret) {
		ql_log(ql_log_warn, vha, 0x7096,
		    "Failed to unprotect flash.\n");
		goto done;
	}

	rest_addr = (ha->fdt_block_size >> 2) - 1;
	sec_mask = ~rest_addr;
	for (liter = 0; liter < dwords; liter++, faddr++, dwptr++) {
		fdata = (faddr & sec_mask) << 2;

		/* Are we at the beginning of a sector? */
		if (!(faddr & rest_addr)) {
			ql_log(ql_log_warn + ql_dbg_verbose, vha, 0x7095,
			    "Erase sector %#x...\n", faddr);

			ret = qla24xx_erase_sector(vha, fdata);
			if (ret) {
				ql_dbg(ql_dbg_user, vha, 0x7007,
				    "Failed to erase sector %x.\n", faddr);
				break;
			}
		}

		if (optrom) {
			/* If smaller than a burst remaining */
			if (dwords - liter < dburst)
				dburst = dwords - liter;

			/* Copy to dma buffer */
			memcpy(optrom, dwptr, dburst << 2);

			/* Burst write */
			ql_log(ql_log_warn + ql_dbg_verbose, vha, 0x7095,
			    "Write burst (%#lx dwords)...\n", dburst);
			ret = qla2x00_load_ram(vha, optrom_dma,
			    flash_data_addr(ha, faddr), dburst);
			if (!ret) {
				liter += dburst - 1;
				faddr += dburst - 1;
				dwptr += dburst - 1;
				continue;
			}

			ql_log(ql_log_warn, vha, 0x7097,
			    "Failed burst-write at %x (%p/%#llx)....\n",
			    flash_data_addr(ha, faddr), optrom,
			    (u64)optrom_dma);

			dma_free_coherent(&ha->pdev->dev,
			    OPTROM_BURST_SIZE, optrom, optrom_dma);
			optrom = NULL;
			if (IS_QLA27XX(ha) || IS_QLA28XX(ha))
				break;
			ql_log(ql_log_warn, vha, 0x7098,
			    "Reverting to slow write...\n");
		}

		/* Slow write */
		ret = qla24xx_write_flash_dword(ha,
		    flash_data_addr(ha, faddr), le32_to_cpu(*dwptr));
		if (ret) {
			ql_dbg(ql_dbg_user, vha, 0x7006,
			    "Failed slopw write %x (%x)\n", faddr, *dwptr);
			break;
		}
	}

	ql_log(ql_log_warn + ql_dbg_verbose, vha, 0x7095,
	    "Protect flash...\n");
	ret = qla24xx_protect_flash(vha);
	if (ret)
		ql_log(ql_log_warn, vha, 0x7099,
		    "Failed to protect flash\n");
done:
	if (optrom)
		dma_free_coherent(&ha->pdev->dev,
		    OPTROM_BURST_SIZE, optrom, optrom_dma);

	return ret;
}

uint8_t *
qla2x00_read_nvram_data(scsi_qla_host_t *vha, void *buf, uint32_t naddr,
    uint32_t bytes)
{
	uint32_t i;
	__le16 *wptr;
	struct qla_hw_data *ha = vha->hw;

	/* Word reads to NVRAM via registers. */
	wptr = buf;
	qla2x00_lock_nvram_access(ha);
	for (i = 0; i < bytes >> 1; i++, naddr++)
		wptr[i] = cpu_to_le16(qla2x00_get_nvram_word(ha,
		    naddr));
	qla2x00_unlock_nvram_access(ha);

	return buf;
}

uint8_t *
qla24xx_read_nvram_data(scsi_qla_host_t *vha, void *buf, uint32_t naddr,
    uint32_t bytes)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t *dwptr = buf;
	uint32_t i;

	if (IS_P3P_TYPE(ha))
		return  buf;

	/* Dword reads to flash. */
	naddr = nvram_data_addr(ha, naddr);
	bytes >>= 2;
	for (i = 0; i < bytes; i++, naddr++, dwptr++) {
		if (qla24xx_read_flash_dword(ha, naddr, dwptr))
			break;
		cpu_to_le32s(dwptr);
	}

	return buf;
}

int
qla2x00_write_nvram_data(scsi_qla_host_t *vha, void *buf, uint32_t naddr,
    uint32_t bytes)
{
	int ret, stat;
	uint32_t i;
	uint16_t *wptr;
	unsigned long flags;
	struct qla_hw_data *ha = vha->hw;

	ret = QLA_SUCCESS;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	qla2x00_lock_nvram_access(ha);

	/* Disable NVRAM write-protection. */
	stat = qla2x00_clear_nvram_protection(ha);

	wptr = (uint16_t *)buf;
	for (i = 0; i < bytes >> 1; i++, naddr++) {
		qla2x00_write_nvram_word(ha, naddr,
		    cpu_to_le16(*wptr));
		wptr++;
	}

	/* Enable NVRAM write-protection. */
	qla2x00_set_nvram_protection(ha, stat);

	qla2x00_unlock_nvram_access(ha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return ret;
}

int
qla24xx_write_nvram_data(scsi_qla_host_t *vha, void *buf, uint32_t naddr,
    uint32_t bytes)
{
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	__le32 *dwptr = buf;
	uint32_t i;
	int ret;

	ret = QLA_SUCCESS;

	if (IS_P3P_TYPE(ha))
		return ret;

	/* Enable flash write. */
	wrt_reg_dword(&reg->ctrl_status,
	    rd_reg_dword(&reg->ctrl_status) | CSRX_FLASH_ENABLE);
	rd_reg_dword(&reg->ctrl_status);	/* PCI Posting. */

	/* Disable NVRAM write-protection. */
	qla24xx_write_flash_dword(ha, nvram_conf_addr(ha, 0x101), 0);
	qla24xx_write_flash_dword(ha, nvram_conf_addr(ha, 0x101), 0);

	/* Dword writes to flash. */
	naddr = nvram_data_addr(ha, naddr);
	bytes >>= 2;
	for (i = 0; i < bytes; i++, naddr++, dwptr++) {
		if (qla24xx_write_flash_dword(ha, naddr, le32_to_cpu(*dwptr))) {
			ql_dbg(ql_dbg_user, vha, 0x709a,
			    "Unable to program nvram address=%x data=%x.\n",
			    naddr, *dwptr);
			break;
		}
	}

	/* Enable NVRAM write-protection. */
	qla24xx_write_flash_dword(ha, nvram_conf_addr(ha, 0x101), 0x8c);

	/* Disable flash write. */
	wrt_reg_dword(&reg->ctrl_status,
	    rd_reg_dword(&reg->ctrl_status) & ~CSRX_FLASH_ENABLE);
	rd_reg_dword(&reg->ctrl_status);	/* PCI Posting. */

	return ret;
}

uint8_t *
qla25xx_read_nvram_data(scsi_qla_host_t *vha, void *buf, uint32_t naddr,
    uint32_t bytes)
{
	struct qla_hw_data *ha = vha->hw;
	uint32_t *dwptr = buf;
	uint32_t i;

	/* Dword reads to flash. */
	naddr = flash_data_addr(ha, ha->flt_region_vpd_nvram | naddr);
	bytes >>= 2;
	for (i = 0; i < bytes; i++, naddr++, dwptr++) {
		if (qla24xx_read_flash_dword(ha, naddr, dwptr))
			break;

		cpu_to_le32s(dwptr);
	}

	return buf;
}

#define RMW_BUFFER_SIZE	(64 * 1024)
int
qla25xx_write_nvram_data(scsi_qla_host_t *vha, void *buf, uint32_t naddr,
    uint32_t bytes)
{
	struct qla_hw_data *ha = vha->hw;
	uint8_t *dbuf = vmalloc(RMW_BUFFER_SIZE);

	if (!dbuf)
		return QLA_MEMORY_ALLOC_FAILED;
	ha->isp_ops->read_optrom(vha, dbuf, ha->flt_region_vpd_nvram << 2,
	    RMW_BUFFER_SIZE);
	memcpy(dbuf + (naddr << 2), buf, bytes);
	ha->isp_ops->write_optrom(vha, dbuf, ha->flt_region_vpd_nvram << 2,
	    RMW_BUFFER_SIZE);
	vfree(dbuf);

	return QLA_SUCCESS;
}

static inline void
qla2x00_flip_colors(struct qla_hw_data *ha, uint16_t *pflags)
{
	if (IS_QLA2322(ha)) {
		/* Flip all colors. */
		if (ha->beacon_color_state == QLA_LED_ALL_ON) {
			/* Turn off. */
			ha->beacon_color_state = 0;
			*pflags = GPIO_LED_ALL_OFF;
		} else {
			/* Turn on. */
			ha->beacon_color_state = QLA_LED_ALL_ON;
			*pflags = GPIO_LED_RGA_ON;
		}
	} else {
		/* Flip green led only. */
		if (ha->beacon_color_state == QLA_LED_GRN_ON) {
			/* Turn off. */
			ha->beacon_color_state = 0;
			*pflags = GPIO_LED_GREEN_OFF_AMBER_OFF;
		} else {
			/* Turn on. */
			ha->beacon_color_state = QLA_LED_GRN_ON;
			*pflags = GPIO_LED_GREEN_ON_AMBER_OFF;
		}
	}
}

#define PIO_REG(h, r) ((h)->pio_address + offsetof(struct device_reg_2xxx, r))

void
qla2x00_beacon_blink(struct scsi_qla_host *vha)
{
	uint16_t gpio_enable;
	uint16_t gpio_data;
	uint16_t led_color = 0;
	unsigned long flags;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	if (IS_P3P_TYPE(ha))
		return;

	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Save the Original GPIOE. */
	if (ha->pio_address) {
		gpio_enable = RD_REG_WORD_PIO(PIO_REG(ha, gpioe));
		gpio_data = RD_REG_WORD_PIO(PIO_REG(ha, gpiod));
	} else {
		gpio_enable = rd_reg_word(&reg->gpioe);
		gpio_data = rd_reg_word(&reg->gpiod);
	}

	/* Set the modified gpio_enable values */
	gpio_enable |= GPIO_LED_MASK;

	if (ha->pio_address) {
		WRT_REG_WORD_PIO(PIO_REG(ha, gpioe), gpio_enable);
	} else {
		wrt_reg_word(&reg->gpioe, gpio_enable);
		rd_reg_word(&reg->gpioe);
	}

	qla2x00_flip_colors(ha, &led_color);

	/* Clear out any previously set LED color. */
	gpio_data &= ~GPIO_LED_MASK;

	/* Set the new input LED color to GPIOD. */
	gpio_data |= led_color;

	/* Set the modified gpio_data values */
	if (ha->pio_address) {
		WRT_REG_WORD_PIO(PIO_REG(ha, gpiod), gpio_data);
	} else {
		wrt_reg_word(&reg->gpiod, gpio_data);
		rd_reg_word(&reg->gpiod);
	}

	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

int
qla2x00_beacon_on(struct scsi_qla_host *vha)
{
	uint16_t gpio_enable;
	uint16_t gpio_data;
	unsigned long flags;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	ha->fw_options[1] &= ~FO1_SET_EMPHASIS_SWING;
	ha->fw_options[1] |= FO1_DISABLE_GPIO6_7;

	if (qla2x00_set_fw_options(vha, ha->fw_options) != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x709b,
		    "Unable to update fw options (beacon on).\n");
		return QLA_FUNCTION_FAILED;
	}

	/* Turn off LEDs. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	if (ha->pio_address) {
		gpio_enable = RD_REG_WORD_PIO(PIO_REG(ha, gpioe));
		gpio_data = RD_REG_WORD_PIO(PIO_REG(ha, gpiod));
	} else {
		gpio_enable = rd_reg_word(&reg->gpioe);
		gpio_data = rd_reg_word(&reg->gpiod);
	}
	gpio_enable |= GPIO_LED_MASK;

	/* Set the modified gpio_enable values. */
	if (ha->pio_address) {
		WRT_REG_WORD_PIO(PIO_REG(ha, gpioe), gpio_enable);
	} else {
		wrt_reg_word(&reg->gpioe, gpio_enable);
		rd_reg_word(&reg->gpioe);
	}

	/* Clear out previously set LED colour. */
	gpio_data &= ~GPIO_LED_MASK;
	if (ha->pio_address) {
		WRT_REG_WORD_PIO(PIO_REG(ha, gpiod), gpio_data);
	} else {
		wrt_reg_word(&reg->gpiod, gpio_data);
		rd_reg_word(&reg->gpiod);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	/*
	 * Let the per HBA timer kick off the blinking process based on
	 * the following flags. No need to do anything else now.
	 */
	ha->beacon_blink_led = 1;
	ha->beacon_color_state = 0;

	return QLA_SUCCESS;
}

int
qla2x00_beacon_off(struct scsi_qla_host *vha)
{
	int rval = QLA_SUCCESS;
	struct qla_hw_data *ha = vha->hw;

	ha->beacon_blink_led = 0;

	/* Set the on flag so when it gets flipped it will be off. */
	if (IS_QLA2322(ha))
		ha->beacon_color_state = QLA_LED_ALL_ON;
	else
		ha->beacon_color_state = QLA_LED_GRN_ON;

	ha->isp_ops->beacon_blink(vha);	/* This turns green LED off */

	ha->fw_options[1] &= ~FO1_SET_EMPHASIS_SWING;
	ha->fw_options[1] &= ~FO1_DISABLE_GPIO6_7;

	rval = qla2x00_set_fw_options(vha, ha->fw_options);
	if (rval != QLA_SUCCESS)
		ql_log(ql_log_warn, vha, 0x709c,
		    "Unable to update fw options (beacon off).\n");
	return rval;
}


static inline void
qla24xx_flip_colors(struct qla_hw_data *ha, uint16_t *pflags)
{
	/* Flip all colors. */
	if (ha->beacon_color_state == QLA_LED_ALL_ON) {
		/* Turn off. */
		ha->beacon_color_state = 0;
		*pflags = 0;
	} else {
		/* Turn on. */
		ha->beacon_color_state = QLA_LED_ALL_ON;
		*pflags = GPDX_LED_YELLOW_ON | GPDX_LED_AMBER_ON;
	}
}

void
qla24xx_beacon_blink(struct scsi_qla_host *vha)
{
	uint16_t led_color = 0;
	uint32_t gpio_data;
	unsigned long flags;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	/* Save the Original GPIOD. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	gpio_data = rd_reg_dword(&reg->gpiod);

	/* Enable the gpio_data reg for update. */
	gpio_data |= GPDX_LED_UPDATE_MASK;

	wrt_reg_dword(&reg->gpiod, gpio_data);
	gpio_data = rd_reg_dword(&reg->gpiod);

	/* Set the color bits. */
	qla24xx_flip_colors(ha, &led_color);

	/* Clear out any previously set LED color. */
	gpio_data &= ~GPDX_LED_COLOR_MASK;

	/* Set the new input LED color to GPIOD. */
	gpio_data |= led_color;

	/* Set the modified gpio_data values. */
	wrt_reg_dword(&reg->gpiod, gpio_data);
	gpio_data = rd_reg_dword(&reg->gpiod);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static uint32_t
qla83xx_select_led_port(struct qla_hw_data *ha)
{
	uint32_t led_select_value = 0;

	if (!IS_QLA83XX(ha) && !IS_QLA27XX(ha) && !IS_QLA28XX(ha))
		goto out;

	if (ha->port_no == 0)
		led_select_value = QLA83XX_LED_PORT0;
	else
		led_select_value = QLA83XX_LED_PORT1;

out:
	return led_select_value;
}

void
qla83xx_beacon_blink(struct scsi_qla_host *vha)
{
	uint32_t led_select_value;
	struct qla_hw_data *ha = vha->hw;
	uint16_t led_cfg[6];
	uint16_t orig_led_cfg[6];
	uint32_t led_10_value, led_43_value;

	if (!IS_QLA83XX(ha) && !IS_QLA81XX(ha) && !IS_QLA27XX(ha) &&
	    !IS_QLA28XX(ha))
		return;

	if (!ha->beacon_blink_led)
		return;

	if (IS_QLA27XX(ha) || IS_QLA28XX(ha)) {
		qla2x00_write_ram_word(vha, 0x1003, 0x40000230);
		qla2x00_write_ram_word(vha, 0x1004, 0x40000230);
	} else if (IS_QLA2031(ha)) {
		led_select_value = qla83xx_select_led_port(ha);

		qla83xx_wr_reg(vha, led_select_value, 0x40000230);
		qla83xx_wr_reg(vha, led_select_value + 4, 0x40000230);
	} else if (IS_QLA8031(ha)) {
		led_select_value = qla83xx_select_led_port(ha);

		qla83xx_rd_reg(vha, led_select_value, &led_10_value);
		qla83xx_rd_reg(vha, led_select_value + 0x10, &led_43_value);
		qla83xx_wr_reg(vha, led_select_value, 0x01f44000);
		msleep(500);
		qla83xx_wr_reg(vha, led_select_value, 0x400001f4);
		msleep(1000);
		qla83xx_wr_reg(vha, led_select_value, led_10_value);
		qla83xx_wr_reg(vha, led_select_value + 0x10, led_43_value);
	} else if (IS_QLA81XX(ha)) {
		int rval;

		/* Save Current */
		rval = qla81xx_get_led_config(vha, orig_led_cfg);
		/* Do the blink */
		if (rval == QLA_SUCCESS) {
			if (IS_QLA81XX(ha)) {
				led_cfg[0] = 0x4000;
				led_cfg[1] = 0x2000;
				led_cfg[2] = 0;
				led_cfg[3] = 0;
				led_cfg[4] = 0;
				led_cfg[5] = 0;
			} else {
				led_cfg[0] = 0x4000;
				led_cfg[1] = 0x4000;
				led_cfg[2] = 0x4000;
				led_cfg[3] = 0x2000;
				led_cfg[4] = 0;
				led_cfg[5] = 0x2000;
			}
			rval = qla81xx_set_led_config(vha, led_cfg);
			msleep(1000);
			if (IS_QLA81XX(ha)) {
				led_cfg[0] = 0x4000;
				led_cfg[1] = 0x2000;
				led_cfg[2] = 0;
			} else {
				led_cfg[0] = 0x4000;
				led_cfg[1] = 0x2000;
				led_cfg[2] = 0x4000;
				led_cfg[3] = 0x4000;
				led_cfg[4] = 0;
				led_cfg[5] = 0x2000;
			}
			rval = qla81xx_set_led_config(vha, led_cfg);
		}
		/* On exit, restore original (presumes no status change) */
		qla81xx_set_led_config(vha, orig_led_cfg);
	}
}

int
qla24xx_beacon_on(struct scsi_qla_host *vha)
{
	uint32_t gpio_data;
	unsigned long flags;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	if (IS_P3P_TYPE(ha))
		return QLA_SUCCESS;

	if (IS_QLA8031(ha) || IS_QLA81XX(ha))
		goto skip_gpio; /* let blink handle it */

	if (ha->beacon_blink_led == 0) {
		/* Enable firmware for update */
		ha->fw_options[1] |= ADD_FO1_DISABLE_GPIO_LED_CTRL;

		if (qla2x00_set_fw_options(vha, ha->fw_options) != QLA_SUCCESS)
			return QLA_FUNCTION_FAILED;

		if (qla2x00_get_fw_options(vha, ha->fw_options) !=
		    QLA_SUCCESS) {
			ql_log(ql_log_warn, vha, 0x7009,
			    "Unable to update fw options (beacon on).\n");
			return QLA_FUNCTION_FAILED;
		}

		if (IS_QLA2031(ha) || IS_QLA27XX(ha) || IS_QLA28XX(ha))
			goto skip_gpio;

		spin_lock_irqsave(&ha->hardware_lock, flags);
		gpio_data = rd_reg_dword(&reg->gpiod);

		/* Enable the gpio_data reg for update. */
		gpio_data |= GPDX_LED_UPDATE_MASK;
		wrt_reg_dword(&reg->gpiod, gpio_data);
		rd_reg_dword(&reg->gpiod);

		spin_unlock_irqrestore(&ha->hardware_lock, flags);
	}

	/* So all colors blink together. */
	ha->beacon_color_state = 0;

skip_gpio:
	/* Let the per HBA timer kick off the blinking process. */
	ha->beacon_blink_led = 1;

	return QLA_SUCCESS;
}

int
qla24xx_beacon_off(struct scsi_qla_host *vha)
{
	uint32_t gpio_data;
	unsigned long flags;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	if (IS_P3P_TYPE(ha))
		return QLA_SUCCESS;

	if (!ha->flags.fw_started)
		return QLA_SUCCESS;

	ha->beacon_blink_led = 0;

	if (IS_QLA2031(ha) || IS_QLA27XX(ha) || IS_QLA28XX(ha))
		goto set_fw_options;

	if (IS_QLA8031(ha) || IS_QLA81XX(ha))
		return QLA_SUCCESS;

	ha->beacon_color_state = QLA_LED_ALL_ON;

	ha->isp_ops->beacon_blink(vha);	/* Will flip to all off. */

	/* Give control back to firmware. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	gpio_data = rd_reg_dword(&reg->gpiod);

	/* Disable the gpio_data reg for update. */
	gpio_data &= ~GPDX_LED_UPDATE_MASK;
	wrt_reg_dword(&reg->gpiod, gpio_data);
	rd_reg_dword(&reg->gpiod);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

set_fw_options:
	ha->fw_options[1] &= ~ADD_FO1_DISABLE_GPIO_LED_CTRL;

	if (qla2x00_set_fw_options(vha, ha->fw_options) != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x704d,
		    "Unable to update fw options (beacon on).\n");
		return QLA_FUNCTION_FAILED;
	}

	if (qla2x00_get_fw_options(vha, ha->fw_options) != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0x704e,
		    "Unable to update fw options (beacon on).\n");
		return QLA_FUNCTION_FAILED;
	}

	return QLA_SUCCESS;
}


/*
 * Flash support routines
 */

/**
 * qla2x00_flash_enable() - Setup flash for reading and writing.
 * @ha: HA context
 */
static void
qla2x00_flash_enable(struct qla_hw_data *ha)
{
	uint16_t data;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	data = rd_reg_word(&reg->ctrl_status);
	data |= CSR_FLASH_ENABLE;
	wrt_reg_word(&reg->ctrl_status, data);
	rd_reg_word(&reg->ctrl_status);		/* PCI Posting. */
}

/**
 * qla2x00_flash_disable() - Disable flash and allow RISC to run.
 * @ha: HA context
 */
static void
qla2x00_flash_disable(struct qla_hw_data *ha)
{
	uint16_t data;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	data = rd_reg_word(&reg->ctrl_status);
	data &= ~(CSR_FLASH_ENABLE);
	wrt_reg_word(&reg->ctrl_status, data);
	rd_reg_word(&reg->ctrl_status);		/* PCI Posting. */
}

/**
 * qla2x00_read_flash_byte() - Reads a byte from flash
 * @ha: HA context
 * @addr: Address in flash to read
 *
 * A word is read from the chip, but, only the lower byte is valid.
 *
 * Returns the byte read from flash @addr.
 */
static uint8_t
qla2x00_read_flash_byte(struct qla_hw_data *ha, uint32_t addr)
{
	uint16_t data;
	uint16_t bank_select;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	bank_select = rd_reg_word(&reg->ctrl_status);

	if (IS_QLA2322(ha) || IS_QLA6322(ha)) {
		/* Specify 64K address range: */
		/*  clear out Module Select and Flash Address bits [19:16]. */
		bank_select &= ~0xf8;
		bank_select |= addr >> 12 & 0xf0;
		bank_select |= CSR_FLASH_64K_BANK;
		wrt_reg_word(&reg->ctrl_status, bank_select);
		rd_reg_word(&reg->ctrl_status);	/* PCI Posting. */

		wrt_reg_word(&reg->flash_address, (uint16_t)addr);
		data = rd_reg_word(&reg->flash_data);

		return (uint8_t)data;
	}

	/* Setup bit 16 of flash address. */
	if ((addr & BIT_16) && ((bank_select & CSR_FLASH_64K_BANK) == 0)) {
		bank_select |= CSR_FLASH_64K_BANK;
		wrt_reg_word(&reg->ctrl_status, bank_select);
		rd_reg_word(&reg->ctrl_status);	/* PCI Posting. */
	} else if (((addr & BIT_16) == 0) &&
	    (bank_select & CSR_FLASH_64K_BANK)) {
		bank_select &= ~(CSR_FLASH_64K_BANK);
		wrt_reg_word(&reg->ctrl_status, bank_select);
		rd_reg_word(&reg->ctrl_status);	/* PCI Posting. */
	}

	/* Always perform IO mapped accesses to the FLASH registers. */
	if (ha->pio_address) {
		uint16_t data2;

		WRT_REG_WORD_PIO(PIO_REG(ha, flash_address), (uint16_t)addr);
		do {
			data = RD_REG_WORD_PIO(PIO_REG(ha, flash_data));
			barrier();
			cpu_relax();
			data2 = RD_REG_WORD_PIO(PIO_REG(ha, flash_data));
		} while (data != data2);
	} else {
		wrt_reg_word(&reg->flash_address, (uint16_t)addr);
		data = qla2x00_debounce_register(&reg->flash_data);
	}

	return (uint8_t)data;
}

/**
 * qla2x00_write_flash_byte() - Write a byte to flash
 * @ha: HA context
 * @addr: Address in flash to write
 * @data: Data to write
 */
static void
qla2x00_write_flash_byte(struct qla_hw_data *ha, uint32_t addr, uint8_t data)
{
	uint16_t bank_select;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	bank_select = rd_reg_word(&reg->ctrl_status);
	if (IS_QLA2322(ha) || IS_QLA6322(ha)) {
		/* Specify 64K address range: */
		/*  clear out Module Select and Flash Address bits [19:16]. */
		bank_select &= ~0xf8;
		bank_select |= addr >> 12 & 0xf0;
		bank_select |= CSR_FLASH_64K_BANK;
		wrt_reg_word(&reg->ctrl_status, bank_select);
		rd_reg_word(&reg->ctrl_status);	/* PCI Posting. */

		wrt_reg_word(&reg->flash_address, (uint16_t)addr);
		rd_reg_word(&reg->ctrl_status);		/* PCI Posting. */
		wrt_reg_word(&reg->flash_data, (uint16_t)data);
		rd_reg_word(&reg->ctrl_status);		/* PCI Posting. */

		return;
	}

	/* Setup bit 16 of flash address. */
	if ((addr & BIT_16) && ((bank_select & CSR_FLASH_64K_BANK) == 0)) {
		bank_select |= CSR_FLASH_64K_BANK;
		wrt_reg_word(&reg->ctrl_status, bank_select);
		rd_reg_word(&reg->ctrl_status);	/* PCI Posting. */
	} else if (((addr & BIT_16) == 0) &&
	    (bank_select & CSR_FLASH_64K_BANK)) {
		bank_select &= ~(CSR_FLASH_64K_BANK);
		wrt_reg_word(&reg->ctrl_status, bank_select);
		rd_reg_word(&reg->ctrl_status);	/* PCI Posting. */
	}

	/* Always perform IO mapped accesses to the FLASH registers. */
	if (ha->pio_address) {
		WRT_REG_WORD_PIO(PIO_REG(ha, flash_address), (uint16_t)addr);
		WRT_REG_WORD_PIO(PIO_REG(ha, flash_data), (uint16_t)data);
	} else {
		wrt_reg_word(&reg->flash_address, (uint16_t)addr);
		rd_reg_word(&reg->ctrl_status);		/* PCI Posting. */
		wrt_reg_word(&reg->flash_data, (uint16_t)data);
		rd_reg_word(&reg->ctrl_status);		/* PCI Posting. */
	}
}

/**
 * qla2x00_poll_flash() - Polls flash for completion.
 * @ha: HA context
 * @addr: Address in flash to poll
 * @poll_data: Data to be polled
 * @man_id: Flash manufacturer ID
 * @flash_id: Flash ID
 *
 * This function polls the device until bit 7 of what is read matches data
 * bit 7 or until data bit 5 becomes a 1.  If that hapens, the flash ROM timed
 * out (a fatal error).  The flash book recommeds reading bit 7 again after
 * reading bit 5 as a 1.
 *
 * Returns 0 on success, else non-zero.
 */
static int
qla2x00_poll_flash(struct qla_hw_data *ha, uint32_t addr, uint8_t poll_data,
    uint8_t man_id, uint8_t flash_id)
{
	int status;
	uint8_t flash_data;
	uint32_t cnt;

	status = 1;

	/* Wait for 30 seconds for command to finish. */
	poll_data &= BIT_7;
	for (cnt = 3000000; cnt; cnt--) {
		flash_data = qla2x00_read_flash_byte(ha, addr);
		if ((flash_data & BIT_7) == poll_data) {
			status = 0;
			break;
		}

		if (man_id != 0x40 && man_id != 0xda) {
			if ((flash_data & BIT_5) && cnt > 2)
				cnt = 2;
		}
		udelay(10);
		barrier();
		cond_resched();
	}
	return status;
}

/**
 * qla2x00_program_flash_address() - Programs a flash address
 * @ha: HA context
 * @addr: Address in flash to program
 * @data: Data to be written in flash
 * @man_id: Flash manufacturer ID
 * @flash_id: Flash ID
 *
 * Returns 0 on success, else non-zero.
 */
static int
qla2x00_program_flash_address(struct qla_hw_data *ha, uint32_t addr,
    uint8_t data, uint8_t man_id, uint8_t flash_id)
{
	/* Write Program Command Sequence. */
	if (IS_OEM_001(ha)) {
		qla2x00_write_flash_byte(ha, 0xaaa, 0xaa);
		qla2x00_write_flash_byte(ha, 0x555, 0x55);
		qla2x00_write_flash_byte(ha, 0xaaa, 0xa0);
		qla2x00_write_flash_byte(ha, addr, data);
	} else {
		if (man_id == 0xda && flash_id == 0xc1) {
			qla2x00_write_flash_byte(ha, addr, data);
			if (addr & 0x7e)
				return 0;
		} else {
			qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
			qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
			qla2x00_write_flash_byte(ha, 0x5555, 0xa0);
			qla2x00_write_flash_byte(ha, addr, data);
		}
	}

	udelay(150);

	/* Wait for write to complete. */
	return qla2x00_poll_flash(ha, addr, data, man_id, flash_id);
}

/**
 * qla2x00_erase_flash() - Erase the flash.
 * @ha: HA context
 * @man_id: Flash manufacturer ID
 * @flash_id: Flash ID
 *
 * Returns 0 on success, else non-zero.
 */
static int
qla2x00_erase_flash(struct qla_hw_data *ha, uint8_t man_id, uint8_t flash_id)
{
	/* Individual Sector Erase Command Sequence */
	if (IS_OEM_001(ha)) {
		qla2x00_write_flash_byte(ha, 0xaaa, 0xaa);
		qla2x00_write_flash_byte(ha, 0x555, 0x55);
		qla2x00_write_flash_byte(ha, 0xaaa, 0x80);
		qla2x00_write_flash_byte(ha, 0xaaa, 0xaa);
		qla2x00_write_flash_byte(ha, 0x555, 0x55);
		qla2x00_write_flash_byte(ha, 0xaaa, 0x10);
	} else {
		qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
		qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
		qla2x00_write_flash_byte(ha, 0x5555, 0x80);
		qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
		qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
		qla2x00_write_flash_byte(ha, 0x5555, 0x10);
	}

	udelay(150);

	/* Wait for erase to complete. */
	return qla2x00_poll_flash(ha, 0x00, 0x80, man_id, flash_id);
}

/**
 * qla2x00_erase_flash_sector() - Erase a flash sector.
 * @ha: HA context
 * @addr: Flash sector to erase
 * @sec_mask: Sector address mask
 * @man_id: Flash manufacturer ID
 * @flash_id: Flash ID
 *
 * Returns 0 on success, else non-zero.
 */
static int
qla2x00_erase_flash_sector(struct qla_hw_data *ha, uint32_t addr,
    uint32_t sec_mask, uint8_t man_id, uint8_t flash_id)
{
	/* Individual Sector Erase Command Sequence */
	qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
	qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
	qla2x00_write_flash_byte(ha, 0x5555, 0x80);
	qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
	qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
	if (man_id == 0x1f && flash_id == 0x13)
		qla2x00_write_flash_byte(ha, addr & sec_mask, 0x10);
	else
		qla2x00_write_flash_byte(ha, addr & sec_mask, 0x30);

	udelay(150);

	/* Wait for erase to complete. */
	return qla2x00_poll_flash(ha, addr, 0x80, man_id, flash_id);
}

/**
 * qla2x00_get_flash_manufacturer() - Read manufacturer ID from flash chip.
 * @ha: host adapter
 * @man_id: Flash manufacturer ID
 * @flash_id: Flash ID
 */
static void
qla2x00_get_flash_manufacturer(struct qla_hw_data *ha, uint8_t *man_id,
    uint8_t *flash_id)
{
	qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
	qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
	qla2x00_write_flash_byte(ha, 0x5555, 0x90);
	*man_id = qla2x00_read_flash_byte(ha, 0x0000);
	*flash_id = qla2x00_read_flash_byte(ha, 0x0001);
	qla2x00_write_flash_byte(ha, 0x5555, 0xaa);
	qla2x00_write_flash_byte(ha, 0x2aaa, 0x55);
	qla2x00_write_flash_byte(ha, 0x5555, 0xf0);
}

static void
qla2x00_read_flash_data(struct qla_hw_data *ha, uint8_t *tmp_buf,
	uint32_t saddr, uint32_t length)
{
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	uint32_t midpoint, ilength;
	uint8_t data;

	midpoint = length / 2;

	wrt_reg_word(&reg->nvram, 0);
	rd_reg_word(&reg->nvram);
	for (ilength = 0; ilength < length; saddr++, ilength++, tmp_buf++) {
		if (ilength == midpoint) {
			wrt_reg_word(&reg->nvram, NVR_SELECT);
			rd_reg_word(&reg->nvram);
		}
		data = qla2x00_read_flash_byte(ha, saddr);
		if (saddr % 100)
			udelay(10);
		*tmp_buf = data;
		cond_resched();
	}
}

static inline void
qla2x00_suspend_hba(struct scsi_qla_host *vha)
{
	int cnt;
	unsigned long flags;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	/* Suspend HBA. */
	scsi_block_requests(vha->host);
	ha->isp_ops->disable_intrs(ha);
	set_bit(MBX_UPDATE_FLASH_ACTIVE, &ha->mbx_cmd_flags);

	/* Pause RISC. */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	wrt_reg_word(&reg->hccr, HCCR_PAUSE_RISC);
	rd_reg_word(&reg->hccr);
	if (IS_QLA2100(ha) || IS_QLA2200(ha) || IS_QLA2300(ha)) {
		for (cnt = 0; cnt < 30000; cnt++) {
			if ((rd_reg_word(&reg->hccr) & HCCR_RISC_PAUSE) != 0)
				break;
			udelay(100);
		}
	} else {
		udelay(10);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

static inline void
qla2x00_resume_hba(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;

	/* Resume HBA. */
	clear_bit(MBX_UPDATE_FLASH_ACTIVE, &ha->mbx_cmd_flags);
	set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
	qla2xxx_wake_dpc(vha);
	qla2x00_wait_for_chip_reset(vha);
	scsi_unblock_requests(vha->host);
}

void *
qla2x00_read_optrom_data(struct scsi_qla_host *vha, void *buf,
    uint32_t offset, uint32_t length)
{
	uint32_t addr, midpoint;
	uint8_t *data;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	/* Suspend HBA. */
	qla2x00_suspend_hba(vha);

	/* Go with read. */
	midpoint = ha->optrom_size / 2;

	qla2x00_flash_enable(ha);
	wrt_reg_word(&reg->nvram, 0);
	rd_reg_word(&reg->nvram);		/* PCI Posting. */
	for (addr = offset, data = buf; addr < length; addr++, data++) {
		if (addr == midpoint) {
			wrt_reg_word(&reg->nvram, NVR_SELECT);
			rd_reg_word(&reg->nvram);	/* PCI Posting. */
		}

		*data = qla2x00_read_flash_byte(ha, addr);
	}
	qla2x00_flash_disable(ha);

	/* Resume HBA. */
	qla2x00_resume_hba(vha);

	return buf;
}

int
qla2x00_write_optrom_data(struct scsi_qla_host *vha, void *buf,
    uint32_t offset, uint32_t length)
{

	int rval;
	uint8_t man_id, flash_id, sec_number, *data;
	uint16_t wd;
	uint32_t addr, liter, sec_mask, rest_addr;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	/* Suspend HBA. */
	qla2x00_suspend_hba(vha);

	rval = QLA_SUCCESS;
	sec_number = 0;

	/* Reset ISP chip. */
	wrt_reg_word(&reg->ctrl_status, CSR_ISP_SOFT_RESET);
	pci_read_config_word(ha->pdev, PCI_COMMAND, &wd);

	/* Go with write. */
	qla2x00_flash_enable(ha);
	do {	/* Loop once to provide quick error exit */
		/* Structure of flash memory based on manufacturer */
		if (IS_OEM_001(ha)) {
			/* OEM variant with special flash part. */
			man_id = flash_id = 0;
			rest_addr = 0xffff;
			sec_mask   = 0x10000;
			goto update_flash;
		}
		qla2x00_get_flash_manufacturer(ha, &man_id, &flash_id);
		switch (man_id) {
		case 0x20: /* ST flash. */
			if (flash_id == 0xd2 || flash_id == 0xe3) {
				/*
				 * ST m29w008at part - 64kb sector size with
				 * 32kb,8kb,8kb,16kb sectors at memory address
				 * 0xf0000.
				 */
				rest_addr = 0xffff;
				sec_mask = 0x10000;
				break;
			}
			/*
			 * ST m29w010b part - 16kb sector size
			 * Default to 16kb sectors
			 */
			rest_addr = 0x3fff;
			sec_mask = 0x1c000;
			break;
		case 0x40: /* Mostel flash. */
			/* Mostel v29c51001 part - 512 byte sector size. */
			rest_addr = 0x1ff;
			sec_mask = 0x1fe00;
			break;
		case 0xbf: /* SST flash. */
			/* SST39sf10 part - 4kb sector size. */
			rest_addr = 0xfff;
			sec_mask = 0x1f000;
			break;
		case 0xda: /* Winbond flash. */
			/* Winbond W29EE011 part - 256 byte sector size. */
			rest_addr = 0x7f;
			sec_mask = 0x1ff80;
			break;
		case 0xc2: /* Macronix flash. */
			/* 64k sector size. */
			if (flash_id == 0x38 || flash_id == 0x4f) {
				rest_addr = 0xffff;
				sec_mask = 0x10000;
				break;
			}
			fallthrough;

		case 0x1f: /* Atmel flash. */
			/* 512k sector size. */
			if (flash_id == 0x13) {
				rest_addr = 0x7fffffff;
				sec_mask =   0x80000000;
				break;
			}
			fallthrough;

		case 0x01: /* AMD flash. */
			if (flash_id == 0x38 || flash_id == 0x40 ||
			    flash_id == 0x4f) {
				/* Am29LV081 part - 64kb sector size. */
				/* Am29LV002BT part - 64kb sector size. */
				rest_addr = 0xffff;
				sec_mask = 0x10000;
				break;
			} else if (flash_id == 0x3e) {
				/*
				 * Am29LV008b part - 64kb sector size with
				 * 32kb,8kb,8kb,16kb sector at memory address
				 * h0xf0000.
				 */
				rest_addr = 0xffff;
				sec_mask = 0x10000;
				break;
			} else if (flash_id == 0x20 || flash_id == 0x6e) {
				/*
				 * Am29LV010 part or AM29f010 - 16kb sector
				 * size.
				 */
				rest_addr = 0x3fff;
				sec_mask = 0x1c000;
				break;
			} else if (flash_id == 0x6d) {
				/* Am29LV001 part - 8kb sector size. */
				rest_addr = 0x1fff;
				sec_mask = 0x1e000;
				break;
			}
			fallthrough;
		default:
			/* Default to 16 kb sector size. */
			rest_addr = 0x3fff;
			sec_mask = 0x1c000;
			break;
		}

update_flash:
		if (IS_QLA2322(ha) || IS_QLA6322(ha)) {
			if (qla2x00_erase_flash(ha, man_id, flash_id)) {
				rval = QLA_FUNCTION_FAILED;
				break;
			}
		}

		for (addr = offset, liter = 0; liter < length; liter++,
		    addr++) {
			data = buf + liter;
			/* Are we at the beginning of a sector? */
			if ((addr & rest_addr) == 0) {
				if (IS_QLA2322(ha) || IS_QLA6322(ha)) {
					if (addr >= 0x10000UL) {
						if (((addr >> 12) & 0xf0) &&
						    ((man_id == 0x01 &&
							flash_id == 0x3e) ||
						     (man_id == 0x20 &&
							 flash_id == 0xd2))) {
							sec_number++;
							if (sec_number == 1) {
								rest_addr =
								    0x7fff;
								sec_mask =
								    0x18000;
							} else if (
							    sec_number == 2 ||
							    sec_number == 3) {
								rest_addr =
								    0x1fff;
								sec_mask =
								    0x1e000;
							} else if (
							    sec_number == 4) {
								rest_addr =
								    0x3fff;
								sec_mask =
								    0x1c000;
							}
						}
					}
				} else if (addr == ha->optrom_size / 2) {
					wrt_reg_word(&reg->nvram, NVR_SELECT);
					rd_reg_word(&reg->nvram);
				}

				if (flash_id == 0xda && man_id == 0xc1) {
					qla2x00_write_flash_byte(ha, 0x5555,
					    0xaa);
					qla2x00_write_flash_byte(ha, 0x2aaa,
					    0x55);
					qla2x00_write_flash_byte(ha, 0x5555,
					    0xa0);
				} else if (!IS_QLA2322(ha) && !IS_QLA6322(ha)) {
					/* Then erase it */
					if (qla2x00_erase_flash_sector(ha,
					    addr, sec_mask, man_id,
					    flash_id)) {
						rval = QLA_FUNCTION_FAILED;
						break;
					}
					if (man_id == 0x01 && flash_id == 0x6d)
						sec_number++;
				}
			}

			if (man_id == 0x01 && flash_id == 0x6d) {
				if (sec_number == 1 &&
				    addr == (rest_addr - 1)) {
					rest_addr = 0x0fff;
					sec_mask   = 0x1f000;
				} else if (sec_number == 3 && (addr & 0x7ffe)) {
					rest_addr = 0x3fff;
					sec_mask   = 0x1c000;
				}
			}

			if (qla2x00_program_flash_address(ha, addr, *data,
			    man_id, flash_id)) {
				rval = QLA_FUNCTION_FAILED;
				break;
			}
			cond_resched();
		}
	} while (0);
	qla2x00_flash_disable(ha);

	/* Resume HBA. */
	qla2x00_resume_hba(vha);

	return rval;
}

void *
qla24xx_read_optrom_data(struct scsi_qla_host *vha, void *buf,
    uint32_t offset, uint32_t length)
{
	struct qla_hw_data *ha = vha->hw;

	/* Suspend HBA. */
	scsi_block_requests(vha->host);
	set_bit(MBX_UPDATE_FLASH_ACTIVE, &ha->mbx_cmd_flags);

	/* Go with read. */
	qla24xx_read_flash_data(vha, buf, offset >> 2, length >> 2);

	/* Resume HBA. */
	clear_bit(MBX_UPDATE_FLASH_ACTIVE, &ha->mbx_cmd_flags);
	scsi_unblock_requests(vha->host);

	return buf;
}

static int
qla28xx_extract_sfub_and_verify(struct scsi_qla_host *vha, uint32_t *buf,
    uint32_t len, uint32_t buf_size_without_sfub, uint8_t *sfub_buf)
{
	uint32_t *p, check_sum = 0;
	int i;

	p = buf + buf_size_without_sfub;

	/* Extract SFUB from end of file */
	memcpy(sfub_buf, (uint8_t *)p,
	    sizeof(struct secure_flash_update_block));

	for (i = 0; i < (sizeof(struct secure_flash_update_block) >> 2); i++)
		check_sum += p[i];

	check_sum = (~check_sum) + 1;

	if (check_sum != p[i]) {
		ql_log(ql_log_warn, vha, 0x7097,
		    "SFUB checksum failed, 0x%x, 0x%x\n",
		    check_sum, p[i]);
		return QLA_COMMAND_ERROR;
	}

	return QLA_SUCCESS;
}

static int
qla28xx_get_flash_region(struct scsi_qla_host *vha, uint32_t start,
    struct qla_flt_region *region)
{
	struct qla_hw_data *ha = vha->hw;
	struct qla_flt_header *flt = ha->flt;
	struct qla_flt_region *flt_reg = &flt->region[0];
	uint16_t cnt;
	int rval = QLA_FUNCTION_FAILED;

	if (!ha->flt)
		return QLA_FUNCTION_FAILED;

	cnt = le16_to_cpu(flt->length) / sizeof(struct qla_flt_region);
	for (; cnt; cnt--, flt_reg++) {
		if (le32_to_cpu(flt_reg->start) == start) {
			memcpy((uint8_t *)region, flt_reg,
			    sizeof(struct qla_flt_region));
			rval = QLA_SUCCESS;
			break;
		}
	}

	return rval;
}

static int
qla28xx_write_flash_data(scsi_qla_host_t *vha, uint32_t *dwptr, uint32_t faddr,
    uint32_t dwords)
{
	struct qla_hw_data *ha = vha->hw;
	ulong liter;
	ulong dburst = OPTROM_BURST_DWORDS; /* burst size in dwords */
	uint32_t sec_mask, rest_addr, fdata;
	void *optrom = NULL;
	dma_addr_t optrom_dma;
	int rval, ret;
	struct secure_flash_update_block *sfub;
	dma_addr_t sfub_dma;
	uint32_t offset = faddr << 2;
	uint32_t buf_size_without_sfub = 0;
	struct qla_flt_region region;
	bool reset_to_rom = false;
	uint32_t risc_size, risc_attr = 0;
	__be32 *fw_array = NULL;

	/* Retrieve region info - must be a start address passed in */
	rval = qla28xx_get_flash_region(vha, offset, &region);

	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0xffff,
		    "Invalid address %x - not a region start address\n",
		    offset);
		goto done;
	}

	/* Allocate dma buffer for burst write */
	optrom = dma_alloc_coherent(&ha->pdev->dev, OPTROM_BURST_SIZE,
	    &optrom_dma, GFP_KERNEL);
	if (!optrom) {
		ql_log(ql_log_warn, vha, 0x7095,
		    "Failed allocate burst (%x bytes)\n", OPTROM_BURST_SIZE);
		rval = QLA_COMMAND_ERROR;
		goto done;
	}

	/*
	 * If adapter supports secure flash and region is secure
	 * extract secure flash update block (SFUB) and verify
	 */
	if (ha->flags.secure_adapter && region.attribute) {

		ql_log(ql_log_warn + ql_dbg_verbose, vha, 0xffff,
		    "Region %x is secure\n", region.code);

		switch (le16_to_cpu(region.code)) {
		case FLT_REG_FW:
		case FLT_REG_FW_SEC_27XX:
		case FLT_REG_MPI_PRI_28XX:
		case FLT_REG_MPI_SEC_28XX:
			fw_array = (__force __be32 *)dwptr;

			/* 1st fw array */
			risc_size = be32_to_cpu(fw_array[3]);
			risc_attr = be32_to_cpu(fw_array[9]);

			buf_size_without_sfub = risc_size;
			fw_array += risc_size;

			/* 2nd fw array */
			risc_size = be32_to_cpu(fw_array[3]);

			buf_size_without_sfub += risc_size;
			fw_array += risc_size;

			/* 1st dump template */
			risc_size = be32_to_cpu(fw_array[2]);

			/* skip header and ignore checksum */
			buf_size_without_sfub += risc_size;
			fw_array += risc_size;

			if (risc_attr & BIT_9) {
				/* 2nd dump template */
				risc_size = be32_to_cpu(fw_array[2]);

				/* skip header and ignore checksum */
				buf_size_without_sfub += risc_size;
				fw_array += risc_size;
			}
			break;

		case FLT_REG_PEP_PRI_28XX:
		case FLT_REG_PEP_SEC_28XX:
			fw_array = (__force __be32 *)dwptr;

			/* 1st fw array */
			risc_size = be32_to_cpu(fw_array[3]);
			risc_attr = be32_to_cpu(fw_array[9]);

			buf_size_without_sfub = risc_size;
			fw_array += risc_size;
			break;

		default:
			ql_log(ql_log_warn + ql_dbg_verbose, vha,
			    0xffff, "Secure region %x not supported\n",
			    region.code);
			rval = QLA_COMMAND_ERROR;
			goto done;
		}

		sfub = dma_alloc_coherent(&ha->pdev->dev,
			sizeof(struct secure_flash_update_block), &sfub_dma,
			GFP_KERNEL);
		if (!sfub) {
			ql_log(ql_log_warn, vha, 0xffff,
			    "Unable to allocate memory for SFUB\n");
			rval = QLA_COMMAND_ERROR;
			goto done;
		}

		rval = qla28xx_extract_sfub_and_verify(vha, dwptr, dwords,
			buf_size_without_sfub, (uint8_t *)sfub);

		if (rval != QLA_SUCCESS)
			goto done;

		ql_log(ql_log_warn + ql_dbg_verbose, vha, 0xffff,
		    "SFUB extract and verify successful\n");
	}

	rest_addr = (ha->fdt_block_size >> 2) - 1;
	sec_mask = ~rest_addr;

	/* Lock semaphore */
	rval = qla81xx_fac_semaphore_access(vha, FAC_SEMAPHORE_LOCK);
	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0xffff,
		    "Unable to lock flash semaphore.");
		goto done;
	}

	ql_log(ql_log_warn + ql_dbg_verbose, vha, 0x7095,
	    "Unprotect flash...\n");
	rval = qla24xx_unprotect_flash(vha);
	if (rval) {
		qla81xx_fac_semaphore_access(vha, FAC_SEMAPHORE_UNLOCK);
		ql_log(ql_log_warn, vha, 0x7096, "Failed unprotect flash\n");
		goto done;
	}

	for (liter = 0; liter < dwords; liter++, faddr++) {
		fdata = (faddr & sec_mask) << 2;

		/* If start of sector */
		if (!(faddr & rest_addr)) {
			ql_log(ql_log_warn + ql_dbg_verbose, vha, 0x7095,
			    "Erase sector %#x...\n", faddr);
			rval = qla24xx_erase_sector(vha, fdata);
			if (rval) {
				ql_dbg(ql_dbg_user, vha, 0x7007,
				    "Failed erase sector %#x\n", faddr);
				goto write_protect;
			}
		}
	}

	if (ha->flags.secure_adapter) {
		/*
		 * If adapter supports secure flash but FW doesn't,
		 * disable write protect, release semaphore and reset
		 * chip to execute ROM code in order to update region securely
		 */
		if (!ha->flags.secure_fw) {
			ql_log(ql_log_warn + ql_dbg_verbose, vha, 0xffff,
			    "Disable Write and Release Semaphore.");
			rval = qla24xx_protect_flash(vha);
			if (rval != QLA_SUCCESS) {
				qla81xx_fac_semaphore_access(vha,
					FAC_SEMAPHORE_UNLOCK);
				ql_log(ql_log_warn, vha, 0xffff,
				    "Unable to protect flash.");
				goto done;
			}

			ql_log(ql_log_warn + ql_dbg_verbose, vha, 0xffff,
			    "Reset chip to ROM.");
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
			set_bit(ISP_ABORT_TO_ROM, &vha->dpc_flags);
			qla2xxx_wake_dpc(vha);
			rval = qla2x00_wait_for_chip_reset(vha);
			if (rval != QLA_SUCCESS) {
				ql_log(ql_log_warn, vha, 0xffff,
				    "Unable to reset to ROM code.");
				goto done;
			}
			reset_to_rom = true;
			ha->flags.fac_supported = 0;

			ql_log(ql_log_warn + ql_dbg_verbose, vha, 0xffff,
			    "Lock Semaphore");
			rval = qla2xxx_write_remote_register(vha,
			    FLASH_SEMAPHORE_REGISTER_ADDR, 0x00020002);
			if (rval != QLA_SUCCESS) {
				ql_log(ql_log_warn, vha, 0xffff,
				    "Unable to lock flash semaphore.");
				goto done;
			}

			/* Unprotect flash */
			ql_log(ql_log_warn + ql_dbg_verbose, vha, 0xffff,
			    "Enable Write.");
			rval = qla2x00_write_ram_word(vha, 0x7ffd0101, 0);
			if (rval) {
				ql_log(ql_log_warn, vha, 0x7096,
				    "Failed unprotect flash\n");
				goto done;
			}
		}

		/* If region is secure, send Secure Flash MB Cmd */
		if (region.attribute && buf_size_without_sfub) {
			ql_log(ql_log_warn + ql_dbg_verbose, vha, 0xffff,
			    "Sending Secure Flash MB Cmd\n");
			rval = qla28xx_secure_flash_update(vha, 0,
				le16_to_cpu(region.code),
				buf_size_without_sfub, sfub_dma,
				sizeof(struct secure_flash_update_block) >> 2);
			if (rval != QLA_SUCCESS) {
				ql_log(ql_log_warn, vha, 0xffff,
				    "Secure Flash MB Cmd failed %x.", rval);
				goto write_protect;
			}
		}

	}

	/* re-init flash offset */
	faddr = offset >> 2;

	for (liter = 0; liter < dwords; liter++, faddr++, dwptr++) {
		fdata = (faddr & sec_mask) << 2;

		/* If smaller than a burst remaining */
		if (dwords - liter < dburst)
			dburst = dwords - liter;

		/* Copy to dma buffer */
		memcpy(optrom, dwptr, dburst << 2);

		/* Burst write */
		ql_log(ql_log_warn + ql_dbg_verbose, vha, 0x7095,
		    "Write burst (%#lx dwords)...\n", dburst);
		rval = qla2x00_load_ram(vha, optrom_dma,
		    flash_data_addr(ha, faddr), dburst);
		if (rval != QLA_SUCCESS) {
			ql_log(ql_log_warn, vha, 0x7097,
			    "Failed burst write at %x (%p/%#llx)...\n",
			    flash_data_addr(ha, faddr), optrom,
			    (u64)optrom_dma);
			break;
		}

		liter += dburst - 1;
		faddr += dburst - 1;
		dwptr += dburst - 1;
		continue;
	}

write_protect:
	ql_log(ql_log_warn + ql_dbg_verbose, vha, 0x7095,
	    "Protect flash...\n");
	ret = qla24xx_protect_flash(vha);
	if (ret) {
		qla81xx_fac_semaphore_access(vha, FAC_SEMAPHORE_UNLOCK);
		ql_log(ql_log_warn, vha, 0x7099,
		    "Failed protect flash\n");
		rval = QLA_COMMAND_ERROR;
	}

	if (reset_to_rom == true) {
		/* Schedule DPC to restart the RISC */
		set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
		qla2xxx_wake_dpc(vha);

		ret = qla2x00_wait_for_hba_online(vha);
		if (ret != QLA_SUCCESS) {
			ql_log(ql_log_warn, vha, 0xffff,
			    "Adapter did not come out of reset\n");
			rval = QLA_COMMAND_ERROR;
		}
	}

done:
	if (optrom)
		dma_free_coherent(&ha->pdev->dev,
		    OPTROM_BURST_SIZE, optrom, optrom_dma);

	return rval;
}

int
qla24xx_write_optrom_data(struct scsi_qla_host *vha, void *buf,
    uint32_t offset, uint32_t length)
{
	int rval;
	struct qla_hw_data *ha = vha->hw;

	/* Suspend HBA. */
	scsi_block_requests(vha->host);
	set_bit(MBX_UPDATE_FLASH_ACTIVE, &ha->mbx_cmd_flags);

	/* Go with write. */
	if (IS_QLA28XX(ha))
		rval = qla28xx_write_flash_data(vha, buf, offset >> 2,
						length >> 2);
	else
		rval = qla24xx_write_flash_data(vha, buf, offset >> 2,
						length >> 2);

	clear_bit(MBX_UPDATE_FLASH_ACTIVE, &ha->mbx_cmd_flags);
	scsi_unblock_requests(vha->host);

	return rval;
}

void *
qla25xx_read_optrom_data(struct scsi_qla_host *vha, void *buf,
    uint32_t offset, uint32_t length)
{
	int rval;
	dma_addr_t optrom_dma;
	void *optrom;
	uint8_t *pbuf;
	uint32_t faddr, left, burst;
	struct qla_hw_data *ha = vha->hw;

	if (IS_QLA25XX(ha) || IS_QLA81XX(ha) || IS_QLA83XX(ha) ||
	    IS_QLA27XX(ha) || IS_QLA28XX(ha))
		goto try_fast;
	if (offset & 0xfff)
		goto slow_read;
	if (length < OPTROM_BURST_SIZE)
		goto slow_read;

try_fast:
	if (offset & 0xff)
		goto slow_read;
	optrom = dma_alloc_coherent(&ha->pdev->dev, OPTROM_BURST_SIZE,
	    &optrom_dma, GFP_KERNEL);
	if (!optrom) {
		ql_log(ql_log_warn, vha, 0x00cc,
		    "Unable to allocate memory for optrom burst read (%x KB).\n",
		    OPTROM_BURST_SIZE / 1024);
		goto slow_read;
	}

	pbuf = buf;
	faddr = offset >> 2;
	left = length >> 2;
	burst = OPTROM_BURST_DWORDS;
	while (left != 0) {
		if (burst > left)
			burst = left;

		rval = qla2x00_dump_ram(vha, optrom_dma,
		    flash_data_addr(ha, faddr), burst);
		if (rval) {
			ql_log(ql_log_warn, vha, 0x00f5,
			    "Unable to burst-read optrom segment (%x/%x/%llx).\n",
			    rval, flash_data_addr(ha, faddr),
			    (unsigned long long)optrom_dma);
			ql_log(ql_log_warn, vha, 0x00f6,
			    "Reverting to slow-read.\n");

			dma_free_coherent(&ha->pdev->dev, OPTROM_BURST_SIZE,
			    optrom, optrom_dma);
			goto slow_read;
		}

		memcpy(pbuf, optrom, burst * 4);

		left -= burst;
		faddr += burst;
		pbuf += burst * 4;
	}

	dma_free_coherent(&ha->pdev->dev, OPTROM_BURST_SIZE, optrom,
	    optrom_dma);

	return buf;

slow_read:
    return qla24xx_read_optrom_data(vha, buf, offset, length);
}

/**
 * qla2x00_get_fcode_version() - Determine an FCODE image's version.
 * @ha: HA context
 * @pcids: Pointer to the FCODE PCI data structure
 *
 * The process of retrieving the FCODE version information is at best
 * described as interesting.
 *
 * Within the first 100h bytes of the image an ASCII string is present
 * which contains several pieces of information including the FCODE
 * version.  Unfortunately it seems the only reliable way to retrieve
 * the version is by scanning for another sentinel within the string,
 * the FCODE build date:
 *
 *	... 2.00.02 10/17/02 ...
 *
 * Returns QLA_SUCCESS on successful retrieval of version.
 */
static void
qla2x00_get_fcode_version(struct qla_hw_data *ha, uint32_t pcids)
{
	int ret = QLA_FUNCTION_FAILED;
	uint32_t istart, iend, iter, vend;
	uint8_t do_next, rbyte, *vbyte;

	memset(ha->fcode_revision, 0, sizeof(ha->fcode_revision));

	/* Skip the PCI data structure. */
	istart = pcids +
	    ((qla2x00_read_flash_byte(ha, pcids + 0x0B) << 8) |
		qla2x00_read_flash_byte(ha, pcids + 0x0A));
	iend = istart + 0x100;
	do {
		/* Scan for the sentinel date string...eeewww. */
		do_next = 0;
		iter = istart;
		while ((iter < iend) && !do_next) {
			iter++;
			if (qla2x00_read_flash_byte(ha, iter) == '/') {
				if (qla2x00_read_flash_byte(ha, iter + 2) ==
				    '/')
					do_next++;
				else if (qla2x00_read_flash_byte(ha,
				    iter + 3) == '/')
					do_next++;
			}
		}
		if (!do_next)
			break;

		/* Backtrack to previous ' ' (space). */
		do_next = 0;
		while ((iter > istart) && !do_next) {
			iter--;
			if (qla2x00_read_flash_byte(ha, iter) == ' ')
				do_next++;
		}
		if (!do_next)
			break;

		/*
		 * Mark end of version tag, and find previous ' ' (space) or
		 * string length (recent FCODE images -- major hack ahead!!!).
		 */
		vend = iter - 1;
		do_next = 0;
		while ((iter > istart) && !do_next) {
			iter--;
			rbyte = qla2x00_read_flash_byte(ha, iter);
			if (rbyte == ' ' || rbyte == 0xd || rbyte == 0x10)
				do_next++;
		}
		if (!do_next)
			break;

		/* Mark beginning of version tag, and copy data. */
		iter++;
		if ((vend - iter) &&
		    ((vend - iter) < sizeof(ha->fcode_revision))) {
			vbyte = ha->fcode_revision;
			while (iter <= vend) {
				*vbyte++ = qla2x00_read_flash_byte(ha, iter);
				iter++;
			}
			ret = QLA_SUCCESS;
		}
	} while (0);

	if (ret != QLA_SUCCESS)
		memset(ha->fcode_revision, 0, sizeof(ha->fcode_revision));
}

int
qla2x00_get_flash_version(scsi_qla_host_t *vha, void *mbuf)
{
	int ret = QLA_SUCCESS;
	uint8_t code_type, last_image;
	uint32_t pcihdr, pcids;
	uint8_t *dbyte;
	uint16_t *dcode;
	struct qla_hw_data *ha = vha->hw;

	if (!ha->pio_address || !mbuf)
		return QLA_FUNCTION_FAILED;

	memset(ha->bios_revision, 0, sizeof(ha->bios_revision));
	memset(ha->efi_revision, 0, sizeof(ha->efi_revision));
	memset(ha->fcode_revision, 0, sizeof(ha->fcode_revision));
	memset(ha->fw_revision, 0, sizeof(ha->fw_revision));

	qla2x00_flash_enable(ha);

	/* Begin with first PCI expansion ROM header. */
	pcihdr = 0;
	last_image = 1;
	do {
		/* Verify PCI expansion ROM header. */
		if (qla2x00_read_flash_byte(ha, pcihdr) != 0x55 ||
		    qla2x00_read_flash_byte(ha, pcihdr + 0x01) != 0xaa) {
			/* No signature */
			ql_log(ql_log_fatal, vha, 0x0050,
			    "No matching ROM signature.\n");
			ret = QLA_FUNCTION_FAILED;
			break;
		}

		/* Locate PCI data structure. */
		pcids = pcihdr +
		    ((qla2x00_read_flash_byte(ha, pcihdr + 0x19) << 8) |
			qla2x00_read_flash_byte(ha, pcihdr + 0x18));

		/* Validate signature of PCI data structure. */
		if (qla2x00_read_flash_byte(ha, pcids) != 'P' ||
		    qla2x00_read_flash_byte(ha, pcids + 0x1) != 'C' ||
		    qla2x00_read_flash_byte(ha, pcids + 0x2) != 'I' ||
		    qla2x00_read_flash_byte(ha, pcids + 0x3) != 'R') {
			/* Incorrect header. */
			ql_log(ql_log_fatal, vha, 0x0051,
			    "PCI data struct not found pcir_adr=%x.\n", pcids);
			ret = QLA_FUNCTION_FAILED;
			break;
		}

		/* Read version */
		code_type = qla2x00_read_flash_byte(ha, pcids + 0x14);
		switch (code_type) {
		case ROM_CODE_TYPE_BIOS:
			/* Intel x86, PC-AT compatible. */
			ha->bios_revision[0] =
			    qla2x00_read_flash_byte(ha, pcids + 0x12);
			ha->bios_revision[1] =
			    qla2x00_read_flash_byte(ha, pcids + 0x13);
			ql_dbg(ql_dbg_init, vha, 0x0052,
			    "Read BIOS %d.%d.\n",
			    ha->bios_revision[1], ha->bios_revision[0]);
			break;
		case ROM_CODE_TYPE_FCODE:
			/* Open Firmware standard for PCI (FCode). */
			/* Eeeewww... */
			qla2x00_get_fcode_version(ha, pcids);
			break;
		case ROM_CODE_TYPE_EFI:
			/* Extensible Firmware Interface (EFI). */
			ha->efi_revision[0] =
			    qla2x00_read_flash_byte(ha, pcids + 0x12);
			ha->efi_revision[1] =
			    qla2x00_read_flash_byte(ha, pcids + 0x13);
			ql_dbg(ql_dbg_init, vha, 0x0053,
			    "Read EFI %d.%d.\n",
			    ha->efi_revision[1], ha->efi_revision[0]);
			break;
		default:
			ql_log(ql_log_warn, vha, 0x0054,
			    "Unrecognized code type %x at pcids %x.\n",
			    code_type, pcids);
			break;
		}

		last_image = qla2x00_read_flash_byte(ha, pcids + 0x15) & BIT_7;

		/* Locate next PCI expansion ROM. */
		pcihdr += ((qla2x00_read_flash_byte(ha, pcids + 0x11) << 8) |
		    qla2x00_read_flash_byte(ha, pcids + 0x10)) * 512;
	} while (!last_image);

	if (IS_QLA2322(ha)) {
		/* Read firmware image information. */
		memset(ha->fw_revision, 0, sizeof(ha->fw_revision));
		dbyte = mbuf;
		memset(dbyte, 0, 8);
		dcode = (uint16_t *)dbyte;

		qla2x00_read_flash_data(ha, dbyte, ha->flt_region_fw * 4 + 10,
		    8);
		ql_dbg(ql_dbg_init + ql_dbg_buffer, vha, 0x010a,
		    "Dumping fw "
		    "ver from flash:.\n");
		ql_dump_buffer(ql_dbg_init + ql_dbg_buffer, vha, 0x010b,
		    dbyte, 32);

		if ((dcode[0] == 0xffff && dcode[1] == 0xffff &&
		    dcode[2] == 0xffff && dcode[3] == 0xffff) ||
		    (dcode[0] == 0 && dcode[1] == 0 && dcode[2] == 0 &&
		    dcode[3] == 0)) {
			ql_log(ql_log_warn, vha, 0x0057,
			    "Unrecognized fw revision at %x.\n",
			    ha->flt_region_fw * 4);
		} else {
			/* values are in big endian */
			ha->fw_revision[0] = dbyte[0] << 16 | dbyte[1];
			ha->fw_revision[1] = dbyte[2] << 16 | dbyte[3];
			ha->fw_revision[2] = dbyte[4] << 16 | dbyte[5];
			ql_dbg(ql_dbg_init, vha, 0x0058,
			    "FW Version: "
			    "%d.%d.%d.\n", ha->fw_revision[0],
			    ha->fw_revision[1], ha->fw_revision[2]);
		}
	}

	qla2x00_flash_disable(ha);

	return ret;
}

int
qla82xx_get_flash_version(scsi_qla_host_t *vha, void *mbuf)
{
	int ret = QLA_SUCCESS;
	uint32_t pcihdr, pcids;
	uint32_t *dcode = mbuf;
	uint8_t *bcode = mbuf;
	uint8_t code_type, last_image;
	struct qla_hw_data *ha = vha->hw;

	if (!mbuf)
		return QLA_FUNCTION_FAILED;

	memset(ha->bios_revision, 0, sizeof(ha->bios_revision));
	memset(ha->efi_revision, 0, sizeof(ha->efi_revision));
	memset(ha->fcode_revision, 0, sizeof(ha->fcode_revision));
	memset(ha->fw_revision, 0, sizeof(ha->fw_revision));

	/* Begin with first PCI expansion ROM header. */
	pcihdr = ha->flt_region_boot << 2;
	last_image = 1;
	do {
		/* Verify PCI expansion ROM header. */
		ha->isp_ops->read_optrom(vha, dcode, pcihdr, 0x20 * 4);
		bcode = mbuf + (pcihdr % 4);
		if (memcmp(bcode, "\x55\xaa", 2)) {
			/* No signature */
			ql_log(ql_log_fatal, vha, 0x0154,
			    "No matching ROM signature.\n");
			ret = QLA_FUNCTION_FAILED;
			break;
		}

		/* Locate PCI data structure. */
		pcids = pcihdr + ((bcode[0x19] << 8) | bcode[0x18]);

		ha->isp_ops->read_optrom(vha, dcode, pcids, 0x20 * 4);
		bcode = mbuf + (pcihdr % 4);

		/* Validate signature of PCI data structure. */
		if (memcmp(bcode, "PCIR", 4)) {
			/* Incorrect header. */
			ql_log(ql_log_fatal, vha, 0x0155,
			    "PCI data struct not found pcir_adr=%x.\n", pcids);
			ret = QLA_FUNCTION_FAILED;
			break;
		}

		/* Read version */
		code_type = bcode[0x14];
		switch (code_type) {
		case ROM_CODE_TYPE_BIOS:
			/* Intel x86, PC-AT compatible. */
			ha->bios_revision[0] = bcode[0x12];
			ha->bios_revision[1] = bcode[0x13];
			ql_dbg(ql_dbg_init, vha, 0x0156,
			    "Read BIOS %d.%d.\n",
			    ha->bios_revision[1], ha->bios_revision[0]);
			break;
		case ROM_CODE_TYPE_FCODE:
			/* Open Firmware standard for PCI (FCode). */
			ha->fcode_revision[0] = bcode[0x12];
			ha->fcode_revision[1] = bcode[0x13];
			ql_dbg(ql_dbg_init, vha, 0x0157,
			    "Read FCODE %d.%d.\n",
			    ha->fcode_revision[1], ha->fcode_revision[0]);
			break;
		case ROM_CODE_TYPE_EFI:
			/* Extensible Firmware Interface (EFI). */
			ha->efi_revision[0] = bcode[0x12];
			ha->efi_revision[1] = bcode[0x13];
			ql_dbg(ql_dbg_init, vha, 0x0158,
			    "Read EFI %d.%d.\n",
			    ha->efi_revision[1], ha->efi_revision[0]);
			break;
		default:
			ql_log(ql_log_warn, vha, 0x0159,
			    "Unrecognized code type %x at pcids %x.\n",
			    code_type, pcids);
			break;
		}

		last_image = bcode[0x15] & BIT_7;

		/* Locate next PCI expansion ROM. */
		pcihdr += ((bcode[0x11] << 8) | bcode[0x10]) * 512;
	} while (!last_image);

	/* Read firmware image information. */
	memset(ha->fw_revision, 0, sizeof(ha->fw_revision));
	dcode = mbuf;
	ha->isp_ops->read_optrom(vha, dcode, ha->flt_region_fw << 2, 0x20);
	bcode = mbuf + (pcihdr % 4);

	/* Validate signature of PCI data structure. */
	if (bcode[0x0] == 0x3 && bcode[0x1] == 0x0 &&
	    bcode[0x2] == 0x40 && bcode[0x3] == 0x40) {
		ha->fw_revision[0] = bcode[0x4];
		ha->fw_revision[1] = bcode[0x5];
		ha->fw_revision[2] = bcode[0x6];
		ql_dbg(ql_dbg_init, vha, 0x0153,
		    "Firmware revision %d.%d.%d\n",
		    ha->fw_revision[0], ha->fw_revision[1],
		    ha->fw_revision[2]);
	}

	return ret;
}

int
qla24xx_get_flash_version(scsi_qla_host_t *vha, void *mbuf)
{
	int ret = QLA_SUCCESS;
	uint32_t pcihdr = 0, pcids = 0;
	uint32_t *dcode = mbuf;
	uint8_t *bcode = mbuf;
	uint8_t code_type, last_image;
	int i;
	struct qla_hw_data *ha = vha->hw;
	uint32_t faddr = 0;
	struct active_regions active_regions = { };

	if (IS_P3P_TYPE(ha))
		return ret;

	if (!mbuf)
		return QLA_FUNCTION_FAILED;

	memset(ha->bios_revision, 0, sizeof(ha->bios_revision));
	memset(ha->efi_revision, 0, sizeof(ha->efi_revision));
	memset(ha->fcode_revision, 0, sizeof(ha->fcode_revision));
	memset(ha->fw_revision, 0, sizeof(ha->fw_revision));

	pcihdr = ha->flt_region_boot << 2;
	if (IS_QLA27XX(ha) || IS_QLA28XX(ha)) {
		qla27xx_get_active_image(vha, &active_regions);
		if (active_regions.global == QLA27XX_SECONDARY_IMAGE) {
			pcihdr = ha->flt_region_boot_sec << 2;
		}
	}

	do {
		/* Verify PCI expansion ROM header. */
		qla24xx_read_flash_data(vha, dcode, pcihdr >> 2, 0x20);
		bcode = mbuf + (pcihdr % 4);
		if (memcmp(bcode, "\x55\xaa", 2)) {
			/* No signature */
			ql_log(ql_log_fatal, vha, 0x0059,
			    "No matching ROM signature.\n");
			ret = QLA_FUNCTION_FAILED;
			break;
		}

		/* Locate PCI data structure. */
		pcids = pcihdr + ((bcode[0x19] << 8) | bcode[0x18]);

		qla24xx_read_flash_data(vha, dcode, pcids >> 2, 0x20);
		bcode = mbuf + (pcihdr % 4);

		/* Validate signature of PCI data structure. */
		if (memcmp(bcode, "PCIR", 4)) {
			/* Incorrect header. */
			ql_log(ql_log_fatal, vha, 0x005a,
			    "PCI data struct not found pcir_adr=%x.\n", pcids);
			ql_dump_buffer(ql_dbg_init, vha, 0x0059, dcode, 32);
			ret = QLA_FUNCTION_FAILED;
			break;
		}

		/* Read version */
		code_type = bcode[0x14];
		switch (code_type) {
		case ROM_CODE_TYPE_BIOS:
			/* Intel x86, PC-AT compatible. */
			ha->bios_revision[0] = bcode[0x12];
			ha->bios_revision[1] = bcode[0x13];
			ql_dbg(ql_dbg_init, vha, 0x005b,
			    "Read BIOS %d.%d.\n",
			    ha->bios_revision[1], ha->bios_revision[0]);
			break;
		case ROM_CODE_TYPE_FCODE:
			/* Open Firmware standard for PCI (FCode). */
			ha->fcode_revision[0] = bcode[0x12];
			ha->fcode_revision[1] = bcode[0x13];
			ql_dbg(ql_dbg_init, vha, 0x005c,
			    "Read FCODE %d.%d.\n",
			    ha->fcode_revision[1], ha->fcode_revision[0]);
			break;
		case ROM_CODE_TYPE_EFI:
			/* Extensible Firmware Interface (EFI). */
			ha->efi_revision[0] = bcode[0x12];
			ha->efi_revision[1] = bcode[0x13];
			ql_dbg(ql_dbg_init, vha, 0x005d,
			    "Read EFI %d.%d.\n",
			    ha->efi_revision[1], ha->efi_revision[0]);
			break;
		default:
			ql_log(ql_log_warn, vha, 0x005e,
			    "Unrecognized code type %x at pcids %x.\n",
			    code_type, pcids);
			break;
		}

		last_image = bcode[0x15] & BIT_7;

		/* Locate next PCI expansion ROM. */
		pcihdr += ((bcode[0x11] << 8) | bcode[0x10]) * 512;
	} while (!last_image);

	/* Read firmware image information. */
	memset(ha->fw_revision, 0, sizeof(ha->fw_revision));
	faddr = ha->flt_region_fw;
	if (IS_QLA27XX(ha) || IS_QLA28XX(ha)) {
		qla27xx_get_active_image(vha, &active_regions);
		if (active_regions.global == QLA27XX_SECONDARY_IMAGE)
			faddr = ha->flt_region_fw_sec;
	}

	qla24xx_read_flash_data(vha, dcode, faddr, 8);
	if (qla24xx_risc_firmware_invalid(dcode)) {
		ql_log(ql_log_warn, vha, 0x005f,
		    "Unrecognized fw revision at %x.\n",
		    ha->flt_region_fw * 4);
		ql_dump_buffer(ql_dbg_init, vha, 0x005f, dcode, 32);
	} else {
		for (i = 0; i < 4; i++)
			ha->fw_revision[i] =
				be32_to_cpu((__force __be32)dcode[4+i]);
		ql_dbg(ql_dbg_init, vha, 0x0060,
		    "Firmware revision (flash) %u.%u.%u (%x).\n",
		    ha->fw_revision[0], ha->fw_revision[1],
		    ha->fw_revision[2], ha->fw_revision[3]);
	}

	/* Check for golden firmware and get version if available */
	if (!IS_QLA81XX(ha)) {
		/* Golden firmware is not present in non 81XX adapters */
		return ret;
	}

	memset(ha->gold_fw_version, 0, sizeof(ha->gold_fw_version));
	faddr = ha->flt_region_gold_fw;
	qla24xx_read_flash_data(vha, dcode, ha->flt_region_gold_fw, 8);
	if (qla24xx_risc_firmware_invalid(dcode)) {
		ql_log(ql_log_warn, vha, 0x0056,
		    "Unrecognized golden fw at %#x.\n", faddr);
		ql_dump_buffer(ql_dbg_init, vha, 0x0056, dcode, 32);
		return ret;
	}

	for (i = 0; i < 4; i++)
		ha->gold_fw_version[i] =
			be32_to_cpu((__force __be32)dcode[4+i]);

	return ret;
}

static int
qla2xxx_is_vpd_valid(uint8_t *pos, uint8_t *end)
{
	if (pos >= end || *pos != 0x82)
		return 0;

	pos += 3 + pos[1];
	if (pos >= end || *pos != 0x90)
		return 0;

	pos += 3 + pos[1];
	if (pos >= end || *pos != 0x78)
		return 0;

	return 1;
}

int
qla2xxx_get_vpd_field(scsi_qla_host_t *vha, char *key, char *str, size_t size)
{
	struct qla_hw_data *ha = vha->hw;
	uint8_t *pos = ha->vpd;
	uint8_t *end = pos + ha->vpd_size;
	int len = 0;

	if (!IS_FWI2_CAPABLE(ha) || !qla2xxx_is_vpd_valid(pos, end))
		return 0;

	while (pos < end && *pos != 0x78) {
		len = (*pos == 0x82) ? pos[1] : pos[2];

		if (!strncmp(pos, key, strlen(key)))
			break;

		if (*pos != 0x90 && *pos != 0x91)
			pos += len;

		pos += 3;
	}

	if (pos < end - len && *pos != 0x78)
		return scnprintf(str, size, "%.*s", len, pos + 3);

	return 0;
}

int
qla24xx_read_fcp_prio_cfg(scsi_qla_host_t *vha)
{
	int len, max_len;
	uint32_t fcp_prio_addr;
	struct qla_hw_data *ha = vha->hw;

	if (!ha->fcp_prio_cfg) {
		ha->fcp_prio_cfg = vmalloc(FCP_PRIO_CFG_SIZE);
		if (!ha->fcp_prio_cfg) {
			ql_log(ql_log_warn, vha, 0x00d5,
			    "Unable to allocate memory for fcp priority data (%x).\n",
			    FCP_PRIO_CFG_SIZE);
			return QLA_FUNCTION_FAILED;
		}
	}
	memset(ha->fcp_prio_cfg, 0, FCP_PRIO_CFG_SIZE);

	fcp_prio_addr = ha->flt_region_fcp_prio;

	/* first read the fcp priority data header from flash */
	ha->isp_ops->read_optrom(vha, ha->fcp_prio_cfg,
			fcp_prio_addr << 2, FCP_PRIO_CFG_HDR_SIZE);

	if (!qla24xx_fcp_prio_cfg_valid(vha, ha->fcp_prio_cfg, 0))
		goto fail;

	/* read remaining FCP CMD config data from flash */
	fcp_prio_addr += (FCP_PRIO_CFG_HDR_SIZE >> 2);
	len = ha->fcp_prio_cfg->num_entries * sizeof(struct qla_fcp_prio_entry);
	max_len = FCP_PRIO_CFG_SIZE - FCP_PRIO_CFG_HDR_SIZE;

	ha->isp_ops->read_optrom(vha, &ha->fcp_prio_cfg->entry[0],
			fcp_prio_addr << 2, (len < max_len ? len : max_len));

	/* revalidate the entire FCP priority config data, including entries */
	if (!qla24xx_fcp_prio_cfg_valid(vha, ha->fcp_prio_cfg, 1))
		goto fail;

	ha->flags.fcp_prio_enabled = 1;
	return QLA_SUCCESS;
fail:
	vfree(ha->fcp_prio_cfg);
	ha->fcp_prio_cfg = NULL;
	return QLA_FUNCTION_FAILED;
}
