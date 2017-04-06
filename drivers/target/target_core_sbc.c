/*
 * SCSI Block Commands (SBC) parsing and emulation.
 *
 * (c) Copyright 2002-2013 Datera, Inc.
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
#include <linux/ratelimit.h>
#include <linux/crc-t10dif.h>
#include <linux/t10-pi.h>
#include <asm/unaligned.h>
#include <scsi/scsi_proto.h>
#include <scsi/scsi_tcq.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>
#include <target/target_core_fabric.h>

#include "target_core_internal.h"
#include "target_core_ua.h"
#include "target_core_alua.h"

static sense_reason_t
sbc_check_prot(struct se_device *, struct se_cmd *, unsigned char *, u32, bool);
static sense_reason_t sbc_execute_unmap(struct se_cmd *cmd);

static sense_reason_t
sbc_emulate_readcapacity(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	unsigned char *cdb = cmd->t_task_cdb;
	unsigned long long blocks_long = dev->transport->get_blocks(dev);
	unsigned char *rbuf;
	unsigned char buf[8];
	u32 blocks;

	/*
	 * SBC-2 says:
	 *   If the PMI bit is set to zero and the LOGICAL BLOCK
	 *   ADDRESS field is not set to zero, the device server shall
	 *   terminate the command with CHECK CONDITION status with
	 *   the sense key set to ILLEGAL REQUEST and the additional
	 *   sense code set to INVALID FIELD IN CDB.
	 *
	 * In SBC-3, these fields are obsolete, but some SCSI
	 * compliance tests actually check this, so we might as well
	 * follow SBC-2.
	 */
	if (!(cdb[8] & 1) && !!(cdb[2] | cdb[3] | cdb[4] | cdb[5]))
		return TCM_INVALID_CDB_FIELD;

	if (blocks_long >= 0x00000000ffffffff)
		blocks = 0xffffffff;
	else
		blocks = (u32)blocks_long;

	buf[0] = (blocks >> 24) & 0xff;
	buf[1] = (blocks >> 16) & 0xff;
	buf[2] = (blocks >> 8) & 0xff;
	buf[3] = blocks & 0xff;
	buf[4] = (dev->dev_attrib.block_size >> 24) & 0xff;
	buf[5] = (dev->dev_attrib.block_size >> 16) & 0xff;
	buf[6] = (dev->dev_attrib.block_size >> 8) & 0xff;
	buf[7] = dev->dev_attrib.block_size & 0xff;

	rbuf = transport_kmap_data_sg(cmd);
	if (rbuf) {
		memcpy(rbuf, buf, min_t(u32, sizeof(buf), cmd->data_length));
		transport_kunmap_data_sg(cmd);
	}

	target_complete_cmd_with_length(cmd, GOOD, 8);
	return 0;
}

static sense_reason_t
sbc_emulate_readcapacity_16(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct se_session *sess = cmd->se_sess;
	int pi_prot_type = dev->dev_attrib.pi_prot_type;

	unsigned char *rbuf;
	unsigned char buf[32];
	unsigned long long blocks = dev->transport->get_blocks(dev);

	memset(buf, 0, sizeof(buf));
	buf[0] = (blocks >> 56) & 0xff;
	buf[1] = (blocks >> 48) & 0xff;
	buf[2] = (blocks >> 40) & 0xff;
	buf[3] = (blocks >> 32) & 0xff;
	buf[4] = (blocks >> 24) & 0xff;
	buf[5] = (blocks >> 16) & 0xff;
	buf[6] = (blocks >> 8) & 0xff;
	buf[7] = blocks & 0xff;
	buf[8] = (dev->dev_attrib.block_size >> 24) & 0xff;
	buf[9] = (dev->dev_attrib.block_size >> 16) & 0xff;
	buf[10] = (dev->dev_attrib.block_size >> 8) & 0xff;
	buf[11] = dev->dev_attrib.block_size & 0xff;
	/*
	 * Set P_TYPE and PROT_EN bits for DIF support
	 */
	if (sess->sup_prot_ops & (TARGET_PROT_DIN_PASS | TARGET_PROT_DOUT_PASS)) {
		/*
		 * Only override a device's pi_prot_type if no T10-PI is
		 * available, and sess_prot_type has been explicitly enabled.
		 */
		if (!pi_prot_type)
			pi_prot_type = sess->sess_prot_type;

		if (pi_prot_type)
			buf[12] = (pi_prot_type - 1) << 1 | 0x1;
	}

	if (dev->transport->get_lbppbe)
		buf[13] = dev->transport->get_lbppbe(dev) & 0x0f;

	if (dev->transport->get_alignment_offset_lbas) {
		u16 lalba = dev->transport->get_alignment_offset_lbas(dev);
		buf[14] = (lalba >> 8) & 0x3f;
		buf[15] = lalba & 0xff;
	}

	/*
	 * Set Thin Provisioning Enable bit following sbc3r22 in section
	 * READ CAPACITY (16) byte 14 if emulate_tpu or emulate_tpws is enabled.
	 */
	if (dev->dev_attrib.emulate_tpu || dev->dev_attrib.emulate_tpws) {
		buf[14] |= 0x80;

		/*
		 * LBPRZ signifies that zeroes will be read back from an LBA after
		 * an UNMAP or WRITE SAME w/ unmap bit (sbc3r36 5.16.2)
		 */
		if (dev->dev_attrib.unmap_zeroes_data)
			buf[14] |= 0x40;
	}

	rbuf = transport_kmap_data_sg(cmd);
	if (rbuf) {
		memcpy(rbuf, buf, min_t(u32, sizeof(buf), cmd->data_length));
		transport_kunmap_data_sg(cmd);
	}

	target_complete_cmd_with_length(cmd, GOOD, 32);
	return 0;
}

static sense_reason_t
sbc_emulate_startstop(struct se_cmd *cmd)
{
	unsigned char *cdb = cmd->t_task_cdb;

	/*
	 * See sbc3r36 section 5.25
	 * Immediate bit should be set since there is nothing to complete
	 * POWER CONDITION MODIFIER 0h
	 */
	if (!(cdb[1] & 1) || cdb[2] || cdb[3])
		return TCM_INVALID_CDB_FIELD;

	/*
	 * See sbc3r36 section 5.25
	 * POWER CONDITION 0h START_VALID - process START and LOEJ
	 */
	if (cdb[4] >> 4 & 0xf)
		return TCM_INVALID_CDB_FIELD;

	/*
	 * See sbc3r36 section 5.25
	 * LOEJ 0h - nothing to load or unload
	 * START 1h - we are ready
	 */
	if (!(cdb[4] & 1) || (cdb[4] & 2) || (cdb[4] & 4))
		return TCM_INVALID_CDB_FIELD;

	target_complete_cmd(cmd, SAM_STAT_GOOD);
	return 0;
}

