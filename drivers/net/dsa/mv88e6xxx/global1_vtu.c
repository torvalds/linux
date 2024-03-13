// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88E6xxx VLAN [Spanning Tree] Translation Unit (VTU [STU]) support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 * Copyright (c) 2015 CMC Electronics, Inc.
 * Copyright (c) 2017 Savoir-faire Linux, Inc.
 */

#include <linux/bitfield.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>

#include "chip.h"
#include "global1.h"
#include "trace.h"

/* Offset 0x02: VTU FID Register */

static int mv88e6xxx_g1_vtu_fid_read(struct mv88e6xxx_chip *chip,
				     struct mv88e6xxx_vtu_entry *entry)
{
	u16 val;
	int err;

	err = mv88e6xxx_g1_read(chip, MV88E6352_G1_VTU_FID, &val);
	if (err)
		return err;

	entry->fid = val & MV88E6352_G1_VTU_FID_MASK;
	entry->policy = !!(val & MV88E6352_G1_VTU_FID_VID_POLICY);
	return 0;
}

static int mv88e6xxx_g1_vtu_fid_write(struct mv88e6xxx_chip *chip,
				      struct mv88e6xxx_vtu_entry *entry)
{
	u16 val = entry->fid & MV88E6352_G1_VTU_FID_MASK;

	if (entry->policy)
		val |= MV88E6352_G1_VTU_FID_VID_POLICY;

	return mv88e6xxx_g1_write(chip, MV88E6352_G1_VTU_FID, val);
}

/* Offset 0x03: VTU SID Register */

static int mv88e6xxx_g1_vtu_sid_read(struct mv88e6xxx_chip *chip, u8 *sid)
{
	u16 val;
	int err;

	err = mv88e6xxx_g1_read(chip, MV88E6352_G1_VTU_SID, &val);
	if (err)
		return err;

	*sid = val & MV88E6352_G1_VTU_SID_MASK;

	return 0;
}

static int mv88e6xxx_g1_vtu_sid_write(struct mv88e6xxx_chip *chip, u8 sid)
{
	u16 val = sid & MV88E6352_G1_VTU_SID_MASK;

	return mv88e6xxx_g1_write(chip, MV88E6352_G1_VTU_SID, val);
}

/* Offset 0x05: VTU Operation Register */

static int mv88e6xxx_g1_vtu_op_wait(struct mv88e6xxx_chip *chip)
{
	int bit = __bf_shf(MV88E6XXX_G1_VTU_OP_BUSY);

	return mv88e6xxx_g1_wait_bit(chip, MV88E6XXX_G1_VTU_OP, bit, 0);
}

static int mv88e6xxx_g1_vtu_op(struct mv88e6xxx_chip *chip, u16 op)
{
	int err;

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_VTU_OP,
				 MV88E6XXX_G1_VTU_OP_BUSY | op);
	if (err)
		return err;

	return mv88e6xxx_g1_vtu_op_wait(chip);
}

/* Offset 0x06: VTU VID Register */

static int mv88e6xxx_g1_vtu_vid_read(struct mv88e6xxx_chip *chip,
				     bool *valid, u16 *vid)
{
	u16 val;
	int err;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_VTU_VID, &val);
	if (err)
		return err;

	if (vid) {
		*vid = val & 0xfff;

		if (val & MV88E6390_G1_VTU_VID_PAGE)
			*vid |= 0x1000;
	}

	if (valid)
		*valid = !!(val & MV88E6XXX_G1_VTU_VID_VALID);

	return 0;
}

static int mv88e6xxx_g1_vtu_vid_write(struct mv88e6xxx_chip *chip,
				      bool valid, u16 vid)
{
	u16 val = vid & 0xfff;

	if (vid & 0x1000)
		val |= MV88E6390_G1_VTU_VID_PAGE;

	if (valid)
		val |= MV88E6XXX_G1_VTU_VID_VALID;

	return mv88e6xxx_g1_write(chip, MV88E6XXX_G1_VTU_VID, val);
}

