// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88E6xxx Address Translation Unit (ATU) support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 * Copyright (c) 2017 Savoir-faire Linux, Inc.
 */

#include <linux/bitfield.h>
#include <linux/interrupt.h>
#include <linux/irqdomain.h>

#include "chip.h"
#include "global1.h"
#include "trace.h"

/* Offset 0x01: ATU FID Register */

static int mv88e6xxx_g1_atu_fid_write(struct mv88e6xxx_chip *chip, u16 fid)
{
	return mv88e6xxx_g1_write(chip, MV88E6352_G1_ATU_FID, fid & 0xfff);
}

/* Offset 0x0A: ATU Control Register */

int mv88e6xxx_g1_atu_set_learn2all(struct mv88e6xxx_chip *chip, bool learn2all)
{
	u16 val;
	int err;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_ATU_CTL, &val);
	if (err)
		return err;

	if (learn2all)
		val |= MV88E6XXX_G1_ATU_CTL_LEARN2ALL;
	else
		val &= ~MV88E6XXX_G1_ATU_CTL_LEARN2ALL;

	return mv88e6xxx_g1_write(chip, MV88E6XXX_G1_ATU_CTL, val);
}

int mv88e6xxx_g1_atu_set_age_time(struct mv88e6xxx_chip *chip,
				  unsigned int msecs)
{
	const unsigned int coeff = chip->info->age_time_coeff;
	const unsigned int min = 0x01 * coeff;
	const unsigned int max = 0xff * coeff;
	u8 age_time;
	u16 val;
	int err;

	if (msecs < min || msecs > max)
		return -ERANGE;

	/* Round to nearest multiple of coeff */
	age_time = (msecs + coeff / 2) / coeff;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_ATU_CTL, &val);
	if (err)
		return err;

	/* AgeTime is 11:4 bits */
	val &= ~0xff0;
	val |= age_time << 4;

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_ATU_CTL, val);
	if (err)
		return err;

	dev_dbg(chip->dev, "AgeTime set to 0x%02x (%d ms)\n", age_time,
		age_time * coeff);

	return 0;
}

int mv88e6165_g1_atu_get_hash(struct mv88e6xxx_chip *chip, u8 *hash)
{
	int err;
	u16 val;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_ATU_CTL, &val);
	if (err)
		return err;

	*hash = val & MV88E6161_G1_ATU_CTL_HASH_MASK;

	return 0;
}

int mv88e6165_g1_atu_set_hash(struct mv88e6xxx_chip *chip, u8 hash)
{
	int err;
	u16 val;

	if (hash & ~MV88E6161_G1_ATU_CTL_HASH_MASK)
		return -EINVAL;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_ATU_CTL, &val);
	if (err)
		return err;

	val &= ~MV88E6161_G1_ATU_CTL_HASH_MASK;
	val |= hash;

	return mv88e6xxx_g1_write(chip, MV88E6XXX_G1_ATU_CTL, val);
}

/* Offset 0x0B: ATU Operation Register */

static int mv88e6xxx_g1_atu_op_wait(struct mv88e6xxx_chip *chip)
{
	int bit = __bf_shf(MV88E6XXX_G1_ATU_OP_BUSY);

	return mv88e6xxx_g1_wait_bit(chip, MV88E6XXX_G1_ATU_OP, bit, 0);
}

static int mv88e6xxx_g1_read_atu_violation(struct mv88e6xxx_chip *chip)
{
	int err;

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_ATU_OP,
				 MV88E6XXX_G1_ATU_OP_BUSY |
				 MV88E6XXX_G1_ATU_OP_GET_CLR_VIOLATION);
	if (err)
		return err;

	return mv88e6xxx_g1_atu_op_wait(chip);
}

