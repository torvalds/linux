/* SPDX-License-Identifier: GPL-2.0+ */
/* Microchip lan969x Switch driver
 *
 * Copyright (c) 2024 Microchip Technology Inc. and its subsidiaries.
 */

#ifndef __LAN969X_H__
#define __LAN969X_H__

#include "../sparx5/sparx5_main.h"
#include "../sparx5/sparx5_regs.h"
#include "../sparx5/sparx5_vcap_impl.h"

/* lan969x.c */
extern const struct sparx5_match_data lan969x_desc;

/* lan969x_vcap_ag_api.c */
extern const struct vcap_statistics lan969x_vcap_stats;
extern const struct vcap_info lan969x_vcaps[];

/* lan969x_vcap_impl.c */
extern const struct sparx5_vcap_inst lan969x_vcap_inst_cfg[];

/* lan969x_regs.c */
extern const unsigned int lan969x_tsize[TSIZE_LAST];
extern const unsigned int lan969x_raddr[RADDR_LAST];
extern const unsigned int lan969x_rcnt[RCNT_LAST];
extern const unsigned int lan969x_gaddr[GADDR_LAST];
extern const unsigned int lan969x_gcnt[GCNT_LAST];
extern const unsigned int lan969x_gsize[GSIZE_LAST];
extern const unsigned int lan969x_fpos[FPOS_LAST];
extern const unsigned int lan969x_fsize[FSIZE_LAST];

static inline bool lan969x_port_is_2g5(int portno)
{
	return portno == 1  || portno == 2  || portno == 3  ||
	       portno == 5  || portno == 6  || portno == 7  ||
	       portno == 10 || portno == 11 || portno == 14 ||
	       portno == 15 || portno == 18 || portno == 19 ||
	       portno == 22 || portno == 23;
}

static inline bool lan969x_port_is_5g(int portno)
{
	return portno == 9 || portno == 13 || portno == 17 ||
	       portno == 21;
}

static inline bool lan969x_port_is_10g(int portno)
{
	return portno == 0  || portno == 4  || portno == 8  ||
	       portno == 12 || portno == 16 || portno == 20 ||
	       portno == 24 || portno == 25 || portno == 26 ||
	       portno == 27;
}

static inline bool lan969x_port_is_25g(int portno)
{
	return false;
}

static inline bool lan969x_port_is_rgmii(int portno)
{
	return portno == 28 || portno == 29;
}

/* lan969x_calendar.c */
int lan969x_dsm_calendar_calc(struct sparx5 *sparx5, u32 taxi,
			      struct sparx5_calendar_data *data);

/* lan969x_rgmii.c */
int lan969x_port_config_rgmii(struct sparx5_port *port,
			      struct sparx5_port_config *conf);

/* lan969x_fdma.c */
int lan969x_fdma_init(struct sparx5 *sparx5);
int lan969x_fdma_deinit(struct sparx5 *sparx5);
int lan969x_fdma_napi_poll(struct napi_struct *napi, int weight);
int lan969x_fdma_xmit(struct sparx5 *sparx5, u32 *ifh, struct sk_buff *skb,
		      struct net_device *dev);

#endif
