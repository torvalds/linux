/*
   This files contains GCT radio frontend programming routines.

   This is part of rtl8180 OpenSource driver
   Copyright (C) Andrea Merello 2004-2005  <andreamrl@tiscali.it>
   Released under the terms of GPL (General Public Licence)

   Parts of this driver are based on the GPL part of the
   official realtek driver

   Parts of this driver are based on the rtl8180 driver skeleton
   from Patric Schenke & Andres Salomon

   Parts of this driver are based on the Intel Pro Wireless 2100 GPL driver.

   Code from Rtw8180 NetBSD driver by David Young has been really useful to
   understand some things and gets some ideas

   Code from rtl8181 project has been useful to me to understand some things.

   Some code from 'Deuce' work

   We want to tanks the Authors of such projects and the Ndiswrapper
   project Authors.
*/


#include "r8180.h"
#include "r8180_hw.h"
#include "r8180_gct.h"


//#define DEBUG_GCT

/* the following experiment are just experiments.
 * this means if you enable them you can have every kind
 * of result, included damage the RF chip, so don't
 * touch them if you don't know what you are doing.
 * In any case, if you do it, do at your own risk
 */

//#define GCT_EXPERIMENT1  //improve RX sensivity

//#define GCT_EXPERIMENT2

//#define GCT_EXPERIMENT3  //iprove a bit RX signal quality ?

//#define GCT_EXPERIMENT4 //maybe solve some brokeness with experiment1 ?

//#define GCT_EXPERIMENT5

//#define GCT_EXPERIMENT6  //not good


u32 gct_chan[] = {
	0x0,	//dummy channel 0
	0x0, //1
	0x1, //2
	0x2, //3
	0x3, //4
	0x4, //5
	0x5, //6
	0x6, //7
	0x7, //8
	0x8, //9
	0x9, //10
	0xa, //11
	0xb, //12
	0xc, //13
	0xd, //14
};

int gct_encode[16] = {
	0, 8, 4, 0xC,
	2, 0xA, 6, 0xE,
	1, 9, 5, 0xD,
	3, 0xB, 7, 0xF
};

void gct_rf_stabilize(struct net_device *dev)
{
	force_pci_posting(dev);
	mdelay(3); //for now use a great value.. we may optimize in future
}


void write_gct(struct net_device *dev, u8 adr, u32 data)
{
//	struct r8180_priv *priv = ieee80211_priv(dev);
	u32 phy_config;

	phy_config =  gct_encode[(data & 0xf00) >> 8];
	phy_config |= gct_encode[(data & 0xf0) >> 4 ] << 4;
	phy_config |= gct_encode[(data & 0xf)       ] << 8;
	phy_config |= gct_encode[(adr >> 1) & 0xf   ] << 12;
	phy_config |=            (adr & 1 )           << 16;
	phy_config |= gct_encode[(data & 0xf000)>>12] << 24;

	phy_config |= 0x90000000; // MAC will bang bits to the chip


	write_nic_dword(dev,PHY_CONFIG,phy_config);
#ifdef DEBUG_GCT
	DMESG("Writing GCT: %x (adr %x)",phy_config,adr);
#endif
	gct_rf_stabilize(dev);
}



void gct_write_phy_antenna(struct net_device *dev,short ch)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	u8 ant;

	ant = GCT_ANTENNA;
	if(priv->antb) /*default antenna is antenna B */
		ant |= BB_ANTENNA_B;
	if(ch == 14)
		ant |= BB_ANTATTEN_CHAN14;
	write_phy(dev,0x10,ant);
	//DMESG("BB antenna %x ",ant);
}


void gct_rf_set_chan(struct net_device *dev, short ch)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	u32 txpw = 0xff & priv->chtxpwr[ch];
	u32 chan = gct_chan[ch];

	//write_phy(dev,3,txpw);
#ifdef DEBUG_GCT
	DMESG("Gct set channel");
#endif
	/* set TX power */
	write_gct(dev,0x15,0);
 	write_gct(dev,6, txpw);
	write_gct(dev,0x15, 0x10);
	write_gct(dev,0x15,0);

	/*set frequency*/
	write_gct(dev,7, 0);
      	write_gct(dev,0xB, chan);
      	write_gct(dev,7, 0x1000);

#ifdef DEBUG_GCT
	DMESG("Gct set channel > write phy antenna");
#endif


	gct_write_phy_antenna(dev,ch);

}


void gct_rf_close(struct net_device *dev)
{
	u32 anaparam;

	anaparam = read_nic_dword(dev,ANAPARAM);
	anaparam &= 0x000fffff;
	anaparam |= 0x3f900000;
	rtl8180_set_anaparam(dev, anaparam);

	write_gct(dev, 0x7, 0);
	write_gct(dev, 0x1f, 0x45);
	write_gct(dev, 0x1f, 0x5);
	write_gct(dev, 0x0, 0x8e4);
}


