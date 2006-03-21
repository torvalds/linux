/*
 *  EBCDIC to ASCII conversion
 *
 * This function moved here from arch/powerpc/platforms/iseries/viopath.c 
 *
 * (C) Copyright 2000-2004 IBM Corporation
 *
 * This program is free software;  you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) anyu later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 *
 */

#include <linux/module.h>

unsigned char e2a(unsigned char x)
{
	switch (x) {
	case 0xF0:
		return '0';
	case 0xF1:
		return '1';
	case 0xF2:
		return '2';
	case 0xF3:
		return '3';
	case 0xF4:
		return '4';
	case 0xF5:
		return '5';
	case 0xF6:
		return '6';
	case 0xF7:
		return '7';
	case 0xF8:
		return '8';
	case 0xF9:
		return '9';
	case 0xC1:
		return 'A';
	case 0xC2:
		return 'B';
	case 0xC3:
		return 'C';
	case 0xC4:
		return 'D';
	case 0xC5:
		return 'E';
	case 0xC6:
		return 'F';
	case 0xC7:
		return 'G';
	case 0xC8:
		return 'H';
	case 0xC9:
		return 'I';
	case 0xD1:
		return 'J';
	case 0xD2:
		return 'K';
	case 0xD3:
		return 'L';
	case 0xD4:
		return 'M';
	case 0xD5:
		return 'N';
	case 0xD6:
		return 'O';
	case 0xD7:
		return 'P';
	case 0xD8:
		return 'Q';
	case 0xD9:
		return 'R';
	case 0xE2:
		return 'S';
	case 0xE3:
		return 'T';
	case 0xE4:
		return 'U';
	case 0xE5:
		return 'V';
	case 0xE6:
		return 'W';
	case 0xE7:
		return 'X';
	case 0xE8:
		return 'Y';
	case 0xE9:
		return 'Z';
	}
	return ' ';
}
EXPORT_SYMBOL(e2a);

unsigned char* strne2a(unsigned char *dest, const unsigned char *src, size_t n)
{
	int i;

	n = strnlen(src, n);

	for (i = 0; i < n; i++)
		dest[i] = e2a(src[i]);

	return dest;
}
