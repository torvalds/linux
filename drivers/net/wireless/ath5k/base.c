/*-
 * Copyright (c) 2002-2005 Sam Leffler, Errno Consulting
 * Copyright (c) 2004-2005 Atheros Communications, Inc.
 * Copyright (c) 2006 Devicescape Software, Inc.
 * Copyright (c) 2007 Jiri Slaby <jirislaby@gmail.com>
 * Copyright (c) 2007 Luis R. Rodriguez <mcgrof@winlab.rutgers.edu>
 *
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer,
 *    without modification.
 * 2. Redistributions in binary form must reproduce at minimum a disclaimer
 *    similar to the "NO WARRANTY" disclaimer below ("Disclaimer") and any
 *    redistribution must be conditioned upon including a substantially
 *    similar Disclaimer requirement for further binary redistribution.
 * 3. Neither the names of the above-listed copyright holders nor the names
 *    of any contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * Alternatively, this software may be distributed under the terms of the
 * GNU General Public License ("GPL") version 2 as published by the Free
 * Software Foundation.
 *
 * NO WARRANTY
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF NONINFRINGEMENT, MERCHANTIBILITY
 * AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL
 * THE COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR SPECIAL, EXEMPLARY,
 * OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGES.
 *
 */

#include <linux/module.h>
#include <linux/delay.h>
#include <linux/hardirq.h>
#include <linux/if.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/cache.h>
#include <linux/pci.h>
#include <linux/ethtool.h>
#include <linux/uaccess.h>

#include <net/ieee80211_radiotap.h>

#include <asm/unaligned.h>

#include "base.h"
#include "reg.h"
#include "debug.h"

static int ath5k_calinterval = 10; /* Calibrate PHY every 10 secs (TODO: Fixme) */


/******************\
* Internal defines *
\******************/

/* Module info */
MODULE_AUTHOR("Jiri Slaby");
MODULE_AUTHOR("Nick Kossifidis");
MODULE_DESCRIPTION("Support for 5xxx series of Atheros 802.11 wireless LAN cards.");
MODULE_SUPPORTED_DEVICE("Atheros 5xxx WLAN cards");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_VERSION("0.5.0 (EXPERIMENTAL)");


/* Known PCI ids */
static struct pci_device_id ath5k_pci_id_table[] __devinitdata = {
	{ PCI_VDEVICE(ATHEROS, 0x0207), .driver_data = AR5K_AR5210 }, /* 5210 early */
	{ PCI_VDEVICE(ATHEROS, 0x0007), .driver_data = AR5K_AR5210 }, /* 5210 */
	{ PCI_VDEVICE(ATHEROS, 0x0011), .driver_data = AR5K_AR5211 }, /* 5311 - this is on AHB bus !*/
	{ PCI_VDEVICE(ATHEROS, 0x0012), .driver_data = AR5K_AR5211 }, /* 5211 */
	{ PCI_VDEVICE(ATHEROS, 0x0013), .driver_data = AR5K_AR5212 }, /* 5212 */
	{ PCI_VDEVICE(3COM_2,  0x0013), .driver_data = AR5K_AR5212 }, /* 3com 5212 */
	{ PCI_VDEVICE(3COM,    0x0013), .driver_data = AR5K_AR5212 }, /* 3com 3CRDAG675 5212 */
	{ PCI_VDEVICE(ATHEROS, 0x1014), .driver_data = AR5K_AR5212 }, /* IBM minipci 5212 */
	{ PCI_VDEVICE(ATHEROS, 0x0014), .driver_data = AR5K_AR5212 }, /* 5212 combatible */
	{ PCI_VDEVICE(ATHEROS, 0x0015), .driver_data = AR5K_AR5212 }, /* 5212 combatible */
	{ PCI_VDEVICE(ATHEROS, 0x0016), .driver_data = AR5K_AR5212 }, /* 5212 combatible */
	{ PCI_VDEVICE(ATHEROS, 0x0017), .driver_data = AR5K_AR5212 }, /* 5212 combatible */
	{ PCI_VDEVICE(ATHEROS, 0x0018), .driver_data = AR5K_AR5212 }, /* 5212 combatible */
	{ PCI_VDEVICE(ATHEROS, 0x0019), .driver_data = AR5K_AR5212 }, /* 5212 combatible */
	{ PCI_VDEVICE(ATHEROS, 0x001a), .driver_data = AR5K_AR5212 }, /* 2413 Griffin-lite */
	{ PCI_VDEVICE(ATHEROS, 0x001b), .driver_data = AR5K_AR5212 }, /* 5413 Eagle */
	{ PCI_VDEVICE(ATHEROS, 0x001c), .driver_data = AR5K_AR5212 }, /* 5424 Condor (PCI-E)*/
	{ 0 }
};
MODULE_DEVICE_TABLE(pci, ath5k_pci_id_table);

/* Known SREVs */
static struct ath5k_srev_name srev_names[] = {
	{ "5210",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5210 },
	{ "5311",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5311 },
	{ "5311A",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5311A },
	{ "5311B",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5311B },
	{ "5211",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5211 },
	{ "5212",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5212 },
	{ "5213",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5213 },
	{ "5213A",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5213A },
	{ "2413",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR2413 },
	{ "2414",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR2414 },
	{ "2424",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR2424 },
	{ "5424",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5424 },
	{ "5413",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5413 },
	{ "5414",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5414 },
	{ "5416",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5416 },
	{ "5418",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR5418 },
	{ "2425",	AR5K_VERSION_VER,	AR5K_SREV_VER_AR2425 },
	{ "xxxxx",	AR5K_VERSION_VER,	AR5K_SREV_UNKNOWN },
	{ "5110",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5110 },
	{ "5111",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5111 },
	{ "2111",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_2111 },
	{ "5112",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5112 },
	{ "5112A",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5112A },
	{ "2112",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_2112 },
	{ "2112A",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_2112A },
	{ "SChip",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_SC0 },
	{ "SChip",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_SC1 },
	{ "SChip",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_SC2 },
	{ "5133",	AR5K_VERSION_RAD,	AR5K_SREV_RAD_5133 },
	{ "xxxxx",	AR5K_VERSION_RAD,	AR5K_SREV_UNKNOWN },
};

/*
 * Prototypes - PCI stack related functions
 */
static int __devinit	ath5k_pci_probe(struct pci_dev *pdev,
				const struct pci_device_id *id);
static void __devexit	ath5k_pci_remove(struct pci_dev *pdev);
#ifdef CONFIG_PM
static int		ath5k_pci_suspend(struct pci_dev *pdev,
					pm_message_t state);
static int		ath5k_pci_resume(struct pci_dev *pdev);
#else
#define ath5k_pci_suspend NULL
#define ath5k_pci_resume NULL
#endif /* CONFIG_PM */

static struct pci_driver ath5k_pci_driver = {
	.name		= "ath5k_pci",
	.id_table	= ath5k_pci_id_table,
	.probe		= ath5k_pci_probe,
	.remove		= __devexit_p(ath5k_pci_remove),
	.suspend	= ath5k_pci_suspend,
	.resume		= ath5k_pci_resume,
};



/*
 * Prototypes - MAC 802.11 stack related functions
 */
static int ath5k_tx(struct ieee80211_hw *hw, struct sk_buff *skb);
static int ath5k_reset(struct ieee80211_hw *hw);
static int ath5k_start(struct ieee80211_hw *hw);
static void ath5k_stop(struct ieee80211_hw *hw);
static int ath5k_add_interface(struct ieee80211_hw *hw,
		struct ieee80211_if_init_conf *conf);
static void ath5k_remove_interface(struct ieee80211_hw *hw,
		struct ieee80211_if_init_conf *conf);
static int ath5k_config(struct ieee80211_hw *hw,
		struct ieee80211_conf *conf);
static int ath5k_config_interface(struct ieee80211_hw *hw,
		struct ieee80211_vif *vif,
		struct ieee80211_if_conf *conf);
static void ath5k_configure_filter(struct ieee80211_hw *hw,
		unsigned int changed_flags,
		unsigned int *new_flags,
		int mc_count, struct dev_mc_list *mclist);
static int ath5k_set_key(struct ieee80211_hw *hw,
		enum set_key_cmd cmd,
		const u8 *local_addr, const u8 *addr,
		struct ieee80211_key_conf *key);
static int ath5k_get_stats(struct ieee80211_hw *hw,
		struct ieee80211_low_level_stats *stats);
static int ath5k_get_tx_stats(struct ieee80211_hw *hw,
		struct ieee80211_tx_queue_stats *stats);
static u64 ath5k_get_tsf(struct ieee80211_hw *hw);
static void ath5k_reset_tsf(struct ieee80211_hw *hw);
static int ath5k_beacon_update(struct ieee80211_hw *hw,
		struct sk_buff *skb);

static struct ieee80211_ops ath5k_hw_ops = {
	.tx 		= ath5k_tx,
	.start 		= ath5k_start,
	.stop 		= ath5k_stop,
	.add_interface 	= ath5k_add_interface,
	.remove_interface = ath5k_remove_interface,
	.config 	= ath5k_config,
	.config_interface = ath5k_config_interface,
	.configure_filter = ath5k_configure_filter,
	.set_key 	= ath5k_set_key,
	.get_stats 	= ath5k_get_stats,
	.conf_tx 	= NULL,
	.get_tx_stats 	= ath5k_get_tx_stats,
	.get_tsf 	= ath5k_get_tsf,
	.reset_tsf 	= ath5k_reset_tsf,
};

/*
 * Prototypes - Internal functions
 */
/* Attach detach */
static int 	ath5k_attach(struct pci_dev *pdev,
			struct ieee80211_hw *hw);
static void 	ath5k_detach(struct pci_dev *pdev,
			struct ieee80211_hw *hw);
/* Channel/mode setup */
static inline short ath5k_ieee2mhz(short chan);
static unsigned int ath5k_copy_rates(struct ieee80211_rate *rates,
				const struct ath5k_rate_table *rt,
				unsigned int max);
static unsigned int ath5k_copy_channels(struct ath5k_hw *ah,
				struct ieee80211_channel *channels,
				unsigned int mode,
				unsigned int max);
static int 	ath5k_getchannels(struct ieee80211_hw *hw);
static int 	ath5k_chan_set(struct ath5k_softc *sc,
				struct ieee80211_channel *chan);
static void	ath5k_setcurmode(struct ath5k_softc *sc,
				unsigned int mode);
static void	ath5k_mode_setup(struct ath5k_softc *sc);
static void	ath5k_set_total_hw_rates(struct ath5k_softc *sc);

/* Descriptor setup */
static int	ath5k_desc_alloc(struct ath5k_softc *sc,
				struct pci_dev *pdev);
static void	ath5k_desc_free(struct ath5k_softc *sc,
				struct pci_dev *pdev);
/* Buffers setup */
static int 	ath5k_rxbuf_setup(struct ath5k_softc *sc,
				struct ath5k_buf *bf);
static int 	ath5k_txbuf_setup(struct ath5k_softc *sc,
				struct ath5k_buf *bf);
static inline void ath5k_txbuf_free(struct ath5k_softc *sc,
				struct ath5k_buf *bf)
{
	BUG_ON(!bf);
	if (!bf->skb)
		return;
	pci_unmap_single(sc->pdev, bf->skbaddr, bf->skb->len,
			PCI_DMA_TODEVICE);
	dev_kfree_skb_any(bf->skb);
	bf->skb = NULL;
}

/* Queues setup */
static struct 	ath5k_txq *ath5k_txq_setup(struct ath5k_softc *sc,
				int qtype, int subtype);
static int 	ath5k_beaconq_setup(struct ath5k_hw *ah);
static int 	ath5k_beaconq_config(struct ath5k_softc *sc);
static void 	ath5k_txq_drainq(struct ath5k_softc *sc,
				struct ath5k_txq *txq);
static void 	ath5k_txq_cleanup(struct ath5k_softc *sc);
static void 	ath5k_txq_release(struct ath5k_softc *sc);
/* Rx handling */
static int 	ath5k_rx_start(struct ath5k_softc *sc);
static void 	ath5k_rx_stop(struct ath5k_softc *sc);
static unsigned int ath5k_rx_decrypted(struct ath5k_softc *sc,
					struct ath5k_desc *ds,
					struct sk_buff *skb,
					struct ath5k_rx_status *rs);
static void 	ath5k_tasklet_rx(unsigned long data);
/* Tx handling */
static void 	ath5k_tx_processq(struct ath5k_softc *sc,
				struct ath5k_txq *txq);
static void 	ath5k_tasklet_tx(unsigned long data);
/* Beacon handling */
static int 	ath5k_beacon_setup(struct ath5k_softc *sc,
					struct ath5k_buf *bf);
static void 	ath5k_beacon_send(struct ath5k_softc *sc);
static void 	ath5k_beacon_config(struct ath5k_softc *sc);
static void	ath5k_beacon_update_timers(struct ath5k_softc *sc, u64 bc_tsf);

static inline u64 ath5k_extend_tsf(struct ath5k_hw *ah, u32 rstamp)
{
	u64 tsf = ath5k_hw_get_tsf64(ah);

	if ((tsf & 0x7fff) < rstamp)
		tsf -= 0x8000;

	return (tsf & ~0x7fff) | rstamp;
}

/* Interrupt handling */
static int 	ath5k_init(struct ath5k_softc *sc);
static int 	ath5k_stop_locked(struct ath5k_softc *sc);
static int 	ath5k_stop_hw(struct ath5k_softc *sc);
static irqreturn_t ath5k_intr(int irq, void *dev_id);
static void 	ath5k_tasklet_reset(unsigned long data);

