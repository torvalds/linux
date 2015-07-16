/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */

#include <linux/vmalloc.h>
#include <linux/delay.h>

#include "qla_def.h"
#include "qla_gbl.h"

#include <linux/delay.h>

#define TIMEOUT_100_MS 100

/* 8044 Flash Read/Write functions */
uint32_t
qla8044_rd_reg(struct qla_hw_data *ha, ulong addr)
{
	return readl((void __iomem *) (ha->nx_pcibase + addr));
}

void
qla8044_wr_reg(struct qla_hw_data *ha, ulong addr, uint32_t val)
{
	writel(val, (void __iomem *)((ha)->nx_pcibase + addr));
}

int
qla8044_rd_direct(struct scsi_qla_host *vha,
	const uint32_t crb_reg)
{
	struct qla_hw_data *ha = vha->hw;

	if (crb_reg < CRB_REG_INDEX_MAX)
		return qla8044_rd_reg(ha, qla8044_reg_tbl[crb_reg]);
	else
		return QLA_FUNCTION_FAILED;
}

void
qla8044_wr_direct(struct scsi_qla_host *vha,
	const uint32_t crb_reg,
	const uint32_t value)
{
	struct qla_hw_data *ha = vha->hw;

	if (crb_reg < CRB_REG_INDEX_MAX)
		qla8044_wr_reg(ha, qla8044_reg_tbl[crb_reg], value);
}

static int
qla8044_set_win_base(scsi_qla_host_t *vha, uint32_t addr)
{
	uint32_t val;
	int ret_val = QLA_SUCCESS;
	struct qla_hw_data *ha = vha->hw;

	qla8044_wr_reg(ha, QLA8044_CRB_WIN_FUNC(ha->portnum), addr);
	val = qla8044_rd_reg(ha, QLA8044_CRB_WIN_FUNC(ha->portnum));

	if (val != addr) {
		ql_log(ql_log_warn, vha, 0xb087,
		    "%s: Failed to set register window : "
		    "addr written 0x%x, read 0x%x!\n",
		    __func__, addr, val);
		ret_val = QLA_FUNCTION_FAILED;
	}
	return ret_val;
}

static int
qla8044_rd_reg_indirect(scsi_qla_host_t *vha, uint32_t addr, uint32_t *data)
{
	int ret_val = QLA_SUCCESS;
	struct qla_hw_data *ha = vha->hw;

	ret_val = qla8044_set_win_base(vha, addr);
	if (!ret_val)
		*data = qla8044_rd_reg(ha, QLA8044_WILDCARD);
	else
		ql_log(ql_log_warn, vha, 0xb088,
		    "%s: failed read of addr 0x%x!\n", __func__, addr);
	return ret_val;
}

static int
qla8044_wr_reg_indirect(scsi_qla_host_t *vha, uint32_t addr, uint32_t data)
{
	int ret_val = QLA_SUCCESS;
	struct qla_hw_data *ha = vha->hw;

	ret_val = qla8044_set_win_base(vha, addr);
	if (!ret_val)
		qla8044_wr_reg(ha, QLA8044_WILDCARD, data);
	else
		ql_log(ql_log_warn, vha, 0xb089,
		    "%s: failed wrt to addr 0x%x, data 0x%x\n",
		    __func__, addr, data);
	return ret_val;
}

/*
 * qla8044_read_write_crb_reg - Read from raddr and write value to waddr.
 *
 * @ha : Pointer to adapter structure
 * @raddr : CRB address to read from
 * @waddr : CRB address to write to
 *
 */
static void
qla8044_read_write_crb_reg(struct scsi_qla_host *vha,
	uint32_t raddr, uint32_t waddr)
{
	uint32_t value;

	qla8044_rd_reg_indirect(vha, raddr, &value);
	qla8044_wr_reg_indirect(vha, waddr, value);
}

static int
qla8044_poll_wait_for_ready(struct scsi_qla_host *vha, uint32_t addr1,
	uint32_t mask)
{
	unsigned long timeout;
	uint32_t temp;

	/* jiffies after 100ms */
	timeout = jiffies + msecs_to_jiffies(TIMEOUT_100_MS);
	do {
		qla8044_rd_reg_indirect(vha, addr1, &temp);
		if ((temp & mask) != 0)
			break;
		if (time_after_eq(jiffies, timeout)) {
			ql_log(ql_log_warn, vha, 0xb151,
				"Error in processing rdmdio entry\n");
			return -1;
		}
	} while (1);

	return 0;
}

static uint32_t
qla8044_ipmdio_rd_reg(struct scsi_qla_host *vha,
	uint32_t addr1, uint32_t addr3, uint32_t mask, uint32_t addr)
{
	uint32_t temp;
	int ret = 0;

	ret = qla8044_poll_wait_for_ready(vha, addr1, mask);
	if (ret == -1)
		return -1;

	temp = (0x40000000 | addr);
	qla8044_wr_reg_indirect(vha, addr1, temp);

	ret = qla8044_poll_wait_for_ready(vha, addr1, mask);
	if (ret == -1)
		return 0;

	qla8044_rd_reg_indirect(vha, addr3, &ret);

	return ret;
}


static int
qla8044_poll_wait_ipmdio_bus_idle(struct scsi_qla_host *vha,
	uint32_t addr1, uint32_t addr2, uint32_t addr3, uint32_t mask)
{
	unsigned long timeout;
	uint32_t temp;

	/* jiffies after 100 msecs */
	timeout = jiffies + msecs_to_jiffies(TIMEOUT_100_MS);
	do {
		temp = qla8044_ipmdio_rd_reg(vha, addr1, addr3, mask, addr2);
		if ((temp & 0x1) != 1)
			break;
		if (time_after_eq(jiffies, timeout)) {
			ql_log(ql_log_warn, vha, 0xb152,
			    "Error in processing mdiobus idle\n");
			return -1;
		}
	} while (1);

	return 0;
}

static int
qla8044_ipmdio_wr_reg(struct scsi_qla_host *vha, uint32_t addr1,
	uint32_t addr3, uint32_t mask, uint32_t addr, uint32_t value)
{
	int ret = 0;

	ret = qla8044_poll_wait_for_ready(vha, addr1, mask);
	if (ret == -1)
		return -1;

	qla8044_wr_reg_indirect(vha, addr3, value);
	qla8044_wr_reg_indirect(vha, addr1, addr);

	ret = qla8044_poll_wait_for_ready(vha, addr1, mask);
	if (ret == -1)
		return -1;

	return 0;
}
/*
 * qla8044_rmw_crb_reg - Read value from raddr, AND with test_mask,
 * Shift Left,Right/OR/XOR with values RMW header and write value to waddr.
 *
 * @vha : Pointer to adapter structure
 * @raddr : CRB address to read from
 * @waddr : CRB address to write to
 * @p_rmw_hdr : header with shift/or/xor values.
 *
 */
static void
qla8044_rmw_crb_reg(struct scsi_qla_host *vha,
	uint32_t raddr, uint32_t waddr,	struct qla8044_rmw *p_rmw_hdr)
{
	uint32_t value;

	if (p_rmw_hdr->index_a)
		value = vha->reset_tmplt.array[p_rmw_hdr->index_a];
	else
		qla8044_rd_reg_indirect(vha, raddr, &value);
	value &= p_rmw_hdr->test_mask;
	value <<= p_rmw_hdr->shl;
	value >>= p_rmw_hdr->shr;
	value |= p_rmw_hdr->or_value;
	value ^= p_rmw_hdr->xor_value;
	qla8044_wr_reg_indirect(vha, waddr, value);
	return;
}

static inline void
qla8044_set_qsnt_ready(struct scsi_qla_host *vha)
{
	uint32_t qsnt_state;
	struct qla_hw_data *ha = vha->hw;

	qsnt_state = qla8044_rd_direct(vha, QLA8044_CRB_DRV_STATE_INDEX);
	qsnt_state |= (1 << ha->portnum);
	qla8044_wr_direct(vha, QLA8044_CRB_DRV_STATE_INDEX, qsnt_state);
	ql_log(ql_log_info, vha, 0xb08e, "%s(%ld): qsnt_state: 0x%08x\n",
	     __func__, vha->host_no, qsnt_state);
}

void
qla8044_clear_qsnt_ready(struct scsi_qla_host *vha)
{
	uint32_t qsnt_state;
	struct qla_hw_data *ha = vha->hw;

	qsnt_state = qla8044_rd_direct(vha, QLA8044_CRB_DRV_STATE_INDEX);
	qsnt_state &= ~(1 << ha->portnum);
	qla8044_wr_direct(vha, QLA8044_CRB_DRV_STATE_INDEX, qsnt_state);
	ql_log(ql_log_info, vha, 0xb08f, "%s(%ld): qsnt_state: 0x%08x\n",
	    __func__, vha->host_no, qsnt_state);
}

/**
 *
 * qla8044_lock_recovery - Recovers the idc_lock.
 * @ha : Pointer to adapter structure
 *
 * Lock Recovery Register
 * 5-2	Lock recovery owner: Function ID of driver doing lock recovery,
 *	valid if bits 1..0 are set by driver doing lock recovery.
 * 1-0  1 - Driver intends to force unlock the IDC lock.
 *	2 - Driver is moving forward to unlock the IDC lock. Driver clears
 *	    this field after force unlocking the IDC lock.
 *
 * Lock Recovery process
 * a. Read the IDC_LOCK_RECOVERY register. If the value in bits 1..0 is
 *    greater than 0, then wait for the other driver to unlock otherwise
 *    move to the next step.
 * b. Indicate intent to force-unlock by writing 1h to the IDC_LOCK_RECOVERY
 *    register bits 1..0 and also set the function# in bits 5..2.
 * c. Read the IDC_LOCK_RECOVERY register again after a delay of 200ms.
 *    Wait for the other driver to perform lock recovery if the function
 *    number in bits 5..2 has changed, otherwise move to the next step.
 * d. Write a value of 2h to the IDC_LOCK_RECOVERY register bits 1..0
 *    leaving your function# in bits 5..2.
 * e. Force unlock using the DRIVER_UNLOCK register and immediately clear
 *    the IDC_LOCK_RECOVERY bits 5..0 by writing 0.
 **/
static int
qla8044_lock_recovery(struct scsi_qla_host *vha)
{
	uint32_t lock = 0, lockid;
	struct qla_hw_data *ha = vha->hw;

	lockid = qla8044_rd_reg(ha, QLA8044_DRV_LOCKRECOVERY);

	/* Check for other Recovery in progress, go wait */
	if ((lockid & IDC_LOCK_RECOVERY_STATE_MASK) != 0)
		return QLA_FUNCTION_FAILED;

	/* Intent to Recover */
	qla8044_wr_reg(ha, QLA8044_DRV_LOCKRECOVERY,
	    (ha->portnum <<
	     IDC_LOCK_RECOVERY_STATE_SHIFT_BITS) | INTENT_TO_RECOVER);
	msleep(200);

	/* Check Intent to Recover is advertised */
	lockid = qla8044_rd_reg(ha, QLA8044_DRV_LOCKRECOVERY);
	if ((lockid & IDC_LOCK_RECOVERY_OWNER_MASK) != (ha->portnum <<
	    IDC_LOCK_RECOVERY_STATE_SHIFT_BITS))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_p3p, vha, 0xb08B, "%s:%d: IDC Lock recovery initiated\n"
	    , __func__, ha->portnum);

	/* Proceed to Recover */
	qla8044_wr_reg(ha, QLA8044_DRV_LOCKRECOVERY,
	    (ha->portnum << IDC_LOCK_RECOVERY_STATE_SHIFT_BITS) |
	    PROCEED_TO_RECOVER);

	/* Force Unlock() */
	qla8044_wr_reg(ha, QLA8044_DRV_LOCK_ID, 0xFF);
	qla8044_rd_reg(ha, QLA8044_DRV_UNLOCK);

	/* Clear bits 0-5 in IDC_RECOVERY register*/
	qla8044_wr_reg(ha, QLA8044_DRV_LOCKRECOVERY, 0);

	/* Get lock() */
	lock = qla8044_rd_reg(ha, QLA8044_DRV_LOCK);
	if (lock) {
		lockid = qla8044_rd_reg(ha, QLA8044_DRV_LOCK_ID);
		lockid = ((lockid + (1 << 8)) & ~0xFF) | ha->portnum;
		qla8044_wr_reg(ha, QLA8044_DRV_LOCK_ID, lockid);
		return QLA_SUCCESS;
	} else
		return QLA_FUNCTION_FAILED;
}

int
qla8044_idc_lock(struct qla_hw_data *ha)
{
	uint32_t ret_val = QLA_SUCCESS, timeout = 0, status = 0;
	uint32_t lock_id, lock_cnt, func_num, tmo_owner = 0, first_owner = 0;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	while (status == 0) {
		/* acquire semaphore5 from PCI HW block */
		status = qla8044_rd_reg(ha, QLA8044_DRV_LOCK);

		if (status) {
			/* Increment Counter (8-31) and update func_num (0-7) on
			 * getting a successful lock  */
			lock_id = qla8044_rd_reg(ha, QLA8044_DRV_LOCK_ID);
			lock_id = ((lock_id + (1 << 8)) & ~0xFF) | ha->portnum;
			qla8044_wr_reg(ha, QLA8044_DRV_LOCK_ID, lock_id);
			break;
		}

		if (timeout == 0)
			first_owner = qla8044_rd_reg(ha, QLA8044_DRV_LOCK_ID);

		if (++timeout >=
		    (QLA8044_DRV_LOCK_TIMEOUT / QLA8044_DRV_LOCK_MSLEEP)) {
			tmo_owner = qla8044_rd_reg(ha, QLA8044_DRV_LOCK_ID);
			func_num = tmo_owner & 0xFF;
			lock_cnt = tmo_owner >> 8;
			ql_log(ql_log_warn, vha, 0xb114,
			    "%s: Lock by func %d failed after 2s, lock held "
			    "by func %d, lock count %d, first_owner %d\n",
			    __func__, ha->portnum, func_num, lock_cnt,
			    (first_owner & 0xFF));
			if (first_owner != tmo_owner) {
				/* Some other driver got lock,
				 * OR same driver got lock again (counter
				 * value changed), when we were waiting for
				 * lock. Retry for another 2 sec */
				ql_dbg(ql_dbg_p3p, vha, 0xb115,
				    "%s: %d: IDC lock failed\n",
				    __func__, ha->portnum);
				timeout = 0;
			} else {
				/* Same driver holding lock > 2sec.
				 * Force Recovery */
				if (qla8044_lock_recovery(vha) == QLA_SUCCESS) {
					/* Recovered and got lock */
					ret_val = QLA_SUCCESS;
					ql_dbg(ql_dbg_p3p, vha, 0xb116,
					    "%s:IDC lock Recovery by %d"
					    "successful...\n", __func__,
					     ha->portnum);
				}
				/* Recovery Failed, some other function
				 * has the lock, wait for 2secs
				 * and retry
				 */
				ql_dbg(ql_dbg_p3p, vha, 0xb08a,
				       "%s: IDC lock Recovery by %d "
				       "failed, Retrying timeout\n", __func__,
				       ha->portnum);
				timeout = 0;
			}
		}
		msleep(QLA8044_DRV_LOCK_MSLEEP);
	}
	return ret_val;
}

void
qla8044_idc_unlock(struct qla_hw_data *ha)
{
	int id;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	id = qla8044_rd_reg(ha, QLA8044_DRV_LOCK_ID);

	if ((id & 0xFF) != ha->portnum) {
		ql_log(ql_log_warn, vha, 0xb118,
		    "%s: IDC Unlock by %d failed, lock owner is %d!\n",
		    __func__, ha->portnum, (id & 0xFF));
		return;
	}

	/* Keep lock counter value, update the ha->func_num to 0xFF */
	qla8044_wr_reg(ha, QLA8044_DRV_LOCK_ID, (id | 0xFF));
	qla8044_rd_reg(ha, QLA8044_DRV_UNLOCK);
}

/* 8044 Flash Lock/Unlock functions */
static int
qla8044_flash_lock(scsi_qla_host_t *vha)
{
	int lock_owner;
	int timeout = 0;
	uint32_t lock_status = 0;
	int ret_val = QLA_SUCCESS;
	struct qla_hw_data *ha = vha->hw;

	while (lock_status == 0) {
		lock_status = qla8044_rd_reg(ha, QLA8044_FLASH_LOCK);
		if (lock_status)
			break;

		if (++timeout >= QLA8044_FLASH_LOCK_TIMEOUT / 20) {
			lock_owner = qla8044_rd_reg(ha,
			    QLA8044_FLASH_LOCK_ID);
			ql_log(ql_log_warn, vha, 0xb113,
			    "%s: Simultaneous flash access by following ports, active port = %d: accessing port = %d",
			    __func__, ha->portnum, lock_owner);
			ret_val = QLA_FUNCTION_FAILED;
			break;
		}
		msleep(20);
	}
	qla8044_wr_reg(ha, QLA8044_FLASH_LOCK_ID, ha->portnum);
	return ret_val;
}

static void
qla8044_flash_unlock(scsi_qla_host_t *vha)
{
	int ret_val;
	struct qla_hw_data *ha = vha->hw;

	/* Reading FLASH_UNLOCK register unlocks the Flash */
	qla8044_wr_reg(ha, QLA8044_FLASH_LOCK_ID, 0xFF);
	ret_val = qla8044_rd_reg(ha, QLA8044_FLASH_UNLOCK);
}


static
void qla8044_flash_lock_recovery(struct scsi_qla_host *vha)
{

	if (qla8044_flash_lock(vha)) {
		/* Someone else is holding the lock. */
		ql_log(ql_log_warn, vha, 0xb120, "Resetting flash_lock\n");
	}

	/*
	 * Either we got the lock, or someone
	 * else died while holding it.
	 * In either case, unlock.
	 */
	qla8044_flash_unlock(vha);
}

/*
 * Address and length are byte address
 */
static int
qla8044_read_flash_data(scsi_qla_host_t *vha,  uint8_t *p_data,
	uint32_t flash_addr, int u32_word_count)
{
	int i, ret_val = QLA_SUCCESS;
	uint32_t u32_word;

	if (qla8044_flash_lock(vha) != QLA_SUCCESS) {
		ret_val = QLA_FUNCTION_FAILED;
		goto exit_lock_error;
	}

	if (flash_addr & 0x03) {
		ql_log(ql_log_warn, vha, 0xb117,
		    "%s: Illegal addr = 0x%x\n", __func__, flash_addr);
		ret_val = QLA_FUNCTION_FAILED;
		goto exit_flash_read;
	}

	for (i = 0; i < u32_word_count; i++) {
		if (qla8044_wr_reg_indirect(vha, QLA8044_FLASH_DIRECT_WINDOW,
		    (flash_addr & 0xFFFF0000))) {
			ql_log(ql_log_warn, vha, 0xb119,
			    "%s: failed to write addr 0x%x to "
			    "FLASH_DIRECT_WINDOW\n! ",
			    __func__, flash_addr);
			ret_val = QLA_FUNCTION_FAILED;
			goto exit_flash_read;
		}

		ret_val = qla8044_rd_reg_indirect(vha,
		    QLA8044_FLASH_DIRECT_DATA(flash_addr),
		    &u32_word);
		if (ret_val != QLA_SUCCESS) {
			ql_log(ql_log_warn, vha, 0xb08c,
			    "%s: failed to read addr 0x%x!\n",
			    __func__, flash_addr);
			goto exit_flash_read;
		}

		*(uint32_t *)p_data = u32_word;
		p_data = p_data + 4;
		flash_addr = flash_addr + 4;
	}

exit_flash_read:
	qla8044_flash_unlock(vha);

exit_lock_error:
	return ret_val;
}

