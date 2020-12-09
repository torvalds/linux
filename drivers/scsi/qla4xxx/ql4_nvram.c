// SPDX-License-Identifier: GPL-2.0-only
/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2013 QLogic Corporation
 */

#include "ql4_def.h"
#include "ql4_glbl.h"
#include "ql4_dbg.h"
#include "ql4_inline.h"

static inline void eeprom_cmd(uint32_t cmd, struct scsi_qla_host *ha)
{
	writel(cmd, isp_nvram(ha));
	readl(isp_nvram(ha));
	udelay(1);
}

static inline int eeprom_size(struct scsi_qla_host *ha)
{
	return is_qla4010(ha) ? FM93C66A_SIZE_16 : FM93C86A_SIZE_16;
}

static inline int eeprom_no_addr_bits(struct scsi_qla_host *ha)
{
	return is_qla4010(ha) ? FM93C56A_NO_ADDR_BITS_16 :
		FM93C86A_NO_ADDR_BITS_16 ;
}

static inline int eeprom_no_data_bits(struct scsi_qla_host *ha)
{
	return FM93C56A_DATA_BITS_16;
}

static int fm93c56a_select(struct scsi_qla_host * ha)
{
	DEBUG5(printk(KERN_ERR "fm93c56a_select:\n"));

	ha->eeprom_cmd_data = AUBURN_EEPROM_CS_1 | 0x000f0000;
	eeprom_cmd(ha->eeprom_cmd_data, ha);
	return 1;
}

static int fm93c56a_cmd(struct scsi_qla_host * ha, int cmd, int addr)
{
	int i;
	int mask;
	int dataBit;
	int previousBit;

	/* Clock in a zero, then do the start bit. */
	eeprom_cmd(ha->eeprom_cmd_data | AUBURN_EEPROM_DO_1, ha);

	eeprom_cmd(ha->eeprom_cmd_data | AUBURN_EEPROM_DO_1 |
	       AUBURN_EEPROM_CLK_RISE, ha);
	eeprom_cmd(ha->eeprom_cmd_data | AUBURN_EEPROM_DO_1 |
	       AUBURN_EEPROM_CLK_FALL, ha);

	mask = 1 << (FM93C56A_CMD_BITS - 1);

	/* Force the previous data bit to be different. */
	previousBit = 0xffff;
	for (i = 0; i < FM93C56A_CMD_BITS; i++) {
		dataBit =
			(cmd & mask) ? AUBURN_EEPROM_DO_1 : AUBURN_EEPROM_DO_0;
		if (previousBit != dataBit) {

			/*
			 * If the bit changed, then change the DO state to
			 * match.
			 */
			eeprom_cmd(ha->eeprom_cmd_data | dataBit, ha);
			previousBit = dataBit;
		}
		eeprom_cmd(ha->eeprom_cmd_data | dataBit |
		       AUBURN_EEPROM_CLK_RISE, ha);
		eeprom_cmd(ha->eeprom_cmd_data | dataBit |
		       AUBURN_EEPROM_CLK_FALL, ha);

		cmd = cmd << 1;
	}
	mask = 1 << (eeprom_no_addr_bits(ha) - 1);

	/* Force the previous data bit to be different. */
	previousBit = 0xffff;
	for (i = 0; i < eeprom_no_addr_bits(ha); i++) {
		dataBit = addr & mask ? AUBURN_EEPROM_DO_1 :
			AUBURN_EEPROM_DO_0;
		if (previousBit != dataBit) {
			/*
			 * If the bit changed, then change the DO state to
			 * match.
			 */
			eeprom_cmd(ha->eeprom_cmd_data | dataBit, ha);

			previousBit = dataBit;
		}
		eeprom_cmd(ha->eeprom_cmd_data | dataBit |
		       AUBURN_EEPROM_CLK_RISE, ha);
		eeprom_cmd(ha->eeprom_cmd_data | dataBit |
		       AUBURN_EEPROM_CLK_FALL, ha);

		addr = addr << 1;
	}
	return 1;
}

static int fm93c56a_deselect(struct scsi_qla_host * ha)
{
	ha->eeprom_cmd_data = AUBURN_EEPROM_CS_0 | 0x000f0000;
	eeprom_cmd(ha->eeprom_cmd_data, ha);
	return 1;
}

