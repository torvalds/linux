/*****************************************************************************
*                                                                            *
*  easycap_main.h                                                           *
*                                                                            *
*****************************************************************************/
/*
 *
 *  Copyright (C) 2010 R.M. Thomas  <rmthomas@sciolus.org>
 *
 *
 *  This is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  The software is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this software; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
*/
/*****************************************************************************/
#if !defined(EASYCAP_MAIN_H)
#define EASYCAP_MAIN_H

extern struct easycap_standard easycap_standard[];
extern struct easycap_format easycap_format[];
extern struct v4l2_queryctrl easycap_control[];
extern struct usb_driver easycap_usb_driver;
#if defined(EASYCAP_NEEDS_ALSA)
extern struct snd_pcm_ops easycap_alsa_ops;
extern struct snd_pcm_hardware easycap_pcm_hardware;
extern struct snd_card *psnd_card;
#else
extern struct usb_class_driver easyoss_class;
extern const struct file_operations easyoss_fops;
#endif /*EASYCAP_NEEDS_ALSA*/

#endif /*EASYCAP_MAIN_H*/
