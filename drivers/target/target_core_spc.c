/*
 * SCSI Primary Commands (SPC) parsing and emulation.
 *
 * Copyright (c) 2002, 2003, 2004, 2005 PyX Technologies, Inc.
 * Copyright (c) 2005, 2006, 2007 SBE, Inc.
 * Copyright (c) 2007-2010 Rising Tide Systems
 * Copyright (c) 2008-2010 Linux-iSCSI.org
 *
 * Nicholas A. Bellinger <nab@kernel.org>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <asm/unaligned.h>

#include <scsi/scsi.h>
#include <scsi/scsi_tcq.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>
#include <target/target_core_fabric.h>

#include "target_core_internal.h"
#include "target_core_alua.h"
#include "target_core_pr.h"
#include "target_core_ua.h"


static void spc_fill_alua_data(struct se_port *port, unsigned char *buf)
{
	struct t10_alua_tg_pt_gp *tg_pt_gp;
	struct t10_alua_tg_pt_gp_member *tg_pt_gp_mem;

	/*
	 * Set SCCS for MAINTENANCE_IN + REPORT_TARGET_PORT_GROUPS.
	 */
	buf[5]	= 0x80;

	/*
	 * Set TPGS field for explict and/or implict ALUA access type
	 * and opteration.
	 *
	 * See spc4r17 section 6.4.2 Table 135
	 */
	if (!port)
		return;
	tg_pt_gp_mem = port->sep_alua_tg_pt_gp_mem;
	if (!tg_pt_gp_mem)
		return;

	spin_lock(&tg_pt_gp_mem->tg_pt_gp_mem_lock);
	tg_pt_gp = tg_pt_gp_mem->tg_pt_gp;
	if (tg_pt_gp)
		buf[5] |= tg_pt_gp->tg_pt_gp_alua_access_type;
	spin_unlock(&tg_pt_gp_mem->tg_pt_gp_mem_lock);
}

static int spc_emulate_inquiry_std(struct se_cmd *cmd, char *buf)
{
	struct se_lun *lun = cmd->se_lun;
	struct se_device *dev = cmd->se_dev;

	/* Set RMB (removable media) for tape devices */
	if (dev->transport->get_device_type(dev) == TYPE_TAPE)
		buf[1] = 0x80;

	buf[2] = dev->transport->get_device_rev(dev);

	/*
	 * NORMACA and HISUP = 0, RESPONSE DATA FORMAT = 2
	 *
	 * SPC4 says:
	 *   A RESPONSE DATA FORMAT field set to 2h indicates that the
	 *   standard INQUIRY data is in the format defined in this
	 *   standard. Response data format values less than 2h are
	 *   obsolete. Response data format values greater than 2h are
	 *   reserved.
	 */
	buf[3] = 2;

	/*
	 * Enable SCCS and TPGS fields for Emulated ALUA
	 */
	if (dev->se_sub_dev->t10_alua.alua_type == SPC3_ALUA_EMULATED)
		spc_fill_alua_data(lun->lun_sep, buf);

	buf[7] = 0x2; /* CmdQue=1 */

	snprintf(&buf[8], 8, "LIO-ORG");
	snprintf(&buf[16], 16, "%s", dev->se_sub_dev->t10_wwn.model);
	snprintf(&buf[32], 4, "%s", dev->se_sub_dev->t10_wwn.revision);
	buf[4] = 31; /* Set additional length to 31 */

	return 0;
}

/* unit serial number */
static int spc_emulate_evpd_80(struct se_cmd *cmd, unsigned char *buf)
{
	struct se_device *dev = cmd->se_dev;
	u16 len = 0;

	if (dev->se_sub_dev->su_dev_flags &
			SDF_EMULATED_VPD_UNIT_SERIAL) {
		u32 unit_serial_len;

		unit_serial_len = strlen(dev->se_sub_dev->t10_wwn.unit_serial);
		unit_serial_len++; /* For NULL Terminator */

		len += sprintf(&buf[4], "%s",
			dev->se_sub_dev->t10_wwn.unit_serial);
		len++; /* Extra Byte for NULL Terminator */
		buf[3] = len;
	}
	return 0;
}

static void spc_parse_naa_6h_vendor_specific(struct se_device *dev,
		unsigned char *buf)
{
	unsigned char *p = &dev->se_sub_dev->t10_wwn.unit_serial[0];
	int cnt;
	bool next = true;

	/*
	 * Generate up to 36 bits of VENDOR SPECIFIC IDENTIFIER starting on
	 * byte 3 bit 3-0 for NAA IEEE Registered Extended DESIGNATOR field
	 * format, followed by 64 bits of VENDOR SPECIFIC IDENTIFIER EXTENSION
	 * to complete the payload.  These are based from VPD=0x80 PRODUCT SERIAL
	 * NUMBER set via vpd_unit_serial in target_core_configfs.c to ensure
	 * per device uniqeness.
	 */
	for (cnt = 0; *p && cnt < 13; p++) {
		int val = hex_to_bin(*p);

		if (val < 0)
			continue;

		if (next) {
			next = false;
			buf[cnt++] |= val;
		} else {
			next = true;
			buf[cnt] = val << 4;
		}
	}
}

