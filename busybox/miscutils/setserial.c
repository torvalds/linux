/* vi: set sw=4 ts=4: */
/*
 * setserial implementation for busybox
 *
 *
 * Copyright (C) 2011 Marek Bečka <yuen@klacno.sk>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config SETSERIAL
//config:	bool "setserial (6.6 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	Retrieve or set Linux serial port.

//applet:IF_SETSERIAL(APPLET_NOEXEC(setserial, setserial, BB_DIR_BIN, BB_SUID_DROP, setserial))

//kbuild:lib-$(CONFIG_SETSERIAL) += setserial.o

#include "libbb.h"
#include <assert.h>

#ifndef PORT_UNKNOWN
# define PORT_UNKNOWN            0
#endif
#ifndef PORT_8250
# define PORT_8250               1
#endif
#ifndef PORT_16450
# define PORT_16450              2
#endif
#ifndef PORT_16550
# define PORT_16550              3
#endif
#ifndef PORT_16550A
# define PORT_16550A             4
#endif
#ifndef PORT_CIRRUS
# define PORT_CIRRUS             5
#endif
#ifndef PORT_16650
# define PORT_16650              6
#endif
#ifndef PORT_16650V2
# define PORT_16650V2            7
#endif
#ifndef PORT_16750
# define PORT_16750              8
#endif
#ifndef PORT_STARTECH
# define PORT_STARTECH           9
#endif
#ifndef PORT_16C950
# define PORT_16C950            10
#endif
#ifndef PORT_16654
# define PORT_16654             11
#endif
#ifndef PORT_16850
# define PORT_16850             12
#endif
#ifndef PORT_RSA
# define PORT_RSA               13
#endif
#ifndef PORT_NS16550A
# define PORT_NS16550A          14
#endif
#ifndef PORT_XSCALE
# define PORT_XSCALE            15
#endif
#ifndef PORT_RM9000
# define PORT_RM9000            16
#endif
#ifndef PORT_OCTEON
# define PORT_OCTEON            17
#endif
#ifndef PORT_AR7
# define PORT_AR7               18
#endif
#ifndef PORT_U6_16550A
# define PORT_U6_16550A         19
#endif

#ifndef ASYNCB_HUP_NOTIFY
# define ASYNCB_HUP_NOTIFY       0
#endif
#ifndef ASYNCB_FOURPORT
# define ASYNCB_FOURPORT         1
#endif
#ifndef ASYNCB_SAK
# define ASYNCB_SAK              2
#endif
#ifndef ASYNCB_SPLIT_TERMIOS
# define ASYNCB_SPLIT_TERMIOS    3
#endif
#ifndef ASYNCB_SPD_HI
# define ASYNCB_SPD_HI           4
#endif
#ifndef ASYNCB_SPD_VHI
# define ASYNCB_SPD_VHI          5
#endif
#ifndef ASYNCB_SKIP_TEST
# define ASYNCB_SKIP_TEST        6
#endif
#ifndef ASYNCB_AUTO_IRQ
# define ASYNCB_AUTO_IRQ         7
#endif
#ifndef ASYNCB_SESSION_LOCKOUT
# define ASYNCB_SESSION_LOCKOUT  8
#endif
#ifndef ASYNCB_PGRP_LOCKOUT
# define ASYNCB_PGRP_LOCKOUT     9
#endif
#ifndef ASYNCB_CALLOUT_NOHUP
# define ASYNCB_CALLOUT_NOHUP   10
#endif
#ifndef ASYNCB_SPD_SHI
# define ASYNCB_SPD_SHI         12
#endif
#ifndef ASYNCB_LOW_LATENCY
# define ASYNCB_LOW_LATENCY     13
#endif
#ifndef ASYNCB_BUGGY_UART
# define ASYNCB_BUGGY_UART      14
#endif

#ifndef ASYNC_HUP_NOTIFY
# define ASYNC_HUP_NOTIFY       (1U << ASYNCB_HUP_NOTIFY)
#endif
#ifndef ASYNC_FOURPORT
# define ASYNC_FOURPORT         (1U << ASYNCB_FOURPORT)
#endif
#ifndef ASYNC_SAK
# define ASYNC_SAK              (1U << ASYNCB_SAK)
#endif
#ifndef ASYNC_SPLIT_TERMIOS
# define ASYNC_SPLIT_TERMIOS    (1U << ASYNCB_SPLIT_TERMIOS)
#endif
#ifndef ASYNC_SPD_HI
# define ASYNC_SPD_HI           (1U << ASYNCB_SPD_HI)
#endif
#ifndef ASYNC_SPD_VHI
# define ASYNC_SPD_VHI          (1U << ASYNCB_SPD_VHI)
#endif
#ifndef ASYNC_SKIP_TEST
# define ASYNC_SKIP_TEST        (1U << ASYNCB_SKIP_TEST)
#endif
#ifndef ASYNC_AUTO_IRQ
# define ASYNC_AUTO_IRQ         (1U << ASYNCB_AUTO_IRQ)
#endif
#ifndef ASYNC_SESSION_LOCKOUT
# define ASYNC_SESSION_LOCKOUT  (1U << ASYNCB_SESSION_LOCKOUT)
#endif
#ifndef ASYNC_PGRP_LOCKOUT
# define ASYNC_PGRP_LOCKOUT     (1U << ASYNCB_PGRP_LOCKOUT)
#endif
#ifndef ASYNC_CALLOUT_NOHUP
# define ASYNC_CALLOUT_NOHUP    (1U << ASYNCB_CALLOUT_NOHUP)
#endif
#ifndef ASYNC_SPD_SHI
# define ASYNC_SPD_SHI          (1U << ASYNCB_SPD_SHI)
#endif
#ifndef ASYNC_LOW_LATENCY
# define ASYNC_LOW_LATENCY      (1U << ASYNCB_LOW_LATENCY)
#endif
#ifndef ASYNC_BUGGY_UART
# define ASYNC_BUGGY_UART       (1U << ASYNCB_BUGGY_UART)
#endif

#ifndef ASYNC_SPD_CUST
# define ASYNC_SPD_CUST         (ASYNC_SPD_HI|ASYNC_SPD_VHI)
#endif
#ifndef ASYNC_SPD_WARP
# define ASYNC_SPD_WARP         (ASYNC_SPD_HI|ASYNC_SPD_SHI)
#endif
#ifndef ASYNC_SPD_MASK
# define ASYNC_SPD_MASK         (ASYNC_SPD_HI|ASYNC_SPD_VHI|ASYNC_SPD_SHI)
#endif

#ifndef ASYNC_CLOSING_WAIT_INF
# define ASYNC_CLOSING_WAIT_INF         0
#endif
#ifndef ASYNC_CLOSING_WAIT_NONE
# define ASYNC_CLOSING_WAIT_NONE        65535
#endif

#ifndef _LINUX_SERIAL_H
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
	unsigned short	closing_wait; /* time to wait before closing */
	unsigned short	closing_wait2; /* no longer used... */
	unsigned char	*iomem_base;
	unsigned short	iomem_reg_shift;
	unsigned int	port_high;
	unsigned long	iomap_base;	/* cookie passed into ioremap */
};
#endif

