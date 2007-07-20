/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"

#include <linux/delay.h>

static inline void
qla2xxx_prep_dump(scsi_qla_host_t *ha, struct qla2xxx_fw_dump *fw_dump)
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
qla2xxx_copy_queues(scsi_qla_host_t *ha, void *ptr)
{
	/* Request queue. */
	memcpy(ptr, ha->request_ring, ha->request_q_length *
	    sizeof(request_t));

	/* Response queue. */
	ptr += ha->request_q_length * sizeof(request_t);
	memcpy(ptr, ha->response_ring, ha->response_q_length  *
	    sizeof(response_t));

	return ptr + (ha->response_q_length * sizeof(response_t));
}

static int
qla2xxx_dump_memory(scsi_qla_host_t *ha, uint32_t *code_ram,
    uint32_t cram_size, uint32_t *ext_mem, void **nxt)
{
	int rval;
	uint32_t cnt, stat, timer, risc_address, ext_mem_cnt;
	uint16_t mb[4];
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	rval = QLA_SUCCESS;
	risc_address = ext_mem_cnt = 0;
	memset(mb, 0, sizeof(mb));

	/* Code RAM. */
	risc_address = 0x20000;
	WRT_REG_WORD(&reg->mailbox0, MBC_READ_RAM_EXTENDED);
	clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

	for (cnt = 0; cnt < cram_size / 4 && rval == QLA_SUCCESS;
	    cnt++, risc_address++) {
		WRT_REG_WORD(&reg->mailbox1, LSW(risc_address));
		WRT_REG_WORD(&reg->mailbox8, MSW(risc_address));
		RD_REG_WORD(&reg->mailbox8);
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

					mb[0] = RD_REG_WORD(&reg->mailbox0);
					mb[2] = RD_REG_WORD(&reg->mailbox2);
					mb[3] = RD_REG_WORD(&reg->mailbox3);

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
			rval = mb[0] & MBS_MASK;
			code_ram[cnt] = htonl((mb[3] << 16) | mb[2]);
		} else {
			rval = QLA_FUNCTION_FAILED;
		}
	}

	if (rval == QLA_SUCCESS) {
		/* External Memory. */
		risc_address = 0x100000;
		ext_mem_cnt = ha->fw_memory_size - 0x100000 + 1;
		WRT_REG_WORD(&reg->mailbox0, MBC_READ_RAM_EXTENDED);
		clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
	}
	for (cnt = 0; cnt < ext_mem_cnt && rval == QLA_SUCCESS;
	    cnt++, risc_address++) {
		WRT_REG_WORD(&reg->mailbox1, LSW(risc_address));
		WRT_REG_WORD(&reg->mailbox8, MSW(risc_address));
		RD_REG_WORD(&reg->mailbox8);
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

					mb[0] = RD_REG_WORD(&reg->mailbox0);
					mb[2] = RD_REG_WORD(&reg->mailbox2);
					mb[3] = RD_REG_WORD(&reg->mailbox3);

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
			rval = mb[0] & MBS_MASK;
			ext_mem[cnt] = htonl((mb[3] << 16) | mb[2]);
		} else {
			rval = QLA_FUNCTION_FAILED;
		}
	}

	*nxt = rval == QLA_SUCCESS ? &ext_mem[cnt]: NULL;
	return rval;
}

/**
 * qla2300_fw_dump() - Dumps binary data from the 2300 firmware.
 * @ha: HA context
 * @hardware_locked: Called with the hardware_lock
 */
