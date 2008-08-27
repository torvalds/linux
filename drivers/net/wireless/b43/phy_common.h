#ifndef LINUX_B43_PHY_COMMON_H_
#define LINUX_B43_PHY_COMMON_H_

#include <linux/rfkill.h>

struct b43_wldev;


/* PHY register routing bits */
#define B43_PHYROUTE			0x0C00 /* PHY register routing bits mask */
#define  B43_PHYROUTE_BASE		0x0000 /* Base registers */
#define  B43_PHYROUTE_OFDM_GPHY		0x0400 /* OFDM register routing for G-PHYs */
#define  B43_PHYROUTE_EXT_GPHY		0x0800 /* Extended G-PHY registers */
#define  B43_PHYROUTE_N_BMODE		0x0C00 /* N-PHY BMODE registers */

/* CCK (B-PHY) registers. */
#define B43_PHY_CCK(reg)		((reg) | B43_PHYROUTE_BASE)
/* N-PHY registers. */
#define B43_PHY_N(reg)			((reg) | B43_PHYROUTE_BASE)
/* N-PHY BMODE registers. */
#define B43_PHY_N_BMODE(reg)		((reg) | B43_PHYROUTE_N_BMODE)
/* OFDM (A-PHY) registers. */
#define B43_PHY_OFDM(reg)		((reg) | B43_PHYROUTE_OFDM_GPHY)
/* Extended G-PHY registers. */
#define B43_PHY_EXTG(reg)		((reg) | B43_PHYROUTE_EXT_GPHY)


/* Masks for the PHY versioning registers. */
#define B43_PHYVER_ANALOG		0xF000
#define B43_PHYVER_ANALOG_SHIFT		12
#define B43_PHYVER_TYPE			0x0F00
#define B43_PHYVER_TYPE_SHIFT		8
#define B43_PHYVER_VERSION		0x00FF

/**
 * enum b43_interference_mitigation - Interference Mitigation mode
 *
 * @B43_INTERFMODE_NONE:	Disabled
 * @B43_INTERFMODE_NONWLAN:	Non-WLAN Interference Mitigation
 * @B43_INTERFMODE_MANUALWLAN:	WLAN Interference Mitigation
 * @B43_INTERFMODE_AUTOWLAN:	Automatic WLAN Interference Mitigation
 */
enum b43_interference_mitigation {
	B43_INTERFMODE_NONE,
	B43_INTERFMODE_NONWLAN,
	B43_INTERFMODE_MANUALWLAN,
	B43_INTERFMODE_AUTOWLAN,
};

/* Antenna identifiers */
enum {
	B43_ANTENNA0,		/* Antenna 0 */
	B43_ANTENNA1,		/* Antenna 0 */
	B43_ANTENNA_AUTO1,	/* Automatic, starting with antenna 1 */
	B43_ANTENNA_AUTO0,	/* Automatic, starting with antenna 0 */
	B43_ANTENNA2,
	B43_ANTENNA3 = 8,

	B43_ANTENNA_AUTO = B43_ANTENNA_AUTO0,
	B43_ANTENNA_DEFAULT = B43_ANTENNA_AUTO,
};

