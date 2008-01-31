#include <linux/netdevice.h>
#include <linux/ethtool.h>
#include <linux/delay.h>

#include "host.h"
#include "decl.h"
#include "defs.h"
#include "dev.h"
#include "join.h"
#include "wext.h"
#include "cmd.h"

static const char * mesh_stat_strings[]= {
			"drop_duplicate_bcast",
			"drop_ttl_zero",
			"drop_no_fwd_route",
			"drop_no_buffers",
			"fwded_unicast_cnt",
			"fwded_bcast_cnt",
			"drop_blind_table",
			"tx_failed_cnt"
};

static void lbs_ethtool_get_drvinfo(struct net_device *dev,
					 struct ethtool_drvinfo *info)
{
	struct lbs_private *priv = (struct lbs_private *) dev->priv;
	char fwver[32];

	lbs_get_fwversion(priv, fwver, sizeof(fwver) - 1);

	strcpy(info->driver, "libertas");
	strcpy(info->version, lbs_driver_version);
	strcpy(info->fw_version, fwver);
}

/* All 8388 parts have 16KiB EEPROM size at the time of writing.
 * In case that changes this needs fixing.
 */
#define LBS_EEPROM_LEN 16384

static int lbs_ethtool_get_eeprom_len(struct net_device *dev)
{
	return LBS_EEPROM_LEN;
}

static int lbs_ethtool_get_eeprom(struct net_device *dev,
                                  struct ethtool_eeprom *eeprom, u8 * bytes)
{
	struct lbs_private *priv = (struct lbs_private *) dev->priv;
	struct lbs_ioctl_regrdwr regctrl;
	char *ptr;
	int ret;

	regctrl.action = 0;
	regctrl.offset = eeprom->offset;
	regctrl.NOB = eeprom->len;

	if (eeprom->offset + eeprom->len > LBS_EEPROM_LEN)
		return -EINVAL;

//      mutex_lock(&priv->mutex);

	priv->prdeeprom = kmalloc(eeprom->len+sizeof(regctrl), GFP_KERNEL);
	if (!priv->prdeeprom)
		return -ENOMEM;
	memcpy(priv->prdeeprom, &regctrl, sizeof(regctrl));

	/* +14 is for action, offset, and NOB in
	 * response */
	lbs_deb_ethtool("action:%d offset: %x NOB: %02x\n",
	       regctrl.action, regctrl.offset, regctrl.NOB);

	ret = lbs_prepare_and_send_command(priv,
				    CMD_802_11_EEPROM_ACCESS,
				    regctrl.action,
				    CMD_OPTION_WAITFORRSP, 0,
				    &regctrl);

	if (ret) {
		if (priv->prdeeprom)
			kfree(priv->prdeeprom);
		goto done;
	}

	mdelay(10);

	ptr = (char *)priv->prdeeprom;

	/* skip the command header, but include the "value" u32 variable */
	ptr = ptr + sizeof(struct lbs_ioctl_regrdwr) - 4;

	/*
	 * Return the result back to the user
	 */
	memcpy(bytes, ptr, eeprom->len);

	if (priv->prdeeprom)
		kfree(priv->prdeeprom);
//	mutex_unlock(&priv->mutex);

	ret = 0;

done:
	lbs_deb_enter_args(LBS_DEB_ETHTOOL, "ret %d", ret);
        return ret;
}

static void lbs_ethtool_get_stats(struct net_device * dev,
				struct ethtool_stats * stats, u64 * data)
{
	struct lbs_private *priv = dev->priv;
	struct cmd_ds_mesh_access mesh_access;
	int ret;

	lbs_deb_enter(LBS_DEB_ETHTOOL);

	/* Get Mesh Statistics */
	ret = lbs_prepare_and_send_command(priv,
			CMD_MESH_ACCESS, CMD_ACT_MESH_GET_STATS,
			CMD_OPTION_WAITFORRSP, 0, &mesh_access);

	if (ret)
		return;

	priv->mstats.fwd_drop_rbt = le32_to_cpu(mesh_access.data[0]);
	priv->mstats.fwd_drop_ttl = le32_to_cpu(mesh_access.data[1]);
	priv->mstats.fwd_drop_noroute = le32_to_cpu(mesh_access.data[2]);
	priv->mstats.fwd_drop_nobuf = le32_to_cpu(mesh_access.data[3]);
	priv->mstats.fwd_unicast_cnt = le32_to_cpu(mesh_access.data[4]);
	priv->mstats.fwd_bcast_cnt = le32_to_cpu(mesh_access.data[5]);
	priv->mstats.drop_blind = le32_to_cpu(mesh_access.data[6]);
	priv->mstats.tx_failed_cnt = le32_to_cpu(mesh_access.data[7]);