void
qla2300_fw_dump(scsi_qla_host_t *ha, int hardware_locked)
{
	int		rval;
	uint32_t	cnt, timer;
	uint32_t	risc_address;
	uint16_t	mb0, mb2;

	uint32_t	stat;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	uint16_t __iomem *dmp_reg;
	unsigned long	flags;
	struct qla2300_fw_dump	*fw;
	uint32_t	data_ram_cnt;

	risc_address = data_ram_cnt = 0;
	mb0 = mb2 = 0;
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
		dmp_reg = (uint16_t __iomem *)(reg + 0);
		for (cnt = 0; cnt < sizeof(fw->pbiu_reg) / 2; cnt++)
			fw->pbiu_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x10);
		for (cnt = 0; cnt < sizeof(fw->risc_host_reg) / 2; cnt++)
			fw->risc_host_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x40);
		for (cnt = 0; cnt < sizeof(fw->mailbox_reg) / 2; cnt++)
			fw->mailbox_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->ctrl_status, 0x40);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->resp_dma_reg) / 2; cnt++)
			fw->resp_dma_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->ctrl_status, 0x50);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->dma_reg) / 2; cnt++)
			fw->dma_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->ctrl_status, 0x00);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0xA0);
		for (cnt = 0; cnt < sizeof(fw->risc_hdw_reg) / 2; cnt++)
			fw->risc_hdw_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->pcr, 0x2000);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp0_reg) / 2; cnt++)
			fw->risc_gp0_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->pcr, 0x2200);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp1_reg) / 2; cnt++)
			fw->risc_gp1_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->pcr, 0x2400);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp2_reg) / 2; cnt++)
			fw->risc_gp2_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->pcr, 0x2600);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp3_reg) / 2; cnt++)
			fw->risc_gp3_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->pcr, 0x2800);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp4_reg) / 2; cnt++)
			fw->risc_gp4_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->pcr, 0x2A00);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp5_reg) / 2; cnt++)
			fw->risc_gp5_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->pcr, 0x2C00);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp6_reg) / 2; cnt++)
			fw->risc_gp6_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->pcr, 0x2E00);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp7_reg) / 2; cnt++)
			fw->risc_gp7_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->ctrl_status, 0x10);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->frame_buf_hdw_reg) / 2; cnt++)
			fw->frame_buf_hdw_reg[cnt] =
			    htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->ctrl_status, 0x20);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->fpm_b0_reg) / 2; cnt++)
			fw->fpm_b0_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->ctrl_status, 0x30);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->fpm_b1_reg) / 2; cnt++)
			fw->fpm_b1_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

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

	if (rval == QLA_SUCCESS) {
		/* Get RISC SRAM. */
		risc_address = 0x800;
 		WRT_MAILBOX_REG(ha, reg, 0, MBC_READ_RAM_WORD);
		clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
	}
	for (cnt = 0; cnt < sizeof(fw->risc_ram) / 2 && rval == QLA_SUCCESS;
	    cnt++, risc_address++) {
 		WRT_MAILBOX_REG(ha, reg, 1, (uint16_t)risc_address);
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
					mb2 = RD_MAILBOX_REG(ha, reg, 2);

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
					mb2 = RD_MAILBOX_REG(ha, reg, 2);

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
			fw->risc_ram[cnt] = htons(mb2);
		} else {
			rval = QLA_FUNCTION_FAILED;
		}
	}

	if (rval == QLA_SUCCESS) {
		/* Get stack SRAM. */
		risc_address = 0x10000;
 		WRT_MAILBOX_REG(ha, reg, 0, MBC_READ_RAM_EXTENDED);
		clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
	}
	for (cnt = 0; cnt < sizeof(fw->stack_ram) / 2 && rval == QLA_SUCCESS;
	    cnt++, risc_address++) {
 		WRT_MAILBOX_REG(ha, reg, 1, LSW(risc_address));
 		WRT_MAILBOX_REG(ha, reg, 8, MSW(risc_address));
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
					mb2 = RD_MAILBOX_REG(ha, reg, 2);

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
					mb2 = RD_MAILBOX_REG(ha, reg, 2);

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
			fw->stack_ram[cnt] = htons(mb2);
		} else {
			rval = QLA_FUNCTION_FAILED;
		}
	}

	if (rval == QLA_SUCCESS) {
		/* Get data SRAM. */
		risc_address = 0x11000;
		data_ram_cnt = ha->fw_memory_size - risc_address + 1;
 		WRT_MAILBOX_REG(ha, reg, 0, MBC_READ_RAM_EXTENDED);
		clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
	}
	for (cnt = 0; cnt < data_ram_cnt && rval == QLA_SUCCESS;
	    cnt++, risc_address++) {
 		WRT_MAILBOX_REG(ha, reg, 1, LSW(risc_address));
 		WRT_MAILBOX_REG(ha, reg, 8, MSW(risc_address));
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
					mb2 = RD_MAILBOX_REG(ha, reg, 2);

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
					mb2 = RD_MAILBOX_REG(ha, reg, 2);

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
			fw->data_ram[cnt] = htons(mb2);
		} else {
			rval = QLA_FUNCTION_FAILED;
		}
	}

	if (rval == QLA_SUCCESS)
		qla2xxx_copy_queues(ha, &fw->data_ram[cnt]);

	if (rval != QLA_SUCCESS) {
		qla_printk(KERN_WARNING, ha,
		    "Failed to dump firmware (%x)!!!\n", rval);
		ha->fw_dumped = 0;

	} else {
		qla_printk(KERN_INFO, ha,
		    "Firmware dump saved to temp buffer (%ld/%p).\n",
		    ha->host_no, ha->fw_dump);
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
qla2100_fw_dump(scsi_qla_host_t *ha, int hardware_locked)
{
	int		rval;
	uint32_t	cnt, timer;
	uint16_t	risc_address;
	uint16_t	mb0, mb2;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	uint16_t __iomem *dmp_reg;
	unsigned long	flags;
	struct qla2100_fw_dump	*fw;

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
		dmp_reg = (uint16_t __iomem *)(reg + 0);
		for (cnt = 0; cnt < sizeof(fw->pbiu_reg) / 2; cnt++)
			fw->pbiu_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x10);
		for (cnt = 0; cnt < ha->mbx_count; cnt++) {
			if (cnt == 8) {
				dmp_reg = (uint16_t __iomem *)
					((uint8_t __iomem *)reg + 0xe0);
			}
			fw->mailbox_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));
		}

		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x20);
		for (cnt = 0; cnt < sizeof(fw->dma_reg) / 2; cnt++)
			fw->dma_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->ctrl_status, 0x00);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0xA0);
		for (cnt = 0; cnt < sizeof(fw->risc_hdw_reg) / 2; cnt++)
			fw->risc_hdw_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->pcr, 0x2000);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp0_reg) / 2; cnt++)
			fw->risc_gp0_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->pcr, 0x2100);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp1_reg) / 2; cnt++)
			fw->risc_gp1_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->pcr, 0x2200);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp2_reg) / 2; cnt++)
			fw->risc_gp2_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->pcr, 0x2300);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp3_reg) / 2; cnt++)
			fw->risc_gp3_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->pcr, 0x2400);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp4_reg) / 2; cnt++)
			fw->risc_gp4_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->pcr, 0x2500);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp5_reg) / 2; cnt++)
			fw->risc_gp5_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->pcr, 0x2600);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp6_reg) / 2; cnt++)
			fw->risc_gp6_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->pcr, 0x2700);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp7_reg) / 2; cnt++)
			fw->risc_gp7_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->ctrl_status, 0x10);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->frame_buf_hdw_reg) / 2; cnt++)
			fw->frame_buf_hdw_reg[cnt] =
			    htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->ctrl_status, 0x20);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->fpm_b0_reg) / 2; cnt++)
			fw->fpm_b0_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

		WRT_REG_WORD(&reg->ctrl_status, 0x30);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->fpm_b1_reg) / 2; cnt++)
			fw->fpm_b1_reg[cnt] = htons(RD_REG_WORD(dmp_reg++));

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
		    ha->host_no, ha->fw_dump);
		ha->fw_dumped = 1;
	}

