/*
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
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
 * Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
 * The full GNU General Public License is included in this distribution
 * in the file called LICENSE.GPL.
 *
 * BSD LICENSE
 *
 * Copyright(c) 2008 - 2011 Intel Corporation. All rights reserved.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   * Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *   * Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in
 *     the documentation and/or other materials provided with the
 *     distribution.
 *   * Neither the name of Intel Corporation nor the names of its
 *     contributors may be used to endorse or promote products derived
 *     from this software without specific prior written permission.
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
 */

#ifndef _SATI_DEVICE_H_
#define _SATI_DEVICE_H_

/**
 * This file contains all of the defintions for the SATI remote device object.
 *    Some translations require information to be remembered on a per device
 *    basis.  This information is stored in the object defined in this file.
 *
 *
 */

#include "sati_types.h"
#include "intel_ata.h"

/**
 * enum _SATI_DEVICE_STATE - This enumeration depicts the various states
 *    possible for the a translation remote device object.
 *
 *
 */
enum sati_device_state {
	SATI_DEVICE_STATE_OPERATIONAL,
	SATI_DEVICE_STATE_STOPPED,
	SATI_DEVICE_STATE_STANDBY,
	SATI_DEVICE_STATE_IDLE,
	SATI_DEVICE_STATE_DEVICE_FAULT_OCCURRED,
	SATI_DEVICE_STATE_FORMAT_UNIT_IN_PROGRESS,
	SATI_DEVICE_STATE_SELF_TEST_IN_PROGRESS,
	SATI_DEVICE_STATE_SEQUENCE_INCOMPLETE,
	SATI_DEVICE_STATE_UNIT_ATTENTION_CONDITION

};

/**
 *
 *
 * SATI_DEVICE_CAPABILITIES These constants define the various capabilities
 * that a remote device may support for which there is an impact on translation.
 */
#define SATI_DEVICE_CAP_UDMA_ENABLE          0x00000001
#define SATI_DEVICE_CAP_NCQ_REQUESTED_ENABLE 0x00000002
#define SATI_DEVICE_CAP_NCQ_SUPPORTED_ENABLE 0x00000004
#define SATI_DEVICE_CAP_48BIT_ENABLE         0x00000008
#define SATI_DEVICE_CAP_DMA_FUA_ENABLE       0x00000010
#define SATI_DEVICE_CAP_SMART_SUPPORT        0x00000020
#define SATI_DEVICE_CAP_REMOVABLE_MEDIA      0x00000040
#define SATI_DEVICE_CAP_SMART_ENABLE         0x00000080
#define SATI_DEVICE_CAP_WRITE_UNCORRECTABLE_ENABLE           0x00000100
#define SATI_DEVICE_CAP_MULTIPLE_SECTORS_PER_PHYSCIAL_SECTOR 0x00000200
#define SATI_DEVICE_CAP_SMART_SELF_TEST_SUPPORT              0x00000400


/**
 * struct sati_device - The SATI_DEVICE structure define the state of the
 *    remote device with respect to translation.
 *
 *
 */
struct sati_device {
	/**
	 * This field simply dictates the state of the SATI device.
	 */
	enum sati_device_state state;

	/**
	 * This field indicates features supported by the remote device that
	 * impact translation execution.
	 */
	u16 capabilities;

	/**
	 * This field indicates the depth of the native command queue supported
	 * by the device.
	 */
	u8 ncq_depth;

	/**
	 * This field stores the additional sense code for a unit attention
	 * condition.
	 */
	u8 unit_attention_asc;

	/**
	 * This field indicates the additional sense code qualifier for a unit
	 * attention condition.
	 */
	u8 unit_attention_ascq;

};

void sati_device_construct(
	struct sati_device *device,
	bool is_ncq_enabled,
	u8 max_ncq_depth);

void sati_device_update_capabilities(
	struct sati_device *device,
	struct ata_identify_device_data *identify);

#endif /* _SATI_TRANSLATOR_SEQUENCE_H_ */

