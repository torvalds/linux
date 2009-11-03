#include "qlge.h"

static void ql_display_mb_sts(struct ql_adapter *qdev,
						struct mbox_params *mbcp)
{
	int i;
	static char *err_sts[] = {
		"Command Complete",
		"Command Not Supported",
		"Host Interface Error",
		"Checksum Error",
		"Unused Completion Status",
		"Test Failed",
		"Command Parameter Error"};

	QPRINTK(qdev, DRV, DEBUG, "%s.\n",
		err_sts[mbcp->mbox_out[0] & 0x0000000f]);
	for (i = 0; i < mbcp->out_count; i++)
		QPRINTK(qdev, DRV, DEBUG, "mbox_out[%d] = 0x%.08x.\n",
				i, mbcp->mbox_out[i]);
}

int ql_read_mpi_reg(struct ql_adapter *qdev, u32 reg, u32 *data)
{
	int status;
	/* wait for reg to come ready */
	status = ql_wait_reg_rdy(qdev, PROC_ADDR, PROC_ADDR_RDY, PROC_ADDR_ERR);
	if (status)
		goto exit;
	/* set up for reg read */
	ql_write32(qdev, PROC_ADDR, reg | PROC_ADDR_R);
	/* wait for reg to come ready */
	status = ql_wait_reg_rdy(qdev, PROC_ADDR, PROC_ADDR_RDY, PROC_ADDR_ERR);
	if (status)
		goto exit;
	/* get the data */
	*data = ql_read32(qdev, PROC_DATA);
exit:
	return status;
}

int ql_write_mpi_reg(struct ql_adapter *qdev, u32 reg, u32 data)
{
	int status = 0;
	/* wait for reg to come ready */
	status = ql_wait_reg_rdy(qdev, PROC_ADDR, PROC_ADDR_RDY, PROC_ADDR_ERR);
	if (status)
		goto exit;
	/* write the data to the data reg */
	ql_write32(qdev, PROC_DATA, data);
	/* trigger the write */
	ql_write32(qdev, PROC_ADDR, reg);
	/* wait for reg to come ready */
	status = ql_wait_reg_rdy(qdev, PROC_ADDR, PROC_ADDR_RDY, PROC_ADDR_ERR);
	if (status)
		goto exit;
exit:
	return status;
}

int ql_soft_reset_mpi_risc(struct ql_adapter *qdev)
{
	int status;
	status = ql_write_mpi_reg(qdev, 0x00001010, 1);
	return status;
}

static int ql_get_mb_sts(struct ql_adapter *qdev, struct mbox_params *mbcp)
{
	int i, status;

	status = ql_sem_spinlock(qdev, SEM_PROC_REG_MASK);
	if (status)
		return -EBUSY;
	for (i = 0; i < mbcp->out_count; i++) {
		status =
		    ql_read_mpi_reg(qdev, qdev->mailbox_out + i,
				     &mbcp->mbox_out[i]);
		if (status) {
			QPRINTK(qdev, DRV, ERR, "Failed mailbox read.\n");
			break;
		}
	}
	ql_sem_unlock(qdev, SEM_PROC_REG_MASK);	/* does flush too */
	return status;
}

/* Wait for a single mailbox command to complete.
 * Returns zero on success.
 */
static int ql_wait_mbx_cmd_cmplt(struct ql_adapter *qdev)
{
	int count = 100;
	u32 value;

	do {
		value = ql_read32(qdev, STS);
		if (value & STS_PI)
			return 0;
		mdelay(UDELAY_DELAY); /* 100ms */
	} while (--count);
	return -ETIMEDOUT;
}

/* Execute a single mailbox command.
 * Caller must hold PROC_ADDR semaphore.
 */