qla2100_fw_dump_failed:
	if (!hardware_locked)
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

void
qla24xx_fw_dump(scsi_qla_host_t *ha, int hardware_locked)
{
	int		rval;
	uint32_t	cnt;
	uint32_t	risc_address;
	uint16_t	mb0, wd;

	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	uint32_t __iomem *dmp_reg;
	uint32_t	*iter_reg;
	uint16_t __iomem *mbx_reg;
	unsigned long	flags;
	struct qla24xx_fw_dump *fw;
	uint32_t	ext_mem_cnt;
	void		*nxt;

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

	rval = QLA_SUCCESS;
	fw->host_status = htonl(RD_REG_DWORD(&reg->host_status));

	/* Pause RISC. */
	if ((RD_REG_DWORD(&reg->hccr) & HCCRX_RISC_PAUSE) == 0) {
		WRT_REG_DWORD(&reg->hccr, HCCRX_SET_RISC_RESET |
		    HCCRX_CLR_HOST_INT);
		RD_REG_DWORD(&reg->hccr);		/* PCI Posting. */
		WRT_REG_DWORD(&reg->hccr, HCCRX_SET_RISC_PAUSE);
		for (cnt = 30000;
		    (RD_REG_DWORD(&reg->hccr) & HCCRX_RISC_PAUSE) == 0 &&
		    rval == QLA_SUCCESS; cnt--) {
			if (cnt)
				udelay(100);
			else
				rval = QLA_FUNCTION_TIMEOUT;
		}
	}

	if (rval == QLA_SUCCESS) {
		/* Host interface registers. */
		dmp_reg = (uint32_t __iomem *)(reg + 0);
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
		WRT_REG_DWORD(&reg->iobase_addr, 0xBF00);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF10);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF20);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF30);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF40);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF50);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF60);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF70);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBFE0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < sizeof(fw->xseq_0_reg) / 4; cnt++)
			fw->xseq_0_reg[cnt] = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBFF0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < sizeof(fw->xseq_1_reg) / 4; cnt++)
			fw->xseq_1_reg[cnt] = htonl(RD_REG_DWORD(dmp_reg++));

		/* Receive sequence registers. */
		iter_reg = fw->rseq_gp_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0xFF00);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF10);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF20);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF30);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF40);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF50);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF60);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF70);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFFD0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < sizeof(fw->rseq_0_reg) / 4; cnt++)
			fw->rseq_0_reg[cnt] = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFFE0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < sizeof(fw->rseq_1_reg) / 4; cnt++)
			fw->rseq_1_reg[cnt] = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFFF0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < sizeof(fw->rseq_2_reg) / 4; cnt++)
			fw->rseq_2_reg[cnt] = htonl(RD_REG_DWORD(dmp_reg++));

		/* Command DMA registers. */
		WRT_REG_DWORD(&reg->iobase_addr, 0x7100);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < sizeof(fw->cmd_dma_reg) / 4; cnt++)
			fw->cmd_dma_reg[cnt] = htonl(RD_REG_DWORD(dmp_reg++));

		/* Queues. */
		iter_reg = fw->req0_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7200);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 8; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		dmp_reg = &reg->iobase_q;
		for (cnt = 0; cnt < 7; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		iter_reg = fw->resp0_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7300);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 8; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		dmp_reg = &reg->iobase_q;
		for (cnt = 0; cnt < 7; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		iter_reg = fw->req1_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7400);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 8; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		dmp_reg = &reg->iobase_q;
		for (cnt = 0; cnt < 7; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		/* Transmit DMA registers. */
		iter_reg = fw->xmt0_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7600);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x7610);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		iter_reg = fw->xmt1_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7620);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x7630);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		iter_reg = fw->xmt2_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7640);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x7650);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		iter_reg = fw->xmt3_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7660);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x7670);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		iter_reg = fw->xmt4_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7680);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x7690);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x76A0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < sizeof(fw->xmt_data_dma_reg) / 4; cnt++)
			fw->xmt_data_dma_reg[cnt] =
			    htonl(RD_REG_DWORD(dmp_reg++));

		/* Receive DMA registers. */
		iter_reg = fw->rcvt0_data_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7700);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x7710);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		iter_reg = fw->rcvt1_data_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7720);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x7730);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		/* RISC registers. */
		iter_reg = fw->risc_gp_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x0F00);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F10);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F20);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F30);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F40);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F50);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F60);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F70);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		/* Local memory controller registers. */
		iter_reg = fw->lmc_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x3000);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x3010);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x3020);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x3030);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x3040);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x3050);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x3060);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		/* Fibre Protocol Module registers. */
		iter_reg = fw->fpm_hdw_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x4000);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x4010);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x4020);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x4030);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x4040);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x4050);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x4060);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x4070);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x4080);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x4090);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x40A0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x40B0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		/* Frame Buffer registers. */
		iter_reg = fw->fb_hdw_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x6000);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6010);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6020);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6030);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6040);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6100);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6130);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6150);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6170);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6190);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x61B0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		/* Reset RISC. */
		WRT_REG_DWORD(&reg->ctrl_status,
		    CSRX_DMA_SHUTDOWN|MWB_4096_BYTES);
		for (cnt = 0; cnt < 30000; cnt++) {
			if ((RD_REG_DWORD(&reg->ctrl_status) &
			    CSRX_DMA_ACTIVE) == 0)
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
	}

	for (cnt = 30000; RD_REG_WORD(&reg->mailbox0) != 0 &&
	    rval == QLA_SUCCESS; cnt--) {
		if (cnt)
			udelay(100);
		else
			rval = QLA_FUNCTION_TIMEOUT;
	}

	if (rval == QLA_SUCCESS)
		rval = qla2xxx_dump_memory(ha, fw->code_ram,
		    sizeof(fw->code_ram), fw->ext_mem, &nxt);

	if (rval == QLA_SUCCESS) {
		nxt = qla2xxx_copy_queues(ha, nxt);
		if (ha->eft)
			memcpy(nxt, ha->eft, ntohl(ha->fw_dump->eft_size));
	}

	if (rval != QLA_SUCCESS) {
		qla_printk(KERN_WARNING, ha,
		    "Failed to dump firmware (%x)!!!\n", rval);
		ha->fw_dumped = 0;

	} else {
		qla_printk(KERN_INFO, ha,
		    "Firmware dump saved to temp buffer (%ld/%p).\n",
		    ha->host_no, ha->fw_dump);
		ha->fw_dumped = 1;
	}