/*
 * Device identification VPD, for a complete list of
 * DESIGNATOR TYPEs see spc4r17 Table 459.
 */
static int spc_emulate_evpd_83(struct se_cmd *cmd, unsigned char *buf)
{
	struct se_device *dev = cmd->se_dev;
	struct se_lun *lun = cmd->se_lun;
	struct se_port *port = NULL;
	struct se_portal_group *tpg = NULL;
	struct t10_alua_lu_gp_member *lu_gp_mem;
	struct t10_alua_tg_pt_gp *tg_pt_gp;
	struct t10_alua_tg_pt_gp_member *tg_pt_gp_mem;
	unsigned char *prod = &dev->se_sub_dev->t10_wwn.model[0];
	u32 prod_len;
	u32 unit_serial_len, off = 0;
	u16 len = 0, id_len;

	off = 4;

	/*
	 * NAA IEEE Registered Extended Assigned designator format, see
	 * spc4r17 section 7.7.3.6.5
	 *
	 * We depend upon a target_core_mod/ConfigFS provided
	 * /sys/kernel/config/target/core/$HBA/$DEV/wwn/vpd_unit_serial
	 * value in order to return the NAA id.
	 */
	if (!(dev->se_sub_dev->su_dev_flags & SDF_EMULATED_VPD_UNIT_SERIAL))
		goto check_t10_vend_desc;

	/* CODE SET == Binary */
	buf[off++] = 0x1;

	/* Set ASSOCIATION == addressed logical unit: 0)b */
	buf[off] = 0x00;

	/* Identifier/Designator type == NAA identifier */
	buf[off++] |= 0x3;
	off++;

	/* Identifier/Designator length */
	buf[off++] = 0x10;

	/*
	 * Start NAA IEEE Registered Extended Identifier/Designator
	 */
	buf[off++] = (0x6 << 4);

	/*
	 * Use OpenFabrics IEEE Company ID: 00 14 05
	 */
	buf[off++] = 0x01;
	buf[off++] = 0x40;
	buf[off] = (0x5 << 4);

	/*
	 * Return ConfigFS Unit Serial Number information for
	 * VENDOR_SPECIFIC_IDENTIFIER and
	 * VENDOR_SPECIFIC_IDENTIFIER_EXTENTION
	 */
	spc_parse_naa_6h_vendor_specific(dev, &buf[off]);

	len = 20;
	off = (len + 4);

check_t10_vend_desc:
	/*
	 * T10 Vendor Identifier Page, see spc4r17 section 7.7.3.4
	 */
	id_len = 8; /* For Vendor field */
	prod_len = 4; /* For VPD Header */
	prod_len += 8; /* For Vendor field */
	prod_len += strlen(prod);
	prod_len++; /* For : */

	if (dev->se_sub_dev->su_dev_flags &
			SDF_EMULATED_VPD_UNIT_SERIAL) {
		unit_serial_len =
			strlen(&dev->se_sub_dev->t10_wwn.unit_serial[0]);
		unit_serial_len++; /* For NULL Terminator */

		id_len += sprintf(&buf[off+12], "%s:%s", prod,
				&dev->se_sub_dev->t10_wwn.unit_serial[0]);
	}
	buf[off] = 0x2; /* ASCII */
	buf[off+1] = 0x1; /* T10 Vendor ID */
	buf[off+2] = 0x0;
	memcpy(&buf[off+4], "LIO-ORG", 8);
	/* Extra Byte for NULL Terminator */
	id_len++;
	/* Identifier Length */
	buf[off+3] = id_len;
	/* Header size for Designation descriptor */
	len += (id_len + 4);
	off += (id_len + 4);
	/*
	 * struct se_port is only set for INQUIRY VPD=1 through $FABRIC_MOD
	 */
	port = lun->lun_sep;
	if (port) {
		struct t10_alua_lu_gp *lu_gp;
		u32 padding, scsi_name_len;
		u16 lu_gp_id = 0;
		u16 tg_pt_gp_id = 0;
		u16 tpgt;

		tpg = port->sep_tpg;
		/*
		 * Relative target port identifer, see spc4r17
		 * section 7.7.3.7
		 *
		 * Get the PROTOCOL IDENTIFIER as defined by spc4r17
		 * section 7.5.1 Table 362
		 */
		buf[off] =
			(tpg->se_tpg_tfo->get_fabric_proto_ident(tpg) << 4);
		buf[off++] |= 0x1; /* CODE SET == Binary */
		buf[off] = 0x80; /* Set PIV=1 */
		/* Set ASSOCIATION == target port: 01b */
		buf[off] |= 0x10;
		/* DESIGNATOR TYPE == Relative target port identifer */
		buf[off++] |= 0x4;
		off++; /* Skip over Reserved */
		buf[off++] = 4; /* DESIGNATOR LENGTH */
		/* Skip over Obsolete field in RTPI payload
		 * in Table 472 */
		off += 2;
		buf[off++] = ((port->sep_rtpi >> 8) & 0xff);
		buf[off++] = (port->sep_rtpi & 0xff);
		len += 8; /* Header size + Designation descriptor */
		/*
		 * Target port group identifier, see spc4r17
		 * section 7.7.3.8
		 *
		 * Get the PROTOCOL IDENTIFIER as defined by spc4r17
		 * section 7.5.1 Table 362
		 */
		if (dev->se_sub_dev->t10_alua.alua_type !=
				SPC3_ALUA_EMULATED)
			goto check_scsi_name;

		tg_pt_gp_mem = port->sep_alua_tg_pt_gp_mem;
		if (!tg_pt_gp_mem)
			goto check_lu_gp;

		spin_lock(&tg_pt_gp_mem->tg_pt_gp_mem_lock);
		tg_pt_gp = tg_pt_gp_mem->tg_pt_gp;
		if (!tg_pt_gp) {
			spin_unlock(&tg_pt_gp_mem->tg_pt_gp_mem_lock);
			goto check_lu_gp;
		}
		tg_pt_gp_id = tg_pt_gp->tg_pt_gp_id;
		spin_unlock(&tg_pt_gp_mem->tg_pt_gp_mem_lock);

		buf[off] =
			(tpg->se_tpg_tfo->get_fabric_proto_ident(tpg) << 4);
		buf[off++] |= 0x1; /* CODE SET == Binary */
		buf[off] = 0x80; /* Set PIV=1 */
		/* Set ASSOCIATION == target port: 01b */
		buf[off] |= 0x10;
		/* DESIGNATOR TYPE == Target port group identifier */
		buf[off++] |= 0x5;
		off++; /* Skip over Reserved */
		buf[off++] = 4; /* DESIGNATOR LENGTH */
		off += 2; /* Skip over Reserved Field */
		buf[off++] = ((tg_pt_gp_id >> 8) & 0xff);
		buf[off++] = (tg_pt_gp_id & 0xff);
		len += 8; /* Header size + Designation descriptor */
		/*
		 * Logical Unit Group identifier, see spc4r17
		 * section 7.7.3.8
		 */
check_lu_gp:
		lu_gp_mem = dev->dev_alua_lu_gp_mem;
		if (!lu_gp_mem)
			goto check_scsi_name;

		spin_lock(&lu_gp_mem->lu_gp_mem_lock);
		lu_gp = lu_gp_mem->lu_gp;
		if (!lu_gp) {
			spin_unlock(&lu_gp_mem->lu_gp_mem_lock);
			goto check_scsi_name;
		}
		lu_gp_id = lu_gp->lu_gp_id;
		spin_unlock(&lu_gp_mem->lu_gp_mem_lock);

		buf[off++] |= 0x1; /* CODE SET == Binary */
		/* DESIGNATOR TYPE == Logical Unit Group identifier */
		buf[off++] |= 0x6;
		off++; /* Skip over Reserved */
		buf[off++] = 4; /* DESIGNATOR LENGTH */
		off += 2; /* Skip over Reserved Field */
		buf[off++] = ((lu_gp_id >> 8) & 0xff);
		buf[off++] = (lu_gp_id & 0xff);
		len += 8; /* Header size + Designation descriptor */
		/*
		 * SCSI name string designator, see spc4r17
		 * section 7.7.3.11
		 *
		 * Get the PROTOCOL IDENTIFIER as defined by spc4r17
		 * section 7.5.1 Table 362
		 */
check_scsi_name:
		scsi_name_len = strlen(tpg->se_tpg_tfo->tpg_get_wwn(tpg));
		/* UTF-8 ",t,0x<16-bit TPGT>" + NULL Terminator */
		scsi_name_len += 10;
		/* Check for 4-byte padding */
		padding = ((-scsi_name_len) & 3);
		if (padding != 0)
			scsi_name_len += padding;
		/* Header size + Designation descriptor */
		scsi_name_len += 4;

		buf[off] =
			(tpg->se_tpg_tfo->get_fabric_proto_ident(tpg) << 4);
		buf[off++] |= 0x3; /* CODE SET == UTF-8 */
		buf[off] = 0x80; /* Set PIV=1 */
		/* Set ASSOCIATION == target port: 01b */
		buf[off] |= 0x10;
		/* DESIGNATOR TYPE == SCSI name string */
		buf[off++] |= 0x8;
		off += 2; /* Skip over Reserved and length */
		/*
		 * SCSI name string identifer containing, $FABRIC_MOD
		 * dependent information.  For LIO-Target and iSCSI
		 * Target Port, this means "<iSCSI name>,t,0x<TPGT> in
		 * UTF-8 encoding.
		 */
		tpgt = tpg->se_tpg_tfo->tpg_get_tag(tpg);
		scsi_name_len = sprintf(&buf[off], "%s,t,0x%04x",
					tpg->se_tpg_tfo->tpg_get_wwn(tpg), tpgt);
		scsi_name_len += 1 /* Include  NULL terminator */;
		/*
		 * The null-terminated, null-padded (see 4.4.2) SCSI
		 * NAME STRING field contains a UTF-8 format string.
		 * The number of bytes in the SCSI NAME STRING field
		 * (i.e., the value in the DESIGNATOR LENGTH field)
		 * shall be no larger than 256 and shall be a multiple
		 * of four.
		 */
		if (padding)
			scsi_name_len += padding;

		buf[off-1] = scsi_name_len;
		off += scsi_name_len;
		/* Header size + Designation descriptor */
		len += (scsi_name_len + 4);
	}
	buf[2] = ((len >> 8) & 0xff);
	buf[3] = (len & 0xff); /* Page Length for VPD 0x83 */
	return 0;
}