sector_t sbc_get_write_same_sectors(struct se_cmd *cmd)
{
	u32 num_blocks;

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
	if (num_blocks)
		return num_blocks;

	return cmd->se_dev->transport->get_blocks(cmd->se_dev) -
		cmd->t_task_lba + 1;
}
EXPORT_SYMBOL(sbc_get_write_same_sectors);

static sense_reason_t
sbc_execute_write_same_unmap(struct se_cmd *cmd)
{
	struct sbc_ops *ops = cmd->protocol_data;
	sector_t nolb = sbc_get_write_same_sectors(cmd);
	sense_reason_t ret;

	if (nolb) {
		ret = ops->execute_unmap(cmd, cmd->t_task_lba, nolb);
		if (ret)
			return ret;
	}

	target_complete_cmd(cmd, GOOD);
	return 0;
}

static sense_reason_t
sbc_emulate_noop(struct se_cmd *cmd)
{
	target_complete_cmd(cmd, GOOD);
	return 0;
}

static inline u32 sbc_get_size(struct se_cmd *cmd, u32 sectors)
{
	return cmd->se_dev->dev_attrib.block_size * sectors;
}

static inline u32 transport_get_sectors_6(unsigned char *cdb)
{
	/*
	 * Use 8-bit sector value.  SBC-3 says:
	 *
	 *   A TRANSFER LENGTH field set to zero specifies that 256
	 *   logical blocks shall be written.  Any other value
	 *   specifies the number of logical blocks that shall be
	 *   written.
	 */
	return cdb[4] ? : 256;
}

static inline u32 transport_get_sectors_10(unsigned char *cdb)
{
	return (u32)(cdb[7] << 8) + cdb[8];
}

static inline u32 transport_get_sectors_12(unsigned char *cdb)
{
	return (u32)(cdb[6] << 24) + (cdb[7] << 16) + (cdb[8] << 8) + cdb[9];
}

static inline u32 transport_get_sectors_16(unsigned char *cdb)
{
	return (u32)(cdb[10] << 24) + (cdb[11] << 16) +
		    (cdb[12] << 8) + cdb[13];
}

/*
 * Used for VARIABLE_LENGTH_CDB WRITE_32 and READ_32 variants
 */
static inline u32 transport_get_sectors_32(unsigned char *cdb)
{
	return (u32)(cdb[28] << 24) + (cdb[29] << 16) +
		    (cdb[30] << 8) + cdb[31];

}

static inline u32 transport_lba_21(unsigned char *cdb)
{
	return ((cdb[1] & 0x1f) << 16) | (cdb[2] << 8) | cdb[3];
}

