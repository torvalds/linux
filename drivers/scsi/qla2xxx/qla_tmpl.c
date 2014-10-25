/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2014 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
#include "qla_def.h"
#include "qla_tmpl.h"

/* note default template is in big endian */
static const uint32_t ql27xx_fwdt_default_template[] = {
	0x63000000, 0xa4000000, 0x7c050000, 0x00000000,
	0x30000000, 0x01000000, 0x00000000, 0xc0406eb4,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x00000000, 0x00000000, 0x00000000,
	0x00000000, 0x04010000, 0x14000000, 0x00000000,
	0x02000000, 0x44000000, 0x09010000, 0x10000000,
	0x00000000, 0x02000000, 0x01010000, 0x1c000000,
	0x00000000, 0x02000000, 0x00600000, 0x00000000,
	0xc0000000, 0x01010000, 0x1c000000, 0x00000000,
	0x02000000, 0x00600000, 0x00000000, 0xcc000000,
	0x01010000, 0x1c000000, 0x00000000, 0x02000000,
	0x10600000, 0x00000000, 0xd4000000, 0x01010000,
	0x1c000000, 0x00000000, 0x02000000, 0x700f0000,
	0x00000060, 0xf0000000, 0x00010000, 0x18000000,
	0x00000000, 0x02000000, 0x00700000, 0x041000c0,
	0x00010000, 0x18000000, 0x00000000, 0x02000000,
	0x10700000, 0x041000c0, 0x00010000, 0x18000000,
	0x00000000, 0x02000000, 0x40700000, 0x041000c0,
	0x01010000, 0x1c000000, 0x00000000, 0x02000000,
	0x007c0000, 0x01000000, 0xc0000000, 0x00010000,
	0x18000000, 0x00000000, 0x02000000, 0x007c0000,
	0x040300c4, 0x00010000, 0x18000000, 0x00000000,
	0x02000000, 0x007c0000, 0x040100c0, 0x01010000,
	0x1c000000, 0x00000000, 0x02000000, 0x007c0000,
	0x00000000, 0xc0000000, 0x00010000, 0x18000000,
	0x00000000, 0x02000000, 0x007c0000, 0x04200000,
	0x0b010000, 0x18000000, 0x00000000, 0x02000000,
	0x0c000000, 0x00000000, 0x02010000, 0x20000000,
	0x00000000, 0x02000000, 0x700f0000, 0x040100fc,
	0xf0000000, 0x000000b0, 0x02010000, 0x20000000,
	0x00000000, 0x02000000, 0x700f0000, 0x040100fc,
	0xf0000000, 0x000010b0, 0x02010000, 0x20000000,
	0x00000000, 0x02000000, 0x700f0000, 0x040100fc,
	0xf0000000, 0x000020b0, 0x02010000, 0x20000000,
	0x00000000, 0x02000000, 0x700f0000, 0x040100fc,
	0xf0000000, 0x000030b0, 0x02010000, 0x20000000,
	0x00000000, 0x02000000, 0x700f0000, 0x040100fc,
	0xf0000000, 0x000040b0, 0x02010000, 0x20000000,
	0x00000000, 0x02000000, 0x700f0000, 0x040100fc,
	0xf0000000, 0x000050b0, 0x02010000, 0x20000000,
	0x00000000, 0x02000000, 0x700f0000, 0x040100fc,
	0xf0000000, 0x000060b0, 0x02010000, 0x20000000,
	0x00000000, 0x02000000, 0x700f0000, 0x040100fc,
	0xf0000000, 0x000070b0, 0x02010000, 0x20000000,
	0x00000000, 0x02000000, 0x700f0000, 0x040100fc,
	0xf0000000, 0x000080b0, 0x02010000, 0x20000000,
	0x00000000, 0x02000000, 0x700f0000, 0x040100fc,
	0xf0000000, 0x000090b0, 0x02010000, 0x20000000,
	0x00000000, 0x02000000, 0x700f0000, 0x040100fc,
	0xf0000000, 0x0000a0b0, 0x00010000, 0x18000000,
	0x00000000, 0x02000000, 0x0a000000, 0x040100c0,
	0x00010000, 0x18000000, 0x00000000, 0x02000000,
	0x0a000000, 0x04200080, 0x00010000, 0x18000000,
	0x00000000, 0x02000000, 0x00be0000, 0x041000c0,
	0x00010000, 0x18000000, 0x00000000, 0x02000000,
	0x10be0000, 0x041000c0, 0x00010000, 0x18000000,
	0x00000000, 0x02000000, 0x20be0000, 0x041000c0,
	0x00010000, 0x18000000, 0x00000000, 0x02000000,
	0x30be0000, 0x041000c0, 0x00010000, 0x18000000,
	0x00000000, 0x02000000, 0x00b00000, 0x041000c0,
	0x00010000, 0x18000000, 0x00000000, 0x02000000,
	0x10b00000, 0x041000c0, 0x00010000, 0x18000000,
	0x00000000, 0x02000000, 0x20b00000, 0x041000c0,
	0x00010000, 0x18000000, 0x00000000, 0x02000000,
	0x30b00000, 0x041000c0, 0x00010000, 0x18000000,
	0x00000000, 0x02000000, 0x00300000, 0x041000c0,
	0x00010000, 0x18000000, 0x00000000, 0x02000000,
	0x10300000, 0x041000c0, 0x00010000, 0x18000000,
	0x00000000, 0x02000000, 0x20300000, 0x041000c0,
	0x00010000, 0x18000000, 0x00000000, 0x02000000,
	0x30300000, 0x041000c0, 0x0a010000, 0x10000000,
	0x00000000, 0x02000000, 0x06010000, 0x1c000000,
	0x00000000, 0x02000000, 0x01000000, 0x00000200,
	0xff230200, 0x06010000, 0x1c000000, 0x00000000,
	0x02000000, 0x02000000, 0x00001000, 0x00000000,
	0x07010000, 0x18000000, 0x00000000, 0x02000000,
	0x00000000, 0x01000000, 0x07010000, 0x18000000,
	0x00000000, 0x02000000, 0x00000000, 0x02000000,
	0x07010000, 0x18000000, 0x00000000, 0x02000000,
	0x00000000, 0x03000000, 0x0d010000, 0x14000000,
	0x00000000, 0x02000000, 0x00000000, 0xff000000,
	0x10000000, 0x00000000, 0x00000080,
};

