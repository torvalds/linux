// SPDX-License-Identifier: GPL-2.0-only
/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 */

/*
 * Table for showing the current message id in use for particular level
 * Change this table for addition of log/debug messages.
 * ----------------------------------------------------------------------
 * |             Level            |   Last Value Used  |     Holes	|
 * ----------------------------------------------------------------------
 * | Module Init and Probe        |       0x0199       |                |
 * | Mailbox commands             |       0x1206       | 0x11a5-0x11ff	|
 * | Device Discovery             |       0x2134       | 0x210e-0x2115  |
 * |                              |                    | 0x211c-0x2128  |
 * |                              |                    | 0x212c-0x2134  |
 * | Queue Command and IO tracing |       0x3074       | 0x300b         |
 * |                              |                    | 0x3027-0x3028  |
 * |                              |                    | 0x303d-0x3041  |
 * |                              |                    | 0x302e,0x3033  |
 * |                              |                    | 0x3036,0x3038  |
 * |                              |                    | 0x303a		|
 * | DPC Thread                   |       0x4023       | 0x4002,0x4013  |
 * | Async Events                 |       0x509c       |                |
 * | Timer Routines               |       0x6012       |                |
 * | User Space Interactions      |       0x70e3       | 0x7018,0x702e  |
 * |				  |		       | 0x7020,0x7024  |
 * |                              |                    | 0x7039,0x7045  |
 * |                              |                    | 0x7073-0x7075  |
 * |                              |                    | 0x70a5-0x70a6  |
 * |                              |                    | 0x70a8,0x70ab  |
 * |                              |                    | 0x70ad-0x70ae  |
 * |                              |                    | 0x70d0-0x70d6	|
 * |                              |                    | 0x70d7-0x70db  |
 * | Task Management              |       0x8042       | 0x8000         |
 * |                              |                    | 0x8019         |
 * |                              |                    | 0x8025,0x8026  |
 * |                              |                    | 0x8031,0x8032  |
 * |                              |                    | 0x8039,0x803c  |
 * | AER/EEH                      |       0x9011       |		|
 * | Virtual Port                 |       0xa007       |		|
 * | ISP82XX Specific             |       0xb157       | 0xb002,0xb024  |
 * |                              |                    | 0xb09e,0xb0ae  |
 * |				  |		       | 0xb0c3,0xb0c6  |
 * |                              |                    | 0xb0e0-0xb0ef  |
 * |                              |                    | 0xb085,0xb0dc  |
 * |                              |                    | 0xb107,0xb108  |
 * |                              |                    | 0xb111,0xb11e  |
 * |                              |                    | 0xb12c,0xb12d  |
 * |                              |                    | 0xb13a,0xb142  |
 * |                              |                    | 0xb13c-0xb140  |
 * |                              |                    | 0xb149		|
 * | MultiQ                       |       0xc010       |		|
 * | Misc                         |       0xd303       | 0xd031-0xd0ff	|
 * |                              |                    | 0xd101-0xd1fe	|
 * |                              |                    | 0xd214-0xd2fe	|
 * | Target Mode		  |	  0xe081       |		|
 * | Target Mode Management	  |	  0xf09b       | 0xf002		|
 * |                              |                    | 0xf046-0xf049  |
 * | Target Mode Task Management  |	  0x1000d      |		|
 * ----------------------------------------------------------------------
 */

#include "qla_def.h"

#include <linux/delay.h>
#define CREATE_TRACE_POINTS
#include <trace/events/qla.h>

static uint32_t ql_dbg_offset = 0x800;

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

int
qla27xx_dump_mpi_ram(struct qla_hw_data *ha, uint32_t addr, uint32_t *ram,
	uint32_t ram_dwords, void **nxt)
{
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	dma_addr_t dump_dma = ha->gid_list_dma;
	uint32_t *chunk = (uint32_t *)ha->gid_list;
	uint32_t dwords = qla2x00_gid_list_size(ha) / 4;
	uint32_t stat;
	ulong i, j, timer = 6000000;
	int rval = QLA_FUNCTION_FAILED;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

	if (qla_pci_disconnected(vha, reg))
		return rval;

	for (i = 0; i < ram_dwords; i += dwords, addr += dwords) {
		if (i + dwords > ram_dwords)
			dwords = ram_dwords - i;

		wrt_reg_word(&reg->mailbox0, MBC_LOAD_DUMP_MPI_RAM);
		wrt_reg_word(&reg->mailbox1, LSW(addr));
		wrt_reg_word(&reg->mailbox8, MSW(addr));

		wrt_reg_word(&reg->mailbox2, MSW(LSD(dump_dma)));
		wrt_reg_word(&reg->mailbox3, LSW(LSD(dump_dma)));
		wrt_reg_word(&reg->mailbox6, MSW(MSD(dump_dma)));
		wrt_reg_word(&reg->mailbox7, LSW(MSD(dump_dma)));

		wrt_reg_word(&reg->mailbox4, MSW(dwords));
		wrt_reg_word(&reg->mailbox5, LSW(dwords));

		wrt_reg_word(&reg->mailbox9, 0);
		wrt_reg_dword(&reg->hccr, HCCRX_SET_HOST_INT);

		ha->flags.mbox_int = 0;
		while (timer--) {
			udelay(5);

			if (qla_pci_disconnected(vha, reg))
				return rval;

			stat = rd_reg_dword(&reg->host_status);
			/* Check for pending interrupts. */
			if (!(stat & HSRX_RISC_INT))
				continue;

			stat &= 0xff;
			if (stat != 0x1 && stat != 0x2 &&
			    stat != 0x10 && stat != 0x11) {

				/* Clear this intr; it wasn't a mailbox intr */
				wrt_reg_dword(&reg->hccr, HCCRX_CLR_RISC_INT);
				rd_reg_dword(&reg->hccr);
				continue;
			}

			set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
			rval = rd_reg_word(&reg->mailbox0) & MBS_MASK;
			wrt_reg_dword(&reg->hccr, HCCRX_CLR_RISC_INT);
			rd_reg_dword(&reg->hccr);
			break;
		}
		ha->flags.mbox_int = 1;
		*nxt = ram + i;

		if (!test_and_clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags)) {
			/* no interrupt, timed out*/
			return rval;
		}
		if (rval) {
			/* error completion status */
			return rval;
		}
		for (j = 0; j < dwords; j++) {
			ram[i + j] =
			    (IS_QLA27XX(ha) || IS_QLA28XX(ha)) ?
			    chunk[j] : swab32(chunk[j]);
		}
	}

	*nxt = ram + i;
	return QLA_SUCCESS;
}

int
qla24xx_dump_ram(struct qla_hw_data *ha, uint32_t addr, __be32 *ram,
		 uint32_t ram_dwords, void **nxt)
{
	int rval = QLA_FUNCTION_FAILED;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	dma_addr_t dump_dma = ha->gid_list_dma;
	uint32_t *chunk = (uint32_t *)ha->gid_list;
	uint32_t dwords = qla2x00_gid_list_size(ha) / 4;
	uint32_t stat;
	ulong i, j, timer = 6000000;
	scsi_qla_host_t *vha = pci_get_drvdata(ha->pdev);

	clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

	if (qla_pci_disconnected(vha, reg))
		return rval;

	for (i = 0; i < ram_dwords; i += dwords, addr += dwords) {
		if (i + dwords > ram_dwords)
			dwords = ram_dwords - i;

		wrt_reg_word(&reg->mailbox0, MBC_DUMP_RISC_RAM_EXTENDED);
		wrt_reg_word(&reg->mailbox1, LSW(addr));
		wrt_reg_word(&reg->mailbox8, MSW(addr));
		wrt_reg_word(&reg->mailbox10, 0);

		wrt_reg_word(&reg->mailbox2, MSW(LSD(dump_dma)));
		wrt_reg_word(&reg->mailbox3, LSW(LSD(dump_dma)));
		wrt_reg_word(&reg->mailbox6, MSW(MSD(dump_dma)));
		wrt_reg_word(&reg->mailbox7, LSW(MSD(dump_dma)));

		wrt_reg_word(&reg->mailbox4, MSW(dwords));
		wrt_reg_word(&reg->mailbox5, LSW(dwords));
		wrt_reg_dword(&reg->hccr, HCCRX_SET_HOST_INT);

		ha->flags.mbox_int = 0;
		while (timer--) {
			udelay(5);
			if (qla_pci_disconnected(vha, reg))
				return rval;

			stat = rd_reg_dword(&reg->host_status);
			/* Check for pending interrupts. */
			if (!(stat & HSRX_RISC_INT))
				continue;

			stat &= 0xff;
			if (stat != 0x1 && stat != 0x2 &&
			    stat != 0x10 && stat != 0x11) {
				wrt_reg_dword(&reg->hccr, HCCRX_CLR_RISC_INT);
				rd_reg_dword(&reg->hccr);
				continue;
			}

			set_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
			rval = rd_reg_word(&reg->mailbox0) & MBS_MASK;
			wrt_reg_dword(&reg->hccr, HCCRX_CLR_RISC_INT);
			rd_reg_dword(&reg->hccr);
			break;
		}
		ha->flags.mbox_int = 1;
		*nxt = ram + i;

		if (!test_and_clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags)) {
			/* no interrupt, timed out*/
			return rval;
		}
		if (rval) {
			/* error completion status */
			return rval;
		}
		for (j = 0; j < dwords; j++) {
			ram[i + j] = (__force __be32)
				((IS_QLA27XX(ha) || IS_QLA28XX(ha)) ?
				 chunk[j] : swab32(chunk[j]));
		}
	}

	*nxt = ram + i;
	return QLA_SUCCESS;
}

