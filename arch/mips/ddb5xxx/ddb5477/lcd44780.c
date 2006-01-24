/*
 * lcd44780.c
 * Simple "driver" for a memory-mapped 44780-style LCD display.
 *
 * Copyright 2001 Bradley D. LaRonde <brad@ltc.com>
 *
 * This program is free software; you can redistribute  it and/or modify it
 * under  the terms of  the GNU General  Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 *
 */

#define LCD44780_COMMAND   ((volatile unsigned char *)0xbe020000)
#define LCD44780_DATA      ((volatile unsigned char *)0xbe020001)

#define LCD44780_4BIT_1LINE        0x20
#define LCD44780_4BIT_2LINE        0x28
#define LCD44780_8BIT_1LINE        0x30
#define LCD44780_8BIT_2LINE        0x38
#define LCD44780_MODE_DEC          0x04
#define LCD44780_MODE_DEC_SHIFT    0x05
#define LCD44780_MODE_INC          0x06
#define LCD44780_MODE_INC_SHIFT    0x07
#define LCD44780_SCROLL_LEFT       0x18
#define LCD44780_SCROLL_RIGHT      0x1e
#define LCD44780_CURSOR_UNDERLINE  0x0e
#define LCD44780_CURSOR_BLOCK      0x0f
#define LCD44780_CURSOR_OFF        0x0c
#define LCD44780_CLEAR             0x01
#define LCD44780_BLANK             0x08
#define LCD44780_RESTORE           0x0c  // Same as CURSOR_OFF
#define LCD44780_HOME              0x02
#define LCD44780_LEFT              0x10
#define LCD44780_RIGHT             0x14

void lcd44780_wait(void)
{
	int i, j;
	for(i=0; i < 400; i++)
		for(j=0; j < 10000; j++);
}

void lcd44780_command(unsigned char c)
{
	*LCD44780_COMMAND = c;
	lcd44780_wait();
}

void lcd44780_data(unsigned char c)
{
	*LCD44780_DATA = c;
	lcd44780_wait();
}

void lcd44780_puts(const char* s)
{
	int j;
	int pos = 0;

	lcd44780_command(LCD44780_CLEAR);
	while(*s) {
		lcd44780_data(*s);
		s++;
		pos++;
		if (pos == 8) {
		  /* We must write 32 of spaces to get cursor to 2nd line */
		  for (j=0; j<32; j++) {
		    lcd44780_data(' ');
		  }
		}
		if (pos == 16) {
		  /* We have filled all 16 character positions, so stop
		     outputing data */
		  break;
		}
	}
#ifdef LCD44780_PUTS_PAUSE
	{
		int i;

		for(i = 1; i < 2000; i++)
			lcd44780_wait();
	}
#endif
}

void lcd44780_init(void)
{
	// The display on the RockHopper is physically a single
	// 16 char line (two 8 char lines concatenated).  bdl
	lcd44780_command(LCD44780_8BIT_2LINE);
	lcd44780_command(LCD44780_MODE_INC);
	lcd44780_command(LCD44780_CURSOR_BLOCK);
	lcd44780_command(LCD44780_CLEAR);
}
