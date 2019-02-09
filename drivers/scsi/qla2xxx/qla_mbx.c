/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"
#include "qla_target.h"

#include <linux/delay.h>
#include <linux/gfp.h>

static struct mb_cmd_name {
	uint16_t cmd;
	const char *str;
} mb_str[] = {
	{MBC_GET_PORT_DATABASE,		"GPDB"},
	{MBC_GET_ID_LIST,		"GIDList"},
	{MBC_GET_LINK_PRIV_STATS,	"Stats"},
	{MBC_GET_RESOURCE_COUNTS,	"ResCnt"},
};

static const char *mb_to_str(uint16_t cmd)
{
	int i;
	struct mb_cmd_name *e;

	for (i = 0; i < ARRAY_SIZE(mb_str); i++) {
		e = mb_str + i;
		if (cmd == e->cmd)
			return e->str;
	}
	return "unknown";
}

static struct rom_cmd {
	uint16_t cmd;
} rom_cmds[] = {
	{ MBC_LOAD_RAM },
	{ MBC_EXECUTE_FIRMWARE },
	{ MBC_READ_RAM_WORD },
	{ MBC_MAILBOX_REGISTER_TEST },
	{ MBC_VERIFY_CHECKSUM },
	{ MBC_GET_FIRMWARE_VERSION },
	{ MBC_LOAD_RISC_RAM },
	{ MBC_DUMP_RISC_RAM },
	{ MBC_LOAD_RISC_RAM_EXTENDED },
	{ MBC_DUMP_RISC_RAM_EXTENDED },
	{ MBC_WRITE_RAM_WORD_EXTENDED },
	{ MBC_READ_RAM_EXTENDED },
	{ MBC_GET_RESOURCE_COUNTS },
	{ MBC_SET_FIRMWARE_OPTION },
	{ MBC_MID_INITIALIZE_FIRMWARE },
	{ MBC_GET_FIRMWARE_STATE },
	{ MBC_GET_MEM_OFFLOAD_CNTRL_STAT },
	{ MBC_GET_RETRY_COUNT },
	{ MBC_TRACE_CONTROL },
	{ MBC_INITIALIZE_MULTIQ },
	{ MBC_IOCB_COMMAND_A64 },
	{ MBC_GET_ADAPTER_LOOP_ID },
	{ MBC_READ_SFP },
	{ MBC_GET_RNID_PARAMS },
};

static int is_rom_cmd(uint16_t cmd)
{
	int i;
	struct  rom_cmd *wc;

	for (i = 0; i < ARRAY_SIZE(rom_cmds); i++) {
		wc = rom_cmds + i;
		if (wc->cmd == cmd)
			return 1;
	}

	return 0;
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
qla2x00_mailbox_command(scsi_qla_host_t *vha, mbx_cmd_t *mcp)
{
	int		rval, i;
	unsigned long    flags = 0;
	device_reg_t *reg;
	uint8_t		abort_active;
	uint8_t		io_lock_on;
	uint16_t	command = 0;
	uint16_t	*iptr;
	uint16_t __iomem *optr;
	uint32_t	cnt;
	uint32_t	mboxes;
	unsigned long	wait_time;
	struct qla_hw_data *ha = vha->hw;
	scsi_qla_host_t *base_vha = pci_get_drvdata(ha->pdev);
	u32 chip_reset;


	ql_dbg(ql_dbg_mbx, vha, 0x1000, "Entered %s.\n", __func__);

	if (ha->pdev->error_state > pci_channel_io_frozen) {
		ql_log(ql_log_warn, vha, 0x1001,
		    "error_state is greater than pci_channel_io_frozen, "
		    "exiting.\n");
		return QLA_FUNCTION_TIMEOUT;
	}

	if (vha->device_flags & DFLG_DEV_FAILED) {
		ql_log(ql_log_warn, vha, 0x1002,
		    "Device in failed state, exiting.\n");
		return QLA_FUNCTION_TIMEOUT;
	}

	/* if PCI error, then avoid mbx processing.*/
	if (test_bit(PFLG_DISCONNECTED, &base_vha->dpc_flags) &&
	    test_bit(UNLOADING, &base_vha->dpc_flags)) {
		ql_log(ql_log_warn, vha, 0xd04e,
		    "PCI error, exiting.\n");
		return QLA_FUNCTION_TIMEOUT;
	}

	reg = ha->iobase;
	io_lock_on = base_vha->flags.init_done;

	rval = QLA_SUCCESS;
	abort_active = test_bit(ABORT_ISP_ACTIVE, &base_vha->dpc_flags);
	chip_reset = ha->chip_reset;

	if (ha->flags.pci_channel_io_perm_failure) {
		ql_log(ql_log_warn, vha, 0x1003,
		    "Perm failure on EEH timeout MBX, exiting.\n");
		return QLA_FUNCTION_TIMEOUT;
	}

	if (IS_P3P_TYPE(ha) && ha->flags.isp82xx_fw_hung) {
		/* Setting Link-Down error */
		mcp->mb[0] = MBS_LINK_DOWN_ERROR;
		ql_log(ql_log_warn, vha, 0x1004,
		    "FW hung = %d.\n", ha->flags.isp82xx_fw_hung);
		return QLA_FUNCTION_TIMEOUT;
	}

	/* check if ISP abort is active and return cmd with timeout */
	if ((test_bit(ABORT_ISP_ACTIVE, &base_vha->dpc_flags) ||
	    test_bit(ISP_ABORT_RETRY, &base_vha->dpc_flags) ||
	    test_bit(ISP_ABORT_NEEDED, &base_vha->dpc_flags)) &&
	    !is_rom_cmd(mcp->mb[0])) {
		ql_log(ql_log_info, vha, 0x1005,
		    "Cmd 0x%x aborted with timeout since ISP Abort is pending\n",
		    mcp->mb[0]);
		return QLA_FUNCTION_TIMEOUT;
	}

	atomic_inc(&ha->num_pend_mbx_stage1);
	/*
	 * Wait for active mailbox commands to finish by waiting at most tov
	 * seconds. This is to serialize actual issuing of mailbox cmds during
	 * non ISP abort time.
	 */
	if (!wait_for_completion_timeout(&ha->mbx_cmd_comp, mcp->tov * HZ)) {
		/* Timeout occurred. Return error. */
		ql_log(ql_log_warn, vha, 0xd035,
		    "Cmd access timeout, cmd=0x%x, Exiting.\n",
		    mcp->mb[0]);
		atomic_dec(&ha->num_pend_mbx_stage1);
		return QLA_FUNCTION_TIMEOUT;
	}
	atomic_dec(&ha->num_pend_mbx_stage1);
	if (ha->flags.purge_mbox || chip_reset != ha->chip_reset) {
		rval = QLA_ABORTED;
		goto premature_exit;
	}

	ha->flags.mbox_busy = 1;
	/* Save mailbox command for debug */
	ha->mcp = mcp;

	ql_dbg(ql_dbg_mbx, vha, 0x1006,
	    "Prepare to issue mbox cmd=0x%x.\n", mcp->mb[0]);

	spin_lock_irqsave(&ha->hardware_lock, flags);

	if (ha->flags.purge_mbox || chip_reset != ha->chip_reset) {
		rval = QLA_ABORTED;
		ha->flags.mbox_busy = 0;
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
		goto premature_exit;
	}

	/* Load mailbox registers. */
	if (IS_P3P_TYPE(ha))
		optr = (uint16_t __iomem *)&reg->isp82.mailbox_in[0];
	else if (IS_FWI2_CAPABLE(ha) && !(IS_P3P_TYPE(ha)))
		optr = (uint16_t __iomem *)&reg->isp24.mailbox0;
	else
		optr = (uint16_t __iomem *)MAILBOX_REG(ha, &reg->isp, 0);

	iptr = mcp->mb;
	command = mcp->mb[0];
	mboxes = mcp->out_mb;

	ql_dbg(ql_dbg_mbx, vha, 0x1111,
	    "Mailbox registers (OUT):\n");
	for (cnt = 0; cnt < ha->mbx_count; cnt++) {
		if (IS_QLA2200(ha) && cnt == 8)
			optr =
			    (uint16_t __iomem *)MAILBOX_REG(ha, &reg->isp, 8);
		if (mboxes & BIT_0) {
			ql_dbg(ql_dbg_mbx, vha, 0x1112,
			    "mbox[%d]<-0x%04x\n", cnt, *iptr);
			WRT_REG_WORD(optr, *iptr);
		}

		mboxes >>= 1;
		optr++;
		iptr++;
	}

	ql_dbg(ql_dbg_mbx + ql_dbg_buffer, vha, 0x1117,
	    "I/O Address = %p.\n", optr);

	/* Issue set host interrupt command to send cmd out. */
	ha->flags.mbox_int = 0;
	clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

	/* Unlock mbx registers and wait for interrupt */
	ql_dbg(ql_dbg_mbx, vha, 0x100f,
	    "Going to unlock irq & waiting for interrupts. "
	    "jiffies=%lx.\n", jiffies);

	/* Wait for mbx cmd completion until timeout */
	atomic_inc(&ha->num_pend_mbx_stage2);
	if ((!abort_active && io_lock_on) || IS_NOPOLLING_TYPE(ha)) {
		set_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags);

		if (IS_P3P_TYPE(ha)) {
			if (RD_REG_DWORD(&reg->isp82.hint) &
				HINT_MBX_INT_PENDING) {
				spin_unlock_irqrestore(&ha->hardware_lock,
					flags);
				ha->flags.mbox_busy = 0;
				atomic_dec(&ha->num_pend_mbx_stage2);
				ql_dbg(ql_dbg_mbx, vha, 0x1010,
				    "Pending mailbox timeout, exiting.\n");
				rval = QLA_FUNCTION_TIMEOUT;
				goto premature_exit;
			}
			WRT_REG_DWORD(&reg->isp82.hint, HINT_MBX_INT_PENDING);
		} else if (IS_FWI2_CAPABLE(ha))
			WRT_REG_DWORD(&reg->isp24.hccr, HCCRX_SET_HOST_INT);
		else
			WRT_REG_WORD(&reg->isp.hccr, HCCR_SET_HOST_INT);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		wait_time = jiffies;
		atomic_inc(&ha->num_pend_mbx_stage3);
		if (!wait_for_completion_timeout(&ha->mbx_intr_comp,
		    mcp->tov * HZ)) {
			ql_dbg(ql_dbg_mbx, vha, 0x117a,
			    "cmd=%x Timeout.\n", command);
			spin_lock_irqsave(&ha->hardware_lock, flags);
			clear_bit(MBX_INTR_WAIT, &ha->mbx_cmd_flags);
			spin_unlock_irqrestore(&ha->hardware_lock, flags);

		} else if (ha->flags.purge_mbox ||
		    chip_reset != ha->chip_reset) {
			ha->flags.mbox_busy = 0;
			atomic_dec(&ha->num_pend_mbx_stage2);
			atomic_dec(&ha->num_pend_mbx_stage3);
			rval = QLA_ABORTED;
			goto premature_exit;
		}
		atomic_dec(&ha->num_pend_mbx_stage3);

		if (time_after(jiffies, wait_time + 5 * HZ))
			ql_log(ql_log_warn, vha, 0x1015, "cmd=0x%x, waited %d msecs\n",
			    command, jiffies_to_msecs(jiffies - wait_time));
	} else {
		ql_dbg(ql_dbg_mbx, vha, 0x1011,
		    "Cmd=%x Polling Mode.\n", command);

		if (IS_P3P_TYPE(ha)) {
			if (RD_REG_DWORD(&reg->isp82.hint) &
				HINT_MBX_INT_PENDING) {
				spin_unlock_irqrestore(&ha->hardware_lock,
					flags);
				ha->flags.mbox_busy = 0;
				atomic_dec(&ha->num_pend_mbx_stage2);
				ql_dbg(ql_dbg_mbx, vha, 0x1012,
				    "Pending mailbox timeout, exiting.\n");
				rval = QLA_FUNCTION_TIMEOUT;
				goto premature_exit;
			}
			WRT_REG_DWORD(&reg->isp82.hint, HINT_MBX_INT_PENDING);
		} else if (IS_FWI2_CAPABLE(ha))
			WRT_REG_DWORD(&reg->isp24.hccr, HCCRX_SET_HOST_INT);
		else
			WRT_REG_WORD(&reg->isp.hccr, HCCR_SET_HOST_INT);
		spin_unlock_irqrestore(&ha->hardware_lock, flags);

		wait_time = jiffies + mcp->tov * HZ; /* wait at most tov secs */
		while (!ha->flags.mbox_int) {
			if (ha->flags.purge_mbox ||
			    chip_reset != ha->chip_reset) {
				ha->flags.mbox_busy = 0;
				atomic_dec(&ha->num_pend_mbx_stage2);
				rval = QLA_ABORTED;
				goto premature_exit;
			}

			if (time_after(jiffies, wait_time))
				break;

			/*
			 * Check if it's UNLOADING, cause we cannot poll in
			 * this case, or else a NULL pointer dereference
			 * is triggered.
			 */
			if (unlikely(test_bit(UNLOADING, &base_vha->dpc_flags)))
				return QLA_FUNCTION_TIMEOUT;

			/* Check for pending interrupts. */
			qla2x00_poll(ha->rsp_q_map[0]);

			if (!ha->flags.mbox_int &&
			    !(IS_QLA2200(ha) &&
			    command == MBC_LOAD_RISC_RAM_EXTENDED))
				msleep(10);
		} /* while */
		ql_dbg(ql_dbg_mbx, vha, 0x1013,
		    "Waited %d sec.\n",
		    (uint)((jiffies - (wait_time - (mcp->tov * HZ)))/HZ));
	}
	atomic_dec(&ha->num_pend_mbx_stage2);

	/* Check whether we timed out */
	if (ha->flags.mbox_int) {
		uint16_t *iptr2;

		ql_dbg(ql_dbg_mbx, vha, 0x1014,
		    "Cmd=%x completed.\n", command);

		/* Got interrupt. Clear the flag. */
		ha->flags.mbox_int = 0;
		clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

		if (IS_P3P_TYPE(ha) && ha->flags.isp82xx_fw_hung) {
			ha->flags.mbox_busy = 0;
			/* Setting Link-Down error */
			mcp->mb[0] = MBS_LINK_DOWN_ERROR;
			ha->mcp = NULL;
			rval = QLA_FUNCTION_FAILED;
			ql_log(ql_log_warn, vha, 0xd048,
			    "FW hung = %d.\n", ha->flags.isp82xx_fw_hung);
			goto premature_exit;
		}

		if (ha->mailbox_out[0] != MBS_COMMAND_COMPLETE)
			rval = QLA_FUNCTION_FAILED;

		/* Load return mailbox registers. */
		iptr2 = mcp->mb;
		iptr = (uint16_t *)&ha->mailbox_out[0];
		mboxes = mcp->in_mb;

		ql_dbg(ql_dbg_mbx, vha, 0x1113,
		    "Mailbox registers (IN):\n");
		for (cnt = 0; cnt < ha->mbx_count; cnt++) {
			if (mboxes & BIT_0) {
				*iptr2 = *iptr;
				ql_dbg(ql_dbg_mbx, vha, 0x1114,
				    "mbox[%d]->0x%04x\n", cnt, *iptr2);
			}

			mboxes >>= 1;
			iptr2++;
			iptr++;
		}
	} else {

		uint16_t mb[8];
		uint32_t ictrl, host_status, hccr;
		uint16_t        w;

		if (IS_FWI2_CAPABLE(ha)) {
			mb[0] = RD_REG_WORD(&reg->isp24.mailbox0);
			mb[1] = RD_REG_WORD(&reg->isp24.mailbox1);
			mb[2] = RD_REG_WORD(&reg->isp24.mailbox2);
			mb[3] = RD_REG_WORD(&reg->isp24.mailbox3);
			mb[7] = RD_REG_WORD(&reg->isp24.mailbox7);
			ictrl = RD_REG_DWORD(&reg->isp24.ictrl);
			host_status = RD_REG_DWORD(&reg->isp24.host_status);
			hccr = RD_REG_DWORD(&reg->isp24.hccr);

			ql_log(ql_log_warn, vha, 0xd04c,
			    "MBX Command timeout for cmd %x, iocontrol=%x jiffies=%lx "
			    "mb[0-3]=[0x%x 0x%x 0x%x 0x%x] mb7 0x%x host_status 0x%x hccr 0x%x\n",
			    command, ictrl, jiffies, mb[0], mb[1], mb[2], mb[3],
			    mb[7], host_status, hccr);

		} else {
			mb[0] = RD_MAILBOX_REG(ha, &reg->isp, 0);
			ictrl = RD_REG_WORD(&reg->isp.ictrl);
			ql_dbg(ql_dbg_mbx + ql_dbg_buffer, vha, 0x1119,
			    "MBX Command timeout for cmd %x, iocontrol=%x jiffies=%lx "
			    "mb[0]=0x%x\n", command, ictrl, jiffies, mb[0]);
		}
		ql_dump_regs(ql_dbg_mbx + ql_dbg_buffer, vha, 0x1019);

		/* Capture FW dump only, if PCI device active */
		if (!pci_channel_offline(vha->hw->pdev)) {
			pci_read_config_word(ha->pdev, PCI_VENDOR_ID, &w);
			if (w == 0xffff || ictrl == 0xffffffff ||
			    (chip_reset != ha->chip_reset)) {
				/* This is special case if there is unload
				 * of driver happening and if PCI device go
				 * into bad state due to PCI error condition
				 * then only PCI ERR flag would be set.
				 * we will do premature exit for above case.
				 */
				ha->flags.mbox_busy = 0;
				rval = QLA_FUNCTION_TIMEOUT;
				goto premature_exit;
			}

			/* Attempt to capture firmware dump for further
			 * anallysis of the current formware state. we do not
			 * need to do this if we are intentionally generating
			 * a dump
			 */
			if (mcp->mb[0] != MBC_GEN_SYSTEM_ERROR)
				ha->isp_ops->fw_dump(vha, 0);
			rval = QLA_FUNCTION_TIMEOUT;
		 }
	}

	ha->flags.mbox_busy = 0;

	/* Clean up */
	ha->mcp = NULL;

	if ((abort_active || !io_lock_on) && !IS_NOPOLLING_TYPE(ha)) {
		ql_dbg(ql_dbg_mbx, vha, 0x101a,
		    "Checking for additional resp interrupt.\n");

		/* polling mode for non isp_abort commands. */
		qla2x00_poll(ha->rsp_q_map[0]);
	}

	if (rval == QLA_FUNCTION_TIMEOUT &&
	    mcp->mb[0] != MBC_GEN_SYSTEM_ERROR) {
		if (!io_lock_on || (mcp->flags & IOCTL_CMD) ||
		    ha->flags.eeh_busy) {
			/* not in dpc. schedule it for dpc to take over. */
			ql_dbg(ql_dbg_mbx, vha, 0x101b,
			    "Timeout, schedule isp_abort_needed.\n");

			if (!test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags) &&
			    !test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags) &&
			    !test_bit(ISP_ABORT_RETRY, &vha->dpc_flags)) {
				if (IS_QLA82XX(ha)) {
					ql_dbg(ql_dbg_mbx, vha, 0x112a,
					    "disabling pause transmit on port "
					    "0 & 1.\n");
					qla82xx_wr_32(ha,
					    QLA82XX_CRB_NIU + 0x98,
					    CRB_NIU_XG_PAUSE_CTL_P0|
					    CRB_NIU_XG_PAUSE_CTL_P1);
				}
				ql_log(ql_log_info, base_vha, 0x101c,
				    "Mailbox cmd timeout occurred, cmd=0x%x, "
				    "mb[0]=0x%x, eeh_busy=0x%x. Scheduling ISP "
				    "abort.\n", command, mcp->mb[0],
				    ha->flags.eeh_busy);
				set_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
				qla2xxx_wake_dpc(vha);
			}
		} else if (current == ha->dpc_thread) {
			/* call abort directly since we are in the DPC thread */
			ql_dbg(ql_dbg_mbx, vha, 0x101d,
			    "Timeout, calling abort_isp.\n");

			if (!test_bit(ISP_ABORT_NEEDED, &vha->dpc_flags) &&
			    !test_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags) &&
			    !test_bit(ISP_ABORT_RETRY, &vha->dpc_flags)) {
				if (IS_QLA82XX(ha)) {
					ql_dbg(ql_dbg_mbx, vha, 0x112b,
					    "disabling pause transmit on port "
					    "0 & 1.\n");
					qla82xx_wr_32(ha,
					    QLA82XX_CRB_NIU + 0x98,
					    CRB_NIU_XG_PAUSE_CTL_P0|
					    CRB_NIU_XG_PAUSE_CTL_P1);
				}
				ql_log(ql_log_info, base_vha, 0x101e,
				    "Mailbox cmd timeout occurred, cmd=0x%x, "
				    "mb[0]=0x%x. Scheduling ISP abort ",
				    command, mcp->mb[0]);
				set_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags);
				clear_bit(ISP_ABORT_NEEDED, &vha->dpc_flags);
				/* Allow next mbx cmd to come in. */
				complete(&ha->mbx_cmd_comp);
				if (ha->isp_ops->abort_isp(vha)) {
					/* Failed. retry later. */
					set_bit(ISP_ABORT_NEEDED,
					    &vha->dpc_flags);
				}
				clear_bit(ABORT_ISP_ACTIVE, &vha->dpc_flags);
				ql_dbg(ql_dbg_mbx, vha, 0x101f,
				    "Finished abort_isp.\n");
				goto mbx_done;
			}
		}
	}

premature_exit:
	/* Allow next mbx cmd to come in. */
	complete(&ha->mbx_cmd_comp);