static int
qla24xx_dump_memory(struct qla_hw_data *ha, __be32 *code_ram,
		    uint32_t cram_size, void **nxt)
{
	int rval;

	/* Code RAM. */
	rval = qla24xx_dump_ram(ha, 0x20000, code_ram, cram_size / 4, nxt);
	if (rval != QLA_SUCCESS)
		return rval;

	set_bit(RISC_SRAM_DUMP_CMPL, &ha->fw_dump_cap_flags);

	/* External Memory. */
	rval = qla24xx_dump_ram(ha, 0x100000, *nxt,
	    ha->fw_memory_size - 0x100000 + 1, nxt);
	if (rval == QLA_SUCCESS)
		set_bit(RISC_EXT_MEM_DUMP_CMPL, &ha->fw_dump_cap_flags);

	return rval;
}

static __be32 *
qla24xx_read_window(struct device_reg_24xx __iomem *reg, uint32_t iobase,
		    uint32_t count, __be32 *buf)
{
	__le32 __iomem *dmp_reg;

	wrt_reg_dword(&reg->iobase_addr, iobase);
	dmp_reg = &reg->iobase_window;
	for ( ; count--; dmp_reg++)
		*buf++ = htonl(rd_reg_dword(dmp_reg));

	return buf;
}

void
qla24xx_pause_risc(struct device_reg_24xx __iomem *reg, struct qla_hw_data *ha)
{
	wrt_reg_dword(&reg->hccr, HCCRX_SET_RISC_PAUSE);

	/* 100 usec delay is sufficient enough for hardware to pause RISC */
	udelay(100);
	if (rd_reg_dword(&reg->host_status) & HSRX_RISC_PAUSED)
		set_bit(RISC_PAUSE_CMPL, &ha->fw_dump_cap_flags);
}

int
qla24xx_soft_reset(struct qla_hw_data *ha)
{
	int rval = QLA_SUCCESS;
	uint32_t cnt;
	uint16_t wd;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;

	/*
	 * Reset RISC. The delay is dependent on system architecture.
	 * Driver can proceed with the reset sequence after waiting
	 * for a timeout period.
	 */
	wrt_reg_dword(&reg->ctrl_status, CSRX_DMA_SHUTDOWN|MWB_4096_BYTES);
	for (cnt = 0; cnt < 30000; cnt++) {
		if ((rd_reg_dword(&reg->ctrl_status) & CSRX_DMA_ACTIVE) == 0)
			break;

		udelay(10);
	}
	if (!(rd_reg_dword(&reg->ctrl_status) & CSRX_DMA_ACTIVE))
		set_bit(DMA_SHUTDOWN_CMPL, &ha->fw_dump_cap_flags);

	wrt_reg_dword(&reg->ctrl_status,
	    CSRX_ISP_SOFT_RESET|CSRX_DMA_SHUTDOWN|MWB_4096_BYTES);
	pci_read_config_word(ha->pdev, PCI_COMMAND, &wd);

	udelay(100);

	/* Wait for soft-reset to complete. */
	for (cnt = 0; cnt < 30000; cnt++) {
		if ((rd_reg_dword(&reg->ctrl_status) &
		    CSRX_ISP_SOFT_RESET) == 0)
			break;

		udelay(10);
	}
	if (!(rd_reg_dword(&reg->ctrl_status) & CSRX_ISP_SOFT_RESET))
		set_bit(ISP_RESET_CMPL, &ha->fw_dump_cap_flags);

	wrt_reg_dword(&reg->hccr, HCCRX_CLR_RISC_RESET);
	rd_reg_dword(&reg->hccr);             /* PCI Posting. */

	for (cnt = 10000; rd_reg_word(&reg->mailbox0) != 0 &&
	    rval == QLA_SUCCESS; cnt--) {
		if (cnt)
			udelay(10);
		else
			rval = QLA_FUNCTION_TIMEOUT;
	}
	if (rval == QLA_SUCCESS)
		set_bit(RISC_RDY_AFT_RESET, &ha->fw_dump_cap_flags);

	return rval;
}

static int
qla2xxx_dump_ram(struct qla_hw_data *ha, uint32_t addr, __be16 *ram,
    uint32_t ram_words, void **nxt)
{
	int rval;
	uint32_t cnt, stat, timer, words, idx;
	uint16_t mb0;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	dma_addr_t dump_dma = ha->gid_list_dma;
	__le16 *dump = (__force __le16 *)ha->gid_list;

	rval = QLA_SUCCESS;
	mb0 = 0;

	WRT_MAILBOX_REG(ha, reg, 0, MBC_DUMP_RISC_RAM_EXTENDED);
	clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);

	words = qla2x00_gid_list_size(ha) / 2;
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
		wrt_reg_word(&reg->hccr, HCCR_SET_HOST_INT);

		for (timer = 6000000; timer; timer--) {
			/* Check for pending interrupts. */
			stat = rd_reg_dword(&reg->u.isp2300.host_status);
			if (stat & HSR_RISC_INT) {
				stat &= 0xff;

				if (stat == 0x1 || stat == 0x2) {
					set_bit(MBX_INTERRUPT,
					    &ha->mbx_cmd_flags);

					mb0 = RD_MAILBOX_REG(ha, reg, 0);

					/* Release mailbox registers. */
					wrt_reg_word(&reg->semaphore, 0);
					wrt_reg_word(&reg->hccr,
					    HCCR_CLR_RISC_INT);
					rd_reg_word(&reg->hccr);
					break;
				} else if (stat == 0x10 || stat == 0x11) {
					set_bit(MBX_INTERRUPT,
					    &ha->mbx_cmd_flags);

					mb0 = RD_MAILBOX_REG(ha, reg, 0);

					wrt_reg_word(&reg->hccr,
					    HCCR_CLR_RISC_INT);
					rd_reg_word(&reg->hccr);
					break;
				}

				/* clear this intr; it wasn't a mailbox intr */
				wrt_reg_word(&reg->hccr, HCCR_CLR_RISC_INT);
				rd_reg_word(&reg->hccr);
			}
			udelay(5);
		}

		if (test_and_clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags)) {
			rval = mb0 & MBS_MASK;
			for (idx = 0; idx < words; idx++)
				ram[cnt + idx] =
					cpu_to_be16(le16_to_cpu(dump[idx]));
		} else {
			rval = QLA_FUNCTION_FAILED;
		}
	}

	*nxt = rval == QLA_SUCCESS ? &ram[cnt] : NULL;
	return rval;
}

