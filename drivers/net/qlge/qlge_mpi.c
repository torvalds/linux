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
	int count = 50;	/* TODO: arbitrary for now. */
	u32 value;

	do {
		value = ql_read32(qdev, STS);
		if (value & STS_PI)
			return 0;
		udelay(UDELAY_DELAY); /* 10us */
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

static void ql_link_up(struct ql_adapter *qdev, struct mbox_params *mbcp)
{
	mbcp->out_count = 2;

	if (ql_get_mb_sts(qdev, mbcp))
		goto exit;

	qdev->link_status = mbcp->mbox_out[1];
	QPRINTK(qdev, DRV, ERR, "Link Up.\n");
	QPRINTK(qdev, DRV, INFO, "Link Status = 0x%.08x.\n", mbcp->mbox_out[1]);
	if (!netif_carrier_ok(qdev->ndev)) {
		QPRINTK(qdev, LINK, INFO, "Link is Up.\n");
		netif_carrier_on(qdev->ndev);
		netif_wake_queue(qdev->ndev);
	}
exit:
	/* Clear the MPI firmware status. */
	ql_write32(qdev, CSR, CSR_CMD_CLR_R2PCI_INT);
}

static void ql_link_down(struct ql_adapter *qdev, struct mbox_params *mbcp)
{
	mbcp->out_count = 3;

	if (ql_get_mb_sts(qdev, mbcp)) {
		QPRINTK(qdev, DRV, ERR, "Firmware did not initialize!\n");
		goto exit;
	}

	if (netif_carrier_ok(qdev->ndev)) {
		QPRINTK(qdev, LINK, INFO, "Link is Down.\n");
		netif_carrier_off(qdev->ndev);
		netif_stop_queue(qdev->ndev);
	}
	QPRINTK(qdev, DRV, ERR, "Link Down.\n");
	QPRINTK(qdev, DRV, ERR, "Link Status = 0x%.08x.\n", mbcp->mbox_out[1]);
exit:
	/* Clear the MPI firmware status. */
	ql_write32(qdev, CSR, CSR_CMD_CLR_R2PCI_INT);
}

static void ql_init_fw_done(struct ql_adapter *qdev, struct mbox_params *mbcp)
{
	mbcp->out_count = 2;

	if (ql_get_mb_sts(qdev, mbcp)) {
		QPRINTK(qdev, DRV, ERR, "Firmware did not initialize!\n");
		goto exit;
	}
	QPRINTK(qdev, DRV, ERR, "Firmware initialized!\n");
	QPRINTK(qdev, DRV, ERR, "Firmware status = 0x%.08x.\n",
		mbcp->mbox_out[0]);
	QPRINTK(qdev, DRV, ERR, "Firmware Revision  = 0x%.08x.\n",
		mbcp->mbox_out[1]);
exit:
	/* Clear the MPI firmware status. */
	ql_write32(qdev, CSR, CSR_CMD_CLR_R2PCI_INT);
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

	case AEN_LINK_UP:
		ql_link_up(qdev, mbcp);
		break;

	case AEN_LINK_DOWN:
		ql_link_down(qdev, mbcp);
		break;

	case AEN_FW_INIT_DONE:
		ql_init_fw_done(qdev, mbcp);
		break;

	case AEN_FW_INIT_FAIL:
	case AEN_SYS_ERR:
		ql_queue_fw_error(qdev);
		break;

	default:
		QPRINTK(qdev, DRV, ERR,
			"Unsupported AE %.08x.\n", mbcp->mbox_out[0]);
		/* Clear the MPI firmware status. */
	}
end:
	ql_write32(qdev, CSR, CSR_CMD_CLR_R2PCI_INT);
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

	mutex_lock(&qdev->mpi_mutex);

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
	mutex_unlock(&qdev->mpi_mutex);
	/* End polled mode for MPI */
	ql_write32(qdev, INTR_MASK, (INTR_MASK_PI << 16) | INTR_MASK_PI);
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

void ql_mpi_work(struct work_struct *work)
{
	struct ql_adapter *qdev =
	    container_of(work, struct ql_adapter, mpi_work.work);
	struct mbox_params mbc;
	struct mbox_params *mbcp = &mbc;

	mutex_lock(&qdev->mpi_mutex);

	while (ql_read32(qdev, STS) & STS_PI) {
		memset(mbcp, 0, sizeof(struct mbox_params));
		mbcp->out_count = 1;
		ql_mpi_handler(qdev, mbcp);
	}

	mutex_unlock(&qdev->mpi_mutex);
	ql_enable_completion_interrupt(qdev, 0);
}

void ql_mpi_reset_work(struct work_struct *work)
{
	struct ql_adapter *qdev =
	    container_of(work, struct ql_adapter, mpi_reset_work.work);
	ql_soft_reset_mpi_risc(qdev);
}