mbx_done:
	if (rval == QLA_ABORTED) {
		ql_log(ql_log_info, vha, 0xd035,
		    "Chip Reset in progress. Purging Mbox cmd=0x%x.\n",
		    mcp->mb[0]);
	} else if (rval) {
		if (ql2xextended_error_logging & (ql_dbg_disc|ql_dbg_mbx)) {
			pr_warn("%s [%s]-%04x:%ld: **** Failed", QL_MSGHDR,
			    dev_name(&ha->pdev->dev), 0x1020+0x800,
			    vha->host_no);
			mboxes = mcp->in_mb;
			cnt = 4;
			for (i = 0; i < ha->mbx_count && cnt; i++, mboxes >>= 1)
				if (mboxes & BIT_0) {
					printk(" mb[%u]=%x", i, mcp->mb[i]);
					cnt--;
				}
			pr_warn(" cmd=%x ****\n", command);
		}
		if (IS_FWI2_CAPABLE(ha) && !(IS_P3P_TYPE(ha))) {
			ql_dbg(ql_dbg_mbx, vha, 0x1198,
			    "host_status=%#x intr_ctrl=%#x intr_status=%#x\n",
			    RD_REG_DWORD(&reg->isp24.host_status),
			    RD_REG_DWORD(&reg->isp24.ictrl),
			    RD_REG_DWORD(&reg->isp24.istatus));
		} else {
			ql_dbg(ql_dbg_mbx, vha, 0x1206,
			    "ctrl_status=%#x ictrl=%#x istatus=%#x\n",
			    RD_REG_WORD(&reg->isp.ctrl_status),
			    RD_REG_WORD(&reg->isp.ictrl),
			    RD_REG_WORD(&reg->isp.istatus));
		}
	} else {
		ql_dbg(ql_dbg_mbx, base_vha, 0x1021, "Done %s.\n", __func__);
	}

	return rval;
}

int
qla2x00_load_ram(scsi_qla_host_t *vha, dma_addr_t req_dma, uint32_t risc_addr,
    uint32_t risc_code_size)
{
	int rval;
	struct qla_hw_data *ha = vha->hw;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1022,
	    "Entered %s.\n", __func__);

	if (MSW(risc_addr) || IS_FWI2_CAPABLE(ha)) {
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
	if (IS_FWI2_CAPABLE(ha)) {
		mcp->mb[4] = MSW(risc_code_size);
		mcp->mb[5] = LSW(risc_code_size);
		mcp->out_mb |= MBX_5|MBX_4;
	} else {
		mcp->mb[4] = LSW(risc_code_size);
		mcp->out_mb |= MBX_4;
	}

	mcp->in_mb = MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1023,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1024,
		    "Done %s.\n", __func__);
	}

	return rval;
}

#define	EXTENDED_BB_CREDITS	BIT_0
#define	NVME_ENABLE_FLAG	BIT_3
static inline uint16_t qla25xx_set_sfp_lr_dist(struct qla_hw_data *ha)
{
	uint16_t mb4 = BIT_0;

	if (IS_QLA83XX(ha) || IS_QLA27XX(ha))
		mb4 |= ha->long_range_distance << LR_DIST_FW_POS;

	return mb4;
}

static inline uint16_t qla25xx_set_nvr_lr_dist(struct qla_hw_data *ha)
{
	uint16_t mb4 = BIT_0;

	if (IS_QLA83XX(ha) || IS_QLA27XX(ha)) {
		struct nvram_81xx *nv = ha->nvram;

		mb4 |= LR_DIST_FW_FIELD(nv->enhanced_features);
	}

	return mb4;
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
qla2x00_execute_fw(scsi_qla_host_t *vha, uint32_t risc_addr)
{
	int rval;
	struct qla_hw_data *ha = vha->hw;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1025,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_EXECUTE_FIRMWARE;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_0;
	if (IS_FWI2_CAPABLE(ha)) {
		mcp->mb[1] = MSW(risc_addr);
		mcp->mb[2] = LSW(risc_addr);
		mcp->mb[3] = 0;
		mcp->mb[4] = 0;
		ha->flags.using_lr_setting = 0;
		if (IS_QLA25XX(ha) || IS_QLA81XX(ha) || IS_QLA83XX(ha) ||
		    IS_QLA27XX(ha)) {
			if (ql2xautodetectsfp) {
				if (ha->flags.detected_lr_sfp) {
					mcp->mb[4] |=
					    qla25xx_set_sfp_lr_dist(ha);
					ha->flags.using_lr_setting = 1;
				}
			} else {
				struct nvram_81xx *nv = ha->nvram;
				/* set LR distance if specified in nvram */
				if (nv->enhanced_features &
				    NEF_LR_DIST_ENABLE) {
					mcp->mb[4] |=
					    qla25xx_set_nvr_lr_dist(ha);
					ha->flags.using_lr_setting = 1;
				}
			}
		}

		if (ql2xnvmeenable && IS_QLA27XX(ha))
			mcp->mb[4] |= NVME_ENABLE_FLAG;

		if (IS_QLA83XX(ha) || IS_QLA27XX(ha)) {
			struct nvram_81xx *nv = ha->nvram;
			/* set minimum speed if specified in nvram */
			if (nv->min_link_speed >= 2 &&
			    nv->min_link_speed <= 5) {
				mcp->mb[4] |= BIT_4;
				mcp->mb[11] = nv->min_link_speed;
				mcp->out_mb |= MBX_11;
				mcp->in_mb |= BIT_5;
				vha->min_link_speed_feat = nv->min_link_speed;
			}
		}

		if (ha->flags.exlogins_enabled)
			mcp->mb[4] |= ENABLE_EXTENDED_LOGIN;

		if (ha->flags.exchoffld_enabled)
			mcp->mb[4] |= ENABLE_EXCHANGE_OFFLD;

		mcp->out_mb |= MBX_4|MBX_3|MBX_2|MBX_1;
		mcp->in_mb |= MBX_3 | MBX_2 | MBX_1;
	} else {
		mcp->mb[1] = LSW(risc_addr);
		mcp->out_mb |= MBX_1;
		if (IS_QLA2322(ha) || IS_QLA6322(ha)) {
			mcp->mb[2] = 0;
			mcp->out_mb |= MBX_2;
		}
	}

	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1026,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		if (IS_FWI2_CAPABLE(ha)) {
			ha->fw_ability_mask = mcp->mb[3] << 16 | mcp->mb[2];
			ql_dbg(ql_dbg_mbx, vha, 0x119a,
			    "fw_ability_mask=%x.\n", ha->fw_ability_mask);
			ql_dbg(ql_dbg_mbx, vha, 0x1027,
			    "exchanges=%x.\n", mcp->mb[1]);
			if (IS_QLA83XX(ha) || IS_QLA27XX(ha)) {
				ha->max_speed_sup = mcp->mb[2] & BIT_0;
				ql_dbg(ql_dbg_mbx, vha, 0x119b,
				    "Maximum speed supported=%s.\n",
				    ha->max_speed_sup ? "32Gps" : "16Gps");
				if (vha->min_link_speed_feat) {
					ha->min_link_speed = mcp->mb[5];
					ql_dbg(ql_dbg_mbx, vha, 0x119c,
					    "Minimum speed set=%s.\n",
					    mcp->mb[5] == 5 ? "32Gps" :
					    mcp->mb[5] == 4 ? "16Gps" :
					    mcp->mb[5] == 3 ? "8Gps" :
					    mcp->mb[5] == 2 ? "4Gps" :
						"unknown");
				}
			}
		}
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1028,
		    "Done.\n");
	}

	return rval;
}

/*
 * qla_get_exlogin_status
 *	Get extended login status
 *	uses the memory offload control/status Mailbox
 *
 * Input:
 *	ha:		adapter state pointer.
 *	fwopt:		firmware options
 *
 * Returns:
 *	qla2x00 local function status
 *
 * Context:
 *	Kernel context.
 */
#define	FETCH_XLOGINS_STAT	0x8
int
qla_get_exlogin_status(scsi_qla_host_t *vha, uint16_t *buf_sz,
	uint16_t *ex_logins_cnt)
{
	int rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x118f,
	    "Entered %s\n", __func__);

	memset(mcp->mb, 0 , sizeof(mcp->mb));
	mcp->mb[0] = MBC_GET_MEM_OFFLOAD_CNTRL_STAT;
	mcp->mb[1] = FETCH_XLOGINS_STAT;
	mcp->out_mb = MBX_1|MBX_0;
	mcp->in_mb = MBX_10|MBX_4|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;

	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1115, "Failed=%x.\n", rval);
	} else {
		*buf_sz = mcp->mb[4];
		*ex_logins_cnt = mcp->mb[10];

		ql_log(ql_log_info, vha, 0x1190,
		    "buffer size 0x%x, exchange login count=%d\n",
		    mcp->mb[4], mcp->mb[10]);

		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1116,
		    "Done %s.\n", __func__);
	}

	return rval;
}

/*
 * qla_set_exlogin_mem_cfg
 *	set extended login memory configuration
 *	Mbx needs to be issues before init_cb is set
 *
 * Input:
 *	ha:		adapter state pointer.
 *	buffer:		buffer pointer
 *	phys_addr:	physical address of buffer
 *	size:		size of buffer
 *	TARGET_QUEUE_LOCK must be released
 *	ADAPTER_STATE_LOCK must be release
 *
 * Returns:
 *	qla2x00 local funxtion status code.
 *
 * Context:
 *	Kernel context.
 */
#define CONFIG_XLOGINS_MEM	0x3
int
qla_set_exlogin_mem_cfg(scsi_qla_host_t *vha, dma_addr_t phys_addr)
{
	int		rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x111a,
	    "Entered %s.\n", __func__);

	memset(mcp->mb, 0 , sizeof(mcp->mb));
	mcp->mb[0] = MBC_GET_MEM_OFFLOAD_CNTRL_STAT;
	mcp->mb[1] = CONFIG_XLOGINS_MEM;
	mcp->mb[2] = MSW(phys_addr);
	mcp->mb[3] = LSW(phys_addr);
	mcp->mb[6] = MSW(MSD(phys_addr));
	mcp->mb[7] = LSW(MSD(phys_addr));
	mcp->mb[8] = MSW(ha->exlogin_size);
	mcp->mb[9] = LSW(ha->exlogin_size);
	mcp->out_mb = MBX_9|MBX_8|MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_11|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx, vha, 0x111b, "Failed=%x.\n", rval);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x118c,
		    "Done %s.\n", __func__);
	}

	return rval;
}

/*
 * qla_get_exchoffld_status
 *	Get exchange offload status
 *	uses the memory offload control/status Mailbox
 *
 * Input:
 *	ha:		adapter state pointer.
 *	fwopt:		firmware options
 *
 * Returns:
 *	qla2x00 local function status
 *
 * Context:
 *	Kernel context.
 */
#define	FETCH_XCHOFFLD_STAT	0x2
int
qla_get_exchoffld_status(scsi_qla_host_t *vha, uint16_t *buf_sz,
	uint16_t *ex_logins_cnt)
{
	int rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1019,
	    "Entered %s\n", __func__);

	memset(mcp->mb, 0 , sizeof(mcp->mb));
	mcp->mb[0] = MBC_GET_MEM_OFFLOAD_CNTRL_STAT;
	mcp->mb[1] = FETCH_XCHOFFLD_STAT;
	mcp->out_mb = MBX_1|MBX_0;
	mcp->in_mb = MBX_10|MBX_4|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;

	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1155, "Failed=%x.\n", rval);
	} else {
		*buf_sz = mcp->mb[4];
		*ex_logins_cnt = mcp->mb[10];

		ql_log(ql_log_info, vha, 0x118e,
		    "buffer size 0x%x, exchange offload count=%d\n",
		    mcp->mb[4], mcp->mb[10]);

		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1156,
		    "Done %s.\n", __func__);
	}

	return rval;
}

/*
 * qla_set_exchoffld_mem_cfg
 *	Set exchange offload memory configuration
 *	Mbx needs to be issues before init_cb is set
 *
 * Input:
 *	ha:		adapter state pointer.
 *	buffer:		buffer pointer
 *	phys_addr:	physical address of buffer
 *	size:		size of buffer
 *	TARGET_QUEUE_LOCK must be released
 *	ADAPTER_STATE_LOCK must be release
 *
 * Returns:
 *	qla2x00 local funxtion status code.
 *
 * Context:
 *	Kernel context.
 */
#define CONFIG_XCHOFFLD_MEM	0x3
int
qla_set_exchoffld_mem_cfg(scsi_qla_host_t *vha)
{
	int		rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1157,
	    "Entered %s.\n", __func__);

	memset(mcp->mb, 0 , sizeof(mcp->mb));
	mcp->mb[0] = MBC_GET_MEM_OFFLOAD_CNTRL_STAT;
	mcp->mb[1] = CONFIG_XCHOFFLD_MEM;
	mcp->mb[2] = MSW(ha->exchoffld_buf_dma);
	mcp->mb[3] = LSW(ha->exchoffld_buf_dma);
	mcp->mb[6] = MSW(MSD(ha->exchoffld_buf_dma));
	mcp->mb[7] = LSW(MSD(ha->exchoffld_buf_dma));
	mcp->mb[8] = MSW(ha->exchoffld_size);
	mcp->mb[9] = LSW(ha->exchoffld_size);
	mcp->out_mb = MBX_9|MBX_8|MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_11|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx, vha, 0x1158, "Failed=%x.\n", rval);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1192,
		    "Done %s.\n", __func__);
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
int
qla2x00_get_fw_version(scsi_qla_host_t *vha)
{
	int		rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1029,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_GET_FIRMWARE_VERSION;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	if (IS_QLA81XX(vha->hw) || IS_QLA8031(ha) || IS_QLA8044(ha))
		mcp->in_mb |= MBX_13|MBX_12|MBX_11|MBX_10|MBX_9|MBX_8;
	if (IS_FWI2_CAPABLE(ha))
		mcp->in_mb |= MBX_17|MBX_16|MBX_15;
	if (IS_QLA27XX(ha))
		mcp->in_mb |=
		    MBX_25|MBX_24|MBX_23|MBX_22|MBX_21|MBX_20|MBX_19|MBX_18|
		    MBX_14|MBX_13|MBX_11|MBX_10|MBX_9|MBX_8;

	mcp->flags = 0;
	mcp->tov = MBX_TOV_SECONDS;
	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS)
		goto failed;

	/* Return mailbox data. */
	ha->fw_major_version = mcp->mb[1];
	ha->fw_minor_version = mcp->mb[2];
	ha->fw_subminor_version = mcp->mb[3];
	ha->fw_attributes = mcp->mb[6];
	if (IS_QLA2100(vha->hw) || IS_QLA2200(vha->hw))
		ha->fw_memory_size = 0x1FFFF;		/* Defaults to 128KB. */
	else
		ha->fw_memory_size = (mcp->mb[5] << 16) | mcp->mb[4];

	if (IS_QLA81XX(vha->hw) || IS_QLA8031(vha->hw) || IS_QLA8044(ha)) {
		ha->mpi_version[0] = mcp->mb[10] & 0xff;
		ha->mpi_version[1] = mcp->mb[11] >> 8;
		ha->mpi_version[2] = mcp->mb[11] & 0xff;
		ha->mpi_capabilities = (mcp->mb[12] << 16) | mcp->mb[13];
		ha->phy_version[0] = mcp->mb[8] & 0xff;
		ha->phy_version[1] = mcp->mb[9] >> 8;
		ha->phy_version[2] = mcp->mb[9] & 0xff;
	}

	if (IS_FWI2_CAPABLE(ha)) {
		ha->fw_attributes_h = mcp->mb[15];
		ha->fw_attributes_ext[0] = mcp->mb[16];
		ha->fw_attributes_ext[1] = mcp->mb[17];
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1139,
		    "%s: FW_attributes Upper: 0x%x, Lower: 0x%x.\n",
		    __func__, mcp->mb[15], mcp->mb[6]);
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x112f,
		    "%s: Ext_FwAttributes Upper: 0x%x, Lower: 0x%x.\n",
		    __func__, mcp->mb[17], mcp->mb[16]);

		if (ha->fw_attributes_h & 0x4)
			ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x118d,
			    "%s: Firmware supports Extended Login 0x%x\n",
			    __func__, ha->fw_attributes_h);

		if (ha->fw_attributes_h & 0x8)
			ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1191,
			    "%s: Firmware supports Exchange Offload 0x%x\n",
			    __func__, ha->fw_attributes_h);

		/*
		 * FW supports nvme and driver load parameter requested nvme.
		 * BIT 26 of fw_attributes indicates NVMe support.
		 */
		if ((ha->fw_attributes_h & 0x400) && ql2xnvmeenable) {
			vha->flags.nvme_enabled = 1;
			ql_log(ql_log_info, vha, 0xd302,
			    "%s: FC-NVMe is Enabled (0x%x)\n",
			     __func__, ha->fw_attributes_h);
		}
	}

	if (IS_QLA27XX(ha)) {
		ha->mpi_version[0] = mcp->mb[10] & 0xff;
		ha->mpi_version[1] = mcp->mb[11] >> 8;
		ha->mpi_version[2] = mcp->mb[11] & 0xff;
		ha->pep_version[0] = mcp->mb[13] & 0xff;
		ha->pep_version[1] = mcp->mb[14] >> 8;
		ha->pep_version[2] = mcp->mb[14] & 0xff;
		ha->fw_shared_ram_start = (mcp->mb[19] << 16) | mcp->mb[18];
		ha->fw_shared_ram_end = (mcp->mb[21] << 16) | mcp->mb[20];
		ha->fw_ddr_ram_start = (mcp->mb[23] << 16) | mcp->mb[22];
		ha->fw_ddr_ram_end = (mcp->mb[25] << 16) | mcp->mb[24];
	}

