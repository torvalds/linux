/* SPDX-License-Identifier: GPL-2.0+ WITH Linux-syscall-note */
/*
 *  include/asm-s390/zcrypt.h
 *
 *  zcrypt 2.2.1 (user-visible header)
 *
 *  Copyright IBM Corp. 2001, 2019
 *  Author(s): Robert Burroughs
 *	       Eric Rossman (edrossma@us.ibm.com)
 *
 *  Hotplug & misc device support: Jochen Roehrig (roehrig@de.ibm.com)
 */

#ifndef __ASM_S390_ZCRYPT_H
#define __ASM_S390_ZCRYPT_H

#define ZCRYPT_VERSION 2
#define ZCRYPT_RELEASE 2
#define ZCRYPT_VARIANT 1

#include <linux/ioctl.h>
#include <linux/compiler.h>
#include <linux/types.h>

/* Name of the zcrypt device driver. */
#define ZCRYPT_NAME "zcrypt"

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
	__u8 __user  *inputdata;
	__u32	      inputdatalength;
	__u8 __user  *outputdata;
	__u32	      outputdatalength;
	__u8 __user  *b_key;
	__u8 __user  *n_modulus;
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
	__u8 __user  *inputdata;
	__u32	      inputdatalength;
	__u8 __user  *outputdata;
	__u32	      outputdatalength;
	__u8 __user  *bp_key;
	__u8 __user  *bq_key;
	__u8 __user  *np_prime;
	__u8 __user  *nq_prime;
	__u8 __user  *u_mult_inv;
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
	__u16	     cprb_len;		/* CPRB length	      220	 */
	__u8	     cprb_ver_id;	/* CPRB version id.   0x02	 */
	__u8	     pad_000[3];	/* Alignment pad bytes		 */
	__u8	     func_id[2];	/* function id	      0x5432	 */
	__u8	     cprb_flags[4];	/* Flags			 */
	__u32	     req_parml;		/* request parameter buffer len	 */
	__u32	     req_datal;		/* request data buffer		 */
	__u32	     rpl_msgbl;		/* reply  message block length	 */
	__u32	     rpld_parml;	/* replied parameter block len	 */
	__u32	     rpl_datal;		/* reply data block len		 */
	__u32	     rpld_datal;	/* replied data block len	 */
	__u32	     req_extbl;		/* request extension block len	 */
	__u8	     pad_001[4];	/* reserved			 */
	__u32	     rpld_extbl;	/* replied extension block len	 */
	__u8	     padx000[16 - sizeof(__u8 *)];
	__u8 __user *req_parmb;		/* request parm block 'address'	 */
	__u8	     padx001[16 - sizeof(__u8 *)];
	__u8 __user *req_datab;		/* request data block 'address'	 */
	__u8	     padx002[16 - sizeof(__u8 *)];
	__u8 __user *rpl_parmb;		/* reply parm block 'address'	 */
	__u8	     padx003[16 - sizeof(__u8 *)];
	__u8 __user *rpl_datab;		/* reply data block 'address'	 */
	__u8	     padx004[16 - sizeof(__u8 *)];
	__u8 __user *req_extb;		/* request extension block 'addr'*/
	__u8	     padx005[16 - sizeof(__u8 *)];
	__u8 __user *rpl_extb;		/* reply extension block 'address'*/
	__u16	     ccp_rtcode;	/* server return code		 */
	__u16	     ccp_rscode;	/* server reason code		 */
	__u32	     mac_data_len;	/* Mac Data Length		 */
	__u8	     logon_id[8];	/* Logon Identifier		 */
	__u8	     mac_value[8];	/* Mac Value			 */
	__u8	     mac_content_flgs;	/* Mac content flag byte	 */
	__u8	     pad_002;		/* Alignment			 */
	__u16	     domain;		/* Domain			 */
	__u8	     usage_domain[4];	/* Usage domain			 */
	__u8	     cntrl_domain[4];	/* Control domain		 */
	__u8	     S390enf_mask[4];	/* S/390 enforcement mask	 */
	__u8	     pad_004[36];	/* reserved			 */
} __attribute__((packed));

/**
 * xcRB
 */
