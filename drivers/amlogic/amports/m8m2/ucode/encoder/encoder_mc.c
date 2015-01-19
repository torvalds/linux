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
 * Author:  Brian Zhu <brian.zhu@amlogic.com>
 *
 */

#include <linux/types.h>

#define MicroCode mix_dump_mc
#include "h264_enc_mix_dump.h"

#undef MicroCode
#define MicroCode half_encoder_mc
#include "half_encoder_linux.h"

#undef MicroCode
#define MicroCode mix_sw_mc
#include "h264_enc_mix_sw.h"


#undef MicroCode
#define MicroCode mix_sw_mc_hdec_m2_dblk
#include "h264_enc_mix_sw_hdec_m2_dblk.h"

#undef MicroCode
#define MicroCode mix_dump_mc_dblk
#include "h264_enc_mix_dump_dblk.h"

