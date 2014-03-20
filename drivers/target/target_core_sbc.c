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
#include <asm/unaligned.h>
#include <scsi/scsi.h>
#include <scsi/scsi_tcq.h>

#include <target/target_core_base.h>
#include <target/target_core_backend.h>
#include <target/target_core_fabric.h>

#include "target_core_internal.h"
#include "target_core_ua.h"
#include "target_core_alua.h"

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

	target_complete_cmd(cmd, GOOD);
	return 0;
}

static sense_reason_t
sbc_emulate_readcapacity_16(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
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
	if (dev->dev_attrib.pi_prot_type)
		buf[12] = (dev->dev_attrib.pi_prot_type - 1) << 1 | 0x1;

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
	if (dev->dev_attrib.emulate_tpu || dev->dev_attrib.emulate_tpws)
		buf[14] |= 0x80;

	rbuf = transport_kmap_data_sg(cmd);
	if (rbuf) {
		memcpy(rbuf, buf, min_t(u32, sizeof(buf), cmd->data_length));
		transport_kunmap_data_sg(cmd);
	}

	target_complete_cmd(cmd, GOOD);
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
sbc_emulate_noop(struct se_cmd *cmd)
{
	target_complete_cmd(cmd, GOOD);
	return 0;
}

static inline u32 sbc_get_size(struct se_cmd *cmd, u32 sectors)
{
	return cmd->se_dev->dev_attrib.block_size * sectors;
}

static int sbc_check_valid_sectors(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;
	unsigned long long end_lba;
	u32 sectors;

	sectors = cmd->data_length / dev->dev_attrib.block_size;
	end_lba = dev->transport->get_blocks(dev) + 1;

	if (cmd->t_task_lba + sectors > end_lba) {
		pr_err("target: lba %llu, sectors %u exceeds end lba %llu\n",
			cmd->t_task_lba, sectors, end_lba);
		return -EINVAL;
	}

	return 0;
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
	unsigned int sectors = sbc_get_write_same_sectors(cmd);

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
		if (!ops->execute_write_same_unmap)
			return TCM_UNSUPPORTED_SCSI_OPCODE;

		cmd->execute_cmd = ops->execute_write_same_unmap;
		return 0;
	}
	if (!ops->execute_write_same)
		return TCM_UNSUPPORTED_SCSI_OPCODE;

	cmd->execute_cmd = ops->execute_write_same;
	return 0;
}

static sense_reason_t xdreadwrite_callback(struct se_cmd *cmd)
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
	return cmd->execute_rw(cmd, cmd->t_data_sg, cmd->t_data_nents,
			       cmd->data_direction);
}

static sense_reason_t compare_and_write_post(struct se_cmd *cmd)
{
	struct se_device *dev = cmd->se_dev;

	/*
	 * Only set SCF_COMPARE_AND_WRITE_POST to force a response fall-through
	 * within target_complete_ok_work() if the command was successfully
	 * sent to the backend driver.
	 */
	spin_lock_irq(&cmd->t_state_lock);
	if ((cmd->transport_state & CMD_T_SENT) && !cmd->scsi_status)
		cmd->se_cmd_flags |= SCF_COMPARE_AND_WRITE_POST;
	spin_unlock_irq(&cmd->t_state_lock);

	/*
	 * Unlock ->caw_sem originally obtained during sbc_compare_and_write()
	 * before the original READ I/O submission.
	 */
	up(&dev->caw_sem);

	return TCM_NO_SENSE;
}