struct ica_xcRB {
	__u16	      agent_ID;
	__u32	      user_defined;
	__u16	      request_ID;
	__u32	      request_control_blk_length;
	__u8	      _padding1[16 - sizeof(__u8 *)];
	__u8 __user  *request_control_blk_addr;
	__u32	      request_data_length;
	__u8	      _padding2[16 - sizeof(__u8 *)];
	__u8 __user  *request_data_address;
	__u32	      reply_control_blk_length;
	__u8	      _padding3[16 - sizeof(__u8 *)];
	__u8 __user  *reply_control_blk_addr;
	__u32	      reply_data_length;
	__u8	      __padding4[16 - sizeof(__u8 *)];
	__u8 __user  *reply_data_addr;
	__u16	      priority_window;
	__u32	      status;
} __attribute__((packed));

/**
 * struct ep11_cprb - EP11 connectivity programming request block
 * @cprb_len:		CPRB header length [0x0020]
 * @cprb_ver_id:	CPRB version id.   [0x04]
 * @pad_000:		Alignment pad bytes
 * @flags:		Admin bit [0x80], Special bit [0x20]
 * @func_id:		Function id / subtype [0x5434] "T4"
 * @source_id:		Source id [originator id]
 * @target_id:		Target id [usage/ctrl domain id]
 * @ret_code:		Return code
 * @reserved1:		Reserved
 * @reserved2:		Reserved
 * @payload_len:	Payload length
 */
struct ep11_cprb {
	__u16	cprb_len;
	__u8	cprb_ver_id;
	__u8	pad_000[2];
	__u8	flags;
	__u8	func_id[2];
	__u32	source_id;
	__u32	target_id;
	__u32	ret_code;
	__u32	reserved1;
	__u32	reserved2;
	__u32	payload_len;
} __attribute__((packed));

/**
 * struct ep11_target_dev - EP11 target device list
 * @ap_id:	AP device id
 * @dom_id:	Usage domain id
 */
struct ep11_target_dev {
	__u16 ap_id;
	__u16 dom_id;
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
	__u16		targets_num;
	__u8 __user    *targets;
	__u64		weight;
	__u64		req_no;
	__u64		req_len;
	__u8 __user    *req;
	__u64		resp_len;
	__u8 __user    *resp;
} __attribute__((packed));

/**
 * struct zcrypt_device_status_ext
 * @hwtype:		raw hardware type
 * @qid:		8 bit device index, 8 bit domain
 * @functions:		AP device function bit field 'abcdef'
 *			a, b, c = reserved
 *			d = CCA coprocessor
 *			e = Accelerator
 *			f = EP11 coprocessor
 * @online		online status
 * @reserved		reserved
 */
struct zcrypt_device_status_ext {
	unsigned int hwtype:8;
	unsigned int qid:16;
	unsigned int online:1;
	unsigned int functions:6;
	unsigned int reserved:1;
};

#define MAX_ZDEV_CARDIDS_EXT 256
#define MAX_ZDEV_DOMAINS_EXT 256

/* Maximum number of zcrypt devices */
#define MAX_ZDEV_ENTRIES_EXT (MAX_ZDEV_CARDIDS_EXT * MAX_ZDEV_DOMAINS_EXT)

/* Device matrix of all zcrypt devices */
struct zcrypt_device_matrix_ext {
	struct zcrypt_device_status_ext device[MAX_ZDEV_ENTRIES_EXT];
};

#define AUTOSELECT  0xFFFFFFFF
#define AUTOSEL_AP  ((__u16) 0xFFFF)
#define AUTOSEL_DOM ((__u16) 0xFFFF)

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
 *   ZCRYPT_DEVICE_STATUS
 *     The given struct zcrypt_device_matrix_ext is updated with
 *     status information for each currently known apqn.
 *
 *   ZCRYPT_STATUS_MASK
 *     Return an MAX_ZDEV_CARDIDS_EXT element array of unsigned chars for the
 *     status of all devices.
 *	 0x01: PCICA
 *	 0x02: PCICC
 *	 0x03: PCIXCC_MCL2
 *	 0x04: PCIXCC_MCL3
 *	 0x05: CEX2C
 *	 0x06: CEX2A
 *	 0x07: CEX3C
 *	 0x08: CEX3A
 *	 0x0a: CEX4
 *	 0x0b: CEX5
 *	 0x0c: CEX6, CEX7 or CEX8
 *	 0x0d: device is disabled
 *
 *   ZCRYPT_QDEPTH_MASK
 *     Return an MAX_ZDEV_CARDIDS_EXT element array of unsigned chars for the
 *     queue depth of all devices.
 *
 *   ZCRYPT_PERDEV_REQCNT
 *     Return an MAX_ZDEV_CARDIDS_EXT element array of unsigned integers for
 *     the number of successfully completed requests per device since the
 *     device was detected and made available.
 *
 */