/*
 * Address and length are byte address
 */
uint8_t *
qla8044_read_optrom_data(struct scsi_qla_host *vha, uint8_t *buf,
	uint32_t offset, uint32_t length)
{
	scsi_block_requests(vha->host);
	if (qla8044_read_flash_data(vha, (uint8_t *)buf, offset, length / 4)
	    != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha,  0xb08d,
		    "%s: Failed to read from flash\n",
		    __func__);
	}
	scsi_unblock_requests(vha->host);
	return buf;
}

inline int
qla8044_need_reset(struct scsi_qla_host *vha)
{
	uint32_t drv_state, drv_active;
	int rval;
	struct qla_hw_data *ha = vha->hw;

	drv_active = qla8044_rd_direct(vha, QLA8044_CRB_DRV_ACTIVE_INDEX);
	drv_state = qla8044_rd_direct(vha, QLA8044_CRB_DRV_STATE_INDEX);

	rval = drv_state & (1 << ha->portnum);

	if (ha->flags.eeh_busy && drv_active)
		rval = 1;
	return rval;
}

/*
 * qla8044_write_list - Write the value (p_entry->arg2) to address specified
 * by p_entry->arg1 for all entries in header with delay of p_hdr->delay between
 * entries.
 *
 * @vha : Pointer to adapter structure
 * @p_hdr : reset_entry header for WRITE_LIST opcode.
 *
 */
static void
qla8044_write_list(struct scsi_qla_host *vha,
	struct qla8044_reset_entry_hdr *p_hdr)
{
	struct qla8044_entry *p_entry;
	uint32_t i;

	p_entry = (struct qla8044_entry *)((char *)p_hdr +
	    sizeof(struct qla8044_reset_entry_hdr));

	for (i = 0; i < p_hdr->count; i++, p_entry++) {
		qla8044_wr_reg_indirect(vha, p_entry->arg1, p_entry->arg2);
		if (p_hdr->delay)
			udelay((uint32_t)(p_hdr->delay));
	}
}

/*
 * qla8044_read_write_list - Read from address specified by p_entry->arg1,
 * write value read to address specified by p_entry->arg2, for all entries in
 * header with delay of p_hdr->delay between entries.
 *
 * @vha : Pointer to adapter structure
 * @p_hdr : reset_entry header for READ_WRITE_LIST opcode.
 *
 */
static void
qla8044_read_write_list(struct scsi_qla_host *vha,
	struct qla8044_reset_entry_hdr *p_hdr)
{
	struct qla8044_entry *p_entry;
	uint32_t i;

	p_entry = (struct qla8044_entry *)((char *)p_hdr +
	    sizeof(struct qla8044_reset_entry_hdr));

	for (i = 0; i < p_hdr->count; i++, p_entry++) {
		qla8044_read_write_crb_reg(vha, p_entry->arg1,
		    p_entry->arg2);
		if (p_hdr->delay)
			udelay((uint32_t)(p_hdr->delay));
	}
}

/*
 * qla8044_poll_reg - Poll the given CRB addr for duration msecs till
 * value read ANDed with test_mask is equal to test_result.
 *
 * @ha : Pointer to adapter structure
 * @addr : CRB register address
 * @duration : Poll for total of "duration" msecs
 * @test_mask : Mask value read with "test_mask"
 * @test_result : Compare (value&test_mask) with test_result.
 *
 * Return Value - QLA_SUCCESS/QLA_FUNCTION_FAILED
 */
static int
qla8044_poll_reg(struct scsi_qla_host *vha, uint32_t addr,
	int duration, uint32_t test_mask, uint32_t test_result)
{
	uint32_t value;
	int timeout_error;
	uint8_t retries;
	int ret_val = QLA_SUCCESS;

	ret_val = qla8044_rd_reg_indirect(vha, addr, &value);
	if (ret_val == QLA_FUNCTION_FAILED) {
		timeout_error = 1;
		goto exit_poll_reg;
	}

	/* poll every 1/10 of the total duration */
	retries = duration/10;

	do {
		if ((value & test_mask) != test_result) {
			timeout_error = 1;
			msleep(duration/10);
			ret_val = qla8044_rd_reg_indirect(vha, addr, &value);
			if (ret_val == QLA_FUNCTION_FAILED) {
				timeout_error = 1;
				goto exit_poll_reg;
			}
		} else {
			timeout_error = 0;
			break;
		}
	} while (retries--);

exit_poll_reg:
	if (timeout_error) {
		vha->reset_tmplt.seq_error++;
		ql_log(ql_log_fatal, vha, 0xb090,
		    "%s: Poll Failed: 0x%08x 0x%08x 0x%08x\n",
		    __func__, value, test_mask, test_result);
	}

	return timeout_error;
}

/*
 * qla8044_poll_list - For all entries in the POLL_LIST header, poll read CRB
 * register specified by p_entry->arg1 and compare (value AND test_mask) with
 * test_result to validate it. Wait for p_hdr->delay between processing entries.
 *
 * @ha : Pointer to adapter structure
 * @p_hdr : reset_entry header for POLL_LIST opcode.
 *
 */
static void
qla8044_poll_list(struct scsi_qla_host *vha,
	struct qla8044_reset_entry_hdr *p_hdr)
{
	long delay;
	struct qla8044_entry *p_entry;
	struct qla8044_poll *p_poll;
	uint32_t i;
	uint32_t value;

	p_poll = (struct qla8044_poll *)
		((char *)p_hdr + sizeof(struct qla8044_reset_entry_hdr));

	/* Entries start after 8 byte qla8044_poll, poll header contains
	 * the test_mask, test_value.
	 */
	p_entry = (struct qla8044_entry *)((char *)p_poll +
	    sizeof(struct qla8044_poll));

	delay = (long)p_hdr->delay;

	if (!delay) {
		for (i = 0; i < p_hdr->count; i++, p_entry++)
			qla8044_poll_reg(vha, p_entry->arg1,
			    delay, p_poll->test_mask, p_poll->test_value);
	} else {
		for (i = 0; i < p_hdr->count; i++, p_entry++) {
			if (delay) {
				if (qla8044_poll_reg(vha,
				    p_entry->arg1, delay,
				    p_poll->test_mask,
				    p_poll->test_value)) {
					/*If
					* (data_read&test_mask != test_value)
					* read TIMEOUT_ADDR (arg1) and
					* ADDR (arg2) registers
					*/
					qla8044_rd_reg_indirect(vha,
					    p_entry->arg1, &value);
					qla8044_rd_reg_indirect(vha,
					    p_entry->arg2, &value);
				}
			}
		}
	}
}

/*
 * qla8044_poll_write_list - Write dr_value, ar_value to dr_addr/ar_addr,
 * read ar_addr, if (value& test_mask != test_mask) re-read till timeout
 * expires.
 *
 * @vha : Pointer to adapter structure
 * @p_hdr : reset entry header for POLL_WRITE_LIST opcode.
 *
 */
static void
qla8044_poll_write_list(struct scsi_qla_host *vha,
	struct qla8044_reset_entry_hdr *p_hdr)
{
	long delay;
	struct qla8044_quad_entry *p_entry;
	struct qla8044_poll *p_poll;
	uint32_t i;

	p_poll = (struct qla8044_poll *)((char *)p_hdr +
	    sizeof(struct qla8044_reset_entry_hdr));

	p_entry = (struct qla8044_quad_entry *)((char *)p_poll +
	    sizeof(struct qla8044_poll));

	delay = (long)p_hdr->delay;

	for (i = 0; i < p_hdr->count; i++, p_entry++) {
		qla8044_wr_reg_indirect(vha,
		    p_entry->dr_addr, p_entry->dr_value);
		qla8044_wr_reg_indirect(vha,
		    p_entry->ar_addr, p_entry->ar_value);
		if (delay) {
			if (qla8044_poll_reg(vha,
			    p_entry->ar_addr, delay,
			    p_poll->test_mask,
			    p_poll->test_value)) {
				ql_dbg(ql_dbg_p3p, vha, 0xb091,
				    "%s: Timeout Error: poll list, ",
				    __func__);
				ql_dbg(ql_dbg_p3p, vha, 0xb092,
				    "item_num %d, entry_num %d\n", i,
				    vha->reset_tmplt.seq_index);
			}
		}
	}
}

/*
 * qla8044_read_modify_write - Read value from p_entry->arg1, modify the
 * value, write value to p_entry->arg2. Process entries with p_hdr->delay
 * between entries.
 *
 * @vha : Pointer to adapter structure
 * @p_hdr : header with shift/or/xor values.
 *
 */
static void
qla8044_read_modify_write(struct scsi_qla_host *vha,
	struct qla8044_reset_entry_hdr *p_hdr)
{
	struct qla8044_entry *p_entry;
	struct qla8044_rmw *p_rmw_hdr;
	uint32_t i;

	p_rmw_hdr = (struct qla8044_rmw *)((char *)p_hdr +
	    sizeof(struct qla8044_reset_entry_hdr));

	p_entry = (struct qla8044_entry *)((char *)p_rmw_hdr +
	    sizeof(struct qla8044_rmw));

	for (i = 0; i < p_hdr->count; i++, p_entry++) {
		qla8044_rmw_crb_reg(vha, p_entry->arg1,
		    p_entry->arg2, p_rmw_hdr);
		if (p_hdr->delay)
			udelay((uint32_t)(p_hdr->delay));
	}
}

/*
 * qla8044_pause - Wait for p_hdr->delay msecs, called between processing
 * two entries of a sequence.
 *
 * @vha : Pointer to adapter structure
 * @p_hdr : Common reset entry header.
 *
 */
static
void qla8044_pause(struct scsi_qla_host *vha,
	struct qla8044_reset_entry_hdr *p_hdr)
{
	if (p_hdr->delay)
		mdelay((uint32_t)((long)p_hdr->delay));
}

/*
 * qla8044_template_end - Indicates end of reset sequence processing.
 *
 * @vha : Pointer to adapter structure
 * @p_hdr : Common reset entry header.
 *
 */
static void
qla8044_template_end(struct scsi_qla_host *vha,
	struct qla8044_reset_entry_hdr *p_hdr)
{
	vha->reset_tmplt.template_end = 1;

	if (vha->reset_tmplt.seq_error == 0) {
		ql_dbg(ql_dbg_p3p, vha, 0xb093,
		    "%s: Reset sequence completed SUCCESSFULLY.\n", __func__);
	} else {
		ql_log(ql_log_fatal, vha, 0xb094,
		    "%s: Reset sequence completed with some timeout "
		    "errors.\n", __func__);
	}
}

/*
 * qla8044_poll_read_list - Write ar_value to ar_addr register, read ar_addr,
 * if (value & test_mask != test_value) re-read till timeout value expires,
 * read dr_addr register and assign to reset_tmplt.array.
 *
 * @vha : Pointer to adapter structure
 * @p_hdr : Common reset entry header.
 *
 */
static void
qla8044_poll_read_list(struct scsi_qla_host *vha,
	struct qla8044_reset_entry_hdr *p_hdr)
{
	long delay;
	int index;
	struct qla8044_quad_entry *p_entry;
	struct qla8044_poll *p_poll;
	uint32_t i;
	uint32_t value;

	p_poll = (struct qla8044_poll *)
		((char *)p_hdr + sizeof(struct qla8044_reset_entry_hdr));

	p_entry = (struct qla8044_quad_entry *)
		((char *)p_poll + sizeof(struct qla8044_poll));

	delay = (long)p_hdr->delay;

	for (i = 0; i < p_hdr->count; i++, p_entry++) {
		qla8044_wr_reg_indirect(vha, p_entry->ar_addr,
		    p_entry->ar_value);
		if (delay) {
			if (qla8044_poll_reg(vha, p_entry->ar_addr, delay,
			    p_poll->test_mask, p_poll->test_value)) {
				ql_dbg(ql_dbg_p3p, vha, 0xb095,
				    "%s: Timeout Error: poll "
				    "list, ", __func__);
				ql_dbg(ql_dbg_p3p, vha, 0xb096,
				    "Item_num %d, "
				    "entry_num %d\n", i,
				    vha->reset_tmplt.seq_index);
			} else {
				index = vha->reset_tmplt.array_index;
				qla8044_rd_reg_indirect(vha,
				    p_entry->dr_addr, &value);
				vha->reset_tmplt.array[index++] = value;
				if (index == QLA8044_MAX_RESET_SEQ_ENTRIES)
					vha->reset_tmplt.array_index = 1;
			}
		}
	}
}

/*
 * qla8031_process_reset_template - Process all entries in reset template
 * till entry with SEQ_END opcode, which indicates end of the reset template
 * processing. Each entry has a Reset Entry header, entry opcode/command, with
 * size of the entry, number of entries in sub-sequence and delay in microsecs
 * or timeout in millisecs.
 *
 * @ha : Pointer to adapter structure
 * @p_buff : Common reset entry header.
 *
 */
static void
qla8044_process_reset_template(struct scsi_qla_host *vha,
	char *p_buff)
{
	int index, entries;
	struct qla8044_reset_entry_hdr *p_hdr;
	char *p_entry = p_buff;

	vha->reset_tmplt.seq_end = 0;
	vha->reset_tmplt.template_end = 0;
	entries = vha->reset_tmplt.hdr->entries;
	index = vha->reset_tmplt.seq_index;

	for (; (!vha->reset_tmplt.seq_end) && (index  < entries); index++) {
		p_hdr = (struct qla8044_reset_entry_hdr *)p_entry;
		switch (p_hdr->cmd) {
		case OPCODE_NOP:
			break;
		case OPCODE_WRITE_LIST:
			qla8044_write_list(vha, p_hdr);
			break;
		case OPCODE_READ_WRITE_LIST:
			qla8044_read_write_list(vha, p_hdr);
			break;
		case OPCODE_POLL_LIST:
			qla8044_poll_list(vha, p_hdr);
			break;
		case OPCODE_POLL_WRITE_LIST:
			qla8044_poll_write_list(vha, p_hdr);
			break;
		case OPCODE_READ_MODIFY_WRITE:
			qla8044_read_modify_write(vha, p_hdr);
			break;
		case OPCODE_SEQ_PAUSE:
			qla8044_pause(vha, p_hdr);
			break;
		case OPCODE_SEQ_END:
			vha->reset_tmplt.seq_end = 1;
			break;
		case OPCODE_TMPL_END:
			qla8044_template_end(vha, p_hdr);
			break;
		case OPCODE_POLL_READ_LIST:
			qla8044_poll_read_list(vha, p_hdr);
			break;
		default:
			ql_log(ql_log_fatal, vha, 0xb097,
			    "%s: Unknown command ==> 0x%04x on "
			    "entry = %d\n", __func__, p_hdr->cmd, index);
			break;
		}
		/*
		 *Set pointer to next entry in the sequence.
		*/
		p_entry += p_hdr->size;
	}
	vha->reset_tmplt.seq_index = index;
}

static void
qla8044_process_init_seq(struct scsi_qla_host *vha)
{
	qla8044_process_reset_template(vha,
	    vha->reset_tmplt.init_offset);
	if (vha->reset_tmplt.seq_end != 1)
		ql_log(ql_log_fatal, vha, 0xb098,
		    "%s: Abrupt INIT Sub-Sequence end.\n",
		    __func__);
}

static void
qla8044_process_stop_seq(struct scsi_qla_host *vha)
{
	vha->reset_tmplt.seq_index = 0;
	qla8044_process_reset_template(vha, vha->reset_tmplt.stop_offset);
	if (vha->reset_tmplt.seq_end != 1)
		ql_log(ql_log_fatal, vha, 0xb099,
		    "%s: Abrupt STOP Sub-Sequence end.\n", __func__);
}

static void
qla8044_process_start_seq(struct scsi_qla_host *vha)
{
	qla8044_process_reset_template(vha, vha->reset_tmplt.start_offset);
	if (vha->reset_tmplt.template_end != 1)
		ql_log(ql_log_fatal, vha, 0xb09a,
		    "%s: Abrupt START Sub-Sequence end.\n",
		    __func__);
}

static int
qla8044_lockless_flash_read_u32(struct scsi_qla_host *vha,
	uint32_t flash_addr, uint8_t *p_data, int u32_word_count)
{
	uint32_t i;
	uint32_t u32_word;
	uint32_t flash_offset;
	uint32_t addr = flash_addr;
	int ret_val = QLA_SUCCESS;

	flash_offset = addr & (QLA8044_FLASH_SECTOR_SIZE - 1);

	if (addr & 0x3) {
		ql_log(ql_log_fatal, vha, 0xb09b, "%s: Illegal addr = 0x%x\n",
		    __func__, addr);
		ret_val = QLA_FUNCTION_FAILED;
		goto exit_lockless_read;
	}

	ret_val = qla8044_wr_reg_indirect(vha,
	    QLA8044_FLASH_DIRECT_WINDOW, (addr));

	if (ret_val != QLA_SUCCESS) {
		ql_log(ql_log_fatal, vha, 0xb09c,
		    "%s: failed to write addr 0x%x to FLASH_DIRECT_WINDOW!\n",
		    __func__, addr);
		goto exit_lockless_read;
	}

	/* Check if data is spread across multiple sectors  */
	if ((flash_offset + (u32_word_count * sizeof(uint32_t))) >
	    (QLA8044_FLASH_SECTOR_SIZE - 1)) {
		/* Multi sector read */
		for (i = 0; i < u32_word_count; i++) {
			ret_val = qla8044_rd_reg_indirect(vha,
			    QLA8044_FLASH_DIRECT_DATA(addr), &u32_word);
			if (ret_val != QLA_SUCCESS) {
				ql_log(ql_log_fatal, vha, 0xb09d,
				    "%s: failed to read addr 0x%x!\n",
				    __func__, addr);
				goto exit_lockless_read;
			}
			*(uint32_t *)p_data  = u32_word;
			p_data = p_data + 4;
			addr = addr + 4;
			flash_offset = flash_offset + 4;
			if (flash_offset > (QLA8044_FLASH_SECTOR_SIZE - 1)) {
				/* This write is needed once for each sector */
				ret_val = qla8044_wr_reg_indirect(vha,
				    QLA8044_FLASH_DIRECT_WINDOW, (addr));
				if (ret_val != QLA_SUCCESS) {
					ql_log(ql_log_fatal, vha, 0xb09f,
					    "%s: failed to write addr "
					    "0x%x to FLASH_DIRECT_WINDOW!\n",
					    __func__, addr);
					goto exit_lockless_read;
				}
				flash_offset = 0;
			}
		}
	} else {
		/* Single sector read */
		for (i = 0; i < u32_word_count; i++) {
			ret_val = qla8044_rd_reg_indirect(vha,
			    QLA8044_FLASH_DIRECT_DATA(addr), &u32_word);
			if (ret_val != QLA_SUCCESS) {
				ql_log(ql_log_fatal, vha, 0xb0a0,
				    "%s: failed to read addr 0x%x!\n",
				    __func__, addr);
				goto exit_lockless_read;
			}
			*(uint32_t *)p_data = u32_word;
			p_data = p_data + 4;
			addr = addr + 4;
		}
	}

exit_lockless_read:
	return ret_val;
}

