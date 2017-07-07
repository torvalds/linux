/*
 *  include/asm-s390/zcrypt.h
 *
 *  zcrypt 2.1.0 (user-visible header)
 *
 *  Copyright IBM Corp. 2001, 2006
 *  Author(s): Robert Burroughs
 *	       Eric Rossman (edrossma@us.ibm.com)
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

#ifndef __ASM_S390_ZCRYPT_H
#define __ASM_S390_ZCRYPT_H

#define ZCRYPT_VERSION 2
#define ZCRYPT_RELEASE 1
#define ZCRYPT_VARIANT 1

#include <linux/ioctl.h>
#include <linux/compiler.h>

/**
 * struct ica_rsa_modexpo
 *
 * Requirements:
 * - outputdatalength is at least as large as inputdatalength.
 * - All key parts are right justified in their fields, padded on
 *   the left with zeroes.
 * - length(b_key) = inputdatalength
 * - length(n_modulus) = inputdatalength
 */
struct ica_rsa_modexpo {
	char __user *	inputdata;
	unsigned int	inputdatalength;
	char __user *	outputdata;
	unsigned int	outputdatalength;
	char __user *	b_key;
	char __user *	n_modulus;
};

/**
 * struct ica_rsa_modexpo_crt
 *
 * Requirements:
 * - inputdatalength is even.
 * - outputdatalength is at least as large as inputdatalength.
 * - All key parts are right justified in their fields, padded on
 *   the left with zeroes.
 * - length(bp_key)	= inputdatalength/2 + 8
 * - length(bq_key)	= inputdatalength/2
 * - length(np_key)	= inputdatalength/2 + 8
 * - length(nq_key)	= inputdatalength/2
 * - length(u_mult_inv) = inputdatalength/2 + 8
 */
struct ica_rsa_modexpo_crt {
	char __user *	inputdata;
	unsigned int	inputdatalength;
	char __user *	outputdata;
	unsigned int	outputdatalength;
	char __user *	bp_key;
	char __user *	bq_key;
	char __user *	np_prime;
	char __user *	nq_prime;
	char __user *	u_mult_inv;
};

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
	unsigned short	cprb_len;	/* CPRB length	      220	 */
	unsigned char	cprb_ver_id;	/* CPRB version id.   0x02	 */
	unsigned char	pad_000[3];	/* Alignment pad bytes		 */
	unsigned char	func_id[2];	/* function id	      0x5432	 */
	unsigned char	cprb_flags[4];	/* Flags			 */
	unsigned int	req_parml;	/* request parameter buffer len	 */
	unsigned int	req_datal;	/* request data buffer		 */
	unsigned int	rpl_msgbl;	/* reply  message block length	 */
	unsigned int	rpld_parml;	/* replied parameter block len	 */
	unsigned int	rpl_datal;	/* reply data block len		 */
	unsigned int	rpld_datal;	/* replied data block len	 */
	unsigned int	req_extbl;	/* request extension block len	 */
	unsigned char	pad_001[4];	/* reserved			 */
	unsigned int	rpld_extbl;	/* replied extension block len	 */
	unsigned char	padx000[16 - sizeof (char *)];
	unsigned char *	req_parmb;	/* request parm block 'address'	 */
	unsigned char	padx001[16 - sizeof (char *)];
	unsigned char *	req_datab;	/* request data block 'address'	 */
	unsigned char	padx002[16 - sizeof (char *)];
	unsigned char *	rpl_parmb;	/* reply parm block 'address'	 */
	unsigned char	padx003[16 - sizeof (char *)];
	unsigned char *	rpl_datab;	/* reply data block 'address'	 */
	unsigned char	padx004[16 - sizeof (char *)];
	unsigned char *	req_extb;	/* request extension block 'addr'*/
	unsigned char	padx005[16 - sizeof (char *)];
	unsigned char *	rpl_extb;	/* reply extension block 'address'*/
	unsigned short	ccp_rtcode;	/* server return code		 */
	unsigned short	ccp_rscode;	/* server reason code		 */
	unsigned int	mac_data_len;	/* Mac Data Length		 */
	unsigned char	logon_id[8];	/* Logon Identifier		 */
	unsigned char	mac_value[8];	/* Mac Value			 */
	unsigned char	mac_content_flgs;/* Mac content flag byte	 */
	unsigned char	pad_002;	/* Alignment			 */
	unsigned short	domain;		/* Domain			 */
	unsigned char	usage_domain[4];/* Usage domain			 */
	unsigned char	cntrl_domain[4];/* Control domain		 */
	unsigned char	S390enf_mask[4];/* S/390 enforcement mask	 */
	unsigned char	pad_004[36];	/* reserved			 */
} __attribute__((packed));

/**
 * xcRB
 */
