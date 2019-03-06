/* SPDX-License-Identifier: (GPL-2.0+ OR BSD-3-Clause) */
/*
 * Copyright (C) 2014-2016 Freescale Semiconductor, Inc.
 * Copyright 2016 NXP
 *
 */
#ifndef __FSL_QBMAN_PORTAL_H
#define __FSL_QBMAN_PORTAL_H

#include <soc/fsl/dpaa2-fd.h>

struct dpaa2_dq;
struct qbman_swp;

/* qbman software portal descriptor structure */
struct qbman_swp_desc {
	void *cena_bar; /* Cache-enabled portal base address */
	void __iomem *cinh_bar; /* Cache-inhibited portal base address */
	u32 qman_version;
};

#define QBMAN_SWP_INTERRUPT_EQRI 0x01
#define QBMAN_SWP_INTERRUPT_EQDI 0x02
#define QBMAN_SWP_INTERRUPT_DQRI 0x04
#define QBMAN_SWP_INTERRUPT_RCRI 0x08
#define QBMAN_SWP_INTERRUPT_RCDI 0x10
#define QBMAN_SWP_INTERRUPT_VDCI 0x20

/* the structure for pull dequeue descriptor */
struct qbman_pull_desc {
	u8 verb;
	u8 numf;
	u8 tok;
	u8 reserved;
	__le32 dq_src;
	__le64 rsp_addr;
	u64 rsp_addr_virt;
	u8 padding[40];
};

enum qbman_pull_type_e {
	/* dequeue with priority precedence, respect intra-class scheduling */
	qbman_pull_type_prio = 1,
	/* dequeue with active FQ precedence, respect ICS */
	qbman_pull_type_active,
	/* dequeue with active FQ precedence, no ICS */
	qbman_pull_type_active_noics
};

/* Definitions for parsing dequeue entries */
#define QBMAN_RESULT_MASK      0x7f
#define QBMAN_RESULT_DQ        0x60
#define QBMAN_RESULT_FQRN      0x21
#define QBMAN_RESULT_FQRNI     0x22
#define QBMAN_RESULT_FQPN      0x24
#define QBMAN_RESULT_FQDAN     0x25
#define QBMAN_RESULT_CDAN      0x26
#define QBMAN_RESULT_CSCN_MEM  0x27
#define QBMAN_RESULT_CGCU      0x28
#define QBMAN_RESULT_BPSCN     0x29
#define QBMAN_RESULT_CSCN_WQ   0x2a

/* QBMan FQ management command codes */
#define QBMAN_FQ_SCHEDULE	0x48
#define QBMAN_FQ_FORCE		0x49
#define QBMAN_FQ_XON		0x4d
#define QBMAN_FQ_XOFF		0x4e

/* structure of enqueue descriptor */
struct qbman_eq_desc {
	u8 verb;
	u8 dca;
	__le16 seqnum;
	__le16 orpid;
	__le16 reserved1;
	__le32 tgtid;
	__le32 tag;
	__le16 qdbin;
	u8 qpri;
	u8 reserved[3];
	u8 wae;
	u8 rspid;
	__le64 rsp_addr;
	u8 fd[32];
};

/* buffer release descriptor */
struct qbman_release_desc {
	u8 verb;
	u8 reserved;
	__le16 bpid;
	__le32 reserved2;
	__le64 buf[7];
};

/* Management command result codes */
#define QBMAN_MC_RSLT_OK      0xf0

#define CODE_CDAN_WE_EN    0x1
#define CODE_CDAN_WE_CTX   0x4

/* portal data structure */
struct qbman_swp {
	const struct qbman_swp_desc *desc;
	void *addr_cena;
	void __iomem *addr_cinh;

	/* Management commands */
	struct {
		u32 valid_bit; /* 0x00 or 0x80 */
	} mc;

	/* Push dequeues */
	u32 sdq;

	/* Volatile dequeues */
	struct {
		atomic_t available; /* indicates if a command can be sent */
		u32 valid_bit; /* 0x00 or 0x80 */
		struct dpaa2_dq *storage; /* NULL if DQRR */
	} vdq;

	/* DQRR */
	struct {
		u32 next_idx;
		u32 valid_bit;
		u8 dqrr_size;
		int reset_bug; /* indicates dqrr reset workaround is needed */
	} dqrr;
};

struct qbman_swp *qbman_swp_init(const struct qbman_swp_desc *d);
void qbman_swp_finish(struct qbman_swp *p);
u32 qbman_swp_interrupt_read_status(struct qbman_swp *p);
void qbman_swp_interrupt_clear_status(struct qbman_swp *p, u32 mask);
u32 qbman_swp_interrupt_get_trigger(struct qbman_swp *p);
void qbman_swp_interrupt_set_trigger(struct qbman_swp *p, u32 mask);
int qbman_swp_interrupt_get_inhibit(struct qbman_swp *p);
void qbman_swp_interrupt_set_inhibit(struct qbman_swp *p, int inhibit);