static void 	ath5k_calibrate(unsigned long data);
/* LED functions */
static int	ath5k_init_leds(struct ath5k_softc *sc);
static void	ath5k_led_enable(struct ath5k_softc *sc);
static void	ath5k_led_off(struct ath5k_softc *sc);
static void	ath5k_unregister_leds(struct ath5k_softc *sc);

/*
 * Module init/exit functions
 */
static int __init
init_ath5k_pci(void)
{
	int ret;

	ath5k_debug_init();

	ret = pci_register_driver(&ath5k_pci_driver);
	if (ret) {
		printk(KERN_ERR "ath5k_pci: can't register pci driver\n");
		return ret;
	}

	return 0;
}

static void __exit
exit_ath5k_pci(void)
{
	pci_unregister_driver(&ath5k_pci_driver);

	ath5k_debug_finish();
}

module_init(init_ath5k_pci);
module_exit(exit_ath5k_pci);


/********************\
* PCI Initialization *
\********************/

static const char *
ath5k_chip_name(enum ath5k_srev_type type, u_int16_t val)
{
	const char *name = "xxxxx";
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(srev_names); i++) {
		if (srev_names[i].sr_type != type)
			continue;
		if ((val & 0xff) < srev_names[i + 1].sr_val) {
			name = srev_names[i].sr_name;
			break;
		}
	}

	return name;
}

static int __devinit
ath5k_pci_probe(struct pci_dev *pdev,
		const struct pci_device_id *id)
{
	void __iomem *mem;
	struct ath5k_softc *sc;
	struct ieee80211_hw *hw;
	int ret;
	u8 csz;

	ret = pci_enable_device(pdev);
	if (ret) {
		dev_err(&pdev->dev, "can't enable device\n");
		goto err;
	}

	/* XXX 32-bit addressing only */
	ret = pci_set_dma_mask(pdev, DMA_32BIT_MASK);
	if (ret) {
		dev_err(&pdev->dev, "32-bit DMA not available\n");
		goto err_dis;
	}

	/*
	 * Cache line size is used to size and align various
	 * structures used to communicate with the hardware.
	 */
	pci_read_config_byte(pdev, PCI_CACHE_LINE_SIZE, &csz);
	if (csz == 0) {
		/*
		 * Linux 2.4.18 (at least) writes the cache line size
		 * register as a 16-bit wide register which is wrong.
		 * We must have this setup properly for rx buffer
		 * DMA to work so force a reasonable value here if it
		 * comes up zero.
		 */
		csz = L1_CACHE_BYTES / sizeof(u32);
		pci_write_config_byte(pdev, PCI_CACHE_LINE_SIZE, csz);
	}
	/*
	 * The default setting of latency timer yields poor results,
	 * set it to the value used by other systems.  It may be worth
	 * tweaking this setting more.
	 */
	pci_write_config_byte(pdev, PCI_LATENCY_TIMER, 0xa8);

	/* Enable bus mastering */
	pci_set_master(pdev);

	/*
	 * Disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state.
	 */
	pci_write_config_byte(pdev, 0x41, 0);

	ret = pci_request_region(pdev, 0, "ath5k");
	if (ret) {
		dev_err(&pdev->dev, "cannot reserve PCI memory region\n");
		goto err_dis;
	}

	mem = pci_iomap(pdev, 0, 0);
	if (!mem) {
		dev_err(&pdev->dev, "cannot remap PCI memory region\n") ;
		ret = -EIO;
		goto err_reg;
	}

	/*
	 * Allocate hw (mac80211 main struct)
	 * and hw->priv (driver private data)
	 */
	hw = ieee80211_alloc_hw(sizeof(*sc), &ath5k_hw_ops);
	if (hw == NULL) {
		dev_err(&pdev->dev, "cannot allocate ieee80211_hw\n");
		ret = -ENOMEM;
		goto err_map;
	}

	dev_info(&pdev->dev, "registered as '%s'\n", wiphy_name(hw->wiphy));

	/* Initialize driver private data */
	SET_IEEE80211_DEV(hw, &pdev->dev);
	hw->flags = IEEE80211_HW_RX_INCLUDES_FCS |
		    IEEE80211_HW_SIGNAL_DBM |
		    IEEE80211_HW_NOISE_DBM;
	hw->extra_tx_headroom = 2;
	hw->channel_change_time = 5000;
	sc = hw->priv;
	sc->hw = hw;
	sc->pdev = pdev;

	ath5k_debug_init_device(sc);

	/*
	 * Mark the device as detached to avoid processing
	 * interrupts until setup is complete.
	 */
	__set_bit(ATH_STAT_INVALID, sc->status);

	sc->iobase = mem; /* So we can unmap it on detach */
	sc->cachelsz = csz * sizeof(u32); /* convert to bytes */
	sc->opmode = IEEE80211_IF_TYPE_STA;
	mutex_init(&sc->lock);
	spin_lock_init(&sc->rxbuflock);
	spin_lock_init(&sc->txbuflock);
	spin_lock_init(&sc->block);

	/* Set private data */
	pci_set_drvdata(pdev, hw);

	/* Setup interrupt handler */
	ret = request_irq(pdev->irq, ath5k_intr, IRQF_SHARED, "ath", sc);
	if (ret) {
		ATH5K_ERR(sc, "request_irq failed\n");
		goto err_free;
	}

	/* Initialize device */
	sc->ah = ath5k_hw_attach(sc, id->driver_data);
	if (IS_ERR(sc->ah)) {
		ret = PTR_ERR(sc->ah);
		goto err_irq;
	}

	/* Finish private driver data initialization */
	ret = ath5k_attach(pdev, hw);
	if (ret)
		goto err_ah;

	ATH5K_INFO(sc, "Atheros AR%s chip found (MAC: 0x%x, PHY: 0x%x)\n",
			ath5k_chip_name(AR5K_VERSION_VER,sc->ah->ah_mac_srev),
					sc->ah->ah_mac_srev,
					sc->ah->ah_phy_revision);

	if (!sc->ah->ah_single_chip) {
		/* Single chip radio (!RF5111) */
		if (sc->ah->ah_radio_5ghz_revision &&
			!sc->ah->ah_radio_2ghz_revision) {
			/* No 5GHz support -> report 2GHz radio */
			if (!test_bit(AR5K_MODE_11A,
				sc->ah->ah_capabilities.cap_mode)) {
				ATH5K_INFO(sc, "RF%s 2GHz radio found (0x%x)\n",
					ath5k_chip_name(AR5K_VERSION_RAD,
						sc->ah->ah_radio_5ghz_revision),
						sc->ah->ah_radio_5ghz_revision);
			/* No 2GHz support (5110 and some
			 * 5Ghz only cards) -> report 5Ghz radio */
			} else if (!test_bit(AR5K_MODE_11B,
				sc->ah->ah_capabilities.cap_mode)) {
				ATH5K_INFO(sc, "RF%s 5GHz radio found (0x%x)\n",
					ath5k_chip_name(AR5K_VERSION_RAD,
						sc->ah->ah_radio_5ghz_revision),
						sc->ah->ah_radio_5ghz_revision);
			/* Multiband radio */
			} else {
				ATH5K_INFO(sc, "RF%s multiband radio found"
					" (0x%x)\n",
					ath5k_chip_name(AR5K_VERSION_RAD,
						sc->ah->ah_radio_5ghz_revision),
						sc->ah->ah_radio_5ghz_revision);
			}
		}
		/* Multi chip radio (RF5111 - RF2111) ->
		 * report both 2GHz/5GHz radios */
		else if (sc->ah->ah_radio_5ghz_revision &&
				sc->ah->ah_radio_2ghz_revision){
			ATH5K_INFO(sc, "RF%s 5GHz radio found (0x%x)\n",
				ath5k_chip_name(AR5K_VERSION_RAD,
					sc->ah->ah_radio_5ghz_revision),
					sc->ah->ah_radio_5ghz_revision);
			ATH5K_INFO(sc, "RF%s 2GHz radio found (0x%x)\n",
				ath5k_chip_name(AR5K_VERSION_RAD,
					sc->ah->ah_radio_2ghz_revision),
					sc->ah->ah_radio_2ghz_revision);
		}
	}


	/* ready to process interrupts */
	__clear_bit(ATH_STAT_INVALID, sc->status);

	return 0;
err_ah:
	ath5k_hw_detach(sc->ah);
err_irq:
	free_irq(pdev->irq, sc);
err_free:
	ieee80211_free_hw(hw);
err_map:
	pci_iounmap(pdev, mem);
err_reg:
	pci_release_region(pdev, 0);
err_dis:
	pci_disable_device(pdev);
err:
	return ret;
}

static void __devexit
ath5k_pci_remove(struct pci_dev *pdev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct ath5k_softc *sc = hw->priv;

	ath5k_debug_finish_device(sc);
	ath5k_detach(pdev, hw);
	ath5k_hw_detach(sc->ah);
	free_irq(pdev->irq, sc);
	pci_iounmap(pdev, sc->iobase);
	pci_release_region(pdev, 0);
	pci_disable_device(pdev);
	ieee80211_free_hw(hw);
}

#ifdef CONFIG_PM
static int
ath5k_pci_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct ath5k_softc *sc = hw->priv;

	ath5k_led_off(sc);

	ath5k_stop_hw(sc);

	free_irq(pdev->irq, sc);
	pci_save_state(pdev);
	pci_disable_device(pdev);
	pci_set_power_state(pdev, PCI_D3hot);

	return 0;
}

static int
ath5k_pci_resume(struct pci_dev *pdev)
{
	struct ieee80211_hw *hw = pci_get_drvdata(pdev);
	struct ath5k_softc *sc = hw->priv;
	struct ath5k_hw *ah = sc->ah;
	int i, err;

	pci_restore_state(pdev);

	err = pci_enable_device(pdev);
	if (err)
		return err;

	/*
	 * Suspend/Resume resets the PCI configuration space, so we have to
	 * re-disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state
	 */
	pci_write_config_byte(pdev, 0x41, 0);

	err = request_irq(pdev->irq, ath5k_intr, IRQF_SHARED, "ath", sc);
	if (err) {
		ATH5K_ERR(sc, "request_irq failed\n");
		goto err_no_irq;
	}

	err = ath5k_init(sc);
	if (err)
		goto err_irq;
	ath5k_led_enable(sc);

	/*
	 * Reset the key cache since some parts do not
	 * reset the contents on initial power up or resume.
	 *
	 * FIXME: This may need to be revisited when mac80211 becomes
	 *        aware of suspend/resume.
	 */
	for (i = 0; i < AR5K_KEYTABLE_SIZE; i++)
		ath5k_hw_reset_key(ah, i);

	return 0;
err_irq:
	free_irq(pdev->irq, sc);
err_no_irq:
	pci_disable_device(pdev);
	return err;
}
#endif /* CONFIG_PM */



/***********************\
* Driver Initialization *
\***********************/

static int
ath5k_attach(struct pci_dev *pdev, struct ieee80211_hw *hw)
{
	struct ath5k_softc *sc = hw->priv;
	struct ath5k_hw *ah = sc->ah;
	u8 mac[ETH_ALEN];
	unsigned int i;
	int ret;

	ATH5K_DBG(sc, ATH5K_DEBUG_ANY, "devid 0x%x\n", pdev->device);

	/*
	 * Check if the MAC has multi-rate retry support.
	 * We do this by trying to setup a fake extended
	 * descriptor.  MAC's that don't have support will
	 * return false w/o doing anything.  MAC's that do
	 * support it will return true w/o doing anything.
	 */
	ret = ah->ah_setup_xtx_desc(ah, NULL, 0, 0, 0, 0, 0, 0);
	if (ret < 0)
		goto err;
	if (ret > 0)
		__set_bit(ATH_STAT_MRRETRY, sc->status);

	/*
	 * Reset the key cache since some parts do not
	 * reset the contents on initial power up.
	 */
	for (i = 0; i < AR5K_KEYTABLE_SIZE; i++)
		ath5k_hw_reset_key(ah, i);

	/*
	 * Collect the channel list.  The 802.11 layer
	 * is resposible for filtering this list based
	 * on settings like the phy mode and regulatory
	 * domain restrictions.
	 */
	ret = ath5k_getchannels(hw);
	if (ret) {
		ATH5K_ERR(sc, "can't get channels\n");
		goto err;
	}

	/* Set *_rates so we can map hw rate index */
	ath5k_set_total_hw_rates(sc);

	/* NB: setup here so ath5k_rate_update is happy */
	if (test_bit(AR5K_MODE_11A, ah->ah_modes))
		ath5k_setcurmode(sc, AR5K_MODE_11A);
	else
		ath5k_setcurmode(sc, AR5K_MODE_11B);

	/*
	 * Allocate tx+rx descriptors and populate the lists.
	 */
	ret = ath5k_desc_alloc(sc, pdev);
	if (ret) {
		ATH5K_ERR(sc, "can't allocate descriptors\n");
		goto err;
	}

	/*
	 * Allocate hardware transmit queues: one queue for
	 * beacon frames and one data queue for each QoS
	 * priority.  Note that hw functions handle reseting
	 * these queues at the needed time.
	 */
	ret = ath5k_beaconq_setup(ah);
	if (ret < 0) {
		ATH5K_ERR(sc, "can't setup a beacon xmit queue\n");
		goto err_desc;
	}
	sc->bhalq = ret;

	sc->txq = ath5k_txq_setup(sc, AR5K_TX_QUEUE_DATA, AR5K_WME_AC_BK);
	if (IS_ERR(sc->txq)) {
		ATH5K_ERR(sc, "can't setup xmit queue\n");
		ret = PTR_ERR(sc->txq);
		goto err_bhal;
	}

	tasklet_init(&sc->rxtq, ath5k_tasklet_rx, (unsigned long)sc);
	tasklet_init(&sc->txtq, ath5k_tasklet_tx, (unsigned long)sc);
	tasklet_init(&sc->restq, ath5k_tasklet_reset, (unsigned long)sc);
	setup_timer(&sc->calib_tim, ath5k_calibrate, (unsigned long)sc);

	ath5k_hw_get_lladdr(ah, mac);
	SET_IEEE80211_PERM_ADDR(hw, mac);
	/* All MAC address bits matter for ACKs */
	memset(sc->bssidmask, 0xff, ETH_ALEN);
	ath5k_hw_set_bssid_mask(sc->ah, sc->bssidmask);

	ret = ieee80211_register_hw(hw);
	if (ret) {
		ATH5K_ERR(sc, "can't register ieee80211 hw\n");
		goto err_queues;
	}

	ath5k_init_leds(sc);

	return 0;
err_queues:
	ath5k_txq_release(sc);
err_bhal:
	ath5k_hw_release_tx_queue(ah, sc->bhalq);
err_desc:
	ath5k_desc_free(sc, pdev);
err:
	return ret;
}