static sense_reason_t compare_and_write_callback(struct se_cmd *cmd)
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
	 * which will not have taken ->caw_mutex yet..
	 */
	if (!cmd->t_data_sg || !cmd->t_bidi_data_sg)
		return TCM_NO_SENSE;
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

	write_sg = kzalloc(sizeof(struct scatterlist) * cmd->t_data_nents,
			   GFP_KERNEL);
	if (!write_sg) {
		pr_err("Unable to allocate compare_and_write sg\n");
		ret = TCM_OUT_OF_RESOURCES;
		goto out;
	}
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
				    block_size);
		} else {
			sg_miter_next(&m);
			sg_set_page(&write_sg[i], m.page, block_size,
				    0);
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

	cmd->sam_task_attr = MSG_HEAD_TAG;
	cmd->transport_complete_callback = compare_and_write_post;
	/*
	 * Now reset ->execute_cmd() to the normal sbc_execute_rw() handler
	 * for submitting the adjusted SGL to write instance user-data.
	 */
	cmd->execute_cmd = sbc_execute_rw;

	spin_lock_irq(&cmd->t_state_lock);
	cmd->t_state = TRANSPORT_PROCESSING;
	cmd->transport_state |= CMD_T_ACTIVE|CMD_T_BUSY|CMD_T_SENT;
	spin_unlock_irq(&cmd->t_state_lock);

	__target_execute_cmd(cmd);

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
	struct se_device *dev = cmd->se_dev;
	sense_reason_t ret;
	int rc;
	/*
	 * Submit the READ first for COMPARE_AND_WRITE to perform the
	 * comparision using SGLs at cmd->t_bidi_data_sg..
	 */
	rc = down_interruptible(&dev->caw_sem);
	if ((rc != 0) || signal_pending(current)) {
		cmd->transport_complete_callback = NULL;
		return TCM_LOGICAL_UNIT_COMMUNICATION_FAILURE;
	}
	/*
	 * Reset cmd->data_length to individual block_size in order to not
	 * confuse backend drivers that depend on this value matching the
	 * size of the I/O being submitted.
	 */
	cmd->data_length = cmd->t_task_nolb * dev->dev_attrib.block_size;

	ret = cmd->execute_rw(cmd, cmd->t_bidi_data_sg, cmd->t_bidi_data_nents,
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

static bool
sbc_check_prot(struct se_device *dev, struct se_cmd *cmd, unsigned char *cdb,
	       u32 sectors)
{
	if (!cmd->t_prot_sg || !cmd->t_prot_nents)
		return true;

	switch (dev->dev_attrib.pi_prot_type) {
	case TARGET_DIF_TYPE3_PROT:
		if (!(cdb[1] & 0xe0))
			return true;

		cmd->reftag_seed = 0xffffffff;
		break;
	case TARGET_DIF_TYPE2_PROT:
		if (cdb[1] & 0xe0)
			return false;

		cmd->reftag_seed = cmd->t_task_lba;
		break;
	case TARGET_DIF_TYPE1_PROT:
		if (!(cdb[1] & 0xe0))
			return true;

		cmd->reftag_seed = cmd->t_task_lba;
		break;
	case TARGET_DIF_TYPE0_PROT:
	default:
		return true;
	}

	cmd->prot_type = dev->dev_attrib.pi_prot_type;
	cmd->prot_length = dev->prot_length * sectors;
	cmd->prot_handover = PROT_SEPERATED;

	return true;
}

sense_reason_t
sbc_parse_cdb(struct se_cmd *cmd, struct sbc_ops *ops)
{
	struct se_device *dev = cmd->se_dev;
	unsigned char *cdb = cmd->t_task_cdb;
	unsigned int size;
	u32 sectors = 0;
	sense_reason_t ret;

	switch (cdb[0]) {
	case READ_6:
		sectors = transport_get_sectors_6(cdb);
		cmd->t_task_lba = transport_lba_21(cdb);
		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;
		cmd->execute_rw = ops->execute_rw;
		cmd->execute_cmd = sbc_execute_rw;
		break;
	case READ_10:
		sectors = transport_get_sectors_10(cdb);
		cmd->t_task_lba = transport_lba_32(cdb);

		if (!sbc_check_prot(dev, cmd, cdb, sectors))
			return TCM_UNSUPPORTED_SCSI_OPCODE;

		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;
		cmd->execute_rw = ops->execute_rw;
		cmd->execute_cmd = sbc_execute_rw;
		break;
	case READ_12:
		sectors = transport_get_sectors_12(cdb);
		cmd->t_task_lba = transport_lba_32(cdb);

		if (!sbc_check_prot(dev, cmd, cdb, sectors))
			return TCM_UNSUPPORTED_SCSI_OPCODE;

		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;
		cmd->execute_rw = ops->execute_rw;
		cmd->execute_cmd = sbc_execute_rw;
		break;
	case READ_16:
		sectors = transport_get_sectors_16(cdb);
		cmd->t_task_lba = transport_lba_64(cdb);

		if (!sbc_check_prot(dev, cmd, cdb, sectors))
			return TCM_UNSUPPORTED_SCSI_OPCODE;

		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;
		cmd->execute_rw = ops->execute_rw;
		cmd->execute_cmd = sbc_execute_rw;
		break;
	case WRITE_6:
		sectors = transport_get_sectors_6(cdb);
		cmd->t_task_lba = transport_lba_21(cdb);
		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;
		cmd->execute_rw = ops->execute_rw;
		cmd->execute_cmd = sbc_execute_rw;
		break;
	case WRITE_10:
	case WRITE_VERIFY:
		sectors = transport_get_sectors_10(cdb);
		cmd->t_task_lba = transport_lba_32(cdb);

		if (!sbc_check_prot(dev, cmd, cdb, sectors))
			return TCM_UNSUPPORTED_SCSI_OPCODE;

		if (cdb[1] & 0x8)
			cmd->se_cmd_flags |= SCF_FUA;
		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;
		cmd->execute_rw = ops->execute_rw;
		cmd->execute_cmd = sbc_execute_rw;
		break;
	case WRITE_12:
		sectors = transport_get_sectors_12(cdb);
		cmd->t_task_lba = transport_lba_32(cdb);

		if (!sbc_check_prot(dev, cmd, cdb, sectors))
			return TCM_UNSUPPORTED_SCSI_OPCODE;

		if (cdb[1] & 0x8)
			cmd->se_cmd_flags |= SCF_FUA;
		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;
		cmd->execute_rw = ops->execute_rw;
		cmd->execute_cmd = sbc_execute_rw;
		break;
	case WRITE_16:
		sectors = transport_get_sectors_16(cdb);
		cmd->t_task_lba = transport_lba_64(cdb);

		if (!sbc_check_prot(dev, cmd, cdb, sectors))
			return TCM_UNSUPPORTED_SCSI_OPCODE;

		if (cdb[1] & 0x8)
			cmd->se_cmd_flags |= SCF_FUA;
		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;
		cmd->execute_rw = ops->execute_rw;
		cmd->execute_cmd = sbc_execute_rw;
		break;
	case XDWRITEREAD_10:
		if (cmd->data_direction != DMA_TO_DEVICE ||
		    !(cmd->se_cmd_flags & SCF_BIDI))
			return TCM_INVALID_CDB_FIELD;
		sectors = transport_get_sectors_10(cdb);

		cmd->t_task_lba = transport_lba_32(cdb);
		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB;

		/*
		 * Setup BIDI XOR callback to be run after I/O completion.
		 */
		cmd->execute_rw = ops->execute_rw;
		cmd->execute_cmd = sbc_execute_rw;
		cmd->transport_complete_callback = &xdreadwrite_callback;
		if (cdb[1] & 0x8)
			cmd->se_cmd_flags |= SCF_FUA;
		break;
	case VARIABLE_LENGTH_CMD:
	{
		u16 service_action = get_unaligned_be16(&cdb[8]);
		switch (service_action) {
		case XDWRITEREAD_32:
			sectors = transport_get_sectors_32(cdb);

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
			cmd->execute_rw = ops->execute_rw;
			cmd->execute_cmd = sbc_execute_rw;
			cmd->transport_complete_callback = &xdreadwrite_callback;
			if (cdb[1] & 0x8)
				cmd->se_cmd_flags |= SCF_FUA;
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
		/*
		 * Double size because we have two buffers, note that
		 * zero is not an error..
		 */
		size = 2 * sbc_get_size(cmd, sectors);
		cmd->t_task_lba = get_unaligned_be64(&cdb[2]);
		cmd->t_task_nolb = sectors;
		cmd->se_cmd_flags |= SCF_SCSI_DATA_CDB | SCF_COMPARE_AND_WRITE;
		cmd->execute_rw = ops->execute_rw;
		cmd->execute_cmd = sbc_compare_and_write;
		cmd->transport_complete_callback = compare_and_write_callback;
		break;
	case READ_CAPACITY:
		size = READ_CAP_LEN;
		cmd->execute_cmd = sbc_emulate_readcapacity;
		break;
	case SERVICE_ACTION_IN:
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
		if (!ops->execute_sync_cache) {
			size = 0;
			cmd->execute_cmd = sbc_emulate_noop;
			break;
		}

		/*
		 * Extract LBA and range to be flushed for emulated SYNCHRONIZE_CACHE
		 */
		if (cdb[0] == SYNCHRONIZE_CACHE) {
			sectors = transport_get_sectors_10(cdb);
			cmd->t_task_lba = transport_lba_32(cdb);
		} else {
			sectors = transport_get_sectors_16(cdb);
			cmd->t_task_lba = transport_lba_64(cdb);
		}

		size = sbc_get_size(cmd, sectors);

		/*
		 * Check to ensure that LBA + Range does not exceed past end of
		 * device for IBLOCK and FILEIO ->do_sync_cache() backend calls
		 */
		if (cmd->t_task_lba || sectors) {
			if (sbc_check_valid_sectors(cmd) < 0)
				return TCM_ADDRESS_OUT_OF_RANGE;
		}
		cmd->execute_cmd = ops->execute_sync_cache;
		break;
	case UNMAP:
		if (!ops->execute_unmap)
			return TCM_UNSUPPORTED_SCSI_OPCODE;

		size = get_unaligned_be16(&cdb[7]);
		cmd->execute_cmd = ops->execute_unmap;
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
		cmd->execute_cmd = sbc_emulate_noop;
		break;
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
	default:
		ret = spc_parse_cdb(cmd, &size);
		if (ret)
			return ret;
	}

	/* reject any command that we don't have a handler for */
	if (!(cmd->se_cmd_flags & SCF_SCSI_DATA_CDB) && !cmd->execute_cmd)
		return TCM_UNSUPPORTED_SCSI_OPCODE;

	if (cmd->se_cmd_flags & SCF_SCSI_DATA_CDB) {
		unsigned long long end_lba;

		if (sectors > dev->dev_attrib.fabric_max_sectors) {
			printk_ratelimited(KERN_ERR "SCSI OP %02xh with too"
				" big sectors %u exceeds fabric_max_sectors:"
				" %u\n", cdb[0], sectors,
				dev->dev_attrib.fabric_max_sectors);
			return TCM_INVALID_CDB_FIELD;
		}
		if (sectors > dev->dev_attrib.hw_max_sectors) {
			printk_ratelimited(KERN_ERR "SCSI OP %02xh with too"
				" big sectors %u exceeds backend hw_max_sectors:"
				" %u\n", cdb[0], sectors,
				dev->dev_attrib.hw_max_sectors);
			return TCM_INVALID_CDB_FIELD;
		}

		end_lba = dev->transport->get_blocks(dev) + 1;
		if (cmd->t_task_lba + sectors > end_lba) {
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

sense_reason_t
sbc_execute_unmap(struct se_cmd *cmd,
	sense_reason_t (*do_unmap_fn)(struct se_cmd *, void *,
				      sector_t, sector_t),
	void *priv)
{
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

		ret = do_unmap_fn(cmd, priv, lba, range);
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
EXPORT_SYMBOL(sbc_execute_unmap);

static sense_reason_t
sbc_dif_v1_verify(struct se_device *dev, struct se_dif_v1_tuple *sdt,
		  const void *p, sector_t sector, unsigned int ei_lba)
{
	int block_size = dev->dev_attrib.block_size;
	__be16 csum;

	csum = cpu_to_be16(crc_t10dif(p, block_size));

	if (sdt->guard_tag != csum) {
		pr_err("DIFv1 checksum failed on sector %llu guard tag 0x%04x"
			" csum 0x%04x\n", (unsigned long long)sector,
			be16_to_cpu(sdt->guard_tag), be16_to_cpu(csum));
		return TCM_LOGICAL_BLOCK_GUARD_CHECK_FAILED;
	}

	if (dev->dev_attrib.pi_prot_type == TARGET_DIF_TYPE1_PROT &&
	    be32_to_cpu(sdt->ref_tag) != (sector & 0xffffffff)) {
		pr_err("DIFv1 Type 1 reference failed on sector: %llu tag: 0x%08x"
		       " sector MSB: 0x%08x\n", (unsigned long long)sector,
		       be32_to_cpu(sdt->ref_tag), (u32)(sector & 0xffffffff));
		return TCM_LOGICAL_BLOCK_REF_TAG_CHECK_FAILED;
	}

	if (dev->dev_attrib.pi_prot_type == TARGET_DIF_TYPE2_PROT &&
	    be32_to_cpu(sdt->ref_tag) != ei_lba) {
		pr_err("DIFv1 Type 2 reference failed on sector: %llu tag: 0x%08x"
		       " ei_lba: 0x%08x\n", (unsigned long long)sector,
			be32_to_cpu(sdt->ref_tag), ei_lba);
		return TCM_LOGICAL_BLOCK_REF_TAG_CHECK_FAILED;
	}

	return 0;
}

static void
sbc_dif_copy_prot(struct se_cmd *cmd, unsigned int sectors, bool read,
		  struct scatterlist *sg, int sg_off)
{
	struct se_device *dev = cmd->se_dev;
	struct scatterlist *psg;
	void *paddr, *addr;
	unsigned int i, len, left;
	unsigned int offset = sg_off;

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

			if (offset >= sg->length) {
				sg = sg_next(sg);
				offset = 0;
			}
			kunmap_atomic(addr);
		}
		kunmap_atomic(paddr);
	}
}

sense_reason_t
sbc_dif_verify_write(struct se_cmd *cmd, sector_t start, unsigned int sectors,
		     unsigned int ei_lba, struct scatterlist *sg, int sg_off)
{
	struct se_device *dev = cmd->se_dev;
	struct se_dif_v1_tuple *sdt;
	struct scatterlist *dsg, *psg = cmd->t_prot_sg;
	sector_t sector = start;
	void *daddr, *paddr;
	int i, j, offset = 0;
	sense_reason_t rc;

	for_each_sg(cmd->t_data_sg, dsg, cmd->t_data_nents, i) {
		daddr = kmap_atomic(sg_page(dsg)) + dsg->offset;
		paddr = kmap_atomic(sg_page(psg)) + psg->offset;

		for (j = 0; j < dsg->length; j += dev->dev_attrib.block_size) {

			if (offset >= psg->length) {
				kunmap_atomic(paddr);
				psg = sg_next(psg);
				paddr = kmap_atomic(sg_page(psg)) + psg->offset;
				offset = 0;
			}

			sdt = paddr + offset;

			pr_debug("DIF WRITE sector: %llu guard_tag: 0x%04x"
				 " app_tag: 0x%04x ref_tag: %u\n",
				 (unsigned long long)sector, sdt->guard_tag,
				 sdt->app_tag, be32_to_cpu(sdt->ref_tag));

			rc = sbc_dif_v1_verify(dev, sdt, daddr + j, sector,
					       ei_lba);
			if (rc) {
				kunmap_atomic(paddr);
				kunmap_atomic(daddr);
				cmd->bad_sector = sector;
				return rc;
			}

			sector++;
			ei_lba++;
			offset += sizeof(struct se_dif_v1_tuple);
		}

		kunmap_atomic(paddr);
		kunmap_atomic(daddr);
	}
	sbc_dif_copy_prot(cmd, sectors, false, sg, sg_off);

	return 0;
}
EXPORT_SYMBOL(sbc_dif_verify_write);

sense_reason_t
sbc_dif_verify_read(struct se_cmd *cmd, sector_t start, unsigned int sectors,
		    unsigned int ei_lba, struct scatterlist *sg, int sg_off)
{
	struct se_device *dev = cmd->se_dev;
	struct se_dif_v1_tuple *sdt;
	struct scatterlist *dsg, *psg = sg;
	sector_t sector = start;
	void *daddr, *paddr;
	int i, j, offset = sg_off;
	sense_reason_t rc;

	for_each_sg(cmd->t_data_sg, dsg, cmd->t_data_nents, i) {
		daddr = kmap_atomic(sg_page(dsg)) + dsg->offset;
		paddr = kmap_atomic(sg_page(psg)) + sg->offset;

		for (j = 0; j < dsg->length; j += dev->dev_attrib.block_size) {

			if (offset >= psg->length) {
				kunmap_atomic(paddr);
				psg = sg_next(psg);
				paddr = kmap_atomic(sg_page(psg)) + psg->offset;
				offset = 0;
			}

			sdt = paddr + offset;

			pr_debug("DIF READ sector: %llu guard_tag: 0x%04x"
				 " app_tag: 0x%04x ref_tag: %u\n",
				 (unsigned long long)sector, sdt->guard_tag,
				 sdt->app_tag, be32_to_cpu(sdt->ref_tag));

			if (sdt->app_tag == cpu_to_be16(0xffff)) {
				sector++;
				offset += sizeof(struct se_dif_v1_tuple);
				continue;
			}

			rc = sbc_dif_v1_verify(dev, sdt, daddr + j, sector,
					       ei_lba);
			if (rc) {
				kunmap_atomic(paddr);
				kunmap_atomic(daddr);
				cmd->bad_sector = sector;
				return rc;
			}

			sector++;
			ei_lba++;
			offset += sizeof(struct se_dif_v1_tuple);
		}

		kunmap_atomic(paddr);
		kunmap_atomic(daddr);
	}
	sbc_dif_copy_prot(cmd, sectors, true, sg, sg_off);

	return 0;
}
EXPORT_SYMBOL(sbc_dif_verify_read);
