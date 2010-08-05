/*
 * dbc.h
 *
 * DSP-BIOS Bridge driver support functions for TI OMAP processors.
 *
 * "Design by Contract" programming macros.
 *
 * Notes:
 *   Requires that the GT->ERROR function has been defaulted to a valid
 *   error handler for the given execution environment.
 *
 *   Does not require that GT_init() be called.
 *
 * Copyright (C) 2008 Texas Instruments, Inc.
 *
 * This package is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * THIS PACKAGE IS PROVIDED ``AS IS'' AND WITHOUT ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, WITHOUT LIMITATION, THE IMPLIED
 * WARRANTIES OF MERCHANTIBILITY AND FITNESS FOR A PARTICULAR PURPOSE.
 */

#ifndef DBC_
#define DBC_

/* Assertion Macros: */
#ifdef CONFIG_TIDSPBRIDGE_DEBUG

#define DBC_ASSERT(exp) \
    if (!(exp)) \
	pr_err("%s, line %d: Assertion (" #exp ") failed.\n", \
	__FILE__, __LINE__)
#define DBC_REQUIRE DBC_ASSERT	/* Function Precondition. */
#define DBC_ENSURE  DBC_ASSERT	/* Function Postcondition. */

#else

#define DBC_ASSERT(exp) {}
#define DBC_REQUIRE(exp) {}
#define DBC_ENSURE(exp) {}

#endif /* DEBUG */

#endif /* DBC_ */
