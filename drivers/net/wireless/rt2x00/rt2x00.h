/*
	Copyright (C) 2004 - 2009 rt2x00 SourceForge Project
	<http://rt2x00.serialmonkey.com>

	This program is free software; you can redistribute it and/or modify
	it under the terms of the GNU General Public License as published by
	the Free Software Foundation; either version 2 of the License, or
	(at your option) any later version.

	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
	GNU General Public License for more details.

	You should have received a copy of the GNU General Public License
	along with this program; if not, write to the
	Free Software Foundation, Inc.,
	59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

/*
	Module: rt2x00
	Abstract: rt2x00 global information.
 */

#ifndef RT2X00_H
#define RT2X00_H

#include <linux/bitops.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/firmware.h>
#include <linux/leds.h>
#include <linux/mutex.h>
#include <linux/etherdevice.h>
#include <linux/input-polldev.h>

#include <net/mac80211.h>

#include "rt2x00debug.h"
#include "rt2x00leds.h"
#include "rt2x00reg.h"
#include "rt2x00queue.h"

/*
 * Module information.
 */
#define DRV_VERSION	"2.3.0"
#define DRV_PROJECT	"http://rt2x00.serialmonkey.com"

/*
 * Debug definitions.
 * Debug output has to be enabled during compile time.
 */
