/* Encapsulate basic setting changes and retrieval on Hermes hardware
 *
 * See copyright notice in main.c
 */
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/if_arp.h>
#include <linux/ieee80211.h>
#include <linux/wireless.h>
#include <net/cfg80211.h>
#include "hermes.h"
#include "hermes_rid.h"
#include "orinoco.h"

#include "hw.h"

#define SYMBOL_MAX_VER_LEN	(14)

/* Symbol firmware has a bug allocating buffers larger than this */
#define TX_NICBUF_SIZE_BUG	1585

/********************************************************************/
/* Data tables                                                      */
/********************************************************************/

/* This tables gives the actual meanings of the bitrate IDs returned
 * by the firmware. */
static const struct {
	int bitrate; /* in 100s of kilobits */
	int automatic;
	u16 agere_txratectrl;
	u16 intersil_txratectrl;
} bitrate_table[] = {
	{110, 1,  3, 15}, /* Entry 0 is the default */
	{10,  0,  1,  1},
	{10,  1,  1,  1},
	{20,  0,  2,  2},
	{20,  1,  6,  3},
	{55,  0,  4,  4},
	{55,  1,  7,  7},
	{110, 0,  5,  8},
};
#define BITRATE_TABLE_SIZE ARRAY_SIZE(bitrate_table)

/* Firmware version encoding */
struct comp_id {
	u16 id, variant, major, minor;
} __packed;

static inline enum fwtype determine_firmware_type(struct comp_id *nic_id)
{
	if (nic_id->id < 0x8000)
		return FIRMWARE_TYPE_AGERE;
	else if (nic_id->id == 0x8000 && nic_id->major == 0)
		return FIRMWARE_TYPE_SYMBOL;
	else
		return FIRMWARE_TYPE_INTERSIL;
}

/* Set priv->firmware type, determine firmware properties
 * This function can be called before we have registerred with netdev,
 * so all errors go out with dev_* rather than printk
 *
 * If non-NULL stores a firmware description in fw_name.
 * If non-NULL stores a HW version in hw_ver
 *
 * These are output via generic cfg80211 ethtool support.
 */