static inline u32 transport_lba_32(unsigned char *cdb)
{
	return (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
}

static inline unsigned long long transport_lba_64(unsigned char *cdb)
{
	unsigned int __v1, __v2;

	__v1 = (cdb[2] << 24) | (cdb[3] << 16) | (cdb[4] << 8) | cdb[5];
	__v2 = (cdb[6] << 24) | (cdb[7] << 16) | (cdb[8] << 8) | cdb[9];

	return ((unsigned long long)__v2) | (unsigned long long)__v1 << 32;
}

/*
 * For VARIABLE_LENGTH_CDB w/ 32 byte extended CDBs
 */
static inline unsigned long long transport_lba_64_ext(unsigned char *cdb)
{
	unsigned int __v1, __v2;

	__v1 = (cdb[12] << 24) | (cdb[13] << 16) | (cdb[14] << 8) | cdb[15];
	__v2 = (cdb[16] << 24) | (cdb[17] << 16) | (cdb[18] << 8) | cdb[19];

	return ((unsigned long long)__v2) | (unsigned long long)__v1 << 32;
}

static sense_reason_t
sbc_setup_write_same(struct se_cmd *cmd, unsigned char *flags, struct sbc_ops *ops)
{
	struct se_device *dev = cmd->se_dev;
	sector_t end_lba = dev->transport->get_blocks(dev) + 1;
	unsigned int sectors = sbc_get_write_same_sectors(cmd);
	sense_reason_t ret;

	if ((flags[0] & 0x04) || (flags[0] & 0x02)) {
		pr_err("WRITE_SAME PBDATA and LBDATA"
			" bits not supported for Block Discard"
			" Emulation\n");
		return TCM_UNSUPPORTED_SCSI_OPCODE;
	}
	if (sectors > cmd->se_dev->dev_attrib.max_write_same_len) {
		pr_warn("WRITE_SAME sectors: %u exceeds max_write_same_len: %u\n",
			sectors, cmd->se_dev->dev_attrib.max_write_same_len);
		return TCM_INVALID_CDB_FIELD;
	}
	/*
	 * Sanity check for LBA wrap and request past end of device.
	 */
	if (((cmd->t_task_lba + sectors) < cmd->t_task_lba) ||
	    ((cmd->t_task_lba + sectors) > end_lba)) {
		pr_err("WRITE_SAME exceeds last lba %llu (lba %llu, sectors %u)\n",
		       (unsigned long long)end_lba, cmd->t_task_lba, sectors);
		return TCM_ADDRESS_OUT_OF_RANGE;
	}

	/* We always have ANC_SUP == 0 so setting ANCHOR is always an error */
	if (flags[0] & 0x10) {
		pr_warn("WRITE SAME with ANCHOR not supported\n");
		return TCM_INVALID_CDB_FIELD;
	}
	/*
	 * Special case for WRITE_SAME w/ UNMAP=1 that ends up getting
	 * translated into block discard requests within backend code.
	 */
	if (flags[0] & 0x08) {
		if (!ops->execute_unmap)
			return TCM_UNSUPPORTED_SCSI_OPCODE;

		if (!dev->dev_attrib.emulate_tpws) {
			pr_err("Got WRITE_SAME w/ UNMAP=1, but backend device"
			       " has emulate_tpws disabled\n");
			return TCM_UNSUPPORTED_SCSI_OPCODE;
		}
		cmd->execute_cmd = sbc_execute_write_same_unmap;
		return 0;
	}
	if (!ops->execute_write_same)
		return TCM_UNSUPPORTED_SCSI_OPCODE;

	ret = sbc_check_prot(dev, cmd, &cmd->t_task_cdb[0], sectors, true);
	if (ret)
		return ret;

	cmd->execute_cmd = ops->execute_write_same;
	return 0;
}

static sense_reason_t xdreadwrite_callback(struct se_cmd *cmd, bool success,
					   int *post_ret)
{
	unsigned char *buf, *addr;
	struct scatterlist *sg;
	unsigned int offset;
	sense_reason_t ret = TCM_NO_SENSE;
	int i, count;
	/*
	 * From sbc3r22.pdf section 5.48 XDWRITEREAD (10) command
	 *
	 * 1) read the specified logical block(s);
	 * 2) transfer logical blocks from the data-out buffer;
	 * 3) XOR the logical blocks transferred from the data-out buffer with
	 *    the logical blocks read, storing the resulting XOR data in a buffer;
	 * 4) if the DISABLE WRITE bit is set to zero, then write the logical
	 *    blocks transferred from the data-out buffer; and
	 * 5) transfer the resulting XOR data to the data-in buffer.
	 */
	buf = kmalloc(cmd->data_length, GFP_KERNEL);
	if (!buf) {
		pr_err("Unable to allocate xor_callback buf\n");
		return TCM_OUT_OF_RESOURCES;
	}
	/*
	 * Copy the scatterlist WRITE buffer located at cmd->t_data_sg
	 * into the locally allocated *buf
	 */
	sg_copy_to_buffer(cmd->t_data_sg,
			  cmd->t_data_nents,
			  buf,
			  cmd->data_length);

	/*
	 * Now perform the XOR against the BIDI read memory located at
	 * cmd->t_mem_bidi_list
	 */

	offset = 0;
	for_each_sg(cmd->t_bidi_data_sg, sg, cmd->t_bidi_data_nents, count) {
		addr = kmap_atomic(sg_page(sg));
		if (!addr) {
			ret = TCM_OUT_OF_RESOURCES;
			goto out;
		}

		for (i = 0; i < sg->length; i++)
			*(addr + sg->offset + i) ^= *(buf + offset + i);

		offset += sg->length;
		kunmap_atomic(addr);
	}

out:
	kfree(buf);
	return ret;
}

static sense_reason_t
sbc_execute_rw(struct se_cmd *cmd)
{
	struct sbc_ops *ops = cmd->protocol_data;

	return ops->execute_rw(cmd, cmd->t_data_sg, cmd->t_data_nents,
			       cmd->data_direction);
}

static sense_reason_t compare_and_write_post(struct se_cmd *cmd, bool success,
					     int *post_ret)
{
	struct se_device *dev = cmd->se_dev;
	sense_reason_t ret = TCM_NO_SENSE;

	/*
	 * Only set SCF_COMPARE_AND_WRITE_POST to force a response fall-through
	 * within target_complete_ok_work() if the command was successfully
	 * sent to the backend driver.
	 */
	spin_lock_irq(&cmd->t_state_lock);
	if (cmd->transport_state & CMD_T_SENT) {
		cmd->se_cmd_flags |= SCF_COMPARE_AND_WRITE_POST;
		*post_ret = 1;

		if (cmd->scsi_status == SAM_STAT_CHECK_CONDITION)
			ret = TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	spin_unlock_irq(&cmd->t_state_lock);

	/*
	 * Unlock ->caw_sem originally obtained during sbc_compare_and_write()
	 * before the original READ I/O submission.
	 */
	up(&dev->caw_sem);

	return ret;
}

static sense_reason_t compare_and_write_callback(struct se_cmd *cmd, bool success,
						 int *post_ret)
{
	struct se_device *dev = cmd->se_dev;
	struct scatterlist *write_sg = NULL, *sg;
	unsigned char *buf = NULL, *addr;
	struct sg_mapping_iter m;
	unsigned int offset = 0, len;
	unsigned int nlbas = cmd->t_task_nolb;
	unsigned int block_size = dev->dev_attrib.block_size;
	unsigned int compare_len = (nlbas * block_size);
	sense_reason_t ret = TCM_NO_SENSE;
	int rc, i;

	/*
	 * Handle early failure in transport_generic_request_failure(),
	 * which will not have taken ->caw_sem yet..
	 */
	if (!success && (!cmd->t_data_sg || !cmd->t_bidi_data_sg))
		return TCM_NO_SENSE;
	/*
	 * Handle special case for zero-length COMPARE_AND_WRITE
	 */
	if (!cmd->data_length)
		goto out;
	/*
	 * Immediately exit + release dev->caw_sem if command has already
	 * been failed with a non-zero SCSI status.
	 */
	if (cmd->scsi_status) {
		pr_err("compare_and_write_callback: non zero scsi_status:"
			" 0x%02x\n", cmd->scsi_status);
		goto out;
	}

	buf = kzalloc(cmd->data_length, GFP_KERNEL);
	if (!buf) {
		pr_err("Unable to allocate compare_and_write buf\n");
		ret = TCM_OUT_OF_RESOURCES;
		goto out;
	}

	write_sg = kmalloc(sizeof(struct scatterlist) * cmd->t_data_nents,
			   GFP_KERNEL);
	if (!write_sg) {
		pr_err("Unable to allocate compare_and_write sg\n");
		ret = TCM_OUT_OF_RESOURCES;
		goto out;
	}
	sg_init_table(write_sg, cmd->t_data_nents);
	/*
	 * Setup verify and write data payloads from total NumberLBAs.
	 */
	rc = sg_copy_to_buffer(cmd->t_data_sg, cmd->t_data_nents, buf,
			       cmd->data_length);
	if (!rc) {
		pr_err("sg_copy_to_buffer() failed for compare_and_write\n");
		ret = TCM_OUT_OF_RESOURCES;
		goto out;
	}
	/*
	 * Compare against SCSI READ payload against verify payload
	 */
	for_each_sg(cmd->t_bidi_data_sg, sg, cmd->t_bidi_data_nents, i) {
		addr = (unsigned char *)kmap_atomic(sg_page(sg));
		if (!addr) {
			ret = TCM_OUT_OF_RESOURCES;
			goto out;
		}

		len = min(sg->length, compare_len);

		if (memcmp(addr, buf + offset, len)) {
			pr_warn("Detected MISCOMPARE for addr: %p buf: %p\n",
				addr, buf + offset);
			kunmap_atomic(addr);
			goto miscompare;
		}
		kunmap_atomic(addr);

		offset += len;
		compare_len -= len;
		if (!compare_len)
			break;
	}

	i = 0;
	len = cmd->t_task_nolb * block_size;
	sg_miter_start(&m, cmd->t_data_sg, cmd->t_data_nents, SG_MITER_TO_SG);
	/*
	 * Currently assumes NoLB=1 and SGLs are PAGE_SIZE..
	 */
	while (len) {
		sg_miter_next(&m);

		if (block_size < PAGE_SIZE) {
			sg_set_page(&write_sg[i], m.page, block_size,
				    m.piter.sg->offset + block_size);
		} else {
			sg_miter_next(&m);
			sg_set_page(&write_sg[i], m.page, block_size,
				    m.piter.sg->offset);
		}
		len -= block_size;
		i++;
	}
	sg_miter_stop(&m);
	/*
	 * Save the original SGL + nents values before updating to new
	 * assignments, to be released in transport_free_pages() ->
	 * transport_reset_sgl_orig()
	 */
	cmd->t_data_sg_orig = cmd->t_data_sg;
	cmd->t_data_sg = write_sg;
	cmd->t_data_nents_orig = cmd->t_data_nents;
	cmd->t_data_nents = 1;

	cmd->sam_task_attr = TCM_HEAD_TAG;
	cmd->transport_complete_callback = compare_and_write_post;
	/*
	 * Now reset ->execute_cmd() to the normal sbc_execute_rw() handler
	 * for submitting the adjusted SGL to write instance user-data.
	 */
	cmd->execute_cmd = sbc_execute_rw;

	spin_lock_irq(&cmd->t_state_lock);
	cmd->t_state = TRANSPORT_PROCESSING;
	cmd->transport_state |= CMD_T_ACTIVE | CMD_T_SENT;
	spin_unlock_irq(&cmd->t_state_lock);

	__target_execute_cmd(cmd, false);

	kfree(buf);
	return ret;

miscompare:
	pr_warn("Target/%s: Send MISCOMPARE check condition and sense\n",
		dev->transport->name);
	ret = TCM_MISCOMPARE_VERIFY;
out:
	/*
	 * In the MISCOMPARE or failure case, unlock ->caw_sem obtained in
	 * sbc_compare_and_write() before the original READ I/O submission.
	 */
	up(&dev->caw_sem);
	kfree(write_sg);
	kfree(buf);
	return ret;
}

static sense_reason_t
sbc_compare_and_write(struct se_cmd *cmd)
{
	struct sbc_ops *ops = cmd->protocol_data;
	struct se_device *dev = cmd->se_dev;
	sense_reason_t ret;
	int rc;
	/*
	 * Submit the READ first for COMPARE_AND_WRITE to perform the
	 * comparision using SGLs at cmd->t_bidi_data_sg..
	 */
	rc = down_interruptible(&dev->caw_sem);
	if (rc != 0) {
		cmd->transport_complete_callback = NULL;
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	/*
	 * Reset cmd->data_length to individual block_size in order to not
	 * confuse backend drivers that depend on this value matching the
	 * size of the I/O being submitted.
	 */
	cmd->data_length = cmd->t_task_nolb * dev->dev_attrib.block_size;

	ret = ops->execute_rw(cmd, cmd->t_bidi_data_sg, cmd->t_bidi_data_nents,
			      DMA_FROM_DEVICE);
	if (ret) {
		cmd->transport_complete_callback = NULL;
		up(&dev->caw_sem);
		return ret;
	}
	/*
	 * Unlock of dev->caw_sem to occur in compare_and_write_callback()
	 * upon MISCOMPARE, or in compare_and_write_done() upon completion
	 * of WRITE instance user-data.
	 */
	return TCM_NO_SENSE;
}

static int
sbc_set_prot_op_checks(u8 protect, bool fabric_prot, enum target_prot_type prot_type,
		       bool is_write, struct se_cmd *cmd)
{
	if (is_write) {
		cmd->prot_op = fabric_prot ? TARGET_PROT_DOUT_STRIP :
			       protect ? TARGET_PROT_DOUT_PASS :
			       TARGET_PROT_DOUT_INSERT;
		switch (protect) {
		case 0x0:
		case 0x3:
			cmd->prot_checks = 0;
			break;
		case 0x1:
		case 0x5:
			cmd->prot_checks = TARGET_DIF_CHECK_GUARD;
			if (prot_type == TARGET_DIF_TYPE1_PROT)
				cmd->prot_checks |= TARGET_DIF_CHECK_REFTAG;
			break;
		case 0x2:
			if (prot_type == TARGET_DIF_TYPE1_PROT)
				cmd->prot_checks = TARGET_DIF_CHECK_REFTAG;
			break;
		case 0x4:
			cmd->prot_checks = TARGET_DIF_CHECK_GUARD;
			break;
		default:
			pr_err("Unsupported protect field %d\n", protect);
			return -EINVAL;
		}
	} else {
		cmd->prot_op = fabric_prot ? TARGET_PROT_DIN_INSERT :
			       protect ? TARGET_PROT_DIN_PASS :
			       TARGET_PROT_DIN_STRIP;
		switch (protect) {
		case 0x0:
		case 0x1:
		case 0x5:
			cmd->prot_checks = TARGET_DIF_CHECK_GUARD;
			if (prot_type == TARGET_DIF_TYPE1_PROT)
				cmd->prot_checks |= TARGET_DIF_CHECK_REFTAG;
			break;
		case 0x2:
			if (prot_type == TARGET_DIF_TYPE1_PROT)
				cmd->prot_checks = TARGET_DIF_CHECK_REFTAG;
			break;
		case 0x3:
			cmd->prot_checks = 0;
			break;
		case 0x4:
			cmd->prot_checks = TARGET_DIF_CHECK_GUARD;
			break;
		default:
			pr_err("Unsupported protect field %d\n", protect);
			return -EINVAL;
		}
	}

	return 0;
}

static sense_reason_t
sbc_check_prot(struct se_device *dev, struct se_cmd *cmd, unsigned char *cdb,
	       u32 sectors, bool is_write)
{
	u8 protect = cdb[1] >> 5;
	int sp_ops = cmd->se_sess->sup_prot_ops;
	int pi_prot_type = dev->dev_attrib.pi_prot_type;
	bool fabric_prot = false;

	if (!cmd->t_prot_sg || !cmd->t_prot_nents) {
		if (unlikely(protect &&
		    !dev->dev_attrib.pi_prot_type && !cmd->se_sess->sess_prot_type)) {
			pr_err("CDB contains protect bit, but device + fabric does"
			       " not advertise PROTECT=1 feature bit\n");
			return TCM_INVALID_CDB_FIELD;
		}
		if (cmd->prot_pto)
			return TCM_NO_SENSE;
	}

	switch (dev->dev_attrib.pi_prot_type) {
	case TARGET_DIF_TYPE3_PROT:
		cmd->reftag_seed = 0xffffffff;
		break;
	case TARGET_DIF_TYPE2_PROT:
		if (protect)
			return TCM_INVALID_CDB_FIELD;

		cmd->reftag_seed = cmd->t_task_lba;
		break;
	case TARGET_DIF_TYPE1_PROT:
		cmd->reftag_seed = cmd->t_task_lba;
		break;
	case TARGET_DIF_TYPE0_PROT:
		/*
		 * See if the fabric supports T10-PI, and the session has been
		 * configured to allow export PROTECT=1 feature bit with backend
		 * devices that don't support T10-PI.
		 */
		fabric_prot = is_write ?
			      !!(sp_ops & (TARGET_PROT_DOUT_PASS | TARGET_PROT_DOUT_STRIP)) :
			      !!(sp_ops & (TARGET_PROT_DIN_PASS | TARGET_PROT_DIN_INSERT));

		if (fabric_prot && cmd->se_sess->sess_prot_type) {
			pi_prot_type = cmd->se_sess->sess_prot_type;
			break;
		}
		if (!protect)
			return TCM_NO_SENSE;
		/* Fallthrough */
	default:
		pr_err("Unable to determine pi_prot_type for CDB: 0x%02x "
		       "PROTECT: 0x%02x\n", cdb[0], protect);
		return TCM_INVALID_CDB_FIELD;
	}

	if (sbc_set_prot_op_checks(protect, fabric_prot, pi_prot_type, is_write, cmd))
		return TCM_INVALID_CDB_FIELD;

	cmd->prot_type = pi_prot_type;
	cmd->prot_length = dev->prot_length * sectors;

	/**
	 * In case protection information exists over the wire
	 * we modify command data length to describe pure data.
	 * The actual transfer length is data length + protection
	 * length
	 **/
	if (protect)
		cmd->data_length = sectors * dev->dev_attrib.block_size;

	pr_debug("%s: prot_type=%d, data_length=%d, prot_length=%d "
		 "prot_op=%d prot_checks=%d\n",
		 __func__, cmd->prot_type, cmd->data_length, cmd->prot_length,
		 cmd->prot_op, cmd->prot_checks);

	return TCM_NO_SENSE;
}

static int
sbc_check_dpofua(struct se_device *dev, struct se_cmd *cmd, unsigned char *cdb)
{
	if (cdb[1] & 0x10) {
		/* see explanation in spc_emulate_modesense */
		if (!target_check_fua(dev)) {
			pr_err("Got CDB: 0x%02x with DPO bit set, but device"
			       " does not advertise support for DPO\n", cdb[0]);
			return -EINVAL;
		}
	}
	if (cdb[1] & 0x8) {
		if (!target_check_fua(dev)) {
			pr_err("Got CDB: 0x%02x with FUA bit set, but device"
			       " does not advertise support for FUA write\n",
			       cdb[0]);
			return -EINVAL;
		}
		cmd->se_cmd_flags |= SCF_FUA;
	}
	return 0;
}

sense_reason_t
sbc_parse_cdb(struct se_cmd *cmd, struct sbc_ops *ops)
{
	struct se_device *dev = cmd->se_dev;
	unsigned char *cdb = cmd->t_task_cdb;
	unsigned int size;
	u32 sectors = 0;
	sense_reason_t ret;

	cmd->protocol_data = ops;

	switch (cdb[0]) {
	case READ_6:
		sectors = transport_get_sectors_6(cdb);
		cmd->t_task_lba = transport_lba_21(cdb);
		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;
		cmd->execute_cmd = sbc_execute_rw;
		break;
	case READ_10:
		sectors = transport_get_sectors_10(cdb);
		cmd->t_task_lba = transport_lba_32(cdb);

		if (sbc_check_dpofua(dev, cmd, cdb))
			return TCM_INVALID_CDB_FIELD;

		ret = sbc_check_prot(dev, cmd, cdb, sectors, false);
		if (ret)
			return ret;

		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;
		cmd->execute_cmd = sbc_execute_rw;
		break;
	case READ_12:
		sectors = transport_get_sectors_12(cdb);
		cmd->t_task_lba = transport_lba_32(cdb);

		if (sbc_check_dpofua(dev, cmd, cdb))
			return TCM_INVALID_CDB_FIELD;

		ret = sbc_check_prot(dev, cmd, cdb, sectors, false);
		if (ret)
			return ret;

		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;
		cmd->execute_cmd = sbc_execute_rw;
		break;
	case READ_16:
		sectors = transport_get_sectors_16(cdb);
		cmd->t_task_lba = transport_lba_64(cdb);

		if (sbc_check_dpofua(dev, cmd, cdb))
			return TCM_INVALID_CDB_FIELD;

		ret = sbc_check_prot(dev, cmd, cdb, sectors, false);
		if (ret)
			return ret;

		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;
		cmd->execute_cmd = sbc_execute_rw;
		break;
	case WRITE_6:
		sectors = transport_get_sectors_6(cdb);
		cmd->t_task_lba = transport_lba_21(cdb);
		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;
		cmd->execute_cmd = sbc_execute_rw;
		break;
	case WRITE_10:
	case WRITE_VERIFY:
		sectors = transport_get_sectors_10(cdb);
		cmd->t_task_lba = transport_lba_32(cdb);

		if (sbc_check_dpofua(dev, cmd, cdb))
			return TCM_INVALID_CDB_FIELD;

		ret = sbc_check_prot(dev, cmd, cdb, sectors, true);
		if (ret)
			return ret;

		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;
		cmd->execute_cmd = sbc_execute_rw;
		break;
	case WRITE_12:
		sectors = transport_get_sectors_12(cdb);
		cmd->t_task_lba = transport_lba_32(cdb);

		if (sbc_check_dpofua(dev, cmd, cdb))
			return TCM_INVALID_CDB_FIELD;

		ret = sbc_check_prot(dev, cmd, cdb, sectors, true);
		if (ret)
			return ret;

		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;
		cmd->execute_cmd = sbc_execute_rw;
		break;
	case WRITE_16:
		sectors = transport_get_sectors_16(cdb);
		cmd->t_task_lba = transport_lba_64(cdb);

		if (sbc_check_dpofua(dev, cmd, cdb))
			return TCM_INVALID_CDB_FIELD;

		ret = sbc_check_prot(dev, cmd, cdb, sectors, true);
		if (ret)
			return ret;

		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;
		cmd->execute_cmd = sbc_execute_rw;
		break;
	case XDWRITEREAD_10:
		if (cmd->data_direction != DMA_TO_DEVICE ||
		    !(cmd->se_cmd_flags & SCF_BIDI))
			return TCM_INVALID_CDB_FIELD;
		sectors = transport_get_sectors_10(cdb);

		if (sbc_check_dpofua(dev, cmd, cdb))
			return TCM_INVALID_CDB_FIELD;

		cmd->t_task_lba = transport_lba_32(cdb);
		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;

		/*
		 * Setup BIDI XOR callback to be run after I/O completion.
		 */
		cmd->execute_cmd = sbc_execute_rw;
		cmd->transport_complete_callback = &xdreadwrite_callback;
		break;
	case VARIABLE_LENGTH_CMD:
	{
		u16 service_action = get_unaligned_be16(&cdb[8]);
		switch (service_action) {
		case XDWRITEREAD_32:
			sectors = transport_get_sectors_32(cdb);

			if (sbc_check_dpofua(dev, cmd, cdb))
				return TCM_INVALID_CDB_FIELD;
			/*
			 * Use WRITE_32 and READ_32 opcodes for the emulated
			 * XDWRITE_READ_32 logic.
			 */
			cmd->t_task_lba = transport_lba_64_ext(cdb);
			cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;

			/*
			 * Setup BIDI XOR callback to be run during after I/O
			 * completion.
			 */
			cmd->execute_cmd = sbc_execute_rw;
			cmd->transport_complete_callback = &xdreadwrite_callback;
			break;
		case WRITE_SAME_32:
			sectors = transport_get_sectors_32(cdb);
			if (!sectors) {
				pr_err("WSNZ=1, WRITE_SAME w/sectors=0 not"
				       " supported\n");
				return TCM_INVALID_CDB_FIELD;
			}

			size = sbc_get_size(cmd, 1);
			cmd->t_task_lba = get_unaligned_be64(&cdb[12]);

			ret = sbc_setup_write_same(cmd, &cdb[10], ops);
			if (ret)
				return ret;
			break;
		default:
			pr_err("VARIABLE_LENGTH_CMD service action"
				" 0x%04x not supported\n", service_action);
			return TCM_UNSUPPORTED_SCSI_OPCODE;
		}
		break;
	}
	case COMPARE_AND_WRITE:
		sectors = cdb[13];
		/*
		 * Currently enforce COMPARE_AND_WRITE for a single sector
		 */
		if (sectors > 1) {
			pr_err("COMPARE_AND_WRITE contains NoLB: %u greater"
			       " than 1\n", sectors);
			return TCM_INVALID_CDB_FIELD;
		}
		if (sbc_check_dpofua(dev, cmd, cdb))
			return TCM_INVALID_CDB_FIELD;

		/*
		 * Double size because we have two buffers, note that
		 * zero is not an error..
		 */
		size = 2 * sbc_get_size(cmd, sectors);
		cmd->t_task_lba = get_unaligned_be64(&cdb[2]);
		cmd->t_task_nolb = sectors;
		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB | SCF_COMPARE_AND_WRITE;
		cmd->execute_cmd = sbc_compare_and_write;
		cmd->transport_complete_callback = compare_and_write_callback;
		break;
	case READ_CAPACITY:
		size = READ_CAP_LEN;
		cmd->execute_cmd = sbc_emulate_readcapacity;
		break;
	case SERVICE_ACTION_IN_16:
		switch (cmd->t_task_cdb[1] & 0x1f) {
		case SAI_READ_CAPACITY_16:
			cmd->execute_cmd = sbc_emulate_readcapacity_16;
			break;
		case SAI_REPORT_REFERRALS:
			cmd->execute_cmd = target_emulate_report_referrals;
			break;
		default:
			pr_err("Unsupported SA: 0x%02x\n",
				cmd->t_task_cdb[1] & 0x1f);
			return TCM_INVALID_CDB_FIELD;
		}
		size = (cdb[10] << 24) | (cdb[11] << 16) |
		       (cdb[12] << 8) | cdb[13];
		break;
	case SYNCHRONIZE_CACHE:
	case SYNCHRONIZE_CACHE_16:
		if (cdb[0] == SYNCHRONIZE_CACHE) {
			sectors = transport_get_sectors_10(cdb);
			cmd->t_task_lba = transport_lba_32(cdb);
		} else {
			sectors = transport_get_sectors_16(cdb);
			cmd->t_task_lba = transport_lba_64(cdb);
		}
		if (ops->execute_sync_cache) {
			cmd->execute_cmd = ops->execute_sync_cache;
			goto check_lba;
		}
		size = 0;
		cmd->execute_cmd = sbc_emulate_noop;
		break;
	case UNMAP:
		if (!ops->execute_unmap)
			return TCM_UNSUPPORTED_SCSI_OPCODE;

		if (!dev->dev_attrib.emulate_tpu) {
			pr_err("Got UNMAP, but backend device has"
			       " emulate_tpu disabled\n");
			return TCM_UNSUPPORTED_SCSI_OPCODE;
		}
		size = get_unaligned_be16(&cdb[7]);
		cmd->execute_cmd = sbc_execute_unmap;
		break;
	case WRITE_SAME_16:
		sectors = transport_get_sectors_16(cdb);
		if (!sectors) {
			pr_err("WSNZ=1, WRITE_SAME w/sectors=0 not supported\n");
			return TCM_INVALID_CDB_FIELD;
		}

		size = sbc_get_size(cmd, 1);
		cmd->t_task_lba = get_unaligned_be64(&cdb[2]);

		ret = sbc_setup_write_same(cmd, &cdb[1], ops);
		if (ret)
			return ret;
		break;
	case WRITE_SAME:
		sectors = transport_get_sectors_10(cdb);
		if (!sectors) {
			pr_err("WSNZ=1, WRITE_SAME w/sectors=0 not supported\n");
			return TCM_INVALID_CDB_FIELD;
		}

		size = sbc_get_size(cmd, 1);
		cmd->t_task_lba = get_unaligned_be32(&cdb[2]);

		/*
		 * Follow sbcr26 with WRITE_SAME (10) and check for the existence
		 * of byte 1 bit 3 UNMAP instead of original reserved field
		 */
		ret = sbc_setup_write_same(cmd, &cdb[1], ops);
		if (ret)
			return ret;
		break;
	case VERIFY:
		size = 0;
		sectors = transport_get_sectors_10(cdb);
		cmd->t_task_lba = transport_lba_32(cdb);
		cmd->execute_cmd = sbc_emulate_noop;
		goto check_lba;
	case REZERO_UNIT:
	case SEEK_6:
	case SEEK_10:
		/*
		 * There are still clients out there which use these old SCSI-2
		 * commands. This mainly happens when running VMs with legacy
		 * guest systems, connected via SCSI command pass-through to
		 * iSCSI targets. Make them happy and return status GOOD.
		 */
		size = 0;
		cmd->execute_cmd = sbc_emulate_noop;
		break;
	case START_STOP:
		size = 0;
		cmd->execute_cmd = sbc_emulate_startstop;
		break;
	default:
		ret = spc_parse_cdb(cmd, &size);
		if (ret)
			return ret;
	}

	/* reject any command that we don't have a handler for */
	if (!cmd->execute_cmd)
		return TCM_UNSUPPORTED_SCSI_OPCODE;

	if (cmd->se_cmd_flags & SCF_SCSI_DATA_CDB) {
		unsigned long long end_lba;
check_lba:
		end_lba = dev->transport->get_blocks(dev) + 1;
		if (((cmd->t_task_lba + sectors) < cmd->t_task_lba) ||
		    ((cmd->t_task_lba + sectors) > end_lba)) {
			pr_err("cmd exceeds last lba %llu "
				"(lba %llu, sectors %u)\n",
				end_lba, cmd->t_task_lba, sectors);
			return TCM_ADDRESS_OUT_OF_RANGE;
		}

		if (!(cmd->se_cmd_flags & SCF_COMPARE_AND_WRITE))
			size = sbc_get_size(cmd, sectors);
	}

	return target_cmd_size_check(cmd, size);
}
EXPORT_SYMBOL(sbc_parse_cdb);

u32 sbc_get_device_type(struct se_device *dev)
{
	return TYPE_DISK;
}
EXPORT_SYMBOL(sbc_get_device_type);

static sense_reason_t
sbc_execute_unmap(struct se_cmd *cmd)
{
	struct sbc_ops *ops = cmd->protocol_data;
	struct se_device *dev = cmd->se_dev;
	unsigned char *buf, *ptr = NULL;
	sector_t lba;
	int size;
	u32 range;
	sense_reason_t ret = 0;
	int dl, bd_dl;

	/* We never set ANC_SUP */
	if (cmd->t_task_cdb[1])
		return TCM_INVALID_CDB_FIELD;

	if (cmd->data_length == 0) {
		target_complete_cmd(cmd, SAM_STAT_GOOD);
		return 0;
	}

	if (cmd->data_length < 8) {
		pr_warn("UNMAP parameter list length %u too small\n",
			cmd->data_length);
		return TCM_PARAMETER_LIST_LENGTH_ERROR;
	}

	buf = transport_kmap_data_sg(cmd);
	if (!buf)
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;

	dl = get_unaligned_be16(&buf[0]);
	bd_dl = get_unaligned_be16(&buf[2]);

	size = cmd->data_length - 8;
	if (bd_dl > size)
		pr_warn("UNMAP parameter list length %u too small, ignoring bd_dl %u\n",
			cmd->data_length, bd_dl);
	else
		size = bd_dl;

	if (size / 16 > dev->dev_attrib.max_unmap_block_desc_count) {
		ret = TCM_INVALID_PARAMETER_LIST;
		goto err;
	}

	/* First UNMAP block descriptor starts at 8 byte offset */
	ptr = &buf[8];
	pr_debug("UNMAP: Sub: %s Using dl: %u bd_dl: %u size: %u"
		" ptr: %p\n", dev->transport->name, dl, bd_dl, size, ptr);

	while (size >= 16) {
		lba = get_unaligned_be64(&ptr[0]);
		range = get_unaligned_be32(&ptr[8]);
		pr_debug("UNMAP: Using lba: %llu and range: %u\n",
				 (unsigned long long)lba, range);

		if (range > dev->dev_attrib.max_unmap_lba_count) {
			ret = TCM_INVALID_PARAMETER_LIST;
			goto err;
		}

		if (lba + range > dev->transport->get_blocks(dev) + 1) {
			ret = TCM_ADDRESS_OUT_OF_RANGE;
			goto err;
		}

		ret = ops->execute_unmap(cmd, lba, range);
		if (ret)
			goto err;

		ptr += 16;
		size -= 16;
	}

err:
	transport_kunmap_data_sg(cmd);
	if (!ret)
		target_complete_cmd(cmd, GOOD);
	return ret;
}

void
sbc_dif_generate(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	struct t10_pi_tuple *sdt;
	struct scatterlist *dsg = cmd->t_data_sg, *psg;
	sector_t sector = cmd->t_task_lba;
	void *daddr, *paddr;
	int i, j, offset = 0;
	unsigned int block_size = dev->dev_attrib.block_size;

	for_each_sg(cmd->t_prot_sg, psg, cmd->t_prot_nents, i) {
		paddr = kmap_atomic(sg_page(psg)) + psg->offset;
		daddr = kmap_atomic(sg_page(dsg)) + dsg->offset;

		for (j = 0; j < psg->length;
				j += sizeof(*sdt)) {
			__u16 crc;
			unsigned int avail;

			if (offset >= dsg->length) {
				offset -= dsg->length;
				kunmap_atomic(daddr - dsg->offset);
				dsg = sg_next(dsg);
				if (!dsg) {
					kunmap_atomic(paddr - psg->offset);
					return;
				}
				daddr = kmap_atomic(sg_page(dsg)) + dsg->offset;
			}

			sdt = paddr + j;
			avail = min(block_size, dsg->length - offset);
			crc = crc_t10dif(daddr + offset, avail);
			if (avail < block_size) {
				kunmap_atomic(daddr - dsg->offset);
				dsg = sg_next(dsg);
				if (!dsg) {
					kunmap_atomic(paddr - psg->offset);
					return;
				}
				daddr = kmap_atomic(sg_page(dsg)) + dsg->offset;
				offset = block_size - avail;
				crc = crc_t10dif_update(crc, daddr, offset);
			} else {
				offset += block_size;
			}

			sdt->guard_tag = cpu_to_be16(crc);
			if (cmd->prot_type == TARGET_DIF_TYPE1_PROT)
				sdt->ref_tag = cpu_to_be32(sector & 0xffffffff);
			sdt->app_tag = 0;

			pr_debug("DIF %s INSERT sector: %llu guard_tag: 0x%04x"
				 " app_tag: 0x%04x ref_tag: %u\n",
				 (cmd->data_direction == DMA_TO_DEVICE) ?
				 "WRITE" : "READ", (unsigned long long)sector,
				 sdt->guard_tag, sdt->app_tag,
				 be32_to_cpu(sdt->ref_tag));

			sector++;
		}

		kunmap_atomic(daddr - dsg->offset);
		kunmap_atomic(paddr - psg->offset);
	}
}

static sense_reason_t
sbc_dif_v1_verify(struct se_cmd *cmd, struct t10_pi_tuple *sdt,
		  __u16 crc, sector_t sector, unsigned int ei_lba)
{
	__be16 csum;

	if (!(cmd->prot_checks & TARGET_DIF_CHECK_GUARD))
		goto check_ref;

	csum = cpu_to_be16(crc);

	if (sdt->guard_tag != csum) {
		pr_err("DIFv1 checksum failed on sector %llu guard tag 0x%04x"
			" csum 0x%04x\n", (unsigned long long)sector,
			be16_to_cpu(sdt->guard_tag), be16_to_cpu(csum));
		return TCM_LOGICAL_BLOCK_GUARD_CHECK_FAILED;
	}

check_ref:
	if (!(cmd->prot_checks & TARGET_DIF_CHECK_REFTAG))
		return 0;

	if (cmd->prot_type == TARGET_DIF_TYPE1_PROT &&
	    be32_to_cpu(sdt->ref_tag) != (sector & 0xffffffff)) {
		pr_err("DIFv1 Type 1 reference failed on sector: %llu tag: 0x%08x"
		       " sector MSB: 0x%08x\n", (unsigned long long)sector,
		       be32_to_cpu(sdt->ref_tag), (u32)(sector & 0xffffffff));
		return TCM_LOGICAL_BLOCK_REF_TAG_CHECK_FAILED;
	}

	if (cmd->prot_type == TARGET_DIF_TYPE2_PROT &&
	    be32_to_cpu(sdt->ref_tag) != ei_lba) {
		pr_err("DIFv1 Type 2 reference failed on sector: %llu tag: 0x%08x"
		       " ei_lba: 0x%08x\n", (unsigned long long)sector,
			be32_to_cpu(sdt->ref_tag), ei_lba);
		return TCM_LOGICAL_BLOCK_REF_TAG_CHECK_FAILED;
	}

	return 0;
}

void sbc_dif_copy_prot(struct se_cmd *cmd, unsigned int sectors, bool read,
		       struct scatterlist *sg, int sg_off)
{
	struct se_device *dev = cmd->se_dev;
	struct scatterlist *psg;
	void *paddr, *addr;
	unsigned int i, len, left;
	unsigned int offset = sg_off;

	if (!sg)
		return;

	left = sectors * dev->prot_length;

	for_each_sg(cmd->t_prot_sg, psg, cmd->t_prot_nents, i) {
		unsigned int psg_len, copied = 0;

		paddr = kmap_atomic(sg_page(psg)) + psg->offset;
		psg_len = min(left, psg->length);
		while (psg_len) {
			len = min(psg_len, sg->length - offset);
			addr = kmap_atomic(sg_page(sg)) + sg->offset + offset;

			if (read)
				memcpy(paddr + copied, addr, len);
			else
				memcpy(addr, paddr + copied, len);

			left -= len;
			offset += len;
			copied += len;
			psg_len -= len;

			kunmap_atomic(addr - sg->offset - offset);

			if (offset >= sg->length) {
				sg = sg_next(sg);
				offset = 0;
			}
		}
		kunmap_atomic(paddr - psg->offset);
	}
}
EXPORT_SYMBOL(sbc_dif_copy_prot);

sense_reason_t
sbc_dif_verify(struct se_cmd *cmd, sector_t start, unsigned int sectors,
	       unsigned int ei_lba, struct scatterlist *psg, int psg_off)
{
	struct se_device *dev = cmd->se_dev;
	struct t10_pi_tuple *sdt;
	struct scatterlist *dsg = cmd->t_data_sg;
	sector_t sector = start;
	void *daddr, *paddr;
	int i;
	sense_reason_t rc;
	int dsg_off = 0;
	unsigned int block_size = dev->dev_attrib.block_size;

	for (; psg && sector < start + sectors; psg = sg_next(psg)) {
		paddr = kmap_atomic(sg_page(psg)) + psg->offset;
		daddr = kmap_atomic(sg_page(dsg)) + dsg->offset;

		for (i = psg_off; i < psg->length &&
				sector < start + sectors;
				i += sizeof(*sdt)) {
			__u16 crc;
			unsigned int avail;

			if (dsg_off >= dsg->length) {
				dsg_off -= dsg->length;
				kunmap_atomic(daddr - dsg->offset);
				dsg = sg_next(dsg);
				if (!dsg) {
					kunmap_atomic(paddr - psg->offset);
					return 0;
				}
				daddr = kmap_atomic(sg_page(dsg)) + dsg->offset;
			}

			sdt = paddr + i;

			pr_debug("DIF READ sector: %llu guard_tag: 0x%04x"
				 " app_tag: 0x%04x ref_tag: %u\n",
				 (unsigned long long)sector, sdt->guard_tag,
				 sdt->app_tag, be32_to_cpu(sdt->ref_tag));

			if (sdt->app_tag == cpu_to_be16(0xffff)) {
				dsg_off += block_size;
				goto next;
			}

			avail = min(block_size, dsg->length - dsg_off);
			crc = crc_t10dif(daddr + dsg_off, avail);
			if (avail < block_size) {
				kunmap_atomic(daddr - dsg->offset);
				dsg = sg_next(dsg);
				if (!dsg) {
					kunmap_atomic(paddr - psg->offset);
					return 0;
				}
				daddr = kmap_atomic(sg_page(dsg)) + dsg->offset;
				dsg_off = block_size - avail;
				crc = crc_t10dif_update(crc, daddr, dsg_off);
			} else {
				dsg_off += block_size;
			}

			rc = sbc_dif_v1_verify(cmd, sdt, crc, sector, ei_lba);
			if (rc) {
				kunmap_atomic(daddr - dsg->offset);
				kunmap_atomic(paddr - psg->offset);
				cmd->bad_sector = sector;
				return rc;
			}
next:
			sector++;
			ei_lba++;
		}

		psg_off = 0;
		kunmap_atomic(daddr - dsg->offset);
		kunmap_atomic(paddr - psg->offset);
	}

	return 0;
}
EXPORT_SYMBOL(sbc_dif_verify);
