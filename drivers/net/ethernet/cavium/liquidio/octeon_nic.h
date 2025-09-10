/**********************************************************************
 * Author: Cavium, Inc.
 *
 * Contact: support@cavium.com
 *          Please include "LiquidIO" in the subject.
 *
 * Copyright (c) 2003-2016 Cavium, Inc.
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

	/** Input queue to use to send this command. */
	u64 iq_no;

	/** The network device that issued the control command. */
	u64 netpndev;

	/** Callback function called when the command has been fetched */
	octnic_ctrl_pkt_cb_fn_t cb_fn;

	u32 sc_status;
};

#define MAX_UDD_SIZE(nctrl) (sizeof((nctrl)->udd))

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
	union octeon_instr_64B cmd;

	/** Input queue to use to send this command. */
	u32 q_no;

};

/** Structure passed by NIC module to OSI layer to prepare a command to send
 * network data to Octeon.
 */
union octnic_cmd_setup {
	struct {
		u32 iq_no:8;
		u32 gather:1;
		u32 timestamp:1;
		u32 ip_csum:1;
		u32 transport_csum:1;
		u32 tnl_csum:1;
		u32 rsvd:19;

		union {
			u32 datasize;
			u32 gatherptrs;
		} u;
	} s;

	u64 u64;

};

static inline int octnet_iq_is_full(struct octeon_device *oct, u32 q_no)
{
	return ((u32)atomic_read(&oct->instr_queue[q_no]->instr_pending)
		>= (oct->instr_queue[q_no]->max_count - 2));
}

static inline void
octnet_prepare_pci_cmd_o2(struct octeon_device *oct,
			  union octeon_instr_64B *cmd,
			  union octnic_cmd_setup *setup, u32 tag)
{
	struct octeon_instr_ih2 *ih2;
	struct octeon_instr_irh *irh;
	union octnic_packet_params packet_params;
	int port;

	memset(cmd, 0, sizeof(union octeon_instr_64B));

	ih2 = (struct octeon_instr_ih2 *)&cmd->cmd2.ih2;

	/* assume that rflag is cleared so therefore front data will only have
	 * irh and ossp[0], ossp[1] for a total of 32 bytes
	 */
	ih2->fsz = LIO_PCICMD_O2;

	ih2->tagtype = ORDERED_TAG;
	ih2->grp = DEFAULT_POW_GRP;

	port = (int)oct->instr_queue[setup->s.iq_no]->txpciq.s.port;

	if (tag)
		ih2->tag = tag;
	else
		ih2->tag = LIO_DATA(port);

	ih2->raw = 1;
	ih2->qos = (port & 3) + 4;	/* map qos based on interface */

	if (!setup->s.gather) {
		ih2->dlengsz = setup->s.u.datasize;
	} else {
		ih2->gather = 1;
		ih2->dlengsz = setup->s.u.gatherptrs;
	}

	irh = (struct octeon_instr_irh *)&cmd->cmd2.irh;

	irh->opcode = OPCODE_NIC;
	irh->subcode = OPCODE_NIC_NW_DATA;

	packet_params.u32 = 0;

	packet_params.s.ip_csum = setup->s.ip_csum;
	packet_params.s.transport_csum = setup->s.transport_csum;
	packet_params.s.tnl_csum = setup->s.tnl_csum;
	packet_params.s.tsflag = setup->s.timestamp;

	irh->ossp = packet_params.u32;
}

static inline void
octnet_prepare_pci_cmd_o3(struct octeon_device *oct,
			  union octeon_instr_64B *cmd,
			  union octnic_cmd_setup *setup, u32 tag)
{
	struct octeon_instr_irh *irh;
	struct octeon_instr_ih3     *ih3;
	struct octeon_instr_pki_ih3 *pki_ih3;
	union octnic_packet_params packet_params;
	int port;

	memset(cmd, 0, sizeof(union octeon_instr_64B));

