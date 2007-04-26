/*
 * QLogic iSCSI HBA Driver
 * Copyright (c)  2003-2006 QLogic Corporation
 *
 * See LICENSE.qla4xxx for copyright and licensing details.
 */

#include "ql4_def.h"
#include <scsi/scsi_dbg.h>

#if 0

static void qla4xxx_print_srb_info(struct srb * srb)
{
	printk("%s: srb = 0x%p, flags=0x%02x\n", __func__, srb, srb->flags);
	printk("%s: cmd = 0x%p, saved_dma_handle = 0x%lx\n",
	       __func__, srb->cmd, (unsigned long) srb->dma_handle);
	printk("%s: fw_ddb_index = %d, lun = %d\n",
	       __func__, srb->fw_ddb_index, srb->cmd->device->lun);
	printk("%s: iocb_tov = %d\n",
	       __func__, srb->iocb_tov);
	printk("%s: cc_stat = 0x%x, r_start = 0x%lx, u_start = 0x%lx\n\n",
	       __func__, srb->cc_stat, srb->r_start, srb->u_start);
}

void qla4xxx_print_scsi_cmd(struct scsi_cmnd *cmd)
{
	printk("SCSI Command = 0x%p, Handle=0x%p\n", cmd, cmd->host_scribble);
	printk("  b=%d, t=%02xh, l=%02xh, cmd_len = %02xh\n",
	       cmd->device->channel, cmd->device->id, cmd->device->lun,
	       cmd->cmd_len);
	scsi_print_command(cmd);
	printk("  seg_cnt = %d\n", cmd->use_sg);
	printk("  request buffer = 0x%p, request buffer len = 0x%x\n",
	       cmd->request_buffer, cmd->request_bufflen);
	if (cmd->use_sg) {
		struct scatterlist *sg;
		sg = (struct scatterlist *)cmd->request_buffer;
		printk("  SG buffer: \n");
		qla4xxx_dump_buffer((caddr_t) sg,
				    (cmd->use_sg * sizeof(*sg)));
	}
	printk("  tag = %d, transfersize = 0x%x \n", cmd->tag,
	       cmd->transfersize);
	printk("  Pid = %d, SP = 0x%p\n", (int)cmd->pid, cmd->SCp.ptr);
	printk("  underflow size = 0x%x, direction=0x%x\n", cmd->underflow,
	       cmd->sc_data_direction);
	printk("  Current time (jiffies) = 0x%lx, "
	       "timeout expires = 0x%lx\n", jiffies, cmd->eh_timeout.expires);
	qla4xxx_print_srb_info((struct srb *) cmd->SCp.ptr);
}