//usage:#define setserial_trivial_usage
//usage:	"[-abGvz] { DEVICE [PARAMETER [ARG]]... | -g DEVICE... }"
//usage:#define setserial_full_usage "\n\n"
//usage:	"Print or set serial port parameters"
//usage:   "\n"
//usage:   "\n""	-a	Print all"
//usage:   "\n""	-b	Print summary"
//usage:   "\n""	-G	Print as setserial PARAMETERs"
//usage:   "\n""	-v	Verbose"
//usage:   "\n""	-z	Zero out serial flags before setting"
//usage:   "\n""	-g	All args are device names"
//usage:   "\n"
//usage:   "\n""PARAMETERs: (* = takes ARG, ^ = can be turned off by preceding ^)"
//usage:   "\n""	*port, *irq, *divisor, *uart, *baud_base, *close_delay, *closing_wait,"
//usage:   "\n""	^fourport, ^auto_irq, ^skip_test, ^sak, ^session_lockout, ^pgrp_lockout,"
//usage:   "\n""	^callout_nohup, ^split_termios, ^hup_notify, ^low_latency, autoconfig,"
//usage:   "\n""	spd_normal, spd_hi, spd_vhi, spd_shi, spd_warp, spd_cust"
//usage:   "\n""ARG for uart:"
//usage:   "\n""	unknown, 8250, 16450, 16550, 16550A, Cirrus, 16650, 16650V2, 16750,"
//usage:   "\n""	16950, 16954, 16654, 16850, RSA, NS16550A, XSCALE, RM9000, OCTEON, AR7,"
//usage:   "\n""	U6_16550A"

