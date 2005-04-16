/*
** File:		selftest.h
**
** Author:		David Dix
**
** Created:		15th March 1993
**
** Last modified:	94/06/14
**
 *
 *  (C) 1990 - 2000 Specialix International Ltd., Byfleet, Surrey, UK.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#ifndef	_selftests_h_
#define _selftests_h_

/*
** Selftest identifier...
*/
#define SELFTEST_MAGIC	0x5a5a

/*
** This is the structure of the packet that is sent back after each
** selftest on a booting RTA.
*/
typedef struct {
    short		magic;			/* Identifies packet type */
    int			test;			/* Test number, see below */
    unsigned int	result;			/* Result value */
    unsigned int	dataIn;
    unsigned int	dataOut;
}selftestStruct;

/*
** The different tests are identified by the following data values.
*/
enum test {
    TESTS_COMPLETE	= 0x00,
    MEMTEST_ADDR	= 0x01,
    MEMTEST_BIT		= 0x02,
    MEMTEST_FILL	= 0x03,
    MEMTEST_DATABUS	= 0x04,
    MEMTEST_ADDRBUS	= 0x05,
    CD1400_INIT		= 0x10,
    CD1400_LOOP		= 0x11,
    CD1400_INTERRUPT    = 0x12
};

enum result {
    E_PORT		= 0x10,
    E_TX		= 0x11,
    E_RX		= 0x12,
    E_EXCEPT		= 0x13,
    E_COMPARE		= 0x14,
    E_MODEM		= 0x15,
    E_TIMEOUT		= 0x16,
    E_INTERRUPT         = 0x17
};
#endif	/* _selftests_h_ */
