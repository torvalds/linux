/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2013 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

#include <linux/ctype.h>
#include "ql4_def.h"
#include "ql4_glbl.h"
#include "ql4_dbg.h"
#include "ql4_inline.h"
#include "ql4_version.h"

void qla4xxx_queue_mbox_cmd(struct scsi_qla_host *ha, uint32_t *mbx_cmd,
			    int in_count)
{
	int i;

	/* Load all mailbox registers, except mailbox 0. */
	for (i = 1; i < in_count; i++)
		writel(mbx_cmd[i], &ha->reg->mailbox[i]);

	/* Wakeup firmware  */
	writel(mbx_cmd[0], &ha->reg->mailbox[0]);
	readl(&ha->reg->mailbox[0]);
	writel(set_rmask(CSR_INTR_RISC), &ha->reg->ctrl_status);
	readl(&ha->reg->ctrl_status);
}

void qla4xxx_process_mbox_intr(struct scsi_qla_host *ha, int out_count)
{
	int intr_status;

	intr_status = readl(&ha->reg->ctrl_status);
	if (intr_status & INTR_PENDING) {
		/*
		 * Service the interrupt.
		 * The ISR will save the mailbox status registers
		 * to a temporary storage location in the adapter structure.
		 */
		ha->mbox_status_count = out_count;
		ha->isp_ops->interrupt_service_routine(ha, intr_status);
	}
}

/**
 * qla4xxx_is_intr_poll_mode – Are we allowed to poll for interrupts?
 * @ha: Pointer to host adapter structure.
 * @ret: 1=polling mode, 0=non-polling mode
 **/
static int qla4xxx_is_intr_poll_mode(struct scsi_qla_host *ha)
{
	int rval = 1;

	if (is_qla8032(ha) || is_qla8042(ha)) {
		if (test_bit(AF_IRQ_ATTACHED, &ha->flags) &&
		    test_bit(AF_83XX_MBOX_INTR_ON, &ha->flags))
			rval = 0;
	} else {
		if (test_bit(AF_IRQ_ATTACHED, &ha->flags) &&
		    test_bit(AF_INTERRUPTS_ON, &ha->flags) &&
		    test_bit(AF_ONLINE, &ha->flags) &&
		    !test_bit(AF_HA_REMOVAL, &ha->flags))
			rval = 0;
	}

	return rval;
}

/**
 * qla4xxx_mailbox_command - issues mailbox commands
 * @ha: Pointer to host adapter structure.
 * @inCount: number of mailbox registers to load.
 * @outCount: number of mailbox registers to return.
 * @mbx_cmd: data pointer for mailbox in registers.
 * @mbx_sts: data pointer for mailbox out registers.
 *
 * This routine issue mailbox commands and waits for completion.
 * If outCount is 0, this routine completes successfully WITHOUT waiting
 * for the mailbox command to complete.
 **/
int qla4xxx_mailbox_command(struct scsi_qla_host *ha, uint8_t inCount,
			    uint8_t outCount, uint32_t *mbx_cmd,
			    uint32_t *mbx_sts)
{
	int status = QLA_ERROR;
	uint8_t i;
	u_long wait_count;
	unsigned long flags = 0;
	uint32_t dev_state;

	/* Make sure that pointers are valid */
	if (!mbx_cmd || !mbx_sts) {
		DEBUG2(printk("scsi%ld: %s: Invalid mbx_cmd or mbx_sts "
			      "pointer\n", ha->host_no, __func__));
		return status;
	}

	if (is_qla40XX(ha)) {
		if (test_bit(AF_HA_REMOVAL, &ha->flags)) {
			DEBUG2(ql4_printk(KERN_WARNING, ha, "scsi%ld: %s: "
					  "prematurely completing mbx cmd as "
					  "adapter removal detected\n",
					  ha->host_no, __func__));
			return status;
		}
	}

	if ((is_aer_supported(ha)) &&
	    (test_bit(AF_PCI_CHANNEL_IO_PERM_FAILURE, &ha->flags))) {
		DEBUG2(printk(KERN_WARNING "scsi%ld: %s: Perm failure on EEH, "
		    "timeout MBX Exiting.\n", ha->host_no, __func__));
		return status;
	}

	/* Mailbox code active */
	wait_count = MBOX_TOV * 100;

	while (wait_count--) {
		mutex_lock(&ha->mbox_sem);
		if (!test_bit(AF_MBOX_COMMAND, &ha->flags)) {
			set_bit(AF_MBOX_COMMAND, &ha->flags);
			mutex_unlock(&ha->mbox_sem);
			break;
		}
		mutex_unlock(&ha->mbox_sem);
		if (!wait_count) {
			DEBUG2(printk("scsi%ld: %s: mbox_sem failed\n",
				ha->host_no, __func__));
			return status;
		}
		msleep(10);
	}

	if (is_qla80XX(ha)) {
		if (test_bit(AF_FW_RECOVERY, &ha->flags)) {
			DEBUG2(ql4_printk(KERN_WARNING, ha,
					  "scsi%ld: %s: prematurely completing mbx cmd as firmware recovery detected\n",
					  ha->host_no, __func__));
			goto mbox_exit;
		}
		/* Do not send any mbx cmd if h/w is in failed state*/
		ha->isp_ops->idc_lock(ha);
		dev_state = qla4_8xxx_rd_direct(ha, QLA8XXX_CRB_DEV_STATE);
		ha->isp_ops->idc_unlock(ha);
		if (dev_state == QLA8XXX_DEV_FAILED) {
			ql4_printk(KERN_WARNING, ha,
				   "scsi%ld: %s: H/W is in failed state, do not send any mailbox commands\n",
				   ha->host_no, __func__);
			goto mbox_exit;
		}
	}

	spin_lock_irqsave(&ha->hardware_lock, flags);

	ha->mbox_status_count = outCount;
	for (i = 0; i < outCount; i++)
		ha->mbox_status[i] = 0;

	/* Queue the mailbox command to the firmware */
	ha->isp_ops->queue_mailbox_command(ha, mbx_cmd, inCount);

	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	/* Wait for completion */

	/*
	 * If we don't want status, don't wait for the mailbox command to
	 * complete.  For example, MBOX_CMD_RESET_FW doesn't return status,
	 * you must poll the inbound Interrupt Mask for completion.
	 */
	if (outCount == 0) {
		status = QLA_SUCCESS;
		goto mbox_exit;
	}

	/*
	 * Wait for completion: Poll or completion queue
	 */
	if (qla4xxx_is_intr_poll_mode(ha)) {
		/* Poll for command to complete */
		wait_count = jiffies + MBOX_TOV * HZ;
		while (test_bit(AF_MBOX_COMMAND_DONE, &ha->flags) == 0) {
			if (time_after_eq(jiffies, wait_count))
				break;
			/*
			 * Service the interrupt.
			 * The ISR will save the mailbox status registers
			 * to a temporary storage location in the adapter
			 * structure.
			 */
			spin_lock_irqsave(&ha->hardware_lock, flags);
			ha->isp_ops->process_mailbox_interrupt(ha, outCount);
			spin_unlock_irqrestore(&ha->hardware_lock, flags);
			msleep(10);
		}
	} else {
		/* Do not poll for completion. Use completion queue */
		set_bit(AF_MBOX_COMMAND_NOPOLL, &ha->flags);
		wait_for_completion_timeout(&ha->mbx_intr_comp, MBOX_TOV * HZ);
		clear_bit(AF_MBOX_COMMAND_NOPOLL, &ha->flags);
	}

	/* Check for mailbox timeout. */
	if (!test_bit(AF_MBOX_COMMAND_DONE, &ha->flags)) {
		if (is_qla80XX(ha) &&
		    test_bit(AF_FW_RECOVERY, &ha->flags)) {
			DEBUG2(ql4_printk(KERN_INFO, ha,
			    "scsi%ld: %s: prematurely completing mbx cmd as "
			    "firmware recovery detected\n",
			    ha->host_no, __func__));
			goto mbox_exit;
		}
		DEBUG2(printk("scsi%ld: Mailbox Cmd 0x%08X timed out ...,"
			      " Scheduling Adapter Reset\n", ha->host_no,
			      mbx_cmd[0]));
		ha->mailbox_timeout_count++;
		mbx_sts[0] = (-1);
		set_bit(DPC_RESET_HA, &ha->dpc_flags);
		if (is_qla8022(ha)) {
			ql4_printk(KERN_INFO, ha,
				   "disabling pause transmit on port 0 & 1.\n");
			qla4_82xx_wr_32(ha, QLA82XX_CRB_NIU + 0x98,
					CRB_NIU_XG_PAUSE_CTL_P0 |
					CRB_NIU_XG_PAUSE_CTL_P1);
		} else if (is_qla8032(ha) || is_qla8042(ha)) {
			ql4_printk(KERN_INFO, ha, " %s: disabling pause transmit on port 0 & 1.\n",
				   __func__);
			qla4_83xx_disable_pause(ha);
		}
		goto mbox_exit;
	}

	/*
	 * Copy the mailbox out registers to the caller's mailbox in/out
	 * structure.
	 */
	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (i = 0; i < outCount; i++)
		mbx_sts[i] = ha->mbox_status[i];

	/* Set return status and error flags (if applicable). */
	switch (ha->mbox_status[0]) {
	case MBOX_STS_COMMAND_COMPLETE:
		status = QLA_SUCCESS;
		break;

	case MBOX_STS_INTERMEDIATE_COMPLETION:
		status = QLA_SUCCESS;
		break;

	case MBOX_STS_BUSY:
		DEBUG2( printk("scsi%ld: %s: Cmd = %08X, ISP BUSY\n",
			       ha->host_no, __func__, mbx_cmd[0]));
		ha->mailbox_timeout_count++;
		break;

	default:
		DEBUG2(printk("scsi%ld: %s: **** FAILED, cmd = %08X, "
			      "sts = %08X ****\n", ha->host_no, __func__,
			      mbx_cmd[0], mbx_sts[0]));
		break;
	}
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

mbox_exit:
	mutex_lock(&ha->mbox_sem);
	clear_bit(AF_MBOX_COMMAND, &ha->flags);
	mutex_unlock(&ha->mbox_sem);
	clear_bit(AF_MBOX_COMMAND_DONE, &ha->flags);

	return status;
}

/**
 * qla4xxx_get_minidump_template - Get the firmware template
 * @ha: Pointer to host adapter structure.
 * @phys_addr: dma address for template
 *
 * Obtain the minidump template from firmware during initialization
 * as it may not be available when minidump is desired.
 **/
int qla4xxx_get_minidump_template(struct scsi_qla_host *ha,
				  dma_addr_t phys_addr)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_MINIDUMP;
	mbox_cmd[1] = MINIDUMP_GET_TMPLT_SUBCOMMAND;
	mbox_cmd[2] = LSDW(phys_addr);
	mbox_cmd[3] = MSDW(phys_addr);
	mbox_cmd[4] = ha->fw_dump_tmplt_size;
	mbox_cmd[5] = 0;

	status = qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 2, &mbox_cmd[0],
					 &mbox_sts[0]);
	if (status != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "scsi%ld: %s: Cmd = %08X, mbx[0] = 0x%04x, mbx[1] = 0x%04x\n",
				  ha->host_no, __func__, mbox_cmd[0],
				  mbox_sts[0], mbox_sts[1]));
	}
	return status;
}

/**
 * qla4xxx_req_template_size - Get minidump template size from firmware.
 * @ha: Pointer to host adapter structure.
 **/
