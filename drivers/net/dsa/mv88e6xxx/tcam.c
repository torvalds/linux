// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Marvell 88E6xxx Switch TCAM support
 *
 * Copyright (c) 2026 Luminex Network Intelligence
 */

#include "linux/list.h"

#include "chip.h"
#include "tcam.h"

/* TCAM operatation register */
#define MV88E6XXX_TCAM_OP			0x00
#define MV88E6XXX_TCAM_OP_BUSY			0x8000
#define MV88E6XXX_TCAM_OP_OP_MASK		0x7000
#define MV88E6XXX_TCAM_OP_OP_FLUSH_ALL		0x1000
#define MV88E6XXX_TCAM_OP_OP_FLUSH		0x2000
#define MV88E6XXX_TCAM_OP_OP_LOAD		0x3000
#define MV88E6XXX_TCAM_OP_OP_GET_NEXT		0x4000
#define MV88E6XXX_TCAM_OP_OP_READ		0x5000

/* TCAM extension register */
#define MV88E6XXX_TCAM_EXTENSION		0x01

/* TCAM keys register 1 */
#define MV88E6XXX_TCAM_KEYS1			0x02
#define MV88E6XXX_TCAM_KEYS1_FT_MASK		0xC000
#define MV88E6XXX_TCAM_KEYS1_SPV_MASK		0x0007
#define MV88E6XXX_TCAM_KEYS1_SPV_MASK_MASK	0x0700

/* TCAM keys register 2 */
#define MV88E6XXX_TCAM_KEYS2			0x03
#define MV88E6XXX_TCAM_KEYS2_SPV_MASK		0x00ff
#define MV88E6XXX_TCAM_KEYS2_SPV_MASK_MASK	0xff00

#define MV88E6XXX_TCAM_ING_ACT3			0x04
#define MV88E6XXX_TCAM_ING_ACT3_SF		0x0800
#define MV88E6XXX_TCAM_ING_ACT3_DPV_MASK	0x07ff

#define MV88E6XXX_TCAM_ING_ACT5			0x06
#define MV88E6XXX_TCAM_ING_ACT5_DPV_MODE_MASK	0xc000

static int mv88e6xxx_tcam_write(struct mv88e6xxx_chip *chip, int reg, u16 val)
{
	return mv88e6xxx_write(chip, chip->info->tcam_addr, reg, val);
}

static int mv88e6xxx_tcam_wait(struct mv88e6xxx_chip *chip)
{
	int bit = __bf_shf(MV88E6XXX_TCAM_OP_BUSY);

	return mv88e6xxx_wait_bit(chip, chip->info->tcam_addr,
				  MV88E6XXX_TCAM_OP, bit, 0);
}

static int mv88e6xxx_tcam_read_page(struct mv88e6xxx_chip *chip, u8 page,
				    u8 entry)

{
	u16 val = MV88E6XXX_TCAM_OP_BUSY | MV88E6XXX_TCAM_OP_OP_READ |
		  (page & 0x3) << 10 | entry;
	int err;

	err = mv88e6xxx_tcam_write(chip, MV88E6XXX_TCAM_OP, val);
	if (err)
		return err;

	return mv88e6xxx_tcam_wait(chip);
}

static int mv88e6xxx_tcam_load_page(struct mv88e6xxx_chip *chip, u8 page,
				    u8 entry)
{
	u16 val = MV88E6XXX_TCAM_OP_BUSY | MV88E6XXX_TCAM_OP_OP_LOAD |
		  (page & 0x3) << 10 | entry;
	int err;

	err = mv88e6xxx_tcam_write(chip, MV88E6XXX_TCAM_OP, val);
	if (err)
		return err;

	return mv88e6xxx_tcam_wait(chip);
}

static int mv88e6xxx_tcam_flush_entry(struct mv88e6xxx_chip *chip, u8 entry)
{
	u16 val = MV88E6XXX_TCAM_OP_BUSY | MV88E6XXX_TCAM_OP_OP_FLUSH | entry;
	int err;

	err = mv88e6xxx_tcam_write(chip, MV88E6XXX_TCAM_OP, val);
	if (err)
		return err;

	return mv88e6xxx_tcam_wait(chip);
}

