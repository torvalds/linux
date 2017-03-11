/*
 * Marvell 88E6xxx Address Translation Unit (ATU) support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 * Copyright (c) 2017 Savoir-faire Linux, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include "mv88e6xxx.h"
#include "global1.h"

/* Offset 0x0A: ATU Control Register */

int mv88e6xxx_g1_atu_set_learn2all(struct mv88e6xxx_chip *chip, bool learn2all)
{
	u16 val;
	int err;

	err = mv88e6xxx_g1_read(chip, GLOBAL_ATU_CONTROL, &val);
	if (err)
		return err;

	if (learn2all)
		val |= GLOBAL_ATU_CONTROL_LEARN2ALL;
	else
		val &= ~GLOBAL_ATU_CONTROL_LEARN2ALL;

	return mv88e6xxx_g1_write(chip, GLOBAL_ATU_CONTROL, val);
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

	err = mv88e6xxx_g1_read(chip, GLOBAL_ATU_CONTROL, &val);
	if (err)
		return err;

	/* AgeTime is 11:4 bits */
	val &= ~0xff0;
	val |= age_time << 4;

	return mv88e6xxx_g1_write(chip, GLOBAL_ATU_CONTROL, val);
}