// option string is "bGavzgq". "q" is accepted but ignored.
#define OPT_PRINT_SUMMARY       (1 << 0)
#define OPT_PRINT_FEDBACK       (1 << 1)
#define OPT_PRINT_ALL           (1 << 2)
#define OPT_VERBOSE             (1 << 3)
#define OPT_ZERO                (1 << 4)
#define OPT_LIST_OF_DEVS        (1 << 5)
/*#define OPT_QUIET             (1 << 6)*/

#define OPT_MODE_MASK \
	(OPT_PRINT_ALL | OPT_PRINT_SUMMARY | OPT_PRINT_FEDBACK)

enum print_mode
{
	PRINT_NORMAL  = 0,
	PRINT_SUMMARY = (1 << 0),
	PRINT_FEDBACK = (1 << 1),
	PRINT_ALL     = (1 << 2),
};

#define CTL_SET                 (1 << 0)
#define CTL_CONFIG              (1 << 1)
#define CTL_GET                 (1 << 2)
#define CTL_CLOSE               (1 << 3)
#define CTL_NODIE               (1 << 4)

static const char serial_types[] ALIGN1 =
	"unknown\0"		/* 0 */
	"8250\0"		/* 1 */
	"16450\0"		/* 2 */
	"16550\0"		/* 3 */
	"16550A\0"		/* 4 */
	"Cirrus\0"		/* 5 */
	"16650\0"		/* 6 */
	"16650V2\0"		/* 7 */
	"16750\0"		/* 8 */
	"16950\0"		/* 9 UNIMPLEMENTED: also know as "16950/954" */
	"16954\0"		/* 10 */
	"16654\0"		/* 11 */
	"16850\0"		/* 12 */
	"RSA\0"			/* 13 */
#ifndef SETSERIAL_BASE
	"NS16550A\0"		/* 14 */
	"XSCALE\0"		/* 15 */
	"RM9000\0"		/* 16 */
	"OCTEON\0"		/* 17 */
	"AR7\0"			/* 18 */
	"U6_16550A\0"		/* 19 */
#endif
;

#ifndef SETSERIAL_BASE
# define MAX_SERIAL_TYPE	19
#else
# define MAX_SERIAL_TYPE	13
#endif

static const char commands[] ALIGN1 =
	"spd_normal\0"
	"spd_hi\0"
	"spd_vhi\0"
	"spd_shi\0"
	"spd_warp\0"
	"spd_cust\0"

	"sak\0"
	"fourport\0"
	"hup_notify\0"
	"skip_test\0"
	"auto_irq\0"
	"split_termios\0"
	"session_lockout\0"
	"pgrp_lockout\0"
	"callout_nohup\0"
	"low_latency\0"

	"port\0"
	"irq\0"
	"divisor\0"
	"uart\0"
	"baud_base\0"
	"close_delay\0"
	"closing_wait\0"

	"autoconfig\0"
;

enum
{
	CMD_SPD_NORMAL = 0,
	CMD_SPD_HI,
	CMD_SPD_VHI,
	CMD_SPD_SHI,
	CMD_SPD_WARP,
	CMD_SPD_CUST,

	CMD_FLAG_SAK,
	CMD_FLAG_FOURPORT,
	CMD_FLAG_NUP_NOTIFY,
	CMD_FLAG_SKIP_TEST,
	CMD_FLAG_AUTO_IRQ,
	CMD_FLAG_SPLIT_TERMIOS,
	CMD_FLAG_SESSION_LOCKOUT,
	CMD_FLAG_PGRP_LOCKOUT,
	CMD_FLAG_CALLOUT_NOHUP,
	CMD_FLAG_LOW_LATENCY,

