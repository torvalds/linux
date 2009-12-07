#ifndef BCM63XX_DEV_ENET_H_
#define BCM63XX_DEV_ENET_H_

#include <linux/if_ether.h>
#include <linux/init.h>

/*
 * on board ethernet platform data
 */
struct bcm63xx_enet_platform_data {
	char mac_addr[ETH_ALEN];

	int has_phy;

	/* if has_phy, then set use_internal_phy */
	int use_internal_phy;

	/* or fill phy info to use an external one */
	int phy_id;
	int has_phy_interrupt;
	int phy_interrupt;

	/* if has_phy, use autonegociated pause parameters or force
	 * them */
	int pause_auto;
	int pause_rx;
	int pause_tx;

	/* if !has_phy, set desired forced speed/duplex */
	int force_speed_100;
	int force_duplex_full;

	/* if !has_phy, set callback to perform mii device
	 * init/remove */
	int (*mii_config)(struct net_device *dev, int probe,
			  int (*mii_read)(struct net_device *dev,
					  int phy_id, int reg),
			  void (*mii_write)(struct net_device *dev,
					    int phy_id, int reg, int val));
};

int __init bcm63xx_enet_register(int unit,
				 const struct bcm63xx_enet_platform_data *pd);

#endif /* ! BCM63XX_DEV_ENET_H_ */
