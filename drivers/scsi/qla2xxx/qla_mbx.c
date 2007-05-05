/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"

#include <linux/delay.h>

static void
qla2x00_mbx_sem_timeout(unsigned long data)
{
	struct semaphore	*sem_ptr = (struct semaphore *)data;

	DEBUG11(printk("qla2x00_sem_timeout: entered.\n"));

	if (sem_ptr != NULL) {
		up(sem_ptr);
	}

	DEBUG11(printk("qla2x00_mbx_sem_timeout: exiting.\n"));
}

/*
 * qla2x00_mailbox_command
 *	Issue mailbox command and waits for completion.
 *
 * Input:
 *	ha = adapter block pointer.
 *	mcp = driver internal mbx struct pointer.
 *
 * Output:
 *	mb[MAX_MAILBOX_REGISTER_COUNT] = returned mailbox data.
 *
 * Returns:
 *	0 : QLA_SUCCESS = cmd performed success
 *	1 : QLA_FUNCTION_FAILED   (error encountered)
 *	6 : QLA_FUNCTION_TIMEOUT (timeout condition encountered)
 *
 * Context:
 *	Kernel context.
 */
static int
qla2x00_mailbox_command(scsi_qla_host_t *ha, mbx_cmd_t *mcp)
{
	int		rval;
	unsigned long    flags = 0;
	device_reg_t __iomem *reg = ha->iobase;
	struct timer_list	tmp_intr_timer;
	uint8_t		abort_active;
	uint8_t		io_lock_on = ha->flags.init_done;
	uint16_t	command;
	uint16_t	*iptr;
	uint16_t __iomem *optr;
	uint32_t	cnt;
	uint32_t	mboxes;
	unsigned long	wait_time;

	rval = QLA_SUCCESS;
	abort_active = test_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags);

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	/*
	 * Wait for active mailbox commands to finish by waiting at most tov
	 * seconds. This is to serialize actual issuing of mailbox cmds during
	 * non ISP abort time.
	 */
	if (!abort_active) {
		if (qla2x00_down_timeout(&ha->mbx_cmd_sem, mcp->tov * HZ)) {
			/* Timeout occurred. Return error. */
			DEBUG2_3_11(printk("%s(%ld): cmd access timeout. "
			    "Exiting.\n", __func__, ha->host_no));
			return QLA_FUNCTION_TIMEOUT;
		}
	}

	ha->flags.mbox_busy = 1;
	/* Save mailbox command for debug */
	ha->mcp = mcp;

	DEBUG11(printk("scsi(%ld): prepare to issue mbox cmd=0x%x.\n",
	    ha->host_no, mcp->mb[0]));

	spin_lock_irqsave(&ha->hardware_lock, flags);

	/* Load mailbox registers. */
	if (IS_QLA24XX(ha) || IS_QLA54XX(ha))
		optr = (uint16_t __iomem *)&reg->isp24.mailbox0;
	else
		optr = (uint16_t __iomem *)MAILBOX_REG(ha, &reg->isp, 0);

	iptr = mcp->mb;
	command = mcp->mb[0];
	mboxes = mcp->out_mb;

	for (cnt = 0; cnt < ha->mbx_count; cnt++) {
		if (IS_QLA2200(ha) && cnt == 8)
			optr =
			    (uint16_t __iomem *)MAILBOX_REG(ha, &reg->isp, 8);
		if (mboxes & BIT_0)
			WRT_REG_WORD(optr, *iptr);

		mboxes >>= 1;
		optr++;
		iptr++;
	}

#if defined(QL_DEBUG_LEVEL_1)
	printk("%s(%ld): Loaded MBX registers (displayed in bytes) = \n",
	    __func__, ha->host_no);
	qla2x00_dump_buffer((uint8_t *)mcp->mb, 16);
	printk("\n");
	qla2x00_dump_buffer(((uint8_t *)mcp->mb + 0x10), 16);
	printk("\n");
	qla2x00_dump_buffer(((uint8_t *)mcp->mb + 0x20), 8);
	printk("\n");
	printk("%s(%ld): I/O address = %p.\n", __func__, ha->host_no, optr);
	qla2x00_dump_regs(ha);
#endif

	/* Issue set host interrupt command to send cmd out. */
	ha->flags.mbox_int = 0;
	clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

	/* Unlock mbx registers and wait for interrupt */
	DEBUG11(printk("%s(%ld): going to unlock irq & waiting for interrupt. "
	    "jiffies=%lx.\n", __func__, ha->host_no, jiffies));

	/* Wait for mbx cmd completion until timeout */

	if (!abort_active && io_lock_on) {
		/* sleep on completion semaphore */
		DEBUG11(printk("%s(%ld): INTERRUPT MODE. Initializing timer.\n",
		    __func__, ha->host_no));

		init_timer(&tmp_intr_timer);
		tmp_intr_timer.data = (unsigned long)&ha->mbx_intr_sem;
		tmp_intr_timer.expires = jiffies + mcp->tov * HZ;
		tmp_intr_timer.function =
		    (void (*)(unsigned long))qla2x00_mbx_sem_timeout;

		DEBUG11(printk("%s(%ld): Adding timer.\n", __func__,
		    ha->host_no));
		add_timer(&tmp_intr_timer);

		DEBUG11(printk("%s(%ld): going to unlock & sleep. "
		    "time=0x%lx.\n", __func__, ha->host_no, jiffies));

		set_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags);

		if (IS_QLA24XX(ha) || IS_QLA54XX(ha))
			WRT_REG_DWORD(&reg->isp24.hccr, HCCRX_SET_HOST_INT);
		else
			WRT_REG_WORD(&reg->isp.hccr, HCCR_SET_HOST_INT);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		/* Wait for either the timer to expire
		 * or the mbox completion interrupt
		 */
		down(&ha->mbx_intr_sem);

		DEBUG11(printk("%s(%ld): waking up. time=0x%lx\n", __func__,
		    ha->host_no, jiffies));
		clear_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags);

		/* delete the timer */
		del_timer(&tmp_intr_timer);
	} else {
		DEBUG3_11(printk("%s(%ld): cmd=%x POLLING MODE.\n", __func__,
		    ha->host_no, command));

		if (IS_QLA24XX(ha) || IS_QLA54XX(ha))
			WRT_REG_DWORD(&reg->isp24.hccr, HCCRX_SET_HOST_INT);
		else
			WRT_REG_WORD(&reg->isp.hccr, HCCR_SET_HOST_INT);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		wait_time = jiffies + mcp->tov * HZ; /* wait at most tov secs */
		while (!ha->flags.mbox_int) {
			if (time_after(jiffies, wait_time))
				break;

			/* Check for pending interrupts. */
			qla2x00_poll(ha);

			if (command != MBC_LOAD_RISC_RAM_EXTENDED &&
			    !ha->flags.mbox_int)
				msleep(10);
		} /* while */
	}

	/* Check whether we timed out */
	if (ha->flags.mbox_int) {
		uint16_t *iptr2;

		DEBUG3_11(printk("%s(%ld): cmd %x completed.\n", __func__,
		    ha->host_no, command));

		/* Got interrupt. Clear the flag. */
		ha->flags.mbox_int = 0;
		clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

		if (ha->mailbox_out[0] != MBS_COMMAND_COMPLETE)
			rval = QLA_FUNCTION_FAILED;

		/* Load return mailbox registers. */
		iptr2 = mcp->mb;
		iptr = (uint16_t *)&ha->mailbox_out[0];
		mboxes = mcp->in_mb;
		for (cnt = 0; cnt < ha->mbx_count; cnt++) {
			if (mboxes & BIT_0)
				*iptr2 = *iptr;

			mboxes >>= 1;
			iptr2++;
			iptr++;
		}
	} else {

#if defined(QL_DEBUG_LEVEL_2) || defined(QL_DEBUG_LEVEL_3) || \
		defined(QL_DEBUG_LEVEL_11)
		uint16_t mb0;
		uint32_t ictrl;

		if (IS_QLA24XX(ha) || IS_QLA54XX(ha)) {
			mb0 = RD_REG_WORD(&reg->isp24.mailbox0);
			ictrl = RD_REG_DWORD(&reg->isp24.ictrl);
		} else {
			mb0 = RD_MAILBOX_REG(ha, &reg->isp, 0);
			ictrl = RD_REG_WORD(&reg->isp.ictrl);
		}
		printk("%s(%ld): **** MB Command Timeout for cmd %x ****\n",
		    __func__, ha->host_no, command);
		printk("%s(%ld): icontrol=%x jiffies=%lx\n", __func__,
		    ha->host_no, ictrl, jiffies);
		printk("%s(%ld): *** mailbox[0] = 0x%x ***\n", __func__,
		    ha->host_no, mb0);
		qla2x00_dump_regs(ha);
#endif

		rval = QLA_FUNCTION_TIMEOUT;
	}

	ha->flags.mbox_busy = 0;

	/* Clean up */
	ha->mcp = NULL;

	if (!abort_active) {
		DEBUG11(printk("%s(%ld): checking for additional resp "
		    "interrupt.\n", __func__, ha->host_no));

		/* polling mode for non isp_abort commands. */
		qla2x00_poll(ha);
	}

	if (rval == QLA_FUNCTION_TIMEOUT &&
	    mcp->mb[0] != MBC_GEN_SYSTEM_ERROR) {
		if (!io_lock_on || (mcp->flags & IOCTL_CMD)) {
			/* not in dpc. schedule it for dpc to take over. */
			DEBUG(printk("%s(%ld): timeout schedule "
			    "isp_abort_needed.\n", __func__, ha->host_no));
			DEBUG2_3_11(printk("%s(%ld): timeout schedule "
			    "isp_abort_needed.\n", __func__, ha->host_no));
			qla_printk(KERN_WARNING, ha,
			    "Mailbox command timeout occured. Scheduling ISP "
			    "abort.\n");
			set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
			qla2xxx_wake_dpc(ha);
		} else if (!abort_active) {
			/* call abort directly since we are in the DPC thread */
			DEBUG(printk("%s(%ld): timeout calling abort_isp\n",
			    __func__, ha->host_no));
			DEBUG2_3_11(printk("%s(%ld): timeout calling "
			    "abort_isp\n", __func__, ha->host_no));
			qla_printk(KERN_WARNING, ha,
			    "Mailbox command timeout occured. Issuing ISP "
			    "abort.\n");

			set_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags);
			clear_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
			if (qla2x00_abort_isp(ha)) {
				/* Failed. retry later. */
				set_bit(ISP_ABORT_NEEDED, &ha->dpc_flags);
			}
			clear_bit(ABORT_ISP_ACTIVE, &ha->dpc_flags);
			DEBUG(printk("%s(%ld): finished abort_isp\n", __func__,
			    ha->host_no));
			DEBUG2_3_11(printk("%s(%ld): finished abort_isp\n",
			    __func__, ha->host_no));
		}
	}

	/* Allow next mbx cmd to come in. */
	if (!abort_active)
		up(&ha->mbx_cmd_sem);

	if (rval) {
		DEBUG2_3_11(printk("%s(%ld): **** FAILED. mbx0=%x, mbx1=%x, "
		    "mbx2=%x, cmd=%x ****\n", __func__, ha->host_no,
		    mcp->mb[0], mcp->mb[1], mcp->mb[2], command));
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}

