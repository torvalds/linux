/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ4740 SoC clock support
 *
 *  This program is free software; you can redistribute	 it and/or modify it
 *  under  the terms of	 the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the	License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __MIPS_JZ4740_CLOCK_H__
#define __MIPS_JZ4740_CLOCK_H__

#include <linux/clk.h>
#include <linux/list.h>

void jz4740_clock_suspend(void);
void jz4740_clock_resume(void);

#endif
