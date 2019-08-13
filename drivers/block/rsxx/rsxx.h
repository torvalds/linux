/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
* Filename: rsxx.h
*
* Authors: Joshua Morris <josh.h.morris@us.ibm.com>
*	Philip Kelleher <pjk1939@linux.vnet.ibm.com>
*
* (C) Copyright 2013 IBM Corporation
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