int
qla2x00_load_ram(scsi_qla_host_t *ha, dma_addr_t req_dma, uint32_t risc_addr,
    uint32_t risc_code_size)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	if (MSW(risc_addr) || IS_QLA24XX(ha) || IS_QLA54XX(ha)) {
		mcp->mb[0] = MBC_LOAD_RISC_RAM_EXTENDED;
		mcp->mb[8] = MSW(risc_addr);
		mcp->out_mb = MBX_8|MBX_0;
	} else {
		mcp->mb[0] = MBC_LOAD_RISC_RAM;
		mcp->out_mb = MBX_0;
	}
	mcp->mb[1] = LSW(risc_addr);
	mcp->mb[2] = MSW(req_dma);
	mcp->mb[3] = LSW(req_dma);
	mcp->mb[6] = MSW(MSD(req_dma));
	mcp->mb[7] = LSW(MSD(req_dma));
	mcp->out_mb |= MBX_7|MBX_6|MBX_3|MBX_2|MBX_1;
	if (IS_QLA24XX(ha) || IS_QLA54XX(ha)) {
		mcp->mb[4] = MSW(risc_code_size);
		mcp->mb[5] = LSW(risc_code_size);
		mcp->out_mb |= MBX_5|MBX_4;
	} else {
		mcp->mb[4] = LSW(risc_code_size);
		mcp->out_mb |= MBX_4;
	}

	mcp->in_mb = MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed=%x mb[0]=%x.\n", __func__,
		    ha->host_no, rval, mcp->mb[0]));
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}

/*
 * qla2x00_execute_fw
 *     Start adapter firmware.
 *
 * Input:
 *     ha = adapter block pointer.
 *     TARGET_QUEUE_LOCK must be released.
 *     ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *     qla2x00 local function return status code.
 *
 * Context:
 *     Kernel context.
 */
int
qla2x00_execute_fw(scsi_qla_host_t *ha, uint32_t risc_addr)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_EXECUTE_FIRMWARE;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_0;
	if (IS_QLA24XX(ha) || IS_QLA54XX(ha)) {
		mcp->mb[1] = MSW(risc_addr);
		mcp->mb[2] = LSW(risc_addr);
		mcp->mb[3] = 0;
		mcp->out_mb |= MBX_3|MBX_2|MBX_1;
		mcp->in_mb |= MBX_1;
	} else {
		mcp->mb[1] = LSW(risc_addr);
		mcp->out_mb |= MBX_1;
		if (IS_QLA2322(ha) || IS_QLA6322(ha)) {
			mcp->mb[2] = 0;
			mcp->out_mb |= MBX_2;
		}
	}

	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed=%x mb[0]=%x.\n", __func__,
		    ha->host_no, rval, mcp->mb[0]));
	} else {
		if (IS_QLA24XX(ha) || IS_QLA54XX(ha)) {
			DEBUG11(printk("%s(%ld): done exchanges=%x.\n",
			    __func__, ha->host_no, mcp->mb[1]));
		} else {
			DEBUG11(printk("%s(%ld): done.\n", __func__,
			    ha->host_no));
		}
	}

	return rval;
}

/*
 * qla2x00_get_fw_version
 *	Get firmware version.
 *
 * Input:
 *	ha:		adapter state pointer.
 *	major:		pointer for major number.
 *	minor:		pointer for minor number.
 *	subminor:	pointer for subminor number.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
void
qla2x00_get_fw_version(scsi_qla_host_t *ha, uint16_t *major, uint16_t *minor,
    uint16_t *subminor, uint16_t *attributes, uint32_t *memory)
{
	int		rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_GET_FIRMWARE_VERSION;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->flags = 0;
	mcp->tov = 30;
	rval = qla2x00_mailbox_command(ha, mcp);

	/* Return mailbox data. */
	*major = mcp->mb[1];
	*minor = mcp->mb[2];
	*subminor = mcp->mb[3];
	*attributes = mcp->mb[6];
	if (IS_QLA2100(ha) || IS_QLA2200(ha))
		*memory = 0x1FFFF;			/* Defaults to 128KB. */
	else
		*memory = (mcp->mb[5] << 16) | mcp->mb[4];

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("%s(%ld): failed=%x.\n", __func__,
		    ha->host_no, rval));
	} else {
		/*EMPTY*/
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}
}

/*
 * qla2x00_get_fw_options
 *	Set firmware options.
 *
 * Input:
 *	ha = adapter block pointer.
 *	fwopt = pointer for firmware options.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_get_fw_options(scsi_qla_host_t *ha, uint16_t *fwopts)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_GET_FIRMWARE_OPTION;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("%s(%ld): failed=%x.\n", __func__,
		    ha->host_no, rval));
	} else {
		fwopts[0] = mcp->mb[0];
		fwopts[1] = mcp->mb[1];
		fwopts[2] = mcp->mb[2];
		fwopts[3] = mcp->mb[3];

		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}


/*
 * qla2x00_set_fw_options
 *	Set firmware options.
 *
 * Input:
 *	ha = adapter block pointer.
 *	fwopt = pointer for firmware options.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_set_fw_options(scsi_qla_host_t *ha, uint16_t *fwopts)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_SET_FIRMWARE_OPTION;
	mcp->mb[1] = fwopts[1];
	mcp->mb[2] = fwopts[2];
	mcp->mb[3] = fwopts[3];
	mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	if (IS_QLA24XX(ha) || IS_QLA54XX(ha)) {
		mcp->in_mb |= MBX_1;
	} else {
		mcp->mb[10] = fwopts[10];
		mcp->mb[11] = fwopts[11];
		mcp->mb[12] = 0;	/* Undocumented, but used */
		mcp->out_mb |= MBX_12|MBX_11|MBX_10;
	}
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	fwopts[0] = mcp->mb[0];

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("%s(%ld): failed=%x (%x/%x).\n", __func__,
		    ha->host_no, rval, mcp->mb[0], mcp->mb[1]));
	} else {
		/*EMPTY*/
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}