static int mv88e6xxx_g1_atu_op(struct mv88e6xxx_chip *chip, u16 fid, u16 op)
{
	u16 val;
	int err;

	/* FID bits are dispatched all around gradually as more are supported */
	if (mv88e6xxx_num_databases(chip) > 256) {
		err = mv88e6xxx_g1_atu_fid_write(chip, fid);
		if (err)
			return err;
	} else {
		if (mv88e6xxx_num_databases(chip) > 64) {
			/* ATU DBNum[7:4] are located in ATU Control 15:12 */
			err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_ATU_CTL,
						&val);
			if (err)
				return err;

			val = (val & 0x0fff) | ((fid << 8) & 0xf000);
			err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_ATU_CTL,
						 val);
			if (err)
				return err;
		} else if (mv88e6xxx_num_databases(chip) > 16) {
			/* ATU DBNum[5:4] are located in ATU Operation 9:8 */
			op |= (fid & 0x30) << 4;
		}

		/* ATU DBNum[3:0] are located in ATU Operation 3:0 */
		op |= fid & 0xf;
	}

	err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_ATU_OP,
				 MV88E6XXX_G1_ATU_OP_BUSY | op);
	if (err)
		return err;

	return mv88e6xxx_g1_atu_op_wait(chip);
}

int mv88e6xxx_g1_atu_get_next(struct mv88e6xxx_chip *chip, u16 fid)
{
	return mv88e6xxx_g1_atu_op(chip, fid, MV88E6XXX_G1_ATU_OP_GET_NEXT_DB);
}

static int mv88e6xxx_g1_atu_fid_read(struct mv88e6xxx_chip *chip, u16 *fid)
{
	u16 val = 0, upper = 0, op = 0;
	int err = -EOPNOTSUPP;

	if (mv88e6xxx_num_databases(chip) > 256) {
		err = mv88e6xxx_g1_read(chip, MV88E6352_G1_ATU_FID, &val);
		val &= 0xfff;
		if (err)
			return err;
	} else {
		err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_ATU_OP, &op);
		if (err)
			return err;
		if (mv88e6xxx_num_databases(chip) > 64) {
			/* ATU DBNum[7:4] are located in ATU Control 15:12 */
			err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_ATU_CTL,
						&upper);
			if (err)
				return err;

			upper = (upper >> 8) & 0x00f0;
		} else if (mv88e6xxx_num_databases(chip) > 16) {
			/* ATU DBNum[5:4] are located in ATU Operation 9:8 */
			upper = (op >> 4) & 0x30;
		}

		/* ATU DBNum[3:0] are located in ATU Operation 3:0 */
		val = (op & 0xf) | upper;
	}
	*fid = val;

	return err;
}

/* Offset 0x0C: ATU Data Register */

static int mv88e6xxx_g1_atu_data_read(struct mv88e6xxx_chip *chip,
				      struct mv88e6xxx_atu_entry *entry)
{
	u16 val;
	int err;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_ATU_DATA, &val);
	if (err)
		return err;

	entry->state = val & 0xf;
	if (entry->state) {
		entry->trunk = !!(val & MV88E6XXX_G1_ATU_DATA_TRUNK);
		entry->portvec = (val >> 4) & mv88e6xxx_port_mask(chip);
	}

	return 0;
}

static int mv88e6xxx_g1_atu_data_write(struct mv88e6xxx_chip *chip,
				       struct mv88e6xxx_atu_entry *entry)
{
	u16 data = entry->state & 0xf;

	if (entry->state) {
		if (entry->trunk)
			data |= MV88E6XXX_G1_ATU_DATA_TRUNK;

		data |= (entry->portvec & mv88e6xxx_port_mask(chip)) << 4;
	}

	return mv88e6xxx_g1_write(chip, MV88E6XXX_G1_ATU_DATA, data);
}

/* Offset 0x0D: ATU MAC Address Register Bytes 0 & 1
 * Offset 0x0E: ATU MAC Address Register Bytes 2 & 3
 * Offset 0x0F: ATU MAC Address Register Bytes 4 & 5
 */

static int mv88e6xxx_g1_atu_mac_read(struct mv88e6xxx_chip *chip,
				     struct mv88e6xxx_atu_entry *entry)
{
	u16 val;
	int i, err;