/*
 * qla8044_ms_mem_write_128b - Writes data to MS/off-chip memory
 *
 * @vha : Pointer to adapter structure
 * addr : Flash address to write to
 * data : Data to be written
 * count : word_count to be written
 *
 * Return Value - QLA_SUCCESS/QLA_FUNCTION_FAILED
 */
static int
qla8044_ms_mem_write_128b(struct scsi_qla_host *vha,
	uint64_t addr, uint32_t *data, uint32_t count)
{
	int i, j, ret_val = QLA_SUCCESS;
	uint32_t agt_ctrl;
	unsigned long flags;
	struct qla_hw_data *ha = vha->hw;

	/* Only 128-bit aligned access */
	if (addr & 0xF) {
		ret_val = QLA_FUNCTION_FAILED;
		goto exit_ms_mem_write;
	}
	write_lock_irqsave(&ha->hw_lock, flags);

	/* Write address */
	ret_val = qla8044_wr_reg_indirect(vha, MD_MIU_TEST_AGT_ADDR_HI, 0);
	if (ret_val == QLA_FUNCTION_FAILED) {
		ql_log(ql_log_fatal, vha, 0xb0a1,
		    "%s: write to AGT_ADDR_HI failed!\n", __func__);
		goto exit_ms_mem_write_unlock;
	}

	for (i = 0; i < count; i++, addr += 16) {
		if (!((QLA8044_ADDR_IN_RANGE(addr, QLA8044_ADDR_QDR_NET,
		    QLA8044_ADDR_QDR_NET_MAX)) ||
		    (QLA8044_ADDR_IN_RANGE(addr, QLA8044_ADDR_DDR_NET,
			QLA8044_ADDR_DDR_NET_MAX)))) {
			ret_val = QLA_FUNCTION_FAILED;
			goto exit_ms_mem_write_unlock;
		}

		ret_val = qla8044_wr_reg_indirect(vha,
		    MD_MIU_TEST_AGT_ADDR_LO, addr);

		/* Write data */
		ret_val += qla8044_wr_reg_indirect(vha,
		    MD_MIU_TEST_AGT_WRDATA_LO, *data++);
		ret_val += qla8044_wr_reg_indirect(vha,
		    MD_MIU_TEST_AGT_WRDATA_HI, *data++);
		ret_val += qla8044_wr_reg_indirect(vha,
		    MD_MIU_TEST_AGT_WRDATA_ULO, *data++);
		ret_val += qla8044_wr_reg_indirect(vha,
		    MD_MIU_TEST_AGT_WRDATA_UHI, *data++);
		if (ret_val == QLA_FUNCTION_FAILED) {
			ql_log(ql_log_fatal, vha, 0xb0a2,
			    "%s: write to AGT_WRDATA failed!\n",
			    __func__);
			goto exit_ms_mem_write_unlock;
		}

		/* Check write status */
		ret_val = qla8044_wr_reg_indirect(vha, MD_MIU_TEST_AGT_CTRL,
		    MIU_TA_CTL_WRITE_ENABLE);
		ret_val += qla8044_wr_reg_indirect(vha, MD_MIU_TEST_AGT_CTRL,
		    MIU_TA_CTL_WRITE_START);
		if (ret_val == QLA_FUNCTION_FAILED) {
			ql_log(ql_log_fatal, vha, 0xb0a3,
			    "%s: write to AGT_CTRL failed!\n", __func__);
			goto exit_ms_mem_write_unlock;
		}

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			ret_val = qla8044_rd_reg_indirect(vha,
			    MD_MIU_TEST_AGT_CTRL, &agt_ctrl);
			if (ret_val == QLA_FUNCTION_FAILED) {
				ql_log(ql_log_fatal, vha, 0xb0a4,
				    "%s: failed to read "
				    "MD_MIU_TEST_AGT_CTRL!\n", __func__);
				goto exit_ms_mem_write_unlock;
			}
			if ((agt_ctrl & MIU_TA_CTL_BUSY) == 0)
				break;
		}

		/* Status check failed */
		if (j >= MAX_CTL_CHECK) {
			ql_log(ql_log_fatal, vha, 0xb0a5,
			    "%s: MS memory write failed!\n",
			   __func__);
			ret_val = QLA_FUNCTION_FAILED;
			goto exit_ms_mem_write_unlock;
		}
	}

exit_ms_mem_write_unlock:
	write_unlock_irqrestore(&ha->hw_lock, flags);

exit_ms_mem_write:
	return ret_val;
}

static int
qla8044_copy_bootloader(struct scsi_qla_host *vha)
{
	uint8_t *p_cache;
	uint32_t src, count, size;
	uint64_t dest;
	int ret_val = QLA_SUCCESS;
	struct qla_hw_data *ha = vha->hw;

	src = QLA8044_BOOTLOADER_FLASH_ADDR;
	dest = qla8044_rd_reg(ha, QLA8044_BOOTLOADER_ADDR);
	size = qla8044_rd_reg(ha, QLA8044_BOOTLOADER_SIZE);

	/* 128 bit alignment check */
	if (size & 0xF)
		size = (size + 16) & ~0xF;

	/* 16 byte count */
	count = size/16;

	p_cache = vmalloc(size);
	if (p_cache == NULL) {
		ql_log(ql_log_fatal, vha, 0xb0a6,
		    "%s: Failed to allocate memory for "
		    "boot loader cache\n", __func__);
		ret_val = QLA_FUNCTION_FAILED;
		goto exit_copy_bootloader;
	}

	ret_val = qla8044_lockless_flash_read_u32(vha, src,
	    p_cache, size/sizeof(uint32_t));
	if (ret_val == QLA_FUNCTION_FAILED) {
		ql_log(ql_log_fatal, vha, 0xb0a7,
		    "%s: Error reading F/W from flash!!!\n", __func__);
		goto exit_copy_error;
	}
	ql_dbg(ql_dbg_p3p, vha, 0xb0a8, "%s: Read F/W from flash!\n",
	    __func__);

	/* 128 bit/16 byte write to MS memory */
	ret_val = qla8044_ms_mem_write_128b(vha, dest,
	    (uint32_t *)p_cache, count);
	if (ret_val == QLA_FUNCTION_FAILED) {
		ql_log(ql_log_fatal, vha, 0xb0a9,
		    "%s: Error writing F/W to MS !!!\n", __func__);
		goto exit_copy_error;
	}
	ql_dbg(ql_dbg_p3p, vha, 0xb0aa,
	    "%s: Wrote F/W (size %d) to MS !!!\n",
	    __func__, size);

exit_copy_error:
	vfree(p_cache);

exit_copy_bootloader:
	return ret_val;
}

static int
qla8044_restart(struct scsi_qla_host *vha)
{
	int ret_val = QLA_SUCCESS;
	struct qla_hw_data *ha = vha->hw;

	qla8044_process_stop_seq(vha);

	/* Collect minidump */
	if (ql2xmdenable)
		qla8044_get_minidump(vha);
	else
		ql_log(ql_log_fatal, vha, 0xb14c,
		    "Minidump disabled.\n");

	qla8044_process_init_seq(vha);

	if (qla8044_copy_bootloader(vha)) {
		ql_log(ql_log_fatal, vha, 0xb0ab,
		    "%s: Copy bootloader, firmware restart failed!\n",
		    __func__);
		ret_val = QLA_FUNCTION_FAILED;
		goto exit_restart;
	}

	/*
	 *  Loads F/W from flash
	 */
	qla8044_wr_reg(ha, QLA8044_FW_IMAGE_VALID, QLA8044_BOOT_FROM_FLASH);

	qla8044_process_start_seq(vha);

exit_restart:
	return ret_val;
}

/*
 * qla8044_check_cmd_peg_status - Check peg status to see if Peg is
 * initialized.
 *
 * @ha : Pointer to adapter structure
 *
 * Return Value - QLA_SUCCESS/QLA_FUNCTION_FAILED
 */
static int
qla8044_check_cmd_peg_status(struct scsi_qla_host *vha)
{
	uint32_t val, ret_val = QLA_FUNCTION_FAILED;
	int retries = CRB_CMDPEG_CHECK_RETRY_COUNT;
	struct qla_hw_data *ha = vha->hw;

	do {
		val = qla8044_rd_reg(ha, QLA8044_CMDPEG_STATE);
		if (val == PHAN_INITIALIZE_COMPLETE) {
			ql_dbg(ql_dbg_p3p, vha, 0xb0ac,
			    "%s: Command Peg initialization "
			    "complete! state=0x%x\n", __func__, val);
			ret_val = QLA_SUCCESS;
			break;
		}
		msleep(CRB_CMDPEG_CHECK_DELAY);
	} while (--retries);

	return ret_val;
}

static int
qla8044_start_firmware(struct scsi_qla_host *vha)
{
	int ret_val = QLA_SUCCESS;

	if (qla8044_restart(vha)) {
		ql_log(ql_log_fatal, vha, 0xb0ad,
		    "%s: Restart Error!!!, Need Reset!!!\n",
		    __func__);
		ret_val = QLA_FUNCTION_FAILED;
		goto exit_start_fw;
	} else
		ql_dbg(ql_dbg_p3p, vha, 0xb0af,
		    "%s: Restart done!\n", __func__);

	ret_val = qla8044_check_cmd_peg_status(vha);
	if (ret_val) {
		ql_log(ql_log_fatal, vha, 0xb0b0,
		    "%s: Peg not initialized!\n", __func__);
		ret_val = QLA_FUNCTION_FAILED;
	}

exit_start_fw:
	return ret_val;
}

void
qla8044_clear_drv_active(struct qla_hw_data *ha)
{
	uint32_t drv_active;
	struct scsi_qla_host *vha = pci_get_drvdata(ha->pdev);

	drv_active = qla8044_rd_direct(vha, QLA8044_CRB_DRV_ACTIVE_INDEX);
	drv_active &= ~(1 << (ha->portnum));

	ql_log(ql_log_info, vha, 0xb0b1,
	    "%s(%ld): drv_active: 0x%08x\n",
	    __func__, vha->host_no, drv_active);

	qla8044_wr_direct(vha, QLA8044_CRB_DRV_ACTIVE_INDEX, drv_active);
}

/*
 * qla8044_device_bootstrap - Initialize device, set DEV_READY, start fw
 * @ha: pointer to adapter structure
 *
 * Note: IDC lock must be held upon entry
 **/
static int
qla8044_device_bootstrap(struct scsi_qla_host *vha)
{
	int rval = QLA_FUNCTION_FAILED;
	int i;
	uint32_t old_count = 0, count = 0;
	int need_reset = 0;
	uint32_t idc_ctrl;
	struct qla_hw_data *ha = vha->hw;

	need_reset = qla8044_need_reset(vha);

	if (!need_reset) {
		old_count = qla8044_rd_direct(vha,
		    QLA8044_PEG_ALIVE_COUNTER_INDEX);

		for (i = 0; i < 10; i++) {
			msleep(200);

			count = qla8044_rd_direct(vha,
			    QLA8044_PEG_ALIVE_COUNTER_INDEX);
			if (count != old_count) {
				rval = QLA_SUCCESS;
				goto dev_ready;
			}
		}
		qla8044_flash_lock_recovery(vha);
	} else {
		/* We are trying to perform a recovery here. */
		if (ha->flags.isp82xx_fw_hung)
			qla8044_flash_lock_recovery(vha);
	}

	/* set to DEV_INITIALIZING */
	ql_log(ql_log_info, vha, 0xb0b2,
	    "%s: HW State: INITIALIZING\n", __func__);
	qla8044_wr_direct(vha, QLA8044_CRB_DEV_STATE_INDEX,
	    QLA8XXX_DEV_INITIALIZING);

	qla8044_idc_unlock(ha);
	rval = qla8044_start_firmware(vha);
	qla8044_idc_lock(ha);

	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_info, vha, 0xb0b3,
		     "%s: HW State: FAILED\n", __func__);
		qla8044_clear_drv_active(ha);
		qla8044_wr_direct(vha, QLA8044_CRB_DEV_STATE_INDEX,
		    QLA8XXX_DEV_FAILED);
		return rval;
	}

	/* For ISP8044, If IDC_CTRL GRACEFUL_RESET_BIT1 is set , reset it after
	 * device goes to INIT state. */
	idc_ctrl = qla8044_rd_reg(ha, QLA8044_IDC_DRV_CTRL);
	if (idc_ctrl & GRACEFUL_RESET_BIT1) {
		qla8044_wr_reg(ha, QLA8044_IDC_DRV_CTRL,
		    (idc_ctrl & ~GRACEFUL_RESET_BIT1));
		ha->fw_dumped = 0;
	}

dev_ready:
	ql_log(ql_log_info, vha, 0xb0b4,
	    "%s: HW State: READY\n", __func__);
	qla8044_wr_direct(vha, QLA8044_CRB_DEV_STATE_INDEX, QLA8XXX_DEV_READY);

	return rval;
}

/*-------------------------Reset Sequence Functions-----------------------*/
static void
qla8044_dump_reset_seq_hdr(struct scsi_qla_host *vha)
{
	u8 *phdr;

	if (!vha->reset_tmplt.buff) {
		ql_log(ql_log_fatal, vha, 0xb0b5,
		    "%s: Error Invalid reset_seq_template\n", __func__);
		return;
	}

	phdr = vha->reset_tmplt.buff;
	ql_dbg(ql_dbg_p3p, vha, 0xb0b6,
	    "Reset Template :\n\t0x%X 0x%X 0x%X 0x%X"
	    "0x%X 0x%X 0x%X 0x%X 0x%X 0x%X\n"
	    "\t0x%X 0x%X 0x%X 0x%X 0x%X 0x%X\n\n",
	    *phdr, *(phdr+1), *(phdr+2), *(phdr+3), *(phdr+4),
	    *(phdr+5), *(phdr+6), *(phdr+7), *(phdr + 8),
	    *(phdr+9), *(phdr+10), *(phdr+11), *(phdr+12),
	    *(phdr+13), *(phdr+14), *(phdr+15));
}

/*
 * qla8044_reset_seq_checksum_test - Validate Reset Sequence template.
 *
 * @ha : Pointer to adapter structure
 *
 * Return Value - QLA_SUCCESS/QLA_FUNCTION_FAILED
 */
static int
qla8044_reset_seq_checksum_test(struct scsi_qla_host *vha)
{
	uint32_t sum =  0;
	uint16_t *buff = (uint16_t *)vha->reset_tmplt.buff;
	int u16_count =  vha->reset_tmplt.hdr->size / sizeof(uint16_t);

	while (u16_count-- > 0)
		sum += *buff++;

	while (sum >> 16)
		sum = (sum & 0xFFFF) +  (sum >> 16);

	/* checksum of 0 indicates a valid template */
	if (~sum) {
		return QLA_SUCCESS;
	} else {
		ql_log(ql_log_fatal, vha, 0xb0b7,
		    "%s: Reset seq checksum failed\n", __func__);
		return QLA_FUNCTION_FAILED;
	}
}

/*
 * qla8044_read_reset_template - Read Reset Template from Flash, validate
 * the template and store offsets of stop/start/init offsets in ha->reset_tmplt.
 *
 * @ha : Pointer to adapter structure
 */
void
qla8044_read_reset_template(struct scsi_qla_host *vha)
{
	uint8_t *p_buff;
	uint32_t addr, tmplt_hdr_def_size, tmplt_hdr_size;

	vha->reset_tmplt.seq_error = 0;
	vha->reset_tmplt.buff = vmalloc(QLA8044_RESTART_TEMPLATE_SIZE);
	if (vha->reset_tmplt.buff == NULL) {
		ql_log(ql_log_fatal, vha, 0xb0b8,
		    "%s: Failed to allocate reset template resources\n",
		    __func__);
		goto exit_read_reset_template;
	}

	p_buff = vha->reset_tmplt.buff;
	addr = QLA8044_RESET_TEMPLATE_ADDR;

	tmplt_hdr_def_size =
	    sizeof(struct qla8044_reset_template_hdr) / sizeof(uint32_t);

	ql_dbg(ql_dbg_p3p, vha, 0xb0b9,
	    "%s: Read template hdr size %d from Flash\n",
	    __func__, tmplt_hdr_def_size);

	/* Copy template header from flash */
	if (qla8044_read_flash_data(vha, p_buff, addr, tmplt_hdr_def_size)) {
		ql_log(ql_log_fatal, vha, 0xb0ba,
		    "%s: Failed to read reset template\n", __func__);
		goto exit_read_template_error;
	}

	vha->reset_tmplt.hdr =
	 (struct qla8044_reset_template_hdr *) vha->reset_tmplt.buff;

	/* Validate the template header size and signature */
	tmplt_hdr_size = vha->reset_tmplt.hdr->hdr_size/sizeof(uint32_t);
	if ((tmplt_hdr_size != tmplt_hdr_def_size) ||
	    (vha->reset_tmplt.hdr->signature != RESET_TMPLT_HDR_SIGNATURE)) {
		ql_log(ql_log_fatal, vha, 0xb0bb,
		    "%s: Template Header size invalid %d "
		    "tmplt_hdr_def_size %d!!!\n", __func__,
		    tmplt_hdr_size, tmplt_hdr_def_size);
		goto exit_read_template_error;
	}

	addr = QLA8044_RESET_TEMPLATE_ADDR + vha->reset_tmplt.hdr->hdr_size;
	p_buff = vha->reset_tmplt.buff + vha->reset_tmplt.hdr->hdr_size;
	tmplt_hdr_def_size = (vha->reset_tmplt.hdr->size -
	    vha->reset_tmplt.hdr->hdr_size)/sizeof(uint32_t);

	ql_dbg(ql_dbg_p3p, vha, 0xb0bc,
	    "%s: Read rest of the template size %d\n",
	    __func__, vha->reset_tmplt.hdr->size);

	/* Copy rest of the template */
	if (qla8044_read_flash_data(vha, p_buff, addr, tmplt_hdr_def_size)) {
		ql_log(ql_log_fatal, vha, 0xb0bd,
		    "%s: Failed to read reset tempelate\n", __func__);
		goto exit_read_template_error;
	}

	/* Integrity check */
	if (qla8044_reset_seq_checksum_test(vha)) {
		ql_log(ql_log_fatal, vha, 0xb0be,
		    "%s: Reset Seq checksum failed!\n", __func__);
		goto exit_read_template_error;
	}

	ql_dbg(ql_dbg_p3p, vha, 0xb0bf,
	    "%s: Reset Seq checksum passed! Get stop, "
	    "start and init seq offsets\n", __func__);

	/* Get STOP, START, INIT sequence offsets */
	vha->reset_tmplt.init_offset = vha->reset_tmplt.buff +
	    vha->reset_tmplt.hdr->init_seq_offset;

	vha->reset_tmplt.start_offset = vha->reset_tmplt.buff +
	    vha->reset_tmplt.hdr->start_seq_offset;

	vha->reset_tmplt.stop_offset = vha->reset_tmplt.buff +
	    vha->reset_tmplt.hdr->hdr_size;

	qla8044_dump_reset_seq_hdr(vha);

	goto exit_read_reset_template;

exit_read_template_error:
	vfree(vha->reset_tmplt.buff);

exit_read_reset_template:
	return;
}