/*
 * qla2x00_mbx_reg_test
 *	Mailbox register wrap test.
 *
 * Input:
 *	ha = adapter block pointer.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_mbx_reg_test(scsi_qla_host_t *ha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_mbx_reg_test(%ld): entered.\n", ha->host_no));

	mcp->mb[0] = MBC_MAILBOX_REGISTER_TEST;
	mcp->mb[1] = 0xAAAA;
	mcp->mb[2] = 0x5555;
	mcp->mb[3] = 0xAA55;
	mcp->mb[4] = 0x55AA;
	mcp->mb[5] = 0xA5A5;
	mcp->mb[6] = 0x5A5A;
	mcp->mb[7] = 0x2525;
	mcp->out_mb = MBX_7|MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_7|MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval == QLA_SUCCESS) {
		if (mcp->mb[1] != 0xAAAA || mcp->mb[2] != 0x5555 ||
		    mcp->mb[3] != 0xAA55 || mcp->mb[4] != 0x55AA)
			rval = QLA_FUNCTION_FAILED;
		if (mcp->mb[5] != 0xA5A5 || mcp->mb[6] != 0x5A5A ||
		    mcp->mb[7] != 0x2525)
			rval = QLA_FUNCTION_FAILED;
	}

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_mbx_reg_test(%ld): failed=%x.\n",
		    ha->host_no, rval));
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_mbx_reg_test(%ld): done.\n",
		    ha->host_no));
	}

	return rval;
}

/*
 * qla2x00_verify_checksum
 *	Verify firmware checksum.
 *
 * Input:
 *	ha = adapter block pointer.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_verify_checksum(scsi_qla_host_t *ha, uint32_t risc_addr)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_VERIFY_CHECKSUM;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_0;
	if (IS_QLA24XX(ha) || IS_QLA54XX(ha)) {
		mcp->mb[1] = MSW(risc_addr);
		mcp->mb[2] = LSW(risc_addr);
		mcp->out_mb |= MBX_2|MBX_1;
		mcp->in_mb |= MBX_2|MBX_1;
	} else {
		mcp->mb[1] = LSW(risc_addr);
		mcp->out_mb |= MBX_1;
		mcp->in_mb |= MBX_1;
	}

	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed=%x chk sum=%x.\n", __func__,
		    ha->host_no, rval, (IS_QLA24XX(ha) || IS_QLA54XX(ha) ?
		    (mcp->mb[2] << 16) | mcp->mb[1]: mcp->mb[1])));
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}

/*
 * qla2x00_issue_iocb
 *	Issue IOCB using mailbox command
 *
 * Input:
 *	ha = adapter state pointer.
 *	buffer = buffer pointer.
 *	phys_addr = physical address of buffer.
 *	size = size of buffer.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_issue_iocb(scsi_qla_host_t *ha, void*  buffer, dma_addr_t phys_addr,
    size_t size)
{
	int		rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;

	mcp->mb[0] = MBC_IOCB_COMMAND_A64;
	mcp->mb[1] = 0;
	mcp->mb[2] = MSW(phys_addr);
	mcp->mb[3] = LSW(phys_addr);
	mcp->mb[6] = MSW(MSD(phys_addr));
	mcp->mb[7] = LSW(MSD(phys_addr));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_2|MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG(printk("qla2x00_issue_iocb(%ld): failed rval 0x%x\n",
		    ha->host_no, rval));
		DEBUG2(printk("qla2x00_issue_iocb(%ld): failed rval 0x%x\n",
		    ha->host_no, rval));
	} else {
		sts_entry_t *sts_entry = (sts_entry_t *) buffer;

		/* Mask reserved bits. */
		sts_entry->entry_status &=
		    IS_QLA24XX(ha) || IS_QLA54XX(ha) ? RF_MASK_24XX :RF_MASK;
	}

	return rval;
}

/*
 * qla2x00_abort_command
 *	Abort command aborts a specified IOCB.
 *
 * Input:
 *	ha = adapter block pointer.
 *	sp = SB structure pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_abort_command(scsi_qla_host_t *ha, srb_t *sp)
{
	unsigned long   flags = 0;
	fc_port_t	*fcport;
	int		rval;
	uint32_t	handle;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;

	DEBUG11(printk("qla2x00_abort_command(%ld): entered.\n", ha->host_no));

	fcport = sp->fcport;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (handle = 1; handle < MAX_OUTSTANDING_COMMANDS; handle++) {
		if (ha->outstanding_cmds[handle] == sp)
			break;
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (handle == MAX_OUTSTANDING_COMMANDS) {
		/* command not found */
		return QLA_FUNCTION_FAILED;
	}

	mcp->mb[0] = MBC_ABORT_COMMAND;
	if (HAS_EXTENDED_IDS(ha))
		mcp->mb[1] = fcport->loop_id;
	else
		mcp->mb[1] = fcport->loop_id << 8;
	mcp->mb[2] = (uint16_t)handle;
	mcp->mb[3] = (uint16_t)(handle >> 16);
	mcp->mb[6] = (uint16_t)sp->cmd->device->lun;
	mcp->out_mb = MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		DEBUG2_3_11(printk("qla2x00_abort_command(%ld): failed=%x.\n",
		    ha->host_no, rval));
	} else {
		sp->flags |= SRB_ABORT_PENDING;
		DEBUG11(printk("qla2x00_abort_command(%ld): done.\n",
		    ha->host_no));
	}

	return rval;
}

#if USE_ABORT_TGT
/*
 * qla2x00_abort_target
 *	Issue abort target mailbox command.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_abort_target(fc_port_t *fcport)
{
	int        rval;
	mbx_cmd_t  mc;
	mbx_cmd_t  *mcp = &mc;
	scsi_qla_host_t *ha;

	if (fcport == NULL)
		return 0;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, fcport->ha->host_no));

	ha = fcport->ha;
	mcp->mb[0] = MBC_ABORT_TARGET;
	mcp->out_mb = MBX_2|MBX_1|MBX_0;
	if (HAS_EXTENDED_IDS(ha)) {
		mcp->mb[1] = fcport->loop_id;
		mcp->mb[10] = 0;
		mcp->out_mb |= MBX_10;
	} else {
		mcp->mb[1] = fcport->loop_id << 8;
	}
	mcp->mb[2] = ha->loop_reset_delay;

	mcp->in_mb = MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	/* Issue marker command. */
	ha->marker_needed = 1;

	if (rval != QLA_SUCCESS) {
		DEBUG2_3_11(printk("qla2x00_abort_target(%ld): failed=%x.\n",
		    ha->host_no, rval));
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_abort_target(%ld): done.\n",
		    ha->host_no));
	}

	return rval;
}
#endif

/*
 * qla2x00_get_adapter_id
 *	Get adapter ID and topology.
 *
 * Input:
 *	ha = adapter block pointer.
 *	id = pointer for loop ID.
 *	al_pa = pointer for AL_PA.
 *	area = pointer for area.
 *	domain = pointer for domain.
 *	top = pointer for topology.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_get_adapter_id(scsi_qla_host_t *ha, uint16_t *id, uint8_t *al_pa,
    uint8_t *area, uint8_t *domain, uint16_t *top)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_get_adapter_id(%ld): entered.\n",
	    ha->host_no));

	mcp->mb[0] = MBC_GET_ADAPTER_LOOP_ID;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);
	if (mcp->mb[0] == MBS_COMMAND_ERROR)
		rval = QLA_COMMAND_ERROR;

	/* Return data. */
	*id = mcp->mb[1];
	*al_pa = LSB(mcp->mb[2]);
	*area = MSB(mcp->mb[2]);
	*domain	= LSB(mcp->mb[3]);
	*top = mcp->mb[6];

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_get_adapter_id(%ld): failed=%x.\n",
		    ha->host_no, rval));
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_get_adapter_id(%ld): done.\n",
		    ha->host_no));
	}

	return rval;
}

/*
 * qla2x00_get_retry_cnt
 *	Get current firmware login retry count and delay.
 *
 * Input:
 *	ha = adapter block pointer.
 *	retry_cnt = pointer to login retry count.
 *	tov = pointer to login timeout value.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_get_retry_cnt(scsi_qla_host_t *ha, uint8_t *retry_cnt, uint8_t *tov,
    uint16_t *r_a_tov)
{
	int rval;
	uint16_t ratov;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_get_retry_cnt(%ld): entered.\n",
			ha->host_no));

	mcp->mb[0] = MBC_GET_RETRY_COUNT;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_get_retry_cnt(%ld): failed = %x.\n",
		    ha->host_no, mcp->mb[0]));
	} else {
		/* Convert returned data and check our values. */
		*r_a_tov = mcp->mb[3] / 2;
		ratov = (mcp->mb[3]/2) / 10;  /* mb[3] value is in 100ms */
		if (mcp->mb[1] * ratov > (*retry_cnt) * (*tov)) {
			/* Update to the larger values */
			*retry_cnt = (uint8_t)mcp->mb[1];
			*tov = ratov;
		}

		DEBUG11(printk("qla2x00_get_retry_cnt(%ld): done. mb3=%d "
		    "ratov=%d.\n", ha->host_no, mcp->mb[3], ratov));
	}

	return rval;
}

