/*
 * Copyright (C) ST-Ericsson SA 2010
 *
 * License terms: GNU General Public License (GPL) version 2
 */

#ifndef __BOARD_MOP500_H
#define __BOARD_MOP500_H

/* HREFv60-specific GPIO assignments, this board has no GPIO expander */
#define HREFV60_TOUCH_RST_GPIO		143
#define HREFV60_PROX_SENSE_GPIO		217
#define HREFV60_HAL_SW_GPIO		145
#define HREFV60_SDMMC_EN_GPIO		169
#define HREFV60_SDMMC_1V8_3V_GPIO	5
#define HREFV60_SDMMC_CD_GPIO		95
#define HREFV60_ACCEL_INT1_GPIO		82
#define HREFV60_ACCEL_INT2_GPIO		83
#define HREFV60_MAGNET_DRDY_GPIO	32
#define HREFV60_DISP1_RST_GPIO		65
#define HREFV60_DISP2_RST_GPIO		66

/* GPIOs on the TC35892 expander */
#define MOP500_EGPIO(x)			(NOMADIK_NR_GPIO + (x))
#define GPIO_SDMMC_CD			MOP500_EGPIO(3)
#define GPIO_PROX_SENSOR		MOP500_EGPIO(7)
#define GPIO_BU21013_CS			MOP500_EGPIO(13)
#define GPIO_SDMMC_EN			MOP500_EGPIO(17)
#define GPIO_SDMMC_1V8_3V_SEL		MOP500_EGPIO(18)
#define MOP500_EGPIO_END		MOP500_EGPIO(24)

/* GPIOs on the AB8500 mixed-signals circuit */
#define MOP500_AB8500_GPIO(x)		(MOP500_EGPIO_END + (x))

struct i2c_board_info;

extern void mop500_sdi_init(void);
extern void mop500_sdi_tc35892_init(void);
void __init mop500_u8500uib_init(void);
void __init mop500_stuib_init(void);
void __init mop500_pins_init(void);

void mop500_uib_i2c_add(int busnum, struct i2c_board_info *info,
		unsigned n);

#endif
