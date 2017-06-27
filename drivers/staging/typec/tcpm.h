/*
 * Copyright 2015-2017 Google, Inc
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#ifndef __LINUX_USB_TCPM_H
#define __LINUX_USB_TCPM_H

#include <linux/bitops.h>
#include <linux/usb/typec.h>
#include "pd.h"

enum typec_cc_status {
	TYPEC_CC_OPEN,
	TYPEC_CC_RA,
	TYPEC_CC_RD,
	TYPEC_CC_RP_DEF,
	TYPEC_CC_RP_1_5,
	TYPEC_CC_RP_3_0,
};

enum typec_cc_polarity {
	TYPEC_POLARITY_CC1,
	TYPEC_POLARITY_CC2,
};

/* Time to wait for TCPC to complete transmit */
#define PD_T_TCPC_TX_TIMEOUT  100

enum tcpm_transmit_status {
	TCPC_TX_SUCCESS = 0,
	TCPC_TX_DISCARDED = 1,
	TCPC_TX_FAILED = 2,
};

enum tcpm_transmit_type {
	TCPC_TX_SOP = 0,
	TCPC_TX_SOP_PRIME = 1,
	TCPC_TX_SOP_PRIME_PRIME = 2,
	TCPC_TX_SOP_DEBUG_PRIME = 3,
	TCPC_TX_SOP_DEBUG_PRIME_PRIME = 4,
	TCPC_TX_HARD_RESET = 5,
	TCPC_TX_CABLE_RESET = 6,
	TCPC_TX_BIST_MODE_2 = 7
};

struct tcpc_config {
	const u32 *src_pdo;
	unsigned int nr_src_pdo;

	const u32 *snk_pdo;
	unsigned int nr_snk_pdo;

	const u32 *snk_vdo;
	unsigned int nr_snk_vdo;

	unsigned int max_snk_mv;
	unsigned int max_snk_ma;
	unsigned int max_snk_mw;
	unsigned int operating_snk_mw;

	enum typec_port_type type;
	enum typec_role default_role;
	bool try_role_hw;	/* try.{src,snk} implemented in hardware */

	struct typec_altmode_desc *alt_modes;
};

enum tcpc_usb_switch {
	TCPC_USB_SWITCH_CONNECT,
	TCPC_USB_SWITCH_DISCONNECT,
	TCPC_USB_SWITCH_RESTORE,	/* TODO FIXME */
};

/* Mux state attributes */
#define TCPC_MUX_USB_ENABLED		BIT(0)	/* USB enabled */
#define TCPC_MUX_DP_ENABLED		BIT(1)	/* DP enabled */
#define TCPC_MUX_POLARITY_INVERTED	BIT(2)	/* Polarity inverted */

/* Mux modes, decoded to attributes */
enum tcpc_mux_mode {
	TYPEC_MUX_NONE	= 0,				/* Open switch */
	TYPEC_MUX_USB	= TCPC_MUX_USB_ENABLED,		/* USB only */
	TYPEC_MUX_DP	= TCPC_MUX_DP_ENABLED,		/* DP only */
	TYPEC_MUX_DOCK	= TCPC_MUX_USB_ENABLED |	/* Both USB and DP */
			  TCPC_MUX_DP_ENABLED,
};

struct tcpc_mux_dev {
	int (*set)(struct tcpc_mux_dev *dev, enum tcpc_mux_mode mux_mode,
		   enum tcpc_usb_switch usb_config,
		   enum typec_cc_polarity polarity);
	bool dfp_only;
	void *priv_data;
};

struct tcpc_dev {
	const struct tcpc_config *config;

	int (*init)(struct tcpc_dev *dev);
	int (*get_vbus)(struct tcpc_dev *dev);
	int (*set_cc)(struct tcpc_dev *dev, enum typec_cc_status cc);
	int (*get_cc)(struct tcpc_dev *dev, enum typec_cc_status *cc1,
		      enum typec_cc_status *cc2);
	int (*set_polarity)(struct tcpc_dev *dev,
			    enum typec_cc_polarity polarity);
	int (*set_vconn)(struct tcpc_dev *dev, bool on);
	int (*set_vbus)(struct tcpc_dev *dev, bool on, bool charge);
	int (*set_current_limit)(struct tcpc_dev *dev, u32 max_ma, u32 mv);
	int (*set_pd_rx)(struct tcpc_dev *dev, bool on);
	int (*set_roles)(struct tcpc_dev *dev, bool attached,
			 enum typec_role role, enum typec_data_role data);
	int (*start_drp_toggling)(struct tcpc_dev *dev,
				  enum typec_cc_status cc);
	int (*try_role)(struct tcpc_dev *dev, int role);
	int (*pd_transmit)(struct tcpc_dev *dev, enum tcpm_transmit_type type,
			   const struct pd_message *msg);
	struct tcpc_mux_dev *mux;
};

struct tcpm_port;

struct tcpm_port *tcpm_register_port(struct device *dev, struct tcpc_dev *tcpc);
void tcpm_unregister_port(struct tcpm_port *port);

void tcpm_update_source_capabilities(struct tcpm_port *port, const u32 *pdo,
				     unsigned int nr_pdo);
void tcpm_update_sink_capabilities(struct tcpm_port *port, const u32 *pdo,
				   unsigned int nr_pdo,
				   unsigned int max_snk_mv,
				   unsigned int max_snk_ma,
				   unsigned int max_snk_mw,
				   unsigned int operating_snk_mw);

void tcpm_vbus_change(struct tcpm_port *port);
void tcpm_cc_change(struct tcpm_port *port);
void tcpm_pd_receive(struct tcpm_port *port,
		     const struct pd_message *msg);
void tcpm_pd_transmit_complete(struct tcpm_port *port,
			       enum tcpm_transmit_status status);
void tcpm_pd_hard_reset(struct tcpm_port *port);
void tcpm_tcpc_reset(struct tcpm_port *port);

#endif /* __LINUX_USB_TCPM_H */
