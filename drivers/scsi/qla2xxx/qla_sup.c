/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"

#include <linux/delay.h>
#include <asm/uaccess.h>

static uint16_t qla2x00_nvram_request(scsi_qla_host_t *, uint32_t);
static void qla2x00_nv_deselect(scsi_qla_host_t *);
static void qla2x00_nv_write(scsi_qla_host_t *, uint16_t);

/*
 * NVRAM support routines
 */

/**
 * qla2x00_lock_nvram_access() -
 * @ha: HA context
 */
void
qla2x00_lock_nvram_access(scsi_qla_host_t *ha)
{
	uint16_t data;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	if (!IS_QLA2100(ha) && !IS_QLA2200(ha) && !IS_QLA2300(ha)) {
		data = RD_REG_WORD(&reg->nvram);
		while (data & NVR_BUSY) {
			udelay(100);
			data = RD_REG_WORD(&reg->nvram);
		}

		/* Lock resource */
		WRT_REG_WORD(&reg->u.isp2300.host_semaphore, 0x1);
		RD_REG_WORD(&reg->u.isp2300.host_semaphore);
		udelay(5);
		data = RD_REG_WORD(&reg->u.isp2300.host_semaphore);
		while ((data & BIT_0) == 0) {
			/* Lock failed */
			udelay(100);
			WRT_REG_WORD(&reg->u.isp2300.host_semaphore, 0x1);
			RD_REG_WORD(&reg->u.isp2300.host_semaphore);
			udelay(5);
			data = RD_REG_WORD(&reg->u.isp2300.host_semaphore);
		}
	}
}

/**
 * qla2x00_unlock_nvram_access() -
 * @ha: HA context
 */
void
qla2x00_unlock_nvram_access(scsi_qla_host_t *ha)
{
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	if (!IS_QLA2100(ha) && !IS_QLA2200(ha) && !IS_QLA2300(ha)) {
		WRT_REG_WORD(&reg->u.isp2300.host_semaphore, 0);
		RD_REG_WORD(&reg->u.isp2300.host_semaphore);
	}
}

/**
 * qla2x00_get_nvram_word() - Calculates word position in NVRAM and calls the
 *	request routine to get the word from NVRAM.
 * @ha: HA context
 * @addr: Address in NVRAM to read
 *
 * Returns the word read from nvram @addr.
 */
uint16_t
qla2x00_get_nvram_word(scsi_qla_host_t *ha, uint32_t addr)
{
	uint16_t	data;
	uint32_t	nv_cmd;

	nv_cmd = addr << 16;
	nv_cmd |= NV_READ_OP;
	data = qla2x00_nvram_request(ha, nv_cmd);

	return (data);
}

/**
 * qla2x00_write_nvram_word() - Write NVRAM data.
 * @ha: HA context
 * @addr: Address in NVRAM to write
 * @data: word to program
 */
void
qla2x00_write_nvram_word(scsi_qla_host_t *ha, uint32_t addr, uint16_t data)
{
	int count;
	uint16_t word;
	uint32_t nv_cmd;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	qla2x00_nv_write(ha, NVR_DATA_OUT);
	qla2x00_nv_write(ha, 0);
	qla2x00_nv_write(ha, 0);

	for (word = 0; word < 8; word++)
		qla2x00_nv_write(ha, NVR_DATA_OUT);

	qla2x00_nv_deselect(ha);

	/* Write data */
	nv_cmd = (addr << 16) | NV_WRITE_OP;
	nv_cmd |= data;
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
	WRT_REG_WORD(&reg->nvram, NVR_SELECT);
	do {
		NVRAM_DELAY();
		word = RD_REG_WORD(&reg->nvram);
	} while ((word & NVR_DATA_IN) == 0);

	qla2x00_nv_deselect(ha);

	/* Disable writes */
	qla2x00_nv_write(ha, NVR_DATA_OUT);
	for (count = 0; count < 10; count++)
		qla2x00_nv_write(ha, 0);

	qla2x00_nv_deselect(ha);
}