int determine_fw_capabilities(struct orinoco_private *priv,
			      char *fw_name, size_t fw_name_len,
			      u32 *hw_ver)
{
	struct device *dev = priv->dev;
	struct hermes *hw = &priv->hw;
	int err;
	struct comp_id nic_id, sta_id;
	unsigned int firmver;
	char tmp[SYMBOL_MAX_VER_LEN + 1] __attribute__((aligned(2)));

	/* Get the hardware version */
	err = HERMES_READ_RECORD(hw, USER_BAP, HERMES_RID_NICID, &nic_id);
	if (err) {
		dev_err(dev, "Cannot read hardware identity: error %d\n",
			err);
		return err;
	}

	le16_to_cpus(&nic_id.id);
	le16_to_cpus(&nic_id.variant);
	le16_to_cpus(&nic_id.major);
	le16_to_cpus(&nic_id.minor);
	dev_info(dev, "Hardware identity %04x:%04x:%04x:%04x\n",
		 nic_id.id, nic_id.variant, nic_id.major, nic_id.minor);

	if (hw_ver)
		*hw_ver = (((nic_id.id & 0xff) << 24) |
			   ((nic_id.variant & 0xff) << 16) |
			   ((nic_id.major & 0xff) << 8) |
			   (nic_id.minor & 0xff));

	priv->firmware_type = determine_firmware_type(&nic_id);

	/* Get the firmware version */
	err = HERMES_READ_RECORD(hw, USER_BAP, HERMES_RID_STAID, &sta_id);
	if (err) {
		dev_err(dev, "Cannot read station identity: error %d\n",
			err);
		return err;
	}

	le16_to_cpus(&sta_id.id);
	le16_to_cpus(&sta_id.variant);
	le16_to_cpus(&sta_id.major);
	le16_to_cpus(&sta_id.minor);
	dev_info(dev, "Station identity  %04x:%04x:%04x:%04x\n",
		 sta_id.id, sta_id.variant, sta_id.major, sta_id.minor);

	switch (sta_id.id) {
	case 0x15:
		dev_err(dev, "Primary firmware is active\n");
		return -ENODEV;
	case 0x14b:
		dev_err(dev, "Tertiary firmware is active\n");
		return -ENODEV;
	case 0x1f:	/* Intersil, Agere, Symbol Spectrum24 */
	case 0x21:	/* Symbol Spectrum24 Trilogy */
		break;
	default:
		dev_notice(dev, "Unknown station ID, please report\n");
		break;
	}

	/* Default capabilities */
	priv->has_sensitivity = 1;
	priv->has_mwo = 0;
	priv->has_preamble = 0;
	priv->has_port3 = 1;
	priv->has_ibss = 1;
	priv->has_wep = 0;
	priv->has_big_wep = 0;
	priv->has_alt_txcntl = 0;
	priv->has_ext_scan = 0;
	priv->has_wpa = 0;
	priv->do_fw_download = 0;

	/* Determine capabilities from the firmware version */
	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_AGERE:
		/* Lucent Wavelan IEEE, Lucent Orinoco, Cabletron RoamAbout,
		   ELSA, Melco, HP, IBM, Dell 1150, Compaq 110/210 */
		if (fw_name)
			snprintf(fw_name, fw_name_len, "Lucent/Agere %d.%02d",
				 sta_id.major, sta_id.minor);

		firmver = ((unsigned long)sta_id.major << 16) | sta_id.minor;

		priv->has_ibss = (firmver >= 0x60006);
		priv->has_wep = (firmver >= 0x40020);
		priv->has_big_wep = 1; /* FIXME: this is wrong - how do we tell
					  Gold cards from the others? */
		priv->has_mwo = (firmver >= 0x60000);
		priv->has_pm = (firmver >= 0x40020); /* Don't work in 7.52 ? */
		priv->ibss_port = 1;
		priv->has_hostscan = (firmver >= 0x8000a);
		priv->do_fw_download = 1;
		priv->broken_monitor = (firmver >= 0x80000);
		priv->has_alt_txcntl = (firmver >= 0x90000); /* All 9.x ? */
		priv->has_ext_scan = (firmver >= 0x90000); /* All 9.x ? */
		priv->has_wpa = (firmver >= 0x9002a);
		/* Tested with Agere firmware :
		 *	1.16 ; 4.08 ; 4.52 ; 6.04 ; 6.16 ; 7.28 => Jean II
		 * Tested CableTron firmware : 4.32 => Anton */
		break;
	case FIRMWARE_TYPE_SYMBOL:
		/* Symbol , 3Com AirConnect, Intel, Ericsson WLAN */
		/* Intel MAC : 00:02:B3:* */
		/* 3Com MAC : 00:50:DA:* */
		memset(tmp, 0, sizeof(tmp));
		/* Get the Symbol firmware version */
		err = hw->ops->read_ltv(hw, USER_BAP,
					HERMES_RID_SECONDARYVERSION_SYMBOL,
					SYMBOL_MAX_VER_LEN, NULL, &tmp);
		if (err) {
			dev_warn(dev, "Error %d reading Symbol firmware info. "
				 "Wildly guessing capabilities...\n", err);
			firmver = 0;
			tmp[0] = '\0';
		} else {
			/* The firmware revision is a string, the format is
			 * something like : "V2.20-01".
			 * Quick and dirty parsing... - Jean II
			 */
			firmver = ((tmp[1] - '0') << 16)
				| ((tmp[3] - '0') << 12)
				| ((tmp[4] - '0') << 8)
				| ((tmp[6] - '0') << 4)
				| (tmp[7] - '0');

			tmp[SYMBOL_MAX_VER_LEN] = '\0';
		}

		if (fw_name)
			snprintf(fw_name, fw_name_len, "Symbol %s", tmp);

		priv->has_ibss = (firmver >= 0x20000);
		priv->has_wep = (firmver >= 0x15012);
		priv->has_big_wep = (firmver >= 0x20000);
		priv->has_pm = (firmver >= 0x20000 && firmver < 0x22000) ||
			       (firmver >= 0x29000 && firmver < 0x30000) ||
			       firmver >= 0x31000;
		priv->has_preamble = (firmver >= 0x20000);
		priv->ibss_port = 4;

		/* Symbol firmware is found on various cards, but
		 * there has been no attempt to check firmware
		 * download on non-spectrum_cs based cards.
		 *
		 * Given that the Agere firmware download works
		 * differently, we should avoid doing a firmware
		 * download with the Symbol algorithm on non-spectrum
		 * cards.
		 *
		 * For now we can identify a spectrum_cs based card
		 * because it has a firmware reset function.
		 */
		priv->do_fw_download = (priv->stop_fw != NULL);

		priv->broken_disableport = (firmver == 0x25013) ||
				(firmver >= 0x30000 && firmver <= 0x31000);
		priv->has_hostscan = (firmver >= 0x31001) ||
				     (firmver >= 0x29057 && firmver < 0x30000);
		/* Tested with Intel firmware : 0x20015 => Jean II */
		/* Tested with 3Com firmware : 0x15012 & 0x22001 => Jean II */
		break;
	case FIRMWARE_TYPE_INTERSIL:
		/* D-Link, Linksys, Adtron, ZoomAir, and many others...
		 * Samsung, Compaq 100/200 and Proxim are slightly
		 * different and less well tested */
		/* D-Link MAC : 00:40:05:* */
		/* Addtron MAC : 00:90:D1:* */
		if (fw_name)
			snprintf(fw_name, fw_name_len, "Intersil %d.%d.%d",
				 sta_id.major, sta_id.minor, sta_id.variant);

		firmver = ((unsigned long)sta_id.major << 16) |
			((unsigned long)sta_id.minor << 8) | sta_id.variant;

		priv->has_ibss = (firmver >= 0x000700); /* FIXME */
		priv->has_big_wep = priv->has_wep = (firmver >= 0x000800);
		priv->has_pm = (firmver >= 0x000700);
		priv->has_hostscan = (firmver >= 0x010301);

		if (firmver >= 0x000800)
			priv->ibss_port = 0;
		else {
			dev_notice(dev, "Intersil firmware earlier than v0.8.x"
				   " - several features not supported\n");
			priv->ibss_port = 1;
		}
		break;
	}
	if (fw_name)
		dev_info(dev, "Firmware determined as %s\n", fw_name);

#ifndef CONFIG_HERMES_PRISM
	if (priv->firmware_type == FIRMWARE_TYPE_INTERSIL) {
		dev_err(dev, "Support for Prism chipset is not enabled\n");
		return -ENODEV;
	}
#endif

	return 0;
}

/* Read settings from EEPROM into our private structure.
 * MAC address gets dropped into callers buffer
 * Can be called before netdev registration.
 */