static inline void
qla2xxx_read_window(struct device_reg_2xxx __iomem *reg, uint32_t count,
		    __be16 *buf)
{
	__le16 __iomem *dmp_reg = &reg->u.isp2300.fb_cmd;

	for ( ; count--; dmp_reg++)
		*buf++ = htons(rd_reg_word(dmp_reg));
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
qla25xx_copy_fce(struct qla_hw_data *ha, void *ptr, __be32 **last_chain)
{
	uint32_t cnt;
	__be32 *iter_reg;
	struct qla2xxx_fce_chain *fcec = ptr;

	if (!ha->fce)
		return ptr;

	*last_chain = &fcec->type;
	fcec->type = htonl(DUMP_CHAIN_FCE);
	fcec->chain_size = htonl(sizeof(struct qla2xxx_fce_chain) +
	    fce_calc_size(ha->fce_bufs));
	fcec->size = htonl(fce_calc_size(ha->fce_bufs));
	fcec->addr_l = htonl(LSD(ha->fce_dma));
	fcec->addr_h = htonl(MSD(ha->fce_dma));

	iter_reg = fcec->eregs;
	for (cnt = 0; cnt < 8; cnt++)
		*iter_reg++ = htonl(ha->fce_mb[cnt]);

	memcpy(iter_reg, ha->fce, ntohl(fcec->size));

	return (char *)iter_reg + ntohl(fcec->size);
}

static inline void *
qla25xx_copy_exlogin(struct qla_hw_data *ha, void *ptr, __be32 **last_chain)
{
	struct qla2xxx_offld_chain *c = ptr;

	if (!ha->exlogin_buf)
		return ptr;

	*last_chain = &c->type;

	c->type = cpu_to_be32(DUMP_CHAIN_EXLOGIN);
	c->chain_size = cpu_to_be32(sizeof(struct qla2xxx_offld_chain) +
	    ha->exlogin_size);
	c->size = cpu_to_be32(ha->exlogin_size);
	c->addr = cpu_to_be64(ha->exlogin_buf_dma);

	ptr += sizeof(struct qla2xxx_offld_chain);
	memcpy(ptr, ha->exlogin_buf, ha->exlogin_size);

	return (char *)ptr + be32_to_cpu(c->size);
}

static inline void *
qla81xx_copy_exchoffld(struct qla_hw_data *ha, void *ptr, __be32 **last_chain)
{
	struct qla2xxx_offld_chain *c = ptr;

	if (!ha->exchoffld_buf)
		return ptr;

	*last_chain = &c->type;

	c->type = cpu_to_be32(DUMP_CHAIN_EXCHG);
	c->chain_size = cpu_to_be32(sizeof(struct qla2xxx_offld_chain) +
	    ha->exchoffld_size);
	c->size = cpu_to_be32(ha->exchoffld_size);
	c->addr = cpu_to_be64(ha->exchoffld_buf_dma);

	ptr += sizeof(struct qla2xxx_offld_chain);
	memcpy(ptr, ha->exchoffld_buf, ha->exchoffld_size);

	return (char *)ptr + be32_to_cpu(c->size);
}

static inline void *
qla2xxx_copy_atioqueues(struct qla_hw_data *ha, void *ptr,
			__be32 **last_chain)
{
	struct qla2xxx_mqueue_chain *q;
	struct qla2xxx_mqueue_header *qh;
	uint32_t num_queues;
	int que;
	struct {
		int length;
		void *ring;
	} aq, *aqp;

	if (!ha->tgt.atio_ring)
		return ptr;

	num_queues = 1;
	aqp = &aq;
	aqp->length = ha->tgt.atio_q_length;
	aqp->ring = ha->tgt.atio_ring;

	for (que = 0; que < num_queues; que++) {
		/* aqp = ha->atio_q_map[que]; */
		q = ptr;
		*last_chain = &q->type;
		q->type = htonl(DUMP_CHAIN_QUEUE);
		q->chain_size = htonl(
		    sizeof(struct qla2xxx_mqueue_chain) +
		    sizeof(struct qla2xxx_mqueue_header) +
		    (aqp->length * sizeof(request_t)));
		ptr += sizeof(struct qla2xxx_mqueue_chain);

		/* Add header. */
		qh = ptr;
		qh->queue = htonl(TYPE_ATIO_QUEUE);
		qh->number = htonl(que);
		qh->size = htonl(aqp->length * sizeof(request_t));
		ptr += sizeof(struct qla2xxx_mqueue_header);

		/* Add data. */
		memcpy(ptr, aqp->ring, aqp->length * sizeof(request_t));

		ptr += aqp->length * sizeof(request_t);
	}

	return ptr;
}

static inline void *
qla25xx_copy_mqueues(struct qla_hw_data *ha, void *ptr, __be32 **last_chain)
{
	struct qla2xxx_mqueue_chain *q;
	struct qla2xxx_mqueue_header *qh;
	struct req_que *req;
	struct rsp_que *rsp;
	int que;

	if (!ha->mqenable)
		return ptr;

	/* Request queues */
	for (que = 1; que < ha->max_req_queues; que++) {
		req = ha->req_q_map[que];
		if (!req)
			break;

		/* Add chain. */
		q = ptr;
		*last_chain = &q->type;
		q->type = htonl(DUMP_CHAIN_QUEUE);
		q->chain_size = htonl(
		    sizeof(struct qla2xxx_mqueue_chain) +
		    sizeof(struct qla2xxx_mqueue_header) +
		    (req->length * sizeof(request_t)));
		ptr += sizeof(struct qla2xxx_mqueue_chain);

		/* Add header. */
		qh = ptr;
		qh->queue = htonl(TYPE_REQUEST_QUEUE);
		qh->number = htonl(que);
		qh->size = htonl(req->length * sizeof(request_t));
		ptr += sizeof(struct qla2xxx_mqueue_header);

		/* Add data. */
		memcpy(ptr, req->ring, req->length * sizeof(request_t));
		ptr += req->length * sizeof(request_t);
	}

	/* Response queues */
	for (que = 1; que < ha->max_rsp_queues; que++) {
		rsp = ha->rsp_q_map[que];
		if (!rsp)
			break;

		/* Add chain. */
		q = ptr;
		*last_chain = &q->type;
		q->type = htonl(DUMP_CHAIN_QUEUE);
		q->chain_size = htonl(
		    sizeof(struct qla2xxx_mqueue_chain) +
		    sizeof(struct qla2xxx_mqueue_header) +
		    (rsp->length * sizeof(response_t)));
		ptr += sizeof(struct qla2xxx_mqueue_chain);

		/* Add header. */
		qh = ptr;
		qh->queue = htonl(TYPE_RESPONSE_QUEUE);
		qh->number = htonl(que);
		qh->size = htonl(rsp->length * sizeof(response_t));
		ptr += sizeof(struct qla2xxx_mqueue_header);

		/* Add data. */
		memcpy(ptr, rsp->ring, rsp->length * sizeof(response_t));
		ptr += rsp->length * sizeof(response_t);
	}

	return ptr;
}

static inline void *
qla25xx_copy_mq(struct qla_hw_data *ha, void *ptr, __be32 **last_chain)
{
	uint32_t cnt, que_idx;
	uint8_t que_cnt;
	struct qla2xxx_mq_chain *mq = ptr;
	device_reg_t *reg;

	if (!ha->mqenable || IS_QLA83XX(ha) || IS_QLA27XX(ha) ||
	    IS_QLA28XX(ha))
		return ptr;

	mq = ptr;
	*last_chain = &mq->type;
	mq->type = htonl(DUMP_CHAIN_MQ);
	mq->chain_size = htonl(sizeof(struct qla2xxx_mq_chain));

	que_cnt = ha->max_req_queues > ha->max_rsp_queues ?
		ha->max_req_queues : ha->max_rsp_queues;
	mq->count = htonl(que_cnt);
	for (cnt = 0; cnt < que_cnt; cnt++) {
		reg = ISP_QUE_REG(ha, cnt);
		que_idx = cnt * 4;
		mq->qregs[que_idx] =
		    htonl(rd_reg_dword(&reg->isp25mq.req_q_in));
		mq->qregs[que_idx+1] =
		    htonl(rd_reg_dword(&reg->isp25mq.req_q_out));
		mq->qregs[que_idx+2] =
		    htonl(rd_reg_dword(&reg->isp25mq.rsp_q_in));
		mq->qregs[que_idx+3] =
		    htonl(rd_reg_dword(&reg->isp25mq.rsp_q_out));
	}

	return ptr + sizeof(struct qla2xxx_mq_chain);
}

void
qla2xxx_dump_post_process(scsi_qla_host_t *vha, int rval)
{
	struct qla_hw_data *ha = vha->hw;

	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0xd000,
		    "Failed to dump firmware (%x), dump status flags (0x%lx).\n",
		    rval, ha->fw_dump_cap_flags);
		ha->fw_dumped = false;
	} else {
		ql_log(ql_log_info, vha, 0xd001,
		    "Firmware dump saved to temp buffer (%ld/%p), dump status flags (0x%lx).\n",
		    vha->host_no, ha->fw_dump, ha->fw_dump_cap_flags);
		ha->fw_dumped = true;
		qla2x00_post_uevent_work(vha, QLA_UEVENT_CODE_FW_DUMP);
	}
}

void qla2xxx_dump_fw(scsi_qla_host_t *vha)
{
	unsigned long flags;

	spin_lock_irqsave(&vha->hw->hardware_lock, flags);
	vha->hw->isp_ops->fw_dump(vha);
	spin_unlock_irqrestore(&vha->hw->hardware_lock, flags);
}

/**
 * qla2300_fw_dump() - Dumps binary data from the 2300 firmware.
 * @vha: HA context
 */
