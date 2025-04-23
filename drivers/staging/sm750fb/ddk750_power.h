/* SPDX-License-Identifier: GPL-2.0 */
#ifndef DDK750_POWER_H__
#define DDK750_POWER_H__

enum dpms {
	CRT_DPMS_ON = 0x0,
	CRT_DPMS_STANDBY = 0x1,
	CRT_DPMS_SUSPEND = 0x2,
	CRT_DPMS_OFF = 0x3,
};

#define set_DAC(off) {							\
	poke32(MISC_CTRL,						\
	       (peek32(MISC_CTRL) & ~MISC_CTRL_DAC_POWER_OFF) | (off)); \
}

void ddk750_set_dpms(enum dpms state);
void sm750_set_power_mode(unsigned int mode);
void sm750_set_current_gate(unsigned int gate);

/*
 * This function enable/disable the 2D engine.
 */
void sm750_enable_2d_engine(unsigned int enable);

/*
 * This function enable/disable the DMA Engine
 */
void sm750_enable_dma(unsigned int enable);

/*
 * This function enable/disable the GPIO Engine
 */
void sm750_enable_gpio(unsigned int enable);

/*
 * This function enable/disable the I2C Engine
 */
void sm750_enable_i2c(unsigned int enable);

#endif
