/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_CRIS_ARCH_PINMUX_H
#define _ASM_CRIS_ARCH_PINMUX_H

#define PORT_A 0
#define PORT_B 1
#define PORT_C 2

enum pin_mode {
	pinmux_none = 0,
	pinmux_fixed,
	pinmux_gpio,
	pinmux_iop
};

enum fixed_function {
	pinmux_eth,
	pinmux_geth,
	pinmux_tg_ccd,
	pinmux_tg_cmos,
	pinmux_vout,
	pinmux_ser1,
	pinmux_ser2,
	pinmux_ser3,
	pinmux_ser4,
	pinmux_sser,
	pinmux_pio,
	pinmux_pwm0,
	pinmux_pwm1,
	pinmux_pwm2,
	pinmux_i2c0,
	pinmux_i2c1,
	pinmux_i2c1_3wire,
	pinmux_i2c1_sda1,
	pinmux_i2c1_sda2,
	pinmux_i2c1_sda3,
};

int crisv32_pinmux_init(void);
int crisv32_pinmux_alloc(int port, int first_pin, int last_pin, enum pin_mode);
int crisv32_pinmux_alloc_fixed(enum fixed_function function);
int crisv32_pinmux_dealloc(int port, int first_pin, int last_pin);
int crisv32_pinmux_dealloc_fixed(enum fixed_function function);
void crisv32_pinmux_dump(void);

#endif
