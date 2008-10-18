/*
 * include/asm-mips/txx9pio.h
 * TX39/TX49 PIO controller definitions.
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#ifndef __ASM_TXX9PIO_H
#define __ASM_TXX9PIO_H

#include <linux/types.h>

struct txx9_pio_reg {
	__u32 dout;
	__u32 din;
	__u32 dir;
	__u32 od;
	__u32 flag[2];
	__u32 pol;
	__u32 intc;
	__u32 maskcpu;
	__u32 maskext;
};

int txx9_gpio_init(unsigned long baseaddr,
		   unsigned int base, unsigned int num);

#endif /* __ASM_TXX9PIO_H */