/*
 * qla2x00_init_firmware
 *	Initialize adapter firmware.
 *
 * Input:
 *	ha = adapter block pointer.
 *	dptr = Initialization control block pointer.
 *	size = size of initialization control block.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_init_firmware(scsi_qla_host_t *ha, uint16_t size)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_init_firmware(%ld): entered.\n",
	    ha->host_no));

	mcp->mb[0] = MBC_INITIALIZE_FIRMWARE;
	mcp->mb[2] = MSW(ha->init_cb_dma);
	mcp->mb[3] = LSW(ha->init_cb_dma);
	mcp->mb[4] = 0;
	mcp->mb[5] = 0;
	mcp->mb[6] = MSW(MSD(ha->init_cb_dma));
	mcp->mb[7] = LSW(MSD(ha->init_cb_dma));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_0;
	mcp->in_mb = MBX_5|MBX_4|MBX_0;
	mcp->buf_size = size;
	mcp->flags = MBX_DMA_OUT;
	mcp->tov = 30;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_init_firmware(%ld): failed=%x "
		    "mb0=%x.\n",
		    ha->host_no, rval, mcp->mb[0]));
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_init_firmware(%ld): done.\n",
		    ha->host_no));
	}

	return rval;
}

/*
 * qla2x00_get_port_database
 *	Issue normal/enhanced get port database mailbox command
 *	and copy device name as necessary.
 *
 * Input:
 *	ha = adapter state pointer.
 *	dev = structure pointer.
 *	opt = enhanced cmd option byte.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_get_port_database(scsi_qla_host_t *ha, fc_port_t *fcport, uint8_t opt)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	port_database_t *pd;
	struct port_database_24xx *pd24;
	dma_addr_t pd_dma;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	pd24 = NULL;
	pd = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &pd_dma);
	if (pd  == NULL) {
		DEBUG2_3(printk("%s(%ld): failed to allocate Port Database "
		    "structure.\n", __func__, ha->host_no));
		return QLA_MEMORY_ALLOC_FAILED;
	}
	memset(pd, 0, max(PORT_DATABASE_SIZE, PORT_DATABASE_24XX_SIZE));

	mcp->mb[0] = MBC_GET_PORT_DATABASE;
	if (opt != 0 && !IS_QLA24XX(ha) && !IS_QLA54XX(ha))
		mcp->mb[0] = MBC_ENHANCED_GET_PORT_DATABASE;
	mcp->mb[2] = MSW(pd_dma);
	mcp->mb[3] = LSW(pd_dma);
	mcp->mb[6] = MSW(MSD(pd_dma));
	mcp->mb[7] = LSW(MSD(pd_dma));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_0;
	mcp->in_mb = MBX_0;
	if (IS_QLA24XX(ha) || IS_QLA54XX(ha)) {
		mcp->mb[1] = fcport->loop_id;
		mcp->mb[10] = opt;
		mcp->out_mb |= MBX_10|MBX_1;
		mcp->in_mb |= MBX_1;
	} else if (HAS_EXTENDED_IDS(ha)) {
		mcp->mb[1] = fcport->loop_id;
		mcp->mb[10] = opt;
		mcp->out_mb |= MBX_10|MBX_1;
	} else {
		mcp->mb[1] = fcport->loop_id << 8 | opt;
		mcp->out_mb |= MBX_1;
	}
	mcp->buf_size = (IS_QLA24XX(ha) || IS_QLA54XX(ha) ?
	    PORT_DATABASE_24XX_SIZE : PORT_DATABASE_SIZE);
	mcp->flags = MBX_DMA_IN;
	mcp->tov = (ha->login_timeout * 2) + (ha->login_timeout / 2);
	rval = qla2x00_mailbox_command(ha, mcp);
	if (rval != QLA_SUCCESS)
		goto gpd_error_out;

	if (IS_QLA24XX(ha) || IS_QLA54XX(ha)) {
		pd24 = (struct port_database_24xx *) pd;

		/* Check for logged in state. */
		if (pd24->current_login_state != PDS_PRLI_COMPLETE &&
		    pd24->last_login_state != PDS_PRLI_COMPLETE) {
			DEBUG2(printk("%s(%ld): Unable to verify "
			    "login-state (%x/%x) for loop_id %x\n",
			    __func__, ha->host_no,
			    pd24->current_login_state,
			    pd24->last_login_state, fcport->loop_id));
			rval = QLA_FUNCTION_FAILED;
			goto gpd_error_out;
		}

		/* Names are little-endian. */
		memcpy(fcport->node_name, pd24->node_name, WWN_SIZE);
		memcpy(fcport->port_name, pd24->port_name, WWN_SIZE);

		/* Get port_id of device. */
		fcport->d_id.b.domain = pd24->port_id[0];
		fcport->d_id.b.area = pd24->port_id[1];
		fcport->d_id.b.al_pa = pd24->port_id[2];
		fcport->d_id.b.rsvd_1 = 0;

		/* If not target must be initiator or unknown type. */
		if ((pd24->prli_svc_param_word_3[0] & BIT_4) == 0)
			fcport->port_type = FCT_INITIATOR;
		else
			fcport->port_type = FCT_TARGET;
	} else {
		/* Check for logged in state. */
		if (pd->master_state != PD_STATE_PORT_LOGGED_IN &&
		    pd->slave_state != PD_STATE_PORT_LOGGED_IN) {
			rval = QLA_FUNCTION_FAILED;
			goto gpd_error_out;
		}

		/* Names are little-endian. */
		memcpy(fcport->node_name, pd->node_name, WWN_SIZE);
		memcpy(fcport->port_name, pd->port_name, WWN_SIZE);

		/* Get port_id of device. */
		fcport->d_id.b.domain = pd->port_id[0];
		fcport->d_id.b.area = pd->port_id[3];
		fcport->d_id.b.al_pa = pd->port_id[2];
		fcport->d_id.b.rsvd_1 = 0;

		/* Check for device require authentication. */
		pd->common_features & BIT_5 ? (fcport->flags |= FCF_AUTH_REQ) :
		    (fcport->flags &= ~FCF_AUTH_REQ);

		/* If not target must be initiator or unknown type. */
		if ((pd->prli_svc_param_word_3[0] & BIT_4) == 0)
			fcport->port_type = FCT_INITIATOR;
		else
			fcport->port_type = FCT_TARGET;

		/* Passback COS information. */
		fcport->supported_classes = (pd->options & BIT_4) ?
		    FC_COS_CLASS2: FC_COS_CLASS3;
	}

gpd_error_out:
	dma_pool_free(ha->s_dma_pool, pd, pd_dma);

	if (rval != QLA_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed=%x mb[0]=%x mb[1]=%x.\n",
		    __func__, ha->host_no, rval, mcp->mb[0], mcp->mb[1]));
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}

/*
 * qla2x00_get_firmware_state
 *	Get adapter firmware state.
 *
 * Input:
 *	ha = adapter block pointer.
 *	dptr = pointer for firmware state.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_get_firmware_state(scsi_qla_host_t *ha, uint16_t *dptr)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_get_firmware_state(%ld): entered.\n",
	    ha->host_no));

	mcp->mb[0] = MBC_GET_FIRMWARE_STATE;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_2|MBX_1|MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	/* Return firmware state. */
	*dptr = mcp->mb[1];

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_get_firmware_state(%ld): "
		    "failed=%x.\n", ha->host_no, rval));
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_get_firmware_state(%ld): done.\n",
		    ha->host_no));
	}

	return rval;
}

/*
 * qla2x00_get_port_name
 *	Issue get port name mailbox command.
 *	Returned name is in big endian format.
 *
 * Input:
 *	ha = adapter block pointer.
 *	loop_id = loop ID of device.
 *	name = pointer for name.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_get_port_name(scsi_qla_host_t *ha, uint16_t loop_id, uint8_t *name,
    uint8_t opt)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_get_port_name(%ld): entered.\n",
	    ha->host_no));

	mcp->mb[0] = MBC_GET_PORT_NAME;
	mcp->out_mb = MBX_1|MBX_0;
	if (HAS_EXTENDED_IDS(ha)) {
		mcp->mb[1] = loop_id;
		mcp->mb[10] = opt;
		mcp->out_mb |= MBX_10;
	} else {
		mcp->mb[1] = loop_id << 8 | opt;
	}

	mcp->in_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_get_port_name(%ld): failed=%x.\n",
		    ha->host_no, rval));
	} else {
		if (name != NULL) {
			/* This function returns name in big endian. */
			name[0] = MSB(mcp->mb[2]);
			name[1] = LSB(mcp->mb[2]);
			name[2] = MSB(mcp->mb[3]);
			name[3] = LSB(mcp->mb[3]);
			name[4] = MSB(mcp->mb[6]);
			name[5] = LSB(mcp->mb[6]);
			name[6] = MSB(mcp->mb[7]);
			name[7] = LSB(mcp->mb[7]);
		}

		DEBUG11(printk("qla2x00_get_port_name(%ld): done.\n",
		    ha->host_no));
	}

	return rval;
}

/*
 * qla2x00_lip_reset
 *	Issue LIP reset mailbox command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_lip_reset(scsi_qla_host_t *ha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	if (IS_QLA24XX(ha) || IS_QLA54XX(ha)) {
		mcp->mb[0] = MBC_LIP_FULL_LOGIN;
		mcp->mb[1] = BIT_6;
		mcp->mb[2] = 0;
		mcp->mb[3] = ha->loop_reset_delay;
		mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	} else {
		mcp->mb[0] = MBC_LIP_RESET;
		mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
		if (HAS_EXTENDED_IDS(ha)) {
			mcp->mb[1] = 0x00ff;
			mcp->mb[10] = 0;
			mcp->out_mb |= MBX_10;
		} else {
			mcp->mb[1] = 0xff00;
		}
		mcp->mb[2] = ha->loop_reset_delay;
		mcp->mb[3] = 0;
	}
	mcp->in_mb = MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("%s(%ld): failed=%x.\n",
		    __func__, ha->host_no, rval));
	} else {
		/*EMPTY*/
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}

