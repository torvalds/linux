/*
 * Copyright (c) 2016 IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */

#include "ppc_asm.h"
#include "../include/asm/opal-api.h"

	.text

#define OPAL_CALL(name, token)				\
	.globl name;					\
name:							\
	li	r0, token;				\
	b	opal_call;

opal_call:
	mflr	r11
	std	r11,16(r1)
	mfcr	r12
	stw	r12,8(r1)
	mr	r13,r2

	/* Set opal return address */
	ld	r11,opal_return@got(r2)
	mtlr	r11
	mfmsr	r12

	/* switch to BE when we enter OPAL */
	li	r11,MSR_LE
	andc	r12,r12,r11
	mtspr	SPRN_HSRR1,r12

	/* load the opal call entry point and base */
	ld	r11,opal@got(r2)
	ld	r12,8(r11)
	ld	r2,0(r11)
	mtspr	SPRN_HSRR0,r12
	hrfid

opal_return:
	FIXUP_ENDIAN
	mr	r2,r13;
	lwz	r11,8(r1);
	ld	r12,16(r1)
	mtcr	r11;
	mtlr	r12
	blr

OPAL_CALL(opal_console_write,			OPAL_CONSOLE_WRITE);
OPAL_CALL(opal_console_read,			OPAL_CONSOLE_READ);
OPAL_CALL(opal_console_write_buffer_space,	OPAL_CONSOLE_WRITE_BUFFER_SPACE);
OPAL_CALL(opal_poll_events,			OPAL_POLL_EVENTS);
OPAL_CALL(opal_console_flush,			OPAL_CONSOLE_FLUSH);
