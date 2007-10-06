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
	Abstract: rt2x00 generic register information.
 */

#ifndef RT2X00REG_H
#define RT2X00REG_H

/*
 * TX result flags.
 */
enum TX_STATUS {
	TX_SUCCESS = 0,
	TX_SUCCESS_RETRY = 1,
	TX_FAIL_RETRY = 2,
	TX_FAIL_INVALID = 3,
	TX_FAIL_OTHER = 4,
};

/*
 * Antenna values
 */
enum antenna {
	ANTENNA_SW_DIVERSITY = 0,
	ANTENNA_A = 1,
	ANTENNA_B = 2,
	ANTENNA_HW_DIVERSITY = 3,
};

/*
 * Led mode values.
 */
enum led_mode {
	LED_MODE_DEFAULT = 0,
	LED_MODE_TXRX_ACTIVITY = 1,
	LED_MODE_SIGNAL_STRENGTH = 2,
	LED_MODE_ASUS = 3,
	LED_MODE_ALPHA = 4,
};

/*
 * TSF sync values
 */
enum tsf_sync {
	TSF_SYNC_NONE = 0,
	TSF_SYNC_INFRA = 1,
	TSF_SYNC_BEACON = 2,
};

/*
 * Device states
 */
enum dev_state {
	STATE_DEEP_SLEEP = 0,
	STATE_SLEEP = 1,
	STATE_STANDBY = 2,
	STATE_AWAKE = 3,

/*
 * Additional device states, these values are
 * not strict since they are not directly passed
 * into the device.
 */
	STATE_RADIO_ON,
	STATE_RADIO_OFF,
	STATE_RADIO_RX_ON,
	STATE_RADIO_RX_OFF,
	STATE_RADIO_IRQ_ON,
	STATE_RADIO_IRQ_OFF,
};

/*
 * IFS backoff values
 */
enum ifs {
	IFS_BACKOFF = 0,
	IFS_SIFS = 1,
	IFS_NEW_BACKOFF = 2,
	IFS_NONE = 3,
};

/*
 * Cipher types for hardware encryption
 */
enum cipher {
	CIPHER_NONE = 0,
	CIPHER_WEP64 = 1,
	CIPHER_WEP128 = 2,
	CIPHER_TKIP = 3,
	CIPHER_AES = 4,
/*
 * The following fields were added by rt61pci and rt73usb.
 */
	CIPHER_CKIP64 = 5,
	CIPHER_CKIP128 = 6,
	CIPHER_TKIP_NO_MIC = 7,
};

/*
 * Register handlers.
 * We store the position of a register field inside a field structure,
 * This will simplify the process of setting and reading a certain field
 * inside the register while making sure the process remains byte order safe.
 */
struct rt2x00_field8 {
	u8 bit_offset;
	u8 bit_mask;
};

struct rt2x00_field16 {
	u16 bit_offset;
	u16 bit_mask;
};

struct rt2x00_field32 {
	u32 bit_offset;
	u32 bit_mask;
};

/*
 * Power of two check, this will check
 * if the mask that has been given contains
 * and contiguous set of bits.
 */
#define is_power_of_two(x)	( !((x) & ((x)-1)) )
#define low_bit_mask(x)		( ((x)-1) & ~(x) )
#define is_valid_mask(x)	is_power_of_two(1 + (x) + low_bit_mask(x))

#define FIELD8(__mask)				\
({						\
	BUILD_BUG_ON(!(__mask) ||		\
		     !is_valid_mask(__mask) ||	\
		     (__mask) != (u8)(__mask));	\
	(struct rt2x00_field8) {		\
		__ffs(__mask), (__mask)		\
	};					\
})

#define FIELD16(__mask)				\
({						\
	BUILD_BUG_ON(!(__mask) ||		\
		     !is_valid_mask(__mask) ||	\
		     (__mask) != (u16)(__mask));\
	(struct rt2x00_field16) {		\
		__ffs(__mask), (__mask)		\
	};					\
})

#define FIELD32(__mask)				\
({						\
	BUILD_BUG_ON(!(__mask) ||		\
		     !is_valid_mask(__mask) ||	\
		     (__mask) != (u32)(__mask));\
	(struct rt2x00_field32) {		\
		__ffs(__mask), (__mask)		\
	};					\
})

static inline void rt2x00_set_field32(u32 *reg,
				      const struct rt2x00_field32 field,
				      const u32 value)
{
	*reg &= ~(field.bit_mask);
	*reg |= (value << field.bit_offset) & field.bit_mask;
}

static inline u32 rt2x00_get_field32(const u32 reg,
				     const struct rt2x00_field32 field)
{
	return (reg & field.bit_mask) >> field.bit_offset;
}

static inline void rt2x00_set_field16(u16 *reg,
				      const struct rt2x00_field16 field,
				      const u16 value)
{
	*reg &= ~(field.bit_mask);
	*reg |= (value << field.bit_offset) & field.bit_mask;
}

