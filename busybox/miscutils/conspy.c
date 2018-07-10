/* vi: set sw=4 ts=4: */
/*
 * A text-mode VNC like program for Linux virtual terminals.
 *
 * pascal.bellard@ads-lu.com
 *
 * Based on Russell Stuart's conspy.c
 *   http://ace-host.stuart.id.au/russell/files/conspy.c
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
//config:config CONSPY
//config:	bool "conspy (10 kb)"
//config:	default y
//config:	select PLATFORM_LINUX
//config:	help
//config:	A text-mode VNC like program for Linux virtual terminals.
//config:	example:  conspy NUM      shared access to console num
//config:	or        conspy -nd NUM  screenshot of console num
//config:	or        conspy -cs NUM  poor man's GNU screen like

//applet:IF_CONSPY(APPLET(conspy, BB_DIR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_CONSPY) += conspy.o

//usage:#define conspy_trivial_usage
//usage:	"[-vcsndfFQ] [-x COL] [-y LINE] [CONSOLE_NO]"
//usage:#define conspy_full_usage "\n\n"
//usage:     "A text-mode VNC like program for Linux virtual consoles."
//usage:     "\nTo exit, quickly press ESC 3 times."
//usage:     "\n"
//usage:     "\n	-v	Don't send keystrokes to the console"
//usage:     "\n	-c	Create missing /dev/{tty,vcsa}N"
//usage:     "\n	-s	Open a SHELL session"
//usage:     "\n	-n	Black & white"
//usage:     "\n	-d	Dump console to stdout"
//usage:     "\n	-f	Follow cursor"
//usage:     "\n	-F	Assume console is on a framebuffer device"
//usage:     "\n	-Q	Disable exit on ESC-ESC-ESC"
//usage:     "\n	-x COL	Starting column"
//usage:     "\n	-y LINE	Starting line"

#include "libbb.h"
#include "common_bufsiz.h"
#include <sys/kd.h>

#define ESC "\033"
#define CURSOR_ON	-1
#define CURSOR_OFF	1

#define DEV_TTY		"/dev/tty"
#define DEV_VCSA	"/dev/vcsa"

struct screen_info {
	unsigned char lines, cols, cursor_x, cursor_y;
};

#define CHAR(x) (*(uint8_t*)(x))
#define ATTR(x) (((uint8_t*)(x))[1])
#define NEXT(x) ((x) += 2)
#define DATA(x) (*(uint16_t*)(x))

struct globals {
	char* data;
	int size;
	int x, y;
	int kbd_fd;
	int ioerror_count;
	int key_count;
	int escape_count;
	int nokeys;
	int current;
	int first_line_offset;
	int last_attr;
	// cached local tty parameters
	unsigned width;
	unsigned height;
	unsigned col;
	unsigned line;
	smallint curoff; // unknown:0 cursor on:-1 cursor off:1
	char attrbuf[sizeof("0;1;5;30;40m")];
	// remote console
	struct screen_info remote;
	// saved local tty terminfo
	struct termios term_orig;
	char vcsa_name[sizeof(DEV_VCSA "NN")];
};

#define G (*ptr_to_globals)
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
	G.width = G.height = UINT_MAX; \
	G.last_attr--; \
} while (0)

enum {
	FLAG_v,  // view only
	FLAG_c,  // create device if need
	FLAG_Q,  // never exit
	FLAG_s,  // session
	FLAG_n,  // no colors
	FLAG_d,  // dump screen
	FLAG_f,  // follow cursor
	FLAG_F,  // framebuffer
};
#define FLAG(x) (1 << FLAG_##x)
#define BW (option_mask32 & FLAG(n))

static void putcsi(const char *s)
{
	fputs(ESC"[", stdout);
	fputs(s, stdout);
}

static void clrscr(void)
{
	// Home, clear till end of screen
	putcsi("1;1H" ESC"[J");
	G.col = G.line = 0;
}

static void set_cursor(int state)
{
	if (G.curoff != state) {
		G.curoff = state;
		putcsi("?25");
		bb_putchar("h?l"[1 + state]);
	}
}

static void gotoxy(int col, int line)
{
	if (G.col != col || G.line != line) {
		G.col = col;
		G.line = line;
		printf(ESC"[%u;%uH", line + 1, col + 1);
	}
}

static void cleanup(int code) NORETURN;
static void cleanup(int code)
{
	set_cursor(CURSOR_ON);
	tcsetattr(G.kbd_fd, TCSANOW, &G.term_orig);
	if (ENABLE_FEATURE_CLEAN_UP) {
		close(G.kbd_fd);
	}
	// Reset attributes
	if (!BW)
		putcsi("0m");
	bb_putchar('\n');
	if (code > EXIT_FAILURE)
		kill_myself_with_sig(code);
	exit(code);
}

static void screen_read_close(void)
{
	unsigned i, j;
	int vcsa_fd;
	char *data;

	// Close & re-open vcsa in case they have swapped virtual consoles
	vcsa_fd = xopen(G.vcsa_name, O_RDONLY);
	xread(vcsa_fd, &G.remote, 4);
	i = G.remote.cols * 2;
	G.first_line_offset = G.y * i;
	i *= G.remote.lines;
	if (G.data == NULL) {
		G.size = i;
		G.data = xzalloc(2 * i);
	}
	if (G.size != i) {
		cleanup(EXIT_FAILURE);
	}
	data = G.data + G.current;
	xread(vcsa_fd, data, G.size);
	close(vcsa_fd);
	for (i = 0; i < G.remote.lines; i++) {
		for (j = 0; j < G.remote.cols; j++, NEXT(data)) {
			unsigned x = j - G.x; // if will catch j < G.x too
			unsigned y = i - G.y; // if will catch i < G.y too

			if (y >= G.height || x >= G.width)
				DATA(data) = 0;
			else {
				uint8_t ch = CHAR(data);
				if (ch < ' ')
					CHAR(data) = ch | 0x40;
				else if (ch > 0x7e)
					CHAR(data) = '?';
			}
		}
	}
}

static void screen_char(char *data)
{
	if (!BW) {
		uint8_t attr_diff;
		uint8_t attr = ATTR(data);

		if (option_mask32 & FLAG(F)) {
			attr >>= 1;
		}
		attr_diff = G.last_attr ^ attr;
		if (attr_diff) {
// Attribute layout for VGA compatible text videobuffer:
// blinking text
// |red bkgd
// ||green bkgd
// |||blue bkgd
// vvvv
// 00000000 <- lsb bit on the right
//     bold text / text 8th bit
//      red text
//       green text
//        blue text
// TODO: apparently framebuffer-based console uses different layout
// (bug? attempt to get 8th text bit in better position?)
// red bkgd
// |green bkgd
// ||blue bkgd
// vvv
// 00000000 <- lsb bit on the right
//    bold text
//     red text
//      green text
//       blue text
//        text 8th bit
			// converting RGB color bit triad to BGR:
			static const char color[8] = "04261537";
			const uint8_t fg_mask = 0x07, bold_mask  = 0x08;
			const uint8_t bg_mask = 0x70, blink_mask = 0x80;
			char *ptr;

			ptr = G.attrbuf;

			// (G.last_attr & ~attr) has 1 only where
			// G.last_attr has 1 but attr has 0.
			// Here we check whether we have transition
			// bold->non-bold or blink->non-blink:
			if (G.last_attr < 0  // initial value
			 || ((G.last_attr & ~attr) & (bold_mask | blink_mask)) != 0
			) {
				*ptr++ = '0'; // "reset all attrs"
				*ptr++ = ';';
				// must set fg & bg, maybe need to set bold or blink:
				attr_diff = attr | ~(bold_mask | blink_mask);
			}
			G.last_attr = attr;
			if (attr_diff & bold_mask) {
				*ptr++ = '1';
				*ptr++ = ';';
			}
			if (attr_diff & blink_mask) {
				*ptr++ = '5';
				*ptr++ = ';';
			}
			if (attr_diff & fg_mask) {
				*ptr++ = '3';
				*ptr++ = color[attr & fg_mask];
				*ptr++ = ';';
			}
			if (attr_diff & bg_mask) {
				*ptr++ = '4';
				*ptr++ = color[(attr & bg_mask) >> 4];
				ptr++; // last attribute
			}
			if (ptr != G.attrbuf) {
				ptr[-1] = 'm';
				*ptr = '\0';
				putcsi(G.attrbuf);
			}
		}
	}
	putchar(CHAR(data));
	G.col++;
}

static void screen_dump(void)
{
	int linefeed_cnt;
	int line, col;
	int linecnt = G.remote.lines - G.y;
	char *data = G.data + G.current + G.first_line_offset;

	linefeed_cnt = 0;
	for (line = 0; line < linecnt && line < G.height; line++) {
		int space_cnt = 0;
		for (col = 0; col < G.remote.cols; col++, NEXT(data)) {
			unsigned tty_col = col - G.x; // if will catch col < G.x too

			if (tty_col >= G.width)
				continue;
			space_cnt++;
			if (BW && CHAR(data) == ' ')
				continue;
			while (linefeed_cnt != 0) {
				//bb_putchar('\r'); - tty driver does it for us
				bb_putchar('\n');
				linefeed_cnt--;
			}
			while (--space_cnt)
				bb_putchar(' ');
			screen_char(data);
		}
		linefeed_cnt++;
	}
}

static void curmove(void)
{
	unsigned cx = G.remote.cursor_x - G.x;
	unsigned cy = G.remote.cursor_y - G.y;
	int cursor = CURSOR_OFF;

	if (cx < G.width && cy < G.height) {
		gotoxy(cx, cy);
		cursor = CURSOR_ON;
	}
	set_cursor(cursor);
}

static void create_cdev_if_doesnt_exist(const char* name, dev_t dev)
{
	int fd = open(name, O_RDONLY);
	if (fd != -1)
		close(fd);
	else if (errno == ENOENT)
		mknod(name, S_IFCHR | 0660, dev);
}

static NOINLINE void start_shell_in_child(const char* tty_name)
{
	int pid = xvfork();
	if (pid == 0) {
		struct termios termchild;
		const char *shell = get_shell_name();

		signal(SIGHUP, SIG_IGN);
		// set tty as a controlling tty
		setsid();
		// make tty to be input, output, error
		close(0);
		xopen(tty_name, O_RDWR); // uses fd 0
		xdup2(0, 1);
		xdup2(0, 2);
		ioctl(0, TIOCSCTTY, 1);
		tcsetpgrp(0, getpid());
		tcgetattr(0, &termchild);
		termchild.c_lflag |= ECHO;
		termchild.c_oflag |= ONLCR | XTABS;
		termchild.c_iflag |= ICRNL;
		termchild.c_iflag &= ~IXOFF;
		tcsetattr_stdin_TCSANOW(&termchild);
		execl(shell, shell, "-i", (char *) NULL);
		bb_simple_perror_msg_and_die(shell);
	}
}

int conspy_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int conspy_main(int argc UNUSED_PARAM, char **argv)
{
	char tty_name[sizeof(DEV_TTY "NN")];
	unsigned opts;
	unsigned ttynum;
	int poll_timeout_ms;
#if ENABLE_LONG_OPTS
	static const char conspy_longopts[] ALIGN1 =
		"viewonly\0"     No_argument "v"
		"createdevice\0" No_argument "c"
		"neverquit\0"    No_argument "Q"
		"session\0"      No_argument "s"
		"nocolors\0"     No_argument "n"
		"dump\0"         No_argument "d"
		"follow\0"       No_argument "f"
		"framebuffer\0"  No_argument "F"
		;
#endif
#define keybuf bb_common_bufsiz1
	setup_common_bufsiz();

	INIT_G();
	strcpy(G.vcsa_name, DEV_VCSA);

	// numeric params
	opts = getopt32long(argv, "vcQsndfFx:+y:+", conspy_longopts, &G.x, &G.y);
	argv += optind;
	ttynum = 0;
	if (argv[0]) {
		ttynum = xatou_range(argv[0], 0, 63);
		sprintf(G.vcsa_name + sizeof(DEV_VCSA)-1, "%u", ttynum);
	}
	sprintf(tty_name, "%s%u", DEV_TTY, ttynum);
	if (opts & FLAG(c)) {
		if ((opts & (FLAG(s)|FLAG(v))) != FLAG(v))
			create_cdev_if_doesnt_exist(tty_name, makedev(4, ttynum));
		create_cdev_if_doesnt_exist(G.vcsa_name, makedev(7, 128 + ttynum));
	}
	if ((opts & FLAG(s)) && ttynum) {
		start_shell_in_child(tty_name);
	}

	screen_read_close();
	if (opts & FLAG(d)) {
		screen_dump();
		bb_putchar('\n');
		return 0;
	}

	bb_signals(BB_FATAL_SIGS, cleanup);

	G.kbd_fd = xopen(CURRENT_TTY, O_RDONLY);

	// All characters must be passed through to us unaltered
	set_termios_to_raw(G.kbd_fd, &G.term_orig, 0
		| TERMIOS_CLEAR_ISIG // no signals on ^C ^Z etc
		| TERMIOS_RAW_INPUT  // turn off all input conversions
	);
	//Note: termios.c_oflag &= ~(OPOST); - no, we still want \n -> \r\n

	poll_timeout_ms = 250;
	while (1) {
		struct pollfd pfd;
		int bytes_read;
		int i, j;
		char *data, *old;

		// in the first loop G.width = G.height = 0: refresh
		i = G.width;
		j = G.height;
		get_terminal_width_height(G.kbd_fd, &G.width, &G.height);
		if (option_mask32 & FLAG(f)) {
			int nx = G.remote.cursor_x - G.width + 1;
			int ny = G.remote.cursor_y - G.height + 1;

			if (G.remote.cursor_x < G.x) {
				G.x = G.remote.cursor_x;
				i = 0; // force refresh
			}
			if (nx > G.x) {
				G.x = nx;
				i = 0; // force refresh
			}
			if (G.remote.cursor_y < G.y) {
				G.y = G.remote.cursor_y;
				i = 0; // force refresh
			}
			if (ny > G.y) {
				G.y = ny;
				i = 0; // force refresh
			}
		}

		// Scan console data and redraw our tty where needed
		old = G.data + G.current;
		G.current = G.size - G.current;
		data = G.data + G.current;
		screen_read_close();
		if (i != G.width || j != G.height) {
			clrscr();
			screen_dump();
		} else {
			// For each remote line
			old += G.first_line_offset;
			data += G.first_line_offset;
			for (i = G.y; i < G.remote.lines; i++) {
				char *first = NULL; // first char which needs updating
				char *last = last;  // last char which needs updating
				unsigned iy = i - G.y;

				if (iy >= G.height)
					break;
				for (j = 0; j < G.remote.cols; j++, NEXT(old), NEXT(data)) {
					unsigned jx = j - G.x; // if will catch j >= G.x too

					if (jx < G.width && DATA(data) != DATA(old)) {
						last = data;
						if (!first) {
							first = data;
							gotoxy(jx, iy);
						}
					}
				}
				if (first) {
					// Rewrite updated data on the local screen
					for (; first <= last; NEXT(first))
						screen_char(first);
				}
			}
		}
		curmove();

		// Wait for local user keypresses
		fflush_all();
		pfd.fd = G.kbd_fd;
		pfd.events = POLLIN;
		bytes_read = 0;
		switch (poll(&pfd, 1, poll_timeout_ms)) {
			char *k;
		case -1:
			if (errno != EINTR)
				goto abort;
			break;
		case 0:
			if (++G.nokeys >= 4)
				G.nokeys = G.escape_count = 0;
			break;
		default:
			// Read the keys pressed
			k = keybuf + G.key_count;
			bytes_read = read(G.kbd_fd, k, COMMON_BUFSIZE - G.key_count);
			if (bytes_read < 0)
				goto abort;

			// Do exit processing
			if (!(option_mask32 & FLAG(Q))) {
				for (i = 0; i < bytes_read; i++) {
					if (k[i] != '\033')
						G.escape_count = -1;
					if (++G.escape_count >= 3)
						cleanup(EXIT_SUCCESS);
				}
			}
		}
		poll_timeout_ms = 250;
		if (option_mask32 & FLAG(v)) continue;

		// Insert all keys pressed into the virtual console's input
		// buffer.  Don't do this if the virtual console is in scan
		// code mode - giving ASCII characters to a program expecting
		// scan codes will confuse it.
		G.key_count += bytes_read;
		if (G.escape_count == 0) {
			int handle, result;
			long kbd_mode;

			handle = xopen(tty_name, O_WRONLY);
			result = ioctl(handle, KDGKBMODE, &kbd_mode);
			if (result >= 0) {
				char *p = keybuf;

				G.ioerror_count = 0;
				if (kbd_mode != K_XLATE && kbd_mode != K_UNICODE) {
					G.key_count = 0; // scan code mode
				}
				for (; G.key_count != 0; p++, G.key_count--) {
					result = ioctl(handle, TIOCSTI, p);
					if (result < 0) {
						memmove(keybuf, p, G.key_count);
						break;
					}
					// If there is an application on console which reacts
					// to keypresses, we need to make our first sleep
					// shorter to quickly redraw whatever it printed there.
					poll_timeout_ms = 20;
				}
			}
			// We sometimes get spurious IO errors on the TTY
			// as programs close and re-open it
			else if (errno != EIO || ++G.ioerror_count > 4) {
				if (ENABLE_FEATURE_CLEAN_UP)
					close(handle);
				goto abort;
			}
			// Close & re-open tty in case they have
			// swapped virtual consoles
			close(handle);
		}
	} /* while (1) */
  abort:
	cleanup(EXIT_FAILURE);
}