int qla4xxx_req_template_size(struct scsi_qla_host *ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_MINIDUMP;
	mbox_cmd[1] = MINIDUMP_GET_SIZE_SUBCOMMAND;

	status = qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 8, &mbox_cmd[0],
					 &mbox_sts[0]);
	if (status == QLA_SUCCESS) {
		ha->fw_dump_tmplt_size = mbox_sts[1];
		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "%s: sts[0]=0x%04x, template  size=0x%04x, size_cm_02=0x%04x, size_cm_04=0x%04x, size_cm_08=0x%04x, size_cm_10=0x%04x, size_cm_FF=0x%04x, version=0x%04x\n",
				  __func__, mbox_sts[0], mbox_sts[1],
				  mbox_sts[2], mbox_sts[3], mbox_sts[4],
				  mbox_sts[5], mbox_sts[6], mbox_sts[7]));
		if (ha->fw_dump_tmplt_size == 0)
			status = QLA_ERROR;
	} else {
		ql4_printk(KERN_WARNING, ha,
			   "%s: Error sts[0]=0x%04x, mbx[1]=0x%04x\n",
			   __func__, mbox_sts[0], mbox_sts[1]);
		status = QLA_ERROR;
	}

	return status;
}

void qla4xxx_mailbox_premature_completion(struct scsi_qla_host *ha)
{
	set_bit(AF_FW_RECOVERY, &ha->flags);
	ql4_printk(KERN_INFO, ha, "scsi%ld: %s: set FW RECOVERY!\n",
	    ha->host_no, __func__);

	if (test_bit(AF_MBOX_COMMAND, &ha->flags)) {
		if (test_bit(AF_MBOX_COMMAND_NOPOLL, &ha->flags)) {
			complete(&ha->mbx_intr_comp);
			ql4_printk(KERN_INFO, ha, "scsi%ld: %s: Due to fw "
			    "recovery, doing premature completion of "
			    "mbx cmd\n", ha->host_no, __func__);

		} else {
			set_bit(AF_MBOX_COMMAND_DONE, &ha->flags);
			ql4_printk(KERN_INFO, ha, "scsi%ld: %s: Due to fw "
			    "recovery, doing premature completion of "
			    "polling mbx cmd\n", ha->host_no, __func__);
		}
	}
}

static uint8_t
qla4xxx_set_ifcb(struct scsi_qla_host *ha, uint32_t *mbox_cmd,
		 uint32_t *mbox_sts, dma_addr_t init_fw_cb_dma)
{
	memset(mbox_cmd, 0, sizeof(mbox_cmd[0]) * MBOX_REG_COUNT);
	memset(mbox_sts, 0, sizeof(mbox_sts[0]) * MBOX_REG_COUNT);

	if (is_qla8022(ha))
		qla4_82xx_wr_32(ha, ha->nx_db_wr_ptr, 0);

	mbox_cmd[0] = MBOX_CMD_INITIALIZE_FIRMWARE;
	mbox_cmd[1] = 0;
	mbox_cmd[2] = LSDW(init_fw_cb_dma);
	mbox_cmd[3] = MSDW(init_fw_cb_dma);
	mbox_cmd[4] = sizeof(struct addr_ctrl_blk);
	mbox_cmd[5] = (IFCB_VER_MAX << 8) | IFCB_VER_MIN;

	if (qla4xxx_mailbox_command(ha, 6, 6, mbox_cmd, mbox_sts) !=
	    QLA_SUCCESS) {
		DEBUG2(printk(KERN_WARNING "scsi%ld: %s: "
			      "MBOX_CMD_INITIALIZE_FIRMWARE"
			      " failed w/ status %04X\n",
			      ha->host_no, __func__, mbox_sts[0]));
		return QLA_ERROR;
	}
	return QLA_SUCCESS;
}

uint8_t
qla4xxx_get_ifcb(struct scsi_qla_host *ha, uint32_t *mbox_cmd,
		 uint32_t *mbox_sts, dma_addr_t init_fw_cb_dma)
{
	memset(mbox_cmd, 0, sizeof(mbox_cmd[0]) * MBOX_REG_COUNT);
	memset(mbox_sts, 0, sizeof(mbox_sts[0]) * MBOX_REG_COUNT);
	mbox_cmd[0] = MBOX_CMD_GET_INIT_FW_CTRL_BLOCK;
	mbox_cmd[2] = LSDW(init_fw_cb_dma);
	mbox_cmd[3] = MSDW(init_fw_cb_dma);
	mbox_cmd[4] = sizeof(struct addr_ctrl_blk);

	if (qla4xxx_mailbox_command(ha, 5, 5, mbox_cmd, mbox_sts) !=
	    QLA_SUCCESS) {
		DEBUG2(printk(KERN_WARNING "scsi%ld: %s: "
			      "MBOX_CMD_GET_INIT_FW_CTRL_BLOCK"
			      " failed w/ status %04X\n",
			      ha->host_no, __func__, mbox_sts[0]));
		return QLA_ERROR;
	}
	return QLA_SUCCESS;
}

uint8_t qla4xxx_set_ipaddr_state(uint8_t fw_ipaddr_state)
{
	uint8_t ipaddr_state;

	switch (fw_ipaddr_state) {
	case IP_ADDRSTATE_UNCONFIGURED:
		ipaddr_state = ISCSI_IPDDRESS_STATE_UNCONFIGURED;
		break;
	case IP_ADDRSTATE_INVALID:
		ipaddr_state = ISCSI_IPDDRESS_STATE_INVALID;
		break;
	case IP_ADDRSTATE_ACQUIRING:
		ipaddr_state = ISCSI_IPDDRESS_STATE_ACQUIRING;
		break;
	case IP_ADDRSTATE_TENTATIVE:
		ipaddr_state = ISCSI_IPDDRESS_STATE_TENTATIVE;
		break;
	case IP_ADDRSTATE_DEPRICATED:
		ipaddr_state = ISCSI_IPDDRESS_STATE_DEPRECATED;
		break;
	case IP_ADDRSTATE_PREFERRED:
		ipaddr_state = ISCSI_IPDDRESS_STATE_VALID;
		break;
	case IP_ADDRSTATE_DISABLING:
		ipaddr_state = ISCSI_IPDDRESS_STATE_DISABLING;
		break;
	default:
		ipaddr_state = ISCSI_IPDDRESS_STATE_UNCONFIGURED;
	}
	return ipaddr_state;
}

static void
qla4xxx_update_local_ip(struct scsi_qla_host *ha,
			struct addr_ctrl_blk *init_fw_cb)
{
	ha->ip_config.tcp_options = le16_to_cpu(init_fw_cb->ipv4_tcp_opts);
	ha->ip_config.ipv4_options = le16_to_cpu(init_fw_cb->ipv4_ip_opts);
	ha->ip_config.ipv4_addr_state =
			qla4xxx_set_ipaddr_state(init_fw_cb->ipv4_addr_state);
	ha->ip_config.eth_mtu_size =
				le16_to_cpu(init_fw_cb->eth_mtu_size);
	ha->ip_config.ipv4_port = le16_to_cpu(init_fw_cb->ipv4_port);

	if (ha->acb_version == ACB_SUPPORTED) {
		ha->ip_config.ipv6_options = le16_to_cpu(init_fw_cb->ipv6_opts);
		ha->ip_config.ipv6_addl_options =
				le16_to_cpu(init_fw_cb->ipv6_addtl_opts);
		ha->ip_config.ipv6_tcp_options =
				le16_to_cpu(init_fw_cb->ipv6_tcp_opts);
	}

	/* Save IPv4 Address Info */
	memcpy(ha->ip_config.ip_address, init_fw_cb->ipv4_addr,
	       min(sizeof(ha->ip_config.ip_address),
		   sizeof(init_fw_cb->ipv4_addr)));
	memcpy(ha->ip_config.subnet_mask, init_fw_cb->ipv4_subnet,
	       min(sizeof(ha->ip_config.subnet_mask),
		   sizeof(init_fw_cb->ipv4_subnet)));
	memcpy(ha->ip_config.gateway, init_fw_cb->ipv4_gw_addr,
	       min(sizeof(ha->ip_config.gateway),
		   sizeof(init_fw_cb->ipv4_gw_addr)));

	ha->ip_config.ipv4_vlan_tag = be16_to_cpu(init_fw_cb->ipv4_vlan_tag);
	ha->ip_config.control = init_fw_cb->control;
	ha->ip_config.tcp_wsf = init_fw_cb->ipv4_tcp_wsf;
	ha->ip_config.ipv4_tos = init_fw_cb->ipv4_tos;
	ha->ip_config.ipv4_cache_id = init_fw_cb->ipv4_cacheid;
	ha->ip_config.ipv4_alt_cid_len = init_fw_cb->ipv4_dhcp_alt_cid_len;
	memcpy(ha->ip_config.ipv4_alt_cid, init_fw_cb->ipv4_dhcp_alt_cid,
	       min(sizeof(ha->ip_config.ipv4_alt_cid),
		   sizeof(init_fw_cb->ipv4_dhcp_alt_cid)));
	ha->ip_config.ipv4_vid_len = init_fw_cb->ipv4_dhcp_vid_len;
	memcpy(ha->ip_config.ipv4_vid, init_fw_cb->ipv4_dhcp_vid,
	       min(sizeof(ha->ip_config.ipv4_vid),
		   sizeof(init_fw_cb->ipv4_dhcp_vid)));
	ha->ip_config.ipv4_ttl = init_fw_cb->ipv4_ttl;
	ha->ip_config.def_timeout = le16_to_cpu(init_fw_cb->def_timeout);
	ha->ip_config.abort_timer = init_fw_cb->abort_timer;
	ha->ip_config.iscsi_options = le16_to_cpu(init_fw_cb->iscsi_opts);
	ha->ip_config.iscsi_max_pdu_size =
				le16_to_cpu(init_fw_cb->iscsi_max_pdu_size);
	ha->ip_config.iscsi_first_burst_len =
				le16_to_cpu(init_fw_cb->iscsi_fburst_len);
	ha->ip_config.iscsi_max_outstnd_r2t =
				le16_to_cpu(init_fw_cb->iscsi_max_outstnd_r2t);
	ha->ip_config.iscsi_max_burst_len =
				le16_to_cpu(init_fw_cb->iscsi_max_burst_len);
	memcpy(ha->ip_config.iscsi_name, init_fw_cb->iscsi_name,
	       min(sizeof(ha->ip_config.iscsi_name),
		   sizeof(init_fw_cb->iscsi_name)));

