/*
 * midas-sound.h - Sound Management of MIDAS Project
 *
 *  Copyright (C) 2012 Samsung Electrnoics
 *  JS Park <aitdark.park@samsung.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#ifndef __MIDAS_SOUND_H__
#define __MIDAS_SOUND_H__ __FILE__

void midas_sound_init(void);
void midas_snd_set_mclk(bool on, bool forced);
bool midas_snd_get_mclk(void);

extern struct platform_device vbatt_device;
extern struct platform_device s3c_device_fm34;

#endif /* __MIDAS_SOUND_H__ */