failed:
	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx, vha, 0x102a, "Failed=%x.\n", rval);
	} else {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x102b,
		    "Done %s.\n", __func__);
	}
	return rval;
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
qla2x00_get_fw_options(scsi_qla_host_t *vha, uint16_t *fwopts)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x102c,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_GET_FIRMWARE_OPTION;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx, vha, 0x102d, "Failed=%x.\n", rval);
	} else {
		fwopts[0] = mcp->mb[0];
		fwopts[1] = mcp->mb[1];
		fwopts[2] = mcp->mb[2];
		fwopts[3] = mcp->mb[3];

		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x102e,
		    "Done %s.\n", __func__);
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
qla2x00_set_fw_options(scsi_qla_host_t *vha, uint16_t *fwopts)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x102f,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_SET_FIRMWARE_OPTION;
	mcp->mb[1] = fwopts[1];
	mcp->mb[2] = fwopts[2];
	mcp->mb[3] = fwopts[3];
	mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	if (IS_FWI2_CAPABLE(vha->hw)) {
		mcp->in_mb |= MBX_1;
		mcp->mb[10] = fwopts[10];
		mcp->out_mb |= MBX_10;
	} else {
		mcp->mb[10] = fwopts[10];
		mcp->mb[11] = fwopts[11];
		mcp->mb[12] = 0;	/* Undocumented, but used */
		mcp->out_mb |= MBX_12|MBX_11|MBX_10;
	}
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	fwopts[0] = mcp->mb[0];

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx, vha, 0x1030,
		    "Failed=%x (%x/%x).\n", rval, mcp->mb[0], mcp->mb[1]);
	} else {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1031,
		    "Done %s.\n", __func__);
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
qla2x00_mbx_reg_test(scsi_qla_host_t *vha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1032,
	    "Entered %s.\n", __func__);

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
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

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
		ql_dbg(ql_dbg_mbx, vha, 0x1033, "Failed=%x.\n", rval);
	} else {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1034,
		    "Done %s.\n", __func__);
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
qla2x00_verify_checksum(scsi_qla_host_t *vha, uint32_t risc_addr)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1035,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_VERIFY_CHECKSUM;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_0;
	if (IS_FWI2_CAPABLE(vha->hw)) {
		mcp->mb[1] = MSW(risc_addr);
		mcp->mb[2] = LSW(risc_addr);
		mcp->out_mb |= MBX_2|MBX_1;
		mcp->in_mb |= MBX_2|MBX_1;
	} else {
		mcp->mb[1] = LSW(risc_addr);
		mcp->out_mb |= MBX_1;
		mcp->in_mb |= MBX_1;
	}

	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1036,
		    "Failed=%x chm sum=%x.\n", rval, IS_FWI2_CAPABLE(vha->hw) ?
		    (mcp->mb[2] << 16) | mcp->mb[1] : mcp->mb[1]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1037,
		    "Done %s.\n", __func__);
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
qla2x00_issue_iocb_timeout(scsi_qla_host_t *vha, void *buffer,
    dma_addr_t phys_addr, size_t size, uint32_t tov)
{
	int		rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1038,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_IOCB_COMMAND_A64;
	mcp->mb[1] = 0;
	mcp->mb[2] = MSW(phys_addr);
	mcp->mb[3] = LSW(phys_addr);
	mcp->mb[6] = MSW(MSD(phys_addr));
	mcp->mb[7] = LSW(MSD(phys_addr));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_2|MBX_0;
	mcp->tov = tov;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx, vha, 0x1039, "Failed=%x.\n", rval);
	} else {
		sts_entry_t *sts_entry = (sts_entry_t *) buffer;

		/* Mask reserved bits. */
		sts_entry->entry_status &=
		    IS_FWI2_CAPABLE(vha->hw) ? RF_MASK_24XX : RF_MASK;
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x103a,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla2x00_issue_iocb(scsi_qla_host_t *vha, void *buffer, dma_addr_t phys_addr,
    size_t size)
{
	return qla2x00_issue_iocb_timeout(vha, buffer, phys_addr, size,
	    MBX_TOV_SECONDS);
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
qla2x00_abort_command(srb_t *sp)
{
	unsigned long   flags = 0;
	int		rval;
	uint32_t	handle = 0;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;
	fc_port_t	*fcport = sp->fcport;
	scsi_qla_host_t *vha = fcport->vha;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req;
	struct scsi_cmnd *cmd = GET_CMD_SP(sp);

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x103b,
	    "Entered %s.\n", __func__);

	if (vha->flags.qpairs_available && sp->qpair)
		req = sp->qpair->req;
	else
		req = vha->req;

	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (handle = 1; handle < req->num_outstanding_cmds; handle++) {
		if (req->outstanding_cmds[handle] == sp)
			break;
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	if (handle == req->num_outstanding_cmds) {
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
	mcp->mb[6] = (uint16_t)cmd->device->lun;
	mcp->out_mb = MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x103c, "Failed=%x.\n", rval);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x103d,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla2x00_abort_target(struct fc_port *fcport, uint64_t l, int tag)
{
	int rval, rval2;
	mbx_cmd_t  mc;
	mbx_cmd_t  *mcp = &mc;
	scsi_qla_host_t *vha;
	struct req_que *req;
	struct rsp_que *rsp;

	l = l;
	vha = fcport->vha;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x103e,
	    "Entered %s.\n", __func__);

	req = vha->hw->req_q_map[0];
	rsp = req->rsp;
	mcp->mb[0] = MBC_ABORT_TARGET;
	mcp->out_mb = MBX_9|MBX_2|MBX_1|MBX_0;
	if (HAS_EXTENDED_IDS(vha->hw)) {
		mcp->mb[1] = fcport->loop_id;
		mcp->mb[10] = 0;
		mcp->out_mb |= MBX_10;
	} else {
		mcp->mb[1] = fcport->loop_id << 8;
	}
	mcp->mb[2] = vha->hw->loop_reset_delay;
	mcp->mb[9] = vha->vp_idx;

	mcp->in_mb = MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x103f,
		    "Failed=%x.\n", rval);
	}

	/* Issue marker IOCB. */
	rval2 = qla2x00_marker(vha, req, rsp, fcport->loop_id, 0,
							MK_SYNC_ID);
	if (rval2 != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1040,
		    "Failed to issue marker IOCB (%x).\n", rval2);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1041,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla2x00_lun_reset(struct fc_port *fcport, uint64_t l, int tag)
{
	int rval, rval2;
	mbx_cmd_t  mc;
	mbx_cmd_t  *mcp = &mc;
	scsi_qla_host_t *vha;
	struct req_que *req;
	struct rsp_que *rsp;

	vha = fcport->vha;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1042,
	    "Entered %s.\n", __func__);

	req = vha->hw->req_q_map[0];
	rsp = req->rsp;
	mcp->mb[0] = MBC_LUN_RESET;
	mcp->out_mb = MBX_9|MBX_3|MBX_2|MBX_1|MBX_0;
	if (HAS_EXTENDED_IDS(vha->hw))
		mcp->mb[1] = fcport->loop_id;
	else
		mcp->mb[1] = fcport->loop_id << 8;
	mcp->mb[2] = (u32)l;
	mcp->mb[3] = 0;
	mcp->mb[9] = vha->vp_idx;

	mcp->in_mb = MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1043, "Failed=%x.\n", rval);
	}

	/* Issue marker IOCB. */
	rval2 = qla2x00_marker(vha, req, rsp, fcport->loop_id, l,
								MK_SYNC_ID_LUN);
	if (rval2 != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1044,
		    "Failed to issue marker IOCB (%x).\n", rval2);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1045,
		    "Done %s.\n", __func__);
	}

	return rval;
}

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
qla2x00_get_adapter_id(scsi_qla_host_t *vha, uint16_t *id, uint8_t *al_pa,
    uint8_t *area, uint8_t *domain, uint16_t *top, uint16_t *sw_cap)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1046,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_GET_ADAPTER_LOOP_ID;
	mcp->mb[9] = vha->vp_idx;
	mcp->out_mb = MBX_9|MBX_0;
	mcp->in_mb = MBX_9|MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	if (IS_CNA_CAPABLE(vha->hw))
		mcp->in_mb |= MBX_13|MBX_12|MBX_11|MBX_10;
	if (IS_FWI2_CAPABLE(vha->hw))
		mcp->in_mb |= MBX_19|MBX_18|MBX_17|MBX_16;
	if (IS_QLA27XX(vha->hw))
		mcp->in_mb |= MBX_15;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);
	if (mcp->mb[0] == MBS_COMMAND_ERROR)
		rval = QLA_COMMAND_ERROR;
	else if (mcp->mb[0] == MBS_INVALID_COMMAND)
		rval = QLA_INVALID_COMMAND;

	/* Return data. */
	*id = mcp->mb[1];
	*al_pa = LSB(mcp->mb[2]);
	*area = MSB(mcp->mb[2]);
	*domain	= LSB(mcp->mb[3]);
	*top = mcp->mb[6];
	*sw_cap = mcp->mb[7];

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx, vha, 0x1047, "Failed=%x.\n", rval);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1048,
		    "Done %s.\n", __func__);

		if (IS_CNA_CAPABLE(vha->hw)) {
			vha->fcoe_vlan_id = mcp->mb[9] & 0xfff;
			vha->fcoe_fcf_idx = mcp->mb[10];
			vha->fcoe_vn_port_mac[5] = mcp->mb[11] >> 8;
			vha->fcoe_vn_port_mac[4] = mcp->mb[11] & 0xff;
			vha->fcoe_vn_port_mac[3] = mcp->mb[12] >> 8;
			vha->fcoe_vn_port_mac[2] = mcp->mb[12] & 0xff;
			vha->fcoe_vn_port_mac[1] = mcp->mb[13] >> 8;
			vha->fcoe_vn_port_mac[0] = mcp->mb[13] & 0xff;
		}
		/* If FA-WWN supported */
		if (IS_FAWWN_CAPABLE(vha->hw)) {
			if (mcp->mb[7] & BIT_14) {
				vha->port_name[0] = MSB(mcp->mb[16]);
				vha->port_name[1] = LSB(mcp->mb[16]);
				vha->port_name[2] = MSB(mcp->mb[17]);
				vha->port_name[3] = LSB(mcp->mb[17]);
				vha->port_name[4] = MSB(mcp->mb[18]);
				vha->port_name[5] = LSB(mcp->mb[18]);
				vha->port_name[6] = MSB(mcp->mb[19]);
				vha->port_name[7] = LSB(mcp->mb[19]);
				fc_host_port_name(vha->host) =
				    wwn_to_u64(vha->port_name);
				ql_dbg(ql_dbg_mbx, vha, 0x10ca,
				    "FA-WWN acquired %016llx\n",
				    wwn_to_u64(vha->port_name));
			}
		}

		if (IS_QLA27XX(vha->hw))
			vha->bbcr = mcp->mb[15];
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
qla2x00_get_retry_cnt(scsi_qla_host_t *vha, uint8_t *retry_cnt, uint8_t *tov,
    uint16_t *r_a_tov)
{
	int rval;
	uint16_t ratov;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1049,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_GET_RETRY_COUNT;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx, vha, 0x104a,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		/* Convert returned data and check our values. */
		*r_a_tov = mcp->mb[3] / 2;
		ratov = (mcp->mb[3]/2) / 10;  /* mb[3] value is in 100ms */
		if (mcp->mb[1] * ratov > (*retry_cnt) * (*tov)) {
			/* Update to the larger values */
			*retry_cnt = (uint8_t)mcp->mb[1];
			*tov = ratov;
		}

		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x104b,
		    "Done %s mb3=%d ratov=%d.\n", __func__, mcp->mb[3], ratov);
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
qla2x00_init_firmware(scsi_qla_host_t *vha, uint16_t size)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x104c,
	    "Entered %s.\n", __func__);

	if (IS_P3P_TYPE(ha) && ql2xdbwr)
		qla82xx_wr_32(ha, (uintptr_t __force)ha->nxdb_wr_ptr,
			(0x04 | (ha->portnum << 5) | (0 << 8) | (0 << 16)));

	if (ha->flags.npiv_supported)
		mcp->mb[0] = MBC_MID_INITIALIZE_FIRMWARE;
	else
		mcp->mb[0] = MBC_INITIALIZE_FIRMWARE;

	mcp->mb[1] = 0;
	mcp->mb[2] = MSW(ha->init_cb_dma);
	mcp->mb[3] = LSW(ha->init_cb_dma);
	mcp->mb[6] = MSW(MSD(ha->init_cb_dma));
	mcp->mb[7] = LSW(MSD(ha->init_cb_dma));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	if (ha->ex_init_cb && ha->ex_init_cb->ex_version) {
		mcp->mb[1] = BIT_0;
		mcp->mb[10] = MSW(ha->ex_init_cb_dma);
		mcp->mb[11] = LSW(ha->ex_init_cb_dma);
		mcp->mb[12] = MSW(MSD(ha->ex_init_cb_dma));
		mcp->mb[13] = LSW(MSD(ha->ex_init_cb_dma));
		mcp->mb[14] = sizeof(*ha->ex_init_cb);
		mcp->out_mb |= MBX_14|MBX_13|MBX_12|MBX_11|MBX_10;
	}
	/* 1 and 2 should normally be captured. */
	mcp->in_mb = MBX_2|MBX_1|MBX_0;
	if (IS_QLA83XX(ha) || IS_QLA27XX(ha))
		/* mb3 is additional info about the installed SFP. */
		mcp->in_mb  |= MBX_3;
	mcp->buf_size = size;
	mcp->flags = MBX_DMA_OUT;
	mcp->tov = MBX_TOV_SECONDS;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx, vha, 0x104d,
		    "Failed=%x mb[0]=%x, mb[1]=%x, mb[2]=%x, mb[3]=%x,.\n",
		    rval, mcp->mb[0], mcp->mb[1], mcp->mb[2], mcp->mb[3]);
	} else {
		if (IS_QLA27XX(ha)) {
			if (mcp->mb[2] == 6 || mcp->mb[3] == 2)
				ql_dbg(ql_dbg_mbx, vha, 0x119d,
				    "Invalid SFP/Validation Failed\n");
		}
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x104e,
		    "Done %s.\n", __func__);
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
qla2x00_get_port_database(scsi_qla_host_t *vha, fc_port_t *fcport, uint8_t opt)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	port_database_t *pd;
	struct port_database_24xx *pd24;
	dma_addr_t pd_dma;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x104f,
	    "Entered %s.\n", __func__);

	pd24 = NULL;
	pd = dma_pool_zalloc(ha->s_dma_pool, GFP_KERNEL, &pd_dma);
	if (pd  == NULL) {
		ql_log(ql_log_warn, vha, 0x1050,
		    "Failed to allocate port database structure.\n");
		fcport->query = 0;
		return QLA_MEMORY_ALLOC_FAILED;
	}

	mcp->mb[0] = MBC_GET_PORT_DATABASE;
	if (opt != 0 && !IS_FWI2_CAPABLE(ha))
		mcp->mb[0] = MBC_ENHANCED_GET_PORT_DATABASE;
	mcp->mb[2] = MSW(pd_dma);
	mcp->mb[3] = LSW(pd_dma);
	mcp->mb[6] = MSW(MSD(pd_dma));
	mcp->mb[7] = LSW(MSD(pd_dma));
	mcp->mb[9] = vha->vp_idx;
	mcp->out_mb = MBX_9|MBX_7|MBX_6|MBX_3|MBX_2|MBX_0;
	mcp->in_mb = MBX_0;
	if (IS_FWI2_CAPABLE(ha)) {
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
	mcp->buf_size = IS_FWI2_CAPABLE(ha) ?
	    PORT_DATABASE_24XX_SIZE : PORT_DATABASE_SIZE;
	mcp->flags = MBX_DMA_IN;
	mcp->tov = (ha->login_timeout * 2) + (ha->login_timeout / 2);
	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS)
		goto gpd_error_out;

	if (IS_FWI2_CAPABLE(ha)) {
		uint64_t zero = 0;
		u8 current_login_state, last_login_state;

		pd24 = (struct port_database_24xx *) pd;

		/* Check for logged in state. */
		if (fcport->fc4f_nvme) {
			current_login_state = pd24->current_login_state >> 4;
			last_login_state = pd24->last_login_state >> 4;
		} else {
			current_login_state = pd24->current_login_state & 0xf;
			last_login_state = pd24->last_login_state & 0xf;
		}
		fcport->current_login_state = pd24->current_login_state;
		fcport->last_login_state = pd24->last_login_state;

		/* Check for logged in state. */
		if (current_login_state != PDS_PRLI_COMPLETE &&
		    last_login_state != PDS_PRLI_COMPLETE) {
			ql_dbg(ql_dbg_mbx, vha, 0x119a,
			    "Unable to verify login-state (%x/%x) for loop_id %x.\n",
			    current_login_state, last_login_state,
			    fcport->loop_id);
			rval = QLA_FUNCTION_FAILED;

			if (!fcport->query)
				goto gpd_error_out;
		}

		if (fcport->loop_id == FC_NO_LOOP_ID ||
		    (memcmp(fcport->port_name, (uint8_t *)&zero, 8) &&
		     memcmp(fcport->port_name, pd24->port_name, 8))) {
			/* We lost the device mid way. */
			rval = QLA_NOT_LOGGED_IN;
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

		/* Passback COS information. */
		fcport->supported_classes = (pd24->flags & PDF_CLASS_2) ?
				FC_COS_CLASS2 : FC_COS_CLASS3;

		if (pd24->prli_svc_param_word_3[0] & BIT_7)
			fcport->flags |= FCF_CONF_COMP_SUPPORTED;
	} else {
		uint64_t zero = 0;

		/* Check for logged in state. */
		if (pd->master_state != PD_STATE_PORT_LOGGED_IN &&
		    pd->slave_state != PD_STATE_PORT_LOGGED_IN) {
			ql_dbg(ql_dbg_mbx, vha, 0x100a,
			    "Unable to verify login-state (%x/%x) - "
			    "portid=%02x%02x%02x.\n", pd->master_state,
			    pd->slave_state, fcport->d_id.b.domain,
			    fcport->d_id.b.area, fcport->d_id.b.al_pa);
			rval = QLA_FUNCTION_FAILED;
			goto gpd_error_out;
		}

		if (fcport->loop_id == FC_NO_LOOP_ID ||
		    (memcmp(fcport->port_name, (uint8_t *)&zero, 8) &&
		     memcmp(fcport->port_name, pd->port_name, 8))) {
			/* We lost the device mid way. */
			rval = QLA_NOT_LOGGED_IN;
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
	fcport->query = 0;

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1052,
		    "Failed=%x mb[0]=%x mb[1]=%x.\n", rval,
		    mcp->mb[0], mcp->mb[1]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1053,
		    "Done %s.\n", __func__);
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
qla2x00_get_firmware_state(scsi_qla_host_t *vha, uint16_t *states)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1054,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_GET_FIRMWARE_STATE;
	mcp->out_mb = MBX_0;
	if (IS_FWI2_CAPABLE(vha->hw))
		mcp->in_mb = MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	else
		mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	/* Return firmware states. */
	states[0] = mcp->mb[1];
	if (IS_FWI2_CAPABLE(vha->hw)) {
		states[1] = mcp->mb[2];
		states[2] = mcp->mb[3];  /* SFP info */
		states[3] = mcp->mb[4];
		states[4] = mcp->mb[5];
		states[5] = mcp->mb[6];  /* DPORT status */
	}

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx, vha, 0x1055, "Failed=%x.\n", rval);
	} else {
		if (IS_QLA27XX(ha)) {
			if (mcp->mb[2] == 6 || mcp->mb[3] == 2)
				ql_dbg(ql_dbg_mbx, vha, 0x119e,
				    "Invalid SFP/Validation Failed\n");
		}
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1056,
		    "Done %s.\n", __func__);
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
qla2x00_get_port_name(scsi_qla_host_t *vha, uint16_t loop_id, uint8_t *name,
    uint8_t opt)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1057,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_GET_PORT_NAME;
	mcp->mb[9] = vha->vp_idx;
	mcp->out_mb = MBX_9|MBX_1|MBX_0;
	if (HAS_EXTENDED_IDS(vha->hw)) {
		mcp->mb[1] = loop_id;
		mcp->mb[10] = opt;
		mcp->out_mb |= MBX_10;
	} else {
		mcp->mb[1] = loop_id << 8 | opt;
	}

	mcp->in_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx, vha, 0x1058, "Failed=%x.\n", rval);
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

		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1059,
		    "Done %s.\n", __func__);
	}

	return rval;
}

/*
 * qla24xx_link_initialization
 *	Issue link initialization mailbox command.
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
qla24xx_link_initialize(scsi_qla_host_t *vha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1152,
	    "Entered %s.\n", __func__);

	if (!IS_FWI2_CAPABLE(vha->hw) || IS_CNA_CAPABLE(vha->hw))
		return QLA_FUNCTION_FAILED;

	mcp->mb[0] = MBC_LINK_INITIALIZATION;
	mcp->mb[1] = BIT_4;
	if (vha->hw->operating_mode == LOOP)
		mcp->mb[1] |= BIT_6;
	else
		mcp->mb[1] |= BIT_5;
	mcp->mb[2] = 0;
	mcp->mb[3] = 0;
	mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1153, "Failed=%x.\n", rval);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1154,
		    "Done %s.\n", __func__);
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
qla2x00_lip_reset(scsi_qla_host_t *vha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x105a,
	    "Entered %s.\n", __func__);

	if (IS_CNA_CAPABLE(vha->hw)) {
		/* Logout across all FCFs. */
		mcp->mb[0] = MBC_LIP_FULL_LOGIN;
		mcp->mb[1] = BIT_1;
		mcp->mb[2] = 0;
		mcp->out_mb = MBX_2|MBX_1|MBX_0;
	} else if (IS_FWI2_CAPABLE(vha->hw)) {
		mcp->mb[0] = MBC_LIP_FULL_LOGIN;
		if (N2N_TOPO(vha->hw))
			mcp->mb[1] = BIT_4; /* re-init */
		else
			mcp->mb[1] = BIT_6; /* LIP */
		mcp->mb[2] = 0;
		mcp->mb[3] = vha->hw->loop_reset_delay;
		mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	} else {
		mcp->mb[0] = MBC_LIP_RESET;
		mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
		if (HAS_EXTENDED_IDS(vha->hw)) {
			mcp->mb[1] = 0x00ff;
			mcp->mb[10] = 0;
			mcp->out_mb |= MBX_10;
		} else {
			mcp->mb[1] = 0xff00;
		}
		mcp->mb[2] = vha->hw->loop_reset_delay;
		mcp->mb[3] = 0;
	}
	mcp->in_mb = MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx, vha, 0x105b, "Failed=%x.\n", rval);
	} else {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x105c,
		    "Done %s.\n", __func__);
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
qla2x00_send_sns(scsi_qla_host_t *vha, dma_addr_t sns_phys_address,
    uint16_t cmd_size, size_t buf_size)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x105d,
	    "Entered %s.\n", __func__);

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x105e,
	    "Retry cnt=%d ratov=%d total tov=%d.\n",
	    vha->hw->retry_count, vha->hw->login_timeout, mcp->tov);

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
	mcp->tov = (vha->hw->login_timeout * 2) + (vha->hw->login_timeout / 2);
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx, vha, 0x105f,
		    "Failed=%x mb[0]=%x mb[1]=%x.\n",
		    rval, mcp->mb[0], mcp->mb[1]);
	} else {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1060,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla24xx_login_fabric(scsi_qla_host_t *vha, uint16_t loop_id, uint8_t domain,
    uint8_t area, uint8_t al_pa, uint16_t *mb, uint8_t opt)
{
	int		rval;

	struct logio_entry_24xx *lg;
	dma_addr_t	lg_dma;
	uint32_t	iop[2];
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1061,
	    "Entered %s.\n", __func__);

	if (vha->vp_idx && vha->qpair)
		req = vha->qpair->req;
	else
		req = ha->req_q_map[0];

	lg = dma_pool_zalloc(ha->s_dma_pool, GFP_KERNEL, &lg_dma);
	if (lg == NULL) {
		ql_log(ql_log_warn, vha, 0x1062,
		    "Failed to allocate login IOCB.\n");
		return QLA_MEMORY_ALLOC_FAILED;
	}

	lg->entry_type = LOGINOUT_PORT_IOCB_TYPE;
	lg->entry_count = 1;
	lg->handle = MAKE_HANDLE(req->id, lg->handle);
	lg->nport_handle = cpu_to_le16(loop_id);
	lg->control_flags = cpu_to_le16(LCF_COMMAND_PLOGI);
	if (opt & BIT_0)
		lg->control_flags |= cpu_to_le16(LCF_COND_PLOGI);
	if (opt & BIT_1)
		lg->control_flags |= cpu_to_le16(LCF_SKIP_PRLI);
	lg->port_id[0] = al_pa;
	lg->port_id[1] = area;
	lg->port_id[2] = domain;
	lg->vp_index = vha->vp_idx;
	rval = qla2x00_issue_iocb_timeout(vha, lg, lg_dma, 0,
	    (ha->r_a_tov / 10 * 2) + 2);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1063,
		    "Failed to issue login IOCB (%x).\n", rval);
	} else if (lg->entry_status != 0) {
		ql_dbg(ql_dbg_mbx, vha, 0x1064,
		    "Failed to complete IOCB -- error status (%x).\n",
		    lg->entry_status);
		rval = QLA_FUNCTION_FAILED;
	} else if (lg->comp_status != cpu_to_le16(CS_COMPLETE)) {
		iop[0] = le32_to_cpu(lg->io_parameter[0]);
		iop[1] = le32_to_cpu(lg->io_parameter[1]);

		ql_dbg(ql_dbg_mbx, vha, 0x1065,
		    "Failed to complete IOCB -- completion  status (%x) "
		    "ioparam=%x/%x.\n", le16_to_cpu(lg->comp_status),
		    iop[0], iop[1]);

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
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1066,
		    "Done %s.\n", __func__);

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
		if (lg->io_parameter[0] & cpu_to_le32(BIT_7))
			mb[10] |= BIT_7;	/* Confirmed Completion
						 * Allowed
						 */
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
qla2x00_login_fabric(scsi_qla_host_t *vha, uint16_t loop_id, uint8_t domain,
    uint8_t area, uint8_t al_pa, uint16_t *mb, uint8_t opt)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1067,
	    "Entered %s.\n", __func__);

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
	rval = qla2x00_mailbox_command(vha, mcp);

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
		ql_dbg(ql_dbg_mbx, vha, 0x1068,
		    "Failed=%x mb[0]=%x mb[1]=%x mb[2]=%x.\n",
		    rval, mcp->mb[0], mcp->mb[1], mcp->mb[2]);
	} else {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1069,
		    "Done %s.\n", __func__);
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
qla2x00_login_local_device(scsi_qla_host_t *vha, fc_port_t *fcport,
    uint16_t *mb_ret, uint8_t opt)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x106a,
	    "Entered %s.\n", __func__);

	if (IS_FWI2_CAPABLE(ha))
		return qla24xx_login_fabric(vha, fcport->loop_id,
		    fcport->d_id.b.domain, fcport->d_id.b.area,
		    fcport->d_id.b.al_pa, mb_ret, opt);

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
	rval = qla2x00_mailbox_command(vha, mcp);

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

		ql_dbg(ql_dbg_mbx, vha, 0x106b,
		    "Failed=%x mb[0]=%x mb[1]=%x mb[6]=%x mb[7]=%x.\n",
		    rval, mcp->mb[0], mcp->mb[1], mcp->mb[6], mcp->mb[7]);
	} else {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x106c,
		    "Done %s.\n", __func__);
	}

	return (rval);
}