static int mv88e6xxx_tcam_flush_all(struct mv88e6xxx_chip *chip)
{
	u16 val = MV88E6XXX_TCAM_OP_BUSY | MV88E6XXX_TCAM_OP_OP_FLUSH_ALL;
	int err;

	err = mv88e6xxx_tcam_write(chip, MV88E6XXX_TCAM_OP, val);
	if (err)
		return err;

	return mv88e6xxx_tcam_wait(chip);
}

struct mv88e6xxx_tcam_entry *
mv88e6xxx_tcam_entry_find(struct mv88e6xxx_chip *chip, unsigned long cookie)
{
	struct mv88e6xxx_tcam_entry *entry;

	list_for_each_entry(entry, &chip->tcam.entries, list)
		if (entry->cookie == cookie)
			return entry;

	return NULL;
}

/* insert tcam entry in ordered list and move existing entries if necessary */
static int mv88e6xxx_tcam_insert_entry(struct mv88e6xxx_chip *chip,
				       struct mv88e6xxx_tcam_entry *entry)
{
	struct mv88e6xxx_tcam_entry *elem;
	struct list_head *hpos;
	int err;

	list_for_each_prev(hpos, &chip->tcam.entries) {
		u8 move_idx;

		elem = list_entry(hpos, struct mv88e6xxx_tcam_entry, list);
		if (entry->prio >= elem->prio)
			break;

		move_idx = elem->hw_idx + 1;

		err = mv88e6xxx_tcam_flush_entry(chip, move_idx);
		if (err)
			return err;

		err = chip->info->ops->tcam_ops->entry_add(chip, elem,
							   move_idx);
		if (err)
			return err;

		elem->hw_idx = move_idx;
	}

	if (list_is_head(hpos, &chip->tcam.entries)) {
		entry->hw_idx = 0;
	} else {
		elem = list_entry(hpos, struct mv88e6xxx_tcam_entry, list);
		entry->hw_idx = elem->hw_idx + 1;
	}
	list_add(&entry->list, hpos);
	return 0;
}

int mv88e6xxx_tcam_entry_add(struct mv88e6xxx_chip *chip,
			     struct mv88e6xxx_tcam_entry *entry)
{
	int err;
	struct mv88e6xxx_tcam_entry *last;

	last = list_last_entry_or_null(&chip->tcam.entries,
				       struct mv88e6xxx_tcam_entry, list);
	if (last && last->hw_idx == chip->info->num_tcam_entries - 1)
		return -ENOSPC;

	err = mv88e6xxx_tcam_insert_entry(chip, entry);
	if (err)
		return err;

	err = mv88e6xxx_tcam_flush_entry(chip, entry->hw_idx);
	if (err)
		goto unlink_out;

	err = chip->info->ops->tcam_ops->entry_add(chip, entry, entry->hw_idx);
	if (err)
		goto unlink_out;

	return 0;

unlink_out:
	list_del(&entry->list);
	return err;
}

int mv88e6xxx_tcam_entry_del(struct mv88e6xxx_chip *chip,
			     struct mv88e6xxx_tcam_entry *entry)
{
	struct mv88e6xxx_tcam_entry *elem = entry;
	u8 move_idx = entry->hw_idx;
	int err;

	err = mv88e6xxx_tcam_flush_entry(chip, entry->hw_idx);
	if (err)
		return err;

	/* move entries that come after the deleted entry forward */
	list_for_each_entry_continue(elem, &chip->tcam.entries, list) {
		u8 tmp_idx = elem->hw_idx;

		err = mv88e6xxx_tcam_flush_entry(chip, move_idx);
		if (err)
			break;

		err = chip->info->ops->tcam_ops->entry_add(chip, elem,
							   move_idx);
		if (err)
			break;

		elem->hw_idx =  move_idx;
		move_idx = tmp_idx;

		/* flush the last entry after moving entries */
		if (list_is_last(&elem->list, &chip->tcam.entries))
			err = mv88e6xxx_tcam_flush_entry(chip, tmp_idx);
	}

	list_del(&entry->list);
	kfree(entry);
	return err;
}