/* Extended INQUIRY Data VPD Page */
static int spc_emulate_evpd_86(struct se_cmd *cmd, unsigned char *buf)
{
	buf[3] = 0x3c;
	/* Set HEADSUP, ORDSUP, SIMPSUP */
	buf[5] = 0x07;

	/* If WriteCache emulation is enabled, set V_SUP */
	if (cmd->se_dev->se_sub_dev->se_dev_attrib.emulate_write_cache > 0)
		buf[6] = 0x01;
	return 0;
}

/* Block Limits VPD page */
static int spc_emulate_evpd_b0(struct se_cmd *cmd, unsigned char *buf)
{
	struct se_device *dev = cmd->se_dev;
	u32 max_sectors;
	int have_tp = 0;

	/*
	 * Following spc3r22 section 6.5.3 Block Limits VPD page, when
	 * emulate_tpu=1 or emulate_tpws=1 we will be expect a
	 * different page length for Thin Provisioning.
	 */
	if (dev->se_sub_dev->se_dev_attrib.emulate_tpu || dev->se_sub_dev->se_dev_attrib.emulate_tpws)
		have_tp = 1;

	buf[0] = dev->transport->get_device_type(dev);
	buf[3] = have_tp ? 0x3c : 0x10;

	/* Set WSNZ to 1 */
	buf[4] = 0x01;

	/*
	 * Set OPTIMAL TRANSFER LENGTH GRANULARITY
	 */
	put_unaligned_be16(1, &buf[6]);

	/*
	 * Set MAXIMUM TRANSFER LENGTH
	 */
	max_sectors = min(dev->se_sub_dev->se_dev_attrib.fabric_max_sectors,
			  dev->se_sub_dev->se_dev_attrib.hw_max_sectors);
	put_unaligned_be32(max_sectors, &buf[8]);

	/*
	 * Set OPTIMAL TRANSFER LENGTH
	 */
	put_unaligned_be32(dev->se_sub_dev->se_dev_attrib.optimal_sectors, &buf[12]);

	/*
	 * Exit now if we don't support TP.
	 */
	if (!have_tp)
		return 0;

	/*
	 * Set MAXIMUM UNMAP LBA COUNT
	 */
	put_unaligned_be32(dev->se_sub_dev->se_dev_attrib.max_unmap_lba_count, &buf[20]);

	/*
	 * Set MAXIMUM UNMAP BLOCK DESCRIPTOR COUNT
	 */
	put_unaligned_be32(dev->se_sub_dev->se_dev_attrib.max_unmap_block_desc_count,
			   &buf[24]);

	/*
	 * Set OPTIMAL UNMAP GRANULARITY
	 */
	put_unaligned_be32(dev->se_sub_dev->se_dev_attrib.unmap_granularity, &buf[28]);

	/*
	 * UNMAP GRANULARITY ALIGNMENT
	 */
	put_unaligned_be32(dev->se_sub_dev->se_dev_attrib.unmap_granularity_alignment,
			   &buf[32]);
	if (dev->se_sub_dev->se_dev_attrib.unmap_granularity_alignment != 0)
		buf[32] |= 0x80; /* Set the UGAVALID bit */

	return 0;
}