static int ql_exec_mb_cmd(struct ql_adapter *qdev, struct mbox_params *mbcp)
{
	int i, status;

	/*
	 * Make sure there's nothing pending.
	 * This shouldn't happen.
	 */
	if (ql_read32(qdev, CSR) & CSR_HRI)
		return -EIO;

	status = ql_sem_spinlock(qdev, SEM_PROC_REG_MASK);
	if (status)
		return status;

	/*
	 * Fill the outbound mailboxes.
	 */
	for (i = 0; i < mbcp->in_count; i++) {
		status = ql_write_mpi_reg(qdev, qdev->mailbox_in + i,
						mbcp->mbox_in[i]);
		if (status)
			goto end;
	}
	/*
	 * Wake up the MPI firmware.
	 */
	ql_write32(qdev, CSR, CSR_CMD_SET_H2R_INT);
end:
	ql_sem_unlock(qdev, SEM_PROC_REG_MASK);
	return status;
}

/* We are being asked by firmware to accept
 * a change to the port.  This is only
 * a change to max frame sizes (Tx/Rx), pause
 * parameters, or loopback mode. We wake up a worker
 * to handler processing this since a mailbox command
 * will need to be sent to ACK the request.
 */
static int ql_idc_req_aen(struct ql_adapter *qdev)
{
	int status;
	struct mbox_params *mbcp = &qdev->idc_mbc;

	QPRINTK(qdev, DRV, ERR, "Enter!\n");
	/* Get the status data and start up a thread to
	 * handle the request.
	 */
	mbcp = &qdev->idc_mbc;
	mbcp->out_count = 4;
	status = ql_get_mb_sts(qdev, mbcp);
	if (status) {
		QPRINTK(qdev, DRV, ERR,
			"Could not read MPI, resetting ASIC!\n");
		ql_queue_asic_error(qdev);
	} else	{
		/* Begin polled mode early so
		 * we don't get another interrupt
		 * when we leave mpi_worker.
		 */
		ql_write32(qdev, INTR_MASK, (INTR_MASK_PI << 16));
		queue_delayed_work(qdev->workqueue, &qdev->mpi_idc_work, 0);
	}
	return status;
}

/* Process an inter-device event completion.
 * If good, signal the caller's completion.
 */
static int ql_idc_cmplt_aen(struct ql_adapter *qdev)
{
	int status;
	struct mbox_params *mbcp = &qdev->idc_mbc;
	mbcp->out_count = 4;
	status = ql_get_mb_sts(qdev, mbcp);
	if (status) {
		QPRINTK(qdev, DRV, ERR,
			"Could not read MPI, resetting RISC!\n");
		ql_queue_fw_error(qdev);
	} else
		/* Wake up the sleeping mpi_idc_work thread that is
		 * waiting for this event.
		 */
		complete(&qdev->ide_completion);

	return status;
}

static void ql_link_up(struct ql_adapter *qdev, struct mbox_params *mbcp)
{
	int status;
	mbcp->out_count = 2;

	status = ql_get_mb_sts(qdev, mbcp);
	if (status) {
		QPRINTK(qdev, DRV, ERR,
			"%s: Could not get mailbox status.\n", __func__);
		return;
	}

	qdev->link_status = mbcp->mbox_out[1];
	QPRINTK(qdev, DRV, ERR, "Link Up.\n");

	/* If we're coming back from an IDC event
	 * then set up the CAM and frame routing.
	 */
	if (test_bit(QL_CAM_RT_SET, &qdev->flags)) {
		status = ql_cam_route_initialize(qdev);
		if (status) {
			QPRINTK(qdev, IFUP, ERR,
			"Failed to init CAM/Routing tables.\n");
			return;
		} else
			clear_bit(QL_CAM_RT_SET, &qdev->flags);
	}

	/* Queue up a worker to check the frame
	 * size information, and fix it if it's not
	 * to our liking.
	 */
	if (!test_bit(QL_PORT_CFG, &qdev->flags)) {
		QPRINTK(qdev, DRV, ERR, "Queue Port Config Worker!\n");
		set_bit(QL_PORT_CFG, &qdev->flags);
		/* Begin polled mode early so
		 * we don't get another interrupt
		 * when we leave mpi_worker dpc.
		 */
		ql_write32(qdev, INTR_MASK, (INTR_MASK_PI << 16));
		queue_delayed_work(qdev->workqueue,
				&qdev->mpi_port_cfg_work, 0);
	}

	ql_link_on(qdev);
}