/**
 * struct b43_phy_operations - Function pointers for PHY ops.
 *
 * @prepare:		Prepare the PHY. This is called before @init.
 * 			Can be NULL, if not required.
 * @init:		Initialize the PHY.
 * 			Must not be NULL.
 * @exit:		Shutdown the PHY and free all data structures.
 * 			Can be NULL, if not required.
 *
 * @phy_read:		Read from a PHY register.
 * 			Must not be NULL.
 * @phy_write:		Write to a PHY register.
 * 			Must not be NULL.
 * @radio_read:		Read from a Radio register.
 * 			Must not be NULL.
 * @radio_write:	Write to a Radio register.
 * 			Must not be NULL.
 *
 * @supports_hwpctl:	Returns a boolean whether Hardware Power Control
 * 			is supported or not.
 * 			If NULL, hwpctl is assumed to be never supported.
 * @software_rfkill:	Turn the radio ON or OFF.
 * 			Possible state values are
 * 			RFKILL_STATE_SOFT_BLOCKED or
 * 			RFKILL_STATE_UNBLOCKED
 * 			Must not be NULL.
 * @switch_channel:	Switch the radio to another channel.
 * 			Must not be NULL.
 * @get_default_chan:	Just returns the default channel number.
 * 			Must not be NULL.
 * @set_rx_antenna:	Set the antenna used for RX.
 * 			Can be NULL, if not supported.
 * @interf_mitigation:	Switch the Interference Mitigation mode.
 * 			Can be NULL, if not supported.
 *
 * @xmitpower:		FIXME REMOVEME
 * 			Must not be NULL.
 *
 * @pwork_15sec:	Periodic work. Called every 15 seconds.
 * 			Can be NULL, if not required.
 * @pwork_60sec:	Periodic work. Called every 60 seconds.
 * 			Can be NULL, if not required.
 */
struct b43_phy_operations {
	/* Initialisation */
	int (*allocate)(struct b43_wldev *dev);
	int (*prepare)(struct b43_wldev *dev);
	int (*init)(struct b43_wldev *dev);
	void (*exit)(struct b43_wldev *dev);

	/* Register access */
	u16 (*phy_read)(struct b43_wldev *dev, u16 reg);
	void (*phy_write)(struct b43_wldev *dev, u16 reg, u16 value);
	u16 (*radio_read)(struct b43_wldev *dev, u16 reg);
	void (*radio_write)(struct b43_wldev *dev, u16 reg, u16 value);

	/* Radio */
	bool (*supports_hwpctl)(struct b43_wldev *dev);
	void (*software_rfkill)(struct b43_wldev *dev, enum rfkill_state state);
	int (*switch_channel)(struct b43_wldev *dev, unsigned int new_channel);
	unsigned int (*get_default_chan)(struct b43_wldev *dev);
	void (*set_rx_antenna)(struct b43_wldev *dev, int antenna);
	int (*interf_mitigation)(struct b43_wldev *dev,
				 enum b43_interference_mitigation new_mode);

	/* Transmission power adjustment */
	void (*xmitpower)(struct b43_wldev *dev);

	/* Misc */
	void (*pwork_15sec)(struct b43_wldev *dev);
	void (*pwork_60sec)(struct b43_wldev *dev);
};

struct b43_phy_a;
struct b43_phy_g;
struct b43_phy_n;

struct b43_phy {
	/* Hardware operation callbacks. */
	const struct b43_phy_operations *ops;

	/* Most hardware context information is stored in the standard-
	 * specific data structures pointed to by the pointers below.
	 * Only one of them is valid (the currently enabled PHY). */
#ifdef CONFIG_B43_DEBUG
	/* No union for debug build to force NULL derefs in buggy code. */
	struct {
#else
	union {
#endif
		/* A-PHY specific information */
		struct b43_phy_a *a;
		/* G-PHY specific information */
		struct b43_phy_g *g;
		/* N-PHY specific information */
		struct b43_phy_n *n;
	};

	/* Band support flags. */
	bool supports_2ghz;
	bool supports_5ghz;

	/* GMODE bit enabled? */
	bool gmode;

	/* Analog Type */
	u8 analog;
	/* B43_PHYTYPE_ */
	u8 type;
	/* PHY revision number. */
	u8 rev;

	/* Radio versioning */
	u16 radio_manuf;	/* Radio manufacturer */
	u16 radio_ver;		/* Radio version */
	u8 radio_rev;		/* Radio revision */

	/* Software state of the radio */
	bool radio_on;

	/* Desired TX power level (in dBm).
	 * This is set by the user and adjusted in b43_phy_xmitpower(). */
	u8 power_level;

