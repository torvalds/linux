/*
 * An implementation of HyperV key value pair (KVP) functionality for Linux.
 *
 *
 * Copyright (C) 2010, Novell, Inc.
 * Author : K. Y. Srinivasan <ksrinivasan@novell.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */
#ifndef	_KVP_H
#define	_KVP_H_

/*
 * Maximum value size - used for both key names and value data, and includes
 * any applicable NULL terminators.
 *
 * Note:  This limit is somewhat arbitrary, but falls easily within what is
 * supported for all native guests (back to Win 2000) and what is reasonable
 * for the IC KVP exchange functionality.  Note that Windows Me/98/95 are
 * limited to 255 character key names.
 *
 * MSDN recommends not storing data values larger than 2048 bytes in the
 * registry.
 *
 * Note:  This value is used in defining the KVP exchange message - this value
 * cannot be modified without affecting the message size and compatibility.
 */

/*
 * bytes, including any null terminators
 */
#define HV_KVP_EXCHANGE_MAX_VALUE_SIZE          (2048)


/*
 * Maximum key size - the registry limit for the length of an entry name
 * is 256 characters, including the null terminator
 */

#define HV_KVP_EXCHANGE_MAX_KEY_SIZE            (512)

/*
 * In Linux, we implement the KVP functionality in two components:
 * 1) The kernel component which is packaged as part of the hv_utils driver
 * is responsible for communicating with the host and responsible for
 * implementing the host/guest protocol. 2) A user level daemon that is
 * responsible for data gathering.
 *
 * Host/Guest Protocol: The host iterates over an index and expects the guest
 * to assign a key name to the index and also return the value corresponding to
 * the key. The host will have atmost one KVP transaction outstanding at any
 * given point in time. The host side iteration stops when the guest returns
 * an error. Microsoft has specified the following mapping of key names to
 * host specified index:
 *
 *	Index		Key Name
 *	0		FullyQualifiedDomainName
 *	1		IntegrationServicesVersion
 *	2		NetworkAddressIPv4
 *	3		NetworkAddressIPv6
 *	4		OSBuildNumber
 *	5		OSName
 *	6		OSMajorVersion
 *	7		OSMinorVersion
 *	8		OSVersion
 *	9		ProcessorArchitecture
 *
 * The Windows host expects the Key Name and Key Value to be encoded in utf16.
 *
 * Guest Kernel/KVP Daemon Protocol: As noted earlier, we implement all of the
 * data gathering functionality in a user mode daemon. The user level daemon
 * is also responsible for binding the key name to the index as well. The
 * kernel and user-level daemon communicate using a connector channel.
 *
 * The user mode component first registers with the
 * the kernel component. Subsequently, the kernel component requests, data
 * for the specified keys. In response to this message the user mode component
 * fills in the value corresponding to the specified key. We overload the
 * sequence field in the cn_msg header to define our KVP message types.
 *
 *
 * The kernel component simply acts as a conduit for communication between the
 * Windows host and the user-level daemon. The kernel component passes up the
 * index received from the Host to the user-level daemon. If the index is
 * valid (supported), the corresponding key as well as its
 * value (both are strings) is returned. If the index is invalid
 * (not supported), a NULL key string is returned.
 */

/*
 *
 * The following definitions are shared with the user-mode component; do not
 * change any of this without making the corresponding changes in
 * the KVP user-mode component.
 */

#define CN_KVP_VAL             0x1 /* This supports queries from the kernel */
#define CN_KVP_USER_VAL       0x2 /* This supports queries from the user */

enum hv_ku_op {
	KVP_REGISTER = 0, /* Register the user mode component */
	KVP_KERNEL_GET, /* Kernel is requesting the value */
	KVP_KERNEL_SET, /* Kernel is providing the value */
	KVP_USER_GET,  /* User is requesting the value */
	KVP_USER_SET  /* User is providing the value */
};

struct hv_ku_msg {
	__u32 kvp_index; /* Key index */
	__u8  kvp_key[HV_KVP_EXCHANGE_MAX_KEY_SIZE]; /* Key name */
	__u8  kvp_value[HV_KVP_EXCHANGE_MAX_VALUE_SIZE]; /* Key  value */
};




#ifdef __KERNEL__

/*
 * Registry value types.
 */

#define REG_SZ 1

enum hv_kvp_exchg_op {
	KVP_OP_GET = 0,
	KVP_OP_SET,
	KVP_OP_DELETE,
	KVP_OP_ENUMERATE,
	KVP_OP_COUNT /* Number of operations, must be last. */
};

enum hv_kvp_exchg_pool {
	KVP_POOL_EXTERNAL = 0,
	KVP_POOL_GUEST,
	KVP_POOL_AUTO,
	KVP_POOL_AUTO_EXTERNAL,
	KVP_POOL_AUTO_INTERNAL,
	KVP_POOL_COUNT /* Number of pools, must be last. */
};

struct hv_kvp_hdr {
	u8 operation;
	u8 pool;
};

struct hv_kvp_exchg_msg_value {
	u32 value_type;
	u32 key_size;
	u32 value_size;
	u8 key[HV_KVP_EXCHANGE_MAX_KEY_SIZE];
	u8 value[HV_KVP_EXCHANGE_MAX_VALUE_SIZE];
};

struct hv_kvp_msg_enumerate {
	u32 index;
	struct hv_kvp_exchg_msg_value data;
};

struct hv_kvp_msg {
	struct hv_kvp_hdr	kvp_hdr;
	struct hv_kvp_msg_enumerate	kvp_data;
};

int hv_kvp_init(void);
void hv_kvp_deinit(void);
void hv_kvp_onchannelcallback(void *);

#endif /* __KERNEL__ */
#endif	/* _KVP_H */