void
qla8044_set_idc_dontreset(struct scsi_qla_host *vha)
{
	uint32_t idc_ctrl;
	struct qla_hw_data *ha = vha->hw;

	idc_ctrl = qla8044_rd_reg(ha, QLA8044_IDC_DRV_CTRL);
	idc_ctrl |= DONTRESET_BIT0;
	ql_dbg(ql_dbg_p3p, vha, 0xb0c0,
	    "%s: idc_ctrl = %d\n", __func__, idc_ctrl);
	qla8044_wr_reg(ha, QLA8044_IDC_DRV_CTRL, idc_ctrl);
}

inline void
qla8044_set_rst_ready(struct scsi_qla_host *vha)
{
	uint32_t drv_state;
	struct qla_hw_data *ha = vha->hw;

	drv_state = qla8044_rd_direct(vha, QLA8044_CRB_DRV_STATE_INDEX);

	/* For ISP8044, drv_active register has 1 bit per function,
	 * shift 1 by func_num to set a bit for the function.*/
	drv_state |= (1 << ha->portnum);

	ql_log(ql_log_info, vha, 0xb0c1,
	    "%s(%ld): drv_state: 0x%08x\n",
	    __func__, vha->host_no, drv_state);
	qla8044_wr_direct(vha, QLA8044_CRB_DRV_STATE_INDEX, drv_state);
}

/**
 * qla8044_need_reset_handler - Code to start reset sequence
 * @ha: pointer to adapter structure
 *
 * Note: IDC lock must be held upon entry
 **/
static void
qla8044_need_reset_handler(struct scsi_qla_host *vha)
{
	uint32_t dev_state = 0, drv_state, drv_active;
	unsigned long reset_timeout;
	struct qla_hw_data *ha = vha->hw;

	ql_log(ql_log_fatal, vha, 0xb0c2,
	    "%s: Performing ISP error recovery\n", __func__);

	if (vha->flags.online) {
		qla8044_idc_unlock(ha);
		qla2x00_abort_isp_cleanup(vha);
		ha->isp_ops->get_flash_version(vha, vha->req->ring);
		ha->isp_ops->nvram_config(vha);
		qla8044_idc_lock(ha);
	}

	dev_state = qla8044_rd_direct(vha,
	    QLA8044_CRB_DEV_STATE_INDEX);
	drv_state = qla8044_rd_direct(vha,
	    QLA8044_CRB_DRV_STATE_INDEX);
	drv_active = qla8044_rd_direct(vha,
	    QLA8044_CRB_DRV_ACTIVE_INDEX);

	ql_log(ql_log_info, vha, 0xb0c5,
	    "%s(%ld): drv_state = 0x%x, drv_active = 0x%x dev_state = 0x%x\n",
	    __func__, vha->host_no, drv_state, drv_active, dev_state);

	qla8044_set_rst_ready(vha);

	/* wait for 10 seconds for reset ack from all functions */
	reset_timeout = jiffies + (ha->fcoe_reset_timeout * HZ);

	do {
		if (time_after_eq(jiffies, reset_timeout)) {
			ql_log(ql_log_info, vha, 0xb0c4,
			    "%s: Function %d: Reset Ack Timeout!, drv_state: 0x%08x, drv_active: 0x%08x\n",
			    __func__, ha->portnum, drv_state, drv_active);
			break;
		}

		qla8044_idc_unlock(ha);
		msleep(1000);
		qla8044_idc_lock(ha);

		dev_state = qla8044_rd_direct(vha,
		    QLA8044_CRB_DEV_STATE_INDEX);
		drv_state = qla8044_rd_direct(vha,
		    QLA8044_CRB_DRV_STATE_INDEX);
		drv_active = qla8044_rd_direct(vha,
		    QLA8044_CRB_DRV_ACTIVE_INDEX);
	} while (((drv_state & drv_active) != drv_active) &&
	    (dev_state == QLA8XXX_DEV_NEED_RESET));

	/* Remove IDC participation of functions not acknowledging */
	if (drv_state != drv_active) {
		ql_log(ql_log_info, vha, 0xb0c7,
		    "%s(%ld): Function %d turning off drv_active of non-acking function 0x%x\n",
		    __func__, vha->host_no, ha->portnum,
		    (drv_active ^ drv_state));
		drv_active = drv_active & drv_state;
		qla8044_wr_direct(vha, QLA8044_CRB_DRV_ACTIVE_INDEX,
		    drv_active);
	} else {
		/*
		 * Reset owner should execute reset recovery,
		 * if all functions acknowledged
		 */
		if ((ha->flags.nic_core_reset_owner) &&
		    (dev_state == QLA8XXX_DEV_NEED_RESET)) {
			ha->flags.nic_core_reset_owner = 0;
			qla8044_device_bootstrap(vha);
			return;
		}
	}

	/* Exit if non active function */
	if (!(drv_active & (1 << ha->portnum))) {
		ha->flags.nic_core_reset_owner = 0;
		return;
	}

	/*
	 * Execute Reset Recovery if Reset Owner or Function 7
	 * is the only active function
	 */
	if (ha->flags.nic_core_reset_owner ||
	    ((drv_state & drv_active) == QLA8044_FUN7_ACTIVE_INDEX)) {
		ha->flags.nic_core_reset_owner = 0;
		qla8044_device_bootstrap(vha);
	}
}

static void
qla8044_set_drv_active(struct scsi_qla_host *vha)
{
	uint32_t drv_active;
	struct qla_hw_data *ha = vha->hw;

	drv_active = qla8044_rd_direct(vha, QLA8044_CRB_DRV_ACTIVE_INDEX);

	/* For ISP8044, drv_active register has 1 bit per function,
	 * shift 1 by func_num to set a bit for the function.*/
	drv_active |= (1 << ha->portnum);

	ql_log(ql_log_info, vha, 0xb0c8,
	    "%s(%ld): drv_active: 0x%08x\n",
	    __func__, vha->host_no, drv_active);
	qla8044_wr_direct(vha, QLA8044_CRB_DRV_ACTIVE_INDEX, drv_active);
}

static int
qla8044_check_drv_active(struct scsi_qla_host *vha)
{
	uint32_t drv_active;
	struct qla_hw_data *ha = vha->hw;

	drv_active = qla8044_rd_direct(vha, QLA8044_CRB_DRV_ACTIVE_INDEX);
	if (drv_active & (1 << ha->portnum))
		return QLA_SUCCESS;
	else
		return QLA_TEST_FAILED;
}

static void
qla8044_clear_idc_dontreset(struct scsi_qla_host *vha)
{
	uint32_t idc_ctrl;
	struct qla_hw_data *ha = vha->hw;

	idc_ctrl = qla8044_rd_reg(ha, QLA8044_IDC_DRV_CTRL);
	idc_ctrl &= ~DONTRESET_BIT0;
	ql_log(ql_log_info, vha, 0xb0c9,
	    "%s: idc_ctrl = %d\n", __func__,
	    idc_ctrl);
	qla8044_wr_reg(ha, QLA8044_IDC_DRV_CTRL, idc_ctrl);
}

static int
qla8044_set_idc_ver(struct scsi_qla_host *vha)
{
	int idc_ver;
	uint32_t drv_active;
	int rval = QLA_SUCCESS;
	struct qla_hw_data *ha = vha->hw;

	drv_active = qla8044_rd_direct(vha, QLA8044_CRB_DRV_ACTIVE_INDEX);
	if (drv_active == (1 << ha->portnum)) {
		idc_ver = qla8044_rd_direct(vha,
		    QLA8044_CRB_DRV_IDC_VERSION_INDEX);
		idc_ver &= (~0xFF);
		idc_ver |= QLA8044_IDC_VER_MAJ_VALUE;
		qla8044_wr_direct(vha, QLA8044_CRB_DRV_IDC_VERSION_INDEX,
		    idc_ver);
		ql_log(ql_log_info, vha, 0xb0ca,
		    "%s: IDC version updated to %d\n",
		    __func__, idc_ver);
	} else {
		idc_ver = qla8044_rd_direct(vha,
		    QLA8044_CRB_DRV_IDC_VERSION_INDEX);
		idc_ver &= 0xFF;
		if (QLA8044_IDC_VER_MAJ_VALUE != idc_ver) {
			ql_log(ql_log_info, vha, 0xb0cb,
			    "%s: qla4xxx driver IDC version %d "
			    "is not compatible with IDC version %d "
			    "of other drivers!\n",
			    __func__, QLA8044_IDC_VER_MAJ_VALUE,
			    idc_ver);
			rval = QLA_FUNCTION_FAILED;
			goto exit_set_idc_ver;
		}
	}

	/* Update IDC_MINOR_VERSION */
	idc_ver = qla8044_rd_reg(ha, QLA8044_CRB_IDC_VER_MINOR);
	idc_ver &= ~(0x03 << (ha->portnum * 2));
	idc_ver |= (QLA8044_IDC_VER_MIN_VALUE << (ha->portnum * 2));
	qla8044_wr_reg(ha, QLA8044_CRB_IDC_VER_MINOR, idc_ver);

exit_set_idc_ver:
	return rval;
}

static int
qla8044_update_idc_reg(struct scsi_qla_host *vha)
{
	uint32_t drv_active;
	int rval = QLA_SUCCESS;
	struct qla_hw_data *ha = vha->hw;

	if (vha->flags.init_done)
		goto exit_update_idc_reg;

	qla8044_idc_lock(ha);
	qla8044_set_drv_active(vha);

	drv_active = qla8044_rd_direct(vha,
	    QLA8044_CRB_DRV_ACTIVE_INDEX);

	/* If we are the first driver to load and
	 * ql2xdontresethba is not set, clear IDC_CTRL BIT0. */
	if ((drv_active == (1 << ha->portnum)) && !ql2xdontresethba)
		qla8044_clear_idc_dontreset(vha);

	rval = qla8044_set_idc_ver(vha);
	if (rval == QLA_FUNCTION_FAILED)
		qla8044_clear_drv_active(ha);
	qla8044_idc_unlock(ha);

exit_update_idc_reg:
	return rval;
}

/**
 * qla8044_need_qsnt_handler - Code to start qsnt
 * @ha: pointer to adapter structure
 **/
static void
qla8044_need_qsnt_handler(struct scsi_qla_host *vha)
{
	unsigned long qsnt_timeout;
	uint32_t drv_state, drv_active, dev_state;
	struct qla_hw_data *ha = vha->hw;

	if (vha->flags.online)
		qla2x00_quiesce_io(vha);
	else
		return;

	qla8044_set_qsnt_ready(vha);

	/* Wait for 30 secs for all functions to ack qsnt mode */
	qsnt_timeout = jiffies + (QSNT_ACK_TOV * HZ);
	drv_state = qla8044_rd_direct(vha, QLA8044_CRB_DRV_STATE_INDEX);
	drv_active = qla8044_rd_direct(vha, QLA8044_CRB_DRV_ACTIVE_INDEX);

	/* Shift drv_active by 1 to match drv_state. As quiescent ready bit
	   position is at bit 1 and drv active is at bit 0 */
	drv_active = drv_active << 1;

	while (drv_state != drv_active) {
		if (time_after_eq(jiffies, qsnt_timeout)) {
			/* Other functions did not ack, changing state to
			 * DEV_READY
			 */
			clear_bit(ISP_QUIESCE_NEEDED, &vha->dpc_flags);
			qla8044_wr_direct(vha, QLA8044_CRB_DEV_STATE_INDEX,
					    QLA8XXX_DEV_READY);
			qla8044_clear_qsnt_ready(vha);
			ql_log(ql_log_info, vha, 0xb0cc,
			    "Timeout waiting for quiescent ack!!!\n");
			return;
		}
		qla8044_idc_unlock(ha);
		msleep(1000);
		qla8044_idc_lock(ha);

		drv_state = qla8044_rd_direct(vha,
		    QLA8044_CRB_DRV_STATE_INDEX);
		drv_active = qla8044_rd_direct(vha,
		    QLA8044_CRB_DRV_ACTIVE_INDEX);
		drv_active = drv_active << 1;
	}

	/* All functions have Acked. Set quiescent state */
	dev_state = qla8044_rd_direct(vha, QLA8044_CRB_DEV_STATE_INDEX);

	if (dev_state == QLA8XXX_DEV_NEED_QUIESCENT) {
		qla8044_wr_direct(vha, QLA8044_CRB_DEV_STATE_INDEX,
		    QLA8XXX_DEV_QUIESCENT);
		ql_log(ql_log_info, vha, 0xb0cd,
		    "%s: HW State: QUIESCENT\n", __func__);
	}
}

/*
 * qla8044_device_state_handler - Adapter state machine
 * @ha: pointer to host adapter structure.
 *
 * Note: IDC lock must be UNLOCKED upon entry
 **/
int
qla8044_device_state_handler(struct scsi_qla_host *vha)
{
	uint32_t dev_state;
	int rval = QLA_SUCCESS;
	unsigned long dev_init_timeout;
	struct qla_hw_data *ha = vha->hw;

	rval = qla8044_update_idc_reg(vha);
	if (rval == QLA_FUNCTION_FAILED)
		goto exit_error;

	dev_state = qla8044_rd_direct(vha, QLA8044_CRB_DEV_STATE_INDEX);
	ql_dbg(ql_dbg_p3p, vha, 0xb0ce,
	    "Device state is 0x%x = %s\n",
	    dev_state, dev_state < MAX_STATES ?
	    qdev_state(dev_state) : "Unknown");

	/* wait for 30 seconds for device to go ready */
	dev_init_timeout = jiffies + (ha->fcoe_dev_init_timeout * HZ);

	qla8044_idc_lock(ha);

	while (1) {
		if (time_after_eq(jiffies, dev_init_timeout)) {
			if (qla8044_check_drv_active(vha) == QLA_SUCCESS) {
				ql_log(ql_log_warn, vha, 0xb0cf,
				    "%s: Device Init Failed 0x%x = %s\n",
				    QLA2XXX_DRIVER_NAME, dev_state,
				    dev_state < MAX_STATES ?
				    qdev_state(dev_state) : "Unknown");
				qla8044_wr_direct(vha,
				    QLA8044_CRB_DEV_STATE_INDEX,
				    QLA8XXX_DEV_FAILED);
			}
		}

		dev_state = qla8044_rd_direct(vha, QLA8044_CRB_DEV_STATE_INDEX);
		ql_log(ql_log_info, vha, 0xb0d0,
		    "Device state is 0x%x = %s\n",
		    dev_state, dev_state < MAX_STATES ?
		    qdev_state(dev_state) : "Unknown");

		/* NOTE: Make sure idc unlocked upon exit of switch statement */
		switch (dev_state) {
		case QLA8XXX_DEV_READY:
			ha->flags.nic_core_reset_owner = 0;
			goto exit;
		case QLA8XXX_DEV_COLD:
			rval = qla8044_device_bootstrap(vha);
			break;
		case QLA8XXX_DEV_INITIALIZING:
			qla8044_idc_unlock(ha);
			msleep(1000);
			qla8044_idc_lock(ha);
			break;
		case QLA8XXX_DEV_NEED_RESET:
			/* For ISP8044, if NEED_RESET is set by any driver,
			 * it should be honored, irrespective of IDC_CTRL
			 * DONTRESET_BIT0 */
			qla8044_need_reset_handler(vha);
			break;
		case QLA8XXX_DEV_NEED_QUIESCENT:
			/* idc locked/unlocked in handler */
			qla8044_need_qsnt_handler(vha);

			/* Reset the init timeout after qsnt handler */
			dev_init_timeout = jiffies +
			    (ha->fcoe_reset_timeout * HZ);
			break;
		case QLA8XXX_DEV_QUIESCENT:
			ql_log(ql_log_info, vha, 0xb0d1,
			    "HW State: QUIESCENT\n");

			qla8044_idc_unlock(ha);
			msleep(1000);
			qla8044_idc_lock(ha);

			/* Reset the init timeout after qsnt handler */
			dev_init_timeout = jiffies +
			    (ha->fcoe_reset_timeout * HZ);
			break;
		case QLA8XXX_DEV_FAILED:
			ha->flags.nic_core_reset_owner = 0;
			qla8044_idc_unlock(ha);
			qla8xxx_dev_failed_handler(vha);
			rval = QLA_FUNCTION_FAILED;
			qla8044_idc_lock(ha);
			goto exit;
		default:
			qla8044_idc_unlock(ha);
			qla8xxx_dev_failed_handler(vha);
			rval = QLA_FUNCTION_FAILED;
			qla8044_idc_lock(ha);
			goto exit;
		}
	}
exit:
	qla8044_idc_unlock(ha);

exit_error:
	return rval;
}

/**
 * qla4_8xxx_check_temp - Check the ISP82XX temperature.
 * @ha: adapter block pointer.
 *
 * Note: The caller should not hold the idc lock.
 **/
static int
qla8044_check_temp(struct scsi_qla_host *vha)
{
	uint32_t temp, temp_state, temp_val;
	int status = QLA_SUCCESS;

	temp = qla8044_rd_direct(vha, QLA8044_CRB_TEMP_STATE_INDEX);
	temp_state = qla82xx_get_temp_state(temp);
	temp_val = qla82xx_get_temp_val(temp);

	if (temp_state == QLA82XX_TEMP_PANIC) {
		ql_log(ql_log_warn, vha, 0xb0d2,
		    "Device temperature %d degrees C"
		    " exceeds maximum allowed. Hardware has been shut"
		    " down\n", temp_val);
		status = QLA_FUNCTION_FAILED;
		return status;
	} else if (temp_state == QLA82XX_TEMP_WARN) {
		ql_log(ql_log_warn, vha, 0xb0d3,
		    "Device temperature %d"
		    " degrees C exceeds operating range."
		    " Immediate action needed.\n", temp_val);
	}
	return 0;
}

int qla8044_read_temperature(scsi_qla_host_t *vha)
{
	uint32_t temp;

	temp = qla8044_rd_direct(vha, QLA8044_CRB_TEMP_STATE_INDEX);
	return qla82xx_get_temp_val(temp);
}

/**
 * qla8044_check_fw_alive  - Check firmware health
 * @ha: Pointer to host adapter structure.
 *
 * Context: Interrupt
 **/
