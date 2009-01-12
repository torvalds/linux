/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2008 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"

#include <linux/delay.h>

static inline void
qla2xxx_prep_dump(struct qla_hw_data *ha, struct qla2xxx_fw_dump *fw_dump)
{
	fw_dump->fw_major_version = htonl(ha->fw_major_version);
	fw_dump->fw_minor_version = htonl(ha->fw_minor_version);
	fw_dump->fw_subminor_version = htonl(ha->fw_subminor_version);
	fw_dump->fw_attributes = htonl(ha->fw_attributes);

	fw_dump->vendor = htonl(ha->pdev->vendor);
	fw_dump->device = htonl(ha->pdev->device);
	fw_dump->subsystem_vendor = htonl(ha->pdev->subsystem_vendor);
	fw_dump->subsystem_device = htonl(ha->pdev->subsystem_device);
}

static inline void *
qla2xxx_copy_queues(struct qla_hw_data *ha, void *ptr)
{
	struct req_que *req = ha->req_q_map[0];
	struct rsp_que *rsp = ha->rsp_q_map[0];
	/* Request queue. */
	memcpy(ptr, req->ring, req->length *
	    sizeof(request_t));

	/* Response queue. */
	ptr += req->length * sizeof(request_t);
	memcpy(ptr, rsp->ring, rsp->length  *
	    sizeof(response_t));

	return ptr + (rsp->length * sizeof(response_t));
}

static int
qla24xx_dump_ram(struct qla_hw_data *ha, uint32_t addr, uint32_t *ram,
    uint32_t ram_dwords, void **nxt)
{
	int rval;
	uint32_t cnt, stat, timer, dwords, idx;
	uint16_t mb0;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	dma_addr_t dump_dma = ha->gid_list_dma;
	uint32_t *dump = (uint32_t *)ha->gid_list;

	rval = QLA_SUCCESS;
	mb0 = 0;

	WRT_REG_WORD(&reg->mailbox0, MBC_DUMP_RISC_RAM_EXTENDED);
	clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

	dwords = GID_LIST_SIZE / 4;
	for (cnt = 0; cnt < ram_dwords && rval == QLA_SUCCESS;
	    cnt += dwords, addr += dwords) {
		if (cnt + dwords > ram_dwords)
			dwords = ram_dwords - cnt;

		WRT_REG_WORD(&reg->mailbox1, LSW(addr));
		WRT_REG_WORD(&reg->mailbox8, MSW(addr));

		WRT_REG_WORD(&reg->mailbox2, MSW(dump_dma));
		WRT_REG_WORD(&reg->mailbox3, LSW(dump_dma));
		WRT_REG_WORD(&reg->mailbox6, MSW(MSD(dump_dma)));
		WRT_REG_WORD(&reg->mailbox7, LSW(MSD(dump_dma)));

		WRT_REG_WORD(&reg->mailbox4, MSW(dwords));
		WRT_REG_WORD(&reg->mailbox5, LSW(dwords));
		WRT_REG_DWORD(&reg->hccr, HCCRX_SET_HOST_INT);

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

				/* Clear this intr; it wasn't a mailbox intr */
				WRT_REG_DWORD(&reg->hccr, HCCRX_CLR_RISC_INT);
				RD_REG_DWORD(&reg->hccr);
			}
			udelay(5);
		}

		if (test_and_clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags)) {
			rval = mb0 & MBS_MASK;
			for (idx = 0; idx < dwords; idx++)
				ram[cnt + idx] = swab32(dump[idx]);
		} else {
			rval = QLA_FUNCTION_FAILED;
		}
	}

	*nxt = rval == QLA_SUCCESS ? &ram[cnt]: NULL;
	return rval;
}

static int
qla24xx_dump_memory(struct qla_hw_data *ha, uint32_t *code_ram,
    uint32_t cram_size, void **nxt)
{
	int rval;

	/* Code RAM. */
	rval = qla24xx_dump_ram(ha, 0x20000, code_ram, cram_size / 4, nxt);
	if (rval != QLA_SUCCESS)
		return rval;

	/* External Memory. */
	return qla24xx_dump_ram(ha, 0x100000, *nxt,
	    ha->fw_memory_size - 0x100000 + 1, nxt);
}

static uint32_t *
qla24xx_read_window(struct device_reg_24xx __iomem *reg, uint32_t iobase,
    uint32_t count, uint32_t *buf)
{
	uint32_t __iomem *dmp_reg;

	WRT_REG_DWORD(&reg->iobase_addr, iobase);
	dmp_reg = &reg->iobase_window;
	while (count--)
		*buf++ = htonl(RD_REG_DWORD(dmp_reg++));

	return buf;
}

static inline int
qla24xx_pause_risc(struct device_reg_24xx __iomem *reg)
{
	int rval = QLA_SUCCESS;
	uint32_t cnt;

	if (RD_REG_DWORD(&reg->hccr) & HCCRX_RISC_PAUSE)
		return rval;

	WRT_REG_DWORD(&reg->hccr, HCCRX_SET_RISC_PAUSE);
	for (cnt = 30000; (RD_REG_DWORD(&reg->hccr) & HCCRX_RISC_PAUSE) == 0 &&
	    rval == QLA_SUCCESS; cnt--) {
		if (cnt)
			udelay(100);
		else
			rval = QLA_FUNCTION_TIMEOUT;
	}

	return rval;
}

static int
qla24xx_soft_reset(struct qla_hw_data *ha)
{
	int rval = QLA_SUCCESS;
	uint32_t cnt;
	uint16_t mb0, wd;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	/* Reset RISC. */
	WRT_REG_DWORD(&reg->ctrl_status, CSRX_DMA_SHUTDOWN|MWB_4096_BYTES);
	for (cnt = 0; cnt < 30000; cnt++) {
		if ((RD_REG_DWORD(&reg->ctrl_status) & CSRX_DMA_ACTIVE) == 0)
			break;

		udelay(10);
	}

	WRT_REG_DWORD(&reg->ctrl_status,
	    CSRX_ISP_SOFT_RESET|CSRX_DMA_SHUTDOWN|MWB_4096_BYTES);
	pci_read_config_word(ha->pdev, PCI_COMMAND, &wd);

	udelay(100);
	/* Wait for firmware to complete NVRAM accesses. */
	mb0 = (uint32_t) RD_REG_WORD(&reg->mailbox0);
	for (cnt = 10000 ; cnt && mb0; cnt--) {
		udelay(5);
		mb0 = (uint32_t) RD_REG_WORD(&reg->mailbox0);
		barrier();
	}

	/* Wait for soft-reset to complete. */
	for (cnt = 0; cnt < 30000; cnt++) {
		if ((RD_REG_DWORD(&reg->ctrl_status) &
		    CSRX_ISP_SOFT_RESET) == 0)
			break;

		udelay(10);
	}
	WRT_REG_DWORD(&reg->hccr, HCCRX_CLR_RISC_RESET);
	RD_REG_DWORD(&reg->hccr);             /* PCI Posting. */

	for (cnt = 30000; RD_REG_WORD(&reg->mailbox0) != 0 &&
	    rval == QLA_SUCCESS; cnt--) {
		if (cnt)
			udelay(100);
		else
			rval = QLA_FUNCTION_TIMEOUT;
	}

	return rval;
}