	ih3 = (struct octeon_instr_ih3 *)&cmd->cmd3.ih3;
	pki_ih3 = (struct octeon_instr_pki_ih3 *)&cmd->cmd3.pki_ih3;

	/* assume that rflag is cleared so therefore front data will only have
	 * irh and ossp[1] and ossp[2] for a total of 24 bytes
	 */
	ih3->pkind       = oct->instr_queue[setup->s.iq_no]->txpciq.s.pkind;
	/*PKI IH*/
	ih3->fsz = LIO_PCICMD_O3;

	if (!setup->s.gather) {
		ih3->dlengsz = setup->s.u.datasize;
	} else {
		ih3->gather = 1;
		ih3->dlengsz = setup->s.u.gatherptrs;
	}

	pki_ih3->w       = 1;
	pki_ih3->raw     = 1;
	pki_ih3->utag    = 1;
	pki_ih3->utt     = 1;
	pki_ih3->uqpg    = oct->instr_queue[setup->s.iq_no]->txpciq.s.use_qpg;

	port = (int)oct->instr_queue[setup->s.iq_no]->txpciq.s.port;

	if (tag)
		pki_ih3->tag = tag;
	else
		pki_ih3->tag     = LIO_DATA(port);

	pki_ih3->tagtype = ORDERED_TAG;
	pki_ih3->qpg     = oct->instr_queue[setup->s.iq_no]->txpciq.s.qpg;
	pki_ih3->pm      = 0x7; /*0x7 - meant for Parse nothing, uninterpreted*/
	pki_ih3->sl      = 8;   /* sl will be sizeof(pki_ih3)*/

	irh = (struct octeon_instr_irh *)&cmd->cmd3.irh;

	irh->opcode = OPCODE_NIC;
	irh->subcode = OPCODE_NIC_NW_DATA;

	packet_params.u32 = 0;

	packet_params.s.ip_csum = setup->s.ip_csum;
	packet_params.s.transport_csum = setup->s.transport_csum;
	packet_params.s.tnl_csum = setup->s.tnl_csum;
	packet_params.s.tsflag = setup->s.timestamp;

	irh->ossp = packet_params.u32;
}

/** Utility function to prepare a 64B NIC instruction based on a setup command
 * @param cmd - pointer to instruction to be filled in.
 * @param setup - pointer to the setup structure
 * @param q_no - which queue for back pressure
 *
 * Assumes the cmd instruction is pre-allocated, but no fields are filled in.
 */
static inline void
octnet_prepare_pci_cmd(struct octeon_device *oct, union octeon_instr_64B *cmd,
		       union octnic_cmd_setup *setup, u32 tag)
{
	if (OCTEON_CN6XXX(oct))
		octnet_prepare_pci_cmd_o2(oct, cmd, setup, tag);
	else
		octnet_prepare_pci_cmd_o3(oct, cmd, setup, tag);
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
			       union octeon_instr_64B *cmd,
			       u32		       rdatasize);

/** Send a NIC data packet to the device
 * @param oct - octeon device pointer
 * @param ndata - control structure with queueing, and buffer information
 *
 * @returns IQ_FAILED if it failed to add to the input queue. IQ_STOP if the
 * queue should be stopped, and IQ_SEND_OK if it sent okay.
 */
int octnet_send_nic_data_pkt(struct octeon_device *oct,
			     struct octnic_data_pkt *ndata,
			     int xmit_more);

/** Send a NIC control packet to the device
 * @param oct - octeon device pointer
 * @param nctrl - control structure with command, timout, and callback info
 * @returns IQ_FAILED if it failed to add to the input queue. IQ_STOP if the
 * queue should be stopped, and IQ_SEND_OK if it sent okay.
 */
int
octnet_send_nic_ctrl_pkt(struct octeon_device *oct,
			 struct octnic_ctrl_pkt *nctrl);

#endif