int
qla8044_check_fw_alive(struct scsi_qla_host *vha)
{
	uint32_t fw_heartbeat_counter;
	uint32_t halt_status1, halt_status2;
	int status = QLA_SUCCESS;

	fw_heartbeat_counter = qla8044_rd_direct(vha,
	    QLA8044_PEG_ALIVE_COUNTER_INDEX);

	/* If PEG_ALIVE_COUNTER is 0xffffffff, AER/EEH is in progress, ignore */
	if (fw_heartbeat_counter == 0xffffffff) {
		ql_dbg(ql_dbg_p3p, vha, 0xb0d4,
		    "scsi%ld: %s: Device in frozen "
		    "state, QLA82XX_PEG_ALIVE_COUNTER is 0xffffffff\n",
		    vha->host_no, __func__);
		return status;
	}

	if (vha->fw_heartbeat_counter == fw_heartbeat_counter) {
		vha->seconds_since_last_heartbeat++;
		/* FW not alive after 2 seconds */
		if (vha->seconds_since_last_heartbeat == 2) {
			vha->seconds_since_last_heartbeat = 0;
			halt_status1 = qla8044_rd_direct(vha,
			    QLA8044_PEG_HALT_STATUS1_INDEX);
			halt_status2 = qla8044_rd_direct(vha,
			    QLA8044_PEG_HALT_STATUS2_INDEX);

			ql_log(ql_log_info, vha, 0xb0d5,
			    "scsi(%ld): %s, ISP8044 "
			    "Dumping hw/fw registers:\n"
			    " PEG_HALT_STATUS1: 0x%x, "
			    "PEG_HALT_STATUS2: 0x%x,\n",
			    vha->host_no, __func__, halt_status1,
			    halt_status2);
			status = QLA_FUNCTION_FAILED;
		}
	} else
		vha->seconds_since_last_heartbeat = 0;

	vha->fw_heartbeat_counter = fw_heartbeat_counter;
	return status;
}

void
qla8044_watchdog(struct scsi_qla_host *vha)
{
	uint32_t dev_state, halt_status;
	int halt_status_unrecoverable = 0;
	struct qla_hw_data *ha = vha->hw;

	/* don't poll if reset is going on or FW hang in quiescent state */
	if (!(test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags) ||
	    test_bit(FCOE_CTX_RESET_NEEDED, &vha->dpc_flags))) {
		dev_state = qla8044_rd_direct(vha, QLA8044_CRB_DEV_STATE_INDEX);

		if (qla8044_check_fw_alive(vha)) {
			ha->flags.isp82xx_fw_hung = 1;
			ql_log(ql_log_warn, vha, 0xb10a,
			    "Firmware hung.\n");
			qla82xx_clear_pending_mbx(vha);
		}

		if (qla8044_check_temp(vha)) {
			set_bit(ISP_UNRECOVERABLE, &vha->dpc_flags);
			ha->flags.isp82xx_fw_hung = 1;
			qla2xxx_wake_dpc(vha);
		} else if (dev_state == QLA8XXX_DEV_NEED_RESET &&
			   !test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags)) {
			ql_log(ql_log_info, vha, 0xb0d6,
			    "%s: HW State: NEED RESET!\n",
			    __func__);
			set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
			qla2xxx_wake_dpc(vha);
		} else if (dev_state == QLA8XXX_DEV_NEED_QUIESCENT &&
		    !test_bit(ISP_QUIESCE_NEEDED, &vha->dpc_flags)) {
			ql_log(ql_log_info, vha, 0xb0d7,
			    "%s: HW State: NEED QUIES detected!\n",
			    __func__);
			set_bit(ISP_QUIESCE_NEEDED, &vha->dpc_flags);
			qla2xxx_wake_dpc(vha);
		} else  {
			/* Check firmware health */
			if (ha->flags.isp82xx_fw_hung) {
				halt_status = qla8044_rd_direct(vha,
					QLA8044_PEG_HALT_STATUS1_INDEX);
				if (halt_status &
				    QLA8044_HALT_STATUS_FW_RESET) {
					ql_log(ql_log_fatal, vha,
					    0xb0d8, "%s: Firmware "
					    "error detected device "
					    "is being reset\n",
					    __func__);
				} else if (halt_status &
					    QLA8044_HALT_STATUS_UNRECOVERABLE) {
						halt_status_unrecoverable = 1;
				}

				/* Since we cannot change dev_state in interrupt
				 * context, set appropriate DPC flag then wakeup
				 *  DPC */
				if (halt_status_unrecoverable) {
					set_bit(ISP_UNRECOVERABLE,
					    &vha->dpc_flags);
				} else {
					if (dev_state ==
					    QLA8XXX_DEV_QUIESCENT) {
						set_bit(FCOE_CTX_RESET_NEEDED,
						    &vha->dpc_flags);
						ql_log(ql_log_info, vha, 0xb0d9,
						    "%s: FW CONTEXT Reset "
						    "needed!\n", __func__);
					} else {
						ql_log(ql_log_info, vha,
						    0xb0da, "%s: "
						    "detect abort needed\n",
						    __func__);
						set_bit(ISP_ABORT_NEEDED,
						    &vha->dpc_flags);
					}
				}
				qla2xxx_wake_dpc(vha);
			}
		}

	}
}

static int
qla8044_minidump_process_control(struct scsi_qla_host *vha,
				 struct qla8044_minidump_entry_hdr *entry_hdr)
{
	struct qla8044_minidump_entry_crb *crb_entry;
	uint32_t read_value, opcode, poll_time, addr, index;
	uint32_t crb_addr, rval = QLA_SUCCESS;
	unsigned long wtime;
	struct qla8044_minidump_template_hdr *tmplt_hdr;
	int i;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_p3p, vha, 0xb0dd, "Entering fn: %s\n", __func__);
	tmplt_hdr = (struct qla8044_minidump_template_hdr *)
		ha->md_tmplt_hdr;
	crb_entry = (struct qla8044_minidump_entry_crb *)entry_hdr;

	crb_addr = crb_entry->addr;
	for (i = 0; i < crb_entry->op_count; i++) {
		opcode = crb_entry->crb_ctrl.opcode;

		if (opcode & QLA82XX_DBG_OPCODE_WR) {
			qla8044_wr_reg_indirect(vha, crb_addr,
			    crb_entry->value_1);
			opcode &= ~QLA82XX_DBG_OPCODE_WR;
		}

		if (opcode & QLA82XX_DBG_OPCODE_RW) {
			qla8044_rd_reg_indirect(vha, crb_addr, &read_value);
			qla8044_wr_reg_indirect(vha, crb_addr, read_value);
			opcode &= ~QLA82XX_DBG_OPCODE_RW;
		}

		if (opcode & QLA82XX_DBG_OPCODE_AND) {
			qla8044_rd_reg_indirect(vha, crb_addr, &read_value);
			read_value &= crb_entry->value_2;
			opcode &= ~QLA82XX_DBG_OPCODE_AND;
			if (opcode & QLA82XX_DBG_OPCODE_OR) {
				read_value |= crb_entry->value_3;
				opcode &= ~QLA82XX_DBG_OPCODE_OR;
			}
			qla8044_wr_reg_indirect(vha, crb_addr, read_value);
		}
		if (opcode & QLA82XX_DBG_OPCODE_OR) {
			qla8044_rd_reg_indirect(vha, crb_addr, &read_value);
			read_value |= crb_entry->value_3;
			qla8044_wr_reg_indirect(vha, crb_addr, read_value);
			opcode &= ~QLA82XX_DBG_OPCODE_OR;
		}
		if (opcode & QLA82XX_DBG_OPCODE_POLL) {
			poll_time = crb_entry->crb_strd.poll_timeout;
			wtime = jiffies + poll_time;
			qla8044_rd_reg_indirect(vha, crb_addr, &read_value);

			do {
				if ((read_value & crb_entry->value_2) ==
				    crb_entry->value_1) {
					break;
				} else if (time_after_eq(jiffies, wtime)) {
					/* capturing dump failed */
					rval = QLA_FUNCTION_FAILED;
					break;
				} else {
					qla8044_rd_reg_indirect(vha,
					    crb_addr, &read_value);
				}
			} while (1);
			opcode &= ~QLA82XX_DBG_OPCODE_POLL;
		}

		if (opcode & QLA82XX_DBG_OPCODE_RDSTATE) {
			if (crb_entry->crb_strd.state_index_a) {
				index = crb_entry->crb_strd.state_index_a;
				addr = tmplt_hdr->saved_state_array[index];
			} else {
				addr = crb_addr;
			}

			qla8044_rd_reg_indirect(vha, addr, &read_value);
			index = crb_entry->crb_ctrl.state_index_v;
			tmplt_hdr->saved_state_array[index] = read_value;
			opcode &= ~QLA82XX_DBG_OPCODE_RDSTATE;
		}

		if (opcode & QLA82XX_DBG_OPCODE_WRSTATE) {
			if (crb_entry->crb_strd.state_index_a) {
				index = crb_entry->crb_strd.state_index_a;
				addr = tmplt_hdr->saved_state_array[index];
			} else {
				addr = crb_addr;
			}

			if (crb_entry->crb_ctrl.state_index_v) {
				index = crb_entry->crb_ctrl.state_index_v;
				read_value =
				    tmplt_hdr->saved_state_array[index];
			} else {
				read_value = crb_entry->value_1;
			}

			qla8044_wr_reg_indirect(vha, addr, read_value);
			opcode &= ~QLA82XX_DBG_OPCODE_WRSTATE;
		}

		if (opcode & QLA82XX_DBG_OPCODE_MDSTATE) {
			index = crb_entry->crb_ctrl.state_index_v;
			read_value = tmplt_hdr->saved_state_array[index];
			read_value <<= crb_entry->crb_ctrl.shl;
			read_value >>= crb_entry->crb_ctrl.shr;
			if (crb_entry->value_2)
				read_value &= crb_entry->value_2;
			read_value |= crb_entry->value_3;
			read_value += crb_entry->value_1;
			tmplt_hdr->saved_state_array[index] = read_value;
			opcode &= ~QLA82XX_DBG_OPCODE_MDSTATE;
		}
		crb_addr += crb_entry->crb_strd.addr_stride;
	}
	return rval;
}

static void
qla8044_minidump_process_rdcrb(struct scsi_qla_host *vha,
	struct qla8044_minidump_entry_hdr *entry_hdr, uint32_t **d_ptr)
{
	uint32_t r_addr, r_stride, loop_cnt, i, r_value;
	struct qla8044_minidump_entry_crb *crb_hdr;
	uint32_t *data_ptr = *d_ptr;

	ql_dbg(ql_dbg_p3p, vha, 0xb0de, "Entering fn: %s\n", __func__);
	crb_hdr = (struct qla8044_minidump_entry_crb *)entry_hdr;
	r_addr = crb_hdr->addr;
	r_stride = crb_hdr->crb_strd.addr_stride;
	loop_cnt = crb_hdr->op_count;

	for (i = 0; i < loop_cnt; i++) {
		qla8044_rd_reg_indirect(vha, r_addr, &r_value);
		*data_ptr++ = r_addr;
		*data_ptr++ = r_value;
		r_addr += r_stride;
	}
	*d_ptr = data_ptr;
}

static int
qla8044_minidump_process_rdmem(struct scsi_qla_host *vha,
	struct qla8044_minidump_entry_hdr *entry_hdr, uint32_t **d_ptr)
{
	uint32_t r_addr, r_value, r_data;
	uint32_t i, j, loop_cnt;
	struct qla8044_minidump_entry_rdmem *m_hdr;
	unsigned long flags;
	uint32_t *data_ptr = *d_ptr;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_p3p, vha, 0xb0df, "Entering fn: %s\n", __func__);
	m_hdr = (struct qla8044_minidump_entry_rdmem *)entry_hdr;
	r_addr = m_hdr->read_addr;
	loop_cnt = m_hdr->read_data_size/16;

	ql_dbg(ql_dbg_p3p, vha, 0xb0f0,
	    "[%s]: Read addr: 0x%x, read_data_size: 0x%x\n",
	    __func__, r_addr, m_hdr->read_data_size);

	if (r_addr & 0xf) {
		ql_dbg(ql_dbg_p3p, vha, 0xb0f1,
		    "[%s]: Read addr 0x%x not 16 bytes aligned\n",
		    __func__, r_addr);
		return QLA_FUNCTION_FAILED;
	}

	if (m_hdr->read_data_size % 16) {
		ql_dbg(ql_dbg_p3p, vha, 0xb0f2,
		    "[%s]: Read data[0x%x] not multiple of 16 bytes\n",
		    __func__, m_hdr->read_data_size);
		return QLA_FUNCTION_FAILED;
	}

	ql_dbg(ql_dbg_p3p, vha, 0xb0f3,
	    "[%s]: rdmem_addr: 0x%x, read_data_size: 0x%x, loop_cnt: 0x%x\n",
	    __func__, r_addr, m_hdr->read_data_size, loop_cnt);

	write_lock_irqsave(&ha->hw_lock, flags);
	for (i = 0; i < loop_cnt; i++) {
		qla8044_wr_reg_indirect(vha, MD_MIU_TEST_AGT_ADDR_LO, r_addr);
		r_value = 0;
		qla8044_wr_reg_indirect(vha, MD_MIU_TEST_AGT_ADDR_HI, r_value);
		r_value = MIU_TA_CTL_ENABLE;
		qla8044_wr_reg_indirect(vha, MD_MIU_TEST_AGT_CTRL, r_value);
		r_value = MIU_TA_CTL_START_ENABLE;
		qla8044_wr_reg_indirect(vha, MD_MIU_TEST_AGT_CTRL, r_value);

		for (j = 0; j < MAX_CTL_CHECK; j++) {
			qla8044_rd_reg_indirect(vha, MD_MIU_TEST_AGT_CTRL,
			    &r_value);
			if ((r_value & MIU_TA_CTL_BUSY) == 0)
				break;
		}

		if (j >= MAX_CTL_CHECK) {
			write_unlock_irqrestore(&ha->hw_lock, flags);
			return QLA_SUCCESS;
		}

		for (j = 0; j < 4; j++) {
			qla8044_rd_reg_indirect(vha, MD_MIU_TEST_AGT_RDDATA[j],
			    &r_data);
			*data_ptr++ = r_data;
		}

		r_addr += 16;
	}
	write_unlock_irqrestore(&ha->hw_lock, flags);

	ql_dbg(ql_dbg_p3p, vha, 0xb0f4,
	    "Leaving fn: %s datacount: 0x%x\n",
	     __func__, (loop_cnt * 16));

	*d_ptr = data_ptr;
	return QLA_SUCCESS;
}

/* ISP83xx flash read for _RDROM _BOARD */
static uint32_t
qla8044_minidump_process_rdrom(struct scsi_qla_host *vha,
	struct qla8044_minidump_entry_hdr *entry_hdr, uint32_t **d_ptr)
{
	uint32_t fl_addr, u32_count, rval;
	struct qla8044_minidump_entry_rdrom *rom_hdr;
	uint32_t *data_ptr = *d_ptr;

	rom_hdr = (struct qla8044_minidump_entry_rdrom *)entry_hdr;
	fl_addr = rom_hdr->read_addr;
	u32_count = (rom_hdr->read_data_size)/sizeof(uint32_t);

	ql_dbg(ql_dbg_p3p, vha, 0xb0f5, "[%s]: fl_addr: 0x%x, count: 0x%x\n",
	    __func__, fl_addr, u32_count);

	rval = qla8044_lockless_flash_read_u32(vha, fl_addr,
	    (u8 *)(data_ptr), u32_count);

	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_fatal, vha, 0xb0f6,
		    "%s: Flash Read Error,Count=%d\n", __func__, u32_count);
		return QLA_FUNCTION_FAILED;
	} else {
		data_ptr += u32_count;
		*d_ptr = data_ptr;
		return QLA_SUCCESS;
	}
}

static void
qla8044_mark_entry_skipped(struct scsi_qla_host *vha,
	struct qla8044_minidump_entry_hdr *entry_hdr, int index)
{
	entry_hdr->d_ctrl.driver_flags |= QLA82XX_DBG_SKIPPED_FLAG;

	ql_log(ql_log_info, vha, 0xb0f7,
	    "scsi(%ld): Skipping entry[%d]: ETYPE[0x%x]-ELEVEL[0x%x]\n",
	    vha->host_no, index, entry_hdr->entry_type,
	    entry_hdr->d_ctrl.entry_capture_mask);
}

static int
qla8044_minidump_process_l2tag(struct scsi_qla_host *vha,
	struct qla8044_minidump_entry_hdr *entry_hdr,
				 uint32_t **d_ptr)
{
	uint32_t addr, r_addr, c_addr, t_r_addr;
	uint32_t i, k, loop_count, t_value, r_cnt, r_value;
	unsigned long p_wait, w_time, p_mask;
	uint32_t c_value_w, c_value_r;
	struct qla8044_minidump_entry_cache *cache_hdr;
	int rval = QLA_FUNCTION_FAILED;
	uint32_t *data_ptr = *d_ptr;

	ql_dbg(ql_dbg_p3p, vha, 0xb0f8, "Entering fn: %s\n", __func__);
	cache_hdr = (struct qla8044_minidump_entry_cache *)entry_hdr;

	loop_count = cache_hdr->op_count;
	r_addr = cache_hdr->read_addr;
	c_addr = cache_hdr->control_addr;
	c_value_w = cache_hdr->cache_ctrl.write_value;

	t_r_addr = cache_hdr->tag_reg_addr;
	t_value = cache_hdr->addr_ctrl.init_tag_value;
	r_cnt = cache_hdr->read_ctrl.read_addr_cnt;
	p_wait = cache_hdr->cache_ctrl.poll_wait;
	p_mask = cache_hdr->cache_ctrl.poll_mask;

	for (i = 0; i < loop_count; i++) {
		qla8044_wr_reg_indirect(vha, t_r_addr, t_value);
		if (c_value_w)
			qla8044_wr_reg_indirect(vha, c_addr, c_value_w);

		if (p_mask) {
			w_time = jiffies + p_wait;
			do {
				qla8044_rd_reg_indirect(vha, c_addr,
				    &c_value_r);
				if ((c_value_r & p_mask) == 0) {
					break;
				} else if (time_after_eq(jiffies, w_time)) {
					/* capturing dump failed */
					return rval;
				}
			} while (1);
		}

		addr = r_addr;
		for (k = 0; k < r_cnt; k++) {
			qla8044_rd_reg_indirect(vha, addr, &r_value);
			*data_ptr++ = r_value;
			addr += cache_hdr->read_ctrl.read_addr_stride;
		}
		t_value += cache_hdr->addr_ctrl.tag_value_stride;
	}
	*d_ptr = data_ptr;
	return QLA_SUCCESS;
}

static void
qla8044_minidump_process_l1cache(struct scsi_qla_host *vha,
	struct qla8044_minidump_entry_hdr *entry_hdr, uint32_t **d_ptr)
{
	uint32_t addr, r_addr, c_addr, t_r_addr;
	uint32_t i, k, loop_count, t_value, r_cnt, r_value;
	uint32_t c_value_w;
	struct qla8044_minidump_entry_cache *cache_hdr;
	uint32_t *data_ptr = *d_ptr;

	cache_hdr = (struct qla8044_minidump_entry_cache *)entry_hdr;
	loop_count = cache_hdr->op_count;
	r_addr = cache_hdr->read_addr;
	c_addr = cache_hdr->control_addr;
	c_value_w = cache_hdr->cache_ctrl.write_value;

	t_r_addr = cache_hdr->tag_reg_addr;
	t_value = cache_hdr->addr_ctrl.init_tag_value;
	r_cnt = cache_hdr->read_ctrl.read_addr_cnt;

	for (i = 0; i < loop_count; i++) {
		qla8044_wr_reg_indirect(vha, t_r_addr, t_value);
		qla8044_wr_reg_indirect(vha, c_addr, c_value_w);
		addr = r_addr;
		for (k = 0; k < r_cnt; k++) {
			qla8044_rd_reg_indirect(vha, addr, &r_value);
			*data_ptr++ = r_value;
			addr += cache_hdr->read_ctrl.read_addr_stride;
		}
		t_value += cache_hdr->addr_ctrl.tag_value_stride;
	}
	*d_ptr = data_ptr;
}