static inline void __iomem *
qla27xx_isp_reg(struct scsi_qla_host *vha)
{
	return &vha->hw->iobase->isp24;
}

static inline void
qla27xx_insert16(uint16_t value, void *buf, ulong *len)
{
	if (buf) {
		buf += *len;
		*(__le16 *)buf = cpu_to_le16(value);
	}
	*len += sizeof(value);
}

static inline void
qla27xx_insert32(uint32_t value, void *buf, ulong *len)
{
	if (buf) {
		buf += *len;
		*(__le32 *)buf = cpu_to_le32(value);
	}
	*len += sizeof(value);
}

static inline void
qla27xx_insertbuf(void *mem, ulong size, void *buf, ulong *len)
{

	if (buf && mem && size) {
		buf += *len;
		memcpy(buf, mem, size);
	}
	*len += size;
}

static inline void
qla27xx_read8(void *window, void *buf, ulong *len)
{
	uint8_t value = ~0;

	if (buf) {
		value = RD_REG_BYTE((__iomem void *)window);
	}
	qla27xx_insert32(value, buf, len);
}

static inline void
qla27xx_read16(void *window, void *buf, ulong *len)
{
	uint16_t value = ~0;

	if (buf) {
		value = RD_REG_WORD((__iomem void *)window);
	}
	qla27xx_insert32(value, buf, len);
}

static inline void
qla27xx_read32(void *window, void *buf, ulong *len)
{
	uint32_t value = ~0;

	if (buf) {
		value = RD_REG_DWORD((__iomem void *)window);
	}
	qla27xx_insert32(value, buf, len);
}

static inline void (*qla27xx_read_vector(uint width))(void *, void *, ulong *)
{
	return
	    (width == 1) ? qla27xx_read8 :
	    (width == 2) ? qla27xx_read16 :
			   qla27xx_read32;
}

static inline void
qla27xx_read_reg(__iomem struct device_reg_24xx *reg,
	uint offset, void *buf, ulong *len)
{
	void *window = (void *)reg + offset;

	qla27xx_read32(window, buf, len);
}

static inline void
qla27xx_write_reg(__iomem struct device_reg_24xx *reg,
	uint offset, uint32_t data, void *buf)
{
	__iomem void *window = reg + offset;

	if (buf) {
		WRT_REG_DWORD(window, data);
	}
}

