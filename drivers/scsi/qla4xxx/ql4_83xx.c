/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)   2003-2013 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

#include <linux/ratelimit.h>

#include "ql4_def.h"
#include "ql4_version.h"
#include "ql4_glbl.h"
#include "ql4_dbg.h"
#include "ql4_inline.h"

uint32_t qla4_83xx_rd_reg(struct scsi_qla_host *ha, ulong addr)
{
	return readl((void __iomem *)(ha->nx_pcibase + addr));
}

void qla4_83xx_wr_reg(struct scsi_qla_host *ha, ulong addr, uint32_t val)
{
	writel(val, (void __iomem *)(ha->nx_pcibase + addr));
}

static int qla4_83xx_set_win_base(struct scsi_qla_host *ha, uint32_t addr)
{
	uint32_t val;
	int ret_val = QLA_SUCCESS;

	qla4_83xx_wr_reg(ha, QLA83XX_CRB_WIN_FUNC(ha->func_num), addr);
	val = qla4_83xx_rd_reg(ha, QLA83XX_CRB_WIN_FUNC(ha->func_num));
	if (val != addr) {
		ql4_printk(KERN_ERR, ha, "%s: Failed to set register window : addr written 0x%x, read 0x%x!\n",
			   __func__, addr, val);
		ret_val = QLA_ERROR;
	}

	return ret_val;
}

int qla4_83xx_rd_reg_indirect(struct scsi_qla_host *ha, uint32_t addr,
			      uint32_t *data)
{
	int ret_val;

	ret_val = qla4_83xx_set_win_base(ha, addr);

	if (ret_val == QLA_SUCCESS)
		*data = qla4_83xx_rd_reg(ha, QLA83XX_WILDCARD);
	else
		ql4_printk(KERN_ERR, ha, "%s: failed read of addr 0x%x!\n",
			   __func__, addr);

	return ret_val;
}

int qla4_83xx_wr_reg_indirect(struct scsi_qla_host *ha, uint32_t addr,
			      uint32_t data)
{
	int ret_val;

	ret_val = qla4_83xx_set_win_base(ha, addr);

	if (ret_val == QLA_SUCCESS)
		qla4_83xx_wr_reg(ha, QLA83XX_WILDCARD, data);
	else
		ql4_printk(KERN_ERR, ha, "%s: failed wrt to addr 0x%x, data 0x%x\n",
			   __func__, addr, data);

	return ret_val;
}

static int qla4_83xx_flash_lock(struct scsi_qla_host *ha)
{
	int lock_owner;
	int timeout = 0;
	uint32_t lock_status = 0;
	int ret_val = QLA_SUCCESS;

	while (lock_status == 0) {
		lock_status = qla4_83xx_rd_reg(ha, QLA83XX_FLASH_LOCK);
		if (lock_status)
			break;

		if (++timeout >= QLA83XX_FLASH_LOCK_TIMEOUT / 20) {
			lock_owner = qla4_83xx_rd_reg(ha,
						      QLA83XX_FLASH_LOCK_ID);
			ql4_printk(KERN_ERR, ha, "%s: flash lock by func %d failed, held by func %d\n",
				   __func__, ha->func_num, lock_owner);
			ret_val = QLA_ERROR;
			break;
		}
		msleep(20);
	}

	qla4_83xx_wr_reg(ha, QLA83XX_FLASH_LOCK_ID, ha->func_num);
	return ret_val;
}

static void qla4_83xx_flash_unlock(struct scsi_qla_host *ha)
{
	/* Reading FLASH_UNLOCK register unlocks the Flash */
	qla4_83xx_wr_reg(ha, QLA83XX_FLASH_LOCK_ID, 0xFF);
	qla4_83xx_rd_reg(ha, QLA83XX_FLASH_UNLOCK);
}

int qla4_83xx_flash_read_u32(struct scsi_qla_host *ha, uint32_t flash_addr,
			     uint8_t *p_data, int u32_word_count)
{
	int i;
	uint32_t u32_word;
	uint32_t addr = flash_addr;
	int ret_val = QLA_SUCCESS;

	ret_val = qla4_83xx_flash_lock(ha);
	if (ret_val == QLA_ERROR)
		goto exit_lock_error;

	if (addr & 0x03) {
		ql4_printk(KERN_ERR, ha, "%s: Illegal addr = 0x%x\n",
			   __func__, addr);
		ret_val = QLA_ERROR;
		goto exit_flash_read;
	}

	for (i = 0; i < u32_word_count; i++) {
		ret_val = qla4_83xx_wr_reg_indirect(ha,
						    QLA83XX_FLASH_DIRECT_WINDOW,
						    (addr & 0xFFFF0000));
		if (ret_val == QLA_ERROR) {
			ql4_printk(KERN_ERR, ha, "%s: failed to write addr 0x%x to FLASH_DIRECT_WINDOW\n!",
				   __func__, addr);
			goto exit_flash_read;
		}

		ret_val = qla4_83xx_rd_reg_indirect(ha,
						QLA83XX_FLASH_DIRECT_DATA(addr),
						&u32_word);
		if (ret_val == QLA_ERROR) {
			ql4_printk(KERN_ERR, ha, "%s: failed to read addr 0x%x!\n",
				   __func__, addr);
			goto exit_flash_read;
		}

		*(__le32 *)p_data = le32_to_cpu(u32_word);
		p_data = p_data + 4;
		addr = addr + 4;
	}

exit_flash_read:
	qla4_83xx_flash_unlock(ha);

exit_lock_error:
	return ret_val;
}

int qla4_83xx_lockless_flash_read_u32(struct scsi_qla_host *ha,
				      uint32_t flash_addr, uint8_t *p_data,
				      int u32_word_count)
{
	uint32_t i;
	uint32_t u32_word;
	uint32_t flash_offset;
	uint32_t addr = flash_addr;
	int ret_val = QLA_SUCCESS;

	flash_offset = addr & (QLA83XX_FLASH_SECTOR_SIZE - 1);

	if (addr & 0x3) {
		ql4_printk(KERN_ERR, ha, "%s: Illegal addr = 0x%x\n",
			   __func__, addr);
		ret_val = QLA_ERROR;
		goto exit_lockless_read;
	}

	ret_val = qla4_83xx_wr_reg_indirect(ha, QLA83XX_FLASH_DIRECT_WINDOW,
					    addr);
	if (ret_val == QLA_ERROR) {
		ql4_printk(KERN_ERR, ha, "%s: failed to write addr 0x%x to FLASH_DIRECT_WINDOW!\n",
			   __func__, addr);
		goto exit_lockless_read;
	}

	/* Check if data is spread across multiple sectors  */
	if ((flash_offset + (u32_word_count * sizeof(uint32_t))) >
	    (QLA83XX_FLASH_SECTOR_SIZE - 1)) {

		/* Multi sector read */
		for (i = 0; i < u32_word_count; i++) {
			ret_val = qla4_83xx_rd_reg_indirect(ha,
						QLA83XX_FLASH_DIRECT_DATA(addr),
						&u32_word);
			if (ret_val == QLA_ERROR) {
				ql4_printk(KERN_ERR, ha, "%s: failed to read addr 0x%x!\n",
					   __func__, addr);
				goto exit_lockless_read;
			}

			*(__le32 *)p_data  = le32_to_cpu(u32_word);
			p_data = p_data + 4;
			addr = addr + 4;
			flash_offset = flash_offset + 4;

			if (flash_offset > (QLA83XX_FLASH_SECTOR_SIZE - 1)) {
				/* This write is needed once for each sector */
				ret_val = qla4_83xx_wr_reg_indirect(ha,
						   QLA83XX_FLASH_DIRECT_WINDOW,
						   addr);
				if (ret_val == QLA_ERROR) {
					ql4_printk(KERN_ERR, ha, "%s: failed to write addr 0x%x to FLASH_DIRECT_WINDOW!\n",
						   __func__, addr);
					goto exit_lockless_read;
				}
				flash_offset = 0;
			}
		}
	} else {
		/* Single sector read */
		for (i = 0; i < u32_word_count; i++) {
			ret_val = qla4_83xx_rd_reg_indirect(ha,
						QLA83XX_FLASH_DIRECT_DATA(addr),
						&u32_word);
			if (ret_val == QLA_ERROR) {
				ql4_printk(KERN_ERR, ha, "%s: failed to read addr 0x%x!\n",
					   __func__, addr);
				goto exit_lockless_read;
			}

			*(__le32 *)p_data = le32_to_cpu(u32_word);
			p_data = p_data + 4;
			addr = addr + 4;
		}
	}

exit_lockless_read:
	return ret_val;
}