int orinoco_hw_read_card_settings(struct orinoco_private *priv, u8 *dev_addr)
{
	struct device *dev = priv->dev;
	struct hermes_idstring nickbuf;
	struct hermes *hw = &priv->hw;
	int len;
	int err;
	u16 reclen;

	/* Get the MAC address */
	err = hw->ops->read_ltv(hw, USER_BAP, HERMES_RID_CNFOWNMACADDR,
				ETH_ALEN, NULL, dev_addr);
	if (err) {
		dev_warn(dev, "Failed to read MAC address!\n");
		goto out;
	}

	dev_dbg(dev, "MAC address %pM\n", dev_addr);

	/* Get the station name */
	err = hw->ops->read_ltv(hw, USER_BAP, HERMES_RID_CNFOWNNAME,
				sizeof(nickbuf), &reclen, &nickbuf);
	if (err) {
		dev_err(dev, "failed to read station name\n");
		goto out;
	}
	if (nickbuf.len)
		len = min(IW_ESSID_MAX_SIZE, (int)le16_to_cpu(nickbuf.len));
	else
		len = min(IW_ESSID_MAX_SIZE, 2 * reclen);
	memcpy(priv->nick, &nickbuf.val, len);
	priv->nick[len] = '\0';

	dev_dbg(dev, "Station name \"%s\"\n", priv->nick);

	/* Get allowed channels */
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CHANNELLIST,
				  &priv->channel_mask);
	if (err) {
		dev_err(dev, "Failed to read channel list!\n");
		goto out;
	}

	/* Get initial AP density */
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNFSYSTEMSCALE,
				  &priv->ap_density);
	if (err || priv->ap_density < 1 || priv->ap_density > 3)
		priv->has_sensitivity = 0;

	/* Get initial RTS threshold */
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CNFRTSTHRESHOLD,
				  &priv->rts_thresh);
	if (err) {
		dev_err(dev, "Failed to read RTS threshold!\n");
		goto out;
	}

	/* Get initial fragmentation settings */
	if (priv->has_mwo)
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CNFMWOROBUST_AGERE,
					  &priv->mwo_robust);
	else
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CNFFRAGMENTATIONTHRESHOLD,
					  &priv->frag_thresh);
	if (err) {
		dev_err(dev, "Failed to read fragmentation settings!\n");
		goto out;
	}

	/* Power management setup */
	if (priv->has_pm) {
		priv->pm_on = 0;
		priv->pm_mcast = 1;
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CNFMAXSLEEPDURATION,
					  &priv->pm_period);
		if (err) {
			dev_err(dev, "Failed to read power management "
				"period!\n");
			goto out;
		}
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CNFPMHOLDOVERDURATION,
					  &priv->pm_timeout);
		if (err) {
			dev_err(dev, "Failed to read power management "
				"timeout!\n");
			goto out;
		}
	}

	/* Preamble setup */
	if (priv->has_preamble) {
		err = hermes_read_wordrec(hw, USER_BAP,
					  HERMES_RID_CNFPREAMBLE_SYMBOL,
					  &priv->preamble);
		if (err) {
			dev_err(dev, "Failed to read preamble setup\n");
			goto out;
		}
	}

	/* Retry settings */
	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_SHORTRETRYLIMIT,
				  &priv->short_retry_limit);
	if (err) {
		dev_err(dev, "Failed to read short retry limit\n");
		goto out;
	}

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_LONGRETRYLIMIT,
				  &priv->long_retry_limit);
	if (err) {
		dev_err(dev, "Failed to read long retry limit\n");
		goto out;
	}

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_MAXTRANSMITLIFETIME,
				  &priv->retry_lifetime);
	if (err) {
		dev_err(dev, "Failed to read max retry lifetime\n");
		goto out;
	}

out:
	return err;
}

/* Can be called before netdev registration */
int orinoco_hw_allocate_fid(struct orinoco_private *priv)
{
	struct device *dev = priv->dev;
	struct hermes *hw = &priv->hw;
	int err;

	err = hw->ops->allocate(hw, priv->nicbuf_size, &priv->txfid);
	if (err == -EIO && priv->nicbuf_size > TX_NICBUF_SIZE_BUG) {
		/* Try workaround for old Symbol firmware bug */
		priv->nicbuf_size = TX_NICBUF_SIZE_BUG;
		err = hw->ops->allocate(hw, priv->nicbuf_size, &priv->txfid);

		dev_warn(dev, "Firmware ALLOC bug detected "
			 "(old Symbol firmware?). Work around %s\n",
			 err ? "failed!" : "ok.");
	}

	return err;
}

int orinoco_get_bitratemode(int bitrate, int automatic)
{
	int ratemode = -1;
	int i;

	if ((bitrate != 10) && (bitrate != 20) &&
	    (bitrate != 55) && (bitrate != 110))
		return ratemode;

	for (i = 0; i < BITRATE_TABLE_SIZE; i++) {
		if ((bitrate_table[i].bitrate == bitrate) &&
		    (bitrate_table[i].automatic == automatic)) {
			ratemode = i;
			break;
		}
	}
	return ratemode;
}

void orinoco_get_ratemode_cfg(int ratemode, int *bitrate, int *automatic)
{
	BUG_ON((ratemode < 0) || (ratemode >= BITRATE_TABLE_SIZE));

	*bitrate = bitrate_table[ratemode].bitrate * 100000;
	*automatic = bitrate_table[ratemode].automatic;
}

