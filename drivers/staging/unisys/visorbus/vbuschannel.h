/* Copyright (C) 2010 - 2015 UNISYS CORPORATION
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 */

#ifndef __VBUSCHANNEL_H__
#define __VBUSCHANNEL_H__

/*  The vbus channel is the channel area provided via the BUS_CREATE controlvm
 *  message for each virtual bus.  This channel area is provided to both server
 *  and client ends of the bus.  The channel header area is initialized by
 *  the server, and the remaining information is filled in by the client.
 *  We currently use this for the client to provide various information about
 *  the client devices and client drivers for the server end to see.
 */
#include <linux/uuid.h>
#include "channel.h"

/* {193b331b-c58f-11da-95a9-00e08161165f} */
#define SPAR_VBUS_CHANNEL_PROTOCOL_UUID \
		UUID_LE(0x193b331b, 0xc58f, 0x11da, \
				0x95, 0xa9, 0x0, 0xe0, 0x81, 0x61, 0x16, 0x5f)
static const uuid_le spar_vbus_channel_protocol_uuid =
	SPAR_VBUS_CHANNEL_PROTOCOL_UUID;

#define SPAR_VBUS_CHANNEL_PROTOCOL_SIGNATURE ULTRA_CHANNEL_PROTOCOL_SIGNATURE

/* Must increment this whenever you insert or delete fields within this channel
 * struct.  Also increment whenever you change the meaning of fields within this
 * channel struct so as to break pre-existing software.  Note that you can
 * usually add fields to the END of the channel struct withOUT needing to
 * increment this.
 */
#define SPAR_VBUS_CHANNEL_PROTOCOL_VERSIONID 1

#define SPAR_VBUS_CHANNEL_OK_CLIENT(ch)       \
	spar_check_channel_client(ch,				\
				   spar_vbus_channel_protocol_uuid,	\
				   "vbus",				\
				   sizeof(struct spar_vbus_channel_protocol),\
				   SPAR_VBUS_CHANNEL_PROTOCOL_VERSIONID, \
				   SPAR_VBUS_CHANNEL_PROTOCOL_SIGNATURE)

#define SPAR_VBUS_CHANNEL_OK_SERVER(actual_bytes)    \
	(spar_check_channel_server(spar_vbus_channel_protocol_uuid,	\
				   "vbus",				\
				   sizeof(struct spar_vbus_channel_protocol),\
				   actual_bytes))

#pragma pack(push, 1)		/* both GCC and VC now allow this pragma */

/*
 * An array of this struct is present in the channel area for each vbus.
 * (See vbuschannel.h.)
 * It is filled in by the client side to provide info about the device
 * and driver from the client's perspective.
 */
struct ultra_vbus_deviceinfo {
	u8 devtype[16];		/* short string identifying the device type */
	u8 drvname[16];		/* driver .sys file name */
	u8 infostrs[96];	/* kernel version */
	u8 reserved[128];	/* pad size to 256 bytes */
};

/**
 * vbuschannel_sanitize_buffer() - remove non-printable chars from buffer
 * @p: destination buffer where chars are written to
 * @remain: number of bytes that can be written starting at #p
 * @src: pointer to source buffer
 * @srcmax: number of valid characters at #src
 *
 * Reads chars from the buffer at @src for @srcmax bytes, and writes to
 * the buffer at @p, which is @remain bytes long, ensuring never to
 * overflow the buffer at @p, using the following rules:
 * - printable characters are simply copied from the buffer at @src to the
 *   buffer at @p
 * - intervening streaks of non-printable characters in the buffer at @src
 *   are replaced with a single space in the buffer at @p
 * Note that we pay no attention to '\0'-termination.
 *
 * Pass @p == NULL and @remain == 0 for this special behavior -- In this
 * case, we simply return the number of bytes that WOULD HAVE been written
 * to a buffer at @p, had it been infinitely big.
 *
 * Return: the number of bytes written to @p (or WOULD HAVE been written to
 *         @p, as described in the previous paragraph)
 */
static inline int
vbuschannel_sanitize_buffer(char *p, int remain, char *src, int srcmax)
{
	int chars = 0;
	int nonprintable_streak = 0;

	while (srcmax > 0) {
		if ((*src >= ' ') && (*src < 0x7f)) {
			if (nonprintable_streak) {
				if (remain > 0) {
					*p = ' ';
					p++;
					remain--;
					chars++;
				} else if (!p) {
					chars++;
				}
				nonprintable_streak = 0;
			}
			if (remain > 0) {
				*p = *src;
				p++;
				remain--;
				chars++;
			} else if (!p) {
				chars++;
			}
		} else {
			nonprintable_streak = 1;
		}
		src++;
		srcmax--;
	}
	return chars;
}

#define VBUSCHANNEL_ADDACHAR(ch, p, remain, chars) \
	do {					   \
		if (remain <= 0)		   \
			break;			   \
		*p = ch;			   \
		p++;  chars++;  remain--;	   \
	} while (0)

/**
 * vbuschannel_itoa() - convert non-negative int to string
 * @p: destination string
 * @remain: max number of bytes that can be written to @p
 * @num: input int to convert
 *
 * Converts the non-negative value at @num to an ascii decimal string
 * at @p, writing at most @remain bytes.  Note there is NO '\0' termination
 * written to @p.
 *
 * Return: number of bytes written to @p
 *
 */