void __dump_registers(struct scsi_qla_host *ha)
{
	uint8_t i;
	for (i = 0; i < MBOX_REG_COUNT; i++) {
		printk(KERN_INFO "0x%02X mailbox[%d]	  = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg, mailbox[i]), i,
		       readw(&ha->reg->mailbox[i]));
	}
	printk(KERN_INFO "0x%02X flash_address	 = 0x%08X\n",
	       (uint8_t) offsetof(struct isp_reg, flash_address),
	       readw(&ha->reg->flash_address));
	printk(KERN_INFO "0x%02X flash_data	 = 0x%08X\n",
	       (uint8_t) offsetof(struct isp_reg, flash_data),
	       readw(&ha->reg->flash_data));
	printk(KERN_INFO "0x%02X ctrl_status	 = 0x%08X\n",
	       (uint8_t) offsetof(struct isp_reg, ctrl_status),
	       readw(&ha->reg->ctrl_status));
	if (is_qla4010(ha)) {
		printk(KERN_INFO "0x%02X nvram		 = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg, u1.isp4010.nvram),
		       readw(&ha->reg->u1.isp4010.nvram));
	}

	else if (is_qla4022(ha) | is_qla4032(ha)) {
		printk(KERN_INFO "0x%02X intr_mask	 = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg,
					  u1.isp4022.intr_mask),
		       readw(&ha->reg->u1.isp4022.intr_mask));
		printk(KERN_INFO "0x%02X nvram		 = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg, u1.isp4022.nvram),
		       readw(&ha->reg->u1.isp4022.nvram));
		printk(KERN_INFO "0x%02X semaphore	 = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg,
					  u1.isp4022.semaphore),
		       readw(&ha->reg->u1.isp4022.semaphore));
	}
	printk(KERN_INFO "0x%02X req_q_in	 = 0x%08X\n",
	       (uint8_t) offsetof(struct isp_reg, req_q_in),
	       readw(&ha->reg->req_q_in));
	printk(KERN_INFO "0x%02X rsp_q_out	 = 0x%08X\n",
	       (uint8_t) offsetof(struct isp_reg, rsp_q_out),
	       readw(&ha->reg->rsp_q_out));
	if (is_qla4010(ha)) {
		printk(KERN_INFO "0x%02X ext_hw_conf	 = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg,
					  u2.isp4010.ext_hw_conf),
		       readw(&ha->reg->u2.isp4010.ext_hw_conf));
		printk(KERN_INFO "0x%02X port_ctrl	 = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg,
					  u2.isp4010.port_ctrl),
		       readw(&ha->reg->u2.isp4010.port_ctrl));
		printk(KERN_INFO "0x%02X port_status	 = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg,
					  u2.isp4010.port_status),
		       readw(&ha->reg->u2.isp4010.port_status));
		printk(KERN_INFO "0x%02X req_q_out	 = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg,
					  u2.isp4010.req_q_out),
		       readw(&ha->reg->u2.isp4010.req_q_out));
		printk(KERN_INFO "0x%02X gp_out		 = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg, u2.isp4010.gp_out),
		       readw(&ha->reg->u2.isp4010.gp_out));
		printk(KERN_INFO "0x%02X gp_in		 = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg, u2.isp4010.gp_in),
		       readw(&ha->reg->u2.isp4010.gp_in));
		printk(KERN_INFO "0x%02X port_err_status = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg,
					  u2.isp4010.port_err_status),
		       readw(&ha->reg->u2.isp4010.port_err_status));
	}

	else if (is_qla4022(ha) | is_qla4032(ha)) {
		printk(KERN_INFO "Page 0 Registers:\n");
		printk(KERN_INFO "0x%02X ext_hw_conf	 = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg,
					  u2.isp4022.p0.ext_hw_conf),
		       readw(&ha->reg->u2.isp4022.p0.ext_hw_conf));
		printk(KERN_INFO "0x%02X port_ctrl	 = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg,
					  u2.isp4022.p0.port_ctrl),
		       readw(&ha->reg->u2.isp4022.p0.port_ctrl));
		printk(KERN_INFO "0x%02X port_status	 = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg,
					  u2.isp4022.p0.port_status),
		       readw(&ha->reg->u2.isp4022.p0.port_status));
		printk(KERN_INFO "0x%02X gp_out		 = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg,
					  u2.isp4022.p0.gp_out),
		       readw(&ha->reg->u2.isp4022.p0.gp_out));
		printk(KERN_INFO "0x%02X gp_in		 = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg, u2.isp4022.p0.gp_in),
		       readw(&ha->reg->u2.isp4022.p0.gp_in));
		printk(KERN_INFO "0x%02X port_err_status = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg,
					  u2.isp4022.p0.port_err_status),
		       readw(&ha->reg->u2.isp4022.p0.port_err_status));
		printk(KERN_INFO "Page 1 Registers:\n");
		writel(HOST_MEM_CFG_PAGE & set_rmask(CSR_SCSI_PAGE_SELECT),
		       &ha->reg->ctrl_status);
		printk(KERN_INFO "0x%02X req_q_out	 = 0x%08X\n",
		       (uint8_t) offsetof(struct isp_reg,
					  u2.isp4022.p1.req_q_out),
		       readw(&ha->reg->u2.isp4022.p1.req_q_out));
		writel(PORT_CTRL_STAT_PAGE & set_rmask(CSR_SCSI_PAGE_SELECT),
		       &ha->reg->ctrl_status);
	}
}

void qla4xxx_dump_mbox_registers(struct scsi_qla_host *ha)
{
	unsigned long flags = 0;
	int i = 0;
	spin_lock_irqsave(&ha->hardware_lock, flags);
	for (i = 1; i < MBOX_REG_COUNT; i++)
		printk(KERN_INFO "  Mailbox[%d] = %08x\n", i,
		       readw(&ha->reg->mailbox[i]));
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

void qla4xxx_dump_registers(struct scsi_qla_host *ha)
{
	unsigned long flags = 0;
	spin_lock_irqsave(&ha->hardware_lock, flags);
	__dump_registers(ha);
	spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

void qla4xxx_dump_buffer(void *b, uint32_t size)
{
	uint32_t cnt;
	uint8_t *c = b;

	printk(" 0   1	 2   3	 4   5	 6   7	 8   9	Ah  Bh	Ch  Dh	Eh  "
	       "Fh\n");
	printk("------------------------------------------------------------"
	       "--\n");
	for (cnt = 0; cnt < size; cnt++, c++) {
		printk(KERN_DEBUG "%02x", *c);
		if (!(cnt % 16))
			printk(KERN_DEBUG "\n");

		else
			printk(KERN_DEBUG "  ");
	}
	if (cnt % 16)
		printk(KERN_DEBUG "\n");
}

#endif  /*  0  */