static inline void
qla27xx_read_window(__iomem struct device_reg_24xx *reg,
	uint32_t addr, uint offset, uint count, uint width, void *buf,
	ulong *len)
{
	void *window = (void *)reg + offset;
	void (*readn)(void *, void *, ulong *) = qla27xx_read_vector(width);

	qla27xx_write_reg(reg, IOBASE_ADDR, addr, buf);
	while (count--) {
		qla27xx_insert32(addr, buf, len);
		readn(window, buf, len);
		window += width;
		addr++;
	}
}

static inline void
qla27xx_skip_entry(struct qla27xx_fwdt_entry *ent, void *buf)
{
	if (buf)
		ent->hdr.driver_flags |= DRIVER_FLAG_SKIP_ENTRY;
}

static int
qla27xx_fwdt_entry_t0(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	ql_dbg(ql_dbg_misc, vha, 0xd100,
	    "%s: nop [%lx]\n", __func__, *len);
	qla27xx_skip_entry(ent, buf);

	return false;
}

static int
qla27xx_fwdt_entry_t255(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	ql_dbg(ql_dbg_misc, vha, 0xd1ff,
	    "%s: end [%lx]\n", __func__, *len);
	qla27xx_skip_entry(ent, buf);

	/* terminate */
	return true;
}

static int
qla27xx_fwdt_entry_t256(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	struct device_reg_24xx __iomem *reg = qla27xx_isp_reg(vha);

	ql_dbg(ql_dbg_misc, vha, 0xd200,
	    "%s: rdio t1 [%lx]\n", __func__, *len);
	qla27xx_read_window(reg, ent->t256.base_addr, ent->t256.pci_offset,
	    ent->t256.reg_count, ent->t256.reg_width, buf, len);

	return false;
}

static int
qla27xx_fwdt_entry_t257(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	struct device_reg_24xx __iomem *reg = qla27xx_isp_reg(vha);

	ql_dbg(ql_dbg_misc, vha, 0xd201,
	    "%s: wrio t1 [%lx]\n", __func__, *len);
	qla27xx_write_reg(reg, IOBASE_ADDR, ent->t257.base_addr, buf);
	qla27xx_write_reg(reg, ent->t257.pci_offset, ent->t257.write_data, buf);

	return false;
}

static int
qla27xx_fwdt_entry_t258(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	struct device_reg_24xx __iomem *reg = qla27xx_isp_reg(vha);

	ql_dbg(ql_dbg_misc, vha, 0xd202,
	    "%s: rdio t2 [%lx]\n", __func__, *len);
	qla27xx_write_reg(reg, ent->t258.banksel_offset, ent->t258.bank, buf);
	qla27xx_read_window(reg, ent->t258.base_addr, ent->t258.pci_offset,
	    ent->t258.reg_count, ent->t258.reg_width, buf, len);

	return false;
}

static int
qla27xx_fwdt_entry_t259(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	struct device_reg_24xx __iomem *reg = qla27xx_isp_reg(vha);

	ql_dbg(ql_dbg_misc, vha, 0xd203,
	    "%s: wrio t2 [%lx]\n", __func__, *len);
	qla27xx_write_reg(reg, IOBASE_ADDR, ent->t259.base_addr, buf);
	qla27xx_write_reg(reg, ent->t259.banksel_offset, ent->t259.bank, buf);
	qla27xx_write_reg(reg, ent->t259.pci_offset, ent->t259.write_data, buf);

	return false;
}

static int
qla27xx_fwdt_entry_t260(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	struct device_reg_24xx __iomem *reg = qla27xx_isp_reg(vha);

	ql_dbg(ql_dbg_misc, vha, 0xd204,
	    "%s: rdpci [%lx]\n", __func__, *len);
	qla27xx_insert32(ent->t260.pci_offset, buf, len);
	qla27xx_read_reg(reg, ent->t260.pci_offset, buf, len);

	return false;
}

static int
qla27xx_fwdt_entry_t261(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	struct device_reg_24xx __iomem *reg = qla27xx_isp_reg(vha);

	ql_dbg(ql_dbg_misc, vha, 0xd205,
	    "%s: wrpci [%lx]\n", __func__, *len);
	qla27xx_write_reg(reg, ent->t261.pci_offset, ent->t261.write_data, buf);

	return false;
}

