/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

#include <bfa_os_inc.h>
#include <cs/bfa_plog.h>
#include <cs/bfa_debug.h>

static int
plkd_validate_logrec(struct bfa_plog_rec_s *pl_rec)
{
	if ((pl_rec->log_type != BFA_PL_LOG_TYPE_INT)
	    && (pl_rec->log_type != BFA_PL_LOG_TYPE_STRING))
		return 1;

	if ((pl_rec->log_type != BFA_PL_LOG_TYPE_INT)
	    && (pl_rec->log_num_ints > BFA_PL_INT_LOG_SZ))
		return 1;

	return 0;
}

static void
bfa_plog_add(struct bfa_plog_s *plog, struct bfa_plog_rec_s *pl_rec)
{
	u16        tail;
	struct bfa_plog_rec_s *pl_recp;

	if (plog->plog_enabled == 0)
		return;

	if (plkd_validate_logrec(pl_rec)) {
		bfa_assert(0);
		return;
	}

	tail = plog->tail;

	pl_recp = &(plog->plog_recs[tail]);

	bfa_os_memcpy(pl_recp, pl_rec, sizeof(struct bfa_plog_rec_s));

	pl_recp->tv = BFA_TRC_TS(plog);
	BFA_PL_LOG_REC_INCR(plog->tail);

	if (plog->head == plog->tail)
		BFA_PL_LOG_REC_INCR(plog->head);
}

void
bfa_plog_init(struct bfa_plog_s *plog)
{
	bfa_os_memset((char *)plog, 0, sizeof(struct bfa_plog_s));

	bfa_os_memcpy(plog->plog_sig, BFA_PL_SIG_STR, BFA_PL_SIG_LEN);
	plog->head = plog->tail = 0;
	plog->plog_enabled = 1;
}

void
bfa_plog_str(struct bfa_plog_s *plog, enum bfa_plog_mid mid,
		enum bfa_plog_eid event,
		u16 misc, char *log_str)
{
	struct bfa_plog_rec_s  lp;

	if (plog->plog_enabled) {
		bfa_os_memset(&lp, 0, sizeof(struct bfa_plog_rec_s));
		lp.mid = mid;
		lp.eid = event;
		lp.log_type = BFA_PL_LOG_TYPE_STRING;
		lp.misc = misc;
		strncpy(lp.log_entry.string_log, log_str,
			BFA_PL_STRING_LOG_SZ - 1);
		lp.log_entry.string_log[BFA_PL_STRING_LOG_SZ - 1] = '\0';
		bfa_plog_add(plog, &lp);
	}
}

void
bfa_plog_intarr(struct bfa_plog_s *plog, enum bfa_plog_mid mid,
		enum bfa_plog_eid event,
		u16 misc, u32 *intarr, u32 num_ints)
{
	struct bfa_plog_rec_s  lp;
	u32        i;

	if (num_ints > BFA_PL_INT_LOG_SZ)
		num_ints = BFA_PL_INT_LOG_SZ;

	if (plog->plog_enabled) {
		bfa_os_memset(&lp, 0, sizeof(struct bfa_plog_rec_s));
		lp.mid = mid;
		lp.eid = event;
		lp.log_type = BFA_PL_LOG_TYPE_INT;
		lp.misc = misc;

		for (i = 0; i < num_ints; i++)
			bfa_os_assign(lp.log_entry.int_log[i],
					intarr[i]);

		lp.log_num_ints = (u8) num_ints;

		bfa_plog_add(plog, &lp);
	}
}

void
bfa_plog_fchdr(struct bfa_plog_s *plog, enum bfa_plog_mid mid,
			enum bfa_plog_eid event,
			u16 misc, struct fchs_s *fchdr)
{
	struct bfa_plog_rec_s  lp;
	u32       *tmp_int = (u32 *) fchdr;
	u32        ints[BFA_PL_INT_LOG_SZ];

	if (plog->plog_enabled) {
		bfa_os_memset(&lp, 0, sizeof(struct bfa_plog_rec_s));

		ints[0] = tmp_int[0];
		ints[1] = tmp_int[1];
		ints[2] = tmp_int[4];

		bfa_plog_intarr(plog, mid, event, misc, ints, 3);
	}
}

void
bfa_plog_fchdr_and_pl(struct bfa_plog_s *plog, enum bfa_plog_mid mid,
		      enum bfa_plog_eid event, u16 misc, struct fchs_s *fchdr,
		      u32 pld_w0)
{
	struct bfa_plog_rec_s  lp;
	u32       *tmp_int = (u32 *) fchdr;
	u32        ints[BFA_PL_INT_LOG_SZ];

	if (plog->plog_enabled) {
		bfa_os_memset(&lp, 0, sizeof(struct bfa_plog_rec_s));

		ints[0] = tmp_int[0];
		ints[1] = tmp_int[1];
		ints[2] = tmp_int[4];
		ints[3] = pld_w0;

		bfa_plog_intarr(plog, mid, event, misc, ints, 4);
	}
}

void
bfa_plog_clear(struct bfa_plog_s *plog)
{
	plog->head = plog->tail = 0;
}

void
bfa_plog_enable(struct bfa_plog_s *plog)
{
	plog->plog_enabled = 1;
}

void
bfa_plog_disable(struct bfa_plog_s *plog)
{
	plog->plog_enabled = 0;
}

bfa_boolean_t
bfa_plog_get_setting(struct bfa_plog_s *plog)
{
	return (bfa_boolean_t)plog->plog_enabled;
}
