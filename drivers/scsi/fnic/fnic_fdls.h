/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright 2008 Cisco Systems, Inc.  All rights reserved.
 * Copyright 2007 Nuova Systems, Inc.  All rights reserved.
 */

#ifndef _FNIC_FDLS_H_
#define _FNIC_FDLS_H_

#include "fnic_stats.h"
#include "fdls_fc.h"

/* FDLS - Fabric discovery and login services
 * -> VLAN discovery
 *   -> retry every retry delay seconds until it succeeds.
 *                        <- List of VLANs
 *
 * -> Solicitation
 *                        <- Solicitation response (Advertisement)
 *
 * -> FCF selection & FLOGI ( FLOGI timeout - 2 * E_D_TOV)
 *                        <- FLOGI response
 *
 * -> FCF keep alive
 *                         <- FCF keep alive
 *
 * -> PLOGI to FFFFFC (DNS) (PLOGI timeout - 2 * R_A_TOV)
 *    -> ABTS if timeout (ABTS tomeout - 2 * R_A_TOV)
 *                        <- PLOGI response
 *    -> Retry PLOGI to FFFFFC (DNS) - Number of retries from vnic.cfg
 *
 * -> SCR to FFFFFC (DNS) (SCR timeout - 2 * R_A_TOV)
 *    -> ABTS if timeout (ABTS tomeout - 2 * R_A_TOV)
 *                        <- SCR response
 *    -> Retry SCR - Number of retries 2
 *
 * -> GPN_FT to FFFFFC (GPN_FT timeout - 2 * R_A_TOV)a
 *    -> Retry on BUSY until it succeeds
 *    -> Retry on BUSY until it succeeds
 *    -> 2 retries on timeout
 *
 * -> RFT_ID to FFFFFC (DNS)        (RFT_ID timeout - 3 * R_A_TOV)
 *    -> ABTS if timeout (ABTS tomeout - 2 * R_A_TOV)
 *    -> Retry RFT_ID to FFFFFC (DNS) (Number of retries 2 )
 *    -> Ignore if both retires fail.
 *
 *        Session establishment with targets
 * For each PWWN
 *   -> PLOGI to FCID of that PWWN (PLOGI timeout 2 * R_A_TOV)
 *    -> ABTS if timeout (ABTS tomeout - 2 * R_A_TOV)
 *                        <- PLOGI response
 *    -> Retry PLOGI. Num retries using vnic.cfg
 *
 *   -> PRLI to FCID of that PWWN (PRLI timeout 2 * R_A_TOV)
 *    -> ABTS if timeout (ABTS tomeout - 2 * R_A_TOV)
 *                        <- PRLI response
 *    -> Retry PRLI. Num retries using vnic.cfg
 *
 */

#define FDLS_RETRY_COUNT 2

/*
 * OXID encoding:
 * bits 0-8: oxid idx - allocated from poool
 * bits 9-13: oxid frame code from fnic_oxid_frame_type_e
 * bits 14-15: all zeros
 */
#define FNIC_OXID_POOL_SZ	(512)	/* always power of 2 */
#define FNIC_OXID_ENCODE(idx, frame_type)	(frame_type | idx)
#define FNIC_FRAME_MASK				0xFE00
#define FNIC_FRAME_TYPE(oxid)			(oxid & FNIC_FRAME_MASK)
#define FNIC_OXID_IDX(oxid)		((oxid) & (FNIC_OXID_POOL_SZ - 1))

#define OXID_RECLAIM_TOV(iport) (2 * iport->r_a_tov) /* in milliseconds */

#define FNIC_FDLS_FABRIC_ABORT_ISSUED     0x1
#define FNIC_FDLS_FPMA_LEARNT             0x2

/* tport flags */
#define FNIC_FDLS_TPORT_IN_GPN_FT_LIST 0x1
#define FNIC_FDLS_TGT_ABORT_ISSUED     0x2
#define FNIC_FDLS_TPORT_SEND_ADISC     0x4
#define FNIC_FDLS_RETRY_FRAME          0x8
#define FNIC_FDLS_TPORT_BUSY	       0x10
#define FNIC_FDLS_TPORT_TERMINATING      0x20
#define FNIC_FDLS_TPORT_DELETED        0x40
#define FNIC_FDLS_SCSI_REGISTERED      0x200

/* Retry supported by rport(returned by prli service parameters) */
#define FDLS_FC_RP_FLAGS_RETRY 0x1

#define fdls_set_state(_fdls_fabric, _state)  ((_fdls_fabric)->state = _state)
#define fdls_get_state(_fdls_fabric)          ((_fdls_fabric)->state)

#define FNIC_FDMI_ACTIVE    0x8
#define FNIC_FIRST_LINK_UP    0x2

#define fdls_set_tport_state(_tport, _state)    (_tport->state = _state)
#define fdls_get_tport_state(_tport)            (_tport->state)

#define FNIC_PORTSPEED_10GBIT   1
#define FNIC_FRAME_HT_ROOM     (2148)
#define FNIC_FCOE_FRAME_MAXSZ   (2112)


#define FNIC_FRAME_TYPE_FABRIC_FLOGI	0x1000
#define FNIC_FRAME_TYPE_FABRIC_PLOGI	0x1200
#define FNIC_FRAME_TYPE_FABRIC_RPN	0x1400
#define FNIC_FRAME_TYPE_FABRIC_RFT	0x1600
#define FNIC_FRAME_TYPE_FABRIC_RFF	0x1800
#define FNIC_FRAME_TYPE_FABRIC_SCR	0x1A00
#define FNIC_FRAME_TYPE_FABRIC_GPN_FT	0x1C00
#define FNIC_FRAME_TYPE_FABRIC_LOGO	0x1E00
#define FNIC_FRAME_TYPE_FDMI_PLOGI	0x2000
#define FNIC_FRAME_TYPE_FDMI_RHBA	0x2200
#define FNIC_FRAME_TYPE_FDMI_RPA	0x2400
#define FNIC_FRAME_TYPE_TGT_PLOGI	0x2600
#define FNIC_FRAME_TYPE_TGT_PRLI	0x2800
#define FNIC_FRAME_TYPE_TGT_ADISC	0x2A00
#define FNIC_FRAME_TYPE_TGT_LOGO	0x2C00

struct fnic_fip_fcf_s {
	uint16_t vlan_id;
	uint8_t fcf_mac[6];
	uint8_t fcf_priority;
	uint32_t fka_adv_period;
	uint8_t ka_disabled;
};

enum fnic_fdls_state_e {
	FDLS_STATE_INIT = 0,
	FDLS_STATE_LINKDOWN,
	FDLS_STATE_FABRIC_LOGO,
	FDLS_STATE_FLOGO_DONE,
	FDLS_STATE_FABRIC_FLOGI,
	FDLS_STATE_FABRIC_PLOGI,
	FDLS_STATE_RPN_ID,
	FDLS_STATE_REGISTER_FC4_TYPES,
	FDLS_STATE_REGISTER_FC4_FEATURES,
	FDLS_STATE_SCR,
	FDLS_STATE_GPN_FT,
	FDLS_STATE_TGT_DISCOVERY,
	FDLS_STATE_RSCN_GPN_FT,
	FDLS_STATE_SEND_GPNFT
};

struct fnic_fdls_fabric_s {
	enum fnic_fdls_state_e state;
	uint32_t flags;
	struct list_head tport_list; /* List of discovered tports */
	struct timer_list retry_timer;
	int del_timer_inprogress;
	int del_fdmi_timer_inprogress;
	int retry_counter;
	int timer_pending;
	int fdmi_retry;
	struct timer_list fdmi_timer;
	int fdmi_pending;
};

struct fnic_fdls_fip_s {
	uint32_t state;
	uint32_t flogi_retry;
};

/* Message to tport_event_handler */
enum fnic_tgt_msg_id {
	TGT_EV_NONE = 0,
	TGT_EV_RPORT_ADD,
	TGT_EV_RPORT_DEL,
	TGT_EV_TPORT_DELETE,
	TGT_EV_REMOVE
};

struct fnic_tport_event_s {
	struct list_head links;
	enum fnic_tgt_msg_id event;
	void *arg1;
};

enum fdls_tgt_state_e {
	FDLS_TGT_STATE_INIT = 0,
	FDLS_TGT_STATE_PLOGI,
	FDLS_TGT_STATE_PRLI,
	FDLS_TGT_STATE_READY,
	FDLS_TGT_STATE_LOGO_RECEIVED,
	FDLS_TGT_STATE_ADISC,
	FDL_TGT_STATE_PLOGO,
	FDLS_TGT_STATE_OFFLINING,
	FDLS_TGT_STATE_OFFLINE
};

struct fnic_tport_s {
	struct list_head links; /* To link the tports */
	enum fdls_tgt_state_e state;
	uint32_t flags;
	uint32_t fcid;
	uint64_t wwpn;
	uint64_t wwnn;
	uint16_t active_oxid;
	uint16_t tgt_flags;
	atomic_t in_flight; /* io counter */
	uint16_t max_payload_size;
	uint16_t r_a_tov;
	uint16_t e_d_tov;
	uint16_t lun0_delay;
	int max_concur_seqs;
	uint32_t fcp_csp;
	struct timer_list retry_timer;
	int del_timer_inprogress;
	int retry_counter;
	int timer_pending;
	unsigned int num_pending_cmds;
	int nexus_restart_count;
	int exch_reset_in_progress;
	void *iport;
	struct work_struct tport_del_work;
	struct completion *tport_del_done;
	struct fc_rport *rport;
	char str_wwpn[20];
	char str_wwnn[20];
};