static void
ath5k_detach(struct pci_dev *pdev, struct ieee80211_hw *hw)
{
	struct ath5k_softc *sc = hw->priv;

	/*
	 * NB: the order of these is important:
	 * o call the 802.11 layer before detaching ath5k_hw to
	 *   insure callbacks into the driver to delete global
	 *   key cache entries can be handled
	 * o reclaim the tx queue data structures after calling
	 *   the 802.11 layer as we'll get called back to reclaim
	 *   node state and potentially want to use them
	 * o to cleanup the tx queues the hal is called, so detach
	 *   it last
	 * XXX: ??? detach ath5k_hw ???
	 * Other than that, it's straightforward...
	 */
	ieee80211_unregister_hw(hw);
	ath5k_desc_free(sc, pdev);
	ath5k_txq_release(sc);
	ath5k_hw_release_tx_queue(sc->ah, sc->bhalq);
	ath5k_unregister_leds(sc);

	/*
	 * NB: can't reclaim these until after ieee80211_ifdetach
	 * returns because we'll get called back to reclaim node
	 * state and potentially want to use them.
	 */
}




/********************\
* Channel/mode setup *
\********************/

/*
 * Convert IEEE channel number to MHz frequency.
 */
static inline short
ath5k_ieee2mhz(short chan)
{
	if (chan <= 14 || chan >= 27)
		return ieee80211chan2mhz(chan);
	else
		return 2212 + chan * 20;
}

static unsigned int
ath5k_copy_rates(struct ieee80211_rate *rates,
		const struct ath5k_rate_table *rt,
		unsigned int max)
{
	unsigned int i, count;

	if (rt == NULL)
		return 0;

	for (i = 0, count = 0; i < rt->rate_count && max > 0; i++) {
		rates[count].bitrate = rt->rates[i].rate_kbps / 100;
		rates[count].hw_value = rt->rates[i].rate_code;
		rates[count].flags = rt->rates[i].modulation;
		count++;
		max--;
	}

	return count;
}

static unsigned int
ath5k_copy_channels(struct ath5k_hw *ah,
		struct ieee80211_channel *channels,
		unsigned int mode,
		unsigned int max)
{
	unsigned int i, count, size, chfreq, freq, ch;

	if (!test_bit(mode, ah->ah_modes))
		return 0;

	switch (mode) {
	case AR5K_MODE_11A:
	case AR5K_MODE_11A_TURBO:
		/* 1..220, but 2GHz frequencies are filtered by check_channel */
		size = 220 ;
		chfreq = CHANNEL_5GHZ;
		break;
	case AR5K_MODE_11B:
	case AR5K_MODE_11G:
	case AR5K_MODE_11G_TURBO:
		size = 26;
		chfreq = CHANNEL_2GHZ;
		break;
	default:
		ATH5K_WARN(ah->ah_sc, "bad mode, not copying channels\n");
		return 0;
	}

	for (i = 0, count = 0; i < size && max > 0; i++) {
		ch = i + 1 ;
		freq = ath5k_ieee2mhz(ch);

		/* Check if channel is supported by the chipset */
		if (!ath5k_channel_ok(ah, freq, chfreq))
			continue;

		/* Write channel info and increment counter */
		channels[count].center_freq = freq;
		channels[count].band = (chfreq == CHANNEL_2GHZ) ?
			IEEE80211_BAND_2GHZ : IEEE80211_BAND_5GHZ;
		switch (mode) {
		case AR5K_MODE_11A:
		case AR5K_MODE_11G:
			channels[count].hw_value = chfreq | CHANNEL_OFDM;
			break;
		case AR5K_MODE_11A_TURBO:
		case AR5K_MODE_11G_TURBO:
			channels[count].hw_value = chfreq |
				CHANNEL_OFDM | CHANNEL_TURBO;
			break;
		case AR5K_MODE_11B:
			channels[count].hw_value = CHANNEL_B;
		}

		count++;
		max--;
	}

	return count;
}

static int
ath5k_getchannels(struct ieee80211_hw *hw)
{
	struct ath5k_softc *sc = hw->priv;
	struct ath5k_hw *ah = sc->ah;
	struct ieee80211_supported_band *sbands = sc->sbands;
	const struct ath5k_rate_table *hw_rates;
	unsigned int max_r, max_c, count_r, count_c;
	int mode2g = AR5K_MODE_11G;

	BUILD_BUG_ON(ARRAY_SIZE(sc->sbands) < IEEE80211_NUM_BANDS);

	max_r = ARRAY_SIZE(sc->rates);
	max_c = ARRAY_SIZE(sc->channels);
	count_r = count_c = 0;

	/* 2GHz band */
	if (!test_bit(AR5K_MODE_11G, sc->ah->ah_capabilities.cap_mode)) {
		mode2g = AR5K_MODE_11B;
		if (!test_bit(AR5K_MODE_11B,
			sc->ah->ah_capabilities.cap_mode))
			mode2g = -1;
	}

	if (mode2g > 0) {
		struct ieee80211_supported_band *sband =
			&sbands[IEEE80211_BAND_2GHZ];

		sband->bitrates = sc->rates;
		sband->channels = sc->channels;

		sband->band = IEEE80211_BAND_2GHZ;
		sband->n_channels = ath5k_copy_channels(ah, sband->channels,
					mode2g, max_c);

		hw_rates = ath5k_hw_get_rate_table(ah, mode2g);
		sband->n_bitrates = ath5k_copy_rates(sband->bitrates,
					hw_rates, max_r);

		count_c = sband->n_channels;
		count_r = sband->n_bitrates;

		hw->wiphy->bands[IEEE80211_BAND_2GHZ] = sband;

		max_r -= count_r;
		max_c -= count_c;

	}

	/* 5GHz band */

	if (test_bit(AR5K_MODE_11A, sc->ah->ah_capabilities.cap_mode)) {
		struct ieee80211_supported_band *sband =
			&sbands[IEEE80211_BAND_5GHZ];

		sband->bitrates = &sc->rates[count_r];
		sband->channels = &sc->channels[count_c];

		sband->band = IEEE80211_BAND_5GHZ;
		sband->n_channels = ath5k_copy_channels(ah, sband->channels,
					AR5K_MODE_11A, max_c);

		hw_rates = ath5k_hw_get_rate_table(ah, AR5K_MODE_11A);
		sband->n_bitrates = ath5k_copy_rates(sband->bitrates,
					hw_rates, max_r);

		hw->wiphy->bands[IEEE80211_BAND_5GHZ] = sband;
	}

	ath5k_debug_dump_bands(sc);

	return 0;
}

/*
 * Set/change channels.  If the channel is really being changed,
 * it's done by reseting the chip.  To accomplish this we must
 * first cleanup any pending DMA, then restart stuff after a la
 * ath5k_init.
 */
static int
ath5k_chan_set(struct ath5k_softc *sc, struct ieee80211_channel *chan)
{
	struct ath5k_hw *ah = sc->ah;
	int ret;

	ATH5K_DBG(sc, ATH5K_DEBUG_RESET, "(%u MHz) -> (%u MHz)\n",
		sc->curchan->center_freq, chan->center_freq);

	if (chan->center_freq != sc->curchan->center_freq ||
		chan->hw_value != sc->curchan->hw_value) {

		sc->curchan = chan;
		sc->curband = &sc->sbands[chan->band];

		/*
		 * To switch channels clear any pending DMA operations;
		 * wait long enough for the RX fifo to drain, reset the
		 * hardware at the new frequency, and then re-enable
		 * the relevant bits of the h/w.
		 */
		ath5k_hw_set_intr(ah, 0);	/* disable interrupts */
		ath5k_txq_cleanup(sc);		/* clear pending tx frames */
		ath5k_rx_stop(sc);		/* turn off frame recv */
		ret = ath5k_hw_reset(ah, sc->opmode, sc->curchan, true);
		if (ret) {
			ATH5K_ERR(sc, "%s: unable to reset channel "
				"(%u Mhz)\n", __func__, chan->center_freq);
			return ret;
		}

		ath5k_hw_set_txpower_limit(sc->ah, 0);

		/*
		 * Re-enable rx framework.
		 */
		ret = ath5k_rx_start(sc);
		if (ret) {
			ATH5K_ERR(sc, "%s: unable to restart recv logic\n",
					__func__);
			return ret;
		}

		/*
		 * Change channels and update the h/w rate map
		 * if we're switching; e.g. 11a to 11b/g.
		 *
		 * XXX needed?
		 */
/*		ath5k_chan_change(sc, chan); */

		ath5k_beacon_config(sc);
		/*
		 * Re-enable interrupts.
		 */
		ath5k_hw_set_intr(ah, sc->imask);
	}

	return 0;
}

static void
ath5k_setcurmode(struct ath5k_softc *sc, unsigned int mode)
{
	sc->curmode = mode;

	if (mode == AR5K_MODE_11A) {
		sc->curband = &sc->sbands[IEEE80211_BAND_5GHZ];
	} else {
		sc->curband = &sc->sbands[IEEE80211_BAND_2GHZ];
	}
}

static void
ath5k_mode_setup(struct ath5k_softc *sc)
{
	struct ath5k_hw *ah = sc->ah;
	u32 rfilt;

	/* configure rx filter */
	rfilt = sc->filter_flags;
	ath5k_hw_set_rx_filter(ah, rfilt);

	if (ath5k_hw_hasbssidmask(ah))
		ath5k_hw_set_bssid_mask(ah, sc->bssidmask);

	/* configure operational mode */
	ath5k_hw_set_opmode(ah);

	ath5k_hw_set_mcast_filter(ah, 0, 0);
	ATH5K_DBG(sc, ATH5K_DEBUG_MODE, "RX filter 0x%x\n", rfilt);
}

/*
 * Match the hw provided rate index (through descriptors)
 * to an index for sc->curband->bitrates, so it can be used
 * by the stack.
 *
 * This one is a little bit tricky but i think i'm right
 * about this...
 *
 * We have 4 rate tables in the following order:
 * XR (4 rates)
 * 802.11a (8 rates)
 * 802.11b (4 rates)
 * 802.11g (12 rates)
 * that make the hw rate table.
 *
 * Lets take a 5211 for example that supports a and b modes only.
 * First comes the 802.11a table and then 802.11b (total 12 rates).
 * When hw returns eg. 11 it points to the last 802.11b rate (11Mbit),
 * if it returns 2 it points to the second 802.11a rate etc.
 *
 * Same goes for 5212 who has xr/a/b/g support (total 28 rates).
 * First comes the XR table, then 802.11a, 802.11b and 802.11g.
 * When hw returns eg. 27 it points to the last 802.11g rate (54Mbits) etc
 */
static void
ath5k_set_total_hw_rates(struct ath5k_softc *sc) {

	struct ath5k_hw *ah = sc->ah;

	if (test_bit(AR5K_MODE_11A, ah->ah_modes))
		sc->a_rates = 8;

	if (test_bit(AR5K_MODE_11B, ah->ah_modes))
		sc->b_rates = 4;

	if (test_bit(AR5K_MODE_11G, ah->ah_modes))
		sc->g_rates = 12;

	/* XXX: Need to see what what happens when
		xr disable bits in eeprom are set */
	if (ah->ah_version >= AR5K_AR5212)
		sc->xr_rates = 4;

}

static inline int
ath5k_hw_to_driver_rix(struct ath5k_softc *sc, int hw_rix) {

	int mac80211_rix;

	if(sc->curband->band == IEEE80211_BAND_2GHZ) {
		/* We setup a g ratetable for both b/g modes */
		mac80211_rix =
			hw_rix - sc->b_rates - sc->a_rates - sc->xr_rates;
	} else {
		mac80211_rix = hw_rix - sc->xr_rates;
	}

	/* Something went wrong, fallback to basic rate for this band */
	if ((mac80211_rix >= sc->curband->n_bitrates) ||
		(mac80211_rix <= 0 ))
		mac80211_rix = 1;

	return mac80211_rix;
}




/***************\
* Buffers setup *
\***************/