struct ica_xcRB {
	unsigned short	agent_ID;
	unsigned int	user_defined;
	unsigned short	request_ID;
	unsigned int	request_control_blk_length;
	unsigned char	padding1[16 - sizeof (char *)];
	char __user *	request_control_blk_addr;
	unsigned int	request_data_length;
	char		padding2[16 - sizeof (char *)];
	char __user *	request_data_address;
	unsigned int	reply_control_blk_length;
	char		padding3[16 - sizeof (char *)];
	char __user *	reply_control_blk_addr;
	unsigned int	reply_data_length;
	char		padding4[16 - sizeof (char *)];
	char __user *	reply_data_addr;
	unsigned short	priority_window;
	unsigned int	status;
} __attribute__((packed));

/**
 * struct ep11_cprb - EP11 connectivity programming request block
 * @cprb_len:		CPRB header length [0x0020]
 * @cprb_ver_id:	CPRB version id.   [0x04]
 * @pad_000:		Alignment pad bytes
 * @flags:		Admin cmd [0x80] or functional cmd [0x00]
 * @func_id:		Function id / subtype [0x5434]
 * @source_id:		Source id [originator id]
 * @target_id:		Target id [usage/ctrl domain id]
 * @ret_code:		Return code
 * @reserved1:		Reserved
 * @reserved2:		Reserved
 * @payload_len:	Payload length
 */
struct ep11_cprb {
	uint16_t	cprb_len;
	unsigned char	cprb_ver_id;
	unsigned char	pad_000[2];
	unsigned char	flags;
	unsigned char	func_id[2];
	uint32_t	source_id;
	uint32_t	target_id;
	uint32_t	ret_code;
	uint32_t	reserved1;
	uint32_t	reserved2;
	uint32_t	payload_len;
} __attribute__((packed));

/**
 * struct ep11_target_dev - EP11 target device list
 * @ap_id:	AP device id
 * @dom_id:	Usage domain id
 */
struct ep11_target_dev {
	uint16_t ap_id;
	uint16_t dom_id;
};

/**
 * struct ep11_urb - EP11 user request block
 * @targets_num:	Number of target adapters
 * @targets:		Addr to target adapter list
 * @weight:		Level of request priority
 * @req_no:		Request id/number
 * @req_len:		Request length
 * @req:		Addr to request block
 * @resp_len:		Response length
 * @resp:		Addr to response block
 */
struct ep11_urb {
	uint16_t		targets_num;
	uint64_t		targets;
	uint64_t		weight;
	uint64_t		req_no;
	uint64_t		req_len;
	uint64_t		req;
	uint64_t		resp_len;
	uint64_t		resp;
} __attribute__((packed));

/**
 * struct zcrypt_device_status
 * @hwtype:		raw hardware type
 * @qid:		6 bit device index, 8 bit domain
 * @functions:		AP device function bit field 'abcdef'
 *			a, b, c = reserved
 *			d = CCA coprocessor
 *			e = Accelerator
 *			f = EP11 coprocessor
 * @online		online status
 * @reserved		reserved
 */
struct zcrypt_device_status {
	unsigned int hwtype:8;
	unsigned int qid:14;
	unsigned int online:1;
	unsigned int functions:6;
	unsigned int reserved:3;
};

#define MAX_ZDEV_CARDIDS 64
#define MAX_ZDEV_DOMAINS 256

/**
 * Maximum number of zcrypt devices
 */
#define MAX_ZDEV_ENTRIES (MAX_ZDEV_CARDIDS * MAX_ZDEV_DOMAINS)

/**
 * zcrypt_device_matrix
 * Device matrix of all zcrypt devices
 */
struct zcrypt_device_matrix {
	struct zcrypt_device_status device[MAX_ZDEV_ENTRIES];
};

#define AUTOSELECT ((unsigned int)0xFFFFFFFF)

#define ZCRYPT_IOCTL_MAGIC 'z'

