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

int mv88e6185_g1_reset(struct mv88e6xxx_chip *chip);
int mv88e6352_g1_reset(struct mv88e6xxx_chip *chip);

int mv88e6185_g1_ppu_enable(struct mv88e6xxx_chip *chip);
int mv88e6185_g1_ppu_disable(struct mv88e6xxx_chip *chip);

int mv88e6xxx_g1_stats_wait(struct mv88e6xxx_chip *chip);
int mv88e6xxx_g1_stats_snapshot(struct mv88e6xxx_chip *chip, int port);
int mv88e6320_g1_stats_snapshot(struct mv88e6xxx_chip *chip, int port);
int mv88e6390_g1_stats_snapshot(struct mv88e6xxx_chip *chip, int port);
int mv88e6390_g1_stats_set_histogram(struct mv88e6xxx_chip *chip);
void mv88e6xxx_g1_stats_read(struct mv88e6xxx_chip *chip, int stat, u32 *val);
int mv88e6095_g1_set_egress_port(struct mv88e6xxx_chip *chip, int port);
int mv88e6390_g1_set_egress_port(struct mv88e6xxx_chip *chip, int port);
int mv88e6095_g1_set_cpu_port(struct mv88e6xxx_chip *chip, int port);
int mv88e6390_g1_set_cpu_port(struct mv88e6xxx_chip *chip, int port);
int mv88e6390_g1_mgmt_rsvd2cpu(struct mv88e6xxx_chip *chip);

#endif /* _MV88E6XXX_GLOBAL1_H */