static inline u16 rt2x00_get_field16(const u16 reg,
				     const struct rt2x00_field16 field)
{
	return (reg & field.bit_mask) >> field.bit_offset;
}

static inline void rt2x00_set_field8(u8 *reg,
				     const struct rt2x00_field8 field,
				     const u8 value)
{
	*reg &= ~(field.bit_mask);
	*reg |= (value << field.bit_offset) & field.bit_mask;
}

static inline u8 rt2x00_get_field8(const u8 reg,
				   const struct rt2x00_field8 field)
{
	return (reg & field.bit_mask) >> field.bit_offset;
}

/*
 * Device specific rate value.
 * We will have to create the device specific rate value
 * passed to the ieee80211 kernel. We need to make it a consist of
 * multiple fields because we want to store more then 1 device specific
 * values inside the value.
 *	1 - rate, stored as 100 kbit/s.
 *	2 - preamble, short_preamble enabled flag.
 *	3 - MASK_RATE, which rates are enabled in this mode, this mask
 *	corresponds with the TX register format for the current device.
 *	4 - plcp, 802.11b rates are device specific,
 *	802.11g rates are set according to the ieee802.11a-1999 p.14.
 * The bit to enable preamble is set in a seperate define.
 */
#define DEV_RATE	FIELD32(0x000007ff)
#define DEV_PREAMBLE	FIELD32(0x00000800)
#define DEV_RATEMASK	FIELD32(0x00fff000)
#define DEV_PLCP	FIELD32(0xff000000)

/*
 * Bitfields
 */
#define DEV_RATEBIT_1MB		( 1 << 0 )
#define DEV_RATEBIT_2MB		( 1 << 1 )
#define DEV_RATEBIT_5_5MB	( 1 << 2 )
#define DEV_RATEBIT_11MB	( 1 << 3 )
#define DEV_RATEBIT_6MB		( 1 << 4 )
#define DEV_RATEBIT_9MB		( 1 << 5 )
#define DEV_RATEBIT_12MB	( 1 << 6 )
#define DEV_RATEBIT_18MB	( 1 << 7 )
#define DEV_RATEBIT_24MB	( 1 << 8 )
#define DEV_RATEBIT_36MB	( 1 << 9 )
#define DEV_RATEBIT_48MB	( 1 << 10 )
#define DEV_RATEBIT_54MB	( 1 << 11 )

/*
 * Bitmasks for DEV_RATEMASK
 */
#define DEV_RATEMASK_1MB	( (DEV_RATEBIT_1MB << 1) -1 )
#define DEV_RATEMASK_2MB	( (DEV_RATEBIT_2MB << 1) -1 )
#define DEV_RATEMASK_5_5MB	( (DEV_RATEBIT_5_5MB << 1) -1 )
#define DEV_RATEMASK_11MB	( (DEV_RATEBIT_11MB << 1) -1 )
#define DEV_RATEMASK_6MB	( (DEV_RATEBIT_6MB << 1) -1 )
#define DEV_RATEMASK_9MB	( (DEV_RATEBIT_9MB << 1) -1 )
#define DEV_RATEMASK_12MB	( (DEV_RATEBIT_12MB << 1) -1 )
#define DEV_RATEMASK_18MB	( (DEV_RATEBIT_18MB << 1) -1 )
#define DEV_RATEMASK_24MB	( (DEV_RATEBIT_24MB << 1) -1 )
#define DEV_RATEMASK_36MB	( (DEV_RATEBIT_36MB << 1) -1 )
#define DEV_RATEMASK_48MB	( (DEV_RATEBIT_48MB << 1) -1 )
#define DEV_RATEMASK_54MB	( (DEV_RATEBIT_54MB << 1) -1 )

/*
 * Bitmask groups of bitrates
 */
#define DEV_BASIC_RATEMASK \
	( DEV_RATEMASK_11MB | \
	  DEV_RATEBIT_6MB | DEV_RATEBIT_12MB | DEV_RATEBIT_24MB )

#define DEV_CCK_RATEMASK	( DEV_RATEMASK_11MB )
#define DEV_OFDM_RATEMASK	( DEV_RATEMASK_54MB & ~DEV_CCK_RATEMASK )

/*
 * Macro's to set and get specific fields from the device specific val and val2
 * fields inside the ieee80211_rate entry.
 */
#define DEVICE_SET_RATE_FIELD(__value, __mask) \
	(int)( ((__value) << DEV_##__mask.bit_offset) & DEV_##__mask.bit_mask )

#define DEVICE_GET_RATE_FIELD(__value, __mask) \
	(int)( ((__value) & DEV_##__mask.bit_mask) >> DEV_##__mask.bit_offset )

#endif /* RT2X00REG_H */