void qbman_swp_push_get(struct qbman_swp *p, u8 channel_idx, int *enabled);
void qbman_swp_push_set(struct qbman_swp *p, u8 channel_idx, int enable);

void qbman_pull_desc_clear(struct qbman_pull_desc *d);
void qbman_pull_desc_set_storage(struct qbman_pull_desc *d,
				 struct dpaa2_dq *storage,
				 dma_addr_t storage_phys,
				 int stash);
void qbman_pull_desc_set_numframes(struct qbman_pull_desc *d, u8 numframes);
void qbman_pull_desc_set_fq(struct qbman_pull_desc *d, u32 fqid);
void qbman_pull_desc_set_wq(struct qbman_pull_desc *d, u32 wqid,
			    enum qbman_pull_type_e dct);
void qbman_pull_desc_set_channel(struct qbman_pull_desc *d, u32 chid,
				 enum qbman_pull_type_e dct);

int qbman_swp_pull(struct qbman_swp *p, struct qbman_pull_desc *d);

const struct dpaa2_dq *qbman_swp_dqrr_next(struct qbman_swp *s);
void qbman_swp_dqrr_consume(struct qbman_swp *s, const struct dpaa2_dq *dq);

int qbman_result_has_new_result(struct qbman_swp *p, const struct dpaa2_dq *dq);

void qbman_eq_desc_clear(struct qbman_eq_desc *d);
void qbman_eq_desc_set_no_orp(struct qbman_eq_desc *d, int respond_success);
void qbman_eq_desc_set_token(struct qbman_eq_desc *d, u8 token);
void qbman_eq_desc_set_fq(struct qbman_eq_desc *d, u32 fqid);
void qbman_eq_desc_set_qd(struct qbman_eq_desc *d, u32 qdid,
			  u32 qd_bin, u32 qd_prio);

int qbman_swp_enqueue(struct qbman_swp *p, const struct qbman_eq_desc *d,
		      const struct dpaa2_fd *fd);

void qbman_release_desc_clear(struct qbman_release_desc *d);
void qbman_release_desc_set_bpid(struct qbman_release_desc *d, u16 bpid);
void qbman_release_desc_set_rcdi(struct qbman_release_desc *d, int enable);

int qbman_swp_release(struct qbman_swp *s, const struct qbman_release_desc *d,
		      const u64 *buffers, unsigned int num_buffers);
int qbman_swp_acquire(struct qbman_swp *s, u16 bpid, u64 *buffers,
		      unsigned int num_buffers);
int qbman_swp_alt_fq_state(struct qbman_swp *s, u32 fqid,
			   u8 alt_fq_verb);
int qbman_swp_CDAN_set(struct qbman_swp *s, u16 channelid,
		       u8 we_mask, u8 cdan_en,
		       u64 ctx);

void *qbman_swp_mc_start(struct qbman_swp *p);
void qbman_swp_mc_submit(struct qbman_swp *p, void *cmd, u8 cmd_verb);
void *qbman_swp_mc_result(struct qbman_swp *p);

/**
 * qbman_result_is_DQ() - check if the dequeue result is a dequeue response
 * @dq: the dequeue result to be checked
 *
 * DQRR entries may contain non-dequeue results, ie. notifications
 */
static inline int qbman_result_is_DQ(const struct dpaa2_dq *dq)
{
	return ((dq->dq.verb & QBMAN_RESULT_MASK) == QBMAN_RESULT_DQ);
}

/**
 * qbman_result_is_SCN() - Check the dequeue result is notification or not
 * @dq: the dequeue result to be checked
 *
 */
static inline int qbman_result_is_SCN(const struct dpaa2_dq *dq)
{
	return !qbman_result_is_DQ(dq);
}

/* FQ Data Availability */
static inline int qbman_result_is_FQDAN(const struct dpaa2_dq *dq)
{
	return ((dq->dq.verb & QBMAN_RESULT_MASK) == QBMAN_RESULT_FQDAN);
}

/* Channel Data Availability */
static inline int qbman_result_is_CDAN(const struct dpaa2_dq *dq)
{
	return ((dq->dq.verb & QBMAN_RESULT_MASK) == QBMAN_RESULT_CDAN);
}

/* Congestion State Change */
static inline int qbman_result_is_CSCN(const struct dpaa2_dq *dq)
{
	return ((dq->dq.verb & QBMAN_RESULT_MASK) == QBMAN_RESULT_CSCN_WQ);
}

