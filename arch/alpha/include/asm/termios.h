/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ALPHA_TERMIOS_H
#define _ALPHA_TERMIOS_H

#include <uapi/asm/termios.h>

/*	eof=^D		eol=\0		eol2=\0		erase=del
	werase=^W	kill=^U		reprint=^R	sxtc=\0
	intr=^C		quit=^\		susp=^Z		<OSF/1 VDSUSP>
	start=^Q	stop=^S		lnext=^V	discard=^U
	vmin=\1		vtime=\0
*/
#define INIT_C_CC "\004\000\000\177\027\025\022\000\003\034\032\000\021\023\026\025\001\000"

/*
 * Translate a "termio" structure into a "termios". Ugh.
 */

#define user_termio_to_kernel_termios(a_termios, u_termio)			\
({										\
	struct ktermios *k_termios = (a_termios);				\
	struct termio k_termio;							\
	int canon, ret;								\
										\
	ret = copy_from_user(&k_termio, u_termio, sizeof(k_termio));		\
	if (!ret) {								\
		/* Overwrite only the low bits.  */				\
		*(unsigned short *)&k_termios->c_iflag = k_termio.c_iflag;	\
		*(unsigned short *)&k_termios->c_oflag = k_termio.c_oflag;	\
		*(unsigned short *)&k_termios->c_cflag = k_termio.c_cflag;	\
		*(unsigned short *)&k_termios->c_lflag = k_termio.c_lflag;	\
		canon = k_termio.c_lflag & ICANON;				\
										\
		k_termios->c_cc[VINTR]  = k_termio.c_cc[_VINTR];		\
		k_termios->c_cc[VQUIT]  = k_termio.c_cc[_VQUIT];		\
		k_termios->c_cc[VERASE] = k_termio.c_cc[_VERASE];		\
		k_termios->c_cc[VKILL]  = k_termio.c_cc[_VKILL];		\
		k_termios->c_cc[VEOL2]  = k_termio.c_cc[_VEOL2];		\
		k_termios->c_cc[VSWTC]  = k_termio.c_cc[_VSWTC];		\
		k_termios->c_cc[canon ? VEOF : VMIN]  = k_termio.c_cc[_VEOF];	\
		k_termios->c_cc[canon ? VEOL : VTIME] = k_termio.c_cc[_VEOL];	\
	}									\
	ret;									\
})

/*
 * Translate a "termios" structure into a "termio". Ugh.
 *
 * Note the "fun" _VMIN overloading.
 */
#define kernel_termios_to_user_termio(u_termio, a_termios)		\
({									\
	struct ktermios *k_termios = (a_termios);			\
	struct termio k_termio;						\
	int canon;							\
									\
	k_termio.c_iflag = k_termios->c_iflag;				\
	k_termio.c_oflag = k_termios->c_oflag;				\
	k_termio.c_cflag = k_termios->c_cflag;				\
	canon = (k_termio.c_lflag = k_termios->c_lflag) & ICANON;	\
									\
	k_termio.c_line = k_termios->c_line;				\
	k_termio.c_cc[_VINTR]  = k_termios->c_cc[VINTR];		\
	k_termio.c_cc[_VQUIT]  = k_termios->c_cc[VQUIT];		\
	k_termio.c_cc[_VERASE] = k_termios->c_cc[VERASE];		\
	k_termio.c_cc[_VKILL]  = k_termios->c_cc[VKILL];		\
	k_termio.c_cc[_VEOF]   = k_termios->c_cc[canon ? VEOF : VMIN];	\
	k_termio.c_cc[_VEOL]   = k_termios->c_cc[canon ? VEOL : VTIME];	\
	k_termio.c_cc[_VEOL2]  = k_termios->c_cc[VEOL2];		\
	k_termio.c_cc[_VSWTC]  = k_termios->c_cc[VSWTC];		\
									\
	copy_to_user(u_termio, &k_termio, sizeof(k_termio));		\
})

#define user_termios_to_kernel_termios(k, u) \
	copy_from_user(k, u, sizeof(struct termios))

#define kernel_termios_to_user_termios(u, k) \
	copy_to_user(u, k, sizeof(struct termios))

#endif	/* _ALPHA_TERMIOS_H */
