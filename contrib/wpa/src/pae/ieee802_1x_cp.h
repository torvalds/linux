/*
 * IEEE Std 802.1X-2010 Controlled Port of PAE state machine - CP state machine
 * Copyright (c) 2013, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef IEEE802_1X_CP_H
#define IEEE802_1X_CP_H

#include "common/defs.h"
#include "common/ieee802_1x_defs.h"

struct ieee802_1x_cp_sm;
struct ieee802_1x_kay;
struct ieee802_1x_mka_ki;

struct ieee802_1x_cp_sm * ieee802_1x_cp_sm_init(struct ieee802_1x_kay *kay);
void ieee802_1x_cp_sm_deinit(struct ieee802_1x_cp_sm *sm);
void ieee802_1x_cp_sm_step(void *cp_ctx);
void ieee802_1x_cp_connect_pending(void *cp_ctx);
void ieee802_1x_cp_connect_unauthenticated(void *cp_ctx);
void ieee802_1x_cp_connect_authenticated(void *cp_ctx);
void ieee802_1x_cp_connect_secure(void *cp_ctx);
void ieee802_1x_cp_signal_chgdserver(void *cp_ctx);
void ieee802_1x_cp_set_electedself(void *cp_ctx, Boolean status);
void ieee802_1x_cp_set_authorizationdata(void *cp_ctx, u8 *pdata, int len);
void ieee802_1x_cp_set_ciphersuite(void *cp_ctx, u64 cs);
void ieee802_1x_cp_set_offset(void *cp_ctx, enum confidentiality_offset offset);
void ieee802_1x_cp_signal_newsak(void *cp_ctx);
void ieee802_1x_cp_set_distributedki(void *cp_ctx,
				     const struct ieee802_1x_mka_ki *dki);
void ieee802_1x_cp_set_distributedan(void *cp_ctx, u8 an);
void ieee802_1x_cp_set_usingreceivesas(void *cp_ctx, Boolean status);
void ieee802_1x_cp_set_allreceiving(void *cp_ctx, Boolean status);
void ieee802_1x_cp_set_servertransmitting(void *cp_ctx, Boolean status);
void ieee802_1x_cp_set_usingtransmitas(void *cp_ctx, Boolean status);

#endif /* IEEE802_1X_CP_H */