static int
qla2xxx_dump_ram(struct qla_hw_data *ha, uint32_t addr, uint16_t *ram,
    uint16_t ram_words, void **nxt)
{
	int rval;
	uint32_t cnt, stat, timer, words, idx;
	uint16_t mb0;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	dma_addr_t dump_dma = ha->gid_list_dma;
	uint16_t *dump = (uint16_t *)ha->gid_list;

	rval = QLA_SUCCESS;
	mb0 = 0;

	WRT_MAILBOX_REG(ha, reg, 0, MBC_DUMP_RISC_RAM_EXTENDED);
	clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

	words = GID_LIST_SIZE / 2;
	for (cnt = 0; cnt < ram_words && rval == QLA_SUCCESS;
	    cnt += words, addr += words) {
		if (cnt + words > ram_words)
			words = ram_words - cnt;

		WRT_MAILBOX_REG(ha, reg, 1, LSW(addr));
		WRT_MAILBOX_REG(ha, reg, 8, MSW(addr));

		WRT_MAILBOX_REG(ha, reg, 2, MSW(dump_dma));
		WRT_MAILBOX_REG(ha, reg, 3, LSW(dump_dma));
		WRT_MAILBOX_REG(ha, reg, 6, MSW(MSD(dump_dma)));
		WRT_MAILBOX_REG(ha, reg, 7, LSW(MSD(dump_dma)));

		WRT_MAILBOX_REG(ha, reg, 4, words);
		WRT_REG_WORD(&reg->hccr, HCCR_SET_HOST_INT);

		for (timer = 6000000; timer; timer--) {
			/* Check for pending interrupts. */
			stat = RD_REG_DWORD(&reg->u.isp2300.host_status);
			if (stat & HSR_RISC_INT) {
				stat &= 0xff;

				if (stat == 0x1 || stat == 0x2) {
					set_bit(MBX_INTERRUPT,
					    &ha->mbx_cmd_flags);

					mb0 = RD_MAILBOX_REG(ha, reg, 0);

					/* Release mailbox registers. */
					WRT_REG_WORD(&reg->semaphore, 0);
					WRT_REG_WORD(&reg->hccr,
					    HCCR_CLR_RISC_INT);
					RD_REG_WORD(&reg->hccr);
					break;
				} else if (stat == 0x10 || stat == 0x11) {
					set_bit(MBX_INTERRUPT,
					    &ha->mbx_cmd_flags);

					mb0 = RD_MAILBOX_REG(ha, reg, 0);

					WRT_REG_WORD(&reg->hccr,
					    HCCR_CLR_RISC_INT);
					RD_REG_WORD(&reg->hccr);
					break;
				}

				/* clear this intr; it wasn't a mailbox intr */
				WRT_REG_WORD(&reg->hccr, HCCR_CLR_RISC_INT);
				RD_REG_WORD(&reg->hccr);
			}
			udelay(5);
		}

		if (test_and_clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags)) {
			rval = mb0 & MBS_MASK;
			for (idx = 0; idx < words; idx++)
				ram[cnt + idx] = swab16(dump[idx]);
		} else {
			rval = QLA_FUNCTION_FAILED;
		}
	}

	*nxt = rval == QLA_SUCCESS ? &ram[cnt]: NULL;
	return rval;
}

static inline void
qla2xxx_read_window(struct device_reg_2xxx __iomem *reg, uint32_t count,
    uint16_t *buf)
{
	uint16_t __iomem *dmp_reg = &reg->u.isp2300.fb_cmd;

	while (count--)
		*buf++ = htons(RD_REG_WORD(dmp_reg++));
}

static inline void *
qla24xx_copy_eft(struct qla_hw_data *ha, void *ptr)
{
	if (!ha->eft)
		return ptr;

	memcpy(ptr, ha->eft, ntohl(ha->fw_dump->eft_size));
	return ptr + ntohl(ha->fw_dump->eft_size);
}

static inline void *
qla25xx_copy_fce(struct qla_hw_data *ha, void *ptr, uint32_t **last_chain)
{
	uint32_t cnt;
	uint32_t *iter_reg;
	struct qla2xxx_fce_chain *fcec = ptr;

	if (!ha->fce)
		return ptr;

	*last_chain = &fcec->type;
	fcec->type = __constant_htonl(DUMP_CHAIN_FCE);
	fcec->chain_size = htonl(sizeof(struct qla2xxx_fce_chain) +
	    fce_calc_size(ha->fce_bufs));
	fcec->size = htonl(fce_calc_size(ha->fce_bufs));
	fcec->addr_l = htonl(LSD(ha->fce_dma));
	fcec->addr_h = htonl(MSD(ha->fce_dma));

	iter_reg = fcec->eregs;
	for (cnt = 0; cnt < 8; cnt++)
		*iter_reg++ = htonl(ha->fce_mb[cnt]);

	memcpy(iter_reg, ha->fce, ntohl(fcec->size));

	return iter_reg;
}

static inline void *
qla25xx_copy_mq(struct qla_hw_data *ha, void *ptr, uint32_t **last_chain)
{
	uint32_t cnt, que_idx;
	uint8_t req_cnt, rsp_cnt, que_cnt;
	struct qla2xxx_mq_chain *mq = ptr;
	struct device_reg_25xxmq __iomem *reg;

	if (!ha->mqenable)
		return ptr;

	mq = ptr;
	*last_chain = &mq->type;
	mq->type = __constant_htonl(DUMP_CHAIN_MQ);
	mq->chain_size = __constant_htonl(sizeof(struct qla2xxx_mq_chain));

	req_cnt = find_first_zero_bit(ha->req_qid_map, ha->max_queues);
	rsp_cnt = find_first_zero_bit(ha->rsp_qid_map, ha->max_queues);
	que_cnt = req_cnt > rsp_cnt ? req_cnt : rsp_cnt;
	mq->count = htonl(que_cnt);
	for (cnt = 0; cnt < que_cnt; cnt++) {
		reg = (struct device_reg_25xxmq *) ((void *)
			ha->mqiobase + cnt * QLA_QUE_PAGE);
		que_idx = cnt * 4;
		mq->qregs[que_idx] = htonl(RD_REG_DWORD(&reg->req_q_in));
		mq->qregs[que_idx+1] = htonl(RD_REG_DWORD(&reg->req_q_out));
		mq->qregs[que_idx+2] = htonl(RD_REG_DWORD(&reg->rsp_q_in));
		mq->qregs[que_idx+3] = htonl(RD_REG_DWORD(&reg->rsp_q_out));
	}

	return ptr + sizeof(struct qla2xxx_mq_chain);
}

/**
 * qla2300_fw_dump() - Dumps binary data from the 2300 firmware.
 * @ha: HA context
 * @hardware_locked: Called with the hardware_lock
 */