	if (is_ipv6_enabled(ha)) {
		/* Save IPv6 Address */
		ha->ip_config.ipv6_link_local_state =
		  qla4xxx_set_ipaddr_state(init_fw_cb->ipv6_lnk_lcl_addr_state);
		ha->ip_config.ipv6_addr0_state =
			qla4xxx_set_ipaddr_state(init_fw_cb->ipv6_addr0_state);
		ha->ip_config.ipv6_addr1_state =
			qla4xxx_set_ipaddr_state(init_fw_cb->ipv6_addr1_state);

		switch (le16_to_cpu(init_fw_cb->ipv6_dflt_rtr_state)) {
		case IPV6_RTRSTATE_UNKNOWN:
			ha->ip_config.ipv6_default_router_state =
						ISCSI_ROUTER_STATE_UNKNOWN;
			break;
		case IPV6_RTRSTATE_MANUAL:
			ha->ip_config.ipv6_default_router_state =
						ISCSI_ROUTER_STATE_MANUAL;
			break;
		case IPV6_RTRSTATE_ADVERTISED:
			ha->ip_config.ipv6_default_router_state =
						ISCSI_ROUTER_STATE_ADVERTISED;
			break;
		case IPV6_RTRSTATE_STALE:
			ha->ip_config.ipv6_default_router_state =
						ISCSI_ROUTER_STATE_STALE;
			break;
		default:
			ha->ip_config.ipv6_default_router_state =
						ISCSI_ROUTER_STATE_UNKNOWN;
		}

		ha->ip_config.ipv6_link_local_addr.in6_u.u6_addr8[0] = 0xFE;
		ha->ip_config.ipv6_link_local_addr.in6_u.u6_addr8[1] = 0x80;

		memcpy(&ha->ip_config.ipv6_link_local_addr.in6_u.u6_addr8[8],
		       init_fw_cb->ipv6_if_id,
		       min(sizeof(ha->ip_config.ipv6_link_local_addr)/2,
			   sizeof(init_fw_cb->ipv6_if_id)));
		memcpy(&ha->ip_config.ipv6_addr0, init_fw_cb->ipv6_addr0,
		       min(sizeof(ha->ip_config.ipv6_addr0),
			   sizeof(init_fw_cb->ipv6_addr0)));
		memcpy(&ha->ip_config.ipv6_addr1, init_fw_cb->ipv6_addr1,
		       min(sizeof(ha->ip_config.ipv6_addr1),
			   sizeof(init_fw_cb->ipv6_addr1)));
		memcpy(&ha->ip_config.ipv6_default_router_addr,
		       init_fw_cb->ipv6_dflt_rtr_addr,
		       min(sizeof(ha->ip_config.ipv6_default_router_addr),
			   sizeof(init_fw_cb->ipv6_dflt_rtr_addr)));
		ha->ip_config.ipv6_vlan_tag =
				be16_to_cpu(init_fw_cb->ipv6_vlan_tag);
		ha->ip_config.ipv6_port = le16_to_cpu(init_fw_cb->ipv6_port);
		ha->ip_config.ipv6_cache_id = init_fw_cb->ipv6_cache_id;
		ha->ip_config.ipv6_flow_lbl =
				le16_to_cpu(init_fw_cb->ipv6_flow_lbl);
		ha->ip_config.ipv6_traffic_class =
				init_fw_cb->ipv6_traffic_class;
		ha->ip_config.ipv6_hop_limit = init_fw_cb->ipv6_hop_limit;
		ha->ip_config.ipv6_nd_reach_time =
				le32_to_cpu(init_fw_cb->ipv6_nd_reach_time);
		ha->ip_config.ipv6_nd_rexmit_timer =
				le32_to_cpu(init_fw_cb->ipv6_nd_rexmit_timer);
		ha->ip_config.ipv6_nd_stale_timeout =
				le32_to_cpu(init_fw_cb->ipv6_nd_stale_timeout);
		ha->ip_config.ipv6_dup_addr_detect_count =
					init_fw_cb->ipv6_dup_addr_detect_count;
		ha->ip_config.ipv6_gw_advrt_mtu =
				le32_to_cpu(init_fw_cb->ipv6_gw_advrt_mtu);
		ha->ip_config.ipv6_tcp_wsf = init_fw_cb->ipv6_tcp_wsf;
	}
}

uint8_t
qla4xxx_update_local_ifcb(struct scsi_qla_host *ha,
			  uint32_t *mbox_cmd,
			  uint32_t *mbox_sts,
			  struct addr_ctrl_blk  *init_fw_cb,
			  dma_addr_t init_fw_cb_dma)
{
	if (qla4xxx_get_ifcb(ha, mbox_cmd, mbox_sts, init_fw_cb_dma)
	    != QLA_SUCCESS) {
		DEBUG2(printk(KERN_WARNING
			      "scsi%ld: %s: Failed to get init_fw_ctrl_blk\n",
			      ha->host_no, __func__));
		return QLA_ERROR;
	}

	DEBUG2(qla4xxx_dump_buffer(init_fw_cb, sizeof(struct addr_ctrl_blk)));

	/* Save some info in adapter structure. */
	ha->acb_version = init_fw_cb->acb_version;
	ha->firmware_options = le16_to_cpu(init_fw_cb->fw_options);
	ha->heartbeat_interval = init_fw_cb->hb_interval;
	memcpy(ha->name_string, init_fw_cb->iscsi_name,
		min(sizeof(ha->name_string),
		sizeof(init_fw_cb->iscsi_name)));
	ha->def_timeout = le16_to_cpu(init_fw_cb->def_timeout);
	/*memcpy(ha->alias, init_fw_cb->Alias,
	       min(sizeof(ha->alias), sizeof(init_fw_cb->Alias)));*/

	qla4xxx_update_local_ip(ha, init_fw_cb);

	return QLA_SUCCESS;
}

/**
 * qla4xxx_initialize_fw_cb - initializes firmware control block.
 * @ha: Pointer to host adapter structure.
 **/
int qla4xxx_initialize_fw_cb(struct scsi_qla_host * ha)
{
	struct addr_ctrl_blk *init_fw_cb;
	dma_addr_t init_fw_cb_dma;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status = QLA_ERROR;

	init_fw_cb = dma_alloc_coherent(&ha->pdev->dev,
					sizeof(struct addr_ctrl_blk),
					&init_fw_cb_dma, GFP_KERNEL);
	if (init_fw_cb == NULL) {
		DEBUG2(printk("scsi%ld: %s: Unable to alloc init_cb\n",
			      ha->host_no, __func__));
		goto exit_init_fw_cb_no_free;
	}
	memset(init_fw_cb, 0, sizeof(struct addr_ctrl_blk));

	/* Get Initialize Firmware Control Block. */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	if (qla4xxx_get_ifcb(ha, &mbox_cmd[0], &mbox_sts[0], init_fw_cb_dma) !=
	    QLA_SUCCESS) {
		dma_free_coherent(&ha->pdev->dev,
				  sizeof(struct addr_ctrl_blk),
				  init_fw_cb, init_fw_cb_dma);
		goto exit_init_fw_cb;
	}

	/* Initialize request and response queues. */
	qla4xxx_init_rings(ha);

	/* Fill in the request and response queue information. */
	init_fw_cb->rqq_consumer_idx = cpu_to_le16(ha->request_out);
	init_fw_cb->compq_producer_idx = cpu_to_le16(ha->response_in);
	init_fw_cb->rqq_len = __constant_cpu_to_le16(REQUEST_QUEUE_DEPTH);
	init_fw_cb->compq_len = __constant_cpu_to_le16(RESPONSE_QUEUE_DEPTH);
	init_fw_cb->rqq_addr_lo = cpu_to_le32(LSDW(ha->request_dma));
	init_fw_cb->rqq_addr_hi = cpu_to_le32(MSDW(ha->request_dma));
	init_fw_cb->compq_addr_lo = cpu_to_le32(LSDW(ha->response_dma));
	init_fw_cb->compq_addr_hi = cpu_to_le32(MSDW(ha->response_dma));
	init_fw_cb->shdwreg_addr_lo = cpu_to_le32(LSDW(ha->shadow_regs_dma));
	init_fw_cb->shdwreg_addr_hi = cpu_to_le32(MSDW(ha->shadow_regs_dma));

	/* Set up required options. */
	init_fw_cb->fw_options |=
		__constant_cpu_to_le16(FWOPT_SESSION_MODE |
				       FWOPT_INITIATOR_MODE);

	if (is_qla80XX(ha))
		init_fw_cb->fw_options |=
		    __constant_cpu_to_le16(FWOPT_ENABLE_CRBDB);

	init_fw_cb->fw_options &= __constant_cpu_to_le16(~FWOPT_TARGET_MODE);

	init_fw_cb->add_fw_options = 0;
	init_fw_cb->add_fw_options |=
			__constant_cpu_to_le16(ADFWOPT_SERIALIZE_TASK_MGMT);
	init_fw_cb->add_fw_options |=
			__constant_cpu_to_le16(ADFWOPT_AUTOCONN_DISABLE);

	if (qla4xxx_set_ifcb(ha, &mbox_cmd[0], &mbox_sts[0], init_fw_cb_dma)
		!= QLA_SUCCESS) {
		DEBUG2(printk(KERN_WARNING
			      "scsi%ld: %s: Failed to set init_fw_ctrl_blk\n",
			      ha->host_no, __func__));
		goto exit_init_fw_cb;
	}

	if (qla4xxx_update_local_ifcb(ha, &mbox_cmd[0], &mbox_sts[0],
		init_fw_cb, init_fw_cb_dma) != QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: Failed to update local ifcb\n",
				ha->host_no, __func__));
		goto exit_init_fw_cb;
	}
	status = QLA_SUCCESS;

exit_init_fw_cb:
	dma_free_coherent(&ha->pdev->dev, sizeof(struct addr_ctrl_blk),
				init_fw_cb, init_fw_cb_dma);
exit_init_fw_cb_no_free:
	return status;
}

/**
 * qla4xxx_get_dhcp_ip_address - gets HBA ip address via DHCP
 * @ha: Pointer to host adapter structure.
 **/
int qla4xxx_get_dhcp_ip_address(struct scsi_qla_host * ha)
{
	struct addr_ctrl_blk *init_fw_cb;
	dma_addr_t init_fw_cb_dma;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	init_fw_cb = dma_alloc_coherent(&ha->pdev->dev,
					sizeof(struct addr_ctrl_blk),
					&init_fw_cb_dma, GFP_KERNEL);
	if (init_fw_cb == NULL) {
		printk("scsi%ld: %s: Unable to alloc init_cb\n", ha->host_no,
		       __func__);
		return QLA_ERROR;
	}

	/* Get Initialize Firmware Control Block. */
	memset(init_fw_cb, 0, sizeof(struct addr_ctrl_blk));
	if (qla4xxx_get_ifcb(ha, &mbox_cmd[0], &mbox_sts[0], init_fw_cb_dma) !=
	    QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: Failed to get init_fw_ctrl_blk\n",
			      ha->host_no, __func__));
		dma_free_coherent(&ha->pdev->dev,
				  sizeof(struct addr_ctrl_blk),
				  init_fw_cb, init_fw_cb_dma);
		return QLA_ERROR;
	}

	/* Save IP Address. */
	qla4xxx_update_local_ip(ha, init_fw_cb);
	dma_free_coherent(&ha->pdev->dev, sizeof(struct addr_ctrl_blk),
				init_fw_cb, init_fw_cb_dma);

	return QLA_SUCCESS;
}

/**
 * qla4xxx_get_firmware_state - gets firmware state of HBA
 * @ha: Pointer to host adapter structure.
 **/
int qla4xxx_get_firmware_state(struct scsi_qla_host * ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	/* Get firmware version */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_GET_FW_STATE;

	if (qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 4, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: MBOX_CMD_GET_FW_STATE failed w/ "
			      "status %04X\n", ha->host_no, __func__,
			      mbox_sts[0]));
		return QLA_ERROR;
	}
	ha->firmware_state = mbox_sts[1];
	ha->board_id = mbox_sts[2];
	ha->addl_fw_state = mbox_sts[3];
	DEBUG2(printk("scsi%ld: %s firmware_state=0x%x\n",
		      ha->host_no, __func__, ha->firmware_state);)

	return QLA_SUCCESS;
}

/**
 * qla4xxx_get_firmware_status - retrieves firmware status
 * @ha: Pointer to host adapter structure.
 **/
int qla4xxx_get_firmware_status(struct scsi_qla_host * ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	/* Get firmware version */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_GET_FW_STATUS;

	if (qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 3, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: MBOX_CMD_GET_FW_STATUS failed w/ "
			      "status %04X\n", ha->host_no, __func__,
			      mbox_sts[0]));
		return QLA_ERROR;
	}

	/* High-water mark of IOCBs */
	ha->iocb_hiwat = mbox_sts[2];
	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "%s: firmware IOCBs available = %d\n", __func__,
			  ha->iocb_hiwat));

	if (ha->iocb_hiwat > IOCB_HIWAT_CUSHION)
		ha->iocb_hiwat -= IOCB_HIWAT_CUSHION;

	/* Ideally, we should not enter this code, as the # of firmware
	 * IOCBs is hard-coded in the firmware. We set a default
	 * iocb_hiwat here just in case */
	if (ha->iocb_hiwat == 0) {
		ha->iocb_hiwat = REQUEST_QUEUE_DEPTH / 4;
		DEBUG2(ql4_printk(KERN_WARNING, ha,
				  "%s: Setting IOCB's to = %d\n", __func__,
				  ha->iocb_hiwat));
	}

	return QLA_SUCCESS;
}

