/*
 * CDB emulation for non-READ/WRITE commands.
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

#include <target/target_core_base.h>
#include <target/target_core_backend.h>
#include <target/target_core_fabric.h>

#include "target_core_internal.h"
#include "target_core_ua.h"

static void
target_fill_alua_data(struct se_port *port, unsigned char *buf)
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

static int
target_emulate_inquiry_std(struct se_cmd *cmd)
{
	struct se_lun *lun = cmd->se_lun;
	struct se_device *dev = cmd->se_dev;
	struct se_portal_group *tpg = lun->lun_sep->sep_tpg;
	unsigned char *buf;

	/*
	 * Make sure we at least have 6 bytes of INQUIRY response
	 * payload going back for EVPD=0
	 */
	if (cmd->data_length < 6) {
		pr_err("SCSI Inquiry payload length: %u"
			" too small for EVPD=0\n", cmd->data_length);
		return -EINVAL;
	}

	buf = transport_kmap_data_sg(cmd);

	if (dev == tpg->tpg_virt_lun0.lun_se_dev) {
		buf[0] = 0x3f; /* Not connected */
	} else {
		buf[0] = dev->transport->get_device_type(dev);
		if (buf[0] == TYPE_TAPE)
			buf[1] = 0x80;
	}
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
		target_fill_alua_data(lun->lun_sep, buf);

	if (cmd->data_length < 8) {
		buf[4] = 1; /* Set additional length to 1 */
		goto out;
	}

	buf[7] = 0x32; /* Sync=1 and CmdQue=1 */

	/*
	 * Do not include vendor, product, reversion info in INQUIRY
	 * response payload for cdbs with a small allocation length.
	 */
	if (cmd->data_length < 36) {
		buf[4] = 3; /* Set additional length to 3 */
		goto out;
	}

	snprintf(&buf[8], 8, "LIO-ORG");
	snprintf(&buf[16], 16, "%s", dev->se_sub_dev->t10_wwn.model);
	snprintf(&buf[32], 4, "%s", dev->se_sub_dev->t10_wwn.revision);
	buf[4] = 31; /* Set additional length to 31 */

out:
	transport_kunmap_data_sg(cmd);
	return 0;
}

/* unit serial number */
static int
target_emulate_evpd_80(struct se_cmd *cmd, unsigned char *buf)
{
	struct se_device *dev = cmd->se_dev;
	u16 len = 0;

	if (dev->se_sub_dev->su_dev_flags &
			SDF_EMULATED_VPD_UNIT_SERIAL) {
		u32 unit_serial_len;

		unit_serial_len = strlen(dev->se_sub_dev->t10_wwn.unit_serial);
		unit_serial_len++; /* For NULL Terminator */

		if (((len + 4) + unit_serial_len) > cmd->data_length) {
			len += unit_serial_len;
			buf[2] = ((len >> 8) & 0xff);
			buf[3] = (len & 0xff);
			return 0;
		}
		len += sprintf(&buf[4], "%s",
			dev->se_sub_dev->t10_wwn.unit_serial);
		len++; /* Extra Byte for NULL Terminator */
		buf[3] = len;
	}
	return 0;
}

static void
target_parse_naa_6h_vendor_specific(struct se_device *dev, unsigned char *buf)
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
static int
target_emulate_evpd_83(struct se_cmd *cmd, unsigned char *buf)
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

	if (off + 20 > cmd->data_length)
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
	target_parse_naa_6h_vendor_specific(dev, &buf[off]);

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

		if ((len + (id_len + 4) +
		    (prod_len + unit_serial_len)) >
				cmd->data_length) {
			len += (prod_len + unit_serial_len);
			goto check_port;
		}
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
check_port:
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
		if (((len + 4) + 8) > cmd->data_length) {
			len += 8;
			goto check_tpgi;
		}
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
check_tpgi:
		if (dev->se_sub_dev->t10_alua.alua_type !=
				SPC3_ALUA_EMULATED)
			goto check_scsi_name;

		if (((len + 4) + 8) > cmd->data_length) {
			len += 8;
			goto check_lu_gp;
		}
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
		if (((len + 4) + 8) > cmd->data_length) {
			len += 8;
			goto check_scsi_name;
		}
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

		if (((len + 4) + scsi_name_len) > cmd->data_length) {
			len += scsi_name_len;
			goto set_len;
		}
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
set_len:
	buf[2] = ((len >> 8) & 0xff);
	buf[3] = (len & 0xff); /* Page Length for VPD 0x83 */
	return 0;
}