void
qla2300_fw_dump(scsi_qla_host_t *vha, int hardware_locked)
{
	int		rval;
	uint32_t	cnt;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	uint16_t __iomem *dmp_reg;
	unsigned long	flags;
	struct qla2300_fw_dump	*fw;
	void		*nxt;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);

	flags = 0;

	if (!hardware_locked)
		spin_lock_irqsave(&ha->hardware_lock, flags);

	if (!ha->fw_dump) {
		qla_printk(KERN_WARNING, ha,
		    "No buffer available for dump!!!\n");
		goto qla2300_fw_dump_failed;
	}

	if (ha->fw_dumped) {
		qla_printk(KERN_WARNING, ha,
		    "Firmware has been previously dumped (%p) -- ignoring "
		    "request...\n", ha->fw_dump);
		goto qla2300_fw_dump_failed;
	}
	fw = &ha->fw_dump->isp.isp23;
	qla2xxx_prep_dump(ha, ha->fw_dump);

	rval = QLA_SUCCESS;
	fw->hccr = htons(RD_REG_WORD(&reg->hccr));

	/* Pause RISC. */
	WRT_REG_WORD(&reg->hccr, HCCR_PAUSE_RISC);
	if (IS_QLA2300(ha)) {
		for (cnt = 30000;
		    (RD_REG_WORD(&reg->hccr) & HCCR_RISC_PAUSE) == 0 &&
			rval == QLA_SUCCESS; cnt--) {
			if (cnt)
				udelay(100);
			else
				rval = QLA_FUNCTION_TIMEOUT;
		}
	} else {
		RD_REG_WORD(&reg->hccr);		/* PCI Posting. */
		udelay(10);
	}

	if (rval == QLA_SUCCESS) {
		dmp_reg = &reg->flash_address;
		for (cnt = 0; cnt < sizeof(fw->pbiu_reg) / 2; cnt++)
			fw->pbiu_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		dmp_reg = &reg->u.isp2300.req_q_in;
		for (cnt = 0; cnt < sizeof(fw->risc_host_reg) / 2; cnt++)
			fw->risc_host_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		dmp_reg = &reg->u.isp2300.mailbox0;
		for (cnt = 0; cnt < sizeof(fw->mailbox_reg) / 2; cnt++)
			fw->mailbox_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->ctrl_status, 0x40);
		qla2xxx_read_window(reg, 32, fw->resp_dma_reg);

		WRT_REG_WORD(&reg->ctrl_status, 0x50);
		qla2xxx_read_window(reg, 48, fw->dma_reg);

		WRT_REG_WORD(&reg->ctrl_status, 0x00);
		dmp_reg = &reg->risc_hw;
		for (cnt = 0; cnt < sizeof(fw->risc_hdw_reg) / 2; cnt++)
			fw->risc_hdw_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->pcr, 0x2000);
		qla2xxx_read_window(reg, 16, fw->risc_gp0_reg);

		WRT_REG_WORD(&reg->pcr, 0x2200);
		qla2xxx_read_window(reg, 16, fw->risc_gp1_reg);

		WRT_REG_WORD(&reg->pcr, 0x2400);
		qla2xxx_read_window(reg, 16, fw->risc_gp2_reg);

		WRT_REG_WORD(&reg->pcr, 0x2600);
		qla2xxx_read_window(reg, 16, fw->risc_gp3_reg);

		WRT_REG_WORD(&reg->pcr, 0x2800);
		qla2xxx_read_window(reg, 16, fw->risc_gp4_reg);

		WRT_REG_WORD(&reg->pcr, 0x2A00);
		qla2xxx_read_window(reg, 16, fw->risc_gp5_reg);

		WRT_REG_WORD(&reg->pcr, 0x2C00);
		qla2xxx_read_window(reg, 16, fw->risc_gp6_reg);

		WRT_REG_WORD(&reg->pcr, 0x2E00);
		qla2xxx_read_window(reg, 16, fw->risc_gp7_reg);

		WRT_REG_WORD(&reg->ctrl_status, 0x10);
		qla2xxx_read_window(reg, 64, fw->frame_buf_hdw_reg);

		WRT_REG_WORD(&reg->ctrl_status, 0x20);
		qla2xxx_read_window(reg, 64, fw->fpm_b0_reg);

		WRT_REG_WORD(&reg->ctrl_status, 0x30);
		qla2xxx_read_window(reg, 64, fw->fpm_b1_reg);

		/* Reset RISC. */
		WRT_REG_WORD(&reg->ctrl_status, CSR_ISP_SOFT_RESET);
		for (cnt = 0; cnt < 30000; cnt++) {
			if ((RD_REG_WORD(&reg->ctrl_status) &
			    CSR_ISP_SOFT_RESET) == 0)
				break;

			udelay(10);
		}
	}

	if (!IS_QLA2300(ha)) {
		for (cnt = 30000; RD_MAILBOX_REG(ha, reg, 0) != 0 &&
		    rval == QLA_SUCCESS; cnt--) {
			if (cnt)
				udelay(100);
			else
				rval = QLA_FUNCTION_TIMEOUT;
		}
	}

	/* Get RISC SRAM. */
	if (rval == QLA_SUCCESS)
		rval = qla2xxx_dump_ram(ha, 0x800, fw->risc_ram,
		    sizeof(fw->risc_ram) / 2, &nxt);

	/* Get stack SRAM. */
	if (rval == QLA_SUCCESS)
		rval = qla2xxx_dump_ram(ha, 0x10000, fw->stack_ram,
		    sizeof(fw->stack_ram) / 2, &nxt);

	/* Get data SRAM. */
	if (rval == QLA_SUCCESS)
		rval = qla2xxx_dump_ram(ha, 0x11000, fw->data_ram,
		    ha->fw_memory_size - 0x11000 + 1, &nxt);

	if (rval == QLA_SUCCESS)
		qla2xxx_copy_queues(ha, nxt);

	if (rval != QLA_SUCCESS) {
		qla_printk(KERN_WARNING, ha,
		    "Failed to dump firmware (%x)!!!\n", rval);
		ha->fw_dumped = 0;

	} else {
		qla_printk(KERN_INFO, ha,
		    "Firmware dump saved to temp buffer (%ld/%p).\n",
		    base_vha->host_no, ha->fw_dump);
		ha->fw_dumped = 1;
	}

qla2300_fw_dump_failed:
	if (!hardware_locked)
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

/**
 * qla2100_fw_dump() - Dumps binary data from the 2100/2200 firmware.
 * @ha: HA context
 * @hardware_locked: Called with the hardware_lock
 */
