/*
 * Marvell 88E6xxx Switch Global (1) Registers support
 *
 * Copyright (c) 2008 Marvell Semiconductor
 *
 * Copyright (c) 2016 Vivien Didelot <vivien.didelot@savoirfairelinux.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef _MV88E6XXX_GLOBAL1_H
#define _MV88E6XXX_GLOBAL1_H

#include "mv88e6xxx.h"

int mv88e6xxx_g1_read(struct mv88e6xxx_chip *chip, int reg, u16 *val);
int mv88e6xxx_g1_write(struct mv88e6xxx_chip *chip, int reg, u16 val);
int mv88e6xxx_g1_wait(struct mv88e6xxx_chip *chip, int reg, u16 mask);

#endif /* _MV88E6XXX_GLOBAL1_H */