static inline int
vbuschannel_itoa(char *p, int remain, int num)
{
	int digits = 0;
	char s[32];
	int i;

	if (num == 0) {
		/* '0' is a special case */
		if (remain <= 0)
			return 0;
		*p = '0';
		return 1;
	}
	/* form a backwards decimal ascii string in <s> */
	while (num > 0) {
		if (digits >= (int)sizeof(s))
			return 0;
		s[digits++] = (num % 10) + '0';
		num = num / 10;
	}
	if (remain < digits) {
		/* not enough room left at <p> to hold number, so fill with
		 * '?'
		 */
		for (i = 0; i < remain; i++, p++)
			*p = '?';
		return remain;
	}
	/* plug in the decimal ascii string representing the number, by */
	/* reversing the string we just built in <s> */
	i = digits;
	while (i > 0) {
		i--;
		*p = s[i];
		p++;
	}
	return digits;
}

/**
 * vbuschannel_devinfo_to_string() - format a struct ultra_vbus_deviceinfo
 *                                   to a printable string
 * @devinfo: the struct ultra_vbus_deviceinfo to format
 * @p: destination string area
 * @remain: size of destination string area in bytes
 * @devix: the device index to be included in the output data, or -1 if no
 *         device index is to be included
 *
 * Reads @devInfo, and converts its contents to a printable string at @p,
 * writing at most @remain bytes. Note there is NO '\0' termination
 * written to @p.
 *
 * Return: number of bytes written to @p
 */
static inline int
vbuschannel_devinfo_to_string(struct ultra_vbus_deviceinfo *devinfo,
			      char *p, int remain, int devix)
{
	char *psrc;
	int nsrc, x, i, pad;
	int chars = 0;

	psrc = &devinfo->devtype[0];
	nsrc = sizeof(devinfo->devtype);
	if (vbuschannel_sanitize_buffer(NULL, 0, psrc, nsrc) <= 0)
		return 0;

	/* emit device index */
	if (devix >= 0) {
		VBUSCHANNEL_ADDACHAR('[', p, remain, chars);
		x = vbuschannel_itoa(p, remain, devix);
		p += x;
		remain -= x;
		chars += x;
		VBUSCHANNEL_ADDACHAR(']', p, remain, chars);
	} else {
		VBUSCHANNEL_ADDACHAR(' ', p, remain, chars);
		VBUSCHANNEL_ADDACHAR(' ', p, remain, chars);
		VBUSCHANNEL_ADDACHAR(' ', p, remain, chars);
	}

	/* emit device type */
	x = vbuschannel_sanitize_buffer(p, remain, psrc, nsrc);
	p += x;
	remain -= x;
	chars += x;
	pad = 15 - x;		/* pad device type to be exactly 15 chars */
	for (i = 0; i < pad; i++)
		VBUSCHANNEL_ADDACHAR(' ', p, remain, chars);
	VBUSCHANNEL_ADDACHAR(' ', p, remain, chars);

	/* emit driver name */
	psrc = &devinfo->drvname[0];
	nsrc = sizeof(devinfo->drvname);
	x = vbuschannel_sanitize_buffer(p, remain, psrc, nsrc);
	p += x;
	remain -= x;
	chars += x;
	pad = 15 - x;		/* pad driver name to be exactly 15 chars */
	for (i = 0; i < pad; i++)
		VBUSCHANNEL_ADDACHAR(' ', p, remain, chars);
	VBUSCHANNEL_ADDACHAR(' ', p, remain, chars);

	/* emit strings */
	psrc = &devinfo->infostrs[0];
	nsrc = sizeof(devinfo->infostrs);
	x = vbuschannel_sanitize_buffer(p, remain, psrc, nsrc);
	p += x;
	remain -= x;
	chars += x;
	VBUSCHANNEL_ADDACHAR('\n', p, remain, chars);

	return chars;
}

struct spar_vbus_headerinfo {
	u32 struct_bytes;	/* size of this struct in bytes */
	u32 device_info_struct_bytes;	/* sizeof(ULTRA_VBUS_DEVICEINFO) */
	u32 dev_info_count;	/* num of items in DevInfo member */
	/* (this is the allocated size) */
	u32 chp_info_offset;	/* byte offset from beginning of this struct */
	/* to the ChpInfo struct (below) */
	u32 bus_info_offset;	/* byte offset from beginning of this struct */
	/* to the BusInfo struct (below) */
	u32 dev_info_offset;	/* byte offset from beginning of this struct */
	/* to the DevInfo array (below) */
	u8 reserved[104];
};

struct spar_vbus_channel_protocol {
	struct channel_header channel_header;	/* initialized by server */
	struct spar_vbus_headerinfo hdr_info;	/* initialized by server */
	/* the remainder of this channel is filled in by the client */
	struct ultra_vbus_deviceinfo chp_info;
	/* describes client chipset device and driver */
	struct ultra_vbus_deviceinfo bus_info;
	/* describes client bus device and driver */
	struct ultra_vbus_deviceinfo dev_info[0];
	/* describes client device and driver for each device on the bus */
};

#define VBUS_CH_SIZE_EXACT(MAXDEVICES) \
	(sizeof(ULTRA_VBUS_CHANNEL_PROTOCOL) + ((MAXDEVICES) * \
						sizeof(ULTRA_VBUS_DEVICEINFO)))
#define VBUS_CH_SIZE(MAXDEVICES) COVER(VBUS_CH_SIZE_EXACT(MAXDEVICES), 4096)

#pragma pack(pop)

#endif
