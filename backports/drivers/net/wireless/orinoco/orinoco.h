/* orinoco.h
 *
 * Common definitions to all pieces of the various orinoco
 * drivers
 */

#ifndef _ORINOCO_H
#define _ORINOCO_H

#define DRIVER_VERSION "0.15"

#include <linux/interrupt.h>
#include <linux/suspend.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>
#include <net/cfg80211.h>

#include "hermes.h"

/* To enable debug messages */
/*#define ORINOCO_DEBUG		3*/

#define WIRELESS_SPY		/* enable iwspy support */

#define MAX_SCAN_LEN		4096

#define ORINOCO_SEQ_LEN		8
#define ORINOCO_MAX_KEY_SIZE	14
#define ORINOCO_MAX_KEYS	4

struct orinoco_key {
	__le16 len;	/* always stored as little-endian */
	char data[ORINOCO_MAX_KEY_SIZE];
} __packed;

#define TKIP_KEYLEN	16
#define MIC_KEYLEN	8

struct orinoco_tkip_key {
	u8 tkip[TKIP_KEYLEN];
	u8 tx_mic[MIC_KEYLEN];
	u8 rx_mic[MIC_KEYLEN];
};

enum orinoco_alg {
	ORINOCO_ALG_NONE,
	ORINOCO_ALG_WEP,
	ORINOCO_ALG_TKIP
};

enum fwtype {
	FIRMWARE_TYPE_AGERE,
	FIRMWARE_TYPE_INTERSIL,
	FIRMWARE_TYPE_SYMBOL
};

struct firmware;

struct orinoco_private {
	void *card;	/* Pointer to card dependent structure */
	struct device *dev;
	int (*hard_reset)(struct orinoco_private *);
	int (*stop_fw)(struct orinoco_private *, int);

	struct ieee80211_supported_band band;
	struct ieee80211_channel channels[14];
	u32 cipher_suites[3];

	/* Synchronisation stuff */
	spinlock_t lock;
	int hw_unavailable;
	struct work_struct reset_work;

	/* Interrupt tasklets */
	struct tasklet_struct rx_tasklet;
	struct list_head rx_list;

	/* driver state */
	int open;
	u16 last_linkstatus;
	struct work_struct join_work;
	struct work_struct wevent_work;

	/* Net device stuff */
	struct net_device *ndev;
	struct net_device_stats stats;
	struct iw_statistics wstats;

	/* Hardware control variables */
	struct hermes hw;
	u16 txfid;

	/* Capabilities of the hardware/firmware */
	enum fwtype firmware_type;
	int ibss_port;
	int nicbuf_size;
	u16 channel_mask;

	/* Boolean capabilities */
	unsigned int has_ibss:1;
	unsigned int has_port3:1;
	unsigned int has_wep:1;
	unsigned int has_big_wep:1;
	unsigned int has_mwo:1;
	unsigned int has_pm:1;
	unsigned int has_preamble:1;
	unsigned int has_sensitivity:1;
	unsigned int has_hostscan:1;
	unsigned int has_alt_txcntl:1;
	unsigned int has_ext_scan:1;
	unsigned int has_wpa:1;
	unsigned int do_fw_download:1;
	unsigned int broken_disableport:1;
	unsigned int broken_monitor:1;
	unsigned int prefer_port3:1;

	/* Configuration paramaters */
	enum nl80211_iftype iw_mode;
	enum orinoco_alg encode_alg;
	u16 wep_restrict, tx_key;
	struct key_params keys[ORINOCO_MAX_KEYS];

	int bitratemode;
	char nick[IW_ESSID_MAX_SIZE + 1];
	char desired_essid[IW_ESSID_MAX_SIZE + 1];
	char desired_bssid[ETH_ALEN];
	int bssid_fixed;
	u16 frag_thresh, mwo_robust;
	u16 channel;
	u16 ap_density, rts_thresh;
	u16 pm_on, pm_mcast, pm_period, pm_timeout;
	u16 preamble;
	u16 short_retry_limit, long_retry_limit;
	u16 retry_lifetime;
#ifdef WIRELESS_SPY
	struct iw_spy_data spy_data; /* iwspy support */
	struct iw_public_data	wireless_data;
#endif