static int
ath5k_rxbuf_setup(struct ath5k_softc *sc, struct ath5k_buf *bf)
{
	struct ath5k_hw *ah = sc->ah;
	struct sk_buff *skb = bf->skb;
	struct ath5k_desc *ds;

	if (likely(skb == NULL)) {
		unsigned int off;

		/*
		 * Allocate buffer with headroom_needed space for the
		 * fake physical layer header at the start.
		 */
		skb = dev_alloc_skb(sc->rxbufsize + sc->cachelsz - 1);
		if (unlikely(skb == NULL)) {
			ATH5K_ERR(sc, "can't alloc skbuff of size %u\n",
					sc->rxbufsize + sc->cachelsz - 1);
			return -ENOMEM;
		}
		/*
		 * Cache-line-align.  This is important (for the
		 * 5210 at least) as not doing so causes bogus data
		 * in rx'd frames.
		 */
		off = ((unsigned long)skb->data) % sc->cachelsz;
		if (off != 0)
			skb_reserve(skb, sc->cachelsz - off);

		bf->skb = skb;
		bf->skbaddr = pci_map_single(sc->pdev,
			skb->data, sc->rxbufsize, PCI_DMA_FROMDEVICE);
		if (unlikely(pci_dma_mapping_error(sc->pdev, bf->skbaddr))) {
			ATH5K_ERR(sc, "%s: DMA mapping failed\n", __func__);
			dev_kfree_skb(skb);
			bf->skb = NULL;
			return -ENOMEM;
		}
	}

	/*
	 * Setup descriptors.  For receive we always terminate
	 * the descriptor list with a self-linked entry so we'll
	 * not get overrun under high load (as can happen with a
	 * 5212 when ANI processing enables PHY error frames).
	 *
	 * To insure the last descriptor is self-linked we create
	 * each descriptor as self-linked and add it to the end.  As
	 * each additional descriptor is added the previous self-linked
	 * entry is ``fixed'' naturally.  This should be safe even
	 * if DMA is happening.  When processing RX interrupts we
	 * never remove/process the last, self-linked, entry on the
	 * descriptor list.  This insures the hardware always has
	 * someplace to write a new frame.
	 */
	ds = bf->desc;
	ds->ds_link = bf->daddr;	/* link to self */
	ds->ds_data = bf->skbaddr;
	ath5k_hw_setup_rx_desc(ah, ds,
		skb_tailroom(skb),	/* buffer size */
		0);

	if (sc->rxlink != NULL)
		*sc->rxlink = bf->daddr;
	sc->rxlink = &ds->ds_link;
	return 0;
}

static int
ath5k_txbuf_setup(struct ath5k_softc *sc, struct ath5k_buf *bf)
{
	struct ath5k_hw *ah = sc->ah;
	struct ath5k_txq *txq = sc->txq;
	struct ath5k_desc *ds = bf->desc;
	struct sk_buff *skb = bf->skb;
	struct ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	unsigned int pktlen, flags, keyidx = AR5K_TXKEYIX_INVALID;
	int ret;

	flags = AR5K_TXDESC_INTREQ | AR5K_TXDESC_CLRDMASK;

	/* XXX endianness */
	bf->skbaddr = pci_map_single(sc->pdev, skb->data, skb->len,
			PCI_DMA_TODEVICE);

	if (info->flags & IEEE80211_TX_CTL_NO_ACK)
		flags |= AR5K_TXDESC_NOACK;

	pktlen = skb->len;

	if (info->control.hw_key) {
		keyidx = info->control.hw_key->hw_key_idx;
		pktlen += info->control.icv_len;
	}
	ret = ah->ah_setup_tx_desc(ah, ds, pktlen,
		ieee80211_get_hdrlen_from_skb(skb), AR5K_PKT_TYPE_NORMAL,
		(sc->power_level * 2),
		ieee80211_get_tx_rate(sc->hw, info)->hw_value,
		info->control.retry_limit, keyidx, 0, flags, 0, 0);
	if (ret)
		goto err_unmap;

	ds->ds_link = 0;
	ds->ds_data = bf->skbaddr;

	spin_lock_bh(&txq->lock);
	list_add_tail(&bf->list, &txq->q);
	sc->tx_stats[txq->qnum].len++;
	if (txq->link == NULL) /* is this first packet? */
		ath5k_hw_put_tx_buf(ah, txq->qnum, bf->daddr);
	else /* no, so only link it */
		*txq->link = bf->daddr;

	txq->link = &ds->ds_link;
	ath5k_hw_tx_start(ah, txq->qnum);
	mmiowb();
	spin_unlock_bh(&txq->lock);

	return 0;
err_unmap:
	pci_unmap_single(sc->pdev, bf->skbaddr, skb->len, PCI_DMA_TODEVICE);
	return ret;
}

/*******************\
* Descriptors setup *
\*******************/

static int
ath5k_desc_alloc(struct ath5k_softc *sc, struct pci_dev *pdev)
{
	struct ath5k_desc *ds;
	struct ath5k_buf *bf;
	dma_addr_t da;
	unsigned int i;
	int ret;

	/* allocate descriptors */
	sc->desc_len = sizeof(struct ath5k_desc) *
			(ATH_TXBUF + ATH_RXBUF + ATH_BCBUF + 1);
	sc->desc = pci_alloc_consistent(pdev, sc->desc_len, &sc->desc_daddr);
	if (sc->desc == NULL) {
		ATH5K_ERR(sc, "can't allocate descriptors\n");
		ret = -ENOMEM;
		goto err;
	}
	ds = sc->desc;
	da = sc->desc_daddr;
	ATH5K_DBG(sc, ATH5K_DEBUG_ANY, "DMA map: %p (%zu) -> %llx\n",
		ds, sc->desc_len, (unsigned long long)sc->desc_daddr);

	bf = kcalloc(1 + ATH_TXBUF + ATH_RXBUF + ATH_BCBUF,
			sizeof(struct ath5k_buf), GFP_KERNEL);
	if (bf == NULL) {
		ATH5K_ERR(sc, "can't allocate bufptr\n");
		ret = -ENOMEM;
		goto err_free;
	}
	sc->bufptr = bf;

	INIT_LIST_HEAD(&sc->rxbuf);
	for (i = 0; i < ATH_RXBUF; i++, bf++, ds++, da += sizeof(*ds)) {
		bf->desc = ds;
		bf->daddr = da;
		list_add_tail(&bf->list, &sc->rxbuf);
	}

	INIT_LIST_HEAD(&sc->txbuf);
	sc->txbuf_len = ATH_TXBUF;
	for (i = 0; i < ATH_TXBUF; i++, bf++, ds++,
			da += sizeof(*ds)) {
		bf->desc = ds;
		bf->daddr = da;
		list_add_tail(&bf->list, &sc->txbuf);
	}

	/* beacon buffer */
	bf->desc = ds;
	bf->daddr = da;
	sc->bbuf = bf;

	return 0;
err_free:
	pci_free_consistent(pdev, sc->desc_len, sc->desc, sc->desc_daddr);
err:
	sc->desc = NULL;
	return ret;
}

static void
ath5k_desc_free(struct ath5k_softc *sc, struct pci_dev *pdev)
{
	struct ath5k_buf *bf;

	ath5k_txbuf_free(sc, sc->bbuf);
	list_for_each_entry(bf, &sc->txbuf, list)
		ath5k_txbuf_free(sc, bf);
	list_for_each_entry(bf, &sc->rxbuf, list)
		ath5k_txbuf_free(sc, bf);

	/* Free memory associated with all descriptors */
	pci_free_consistent(pdev, sc->desc_len, sc->desc, sc->desc_daddr);

	kfree(sc->bufptr);
	sc->bufptr = NULL;
}





/**************\
* Queues setup *
\**************/

static struct ath5k_txq *
ath5k_txq_setup(struct ath5k_softc *sc,
		int qtype, int subtype)
{
	struct ath5k_hw *ah = sc->ah;
	struct ath5k_txq *txq;
	struct ath5k_txq_info qi = {
		.tqi_subtype = subtype,
		.tqi_aifs = AR5K_TXQ_USEDEFAULT,
		.tqi_cw_min = AR5K_TXQ_USEDEFAULT,
		.tqi_cw_max = AR5K_TXQ_USEDEFAULT
	};
	int qnum;

	/*
	 * Enable interrupts only for EOL and DESC conditions.
	 * We mark tx descriptors to receive a DESC interrupt
	 * when a tx queue gets deep; otherwise waiting for the
	 * EOL to reap descriptors.  Note that this is done to
	 * reduce interrupt load and this only defers reaping
	 * descriptors, never transmitting frames.  Aside from
	 * reducing interrupts this also permits more concurrency.
	 * The only potential downside is if the tx queue backs
	 * up in which case the top half of the kernel may backup
	 * due to a lack of tx descriptors.
	 */
	qi.tqi_flags = AR5K_TXQ_FLAG_TXEOLINT_ENABLE |
				AR5K_TXQ_FLAG_TXDESCINT_ENABLE;
	qnum = ath5k_hw_setup_tx_queue(ah, qtype, &qi);
	if (qnum < 0) {
		/*
		 * NB: don't print a message, this happens
		 * normally on parts with too few tx queues
		 */
		return ERR_PTR(qnum);
	}
	if (qnum >= ARRAY_SIZE(sc->txqs)) {
		ATH5K_ERR(sc, "hw qnum %u out of range, max %tu!\n",
			qnum, ARRAY_SIZE(sc->txqs));
		ath5k_hw_release_tx_queue(ah, qnum);
		return ERR_PTR(-EINVAL);
	}
	txq = &sc->txqs[qnum];
	if (!txq->setup) {
		txq->qnum = qnum;
		txq->link = NULL;
		INIT_LIST_HEAD(&txq->q);
		spin_lock_init(&txq->lock);
		txq->setup = true;
	}
	return &sc->txqs[qnum];
}

static int
ath5k_beaconq_setup(struct ath5k_hw *ah)
{
	struct ath5k_txq_info qi = {
		.tqi_aifs = AR5K_TXQ_USEDEFAULT,
		.tqi_cw_min = AR5K_TXQ_USEDEFAULT,
		.tqi_cw_max = AR5K_TXQ_USEDEFAULT,
		/* NB: for dynamic turbo, don't enable any other interrupts */
		.tqi_flags = AR5K_TXQ_FLAG_TXDESCINT_ENABLE
	};

	return ath5k_hw_setup_tx_queue(ah, AR5K_TX_QUEUE_BEACON, &qi);
}

static int
ath5k_beaconq_config(struct ath5k_softc *sc)
{
	struct ath5k_hw *ah = sc->ah;
	struct ath5k_txq_info qi;
	int ret;

	ret = ath5k_hw_get_tx_queueprops(ah, sc->bhalq, &qi);
	if (ret)
		return ret;
	if (sc->opmode == IEEE80211_IF_TYPE_AP) {
		/*
		 * Always burst out beacon and CAB traffic
		 * (aifs = cwmin = cwmax = 0)
		 */
		qi.tqi_aifs = 0;
		qi.tqi_cw_min = 0;
		qi.tqi_cw_max = 0;
	} else if (sc->opmode == IEEE80211_IF_TYPE_IBSS) {
		/*
		 * Adhoc mode; backoff between 0 and (2 * cw_min).
		 */
		qi.tqi_aifs = 0;
		qi.tqi_cw_min = 0;
		qi.tqi_cw_max = 2 * ah->ah_cw_min;
	}

	ATH5K_DBG(sc, ATH5K_DEBUG_BEACON,
		"beacon queueprops tqi_aifs:%d tqi_cw_min:%d tqi_cw_max:%d\n",
		qi.tqi_aifs, qi.tqi_cw_min, qi.tqi_cw_max);

	ret = ath5k_hw_setup_tx_queueprops(ah, sc->bhalq, &qi);
	if (ret) {
		ATH5K_ERR(sc, "%s: unable to update parameters for beacon "
			"hardware queue!\n", __func__);
		return ret;
	}

	return ath5k_hw_reset_tx_queue(ah, sc->bhalq); /* push to h/w */;
}

static void
ath5k_txq_drainq(struct ath5k_softc *sc, struct ath5k_txq *txq)
{
	struct ath5k_buf *bf, *bf0;

	/*
	 * NB: this assumes output has been stopped and
	 *     we do not need to block ath5k_tx_tasklet
	 */
	spin_lock_bh(&txq->lock);
	list_for_each_entry_safe(bf, bf0, &txq->q, list) {
		ath5k_debug_printtxbuf(sc, bf);

		ath5k_txbuf_free(sc, bf);

		spin_lock_bh(&sc->txbuflock);
		sc->tx_stats[txq->qnum].len--;
		list_move_tail(&bf->list, &sc->txbuf);
		sc->txbuf_len++;
		spin_unlock_bh(&sc->txbuflock);
	}
	txq->link = NULL;
	spin_unlock_bh(&txq->lock);
}

/*
 * Drain the transmit queues and reclaim resources.
 */
static void
ath5k_txq_cleanup(struct ath5k_softc *sc)
{
	struct ath5k_hw *ah = sc->ah;
	unsigned int i;

	/* XXX return value */
	if (likely(!test_bit(ATH_STAT_INVALID, sc->status))) {
		/* don't touch the hardware if marked invalid */
		ath5k_hw_stop_tx_dma(ah, sc->bhalq);
		ATH5K_DBG(sc, ATH5K_DEBUG_RESET, "beacon queue %x\n",
			ath5k_hw_get_tx_buf(ah, sc->bhalq));
		for (i = 0; i < ARRAY_SIZE(sc->txqs); i++)
			if (sc->txqs[i].setup) {
				ath5k_hw_stop_tx_dma(ah, sc->txqs[i].qnum);
				ATH5K_DBG(sc, ATH5K_DEBUG_RESET, "txq [%u] %x, "
					"link %p\n",
					sc->txqs[i].qnum,
					ath5k_hw_get_tx_buf(ah,
							sc->txqs[i].qnum),
					sc->txqs[i].link);
			}
	}
	ieee80211_wake_queues(sc->hw); /* XXX move to callers */

	for (i = 0; i < ARRAY_SIZE(sc->txqs); i++)
		if (sc->txqs[i].setup)
			ath5k_txq_drainq(sc, &sc->txqs[i]);
}