int orinoco_hw_program_rids(struct orinoco_private *priv)
{
	struct net_device *dev = priv->ndev;
	struct wireless_dev *wdev = netdev_priv(dev);
	struct hermes *hw = &priv->hw;
	int err;
	struct hermes_idstring idbuf;

	/* Set the MAC address */
	err = hw->ops->write_ltv(hw, USER_BAP, HERMES_RID_CNFOWNMACADDR,
				 HERMES_BYTES_TO_RECLEN(ETH_ALEN),
				 dev->dev_addr);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting MAC address\n",
		       dev->name, err);
		return err;
	}

	/* Set up the link mode */
	err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNFPORTTYPE,
				   priv->port_type);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting port type\n",
		       dev->name, err);
		return err;
	}
	/* Set the channel/frequency */
	if (priv->channel != 0 && priv->iw_mode != NL80211_IFTYPE_STATION) {
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFOWNCHANNEL,
					   priv->channel);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting channel %d\n",
			       dev->name, err, priv->channel);
			return err;
		}
	}

	if (priv->has_ibss) {
		u16 createibss;

		if ((strlen(priv->desired_essid) == 0) && (priv->createibss)) {
			printk(KERN_WARNING "%s: This firmware requires an "
			       "ESSID in IBSS-Ad-Hoc mode.\n", dev->name);
			/* With wvlan_cs, in this case, we would crash.
			 * hopefully, this driver will behave better...
			 * Jean II */
			createibss = 0;
		} else {
			createibss = priv->createibss;
		}

		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFCREATEIBSS,
					   createibss);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting CREATEIBSS\n",
			       dev->name, err);
			return err;
		}
	}

	/* Set the desired BSSID */
	err = __orinoco_hw_set_wap(priv);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting AP address\n",
		       dev->name, err);
		return err;
	}

	/* Set the desired ESSID */
	idbuf.len = cpu_to_le16(strlen(priv->desired_essid));
	memcpy(&idbuf.val, priv->desired_essid, sizeof(idbuf.val));
	/* WinXP wants partner to configure OWNSSID even in IBSS mode. (jimc) */
	err = hw->ops->write_ltv(hw, USER_BAP, HERMES_RID_CNFOWNSSID,
			HERMES_BYTES_TO_RECLEN(strlen(priv->desired_essid) + 2),
			&idbuf);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting OWNSSID\n",
		       dev->name, err);
		return err;
	}
	err = hw->ops->write_ltv(hw, USER_BAP, HERMES_RID_CNFDESIREDSSID,
			HERMES_BYTES_TO_RECLEN(strlen(priv->desired_essid) + 2),
			&idbuf);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting DESIREDSSID\n",
		       dev->name, err);
		return err;
	}

	/* Set the station name */
	idbuf.len = cpu_to_le16(strlen(priv->nick));
	memcpy(&idbuf.val, priv->nick, sizeof(idbuf.val));
	err = hw->ops->write_ltv(hw, USER_BAP, HERMES_RID_CNFOWNNAME,
				 HERMES_BYTES_TO_RECLEN(strlen(priv->nick) + 2),
				 &idbuf);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting nickname\n",
		       dev->name, err);
		return err;
	}

	/* Set AP density */
	if (priv->has_sensitivity) {
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFSYSTEMSCALE,
					   priv->ap_density);
		if (err) {
			printk(KERN_WARNING "%s: Error %d setting SYSTEMSCALE. "
			       "Disabling sensitivity control\n",
			       dev->name, err);

			priv->has_sensitivity = 0;
		}
	}

	/* Set RTS threshold */
	err = hermes_write_wordrec(hw, USER_BAP, HERMES_RID_CNFRTSTHRESHOLD,
				   priv->rts_thresh);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting RTS threshold\n",
		       dev->name, err);
		return err;
	}

	/* Set fragmentation threshold or MWO robustness */
	if (priv->has_mwo)
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFMWOROBUST_AGERE,
					   priv->mwo_robust);
	else
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFFRAGMENTATIONTHRESHOLD,
					   priv->frag_thresh);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting fragmentation\n",
		       dev->name, err);
		return err;
	}

	/* Set bitrate */
	err = __orinoco_hw_set_bitrate(priv);
	if (err) {
		printk(KERN_ERR "%s: Error %d setting bitrate\n",
		       dev->name, err);
		return err;
	}

	/* Set power management */
	if (priv->has_pm) {
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFPMENABLED,
					   priv->pm_on);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting up PM\n",
			       dev->name, err);
			return err;
		}

		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFMULTICASTRECEIVE,
					   priv->pm_mcast);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting up PM\n",
			       dev->name, err);
			return err;
		}
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFMAXSLEEPDURATION,
					   priv->pm_period);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting up PM\n",
			       dev->name, err);
			return err;
		}
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFPMHOLDOVERDURATION,
					   priv->pm_timeout);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting up PM\n",
			       dev->name, err);
			return err;
		}
	}

	/* Set preamble - only for Symbol so far... */
	if (priv->has_preamble) {
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFPREAMBLE_SYMBOL,
					   priv->preamble);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting preamble\n",
			       dev->name, err);
			return err;
		}
	}

	/* Set up encryption */
	if (priv->has_wep || priv->has_wpa) {
		err = __orinoco_hw_setup_enc(priv);
		if (err) {
			printk(KERN_ERR "%s: Error %d activating encryption\n",
			       dev->name, err);
			return err;
		}
	}

	if (priv->iw_mode == NL80211_IFTYPE_MONITOR) {
		/* Enable monitor mode */
		dev->type = ARPHRD_IEEE80211;
		err = hw->ops->cmd_wait(hw, HERMES_CMD_TEST |
					    HERMES_TEST_MONITOR, 0, NULL);
	} else {
		/* Disable monitor mode */
		dev->type = ARPHRD_ETHER;
		err = hw->ops->cmd_wait(hw, HERMES_CMD_TEST |
					    HERMES_TEST_STOP, 0, NULL);
	}
	if (err)
		return err;

	/* Reset promiscuity / multicast*/
	priv->promiscuous = 0;
	priv->mc_count = 0;

	/* Record mode change */
	wdev->iftype = priv->iw_mode;

	return 0;
}