/* Offset 0x07: VTU/STU Data Register 1
 * Offset 0x08: VTU/STU Data Register 2
 * Offset 0x09: VTU/STU Data Register 3
 */
static int mv88e6185_g1_vtu_stu_data_read(struct mv88e6xxx_chip *chip,
					  u16 *regs)
{
	int i;

	/* Read all 3 VTU/STU Data registers */
	for (i = 0; i < 3; ++i) {
		u16 *reg = &regs[i];
		int err;

		err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_VTU_DATA1 + i, reg);
		if (err)
			return err;
	}

	return 0;
}

static int mv88e6185_g1_vtu_data_read(struct mv88e6xxx_chip *chip,
				      u8 *member, u8 *state)
{
	u16 regs[3];
	int err;
	int i;

	err = mv88e6185_g1_vtu_stu_data_read(chip, regs);
	if (err)
		return err;

	/* Extract MemberTag data */
	for (i = 0; i < mv88e6xxx_num_ports(chip); ++i) {
		unsigned int member_offset = (i % 4) * 4;
		unsigned int state_offset = member_offset + 2;

		if (member)
			member[i] = (regs[i / 4] >> member_offset) & 0x3;

		if (state)
			state[i] = (regs[i / 4] >> state_offset) & 0x3;
	}

	return 0;
}

static int mv88e6185_g1_vtu_data_write(struct mv88e6xxx_chip *chip,
				       u8 *member, u8 *state)
{
	u16 regs[3] = { 0 };
	int i;

	/* Insert MemberTag and PortState data */
	for (i = 0; i < mv88e6xxx_num_ports(chip); ++i) {
		unsigned int member_offset = (i % 4) * 4;
		unsigned int state_offset = member_offset + 2;

		if (member)
			regs[i / 4] |= (member[i] & 0x3) << member_offset;

		if (state)
			regs[i / 4] |= (state[i] & 0x3) << state_offset;
	}

	/* Write all 3 VTU/STU Data registers */
	for (i = 0; i < 3; ++i) {
		u16 reg = regs[i];
		int err;

		err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_VTU_DATA1 + i, reg);
		if (err)
			return err;
	}

	return 0;
}

static int mv88e6390_g1_vtu_data_read(struct mv88e6xxx_chip *chip, u8 *data)
{
	u16 regs[2];
	int i;

	/* Read the 2 VTU/STU Data registers */
	for (i = 0; i < 2; ++i) {
		u16 *reg = &regs[i];
		int err;

		err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_VTU_DATA1 + i, reg);
		if (err)
			return err;
	}

	/* Extract data */
	for (i = 0; i < mv88e6xxx_num_ports(chip); ++i) {
		unsigned int offset = (i % 8) * 2;

		data[i] = (regs[i / 8] >> offset) & 0x3;
	}

	return 0;
}

static int mv88e6390_g1_vtu_data_write(struct mv88e6xxx_chip *chip, u8 *data)
{
	u16 regs[2] = { 0 };
	int i;

	/* Insert data */
	for (i = 0; i < mv88e6xxx_num_ports(chip); ++i) {
		unsigned int offset = (i % 8) * 2;

		regs[i / 8] |= (data[i] & 0x3) << offset;
	}

	/* Write the 2 VTU/STU Data registers */
	for (i = 0; i < 2; ++i) {
		u16 reg = regs[i];
		int err;

		err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_VTU_DATA1 + i, reg);
		if (err)
			return err;
	}

	return 0;
}

/* VLAN Translation Unit Operations */

int mv88e6xxx_g1_vtu_getnext(struct mv88e6xxx_chip *chip,
			     struct mv88e6xxx_vtu_entry *entry)
{
	int err;

	err = mv88e6xxx_g1_vtu_op_wait(chip);
	if (err)
		return err;

	/* To get the next higher active VID, the VTU GetNext operation can be
	 * started again without setting the VID registers since it already
	 * contains the last VID.
	 *
	 * To save a few hardware accesses and abstract this to the caller,
	 * write the VID only once, when the entry is given as invalid.
	 */
	if (!entry->valid) {
		err = mv88e6xxx_g1_vtu_vid_write(chip, false, entry->vid);
		if (err)
			return err;
	}