void
qla2300_fw_dump(scsi_qla_host_t *vha)
{
	int		rval;
	uint32_t	cnt;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	__le16 __iomem *dmp_reg;
	struct qla2300_fw_dump	*fw;
	void		*nxt;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);

	lockdep_assert_held(&ha->hardware_lock);

	if (!ha->fw_dump) {
		ql_log(ql_log_warn, vha, 0xd002,
		    "No buffer available for dump.\n");
		return;
	}

	if (ha->fw_dumped) {
		ql_log(ql_log_warn, vha, 0xd003,
		    "Firmware has been previously dumped (%p) "
		    "-- ignoring request.\n",
		    ha->fw_dump);
		return;
	}
	fw = &ha->fw_dump->isp.isp23;
	qla2xxx_prep_dump(ha, ha->fw_dump);

	rval = QLA_SUCCESS;
	fw->hccr = htons(rd_reg_word(&reg->hccr));

	/* Pause RISC. */
	wrt_reg_word(&reg->hccr, HCCR_PAUSE_RISC);
	if (IS_QLA2300(ha)) {
		for (cnt = 30000;
		    (rd_reg_word(&reg->hccr) & HCCR_RISC_PAUSE) == 0 &&
			rval == QLA_SUCCESS; cnt--) {
			if (cnt)
				udelay(100);
			else
				rval = QLA_FUNCTION_TIMEOUT;
		}
	} else {
		rd_reg_word(&reg->hccr);		/* PCI Posting. */
		udelay(10);
	}

	if (rval == QLA_SUCCESS) {
		dmp_reg = &reg->flash_address;
		for (cnt = 0; cnt < ARRAY_SIZE(fw->pbiu_reg); cnt++, dmp_reg++)
			fw->pbiu_reg[cnt] = htons(rd_reg_word(dmp_reg));

		dmp_reg = &reg->u.isp2300.req_q_in;
		for (cnt = 0; cnt < ARRAY_SIZE(fw->risc_host_reg);
		    cnt++, dmp_reg++)
			fw->risc_host_reg[cnt] = htons(rd_reg_word(dmp_reg));

		dmp_reg = &reg->u.isp2300.mailbox0;
		for (cnt = 0; cnt < ARRAY_SIZE(fw->mailbox_reg);
		    cnt++, dmp_reg++)
			fw->mailbox_reg[cnt] = htons(rd_reg_word(dmp_reg));

		wrt_reg_word(&reg->ctrl_status, 0x40);
		qla2xxx_read_window(reg, 32, fw->resp_dma_reg);

		wrt_reg_word(&reg->ctrl_status, 0x50);
		qla2xxx_read_window(reg, 48, fw->dma_reg);

		wrt_reg_word(&reg->ctrl_status, 0x00);
		dmp_reg = &reg->risc_hw;
		for (cnt = 0; cnt < ARRAY_SIZE(fw->risc_hdw_reg);
		    cnt++, dmp_reg++)
			fw->risc_hdw_reg[cnt] = htons(rd_reg_word(dmp_reg));

		wrt_reg_word(&reg->pcr, 0x2000);
		qla2xxx_read_window(reg, 16, fw->risc_gp0_reg);

		wrt_reg_word(&reg->pcr, 0x2200);
		qla2xxx_read_window(reg, 16, fw->risc_gp1_reg);

		wrt_reg_word(&reg->pcr, 0x2400);
		qla2xxx_read_window(reg, 16, fw->risc_gp2_reg);

		wrt_reg_word(&reg->pcr, 0x2600);
		qla2xxx_read_window(reg, 16, fw->risc_gp3_reg);

		wrt_reg_word(&reg->pcr, 0x2800);
		qla2xxx_read_window(reg, 16, fw->risc_gp4_reg);

		wrt_reg_word(&reg->pcr, 0x2A00);
		qla2xxx_read_window(reg, 16, fw->risc_gp5_reg);

		wrt_reg_word(&reg->pcr, 0x2C00);
		qla2xxx_read_window(reg, 16, fw->risc_gp6_reg);

		wrt_reg_word(&reg->pcr, 0x2E00);
		qla2xxx_read_window(reg, 16, fw->risc_gp7_reg);

		wrt_reg_word(&reg->ctrl_status, 0x10);
		qla2xxx_read_window(reg, 64, fw->frame_buf_hdw_reg);

		wrt_reg_word(&reg->ctrl_status, 0x20);
		qla2xxx_read_window(reg, 64, fw->fpm_b0_reg);

		wrt_reg_word(&reg->ctrl_status, 0x30);
		qla2xxx_read_window(reg, 64, fw->fpm_b1_reg);

		/* Reset RISC. */
		wrt_reg_word(&reg->ctrl_status, CSR_ISP_SOFT_RESET);
		for (cnt = 0; cnt < 30000; cnt++) {
			if ((rd_reg_word(&reg->ctrl_status) &
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
					ARRAY_SIZE(fw->risc_ram), &nxt);

	/* Get stack SRAM. */
	if (rval == QLA_SUCCESS)
		rval = qla2xxx_dump_ram(ha, 0x10000, fw->stack_ram,
					ARRAY_SIZE(fw->stack_ram), &nxt);

	/* Get data SRAM. */
	if (rval == QLA_SUCCESS)
		rval = qla2xxx_dump_ram(ha, 0x11000, fw->data_ram,
		    ha->fw_memory_size - 0x11000 + 1, &nxt);

	if (rval == QLA_SUCCESS)
		qla2xxx_copy_queues(ha, nxt);

	qla2xxx_dump_post_process(base_vha, rval);
}

/**
 * qla2100_fw_dump() - Dumps binary data from the 2100/2200 firmware.
 * @vha: HA context
 */
void
qla2100_fw_dump(scsi_qla_host_t *vha)
{
	int		rval;
	uint32_t	cnt, timer;
	uint16_t	risc_address = 0;
	uint16_t	mb0 = 0, mb2 = 0;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	__le16 __iomem *dmp_reg;
	struct qla2100_fw_dump	*fw;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);

	lockdep_assert_held(&ha->hardware_lock);

	if (!ha->fw_dump) {
		ql_log(ql_log_warn, vha, 0xd004,
		    "No buffer available for dump.\n");
		return;
	}

	if (ha->fw_dumped) {
		ql_log(ql_log_warn, vha, 0xd005,
		    "Firmware has been previously dumped (%p) "
		    "-- ignoring request.\n",
		    ha->fw_dump);
		return;
	}
	fw = &ha->fw_dump->isp.isp21;
	qla2xxx_prep_dump(ha, ha->fw_dump);

	rval = QLA_SUCCESS;
	fw->hccr = htons(rd_reg_word(&reg->hccr));

	/* Pause RISC. */
	wrt_reg_word(&reg->hccr, HCCR_PAUSE_RISC);
	for (cnt = 30000; (rd_reg_word(&reg->hccr) & HCCR_RISC_PAUSE) == 0 &&
	    rval == QLA_SUCCESS; cnt--) {
		if (cnt)
			udelay(100);
		else
			rval = QLA_FUNCTION_TIMEOUT;
	}
	if (rval == QLA_SUCCESS) {
		dmp_reg = &reg->flash_address;
		for (cnt = 0; cnt < ARRAY_SIZE(fw->pbiu_reg); cnt++, dmp_reg++)
			fw->pbiu_reg[cnt] = htons(rd_reg_word(dmp_reg));

		dmp_reg = &reg->u.isp2100.mailbox0;
		for (cnt = 0; cnt < ha->mbx_count; cnt++, dmp_reg++) {
			if (cnt == 8)
				dmp_reg = &reg->u_end.isp2200.mailbox8;

			fw->mailbox_reg[cnt] = htons(rd_reg_word(dmp_reg));
		}

		dmp_reg = &reg->u.isp2100.unused_2[0];
		for (cnt = 0; cnt < ARRAY_SIZE(fw->dma_reg); cnt++, dmp_reg++)
			fw->dma_reg[cnt] = htons(rd_reg_word(dmp_reg));

		wrt_reg_word(&reg->ctrl_status, 0x00);
		dmp_reg = &reg->risc_hw;
		for (cnt = 0; cnt < ARRAY_SIZE(fw->risc_hdw_reg); cnt++, dmp_reg++)
			fw->risc_hdw_reg[cnt] = htons(rd_reg_word(dmp_reg));

		wrt_reg_word(&reg->pcr, 0x2000);
		qla2xxx_read_window(reg, 16, fw->risc_gp0_reg);

		wrt_reg_word(&reg->pcr, 0x2100);
		qla2xxx_read_window(reg, 16, fw->risc_gp1_reg);

		wrt_reg_word(&reg->pcr, 0x2200);
		qla2xxx_read_window(reg, 16, fw->risc_gp2_reg);

		wrt_reg_word(&reg->pcr, 0x2300);
		qla2xxx_read_window(reg, 16, fw->risc_gp3_reg);

		wrt_reg_word(&reg->pcr, 0x2400);
		qla2xxx_read_window(reg, 16, fw->risc_gp4_reg);

		wrt_reg_word(&reg->pcr, 0x2500);
		qla2xxx_read_window(reg, 16, fw->risc_gp5_reg);

		wrt_reg_word(&reg->pcr, 0x2600);
		qla2xxx_read_window(reg, 16, fw->risc_gp6_reg);

		wrt_reg_word(&reg->pcr, 0x2700);
		qla2xxx_read_window(reg, 16, fw->risc_gp7_reg);

		wrt_reg_word(&reg->ctrl_status, 0x10);
		qla2xxx_read_window(reg, 16, fw->frame_buf_hdw_reg);

		wrt_reg_word(&reg->ctrl_status, 0x20);
		qla2xxx_read_window(reg, 64, fw->fpm_b0_reg);

		wrt_reg_word(&reg->ctrl_status, 0x30);
		qla2xxx_read_window(reg, 64, fw->fpm_b1_reg);

		/* Reset the ISP. */
		wrt_reg_word(&reg->ctrl_status, CSR_ISP_SOFT_RESET);
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
	    (rd_reg_word(&reg->mctr) & (BIT_1 | BIT_0)) != 0))) {

		wrt_reg_word(&reg->hccr, HCCR_PAUSE_RISC);
		for (cnt = 30000;
		    (rd_reg_word(&reg->hccr) & HCCR_RISC_PAUSE) == 0 &&
		    rval == QLA_SUCCESS; cnt--) {
			if (cnt)
				udelay(100);
			else
				rval = QLA_FUNCTION_TIMEOUT;
		}
		if (rval == QLA_SUCCESS) {
			/* Set memory configuration and timing. */
			if (IS_QLA2100(ha))
				wrt_reg_word(&reg->mctr, 0xf1);
			else
				wrt_reg_word(&reg->mctr, 0xf2);
			rd_reg_word(&reg->mctr);	/* PCI Posting. */

			/* Release RISC. */
			wrt_reg_word(&reg->hccr, HCCR_RELEASE_RISC);
		}
	}

	if (rval == QLA_SUCCESS) {
		/* Get RISC SRAM. */
		risc_address = 0x1000;
 		WRT_MAILBOX_REG(ha, reg, 0, MBC_READ_RAM_WORD);
		clear_bit(MBX_INTERRUPT, &ha->mbx_cmd_flags);
	}
	for (cnt = 0; cnt < ARRAY_SIZE(fw->risc_ram) && rval == QLA_SUCCESS;
	    cnt++, risc_address++) {
 		WRT_MAILBOX_REG(ha, reg, 1, risc_address);
		wrt_reg_word(&reg->hccr, HCCR_SET_HOST_INT);

		for (timer = 6000000; timer != 0; timer--) {
			/* Check for pending interrupts. */
			if (rd_reg_word(&reg->istatus) & ISR_RISC_INT) {
				if (rd_reg_word(&reg->semaphore) & BIT_0) {
					set_bit(MBX_INTERRUPT,
					    &ha->mbx_cmd_flags);

					mb0 = RD_MAILBOX_REG(ha, reg, 0);
					mb2 = RD_MAILBOX_REG(ha, reg, 2);

					wrt_reg_word(&reg->semaphore, 0);
					wrt_reg_word(&reg->hccr,
					    HCCR_CLR_RISC_INT);
					rd_reg_word(&reg->hccr);
					break;
				}
				wrt_reg_word(&reg->hccr, HCCR_CLR_RISC_INT);
				rd_reg_word(&reg->hccr);
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
		qla2xxx_copy_queues(ha, &fw->queue_dump[0]);

	qla2xxx_dump_post_process(base_vha, rval);
}

void
qla24xx_fw_dump(scsi_qla_host_t *vha)
{
	int		rval;
	uint32_t	cnt;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	__le32 __iomem *dmp_reg;
	__be32		*iter_reg;
	__le16 __iomem *mbx_reg;
	struct qla24xx_fw_dump *fw;
	void		*nxt;
	void		*nxt_chain;
	__be32		*last_chain = NULL;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);

	lockdep_assert_held(&ha->hardware_lock);

	if (IS_P3P_TYPE(ha))
		return;

	ha->fw_dump_cap_flags = 0;

	if (!ha->fw_dump) {
		ql_log(ql_log_warn, vha, 0xd006,
		    "No buffer available for dump.\n");
		return;
	}

	if (ha->fw_dumped) {
		ql_log(ql_log_warn, vha, 0xd007,
		    "Firmware has been previously dumped (%p) "
		    "-- ignoring request.\n",
		    ha->fw_dump);
		return;
	}
	QLA_FW_STOPPED(ha);
	fw = &ha->fw_dump->isp.isp24;
	qla2xxx_prep_dump(ha, ha->fw_dump);

	fw->host_status = htonl(rd_reg_dword(&reg->host_status));

	/*
	 * Pause RISC. No need to track timeout, as resetting the chip
	 * is the right approach incase of pause timeout
	 */
	qla24xx_pause_risc(reg, ha);

	/* Host interface registers. */
	dmp_reg = &reg->flash_addr;
	for (cnt = 0; cnt < ARRAY_SIZE(fw->host_reg); cnt++, dmp_reg++)
		fw->host_reg[cnt] = htonl(rd_reg_dword(dmp_reg));

	/* Disable interrupts. */
	wrt_reg_dword(&reg->ictrl, 0);
	rd_reg_dword(&reg->ictrl);

	/* Shadow registers. */
	wrt_reg_dword(&reg->iobase_addr, 0x0F70);
	rd_reg_dword(&reg->iobase_addr);
	wrt_reg_dword(&reg->iobase_select, 0xB0000000);
	fw->shadow_reg[0] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0100000);
	fw->shadow_reg[1] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0200000);
	fw->shadow_reg[2] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0300000);
	fw->shadow_reg[3] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0400000);
	fw->shadow_reg[4] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0500000);
	fw->shadow_reg[5] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0600000);
	fw->shadow_reg[6] = htonl(rd_reg_dword(&reg->iobase_sdata));

	/* Mailbox registers. */
	mbx_reg = &reg->mailbox0;
	for (cnt = 0; cnt < ARRAY_SIZE(fw->mailbox_reg); cnt++, mbx_reg++)
		fw->mailbox_reg[cnt] = htons(rd_reg_word(mbx_reg));

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
	for (cnt = 0; cnt < 7; cnt++, dmp_reg++)
		*iter_reg++ = htonl(rd_reg_dword(dmp_reg));

	iter_reg = fw->resp0_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7300, 8, iter_reg);
	dmp_reg = &reg->iobase_q;
	for (cnt = 0; cnt < 7; cnt++, dmp_reg++)
		*iter_reg++ = htonl(rd_reg_dword(dmp_reg));

	iter_reg = fw->req1_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7400, 8, iter_reg);
	dmp_reg = &reg->iobase_q;
	for (cnt = 0; cnt < 7; cnt++, dmp_reg++)
		*iter_reg++ = htonl(rd_reg_dword(dmp_reg));

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

	nxt_chain = (void *)ha->fw_dump + ha->chain_offset;
	nxt_chain = qla2xxx_copy_atioqueues(ha, nxt_chain, &last_chain);
	if (last_chain) {
		ha->fw_dump->version |= htonl(DUMP_CHAIN_VARIANT);
		*last_chain |= htonl(DUMP_CHAIN_LAST);
	}

	/* Adjust valid length. */
	ha->fw_dump_len = (nxt_chain - (void *)ha->fw_dump);

