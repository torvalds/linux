/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 1997, 2003 by Ralf Baechle
 * Copyright (C) 2001, 2002, 2003 by Liam Davies (ldavies@agile.tv)
 */
#include <asm/asm.h>
#include <asm/mipsregs.h>
#include <asm/cobalt/cobalt.h>
#include <asm/regdef.h>
#include <asm/stackframe.h>

		.text
		.align	5
		NESTED(cobalt_handle_int, PT_SIZE, sp)
		SAVE_ALL
		CLI

		la	ra, ret_from_irq
		move	a1, sp
		j	cobalt_irq

		END(cobalt_handle_int)
