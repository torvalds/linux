#include <linux/mii.h>
#include <linux/phy.h>
#include <linux/slab.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>

#define MII_BUSY 0x00000001
#define MII_WRITE 0x00000002

static int mdio_read(struct mii_bus *bus, int phyaddr, int phyreg)
{
	struct net_device *ndev = bus->priv;
        struct am_net_private *priv = netdev_priv(ndev);
        unsigned int mii_address = ETH_MAC_4_GMII_Addr;
        unsigned int mii_data = ETH_MAC_5_GMII_Data;

        int data;
        u16 regValue = (((phyaddr << 11) & (0x0000F800)) |
                        ((phyreg << 6) & (0x000007C0)));
        regValue |= MII_BUSY | MDCCLK;

        do {} while (((readl((void*)(priv->base_addr + mii_address))) & MII_BUSY) == 1);
        writel(regValue, (void*)(priv->base_addr + mii_address));
        do {} while (((readl((void*)(priv->base_addr + mii_address))) & MII_BUSY) == 1);

        /* Read the data from the MII data register */
        data = (int)readl((void*)(priv->base_addr + mii_data));

        return data;
}

static int mdio_write(struct mii_bus *bus, int phyaddr, int phyreg, u16 phydata)
{
        struct net_device *ndev = bus->priv;
        struct am_net_private *priv = netdev_priv(ndev);

        unsigned int mii_address = ETH_MAC_4_GMII_Addr;
        unsigned int mii_data = ETH_MAC_5_GMII_Data;

        u16 value = (((phyaddr << 11) & (0x0000F800)) | ((phyreg << 6) & (0x000007C0))) | MII_WRITE;
        value |= MII_BUSY | MDCCLK;

        do {} while (((readl((void*)(priv->base_addr + mii_address))) & MII_BUSY) == 1);
        writel(phydata, (void*)(priv->base_addr + mii_data));

        writel(value, (void*)(priv->base_addr + mii_address));

        do {} while (((readl((void*)(priv->base_addr + mii_address))) & MII_BUSY) == 1);
	
	return 0;
}

static int mdio_reset(struct mii_bus *bus)
{
       return 0;
}

/**
 * aml_mdio_register
 * @ndev: net device structure
 * Description: it registers the MII bus
 */
int aml_mdio_register(struct net_device *ndev)
{
        int err = 0;
        struct mii_bus *new_bus;
	int *irqlist;
        struct am_net_private *priv = netdev_priv(ndev);
        int addr, found;

	priv->phy_addr = -1;
        new_bus = mdiobus_alloc();
        if (new_bus == NULL)
                return -ENOMEM;

	irqlist = kzalloc(sizeof(int) * PHY_MAX_ADDR, GFP_KERNEL);
	if (irqlist == NULL) {
		err = -ENOMEM;
		goto irqlist_alloc_fail;
	}

        new_bus->name = "AMLMAC MII Bus";
        new_bus->read = &mdio_read;
        new_bus->write = &mdio_write;
        new_bus->reset = &mdio_reset;
        snprintf(new_bus->id, MII_BUS_ID_SIZE, "%x", 0);
        new_bus->priv = ndev;
	new_bus->irq = irqlist;
        new_bus->phy_mask = priv->phy_mask;
        err = mdiobus_register(new_bus);
        if (err != 0) {
                pr_err("%s: Cannot register as MDIO bus\n", new_bus->name);
                goto bus_register_fail;
        }

        priv->mii = new_bus;

        found = 0;
	for (addr = 0; addr < 32; addr++) {
		struct phy_device *phydev = new_bus->phy_map[addr];
		if (phydev) {
			priv->phydev = phydev;
			if (priv->phy_addr == -1) {
				priv->phy_addr = addr;
				phydev->irq = PHY_POLL;
				irqlist[addr] = PHY_POLL;
			}
			if (phydev->phy_id  != 0) {
				//priv->phydev->addr = addr;
				if (!((phydev->phy_id  == 0x001cc916)&& (addr == 0))) 
				{
					priv->phy_addr = addr;
					phydev->irq = PHY_POLL;
					irqlist[addr] = PHY_POLL;
				}
			}
			pr_info("%s: PHY ID %08x at %d IRQ %d (%s)%s\n",
					ndev->name, phydev->phy_id, addr,
					phydev->irq, dev_name(&phydev->dev),
					(addr == priv->phy_addr) ? " active" : "");
			found = 1;
		}
	}

	if (!found)
		pr_warning("%s: No PHY found\n", ndev->name);

	return 0;
bus_register_fail:
	kfree(irqlist);
irqlist_alloc_fail:
	kfree(new_bus);
	return err;
}

/**
 * mdio_unregister
 * @ndev: net device structure
 * Description: it unregisters the MII bus
 */
int aml_mdio_unregister(struct net_device *ndev)
{
        struct am_net_private *priv = netdev_priv(ndev);

        mdiobus_unregister(priv->mii);
        priv->mii->priv = NULL;
        kfree(priv->mii);

        return 0;
}