	err = mv88e6xxx_g1_vtu_op(chip, MV88E6XXX_G1_VTU_OP_VTU_GET_NEXT);
	if (err)
		return err;

	return mv88e6xxx_g1_vtu_vid_read(chip, &entry->valid, &entry->vid);
}

int mv88e6185_g1_vtu_getnext(struct mv88e6xxx_chip *chip,
			     struct mv88e6xxx_vtu_entry *entry)
{
	u16 val;
	int err;

	err = mv88e6xxx_g1_vtu_getnext(chip, entry);
	if (err)
		return err;

	if (entry->valid) {
		err = mv88e6185_g1_vtu_data_read(chip, entry->member, entry->state);
		if (err)
			return err;

		/* VTU DBNum[3:0] are located in VTU Operation 3:0
		 * VTU DBNum[7:4] ([5:4] for 6250) are located in VTU Operation 11:8 (9:8)
		 */
		err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_VTU_OP, &val);
		if (err)
			return err;

		entry->fid = val & 0x000f;
		entry->fid |= (val & 0x0f00) >> 4;
		entry->fid &= mv88e6xxx_num_databases(chip) - 1;
	}

	return 0;
}

int mv88e6352_g1_vtu_getnext(struct mv88e6xxx_chip *chip,
			     struct mv88e6xxx_vtu_entry *entry)
{
	int err;

	/* Fetch VLAN MemberTag data from the VTU */
	err = mv88e6xxx_g1_vtu_getnext(chip, entry);
	if (err)
		return err;

	if (entry->valid) {
		err = mv88e6185_g1_vtu_data_read(chip, entry->member, NULL);
		if (err)
			return err;

		err = mv88e6xxx_g1_vtu_fid_read(chip, entry);
		if (err)
			return err;

		err = mv88e6xxx_g1_vtu_sid_read(chip, &entry->sid);
		if (err)
			return err;
	}

	return 0;
}

int mv88e6390_g1_vtu_getnext(struct mv88e6xxx_chip *chip,
			     struct mv88e6xxx_vtu_entry *entry)
{
	int err;

	/* Fetch VLAN MemberTag data from the VTU */
	err = mv88e6xxx_g1_vtu_getnext(chip, entry);
	if (err)
		return err;

	if (entry->valid) {
		err = mv88e6390_g1_vtu_data_read(chip, entry->member);
		if (err)
			return err;

		err = mv88e6xxx_g1_vtu_fid_read(chip, entry);
		if (err)
			return err;

		err = mv88e6xxx_g1_vtu_sid_read(chip, &entry->sid);
		if (err)
			return err;
	}

	return 0;
}

int mv88e6185_g1_vtu_loadpurge(struct mv88e6xxx_chip *chip,
			       struct mv88e6xxx_vtu_entry *entry)
{
	u16 op = MV88E6XXX_G1_VTU_OP_VTU_LOAD_PURGE;
	int err;

	err = mv88e6xxx_g1_vtu_op_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g1_vtu_vid_write(chip, entry->valid, entry->vid);
	if (err)
		return err;

	if (entry->valid) {
		err = mv88e6185_g1_vtu_data_write(chip, entry->member, entry->state);
		if (err)
			return err;

		/* VTU DBNum[3:0] are located in VTU Operation 3:0
		 * VTU DBNum[7:4] are located in VTU Operation 11:8
		 *
		 * For the 6250/6220, the latter are really [5:4] and
		 * 9:8, but in those cases bits 7:6 of entry->fid are
		 * 0 since they have num_databases = 64.
		 */
		op |= entry->fid & 0x000f;
		op |= (entry->fid & 0x00f0) << 4;
	}

	return mv88e6xxx_g1_vtu_op(chip, op);
}

int mv88e6352_g1_vtu_loadpurge(struct mv88e6xxx_chip *chip,
			       struct mv88e6xxx_vtu_entry *entry)
{
	int err;

	err = mv88e6xxx_g1_vtu_op_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g1_vtu_vid_write(chip, entry->valid, entry->vid);
	if (err)
		return err;