int
qla24xx_fabric_logout(scsi_qla_host_t *vha, uint16_t loop_id, uint8_t domain,
    uint8_t area, uint8_t al_pa)
{
	int		rval;
	struct logio_entry_24xx *lg;
	dma_addr_t	lg_dma;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x106d,
	    "Entered %s.\n", __func__);

	lg = dma_pool_zalloc(ha->s_dma_pool, GFP_KERNEL, &lg_dma);
	if (lg == NULL) {
		ql_log(ql_log_warn, vha, 0x106e,
		    "Failed to allocate logout IOCB.\n");
		return QLA_MEMORY_ALLOC_FAILED;
	}

	req = vha->req;
	lg->entry_type = LOGINOUT_PORT_IOCB_TYPE;
	lg->entry_count = 1;
	lg->handle = MAKE_HANDLE(req->id, lg->handle);
	lg->nport_handle = cpu_to_le16(loop_id);
	lg->control_flags =
	    cpu_to_le16(LCF_COMMAND_LOGO|LCF_IMPL_LOGO|
		LCF_FREE_NPORT);
	lg->port_id[0] = al_pa;
	lg->port_id[1] = area;
	lg->port_id[2] = domain;
	lg->vp_index = vha->vp_idx;
	rval = qla2x00_issue_iocb_timeout(vha, lg, lg_dma, 0,
	    (ha->r_a_tov / 10 * 2) + 2);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x106f,
		    "Failed to issue logout IOCB (%x).\n", rval);
	} else if (lg->entry_status != 0) {
		ql_dbg(ql_dbg_mbx, vha, 0x1070,
		    "Failed to complete IOCB -- error status (%x).\n",
		    lg->entry_status);
		rval = QLA_FUNCTION_FAILED;
	} else if (lg->comp_status != cpu_to_le16(CS_COMPLETE)) {
		ql_dbg(ql_dbg_mbx, vha, 0x1071,
		    "Failed to complete IOCB -- completion status (%x) "
		    "ioparam=%x/%x.\n", le16_to_cpu(lg->comp_status),
		    le32_to_cpu(lg->io_parameter[0]),
		    le32_to_cpu(lg->io_parameter[1]));
	} else {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1072,
		    "Done %s.\n", __func__);
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
qla2x00_fabric_logout(scsi_qla_host_t *vha, uint16_t loop_id, uint8_t domain,
    uint8_t area, uint8_t al_pa)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1073,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_LOGOUT_FABRIC_PORT;
	mcp->out_mb = MBX_1|MBX_0;
	if (HAS_EXTENDED_IDS(vha->hw)) {
		mcp->mb[1] = loop_id;
		mcp->mb[10] = 0;
		mcp->out_mb |= MBX_10;
	} else {
		mcp->mb[1] = loop_id << 8;
	}

	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx, vha, 0x1074,
		    "Failed=%x mb[1]=%x.\n", rval, mcp->mb[1]);
	} else {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1075,
		    "Done %s.\n", __func__);
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
qla2x00_full_login_lip(scsi_qla_host_t *vha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1076,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_LIP_FULL_LOGIN;
	mcp->mb[1] = IS_FWI2_CAPABLE(vha->hw) ? BIT_3 : 0;
	mcp->mb[2] = 0;
	mcp->mb[3] = 0;
	mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx, vha, 0x1077, "Failed=%x.\n", rval);
	} else {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1078,
		    "Done %s.\n", __func__);
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
qla2x00_get_id_list(scsi_qla_host_t *vha, void *id_list, dma_addr_t id_list_dma,
    uint16_t *entries)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1079,
	    "Entered %s.\n", __func__);

	if (id_list == NULL)
		return QLA_FUNCTION_FAILED;

	mcp->mb[0] = MBC_GET_ID_LIST;
	mcp->out_mb = MBX_0;
	if (IS_FWI2_CAPABLE(vha->hw)) {
		mcp->mb[2] = MSW(id_list_dma);
		mcp->mb[3] = LSW(id_list_dma);
		mcp->mb[6] = MSW(MSD(id_list_dma));
		mcp->mb[7] = LSW(MSD(id_list_dma));
		mcp->mb[8] = 0;
		mcp->mb[9] = vha->vp_idx;
		mcp->out_mb |= MBX_9|MBX_8|MBX_7|MBX_6|MBX_3|MBX_2;
	} else {
		mcp->mb[1] = MSW(id_list_dma);
		mcp->mb[2] = LSW(id_list_dma);
		mcp->mb[3] = MSW(MSD(id_list_dma));
		mcp->mb[6] = LSW(MSD(id_list_dma));
		mcp->out_mb |= MBX_6|MBX_3|MBX_2|MBX_1;
	}
	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx, vha, 0x107a, "Failed=%x.\n", rval);
	} else {
		*entries = mcp->mb[1];
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x107b,
		    "Done %s.\n", __func__);
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
qla2x00_get_resource_cnts(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x107c,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_GET_RESOURCE_COUNTS;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_11|MBX_10|MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	if (IS_QLA81XX(vha->hw) || IS_QLA83XX(vha->hw) || IS_QLA27XX(vha->hw))
		mcp->in_mb |= MBX_12;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx, vha, 0x107d,
		    "Failed mb[0]=%x.\n", mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x107e,
		    "Done %s mb1=%x mb2=%x mb3=%x mb6=%x mb7=%x mb10=%x "
		    "mb11=%x mb12=%x.\n", __func__, mcp->mb[1], mcp->mb[2],
		    mcp->mb[3], mcp->mb[6], mcp->mb[7], mcp->mb[10],
		    mcp->mb[11], mcp->mb[12]);

		ha->orig_fw_tgt_xcb_count =  mcp->mb[1];
		ha->cur_fw_tgt_xcb_count = mcp->mb[2];
		ha->cur_fw_xcb_count = mcp->mb[3];
		ha->orig_fw_xcb_count = mcp->mb[6];
		ha->cur_fw_iocb_count = mcp->mb[7];
		ha->orig_fw_iocb_count = mcp->mb[10];
		if (ha->flags.npiv_supported)
			ha->max_npiv_vports = mcp->mb[11];
		if (IS_QLA81XX(ha) || IS_QLA83XX(ha) || IS_QLA27XX(ha))
			ha->fw_max_fcf_count = mcp->mb[12];
	}

	return (rval);
}

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
qla2x00_get_fcal_position_map(scsi_qla_host_t *vha, char *pos_map)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	char *pmap;
	dma_addr_t pmap_dma;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x107f,
	    "Entered %s.\n", __func__);

	pmap = dma_pool_zalloc(ha->s_dma_pool, GFP_KERNEL, &pmap_dma);
	if (pmap  == NULL) {
		ql_log(ql_log_warn, vha, 0x1080,
		    "Memory alloc failed.\n");
		return QLA_MEMORY_ALLOC_FAILED;
	}

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
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval == QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx + ql_dbg_buffer, vha, 0x1081,
		    "mb0/mb1=%x/%X FC/AL position map size (%x).\n",
		    mcp->mb[0], mcp->mb[1], (unsigned)pmap[0]);
		ql_dump_buffer(ql_dbg_mbx + ql_dbg_buffer, vha, 0x111d,
		    pmap, pmap[0] + 1);

		if (pos_map)
			memcpy(pos_map, pmap, FCAL_MAP_SIZE);
	}
	dma_pool_free(ha->s_dma_pool, pmap, pmap_dma);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1082, "Failed=%x.\n", rval);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1083,
		    "Done %s.\n", __func__);
	}

	return rval;
}

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
qla2x00_get_link_status(scsi_qla_host_t *vha, uint16_t loop_id,
    struct link_statistics *stats, dma_addr_t stats_dma)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	uint32_t *iter = (void *)stats;
	ushort dwords = offsetof(typeof(*stats), link_up_cnt)/sizeof(*iter);
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1084,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_GET_LINK_STATUS;
	mcp->mb[2] = MSW(LSD(stats_dma));
	mcp->mb[3] = LSW(LSD(stats_dma));
	mcp->mb[6] = MSW(MSD(stats_dma));
	mcp->mb[7] = LSW(MSD(stats_dma));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_0;
	mcp->in_mb = MBX_0;
	if (IS_FWI2_CAPABLE(ha)) {
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
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = IOCTL_CMD;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval == QLA_SUCCESS) {
		if (mcp->mb[0] != MBS_COMMAND_COMPLETE) {
			ql_dbg(ql_dbg_mbx, vha, 0x1085,
			    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
			rval = QLA_FUNCTION_FAILED;
		} else {
			/* Re-endianize - firmware data is le32. */
			ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1086,
			    "Done %s.\n", __func__);
			for ( ; dwords--; iter++)
				le32_to_cpus(iter);
		}
	} else {
		/* Failed. */
		ql_dbg(ql_dbg_mbx, vha, 0x1087, "Failed=%x.\n", rval);
	}

	return rval;
}

int
qla24xx_get_isp_stats(scsi_qla_host_t *vha, struct link_statistics *stats,
    dma_addr_t stats_dma, uint16_t options)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	uint32_t *iter, dwords;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1088,
	    "Entered %s.\n", __func__);

	memset(&mc, 0, sizeof(mc));
	mc.mb[0] = MBC_GET_LINK_PRIV_STATS;
	mc.mb[2] = MSW(stats_dma);
	mc.mb[3] = LSW(stats_dma);
	mc.mb[6] = MSW(MSD(stats_dma));
	mc.mb[7] = LSW(MSD(stats_dma));
	mc.mb[8] = sizeof(struct link_statistics) / 4;
	mc.mb[9] = cpu_to_le16(vha->vp_idx);
	mc.mb[10] = cpu_to_le16(options);

	rval = qla24xx_send_mb_cmd(vha, &mc);

	if (rval == QLA_SUCCESS) {
		if (mcp->mb[0] != MBS_COMMAND_COMPLETE) {
			ql_dbg(ql_dbg_mbx, vha, 0x1089,
			    "Failed mb[0]=%x.\n", mcp->mb[0]);
			rval = QLA_FUNCTION_FAILED;
		} else {
			ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x108a,
			    "Done %s.\n", __func__);
			/* Re-endianize - firmware data is le32. */
			dwords = sizeof(struct link_statistics) / 4;
			iter = &stats->link_fail_cnt;
			for ( ; dwords--; iter++)
				le32_to_cpus(iter);
		}
	} else {
		/* Failed. */
		ql_dbg(ql_dbg_mbx, vha, 0x108b, "Failed=%x.\n", rval);
	}

	return rval;
}

int
qla24xx_abort_command(srb_t *sp)
{
	int		rval;
	unsigned long   flags = 0;

	struct abort_entry_24xx *abt;
	dma_addr_t	abt_dma;
	uint32_t	handle;
	fc_port_t	*fcport = sp->fcport;
	struct scsi_qla_host *vha = fcport->vha;
	struct qla_hw_data *ha = vha->hw;
	struct req_que *req = vha->req;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x108c,
	    "Entered %s.\n", __func__);

	if (vha->flags.qpairs_available && sp->qpair)
		req = sp->qpair->req;

	if (ql2xasynctmfenable)
		return qla24xx_async_abort_command(sp);

	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (handle = 1; handle < req->num_outstanding_cmds; handle++) {
		if (req->outstanding_cmds[handle] == sp)
			break;
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
	if (handle == req->num_outstanding_cmds) {
		/* Command not found. */
		return QLA_FUNCTION_FAILED;
	}

	abt = dma_pool_zalloc(ha->s_dma_pool, GFP_KERNEL, &abt_dma);
	if (abt == NULL) {
		ql_log(ql_log_warn, vha, 0x108d,
		    "Failed to allocate abort IOCB.\n");
		return QLA_MEMORY_ALLOC_FAILED;
	}

	abt->entry_type = ABORT_IOCB_TYPE;
	abt->entry_count = 1;
	abt->handle = MAKE_HANDLE(req->id, abt->handle);
	abt->nport_handle = cpu_to_le16(fcport->loop_id);
	abt->handle_to_abort = MAKE_HANDLE(req->id, handle);
	abt->port_id[0] = fcport->d_id.b.al_pa;
	abt->port_id[1] = fcport->d_id.b.area;
	abt->port_id[2] = fcport->d_id.b.domain;
	abt->vp_index = fcport->vha->vp_idx;

	abt->req_que_no = cpu_to_le16(req->id);

	rval = qla2x00_issue_iocb(vha, abt, abt_dma, 0);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x108e,
		    "Failed to issue IOCB (%x).\n", rval);
	} else if (abt->entry_status != 0) {
		ql_dbg(ql_dbg_mbx, vha, 0x108f,
		    "Failed to complete IOCB -- error status (%x).\n",
		    abt->entry_status);
		rval = QLA_FUNCTION_FAILED;
	} else if (abt->nport_handle != cpu_to_le16(0)) {
		ql_dbg(ql_dbg_mbx, vha, 0x1090,
		    "Failed to complete IOCB -- completion status (%x).\n",
		    le16_to_cpu(abt->nport_handle));
		if (abt->nport_handle == CS_IOCB_ERROR)
			rval = QLA_FUNCTION_PARAMETER_ERROR;
		else
			rval = QLA_FUNCTION_FAILED;
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1091,
		    "Done %s.\n", __func__);
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

static int
__qla24xx_issue_tmf(char *name, uint32_t type, struct fc_port *fcport,
    uint64_t l, int tag)
{
	int		rval, rval2;
	struct tsk_mgmt_cmd *tsk;
	struct sts_entry_24xx *sts;
	dma_addr_t	tsk_dma;
	scsi_qla_host_t *vha;
	struct qla_hw_data *ha;
	struct req_que *req;
	struct rsp_que *rsp;
	struct qla_qpair *qpair;

	vha = fcport->vha;
	ha = vha->hw;
	req = vha->req;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1092,
	    "Entered %s.\n", __func__);

	if (vha->vp_idx && vha->qpair) {
		/* NPIV port */
		qpair = vha->qpair;
		rsp = qpair->rsp;
		req = qpair->req;
	} else {
		rsp = req->rsp;
	}

	tsk = dma_pool_zalloc(ha->s_dma_pool, GFP_KERNEL, &tsk_dma);
	if (tsk == NULL) {
		ql_log(ql_log_warn, vha, 0x1093,
		    "Failed to allocate task management IOCB.\n");
		return QLA_MEMORY_ALLOC_FAILED;
	}

	tsk->p.tsk.entry_type = TSK_MGMT_IOCB_TYPE;
	tsk->p.tsk.entry_count = 1;
	tsk->p.tsk.handle = MAKE_HANDLE(req->id, tsk->p.tsk.handle);
	tsk->p.tsk.nport_handle = cpu_to_le16(fcport->loop_id);
	tsk->p.tsk.timeout = cpu_to_le16(ha->r_a_tov / 10 * 2);
	tsk->p.tsk.control_flags = cpu_to_le32(type);
	tsk->p.tsk.port_id[0] = fcport->d_id.b.al_pa;
	tsk->p.tsk.port_id[1] = fcport->d_id.b.area;
	tsk->p.tsk.port_id[2] = fcport->d_id.b.domain;
	tsk->p.tsk.vp_index = fcport->vha->vp_idx;
	if (type == TCF_LUN_RESET) {
		int_to_scsilun(l, &tsk->p.tsk.lun);
		host_to_fcp_swap((uint8_t *)&tsk->p.tsk.lun,
		    sizeof(tsk->p.tsk.lun));
	}

	sts = &tsk->p.sts;
	rval = qla2x00_issue_iocb(vha, tsk, tsk_dma, 0);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1094,
		    "Failed to issue %s reset IOCB (%x).\n", name, rval);
	} else if (sts->entry_status != 0) {
		ql_dbg(ql_dbg_mbx, vha, 0x1095,
		    "Failed to complete IOCB -- error status (%x).\n",
		    sts->entry_status);
		rval = QLA_FUNCTION_FAILED;
	} else if (sts->comp_status != cpu_to_le16(CS_COMPLETE)) {
		ql_dbg(ql_dbg_mbx, vha, 0x1096,
		    "Failed to complete IOCB -- completion status (%x).\n",
		    le16_to_cpu(sts->comp_status));
		rval = QLA_FUNCTION_FAILED;
	} else if (le16_to_cpu(sts->scsi_status) &
	    SS_RESPONSE_INFO_LEN_VALID) {
		if (le32_to_cpu(sts->rsp_data_len) < 4) {
			ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1097,
			    "Ignoring inconsistent data length -- not enough "
			    "response info (%d).\n",
			    le32_to_cpu(sts->rsp_data_len));
		} else if (sts->data[3]) {
			ql_dbg(ql_dbg_mbx, vha, 0x1098,
			    "Failed to complete IOCB -- response (%x).\n",
			    sts->data[3]);
			rval = QLA_FUNCTION_FAILED;
		}
	}

	/* Issue marker IOCB. */
	rval2 = qla2x00_marker(vha, req, rsp, fcport->loop_id, l,
	    type == TCF_LUN_RESET ? MK_SYNC_ID_LUN: MK_SYNC_ID);
	if (rval2 != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1099,
		    "Failed to issue marker IOCB (%x).\n", rval2);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x109a,
		    "Done %s.\n", __func__);
	}

	dma_pool_free(ha->s_dma_pool, tsk, tsk_dma);

	return rval;
}

int
qla24xx_abort_target(struct fc_port *fcport, uint64_t l, int tag)
{
	struct qla_hw_data *ha = fcport->vha->hw;

	if ((ql2xasynctmfenable) && IS_FWI2_CAPABLE(ha))
		return qla2x00_async_tm_cmd(fcport, TCF_TARGET_RESET, l, tag);

	return __qla24xx_issue_tmf("Target", TCF_TARGET_RESET, fcport, l, tag);
}

int
qla24xx_lun_reset(struct fc_port *fcport, uint64_t l, int tag)
{
	struct qla_hw_data *ha = fcport->vha->hw;

	if ((ql2xasynctmfenable) && IS_FWI2_CAPABLE(ha))
		return qla2x00_async_tm_cmd(fcport, TCF_LUN_RESET, l, tag);

	return __qla24xx_issue_tmf("Lun", TCF_LUN_RESET, fcport, l, tag);
}

int
qla2x00_system_error(scsi_qla_host_t *vha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA23XX(ha) && !IS_FWI2_CAPABLE(ha))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x109b,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_GEN_SYSTEM_ERROR;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 5;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x109c, "Failed=%x.\n", rval);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x109d,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla2x00_write_serdes_word(scsi_qla_host_t *vha, uint16_t addr, uint16_t data)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_QLA25XX(vha->hw) && !IS_QLA2031(vha->hw) &&
	    !IS_QLA27XX(vha->hw))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1182,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_WRITE_SERDES;
	mcp->mb[1] = addr;
	if (IS_QLA2031(vha->hw))
		mcp->mb[2] = data & 0xff;
	else
		mcp->mb[2] = data;

	mcp->mb[3] = 0;
	mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1183,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1184,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla2x00_read_serdes_word(scsi_qla_host_t *vha, uint16_t addr, uint16_t *data)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_QLA25XX(vha->hw) && !IS_QLA2031(vha->hw) &&
	    !IS_QLA27XX(vha->hw))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1185,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_READ_SERDES;
	mcp->mb[1] = addr;
	mcp->mb[3] = 0;
	mcp->out_mb = MBX_3|MBX_1|MBX_0;
	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (IS_QLA2031(vha->hw))
		*data = mcp->mb[1] & 0xff;
	else
		*data = mcp->mb[1];

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1186,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1187,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla8044_write_serdes_word(scsi_qla_host_t *vha, uint32_t addr, uint32_t data)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_QLA8044(vha->hw))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x11a0,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_SET_GET_ETH_SERDES_REG;
	mcp->mb[1] = HCS_WRITE_SERDES;
	mcp->mb[3] = LSW(addr);
	mcp->mb[4] = MSW(addr);
	mcp->mb[5] = LSW(data);
	mcp->mb[6] = MSW(data);
	mcp->out_mb = MBX_6|MBX_5|MBX_4|MBX_3|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x11a1,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1188,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla8044_read_serdes_word(scsi_qla_host_t *vha, uint32_t addr, uint32_t *data)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_QLA8044(vha->hw))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1189,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_SET_GET_ETH_SERDES_REG;
	mcp->mb[1] = HCS_READ_SERDES;
	mcp->mb[3] = LSW(addr);
	mcp->mb[4] = MSW(addr);
	mcp->out_mb = MBX_4|MBX_3|MBX_1|MBX_0;
	mcp->in_mb = MBX_2|MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	*data = mcp->mb[2] << 16 | mcp->mb[1];

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x118a,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x118b,
		    "Done %s.\n", __func__);
	}

	return rval;
}

