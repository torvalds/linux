/* SPDX-License-Identifier: GPL-2.0-only */
/* Atlantic Network Driver
 * Copyright (C) 2020 Marvell International Ltd.
 */

#ifndef AQ_MACSEC_H
#define AQ_MACSEC_H

#include <linux/netdevice.h>
#if IS_ENABLED(CONFIG_MACSEC)

#include "net/macsec.h"

struct aq_nic_s;

#define AQ_MACSEC_MAX_SC 32
#define AQ_MACSEC_MAX_SA 32

enum aq_macsec_sc_sa {
	aq_macsec_sa_sc_4sa_8sc,
	aq_macsec_sa_sc_not_used,
	aq_macsec_sa_sc_2sa_16sc,
	aq_macsec_sa_sc_1sa_32sc,
};

struct aq_macsec_txsc {
};

struct aq_macsec_rxsc {
};

struct aq_macsec_cfg {
	enum aq_macsec_sc_sa sc_sa;
	/* Egress channel configuration */
	unsigned long txsc_idx_busy;
	struct aq_macsec_txsc aq_txsc[AQ_MACSEC_MAX_SC];
	/* Ingress channel configuration */
	unsigned long rxsc_idx_busy;
	struct aq_macsec_rxsc aq_rxsc[AQ_MACSEC_MAX_SC];
};

extern const struct macsec_ops aq_macsec_ops;

int aq_macsec_init(struct aq_nic_s *nic);
void aq_macsec_free(struct aq_nic_s *nic);
int aq_macsec_enable(struct aq_nic_s *nic);
void aq_macsec_work(struct aq_nic_s *nic);

#endif

#endif /* AQ_MACSEC_H */