/* Block Device Characteristics VPD page */
static int spc_emulate_evpd_b1(struct se_cmd *cmd, unsigned char *buf)
{
	struct se_device *dev = cmd->se_dev;

	buf[0] = dev->transport->get_device_type(dev);
	buf[3] = 0x3c;
	buf[5] = dev->se_sub_dev->se_dev_attrib.is_nonrot ? 1 : 0;

	return 0;
}

/* Thin Provisioning VPD */
static int spc_emulate_evpd_b2(struct se_cmd *cmd, unsigned char *buf)
{
	struct se_device *dev = cmd->se_dev;

	/*
	 * From spc3r22 section 6.5.4 Thin Provisioning VPD page:
	 *
	 * The PAGE LENGTH field is defined in SPC-4. If the DP bit is set to
	 * zero, then the page length shall be set to 0004h.  If the DP bit
	 * is set to one, then the page length shall be set to the value
	 * defined in table 162.
	 */
	buf[0] = dev->transport->get_device_type(dev);

	/*
	 * Set Hardcoded length mentioned above for DP=0
	 */
	put_unaligned_be16(0x0004, &buf[2]);

	/*
	 * The THRESHOLD EXPONENT field indicates the threshold set size in
	 * LBAs as a power of 2 (i.e., the threshold set size is equal to
	 * 2(threshold exponent)).
	 *
	 * Note that this is currently set to 0x00 as mkp says it will be
	 * changing again.  We can enable this once it has settled in T10
	 * and is actually used by Linux/SCSI ML code.
	 */
	buf[4] = 0x00;

	/*
	 * A TPU bit set to one indicates that the device server supports
	 * the UNMAP command (see 5.25). A TPU bit set to zero indicates
	 * that the device server does not support the UNMAP command.
	 */
	if (dev->se_sub_dev->se_dev_attrib.emulate_tpu != 0)
		buf[5] = 0x80;

	/*
	 * A TPWS bit set to one indicates that the device server supports
	 * the use of the WRITE SAME (16) command (see 5.42) to unmap LBAs.
	 * A TPWS bit set to zero indicates that the device server does not
	 * support the use of the WRITE SAME (16) command to unmap LBAs.
	 */
	if (dev->se_sub_dev->se_dev_attrib.emulate_tpws != 0)
		buf[5] |= 0x40;

	return 0;
}