/**
 * qla2x00_set_serdes_params() -
 * @vha: HA context
 * @sw_em_1g:
 * @sw_em_2g:
 * @sw_em_4g:
 *
 * Returns
 */
int
qla2x00_set_serdes_params(scsi_qla_host_t *vha, uint16_t sw_em_1g,
    uint16_t sw_em_2g, uint16_t sw_em_4g)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x109e,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_SERDES_PARAMS;
	mcp->mb[1] = BIT_0;
	mcp->mb[2] = sw_em_1g | BIT_15;
	mcp->mb[3] = sw_em_2g | BIT_15;
	mcp->mb[4] = sw_em_4g | BIT_15;
	mcp->out_mb = MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx, vha, 0x109f,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		/*EMPTY*/
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10a0,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla2x00_stop_firmware(scsi_qla_host_t *vha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_FWI2_CAPABLE(vha->hw))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10a1,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_STOP_FIRMWARE;
	mcp->mb[1] = 0;
	mcp->out_mb = MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 5;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10a2, "Failed=%x.\n", rval);
		if (mcp->mb[0] == MBS_INVALID_COMMAND)
			rval = QLA_INVALID_COMMAND;
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10a3,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla2x00_enable_eft_trace(scsi_qla_host_t *vha, dma_addr_t eft_dma,
    uint16_t buffers)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10a4,
	    "Entered %s.\n", __func__);

	if (!IS_FWI2_CAPABLE(vha->hw))
		return QLA_FUNCTION_FAILED;

	if (unlikely(pci_channel_offline(vha->hw->pdev)))
		return QLA_FUNCTION_FAILED;

	mcp->mb[0] = MBC_TRACE_CONTROL;
	mcp->mb[1] = TC_EFT_ENABLE;
	mcp->mb[2] = LSW(eft_dma);
	mcp->mb[3] = MSW(eft_dma);
	mcp->mb[4] = LSW(MSD(eft_dma));
	mcp->mb[5] = MSW(MSD(eft_dma));
	mcp->mb[6] = buffers;
	mcp->mb[7] = TC_AEN_DISABLE;
	mcp->out_mb = MBX_7|MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10a5,
		    "Failed=%x mb[0]=%x mb[1]=%x.\n",
		    rval, mcp->mb[0], mcp->mb[1]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10a6,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla2x00_disable_eft_trace(scsi_qla_host_t *vha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10a7,
	    "Entered %s.\n", __func__);

	if (!IS_FWI2_CAPABLE(vha->hw))
		return QLA_FUNCTION_FAILED;

	if (unlikely(pci_channel_offline(vha->hw->pdev)))
		return QLA_FUNCTION_FAILED;

	mcp->mb[0] = MBC_TRACE_CONTROL;
	mcp->mb[1] = TC_EFT_DISABLE;
	mcp->out_mb = MBX_1|MBX_0;
	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10a8,
		    "Failed=%x mb[0]=%x mb[1]=%x.\n",
		    rval, mcp->mb[0], mcp->mb[1]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10a9,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla2x00_enable_fce_trace(scsi_qla_host_t *vha, dma_addr_t fce_dma,
    uint16_t buffers, uint16_t *mb, uint32_t *dwords)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10aa,
	    "Entered %s.\n", __func__);

	if (!IS_QLA25XX(vha->hw) && !IS_QLA81XX(vha->hw) &&
	    !IS_QLA83XX(vha->hw) && !IS_QLA27XX(vha->hw))
		return QLA_FUNCTION_FAILED;

	if (unlikely(pci_channel_offline(vha->hw->pdev)))
		return QLA_FUNCTION_FAILED;

	mcp->mb[0] = MBC_TRACE_CONTROL;
	mcp->mb[1] = TC_FCE_ENABLE;
	mcp->mb[2] = LSW(fce_dma);
	mcp->mb[3] = MSW(fce_dma);
	mcp->mb[4] = LSW(MSD(fce_dma));
	mcp->mb[5] = MSW(MSD(fce_dma));
	mcp->mb[6] = buffers;
	mcp->mb[7] = TC_AEN_DISABLE;
	mcp->mb[8] = 0;
	mcp->mb[9] = TC_FCE_DEFAULT_RX_SIZE;
	mcp->mb[10] = TC_FCE_DEFAULT_TX_SIZE;
	mcp->out_mb = MBX_10|MBX_9|MBX_8|MBX_7|MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|
	    MBX_1|MBX_0;
	mcp->in_mb = MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10ab,
		    "Failed=%x mb[0]=%x mb[1]=%x.\n",
		    rval, mcp->mb[0], mcp->mb[1]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10ac,
		    "Done %s.\n", __func__);

		if (mb)
			memcpy(mb, mcp->mb, 8 * sizeof(*mb));
		if (dwords)
			*dwords = buffers;
	}

	return rval;
}

int
qla2x00_disable_fce_trace(scsi_qla_host_t *vha, uint64_t *wr, uint64_t *rd)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10ad,
	    "Entered %s.\n", __func__);

	if (!IS_FWI2_CAPABLE(vha->hw))
		return QLA_FUNCTION_FAILED;

	if (unlikely(pci_channel_offline(vha->hw->pdev)))
		return QLA_FUNCTION_FAILED;

	mcp->mb[0] = MBC_TRACE_CONTROL;
	mcp->mb[1] = TC_FCE_DISABLE;
	mcp->mb[2] = TC_FCE_DISABLE_TRACE;
	mcp->out_mb = MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_9|MBX_8|MBX_7|MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|
	    MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10ae,
		    "Failed=%x mb[0]=%x mb[1]=%x.\n",
		    rval, mcp->mb[0], mcp->mb[1]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10af,
		    "Done %s.\n", __func__);

		if (wr)
			*wr = (uint64_t) mcp->mb[5] << 48 |
			    (uint64_t) mcp->mb[4] << 32 |
			    (uint64_t) mcp->mb[3] << 16 |
			    (uint64_t) mcp->mb[2];
		if (rd)
			*rd = (uint64_t) mcp->mb[9] << 48 |
			    (uint64_t) mcp->mb[8] << 32 |
			    (uint64_t) mcp->mb[7] << 16 |
			    (uint64_t) mcp->mb[6];
	}

	return rval;
}

int
qla2x00_get_idma_speed(scsi_qla_host_t *vha, uint16_t loop_id,
	uint16_t *port_speed, uint16_t *mb)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10b0,
	    "Entered %s.\n", __func__);

	if (!IS_IIDMA_CAPABLE(vha->hw))
		return QLA_FUNCTION_FAILED;

	mcp->mb[0] = MBC_PORT_PARAMS;
	mcp->mb[1] = loop_id;
	mcp->mb[2] = mcp->mb[3] = 0;
	mcp->mb[9] = vha->vp_idx;
	mcp->out_mb = MBX_9|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_3|MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	/* Return mailbox statuses. */
	if (mb != NULL) {
		mb[0] = mcp->mb[0];
		mb[1] = mcp->mb[1];
		mb[3] = mcp->mb[3];
	}

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10b1, "Failed=%x.\n", rval);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10b2,
		    "Done %s.\n", __func__);
		if (port_speed)
			*port_speed = mcp->mb[3];
	}

	return rval;
}

int
qla2x00_set_idma_speed(scsi_qla_host_t *vha, uint16_t loop_id,
    uint16_t port_speed, uint16_t *mb)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10b3,
	    "Entered %s.\n", __func__);

	if (!IS_IIDMA_CAPABLE(vha->hw))
		return QLA_FUNCTION_FAILED;

	mcp->mb[0] = MBC_PORT_PARAMS;
	mcp->mb[1] = loop_id;
	mcp->mb[2] = BIT_0;
	mcp->mb[3] = port_speed & (BIT_5|BIT_4|BIT_3|BIT_2|BIT_1|BIT_0);
	mcp->mb[9] = vha->vp_idx;
	mcp->out_mb = MBX_9|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_3|MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	/* Return mailbox statuses. */
	if (mb != NULL) {
		mb[0] = mcp->mb[0];
		mb[1] = mcp->mb[1];
		mb[3] = mcp->mb[3];
	}

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10b4,
		    "Failed=%x.\n", rval);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10b5,
		    "Done %s.\n", __func__);
	}

	return rval;
}

void
qla24xx_report_id_acquisition(scsi_qla_host_t *vha,
	struct vp_rpt_id_entry_24xx *rptid_entry)
{
	struct qla_hw_data *ha = vha->hw;
	scsi_qla_host_t *vp = NULL;
	unsigned long   flags;
	int found;
	port_id_t id;
	struct fc_port *fcport;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10b6,
	    "Entered %s.\n", __func__);

	if (rptid_entry->entry_status != 0)
		return;

	id.b.domain = rptid_entry->port_id[2];
	id.b.area   = rptid_entry->port_id[1];
	id.b.al_pa  = rptid_entry->port_id[0];
	id.b.rsvd_1 = 0;
	ha->flags.n2n_ae = 0;

	if (rptid_entry->format == 0) {
		/* loop */
		ql_dbg(ql_dbg_async, vha, 0x10b7,
		    "Format 0 : Number of VPs setup %d, number of "
		    "VPs acquired %d.\n", rptid_entry->vp_setup,
		    rptid_entry->vp_acquired);
		ql_dbg(ql_dbg_async, vha, 0x10b8,
		    "Primary port id %02x%02x%02x.\n",
		    rptid_entry->port_id[2], rptid_entry->port_id[1],
		    rptid_entry->port_id[0]);
		ha->current_topology = ISP_CFG_NL;
		qlt_update_host_map(vha, id);

	} else if (rptid_entry->format == 1) {
		/* fabric */
		ql_dbg(ql_dbg_async, vha, 0x10b9,
		    "Format 1: VP[%d] enabled - status %d - with "
		    "port id %02x%02x%02x.\n", rptid_entry->vp_idx,
			rptid_entry->vp_status,
		    rptid_entry->port_id[2], rptid_entry->port_id[1],
		    rptid_entry->port_id[0]);
		ql_dbg(ql_dbg_async, vha, 0x5075,
		   "Format 1: Remote WWPN %8phC.\n",
		   rptid_entry->u.f1.port_name);

		ql_dbg(ql_dbg_async, vha, 0x5075,
		   "Format 1: WWPN %8phC.\n",
		   vha->port_name);

		switch (rptid_entry->u.f1.flags & TOPO_MASK) {
		case TOPO_N2N:
			ha->current_topology = ISP_CFG_N;
			spin_lock_irqsave(&vha->hw->tgt.sess_lock, flags);
			fcport = qla2x00_find_fcport_by_wwpn(vha,
			    rptid_entry->u.f1.port_name, 1);
			spin_unlock_irqrestore(&vha->hw->tgt.sess_lock, flags);

			if (fcport) {
				fcport->plogi_nack_done_deadline = jiffies + HZ;
				fcport->dm_login_expire = jiffies + 3*HZ;
				fcport->scan_state = QLA_FCPORT_FOUND;
				switch (fcport->disc_state) {
				case DSC_DELETED:
					set_bit(RELOGIN_NEEDED,
					    &vha->dpc_flags);
					break;
				case DSC_DELETE_PEND:
					break;
				default:
					qlt_schedule_sess_for_deletion(fcport);
					break;
				}
			} else {
				id.b24 = 0;
				if (wwn_to_u64(vha->port_name) >
				    wwn_to_u64(rptid_entry->u.f1.port_name)) {
					vha->d_id.b24 = 0;
					vha->d_id.b.al_pa = 1;
					ha->flags.n2n_bigger = 1;

					id.b.al_pa = 2;
					ql_dbg(ql_dbg_async, vha, 0x5075,
					    "Format 1: assign local id %x remote id %x\n",
					    vha->d_id.b24, id.b24);
				} else {
					ql_dbg(ql_dbg_async, vha, 0x5075,
					    "Format 1: Remote login - Waiting for WWPN %8phC.\n",
					    rptid_entry->u.f1.port_name);
					ha->flags.n2n_bigger = 0;
				}
				qla24xx_post_newsess_work(vha, &id,
				    rptid_entry->u.f1.port_name,
				    rptid_entry->u.f1.node_name,
				    NULL,
				    FC4_TYPE_UNKNOWN);
			}

			/* if our portname is higher then initiate N2N login */

			set_bit(N2N_LOGIN_NEEDED, &vha->dpc_flags);
			ha->flags.n2n_ae = 1;
			return;
			break;
		case TOPO_FL:
			ha->current_topology = ISP_CFG_FL;
			break;
		case TOPO_F:
			ha->current_topology = ISP_CFG_F;
			break;
		default:
			break;
		}

		ha->flags.gpsc_supported = 1;
		ha->current_topology = ISP_CFG_F;
		/* buffer to buffer credit flag */
		vha->flags.bbcr_enable = (rptid_entry->u.f1.bbcr & 0xf) != 0;

		if (rptid_entry->vp_idx == 0) {
			if (rptid_entry->vp_status == VP_STAT_COMPL) {
				/* FA-WWN is only for physical port */
				if (qla_ini_mode_enabled(vha) &&
				    ha->flags.fawwpn_enabled &&
				    (rptid_entry->u.f1.flags &
				     BIT_6)) {
					memcpy(vha->port_name,
					    rptid_entry->u.f1.port_name,
					    WWN_SIZE);
				}

				qlt_update_host_map(vha, id);
			}

			set_bit(REGISTER_FC4_NEEDED, &vha->dpc_flags);
			set_bit(REGISTER_FDMI_NEEDED, &vha->dpc_flags);
		} else {
			if (rptid_entry->vp_status != VP_STAT_COMPL &&
				rptid_entry->vp_status != VP_STAT_ID_CHG) {
				ql_dbg(ql_dbg_mbx, vha, 0x10ba,
				    "Could not acquire ID for VP[%d].\n",
				    rptid_entry->vp_idx);
				return;
			}

			found = 0;
			spin_lock_irqsave(&ha->vport_slock, flags);
			list_for_each_entry(vp, &ha->vp_list, list) {
				if (rptid_entry->vp_idx == vp->vp_idx) {
					found = 1;
					break;
				}
			}
			spin_unlock_irqrestore(&ha->vport_slock, flags);

			if (!found)
				return;

			qlt_update_host_map(vp, id);

			/*
			 * Cannot configure here as we are still sitting on the
			 * response queue. Handle it in dpc context.
			 */
			set_bit(VP_IDX_ACQUIRED, &vp->vp_flags);
			set_bit(REGISTER_FC4_NEEDED, &vp->dpc_flags);
			set_bit(REGISTER_FDMI_NEEDED, &vp->dpc_flags);
		}
		set_bit(VP_DPC_NEEDED, &vha->dpc_flags);
		qla2xxx_wake_dpc(vha);
	} else if (rptid_entry->format == 2) {
		ql_dbg(ql_dbg_async, vha, 0x505f,
		    "RIDA: format 2/N2N Primary port id %02x%02x%02x.\n",
		    rptid_entry->port_id[2], rptid_entry->port_id[1],
		    rptid_entry->port_id[0]);

		ql_dbg(ql_dbg_async, vha, 0x5075,
		    "N2N: Remote WWPN %8phC.\n",
		    rptid_entry->u.f2.port_name);

		/* N2N.  direct connect */
		ha->current_topology = ISP_CFG_N;
		ha->flags.rida_fmt2 = 1;
		vha->d_id.b.domain = rptid_entry->port_id[2];
		vha->d_id.b.area = rptid_entry->port_id[1];
		vha->d_id.b.al_pa = rptid_entry->port_id[0];

		ha->flags.n2n_ae = 1;
		spin_lock_irqsave(&ha->vport_slock, flags);
		qlt_update_vp_map(vha, SET_AL_PA);
		spin_unlock_irqrestore(&ha->vport_slock, flags);

		list_for_each_entry(fcport, &vha->vp_fcports, list) {
			fcport->scan_state = QLA_FCPORT_SCAN;
		}

		fcport = qla2x00_find_fcport_by_wwpn(vha,
		    rptid_entry->u.f2.port_name, 1);

		if (fcport) {
			fcport->login_retry = vha->hw->login_retry_count;
			fcport->plogi_nack_done_deadline = jiffies + HZ;
			fcport->scan_state = QLA_FCPORT_FOUND;
		}
	}
}

/*
 * qla24xx_modify_vp_config
 *	Change VP configuration for vha
 *
 * Input:
 *	vha = adapter block pointer.
 *
 * Returns:
 *	qla2xxx local function return status code.
 *
 * Context:
 *	Kernel context.
 */
int
qla24xx_modify_vp_config(scsi_qla_host_t *vha)
{
	int		rval;
	struct vp_config_entry_24xx *vpmod;
	dma_addr_t	vpmod_dma;
	struct qla_hw_data *ha = vha->hw;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);

	/* This can be called by the parent */

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10bb,
	    "Entered %s.\n", __func__);

	vpmod = dma_pool_zalloc(ha->s_dma_pool, GFP_KERNEL, &vpmod_dma);
	if (!vpmod) {
		ql_log(ql_log_warn, vha, 0x10bc,
		    "Failed to allocate modify VP IOCB.\n");
		return QLA_MEMORY_ALLOC_FAILED;
	}

	vpmod->entry_type = VP_CONFIG_IOCB_TYPE;
	vpmod->entry_count = 1;
	vpmod->command = VCT_COMMAND_MOD_ENABLE_VPS;
	vpmod->vp_count = 1;
	vpmod->vp_index1 = vha->vp_idx;
	vpmod->options_idx1 = BIT_3|BIT_4|BIT_5;

	qlt_modify_vp_config(vha, vpmod);

	memcpy(vpmod->node_name_idx1, vha->node_name, WWN_SIZE);
	memcpy(vpmod->port_name_idx1, vha->port_name, WWN_SIZE);
	vpmod->entry_count = 1;

	rval = qla2x00_issue_iocb(base_vha, vpmod, vpmod_dma, 0);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10bd,
		    "Failed to issue VP config IOCB (%x).\n", rval);
	} else if (vpmod->comp_status != 0) {
		ql_dbg(ql_dbg_mbx, vha, 0x10be,
		    "Failed to complete IOCB -- error status (%x).\n",
		    vpmod->comp_status);
		rval = QLA_FUNCTION_FAILED;
	} else if (vpmod->comp_status != cpu_to_le16(CS_COMPLETE)) {
		ql_dbg(ql_dbg_mbx, vha, 0x10bf,
		    "Failed to complete IOCB -- completion status (%x).\n",
		    le16_to_cpu(vpmod->comp_status));
		rval = QLA_FUNCTION_FAILED;
	} else {
		/* EMPTY */
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10c0,
		    "Done %s.\n", __func__);
		fc_vport_set_state(vha->fc_vport, FC_VPORT_INITIALIZING);
	}
	dma_pool_free(ha->s_dma_pool, vpmod, vpmod_dma);

	return rval;
}

/*
 * qla2x00_send_change_request
 *	Receive or disable RSCN request from fabric controller
 *
 * Input:
 *	ha = adapter block pointer
 *	format = registration format:
 *		0 - Reserved
 *		1 - Fabric detected registration
 *		2 - N_port detected registration
 *		3 - Full registration
 *		FF - clear registration
 *	vp_idx = Virtual port index
 *
 * Returns:
 *	qla2x00 local function return status code.
 *
 * Context:
 *	Kernel Context
 */

int
qla2x00_send_change_request(scsi_qla_host_t *vha, uint16_t format,
			    uint16_t vp_idx)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10c7,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_SEND_CHANGE_REQUEST;
	mcp->mb[1] = format;
	mcp->mb[9] = vp_idx;
	mcp->out_mb = MBX_9|MBX_1|MBX_0;
	mcp->in_mb = MBX_0|MBX_1;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval == QLA_SUCCESS) {
		if (mcp->mb[0] != MBS_COMMAND_COMPLETE) {
			rval = BIT_1;
		}
	} else
		rval = BIT_1;

	return rval;
}

