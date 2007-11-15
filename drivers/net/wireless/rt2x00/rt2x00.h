/*
	Copyright (C) 2004 - 2007 rt2x00 SourceForge Project
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
#include <linux/prefetch.h>
#include <linux/skbuff.h>
#include <linux/workqueue.h>
#include <linux/firmware.h>

#include <net/mac80211.h>

#include "rt2x00debug.h"
#include "rt2x00reg.h"
#include "rt2x00ring.h"

/*
 * Module information.
 * DRV_NAME should be set within the individual module source files.
 */
#define DRV_VERSION	"2.0.10"
#define DRV_PROJECT	"http://rt2x00.serialmonkey.com"

/*
 * Debug definitions.
 * Debug output has to be enabled during compile time.
 */
#define DEBUG_PRINTK_MSG(__dev, __kernlvl, __lvl, __msg, __args...)	\
	printk(__kernlvl "%s -> %s: %s - " __msg,			\
	       wiphy_name((__dev)->hw->wiphy), __FUNCTION__, __lvl, ##__args)

#define DEBUG_PRINTK_PROBE(__kernlvl, __lvl, __msg, __args...)	\
	printk(__kernlvl "%s -> %s: %s - " __msg,		\
	       DRV_NAME, __FUNCTION__, __lvl, ##__args)

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
 * Ring sizes.
 * Ralink PCI devices demand the Frame size to be a multiple of 128 bytes.
 * DATA_FRAME_SIZE is used for TX, RX, ATIM and PRIO rings.
 * MGMT_FRAME_SIZE is used for the BEACON ring.
 */
#define DATA_FRAME_SIZE	2432
#define MGMT_FRAME_SIZE	256

/*
 * Number of entries in a packet ring.
 * PCI devices only need 1 Beacon entry,
 * but USB devices require a second because they
 * have to send a Guardian byte first.
 */
#define RX_ENTRIES	12
#define TX_ENTRIES	12
#define ATIM_ENTRIES	1
#define BEACON_ENTRIES	2

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
#define EIFS			( SIFS + (8 * (IEEE80211_HEADER + ACK_SIZE)) )

/*
 * IEEE802.11 header defines
 */
static inline int is_rts_frame(u16 fc)
{
	return !!(((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_CTL) &&
		  ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_RTS));
}

static inline int is_cts_frame(u16 fc)
{
	return !!(((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_CTL) &&
		  ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_CTS));
}

static inline int is_probe_resp(u16 fc)
{
	return !!(((fc & IEEE80211_FCTL_FTYPE) == IEEE80211_FTYPE_MGMT) &&
		  ((fc & IEEE80211_FCTL_STYPE) == IEEE80211_STYPE_PROBE_RESP));
}

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
	 * Statistics required for Link tuning.
	 * For the average RSSI value we use the "Walking average" approach.
	 * When adding RSSI to the average value the following calculation
	 * is needed:
	 *
	 *        avg_rssi = ((avg_rssi * 7) + rssi) / 8;
	 *
	 * The advantage of this approach is that we only need 1 variable
	 * to store the average in (No need for a count and a total).
	 * But more importantly, normal average values will over time
	 * move less and less towards newly added values this results
	 * that with link tuning, the device can have a very good RSSI
	 * for a few minutes but when the device is moved away from the AP
	 * the average will not decrease fast enough to compensate.
	 * The walking average compensates this and will move towards
	 * the new values correctly allowing a effective link tuning.
	 */
	int avg_rssi;
	int vgc_level;
	int false_cca;

	/*
	 * Statistics required for Signal quality calculation.
	 * For calculating the Signal quality we have to determine
	 * the total number of success and failed RX and TX frames.
	 * After that we also use the average RSSI value to help
	 * determining the signal quality.
	 * For the calculation we will use the following algorithm:
	 *
	 *         rssi_percentage = (avg_rssi * 100) / rssi_offset
	 *         rx_percentage = (rx_success * 100) / rx_total
	 *         tx_percentage = (tx_success * 100) / tx_total
	 *         avg_signal = ((WEIGHT_RSSI * avg_rssi) +
	 *                       (WEIGHT_TX * tx_percentage) +
	 *                       (WEIGHT_RX * rx_percentage)) / 100
	 *
	 * This value should then be checked to not be greated then 100.
	 */
	int rx_percentage;
	int rx_success;
	int rx_failed;
	int tx_percentage;
	int tx_success;
	int tx_failed;
#define WEIGHT_RSSI	20
#define WEIGHT_RX	40
#define WEIGHT_TX	40

	/*
	 * Work structure for scheduling periodic link tuning.
	 */
	struct delayed_work work;
};

/*
 * Clear all counters inside the link structure.
 * This can be easiest achieved by memsetting everything
 * except for the work structure at the end.
 */
static inline void rt2x00_clear_link(struct link *link)
{
	memset(link, 0x00, sizeof(*link) - sizeof(link->work));
	link->rx_percentage = 50;
	link->tx_percentage = 50;
}

/*
 * Update the rssi using the walking average approach.
 */
static inline void rt2x00_update_link_rssi(struct link *link, int rssi)
{
	if (!link->avg_rssi)
		link->avg_rssi = rssi;
	else
		link->avg_rssi = ((link->avg_rssi * 7) + rssi) / 8;
}

/*
 * When the avg_rssi is unset or no frames  have been received),
 * we need to return the default value which needs to be less
 * than -80 so the device will select the maximum sensitivity.
 */
static inline int rt2x00_get_link_rssi(struct link *link)
{
	return (link->avg_rssi && link->rx_success) ? link->avg_rssi : -128;
}

/*
 * Interface structure
 * Configuration details about the current interface.
 */
struct interface {
	/*
	 * Interface identification. The value is assigned
	 * to us by the 80211 stack, and is used to request
	 * new beacons.
	 */
	int id;

	/*
	 * Current working type (IEEE80211_IF_TYPE_*).
	 * When set to INVALID_INTERFACE, no interface is configured.
	 */
	int type;
#define INVALID_INTERFACE	IEEE80211_IF_TYPE_INVALID

	/*
	 * MAC of the device.
	 */
	u8 mac[ETH_ALEN];

	/*
	 * BBSID of the AP to associate with.
	 */
	u8 bssid[ETH_ALEN];

	/*
	 * Store the packet filter mode for the current interface.
	 */
	unsigned int filter;
};

static inline int is_interface_present(struct interface *intf)
{
	return !!intf->id;
}

static inline int is_interface_type(struct interface *intf, int type)
{
	return intf->type == type;
}

/*
 * Details about the supported modes, rates and channels
 * of a particular chipset. This is used by rt2x00lib
 * to build the ieee80211_hw_mode array for mac80211.
 */
struct hw_mode_spec {
	/*
	 * Number of modes, rates and channels.
	 */
	int num_modes;
	int num_rates;
	int num_channels;

	/*
	 * txpower values.
	 */
	const u8 *tx_power_a;
	const u8 *tx_power_bg;
	u8 tx_power_default;

	/*
	 * Device/chipset specific value.
	 */
	const struct rf_channel *channels;
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

	int phymode;

	int basic_rates;
	int slot_time;

	short sifs;
	short pifs;
	short difs;
	short eifs;
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
	int (*load_firmware) (struct rt2x00_dev *rt2x00dev, void *data,
			      const size_t len);

	/*
	 * Device initialization/deinitialization handlers.
	 */
	int (*initialize) (struct rt2x00_dev *rt2x00dev);
	void (*uninitialize) (struct rt2x00_dev *rt2x00dev);

	/*
	 * Radio control handlers.
	 */
	int (*set_device_state) (struct rt2x00_dev *rt2x00dev,
				 enum dev_state state);
	int (*rfkill_poll) (struct rt2x00_dev *rt2x00dev);
	void (*link_stats) (struct rt2x00_dev *rt2x00dev);
	void (*reset_tuner) (struct rt2x00_dev *rt2x00dev);
	void (*link_tuner) (struct rt2x00_dev *rt2x00dev);

	/*
	 * TX control handlers
	 */
	void (*write_tx_desc) (struct rt2x00_dev *rt2x00dev,
			       struct data_desc *txd,
			       struct txdata_entry_desc *desc,
			       struct ieee80211_hdr *ieee80211hdr,
			       unsigned int length,
			       struct ieee80211_tx_control *control);
	int (*write_tx_data) (struct rt2x00_dev *rt2x00dev,
			      struct data_ring *ring, struct sk_buff *skb,
			      struct ieee80211_tx_control *control);
	int (*get_tx_data_len) (struct rt2x00_dev *rt2x00dev, int maxpacket,
				struct sk_buff *skb);
	void (*kick_tx_queue) (struct rt2x00_dev *rt2x00dev,
			       unsigned int queue);

	/*
	 * RX control handlers
	 */
	void (*fill_rxdone) (struct data_entry *entry,
			     struct rxdata_entry_desc *desc);

	/*
	 * Configuration handlers.
	 */
	void (*config_mac_addr) (struct rt2x00_dev *rt2x00dev, __le32 *mac);
	void (*config_bssid) (struct rt2x00_dev *rt2x00dev, __le32 *bssid);
	void (*config_type) (struct rt2x00_dev *rt2x00dev, const int type,
							   const int tsf_sync);
	void (*config_preamble) (struct rt2x00_dev *rt2x00dev,
				 const int short_preamble,
				 const int ack_timeout,
				 const int ack_consume_time);
	void (*config) (struct rt2x00_dev *rt2x00dev, const unsigned int flags,
			struct rt2x00lib_conf *libconf);
#define CONFIG_UPDATE_PHYMODE		( 1 << 1 )
#define CONFIG_UPDATE_CHANNEL		( 1 << 2 )
#define CONFIG_UPDATE_TXPOWER		( 1 << 3 )
#define CONFIG_UPDATE_ANTENNA		( 1 << 4 )
#define CONFIG_UPDATE_SLOT_TIME 	( 1 << 5 )
#define CONFIG_UPDATE_BEACON_INT	( 1 << 6 )
#define CONFIG_UPDATE_ALL		0xffff
};

/*
 * rt2x00 driver callback operation structure.
 */
struct rt2x00_ops {
	const char *name;
	const unsigned int rxd_size;
	const unsigned int txd_size;
	const unsigned int eeprom_size;
	const unsigned int rf_size;
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
	DEVICE_PRESENT,
	DEVICE_REGISTERED_HW,
	DEVICE_INITIALIZED,
	DEVICE_STARTED,
	DEVICE_STARTED_SUSPEND,
	DEVICE_ENABLED_RADIO,
	DEVICE_DISABLED_RADIO_HW,

	/*
	 * Driver features
	 */
	DRIVER_REQUIRE_FIRMWARE,
	DRIVER_REQUIRE_BEACON_RING,

	/*
	 * Driver configuration
	 */
	CONFIG_SUPPORT_HW_BUTTON,
	CONFIG_FRAME_TYPE,
	CONFIG_RF_SEQUENCE,
	CONFIG_EXTERNAL_LNA_A,
	CONFIG_EXTERNAL_LNA_BG,
	CONFIG_DOUBLE_ANTENNA,
	CONFIG_DISABLE_LINK_TUNING,
	CONFIG_SHORT_PREAMBLE,
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
	void *dev;
#define rt2x00dev_pci(__dev)	( (struct pci_dev*)(__dev)->dev )
#define rt2x00dev_usb(__dev)	( (struct usb_interface*)(__dev)->dev )

	/*
	 * Callback functions.
	 */
	const struct rt2x00_ops *ops;

	/*
	 * IEEE80211 control structure.
	 */
	struct ieee80211_hw *hw;
	struct ieee80211_hw_mode *hwmodes;
	unsigned int curr_hwmode;
#define HWMODE_B	0
#define HWMODE_G	1
#define HWMODE_A	2

	/*
	 * rfkill structure for RF state switching support.
	 * This will only be compiled in when required.
	 */
#ifdef CONFIG_RT2X00_LIB_RFKILL
	struct rfkill *rfkill;
	struct input_polled_dev *poll_dev;
#endif /* CONFIG_RT2X00_LIB_RFKILL */

	/*
	 * If enabled, the debugfs interface structures
	 * required for deregistration of debugfs.
	 */
#ifdef CONFIG_RT2X00_LIB_DEBUGFS
	const struct rt2x00debug_intf *debugfs_intf;
#endif /* CONFIG_RT2X00_LIB_DEBUGFS */

	/*
	 * Device flags.
	 * In these flags the current status and some
	 * of the device capabilities are stored.
	 */
	unsigned long flags;

	/*
	 * Chipset identification.
	 */
	struct rt2x00_chip chip;

	/*
	 * hw capability specifications.
	 */
	struct hw_mode_spec spec;

	/*
	 * Register pointers
	 * csr_addr: Base register address. (PCI)
	 * csr_cache: CSR cache for usb_control_msg. (USB)
	 */
	void __iomem *csr_addr;
	void *csr_cache;

	/*
	 * Interface configuration.
	 */
	struct interface interface;

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
	 * Current TX power value.
	 */
	u16 tx_power;

	/*
	 * LED register (for rt61pci & rt73usb).
	 */
	u16 led_reg;

	/*
	 * Led mode (LED_MODE_*)
	 */
	u8 led_mode;

	/*
	 * Rssi <-> Dbm offset
	 */
	u8 rssi_offset;

	/*
	 * Frequency offset (for rt61pci & rt73usb).
	 */
	u8 freq_offset;

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
	 */
	struct work_struct beacon_work;
	struct work_struct filter_work;
	struct work_struct config_work;

	/*
	 * Data ring arrays for RX, TX and Beacon.
	 * The Beacon array also contains the Atim ring
	 * if that is supported by the device.
	 */
	int data_rings;
	struct data_ring *rx;
	struct data_ring *tx;
	struct data_ring *bcn;

	/*
	 * Firmware image.
	 */
	const struct firmware *fw;
};

/*
 * For-each loop for the ring array.
 * All rings have been allocated as a single array,
 * this means we can create a very simply loop macro
 * that is capable of looping through all rings.
 * ring_end(), txring_end() and ring_loop() are helper macro's which
 * should not be used directly. Instead the following should be used:
 * ring_for_each() - Loops through all rings (RX, TX, Beacon & Atim)
 * txring_for_each() - Loops through TX data rings (TX only)
 * txringall_for_each() - Loops through all TX rings (TX, Beacon & Atim)
 */
#define ring_end(__dev) \
	&(__dev)->rx[(__dev)->data_rings]

#define txring_end(__dev) \
	&(__dev)->tx[(__dev)->hw->queues]

#define ring_loop(__entry, __start, __end)			\
	for ((__entry) = (__start);				\
	     prefetch(&(__entry)[1]), (__entry) != (__end);	\
	     (__entry) = &(__entry)[1])

#define ring_for_each(__dev, __entry) \
	ring_loop(__entry, (__dev)->rx, ring_end(__dev))

#define txring_for_each(__dev, __entry) \
	ring_loop(__entry, (__dev)->tx, txring_end(__dev))

#define txringall_for_each(__dev, __entry) \
	ring_loop(__entry, (__dev)->tx, ring_end(__dev))

/*
 * Generic RF access.
 * The RF is being accessed by word index.
 */
static inline void rt2x00_rf_read(const struct rt2x00_dev *rt2x00dev,
				  const unsigned int word, u32 *data)
{
	*data = rt2x00dev->rf[word];
}

static inline void rt2x00_rf_write(const struct rt2x00_dev *rt2x00dev,
				   const unsigned int word, u32 data)
{
	rt2x00dev->rf[word] = data;
}

/*
 *  Generic EEPROM access.
 * The EEPROM is being accessed by word index.
 */
static inline void *rt2x00_eeprom_addr(const struct rt2x00_dev *rt2x00dev,
				       const unsigned int word)
{
	return (void *)&rt2x00dev->eeprom[word];
}

static inline void rt2x00_eeprom_read(const struct rt2x00_dev *rt2x00dev,
				      const unsigned int word, u16 *data)
{
	*data = le16_to_cpu(rt2x00dev->eeprom[word]);
}

static inline void rt2x00_eeprom_write(const struct rt2x00_dev *rt2x00dev,
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

static inline char rt2x00_rt(const struct rt2x00_chip *chipset, const u16 chip)
{
	return (chipset->rt == chip);
}

static inline char rt2x00_rf(const struct rt2x00_chip *chipset, const u16 chip)
{
	return (chipset->rf == chip);
}

static inline u16 rt2x00_rev(const struct rt2x00_chip *chipset)
{
	return chipset->rev;
}

static inline u16 rt2x00_check_rev(const struct rt2x00_chip *chipset,
				   const u32 rev)
{
	return (((chipset->rev & 0xffff0) == rev) &&
		!!(chipset->rev & 0x0000f));
}

/*
 * Duration calculations
 * The rate variable passed is: 100kbs.
 * To convert from bytes to bits we multiply size with 8,
 * then the size is multiplied with 10 to make the
 * real rate -> rate argument correction.
 */
static inline u16 get_duration(const unsigned int size, const u8 rate)
{
	return ((size * 8 * 10) / rate);
}

static inline u16 get_duration_res(const unsigned int size, const u8 rate)
{
	return ((size * 8 * 10) % rate);
}

/*
 * Library functions.
 */
struct data_ring *rt2x00lib_get_ring(struct rt2x00_dev *rt2x00dev,
				     const unsigned int queue);

/*
 * Interrupt context handlers.
 */
void rt2x00lib_beacondone(struct rt2x00_dev *rt2x00dev);
void rt2x00lib_txdone(struct data_entry *entry,
		      const int status, const int retry);
void rt2x00lib_rxdone(struct data_entry *entry, struct sk_buff *skb,
		      struct rxdata_entry_desc *desc);

/*
 * TX descriptor initializer
 */
void rt2x00lib_write_tx_desc(struct rt2x00_dev *rt2x00dev,
			     struct data_desc *txd,
			     struct ieee80211_hdr *ieee80211hdr,
			     unsigned int length,
			     struct ieee80211_tx_control *control);

/*
 * mac80211 handlers.
 */
int rt2x00mac_tx(struct ieee80211_hw *hw, struct sk_buff *skb,
		 struct ieee80211_tx_control *control);
int rt2x00mac_start(struct ieee80211_hw *hw);
void rt2x00mac_stop(struct ieee80211_hw *hw);
int rt2x00mac_add_interface(struct ieee80211_hw *hw,
			    struct ieee80211_if_init_conf *conf);
void rt2x00mac_remove_interface(struct ieee80211_hw *hw,
				struct ieee80211_if_init_conf *conf);
int rt2x00mac_config(struct ieee80211_hw *hw, struct ieee80211_conf *conf);
int rt2x00mac_config_interface(struct ieee80211_hw *hw, int if_id,
			       struct ieee80211_if_conf *conf);
int rt2x00mac_get_stats(struct ieee80211_hw *hw,
			struct ieee80211_low_level_stats *stats);
int rt2x00mac_get_tx_stats(struct ieee80211_hw *hw,
			   struct ieee80211_tx_queue_stats *stats);
void rt2x00mac_erp_ie_changed(struct ieee80211_hw *hw, u8 changes,
			      int cts_protection, int preamble);
int rt2x00mac_conf_tx(struct ieee80211_hw *hw, int queue,
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
