#ifndef _SPARC_TERMIOS_H
#define _SPARC_TERMIOS_H

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

#ifdef __KERNEL__

/*
 * c_cc characters in the termio structure.  Oh, how I love being
 * backwardly compatible.  Notice that character 4 and 5 are
 * interpreted differently depending on whether ICANON is set in
 * c_lflag.  If it's set, they are used as _VEOF and _VEOL, otherwise
 * as _VMIN and V_TIME.  This is for compatibility with OSF/1 (which
 * is compatible with sysV)...
 */
#define _VMIN	4
#define _VTIME	5

/*	intr=^C		quit=^\		erase=del	kill=^U
	eof=^D		eol=\0		eol2=\0		sxtc=\0
	start=^Q	stop=^S		susp=^Z		dsusp=^Y
	reprint=^R	discard=^U	werase=^W	lnext=^V
	vmin=\1         vtime=\0
*/
#define INIT_C_CC "\003\034\177\025\004\000\000\000\021\023\032\031\022\025\027\026\001"

/*
 * Translate a "termio" structure into a "termios". Ugh.
 */
#define user_termio_to_kernel_termios(termios, termio) \
({ \
	unsigned short tmp; \
	int err; \
	err = get_user(tmp, &(termio)->c_iflag); \
	(termios)->c_iflag = (0xffff0000 & ((termios)->c_iflag)) | tmp; \
	err |= get_user(tmp, &(termio)->c_oflag); \
	(termios)->c_oflag = (0xffff0000 & ((termios)->c_oflag)) | tmp; \
	err |= get_user(tmp, &(termio)->c_cflag); \
	(termios)->c_cflag = (0xffff0000 & ((termios)->c_cflag)) | tmp; \
	err |= get_user(tmp, &(termio)->c_lflag); \
	(termios)->c_lflag = (0xffff0000 & ((termios)->c_lflag)) | tmp; \
	err |= copy_from_user((termios)->c_cc, (termio)->c_cc, NCC); \
	err; \
})

/*
 * Translate a "termios" structure into a "termio". Ugh.
 *
 * Note the "fun" _VMIN overloading.
 */
#define kernel_termios_to_user_termio(termio, termios) \
({ \
	int err; \
	err  = put_user((termios)->c_iflag, &(termio)->c_iflag); \
	err |= put_user((termios)->c_oflag, &(termio)->c_oflag); \
	err |= put_user((termios)->c_cflag, &(termio)->c_cflag); \
	err |= put_user((termios)->c_lflag, &(termio)->c_lflag); \
	err |= put_user((termios)->c_line,  &(termio)->c_line); \
	err |= copy_to_user((termio)->c_cc, (termios)->c_cc, NCC); \
	if (!((termios)->c_lflag & ICANON)) { \
		err |= put_user((termios)->c_cc[VMIN], &(termio)->c_cc[_VMIN]); \
		err |= put_user((termios)->c_cc[VTIME], &(termio)->c_cc[_VTIME]); \
	} \
	err; \
})

#define user_termios_to_kernel_termios(k, u) \
({ \
	int err; \
	err  = get_user((k)->c_iflag, &(u)->c_iflag); \
	err |= get_user((k)->c_oflag, &(u)->c_oflag); \
	err |= get_user((k)->c_cflag, &(u)->c_cflag); \
	err |= get_user((k)->c_lflag, &(u)->c_lflag); \
	err |= get_user((k)->c_line,  &(u)->c_line); \
	err |= copy_from_user((k)->c_cc, (u)->c_cc, NCCS); \
	if ((k)->c_lflag & ICANON) { \
		err |= get_user((k)->c_cc[VEOF], &(u)->c_cc[VEOF]); \
		err |= get_user((k)->c_cc[VEOL], &(u)->c_cc[VEOL]); \
	} else { \
		err |= get_user((k)->c_cc[VMIN],  &(u)->c_cc[_VMIN]); \
		err |= get_user((k)->c_cc[VTIME], &(u)->c_cc[_VTIME]); \
	} \
	err |= get_user((k)->c_ispeed,  &(u)->c_ispeed); \
	err |= get_user((k)->c_ospeed,  &(u)->c_ospeed); \
	err; \
})

#define kernel_termios_to_user_termios(u, k) \
({ \
	int err; \
	err  = put_user((k)->c_iflag, &(u)->c_iflag); \
	err |= put_user((k)->c_oflag, &(u)->c_oflag); \
	err |= put_user((k)->c_cflag, &(u)->c_cflag); \
	err |= put_user((k)->c_lflag, &(u)->c_lflag); \
	err |= put_user((k)->c_line, &(u)->c_line); \
	err |= copy_to_user((u)->c_cc, (k)->c_cc, NCCS); \
	if (!((k)->c_lflag & ICANON)) { \
		err |= put_user((k)->c_cc[VMIN],  &(u)->c_cc[_VMIN]); \
		err |= put_user((k)->c_cc[VTIME], &(u)->c_cc[_VTIME]); \
	} else { \
		err |= put_user((k)->c_cc[VEOF], &(u)->c_cc[VEOF]); \
		err |= put_user((k)->c_cc[VEOL], &(u)->c_cc[VEOL]); \
	} \
	err |= put_user((k)->c_ispeed, &(u)->c_ispeed); \
	err |= put_user((k)->c_ospeed, &(u)->c_ospeed); \
	err; \
})

#define user_termios_to_kernel_termios_1(k, u) \
({ \
	int err; \
	err  = get_user((k)->c_iflag, &(u)->c_iflag); \
	err |= get_user((k)->c_oflag, &(u)->c_oflag); \
	err |= get_user((k)->c_cflag, &(u)->c_cflag); \
	err |= get_user((k)->c_lflag, &(u)->c_lflag); \
	err |= get_user((k)->c_line,  &(u)->c_line); \
	err |= copy_from_user((k)->c_cc, (u)->c_cc, NCCS); \
	if ((k)->c_lflag & ICANON) { \
		err |= get_user((k)->c_cc[VEOF], &(u)->c_cc[VEOF]); \
		err |= get_user((k)->c_cc[VEOL], &(u)->c_cc[VEOL]); \
	} else { \
		err |= get_user((k)->c_cc[VMIN],  &(u)->c_cc[_VMIN]); \
		err |= get_user((k)->c_cc[VTIME], &(u)->c_cc[_VTIME]); \
	} \
	err; \
})

#define kernel_termios_to_user_termios_1(u, k) \
({ \
	int err; \
	err  = put_user((k)->c_iflag, &(u)->c_iflag); \
	err |= put_user((k)->c_oflag, &(u)->c_oflag); \
	err |= put_user((k)->c_cflag, &(u)->c_cflag); \
	err |= put_user((k)->c_lflag, &(u)->c_lflag); \
	err |= put_user((k)->c_line, &(u)->c_line); \
	err |= copy_to_user((u)->c_cc, (k)->c_cc, NCCS); \
	if (!((k)->c_lflag & ICANON)) { \
		err |= put_user((k)->c_cc[VMIN],  &(u)->c_cc[_VMIN]); \
		err |= put_user((k)->c_cc[VTIME], &(u)->c_cc[_VTIME]); \
	} else { \
		err |= put_user((k)->c_cc[VEOF], &(u)->c_cc[VEOF]); \
		err |= put_user((k)->c_cc[VEOL], &(u)->c_cc[VEOL]); \
	} \
	err; \
})

#endif	/* __KERNEL__ */

#endif /* _SPARC_TERMIOS_H */
