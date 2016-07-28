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
#include <linux/interrupt.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include "liquidio_common.h"
#include "octeon_droq.h"
#include "octeon_iq.h"
#include "response_manager.h"
#include "octeon_device.h"
#include "octeon_nic.h"
#include "octeon_main.h"

void *
octeon_alloc_soft_command_resp(struct octeon_device    *oct,
			       union octeon_instr_64B *cmd,
			       u32		       rdatasize)
{
	struct octeon_soft_command *sc;
	struct octeon_instr_ih2  *ih2;
	struct octeon_instr_irh *irh;
	struct octeon_instr_rdp *rdp;

	sc = (struct octeon_soft_command *)
		octeon_alloc_soft_command(oct, 0, rdatasize, 0);

	if (!sc)
		return NULL;

	/* Copy existing command structure into the soft command */
	memcpy(&sc->cmd, cmd, sizeof(union octeon_instr_64B));

	/* Add in the response related fields. Opcode and Param are already
	 * there.
	 */
	ih2      = (struct octeon_instr_ih2 *)&sc->cmd.cmd2.ih2;
	rdp     = (struct octeon_instr_rdp *)&sc->cmd.cmd2.rdp;
	irh     = (struct octeon_instr_irh *)&sc->cmd.cmd2.irh;
	ih2->fsz = 40; /* irh + ossp[0] + ossp[1] + rdp + rptr = 40 bytes */

	irh->rflag = 1; /* a response is required */

	rdp->pcie_port = oct->pcie_port;
	rdp->rlen      = rdatasize;

	*sc->status_word = COMPLETION_WORD_INIT;

	sc->cmd.cmd2.rptr =  sc->dmarptr;

	sc->wait_time = 1000;
	sc->timeout = jiffies + sc->wait_time;

	return sc;
}

int octnet_send_nic_data_pkt(struct octeon_device *oct,
			     struct octnic_data_pkt *ndata,
			     u32 xmit_more)
{
	int ring_doorbell;

	ring_doorbell = !xmit_more;

	return octeon_send_command(oct, ndata->q_no, ring_doorbell, &ndata->cmd,
				   ndata->buf, ndata->datasize,
				   ndata->reqtype);
}

static void octnet_link_ctrl_callback(struct octeon_device *oct,
				      u32 status,
				      void *sc_ptr)
{
	struct octeon_soft_command *sc = (struct octeon_soft_command *)sc_ptr;
	struct octnic_ctrl_pkt *nctrl;

	nctrl = (struct octnic_ctrl_pkt *)sc->ctxptr;

	/* Call the callback function if status is OK.
	 * Status is OK only if a response was expected and core returned
	 * success.
	 * If no response was expected, status is OK if the command was posted
	 * successfully.
	 */
	if (!status && nctrl->cb_fn)
		nctrl->cb_fn(nctrl);

	octeon_free_soft_command(oct, sc);
}

static inline struct octeon_soft_command
*octnic_alloc_ctrl_pkt_sc(struct octeon_device *oct,
			  struct octnic_ctrl_pkt *nctrl)
{
	struct octeon_soft_command *sc = NULL;
	u8 *data;
	u32 rdatasize;
	u32 uddsize = 0, datasize = 0;

	uddsize = (u32)(nctrl->ncmd.s.more * 8);

	datasize = OCTNET_CMD_SIZE + uddsize;
	rdatasize = (nctrl->wait_time) ? 16 : 0;

	sc = (struct octeon_soft_command *)
		octeon_alloc_soft_command(oct, datasize, rdatasize,
					  sizeof(struct octnic_ctrl_pkt));

	if (!sc)
		return NULL;

	memcpy(sc->ctxptr, nctrl, sizeof(struct octnic_ctrl_pkt));

	data = (u8 *)sc->virtdptr;

	memcpy(data, &nctrl->ncmd, OCTNET_CMD_SIZE);

	octeon_swap_8B_data((u64 *)data, (OCTNET_CMD_SIZE >> 3));

	if (uddsize) {
		/* Endian-Swap for UDD should have been done by caller. */
		memcpy(data + OCTNET_CMD_SIZE, nctrl->udd, uddsize);
	}

	sc->iq_no = (u32)nctrl->iq_no;

	octeon_prepare_soft_command(oct, sc, OPCODE_NIC, OPCODE_NIC_CMD,
				    0, 0, 0);

	sc->callback = octnet_link_ctrl_callback;
	sc->callback_arg = sc;
	sc->wait_time = nctrl->wait_time;

	return sc;
}

int
octnet_send_nic_ctrl_pkt(struct octeon_device *oct,
			 struct octnic_ctrl_pkt *nctrl)
{
	int retval;
	struct octeon_soft_command *sc = NULL;

	spin_lock_bh(&oct->cmd_resp_wqlock);
	/* Allow only rx ctrl command to stop traffic on the chip
	 * during offline operations
	 */
	if ((oct->cmd_resp_state == OCT_DRV_OFFLINE) &&
	    (nctrl->ncmd.s.cmd != OCTNET_CMD_RX_CTL)) {
		spin_unlock_bh(&oct->cmd_resp_wqlock);
		dev_err(&oct->pci_dev->dev,
			"%s cmd:%d not processed since driver offline\n",
			__func__, nctrl->ncmd.s.cmd);
		return -1;
	}

	sc = octnic_alloc_ctrl_pkt_sc(oct, nctrl);
	if (!sc) {
		dev_err(&oct->pci_dev->dev, "%s soft command alloc failed\n",
			__func__);
		spin_unlock_bh(&oct->cmd_resp_wqlock);
		return -1;
	}

	retval = octeon_send_soft_command(oct, sc);
	if (retval == IQ_SEND_FAILED) {
		octeon_free_soft_command(oct, sc);
		dev_err(&oct->pci_dev->dev, "%s soft command:%d send failed status: %x\n",
			__func__, nctrl->ncmd.s.cmd, retval);
		spin_unlock_bh(&oct->cmd_resp_wqlock);
		return -1;
	}

	spin_unlock_bh(&oct->cmd_resp_wqlock);
	return retval;
}