static void ql_link_down(struct ql_adapter *qdev, struct mbox_params *mbcp)
{
	int status;

	mbcp->out_count = 3;

	status = ql_get_mb_sts(qdev, mbcp);
	if (status)
		QPRINTK(qdev, DRV, ERR, "Link down AEN broken!\n");

	ql_link_off(qdev);
}

static int ql_sfp_in(struct ql_adapter *qdev, struct mbox_params *mbcp)
{
	int status;

	mbcp->out_count = 5;

	status = ql_get_mb_sts(qdev, mbcp);
	if (status)
		QPRINTK(qdev, DRV, ERR, "SFP in AEN broken!\n");
	else
		QPRINTK(qdev, DRV, ERR, "SFP insertion detected.\n");

	return status;
}

static int ql_sfp_out(struct ql_adapter *qdev, struct mbox_params *mbcp)
{
	int status;

	mbcp->out_count = 1;

	status = ql_get_mb_sts(qdev, mbcp);
	if (status)
		QPRINTK(qdev, DRV, ERR, "SFP out AEN broken!\n");
	else
		QPRINTK(qdev, DRV, ERR, "SFP removal detected.\n");

	return status;
}

static int ql_aen_lost(struct ql_adapter *qdev, struct mbox_params *mbcp)
{
	int status;

	mbcp->out_count = 6;

	status = ql_get_mb_sts(qdev, mbcp);
	if (status)
		QPRINTK(qdev, DRV, ERR, "Lost AEN broken!\n");
	else {
		int i;
		QPRINTK(qdev, DRV, ERR, "Lost AEN detected.\n");
		for (i = 0; i < mbcp->out_count; i++)
			QPRINTK(qdev, DRV, ERR, "mbox_out[%d] = 0x%.08x.\n",
					i, mbcp->mbox_out[i]);

	}

	return status;
}

static void ql_init_fw_done(struct ql_adapter *qdev, struct mbox_params *mbcp)
{
	int status;

	mbcp->out_count = 2;

	status = ql_get_mb_sts(qdev, mbcp);
	if (status) {
		QPRINTK(qdev, DRV, ERR, "Firmware did not initialize!\n");
	} else {
		QPRINTK(qdev, DRV, ERR, "Firmware Revision  = 0x%.08x.\n",
			mbcp->mbox_out[1]);
		status = ql_cam_route_initialize(qdev);
		if (status)
			QPRINTK(qdev, IFUP, ERR,
				"Failed to init CAM/Routing tables.\n");
	}
}

/* Process an async event and clear it unless it's an
 * error condition.
 *  This can get called iteratively from the mpi_work thread
 *  when events arrive via an interrupt.
 *  It also gets called when a mailbox command is polling for
 *  it's completion. */