void qla4_83xx_rom_lock_recovery(struct scsi_qla_host *ha)
{
	if (qla4_83xx_flash_lock(ha))
		ql4_printk(KERN_INFO, ha, "%s: Resetting rom lock\n", __func__);

	/*
	 * We got the lock, or someone else is holding the lock
	 * since we are restting, forcefully unlock
	 */
	qla4_83xx_flash_unlock(ha);
}

/**
 * qla4_83xx_ms_mem_write_128b - Writes data to MS/off-chip memory
 * @ha: Pointer to adapter structure
 * @addr: Flash address to write to
 * @data: Data to be written
 * @count: word_count to be written
 *
 * Return: On success return QLA_SUCCESS
 *	   On error return QLA_ERROR
 **/
int qla4_83xx_ms_mem_write_128b(struct scsi_qla_host *ha, uint64_t addr,
				uint32_t *data, uint32_t count)
{
	int i, j;
	uint32_t agt_ctrl;
	unsigned long flags;
	int ret_val = QLA_SUCCESS;

	/* Only 128-bit aligned access */
	if (addr & 0xF) {
		ret_val = QLA_ERROR;
		goto exit_ms_mem_write;
	}

	write_lock_irqsave(&ha->hw_lock, flags);

	/* Write address */
	ret_val = qla4_83xx_wr_reg_indirect(ha, MD_MIU_TEST_AGT_ADDR_HI, 0);
	if (ret_val == QLA_ERROR) {
		ql4_printk(KERN_ERR, ha, "%s: write to AGT_ADDR_HI failed\n",
			   __func__);
		goto exit_ms_mem_write_unlock;
	}

	for (i = 0; i < count; i++, addr += 16) {
		if (!((QLA8XXX_ADDR_IN_RANGE(addr, QLA8XXX_ADDR_QDR_NET,
					     QLA8XXX_ADDR_QDR_NET_MAX)) ||
		      (QLA8XXX_ADDR_IN_RANGE(addr, QLA8XXX_ADDR_DDR_NET,
					     QLA8XXX_ADDR_DDR_NET_MAX)))) {
			ret_val = QLA_ERROR;
			goto exit_ms_mem_write_unlock;
		}

		ret_val = qla4_83xx_wr_reg_indirect(ha, MD_MIU_TEST_AGT_ADDR_LO,
						    addr);
		/* Write data */
		ret_val |= qla4_83xx_wr_reg_indirect(ha,
						     MD_MIU_TEST_AGT_WRDATA_LO,
						     *data++);
		ret_val |= qla4_83xx_wr_reg_indirect(ha,
						     MD_MIU_TEST_AGT_WRDATA_HI,
						     *data++);
		ret_val |= qla4_83xx_wr_reg_indirect(ha,
						     MD_MIU_TEST_AGT_WRDATA_ULO,
						     *data++);
		ret_val |= qla4_83xx_wr_reg_indirect(ha,
						     MD_MIU_TEST_AGT_WRDATA_UHI,
						     *data++);
		if (ret_val == QLA_ERROR) {
			ql4_printk(KERN_ERR, ha, "%s: write to AGT_WRDATA failed\n",
				   __func__);
			goto exit_ms_mem_write_unlock;
		}

		/* Check write status */
		ret_val = qla4_83xx_wr_reg_indirect(ha, MD_MIU_TEST_AGT_CTRL,
						    MIU_TA_CTL_WRITE_ENABLE);
		ret_val |= qla4_83xx_wr_reg_indirect(ha, MD_MIU_TEST_AGT_CTRL,
						     MIU_TA_CTL_WRITE_START);
		if (ret_val == QLA_ERROR) {
			ql4_printk(KERN_ERR, ha, "%s: write to AGT_CTRL failed\n",
				   __func__);
			goto exit_ms_mem_write_unlock;
		}

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			ret_val = qla4_83xx_rd_reg_indirect(ha,
							MD_MIU_TEST_AGT_CTRL,
							&agt_ctrl);
			if (ret_val == QLA_ERROR) {
				ql4_printk(KERN_ERR, ha, "%s: failed to read MD_MIU_TEST_AGT_CTRL\n",
					   __func__);
				goto exit_ms_mem_write_unlock;
			}
			if ((agt_ctrl & MIU_TA_CTL_BUSY) == 0)
				break;
		}

		/* Status check failed */
		if (j >= MAX_CTL_CHECK) {
			printk_ratelimited(KERN_ERR "%s: MS memory write failed!\n",
					   __func__);
			ret_val = QLA_ERROR;
			goto exit_ms_mem_write_unlock;
		}
	}

exit_ms_mem_write_unlock:
	write_unlock_irqrestore(&ha->hw_lock, flags);

exit_ms_mem_write:
	return ret_val;
}

#define INTENT_TO_RECOVER	0x01
#define PROCEED_TO_RECOVER	0x02

static int qla4_83xx_lock_recovery(struct scsi_qla_host *ha)
{

	uint32_t lock = 0, lockid;
	int ret_val = QLA_ERROR;

	lockid = ha->isp_ops->rd_reg_direct(ha, QLA83XX_DRV_LOCKRECOVERY);

	/* Check for other Recovery in progress, go wait */
	if ((lockid & 0x3) != 0)
		goto exit_lock_recovery;

	/* Intent to Recover */
	ha->isp_ops->wr_reg_direct(ha, QLA83XX_DRV_LOCKRECOVERY,
				   (ha->func_num << 2) | INTENT_TO_RECOVER);

	msleep(200);

	/* Check Intent to Recover is advertised */
	lockid = ha->isp_ops->rd_reg_direct(ha, QLA83XX_DRV_LOCKRECOVERY);
	if ((lockid & 0x3C) != (ha->func_num << 2))
		goto exit_lock_recovery;

	ql4_printk(KERN_INFO, ha, "%s: IDC Lock recovery initiated for func %d\n",
		   __func__, ha->func_num);

	/* Proceed to Recover */
	ha->isp_ops->wr_reg_direct(ha, QLA83XX_DRV_LOCKRECOVERY,
				   (ha->func_num << 2) | PROCEED_TO_RECOVER);

	/* Force Unlock */
	ha->isp_ops->wr_reg_direct(ha, QLA83XX_DRV_LOCK_ID, 0xFF);
	ha->isp_ops->rd_reg_direct(ha, QLA83XX_DRV_UNLOCK);

	/* Clear bits 0-5 in IDC_RECOVERY register*/
	ha->isp_ops->wr_reg_direct(ha, QLA83XX_DRV_LOCKRECOVERY, 0);

	/* Get lock */
	lock = ha->isp_ops->rd_reg_direct(ha, QLA83XX_DRV_LOCK);
	if (lock) {
		lockid = ha->isp_ops->rd_reg_direct(ha, QLA83XX_DRV_LOCK_ID);
		lockid = ((lockid + (1 << 8)) & ~0xFF) | ha->func_num;
		ha->isp_ops->wr_reg_direct(ha, QLA83XX_DRV_LOCK_ID, lockid);
		ret_val = QLA_SUCCESS;
	}

exit_lock_recovery:
	return ret_val;
}

#define	QLA83XX_DRV_LOCK_MSLEEP		200