/* Get tsc from the firmware */
int orinoco_hw_get_tkip_iv(struct orinoco_private *priv, int key, u8 *tsc)
{
	struct hermes *hw = &priv->hw;
	int err = 0;
	u8 tsc_arr[4][ORINOCO_SEQ_LEN];

	if ((key < 0) || (key >= 4))
		return -EINVAL;

	err = hw->ops->read_ltv(hw, USER_BAP, HERMES_RID_CURRENT_TKIP_IV,
				sizeof(tsc_arr), NULL, &tsc_arr);
	if (!err)
		memcpy(tsc, &tsc_arr[key][0], sizeof(tsc_arr[0]));

	return err;
}

int __orinoco_hw_set_bitrate(struct orinoco_private *priv)
{
	struct hermes *hw = &priv->hw;
	int ratemode = priv->bitratemode;
	int err = 0;

	if (ratemode >= BITRATE_TABLE_SIZE) {
		printk(KERN_ERR "%s: BUG: Invalid bitrate mode %d\n",
		       priv->ndev->name, ratemode);
		return -EINVAL;
	}

	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_AGERE:
		err = hermes_write_wordrec(hw, USER_BAP,
				HERMES_RID_CNFTXRATECONTROL,
				bitrate_table[ratemode].agere_txratectrl);
		break;
	case FIRMWARE_TYPE_INTERSIL:
	case FIRMWARE_TYPE_SYMBOL:
		err = hermes_write_wordrec(hw, USER_BAP,
				HERMES_RID_CNFTXRATECONTROL,
				bitrate_table[ratemode].intersil_txratectrl);
		break;
	default:
		BUG();
	}

	return err;
}

int orinoco_hw_get_act_bitrate(struct orinoco_private *priv, int *bitrate)
{
	struct hermes *hw = &priv->hw;
	int i;
	int err = 0;
	u16 val;

	err = hermes_read_wordrec(hw, USER_BAP,
				  HERMES_RID_CURRENTTXRATE, &val);
	if (err)
		return err;

	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_AGERE: /* Lucent style rate */
		/* Note : in Lucent firmware, the return value of
		 * HERMES_RID_CURRENTTXRATE is the bitrate in Mb/s,
		 * and therefore is totally different from the
		 * encoding of HERMES_RID_CNFTXRATECONTROL.
		 * Don't forget that 6Mb/s is really 5.5Mb/s */
		if (val == 6)
			*bitrate = 5500000;
		else
			*bitrate = val * 1000000;
		break;
	case FIRMWARE_TYPE_INTERSIL: /* Intersil style rate */
	case FIRMWARE_TYPE_SYMBOL: /* Symbol style rate */
		for (i = 0; i < BITRATE_TABLE_SIZE; i++)
			if (bitrate_table[i].intersil_txratectrl == val) {
				*bitrate = bitrate_table[i].bitrate * 100000;
				break;
			}

		if (i >= BITRATE_TABLE_SIZE) {
			printk(KERN_INFO "%s: Unable to determine current bitrate (0x%04hx)\n",
			       priv->ndev->name, val);
			err = -EIO;
		}

		break;
	default:
		BUG();
	}

	return err;
}

/* Set fixed AP address */
int __orinoco_hw_set_wap(struct orinoco_private *priv)
{
	int roaming_flag;
	int err = 0;
	struct hermes *hw = &priv->hw;

	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_AGERE:
		/* not supported */
		break;
	case FIRMWARE_TYPE_INTERSIL:
		if (priv->bssid_fixed)
			roaming_flag = 2;
		else
			roaming_flag = 1;

		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFROAMINGMODE,
					   roaming_flag);
		break;
	case FIRMWARE_TYPE_SYMBOL:
		err = HERMES_WRITE_RECORD(hw, USER_BAP,
					  HERMES_RID_CNFMANDATORYBSSID_SYMBOL,
					  &priv->desired_bssid);
		break;
	}
	return err;
}

/* Change the WEP keys and/or the current keys.  Can be called
 * either from __orinoco_hw_setup_enc() or directly from
 * orinoco_ioctl_setiwencode().  In the later case the association
 * with the AP is not broken (if the firmware can handle it),
 * which is needed for 802.1x implementations. */
