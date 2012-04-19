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

#include <asm/unaligned.h>
#include <scsi/scsi.h>

#include <target/target_core_base.h>
#include <target/target_core_transport.h>
#include <target/target_core_fabric_ops.h>
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
	struct se_lun *lun = SE_LUN(cmd);
	struct se_device *dev = SE_DEV(cmd);
	unsigned char *buf = cmd->t_task->t_task_buf;

	/*
	 * Make sure we at least have 6 bytes of INQUIRY response
	 * payload going back for EVPD=0
	 */
	if (cmd->data_length < 6) {
		printk(KERN_ERR "SCSI Inquiry payload length: %u"
			" too small for EVPD=0\n", cmd->data_length);
		return -1;
	}

	buf[0] = dev->transport->get_device_type(dev);
	if (buf[0] == TYPE_TAPE)
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
	if (T10_ALUA(dev->se_sub_dev)->alua_type == SPC3_ALUA_EMULATED)
		target_fill_alua_data(lun->lun_sep, buf);

	if (cmd->data_length < 8) {
		buf[4] = 1; /* Set additional length to 1 */
		return 0;
	}

	buf[7] = 0x2; /* CmdQue=1 */

	/*
	 * Do not include vendor, product, reversion info in INQUIRY
	 * response payload for cdbs with a small allocation length.
	 */
	if (cmd->data_length < 36) {
		buf[4] = 3; /* Set additional length to 3 */
		return 0;
	}

	snprintf((unsigned char *)&buf[8], 8, "LIO-ORG");
	snprintf((unsigned char *)&buf[16], 16, "%s",
		 &DEV_T10_WWN(dev)->model[0]);
	snprintf((unsigned char *)&buf[32], 4, "%s",
		 &DEV_T10_WWN(dev)->revision[0]);
	buf[4] = 31; /* Set additional length to 31 */
	return 0;
}

/* supported vital product data pages */
static int
target_emulate_evpd_00(struct se_cmd *cmd, unsigned char *buf)
{
	buf[1] = 0x00;
	if (cmd->data_length < 8)
		return 0;

	buf[4] = 0x0;
	/*
	 * Only report the INQUIRY EVPD=1 pages after a valid NAA
	 * Registered Extended LUN WWN has been set via ConfigFS
	 * during device creation/restart.
	 */
	if (SE_DEV(cmd)->se_sub_dev->su_dev_flags &
			SDF_EMULATED_VPD_UNIT_SERIAL) {
		buf[3] = 3;
		buf[5] = 0x80;
		buf[6] = 0x83;
		buf[7] = 0x86;
	}

	return 0;
}

/* unit serial number */
static int
target_emulate_evpd_80(struct se_cmd *cmd, unsigned char *buf)
{
	struct se_device *dev = SE_DEV(cmd);
	u16 len = 0;

	buf[1] = 0x80;
	if (dev->se_sub_dev->su_dev_flags &
			SDF_EMULATED_VPD_UNIT_SERIAL) {
		u32 unit_serial_len;

		unit_serial_len =
			strlen(&DEV_T10_WWN(dev)->unit_serial[0]);
		unit_serial_len++; /* For NULL Terminator */

		if (((len + 4) + unit_serial_len) > cmd->data_length) {
			len += unit_serial_len;
			buf[2] = ((len >> 8) & 0xff);
			buf[3] = (len & 0xff);
			return 0;
		}
		len += sprintf((unsigned char *)&buf[4], "%s",
			&DEV_T10_WWN(dev)->unit_serial[0]);
		len++; /* Extra Byte for NULL Terminator */
		buf[3] = len;
	}
	return 0;
}

/*
 * Device identification VPD, for a complete list of
 * DESIGNATOR TYPEs see spc4r17 Table 459.
 */