static void
ath5k_txq_release(struct ath5k_softc *sc)
{
	struct ath5k_txq *txq = sc->txqs;
	unsigned int i;

	for (i = 0; i < ARRAY_SIZE(sc->txqs); i++, txq++)
		if (txq->setup) {
			ath5k_hw_release_tx_queue(sc->ah, txq->qnum);
			txq->setup = false;
		}
}




/*************\
* RX Handling *
\*************/

/*
 * Enable the receive h/w following a reset.
 */
static int
ath5k_rx_start(struct ath5k_softc *sc)
{
	struct ath5k_hw *ah = sc->ah;
	struct ath5k_buf *bf;
	int ret;

	sc->rxbufsize = roundup(IEEE80211_MAX_LEN, sc->cachelsz);

	ATH5K_DBG(sc, ATH5K_DEBUG_RESET, "cachelsz %u rxbufsize %u\n",
		sc->cachelsz, sc->rxbufsize);

	sc->rxlink = NULL;

	spin_lock_bh(&sc->rxbuflock);
	list_for_each_entry(bf, &sc->rxbuf, list) {
		ret = ath5k_rxbuf_setup(sc, bf);
		if (ret != 0) {
			spin_unlock_bh(&sc->rxbuflock);
			goto err;
		}
	}
	bf = list_first_entry(&sc->rxbuf, struct ath5k_buf, list);
	spin_unlock_bh(&sc->rxbuflock);

	ath5k_hw_put_rx_buf(ah, bf->daddr);
	ath5k_hw_start_rx(ah);		/* enable recv descriptors */
	ath5k_mode_setup(sc);		/* set filters, etc. */
	ath5k_hw_start_rx_pcu(ah);	/* re-enable PCU/DMA engine */

	return 0;
err:
	return ret;
}

/*
 * Disable the receive h/w in preparation for a reset.
 */
static void
ath5k_rx_stop(struct ath5k_softc *sc)
{
	struct ath5k_hw *ah = sc->ah;

	ath5k_hw_stop_pcu_recv(ah);	/* disable PCU */
	ath5k_hw_set_rx_filter(ah, 0);	/* clear recv filter */
	ath5k_hw_stop_rx_dma(ah);	/* disable DMA engine */

	ath5k_debug_printrxbuffs(sc, ah);

	sc->rxlink = NULL;		/* just in case */
}

static unsigned int
ath5k_rx_decrypted(struct ath5k_softc *sc, struct ath5k_desc *ds,
		struct sk_buff *skb, struct ath5k_rx_status *rs)
{
	struct ieee80211_hdr *hdr = (void *)skb->data;
	unsigned int keyix, hlen = ieee80211_get_hdrlen_from_skb(skb);

	if (!(rs->rs_status & AR5K_RXERR_DECRYPT) &&
			rs->rs_keyix != AR5K_RXKEYIX_INVALID)
		return RX_FLAG_DECRYPTED;

	/* Apparently when a default key is used to decrypt the packet
	   the hw does not set the index used to decrypt.  In such cases
	   get the index from the packet. */
	if (ieee80211_has_protected(hdr->frame_control) &&
	    !(rs->rs_status & AR5K_RXERR_DECRYPT) &&
	    skb->len >= hlen + 4) {
		keyix = skb->data[hlen + 3] >> 6;

		if (test_bit(keyix, sc->keymap))
			return RX_FLAG_DECRYPTED;
	}

	return 0;
}


static void
ath5k_check_ibss_tsf(struct ath5k_softc *sc, struct sk_buff *skb,
		     struct ieee80211_rx_status *rxs)
{
	u64 tsf, bc_tstamp;
	u32 hw_tu;
	struct ieee80211_mgmt *mgmt = (struct ieee80211_mgmt *)skb->data;

	if (ieee80211_is_beacon(mgmt->frame_control) &&
	    le16_to_cpu(mgmt->u.beacon.capab_info) & WLAN_CAPABILITY_IBSS &&
	    memcmp(mgmt->bssid, sc->ah->ah_bssid, ETH_ALEN) == 0) {
		/*
		 * Received an IBSS beacon with the same BSSID. Hardware *must*
		 * have updated the local TSF. We have to work around various
		 * hardware bugs, though...
		 */
		tsf = ath5k_hw_get_tsf64(sc->ah);
		bc_tstamp = le64_to_cpu(mgmt->u.beacon.timestamp);
		hw_tu = TSF_TO_TU(tsf);

		ATH5K_DBG_UNLIMIT(sc, ATH5K_DEBUG_BEACON,
			"beacon %llx mactime %llx (diff %lld) tsf now %llx\n",
			(unsigned long long)bc_tstamp,
			(unsigned long long)rxs->mactime,
			(unsigned long long)(rxs->mactime - bc_tstamp),
			(unsigned long long)tsf);

		/*
		 * Sometimes the HW will give us a wrong tstamp in the rx
		 * status, causing the timestamp extension to go wrong.
		 * (This seems to happen especially with beacon frames bigger
		 * than 78 byte (incl. FCS))
		 * But we know that the receive timestamp must be later than the
		 * timestamp of the beacon since HW must have synced to that.
		 *
		 * NOTE: here we assume mactime to be after the frame was
		 * received, not like mac80211 which defines it at the start.
		 */
		if (bc_tstamp > rxs->mactime) {
			ATH5K_DBG_UNLIMIT(sc, ATH5K_DEBUG_BEACON,
				"fixing mactime from %llx to %llx\n",
				(unsigned long long)rxs->mactime,
				(unsigned long long)tsf);
			rxs->mactime = tsf;
		}

		/*
		 * Local TSF might have moved higher than our beacon timers,
		 * in that case we have to update them to continue sending
		 * beacons. This also takes care of synchronizing beacon sending
		 * times with other stations.
		 */
		if (hw_tu >= sc->nexttbtt)
			ath5k_beacon_update_timers(sc, bc_tstamp);
	}
}


static void
ath5k_tasklet_rx(unsigned long data)
{
	struct ieee80211_rx_status rxs = {};
	struct ath5k_rx_status rs = {};
	struct sk_buff *skb;
	struct ath5k_softc *sc = (void *)data;
	struct ath5k_buf *bf, *bf_last;
	struct ath5k_desc *ds;
	int ret;
	int hdrlen;
	int pad;

	spin_lock(&sc->rxbuflock);
	if (list_empty(&sc->rxbuf)) {
		ATH5K_WARN(sc, "empty rx buf pool\n");
		goto unlock;
	}
	bf_last = list_entry(sc->rxbuf.prev, struct ath5k_buf, list);
	do {
		rxs.flag = 0;

		bf = list_first_entry(&sc->rxbuf, struct ath5k_buf, list);
		BUG_ON(bf->skb == NULL);
		skb = bf->skb;
		ds = bf->desc;

		/*
		 * last buffer must not be freed to ensure proper hardware
		 * function. When the hardware finishes also a packet next to
		 * it, we are sure, it doesn't use it anymore and we can go on.
		 */
		if (bf_last == bf)
			bf->flags |= 1;
		if (bf->flags) {
			struct ath5k_buf *bf_next = list_entry(bf->list.next,
					struct ath5k_buf, list);
			ret = sc->ah->ah_proc_rx_desc(sc->ah, bf_next->desc,
					&rs);
			if (ret)
				break;
			bf->flags &= ~1;
			/* skip the overwritten one (even status is martian) */
			goto next;
		}

		ret = sc->ah->ah_proc_rx_desc(sc->ah, ds, &rs);
		if (unlikely(ret == -EINPROGRESS))
			break;
		else if (unlikely(ret)) {
			ATH5K_ERR(sc, "error in processing rx descriptor\n");
			spin_unlock(&sc->rxbuflock);
			return;
		}

		if (unlikely(rs.rs_more)) {
			ATH5K_WARN(sc, "unsupported jumbo\n");
			goto next;
		}

		if (unlikely(rs.rs_status)) {
			if (rs.rs_status & AR5K_RXERR_PHY)
				goto next;
			if (rs.rs_status & AR5K_RXERR_DECRYPT) {
				/*
				 * Decrypt error.  If the error occurred
				 * because there was no hardware key, then
				 * let the frame through so the upper layers
				 * can process it.  This is necessary for 5210
				 * parts which have no way to setup a ``clear''
				 * key cache entry.
				 *
				 * XXX do key cache faulting
				 */
				if (rs.rs_keyix == AR5K_RXKEYIX_INVALID &&
				    !(rs.rs_status & AR5K_RXERR_CRC))
					goto accept;
			}
			if (rs.rs_status & AR5K_RXERR_MIC) {
				rxs.flag |= RX_FLAG_MMIC_ERROR;
				goto accept;
			}

			/* let crypto-error packets fall through in MNTR */
			if ((rs.rs_status &
				~(AR5K_RXERR_DECRYPT|AR5K_RXERR_MIC)) ||
					sc->opmode != IEEE80211_IF_TYPE_MNTR)
				goto next;
		}
accept:
		pci_unmap_single(sc->pdev, bf->skbaddr, sc->rxbufsize,
				PCI_DMA_FROMDEVICE);
		bf->skb = NULL;

		skb_put(skb, rs.rs_datalen);

		/*
		 * the hardware adds a padding to 4 byte boundaries between
		 * the header and the payload data if the header length is
		 * not multiples of 4 - remove it
		 */
		hdrlen = ieee80211_get_hdrlen_from_skb(skb);
		if (hdrlen & 3) {
			pad = hdrlen % 4;
			memmove(skb->data + pad, skb->data, hdrlen);
			skb_pull(skb, pad);
		}

		/*
		 * always extend the mac timestamp, since this information is
		 * also needed for proper IBSS merging.
		 *
		 * XXX: it might be too late to do it here, since rs_tstamp is
		 * 15bit only. that means TSF extension has to be done within
		 * 32768usec (about 32ms). it might be necessary to move this to
		 * the interrupt handler, like it is done in madwifi.
		 *
		 * Unfortunately we don't know when the hardware takes the rx
		 * timestamp (beginning of phy frame, data frame, end of rx?).
		 * The only thing we know is that it is hardware specific...
		 * On AR5213 it seems the rx timestamp is at the end of the
		 * frame, but i'm not sure.
		 *
		 * NOTE: mac80211 defines mactime at the beginning of the first
		 * data symbol. Since we don't have any time references it's
		 * impossible to comply to that. This affects IBSS merge only
		 * right now, so it's not too bad...
		 */
		rxs.mactime = ath5k_extend_tsf(sc->ah, rs.rs_tstamp);
		rxs.flag |= RX_FLAG_TSFT;

		rxs.freq = sc->curchan->center_freq;
		rxs.band = sc->curband->band;

		rxs.noise = sc->ah->ah_noise_floor;
		rxs.signal = rxs.noise + rs.rs_rssi;
		rxs.qual = rs.rs_rssi * 100 / 64;

		rxs.antenna = rs.rs_antenna;
		rxs.rate_idx = ath5k_hw_to_driver_rix(sc, rs.rs_rate);
		rxs.flag |= ath5k_rx_decrypted(sc, ds, skb, &rs);

		ath5k_debug_dump_skb(sc, skb, "RX  ", 0);

		/* check beacons in IBSS mode */
		if (sc->opmode == IEEE80211_IF_TYPE_IBSS)
			ath5k_check_ibss_tsf(sc, skb, &rxs);

		__ieee80211_rx(sc->hw, skb, &rxs);
next:
		list_move_tail(&bf->list, &sc->rxbuf);
	} while (ath5k_rxbuf_setup(sc, bf) == 0);
unlock:
	spin_unlock(&sc->rxbuflock);
}




/*************\
* TX Handling *
\*************/

static void
ath5k_tx_processq(struct ath5k_softc *sc, struct ath5k_txq *txq)
{
	struct ath5k_tx_status ts = {};
	struct ath5k_buf *bf, *bf0;
	struct ath5k_desc *ds;
	struct sk_buff *skb;
	struct ieee80211_tx_info *info;
	int ret;

	spin_lock(&txq->lock);
	list_for_each_entry_safe(bf, bf0, &txq->q, list) {
		ds = bf->desc;

		ret = sc->ah->ah_proc_tx_desc(sc->ah, ds, &ts);
		if (unlikely(ret == -EINPROGRESS))
			break;
		else if (unlikely(ret)) {
			ATH5K_ERR(sc, "error %d while processing queue %u\n",
				ret, txq->qnum);
			break;
		}

		skb = bf->skb;
		info = IEEE80211_SKB_CB(skb);
		bf->skb = NULL;

		pci_unmap_single(sc->pdev, bf->skbaddr, skb->len,
				PCI_DMA_TODEVICE);

		info->status.retry_count = ts.ts_shortretry + ts.ts_longretry / 6;
		if (unlikely(ts.ts_status)) {
			sc->ll_stats.dot11ACKFailureCount++;
			if (ts.ts_status & AR5K_TXERR_XRETRY)
				info->status.excessive_retries = 1;
			else if (ts.ts_status & AR5K_TXERR_FILT)
				info->flags |= IEEE80211_TX_STAT_TX_FILTERED;
		} else {
			info->flags |= IEEE80211_TX_STAT_ACK;
			info->status.ack_signal = ts.ts_rssi;
		}

		ieee80211_tx_status(sc->hw, skb);
		sc->tx_stats[txq->qnum].count++;

		spin_lock(&sc->txbuflock);
		sc->tx_stats[txq->qnum].len--;
		list_move_tail(&bf->list, &sc->txbuf);
		sc->txbuf_len++;
		spin_unlock(&sc->txbuflock);
	}
	if (likely(list_empty(&txq->q)))
		txq->link = NULL;
	spin_unlock(&txq->lock);
	if (sc->txbuf_len > ATH_TXBUF / 5)
		ieee80211_wake_queues(sc->hw);
}

