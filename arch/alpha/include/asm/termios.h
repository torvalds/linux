/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ALPHA_TERMIOS_H
#define _ALPHA_TERMIOS_H

#include <linux/uaccess.h>
#include <uapi/asm/termios.h>

/*	eof=^D		eol=\0		eol2=\0		erase=del
	werase=^W	kill=^U		reprint=^R	sxtc=\0
	intr=^C		quit=^\		susp=^Z		<OSF/1 VDSUSP>
	start=^Q	stop=^S		lnext=^V	discard=^U
	vmin=\1		vtime=\0
*/
#define INIT_C_CC "\004\000\000\177\027\025\022\000\003\034\032\000\021\023\026\025\001\000"

#endif	/* _ALPHA_TERMIOS_H */