static int fm93c56a_datain(struct scsi_qla_host * ha, unsigned short *value)
{
	int i;
	int data = 0;
	int dataBit;

	/* Read the data bits
	 * The first bit is a dummy.  Clock right over it. */
	for (i = 0; i < eeprom_no_data_bits(ha); i++) {
		eeprom_cmd(ha->eeprom_cmd_data |
		       AUBURN_EEPROM_CLK_RISE, ha);
		eeprom_cmd(ha->eeprom_cmd_data |
		       AUBURN_EEPROM_CLK_FALL, ha);

		dataBit = (readw(isp_nvram(ha)) & AUBURN_EEPROM_DI_1) ? 1 : 0;

		data = (data << 1) | dataBit;
	}

	*value = data;
	return 1;
}

static int eeprom_readword(int eepromAddr, u16 * value,
			   struct scsi_qla_host * ha)
{
	fm93c56a_select(ha);
	fm93c56a_cmd(ha, FM93C56A_READ, eepromAddr);
	fm93c56a_datain(ha, value);
	fm93c56a_deselect(ha);
	return 1;
}

/* Hardware_lock must be set before calling */
u16 rd_nvram_word(struct scsi_qla_host * ha, int offset)
{
	u16 val = 0;

	/* NOTE: NVRAM uses half-word addresses */
	eeprom_readword(offset, &val, ha);
	return val;
}

u8 rd_nvram_byte(struct scsi_qla_host *ha, int offset)
{
	u16 val = 0;
	u8 rval = 0;
	int index = 0;

	if (offset & 0x1)
		index = (offset - 1) / 2;
	else
		index = offset / 2;

	val = le16_to_cpu(rd_nvram_word(ha, index));

	if (offset & 0x1)
		rval = (u8)((val & 0xff00) >> 8);
	else
		rval = (u8)((val & 0x00ff));

	return rval;
}

int qla4xxx_is_nvram_configuration_valid(struct scsi_qla_host * ha)
{
	int status = QLA_ERROR;
	uint16_t checksum = 0;
	uint32_t index;
	unsigned long flags;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (index = 0; index < eeprom_size(ha); index++)
		checksum += rd_nvram_word(ha, index);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (checksum == 0)
		status = QLA_SUCCESS;

	return status;
}

/*************************************************************************
 *
 *			Hardware Semaphore routines
 *
 *************************************************************************/
int ql4xxx_sem_spinlock(struct scsi_qla_host * ha, u32 sem_mask, u32 sem_bits)
{
	uint32_t value;
	unsigned long flags;
	unsigned int seconds = 30;

	DEBUG2(printk("scsi%ld : Trying to get SEM lock - mask= 0x%x, code = "
		      "0x%x\n", ha->host_no, sem_mask, sem_bits));
	do {
		spin_lock_irqsave(&ha->hardware_lock, flags);
		writel((sem_mask | sem_bits), isp_semaphore(ha));
		value = readw(isp_semaphore(ha));
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		if ((value & (sem_mask >> 16)) == sem_bits) {
			DEBUG2(printk("scsi%ld : Got SEM LOCK - mask= 0x%x, "
				      "code = 0x%x\n", ha->host_no,
				      sem_mask, sem_bits));
			return QLA_SUCCESS;
		}
		ssleep(1);
	} while (--seconds);
	return QLA_ERROR;
}

void ql4xxx_sem_unlock(struct scsi_qla_host * ha, u32 sem_mask)
{
	unsigned long flags;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	writel(sem_mask, isp_semaphore(ha));
	readl(isp_semaphore(ha));
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	DEBUG2(printk("scsi%ld : UNLOCK SEM - mask= 0x%x\n", ha->host_no,
		      sem_mask));
}

int ql4xxx_sem_lock(struct scsi_qla_host * ha, u32 sem_mask, u32 sem_bits)
{
	uint32_t value;
	unsigned long flags;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	writel((sem_mask | sem_bits), isp_semaphore(ha));
	value = readw(isp_semaphore(ha));
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	if ((value & (sem_mask >> 16)) == sem_bits) {
		DEBUG2(printk("scsi%ld : Got SEM LOCK - mask= 0x%x, code = "
			      "0x%x, sema code=0x%x\n", ha->host_no,
			      sem_mask, sem_bits, value));
		return 1;
	}
	return 0;
}
