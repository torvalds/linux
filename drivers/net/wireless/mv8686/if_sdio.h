/*
 *  linux/drivers/net/wireless/libertas/if_sdio.h
 *
 *  Copyright 2007 Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 */

#ifndef _LBS_IF_SDIO_H
#define _LBS_IF_SDIO_H

#define IF_SDIO_IOPORT				0x00

#define IF_SDIO_CONFIGURE			0x03
#define   IF_SDIO_HOST_UP				0x02
#define   IF_SDIO_HOST_DOWN			0x01
#define   IF_SDIO_HOST_RESET		0x00

#define IF_SDIO_H_INT_MASK		0x04
#define   IF_SDIO_H_INT_OFLOW		0x08
#define   IF_SDIO_H_INT_UFLOW		0x04
#define   IF_SDIO_H_INT_DNLD		0x02
#define   IF_SDIO_H_INT_UPLD		0x01

#define IF_SDIO_H_INT_STATUS	0x05
#define IF_SDIO_H_INT_RSR	0x06
#define IF_SDIO_H_INT_STATUS2	0x07

#define IF_SDIO_RD_BASE		0x10

#define IF_SDIO_STATUS		0x20
#define   IF_SDIO_IO_RDY	0x08
#define   IF_SDIO_CIS_RDY	0x04
#define   IF_SDIO_UL_RDY	0x02
#define   IF_SDIO_DL_RDY	0x01

#define IF_SDIO_C_INT_MASK	0x24
#define IF_SDIO_C_INT_STATUS	0x28
#define IF_SDIO_C_INT_RSR	0x2C

#define IF_SDIO_SCRATCH		0x34
#define IF_SDIO_SCRATCH_OLD	0x80fe
#define   IF_SDIO_FIRMWARE_OK	0xfedc

#define IF_SDIO_EVENT           0x80fc

/** Number of firmware blocks to transfer */
#define FW_TRANSFER_NBLOCK      2
#define SD_BLOCK_SIZE           256

/** The number of times to try when polling for status bits */
#define MAX_POLL_TRIES          100

/** SDIO header length */
#define SDIO_HEADER_LEN         4

/** Macros for Data Alignment : address */
#define ALIGN_ADDR(p, a)	\
	((((u32)(p)) + (((u32)(a)) - 1)) & ~(((u32)(a)) - 1))
	
#endif
