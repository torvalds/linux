/*
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
 * Copyright (C) 2003-2004 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 */
#include "qla_def.h"

#include <linux/delay.h>

static int qla_uprintf(char **, char *, ...);

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
	device_reg_t __iomem *reg = ha->iobase;
	uint16_t __iomem *dmp_reg;
	unsigned long	flags;
	struct qla2300_fw_dump	*fw;
	uint32_t	dump_size, data_ram_cnt;

	risc_address = data_ram_cnt = 0;
	mb0 = mb2 = 0;
	flags = 0;

	if (!hardware_locked)
		spin_lock_irqsave(&ha->hardware_lock, flags);

	if (ha->fw_dump != NULL) {
		qla_printk(KERN_WARNING, ha,
		    "Firmware has been previously dumped (%p) -- ignoring "
		    "request...\n", ha->fw_dump);
		goto qla2300_fw_dump_failed;
	}

	/* Allocate (large) dump buffer. */
	dump_size = sizeof(struct qla2300_fw_dump);
	dump_size += (ha->fw_memory_size - 0x11000) * sizeof(uint16_t);
	ha->fw_dump_order = get_order(dump_size);
	ha->fw_dump = (struct qla2300_fw_dump *) __get_free_pages(GFP_ATOMIC,
	    ha->fw_dump_order);
	if (ha->fw_dump == NULL) {
		qla_printk(KERN_WARNING, ha,
		    "Unable to allocated memory for firmware dump (%d/%d).\n",
		    ha->fw_dump_order, dump_size);
		goto qla2300_fw_dump_failed;
	}
	fw = ha->fw_dump;

	rval = QLA_SUCCESS;
	fw->hccr = RD_REG_WORD(&reg->hccr);

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
			fw->pbiu_reg[cnt] = RD_REG_WORD(dmp_reg++);

		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x10);
		for (cnt = 0; cnt < sizeof(fw->risc_host_reg) / 2; cnt++) 
			fw->risc_host_reg[cnt] = RD_REG_WORD(dmp_reg++);

		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x40);
		for (cnt = 0; cnt < sizeof(fw->mailbox_reg) / 2; cnt++) 
			fw->mailbox_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->ctrl_status, 0x40);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->resp_dma_reg) / 2; cnt++) 
			fw->resp_dma_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->ctrl_status, 0x50);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->dma_reg) / 2; cnt++) 
			fw->dma_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->ctrl_status, 0x00);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0xA0);
		for (cnt = 0; cnt < sizeof(fw->risc_hdw_reg) / 2; cnt++) 
			fw->risc_hdw_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->pcr, 0x2000); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp0_reg) / 2; cnt++) 
			fw->risc_gp0_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->pcr, 0x2200); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp1_reg) / 2; cnt++) 
			fw->risc_gp1_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->pcr, 0x2400); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp2_reg) / 2; cnt++) 
			fw->risc_gp2_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->pcr, 0x2600); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp3_reg) / 2; cnt++) 
			fw->risc_gp3_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->pcr, 0x2800); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp4_reg) / 2; cnt++) 
			fw->risc_gp4_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->pcr, 0x2A00); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp5_reg) / 2; cnt++) 
			fw->risc_gp5_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->pcr, 0x2C00); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp6_reg) / 2; cnt++) 
			fw->risc_gp6_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->pcr, 0x2E00); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp7_reg) / 2; cnt++) 
			fw->risc_gp7_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->ctrl_status, 0x10); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->frame_buf_hdw_reg) / 2; cnt++) 
			fw->frame_buf_hdw_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->ctrl_status, 0x20); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->fpm_b0_reg) / 2; cnt++) 
			fw->fpm_b0_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->ctrl_status, 0x30); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->fpm_b1_reg) / 2; cnt++) 
			fw->fpm_b1_reg[cnt] = RD_REG_WORD(dmp_reg++);

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
			fw->risc_ram[cnt] = mb2;
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
			fw->stack_ram[cnt] = mb2;
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
			fw->data_ram[cnt] = mb2;
		} else {
			rval = QLA_FUNCTION_FAILED;
		}
	}


	if (rval != QLA_SUCCESS) {
		qla_printk(KERN_WARNING, ha,
		    "Failed to dump firmware (%x)!!!\n", rval);

		free_pages((unsigned long)ha->fw_dump, ha->fw_dump_order);
		ha->fw_dump = NULL;
	} else {
		qla_printk(KERN_INFO, ha,
		    "Firmware dump saved to temp buffer (%ld/%p).\n",
		    ha->host_no, ha->fw_dump);
	}

qla2300_fw_dump_failed:
	if (!hardware_locked)
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

/**
 * qla2300_ascii_fw_dump() - Converts a binary firmware dump to ASCII.
 * @ha: HA context
 */
void
qla2300_ascii_fw_dump(scsi_qla_host_t *ha)
{
	uint32_t cnt;
	char *uiter;
	char fw_info[30];
	struct qla2300_fw_dump *fw;
	uint32_t data_ram_cnt;

	uiter = ha->fw_dump_buffer;
	fw = ha->fw_dump;

	qla_uprintf(&uiter, "%s Firmware Version %s\n", ha->model_number,
	    qla2x00_get_fw_version_str(ha, fw_info));

	qla_uprintf(&uiter, "\n[==>BEG]\n");

	qla_uprintf(&uiter, "HCCR Register:\n%04x\n\n", fw->hccr);

	qla_uprintf(&uiter, "PBIU Registers:");
	for (cnt = 0; cnt < sizeof (fw->pbiu_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->pbiu_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nReqQ-RspQ-Risc2Host Status registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_host_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_host_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nMailbox Registers:");
	for (cnt = 0; cnt < sizeof (fw->mailbox_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->mailbox_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nAuto Request Response DMA Registers:");
	for (cnt = 0; cnt < sizeof (fw->resp_dma_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->resp_dma_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nDMA Registers:");
	for (cnt = 0; cnt < sizeof (fw->dma_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->dma_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC Hardware Registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_hdw_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_hdw_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC GP0 Registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_gp0_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_gp0_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC GP1 Registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_gp1_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_gp1_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC GP2 Registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_gp2_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_gp2_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC GP3 Registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_gp3_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_gp3_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC GP4 Registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_gp4_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_gp4_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC GP5 Registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_gp5_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_gp5_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC GP6 Registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_gp6_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_gp6_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC GP7 Registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_gp7_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_gp7_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nFrame Buffer Hardware Registers:");
	for (cnt = 0; cnt < sizeof (fw->frame_buf_hdw_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->frame_buf_hdw_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nFPM B0 Registers:");
	for (cnt = 0; cnt < sizeof (fw->fpm_b0_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->fpm_b0_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nFPM B1 Registers:");
	for (cnt = 0; cnt < sizeof (fw->fpm_b1_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->fpm_b1_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nCode RAM Dump:");
	for (cnt = 0; cnt < sizeof (fw->risc_ram) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n%04x: ", cnt + 0x0800);
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_ram[cnt]);
	}

	qla_uprintf(&uiter, "\n\nStack RAM Dump:");
	for (cnt = 0; cnt < sizeof (fw->stack_ram) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n%05x: ", cnt + 0x10000);
		}
		qla_uprintf(&uiter, "%04x ", fw->stack_ram[cnt]);
	}

	qla_uprintf(&uiter, "\n\nData RAM Dump:");
	data_ram_cnt = ha->fw_memory_size - 0x11000 + 1;
	for (cnt = 0; cnt < data_ram_cnt; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n%05x: ", cnt + 0x11000);
		}
		qla_uprintf(&uiter, "%04x ", fw->data_ram[cnt]);
	}

	qla_uprintf(&uiter, "\n\n[<==END] ISP Debug Dump.");
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
	device_reg_t __iomem *reg = ha->iobase;
	uint16_t __iomem *dmp_reg;
	unsigned long	flags;
	struct qla2100_fw_dump	*fw;

	risc_address = 0;
	mb0 = mb2 = 0;
	flags = 0;

	if (!hardware_locked)
		spin_lock_irqsave(&ha->hardware_lock, flags);

	if (ha->fw_dump != NULL) {
		qla_printk(KERN_WARNING, ha,
		    "Firmware has been previously dumped (%p) -- ignoring "
		    "request...\n", ha->fw_dump);
		goto qla2100_fw_dump_failed;
	}

	/* Allocate (large) dump buffer. */
	ha->fw_dump_order = get_order(sizeof(struct qla2100_fw_dump));
	ha->fw_dump = (struct qla2100_fw_dump *) __get_free_pages(GFP_ATOMIC,
	    ha->fw_dump_order);
	if (ha->fw_dump == NULL) {
		qla_printk(KERN_WARNING, ha,
		    "Unable to allocated memory for firmware dump (%d/%Zd).\n",
		    ha->fw_dump_order, sizeof(struct qla2100_fw_dump));
		goto qla2100_fw_dump_failed;
	}
	fw = ha->fw_dump;

	rval = QLA_SUCCESS;
	fw->hccr = RD_REG_WORD(&reg->hccr);

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
			fw->pbiu_reg[cnt] = RD_REG_WORD(dmp_reg++);

		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x10);
		for (cnt = 0; cnt < ha->mbx_count; cnt++) {
			if (cnt == 8) {
				dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0xe0);
			}
			fw->mailbox_reg[cnt] = RD_REG_WORD(dmp_reg++);
		}

		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x20);
		for (cnt = 0; cnt < sizeof(fw->dma_reg) / 2; cnt++) 
			fw->dma_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->ctrl_status, 0x00);
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0xA0);
		for (cnt = 0; cnt < sizeof(fw->risc_hdw_reg) / 2; cnt++) 
			fw->risc_hdw_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->pcr, 0x2000); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp0_reg) / 2; cnt++) 
			fw->risc_gp0_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->pcr, 0x2100); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp1_reg) / 2; cnt++) 
			fw->risc_gp1_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->pcr, 0x2200); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp2_reg) / 2; cnt++) 
			fw->risc_gp2_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->pcr, 0x2300); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp3_reg) / 2; cnt++) 
			fw->risc_gp3_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->pcr, 0x2400); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp4_reg) / 2; cnt++) 
			fw->risc_gp4_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->pcr, 0x2500); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp5_reg) / 2; cnt++) 
			fw->risc_gp5_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->pcr, 0x2600); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp6_reg) / 2; cnt++) 
			fw->risc_gp6_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->pcr, 0x2700); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->risc_gp7_reg) / 2; cnt++) 
			fw->risc_gp7_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->ctrl_status, 0x10); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->frame_buf_hdw_reg) / 2; cnt++) 
			fw->frame_buf_hdw_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->ctrl_status, 0x20); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->fpm_b0_reg) / 2; cnt++) 
			fw->fpm_b0_reg[cnt] = RD_REG_WORD(dmp_reg++);

		WRT_REG_WORD(&reg->ctrl_status, 0x30); 
		dmp_reg = (uint16_t __iomem *)((uint8_t __iomem *)reg + 0x80);
		for (cnt = 0; cnt < sizeof(fw->fpm_b1_reg) / 2; cnt++) 
			fw->fpm_b1_reg[cnt] = RD_REG_WORD(dmp_reg++);

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
			fw->risc_ram[cnt] = mb2;
		} else {
			rval = QLA_FUNCTION_FAILED;
		}
	}

	if (rval != QLA_SUCCESS) {
		qla_printk(KERN_WARNING, ha,
		    "Failed to dump firmware (%x)!!!\n", rval);

		free_pages((unsigned long)ha->fw_dump, ha->fw_dump_order);
		ha->fw_dump = NULL;
	} else {
		qla_printk(KERN_INFO, ha,
		    "Firmware dump saved to temp buffer (%ld/%p).\n",
		    ha->host_no, ha->fw_dump);
	}