	for (i = 0; i < 3; i++) {
		err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_ATU_MAC01 + i, &val);
		if (err)
			return err;

		entry->mac[i * 2] = val >> 8;
		entry->mac[i * 2 + 1] = val & 0xff;
	}

	return 0;
}

static int mv88e6xxx_g1_atu_mac_write(struct mv88e6xxx_chip *chip,
				      struct mv88e6xxx_atu_entry *entry)
{
	u16 val;
	int i, err;

	for (i = 0; i < 3; i++) {
		val = (entry->mac[i * 2] << 8) | entry->mac[i * 2 + 1];
		err = mv88e6xxx_g1_write(chip, MV88E6XXX_G1_ATU_MAC01 + i, val);
		if (err)
			return err;
	}

	return 0;
}

/* Address Translation Unit operations */

int mv88e6xxx_g1_atu_getnext(struct mv88e6xxx_chip *chip, u16 fid,
			     struct mv88e6xxx_atu_entry *entry)
{
	int err;

	err = mv88e6xxx_g1_atu_op_wait(chip);
	if (err)
		return err;

	/* Write the MAC address to iterate from only once */
	if (!entry->state) {
		err = mv88e6xxx_g1_atu_mac_write(chip, entry);
		if (err)
			return err;
	}

	err = mv88e6xxx_g1_atu_op(chip, fid, MV88E6XXX_G1_ATU_OP_GET_NEXT_DB);
	if (err)
		return err;

	err = mv88e6xxx_g1_atu_data_read(chip, entry);
	if (err)
		return err;

	return mv88e6xxx_g1_atu_mac_read(chip, entry);
}

int mv88e6xxx_g1_atu_loadpurge(struct mv88e6xxx_chip *chip, u16 fid,
			       struct mv88e6xxx_atu_entry *entry)
{
	int err;

	err = mv88e6xxx_g1_atu_op_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g1_atu_mac_write(chip, entry);
	if (err)
		return err;

	err = mv88e6xxx_g1_atu_data_write(chip, entry);
	if (err)
		return err;

	return mv88e6xxx_g1_atu_op(chip, fid, MV88E6XXX_G1_ATU_OP_LOAD_DB);
}

static int mv88e6xxx_g1_atu_flushmove(struct mv88e6xxx_chip *chip, u16 fid,
				      struct mv88e6xxx_atu_entry *entry,
				      bool all)
{
	u16 op;
	int err;

	err = mv88e6xxx_g1_atu_op_wait(chip);
	if (err)
		return err;

	err = mv88e6xxx_g1_atu_data_write(chip, entry);
	if (err)
		return err;

	/* Flush/Move all or non-static entries from all or a given database */
	if (all && fid)
		op = MV88E6XXX_G1_ATU_OP_FLUSH_MOVE_ALL_DB;
	else if (fid)
		op = MV88E6XXX_G1_ATU_OP_FLUSH_MOVE_NON_STATIC_DB;
	else if (all)
		op = MV88E6XXX_G1_ATU_OP_FLUSH_MOVE_ALL;
	else
		op = MV88E6XXX_G1_ATU_OP_FLUSH_MOVE_NON_STATIC;

	return mv88e6xxx_g1_atu_op(chip, fid, op);
}

int mv88e6xxx_g1_atu_flush(struct mv88e6xxx_chip *chip, u16 fid, bool all)
{
	struct mv88e6xxx_atu_entry entry = {
		.state = 0, /* Null EntryState means Flush */
	};

	return mv88e6xxx_g1_atu_flushmove(chip, fid, &entry, all);
}

static int mv88e6xxx_g1_atu_move(struct mv88e6xxx_chip *chip, u16 fid,
				 int from_port, int to_port, bool all)
{
	struct mv88e6xxx_atu_entry entry = { 0 };
	unsigned long mask;
	int shift;

	if (!chip->info->atu_move_port_mask)
		return -EOPNOTSUPP;

	mask = chip->info->atu_move_port_mask;
	shift = bitmap_weight(&mask, 16);

	entry.state = 0xf; /* Full EntryState means Move */
	entry.portvec = from_port & mask;
	entry.portvec |= (to_port & mask) << shift;

	return mv88e6xxx_g1_atu_flushmove(chip, fid, &entry, all);
}