static int spc_emulate_evpd_00(struct se_cmd *cmd, unsigned char *buf);

static struct {
	uint8_t		page;
	int		(*emulate)(struct se_cmd *, unsigned char *);
} evpd_handlers[] = {
	{ .page = 0x00, .emulate = spc_emulate_evpd_00 },
	{ .page = 0x80, .emulate = spc_emulate_evpd_80 },
	{ .page = 0x83, .emulate = spc_emulate_evpd_83 },
	{ .page = 0x86, .emulate = spc_emulate_evpd_86 },
	{ .page = 0xb0, .emulate = spc_emulate_evpd_b0 },
	{ .page = 0xb1, .emulate = spc_emulate_evpd_b1 },
	{ .page = 0xb2, .emulate = spc_emulate_evpd_b2 },
};

/* supported vital product data pages */
static int spc_emulate_evpd_00(struct se_cmd *cmd, unsigned char *buf)
{
	int p;

	/*
	 * Only report the INQUIRY EVPD=1 pages after a valid NAA
	 * Registered Extended LUN WWN has been set via ConfigFS
	 * during device creation/restart.
	 */
	if (cmd->se_dev->se_sub_dev->su_dev_flags &
			SDF_EMULATED_VPD_UNIT_SERIAL) {
		buf[3] = ARRAY_SIZE(evpd_handlers);
		for (p = 0; p < ARRAY_SIZE(evpd_handlers); ++p)
			buf[p + 4] = evpd_handlers[p].page;
	}

	return 0;
}

static int spc_emulate_inquiry(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct se_portal_group *tpg = cmd->se_lun->lun_sep->sep_tpg;
	unsigned char *rbuf;
	unsigned char *cdb = cmd->t_task_cdb;
	unsigned char buf[SE_INQUIRY_BUF];
	int p, ret;

	if (dev == tpg->tpg_virt_lun0.lun_se_dev)
		buf[0] = 0x3f; /* Not connected */
	else
		buf[0] = dev->transport->get_device_type(dev);

	if (!(cdb[1] & 0x1)) {
		if (cdb[2]) {
			pr_err("INQUIRY with EVPD==0 but PAGE CODE=%02x\n",
			       cdb[2]);
			cmd->scsi_sense_reason = TCM_INVALID_CDB_FIELD;
			ret = -EINVAL;
			goto out;
		}

		ret = spc_emulate_inquiry_std(cmd, buf);
		goto out;
	}

	for (p = 0; p < ARRAY_SIZE(evpd_handlers); ++p) {
		if (cdb[2] == evpd_handlers[p].page) {
			buf[1] = cdb[2];
			ret = evpd_handlers[p].emulate(cmd, buf);
			goto out;
		}
	}

	pr_err("Unknown VPD Code: 0x%02x\n", cdb[2]);
	cmd->scsi_sense_reason = TCM_INVALID_CDB_FIELD;
	ret = -EINVAL;

out:
	rbuf = transport_kmap_data_sg(cmd);
	if (rbuf) {
		memcpy(rbuf, buf, min_t(u32, sizeof(buf), cmd->data_length));
		transport_kunmap_data_sg(cmd);
	}

	if (!ret)
		target_complete_cmd(cmd, GOOD);
	return ret;
}

static int spc_modesense_rwrecovery(unsigned char *p)
{
	p[0] = 0x01;
	p[1] = 0x0a;

	return 12;
}