qla2100_fw_dump_failed:
	if (!hardware_locked)
		spin_unlock_irqrestore(&ha->hardware_lock, flags);
}

/**
 * qla2100_ascii_fw_dump() - Converts a binary firmware dump to ASCII.
 * @ha: HA context
 */
void
qla2100_ascii_fw_dump(scsi_qla_host_t *ha)
{
	uint32_t cnt;
	char *uiter;
	char fw_info[30];
	struct qla2100_fw_dump *fw;

	uiter = ha->fw_dump_buffer;
	fw = ha->fw_dump;

	qla_uprintf(&uiter, "%s Firmware Version %s\n", ha->model_number,
	    qla2x00_get_fw_version_str(ha, fw_info));

	qla_uprintf(&uiter, "\n[==>BEG]\n");

	qla_uprintf(&uiter, "HCCR Register:\n%04x\n\n", fw->hccr);

	qla_uprintf(&uiter, "PBIU Registers:");
	for (cnt = 0; cnt < sizeof (fw->pbiu_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->pbiu_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nMailbox Registers:");
	for (cnt = 0; cnt < sizeof (fw->mailbox_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->mailbox_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nDMA Registers:");
	for (cnt = 0; cnt < sizeof (fw->dma_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->dma_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC Hardware Registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_hdw_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_hdw_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC GP0 Registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_gp0_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_gp0_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC GP1 Registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_gp1_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_gp1_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC GP2 Registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_gp2_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_gp2_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC GP3 Registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_gp3_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_gp3_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC GP4 Registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_gp4_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_gp4_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC GP5 Registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_gp5_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_gp5_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC GP6 Registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_gp6_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_gp6_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC GP7 Registers:");
	for (cnt = 0; cnt < sizeof (fw->risc_gp7_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_gp7_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nFrame Buffer Hardware Registers:");
	for (cnt = 0; cnt < sizeof (fw->frame_buf_hdw_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->frame_buf_hdw_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nFPM B0 Registers:");
	for (cnt = 0; cnt < sizeof (fw->fpm_b0_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->fpm_b0_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nFPM B1 Registers:");
	for (cnt = 0; cnt < sizeof (fw->fpm_b1_reg) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n");
		}
		qla_uprintf(&uiter, "%04x ", fw->fpm_b1_reg[cnt]);
	}

	qla_uprintf(&uiter, "\n\nRISC SRAM:");
	for (cnt = 0; cnt < sizeof (fw->risc_ram) / 2; cnt++) {
		if (cnt % 8 == 0) {
			qla_uprintf(&uiter, "\n%04x: ", cnt + 0x1000);
		}
		qla_uprintf(&uiter, "%04x ", fw->risc_ram[cnt]);
	}

	qla_uprintf(&uiter, "\n\n[<==END] ISP Debug Dump.");

	return;
}

static int
qla_uprintf(char **uiter, char *fmt, ...)
{
	int	iter, len;
	char	buf[128];
	va_list	args;
 
	va_start(args, fmt);
	len = vsprintf(buf, fmt, args);
	va_end(args);

	for (iter = 0; iter < len; iter++, *uiter += 1)
		*uiter[0] = buf[iter];

	return (len);
}

//FIXME

/****************************************************************************/
/*                         Driver Debug Functions.                          */
/****************************************************************************/

void 
qla2x00_dump_regs(scsi_qla_host_t *ha) 
{
	device_reg_t __iomem *reg = ha->iobase;

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
	printk("\n  seg_cnt=%d, allowed=%d, retries=%d, "
	    "serial_number_at_timeout=0x%lx\n",
	    cmd->use_sg, cmd->allowed, cmd->retries,
	    cmd->serial_number_at_timeout);
	printk("  request buffer=0x%p, request buffer len=0x%x\n",
	    cmd->request_buffer, cmd->request_bufflen);
	printk("  tag=%d, transfersize=0x%x\n",
	    cmd->tag, cmd->transfersize);
	printk("  serial_number=%lx, SP=%p\n", cmd->serial_number, sp); 
	printk("  data direction=%d\n", cmd->sc_data_direction);

	if (!sp)
		return;

	printk("  sp flags=0x%x\n", sp->flags);
	printk("  r_start=0x%lx, u_start=0x%lx, f_start=0x%lx, state=%d\n",
	    sp->r_start, sp->u_start, sp->f_start, sp->state);

	printk(" e_start= 0x%lx, ext_history=%d, fo retry=%d, loopid=%x, "
	    "port path=%d\n", sp->e_start, sp->ext_history, sp->fo_retry_cnt,
	    sp->lun_queue->fclun->fcport->loop_id,
	    sp->lun_queue->fclun->fcport->cur_path);
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