/* Extended INQUIRY Data VPD Page */
static int
target_emulate_evpd_86(struct se_cmd *cmd, unsigned char *buf)
{
	if (cmd->data_length < 60)
		return 0;

	buf[3] = 0x3c;
	/* Set HEADSUP, ORDSUP, SIMPSUP */
	buf[5] = 0x07;

	/* If WriteCache emulation is enabled, set V_SUP */
	if (cmd->se_dev->se_sub_dev->se_dev_attrib.emulate_write_cache > 0)
		buf[6] = 0x01;
	return 0;
}

/* Block Limits VPD page */
static int
target_emulate_evpd_b0(struct se_cmd *cmd, unsigned char *buf)
{
	struct se_device *dev = cmd->se_dev;
	int have_tp = 0;

	/*
	 * Following sbc3r22 section 6.5.3 Block Limits VPD page, when
	 * emulate_tpu=1 or emulate_tpws=1 we will be expect a
	 * different page length for Thin Provisioning.
	 */
	if (dev->se_sub_dev->se_dev_attrib.emulate_tpu || dev->se_sub_dev->se_dev_attrib.emulate_tpws)
		have_tp = 1;

	if (cmd->data_length < (0x10 + 4)) {
		pr_debug("Received data_length: %u"
			" too small for EVPD 0xb0\n",
			cmd->data_length);
		return -EINVAL;
	}

	if (have_tp && cmd->data_length < (0x3c + 4)) {
		pr_debug("Received data_length: %u"
			" too small for TPE=1 EVPD 0xb0\n",
			cmd->data_length);
		have_tp = 0;
	}

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
	put_unaligned_be32(dev->se_sub_dev->se_dev_attrib.max_sectors, &buf[8]);

	/*
	 * Set OPTIMAL TRANSFER LENGTH
	 */
	put_unaligned_be32(dev->se_sub_dev->se_dev_attrib.optimal_sectors, &buf[12]);

	/*
	 * Exit now if we don't support TP or the initiator sent a too
	 * short buffer.
	 */
	if (!have_tp || cmd->data_length < (0x3c + 4))
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
static int
target_emulate_evpd_b1(struct se_cmd *cmd, unsigned char *buf)
{
	struct se_device *dev = cmd->se_dev;

	buf[0] = dev->transport->get_device_type(dev);
	buf[3] = 0x3c;

	if (cmd->data_length >= 5 &&
	    dev->se_sub_dev->se_dev_attrib.is_nonrot)
		buf[5] = 1;

	return 0;
}

/* Thin Provisioning VPD */
static int
target_emulate_evpd_b2(struct se_cmd *cmd, unsigned char *buf)
{
	struct se_device *dev = cmd->se_dev;

	/*
	 * From sbc3r22 section 6.5.4 Thin Provisioning VPD page:
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

static int
target_emulate_evpd_00(struct se_cmd *cmd, unsigned char *buf);

static struct {
	uint8_t		page;
	int		(*emulate)(struct se_cmd *, unsigned char *);
} evpd_handlers[] = {
	{ .page = 0x00, .emulate = target_emulate_evpd_00 },
	{ .page = 0x80, .emulate = target_emulate_evpd_80 },
	{ .page = 0x83, .emulate = target_emulate_evpd_83 },
	{ .page = 0x86, .emulate = target_emulate_evpd_86 },
	{ .page = 0xb0, .emulate = target_emulate_evpd_b0 },
	{ .page = 0xb1, .emulate = target_emulate_evpd_b1 },
	{ .page = 0xb2, .emulate = target_emulate_evpd_b2 },
};

/* supported vital product data pages */
static int
target_emulate_evpd_00(struct se_cmd *cmd, unsigned char *buf)
{
	int p;

	if (cmd->data_length < 8)
		return 0;
	/*
	 * Only report the INQUIRY EVPD=1 pages after a valid NAA
	 * Registered Extended LUN WWN has been set via ConfigFS
	 * during device creation/restart.
	 */
	if (cmd->se_dev->se_sub_dev->su_dev_flags &
			SDF_EMULATED_VPD_UNIT_SERIAL) {
		buf[3] = ARRAY_SIZE(evpd_handlers);
		for (p = 0; p < min_t(int, ARRAY_SIZE(evpd_handlers),
				      cmd->data_length - 4); ++p)
			buf[p + 4] = evpd_handlers[p].page;
	}

	return 0;
}

int target_emulate_inquiry(struct se_task *task)
{
	struct se_cmd *cmd = task->task_se_cmd;
	struct se_device *dev = cmd->se_dev;
	unsigned char *buf;
	unsigned char *cdb = cmd->t_task_cdb;
	int p, ret;

	if (!(cdb[1] & 0x1)) {
		if (cdb[2]) {
			pr_err("INQUIRY with EVPD==0 but PAGE CODE=%02x\n",
			       cdb[2]);
			cmd->scsi_sense_reason = TCM_INVALID_CDB_FIELD;
			return -EINVAL;
		}

		ret = target_emulate_inquiry_std(cmd);
		goto out;
	}

	/*
	 * Make sure we at least have 4 bytes of INQUIRY response
	 * payload for 0x00 going back for EVPD=1.  Note that 0x80
	 * and 0x83 will check for enough payload data length and
	 * jump to set_len: label when there is not enough inquiry EVPD
	 * payload length left for the next outgoing EVPD metadata
	 */
	if (cmd->data_length < 4) {
		pr_err("SCSI Inquiry payload length: %u"
			" too small for EVPD=1\n", cmd->data_length);
		cmd->scsi_sense_reason = TCM_INVALID_CDB_FIELD;
		return -EINVAL;
	}

	buf = transport_kmap_data_sg(cmd);

	buf[0] = dev->transport->get_device_type(dev);

	for (p = 0; p < ARRAY_SIZE(evpd_handlers); ++p) {
		if (cdb[2] == evpd_handlers[p].page) {
			buf[1] = cdb[2];
			ret = evpd_handlers[p].emulate(cmd, buf);
			goto out_unmap;
		}
	}

	pr_err("Unknown VPD Code: 0x%02x\n", cdb[2]);
	cmd->scsi_sense_reason = TCM_INVALID_CDB_FIELD;
	ret = -EINVAL;

out_unmap:
	transport_kunmap_data_sg(cmd);
out:
	if (!ret) {
		task->task_scsi_status = GOOD;
		transport_complete_task(task, 1);
	}
	return ret;
}

int target_emulate_readcapacity(struct se_task *task)
{
	struct se_cmd *cmd = task->task_se_cmd;
	struct se_device *dev = cmd->se_dev;
	unsigned char *buf;
	unsigned long long blocks_long = dev->transport->get_blocks(dev);
	u32 blocks;

	if (blocks_long >= 0x00000000ffffffff)
		blocks = 0xffffffff;
	else
		blocks = (u32)blocks_long;

	buf = transport_kmap_data_sg(cmd);

	buf[0] = (blocks >> 24) & 0xff;
	buf[1] = (blocks >> 16) & 0xff;
	buf[2] = (blocks >> 8) & 0xff;
	buf[3] = blocks & 0xff;
	buf[4] = (dev->se_sub_dev->se_dev_attrib.block_size >> 24) & 0xff;
	buf[5] = (dev->se_sub_dev->se_dev_attrib.block_size >> 16) & 0xff;
	buf[6] = (dev->se_sub_dev->se_dev_attrib.block_size >> 8) & 0xff;
	buf[7] = dev->se_sub_dev->se_dev_attrib.block_size & 0xff;
	/*
	 * Set max 32-bit blocks to signal SERVICE ACTION READ_CAPACITY_16
	*/
	if (dev->se_sub_dev->se_dev_attrib.emulate_tpu || dev->se_sub_dev->se_dev_attrib.emulate_tpws)
		put_unaligned_be32(0xFFFFFFFF, &buf[0]);

	transport_kunmap_data_sg(cmd);

	task->task_scsi_status = GOOD;
	transport_complete_task(task, 1);
	return 0;
}

int target_emulate_readcapacity_16(struct se_task *task)
{
	struct se_cmd *cmd = task->task_se_cmd;
	struct se_device *dev = cmd->se_dev;
	unsigned char *buf;
	unsigned long long blocks = dev->transport->get_blocks(dev);

	buf = transport_kmap_data_sg(cmd);

	buf[0] = (blocks >> 56) & 0xff;
	buf[1] = (blocks >> 48) & 0xff;
	buf[2] = (blocks >> 40) & 0xff;
	buf[3] = (blocks >> 32) & 0xff;
	buf[4] = (blocks >> 24) & 0xff;
	buf[5] = (blocks >> 16) & 0xff;
	buf[6] = (blocks >> 8) & 0xff;
	buf[7] = blocks & 0xff;
	buf[8] = (dev->se_sub_dev->se_dev_attrib.block_size >> 24) & 0xff;
	buf[9] = (dev->se_sub_dev->se_dev_attrib.block_size >> 16) & 0xff;
	buf[10] = (dev->se_sub_dev->se_dev_attrib.block_size >> 8) & 0xff;
	buf[11] = dev->se_sub_dev->se_dev_attrib.block_size & 0xff;
	/*
	 * Set Thin Provisioning Enable bit following sbc3r22 in section
	 * READ CAPACITY (16) byte 14 if emulate_tpu or emulate_tpws is enabled.
	 */
	if (dev->se_sub_dev->se_dev_attrib.emulate_tpu || dev->se_sub_dev->se_dev_attrib.emulate_tpws)
		buf[14] = 0x80;

	transport_kunmap_data_sg(cmd);

	task->task_scsi_status = GOOD;
	transport_complete_task(task, 1);
	return 0;
}

static int
target_modesense_rwrecovery(unsigned char *p)
{
	p[0] = 0x01;
	p[1] = 0x0a;

	return 12;
}

static int
target_modesense_control(struct se_device *dev, unsigned char *p)
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

static int
target_modesense_caching(struct se_device *dev, unsigned char *p)
{
	p[0] = 0x08;
	p[1] = 0x12;
	if (dev->se_sub_dev->se_dev_attrib.emulate_write_cache > 0)
		p[2] = 0x04; /* Write Cache Enable */
	p[12] = 0x20; /* Disabled Read Ahead */

	return 20;
}

static void
target_modesense_write_protect(unsigned char *buf, int type)
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

static void
target_modesense_dpofua(unsigned char *buf, int type)
{
	switch (type) {
	case TYPE_DISK:
		buf[0] |= 0x10; /* DPOFUA bit */
		break;
	default:
		break;
	}
}

int target_emulate_modesense(struct se_task *task)
{
	struct se_cmd *cmd = task->task_se_cmd;
	struct se_device *dev = cmd->se_dev;
	char *cdb = cmd->t_task_cdb;
	unsigned char *rbuf;
	int type = dev->transport->get_device_type(dev);
	int ten = (cmd->t_task_cdb[0] == MODE_SENSE_10);
	int offset = ten ? 8 : 4;
	int length = 0;
	unsigned char buf[SE_MODE_PAGE_BUF];

	memset(buf, 0, SE_MODE_PAGE_BUF);

	switch (cdb[2] & 0x3f) {
	case 0x01:
		length = target_modesense_rwrecovery(&buf[offset]);
		break;
	case 0x08:
		length = target_modesense_caching(dev, &buf[offset]);
		break;
	case 0x0a:
		length = target_modesense_control(dev, &buf[offset]);
		break;
	case 0x3f:
		length = target_modesense_rwrecovery(&buf[offset]);
		length += target_modesense_caching(dev, &buf[offset+length]);
		length += target_modesense_control(dev, &buf[offset+length]);
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

		if ((cmd->se_lun->lun_access & TRANSPORT_LUNFLAGS_READ_ONLY) ||
		    (cmd->se_deve &&
		    (cmd->se_deve->lun_flags & TRANSPORT_LUNFLAGS_READ_ONLY)))
			target_modesense_write_protect(&buf[3], type);

		if ((dev->se_sub_dev->se_dev_attrib.emulate_write_cache > 0) &&
		    (dev->se_sub_dev->se_dev_attrib.emulate_fua_write > 0))
			target_modesense_dpofua(&buf[3], type);

		if ((offset + 2) > cmd->data_length)
			offset = cmd->data_length;

	} else {
		offset -= 1;
		buf[0] = offset & 0xff;

		if ((cmd->se_lun->lun_access & TRANSPORT_LUNFLAGS_READ_ONLY) ||
		    (cmd->se_deve &&
		    (cmd->se_deve->lun_flags & TRANSPORT_LUNFLAGS_READ_ONLY)))
			target_modesense_write_protect(&buf[2], type);

		if ((dev->se_sub_dev->se_dev_attrib.emulate_write_cache > 0) &&
		    (dev->se_sub_dev->se_dev_attrib.emulate_fua_write > 0))
			target_modesense_dpofua(&buf[2], type);

		if ((offset + 1) > cmd->data_length)
			offset = cmd->data_length;
	}

	rbuf = transport_kmap_data_sg(cmd);
	memcpy(rbuf, buf, offset);
	transport_kunmap_data_sg(cmd);

	task->task_scsi_status = GOOD;
	transport_complete_task(task, 1);
	return 0;
}

int target_emulate_request_sense(struct se_task *task)
{
	struct se_cmd *cmd = task->task_se_cmd;
	unsigned char *cdb = cmd->t_task_cdb;
	unsigned char *buf;
	u8 ua_asc = 0, ua_ascq = 0;
	int err = 0;

	if (cdb[1] & 0x01) {
		pr_err("REQUEST_SENSE description emulation not"
			" supported\n");
		cmd->scsi_sense_reason = TCM_INVALID_CDB_FIELD;
		return -ENOSYS;
	}

	buf = transport_kmap_data_sg(cmd);

	if (!core_scsi3_ua_clear_for_request_sense(cmd, &ua_asc, &ua_ascq)) {
		/*
		 * CURRENT ERROR, UNIT ATTENTION
		 */
		buf[0] = 0x70;
		buf[SPC_SENSE_KEY_OFFSET] = UNIT_ATTENTION;

		if (cmd->data_length < 18) {
			buf[7] = 0x00;
			err = -EINVAL;
			goto end;
		}
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

		if (cmd->data_length < 18) {
			buf[7] = 0x00;
			err = -EINVAL;
			goto end;
		}
		/*
		 * NO ADDITIONAL SENSE INFORMATION
		 */
		buf[SPC_ASC_KEY_OFFSET] = 0x00;
		buf[7] = 0x0A;
	}

end:
	transport_kunmap_data_sg(cmd);
	task->task_scsi_status = GOOD;
	transport_complete_task(task, 1);
	return 0;
}

/*
 * Used for TCM/IBLOCK and TCM/FILEIO for block/blk-lib.c level discard support.
 * Note this is not used for TCM/pSCSI passthrough
 */
int target_emulate_unmap(struct se_task *task)
{
	struct se_cmd *cmd = task->task_se_cmd;
	struct se_device *dev = cmd->se_dev;
	unsigned char *buf, *ptr = NULL;
	unsigned char *cdb = &cmd->t_task_cdb[0];
	sector_t lba;
	unsigned int size = cmd->data_length, range;
	int ret = 0, offset;
	unsigned short dl, bd_dl;

	if (!dev->transport->do_discard) {
		pr_err("UNMAP emulation not supported for: %s\n",
				dev->transport->name);
		cmd->scsi_sense_reason = TCM_UNSUPPORTED_SCSI_OPCODE;
		return -ENOSYS;
	}

	/* First UNMAP block descriptor starts at 8 byte offset */
	offset = 8;
	size -= 8;
	dl = get_unaligned_be16(&cdb[0]);
	bd_dl = get_unaligned_be16(&cdb[2]);

	buf = transport_kmap_data_sg(cmd);

	ptr = &buf[offset];
	pr_debug("UNMAP: Sub: %s Using dl: %hu bd_dl: %hu size: %hu"
		" ptr: %p\n", dev->transport->name, dl, bd_dl, size, ptr);

	while (size) {
		lba = get_unaligned_be64(&ptr[0]);
		range = get_unaligned_be32(&ptr[8]);
		pr_debug("UNMAP: Using lba: %llu and range: %u\n",
				 (unsigned long long)lba, range);

		ret = dev->transport->do_discard(dev, lba, range);
		if (ret < 0) {
			pr_err("blkdev_issue_discard() failed: %d\n",
					ret);
			goto err;
		}

		ptr += 16;
		size -= 16;
	}

err:
	transport_kunmap_data_sg(cmd);
	if (!ret) {
		task->task_scsi_status = GOOD;
		transport_complete_task(task, 1);
	}
	return ret;
}

/*
 * Used for TCM/IBLOCK and TCM/FILEIO for block/blk-lib.c level discard support.
 * Note this is not used for TCM/pSCSI passthrough
 */
int target_emulate_write_same(struct se_task *task)
{
	struct se_cmd *cmd = task->task_se_cmd;
	struct se_device *dev = cmd->se_dev;
	sector_t range;
	sector_t lba = cmd->t_task_lba;
	u32 num_blocks;
	int ret;

	if (!dev->transport->do_discard) {
		pr_err("WRITE_SAME emulation not supported"
				" for: %s\n", dev->transport->name);
		cmd->scsi_sense_reason = TCM_UNSUPPORTED_SCSI_OPCODE;
		return -ENOSYS;
	}

	if (cmd->t_task_cdb[0] == WRITE_SAME)
		num_blocks = get_unaligned_be16(&cmd->t_task_cdb[7]);
	else if (cmd->t_task_cdb[0] == WRITE_SAME_16)
		num_blocks = get_unaligned_be32(&cmd->t_task_cdb[10]);
	else /* WRITE_SAME_32 via VARIABLE_LENGTH_CMD */
		num_blocks = get_unaligned_be32(&cmd->t_task_cdb[28]);

	/*
	 * Use the explicit range when non zero is supplied, otherwise calculate
	 * the remaining range based on ->get_blocks() - starting LBA.
	 */
	if (num_blocks != 0)
		range = num_blocks;
	else
		range = (dev->transport->get_blocks(dev) - lba);

	pr_debug("WRITE_SAME UNMAP: LBA: %llu Range: %llu\n",
		 (unsigned long long)lba, (unsigned long long)range);

	ret = dev->transport->do_discard(dev, lba, range);
	if (ret < 0) {
		pr_debug("blkdev_issue_discard() failed for WRITE_SAME\n");
		return ret;
	}

	task->task_scsi_status = GOOD;
	transport_complete_task(task, 1);
	return 0;
}

int target_emulate_synchronize_cache(struct se_task *task)
{
	struct se_device *dev = task->task_se_cmd->se_dev;
	struct se_cmd *cmd = task->task_se_cmd;

	if (!dev->transport->do_sync_cache) {
		pr_err("SYNCHRONIZE_CACHE emulation not supported"
			" for: %s\n", dev->transport->name);
		cmd->scsi_sense_reason = TCM_UNSUPPORTED_SCSI_OPCODE;
		return -ENOSYS;
	}

	dev->transport->do_sync_cache(task);
	return 0;
}

int target_emulate_noop(struct se_task *task)
{
	task->task_scsi_status = GOOD;
	transport_complete_task(task, 1);
	return 0;
}

/*
 * Write a CDB into @cdb that is based on the one the intiator sent us,
 * but updated to only cover the sectors that the current task handles.
 */
void target_get_task_cdb(struct se_task *task, unsigned char *cdb)
{
	struct se_cmd *cmd = task->task_se_cmd;
	unsigned int cdb_len = scsi_command_size(cmd->t_task_cdb);

	memcpy(cdb, cmd->t_task_cdb, cdb_len);
	if (cmd->se_cmd_flags & SCF_SCSI_DATA_SG_IO_CDB) {
		unsigned long long lba = task->task_lba;
		u32 sectors = task->task_sectors;

		switch (cdb_len) {
		case 6:
			/* 21-bit LBA and 8-bit sectors */
			cdb[1] = (lba >> 16) & 0x1f;
			cdb[2] = (lba >> 8) & 0xff;
			cdb[3] = lba & 0xff;
			cdb[4] = sectors & 0xff;
			break;
		case 10:
			/* 32-bit LBA and 16-bit sectors */
			put_unaligned_be32(lba, &cdb[2]);
			put_unaligned_be16(sectors, &cdb[7]);
			break;
		case 12:
			/* 32-bit LBA and 32-bit sectors */
			put_unaligned_be32(lba, &cdb[2]);
			put_unaligned_be32(sectors, &cdb[6]);
			break;
		case 16:
			/* 64-bit LBA and 32-bit sectors */
			put_unaligned_be64(lba, &cdb[2]);
			put_unaligned_be32(sectors, &cdb[10]);
			break;
		case 32:
			/* 64-bit LBA and 32-bit sectors, extended CDB */
			put_unaligned_be64(lba, &cdb[12]);
			put_unaligned_be32(sectors, &cdb[28]);
			break;
		default:
			BUG();
		}
	}
}
EXPORT_SYMBOL(target_get_task_cdb);