#define DEBUG_PRINTK_MSG(__dev, __kernlvl, __lvl, __msg, __args...)	\
	printk(__kernlvl "%s -> %s: %s - " __msg,			\
	       wiphy_name((__dev)->hw->wiphy), __func__, __lvl, ##__args)

#define DEBUG_PRINTK_PROBE(__kernlvl, __lvl, __msg, __args...)	\
	printk(__kernlvl "%s -> %s: %s - " __msg,		\
	       KBUILD_MODNAME, __func__, __lvl, ##__args)

#ifdef CONFIG_RT2X00_DEBUG
#define DEBUG_PRINTK(__dev, __kernlvl, __lvl, __msg, __args...)	\
	DEBUG_PRINTK_MSG(__dev, __kernlvl, __lvl, __msg, ##__args);
#else
#define DEBUG_PRINTK(__dev, __kernlvl, __lvl, __msg, __args...)	\
	do { } while (0)
#endif /* CONFIG_RT2X00_DEBUG */

/*
 * Various debug levels.
 * The debug levels PANIC and ERROR both indicate serious problems,
 * for this reason they should never be ignored.
 * The special ERROR_PROBE message is for messages that are generated
 * when the rt2x00_dev is not yet initialized.
 */
#define PANIC(__dev, __msg, __args...) \
	DEBUG_PRINTK_MSG(__dev, KERN_CRIT, "Panic", __msg, ##__args)
#define ERROR(__dev, __msg, __args...)	\
	DEBUG_PRINTK_MSG(__dev, KERN_ERR, "Error", __msg, ##__args)
#define ERROR_PROBE(__msg, __args...) \
	DEBUG_PRINTK_PROBE(KERN_ERR, "Error", __msg, ##__args)
#define WARNING(__dev, __msg, __args...) \
	DEBUG_PRINTK(__dev, KERN_WARNING, "Warning", __msg, ##__args)
#define NOTICE(__dev, __msg, __args...) \
	DEBUG_PRINTK(__dev, KERN_NOTICE, "Notice", __msg, ##__args)
#define INFO(__dev, __msg, __args...) \
	DEBUG_PRINTK(__dev, KERN_INFO, "Info", __msg, ##__args)
#define DEBUG(__dev, __msg, __args...) \
	DEBUG_PRINTK(__dev, KERN_DEBUG, "Debug", __msg, ##__args)
#define EEPROM(__dev, __msg, __args...) \
	DEBUG_PRINTK(__dev, KERN_DEBUG, "EEPROM recovery", __msg, ##__args)

/*
 * Duration calculations
 * The rate variable passed is: 100kbs.
 * To convert from bytes to bits we multiply size with 8,
 * then the size is multiplied with 10 to make the
 * real rate -> rate argument correction.
 */
#define GET_DURATION(__size, __rate)	(((__size) * 8 * 10) / (__rate))
#define GET_DURATION_RES(__size, __rate)(((__size) * 8 * 10) % (__rate))

/*
 * Determine the alignment requirement,
 * to make sure the 802.11 payload is padded to a 4-byte boundrary
 * we must determine the address of the payload and calculate the
 * amount of bytes needed to move the data.
 */
#define ALIGN_SIZE(__skb, __header) \
	(  ((unsigned long)((__skb)->data + (__header))) & 3 )

/*
 * Standard timing and size defines.
 * These values should follow the ieee80211 specifications.
 */
#define ACK_SIZE		14
#define IEEE80211_HEADER	24
#define PLCP			48
#define BEACON			100
#define PREAMBLE		144
#define SHORT_PREAMBLE		72
#define SLOT_TIME		20
#define SHORT_SLOT_TIME		9
#define SIFS			10
#define PIFS			( SIFS + SLOT_TIME )
#define SHORT_PIFS		( SIFS + SHORT_SLOT_TIME )
#define DIFS			( PIFS + SLOT_TIME )
#define SHORT_DIFS		( SHORT_PIFS + SHORT_SLOT_TIME )
#define EIFS			( SIFS + DIFS + \
				  GET_DURATION(IEEE80211_HEADER + ACK_SIZE, 10) )
#define SHORT_EIFS		( SIFS + SHORT_DIFS + \
				  GET_DURATION(IEEE80211_HEADER + ACK_SIZE, 10) )

/*
 * Chipset identification
 * The chipset on the device is composed of a RT and RF chip.
 * The chipset combination is important for determining device capabilities.
 */
struct rt2x00_chip {
	u16 rt;
#define RT2460		0x0101
#define RT2560		0x0201
#define RT2570		0x1201
#define RT2561s		0x0301	/* Turbo */
#define RT2561		0x0302
#define RT2661		0x0401
#define RT2571		0x1300
#define RT2870		0x1600

	u16 rf;
	u32 rev;
};

/*
 * RF register values that belong to a particular channel.
 */
struct rf_channel {
	int channel;
	u32 rf1;
	u32 rf2;
	u32 rf3;
	u32 rf4;
};

/*
 * Channel information structure
 */
struct channel_info {
	unsigned int flags;
#define GEOGRAPHY_ALLOWED	0x00000001

	short tx_power1;
	short tx_power2;
};

/*
 * Antenna setup values.
 */
struct antenna_setup {
	enum antenna rx;
	enum antenna tx;
};

/*
 * Quality statistics about the currently active link.
 */
struct link_qual {
	/*
	 * Statistics required for Link tuning by driver
	 * The rssi value is provided by rt2x00lib during the
	 * link_tuner() callback function.
	 * The false_cca field is filled during the link_stats()
	 * callback function and could be used during the
	 * link_tuner() callback function.
	 */
	int rssi;
	int false_cca;

	/*
	 * VGC levels
	 * Hardware driver will tune the VGC level during each call
	 * to the link_tuner() callback function. This vgc_level is
	 * is determined based on the link quality statistics like
	 * average RSSI and the false CCA count.
	 *
	 * In some cases the drivers need to differentiate between
	 * the currently "desired" VGC level and the level configured
	 * in the hardware. The latter is important to reduce the
	 * number of BBP register reads to reduce register access
	 * overhead. For this reason we store both values here.
	 */
	u8 vgc_level;
	u8 vgc_level_reg;

	/*
	 * Statistics required for Signal quality calculation.
	 * These fields might be changed during the link_stats()
	 * callback function.
	 */
	int rx_success;
	int rx_failed;
	int tx_success;
	int tx_failed;
};

/*
 * Antenna settings about the currently active link.
 */
struct link_ant {
	/*
	 * Antenna flags
	 */
	unsigned int flags;
#define ANTENNA_RX_DIVERSITY	0x00000001
#define ANTENNA_TX_DIVERSITY	0x00000002
#define ANTENNA_MODE_SAMPLE	0x00000004

	/*
	 * Currently active TX/RX antenna setup.
	 * When software diversity is used, this will indicate
	 * which antenna is actually used at this time.
	 */
	struct antenna_setup active;

	/*
	 * RSSI information for the different antenna's.
	 * These statistics are used to determine when
	 * to switch antenna when using software diversity.
	 *
	 *        rssi[0] -> Antenna A RSSI
	 *        rssi[1] -> Antenna B RSSI
	 */
	int rssi_history[2];

	/*
	 * Current RSSI average of the currently active antenna.
	 * Similar to the avg_rssi in the link_qual structure
	 * this value is updated by using the walking average.
	 */
	int rssi_ant;
};

/*
 * To optimize the quality of the link we need to store
 * the quality of received frames and periodically
 * optimize the link.
 */
struct link {
	/*
	 * Link tuner counter
	 * The number of times the link has been tuned
	 * since the radio has been switched on.
	 */
	u32 count;

	/*
	 * Quality measurement values.
	 */
	struct link_qual qual;

	/*
	 * TX/RX antenna setup.
	 */
	struct link_ant ant;

	/*
	 * Currently active average RSSI value
	 */
	int avg_rssi;

	/*
	 * Currently precalculated percentages of successful
	 * TX and RX frames.
	 */
	int rx_percentage;
	int tx_percentage;

	/*
	 * Work structure for scheduling periodic link tuning.
	 */
	struct delayed_work work;
};

/*
 * Interface structure
 * Per interface configuration details, this structure
 * is allocated as the private data for ieee80211_vif.
 */
struct rt2x00_intf {
	/*
	 * All fields within the rt2x00_intf structure
	 * must be protected with a spinlock.
	 */
	spinlock_t lock;

	/*
	 * MAC of the device.
	 */
	u8 mac[ETH_ALEN];

	/*
	 * BBSID of the AP to associate with.
	 */
	u8 bssid[ETH_ALEN];

	/*
	 * Entry in the beacon queue which belongs to
	 * this interface. Each interface has its own
	 * dedicated beacon entry.
	 */
	struct queue_entry *beacon;

	/*
	 * Actions that needed rescheduling.
	 */
	unsigned int delayed_flags;
#define DELAYED_UPDATE_BEACON		0x00000001
#define DELAYED_CONFIG_ERP		0x00000002
#define DELAYED_LED_ASSOC		0x00000004

	/*
	 * Software sequence counter, this is only required
	 * for hardware which doesn't support hardware
	 * sequence counting.
	 */
	spinlock_t seqlock;
	u16 seqno;
};

static inline struct rt2x00_intf* vif_to_intf(struct ieee80211_vif *vif)
{
	return (struct rt2x00_intf *)vif->drv_priv;
}

/**
 * struct hw_mode_spec: Hardware specifications structure
 *
 * Details about the supported modes, rates and channels
 * of a particular chipset. This is used by rt2x00lib
 * to build the ieee80211_hw_mode array for mac80211.
 *
 * @supported_bands: Bitmask contained the supported bands (2.4GHz, 5.2GHz).
 * @supported_rates: Rate types which are supported (CCK, OFDM).
 * @num_channels: Number of supported channels. This is used as array size
 *	for @tx_power_a, @tx_power_bg and @channels.
 * @channels: Device/chipset specific channel values (See &struct rf_channel).
 * @channels_info: Additional information for channels (See &struct channel_info).
 * @ht: Driver HT Capabilities (See &ieee80211_sta_ht_cap).
 */
struct hw_mode_spec {
	unsigned int supported_bands;
#define SUPPORT_BAND_2GHZ	0x00000001
#define SUPPORT_BAND_5GHZ	0x00000002

	unsigned int supported_rates;
#define SUPPORT_RATE_CCK	0x00000001
#define SUPPORT_RATE_OFDM	0x00000002

	unsigned int num_channels;
	const struct rf_channel *channels;
	const struct channel_info *channels_info;

	struct ieee80211_sta_ht_cap ht;
};

/*
 * Configuration structure wrapper around the
 * mac80211 configuration structure.
 * When mac80211 configures the driver, rt2x00lib
 * can precalculate values which are equal for all
 * rt2x00 drivers. Those values can be stored in here.
 */
struct rt2x00lib_conf {
	struct ieee80211_conf *conf;

	struct rf_channel rf;
	struct channel_info channel;
};

/*
 * Configuration structure for erp settings.
 */
struct rt2x00lib_erp {
	int short_preamble;
	int cts_protection;

	int ack_timeout;
	int ack_consume_time;

	u32 basic_rates;

	int slot_time;

	short sifs;
	short pifs;
	short difs;
	short eifs;

	u16 beacon_int;
};

/*
 * Configuration structure for hardware encryption.
 */
struct rt2x00lib_crypto {
	enum cipher cipher;

	enum set_key_cmd cmd;
	const u8 *address;

	u32 bssidx;
	u32 aid;

	u8 key[16];
	u8 tx_mic[8];
	u8 rx_mic[8];
};

/*
 * Configuration structure wrapper around the
 * rt2x00 interface configuration handler.
 */
struct rt2x00intf_conf {
	/*
	 * Interface type
	 */
	enum nl80211_iftype type;

	/*
	 * TSF sync value, this is dependant on the operation type.
	 */
	enum tsf_sync sync;

	/*
	 * The MAC and BSSID addressess are simple array of bytes,
	 * these arrays are little endian, so when sending the addressess
	 * to the drivers, copy the it into a endian-signed variable.
	 *
	 * Note that all devices (except rt2500usb) have 32 bits
	 * register word sizes. This means that whatever variable we
	 * pass _must_ be a multiple of 32 bits. Otherwise the device
	 * might not accept what we are sending to it.
	 * This will also make it easier for the driver to write
	 * the data to the device.
	 */
	__le32 mac[2];
	__le32 bssid[2];
};

/*
 * rt2x00lib callback functions.
 */
struct rt2x00lib_ops {
	/*
	 * Interrupt handlers.
	 */
	irq_handler_t irq_handler;

	/*
	 * Device init handlers.
	 */
	int (*probe_hw) (struct rt2x00_dev *rt2x00dev);
	char *(*get_firmware_name) (struct rt2x00_dev *rt2x00dev);
	int (*check_firmware) (struct rt2x00_dev *rt2x00dev,
			       const u8 *data, const size_t len);
	int (*load_firmware) (struct rt2x00_dev *rt2x00dev,
			      const u8 *data, const size_t len);

	/*
	 * Device initialization/deinitialization handlers.
	 */
	int (*initialize) (struct rt2x00_dev *rt2x00dev);
	void (*uninitialize) (struct rt2x00_dev *rt2x00dev);

	/*
	 * queue initialization handlers
	 */
	bool (*get_entry_state) (struct queue_entry *entry);
	void (*clear_entry) (struct queue_entry *entry);

	/*
	 * Radio control handlers.
	 */
	int (*set_device_state) (struct rt2x00_dev *rt2x00dev,
				 enum dev_state state);
	int (*rfkill_poll) (struct rt2x00_dev *rt2x00dev);
	void (*link_stats) (struct rt2x00_dev *rt2x00dev,
			    struct link_qual *qual);
	void (*reset_tuner) (struct rt2x00_dev *rt2x00dev,
			     struct link_qual *qual);
	void (*link_tuner) (struct rt2x00_dev *rt2x00dev,
			    struct link_qual *qual, const u32 count);

	/*
	 * TX control handlers
	 */
	void (*write_tx_desc) (struct rt2x00_dev *rt2x00dev,
			       struct sk_buff *skb,
			       struct txentry_desc *txdesc);
	int (*write_tx_data) (struct queue_entry *entry);
	void (*write_beacon) (struct queue_entry *entry);
	int (*get_tx_data_len) (struct queue_entry *entry);
	void (*kick_tx_queue) (struct rt2x00_dev *rt2x00dev,
			       const enum data_queue_qid queue);
	void (*kill_tx_queue) (struct rt2x00_dev *rt2x00dev,
			       const enum data_queue_qid queue);

	/*
	 * RX control handlers
	 */
	void (*fill_rxdone) (struct queue_entry *entry,
			     struct rxdone_entry_desc *rxdesc);

	/*
	 * Configuration handlers.
	 */
	int (*config_shared_key) (struct rt2x00_dev *rt2x00dev,
				  struct rt2x00lib_crypto *crypto,
				  struct ieee80211_key_conf *key);
	int (*config_pairwise_key) (struct rt2x00_dev *rt2x00dev,
				    struct rt2x00lib_crypto *crypto,
				    struct ieee80211_key_conf *key);
	void (*config_filter) (struct rt2x00_dev *rt2x00dev,
			       const unsigned int filter_flags);
	void (*config_intf) (struct rt2x00_dev *rt2x00dev,
			     struct rt2x00_intf *intf,
			     struct rt2x00intf_conf *conf,
			     const unsigned int flags);
#define CONFIG_UPDATE_TYPE		( 1 << 1 )
#define CONFIG_UPDATE_MAC		( 1 << 2 )
#define CONFIG_UPDATE_BSSID		( 1 << 3 )

	void (*config_erp) (struct rt2x00_dev *rt2x00dev,
			    struct rt2x00lib_erp *erp);
	void (*config_ant) (struct rt2x00_dev *rt2x00dev,
			    struct antenna_setup *ant);
	void (*config) (struct rt2x00_dev *rt2x00dev,
			struct rt2x00lib_conf *libconf,
			const unsigned int changed_flags);
};

/*
 * rt2x00 driver callback operation structure.
 */
struct rt2x00_ops {
	const char *name;
	const unsigned int max_sta_intf;
	const unsigned int max_ap_intf;
	const unsigned int eeprom_size;
	const unsigned int rf_size;
	const unsigned int tx_queues;
	const struct data_queue_desc *rx;
	const struct data_queue_desc *tx;
	const struct data_queue_desc *bcn;
	const struct data_queue_desc *atim;
	const struct rt2x00lib_ops *lib;
	const struct ieee80211_ops *hw;
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
	const struct rt2x00debug *debugfs;
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */
};

/*
 * rt2x00 device flags
 */
enum rt2x00_flags {
	/*
	 * Device state flags
	 */
	DEVICE_STATE_PRESENT,
	DEVICE_STATE_REGISTERED_HW,
	DEVICE_STATE_INITIALIZED,
	DEVICE_STATE_STARTED,
	DEVICE_STATE_ENABLED_RADIO,
	DEVICE_STATE_DISABLED_RADIO_HW,

	/*
	 * Driver requirements
	 */
	DRIVER_REQUIRE_FIRMWARE,
	DRIVER_REQUIRE_BEACON_GUARD,
	DRIVER_REQUIRE_ATIM_QUEUE,
	DRIVER_REQUIRE_SCHEDULED,
	DRIVER_REQUIRE_DMA,
	DRIVER_REQUIRE_COPY_IV,
	DRIVER_REQUIRE_L2PAD,

	/*
	 * Driver features
	 */
	CONFIG_SUPPORT_HW_BUTTON,
	CONFIG_SUPPORT_HW_CRYPTO,

	/*
	 * Driver configuration
	 */
	CONFIG_FRAME_TYPE,
	CONFIG_RF_SEQUENCE,
	CONFIG_EXTERNAL_LNA_A,
	CONFIG_EXTERNAL_LNA_BG,
	CONFIG_DOUBLE_ANTENNA,
	CONFIG_DISABLE_LINK_TUNING,
	CONFIG_CHANNEL_HT40,
};

/*
 * rt2x00 device structure.
 */
struct rt2x00_dev {
	/*
	 * Device structure.
	 * The structure stored in here depends on the
	 * system bus (PCI or USB).
	 * When accessing this variable, the rt2x00dev_{pci,usb}
	 * macro's should be used for correct typecasting.
	 */
	struct device *dev;

	/*
	 * Callback functions.
	 */
	const struct rt2x00_ops *ops;

	/*
	 * IEEE80211 control structure.
	 */
	struct ieee80211_hw *hw;
	struct ieee80211_supported_band bands[IEEE80211_NUM_BANDS];
	enum ieee80211_band curr_band;

	/*
	 * rfkill structure for RF state switching support.
	 * This will only be compiled in when required.
	 */
#ifdef CONFIG_RT2X00_LIB_RFKILL
	unsigned long rfkill_state;
#define RFKILL_STATE_ALLOCATED		1
#define RFKILL_STATE_REGISTERED		2
#define RFKILL_STATE_BLOCKED		3
	struct input_polled_dev *rfkill_poll_dev;
#endif /* CONFIG_RT2X00_LIB_RFKILL */

	/*
	 * If enabled, the debugfs interface structures
	 * required for deregistration of debugfs.
	 */
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
	struct rt2x00debug_intf *debugfs_intf;
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */

	/*
	 * LED structure for changing the LED status
	 * by mac8011 or the kernel.
	 */
#ifdef CONFIG_RT2X00_LIB_LEDS
	struct rt2x00_led led_radio;
	struct rt2x00_led led_assoc;
	struct rt2x00_led led_qual;
	u16 led_mcu_reg;
#endif /* CONFIG_RT2X00_LIB_LEDS */

	/*
	 * Device flags.
	 * In these flags the current status and some
	 * of the device capabilities are stored.
	 */
	unsigned long flags;

	/*
	 * Device information, Bus IRQ and name (PCI, SoC)
	 */
	int irq;
	const char *name;

	/*
	 * Chipset identification.
	 */
	struct rt2x00_chip chip;

	/*
	 * hw capability specifications.
	 */
	struct hw_mode_spec spec;

	/*
	 * This is the default TX/RX antenna setup as indicated
	 * by the device's EEPROM.
	 */
	struct antenna_setup default_ant;

	/*
	 * Register pointers
	 * csr.base: CSR base register address. (PCI)
	 * csr.cache: CSR cache for usb_control_msg. (USB)
	 */
	union csr {
		void __iomem *base;
		void *cache;
	} csr;

	/*
	 * Mutex to protect register accesses.
	 * For PCI and USB devices it protects against concurrent indirect
	 * register access (BBP, RF, MCU) since accessing those
	 * registers require multiple calls to the CSR registers.
	 * For USB devices it also protects the csr_cache since that
	 * field is used for normal CSR access and it cannot support
	 * multiple callers simultaneously.
	 */
	struct mutex csr_mutex;

	/*
	 * Current packet filter configuration for the device.
	 * This contains all currently active FIF_* flags send
	 * to us by mac80211 during configure_filter().
	 */
	unsigned int packet_filter;

	/*
	 * Interface details:
	 *  - Open ap interface count.
	 *  - Open sta interface count.
	 *  - Association count.
	 */
	unsigned int intf_ap_count;
	unsigned int intf_sta_count;
	unsigned int intf_associated;

	/*
	 * Link quality
	 */
	struct link link;

	/*
	 * EEPROM data.
	 */
	__le16 *eeprom;

	/*
	 * Active RF register values.
	 * These are stored here so we don't need
	 * to read the rf registers and can directly
	 * use this value instead.
	 * This field should be accessed by using
	 * rt2x00_rf_read() and rt2x00_rf_write().
	 */
	u32 *rf;

	/*
	 * LNA gain
	 */
	short lna_gain;

	/*
	 * Current TX power value.
	 */
	u16 tx_power;

	/*
	 * Current retry values.
	 */
	u8 short_retry;
	u8 long_retry;

	/*
	 * Rssi <-> Dbm offset
	 */
	u8 rssi_offset;

	/*
	 * Frequency offset (for rt61pci & rt73usb).
	 */
	u8 freq_offset;

	/*
	 * Calibration information (for rt2800usb & rt2800pci).
	 * [0] -> BW20
	 * [1] -> BW40
	 */
	u8 calibration[2];

	/*
	 * Beacon interval.
	 */
	u16 beacon_int;

	/*
	 * Low level statistics which will have
	 * to be kept up to date while device is running.
	 */
	struct ieee80211_low_level_stats low_level_stats;

	/*
	 * RX configuration information.
	 */
	struct ieee80211_rx_status rx_status;

	/*
	 * Scheduled work.
	 * NOTE: intf_work will use ieee80211_iterate_active_interfaces()
	 * which means it cannot be placed on the hw->workqueue
	 * due to RTNL locking requirements.
	 */
	struct work_struct intf_work;
	struct work_struct filter_work;

	/*
	 * Data queue arrays for RX, TX and Beacon.
	 * The Beacon array also contains the Atim queue
	 * if that is supported by the device.
	 */
	unsigned int data_queues;
	struct data_queue *rx;
	struct data_queue *tx;
	struct data_queue *bcn;

	/*
	 * Firmware image.
	 */
	const struct firmware *fw;
};

/*
 * Generic RF access.
 * The RF is being accessed by word index.
 */
static inline void rt2x00_rf_read(struct rt2x00_dev *rt2x00dev,
				  const unsigned int word, u32 *data)
{
	*data = rt2x00dev->rf[word];
}

static inline void rt2x00_rf_write(struct rt2x00_dev *rt2x00dev,
				   const unsigned int word, u32 data)
{
	rt2x00dev->rf[word] = data;
}

/*
 *  Generic EEPROM access.
 * The EEPROM is being accessed by word index.
 */
static inline void *rt2x00_eeprom_addr(struct rt2x00_dev *rt2x00dev,
				       const unsigned int word)
{
	return (void *)&rt2x00dev->eeprom[word];
}

static inline void rt2x00_eeprom_read(struct rt2x00_dev *rt2x00dev,
				      const unsigned int word, u16 *data)
{
	*data = le16_to_cpu(rt2x00dev->eeprom[word]);
}

static inline void rt2x00_eeprom_write(struct rt2x00_dev *rt2x00dev,
				       const unsigned int word, u16 data)
{
	rt2x00dev->eeprom[word] = cpu_to_le16(data);
}

/*
 * Chipset handlers
 */
static inline void rt2x00_set_chip(struct rt2x00_dev *rt2x00dev,
				   const u16 rt, const u16 rf, const u32 rev)
{
	INFO(rt2x00dev,
	     "Chipset detected - rt: %04x, rf: %04x, rev: %08x.\n",
	     rt, rf, rev);

	rt2x00dev->chip.rt = rt;
	rt2x00dev->chip.rf = rf;
	rt2x00dev->chip.rev = rev;
}

static inline void rt2x00_set_chip_rt(struct rt2x00_dev *rt2x00dev,
				      const u16 rt)
{
	rt2x00dev->chip.rt = rt;
}

static inline void rt2x00_set_chip_rf(struct rt2x00_dev *rt2x00dev,
				      const u16 rf, const u32 rev)
{
	rt2x00_set_chip(rt2x00dev, rt2x00dev->chip.rt, rf, rev);
}

static inline char rt2x00_rt(const struct rt2x00_chip *chipset, const u16 chip)
{
	return (chipset->rt == chip);
}

static inline char rt2x00_rf(const struct rt2x00_chip *chipset, const u16 chip)
{
	return (chipset->rf == chip);
}

static inline u32 rt2x00_rev(const struct rt2x00_chip *chipset)
{
	return chipset->rev;
}

static inline bool rt2x00_check_rev(const struct rt2x00_chip *chipset,
				    const u32 mask, const u32 rev)
{
	return ((chipset->rev & mask) == rev);
}

/**
 * rt2x00queue_map_txskb - Map a skb into DMA for TX purposes.
 * @rt2x00dev: Pointer to &struct rt2x00_dev.
 * @skb: The skb to map.
 */
void rt2x00queue_map_txskb(struct rt2x00_dev *rt2x00dev, struct sk_buff *skb);

/**
 * rt2x00queue_get_queue - Convert queue index to queue pointer
 * @rt2x00dev: Pointer to &struct rt2x00_dev.
 * @queue: rt2x00 queue index (see &enum data_queue_qid).
 */
struct data_queue *rt2x00queue_get_queue(struct rt2x00_dev *rt2x00dev,
					 const enum data_queue_qid queue);

/**
 * rt2x00queue_get_entry - Get queue entry where the given index points to.
 * @queue: Pointer to &struct data_queue from where we obtain the entry.
 * @index: Index identifier for obtaining the correct index.
 */
struct queue_entry *rt2x00queue_get_entry(struct data_queue *queue,
					  enum queue_index index);

/*
 * Interrupt context handlers.
 */
void rt2x00lib_beacondone(struct rt2x00_dev *rt2x00dev);
void rt2x00lib_txdone(struct queue_entry *entry,
		      struct txdone_entry_desc *txdesc);
void rt2x00lib_rxdone(struct rt2x00_dev *rt2x00dev,
		      struct queue_entry *entry);

/*
 * mac80211 handlers.
 */
int rt2x00mac_tx(struct ieee80211_hw *hw, struct sk_buff *skb);
int rt2x00mac_start(struct ieee80211_hw *hw);
void rt2x00mac_stop(struct ieee80211_hw *hw);
int rt2x00mac_add_interface(struct ieee80211_hw *hw,
			    struct ieee80211_if_init_conf *conf);
void rt2x00mac_remove_interface(struct ieee80211_hw *hw,
				struct ieee80211_if_init_conf *conf);
int rt2x00mac_config(struct ieee80211_hw *hw, u32 changed);
void rt2x00mac_configure_filter(struct ieee80211_hw *hw,
				unsigned int changed_flags,
				unsigned int *total_flags,
				int mc_count, struct dev_addr_list *mc_list);
#ifdef CONFIG_RT2X00_LIB_CRYPTO
int rt2x00mac_set_key(struct ieee80211_hw *hw, enum set_key_cmd cmd,
		      struct ieee80211_vif *vif, struct ieee80211_sta *sta,
		      struct ieee80211_key_conf *key);
#else
#define rt2x00mac_set_key	NULL
#endif /* CONFIG_RT2X00_LIB_CRYPTO */
int rt2x00mac_get_stats(struct ieee80211_hw *hw,
			struct ieee80211_low_level_stats *stats);
int rt2x00mac_get_tx_stats(struct ieee80211_hw *hw,
			   struct ieee80211_tx_queue_stats *stats);
void rt2x00mac_bss_info_changed(struct ieee80211_hw *hw,
				struct ieee80211_vif *vif,
				struct ieee80211_bss_conf *bss_conf,
				u32 changes);
int rt2x00mac_conf_tx(struct ieee80211_hw *hw, u16 queue,
		      const struct ieee80211_tx_queue_params *params);

/*
 * Driver allocation handlers.
 */
int rt2x00lib_probe_dev(struct rt2x00_dev *rt2x00dev);
void rt2x00lib_remove_dev(struct rt2x00_dev *rt2x00dev);
#ifdef CONFIG_PM
int rt2x00lib_suspend(struct rt2x00_dev *rt2x00dev, pm_message_t state);
int rt2x00lib_resume(struct rt2x00_dev *rt2x00dev);
#endif /* CONFIG_PM */

#endif /* RT2X00_H */
