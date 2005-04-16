/*
** -----------------------------------------------------------------------------
**
**  Perle Specialix driver for Linux
**  Ported from existing RIO Driver for SCO sources.
 *
 *  (C) 1990 - 2000 Specialix International Ltd., Byfleet, Surrey, UK.
 *
 *      This program is free software; you can redistribute it and/or modify
 *      it under the terms of the GNU General Public License as published by
 *      the Free Software Foundation; either version 2 of the License, or
 *      (at your option) any later version.
 *
 *      This program is distributed in the hope that it will be useful,
 *      but WITHOUT ANY WARRANTY; without even the implied warranty of
 *      MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *      GNU General Public License for more details.
 *
 *      You should have received a copy of the GNU General Public License
 *      along with this program; if not, write to the Free Software
 *      Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
**
**	Module		: map.h
**	SID		: 1.2
**	Last Modified	: 11/6/98 11:34:11
**	Retrieved	: 11/6/98 11:34:21
**
**  ident @(#)map.h	1.2
**
** -----------------------------------------------------------------------------
*/

#ifndef __rio_map_h__
#define __rio_map_h__

#ifdef SCCS_LABELS
static char *_map_h_sccs_ = "@(#)map.h	1.2";
#endif

/*
** mapping structure passed to and from the config.rio program to
** determine the current topology of the world
*/

#define MAX_MAP_ENTRY 17
#define	TOTAL_MAP_ENTRIES (MAX_MAP_ENTRY*RIO_SLOTS)
#define	MAX_NAME_LEN 32

struct Map
{
	uint	HostUniqueNum;	        /* Supporting hosts unique number */
	uint	RtaUniqueNum;	        /* Unique number */
	/*
	** The next two IDs must be swapped on big-endian architectures
	** when using a v2.04 /etc/rio/config with a v3.00 driver (when
	** upgrading for example).
	*/
	ushort	ID;			/* ID used in the subnet */
	ushort	ID2;			/* ID of 2nd block of 8 for 16 port */
	ulong	Flags;			/* Booted, ID Given, Disconnected */
	ulong	SysPort;		/* First tty mapped to this port */
	struct Top   Topology[LINKS_PER_UNIT];	/* ID connected to each link */
	char	Name[MAX_NAME_LEN];        /* Cute name by which RTA is known */
};

/*
** Flag values:
*/
#define	RTA_BOOTED		0x00000001
#define RTA_NEWBOOT		0x00000010
#define	MSG_DONE		0x00000020
#define	RTA_INTERCONNECT	0x00000040
#define	RTA16_SECOND_SLOT	0x00000080
#define	BEEN_HERE		0x00000100
#define SLOT_TENTATIVE		0x40000000
#define SLOT_IN_USE		0x80000000

/*
** HostUniqueNum is the unique number from the host card that this RTA
** is to be connected to.
** RtaUniqueNum is the unique number of the RTA concerned. It will be ZERO
** if the slot in the table is unused. If it is the same as the HostUniqueNum
** then this slot represents a host card.
** Flags contains current boot/route state info
** SysPort is a value in the range 0-504, being the number of the first tty
** on this RTA. Each RTA supports 8 ports. The SysPort value must be modulo 8.
** SysPort 0-127 correspond to /dev/ttyr001 to /dev/ttyr128, with minor
** numbers 0-127. SysPort 128-255 correspond to /dev/ttyr129 to /dev/ttyr256,
** again with minor numbers 0-127, and so on for SysPorts 256-383 and 384-511
** ID will be in the range 0-16 for a `known' RTA. ID will be 0xFFFF for an
** unused slot/unknown ID etc.
** The Topology array contains the ID of the unit connected to each of the
** four links on this unit. The entry will be 0xFFFF if NOTHING is connected
** to the link, or will be 0xFF00 if an UNKNOWN unit is connected to the link.
** The Name field is a null-terminated string, upto 31 characters, containing
** the 'cute' name that the sysadmin/users know the RTA by. It is permissible
** for this string to contain any character in the range \040 to \176 inclusive.
** In particular, ctrl sequences and DEL (0x7F, \177) are not allowed. The
** special character '%' IS allowable, and needs no special action.
**
*/

#endif