	if (entry->valid) {
		/* Write MemberTag data */
		err = mv88e6185_g1_vtu_data_write(chip, entry->member, NULL);
		if (err)
			return err;

		err = mv88e6xxx_g1_vtu_fid_write(chip, entry);
		if (err)
			return err;

		err = mv88e6xxx_g1_vtu_sid_write(chip, entry->sid);
		if (err)
			return err;
	}

	/* Load/Purge VTU entry */
	return mv88e6xxx_g1_vtu_op(chip, MV88E6XXX_G1_VTU_OP_VTU_LOAD_PURGE);
}

int mv88e6390_g1_vtu_loadpurge(struct mv88e6xxx_chip *chip,
			       struct mv88e6xxx_vtu_entry *entry)
{
	int err;

	err = mv88e6xxx_g1_vtu_op_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g1_vtu_vid_write(chip, entry->valid, entry->vid);
	if (err)
		return err;

	if (entry->valid) {
		/* Write MemberTag data */
		err = mv88e6390_g1_vtu_data_write(chip, entry->member);
		if (err)
			return err;

		err = mv88e6xxx_g1_vtu_fid_write(chip, entry);
		if (err)
			return err;

		err = mv88e6xxx_g1_vtu_sid_write(chip, entry->sid);
		if (err)
			return err;
	}

	/* Load/Purge VTU entry */
	return mv88e6xxx_g1_vtu_op(chip, MV88E6XXX_G1_VTU_OP_VTU_LOAD_PURGE);
}

int mv88e6xxx_g1_vtu_flush(struct mv88e6xxx_chip *chip)
{
	int err;

	err = mv88e6xxx_g1_vtu_op_wait(chip);
	if (err)
		return err;

	return mv88e6xxx_g1_vtu_op(chip, MV88E6XXX_G1_VTU_OP_FLUSH_ALL);
}

/* Spanning Tree Unit Operations */

int mv88e6xxx_g1_stu_getnext(struct mv88e6xxx_chip *chip,
			     struct mv88e6xxx_stu_entry *entry)
{
	int err;

	err = mv88e6xxx_g1_vtu_op_wait(chip);
	if (err)
		return err;

	/* To get the next higher active SID, the STU GetNext operation can be
	 * started again without setting the SID registers since it already
	 * contains the last SID.
	 *
	 * To save a few hardware accesses and abstract this to the caller,
	 * write the SID only once, when the entry is given as invalid.
	 */
	if (!entry->valid) {
		err = mv88e6xxx_g1_vtu_sid_write(chip, entry->sid);
		if (err)
			return err;
	}

	err = mv88e6xxx_g1_vtu_op(chip, MV88E6XXX_G1_VTU_OP_STU_GET_NEXT);
	if (err)
		return err;

	err = mv88e6xxx_g1_vtu_vid_read(chip, &entry->valid, NULL);
	if (err)
		return err;

	if (entry->valid) {
		err = mv88e6xxx_g1_vtu_sid_read(chip, &entry->sid);
		if (err)
			return err;
	}

	return 0;
}

int mv88e6352_g1_stu_getnext(struct mv88e6xxx_chip *chip,
			     struct mv88e6xxx_stu_entry *entry)
{
	int err;

	err = mv88e6xxx_g1_stu_getnext(chip, entry);
	if (err)
		return err;

	if (!entry->valid)
		return 0;

	return mv88e6185_g1_vtu_data_read(chip, NULL, entry->state);
}

int mv88e6390_g1_stu_getnext(struct mv88e6xxx_chip *chip,
			     struct mv88e6xxx_stu_entry *entry)
{
	int err;

	err = mv88e6xxx_g1_stu_getnext(chip, entry);
	if (err)
		return err;

	if (!entry->valid)
		return 0;

	return mv88e6390_g1_vtu_data_read(chip, entry->state);
}

int mv88e6352_g1_stu_loadpurge(struct mv88e6xxx_chip *chip,
			       struct mv88e6xxx_stu_entry *entry)
{
	int err;