static int
qla2x00_write_nvram_word_tmo(scsi_qla_host_t *ha, uint32_t addr, uint16_t data,
    uint32_t tmo)
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
	nv_cmd |= data;
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
	WRT_REG_WORD(&reg->nvram, NVR_SELECT);
	do {
		NVRAM_DELAY();
		word = RD_REG_WORD(&reg->nvram);
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
qla2x00_nvram_request(scsi_qla_host_t *ha, uint32_t nv_cmd)
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
		WRT_REG_WORD(&reg->nvram, NVR_SELECT | NVR_CLOCK);
		NVRAM_DELAY();
		data <<= 1;
		reg_data = RD_REG_WORD(&reg->nvram);
		if (reg_data & NVR_DATA_IN)
			data |= BIT_0;
		WRT_REG_WORD(&reg->nvram, NVR_SELECT);
		RD_REG_WORD(&reg->nvram);	/* PCI Posting. */
		NVRAM_DELAY();
	}

	/* Deselect chip. */
	WRT_REG_WORD(&reg->nvram, NVR_DESELECT);
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
	NVRAM_DELAY();

	return (data);
}

/**
 * qla2x00_nv_write() - Clean NVRAM operations.
 * @ha: HA context
 */
static void
qla2x00_nv_deselect(scsi_qla_host_t *ha)
{
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	WRT_REG_WORD(&reg->nvram, NVR_DESELECT);
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
	NVRAM_DELAY();
}

/**
 * qla2x00_nv_write() - Prepare for NVRAM read/write operation.
 * @ha: HA context
 * @data: Serial interface selector
 */
static void
qla2x00_nv_write(scsi_qla_host_t *ha, uint16_t data)
{
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	WRT_REG_WORD(&reg->nvram, data | NVR_SELECT | NVR_WRT_ENABLE);
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
	NVRAM_DELAY();
	WRT_REG_WORD(&reg->nvram, data | NVR_SELECT| NVR_CLOCK |
	    NVR_WRT_ENABLE);
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
	NVRAM_DELAY();
	WRT_REG_WORD(&reg->nvram, data | NVR_SELECT | NVR_WRT_ENABLE);
	RD_REG_WORD(&reg->nvram);		/* PCI Posting. */
	NVRAM_DELAY();
}

/**
 * qla2x00_clear_nvram_protection() -
 * @ha: HA context
 */
static int
qla2x00_clear_nvram_protection(scsi_qla_host_t *ha)
{
	int ret, stat;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	uint32_t word;
	uint16_t wprot, wprot_old;

	/* Clear NVRAM write protection. */
	ret = QLA_FUNCTION_FAILED;
	wprot_old = cpu_to_le16(qla2x00_get_nvram_word(ha, 0));
	stat = qla2x00_write_nvram_word_tmo(ha, 0,
	    __constant_cpu_to_le16(0x1234), 100000);
	wprot = cpu_to_le16(qla2x00_get_nvram_word(ha, 0));
	if (stat != QLA_SUCCESS || wprot != __constant_cpu_to_le16(0x1234)) {
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
		WRT_REG_WORD(&reg->nvram, NVR_SELECT);
		do {
			NVRAM_DELAY();
			word = RD_REG_WORD(&reg->nvram);
		} while ((word & NVR_DATA_IN) == 0);

		ret = QLA_SUCCESS;
	} else
		qla2x00_write_nvram_word(ha, 0, wprot_old);

	return ret;
}

static void
qla2x00_set_nvram_protection(scsi_qla_host_t *ha, int stat)
{
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	uint32_t word;

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
	WRT_REG_WORD(&reg->nvram, NVR_SELECT);
	do {
		NVRAM_DELAY();
		word = RD_REG_WORD(&reg->nvram);
	} while ((word & NVR_DATA_IN) == 0);
}


/*****************************************************************************/
/* Flash Manipulation Routines                                               */
/*****************************************************************************/

static inline uint32_t
flash_conf_to_access_addr(uint32_t faddr)
{
	return FARX_ACCESS_FLASH_CONF | faddr;
}

static inline uint32_t
flash_data_to_access_addr(uint32_t faddr)
{
	return FARX_ACCESS_FLASH_DATA | faddr;
}

static inline uint32_t
nvram_conf_to_access_addr(uint32_t naddr)
{
	return FARX_ACCESS_NVRAM_CONF | naddr;
}

static inline uint32_t
nvram_data_to_access_addr(uint32_t naddr)
{
	return FARX_ACCESS_NVRAM_DATA | naddr;
}

