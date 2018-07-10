/* vi: set sw=4 ts=4: */
/*
 * Stripped down version of net-tools for busybox.
 *
 * Author: Ignacio Garcia Perez (iggarpe at gmail dot com)
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 *
 * There are some differences from the standard net-tools slattach:
 *
 * - The -l option is not supported.
 *
 * - The -F options allows disabling of RTS/CTS flow control.
 */
//config:config SLATTACH
//config:	bool "slattach (6.1 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	slattach configures serial line as SLIP network interface.

//applet:IF_SLATTACH(APPLET(slattach, BB_DIR_SBIN, BB_SUID_DROP))
/* shouldn't be NOEXEC: may sleep indefinitely */

//kbuild:lib-$(CONFIG_SLATTACH) += slattach.o

//usage:#define slattach_trivial_usage
//usage:       "[-ehmLF] [-c SCRIPT] [-s BAUD] [-p PROTOCOL] SERIAL_DEVICE"
//usage:#define slattach_full_usage "\n\n"
//usage:       "Configure serial line as SLIP network interface\n"
//usage:     "\n	-p PROT	Protocol: slip, cslip (default), slip6, clisp6, adaptive"
//usage:     "\n	-s BAUD	Line speed"
//usage:     "\n	-e	Exit after initialization"
//usage:     "\n	-h	Exit if carrier is lost (else never exits)"
//usage:     "\n	-c PROG	Run PROG on carrier loss"
//usage:     "\n	-m	Do NOT set raw 8bit mode"
//usage:     "\n	-L	Enable 3-wire operation"
//usage:     "\n	-F	Disable RTS/CTS flow control"

#include "libbb.h"
#include "common_bufsiz.h"
#include "libiproute/utils.h" /* invarg_1_to_2() */

struct globals {
	int saved_disc;
	struct termios saved_state;
} FIX_ALIASING;
#define G (*(struct globals*)bb_common_bufsiz1)
#define INIT_G() do { setup_common_bufsiz(); } while (0)

#define serial_fd 3

static int tcsetattr_serial_or_warn(struct termios *state)
{
	int ret;

	ret = tcsetattr(serial_fd, TCSANOW, state);
	if (ret != 0) {
		bb_perror_msg("tcsetattr");
		return 1; /* used as exitcode */
	}
	return ret; /* 0 */
}

static void restore_state_and_exit(int exitcode) NORETURN;
static void restore_state_and_exit(int exitcode)
{
	struct termios state;

	/* Restore line discipline */
	if (ioctl_or_warn(serial_fd, TIOCSETD, &G.saved_disc)) {
		exitcode = 1;
	}

	/* Hangup */
	memcpy(&state, &G.saved_state, sizeof(state));
	cfsetispeed(&state, B0);
	cfsetospeed(&state, B0);
	exitcode |= tcsetattr_serial_or_warn(&state);
	sleep(1);

	/* Restore line status */
	if (tcsetattr_serial_or_warn(&G.saved_state))
		exit(EXIT_FAILURE);

	if (ENABLE_FEATURE_CLEAN_UP)
		close(serial_fd);

	exit(exitcode);
}

static void sig_handler(int signo UNUSED_PARAM)
{
	restore_state_and_exit(EXIT_SUCCESS);
}