static void
ath5k_tasklet_tx(unsigned long data)
{
	struct ath5k_softc *sc = (void *)data;

	ath5k_tx_processq(sc, sc->txq);
}


/*****************\
* Beacon handling *
\*****************/

/*
 * Setup the beacon frame for transmit.
 */
static int
ath5k_beacon_setup(struct ath5k_softc *sc, struct ath5k_buf *bf)
{
	struct sk_buff *skb = bf->skb;
	struct	ieee80211_tx_info *info = IEEE80211_SKB_CB(skb);
	struct ath5k_hw *ah = sc->ah;
	struct ath5k_desc *ds;
	int ret, antenna = 0;
	u32 flags;

	bf->skbaddr = pci_map_single(sc->pdev, skb->data, skb->len,
			PCI_DMA_TODEVICE);
	ATH5K_DBG(sc, ATH5K_DEBUG_BEACON, "skb %p [data %p len %u] "
			"skbaddr %llx\n", skb, skb->data, skb->len,
			(unsigned long long)bf->skbaddr);
	if (pci_dma_mapping_error(sc->pdev, bf->skbaddr)) {
		ATH5K_ERR(sc, "beacon DMA mapping failed\n");
		return -EIO;
	}

	ds = bf->desc;

	flags = AR5K_TXDESC_NOACK;
	if (sc->opmode == IEEE80211_IF_TYPE_IBSS && ath5k_hw_hasveol(ah)) {
		ds->ds_link = bf->daddr;	/* self-linked */
		flags |= AR5K_TXDESC_VEOL;
		/*
		 * Let hardware handle antenna switching if txantenna is not set
		 */
	} else {
		ds->ds_link = 0;
		/*
		 * Switch antenna every 4 beacons if txantenna is not set
		 * XXX assumes two antennas
		 */
		if (antenna == 0)
			antenna = sc->bsent & 4 ? 2 : 1;
	}

	ds->ds_data = bf->skbaddr;
	ret = ah->ah_setup_tx_desc(ah, ds, skb->len,
			ieee80211_get_hdrlen_from_skb(skb),
			AR5K_PKT_TYPE_BEACON, (sc->power_level * 2),
			ieee80211_get_tx_rate(sc->hw, info)->hw_value,
			1, AR5K_TXKEYIX_INVALID,
			antenna, flags, 0, 0);
	if (ret)
		goto err_unmap;

	return 0;
err_unmap:
	pci_unmap_single(sc->pdev, bf->skbaddr, skb->len, PCI_DMA_TODEVICE);
	return ret;
}

/*
 * Transmit a beacon frame at SWBA.  Dynamic updates to the
 * frame contents are done as needed and the slot time is
 * also adjusted based on current state.
 *
 * this is usually called from interrupt context (ath5k_intr())
 * but also from ath5k_beacon_config() in IBSS mode which in turn
 * can be called from a tasklet and user context
 */
static void
ath5k_beacon_send(struct ath5k_softc *sc)
{
	struct ath5k_buf *bf = sc->bbuf;
	struct ath5k_hw *ah = sc->ah;

	ATH5K_DBG_UNLIMIT(sc, ATH5K_DEBUG_BEACON, "in beacon_send\n");

	if (unlikely(bf->skb == NULL || sc->opmode == IEEE80211_IF_TYPE_STA ||
			sc->opmode == IEEE80211_IF_TYPE_MNTR)) {
		ATH5K_WARN(sc, "bf=%p bf_skb=%p\n", bf, bf ? bf->skb : NULL);
		return;
	}
	/*
	 * Check if the previous beacon has gone out.  If
	 * not don't don't try to post another, skip this
	 * period and wait for the next.  Missed beacons
	 * indicate a problem and should not occur.  If we
	 * miss too many consecutive beacons reset the device.
	 */
	if (unlikely(ath5k_hw_num_tx_pending(ah, sc->bhalq) != 0)) {
		sc->bmisscount++;
		ATH5K_DBG(sc, ATH5K_DEBUG_BEACON,
			"missed %u consecutive beacons\n", sc->bmisscount);
		if (sc->bmisscount > 3) {		/* NB: 3 is a guess */
			ATH5K_DBG(sc, ATH5K_DEBUG_BEACON,
				"stuck beacon time (%u missed)\n",
				sc->bmisscount);
			tasklet_schedule(&sc->restq);
		}
		return;
	}
	if (unlikely(sc->bmisscount != 0)) {
		ATH5K_DBG(sc, ATH5K_DEBUG_BEACON,
			"resume beacon xmit after %u misses\n",
			sc->bmisscount);
		sc->bmisscount = 0;
	}

	/*
	 * Stop any current dma and put the new frame on the queue.
	 * This should never fail since we check above that no frames
	 * are still pending on the queue.
	 */
	if (unlikely(ath5k_hw_stop_tx_dma(ah, sc->bhalq))) {
		ATH5K_WARN(sc, "beacon queue %u didn't stop?\n", sc->bhalq);
		/* NB: hw still stops DMA, so proceed */
	}

	ath5k_hw_put_tx_buf(ah, sc->bhalq, bf->daddr);
	ath5k_hw_tx_start(ah, sc->bhalq);
	ATH5K_DBG(sc, ATH5K_DEBUG_BEACON, "TXDP[%u] = %llx (%p)\n",
		sc->bhalq, (unsigned long long)bf->daddr, bf->desc);

	sc->bsent++;
}


/**
 * ath5k_beacon_update_timers - update beacon timers
 *
 * @sc: struct ath5k_softc pointer we are operating on
 * @bc_tsf: the timestamp of the beacon. 0 to reset the TSF. -1 to perform a
 *          beacon timer update based on the current HW TSF.
 *
 * Calculate the next target beacon transmit time (TBTT) based on the timestamp
 * of a received beacon or the current local hardware TSF and write it to the
 * beacon timer registers.
 *
 * This is called in a variety of situations, e.g. when a beacon is received,
 * when a TSF update has been detected, but also when an new IBSS is created or
 * when we otherwise know we have to update the timers, but we keep it in this
 * function to have it all together in one place.
 */
static void
ath5k_beacon_update_timers(struct ath5k_softc *sc, u64 bc_tsf)
{
	struct ath5k_hw *ah = sc->ah;
	u32 nexttbtt, intval, hw_tu, bc_tu;
	u64 hw_tsf;

	intval = sc->bintval & AR5K_BEACON_PERIOD;
	if (WARN_ON(!intval))
		return;

	/* beacon TSF converted to TU */
	bc_tu = TSF_TO_TU(bc_tsf);

	/* current TSF converted to TU */
	hw_tsf = ath5k_hw_get_tsf64(ah);
	hw_tu = TSF_TO_TU(hw_tsf);

#define FUDGE 3
	/* we use FUDGE to make sure the next TBTT is ahead of the current TU */
	if (bc_tsf == -1) {
		/*
		 * no beacons received, called internally.
		 * just need to refresh timers based on HW TSF.
		 */
		nexttbtt = roundup(hw_tu + FUDGE, intval);
	} else if (bc_tsf == 0) {
		/*
		 * no beacon received, probably called by ath5k_reset_tsf().
		 * reset TSF to start with 0.
		 */
		nexttbtt = intval;
		intval |= AR5K_BEACON_RESET_TSF;
	} else if (bc_tsf > hw_tsf) {
		/*
		 * beacon received, SW merge happend but HW TSF not yet updated.
		 * not possible to reconfigure timers yet, but next time we
		 * receive a beacon with the same BSSID, the hardware will
		 * automatically update the TSF and then we need to reconfigure
		 * the timers.
		 */
		ATH5K_DBG_UNLIMIT(sc, ATH5K_DEBUG_BEACON,
			"need to wait for HW TSF sync\n");
		return;
	} else {
		/*
		 * most important case for beacon synchronization between STA.
		 *
		 * beacon received and HW TSF has been already updated by HW.
		 * update next TBTT based on the TSF of the beacon, but make
		 * sure it is ahead of our local TSF timer.
		 */
		nexttbtt = bc_tu + roundup(hw_tu + FUDGE - bc_tu, intval);
	}
#undef FUDGE

	sc->nexttbtt = nexttbtt;

	intval |= AR5K_BEACON_ENA;
	ath5k_hw_init_beacon(ah, nexttbtt, intval);

	/*
	 * debugging output last in order to preserve the time critical aspect
	 * of this function
	 */
	if (bc_tsf == -1)
		ATH5K_DBG_UNLIMIT(sc, ATH5K_DEBUG_BEACON,
			"reconfigured timers based on HW TSF\n");
	else if (bc_tsf == 0)
		ATH5K_DBG_UNLIMIT(sc, ATH5K_DEBUG_BEACON,
			"reset HW TSF and timers\n");
	else
		ATH5K_DBG_UNLIMIT(sc, ATH5K_DEBUG_BEACON,
			"updated timers based on beacon TSF\n");

	ATH5K_DBG_UNLIMIT(sc, ATH5K_DEBUG_BEACON,
			  "bc_tsf %llx hw_tsf %llx bc_tu %u hw_tu %u nexttbtt %u\n",
			  (unsigned long long) bc_tsf,
			  (unsigned long long) hw_tsf, bc_tu, hw_tu, nexttbtt);
	ATH5K_DBG_UNLIMIT(sc, ATH5K_DEBUG_BEACON, "intval %u %s %s\n",
		intval & AR5K_BEACON_PERIOD,
		intval & AR5K_BEACON_ENA ? "AR5K_BEACON_ENA" : "",
		intval & AR5K_BEACON_RESET_TSF ? "AR5K_BEACON_RESET_TSF" : "");
}


/**
 * ath5k_beacon_config - Configure the beacon queues and interrupts
 *
 * @sc: struct ath5k_softc pointer we are operating on
 *
 * When operating in station mode we want to receive a BMISS interrupt when we
 * stop seeing beacons from the AP we've associated with so we can look for
 * another AP to associate with.
 *
 * In IBSS mode we use a self-linked tx descriptor if possible. We enable SWBA
 * interrupts to detect TSF updates only.
 *
 * AP mode is missing.
 */
static void
ath5k_beacon_config(struct ath5k_softc *sc)
{
	struct ath5k_hw *ah = sc->ah;

	ath5k_hw_set_intr(ah, 0);
	sc->bmisscount = 0;
	sc->imask &= ~(AR5K_INT_BMISS | AR5K_INT_SWBA);

	if (sc->opmode == IEEE80211_IF_TYPE_STA) {
		sc->imask |= AR5K_INT_BMISS;
	} else if (sc->opmode == IEEE80211_IF_TYPE_IBSS) {
		/*
		 * In IBSS mode we use a self-linked tx descriptor and let the
		 * hardware send the beacons automatically. We have to load it
		 * only once here.
		 * We use the SWBA interrupt only to keep track of the beacon
		 * timers in order to detect automatic TSF updates.
		 */
		ath5k_beaconq_config(sc);

		sc->imask |= AR5K_INT_SWBA;

		if (ath5k_hw_hasveol(ah)) {
			spin_lock(&sc->block);
			ath5k_beacon_send(sc);
			spin_unlock(&sc->block);
		}
	}
	/* TODO else AP */

	ath5k_hw_set_intr(ah, sc->imask);
}


/********************\
* Interrupt handling *
\********************/

static int
ath5k_init(struct ath5k_softc *sc)
{
	int ret;

	mutex_lock(&sc->lock);

	ATH5K_DBG(sc, ATH5K_DEBUG_RESET, "mode %d\n", sc->opmode);

	/*
	 * Stop anything previously setup.  This is safe
	 * no matter this is the first time through or not.
	 */
	ath5k_stop_locked(sc);

	/*
	 * The basic interface to setting the hardware in a good
	 * state is ``reset''.  On return the hardware is known to
	 * be powered up and with interrupts disabled.  This must
	 * be followed by initialization of the appropriate bits
	 * and then setup of the interrupt mask.
	 */
	sc->curchan = sc->hw->conf.channel;
	sc->curband = &sc->sbands[sc->curchan->band];
	ret = ath5k_hw_reset(sc->ah, sc->opmode, sc->curchan, false);
	if (ret) {
		ATH5K_ERR(sc, "unable to reset hardware: %d\n", ret);
		goto done;
	}
	/*
	 * This is needed only to setup initial state
	 * but it's best done after a reset.
	 */
	ath5k_hw_set_txpower_limit(sc->ah, 0);

	/*
	 * Setup the hardware after reset: the key cache
	 * is filled as needed and the receive engine is
	 * set going.  Frame transmit is handled entirely
	 * in the frame output path; there's nothing to do
	 * here except setup the interrupt mask.
	 */
	ret = ath5k_rx_start(sc);
	if (ret)
		goto done;

	/*
	 * Enable interrupts.
	 */
	sc->imask = AR5K_INT_RX | AR5K_INT_TX | AR5K_INT_RXEOL |
		AR5K_INT_RXORN | AR5K_INT_FATAL | AR5K_INT_GLOBAL |
		AR5K_INT_MIB;

	ath5k_hw_set_intr(sc->ah, sc->imask);
	/* Set ack to be sent at low bit-rates */
	ath5k_hw_set_ack_bitrate_high(sc->ah, false);

	mod_timer(&sc->calib_tim, round_jiffies(jiffies +
			msecs_to_jiffies(ath5k_calinterval * 1000)));

	ret = 0;
done:
	mmiowb();
	mutex_unlock(&sc->lock);
	return ret;
}

