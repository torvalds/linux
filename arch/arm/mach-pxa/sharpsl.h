/*
 * Copyright (c) 2004-2005 Richard Purdie
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 */

#include <asm/hardware/sharpsl_pm.h>

/*
 * SharpSL SSP Driver
 */
struct corgissp_machinfo {
	int port;
	int cs_lcdcon;
	int cs_ads7846;
	int cs_max1111;
	int clk_lcdcon;
	int clk_ads7846;
	int clk_max1111;
};

void corgi_ssp_set_machinfo(struct corgissp_machinfo *machinfo);


/*
 * SharpSL/Corgi LCD Driver
 */
void corgi_lcdtg_suspend(void);
void corgi_lcdtg_hw_init(int mode);


/*
 * SharpSL Battery/PM Driver
 */
#define READ_GPIO_BIT(x)    (GPLR(x) & GPIO_bit(x))

/* MAX1111 Channel Definitions */
#define MAX1111_BATT_VOLT   4u
#define MAX1111_BATT_TEMP   2u
#define MAX1111_ACIN_VOLT   6u

extern struct battery_thresh spitz_battery_levels_acin[];
extern struct battery_thresh spitz_battery_levels_noac[];
void sharpsl_pm_pxa_init(void);
void sharpsl_pm_pxa_remove(void);
int sharpsl_pm_pxa_read_max1111(int channel);