static void
qla8044_minidump_process_rdocm(struct scsi_qla_host *vha,
	struct qla8044_minidump_entry_hdr *entry_hdr, uint32_t **d_ptr)
{
	uint32_t r_addr, r_stride, loop_cnt, i, r_value;
	struct qla8044_minidump_entry_rdocm *ocm_hdr;
	uint32_t *data_ptr = *d_ptr;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_p3p, vha, 0xb0f9, "Entering fn: %s\n", __func__);

	ocm_hdr = (struct qla8044_minidump_entry_rdocm *)entry_hdr;
	r_addr = ocm_hdr->read_addr;
	r_stride = ocm_hdr->read_addr_stride;
	loop_cnt = ocm_hdr->op_count;

	ql_dbg(ql_dbg_p3p, vha, 0xb0fa,
	    "[%s]: r_addr: 0x%x, r_stride: 0x%x, loop_cnt: 0x%x\n",
	    __func__, r_addr, r_stride, loop_cnt);

	for (i = 0; i < loop_cnt; i++) {
		r_value = readl((void __iomem *)(r_addr + ha->nx_pcibase));
		*data_ptr++ = r_value;
		r_addr += r_stride;
	}
	ql_dbg(ql_dbg_p3p, vha, 0xb0fb, "Leaving fn: %s datacount: 0x%lx\n",
	    __func__, (long unsigned int) (loop_cnt * sizeof(uint32_t)));

	*d_ptr = data_ptr;
}

static void
qla8044_minidump_process_rdmux(struct scsi_qla_host *vha,
	struct qla8044_minidump_entry_hdr *entry_hdr,
	uint32_t **d_ptr)
{
	uint32_t r_addr, s_stride, s_addr, s_value, loop_cnt, i, r_value;
	struct qla8044_minidump_entry_mux *mux_hdr;
	uint32_t *data_ptr = *d_ptr;

	ql_dbg(ql_dbg_p3p, vha, 0xb0fc, "Entering fn: %s\n", __func__);

	mux_hdr = (struct qla8044_minidump_entry_mux *)entry_hdr;
	r_addr = mux_hdr->read_addr;
	s_addr = mux_hdr->select_addr;
	s_stride = mux_hdr->select_value_stride;
	s_value = mux_hdr->select_value;
	loop_cnt = mux_hdr->op_count;

	for (i = 0; i < loop_cnt; i++) {
		qla8044_wr_reg_indirect(vha, s_addr, s_value);
		qla8044_rd_reg_indirect(vha, r_addr, &r_value);
		*data_ptr++ = s_value;
		*data_ptr++ = r_value;
		s_value += s_stride;
	}
	*d_ptr = data_ptr;
}

static void
qla8044_minidump_process_queue(struct scsi_qla_host *vha,
	struct qla8044_minidump_entry_hdr *entry_hdr,
	uint32_t **d_ptr)
{
	uint32_t s_addr, r_addr;
	uint32_t r_stride, r_value, r_cnt, qid = 0;
	uint32_t i, k, loop_cnt;
	struct qla8044_minidump_entry_queue *q_hdr;
	uint32_t *data_ptr = *d_ptr;

	ql_dbg(ql_dbg_p3p, vha, 0xb0fd, "Entering fn: %s\n", __func__);
	q_hdr = (struct qla8044_minidump_entry_queue *)entry_hdr;
	s_addr = q_hdr->select_addr;
	r_cnt = q_hdr->rd_strd.read_addr_cnt;
	r_stride = q_hdr->rd_strd.read_addr_stride;
	loop_cnt = q_hdr->op_count;

	for (i = 0; i < loop_cnt; i++) {
		qla8044_wr_reg_indirect(vha, s_addr, qid);
		r_addr = q_hdr->read_addr;
		for (k = 0; k < r_cnt; k++) {
			qla8044_rd_reg_indirect(vha, r_addr, &r_value);
			*data_ptr++ = r_value;
			r_addr += r_stride;
		}
		qid += q_hdr->q_strd.queue_id_stride;
	}
	*d_ptr = data_ptr;
}

/* ISP83xx functions to process new minidump entries... */
static uint32_t
qla8044_minidump_process_pollrd(struct scsi_qla_host *vha,
	struct qla8044_minidump_entry_hdr *entry_hdr,
	uint32_t **d_ptr)
{
	uint32_t r_addr, s_addr, s_value, r_value, poll_wait, poll_mask;
	uint16_t s_stride, i;
	struct qla8044_minidump_entry_pollrd *pollrd_hdr;
	uint32_t *data_ptr = *d_ptr;

	pollrd_hdr = (struct qla8044_minidump_entry_pollrd *) entry_hdr;
	s_addr = pollrd_hdr->select_addr;
	r_addr = pollrd_hdr->read_addr;
	s_value = pollrd_hdr->select_value;
	s_stride = pollrd_hdr->select_value_stride;

	poll_wait = pollrd_hdr->poll_wait;
	poll_mask = pollrd_hdr->poll_mask;

	for (i = 0; i < pollrd_hdr->op_count; i++) {
		qla8044_wr_reg_indirect(vha, s_addr, s_value);
		poll_wait = pollrd_hdr->poll_wait;
		while (1) {
			qla8044_rd_reg_indirect(vha, s_addr, &r_value);
			if ((r_value & poll_mask) != 0) {
				break;
			} else {
				usleep_range(1000, 1100);
				if (--poll_wait == 0) {
					ql_log(ql_log_fatal, vha, 0xb0fe,
					    "%s: TIMEOUT\n", __func__);
					goto error;
				}
			}
		}
		qla8044_rd_reg_indirect(vha, r_addr, &r_value);
		*data_ptr++ = s_value;
		*data_ptr++ = r_value;

		s_value += s_stride;
	}
	*d_ptr = data_ptr;
	return QLA_SUCCESS;

error:
	return QLA_FUNCTION_FAILED;
}

static void
qla8044_minidump_process_rdmux2(struct scsi_qla_host *vha,
	struct qla8044_minidump_entry_hdr *entry_hdr, uint32_t **d_ptr)
{
	uint32_t sel_val1, sel_val2, t_sel_val, data, i;
	uint32_t sel_addr1, sel_addr2, sel_val_mask, read_addr;
	struct qla8044_minidump_entry_rdmux2 *rdmux2_hdr;
	uint32_t *data_ptr = *d_ptr;

	rdmux2_hdr = (struct qla8044_minidump_entry_rdmux2 *) entry_hdr;
	sel_val1 = rdmux2_hdr->select_value_1;
	sel_val2 = rdmux2_hdr->select_value_2;
	sel_addr1 = rdmux2_hdr->select_addr_1;
	sel_addr2 = rdmux2_hdr->select_addr_2;
	sel_val_mask = rdmux2_hdr->select_value_mask;
	read_addr = rdmux2_hdr->read_addr;

	for (i = 0; i < rdmux2_hdr->op_count; i++) {
		qla8044_wr_reg_indirect(vha, sel_addr1, sel_val1);
		t_sel_val = sel_val1 & sel_val_mask;
		*data_ptr++ = t_sel_val;

		qla8044_wr_reg_indirect(vha, sel_addr2, t_sel_val);
		qla8044_rd_reg_indirect(vha, read_addr, &data);

		*data_ptr++ = data;

		qla8044_wr_reg_indirect(vha, sel_addr1, sel_val2);
		t_sel_val = sel_val2 & sel_val_mask;
		*data_ptr++ = t_sel_val;

		qla8044_wr_reg_indirect(vha, sel_addr2, t_sel_val);
		qla8044_rd_reg_indirect(vha, read_addr, &data);

		*data_ptr++ = data;

		sel_val1 += rdmux2_hdr->select_value_stride;
		sel_val2 += rdmux2_hdr->select_value_stride;
	}

	*d_ptr = data_ptr;
}

static uint32_t
qla8044_minidump_process_pollrdmwr(struct scsi_qla_host *vha,
	struct qla8044_minidump_entry_hdr *entry_hdr,
	uint32_t **d_ptr)
{
	uint32_t poll_wait, poll_mask, r_value, data;
	uint32_t addr_1, addr_2, value_1, value_2;
	struct qla8044_minidump_entry_pollrdmwr *poll_hdr;
	uint32_t *data_ptr = *d_ptr;

	poll_hdr = (struct qla8044_minidump_entry_pollrdmwr *) entry_hdr;
	addr_1 = poll_hdr->addr_1;
	addr_2 = poll_hdr->addr_2;
	value_1 = poll_hdr->value_1;
	value_2 = poll_hdr->value_2;
	poll_mask = poll_hdr->poll_mask;

	qla8044_wr_reg_indirect(vha, addr_1, value_1);

	poll_wait = poll_hdr->poll_wait;
	while (1) {
		qla8044_rd_reg_indirect(vha, addr_1, &r_value);

		if ((r_value & poll_mask) != 0) {
			break;
		} else {
			usleep_range(1000, 1100);
			if (--poll_wait == 0) {
				ql_log(ql_log_fatal, vha, 0xb0ff,
				    "%s: TIMEOUT\n", __func__);
				goto error;
			}
		}
	}

	qla8044_rd_reg_indirect(vha, addr_2, &data);
	data &= poll_hdr->modify_mask;
	qla8044_wr_reg_indirect(vha, addr_2, data);
	qla8044_wr_reg_indirect(vha, addr_1, value_2);

	poll_wait = poll_hdr->poll_wait;
	while (1) {
		qla8044_rd_reg_indirect(vha, addr_1, &r_value);

		if ((r_value & poll_mask) != 0) {
			break;
		} else {
			usleep_range(1000, 1100);
			if (--poll_wait == 0) {
				ql_log(ql_log_fatal, vha, 0xb100,
				    "%s: TIMEOUT2\n", __func__);
				goto error;
			}
		}
	}

	*data_ptr++ = addr_2;
	*data_ptr++ = data;

	*d_ptr = data_ptr;

	return QLA_SUCCESS;

error:
	return QLA_FUNCTION_FAILED;
}

#define ISP8044_PEX_DMA_ENGINE_INDEX		8
#define ISP8044_PEX_DMA_BASE_ADDRESS		0x77320000
#define ISP8044_PEX_DMA_NUM_OFFSET		0x10000
#define ISP8044_PEX_DMA_CMD_ADDR_LOW		0x0
#define ISP8044_PEX_DMA_CMD_ADDR_HIGH		0x04
#define ISP8044_PEX_DMA_CMD_STS_AND_CNTRL	0x08

#define ISP8044_PEX_DMA_READ_SIZE	(16 * 1024)
#define ISP8044_PEX_DMA_MAX_WAIT	(100 * 100) /* Max wait of 100 msecs */

static int
qla8044_check_dma_engine_state(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;
	int rval = QLA_SUCCESS;
	uint32_t dma_eng_num = 0, cmd_sts_and_cntrl = 0;
	uint64_t dma_base_addr = 0;
	struct qla8044_minidump_template_hdr *tmplt_hdr = NULL;

	tmplt_hdr = ha->md_tmplt_hdr;
	dma_eng_num =
	    tmplt_hdr->saved_state_array[ISP8044_PEX_DMA_ENGINE_INDEX];
	dma_base_addr = ISP8044_PEX_DMA_BASE_ADDRESS +
		(dma_eng_num * ISP8044_PEX_DMA_NUM_OFFSET);

	/* Read the pex-dma's command-status-and-control register. */
	rval = qla8044_rd_reg_indirect(vha,
	    (dma_base_addr + ISP8044_PEX_DMA_CMD_STS_AND_CNTRL),
	    &cmd_sts_and_cntrl);
	if (rval)
		return QLA_FUNCTION_FAILED;

	/* Check if requested pex-dma engine is available. */
	if (cmd_sts_and_cntrl & BIT_31)
		return QLA_SUCCESS;

	return QLA_FUNCTION_FAILED;
}

static int
qla8044_start_pex_dma(struct scsi_qla_host *vha,
	struct qla8044_minidump_entry_rdmem_pex_dma *m_hdr)
{
	struct qla_hw_data *ha = vha->hw;
	int rval = QLA_SUCCESS, wait = 0;
	uint32_t dma_eng_num = 0, cmd_sts_and_cntrl = 0;
	uint64_t dma_base_addr = 0;
	struct qla8044_minidump_template_hdr *tmplt_hdr = NULL;

	tmplt_hdr = ha->md_tmplt_hdr;
	dma_eng_num =
	    tmplt_hdr->saved_state_array[ISP8044_PEX_DMA_ENGINE_INDEX];
	dma_base_addr = ISP8044_PEX_DMA_BASE_ADDRESS +
		(dma_eng_num * ISP8044_PEX_DMA_NUM_OFFSET);

	rval = qla8044_wr_reg_indirect(vha,
	    dma_base_addr + ISP8044_PEX_DMA_CMD_ADDR_LOW,
	    m_hdr->desc_card_addr);
	if (rval)
		goto error_exit;

	rval = qla8044_wr_reg_indirect(vha,
	    dma_base_addr + ISP8044_PEX_DMA_CMD_ADDR_HIGH, 0);
	if (rval)
		goto error_exit;

	rval = qla8044_wr_reg_indirect(vha,
	    dma_base_addr + ISP8044_PEX_DMA_CMD_STS_AND_CNTRL,
	    m_hdr->start_dma_cmd);
	if (rval)
		goto error_exit;

	/* Wait for dma operation to complete. */
	for (wait = 0; wait < ISP8044_PEX_DMA_MAX_WAIT; wait++) {
		rval = qla8044_rd_reg_indirect(vha,
		    (dma_base_addr + ISP8044_PEX_DMA_CMD_STS_AND_CNTRL),
		    &cmd_sts_and_cntrl);
		if (rval)
			goto error_exit;

		if ((cmd_sts_and_cntrl & BIT_1) == 0)
			break;

		udelay(10);
	}

	/* Wait a max of 100 ms, otherwise fallback to rdmem entry read */
	if (wait >= ISP8044_PEX_DMA_MAX_WAIT) {
		rval = QLA_FUNCTION_FAILED;
		goto error_exit;
	}

error_exit:
	return rval;
}

static int
qla8044_minidump_pex_dma_read(struct scsi_qla_host *vha,
	struct qla8044_minidump_entry_hdr *entry_hdr, uint32_t **d_ptr)
{
	struct qla_hw_data *ha = vha->hw;
	int rval = QLA_SUCCESS;
	struct qla8044_minidump_entry_rdmem_pex_dma *m_hdr = NULL;
	uint32_t chunk_size, read_size;
	uint8_t *data_ptr = (uint8_t *)*d_ptr;
	void *rdmem_buffer = NULL;
	dma_addr_t rdmem_dma;
	struct qla8044_pex_dma_descriptor dma_desc;

	rval = qla8044_check_dma_engine_state(vha);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_p3p, vha, 0xb147,
		    "DMA engine not available. Fallback to rdmem-read.\n");
		return QLA_FUNCTION_FAILED;
	}

	m_hdr = (void *)entry_hdr;

	rdmem_buffer = dma_alloc_coherent(&ha->pdev->dev,
	    ISP8044_PEX_DMA_READ_SIZE, &rdmem_dma, GFP_KERNEL);
	if (!rdmem_buffer) {
		ql_dbg(ql_dbg_p3p, vha, 0xb148,
		    "Unable to allocate rdmem dma buffer\n");
		return QLA_FUNCTION_FAILED;
	}

	/* Prepare pex-dma descriptor to be written to MS memory. */
	/* dma-desc-cmd layout:
	 *		0-3: dma-desc-cmd 0-3
	 *		4-7: pcid function number
	 *		8-15: dma-desc-cmd 8-15
	 * dma_bus_addr: dma buffer address
	 * cmd.read_data_size: amount of data-chunk to be read.
	 */
	dma_desc.cmd.dma_desc_cmd = (m_hdr->dma_desc_cmd & 0xff0f);
	dma_desc.cmd.dma_desc_cmd |=
	    ((PCI_FUNC(ha->pdev->devfn) & 0xf) << 0x4);

	dma_desc.dma_bus_addr = rdmem_dma;
	dma_desc.cmd.read_data_size = chunk_size = ISP8044_PEX_DMA_READ_SIZE;
	read_size = 0;

	/*
	 * Perform rdmem operation using pex-dma.
	 * Prepare dma in chunks of ISP8044_PEX_DMA_READ_SIZE.
	 */
	while (read_size < m_hdr->read_data_size) {
		if (m_hdr->read_data_size - read_size <
		    ISP8044_PEX_DMA_READ_SIZE) {
			chunk_size = (m_hdr->read_data_size - read_size);
			dma_desc.cmd.read_data_size = chunk_size;
		}

		dma_desc.src_addr = m_hdr->read_addr + read_size;

		/* Prepare: Write pex-dma descriptor to MS memory. */
		rval = qla8044_ms_mem_write_128b(vha,
		    m_hdr->desc_card_addr, (void *)&dma_desc,
		    (sizeof(struct qla8044_pex_dma_descriptor)/16));
		if (rval) {
			ql_log(ql_log_warn, vha, 0xb14a,
			    "%s: Error writing rdmem-dma-init to MS !!!\n",
			    __func__);
			goto error_exit;
		}
		ql_dbg(ql_dbg_p3p, vha, 0xb14b,
		    "%s: Dma-descriptor: Instruct for rdmem dma "
		    "(chunk_size 0x%x).\n", __func__, chunk_size);

		/* Execute: Start pex-dma operation. */
		rval = qla8044_start_pex_dma(vha, m_hdr);
		if (rval)
			goto error_exit;

		memcpy(data_ptr, rdmem_buffer, chunk_size);
		data_ptr += chunk_size;
		read_size += chunk_size;
	}

	*d_ptr = (void *)data_ptr;

error_exit:
	if (rdmem_buffer)
		dma_free_coherent(&ha->pdev->dev, ISP8044_PEX_DMA_READ_SIZE,
		    rdmem_buffer, rdmem_dma);

	return rval;
}

static uint32_t
qla8044_minidump_process_rddfe(struct scsi_qla_host *vha,
	struct qla8044_minidump_entry_hdr *entry_hdr, uint32_t **d_ptr)
{
	int loop_cnt;
	uint32_t addr1, addr2, value, data, temp, wrVal;
	uint8_t stride, stride2;
	uint16_t count;
	uint32_t poll, mask, data_size, modify_mask;
	uint32_t wait_count = 0;

	uint32_t *data_ptr = *d_ptr;

	struct qla8044_minidump_entry_rddfe *rddfe;
	rddfe = (struct qla8044_minidump_entry_rddfe *) entry_hdr;

	addr1 = rddfe->addr_1;
	value = rddfe->value;
	stride = rddfe->stride;
	stride2 = rddfe->stride2;
	count = rddfe->count;

	poll = rddfe->poll;
	mask = rddfe->mask;
	modify_mask = rddfe->modify_mask;
	data_size = rddfe->data_size;

	addr2 = addr1 + stride;

