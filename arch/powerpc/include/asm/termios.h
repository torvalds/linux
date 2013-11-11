/*
 * Liberally adapted from alpha/termios.h.  In particular, the c_cc[]
 * fields have been reordered so that termio & termios share the
 * common subset in the same order (for brain dead programs that don't
 * know or care about the differences).
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_POWERPC_TERMIOS_H
#define _ASM_POWERPC_TERMIOS_H

#include <uapi/asm/termios.h>

/*                   ^C  ^\ del  ^U  ^D   1   0   0   0   0  ^W  ^R  ^Z  ^Q  ^S  ^V  ^U  */
#define INIT_C_CC "\003\034\177\025\004\001\000\000\000\000\027\022\032\021\023\026\025" 

#include <asm-generic/termios-base.h>

#endif	/* _ASM_POWERPC_TERMIOS_H */
