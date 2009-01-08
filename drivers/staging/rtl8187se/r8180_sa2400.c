/*
   This files contains PHILIPS SA2400 radio frontend programming routines.

   This is part of rtl8180 OpenSource driver
   Copyright (C) Andrea Merello 2004-2005  <andreamrl@tiscali.it>
   Released under the terms of GPL (General Public Licence)

   Parts of this driver are based on the GPL part of the
   official realtek driver

   Parts of this driver are based on the rtl8180 driver skeleton
   from Patric Schenke & Andres Salomon

   Parts of this driver are based on the Intel Pro Wireless 2100 GPL driver.

   Code at http://che.ojctech.com/~dyoung/rtw/ has been useful to me to
   understand some things.

   Code from rtl8181 project has been useful to me to understand some things.

   We want to tanks the Authors of such projects and the Ndiswrapper
   project Authors.
*/


#include "r8180.h"
#include "r8180_hw.h"
#include "r8180_sa2400.h"


//#define DEBUG_SA2400

u32 sa2400_chan[] = {
	0x0,	//dummy channel 0
	0x00096c, //1
	0x080970, //2
	0x100974, //3
	0x180978, //4
	0x000980, //5
	0x080984, //6
	0x100988, //7
	0x18098c, //8
	0x000994, //9
	0x080998, //10
	0x10099c, //11
	0x1809a0, //12
	0x0009a8, //13
	0x0009b4, //14
};


void rf_stabilize(struct net_device *dev)
{
	force_pci_posting(dev);
	mdelay(3); //for now use a great value.. we may optimize in future
}


void write_sa2400(struct net_device *dev,u8 adr, u32 data)
{
//	struct r8180_priv *priv = ieee80211_priv(dev);
	u32 phy_config;

        // philips sa2400 expects 24 bits data

	/*if(adr == 4 && priv->digphy){
		phy_config=0x60000000;
	}else{
		phy_config=0xb0000000;
	}*/

	phy_config = 0xb0000000; // MAC will bang bits to the sa2400

	phy_config |= (((u32)(adr&0xf))<< 24);
	phy_config |= (data & 0xffffff);
	write_nic_dword(dev,PHY_CONFIG,phy_config);
#ifdef DEBUG_SA2400
	DMESG("Writing sa2400: %x (adr %x)",phy_config,adr);
#endif
	rf_stabilize(dev);
}



void sa2400_write_phy_antenna(struct net_device *dev,short ch)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	u8 ant;

	ant = SA2400_ANTENNA;
	if(priv->antb) /*default antenna is antenna B */
		ant |= BB_ANTENNA_B;
	if(ch == 14)
		ant |= BB_ANTATTEN_CHAN14;
	write_phy(dev,0x10,ant);
	//DMESG("BB antenna %x ",ant);
}


/* from the rtl8181 embedded driver */
short sa2400_rf_set_sens(struct net_device *dev, short sens)
{
	u8 finetune = 0;
	if ((sens > 85) || (sens < 54)) return -1;

	write_sa2400(dev,5,0x1dfb | (sens-54) << 15 |(finetune<<20));  // AGC	0xc9dfb

	return 0;
}


void sa2400_rf_set_chan(struct net_device *dev, short ch)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	u32 txpw = 0xff & priv->chtxpwr[ch];
	u32 chan = sa2400_chan[ch];

	write_sa2400(dev,7,txpw);
	//write_phy(dev,0x10,0xd1);
	sa2400_write_phy_antenna(dev,ch);
	write_sa2400(dev,0,chan);
	write_sa2400(dev,1,0xbb50);
	write_sa2400(dev,2,0x80);
	write_sa2400(dev,3,0);
}


void sa2400_rf_close(struct net_device *dev)
{
	write_sa2400(dev, 4, 0);
}


void sa2400_rf_init(struct net_device *dev)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	u32 anaparam;
	u8 firdac;

	write_nic_byte(dev,PHY_DELAY,0x6);	//this is general
	write_nic_byte(dev,CARRIER_SENSE_COUNTER,0x4c); //this is general

	/*these are philips sa2400 specific*/
	anaparam = read_nic_dword(dev,ANAPARAM);
	anaparam = anaparam &~ (1<<ANAPARAM_TXDACOFF_SHIFT);

	anaparam = anaparam &~ANAPARAM_PWR1_MASK;
	anaparam = anaparam &~ANAPARAM_PWR0_MASK;
	if(priv->digphy){
		anaparam |= (SA2400_DIG_ANAPARAM_PWR1_ON<<ANAPARAM_PWR1_SHIFT);
		anaparam |= (SA2400_ANAPARAM_PWR0_ON<<ANAPARAM_PWR0_SHIFT);
	}else{
		anaparam |= (SA2400_ANA_ANAPARAM_PWR1_ON<<ANAPARAM_PWR1_SHIFT);
	}

	rtl8180_set_anaparam(dev,anaparam);

	firdac = (priv->digphy) ? (1<<SA2400_REG4_FIRDAC_SHIFT) : 0;
	write_sa2400(dev,0,sa2400_chan[priv->chan]);
	write_sa2400(dev,1,0xbb50);
	write_sa2400(dev,2,0x80);
	write_sa2400(dev,3,0);
	write_sa2400(dev,4,0x19340 | firdac);
	write_sa2400(dev,5,0xc9dfb);  // AGC
	write_sa2400(dev,4,0x19348 | firdac);  //calibrates VCO

	if(priv->digphy)
		write_sa2400(dev,4,0x1938c); /*???*/

	write_sa2400(dev,4,0x19340 | firdac);

	write_sa2400(dev,0,sa2400_chan[priv->chan]);
	write_sa2400(dev,1,0xbb50);
	write_sa2400(dev,2,0x80);
	write_sa2400(dev,3,0);
	write_sa2400(dev,4,0x19344 | firdac); //calibrates filter

	/* new from rtl8180 embedded driver (rtl8181 project) */
	write_sa2400(dev,6,0x13ff | (1<<23)); // MANRX
	write_sa2400(dev,8,0); //VCO

	if(!priv->digphy)
	{
		rtl8180_set_anaparam(dev, anaparam | \
				     (1<<ANAPARAM_TXDACOFF_SHIFT));

		rtl8180_conttx_enable(dev);

		write_sa2400(dev, 4, 0x19341); // calibrates DC

		/* a 5us sleep is required here,
		   we rely on the 3ms delay introduced in write_sa2400
		*/
		write_sa2400(dev, 4, 0x19345);
		/* a 20us sleep is required here,
		   we rely on the 3ms delay introduced in write_sa2400
		*/
		rtl8180_conttx_disable(dev);

		rtl8180_set_anaparam(dev, anaparam);
	}
	/* end new */

	write_sa2400(dev,4,0x19341 | firdac ); //RTX MODE

	// Set tx power level !?


	/*baseband configuration*/
	write_phy(dev,0,0x98);
	write_phy(dev,3,0x38);
	write_phy(dev,4,0xe0);
	write_phy(dev,5,0x90);
	write_phy(dev,6,0x1a);
	write_phy(dev,7,0x64);

	/*Should be done something more here??*/

	sa2400_write_phy_antenna(dev,priv->chan);

	write_phy(dev,0x11,0x80);
	if(priv->diversity)
		write_phy(dev,0x12,0xc7);
	else
		write_phy(dev,0x12,0x47);

	write_phy(dev,0x13,0x90 | priv->cs_treshold );

	write_phy(dev,0x19,0x0);
	write_phy(dev,0x1a,0xa0);

	sa2400_rf_set_chan(dev,priv->chan);
}
