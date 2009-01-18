/*
   This files contains MAXIM MAX2820 radio frontend programming routines.

   This is part of rtl8180 OpenSource driver
   Copyright (C) Andrea Merello 2004-2005  <andreamrl@tiscali.it>
   Released under the terms of GPL (General Public Licence)

   Parts of this driver are based on the GPL part of the
   official realtek driver

   Parts of this driver are based on the rtl8180 driver skeleton
   from Patric Schenke & Andres Salomon

   Parts of this driver are based on the Intel Pro Wireless 2100 GPL driver.

   NetBSD rtl8180 driver from Dave Young has been really useful to
   understand how to program the MAXIM radio. Thanks a lot!!!

   'The Deuce' tested this and fixed some bugs.

   Code from rtl8181 project has been useful to me to understand some things.

   We want to tanks the Authors of such projects and the Ndiswrapper
   project Authors.
*/


#include "r8180.h"
#include "r8180_hw.h"
#include "r8180_max2820.h"


//#define DEBUG_MAXIM

u32 maxim_chan[] = {
	0,	//dummy channel 0
	12, //1
	17, //2
	22, //3
	27, //4
	32, //5
	37, //6
	42, //7
	47, //8
	52, //9
	57, //10
	62, //11
	67, //12
	72, //13
	84, //14
};

#if 0
/* maxim expects 4 bit address MSF, then 12 bit data MSF*/
void write_maxim(struct net_device *dev,u8 adr, u32 data)
{

	int shift;
	short bit;
	u16 word;

	adr = adr &0xf;
	word = (u16)data & 0xfff;
	word |= (adr<<12);
	/*write_nic_dword(dev,PHY_CONFIG,BB_HOST_BANG | BB_HOST_BANG_EN);
	read_nic_dword(dev,PHY_CONFIG);
	mdelay(1);

	write_nic_dword(dev,PHY_CONFIG,BB_HOST_BANG | BB_HOST_BANG_EN | BB_HOST_BANG_CLK);
	read_nic_dword(dev,PHY_CONFIG);
	mdelay(1);
	*/

	/* MAX2820 will sample data on rising edge of clock */
	for(shift = 15;shift >=0; shift--){
		bit = word>>shift & 1;

		write_nic_dword(dev,PHY_CONFIG,BB_HOST_BANG | (bit<<BB_HOST_BANG_DATA));

		read_nic_dword(dev,PHY_CONFIG);
		mdelay(2);

		write_nic_dword(dev,PHY_CONFIG,BB_HOST_BANG |
		(bit<<BB_HOST_BANG_DATA) | BB_HOST_BANG_CLK); /* sample data */

		read_nic_dword(dev,PHY_CONFIG);
		mdelay(1);

		write_nic_dword(dev,PHY_CONFIG,BB_HOST_BANG |
		(bit<<BB_HOST_BANG_DATA));

		read_nic_dword(dev,PHY_CONFIG);
		mdelay(2);

	}
	write_nic_dword(dev,PHY_CONFIG,BB_HOST_BANG | (bit<<BB_HOST_BANG_DATA)|
					BB_HOST_BANG_EN);
	read_nic_dword(dev,PHY_CONFIG);
	mdelay(2);

	/* The shift register fill flush to the requested register the
	 * last 12 bits data shifted in
	 */
	write_nic_dword(dev,PHY_CONFIG,BB_HOST_BANG | (bit<<BB_HOST_BANG_DATA)|
					BB_HOST_BANG_EN | BB_HOST_BANG_CLK);
	read_nic_dword(dev,PHY_CONFIG);
	mdelay(2);

	write_nic_dword(dev,PHY_CONFIG,BB_HOST_BANG | (bit<<BB_HOST_BANG_DATA)|
					BB_HOST_BANG_EN);
	read_nic_dword(dev,PHY_CONFIG);
	mdelay(2);


#ifdef DEBUG_MAXIM
	DMESG("Writing maxim: %x (adr %x)",phy_config,adr);
#endif

}
#endif