/*
 * qla2x00_send_sns
 *	Send SNS command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	sns = pointer for command.
 *	cmd_size = command size.
 *	buf_size = response/command size.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_send_sns(scsi_qla_host_t *ha, dma_addr_t sns_phys_address,
    uint16_t cmd_size, size_t buf_size)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_send_sns(%ld): entered.\n",
	    ha->host_no));

	DEBUG11(printk("qla2x00_send_sns: retry cnt=%d ratov=%d total "
	    "tov=%d.\n", ha->retry_count, ha->login_timeout, mcp->tov));

	mcp->mb[0] = MBC_SEND_SNS_COMMAND;
	mcp->mb[1] = cmd_size;
	mcp->mb[2] = MSW(sns_phys_address);
	mcp->mb[3] = LSW(sns_phys_address);
	mcp->mb[6] = MSW(MSD(sns_phys_address));
	mcp->mb[7] = LSW(MSD(sns_phys_address));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0|MBX_1;
	mcp->buf_size = buf_size;
	mcp->flags = MBX_DMA_OUT|MBX_DMA_IN;
	mcp->tov = (ha->login_timeout * 2) + (ha->login_timeout / 2);
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG(printk("qla2x00_send_sns(%ld): failed=%x mb[0]=%x "
		    "mb[1]=%x.\n", ha->host_no, rval, mcp->mb[0], mcp->mb[1]));
		DEBUG2_3_11(printk("qla2x00_send_sns(%ld): failed=%x mb[0]=%x "
		    "mb[1]=%x.\n", ha->host_no, rval, mcp->mb[0], mcp->mb[1]));
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_send_sns(%ld): done.\n", ha->host_no));
	}

	return rval;
}

int
qla24xx_login_fabric(scsi_qla_host_t *ha, uint16_t loop_id, uint8_t domain,
    uint8_t area, uint8_t al_pa, uint16_t *mb, uint8_t opt)
{
	int		rval;

	struct logio_entry_24xx *lg;
	dma_addr_t	lg_dma;
	uint32_t	iop[2];

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	lg = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &lg_dma);
	if (lg == NULL) {
		DEBUG2_3(printk("%s(%ld): failed to allocate Login IOCB.\n",
		    __func__, ha->host_no));
		return QLA_MEMORY_ALLOC_FAILED;
	}
	memset(lg, 0, sizeof(struct logio_entry_24xx));

	lg->entry_type = LOGINOUT_PORT_IOCB_TYPE;
	lg->entry_count = 1;
	lg->nport_handle = cpu_to_le16(loop_id);
	lg->control_flags = __constant_cpu_to_le16(LCF_COMMAND_PLOGI);
	if (opt & BIT_0)
		lg->control_flags |= __constant_cpu_to_le16(LCF_COND_PLOGI);
	if (opt & BIT_1)
		lg->control_flags |= __constant_cpu_to_le16(LCF_SKIP_PRLI);
	lg->port_id[0] = al_pa;
	lg->port_id[1] = area;
	lg->port_id[2] = domain;
	rval = qla2x00_issue_iocb(ha, lg, lg_dma, 0);
	if (rval != QLA_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed to issue Login IOCB "
		    "(%x).\n", __func__, ha->host_no, rval));
	} else if (lg->entry_status != 0) {
		DEBUG2_3_11(printk("%s(%ld): failed to complete IOCB "
		    "-- error status (%x).\n", __func__, ha->host_no,
		    lg->entry_status));
		rval = QLA_FUNCTION_FAILED;
	} else if (lg->comp_status != __constant_cpu_to_le16(CS_COMPLETE)) {
		iop[0] = le32_to_cpu(lg->io_parameter[0]);
		iop[1] = le32_to_cpu(lg->io_parameter[1]);

		DEBUG2_3_11(printk("%s(%ld): failed to complete IOCB "
		    "-- completion status (%x)  ioparam=%x/%x.\n", __func__,
		    ha->host_no, le16_to_cpu(lg->comp_status), iop[0],
		    iop[1]));

		switch (iop[0]) {
		case LSC_SCODE_PORTID_USED:
			mb[0] = MBS_PORT_ID_USED;
			mb[1] = LSW(iop[1]);
			break;
		case LSC_SCODE_NPORT_USED:
			mb[0] = MBS_LOOP_ID_USED;
			break;
		case LSC_SCODE_NOLINK:
		case LSC_SCODE_NOIOCB:
		case LSC_SCODE_NOXCB:
		case LSC_SCODE_CMD_FAILED:
		case LSC_SCODE_NOFABRIC:
		case LSC_SCODE_FW_NOT_READY:
		case LSC_SCODE_NOT_LOGGED_IN:
		case LSC_SCODE_NOPCB:
		case LSC_SCODE_ELS_REJECT:
		case LSC_SCODE_CMD_PARAM_ERR:
		case LSC_SCODE_NONPORT:
		case LSC_SCODE_LOGGED_IN:
		case LSC_SCODE_NOFLOGI_ACC:
		default:
			mb[0] = MBS_COMMAND_ERROR;
			break;
		}
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));

		iop[0] = le32_to_cpu(lg->io_parameter[0]);

		mb[0] = MBS_COMMAND_COMPLETE;
		mb[1] = 0;
		if (iop[0] & BIT_4) {
			if (iop[0] & BIT_8)
				mb[1] |= BIT_1;
		} else
			mb[1] = BIT_0;

		/* Passback COS information. */
		mb[10] = 0;
		if (lg->io_parameter[7] || lg->io_parameter[8])
			mb[10] |= BIT_0;	/* Class 2. */
		if (lg->io_parameter[9] || lg->io_parameter[10])
			mb[10] |= BIT_1;	/* Class 3. */
	}

	dma_pool_free(ha->s_dma_pool, lg, lg_dma);

	return rval;
}

/*
 * qla2x00_login_fabric
 *	Issue login fabric port mailbox command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	loop_id = device loop ID.
 *	domain = device domain.
 *	area = device area.
 *	al_pa = device AL_PA.
 *	status = pointer for return status.
 *	opt = command options.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_login_fabric(scsi_qla_host_t *ha, uint16_t loop_id, uint8_t domain,
    uint8_t area, uint8_t al_pa, uint16_t *mb, uint8_t opt)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_login_fabric(%ld): entered.\n", ha->host_no));

	mcp->mb[0] = MBC_LOGIN_FABRIC_PORT;
	mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	if (HAS_EXTENDED_IDS(ha)) {
		mcp->mb[1] = loop_id;
		mcp->mb[10] = opt;
		mcp->out_mb |= MBX_10;
	} else {
		mcp->mb[1] = (loop_id << 8) | opt;
	}
	mcp->mb[2] = domain;
	mcp->mb[3] = area << 8 | al_pa;

	mcp->in_mb = MBX_7|MBX_6|MBX_2|MBX_1|MBX_0;
	mcp->tov = (ha->login_timeout * 2) + (ha->login_timeout / 2);
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	/* Return mailbox statuses. */
	if (mb != NULL) {
		mb[0] = mcp->mb[0];
		mb[1] = mcp->mb[1];
		mb[2] = mcp->mb[2];
		mb[6] = mcp->mb[6];
		mb[7] = mcp->mb[7];
		/* COS retrieved from Get-Port-Database mailbox command. */
		mb[10] = 0;
	}

	if (rval != QLA_SUCCESS) {
		/* RLU tmp code: need to change main mailbox_command function to
		 * return ok even when the mailbox completion value is not
		 * SUCCESS. The caller needs to be responsible to interpret
		 * the return values of this mailbox command if we're not
		 * to change too much of the existing code.
		 */
		if (mcp->mb[0] == 0x4001 || mcp->mb[0] == 0x4002 ||
		    mcp->mb[0] == 0x4003 || mcp->mb[0] == 0x4005 ||
		    mcp->mb[0] == 0x4006)
			rval = QLA_SUCCESS;

		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_login_fabric(%ld): failed=%x "
		    "mb[0]=%x mb[1]=%x mb[2]=%x.\n", ha->host_no, rval,
		    mcp->mb[0], mcp->mb[1], mcp->mb[2]));
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_login_fabric(%ld): done.\n",
		    ha->host_no));
	}

	return rval;
}

/*
 * qla2x00_login_local_device
 *           Issue login loop port mailbox command.
 *
 * Input:
 *           ha = adapter block pointer.
 *           loop_id = device loop ID.
 *           opt = command options.
 *
 * Returns:
 *            Return status code.
 *
 * Context:
 *            Kernel context.
 *
 */