qla24xx_fw_dump_failed_0:
	qla2xxx_dump_post_process(base_vha, rval);
}

void
qla25xx_fw_dump(scsi_qla_host_t *vha)
{
	int		rval;
	uint32_t	cnt;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	__le32 __iomem *dmp_reg;
	__be32		*iter_reg;
	__le16 __iomem *mbx_reg;
	struct qla25xx_fw_dump *fw;
	void		*nxt, *nxt_chain;
	__be32		*last_chain = NULL;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);

	lockdep_assert_held(&ha->hardware_lock);

	ha->fw_dump_cap_flags = 0;

	if (!ha->fw_dump) {
		ql_log(ql_log_warn, vha, 0xd008,
		    "No buffer available for dump.\n");
		return;
	}

	if (ha->fw_dumped) {
		ql_log(ql_log_warn, vha, 0xd009,
		    "Firmware has been previously dumped (%p) "
		    "-- ignoring request.\n",
		    ha->fw_dump);
		return;
	}
	QLA_FW_STOPPED(ha);
	fw = &ha->fw_dump->isp.isp25;
	qla2xxx_prep_dump(ha, ha->fw_dump);
	ha->fw_dump->version = htonl(2);

	fw->host_status = htonl(rd_reg_dword(&reg->host_status));

	/*
	 * Pause RISC. No need to track timeout, as resetting the chip
	 * is the right approach incase of pause timeout
	 */
	qla24xx_pause_risc(reg, ha);

	/* Host/Risc registers. */
	iter_reg = fw->host_risc_reg;
	iter_reg = qla24xx_read_window(reg, 0x7000, 16, iter_reg);
	qla24xx_read_window(reg, 0x7010, 16, iter_reg);

	/* PCIe registers. */
	wrt_reg_dword(&reg->iobase_addr, 0x7C00);
	rd_reg_dword(&reg->iobase_addr);
	wrt_reg_dword(&reg->iobase_window, 0x01);
	dmp_reg = &reg->iobase_c4;
	fw->pcie_regs[0] = htonl(rd_reg_dword(dmp_reg));
	dmp_reg++;
	fw->pcie_regs[1] = htonl(rd_reg_dword(dmp_reg));
	dmp_reg++;
	fw->pcie_regs[2] = htonl(rd_reg_dword(dmp_reg));
	fw->pcie_regs[3] = htonl(rd_reg_dword(&reg->iobase_window));

	wrt_reg_dword(&reg->iobase_window, 0x00);
	rd_reg_dword(&reg->iobase_window);

	/* Host interface registers. */
	dmp_reg = &reg->flash_addr;
	for (cnt = 0; cnt < ARRAY_SIZE(fw->host_reg); cnt++, dmp_reg++)
		fw->host_reg[cnt] = htonl(rd_reg_dword(dmp_reg));

	/* Disable interrupts. */
	wrt_reg_dword(&reg->ictrl, 0);
	rd_reg_dword(&reg->ictrl);

	/* Shadow registers. */
	wrt_reg_dword(&reg->iobase_addr, 0x0F70);
	rd_reg_dword(&reg->iobase_addr);
	wrt_reg_dword(&reg->iobase_select, 0xB0000000);
	fw->shadow_reg[0] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0100000);
	fw->shadow_reg[1] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0200000);
	fw->shadow_reg[2] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0300000);
	fw->shadow_reg[3] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0400000);
	fw->shadow_reg[4] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0500000);
	fw->shadow_reg[5] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0600000);
	fw->shadow_reg[6] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0700000);
	fw->shadow_reg[7] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0800000);
	fw->shadow_reg[8] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0900000);
	fw->shadow_reg[9] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0A00000);
	fw->shadow_reg[10] = htonl(rd_reg_dword(&reg->iobase_sdata));

	/* RISC I/O register. */
	wrt_reg_dword(&reg->iobase_addr, 0x0010);
	fw->risc_io_reg = htonl(rd_reg_dword(&reg->iobase_window));

	/* Mailbox registers. */
	mbx_reg = &reg->mailbox0;
	for (cnt = 0; cnt < ARRAY_SIZE(fw->mailbox_reg); cnt++, mbx_reg++)
		fw->mailbox_reg[cnt] = htons(rd_reg_word(mbx_reg));

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
	for (cnt = 0; cnt < 7; cnt++, dmp_reg++)
		*iter_reg++ = htonl(rd_reg_dword(dmp_reg));

	iter_reg = fw->resp0_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7300, 8, iter_reg);
	dmp_reg = &reg->iobase_q;
	for (cnt = 0; cnt < 7; cnt++, dmp_reg++)
		*iter_reg++ = htonl(rd_reg_dword(dmp_reg));

	iter_reg = fw->req1_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7400, 8, iter_reg);
	dmp_reg = &reg->iobase_q;
	for (cnt = 0; cnt < 7; cnt++, dmp_reg++)
		*iter_reg++ = htonl(rd_reg_dword(dmp_reg));

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

	qla24xx_copy_eft(ha, nxt);

	/* Chain entries -- started with MQ. */
	nxt_chain = qla25xx_copy_fce(ha, nxt_chain, &last_chain);
	nxt_chain = qla25xx_copy_mqueues(ha, nxt_chain, &last_chain);
	nxt_chain = qla2xxx_copy_atioqueues(ha, nxt_chain, &last_chain);
	nxt_chain = qla25xx_copy_exlogin(ha, nxt_chain, &last_chain);
	if (last_chain) {
		ha->fw_dump->version |= htonl(DUMP_CHAIN_VARIANT);
		*last_chain |= htonl(DUMP_CHAIN_LAST);
	}

	/* Adjust valid length. */
	ha->fw_dump_len = (nxt_chain - (void *)ha->fw_dump);

qla25xx_fw_dump_failed_0:
	qla2xxx_dump_post_process(base_vha, rval);
}