static int
qla27xx_fwdt_entry_t262(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	ulong dwords;
	ulong start;
	ulong end;

	ql_dbg(ql_dbg_misc, vha, 0xd206,
	    "%s: rdram(%x) [%lx]\n", __func__, ent->t262.ram_area, *len);
	start = ent->t262.start_addr;
	end = ent->t262.end_addr;

	if (ent->t262.ram_area == T262_RAM_AREA_CRITICAL_RAM) {
		;
	} else if (ent->t262.ram_area == T262_RAM_AREA_EXTERNAL_RAM) {
		end = vha->hw->fw_memory_size;
		if (buf)
			ent->t262.end_addr = end;
	} else if (ent->t262.ram_area == T262_RAM_AREA_SHARED_RAM) {
		start = vha->hw->fw_shared_ram_start;
		end = vha->hw->fw_shared_ram_end;
		if (buf) {
			ent->t262.start_addr = start;
			ent->t262.end_addr = end;
		}
	} else {
		ql_dbg(ql_dbg_misc, vha, 0xd022,
		    "%s: unknown area %x\n", __func__, ent->t262.ram_area);
		qla27xx_skip_entry(ent, buf);
		goto done;
	}

	if (end < start || end == 0) {
		ql_dbg(ql_dbg_misc, vha, 0xd023,
		    "%s: unusable range (start=%x end=%x)\n", __func__,
		    ent->t262.end_addr, ent->t262.start_addr);
		qla27xx_skip_entry(ent, buf);
		goto done;
	}

	dwords = end - start + 1;
	if (buf) {
		buf += *len;
		qla24xx_dump_ram(vha->hw, start, buf, dwords, &buf);
	}
	*len += dwords * sizeof(uint32_t);
done:
	return false;
}

static int
qla27xx_fwdt_entry_t263(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	uint count = 0;
	uint i;
	uint length;

	ql_dbg(ql_dbg_misc, vha, 0xd207,
	    "%s: getq(%x) [%lx]\n", __func__, ent->t263.queue_type, *len);
	if (ent->t263.queue_type == T263_QUEUE_TYPE_REQ) {
		for (i = 0; i < vha->hw->max_req_queues; i++) {
			struct req_que *req = vha->hw->req_q_map[i];
			if (req || !buf) {
				length = req ?
				    req->length : REQUEST_ENTRY_CNT_24XX;
				qla27xx_insert16(i, buf, len);
				qla27xx_insert16(length, buf, len);
				qla27xx_insertbuf(req ? req->ring : NULL,
				    length * sizeof(*req->ring), buf, len);
				count++;
			}
		}
	} else if (ent->t263.queue_type == T263_QUEUE_TYPE_RSP) {
		for (i = 0; i < vha->hw->max_rsp_queues; i++) {
			struct rsp_que *rsp = vha->hw->rsp_q_map[i];
			if (rsp || !buf) {
				length = rsp ?
				    rsp->length : RESPONSE_ENTRY_CNT_MQ;
				qla27xx_insert16(i, buf, len);
				qla27xx_insert16(length, buf, len);
				qla27xx_insertbuf(rsp ? rsp->ring : NULL,
				    length * sizeof(*rsp->ring), buf, len);
				count++;
			}
		}
	} else {
		ql_dbg(ql_dbg_misc, vha, 0xd026,
		    "%s: unknown queue %x\n", __func__, ent->t263.queue_type);
		qla27xx_skip_entry(ent, buf);
	}

	if (buf)
		ent->t263.num_queues = count;

	return false;
}

static int
qla27xx_fwdt_entry_t264(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	ql_dbg(ql_dbg_misc, vha, 0xd208,
	    "%s: getfce [%lx]\n", __func__, *len);
	if (vha->hw->fce) {
		if (buf) {
			ent->t264.fce_trace_size = FCE_SIZE;
			ent->t264.write_pointer = vha->hw->fce_wr;
			ent->t264.base_pointer = vha->hw->fce_dma;
			ent->t264.fce_enable_mb0 = vha->hw->fce_mb[0];
			ent->t264.fce_enable_mb2 = vha->hw->fce_mb[2];
			ent->t264.fce_enable_mb3 = vha->hw->fce_mb[3];
			ent->t264.fce_enable_mb4 = vha->hw->fce_mb[4];
			ent->t264.fce_enable_mb5 = vha->hw->fce_mb[5];
			ent->t264.fce_enable_mb6 = vha->hw->fce_mb[6];
		}
		qla27xx_insertbuf(vha->hw->fce, FCE_SIZE, buf, len);
	} else {
		ql_dbg(ql_dbg_misc, vha, 0xd027,
		    "%s: missing fce\n", __func__);
		qla27xx_skip_entry(ent, buf);
	}

	return false;
}