int qla4_83xx_drv_lock(struct scsi_qla_host *ha)
{
	int timeout = 0;
	uint32_t status = 0;
	int ret_val = QLA_SUCCESS;
	uint32_t first_owner = 0;
	uint32_t tmo_owner = 0;
	uint32_t lock_id;
	uint32_t func_num;
	uint32_t lock_cnt;

	while (status == 0) {
		status = qla4_83xx_rd_reg(ha, QLA83XX_DRV_LOCK);
		if (status) {
			/* Increment Counter (8-31) and update func_num (0-7) on
			 * getting a successful lock  */
			lock_id = qla4_83xx_rd_reg(ha, QLA83XX_DRV_LOCK_ID);
			lock_id = ((lock_id + (1 << 8)) & ~0xFF) | ha->func_num;
			qla4_83xx_wr_reg(ha, QLA83XX_DRV_LOCK_ID, lock_id);
			break;
		}

		if (timeout == 0)
			/* Save counter + ID of function holding the lock for
			 * first failure */
			first_owner = ha->isp_ops->rd_reg_direct(ha,
							  QLA83XX_DRV_LOCK_ID);

		if (++timeout >=
		    (QLA83XX_DRV_LOCK_TIMEOUT / QLA83XX_DRV_LOCK_MSLEEP)) {
			tmo_owner = qla4_83xx_rd_reg(ha, QLA83XX_DRV_LOCK_ID);
			func_num = tmo_owner & 0xFF;
			lock_cnt = tmo_owner >> 8;
			ql4_printk(KERN_INFO, ha, "%s: Lock by func %d failed after 2s, lock held by func %d, lock count %d, first_owner %d\n",
				   __func__, ha->func_num, func_num, lock_cnt,
				   (first_owner & 0xFF));

			if (first_owner != tmo_owner) {
				/* Some other driver got lock, OR same driver
				 * got lock again (counter value changed), when
				 * we were waiting for lock.
				 * Retry for another 2 sec */
				ql4_printk(KERN_INFO, ha, "%s: IDC lock failed for func %d\n",
					   __func__, ha->func_num);
				timeout = 0;
			} else {
				/* Same driver holding lock > 2sec.
				 * Force Recovery */
				ret_val = qla4_83xx_lock_recovery(ha);
				if (ret_val == QLA_SUCCESS) {
					/* Recovered and got lock */
					ql4_printk(KERN_INFO, ha, "%s: IDC lock Recovery by %d successful\n",
						   __func__, ha->func_num);
					break;
				}
				/* Recovery Failed, some other function
				 * has the lock, wait for 2secs and retry */
				ql4_printk(KERN_INFO, ha, "%s: IDC lock Recovery by %d failed, Retrying timeout\n",
					   __func__, ha->func_num);
				timeout = 0;
			}
		}
		msleep(QLA83XX_DRV_LOCK_MSLEEP);
	}

	return ret_val;
}

void qla4_83xx_drv_unlock(struct scsi_qla_host *ha)
{
	int id;

	id = qla4_83xx_rd_reg(ha, QLA83XX_DRV_LOCK_ID);

	if ((id & 0xFF) != ha->func_num) {
		ql4_printk(KERN_ERR, ha, "%s: IDC Unlock by %d failed, lock owner is %d\n",
			   __func__, ha->func_num, (id & 0xFF));
		return;
	}

	/* Keep lock counter value, update the ha->func_num to 0xFF */
	qla4_83xx_wr_reg(ha, QLA83XX_DRV_LOCK_ID, (id | 0xFF));
	qla4_83xx_rd_reg(ha, QLA83XX_DRV_UNLOCK);
}

void qla4_83xx_set_idc_dontreset(struct scsi_qla_host *ha)
{
	uint32_t idc_ctrl;

	idc_ctrl = qla4_83xx_rd_reg(ha, QLA83XX_IDC_DRV_CTRL);
	idc_ctrl |= DONTRESET_BIT0;
	qla4_83xx_wr_reg(ha, QLA83XX_IDC_DRV_CTRL, idc_ctrl);
	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: idc_ctrl = %d\n", __func__,
			  idc_ctrl));
}

void qla4_83xx_clear_idc_dontreset(struct scsi_qla_host *ha)
{
	uint32_t idc_ctrl;

	idc_ctrl = qla4_83xx_rd_reg(ha, QLA83XX_IDC_DRV_CTRL);
	idc_ctrl &= ~DONTRESET_BIT0;
	qla4_83xx_wr_reg(ha, QLA83XX_IDC_DRV_CTRL, idc_ctrl);
	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: idc_ctrl = %d\n", __func__,
			  idc_ctrl));
}

int qla4_83xx_idc_dontreset(struct scsi_qla_host *ha)
{
	uint32_t idc_ctrl;

	idc_ctrl = qla4_83xx_rd_reg(ha, QLA83XX_IDC_DRV_CTRL);
	return idc_ctrl & DONTRESET_BIT0;
}

/*-------------------------IDC State Machine ---------------------*/

enum {
	UNKNOWN_CLASS = 0,
	NIC_CLASS,
	FCOE_CLASS,
	ISCSI_CLASS
};

struct device_info {
	int func_num;
	int device_type;
	int port_num;
};

int qla4_83xx_can_perform_reset(struct scsi_qla_host *ha)
{
	uint32_t drv_active;
	uint32_t dev_part, dev_part1, dev_part2;
	int i;
	struct device_info device_map[16];
	int func_nibble;
	int nibble;
	int nic_present = 0;
	int iscsi_present = 0;
	int iscsi_func_low = 0;

	/* Use the dev_partition register to determine the PCI function number
	 * and then check drv_active register to see which driver is loaded */
	dev_part1 = qla4_83xx_rd_reg(ha,
				     ha->reg_tbl[QLA8XXX_CRB_DEV_PART_INFO]);
	dev_part2 = qla4_83xx_rd_reg(ha, QLA83XX_CRB_DEV_PART_INFO2);
	drv_active = qla4_83xx_rd_reg(ha, ha->reg_tbl[QLA8XXX_CRB_DRV_ACTIVE]);

	/* Each function has 4 bits in dev_partition Info register,
	 * Lower 2 bits - device type, Upper 2 bits - physical port number */
	dev_part = dev_part1;
	for (i = nibble = 0; i <= 15; i++, nibble++) {
		func_nibble = dev_part & (0xF << (nibble * 4));
		func_nibble >>= (nibble * 4);
		device_map[i].func_num = i;
		device_map[i].device_type = func_nibble & 0x3;
		device_map[i].port_num = func_nibble & 0xC;

		if (device_map[i].device_type == NIC_CLASS) {
			if (drv_active & (1 << device_map[i].func_num)) {
				nic_present++;
				break;
			}
		} else if (device_map[i].device_type == ISCSI_CLASS) {
			if (drv_active & (1 << device_map[i].func_num)) {
				if (!iscsi_present ||
				    (iscsi_present &&
				     (iscsi_func_low > device_map[i].func_num)))
					iscsi_func_low = device_map[i].func_num;

				iscsi_present++;
			}
		}

		/* For function_num[8..15] get info from dev_part2 register */
		if (nibble == 7) {
			nibble = 0;
			dev_part = dev_part2;
		}
	}

	/* NIC, iSCSI and FCOE are the Reset owners based on order, NIC gets
	 * precedence over iSCSI and FCOE and iSCSI over FCOE, based on drivers
	 * present. */
	if (!nic_present && (ha->func_num == iscsi_func_low)) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "%s: can reset - NIC not present and lower iSCSI function is %d\n",
				  __func__, ha->func_num));
		return 1;
	}

	return 0;
}

/**
 * qla4_83xx_need_reset_handler - Code to start reset sequence
 * @ha: pointer to adapter structure
 *
 * Note: IDC lock must be held upon entry
 **/
void qla4_83xx_need_reset_handler(struct scsi_qla_host *ha)
{
	uint32_t dev_state, drv_state, drv_active;
	unsigned long reset_timeout, dev_init_timeout;

	ql4_printk(KERN_INFO, ha, "%s: Performing ISP error recovery\n",
		   __func__);

	if (!test_bit(AF_8XXX_RST_OWNER, &ha->flags)) {
		DEBUG2(ql4_printk(KERN_INFO, ha, "%s: reset acknowledged\n",
				  __func__));
		qla4_8xxx_set_rst_ready(ha);

		/* Non-reset owners ACK Reset and wait for device INIT state
		 * as part of Reset Recovery by Reset Owner */
		dev_init_timeout = jiffies + (ha->nx_dev_init_timeout * HZ);

		do {
			if (time_after_eq(jiffies, dev_init_timeout)) {
				ql4_printk(KERN_INFO, ha, "%s: Non Reset owner dev init timeout\n",
					   __func__);
				break;
			}

			ha->isp_ops->idc_unlock(ha);
			msleep(1000);
			ha->isp_ops->idc_lock(ha);

			dev_state = qla4_8xxx_rd_direct(ha,
							QLA8XXX_CRB_DEV_STATE);
		} while (dev_state == QLA8XXX_DEV_NEED_RESET);
	} else {
		qla4_8xxx_set_rst_ready(ha);
		reset_timeout = jiffies + (ha->nx_reset_timeout * HZ);
		drv_state = qla4_8xxx_rd_direct(ha, QLA8XXX_CRB_DRV_STATE);
		drv_active = qla4_8xxx_rd_direct(ha, QLA8XXX_CRB_DRV_ACTIVE);

		ql4_printk(KERN_INFO, ha, "%s: drv_state = 0x%x, drv_active = 0x%x\n",
			   __func__, drv_state, drv_active);

		while (drv_state != drv_active) {
			if (time_after_eq(jiffies, reset_timeout)) {
				ql4_printk(KERN_INFO, ha, "%s: %s: RESET TIMEOUT! drv_state: 0x%08x, drv_active: 0x%08x\n",
					   __func__, DRIVER_NAME, drv_state,
					   drv_active);
				break;
			}

			ha->isp_ops->idc_unlock(ha);
			msleep(1000);
			ha->isp_ops->idc_lock(ha);

			drv_state = qla4_8xxx_rd_direct(ha,
							QLA8XXX_CRB_DRV_STATE);
			drv_active = qla4_8xxx_rd_direct(ha,
							QLA8XXX_CRB_DRV_ACTIVE);
		}

		if (drv_state != drv_active) {
			ql4_printk(KERN_INFO, ha, "%s: Reset_owner turning off drv_active of non-acking function 0x%x\n",
				   __func__, (drv_active ^ drv_state));
			drv_active = drv_active & drv_state;
			qla4_8xxx_wr_direct(ha, QLA8XXX_CRB_DRV_ACTIVE,
					    drv_active);
		}

		clear_bit(AF_8XXX_RST_OWNER, &ha->flags);
		/* Start Reset Recovery */
		qla4_8xxx_device_bootstrap(ha);
	}
}