/**
 * qla4xxx_get_fwddb_entry - retrieves firmware ddb entry
 * @ha: Pointer to host adapter structure.
 * @fw_ddb_index: Firmware's device database index
 * @fw_ddb_entry: Pointer to firmware's device database entry structure
 * @num_valid_ddb_entries: Pointer to number of valid ddb entries
 * @next_ddb_index: Pointer to next valid device database index
 * @fw_ddb_device_state: Pointer to device state
 **/
int qla4xxx_get_fwddb_entry(struct scsi_qla_host *ha,
			    uint16_t fw_ddb_index,
			    struct dev_db_entry *fw_ddb_entry,
			    dma_addr_t fw_ddb_entry_dma,
			    uint32_t *num_valid_ddb_entries,
			    uint32_t *next_ddb_index,
			    uint32_t *fw_ddb_device_state,
			    uint32_t *conn_err_detail,
			    uint16_t *tcp_source_port_num,
			    uint16_t *connection_id)
{
	int status = QLA_ERROR;
	uint16_t options;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	/* Make sure the device index is valid */
	if (fw_ddb_index >= MAX_DDB_ENTRIES) {
		DEBUG2(printk("scsi%ld: %s: ddb [%d] out of range.\n",
			      ha->host_no, __func__, fw_ddb_index));
		goto exit_get_fwddb;
	}
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	if (fw_ddb_entry)
		memset(fw_ddb_entry, 0, sizeof(struct dev_db_entry));

	mbox_cmd[0] = MBOX_CMD_GET_DATABASE_ENTRY;
	mbox_cmd[1] = (uint32_t) fw_ddb_index;
	mbox_cmd[2] = LSDW(fw_ddb_entry_dma);
	mbox_cmd[3] = MSDW(fw_ddb_entry_dma);
	mbox_cmd[4] = sizeof(struct dev_db_entry);

	if (qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 7, &mbox_cmd[0], &mbox_sts[0]) ==
	    QLA_ERROR) {
		DEBUG2(printk("scsi%ld: %s: MBOX_CMD_GET_DATABASE_ENTRY failed"
			      " with status 0x%04X\n", ha->host_no, __func__,
			      mbox_sts[0]));
		goto exit_get_fwddb;
	}
	if (fw_ddb_index != mbox_sts[1]) {
		DEBUG2(printk("scsi%ld: %s: ddb mismatch [%d] != [%d].\n",
			      ha->host_no, __func__, fw_ddb_index,
			      mbox_sts[1]));
		goto exit_get_fwddb;
	}
	if (fw_ddb_entry) {
		options = le16_to_cpu(fw_ddb_entry->options);
		if (options & DDB_OPT_IPV6_DEVICE) {
			ql4_printk(KERN_INFO, ha, "%s: DDB[%d] MB0 %04x Tot %d "
				"Next %d State %04x ConnErr %08x %pI6 "
				":%04d \"%s\"\n", __func__, fw_ddb_index,
				mbox_sts[0], mbox_sts[2], mbox_sts[3],
				mbox_sts[4], mbox_sts[5],
				fw_ddb_entry->ip_addr,
				le16_to_cpu(fw_ddb_entry->port),
				fw_ddb_entry->iscsi_name);
		} else {
			ql4_printk(KERN_INFO, ha, "%s: DDB[%d] MB0 %04x Tot %d "
				"Next %d State %04x ConnErr %08x %pI4 "
				":%04d \"%s\"\n", __func__, fw_ddb_index,
				mbox_sts[0], mbox_sts[2], mbox_sts[3],
				mbox_sts[4], mbox_sts[5],
				fw_ddb_entry->ip_addr,
				le16_to_cpu(fw_ddb_entry->port),
				fw_ddb_entry->iscsi_name);
		}
	}
	if (num_valid_ddb_entries)
		*num_valid_ddb_entries = mbox_sts[2];
	if (next_ddb_index)
		*next_ddb_index = mbox_sts[3];
	if (fw_ddb_device_state)
		*fw_ddb_device_state = mbox_sts[4];

	/*
	 * RA: This mailbox has been changed to pass connection error and
	 * details.  Its true for ISP4010 as per Version E - Not sure when it
	 * was changed.	 Get the time2wait from the fw_dd_entry field :
	 * default_time2wait which we call it as minTime2Wait DEV_DB_ENTRY
	 * struct.
	 */
	if (conn_err_detail)
		*conn_err_detail = mbox_sts[5];
	if (tcp_source_port_num)
		*tcp_source_port_num = (uint16_t) (mbox_sts[6] >> 16);
	if (connection_id)
		*connection_id = (uint16_t) mbox_sts[6] & 0x00FF;
	status = QLA_SUCCESS;

exit_get_fwddb:
	return status;
}

int qla4xxx_conn_open(struct scsi_qla_host *ha, uint16_t fw_ddb_index)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_CONN_OPEN;
	mbox_cmd[1] = fw_ddb_index;

	status = qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 2, &mbox_cmd[0],
					 &mbox_sts[0]);
	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "%s: status = %d mbx0 = 0x%x mbx1 = 0x%x\n",
			  __func__, status, mbox_sts[0], mbox_sts[1]));
	return status;
}

/**
 * qla4xxx_set_fwddb_entry - sets a ddb entry.
 * @ha: Pointer to host adapter structure.
 * @fw_ddb_index: Firmware's device database index
 * @fw_ddb_entry_dma: dma address of ddb entry
 * @mbx_sts: mailbox 0 to be returned or NULL
 *
 * This routine initializes or updates the adapter's device database
 * entry for the specified device.
 **/
int qla4xxx_set_ddb_entry(struct scsi_qla_host * ha, uint16_t fw_ddb_index,
			  dma_addr_t fw_ddb_entry_dma, uint32_t *mbx_sts)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status;

	/* Do not wait for completion. The firmware will send us an
	 * ASTS_DATABASE_CHANGED (0x8014) to notify us of the login status.
	 */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_SET_DATABASE_ENTRY;
	mbox_cmd[1] = (uint32_t) fw_ddb_index;
	mbox_cmd[2] = LSDW(fw_ddb_entry_dma);
	mbox_cmd[3] = MSDW(fw_ddb_entry_dma);
	mbox_cmd[4] = sizeof(struct dev_db_entry);

	status = qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 5, &mbox_cmd[0],
					 &mbox_sts[0]);
	if (mbx_sts)
		*mbx_sts = mbox_sts[0];
	DEBUG2(printk("scsi%ld: %s: status=%d mbx0=0x%x mbx4=0x%x\n",
	    ha->host_no, __func__, status, mbox_sts[0], mbox_sts[4]);)

	return status;
}

int qla4xxx_session_logout_ddb(struct scsi_qla_host *ha,
			       struct ddb_entry *ddb_entry, int options)
{
	int status;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_CONN_CLOSE_SESS_LOGOUT;
	mbox_cmd[1] = ddb_entry->fw_ddb_index;
	mbox_cmd[3] = options;

	status = qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 2, &mbox_cmd[0],
					 &mbox_sts[0]);
	if (status != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "%s: MBOX_CMD_CONN_CLOSE_SESS_LOGOUT "
				  "failed sts %04X %04X", __func__,
				  mbox_sts[0], mbox_sts[1]));
		if ((mbox_sts[0] == MBOX_STS_COMMAND_ERROR) &&
		    (mbox_sts[1] == DDB_NOT_LOGGED_IN)) {
			set_bit(DDB_CONN_CLOSE_FAILURE, &ddb_entry->flags);
		}
	}

	return status;
}

/**
 * qla4xxx_get_crash_record - retrieves crash record.
 * @ha: Pointer to host adapter structure.
 *
 * This routine retrieves a crash record from the QLA4010 after an 8002h aen.
 **/
void qla4xxx_get_crash_record(struct scsi_qla_host * ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	struct crash_record *crash_record = NULL;
	dma_addr_t crash_record_dma = 0;
	uint32_t crash_record_size = 0;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_cmd));

	/* Get size of crash record. */
	mbox_cmd[0] = MBOX_CMD_GET_CRASH_RECORD;

	if (qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 5, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: ERROR: Unable to retrieve size!\n",
			      ha->host_no, __func__));
		goto exit_get_crash_record;
	}
	crash_record_size = mbox_sts[4];
	if (crash_record_size == 0) {
		DEBUG2(printk("scsi%ld: %s: ERROR: Crash record size is 0!\n",
			      ha->host_no, __func__));
		goto exit_get_crash_record;
	}

	/* Alloc Memory for Crash Record. */
	crash_record = dma_alloc_coherent(&ha->pdev->dev, crash_record_size,
					  &crash_record_dma, GFP_KERNEL);
	if (crash_record == NULL)
		goto exit_get_crash_record;

	/* Get Crash Record. */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_cmd));

	mbox_cmd[0] = MBOX_CMD_GET_CRASH_RECORD;
	mbox_cmd[2] = LSDW(crash_record_dma);
	mbox_cmd[3] = MSDW(crash_record_dma);
	mbox_cmd[4] = crash_record_size;

	if (qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 5, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS)
		goto exit_get_crash_record;

	/* Dump Crash Record. */

exit_get_crash_record:
	if (crash_record)
		dma_free_coherent(&ha->pdev->dev, crash_record_size,
				  crash_record, crash_record_dma);
}

/**
 * qla4xxx_get_conn_event_log - retrieves connection event log
 * @ha: Pointer to host adapter structure.
 **/
void qla4xxx_get_conn_event_log(struct scsi_qla_host * ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	struct conn_event_log_entry *event_log = NULL;
	dma_addr_t event_log_dma = 0;
	uint32_t event_log_size = 0;
	uint32_t num_valid_entries;
	uint32_t      oldest_entry = 0;
	uint32_t	max_event_log_entries;
	uint8_t		i;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_cmd));

	/* Get size of crash record. */
	mbox_cmd[0] = MBOX_CMD_GET_CONN_EVENT_LOG;

	if (qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 5, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS)
		goto exit_get_event_log;

	event_log_size = mbox_sts[4];
	if (event_log_size == 0)
		goto exit_get_event_log;

	/* Alloc Memory for Crash Record. */
	event_log = dma_alloc_coherent(&ha->pdev->dev, event_log_size,
				       &event_log_dma, GFP_KERNEL);
	if (event_log == NULL)
		goto exit_get_event_log;

	/* Get Crash Record. */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_cmd));

	mbox_cmd[0] = MBOX_CMD_GET_CONN_EVENT_LOG;
	mbox_cmd[2] = LSDW(event_log_dma);
	mbox_cmd[3] = MSDW(event_log_dma);

	if (qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 5, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: ERROR: Unable to retrieve event "
			      "log!\n", ha->host_no, __func__));
		goto exit_get_event_log;
	}

	/* Dump Event Log. */
	num_valid_entries = mbox_sts[1];

	max_event_log_entries = event_log_size /
		sizeof(struct conn_event_log_entry);

	if (num_valid_entries > max_event_log_entries)
		oldest_entry = num_valid_entries % max_event_log_entries;

	DEBUG3(printk("scsi%ld: Connection Event Log Dump (%d entries):\n",
		      ha->host_no, num_valid_entries));

	if (ql4xextended_error_logging == 3) {
		if (oldest_entry == 0) {
			/* Circular Buffer has not wrapped around */
			for (i=0; i < num_valid_entries; i++) {
				qla4xxx_dump_buffer((uint8_t *)event_log+
						    (i*sizeof(*event_log)),
						    sizeof(*event_log));
			}
		}
		else {
			/* Circular Buffer has wrapped around -
			 * display accordingly*/
			for (i=oldest_entry; i < max_event_log_entries; i++) {
				qla4xxx_dump_buffer((uint8_t *)event_log+
						    (i*sizeof(*event_log)),
						    sizeof(*event_log));
			}
			for (i=0; i < oldest_entry; i++) {
				qla4xxx_dump_buffer((uint8_t *)event_log+
						    (i*sizeof(*event_log)),
						    sizeof(*event_log));
			}
		}
	}