static int
qla27xx_fwdt_entry_t265(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	struct device_reg_24xx __iomem *reg = qla27xx_isp_reg(vha);

	ql_dbg(ql_dbg_misc, vha, 0xd209,
	    "%s: pause risc [%lx]\n", __func__, *len);
	if (buf)
		qla24xx_pause_risc(reg, vha->hw);

	return false;
}

static int
qla27xx_fwdt_entry_t266(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	ql_dbg(ql_dbg_misc, vha, 0xd20a,
	    "%s: reset risc [%lx]\n", __func__, *len);
	if (buf)
		qla24xx_soft_reset(vha->hw);

	return false;
}

static int
qla27xx_fwdt_entry_t267(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	struct device_reg_24xx __iomem *reg = qla27xx_isp_reg(vha);

	ql_dbg(ql_dbg_misc, vha, 0xd20b,
	    "%s: dis intr [%lx]\n", __func__, *len);
	qla27xx_write_reg(reg, ent->t267.pci_offset, ent->t267.data, buf);

	return false;
}

static int
qla27xx_fwdt_entry_t268(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	ql_dbg(ql_dbg_misc, vha, 0xd20c,
	    "%s: gethb(%x) [%lx]\n", __func__, ent->t268.buf_type, *len);
	if (ent->t268.buf_type == T268_BUF_TYPE_EXTD_TRACE) {
		if (vha->hw->eft) {
			if (buf) {
				ent->t268.buf_size = EFT_SIZE;
				ent->t268.start_addr = vha->hw->eft_dma;
			}
			qla27xx_insertbuf(vha->hw->eft, EFT_SIZE, buf, len);
		} else {
			ql_dbg(ql_dbg_misc, vha, 0xd028,
			    "%s: missing eft\n", __func__);
			qla27xx_skip_entry(ent, buf);
		}
	} else {
		ql_dbg(ql_dbg_misc, vha, 0xd02b,
		    "%s: unknown buffer %x\n", __func__, ent->t268.buf_type);
		qla27xx_skip_entry(ent, buf);
	}

	return false;
}

static int
qla27xx_fwdt_entry_t269(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	ql_dbg(ql_dbg_misc, vha, 0xd20d,
	    "%s: scratch [%lx]\n", __func__, *len);
	qla27xx_insert32(0xaaaaaaaa, buf, len);
	qla27xx_insert32(0xbbbbbbbb, buf, len);
	qla27xx_insert32(0xcccccccc, buf, len);
	qla27xx_insert32(0xdddddddd, buf, len);
	qla27xx_insert32(*len + sizeof(uint32_t), buf, len);
	if (buf)
		ent->t269.scratch_size = 5 * sizeof(uint32_t);

	return false;
}

static int
qla27xx_fwdt_entry_t270(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	struct device_reg_24xx __iomem *reg = qla27xx_isp_reg(vha);
	ulong dwords = ent->t270.count;
	ulong addr = ent->t270.addr;

	ql_dbg(ql_dbg_misc, vha, 0xd20e,
	    "%s: rdremreg [%lx]\n", __func__, *len);
	qla27xx_write_reg(reg, IOBASE_ADDR, 0x40, buf);
	while (dwords--) {
		qla27xx_write_reg(reg, 0xc0, addr|0x80000000, buf);
		qla27xx_insert32(addr, buf, len);
		qla27xx_read_reg(reg, 0xc4, buf, len);
		addr += sizeof(uint32_t);
	}

	return false;
}

static int
qla27xx_fwdt_entry_t271(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	struct device_reg_24xx __iomem *reg = qla27xx_isp_reg(vha);
	ulong addr = ent->t271.addr;
	ulong data = ent->t271.data;

	ql_dbg(ql_dbg_misc, vha, 0xd20f,
	    "%s: wrremreg [%lx]\n", __func__, *len);
	qla27xx_write_reg(reg, IOBASE_ADDR, 0x40, buf);
	qla27xx_write_reg(reg, 0xc4, data, buf);
	qla27xx_write_reg(reg, 0xc0, addr, buf);

	return false;
}

static int
qla27xx_fwdt_entry_t272(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	ulong dwords = ent->t272.count;
	ulong start = ent->t272.addr;

	ql_dbg(ql_dbg_misc, vha, 0xd210,
	    "%s: rdremram [%lx]\n", __func__, *len);
	if (buf) {
		ql_dbg(ql_dbg_misc, vha, 0xd02c,
		    "%s: @%lx -> (%lx dwords)\n", __func__, start, dwords);
		buf += *len;
		qla27xx_dump_mpi_ram(vha->hw, start, buf, dwords, &buf);
	}
	*len += dwords * sizeof(uint32_t);

	return false;
}