static int
ath5k_stop_locked(struct ath5k_softc *sc)
{
	struct ath5k_hw *ah = sc->ah;

	ATH5K_DBG(sc, ATH5K_DEBUG_RESET, "invalid %u\n",
			test_bit(ATH_STAT_INVALID, sc->status));

	/*
	 * Shutdown the hardware and driver:
	 *    stop output from above
	 *    disable interrupts
	 *    turn off timers
	 *    turn off the radio
	 *    clear transmit machinery
	 *    clear receive machinery
	 *    drain and release tx queues
	 *    reclaim beacon resources
	 *    power down hardware
	 *
	 * Note that some of this work is not possible if the
	 * hardware is gone (invalid).
	 */
	ieee80211_stop_queues(sc->hw);

	if (!test_bit(ATH_STAT_INVALID, sc->status)) {
		ath5k_led_off(sc);
		ath5k_hw_set_intr(ah, 0);
		synchronize_irq(sc->pdev->irq);
	}
	ath5k_txq_cleanup(sc);
	if (!test_bit(ATH_STAT_INVALID, sc->status)) {
		ath5k_rx_stop(sc);
		ath5k_hw_phy_disable(ah);
	} else
		sc->rxlink = NULL;

	return 0;
}

/*
 * Stop the device, grabbing the top-level lock to protect
 * against concurrent entry through ath5k_init (which can happen
 * if another thread does a system call and the thread doing the
 * stop is preempted).
 */
static int
ath5k_stop_hw(struct ath5k_softc *sc)
{
	int ret;

	mutex_lock(&sc->lock);
	ret = ath5k_stop_locked(sc);
	if (ret == 0 && !test_bit(ATH_STAT_INVALID, sc->status)) {
		/*
		 * Set the chip in full sleep mode.  Note that we are
		 * careful to do this only when bringing the interface
		 * completely to a stop.  When the chip is in this state
		 * it must be carefully woken up or references to
		 * registers in the PCI clock domain may freeze the bus
		 * (and system).  This varies by chip and is mostly an
		 * issue with newer parts that go to sleep more quickly.
		 */
		if (sc->ah->ah_mac_srev >= 0x78) {
			/*
			 * XXX
			 * don't put newer MAC revisions > 7.8 to sleep because
			 * of the above mentioned problems
			 */
			ATH5K_DBG(sc, ATH5K_DEBUG_RESET, "mac version > 7.8, "
				"not putting device to sleep\n");
		} else {
			ATH5K_DBG(sc, ATH5K_DEBUG_RESET,
				"putting device to full sleep\n");
			ath5k_hw_set_power(sc->ah, AR5K_PM_FULL_SLEEP, true, 0);
		}
	}
	ath5k_txbuf_free(sc, sc->bbuf);
	mmiowb();
	mutex_unlock(&sc->lock);

	del_timer_sync(&sc->calib_tim);
	tasklet_kill(&sc->rxtq);
	tasklet_kill(&sc->txtq);
	tasklet_kill(&sc->restq);

	return ret;
}

static irqreturn_t
ath5k_intr(int irq, void *dev_id)
{
	struct ath5k_softc *sc = dev_id;
	struct ath5k_hw *ah = sc->ah;
	enum ath5k_int status;
	unsigned int counter = 1000;

	if (unlikely(test_bit(ATH_STAT_INVALID, sc->status) ||
				!ath5k_hw_is_intr_pending(ah)))
		return IRQ_NONE;

	do {
		/*
		 * Figure out the reason(s) for the interrupt.  Note
		 * that get_isr returns a pseudo-ISR that may include
		 * bits we haven't explicitly enabled so we mask the
		 * value to insure we only process bits we requested.
		 */
		ath5k_hw_get_isr(ah, &status);		/* NB: clears IRQ too */
		ATH5K_DBG(sc, ATH5K_DEBUG_INTR, "status 0x%x/0x%x\n",
				status, sc->imask);
		status &= sc->imask; /* discard unasked for bits */
		if (unlikely(status & AR5K_INT_FATAL)) {
			/*
			 * Fatal errors are unrecoverable.
			 * Typically these are caused by DMA errors.
			 */
			tasklet_schedule(&sc->restq);
		} else if (unlikely(status & AR5K_INT_RXORN)) {
			tasklet_schedule(&sc->restq);
		} else {
			if (status & AR5K_INT_SWBA) {
				/*
				* Software beacon alert--time to send a beacon.
				* Handle beacon transmission directly; deferring
				* this is too slow to meet timing constraints
				* under load.
				*
				* In IBSS mode we use this interrupt just to
				* keep track of the next TBTT (target beacon
				* transmission time) in order to detect wether
				* automatic TSF updates happened.
				*/
				if (sc->opmode == IEEE80211_IF_TYPE_IBSS) {
					 /* XXX: only if VEOL suppported */
					u64 tsf = ath5k_hw_get_tsf64(ah);
					sc->nexttbtt += sc->bintval;
					ATH5K_DBG(sc, ATH5K_DEBUG_BEACON,
						  "SWBA nexttbtt: %x hw_tu: %x "
						  "TSF: %llx\n",
						  sc->nexttbtt,
						  TSF_TO_TU(tsf),
						  (unsigned long long) tsf);
				} else {
					spin_lock(&sc->block);
					ath5k_beacon_send(sc);
					spin_unlock(&sc->block);
				}
			}
			if (status & AR5K_INT_RXEOL) {
				/*
				* NB: the hardware should re-read the link when
				*     RXE bit is written, but it doesn't work at
				*     least on older hardware revs.
				*/
				sc->rxlink = NULL;
			}
			if (status & AR5K_INT_TXURN) {
				/* bump tx trigger level */
				ath5k_hw_update_tx_triglevel(ah, true);
			}
			if (status & AR5K_INT_RX)
				tasklet_schedule(&sc->rxtq);
			if (status & AR5K_INT_TX)
				tasklet_schedule(&sc->txtq);
			if (status & AR5K_INT_BMISS) {
			}
			if (status & AR5K_INT_MIB) {
				/*
				 * These stats are also used for ANI i think
				 * so how about updating them more often ?
				 */
				ath5k_hw_update_mib_counters(ah, &sc->ll_stats);
			}
		}
	} while (ath5k_hw_is_intr_pending(ah) && counter-- > 0);

	if (unlikely(!counter))
		ATH5K_WARN(sc, "too many interrupts, giving up for now\n");

	return IRQ_HANDLED;
}

static void
ath5k_tasklet_reset(unsigned long data)
{
	struct ath5k_softc *sc = (void *)data;

	ath5k_reset(sc->hw);
}

/*
 * Periodically recalibrate the PHY to account
 * for temperature/environment changes.
 */
static void
ath5k_calibrate(unsigned long data)
{
	struct ath5k_softc *sc = (void *)data;
	struct ath5k_hw *ah = sc->ah;

	ATH5K_DBG(sc, ATH5K_DEBUG_CALIBRATE, "channel %u/%x\n",
		ieee80211_frequency_to_channel(sc->curchan->center_freq),
		sc->curchan->hw_value);

	if (ath5k_hw_get_rf_gain(ah) == AR5K_RFGAIN_NEED_CHANGE) {
		/*
		 * Rfgain is out of bounds, reset the chip
		 * to load new gain values.
		 */
		ATH5K_DBG(sc, ATH5K_DEBUG_RESET, "calibration, resetting\n");
		ath5k_reset(sc->hw);
	}
	if (ath5k_hw_phy_calibrate(ah, sc->curchan))
		ATH5K_ERR(sc, "calibration of channel %u failed\n",
			ieee80211_frequency_to_channel(
				sc->curchan->center_freq));

	mod_timer(&sc->calib_tim, round_jiffies(jiffies +
			msecs_to_jiffies(ath5k_calinterval * 1000)));
}



/***************\
* LED functions *
\***************/

static void
ath5k_led_enable(struct ath5k_softc *sc)
{
	if (test_bit(ATH_STAT_LEDSOFT, sc->status)) {
		ath5k_hw_set_gpio_output(sc->ah, sc->led_pin);
		ath5k_led_off(sc);
	}
}

static void
ath5k_led_on(struct ath5k_softc *sc)
{
	if (!test_bit(ATH_STAT_LEDSOFT, sc->status))
		return;
	ath5k_hw_set_gpio(sc->ah, sc->led_pin, sc->led_on);
}

static void
ath5k_led_off(struct ath5k_softc *sc)
{
	if (!test_bit(ATH_STAT_LEDSOFT, sc->status))
		return;
	ath5k_hw_set_gpio(sc->ah, sc->led_pin, !sc->led_on);
}

static void
ath5k_led_brightness_set(struct led_classdev *led_dev,
	enum led_brightness brightness)
{
	struct ath5k_led *led = container_of(led_dev, struct ath5k_led,
		led_dev);

	if (brightness == LED_OFF)
		ath5k_led_off(led->sc);
	else
		ath5k_led_on(led->sc);
}

static int
ath5k_register_led(struct ath5k_softc *sc, struct ath5k_led *led,
		   const char *name, char *trigger)
{
	int err;

	led->sc = sc;
	strncpy(led->name, name, sizeof(led->name));
	led->led_dev.name = led->name;
	led->led_dev.default_trigger = trigger;
	led->led_dev.brightness_set = ath5k_led_brightness_set;

	err = led_classdev_register(&sc->pdev->dev, &led->led_dev);
	if (err)
	{
		ATH5K_WARN(sc, "could not register LED %s\n", name);
		led->sc = NULL;
	}
	return err;
}

static void
ath5k_unregister_led(struct ath5k_led *led)
{
	if (!led->sc)
		return;
	led_classdev_unregister(&led->led_dev);
	ath5k_led_off(led->sc);
	led->sc = NULL;
}

static void
ath5k_unregister_leds(struct ath5k_softc *sc)
{
	ath5k_unregister_led(&sc->rx_led);
	ath5k_unregister_led(&sc->tx_led);
}


static int
ath5k_init_leds(struct ath5k_softc *sc)
{
	int ret = 0;
	struct ieee80211_hw *hw = sc->hw;
	struct pci_dev *pdev = sc->pdev;
	char name[ATH5K_LED_MAX_NAME_LEN + 1];

	/*
	 * Auto-enable soft led processing for IBM cards and for
	 * 5211 minipci cards.
	 */
	if (pdev->device == PCI_DEVICE_ID_ATHEROS_AR5212_IBM ||
	    pdev->device == PCI_DEVICE_ID_ATHEROS_AR5211) {
		__set_bit(ATH_STAT_LEDSOFT, sc->status);
		sc->led_pin = 0;
		sc->led_on = 0;  /* active low */
	}
	/* Enable softled on PIN1 on HP Compaq nc6xx, nc4000 & nx5000 laptops */
	if (pdev->subsystem_vendor == PCI_VENDOR_ID_COMPAQ) {
		__set_bit(ATH_STAT_LEDSOFT, sc->status);
		sc->led_pin = 1;
		sc->led_on = 1;  /* active high */
	}
	if (!test_bit(ATH_STAT_LEDSOFT, sc->status))
		goto out;

	ath5k_led_enable(sc);

	snprintf(name, sizeof(name), "ath5k-%s::rx", wiphy_name(hw->wiphy));
	ret = ath5k_register_led(sc, &sc->rx_led, name,
		ieee80211_get_rx_led_name(hw));
	if (ret)
		goto out;

	snprintf(name, sizeof(name), "ath5k-%s::tx", wiphy_name(hw->wiphy));
	ret = ath5k_register_led(sc, &sc->tx_led, name,
		ieee80211_get_tx_led_name(hw));
out:
	return ret;
}


/********************\
* Mac80211 functions *
\********************/

static int
ath5k_tx(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ath5k_softc *sc = hw->priv;
	struct ath5k_buf *bf;
	unsigned long flags;
	int hdrlen;
	int pad;

	ath5k_debug_dump_skb(sc, skb, "TX  ", 1);

	if (sc->opmode == IEEE80211_IF_TYPE_MNTR)
		ATH5K_DBG(sc, ATH5K_DEBUG_XMIT, "tx in monitor (scan?)\n");

	/*
	 * the hardware expects the header padded to 4 byte boundaries
	 * if this is not the case we add the padding after the header
	 */
	hdrlen = ieee80211_get_hdrlen_from_skb(skb);
	if (hdrlen & 3) {
		pad = hdrlen % 4;
		if (skb_headroom(skb) < pad) {
			ATH5K_ERR(sc, "tx hdrlen not %%4: %d not enough"
				" headroom to pad %d\n", hdrlen, pad);
			return -1;
		}
		skb_push(skb, pad);
		memmove(skb->data, skb->data+pad, hdrlen);
	}

	spin_lock_irqsave(&sc->txbuflock, flags);
	if (list_empty(&sc->txbuf)) {
		ATH5K_ERR(sc, "no further txbuf available, dropping packet\n");
		spin_unlock_irqrestore(&sc->txbuflock, flags);
		ieee80211_stop_queue(hw, skb_get_queue_mapping(skb));
		return -1;
	}
	bf = list_first_entry(&sc->txbuf, struct ath5k_buf, list);
	list_del(&bf->list);
	sc->txbuf_len--;
	if (list_empty(&sc->txbuf))
		ieee80211_stop_queues(hw);
	spin_unlock_irqrestore(&sc->txbuflock, flags);

	bf->skb = skb;

	if (ath5k_txbuf_setup(sc, bf)) {
		bf->skb = NULL;
		spin_lock_irqsave(&sc->txbuflock, flags);
		list_add_tail(&bf->list, &sc->txbuf);
		sc->txbuf_len++;
		spin_unlock_irqrestore(&sc->txbuflock, flags);
		dev_kfree_skb_any(skb);
		return 0;
	}

	return 0;
}

