/*
 *  S390 version
 *
 *  Derived from "include/asm-i386/termios.h"
 */
#ifndef _S390_TERMIOS_H
#define _S390_TERMIOS_H

#include <uapi/asm/termios.h>


/*	intr=^C		quit=^\		erase=del	kill=^U
	eof=^D		vtime=\0	vmin=\1		sxtc=\0
	start=^Q	stop=^S		susp=^Z		eol=\0
	reprint=^R	discard=^U	werase=^W	lnext=^V
	eol2=\0
*/
#define INIT_C_CC "\003\034\177\025\004\0\1\0\021\023\032\0\022\017\027\026\0"

#define user_termios_to_kernel_termios(k, u) copy_from_user(k, u, sizeof(struct termios2))
#define kernel_termios_to_user_termios(u, k) copy_to_user(u, k, sizeof(struct termios2))

#include <asm-generic/termios-base.h>

#endif	/* _S390_TERMIOS_H */