void
qla2100_fw_dump(scsi_qla_host_t *vha, int hardware_locked)
{
	int		rval;
	uint32_t	cnt, timer;
	uint16_t	risc_address;
	uint16_t	mb0, mb2;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	uint16_t __iomem *dmp_reg;
	unsigned long	flags;
	struct qla2100_fw_dump	*fw;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);

	risc_address = 0;
	mb0 = mb2 = 0;
	flags = 0;

	if (!hardware_locked)
		spin_lock_irqsave(&ha->hardware_lock, flags);

	if (!ha->fw_dump) {
		qla_printk(KERN_WARNING, ha,
		    "No buffer available for dump!!!\n");
		goto qla2100_fw_dump_failed;
	}

	if (ha->fw_dumped) {
		qla_printk(KERN_WARNING, ha,
		    "Firmware has been previously dumped (%p) -- ignoring "
		    "request...\n", ha->fw_dump);
		goto qla2100_fw_dump_failed;
	}
	fw = &ha->fw_dump->isp.isp21;
	qla2xxx_prep_dump(ha, ha->fw_dump);

	rval = QLA_SUCCESS;
	fw->hccr = htons(RD_REG_WORD(&reg->hccr));

	/* Pause RISC. */
	WRT_REG_WORD(&reg->hccr, HCCR_PAUSE_RISC);
	for (cnt = 30000; (RD_REG_WORD(&reg->hccr) & HCCR_RISC_PAUSE) == 0 &&
	    rval == QLA_SUCCESS; cnt--) {
		if (cnt)
			udelay(100);
		else
			rval = QLA_FUNCTION_TIMEOUT;
	}
	if (rval == QLA_SUCCESS) {
		dmp_reg = &reg->flash_address;
		for (cnt = 0; cnt < sizeof(fw->pbiu_reg) / 2; cnt++)
			fw->pbiu_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		dmp_reg = &reg->u.isp2100.mailbox0;
		for (cnt = 0; cnt < ha->mbx_count; cnt++) {
			if (cnt == 8)
				dmp_reg = &reg->u_end.isp2200.mailbox8;

			fw->mailbox_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));
		}

		dmp_reg = &reg->u.isp2100.unused_2[0];
		for (cnt = 0; cnt < sizeof(fw->dma_reg) / 2; cnt++)
			fw->dma_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->ctrl_status, 0x00);
		dmp_reg = &reg->risc_hw;
		for (cnt = 0; cnt < sizeof(fw->risc_hdw_reg) / 2; cnt++)
			fw->risc_hdw_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->pcr, 0x2000);
		qla2xxx_read_window(reg, 16, fw->risc_gp0_reg);

		WRT_REG_WORD(&reg->pcr, 0x2100);
		qla2xxx_read_window(reg, 16, fw->risc_gp1_reg);

		WRT_REG_WORD(&reg->pcr, 0x2200);
		qla2xxx_read_window(reg, 16, fw->risc_gp2_reg);

		WRT_REG_WORD(&reg->pcr, 0x2300);
		qla2xxx_read_window(reg, 16, fw->risc_gp3_reg);

		WRT_REG_WORD(&reg->pcr, 0x2400);
		qla2xxx_read_window(reg, 16, fw->risc_gp4_reg);

		WRT_REG_WORD(&reg->pcr, 0x2500);
		qla2xxx_read_window(reg, 16, fw->risc_gp5_reg);

		WRT_REG_WORD(&reg->pcr, 0x2600);
		qla2xxx_read_window(reg, 16, fw->risc_gp6_reg);

		WRT_REG_WORD(&reg->pcr, 0x2700);
		qla2xxx_read_window(reg, 16, fw->risc_gp7_reg);

		WRT_REG_WORD(&reg->ctrl_status, 0x10);
		qla2xxx_read_window(reg, 16, fw->frame_buf_hdw_reg);

		WRT_REG_WORD(&reg->ctrl_status, 0x20);
		qla2xxx_read_window(reg, 64, fw->fpm_b0_reg);

		WRT_REG_WORD(&reg->ctrl_status, 0x30);
		qla2xxx_read_window(reg, 64, fw->fpm_b1_reg);

		/* Reset the ISP. */
		WRT_REG_WORD(&reg->ctrl_status, CSR_ISP_SOFT_RESET);
	}

	for (cnt = 30000; RD_MAILBOX_REG(ha, reg, 0) != 0 &&
	    rval == QLA_SUCCESS; cnt--) {
		if (cnt)
			udelay(100);
		else
			rval = QLA_FUNCTION_TIMEOUT;
	}

	/* Pause RISC. */
	if (rval == QLA_SUCCESS && (IS_QLA2200(ha) || (IS_QLA2100(ha) &&
	    (RD_REG_WORD(&reg->mctr) & (BIT_1 | BIT_0)) != 0))) {

		WRT_REG_WORD(&reg->hccr, HCCR_PAUSE_RISC);
		for (cnt = 30000;
		    (RD_REG_WORD(&reg->hccr) & HCCR_RISC_PAUSE) == 0 &&
		    rval == QLA_SUCCESS; cnt--) {
			if (cnt)
				udelay(100);
			else
				rval = QLA_FUNCTION_TIMEOUT;
		}
		if (rval == QLA_SUCCESS) {
			/* Set memory configuration and timing. */
			if (IS_QLA2100(ha))
				WRT_REG_WORD(&reg->mctr, 0xf1);
			else
				WRT_REG_WORD(&reg->mctr, 0xf2);
			RD_REG_WORD(&reg->mctr);	/* PCI Posting. */

			/* Release RISC. */
			WRT_REG_WORD(&reg->hccr, HCCR_RELEASE_RISC);
		}
	}

	if (rval == QLA_SUCCESS) {
		/* Get RISC SRAM. */
		risc_address = 0x1000;
 		WRT_MAILBOX_REG(ha, reg, 0, MBC_READ_RAM_WORD);
		clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
	}
	for (cnt = 0; cnt < sizeof(fw->risc_ram) / 2 && rval == QLA_SUCCESS;
	    cnt++, risc_address++) {
 		WRT_MAILBOX_REG(ha, reg, 1, risc_address);
		WRT_REG_WORD(&reg->hccr, HCCR_SET_HOST_INT);

		for (timer = 6000000; timer != 0; timer--) {
			/* Check for pending interrupts. */
			if (RD_REG_WORD(&reg->istatus) & ISR_RISC_INT) {
				if (RD_REG_WORD(&reg->semaphore) & BIT_0) {
					set_bit(MBX_INTERRUPT,
					    &ha->mbx_cmd_flags);

					mb0 = RD_MAILBOX_REG(ha, reg, 0);
					mb2 = RD_MAILBOX_REG(ha, reg, 2);

					WRT_REG_WORD(&reg->semaphore, 0);
					WRT_REG_WORD(&reg->hccr,
					    HCCR_CLR_RISC_INT);
					RD_REG_WORD(&reg->hccr);
					break;
				}
				WRT_REG_WORD(&reg->hccr, HCCR_CLR_RISC_INT);
				RD_REG_WORD(&reg->hccr);
			}
			udelay(5);
		}

		if (test_and_clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags)) {
			rval = mb0 & MBS_MASK;
			fw->risc_ram[cnt] = htons(mb2);
		} else {
			rval = QLA_FUNCTION_FAILED;
		}
	}

	if (rval == QLA_SUCCESS)
		qla2xxx_copy_queues(ha, &fw->risc_ram[cnt]);

	if (rval != QLA_SUCCESS) {
		qla_printk(KERN_WARNING, ha,
		    "Failed to dump firmware (%x)!!!\n", rval);
		ha->fw_dumped = 0;

	} else {
		qla_printk(KERN_INFO, ha,
		    "Firmware dump saved to temp buffer (%ld/%p).\n",
		    base_vha->host_no, ha->fw_dump);
		ha->fw_dumped = 1;
	}

qla2100_fw_dump_failed:
	if (!hardware_locked)
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