	CMD_PORT,
	CMD_IRQ,
	CMD_DIVISOR,
	CMD_UART,
	CMD_BASE,
	CMD_DELAY,
	CMD_WAIT,

	CMD_AUTOCONFIG,

	CMD_FLAG_FIRST = CMD_FLAG_SAK,
	CMD_FLAG_LAST  = CMD_FLAG_LOW_LATENCY,
};

static bool cmd_noprint(int cmd)
{
	return (cmd >= CMD_FLAG_SKIP_TEST && cmd <= CMD_FLAG_CALLOUT_NOHUP);
}

static bool cmd_is_flag(int cmd)
{
	return (cmd >= CMD_FLAG_FIRST && cmd <= CMD_FLAG_LAST);
}

static bool cmd_needs_arg(int cmd)
{
	return (cmd >= CMD_PORT && cmd <= CMD_WAIT);
}

#define ALL_SPD ( \
	ASYNC_SPD_HI | ASYNC_SPD_VHI | ASYNC_SPD_SHI | \
	ASYNC_SPD_WARP | ASYNC_SPD_CUST \
	)

#define ALL_FLAGS ( \
	ASYNC_SAK | ASYNC_FOURPORT | ASYNC_HUP_NOTIFY | \
	ASYNC_SKIP_TEST | ASYNC_AUTO_IRQ | ASYNC_SPLIT_TERMIOS | \
	ASYNC_SESSION_LOCKOUT | ASYNC_PGRP_LOCKOUT | ASYNC_CALLOUT_NOHUP | \
	ASYNC_LOW_LATENCY \
	)

#if (ALL_SPD | ALL_FLAGS) > 0xffff
# error "Unexpected flags size"
#endif

static const uint16_t setbits[CMD_FLAG_LAST + 1] =
{
	0,
	ASYNC_SPD_HI,
	ASYNC_SPD_VHI,
	ASYNC_SPD_SHI,
	ASYNC_SPD_WARP,
	ASYNC_SPD_CUST,

	ASYNC_SAK,
	ASYNC_FOURPORT,
	ASYNC_HUP_NOTIFY,
	ASYNC_SKIP_TEST,
	ASYNC_AUTO_IRQ,
	ASYNC_SPLIT_TERMIOS,
	ASYNC_SESSION_LOCKOUT,
	ASYNC_PGRP_LOCKOUT,
	ASYNC_CALLOUT_NOHUP,
	ASYNC_LOW_LATENCY
};

#define STR_INFINITE "infinite"
#define STR_NONE     "none"

static const char *uart_type(int type)
{
	if (type > MAX_SERIAL_TYPE)
		return "undefined";

	return nth_string(serial_types, type);
}

/* libbb candidate */
static int index_in_strings_case_insensitive(const char *strings, const char *key)
{
	int idx = 0;

	while (*strings) {
		if (strcasecmp(strings, key) == 0) {
			return idx;
		}
		strings += strlen(strings) + 1; /* skip NUL */
		idx++;
	}
	return -1;
}

static int uart_id(const char *name)
{
	return index_in_strings_case_insensitive(serial_types, name);
}

static const char *get_spd(int flags, enum print_mode mode)
{
	int idx;

	switch (flags & ASYNC_SPD_MASK) {
	case ASYNC_SPD_HI:
		idx = CMD_SPD_HI;
		break;
	case ASYNC_SPD_VHI:
		idx = CMD_SPD_VHI;
		break;
	case ASYNC_SPD_SHI:
		idx = CMD_SPD_SHI;
		break;
	case ASYNC_SPD_WARP:
		idx = CMD_SPD_WARP;
		break;
	case ASYNC_SPD_CUST:
		idx = CMD_SPD_CUST;
		break;
	default:
		if (mode < PRINT_FEDBACK)
			return NULL;
		idx = CMD_SPD_NORMAL;
	}

	return nth_string(commands, idx);
}

static int get_numeric(const char *arg)
{
	return bb_strtol(arg, NULL, 0);
}

static int get_wait(const char *arg)
{
	if (strcasecmp(arg, STR_NONE) == 0)
		return ASYNC_CLOSING_WAIT_NONE;

	if (strcasecmp(arg, STR_INFINITE) == 0)
		return ASYNC_CLOSING_WAIT_INF;

	return get_numeric(arg);
}