qla24xx_fw_dump_failed:
	if (!hardware_locked)
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

void
qla25xx_fw_dump(scsi_qla_host_t *ha, int hardware_locked)
{
	int		rval;
	uint32_t	cnt;
	uint32_t	risc_address;
	uint16_t	mb0, wd;

	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	uint32_t __iomem *dmp_reg;
	uint32_t	*iter_reg;
	uint16_t __iomem *mbx_reg;
	unsigned long	flags;
	struct qla25xx_fw_dump *fw;
	uint32_t	ext_mem_cnt;
	void		*nxt;

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

	rval = QLA_SUCCESS;
	fw->host_status = htonl(RD_REG_DWORD(&reg->host_status));

	/* Pause RISC. */
	if ((RD_REG_DWORD(&reg->hccr) & HCCRX_RISC_PAUSE) == 0) {
		WRT_REG_DWORD(&reg->hccr, HCCRX_SET_RISC_RESET |
		    HCCRX_CLR_HOST_INT);
		RD_REG_DWORD(&reg->hccr);		/* PCI Posting. */
		WRT_REG_DWORD(&reg->hccr, HCCRX_SET_RISC_PAUSE);
		for (cnt = 30000;
		    (RD_REG_DWORD(&reg->hccr) & HCCRX_RISC_PAUSE) == 0 &&
		    rval == QLA_SUCCESS; cnt--) {
			if (cnt)
				udelay(100);
			else
				rval = QLA_FUNCTION_TIMEOUT;
		}
	}

	if (rval == QLA_SUCCESS) {
		/* Host interface registers. */
		dmp_reg = (uint32_t __iomem *)(reg + 0);
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
		RD_REG_DWORD(&reg->iobase_addr);
		fw->risc_io_reg = htonl(RD_REG_DWORD(&reg->iobase_window));

		/* Mailbox registers. */
		mbx_reg = &reg->mailbox0;
		for (cnt = 0; cnt < sizeof(fw->mailbox_reg) / 2; cnt++)
			fw->mailbox_reg[cnt] = htons(RD_REG_WORD(mbx_reg++));

		/* Transfer sequence registers. */
		iter_reg = fw->xseq_gp_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0xBF00);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF10);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF20);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF30);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF40);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF50);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF60);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBF70);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		iter_reg = fw->xseq_0_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0xBFC0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBFD0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBFE0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xBFF0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < sizeof(fw->xseq_1_reg) / 4; cnt++)
			fw->xseq_1_reg[cnt] = htonl(RD_REG_DWORD(dmp_reg++));

		/* Receive sequence registers. */
		iter_reg = fw->rseq_gp_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0xFF00);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF10);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF20);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF30);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF40);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF50);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF60);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFF70);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		iter_reg = fw->rseq_0_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0xFFC0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFFD0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFFE0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < sizeof(fw->rseq_1_reg) / 4; cnt++)
			fw->rseq_1_reg[cnt] = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xFFF0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < sizeof(fw->rseq_2_reg) / 4; cnt++)
			fw->rseq_2_reg[cnt] = htonl(RD_REG_DWORD(dmp_reg++));

		/* Auxiliary sequence registers. */
		iter_reg = fw->aseq_gp_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0xB000);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xB010);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xB020);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xB030);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xB040);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xB050);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xB060);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xB070);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		iter_reg = fw->aseq_0_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0xB0C0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xB0D0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xB0E0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < sizeof(fw->aseq_1_reg) / 4; cnt++)
			fw->aseq_1_reg[cnt] = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0xB0F0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < sizeof(fw->aseq_2_reg) / 4; cnt++)
			fw->aseq_2_reg[cnt] = htonl(RD_REG_DWORD(dmp_reg++));

		/* Command DMA registers. */
		WRT_REG_DWORD(&reg->iobase_addr, 0x7100);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < sizeof(fw->cmd_dma_reg) / 4; cnt++)
			fw->cmd_dma_reg[cnt] = htonl(RD_REG_DWORD(dmp_reg++));

		/* Queues. */
		iter_reg = fw->req0_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7200);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 8; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		dmp_reg = &reg->iobase_q;
		for (cnt = 0; cnt < 7; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		iter_reg = fw->resp0_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7300);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 8; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		dmp_reg = &reg->iobase_q;
		for (cnt = 0; cnt < 7; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		iter_reg = fw->req1_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7400);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 8; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		dmp_reg = &reg->iobase_q;
		for (cnt = 0; cnt < 7; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		/* Transmit DMA registers. */
		iter_reg = fw->xmt0_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7600);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x7610);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		iter_reg = fw->xmt1_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7620);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x7630);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		iter_reg = fw->xmt2_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7640);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x7650);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		iter_reg = fw->xmt3_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7660);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x7670);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		iter_reg = fw->xmt4_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7680);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x7690);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x76A0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < sizeof(fw->xmt_data_dma_reg) / 4; cnt++)
			fw->xmt_data_dma_reg[cnt] =
			    htonl(RD_REG_DWORD(dmp_reg++));

		/* Receive DMA registers. */
		iter_reg = fw->rcvt0_data_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7700);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x7710);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		iter_reg = fw->rcvt1_data_dma_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x7720);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x7730);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		/* RISC registers. */
		iter_reg = fw->risc_gp_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x0F00);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F10);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F20);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F30);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F40);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F50);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F60);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x0F70);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		/* Local memory controller registers. */
		iter_reg = fw->lmc_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x3000);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x3010);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x3020);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x3030);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x3040);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x3050);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x3060);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x3070);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		/* Fibre Protocol Module registers. */
		iter_reg = fw->fpm_hdw_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x4000);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x4010);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x4020);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x4030);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x4040);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x4050);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x4060);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x4070);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x4080);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x4090);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x40A0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x40B0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		/* Frame Buffer registers. */
		iter_reg = fw->fb_hdw_reg;
		WRT_REG_DWORD(&reg->iobase_addr, 0x6000);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6010);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6020);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6030);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6040);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6100);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6130);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6150);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6170);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6190);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x61B0);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		WRT_REG_DWORD(&reg->iobase_addr, 0x6F00);
		dmp_reg = &reg->iobase_window;
		for (cnt = 0; cnt < 16; cnt++)
			*iter_reg++ = htonl(RD_REG_DWORD(dmp_reg++));

		/* Reset RISC. */
		WRT_REG_DWORD(&reg->ctrl_status,
		    CSRX_DMA_SHUTDOWN|MWB_4096_BYTES);
		for (cnt = 0; cnt < 30000; cnt++) {
			if ((RD_REG_DWORD(&reg->ctrl_status) &
			    CSRX_DMA_ACTIVE) == 0)
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
	}

	for (cnt = 30000; RD_REG_WORD(&reg->mailbox0) != 0 &&
	    rval == QLA_SUCCESS; cnt--) {
		if (cnt)
			udelay(100);
		else
			rval = QLA_FUNCTION_TIMEOUT;
	}

	if (rval == QLA_SUCCESS)
		rval = qla2xxx_dump_memory(ha, fw->code_ram,
		    sizeof(fw->code_ram), fw->ext_mem, &nxt);

	if (rval == QLA_SUCCESS) {
		nxt = qla2xxx_copy_queues(ha, nxt);
		if (ha->eft)
			memcpy(nxt, ha->eft, ntohl(ha->fw_dump->eft_size));
	}

	if (rval != QLA_SUCCESS) {
		qla_printk(KERN_WARNING, ha,
		    "Failed to dump firmware (%x)!!!\n", rval);
		ha->fw_dumped = 0;

	} else {
		qla_printk(KERN_INFO, ha,
		    "Firmware dump saved to temp buffer (%ld/%p).\n",
		    ha->host_no, ha->fw_dump);
		ha->fw_dumped = 1;
	}