	for (loop_cnt = 0x0; loop_cnt < count; loop_cnt++) {
		qla8044_wr_reg_indirect(vha, addr1, (0x40000000 | value));

		wait_count = 0;
		while (wait_count < poll) {
			qla8044_rd_reg_indirect(vha, addr1, &temp);
			if ((temp & mask) != 0)
				break;
			wait_count++;
		}

		if (wait_count == poll) {
			ql_log(ql_log_warn, vha, 0xb153,
			    "%s: TIMEOUT\n", __func__);
			goto error;
		} else {
			qla8044_rd_reg_indirect(vha, addr2, &temp);
			temp = temp & modify_mask;
			temp = (temp | ((loop_cnt << 16) | loop_cnt));
			wrVal = ((temp << 16) | temp);

			qla8044_wr_reg_indirect(vha, addr2, wrVal);
			qla8044_wr_reg_indirect(vha, addr1, value);

			wait_count = 0;
			while (wait_count < poll) {
				qla8044_rd_reg_indirect(vha, addr1, &temp);
				if ((temp & mask) != 0)
					break;
				wait_count++;
			}
			if (wait_count == poll) {
				ql_log(ql_log_warn, vha, 0xb154,
				    "%s: TIMEOUT\n", __func__);
				goto error;
			}

			qla8044_wr_reg_indirect(vha, addr1,
			    ((0x40000000 | value) + stride2));
			wait_count = 0;
			while (wait_count < poll) {
				qla8044_rd_reg_indirect(vha, addr1, &temp);
				if ((temp & mask) != 0)
					break;
				wait_count++;
			}

			if (wait_count == poll) {
				ql_log(ql_log_warn, vha, 0xb155,
				    "%s: TIMEOUT\n", __func__);
				goto error;
			}

			qla8044_rd_reg_indirect(vha, addr2, &data);

			*data_ptr++ = wrVal;
			*data_ptr++ = data;
		}

	}

	*d_ptr = data_ptr;
	return QLA_SUCCESS;

error:
	return -1;

}

static uint32_t
qla8044_minidump_process_rdmdio(struct scsi_qla_host *vha,
	struct qla8044_minidump_entry_hdr *entry_hdr, uint32_t **d_ptr)
{
	int ret = 0;
	uint32_t addr1, addr2, value1, value2, data, selVal;
	uint8_t stride1, stride2;
	uint32_t addr3, addr4, addr5, addr6, addr7;
	uint16_t count, loop_cnt;
	uint32_t poll, mask;
	uint32_t *data_ptr = *d_ptr;

	struct qla8044_minidump_entry_rdmdio *rdmdio;

	rdmdio = (struct qla8044_minidump_entry_rdmdio *) entry_hdr;

	addr1 = rdmdio->addr_1;
	addr2 = rdmdio->addr_2;
	value1 = rdmdio->value_1;
	stride1 = rdmdio->stride_1;
	stride2 = rdmdio->stride_2;
	count = rdmdio->count;

	poll = rdmdio->poll;
	mask = rdmdio->mask;
	value2 = rdmdio->value_2;

	addr3 = addr1 + stride1;

	for (loop_cnt = 0; loop_cnt < count; loop_cnt++) {
		ret = qla8044_poll_wait_ipmdio_bus_idle(vha, addr1, addr2,
		    addr3, mask);
		if (ret == -1)
			goto error;

		addr4 = addr2 - stride1;
		ret = qla8044_ipmdio_wr_reg(vha, addr1, addr3, mask, addr4,
		    value2);
		if (ret == -1)
			goto error;

		addr5 = addr2 - (2 * stride1);
		ret = qla8044_ipmdio_wr_reg(vha, addr1, addr3, mask, addr5,
		    value1);
		if (ret == -1)
			goto error;

		addr6 = addr2 - (3 * stride1);
		ret = qla8044_ipmdio_wr_reg(vha, addr1, addr3, mask,
		    addr6, 0x2);
		if (ret == -1)
			goto error;

		ret = qla8044_poll_wait_ipmdio_bus_idle(vha, addr1, addr2,
		    addr3, mask);
		if (ret == -1)
			goto error;

		addr7 = addr2 - (4 * stride1);
		data = qla8044_ipmdio_rd_reg(vha, addr1, addr3, mask, addr7);
		if (data == -1)
			goto error;

		selVal = (value2 << 18) | (value1 << 2) | 2;

		stride2 = rdmdio->stride_2;
		*data_ptr++ = selVal;
		*data_ptr++ = data;

		value1 = value1 + stride2;
		*d_ptr = data_ptr;
	}

	return 0;

error:
	return -1;
}

static uint32_t qla8044_minidump_process_pollwr(struct scsi_qla_host *vha,
		struct qla8044_minidump_entry_hdr *entry_hdr, uint32_t **d_ptr)
{
	uint32_t addr1, addr2, value1, value2, poll, mask, r_value;
	uint32_t wait_count = 0;
	struct qla8044_minidump_entry_pollwr *pollwr_hdr;

	pollwr_hdr = (struct qla8044_minidump_entry_pollwr *)entry_hdr;
	addr1 = pollwr_hdr->addr_1;
	addr2 = pollwr_hdr->addr_2;
	value1 = pollwr_hdr->value_1;
	value2 = pollwr_hdr->value_2;

	poll = pollwr_hdr->poll;
	mask = pollwr_hdr->mask;

	while (wait_count < poll) {
		qla8044_rd_reg_indirect(vha, addr1, &r_value);

		if ((r_value & poll) != 0)
			break;
		wait_count++;
	}

	if (wait_count == poll) {
		ql_log(ql_log_warn, vha, 0xb156, "%s: TIMEOUT\n", __func__);
		goto error;
	}

	qla8044_wr_reg_indirect(vha, addr2, value2);
	qla8044_wr_reg_indirect(vha, addr1, value1);

	wait_count = 0;
	while (wait_count < poll) {
		qla8044_rd_reg_indirect(vha, addr1, &r_value);

		if ((r_value & poll) != 0)
			break;
		wait_count++;
	}

	return QLA_SUCCESS;

error:
	return -1;
}

/*
 *
 * qla8044_collect_md_data - Retrieve firmware minidump data.
 * @ha: pointer to adapter structure
 **/
int
qla8044_collect_md_data(struct scsi_qla_host *vha)
{
	int num_entry_hdr = 0;
	struct qla8044_minidump_entry_hdr *entry_hdr;
	struct qla8044_minidump_template_hdr *tmplt_hdr;
	uint32_t *data_ptr;
	uint32_t data_collected = 0, f_capture_mask;
	int i, rval = QLA_FUNCTION_FAILED;
	uint64_t now;
	uint32_t timestamp, idc_control;
	struct qla_hw_data *ha = vha->hw;

	if (!ha->md_dump) {
		ql_log(ql_log_info, vha, 0xb101,
		    "%s(%ld) No buffer to dump\n",
		    __func__, vha->host_no);
		return rval;
	}

	if (ha->fw_dumped) {
		ql_log(ql_log_warn, vha, 0xb10d,
		    "Firmware has been previously dumped (%p) "
		    "-- ignoring request.\n", ha->fw_dump);
		goto md_failed;
	}

	ha->fw_dumped = 0;

	if (!ha->md_tmplt_hdr || !ha->md_dump) {
		ql_log(ql_log_warn, vha, 0xb10e,
		    "Memory not allocated for minidump capture\n");
		goto md_failed;
	}

	qla8044_idc_lock(ha);
	idc_control = qla8044_rd_reg(ha, QLA8044_IDC_DRV_CTRL);
	if (idc_control & GRACEFUL_RESET_BIT1) {
		ql_log(ql_log_warn, vha, 0xb112,
		    "Forced reset from application, "
		    "ignore minidump capture\n");
		qla8044_wr_reg(ha, QLA8044_IDC_DRV_CTRL,
		    (idc_control & ~GRACEFUL_RESET_BIT1));
		qla8044_idc_unlock(ha);

		goto md_failed;
	}
	qla8044_idc_unlock(ha);

	if (qla82xx_validate_template_chksum(vha)) {
		ql_log(ql_log_info, vha, 0xb109,
		    "Template checksum validation error\n");
		goto md_failed;
	}

	tmplt_hdr = (struct qla8044_minidump_template_hdr *)
		ha->md_tmplt_hdr;
	data_ptr = (uint32_t *)((uint8_t *)ha->md_dump);
	num_entry_hdr = tmplt_hdr->num_of_entries;

	ql_dbg(ql_dbg_p3p, vha, 0xb11a,
	    "Capture Mask obtained: 0x%x\n", tmplt_hdr->capture_debug_level);

	f_capture_mask = tmplt_hdr->capture_debug_level & 0xFF;

	/* Validate whether required debug level is set */
	if ((f_capture_mask & 0x3) != 0x3) {
		ql_log(ql_log_warn, vha, 0xb10f,
		    "Minimum required capture mask[0x%x] level not set\n",
		    f_capture_mask);

	}
	tmplt_hdr->driver_capture_mask = ql2xmdcapmask;
	ql_log(ql_log_info, vha, 0xb102,
	    "[%s]: starting data ptr: %p\n",
	   __func__, data_ptr);
	ql_log(ql_log_info, vha, 0xb10b,
	   "[%s]: no of entry headers in Template: 0x%x\n",
	   __func__, num_entry_hdr);
	ql_log(ql_log_info, vha, 0xb10c,
	    "[%s]: Total_data_size 0x%x, %d obtained\n",
	   __func__, ha->md_dump_size, ha->md_dump_size);

	/* Update current timestamp before taking dump */
	now = get_jiffies_64();
	timestamp = (u32)(jiffies_to_msecs(now) / 1000);
	tmplt_hdr->driver_timestamp = timestamp;

	entry_hdr = (struct qla8044_minidump_entry_hdr *)
		(((uint8_t *)ha->md_tmplt_hdr) + tmplt_hdr->first_entry_offset);
	tmplt_hdr->saved_state_array[QLA8044_SS_OCM_WNDREG_INDEX] =
	    tmplt_hdr->ocm_window_reg[ha->portnum];

	/* Walk through the entry headers - validate/perform required action */
	for (i = 0; i < num_entry_hdr; i++) {
		if (data_collected > ha->md_dump_size) {
			ql_log(ql_log_info, vha, 0xb103,
			    "Data collected: [0x%x], "
			    "Total Dump size: [0x%x]\n",
			    data_collected, ha->md_dump_size);
			return rval;
		}

		if (!(entry_hdr->d_ctrl.entry_capture_mask &
		      ql2xmdcapmask)) {
			entry_hdr->d_ctrl.driver_flags |=
			    QLA82XX_DBG_SKIPPED_FLAG;
			goto skip_nxt_entry;
		}

		ql_dbg(ql_dbg_p3p, vha, 0xb104,
		    "Data collected: [0x%x], Dump size left:[0x%x]\n",
		    data_collected,
		    (ha->md_dump_size - data_collected));

		/* Decode the entry type and take required action to capture
		 * debug data
		 */
		switch (entry_hdr->entry_type) {
		case QLA82XX_RDEND:
			qla8044_mark_entry_skipped(vha, entry_hdr, i);
			break;
		case QLA82XX_CNTRL:
			rval = qla8044_minidump_process_control(vha,
			    entry_hdr);
			if (rval != QLA_SUCCESS) {
				qla8044_mark_entry_skipped(vha, entry_hdr, i);
				goto md_failed;
			}
			break;
		case QLA82XX_RDCRB:
			qla8044_minidump_process_rdcrb(vha,
			    entry_hdr, &data_ptr);
			break;
		case QLA82XX_RDMEM:
			rval = qla8044_minidump_pex_dma_read(vha,
			    entry_hdr, &data_ptr);
			if (rval != QLA_SUCCESS) {
				rval = qla8044_minidump_process_rdmem(vha,
				    entry_hdr, &data_ptr);
				if (rval != QLA_SUCCESS) {
					qla8044_mark_entry_skipped(vha,
					    entry_hdr, i);
					goto md_failed;
				}
			}
			break;
		case QLA82XX_BOARD:
		case QLA82XX_RDROM:
			rval = qla8044_minidump_process_rdrom(vha,
			    entry_hdr, &data_ptr);
			if (rval != QLA_SUCCESS) {
				qla8044_mark_entry_skipped(vha,
				    entry_hdr, i);
			}
			break;
		case QLA82XX_L2DTG:
		case QLA82XX_L2ITG:
		case QLA82XX_L2DAT:
		case QLA82XX_L2INS:
			rval = qla8044_minidump_process_l2tag(vha,
			    entry_hdr, &data_ptr);
			if (rval != QLA_SUCCESS) {
				qla8044_mark_entry_skipped(vha, entry_hdr, i);
				goto md_failed;
			}
			break;
		case QLA8044_L1DTG:
		case QLA8044_L1ITG:
		case QLA82XX_L1DAT:
		case QLA82XX_L1INS:
			qla8044_minidump_process_l1cache(vha,
			    entry_hdr, &data_ptr);
			break;
		case QLA82XX_RDOCM:
			qla8044_minidump_process_rdocm(vha,
			    entry_hdr, &data_ptr);
			break;
		case QLA82XX_RDMUX:
			qla8044_minidump_process_rdmux(vha,
			    entry_hdr, &data_ptr);
			break;
		case QLA82XX_QUEUE:
			qla8044_minidump_process_queue(vha,
			    entry_hdr, &data_ptr);
			break;
		case QLA8044_POLLRD:
			rval = qla8044_minidump_process_pollrd(vha,
			    entry_hdr, &data_ptr);
			if (rval != QLA_SUCCESS)
				qla8044_mark_entry_skipped(vha, entry_hdr, i);
			break;
		case QLA8044_RDMUX2:
			qla8044_minidump_process_rdmux2(vha,
			    entry_hdr, &data_ptr);
			break;
		case QLA8044_POLLRDMWR:
			rval = qla8044_minidump_process_pollrdmwr(vha,
			    entry_hdr, &data_ptr);
			if (rval != QLA_SUCCESS)
				qla8044_mark_entry_skipped(vha, entry_hdr, i);
			break;
		case QLA8044_RDDFE:
			rval = qla8044_minidump_process_rddfe(vha, entry_hdr,
			    &data_ptr);
			if (rval != QLA_SUCCESS)
				qla8044_mark_entry_skipped(vha, entry_hdr, i);
			break;
		case QLA8044_RDMDIO:
			rval = qla8044_minidump_process_rdmdio(vha, entry_hdr,
			    &data_ptr);
			if (rval != QLA_SUCCESS)
				qla8044_mark_entry_skipped(vha, entry_hdr, i);
			break;
		case QLA8044_POLLWR:
			rval = qla8044_minidump_process_pollwr(vha, entry_hdr,
			    &data_ptr);
			if (rval != QLA_SUCCESS)
				qla8044_mark_entry_skipped(vha, entry_hdr, i);
			break;
		case QLA82XX_RDNOP:
		default:
			qla8044_mark_entry_skipped(vha, entry_hdr, i);
			break;
		}

		data_collected = (uint8_t *)data_ptr -
		    (uint8_t *)((uint8_t *)ha->md_dump);
skip_nxt_entry:
		/*
		 * next entry in the template
		 */
		entry_hdr = (struct qla8044_minidump_entry_hdr *)
		    (((uint8_t *)entry_hdr) + entry_hdr->entry_size);
	}

	if (data_collected != ha->md_dump_size) {
		ql_log(ql_log_info, vha, 0xb105,
		    "Dump data mismatch: Data collected: "
		    "[0x%x], total_data_size:[0x%x]\n",
		    data_collected, ha->md_dump_size);
		rval = QLA_FUNCTION_FAILED;
		goto md_failed;
	}

	ql_log(ql_log_info, vha, 0xb110,
	    "Firmware dump saved to temp buffer (%ld/%p %ld/%p).\n",
	    vha->host_no, ha->md_tmplt_hdr, vha->host_no, ha->md_dump);
	ha->fw_dumped = 1;
	qla2x00_post_uevent_work(vha, QLA_UEVENT_CODE_FW_DUMP);


	ql_log(ql_log_info, vha, 0xb106,
	    "Leaving fn: %s Last entry: 0x%x\n",
	    __func__, i);
md_failed:
	return rval;
}

void
qla8044_get_minidump(struct scsi_qla_host *vha)
{
	struct qla_hw_data *ha = vha->hw;

	if (!qla8044_collect_md_data(vha)) {
		ha->fw_dumped = 1;
		ha->prev_minidump_failed = 0;
	} else {
		ql_log(ql_log_fatal, vha, 0xb0db,
		    "%s: Unable to collect minidump\n",
		    __func__);
		ha->prev_minidump_failed = 1;
	}
}

static int
qla8044_poll_flash_status_reg(struct scsi_qla_host *vha)
{
	uint32_t flash_status;
	int retries = QLA8044_FLASH_READ_RETRY_COUNT;
	int ret_val = QLA_SUCCESS;

	while (retries--) {
		ret_val = qla8044_rd_reg_indirect(vha, QLA8044_FLASH_STATUS,
		    &flash_status);
		if (ret_val) {
			ql_log(ql_log_warn, vha, 0xb13c,
			    "%s: Failed to read FLASH_STATUS reg.\n",
			    __func__);
			break;
		}
		if ((flash_status & QLA8044_FLASH_STATUS_READY) ==
		    QLA8044_FLASH_STATUS_READY)
			break;
		msleep(QLA8044_FLASH_STATUS_REG_POLL_DELAY);
	}

	if (!retries)
		ret_val = QLA_FUNCTION_FAILED;

	return ret_val;
}

static int
qla8044_write_flash_status_reg(struct scsi_qla_host *vha,
			       uint32_t data)
{
	int ret_val = QLA_SUCCESS;
	uint32_t cmd;

	cmd = vha->hw->fdt_wrt_sts_reg_cmd;

	ret_val = qla8044_wr_reg_indirect(vha, QLA8044_FLASH_ADDR,
	    QLA8044_FLASH_STATUS_WRITE_DEF_SIG | cmd);
	if (ret_val) {
		ql_log(ql_log_warn, vha, 0xb125,
		    "%s: Failed to write to FLASH_ADDR.\n", __func__);
		goto exit_func;
	}

	ret_val = qla8044_wr_reg_indirect(vha, QLA8044_FLASH_WRDATA, data);
	if (ret_val) {
		ql_log(ql_log_warn, vha, 0xb126,
		    "%s: Failed to write to FLASH_WRDATA.\n", __func__);
		goto exit_func;
	}

	ret_val = qla8044_wr_reg_indirect(vha, QLA8044_FLASH_CONTROL,
	    QLA8044_FLASH_SECOND_ERASE_MS_VAL);
	if (ret_val) {
		ql_log(ql_log_warn, vha, 0xb127,
		    "%s: Failed to write to FLASH_CONTROL.\n", __func__);
		goto exit_func;
	}

	ret_val = qla8044_poll_flash_status_reg(vha);
	if (ret_val)
		ql_log(ql_log_warn, vha, 0xb128,
		    "%s: Error polling flash status reg.\n", __func__);

exit_func:
	return ret_val;
}

/*
 * This function assumes that the flash lock is held.
 */
static int
qla8044_unprotect_flash(scsi_qla_host_t *vha)
{
	int ret_val;
	struct qla_hw_data *ha = vha->hw;

	ret_val = qla8044_write_flash_status_reg(vha, ha->fdt_wrt_enable);
	if (ret_val)
		ql_log(ql_log_warn, vha, 0xb139,
		    "%s: Write flash status failed.\n", __func__);

	return ret_val;
}

