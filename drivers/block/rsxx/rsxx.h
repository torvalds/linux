/*
* Filename: rsxx.h
*
*
* Authors: Joshua Morris <josh.h.morris@us.ibm.com>
*	Philip Kelleher <pjk1939@linux.vnet.ibm.com>
*
* (C) Copyright 2013 IBM Corporation
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public License as
* published by the Free Software Foundation; either version 2 of the
* License, or (at your option) any later version.
*
* This program is distributed in the hope that it will be useful, but
* WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
* General Public License for more details.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software Foundation,
* Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
*/

#ifndef __RSXX_H__
#define __RSXX_H__

/*----------------- IOCTL Definitions -------------------*/

#define RSXX_MAX_DATA 8

struct rsxx_reg_access {
	__u32 addr;
	__u32 cnt;
	__u32 stat;
	__u32 stream;
	__u32 data[RSXX_MAX_DATA];
};

#define RSXX_MAX_REG_CNT	(RSXX_MAX_DATA * (sizeof(__u32)))

#define RSXX_IOC_MAGIC 'r'

#define RSXX_GETREG _IOWR(RSXX_IOC_MAGIC, 0x20, struct rsxx_reg_access)
#define RSXX_SETREG _IOWR(RSXX_IOC_MAGIC, 0x21, struct rsxx_reg_access)

#endif /* __RSXX_H_ */