int slattach_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int slattach_main(int argc UNUSED_PARAM, char **argv)
{
	/* Line discipline code table */
	static const char proto_names[] ALIGN1 =
		"slip\0"        /* 0 */
		"cslip\0"       /* 1 */
		"slip6\0"       /* 2 */
		"cslip6\0"      /* 3 */
		"adaptive\0"    /* 8 */
		;
	static const int int_N_SLIP = N_SLIP;

	int encap, opt, fd;
	struct termios state;
	const char *proto = "cslip";
	const char *extcmd;   /* Command to execute after hangup */
	const char *baud_str;
	int baud_code = baud_code; /* for compiler */

	enum {
		OPT_p_proto  = 1 << 0,
		OPT_s_baud   = 1 << 1,
		OPT_c_extcmd = 1 << 2,
		OPT_e_quit   = 1 << 3,
		OPT_h_watch  = 1 << 4,
		OPT_m_nonraw = 1 << 5,
		OPT_L_local  = 1 << 6,
		OPT_F_noflow = 1 << 7
	};

	INIT_G();

	/* Parse command line options */
	opt = getopt32(argv, "^" "p:s:c:ehmLF" "\0" "=1",
				&proto, &baud_str, &extcmd
	);
	/*argc -= optind;*/
	argv += optind;

	encap = index_in_strings(proto_names, proto);
	if (encap < 0)
		invarg_1_to_2(proto, "protocol");
	if (encap > 3)
		encap = 8;

	/* We want to know if the baud rate is valid before we start touching the ttys */
	if (opt & OPT_s_baud) {
		baud_code = tty_value_to_baud(xatoi(baud_str));
		if (baud_code < 0)
			invarg_1_to_2(baud_str, "baud rate");
	}

	/* Open tty */
	fd = open(*argv, O_RDWR | O_NDELAY);
	if (fd < 0) {
		char *buf = concat_path_file("/dev", *argv);
		fd = xopen(buf, O_RDWR | O_NDELAY);
		/* maybe if (ENABLE_FEATURE_CLEAN_UP) ?? */
		free(buf);
	}
	xmove_fd(fd, serial_fd);

	/* Save current tty state */
	if (tcgetattr(serial_fd, &G.saved_state) != 0)
		bb_perror_msg_and_die("tcgetattr");
	/* Save line discipline */
	xioctl(serial_fd, TIOCGETD, &G.saved_disc);

	/* Trap signals in order to restore tty states upon exit */
	if (!(opt & OPT_e_quit)) {
		bb_signals(0
			+ (1 << SIGHUP)
			+ (1 << SIGINT)
			+ (1 << SIGQUIT)
			+ (1 << SIGTERM)
			, sig_handler);
	}

	/* Configure tty */
	memcpy(&state, &G.saved_state, sizeof(state));
	if (!(opt & OPT_m_nonraw)) { /* raw not suppressed */
		memset(&state.c_cc, 0, sizeof(state.c_cc));
		state.c_cc[VMIN] = 1;
		state.c_iflag = IGNBRK | IGNPAR;
		/*state.c_oflag = 0;*/
		/*state.c_lflag = 0;*/
		state.c_cflag = CS8 | HUPCL | CREAD
		              | ((opt & OPT_L_local) ? CLOCAL : 0)
		              | ((opt & OPT_F_noflow) ? 0 : CRTSCTS);
		cfsetispeed(&state, cfgetispeed(&G.saved_state));
		cfsetospeed(&state, cfgetospeed(&G.saved_state));
	}
	if (opt & OPT_s_baud) {
		cfsetispeed(&state, baud_code);
		cfsetospeed(&state, baud_code);
	}
	/* Set line status */
	if (tcsetattr_serial_or_warn(&state))
		goto bad;
	/* Set line disclipline (N_SLIP always) */
	if (ioctl_or_warn(serial_fd, TIOCSETD, (void*)&int_N_SLIP))
		goto bad;
	/* Set encapsulation (SLIP, CSLIP, etc) */
	if (ioctl_or_warn(serial_fd, SIOCSIFENCAP, &encap))
		goto bad;

	/* Exit now if option -e was passed */
	if (opt & OPT_e_quit)
		return EXIT_SUCCESS;

	/* If we're not requested to watch, just keep descriptor open
	 * until we are killed */
	if (!(opt & OPT_h_watch))
		while (1)
			sleep(24*60*60);

	/* Watch line for hangup */
	while (1) {
		int modem_stat;
		if (ioctl(serial_fd, TIOCMGET, &modem_stat))
			break;
		if (!(modem_stat & TIOCM_CAR))
			break;
		sleep(15);
	}

	/* Execute command on hangup */
	if (opt & OPT_c_extcmd)
		system(extcmd);

	/* Restore states and exit */
	restore_state_and_exit(EXIT_SUCCESS);
 bad:
	restore_state_and_exit(EXIT_FAILURE);
}