int
qla2x00_login_local_device(scsi_qla_host_t *ha, fc_port_t *fcport,
    uint16_t *mb_ret, uint8_t opt)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (IS_QLA24XX(ha) || IS_QLA54XX(ha))
		return qla24xx_login_fabric(ha, fcport->loop_id,
		    fcport->d_id.b.domain, fcport->d_id.b.area,
		    fcport->d_id.b.al_pa, mb_ret, opt);

	DEBUG3(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_LOGIN_LOOP_PORT;
	if (HAS_EXTENDED_IDS(ha))
		mcp->mb[1] = fcport->loop_id;
	else
		mcp->mb[1] = fcport->loop_id << 8;
	mcp->mb[2] = opt;
	mcp->out_mb = MBX_2|MBX_1|MBX_0;
 	mcp->in_mb = MBX_7|MBX_6|MBX_1|MBX_0;
	mcp->tov = (ha->login_timeout * 2) + (ha->login_timeout / 2);
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

 	/* Return mailbox statuses. */
 	if (mb_ret != NULL) {
 		mb_ret[0] = mcp->mb[0];
 		mb_ret[1] = mcp->mb[1];
 		mb_ret[6] = mcp->mb[6];
 		mb_ret[7] = mcp->mb[7];
 	}

	if (rval != QLA_SUCCESS) {
 		/* AV tmp code: need to change main mailbox_command function to
 		 * return ok even when the mailbox completion value is not
 		 * SUCCESS. The caller needs to be responsible to interpret
 		 * the return values of this mailbox command if we're not
 		 * to change too much of the existing code.
 		 */
 		if (mcp->mb[0] == 0x4005 || mcp->mb[0] == 0x4006)
 			rval = QLA_SUCCESS;

		DEBUG(printk("%s(%ld): failed=%x mb[0]=%x mb[1]=%x "
		    "mb[6]=%x mb[7]=%x.\n", __func__, ha->host_no, rval,
		    mcp->mb[0], mcp->mb[1], mcp->mb[6], mcp->mb[7]));
		DEBUG2_3(printk("%s(%ld): failed=%x mb[0]=%x mb[1]=%x "
		    "mb[6]=%x mb[7]=%x.\n", __func__, ha->host_no, rval,
		    mcp->mb[0], mcp->mb[1], mcp->mb[6], mcp->mb[7]));
	} else {
		/*EMPTY*/
		DEBUG3(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return (rval);
}

int
qla24xx_fabric_logout(scsi_qla_host_t *ha, uint16_t loop_id, uint8_t domain,
    uint8_t area, uint8_t al_pa)
{
	int		rval;
	struct logio_entry_24xx *lg;
	dma_addr_t	lg_dma;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	lg = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &lg_dma);
	if (lg == NULL) {
		DEBUG2_3(printk("%s(%ld): failed to allocate Logout IOCB.\n",
		    __func__, ha->host_no));
		return QLA_MEMORY_ALLOC_FAILED;
	}
	memset(lg, 0, sizeof(struct logio_entry_24xx));

	lg->entry_type = LOGINOUT_PORT_IOCB_TYPE;
	lg->entry_count = 1;
	lg->nport_handle = cpu_to_le16(loop_id);
	lg->control_flags =
	    __constant_cpu_to_le16(LCF_COMMAND_LOGO|LCF_IMPL_LOGO);
	lg->port_id[0] = al_pa;
	lg->port_id[1] = area;
	lg->port_id[2] = domain;
	rval = qla2x00_issue_iocb(ha, lg, lg_dma, 0);
	if (rval != QLA_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed to issue Logout IOCB "
		    "(%x).\n", __func__, ha->host_no, rval));
	} else if (lg->entry_status != 0) {
		DEBUG2_3_11(printk("%s(%ld): failed to complete IOCB "
		    "-- error status (%x).\n", __func__, ha->host_no,
		    lg->entry_status));
		rval = QLA_FUNCTION_FAILED;
	} else if (lg->comp_status != __constant_cpu_to_le16(CS_COMPLETE)) {
		DEBUG2_3_11(printk("%s(%ld): failed to complete IOCB "
		    "-- completion status (%x)  ioparam=%x/%x.\n", __func__,
		    ha->host_no, le16_to_cpu(lg->comp_status),
		    le32_to_cpu(lg->io_parameter[0]),
		    le32_to_cpu(lg->io_parameter[1])));
	} else {
		/*EMPTY*/
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	dma_pool_free(ha->s_dma_pool, lg, lg_dma);

	return rval;
}

/*
 * qla2x00_fabric_logout
 *	Issue logout fabric port mailbox command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	loop_id = device loop ID.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_fabric_logout(scsi_qla_host_t *ha, uint16_t loop_id, uint8_t domain,
    uint8_t area, uint8_t al_pa)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_fabric_logout(%ld): entered.\n",
	    ha->host_no));

	mcp->mb[0] = MBC_LOGOUT_FABRIC_PORT;
	mcp->out_mb = MBX_1|MBX_0;
	if (HAS_EXTENDED_IDS(ha)) {
		mcp->mb[1] = loop_id;
		mcp->mb[10] = 0;
		mcp->out_mb |= MBX_10;
	} else {
		mcp->mb[1] = loop_id << 8;
	}

	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_fabric_logout(%ld): failed=%x "
		    "mbx1=%x.\n", ha->host_no, rval, mcp->mb[1]));
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_fabric_logout(%ld): done.\n",
		    ha->host_no));
	}

	return rval;
}

/*
 * qla2x00_full_login_lip
 *	Issue full login LIP mailbox command.
 *
 * Input:
 *	ha = adapter block pointer.
 *	TARGET_QUEUE_LOCK must be released.
 *	ADAPTER_STATE_LOCK must be released.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_full_login_lip(scsi_qla_host_t *ha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_full_login_lip(%ld): entered.\n",
	    ha->host_no));

	mcp->mb[0] = MBC_LIP_FULL_LOGIN;
	mcp->mb[1] = IS_QLA24XX(ha) || IS_QLA54XX(ha) ? BIT_3: 0;
	mcp->mb[2] = 0;
	mcp->mb[3] = 0;
	mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_full_login_lip(%ld): failed=%x.\n",
		    ha->host_no, rval));
	} else {
		/*EMPTY*/
		DEBUG11(printk("qla2x00_full_login_lip(%ld): done.\n",
		    ha->host_no));
	}

	return rval;
}

/*
 * qla2x00_get_id_list
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_get_id_list(scsi_qla_host_t *ha, void *id_list, dma_addr_t id_list_dma,
    uint16_t *entries)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("qla2x00_get_id_list(%ld): entered.\n",
	    ha->host_no));

	if (id_list == NULL)
		return QLA_FUNCTION_FAILED;

	mcp->mb[0] = MBC_GET_ID_LIST;
	mcp->out_mb = MBX_0;
	if (IS_QLA24XX(ha) || IS_QLA54XX(ha)) {
		mcp->mb[2] = MSW(id_list_dma);
		mcp->mb[3] = LSW(id_list_dma);
		mcp->mb[6] = MSW(MSD(id_list_dma));
		mcp->mb[7] = LSW(MSD(id_list_dma));
		mcp->mb[8] = 0;
		mcp->out_mb |= MBX_8|MBX_7|MBX_6|MBX_3|MBX_2;
	} else {
		mcp->mb[1] = MSW(id_list_dma);
		mcp->mb[2] = LSW(id_list_dma);
		mcp->mb[3] = MSW(MSD(id_list_dma));
		mcp->mb[6] = LSW(MSD(id_list_dma));
		mcp->out_mb |= MBX_6|MBX_3|MBX_2|MBX_1;
	}
	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("qla2x00_get_id_list(%ld): failed=%x.\n",
		    ha->host_no, rval));
	} else {
		*entries = mcp->mb[1];
		DEBUG11(printk("qla2x00_get_id_list(%ld): done.\n",
		    ha->host_no));
	}

	return rval;
}

/*
 * qla2x00_get_resource_cnts
 *	Get current firmware resource counts.
 *
 * Input:
 *	ha = adapter block pointer.
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_get_resource_cnts(scsi_qla_host_t *ha, uint16_t *cur_xchg_cnt,
    uint16_t *orig_xchg_cnt, uint16_t *cur_iocb_cnt, uint16_t *orig_iocb_cnt)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_GET_RESOURCE_COUNTS;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_10|MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("%s(%ld): failed = %x.\n", __func__,
		    ha->host_no, mcp->mb[0]));
	} else {
		DEBUG11(printk("%s(%ld): done. mb1=%x mb2=%x mb3=%x mb6=%x "
		    "mb7=%x mb10=%x.\n", __func__, ha->host_no,
		    mcp->mb[1], mcp->mb[2], mcp->mb[3], mcp->mb[6], mcp->mb[7],
		    mcp->mb[10]));

		if (cur_xchg_cnt)
			*cur_xchg_cnt = mcp->mb[3];
		if (orig_xchg_cnt)
			*orig_xchg_cnt = mcp->mb[6];
		if (cur_iocb_cnt)
			*cur_iocb_cnt = mcp->mb[7];
		if (orig_iocb_cnt)
			*orig_iocb_cnt = mcp->mb[10];
	}

	return (rval);
}

#if defined(QL_DEBUG_LEVEL_3)
/*
 * qla2x00_get_fcal_position_map
 *	Get FCAL (LILP) position map using mailbox command
 *
 * Input:
 *	ha = adapter state pointer.
 *	pos_map = buffer pointer (can be NULL).
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla2x00_get_fcal_position_map(scsi_qla_host_t *ha, char *pos_map)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	char *pmap;
	dma_addr_t pmap_dma;

	pmap = dma_pool_alloc(ha->s_dma_pool, GFP_ATOMIC, &pmap_dma);
	if (pmap  == NULL) {
		DEBUG2_3_11(printk("%s(%ld): **** Mem Alloc Failed ****",
		    __func__, ha->host_no));
		return QLA_MEMORY_ALLOC_FAILED;
	}
	memset(pmap, 0, FCAL_MAP_SIZE);

	mcp->mb[0] = MBC_GET_FC_AL_POSITION_MAP;
	mcp->mb[2] = MSW(pmap_dma);
	mcp->mb[3] = LSW(pmap_dma);
	mcp->mb[6] = MSW(MSD(pmap_dma));
	mcp->mb[7] = LSW(MSD(pmap_dma));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_0;
	mcp->in_mb = MBX_1|MBX_0;
	mcp->buf_size = FCAL_MAP_SIZE;
	mcp->flags = MBX_DMA_IN;
	mcp->tov = (ha->login_timeout * 2) + (ha->login_timeout / 2);
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval == QLA_SUCCESS) {
		DEBUG11(printk("%s(%ld): (mb0=%x/mb1=%x) FC/AL Position Map "
		    "size (%x)\n", __func__, ha->host_no, mcp->mb[0],
		    mcp->mb[1], (unsigned)pmap[0]));
		DEBUG11(qla2x00_dump_buffer(pmap, pmap[0] + 1));

		if (pos_map)
			memcpy(pos_map, pmap, FCAL_MAP_SIZE);
	}
	dma_pool_free(ha->s_dma_pool, pmap, pmap_dma);

	if (rval != QLA_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed=%x.\n", __func__,
		    ha->host_no, rval));
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}
#endif

/*
 * qla2x00_get_link_status
 *
 * Input:
 *	ha = adapter block pointer.
 *	loop_id = device loop ID.
 *	ret_buf = pointer to link status return buffer.
 *
 * Returns:
 *	0 = success.
 *	BIT_0 = mem alloc error.
 *	BIT_1 = mailbox error.
 */
