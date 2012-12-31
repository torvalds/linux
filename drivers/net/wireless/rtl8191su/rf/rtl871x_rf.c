/******************************************************************************
 *
 * Copyright(c) 2007 - 2010 Realtek Corporation. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110, USA
 *
 *
 ******************************************************************************/ 
#define _RTL871X_RF_C_

#include <drv_conf.h>
#include <osdep_service.h>
#include <drv_types.h>
#include <recv_osdep.h>
#include <xmit_osdep.h>

#define _A_BAND		BIT(1)

void init_phyinfo(_adapter  *adapter, struct setphyinfo_parm* psetphyinfopara);

#define channel2freq(starting_freq, channel) \
	((starting_freq + (5 * channel)))

u32 ch2freq(u32 ch)
{
	u32 starting_freq;
	
	if (ch <= 14)
		starting_freq = 2412;
		
	else if (ch >= 180)
		starting_freq = 4000;
	
	else 
		starting_freq = 5000;
		
	return channel2freq(starting_freq, ch);
		
}

u32 freq2ch(u32 freq)
{
	u32 starting_freq;
	
	if (freq > 5000)
	
		starting_freq = 5000;
	
	else if (freq > 4000)
			
		starting_freq = 4000;
		
	else
	{
		starting_freq = 2412;
		return ((freq - starting_freq)/5 + 1);
	}	
		
	return ((freq - starting_freq)/5);

}


void set_channelset_a(_adapter  *padapter, struct regulatory_class *reg_class, u8 index, u8 channel_set)
{

	struct eeprom_priv* peeprompriv = &padapter->eeprompriv;

	reg_class->channel_set[index] = channel_set;
	
	reg_class->modem = OFDM_PHY;	

	reg_class->channel_ofdm_power[index] = peeprompriv->tx_power_a[channel_set];
	
	
}


void set_channelset_bg(_adapter  *padapter, struct regulatory_class *reg_class, u8 index, u8 channel_set)
{
	struct eeprom_priv* peeprompriv = &padapter->eeprompriv;

	reg_class->channel_set[index] = channel_set;
	
	reg_class->modem = MIXED_PHY;	

	switch(channel_set)
	{	
		case 1:
		case 2:
		case 3:
			reg_class->channel_cck_power[index] = peeprompriv->tx_power_b[1];					
			reg_class->channel_ofdm_power[index] = peeprompriv->tx_power_g[1];
			break;
		case 4:
		case 5:
		case 6:
		case 7:
		case 8:
			reg_class->channel_cck_power[index] =   peeprompriv->tx_power_b[6];					
			reg_class->channel_ofdm_power[index] = peeprompriv->tx_power_g[6];
			break;
		case 9:
		case 10:
		case 11:
		case 12:
		case 13:
			reg_class->channel_cck_power[index] = peeprompriv->tx_power_b[11];					
			reg_class->channel_ofdm_power[index] = peeprompriv->tx_power_g[11];
			break;
		case 14:
			reg_class->channel_cck_power[index] = peeprompriv->tx_power_b[14];					
			reg_class->channel_ofdm_power[index] = peeprompriv->tx_power_g[14];
			break;
		default:
			break;
			
	}
	
}

		

