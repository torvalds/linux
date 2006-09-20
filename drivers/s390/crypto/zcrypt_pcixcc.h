/*
 *  linux/drivers/s390/crypto/zcrypt_pcixcc.h
 *
 *  zcrypt 2.1.0
 *
 *  Copyright (C)  2001, 2006 IBM Corporation
 *  Author(s): Robert Burroughs
 *	       Eric Rossman (edrossma@us.ibm.com)
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 *  Major cleanup & driver split: Martin Schwidefsky <schwidefsky@de.ibm.com>
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

#ifndef _ZCRYPT_PCIXCC_H_
#define _ZCRYPT_PCIXCC_H_

/**
 * CPRBX
 *	  Note that all shorts and ints are big-endian.
 *	  All pointer fields are 16 bytes long, and mean nothing.
 *
 *	  A request CPRB is followed by a request_parameter_block.
 *
 *	  The request (or reply) parameter block is organized thus:
 *	    function code
 *	    VUD block
 *	    key block
 */
struct CPRBX {
	unsigned short cprb_len;	/* CPRB length	      220	 */
	unsigned char  cprb_ver_id;	/* CPRB version id.   0x02	 */
	unsigned char  pad_000[3];	/* Alignment pad bytes		 */
	unsigned char  func_id[2];	/* function id	      0x5432	 */
	unsigned char  cprb_flags[4];	/* Flags			 */
	unsigned int   req_parml;	/* request parameter buffer len	 */
	unsigned int   req_datal;	/* request data buffer		 */
	unsigned int   rpl_msgbl;	/* reply  message block length	 */
	unsigned int   rpld_parml;	/* replied parameter block len	 */
	unsigned int   rpl_datal;	/* reply data block len		 */
	unsigned int   rpld_datal;	/* replied data block len	 */
	unsigned int   req_extbl;	/* request extension block len	 */
	unsigned char  pad_001[4];	/* reserved			 */
	unsigned int   rpld_extbl;	/* replied extension block len	 */
	unsigned char  req_parmb[16];	/* request parm block 'address'	 */
	unsigned char  req_datab[16];	/* request data block 'address'	 */
	unsigned char  rpl_parmb[16];	/* reply parm block 'address'	 */
	unsigned char  rpl_datab[16];	/* reply data block 'address'	 */
	unsigned char  req_extb[16];	/* request extension block 'addr'*/
	unsigned char  rpl_extb[16];	/* reply extension block 'addres'*/
	unsigned short ccp_rtcode;	/* server return code		 */
	unsigned short ccp_rscode;	/* server reason code		 */
	unsigned int   mac_data_len;	/* Mac Data Length		 */
	unsigned char  logon_id[8];	/* Logon Identifier		 */
	unsigned char  mac_value[8];	/* Mac Value			 */
	unsigned char  mac_content_flgs;/* Mac content flag byte	 */
	unsigned char  pad_002;		/* Alignment			 */
	unsigned short domain;		/* Domain			 */
	unsigned char  pad_003[12];	/* Domain masks			 */
	unsigned char  pad_004[36];	/* reserved			 */
} __attribute__((packed));

int zcrypt_pcixcc_init(void);
void zcrypt_pcixcc_exit(void);

#endif /* _ZCRYPT_PCIXCC_H_ */
