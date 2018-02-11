/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_CRIS_ARCH_PINMUX_H
#define _ASM_CRIS_ARCH_PINMUX_H

#define PORT_B 0
#define PORT_C 1
#define PORT_D 2
#define PORT_E 3

enum pin_mode {
  pinmux_none = 0,
  pinmux_fixed,
  pinmux_gpio,
  pinmux_iop
};

enum fixed_function {
  pinmux_ser1,
  pinmux_ser2,
  pinmux_ser3,
  pinmux_sser0,
  pinmux_sser1,
  pinmux_ata0,
  pinmux_ata1,
  pinmux_ata2,
  pinmux_ata3,
  pinmux_ata,
  pinmux_eth1,
  pinmux_timer
};

int crisv32_pinmux_alloc(int port, int first_pin, int last_pin, enum pin_mode);
int crisv32_pinmux_alloc_fixed(enum fixed_function function);
int crisv32_pinmux_dealloc(int port, int first_pin, int last_pin);
int crisv32_pinmux_dealloc_fixed(enum fixed_function function);

#endif
