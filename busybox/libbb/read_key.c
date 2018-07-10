/* vi: set sw=4 ts=4: */
/*
 * Utility routines.
 *
 * Copyright (C) 2008 Rob Landley <rob@landley.net>
 * Copyright (C) 2008 Denys Vlasenko <vda.linux@googlemail.com>
 *
 * Licensed under GPLv2, see file LICENSE in this source tree.
 */
#include "libbb.h"

int64_t FAST_FUNC read_key(int fd, char *buffer, int timeout)
{
	struct pollfd pfd;
	const char *seq;
	int n;

	/* Known escape sequences for cursor and function keys.
	 * See "Xterm Control Sequences"
	 * http://invisible-island.net/xterm/ctlseqs/ctlseqs.html
	 * Array should be sorted from shortest to longest.
	 */
	static const char esccmds[] ALIGN1 = {
		'\x7f'         |0x80,KEYCODE_ALT_BACKSPACE,
		'\b'           |0x80,KEYCODE_ALT_BACKSPACE,
		'd'            |0x80,KEYCODE_ALT_D   ,
	/* lineedit mimics bash: Alt-f and Alt-b are forward/backward
	 * word jumps. We cheat here and make them return ALT_LEFT/RIGHT
	 * keycodes. This way, lineedit need no special code to handle them.
	 * If we'll need to distinguish them, introduce new ALT_F/B keycodes,
	 * and update lineedit to react to them.
	 */
		'f'            |0x80,KEYCODE_ALT_RIGHT,
		'b'            |0x80,KEYCODE_ALT_LEFT,
		'O','A'        |0x80,KEYCODE_UP      ,
		'O','B'        |0x80,KEYCODE_DOWN    ,
		'O','C'        |0x80,KEYCODE_RIGHT   ,
		'O','D'        |0x80,KEYCODE_LEFT    ,
		'O','H'        |0x80,KEYCODE_HOME    ,
		'O','F'        |0x80,KEYCODE_END     ,
#if 0
		'O','P'        |0x80,KEYCODE_FUN1    ,
		/* [ESC] ESC O [2] P - [Alt-][Shift-]F1 */
		/* ESC [ O 1 ; 2 P - Shift-F1 */
		/* ESC [ O 1 ; 3 P - Alt-F1 */
		/* ESC [ O 1 ; 4 P - Alt-Shift-F1 */
		/* ESC [ O 1 ; 5 P - Ctrl-F1 */
		/* ESC [ O 1 ; 6 P - Ctrl-Shift-F1 */
		'O','Q'        |0x80,KEYCODE_FUN2    ,
		'O','R'        |0x80,KEYCODE_FUN3    ,
		'O','S'        |0x80,KEYCODE_FUN4    ,
#endif
		'[','A'        |0x80,KEYCODE_UP      ,
		'[','B'        |0x80,KEYCODE_DOWN    ,
		'[','C'        |0x80,KEYCODE_RIGHT   ,
		'[','D'        |0x80,KEYCODE_LEFT    ,
		/* ESC [ 1 ; 2 x, where x = A/B/C/D: Shift-<arrow> */
		/* ESC [ 1 ; 3 x, where x = A/B/C/D: Alt-<arrow> - implemented below */
		/* ESC [ 1 ; 4 x, where x = A/B/C/D: Alt-Shift-<arrow> */
		/* ESC [ 1 ; 5 x, where x = A/B/C/D: Ctrl-<arrow> - implemented below */
		/* ESC [ 1 ; 6 x, where x = A/B/C/D: Ctrl-Shift-<arrow> */
		/* ESC [ 1 ; 7 x, where x = A/B/C/D: Ctrl-Alt-<arrow> */
		/* ESC [ 1 ; 8 x, where x = A/B/C/D: Ctrl-Alt-Shift-<arrow> */
		'[','H'        |0x80,KEYCODE_HOME    , /* xterm */
		'[','F'        |0x80,KEYCODE_END     , /* xterm */
		/* [ESC] ESC [ [2] H - [Alt-][Shift-]Home (End similarly?) */
		/* '[','Z'        |0x80,KEYCODE_SHIFT_TAB, */
		'[','1','~'    |0x80,KEYCODE_HOME    , /* vt100? linux vt? or what? */
		'[','2','~'    |0x80,KEYCODE_INSERT  ,
		/* ESC [ 2 ; 3 ~ - Alt-Insert */
		'[','3','~'    |0x80,KEYCODE_DELETE  ,
		/* [ESC] ESC [ 3 [;2] ~ - [Alt-][Shift-]Delete */
		/* ESC [ 3 ; 3 ~ - Alt-Delete */
		/* ESC [ 3 ; 5 ~ - Ctrl-Delete */
		'[','4','~'    |0x80,KEYCODE_END     , /* vt100? linux vt? or what? */
		'[','5','~'    |0x80,KEYCODE_PAGEUP  ,
		/* ESC [ 5 ; 3 ~ - Alt-PgUp */
		/* ESC [ 5 ; 5 ~ - Ctrl-PgUp */
		/* ESC [ 5 ; 7 ~ - Ctrl-Alt-PgUp */
		'[','6','~'    |0x80,KEYCODE_PAGEDOWN,
		'[','7','~'    |0x80,KEYCODE_HOME    , /* vt100? linux vt? or what? */
		'[','8','~'    |0x80,KEYCODE_END     , /* vt100? linux vt? or what? */
#if 0
		'[','1','1','~'|0x80,KEYCODE_FUN1    , /* old xterm, deprecated by ESC O P */
		'[','1','2','~'|0x80,KEYCODE_FUN2    , /* old xterm... */
		'[','1','3','~'|0x80,KEYCODE_FUN3    , /* old xterm... */
		'[','1','4','~'|0x80,KEYCODE_FUN4    , /* old xterm... */
		'[','1','5','~'|0x80,KEYCODE_FUN5    ,
		/* [ESC] ESC [ 1 5 [;2] ~ - [Alt-][Shift-]F5 */
		'[','1','7','~'|0x80,KEYCODE_FUN6    ,
		'[','1','8','~'|0x80,KEYCODE_FUN7    ,
		'[','1','9','~'|0x80,KEYCODE_FUN8    ,
		'[','2','0','~'|0x80,KEYCODE_FUN9    ,
		'[','2','1','~'|0x80,KEYCODE_FUN10   ,
		'[','2','3','~'|0x80,KEYCODE_FUN11   ,
		'[','2','4','~'|0x80,KEYCODE_FUN12   ,
		/* ESC [ 2 4 ; 2 ~ - Shift-F12 */
		/* ESC [ 2 4 ; 3 ~ - Alt-F12 */
		/* ESC [ 2 4 ; 4 ~ - Alt-Shift-F12 */
		/* ESC [ 2 4 ; 5 ~ - Ctrl-F12 */
		/* ESC [ 2 4 ; 6 ~ - Ctrl-Shift-F12 */
#endif
		/* '[','1',';','5','A' |0x80,KEYCODE_CTRL_UP   , - unused */
		/* '[','1',';','5','B' |0x80,KEYCODE_CTRL_DOWN , - unused */
		'[','1',';','5','C' |0x80,KEYCODE_CTRL_RIGHT,
		'[','1',';','5','D' |0x80,KEYCODE_CTRL_LEFT ,
		/* '[','1',';','3','A' |0x80,KEYCODE_ALT_UP    , - unused */
		/* '[','1',';','3','B' |0x80,KEYCODE_ALT_DOWN  , - unused */
		'[','1',';','3','C' |0x80,KEYCODE_ALT_RIGHT,
		'[','1',';','3','D' |0x80,KEYCODE_ALT_LEFT ,
		/* '[','3',';','3','~' |0x80,KEYCODE_ALT_DELETE, - unused */
		0
	};

	pfd.fd = fd;
	pfd.events = POLLIN;

	buffer++; /* saved chars counter is in buffer[-1] now */

 start_over:
	errno = 0;
	n = (unsigned char)buffer[-1];
	if (n == 0) {
		/* If no data, wait for input.
		 * If requested, wait TIMEOUT ms. TIMEOUT = -1 is useful
		 * if fd can be in non-blocking mode.
		 */
		if (timeout >= -1) {
			if (safe_poll(&pfd, 1, timeout) == 0) {
				/* Timed out */
				errno = EAGAIN;
				return -1;
			}
		}
		/* It is tempting to read more than one byte here,
		 * but it breaks pasting. Example: at shell prompt,
		 * user presses "c","a","t" and then pastes "\nline\n".
		 * When we were reading 3 bytes here, we were eating
		 * "li" too, and cat was getting wrong input.
		 */
		n = safe_read(fd, buffer, 1);
		if (n <= 0)
			return -1;
	}

	{
		unsigned char c = buffer[0];
		n--;
		if (n)
			memmove(buffer, buffer + 1, n);
		/* Only ESC starts ESC sequences */
		if (c != 27) {
			buffer[-1] = n;
			return c;
		}
	}

	/* Loop through known ESC sequences */
	seq = esccmds;
	while (*seq != '\0') {
		/* n - position in sequence we did not read yet */
		int i = 0; /* position in sequence to compare */

		/* Loop through chars in this sequence */
		while (1) {
			/* So far escape sequence matched up to [i-1] */
			if (n <= i) {
				/* Need more chars, read another one if it wouldn't block.
				 * Note that escape sequences come in as a unit,
				 * so if we block for long it's not really an escape sequence.
				 * Timeout is needed to reconnect escape sequences
				 * split up by transmission over a serial console. */
				if (safe_poll(&pfd, 1, 50) == 0) {
					/* No more data!
					 * Array is sorted from shortest to longest,
					 * we can't match anything later in array -
					 * anything later is longer than this seq.
					 * Break out of both loops. */
					goto got_all;
				}
				errno = 0;
				if (safe_read(fd, buffer + n, 1) <= 0) {
					/* If EAGAIN, then fd is O_NONBLOCK and poll lied:
					 * in fact, there is no data. */
					if (errno != EAGAIN) {
						/* otherwise: it's EOF/error */
						buffer[-1] = 0;
						return -1;
					}
					goto got_all;
				}
				n++;
			}
			if (buffer[i] != (seq[i] & 0x7f)) {
				/* This seq doesn't match, go to next */
				seq += i;
				/* Forward to last char */
				while (!(*seq & 0x80))
					seq++;
				/* Skip it and the keycode which follows */
				seq += 2;
				break;
			}
			if (seq[i] & 0x80) {
				/* Entire seq matched */
				n = 0;
				/* n -= i; memmove(...);
				 * would be more correct,
				 * but we never read ahead that much,
				 * and n == i here. */
				buffer[-1] = 0;
				return (signed char)seq[i+1];
			}
			i++;
		}
	}
	/* We did not find matching sequence.
	 * We possibly read and stored more input in buffer[] by now.
	 * n = bytes read. Try to read more until we time out.
	 */
	while (n < KEYCODE_BUFFER_SIZE-1) { /* 1 for count byte at buffer[-1] */
		if (safe_poll(&pfd, 1, 50) == 0) {
			/* No more data! */
			break;
		}
		errno = 0;
		if (safe_read(fd, buffer + n, 1) <= 0) {
			/* If EAGAIN, then fd is O_NONBLOCK and poll lied:
			 * in fact, there is no data. */
			if (errno != EAGAIN) {
				/* otherwise: it's EOF/error */
				buffer[-1] = 0;
				return -1;
			}
			break;
		}
		n++;
		/* Try to decipher "ESC [ NNN ; NNN R" sequence */
		if ((ENABLE_FEATURE_EDITING_ASK_TERMINAL
		    || ENABLE_FEATURE_VI_ASK_TERMINAL
		    || ENABLE_FEATURE_LESS_ASK_TERMINAL
		    )
		 && n >= 5
		 && buffer[0] == '['
		 && buffer[n-1] == 'R'
		 && isdigit(buffer[1])
		) {
			char *end;
			unsigned long row, col;

			row = strtoul(buffer + 1, &end, 10);
			if (*end != ';' || !isdigit(end[1]))
				continue;
			col = strtoul(end + 1, &end, 10);
			if (*end != 'R')
				continue;
			if (row < 1 || col < 1 || (row | col) > 0x7fff)
				continue;

			buffer[-1] = 0;
			/* Pack into "1 <row15bits> <col16bits>" 32-bit sequence */
			col |= (((-1 << 15) | row) << 16);
			/* Return it in high-order word */
			return ((int64_t) col << 32) | (uint32_t)KEYCODE_CURSOR_POS;
		}
	}
 got_all:

	if (n <= 1) {
		/* Alt-x is usually returned as ESC x.
		 * Report ESC, x is remembered for the next call.
		 */
		buffer[-1] = n;
		return 27;
	}

	/* We were doing "buffer[-1] = n; return c;" here, but this results
	 * in unknown key sequences being interpreted as ESC + garbage.
	 * This was not useful. Pretend there was no key pressed,
	 * go and wait for a new keypress:
	 */
	buffer[-1] = 0;
	goto start_over;
}

void FAST_FUNC read_key_ungets(char *buffer, const char *str, unsigned len)
{
	unsigned cur_len = (unsigned char)buffer[0];
	if (len > KEYCODE_BUFFER_SIZE-1 - cur_len)
		len = KEYCODE_BUFFER_SIZE-1 - cur_len;
	memcpy(buffer + 1 + cur_len, str, len);
	buffer[0] += len;
}