void gct_rf_init(struct net_device *dev)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	//u32 anaparam;


	write_nic_byte(dev,PHY_DELAY,0x6);	//this is general
	write_nic_byte(dev,CARRIER_SENSE_COUNTER,0x4c); //this is general

	//DMESG("%x", read_nic_dword(dev,ANAPARAM));
	/* we should set anaparm here*/
	//rtl8180_set_anaparam(dev,anaparam);

	write_gct(dev,0x1f,0);
	write_gct(dev,0x1f,0);
	write_gct(dev,0x1f,0x40);
	write_gct(dev,0x1f,0x60);
	write_gct(dev,0x1f,0x61);
	write_gct(dev,0x1f,0x61);
	write_gct(dev,0x0,0xae4);
	write_gct(dev,0x1f,0x1);
	write_gct(dev,0x1f,0x41);
	write_gct(dev,0x1f,0x61);
	write_gct(dev,0x1,0x1a23);
	write_gct(dev,0x2,0x4971);
	write_gct(dev,0x3,0x41de);
	write_gct(dev,0x4,0x2d80);
#ifdef GCT_EXPERIMENT1
	//write_gct(dev,0x5,0x6810);  // from zydas driver. sens+ but quite slow
	//write_gct(dev,0x5,0x681f);  //good+ (somewhat stable, better sens, performance decent)
	write_gct(dev,0x5,0x685f);  //good performances, not sure sens is really so beeter
	//write_gct(dev,0x5,0x687f);  //good performances, maybe sens is not improved
	//write_gct(dev,0x5,0x689f);  //like above
	//write_gct(dev,0x5,0x685e);  //bad
	//write_gct(dev,0x5,0x68ff);  //good+ (somewhat stable, better sens(?), performance decent)
	//write_gct(dev,0x5,0x68f0);  //bad
	//write_gct(dev,0x5,0x6cff);  //sens+ but not so good
	//write_gct(dev,0x5,0x6dff);  //sens+,apparentely very good but broken
	//write_gct(dev,0x5,0x65ff);  //sens+,good
	//write_gct(dev,0x5,0x78ff);  //sens + but almost broken
	//write_gct(dev,0x5,0x7810);  //- //snes + but broken
	//write_gct(dev,0x5,0x781f);  //-- //sens +
	//write_gct(dev,0x5,0x78f0);  //low sens
#else
	write_gct(dev,0x5,0x61ff);   //best performance but weak sensitivity
#endif
#ifdef GCT_EXPERIMENT2
	write_gct(dev,0x6,0xe);
#else
	write_gct(dev,0x6,0x0);
#endif
	write_gct(dev,0x7,0x0);
	write_gct(dev,0x8,0x7533);
	write_gct(dev,0x9,0xc401);
	write_gct(dev,0xa,0x0);
	write_gct(dev,0xc,0x1c7);
	write_gct(dev,0xd,0x29d3);
	write_gct(dev,0xe,0x2e8);
	write_gct(dev,0x10,0x192);
#ifdef GCT_EXPERIMENT3
	write_gct(dev,0x11,0x246);
#else
	write_gct(dev,0x11,0x248);
#endif
	write_gct(dev,0x12,0x0);
	write_gct(dev,0x13,0x20c4);
#ifdef GCT_EXPERIMENT4
	write_gct(dev,0x14,0xf488);
#else
	write_gct(dev,0x14,0xf4fc);
#endif
#ifdef GCT_EXPERIMENT5
	write_gct(dev,0x15,0xb152);
#else
	write_gct(dev,0x15,0x0);
#endif
#ifdef GCT_EXPERIMENT6
	write_gct(dev,0x1e,0x1);
#endif
	write_gct(dev,0x16,0x1500);

	write_gct(dev,0x7,0x1000);
	/*write_gct(dev,0x15,0x0);
	write_gct(dev,0x6,0x15);
	write_gct(dev,0x15,0x8);
	write_gct(dev,0x15,0x0);
*/
	write_phy(dev,0,0xa8);

/*	write_gct(dev,0x15,0x0);
	write_gct(dev,0x6,0x12);
	write_gct(dev,0x15,0x8);
	write_gct(dev,0x15,0x0);
*/
	write_phy(dev,3,0x0);
	write_phy(dev,4,0xc0); /* lna det*/
	write_phy(dev,5,0x90);
	write_phy(dev,6,0x1e);
	write_phy(dev,7,0x64);

#ifdef DEBUG_GCT
	DMESG("Gct init> write phy antenna");
#endif

	gct_write_phy_antenna(dev,priv->chan);

	write_phy(dev,0x11,0x88);
	if(!priv->diversity)
		write_phy(dev,0x12,0xc0);
	else
		write_phy(dev,0x12,0x40);

	write_phy(dev,0x13,0x90 | priv->cs_treshold );

	write_phy(dev,0x19,0x0);
	write_phy(dev,0x1a,0xa0);
	write_phy(dev,0x1b,0x44);

#ifdef DEBUG_GCT
	DMESG("Gct init > set channel2");
#endif

	gct_rf_set_chan(dev,priv->chan);
}