int
qla2x00_dump_ram(scsi_qla_host_t *vha, dma_addr_t req_dma, uint32_t addr,
    uint32_t size)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1009,
	    "Entered %s.\n", __func__);

	if (MSW(addr) || IS_FWI2_CAPABLE(vha->hw)) {
		mcp->mb[0] = MBC_DUMP_RISC_RAM_EXTENDED;
		mcp->mb[8] = MSW(addr);
		mcp->out_mb = MBX_8|MBX_0;
	} else {
		mcp->mb[0] = MBC_DUMP_RISC_RAM;
		mcp->out_mb = MBX_0;
	}
	mcp->mb[1] = LSW(addr);
	mcp->mb[2] = MSW(req_dma);
	mcp->mb[3] = LSW(req_dma);
	mcp->mb[6] = MSW(MSD(req_dma));
	mcp->mb[7] = LSW(MSD(req_dma));
	mcp->out_mb |= MBX_7|MBX_6|MBX_3|MBX_2|MBX_1;
	if (IS_FWI2_CAPABLE(vha->hw)) {
		mcp->mb[4] = MSW(size);
		mcp->mb[5] = LSW(size);
		mcp->out_mb |= MBX_5|MBX_4;
	} else {
		mcp->mb[4] = LSW(size);
		mcp->out_mb |= MBX_4;
	}

	mcp->in_mb = MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1008,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1007,
		    "Done %s.\n", __func__);
	}

	return rval;
}
/* 84XX Support **************************************************************/

struct cs84xx_mgmt_cmd {
	union {
		struct verify_chip_entry_84xx req;
		struct verify_chip_rsp_84xx rsp;
	} p;
};

int
qla84xx_verify_chip(struct scsi_qla_host *vha, uint16_t *status)
{
	int rval, retry;
	struct cs84xx_mgmt_cmd *mn;
	dma_addr_t mn_dma;
	uint16_t options;
	unsigned long flags;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10c8,
	    "Entered %s.\n", __func__);

	mn = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &mn_dma);
	if (mn == NULL) {
		return QLA_MEMORY_ALLOC_FAILED;
	}

	/* Force Update? */
	options = ha->cs84xx->fw_update ? VCO_FORCE_UPDATE : 0;
	/* Diagnostic firmware? */
	/* options |= MENLO_DIAG_FW; */
	/* We update the firmware with only one data sequence. */
	options |= VCO_END_OF_DATA;

	do {
		retry = 0;
		memset(mn, 0, sizeof(*mn));
		mn->p.req.entry_type = VERIFY_CHIP_IOCB_TYPE;
		mn->p.req.entry_count = 1;
		mn->p.req.options = cpu_to_le16(options);

		ql_dbg(ql_dbg_mbx + ql_dbg_buffer, vha, 0x111c,
		    "Dump of Verify Request.\n");
		ql_dump_buffer(ql_dbg_mbx + ql_dbg_buffer, vha, 0x111e,
		    (uint8_t *)mn, sizeof(*mn));

		rval = qla2x00_issue_iocb_timeout(vha, mn, mn_dma, 0, 120);
		if (rval != QLA_SUCCESS) {
			ql_dbg(ql_dbg_mbx, vha, 0x10cb,
			    "Failed to issue verify IOCB (%x).\n", rval);
			goto verify_done;
		}

		ql_dbg(ql_dbg_mbx + ql_dbg_buffer, vha, 0x1110,
		    "Dump of Verify Response.\n");
		ql_dump_buffer(ql_dbg_mbx + ql_dbg_buffer, vha, 0x1118,
		    (uint8_t *)mn, sizeof(*mn));

		status[0] = le16_to_cpu(mn->p.rsp.comp_status);
		status[1] = status[0] == CS_VCS_CHIP_FAILURE ?
		    le16_to_cpu(mn->p.rsp.failure_code) : 0;
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10ce,
		    "cs=%x fc=%x.\n", status[0], status[1]);

		if (status[0] != CS_COMPLETE) {
			rval = QLA_FUNCTION_FAILED;
			if (!(options & VCO_DONT_UPDATE_FW)) {
				ql_dbg(ql_dbg_mbx, vha, 0x10cf,
				    "Firmware update failed. Retrying "
				    "without update firmware.\n");
				options |= VCO_DONT_UPDATE_FW;
				options &= ~VCO_FORCE_UPDATE;
				retry = 1;
			}
		} else {
			ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10d0,
			    "Firmware updated to %x.\n",
			    le32_to_cpu(mn->p.rsp.fw_ver));

			/* NOTE: we only update OP firmware. */
			spin_lock_irqsave(&ha->cs84xx->access_lock, flags);
			ha->cs84xx->op_fw_version =
			    le32_to_cpu(mn->p.rsp.fw_ver);
			spin_unlock_irqrestore(&ha->cs84xx->access_lock,
			    flags);
		}
	} while (retry);

verify_done:
	dma_pool_free(ha->s_dma_pool, mn, mn_dma);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10d1,
		    "Failed=%x.\n", rval);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10d2,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla25xx_init_req_que(struct scsi_qla_host *vha, struct req_que *req)
{
	int rval;
	unsigned long flags;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	struct qla_hw_data *ha = vha->hw;

	if (!ha->flags.fw_started)
		return QLA_SUCCESS;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10d3,
	    "Entered %s.\n", __func__);

	if (IS_SHADOW_REG_CAPABLE(ha))
		req->options |= BIT_13;

	mcp->mb[0] = MBC_INITIALIZE_MULTIQ;
	mcp->mb[1] = req->options;
	mcp->mb[2] = MSW(LSD(req->dma));
	mcp->mb[3] = LSW(LSD(req->dma));
	mcp->mb[6] = MSW(MSD(req->dma));
	mcp->mb[7] = LSW(MSD(req->dma));
	mcp->mb[5] = req->length;
	if (req->rsp)
		mcp->mb[10] = req->rsp->id;
	mcp->mb[12] = req->qos;
	mcp->mb[11] = req->vp_idx;
	mcp->mb[13] = req->rid;
	if (IS_QLA83XX(ha) || IS_QLA27XX(ha))
		mcp->mb[15] = 0;

	mcp->mb[4] = req->id;
	/* que in ptr index */
	mcp->mb[8] = 0;
	/* que out ptr index */
	mcp->mb[9] = *req->out_ptr = 0;
	mcp->out_mb = MBX_14|MBX_13|MBX_12|MBX_11|MBX_10|MBX_9|MBX_8|MBX_7|
			MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->flags = MBX_DMA_OUT;
	mcp->tov = MBX_TOV_SECONDS * 2;

	if (IS_QLA81XX(ha) || IS_QLA83XX(ha) || IS_QLA27XX(ha))
		mcp->in_mb |= MBX_1;
	if (IS_QLA83XX(ha) || IS_QLA27XX(ha)) {
		mcp->out_mb |= MBX_15;
		/* debug q create issue in SR-IOV */
		mcp->in_mb |= MBX_9 | MBX_8 | MBX_7;
	}

	spin_lock_irqsave(&ha->hardware_lock, flags);
	if (!(req->options & BIT_0)) {
		WRT_REG_DWORD(req->req_q_in, 0);
		if (!IS_QLA83XX(ha) && !IS_QLA27XX(ha))
			WRT_REG_DWORD(req->req_q_out, 0);
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10d4,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10d5,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla25xx_init_rsp_que(struct scsi_qla_host *vha, struct rsp_que *rsp)
{
	int rval;
	unsigned long flags;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	struct qla_hw_data *ha = vha->hw;

	if (!ha->flags.fw_started)
		return QLA_SUCCESS;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10d6,
	    "Entered %s.\n", __func__);

	if (IS_SHADOW_REG_CAPABLE(ha))
		rsp->options |= BIT_13;

	mcp->mb[0] = MBC_INITIALIZE_MULTIQ;
	mcp->mb[1] = rsp->options;
	mcp->mb[2] = MSW(LSD(rsp->dma));
	mcp->mb[3] = LSW(LSD(rsp->dma));
	mcp->mb[6] = MSW(MSD(rsp->dma));
	mcp->mb[7] = LSW(MSD(rsp->dma));
	mcp->mb[5] = rsp->length;
	mcp->mb[14] = rsp->msix->entry;
	mcp->mb[13] = rsp->rid;
	if (IS_QLA83XX(ha) || IS_QLA27XX(ha))
		mcp->mb[15] = 0;

	mcp->mb[4] = rsp->id;
	/* que in ptr index */
	mcp->mb[8] = *rsp->in_ptr = 0;
	/* que out ptr index */
	mcp->mb[9] = 0;
	mcp->out_mb = MBX_14|MBX_13|MBX_9|MBX_8|MBX_7
			|MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->flags = MBX_DMA_OUT;
	mcp->tov = MBX_TOV_SECONDS * 2;

	if (IS_QLA81XX(ha)) {
		mcp->out_mb |= MBX_12|MBX_11|MBX_10;
		mcp->in_mb |= MBX_1;
	} else if (IS_QLA83XX(ha) || IS_QLA27XX(ha)) {
		mcp->out_mb |= MBX_15|MBX_12|MBX_11|MBX_10;
		mcp->in_mb |= MBX_1;
		/* debug q create issue in SR-IOV */
		mcp->in_mb |= MBX_9 | MBX_8 | MBX_7;
	}

	spin_lock_irqsave(&ha->hardware_lock, flags);
	if (!(rsp->options & BIT_0)) {
		WRT_REG_DWORD(rsp->rsp_q_out, 0);
		if (!IS_QLA83XX(ha) && !IS_QLA27XX(ha))
			WRT_REG_DWORD(rsp->rsp_q_in, 0);
	}

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10d7,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10d8,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla81xx_idc_ack(scsi_qla_host_t *vha, uint16_t *mb)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10d9,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_IDC_ACK;
	memcpy(&mcp->mb[1], mb, QLA_IDC_ACK_REGS * sizeof(uint16_t));
	mcp->out_mb = MBX_7|MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10da,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10db,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla81xx_fac_get_sector_size(scsi_qla_host_t *vha, uint32_t *sector_size)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10dc,
	    "Entered %s.\n", __func__);

	if (!IS_QLA81XX(vha->hw) && !IS_QLA83XX(vha->hw) &&
	    !IS_QLA27XX(vha->hw))
		return QLA_FUNCTION_FAILED;

	mcp->mb[0] = MBC_FLASH_ACCESS_CTRL;
	mcp->mb[1] = FAC_OPT_CMD_GET_SECTOR_SIZE;
	mcp->out_mb = MBX_1|MBX_0;
	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10dd,
		    "Failed=%x mb[0]=%x mb[1]=%x.\n",
		    rval, mcp->mb[0], mcp->mb[1]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10de,
		    "Done %s.\n", __func__);
		*sector_size = mcp->mb[1];
	}

	return rval;
}

int
qla81xx_fac_do_write_enable(scsi_qla_host_t *vha, int enable)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_QLA81XX(vha->hw) && !IS_QLA83XX(vha->hw) &&
	    !IS_QLA27XX(vha->hw))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10df,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_FLASH_ACCESS_CTRL;
	mcp->mb[1] = enable ? FAC_OPT_CMD_WRITE_ENABLE :
	    FAC_OPT_CMD_WRITE_PROTECT;
	mcp->out_mb = MBX_1|MBX_0;
	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10e0,
		    "Failed=%x mb[0]=%x mb[1]=%x.\n",
		    rval, mcp->mb[0], mcp->mb[1]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10e1,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla81xx_fac_erase_sector(scsi_qla_host_t *vha, uint32_t start, uint32_t finish)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_QLA81XX(vha->hw) && !IS_QLA83XX(vha->hw) &&
	    !IS_QLA27XX(vha->hw))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10e2,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_FLASH_ACCESS_CTRL;
	mcp->mb[1] = FAC_OPT_CMD_ERASE_SECTOR;
	mcp->mb[2] = LSW(start);
	mcp->mb[3] = MSW(start);
	mcp->mb[4] = LSW(finish);
	mcp->mb[5] = MSW(finish);
	mcp->out_mb = MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_2|MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10e3,
		    "Failed=%x mb[0]=%x mb[1]=%x mb[2]=%x.\n",
		    rval, mcp->mb[0], mcp->mb[1], mcp->mb[2]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10e4,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla81xx_restart_mpi_firmware(scsi_qla_host_t *vha)
{
	int rval = 0;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10e5,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_RESTART_MPI_FW;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_0|MBX_1;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10e6,
		    "Failed=%x mb[0]=%x mb[1]=%x.\n",
		    rval, mcp->mb[0], mcp->mb[1]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10e7,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla82xx_set_driver_version(scsi_qla_host_t *vha, char *version)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	int i;
	int len;
	uint16_t *str;
	struct qla_hw_data *ha = vha->hw;

	if (!IS_P3P_TYPE(ha))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x117b,
	    "Entered %s.\n", __func__);

	str = (void *)version;
	len = strlen(version);

	mcp->mb[0] = MBC_SET_RNID_PARAMS;
	mcp->mb[1] = RNID_TYPE_SET_VERSION << 8;
	mcp->out_mb = MBX_1|MBX_0;
	for (i = 4; i < 16 && len; i++, str++, len -= 2) {
		mcp->mb[i] = cpu_to_le16p(str);
		mcp->out_mb |= 1<<i;
	}
	for (; i < 16; i++) {
		mcp->mb[i] = 0;
		mcp->out_mb |= 1<<i;
	}
	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x117c,
		    "Failed=%x mb[0]=%x,%x.\n", rval, mcp->mb[0], mcp->mb[1]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x117d,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla25xx_set_driver_version(scsi_qla_host_t *vha, char *version)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	int len;
	uint16_t dwlen;
	uint8_t *str;
	dma_addr_t str_dma;
	struct qla_hw_data *ha = vha->hw;

	if (!IS_FWI2_CAPABLE(ha) || IS_QLA24XX_TYPE(ha) || IS_QLA81XX(ha) ||
	    IS_P3P_TYPE(ha))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x117e,
	    "Entered %s.\n", __func__);

	str = dma_pool_alloc(ha->s_dma_pool, GFP_KERNEL, &str_dma);
	if (!str) {
		ql_log(ql_log_warn, vha, 0x117f,
		    "Failed to allocate driver version param.\n");
		return QLA_MEMORY_ALLOC_FAILED;
	}

	memcpy(str, "\x7\x3\x11\x0", 4);
	dwlen = str[0];
	len = dwlen * 4 - 4;
	memset(str + 4, 0, len);
	if (len > strlen(version))
		len = strlen(version);
	memcpy(str + 4, version, len);

	mcp->mb[0] = MBC_SET_RNID_PARAMS;
	mcp->mb[1] = RNID_TYPE_SET_VERSION << 8 | dwlen;
	mcp->mb[2] = MSW(LSD(str_dma));
	mcp->mb[3] = LSW(LSD(str_dma));
	mcp->mb[6] = MSW(MSD(str_dma));
	mcp->mb[7] = LSW(MSD(str_dma));
	mcp->out_mb = MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1180,
		    "Failed=%x mb[0]=%x,%x.\n", rval, mcp->mb[0], mcp->mb[1]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1181,
		    "Done %s.\n", __func__);
	}

	dma_pool_free(ha->s_dma_pool, str, str_dma);

	return rval;
}

int
qla24xx_get_port_login_templ(scsi_qla_host_t *vha, dma_addr_t buf_dma,
			     void *buf, uint16_t bufsiz)
{
	int rval, i;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	uint32_t	*bp;

	if (!IS_FWI2_CAPABLE(vha->hw))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1159,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_GET_RNID_PARAMS;
	mcp->mb[1] = RNID_TYPE_PORT_LOGIN << 8;
	mcp->mb[2] = MSW(buf_dma);
	mcp->mb[3] = LSW(buf_dma);
	mcp->mb[6] = MSW(MSD(buf_dma));
	mcp->mb[7] = LSW(MSD(buf_dma));
	mcp->mb[8] = bufsiz/4;
	mcp->out_mb = MBX_8|MBX_7|MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x115a,
		    "Failed=%x mb[0]=%x,%x.\n", rval, mcp->mb[0], mcp->mb[1]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x115b,
		    "Done %s.\n", __func__);
		bp = (uint32_t *) buf;
		for (i = 0; i < (bufsiz-4)/4; i++, bp++)
			*bp = le32_to_cpu(*bp);
	}

	return rval;
}

static int
qla2x00_read_asic_temperature(scsi_qla_host_t *vha, uint16_t *temp)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_FWI2_CAPABLE(vha->hw))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1159,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_GET_RNID_PARAMS;
	mcp->mb[1] = RNID_TYPE_ASIC_TEMP << 8;
	mcp->out_mb = MBX_1|MBX_0;
	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);
	*temp = mcp->mb[1];

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x115a,
		    "Failed=%x mb[0]=%x,%x.\n", rval, mcp->mb[0], mcp->mb[1]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x115b,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla2x00_read_sfp(scsi_qla_host_t *vha, dma_addr_t sfp_dma, uint8_t *sfp,
	uint16_t dev, uint16_t off, uint16_t len, uint16_t opt)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10e8,
	    "Entered %s.\n", __func__);

	if (!IS_FWI2_CAPABLE(ha))
		return QLA_FUNCTION_FAILED;

	if (len == 1)
		opt |= BIT_0;

	mcp->mb[0] = MBC_READ_SFP;
	mcp->mb[1] = dev;
	mcp->mb[2] = MSW(sfp_dma);
	mcp->mb[3] = LSW(sfp_dma);
	mcp->mb[6] = MSW(MSD(sfp_dma));
	mcp->mb[7] = LSW(MSD(sfp_dma));
	mcp->mb[8] = len;
	mcp->mb[9] = off;
	mcp->mb[10] = opt;
	mcp->out_mb = MBX_10|MBX_9|MBX_8|MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (opt & BIT_0)
		*sfp = mcp->mb[1];

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10e9,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
		if (mcp->mb[0] == MBS_COMMAND_ERROR &&
		    mcp->mb[1] == 0x22)
			/* sfp is not there */
			rval = QLA_INTERFACE_ERROR;
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10ea,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla2x00_write_sfp(scsi_qla_host_t *vha, dma_addr_t sfp_dma, uint8_t *sfp,
	uint16_t dev, uint16_t off, uint16_t len, uint16_t opt)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10eb,
	    "Entered %s.\n", __func__);

	if (!IS_FWI2_CAPABLE(ha))
		return QLA_FUNCTION_FAILED;

	if (len == 1)
		opt |= BIT_0;

	if (opt & BIT_0)
		len = *sfp;

	mcp->mb[0] = MBC_WRITE_SFP;
	mcp->mb[1] = dev;
	mcp->mb[2] = MSW(sfp_dma);
	mcp->mb[3] = LSW(sfp_dma);
	mcp->mb[6] = MSW(MSD(sfp_dma));
	mcp->mb[7] = LSW(MSD(sfp_dma));
	mcp->mb[8] = len;
	mcp->mb[9] = off;
	mcp->mb[10] = opt;
	mcp->out_mb = MBX_10|MBX_9|MBX_8|MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10ec,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10ed,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla2x00_get_xgmac_stats(scsi_qla_host_t *vha, dma_addr_t stats_dma,
    uint16_t size_in_bytes, uint16_t *actual_size)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10ee,
	    "Entered %s.\n", __func__);

	if (!IS_CNA_CAPABLE(vha->hw))
		return QLA_FUNCTION_FAILED;

	mcp->mb[0] = MBC_GET_XGMAC_STATS;
	mcp->mb[2] = MSW(stats_dma);
	mcp->mb[3] = LSW(stats_dma);
	mcp->mb[6] = MSW(MSD(stats_dma));
	mcp->mb[7] = LSW(MSD(stats_dma));
	mcp->mb[8] = size_in_bytes >> 2;
	mcp->out_mb = MBX_8|MBX_7|MBX_6|MBX_3|MBX_2|MBX_0;
	mcp->in_mb = MBX_2|MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10ef,
		    "Failed=%x mb[0]=%x mb[1]=%x mb[2]=%x.\n",
		    rval, mcp->mb[0], mcp->mb[1], mcp->mb[2]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10f0,
		    "Done %s.\n", __func__);


		*actual_size = mcp->mb[2] << 2;
	}

	return rval;
}

int
qla2x00_get_dcbx_params(scsi_qla_host_t *vha, dma_addr_t tlv_dma,
    uint16_t size)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10f1,
	    "Entered %s.\n", __func__);

	if (!IS_CNA_CAPABLE(vha->hw))
		return QLA_FUNCTION_FAILED;

	mcp->mb[0] = MBC_GET_DCBX_PARAMS;
	mcp->mb[1] = 0;
	mcp->mb[2] = MSW(tlv_dma);
	mcp->mb[3] = LSW(tlv_dma);
	mcp->mb[6] = MSW(MSD(tlv_dma));
	mcp->mb[7] = LSW(MSD(tlv_dma));
	mcp->mb[8] = size;
	mcp->out_mb = MBX_8|MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_2|MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10f2,
		    "Failed=%x mb[0]=%x mb[1]=%x mb[2]=%x.\n",
		    rval, mcp->mb[0], mcp->mb[1], mcp->mb[2]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10f3,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla2x00_read_ram_word(scsi_qla_host_t *vha, uint32_t risc_addr, uint32_t *data)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10f4,
	    "Entered %s.\n", __func__);

	if (!IS_FWI2_CAPABLE(vha->hw))
		return QLA_FUNCTION_FAILED;

	mcp->mb[0] = MBC_READ_RAM_EXTENDED;
	mcp->mb[1] = LSW(risc_addr);
	mcp->mb[8] = MSW(risc_addr);
	mcp->out_mb = MBX_8|MBX_1|MBX_0;
	mcp->in_mb = MBX_3|MBX_2|MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10f5,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10f6,
		    "Done %s.\n", __func__);
		*data = mcp->mb[3] << 16 | mcp->mb[2];
	}

	return rval;
}

