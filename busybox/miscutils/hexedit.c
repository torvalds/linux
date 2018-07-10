/*
 * Copyright (C) 2017 Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
//config:config HEXEDIT
//config:	bool "hexedit (20 kb)"
//config:	default y
//config:	help
//config:	Edit file in hexadecimal.

//applet:IF_HEXEDIT(APPLET(hexedit, BB_DIR_USR_BIN, BB_SUID_DROP))

//kbuild:lib-$(CONFIG_HEXEDIT) += hexedit.o

#include "libbb.h"

#define ESC		"\033"
#define HOME		ESC"[H"
#define CLEAR		ESC"[J"
#define CLEAR_TILL_EOL	ESC"[K"
#define SET_ALT_SCR	ESC"[?1049h"
#define POP_ALT_SCR	ESC"[?1049l"

#undef CTRL
#define CTRL(c)  ((c) & (uint8_t)~0x60)

struct globals {
	smallint half;
	smallint in_read_key;
	int fd;
	unsigned height;
	unsigned row;
	unsigned pagesize;
	uint8_t *baseaddr;
	uint8_t *current_byte;
	uint8_t *eof_byte;
	off_t size;
	off_t offset;
	/* needs to be zero-inited, thus keeping it in G: */
	char read_key_buffer[KEYCODE_BUFFER_SIZE];
	struct termios orig_termios;
};
#define G (*ptr_to_globals)
#define INIT_G() do { \
	SET_PTR_TO_GLOBALS(xzalloc(sizeof(G))); \
} while (0)

//TODO: move to libbb
#if defined(__x86_64__) || defined(i386)
# define G_pagesize 4096
# define INIT_PAGESIZE() ((void)0)
#else
# define G_pagesize (G.pagesize)
# define INIT_PAGESIZE() ((void)(G.pagesize = getpagesize()))
#endif

/* hopefully there aren't arches with PAGE_SIZE > 64k */
#define G_mapsize  (64*1024)

/* "12ef5670 (xx )*16 _1_3_5_7_9abcdef\n"NUL */
#define LINEBUF_SIZE (8 + 1 + 3*16 + 16 + 1 + 1 /*paranoia:*/ + 13)

static void restore_term(void)
{
	tcsetattr_stdin_TCSANOW(&G.orig_termios);
	printf(POP_ALT_SCR);
	fflush_all();
}

static void sig_catcher(int sig)
{
	if (!G.in_read_key) {
		/* now it's not safe to do I/O, just inform the main loop */
		bb_got_signal = sig;
		return;
	}
	restore_term();
	kill_myself_with_sig(sig);
}

static int format_line(char *hex, uint8_t *data, off_t offset)
{
	int ofs_pos;
	char *text;
	uint8_t *end, *end1;

#if 1
	/* Can be more than 4Gb, thus >8 chars, thus use a variable - don't assume 8! */
	ofs_pos = sprintf(hex, "%08"OFF_FMT"x ", offset);
#else
	if (offset <= 0xffff)
		ofs_pos = sprintf(hex, "%04"OFF_FMT"x ", offset);
	else
		ofs_pos = sprintf(hex, "%08"OFF_FMT"x ", offset);
#endif
	hex += ofs_pos;

	text = hex + 16 * 3;
	end1 = data + 15;
	if ((G.size - offset) > 0) {
		end = end1;
		if ((G.size - offset) <= 15)
			end = data + (G.size - offset) - 1;
		while (data <= end) {
			uint8_t c = *data++;
			*hex++ = bb_hexdigits_upcase[c >> 4];
			*hex++ = bb_hexdigits_upcase[c & 0xf];
			*hex++ = ' ';
			if (c < ' ' || c > 0x7e)
				c = '.';
			*text++ = c;
		}
	}
	while (data <= end1) {
		*hex++ = ' ';
		*hex++ = ' ';
		*hex++ = ' ';
		*text++ = ' ';
		data++;
	}
	*text = '\0';

	return ofs_pos;
}

static void redraw(unsigned cursor)
{
	uint8_t *data;
	off_t offset;
	unsigned i, pos;

	printf(HOME CLEAR);

	/* if cursor is past end of screen, how many lines to move down? */
	i = (cursor / 16) - G.height + 1;
	if ((int)i < 0)
		i = 0;

	data = G.baseaddr + i * 16;
	offset = G.offset + i * 16;
	cursor -= i * 16;
	pos = i = 0;
	while (i < G.height) {
		char buf[LINEBUF_SIZE];
		pos = format_line(buf, data, offset);
		printf(
			"\r\n%s" + (!i) * 2, /* print \r\n only on 2nd line and later */
			buf
		);
		data += 16;
		offset += 16;
		i++;
	}

	printf(ESC"[%u;%uH", 1 + cursor / 16, 1 + pos + (cursor & 0xf) * 3);
}