	/* Configuration dependent variables */
	int port_type, createibss;
	int promiscuous, mc_count;

	/* Scanning support */
	struct cfg80211_scan_request *scan_request;
	struct work_struct process_scan;
	struct list_head scan_list;
	spinlock_t scan_lock; /* protects the scan list */

	/* WPA support */
	u8 *wpa_ie;
	int wpa_ie_len;

	struct crypto_hash *rx_tfm_mic;
	struct crypto_hash *tx_tfm_mic;

	unsigned int wpa_enabled:1;
	unsigned int tkip_cm_active:1;
	unsigned int key_mgmt:3;

#if defined(CONFIG_HERMES_CACHE_FW_ON_INIT) || defined(CONFIG_PM_SLEEP)
	/* Cached in memory firmware to use during ->resume. */
	const struct firmware *cached_pri_fw;
	const struct firmware *cached_fw;
#endif

	struct notifier_block pm_notifier;
};

#ifdef ORINOCO_DEBUG
extern int orinoco_debug;
#define DEBUG(n, args...) do { \
	if (orinoco_debug > (n)) \
		printk(KERN_DEBUG args); \
} while (0)
#else
#define DEBUG(n, args...) do { } while (0)
#endif	/* ORINOCO_DEBUG */

/********************************************************************/
/* Exported prototypes                                              */
/********************************************************************/

struct orinoco_private *alloc_orinocodev(int sizeof_card, struct device *device,
					 int (*hard_reset)(struct orinoco_private *),
					 int (*stop_fw)(struct orinoco_private *, int));
void free_orinocodev(struct orinoco_private *priv);
int orinoco_init(struct orinoco_private *priv);
int orinoco_if_add(struct orinoco_private *priv, unsigned long base_addr,
		   unsigned int irq, const struct net_device_ops *ops);
void orinoco_if_del(struct orinoco_private *priv);
int orinoco_up(struct orinoco_private *priv);
void orinoco_down(struct orinoco_private *priv);
irqreturn_t orinoco_interrupt(int irq, void *dev_id);

void __orinoco_ev_info(struct net_device *dev, struct hermes *hw);
void __orinoco_ev_rx(struct net_device *dev, struct hermes *hw);

int orinoco_process_xmit_skb(struct sk_buff *skb,
			     struct net_device *dev,
			     struct orinoco_private *priv,
			     int *tx_control,
			     u8 *mic);

/* Common ndo functions exported for reuse by orinoco_usb */
int orinoco_open(struct net_device *dev);
int orinoco_stop(struct net_device *dev);
struct net_device_stats *orinoco_get_stats(struct net_device *dev);
void orinoco_set_multicast_list(struct net_device *dev);
int orinoco_change_mtu(struct net_device *dev, int new_mtu);
void orinoco_tx_timeout(struct net_device *dev);

/********************************************************************/
/* Locking and synchronization functions                            */
/********************************************************************/

static inline int orinoco_lock(struct orinoco_private *priv,
			       unsigned long *flags)
{
	priv->hw.ops->lock_irqsave(&priv->lock, flags);
	if (priv->hw_unavailable) {
		DEBUG(1, "orinoco_lock() called with hw_unavailable (dev=%p)\n",
		       priv->ndev);
		priv->hw.ops->unlock_irqrestore(&priv->lock, flags);
		return -EBUSY;
	}
	return 0;
}

static inline void orinoco_unlock(struct orinoco_private *priv,
				  unsigned long *flags)
{
	priv->hw.ops->unlock_irqrestore(&priv->lock, flags);
}

static inline void orinoco_lock_irq(struct orinoco_private *priv)
{
	priv->hw.ops->lock_irq(&priv->lock);
}

static inline void orinoco_unlock_irq(struct orinoco_private *priv)
{
	priv->hw.ops->unlock_irq(&priv->lock);
}

/*** Navigate from net_device to orinoco_private ***/
static inline struct orinoco_private *ndev_priv(struct net_device *dev)
{
	struct wireless_dev *wdev = netdev_priv(dev);
	return wdev_priv(wdev);
}
#endif /* _ORINOCO_H */