static int get_uart(const char *arg)
{
	int uart = uart_id(arg);

	if (uart < 0)
		bb_error_msg_and_die("illegal UART type: %s", arg);

	return uart;
}

static int serial_open(const char *dev, bool quiet)
{
	int fd;

	fd = device_open(dev, O_RDWR | O_NONBLOCK);
	if (fd < 0 && !quiet)
		bb_simple_perror_msg(dev);

	return fd;
}

static int serial_ctl(int fd, int ops, struct serial_struct *serinfo)
{
	int ret = 0;
	const char *err;

	if (ops & CTL_SET) {
		ret = ioctl(fd, TIOCSSERIAL, serinfo);
		if (ret < 0) {
			err = "can't set serial info";
			goto fail;
		}
	}

	if (ops & CTL_CONFIG) {
		ret = ioctl(fd, TIOCSERCONFIG);
		if (ret < 0) {
			err = "can't autoconfigure port";
			goto fail;
		}
	}

	if (ops & CTL_GET) {
		ret = ioctl(fd, TIOCGSERIAL, serinfo);
		if (ret < 0) {
			err = "can't get serial info";
			goto fail;
		}
	}
 nodie:
	if (ops & CTL_CLOSE)
		close(fd);

	return ret;
 fail:
	bb_simple_perror_msg(err);
	if (ops & CTL_NODIE)
		goto nodie;
	exit(EXIT_FAILURE);
}

static void print_flag(const char **prefix, const char *flag)
{
	printf("%s%s", *prefix, flag);
	*prefix = " ";
}

static void print_serial_flags(int serial_flags, enum print_mode mode,
				const char *prefix, const char *postfix)
{
	int i;
	const char *spd, *pr;

	pr = prefix;

	spd = get_spd(serial_flags, mode);
	if (spd)
		print_flag(&pr, spd);

	for (i = CMD_FLAG_FIRST; i <= CMD_FLAG_LAST; i++) {
		if ((serial_flags & setbits[i])
		 && (mode > PRINT_SUMMARY || !cmd_noprint(i))
		) {
			print_flag(&pr, nth_string(commands, i));
		}
	}

	puts(pr == prefix ? "" : postfix);
}

static void print_closing_wait(unsigned int closing_wait)
{
	switch (closing_wait) {
	case ASYNC_CLOSING_WAIT_NONE:
		puts(STR_NONE);
		break;
	case ASYNC_CLOSING_WAIT_INF:
		puts(STR_INFINITE);
		break;
	default:
		printf("%u\n", closing_wait);
	}
}

static void serial_get(const char *device, enum print_mode mode)
{
	int fd, ret;
	const char *uart, *prefix, *postfix;
	struct serial_struct serinfo;

	fd = serial_open(device, /*quiet:*/ mode == PRINT_SUMMARY);
	if (fd < 0)
		return;

	ret = serial_ctl(fd, CTL_GET | CTL_CLOSE | CTL_NODIE, &serinfo);
	if (ret < 0)
		return;

	uart = uart_type(serinfo.type);
	prefix = ", Flags: ";
	postfix = "";

	switch (mode) {
	case PRINT_NORMAL:
		printf("%s, UART: %s, Port: 0x%.4x, IRQ: %d",
			device, uart, serinfo.port, serinfo.irq);
		break;
	case PRINT_SUMMARY:
		if (!serinfo.type)
			return;
		printf("%s at 0x%.4x (irq = %d) is a %s",
			device, serinfo.port, serinfo.irq, uart);
		prefix = " (";
		postfix = ")";
		break;
	case PRINT_FEDBACK:
		printf("%s uart %s port 0x%.4x irq %d baud_base %d", device,
			uart, serinfo.port, serinfo.irq, serinfo.baud_base);
		prefix = " ";
		break;
	case PRINT_ALL:
		printf("%s, Line %d, UART: %s, Port: 0x%.4x, IRQ: %d\n",
			device, serinfo.line, uart, serinfo.port, serinfo.irq);
		printf("\tBaud_base: %d, close_delay: %u, divisor: %d\n",
			serinfo.baud_base, serinfo.close_delay,
			serinfo.custom_divisor);
		printf("\tclosing_wait: ");
		print_closing_wait(serinfo.closing_wait);
		prefix = "\tFlags: ";
		postfix = "\n";
		break;
	default:
		assert(0);
	}

	print_serial_flags(serinfo.flags, mode, prefix, postfix);
}

