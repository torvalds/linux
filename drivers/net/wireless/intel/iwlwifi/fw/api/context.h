/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
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
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2012 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016 - 2017 Intel Deutschland GmbH
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
 *
 *****************************************************************************/

#ifndef __iwl_fw_api_context_h__
#define __iwl_fw_api_context_h__

/**
 * enum iwl_ctxt_id_and_color - ID and color fields in context dword
 * @FW_CTXT_ID_POS: position of the ID
 * @FW_CTXT_ID_MSK: mask of the ID
 * @FW_CTXT_COLOR_POS: position of the color
 * @FW_CTXT_COLOR_MSK: mask of the color
 * @FW_CTXT_INVALID: value used to indicate unused/invalid
 */
enum iwl_ctxt_id_and_color {
	FW_CTXT_ID_POS		= 0,
	FW_CTXT_ID_MSK		= 0xff << FW_CTXT_ID_POS,
	FW_CTXT_COLOR_POS	= 8,
	FW_CTXT_COLOR_MSK	= 0xff << FW_CTXT_COLOR_POS,
	FW_CTXT_INVALID		= 0xffffffff,
};

#define FW_CMD_ID_AND_COLOR(_id, _color) (((_id) << FW_CTXT_ID_POS) |\
					  ((_color) << FW_CTXT_COLOR_POS))

/* Possible actions on PHYs, MACs and Bindings */
enum iwl_ctxt_action {
	FW_CTXT_ACTION_STUB = 0,
	FW_CTXT_ACTION_ADD,
	FW_CTXT_ACTION_MODIFY,
	FW_CTXT_ACTION_REMOVE,
	FW_CTXT_ACTION_NUM
}; /* COMMON_CONTEXT_ACTION_API_E_VER_1 */

#endif /* __iwl_fw_api_context_h__ */