static int ql_mpi_handler(struct ql_adapter *qdev, struct mbox_params *mbcp)
{
	int status;
	int orig_count = mbcp->out_count;

	/* Just get mailbox zero for now. */
	mbcp->out_count = 1;
	status = ql_get_mb_sts(qdev, mbcp);
	if (status) {
		QPRINTK(qdev, DRV, ERR,
			"Could not read MPI, resetting ASIC!\n");
		ql_queue_asic_error(qdev);
		goto end;
	}

	switch (mbcp->mbox_out[0]) {

	/* This case is only active when we arrive here
	 * as a result of issuing a mailbox command to
	 * the firmware.
	 */
	case MB_CMD_STS_INTRMDT:
	case MB_CMD_STS_GOOD:
	case MB_CMD_STS_INVLD_CMD:
	case MB_CMD_STS_XFC_ERR:
	case MB_CMD_STS_CSUM_ERR:
	case MB_CMD_STS_ERR:
	case MB_CMD_STS_PARAM_ERR:
		/* We can only get mailbox status if we're polling from an
		 * unfinished command.  Get the rest of the status data and
		 * return back to the caller.
		 * We only end up here when we're polling for a mailbox
		 * command completion.
		 */
		mbcp->out_count = orig_count;
		status = ql_get_mb_sts(qdev, mbcp);
		return status;

	/* We are being asked by firmware to accept
	 * a change to the port.  This is only
	 * a change to max frame sizes (Tx/Rx), pause
	 * parameters, or loopback mode.
	 */
	case AEN_IDC_REQ:
		status = ql_idc_req_aen(qdev);
		break;

	/* Process and inbound IDC event.
	 * This will happen when we're trying to
	 * change tx/rx max frame size, change pause
	 * parameters or loopback mode.
	 */
	case AEN_IDC_CMPLT:
	case AEN_IDC_EXT:
		status = ql_idc_cmplt_aen(qdev);
		break;

	case AEN_LINK_UP:
		ql_link_up(qdev, mbcp);
		break;

	case AEN_LINK_DOWN:
		ql_link_down(qdev, mbcp);
		break;

	case AEN_FW_INIT_DONE:
		/* If we're in process on executing the firmware,
		 * then convert the status to normal mailbox status.
		 */
		if (mbcp->mbox_in[0] == MB_CMD_EX_FW) {
			mbcp->out_count = orig_count;
			status = ql_get_mb_sts(qdev, mbcp);
			mbcp->mbox_out[0] = MB_CMD_STS_GOOD;
			return status;
		}
		ql_init_fw_done(qdev, mbcp);
		break;

	case AEN_AEN_SFP_IN:
		ql_sfp_in(qdev, mbcp);
		break;

	case AEN_AEN_SFP_OUT:
		ql_sfp_out(qdev, mbcp);
		break;

	/* This event can arrive at boot time or after an
	 * MPI reset if the firmware failed to initialize.
	 */
	case AEN_FW_INIT_FAIL:
		/* If we're in process on executing the firmware,
		 * then convert the status to normal mailbox status.
		 */
		if (mbcp->mbox_in[0] == MB_CMD_EX_FW) {
			mbcp->out_count = orig_count;
			status = ql_get_mb_sts(qdev, mbcp);
			mbcp->mbox_out[0] = MB_CMD_STS_ERR;
			return status;
		}
		QPRINTK(qdev, DRV, ERR,
			"Firmware initialization failed.\n");
		status = -EIO;
		ql_queue_fw_error(qdev);
		break;

	case AEN_SYS_ERR:
		QPRINTK(qdev, DRV, ERR,
			"System Error.\n");
		ql_queue_fw_error(qdev);
		status = -EIO;
		break;

	case AEN_AEN_LOST:
		ql_aen_lost(qdev, mbcp);
		break;

	default:
		QPRINTK(qdev, DRV, ERR,
			"Unsupported AE %.08x.\n", mbcp->mbox_out[0]);
		/* Clear the MPI firmware status. */
	}
end:
	ql_write32(qdev, CSR, CSR_CMD_CLR_R2PCI_INT);
	/* Restore the original mailbox count to
	 * what the caller asked for.  This can get
	 * changed when a mailbox command is waiting
	 * for a response and an AEN arrives and
	 * is handled.
	 * */
	mbcp->out_count = orig_count;
	return status;
}

/* Execute a single mailbox command.
 * mbcp is a pointer to an array of u32.  Each
 * element in the array contains the value for it's
 * respective mailbox register.
 */
