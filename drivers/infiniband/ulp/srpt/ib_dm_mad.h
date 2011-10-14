/*
 * Copyright (c) 2006 - 2009 Mellanox Technology Inc.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#ifndef IB_DM_MAD_H
#define IB_DM_MAD_H

#include <linux/types.h>

#include <rdma/ib_mad.h>

enum {
	/*
	 * See also section 13.4.7 Status Field, table 115 MAD Common Status
	 * Field Bit Values and also section 16.3.1.1 Status Field in the
	 * InfiniBand Architecture Specification.
	 */
	DM_MAD_STATUS_UNSUP_METHOD = 0x0008,
	DM_MAD_STATUS_UNSUP_METHOD_ATTR = 0x000c,
	DM_MAD_STATUS_INVALID_FIELD = 0x001c,
	DM_MAD_STATUS_NO_IOC = 0x0100,

	/*
	 * See also the Device Management chapter, section 16.3.3 Attributes,
	 * table 279 Device Management Attributes in the InfiniBand
	 * Architecture Specification.
	 */
	DM_ATTR_CLASS_PORT_INFO = 0x01,
	DM_ATTR_IOU_INFO = 0x10,
	DM_ATTR_IOC_PROFILE = 0x11,
	DM_ATTR_SVC_ENTRIES = 0x12
};

struct ib_dm_hdr {
	u8 reserved[28];
};

/*
 * Structure of management datagram sent by the SRP target implementation.
 * Contains a management datagram header, reliable multi-packet transaction
 * protocol (RMPP) header and ib_dm_hdr. Notes:
 * - The SRP target implementation does not use RMPP or ib_dm_hdr when sending
 *   management datagrams.
 * - The header size must be exactly 64 bytes (IB_MGMT_DEVICE_HDR), since this
 *   is the header size that is passed to ib_create_send_mad() in ib_srpt.c.
 * - The maximum supported size for a management datagram when not using RMPP
 *   is 256 bytes -- 64 bytes header and 192 (IB_MGMT_DEVICE_DATA) bytes data.
 */
struct ib_dm_mad {
	struct ib_mad_hdr mad_hdr;
	struct ib_rmpp_hdr rmpp_hdr;
	struct ib_dm_hdr dm_hdr;
	u8 data[IB_MGMT_DEVICE_DATA];
};

/*
 * IOUnitInfo as defined in section 16.3.3.3 IOUnitInfo of the InfiniBand
 * Architecture Specification.
 */
struct ib_dm_iou_info {
	__be16 change_id;
	u8 max_controllers;
	u8 op_rom;
	u8 controller_list[128];
};

/*
 * IOControllerprofile as defined in section 16.3.3.4 IOControllerProfile of
 * the InfiniBand Architecture Specification.
 */
struct ib_dm_ioc_profile {
	__be64 guid;
	__be32 vendor_id;
	__be32 device_id;
	__be16 device_version;
	__be16 reserved1;
	__be32 subsys_vendor_id;
	__be32 subsys_device_id;
	__be16 io_class;
	__be16 io_subclass;
	__be16 protocol;
	__be16 protocol_version;
	__be16 service_conn;
	__be16 initiators_supported;
	__be16 send_queue_depth;
	u8 reserved2;
	u8 rdma_read_depth;
	__be32 send_size;
	__be32 rdma_size;
	u8 op_cap_mask;
	u8 svc_cap_mask;
	u8 num_svc_entries;
	u8 reserved3[9];
	u8 id_string[64];
};

struct ib_dm_svc_entry {
	u8 name[40];
	__be64 id;
};

/*
 * See also section 16.3.3.5 ServiceEntries in the InfiniBand Architecture
 * Specification. See also section B.7, table B.8 in the T10 SRP r16a document.
 */
struct ib_dm_svc_entries {
	struct ib_dm_svc_entry service_entries[4];
};

#endif