void qla4_83xx_get_idc_param(struct scsi_qla_host *ha)
{
	uint32_t idc_params, ret_val;

	ret_val = qla4_83xx_flash_read_u32(ha, QLA83XX_IDC_PARAM_ADDR,
					   (uint8_t *)&idc_params, 1);
	if (ret_val == QLA_SUCCESS) {
		ha->nx_dev_init_timeout = idc_params & 0xFFFF;
		ha->nx_reset_timeout = (idc_params >> 16) & 0xFFFF;
	} else {
		ha->nx_dev_init_timeout = ROM_DEV_INIT_TIMEOUT;
		ha->nx_reset_timeout = ROM_DRV_RESET_ACK_TIMEOUT;
	}

	DEBUG2(ql4_printk(KERN_DEBUG, ha,
			  "%s: ha->nx_dev_init_timeout = %d, ha->nx_reset_timeout = %d\n",
			  __func__, ha->nx_dev_init_timeout,
			  ha->nx_reset_timeout));
}

/*-------------------------Reset Sequence Functions-----------------------*/

static void qla4_83xx_dump_reset_seq_hdr(struct scsi_qla_host *ha)
{
	uint8_t *phdr;

	if (!ha->reset_tmplt.buff) {
		ql4_printk(KERN_ERR, ha, "%s: Error: Invalid reset_seq_template\n",
			   __func__);
		return;
	}

	phdr = ha->reset_tmplt.buff;

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "Reset Template: 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X 0x%X\n",
			  *phdr, *(phdr+1), *(phdr+2), *(phdr+3), *(phdr+4),
			  *(phdr+5), *(phdr+6), *(phdr+7), *(phdr + 8),
			  *(phdr+9), *(phdr+10), *(phdr+11), *(phdr+12),
			  *(phdr+13), *(phdr+14), *(phdr+15)));
}

static int qla4_83xx_copy_bootloader(struct scsi_qla_host *ha)
{
	uint8_t *p_cache;
	uint32_t src, count, size;
	uint64_t dest;
	int ret_val = QLA_SUCCESS;

	src = QLA83XX_BOOTLOADER_FLASH_ADDR;
	dest = qla4_83xx_rd_reg(ha, QLA83XX_BOOTLOADER_ADDR);
	size = qla4_83xx_rd_reg(ha, QLA83XX_BOOTLOADER_SIZE);

	/* 128 bit alignment check */
	if (size & 0xF)
		size = (size + 16) & ~0xF;

	/* 16 byte count */
	count = size/16;

	p_cache = vmalloc(size);
	if (p_cache == NULL) {
		ql4_printk(KERN_ERR, ha, "%s: Failed to allocate memory for boot loader cache\n",
			   __func__);
		ret_val = QLA_ERROR;
		goto exit_copy_bootloader;
	}

	ret_val = qla4_83xx_lockless_flash_read_u32(ha, src, p_cache,
						    size / sizeof(uint32_t));
	if (ret_val == QLA_ERROR) {
		ql4_printk(KERN_ERR, ha, "%s: Error reading firmware from flash\n",
			   __func__);
		goto exit_copy_error;
	}
	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: Read firmware from flash\n",
			  __func__));

	/* 128 bit/16 byte write to MS memory */
	ret_val = qla4_83xx_ms_mem_write_128b(ha, dest, (uint32_t *)p_cache,
					      count);
	if (ret_val == QLA_ERROR) {
		ql4_printk(KERN_ERR, ha, "%s: Error writing firmware to MS\n",
			   __func__);
		goto exit_copy_error;
	}

	DEBUG2(ql4_printk(KERN_INFO, ha, "%s: Wrote firmware size %d to MS\n",
			  __func__, size));

exit_copy_error:
	vfree(p_cache);

exit_copy_bootloader:
	return ret_val;
}

static int qla4_83xx_check_cmd_peg_status(struct scsi_qla_host *ha)
{
	uint32_t val, ret_val = QLA_ERROR;
	int retries = CRB_CMDPEG_CHECK_RETRY_COUNT;

	do {
		val = qla4_83xx_rd_reg(ha, QLA83XX_CMDPEG_STATE);
		if (val == PHAN_INITIALIZE_COMPLETE) {
			DEBUG2(ql4_printk(KERN_INFO, ha,
					  "%s: Command Peg initialization complete. State=0x%x\n",
					  __func__, val));
			ret_val = QLA_SUCCESS;
			break;
		}
		msleep(CRB_CMDPEG_CHECK_DELAY);
	} while (--retries);

	return ret_val;
}

/**
 * qla4_83xx_poll_reg - Poll the given CRB addr for duration msecs till
 * value read ANDed with test_mask is equal to test_result.
 *
 * @ha : Pointer to adapter structure
 * @addr : CRB register address
 * @duration : Poll for total of "duration" msecs
 * @test_mask : Mask value read with "test_mask"
 * @test_result : Compare (value&test_mask) with test_result.
 **/
static int qla4_83xx_poll_reg(struct scsi_qla_host *ha, uint32_t addr,
			      int duration, uint32_t test_mask,
			      uint32_t test_result)
{
	uint32_t value;
	uint8_t retries;
	int ret_val = QLA_SUCCESS;

	ret_val = qla4_83xx_rd_reg_indirect(ha, addr, &value);
	if (ret_val == QLA_ERROR)
		goto exit_poll_reg;

	retries = duration / 10;
	do {
		if ((value & test_mask) != test_result) {
			msleep(duration / 10);
			ret_val = qla4_83xx_rd_reg_indirect(ha, addr, &value);
			if (ret_val == QLA_ERROR)
				goto exit_poll_reg;

			ret_val = QLA_ERROR;
		} else {
			ret_val = QLA_SUCCESS;
			break;
		}
	} while (retries--);

exit_poll_reg:
	if (ret_val == QLA_ERROR) {
		ha->reset_tmplt.seq_error++;
		ql4_printk(KERN_ERR, ha, "%s: Poll Failed:  0x%08x 0x%08x 0x%08x\n",
			   __func__, value, test_mask, test_result);
	}

	return ret_val;
}

static int qla4_83xx_reset_seq_checksum_test(struct scsi_qla_host *ha)
{
	uint32_t sum =  0;
	uint16_t *buff = (uint16_t *)ha->reset_tmplt.buff;
	int u16_count =  ha->reset_tmplt.hdr->size / sizeof(uint16_t);
	int ret_val;

	while (u16_count-- > 0)
		sum += *buff++;

	while (sum >> 16)
		sum = (sum & 0xFFFF) +  (sum >> 16);

	/* checksum of 0 indicates a valid template */
	if (~sum) {
		ret_val = QLA_SUCCESS;
	} else {
		ql4_printk(KERN_ERR, ha, "%s: Reset seq checksum failed\n",
			   __func__);
		ret_val = QLA_ERROR;
	}

	return ret_val;
}

/**
 * qla4_83xx_read_reset_template - Read Reset Template from Flash
 * @ha: Pointer to adapter structure
 **/