int __orinoco_hw_setup_wepkeys(struct orinoco_private *priv)
{
	struct hermes *hw = &priv->hw;
	int err = 0;
	int i;

	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_AGERE:
	{
		struct orinoco_key keys[ORINOCO_MAX_KEYS];

		memset(&keys, 0, sizeof(keys));
		for (i = 0; i < ORINOCO_MAX_KEYS; i++) {
			int len = min(priv->keys[i].key_len,
				      ORINOCO_MAX_KEY_SIZE);
			memcpy(&keys[i].data, priv->keys[i].key, len);
			if (len > SMALL_KEY_SIZE)
				keys[i].len = cpu_to_le16(LARGE_KEY_SIZE);
			else if (len > 0)
				keys[i].len = cpu_to_le16(SMALL_KEY_SIZE);
			else
				keys[i].len = cpu_to_le16(0);
		}

		err = HERMES_WRITE_RECORD(hw, USER_BAP,
					  HERMES_RID_CNFWEPKEYS_AGERE,
					  &keys);
		if (err)
			return err;
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFTXKEY_AGERE,
					   priv->tx_key);
		if (err)
			return err;
		break;
	}
	case FIRMWARE_TYPE_INTERSIL:
	case FIRMWARE_TYPE_SYMBOL:
		{
			int keylen;

			/* Force uniform key length to work around
			 * firmware bugs */
			keylen = priv->keys[priv->tx_key].key_len;

			if (keylen > LARGE_KEY_SIZE) {
				printk(KERN_ERR "%s: BUG: Key %d has oversize length %d.\n",
				       priv->ndev->name, priv->tx_key, keylen);
				return -E2BIG;
			} else if (keylen > SMALL_KEY_SIZE)
				keylen = LARGE_KEY_SIZE;
			else if (keylen > 0)
				keylen = SMALL_KEY_SIZE;
			else
				keylen = 0;

			/* Write all 4 keys */
			for (i = 0; i < ORINOCO_MAX_KEYS; i++) {
				u8 key[LARGE_KEY_SIZE] = { 0 };

				memcpy(key, priv->keys[i].key,
				       priv->keys[i].key_len);

				err = hw->ops->write_ltv(hw, USER_BAP,
						HERMES_RID_CNFDEFAULTKEY0 + i,
						HERMES_BYTES_TO_RECLEN(keylen),
						key);
				if (err)
					return err;
			}

			/* Write the index of the key used in transmission */
			err = hermes_write_wordrec(hw, USER_BAP,
						HERMES_RID_CNFWEPDEFAULTKEYID,
						priv->tx_key);
			if (err)
				return err;
		}
		break;
	}

	return 0;
}

int __orinoco_hw_setup_enc(struct orinoco_private *priv)
{
	struct hermes *hw = &priv->hw;
	int err = 0;
	int master_wep_flag;
	int auth_flag;
	int enc_flag;

	/* Setup WEP keys */
	if (priv->encode_alg == ORINOCO_ALG_WEP)
		__orinoco_hw_setup_wepkeys(priv);

	if (priv->wep_restrict)
		auth_flag = HERMES_AUTH_SHARED_KEY;
	else
		auth_flag = HERMES_AUTH_OPEN;

	if (priv->wpa_enabled)
		enc_flag = 2;
	else if (priv->encode_alg == ORINOCO_ALG_WEP)
		enc_flag = 1;
	else
		enc_flag = 0;

	switch (priv->firmware_type) {
	case FIRMWARE_TYPE_AGERE: /* Agere style WEP */
		if (priv->encode_alg == ORINOCO_ALG_WEP) {
			/* Enable the shared-key authentication. */
			err = hermes_write_wordrec(hw, USER_BAP,
					HERMES_RID_CNFAUTHENTICATION_AGERE,
					auth_flag);
		}
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFWEPENABLED_AGERE,
					   enc_flag);
		if (err)
			return err;

		if (priv->has_wpa) {
			/* Set WPA key management */
			err = hermes_write_wordrec(hw, USER_BAP,
				  HERMES_RID_CNFSETWPAAUTHMGMTSUITE_AGERE,
				  priv->key_mgmt);
			if (err)
				return err;
		}

		break;

	case FIRMWARE_TYPE_INTERSIL: /* Intersil style WEP */
	case FIRMWARE_TYPE_SYMBOL: /* Symbol style WEP */
		if (priv->encode_alg == ORINOCO_ALG_WEP) {
			if (priv->wep_restrict ||
			    (priv->firmware_type == FIRMWARE_TYPE_SYMBOL))
				master_wep_flag = HERMES_WEP_PRIVACY_INVOKED |
						  HERMES_WEP_EXCL_UNENCRYPTED;
			else
				master_wep_flag = HERMES_WEP_PRIVACY_INVOKED;

			err = hermes_write_wordrec(hw, USER_BAP,
						   HERMES_RID_CNFAUTHENTICATION,
						   auth_flag);
			if (err)
				return err;
		} else
			master_wep_flag = 0;

		if (priv->iw_mode == NL80211_IFTYPE_MONITOR)
			master_wep_flag |= HERMES_WEP_HOST_DECRYPT;

		/* Master WEP setting : on/off */
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFWEPFLAGS_INTERSIL,
					   master_wep_flag);
		if (err)
			return err;

		break;
	}

	return 0;
}

/* key must be 32 bytes, including the tx and rx MIC keys.
 * rsc must be NULL or up to 8 bytes
 * tsc must be NULL or up to 8 bytes
 */
