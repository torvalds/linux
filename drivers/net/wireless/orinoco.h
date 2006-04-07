/* orinoco.h
 * 
 * Common definitions to all pieces of the various orinoco
 * drivers
 */

#ifndef _ORINOCO_H
#define _ORINOCO_H

#define DRIVER_VERSION "0.15rc3"

#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <net/iw_handler.h>

#include "hermes.h"

/* To enable debug messages */
//#define ORINOCO_DEBUG		3

#define WIRELESS_SPY		// enable iwspy support

#define MAX_SCAN_LEN		4096

#define ORINOCO_MAX_KEY_SIZE	14
#define ORINOCO_MAX_KEYS	4

struct orinoco_key {
	__le16 len;	/* always stored as little-endian */
	char data[ORINOCO_MAX_KEY_SIZE];
} __attribute__ ((packed));

struct header_struct {
	/* 802.3 */
	u8 dest[ETH_ALEN];
	u8 src[ETH_ALEN];
	__be16 len;
	/* 802.2 */
	u8 dsap;
	u8 ssap;
	u8 ctrl;
	/* SNAP */
	u8 oui[3];
	unsigned short ethertype;
} __attribute__ ((packed));

typedef enum {
	FIRMWARE_TYPE_AGERE,
	FIRMWARE_TYPE_INTERSIL,
	FIRMWARE_TYPE_SYMBOL
} fwtype_t;

struct orinoco_private {
	void *card;	/* Pointer to card dependent structure */
	int (*hard_reset)(struct orinoco_private *);

	/* Synchronisation stuff */
	spinlock_t lock;
	int hw_unavailable;
	struct work_struct reset_work;

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
	hermes_t hw;
	u16 txfid;

	/* Capabilities of the hardware/firmware */
	fwtype_t firmware_type;
	char fw_name[32];
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
	unsigned int broken_disableport:1;
	unsigned int broken_monitor:1;

	/* Configuration paramaters */
	u32 iw_mode;
	int prefer_port3;
	u16 wep_on, wep_restrict, tx_key;
	struct orinoco_key keys[ORINOCO_MAX_KEYS];
	int bitratemode;
 	char nick[IW_ESSID_MAX_SIZE+1];
	char desired_essid[IW_ESSID_MAX_SIZE+1];
	char desired_bssid[ETH_ALEN];
	int bssid_fixed;
	u16 frag_thresh, mwo_robust;
	u16 channel;
	u16 ap_density, rts_thresh;
	u16 pm_on, pm_mcast, pm_period, pm_timeout;
	u16 preamble;
#ifdef WIRELESS_SPY
 	struct iw_spy_data spy_data; /* iwspy support */
	struct iw_public_data	wireless_data;
#endif

	/* Configuration dependent variables */
	int port_type, createibss;
	int promiscuous, mc_count;

	/* Scanning support */
	int	scan_inprogress;	/* Scan pending... */
	u32	scan_mode;		/* Type of scan done */
	char *	scan_result;		/* Result of previous scan */
	int	scan_len;		/* Lenght of result */
};

#ifdef ORINOCO_DEBUG
extern int orinoco_debug;
#define DEBUG(n, args...) do { if (orinoco_debug>(n)) printk(KERN_DEBUG args); } while(0)
#else
#define DEBUG(n, args...) do { } while (0)
#endif	/* ORINOCO_DEBUG */

/********************************************************************/
/* Exported prototypes                                              */
/********************************************************************/

extern struct net_device *alloc_orinocodev(int sizeof_card,
					   int (*hard_reset)(struct orinoco_private *));
extern void free_orinocodev(struct net_device *dev);
extern int __orinoco_up(struct net_device *dev);
extern int __orinoco_down(struct net_device *dev);
extern int orinoco_reinit_firmware(struct net_device *dev);
extern irqreturn_t orinoco_interrupt(int irq, void * dev_id, struct pt_regs *regs);

/********************************************************************/
/* Locking and synchronization functions                            */
/********************************************************************/

/* These functions *must* be inline or they will break horribly on
 * SPARC, due to its weird semantics for save/restore flags. extern
 * inline should prevent the kernel from linking or module from
 * loading if they are not inlined. */
extern inline int orinoco_lock(struct orinoco_private *priv,
			       unsigned long *flags)
{
	spin_lock_irqsave(&priv->lock, *flags);
	if (priv->hw_unavailable) {
		DEBUG(1, "orinoco_lock() called with hw_unavailable (dev=%p)\n",
		       priv->ndev);
		spin_unlock_irqrestore(&priv->lock, *flags);
		return -EBUSY;
	}
	return 0;
}

extern inline void orinoco_unlock(struct orinoco_private *priv,
				  unsigned long *flags)
{
	spin_unlock_irqrestore(&priv->lock, *flags);
}

#endif /* _ORINOCO_H */