void qla4_83xx_read_reset_template(struct scsi_qla_host *ha)
{
	uint8_t *p_buff;
	uint32_t addr, tmplt_hdr_def_size, tmplt_hdr_size;
	uint32_t ret_val;

	ha->reset_tmplt.seq_error = 0;
	ha->reset_tmplt.buff = vmalloc(QLA83XX_RESTART_TEMPLATE_SIZE);
	if (ha->reset_tmplt.buff == NULL) {
		ql4_printk(KERN_ERR, ha, "%s: Failed to allocate reset template resources\n",
			   __func__);
		goto exit_read_reset_template;
	}

	p_buff = ha->reset_tmplt.buff;
	addr = QLA83XX_RESET_TEMPLATE_ADDR;

	tmplt_hdr_def_size = sizeof(struct qla4_83xx_reset_template_hdr) /
				    sizeof(uint32_t);

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "%s: Read template hdr size %d from Flash\n",
			  __func__, tmplt_hdr_def_size));

	/* Copy template header from flash */
	ret_val = qla4_83xx_flash_read_u32(ha, addr, p_buff,
					   tmplt_hdr_def_size);
	if (ret_val != QLA_SUCCESS) {
		ql4_printk(KERN_ERR, ha, "%s: Failed to read reset template\n",
			   __func__);
		goto exit_read_template_error;
	}

	ha->reset_tmplt.hdr =
		(struct qla4_83xx_reset_template_hdr *)ha->reset_tmplt.buff;

	/* Validate the template header size and signature */
	tmplt_hdr_size = ha->reset_tmplt.hdr->hdr_size/sizeof(uint32_t);
	if ((tmplt_hdr_size != tmplt_hdr_def_size) ||
	    (ha->reset_tmplt.hdr->signature != RESET_TMPLT_HDR_SIGNATURE)) {
		ql4_printk(KERN_ERR, ha, "%s: Template Header size %d is invalid, tmplt_hdr_def_size %d\n",
			   __func__, tmplt_hdr_size, tmplt_hdr_def_size);
		goto exit_read_template_error;
	}

	addr = QLA83XX_RESET_TEMPLATE_ADDR + ha->reset_tmplt.hdr->hdr_size;
	p_buff = ha->reset_tmplt.buff + ha->reset_tmplt.hdr->hdr_size;
	tmplt_hdr_def_size = (ha->reset_tmplt.hdr->size -
			      ha->reset_tmplt.hdr->hdr_size) / sizeof(uint32_t);

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "%s: Read rest of the template size %d\n",
			  __func__, ha->reset_tmplt.hdr->size));

	/* Copy rest of the template */
	ret_val = qla4_83xx_flash_read_u32(ha, addr, p_buff,
					   tmplt_hdr_def_size);
	if (ret_val != QLA_SUCCESS) {
		ql4_printk(KERN_ERR, ha, "%s: Failed to read reset tempelate\n",
			   __func__);
		goto exit_read_template_error;
	}

	/* Integrity check */
	if (qla4_83xx_reset_seq_checksum_test(ha)) {
		ql4_printk(KERN_ERR, ha, "%s: Reset Seq checksum failed!\n",
			   __func__);
		goto exit_read_template_error;
	}
	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "%s: Reset Seq checksum passed, Get stop, start and init seq offsets\n",
			  __func__));

	/* Get STOP, START, INIT sequence offsets */
	ha->reset_tmplt.init_offset = ha->reset_tmplt.buff +
				      ha->reset_tmplt.hdr->init_seq_offset;
	ha->reset_tmplt.start_offset = ha->reset_tmplt.buff +
				       ha->reset_tmplt.hdr->start_seq_offset;
	ha->reset_tmplt.stop_offset = ha->reset_tmplt.buff +
				      ha->reset_tmplt.hdr->hdr_size;
	qla4_83xx_dump_reset_seq_hdr(ha);

	goto exit_read_reset_template;

exit_read_template_error:
	vfree(ha->reset_tmplt.buff);

exit_read_reset_template:
	return;
}

/**
 * qla4_83xx_read_write_crb_reg - Read from raddr and write value to waddr.
 *
 * @ha : Pointer to adapter structure
 * @raddr : CRB address to read from
 * @waddr : CRB address to write to
 **/
static void qla4_83xx_read_write_crb_reg(struct scsi_qla_host *ha,
					 uint32_t raddr, uint32_t waddr)
{
	uint32_t value;

	qla4_83xx_rd_reg_indirect(ha, raddr, &value);
	qla4_83xx_wr_reg_indirect(ha, waddr, value);
}

/**
 * qla4_83xx_rmw_crb_reg - Read Modify Write crb register
 *
 * This function read value from raddr, AND with test_mask,
 * Shift Left,Right/OR/XOR with values RMW header and write value to waddr.
 *
 * @ha : Pointer to adapter structure
 * @raddr : CRB address to read from
 * @waddr : CRB address to write to
 * @p_rmw_hdr : header with shift/or/xor values.
 **/
static void qla4_83xx_rmw_crb_reg(struct scsi_qla_host *ha, uint32_t raddr,
				  uint32_t waddr,
				  struct qla4_83xx_rmw *p_rmw_hdr)
{
	uint32_t value;

	if (p_rmw_hdr->index_a)
		value = ha->reset_tmplt.array[p_rmw_hdr->index_a];
	else
		qla4_83xx_rd_reg_indirect(ha, raddr, &value);

	value &= p_rmw_hdr->test_mask;
	value <<= p_rmw_hdr->shl;
	value >>= p_rmw_hdr->shr;
	value |= p_rmw_hdr->or_value;
	value ^= p_rmw_hdr->xor_value;

	qla4_83xx_wr_reg_indirect(ha, waddr, value);

	return;
}

static void qla4_83xx_write_list(struct scsi_qla_host *ha,
				 struct qla4_83xx_reset_entry_hdr *p_hdr)
{
	struct qla4_83xx_entry *p_entry;
	uint32_t i;

	p_entry = (struct qla4_83xx_entry *)
		  ((char *)p_hdr + sizeof(struct qla4_83xx_reset_entry_hdr));

	for (i = 0; i < p_hdr->count; i++, p_entry++) {
		qla4_83xx_wr_reg_indirect(ha, p_entry->arg1, p_entry->arg2);
		if (p_hdr->delay)
			udelay((uint32_t)(p_hdr->delay));
	}
}

static void qla4_83xx_read_write_list(struct scsi_qla_host *ha,
				      struct qla4_83xx_reset_entry_hdr *p_hdr)
{
	struct qla4_83xx_entry *p_entry;
	uint32_t i;

	p_entry = (struct qla4_83xx_entry *)
		  ((char *)p_hdr + sizeof(struct qla4_83xx_reset_entry_hdr));

	for (i = 0; i < p_hdr->count; i++, p_entry++) {
		qla4_83xx_read_write_crb_reg(ha, p_entry->arg1, p_entry->arg2);
		if (p_hdr->delay)
			udelay((uint32_t)(p_hdr->delay));
	}
}

static void qla4_83xx_poll_list(struct scsi_qla_host *ha,
				struct qla4_83xx_reset_entry_hdr *p_hdr)
{
	long delay;
	struct qla4_83xx_entry *p_entry;
	struct qla4_83xx_poll *p_poll;
	uint32_t i;
	uint32_t value;

	p_poll = (struct qla4_83xx_poll *)
		 ((char *)p_hdr + sizeof(struct qla4_83xx_reset_entry_hdr));

	/* Entries start after 8 byte qla4_83xx_poll, poll header contains
	 * the test_mask, test_value. */
	p_entry = (struct qla4_83xx_entry *)((char *)p_poll +
					     sizeof(struct qla4_83xx_poll));

	delay = (long)p_hdr->delay;
	if (!delay) {
		for (i = 0; i < p_hdr->count; i++, p_entry++) {
			qla4_83xx_poll_reg(ha, p_entry->arg1, delay,
					   p_poll->test_mask,
					   p_poll->test_value);
		}
	} else {
		for (i = 0; i < p_hdr->count; i++, p_entry++) {
			if (qla4_83xx_poll_reg(ha, p_entry->arg1, delay,
					       p_poll->test_mask,
					       p_poll->test_value)) {
				qla4_83xx_rd_reg_indirect(ha, p_entry->arg1,
							  &value);
				qla4_83xx_rd_reg_indirect(ha, p_entry->arg2,
							  &value);
			}
		}
	}
}