exit_get_event_log:
	if (event_log)
		dma_free_coherent(&ha->pdev->dev, event_log_size, event_log,
				  event_log_dma);
}

/**
 * qla4xxx_abort_task - issues Abort Task
 * @ha: Pointer to host adapter structure.
 * @srb: Pointer to srb entry
 *
 * This routine performs a LUN RESET on the specified target/lun.
 * The caller must ensure that the ddb_entry and lun_entry pointers
 * are valid before calling this routine.
 **/
int qla4xxx_abort_task(struct scsi_qla_host *ha, struct srb *srb)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	struct scsi_cmnd *cmd = srb->cmd;
	int status = QLA_SUCCESS;
	unsigned long flags = 0;
	uint32_t index;

	/*
	 * Send abort task command to ISP, so that the ISP will return
	 * request with ABORT status
	 */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	spin_lock_irqsave(&ha->hardware_lock, flags);
	index = (unsigned long)(unsigned char *)cmd->host_scribble;
	spin_unlock_irqrestore(&ha->hardware_lock, flags);

	/* Firmware already posted completion on response queue */
	if (index == MAX_SRBS)
		return status;

	mbox_cmd[0] = MBOX_CMD_ABORT_TASK;
	mbox_cmd[1] = srb->ddb->fw_ddb_index;
	mbox_cmd[2] = index;
	/* Immediate Command Enable */
	mbox_cmd[5] = 0x01;

	qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 5, &mbox_cmd[0],
	    &mbox_sts[0]);
	if (mbox_sts[0] != MBOX_STS_COMMAND_COMPLETE) {
		status = QLA_ERROR;

		DEBUG2(printk(KERN_WARNING "scsi%ld:%d:%d: abort task FAILED: "
		    "mbx0=%04X, mb1=%04X, mb2=%04X, mb3=%04X, mb4=%04X\n",
		    ha->host_no, cmd->device->id, cmd->device->lun, mbox_sts[0],
		    mbox_sts[1], mbox_sts[2], mbox_sts[3], mbox_sts[4]));
	}

	return status;
}

/**
 * qla4xxx_reset_lun - issues LUN Reset
 * @ha: Pointer to host adapter structure.
 * @ddb_entry: Pointer to device database entry
 * @lun: lun number
 *
 * This routine performs a LUN RESET on the specified target/lun.
 * The caller must ensure that the ddb_entry and lun_entry pointers
 * are valid before calling this routine.
 **/
int qla4xxx_reset_lun(struct scsi_qla_host * ha, struct ddb_entry * ddb_entry,
		      int lun)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	uint32_t scsi_lun[2];
	int status = QLA_SUCCESS;

	DEBUG2(printk("scsi%ld:%d:%d: lun reset issued\n", ha->host_no,
		      ddb_entry->fw_ddb_index, lun));

	/*
	 * Send lun reset command to ISP, so that the ISP will return all
	 * outstanding requests with RESET status
	 */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	int_to_scsilun(lun, (struct scsi_lun *) scsi_lun);

	mbox_cmd[0] = MBOX_CMD_LUN_RESET;
	mbox_cmd[1] = ddb_entry->fw_ddb_index;
	/* FW expects LUN bytes 0-3 in Incoming Mailbox 2
	 * (LUN byte 0 is LSByte, byte 3 is MSByte) */
	mbox_cmd[2] = cpu_to_le32(scsi_lun[0]);
	/* FW expects LUN bytes 4-7 in Incoming Mailbox 3
	 * (LUN byte 4 is LSByte, byte 7 is MSByte) */
	mbox_cmd[3] = cpu_to_le32(scsi_lun[1]);
	mbox_cmd[5] = 0x01;	/* Immediate Command Enable */

	qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 1, &mbox_cmd[0], &mbox_sts[0]);
	if (mbox_sts[0] != MBOX_STS_COMMAND_COMPLETE &&
	    mbox_sts[0] != MBOX_STS_COMMAND_ERROR)
		status = QLA_ERROR;

	return status;
}

/**
 * qla4xxx_reset_target - issues target Reset
 * @ha: Pointer to host adapter structure.
 * @db_entry: Pointer to device database entry
 * @un_entry: Pointer to lun entry structure
 *
 * This routine performs a TARGET RESET on the specified target.
 * The caller must ensure that the ddb_entry pointers
 * are valid before calling this routine.
 **/
int qla4xxx_reset_target(struct scsi_qla_host *ha,
			 struct ddb_entry *ddb_entry)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status = QLA_SUCCESS;

	DEBUG2(printk("scsi%ld:%d: target reset issued\n", ha->host_no,
		      ddb_entry->fw_ddb_index));

	/*
	 * Send target reset command to ISP, so that the ISP will return all
	 * outstanding requests with RESET status
	 */
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_TARGET_WARM_RESET;
	mbox_cmd[1] = ddb_entry->fw_ddb_index;
	mbox_cmd[5] = 0x01;	/* Immediate Command Enable */

	qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 1, &mbox_cmd[0],
				&mbox_sts[0]);
	if (mbox_sts[0] != MBOX_STS_COMMAND_COMPLETE &&
	    mbox_sts[0] != MBOX_STS_COMMAND_ERROR)
		status = QLA_ERROR;

	return status;
}

int qla4xxx_get_flash(struct scsi_qla_host * ha, dma_addr_t dma_addr,
		      uint32_t offset, uint32_t len)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_READ_FLASH;
	mbox_cmd[1] = LSDW(dma_addr);
	mbox_cmd[2] = MSDW(dma_addr);
	mbox_cmd[3] = offset;
	mbox_cmd[4] = len;

	if (qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 2, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: MBOX_CMD_READ_FLASH, failed w/ "
		    "status %04X %04X, offset %08x, len %08x\n", ha->host_no,
		    __func__, mbox_sts[0], mbox_sts[1], offset, len));
		return QLA_ERROR;
	}
	return QLA_SUCCESS;
}

/**
 * qla4xxx_about_firmware - gets FW, iscsi draft and boot loader version
 * @ha: Pointer to host adapter structure.
 *
 * Retrieves the FW version, iSCSI draft version & bootloader version of HBA.
 * Mailboxes 2 & 3 may hold an address for data. Make sure that we write 0 to
 * those mailboxes, if unused.
 **/
int qla4xxx_about_firmware(struct scsi_qla_host *ha)
{
	struct about_fw_info *about_fw = NULL;
	dma_addr_t about_fw_dma;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status = QLA_ERROR;

	about_fw = dma_alloc_coherent(&ha->pdev->dev,
				      sizeof(struct about_fw_info),
				      &about_fw_dma, GFP_KERNEL);
	if (!about_fw) {
		DEBUG2(ql4_printk(KERN_ERR, ha, "%s: Unable to alloc memory "
				  "for about_fw\n", __func__));
		return status;
	}

	memset(about_fw, 0, sizeof(struct about_fw_info));
	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_ABOUT_FW;
	mbox_cmd[2] = LSDW(about_fw_dma);
	mbox_cmd[3] = MSDW(about_fw_dma);
	mbox_cmd[4] = sizeof(struct about_fw_info);

	status = qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, MBOX_REG_COUNT,
					 &mbox_cmd[0], &mbox_sts[0]);
	if (status != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_WARNING, ha, "%s: MBOX_CMD_ABOUT_FW "
				  "failed w/ status %04X\n", __func__,
				  mbox_sts[0]));
		goto exit_about_fw;
	}

	/* Save version information. */
	ha->fw_info.fw_major = le16_to_cpu(about_fw->fw_major);
	ha->fw_info.fw_minor = le16_to_cpu(about_fw->fw_minor);
	ha->fw_info.fw_patch = le16_to_cpu(about_fw->fw_patch);
	ha->fw_info.fw_build = le16_to_cpu(about_fw->fw_build);
	memcpy(ha->fw_info.fw_build_date, about_fw->fw_build_date,
	       sizeof(about_fw->fw_build_date));
	memcpy(ha->fw_info.fw_build_time, about_fw->fw_build_time,
	       sizeof(about_fw->fw_build_time));
	strcpy((char *)ha->fw_info.fw_build_user,
	       skip_spaces((char *)about_fw->fw_build_user));
	ha->fw_info.fw_load_source = le16_to_cpu(about_fw->fw_load_source);
	ha->fw_info.iscsi_major = le16_to_cpu(about_fw->iscsi_major);
	ha->fw_info.iscsi_minor = le16_to_cpu(about_fw->iscsi_minor);
	ha->fw_info.bootload_major = le16_to_cpu(about_fw->bootload_major);
	ha->fw_info.bootload_minor = le16_to_cpu(about_fw->bootload_minor);
	ha->fw_info.bootload_patch = le16_to_cpu(about_fw->bootload_patch);
	ha->fw_info.bootload_build = le16_to_cpu(about_fw->bootload_build);
	strcpy((char *)ha->fw_info.extended_timestamp,
	       skip_spaces((char *)about_fw->extended_timestamp));

	ha->fw_uptime_secs = le32_to_cpu(mbox_sts[5]);
	ha->fw_uptime_msecs = le32_to_cpu(mbox_sts[6]);
	status = QLA_SUCCESS;

exit_about_fw:
	dma_free_coherent(&ha->pdev->dev, sizeof(struct about_fw_info),
			  about_fw, about_fw_dma);
	return status;
}

int qla4xxx_get_default_ddb(struct scsi_qla_host *ha, uint32_t options,
			    dma_addr_t dma_addr)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_GET_DATABASE_ENTRY_DEFAULTS;
	mbox_cmd[1] = options;
	mbox_cmd[2] = LSDW(dma_addr);
	mbox_cmd[3] = MSDW(dma_addr);

	if (qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 1, &mbox_cmd[0], &mbox_sts[0]) !=
	    QLA_SUCCESS) {
		DEBUG2(printk("scsi%ld: %s: failed status %04X\n",
		     ha->host_no, __func__, mbox_sts[0]));
		return QLA_ERROR;
	}
	return QLA_SUCCESS;
}

int qla4xxx_req_ddb_entry(struct scsi_qla_host *ha, uint32_t ddb_index,
			  uint32_t *mbx_sts)
{
	int status;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_REQUEST_DATABASE_ENTRY;
	mbox_cmd[1] = ddb_index;

	status = qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 1, &mbox_cmd[0],
					 &mbox_sts[0]);
	if (status != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_ERR, ha, "%s: failed status %04X\n",
				   __func__, mbox_sts[0]));
	}

	*mbx_sts = mbox_sts[0];
	return status;
}

int qla4xxx_clear_ddb_entry(struct scsi_qla_host *ha, uint32_t ddb_index)
{
	int status;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_CLEAR_DATABASE_ENTRY;
	mbox_cmd[1] = ddb_index;

	status = qla4xxx_mailbox_command(ha, 2, 1, &mbox_cmd[0],
					 &mbox_sts[0]);
	if (status != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_ERR, ha, "%s: failed status %04X\n",
				   __func__, mbox_sts[0]));
	}

	return status;
}

