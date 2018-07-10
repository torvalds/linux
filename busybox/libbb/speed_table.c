/* vi: set sw=4 ts=4: */
/*
 * compact speed_t <-> speed functions for busybox
 *
 * Copyright (C) 2003  Manuel Novoa III  <mjn3@codepoet.org>
 *
 * Licensed under GPLv2 or later, see file LICENSE in this source tree.
 */
#include "libbb.h"

struct speed_map {
#if defined __FreeBSD__ \
 || (defined B115200  && B115200  > 0xffff) \
 || (defined B230400  && B230400  > 0xffff) \
 || (defined B460800  && B460800  > 0xffff) \
 || (defined B921600  && B921600  > 0xffff) \
 || (defined B1152000 && B1152000 > 0xffff) \
 || (defined B1000000 && B1000000 > 0xffff) \
 || (defined B2000000 && B2000000 > 0xffff) \
 || (defined B3000000 && B3000000 > 0xffff) \
 || (defined B4000000 && B4000000 > 0xffff)
	/* On FreeBSD, B<num> constants don't fit into a short */
	unsigned speed;
#else
	unsigned short speed;
#endif
	unsigned short value;
};

/* On Linux, Bxx constants are 0..15 (up to B38400) and 0x1001..0x100f */
static const struct speed_map speeds[] = {
	{B0, 0},
	{B50, 50},
	{B75, 75},
	{B110, 110},
	{B134, 134},
	{B150, 150},
	{B200, 200},
	{B300, 300},
	{B600, 600},
	{B1200, 1200},
	{B1800, 1800},
	{B2400, 2400},
	{B4800, 4800},
	{B9600, 9600},
#ifdef B19200
	{B19200, 19200},
#elif defined(EXTA)
	{EXTA,   19200},
#endif
/* 19200 = 0x4b00 */
/* 38400 = 0x9600, this value would use bit#15 if not "/200" encoded: */
#ifdef B38400
	{B38400, 38400/200 + 0x8000u},
#elif defined(EXTB)
	{EXTB,   38400/200 + 0x8000u},
#endif
#ifdef B57600
	{B57600, 57600/200 + 0x8000u},
#endif
#ifdef B115200
	{B115200, 115200/200 + 0x8000u},
#endif
#ifdef B230400
	{B230400, 230400/200 + 0x8000u},
#endif
#ifdef B460800
	{B460800, 460800/200 + 0x8000u},
#endif
#ifdef B576000
	{B576000, 576000/200 + 0x8000u},
#endif
#ifdef B921600
	{B921600, 921600/200 + 0x8000u},
#endif
#ifdef B1152000
	{B1152000, 1152000/200 + 0x8000u},
#endif

#ifdef B500000
	{B500000,   500000/200 + 0x8000u},
#endif
#ifdef B1000000
	{B1000000, 1000000/200 + 0x8000u},
#endif
#ifdef B1500000
	{B1500000, 1500000/200 + 0x8000u},
#endif
#ifdef B2000000
	{B2000000, 2000000/200 + 0x8000u},
#endif
#ifdef B2500000
	{B2500000, 2500000/200 + 0x8000u},
#endif
#ifdef B3000000
	{B3000000, 3000000/200 + 0x8000u},
#endif
#ifdef B3500000
	{B3500000, 3500000/200 + 0x8000u},
#endif
#ifdef B4000000
	{B4000000, 4000000/200 + 0x8000u},
#endif
/* 4000000/200 = 0x4e20, bit#15 still does not interfere with the value */
/* (can use /800 if higher speeds would appear, /1600 won't work for B500000) */
};

/*
 * TODO: maybe we can just bite the bullet, ditch the table and use termios2
 * Linux API (supports arbitrary baud rates, no Bxxxx mess needed)? Example:
 *
 * #include <asm/termios.h>
 * #include <asm/ioctls.h>
 * struct termios2 t;
 * ioctl(fd, TCGETS2, &t);
 * t.c_ospeed = t.c_ispeed = 543210;
 * t.c_cflag &= ~CBAUD;
 * t.c_cflag |= BOTHER;
 * ioctl(fd, TCSETS2, &t);
 */

enum { NUM_SPEEDS = ARRAY_SIZE(speeds) };

unsigned FAST_FUNC tty_baud_to_value(speed_t speed)
{
	int i = 0;

	do {
		if (speed == speeds[i].speed) {
			if (speeds[i].value & 0x8000u) {
				return ((unsigned)(speeds[i].value) & 0x7fffU) * 200;
			}
			return speeds[i].value;
		}
	} while (++i < NUM_SPEEDS);

	return 0;
}

speed_t FAST_FUNC tty_value_to_baud(unsigned int value)
{
	int i = 0;

	do {
		if (value == tty_baud_to_value(speeds[i].speed)) {
			return speeds[i].speed;
		}
	} while (++i < NUM_SPEEDS);

	return (speed_t) - 1;
}

#if 0
/* testing code */
#include <stdio.h>

int main(void)
{
	unsigned long v;
	speed_t s;

	for (v = 0 ; v < 1000000; v++) {
		s = tty_value_to_baud(v);
		if (s == (speed_t) -1) {
			continue;
		}
		printf("v = %lu -- s = %0lo\n", v, (unsigned long) s);
	}

	printf("-------------------------------\n");

	for (s = 0 ; s < 010017+1; s++) {
		v = tty_baud_to_value(s);
		if (!v) {
			continue;
		}
		printf("v = %lu -- s = %0lo\n", v, (unsigned long) s);
	}

	return 0;
}
#endif