/**
 * Supported ioctl calls
 */
#define ICARSAMODEXPO	_IOC(_IOC_READ|_IOC_WRITE, ZCRYPT_IOCTL_MAGIC, 0x05, 0)
#define ICARSACRT	_IOC(_IOC_READ|_IOC_WRITE, ZCRYPT_IOCTL_MAGIC, 0x06, 0)
#define ZSECSENDCPRB	_IOC(_IOC_READ|_IOC_WRITE, ZCRYPT_IOCTL_MAGIC, 0x81, 0)
#define ZSENDEP11CPRB	_IOC(_IOC_READ|_IOC_WRITE, ZCRYPT_IOCTL_MAGIC, 0x04, 0)

#define ZCRYPT_DEVICE_STATUS _IOC(_IOC_READ|_IOC_WRITE, ZCRYPT_IOCTL_MAGIC, 0x5f, 0)
#define ZCRYPT_STATUS_MASK   _IOR(ZCRYPT_IOCTL_MAGIC, 0x58, char[MAX_ZDEV_CARDIDS_EXT])
#define ZCRYPT_QDEPTH_MASK   _IOR(ZCRYPT_IOCTL_MAGIC, 0x59, char[MAX_ZDEV_CARDIDS_EXT])
#define ZCRYPT_PERDEV_REQCNT _IOR(ZCRYPT_IOCTL_MAGIC, 0x5a, int[MAX_ZDEV_CARDIDS_EXT])

/*
 * Support for multiple zcrypt device nodes.
 */

/* Nr of minor device node numbers to allocate. */
#define ZCRYPT_MAX_MINOR_NODES 256

/* Max amount of possible ioctls */
#define MAX_ZDEV_IOCTLS (1 << _IOC_NRBITS)

/*
 * Only deprecated defines, structs and ioctls below this line.
 */

/* Deprecated: use MAX_ZDEV_CARDIDS_EXT */
#define MAX_ZDEV_CARDIDS 64
/* Deprecated: use MAX_ZDEV_DOMAINS_EXT */
#define MAX_ZDEV_DOMAINS 256

/* Deprecated: use MAX_ZDEV_ENTRIES_EXT */
#define MAX_ZDEV_ENTRIES (MAX_ZDEV_CARDIDS * MAX_ZDEV_DOMAINS)

/* Deprecated: use struct zcrypt_device_status_ext */
struct zcrypt_device_status {
	unsigned int hwtype:8;
	unsigned int qid:14;
	unsigned int online:1;
	unsigned int functions:6;
	unsigned int reserved:3;
};

/* Deprecated: use struct zcrypt_device_matrix_ext */
struct zcrypt_device_matrix {
	struct zcrypt_device_status device[MAX_ZDEV_ENTRIES];
};

/* Deprecated: use ZCRYPT_DEVICE_STATUS */
#define ZDEVICESTATUS _IOC(_IOC_READ|_IOC_WRITE, ZCRYPT_IOCTL_MAGIC, 0x4f, 0)
/* Deprecated: use ZCRYPT_STATUS_MASK */
#define Z90STAT_STATUS_MASK _IOR(ZCRYPT_IOCTL_MAGIC, 0x48, char[64])
/* Deprecated: use ZCRYPT_QDEPTH_MASK */
#define Z90STAT_QDEPTH_MASK _IOR(ZCRYPT_IOCTL_MAGIC, 0x49, char[64])
/* Deprecated: use ZCRYPT_PERDEV_REQCNT */
#define Z90STAT_PERDEV_REQCNT _IOR(ZCRYPT_IOCTL_MAGIC, 0x4a, int[64])

/* Deprecated: use sysfs to query these values */
#define Z90STAT_REQUESTQ_COUNT	_IOR(ZCRYPT_IOCTL_MAGIC, 0x44, int)
#define Z90STAT_PENDINGQ_COUNT	_IOR(ZCRYPT_IOCTL_MAGIC, 0x45, int)
#define Z90STAT_TOTALOPEN_COUNT _IOR(ZCRYPT_IOCTL_MAGIC, 0x46, int)
#define Z90STAT_DOMAIN_INDEX	_IOR(ZCRYPT_IOCTL_MAGIC, 0x47, int)

/*
 * The ioctl number ranges 0x40 - 0x42 and 0x4b - 0x4e had been used in the
 * past, don't assign new ioctls for these.
 */

#endif /* __ASM_S390_ZCRYPT_H */