static int find_cmd(const char *cmd)
{
	int idx;

	idx = index_in_strings_case_insensitive(commands, cmd);
	if (idx < 0)
		bb_error_msg_and_die("invalid flag: %s", cmd);

	return idx;
}

static void serial_set(char **arg, int opts)
{
	struct serial_struct serinfo;
	int fd;

	fd = serial_open(*arg, /*quiet:*/ false);
	if (fd < 0)
		exit(201);

	serial_ctl(fd, CTL_GET, &serinfo);

	if (opts & OPT_ZERO)
		serinfo.flags = 0;

	while (*++arg) {
		const char *word;
		int invert;
		int cmd;

		word = *arg;
		invert = (word[0] == '^');
		word += invert;

		cmd = find_cmd(word);

		if (cmd_needs_arg(cmd))
			if (*++arg == NULL)
				bb_error_msg_and_die(bb_msg_requires_arg, word);

		if (invert && !cmd_is_flag(cmd))
			bb_error_msg_and_die("can't invert %s", word);

		switch (cmd) {
		case CMD_SPD_NORMAL:
		case CMD_SPD_HI:
		case CMD_SPD_VHI:
		case CMD_SPD_SHI:
		case CMD_SPD_WARP:
		case CMD_SPD_CUST:
			serinfo.flags &= ~ASYNC_SPD_MASK;
			/* fallthrough */
		case CMD_FLAG_SAK:
		case CMD_FLAG_FOURPORT:
		case CMD_FLAG_NUP_NOTIFY:
		case CMD_FLAG_SKIP_TEST:
		case CMD_FLAG_AUTO_IRQ:
		case CMD_FLAG_SPLIT_TERMIOS:
		case CMD_FLAG_SESSION_LOCKOUT:
		case CMD_FLAG_PGRP_LOCKOUT:
		case CMD_FLAG_CALLOUT_NOHUP:
		case CMD_FLAG_LOW_LATENCY:
			if (invert)
				serinfo.flags &= ~setbits[cmd];
			else
				serinfo.flags |= setbits[cmd];
			break;
		case CMD_PORT:
			serinfo.port = get_numeric(*arg);
			break;
		case CMD_IRQ:
			serinfo.irq = get_numeric(*arg);
			break;
		case CMD_DIVISOR:
			serinfo.custom_divisor = get_numeric(*arg);
			break;
		case CMD_UART:
			serinfo.type = get_uart(*arg);
			break;
		case CMD_BASE:
			serinfo.baud_base = get_numeric(*arg);
			break;
		case CMD_DELAY:
			serinfo.close_delay = get_numeric(*arg);
			break;
		case CMD_WAIT:
			serinfo.closing_wait = get_wait(*arg);
			break;
		case CMD_AUTOCONFIG:
			serial_ctl(fd, CTL_SET | CTL_CONFIG | CTL_GET, &serinfo);
			break;
		default:
			assert(0);
		}
	}

	serial_ctl(fd, CTL_SET | CTL_CLOSE, &serinfo);
}

int setserial_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int setserial_main(int argc UNUSED_PARAM, char **argv)
{
	int opts;

	opts = getopt32(argv, "^" "bGavzgq" "\0" "-1:b-aG:G-ab:a-bG");
	argv += optind;

	if (!argv[1]) /* one arg only? (nothing to change?) */
		opts |= OPT_LIST_OF_DEVS; /* force display */

	if (!(opts & OPT_LIST_OF_DEVS)) {
		serial_set(argv, opts);
		argv[1] = NULL;
	}

	/* -v effect: "after setting params, do not be silent, show them" */
	if (opts & (OPT_VERBOSE | OPT_LIST_OF_DEVS)) {
		do {
			serial_get(*argv, opts & OPT_MODE_MASK);
		} while (*++argv);
	}

	return EXIT_SUCCESS;
}