static int spc_modesense_control(struct se_device *dev, unsigned char *p)
{
	p[0] = 0x0a;
	p[1] = 0x0a;
	p[2] = 2;
	/*
	 * From spc4r23, 7.4.7 Control mode page
	 *
	 * The QUEUE ALGORITHM MODIFIER field (see table 368) specifies
	 * restrictions on the algorithm used for reordering commands
	 * having the SIMPLE task attribute (see SAM-4).
	 *
	 *                    Table 368 -- QUEUE ALGORITHM MODIFIER field
	 *                         Code      Description
	 *                          0h       Restricted reordering
	 *                          1h       Unrestricted reordering allowed
	 *                          2h to 7h    Reserved
	 *                          8h to Fh    Vendor specific
	 *
	 * A value of zero in the QUEUE ALGORITHM MODIFIER field specifies that
	 * the device server shall order the processing sequence of commands
	 * having the SIMPLE task attribute such that data integrity is maintained
	 * for that I_T nexus (i.e., if the transmission of new SCSI transport protocol
	 * requests is halted at any time, the final value of all data observable
	 * on the medium shall be the same as if all the commands had been processed
	 * with the ORDERED task attribute).
	 *
	 * A value of one in the QUEUE ALGORITHM MODIFIER field specifies that the
	 * device server may reorder the processing sequence of commands having the
	 * SIMPLE task attribute in any manner. Any data integrity exposures related to
	 * command sequence order shall be explicitly handled by the application client
	 * through the selection of appropriate ommands and task attributes.
	 */
	p[3] = (dev->se_sub_dev->se_dev_attrib.emulate_rest_reord == 1) ? 0x00 : 0x10;
	/*
	 * From spc4r17, section 7.4.6 Control mode Page
	 *
	 * Unit Attention interlocks control (UN_INTLCK_CTRL) to code 00b
	 *
	 * 00b: The logical unit shall clear any unit attention condition
	 * reported in the same I_T_L_Q nexus transaction as a CHECK CONDITION
	 * status and shall not establish a unit attention condition when a com-
	 * mand is completed with BUSY, TASK SET FULL, or RESERVATION CONFLICT
	 * status.
	 *
	 * 10b: The logical unit shall not clear any unit attention condition
	 * reported in the same I_T_L_Q nexus transaction as a CHECK CONDITION
	 * status and shall not establish a unit attention condition when
	 * a command is completed with BUSY, TASK SET FULL, or RESERVATION
	 * CONFLICT status.
	 *
	 * 11b a The logical unit shall not clear any unit attention condition
	 * reported in the same I_T_L_Q nexus transaction as a CHECK CONDITION
	 * status and shall establish a unit attention condition for the
	 * initiator port associated with the I_T nexus on which the BUSY,
	 * TASK SET FULL, or RESERVATION CONFLICT status is being returned.
	 * Depending on the status, the additional sense code shall be set to
	 * PREVIOUS BUSY STATUS, PREVIOUS TASK SET FULL STATUS, or PREVIOUS
	 * RESERVATION CONFLICT STATUS. Until it is cleared by a REQUEST SENSE
	 * command, a unit attention condition shall be established only once
	 * for a BUSY, TASK SET FULL, or RESERVATION CONFLICT status regardless
	 * to the number of commands completed with one of those status codes.
	 */
	p[4] = (dev->se_sub_dev->se_dev_attrib.emulate_ua_intlck_ctrl == 2) ? 0x30 :
	       (dev->se_sub_dev->se_dev_attrib.emulate_ua_intlck_ctrl == 1) ? 0x20 : 0x00;
	/*
	 * From spc4r17, section 7.4.6 Control mode Page
	 *
	 * Task Aborted Status (TAS) bit set to zero.
	 *
	 * A task aborted status (TAS) bit set to zero specifies that aborted
	 * tasks shall be terminated by the device server without any response
	 * to the application client. A TAS bit set to one specifies that tasks
	 * aborted by the actions of an I_T nexus other than the I_T nexus on
	 * which the command was received shall be completed with TASK ABORTED
	 * status (see SAM-4).
	 */
	p[5] = (dev->se_sub_dev->se_dev_attrib.emulate_tas) ? 0x40 : 0x00;
	p[8] = 0xff;
	p[9] = 0xff;
	p[11] = 30;

	return 12;
}

static int spc_modesense_caching(struct se_device *dev, unsigned char *p)
{
	p[0] = 0x08;
	p[1] = 0x12;
	if (dev->se_sub_dev->se_dev_attrib.emulate_write_cache > 0)
		p[2] = 0x04; /* Write Cache Enable */
	p[12] = 0x20; /* Disabled Read Ahead */

	return 20;
}

static void spc_modesense_write_protect(unsigned char *buf, int type)
{
	/*
	 * I believe that the WP bit (bit 7) in the mode header is the same for
	 * all device types..
	 */
	switch (type) {
	case TYPE_DISK:
	case TYPE_TAPE:
	default:
		buf[0] |= 0x80; /* WP bit */
		break;
	}
}

static void spc_modesense_dpofua(unsigned char *buf, int type)
{
	switch (type) {
	case TYPE_DISK:
		buf[0] |= 0x10; /* DPOFUA bit */
		break;
	default:
		break;
	}
}