static void qla4_83xx_poll_write_list(struct scsi_qla_host *ha,
				      struct qla4_83xx_reset_entry_hdr *p_hdr)
{
	long delay;
	struct qla4_83xx_quad_entry *p_entry;
	struct qla4_83xx_poll *p_poll;
	uint32_t i;

	p_poll = (struct qla4_83xx_poll *)
		 ((char *)p_hdr + sizeof(struct qla4_83xx_reset_entry_hdr));
	p_entry = (struct qla4_83xx_quad_entry *)
		  ((char *)p_poll + sizeof(struct qla4_83xx_poll));
	delay = (long)p_hdr->delay;

	for (i = 0; i < p_hdr->count; i++, p_entry++) {
		qla4_83xx_wr_reg_indirect(ha, p_entry->dr_addr,
					  p_entry->dr_value);
		qla4_83xx_wr_reg_indirect(ha, p_entry->ar_addr,
					  p_entry->ar_value);
		if (delay) {
			if (qla4_83xx_poll_reg(ha, p_entry->ar_addr, delay,
					       p_poll->test_mask,
					       p_poll->test_value)) {
				DEBUG2(ql4_printk(KERN_INFO, ha,
						  "%s: Timeout Error: poll list, item_num %d, entry_num %d\n",
						  __func__, i,
						  ha->reset_tmplt.seq_index));
			}
		}
	}
}

static void qla4_83xx_read_modify_write(struct scsi_qla_host *ha,
					struct qla4_83xx_reset_entry_hdr *p_hdr)
{
	struct qla4_83xx_entry *p_entry;
	struct qla4_83xx_rmw *p_rmw_hdr;
	uint32_t i;

	p_rmw_hdr = (struct qla4_83xx_rmw *)
		    ((char *)p_hdr + sizeof(struct qla4_83xx_reset_entry_hdr));
	p_entry = (struct qla4_83xx_entry *)
		  ((char *)p_rmw_hdr + sizeof(struct qla4_83xx_rmw));

	for (i = 0; i < p_hdr->count; i++, p_entry++) {
		qla4_83xx_rmw_crb_reg(ha, p_entry->arg1, p_entry->arg2,
				      p_rmw_hdr);
		if (p_hdr->delay)
			udelay((uint32_t)(p_hdr->delay));
	}
}

static void qla4_83xx_pause(struct scsi_qla_host *ha,
			    struct qla4_83xx_reset_entry_hdr *p_hdr)
{
	if (p_hdr->delay)
		mdelay((uint32_t)((long)p_hdr->delay));
}

static void qla4_83xx_poll_read_list(struct scsi_qla_host *ha,
				     struct qla4_83xx_reset_entry_hdr *p_hdr)
{
	long delay;
	int index;
	struct qla4_83xx_quad_entry *p_entry;
	struct qla4_83xx_poll *p_poll;
	uint32_t i;
	uint32_t value;

	p_poll = (struct qla4_83xx_poll *)
		 ((char *)p_hdr + sizeof(struct qla4_83xx_reset_entry_hdr));
	p_entry = (struct qla4_83xx_quad_entry *)
		  ((char *)p_poll + sizeof(struct qla4_83xx_poll));
	delay = (long)p_hdr->delay;

	for (i = 0; i < p_hdr->count; i++, p_entry++) {
		qla4_83xx_wr_reg_indirect(ha, p_entry->ar_addr,
					  p_entry->ar_value);
		if (delay) {
			if (qla4_83xx_poll_reg(ha, p_entry->ar_addr, delay,
					       p_poll->test_mask,
					       p_poll->test_value)) {
				DEBUG2(ql4_printk(KERN_INFO, ha,
						  "%s: Timeout Error: poll list, Item_num %d, entry_num %d\n",
						  __func__, i,
						  ha->reset_tmplt.seq_index));
			} else {
				index = ha->reset_tmplt.array_index;
				qla4_83xx_rd_reg_indirect(ha, p_entry->dr_addr,
							  &value);
				ha->reset_tmplt.array[index++] = value;

				if (index == QLA83XX_MAX_RESET_SEQ_ENTRIES)
					ha->reset_tmplt.array_index = 1;
			}
		}
	}
}

static void qla4_83xx_seq_end(struct scsi_qla_host *ha,
			      struct qla4_83xx_reset_entry_hdr *p_hdr)
{
	ha->reset_tmplt.seq_end = 1;
}

static void qla4_83xx_template_end(struct scsi_qla_host *ha,
				   struct qla4_83xx_reset_entry_hdr *p_hdr)
{
	ha->reset_tmplt.template_end = 1;

	if (ha->reset_tmplt.seq_error == 0) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "%s: Reset sequence completed SUCCESSFULLY.\n",
				  __func__));
	} else {
		ql4_printk(KERN_ERR, ha, "%s: Reset sequence completed with some timeout errors.\n",
			   __func__);
	}
}

/**
 * qla4_83xx_process_reset_template - Process reset template.
 *
 * Process all entries in reset template till entry with SEQ_END opcode,
 * which indicates end of the reset template processing. Each entry has a
 * Reset Entry header, entry opcode/command, with size of the entry, number
 * of entries in sub-sequence and delay in microsecs or timeout in millisecs.
 *
 * @ha : Pointer to adapter structure
 * @p_buff : Common reset entry header.
 **/
static void qla4_83xx_process_reset_template(struct scsi_qla_host *ha,
					     char *p_buff)
{
	int index, entries;
	struct qla4_83xx_reset_entry_hdr *p_hdr;
	char *p_entry = p_buff;

	ha->reset_tmplt.seq_end = 0;
	ha->reset_tmplt.template_end = 0;
	entries = ha->reset_tmplt.hdr->entries;
	index = ha->reset_tmplt.seq_index;

	for (; (!ha->reset_tmplt.seq_end) && (index  < entries); index++) {

		p_hdr = (struct qla4_83xx_reset_entry_hdr *)p_entry;
		switch (p_hdr->cmd) {
		case OPCODE_NOP:
			break;
		case OPCODE_WRITE_LIST:
			qla4_83xx_write_list(ha, p_hdr);
			break;
		case OPCODE_READ_WRITE_LIST:
			qla4_83xx_read_write_list(ha, p_hdr);
			break;
		case OPCODE_POLL_LIST:
			qla4_83xx_poll_list(ha, p_hdr);
			break;
		case OPCODE_POLL_WRITE_LIST:
			qla4_83xx_poll_write_list(ha, p_hdr);
			break;
		case OPCODE_READ_MODIFY_WRITE:
			qla4_83xx_read_modify_write(ha, p_hdr);
			break;
		case OPCODE_SEQ_PAUSE:
			qla4_83xx_pause(ha, p_hdr);
			break;
		case OPCODE_SEQ_END:
			qla4_83xx_seq_end(ha, p_hdr);
			break;
		case OPCODE_TMPL_END:
			qla4_83xx_template_end(ha, p_hdr);
			break;
		case OPCODE_POLL_READ_LIST:
			qla4_83xx_poll_read_list(ha, p_hdr);
			break;
		default:
			ql4_printk(KERN_ERR, ha, "%s: Unknown command ==> 0x%04x on entry = %d\n",
				   __func__, p_hdr->cmd, index);
			break;
		}

		/* Set pointer to next entry in the sequence. */
		p_entry += p_hdr->size;
	}

	ha->reset_tmplt.seq_index = index;
}

static void qla4_83xx_process_stop_seq(struct scsi_qla_host *ha)
{
	ha->reset_tmplt.seq_index = 0;
	qla4_83xx_process_reset_template(ha, ha->reset_tmplt.stop_offset);

	if (ha->reset_tmplt.seq_end != 1)
		ql4_printk(KERN_ERR, ha, "%s: Abrupt STOP Sub-Sequence end.\n",
			   __func__);
}

static void qla4_83xx_process_start_seq(struct scsi_qla_host *ha)
{
	qla4_83xx_process_reset_template(ha, ha->reset_tmplt.start_offset);

	if (ha->reset_tmplt.template_end != 1)
		ql4_printk(KERN_ERR, ha, "%s: Abrupt START Sub-Sequence end.\n",
			   __func__);
}