static void redraw_cur_line(void)
{
	char buf[LINEBUF_SIZE];
	uint8_t *data;
	off_t offset;
	int column;

	column = (0xf & (uintptr_t)G.current_byte);
	data = G.current_byte - column;
	offset = G.offset + (data - G.baseaddr);

	column = column*3 + G.half;
	column += format_line(buf, data, offset);
	printf("%s"
		"\r"
		"%.*s",
		buf + column,
		column, buf
	);
}

/* if remappers return 0, no change was done */
static int remap(unsigned cur_pos)
{
	if (G.baseaddr)
		munmap(G.baseaddr, G_mapsize);

	G.baseaddr = mmap(NULL,
		G_mapsize,
		PROT_READ | PROT_WRITE,
		MAP_SHARED,
		G.fd,
		G.offset
	);
	if (G.baseaddr == MAP_FAILED) {
		restore_term();
		bb_perror_msg_and_die("mmap");
	}

	G.current_byte = G.baseaddr + cur_pos;

	G.eof_byte = G.baseaddr + G_mapsize;
	if ((G.size - G.offset) < G_mapsize) {
		/* mapping covers tail of the file */
		/* we do have a mapped byte which is past eof */
		G.eof_byte = G.baseaddr + (G.size - G.offset);
	}
	return 1;
}
static int move_mapping_further(void)
{
	unsigned pos;
	unsigned pagesize;

	if ((G.size - G.offset) < G_mapsize)
		return 0; /* can't move mapping even further, it's at the end already */

	pagesize = G_pagesize; /* constant on most arches */
	pos = G.current_byte - G.baseaddr;
	if (pos >= pagesize) {
		/* move offset up until current position is in 1st page */
		do {
			G.offset += pagesize;
			if (G.offset == 0) { /* whoops */
				G.offset -= pagesize;
				break;
			}
			pos -= pagesize;
		} while (pos >= pagesize);
		return remap(pos);
	}
	return 0;
}
static int move_mapping_lower(void)
{
	unsigned pos;
	unsigned pagesize;

	if (G.offset == 0)
		return 0; /* we are at 0 already */

	pagesize = G_pagesize; /* constant on most arches */
	pos = G.current_byte - G.baseaddr;

	/* move offset down until current position is in last page */
	pos += pagesize;
	while (pos < G_mapsize) {
		pos += pagesize;
		G.offset -= pagesize;
		if (G.offset == 0)
			break;
	}
	pos -= pagesize;

	return remap(pos);
}