void write_maxim(struct net_device *dev,u8 adr, u32 data) {
	u32 temp;
	temp =  0x90 + (data & 0xf);
	temp <<= 16;
	temp += adr;
	temp <<= 8;
	temp += (data >> 4) & 0xff;
#ifdef DEBUG_MAXIM
	DMESG("write_maxim: %08x", temp);
#endif
	write_nic_dword(dev, PHY_CONFIG, temp);
	force_pci_posting(dev);
	mdelay(1);
}


void maxim_write_phy_antenna(struct net_device *dev,short ch)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	u8 ant;

	ant = MAXIM_ANTENNA;
	if(priv->antb) /*default antenna is antenna B */
		ant |= BB_ANTENNA_B;
	if(ch == 14)
		ant |= BB_ANTATTEN_CHAN14;
	write_phy(dev,0x10,ant);
	//DMESG("BB antenna %x ",ant);
}


void maxim_rf_set_chan(struct net_device *dev, short ch)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	u32 txpw = 0xff & priv->chtxpwr[ch];
	u32 chan = maxim_chan[ch];

	/*While philips SA2400 drive the PA bias
	 *seems that for MAXIM we delegate this
	 *to the BB
	 */

	//write_maxim(dev,5,txpw);
	write_phy(dev,3,txpw);

	maxim_write_phy_antenna(dev,ch);
	write_maxim(dev,3,chan);
}


void maxim_rf_close(struct net_device *dev)
{
	write_phy(dev, 3, 0x8);
	write_maxim(dev, 1, 0);
}


void maxim_rf_init(struct net_device *dev)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	u32 anaparam;

	write_nic_byte(dev,PHY_DELAY,0x6);	//this is general
	write_nic_byte(dev,CARRIER_SENSE_COUNTER,0x4c); //this is general

	/*these are maxim specific*/
	anaparam = read_nic_dword(dev,ANAPARAM);
	anaparam = anaparam &~ (ANAPARAM_TXDACOFF_SHIFT);
	anaparam = anaparam &~ANAPARAM_PWR1_MASK;
	anaparam = anaparam &~ANAPARAM_PWR0_MASK;
	anaparam |= (MAXIM_ANAPARAM_PWR1_ON<<ANAPARAM_PWR1_SHIFT);
	anaparam |= (MAXIM_ANAPARAM_PWR0_ON<<ANAPARAM_PWR0_SHIFT);

	//rtl8180_set_anaparam(dev,anaparam);

	/* MAXIM from netbsd driver */

	write_maxim(dev,0, 7); /* test mode as indicated in datasheet*/
	write_maxim(dev,1, 0x1e); /* enable register*/
	write_maxim(dev,2, 1); /* synt register */


	maxim_rf_set_chan(dev,priv->chan);

	write_maxim(dev,4, 0x313); /* rx register*/

	/* PA is driven directly by the BB, we keep the MAXIM bias
	 * at the highest value in the boubt tha pleacing it to lower
	 * values may introduce some further attenuation somewhere..
	 */

	write_maxim(dev,5, 0xf);


	/*baseband configuration*/
	write_phy(dev,0,0x88); //sys1
	write_phy(dev,3,0x8); //txagc
	write_phy(dev,4,0xf8); // lnadet
	write_phy(dev,5,0x90); // ifagcinit
	write_phy(dev,6,0x1a); // ifagclimit
	write_phy(dev,7,0x64); // ifagcdet

	/*Should be done something more here??*/

	maxim_write_phy_antenna(dev,priv->chan);

	write_phy(dev,0x11,0x88); //trl
	if(priv->diversity)
		write_phy(dev,0x12,0xc7);
	else
		write_phy(dev,0x12,0x47);

	write_phy(dev,0x13,0x9b);

	write_phy(dev,0x19,0x0); //CHESTLIM
	write_phy(dev,0x1a,0x9f); //CHSQLIM

	maxim_rf_set_chan(dev,priv->chan);
}
