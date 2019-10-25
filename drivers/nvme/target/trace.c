// SPDX-License-Identifier: GPL-2.0
/*
 * NVM Express target device driver tracepoints
 * Copyright (c) 2018 Johannes Thumshirn, SUSE Linux GmbH
 */

#include <asm/unaligned.h>
#include "trace.h"

static const char *nvmet_trace_admin_identify(struct trace_seq *p, u8 *cdw10)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u8 cns = cdw10[0];
	u16 ctrlid = get_unaligned_le16(cdw10 + 2);

	trace_seq_printf(p, "cns=%u, ctrlid=%u", cns, ctrlid);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvmet_trace_admin_get_features(struct trace_seq *p,
						 u8 *cdw10)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u8 fid = cdw10[0];
	u8 sel = cdw10[1] & 0x7;
	u32 cdw11 = get_unaligned_le32(cdw10 + 4);

	trace_seq_printf(p, "fid=0x%x sel=0x%x cdw11=0x%x", fid, sel, cdw11);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvmet_trace_get_lba_status(struct trace_seq *p,
					     u8 *cdw10)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u64 slba = get_unaligned_le64(cdw10);
	u32 mndw = get_unaligned_le32(cdw10 + 8);
	u16 rl = get_unaligned_le16(cdw10 + 12);
	u8 atype = cdw10[15];

	trace_seq_printf(p, "slba=0x%llx, mndw=0x%x, rl=0x%x, atype=%u",
			slba, mndw, rl, atype);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvmet_trace_read_write(struct trace_seq *p, u8 *cdw10)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u64 slba = get_unaligned_le64(cdw10);
	u16 length = get_unaligned_le16(cdw10 + 8);
	u16 control = get_unaligned_le16(cdw10 + 10);
	u32 dsmgmt = get_unaligned_le32(cdw10 + 12);
	u32 reftag = get_unaligned_le32(cdw10 +  16);

	trace_seq_printf(p,
			 "slba=%llu, len=%u, ctrl=0x%x, dsmgmt=%u, reftag=%u",
			 slba, length, control, dsmgmt, reftag);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvmet_trace_dsm(struct trace_seq *p, u8 *cdw10)
{
	const char *ret = trace_seq_buffer_ptr(p);

	trace_seq_printf(p, "nr=%u, attributes=%u",
			 get_unaligned_le32(cdw10),
			 get_unaligned_le32(cdw10 + 4));
	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvmet_trace_common(struct trace_seq *p, u8 *cdw10)
{
	const char *ret = trace_seq_buffer_ptr(p);

	trace_seq_printf(p, "cdw10=%*ph", 24, cdw10);
	trace_seq_putc(p, 0);

	return ret;
}

const char *nvmet_trace_parse_admin_cmd(struct trace_seq *p,
		u8 opcode, u8 *cdw10)
{
	switch (opcode) {
	case nvme_admin_identify:
		return nvmet_trace_admin_identify(p, cdw10);
	case nvme_admin_get_features:
		return nvmet_trace_admin_get_features(p, cdw10);
	case nvme_admin_get_lba_status:
		return nvmet_trace_get_lba_status(p, cdw10);
	default:
		return nvmet_trace_common(p, cdw10);
	}
}

const char *nvmet_trace_parse_nvm_cmd(struct trace_seq *p,
		u8 opcode, u8 *cdw10)
{
	switch (opcode) {
	case nvme_cmd_read:
	case nvme_cmd_write:
	case nvme_cmd_write_zeroes:
		return nvmet_trace_read_write(p, cdw10);
	case nvme_cmd_dsm:
		return nvmet_trace_dsm(p, cdw10);
	default:
		return nvmet_trace_common(p, cdw10);
	}
}

static const char *nvmet_trace_fabrics_property_set(struct trace_seq *p,
		u8 *spc)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u8 attrib = spc[0];
	u32 ofst = get_unaligned_le32(spc + 4);
	u64 value = get_unaligned_le64(spc + 8);

	trace_seq_printf(p, "attrib=%u, ofst=0x%x, value=0x%llx",
			 attrib, ofst, value);
	trace_seq_putc(p, 0);
	return ret;
}

static const char *nvmet_trace_fabrics_connect(struct trace_seq *p,
		u8 *spc)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u16 recfmt = get_unaligned_le16(spc);
	u16 qid = get_unaligned_le16(spc + 2);
	u16 sqsize = get_unaligned_le16(spc + 4);
	u8 cattr = spc[6];
	u32 kato = get_unaligned_le32(spc + 8);

	trace_seq_printf(p, "recfmt=%u, qid=%u, sqsize=%u, cattr=%u, kato=%u",
			 recfmt, qid, sqsize, cattr, kato);
	trace_seq_putc(p, 0);
	return ret;
}

static const char *nvmet_trace_fabrics_property_get(struct trace_seq *p,
		u8 *spc)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u8 attrib = spc[0];
	u32 ofst = get_unaligned_le32(spc + 4);

	trace_seq_printf(p, "attrib=%u, ofst=0x%x", attrib, ofst);
	trace_seq_putc(p, 0);
	return ret;
}

static const char *nvmet_trace_fabrics_common(struct trace_seq *p, u8 *spc)
{
	const char *ret = trace_seq_buffer_ptr(p);

	trace_seq_printf(p, "specific=%*ph", 24, spc);
	trace_seq_putc(p, 0);
	return ret;
}

const char *nvmet_trace_parse_fabrics_cmd(struct trace_seq *p,
		u8 fctype, u8 *spc)
{
	switch (fctype) {
	case nvme_fabrics_type_property_set:
		return nvmet_trace_fabrics_property_set(p, spc);
	case nvme_fabrics_type_connect:
		return nvmet_trace_fabrics_connect(p, spc);
	case nvme_fabrics_type_property_get:
		return nvmet_trace_fabrics_property_get(p, spc);
	default:
		return nvmet_trace_fabrics_common(p, spc);
	}
}

const char *nvmet_trace_disk_name(struct trace_seq *p, char *name)
{
	const char *ret = trace_seq_buffer_ptr(p);

	if (*name)
		trace_seq_printf(p, "disk=%s, ", name);
	trace_seq_putc(p, 0);

	return ret;
}

const char *nvmet_trace_ctrl_name(struct trace_seq *p, struct nvmet_ctrl *ctrl)
{
	const char *ret = trace_seq_buffer_ptr(p);

	/*
	 * XXX: We don't know the controller instance before executing the
	 * connect command itself because the connect command for the admin
	 * queue will not provide the cntlid which will be allocated in this
	 * command.  In case of io queues, the controller instance will be
	 * mapped by the extra data of the connect command.
	 * If we can know the extra data of the connect command in this stage,
	 * we can update this print statement later.
	 */
	if (ctrl)
		trace_seq_printf(p, "%d", ctrl->cntlid);
	else
		trace_seq_printf(p, "_");
	trace_seq_putc(p, 0);

	return ret;
}