static int ql_mailbox_command(struct ql_adapter *qdev, struct mbox_params *mbcp)
{
	int status, count;


	/* Begin polled mode for MPI */
	ql_write32(qdev, INTR_MASK, (INTR_MASK_PI << 16));

	/* Load the mailbox registers and wake up MPI RISC. */
	status = ql_exec_mb_cmd(qdev, mbcp);
	if (status)
		goto end;


	/* If we're generating a system error, then there's nothing
	 * to wait for.
	 */
	if (mbcp->mbox_in[0] == MB_CMD_MAKE_SYS_ERR)
		goto end;

	/* Wait for the command to complete. We loop
	 * here because some AEN might arrive while
	 * we're waiting for the mailbox command to
	 * complete. If more than 5 arrive then we can
	 * assume something is wrong. */
	count = 5;
	do {
		/* Wait for the interrupt to come in. */
		status = ql_wait_mbx_cmd_cmplt(qdev);
		if (status)
			goto end;

		/* Process the event.  If it's an AEN, it
		 * will be handled in-line or a worker
		 * will be spawned. If it's our completion
		 * we will catch it below.
		 */
		status = ql_mpi_handler(qdev, mbcp);
		if (status)
			goto end;

		/* It's either the completion for our mailbox
		 * command complete or an AEN.  If it's our
		 * completion then get out.
		 */
		if (((mbcp->mbox_out[0] & 0x0000f000) ==
					MB_CMD_STS_GOOD) ||
			((mbcp->mbox_out[0] & 0x0000f000) ==
					MB_CMD_STS_INTRMDT))
			break;
	} while (--count);

	if (!count) {
		QPRINTK(qdev, DRV, ERR,
			"Timed out waiting for mailbox complete.\n");
		status = -ETIMEDOUT;
		goto end;
	}

	/* Now we can clear the interrupt condition
	 * and look at our status.
	 */
	ql_write32(qdev, CSR, CSR_CMD_CLR_R2PCI_INT);

	if (((mbcp->mbox_out[0] & 0x0000f000) !=
					MB_CMD_STS_GOOD) &&
		((mbcp->mbox_out[0] & 0x0000f000) !=
					MB_CMD_STS_INTRMDT)) {
		ql_display_mb_sts(qdev, mbcp);
		status = -EIO;
	}
end:
	/* End polled mode for MPI */
	ql_write32(qdev, INTR_MASK, (INTR_MASK_PI << 16) | INTR_MASK_PI);
	return status;
}


/* Get MPI firmware version. This will be used for
 * driver banner and for ethtool info.
 * Returns zero on success.
 */
int ql_mb_about_fw(struct ql_adapter *qdev)
{
	struct mbox_params mbc;
	struct mbox_params *mbcp = &mbc;
	int status = 0;

	memset(mbcp, 0, sizeof(struct mbox_params));

	mbcp->in_count = 1;
	mbcp->out_count = 3;

	mbcp->mbox_in[0] = MB_CMD_ABOUT_FW;

	status = ql_mailbox_command(qdev, mbcp);
	if (status)
		return status;

	if (mbcp->mbox_out[0] != MB_CMD_STS_GOOD) {
		QPRINTK(qdev, DRV, ERR,
			"Failed about firmware command\n");
		status = -EIO;
	}

	/* Store the firmware version */
	qdev->fw_rev_id = mbcp->mbox_out[1];

	return status;
}

/* Get functional state for MPI firmware.
 * Returns zero on success.
 */
int ql_mb_get_fw_state(struct ql_adapter *qdev)
{
	struct mbox_params mbc;
	struct mbox_params *mbcp = &mbc;
	int status = 0;

	memset(mbcp, 0, sizeof(struct mbox_params));

	mbcp->in_count = 1;
	mbcp->out_count = 2;

	mbcp->mbox_in[0] = MB_CMD_GET_FW_STATE;

	status = ql_mailbox_command(qdev, mbcp);
	if (status)
		return status;

	if (mbcp->mbox_out[0] != MB_CMD_STS_GOOD) {
		QPRINTK(qdev, DRV, ERR,
			"Failed Get Firmware State.\n");
		status = -EIO;
	}

	/* If bit zero is set in mbx 1 then the firmware is
	 * running, but not initialized.  This should never
	 * happen.
	 */
	if (mbcp->mbox_out[1] & 1) {
		QPRINTK(qdev, DRV, ERR,
			"Firmware waiting for initialization.\n");
		status = -EIO;
	}

	return status;
}

/* Send and ACK mailbox command to the firmware to
 * let it continue with the change.
 */