int
qla2x00_loopback_test(scsi_qla_host_t *vha, struct msg_echo_lb *mreq,
	uint16_t *mresp)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10f7,
	    "Entered %s.\n", __func__);

	memset(mcp->mb, 0 , sizeof(mcp->mb));
	mcp->mb[0] = MBC_DIAGNOSTIC_LOOP_BACK;
	mcp->mb[1] = mreq->options | BIT_6;	// BIT_6 specifies 64 bit addressing

	/* transfer count */
	mcp->mb[10] = LSW(mreq->transfer_size);
	mcp->mb[11] = MSW(mreq->transfer_size);

	/* send data address */
	mcp->mb[14] = LSW(mreq->send_dma);
	mcp->mb[15] = MSW(mreq->send_dma);
	mcp->mb[20] = LSW(MSD(mreq->send_dma));
	mcp->mb[21] = MSW(MSD(mreq->send_dma));

	/* receive data address */
	mcp->mb[16] = LSW(mreq->rcv_dma);
	mcp->mb[17] = MSW(mreq->rcv_dma);
	mcp->mb[6] = LSW(MSD(mreq->rcv_dma));
	mcp->mb[7] = MSW(MSD(mreq->rcv_dma));

	/* Iteration count */
	mcp->mb[18] = LSW(mreq->iteration_count);
	mcp->mb[19] = MSW(mreq->iteration_count);

	mcp->out_mb = MBX_21|MBX_20|MBX_19|MBX_18|MBX_17|MBX_16|MBX_15|
	    MBX_14|MBX_13|MBX_12|MBX_11|MBX_10|MBX_7|MBX_6|MBX_1|MBX_0;
	if (IS_CNA_CAPABLE(vha->hw))
		mcp->out_mb |= MBX_2;
	mcp->in_mb = MBX_19|MBX_18|MBX_3|MBX_2|MBX_1|MBX_0;

	mcp->buf_size = mreq->transfer_size;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = MBX_DMA_OUT|MBX_DMA_IN|IOCTL_CMD;

	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10f8,
		    "Failed=%x mb[0]=%x mb[1]=%x mb[2]=%x mb[3]=%x mb[18]=%x "
		    "mb[19]=%x.\n", rval, mcp->mb[0], mcp->mb[1], mcp->mb[2],
		    mcp->mb[3], mcp->mb[18], mcp->mb[19]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10f9,
		    "Done %s.\n", __func__);
	}

	/* Copy mailbox information */
	memcpy( mresp, mcp->mb, 64);
	return rval;
}

int
qla2x00_echo_test(scsi_qla_host_t *vha, struct msg_echo_lb *mreq,
	uint16_t *mresp)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10fa,
	    "Entered %s.\n", __func__);

	memset(mcp->mb, 0 , sizeof(mcp->mb));
	mcp->mb[0] = MBC_DIAGNOSTIC_ECHO;
	/* BIT_6 specifies 64bit address */
	mcp->mb[1] = mreq->options | BIT_15 | BIT_6;
	if (IS_CNA_CAPABLE(ha)) {
		mcp->mb[2] = vha->fcoe_fcf_idx;
	}
	mcp->mb[16] = LSW(mreq->rcv_dma);
	mcp->mb[17] = MSW(mreq->rcv_dma);
	mcp->mb[6] = LSW(MSD(mreq->rcv_dma));
	mcp->mb[7] = MSW(MSD(mreq->rcv_dma));

	mcp->mb[10] = LSW(mreq->transfer_size);

	mcp->mb[14] = LSW(mreq->send_dma);
	mcp->mb[15] = MSW(mreq->send_dma);
	mcp->mb[20] = LSW(MSD(mreq->send_dma));
	mcp->mb[21] = MSW(MSD(mreq->send_dma));

	mcp->out_mb = MBX_21|MBX_20|MBX_17|MBX_16|MBX_15|
	    MBX_14|MBX_10|MBX_7|MBX_6|MBX_1|MBX_0;
	if (IS_CNA_CAPABLE(ha))
		mcp->out_mb |= MBX_2;

	mcp->in_mb = MBX_0;
	if (IS_QLA24XX_TYPE(ha) || IS_QLA25XX(ha) ||
	    IS_CNA_CAPABLE(ha) || IS_QLA2031(ha))
		mcp->in_mb |= MBX_1;
	if (IS_CNA_CAPABLE(ha) || IS_QLA2031(ha))
		mcp->in_mb |= MBX_3;

	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = MBX_DMA_OUT|MBX_DMA_IN|IOCTL_CMD;
	mcp->buf_size = mreq->transfer_size;

	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10fb,
		    "Failed=%x mb[0]=%x mb[1]=%x.\n",
		    rval, mcp->mb[0], mcp->mb[1]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10fc,
		    "Done %s.\n", __func__);
	}

	/* Copy mailbox information */
	memcpy(mresp, mcp->mb, 64);
	return rval;
}

int
qla84xx_reset_chip(scsi_qla_host_t *vha, uint16_t enable_diagnostic)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10fd,
	    "Entered %s enable_diag=%d.\n", __func__, enable_diagnostic);

	mcp->mb[0] = MBC_ISP84XX_RESET;
	mcp->mb[1] = enable_diagnostic;
	mcp->out_mb = MBX_1|MBX_0;
	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = MBX_DMA_OUT|MBX_DMA_IN|IOCTL_CMD;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS)
		ql_dbg(ql_dbg_mbx, vha, 0x10fe, "Failed=%x.\n", rval);
	else
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10ff,
		    "Done %s.\n", __func__);

	return rval;
}

int
qla2x00_write_ram_word(scsi_qla_host_t *vha, uint32_t risc_addr, uint32_t data)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1100,
	    "Entered %s.\n", __func__);

	if (!IS_FWI2_CAPABLE(vha->hw))
		return QLA_FUNCTION_FAILED;

	mcp->mb[0] = MBC_WRITE_RAM_WORD_EXTENDED;
	mcp->mb[1] = LSW(risc_addr);
	mcp->mb[2] = LSW(data);
	mcp->mb[3] = MSW(data);
	mcp->mb[8] = MSW(risc_addr);
	mcp->out_mb = MBX_8|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1101,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1102,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla81xx_write_mpi_register(scsi_qla_host_t *vha, uint16_t *mb)
{
	int rval;
	uint32_t stat, timer;
	uint16_t mb0 = 0;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	rval = QLA_SUCCESS;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1103,
	    "Entered %s.\n", __func__);

	clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

	/* Write the MBC data to the registers */
	WRT_REG_WORD(&reg->mailbox0, MBC_WRITE_MPI_REGISTER);
	WRT_REG_WORD(&reg->mailbox1, mb[0]);
	WRT_REG_WORD(&reg->mailbox2, mb[1]);
	WRT_REG_WORD(&reg->mailbox3, mb[2]);
	WRT_REG_WORD(&reg->mailbox4, mb[3]);

	WRT_REG_DWORD(&reg->hccr, HCCRX_SET_HOST_INT);

	/* Poll for MBC interrupt */
	for (timer = 6000000; timer; timer--) {
		/* Check for pending interrupts. */
		stat = RD_REG_DWORD(&reg->host_status);
		if (stat & HSRX_RISC_INT) {
			stat &= 0xff;

			if (stat == 0x1 || stat == 0x2 ||
			    stat == 0x10 || stat == 0x11) {
				set_bit(MBX_INTERRUPT,
				    &ha->mbx_cmd_flags);
				mb0 = RD_REG_WORD(&reg->mailbox0);
				WRT_REG_DWORD(&reg->hccr,
				    HCCRX_CLR_RISC_INT);
				RD_REG_DWORD(&reg->hccr);
				break;
			}
		}
		udelay(5);
	}

	if (test_and_clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags))
		rval = mb0 & MBS_MASK;
	else
		rval = QLA_FUNCTION_FAILED;

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1104,
		    "Failed=%x mb[0]=%x.\n", rval, mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1105,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla2x00_get_data_rate(scsi_qla_host_t *vha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1106,
	    "Entered %s.\n", __func__);

	if (!IS_FWI2_CAPABLE(ha))
		return QLA_FUNCTION_FAILED;

	mcp->mb[0] = MBC_DATA_RATE;
	mcp->mb[1] = 0;
	mcp->out_mb = MBX_1|MBX_0;
	mcp->in_mb = MBX_2|MBX_1|MBX_0;
	if (IS_QLA83XX(ha) || IS_QLA27XX(ha))
		mcp->in_mb |= MBX_3;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1107,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1108,
		    "Done %s.\n", __func__);
		if (mcp->mb[1] != 0x7)
			ha->link_data_rate = mcp->mb[1];
	}

	return rval;
}

int
qla81xx_get_port_config(scsi_qla_host_t *vha, uint16_t *mb)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1109,
	    "Entered %s.\n", __func__);

	if (!IS_QLA81XX(ha) && !IS_QLA83XX(ha) && !IS_QLA8044(ha) &&
	    !IS_QLA27XX(ha))
		return QLA_FUNCTION_FAILED;
	mcp->mb[0] = MBC_GET_PORT_CONFIG;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;

	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x110a,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		/* Copy all bits to preserve original value */
		memcpy(mb, &mcp->mb[1], sizeof(uint16_t) * 4);

		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x110b,
		    "Done %s.\n", __func__);
	}
	return rval;
}

int
qla81xx_set_port_config(scsi_qla_host_t *vha, uint16_t *mb)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x110c,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_SET_PORT_CONFIG;
	/* Copy all bits to preserve original setting */
	memcpy(&mcp->mb[1], mb, sizeof(uint16_t) * 4);
	mcp->out_mb = MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x110d,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x110e,
		    "Done %s.\n", __func__);

	return rval;
}


int
qla24xx_set_fcp_prio(scsi_qla_host_t *vha, uint16_t loop_id, uint16_t priority,
		uint16_t *mb)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	struct qla_hw_data *ha = vha->hw;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x110f,
	    "Entered %s.\n", __func__);

	if (!IS_QLA24XX_TYPE(ha) && !IS_QLA25XX(ha))
		return QLA_FUNCTION_FAILED;

	mcp->mb[0] = MBC_PORT_PARAMS;
	mcp->mb[1] = loop_id;
	if (ha->flags.fcp_prio_enabled)
		mcp->mb[2] = BIT_1;
	else
		mcp->mb[2] = BIT_2;
	mcp->mb[4] = priority & 0xf;
	mcp->mb[9] = vha->vp_idx;
	mcp->out_mb = MBX_9|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_4|MBX_3|MBX_1|MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);
	if (mb != NULL) {
		mb[0] = mcp->mb[0];
		mb[1] = mcp->mb[1];
		mb[3] = mcp->mb[3];
		mb[4] = mcp->mb[4];
	}

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x10cd, "Failed=%x.\n", rval);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x10cc,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla2x00_get_thermal_temp(scsi_qla_host_t *vha, uint16_t *temp)
{
	int rval = QLA_FUNCTION_FAILED;
	struct qla_hw_data *ha = vha->hw;
	uint8_t byte;

	if (!IS_FWI2_CAPABLE(ha) || IS_QLA24XX_TYPE(ha) || IS_QLA81XX(ha)) {
		ql_dbg(ql_dbg_mbx, vha, 0x1150,
		    "Thermal not supported by this card.\n");
		return rval;
	}

	if (IS_QLA25XX(ha)) {
		if (ha->pdev->subsystem_vendor == PCI_VENDOR_ID_QLOGIC &&
		    ha->pdev->subsystem_device == 0x0175) {
			rval = qla2x00_read_sfp(vha, 0, &byte,
			    0x98, 0x1, 1, BIT_13|BIT_0);
			*temp = byte;
			return rval;
		}
		if (ha->pdev->subsystem_vendor == PCI_VENDOR_ID_HP &&
		    ha->pdev->subsystem_device == 0x338e) {
			rval = qla2x00_read_sfp(vha, 0, &byte,
			    0x98, 0x1, 1, BIT_15|BIT_14|BIT_0);
			*temp = byte;
			return rval;
		}
		ql_dbg(ql_dbg_mbx, vha, 0x10c9,
		    "Thermal not supported by this card.\n");
		return rval;
	}

	if (IS_QLA82XX(ha)) {
		*temp = qla82xx_read_temperature(vha);
		rval = QLA_SUCCESS;
		return rval;
	} else if (IS_QLA8044(ha)) {
		*temp = qla8044_read_temperature(vha);
		rval = QLA_SUCCESS;
		return rval;
	}

	rval = qla2x00_read_asic_temperature(vha, temp);
	return rval;
}

int
qla82xx_mbx_intr_enable(scsi_qla_host_t *vha)
{
	int rval;
	struct qla_hw_data *ha = vha->hw;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1017,
	    "Entered %s.\n", __func__);

	if (!IS_FWI2_CAPABLE(ha))
		return QLA_FUNCTION_FAILED;

	memset(mcp, 0, sizeof(mbx_cmd_t));
	mcp->mb[0] = MBC_TOGGLE_INTERRUPT;
	mcp->mb[1] = 1;

	mcp->out_mb = MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;

	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1016,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x100e,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla82xx_mbx_intr_disable(scsi_qla_host_t *vha)
{
	int rval;
	struct qla_hw_data *ha = vha->hw;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x100d,
	    "Entered %s.\n", __func__);

	if (!IS_P3P_TYPE(ha))
		return QLA_FUNCTION_FAILED;

	memset(mcp, 0, sizeof(mbx_cmd_t));
	mcp->mb[0] = MBC_TOGGLE_INTERRUPT;
	mcp->mb[1] = 0;

	mcp->out_mb = MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;

	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x100c,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x100b,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla82xx_md_get_template_size(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	int rval = QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x111f,
	    "Entered %s.\n", __func__);

	memset(mcp->mb, 0 , sizeof(mcp->mb));
	mcp->mb[0] = LSW(MBC_DIAGNOSTIC_MINIDUMP_TEMPLATE);
	mcp->mb[1] = MSW(MBC_DIAGNOSTIC_MINIDUMP_TEMPLATE);
	mcp->mb[2] = LSW(RQST_TMPLT_SIZE);
	mcp->mb[3] = MSW(RQST_TMPLT_SIZE);

	mcp->out_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_14|MBX_13|MBX_12|MBX_11|MBX_10|MBX_9|MBX_8|
	    MBX_7|MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;

	mcp->flags = MBX_DMA_OUT|MBX_DMA_IN|IOCTL_CMD;
	mcp->tov = MBX_TOV_SECONDS;
	rval = qla2x00_mailbox_command(vha, mcp);

	/* Always copy back return mailbox values. */
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1120,
		    "mailbox command FAILED=0x%x, subcode=%x.\n",
		    (mcp->mb[1] << 16) | mcp->mb[0],
		    (mcp->mb[3] << 16) | mcp->mb[2]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1121,
		    "Done %s.\n", __func__);
		ha->md_template_size = ((mcp->mb[3] << 16) | mcp->mb[2]);
		if (!ha->md_template_size) {
			ql_dbg(ql_dbg_mbx, vha, 0x1122,
			    "Null template size obtained.\n");
			rval = QLA_FUNCTION_FAILED;
		}
	}
	return rval;
}

int
qla82xx_md_get_template(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	int rval = QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1123,
	    "Entered %s.\n", __func__);

	ha->md_tmplt_hdr = dma_alloc_coherent(&ha->pdev->dev,
	   ha->md_template_size, &ha->md_tmplt_hdr_dma, GFP_KERNEL);
	if (!ha->md_tmplt_hdr) {
		ql_log(ql_log_warn, vha, 0x1124,
		    "Unable to allocate memory for Minidump template.\n");
		return rval;
	}

	memset(mcp->mb, 0 , sizeof(mcp->mb));
	mcp->mb[0] = LSW(MBC_DIAGNOSTIC_MINIDUMP_TEMPLATE);
	mcp->mb[1] = MSW(MBC_DIAGNOSTIC_MINIDUMP_TEMPLATE);
	mcp->mb[2] = LSW(RQST_TMPLT);
	mcp->mb[3] = MSW(RQST_TMPLT);
	mcp->mb[4] = LSW(LSD(ha->md_tmplt_hdr_dma));
	mcp->mb[5] = MSW(LSD(ha->md_tmplt_hdr_dma));
	mcp->mb[6] = LSW(MSD(ha->md_tmplt_hdr_dma));
	mcp->mb[7] = MSW(MSD(ha->md_tmplt_hdr_dma));
	mcp->mb[8] = LSW(ha->md_template_size);
	mcp->mb[9] = MSW(ha->md_template_size);

	mcp->flags = MBX_DMA_OUT|MBX_DMA_IN|IOCTL_CMD;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->out_mb = MBX_11|MBX_10|MBX_9|MBX_8|
	    MBX_7|MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1125,
		    "mailbox command FAILED=0x%x, subcode=%x.\n",
		    ((mcp->mb[1] << 16) | mcp->mb[0]),
		    ((mcp->mb[3] << 16) | mcp->mb[2]));
	} else
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1126,
		    "Done %s.\n", __func__);
	return rval;
}

int
qla8044_md_get_template(scsi_qla_host_t *vha)
{
	struct qla_hw_data *ha = vha->hw;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	int rval = QLA_FUNCTION_FAILED;
	int offset = 0, size = MINIDUMP_SIZE_36K;
	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0xb11f,
	    "Entered %s.\n", __func__);

	ha->md_tmplt_hdr = dma_alloc_coherent(&ha->pdev->dev,
	   ha->md_template_size, &ha->md_tmplt_hdr_dma, GFP_KERNEL);
	if (!ha->md_tmplt_hdr) {
		ql_log(ql_log_warn, vha, 0xb11b,
		    "Unable to allocate memory for Minidump template.\n");
		return rval;
	}

	memset(mcp->mb, 0 , sizeof(mcp->mb));
	while (offset < ha->md_template_size) {
		mcp->mb[0] = LSW(MBC_DIAGNOSTIC_MINIDUMP_TEMPLATE);
		mcp->mb[1] = MSW(MBC_DIAGNOSTIC_MINIDUMP_TEMPLATE);
		mcp->mb[2] = LSW(RQST_TMPLT);
		mcp->mb[3] = MSW(RQST_TMPLT);
		mcp->mb[4] = LSW(LSD(ha->md_tmplt_hdr_dma + offset));
		mcp->mb[5] = MSW(LSD(ha->md_tmplt_hdr_dma + offset));
		mcp->mb[6] = LSW(MSD(ha->md_tmplt_hdr_dma + offset));
		mcp->mb[7] = MSW(MSD(ha->md_tmplt_hdr_dma + offset));
		mcp->mb[8] = LSW(size);
		mcp->mb[9] = MSW(size);
		mcp->mb[10] = offset & 0x0000FFFF;
		mcp->mb[11] = offset & 0xFFFF0000;
		mcp->flags = MBX_DMA_OUT|MBX_DMA_IN|IOCTL_CMD;
		mcp->tov = MBX_TOV_SECONDS;
		mcp->out_mb = MBX_11|MBX_10|MBX_9|MBX_8|
			MBX_7|MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;
		mcp->in_mb = MBX_3|MBX_2|MBX_1|MBX_0;
		rval = qla2x00_mailbox_command(vha, mcp);

		if (rval != QLA_SUCCESS) {
			ql_dbg(ql_dbg_mbx, vha, 0xb11c,
				"mailbox command FAILED=0x%x, subcode=%x.\n",
				((mcp->mb[1] << 16) | mcp->mb[0]),
				((mcp->mb[3] << 16) | mcp->mb[2]));
			return rval;
		} else
			ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0xb11d,
				"Done %s.\n", __func__);
		offset = offset + size;
	}
	return rval;
}

int
qla81xx_set_led_config(scsi_qla_host_t *vha, uint16_t *led_cfg)
{
	int rval;
	struct qla_hw_data *ha = vha->hw;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_QLA81XX(ha) && !IS_QLA8031(ha))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1133,
	    "Entered %s.\n", __func__);

	memset(mcp, 0, sizeof(mbx_cmd_t));
	mcp->mb[0] = MBC_SET_LED_CONFIG;
	mcp->mb[1] = led_cfg[0];
	mcp->mb[2] = led_cfg[1];
	if (IS_QLA8031(ha)) {
		mcp->mb[3] = led_cfg[2];
		mcp->mb[4] = led_cfg[3];
		mcp->mb[5] = led_cfg[4];
		mcp->mb[6] = led_cfg[5];
	}

	mcp->out_mb = MBX_2|MBX_1|MBX_0;
	if (IS_QLA8031(ha))
		mcp->out_mb |= MBX_6|MBX_5|MBX_4|MBX_3;
	mcp->in_mb = MBX_0;
	mcp->tov = 30;
	mcp->flags = 0;

	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1134,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1135,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla81xx_get_led_config(scsi_qla_host_t *vha, uint16_t *led_cfg)
{
	int rval;
	struct qla_hw_data *ha = vha->hw;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_QLA81XX(ha) && !IS_QLA8031(ha))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1136,
	    "Entered %s.\n", __func__);

	memset(mcp, 0, sizeof(mbx_cmd_t));
	mcp->mb[0] = MBC_GET_LED_CONFIG;

	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_2|MBX_1|MBX_0;
	if (IS_QLA8031(ha))
		mcp->in_mb |= MBX_6|MBX_5|MBX_4|MBX_3;
	mcp->tov = 30;
	mcp->flags = 0;

	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1137,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		led_cfg[0] = mcp->mb[1];
		led_cfg[1] = mcp->mb[2];
		if (IS_QLA8031(ha)) {
			led_cfg[2] = mcp->mb[3];
			led_cfg[3] = mcp->mb[4];
			led_cfg[4] = mcp->mb[5];
			led_cfg[5] = mcp->mb[6];
		}
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1138,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla82xx_mbx_beacon_ctl(scsi_qla_host_t *vha, int enable)
{
	int rval;
	struct qla_hw_data *ha = vha->hw;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_P3P_TYPE(ha))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1127,
		"Entered %s.\n", __func__);

	memset(mcp, 0, sizeof(mbx_cmd_t));
	mcp->mb[0] = MBC_SET_LED_CONFIG;
	if (enable)
		mcp->mb[7] = 0xE;
	else
		mcp->mb[7] = 0xD;

	mcp->out_mb = MBX_7|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;

	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1128,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1129,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla83xx_wr_reg(scsi_qla_host_t *vha, uint32_t reg, uint32_t data)
{
	int rval;
	struct qla_hw_data *ha = vha->hw;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_QLA83XX(ha) && !IS_QLA27XX(ha))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1130,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_WRITE_REMOTE_REG;
	mcp->mb[1] = LSW(reg);
	mcp->mb[2] = MSW(reg);
	mcp->mb[3] = LSW(data);
	mcp->mb[4] = MSW(data);
	mcp->out_mb = MBX_4|MBX_3|MBX_2|MBX_1|MBX_0;

	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1131,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1132,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla2x00_port_logout(scsi_qla_host_t *vha, struct fc_port *fcport)
{
	int rval;
	struct qla_hw_data *ha = vha->hw;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (IS_QLA2100(ha) || IS_QLA2200(ha)) {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x113b,
		    "Implicit LOGO Unsupported.\n");
		return QLA_FUNCTION_FAILED;
	}


	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x113c,
	    "Entering %s.\n",  __func__);

	/* Perform Implicit LOGO. */
	mcp->mb[0] = MBC_PORT_LOGOUT;
	mcp->mb[1] = fcport->loop_id;
	mcp->mb[10] = BIT_15;
	mcp->out_mb = MBX_10|MBX_1|MBX_0;
	mcp->in_mb = MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval != QLA_SUCCESS)
		ql_dbg(ql_dbg_mbx, vha, 0x113d,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	else
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x113e,
		    "Done %s.\n", __func__);

	return rval;
}