void
qla81xx_fw_dump(scsi_qla_host_t *vha)
{
	int		rval;
	uint32_t	cnt;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	__le32 __iomem *dmp_reg;
	__be32		*iter_reg;
	__le16 __iomem *mbx_reg;
	struct qla81xx_fw_dump *fw;
	void		*nxt, *nxt_chain;
	__be32		*last_chain = NULL;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);

	lockdep_assert_held(&ha->hardware_lock);

	ha->fw_dump_cap_flags = 0;

	if (!ha->fw_dump) {
		ql_log(ql_log_warn, vha, 0xd00a,
		    "No buffer available for dump.\n");
		return;
	}

	if (ha->fw_dumped) {
		ql_log(ql_log_warn, vha, 0xd00b,
		    "Firmware has been previously dumped (%p) "
		    "-- ignoring request.\n",
		    ha->fw_dump);
		return;
	}
	fw = &ha->fw_dump->isp.isp81;
	qla2xxx_prep_dump(ha, ha->fw_dump);

	fw->host_status = htonl(rd_reg_dword(&reg->host_status));

	/*
	 * Pause RISC. No need to track timeout, as resetting the chip
	 * is the right approach incase of pause timeout
	 */
	qla24xx_pause_risc(reg, ha);

	/* Host/Risc registers. */
	iter_reg = fw->host_risc_reg;
	iter_reg = qla24xx_read_window(reg, 0x7000, 16, iter_reg);
	qla24xx_read_window(reg, 0x7010, 16, iter_reg);

	/* PCIe registers. */
	wrt_reg_dword(&reg->iobase_addr, 0x7C00);
	rd_reg_dword(&reg->iobase_addr);
	wrt_reg_dword(&reg->iobase_window, 0x01);
	dmp_reg = &reg->iobase_c4;
	fw->pcie_regs[0] = htonl(rd_reg_dword(dmp_reg));
	dmp_reg++;
	fw->pcie_regs[1] = htonl(rd_reg_dword(dmp_reg));
	dmp_reg++;
	fw->pcie_regs[2] = htonl(rd_reg_dword(dmp_reg));
	fw->pcie_regs[3] = htonl(rd_reg_dword(&reg->iobase_window));

	wrt_reg_dword(&reg->iobase_window, 0x00);
	rd_reg_dword(&reg->iobase_window);

	/* Host interface registers. */
	dmp_reg = &reg->flash_addr;
	for (cnt = 0; cnt < ARRAY_SIZE(fw->host_reg); cnt++, dmp_reg++)
		fw->host_reg[cnt] = htonl(rd_reg_dword(dmp_reg));

	/* Disable interrupts. */
	wrt_reg_dword(&reg->ictrl, 0);
	rd_reg_dword(&reg->ictrl);

	/* Shadow registers. */
	wrt_reg_dword(&reg->iobase_addr, 0x0F70);
	rd_reg_dword(&reg->iobase_addr);
	wrt_reg_dword(&reg->iobase_select, 0xB0000000);
	fw->shadow_reg[0] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0100000);
	fw->shadow_reg[1] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0200000);
	fw->shadow_reg[2] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0300000);
	fw->shadow_reg[3] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0400000);
	fw->shadow_reg[4] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0500000);
	fw->shadow_reg[5] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0600000);
	fw->shadow_reg[6] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0700000);
	fw->shadow_reg[7] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0800000);
	fw->shadow_reg[8] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0900000);
	fw->shadow_reg[9] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0A00000);
	fw->shadow_reg[10] = htonl(rd_reg_dword(&reg->iobase_sdata));

	/* RISC I/O register. */
	wrt_reg_dword(&reg->iobase_addr, 0x0010);
	fw->risc_io_reg = htonl(rd_reg_dword(&reg->iobase_window));

	/* Mailbox registers. */
	mbx_reg = &reg->mailbox0;
	for (cnt = 0; cnt < ARRAY_SIZE(fw->mailbox_reg); cnt++, mbx_reg++)
		fw->mailbox_reg[cnt] = htons(rd_reg_word(mbx_reg));

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
	for (cnt = 0; cnt < 7; cnt++, dmp_reg++)
		*iter_reg++ = htonl(rd_reg_dword(dmp_reg));

	iter_reg = fw->resp0_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7300, 8, iter_reg);
	dmp_reg = &reg->iobase_q;
	for (cnt = 0; cnt < 7; cnt++, dmp_reg++)
		*iter_reg++ = htonl(rd_reg_dword(dmp_reg));

	iter_reg = fw->req1_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7400, 8, iter_reg);
	dmp_reg = &reg->iobase_q;
	for (cnt = 0; cnt < 7; cnt++, dmp_reg++)
		*iter_reg++ = htonl(rd_reg_dword(dmp_reg));

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

	qla24xx_copy_eft(ha, nxt);

	/* Chain entries -- started with MQ. */
	nxt_chain = qla25xx_copy_fce(ha, nxt_chain, &last_chain);
	nxt_chain = qla25xx_copy_mqueues(ha, nxt_chain, &last_chain);
	nxt_chain = qla2xxx_copy_atioqueues(ha, nxt_chain, &last_chain);
	nxt_chain = qla25xx_copy_exlogin(ha, nxt_chain, &last_chain);
	nxt_chain = qla81xx_copy_exchoffld(ha, nxt_chain, &last_chain);
	if (last_chain) {
		ha->fw_dump->version |= htonl(DUMP_CHAIN_VARIANT);
		*last_chain |= htonl(DUMP_CHAIN_LAST);
	}

	/* Adjust valid length. */
	ha->fw_dump_len = (nxt_chain - (void *)ha->fw_dump);

qla81xx_fw_dump_failed_0:
	qla2xxx_dump_post_process(base_vha, rval);
}