static void qla4_83xx_process_init_seq(struct scsi_qla_host *ha)
{
	qla4_83xx_process_reset_template(ha, ha->reset_tmplt.init_offset);

	if (ha->reset_tmplt.seq_end != 1)
		ql4_printk(KERN_ERR, ha, "%s: Abrupt INIT Sub-Sequence end.\n",
			   __func__);
}

static int qla4_83xx_restart(struct scsi_qla_host *ha)
{
	int ret_val = QLA_SUCCESS;

	qla4_83xx_process_stop_seq(ha);

	/* Collect minidump*/
	if (!test_and_clear_bit(AF_83XX_NO_FW_DUMP, &ha->flags))
		qla4_8xxx_get_minidump(ha);

	qla4_83xx_process_init_seq(ha);

	if (qla4_83xx_copy_bootloader(ha)) {
		ql4_printk(KERN_ERR, ha, "%s: Copy bootloader, firmware restart failed!\n",
			   __func__);
		ret_val = QLA_ERROR;
		goto exit_restart;
	}

	qla4_83xx_wr_reg(ha, QLA83XX_FW_IMAGE_VALID, QLA83XX_BOOT_FROM_FLASH);
	qla4_83xx_process_start_seq(ha);

exit_restart:
	return ret_val;
}

int qla4_83xx_start_firmware(struct scsi_qla_host *ha)
{
	int ret_val = QLA_SUCCESS;

	ret_val = qla4_83xx_restart(ha);
	if (ret_val == QLA_ERROR) {
		ql4_printk(KERN_ERR, ha, "%s: Restart error\n", __func__);
		goto exit_start_fw;
	} else {
		DEBUG2(ql4_printk(KERN_INFO, ha, "%s: Restart done\n",
				  __func__));
	}

	ret_val = qla4_83xx_check_cmd_peg_status(ha);
	if (ret_val == QLA_ERROR)
		ql4_printk(KERN_ERR, ha, "%s: Peg not initialized\n",
			   __func__);

exit_start_fw:
	return ret_val;
}

/*----------------------Interrupt Related functions ---------------------*/

static void qla4_83xx_disable_iocb_intrs(struct scsi_qla_host *ha)
{
	if (test_and_clear_bit(AF_83XX_IOCB_INTR_ON, &ha->flags))
		qla4_8xxx_intr_disable(ha);
}

static void qla4_83xx_disable_mbox_intrs(struct scsi_qla_host *ha)
{
	uint32_t mb_int, ret;

	if (test_and_clear_bit(AF_83XX_MBOX_INTR_ON, &ha->flags)) {
		ret = readl(&ha->qla4_83xx_reg->mbox_int);
		mb_int = ret & ~INT_ENABLE_FW_MB;
		writel(mb_int, &ha->qla4_83xx_reg->mbox_int);
		writel(1, &ha->qla4_83xx_reg->leg_int_mask);
	}
}

void qla4_83xx_disable_intrs(struct scsi_qla_host *ha)
{
	qla4_83xx_disable_mbox_intrs(ha);
	qla4_83xx_disable_iocb_intrs(ha);
}

static void qla4_83xx_enable_iocb_intrs(struct scsi_qla_host *ha)
{
	if (!test_bit(AF_83XX_IOCB_INTR_ON, &ha->flags)) {
		qla4_8xxx_intr_enable(ha);
		set_bit(AF_83XX_IOCB_INTR_ON, &ha->flags);
	}
}

void qla4_83xx_enable_mbox_intrs(struct scsi_qla_host *ha)
{
	uint32_t mb_int;

	if (!test_bit(AF_83XX_MBOX_INTR_ON, &ha->flags)) {
		mb_int = INT_ENABLE_FW_MB;
		writel(mb_int, &ha->qla4_83xx_reg->mbox_int);
		writel(0, &ha->qla4_83xx_reg->leg_int_mask);
		set_bit(AF_83XX_MBOX_INTR_ON, &ha->flags);
	}
}


void qla4_83xx_enable_intrs(struct scsi_qla_host *ha)
{
	qla4_83xx_enable_mbox_intrs(ha);
	qla4_83xx_enable_iocb_intrs(ha);
}


void qla4_83xx_queue_mbox_cmd(struct scsi_qla_host *ha, uint32_t *mbx_cmd,
			      int incount)
{
	int i;

	/* Load all mailbox registers, except mailbox 0. */
	for (i = 1; i < incount; i++)
		writel(mbx_cmd[i], &ha->qla4_83xx_reg->mailbox_in[i]);

	writel(mbx_cmd[0], &ha->qla4_83xx_reg->mailbox_in[0]);

	/* Set Host Interrupt register to 1, to tell the firmware that
	 * a mailbox command is pending. Firmware after reading the
	 * mailbox command, clears the host interrupt register */
	writel(HINT_MBX_INT_PENDING, &ha->qla4_83xx_reg->host_intr);
}

void qla4_83xx_process_mbox_intr(struct scsi_qla_host *ha, int outcount)
{
	int intr_status;

	intr_status = readl(&ha->qla4_83xx_reg->risc_intr);
	if (intr_status) {
		ha->mbox_status_count = outcount;
		ha->isp_ops->interrupt_service_routine(ha, intr_status);
	}
}

/**
 * qla4_83xx_isp_reset - Resets ISP and aborts all outstanding commands.
 * @ha: pointer to host adapter structure.
 **/
int qla4_83xx_isp_reset(struct scsi_qla_host *ha)
{
	int rval;
	uint32_t dev_state;

	ha->isp_ops->idc_lock(ha);
	dev_state = qla4_8xxx_rd_direct(ha, QLA8XXX_CRB_DEV_STATE);

	if (ql4xdontresethba)
		qla4_83xx_set_idc_dontreset(ha);

	if (dev_state == QLA8XXX_DEV_READY) {
		/* If IDC_CTRL DONTRESETHBA_BIT0 is set dont do reset
		 * recovery */
		if (qla4_83xx_idc_dontreset(ha) == DONTRESET_BIT0) {
			ql4_printk(KERN_ERR, ha, "%s: Reset recovery disabled\n",
				   __func__);
			rval = QLA_ERROR;
			goto exit_isp_reset;
		}

		DEBUG2(ql4_printk(KERN_INFO, ha, "%s: HW State: NEED RESET\n",
				  __func__));
		qla4_8xxx_wr_direct(ha, QLA8XXX_CRB_DEV_STATE,
				    QLA8XXX_DEV_NEED_RESET);

	} else {
		/* If device_state is NEED_RESET, go ahead with
		 * Reset,irrespective of ql4xdontresethba. This is to allow a
		 * non-reset-owner to force a reset. Non-reset-owner sets
		 * the IDC_CTRL BIT0 to prevent Reset-owner from doing a Reset
		 * and then forces a Reset by setting device_state to
		 * NEED_RESET. */
		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "%s: HW state already set to NEED_RESET\n",
				  __func__));
	}

	/* For ISP8324 and ISP8042, Reset owner is NIC, iSCSI or FCOE based on
	 * priority and which drivers are present. Unlike ISP8022, the function
	 * setting NEED_RESET, may not be the Reset owner. */
	if (qla4_83xx_can_perform_reset(ha))
		set_bit(AF_8XXX_RST_OWNER, &ha->flags);

	ha->isp_ops->idc_unlock(ha);
	rval = qla4_8xxx_device_state_handler(ha);

	ha->isp_ops->idc_lock(ha);
	qla4_8xxx_clear_rst_ready(ha);
exit_isp_reset:
	ha->isp_ops->idc_unlock(ha);

	if (rval == QLA_SUCCESS)
		clear_bit(AF_FW_RECOVERY, &ha->flags);

	return rval;
}

