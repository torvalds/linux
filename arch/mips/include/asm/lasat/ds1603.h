/* SPDX-License-Identifier: GPL-2.0 */
#include <asm/addrspace.h>

/* Lasat 100	*/
#define DS1603_REG_100		(KSEG1ADDR(0x1c810000))
#define DS1603_RST_100		(1 << 2)
#define DS1603_CLK_100		(1 << 0)
#define DS1603_DATA_SHIFT_100	1
#define DS1603_DATA_100		(1 << DS1603_DATA_SHIFT_100)

/* Lasat 200	*/
#define DS1603_REG_200		(KSEG1ADDR(0x11000000))
#define DS1603_RST_200		(1 << 3)
#define DS1603_CLK_200		(1 << 4)
#define DS1603_DATA_200		(1 << 5)

#define DS1603_DATA_REG_200		(DS1603_REG_200 + 0x10000)
#define DS1603_DATA_READ_SHIFT_200	9
#define DS1603_DATA_READ_200	(1 << DS1603_DATA_READ_SHIFT_200)
