#ifndef AGNX_H_
#define AGNX_H_

#include <linux/io.h>

#include "xmit.h"

#define PFX				KBUILD_MODNAME ": "

static inline u32 agnx_read32(void __iomem *mem_region, u32 offset)
{
	return ioread32(mem_region + offset);
}

static inline void agnx_write32(void __iomem *mem_region, u32 offset, u32 val)
{
	iowrite32(val, mem_region + offset);
}

/* static const struct ieee80211_rate agnx_rates_80211b[] = { */
/* 	{ .rate = 10, */
/* 	  .val = 0xa, */
/* 	  .flags = IEEE80211_RATE_CCK }, */
/* 	{ .rate = 20, */
/* 	  .val = 0x14, */
/* 	  .hw_value = -0x14, */
/* 	  .flags = IEEE80211_RATE_CCK_2 }, */
/* 	{ .rate = 55, */
/* 	  .val = 0x37, */
/* 	  .val2 = -0x37, */
/* 	  .flags = IEEE80211_RATE_CCK_2 }, */
/* 	{ .rate = 110, */
/* 	  .val = 0x6e, */
/* 	  .val2 = -0x6e, */
/* 	  .flags = IEEE80211_RATE_CCK_2 } */
/* }; */


static const struct ieee80211_rate agnx_rates_80211g[] = {
/* 	{ .bitrate = 10, .hw_value = 1, .flags = IEEE80211_RATE_SHORT_PREAMBLE }, */
/* 	{ .bitrate = 20, .hw_value = 2, .flags = IEEE80211_RATE_SHORT_PREAMBLE }, */
/* 	{ .bitrate = 55, .hw_value = 3, .flags = IEEE80211_RATE_SHORT_PREAMBLE }, */
/* 	{ .bitrate = 110, .hw_value = 4, .flags = IEEE80211_RATE_SHORT_PREAMBLE }, */
	{ .bitrate = 10, .hw_value = 1, },
	{ .bitrate = 20, .hw_value = 2, },
	{ .bitrate = 55, .hw_value = 3, },
	{ .bitrate = 110, .hw_value = 4,},

	{ .bitrate = 60, .hw_value = 0xB, },
	{ .bitrate = 90, .hw_value = 0xF, },
	{ .bitrate = 120, .hw_value = 0xA },
	{ .bitrate = 180, .hw_value = 0xE, },
/*	{ .bitrate = 240, .hw_value = 0xd, }, */
	{ .bitrate = 360, .hw_value = 0xD, },
	{ .bitrate = 480, .hw_value = 0x8, },
	{ .bitrate = 540, .hw_value = 0xC, },
};

static const struct ieee80211_channel agnx_channels[] = {
	{ .center_freq = 2412, .hw_value = 1, },
	{ .center_freq = 2417, .hw_value = 2, },
	{ .center_freq = 2422, .hw_value = 3, },
	{ .center_freq = 2427, .hw_value = 4, },
	{ .center_freq = 2432, .hw_value = 5, },
	{ .center_freq = 2437, .hw_value = 6, },
	{ .center_freq = 2442, .hw_value = 7, },
	{ .center_freq = 2447, .hw_value = 8, },
	{ .center_freq = 2452, .hw_value = 9, },
	{ .center_freq = 2457, .hw_value = 10, },
	{ .center_freq = 2462, .hw_value = 11, },
	{ .center_freq = 2467, .hw_value = 12, },
	{ .center_freq = 2472, .hw_value = 13, },
	{ .center_freq = 2484, .hw_value = 14, },
};

#define NUM_DRIVE_MODES	2
/* Agnx operate mode */
enum {
	AGNX_MODE_80211A,
	AGNX_MODE_80211A_OOB,
	AGNX_MODE_80211A_MIMO,
	AGNX_MODE_80211B_SHORT,
	AGNX_MODE_80211B_LONG,
	AGNX_MODE_80211G,
	AGNX_MODE_80211G_OOB,
	AGNX_MODE_80211G_MIMO,
};

enum {
	AGNX_UNINIT,
	AGNX_START,
	AGNX_STOP,
};

struct agnx_priv {
	struct pci_dev *pdev;
	struct ieee80211_hw *hw;

	spinlock_t lock;
	struct mutex mutex;
	unsigned int init_status;

	void __iomem *ctl;	/* pointer to base ram address */
	void __iomem *data;	/* pointer to mem region #2 */

	struct agnx_ring rx;
	struct agnx_ring txm;
	struct agnx_ring txd;

	/* Need volatile? */
	u32 irq_status;

	struct delayed_work periodic_work; /* Periodic tasks like recalibrate */
	struct ieee80211_low_level_stats stats;

	/* unsigned int phymode; */
	int mode;
	int channel;
	u8 bssid[ETH_ALEN];

	u8 mac_addr[ETH_ALEN];
	u8 revid;

	struct ieee80211_supported_band band;
};


#define AGNX_CHAINS_MAX	6
#define AGNX_PERIODIC_DELAY 60000 /* unit: ms */
#define LOCAL_STAID	0	/* the station entry for the card itself */
#define BSSID_STAID	1	/* the station entry for the bsssid AP */
#define	spi_delay()	udelay(40)
#define eeprom_delay()	udelay(40)
#define	routing_table_delay()	udelay(50)

/* PDU pool MEM region #2 */
#define AGNX_PDUPOOL		0x40000	/* PDU pool */
#define AGNX_PDUPOOL_SIZE	0x8000	/* PDU pool size*/
#define AGNX_PDU_TX_WQ		0x41000	/* PDU list TX workqueue */
#define AGNX_PDU_FREE		0x41800	/* Free Pool */
#define PDU_SIZE		0x80	/* Free Pool node size */
#define PDU_FREE_CNT		0xd0 /* Free pool node count */


/* RF stuffs */
extern void rf_chips_init(struct agnx_priv *priv);
extern void spi_rc_write(void __iomem *mem_region, u32 chip_ids, u32 sw);
extern void calibrate_oscillator(struct agnx_priv *priv);
extern void do_calibration(struct agnx_priv *priv);
extern void antenna_calibrate(struct agnx_priv *priv);
extern void __antenna_calibrate(struct agnx_priv *priv);
extern void print_offsets(struct agnx_priv *priv);
extern int agnx_set_channel(struct agnx_priv *priv, unsigned int channel);


#endif /* AGNX_H_ */
