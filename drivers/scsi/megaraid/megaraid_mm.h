/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *
 *			Linux MegaRAID device driver
 *
 * Copyright (c) 2003-2004  LSI Logic Corporation.
 *
 * FILE		: megaraid_mm.h
 */

#ifndef MEGARAID_MM_H
#define MEGARAID_MM_H

#include <linux/spinlock.h>
#include <linux/fs.h>
#include <linux/uaccess.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/pci.h>
#include <linux/list.h>
#include <linux/miscdevice.h>

#include "mbox_defs.h"
#include "megaraid_ioctl.h"


#define LSI_COMMON_MOD_VERSION	"2.20.2.7"
#define LSI_COMMON_MOD_EXT_VERSION	\
		"(Release Date: Sun Jul 16 00:01:03 EST 2006)"


#define LSI_DBGLVL			dbglevel

// The smallest dma pool
#define MRAID_MM_INIT_BUFF_SIZE		4096

/**
 * mimd_t	: Old style ioctl packet structure (deprecated)
 *
 * @inlen	:
 * @outlen	:
 * @fca		:
 * @opcode	:
 * @subopcode	:
 * @adapno	:
 * @buffer	:
 * @pad		:
 * @length	:
 * @mbox	:
 * @pthru	:
 * @data	:
 * @pad		:
 *
 * Note		: This structure is DEPRECATED. New applications must use
 *		: uioc_t structure instead. All new hba drivers use the new
 *		: format. If we get this mimd packet, we will convert it into
 *		: new uioc_t format and send it to the hba drivers.
 */

typedef struct mimd {

	uint32_t inlen;
	uint32_t outlen;

	union {
		uint8_t fca[16];
		struct {
			uint8_t opcode;
			uint8_t subopcode;
			uint16_t adapno;
#if BITS_PER_LONG == 32
			uint8_t __user *buffer;
			uint8_t pad[4];
#endif
#if BITS_PER_LONG == 64
			uint8_t __user *buffer;
#endif
			uint32_t length;
		} __attribute__ ((packed)) fcs;
	} __attribute__ ((packed)) ui;

	uint8_t mbox[18];		/* 16 bytes + 2 status bytes */
	mraid_passthru_t pthru;

#if BITS_PER_LONG == 32
	char __user *data;		/* buffer <= 4096 for 0x80 commands */
	char pad[4];
#endif
#if BITS_PER_LONG == 64
	char __user *data;
#endif

} __attribute__ ((packed))mimd_t;

#endif // MEGARAID_MM_H

// vi: set ts=8 sw=8 tw=78:
