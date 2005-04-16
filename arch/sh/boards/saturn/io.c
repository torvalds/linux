/*
 * arch/sh/boards/saturn/io.c
 *
 * I/O routines for the Sega Saturn.
 *
 * Copyright (C) 2002 Paul Mundt
 *
 * Released under the terms of the GNU GPL v2.0.
 */
#include <asm/saturn/io.h>
#include <asm/machvec.h>

unsigned long saturn_isa_port2addr(unsigned long offset)
{
	return offset;
}

void *saturn_ioremap(unsigned long offset, unsigned long size)
{
	return (void *)offset;
}

void saturn_iounmap(void *addr)
{
}

