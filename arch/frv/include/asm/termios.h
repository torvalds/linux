/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_TERMIOS_H
#define _ASM_TERMIOS_H

#include <uapi/asm/termios.h>

/*	intr=^C		quit=^|		erase=del	kill=^U
	eof=^D		vtime=\0	vmin=\1		sxtc=\0
	start=^Q	stop=^S		susp=^Z		eol=\0
	reprint=^R	discard=^U	werase=^W	lnext=^V
	eol2=\0
*/
#define INIT_C_CC "\003\034\177\025\004\0\1\0\021\023\032\0\022\017\027\026\0"
#include <asm-generic/termios-base.h>
#endif /* _ASM_TERMIOS_H */