void
qla24xx_fw_dump(scsi_qla_host_t *vha, int hardware_locked)
{
	int		rval;
	uint32_t	cnt;
	uint32_t	risc_address;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	uint32_t __iomem *dmp_reg;
	uint32_t	*iter_reg;
	uint16_t __iomem *mbx_reg;
	unsigned long	flags;
	struct qla24xx_fw_dump *fw;
	uint32_t	ext_mem_cnt;
	void		*nxt;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);

	risc_address = ext_mem_cnt = 0;
	flags = 0;

	if (!hardware_locked)
		spin_lock_irqsave(&ha->hardware_lock, flags);

	if (!ha->fw_dump) {
		qla_printk(KERN_WARNING, ha,
		    "No buffer available for dump!!!\n");
		goto qla24xx_fw_dump_failed;
	}

	if (ha->fw_dumped) {
		qla_printk(KERN_WARNING, ha,
		    "Firmware has been previously dumped (%p) -- ignoring "
		    "request...\n", ha->fw_dump);
		goto qla24xx_fw_dump_failed;
	}
	fw = &ha->fw_dump->isp.isp24;
	qla2xxx_prep_dump(ha, ha->fw_dump);

	fw->host_status = htonl(RD_REG_DWORD(&reg->host_status));

	/* Pause RISC. */
	rval = qla24xx_pause_risc(reg);
	if (rval != QLA_SUCCESS)
		goto qla24xx_fw_dump_failed_0;

	/* Host interface registers. */
	dmp_reg = &reg->flash_addr;
	for (cnt = 0; cnt < sizeof(fw->host_reg) / 4; cnt++)
		fw->host_reg[cnt] = htonl(RD_REG_DWORD(dmp_reg++));

	/* Disable interrupts. */
	WRT_REG_DWORD(&reg->ictrl, 0);
	RD_REG_DWORD(&reg->ictrl);

	/* Shadow registers. */
	WRT_REG_DWORD(&reg->iobase_addr, 0x0F70);
	RD_REG_DWORD(&reg->iobase_addr);
	WRT_REG_DWORD(&reg->iobase_select, 0xB0000000);
	fw->shadow_reg[0] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0100000);
	fw->shadow_reg[1] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0200000);
	fw->shadow_reg[2] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0300000);
	fw->shadow_reg[3] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0400000);
	fw->shadow_reg[4] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0500000);
	fw->shadow_reg[5] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0600000);
	fw->shadow_reg[6] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	/* Mailbox registers. */
	mbx_reg = &reg->mailbox0;
	for (cnt = 0; cnt < sizeof(fw->mailbox_reg) / 2; cnt++)
		fw->mailbox_reg[cnt] = htons(RD_REG_WORD(mbx_reg++));

	/* Transfer sequence registers. */
	iter_reg = fw->xseq_gp_reg;
	iter_reg = qla24xx_read_window(reg, 0xBF00, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBF10, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBF20, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBF30, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBF40, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBF50, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBF60, 16, iter_reg);
	qla24xx_read_window(reg, 0xBF70, 16, iter_reg);

	qla24xx_read_window(reg, 0xBFE0, 16, fw->xseq_0_reg);
	qla24xx_read_window(reg, 0xBFF0, 16, fw->xseq_1_reg);

	/* Receive sequence registers. */
	iter_reg = fw->rseq_gp_reg;
	iter_reg = qla24xx_read_window(reg, 0xFF00, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFF10, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFF20, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFF30, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFF40, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFF50, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFF60, 16, iter_reg);
	qla24xx_read_window(reg, 0xFF70, 16, iter_reg);

	qla24xx_read_window(reg, 0xFFD0, 16, fw->rseq_0_reg);
	qla24xx_read_window(reg, 0xFFE0, 16, fw->rseq_1_reg);
	qla24xx_read_window(reg, 0xFFF0, 16, fw->rseq_2_reg);

	/* Command DMA registers. */
	qla24xx_read_window(reg, 0x7100, 16, fw->cmd_dma_reg);

	/* Queues. */
	iter_reg = fw->req0_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7200, 8, iter_reg);
	dmp_reg = &reg->iobase_q;
	for (cnt = 0; cnt < 7; cnt++)
		*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

	iter_reg = fw->resp0_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7300, 8, iter_reg);
	dmp_reg = &reg->iobase_q;
	for (cnt = 0; cnt < 7; cnt++)
		*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

	iter_reg = fw->req1_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7400, 8, iter_reg);
	dmp_reg = &reg->iobase_q;
	for (cnt = 0; cnt < 7; cnt++)
		*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

	/* Transmit DMA registers. */
	iter_reg = fw->xmt0_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7600, 16, iter_reg);
	qla24xx_read_window(reg, 0x7610, 16, iter_reg);

	iter_reg = fw->xmt1_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7620, 16, iter_reg);
	qla24xx_read_window(reg, 0x7630, 16, iter_reg);

	iter_reg = fw->xmt2_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7640, 16, iter_reg);
	qla24xx_read_window(reg, 0x7650, 16, iter_reg);

	iter_reg = fw->xmt3_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7660, 16, iter_reg);
	qla24xx_read_window(reg, 0x7670, 16, iter_reg);

	iter_reg = fw->xmt4_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7680, 16, iter_reg);
	qla24xx_read_window(reg, 0x7690, 16, iter_reg);

	qla24xx_read_window(reg, 0x76A0, 16, fw->xmt_data_dma_reg);

	/* Receive DMA registers. */
	iter_reg = fw->rcvt0_data_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7700, 16, iter_reg);
	qla24xx_read_window(reg, 0x7710, 16, iter_reg);

	iter_reg = fw->rcvt1_data_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7720, 16, iter_reg);
	qla24xx_read_window(reg, 0x7730, 16, iter_reg);

	/* RISC registers. */
	iter_reg = fw->risc_gp_reg;
	iter_reg = qla24xx_read_window(reg, 0x0F00, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x0F10, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x0F20, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x0F30, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x0F40, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x0F50, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x0F60, 16, iter_reg);
	qla24xx_read_window(reg, 0x0F70, 16, iter_reg);

	/* Local memory controller registers. */
	iter_reg = fw->lmc_reg;
	iter_reg = qla24xx_read_window(reg, 0x3000, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x3010, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x3020, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x3030, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x3040, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x3050, 16, iter_reg);
	qla24xx_read_window(reg, 0x3060, 16, iter_reg);

	/* Fibre Protocol Module registers. */
	iter_reg = fw->fpm_hdw_reg;
	iter_reg = qla24xx_read_window(reg, 0x4000, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4010, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4020, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4030, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4040, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4050, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4060, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4070, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4080, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4090, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x40A0, 16, iter_reg);
	qla24xx_read_window(reg, 0x40B0, 16, iter_reg);

	/* Frame Buffer registers. */
	iter_reg = fw->fb_hdw_reg;
	iter_reg = qla24xx_read_window(reg, 0x6000, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6010, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6020, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6030, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6040, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6100, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6130, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6150, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6170, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6190, 16, iter_reg);
	qla24xx_read_window(reg, 0x61B0, 16, iter_reg);

	rval = qla24xx_soft_reset(ha);
	if (rval != QLA_SUCCESS)
		goto qla24xx_fw_dump_failed_0;

	rval = qla24xx_dump_memory(ha, fw->code_ram, sizeof(fw->code_ram),
	    &nxt);
	if (rval != QLA_SUCCESS)
		goto qla24xx_fw_dump_failed_0;

	nxt = qla2xxx_copy_queues(ha, nxt);

	qla24xx_copy_eft(ha, nxt);

qla24xx_fw_dump_failed_0:
	if (rval != QLA_SUCCESS) {
		qla_printk(KERN_WARNING, ha,
		    "Failed to dump firmware (%x)!!!\n", rval);
		ha->fw_dumped = 0;

	} else {
		qla_printk(KERN_INFO, ha,
		    "Firmware dump saved to temp buffer (%ld/%p).\n",
		    base_vha->host_no, ha->fw_dump);
		ha->fw_dumped = 1;
	}

qla24xx_fw_dump_failed:
	if (!hardware_locked)
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

