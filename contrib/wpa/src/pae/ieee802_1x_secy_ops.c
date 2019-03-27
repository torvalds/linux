 /*
 * SecY Operations
 * Copyright (c) 2013, Qualcomm Atheros, Inc.
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "utils/includes.h"

#include "utils/common.h"
#include "utils/eloop.h"
#include "common/defs.h"
#include "drivers/driver.h"
#include "pae/ieee802_1x_kay.h"
#include "pae/ieee802_1x_kay_i.h"
#include "pae/ieee802_1x_secy_ops.h"


int secy_cp_control_validate_frames(struct ieee802_1x_kay *kay,
				    enum validate_frames vf)
{
	kay->vf = vf;
	return 0;
}


int secy_cp_control_protect_frames(struct ieee802_1x_kay *kay, Boolean enabled)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->enable_protect_frames) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy enable_protect_frames operation not supported");
		return -1;
	}

	return ops->enable_protect_frames(ops->ctx, enabled);
}


int secy_cp_control_encrypt(struct ieee802_1x_kay *kay, Boolean enabled)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->enable_encrypt) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy enable_encrypt operation not supported");
		return -1;
	}

	return ops->enable_encrypt(ops->ctx, enabled);
}


int secy_cp_control_replay(struct ieee802_1x_kay *kay, Boolean enabled, u32 win)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->set_replay_protect) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy set_replay_protect operation not supported");
		return -1;
	}

	return ops->set_replay_protect(ops->ctx, enabled, win);
}


int secy_cp_control_current_cipher_suite(struct ieee802_1x_kay *kay, u64 cs)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->set_current_cipher_suite) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy set_current_cipher_suite operation not supported");
		return -1;
	}

	return ops->set_current_cipher_suite(ops->ctx, cs);
}


int secy_cp_control_confidentiality_offset(struct ieee802_1x_kay *kay,
					   enum confidentiality_offset co)
{
	kay->co = co;
	return 0;
}


int secy_cp_control_enable_port(struct ieee802_1x_kay *kay, Boolean enabled)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->enable_controlled_port) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy enable_controlled_port operation not supported");
		return -1;
	}

	return ops->enable_controlled_port(ops->ctx, enabled);
}


int secy_get_capability(struct ieee802_1x_kay *kay, enum macsec_cap *cap)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->macsec_get_capability) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy macsec_get_capability operation not supported");
		return -1;
	}

	return ops->macsec_get_capability(ops->ctx, cap);
}


int secy_get_receive_lowest_pn(struct ieee802_1x_kay *kay,
			       struct receive_sa *rxsa)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay || !rxsa) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->get_receive_lowest_pn) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy get_receive_lowest_pn operation not supported");
		return -1;
	}

	return ops->get_receive_lowest_pn(ops->ctx, rxsa);
}


int secy_get_transmit_next_pn(struct ieee802_1x_kay *kay,
			      struct transmit_sa *txsa)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay || !txsa) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->get_transmit_next_pn) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy get_receive_lowest_pn operation not supported");
		return -1;
	}

	return ops->get_transmit_next_pn(ops->ctx, txsa);
}


int secy_set_transmit_next_pn(struct ieee802_1x_kay *kay,
			      struct transmit_sa *txsa)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay || !txsa) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->set_transmit_next_pn) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy get_receive_lowest_pn operation not supported");
		return -1;
	}

	return ops->set_transmit_next_pn(ops->ctx, txsa);
}


int secy_create_receive_sc(struct ieee802_1x_kay *kay, struct receive_sc *rxsc)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay || !rxsc) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->create_receive_sc) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy create_receive_sc operation not supported");
		return -1;
	}

	return ops->create_receive_sc(ops->ctx, rxsc, kay->vf, kay->co);
}


int secy_delete_receive_sc(struct ieee802_1x_kay *kay, struct receive_sc *rxsc)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay || !rxsc) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->delete_receive_sc) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy delete_receive_sc operation not supported");
		return -1;
	}

	return ops->delete_receive_sc(ops->ctx, rxsc);
}


int secy_create_receive_sa(struct ieee802_1x_kay *kay, struct receive_sa *rxsa)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay || !rxsa) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->create_receive_sa) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy create_receive_sa operation not supported");
		return -1;
	}

	return ops->create_receive_sa(ops->ctx, rxsa);
}


int secy_delete_receive_sa(struct ieee802_1x_kay *kay, struct receive_sa *rxsa)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay || !rxsa) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->delete_receive_sa) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy delete_receive_sa operation not supported");
		return -1;
	}

	return ops->delete_receive_sa(ops->ctx, rxsa);
}


int secy_enable_receive_sa(struct ieee802_1x_kay *kay, struct receive_sa *rxsa)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay || !rxsa) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->enable_receive_sa) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy enable_receive_sa operation not supported");
		return -1;
	}

	rxsa->enable_receive = TRUE;

	return ops->enable_receive_sa(ops->ctx, rxsa);
}


int secy_disable_receive_sa(struct ieee802_1x_kay *kay, struct receive_sa *rxsa)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay || !rxsa) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->disable_receive_sa) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy disable_receive_sa operation not supported");
		return -1;
	}

	rxsa->enable_receive = FALSE;

	return ops->disable_receive_sa(ops->ctx, rxsa);
}


int secy_create_transmit_sc(struct ieee802_1x_kay *kay,
			    struct transmit_sc *txsc)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay || !txsc) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->create_transmit_sc) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy create_transmit_sc operation not supported");
		return -1;
	}

	return ops->create_transmit_sc(ops->ctx, txsc, kay->co);
}


int secy_delete_transmit_sc(struct ieee802_1x_kay *kay,
			    struct transmit_sc *txsc)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay || !txsc) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->delete_transmit_sc) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy delete_transmit_sc operation not supported");
		return -1;
	}

	return ops->delete_transmit_sc(ops->ctx, txsc);
}


int secy_create_transmit_sa(struct ieee802_1x_kay *kay,
			    struct transmit_sa *txsa)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay || !txsa) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->create_transmit_sa) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy create_transmit_sa operation not supported");
		return -1;
	}

	return ops->create_transmit_sa(ops->ctx, txsa);
}


int secy_delete_transmit_sa(struct ieee802_1x_kay *kay,
			    struct transmit_sa *txsa)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay || !txsa) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->delete_transmit_sa) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy delete_transmit_sa operation not supported");
		return -1;
	}

	return ops->delete_transmit_sa(ops->ctx, txsa);
}


int secy_enable_transmit_sa(struct ieee802_1x_kay *kay,
			    struct transmit_sa *txsa)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay || !txsa) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->enable_transmit_sa) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy enable_transmit_sa operation not supported");
		return -1;
	}

	txsa->enable_transmit = TRUE;

	return ops->enable_transmit_sa(ops->ctx, txsa);
}


int secy_disable_transmit_sa(struct ieee802_1x_kay *kay,
			     struct transmit_sa *txsa)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay || !txsa) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->disable_transmit_sa) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy disable_transmit_sa operation not supported");
		return -1;
	}

	txsa->enable_transmit = FALSE;

	return ops->disable_transmit_sa(ops->ctx, txsa);
}


int secy_init_macsec(struct ieee802_1x_kay *kay)
{
	int ret;
	struct ieee802_1x_kay_ctx *ops;
	struct macsec_init_params params;

	if (!kay) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->macsec_init) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy macsec_init operation not supported");
		return -1;
	}

	params.use_es = FALSE;
	params.use_scb = FALSE;
	params.always_include_sci = TRUE;

	ret = ops->macsec_init(ops->ctx, &params);

	return ret;
}


int secy_deinit_macsec(struct ieee802_1x_kay *kay)
{
	struct ieee802_1x_kay_ctx *ops;

	if (!kay) {
		wpa_printf(MSG_ERROR, "KaY: %s params invalid", __func__);
		return -1;
	}

	ops = kay->ctx;
	if (!ops || !ops->macsec_deinit) {
		wpa_printf(MSG_ERROR,
			   "KaY: secy macsec_deinit operation not supported");
		return -1;
	}

	return ops->macsec_deinit(ops->ctx);
}