static int
qla27xx_fwdt_entry_t273(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	ulong dwords = ent->t273.count;
	ulong addr = ent->t273.addr;
	uint32_t value;

	ql_dbg(ql_dbg_misc, vha, 0xd211,
	    "%s: pcicfg [%lx]\n", __func__, *len);
	while (dwords--) {
		value = ~0;
		if (pci_read_config_dword(vha->hw->pdev, addr, &value))
			ql_dbg(ql_dbg_misc, vha, 0xd02d,
			    "%s: failed pcicfg read at %lx\n", __func__, addr);
		qla27xx_insert32(addr, buf, len);
		qla27xx_insert32(value, buf, len);
		addr += sizeof(uint32_t);
	}

	return false;
}

static int
qla27xx_fwdt_entry_t274(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	uint count = 0;
	uint i;

	ql_dbg(ql_dbg_misc, vha, 0xd212,
	    "%s: getqsh(%x) [%lx]\n", __func__, ent->t274.queue_type, *len);
	if (ent->t274.queue_type == T274_QUEUE_TYPE_REQ_SHAD) {
		for (i = 0; i < vha->hw->max_req_queues; i++) {
			struct req_que *req = vha->hw->req_q_map[i];
			if (req || !buf) {
				qla27xx_insert16(i, buf, len);
				qla27xx_insert16(1, buf, len);
				qla27xx_insert32(req && req->out_ptr ?
				    *req->out_ptr : 0, buf, len);
				count++;
			}
		}
	} else if (ent->t274.queue_type == T274_QUEUE_TYPE_RSP_SHAD) {
		for (i = 0; i < vha->hw->max_rsp_queues; i++) {
			struct rsp_que *rsp = vha->hw->rsp_q_map[i];
			if (rsp || !buf) {
				qla27xx_insert16(i, buf, len);
				qla27xx_insert16(1, buf, len);
				qla27xx_insert32(rsp && rsp->in_ptr ?
				    *rsp->in_ptr : 0, buf, len);
				count++;
			}
		}
	} else {
		ql_dbg(ql_dbg_misc, vha, 0xd02f,
		    "%s: unknown queue %x\n", __func__, ent->t274.queue_type);
		qla27xx_skip_entry(ent, buf);
	}

	if (buf)
		ent->t274.num_queues = count;

	if (!count)
		qla27xx_skip_entry(ent, buf);

	return false;
}

static int
qla27xx_fwdt_entry_t275(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	ulong offset = offsetof(typeof(*ent), t275.buffer);

	ql_dbg(ql_dbg_misc, vha, 0xd213,
	    "%s: buffer(%x) [%lx]\n", __func__, ent->t275.length, *len);
	if (!ent->t275.length) {
		ql_dbg(ql_dbg_misc, vha, 0xd020,
		    "%s: buffer zero length\n", __func__);
		qla27xx_skip_entry(ent, buf);
		goto done;
	}
	if (offset + ent->t275.length > ent->hdr.entry_size) {
		ql_dbg(ql_dbg_misc, vha, 0xd030,
		    "%s: buffer overflow\n", __func__);
		qla27xx_skip_entry(ent, buf);
		goto done;
	}

	qla27xx_insertbuf(ent->t275.buffer, ent->t275.length, buf, len);
done:
	return false;
}

static int
qla27xx_fwdt_entry_other(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_entry *ent, void *buf, ulong *len)
{
	ql_dbg(ql_dbg_misc, vha, 0xd2ff,
	    "%s: type %x [%lx]\n", __func__, ent->hdr.entry_type, *len);
	qla27xx_skip_entry(ent, buf);

	return false;
}

struct qla27xx_fwdt_entry_call {
	uint type;
	int (*call)(
	    struct scsi_qla_host *,
	    struct qla27xx_fwdt_entry *,
	    void *,
	    ulong *);
};

