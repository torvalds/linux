/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Virtual Function Driver
 * Copyright(c) 2013 - 2014 Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * The full GNU General Public License is included in this distribution in
 * the file called "COPYING".
 *
 * Contact Information:
 * e1000-devel Mailing List <e1000-devel@lists.sourceforge.net>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 ******************************************************************************/

#ifndef _I40E_ADMINQ_H_
#define _I40E_ADMINQ_H_

#include "i40e_osdep.h"
#include "i40e_adminq_cmd.h"

#define I40E_ADMINQ_DESC(R, i)   \
	(&(((struct i40e_aq_desc *)((R).desc_buf.va))[i]))

#define I40E_ADMINQ_DESC_ALIGNMENT 4096

struct i40e_adminq_ring {
	struct i40e_virt_mem dma_head;	/* space for dma structures */
	struct i40e_dma_mem desc_buf;	/* descriptor ring memory */
	struct i40e_virt_mem cmd_buf;	/* command buffer memory */

	union {
		struct i40e_dma_mem *asq_bi;
		struct i40e_dma_mem *arq_bi;
	} r;

	u16 count;		/* Number of descriptors */
	u16 rx_buf_len;		/* Admin Receive Queue buffer length */

	/* used for interrupt processing */
	u16 next_to_use;
	u16 next_to_clean;

	/* used for queue tracking */
	u32 head;
	u32 tail;
	u32 len;
};

/* ASQ transaction details */
struct i40e_asq_cmd_details {
	void *callback; /* cast from type I40E_ADMINQ_CALLBACK */
	u64 cookie;
	u16 flags_ena;
	u16 flags_dis;
	bool async;
	bool postpone;
};

#define I40E_ADMINQ_DETAILS(R, i)   \
	(&(((struct i40e_asq_cmd_details *)((R).cmd_buf.va))[i]))

/* ARQ event information */
struct i40e_arq_event_info {
	struct i40e_aq_desc desc;
	u16 msg_size;
	u8 *msg_buf;
};

/* Admin Queue information */
struct i40e_adminq_info {
	struct i40e_adminq_ring arq;    /* receive queue */
	struct i40e_adminq_ring asq;    /* send queue */
	u16 num_arq_entries;            /* receive queue depth */
	u16 num_asq_entries;            /* send queue depth */
	u16 arq_buf_size;               /* receive queue buffer size */
	u16 asq_buf_size;               /* send queue buffer size */
	u16 fw_maj_ver;                 /* firmware major version */
	u16 fw_min_ver;                 /* firmware minor version */
	u16 api_maj_ver;                /* api major version */
	u16 api_min_ver;                /* api minor version */
	bool nvm_busy;

	struct mutex asq_mutex; /* Send queue lock */
	struct mutex arq_mutex; /* Receive queue lock */

	/* last status values on send and receive queues */
	enum i40e_admin_queue_err asq_last_status;
	enum i40e_admin_queue_err arq_last_status;
};

/* general information */
#define I40E_AQ_LARGE_BUF	512
#define I40E_ASQ_CMD_TIMEOUT	100000  /* usecs */

void i40evf_fill_default_direct_cmd_desc(struct i40e_aq_desc *desc,
				       u16 opcode);

#endif /* _I40E_ADMINQ_H_ */