int
qla2x00_get_link_status(scsi_qla_host_t *ha, uint16_t loop_id,
    link_stat_t *ret_buf, uint16_t *status)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	link_stat_t *stat_buf;
	dma_addr_t stat_buf_dma;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	stat_buf = dma_pool_alloc(ha->s_dma_pool, GFP_ATOMIC, &stat_buf_dma);
	if (stat_buf == NULL) {
		DEBUG2_3_11(printk("%s(%ld): Failed to allocate memory.\n",
		    __func__, ha->host_no));
		return BIT_0;
	}
	memset(stat_buf, 0, sizeof(link_stat_t));

	mcp->mb[0] = MBC_GET_LINK_STATUS;
	mcp->mb[2] = MSW(stat_buf_dma);
	mcp->mb[3] = LSW(stat_buf_dma);
	mcp->mb[6] = MSW(MSD(stat_buf_dma));
	mcp->mb[7] = LSW(MSD(stat_buf_dma));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_0;
	mcp->in_mb = MBX_0;
	if (IS_QLA24XX(ha) || IS_QLA54XX(ha)) {
		mcp->mb[1] = loop_id;
		mcp->mb[4] = 0;
		mcp->mb[10] = 0;
		mcp->out_mb |= MBX_10|MBX_4|MBX_1;
		mcp->in_mb |= MBX_1;
	} else if (HAS_EXTENDED_IDS(ha)) {
		mcp->mb[1] = loop_id;
		mcp->mb[10] = 0;
		mcp->out_mb |= MBX_10|MBX_1;
	} else {
		mcp->mb[1] = loop_id << 8;
		mcp->out_mb |= MBX_1;
	}
	mcp->tov = 30;
	mcp->flags = IOCTL_CMD;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval == QLA_SUCCESS) {
		if (mcp->mb[0] != MBS_COMMAND_COMPLETE) {
			DEBUG2_3_11(printk("%s(%ld): cmd failed. mbx0=%x.\n",
			    __func__, ha->host_no, mcp->mb[0]));
			status[0] = mcp->mb[0];
			rval = BIT_1;
		} else {
			/* copy over data -- firmware data is LE. */
			ret_buf->link_fail_cnt =
			    le32_to_cpu(stat_buf->link_fail_cnt);
			ret_buf->loss_sync_cnt =
			    le32_to_cpu(stat_buf->loss_sync_cnt);
			ret_buf->loss_sig_cnt =
			    le32_to_cpu(stat_buf->loss_sig_cnt);
			ret_buf->prim_seq_err_cnt =
			    le32_to_cpu(stat_buf->prim_seq_err_cnt);
			ret_buf->inval_xmit_word_cnt =
			    le32_to_cpu(stat_buf->inval_xmit_word_cnt);
			ret_buf->inval_crc_cnt =
			    le32_to_cpu(stat_buf->inval_crc_cnt);

			DEBUG11(printk("%s(%ld): stat dump: fail_cnt=%d "
			    "loss_sync=%d loss_sig=%d seq_err=%d "
			    "inval_xmt_word=%d inval_crc=%d.\n", __func__,
			    ha->host_no, stat_buf->link_fail_cnt,
			    stat_buf->loss_sync_cnt, stat_buf->loss_sig_cnt,
			    stat_buf->prim_seq_err_cnt,
			    stat_buf->inval_xmit_word_cnt,
			    stat_buf->inval_crc_cnt));
		}
	} else {
		/* Failed. */
		DEBUG2_3_11(printk("%s(%ld): failed=%x.\n", __func__,
		    ha->host_no, rval));
		rval = BIT_1;
	}

	dma_pool_free(ha->s_dma_pool, stat_buf, stat_buf_dma);

	return rval;
}

int
qla24xx_get_isp_stats(scsi_qla_host_t *ha, uint32_t *dwbuf, uint32_t dwords,
    uint16_t *status)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	uint32_t *sbuf, *siter;
	dma_addr_t sbuf_dma;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	if (dwords > (DMA_POOL_SIZE / 4)) {
		DEBUG2_3_11(printk("%s(%ld): Unabled to retrieve %d DWORDs "
		    "(max %d).\n", __func__, ha->host_no, dwords,
		    DMA_POOL_SIZE / 4));
		return BIT_0;
	}
	sbuf = dma_pool_alloc(ha->s_dma_pool, GFP_ATOMIC, &sbuf_dma);
	if (sbuf == NULL) {
		DEBUG2_3_11(printk("%s(%ld): Failed to allocate memory.\n",
		    __func__, ha->host_no));
		return BIT_0;
	}
	memset(sbuf, 0, DMA_POOL_SIZE);

	mcp->mb[0] = MBC_GET_LINK_PRIV_STATS;
	mcp->mb[2] = MSW(sbuf_dma);
	mcp->mb[3] = LSW(sbuf_dma);
	mcp->mb[6] = MSW(MSD(sbuf_dma));
	mcp->mb[7] = LSW(MSD(sbuf_dma));
	mcp->mb[8] = dwords;
	mcp->mb[10] = 0;
	mcp->out_mb = MBX_10|MBX_8|MBX_7|MBX_6|MBX_3|MBX_2|MBX_0;
	mcp->in_mb = MBX_2|MBX_1|MBX_0;
	mcp->tov = 30;
	mcp->flags = IOCTL_CMD;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval == QLA_SUCCESS) {
		if (mcp->mb[0] != MBS_COMMAND_COMPLETE) {
			DEBUG2_3_11(printk("%s(%ld): cmd failed. mbx0=%x.\n",
			    __func__, ha->host_no, mcp->mb[0]));
			status[0] = mcp->mb[0];
			rval = BIT_1;
		} else {
			/* Copy over data -- firmware data is LE. */
			siter = sbuf;
			while (dwords--)
				*dwbuf++ = le32_to_cpu(*siter++);
		}
	} else {
		/* Failed. */
		DEBUG2_3_11(printk("%s(%ld): failed=%x.\n", __func__,
		    ha->host_no, rval));
		rval = BIT_1;
	}

	dma_pool_free(ha->s_dma_pool, sbuf, sbuf_dma);

	return rval;
}

int
qla24xx_abort_command(scsi_qla_host_t *ha, srb_t *sp)
{
	int		rval;
	fc_port_t	*fcport;
	unsigned long   flags = 0;

	struct abort_entry_24xx *abt;
	dma_addr_t	abt_dma;
	uint32_t	handle;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	fcport = sp->fcport;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (handle = 1; handle < MAX_OUTSTANDING_COMMANDS; handle++) {
		if (ha->outstanding_cmds[handle] == sp)
			break;
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	if (handle == MAX_OUTSTANDING_COMMANDS) {
		/* Command not found. */
		return QLA_FUNCTION_FAILED;
	}

	abt = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &abt_dma);
	if (abt == NULL) {
		DEBUG2_3(printk("%s(%ld): failed to allocate Abort IOCB.\n",
		    __func__, ha->host_no));
		return QLA_MEMORY_ALLOC_FAILED;
	}
	memset(abt, 0, sizeof(struct abort_entry_24xx));

	abt->entry_type = ABORT_IOCB_TYPE;
	abt->entry_count = 1;
	abt->nport_handle = cpu_to_le16(fcport->loop_id);
	abt->handle_to_abort = handle;
	abt->port_id[0] = fcport->d_id.b.al_pa;
	abt->port_id[1] = fcport->d_id.b.area;
	abt->port_id[2] = fcport->d_id.b.domain;
	rval = qla2x00_issue_iocb(ha, abt, abt_dma, 0);
	if (rval != QLA_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed to issue IOCB (%x).\n",
		    __func__, ha->host_no, rval));
	} else if (abt->entry_status != 0) {
		DEBUG2_3_11(printk("%s(%ld): failed to complete IOCB "
		    "-- error status (%x).\n", __func__, ha->host_no,
		    abt->entry_status));
		rval = QLA_FUNCTION_FAILED;
	} else if (abt->nport_handle != __constant_cpu_to_le16(0)) {
		DEBUG2_3_11(printk("%s(%ld): failed to complete IOCB "
		    "-- completion status (%x).\n", __func__, ha->host_no,
		    le16_to_cpu(abt->nport_handle)));
		rval = QLA_FUNCTION_FAILED;
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
		sp->flags |= SRB_ABORT_PENDING;
	}

	dma_pool_free(ha->s_dma_pool, abt, abt_dma);

	return rval;
}

struct tsk_mgmt_cmd {
	union {
		struct tsk_mgmt_entry tsk;
		struct sts_entry_24xx sts;
	} p;
};

