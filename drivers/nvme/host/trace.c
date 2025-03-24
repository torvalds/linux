// SPDX-License-Identifier: GPL-2.0
/*
 * NVM Express device driver tracepoints
 * Copyright (c) 2018 Johannes Thumshirn, SUSE Linux GmbH
 */

#include <linux/unaligned.h>
#include "trace.h"

static const char *nvme_trace_delete_sq(struct trace_seq *p, u8 *cdw10)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u16 sqid = get_unaligned_le16(cdw10);

	trace_seq_printf(p, "sqid=%u", sqid);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvme_trace_create_sq(struct trace_seq *p, u8 *cdw10)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u16 sqid = get_unaligned_le16(cdw10);
	u16 qsize = get_unaligned_le16(cdw10 + 2);
	u16 sq_flags = get_unaligned_le16(cdw10 + 4);
	u16 cqid = get_unaligned_le16(cdw10 + 6);


	trace_seq_printf(p, "sqid=%u, qsize=%u, sq_flags=0x%x, cqid=%u",
			 sqid, qsize, sq_flags, cqid);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvme_trace_delete_cq(struct trace_seq *p, u8 *cdw10)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u16 cqid = get_unaligned_le16(cdw10);

	trace_seq_printf(p, "cqid=%u", cqid);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvme_trace_create_cq(struct trace_seq *p, u8 *cdw10)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u16 cqid = get_unaligned_le16(cdw10);
	u16 qsize = get_unaligned_le16(cdw10 + 2);
	u16 cq_flags = get_unaligned_le16(cdw10 + 4);
	u16 irq_vector = get_unaligned_le16(cdw10 + 6);

	trace_seq_printf(p, "cqid=%u, qsize=%u, cq_flags=0x%x, irq_vector=%u",
			 cqid, qsize, cq_flags, irq_vector);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvme_trace_admin_identify(struct trace_seq *p, u8 *cdw10)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u8 cns = cdw10[0];
	u16 ctrlid = get_unaligned_le16(cdw10 + 2);

	trace_seq_printf(p, "cns=%u, ctrlid=%u", cns, ctrlid);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvme_trace_admin_set_features(struct trace_seq *p,
						 u8 *cdw10)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u8 fid = cdw10[0];
	u8 sv = cdw10[3] & 0x8;
	u32 cdw11 = get_unaligned_le32(cdw10 + 4);

	trace_seq_printf(p, "fid=0x%x, sv=0x%x, cdw11=0x%x", fid, sv, cdw11);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvme_trace_admin_get_features(struct trace_seq *p,
						 u8 *cdw10)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u8 fid = cdw10[0];
	u8 sel = cdw10[1] & 0x7;
	u32 cdw11 = get_unaligned_le32(cdw10 + 4);

	trace_seq_printf(p, "fid=0x%x, sel=0x%x, cdw11=0x%x", fid, sel, cdw11);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvme_trace_get_lba_status(struct trace_seq *p,
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

static const char *nvme_trace_admin_format_nvm(struct trace_seq *p, u8 *cdw10)
{
	const char *ret = trace_seq_buffer_ptr(p);
	/*
	 * lbafu(bit 13:12) is already in the upper 4 bits, lbafl: bit 03:00.
	 */
	u8 lbaf = (cdw10[1] & 0x30) | (cdw10[0] & 0xF);
	u8 mset = (cdw10[0] >> 4) & 0x1;
	u8 pi = (cdw10[0] >> 5) & 0x7;
	u8 pil = cdw10[1] & 0x1;
	u8 ses = (cdw10[1] >> 1) & 0x7;

	trace_seq_printf(p, "lbaf=%u, mset=%u, pi=%u, pil=%u, ses=%u",
			lbaf, mset, pi, pil, ses);

	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvme_trace_read_write(struct trace_seq *p, u8 *cdw10)
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

static const char *nvme_trace_dsm(struct trace_seq *p, u8 *cdw10)
{
	const char *ret = trace_seq_buffer_ptr(p);

	trace_seq_printf(p, "nr=%u, attributes=%u",
			 get_unaligned_le32(cdw10),
			 get_unaligned_le32(cdw10 + 4));
	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvme_trace_zone_mgmt_send(struct trace_seq *p, u8 *cdw10)
{
	static const char * const zsa_strs[] = {
		[0x01] = "close zone",
		[0x02] = "finish zone",
		[0x03] = "open zone",
		[0x04] = "reset zone",
		[0x05] = "offline zone",
		[0x10] = "set zone descriptor extension"
	};
	const char *ret = trace_seq_buffer_ptr(p);
	u64 slba = get_unaligned_le64(cdw10);
	const char *zsa_str;
	u8 zsa = cdw10[12];
	u8 all = cdw10[13];

	if (zsa < ARRAY_SIZE(zsa_strs) && zsa_strs[zsa])
		zsa_str = zsa_strs[zsa];
	else
		zsa_str = "reserved";

	trace_seq_printf(p, "slba=%llu, zsa=%u:%s, all=%u",
		slba, zsa, zsa_str, all);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvme_trace_zone_mgmt_recv(struct trace_seq *p, u8 *cdw10)
{
	static const char * const zrasf_strs[] = {
		[0x00] = "list all zones",
		[0x01] = "list the zones in the ZSE: Empty state",
		[0x02] = "list the zones in the ZSIO: Implicitly Opened state",
		[0x03] = "list the zones in the ZSEO: Explicitly Opened state",
		[0x04] = "list the zones in the ZSC: Closed state",
		[0x05] = "list the zones in the ZSF: Full state",
		[0x06] = "list the zones in the ZSRO: Read Only state",
		[0x07] = "list the zones in the ZSO: Offline state",
		[0x09] = "list the zones that have the zone attribute"
	};
	const char *ret = trace_seq_buffer_ptr(p);
	u64 slba = get_unaligned_le64(cdw10);
	u32 numd = get_unaligned_le32(cdw10 + 8);
	u8 zra = cdw10[12];
	u8 zrasf = cdw10[13];
	const char *zrasf_str;
	u8 pr = cdw10[14];

	if (zrasf < ARRAY_SIZE(zrasf_strs) && zrasf_strs[zrasf])
		zrasf_str = zrasf_strs[zrasf];
	else
		zrasf_str = "reserved";

	trace_seq_printf(p, "slba=%llu, numd=%u, zra=%u, zrasf=%u:%s, pr=%u",
		slba, numd, zra, zrasf, zrasf_str, pr);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvme_trace_resv_reg(struct trace_seq *p, u8 *cdw10)
{
	static const char * const rrega_strs[] = {
		[0x00] = "register",
		[0x01] = "unregister",
		[0x02] = "replace",
	};
	const char *ret = trace_seq_buffer_ptr(p);
	u8 rrega = cdw10[0] & 0x7;
	u8 iekey = (cdw10[0] >> 3) & 0x1;
	u8 ptpl = (cdw10[3] >> 6) & 0x3;
	const char *rrega_str;

	if (rrega < ARRAY_SIZE(rrega_strs) && rrega_strs[rrega])
		rrega_str = rrega_strs[rrega];
	else
		rrega_str = "reserved";

	trace_seq_printf(p, "rrega=%u:%s, iekey=%u, ptpl=%u",
			 rrega, rrega_str, iekey, ptpl);
	trace_seq_putc(p, 0);

	return ret;
}

static const char * const rtype_strs[] = {
	[0x00] = "reserved",
	[0x01] = "write exclusive",
	[0x02] = "exclusive access",
	[0x03] = "write exclusive registrants only",
	[0x04] = "exclusive access registrants only",
	[0x05] = "write exclusive all registrants",
	[0x06] = "exclusive access all registrants",
};

static const char *nvme_trace_resv_acq(struct trace_seq *p, u8 *cdw10)
{
	static const char * const racqa_strs[] = {
		[0x00] = "acquire",
		[0x01] = "preempt",
		[0x02] = "preempt and abort",
	};
	const char *ret = trace_seq_buffer_ptr(p);
	u8 racqa = cdw10[0] & 0x7;
	u8 iekey = (cdw10[0] >> 3) & 0x1;
	u8 rtype = cdw10[1];
	const char *racqa_str = "reserved";
	const char *rtype_str = "reserved";

	if (racqa < ARRAY_SIZE(racqa_strs) && racqa_strs[racqa])
		racqa_str = racqa_strs[racqa];

	if (rtype < ARRAY_SIZE(rtype_strs) && rtype_strs[rtype])
		rtype_str = rtype_strs[rtype];

	trace_seq_printf(p, "racqa=%u:%s, iekey=%u, rtype=%u:%s",
			 racqa, racqa_str, iekey, rtype, rtype_str);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvme_trace_resv_rel(struct trace_seq *p, u8 *cdw10)
{
	static const char * const rrela_strs[] = {
		[0x00] = "release",
		[0x01] = "clear",
	};
	const char *ret = trace_seq_buffer_ptr(p);
	u8 rrela = cdw10[0] & 0x7;
	u8 iekey = (cdw10[0] >> 3) & 0x1;
	u8 rtype = cdw10[1];
	const char *rrela_str = "reserved";
	const char *rtype_str = "reserved";

	if (rrela < ARRAY_SIZE(rrela_strs) && rrela_strs[rrela])
		rrela_str = rrela_strs[rrela];

	if (rtype < ARRAY_SIZE(rtype_strs) && rtype_strs[rtype])
		rtype_str = rtype_strs[rtype];

	trace_seq_printf(p, "rrela=%u:%s, iekey=%u, rtype=%u:%s",
			 rrela, rrela_str, iekey, rtype, rtype_str);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvme_trace_resv_report(struct trace_seq *p, u8 *cdw10)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u32 numd = get_unaligned_le32(cdw10);
	u8 eds = cdw10[4] & 0x1;

	trace_seq_printf(p, "numd=%u, eds=%u", numd, eds);
	trace_seq_putc(p, 0);

	return ret;
}

static const char *nvme_trace_common(struct trace_seq *p, u8 *cdw10)
{
	const char *ret = trace_seq_buffer_ptr(p);

	trace_seq_printf(p, "cdw10=%*ph", 24, cdw10);
	trace_seq_putc(p, 0);

	return ret;
}

const char *nvme_trace_parse_admin_cmd(struct trace_seq *p,
				       u8 opcode, u8 *cdw10)
{
	switch (opcode) {
	case nvme_admin_delete_sq:
		return nvme_trace_delete_sq(p, cdw10);
	case nvme_admin_create_sq:
		return nvme_trace_create_sq(p, cdw10);
	case nvme_admin_delete_cq:
		return nvme_trace_delete_cq(p, cdw10);
	case nvme_admin_create_cq:
		return nvme_trace_create_cq(p, cdw10);
	case nvme_admin_identify:
		return nvme_trace_admin_identify(p, cdw10);
	case nvme_admin_set_features:
		return nvme_trace_admin_set_features(p, cdw10);
	case nvme_admin_get_features:
		return nvme_trace_admin_get_features(p, cdw10);
	case nvme_admin_get_lba_status:
		return nvme_trace_get_lba_status(p, cdw10);
	case nvme_admin_format_nvm:
		return nvme_trace_admin_format_nvm(p, cdw10);
	default:
		return nvme_trace_common(p, cdw10);
	}
}

const char *nvme_trace_parse_nvm_cmd(struct trace_seq *p,
				     u8 opcode, u8 *cdw10)
{
	switch (opcode) {
	case nvme_cmd_read:
	case nvme_cmd_write:
	case nvme_cmd_write_zeroes:
	case nvme_cmd_zone_append:
		return nvme_trace_read_write(p, cdw10);
	case nvme_cmd_dsm:
		return nvme_trace_dsm(p, cdw10);
	case nvme_cmd_zone_mgmt_send:
		return nvme_trace_zone_mgmt_send(p, cdw10);
	case nvme_cmd_zone_mgmt_recv:
		return nvme_trace_zone_mgmt_recv(p, cdw10);
	case nvme_cmd_resv_register:
		return nvme_trace_resv_reg(p, cdw10);
	case nvme_cmd_resv_acquire:
		return nvme_trace_resv_acq(p, cdw10);
	case nvme_cmd_resv_release:
		return nvme_trace_resv_rel(p, cdw10);
	case nvme_cmd_resv_report:
		return nvme_trace_resv_report(p, cdw10);
	default:
		return nvme_trace_common(p, cdw10);
	}
}

static const char *nvme_trace_fabrics_property_set(struct trace_seq *p, u8 *spc)
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

static const char *nvme_trace_fabrics_connect(struct trace_seq *p, u8 *spc)
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

static const char *nvme_trace_fabrics_property_get(struct trace_seq *p, u8 *spc)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u8 attrib = spc[0];
	u32 ofst = get_unaligned_le32(spc + 4);

	trace_seq_printf(p, "attrib=%u, ofst=0x%x", attrib, ofst);
	trace_seq_putc(p, 0);
	return ret;
}

static const char *nvme_trace_fabrics_auth_send(struct trace_seq *p, u8 *spc)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u8 spsp0 = spc[1];
	u8 spsp1 = spc[2];
	u8 secp = spc[3];
	u32 tl = get_unaligned_le32(spc + 4);

	trace_seq_printf(p, "spsp0=%02x, spsp1=%02x, secp=%02x, tl=%u",
			 spsp0, spsp1, secp, tl);
	trace_seq_putc(p, 0);
	return ret;
}

static const char *nvme_trace_fabrics_auth_receive(struct trace_seq *p, u8 *spc)
{
	const char *ret = trace_seq_buffer_ptr(p);
	u8 spsp0 = spc[1];
	u8 spsp1 = spc[2];
	u8 secp = spc[3];
	u32 al = get_unaligned_le32(spc + 4);

	trace_seq_printf(p, "spsp0=%02x, spsp1=%02x, secp=%02x, al=%u",
			 spsp0, spsp1, secp, al);
	trace_seq_putc(p, 0);
	return ret;
}

static const char *nvme_trace_fabrics_common(struct trace_seq *p, u8 *spc)
{
	const char *ret = trace_seq_buffer_ptr(p);

	trace_seq_printf(p, "specific=%*ph", 24, spc);
	trace_seq_putc(p, 0);
	return ret;
}

const char *nvme_trace_parse_fabrics_cmd(struct trace_seq *p,
		u8 fctype, u8 *spc)
{
	switch (fctype) {
	case nvme_fabrics_type_property_set:
		return nvme_trace_fabrics_property_set(p, spc);
	case nvme_fabrics_type_connect:
		return nvme_trace_fabrics_connect(p, spc);
	case nvme_fabrics_type_property_get:
		return nvme_trace_fabrics_property_get(p, spc);
	case nvme_fabrics_type_auth_send:
		return nvme_trace_fabrics_auth_send(p, spc);
	case nvme_fabrics_type_auth_receive:
		return nvme_trace_fabrics_auth_receive(p, spc);
	default:
		return nvme_trace_fabrics_common(p, spc);
	}
}

const char *nvme_trace_disk_name(struct trace_seq *p, char *name)
{
	const char *ret = trace_seq_buffer_ptr(p);

	if (*name)
		trace_seq_printf(p, "disk=%s, ", name);
	trace_seq_putc(p, 0);

	return ret;
}

EXPORT_TRACEPOINT_SYMBOL_GPL(nvme_sq);
