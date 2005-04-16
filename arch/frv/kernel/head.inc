/* head.inc: head common definitions -*- asm -*-
 *
 * Copyright (C) 2004 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */


#if defined(CONFIG_MB93090_MB00)
#define LED_ADDR (0x21200000+4)

.macro LEDS val
	sethi.p		%hi(0xFFC00030),gr3
	setlo		%lo(0xFFC00030),gr3
	lduh		@(gr3,gr0),gr3
	andicc		gr3,#0x100,gr0,icc0
	bne		icc0,0,999f

	setlos		#~\val,gr3
	st		gr3,@(gr30,gr0)
	membar
	dcf		@(gr30,gr0)
    999:
.endm

#elif defined(CONFIG_MB93093_PDK)
#define LED_ADDR (0x20000023)

.macro LEDS val
	setlos		#\val,gr3
	stb		gr3,@(gr30,gr0)
	membar
.endm

#else
#define LED_ADDR 0

.macro LEDS val
.endm
#endif

#ifdef CONFIG_MMU
__sdram_base = 0x00000000		/* base address to which SDRAM relocated */
#else
__sdram_base = 0xc0000000		/* base address to which SDRAM relocated */
#endif