static int
target_emulate_evpd_83(struct se_cmd *cmd, unsigned char *buf)
{
	struct se_device *dev = SE_DEV(cmd);
	struct se_lun *lun = SE_LUN(cmd);
	struct se_port *port = NULL;
	struct se_portal_group *tpg = NULL;
	struct t10_alua_lu_gp_member *lu_gp_mem;
	struct t10_alua_tg_pt_gp *tg_pt_gp;
	struct t10_alua_tg_pt_gp_member *tg_pt_gp_mem;
	unsigned char binary, binary_new;
	unsigned char *prod = &DEV_T10_WWN(dev)->model[0];
	u32 prod_len;
	u32 unit_serial_len, off = 0;
	int i;
	u16 len = 0, id_len;

	buf[1] = 0x83;
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

	/* Set ASSOICATION == addressed logical unit: 0)b */
	buf[off] = 0x00;

	/* Identifier/Designator type == NAA identifier */
	buf[off++] = 0x3;
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
	binary = transport_asciihex_to_binaryhex(
				&DEV_T10_WWN(dev)->unit_serial[0]);
	buf[off++] |= (binary & 0xf0) >> 4;
	for (i = 0; i < 24; i += 2) {
		binary_new = transport_asciihex_to_binaryhex(
			&DEV_T10_WWN(dev)->unit_serial[i+2]);
		buf[off] = (binary & 0x0f) << 4;
		buf[off++] |= (binary_new & 0xf0) >> 4;
		binary = binary_new;
	}
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
			strlen(&DEV_T10_WWN(dev)->unit_serial[0]);
		unit_serial_len++; /* For NULL Terminator */

		if ((len + (id_len + 4) +
		    (prod_len + unit_serial_len)) >
				cmd->data_length) {
			len += (prod_len + unit_serial_len);
			goto check_port;
		}
		id_len += sprintf((unsigned char *)&buf[off+12],
				"%s:%s", prod,
				&DEV_T10_WWN(dev)->unit_serial[0]);
	}
	buf[off] = 0x2; /* ASCII */
	buf[off+1] = 0x1; /* T10 Vendor ID */
	buf[off+2] = 0x0;
	memcpy((unsigned char *)&buf[off+4], "LIO-ORG", 8);
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
			(TPG_TFO(tpg)->get_fabric_proto_ident(tpg) << 4);
		buf[off++] |= 0x1; /* CODE SET == Binary */
		buf[off] = 0x80; /* Set PIV=1 */
		/* Set ASSOICATION == target port: 01b */
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
		if (T10_ALUA(dev->se_sub_dev)->alua_type !=
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
		if (!(tg_pt_gp)) {
			spin_unlock(&tg_pt_gp_mem->tg_pt_gp_mem_lock);
			goto check_lu_gp;
		}
		tg_pt_gp_id = tg_pt_gp->tg_pt_gp_id;
		spin_unlock(&tg_pt_gp_mem->tg_pt_gp_mem_lock);

		buf[off] =
			(TPG_TFO(tpg)->get_fabric_proto_ident(tpg) << 4);
		buf[off++] |= 0x1; /* CODE SET == Binary */
		buf[off] = 0x80; /* Set PIV=1 */
		/* Set ASSOICATION == target port: 01b */
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
		if (!(lu_gp_mem))
			goto check_scsi_name;

		spin_lock(&lu_gp_mem->lu_gp_mem_lock);
		lu_gp = lu_gp_mem->lu_gp;
		if (!(lu_gp)) {
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
		scsi_name_len = strlen(TPG_TFO(tpg)->tpg_get_wwn(tpg));
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
			(TPG_TFO(tpg)->get_fabric_proto_ident(tpg) << 4);
		buf[off++] |= 0x3; /* CODE SET == UTF-8 */
		buf[off] = 0x80; /* Set PIV=1 */
		/* Set ASSOICATION == target port: 01b */
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
		tpgt = TPG_TFO(tpg)->tpg_get_tag(tpg);
		scsi_name_len = sprintf(&buf[off], "%s,t,0x%04x",
					TPG_TFO(tpg)->tpg_get_wwn(tpg), tpgt);
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

	buf[1] = 0x86;
	buf[2] = 0x3c;
	/* Set HEADSUP, ORDSUP, SIMPSUP */
	buf[5] = 0x07;

	/* If WriteCache emulation is enabled, set V_SUP */
	if (DEV_ATTRIB(SE_DEV(cmd))->emulate_write_cache > 0)
		buf[6] = 0x01;
	return 0;
}

/* Block Limits VPD page */
static int
target_emulate_evpd_b0(struct se_cmd *cmd, unsigned char *buf)
{
	struct se_device *dev = SE_DEV(cmd);
	int have_tp = 0;

	/*
	 * Following sbc3r22 section 6.5.3 Block Limits VPD page, when
	 * emulate_tpu=1 or emulate_tpws=1 we will be expect a
	 * different page length for Thin Provisioning.
	 */
	if (DEV_ATTRIB(dev)->emulate_tpu || DEV_ATTRIB(dev)->emulate_tpws)
		have_tp = 1;

	if (cmd->data_length < (0x10 + 4)) {
		printk(KERN_INFO "Received data_length: %u"
			" too small for EVPD 0xb0\n",
			cmd->data_length);
		return -1;
	}

	if (have_tp && cmd->data_length < (0x3c + 4)) {
		printk(KERN_INFO "Received data_length: %u"
			" too small for TPE=1 EVPD 0xb0\n",
			cmd->data_length);
		have_tp = 0;
	}

	buf[0] = dev->transport->get_device_type(dev);
	buf[1] = 0xb0;
	buf[3] = have_tp ? 0x3c : 0x10;

	/*
	 * Set OPTIMAL TRANSFER LENGTH GRANULARITY
	 */
	put_unaligned_be16(1, &buf[6]);

	/*
	 * Set MAXIMUM TRANSFER LENGTH
	 */
	put_unaligned_be32(DEV_ATTRIB(dev)->max_sectors, &buf[8]);

	/*
	 * Set OPTIMAL TRANSFER LENGTH
	 */
	put_unaligned_be32(DEV_ATTRIB(dev)->optimal_sectors, &buf[12]);

	/*
	 * Exit now if we don't support TP or the initiator sent a too
	 * short buffer.
	 */
	if (!have_tp || cmd->data_length < (0x3c + 4))
		return 0;

	/*
	 * Set MAXIMUM UNMAP LBA COUNT
	 */
	put_unaligned_be32(DEV_ATTRIB(dev)->max_unmap_lba_count, &buf[20]);

	/*
	 * Set MAXIMUM UNMAP BLOCK DESCRIPTOR COUNT
	 */
	put_unaligned_be32(DEV_ATTRIB(dev)->max_unmap_block_desc_count,
			   &buf[24]);

	/*
	 * Set OPTIMAL UNMAP GRANULARITY
	 */
	put_unaligned_be32(DEV_ATTRIB(dev)->unmap_granularity, &buf[28]);

	/*
	 * UNMAP GRANULARITY ALIGNMENT
	 */
	put_unaligned_be32(DEV_ATTRIB(dev)->unmap_granularity_alignment,
			   &buf[32]);
	if (DEV_ATTRIB(dev)->unmap_granularity_alignment != 0)
		buf[32] |= 0x80; /* Set the UGAVALID bit */

	return 0;
}

/* Thin Provisioning VPD */
static int
target_emulate_evpd_b2(struct se_cmd *cmd, unsigned char *buf)
{
	struct se_device *dev = SE_DEV(cmd);

	/*
	 * From sbc3r22 section 6.5.4 Thin Provisioning VPD page:
	 *
	 * The PAGE LENGTH field is defined in SPC-4. If the DP bit is set to
	 * zero, then the page length shall be set to 0004h.  If the DP bit
	 * is set to one, then the page length shall be set to the value
	 * defined in table 162.
	 */
	buf[0] = dev->transport->get_device_type(dev);
	buf[1] = 0xb2;

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
	if (DEV_ATTRIB(dev)->emulate_tpu != 0)
		buf[5] = 0x80;

	/*
	 * A TPWS bit set to one indicates that the device server supports
	 * the use of the WRITE SAME (16) command (see 5.42) to unmap LBAs.
	 * A TPWS bit set to zero indicates that the device server does not
	 * support the use of the WRITE SAME (16) command to unmap LBAs.
	 */
	if (DEV_ATTRIB(dev)->emulate_tpws != 0)
		buf[5] |= 0x40;

	return 0;
}

static int
target_emulate_inquiry(struct se_cmd *cmd)
{
	struct se_device *dev = SE_DEV(cmd);
	unsigned char *buf = cmd->t_task->t_task_buf;
	unsigned char *cdb = cmd->t_task->t_task_cdb;

	if (!(cdb[1] & 0x1))
		return target_emulate_inquiry_std(cmd);

	/*
	 * Make sure we at least have 4 bytes of INQUIRY response
	 * payload for 0x00 going back for EVPD=1.  Note that 0x80
	 * and 0x83 will check for enough payload data length and
	 * jump to set_len: label when there is not enough inquiry EVPD
	 * payload length left for the next outgoing EVPD metadata
	 */
	if (cmd->data_length < 4) {
		printk(KERN_ERR "SCSI Inquiry payload length: %u"
			" too small for EVPD=1\n", cmd->data_length);
		return -1;
	}
	buf[0] = dev->transport->get_device_type(dev);

	switch (cdb[2]) {
	case 0x00:
		return target_emulate_evpd_00(cmd, buf);
	case 0x80:
		return target_emulate_evpd_80(cmd, buf);
	case 0x83:
		return target_emulate_evpd_83(cmd, buf);
	case 0x86:
		return target_emulate_evpd_86(cmd, buf);
	case 0xb0:
		return target_emulate_evpd_b0(cmd, buf);
	case 0xb2:
		return target_emulate_evpd_b2(cmd, buf);
	default:
		printk(KERN_ERR "Unknown VPD Code: 0x%02x\n", cdb[2]);
		return -1;
	}

	return 0;
}

static int
target_emulate_readcapacity(struct se_cmd *cmd)
{
	struct se_device *dev = SE_DEV(cmd);
	unsigned char *buf = cmd->t_task->t_task_buf;
	unsigned long long blocks_long = dev->transport->get_blocks(dev);
	u32 blocks;

	if (blocks_long >= 0x00000000ffffffff)
		blocks = 0xffffffff;
	else
		blocks = (u32)blocks_long;

	buf[0] = (blocks >> 24) & 0xff;
	buf[1] = (blocks >> 16) & 0xff;
	buf[2] = (blocks >> 8) & 0xff;
	buf[3] = blocks & 0xff;
	buf[4] = (DEV_ATTRIB(dev)->block_size >> 24) & 0xff;
	buf[5] = (DEV_ATTRIB(dev)->block_size >> 16) & 0xff;
	buf[6] = (DEV_ATTRIB(dev)->block_size >> 8) & 0xff;
	buf[7] = DEV_ATTRIB(dev)->block_size & 0xff;
	/*
	 * Set max 32-bit blocks to signal SERVICE ACTION READ_CAPACITY_16
	*/
	if (DEV_ATTRIB(dev)->emulate_tpu || DEV_ATTRIB(dev)->emulate_tpws)
		put_unaligned_be32(0xFFFFFFFF, &buf[0]);

	return 0;
}

static int
target_emulate_readcapacity_16(struct se_cmd *cmd)
{
	struct se_device *dev = SE_DEV(cmd);
	unsigned char *buf = cmd->t_task->t_task_buf;
	unsigned long long blocks = dev->transport->get_blocks(dev);

	buf[0] = (blocks >> 56) & 0xff;
	buf[1] = (blocks >> 48) & 0xff;
	buf[2] = (blocks >> 40) & 0xff;
	buf[3] = (blocks >> 32) & 0xff;
	buf[4] = (blocks >> 24) & 0xff;
	buf[5] = (blocks >> 16) & 0xff;
	buf[6] = (blocks >> 8) & 0xff;
	buf[7] = blocks & 0xff;
	buf[8] = (DEV_ATTRIB(dev)->block_size >> 24) & 0xff;
	buf[9] = (DEV_ATTRIB(dev)->block_size >> 16) & 0xff;
	buf[10] = (DEV_ATTRIB(dev)->block_size >> 8) & 0xff;
	buf[11] = DEV_ATTRIB(dev)->block_size & 0xff;
	/*
	 * Set Thin Provisioning Enable bit following sbc3r22 in section
	 * READ CAPACITY (16) byte 14 if emulate_tpu or emulate_tpws is enabled.
	 */
	if (DEV_ATTRIB(dev)->emulate_tpu || DEV_ATTRIB(dev)->emulate_tpws)
		buf[14] = 0x80;

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
	p[4] = (DEV_ATTRIB(dev)->emulate_ua_intlck_ctrl == 2) ? 0x30 :
	       (DEV_ATTRIB(dev)->emulate_ua_intlck_ctrl == 1) ? 0x20 : 0x00;
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
	p[5] = (DEV_ATTRIB(dev)->emulate_tas) ? 0x40 : 0x00;
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
	if (DEV_ATTRIB(dev)->emulate_write_cache > 0)
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

static int
target_emulate_modesense(struct se_cmd *cmd, int ten)
{
	struct se_device *dev = SE_DEV(cmd);
	char *cdb = cmd->t_task->t_task_cdb;
	unsigned char *rbuf = cmd->t_task->t_task_buf;
	int type = dev->transport->get_device_type(dev);
	int offset = (ten) ? 8 : 4;
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
		printk(KERN_ERR "Got Unknown Mode Page: 0x%02x\n",
				cdb[2] & 0x3f);
		return PYX_TRANSPORT_UNKNOWN_MODE_PAGE;
	}
	offset += length;

	if (ten) {
		offset -= 2;
		buf[0] = (offset >> 8) & 0xff;
		buf[1] = offset & 0xff;

		if ((SE_LUN(cmd)->lun_access & TRANSPORT_LUNFLAGS_READ_ONLY) ||
		    (cmd->se_deve &&
		    (cmd->se_deve->lun_flags & TRANSPORT_LUNFLAGS_READ_ONLY)))
			target_modesense_write_protect(&buf[3], type);

		if ((DEV_ATTRIB(dev)->emulate_write_cache > 0) &&
		    (DEV_ATTRIB(dev)->emulate_fua_write > 0))
			target_modesense_dpofua(&buf[3], type);

		if ((offset + 2) > cmd->data_length)
			offset = cmd->data_length;

	} else {
		offset -= 1;
		buf[0] = offset & 0xff;

		if ((SE_LUN(cmd)->lun_access & TRANSPORT_LUNFLAGS_READ_ONLY) ||
		    (cmd->se_deve &&
		    (cmd->se_deve->lun_flags & TRANSPORT_LUNFLAGS_READ_ONLY)))
			target_modesense_write_protect(&buf[2], type);

		if ((DEV_ATTRIB(dev)->emulate_write_cache > 0) &&
		    (DEV_ATTRIB(dev)->emulate_fua_write > 0))
			target_modesense_dpofua(&buf[2], type);

		if ((offset + 1) > cmd->data_length)
			offset = cmd->data_length;
	}
	memcpy(rbuf, buf, offset);

	return 0;
}

static int
target_emulate_request_sense(struct se_cmd *cmd)
{
	unsigned char *cdb = cmd->t_task->t_task_cdb;
	unsigned char *buf = cmd->t_task->t_task_buf;
	u8 ua_asc = 0, ua_ascq = 0;

	if (cdb[1] & 0x01) {
		printk(KERN_ERR "REQUEST_SENSE description emulation not"
			" supported\n");
		return PYX_TRANSPORT_INVALID_CDB_FIELD;
	}
	if (!(core_scsi3_ua_clear_for_request_sense(cmd, &ua_asc, &ua_ascq))) {
		/*
		 * CURRENT ERROR, UNIT ATTENTION
		 */
		buf[0] = 0x70;
		buf[SPC_SENSE_KEY_OFFSET] = UNIT_ATTENTION;
		/*
		 * Make sure request data length is enough for additional
		 * sense data.
		 */
		if (cmd->data_length <= 18) {
			buf[7] = 0x00;
			return 0;
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
		/*
		 * Make sure request data length is enough for additional
		 * sense data.
		 */
		if (cmd->data_length <= 18) {
			buf[7] = 0x00;
			return 0;
		}
		/*
		 * NO ADDITIONAL SENSE INFORMATION
		 */
		buf[SPC_ASC_KEY_OFFSET] = 0x00;
		buf[7] = 0x0A;
	}

	return 0;
}

/*
 * Used for TCM/IBLOCK and TCM/FILEIO for block/blk-lib.c level discard support.
 * Note this is not used for TCM/pSCSI passthrough
 */
static int
target_emulate_unmap(struct se_task *task)
{
	struct se_cmd *cmd = TASK_CMD(task);
	struct se_device *dev = SE_DEV(cmd);
	unsigned char *buf = cmd->t_task->t_task_buf, *ptr = NULL;
	unsigned char *cdb = &cmd->t_task->t_task_cdb[0];
	sector_t lba;
	unsigned int size = cmd->data_length, range;
	int ret, offset;
	unsigned short dl, bd_dl;

	/* First UNMAP block descriptor starts at 8 byte offset */
	offset = 8;
	size -= 8;
	dl = get_unaligned_be16(&cdb[0]);
	bd_dl = get_unaligned_be16(&cdb[2]);
	ptr = &buf[offset];
	printk(KERN_INFO "UNMAP: Sub: %s Using dl: %hu bd_dl: %hu size: %hu"
		" ptr: %p\n", dev->transport->name, dl, bd_dl, size, ptr);

	while (size) {
		lba = get_unaligned_be64(&ptr[0]);
		range = get_unaligned_be32(&ptr[8]);
		printk(KERN_INFO "UNMAP: Using lba: %llu and range: %u\n",
				 (unsigned long long)lba, range);

		ret = dev->transport->do_discard(dev, lba, range);
		if (ret < 0) {
			printk(KERN_ERR "blkdev_issue_discard() failed: %d\n",
					ret);
			return -1;
		}

		ptr += 16;
		size -= 16;
	}

	task->task_scsi_status = GOOD;
	transport_complete_task(task, 1);
	return 0;
}

/*
 * Used for TCM/IBLOCK and TCM/FILEIO for block/blk-lib.c level discard support.
 * Note this is not used for TCM/pSCSI passthrough
 */
static int
target_emulate_write_same(struct se_task *task)
{
	struct se_cmd *cmd = TASK_CMD(task);
	struct se_device *dev = SE_DEV(cmd);
	sector_t lba = cmd->t_task->t_task_lba;
	unsigned int range;
	int ret;

	range = (cmd->data_length / DEV_ATTRIB(dev)->block_size);

	printk(KERN_INFO "WRITE_SAME UNMAP: LBA: %llu Range: %u\n",
			 (unsigned long long)lba, range);

	ret = dev->transport->do_discard(dev, lba, range);
	if (ret < 0) {
		printk(KERN_INFO "blkdev_issue_discard() failed for WRITE_SAME\n");
		return -1;
	}

	task->task_scsi_status = GOOD;
	transport_complete_task(task, 1);
	return 0;
}

int
transport_emulate_control_cdb(struct se_task *task)
{
	struct se_cmd *cmd = TASK_CMD(task);
	struct se_device *dev = SE_DEV(cmd);
	unsigned short service_action;
	int ret = 0;

	switch (cmd->t_task->t_task_cdb[0]) {
	case INQUIRY:
		ret = target_emulate_inquiry(cmd);
		break;
	case READ_CAPACITY:
		ret = target_emulate_readcapacity(cmd);
		break;
	case MODE_SENSE:
		ret = target_emulate_modesense(cmd, 0);
		break;
	case MODE_SENSE_10:
		ret = target_emulate_modesense(cmd, 1);
		break;
	case SERVICE_ACTION_IN:
		switch (cmd->t_task->t_task_cdb[1] & 0x1f) {
		case SAI_READ_CAPACITY_16:
			ret = target_emulate_readcapacity_16(cmd);
			break;
		default:
			printk(KERN_ERR "Unsupported SA: 0x%02x\n",
				cmd->t_task->t_task_cdb[1] & 0x1f);
			return PYX_TRANSPORT_UNKNOWN_SAM_OPCODE;
		}
		break;
	case REQUEST_SENSE:
		ret = target_emulate_request_sense(cmd);
		break;
	case UNMAP:
		if (!dev->transport->do_discard) {
			printk(KERN_ERR "UNMAP emulation not supported for: %s\n",
					dev->transport->name);
			return PYX_TRANSPORT_UNKNOWN_SAM_OPCODE;
		}
		ret = target_emulate_unmap(task);
		break;
	case WRITE_SAME_16:
		if (!dev->transport->do_discard) {
			printk(KERN_ERR "WRITE_SAME_16 emulation not supported"
					" for: %s\n", dev->transport->name);
			return PYX_TRANSPORT_UNKNOWN_SAM_OPCODE;
		}
		ret = target_emulate_write_same(task);
		break;
	case VARIABLE_LENGTH_CMD:
		service_action =
			get_unaligned_be16(&cmd->t_task->t_task_cdb[8]);
		switch (service_action) {
		case WRITE_SAME_32:
			if (!dev->transport->do_discard) {
				printk(KERN_ERR "WRITE_SAME_32 SA emulation not"
					" supported for: %s\n",
					dev->transport->name);
				return PYX_TRANSPORT_UNKNOWN_SAM_OPCODE;
			}
			ret = target_emulate_write_same(task);
			break;
		default:
			printk(KERN_ERR "Unsupported VARIABLE_LENGTH_CMD SA:"
					" 0x%02x\n", service_action);
			break;
		}
		break;
	case SYNCHRONIZE_CACHE:
	case 0x91: /* SYNCHRONIZE_CACHE_16: */
		if (!dev->transport->do_sync_cache) {
			printk(KERN_ERR
				"SYNCHRONIZE_CACHE emulation not supported"
				" for: %s\n", dev->transport->name);
			return PYX_TRANSPORT_UNKNOWN_SAM_OPCODE;
		}
		dev->transport->do_sync_cache(task);
		break;
	case ALLOW_MEDIUM_REMOVAL:
	case ERASE:
	case REZERO_UNIT:
	case SEEK_10:
	case SPACE:
	case START_STOP:
	case TEST_UNIT_READY:
	case VERIFY:
	case WRITE_FILEMARKS:
		break;
	default:
		printk(KERN_ERR "Unsupported SCSI Opcode: 0x%02x for %s\n",
			cmd->t_task->t_task_cdb[0], dev->transport->name);
		return PYX_TRANSPORT_UNKNOWN_SAM_OPCODE;
	}

	if (ret < 0)
		return ret;
	task->task_scsi_status = GOOD;
	transport_complete_task(task, 1);

	return PYX_TRANSPORT_SENT_TO_TRANSPORT;
}
