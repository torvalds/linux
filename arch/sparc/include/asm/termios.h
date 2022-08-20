/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _SPARC_TERMIOS_H
#define _SPARC_TERMIOS_H

#include <uapi/asm/termios.h>


/*	intr=^C		quit=^\		erase=del	kill=^U
	eof=^D		eol=\0		eol2=\0		sxtc=\0
	start=^Q	stop=^S		susp=^Z		dsusp=^Y
	reprint=^R	discard=^U	werase=^W	lnext=^V
	vmin=\1         vtime=\0
*/
#define INIT_C_CC "\003\034\177\025\004\000\000\000\021\023\032\031\022\025\027\026\001"

int user_termio_to_kernel_termios(struct ktermios *, struct termio __user *);
int kernel_termios_to_user_termio(struct termio __user *, struct ktermios *);
int user_termios_to_kernel_termios(struct ktermios *, struct termios2 __user *);
int kernel_termios_to_user_termios(struct termios2 __user *, struct ktermios *);
int user_termios_to_kernel_termios_1(struct ktermios *, struct termios __user *);
int kernel_termios_to_user_termios_1(struct termios __user *, struct ktermios *);

#endif /* _SPARC_TERMIOS_H */
