/*******************************************************************************
 *
 * Intel Ethernet Controller XL710 Family Linux Driver
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

#ifndef _I40E_FCOE_H_
#define _I40E_FCOE_H_

/* FCoE HW context helper macros */
#define I40E_DDP_CONTEXT_DESC(R, i)     \
	(&(((struct i40e_fcoe_ddp_context_desc *)((R)->desc))[i]))

#define I40E_QUEUE_CONTEXT_DESC(R, i)   \
	(&(((struct i40e_fcoe_queue_context_desc *)((R)->desc))[i]))

#define I40E_FILTER_CONTEXT_DESC(R, i)  \
	(&(((struct i40e_fcoe_filter_context_desc *)((R)->desc))[i]))


/* receive queue descriptor filter status for FCoE */
#define I40E_RX_DESC_FLTSTAT_FCMASK	0x3
#define I40E_RX_DESC_FLTSTAT_NOMTCH	0x0	/* no ddp context match */
#define I40E_RX_DESC_FLTSTAT_NODDP	0x1	/* no ddp due to error */
#define I40E_RX_DESC_FLTSTAT_DDP	0x2	/* DDPed payload, post header */
#define I40E_RX_DESC_FLTSTAT_FCPRSP	0x3	/* FCP_RSP */

/* receive queue descriptor error codes for FCoE */
#define I40E_RX_DESC_FCOE_ERROR_MASK		\
	(I40E_RX_DESC_ERROR_L3L4E_PROT |	\
	 I40E_RX_DESC_ERROR_L3L4E_FC |		\
	 I40E_RX_DESC_ERROR_L3L4E_DMAC_ERR |	\
	 I40E_RX_DESC_ERROR_L3L4E_DMAC_WARN)

/* receive queue descriptor programming error */
#define I40E_RX_PROG_FCOE_ERROR_TBL_FULL(e)	\
	(((e) >> I40E_RX_PROG_STATUS_DESC_FCOE_TBL_FULL_SHIFT) & 0x1)

#define I40E_RX_PROG_FCOE_ERROR_CONFLICT(e)	\
	(((e) >> I40E_RX_PROG_STATUS_DESC_FCOE_CONFLICT_SHIFT) & 0x1)

#define I40E_RX_PROG_FCOE_ERROR_TBL_FULL_BIT	\
	(1 << I40E_RX_PROG_STATUS_DESC_FCOE_TBL_FULL_SHIFT)
#define I40E_RX_PROG_FCOE_ERROR_CONFLICT_BIT	\
	(1 << I40E_RX_PROG_STATUS_DESC_FCOE_CONFLICT_SHIFT)

#define I40E_RX_PROG_FCOE_ERROR_INVLFAIL(e)	\
	I40E_RX_PROG_FCOE_ERROR_CONFLICT(e)
#define I40E_RX_PROG_FCOE_ERROR_INVLFAIL_BIT	\
	I40E_RX_PROG_FCOE_ERROR_CONFLICT_BIT

/* FCoE DDP related definitions */
#define I40E_FCOE_MIN_XID	0x0000  /* the min xid supported by fcoe_sw */
#define I40E_FCOE_MAX_XID	0x0FFF  /* the max xid supported by fcoe_sw */
#define I40E_FCOE_DDP_BUFFCNT_MAX	512	/* 9 bits bufcnt */
#define I40E_FCOE_DDP_PTR_ALIGN		16
#define I40E_FCOE_DDP_PTR_MAX	(I40E_FCOE_DDP_BUFFCNT_MAX * sizeof(dma_addr_t))
#define I40E_FCOE_DDP_BUF_MIN	4096
#define I40E_FCOE_DDP_MAX	2048
#define I40E_FCOE_FILTER_CTX_QW1_PCTYPE_SHIFT	8

/* supported netdev features for FCoE */
#define I40E_FCOE_NETIF_FEATURES (NETIF_F_ALL_FCOE | \
	NETIF_F_HW_VLAN_CTAG_TX | \
	NETIF_F_HW_VLAN_CTAG_RX | \
	NETIF_F_HW_VLAN_CTAG_FILTER)

/* DDP context flags */
enum i40e_fcoe_ddp_flags {
	__I40E_FCOE_DDP_NONE = 1,
	__I40E_FCOE_DDP_TARGET,
	__I40E_FCOE_DDP_INITALIZED,
	__I40E_FCOE_DDP_PROGRAMMED,
	__I40E_FCOE_DDP_DONE,
	__I40E_FCOE_DDP_ABORTED,
	__I40E_FCOE_DDP_UNMAPPED,
};

/* DDP SW context struct */
struct i40e_fcoe_ddp {
	int len;
	u16 xid;
	u16 firstoff;
	u16 lastsize;
	u16 list_len;
	u8 fcerr;
	u8 prerr;
	unsigned long flags;
	unsigned int sgc;
	struct scatterlist *sgl;
	dma_addr_t udp;
	u64 *udl;
	struct dma_pool *pool;

};

struct i40e_fcoe_ddp_pool {
	struct dma_pool *pool;
};

struct i40e_fcoe {
	unsigned long mode;
	atomic_t refcnt;
	struct i40e_fcoe_ddp_pool __percpu *ddp_pool;
	struct i40e_fcoe_ddp ddp[I40E_FCOE_DDP_MAX];
};

#endif /* _I40E_FCOE_H_ */