void
qla25xx_fw_dump(scsi_qla_host_t *vha, int hardware_locked)
{
	int		rval;
	uint32_t	cnt;
	uint32_t	risc_address;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	uint32_t __iomem *dmp_reg;
	uint32_t	*iter_reg;
	uint16_t __iomem *mbx_reg;
	unsigned long	flags;
	struct qla25xx_fw_dump *fw;
	uint32_t	ext_mem_cnt;
	void		*nxt, *nxt_chain;
	uint32_t	*last_chain = NULL;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);

	risc_address = ext_mem_cnt = 0;
	flags = 0;

	if (!hardware_locked)
		spin_lock_irqsave(&ha->hardware_lock, flags);

	if (!ha->fw_dump) {
		qla_printk(KERN_WARNING, ha,
		    "No buffer available for dump!!!\n");
		goto qla25xx_fw_dump_failed;
	}

	if (ha->fw_dumped) {
		qla_printk(KERN_WARNING, ha,
		    "Firmware has been previously dumped (%p) -- ignoring "
		    "request...\n", ha->fw_dump);
		goto qla25xx_fw_dump_failed;
	}
	fw = &ha->fw_dump->isp.isp25;
	qla2xxx_prep_dump(ha, ha->fw_dump);
	ha->fw_dump->version = __constant_htonl(2);

	fw->host_status = htonl(RD_REG_DWORD(&reg->host_status));

	/* Pause RISC. */
	rval = qla24xx_pause_risc(reg);
	if (rval != QLA_SUCCESS)
		goto qla25xx_fw_dump_failed_0;

	/* Host/Risc registers. */
	iter_reg = fw->host_risc_reg;
	iter_reg = qla24xx_read_window(reg, 0x7000, 16, iter_reg);
	qla24xx_read_window(reg, 0x7010, 16, iter_reg);

	/* PCIe registers. */
	WRT_REG_DWORD(&reg->iobase_addr, 0x7C00);
	RD_REG_DWORD(&reg->iobase_addr);
	WRT_REG_DWORD(&reg->iobase_window, 0x01);
	dmp_reg = &reg->iobase_c4;
	fw->pcie_regs[0] = htonl(RD_REG_DWORD(dmp_reg++));
	fw->pcie_regs[1] = htonl(RD_REG_DWORD(dmp_reg++));
	fw->pcie_regs[2] = htonl(RD_REG_DWORD(dmp_reg));
	fw->pcie_regs[3] = htonl(RD_REG_DWORD(&reg->iobase_window));

	WRT_REG_DWORD(&reg->iobase_window, 0x00);
	RD_REG_DWORD(&reg->iobase_window);

	/* Host interface registers. */
	dmp_reg = &reg->flash_addr;
	for (cnt = 0; cnt < sizeof(fw->host_reg) / 4; cnt++)
		fw->host_reg[cnt] = htonl(RD_REG_DWORD(dmp_reg++));

	/* Disable interrupts. */
	WRT_REG_DWORD(&reg->ictrl, 0);
	RD_REG_DWORD(&reg->ictrl);

	/* Shadow registers. */
	WRT_REG_DWORD(&reg->iobase_addr, 0x0F70);
	RD_REG_DWORD(&reg->iobase_addr);
	WRT_REG_DWORD(&reg->iobase_select, 0xB0000000);
	fw->shadow_reg[0] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0100000);
	fw->shadow_reg[1] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0200000);
	fw->shadow_reg[2] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0300000);
	fw->shadow_reg[3] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0400000);
	fw->shadow_reg[4] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0500000);
	fw->shadow_reg[5] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0600000);
	fw->shadow_reg[6] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0700000);
	fw->shadow_reg[7] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0800000);
	fw->shadow_reg[8] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0900000);
	fw->shadow_reg[9] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0A00000);
	fw->shadow_reg[10] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	/* RISC I/O register. */
	WRT_REG_DWORD(&reg->iobase_addr, 0x0010);
	fw->risc_io_reg = htonl(RD_REG_DWORD(&reg->iobase_window));

	/* Mailbox registers. */
	mbx_reg = &reg->mailbox0;
	for (cnt = 0; cnt < sizeof(fw->mailbox_reg) / 2; cnt++)
		fw->mailbox_reg[cnt] = htons(RD_REG_WORD(mbx_reg++));

	/* Transfer sequence registers. */
	iter_reg = fw->xseq_gp_reg;
	iter_reg = qla24xx_read_window(reg, 0xBF00, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBF10, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBF20, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBF30, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBF40, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBF50, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBF60, 16, iter_reg);
	qla24xx_read_window(reg, 0xBF70, 16, iter_reg);

	iter_reg = fw->xseq_0_reg;
	iter_reg = qla24xx_read_window(reg, 0xBFC0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBFD0, 16, iter_reg);
	qla24xx_read_window(reg, 0xBFE0, 16, iter_reg);

	qla24xx_read_window(reg, 0xBFF0, 16, fw->xseq_1_reg);

	/* Receive sequence registers. */
	iter_reg = fw->rseq_gp_reg;
	iter_reg = qla24xx_read_window(reg, 0xFF00, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFF10, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFF20, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFF30, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFF40, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFF50, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFF60, 16, iter_reg);
	qla24xx_read_window(reg, 0xFF70, 16, iter_reg);

	iter_reg = fw->rseq_0_reg;
	iter_reg = qla24xx_read_window(reg, 0xFFC0, 16, iter_reg);
	qla24xx_read_window(reg, 0xFFD0, 16, iter_reg);

	qla24xx_read_window(reg, 0xFFE0, 16, fw->rseq_1_reg);
	qla24xx_read_window(reg, 0xFFF0, 16, fw->rseq_2_reg);

	/* Auxiliary sequence registers. */
	iter_reg = fw->aseq_gp_reg;
	iter_reg = qla24xx_read_window(reg, 0xB000, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB010, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB020, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB030, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB040, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB050, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB060, 16, iter_reg);
	qla24xx_read_window(reg, 0xB070, 16, iter_reg);

	iter_reg = fw->aseq_0_reg;
	iter_reg = qla24xx_read_window(reg, 0xB0C0, 16, iter_reg);
	qla24xx_read_window(reg, 0xB0D0, 16, iter_reg);

	qla24xx_read_window(reg, 0xB0E0, 16, fw->aseq_1_reg);
	qla24xx_read_window(reg, 0xB0F0, 16, fw->aseq_2_reg);

	/* Command DMA registers. */
	qla24xx_read_window(reg, 0x7100, 16, fw->cmd_dma_reg);

	/* Queues. */
	iter_reg = fw->req0_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7200, 8, iter_reg);
	dmp_reg = &reg->iobase_q;
	for (cnt = 0; cnt < 7; cnt++)
		*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

	iter_reg = fw->resp0_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7300, 8, iter_reg);
	dmp_reg = &reg->iobase_q;
	for (cnt = 0; cnt < 7; cnt++)
		*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

	iter_reg = fw->req1_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7400, 8, iter_reg);
	dmp_reg = &reg->iobase_q;
	for (cnt = 0; cnt < 7; cnt++)
		*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

	/* Transmit DMA registers. */
	iter_reg = fw->xmt0_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7600, 16, iter_reg);
	qla24xx_read_window(reg, 0x7610, 16, iter_reg);

	iter_reg = fw->xmt1_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7620, 16, iter_reg);
	qla24xx_read_window(reg, 0x7630, 16, iter_reg);

	iter_reg = fw->xmt2_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7640, 16, iter_reg);
	qla24xx_read_window(reg, 0x7650, 16, iter_reg);

	iter_reg = fw->xmt3_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7660, 16, iter_reg);
	qla24xx_read_window(reg, 0x7670, 16, iter_reg);

	iter_reg = fw->xmt4_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7680, 16, iter_reg);
	qla24xx_read_window(reg, 0x7690, 16, iter_reg);

	qla24xx_read_window(reg, 0x76A0, 16, fw->xmt_data_dma_reg);

	/* Receive DMA registers. */
	iter_reg = fw->rcvt0_data_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7700, 16, iter_reg);
	qla24xx_read_window(reg, 0x7710, 16, iter_reg);

	iter_reg = fw->rcvt1_data_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7720, 16, iter_reg);
	qla24xx_read_window(reg, 0x7730, 16, iter_reg);

	/* RISC registers. */
	iter_reg = fw->risc_gp_reg;
	iter_reg = qla24xx_read_window(reg, 0x0F00, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x0F10, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x0F20, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x0F30, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x0F40, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x0F50, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x0F60, 16, iter_reg);
	qla24xx_read_window(reg, 0x0F70, 16, iter_reg);

	/* Local memory controller registers. */
	iter_reg = fw->lmc_reg;
	iter_reg = qla24xx_read_window(reg, 0x3000, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x3010, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x3020, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x3030, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x3040, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x3050, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x3060, 16, iter_reg);
	qla24xx_read_window(reg, 0x3070, 16, iter_reg);

	/* Fibre Protocol Module registers. */
	iter_reg = fw->fpm_hdw_reg;
	iter_reg = qla24xx_read_window(reg, 0x4000, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4010, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4020, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4030, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4040, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4050, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4060, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4070, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4080, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4090, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x40A0, 16, iter_reg);
	qla24xx_read_window(reg, 0x40B0, 16, iter_reg);

	/* Frame Buffer registers. */
	iter_reg = fw->fb_hdw_reg;
	iter_reg = qla24xx_read_window(reg, 0x6000, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6010, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6020, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6030, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6040, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6100, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6130, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6150, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6170, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6190, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x61B0, 16, iter_reg);
	qla24xx_read_window(reg, 0x6F00, 16, iter_reg);

	/* Multi queue registers */
	nxt_chain = qla25xx_copy_mq(ha, (void *)ha->fw_dump + ha->chain_offset,
	    &last_chain);

	rval = qla24xx_soft_reset(ha);
	if (rval != QLA_SUCCESS)
		goto qla25xx_fw_dump_failed_0;

	rval = qla24xx_dump_memory(ha, fw->code_ram, sizeof(fw->code_ram),
	    &nxt);
	if (rval != QLA_SUCCESS)
		goto qla25xx_fw_dump_failed_0;

	nxt = qla2xxx_copy_queues(ha, nxt);

	nxt = qla24xx_copy_eft(ha, nxt);

	/* Chain entries -- started with MQ. */
	qla25xx_copy_fce(ha, nxt_chain, &last_chain);
	if (last_chain) {
		ha->fw_dump->version |= __constant_htonl(DUMP_CHAIN_VARIANT);
		*last_chain |= __constant_htonl(DUMP_CHAIN_LAST);
	}