int __orinoco_hw_set_tkip_key(struct orinoco_private *priv, int key_idx,
			      int set_tx, const u8 *key, const u8 *rsc,
			      size_t rsc_len, const u8 *tsc, size_t tsc_len)
{
	struct {
		__le16 idx;
		u8 rsc[ORINOCO_SEQ_LEN];
		u8 key[TKIP_KEYLEN];
		u8 tx_mic[MIC_KEYLEN];
		u8 rx_mic[MIC_KEYLEN];
		u8 tsc[ORINOCO_SEQ_LEN];
	} __packed buf;
	struct hermes *hw = &priv->hw;
	int ret;
	int err;
	int k;
	u16 xmitting;

	key_idx &= 0x3;

	if (set_tx)
		key_idx |= 0x8000;

	buf.idx = cpu_to_le16(key_idx);
	memcpy(buf.key, key,
	       sizeof(buf.key) + sizeof(buf.tx_mic) + sizeof(buf.rx_mic));

	if (rsc_len > sizeof(buf.rsc))
		rsc_len = sizeof(buf.rsc);

	if (tsc_len > sizeof(buf.tsc))
		tsc_len = sizeof(buf.tsc);

	memset(buf.rsc, 0, sizeof(buf.rsc));
	memset(buf.tsc, 0, sizeof(buf.tsc));

	if (rsc != NULL)
		memcpy(buf.rsc, rsc, rsc_len);

	if (tsc != NULL)
		memcpy(buf.tsc, tsc, tsc_len);
	else
		buf.tsc[4] = 0x10;

	/* Wait up to 100ms for tx queue to empty */
	for (k = 100; k > 0; k--) {
		udelay(1000);
		ret = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_TXQUEUEEMPTY,
					  &xmitting);
		if (ret || !xmitting)
			break;
	}

	if (k == 0)
		ret = -ETIMEDOUT;

	err = HERMES_WRITE_RECORD(hw, USER_BAP,
				  HERMES_RID_CNFADDDEFAULTTKIPKEY_AGERE,
				  &buf);

	return ret ? ret : err;
}

int orinoco_clear_tkip_key(struct orinoco_private *priv, int key_idx)
{
	struct hermes *hw = &priv->hw;
	int err;

	err = hermes_write_wordrec(hw, USER_BAP,
				   HERMES_RID_CNFREMDEFAULTTKIPKEY_AGERE,
				   key_idx);
	if (err)
		printk(KERN_WARNING "%s: Error %d clearing TKIP key %d\n",
		       priv->ndev->name, err, key_idx);
	return err;
}

int __orinoco_hw_set_multicast_list(struct orinoco_private *priv,
				    struct net_device *dev,
				    int mc_count, int promisc)
{
	struct hermes *hw = &priv->hw;
	int err = 0;

	if (promisc != priv->promiscuous) {
		err = hermes_write_wordrec(hw, USER_BAP,
					   HERMES_RID_CNFPROMISCUOUSMODE,
					   promisc);
		if (err) {
			printk(KERN_ERR "%s: Error %d setting PROMISCUOUSMODE to 1.\n",
			       priv->ndev->name, err);
		} else
			priv->promiscuous = promisc;
	}

	/* If we're not in promiscuous mode, then we need to set the
	 * group address if either we want to multicast, or if we were
	 * multicasting and want to stop */
	if (!promisc && (mc_count || priv->mc_count)) {
		struct netdev_hw_addr *ha;
		struct hermes_multicast mclist;
		int i = 0;

		netdev_for_each_mc_addr(ha, dev) {
			if (i == mc_count)
				break;
			memcpy(mclist.addr[i++], ha->addr, ETH_ALEN);
		}

		err = hw->ops->write_ltv(hw, USER_BAP,
				   HERMES_RID_CNFGROUPADDRESSES,
				   HERMES_BYTES_TO_RECLEN(mc_count * ETH_ALEN),
				   &mclist);
		if (err)
			printk(KERN_ERR "%s: Error %d setting multicast list.\n",
			       priv->ndev->name, err);
		else
			priv->mc_count = mc_count;
	}
	return err;
}

/* Return : < 0 -> error code ; >= 0 -> length */
int orinoco_hw_get_essid(struct orinoco_private *priv, int *active,
			 char buf[IW_ESSID_MAX_SIZE + 1])
{
	struct hermes *hw = &priv->hw;
	int err = 0;
	struct hermes_idstring essidbuf;
	char *p = (char *)(&essidbuf.val);
	int len;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	if (strlen(priv->desired_essid) > 0) {
		/* We read the desired SSID from the hardware rather
		   than from priv->desired_essid, just in case the
		   firmware is allowed to change it on us. I'm not
		   sure about this */
		/* My guess is that the OWNSSID should always be whatever
		 * we set to the card, whereas CURRENT_SSID is the one that
		 * may change... - Jean II */
		u16 rid;

		*active = 1;

		rid = (priv->port_type == 3) ? HERMES_RID_CNFOWNSSID :
			HERMES_RID_CNFDESIREDSSID;

		err = hw->ops->read_ltv(hw, USER_BAP, rid, sizeof(essidbuf),
					NULL, &essidbuf);
		if (err)
			goto fail_unlock;
	} else {
		*active = 0;

		err = hw->ops->read_ltv(hw, USER_BAP, HERMES_RID_CURRENTSSID,
					sizeof(essidbuf), NULL, &essidbuf);
		if (err)
			goto fail_unlock;
	}

	len = le16_to_cpu(essidbuf.len);
	BUG_ON(len > IW_ESSID_MAX_SIZE);

	memset(buf, 0, IW_ESSID_MAX_SIZE);
	memcpy(buf, p, len);
	err = len;

 fail_unlock:
	orinoco_unlock(priv, &flags);

	return err;
}