int ql_mb_idc_ack(struct ql_adapter *qdev)
{
	struct mbox_params mbc;
	struct mbox_params *mbcp = &mbc;
	int status = 0;

	memset(mbcp, 0, sizeof(struct mbox_params));

	mbcp->in_count = 5;
	mbcp->out_count = 1;

	mbcp->mbox_in[0] = MB_CMD_IDC_ACK;
	mbcp->mbox_in[1] = qdev->idc_mbc.mbox_out[1];
	mbcp->mbox_in[2] = qdev->idc_mbc.mbox_out[2];
	mbcp->mbox_in[3] = qdev->idc_mbc.mbox_out[3];
	mbcp->mbox_in[4] = qdev->idc_mbc.mbox_out[4];

	status = ql_mailbox_command(qdev, mbcp);
	if (status)
		return status;

	if (mbcp->mbox_out[0] != MB_CMD_STS_GOOD) {
		QPRINTK(qdev, DRV, ERR,
			"Failed IDC ACK send.\n");
		status = -EIO;
	}
	return status;
}

/* Get link settings and maximum frame size settings
 * for the current port.
 * Most likely will block.
 */
static int ql_mb_set_port_cfg(struct ql_adapter *qdev)
{
	struct mbox_params mbc;
	struct mbox_params *mbcp = &mbc;
	int status = 0;

	memset(mbcp, 0, sizeof(struct mbox_params));

	mbcp->in_count = 3;
	mbcp->out_count = 1;

	mbcp->mbox_in[0] = MB_CMD_SET_PORT_CFG;
	mbcp->mbox_in[1] = qdev->link_config;
	mbcp->mbox_in[2] = qdev->max_frame_size;


	status = ql_mailbox_command(qdev, mbcp);
	if (status)
		return status;

	if (mbcp->mbox_out[0] == MB_CMD_STS_INTRMDT) {
		QPRINTK(qdev, DRV, ERR,
			"Port Config sent, wait for IDC.\n");
	} else	if (mbcp->mbox_out[0] != MB_CMD_STS_GOOD) {
		QPRINTK(qdev, DRV, ERR,
			"Failed Set Port Configuration.\n");
		status = -EIO;
	}
	return status;
}

/* Get link settings and maximum frame size settings
 * for the current port.
 * Most likely will block.
 */
static int ql_mb_get_port_cfg(struct ql_adapter *qdev)
{
	struct mbox_params mbc;
	struct mbox_params *mbcp = &mbc;
	int status = 0;

	memset(mbcp, 0, sizeof(struct mbox_params));

	mbcp->in_count = 1;
	mbcp->out_count = 3;

	mbcp->mbox_in[0] = MB_CMD_GET_PORT_CFG;

	status = ql_mailbox_command(qdev, mbcp);
	if (status)
		return status;

	if (mbcp->mbox_out[0] != MB_CMD_STS_GOOD) {
		QPRINTK(qdev, DRV, ERR,
			"Failed Get Port Configuration.\n");
		status = -EIO;
	} else	{
		QPRINTK(qdev, DRV, DEBUG,
			"Passed Get Port Configuration.\n");
		qdev->link_config = mbcp->mbox_out[1];
		qdev->max_frame_size = mbcp->mbox_out[2];
	}
	return status;
}

/* IDC - Inter Device Communication...
 * Some firmware commands require consent of adjacent FCOE
 * function.  This function waits for the OK, or a
 * counter-request for a little more time.i
 * The firmware will complete the request if the other
 * function doesn't respond.
 */
static int ql_idc_wait(struct ql_adapter *qdev)
{
	int status = -ETIMEDOUT;
	long wait_time = 1 * HZ;
	struct mbox_params *mbcp = &qdev->idc_mbc;
	do {
		/* Wait here for the command to complete
		 * via the IDC process.
		 */
		wait_time =
			wait_for_completion_timeout(&qdev->ide_completion,
							wait_time);
		if (!wait_time) {
			QPRINTK(qdev, DRV, ERR,
				"IDC Timeout.\n");
			break;
		}
		/* Now examine the response from the IDC process.
		 * We might have a good completion or a request for
		 * more wait time.
		 */
		if (mbcp->mbox_out[0] == AEN_IDC_EXT) {
			QPRINTK(qdev, DRV, ERR,
				"IDC Time Extension from function.\n");
			wait_time += (mbcp->mbox_out[1] >> 8) & 0x0000000f;
		} else if (mbcp->mbox_out[0] == AEN_IDC_CMPLT) {
			QPRINTK(qdev, DRV, ERR,
				"IDC Success.\n");
			status = 0;
			break;
		} else {
			QPRINTK(qdev, DRV, ERR,
				"IDC: Invalid State 0x%.04x.\n",
				mbcp->mbox_out[0]);
			status = -EIO;
			break;
		}
	} while (wait_time);

	return status;
}