void
qla83xx_fw_dump(scsi_qla_host_t *vha)
{
	int		rval;
	uint32_t	cnt;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_24xx __iomem *reg = &ha->iobase->isp24;
	__le32 __iomem *dmp_reg;
	__be32		*iter_reg;
	__le16 __iomem *mbx_reg;
	struct qla83xx_fw_dump *fw;
	void		*nxt, *nxt_chain;
	__be32		*last_chain = NULL;
	struct scsi_qla_host *base_vha = pci_get_drvdata(ha->pdev);

	lockdep_assert_held(&ha->hardware_lock);

	ha->fw_dump_cap_flags = 0;

	if (!ha->fw_dump) {
		ql_log(ql_log_warn, vha, 0xd00c,
		    "No buffer available for dump!!!\n");
		return;
	}

	if (ha->fw_dumped) {
		ql_log(ql_log_warn, vha, 0xd00d,
		    "Firmware has been previously dumped (%p) -- ignoring "
		    "request...\n", ha->fw_dump);
		return;
	}
	QLA_FW_STOPPED(ha);
	fw = &ha->fw_dump->isp.isp83;
	qla2xxx_prep_dump(ha, ha->fw_dump);

	fw->host_status = htonl(rd_reg_dword(&reg->host_status));

	/*
	 * Pause RISC. No need to track timeout, as resetting the chip
	 * is the right approach incase of pause timeout
	 */
	qla24xx_pause_risc(reg, ha);

	wrt_reg_dword(&reg->iobase_addr, 0x6000);
	dmp_reg = &reg->iobase_window;
	rd_reg_dword(dmp_reg);
	wrt_reg_dword(dmp_reg, 0);

	dmp_reg = &reg->unused_4_1[0];
	rd_reg_dword(dmp_reg);
	wrt_reg_dword(dmp_reg, 0);

	wrt_reg_dword(&reg->iobase_addr, 0x6010);
	dmp_reg = &reg->unused_4_1[2];
	rd_reg_dword(dmp_reg);
	wrt_reg_dword(dmp_reg, 0);

	/* select PCR and disable ecc checking and correction */
	wrt_reg_dword(&reg->iobase_addr, 0x0F70);
	rd_reg_dword(&reg->iobase_addr);
	wrt_reg_dword(&reg->iobase_select, 0x60000000);	/* write to F0h = PCR */

	/* Host/Risc registers. */
	iter_reg = fw->host_risc_reg;
	iter_reg = qla24xx_read_window(reg, 0x7000, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x7010, 16, iter_reg);
	qla24xx_read_window(reg, 0x7040, 16, iter_reg);

	/* PCIe registers. */
	wrt_reg_dword(&reg->iobase_addr, 0x7C00);
	rd_reg_dword(&reg->iobase_addr);
	wrt_reg_dword(&reg->iobase_window, 0x01);
	dmp_reg = &reg->iobase_c4;
	fw->pcie_regs[0] = htonl(rd_reg_dword(dmp_reg));
	dmp_reg++;
	fw->pcie_regs[1] = htonl(rd_reg_dword(dmp_reg));
	dmp_reg++;
	fw->pcie_regs[2] = htonl(rd_reg_dword(dmp_reg));
	fw->pcie_regs[3] = htonl(rd_reg_dword(&reg->iobase_window));

	wrt_reg_dword(&reg->iobase_window, 0x00);
	rd_reg_dword(&reg->iobase_window);

	/* Host interface registers. */
	dmp_reg = &reg->flash_addr;
	for (cnt = 0; cnt < ARRAY_SIZE(fw->host_reg); cnt++, dmp_reg++)
		fw->host_reg[cnt] = htonl(rd_reg_dword(dmp_reg));

	/* Disable interrupts. */
	wrt_reg_dword(&reg->ictrl, 0);
	rd_reg_dword(&reg->ictrl);

	/* Shadow registers. */
	wrt_reg_dword(&reg->iobase_addr, 0x0F70);
	rd_reg_dword(&reg->iobase_addr);
	wrt_reg_dword(&reg->iobase_select, 0xB0000000);
	fw->shadow_reg[0] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0100000);
	fw->shadow_reg[1] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0200000);
	fw->shadow_reg[2] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0300000);
	fw->shadow_reg[3] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0400000);
	fw->shadow_reg[4] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0500000);
	fw->shadow_reg[5] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0600000);
	fw->shadow_reg[6] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0700000);
	fw->shadow_reg[7] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0800000);
	fw->shadow_reg[8] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0900000);
	fw->shadow_reg[9] = htonl(rd_reg_dword(&reg->iobase_sdata));

	wrt_reg_dword(&reg->iobase_select, 0xB0A00000);
	fw->shadow_reg[10] = htonl(rd_reg_dword(&reg->iobase_sdata));

	/* RISC I/O register. */
	wrt_reg_dword(&reg->iobase_addr, 0x0010);
	fw->risc_io_reg = htonl(rd_reg_dword(&reg->iobase_window));

	/* Mailbox registers. */
	mbx_reg = &reg->mailbox0;
	for (cnt = 0; cnt < ARRAY_SIZE(fw->mailbox_reg); cnt++, mbx_reg++)
		fw->mailbox_reg[cnt] = htons(rd_reg_word(mbx_reg));

	/* Transfer sequence registers. */
	iter_reg = fw->xseq_gp_reg;
	iter_reg = qla24xx_read_window(reg, 0xBE00, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBE10, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBE20, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBE30, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBE40, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBE50, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBE60, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xBE70, 16, iter_reg);
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

	qla24xx_read_window(reg, 0xBEF0, 16, fw->xseq_2_reg);

	/* Receive sequence registers. */
	iter_reg = fw->rseq_gp_reg;
	iter_reg = qla24xx_read_window(reg, 0xFE00, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFE10, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFE20, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFE30, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFE40, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFE50, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFE60, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xFE70, 16, iter_reg);
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
	qla24xx_read_window(reg, 0xFEF0, 16, fw->rseq_3_reg);

	/* Auxiliary sequence registers. */
	iter_reg = fw->aseq_gp_reg;
	iter_reg = qla24xx_read_window(reg, 0xB000, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB010, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB020, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB030, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB040, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB050, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB060, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB070, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB100, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB110, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB120, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB130, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB140, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB150, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0xB160, 16, iter_reg);
	qla24xx_read_window(reg, 0xB170, 16, iter_reg);

	iter_reg = fw->aseq_0_reg;
	iter_reg = qla24xx_read_window(reg, 0xB0C0, 16, iter_reg);
	qla24xx_read_window(reg, 0xB0D0, 16, iter_reg);

	qla24xx_read_window(reg, 0xB0E0, 16, fw->aseq_1_reg);
	qla24xx_read_window(reg, 0xB0F0, 16, fw->aseq_2_reg);
	qla24xx_read_window(reg, 0xB1F0, 16, fw->aseq_3_reg);

	/* Command DMA registers. */
	iter_reg = fw->cmd_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7100, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x7120, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x7130, 16, iter_reg);
	qla24xx_read_window(reg, 0x71F0, 16, iter_reg);

	/* Queues. */
	iter_reg = fw->req0_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7200, 8, iter_reg);
	dmp_reg = &reg->iobase_q;
	for (cnt = 0; cnt < 7; cnt++, dmp_reg++)
		*iter_reg++ = htonl(rd_reg_dword(dmp_reg));

	iter_reg = fw->resp0_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7300, 8, iter_reg);
	dmp_reg = &reg->iobase_q;
	for (cnt = 0; cnt < 7; cnt++, dmp_reg++)
		*iter_reg++ = htonl(rd_reg_dword(dmp_reg));

	iter_reg = fw->req1_dma_reg;
	iter_reg = qla24xx_read_window(reg, 0x7400, 8, iter_reg);
	dmp_reg = &reg->iobase_q;
	for (cnt = 0; cnt < 7; cnt++, dmp_reg++)
		*iter_reg++ = htonl(rd_reg_dword(dmp_reg));

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
	iter_reg = qla24xx_read_window(reg, 0x40D0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x40E0, 16, iter_reg);
	qla24xx_read_window(reg, 0x40F0, 16, iter_reg);

	/* RQ0 Array registers. */
	iter_reg = fw->rq0_array_reg;
	iter_reg = qla24xx_read_window(reg, 0x5C00, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5C10, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5C20, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5C30, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5C40, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5C50, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5C60, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5C70, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5C80, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5C90, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5CA0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5CB0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5CC0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5CD0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5CE0, 16, iter_reg);
	qla24xx_read_window(reg, 0x5CF0, 16, iter_reg);

	/* RQ1 Array registers. */
	iter_reg = fw->rq1_array_reg;
	iter_reg = qla24xx_read_window(reg, 0x5D00, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5D10, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5D20, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5D30, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5D40, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5D50, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5D60, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5D70, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5D80, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5D90, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5DA0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5DB0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5DC0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5DD0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5DE0, 16, iter_reg);
	qla24xx_read_window(reg, 0x5DF0, 16, iter_reg);

	/* RP0 Array registers. */
	iter_reg = fw->rp0_array_reg;
	iter_reg = qla24xx_read_window(reg, 0x5E00, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5E10, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5E20, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5E30, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5E40, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5E50, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5E60, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5E70, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5E80, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5E90, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5EA0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5EB0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5EC0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5ED0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5EE0, 16, iter_reg);
	qla24xx_read_window(reg, 0x5EF0, 16, iter_reg);

	/* RP1 Array registers. */
	iter_reg = fw->rp1_array_reg;
	iter_reg = qla24xx_read_window(reg, 0x5F00, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5F10, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5F20, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5F30, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5F40, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5F50, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5F60, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5F70, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5F80, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5F90, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5FA0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5FB0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5FC0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5FD0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x5FE0, 16, iter_reg);
	qla24xx_read_window(reg, 0x5FF0, 16, iter_reg);

	iter_reg = fw->at0_array_reg;
	iter_reg = qla24xx_read_window(reg, 0x7080, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x7090, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x70A0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x70B0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x70C0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x70D0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x70E0, 16, iter_reg);
	qla24xx_read_window(reg, 0x70F0, 16, iter_reg);

	/* I/O Queue Control registers. */
	qla24xx_read_window(reg, 0x7800, 16, fw->queue_control_reg);

	/* Frame Buffer registers. */
	iter_reg = fw->fb_hdw_reg;
	iter_reg = qla24xx_read_window(reg, 0x6000, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6010, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6020, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6030, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6040, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6060, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6070, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6100, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6130, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6150, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6170, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6190, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x61B0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x61C0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6530, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6540, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6550, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6560, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6570, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6580, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x6590, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x65A0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x65B0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x65C0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x65D0, 16, iter_reg);
	iter_reg = qla24xx_read_window(reg, 0x65E0, 16, iter_reg);
	qla24xx_read_window(reg, 0x6F00, 16, iter_reg);

	/* Multi queue registers */
	nxt_chain = qla25xx_copy_mq(ha, (void *)ha->fw_dump + ha->chain_offset,
	    &last_chain);

	rval = qla24xx_soft_reset(ha);
	if (rval != QLA_SUCCESS) {
		ql_log(ql_log_warn, vha, 0xd00e,
		    "SOFT RESET FAILED, forcing continuation of dump!!!\n");
		rval = QLA_SUCCESS;

		ql_log(ql_log_warn, vha, 0xd00f, "try a bigger hammer!!!\n");

		wrt_reg_dword(&reg->hccr, HCCRX_SET_RISC_RESET);
		rd_reg_dword(&reg->hccr);

		wrt_reg_dword(&reg->hccr, HCCRX_REL_RISC_PAUSE);
		rd_reg_dword(&reg->hccr);

		wrt_reg_dword(&reg->hccr, HCCRX_CLR_RISC_RESET);
		rd_reg_dword(&reg->hccr);

		for (cnt = 30000; cnt && (rd_reg_word(&reg->mailbox0)); cnt--)
			udelay(5);

		if (!cnt) {
			nxt = fw->code_ram;
			nxt += sizeof(fw->code_ram);
			nxt += (ha->fw_memory_size - 0x100000 + 1);
			goto copy_queue;
		} else {
			set_bit(RISC_RDY_AFT_RESET, &ha->fw_dump_cap_flags);
			ql_log(ql_log_warn, vha, 0xd010,
			    "bigger hammer success?\n");
		}
	}

	rval = qla24xx_dump_memory(ha, fw->code_ram, sizeof(fw->code_ram),
	    &nxt);
	if (rval != QLA_SUCCESS)
		goto qla83xx_fw_dump_failed_0;

copy_queue:
	nxt = qla2xxx_copy_queues(ha, nxt);

	qla24xx_copy_eft(ha, nxt);

	/* Chain entries -- started with MQ. */
	nxt_chain = qla25xx_copy_fce(ha, nxt_chain, &last_chain);
	nxt_chain = qla25xx_copy_mqueues(ha, nxt_chain, &last_chain);
	nxt_chain = qla2xxx_copy_atioqueues(ha, nxt_chain, &last_chain);
	nxt_chain = qla25xx_copy_exlogin(ha, nxt_chain, &last_chain);
	nxt_chain = qla81xx_copy_exchoffld(ha, nxt_chain, &last_chain);
	if (last_chain) {
		ha->fw_dump->version |= htonl(DUMP_CHAIN_VARIANT);
		*last_chain |= htonl(DUMP_CHAIN_LAST);
	}

	/* Adjust valid length. */
	ha->fw_dump_len = (nxt_chain - (void *)ha->fw_dump);

qla83xx_fw_dump_failed_0:
	qla2xxx_dump_post_process(base_vha, rval);
}

/****************************************************************************/
/*                         Driver Debug Functions.                          */
/****************************************************************************/