static struct qla27xx_fwdt_entry_call ql27xx_fwdt_entry_call_list[] = {
	{ ENTRY_TYPE_NOP		, qla27xx_fwdt_entry_t0    } ,
	{ ENTRY_TYPE_TMP_END		, qla27xx_fwdt_entry_t255  } ,
	{ ENTRY_TYPE_RD_IOB_T1		, qla27xx_fwdt_entry_t256  } ,
	{ ENTRY_TYPE_WR_IOB_T1		, qla27xx_fwdt_entry_t257  } ,
	{ ENTRY_TYPE_RD_IOB_T2		, qla27xx_fwdt_entry_t258  } ,
	{ ENTRY_TYPE_WR_IOB_T2		, qla27xx_fwdt_entry_t259  } ,
	{ ENTRY_TYPE_RD_PCI		, qla27xx_fwdt_entry_t260  } ,
	{ ENTRY_TYPE_WR_PCI		, qla27xx_fwdt_entry_t261  } ,
	{ ENTRY_TYPE_RD_RAM		, qla27xx_fwdt_entry_t262  } ,
	{ ENTRY_TYPE_GET_QUEUE		, qla27xx_fwdt_entry_t263  } ,
	{ ENTRY_TYPE_GET_FCE		, qla27xx_fwdt_entry_t264  } ,
	{ ENTRY_TYPE_PSE_RISC		, qla27xx_fwdt_entry_t265  } ,
	{ ENTRY_TYPE_RST_RISC		, qla27xx_fwdt_entry_t266  } ,
	{ ENTRY_TYPE_DIS_INTR		, qla27xx_fwdt_entry_t267  } ,
	{ ENTRY_TYPE_GET_HBUF		, qla27xx_fwdt_entry_t268  } ,
	{ ENTRY_TYPE_SCRATCH		, qla27xx_fwdt_entry_t269  } ,
	{ ENTRY_TYPE_RDREMREG		, qla27xx_fwdt_entry_t270  } ,
	{ ENTRY_TYPE_WRREMREG		, qla27xx_fwdt_entry_t271  } ,
	{ ENTRY_TYPE_RDREMRAM		, qla27xx_fwdt_entry_t272  } ,
	{ ENTRY_TYPE_PCICFG		, qla27xx_fwdt_entry_t273  } ,
	{ ENTRY_TYPE_GET_SHADOW		, qla27xx_fwdt_entry_t274  } ,
	{ ENTRY_TYPE_WRITE_BUF		, qla27xx_fwdt_entry_t275  } ,
	{ -1				, qla27xx_fwdt_entry_other }
};

static inline int (*qla27xx_find_entry(uint type))
	(struct scsi_qla_host *, struct qla27xx_fwdt_entry *, void *, ulong *)
{
	struct qla27xx_fwdt_entry_call *list = ql27xx_fwdt_entry_call_list;

	while (list->type < type)
		list++;

	if (list->type == type)
		return list->call;
	return qla27xx_fwdt_entry_other;
}

static inline void *
qla27xx_next_entry(void *p)
{
	struct qla27xx_fwdt_entry *ent = p;

	return p + ent->hdr.entry_size;
}

static void
qla27xx_walk_template(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_template *tmp, void *buf, ulong *len)
{
	struct qla27xx_fwdt_entry *ent = (void *)tmp + tmp->entry_offset;
	ulong count = tmp->entry_count;

	ql_dbg(ql_dbg_misc, vha, 0xd01a,
	    "%s: entry count %lx\n", __func__, count);
	while (count--) {
		if (qla27xx_find_entry(ent->hdr.entry_type)(vha, ent, buf, len))
			break;
		ent = qla27xx_next_entry(ent);
	}

	if (count)
		ql_dbg(ql_dbg_misc, vha, 0xd018,
		    "%s: residual count (%lx)\n", __func__, count);

	if (ent->hdr.entry_type != ENTRY_TYPE_TMP_END)
		ql_dbg(ql_dbg_misc, vha, 0xd019,
		    "%s: missing end (%lx)\n", __func__, count);

	ql_dbg(ql_dbg_misc, vha, 0xd01b,
	    "%s: len=%lx\n", __func__, *len);
}

static void
qla27xx_time_stamp(struct qla27xx_fwdt_template *tmp)
{
	tmp->capture_timestamp = jiffies;
}

static void
qla27xx_driver_info(struct qla27xx_fwdt_template *tmp)
{
	uint8_t v[] = { 0, 0, 0, 0, 0, 0 };
	int rval = 0;

	rval = sscanf(qla2x00_version_str, "%hhu.%hhu.%hhu.%hhu.%hhu.%hhu",
	    v+0, v+1, v+2, v+3, v+4, v+5);

	tmp->driver_info[0] = v[3] << 24 | v[2] << 16 | v[1] << 8 | v[0];
	tmp->driver_info[1] = v[5] << 8 | v[4];
	tmp->driver_info[2] = 0x12345678;
}

