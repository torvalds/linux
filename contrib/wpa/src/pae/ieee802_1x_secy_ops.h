 /*
 * SecY Operations
 * Copyright (c) 2013, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef IEEE802_1X_SECY_OPS_H
#define IEEE802_1X_SECY_OPS_H

#include "common/defs.h"
#include "common/ieee802_1x_defs.h"

struct ieee802_1x_kay_conf;

int secy_init_macsec(struct ieee802_1x_kay *kay);
int secy_deinit_macsec(struct ieee802_1x_kay *kay);

/****** CP -> SecY ******/
int secy_cp_control_validate_frames(struct ieee802_1x_kay *kay,
				    enum validate_frames vf);
int secy_cp_control_protect_frames(struct ieee802_1x_kay *kay, Boolean flag);
int secy_cp_control_encrypt(struct ieee802_1x_kay *kay, Boolean enabled);
int secy_cp_control_replay(struct ieee802_1x_kay *kay, Boolean flag, u32 win);
int secy_cp_control_current_cipher_suite(struct ieee802_1x_kay *kay, u64 cs);
int secy_cp_control_confidentiality_offset(struct ieee802_1x_kay *kay,
					   enum confidentiality_offset co);
int secy_cp_control_enable_port(struct ieee802_1x_kay *kay, Boolean flag);

/****** KaY -> SecY *******/
int secy_get_capability(struct ieee802_1x_kay *kay, enum macsec_cap *cap);
int secy_get_receive_lowest_pn(struct ieee802_1x_kay *kay,
			       struct receive_sa *rxsa);
int secy_get_transmit_next_pn(struct ieee802_1x_kay *kay,
			      struct transmit_sa *txsa);
int secy_set_transmit_next_pn(struct ieee802_1x_kay *kay,
			      struct transmit_sa *txsa);
int secy_create_receive_sc(struct ieee802_1x_kay *kay, struct receive_sc *rxsc);
int secy_delete_receive_sc(struct ieee802_1x_kay *kay, struct receive_sc *rxsc);
int secy_create_receive_sa(struct ieee802_1x_kay *kay, struct receive_sa *rxsa);
int secy_delete_receive_sa(struct ieee802_1x_kay *kay, struct receive_sa *rxsa);
int secy_enable_receive_sa(struct ieee802_1x_kay *kay, struct receive_sa *rxsa);
int secy_disable_receive_sa(struct ieee802_1x_kay *kay,
			    struct receive_sa *rxsa);

int secy_create_transmit_sc(struct ieee802_1x_kay *kay,
			    struct transmit_sc *txsc);
int secy_delete_transmit_sc(struct ieee802_1x_kay *kay,
			    struct transmit_sc *txsc);
int secy_create_transmit_sa(struct ieee802_1x_kay *kay,
			    struct transmit_sa *txsa);
int secy_delete_transmit_sa(struct ieee802_1x_kay *kay,
			    struct transmit_sa *txsa);
int secy_enable_transmit_sa(struct ieee802_1x_kay *kay,
			    struct transmit_sa *txsa);
int secy_disable_transmit_sa(struct ieee802_1x_kay *kay,
			     struct transmit_sa *txsa);

#endif /* IEEE802_1X_SECY_OPS_H */