/* Buffer Pool State Change */
static inline int qbman_result_is_BPSCN(const struct dpaa2_dq *dq)
{
	return ((dq->dq.verb & QBMAN_RESULT_MASK) == QBMAN_RESULT_BPSCN);
}

/* Congestion Group Count Update */
static inline int qbman_result_is_CGCU(const struct dpaa2_dq *dq)
{
	return ((dq->dq.verb & QBMAN_RESULT_MASK) == QBMAN_RESULT_CGCU);
}

/* Retirement */
static inline int qbman_result_is_FQRN(const struct dpaa2_dq *dq)
{
	return ((dq->dq.verb & QBMAN_RESULT_MASK) == QBMAN_RESULT_FQRN);
}

/* Retirement Immediate */
static inline int qbman_result_is_FQRNI(const struct dpaa2_dq *dq)
{
	return ((dq->dq.verb & QBMAN_RESULT_MASK) == QBMAN_RESULT_FQRNI);
}

 /* Park */
static inline int qbman_result_is_FQPN(const struct dpaa2_dq *dq)
{
	return ((dq->dq.verb & QBMAN_RESULT_MASK) == QBMAN_RESULT_FQPN);
}

/**
 * qbman_result_SCN_state() - Get the state field in State-change notification
 */
static inline u8 qbman_result_SCN_state(const struct dpaa2_dq *scn)
{
	return scn->scn.state;
}

#define SCN_RID_MASK 0x00FFFFFF

/**
 * qbman_result_SCN_rid() - Get the resource id in State-change notification
 */
static inline u32 qbman_result_SCN_rid(const struct dpaa2_dq *scn)
{
	return le32_to_cpu(scn->scn.rid_tok) & SCN_RID_MASK;
}

/**
 * qbman_result_SCN_ctx() - Get the context data in State-change notification
 */
static inline u64 qbman_result_SCN_ctx(const struct dpaa2_dq *scn)
{
	return le64_to_cpu(scn->scn.ctx);
}

/**
 * qbman_swp_fq_schedule() - Move the fq to the scheduled state
 * @s:    the software portal object
 * @fqid: the index of frame queue to be scheduled
 *
 * There are a couple of different ways that a FQ can end up parked state,
 * This schedules it.
 *
 * Return 0 for success, or negative error code for failure.
 */
static inline int qbman_swp_fq_schedule(struct qbman_swp *s, u32 fqid)
{
	return qbman_swp_alt_fq_state(s, fqid, QBMAN_FQ_SCHEDULE);
}

/**
 * qbman_swp_fq_force() - Force the FQ to fully scheduled state
 * @s:    the software portal object
 * @fqid: the index of frame queue to be forced
 *
 * Force eligible will force a tentatively-scheduled FQ to be fully-scheduled
 * and thus be available for selection by any channel-dequeuing behaviour (push
 * or pull). If the FQ is subsequently "dequeued" from the channel and is still
 * empty at the time this happens, the resulting dq_entry will have no FD.
 * (qbman_result_DQ_fd() will return NULL.)
 *
 * Return 0 for success, or negative error code for failure.
 */
static inline int qbman_swp_fq_force(struct qbman_swp *s, u32 fqid)
{
	return qbman_swp_alt_fq_state(s, fqid, QBMAN_FQ_FORCE);
}

/**
 * qbman_swp_fq_xon() - sets FQ flow-control to XON
 * @s:    the software portal object
 * @fqid: the index of frame queue
 *
 * This setting doesn't affect enqueues to the FQ, just dequeues.
 *
 * Return 0 for success, or negative error code for failure.
 */
static inline int qbman_swp_fq_xon(struct qbman_swp *s, u32 fqid)
{
	return qbman_swp_alt_fq_state(s, fqid, QBMAN_FQ_XON);
}

/**
 * qbman_swp_fq_xoff() - sets FQ flow-control to XOFF
 * @s:    the software portal object
 * @fqid: the index of frame queue
 *
 * This setting doesn't affect enqueues to the FQ, just dequeues.
 * XOFF FQs will remain in the tenatively-scheduled state, even when
 * non-empty, meaning they won't be selected for scheduled dequeuing.
 * If a FQ is changed to XOFF after it had already become truly-scheduled
 * to a channel, and a pull dequeue of that channel occurs that selects
 * that FQ for dequeuing, then the resulting dq_entry will have no FD.
 * (qbman_result_DQ_fd() will return NULL.)
 *
 * Return 0 for success, or negative error code for failure.
 */
static inline int qbman_swp_fq_xoff(struct qbman_swp *s, u32 fqid)
{
	return qbman_swp_alt_fq_state(s, fqid, QBMAN_FQ_XOFF);
}

