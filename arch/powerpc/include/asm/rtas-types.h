/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef _ASM_POWERPC_RTAS_TYPES_H
#define _ASM_POWERPC_RTAS_TYPES_H

#include <linux/spinlock_types.h>

typedef __be32 rtas_arg_t;

struct rtas_args {
	__be32 token;
	__be32 nargs;
	__be32 nret;
	rtas_arg_t args[16];
	rtas_arg_t *rets;     /* Pointer to return values in args[]. */
};

struct rtas_t {
	unsigned long entry;		/* physical address pointer */
	unsigned long base;		/* physical address pointer */
	unsigned long size;
	struct device_node *dev;	/* virtual address pointer */
};

struct rtas_error_log {
	/* Byte 0 */
	u8		byte0;			/* Architectural version */

	/* Byte 1 */
	u8		byte1;
	/* XXXXXXXX
	 * XXX		3: Severity level of error
	 *    XX	2: Degree of recovery
	 *      X	1: Extended log present?
	 *       XX	2: Reserved
	 */

	/* Byte 2 */
	u8		byte2;
	/* XXXXXXXX
	 * XXXX		4: Initiator of event
	 *     XXXX	4: Target of failed operation
	 */
	u8		byte3;			/* General event or error*/
	__be32		extended_log_length;	/* length in bytes */
	unsigned char	buffer[1];		/* Start of extended log */
						/* Variable length.      */
};

/* RTAS general extended event log, Version 6. The extended log starts
 * from "buffer" field of struct rtas_error_log defined above.
 */
struct rtas_ext_event_log_v6 {
	/* Byte 0 */
	u8 byte0;
	/* XXXXXXXX
	 * X		1: Log valid
	 *  X		1: Unrecoverable error
	 *   X		1: Recoverable (correctable or successfully retried)
	 *    X		1: Bypassed unrecoverable error (degraded operation)
	 *     X	1: Predictive error
	 *      X	1: "New" log (always 1 for data returned from RTAS)
	 *       X	1: Big Endian
	 *        X	1: Reserved
	 */

	/* Byte 1 */
	u8 byte1;			/* reserved */

	/* Byte 2 */
	u8 byte2;
	/* XXXXXXXX
	 * X		1: Set to 1 (indicating log is in PowerPC format)
	 *  XXX		3: Reserved
	 *     XXXX	4: Log format used for bytes 12-2047
	 */

	/* Byte 3 */
	u8 byte3;			/* reserved */
	/* Byte 4-11 */
	u8 reserved[8];			/* reserved */
	/* Byte 12-15 */
	__be32  company_id;		/* Company ID of the company	*/
					/* that defines the format for	*/
					/* the vendor specific log type	*/
	/* Byte 16-end of log */
	u8 vendor_log[1];		/* Start of vendor specific log	*/
					/* Variable length.		*/
};

/* Vendor specific Platform Event Log Format, Version 6, section header */
struct pseries_errorlog {
	__be16 id;			/* 0x00 2-byte ASCII section ID	*/
	__be16 length;			/* 0x02 Section length in bytes	*/
	u8 version;			/* 0x04 Section version		*/
	u8 subtype;			/* 0x05 Section subtype		*/
	__be16 creator_component;	/* 0x06 Creator component ID	*/
	u8 data[];			/* 0x08 Start of section data	*/
};

/* RTAS pseries hotplug errorlog section */
struct pseries_hp_errorlog {
	u8	resource;
	u8	action;
	u8	id_type;
	u8	reserved;
	union {
		__be32	drc_index;
		__be32	drc_count;
		struct { __be32 count, index; } ic;
		char	drc_name[1];
	} _drc_u;
};

#endif /* _ASM_POWERPC_RTAS_TYPES_H */
