/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SPARC_TERMIOS_H
#define _SPARC_TERMIOS_H

#include <uapi/asm/termios.h>
#include <linux/uaccess.h>


/*	intr=^C		quit=^\		erase=del	kill=^U
	eof=^D		eol=\0		eol2=\0		sxtc=\0
	start=^Q	stop=^S		susp=^Z		dsusp=^Y
	reprint=^R	discard=^O	werase=^W	lnext=^V
	vmin=\1         vtime=\0
*/
#define INIT_C_CC "\003\034\177\025\004\000\000\000\021\023\032\031\022\017\027\026\001"

#endif /* _SPARC_TERMIOS_H */