/* OXID pool related structures */
struct reclaim_entry_s {
	struct list_head links;
	/* oxid that needs to be freed after 2*r_a_tov */
	uint16_t oxid_idx;
	/* in jiffies. Use this to waiting time */
	unsigned long expires;
	unsigned long *bitmap;
};

/* used for allocating oxids for fabric and fdmi requests */
struct fnic_oxid_pool_s {
	DECLARE_BITMAP(bitmap, FNIC_OXID_POOL_SZ);
	int sz;			/* size of the pool or block */
	int next_idx;		/* used for cycling through the oxid pool */

	/* retry schedule free */
	DECLARE_BITMAP(pending_schedule_free, FNIC_OXID_POOL_SZ);
	struct delayed_work schedule_oxid_free_retry;

	/* List of oxids that need to be freed and reclaimed.
	 * This list is shared by all the oxid pools
	 */
	struct list_head oxid_reclaim_list;
	/* Work associated with reclaim list */
	struct delayed_work oxid_reclaim_work;
};

/* iport */
enum fnic_iport_state_e {
	FNIC_IPORT_STATE_INIT = 0,
	FNIC_IPORT_STATE_LINK_WAIT,
	FNIC_IPORT_STATE_FIP,
	FNIC_IPORT_STATE_FABRIC_DISC,
	FNIC_IPORT_STATE_READY
};

struct fnic_iport_s {
	enum fnic_iport_state_e state;
	struct fnic *fnic;
	uint64_t boot_time;
	uint32_t flags;
	int usefip;
	uint8_t hwmac[6]; /* HW MAC Addr */
	uint8_t fpma[6]; /* Fabric Provided MA */
	uint8_t fcfmac[6]; /* MAC addr of Fabric */
	uint16_t vlan_id;
	uint32_t fcid;

	/* oxid pool */
	struct fnic_oxid_pool_s oxid_pool;

	/*
	 * fabric reqs are serialized and only one req at a time.
	 * Tracking the oxid for sending abort
	 */
	uint16_t active_oxid_fabric_req;
	/* fdmi only */
	uint16_t active_oxid_fdmi_plogi;
	uint16_t active_oxid_fdmi_rhba;
	uint16_t active_oxid_fdmi_rpa;

	struct fnic_fip_fcf_s selected_fcf;
	struct fnic_fdls_fip_s fip;
	struct fnic_fdls_fabric_s fabric;
	struct list_head tport_list;
	struct list_head tport_list_pending_del;
	/* list of tports for which we are yet to send PLOGO */
	struct list_head inprocess_tport_list;
	struct list_head deleted_tport_list;
	struct work_struct tport_event_work;
	uint32_t e_d_tov; /* msec */
	uint32_t r_a_tov; /* msec */
	uint32_t link_supported_speeds;
	uint32_t max_flogi_retries;
	uint32_t max_plogi_retries;
	uint32_t plogi_timeout;
	uint32_t service_params;
	uint64_t wwpn;
	uint64_t wwnn;
	uint16_t max_payload_size;
	spinlock_t deleted_tport_lst_lock;
	struct completion *flogi_reg_done;
	struct fnic_iport_stats iport_stats;
	char str_wwpn[20];
	char str_wwnn[20];
};

struct rport_dd_data_s {
	struct fnic_tport_s *tport;
	struct fnic_iport_s *iport;
};

enum fnic_recv_frame_type_e {
	FNIC_FABRIC_FLOGI_RSP = 1,
	FNIC_FABRIC_PLOGI_RSP,
	FNIC_FABRIC_RPN_RSP,
	FNIC_FABRIC_RFT_RSP,
	FNIC_FABRIC_RFF_RSP,
	FNIC_FABRIC_SCR_RSP,
	FNIC_FABRIC_GPN_FT_RSP,
	FNIC_FABRIC_BLS_ABTS_RSP,
	FNIC_FDMI_PLOGI_RSP,
	FNIC_FDMI_REG_HBA_RSP,
	FNIC_FDMI_RPA_RSP,
	FNIC_FDMI_BLS_ABTS_RSP,
	FNIC_FABRIC_LOGO_RSP,

	/* responses to target requests */
	FNIC_TPORT_PLOGI_RSP,
	FNIC_TPORT_PRLI_RSP,
	FNIC_TPORT_ADISC_RSP,
	FNIC_TPORT_BLS_ABTS_RSP,
	FNIC_TPORT_LOGO_RSP,

	/* unsolicited requests */
	FNIC_BLS_ABTS_REQ,
	FNIC_ELS_PLOGI_REQ,
	FNIC_ELS_RSCN_REQ,
	FNIC_ELS_LOGO_REQ,
	FNIC_ELS_ECHO_REQ,
	FNIC_ELS_ADISC,
	FNIC_ELS_RLS,
	FNIC_ELS_RRQ,
	FNIC_ELS_UNSUPPORTED_REQ,
};