//usage:#define hexedit_trivial_usage
//usage:	"FILE"
//usage:#define hexedit_full_usage "\n\n"
//usage:	"Edit FILE in hexadecimal"
int hexedit_main(int argc, char **argv) MAIN_EXTERNALLY_VISIBLE;
int hexedit_main(int argc UNUSED_PARAM, char **argv)
{
	INIT_G();
	INIT_PAGESIZE();

	get_terminal_width_height(-1, NULL, &G.height);
	if (1) {
		/* reduce number of write() syscalls while PgUp/Down: fully buffered output */
		unsigned sz = (G.height | 0xf) * LINEBUF_SIZE;
		setvbuf(stdout, xmalloc(sz), _IOFBF, sz);
	}

	getopt32(argv, "^" "" "\0" "=1"/*one arg*/);
	argv += optind;

	G.fd = xopen(*argv, O_RDWR);
	G.size = xlseek(G.fd, 0, SEEK_END);

	/* TERMIOS_RAW_CRNL suppresses \n -> \r\n translation, helps with down-arrow */
	printf(SET_ALT_SCR);
	set_termios_to_raw(STDIN_FILENO, &G.orig_termios, TERMIOS_RAW_CRNL);
	bb_signals(BB_FATAL_SIGS, sig_catcher);

	remap(0);
	redraw(0);

//TODO: //Home/End: start/end of line; '<'/'>': start/end of file
	//Backspace: undo
	//Ctrl-L: redraw
	//Ctrl-Z: suspend
	//'/', Ctrl-S: search
//TODO: detect window resize

	for (;;) {
		unsigned cnt;
		int32_t key = key; /* for compiler */
		uint8_t byte;

		fflush_all();
		G.in_read_key = 1;
		if (!bb_got_signal)
			key = read_key(STDIN_FILENO, G.read_key_buffer, -1);
		G.in_read_key = 0;
		if (bb_got_signal)
			key = CTRL('X');

		cnt = 1;
		if ((unsigned)(key - 'A') <= 'Z' - 'A')
			key |= 0x20; /* convert A-Z to a-z */
		switch (key) {
		case 'a': case 'b': case 'c': case 'd': case 'e': case 'f':
			/* convert to '0'+10...15 */
			key = key - ('a' - '0' - 10);
			/* fall through */
		case '0': case '1': case '2': case '3': case '4':
		case '5': case '6': case '7': case '8': case '9':
			if (G.current_byte == G.eof_byte) {
				if (!move_mapping_further()) {
					/* already at EOF; extend the file */
					if (++G.size <= 0 /* overflow? */
					 || ftruncate(G.fd, G.size) != 0 /* error extending? (e.g. block dev) */
					) {
						G.size--;
						break;
					}
					G.eof_byte++;
				}
			}
			key -= '0';
			byte = *G.current_byte & 0xf0;
			if (!G.half) {
				byte = *G.current_byte & 0x0f;
				key <<= 4;
			}
			*G.current_byte = byte + key;
			/* can't just print one updated hex char: need to update right-hand ASCII too */
			redraw_cur_line();
			/* fall through */
		case KEYCODE_RIGHT:
			if (G.current_byte == G.eof_byte)
				break; /* eof - don't allow going past it */
			byte = *G.current_byte;
			if (!G.half) {
				G.half = 1;
				putchar(bb_hexdigits_upcase[byte >> 4]);
			} else {
				G.half = 0;
				G.current_byte++;
				if ((0xf & (uintptr_t)G.current_byte) == 0) {
					/* rightmost pos, wrap to next line */
					if (G.current_byte == G.eof_byte)
						move_mapping_further();
					printf(ESC"[46D"); /* cursor left 3*15 + 1 chars */
					goto down;
				}
				putchar(bb_hexdigits_upcase[byte & 0xf]);
				putchar(' ');
			}
			break;
		case KEYCODE_PAGEDOWN:
			cnt = G.height;
		case KEYCODE_DOWN:
 k_down:
			G.current_byte += 16;
			if (G.current_byte >= G.eof_byte) {
				move_mapping_further();
				if (G.current_byte > G.eof_byte) {
					/* _after_ eof - don't allow this */
					G.current_byte -= 16;
					break;
				}
			}
 down:
			putchar('\n'); /* down one line, possibly scroll screen */
			G.row++;
			if (G.row >= G.height) {
				G.row--;
				redraw_cur_line();
			}
			if (--cnt)
				goto k_down;
			break;

		case KEYCODE_LEFT:
			if (G.half) {
				G.half = 0;
				printf(ESC"[D");
				break;
			}
			if ((0xf & (uintptr_t)G.current_byte) == 0) {
				/* leftmost pos, wrap to prev line */
				if (G.current_byte == G.baseaddr) {
					if (!move_mapping_lower())
						break; /* first line, don't do anything */
				}
				G.half = 1;
				G.current_byte--;
				printf(ESC"[46C"); /* cursor right 3*15 + 1 chars */
				goto up;
			}
			G.half = 1;
			G.current_byte--;
			printf(ESC"[2D");
			break;
		case KEYCODE_PAGEUP:
			cnt = G.height;
		case KEYCODE_UP:
 k_up:
			if ((G.current_byte - G.baseaddr) < 16) {
				if (!move_mapping_lower())
					break; /* already at 0, stop */
			}
			G.current_byte -= 16;
 up:
			if (G.row != 0) {
				G.row--;
				printf(ESC"[A"); /* up (won't scroll) */
			} else {
				//printf(ESC"[T"); /* scroll up */ - not implemented on Linux VT!
				printf(ESC"M"); /* scroll up */
				redraw_cur_line();
			}
			if (--cnt)
				goto k_up;
			break;

		case '\n':
		case '\r':
			/* [Enter]: goto specified position */
			{
				char buf[sizeof(G.offset)*3 + 4];
				printf(ESC"[999;1H" CLEAR_TILL_EOL); /* go to last line */
				if (read_line_input(NULL, "Go to (dec,0Xhex,0oct): ", buf, sizeof(buf)) > 0) {
					off_t t;
					unsigned cursor;

					t = bb_strtoull(buf, NULL, 0);
					if (t >= G.size)
						t = G.size - 1;
					cursor = t & (G_pagesize - 1);
					t -= cursor;
					if (t < 0)
						cursor = t = 0;
					if (t != 0 && cursor < 0x1ff) {
						/* very close to end of page, possibly to EOF */
						/* move one page lower */
						t -= G_pagesize;
						cursor += G_pagesize;
					}
					G.offset = t;
					remap(cursor);
					redraw(cursor);
					break;
				}
				/* ^C/EOF/error: fall through to exiting */
			}
		case CTRL('X'):
			restore_term();
			return EXIT_SUCCESS;
		} /* switch */
	} /* for (;;) */

	/* not reached */
	return EXIT_SUCCESS;
}