/**
 * Interface notes:
 *
 * The ioctl()s which are implemented (along with relevant details)
 * are:
 *
 *   ICARSAMODEXPO
 *     Perform an RSA operation using a Modulus-Exponent pair
 *     This takes an ica_rsa_modexpo struct as its arg.
 *
 *     NOTE: please refer to the comments preceding this structure
 *	     for the implementation details for the contents of the
 *	     block
 *
 *   ICARSACRT
 *     Perform an RSA operation using a Chinese-Remainder Theorem key
 *     This takes an ica_rsa_modexpo_crt struct as its arg.
 *
 *     NOTE: please refer to the comments preceding this structure
 *	     for the implementation details for the contents of the
 *	     block
 *
 *   ZSECSENDCPRB
 *     Send an arbitrary CPRB to a crypto card.
 *
 *   ZSENDEP11CPRB
 *     Send an arbitrary EP11 CPRB to an EP11 coprocessor crypto card.
 *
 *   Z90STAT_STATUS_MASK
 *     Return an 64 element array of unsigned chars for the status of
 *     all devices.
 *	 0x01: PCICA
 *	 0x02: PCICC
 *	 0x03: PCIXCC_MCL2
 *	 0x04: PCIXCC_MCL3
 *	 0x05: CEX2C
 *	 0x06: CEX2A
 *	 0x0d: device is disabled via the proc filesystem
 *
 *   Z90STAT_QDEPTH_MASK
 *     Return an 64 element array of unsigned chars for the queue
 *     depth of all devices.
 *
 *   Z90STAT_PERDEV_REQCNT
 *     Return an 64 element array of unsigned integers for the number
 *     of successfully completed requests per device since the device
 *     was detected and made available.
 *
 *   Z90STAT_REQUESTQ_COUNT
 *     Return an integer count of the number of entries waiting to be
 *     sent to a device.
 *
 *   Z90STAT_PENDINGQ_COUNT
 *     Return an integer count of the number of entries sent to all
 *     devices awaiting the reply.
 *
 *   Z90STAT_TOTALOPEN_COUNT
 *     Return an integer count of the number of open file handles.
 *
 *   Z90STAT_DOMAIN_INDEX
 *     Return the integer value of the Cryptographic Domain.
 *
 *   The following ioctls are deprecated and should be no longer used:
 *
 *   Z90STAT_TOTALCOUNT
 *     Return an integer count of all device types together.
 *
 *   Z90STAT_PCICACOUNT
 *     Return an integer count of all PCICAs.
 *
 *   Z90STAT_PCICCCOUNT
 *     Return an integer count of all PCICCs.
 *
 *   Z90STAT_PCIXCCMCL2COUNT
 *     Return an integer count of all MCL2 PCIXCCs.
 *
 *   Z90STAT_PCIXCCMCL3COUNT
 *     Return an integer count of all MCL3 PCIXCCs.
 *
 *   Z90STAT_CEX2CCOUNT
 *     Return an integer count of all CEX2Cs.
 *
 *   Z90STAT_CEX2ACOUNT
 *     Return an integer count of all CEX2As.
 *
 *   ICAZ90STATUS
 *     Return some device driver status in a ica_z90_status struct
 *     This takes an ica_z90_status struct as its arg.
 *
 *   Z90STAT_PCIXCCCOUNT
 *     Return an integer count of all PCIXCCs (MCL2 + MCL3).
 *     This is DEPRECATED now that MCL3 PCIXCCs are treated differently from
 *     MCL2 PCIXCCs.
 */

/**
 * Supported ioctl calls
 */
#define ICARSAMODEXPO	_IOC(_IOC_READ|_IOC_WRITE, ZCRYPT_IOCTL_MAGIC, 0x05, 0)
#define ICARSACRT	_IOC(_IOC_READ|_IOC_WRITE, ZCRYPT_IOCTL_MAGIC, 0x06, 0)
#define ZSECSENDCPRB	_IOC(_IOC_READ|_IOC_WRITE, ZCRYPT_IOCTL_MAGIC, 0x81, 0)
#define ZSENDEP11CPRB	_IOC(_IOC_READ|_IOC_WRITE, ZCRYPT_IOCTL_MAGIC, 0x04, 0)
#define ZDEVICESTATUS	_IOC(_IOC_READ|_IOC_WRITE, ZCRYPT_IOCTL_MAGIC, 0x4f, 0)

/* New status calls */
#define Z90STAT_TOTALCOUNT	_IOR(ZCRYPT_IOCTL_MAGIC, 0x40, int)
#define Z90STAT_PCICACOUNT	_IOR(ZCRYPT_IOCTL_MAGIC, 0x41, int)
#define Z90STAT_PCICCCOUNT	_IOR(ZCRYPT_IOCTL_MAGIC, 0x42, int)
#define Z90STAT_PCIXCCMCL2COUNT	_IOR(ZCRYPT_IOCTL_MAGIC, 0x4b, int)
#define Z90STAT_PCIXCCMCL3COUNT	_IOR(ZCRYPT_IOCTL_MAGIC, 0x4c, int)
#define Z90STAT_CEX2CCOUNT	_IOR(ZCRYPT_IOCTL_MAGIC, 0x4d, int)
#define Z90STAT_CEX2ACOUNT	_IOR(ZCRYPT_IOCTL_MAGIC, 0x4e, int)
#define Z90STAT_REQUESTQ_COUNT	_IOR(ZCRYPT_IOCTL_MAGIC, 0x44, int)
#define Z90STAT_PENDINGQ_COUNT	_IOR(ZCRYPT_IOCTL_MAGIC, 0x45, int)
#define Z90STAT_TOTALOPEN_COUNT _IOR(ZCRYPT_IOCTL_MAGIC, 0x46, int)
#define Z90STAT_DOMAIN_INDEX	_IOR(ZCRYPT_IOCTL_MAGIC, 0x47, int)
#define Z90STAT_STATUS_MASK	_IOR(ZCRYPT_IOCTL_MAGIC, 0x48, char[64])
#define Z90STAT_QDEPTH_MASK	_IOR(ZCRYPT_IOCTL_MAGIC, 0x49, char[64])
#define Z90STAT_PERDEV_REQCNT	_IOR(ZCRYPT_IOCTL_MAGIC, 0x4a, int[64])

#endif /* __ASM_S390_ZCRYPT_H */
