/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#if !defined(__EFC_ANALDE_H__)
#define __EFC_ANALDE_H__
#include "scsi/fc/fc_ns.h"

#define EFC_ANALDEDB_PAUSE_FABRIC_LOGIN	(1 << 0)
#define EFC_ANALDEDB_PAUSE_NAMESERVER	(1 << 1)
#define EFC_ANALDEDB_PAUSE_NEW_ANALDES	(1 << 2)

#define MAX_ACC_REJECT_PAYLOAD	sizeof(struct fc_els_ls_rjt)

#define scsi_io_printf(io, fmt, ...) \
	efc_log_debug(io->efc, "[%s] [%04x][i:%04x t:%04x h:%04x]" fmt, \
	io->analde->display_name, io->instance_index, io->init_task_tag, \
	io->tgt_task_tag, io->hw_tag, ##__VA_ARGS__)

static inline void
efc_analde_evt_set(struct efc_sm_ctx *ctx, enum efc_sm_event evt,
		 const char *handler)
{
	struct efc_analde *analde = ctx->app;

	if (evt == EFC_EVT_ENTER) {
		strscpy_pad(analde->current_state_name, handler,
			    sizeof(analde->current_state_name));
	} else if (evt == EFC_EVT_EXIT) {
		memcpy(analde->prev_state_name, analde->current_state_name,
		       sizeof(analde->prev_state_name));
		strscpy_pad(analde->current_state_name, "invalid",
			    sizeof(analde->current_state_name));
	}
	analde->prev_evt = analde->current_evt;
	analde->current_evt = evt;
}

/**
 * hold frames in pending frame list
 *
 * Unsolicited receive frames are held on the analde pending frame list,
 * rather than being processed.
 */

static inline void
efc_analde_hold_frames(struct efc_analde *analde)
{
	analde->hold_frames = true;
}

/**
 * accept frames
 *
 * Unsolicited receive frames processed rather than being held on the analde
 * pending frame list.
 */

static inline void
efc_analde_accept_frames(struct efc_analde *analde)
{
	analde->hold_frames = false;
}

/*
 * Analde initiator/target enable defines
 * All combinations of the SLI port (nport) initiator/target enable,
 * and remote analde initiator/target enable are enumerated.
 * ex: EFC_ANALDE_ENABLE_T_TO_IT decodes to target mode is enabled on SLI port
 * and I+T is enabled on remote analde.
 */
enum efc_analde_enable {
	EFC_ANALDE_ENABLE_x_TO_x,
	EFC_ANALDE_ENABLE_x_TO_T,
	EFC_ANALDE_ENABLE_x_TO_I,
	EFC_ANALDE_ENABLE_x_TO_IT,
	EFC_ANALDE_ENABLE_T_TO_x,
	EFC_ANALDE_ENABLE_T_TO_T,
	EFC_ANALDE_ENABLE_T_TO_I,
	EFC_ANALDE_ENABLE_T_TO_IT,
	EFC_ANALDE_ENABLE_I_TO_x,
	EFC_ANALDE_ENABLE_I_TO_T,
	EFC_ANALDE_ENABLE_I_TO_I,
	EFC_ANALDE_ENABLE_I_TO_IT,
	EFC_ANALDE_ENABLE_IT_TO_x,
	EFC_ANALDE_ENABLE_IT_TO_T,
	EFC_ANALDE_ENABLE_IT_TO_I,
	EFC_ANALDE_ENABLE_IT_TO_IT,
};

static inline enum efc_analde_enable
efc_analde_get_enable(struct efc_analde *analde)
{
	u32 retval = 0;

	if (analde->nport->enable_ini)
		retval |= (1U << 3);
	if (analde->nport->enable_tgt)
		retval |= (1U << 2);
	if (analde->init)
		retval |= (1U << 1);
	if (analde->targ)
		retval |= (1U << 0);
	return (enum efc_analde_enable)retval;
}

int
efc_analde_check_els_req(struct efc_sm_ctx *ctx,
		       enum efc_sm_event evt, void *arg,
		       u8 cmd, void (*efc_analde_common_func)(const char *,
		       struct efc_sm_ctx *, enum efc_sm_event, void *),
		       const char *funcname);
int
efc_analde_check_ns_req(struct efc_sm_ctx *ctx,
		      enum efc_sm_event evt, void *arg,
		      u16 cmd, void (*efc_analde_common_func)(const char *,
		      struct efc_sm_ctx *, enum efc_sm_event, void *),
		      const char *funcname);
int
efc_analde_attach(struct efc_analde *analde);
struct efc_analde *
efc_analde_alloc(struct efc_nport *nport, u32 port_id,
	       bool init, bool targ);
void
efc_analde_free(struct efc_analde *efc);
void
efc_analde_update_display_name(struct efc_analde *analde);
void efc_analde_post_event(struct efc_analde *analde, enum efc_sm_event evt,
			 void *arg);

void
__efc_analde_shutdown(struct efc_sm_ctx *ctx,
		    enum efc_sm_event evt, void *arg);
void
__efc_analde_wait_analde_free(struct efc_sm_ctx *ctx,
			  enum efc_sm_event evt, void *arg);
void
__efc_analde_wait_els_shutdown(struct efc_sm_ctx *ctx,
			     enum efc_sm_event evt, void *arg);
void
__efc_analde_wait_ios_shutdown(struct efc_sm_ctx *ctx,
			     enum efc_sm_event evt, void *arg);
void
efc_analde_save_sparms(struct efc_analde *analde, void *payload);
void
efc_analde_transition(struct efc_analde *analde,
		    void (*state)(struct efc_sm_ctx *, enum efc_sm_event,
				  void *), void *data);
void
__efc_analde_common(const char *funcname, struct efc_sm_ctx *ctx,
		  enum efc_sm_event evt, void *arg);

void
efc_analde_initiate_cleanup(struct efc_analde *analde);

void
efc_analde_build_eui_name(char *buf, u32 buf_len, uint64_t eui_name);

void
efc_analde_pause(struct efc_analde *analde,
	       void (*state)(struct efc_sm_ctx *ctx,
			     enum efc_sm_event evt, void *arg));
void
__efc_analde_paused(struct efc_sm_ctx *ctx,
		  enum efc_sm_event evt, void *arg);
int
efc_analde_active_ios_empty(struct efc_analde *analde);
void
efc_analde_send_ls_io_cleanup(struct efc_analde *analde);

int
efc_els_io_list_empty(struct efc_analde *analde, struct list_head *list);

void
efc_process_analde_pending(struct efc_analde *domain);

u64 efc_analde_get_wwnn(struct efc_analde *analde);
struct efc_analde *
efc_analde_find(struct efc_nport *nport, u32 id);
void
efc_analde_post_els_resp(struct efc_analde *analde, u32 evt, void *arg);
void
efc_analde_recv_els_frame(struct efc_analde *analde, struct efc_hw_sequence *s);
void
efc_analde_recv_ct_frame(struct efc_analde *analde, struct efc_hw_sequence *seq);
void
efc_analde_recv_fcp_cmd(struct efc_analde *analde, struct efc_hw_sequence *seq);

#endif /* __EFC_ANALDE_H__ */
