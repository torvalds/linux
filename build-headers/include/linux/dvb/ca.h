/* SPDX-License-Identifier: LGPL-2.1+ WITH Linux-syscall-note */
/*
 * ca.h
 *
 * Copyright (C) 2000 Ralph  Metzler <ralph@convergence.de>
 *                  & Marcus Metzler <marcus@convergence.de>
 *                    for convergence integrated media GmbH
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Lesser Public License
 * as published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 *
 */

#ifndef _DVBCA_H_
#define _DVBCA_H_

/**
 * struct ca_slot_info - CA slot interface types and info.
 *
 * @num:	slot number.
 * @type:	slot type.
 * @flags:	flags applicable to the slot.
 *
 * This struct stores the CA slot information.
 *
 * @type can be:
 *
 *	- %CA_CI - CI high level interface;
 *	- %CA_CI_LINK - CI link layer level interface;
 *	- %CA_CI_PHYS - CI physical layer level interface;
 *	- %CA_DESCR - built-in descrambler;
 *	- %CA_SC -simple smart card interface.
 *
 * @flags can be:
 *
 *	- %CA_CI_MODULE_PRESENT - module (or card) inserted;
 *	- %CA_CI_MODULE_READY - module is ready for usage.
 */

struct ca_slot_info {
	int num;
	int type;
#define CA_CI            1
#define CA_CI_LINK       2
#define CA_CI_PHYS       4
#define CA_DESCR         8
#define CA_SC          128

	unsigned int flags;
#define CA_CI_MODULE_PRESENT 1
#define CA_CI_MODULE_READY   2
};


/**
 * struct ca_descr_info - descrambler types and info.
 *
 * @num:	number of available descramblers (keys).
 * @type:	type of supported scrambling system.
 *
 * Identifies the number of descramblers and their type.
 *
 * @type can be:
 *
 *	- %CA_ECD - European Common Descrambler (ECD) hardware;
 *	- %CA_NDS - Videoguard (NDS) hardware;
 *	- %CA_DSS - Distributed Sample Scrambling (DSS) hardware.
 */
struct ca_descr_info {
	unsigned int num;
	unsigned int type;
#define CA_ECD           1
#define CA_NDS           2
#define CA_DSS           4
};

/**
 * struct ca_caps - CA slot interface capabilities.
 *
 * @slot_num:	total number of CA card and module slots.
 * @slot_type:	bitmap with all supported types as defined at
 *		&struct ca_slot_info (e. g. %CA_CI, %CA_CI_LINK, etc).
 * @descr_num:	total number of descrambler slots (keys)
 * @descr_type:	bitmap with all supported types as defined at
 *		&struct ca_descr_info (e. g. %CA_ECD, %CA_NDS, etc).
 */
struct ca_caps {
	unsigned int slot_num;
	unsigned int slot_type;
	unsigned int descr_num;
	unsigned int descr_type;
};

/**
 * struct ca_msg - a message to/from a CI-CAM
 *
 * @index:	unused
 * @type:	unused
 * @length:	length of the message
 * @msg:	message
 *
 * This struct carries a message to be send/received from a CI CA module.
 */
struct ca_msg {
	unsigned int index;
	unsigned int type;
	unsigned int length;
	unsigned char msg[256];
};

/**
 * struct ca_descr - CA descrambler control words info
 *
 * @index: CA Descrambler slot
 * @parity: control words parity, where 0 means even and 1 means odd
 * @cw: CA Descrambler control words
 */
struct ca_descr {
	unsigned int index;
	unsigned int parity;
	unsigned char cw[8];
};

#define CA_RESET          _IO('o', 128)
#define CA_GET_CAP        _IOR('o', 129, struct ca_caps)
#define CA_GET_SLOT_INFO  _IOR('o', 130, struct ca_slot_info)
#define CA_GET_DESCR_INFO _IOR('o', 131, struct ca_descr_info)
#define CA_GET_MSG        _IOR('o', 132, struct ca_msg)
#define CA_SEND_MSG       _IOW('o', 133, struct ca_msg)
#define CA_SET_DESCR      _IOW('o', 134, struct ca_descr)


/* This is needed for legacy userspace support */
typedef struct ca_slot_info ca_slot_info_t;
typedef struct ca_descr_info  ca_descr_info_t;
typedef struct ca_caps  ca_caps_t;
typedef struct ca_msg ca_msg_t;
typedef struct ca_descr ca_descr_t;



#endif