/*
 * This function assumes that the flash lock is held.
 */
static int
qla8044_protect_flash(scsi_qla_host_t *vha)
{
	int ret_val;
	struct qla_hw_data *ha = vha->hw;

	ret_val = qla8044_write_flash_status_reg(vha, ha->fdt_wrt_disable);
	if (ret_val)
		ql_log(ql_log_warn, vha, 0xb13b,
		    "%s: Write flash status failed.\n", __func__);

	return ret_val;
}


static int
qla8044_erase_flash_sector(struct scsi_qla_host *vha,
			   uint32_t sector_start_addr)
{
	uint32_t reversed_addr;
	int ret_val = QLA_SUCCESS;

	ret_val = qla8044_poll_flash_status_reg(vha);
	if (ret_val) {
		ql_log(ql_log_warn, vha, 0xb12e,
		    "%s: Poll flash status after erase failed..\n", __func__);
	}

	reversed_addr = (((sector_start_addr & 0xFF) << 16) |
	    (sector_start_addr & 0xFF00) |
	    ((sector_start_addr & 0xFF0000) >> 16));

	ret_val = qla8044_wr_reg_indirect(vha,
	    QLA8044_FLASH_WRDATA, reversed_addr);
	if (ret_val) {
		ql_log(ql_log_warn, vha, 0xb12f,
		    "%s: Failed to write to FLASH_WRDATA.\n", __func__);
	}
	ret_val = qla8044_wr_reg_indirect(vha, QLA8044_FLASH_ADDR,
	   QLA8044_FLASH_ERASE_SIG | vha->hw->fdt_erase_cmd);
	if (ret_val) {
		ql_log(ql_log_warn, vha, 0xb130,
		    "%s: Failed to write to FLASH_ADDR.\n", __func__);
	}
	ret_val = qla8044_wr_reg_indirect(vha, QLA8044_FLASH_CONTROL,
	    QLA8044_FLASH_LAST_ERASE_MS_VAL);
	if (ret_val) {
		ql_log(ql_log_warn, vha, 0xb131,
		    "%s: Failed write to FLASH_CONTROL.\n", __func__);
	}
	ret_val = qla8044_poll_flash_status_reg(vha);
	if (ret_val) {
		ql_log(ql_log_warn, vha, 0xb132,
		    "%s: Poll flash status failed.\n", __func__);
	}


	return ret_val;
}

/*
 * qla8044_flash_write_u32 - Write data to flash
 *
 * @ha : Pointer to adapter structure
 * addr : Flash address to write to
 * p_data : Data to be written
 *
 * Return Value - QLA_SUCCESS/QLA_FUNCTION_FAILED
 *
 * NOTE: Lock should be held on entry
 */
static int
qla8044_flash_write_u32(struct scsi_qla_host *vha, uint32_t addr,
			uint32_t *p_data)
{
	int ret_val = QLA_SUCCESS;

	ret_val = qla8044_wr_reg_indirect(vha, QLA8044_FLASH_ADDR,
	    0x00800000 | (addr >> 2));
	if (ret_val) {
		ql_log(ql_log_warn, vha, 0xb134,
		    "%s: Failed write to FLASH_ADDR.\n", __func__);
		goto exit_func;
	}
	ret_val = qla8044_wr_reg_indirect(vha, QLA8044_FLASH_WRDATA, *p_data);
	if (ret_val) {
		ql_log(ql_log_warn, vha, 0xb135,
		    "%s: Failed write to FLASH_WRDATA.\n", __func__);
		goto exit_func;
	}
	ret_val = qla8044_wr_reg_indirect(vha, QLA8044_FLASH_CONTROL, 0x3D);
	if (ret_val) {
		ql_log(ql_log_warn, vha, 0xb136,
		    "%s: Failed write to FLASH_CONTROL.\n", __func__);
		goto exit_func;
	}
	ret_val = qla8044_poll_flash_status_reg(vha);
	if (ret_val) {
		ql_log(ql_log_warn, vha, 0xb137,
		    "%s: Poll flash status failed.\n", __func__);
	}

exit_func:
	return ret_val;
}

static int
qla8044_write_flash_buffer_mode(scsi_qla_host_t *vha, uint32_t *dwptr,
				uint32_t faddr, uint32_t dwords)
{
	int ret = QLA_FUNCTION_FAILED;
	uint32_t spi_val;

	if (dwords < QLA8044_MIN_OPTROM_BURST_DWORDS ||
	    dwords > QLA8044_MAX_OPTROM_BURST_DWORDS) {
		ql_dbg(ql_dbg_user, vha, 0xb123,
		    "Got unsupported dwords = 0x%x.\n",
		    dwords);
		return QLA_FUNCTION_FAILED;
	}

	qla8044_rd_reg_indirect(vha, QLA8044_FLASH_SPI_CONTROL, &spi_val);
	qla8044_wr_reg_indirect(vha, QLA8044_FLASH_SPI_CONTROL,
	    spi_val | QLA8044_FLASH_SPI_CTL);
	qla8044_wr_reg_indirect(vha, QLA8044_FLASH_ADDR,
	    QLA8044_FLASH_FIRST_TEMP_VAL);

	/* First DWORD write to FLASH_WRDATA */
	ret = qla8044_wr_reg_indirect(vha, QLA8044_FLASH_WRDATA,
	    *dwptr++);
	qla8044_wr_reg_indirect(vha, QLA8044_FLASH_CONTROL,
	    QLA8044_FLASH_FIRST_MS_PATTERN);

	ret = qla8044_poll_flash_status_reg(vha);
	if (ret) {
		ql_log(ql_log_warn, vha, 0xb124,
		    "%s: Failed.\n", __func__);
		goto exit_func;
	}

	dwords--;

	qla8044_wr_reg_indirect(vha, QLA8044_FLASH_ADDR,
	    QLA8044_FLASH_SECOND_TEMP_VAL);


	/* Second to N-1 DWORDS writes */
	while (dwords != 1) {
		qla8044_wr_reg_indirect(vha, QLA8044_FLASH_WRDATA, *dwptr++);
		qla8044_wr_reg_indirect(vha, QLA8044_FLASH_CONTROL,
		    QLA8044_FLASH_SECOND_MS_PATTERN);
		ret = qla8044_poll_flash_status_reg(vha);
		if (ret) {
			ql_log(ql_log_warn, vha, 0xb129,
			    "%s: Failed.\n", __func__);
			goto exit_func;
		}
		dwords--;
	}

	qla8044_wr_reg_indirect(vha, QLA8044_FLASH_ADDR,
	    QLA8044_FLASH_FIRST_TEMP_VAL | (faddr >> 2));

	/* Last DWORD write */
	qla8044_wr_reg_indirect(vha, QLA8044_FLASH_WRDATA, *dwptr++);
	qla8044_wr_reg_indirect(vha, QLA8044_FLASH_CONTROL,
	    QLA8044_FLASH_LAST_MS_PATTERN);
	ret = qla8044_poll_flash_status_reg(vha);
	if (ret) {
		ql_log(ql_log_warn, vha, 0xb12a,
		    "%s: Failed.\n", __func__);
		goto exit_func;
	}
	qla8044_rd_reg_indirect(vha, QLA8044_FLASH_SPI_STATUS, &spi_val);

	if ((spi_val & QLA8044_FLASH_SPI_CTL) == QLA8044_FLASH_SPI_CTL) {
		ql_log(ql_log_warn, vha, 0xb12b,
		    "%s: Failed.\n", __func__);
		spi_val = 0;
		/* Operation failed, clear error bit. */
		qla8044_rd_reg_indirect(vha, QLA8044_FLASH_SPI_CONTROL,
		    &spi_val);
		qla8044_wr_reg_indirect(vha, QLA8044_FLASH_SPI_CONTROL,
		    spi_val | QLA8044_FLASH_SPI_CTL);
	}
exit_func:
	return ret;
}

static int
qla8044_write_flash_dword_mode(scsi_qla_host_t *vha, uint32_t *dwptr,
			       uint32_t faddr, uint32_t dwords)
{
	int ret = QLA_FUNCTION_FAILED;
	uint32_t liter;

	for (liter = 0; liter < dwords; liter++, faddr += 4, dwptr++) {
		ret = qla8044_flash_write_u32(vha, faddr, dwptr);
		if (ret) {
			ql_dbg(ql_dbg_p3p, vha, 0xb141,
			    "%s: flash address=%x data=%x.\n", __func__,
			     faddr, *dwptr);
			break;
		}
	}

	return ret;
}

int
qla8044_write_optrom_data(struct scsi_qla_host *vha, uint8_t *buf,
			  uint32_t offset, uint32_t length)
{
	int rval = QLA_FUNCTION_FAILED, i, burst_iter_count;
	int dword_count, erase_sec_count;
	uint32_t erase_offset;
	uint8_t *p_cache, *p_src;

	erase_offset = offset;

	p_cache = kcalloc(length, sizeof(uint8_t), GFP_KERNEL);
	if (!p_cache)
		return QLA_FUNCTION_FAILED;

	memcpy(p_cache, buf, length);
	p_src = p_cache;
	dword_count = length / sizeof(uint32_t);
	/* Since the offset and legth are sector aligned, it will be always
	 * multiple of burst_iter_count (64)
	 */
	burst_iter_count = dword_count / QLA8044_MAX_OPTROM_BURST_DWORDS;
	erase_sec_count = length / QLA8044_SECTOR_SIZE;

	/* Suspend HBA. */
	scsi_block_requests(vha->host);
	/* Lock and enable write for whole operation. */
	qla8044_flash_lock(vha);
	qla8044_unprotect_flash(vha);

	/* Erasing the sectors */
	for (i = 0; i < erase_sec_count; i++) {
		rval = qla8044_erase_flash_sector(vha, erase_offset);
		ql_dbg(ql_dbg_user, vha, 0xb138,
		    "Done erase of sector=0x%x.\n",
		    erase_offset);
		if (rval) {
			ql_log(ql_log_warn, vha, 0xb121,
			    "Failed to erase the sector having address: "
			    "0x%x.\n", erase_offset);
			goto out;
		}
		erase_offset += QLA8044_SECTOR_SIZE;
	}
	ql_dbg(ql_dbg_user, vha, 0xb13f,
	    "Got write for addr = 0x%x length=0x%x.\n",
	    offset, length);

	for (i = 0; i < burst_iter_count; i++) {

		/* Go with write. */
		rval = qla8044_write_flash_buffer_mode(vha, (uint32_t *)p_src,
		    offset, QLA8044_MAX_OPTROM_BURST_DWORDS);
		if (rval) {
			/* Buffer Mode failed skip to dword mode */
			ql_log(ql_log_warn, vha, 0xb122,
			    "Failed to write flash in buffer mode, "
			    "Reverting to slow-write.\n");
			rval = qla8044_write_flash_dword_mode(vha,
			    (uint32_t *)p_src, offset,
			    QLA8044_MAX_OPTROM_BURST_DWORDS);
		}
		p_src +=  sizeof(uint32_t) * QLA8044_MAX_OPTROM_BURST_DWORDS;
		offset += sizeof(uint32_t) * QLA8044_MAX_OPTROM_BURST_DWORDS;
	}
	ql_dbg(ql_dbg_user, vha, 0xb133,
	    "Done writing.\n");

out:
	qla8044_protect_flash(vha);
	qla8044_flash_unlock(vha);
	scsi_unblock_requests(vha->host);
	kfree(p_cache);

	return rval;
}

#define LEG_INT_PTR_B31		(1 << 31)
#define LEG_INT_PTR_B30		(1 << 30)
#define PF_BITS_MASK		(0xF << 16)
/**
 * qla8044_intr_handler() - Process interrupts for the ISP8044
 * @irq:
 * @dev_id: SCSI driver HA context
 *
 * Called by system whenever the host adapter generates an interrupt.
 *
 * Returns handled flag.
 */
irqreturn_t
qla8044_intr_handler(int irq, void *dev_id)
{
	scsi_qla_host_t	*vha;
	struct qla_hw_data *ha;
	struct rsp_que *rsp;
	struct device_reg_82xx __iomem *reg;
	int		status = 0;
	unsigned long	flags;
	unsigned long	iter;
	uint32_t	stat;
	uint16_t	mb[4];
	uint32_t leg_int_ptr = 0, pf_bit;

	rsp = (struct rsp_que *) dev_id;
	if (!rsp) {
		ql_log(ql_log_info, NULL, 0xb143,
		    "%s(): NULL response queue pointer\n", __func__);
		return IRQ_NONE;
	}
	ha = rsp->hw;
	vha = pci_get_drvdata(ha->pdev);

	if (unlikely(pci_channel_offline(ha->pdev)))
		return IRQ_HANDLED;

	leg_int_ptr = qla8044_rd_reg(ha, LEG_INTR_PTR_OFFSET);

	/* Legacy interrupt is valid if bit31 of leg_int_ptr is set */
	if (!(leg_int_ptr & (LEG_INT_PTR_B31))) {
		ql_dbg(ql_dbg_p3p, vha, 0xb144,
		    "%s: Legacy Interrupt Bit 31 not set, "
		    "spurious interrupt!\n", __func__);
		return IRQ_NONE;
	}

	pf_bit = ha->portnum << 16;
	/* Validate the PCIE function ID set in leg_int_ptr bits [19..16] */
	if ((leg_int_ptr & (PF_BITS_MASK)) != pf_bit) {
		ql_dbg(ql_dbg_p3p, vha, 0xb145,
		    "%s: Incorrect function ID 0x%x in "
		    "legacy interrupt register, "
		    "ha->pf_bit = 0x%x\n", __func__,
		    (leg_int_ptr & (PF_BITS_MASK)), pf_bit);
		return IRQ_NONE;
	}

	/* To de-assert legacy interrupt, write 0 to Legacy Interrupt Trigger
	 * Control register and poll till Legacy Interrupt Pointer register
	 * bit32 is 0.
	 */
	qla8044_wr_reg(ha, LEG_INTR_TRIG_OFFSET, 0);
	do {
		leg_int_ptr = qla8044_rd_reg(ha, LEG_INTR_PTR_OFFSET);
		if ((leg_int_ptr & (PF_BITS_MASK)) != pf_bit)
			break;
	} while (leg_int_ptr & (LEG_INT_PTR_B30));

	reg = &ha->iobase->isp82;
	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (iter = 1; iter--; ) {

		if (RD_REG_DWORD(&reg->host_int)) {
			stat = RD_REG_DWORD(&reg->host_status);
			if ((stat & HSRX_RISC_INT) == 0)
				break;

			switch (stat & 0xff) {
			case 0x1:
			case 0x2:
			case 0x10:
			case 0x11:
				qla82xx_mbx_completion(vha, MSW(stat));
				status |= MBX_INTERRUPT;
				break;
			case 0x12:
				mb[0] = MSW(stat);
				mb[1] = RD_REG_WORD(&reg->mailbox_out[1]);
				mb[2] = RD_REG_WORD(&reg->mailbox_out[2]);
				mb[3] = RD_REG_WORD(&reg->mailbox_out[3]);
				qla2x00_async_event(vha, rsp, mb);
				break;
			case 0x13:
				qla24xx_process_response_queue(vha, rsp);
				break;
			default:
				ql_dbg(ql_dbg_p3p, vha, 0xb146,
				    "Unrecognized interrupt type "
				    "(%d).\n", stat & 0xff);
				break;
			}
		}
		WRT_REG_DWORD(&reg->host_int, 0);
	}

	qla2x00_handle_mbx_completion(ha, status);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	return IRQ_HANDLED;
}

static int
qla8044_idc_dontreset(struct qla_hw_data *ha)
{
	uint32_t idc_ctrl;

	idc_ctrl = qla8044_rd_reg(ha, QLA8044_IDC_DRV_CTRL);
	return idc_ctrl & DONTRESET_BIT0;
}

static void
qla8044_clear_rst_ready(scsi_qla_host_t *vha)
{
	uint32_t drv_state;

	drv_state = qla8044_rd_direct(vha, QLA8044_CRB_DRV_STATE_INDEX);

	/*
	 * For ISP8044, drv_active register has 1 bit per function,
	 * shift 1 by func_num to set a bit for the function.
	 * For ISP82xx, drv_active has 4 bits per function
	 */
	drv_state &= ~(1 << vha->hw->portnum);

	ql_dbg(ql_dbg_p3p, vha, 0xb13d,
	    "drv_state: 0x%08x\n", drv_state);
	qla8044_wr_direct(vha, QLA8044_CRB_DRV_STATE_INDEX, drv_state);
}

int
qla8044_abort_isp(scsi_qla_host_t *vha)
{
	int rval;
	uint32_t dev_state;
	struct qla_hw_data *ha = vha->hw;

	qla8044_idc_lock(ha);
	dev_state = qla8044_rd_direct(vha, QLA8044_CRB_DEV_STATE_INDEX);

	if (ql2xdontresethba)
		qla8044_set_idc_dontreset(vha);

	/* If device_state is NEED_RESET, go ahead with
	 * Reset,irrespective of ql2xdontresethba. This is to allow a
	 * non-reset-owner to force a reset. Non-reset-owner sets
	 * the IDC_CTRL BIT0 to prevent Reset-owner from doing a Reset
	 * and then forces a Reset by setting device_state to
	 * NEED_RESET. */
	if (dev_state == QLA8XXX_DEV_READY) {
		/* If IDC_CTRL DONTRESETHBA_BIT0 is set don't do reset
		 * recovery */
		if (qla8044_idc_dontreset(ha) == DONTRESET_BIT0) {
			ql_dbg(ql_dbg_p3p, vha, 0xb13e,
			    "Reset recovery disabled\n");
			rval = QLA_FUNCTION_FAILED;
			goto exit_isp_reset;
		}

		ql_dbg(ql_dbg_p3p, vha, 0xb140,
		    "HW State: NEED RESET\n");
		qla8044_wr_direct(vha, QLA8044_CRB_DEV_STATE_INDEX,
		    QLA8XXX_DEV_NEED_RESET);
	}

	/* For ISP8044, Reset owner is NIC, iSCSI or FCOE based on priority
	 * and which drivers are present. Unlike ISP82XX, the function setting
	 * NEED_RESET, may not be the Reset owner. */
	qla83xx_reset_ownership(vha);

	qla8044_idc_unlock(ha);
	rval = qla8044_device_state_handler(vha);
	qla8044_idc_lock(ha);
	qla8044_clear_rst_ready(vha);

exit_isp_reset:
	qla8044_idc_unlock(ha);
	if (rval == QLA_SUCCESS) {
		ha->flags.isp82xx_fw_hung = 0;
		ha->flags.nic_core_reset_hdlr_active = 0;
		rval = qla82xx_restart_isp(vha);
	}

	return rval;
}

void
qla8044_fw_dump(scsi_qla_host_t *vha, int hardware_locked)
{
	struct qla_hw_data *ha = vha->hw;

	if (!ha->allow_cna_fw_dump)
		return;

	scsi_block_requests(vha->host);
	ha->flags.isp82xx_no_md_cap = 1;
	qla8044_idc_lock(ha);
	qla82xx_set_reset_owner(vha);
	qla8044_idc_unlock(ha);
	qla2x00_wait_for_chip_reset(vha);
	scsi_unblock_requests(vha->host);
}