static void qla4_83xx_dump_pause_control_regs(struct scsi_qla_host *ha)
{
	u32 val = 0, val1 = 0;
	int i, status = QLA_SUCCESS;

	status = qla4_83xx_rd_reg_indirect(ha, QLA83XX_SRE_SHIM_CONTROL, &val);
	DEBUG2(ql4_printk(KERN_INFO, ha, "SRE-Shim Ctrl:0x%x\n", val));

	/* Port 0 Rx Buffer Pause Threshold Registers. */
	DEBUG2(ql4_printk(KERN_INFO, ha,
		"Port 0 Rx Buffer Pause Threshold Registers[TC7..TC0]:"));
	for (i = 0; i < 8; i++) {
		status = qla4_83xx_rd_reg_indirect(ha,
				QLA83XX_PORT0_RXB_PAUSE_THRS + (i * 0x4), &val);
		DEBUG2(pr_info("0x%x ", val));
	}

	DEBUG2(pr_info("\n"));

	/* Port 1 Rx Buffer Pause Threshold Registers. */
	DEBUG2(ql4_printk(KERN_INFO, ha,
		"Port 1 Rx Buffer Pause Threshold Registers[TC7..TC0]:"));
	for (i = 0; i < 8; i++) {
		status = qla4_83xx_rd_reg_indirect(ha,
				QLA83XX_PORT1_RXB_PAUSE_THRS + (i * 0x4), &val);
		DEBUG2(pr_info("0x%x  ", val));
	}

	DEBUG2(pr_info("\n"));

	/* Port 0 RxB Traffic Class Max Cell Registers. */
	DEBUG2(ql4_printk(KERN_INFO, ha,
		"Port 0 RxB Traffic Class Max Cell Registers[3..0]:"));
	for (i = 0; i < 4; i++) {
		status = qla4_83xx_rd_reg_indirect(ha,
			       QLA83XX_PORT0_RXB_TC_MAX_CELL + (i * 0x4), &val);
		DEBUG2(pr_info("0x%x  ", val));
	}

	DEBUG2(pr_info("\n"));

	/* Port 1 RxB Traffic Class Max Cell Registers. */
	DEBUG2(ql4_printk(KERN_INFO, ha,
		"Port 1 RxB Traffic Class Max Cell Registers[3..0]:"));
	for (i = 0; i < 4; i++) {
		status = qla4_83xx_rd_reg_indirect(ha,
			       QLA83XX_PORT1_RXB_TC_MAX_CELL + (i * 0x4), &val);
		DEBUG2(pr_info("0x%x  ", val));
	}

	DEBUG2(pr_info("\n"));

	/* Port 0 RxB Rx Traffic Class Stats. */
	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "Port 0 RxB Rx Traffic Class Stats [TC7..TC0]"));
	for (i = 7; i >= 0; i--) {
		status = qla4_83xx_rd_reg_indirect(ha,
						   QLA83XX_PORT0_RXB_TC_STATS,
						   &val);
		val &= ~(0x7 << 29);    /* Reset bits 29 to 31 */
		qla4_83xx_wr_reg_indirect(ha, QLA83XX_PORT0_RXB_TC_STATS,
					  (val | (i << 29)));
		status = qla4_83xx_rd_reg_indirect(ha,
						   QLA83XX_PORT0_RXB_TC_STATS,
						   &val);
		DEBUG2(pr_info("0x%x  ", val));
	}

	DEBUG2(pr_info("\n"));

	/* Port 1 RxB Rx Traffic Class Stats. */
	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "Port 1 RxB Rx Traffic Class Stats [TC7..TC0]"));
	for (i = 7; i >= 0; i--) {
		status = qla4_83xx_rd_reg_indirect(ha,
						   QLA83XX_PORT1_RXB_TC_STATS,
						   &val);
		val &= ~(0x7 << 29);    /* Reset bits 29 to 31 */
		qla4_83xx_wr_reg_indirect(ha, QLA83XX_PORT1_RXB_TC_STATS,
					  (val | (i << 29)));
		status = qla4_83xx_rd_reg_indirect(ha,
						   QLA83XX_PORT1_RXB_TC_STATS,
						   &val);
		DEBUG2(pr_info("0x%x  ", val));
	}

	DEBUG2(pr_info("\n"));

	status = qla4_83xx_rd_reg_indirect(ha, QLA83XX_PORT2_IFB_PAUSE_THRS,
					   &val);
	status = qla4_83xx_rd_reg_indirect(ha, QLA83XX_PORT3_IFB_PAUSE_THRS,
					   &val1);

	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "IFB-Pause Thresholds: Port 2:0x%x, Port 3:0x%x\n",
			  val, val1));
}

static void __qla4_83xx_disable_pause(struct scsi_qla_host *ha)
{
	int i;

	/* set SRE-Shim Control Register */
	qla4_83xx_wr_reg_indirect(ha, QLA83XX_SRE_SHIM_CONTROL,
				  QLA83XX_SET_PAUSE_VAL);

	for (i = 0; i < 8; i++) {
		/* Port 0 Rx Buffer Pause Threshold Registers. */
		qla4_83xx_wr_reg_indirect(ha,
				      QLA83XX_PORT0_RXB_PAUSE_THRS + (i * 0x4),
				      QLA83XX_SET_PAUSE_VAL);
		/* Port 1 Rx Buffer Pause Threshold Registers. */
		qla4_83xx_wr_reg_indirect(ha,
				      QLA83XX_PORT1_RXB_PAUSE_THRS + (i * 0x4),
				      QLA83XX_SET_PAUSE_VAL);
	}

	for (i = 0; i < 4; i++) {
		/* Port 0 RxB Traffic Class Max Cell Registers. */
		qla4_83xx_wr_reg_indirect(ha,
				     QLA83XX_PORT0_RXB_TC_MAX_CELL + (i * 0x4),
				     QLA83XX_SET_TC_MAX_CELL_VAL);
		/* Port 1 RxB Traffic Class Max Cell Registers. */
		qla4_83xx_wr_reg_indirect(ha,
				     QLA83XX_PORT1_RXB_TC_MAX_CELL + (i * 0x4),
				     QLA83XX_SET_TC_MAX_CELL_VAL);
	}

	qla4_83xx_wr_reg_indirect(ha, QLA83XX_PORT2_IFB_PAUSE_THRS,
				  QLA83XX_SET_PAUSE_VAL);
	qla4_83xx_wr_reg_indirect(ha, QLA83XX_PORT3_IFB_PAUSE_THRS,
				  QLA83XX_SET_PAUSE_VAL);

	ql4_printk(KERN_INFO, ha, "Disabled pause frames successfully.\n");
}

/**
 * qla4_83xx_eport_init - Initialize EPort.
 * @ha: Pointer to host adapter structure.
 *
 * If EPort hardware is in reset state before disabling pause, there would be
 * serious hardware wedging issues. To prevent this perform eport init everytime
 * before disabling pause frames.
 **/
static void qla4_83xx_eport_init(struct scsi_qla_host *ha)
{
	/* Clear the 8 registers */
	qla4_83xx_wr_reg_indirect(ha, QLA83XX_RESET_REG, 0x0);
	qla4_83xx_wr_reg_indirect(ha, QLA83XX_RESET_PORT0, 0x0);
	qla4_83xx_wr_reg_indirect(ha, QLA83XX_RESET_PORT1, 0x0);
	qla4_83xx_wr_reg_indirect(ha, QLA83XX_RESET_PORT2, 0x0);
	qla4_83xx_wr_reg_indirect(ha, QLA83XX_RESET_PORT3, 0x0);
	qla4_83xx_wr_reg_indirect(ha, QLA83XX_RESET_SRE_SHIM, 0x0);
	qla4_83xx_wr_reg_indirect(ha, QLA83XX_RESET_EPG_SHIM, 0x0);
	qla4_83xx_wr_reg_indirect(ha, QLA83XX_RESET_ETHER_PCS, 0x0);

	/* Write any value to Reset Control register */
	qla4_83xx_wr_reg_indirect(ha, QLA83XX_RESET_CONTROL, 0xFF);

	ql4_printk(KERN_INFO, ha, "EPORT is out of reset.\n");
}

void qla4_83xx_disable_pause(struct scsi_qla_host *ha)
{
	ha->isp_ops->idc_lock(ha);
	/* Before disabling pause frames, ensure that eport is not in reset */
	qla4_83xx_eport_init(ha);
	qla4_83xx_dump_pause_control_regs(ha);
	__qla4_83xx_disable_pause(ha);
	ha->isp_ops->idc_unlock(ha);
}

/**
 * qla4_83xx_is_detached - Check if we are marked invisible.
 * @ha: Pointer to host adapter structure.
 **/
int qla4_83xx_is_detached(struct scsi_qla_host *ha)
{
	uint32_t drv_active;

	drv_active = qla4_8xxx_rd_direct(ha, QLA8XXX_CRB_DRV_ACTIVE);

	if (test_bit(AF_INIT_DONE, &ha->flags) &&
	    !(drv_active & (1 << ha->func_num))) {
		DEBUG2(ql4_printk(KERN_INFO, ha, "%s: drv_active = 0x%X\n",
				  __func__, drv_active));
		return QLA_SUCCESS;
	}

	return QLA_ERROR;
}
