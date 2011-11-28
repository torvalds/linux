/*
 * drivers/media/video/samsung/mfc5/s5p_mfc_debug.h
 *
 * Header file for Samsung MFC (Multi Function Codec - FIMV) driver
 * This file contains debug macros
 *
 * Kamil Debski, Copyright (c) 2011 Samsung Electronics
 * http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef S5P_MFC_DEBUG_H_
#define S5P_MFC_DEBUG_H_

#define DEBUG

#ifdef DEBUG
extern int debug;

#define mfc_debug(level, fmt, args...)				\
	do {							\
		if (debug >= level)				\
			printk(KERN_DEBUG "%s:%d: " fmt,	\
				__func__, __LINE__, ##args);	\
	} while (0)
#else
#define mfc_debug(level, fmt, args...)
#endif

#define mfc_debug_enter() mfc_debug(5, "enter")
#define mfc_debug_leave() mfc_debug(5, "leave")

#define mfc_err(fmt, args...)				\
	do {						\
		printk(KERN_ERR "%s:%d: " fmt,		\
		       __func__, __LINE__, ##args);	\
	} while (0)

#define mfc_info(fmt, args...)				\
	do {						\
		printk(KERN_INFO "%s:%d: " fmt,		\
		       __func__, __LINE__, ##args);	\
	} while (0)

#endif /* S5P_MFC_DEBUG_H_ */