int
qla24xx_abort_target(fc_port_t *fcport)
{
	int		rval;
	struct tsk_mgmt_cmd *tsk;
	dma_addr_t	tsk_dma;
	scsi_qla_host_t *ha;

	if (fcport == NULL)
		return 0;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, fcport->ha->host_no));

	ha = fcport->ha;
	tsk = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &tsk_dma);
	if (tsk == NULL) {
		DEBUG2_3(printk("%s(%ld): failed to allocate Task Management "
		    "IOCB.\n", __func__, ha->host_no));
		return QLA_MEMORY_ALLOC_FAILED;
	}
	memset(tsk, 0, sizeof(struct tsk_mgmt_cmd));

	tsk->p.tsk.entry_type = TSK_MGMT_IOCB_TYPE;
	tsk->p.tsk.entry_count = 1;
	tsk->p.tsk.nport_handle = cpu_to_le16(fcport->loop_id);
	tsk->p.tsk.timeout = __constant_cpu_to_le16(25);
	tsk->p.tsk.control_flags = __constant_cpu_to_le32(TCF_TARGET_RESET);
	tsk->p.tsk.port_id[0] = fcport->d_id.b.al_pa;
	tsk->p.tsk.port_id[1] = fcport->d_id.b.area;
	tsk->p.tsk.port_id[2] = fcport->d_id.b.domain;
	rval = qla2x00_issue_iocb(ha, tsk, tsk_dma, 0);
	if (rval != QLA_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed to issue Target Reset IOCB "
		    "(%x).\n", __func__, ha->host_no, rval));
		goto atarget_done;
	} else if (tsk->p.sts.entry_status != 0) {
		DEBUG2_3_11(printk("%s(%ld): failed to complete IOCB "
		    "-- error status (%x).\n", __func__, ha->host_no,
		    tsk->p.sts.entry_status));
		rval = QLA_FUNCTION_FAILED;
		goto atarget_done;
	} else if (tsk->p.sts.comp_status !=
	    __constant_cpu_to_le16(CS_COMPLETE)) {
		DEBUG2_3_11(printk("%s(%ld): failed to complete IOCB "
		    "-- completion status (%x).\n", __func__,
		    ha->host_no, le16_to_cpu(tsk->p.sts.comp_status)));
		rval = QLA_FUNCTION_FAILED;
		goto atarget_done;
	}

	/* Issue marker IOCB. */
	rval = qla2x00_marker(ha, fcport->loop_id, 0, MK_SYNC_ID);
	if (rval != QLA_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed to issue Marker IOCB "
		    "(%x).\n", __func__, ha->host_no, rval));
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

atarget_done:
	dma_pool_free(ha->s_dma_pool, tsk, tsk_dma);

	return rval;
}

int
qla2x00_system_error(scsi_qla_host_t *ha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_QLA24XX(ha) && !IS_QLA54XX(ha))
		return QLA_FUNCTION_FAILED;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_GEN_SYSTEM_ERROR;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 5;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed=%x.\n", __func__,
		    ha->host_no, rval));
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}

/**
 * qla2x00_get_serdes_params() -
 * @ha: HA context
 *
 * Returns
 */
int
qla2x00_get_serdes_params(scsi_qla_host_t *ha, uint16_t *sw_em_1g,
    uint16_t *sw_em_2g, uint16_t *sw_em_4g)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_SERDES_PARAMS;
	mcp->mb[1] = 0;
	mcp->out_mb = MBX_1|MBX_0;
	mcp->in_mb = MBX_4|MBX_3|MBX_2|MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("%s(%ld): failed=%x (%x).\n", __func__,
		    ha->host_no, rval, mcp->mb[0]));
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));

		if (sw_em_1g)
			*sw_em_1g = mcp->mb[2];
		if (sw_em_2g)
			*sw_em_2g = mcp->mb[3];
		if (sw_em_4g)
			*sw_em_4g = mcp->mb[4];
	}

	return rval;
}

/**
 * qla2x00_set_serdes_params() -
 * @ha: HA context
 *
 * Returns
 */
int
qla2x00_set_serdes_params(scsi_qla_host_t *ha, uint16_t sw_em_1g,
    uint16_t sw_em_2g, uint16_t sw_em_4g)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_SERDES_PARAMS;
	mcp->mb[1] = BIT_0;
	mcp->mb[2] = sw_em_1g | BIT_15;
	mcp->mb[3] = sw_em_2g | BIT_15;
	mcp->mb[4] = sw_em_4g | BIT_15;
	mcp->out_mb = MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		DEBUG2_3_11(printk("%s(%ld): failed=%x (%x).\n", __func__,
		    ha->host_no, rval, mcp->mb[0]));
	} else {
		/*EMPTY*/
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}

int
qla2x00_stop_firmware(scsi_qla_host_t *ha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_QLA24XX(ha) && !IS_QLA54XX(ha))
		return QLA_FUNCTION_FAILED;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_STOP_FIRMWARE;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 5;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed=%x.\n", __func__,
		    ha->host_no, rval));
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}

int
qla2x00_trace_control(scsi_qla_host_t *ha, uint16_t ctrl, dma_addr_t eft_dma,
    uint16_t buffers)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_QLA24XX(ha) && !IS_QLA54XX(ha))
		return QLA_FUNCTION_FAILED;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_TRACE_CONTROL;
	mcp->mb[1] = ctrl;
	mcp->out_mb = MBX_1|MBX_0;
	mcp->in_mb = MBX_1|MBX_0;
	if (ctrl == TC_ENABLE) {
		mcp->mb[2] = LSW(eft_dma);
		mcp->mb[3] = MSW(eft_dma);
		mcp->mb[4] = LSW(MSD(eft_dma));
		mcp->mb[5] = MSW(MSD(eft_dma));
		mcp->mb[6] = buffers;
		mcp->mb[7] = 0;
		mcp->out_mb |= MBX_7|MBX_6|MBX_5|MBX_4|MBX_3|MBX_2;
	}
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed=%x mb[0]=%x mb[1]=%x.\n",
		    __func__, ha->host_no, rval, mcp->mb[0], mcp->mb[1]));
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}

int
qla2x00_read_sfp(scsi_qla_host_t *ha, dma_addr_t sfp_dma, uint16_t addr,
    uint16_t off, uint16_t count)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_QLA24XX(ha) && !IS_QLA54XX(ha))
		return QLA_FUNCTION_FAILED;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_READ_SFP;
	mcp->mb[1] = addr;
	mcp->mb[2] = MSW(sfp_dma);
	mcp->mb[3] = LSW(sfp_dma);
	mcp->mb[6] = MSW(MSD(sfp_dma));
	mcp->mb[7] = LSW(MSD(sfp_dma));
	mcp->mb[8] = count;
	mcp->mb[9] = off;
	mcp->mb[10] = 0;
	mcp->out_mb = MBX_10|MBX_9|MBX_8|MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	if (rval != QLA_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed=%x (%x).\n", __func__,
		    ha->host_no, rval, mcp->mb[0]));
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}

int
qla2x00_get_idma_speed(scsi_qla_host_t *ha, uint16_t loop_id,
    uint16_t *port_speed, uint16_t *mb)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_QLA24XX(ha))
		return QLA_FUNCTION_FAILED;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_PORT_PARAMS;
	mcp->mb[1] = loop_id;
	mcp->mb[2] = mcp->mb[3] = mcp->mb[4] = mcp->mb[5] = 0;
	mcp->out_mb = MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_5|MBX_4|MBX_3|MBX_1|MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	/* Return mailbox statuses. */
	if (mb != NULL) {
		mb[0] = mcp->mb[0];
		mb[1] = mcp->mb[1];
		mb[3] = mcp->mb[3];
		mb[4] = mcp->mb[4];
		mb[5] = mcp->mb[5];
	}

	if (rval != QLA_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed=%x.\n", __func__,
		    ha->host_no, rval));
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
		if (port_speed)
			*port_speed = mcp->mb[3];
	}

	return rval;
}

int
qla2x00_set_idma_speed(scsi_qla_host_t *ha, uint16_t loop_id,
    uint16_t port_speed, uint16_t *mb)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_QLA24XX(ha))
		return QLA_FUNCTION_FAILED;

	DEBUG11(printk("%s(%ld): entered.\n", __func__, ha->host_no));

	mcp->mb[0] = MBC_PORT_PARAMS;
	mcp->mb[1] = loop_id;
	mcp->mb[2] = BIT_0;
	mcp->mb[3] = port_speed & (BIT_2|BIT_1|BIT_0);
	mcp->mb[4] = mcp->mb[5] = 0;
	mcp->out_mb = MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_5|MBX_4|MBX_3|MBX_1|MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(ha, mcp);

	/* Return mailbox statuses. */
	if (mb != NULL) {
		mb[0] = mcp->mb[0];
		mb[1] = mcp->mb[1];
		mb[3] = mcp->mb[3];
		mb[4] = mcp->mb[4];
		mb[5] = mcp->mb[5];
	}

	if (rval != QLA_SUCCESS) {
		DEBUG2_3_11(printk("%s(%ld): failed=%x.\n", __func__,
		    ha->host_no, rval));
	} else {
		DEBUG11(printk("%s(%ld): done.\n", __func__, ha->host_no));
	}

	return rval;
}