uint32_t
qla24xx_read_flash_dword(scsi_qla_host_t *ha, uint32_t addr)
{
	int rval;
	uint32_t cnt, data;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	WRT_REG_DWORD(&reg->flash_addr, addr & ~FARX_DATA_FLAG);
	/* Wait for READ cycle to complete. */
	rval = QLA_SUCCESS;
	for (cnt = 3000;
	    (RD_REG_DWORD(&reg->flash_addr) & FARX_DATA_FLAG) == 0 &&
	    rval == QLA_SUCCESS; cnt--) {
		if (cnt)
			udelay(10);
		else
			rval = QLA_FUNCTION_TIMEOUT;
	}

	/* TODO: What happens if we time out? */
	data = 0xDEADDEAD;
	if (rval == QLA_SUCCESS)
		data = RD_REG_DWORD(&reg->flash_data);

	return data;
}

uint32_t *
qla24xx_read_flash_data(scsi_qla_host_t *ha, uint32_t *dwptr, uint32_t faddr,
    uint32_t dwords)
{
	uint32_t i;

	/* Dword reads to flash. */
	for (i = 0; i < dwords; i++, faddr++)
		dwptr[i] = cpu_to_le32(qla24xx_read_flash_dword(ha,
		    flash_data_to_access_addr(faddr)));

	return dwptr;
}

int
qla24xx_write_flash_dword(scsi_qla_host_t *ha, uint32_t addr, uint32_t data)
{
	int rval;
	uint32_t cnt;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	WRT_REG_DWORD(&reg->flash_data, data);
	RD_REG_DWORD(&reg->flash_data);		/* PCI Posting. */
	WRT_REG_DWORD(&reg->flash_addr, addr | FARX_DATA_FLAG);
	/* Wait for Write cycle to complete. */
	rval = QLA_SUCCESS;
	for (cnt = 500000; (RD_REG_DWORD(&reg->flash_addr) & FARX_DATA_FLAG) &&
	    rval == QLA_SUCCESS; cnt--) {
		if (cnt)
			udelay(10);
		else
			rval = QLA_FUNCTION_TIMEOUT;
	}
	return rval;
}

void
qla24xx_get_flash_manufacturer(scsi_qla_host_t *ha, uint8_t *man_id,
    uint8_t *flash_id)
{
	uint32_t ids;

	ids = qla24xx_read_flash_dword(ha, flash_data_to_access_addr(0xd03ab));
	*man_id = LSB(ids);
	*flash_id = MSB(ids);
}

int
qla24xx_write_flash_data(scsi_qla_host_t *ha, uint32_t *dwptr, uint32_t faddr,
    uint32_t dwords)
{
	int ret;
	uint32_t liter;
	uint32_t sec_mask, rest_addr, conf_addr;
	uint32_t fdata;
	uint8_t	man_id, flash_id;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	ret = QLA_SUCCESS;

	qla24xx_get_flash_manufacturer(ha, &man_id, &flash_id);
	DEBUG9(printk("%s(%ld): Flash man_id=%d flash_id=%d\n", __func__,
	    ha->host_no, man_id, flash_id));

	conf_addr = flash_conf_to_access_addr(0x03d8);
	switch (man_id) {
	case 0xbf: /* STT flash. */
		rest_addr = 0x1fff;
		sec_mask = 0x3e000;
		if (flash_id == 0x80)
			conf_addr = flash_conf_to_access_addr(0x0352);
		break;
	case 0x13: /* ST M25P80. */
		rest_addr = 0x3fff;
		sec_mask = 0x3c000;
		break;
	default:
		/* Default to 64 kb sector size. */
		rest_addr = 0x3fff;
		sec_mask = 0x3c000;
		break;
	}

	/* Enable flash write. */
	WRT_REG_DWORD(&reg->ctrl_status,
	    RD_REG_DWORD(&reg->ctrl_status) | CSRX_FLASH_ENABLE);
	RD_REG_DWORD(&reg->ctrl_status);	/* PCI Posting. */

	/* Disable flash write-protection. */
	qla24xx_write_flash_dword(ha, flash_conf_to_access_addr(0x101), 0);

	do {    /* Loop once to provide quick error exit. */
		for (liter = 0; liter < dwords; liter++, faddr++, dwptr++) {
			/* Are we at the beginning of a sector? */
			if ((faddr & rest_addr) == 0) {
				fdata = (faddr & sec_mask) << 2;
				ret = qla24xx_write_flash_dword(ha, conf_addr,
				    (fdata & 0xff00) |((fdata << 16) &
				    0xff0000) | ((fdata >> 16) & 0xff));
				if (ret != QLA_SUCCESS) {
					DEBUG9(printk("%s(%ld) Unable to flash "
					    "sector: address=%x.\n", __func__,
					    ha->host_no, faddr));
					break;
				}
			}
			ret = qla24xx_write_flash_dword(ha,
			    flash_data_to_access_addr(faddr),
			    cpu_to_le32(*dwptr));
			if (ret != QLA_SUCCESS) {
				DEBUG9(printk("%s(%ld) Unable to program flash "
				    "address=%x data=%x.\n", __func__,
				    ha->host_no, faddr, *dwptr));
				break;
			}
		}
	} while (0);

	/* Disable flash write. */
	WRT_REG_DWORD(&reg->ctrl_status,
	    RD_REG_DWORD(&reg->ctrl_status) & ~CSRX_FLASH_ENABLE);
	RD_REG_DWORD(&reg->ctrl_status);	/* PCI Posting. */

	return ret;
}

