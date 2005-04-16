/*
 *  linux/drivers/s390/crypto/z90common.h
 *
 *  z90crypt 1.3.2
 *
 *  Copyright (C)  2001, 2004 IBM Corporation
 *  Author(s): Robert Burroughs (burrough@us.ibm.com)
 *             Eric Rossman (edrossma@us.ibm.com)
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#ifndef _Z90COMMON_H_
#define _Z90COMMON_H_

#define VERSION_Z90COMMON_H "$Revision: 1.16 $"


#define RESPBUFFSIZE 256
#define PCI_FUNC_KEY_DECRYPT 0x5044
#define PCI_FUNC_KEY_ENCRYPT 0x504B
extern int ext_bitlens;

enum devstat {
	DEV_GONE,
	DEV_ONLINE,
	DEV_QUEUE_FULL,
	DEV_EMPTY,
	DEV_NO_WORK,
	DEV_BAD_MESSAGE,
	DEV_TSQ_EXCEPTION,
	DEV_RSQ_EXCEPTION,
	DEV_SEN_EXCEPTION,
	DEV_REC_EXCEPTION
};

enum hdstat {
	HD_NOT_THERE,
	HD_BUSY,
	HD_DECONFIGURED,
	HD_CHECKSTOPPED,
	HD_ONLINE,
	HD_TSQ_EXCEPTION
};

#define Z90C_NO_DEVICES		1
#define Z90C_AMBIGUOUS_DOMAIN	2
#define Z90C_INCORRECT_DOMAIN	3
#define ENOTINIT		4

#define SEN_BUSY	 7
#define SEN_USER_ERROR	 8
#define SEN_QUEUE_FULL	11
#define SEN_NOT_AVAIL	16
#define SEN_PAD_ERROR	17
#define SEN_RETRY	18
#define SEN_RELEASED	24

#define REC_EMPTY	 4
#define REC_BUSY	 6
#define REC_OPERAND_INV	 8
#define REC_OPERAND_SIZE 9
#define REC_EVEN_MOD	10
#define REC_NO_WORK	11
#define REC_HARDWAR_ERR	12
#define REC_NO_RESPONSE	13
#define REC_RETRY_DEV	14
#define REC_USER_GONE	15
#define REC_BAD_MESSAGE	16
#define REC_INVALID_PAD	17
#define REC_USE_PCICA	18

#define WRONG_DEVICE_TYPE 20

#define REC_FATAL_ERROR 32
#define SEN_FATAL_ERROR 33
#define TSQ_FATAL_ERROR 34
#define RSQ_FATAL_ERROR 35

#define Z90CRYPT_NUM_TYPES	5
#define PCICA		0
#define PCICC		1
#define PCIXCC_MCL2	2
#define PCIXCC_MCL3	3
#define CEX2C		4
#define NILDEV		-1
#define ANYDEV		-1
#define PCIXCC_UNK	-2

enum hdevice_type {
	PCICC_HW  = 3,
	PCICA_HW  = 4,
	PCIXCC_HW = 5,
	OTHER_HW  = 6,
	CEX2C_HW  = 7
};

struct CPRBX {
	unsigned short cprb_len;
	unsigned char  cprb_ver_id;
	unsigned char  pad_000[3];
	unsigned char  func_id[2];
	unsigned char  cprb_flags[4];
	unsigned int   req_parml;
	unsigned int   req_datal;
	unsigned int   rpl_msgbl;
	unsigned int   rpld_parml;
	unsigned int   rpl_datal;
	unsigned int   rpld_datal;
	unsigned int   req_extbl;
	unsigned char  pad_001[4];
	unsigned int   rpld_extbl;
	unsigned char  req_parmb[16];
	unsigned char  req_datab[16];
	unsigned char  rpl_parmb[16];
	unsigned char  rpl_datab[16];
	unsigned char  req_extb[16];
	unsigned char  rpl_extb[16];
	unsigned short ccp_rtcode;
	unsigned short ccp_rscode;
	unsigned int   mac_data_len;
	unsigned char  logon_id[8];
	unsigned char  mac_value[8];
	unsigned char  mac_content_flgs;
	unsigned char  pad_002;
	unsigned short domain;
	unsigned char  pad_003[12];
	unsigned char  pad_004[36];
};

#ifndef DEV_NAME
#define DEV_NAME	"z90crypt"
#endif
#define PRINTK(fmt, args...) \
	printk(KERN_DEBUG DEV_NAME ": %s -> " fmt, __FUNCTION__ , ## args)
#define PRINTKN(fmt, args...) \
	printk(KERN_DEBUG DEV_NAME ": " fmt, ## args)
#define PRINTKW(fmt, args...) \
	printk(KERN_WARNING DEV_NAME ": %s -> " fmt, __FUNCTION__ , ## args)
#define PRINTKC(fmt, args...) \
	printk(KERN_CRIT DEV_NAME ": %s -> " fmt, __FUNCTION__ , ## args)

#ifdef Z90CRYPT_DEBUG
#define PDEBUG(fmt, args...) \
	printk(KERN_DEBUG DEV_NAME ": %s -> " fmt, __FUNCTION__ , ## args)
#else
#define PDEBUG(fmt, args...) do {} while (0)
#endif

#define UMIN(a,b) ((a) < (b) ? (a) : (b))
#define IS_EVEN(x) ((x) == (2 * ((x) / 2)))


#endif
