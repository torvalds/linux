/*
 * lcd44780.h
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

void lcd44780_puts(const char* s);
void lcd44780_init(void);
