/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __BOARD_MOP500_H
#define __BOARD_MOP500_H

#define MOP500_EGPIO(x)			(NOMADIK_NR_GPIO + (x))

/* GPIOs on the TC35892 expander */
#define GPIO_SDMMC_CD			MOP500_EGPIO(3)
#define GPIO_PROX_SENSOR		MOP500_EGPIO(7)
#define GPIO_BU21013_CS			MOP500_EGPIO(13)
#define GPIO_SDMMC_EN			MOP500_EGPIO(17)
#define GPIO_SDMMC_1V8_3V_SEL		MOP500_EGPIO(18)

struct i2c_board_info;

extern void mop500_sdi_init(void);
extern void mop500_sdi_tc35892_init(void);
void __init mop500_u8500uib_init(void);
void __init mop500_stuib_init(void);

void mop500_uib_i2c_add(int busnum, struct i2c_board_info *info,
		unsigned n);

#endif