static int spc_emulate_modesense(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	char *cdb = cmd->t_task_cdb;
	unsigned char *rbuf;
	int type = dev->transport->get_device_type(dev);
	int ten = (cmd->t_task_cdb[0] == MODE_SENSE_10);
	u32 offset = ten ? 8 : 4;
	int length = 0;
	unsigned char buf[SE_MODE_PAGE_BUF];

	memset(buf, 0, SE_MODE_PAGE_BUF);

	switch (cdb[2] & 0x3f) {
	case 0x01:
		length = spc_modesense_rwrecovery(&buf[offset]);
		break;
	case 0x08:
		length = spc_modesense_caching(dev, &buf[offset]);
		break;
	case 0x0a:
		length = spc_modesense_control(dev, &buf[offset]);
		break;
	case 0x3f:
		length = spc_modesense_rwrecovery(&buf[offset]);
		length += spc_modesense_caching(dev, &buf[offset+length]);
		length += spc_modesense_control(dev, &buf[offset+length]);
		break;
	default:
		pr_err("MODE SENSE: unimplemented page/subpage: 0x%02x/0x%02x\n",
		       cdb[2] & 0x3f, cdb[3]);
		cmd->scsi_sense_reason = TCM_UNKNOWN_MODE_PAGE;
		return -EINVAL;
	}
	offset += length;

	if (ten) {
		offset -= 2;
		buf[0] = (offset >> 8) & 0xff;
		buf[1] = offset & 0xff;
		offset += 2;

		if ((cmd->se_lun->lun_access & TRANSPORT_LUNFLAGS_READ_ONLY) ||
		    (cmd->se_deve &&
		    (cmd->se_deve->lun_flags & TRANSPORT_LUNFLAGS_READ_ONLY)))
			spc_modesense_write_protect(&buf[3], type);

		if ((dev->se_sub_dev->se_dev_attrib.emulate_write_cache > 0) &&
		    (dev->se_sub_dev->se_dev_attrib.emulate_fua_write > 0))
			spc_modesense_dpofua(&buf[3], type);
	} else {
		offset -= 1;
		buf[0] = offset & 0xff;
		offset += 1;

		if ((cmd->se_lun->lun_access & TRANSPORT_LUNFLAGS_READ_ONLY) ||
		    (cmd->se_deve &&
		    (cmd->se_deve->lun_flags & TRANSPORT_LUNFLAGS_READ_ONLY)))
			spc_modesense_write_protect(&buf[2], type);

		if ((dev->se_sub_dev->se_dev_attrib.emulate_write_cache > 0) &&
		    (dev->se_sub_dev->se_dev_attrib.emulate_fua_write > 0))
			spc_modesense_dpofua(&buf[2], type);
	}

	rbuf = transport_kmap_data_sg(cmd);
	if (rbuf) {
		memcpy(rbuf, buf, min(offset, cmd->data_length));
		transport_kunmap_data_sg(cmd);
	}

	target_complete_cmd(cmd, GOOD);
	return 0;
}

static int spc_emulate_request_sense(struct se_cmd *cmd)
{
	unsigned char *cdb = cmd->t_task_cdb;
	unsigned char *rbuf;
	u8 ua_asc = 0, ua_ascq = 0;
	unsigned char buf[SE_SENSE_BUF];

	memset(buf, 0, SE_SENSE_BUF);

	if (cdb[1] & 0x01) {
		pr_err("REQUEST_SENSE description emulation not"
			" supported\n");
		cmd->scsi_sense_reason = TCM_INVALID_CDB_FIELD;
		return -ENOSYS;
	}

	rbuf = transport_kmap_data_sg(cmd);
	if (cmd->scsi_sense_reason != 0) {
		/*
		 * Out of memory.  We will fail with CHECK CONDITION, so
		 * we must not clear the unit attention condition.
		 */
		target_complete_cmd(cmd, CHECK_CONDITION);
		return 0;
	} else if (!core_scsi3_ua_clear_for_request_sense(cmd, &ua_asc, &ua_ascq)) {
		/*
		 * CURRENT ERROR, UNIT ATTENTION
		 */
		buf[0] = 0x70;
		buf[SPC_SENSE_KEY_OFFSET] = UNIT_ATTENTION;

		/*
		 * The Additional Sense Code (ASC) from the UNIT ATTENTION
		 */
		buf[SPC_ASC_KEY_OFFSET] = ua_asc;
		buf[SPC_ASCQ_KEY_OFFSET] = ua_ascq;
		buf[7] = 0x0A;
	} else {
		/*
		 * CURRENT ERROR, NO SENSE
		 */
		buf[0] = 0x70;
		buf[SPC_SENSE_KEY_OFFSET] = NO_SENSE;

		/*
		 * NO ADDITIONAL SENSE INFORMATION
		 */
		buf[SPC_ASC_KEY_OFFSET] = 0x00;
		buf[7] = 0x0A;
	}

	if (rbuf) {
		memcpy(rbuf, buf, min_t(u32, sizeof(buf), cmd->data_length));
		transport_kunmap_data_sg(cmd);
	}

	target_complete_cmd(cmd, GOOD);
	return 0;
}

static int spc_emulate_testunitready(struct se_cmd *cmd)
{
	target_complete_cmd(cmd, GOOD);
	return 0;
}