static int mv88e6390_tcam_entry_add(struct mv88e6xxx_chip *chip,
				    struct mv88e6xxx_tcam_entry *entry, u8 idx)
{
	int err = 0;
	int i;
	u16 val, spv_mask, spv;

	err = mv88e6xxx_tcam_read_page(chip, 2, idx);
	if (err)
		return err;
	if (entry->action.dpv_mode != 0) {
		val = MV88E6XXX_TCAM_ING_ACT3_SF |
		      (entry->action.dpv & MV88E6XXX_TCAM_ING_ACT3_DPV_MASK);

		err = mv88e6xxx_tcam_write(chip, MV88E6XXX_TCAM_ING_ACT3, val);
		if (err)
			return err;

		val = entry->action.dpv_mode << 14;
		err = mv88e6xxx_tcam_write(chip, MV88E6XXX_TCAM_ING_ACT5, val);
		if (err)
			return err;
	}
	err = mv88e6xxx_tcam_load_page(chip, 2, idx);
	if (err)
		return err;

	err = mv88e6xxx_tcam_read_page(chip, 1, idx);
	if (err)
		return err;

	for (i = PAGE0_MATCH_SIZE;
	     i < PAGE0_MATCH_SIZE + PAGE1_MATCH_SIZE; i++) {
		if (entry->key.frame_mask[i]) {
			val = entry->key.frame_mask[i] << 8 |
			      entry->key.frame_data[i];

			err = mv88e6xxx_tcam_write(chip,
						   i - PAGE0_MATCH_SIZE + 2,
						   val);
			if (err)
				return err;
		}
	}
	err = mv88e6xxx_tcam_load_page(chip, 1, idx);
	if (err)
		return err;

	err = mv88e6xxx_tcam_read_page(chip, 0, idx);
	if (err)
		return err;

	for (i = 0; i < PAGE0_MATCH_SIZE; i++) {
		if (entry->key.frame_mask[i]) {
			val = entry->key.frame_mask[i] << 8 |
			      entry->key.frame_data[i];

			err = mv88e6xxx_tcam_write(chip, i + 6, val);
			if (err)
				return err;
		}
	}

	spv_mask = entry->key.spv_mask & mv88e6xxx_port_mask(chip);
	spv = entry->key.spv & mv88e6xxx_port_mask(chip);
	/* frame type mask bits must be set for a valid entry */
	val = MV88E6XXX_TCAM_KEYS1_FT_MASK |
	      (spv_mask & MV88E6XXX_TCAM_KEYS1_SPV_MASK_MASK) |
	      ((spv >> 8) & MV88E6XXX_TCAM_KEYS1_SPV_MASK);
	err = mv88e6xxx_tcam_write(chip, MV88E6XXX_TCAM_KEYS1, val);
	if (err)
		return err;

	val = ((spv_mask << 8) & MV88E6XXX_TCAM_KEYS2_SPV_MASK_MASK) |
	      (spv & MV88E6XXX_TCAM_KEYS2_SPV_MASK);
	err = mv88e6xxx_tcam_write(chip, MV88E6XXX_TCAM_KEYS2, val);
	if (err)
		return err;

	err = mv88e6xxx_tcam_load_page(chip, 0, idx);
	if (err)
		return err;

	entry->hw_idx = idx;
	return 0;
}

static int mv88e6393_tcam_entry_add(struct mv88e6xxx_chip *chip,
				    struct mv88e6xxx_tcam_entry *entry, u8 idx)
{
	int err;

	/* select block 0 port 0, then adding an entry is the same as 6390 as
	 * other blocks aren't used at the moment
	 */
	err = mv88e6xxx_tcam_write(chip, MV88E6XXX_TCAM_EXTENSION, 0x00);
	if (err)
		return err;

	return mv88e6390_tcam_entry_add(chip, entry, idx);
}

const struct mv88e6xxx_tcam_ops mv88e6390_tcam_ops = {
	.entry_add = mv88e6390_tcam_entry_add,
	.flush_tcam = mv88e6xxx_tcam_flush_all,
};

const struct mv88e6xxx_tcam_ops mv88e6393_tcam_ops = {
	.entry_add = mv88e6393_tcam_entry_add,
	.flush_tcam = mv88e6xxx_tcam_flush_all,
};