uint8_t *
qla2x00_read_nvram_data(scsi_qla_host_t *ha, uint8_t *buf, uint32_t naddr,
    uint32_t bytes)
{
	uint32_t i;
	uint16_t *wptr;

	/* Word reads to NVRAM via registers. */
	wptr = (uint16_t *)buf;
	qla2x00_lock_nvram_access(ha);
	for (i = 0; i < bytes >> 1; i++, naddr++)
		wptr[i] = cpu_to_le16(qla2x00_get_nvram_word(ha,
		    naddr));
	qla2x00_unlock_nvram_access(ha);

	return buf;
}

uint8_t *
qla24xx_read_nvram_data(scsi_qla_host_t *ha, uint8_t *buf, uint32_t naddr,
    uint32_t bytes)
{
	uint32_t i;
	uint32_t *dwptr;

	/* Dword reads to flash. */
	dwptr = (uint32_t *)buf;
	for (i = 0; i < bytes >> 2; i++, naddr++)
		dwptr[i] = cpu_to_le32(qla24xx_read_flash_dword(ha,
		    nvram_data_to_access_addr(naddr)));

	return buf;
}

int
qla2x00_write_nvram_data(scsi_qla_host_t *ha, uint8_t *buf, uint32_t naddr,
    uint32_t bytes)
{
	int ret, stat;
	uint32_t i;
	uint16_t *wptr;

	ret = QLA_SUCCESS;

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

	return ret;
}

int
qla24xx_write_nvram_data(scsi_qla_host_t *ha, uint8_t *buf, uint32_t naddr,
    uint32_t bytes)
{
	int ret;
	uint32_t i;
	uint32_t *dwptr;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	ret = QLA_SUCCESS;

	/* Enable flash write. */
	WRT_REG_DWORD(&reg->ctrl_status,
	    RD_REG_DWORD(&reg->ctrl_status) | CSRX_FLASH_ENABLE);
	RD_REG_DWORD(&reg->ctrl_status);	/* PCI Posting. */

	/* Disable NVRAM write-protection. */
	qla24xx_write_flash_dword(ha, nvram_conf_to_access_addr(0x101),
	    0);
	qla24xx_write_flash_dword(ha, nvram_conf_to_access_addr(0x101),
	    0);

	/* Dword writes to flash. */
	dwptr = (uint32_t *)buf;
	for (i = 0; i < bytes >> 2; i++, naddr++, dwptr++) {
		ret = qla24xx_write_flash_dword(ha,
		    nvram_data_to_access_addr(naddr),
		    cpu_to_le32(*dwptr));
		if (ret != QLA_SUCCESS) {
			DEBUG9(printk("%s(%ld) Unable to program "
			    "nvram address=%x data=%x.\n", __func__,
			    ha->host_no, naddr, *dwptr));
			break;
		}
	}

	/* Enable NVRAM write-protection. */
	qla24xx_write_flash_dword(ha, nvram_conf_to_access_addr(0x101),
	    0x8c);

	/* Disable flash write. */
	WRT_REG_DWORD(&reg->ctrl_status,
	    RD_REG_DWORD(&reg->ctrl_status) & ~CSRX_FLASH_ENABLE);
	RD_REG_DWORD(&reg->ctrl_status);	/* PCI Posting. */

	return ret;
}
