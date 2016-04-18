/*
 * Copyright (C) 2015 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#include <asm/addrspace.h>
#include <asm/asm.h>
#include <asm/asm-offsets.h>
#include <asm/mipsregs.h>
#include <asm/regdef.h>
#include <linux/serial_reg.h>

#define UART_TX_OFS	(UART_TX << CONFIG_MIPS_CPS_NS16550_SHIFT)
#define UART_LSR_OFS	(UART_LSR << CONFIG_MIPS_CPS_NS16550_SHIFT)

/**
 * _mips_cps_putc() - write a character to the UART
 * @a0: ASCII character to write
 * @t9: UART base address
 */
LEAF(_mips_cps_putc)
1:	lw		t0, UART_LSR_OFS(t9)
	andi		t0, t0, UART_LSR_TEMT
	beqz		t0, 1b
	sb		a0, UART_TX_OFS(t9)
	jr		ra
	END(_mips_cps_putc)

/**
 * _mips_cps_puts() - write a string to the UART
 * @a0: pointer to NULL-terminated ASCII string
 * @t9: UART base address
 *
 * Write a null-terminated ASCII string to the UART.
 */
NESTED(_mips_cps_puts, 0, ra)
	move		s7, ra
	move		s6, a0

1:	lb		a0, 0(s6)
	beqz		a0, 2f
	jal		_mips_cps_putc
	PTR_ADDIU	s6, s6, 1
	b		1b

2:	jr		s7
	END(_mips_cps_puts)

/**
 * _mips_cps_putx4 - write a 4b hex value to the UART
 * @a0: the 4b value to write to the UART
 * @t9: UART base address
 *
 * Write a single hexadecimal character to the UART.
 */
NESTED(_mips_cps_putx4, 0, ra)
	andi		a0, a0, 0xf
	li		t0, '0'
	blt		a0, 10, 1f
	li		t0, 'a'
	addiu		a0, a0, -10
1:	addu		a0, a0, t0
	b		_mips_cps_putc
	END(_mips_cps_putx4)

/**
 * _mips_cps_putx8 - write an 8b hex value to the UART
 * @a0: the 8b value to write to the UART
 * @t9: UART base address
 *
 * Write an 8 bit value (ie. 2 hexadecimal characters) to the UART.
 */
NESTED(_mips_cps_putx8, 0, ra)
	move		s3, ra
	move		s2, a0
	srl		a0, a0, 4
	jal		_mips_cps_putx4
	move		a0, s2
	move		ra, s3
	b		_mips_cps_putx4
	END(_mips_cps_putx8)

/**
 * _mips_cps_putx16 - write a 16b hex value to the UART
 * @a0: the 16b value to write to the UART
 * @t9: UART base address
 *
 * Write a 16 bit value (ie. 4 hexadecimal characters) to the UART.
 */
NESTED(_mips_cps_putx16, 0, ra)
	move		s5, ra
	move		s4, a0
	srl		a0, a0, 8
	jal		_mips_cps_putx8
	move		a0, s4
	move		ra, s5
	b		_mips_cps_putx8
	END(_mips_cps_putx16)

/**
 * _mips_cps_putx32 - write a 32b hex value to the UART
 * @a0: the 32b value to write to the UART
 * @t9: UART base address
 *
 * Write a 32 bit value (ie. 8 hexadecimal characters) to the UART.
 */
NESTED(_mips_cps_putx32, 0, ra)
	move		s7, ra
	move		s6, a0
	srl		a0, a0, 16
	jal		_mips_cps_putx16
	move		a0, s6
	move		ra, s7
	b		_mips_cps_putx16
	END(_mips_cps_putx32)

#ifdef CONFIG_64BIT

/**
 * _mips_cps_putx64 - write a 64b hex value to the UART
 * @a0: the 64b value to write to the UART
 * @t9: UART base address
 *
 * Write a 64 bit value (ie. 16 hexadecimal characters) to the UART.
 */
NESTED(_mips_cps_putx64, 0, ra)
	move		sp, ra
	move		s8, a0
	dsrl32		a0, a0, 0
	jal		_mips_cps_putx32
	move		a0, s8
	move		ra, sp
	b		_mips_cps_putx32
	END(_mips_cps_putx64)

#define _mips_cps_putxlong _mips_cps_putx64

#else /* !CONFIG_64BIT */

#define _mips_cps_putxlong _mips_cps_putx32

#endif /* !CONFIG_64BIT */

/**
 * mips_cps_bev_dump() - dump relevant exception state to UART
 * @a0: pointer to NULL-terminated ASCII string naming the exception
 *
 * Write information that may be useful in debugging an exception to the
 * UART configured by CONFIG_MIPS_CPS_NS16550_*. As this BEV exception
 * will only be run if something goes horribly wrong very early during
 * the bringup of a core and it is very likely to be unsafe to perform
 * memory accesses at that point (cache state indeterminate, EVA may not
 * be configured, coherence may be disabled) let alone have a stack,
 * this is all written in assembly using only registers & unmapped
 * uncached access to the UART registers.
 */
LEAF(mips_cps_bev_dump)
	move		s0, ra
	move		s1, a0

	li		t9, CKSEG1ADDR(CONFIG_MIPS_CPS_NS16550_BASE)

	PTR_LA		a0, str_newline
	jal		_mips_cps_puts
	PTR_LA		a0, str_bev
	jal		_mips_cps_puts
	move		a0, s1
	jal		_mips_cps_puts
	PTR_LA		a0, str_newline
	jal		_mips_cps_puts
	PTR_LA		a0, str_newline
	jal		_mips_cps_puts

#define DUMP_COP0_REG(reg, name, sz, _mfc0)		\
	PTR_LA		a0, 8f;				\
	jal		_mips_cps_puts;			\
	_mfc0		a0, reg;			\
	jal		_mips_cps_putx##sz;		\
	PTR_LA		a0, str_newline;		\
	jal		_mips_cps_puts;			\
	TEXT(name)

	DUMP_COP0_REG(CP0_CAUSE,    "Cause:    0x", 32, mfc0)
	DUMP_COP0_REG(CP0_STATUS,   "Status:   0x", 32, mfc0)
	DUMP_COP0_REG(CP0_EBASE,    "EBase:    0x", long, MFC0)
	DUMP_COP0_REG(CP0_BADVADDR, "BadVAddr: 0x", long, MFC0)
	DUMP_COP0_REG(CP0_BADINSTR, "BadInstr: 0x", 32, mfc0)

	PTR_LA		a0, str_newline
	jal		_mips_cps_puts
	jr		s0
	END(mips_cps_bev_dump)

.pushsection	.data
str_bev: .asciiz "BEV Exception: "
str_newline: .asciiz "\r\n"
.popsection