/* If the user has been allocated a channel object that is going to generate
 * CDANs to another channel, then the qbman_swp_CDAN* functions will be
 * necessary.
 *
 * CDAN-enabled channels only generate a single CDAN notification, after which
 * they need to be reenabled before they'll generate another. The idea is
 * that pull dequeuing will occur in reaction to the CDAN, followed by a
 * reenable step. Each function generates a distinct command to hardware, so a
 * combination function is provided if the user wishes to modify the "context"
 * (which shows up in each CDAN message) each time they reenable, as a single
 * command to hardware.
 */

/**
 * qbman_swp_CDAN_set_context() - Set CDAN context
 * @s:         the software portal object
 * @channelid: the channel index
 * @ctx:       the context to be set in CDAN
 *
 * Return 0 for success, or negative error code for failure.
 */
static inline int qbman_swp_CDAN_set_context(struct qbman_swp *s, u16 channelid,
					     u64 ctx)
{
	return qbman_swp_CDAN_set(s, channelid,
				  CODE_CDAN_WE_CTX,
				  0, ctx);
}

/**
 * qbman_swp_CDAN_enable() - Enable CDAN for the channel
 * @s:         the software portal object
 * @channelid: the index of the channel to generate CDAN
 *
 * Return 0 for success, or negative error code for failure.
 */
static inline int qbman_swp_CDAN_enable(struct qbman_swp *s, u16 channelid)
{
	return qbman_swp_CDAN_set(s, channelid,
				  CODE_CDAN_WE_EN,
				  1, 0);
}

/**
 * qbman_swp_CDAN_disable() - disable CDAN for the channel
 * @s:         the software portal object
 * @channelid: the index of the channel to generate CDAN
 *
 * Return 0 for success, or negative error code for failure.
 */
static inline int qbman_swp_CDAN_disable(struct qbman_swp *s, u16 channelid)
{
	return qbman_swp_CDAN_set(s, channelid,
				  CODE_CDAN_WE_EN,
				  0, 0);
}

/**
 * qbman_swp_CDAN_set_context_enable() - Set CDAN contest and enable CDAN
 * @s:         the software portal object
 * @channelid: the index of the channel to generate CDAN
 * @ctx:i      the context set in CDAN
 *
 * Return 0 for success, or negative error code for failure.
 */
static inline int qbman_swp_CDAN_set_context_enable(struct qbman_swp *s,
						    u16 channelid,
						    u64 ctx)
{
	return qbman_swp_CDAN_set(s, channelid,
				  CODE_CDAN_WE_EN | CODE_CDAN_WE_CTX,
				  1, ctx);
}

/* Wraps up submit + poll-for-result */
static inline void *qbman_swp_mc_complete(struct qbman_swp *swp, void *cmd,
					  u8 cmd_verb)
{
	int loopvar = 1000;

	qbman_swp_mc_submit(swp, cmd, cmd_verb);

	do {
		cmd = qbman_swp_mc_result(swp);
	} while (!cmd && loopvar--);

	WARN_ON(!loopvar);

	return cmd;
}

/* Query APIs */
struct qbman_fq_query_np_rslt {
	u8 verb;
	u8 rslt;
	u8 st1;
	u8 st2;
	u8 reserved[2];
	__le16 od1_sfdr;
	__le16 od2_sfdr;
	__le16 od3_sfdr;
	__le16 ra1_sfdr;
	__le16 ra2_sfdr;
	__le32 pfdr_hptr;
	__le32 pfdr_tptr;
	__le32 frm_cnt;
	__le32 byte_cnt;
	__le16 ics_surp;
	u8 is;
	u8 reserved2[29];
};

int qbman_fq_query_state(struct qbman_swp *s, u32 fqid,
			 struct qbman_fq_query_np_rslt *r);
u32 qbman_fq_state_frame_count(const struct qbman_fq_query_np_rslt *r);
u32 qbman_fq_state_byte_count(const struct qbman_fq_query_np_rslt *r);

struct qbman_bp_query_rslt {
	u8 verb;
	u8 rslt;
	u8 reserved[4];
	u8 bdi;
	u8 state;
	__le32 fill;
	__le32 hdotr;
	__le16 swdet;
	__le16 swdxt;
	__le16 hwdet;
	__le16 hwdxt;
	__le16 swset;
	__le16 swsxt;
	__le16 vbpid;
	__le16 icid;
	__le64 bpscn_addr;
	__le64 bpscn_ctx;
	__le16 hw_targ;
	u8 dbe;
	u8 reserved2;
	u8 sdcnt;
	u8 hdcnt;
	u8 sscnt;
	u8 reserved3[9];
};

int qbman_bp_query(struct qbman_swp *s, u16 bpid,
		   struct qbman_bp_query_rslt *r);

u32 qbman_bp_info_num_free_bufs(struct qbman_bp_query_rslt *a);

#endif /* __FSL_QBMAN_PORTAL_H */
