#ifndef _UAPI_ASM_TERMIOS_H
#define _UAPI_ASM_TERMIOS_H

#include <asm/termbits.h>
#include <asm/ioctls.h>

struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

#define NCC 8
struct termio {
	unsigned short c_iflag;		/* input mode flags */
	unsigned short c_oflag;		/* output mode flags */
	unsigned short c_cflag;		/* control mode flags */
	unsigned short c_lflag;		/* local mode flags */
	unsigned char c_line;		/* line discipline */
	unsigned char c_cc[NCC];	/* control characters */
};


/* modem lines */
#define TIOCM_LE	0x001
#define TIOCM_DTR	0x002
#define TIOCM_RTS	0x004
#define TIOCM_ST	0x008
#define TIOCM_SR	0x010
#define TIOCM_CTS	0x020
#define TIOCM_CAR	0x040
#define TIOCM_RNG	0x080
#define TIOCM_DSR	0x100
#define TIOCM_CD	TIOCM_CAR
#define TIOCM_RI	TIOCM_RNG
#define TIOCM_OUT1	0x2000
#define TIOCM_OUT2	0x4000
#define TIOCM_LOOP	0x8000

#define TIOCM_MODEM_BITS       TIOCM_OUT2      /* IRDA support */

/*
 * Translate a "termio" structure into a "termios". Ugh.
 */
#define SET_LOW_TERMIOS_BITS(termios, termio, x) { \
	unsigned short __tmp; \
	get_user(__tmp, &(termio)->x); \
	*(unsigned short *) &(termios)->x = __tmp; \
}

#define user_termio_to_kernel_termios(termios, termio) \
({ \
	SET_LOW_TERMIOS_BITS(termios, termio, c_iflag); \
	SET_LOW_TERMIOS_BITS(termios, termio, c_oflag); \
	SET_LOW_TERMIOS_BITS(termios, termio, c_cflag); \
	SET_LOW_TERMIOS_BITS(termios, termio, c_lflag); \
	copy_from_user((termios)->c_cc, (termio)->c_cc, NCC); \
})

/*
 * Translate a "termios" structure into a "termio". Ugh.
 */
#define kernel_termios_to_user_termio(termio, termios) \
({ \
	put_user((termios)->c_iflag, &(termio)->c_iflag); \
	put_user((termios)->c_oflag, &(termio)->c_oflag); \
	put_user((termios)->c_cflag, &(termio)->c_cflag); \
	put_user((termios)->c_lflag, &(termio)->c_lflag); \
	put_user((termios)->c_line,  &(termio)->c_line); \
	copy_to_user((termio)->c_cc, (termios)->c_cc, NCC); \
})

#define user_termios_to_kernel_termios(k, u) \
	copy_from_user(k, u, sizeof(struct termios2))
#define kernel_termios_to_user_termios(u, k) \
	copy_to_user(u, k, sizeof(struct termios2))
#define user_termios_to_kernel_termios_1(k, u) \
	copy_from_user(k, u, sizeof(struct termios))
#define kernel_termios_to_user_termios_1(u, k) \
	copy_to_user(u, k, sizeof(struct termios))

#endif /* _UAPI_ASM_TERMIOS_H */