int mv88e6xxx_g1_atu_remove(struct mv88e6xxx_chip *chip, u16 fid, int port,
			    bool all)
{
	int from_port = port;
	int to_port = chip->info->atu_move_port_mask;

	return mv88e6xxx_g1_atu_move(chip, fid, from_port, to_port, all);
}

static irqreturn_t mv88e6xxx_g1_atu_prob_irq_thread_fn(int irq, void *dev_id)
{
	struct mv88e6xxx_chip *chip = dev_id;
	struct mv88e6xxx_atu_entry entry;
	int err, spid;
	u16 val, fid;

	mv88e6xxx_reg_lock(chip);

	err = mv88e6xxx_g1_read_atu_violation(chip);
	if (err)
		goto out;

	err = mv88e6xxx_g1_read(chip, MV88E6XXX_G1_ATU_OP, &val);
	if (err)
		goto out;

	err = mv88e6xxx_g1_atu_fid_read(chip, &fid);
	if (err)
		goto out;

	err = mv88e6xxx_g1_atu_data_read(chip, &entry);
	if (err)
		goto out;

	err = mv88e6xxx_g1_atu_mac_read(chip, &entry);
	if (err)
		goto out;

	spid = entry.state;

	if (val & MV88E6XXX_G1_ATU_OP_AGE_OUT_VIOLATION) {
		dev_err_ratelimited(chip->dev,
				    "ATU age out violation for %pM\n",
				    entry.mac);
	}

	if (val & MV88E6XXX_G1_ATU_OP_MEMBER_VIOLATION) {
		trace_mv88e6xxx_atu_member_violation(chip->dev, spid,
						     entry.portvec, entry.mac,
						     fid);
		chip->ports[spid].atu_member_violation++;
	}

	if (val & MV88E6XXX_G1_ATU_OP_MISS_VIOLATION) {
		trace_mv88e6xxx_atu_miss_violation(chip->dev, spid,
						   entry.portvec, entry.mac,
						   fid);
		chip->ports[spid].atu_miss_violation++;
	}

	if (val & MV88E6XXX_G1_ATU_OP_FULL_VIOLATION) {
		trace_mv88e6xxx_atu_full_violation(chip->dev, spid,
						   entry.portvec, entry.mac,
						   fid);
		chip->ports[spid].atu_full_violation++;
	}
	mv88e6xxx_reg_unlock(chip);

	return IRQ_HANDLED;

out:
	mv88e6xxx_reg_unlock(chip);

	dev_err(chip->dev, "ATU problem: error %d while handling interrupt\n",
		err);
	return IRQ_HANDLED;
}

int mv88e6xxx_g1_atu_prob_irq_setup(struct mv88e6xxx_chip *chip)
{
	int err;

	chip->atu_prob_irq = irq_find_mapping(chip->g1_irq.domain,
					      MV88E6XXX_G1_STS_IRQ_ATU_PROB);
	if (chip->atu_prob_irq < 0)
		return chip->atu_prob_irq;

	snprintf(chip->atu_prob_irq_name, sizeof(chip->atu_prob_irq_name),
		 "mv88e6xxx-%s-g1-atu-prob", dev_name(chip->dev));

	err = request_threaded_irq(chip->atu_prob_irq, NULL,
				   mv88e6xxx_g1_atu_prob_irq_thread_fn,
				   IRQF_ONESHOT, chip->atu_prob_irq_name,
				   chip);
	if (err)
		irq_dispose_mapping(chip->atu_prob_irq);

	return err;
}

void mv88e6xxx_g1_atu_prob_irq_free(struct mv88e6xxx_chip *chip)
{
	free_irq(chip->atu_prob_irq, chip);
	irq_dispose_mapping(chip->atu_prob_irq);
}