int qla4xxx_set_flash(struct scsi_qla_host *ha, dma_addr_t dma_addr,
		      uint32_t offset, uint32_t length, uint32_t options)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status = QLA_SUCCESS;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_WRITE_FLASH;
	mbox_cmd[1] = LSDW(dma_addr);
	mbox_cmd[2] = MSDW(dma_addr);
	mbox_cmd[3] = offset;
	mbox_cmd[4] = length;
	mbox_cmd[5] = options;

	status = qla4xxx_mailbox_command(ha, 6, 2, &mbox_cmd[0], &mbox_sts[0]);
	if (status != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_WARNING, ha, "%s: MBOX_CMD_WRITE_FLASH "
				  "failed w/ status %04X, mbx1 %04X\n",
				  __func__, mbox_sts[0], mbox_sts[1]));
	}
	return status;
}

int qla4xxx_bootdb_by_index(struct scsi_qla_host *ha,
			    struct dev_db_entry *fw_ddb_entry,
			    dma_addr_t fw_ddb_entry_dma, uint16_t ddb_index)
{
	uint32_t dev_db_start_offset = FLASH_OFFSET_DB_INFO;
	uint32_t dev_db_end_offset;
	int status = QLA_ERROR;

	memset(fw_ddb_entry, 0, sizeof(*fw_ddb_entry));

	dev_db_start_offset += (ddb_index * sizeof(*fw_ddb_entry));
	dev_db_end_offset = FLASH_OFFSET_DB_END;

	if (dev_db_start_offset > dev_db_end_offset) {
		DEBUG2(ql4_printk(KERN_ERR, ha,
				  "%s:Invalid DDB index %d", __func__,
				  ddb_index));
		goto exit_bootdb_failed;
	}

	if (qla4xxx_get_flash(ha, fw_ddb_entry_dma, dev_db_start_offset,
			      sizeof(*fw_ddb_entry)) != QLA_SUCCESS) {
		ql4_printk(KERN_ERR, ha, "scsi%ld: %s: Get Flash"
			   "failed\n", ha->host_no, __func__);
		goto exit_bootdb_failed;
	}

	if (fw_ddb_entry->cookie == DDB_VALID_COOKIE)
		status = QLA_SUCCESS;

exit_bootdb_failed:
	return status;
}

int qla4xxx_flashdb_by_index(struct scsi_qla_host *ha,
			     struct dev_db_entry *fw_ddb_entry,
			     dma_addr_t fw_ddb_entry_dma, uint16_t ddb_index)
{
	uint32_t dev_db_start_offset;
	uint32_t dev_db_end_offset;
	int status = QLA_ERROR;

	memset(fw_ddb_entry, 0, sizeof(*fw_ddb_entry));

	if (is_qla40XX(ha)) {
		dev_db_start_offset = FLASH_OFFSET_DB_INFO;
		dev_db_end_offset = FLASH_OFFSET_DB_END;
	} else {
		dev_db_start_offset = FLASH_RAW_ACCESS_ADDR +
				      (ha->hw.flt_region_ddb << 2);
		/* flt_ddb_size is DDB table size for both ports
		 * so divide it by 2 to calculate the offset for second port
		 */
		if (ha->port_num == 1)
			dev_db_start_offset += (ha->hw.flt_ddb_size / 2);

		dev_db_end_offset = dev_db_start_offset +
				    (ha->hw.flt_ddb_size / 2);
	}

	dev_db_start_offset += (ddb_index * sizeof(*fw_ddb_entry));

	if (dev_db_start_offset > dev_db_end_offset) {
		DEBUG2(ql4_printk(KERN_ERR, ha,
				  "%s:Invalid DDB index %d", __func__,
				  ddb_index));
		goto exit_fdb_failed;
	}

	if (qla4xxx_get_flash(ha, fw_ddb_entry_dma, dev_db_start_offset,
			      sizeof(*fw_ddb_entry)) != QLA_SUCCESS) {
		ql4_printk(KERN_ERR, ha, "scsi%ld: %s: Get Flash failed\n",
			   ha->host_no, __func__);
		goto exit_fdb_failed;
	}

	if (fw_ddb_entry->cookie == DDB_VALID_COOKIE)
		status = QLA_SUCCESS;

exit_fdb_failed:
	return status;
}

int qla4xxx_get_chap(struct scsi_qla_host *ha, char *username, char *password,
		     uint16_t idx)
{
	int ret = 0;
	int rval = QLA_ERROR;
	uint32_t offset = 0, chap_size;
	struct ql4_chap_table *chap_table;
	dma_addr_t chap_dma;

	chap_table = dma_pool_alloc(ha->chap_dma_pool, GFP_KERNEL, &chap_dma);
	if (chap_table == NULL)
		return -ENOMEM;

	chap_size = sizeof(struct ql4_chap_table);
	memset(chap_table, 0, chap_size);

	if (is_qla40XX(ha))
		offset = FLASH_CHAP_OFFSET | (idx * chap_size);
	else {
		offset = FLASH_RAW_ACCESS_ADDR + (ha->hw.flt_region_chap << 2);
		/* flt_chap_size is CHAP table size for both ports
		 * so divide it by 2 to calculate the offset for second port
		 */
		if (ha->port_num == 1)
			offset += (ha->hw.flt_chap_size / 2);
		offset += (idx * chap_size);
	}

	rval = qla4xxx_get_flash(ha, chap_dma, offset, chap_size);
	if (rval != QLA_SUCCESS) {
		ret = -EINVAL;
		goto exit_get_chap;
	}

	DEBUG2(ql4_printk(KERN_INFO, ha, "Chap Cookie: x%x\n",
		__le16_to_cpu(chap_table->cookie)));

	if (__le16_to_cpu(chap_table->cookie) != CHAP_VALID_COOKIE) {
		ql4_printk(KERN_ERR, ha, "No valid chap entry found\n");
		goto exit_get_chap;
	}

	strncpy(password, chap_table->secret, QL4_CHAP_MAX_SECRET_LEN);
	strncpy(username, chap_table->name, QL4_CHAP_MAX_NAME_LEN);
	chap_table->cookie = __constant_cpu_to_le16(CHAP_VALID_COOKIE);

exit_get_chap:
	dma_pool_free(ha->chap_dma_pool, chap_table, chap_dma);
	return ret;
}

/**
 * qla4xxx_set_chap - Make a chap entry at the given index
 * @ha: pointer to adapter structure
 * @username: CHAP username to set
 * @password: CHAP password to set
 * @idx: CHAP index at which to make the entry
 * @bidi: type of chap entry (chap_in or chap_out)
 *
 * Create chap entry at the given index with the information provided.
 *
 * Note: Caller should acquire the chap lock before getting here.
 **/
int qla4xxx_set_chap(struct scsi_qla_host *ha, char *username, char *password,
		     uint16_t idx, int bidi)
{
	int ret = 0;
	int rval = QLA_ERROR;
	uint32_t offset = 0;
	struct ql4_chap_table *chap_table;
	uint32_t chap_size = 0;
	dma_addr_t chap_dma;

	chap_table = dma_pool_alloc(ha->chap_dma_pool, GFP_KERNEL, &chap_dma);
	if (chap_table == NULL) {
		ret =  -ENOMEM;
		goto exit_set_chap;
	}

	memset(chap_table, 0, sizeof(struct ql4_chap_table));
	if (bidi)
		chap_table->flags |= BIT_6; /* peer */
	else
		chap_table->flags |= BIT_7; /* local */
	chap_table->secret_len = strlen(password);
	strncpy(chap_table->secret, password, MAX_CHAP_SECRET_LEN);
	strncpy(chap_table->name, username, MAX_CHAP_NAME_LEN);
	chap_table->cookie = __constant_cpu_to_le16(CHAP_VALID_COOKIE);

	if (is_qla40XX(ha)) {
		chap_size = MAX_CHAP_ENTRIES_40XX * sizeof(*chap_table);
		offset = FLASH_CHAP_OFFSET;
	} else { /* Single region contains CHAP info for both ports which is
		  * divided into half for each port.
		  */
		chap_size = ha->hw.flt_chap_size / 2;
		offset = FLASH_RAW_ACCESS_ADDR + (ha->hw.flt_region_chap << 2);
		if (ha->port_num == 1)
			offset += chap_size;
	}

	offset += (idx * sizeof(struct ql4_chap_table));
	rval = qla4xxx_set_flash(ha, chap_dma, offset,
				sizeof(struct ql4_chap_table),
				FLASH_OPT_RMW_COMMIT);

	if (rval == QLA_SUCCESS && ha->chap_list) {
		/* Update ha chap_list cache */
		memcpy((struct ql4_chap_table *)ha->chap_list + idx,
		       chap_table, sizeof(struct ql4_chap_table));
	}
	dma_pool_free(ha->chap_dma_pool, chap_table, chap_dma);
	if (rval != QLA_SUCCESS)
		ret =  -EINVAL;

exit_set_chap:
	return ret;
}


int qla4xxx_get_uni_chap_at_index(struct scsi_qla_host *ha, char *username,
				  char *password, uint16_t chap_index)
{
	int rval = QLA_ERROR;
	struct ql4_chap_table *chap_table = NULL;
	int max_chap_entries;

	if (!ha->chap_list) {
		ql4_printk(KERN_ERR, ha, "Do not have CHAP table cache\n");
		rval = QLA_ERROR;
		goto exit_uni_chap;
	}

	if (!username || !password) {
		ql4_printk(KERN_ERR, ha, "No memory for username & secret\n");
		rval = QLA_ERROR;
		goto exit_uni_chap;
	}

	if (is_qla80XX(ha))
		max_chap_entries = (ha->hw.flt_chap_size / 2) /
				   sizeof(struct ql4_chap_table);
	else
		max_chap_entries = MAX_CHAP_ENTRIES_40XX;

	if (chap_index > max_chap_entries) {
		ql4_printk(KERN_ERR, ha, "Invalid Chap index\n");
		rval = QLA_ERROR;
		goto exit_uni_chap;
	}

	mutex_lock(&ha->chap_sem);
	chap_table = (struct ql4_chap_table *)ha->chap_list + chap_index;
	if (chap_table->cookie != __constant_cpu_to_le16(CHAP_VALID_COOKIE)) {
		rval = QLA_ERROR;
		goto exit_unlock_uni_chap;
	}

	if (!(chap_table->flags & BIT_7)) {
		ql4_printk(KERN_ERR, ha, "Unidirectional entry not set\n");
		rval = QLA_ERROR;
		goto exit_unlock_uni_chap;
	}

	strncpy(password, chap_table->secret, MAX_CHAP_SECRET_LEN);
	strncpy(username, chap_table->name, MAX_CHAP_NAME_LEN);

	rval = QLA_SUCCESS;

exit_unlock_uni_chap:
	mutex_unlock(&ha->chap_sem);
exit_uni_chap:
	return rval;
}

/**
 * qla4xxx_get_chap_index - Get chap index given username and secret
 * @ha: pointer to adapter structure
 * @username: CHAP username to be searched
 * @password: CHAP password to be searched
 * @bidi: Is this a BIDI CHAP
 * @chap_index: CHAP index to be returned
 *
 * Match the username and password in the chap_list, return the index if a
 * match is found. If a match is not found then add the entry in FLASH and
 * return the index at which entry is written in the FLASH.
 **/
