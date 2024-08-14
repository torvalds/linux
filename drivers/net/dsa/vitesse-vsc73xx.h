/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/device.h>
#include <linux/etherdevice.h>
#include <linux/gpio/driver.h>

/* The VSC7395 switch chips have 5+1 ports which means 5 ordinary ports and
 * a sixth CPU port facing the processor with an RGMII interface. These ports
 * are numbered 0..4 and 6, so they leave a "hole" in the port map for port 5,
 * which is invalid.
 *
 * The VSC7398 has 8 ports, port 7 is again the CPU port.
 *
 * We allocate 8 ports and avoid access to the nonexistent ports.
 */
#define VSC73XX_MAX_NUM_PORTS	8

/**
 * struct vsc73xx_portinfo - port data structure: contains storage data
 * @pvid_vlan_filtering: pvid vlan number used in vlan filtering mode
 * @pvid_tag_8021q: pvid vlan number used in tag_8021q mode
 * @pvid_vlan_filtering_configured: informs if port has configured pvid in vlan
 *	filtering mode
 * @pvid_tag_8021q_configured: imforms if port have configured pvid in tag_8021q
 *	mode
 */
struct vsc73xx_portinfo {
	u16		pvid_vlan_filtering;
	u16		pvid_tag_8021q;
	bool		pvid_vlan_filtering_configured;
	bool		pvid_tag_8021q_configured;
};

/**
 * struct vsc73xx - VSC73xx state container: main data structure
 * @dev: The device pointer
 * @reset: The descriptor for the GPIO line tied to the reset pin
 * @ds: Pointer to the DSA core structure
 * @gc: Main structure of the GPIO controller
 * @chipid: Storage for the Chip ID value read from the CHIPID register of the
 *	switch
 * @addr: MAC address used in flow control frames
 * @ops: Structure with hardware-dependent operations
 * @priv: Pointer to the configuration interface structure
 * @portinfo: Storage table portinfo structructures
 * @vlans: List of configured vlans. Contains port mask and untagged status of
 *	every vlan configured in port vlan operation. It doesn't cover tag_8021q
 *	vlans.
 */
struct vsc73xx {
	struct device			*dev;
	struct gpio_desc		*reset;
	struct dsa_switch		*ds;
	struct gpio_chip		gc;
	u16				chipid;
	u8				addr[ETH_ALEN];
	const struct vsc73xx_ops	*ops;
	void				*priv;
	struct vsc73xx_portinfo		portinfo[VSC73XX_MAX_NUM_PORTS];
	struct list_head		vlans;
};

/**
 * struct vsc73xx_ops - VSC73xx methods container
 * @read: Method for register reading over the hardware-dependent interface
 * @write: Method for register writing over the hardware-dependent interface
 */
struct vsc73xx_ops {
	int (*read)(struct vsc73xx *vsc, u8 block, u8 subblock, u8 reg,
		    u32 *val);
	int (*write)(struct vsc73xx *vsc, u8 block, u8 subblock, u8 reg,
		     u32 val);
};

/**
 * struct vsc73xx_bridge_vlan - VSC73xx driver structure which keeps vlan
 *	database copy
 * @vid: VLAN number
 * @portmask: each bit represents one port
 * @untagged: each bit represents one port configured with @vid untagged
 * @list: list structure
 */
struct vsc73xx_bridge_vlan {
	u16 vid;
	u8 portmask;
	u8 untagged;
	struct list_head list;
};

int vsc73xx_is_addr_valid(u8 block, u8 subblock);
int vsc73xx_probe(struct vsc73xx *vsc);
void vsc73xx_remove(struct vsc73xx *vsc);
void vsc73xx_shutdown(struct vsc73xx *vsc);