qla25xx_fw_dump_failed:
	if (!hardware_locked)
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

/****************************************************************************/
/*                         Driver Debug Functions.                          */
/****************************************************************************/

void
qla2x00_dump_regs(scsi_qla_host_t *ha)
{
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;

	printk("Mailbox registers:\n");
	printk("scsi(%ld): mbox 0 0x%04x \n",
	    ha->host_no, RD_MAILBOX_REG(ha, reg, 0));
	printk("scsi(%ld): mbox 1 0x%04x \n",
	    ha->host_no, RD_MAILBOX_REG(ha, reg, 1));
	printk("scsi(%ld): mbox 2 0x%04x \n",
	    ha->host_no, RD_MAILBOX_REG(ha, reg, 2));
	printk("scsi(%ld): mbox 3 0x%04x \n",
	    ha->host_no, RD_MAILBOX_REG(ha, reg, 3));
	printk("scsi(%ld): mbox 4 0x%04x \n",
	    ha->host_no, RD_MAILBOX_REG(ha, reg, 4));
	printk("scsi(%ld): mbox 5 0x%04x \n",
	    ha->host_no, RD_MAILBOX_REG(ha, reg, 5));
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

/**************************************************************************
 *   qla2x00_print_scsi_cmd
 *	 Dumps out info about the scsi cmd and srb.
 *   Input
 *	 cmd : struct scsi_cmnd
 **************************************************************************/
void
qla2x00_print_scsi_cmd(struct scsi_cmnd * cmd)
{
	int i;
	struct scsi_qla_host *ha;
	srb_t *sp;

	ha = (struct scsi_qla_host *)cmd->device->host->hostdata;

	sp = (srb_t *) cmd->SCp.ptr;
	printk("SCSI Command @=0x%p, Handle=0x%p\n", cmd, cmd->host_scribble);
	printk("  chan=0x%02x, target=0x%02x, lun=0x%02x, cmd_len=0x%02x\n",
	    cmd->device->channel, cmd->device->id, cmd->device->lun,
	    cmd->cmd_len);
	printk(" CDB: ");
	for (i = 0; i < cmd->cmd_len; i++) {
		printk("0x%02x ", cmd->cmnd[i]);
	}
	printk("\n  seg_cnt=%d, allowed=%d, retries=%d\n",
	       scsi_sg_count(cmd), cmd->allowed, cmd->retries);
	printk("  request buffer=0x%p, request buffer len=0x%x\n",
	       scsi_sglist(cmd), scsi_bufflen(cmd));
	printk("  tag=%d, transfersize=0x%x\n",
	    cmd->tag, cmd->transfersize);
	printk("  serial_number=%lx, SP=%p\n", cmd->serial_number, sp);
	printk("  data direction=%d\n", cmd->sc_data_direction);

	if (!sp)
		return;

	printk("  sp flags=0x%x\n", sp->flags);
}

void
qla2x00_dump_pkt(void *pkt)
{
	uint32_t i;
	uint8_t *data = (uint8_t *) pkt;

	for (i = 0; i < 64; i++) {
		if (!(i % 4))
			printk("\n%02x: ", i);

		printk("%02x ", data[i]);
	}
	printk("\n");
}

#if defined(QL_DEBUG_ROUTINES)
/*
 * qla2x00_formatted_dump_buffer
 *       Prints string plus buffer.
 *
 * Input:
 *       string  = Null terminated string (no newline at end).
 *       buffer  = buffer address.
 *       wd_size = word size 8, 16, 32 or 64 bits
 *       count   = number of words.
 */
void
qla2x00_formatted_dump_buffer(char *string, uint8_t * buffer,
				uint8_t wd_size, uint32_t count)
{
	uint32_t cnt;
	uint16_t *buf16;
	uint32_t *buf32;

	if (strcmp(string, "") != 0)
		printk("%s\n",string);

	switch (wd_size) {
		case 8:
			printk(" 0    1    2    3    4    5    6    7    "
				"8    9    Ah   Bh   Ch   Dh   Eh   Fh\n");
			printk("-----------------------------------------"
				"-------------------------------------\n");

			for (cnt = 1; cnt <= count; cnt++, buffer++) {
				printk("%02x",*buffer);
				if (cnt % 16 == 0)
					printk("\n");
				else
					printk("  ");
			}
			if (cnt % 16 != 0)
				printk("\n");
			break;
		case 16:
			printk("   0      2      4      6      8      Ah "
				"	Ch     Eh\n");
			printk("-----------------------------------------"
				"-------------\n");

			buf16 = (uint16_t *) buffer;
			for (cnt = 1; cnt <= count; cnt++, buf16++) {
				printk("%4x",*buf16);

				if (cnt % 8 == 0)
					printk("\n");
				else if (*buf16 < 10)
					printk("   ");
				else
					printk("  ");
			}
			if (cnt % 8 != 0)
				printk("\n");
			break;
		case 32:
			printk("       0          4          8          Ch\n");
			printk("------------------------------------------\n");

			buf32 = (uint32_t *) buffer;
			for (cnt = 1; cnt <= count; cnt++, buf32++) {
				printk("%8x", *buf32);

				if (cnt % 4 == 0)
					printk("\n");
				else if (*buf32 < 10)
					printk("   ");
				else
					printk("  ");
			}
			if (cnt % 4 != 0)
				printk("\n");
			break;
		default:
			break;
	}
}
#endif
