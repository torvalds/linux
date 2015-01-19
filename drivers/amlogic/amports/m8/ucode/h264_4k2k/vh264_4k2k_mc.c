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

#define MicroCode vh264_4k2k_mc
#include "h264c_linux.h"

#undef MicroCode
#define MicroCode vh264_4k2k_mc_single
#include "h264c_linux_single.h"

#undef MicroCode
#define MicroCode vh264_4k2k_header_mc
#include "h264header_linux.h"

#undef MicroCode
#define MicroCode vh264_4k2k_header_mc_single
#include "h264header_linux_single.h"

#undef MicroCode
#define MicroCode vh264_4k2k_mmco_mc
#include "h264mmc_linux.h"

#undef MicroCode
#define MicroCode vh264_4k2k_mmco_mc_single
#include "h264mmc_linux_single.h"

#undef MicroCode
#define MicroCode vh264_4k2k_slice_mc
#include "h264slice_linux.h"

#undef MicroCode
#define MicroCode vh264_4k2k_slice_mc_single
#include "h264slice_linux_single.h"

