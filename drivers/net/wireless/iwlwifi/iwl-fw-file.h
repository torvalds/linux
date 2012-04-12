/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2012 Intel Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2012 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *****************************************************************************/

#ifndef __iwl_fw_file_h__
#define __iwl_fw_file_h__

#include <linux/netdevice.h>

/* v1/v2 uCode file layout */
struct iwl_ucode_header {
	__le32 ver;	/* major/minor/API/serial */
	union {
		struct {
			__le32 inst_size;	/* bytes of runtime code */
			__le32 data_size;	/* bytes of runtime data */
			__le32 init_size;	/* bytes of init code */
			__le32 init_data_size;	/* bytes of init data */
			__le32 boot_size;	/* bytes of bootstrap code */
			u8 data[0];		/* in same order as sizes */
		} v1;
		struct {
			__le32 build;		/* build number */
			__le32 inst_size;	/* bytes of runtime code */
			__le32 data_size;	/* bytes of runtime data */
			__le32 init_size;	/* bytes of init code */
			__le32 init_data_size;	/* bytes of init data */
			__le32 boot_size;	/* bytes of bootstrap code */
			u8 data[0];		/* in same order as sizes */
		} v2;
	} u;
};

/*
 * new TLV uCode file layout
 *
 * The new TLV file format contains TLVs, that each specify
 * some piece of data. To facilitate "groups", for example
 * different instruction image with different capabilities,
 * bundled with the same init image, an alternative mechanism
 * is provided:
 * When the alternative field is 0, that means that the item
 * is always valid. When it is non-zero, then it is only
 * valid in conjunction with items of the same alternative,
 * in which case the driver (user) selects one alternative
 * to use.
 */

enum iwl_ucode_tlv_type {
	IWL_UCODE_TLV_INVALID		= 0, /* unused */
	IWL_UCODE_TLV_INST		= 1,
	IWL_UCODE_TLV_DATA		= 2,
	IWL_UCODE_TLV_INIT		= 3,
	IWL_UCODE_TLV_INIT_DATA		= 4,
	IWL_UCODE_TLV_BOOT		= 5,
	IWL_UCODE_TLV_PROBE_MAX_LEN	= 6, /* a u32 value */
	IWL_UCODE_TLV_PAN		= 7,
	IWL_UCODE_TLV_RUNT_EVTLOG_PTR	= 8,
	IWL_UCODE_TLV_RUNT_EVTLOG_SIZE	= 9,
	IWL_UCODE_TLV_RUNT_ERRLOG_PTR	= 10,
	IWL_UCODE_TLV_INIT_EVTLOG_PTR	= 11,
	IWL_UCODE_TLV_INIT_EVTLOG_SIZE	= 12,
	IWL_UCODE_TLV_INIT_ERRLOG_PTR	= 13,
	IWL_UCODE_TLV_ENHANCE_SENS_TBL	= 14,
	IWL_UCODE_TLV_PHY_CALIBRATION_SIZE = 15,
	IWL_UCODE_TLV_WOWLAN_INST	= 16,
	IWL_UCODE_TLV_WOWLAN_DATA	= 17,
	IWL_UCODE_TLV_FLAGS		= 18,
	IWL_UCODE_TLV_SEC_RT		= 19,
	IWL_UCODE_TLV_SEC_INIT		= 20,
	IWL_UCODE_TLV_SEC_WOWLAN	= 21,
	IWL_UCODE_TLV_DEF_CALIB		= 22,
	IWL_UCODE_TLV_PHY_SKU		= 23,
};

struct iwl_ucode_tlv {
	__le16 type;		/* see above */
	__le16 alternative;	/* see comment */
	__le32 length;		/* not including type/length fields */
	u8 data[0];
};

#define IWL_TLV_UCODE_MAGIC	0x0a4c5749

struct iwl_tlv_ucode_header {
	/*
	 * The TLV style ucode header is distinguished from
	 * the v1/v2 style header by first four bytes being
	 * zero, as such is an invalid combination of
	 * major/minor/API/serial versions.
	 */
	__le32 zero;
	__le32 magic;
	u8 human_readable[64];
	__le32 ver;		/* major/minor/API/serial */
	__le32 build;
	__le64 alternatives;	/* bitmask of valid alternatives */
	/*
	 * The data contained herein has a TLV layout,
	 * see above for the TLV header and types.
	 * Note that each TLV is padded to a length
	 * that is a multiple of 4 for alignment.
	 */
	u8 data[0];
};

#endif  /* __iwl_fw_file_h__ */
