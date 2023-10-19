/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * drivers/media/platform/samsung/s5p-mfc/s5p_mfc_debug.h
 *
 * Header file for Samsung MFC (Multi Function Codec - FIMV) driver
 * This file contains debug macros
 *
 * Kamil Debski, Copyright (c) 2011 Samsung Electronics
 * http://www.samsung.com/
 */

#ifndef S5P_MFC_DEBUG_H_
#define S5P_MFC_DEBUG_H_

#define DEBUG

#ifdef DEBUG
extern int mfc_debug_level;

#define mfc_debug(level, fmt, args...)				\
	do {							\
		if (mfc_debug_level >= level)			\
			printk(KERN_DEBUG "%s:%d: " fmt,	\
				__func__, __LINE__, ##args);	\
	} while (0)
#else
#define mfc_debug(level, fmt, args...)
#endif

#define mfc_debug_enter() mfc_debug(5, "enter\n")
#define mfc_debug_leave() mfc_debug(5, "leave\n")

#define mfc_err(fmt, args...)				\
	do {						\
		printk(KERN_ERR "%s:%d: " fmt,		\
		       __func__, __LINE__, ##args);	\
	} while (0)

#define mfc_err_limited(fmt, args...)			\
	do {						\
		printk_ratelimited(KERN_ERR "%s:%d: " fmt,	\
		       __func__, __LINE__, ##args);	\
	} while (0)

#define mfc_info(fmt, args...)				\
	do {						\
		printk(KERN_INFO "%s:%d: " fmt,		\
		       __func__, __LINE__, ##args);	\
	} while (0)

#endif /* S5P_MFC_DEBUG_H_ */
