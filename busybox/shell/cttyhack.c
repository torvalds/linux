/* vi: set sw=4 ts=4: */
/*
 * Copyright (c) 2007 Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config CTTYHACK
//config:	bool "cttyhack (2.5 kb)"
//config:	default y
//config:	help
//config:	One common problem reported on the mailing list is the "can't
//config:	access tty; job control turned off" error message, which typically
//config:	appears when one tries to use a shell with stdin/stdout on
//config:	/dev/console.
//config:	This device is special - it cannot be a controlling tty.
//config:
//config:	The proper solution is to use the correct device instead of
//config:	/dev/console.
//config:
//config:	cttyhack provides a "quick and dirty" solution to this problem.
//config:	It analyzes stdin with various ioctls, trying to determine whether
//config:	it is a /dev/ttyN or /dev/ttySN (virtual terminal or serial line).
//config:	On Linux it also checks sysfs for a pointer to the active console.
//config:	If cttyhack is able to find the real console device, it closes
//config:	stdin/out/err and reopens that device.
//config:	Then it executes the given program. Opening the device will make
//config:	that device a controlling tty. This may require cttyhack
//config:	to be a session leader.
//config:
//config:	Example for /etc/inittab (for busybox init):
//config:
//config:	::respawn:/bin/cttyhack /bin/sh
//config:
//config:	Starting an interactive shell from boot shell script:
//config:
//config:	setsid cttyhack sh
//config:
//config:	Giving controlling tty to shell running with PID 1:
//config:
//config:	# exec cttyhack sh
//config:
//config:	Without cttyhack, you need to know exact tty name,
//config:	and do something like this:
//config:
//config:	# exec setsid sh -c 'exec sh </dev/tty1 >/dev/tty1 2>&1'
//config:
//config:	Starting getty on a controlling tty from a shell script:
//config:
//config:	# getty 115200 $(cttyhack)

//applet:IF_CTTYHACK(APPLET_NOEXEC(cttyhack, cttyhack, BB_DIR_BIN, BB_SUID_DROP, cttyhack))

//kbuild:lib-$(CONFIG_CTTYHACK) += cttyhack.o

//usage:#define cttyhack_trivial_usage
//usage:       "[PROG ARGS]"
//usage:#define cttyhack_full_usage "\n\n"
//usage:       "Give PROG a controlling tty if possible."
//usage:     "\nExample for /etc/inittab (for busybox init):"
//usage:     "\n	::respawn:/bin/cttyhack /bin/sh"
//usage:     "\nGiving controlling tty to shell running with PID 1:"
//usage:     "\n	$ exec cttyhack sh"
//usage:     "\nStarting interactive shell from boot shell script:"
//usage:     "\n	setsid cttyhack sh"

#include "libbb.h"

#if !defined(__linux__) && !defined(TIOCGSERIAL) && !ENABLE_WERROR
# warning cttyhack will not be able to detect a controlling tty on this system
#endif

/* From <linux/vt.h> */
struct vt_stat {
	unsigned short v_active;        /* active vt */
	unsigned short v_signal;        /* signal to send */
	unsigned short v_state;         /* vt bitmask */
};
enum { VT_GETSTATE = 0x5603 }; /* get global vt state info */

/* From <linux/serial.h> */
struct serial_struct {
	int	type;
	int	line;
	unsigned int	port;
	int	irq;
	int	flags;
	int	xmit_fifo_size;
	int	custom_divisor;
	int	baud_base;
	unsigned short	close_delay;
	char	io_type;
	char	reserved_char[1];
	int	hub6;
	unsigned short	closing_wait;   /* time to wait before closing */
	unsigned short	closing_wait2;  /* no longer used... */
	unsigned char	*iomem_base;
	unsigned short	iomem_reg_shift;
	unsigned int	port_high;
	unsigned long	iomap_base;	/* cookie passed into ioremap */
	int	reserved[1];
};

int cttyhack_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int cttyhack_main(int argc UNUSED_PARAM, char **argv)
{
	int fd;
	char console[sizeof(int)*3 + 16];
	union {
		struct vt_stat vt;
		struct serial_struct sr;
		char paranoia[sizeof(struct serial_struct) * 3];
	} u;

	strcpy(console, "/dev/tty");
	fd = open(console, O_RDWR);
	if (fd < 0) {
		/* We don't have ctty (or don't have "/dev/tty" node...) */
		do {
#ifdef __linux__
			/* Note that this method does not use _stdin_.
			 * Thus, "cttyhack </dev/something" can't be used.
			 * However, this method is more reliable than
			 * TIOCGSERIAL check, which assumes that all
			 * serial lines follow /dev/ttySn convention -
			 * which is not always the case.
			 * Therefore, we use this method first:
			 */
			int s = open_read_close("/sys/class/tty/console/active",
				console + 5, sizeof(console) - 5);
			if (s > 0) {
				char *last;
				/* Found active console via sysfs (Linux 2.6.38+).
				 * It looks like "[tty0 ]ttyS0\n" so zap the newline:
				 */
				console[4 + s] = '\0';
				/* If there are multiple consoles,
				 * take the last one:
				 */
				last = strrchr(console + 5, ' ');
				if (last)
					overlapping_strcpy(console + 5, last + 1);
				break;
			}

			if (ioctl(0, VT_GETSTATE, &u.vt) == 0) {
				/* this is linux virtual tty */
				sprintf(console + 8, "S%u" + 1, (int)u.vt.v_active);
				break;
			}
#endif
#ifdef TIOCGSERIAL
			if (ioctl(0, TIOCGSERIAL, &u.sr) == 0) {
				/* this is a serial console; assuming it is named /dev/ttySn */
				sprintf(console + 8, "S%u", (int)u.sr.line);
				break;
			}
#endif
			/* nope, could not find it */
			console[0] = '\0';
		} while (0);
	}

	argv++;
	if (!argv[0]) {
		if (!console[0])
			return EXIT_FAILURE;
		puts(console);
		return EXIT_SUCCESS;
	}

	if (fd < 0) {
		fd = open_or_warn(console, O_RDWR);
		if (fd < 0)
			goto ret;
	}
	//bb_error_msg("switching to '%s'", console);
	dup2(fd, 0);
	dup2(fd, 1);
	dup2(fd, 2);
	while (fd > 2)
		close(fd--);
	/* Some other session may have it as ctty,
	 * try to steal it from them:
	 */
	ioctl(0, TIOCSCTTY, 1);
 ret:
	BB_EXECVP_or_die(argv);
}