int ql_mb_set_mgmnt_traffic_ctl(struct ql_adapter *qdev, u32 control)
{
	struct mbox_params mbc;
	struct mbox_params *mbcp = &mbc;
	int status;

	memset(mbcp, 0, sizeof(struct mbox_params));

	mbcp->in_count = 1;
	mbcp->out_count = 2;

	mbcp->mbox_in[0] = MB_CMD_SET_MGMNT_TFK_CTL;
	mbcp->mbox_in[1] = control;

	status = ql_mailbox_command(qdev, mbcp);
	if (status)
		return status;

	if (mbcp->mbox_out[0] == MB_CMD_STS_GOOD)
		return status;

	if (mbcp->mbox_out[0] == MB_CMD_STS_INVLD_CMD) {
		QPRINTK(qdev, DRV, ERR,
			"Command not supported by firmware.\n");
		status = -EINVAL;
	} else if (mbcp->mbox_out[0] == MB_CMD_STS_ERR) {
		/* This indicates that the firmware is
		 * already in the state we are trying to
		 * change it to.
		 */
		QPRINTK(qdev, DRV, ERR,
			"Command parameters make no change.\n");
	}
	return status;
}

/* Returns a negative error code or the mailbox command status. */
static int ql_mb_get_mgmnt_traffic_ctl(struct ql_adapter *qdev, u32 *control)
{
	struct mbox_params mbc;
	struct mbox_params *mbcp = &mbc;
	int status;

	memset(mbcp, 0, sizeof(struct mbox_params));
	*control = 0;

	mbcp->in_count = 1;
	mbcp->out_count = 1;

	mbcp->mbox_in[0] = MB_CMD_GET_MGMNT_TFK_CTL;

	status = ql_mailbox_command(qdev, mbcp);
	if (status)
		return status;

	if (mbcp->mbox_out[0] == MB_CMD_STS_GOOD) {
		*control = mbcp->mbox_in[1];
		return status;
	}

	if (mbcp->mbox_out[0] == MB_CMD_STS_INVLD_CMD) {
		QPRINTK(qdev, DRV, ERR,
			"Command not supported by firmware.\n");
		status = -EINVAL;
	} else if (mbcp->mbox_out[0] == MB_CMD_STS_ERR) {
		QPRINTK(qdev, DRV, ERR,
			"Failed to get MPI traffic control.\n");
		status = -EIO;
	}
	return status;
}

int ql_wait_fifo_empty(struct ql_adapter *qdev)
{
	int count = 5;
	u32 mgmnt_fifo_empty;
	u32 nic_fifo_empty;

	do {
		nic_fifo_empty = ql_read32(qdev, STS) & STS_NFE;
		ql_mb_get_mgmnt_traffic_ctl(qdev, &mgmnt_fifo_empty);
		mgmnt_fifo_empty &= MB_GET_MPI_TFK_FIFO_EMPTY;
		if (nic_fifo_empty && mgmnt_fifo_empty)
			return 0;
		msleep(100);
	} while (count-- > 0);
	return -ETIMEDOUT;
}

/* API called in work thread context to set new TX/RX
 * maximum frame size values to match MTU.
 */
static int ql_set_port_cfg(struct ql_adapter *qdev)
{
	int status;
	rtnl_lock();
	status = ql_mb_set_port_cfg(qdev);
	rtnl_unlock();
	if (status)
		return status;
	status = ql_idc_wait(qdev);
	return status;
}

/* The following routines are worker threads that process
 * events that may sleep waiting for completion.
 */

