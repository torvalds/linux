/*
 * Marvell 88E6xxx VLAN [Spanning Tree] Translation Unit (VTU [STU]) support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 * Copyright (c) 2015 CMC Electronics, Inc.
 * Copyright (c) 2017 Savoir-faire Linux, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mv88e6xxx.h"
#include "global1.h"

/* Offset 0x02: VTU FID Register */

int mv88e6xxx_g1_vtu_fid_read(struct mv88e6xxx_chip *chip,
			      struct mv88e6xxx_vtu_entry *entry)
{
	u16 val;
	int err;

	err = mv88e6xxx_g1_read(chip, GLOBAL_VTU_FID, &val);
	if (err)
		return err;

	entry->fid = val & GLOBAL_VTU_FID_MASK;

	return 0;
}

int mv88e6xxx_g1_vtu_fid_write(struct mv88e6xxx_chip *chip,
			       struct mv88e6xxx_vtu_entry *entry)
{
	u16 val = entry->fid & GLOBAL_VTU_FID_MASK;

	return mv88e6xxx_g1_write(chip, GLOBAL_VTU_FID, val);
}

/* Offset 0x03: VTU SID Register */

int mv88e6xxx_g1_vtu_sid_read(struct mv88e6xxx_chip *chip,
			      struct mv88e6xxx_vtu_entry *entry)
{
	u16 val;
	int err;

	err = mv88e6xxx_g1_read(chip, GLOBAL_VTU_SID, &val);
	if (err)
		return err;

	entry->sid = val & GLOBAL_VTU_SID_MASK;

	return 0;
}

int mv88e6xxx_g1_vtu_sid_write(struct mv88e6xxx_chip *chip,
			       struct mv88e6xxx_vtu_entry *entry)
{
	u16 val = entry->sid & GLOBAL_VTU_SID_MASK;

	return mv88e6xxx_g1_write(chip, GLOBAL_VTU_SID, val);
}

/* Offset 0x05: VTU Operation Register */

int mv88e6xxx_g1_vtu_op_wait(struct mv88e6xxx_chip *chip)
{
	return mv88e6xxx_g1_wait(chip, GLOBAL_VTU_OP, GLOBAL_VTU_OP_BUSY);
}

int mv88e6xxx_g1_vtu_op(struct mv88e6xxx_chip *chip, u16 op)
{
	int err;

	err = mv88e6xxx_g1_write(chip, GLOBAL_VTU_OP, op);
	if (err)
		return err;

	return mv88e6xxx_g1_vtu_op_wait(chip);
}

/* Offset 0x06: VTU VID Register */

int mv88e6xxx_g1_vtu_vid_read(struct mv88e6xxx_chip *chip,
			      struct mv88e6xxx_vtu_entry *entry)
{
	u16 val;
	int err;

	err = mv88e6xxx_g1_read(chip, GLOBAL_VTU_VID, &val);
	if (err)
		return err;

	entry->vid = val & 0xfff;
	entry->valid = !!(val & GLOBAL_VTU_VID_VALID);

	return 0;
}

int mv88e6xxx_g1_vtu_vid_write(struct mv88e6xxx_chip *chip,
			       struct mv88e6xxx_vtu_entry *entry)
{
	u16 val = entry->vid & 0xfff;

	if (entry->valid)
		val |= GLOBAL_VTU_VID_VALID;

	return mv88e6xxx_g1_write(chip, GLOBAL_VTU_VID, val);
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
		err = mv88e6xxx_g1_vtu_vid_write(chip, entry);
		if (err)
			return err;
	}

	err = mv88e6xxx_g1_vtu_op(chip, GLOBAL_VTU_OP_VTU_GET_NEXT);
	if (err)
		return err;

	return mv88e6xxx_g1_vtu_vid_read(chip, entry);
}

int mv88e6xxx_g1_vtu_flush(struct mv88e6xxx_chip *chip)
{
	int err;

	err = mv88e6xxx_g1_vtu_op_wait(chip);
	if (err)
		return err;

	return mv88e6xxx_g1_vtu_op(chip, GLOBAL_VTU_OP_FLUSH_ALL);
}