int spc_parse_cdb(struct se_cmd *cmd, unsigned int *size)
{
	struct se_device *dev = cmd->se_dev;
	struct se_subsystem_dev *su_dev = dev->se_sub_dev;
	unsigned char *cdb = cmd->t_task_cdb;

	switch (cdb[0]) {
	case MODE_SELECT:
		*size = cdb[4];
		break;
	case MODE_SELECT_10:
		*size = (cdb[7] << 8) + cdb[8];
		break;
	case MODE_SENSE:
		*size = cdb[4];
		cmd->execute_cmd = spc_emulate_modesense;
		break;
	case MODE_SENSE_10:
		*size = (cdb[7] << 8) + cdb[8];
		cmd->execute_cmd = spc_emulate_modesense;
		break;
	case LOG_SELECT:
	case LOG_SENSE:
		*size = (cdb[7] << 8) + cdb[8];
		break;
	case PERSISTENT_RESERVE_IN:
		if (su_dev->t10_pr.res_type == SPC3_PERSISTENT_RESERVATIONS)
			cmd->execute_cmd = target_scsi3_emulate_pr_in;
		*size = (cdb[7] << 8) + cdb[8];
		break;
	case PERSISTENT_RESERVE_OUT:
		if (su_dev->t10_pr.res_type == SPC3_PERSISTENT_RESERVATIONS)
			cmd->execute_cmd = target_scsi3_emulate_pr_out;
		*size = (cdb[7] << 8) + cdb[8];
		break;
	case RELEASE:
	case RELEASE_10:
		if (cdb[0] == RELEASE_10)
			*size = (cdb[7] << 8) | cdb[8];
		else
			*size = cmd->data_length;

		if (su_dev->t10_pr.res_type != SPC_PASSTHROUGH)
			cmd->execute_cmd = target_scsi2_reservation_release;
		break;
	case RESERVE:
	case RESERVE_10:
		/*
		 * The SPC-2 RESERVE does not contain a size in the SCSI CDB.
		 * Assume the passthrough or $FABRIC_MOD will tell us about it.
		 */
		if (cdb[0] == RESERVE_10)
			*size = (cdb[7] << 8) | cdb[8];
		else
			*size = cmd->data_length;

		/*
		 * Setup the legacy emulated handler for SPC-2 and
		 * >= SPC-3 compatible reservation handling (CRH=1)
		 * Otherwise, we assume the underlying SCSI logic is
		 * is running in SPC_PASSTHROUGH, and wants reservations
		 * emulation disabled.
		 */
		if (su_dev->t10_pr.res_type != SPC_PASSTHROUGH)
			cmd->execute_cmd = target_scsi2_reservation_reserve;
		break;
	case REQUEST_SENSE:
		*size = cdb[4];
		cmd->execute_cmd = spc_emulate_request_sense;
		break;
	case INQUIRY:
		*size = (cdb[3] << 8) + cdb[4];

		/*
		 * Do implict HEAD_OF_QUEUE processing for INQUIRY.
		 * See spc4r17 section 5.3
		 */
		if (cmd->se_dev->dev_task_attr_type == SAM_TASK_ATTR_EMULATED)
			cmd->sam_task_attr = MSG_HEAD_TAG;
		cmd->execute_cmd = spc_emulate_inquiry;
		break;
	case SECURITY_PROTOCOL_IN:
	case SECURITY_PROTOCOL_OUT:
		*size = (cdb[6] << 24) | (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];
		break;
	case EXTENDED_COPY:
	case READ_ATTRIBUTE:
	case RECEIVE_COPY_RESULTS:
	case WRITE_ATTRIBUTE:
		*size = (cdb[10] << 24) | (cdb[11] << 16) |
		       (cdb[12] << 8) | cdb[13];
		break;
	case RECEIVE_DIAGNOSTIC:
	case SEND_DIAGNOSTIC:
		*size = (cdb[3] << 8) | cdb[4];
		break;
	case WRITE_BUFFER:
		*size = (cdb[6] << 16) + (cdb[7] << 8) + cdb[8];
		break;
	case REPORT_LUNS:
		cmd->execute_cmd = target_report_luns;
		*size = (cdb[6] << 24) | (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];
		/*
		 * Do implict HEAD_OF_QUEUE processing for REPORT_LUNS
		 * See spc4r17 section 5.3
		 */
		if (cmd->se_dev->dev_task_attr_type == SAM_TASK_ATTR_EMULATED)
			cmd->sam_task_attr = MSG_HEAD_TAG;
		break;
	case TEST_UNIT_READY:
		cmd->execute_cmd = spc_emulate_testunitready;
		*size = 0;
		break;
	case MAINTENANCE_IN:
		if (dev->transport->get_device_type(dev) != TYPE_ROM) {
			/*
			 * MAINTENANCE_IN from SCC-2
			 * Check for emulated MI_REPORT_TARGET_PGS
			 */
			if ((cdb[1] & 0x1f) == MI_REPORT_TARGET_PGS &&
			    su_dev->t10_alua.alua_type == SPC3_ALUA_EMULATED) {
				cmd->execute_cmd =
					target_emulate_report_target_port_groups;
			}
			*size = get_unaligned_be32(&cdb[6]);
		} else {
			/*
			 * GPCMD_SEND_KEY from multi media commands
			 */
			*size = get_unaligned_be16(&cdb[8]);
		}
		break;
	case MAINTENANCE_OUT:
		if (dev->transport->get_device_type(dev) != TYPE_ROM) {
			/*
			 * MAINTENANCE_OUT from SCC-2
			 * Check for emulated MO_SET_TARGET_PGS.
			 */
			if (cdb[1] == MO_SET_TARGET_PGS &&
			    su_dev->t10_alua.alua_type == SPC3_ALUA_EMULATED) {
				cmd->execute_cmd =
					target_emulate_set_target_port_groups;
			}
			*size = get_unaligned_be32(&cdb[6]);
		} else {
			/*
			 * GPCMD_SEND_KEY from multi media commands
			 */
			*size = get_unaligned_be16(&cdb[8]);
		}
		break;
	default:
		pr_warn("TARGET_CORE[%s]: Unsupported SCSI Opcode"
			" 0x%02x, sending CHECK_CONDITION.\n",
			cmd->se_tfo->get_fabric_name(), cdb[0]);
		cmd->se_cmd_flags |= SCF_SCSI_CDB_EXCEPTION;
		cmd->scsi_sense_reason = TCM_UNSUPPORTED_SCSI_OPCODE;
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(spc_parse_cdb);
