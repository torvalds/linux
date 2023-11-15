/* SPDX-License-Identifier: GPL-2.0-or-later */
/***************************************************************************
 *   Copyright (C) 2010-2012 Hans de Goede <hdegoede@redhat.com>           *
 *                                                                         *
 ***************************************************************************/

#include <linux/mutex.h>
#include <linux/regmap.h>

struct sch56xx_watchdog_data;

struct regmap *devm_regmap_init_sch56xx(struct device *dev, struct mutex *lock, u16 addr,
					const struct regmap_config *config);
int sch56xx_regmap_read16(struct regmap *map, unsigned int reg, unsigned int *val);
int sch56xx_regmap_write16(struct regmap *map, unsigned int reg, unsigned int val);

int sch56xx_read_virtual_reg(u16 addr, u16 reg);
int sch56xx_write_virtual_reg(u16 addr, u16 reg, u8 val);
int sch56xx_read_virtual_reg16(u16 addr, u16 reg);
int sch56xx_read_virtual_reg12(u16 addr, u16 msb_reg, u16 lsn_reg,
			       int high_nibble);

void sch56xx_watchdog_register(struct device *parent, u16 addr, u32 revision,
			       struct mutex *io_lock, int check_enabled);
void sch56xx_watchdog_unregister(struct sch56xx_watchdog_data *data);