enum fnic_port_speeds {
	DCEM_PORTSPEED_NONE = 0,
	DCEM_PORTSPEED_1G = 1000,
	DCEM_PORTSPEED_2G = 2000,
	DCEM_PORTSPEED_4G = 4000,
	DCEM_PORTSPEED_8G = 8000,
	DCEM_PORTSPEED_10G = 10000,
	DCEM_PORTSPEED_16G = 16000,
	DCEM_PORTSPEED_20G = 20000,
	DCEM_PORTSPEED_25G = 25000,
	DCEM_PORTSPEED_32G = 32000,
	DCEM_PORTSPEED_40G = 40000,
	DCEM_PORTSPEED_4x10G = 41000,
	DCEM_PORTSPEED_50G = 50000,
	DCEM_PORTSPEED_64G = 64000,
	DCEM_PORTSPEED_100G = 100000,
	DCEM_PORTSPEED_128G = 128000,
};

/* Function Declarations */
/* fdls_disc.c */
void fnic_fdls_disc_init(struct fnic_iport_s *iport);
void fnic_fdls_disc_start(struct fnic_iport_s *iport);
void fnic_fdls_recv_frame(struct fnic_iport_s *iport, void *rx_frame,
			  int len, int fchdr_offset);
void fnic_fdls_link_down(struct fnic_iport_s *iport);
int fdls_init_frame_pool(struct fnic_iport_s *iport);
uint8_t *fdls_alloc_frame(struct fnic_iport_s *iport);
uint16_t fdls_alloc_oxid(struct fnic_iport_s *iport, int oxid_frame_type,
	uint16_t *active_oxid);
void fdls_free_oxid(struct fnic_iport_s *iport,
	uint16_t oxid, uint16_t *active_oxid);
void fdls_tgt_logout(struct fnic_iport_s *iport,
		     struct fnic_tport_s *tport);
void fnic_del_fabric_timer_sync(struct fnic *fnic);
void fnic_del_tport_timer_sync(struct fnic *fnic,
							struct fnic_tport_s *tport);
void fdls_send_fabric_logo(struct fnic_iport_s *iport);
int fnic_fdls_validate_and_get_frame_type(struct fnic_iport_s *iport,
	struct fc_frame_header *fchdr);
void fdls_send_tport_abts(struct fnic_iport_s *iport,
						struct fnic_tport_s *tport);
bool fdls_delete_tport(struct fnic_iport_s *iport,
		       struct fnic_tport_s *tport);
void fdls_fdmi_timer_callback(struct timer_list *t);
void fdls_fdmi_retry_plogi(struct fnic_iport_s *iport);

/* fnic_fcs.c */
void fnic_fdls_init(struct fnic *fnic, int usefip);
void fnic_send_fcoe_frame(struct fnic_iport_s *iport, void *frame,
	int frame_size);
void fnic_fcoe_send_vlan_req(struct fnic *fnic);
int fnic_send_fip_frame(struct fnic_iport_s *iport,
	void *frame, int frame_size);
void fnic_fdls_learn_fcoe_macs(struct fnic_iport_s *iport, void *rx_frame,
	uint8_t *fcid);
void fnic_fdls_add_tport(struct fnic_iport_s *iport,
		struct fnic_tport_s *tport, unsigned long flags);
void fnic_fdls_remove_tport(struct fnic_iport_s *iport,
			    struct fnic_tport_s *tport,
			    unsigned long flags);

/* fip.c */
void fnic_fcoe_send_vlan_req(struct fnic *fnic);
void fnic_common_fip_cleanup(struct fnic *fnic);
int fdls_fip_recv_frame(struct fnic *fnic, void *frame);
void fnic_handle_fcs_ka_timer(struct timer_list *t);
void fnic_handle_enode_ka_timer(struct timer_list *t);
void fnic_handle_vn_ka_timer(struct timer_list *t);
void fnic_handle_fip_timer(struct timer_list *t);
extern void fdls_fabric_timer_callback(struct timer_list *t);

/* fnic_scsi.c */
void fnic_scsi_fcpio_reset(struct fnic *fnic);
extern void fdls_fabric_timer_callback(struct timer_list *t);
void fnic_rport_exch_reset(struct fnic *fnic, u32 fcid);
int fnic_fdls_register_portid(struct fnic_iport_s *iport, u32 port_id,
		void *fp);
struct fnic_tport_s *fnic_find_tport_by_fcid(struct fnic_iport_s *iport,
		uint32_t fcid);
struct fnic_tport_s *fnic_find_tport_by_wwpn(struct fnic_iport_s *iport,
		uint64_t  wwpn);

#endif /* _FNIC_FDLS_H_ */