int
qla83xx_rd_reg(scsi_qla_host_t *vha, uint32_t reg, uint32_t *data)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	struct qla_hw_data *ha = vha->hw;
	unsigned long retry_max_time = jiffies + (2 * HZ);

	if (!IS_QLA83XX(ha) && !IS_QLA27XX(ha))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx, vha, 0x114b, "Entered %s.\n", __func__);

retry_rd_reg:
	mcp->mb[0] = MBC_READ_REMOTE_REG;
	mcp->mb[1] = LSW(reg);
	mcp->mb[2] = MSW(reg);
	mcp->out_mb = MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_4|MBX_3|MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x114c,
		    "Failed=%x mb[0]=%x mb[1]=%x.\n",
		    rval, mcp->mb[0], mcp->mb[1]);
	} else {
		*data = (mcp->mb[3] | (mcp->mb[4] << 16));
		if (*data == QLA8XXX_BAD_VALUE) {
			/*
			 * During soft-reset CAMRAM register reads might
			 * return 0xbad0bad0. So retry for MAX of 2 sec
			 * while reading camram registers.
			 */
			if (time_after(jiffies, retry_max_time)) {
				ql_dbg(ql_dbg_mbx, vha, 0x1141,
				    "Failure to read CAMRAM register. "
				    "data=0x%x.\n", *data);
				return QLA_FUNCTION_FAILED;
			}
			msleep(100);
			goto retry_rd_reg;
		}
		ql_dbg(ql_dbg_mbx, vha, 0x1142, "Done %s.\n", __func__);
	}

	return rval;
}

int
qla83xx_restart_nic_firmware(scsi_qla_host_t *vha)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA83XX(ha) && !IS_QLA27XX(ha))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx, vha, 0x1143, "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_RESTART_NIC_FIRMWARE;
	mcp->out_mb = MBX_0;
	mcp->in_mb = MBX_1|MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1144,
		    "Failed=%x mb[0]=%x mb[1]=%x.\n",
		    rval, mcp->mb[0], mcp->mb[1]);
		ha->isp_ops->fw_dump(vha, 0);
	} else {
		ql_dbg(ql_dbg_mbx, vha, 0x1145, "Done %s.\n", __func__);
	}

	return rval;
}

int
qla83xx_access_control(scsi_qla_host_t *vha, uint16_t options,
	uint32_t start_addr, uint32_t end_addr, uint16_t *sector_size)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	uint8_t subcode = (uint8_t)options;
	struct qla_hw_data *ha = vha->hw;

	if (!IS_QLA8031(ha))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx, vha, 0x1146, "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_SET_ACCESS_CONTROL;
	mcp->mb[1] = options;
	mcp->out_mb = MBX_1|MBX_0;
	if (subcode & BIT_2) {
		mcp->mb[2] = LSW(start_addr);
		mcp->mb[3] = MSW(start_addr);
		mcp->mb[4] = LSW(end_addr);
		mcp->mb[5] = MSW(end_addr);
		mcp->out_mb |= MBX_5|MBX_4|MBX_3|MBX_2;
	}
	mcp->in_mb = MBX_2|MBX_1|MBX_0;
	if (!(subcode & (BIT_2 | BIT_5)))
		mcp->in_mb |= MBX_4|MBX_3;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1147,
		    "Failed=%x mb[0]=%x mb[1]=%x mb[2]=%x mb[3]=%x mb[4]=%x.\n",
		    rval, mcp->mb[0], mcp->mb[1], mcp->mb[2], mcp->mb[3],
		    mcp->mb[4]);
		ha->isp_ops->fw_dump(vha, 0);
	} else {
		if (subcode & BIT_5)
			*sector_size = mcp->mb[1];
		else if (subcode & (BIT_6 | BIT_7)) {
			ql_dbg(ql_dbg_mbx, vha, 0x1148,
			    "Driver-lock id=%x%x", mcp->mb[4], mcp->mb[3]);
		} else if (subcode & (BIT_3 | BIT_4)) {
			ql_dbg(ql_dbg_mbx, vha, 0x1149,
			    "Flash-lock id=%x%x", mcp->mb[4], mcp->mb[3]);
		}
		ql_dbg(ql_dbg_mbx, vha, 0x114a, "Done %s.\n", __func__);
	}

	return rval;
}

int
qla2x00_dump_mctp_data(scsi_qla_host_t *vha, dma_addr_t req_dma, uint32_t addr,
	uint32_t size)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;

	if (!IS_MCTP_CAPABLE(vha->hw))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x114f,
	    "Entered %s.\n", __func__);

	mcp->mb[0] = MBC_DUMP_RISC_RAM_EXTENDED;
	mcp->mb[1] = LSW(addr);
	mcp->mb[2] = MSW(req_dma);
	mcp->mb[3] = LSW(req_dma);
	mcp->mb[4] = MSW(size);
	mcp->mb[5] = LSW(size);
	mcp->mb[6] = MSW(MSD(req_dma));
	mcp->mb[7] = LSW(MSD(req_dma));
	mcp->mb[8] = MSW(addr);
	/* Setting RAM ID to valid */
	mcp->mb[10] |= BIT_7;
	/* For MCTP RAM ID is 0x40 */
	mcp->mb[10] |= 0x40;

	mcp->out_mb |= MBX_10|MBX_8|MBX_7|MBX_6|MBX_5|MBX_4|MBX_3|MBX_2|MBX_1|
	    MBX_0;

	mcp->in_mb = MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x114e,
		    "Failed=%x mb[0]=%x.\n", rval, mcp->mb[0]);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x114d,
		    "Done %s.\n", __func__);
	}

	return rval;
}

int
qla26xx_dport_diagnostics(scsi_qla_host_t *vha,
	void *dd_buf, uint size, uint options)
{
	int rval;
	mbx_cmd_t mc;
	mbx_cmd_t *mcp = &mc;
	dma_addr_t dd_dma;

	if (!IS_QLA83XX(vha->hw) && !IS_QLA27XX(vha->hw))
		return QLA_FUNCTION_FAILED;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x119f,
	    "Entered %s.\n", __func__);

	dd_dma = dma_map_single(&vha->hw->pdev->dev,
	    dd_buf, size, DMA_FROM_DEVICE);
	if (dma_mapping_error(&vha->hw->pdev->dev, dd_dma)) {
		ql_log(ql_log_warn, vha, 0x1194, "Failed to map dma buffer.\n");
		return QLA_MEMORY_ALLOC_FAILED;
	}

	memset(dd_buf, 0, size);

	mcp->mb[0] = MBC_DPORT_DIAGNOSTICS;
	mcp->mb[1] = options;
	mcp->mb[2] = MSW(LSD(dd_dma));
	mcp->mb[3] = LSW(LSD(dd_dma));
	mcp->mb[6] = MSW(MSD(dd_dma));
	mcp->mb[7] = LSW(MSD(dd_dma));
	mcp->mb[8] = size;
	mcp->out_mb = MBX_8|MBX_7|MBX_6|MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->in_mb = MBX_3|MBX_2|MBX_1|MBX_0;
	mcp->buf_size = size;
	mcp->flags = MBX_DMA_IN;
	mcp->tov = MBX_TOV_SECONDS * 4;
	rval = qla2x00_mailbox_command(vha, mcp);

	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1195, "Failed=%x.\n", rval);
	} else {
		ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1196,
		    "Done %s.\n", __func__);
	}

	dma_unmap_single(&vha->hw->pdev->dev, dd_dma,
	    size, DMA_FROM_DEVICE);

	return rval;
}

static void qla2x00_async_mb_sp_done(void *s, int res)
{
	struct srb *sp = s;

	sp->u.iocb_cmd.u.mbx.rc = res;

	complete(&sp->u.iocb_cmd.u.mbx.comp);
	/* don't free sp here. Let the caller do the free */
}

/*
 * This mailbox uses the iocb interface to send MB command.
 * This allows non-critial (non chip setup) command to go
 * out in parrallel.
 */
int qla24xx_send_mb_cmd(struct scsi_qla_host *vha, mbx_cmd_t *mcp)
{
	int rval = QLA_FUNCTION_FAILED;
	srb_t *sp;
	struct srb_iocb *c;

	if (!vha->hw->flags.fw_started)
		goto done;

	sp = qla2x00_get_sp(vha, NULL, GFP_KERNEL);
	if (!sp)
		goto done;

	sp->type = SRB_MB_IOCB;
	sp->name = mb_to_str(mcp->mb[0]);

	c = &sp->u.iocb_cmd;
	c->timeout = qla2x00_async_iocb_timeout;
	init_completion(&c->u.mbx.comp);

	qla2x00_init_timer(sp, qla2x00_get_async_timeout(vha) + 2);

	memcpy(sp->u.iocb_cmd.u.mbx.out_mb, mcp->mb, SIZEOF_IOCB_MB_REG);

	sp->done = qla2x00_async_mb_sp_done;

	rval = qla2x00_start_sp(sp);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1018,
		    "%s: %s Failed submission. %x.\n",
		    __func__, sp->name, rval);
		goto done_free_sp;
	}

	ql_dbg(ql_dbg_mbx, vha, 0x113f, "MB:%s hndl %x submitted\n",
	    sp->name, sp->handle);

	wait_for_completion(&c->u.mbx.comp);
	memcpy(mcp->mb, sp->u.iocb_cmd.u.mbx.in_mb, SIZEOF_IOCB_MB_REG);

	rval = c->u.mbx.rc;
	switch (rval) {
	case QLA_FUNCTION_TIMEOUT:
		ql_dbg(ql_dbg_mbx, vha, 0x1140, "%s: %s Timeout. %x.\n",
		    __func__, sp->name, rval);
		break;
	case  QLA_SUCCESS:
		ql_dbg(ql_dbg_mbx, vha, 0x119d, "%s: %s done.\n",
		    __func__, sp->name);
		sp->free(sp);
		break;
	default:
		ql_dbg(ql_dbg_mbx, vha, 0x119e, "%s: %s Failed. %x.\n",
		    __func__, sp->name, rval);
		sp->free(sp);
		break;
	}

	return rval;

done_free_sp:
	sp->free(sp);
done:
	return rval;
}

/*
 * qla24xx_gpdb_wait
 * NOTE: Do not call this routine from DPC thread
 */
int qla24xx_gpdb_wait(struct scsi_qla_host *vha, fc_port_t *fcport, u8 opt)
{
	int rval = QLA_FUNCTION_FAILED;
	dma_addr_t pd_dma;
	struct port_database_24xx *pd;
	struct qla_hw_data *ha = vha->hw;
	mbx_cmd_t mc;

	if (!vha->hw->flags.fw_started)
		goto done;

	pd = dma_pool_zalloc(ha->s_dma_pool, GFP_KERNEL, &pd_dma);
	if (pd  == NULL) {
		ql_log(ql_log_warn, vha, 0xd047,
		    "Failed to allocate port database structure.\n");
		goto done_free_sp;
	}

	memset(&mc, 0, sizeof(mc));
	mc.mb[0] = MBC_GET_PORT_DATABASE;
	mc.mb[1] = cpu_to_le16(fcport->loop_id);
	mc.mb[2] = MSW(pd_dma);
	mc.mb[3] = LSW(pd_dma);
	mc.mb[6] = MSW(MSD(pd_dma));
	mc.mb[7] = LSW(MSD(pd_dma));
	mc.mb[9] = cpu_to_le16(vha->vp_idx);
	mc.mb[10] = cpu_to_le16((uint16_t)opt);

	rval = qla24xx_send_mb_cmd(vha, &mc);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x1193,
		    "%s: %8phC fail\n", __func__, fcport->port_name);
		goto done_free_sp;
	}

	rval = __qla24xx_parse_gpdb(vha, fcport, pd);

	ql_dbg(ql_dbg_mbx, vha, 0x1197, "%s: %8phC done\n",
	    __func__, fcport->port_name);

done_free_sp:
	if (pd)
		dma_pool_free(ha->s_dma_pool, pd, pd_dma);
done:
	return rval;
}

int __qla24xx_parse_gpdb(struct scsi_qla_host *vha, fc_port_t *fcport,
    struct port_database_24xx *pd)
{
	int rval = QLA_SUCCESS;
	uint64_t zero = 0;
	u8 current_login_state, last_login_state;

	if (fcport->fc4f_nvme) {
		current_login_state = pd->current_login_state >> 4;
		last_login_state = pd->last_login_state >> 4;
	} else {
		current_login_state = pd->current_login_state & 0xf;
		last_login_state = pd->last_login_state & 0xf;
	}

	/* Check for logged in state. */
	if (current_login_state != PDS_PRLI_COMPLETE) {
		ql_dbg(ql_dbg_mbx, vha, 0x119a,
		    "Unable to verify login-state (%x/%x) for loop_id %x.\n",
		    current_login_state, last_login_state, fcport->loop_id);
		rval = QLA_FUNCTION_FAILED;
		goto gpd_error_out;
	}

	if (fcport->loop_id == FC_NO_LOOP_ID ||
	    (memcmp(fcport->port_name, (uint8_t *)&zero, 8) &&
	     memcmp(fcport->port_name, pd->port_name, 8))) {
		/* We lost the device mid way. */
		rval = QLA_NOT_LOGGED_IN;
		goto gpd_error_out;
	}

	/* Names are little-endian. */
	memcpy(fcport->node_name, pd->node_name, WWN_SIZE);
	memcpy(fcport->port_name, pd->port_name, WWN_SIZE);

	/* Get port_id of device. */
	fcport->d_id.b.domain = pd->port_id[0];
	fcport->d_id.b.area = pd->port_id[1];
	fcport->d_id.b.al_pa = pd->port_id[2];
	fcport->d_id.b.rsvd_1 = 0;

	if (fcport->fc4f_nvme) {
		fcport->nvme_prli_service_param =
		    pd->prli_nvme_svc_param_word_3;
		fcport->port_type = FCT_NVME;
	} else {
		/* If not target must be initiator or unknown type. */
		if ((pd->prli_svc_param_word_3[0] & BIT_4) == 0)
			fcport->port_type = FCT_INITIATOR;
		else
			fcport->port_type = FCT_TARGET;
	}
	/* Passback COS information. */
	fcport->supported_classes = (pd->flags & PDF_CLASS_2) ?
		FC_COS_CLASS2 : FC_COS_CLASS3;

	if (pd->prli_svc_param_word_3[0] & BIT_7) {
		fcport->flags |= FCF_CONF_COMP_SUPPORTED;
		fcport->conf_compl_supported = 1;
	}

gpd_error_out:
	return rval;
}

/*
 * qla24xx_gidlist__wait
 * NOTE: don't call this routine from DPC thread.
 */
int qla24xx_gidlist_wait(struct scsi_qla_host *vha,
	void *id_list, dma_addr_t id_list_dma, uint16_t *entries)
{
	int rval = QLA_FUNCTION_FAILED;
	mbx_cmd_t mc;

	if (!vha->hw->flags.fw_started)
		goto done;

	memset(&mc, 0, sizeof(mc));
	mc.mb[0] = MBC_GET_ID_LIST;
	mc.mb[2] = MSW(id_list_dma);
	mc.mb[3] = LSW(id_list_dma);
	mc.mb[6] = MSW(MSD(id_list_dma));
	mc.mb[7] = LSW(MSD(id_list_dma));
	mc.mb[8] = 0;
	mc.mb[9] = cpu_to_le16(vha->vp_idx);

	rval = qla24xx_send_mb_cmd(vha, &mc);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0x119b,
		    "%s:  fail\n", __func__);
	} else {
		*entries = mc.mb[1];
		ql_dbg(ql_dbg_mbx, vha, 0x119c,
		    "%s:  done\n", __func__);
	}
done:
	return rval;
}

int qla27xx_set_zio_threshold(scsi_qla_host_t *vha, uint16_t value)
{
	int rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1200,
	    "Entered %s\n", __func__);

	memset(mcp->mb, 0 , sizeof(mcp->mb));
	mcp->mb[0] = MBC_GET_SET_ZIO_THRESHOLD;
	mcp->mb[1] = cpu_to_le16(1);
	mcp->mb[2] = cpu_to_le16(value);
	mcp->out_mb = MBX_2 | MBX_1 | MBX_0;
	mcp->in_mb = MBX_2 | MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;

	rval = qla2x00_mailbox_command(vha, mcp);

	ql_dbg(ql_dbg_mbx, vha, 0x1201, "%s %x\n",
	    (rval != QLA_SUCCESS) ? "Failed"  : "Done", rval);

	return rval;
}

int qla27xx_get_zio_threshold(scsi_qla_host_t *vha, uint16_t *value)
{
	int rval;
	mbx_cmd_t	mc;
	mbx_cmd_t	*mcp = &mc;

	ql_dbg(ql_dbg_mbx + ql_dbg_verbose, vha, 0x1203,
	    "Entered %s\n", __func__);

	memset(mcp->mb, 0, sizeof(mcp->mb));
	mcp->mb[0] = MBC_GET_SET_ZIO_THRESHOLD;
	mcp->mb[1] = cpu_to_le16(0);
	mcp->out_mb = MBX_1 | MBX_0;
	mcp->in_mb = MBX_2 | MBX_0;
	mcp->tov = MBX_TOV_SECONDS;
	mcp->flags = 0;

	rval = qla2x00_mailbox_command(vha, mcp);
	if (rval == QLA_SUCCESS)
		*value = mc.mb[2];

	ql_dbg(ql_dbg_mbx, vha, 0x1205, "%s %x\n",
	    (rval != QLA_SUCCESS) ? "Failed" : "Done", rval);

	return rval;
}

int
qla2x00_read_sfp_dev(struct scsi_qla_host *vha, char *buf, int count)
{
	struct qla_hw_data *ha = vha->hw;
	uint16_t iter, addr, offset;
	dma_addr_t phys_addr;
	int rval, c;
	u8 *sfp_data;

	memset(ha->sfp_data, 0, SFP_DEV_SIZE);
	addr = 0xa0;
	phys_addr = ha->sfp_data_dma;
	sfp_data = ha->sfp_data;
	offset = c = 0;

	for (iter = 0; iter < SFP_DEV_SIZE / SFP_BLOCK_SIZE; iter++) {
		if (iter == 4) {
			/* Skip to next device address. */
			addr = 0xa2;
			offset = 0;
		}

		rval = qla2x00_read_sfp(vha, phys_addr, sfp_data,
		    addr, offset, SFP_BLOCK_SIZE, BIT_1);
		if (rval != QLA_SUCCESS) {
			ql_log(ql_log_warn, vha, 0x706d,
			    "Unable to read SFP data (%x/%x/%x).\n", rval,
			    addr, offset);

			return rval;
		}

		if (buf && (c < count)) {
			u16 sz;

			if ((count - c) >= SFP_BLOCK_SIZE)
				sz = SFP_BLOCK_SIZE;
			else
				sz = count - c;

			memcpy(buf, sfp_data, sz);
			buf += SFP_BLOCK_SIZE;
			c += sz;
		}
		phys_addr += SFP_BLOCK_SIZE;
		sfp_data  += SFP_BLOCK_SIZE;
		offset += SFP_BLOCK_SIZE;
	}

	return rval;
}

int qla24xx_res_count_wait(struct scsi_qla_host *vha,
    uint16_t *out_mb, int out_mb_sz)
{
	int rval = QLA_FUNCTION_FAILED;
	mbx_cmd_t mc;

	if (!vha->hw->flags.fw_started)
		goto done;

	memset(&mc, 0, sizeof(mc));
	mc.mb[0] = MBC_GET_RESOURCE_COUNTS;

	rval = qla24xx_send_mb_cmd(vha, &mc);
	if (rval != QLA_SUCCESS) {
		ql_dbg(ql_dbg_mbx, vha, 0xffff,
			"%s:  fail\n", __func__);
	} else {
		if (out_mb_sz <= SIZEOF_IOCB_MB_REG)
			memcpy(out_mb, mc.mb, out_mb_sz);
		else
			memcpy(out_mb, mc.mb, SIZEOF_IOCB_MB_REG);

		ql_dbg(ql_dbg_mbx, vha, 0xffff,
			"%s:  done\n", __func__);
	}
done:
	return rval;
}
