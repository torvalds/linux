
#include <linux/phy.h>
#include <linux/module.h>
#include <linux/delay.h>

#define RTL821x_PHYSR		0x11
#define RTL821x_PHYSR_DUPLEX	0x2000
#define RTL821x_PHYSR_SPEED	0xc000
#define RTL821x_INER		0x12
#define RTL821x_INER_INIT	0x6400
#define RTL821x_INSR		0x13
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
#if 0
static int rtl821x_ack_interrupt(struct phy_device *phydev)
{
	int err;

	err = phy_read(phydev, RTL821x_INSR);

	return (err < 0) ? err : 0;
}
static int rtl8211e_config_intr(struct phy_device *phydev)
{
	int err;

	if (phydev->interrupts == PHY_INTERRUPT_ENABLED)
		err = phy_write(phydev, RTL821x_INER,
				RTL8211E_INER_LINK_STAT);
	else
		err = phy_write(phydev, RTL821x_INER, 0);

	return err;
}
#endif
static int rtl8211e_config_init(struct phy_device *phydev)
{
	int val;

        /* Disable CLK_OUT */
        phy_write(phydev, RTL8211F_REGPAGE, 0x0a43);    // return to page 0xa43
        val = phy_read(phydev, RTL8211F_PHYCR2);
        phy_write(phydev, RTL8211F_PHYCR2, val & ~(1 << 0));
        phy_write(phydev, RTL8211F_REGPAGE, 0x0000);    // return to page 0

        phy_write(phydev, RTL8211F_REGPAGE, 0x0a43);    // return to page 0xa43
        phy_write(phydev, 0x19, 0x8eb);     // 125mhz clock, rxc ssc, clock ssc, and enable EEE
		// if we don't enable EEE then it gets stuck requesting DHCP address and no lan
        phy_write(phydev, RTL8211F_REGPAGE, 0x0000);    // return to page 0
        /* Enable RXC SSC */
//        phy_write(phydev, RTL8211F_REGPAGE, 0x0c44);    // return to page 0xc44
//        phy_write(phydev, RTL8211F_RXCSSC, 0x5f00);     // enable RXC SSC
//        phy_write(phydev, RTL8211F_REGPAGE, 0x0000);    // return to page 0

        /* Enable System Clock SSC */
//        phy_write(phydev, RTL8211F_REGPAGE, 0x0c44);    // return to page 0xc44
//        phy_write(phydev, RTL8211F_SYSCLK_SSC, 0x4f00); // enable system clock SSC
//        phy_write(phydev, RTL8211F_REGPAGE, 0x0a43);    // return to page 0xa43
//        val = phy_read(phydev, RTL8211F_PHYCR2);
//        phy_write(phydev, RTL8211F_PHYCR2, val | (1 << 3));
//        phy_write(phydev, RTL8211F_REGPAGE, 0x0000);    // return to page 0
        /* PHY Reset */
		phy_write(phydev, 31, 0x0a43); /* 3, hk test values */
		phy_write(phydev, 27, 0x8011); // I do it twice since not sure yet if it survives PHY reset
		phy_write(phydev, 28, 0x573f); // boosted perf about 2-3%
//======= testing items that must be set before reset called to have effect =====
		val = phy_read(phydev, 0x04);
		phy_write(phydev, 0x04, val|(1<<11)|(1<<10));	// advert asymm pause and pause frames	

        phy_write(phydev, RTL8211F_PHYCTRL, 0x9200);    // PHY reset
        msleep(20); 

		val = phy_read(phydev, 0x09);
		phy_write(phydev, 0x09, val|(1<<9));	// advertise 1000base-T full duplex
		val = phy_read(phydev, 0x00);
		phy_write(phydev, 0x00, val|(1<<9));	// restart auto-neg

		phy_write(phydev, 31, 0x0a43); /* 3, hk test values */
		phy_write(phydev, 27, 0x8011);
		phy_write(phydev, 28, 0x573f);
		// more experimentation needed below to see if these are correct values
/* we want to disable eee */
// commenting the next 4 out stops net from working, so don't mess with them
        phy_write(phydev, RTL8211F_MMD_CTRL, 0x7);
        phy_write(phydev, RTL8211F_MMD_DATA, 0x3c);
        phy_write(phydev, RTL8211F_MMD_CTRL, 0x4007);
        phy_write(phydev, RTL8211F_MMD_DATA, 0x0);

        
/* disable 1000m adv*/
// no idea why he did this, commenting it out seems to change nothing on a broadcom switch
// would need to see if it helps on athero's switches
//	val = phy_read(phydev, 0x9);
//	phy_write(phydev, 0x9, val&(~(1<<9))); 
  /* rx reg 21 bit 3 tx reg 17 bit 8*/  
    /*    phy_write(phydev, 0x1f, 0xd08);
        val =  phy_read(phydev, 0x15);
        phy_write(phydev, 0x15,val| 1<<21);
*/
	return 0;
	/* Enable Auto Power Saving mode */
	
}
/* RTL8211F */
static struct phy_driver rtl8211e_driver = {
	.phy_id		= 0x001cc916,
	.name		= "RTL8211F Gigabit Ethernet",
	.phy_id_mask	= 0x001fffff,
#if 1
	.features	= PHY_GBIT_FEATURES | SUPPORTED_Pause |
			  SUPPORTED_Asym_Pause,// close 1000m speed
	.flags		= PHY_HAS_INTERRUPT | PHY_HAS_MAGICANEG,
#else
	.features	= PHY_BASIC_FEATURES | SUPPORTED_Pause |
			  SUPPORTED_Asym_Pause,
	.flags		= PHY_HAS_INTERRUPT | PHY_HAS_MAGICANEG,
#endif
	.config_aneg	= &genphy_config_aneg,
	.read_status	= &genphy_read_status,
	.config_init	= &rtl8211e_config_init,
//	.ack_interrupt	= &rtl821x_ack_interrupt,
//	.config_intr	= &rtl8211e_config_intr,
	.suspend	= genphy_suspend,
	.resume		= genphy_resume,
	.driver		= { .owner = THIS_MODULE,},
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