static void
qla27xx_firmware_info(struct qla27xx_fwdt_template *tmp,
	struct scsi_qla_host *vha)
{
	tmp->firmware_version[0] = vha->hw->fw_major_version;
	tmp->firmware_version[1] = vha->hw->fw_minor_version;
	tmp->firmware_version[2] = vha->hw->fw_subminor_version;
	tmp->firmware_version[3] =
	    vha->hw->fw_attributes_h << 16 | vha->hw->fw_attributes;
	tmp->firmware_version[4] =
	    vha->hw->fw_attributes_ext[1] << 16 | vha->hw->fw_attributes_ext[0];
}

static void
ql27xx_edit_template(struct scsi_qla_host *vha,
	struct qla27xx_fwdt_template *tmp)
{
	qla27xx_time_stamp(tmp);
	qla27xx_driver_info(tmp);
	qla27xx_firmware_info(tmp, vha);
}

static inline uint32_t
qla27xx_template_checksum(void *p, ulong size)
{
	uint32_t *buf = p;
	uint64_t sum = 0;

	size /= sizeof(*buf);

	while (size--)
		sum += *buf++;

	sum = (sum & 0xffffffff) + (sum >> 32);

	return ~sum;
}

static inline int
qla27xx_verify_template_checksum(struct qla27xx_fwdt_template *tmp)
{
	return qla27xx_template_checksum(tmp, tmp->template_size) == 0;
}

static inline int
qla27xx_verify_template_header(struct qla27xx_fwdt_template *tmp)
{
	return tmp->template_type == TEMPLATE_TYPE_FWDUMP;
}

static void
qla27xx_execute_fwdt_template(struct scsi_qla_host *vha)
{
	struct qla27xx_fwdt_template *tmp = vha->hw->fw_dump_template;
	ulong len;

	if (qla27xx_fwdt_template_valid(tmp)) {
		len = tmp->template_size;
		tmp = memcpy(vha->hw->fw_dump, tmp, len);
		ql27xx_edit_template(vha, tmp);
		qla27xx_walk_template(vha, tmp, tmp, &len);
		vha->hw->fw_dump_len = len;
		vha->hw->fw_dumped = 1;
	}
}

ulong
qla27xx_fwdt_calculate_dump_size(struct scsi_qla_host *vha)
{
	struct qla27xx_fwdt_template *tmp = vha->hw->fw_dump_template;
	ulong len = 0;

	if (qla27xx_fwdt_template_valid(tmp)) {
		len = tmp->template_size;
		qla27xx_walk_template(vha, tmp, NULL, &len);
	}

	return len;
}

ulong
qla27xx_fwdt_template_size(void *p)
{
	struct qla27xx_fwdt_template *tmp = p;

	return tmp->template_size;
}

ulong
qla27xx_fwdt_template_default_size(void)
{
	return sizeof(ql27xx_fwdt_default_template);
}

const void *
qla27xx_fwdt_template_default(void)
{
	return ql27xx_fwdt_default_template;
}

int
qla27xx_fwdt_template_valid(void *p)
{
	struct qla27xx_fwdt_template *tmp = p;

	if (!qla27xx_verify_template_header(tmp)) {
		ql_log(ql_log_warn, NULL, 0xd01c,
		    "%s: template type %x\n", __func__, tmp->template_type);
		return false;
	}

	if (!qla27xx_verify_template_checksum(tmp)) {
		ql_log(ql_log_warn, NULL, 0xd01d,
		    "%s: failed template checksum\n", __func__);
		return false;
	}

	return true;
}

void
qla27xx_fwdump(scsi_qla_host_t *vha, int hardware_locked)
{
	ulong flags = 0;

	if (!hardware_locked)
		spin_lock_irqsave(&vha->hw->hardware_lock, flags);

	if (!vha->hw->fw_dump)
		ql_log(ql_log_warn, vha, 0xd01e, "fwdump buffer missing.\n");
	else if (!vha->hw->fw_dump_template)
		ql_log(ql_log_warn, vha, 0xd01f, "fwdump template missing.\n");
	else
		qla27xx_execute_fwdt_template(vha);

	if (!hardware_locked)
		spin_unlock_irqrestore(&vha->hw->hardware_lock, flags);
}