	/* Hardware Power Control enabled? */
	bool hardware_power_control;

	/* current channel */
	unsigned int channel;

	/* PHY TX errors counter. */
	atomic_t txerr_cnt;

#ifdef CONFIG_B43_DEBUG
	/* PHY registers locked by b43_phy_lock()? */
	bool phy_locked;
#endif /* B43_DEBUG */
};


/**
 * b43_phy_operations_setup - Initialize the PHY operations datastructure
 * based on the current PHY type.
 */
int b43_phy_operations_setup(struct b43_wldev *dev);

/**
 * b43_phy_init - Initialise the PHY
 */
int b43_phy_init(struct b43_wldev *dev);

/**
 * b43_phy_exit - Cleanup PHY
 */
void b43_phy_exit(struct b43_wldev *dev);

/**
 * b43_has_hardware_pctl - Hardware Power Control supported?
 * Returns a boolean, whether hardware power control is supported.
 */
bool b43_has_hardware_pctl(struct b43_wldev *dev);

/**
 * b43_phy_read - 16bit PHY register read access
 */
u16 b43_phy_read(struct b43_wldev *dev, u16 reg);

/**
 * b43_phy_write - 16bit PHY register write access
 */
void b43_phy_write(struct b43_wldev *dev, u16 reg, u16 value);

/**
 * b43_phy_mask - Mask a PHY register with a mask
 */
void b43_phy_mask(struct b43_wldev *dev, u16 offset, u16 mask);

/**
 * b43_phy_set - OR a PHY register with a bitmap
 */
void b43_phy_set(struct b43_wldev *dev, u16 offset, u16 set);

/**
 * b43_phy_maskset - Mask and OR a PHY register with a mask and bitmap
 */
void b43_phy_maskset(struct b43_wldev *dev, u16 offset, u16 mask, u16 set);

/**
 * b43_radio_read - 16bit Radio register read access
 */
u16 b43_radio_read(struct b43_wldev *dev, u16 reg);
#define b43_radio_read16	b43_radio_read /* DEPRECATED */

/**
 * b43_radio_write - 16bit Radio register write access
 */
void b43_radio_write(struct b43_wldev *dev, u16 reg, u16 value);
#define b43_radio_write16	b43_radio_write /* DEPRECATED */

/**
 * b43_radio_mask - Mask a 16bit radio register with a mask
 */
void b43_radio_mask(struct b43_wldev *dev, u16 offset, u16 mask);

/**
 * b43_radio_set - OR a 16bit radio register with a bitmap
 */
void b43_radio_set(struct b43_wldev *dev, u16 offset, u16 set);

/**
 * b43_radio_maskset - Mask and OR a radio register with a mask and bitmap
 */
void b43_radio_maskset(struct b43_wldev *dev, u16 offset, u16 mask, u16 set);

/**
 * b43_radio_lock - Lock firmware radio register access
 */
void b43_radio_lock(struct b43_wldev *dev);

/**
 * b43_radio_unlock - Unlock firmware radio register access
 */
void b43_radio_unlock(struct b43_wldev *dev);

/**
 * b43_phy_lock - Lock firmware PHY register access
 */
void b43_phy_lock(struct b43_wldev *dev);

/**
 * b43_phy_unlock - Unlock firmware PHY register access
 */
void b43_phy_unlock(struct b43_wldev *dev);

/**
 * b43_switch_channel - Switch to another channel
 */
int b43_switch_channel(struct b43_wldev *dev, unsigned int new_channel);
/**
 * B43_DEFAULT_CHANNEL - Switch to the default channel.
 */
#define B43_DEFAULT_CHANNEL	UINT_MAX

/**
 * b43_software_rfkill - Turn the radio ON or OFF in software.
 */
void b43_software_rfkill(struct b43_wldev *dev, enum rfkill_state state);

#endif /* LINUX_B43_PHY_COMMON_H_ */