int orinoco_hw_get_freq(struct orinoco_private *priv)
{
	struct hermes *hw = &priv->hw;
	int err = 0;
	u16 channel;
	int freq = 0;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	err = hermes_read_wordrec(hw, USER_BAP, HERMES_RID_CURRENTCHANNEL,
				  &channel);
	if (err)
		goto out;

	/* Intersil firmware 1.3.5 returns 0 when the interface is down */
	if (channel == 0) {
		err = -EBUSY;
		goto out;
	}

	if ((channel < 1) || (channel > NUM_CHANNELS)) {
		printk(KERN_WARNING "%s: Channel out of range (%d)!\n",
		       priv->ndev->name, channel);
		err = -EBUSY;
		goto out;

	}
	freq = ieee80211_channel_to_frequency(channel, NL80211_BAND_2GHZ);

 out:
	orinoco_unlock(priv, &flags);

	if (err > 0)
		err = -EBUSY;
	return err ? err : freq;
}

int orinoco_hw_get_bitratelist(struct orinoco_private *priv,
			       int *numrates, s32 *rates, int max)
{
	struct hermes *hw = &priv->hw;
	struct hermes_idstring list;
	unsigned char *p = (unsigned char *)&list.val;
	int err = 0;
	int num;
	int i;
	unsigned long flags;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	err = hw->ops->read_ltv(hw, USER_BAP, HERMES_RID_SUPPORTEDDATARATES,
				sizeof(list), NULL, &list);
	orinoco_unlock(priv, &flags);

	if (err)
		return err;

	num = le16_to_cpu(list.len);
	*numrates = num;
	num = min(num, max);

	for (i = 0; i < num; i++)
		rates[i] = (p[i] & 0x7f) * 500000; /* convert to bps */

	return 0;
}

int orinoco_hw_trigger_scan(struct orinoco_private *priv,
			    const struct cfg80211_ssid *ssid)
{
	struct net_device *dev = priv->ndev;
	struct hermes *hw = &priv->hw;
	unsigned long flags;
	int err = 0;

	if (orinoco_lock(priv, &flags) != 0)
		return -EBUSY;

	/* Scanning with port 0 disabled would fail */
	if (!netif_running(dev)) {
		err = -ENETDOWN;
		goto out;
	}

	/* In monitor mode, the scan results are always empty.
	 * Probe responses are passed to the driver as received
	 * frames and could be processed in software. */
	if (priv->iw_mode == NL80211_IFTYPE_MONITOR) {
		err = -EOPNOTSUPP;
		goto out;
	}

	if (priv->has_hostscan) {
		switch (priv->firmware_type) {
		case FIRMWARE_TYPE_SYMBOL:
			err = hermes_write_wordrec(hw, USER_BAP,
						HERMES_RID_CNFHOSTSCAN_SYMBOL,
						HERMES_HOSTSCAN_SYMBOL_ONCE |
						HERMES_HOSTSCAN_SYMBOL_BCAST);
			break;
		case FIRMWARE_TYPE_INTERSIL: {
			__le16 req[3];

			req[0] = cpu_to_le16(0x3fff);	/* All channels */
			req[1] = cpu_to_le16(0x0001);	/* rate 1 Mbps */
			req[2] = 0;			/* Any ESSID */
			err = HERMES_WRITE_RECORD(hw, USER_BAP,
						  HERMES_RID_CNFHOSTSCAN, &req);
			break;
		}
		case FIRMWARE_TYPE_AGERE:
			if (ssid->ssid_len > 0) {
				struct hermes_idstring idbuf;
				size_t len = ssid->ssid_len;

				idbuf.len = cpu_to_le16(len);
				memcpy(idbuf.val, ssid->ssid, len);

				err = hw->ops->write_ltv(hw, USER_BAP,
					       HERMES_RID_CNFSCANSSID_AGERE,
					       HERMES_BYTES_TO_RECLEN(len + 2),
					       &idbuf);
			} else
				err = hermes_write_wordrec(hw, USER_BAP,
						   HERMES_RID_CNFSCANSSID_AGERE,
						   0);	/* Any ESSID */
			if (err)
				break;

			if (priv->has_ext_scan) {
				err = hermes_write_wordrec(hw, USER_BAP,
						HERMES_RID_CNFSCANCHANNELS2GHZ,
						0x7FFF);
				if (err)
					goto out;

				err = hermes_inquire(hw,
						     HERMES_INQ_CHANNELINFO);
			} else
				err = hermes_inquire(hw, HERMES_INQ_SCAN);

			break;
		}
	} else
		err = hermes_inquire(hw, HERMES_INQ_SCAN);

 out:
	orinoco_unlock(priv, &flags);

	return err;
}

/* Disassociate from node with BSSID addr */
int orinoco_hw_disassociate(struct orinoco_private *priv,
			    u8 *addr, u16 reason_code)
{
	struct hermes *hw = &priv->hw;
	int err;

	struct {
		u8 addr[ETH_ALEN];
		__le16 reason_code;
	} __packed buf;

	/* Currently only supported by WPA enabled Agere fw */
	if (!priv->has_wpa)
		return -EOPNOTSUPP;

	memcpy(buf.addr, addr, ETH_ALEN);
	buf.reason_code = cpu_to_le16(reason_code);
	err = HERMES_WRITE_RECORD(hw, USER_BAP,
				  HERMES_RID_CNFDISASSOCIATE,
				  &buf);
	return err;
}

int orinoco_hw_get_current_bssid(struct orinoco_private *priv,
				 u8 *addr)
{
	struct hermes *hw = &priv->hw;
	int err;

	err = hw->ops->read_ltv(hw, USER_BAP, HERMES_RID_CURRENTBSSID,
				ETH_ALEN, NULL, addr);

	return err;
}
