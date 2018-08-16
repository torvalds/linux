/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1995, 1996, 2000, 2001 by Ralf Baechle
 * Copyright (C) 2000, 2001 Silicon Graphics, Inc.
 */
#ifndef _ASM_TERMIOS_H
#define _ASM_TERMIOS_H

#include <linux/uaccess.h>
#include <uapi/asm/termios.h>

/*
 *	intr=^C		quit=^\		erase=del	kill=^U
 *	vmin=\1		vtime=\0	eol2=\0		swtc=\0
 *	start=^Q	stop=^S		susp=^Z		vdsusp=
 *	reprint=^R	discard=^U	werase=^W	lnext=^V
 *	eof=^D		eol=\0
 */
#define INIT_C_CC "\003\034\177\025\1\0\0\0\021\023\032\0\022\017\027\026\004\0"

#endif /* _ASM_TERMIOS_H */