/* This thread gets the maximum TX and RX frame size values
 * from the firmware and, if necessary, changes them to match
 * the MTU setting.
 */
void ql_mpi_port_cfg_work(struct work_struct *work)
{
	struct ql_adapter *qdev =
	    container_of(work, struct ql_adapter, mpi_port_cfg_work.work);
	int status;

	rtnl_lock();
	status = ql_mb_get_port_cfg(qdev);
	rtnl_unlock();
	if (status) {
		QPRINTK(qdev, DRV, ERR,
			"Bug: Failed to get port config data.\n");
		goto err;
	}

	if (qdev->link_config & CFG_JUMBO_FRAME_SIZE &&
			qdev->max_frame_size ==
			CFG_DEFAULT_MAX_FRAME_SIZE)
		goto end;

	qdev->link_config |=	CFG_JUMBO_FRAME_SIZE;
	qdev->max_frame_size = CFG_DEFAULT_MAX_FRAME_SIZE;
	status = ql_set_port_cfg(qdev);
	if (status) {
		QPRINTK(qdev, DRV, ERR,
			"Bug: Failed to set port config data.\n");
		goto err;
	}
end:
	clear_bit(QL_PORT_CFG, &qdev->flags);
	return;
err:
	ql_queue_fw_error(qdev);
	goto end;
}

/* Process an inter-device request.  This is issues by
 * the firmware in response to another function requesting
 * a change to the port. We set a flag to indicate a change
 * has been made and then send a mailbox command ACKing
 * the change request.
 */
void ql_mpi_idc_work(struct work_struct *work)
{
	struct ql_adapter *qdev =
	    container_of(work, struct ql_adapter, mpi_idc_work.work);
	int status;
	struct mbox_params *mbcp = &qdev->idc_mbc;
	u32 aen;

	aen = mbcp->mbox_out[1] >> 16;

	switch (aen) {
	default:
		QPRINTK(qdev, DRV, ERR,
			"Bug: Unhandled IDC action.\n");
		break;
	case MB_CMD_PORT_RESET:
	case MB_CMD_SET_PORT_CFG:
	case MB_CMD_STOP_FW:
		ql_link_off(qdev);
		/* Signal the resulting link up AEN
		 * that the frame routing and mac addr
		 * needs to be set.
		 * */
		set_bit(QL_CAM_RT_SET, &qdev->flags);
		rtnl_lock();
		status = ql_mb_idc_ack(qdev);
		rtnl_unlock();
		if (status) {
			QPRINTK(qdev, DRV, ERR,
			"Bug: No pending IDC!\n");
		}
	}
}

void ql_mpi_work(struct work_struct *work)
{
	struct ql_adapter *qdev =
	    container_of(work, struct ql_adapter, mpi_work.work);
	struct mbox_params mbc;
	struct mbox_params *mbcp = &mbc;
	int err = 0;

	rtnl_lock();
	/* Begin polled mode for MPI */
	ql_write32(qdev, INTR_MASK, (INTR_MASK_PI << 16));

	while (ql_read32(qdev, STS) & STS_PI) {
		memset(mbcp, 0, sizeof(struct mbox_params));
		mbcp->out_count = 1;
		/* Don't continue if an async event
		 * did not complete properly.
		 */
		err = ql_mpi_handler(qdev, mbcp);
		if (err)
			break;
	}

	/* End polled mode for MPI */
	ql_write32(qdev, INTR_MASK, (INTR_MASK_PI << 16) | INTR_MASK_PI);
	rtnl_unlock();
	ql_enable_completion_interrupt(qdev, 0);
}

void ql_mpi_reset_work(struct work_struct *work)
{
	struct ql_adapter *qdev =
	    container_of(work, struct ql_adapter, mpi_reset_work.work);
	cancel_delayed_work_sync(&qdev->mpi_work);
	cancel_delayed_work_sync(&qdev->mpi_port_cfg_work);
	cancel_delayed_work_sync(&qdev->mpi_idc_work);
	ql_soft_reset_mpi_risc(qdev);
}
