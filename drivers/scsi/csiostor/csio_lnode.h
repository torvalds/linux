/*
 * This file is part of the Chelsio FCoE driver for Linux.
 *
 * Copyright (c) 2008-2012 Chelsio Communications, Inc. All rights reserved.
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
 */

#ifndef __CSIO_LNODE_H__
#define __CSIO_LNODE_H__

#include <linux/kref.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <scsi/fc/fc_els.h>


#include "csio_defs.h"
#include "csio_hw.h"

#define CSIO_FCOE_MAX_NPIV	128
#define CSIO_FCOE_MAX_RNODES	2048

/* FDMI port attribute unknown speed */
#define CSIO_HBA_PORTSPEED_UNKNOWN	0x8000

extern int csio_fcoe_rnodes;
extern int csio_fdmi_enable;

struct csio_fcf_info {
	struct list_head	list;
	uint8_t			priority;
	uint8_t			mac[6];
	uint8_t			name_id[8];
	uint8_t			fabric[8];
	uint16_t		vf_id;
	uint8_t			vlan_id;
	uint16_t		max_fcoe_size;
	uint8_t			fc_map[3];
	uint32_t		fka_adv;
	uint32_t		fcfi;
	uint8_t			get_next:1;
	uint8_t			link_aff:1;
	uint8_t			fpma:1;
	uint8_t			spma:1;
	uint8_t			login:1;
	uint8_t			portid;
	uint8_t			spma_mac[6];
	struct kref		kref;
};

/* Defines for flags */
#define	CSIO_LNF_FIPSUPP		0x00000001	/* Fip Supported */
#define	CSIO_LNF_NPIVSUPP		0x00000002	/* NPIV supported */
#define CSIO_LNF_LINK_ENABLE		0x00000004	/* Link enabled */
#define	CSIO_LNF_FDMI_ENABLE		0x00000008	/* FDMI support */

/* Transport events */
enum csio_ln_fc_evt {
	CSIO_LN_FC_LINKUP = 1,
	CSIO_LN_FC_LINKDOWN,
	CSIO_LN_FC_RSCN,
	CSIO_LN_FC_ATTRIB_UPDATE,
};

/* Lnode stats */
struct csio_lnode_stats {
	uint32_t	n_link_up;	/* Link down */
	uint32_t	n_link_down;	/* Link up */
	uint32_t	n_err;		/* error */
	uint32_t	n_err_nomem;	/* memory not available */
	uint32_t	n_inval_parm;   /* Invalid parameters */
	uint32_t	n_evt_unexp;	/* unexpected event */
	uint32_t	n_evt_drop;	/* dropped event */
	uint32_t	n_rnode_match;  /* matched rnode */
	uint32_t	n_dev_loss_tmo; /* Device loss timeout */
	uint32_t	n_fdmi_err;	/* fdmi err */
	uint32_t	n_evt_fw[PROTO_ERR_IMPL_LOGO + 1];	/* fw events */
	enum csio_ln_ev	n_evt_sm[CSIO_LNE_MAX_EVENT];	/* State m/c events */
	uint32_t	n_rnode_alloc;	/* rnode allocated */
	uint32_t	n_rnode_free;	/* rnode freed */
	uint32_t	n_rnode_nomem;	/* rnode alloc failure */
	uint32_t        n_input_requests; /* Input Requests */
	uint32_t        n_output_requests; /* Output Requests */
	uint32_t        n_control_requests; /* Control Requests */
	uint32_t        n_input_bytes; /* Input Bytes */
	uint32_t        n_output_bytes; /* Output Bytes */
	uint32_t	rsvd1;
};

/* Common Lnode params */
struct csio_lnode_params {
	uint32_t	ra_tov;
	uint32_t	fcfi;
	uint32_t	log_level;	/* Module level for debugging */
};

struct csio_service_parms {
	struct fc_els_csp	csp;		/* Common service parms */
	uint8_t			wwpn[8];	/* WWPN */
	uint8_t			wwnn[8];	/* WWNN */
	struct fc_els_cssp	clsp[4];	/* Class service params */
	uint8_t			vvl[16];	/* Vendor version level */
};

/* Lnode */
struct csio_lnode {
	struct csio_sm		sm;		/* State machine + sibling
						 * lnode list.
						 */
	struct csio_hw		*hwp;		/* Pointer to the HW module */
	uint8_t			portid;		/* Port ID */
	uint8_t			rsvd1;
	uint16_t		rsvd2;
	uint32_t		dev_num;	/* Device number */
	uint32_t		flags;		/* Flags */
	struct list_head	fcf_lsthead;	/* FCF entries */
	struct csio_fcf_info	*fcfinfo;	/* FCF in use */
	struct csio_ioreq	*mgmt_req;	/* MGMT request */