qla25xx_fw_dump_failed_0:
	if (rval != QLA_SUCCESS) {
		qla_printk(KERN_WARNING, ha,
		    "Failed to dump firmware (%x)!!!\n", rval);
		ha->fw_dumped = 0;

	} else {
		qla_printk(KERN_INFO, ha,
		    "Firmware dump saved to temp buffer (%ld/%p).\n",
		    base_vha->host_no, ha->fw_dump);
		ha->fw_dumped = 1;
	}

qla25xx_fw_dump_failed:
	if (!hardware_locked)
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

void
qla81xx_fw_dump(scsi_qla_host_t *vha, int hardware_locked)
{
	int		rval;
	uint32_t	cnt;
	uint32_t	risc_address;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	uint32_t __iomem *dmp_reg;
	uint32_t	*iter_reg;
	uint16_t __iomem *mbx_reg;
	unsigned long	flags;
	struct qla81xx_fw_dump *fw;
	uint32_t	ext_mem_cnt;
	void		*nxt, *nxt_chain;
	uint32_t	*last_chain = NULL;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);

	risc_address = ext_mem_cnt = 0;
	flags = 0;

	if (!hardware_locked)
		spin_lock_irqsave(&ha->hardware_lock, flags);

	if (!ha->fw_dump) {
		qla_printk(KERN_WARNING, ha,
		    "No buffer available for dump!!!\n");
		goto qla81xx_fw_dump_failed;
	}

	if (ha->fw_dumped) {
		qla_printk(KERN_WARNING, ha,
		    "Firmware has been previously dumped (%p) -- ignoring "
		    "request...\n", ha->fw_dump);
		goto qla81xx_fw_dump_failed;
	}
	fw = &ha->fw_dump->isp.isp81;
	qla2xxx_prep_dump(ha, ha->fw_dump);

	fw->host_status = htonl(RD_REG_DWORD(&reg->host_status));

	/* Pause RISC. */
	rval = qla24xx_pause_risc(reg);
	if (rval != QLA_SUCCESS)
		goto qla81xx_fw_dump_failed_0;

	/* Host/Risc registers. */
	iter_reg = fw->host_risc_reg;
	iter_reg = qla24xx_read_window(reg, 0x7000, 16, iter_reg);
	qla24xx_read_window(reg, 0x7010, 16, iter_reg);

	/* PCIe registers. */
	WRT_REG_DWORD(&reg->iobase_addr, 0x7C00);
	RD_REG_DWORD(&reg->iobase_addr);
	WRT_REG_DWORD(&reg->iobase_window, 0x01);
	dmp_reg = &reg->iobase_c4;
	fw->pcie_regs[0] = htonl(RD_REG_DWORD(dmp_reg++));
	fw->pcie_regs[1] = htonl(RD_REG_DWORD(dmp_reg++));
	fw->pcie_regs[2] = htonl(RD_REG_DWORD(dmp_reg));
	fw->pcie_regs[3] = htonl(RD_REG_DWORD(&reg->iobase_window));

	WRT_REG_DWORD(&reg->iobase_window, 0x00);
	RD_REG_DWORD(&reg->iobase_window);

	/* Host interface registers. */
	dmp_reg = &reg->flash_addr;
	for (cnt = 0; cnt < sizeof(fw->host_reg) / 4; cnt++)
		fw->host_reg[cnt] = htonl(RD_REG_DWORD(dmp_reg++));

	/* Disable interrupts. */
	WRT_REG_DWORD(&reg->ictrl, 0);
	RD_REG_DWORD(&reg->ictrl);

	/* Shadow registers. */
	WRT_REG_DWORD(&reg->iobase_addr, 0x0F70);
	RD_REG_DWORD(&reg->iobase_addr);
	WRT_REG_DWORD(&reg->iobase_select, 0xB0000000);
	fw->shadow_reg[0] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0100000);
	fw->shadow_reg[1] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0200000);
	fw->shadow_reg[2] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0300000);
	fw->shadow_reg[3] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0400000);
	fw->shadow_reg[4] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0500000);
	fw->shadow_reg[5] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0600000);
	fw->shadow_reg[6] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0700000);
	fw->shadow_reg[7] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0800000);
	fw->shadow_reg[8] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0900000);
	fw->shadow_reg[9] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	WRT_REG_DWORD(&reg->iobase_select, 0xB0A00000);
	fw->shadow_reg[10] = htonl(RD_REG_DWORD(&reg->iobase_sdata));

	/* RISC I/O register. */
	WRT_REG_DWORD(&reg->iobase_addr, 0x0010);
	fw->risc_io_reg = htonl(RD_REG_DWORD(&reg->iobase_window));

	/* Mailbox registers. */
	mbx_reg = &reg->mailbox0;
	for (cnt = 0; cnt < sizeof(fw->mailbox_reg) / 2; cnt++)
		fw->mailbox_reg[cnt] = htons(RD_REG_WORD(mbx_reg++));

	/* Transfer sequence registers. */
	iter_reg = fw->xseq_gp_reg;
	iter_reg = qla24xx_read_window(reg, 0xBF00, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBF10, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBF20, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBF30, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBF40, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBF50, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBF60, 16, iter_reg);
	qla24xx_read_window(reg, 0xBF70, 16, iter_reg);

	iter_reg = fw->xseq_0_reg;
	iter_reg = qla24xx_read_window(reg, 0xBFC0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBFD0, 16, iter_reg);
	qla24xx_read_window(reg, 0xBFE0, 16, iter_reg);

	qla24xx_read_window(reg, 0xBFF0, 16, fw->xseq_1_reg);

	/* Receive sequence registers. */
	iter_reg = fw->rseq_gp_reg;
	iter_reg = qla24xx_read_window(reg, 0xFF00, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFF10, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFF20, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFF30, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFF40, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFF50, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFF60, 16, iter_reg);
	qla24xx_read_window(reg, 0xFF70, 16, iter_reg);

	iter_reg = fw->rseq_0_reg;
	iter_reg = qla24xx_read_window(reg, 0xFFC0, 16, iter_reg);
	qla24xx_read_window(reg, 0xFFD0, 16, iter_reg);

	qla24xx_read_window(reg, 0xFFE0, 16, fw->rseq_1_reg);
	qla24xx_read_window(reg, 0xFFF0, 16, fw->rseq_2_reg);

	/* Auxiliary sequence registers. */
	iter_reg = fw->aseq_gp_reg;
	iter_reg = qla24xx_read_window(reg, 0xB000, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB010, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB020, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB030, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB040, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB050, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB060, 16, iter_reg);
	qla24xx_read_window(reg, 0xB070, 16, iter_reg);

	iter_reg = fw->aseq_0_reg;
	iter_reg = qla24xx_read_window(reg, 0xB0C0, 16, iter_reg);
	qla24xx_read_window(reg, 0xB0D0, 16, iter_reg);

	qla24xx_read_window(reg, 0xB0E0, 16, fw->aseq_1_reg);
	qla24xx_read_window(reg, 0xB0F0, 16, fw->aseq_2_reg);

	/* Command DMA registers. */
	qla24xx_read_window(reg, 0x7100, 16, fw->cmd_dma_reg);

	/* Queues. */
	iter_reg = fw->req0_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7200, 8, iter_reg);
	dmp_reg = &reg->iobase_q;
	for (cnt = 0; cnt < 7; cnt++)
		*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

	iter_reg = fw->resp0_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7300, 8, iter_reg);
	dmp_reg = &reg->iobase_q;
	for (cnt = 0; cnt < 7; cnt++)
		*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

	iter_reg = fw->req1_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7400, 8, iter_reg);
	dmp_reg = &reg->iobase_q;
	for (cnt = 0; cnt < 7; cnt++)
		*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

	/* Transmit DMA registers. */
	iter_reg = fw->xmt0_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7600, 16, iter_reg);
	qla24xx_read_window(reg, 0x7610, 16, iter_reg);

	iter_reg = fw->xmt1_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7620, 16, iter_reg);
	qla24xx_read_window(reg, 0x7630, 16, iter_reg);

	iter_reg = fw->xmt2_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7640, 16, iter_reg);
	qla24xx_read_window(reg, 0x7650, 16, iter_reg);

	iter_reg = fw->xmt3_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7660, 16, iter_reg);
	qla24xx_read_window(reg, 0x7670, 16, iter_reg);

	iter_reg = fw->xmt4_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7680, 16, iter_reg);
	qla24xx_read_window(reg, 0x7690, 16, iter_reg);

	qla24xx_read_window(reg, 0x76A0, 16, fw->xmt_data_dma_reg);

	/* Receive DMA registers. */
	iter_reg = fw->rcvt0_data_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7700, 16, iter_reg);
	qla24xx_read_window(reg, 0x7710, 16, iter_reg);

	iter_reg = fw->rcvt1_data_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7720, 16, iter_reg);
	qla24xx_read_window(reg, 0x7730, 16, iter_reg);

	/* RISC registers. */
	iter_reg = fw->risc_gp_reg;
	iter_reg = qla24xx_read_window(reg, 0x0F00, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x0F10, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x0F20, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x0F30, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x0F40, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x0F50, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x0F60, 16, iter_reg);
	qla24xx_read_window(reg, 0x0F70, 16, iter_reg);

	/* Local memory controller registers. */
	iter_reg = fw->lmc_reg;
	iter_reg = qla24xx_read_window(reg, 0x3000, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x3010, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x3020, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x3030, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x3040, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x3050, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x3060, 16, iter_reg);
	qla24xx_read_window(reg, 0x3070, 16, iter_reg);

	/* Fibre Protocol Module registers. */
	iter_reg = fw->fpm_hdw_reg;
	iter_reg = qla24xx_read_window(reg, 0x4000, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4010, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4020, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4030, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4040, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4050, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4060, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4070, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4080, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x4090, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x40A0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x40B0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x40C0, 16, iter_reg);
	qla24xx_read_window(reg, 0x40D0, 16, iter_reg);

	/* Frame Buffer registers. */
	iter_reg = fw->fb_hdw_reg;
	iter_reg = qla24xx_read_window(reg, 0x6000, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6010, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6020, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6030, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6040, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6100, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6130, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6150, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6170, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6190, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x61B0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x61C0, 16, iter_reg);
	qla24xx_read_window(reg, 0x6F00, 16, iter_reg);

	/* Multi queue registers */
	nxt_chain = qla25xx_copy_mq(ha, (void *)ha->fw_dump + ha->chain_offset,
	    &last_chain);

	rval = qla24xx_soft_reset(ha);
	if (rval != QLA_SUCCESS)
		goto qla81xx_fw_dump_failed_0;

	rval = qla24xx_dump_memory(ha, fw->code_ram, sizeof(fw->code_ram),
	    &nxt);
	if (rval != QLA_SUCCESS)
		goto qla81xx_fw_dump_failed_0;

	nxt = qla2xxx_copy_queues(ha, nxt);

	nxt = qla24xx_copy_eft(ha, nxt);

	/* Chain entries -- started with MQ. */
	qla25xx_copy_fce(ha, nxt_chain, &last_chain);
	if (last_chain) {
		ha->fw_dump->version |= __constant_htonl(DUMP_CHAIN_VARIANT);
		*last_chain |= __constant_htonl(DUMP_CHAIN_LAST);
	}

