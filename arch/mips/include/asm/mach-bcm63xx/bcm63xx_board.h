/* SPDX-License-Identifier: GPL-2.0 */
#ifndef BCM63XX_BOARD_H_
#define BCM63XX_BOARD_H_

const char *board_get_name(void);

void board_prom_init(void);

void board_setup(void);

int board_register_devices(void);

#endif /* ! BCM63XX_BOARD_H_ */