static int
ath5k_reset(struct ieee80211_hw *hw)
{
	struct ath5k_softc *sc = hw->priv;
	struct ath5k_hw *ah = sc->ah;
	int ret;

	ATH5K_DBG(sc, ATH5K_DEBUG_RESET, "resetting\n");

	ath5k_hw_set_intr(ah, 0);
	ath5k_txq_cleanup(sc);
	ath5k_rx_stop(sc);

	ret = ath5k_hw_reset(ah, sc->opmode, sc->curchan, true);
	if (unlikely(ret)) {
		ATH5K_ERR(sc, "can't reset hardware (%d)\n", ret);
		goto err;
	}
	ath5k_hw_set_txpower_limit(sc->ah, 0);

	ret = ath5k_rx_start(sc);
	if (unlikely(ret)) {
		ATH5K_ERR(sc, "can't start recv logic\n");
		goto err;
	}
	/*
	 * We may be doing a reset in response to an ioctl
	 * that changes the channel so update any state that
	 * might change as a result.
	 *
	 * XXX needed?
	 */
/*	ath5k_chan_change(sc, c); */
	ath5k_beacon_config(sc);
	/* intrs are started by ath5k_beacon_config */

	ieee80211_wake_queues(hw);

	return 0;
err:
	return ret;
}

static int ath5k_start(struct ieee80211_hw *hw)
{
	return ath5k_init(hw->priv);
}

static void ath5k_stop(struct ieee80211_hw *hw)
{
	ath5k_stop_hw(hw->priv);
}

static int ath5k_add_interface(struct ieee80211_hw *hw,
		struct ieee80211_if_init_conf *conf)
{
	struct ath5k_softc *sc = hw->priv;
	int ret;

	mutex_lock(&sc->lock);
	if (sc->vif) {
		ret = 0;
		goto end;
	}

	sc->vif = conf->vif;

	switch (conf->type) {
	case IEEE80211_IF_TYPE_STA:
	case IEEE80211_IF_TYPE_IBSS:
	case IEEE80211_IF_TYPE_MNTR:
		sc->opmode = conf->type;
		break;
	default:
		ret = -EOPNOTSUPP;
		goto end;
	}
	ret = 0;
end:
	mutex_unlock(&sc->lock);
	return ret;
}

static void
ath5k_remove_interface(struct ieee80211_hw *hw,
			struct ieee80211_if_init_conf *conf)
{
	struct ath5k_softc *sc = hw->priv;

	mutex_lock(&sc->lock);
	if (sc->vif != conf->vif)
		goto end;

	sc->vif = NULL;
end:
	mutex_unlock(&sc->lock);
}

/*
 * TODO: Phy disable/diversity etc
 */
static int
ath5k_config(struct ieee80211_hw *hw,
			struct ieee80211_conf *conf)
{
	struct ath5k_softc *sc = hw->priv;

	sc->bintval = conf->beacon_int;
	sc->power_level = conf->power_level;

	return ath5k_chan_set(sc, conf->channel);
}

static int
ath5k_config_interface(struct ieee80211_hw *hw, struct ieee80211_vif *vif,
			struct ieee80211_if_conf *conf)
{
	struct ath5k_softc *sc = hw->priv;
	struct ath5k_hw *ah = sc->ah;
	int ret;

	/* Set to a reasonable value. Note that this will
	 * be set to mac80211's value at ath5k_config(). */
	sc->bintval = 1000;
	mutex_lock(&sc->lock);
	if (sc->vif != vif) {
		ret = -EIO;
		goto unlock;
	}
	if (conf->bssid) {
		/* Cache for later use during resets */
		memcpy(ah->ah_bssid, conf->bssid, ETH_ALEN);
		/* XXX: assoc id is set to 0 for now, mac80211 doesn't have
		 * a clean way of letting us retrieve this yet. */
		ath5k_hw_set_associd(ah, ah->ah_bssid, 0);
		mmiowb();
	}

	if (conf->changed & IEEE80211_IFCC_BEACON &&
	    vif->type == IEEE80211_IF_TYPE_IBSS) {
		struct sk_buff *beacon = ieee80211_beacon_get(hw, vif);
		if (!beacon) {
			ret = -ENOMEM;
			goto unlock;
		}
		/* call old handler for now */
		ath5k_beacon_update(hw, beacon);
	}

	mutex_unlock(&sc->lock);

	return ath5k_reset(hw);
unlock:
	mutex_unlock(&sc->lock);
	return ret;
}

#define SUPPORTED_FIF_FLAGS \
	FIF_PROMISC_IN_BSS |  FIF_ALLMULTI | FIF_FCSFAIL | \
	FIF_PLCPFAIL | FIF_CONTROL | FIF_OTHER_BSS | \
	FIF_BCN_PRBRESP_PROMISC
/*
 * o always accept unicast, broadcast, and multicast traffic
 * o multicast traffic for all BSSIDs will be enabled if mac80211
 *   says it should be
 * o maintain current state of phy ofdm or phy cck error reception.
 *   If the hardware detects any of these type of errors then
 *   ath5k_hw_get_rx_filter() will pass to us the respective
 *   hardware filters to be able to receive these type of frames.
 * o probe request frames are accepted only when operating in
 *   hostap, adhoc, or monitor modes
 * o enable promiscuous mode according to the interface state
 * o accept beacons:
 *   - when operating in adhoc mode so the 802.11 layer creates
 *     node table entries for peers,
 *   - when operating in station mode for collecting rssi data when
 *     the station is otherwise quiet, or
 *   - when scanning
 */
static void ath5k_configure_filter(struct ieee80211_hw *hw,
		unsigned int changed_flags,
		unsigned int *new_flags,
		int mc_count, struct dev_mc_list *mclist)
{
	struct ath5k_softc *sc = hw->priv;
	struct ath5k_hw *ah = sc->ah;
	u32 mfilt[2], val, rfilt;
	u8 pos;
	int i;

	mfilt[0] = 0;
	mfilt[1] = 0;

	/* Only deal with supported flags */
	changed_flags &= SUPPORTED_FIF_FLAGS;
	*new_flags &= SUPPORTED_FIF_FLAGS;

	/* If HW detects any phy or radar errors, leave those filters on.
	 * Also, always enable Unicast, Broadcasts and Multicast
	 * XXX: move unicast, bssid broadcasts and multicast to mac80211 */
	rfilt = (ath5k_hw_get_rx_filter(ah) & (AR5K_RX_FILTER_PHYERR)) |
		(AR5K_RX_FILTER_UCAST | AR5K_RX_FILTER_BCAST |
		AR5K_RX_FILTER_MCAST);

	if (changed_flags & (FIF_PROMISC_IN_BSS | FIF_OTHER_BSS)) {
		if (*new_flags & FIF_PROMISC_IN_BSS) {
			rfilt |= AR5K_RX_FILTER_PROM;
			__set_bit(ATH_STAT_PROMISC, sc->status);
		}
		else
			__clear_bit(ATH_STAT_PROMISC, sc->status);
	}

	/* Note, AR5K_RX_FILTER_MCAST is already enabled */
	if (*new_flags & FIF_ALLMULTI) {
		mfilt[0] =  ~0;
		mfilt[1] =  ~0;
	} else {
		for (i = 0; i < mc_count; i++) {
			if (!mclist)
				break;
			/* calculate XOR of eight 6-bit values */
			val = get_unaligned_le32(mclist->dmi_addr + 0);
			pos = (val >> 18) ^ (val >> 12) ^ (val >> 6) ^ val;
			val = get_unaligned_le32(mclist->dmi_addr + 3);
			pos ^= (val >> 18) ^ (val >> 12) ^ (val >> 6) ^ val;
			pos &= 0x3f;
			mfilt[pos / 32] |= (1 << (pos % 32));
			/* XXX: we might be able to just do this instead,
			* but not sure, needs testing, if we do use this we'd
			* neet to inform below to not reset the mcast */
			/* ath5k_hw_set_mcast_filterindex(ah,
			 *      mclist->dmi_addr[5]); */
			mclist = mclist->next;
		}
	}

	/* This is the best we can do */
	if (*new_flags & (FIF_FCSFAIL | FIF_PLCPFAIL))
		rfilt |= AR5K_RX_FILTER_PHYERR;

	/* FIF_BCN_PRBRESP_PROMISC really means to enable beacons
	* and probes for any BSSID, this needs testing */
	if (*new_flags & FIF_BCN_PRBRESP_PROMISC)
		rfilt |= AR5K_RX_FILTER_BEACON | AR5K_RX_FILTER_PROBEREQ;

	/* FIF_CONTROL doc says that if FIF_PROMISC_IN_BSS is not
	 * set we should only pass on control frames for this
	 * station. This needs testing. I believe right now this
	 * enables *all* control frames, which is OK.. but
	 * but we should see if we can improve on granularity */
	if (*new_flags & FIF_CONTROL)
		rfilt |= AR5K_RX_FILTER_CONTROL;

	/* Additional settings per mode -- this is per ath5k */

	/* XXX move these to mac80211, and add a beacon IFF flag to mac80211 */

	if (sc->opmode == IEEE80211_IF_TYPE_MNTR)
		rfilt |= AR5K_RX_FILTER_CONTROL | AR5K_RX_FILTER_BEACON |
			AR5K_RX_FILTER_PROBEREQ | AR5K_RX_FILTER_PROM;
	if (sc->opmode != IEEE80211_IF_TYPE_STA)
		rfilt |= AR5K_RX_FILTER_PROBEREQ;
	if (sc->opmode != IEEE80211_IF_TYPE_AP &&
		test_bit(ATH_STAT_PROMISC, sc->status))
		rfilt |= AR5K_RX_FILTER_PROM;
	if (sc->opmode == IEEE80211_IF_TYPE_STA ||
		sc->opmode == IEEE80211_IF_TYPE_IBSS) {
		rfilt |= AR5K_RX_FILTER_BEACON;
	}

	/* Set filters */
	ath5k_hw_set_rx_filter(ah,rfilt);

	/* Set multicast bits */
	ath5k_hw_set_mcast_filter(ah, mfilt[0], mfilt[1]);
	/* Set the cached hw filter flags, this will alter actually
	 * be set in HW */
	sc->filter_flags = rfilt;
}

static int
ath5k_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		const u8 *local_addr, const u8 *addr,
		struct ieee80211_key_conf *key)
{
	struct ath5k_softc *sc = hw->priv;
	int ret = 0;

	switch(key->alg) {
	case ALG_WEP:
	/* XXX: fix hardware encryption, its not working. For now
	 * allow software encryption */
		/* break; */
	case ALG_TKIP:
	case ALG_CCMP:
		return -EOPNOTSUPP;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	mutex_lock(&sc->lock);

	switch (cmd) {
	case SET_KEY:
		ret = ath5k_hw_set_key(sc->ah, key->keyidx, key, addr);
		if (ret) {
			ATH5K_ERR(sc, "can't set the key\n");
			goto unlock;
		}
		__set_bit(key->keyidx, sc->keymap);
		key->hw_key_idx = key->keyidx;
		break;
	case DISABLE_KEY:
		ath5k_hw_reset_key(sc->ah, key->keyidx);
		__clear_bit(key->keyidx, sc->keymap);
		break;
	default:
		ret = -EINVAL;
		goto unlock;
	}

unlock:
	mmiowb();
	mutex_unlock(&sc->lock);
	return ret;
}

static int
ath5k_get_stats(struct ieee80211_hw *hw,
		struct ieee80211_low_level_stats *stats)
{
	struct ath5k_softc *sc = hw->priv;
	struct ath5k_hw *ah = sc->ah;

	/* Force update */
	ath5k_hw_update_mib_counters(ah, &sc->ll_stats);

	memcpy(stats, &sc->ll_stats, sizeof(sc->ll_stats));

	return 0;
}

static int
ath5k_get_tx_stats(struct ieee80211_hw *hw,
		struct ieee80211_tx_queue_stats *stats)
{
	struct ath5k_softc *sc = hw->priv;

	memcpy(stats, &sc->tx_stats, sizeof(sc->tx_stats));

	return 0;
}

static u64
ath5k_get_tsf(struct ieee80211_hw *hw)
{
	struct ath5k_softc *sc = hw->priv;

	return ath5k_hw_get_tsf64(sc->ah);
}

static void
ath5k_reset_tsf(struct ieee80211_hw *hw)
{
	struct ath5k_softc *sc = hw->priv;

	/*
	 * in IBSS mode we need to update the beacon timers too.
	 * this will also reset the TSF if we call it with 0
	 */
	if (sc->opmode == IEEE80211_IF_TYPE_IBSS)
		ath5k_beacon_update_timers(sc, 0);
	else
		ath5k_hw_reset_tsf(sc->ah);
}

static int
ath5k_beacon_update(struct ieee80211_hw *hw, struct sk_buff *skb)
{
	struct ath5k_softc *sc = hw->priv;
	unsigned long flags;
	int ret;

	ath5k_debug_dump_skb(sc, skb, "BC  ", 1);

	if (sc->opmode != IEEE80211_IF_TYPE_IBSS) {
		ret = -EIO;
		goto end;
	}

	spin_lock_irqsave(&sc->block, flags);
	ath5k_txbuf_free(sc, sc->bbuf);
	sc->bbuf->skb = skb;
	ret = ath5k_beacon_setup(sc, sc->bbuf);
	if (ret)
		sc->bbuf->skb = NULL;
	spin_unlock_irqrestore(&sc->block, flags);
	if (!ret) {
		ath5k_beacon_config(sc);
		mmiowb();
	}

end:
	return ret;
}