/* Write the debug message prefix into @pbuf. */
static void ql_dbg_prefix(char *pbuf, int pbuf_size,
			  const scsi_qla_host_t *vha, uint msg_id)
{
	if (vha) {
		const struct pci_dev *pdev = vha->hw->pdev;

		/* <module-name> [<dev-name>]-<msg-id>:<host>: */
		snprintf(pbuf, pbuf_size, "%s [%s]-%04x:%lu: ", QL_MSGHDR,
			 dev_name(&(pdev->dev)), msg_id, vha->host_no);
	} else {
		/* <module-name> [<dev-name>]-<msg-id>: : */
		snprintf(pbuf, pbuf_size, "%s [%s]-%04x: : ", QL_MSGHDR,
			 "0000:00:00.0", msg_id);
	}
}

/*
 * This function is for formatting and logging debug information.
 * It is to be used when vha is available. It formats the message
 * and logs it to the messages file.
 * parameters:
 * level: The level of the debug messages to be printed.
 *        If ql2xextended_error_logging value is correctly set,
 *        this message will appear in the messages file.
 * vha:   Pointer to the scsi_qla_host_t.
 * id:    This is a unique identifier for the level. It identifies the
 *        part of the code from where the message originated.
 * msg:   The message to be displayed.
 */
void
ql_dbg(uint level, scsi_qla_host_t *vha, uint id, const char *fmt, ...)
{
	va_list va;
	struct va_format vaf;
	char pbuf[64];

	if (!ql_mask_match(level) && !trace_ql_dbg_log_enabled())
		return;

	va_start(va, fmt);

	vaf.fmt = fmt;
	vaf.va = &va;

	ql_dbg_prefix(pbuf, ARRAY_SIZE(pbuf), vha, id);

	if (!ql_mask_match(level))
		trace_ql_dbg_log(pbuf, &vaf);
	else
		pr_warn("%s%pV", pbuf, &vaf);

	va_end(va);

}

/*
 * This function is for formatting and logging debug information.
 * It is to be used when vha is not available and pci is available,
 * i.e., before host allocation. It formats the message and logs it
 * to the messages file.
 * parameters:
 * level: The level of the debug messages to be printed.
 *        If ql2xextended_error_logging value is correctly set,
 *        this message will appear in the messages file.
 * pdev:  Pointer to the struct pci_dev.
 * id:    This is a unique id for the level. It identifies the part
 *        of the code from where the message originated.
 * msg:   The message to be displayed.
 */
void
ql_dbg_pci(uint level, struct pci_dev *pdev, uint id, const char *fmt, ...)
{
	va_list va;
	struct va_format vaf;
	char pbuf[128];

	if (pdev == NULL)
		return;
	if (!ql_mask_match(level))
		return;

	va_start(va, fmt);

	vaf.fmt = fmt;
	vaf.va = &va;

	ql_dbg_prefix(pbuf, ARRAY_SIZE(pbuf), NULL, id + ql_dbg_offset);
	pr_warn("%s%pV", pbuf, &vaf);

	va_end(va);
}

/*
 * This function is for formatting and logging log messages.
 * It is to be used when vha is available. It formats the message
 * and logs it to the messages file. All the messages will be logged
 * irrespective of value of ql2xextended_error_logging.
 * parameters:
 * level: The level of the log messages to be printed in the
 *        messages file.
 * vha:   Pointer to the scsi_qla_host_t
 * id:    This is a unique id for the level. It identifies the
 *        part of the code from where the message originated.
 * msg:   The message to be displayed.
 */
void
ql_log(uint level, scsi_qla_host_t *vha, uint id, const char *fmt, ...)
{
	va_list va;
	struct va_format vaf;
	char pbuf[128];

	if (level > ql_errlev)
		return;

	ql_dbg_prefix(pbuf, ARRAY_SIZE(pbuf), vha, id);

	va_start(va, fmt);

	vaf.fmt = fmt;
	vaf.va = &va;

	switch (level) {
	case ql_log_fatal: /* FATAL LOG */
		pr_crit("%s%pV", pbuf, &vaf);
		break;
	case ql_log_warn:
		pr_err("%s%pV", pbuf, &vaf);
		break;
	case ql_log_info:
		pr_warn("%s%pV", pbuf, &vaf);
		break;
	default:
		pr_info("%s%pV", pbuf, &vaf);
		break;
	}

	va_end(va);
}

/*
 * This function is for formatting and logging log messages.
 * It is to be used when vha is not available and pci is available,
 * i.e., before host allocation. It formats the message and logs
 * it to the messages file. All the messages are logged irrespective
 * of the value of ql2xextended_error_logging.
 * parameters:
 * level: The level of the log messages to be printed in the
 *        messages file.
 * pdev:  Pointer to the struct pci_dev.
 * id:    This is a unique id for the level. It identifies the
 *        part of the code from where the message originated.
 * msg:   The message to be displayed.
 */
void
ql_log_pci(uint level, struct pci_dev *pdev, uint id, const char *fmt, ...)
{
	va_list va;
	struct va_format vaf;
	char pbuf[128];

	if (pdev == NULL)
		return;
	if (level > ql_errlev)
		return;

	ql_dbg_prefix(pbuf, ARRAY_SIZE(pbuf), NULL, id);

	va_start(va, fmt);

	vaf.fmt = fmt;
	vaf.va = &va;

	switch (level) {
	case ql_log_fatal: /* FATAL LOG */
		pr_crit("%s%pV", pbuf, &vaf);
		break;
	case ql_log_warn:
		pr_err("%s%pV", pbuf, &vaf);
		break;
	case ql_log_info:
		pr_warn("%s%pV", pbuf, &vaf);
		break;
	default:
		pr_info("%s%pV", pbuf, &vaf);
		break;
	}

	va_end(va);
}

void
ql_dump_regs(uint level, scsi_qla_host_t *vha, uint id)
{
	int i;
	struct qla_hw_data *ha = vha->hw;
	struct device_reg_2xxx __iomem *reg = &ha->iobase->isp;
	struct device_reg_24xx __iomem *reg24 = &ha->iobase->isp24;
	struct device_reg_82xx __iomem *reg82 = &ha->iobase->isp82;
	__le16 __iomem *mbx_reg;

	if (!ql_mask_match(level))
		return;

	if (IS_P3P_TYPE(ha))
		mbx_reg = &reg82->mailbox_in[0];
	else if (IS_FWI2_CAPABLE(ha))
		mbx_reg = &reg24->mailbox0;
	else
		mbx_reg = MAILBOX_REG(ha, reg, 0);

	ql_dbg(level, vha, id, "Mailbox registers:\n");
	for (i = 0; i < 6; i++, mbx_reg++)
		ql_dbg(level, vha, id,
		    "mbox[%d] %#04x\n", i, rd_reg_word(mbx_reg));
}

void
ql_dump_buffer(uint level, scsi_qla_host_t *vha, uint id, const void *buf,
	       uint size)
{
	uint cnt;

	if (!ql_mask_match(level))
		return;

	ql_dbg(level, vha, id,
	    "%-+5d  0  1  2  3  4  5  6  7  8  9  A  B  C  D  E  F\n", size);
	ql_dbg(level, vha, id,
	    "----- -----------------------------------------------\n");
	for (cnt = 0; cnt < size; cnt += 16) {
		ql_dbg(level, vha, id, "%04x: ", cnt);
		print_hex_dump(KERN_CONT, "", DUMP_PREFIX_NONE, 16, 1,
			       buf + cnt, min(16U, size - cnt), false);
	}
}

/*
 * This function is for formatting and logging log messages.
 * It is to be used when vha is available. It formats the message
 * and logs it to the messages file. All the messages will be logged
 * irrespective of value of ql2xextended_error_logging.
 * parameters:
 * level: The level of the log messages to be printed in the
 *        messages file.
 * vha:   Pointer to the scsi_qla_host_t
 * id:    This is a unique id for the level. It identifies the
 *        part of the code from where the message originated.
 * msg:   The message to be displayed.
 */
void
ql_log_qp(uint32_t level, struct qla_qpair *qpair, int32_t id,
    const char *fmt, ...)
{
	va_list va;
	struct va_format vaf;
	char pbuf[128];

	if (level > ql_errlev)
		return;

	ql_dbg_prefix(pbuf, ARRAY_SIZE(pbuf), qpair ? qpair->vha : NULL, id);

	va_start(va, fmt);

	vaf.fmt = fmt;
	vaf.va = &va;

	switch (level) {
	case ql_log_fatal: /* FATAL LOG */
		pr_crit("%s%pV", pbuf, &vaf);
		break;
	case ql_log_warn:
		pr_err("%s%pV", pbuf, &vaf);
		break;
	case ql_log_info:
		pr_warn("%s%pV", pbuf, &vaf);
		break;
	default:
		pr_info("%s%pV", pbuf, &vaf);
		break;
	}

	va_end(va);
}

/*
 * This function is for formatting and logging debug information.
 * It is to be used when vha is available. It formats the message
 * and logs it to the messages file.
 * parameters:
 * level: The level of the debug messages to be printed.
 *        If ql2xextended_error_logging value is correctly set,
 *        this message will appear in the messages file.
 * vha:   Pointer to the scsi_qla_host_t.
 * id:    This is a unique identifier for the level. It identifies the
 *        part of the code from where the message originated.
 * msg:   The message to be displayed.
 */
void
ql_dbg_qp(uint32_t level, struct qla_qpair *qpair, int32_t id,
    const char *fmt, ...)
{
	va_list va;
	struct va_format vaf;
	char pbuf[128];

	if (!ql_mask_match(level))
		return;

	va_start(va, fmt);

	vaf.fmt = fmt;
	vaf.va = &va;

	ql_dbg_prefix(pbuf, ARRAY_SIZE(pbuf), qpair ? qpair->vha : NULL,
		      id + ql_dbg_offset);
	pr_warn("%s%pV", pbuf, &vaf);

	va_end(va);

}
