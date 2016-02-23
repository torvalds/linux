/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2015 Cavium, Inc.
 *
 * This file is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, Version 2, as
 * published by the Free Software Foundation.
 *
 * This file is distributed in the hope that it will be useful, but
 * AS-IS and WITHOUT ANY WARRANTY; without even the implied warranty
 * of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE, TITLE, or
 * NONINFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * This file may also be available under a different license from Cavium.
 * Contact Cavium, Inc. for more information
 **********************************************************************/

/*!  \file octeon_nic.h
 *   \brief Host NIC Driver: Routine to send network data &
 *   control packet to Octeon.
 */

#ifndef __OCTEON_NIC_H__
#define  __OCTEON_NIC_H__

/* Maximum number of 8-byte words can be sent in a NIC control message.
 */
#define  MAX_NCTRL_UDD  32

typedef void (*octnic_ctrl_pkt_cb_fn_t) (void *);

/* Structure of control information passed by the NIC module to the OSI
 * layer when sending control commands to Octeon device software.
 */
struct octnic_ctrl_pkt {
	/** Command to be passed to the Octeon device software. */
	union octnet_cmd ncmd;

	/** Send buffer  */
	void *data;
	u64 dmadata;

	/** Response buffer */
	void *rdata;
	u64 dmardata;

	/** Additional data that may be needed by some commands. */
	u64 udd[MAX_NCTRL_UDD];

	/** Time to wait for Octeon software to respond to this control command.
	 *  If wait_time is 0, OSI assumes no response is expected.
	 */
	size_t wait_time;

	/** The network device that issued the control command. */
	u64 netpndev;

	/** Callback function called when the command has been fetched */
	octnic_ctrl_pkt_cb_fn_t cb_fn;
};

#define MAX_UDD_SIZE(nctrl) (sizeof(nctrl->udd))

/** Structure of data information passed by the NIC module to the OSI
 * layer when forwarding data to Octeon device software.
 */
struct octnic_data_pkt {
	/** Pointer to information maintained by NIC module for this packet. The
	 *  OSI layer passes this as-is to the driver.
	 */
	void *buf;

	/** Type of buffer passed in "buf" above. */
	u32 reqtype;

	/** Total data bytes to be transferred in this command. */
	u32 datasize;

	/** Command to be passed to the Octeon device software. */
	struct octeon_instr_64B cmd;

	/** Input queue to use to send this command. */
	u32 q_no;

};

/** Structure passed by NIC module to OSI layer to prepare a command to send
 * network data to Octeon.
 */
union octnic_cmd_setup {
	struct {
		u32 ifidx:8;
		u32 cksum_offset:7;
		u32 gather:1;
		u32 timestamp:1;
		u32 ipv4opts_ipv6exthdr:2;
		u32 ip_csum:1;
		u32 tnl_csum:1;

		u32 rsvd:11;
		union {
			u32 datasize;
			u32 gatherptrs;
		} u;
	} s;

	u64 u64;

};

struct octnic_ctrl_params {
	u32 resp_order;
};

static inline int octnet_iq_is_full(struct octeon_device *oct, u32 q_no)
{
	return ((u32)atomic_read(&oct->instr_queue[q_no]->instr_pending)
		>= (oct->instr_queue[q_no]->max_count - 2));
}

/** Utility function to prepare a 64B NIC instruction based on a setup command
 * @param cmd - pointer to instruction to be filled in.
 * @param setup - pointer to the setup structure
 * @param q_no - which queue for back pressure
 *
 * Assumes the cmd instruction is pre-allocated, but no fields are filled in.
 */
static inline void
octnet_prepare_pci_cmd(struct octeon_instr_64B *cmd,
		       union octnic_cmd_setup *setup, u32 tag)
{
	struct octeon_instr_ih *ih;
	struct octeon_instr_irh *irh;
	union octnic_packet_params packet_params;

	memset(cmd, 0, sizeof(struct octeon_instr_64B));

	ih = (struct octeon_instr_ih *)&cmd->ih;

	/* assume that rflag is cleared so therefore front data will only have
	 * irh and ossp[1] and ossp[2] for a total of 24 bytes
	 */
	ih->fsz = 24;

	ih->tagtype = ORDERED_TAG;
	ih->grp = DEFAULT_POW_GRP;

	if (tag)
		ih->tag = tag;
	else
		ih->tag = LIO_DATA(setup->s.ifidx);

	ih->raw = 1;
	ih->qos = (setup->s.ifidx & 3) + 4;	/* map qos based on interface */

	if (!setup->s.gather) {
		ih->dlengsz = setup->s.u.datasize;
	} else {
		ih->gather = 1;
		ih->dlengsz = setup->s.u.gatherptrs;
	}

	irh = (struct octeon_instr_irh *)&cmd->irh;

	irh->opcode = OPCODE_NIC;
	irh->subcode = OPCODE_NIC_NW_DATA;

	packet_params.u32 = 0;

	if (setup->s.cksum_offset) {
		packet_params.s.csoffset = setup->s.cksum_offset;
		packet_params.s.ipv4opts_ipv6exthdr =
						setup->s.ipv4opts_ipv6exthdr;
	}

	packet_params.s.ip_csum = setup->s.ip_csum;
	packet_params.s.tnl_csum = setup->s.tnl_csum;
	packet_params.s.ifidx = setup->s.ifidx;
	packet_params.s.tsflag = setup->s.timestamp;

	irh->ossp = packet_params.u32;
}

/** Allocate and a soft command with space for a response immediately following
 * the commnad.
 * @param oct - octeon device pointer
 * @param cmd - pointer to the command structure, pre-filled for everything
 * except the response.
 * @param rdatasize - size in bytes of the response.
 *
 * @returns pointer to allocated buffer with command copied into it, and
 * response space immediately following.
 */
void *
octeon_alloc_soft_command_resp(struct octeon_device    *oct,
			       struct octeon_instr_64B *cmd,
			       size_t		       rdatasize);

/** Send a NIC data packet to the device
 * @param oct - octeon device pointer
 * @param ndata - control structure with queueing, and buffer information
 *
 * @returns IQ_FAILED if it failed to add to the input queue. IQ_STOP if it the
 * queue should be stopped, and IQ_SEND_OK if it sent okay.
 */
int octnet_send_nic_data_pkt(struct octeon_device *oct,
			     struct octnic_data_pkt *ndata, u32 xmit_more);

/** Send a NIC control packet to the device
 * @param oct - octeon device pointer
 * @param nctrl - control structure with command, timout, and callback info
 * @param nparams - response control structure
 *
 * @returns IQ_FAILED if it failed to add to the input queue. IQ_STOP if it the
 * queue should be stopped, and IQ_SEND_OK if it sent okay.
 */
int
octnet_send_nic_ctrl_pkt(struct octeon_device *oct,
			 struct octnic_ctrl_pkt *nctrl,
			 struct octnic_ctrl_params nparams);

#endif