	err = mv88e6xxx_g1_vtu_op_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g1_vtu_vid_write(chip, entry->valid, 0);
	if (err)
		return err;

	err = mv88e6xxx_g1_vtu_sid_write(chip, entry->sid);
	if (err)
		return err;

	if (entry->valid) {
		err = mv88e6185_g1_vtu_data_write(chip, NULL, entry->state);
		if (err)
			return err;
	}

	/* Load/Purge STU entry */
	return mv88e6xxx_g1_vtu_op(chip, MV88E6XXX_G1_VTU_OP_STU_LOAD_PURGE);
}

int mv88e6390_g1_stu_loadpurge(struct mv88e6xxx_chip *chip,
			       struct mv88e6xxx_stu_entry *entry)
{
	int err;

	err = mv88e6xxx_g1_vtu_op_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g1_vtu_vid_write(chip, entry->valid, 0);
	if (err)
		return err;

	err = mv88e6xxx_g1_vtu_sid_write(chip, entry->sid);
	if (err)
		return err;

	if (entry->valid) {
		err = mv88e6390_g1_vtu_data_write(chip, entry->state);
		if (err)
			return err;
	}

	/* Load/Purge STU entry */
	return mv88e6xxx_g1_vtu_op(chip, MV88E6XXX_G1_VTU_OP_STU_LOAD_PURGE);
}

/* VTU Violation Management */

static irqreturn_t mv88e6xxx_g1_vtu_prob_irq_thread_fn(int irq, void *dev_id)
{
	struct mv88e6xxx_chip *chip = dev_id;
	u16 val, vid;
	int spid;
	int err;

	mv88e6xxx_reg_lock(chip);

	err = mv88e6xxx_g1_vtu_op(chip, MV88E6XXX_G1_VTU_OP_GET_CLR_VIOLATION);
	if (err)
		goto out;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_VTU_OP, &val);
	if (err)
		goto out;

	err = mv88e6xxx_g1_vtu_vid_read(chip, NULL, &vid);
	if (err)
		goto out;

	spid = val & MV88E6XXX_G1_VTU_OP_SPID_MASK;

	if (val & MV88E6XXX_G1_VTU_OP_MEMBER_VIOLATION) {
		trace_mv88e6xxx_vtu_member_violation(chip->dev, spid, vid);
		chip->ports[spid].vtu_member_violation++;
	}

	if (val & MV88E6XXX_G1_VTU_OP_MISS_VIOLATION) {
		trace_mv88e6xxx_vtu_miss_violation(chip->dev, spid, vid);
		chip->ports[spid].vtu_miss_violation++;
	}

	mv88e6xxx_reg_unlock(chip);

	return IRQ_HANDLED;

out:
	mv88e6xxx_reg_unlock(chip);

	dev_err(chip->dev, "VTU problem: error %d while handling interrupt\n",
		err);

	return IRQ_HANDLED;
}

int mv88e6xxx_g1_vtu_prob_irq_setup(struct mv88e6xxx_chip *chip)
{
	int err;

	chip->vtu_prob_irq = irq_find_mapping(chip->g1_irq.domain,
					      MV88E6XXX_G1_STS_IRQ_VTU_PROB);
	if (chip->vtu_prob_irq < 0)
		return chip->vtu_prob_irq;

	snprintf(chip->vtu_prob_irq_name, sizeof(chip->vtu_prob_irq_name),
		 "mv88e6xxx-%s-g1-vtu-prob", dev_name(chip->dev));

	err = request_threaded_irq(chip->vtu_prob_irq, NULL,
				   mv88e6xxx_g1_vtu_prob_irq_thread_fn,
				   IRQF_ONESHOT, chip->vtu_prob_irq_name,
				   chip);
	if (err)
		irq_dispose_mapping(chip->vtu_prob_irq);

	return err;
}

void mv88e6xxx_g1_vtu_prob_irq_free(struct mv88e6xxx_chip *chip)
{
	free_irq(chip->vtu_prob_irq, chip);
	irq_dispose_mapping(chip->vtu_prob_irq);
}
