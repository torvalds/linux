/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * PAPR nvDimm Specific Methods (PDSM) and structs for libndctl
 *
 * (C) Copyright IBM 2020
 *
 * Author: Vaibhav Jain <vaibhav at linux.ibm.com>
 */

#ifndef _UAPI_ASM_POWERPC_PAPR_PDSM_H_
#define _UAPI_ASM_POWERPC_PAPR_PDSM_H_

#include <linux/types.h>
#include <linux/ndctl.h>

/*
 * PDSM Envelope:
 *
 * The ioctl ND_CMD_CALL exchange data between user-space and kernel via
 * envelope which consists of 2 headers sections and payload sections as
 * illustrated below:
 *  +-----------------+---------------+---------------------------+
 *  |   64-Bytes      |   8-Bytes     |       Max 184-Bytes       |
 *  +-----------------+---------------+---------------------------+
 *  | ND-HEADER       |  PDSM-HEADER  |      PDSM-PAYLOAD         |
 *  +-----------------+---------------+---------------------------+
 *  | nd_family       |               |                           |
 *  | nd_size_out     | cmd_status    |                           |
 *  | nd_size_in      | reserved      |     nd_pdsm_payload       |
 *  | nd_command      | payload   --> |                           |
 *  | nd_fw_size      |               |                           |
 *  | nd_payload ---> |               |                           |
 *  +---------------+-----------------+---------------------------+
 *
 * ND Header:
 * This is the generic libnvdimm header described as 'struct nd_cmd_pkg'
 * which is interpreted by libnvdimm before passed on to papr_scm. Important
 * member fields used are:
 * 'nd_family'		: (In) NVDIMM_FAMILY_PAPR_SCM
 * 'nd_size_in'		: (In) PDSM-HEADER + PDSM-IN-PAYLOAD (usually 0)
 * 'nd_size_out'        : (In) PDSM-HEADER + PDSM-RETURN-PAYLOAD
 * 'nd_command'         : (In) One of PAPR_PDSM_XXX
 * 'nd_fw_size'         : (Out) PDSM-HEADER + size of actual payload returned
 *
 * PDSM Header:
 * This is papr-scm specific header that precedes the payload. This is defined
 * as nd_cmd_pdsm_pkg.  Following fields aare available in this header:
 *
 * 'cmd_status'		: (Out) Errors if any encountered while servicing PDSM.
 * 'reserved'		: Not used, reserved for future and should be set to 0.
 * 'payload'            : A union of all the possible payload structs
 *
 * PDSM Payload:
 *
 * The layout of the PDSM Payload is defined by various structs shared between
 * papr_scm and libndctl so that contents of payload can be interpreted. As such
 * its defined as a union of all possible payload structs as
 * 'union nd_pdsm_payload'. Based on the value of 'nd_cmd_pkg.nd_command'
 * appropriate member of the union is accessed.
 */

/* Max payload size that we can handle */
#define ND_PDSM_PAYLOAD_MAX_SIZE 184

/* Max payload size that we can handle */
#define ND_PDSM_HDR_SIZE \
	(sizeof(struct nd_pkg_pdsm) - ND_PDSM_PAYLOAD_MAX_SIZE)

/* Various nvdimm health indicators */
#define PAPR_PDSM_DIMM_HEALTHY       0
#define PAPR_PDSM_DIMM_UNHEALTHY     1
#define PAPR_PDSM_DIMM_CRITICAL      2
#define PAPR_PDSM_DIMM_FATAL         3

/*
 * Struct exchanged between kernel & ndctl in for PAPR_PDSM_HEALTH
 * Various flags indicate the health status of the dimm.
 *
 * extension_flags	: Any extension fields present in the struct.
 * dimm_unarmed		: Dimm not armed. So contents wont persist.
 * dimm_bad_shutdown	: Previous shutdown did not persist contents.
 * dimm_bad_restore	: Contents from previous shutdown werent restored.
 * dimm_scrubbed	: Contents of the dimm have been scrubbed.
 * dimm_locked		: Contents of the dimm cant be modified until CEC reboot
 * dimm_encrypted	: Contents of dimm are encrypted.
 * dimm_health		: Dimm health indicator. One of PAPR_PDSM_DIMM_XXXX
 */
struct nd_papr_pdsm_health {
	union {
		struct {
			__u32 extension_flags;
			__u8 dimm_unarmed;
			__u8 dimm_bad_shutdown;
			__u8 dimm_bad_restore;
			__u8 dimm_scrubbed;
			__u8 dimm_locked;
			__u8 dimm_encrypted;
			__u16 dimm_health;
		};
		__u8 buf[ND_PDSM_PAYLOAD_MAX_SIZE];
	};
};

/*
 * Methods to be embedded in ND_CMD_CALL request. These are sent to the kernel
 * via 'nd_cmd_pkg.nd_command' member of the ioctl struct
 */
enum papr_pdsm {
	PAPR_PDSM_MIN = 0x0,
	PAPR_PDSM_HEALTH,
	PAPR_PDSM_MAX,
};

/* Maximal union that can hold all possible payload types */
union nd_pdsm_payload {
	struct nd_papr_pdsm_health health;
	__u8 buf[ND_PDSM_PAYLOAD_MAX_SIZE];
} __packed;

/*
 * PDSM-header + payload expected with ND_CMD_CALL ioctl from libnvdimm
 * Valid member of union 'payload' is identified via 'nd_cmd_pkg.nd_command'
 * that should always precede this struct when sent to papr_scm via CMD_CALL
 * interface.
 */
struct nd_pkg_pdsm {
	__s32 cmd_status;	/* Out: Sub-cmd status returned back */
	__u16 reserved[2];	/* Ignored and to be set as '0' */
	union nd_pdsm_payload payload;
} __packed;

#endif /* _UAPI_ASM_POWERPC_PAPR_PDSM_H_ */
