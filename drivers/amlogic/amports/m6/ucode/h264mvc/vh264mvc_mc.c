/*
 * AMLOGIC Audio/Video streaming port driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the named License,
 * or any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307, USA
 *
 * Author:  Tim Yao <timyao@amlogic.com>
 *
 */

#include <linux/types.h>

#define MicroCode vh264mvc_mc
#include "h264c_mvc_linux.h"

#undef MicroCode
#define MicroCode vh264mvc_header_mc
#include "h264header_mvc_linux.h"

#undef MicroCode
#define MicroCode vh264mvc_mmco_mc
#include "h264mmc_mvc_linux.h"

#undef MicroCode
#define MicroCode vh264mvc_slice_mc
#include "h264slice_mvc_linux.h"