int qla4xxx_get_chap_index(struct scsi_qla_host *ha, char *username,
			   char *password, int bidi, uint16_t *chap_index)
{
	int i, rval;
	int free_index = -1;
	int found_index = 0;
	int max_chap_entries = 0;
	struct ql4_chap_table *chap_table;

	if (is_qla80XX(ha))
		max_chap_entries = (ha->hw.flt_chap_size / 2) /
						sizeof(struct ql4_chap_table);
	else
		max_chap_entries = MAX_CHAP_ENTRIES_40XX;

	if (!ha->chap_list) {
		ql4_printk(KERN_ERR, ha, "Do not have CHAP table cache\n");
		return QLA_ERROR;
	}

	if (!username || !password) {
		ql4_printk(KERN_ERR, ha, "Do not have username and psw\n");
		return QLA_ERROR;
	}

	mutex_lock(&ha->chap_sem);
	for (i = 0; i < max_chap_entries; i++) {
		chap_table = (struct ql4_chap_table *)ha->chap_list + i;
		if (chap_table->cookie !=
		    __constant_cpu_to_le16(CHAP_VALID_COOKIE)) {
			if (i > MAX_RESRV_CHAP_IDX && free_index == -1)
				free_index = i;
			continue;
		}
		if (bidi) {
			if (chap_table->flags & BIT_7)
				continue;
		} else {
			if (chap_table->flags & BIT_6)
				continue;
		}
		if (!strncmp(chap_table->secret, password,
			     MAX_CHAP_SECRET_LEN) &&
		    !strncmp(chap_table->name, username,
			     MAX_CHAP_NAME_LEN)) {
			*chap_index = i;
			found_index = 1;
			break;
		}
	}

	/* If chap entry is not present and a free index is available then
	 * write the entry in flash
	 */
	if (!found_index && free_index != -1) {
		rval = qla4xxx_set_chap(ha, username, password,
					free_index, bidi);
		if (!rval) {
			*chap_index = free_index;
			found_index = 1;
		}
	}

	mutex_unlock(&ha->chap_sem);

	if (found_index)
		return QLA_SUCCESS;
	return QLA_ERROR;
}

int qla4xxx_conn_close_sess_logout(struct scsi_qla_host *ha,
				   uint16_t fw_ddb_index,
				   uint16_t connection_id,
				   uint16_t option)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status = QLA_SUCCESS;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_CONN_CLOSE_SESS_LOGOUT;
	mbox_cmd[1] = fw_ddb_index;
	mbox_cmd[2] = connection_id;
	mbox_cmd[3] = option;

	status = qla4xxx_mailbox_command(ha, 4, 2, &mbox_cmd[0], &mbox_sts[0]);
	if (status != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_WARNING, ha, "%s: MBOX_CMD_CONN_CLOSE "
				  "option %04x failed w/ status %04X %04X\n",
				  __func__, option, mbox_sts[0], mbox_sts[1]));
	}
	return status;
}

/**
 * qla4_84xx_extend_idc_tmo - Extend IDC Timeout.
 * @ha: Pointer to host adapter structure.
 * @ext_tmo: idc timeout value
 *
 * Requests firmware to extend the idc timeout value.
 **/
static int qla4_84xx_extend_idc_tmo(struct scsi_qla_host *ha, uint32_t ext_tmo)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	ext_tmo &= 0xf;

	mbox_cmd[0] = MBOX_CMD_IDC_TIME_EXTEND;
	mbox_cmd[1] = ((ha->idc_info.request_desc & 0xfffff0ff) |
		       (ext_tmo << 8));		/* new timeout */
	mbox_cmd[2] = ha->idc_info.info1;
	mbox_cmd[3] = ha->idc_info.info2;
	mbox_cmd[4] = ha->idc_info.info3;

	status = qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, MBOX_REG_COUNT,
					 mbox_cmd, mbox_sts);
	if (status != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "scsi%ld: %s: failed status %04X\n",
				  ha->host_no, __func__, mbox_sts[0]));
		return QLA_ERROR;
	} else {
		ql4_printk(KERN_INFO, ha, "%s: IDC timeout extended by %d secs\n",
			   __func__, ext_tmo);
	}

	return QLA_SUCCESS;
}

int qla4xxx_disable_acb(struct scsi_qla_host *ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status = QLA_SUCCESS;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_DISABLE_ACB;

	status = qla4xxx_mailbox_command(ha, 8, 5, &mbox_cmd[0], &mbox_sts[0]);
	if (status != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_WARNING, ha, "%s: MBOX_CMD_DISABLE_ACB "
				  "failed w/ status %04X %04X %04X", __func__,
				  mbox_sts[0], mbox_sts[1], mbox_sts[2]));
	} else {
		if (is_qla8042(ha) &&
		    (mbox_sts[0] != MBOX_STS_COMMAND_COMPLETE)) {
			/*
			 * Disable ACB mailbox command takes time to complete
			 * based on the total number of targets connected.
			 * For 512 targets, it took approximately 5 secs to
			 * complete. Setting the timeout value to 8, with the 3
			 * secs buffer.
			 */
			qla4_84xx_extend_idc_tmo(ha, IDC_EXTEND_TOV);
			if (!wait_for_completion_timeout(&ha->disable_acb_comp,
							 IDC_EXTEND_TOV * HZ)) {
				ql4_printk(KERN_WARNING, ha, "%s: Disable ACB Completion not received\n",
					   __func__);
			}
		}
	}
	return status;
}

int qla4xxx_get_acb(struct scsi_qla_host *ha, dma_addr_t acb_dma,
		    uint32_t acb_type, uint32_t len)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status = QLA_SUCCESS;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_GET_ACB;
	mbox_cmd[1] = acb_type;
	mbox_cmd[2] = LSDW(acb_dma);
	mbox_cmd[3] = MSDW(acb_dma);
	mbox_cmd[4] = len;

	status = qla4xxx_mailbox_command(ha, 5, 5, &mbox_cmd[0], &mbox_sts[0]);
	if (status != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_WARNING, ha, "%s: MBOX_CMD_GET_ACB "
				  "failed w/ status %04X\n", __func__,
				  mbox_sts[0]));
	}
	return status;
}

int qla4xxx_set_acb(struct scsi_qla_host *ha, uint32_t *mbox_cmd,
		    uint32_t *mbox_sts, dma_addr_t acb_dma)
{
	int status = QLA_SUCCESS;

	memset(mbox_cmd, 0, sizeof(mbox_cmd[0]) * MBOX_REG_COUNT);
	memset(mbox_sts, 0, sizeof(mbox_sts[0]) * MBOX_REG_COUNT);
	mbox_cmd[0] = MBOX_CMD_SET_ACB;
	mbox_cmd[1] = 0; /* Primary ACB */
	mbox_cmd[2] = LSDW(acb_dma);
	mbox_cmd[3] = MSDW(acb_dma);
	mbox_cmd[4] = sizeof(struct addr_ctrl_blk);

	status = qla4xxx_mailbox_command(ha, 5, 5, &mbox_cmd[0], &mbox_sts[0]);
	if (status != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_WARNING, ha,  "%s: MBOX_CMD_SET_ACB "
				  "failed w/ status %04X\n", __func__,
				  mbox_sts[0]));
	}
	return status;
}

int qla4xxx_set_param_ddbentry(struct scsi_qla_host *ha,
			       struct ddb_entry *ddb_entry,
			       struct iscsi_cls_conn *cls_conn,
			       uint32_t *mbx_sts)
{
	struct dev_db_entry *fw_ddb_entry;
	struct iscsi_conn *conn;
	struct iscsi_session *sess;
	struct qla_conn *qla_conn;
	struct sockaddr *dst_addr;
	dma_addr_t fw_ddb_entry_dma;
	int status = QLA_SUCCESS;
	int rval = 0;
	struct sockaddr_in *addr;
	struct sockaddr_in6 *addr6;
	char *ip;
	uint16_t iscsi_opts = 0;
	uint32_t options = 0;
	uint16_t idx, *ptid;

	fw_ddb_entry = dma_alloc_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
					  &fw_ddb_entry_dma, GFP_KERNEL);
	if (!fw_ddb_entry) {
		DEBUG2(ql4_printk(KERN_ERR, ha,
				  "%s: Unable to allocate dma buffer.\n",
				  __func__));
		rval = -ENOMEM;
		goto exit_set_param_no_free;
	}

	conn = cls_conn->dd_data;
	qla_conn = conn->dd_data;
	sess = conn->session;
	dst_addr = (struct sockaddr *)&qla_conn->qla_ep->dst_addr;

	if (dst_addr->sa_family == AF_INET6)
		options |= IPV6_DEFAULT_DDB_ENTRY;

	status = qla4xxx_get_default_ddb(ha, options, fw_ddb_entry_dma);
	if (status == QLA_ERROR) {
		rval = -EINVAL;
		goto exit_set_param;
	}

	ptid = (uint16_t *)&fw_ddb_entry->isid[1];
	*ptid = cpu_to_le16((uint16_t)ddb_entry->sess->target_id);

	DEBUG2(ql4_printk(KERN_INFO, ha, "ISID [%02x%02x%02x%02x%02x%02x]\n",
			  fw_ddb_entry->isid[5], fw_ddb_entry->isid[4],
			  fw_ddb_entry->isid[3], fw_ddb_entry->isid[2],
			  fw_ddb_entry->isid[1], fw_ddb_entry->isid[0]));

	iscsi_opts = le16_to_cpu(fw_ddb_entry->iscsi_options);
	memset(fw_ddb_entry->iscsi_alias, 0, sizeof(fw_ddb_entry->iscsi_alias));

	memset(fw_ddb_entry->iscsi_name, 0, sizeof(fw_ddb_entry->iscsi_name));

	if (sess->targetname != NULL) {
		memcpy(fw_ddb_entry->iscsi_name, sess->targetname,
		       min(strlen(sess->targetname),
		       sizeof(fw_ddb_entry->iscsi_name)));
	}

	memset(fw_ddb_entry->ip_addr, 0, sizeof(fw_ddb_entry->ip_addr));
	memset(fw_ddb_entry->tgt_addr, 0, sizeof(fw_ddb_entry->tgt_addr));

	fw_ddb_entry->options =  DDB_OPT_TARGET | DDB_OPT_AUTO_SENDTGTS_DISABLE;

	if (dst_addr->sa_family == AF_INET) {
		addr = (struct sockaddr_in *)dst_addr;
		ip = (char *)&addr->sin_addr;
		memcpy(fw_ddb_entry->ip_addr, ip, IP_ADDR_LEN);
		fw_ddb_entry->port = cpu_to_le16(ntohs(addr->sin_port));
		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "%s: Destination Address [%pI4]: index [%d]\n",
				   __func__, fw_ddb_entry->ip_addr,
				  ddb_entry->fw_ddb_index));
	} else if (dst_addr->sa_family == AF_INET6) {
		addr6 = (struct sockaddr_in6 *)dst_addr;
		ip = (char *)&addr6->sin6_addr;
		memcpy(fw_ddb_entry->ip_addr, ip, IPv6_ADDR_LEN);
		fw_ddb_entry->port = cpu_to_le16(ntohs(addr6->sin6_port));
		fw_ddb_entry->options |= DDB_OPT_IPV6_DEVICE;
		DEBUG2(ql4_printk(KERN_INFO, ha,
				  "%s: Destination Address [%pI6]: index [%d]\n",
				   __func__, fw_ddb_entry->ip_addr,
				  ddb_entry->fw_ddb_index));
	} else {
		ql4_printk(KERN_ERR, ha,
			   "%s: Failed to get IP Address\n",
			   __func__);
		rval = -EINVAL;
		goto exit_set_param;
	}

	/* CHAP */
	if (sess->username != NULL && sess->password != NULL) {
		if (strlen(sess->username) && strlen(sess->password)) {
			iscsi_opts |= BIT_7;

			rval = qla4xxx_get_chap_index(ha, sess->username,
						sess->password,
						LOCAL_CHAP, &idx);
			if (rval)
				goto exit_set_param;

			fw_ddb_entry->chap_tbl_idx = cpu_to_le16(idx);
		}
	}

	if (sess->username_in != NULL && sess->password_in != NULL) {
		/* Check if BIDI CHAP */
		if (strlen(sess->username_in) && strlen(sess->password_in)) {
			iscsi_opts |= BIT_4;

			rval = qla4xxx_get_chap_index(ha, sess->username_in,
						      sess->password_in,
						      BIDI_CHAP, &idx);
			if (rval)
				goto exit_set_param;
		}
	}

	if (sess->initial_r2t_en)
		iscsi_opts |= BIT_10;

	if (sess->imm_data_en)
		iscsi_opts |= BIT_11;

	fw_ddb_entry->iscsi_options = cpu_to_le16(iscsi_opts);

	if (conn->max_recv_dlength)
		fw_ddb_entry->iscsi_max_rcv_data_seg_len =
		  __constant_cpu_to_le16((conn->max_recv_dlength / BYTE_UNITS));

	if (sess->max_r2t)
		fw_ddb_entry->iscsi_max_outsnd_r2t = cpu_to_le16(sess->max_r2t);

	if (sess->first_burst)
		fw_ddb_entry->iscsi_first_burst_len =
		       __constant_cpu_to_le16((sess->first_burst / BYTE_UNITS));

	if (sess->max_burst)
		fw_ddb_entry->iscsi_max_burst_len =
			__constant_cpu_to_le16((sess->max_burst / BYTE_UNITS));

	if (sess->time2wait)
		fw_ddb_entry->iscsi_def_time2wait =
			cpu_to_le16(sess->time2wait);

	if (sess->time2retain)
		fw_ddb_entry->iscsi_def_time2retain =
			cpu_to_le16(sess->time2retain);

	status = qla4xxx_set_ddb_entry(ha, ddb_entry->fw_ddb_index,
				       fw_ddb_entry_dma, mbx_sts);

	if (status != QLA_SUCCESS)
		rval = -EINVAL;
