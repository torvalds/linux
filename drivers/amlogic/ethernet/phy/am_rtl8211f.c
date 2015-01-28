#include <linux/phy.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <uapi/linux/ethtool.h>
//#define RTL_SPRD_CLK_MODE	1			// enables spread spectrum clocks, disable for slight boost
//#define RTL_GREEN_MODE		1			// disable to stop grn mode and slight boost in perf
#define RTL821x_PHYSR			0x11
#define RTL821x_PHYSR_DUPLEX	0x2000
#define RTL821x_PHYSR_SPEED		0xc000
#define RTL821x_INER			0x12
#define RTL821x_INER_INIT		0x6400
#define RTL821x_INSR			0x13
#define RTL8211F_MMD_CTRL       0x0D
#define RTL8211F_MMD_DATA       0x0E
#define	RTL8211E_INER_LINK_STAT	0x10

#define RTL8211F_PHYCTRL        0

#define RTL8211F_PHYCR1         24
#define RTL8211F_PHYCR2         25
#define RTL8211F_PHYSR          26
#define RTL8211F_REGPAGE        31

#define RTL8211F_RXCSSC         19
#define RTL8211F_SYSCLK_SSC     23

MODULE_DESCRIPTION("Realtek PHY driver");
MODULE_AUTHOR("Johnson Leung");
MODULE_LICENSE("GPL");

//	probably can go back to using genphy_read_status, but this may fix PHY_AN till later kernels
static int rtl8211e_read_status(struct phy_device *phydev)
{
	int val, tmp;
        phy_write(phydev, RTL8211F_REGPAGE, 0x0a43);    // return to page 0xa43
        val = phy_read(phydev, RTL8211F_PHYSR);			// phy status reg
		if (val & (1<<3)) {		// check duplex setting
				phydev->duplex = DUPLEX_FULL;
		} else {
				phydev->duplex = DUPLEX_HALF;
		}
		tmp = (val & ((1<<4)|(1<<5))) >> 4;		// just look at speed bits
		if (tmp == 0) {			// 10mbs
				phydev->speed = SPEED_10;
		}
		if (tmp == 1) {			// 100mbs
				phydev->speed = SPEED_100;
		}
		if (tmp == 2) {			// 1000mbs
				phydev->speed = SPEED_1000;
		}
		if (val & (1<<2)) {		// link status in real time
			phydev->link = 1;	// link up
		} else {
			phydev->link = 0;	// link down
		}
		// in later kernels 3.18+ they change phy.c state machine to do 
		// PHY_AN in proper location
		if (phydev->link) {
        	val = phy_read(phydev, 0x01);			// phy status reg
			if (val & (1<<5)) {
				phydev->state = PHY_RUNNING;// autoneg completed
				netif_carrier_on(phydev->attached_dev);
				phydev->adjust_link(phydev->attached_dev);
			} else {
				phydev->state = PHY_AN;		// autoneg not completed
				netif_carrier_on(phydev->attached_dev);
				phydev->adjust_link(phydev->attached_dev);
			}
		} else {
			phydev->state = PHY_NOLINK;
			netif_carrier_off(phydev->attached_dev);
			phydev->adjust_link(phydev->attached_dev);
		}

		phydev->pause = phydev->asym_pause = 1;
        phy_write(phydev, RTL8211F_REGPAGE, 0x0000);    // return to page 0
		return 0;
}

static int rtl8211e_config_init(struct phy_device *phydev)
{
	int val;
        /* Disable CLK_OUT */
        phy_write(phydev, RTL8211F_REGPAGE, 0x0a43);    // return to page 0xa43
        val = phy_read(phydev, RTL8211F_PHYCR2);
        phy_write(phydev, RTL8211F_PHYCR2, val & ~(1 << 0));// disable clock out
        phy_write(phydev, RTL8211F_REGPAGE, 0x0000);    // return to page 0

        phy_write(phydev, RTL8211F_REGPAGE, 0x0a43);    // return to page 0xa43
#ifdef RTL_SPRD_CLK_MODE
        phy_write(phydev, 0x19, 0x8eb);     // 125mhz clock, rxc ssc, clock ssc, and enable EEE
#else
        phy_write(phydev, 0x19, 0x803);     // 125mhz clock, no EEE, RXC clock enable, clock
#endif
#ifdef RTL_GREEN_MODE		
        phy_write(phydev, RTL8211F_REGPAGE, 0x0000);    // return to page 0
		phy_write(phydev, 31, 0x0a43); /* 3, hk test values */
		phy_write(phydev, 27, 0x8011); // I do it twice since not sure yet if it survives PHY reset
		phy_write(phydev, 28, 0x573f); // boosted perf about 2-3%
#endif 
		printk("am_rtl811f called phy reset\n");
        phy_write(phydev, RTL8211F_PHYCTRL, 0x9200);    // PHY reset
        msleep(10);		// calls for min 50msec 

#ifdef RTL_GREEN_MODE
		phy_write(phydev, 31, 0x0a43); /* 3, hk test values */
		phy_write(phydev, 27, 0x8011);
		phy_write(phydev, 28, 0x573f);
#endif
// can modify the last write, 0x00 disables EEE
        phy_write(phydev, RTL8211F_MMD_CTRL, 0x7);		// device 7
        phy_write(phydev, RTL8211F_MMD_DATA, 0x3c);		// address 0x3c
        phy_write(phydev, RTL8211F_MMD_CTRL, 0x4007);	// no post increment, reg 7 again
        phy_write(phydev, RTL8211F_MMD_DATA, 0x00);		
	return 0;
}

/* RTL8211F */
static struct phy_driver rtl8211e_driver = {
	.phy_id		= 0x001cc916,
	.name		= "RTL8211F Gigabit Ethernet",
	.phy_id_mask	= 0x001fffff,
	.features	= 	PHY_GBIT_FEATURES | SUPPORTED_Pause |
			  		SUPPORTED_Asym_Pause,// close 1000m speed
	.flags		= 	PHY_HAS_INTERRUPT | PHY_HAS_MAGICANEG,
	.config_aneg	= &genphy_config_aneg,
	.read_status	= &rtl8211e_read_status,
	.config_init	= &rtl8211e_config_init,
	.suspend		= genphy_suspend,
	.resume			= genphy_resume,
	.driver			= { .owner = THIS_MODULE,},
};

static int __init realtek_init(void)
{

	return phy_driver_register(&rtl8211e_driver);
}

static void __exit realtek_exit(void)
{
	phy_driver_unregister(&rtl8211e_driver);
}

module_init(realtek_init);
module_exit(realtek_exit);

static struct mdio_device_id __maybe_unused realtek_tbl[] = {
	{ 0x001cc916, 0x001fffff },
	{ }
};

MODULE_DEVICE_TABLE(mdio, realtek_tbl);
