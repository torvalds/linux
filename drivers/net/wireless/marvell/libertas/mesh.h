/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Contains all definitions needed for the Libertas' MESH implementation.
 */
#ifndef _LBS_MESH_H_
#define _LBS_MESH_H_


#include <net/iw_handler.h>
#include <net/lib80211.h>

#include "host.h"
#include "dev.h"

#ifdef CONFIG_LIBERTAS_MESH

struct net_device;

void lbs_init_mesh(struct lbs_private *priv);
void lbs_start_mesh(struct lbs_private *priv);
int lbs_deinit_mesh(struct lbs_private *priv);

void lbs_remove_mesh(struct lbs_private *priv);

static inline bool lbs_mesh_activated(struct lbs_private *priv)
{
	return !!priv->mesh_tlv;
}

int lbs_mesh_set_channel(struct lbs_private *priv, u8 channel);

/* Sending / Receiving */

struct rxpd;
struct txpd;

struct net_device *lbs_mesh_set_dev(struct lbs_private *priv,
	struct net_device *dev, struct rxpd *rxpd);
void lbs_mesh_set_txpd(struct lbs_private *priv,
	struct net_device *dev, struct txpd *txpd);


/* Command handling */

struct cmd_ds_command;
struct cmd_ds_mesh_access;
struct cmd_ds_mesh_config;


/* Ethtool statistics */

struct ethtool_stats;

void lbs_mesh_ethtool_get_stats(struct net_device *dev,
	struct ethtool_stats *stats, uint64_t *data);
int lbs_mesh_ethtool_get_sset_count(struct net_device *dev, int sset);
void lbs_mesh_ethtool_get_strings(struct net_device *dev,
	uint32_t stringset, uint8_t *s);


#else

#define lbs_init_mesh(priv)	do { } while (0)
#define lbs_deinit_mesh(priv)	do { } while (0)
#define lbs_start_mesh(priv)	do { } while (0)
#define lbs_add_mesh(priv)	do { } while (0)
#define lbs_remove_mesh(priv)	do { } while (0)
#define lbs_mesh_set_dev(priv, dev, rxpd) (dev)
#define lbs_mesh_set_txpd(priv, dev, txpd) do { } while (0)
#define lbs_mesh_set_channel(priv, channel) (0)
#define lbs_mesh_activated(priv) (false)

#endif



#endif