	data[0] = priv->mstats.fwd_drop_rbt;
	data[1] = priv->mstats.fwd_drop_ttl;
	data[2] = priv->mstats.fwd_drop_noroute;
	data[3] = priv->mstats.fwd_drop_nobuf;
	data[4] = priv->mstats.fwd_unicast_cnt;
	data[5] = priv->mstats.fwd_bcast_cnt;
	data[6] = priv->mstats.drop_blind;
	data[7] = priv->mstats.tx_failed_cnt;

	lbs_deb_enter(LBS_DEB_ETHTOOL);
}

static int lbs_ethtool_get_sset_count(struct net_device * dev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return MESH_STATS_NUM;
	default:
		return -EOPNOTSUPP;
	}
}

static void lbs_ethtool_get_strings(struct net_device *dev,
					  u32 stringset,
					  u8 * s)
{
	int i;

	lbs_deb_enter(LBS_DEB_ETHTOOL);

	switch (stringset) {
        case ETH_SS_STATS:
		for (i=0; i < MESH_STATS_NUM; i++) {
			memcpy(s + i * ETH_GSTRING_LEN,
					mesh_stat_strings[i],
					ETH_GSTRING_LEN);
		}
		break;
        }
	lbs_deb_enter(LBS_DEB_ETHTOOL);
}

static void lbs_ethtool_get_wol(struct net_device *dev,
				struct ethtool_wolinfo *wol)
{
	struct lbs_private *priv = dev->priv;

	if (priv->wol_criteria == 0xffffffff) {
		/* Interface driver didn't configure wake */
		wol->supported = wol->wolopts = 0;
		return;
	}

	wol->supported = WAKE_UCAST|WAKE_MCAST|WAKE_BCAST|WAKE_PHY;

	if (priv->wol_criteria & EHS_WAKE_ON_UNICAST_DATA)
		wol->wolopts |= WAKE_UCAST;
	if (priv->wol_criteria & EHS_WAKE_ON_MULTICAST_DATA)
		wol->wolopts |= WAKE_MCAST;
	if (priv->wol_criteria & EHS_WAKE_ON_BROADCAST_DATA)
		wol->wolopts |= WAKE_BCAST;
	if (priv->wol_criteria & EHS_WAKE_ON_MAC_EVENT)
		wol->wolopts |= WAKE_PHY;
}

static int lbs_ethtool_set_wol(struct net_device *dev,
			       struct ethtool_wolinfo *wol)
{
	struct lbs_private *priv = dev->priv;
	uint32_t criteria = 0;

	if (priv->wol_criteria == 0xffffffff && wol->wolopts)
		return -EOPNOTSUPP;

	if (wol->wolopts & ~(WAKE_UCAST|WAKE_MCAST|WAKE_BCAST|WAKE_PHY))
		return -EOPNOTSUPP;

	if (wol->wolopts & WAKE_UCAST) criteria |= EHS_WAKE_ON_UNICAST_DATA;
	if (wol->wolopts & WAKE_MCAST) criteria |= EHS_WAKE_ON_MULTICAST_DATA;
	if (wol->wolopts & WAKE_BCAST) criteria |= EHS_WAKE_ON_BROADCAST_DATA;
	if (wol->wolopts & WAKE_PHY)   criteria |= EHS_WAKE_ON_MAC_EVENT;

	return lbs_host_sleep_cfg(priv, criteria);
}

struct ethtool_ops lbs_ethtool_ops = {
	.get_drvinfo = lbs_ethtool_get_drvinfo,
	.get_eeprom =  lbs_ethtool_get_eeprom,
	.get_eeprom_len = lbs_ethtool_get_eeprom_len,
	.get_sset_count = lbs_ethtool_get_sset_count,
	.get_ethtool_stats = lbs_ethtool_get_stats,
	.get_strings = lbs_ethtool_get_strings,
	.get_wol = lbs_ethtool_get_wol,
	.set_wol = lbs_ethtool_set_wol,
};

