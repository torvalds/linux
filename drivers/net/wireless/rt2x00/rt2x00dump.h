/*
	Copyright (C) 2004 - 2009 Ivo van Doorn <IvDoorn@gmail.com>
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2x00dump
	Abstract:
		Data structures for the rt2x00debug & userspace.

		The declarations in this file can be used by both rt2x00
		and userspace and therefore should be kept together in
		this file.
 */

#ifndef RT2X00DUMP_H
#define RT2X00DUMP_H

/**
 * DOC: Introduction
 *
 * This header is intended to be exported to userspace,
 * to make the structures and enumerations available to userspace
 * applications. This means that all data types should be exportable.
 *
 * When rt2x00 is compiled with debugfs support enabled,
 * it is possible to capture all data coming in and out of the device
 * by reading the frame dump file. This file can have only a single reader.
 * The following frames will be reported:
 *   - All incoming frames (rx)
 *   - All outgoing frames (tx, including beacon and atim)
 *   - All completed frames (txdone including atim)
 *
 * The data is send to the file using the following format:
 *
 *   [rt2x00dump header][hardware descriptor][ieee802.11 frame]
 *
 * rt2x00dump header: The description of the dumped frame, as well as
 *	additional information useful for debugging. See &rt2x00dump_hdr.
 * hardware descriptor: Descriptor that was used to receive or transmit
 *	the frame.
 * ieee802.11 frame: The actual frame that was received or transmitted.
 */

/**
 * enum rt2x00_dump_type - Frame type
 *
 * These values are used for the @type member of &rt2x00dump_hdr.
 * @DUMP_FRAME_RXDONE: This frame has been received by the hardware.
 * @DUMP_FRAME_TX: This frame is queued for transmission to the hardware.
 * @DUMP_FRAME_TXDONE: This frame indicates the device has handled
 *	the tx event which has either succeeded or failed. A frame
 *	with this type should also have been reported with as a
 *	%DUMP_FRAME_TX frame.
 * @DUMP_FRAME_BEACON: This beacon frame is queued for transmission to the
 *	hardware.
 */
enum rt2x00_dump_type {
	DUMP_FRAME_RXDONE = 1,
	DUMP_FRAME_TX = 2,
	DUMP_FRAME_TXDONE = 3,
	DUMP_FRAME_BEACON = 4,
};

/**
 * struct rt2x00dump_hdr - Dump frame header
 *
 * Each frame dumped to the debugfs file starts with this header
 * attached. This header contains the description of the actual
 * frame which was dumped.
 *
 * New fields inside the structure must be appended to the end of
 * the structure. This way userspace tools compiled for earlier
 * header versions can still correctly handle the frame dump
 * (although they will not handle all data passed to them in the dump).
 *
 * @version: Header version should always be set to %DUMP_HEADER_VERSION.
 *	This field must be checked by userspace to determine if it can
 *	handle this frame.
 * @header_length: The length of the &rt2x00dump_hdr structure. This is
 *	used for compatibility reasons so userspace can easily determine
 *	the location of the next field in the dump.
 * @desc_length: The length of the device descriptor.
 * @data_length: The length of the frame data (including the ieee802.11 header.
 * @chip_rt: RT chipset
 * @chip_rf: RF chipset
 * @chip_rev: Chipset revision
 * @type: The frame type (&rt2x00_dump_type)
 * @queue_index: The index number of the data queue.
 * @entry_index: The index number of the entry inside the data queue.
 * @timestamp_sec: Timestamp - seconds
 * @timestamp_usec: Timestamp - microseconds
 */
struct rt2x00dump_hdr {
	__le32 version;
#define DUMP_HEADER_VERSION	2

	__le32 header_length;
	__le32 desc_length;
	__le32 data_length;

	__le16 chip_rt;
	__le16 chip_rf;
	__le16 chip_rev;

	__le16 type;
	__u8 queue_index;
	__u8 entry_index;

	__le32 timestamp_sec;
	__le32 timestamp_usec;
};

#endif /* RT2X00DUMP_H */
