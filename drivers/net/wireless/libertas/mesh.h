/*
 * Contains all definitions needed for the Libertas' MESH implementation.
 */
#ifndef _LBS_MESH_H_
#define _LBS_MESH_H_


#include <net/iw_handler.h>
#include <net/lib80211.h>

#include "host.h"

#ifdef CONFIG_LIBERTAS_MESH

/* Mesh statistics */
struct lbs_mesh_stats {
	u32	fwd_bcast_cnt;		/* Fwd: Broadcast counter */
	u32	fwd_unicast_cnt;	/* Fwd: Unicast counter */
	u32	fwd_drop_ttl;		/* Fwd: TTL zero */
	u32	fwd_drop_rbt;		/* Fwd: Recently Broadcasted */
	u32	fwd_drop_noroute; 	/* Fwd: No route to Destination */
	u32	fwd_drop_nobuf;		/* Fwd: Run out of internal buffers */
	u32	drop_blind;		/* Rx:  Dropped by blinding table */
	u32	tx_failed_cnt;		/* Tx:  Failed transmissions */
};


struct net_device;
struct lbs_private;

int lbs_init_mesh(struct lbs_private *priv);
int lbs_deinit_mesh(struct lbs_private *priv);

void lbs_remove_mesh(struct lbs_private *priv);


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

#define lbs_init_mesh(priv)
#define lbs_deinit_mesh(priv)
#define lbs_add_mesh(priv)
#define lbs_remove_mesh(priv)
#define lbs_mesh_set_dev(priv, dev, rxpd) (dev)
#define lbs_mesh_set_txpd(priv, dev, txpd)
#define lbs_mesh_config(priv, enable, chan)

#endif



#endif