exit_set_param:
	dma_free_coherent(&ha->pdev->dev, sizeof(*fw_ddb_entry),
			  fw_ddb_entry, fw_ddb_entry_dma);
exit_set_param_no_free:
	return rval;
}

int qla4xxx_get_mgmt_data(struct scsi_qla_host *ha, uint16_t fw_ddb_index,
			  uint16_t stats_size, dma_addr_t stats_dma)
{
	int status = QLA_SUCCESS;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	memset(mbox_cmd, 0, sizeof(mbox_cmd[0]) * MBOX_REG_COUNT);
	memset(mbox_sts, 0, sizeof(mbox_sts[0]) * MBOX_REG_COUNT);
	mbox_cmd[0] = MBOX_CMD_GET_MANAGEMENT_DATA;
	mbox_cmd[1] = fw_ddb_index;
	mbox_cmd[2] = LSDW(stats_dma);
	mbox_cmd[3] = MSDW(stats_dma);
	mbox_cmd[4] = stats_size;

	status = qla4xxx_mailbox_command(ha, 5, 1, &mbox_cmd[0], &mbox_sts[0]);
	if (status != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_WARNING, ha,
				  "%s: MBOX_CMD_GET_MANAGEMENT_DATA "
				  "failed w/ status %04X\n", __func__,
				  mbox_sts[0]));
	}
	return status;
}

int qla4xxx_get_ip_state(struct scsi_qla_host *ha, uint32_t acb_idx,
			 uint32_t ip_idx, uint32_t *sts)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status = QLA_SUCCESS;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));
	mbox_cmd[0] = MBOX_CMD_GET_IP_ADDR_STATE;
	mbox_cmd[1] = acb_idx;
	mbox_cmd[2] = ip_idx;

	status = qla4xxx_mailbox_command(ha, 3, 8, &mbox_cmd[0], &mbox_sts[0]);
	if (status != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_WARNING, ha,  "%s: "
				  "MBOX_CMD_GET_IP_ADDR_STATE failed w/ "
				  "status %04X\n", __func__, mbox_sts[0]));
	}
	memcpy(sts, mbox_sts, sizeof(mbox_sts));
	return status;
}

int qla4xxx_get_nvram(struct scsi_qla_host *ha, dma_addr_t nvram_dma,
		      uint32_t offset, uint32_t size)
{
	int status = QLA_SUCCESS;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_GET_NVRAM;
	mbox_cmd[1] = LSDW(nvram_dma);
	mbox_cmd[2] = MSDW(nvram_dma);
	mbox_cmd[3] = offset;
	mbox_cmd[4] = size;

	status = qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 1, &mbox_cmd[0],
					 &mbox_sts[0]);
	if (status != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_ERR, ha, "scsi%ld: %s: failed "
				  "status %04X\n", ha->host_no, __func__,
				  mbox_sts[0]));
	}
	return status;
}

int qla4xxx_set_nvram(struct scsi_qla_host *ha, dma_addr_t nvram_dma,
		      uint32_t offset, uint32_t size)
{
	int status = QLA_SUCCESS;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_SET_NVRAM;
	mbox_cmd[1] = LSDW(nvram_dma);
	mbox_cmd[2] = MSDW(nvram_dma);
	mbox_cmd[3] = offset;
	mbox_cmd[4] = size;

	status = qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 1, &mbox_cmd[0],
					 &mbox_sts[0]);
	if (status != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_ERR, ha, "scsi%ld: %s: failed "
				  "status %04X\n", ha->host_no, __func__,
				  mbox_sts[0]));
	}
	return status;
}

int qla4xxx_restore_factory_defaults(struct scsi_qla_host *ha,
				     uint32_t region, uint32_t field0,
				     uint32_t field1)
{
	int status = QLA_SUCCESS;
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_RESTORE_FACTORY_DEFAULTS;
	mbox_cmd[3] = region;
	mbox_cmd[4] = field0;
	mbox_cmd[5] = field1;

	status = qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 3, &mbox_cmd[0],
					 &mbox_sts[0]);
	if (status != QLA_SUCCESS) {
		DEBUG2(ql4_printk(KERN_ERR, ha, "scsi%ld: %s: failed "
				  "status %04X\n", ha->host_no, __func__,
				  mbox_sts[0]));
	}
	return status;
}

/**
 * qla4_8xxx_set_param - set driver version in firmware.
 * @ha: Pointer to host adapter structure.
 * @param: Parameter to set i.e driver version
 **/
int qla4_8xxx_set_param(struct scsi_qla_host *ha, int param)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	uint32_t status;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_SET_PARAM;
	if (param == SET_DRVR_VERSION) {
		mbox_cmd[1] = SET_DRVR_VERSION;
		strncpy((char *)&mbox_cmd[2], QLA4XXX_DRIVER_VERSION,
			MAX_DRVR_VER_LEN);
	} else {
		ql4_printk(KERN_ERR, ha, "%s: invalid parameter 0x%x\n",
			   __func__, param);
		status = QLA_ERROR;
		goto exit_set_param;
	}

	status = qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, 2, mbox_cmd,
					 mbox_sts);
	if (status == QLA_ERROR)
		ql4_printk(KERN_ERR, ha, "%s: failed status %04X\n",
			   __func__, mbox_sts[0]);

exit_set_param:
	return status;
}

/**
 * qla4_83xx_post_idc_ack - post IDC ACK
 * @ha: Pointer to host adapter structure.
 *
 * Posts IDC ACK for IDC Request Notification AEN.
 **/
int qla4_83xx_post_idc_ack(struct scsi_qla_host *ha)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_IDC_ACK;
	mbox_cmd[1] = ha->idc_info.request_desc;
	mbox_cmd[2] = ha->idc_info.info1;
	mbox_cmd[3] = ha->idc_info.info2;
	mbox_cmd[4] = ha->idc_info.info3;

	status = qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, MBOX_REG_COUNT,
					 mbox_cmd, mbox_sts);
	if (status == QLA_ERROR)
		ql4_printk(KERN_ERR, ha, "%s: failed status %04X\n", __func__,
			   mbox_sts[0]);
	else
	       ql4_printk(KERN_INFO, ha, "%s: IDC ACK posted\n", __func__);

	return status;
}

int qla4_84xx_config_acb(struct scsi_qla_host *ha, int acb_config)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	struct addr_ctrl_blk *acb = NULL;
	uint32_t acb_len = sizeof(struct addr_ctrl_blk);
	int rval = QLA_SUCCESS;
	dma_addr_t acb_dma;

	acb = dma_alloc_coherent(&ha->pdev->dev,
				 sizeof(struct addr_ctrl_blk),
				 &acb_dma, GFP_KERNEL);
	if (!acb) {
		ql4_printk(KERN_ERR, ha, "%s: Unable to alloc acb\n", __func__);
		rval = QLA_ERROR;
		goto exit_config_acb;
	}
	memset(acb, 0, acb_len);

	switch (acb_config) {
	case ACB_CONFIG_DISABLE:
		rval = qla4xxx_get_acb(ha, acb_dma, 0, acb_len);
		if (rval != QLA_SUCCESS)
			goto exit_free_acb;

		rval = qla4xxx_disable_acb(ha);
		if (rval != QLA_SUCCESS)
			goto exit_free_acb;

		if (!ha->saved_acb)
			ha->saved_acb = kzalloc(acb_len, GFP_KERNEL);

		if (!ha->saved_acb) {
			ql4_printk(KERN_ERR, ha, "%s: Unable to alloc acb\n",
				   __func__);
			rval = QLA_ERROR;
			goto exit_config_acb;
		}
		memcpy(ha->saved_acb, acb, acb_len);
		break;
	case ACB_CONFIG_SET:

		if (!ha->saved_acb) {
			ql4_printk(KERN_ERR, ha, "%s: Can't set ACB, Saved ACB not available\n",
				   __func__);
			rval = QLA_ERROR;
			goto exit_free_acb;
		}

		memcpy(acb, ha->saved_acb, acb_len);
		kfree(ha->saved_acb);
		ha->saved_acb = NULL;

		rval = qla4xxx_set_acb(ha, &mbox_cmd[0], &mbox_sts[0], acb_dma);
		if (rval != QLA_SUCCESS)
			goto exit_free_acb;

		break;
	default:
		ql4_printk(KERN_ERR, ha, "%s: Invalid ACB Configuration\n",
			   __func__);
	}

exit_free_acb:
	dma_free_coherent(&ha->pdev->dev, sizeof(struct addr_ctrl_blk), acb,
			  acb_dma);
exit_config_acb:
	DEBUG2(ql4_printk(KERN_INFO, ha,
			  "%s %s\n", __func__,
			  rval == QLA_SUCCESS ? "SUCCEEDED" : "FAILED"));
	return rval;
}

int qla4_83xx_get_port_config(struct scsi_qla_host *ha, uint32_t *config)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_GET_PORT_CONFIG;

	status = qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, MBOX_REG_COUNT,
					 mbox_cmd, mbox_sts);
	if (status == QLA_SUCCESS)
		*config = mbox_sts[1];
	else
		ql4_printk(KERN_ERR, ha, "%s: failed status %04X\n", __func__,
			   mbox_sts[0]);

	return status;
}

int qla4_83xx_set_port_config(struct scsi_qla_host *ha, uint32_t *config)
{
	uint32_t mbox_cmd[MBOX_REG_COUNT];
	uint32_t mbox_sts[MBOX_REG_COUNT];
	int status;

	memset(&mbox_cmd, 0, sizeof(mbox_cmd));
	memset(&mbox_sts, 0, sizeof(mbox_sts));

	mbox_cmd[0] = MBOX_CMD_SET_PORT_CONFIG;
	mbox_cmd[1] = *config;

	status = qla4xxx_mailbox_command(ha, MBOX_REG_COUNT, MBOX_REG_COUNT,
				mbox_cmd, mbox_sts);
	if (status != QLA_SUCCESS)
		ql4_printk(KERN_ERR, ha, "%s: failed status %04X\n", __func__,
			   mbox_sts[0]);

	return status;
}