qla81xx_fw_dump_failed_0:
	if (rval != QLA_SUCCESS) {
		qla_printk(KERN_WARNING, ha,
		    "Failed to dump firmware (%x)!!!\n", rval);
		ha->fw_dumped = 0;

	} else {
		qla_printk(KERN_INFO, ha,
		    "Firmware dump saved to temp buffer (%ld/%p).\n",
		    base_vha->host_no, ha->fw_dump);
		ha->fw_dumped = 1;
	}

qla81xx_fw_dump_failed:
	if (!hardware_locked)
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

/****************************************************************************/
/*                         Driver Debug Functions.                          */
/****************************************************************************/

void
qla2x00_dump_regs(scsi_qla_host_t *vha)
{
	int i;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	struct device_reg_24xx __iomem *reg24 = &ha->iobase->isp24;
	uint16_t __iomem *mbx_reg;

	mbx_reg = IS_FWI2_CAPABLE(ha) ? &reg24->mailbox0:
	    MAILBOX_REG(ha, reg, 0);

	printk("Mailbox registers:\n");
	for (i = 0; i < 6; i++)
		printk("scsi(%ld): mbox %d 0x%04x \n", vha->host_no, i,
		    RD_REG_WORD(mbx_reg++));
}


void
qla2x00_dump_buffer(uint8_t * b, uint32_t size)
{
	uint32_t cnt;
	uint8_t c;

	printk(" 0   1   2   3   4   5   6   7   8   9  "
	    "Ah  Bh  Ch  Dh  Eh  Fh\n");
	printk("----------------------------------------"
	    "----------------------\n");

	for (cnt = 0; cnt < size;) {
		c = *b++;
		printk("%02x",(uint32_t) c);
		cnt++;
		if (!(cnt % 16))
			printk("\n");
		else
			printk("  ");
	}
	if (cnt % 16)
		printk("\n");
}