/*! \init_phyinfo:
	Init data for country information element and regulatory classes.

	If you need to add the additional countries/regions:
	1.) Please add the case in the switch-case options for each necessary country/region.
	2.) For each country/region, you can add 1~NUM_REGULATORYS regulatory class(es).
	     (Driver's NUM_REGULATORYS must have the same value with FW corres. value)
	3.) For each regulatory class, you need to supply these items:
		- Channel starting frequency (MHz):
		   psetphyinfopara->class_sets[class_index].starting_freq
		- Channel sets:
		   For each channel set, please call ->
		   a.) 802.11b/g 
			 set_channelset_bg(adapter, &psetphyinfopara->class_sets[class_index], channel_set_index, channel_set);
		   b.) 802.11a
			 set_channelset_a(adapter, &psetphyinfopara->class_sets[class_index], channel_set_index, channel_set);			 
  		- Channel spacing (MHz):  
         	   psetphyinfopara->class_sets[class_index].channel_spacing
         	- Transmit power limit (dBm):   
  		   psetphyinfopara->class_sets[class_index].txpower_limit
*/     	   	  
void init_phyinfo(_adapter  *adapter, struct setphyinfo_parm* psetphyinfopara)
{

	struct eeprom_priv* peeprompriv = &adapter->eeprompriv;
	unsigned long country_string = (unsigned long)(peeprompriv->country_string[0])<<16 | (unsigned long)(peeprompriv->country_string[1])<<8 | (peeprompriv->country_string[2]);

	switch(country_string)
	{
	
		case USA:

			/***** Regulatory domain for 802.11b/g *****/

			//Regulatory domain in 802.11 b/g -> class_sets[0]
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 0, 1);
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 1, 2);
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 2, 3);			
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 3, 4);		
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 4, 5);		
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 5, 6);
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 6, 7);
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 7, 8);			
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 8, 9);		
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 9, 10);					
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 10, 11);


			/***** Regulatory domain for 802.11A *****/
			if(peeprompriv->sys_config & _A_BAND) //if A band exists in EEPROM setting
			{
			//Regulatory class 1-> class_sets[1]
			psetphyinfopara->class_sets[1].starting_freq = 5000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[1], 0, 36);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[1], 1, 40);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[1], 2, 44);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[1], 3, 48);		
			psetphyinfopara->class_sets[1].channel_spacing = 20;
			psetphyinfopara->class_sets[1].txpower_limit = 16;

			//Regulatory class 2 -> class_sets[2]
			psetphyinfopara->class_sets[2].starting_freq = 5000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[2], 0, 52);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[2], 1, 56);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[2], 2, 60);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[2], 3, 64);		
			psetphyinfopara->class_sets[2].channel_spacing = 20;
			psetphyinfopara->class_sets[2].txpower_limit = 23;

			//Regulatory class 3 -> class_sets[3]
			psetphyinfopara->class_sets[3].starting_freq = 5000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[3], 0, 149);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[3], 1, 153);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[3], 2, 157);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[3], 3, 161);		
			psetphyinfopara->class_sets[3].channel_spacing = 20;
			psetphyinfopara->class_sets[3].txpower_limit = 29;			
			}
				
			break;
			
		case EUROPE:
			
			/***** Regulatory domain for 802.11b/g *****/

			//Regulatory domain in 802.11 b/g -> class_sets[0]
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 0, 1);
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 1, 2);
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 2, 3);			
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 3, 4);		
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 4, 5);		
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 5, 6);
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 6, 7);
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 7, 8);			
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 8, 9);		
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 9, 10);					
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 10, 11);
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 11, 12);
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 12, 13);			


			/***** Regulatory domain for 802.11a *****/
		
			if(peeprompriv->sys_config & _A_BAND) //if A band exists in EEPROM setting
			{
			//Regulatory class 1-> class_sets[1]
			psetphyinfopara->class_sets[1].starting_freq = 5000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[1], 0, 36);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[1], 1, 40);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[1], 2, 44);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[1], 3, 48);		
			psetphyinfopara->class_sets[1].channel_spacing = 20;
			psetphyinfopara->class_sets[1].txpower_limit = 23;

			//Regulatory class 2 -> class_sets[2]
			psetphyinfopara->class_sets[2].starting_freq = 5000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[2], 0, 52);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[2], 1, 56);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[2], 2, 60);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[2], 3, 64);		
			psetphyinfopara->class_sets[2].channel_spacing = 20;
			psetphyinfopara->class_sets[2].txpower_limit = 23;

			//Regulatory class 3 -> class_sets[3]
			psetphyinfopara->class_sets[3].starting_freq = 5000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[3], 0, 100);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[3], 1, 104);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[3], 2, 108);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[3], 3, 112);		
			set_channelset_a(adapter, &psetphyinfopara->class_sets[3], 4, 116);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[3], 5, 120);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[3], 6, 124);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[3], 7, 128);	
			set_channelset_a(adapter, &psetphyinfopara->class_sets[3], 8, 132);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[3], 9, 136);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[3], 10, 140);			
			psetphyinfopara->class_sets[3].channel_spacing = 20;
			psetphyinfopara->class_sets[3].txpower_limit = 30;			
			}	
			
			break;
			
		case JAPAN:
			
			/***** Regulatory domain for 802.11b/g *****/

			//Regulatory domain in 802.11 b/g -> class_sets[0]
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 0, 1);
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 1, 2);
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 2, 3);			
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 3, 4);		
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 4, 5);		
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 5, 6);
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 6, 7);
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 7, 8);			
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 8, 9);		
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 9, 10);					
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 10, 11);
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 11, 12);
			set_channelset_bg(adapter, &psetphyinfopara->class_sets[0], 12, 13);			


			/***** Regulatory domain for 802.11a *****/
			if(peeprompriv->sys_config & _A_BAND) //if A band exists in EEPROM setting
			{		
			//Regulatory class 1-> class_sets[1]
			psetphyinfopara->class_sets[1].starting_freq = 5000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[1], 0, 34);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[1], 1, 38);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[1], 2, 42);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[1], 3, 46);		
			psetphyinfopara->class_sets[1].channel_spacing = 20;
			psetphyinfopara->class_sets[1].txpower_limit = 22;

			//Regulatory class 2 -> class_sets[2]
			psetphyinfopara->class_sets[2].starting_freq = 5000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[2], 0, 8);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[2], 1, 12);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[2], 2, 16);			
			psetphyinfopara->class_sets[2].channel_spacing = 20;
			psetphyinfopara->class_sets[2].txpower_limit = 24;

			//Regulatory class 3 -> class_sets[3]
			psetphyinfopara->class_sets[3].starting_freq = 5000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[3], 0, 8);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[3], 1, 12);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[3], 2, 16);			
			psetphyinfopara->class_sets[3].channel_spacing = 20;
			psetphyinfopara->class_sets[3].txpower_limit = 24;			

			//Regulatory class 4 -> class_sets[4]
			psetphyinfopara->class_sets[4].starting_freq = 5000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[4], 0, 8);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[4], 1, 12);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[4], 2, 16);			
			psetphyinfopara->class_sets[4].channel_spacing = 20;
			psetphyinfopara->class_sets[4].txpower_limit = 24;				

			//Regulatory class 5 -> class_sets[5]
			psetphyinfopara->class_sets[5].starting_freq = 5000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[5], 0, 8);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[5], 1, 12);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[5], 2, 16);			
			psetphyinfopara->class_sets[5].channel_spacing = 20;
			psetphyinfopara->class_sets[5].txpower_limit = 24;

			//Regulatory class 6 -> class_sets[6]
			psetphyinfopara->class_sets[6].starting_freq = 5000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[6], 0, 8);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[6], 1, 12);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[6], 2, 16);			
			psetphyinfopara->class_sets[6].channel_spacing = 20;
			psetphyinfopara->class_sets[6].txpower_limit = 22;

			//Regulatory class 7 -> class_sets[7]
			psetphyinfopara->class_sets[7].starting_freq = 4000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[7], 0, 184);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[7], 1, 188);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[7], 2, 192);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[7], 3, 195);			
			psetphyinfopara->class_sets[7].channel_spacing = 20;
			psetphyinfopara->class_sets[7].txpower_limit = 24;

			//Regulatory class 8 -> class_sets[8]
			psetphyinfopara->class_sets[8].starting_freq = 4000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[8], 0, 184);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[8], 1, 188);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[8], 2, 192);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[8], 3, 196);			
			psetphyinfopara->class_sets[8].channel_spacing = 20;
			psetphyinfopara->class_sets[8].txpower_limit = 24;

			//Regulatory class 9 -> class_sets[9]
			psetphyinfopara->class_sets[9].starting_freq = 4000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[9], 0, 184);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[9], 1, 188);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[9], 2, 192);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[9], 3, 196);			
			psetphyinfopara->class_sets[9].channel_spacing = 20;
			psetphyinfopara->class_sets[9].txpower_limit = 24;

			//Regulatory class 10 -> class_sets[9]
			psetphyinfopara->class_sets[10].starting_freq = 4000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[10], 0, 184);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[10], 1, 188);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[10], 2, 192);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[10], 3, 196);			
			psetphyinfopara->class_sets[10].channel_spacing = 20;
			psetphyinfopara->class_sets[10].txpower_limit = 24;

			//Regulatory class 11 -> class_sets[11]
			psetphyinfopara->class_sets[11].starting_freq = 4000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[11], 0, 184);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[11], 1, 188);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[11], 2, 192);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[11], 3, 196);			
			psetphyinfopara->class_sets[11].channel_spacing = 20;
			psetphyinfopara->class_sets[11].txpower_limit = 22;

			//Regulatory class 12 -> class_sets[12]
			psetphyinfopara->class_sets[12].starting_freq = 5000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[12], 0, 7);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[12], 1, 8);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[12], 2, 9);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[12], 3, 11);			
			psetphyinfopara->class_sets[12].channel_spacing = 10;
			psetphyinfopara->class_sets[12].txpower_limit = 24;

			//Regulatory class 13 -> class_sets[13]
			psetphyinfopara->class_sets[13].starting_freq = 5000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[13], 0, 7);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[13], 1, 8);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[13], 2, 9);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[13], 3, 11);			
			psetphyinfopara->class_sets[13].channel_spacing = 10;
			psetphyinfopara->class_sets[13].txpower_limit = 24;

			//Regulatory class 14 -> class_sets[14]
			psetphyinfopara->class_sets[14].starting_freq = 5000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[14], 0, 7);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[14], 1, 8);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[14], 2, 9);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[14], 3, 11);			
			psetphyinfopara->class_sets[14].channel_spacing = 10;
			psetphyinfopara->class_sets[14].txpower_limit = 24;		
	
			//Regulatory class 15 -> class_sets[15]
			psetphyinfopara->class_sets[15].starting_freq = 5000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[15], 0, 7);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[15], 1, 8);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[15], 2, 9);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[15], 3, 11);			
			psetphyinfopara->class_sets[15].channel_spacing = 10;
			psetphyinfopara->class_sets[15].txpower_limit = 24;		

			//Regulatory class 16 -> class_sets[16]
			psetphyinfopara->class_sets[16].starting_freq = 4000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[16], 0, 183);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[16], 1, 184);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[16], 2, 185);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[16], 3, 187);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[16], 4, 188);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[16], 5, 189);			
			psetphyinfopara->class_sets[16].channel_spacing = 10;
			psetphyinfopara->class_sets[16].txpower_limit = 24;		

			//Regulatory class 17 -> class_sets[17]
			psetphyinfopara->class_sets[17].starting_freq = 4000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[17], 0, 183);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[17], 1, 184);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[17], 2, 185);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[17], 3, 187);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[17], 4, 188);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[17], 5, 189);			
			psetphyinfopara->class_sets[17].channel_spacing = 10;
			psetphyinfopara->class_sets[17].txpower_limit = 24;		

			//Regulatory class 18 -> class_sets[18]
			psetphyinfopara->class_sets[18].starting_freq = 4000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[18], 0, 183);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[18], 1, 184);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[18], 2, 185);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[18], 3, 187);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[18], 4, 188);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[18], 5, 189);			
			psetphyinfopara->class_sets[18].channel_spacing = 10;
			psetphyinfopara->class_sets[18].txpower_limit = 24;		

			//Regulatory class 19 -> class_sets[19]
			psetphyinfopara->class_sets[19].starting_freq = 4000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[19], 0, 183);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[19], 1, 184);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[19], 2, 185);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[19], 3, 187);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[19], 4, 188);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[19], 5, 189);			
			psetphyinfopara->class_sets[19].channel_spacing = 10;
			psetphyinfopara->class_sets[19].txpower_limit = 24;		

			//Regulatory class 20 -> class_sets[20]
			psetphyinfopara->class_sets[20].starting_freq = 4000; 
			set_channelset_a(adapter, &psetphyinfopara->class_sets[20], 0, 183);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[20], 1, 184);
			set_channelset_a(adapter, &psetphyinfopara->class_sets[20], 2, 185);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[20], 3, 187);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[20], 4, 188);			
			set_channelset_a(adapter, &psetphyinfopara->class_sets[20], 5, 189);			
			psetphyinfopara->class_sets[20].channel_spacing = 10;
			psetphyinfopara->class_sets[20].txpower_limit = 17;		
			}
			
			break;
			
		default:
		
			RT_TRACE(_module_hal_init_c_,_drv_err_,("Country string in EEPROM has not been defined."));
			break;
			
	}

}


u8 writephyinfo_fw(_adapter *padapter, u32 addr)
{
	u32	i;
	u32 *tmpWrite;
	struct setphyinfo_parm*	psetphyinfopara;

	psetphyinfopara = (struct setphyinfo_parm*)_malloc(sizeof(struct setphyinfo_parm)); 

	if(psetphyinfopara==NULL){
		return _FAIL;
	}

	_memset((unsigned char *)psetphyinfopara, 0, sizeof (struct setphyinfo_parm));
	
	init_phyinfo(padapter, psetphyinfopara);

	tmpWrite = (u32 *)psetphyinfopara;

	for(i = 0; i < sizeof(struct setphyinfo_parm); i = i + sizeof(u32)) 
	{
		write32(padapter, addr+i, *tmpWrite);
		tmpWrite++;
	}

	_mfree((unsigned char *) psetphyinfopara, sizeof(struct	setphyinfo_parm));
	
	return _SUCCESS;
}




