/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_SPARC_TERMIOS_H
#define _UAPI_SPARC_TERMIOS_H

#include <asm/ioctls.h>
#include <asm/termbits.h>

#if defined(__KERNEL__) || defined(__DEFINE_BSD_TERMIOS)
struct sgttyb {
	char	sg_ispeed;
	char	sg_ospeed;
	char	sg_erase;
	char	sg_kill;
	short	sg_flags;
};

struct tchars {
	char	t_intrc;
	char	t_quitc;
	char	t_startc;
	char	t_stopc;
	char	t_eofc;
	char	t_brkc;
};

struct ltchars {
	char	t_suspc;
	char	t_dsuspc;
	char	t_rprntc;
	char	t_flushc;
	char	t_werasc;
	char	t_lnextc;
};
#endif /* __KERNEL__ */

struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};


#endif /* _UAPI_SPARC_TERMIOS_H */