	/* FCoE identifiers */
	uint8_t			mac[6];
	uint32_t		nport_id;
	struct csio_service_parms ln_sparm;	/* Service parms */

	/* Firmware identifiers */
	uint32_t		fcf_flowid;	/*fcf flowid */
	uint32_t		vnp_flowid;
	uint16_t		ssn_cnt;	/* Registered Session */
	uint8_t			cur_evt;	/* Current event */
	uint8_t			prev_evt;	/* Previous event */

	/* Children */
	struct list_head	cln_head;	/* Head of the children lnode
						 * list.
						 */
	uint32_t		num_vports;	/* Total NPIV/children LNodes*/
	struct csio_lnode	*pln;		/* Parent lnode of child
						 * lnodes.
						 */
	struct list_head	cmpl_q;		/* Pending I/Os on this lnode */

	/* Remote node information */
	struct list_head	rnhead;		/* Head of rnode list */
	uint32_t		num_reg_rnodes;	/* Number of rnodes registered
						 * with the host.
						 */
	uint32_t		n_scsi_tgts;	/* Number of scsi targets
						 * found
						 */
	uint32_t		last_scan_ntgts;/* Number of scsi targets
						 * found per last scan.
						 */
	uint32_t		tgt_scan_tick;	/* timer started after
						 * new tgt found
						 */
	/* FC transport data */
	struct fc_vport		*fc_vport;
	struct fc_host_statistics fch_stats;

	struct csio_lnode_stats stats;		/* Common lnode stats */
	struct csio_lnode_params params;	/* Common lnode params */
};

#define	csio_lnode_to_hw(ln)	((ln)->hwp)
#define csio_root_lnode(ln)	(csio_lnode_to_hw((ln))->rln)
#define csio_parent_lnode(ln)	((ln)->pln)
#define	csio_ln_flowid(ln)	((ln)->vnp_flowid)
#define csio_ln_wwpn(ln)	((ln)->ln_sparm.wwpn)
#define csio_ln_wwnn(ln)	((ln)->ln_sparm.wwnn)

#define csio_is_root_ln(ln)	(((ln) == csio_root_lnode((ln))) ? 1 : 0)
#define csio_is_phys_ln(ln)	(((ln)->pln == NULL) ? 1 : 0)
#define csio_is_npiv_ln(ln)	(((ln)->pln != NULL) ? 1 : 0)


#define csio_ln_dbg(_ln, _fmt, ...)	\
	csio_dbg(_ln->hwp, "%x:%x "_fmt, CSIO_DEVID_HI(_ln), \
		 CSIO_DEVID_LO(_ln), ##__VA_ARGS__);

#define csio_ln_err(_ln, _fmt, ...)	\
	csio_err(_ln->hwp, "%x:%x "_fmt, CSIO_DEVID_HI(_ln), \
		 CSIO_DEVID_LO(_ln), ##__VA_ARGS__);

#define csio_ln_warn(_ln, _fmt, ...)	\
	csio_warn(_ln->hwp, "%x:%x "_fmt, CSIO_DEVID_HI(_ln), \
		 CSIO_DEVID_LO(_ln), ##__VA_ARGS__);

/* HW->Lnode notifications */
enum csio_ln_notify {
	CSIO_LN_NOTIFY_HWREADY = 1,
	CSIO_LN_NOTIFY_HWSTOP,
	CSIO_LN_NOTIFY_HWREMOVE,
	CSIO_LN_NOTIFY_HWRESET,
};

void csio_fcoe_fwevt_handler(struct csio_hw *,  __u8 cpl_op, __be64 *);
int csio_is_lnode_ready(struct csio_lnode *);
void csio_lnode_state_to_str(struct csio_lnode *ln, int8_t *str);
struct csio_lnode *csio_lnode_lookup_by_wwpn(struct csio_hw *, uint8_t *);
int csio_get_phy_port_stats(struct csio_hw *, uint8_t ,
				      struct fw_fcoe_port_stats *);
int csio_scan_done(struct csio_lnode *, unsigned long, unsigned long,
		   unsigned long, unsigned long);
void csio_notify_lnodes(struct csio_hw *, enum csio_ln_notify);
void csio_disable_lnodes(struct csio_hw *, uint8_t, bool);
void csio_lnode_async_event(struct csio_lnode *, enum csio_ln_fc_evt);
int csio_ln_fdmi_start(struct csio_lnode *, void *);
int csio_lnode_start(struct csio_lnode *);
void csio_lnode_stop(struct csio_lnode *);
void csio_lnode_close(struct csio_lnode *);
int csio_lnode_init(struct csio_lnode *, struct csio_hw *,
			      struct csio_lnode *);
void csio_lnode_exit(struct csio_lnode *);

#endif /* ifndef __CSIO_LNODE_H__ */
