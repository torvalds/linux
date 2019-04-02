/*
 * drivers/media/platform/s5p-mfc/s5p_mfc_de.h
 *
 * Header file for Samsung MFC (Multi Function Codec - FIMV) driver
 * This file contains de macros
 *
 * Kamil Debski, Copyright (c) 2011 Samsung Electronics
 * http://www.samsung.com/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef S5P_MFC_DE_H_
#define S5P_MFC_DE_H_

#define DE

#ifdef DE
extern int mfc_de_level;

#define mfc_de(level, fmt, args...)				\
	do {							\
		if (mfc_de_level >= level)			\
			printk(KERN_DE "%s:%d: " fmt,	\
				__func__, __LINE__, ##args);	\
	} while (0)
#else
#define mfc_de(level, fmt, args...)
#endif

#define mfc_de_enter() mfc_de(5, "enter\n")
#define mfc_de_leave() mfc_de(5, "leave\n")

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

#endif /* S5P_MFC_DE_H_ */
