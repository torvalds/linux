/*++
Copyright (c) Realtek Semiconductor Corp. All rights reserved.

Module Name:
 	r8185b_init.c

Abstract:
 	Hardware Initialization and Hardware IO for RTL8185B

Major Change History:
	When        Who      What
	----------    ---------------   -------------------------------
	2006-11-15    Xiong		Created

Notes:
	This file is ported from RTL8185B Windows driver.


--*/

/*--------------------------Include File------------------------------------*/
#include <linux/spinlock.h>
#include "r8180_hw.h"
#include "r8180.h"
#include "r8180_sa2400.h"  /* PHILIPS Radio frontend */
#include "r8180_max2820.h" /* MAXIM Radio frontend */
#include "r8180_gct.h"     /* GCT Radio frontend */
#include "r8180_rtl8225.h" /* RTL8225 Radio frontend */
#include "r8180_rtl8255.h" /* RTL8255 Radio frontend */
#include "r8180_93cx6.h"   /* Card EEPROM */
#include "r8180_wx.h"

#ifdef CONFIG_RTL8180_PM
#include "r8180_pm.h"
#endif

#ifdef ENABLE_DOT11D
#include "dot11d.h"
#endif

#ifdef CONFIG_RTL8185B

//#define CONFIG_RTL8180_IO_MAP

#define TC_3W_POLL_MAX_TRY_CNT 5
#ifdef CONFIG_RTL818X_S
static u8 MAC_REG_TABLE[][2]={
                        //PAGA 0:
                        // 0x34(BRSR), 0xBE(RATE_FALLBACK_CTL), 0x1E0(ARFR) would set in HwConfigureRTL8185()
                        // 0x272(RFSW_CTRL), 0x1CE(AESMSK_QC) set in InitializeAdapter8185().
                        // 0x1F0~0x1F8  set in MacConfig_85BASIC()
                        {0x08, 0xae}, {0x0a, 0x72}, {0x5b, 0x42},
                        {0x84, 0x88}, {0x85, 0x24}, {0x88, 0x54}, {0x8b, 0xb8}, {0x8c, 0x03},
                        {0x8d, 0x40}, {0x8e, 0x00}, {0x8f, 0x00}, {0x5b, 0x18}, {0x91, 0x03},
                        {0x94, 0x0F}, {0x95, 0x32},
                        {0x96, 0x00}, {0x97, 0x07}, {0xb4, 0x22}, {0xdb, 0x00},
                        {0xf0, 0x32}, {0xf1, 0x32}, {0xf2, 0x00}, {0xf3, 0x00}, {0xf4, 0x32},
                        {0xf5, 0x43}, {0xf6, 0x00}, {0xf7, 0x00}, {0xf8, 0x46}, {0xf9, 0xa4},
                        {0xfa, 0x00}, {0xfb, 0x00}, {0xfc, 0x96}, {0xfd, 0xa4}, {0xfe, 0x00},
                        {0xff, 0x00},

                        //PAGE 1:
                        // For Flextronics system Logo PCIHCT failure:
			// 0x1C4~0x1CD set no-zero value to avoid PCI configuration space 0x45[7]=1
                        {0x5e, 0x01},
                        {0x58, 0x00}, {0x59, 0x00}, {0x5a, 0x04}, {0x5b, 0x00}, {0x60, 0x24},
                        {0x61, 0x97}, {0x62, 0xF0}, {0x63, 0x09}, {0x80, 0x0F}, {0x81, 0xFF},
                        {0x82, 0xFF}, {0x83, 0x03},
                        {0xC4, 0x22}, {0xC5, 0x22}, {0xC6, 0x22}, {0xC7, 0x22}, {0xC8, 0x22}, //lzm add 080826
			{0xC9, 0x22}, {0xCA, 0x22}, {0xCB, 0x22}, {0xCC, 0x22}, {0xCD, 0x22},//lzm add 080826
                        {0xe2, 0x00},


                        //PAGE 2:
                        {0x5e, 0x02},
                        {0x0c, 0x04}, {0x4c, 0x30}, {0x4d, 0x08}, {0x50, 0x05}, {0x51, 0xf5},
                        {0x52, 0x04}, {0x53, 0xa0}, {0x54, 0xff}, {0x55, 0xff}, {0x56, 0xff},
                        {0x57, 0xff}, {0x58, 0x08}, {0x59, 0x08}, {0x5a, 0x08}, {0x5b, 0x08},
                        {0x60, 0x08}, {0x61, 0x08}, {0x62, 0x08}, {0x63, 0x08}, {0x64, 0x2f},
                        {0x8c, 0x3f}, {0x8d, 0x3f}, {0x8e, 0x3f},
                        {0x8f, 0x3f}, {0xc4, 0xff}, {0xc5, 0xff}, {0xc6, 0xff}, {0xc7, 0xff},
                        {0xc8, 0x00}, {0xc9, 0x00}, {0xca, 0x80}, {0xcb, 0x00},

                        //PAGA 0:
                        {0x5e, 0x00},{0x9f, 0x03}
                };


static u8  ZEBRA_AGC[]={
			0,
			0x7E,0x7E,0x7E,0x7E,0x7D,0x7C,0x7B,0x7A,0x79,0x78,0x77,0x76,0x75,0x74,0x73,0x72,
			0x71,0x70,0x6F,0x6E,0x6D,0x6C,0x6B,0x6A,0x69,0x68,0x67,0x66,0x65,0x64,0x63,0x62,
			0x48,0x47,0x46,0x45,0x44,0x29,0x28,0x27,0x26,0x25,0x24,0x23,0x22,0x21,0x08,0x07,
			0x06,0x05,0x04,0x03,0x02,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
			0x0f,0x0f,0x0f,0x0f,0x0f,0x0f,0x0f,0x0f,0x0f,0x0f,0x10,0x11,0x12,0x13,0x15,0x16,
			0x17,0x17,0x18,0x18,0x19,0x1a,0x1a,0x1b,0x1b,0x1c,0x1c,0x1d,0x1d,0x1d,0x1e,0x1e,
			0x1f,0x1f,0x1f,0x20,0x20,0x20,0x20,0x21,0x21,0x21,0x22,0x22,0x22,0x23,0x23,0x24,
			0x24,0x25,0x25,0x25,0x26,0x26,0x27,0x27,0x2F,0x2F,0x2F,0x2F,0x2F,0x2F,0x2F,0x2F
			};

static u32 ZEBRA_RF_RX_GAIN_TABLE[]={
			0x0096,0x0076,0x0056,0x0036,0x0016,0x01f6,0x01d6,0x01b6,
			0x0196,0x0176,0x00F7,0x00D7,0x00B7,0x0097,0x0077,0x0057,
			0x0037,0x00FB,0x00DB,0x00BB,0x00FF,0x00E3,0x00C3,0x00A3,
			0x0083,0x0063,0x0043,0x0023,0x0003,0x01E3,0x01C3,0x01A3,
			0x0183,0x0163,0x0143,0x0123,0x0103
	};

static u8 OFDM_CONFIG[]={
			// OFDM reg0x06[7:0]=0xFF: Enable power saving mode in RX
			// OFDM reg0x3C[4]=1'b1: Enable RX power saving mode
			// ofdm 0x3a = 0x7b ,(original : 0xfb) For ECS shielding room TP test

			// 0x00
			0x10, 0x0F, 0x0A, 0x0C, 0x14, 0xFA, 0xFF, 0x50,
			0x00, 0x50, 0x00, 0x00, 0x00, 0x5C, 0x00, 0x00,
			// 0x10
			0x40, 0x00, 0x40, 0x00, 0x00, 0x00, 0xA8, 0x26,
			0x32, 0x33, 0x06, 0xA5, 0x6F, 0x55, 0xC8, 0xBB,
			// 0x20
			0x0A, 0xE1, 0x2C, 0x4A, 0x86, 0x83, 0x34, 0x00,
			0x4F, 0x24, 0x6F, 0xC2, 0x03, 0x40, 0x80, 0x00,
			// 0x30
			0xC0, 0xC1, 0x58, 0xF1, 0x00, 0xC4, 0x90, 0x3e,
			0xD8, 0x3C, 0x7B, 0x10, 0x10
		};
#else
 static u8 MAC_REG_TABLE[][2]={
			//PAGA 0:
			{0xf0, 0x32}, {0xf1, 0x32}, {0xf2, 0x00}, {0xf3, 0x00}, {0xf4, 0x32},
			{0xf5, 0x43}, {0xf6, 0x00}, {0xf7, 0x00}, {0xf8, 0x46}, {0xf9, 0xa4},
			{0xfa, 0x00}, {0xfb, 0x00}, {0xfc, 0x96}, {0xfd, 0xa4}, {0xfe, 0x00},
			{0xff, 0x00},

			//PAGE 1:
			{0x5e, 0x01},
			{0x58, 0x4b}, {0x59, 0x00}, {0x5a, 0x4b}, {0x5b, 0x00}, {0x60, 0x4b},
			{0x61, 0x09}, {0x62, 0x4b}, {0x63, 0x09}, {0xce, 0x0f}, {0xcf, 0x00},
			{0xe0, 0xff}, {0xe1, 0x0f}, {0xe2, 0x00}, {0xf0, 0x4e}, {0xf1, 0x01},
			{0xf2, 0x02}, {0xf3, 0x03}, {0xf4, 0x04}, {0xf5, 0x05}, {0xf6, 0x06},
			{0xf7, 0x07}, {0xf8, 0x08},


			//PAGE 2:
			{0x5e, 0x02},
			{0x0c, 0x04}, {0x21, 0x61}, {0x22, 0x68}, {0x23, 0x6f}, {0x24, 0x76},
			{0x25, 0x7d}, {0x26, 0x84}, {0x27, 0x8d}, {0x4d, 0x08}, {0x4e, 0x00},
			{0x50, 0x05}, {0x51, 0xf5}, {0x52, 0x04}, {0x53, 0xa0}, {0x54, 0x1f},
			{0x55, 0x23}, {0x56, 0x45}, {0x57, 0x67}, {0x58, 0x08}, {0x59, 0x08},
			{0x5a, 0x08}, {0x5b, 0x08}, {0x60, 0x08}, {0x61, 0x08}, {0x62, 0x08},
			{0x63, 0x08}, {0x64, 0xcf}, {0x72, 0x56}, {0x73, 0x9a},

			//PAGA 0:
			{0x5e, 0x00},
			{0x34, 0xff}, {0x35, 0x0f}, {0x5b, 0x40}, {0x84, 0x88}, {0x85, 0x24},
			{0x88, 0x54}, {0x8b, 0xb8}, {0x8c, 0x07}, {0x8d, 0x00}, {0x94, 0x1b},
			{0x95, 0x12}, {0x96, 0x00}, {0x97, 0x06}, {0x9d, 0x1a}, {0x9f, 0x10},
			{0xb4, 0x22}, {0xbe, 0x80}, {0xdb, 0x00}, {0xee, 0x00}, {0x5b, 0x42},
			{0x91, 0x03},

			//PAGE 2:
			{0x5e, 0x02},
			{0x4c, 0x03},

			//PAGE 0:
			{0x5e, 0x00},

			//PAGE 3:
			{0x5e, 0x03},
			{0x9f, 0x00},

			//PAGE 0:
			{0x5e, 0x00},
			{0x8c, 0x01}, {0x8d, 0x10},{0x8e, 0x08}, {0x8f, 0x00}
		};


static u8  ZEBRA_AGC[]={
	0,
	0x5e,0x5e,0x5e,0x5e,0x5d,0x5b,0x59,0x57,0x55,0x53,0x51,0x4f,0x4d,0x4b,0x49,0x47,
	0x45,0x43,0x41,0x3f,0x3d,0x3b,0x39,0x37,0x35,0x33,0x31,0x2f,0x2d,0x2b,0x29,0x27,
	0x25,0x23,0x21,0x1f,0x1d,0x1b,0x19,0x17,0x15,0x13,0x11,0x0f,0x0d,0x0b,0x09,0x07,
	0x05,0x03,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,0x01,
	0x19,0x19,0x19,0x019,0x19,0x19,0x19,0x19,0x19,0x19,0x1e,0x1f,0x20,0x21,0x21,0x22,
	0x23,0x24,0x24,0x25,0x25,0x26,0x26,0x27,0x27,0x28,0x28,0x28,0x29,0x2a,0x2a,0x2b,
	0x2b,0x2b,0x2c,0x2c,0x2c,0x2d,0x2d,0x2d,0x2e,0x2e,0x2f,0x30,0x31,0x31,0x31,0x31,
	0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31,0x31
	};

static u32 ZEBRA_RF_RX_GAIN_TABLE[]={
	0,
	0x0400,0x0401,0x0402,0x0403,0x0404,0x0405,0x0408,0x0409,
	0x040a,0x040b,0x0502,0x0503,0x0504,0x0505,0x0540,0x0541,
	0x0542,0x0543,0x0544,0x0545,0x0580,0x0581,0x0582,0x0583,
	0x0584,0x0585,0x0588,0x0589,0x058a,0x058b,0x0643,0x0644,
	0x0645,0x0680,0x0681,0x0682,0x0683,0x0684,0x0685,0x0688,
	0x0689,0x068a,0x068b,0x068c,0x0742,0x0743,0x0744,0x0745,
	0x0780,0x0781,0x0782,0x0783,0x0784,0x0785,0x0788,0x0789,
	0x078a,0x078b,0x078c,0x078d,0x0790,0x0791,0x0792,0x0793,
	0x0794,0x0795,0x0798,0x0799,0x079a,0x079b,0x079c,0x079d,
	0x07a0,0x07a1,0x07a2,0x07a3,0x07a4,0x07a5,0x07a8,0x07a9,
	0x03aa,0x03ab,0x03ac,0x03ad,0x03b0,0x03b1,0x03b2,0x03b3,
	0x03b4,0x03b5,0x03b8,0x03b9,0x03ba,0x03bb,0x03bb
};

// 2006.07.13, SD3 szuyitasi:
//	OFDM.0x03=0x0C (original is 0x0F)
// Use the new SD3 given param, by shien chang, 2006.07.14
static u8 OFDM_CONFIG[]={
	0x10, 0x0d, 0x01, 0x0C, 0x14, 0xfb, 0x0f, 0x60, 0x00, 0x60,
	0x00, 0x00, 0x00, 0x5c, 0x00, 0x00, 0x40, 0x00, 0x40, 0x00,
	0x00, 0x00, 0xa8, 0x46, 0xb2, 0x33, 0x07, 0xa5, 0x6f, 0x55,
	0xc8, 0xb3, 0x0a, 0xe1, 0x1c, 0x8a, 0xb6, 0x83, 0x34, 0x0f,
	0x4f, 0x23, 0x6f, 0xc2, 0x6b, 0x40, 0x80, 0x00, 0xc0, 0xc1,
	0x58, 0xf1, 0x00, 0xe4, 0x90, 0x3e, 0x6d, 0x3c, 0xff, 0x07
};
#endif

/*---------------------------------------------------------------
  * Hardware IO
  * the code is ported from Windows source code
  ----------------------------------------------------------------*/

void
PlatformIOWrite1Byte(
	struct net_device *dev,
	u32		offset,
	u8		data
	)
{
#ifndef CONFIG_RTL8180_IO_MAP
	write_nic_byte(dev, offset, data);
	read_nic_byte(dev, offset); // To make sure write operation is completed, 2005.11.09, by rcnjko.

#else // Port IO
	u32 Page = (offset >> 8);

	switch(Page)
	{
	case 0: // Page 0
		write_nic_byte(dev, offset, data);
		break;

	case 1: // Page 1
	case 2: // Page 2
	case 3: // Page 3
		{
			u8 psr = read_nic_byte(dev, PSR);

			write_nic_byte(dev, PSR, ((psr & 0xfc) | (u8)Page)); // Switch to page N.
			write_nic_byte(dev, (offset & 0xff), data);
			write_nic_byte(dev, PSR, (psr & 0xfc)); // Switch to page 0.
		}
		break;

	default:
		// Illegal page number.
		DMESGE("PlatformIOWrite1Byte(): illegal page number: %d, offset: %#X", Page, offset);
		break;
	}
#endif
}

void
PlatformIOWrite2Byte(
	struct net_device *dev,
	u32		offset,
	u16		data
	)
{
#ifndef CONFIG_RTL8180_IO_MAP
	write_nic_word(dev, offset, data);
	read_nic_word(dev, offset); // To make sure write operation is completed, 2005.11.09, by rcnjko.


#else // Port IO
	u32 Page = (offset >> 8);

	switch(Page)
	{
	case 0: // Page 0
		write_nic_word(dev, offset, data);
		break;

	case 1: // Page 1
	case 2: // Page 2
	case 3: // Page 3
		{
			u8 psr = read_nic_byte(dev, PSR);

			write_nic_byte(dev, PSR, ((psr & 0xfc) | (u8)Page)); // Switch to page N.
			write_nic_word(dev, (offset & 0xff), data);
			write_nic_byte(dev, PSR, (psr & 0xfc)); // Switch to page 0.
		}
		break;

	default:
		// Illegal page number.
		DMESGE("PlatformIOWrite2Byte(): illegal page number: %d, offset: %#X", Page, offset);
		break;
	}
#endif
}
u8 PlatformIORead1Byte(struct net_device *dev, u32 offset);

void
PlatformIOWrite4Byte(
	struct net_device *dev,
	u32		offset,
	u32		data
	)
{
#ifndef CONFIG_RTL8180_IO_MAP
//{by amy 080312
if (offset == PhyAddr)
	{//For Base Band configuration.
		unsigned char	cmdByte;
		unsigned long	dataBytes;
		unsigned char	idx;
		u8	u1bTmp;

		cmdByte = (u8)(data & 0x000000ff);
		dataBytes = data>>8;

		//
		// 071010, rcnjko:
		// The critical section is only BB read/write race condition.
		// Assumption:
		// 1. We assume NO one will access BB at DIRQL, otherwise, system will crash for
		// acquiring the spinlock in such context.
		// 2. PlatformIOWrite4Byte() MUST NOT be recursive.
		//
//		NdisAcquireSpinLock( &(pDevice->IoSpinLock) );

		for(idx = 0; idx < 30; idx++)
		{ // Make sure command bit is clear before access it.
			u1bTmp = PlatformIORead1Byte(dev, PhyAddr);
			if((u1bTmp & BIT7) == 0)
				break;
			else
				mdelay(10);
		}

		for(idx=0; idx < 3; idx++)
		{
			PlatformIOWrite1Byte(dev,offset+1+idx,((u8*)&dataBytes)[idx] );
		}
		write_nic_byte(dev, offset, cmdByte);

//		NdisReleaseSpinLock( &(pDevice->IoSpinLock) );
	}
//by amy 080312}
	else{
		write_nic_dword(dev, offset, data);
		read_nic_dword(dev, offset); // To make sure write operation is completed, 2005.11.09, by rcnjko.
	}
#else // Port IO
	u32 Page = (offset >> 8);

	switch(Page)
	{
	case 0: // Page 0
		write_nic_word(dev, offset, data);
		break;

	case 1: // Page 1
	case 2: // Page 2
	case 3: // Page 3
		{
			u8 psr = read_nic_byte(dev, PSR);

			write_nic_byte(dev, PSR, ((psr & 0xfc) | (u8)Page)); // Switch to page N.
			write_nic_dword(dev, (offset & 0xff), data);
			write_nic_byte(dev, PSR, (psr & 0xfc)); // Switch to page 0.
		}
		break;

	default:
		// Illegal page number.
		DMESGE("PlatformIOWrite4Byte(): illegal page number: %d, offset: %#X", Page, offset);
		break;
	}
#endif
}

u8
PlatformIORead1Byte(
	struct net_device *dev,
	u32		offset
	)
{
	u8	data = 0;

#ifndef CONFIG_RTL8180_IO_MAP
	data = read_nic_byte(dev, offset);

#else // Port IO
	u32 Page = (offset >> 8);

	switch(Page)
	{
	case 0: // Page 0
		data = read_nic_byte(dev, offset);
		break;

	case 1: // Page 1
	case 2: // Page 2
	case 3: // Page 3
		{
			u8 psr = read_nic_byte(dev, PSR);

			write_nic_byte(dev, PSR, ((psr & 0xfc) | (u8)Page)); // Switch to page N.
			data = read_nic_byte(dev, (offset & 0xff));
			write_nic_byte(dev, PSR, (psr & 0xfc)); // Switch to page 0.
		}
		break;

	default:
		// Illegal page number.
		DMESGE("PlatformIORead1Byte(): illegal page number: %d, offset: %#X", Page, offset);
		break;
	}
#endif

	return data;
}

u16
PlatformIORead2Byte(
	struct net_device *dev,
	u32		offset
	)
{
	u16	data = 0;

#ifndef CONFIG_RTL8180_IO_MAP
	data = read_nic_word(dev, offset);

#else // Port IO
	u32 Page = (offset >> 8);

	switch(Page)
	{
	case 0: // Page 0
		data = read_nic_word(dev, offset);
		break;

	case 1: // Page 1
	case 2: // Page 2
	case 3: // Page 3
		{
			u8 psr = read_nic_byte(dev, PSR);

			write_nic_byte(dev, PSR, ((psr & 0xfc) | (u8)Page)); // Switch to page N.
			data = read_nic_word(dev, (offset & 0xff));
			write_nic_byte(dev, PSR, (psr & 0xfc)); // Switch to page 0.
		}
		break;

	default:
		// Illegal page number.
		DMESGE("PlatformIORead2Byte(): illegal page number: %d, offset: %#X", Page, offset);
		break;
	}
#endif

	return data;
}

u32
PlatformIORead4Byte(
	struct net_device *dev,
	u32		offset
	)
{
	u32	data = 0;

#ifndef CONFIG_RTL8180_IO_MAP
	data = read_nic_dword(dev, offset);

#else // Port IO
	u32 Page = (offset >> 8);

	switch(Page)
	{
	case 0: // Page 0
		data = read_nic_dword(dev, offset);
		break;

	case 1: // Page 1
	case 2: // Page 2
	case 3: // Page 3
		{
			u8 psr = read_nic_byte(dev, PSR);

			write_nic_byte(dev, PSR, ((psr & 0xfc) | (u8)Page)); // Switch to page N.
			data = read_nic_dword(dev, (offset & 0xff));
			write_nic_byte(dev, PSR, (psr & 0xfc)); // Switch to page 0.
		}
		break;

	default:
		// Illegal page number.
		DMESGE("PlatformIORead4Byte(): illegal page number: %d, offset: %#X\n", Page, offset);
		break;
	}
#endif

	return data;
}

void
SetOutputEnableOfRfPins(
	struct net_device *dev
	)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);

	switch(priv->rf_chip)
	{
	case RFCHIPID_RTL8225:
	case RF_ZEBRA2:
	case RF_ZEBRA4:
		write_nic_word(dev, RFPinsEnable, 0x1bff);
		//write_nic_word(dev, RFPinsEnable, 0x1fff);
		break;
	}
}

void
ZEBRA_RFSerialWrite(
	struct net_device *dev,
	u32			data2Write,
	u8			totalLength,
	u8			low2high
	)
{
	ThreeWireReg		twreg;
	int 				i;
	u16				oval,oval2,oval3;
	u32				mask;
	u16				UshortBuffer;

	u8			u1bTmp;
#ifdef CONFIG_RTL818X_S
	// RTL8187S HSSI Read/Write Function
	u1bTmp = read_nic_byte(dev, RF_SW_CONFIG);
	u1bTmp |=   RF_SW_CFG_SI;   //reg08[1]=1 Serial Interface(SI)
	write_nic_byte(dev, RF_SW_CONFIG, u1bTmp);
#endif
	UshortBuffer = read_nic_word(dev, RFPinsOutput);
	oval = UshortBuffer & 0xfff8; // We shall clear bit0, 1, 2 first, 2005.10.28, by rcnjko.

	oval2 = read_nic_word(dev, RFPinsEnable);
	oval3 = read_nic_word(dev, RFPinsSelect);

	// <RJ_NOTE> 3-wire should be controled by HW when we finish SW 3-wire programming. 2005.08.10, by rcnjko.
	oval3 &= 0xfff8;

	write_nic_word(dev, RFPinsEnable, (oval2|0x0007)); // Set To Output Enable
	write_nic_word(dev, RFPinsSelect, (oval3|0x0007)); // Set To SW Switch
	udelay(10);

	// Add this to avoid hardware and software 3-wire conflict.
	// 2005.03.01, by rcnjko.
	twreg.longData = 0;
	twreg.struc.enableB = 1;
	write_nic_word(dev, RFPinsOutput, (twreg.longData|oval)); // Set SI_EN (RFLE)
	udelay(2);
	twreg.struc.enableB = 0;
	write_nic_word(dev, RFPinsOutput, (twreg.longData|oval)); // Clear SI_EN (RFLE)
	udelay(10);

	mask = (low2high)?0x01:((u32)0x01<<(totalLength-1));

	for(i=0; i<totalLength/2; i++)
	{
		twreg.struc.data = ((data2Write&mask)!=0) ? 1 : 0;
		write_nic_word(dev, RFPinsOutput, (twreg.longData|oval));
		twreg.struc.clk = 1;
		write_nic_word(dev, RFPinsOutput, (twreg.longData|oval));
		write_nic_word(dev, RFPinsOutput, (twreg.longData|oval));

		mask = (low2high)?(mask<<1):(mask>>1);
		twreg.struc.data = ((data2Write&mask)!=0) ? 1 : 0;
		write_nic_word(dev, RFPinsOutput, (twreg.longData|oval));
		write_nic_word(dev, RFPinsOutput, (twreg.longData|oval));
		twreg.struc.clk = 0;
		write_nic_word(dev, RFPinsOutput, (twreg.longData|oval));
		mask = (low2high)?(mask<<1):(mask>>1);
	}

	twreg.struc.enableB = 1;
	twreg.struc.clk = 0;
	twreg.struc.data = 0;
	write_nic_word(dev, RFPinsOutput, twreg.longData|oval);
	udelay(10);

	write_nic_word(dev, RFPinsOutput, oval|0x0004);
	write_nic_word(dev, RFPinsSelect, oval3|0x0000);

	SetOutputEnableOfRfPins(dev);
}
//by amy


int
HwHSSIThreeWire(
	struct net_device *dev,
	u8			*pDataBuf,
	u8			nDataBufBitCnt,
	int			bSI,
	int			bWrite
	)
{
	int	bResult = 1;
	u8	TryCnt;
	u8	u1bTmp;

	do
	{
		// Check if WE and RE are cleared.
		for(TryCnt = 0; TryCnt < TC_3W_POLL_MAX_TRY_CNT; TryCnt++)
		{
			u1bTmp = read_nic_byte(dev, SW_3W_CMD1);
			if( (u1bTmp & (SW_3W_CMD1_RE|SW_3W_CMD1_WE)) == 0 )
			{
				break;
			}
			udelay(10);
		}
		if (TryCnt == TC_3W_POLL_MAX_TRY_CNT)
			panic("HwThreeWire(): CmdReg: %#X RE|WE bits are not clear!!\n", u1bTmp);

		// RTL8187S HSSI Read/Write Function
		u1bTmp = read_nic_byte(dev, RF_SW_CONFIG);

		if(bSI)
		{
			u1bTmp |=   RF_SW_CFG_SI;   //reg08[1]=1 Serial Interface(SI)
		}else
		{
			u1bTmp &= ~RF_SW_CFG_SI;  //reg08[1]=0 Parallel Interface(PI)
		}

		write_nic_byte(dev, RF_SW_CONFIG, u1bTmp);

		if(bSI)
		{
			// jong: HW SI read must set reg84[3]=0.
			u1bTmp = read_nic_byte(dev, RFPinsSelect);
			u1bTmp &= ~BIT3;
			write_nic_byte(dev, RFPinsSelect, u1bTmp );
		}
	 	// Fill up data buffer for write operation.

		if(bWrite)
		{
			if(nDataBufBitCnt == 16)
			{
				write_nic_word(dev, SW_3W_DB0, *((u16*)pDataBuf));
			}
			else if(nDataBufBitCnt == 64)  // RTL8187S shouldn't enter this case
			{
				write_nic_dword(dev, SW_3W_DB0, *((u32*)pDataBuf));
				write_nic_dword(dev, SW_3W_DB1, *((u32*)(pDataBuf + 4)));
			}
			else
			{
				int idx;
				int ByteCnt = nDataBufBitCnt / 8;
                                //printk("%d\n",nDataBufBitCnt);
				if ((nDataBufBitCnt % 8) != 0)
				panic("HwThreeWire(): nDataBufBitCnt(%d) should be multiple of 8!!!\n",
				nDataBufBitCnt);

			       if (nDataBufBitCnt > 64)
				panic("HwThreeWire(): nDataBufBitCnt(%d) should <= 64!!!\n",
				nDataBufBitCnt);

				for(idx = 0; idx < ByteCnt; idx++)
				{
					write_nic_byte(dev, (SW_3W_DB0+idx), *(pDataBuf+idx));
				}
			}
		}
		else		//read
		{
			if(bSI)
			{
				// SI - reg274[3:0] : RF register's Address
				write_nic_word(dev, SW_3W_DB0, *((u16*)pDataBuf) );
			}
			else
			{
				// PI - reg274[15:12] : RF register's Address
				write_nic_word(dev, SW_3W_DB0, (*((u16*)pDataBuf)) << 12);
			}
		}

		// Set up command: WE or RE.
		if(bWrite)
		{
			write_nic_byte(dev, SW_3W_CMD1, SW_3W_CMD1_WE);
		}
		else
		{
			write_nic_byte(dev, SW_3W_CMD1, SW_3W_CMD1_RE);
		}

		// Check if DONE is set.
		for(TryCnt = 0; TryCnt < TC_3W_POLL_MAX_TRY_CNT; TryCnt++)
		{
			u1bTmp = read_nic_byte(dev, SW_3W_CMD1);
			if(  (u1bTmp & SW_3W_CMD1_DONE) != 0 )
			{
				break;
			}
			udelay(10);
		}

		write_nic_byte(dev, SW_3W_CMD1, 0);

		// Read back data for read operation.
		if(bWrite == 0)
		{
			if(bSI)
			{
				//Serial Interface : reg363_362[11:0]
				*((u16*)pDataBuf) = read_nic_word(dev, SI_DATA_READ) ;
			}
			else
			{
				//Parallel Interface : reg361_360[11:0]
				*((u16*)pDataBuf) = read_nic_word(dev, PI_DATA_READ);
			}

			*((u16*)pDataBuf) &= 0x0FFF;
		}

	}while(0);

	return bResult;
}
//by amy

int
HwThreeWire(
	struct net_device *dev,
	u8			*pDataBuf,
	u8			nDataBufBitCnt,
	int			bHold,
	int			bWrite
	)
{
	int	bResult = 1;
	u8	TryCnt;
	u8	u1bTmp;

	do
	{
		// Check if WE and RE are cleared.
		for(TryCnt = 0; TryCnt < TC_3W_POLL_MAX_TRY_CNT; TryCnt++)
		{
			u1bTmp = read_nic_byte(dev, SW_3W_CMD1);
			if( (u1bTmp & (SW_3W_CMD1_RE|SW_3W_CMD1_WE)) == 0 )
			{
				break;
			}
			udelay(10);
		}
		if (TryCnt == TC_3W_POLL_MAX_TRY_CNT)
			panic("HwThreeWire(): CmdReg: %#X RE|WE bits are not clear!!\n", u1bTmp);

		// Fill up data buffer for write operation.
		if(nDataBufBitCnt == 16)
		{
			write_nic_word(dev, SW_3W_DB0, *((u16 *)pDataBuf));
		}
		else if(nDataBufBitCnt == 64)
		{
			write_nic_dword(dev, SW_3W_DB0, *((u32 *)pDataBuf));
			write_nic_dword(dev, SW_3W_DB1, *((u32 *)(pDataBuf + 4)));
		}
		else
		{
			int idx;
			int ByteCnt = nDataBufBitCnt / 8;

			if ((nDataBufBitCnt % 8) != 0)
				panic("HwThreeWire(): nDataBufBitCnt(%d) should be multiple of 8!!!\n",
				nDataBufBitCnt);

			if (nDataBufBitCnt > 64)
				panic("HwThreeWire(): nDataBufBitCnt(%d) should <= 64!!!\n",
				nDataBufBitCnt);

			for(idx = 0; idx < ByteCnt; idx++)
			{
				write_nic_byte(dev, (SW_3W_DB0+idx), *(pDataBuf+idx));
			}
		}

		// Fill up length field.
		u1bTmp = (u8)(nDataBufBitCnt - 1); // Number of bits - 1.
		if(bHold)
			u1bTmp |= SW_3W_CMD0_HOLD;
		write_nic_byte(dev, SW_3W_CMD0, u1bTmp);

		// Set up command: WE or RE.
		if(bWrite)
		{
			write_nic_byte(dev, SW_3W_CMD1, SW_3W_CMD1_WE);
		}
		else
		{
			write_nic_byte(dev, SW_3W_CMD1, SW_3W_CMD1_RE);
		}

		// Check if WE and RE are cleared and DONE is set.
		for(TryCnt = 0; TryCnt < TC_3W_POLL_MAX_TRY_CNT; TryCnt++)
		{
			u1bTmp = read_nic_byte(dev, SW_3W_CMD1);
			if( (u1bTmp & (SW_3W_CMD1_RE|SW_3W_CMD1_WE)) == 0 &&
				(u1bTmp & SW_3W_CMD1_DONE) != 0 )
			{
				break;
			}
			udelay(10);
		}
		if(TryCnt == TC_3W_POLL_MAX_TRY_CNT)
		{
			//RT_ASSERT(TryCnt != TC_3W_POLL_MAX_TRY_CNT,
			//	("HwThreeWire(): CmdReg: %#X RE|WE bits are not clear or DONE is not set!!\n", u1bTmp));
			// Workaround suggested by wcchu: clear WE here. 2006.07.07, by rcnjko.
			write_nic_byte(dev, SW_3W_CMD1, 0);
		}

		// Read back data for read operation.
		// <RJ_TODO> I am not sure if this is correct output format of a read operation.
		if(bWrite == 0)
		{
			if(nDataBufBitCnt == 16)
			{
				*((u16 *)pDataBuf) = read_nic_word(dev, SW_3W_DB0);
			}
			else if(nDataBufBitCnt == 64)
			{
				*((u32 *)pDataBuf) = read_nic_dword(dev, SW_3W_DB0);
				*((u32 *)(pDataBuf + 4)) = read_nic_dword(dev, SW_3W_DB1);
			}
			else
			{
				int idx;
				int ByteCnt = nDataBufBitCnt / 8;

				if ((nDataBufBitCnt % 8) != 0)
					panic("HwThreeWire(): nDataBufBitCnt(%d) should be multiple of 8!!!\n",
					nDataBufBitCnt);

				if (nDataBufBitCnt > 64)
					panic("HwThreeWire(): nDataBufBitCnt(%d) should <= 64!!!\n",
					nDataBufBitCnt);

				for(idx = 0; idx < ByteCnt; idx++)
				{
					*(pDataBuf+idx) = read_nic_byte(dev, (SW_3W_DB0+idx));
				}
			}
		}

	}while(0);

	return bResult;
}


void
RF_WriteReg(
	struct net_device *dev,
	u8		offset,
	u32		data
	)
{
	//RFReg			reg;
	u32			data2Write;
	u8			len;
	u8			low2high;
	//u32			RF_Read = 0;
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);


	switch(priv->rf_chip)
	{
	case RFCHIPID_RTL8225:
	case RF_ZEBRA2:		// Annie 2006-05-12.
	case RF_ZEBRA4:        //by amy
		switch(priv->RegThreeWireMode)
		{
		case SW_THREE_WIRE:
			{ // Perform SW 3-wire programming by driver.
				data2Write = (data << 4) | (u32)(offset & 0x0f);
				len = 16;
				low2high = 0;
				ZEBRA_RFSerialWrite(dev, data2Write, len, low2high);
       			}
			break;

 		case HW_THREE_WIRE:
			{ // Pure HW 3-wire.
				data2Write = (data << 4) | (u32)(offset & 0x0f);
				len = 16;
				HwThreeWire(
					dev,
					(u8 *)(&data2Write),	// pDataBuf,
					len,				// nDataBufBitCnt,
					0,					// bHold,
					1);					// bWrite
         		}
			break;
  #ifdef CONFIG_RTL818X_S
			case HW_THREE_WIRE_PI: //Parallel Interface
			{ // Pure HW 3-wire.
				data2Write = (data << 4) | (u32)(offset & 0x0f);
				len = 16;
					HwHSSIThreeWire(
						dev,
						(u8*)(&data2Write),	// pDataBuf,
						len,						// nDataBufBitCnt,
						0, 					// bSI
						1); 					// bWrite

                                //printk("33333\n");
			}
			break;

			case HW_THREE_WIRE_SI: //Serial Interface
			{ // Pure HW 3-wire.
				data2Write = (data << 4) | (u32)(offset & 0x0f);
				len = 16;
//                                printk(" enter  ZEBRA_RFSerialWrite\n ");
//                                low2high = 0;
//                                ZEBRA_RFSerialWrite(dev, data2Write, len, low2high);

				HwHSSIThreeWire(
					dev,
					(u8*)(&data2Write),	// pDataBuf,
					len,						// nDataBufBitCnt,
					1, 					// bSI
					1); 					// bWrite

//                                 printk(" exit ZEBRA_RFSerialWrite\n ");
			}
			break;
  #endif


		default:
			DMESGE("RF_WriteReg(): invalid RegThreeWireMode(%d) !!!", priv->RegThreeWireMode);
			break;
		}
		break;

	default:
		DMESGE("RF_WriteReg(): unknown RFChipID: %#X", priv->rf_chip);
		break;
	}
}


void
ZEBRA_RFSerialRead(
	struct net_device *dev,
	u32		data2Write,
	u8		wLength,
	u32		*data2Read,
	u8		rLength,
	u8		low2high
	)
{
	ThreeWireReg	twreg;
	int				i;
	u16			oval,oval2,oval3,tmp, wReg80;
	u32			mask;
	u8			u1bTmp;
	ThreeWireReg	tdata;
	//PHAL_DATA_8187	pHalData = GetHalData8187(pAdapter);
#ifdef CONFIG_RTL818X_S
	{ // RTL8187S HSSI Read/Write Function
		u1bTmp = read_nic_byte(dev, RF_SW_CONFIG);
		u1bTmp |=   RF_SW_CFG_SI;   //reg08[1]=1 Serial Interface(SI)
		write_nic_byte(dev, RF_SW_CONFIG, u1bTmp);
	}
#endif

	wReg80 = oval = read_nic_word(dev, RFPinsOutput);
	oval2 = read_nic_word(dev, RFPinsEnable);
	oval3 = read_nic_word(dev, RFPinsSelect);

	write_nic_word(dev, RFPinsEnable, oval2|0xf);
	write_nic_word(dev, RFPinsSelect, oval3|0xf);

	*data2Read = 0;

	// We must clear BIT0-3 here, otherwise,
	// SW_Enalbe will be true when we first call ZEBRA_RFSerialRead() after 8187MPVC open,
	// which will cause the value read become 0. 2005.04.11, by rcnjko.
	oval &= ~0xf;

	// Avoid collision with hardware three-wire.
	twreg.longData = 0;
	twreg.struc.enableB = 1;
	write_nic_word(dev, RFPinsOutput, twreg.longData|oval); udelay(4);

	twreg.longData = 0;
	twreg.struc.enableB = 0;
	twreg.struc.clk = 0;
	twreg.struc.read_write = 0;
	write_nic_word(dev, RFPinsOutput, twreg.longData|oval); udelay(5);

	mask = (low2high) ? 0x01 : ((u32)0x01<<(32-1));
	for(i = 0; i < wLength/2; i++)
	{
		twreg.struc.data = ((data2Write&mask) != 0) ? 1 : 0;
		write_nic_word(dev, RFPinsOutput, twreg.longData|oval); udelay(1);
		twreg.struc.clk = 1;
		write_nic_word(dev, RFPinsOutput, twreg.longData|oval); udelay(2);
		write_nic_word(dev, RFPinsOutput, twreg.longData|oval); udelay(2);

		mask = (low2high) ? (mask<<1): (mask>>1);

		if(i == 2)
		{
			// Commented out by Jackie, 2004.08.26. <RJ_NOTE> We must comment out the following two lines for we cannot pull down VCOPDN during RF Serail Read.
			//PlatformEFIOWrite2Byte(pAdapter, RFPinsEnable, 0xe);     // turn off data enable
			//PlatformEFIOWrite2Byte(pAdapter, RFPinsSelect, 0xe);

			twreg.struc.read_write=1;
			write_nic_word(dev, RFPinsOutput, twreg.longData|oval); udelay(2);
			twreg.struc.clk = 0;
			write_nic_word(dev, RFPinsOutput, twreg.longData|oval); udelay(2);
			break;
		}
		twreg.struc.data = ((data2Write&mask) != 0) ? 1: 0;
		write_nic_word(dev, RFPinsOutput, twreg.longData|oval); udelay(2);
		write_nic_word(dev, RFPinsOutput, twreg.longData|oval); udelay(2);

		twreg.struc.clk = 0;
		write_nic_word(dev, RFPinsOutput, twreg.longData|oval); udelay(1);

		mask = (low2high) ? (mask<<1) : (mask>>1);
	}

	twreg.struc.clk = 0;
	twreg.struc.data = 0;
	write_nic_word(dev, RFPinsOutput, twreg.longData|oval); udelay(2);
	mask = (low2high) ? 0x01 : ((u32)0x01 << (12-1));

	//
	// 061016, by rcnjko:
	// We must set data pin to HW controled, otherwise RF can't driver it and
	// value RF register won't be able to read back properly.
	//
	write_nic_word(dev, RFPinsEnable, ( ((oval2|0x0E) & (~0x01))) );

	for(i = 0; i < rLength; i++)
	{
		write_nic_word(dev, RFPinsOutput, twreg.longData|oval); udelay(1);
		twreg.struc.clk = 1;
		write_nic_word(dev, RFPinsOutput, twreg.longData|oval); udelay(2);
		write_nic_word(dev, RFPinsOutput, twreg.longData|oval); udelay(2);
		write_nic_word(dev, RFPinsOutput, twreg.longData|oval); udelay(2);
		tmp = read_nic_word(dev, RFPinsInput);
		tdata.longData = tmp;
		*data2Read |= tdata.struc.clk ? mask : 0;

		twreg.struc.clk = 0;
		write_nic_word(dev, RFPinsOutput, twreg.longData|oval); udelay(2);

		mask = (low2high) ? (mask<<1) : (mask>>1);
	}
	twreg.struc.enableB = 1;
	twreg.struc.clk = 0;
	twreg.struc.data = 0;
	twreg.struc.read_write = 1;
	write_nic_word(dev, RFPinsOutput, twreg.longData|oval); udelay(2);

	//PlatformEFIOWrite2Byte(pAdapter, RFPinsEnable, oval2|0x8);   // Set To Output Enable
	write_nic_word(dev, RFPinsEnable, oval2);   // Set To Output Enable, <RJ_NOTE> We cannot enable BIT3 here, otherwise, we will failed to switch channel. 2005.04.12.
	//PlatformEFIOWrite2Byte(pAdapter, RFPinsEnable, 0x1bff);
	write_nic_word(dev, RFPinsSelect, oval3);   // Set To SW Switch
	//PlatformEFIOWrite2Byte(pAdapter, RFPinsSelect, 0x0488);
	write_nic_word(dev, RFPinsOutput, 0x3a0);
	//PlatformEFIOWrite2Byte(pAdapter, RFPinsOutput, 0x0480);
}


u32
RF_ReadReg(
	struct net_device *dev,
	u8		offset
	)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	u32			data2Write;
	u8			wlen;
	u8			rlen;
	u8			low2high;
	u32			dataRead;

	switch(priv->rf_chip)
	{
	case RFCHIPID_RTL8225:
	case RF_ZEBRA2:
	case RF_ZEBRA4:
		switch(priv->RegThreeWireMode)
		{
#ifdef CONFIG_RTL818X_S
			case HW_THREE_WIRE_PI: // For 87S  Parallel Interface.
			{
				data2Write = ((u32)(offset&0x0f));
				wlen=16;
				HwHSSIThreeWire(
					dev,
					(u8*)(&data2Write),	// pDataBuf,
					wlen,					// nDataBufBitCnt,
					0, 					// bSI
					0); 					// bWrite
				dataRead= data2Write;
			}
			break;

			case HW_THREE_WIRE_SI: // For 87S Serial Interface.
			{
				data2Write = ((u32)(offset&0x0f)) ;
				wlen=16;
				HwHSSIThreeWire(
					dev,
					(u8*)(&data2Write),	// pDataBuf,
					wlen,					// nDataBufBitCnt,
					1, 					// bSI
					0					// bWrite
					);
				dataRead= data2Write;
			}
			break;

#endif
			// Perform SW 3-wire programming by driver.
			default:
			{
				data2Write = ((u32)(offset&0x1f)) << 27; // For Zebra E-cut. 2005.04.11, by rcnjko.
				wlen = 6;
				rlen = 12;
				low2high = 0;
				ZEBRA_RFSerialRead(dev, data2Write, wlen,&dataRead,rlen, low2high);
			}
			break;
		}
		break;
	default:
		dataRead = 0;
		break;
	}

	return dataRead;
}


// by Owen on 04/07/14 for writing BB register successfully
void
WriteBBPortUchar(
	struct net_device *dev,
	u32		Data
	)
{
	//u8	TimeoutCounter;
	u8	RegisterContent;
	u8	UCharData;

	UCharData = (u8)((Data & 0x0000ff00) >> 8);
	PlatformIOWrite4Byte(dev, PhyAddr, Data);
	//for(TimeoutCounter = 10; TimeoutCounter > 0; TimeoutCounter--)
	{
		PlatformIOWrite4Byte(dev, PhyAddr, Data & 0xffffff7f);
		RegisterContent = PlatformIORead1Byte(dev, PhyDataR);
		//if(UCharData == RegisterContent)
		//	break;
	}
}

u8
ReadBBPortUchar(
	struct net_device *dev,
	u32		addr
	)
{
	//u8	TimeoutCounter;
	u8	RegisterContent;

	PlatformIOWrite4Byte(dev, PhyAddr, addr & 0xffffff7f);
	RegisterContent = PlatformIORead1Byte(dev, PhyDataR);

	return RegisterContent;
}
//{by amy 080312
#ifdef CONFIG_RTL818X_S
//
//	Description:
//		Perform Antenna settings with antenna diversity on 87SE.
//    Created by Roger, 2008.01.25.
//
bool
SetAntennaConfig87SE(
	struct net_device *dev,
	u8			DefaultAnt,		// 0: Main, 1: Aux.
	bool		bAntDiversity 	// 1:Enable, 0: Disable.
)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	bool   bAntennaSwitched = true;

	//printk("SetAntennaConfig87SE(): DefaultAnt(%d), bAntDiversity(%d)\n", DefaultAnt, bAntDiversity);

	// Threshold for antenna diversity.
	write_phy_cck(dev, 0x0c, 0x09); // Reg0c : 09

	if( bAntDiversity )  //  Enable Antenna Diversity.
	{
		if( DefaultAnt == 1 )  // aux antenna
		{
			// Mac register, aux antenna
			write_nic_byte(dev, ANTSEL, 0x00);

			// Config CCK RX antenna.
			write_phy_cck(dev, 0x11, 0xbb); // Reg11 : bb
			write_phy_cck(dev, 0x01, 0xc7); // Reg01 : c7

			// Config OFDM RX antenna.
			write_phy_ofdm(dev, 0x0D, 0x54);   // Reg0d : 54
			write_phy_ofdm(dev, 0x18, 0xb2);  // Reg18 : b2
		}
		else //  use main antenna
		{
			// Mac register, main antenna
			write_nic_byte(dev, ANTSEL, 0x03);
			//base band
			// Config CCK RX antenna.
			write_phy_cck(dev, 0x11, 0x9b); // Reg11 : 9b
			write_phy_cck(dev, 0x01, 0xc7); // Reg01 : c7

			// Config OFDM RX antenna.
			write_phy_ofdm(dev, 0x0d, 0x5c);   // Reg0d : 5c
			write_phy_ofdm(dev, 0x18, 0xb2);  // Reg18 : b2
		}
	}
	else   // Disable Antenna Diversity.
	{
		if( DefaultAnt == 1 ) // aux Antenna
		{
			// Mac register, aux antenna
			write_nic_byte(dev, ANTSEL, 0x00);

			// Config CCK RX antenna.
			write_phy_cck(dev, 0x11, 0xbb); // Reg11 : bb
			write_phy_cck(dev, 0x01, 0x47); // Reg01 : 47

			// Config OFDM RX antenna.
			write_phy_ofdm(dev, 0x0D, 0x54);   // Reg0d : 54
			write_phy_ofdm(dev, 0x18, 0x32);  // Reg18 : 32
		}
		else // main Antenna
		{
			// Mac register, main antenna
			write_nic_byte(dev, ANTSEL, 0x03);

			// Config CCK RX antenna.
			write_phy_cck(dev, 0x11, 0x9b); // Reg11 : 9b
			write_phy_cck(dev, 0x01, 0x47); // Reg01 : 47

			// Config OFDM RX antenna.
			write_phy_ofdm(dev, 0x0D, 0x5c);   // Reg0d : 5c
			write_phy_ofdm(dev, 0x18, 0x32);  // Reg18 : 32
		}
	}
	priv->CurrAntennaIndex = DefaultAnt; // Update default settings.
	return	bAntennaSwitched;
}
#endif
//by amy 080312
/*---------------------------------------------------------------
  * Hardware Initialization.
  * the code is ported from Windows source code
  ----------------------------------------------------------------*/

void
ZEBRA_Config_85BASIC_HardCode(
	struct net_device *dev
	)
{

	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	u32			i;
	u32	addr,data;
	u32	u4bRegOffset, u4bRegValue, u4bRF23, u4bRF24;
       u8			u1b24E;

#ifdef CONFIG_RTL818X_S

	//=============================================================================
	// 87S_PCIE :: RADIOCFG.TXT
	//=============================================================================


	// Page1 : reg16-reg30
	RF_WriteReg(dev, 0x00, 0x013f);			mdelay(1); // switch to page1
	u4bRF23= RF_ReadReg(dev, 0x08);			mdelay(1);
	u4bRF24= RF_ReadReg(dev, 0x09);			mdelay(1);

	if (u4bRF23==0x818 && u4bRF24==0x70C && priv->card_8185 == VERSION_8187S_C)
		priv->card_8185 = VERSION_8187S_D;

	// Page0 : reg0-reg15

//	RF_WriteReg(dev, 0x00, 0x003f);			mdelay(1);//1
	RF_WriteReg(dev, 0x00, 0x009f);      	mdelay(1);// 1

	RF_WriteReg(dev, 0x01, 0x06e0);			mdelay(1);

//	RF_WriteReg(dev, 0x02, 0x004c);			mdelay(1);//2
	RF_WriteReg(dev, 0x02, 0x004d);			mdelay(1);// 2

//	RF_WriteReg(dev, 0x03, 0x0000);			mdelay(1);//3
	RF_WriteReg(dev, 0x03, 0x07f1);			mdelay(1);// 3

	RF_WriteReg(dev, 0x04, 0x0975);			mdelay(1);
	RF_WriteReg(dev, 0x05, 0x0c72);			mdelay(1);
	RF_WriteReg(dev, 0x06, 0x0ae6);			mdelay(1);
	RF_WriteReg(dev, 0x07, 0x00ca);			mdelay(1);
	RF_WriteReg(dev, 0x08, 0x0e1c);			mdelay(1);
	RF_WriteReg(dev, 0x09, 0x02f0);			mdelay(1);
	RF_WriteReg(dev, 0x0a, 0x09d0);			mdelay(1);
	RF_WriteReg(dev, 0x0b, 0x01ba);			mdelay(1);
	RF_WriteReg(dev, 0x0c, 0x0640);			mdelay(1);
	RF_WriteReg(dev, 0x0d, 0x08df);			mdelay(1);
	RF_WriteReg(dev, 0x0e, 0x0020);			mdelay(1);
	RF_WriteReg(dev, 0x0f, 0x0990);			mdelay(1);


	// Page1 : reg16-reg30
	RF_WriteReg(dev, 0x00, 0x013f);			mdelay(1);

	RF_WriteReg(dev, 0x03, 0x0806);			mdelay(1);

	if(priv->card_8185 < VERSION_8187S_C)
	{
		RF_WriteReg(dev, 0x04, 0x03f7);			mdelay(1);
		RF_WriteReg(dev, 0x05, 0x05ab);			mdelay(1);
		RF_WriteReg(dev, 0x06, 0x00c1);			mdelay(1);
	}
	else
	{
		RF_WriteReg(dev, 0x04, 0x03a7);			mdelay(1);
		RF_WriteReg(dev, 0x05, 0x059b);			mdelay(1);
		RF_WriteReg(dev, 0x06, 0x0081);			mdelay(1);
	}


	RF_WriteReg(dev, 0x07, 0x01A0);			mdelay(1);
// Don't write RF23/RF24 to make a difference between 87S C cut and D cut. asked by SD3 stevenl.
//	RF_WriteReg(dev, 0x08, 0x0597);			mdelay(1);
//	RF_WriteReg(dev, 0x09, 0x050a);			mdelay(1);
	RF_WriteReg(dev, 0x0a, 0x0001);			mdelay(1);
	RF_WriteReg(dev, 0x0b, 0x0418);			mdelay(1);

	if(priv->card_8185 == VERSION_8187S_D)
	{
		RF_WriteReg(dev, 0x0c, 0x0fbe);			mdelay(1);
		RF_WriteReg(dev, 0x0d, 0x0008);			mdelay(1);
		RF_WriteReg(dev, 0x0e, 0x0807);			mdelay(1); // RX LO buffer
	}
	else
	{
		RF_WriteReg(dev, 0x0c, 0x0fbe);			mdelay(1);
		RF_WriteReg(dev, 0x0d, 0x0008);			mdelay(1);
		RF_WriteReg(dev, 0x0e, 0x0806);			mdelay(1); // RX LO buffer
	}

	RF_WriteReg(dev, 0x0f, 0x0acc);			mdelay(1);

//	RF_WriteReg(dev, 0x00, 0x017f);			mdelay(1);//6
	RF_WriteReg(dev, 0x00, 0x01d7);			mdelay(1);// 6

	RF_WriteReg(dev, 0x03, 0x0e00);			mdelay(1);
	RF_WriteReg(dev, 0x04, 0x0e50);			mdelay(1);
	for(i=0;i<=36;i++)
	{
		RF_WriteReg(dev, 0x01, i);                     mdelay(1);
		RF_WriteReg(dev, 0x02, ZEBRA_RF_RX_GAIN_TABLE[i]); mdelay(1);
		//DbgPrint("RF - 0x%x = 0x%x", i, ZEBRA_RF_RX_GAIN_TABLE[i]);
	}

	RF_WriteReg(dev, 0x05, 0x0203);			mdelay(1); 	/// 203, 343
	//RF_WriteReg(dev, 0x06, 0x0300);			mdelay(1);	// 400
	RF_WriteReg(dev, 0x06, 0x0200);			mdelay(1);	// 400

	RF_WriteReg(dev, 0x00, 0x0137);			mdelay(1);	// switch to reg16-reg30, and HSSI disable 137
	mdelay(10); 	// Deay 10 ms. //0xfd

//	RF_WriteReg(dev, 0x0c, 0x09be);			mdelay(1);	// 7
	//RF_WriteReg(dev, 0x0c, 0x07be);			mdelay(1);
	//mdelay(10); 	// Deay 10 ms. //0xfd

	RF_WriteReg(dev, 0x0d, 0x0008);			mdelay(1);	// Z4 synthesizer loop filter setting, 392
	mdelay(10); 	// Deay 10 ms. //0xfd

	RF_WriteReg(dev, 0x00, 0x0037);			mdelay(1);	// switch to reg0-reg15, and HSSI disable
	mdelay(10); 	// Deay 10 ms. //0xfd

	RF_WriteReg(dev, 0x04, 0x0160);			mdelay(1); 	// CBC on, Tx Rx disable, High gain
	mdelay(10); 	// Deay 10 ms. //0xfd

	RF_WriteReg(dev, 0x07, 0x0080);			mdelay(1);	// Z4 setted channel 1
	mdelay(10); 	// Deay 10 ms. //0xfd

	RF_WriteReg(dev, 0x02, 0x088D);			mdelay(1);	// LC calibration
	mdelay(200); 	// Deay 200 ms. //0xfd
	mdelay(10); 	// Deay 10 ms. //0xfd
	mdelay(10); 	// Deay 10 ms. //0xfd

	RF_WriteReg(dev, 0x00, 0x0137);			mdelay(1);	// switch to reg16-reg30 137, and HSSI disable 137
	mdelay(10); 	// Deay 10 ms. //0xfd

	RF_WriteReg(dev, 0x07, 0x0000);			mdelay(1);
	RF_WriteReg(dev, 0x07, 0x0180);			mdelay(1);
	RF_WriteReg(dev, 0x07, 0x0220);			mdelay(1);
	RF_WriteReg(dev, 0x07, 0x03E0);			mdelay(1);

	// DAC calibration off 20070702
	RF_WriteReg(dev, 0x06, 0x00c1);			mdelay(1);
	RF_WriteReg(dev, 0x0a, 0x0001);			mdelay(1);
//{by amy 080312
	// For crystal calibration, added by Roger, 2007.12.11.
	if( priv->bXtalCalibration ) // reg 30.
	{ // enable crystal calibration.
		// RF Reg[30], (1)Xin:[12:9], Xout:[8:5],  addr[4:0].
		// (2)PA Pwr delay timer[15:14], default: 2.4us, set BIT15=0
		// (3)RF signal on/off when calibration[13], default: on, set BIT13=0.
		// So we should minus 4 BITs offset.
		RF_WriteReg(dev, 0x0f, (priv->XtalCal_Xin<<5)|(priv->XtalCal_Xout<<1)|BIT11|BIT9);			mdelay(1);
		printk("ZEBRA_Config_85BASIC_HardCode(): (%02x)\n",
				(priv->XtalCal_Xin<<5) | (priv->XtalCal_Xout<<1) | BIT11| BIT9);
	}
	else
	{ // using default value. Xin=6, Xout=6.
		RF_WriteReg(dev, 0x0f, 0x0acc);			mdelay(1);
	}
//by amy 080312
//	RF_WriteReg(dev, 0x0f, 0x0acc);			mdelay(1);  //-by amy 080312

	RF_WriteReg(dev, 0x00, 0x00bf);			mdelay(1); // switch to reg0-reg15, and HSSI enable
//	RF_WriteReg(dev, 0x0d, 0x009f);			mdelay(1); // Rx BB start calibration, 00c//-edward
	RF_WriteReg(dev, 0x0d, 0x08df);			mdelay(1); // Rx BB start calibration, 00c//+edward
	RF_WriteReg(dev, 0x02, 0x004d);			mdelay(1); // temperature meter off
	RF_WriteReg(dev, 0x04, 0x0975);			mdelay(1); // Rx mode
	mdelay(10);	// Deay 10 ms. //0xfe
	mdelay(10);	// Deay 10 ms. //0xfe
	mdelay(10);	// Deay 10 ms. //0xfe
	RF_WriteReg(dev, 0x00, 0x0197);			mdelay(1); // Rx mode//+edward
	RF_WriteReg(dev, 0x05, 0x05ab);			mdelay(1); // Rx mode//+edward
	RF_WriteReg(dev, 0x00, 0x009f);			mdelay(1); // Rx mode//+edward

#if 0//-edward
	RF_WriteReg(dev, 0x00, 0x0197);			mdelay(1);
	RF_WriteReg(dev, 0x05, 0x05ab);			mdelay(1);
	RF_WriteReg(dev, 0x00, 0x009F);			mdelay(1);
#endif
	RF_WriteReg(dev, 0x01, 0x0000);			mdelay(1); // Rx mode//+edward
	RF_WriteReg(dev, 0x02, 0x0000);			mdelay(1); // Rx mode//+edward
	//power save parameters.
	u1b24E = read_nic_byte(dev, 0x24E);
	write_nic_byte(dev, 0x24E, (u1b24E & (~(BIT5|BIT6))));

	//=============================================================================

	//=============================================================================
	// CCKCONF.TXT
	//=============================================================================

	/*	[POWER SAVE] Power Saving Parameters by jong. 2007-11-27
	   	CCK reg0x00[7]=1'b1 :power saving for TX (default)
		CCK reg0x00[6]=1'b1: power saving for RX (default)
		CCK reg0x06[4]=1'b1: turn off channel estimation related circuits if not doing channel estimation.
		CCK reg0x06[3]=1'b1: turn off unused circuits before cca = 1
		CCK reg0x06[2]=1'b1: turn off cck's circuit if macrst =0
	*/
#if 0
	write_nic_dword(dev, PHY_ADR, 0x0100c880);
	write_nic_dword(dev, PHY_ADR, 0x01001c86);
	write_nic_dword(dev, PHY_ADR, 0x01007890);
	write_nic_dword(dev, PHY_ADR, 0x0100d0ae);
	write_nic_dword(dev, PHY_ADR, 0x010006af);
	write_nic_dword(dev, PHY_ADR, 0x01004681);
#endif
	write_phy_cck(dev,0x00,0xc8);
	write_phy_cck(dev,0x06,0x1c);
	write_phy_cck(dev,0x10,0x78);
	write_phy_cck(dev,0x2e,0xd0);
	write_phy_cck(dev,0x2f,0x06);
	write_phy_cck(dev,0x01,0x46);

	// power control
	write_nic_byte(dev, CCK_TXAGC, 0x10);
	write_nic_byte(dev, OFDM_TXAGC, 0x1B);
	write_nic_byte(dev, ANTSEL, 0x03);
#else
	//=============================================================================
	// RADIOCFG.TXT
	//=============================================================================

	RF_WriteReg(dev, 0x00, 0x00b7);			mdelay(1);
	RF_WriteReg(dev, 0x01, 0x0ee0);			mdelay(1);
	RF_WriteReg(dev, 0x02, 0x044d);			mdelay(1);
	RF_WriteReg(dev, 0x03, 0x0441);			mdelay(1);
	RF_WriteReg(dev, 0x04, 0x08c3);			mdelay(1);
	RF_WriteReg(dev, 0x05, 0x0c72);			mdelay(1);
	RF_WriteReg(dev, 0x06, 0x00e6);			mdelay(1);
	RF_WriteReg(dev, 0x07, 0x082a);			mdelay(1);
	RF_WriteReg(dev, 0x08, 0x003f);			mdelay(1);
	RF_WriteReg(dev, 0x09, 0x0335);			mdelay(1);
	RF_WriteReg(dev, 0x0a, 0x09d4);			mdelay(1);
	RF_WriteReg(dev, 0x0b, 0x07bb);			mdelay(1);
	RF_WriteReg(dev, 0x0c, 0x0850);			mdelay(1);
	RF_WriteReg(dev, 0x0d, 0x0cdf);			mdelay(1);
	RF_WriteReg(dev, 0x0e, 0x002b);			mdelay(1);
	RF_WriteReg(dev, 0x0f, 0x0114);			mdelay(1);

	RF_WriteReg(dev, 0x00, 0x01b7);			mdelay(1);


	for(i=1;i<=95;i++)
	{
		RF_WriteReg(dev, 0x01, i);	mdelay(1);
		RF_WriteReg(dev, 0x02, ZEBRA_RF_RX_GAIN_TABLE[i]); mdelay(1);
		//DbgPrint("RF - 0x%x = 0x%x", i, ZEBRA_RF_RX_GAIN_TABLE[i]);
	}

	RF_WriteReg(dev, 0x03, 0x0080);			mdelay(1); 	// write reg 18
	RF_WriteReg(dev, 0x05, 0x0004);			mdelay(1);	// write reg 20
	RF_WriteReg(dev, 0x00, 0x00b7);			mdelay(1);	// switch to reg0-reg15
	//0xfd
	//0xfd
	//0xfd
	RF_WriteReg(dev, 0x02, 0x0c4d);			mdelay(1);
	mdelay(100);	// Deay 100 ms. //0xfe
	mdelay(100);	// Deay 100 ms. //0xfe
	RF_WriteReg(dev, 0x02, 0x044d);			mdelay(1);
	RF_WriteReg(dev, 0x00, 0x02bf);			mdelay(1);	//0x002f disable 6us corner change,  06f--> enable

	//=============================================================================

	//=============================================================================
	// CCKCONF.TXT
	//=============================================================================

	//=============================================================================

	//=============================================================================
	// Follow WMAC RTL8225_Config()
	//=============================================================================

	// power control
	write_nic_byte(dev, CCK_TXAGC, 0x03);
	write_nic_byte(dev, OFDM_TXAGC, 0x07);
	write_nic_byte(dev, ANTSEL, 0x03);

	//=============================================================================

	// OFDM BBP setup
//	SetOutputEnableOfRfPins(dev);//by amy
#endif



	//=============================================================================
	// AGC.txt
	//=============================================================================

//	PlatformIOWrite4Byte( dev, PhyAddr, 0x00001280);	// Annie, 2006-05-05
	write_phy_ofdm(dev, 0x00, 0x12);
	//WriteBBPortUchar(dev, 0x00001280);

	for (i=0; i<128; i++)
	{
		//DbgPrint("AGC - [%x+1] = 0x%x\n", i, ZEBRA_AGC[i+1]);

		data = ZEBRA_AGC[i+1];
		data = data << 8;
		data = data | 0x0000008F;

		addr = i + 0x80; //enable writing AGC table
		addr = addr << 8;
		addr = addr | 0x0000008E;

		WriteBBPortUchar(dev, data);
		WriteBBPortUchar(dev, addr);
		WriteBBPortUchar(dev, 0x0000008E);
	}

	PlatformIOWrite4Byte( dev, PhyAddr, 0x00001080);	// Annie, 2006-05-05
	//WriteBBPortUchar(dev, 0x00001080);

	//=============================================================================

	//=============================================================================
	// OFDMCONF.TXT
	//=============================================================================

	for(i=0; i<60; i++)
	{
		u4bRegOffset=i;
		u4bRegValue=OFDM_CONFIG[i];

		//DbgPrint("OFDM - 0x%x = 0x%x\n", u4bRegOffset, u4bRegValue);

		WriteBBPortUchar(dev,
						(0x00000080 |
						(u4bRegOffset & 0x7f) |
						((u4bRegValue & 0xff) << 8)));
	}

	//=============================================================================
//by amy for antenna
	//=============================================================================
//{by amy 080312
#ifdef CONFIG_RTL818X_S
	// Config Sw/Hw  Combinational Antenna Diversity. Added by Roger, 2008.02.26.
	SetAntennaConfig87SE(dev, priv->bDefaultAntenna1, priv->bSwAntennaDiverity);
#endif
//by amy 080312}
#if 0
	// Config Sw/Hw  Antenna Diversity
	if( priv->bSwAntennaDiverity )  //  Use SW+Hw Antenna Diversity
	{
		if( priv->bDefaultAntenna1 == true )  // aux antenna
		{
			// Mac register, aux antenna
			write_nic_byte(dev, ANTSEL, 0x00);
			// Config CCK RX antenna.
			write_phy_cck(dev, 0x11, 0xbb); // Reg11 : bb
			write_phy_cck(dev, 0x0c, 0x09); // Reg0c : 09
			write_phy_cck(dev, 0x01, 0xc7); // Reg01 : c7
			// Config OFDM RX antenna.
			write_phy_ofdm(dev, 0x0d, 0x54);   // Reg0d : 54
			write_phy_ofdm(dev, 0x18, 0xb2);  // Reg18 : b2
		}
		else //  main antenna
		{
			// Mac register, main antenna
			write_nic_byte(dev, ANTSEL, 0x03);
			//base band
			// Config CCK RX antenna.
			write_phy_cck(dev, 0x11, 0x9b); // Reg11 : 9b
			write_phy_cck(dev, 0x0c, 0x09); // Reg0c : 09
			write_phy_cck(dev, 0x01, 0xc7); // Reg01 : c7
			// Config OFDM RX antenna.
			write_phy_ofdm(dev, 0x0d, 0x5c);   // Reg0d : 5c
			write_phy_ofdm(dev, 0x18, 0xb2);  // Reg18 : b2
		}
	}
	else   // Disable Antenna Diversity
	{
		if( priv->bDefaultAntenna1 == true ) // aux Antenna
		{
			// Mac register, aux antenna
			write_nic_byte(dev, ANTSEL, 0x00);
			// Config CCK RX antenna.
			write_phy_cck(dev, 0x11, 0xbb); // Reg11 : bb
			write_phy_cck(dev, 0x0c, 0x09); // Reg0c : 09
			write_phy_cck(dev, 0x01, 0x47); // Reg01 : 47
			// Config OFDM RX antenna.
			write_phy_ofdm(dev, 0x0d, 0x54);   // Reg0d : 54
			write_phy_ofdm(dev, 0x18, 0x32);  // Reg18 : 32
		}
		else // main Antenna
		{
			// Mac register, main antenna
			write_nic_byte(dev, ANTSEL, 0x03);
			// Config CCK RX antenna.
			write_phy_cck(dev, 0x11, 0x9b); // Reg11 : 9b
			write_phy_cck(dev, 0x0c, 0x09); // Reg0c : 09
			write_phy_cck(dev, 0x01, 0x47); // Reg01 : 47
			// Config OFDM RX antenna.
			write_phy_ofdm(dev, 0x0d, 0x5c);   // Reg0d : 5c
			write_phy_ofdm(dev, 0x18, 0x32);  // Reg18 : 32
		}
	}
#endif
//by amy for antenna
}


void
UpdateInitialGain(
	struct net_device *dev
	)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	//unsigned char* IGTable;
	//u8			DIG_CurrentInitialGain = 4;
	//unsigned char u1Tmp;

	//lzm add 080826
	if(priv->eRFPowerState != eRfOn)
	{
		//Don't access BB/RF under disable PLL situation.
		//RT_TRACE(COMP_DIG, DBG_LOUD, ("UpdateInitialGain - pHalData->eRFPowerState!=eRfOn\n"));
		// Back to the original state
		priv->InitialGain= priv->InitialGainBackUp;
		return;
	}

	switch(priv->rf_chip)
	{
#if 0
	case RF_ZEBRA2:
		// Dynamic set initial gain, by shien chang, 2006.07.14
		switch(priv->InitialGain)
		{
			case 1: //m861dBm
				DMESG("RTL8185B + 8225 Initial Gain State 1: -82 dBm \n");
				write_nic_dword(dev, PhyAddr, 0x2697);	mdelay(1);
				write_nic_dword(dev, PhyAddr, 0x86a4);	mdelay(1);
				write_nic_dword(dev, PhyAddr, 0xfa85);	mdelay(1);
				break;

			case 2: //m862dBm
				DMESG("RTL8185B + 8225 Initial Gain State 2: -82 dBm \n");
				write_nic_dword(dev, PhyAddr, 0x2697);	mdelay(1);
				write_nic_dword(dev, PhyAddr, 0x86a4);	mdelay(1);
				write_nic_dword(dev, PhyAddr, 0xfb85);	mdelay(1);
				break;

			case 3: //m863dBm
				DMESG("RTL8185B + 8225 Initial Gain State 3: -82 dBm \n");
				write_nic_dword(dev, PhyAddr, 0x2697);	mdelay(1);
				write_nic_dword(dev, PhyAddr, 0x96a4);	mdelay(1);
				write_nic_dword(dev, PhyAddr, 0xfb85);	mdelay(1);
				break;

			case 4: //m864dBm
				DMESG("RTL8185B + 8225 Initial Gain State 4: -78 dBm \n");
				write_nic_dword(dev, PhyAddr, 0x2697);	mdelay(1);
				write_nic_dword(dev, PhyAddr, 0xa6a4);	mdelay(1);
				write_nic_dword(dev, PhyAddr, 0xfb85);	mdelay(1);
				break;

			case 5: //m82dBm
				DMESG("RTL8185B + 8225 Initial Gain State 5: -74 dBm \n");
				write_nic_dword(dev, PhyAddr, 0x3697);	mdelay(1);
				write_nic_dword(dev, PhyAddr, 0xa6a4);	mdelay(1);
				write_nic_dword(dev, PhyAddr, 0xfb85);	mdelay(1);
				break;

			case 6: //m78dBm
				DMESG("RTL8185B + 8225 Initial Gain State 6: -70 dBm \n");
				write_nic_dword(dev, PhyAddr, 0x4697);	mdelay(1);
				write_nic_dword(dev, PhyAddr, 0xa6a4);	mdelay(1);
				write_nic_dword(dev, PhyAddr, 0xfb85);	mdelay(1);
				break;

			case 7: //m74dBm
				DMESG("RTL8185B + 8225 Initial Gain State 7: -66 dBm \n");
				write_nic_dword(dev, PhyAddr, 0x5697);	mdelay(1);
				write_nic_dword(dev, PhyAddr, 0xa6a4);	mdelay(1);
				write_nic_dword(dev, PhyAddr, 0xfb85);	mdelay(1);
				break;

			default:	//MP
				DMESG("RTL8185B + 8225 Initial Gain State 1: -82 dBm (default)\n");
				write_nic_dword(dev, PhyAddr, 0x2697);	mdelay(1);
				write_nic_dword(dev, PhyAddr, 0x86a4);	mdelay(1);
				write_nic_dword(dev, PhyAddr, 0xfa85);	mdelay(1);
				break;
		}
		break;
#endif
	case RF_ZEBRA4:
		// Dynamic set initial gain, follow 87B
		switch(priv->InitialGain)
		{
			case 1: //m861dBm
				//DMESG("RTL8187 + 8225 Initial Gain State 1: -82 dBm \n");
				write_phy_ofdm(dev, 0x17, 0x26);	mdelay(1);
				write_phy_ofdm(dev, 0x24, 0x86);	mdelay(1);
				write_phy_ofdm(dev, 0x05, 0xfa);	mdelay(1);
				break;

			case 2: //m862dBm
				//DMESG("RTL8187 + 8225 Initial Gain State 2: -82 dBm \n");
				write_phy_ofdm(dev, 0x17, 0x36);	mdelay(1);
				write_phy_ofdm(dev, 0x24, 0x86);	mdelay(1);
				write_phy_ofdm(dev, 0x05, 0xfa);	mdelay(1);
				break;

			case 3: //m863dBm
				//DMESG("RTL8187 + 8225 Initial Gain State 3: -82 dBm \n");
				write_phy_ofdm(dev, 0x17, 0x36);	mdelay(1);
				write_phy_ofdm(dev, 0x24, 0x86);	mdelay(1);
				write_phy_ofdm(dev, 0x05, 0xfb);	mdelay(1);
				break;

			case 4: //m864dBm
				//DMESG("RTL8187 + 8225 Initial Gain State 4: -78 dBm \n");
				write_phy_ofdm(dev, 0x17, 0x46);	mdelay(1);
				write_phy_ofdm(dev, 0x24, 0x86);	mdelay(1);
				write_phy_ofdm(dev, 0x05, 0xfb);	mdelay(1);
				break;

			case 5: //m82dBm
				//DMESG("RTL8187 + 8225 Initial Gain State 5: -74 dBm \n");
				write_phy_ofdm(dev, 0x17, 0x46);	mdelay(1);
				write_phy_ofdm(dev, 0x24, 0x96);	mdelay(1);
				write_phy_ofdm(dev, 0x05, 0xfb);	mdelay(1);
				break;

			case 6: //m78dBm
				//DMESG ("RTL8187 + 8225 Initial Gain State 6: -70 dBm \n");
				write_phy_ofdm(dev, 0x17, 0x56);	mdelay(1);
				write_phy_ofdm(dev, 0x24, 0x96);	mdelay(1);
				write_phy_ofdm(dev, 0x05, 0xfc);	mdelay(1);
				break;

			case 7: //m74dBm
				//DMESG("RTL8187 + 8225 Initial Gain State 7: -66 dBm \n");
				write_phy_ofdm(dev, 0x17, 0x56);	mdelay(1);
				write_phy_ofdm(dev, 0x24, 0xa6);	mdelay(1);
				write_phy_ofdm(dev, 0x05, 0xfc);	mdelay(1);
				break;

			case 8:
				//DMESG("RTL8187 + 8225 Initial Gain State 8:\n");
				write_phy_ofdm(dev, 0x17, 0x66);	mdelay(1);
				write_phy_ofdm(dev, 0x24, 0xb6);	mdelay(1);
				write_phy_ofdm(dev, 0x05, 0xfc);	mdelay(1);
				break;


			default:	//MP
				//DMESG("RTL8187 + 8225 Initial Gain State 1: -82 dBm (default)\n");
				write_phy_ofdm(dev, 0x17, 0x26);	mdelay(1);
				write_phy_ofdm(dev, 0x24, 0x86);	mdelay(1);
				write_phy_ofdm(dev, 0x05, 0xfa);	mdelay(1);
				break;
		}
		break;


	default:
		DMESG("UpdateInitialGain(): unknown RFChipID: %#X\n", priv->rf_chip);
		break;
	}
}
#ifdef CONFIG_RTL818X_S
//
//	Description:
//		Tx Power tracking mechanism routine on 87SE.
// 	Created by Roger, 2007.12.11.
//
void
InitTxPwrTracking87SE(
	struct net_device *dev
)
{
	//struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	u32	u4bRfReg;

	u4bRfReg = RF_ReadReg(dev, 0x02);

	// Enable Thermal meter indication.
	//printk("InitTxPwrTracking87SE(): Enable thermal meter indication, Write RF[0x02] = %#x", u4bRfReg|PWR_METER_EN);
	RF_WriteReg(dev, 0x02, u4bRfReg|PWR_METER_EN);			mdelay(1);
}

#endif
void
PhyConfig8185(
	struct net_device *dev
	)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
       write_nic_dword(dev, RCR, priv->ReceiveConfig);
	   priv->RFProgType = read_nic_byte(dev, CONFIG4) & 0x03;
     	// RF config
	switch(priv->rf_chip)
	{
	case RF_ZEBRA2:
	case RF_ZEBRA4:
		ZEBRA_Config_85BASIC_HardCode( dev);
		break;
	}
//{by amy 080312
#ifdef CONFIG_RTL818X_S
	// Set default initial gain state to 4, approved by SD3 DZ, by Bruce, 2007-06-06.
	if(priv->bDigMechanism)
	{
		if(priv->InitialGain == 0)
			priv->InitialGain = 4;
		//printk("PhyConfig8185(): DIG is enabled, set default initial gain index to %d\n", priv->InitialGain);
	}

	//
	// Enable thermal meter indication to implement TxPower tracking on 87SE.
	// We initialize thermal meter here to avoid unsuccessful configuration.
	// Added by Roger, 2007.12.11.
	//
	if(priv->bTxPowerTrack)
		InitTxPwrTracking87SE(dev);

#endif
//by amy 080312}
	priv->InitialGainBackUp= priv->InitialGain;
	UpdateInitialGain(dev);

	return;
}




void
HwConfigureRTL8185(
		struct net_device *dev
		)
{
	//RTL8185_TODO: Determine Retrylimit, TxAGC, AutoRateFallback control.
//	u8		bUNIVERSAL_CONTROL_RL = 1;
        u8              bUNIVERSAL_CONTROL_RL = 0;

	u8		bUNIVERSAL_CONTROL_AGC = 1;
	u8		bUNIVERSAL_CONTROL_ANT = 1;
	u8		bAUTO_RATE_FALLBACK_CTL = 1;
	u8		val8;
	//struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	//struct ieee80211_device *ieee = priv->ieee80211;
      	//if(IS_WIRELESS_MODE_A(dev) || IS_WIRELESS_MODE_G(dev))
//{by amy 080312	if((ieee->mode == IEEE_G)||(ieee->mode == IEEE_A))
//	{
//		write_nic_word(dev, BRSR, 0xffff);
//	}
//	else
//	{
//		write_nic_word(dev, BRSR, 0x000f);
//	}
//by amy 080312}
        write_nic_word(dev, BRSR, 0x0fff);
	// Retry limit
	val8 = read_nic_byte(dev, CW_CONF);

	if(bUNIVERSAL_CONTROL_RL)
		val8 = val8 & 0xfd;
	else
		val8 = val8 | 0x02;

	write_nic_byte(dev, CW_CONF, val8);

	// Tx AGC
	val8 = read_nic_byte(dev, TXAGC_CTL);
	if(bUNIVERSAL_CONTROL_AGC)
	{
		write_nic_byte(dev, CCK_TXAGC, 128);
		write_nic_byte(dev, OFDM_TXAGC, 128);
		val8 = val8 & 0xfe;
	}
	else
	{
		val8 = val8 | 0x01 ;
	}


	write_nic_byte(dev, TXAGC_CTL, val8);

	// Tx Antenna including Feedback control
	val8 = read_nic_byte(dev, TXAGC_CTL );

	if(bUNIVERSAL_CONTROL_ANT)
	{
		write_nic_byte(dev, ANTSEL, 0x00);
		val8 = val8 & 0xfd;
	}
	else
	{
		val8 = val8 & (val8|0x02); //xiong-2006-11-15
	}

	write_nic_byte(dev, TXAGC_CTL, val8);

	// Auto Rate fallback control
	val8 = read_nic_byte(dev, RATE_FALLBACK);
	val8 &= 0x7c;
	if( bAUTO_RATE_FALLBACK_CTL )
	{
		val8 |= RATE_FALLBACK_CTL_ENABLE | RATE_FALLBACK_CTL_AUTO_STEP1;

		// <RJ_TODO_8185B> We shall set up the ARFR according to user's setting.
		//write_nic_word(dev, ARFR, 0x0fff); // set 1M ~ 54M
//by amy
#if 0
		PlatformIOWrite2Byte(dev, ARFR, 0x0fff); 	// set 1M ~ 54M
#endif
#ifdef CONFIG_RTL818X_S
	        // Aadded by Roger, 2007.11.15.
	        PlatformIOWrite2Byte(dev, ARFR, 0x0fff); //set 1M ~ 54Mbps.
#else
		PlatformIOWrite2Byte(dev, ARFR, 0x0c00); //set 48Mbps, 54Mbps.
                // By SD3 szuyi's request. by Roger, 2007.03.26.
#endif
//by amy
	}
	else
	{
	}
	write_nic_byte(dev, RATE_FALLBACK, val8);
}



static void
MacConfig_85BASIC_HardCode(
	struct net_device *dev)
{
	//============================================================================
	// MACREG.TXT
	//============================================================================
	int			nLinesRead = 0;

	u32	u4bRegOffset, u4bRegValue,u4bPageIndex = 0;
	int	i;

	nLinesRead=sizeof(MAC_REG_TABLE)/2;

	for(i = 0; i < nLinesRead; i++)  //nLinesRead=101
	{
		u4bRegOffset=MAC_REG_TABLE[i][0];
		u4bRegValue=MAC_REG_TABLE[i][1];

                if(u4bRegOffset == 0x5e)
                {
                    u4bPageIndex = u4bRegValue;
                }
                else
                {
                    u4bRegOffset |= (u4bPageIndex << 8);
                }
                //DbgPrint("MAC - 0x%x = 0x%x\n", u4bRegOffset, u4bRegValue);
		write_nic_byte(dev, u4bRegOffset, (u8)u4bRegValue);
	}
	//============================================================================
}



static void
MacConfig_85BASIC(
	struct net_device *dev)
{

       u8			u1DA;
	MacConfig_85BASIC_HardCode(dev);

	//============================================================================

	// Follow TID_AC_MAP of WMac.
	write_nic_word(dev, TID_AC_MAP, 0xfa50);

	// Interrupt Migration, Jong suggested we use set 0x0000 first, 2005.12.14, by rcnjko.
	write_nic_word(dev, IntMig, 0x0000);

	// Prevent TPC to cause CRC error. Added by Annie, 2006-06-10.
	PlatformIOWrite4Byte(dev, 0x1F0, 0x00000000);
	PlatformIOWrite4Byte(dev, 0x1F4, 0x00000000);
	PlatformIOWrite1Byte(dev, 0x1F8, 0x00);

	// Asked for by SD3 CM Lin, 2006.06.27, by rcnjko.
	//PlatformIOWrite4Byte(dev, RFTiming, 0x00004001);
//by amy
#if 0
	write_nic_dword(dev, RFTiming, 0x00004001);
#endif
#ifdef CONFIG_RTL818X_S
	// power save parameter based on "87SE power save parameters 20071127.doc", as follow.

	//Enable DA10 TX power saving
	u1DA = read_nic_byte(dev, PHYPR);
	write_nic_byte(dev, PHYPR, (u1DA | BIT2) );

	//POWER:
	write_nic_word(dev, 0x360, 0x1000);
	write_nic_word(dev, 0x362, 0x1000);

	// AFE.
	write_nic_word(dev, 0x370, 0x0560);
	write_nic_word(dev, 0x372, 0x0560);
	write_nic_word(dev, 0x374, 0x0DA4);
	write_nic_word(dev, 0x376, 0x0DA4);
	write_nic_word(dev, 0x378, 0x0560);
	write_nic_word(dev, 0x37A, 0x0560);
	write_nic_word(dev, 0x37C, 0x00EC);
//	write_nic_word(dev, 0x37E, 0x00FE);//-edward
	write_nic_word(dev, 0x37E, 0x00EC);//+edward
#else
       write_nic_dword(dev, RFTiming, 0x00004003);
#endif
       write_nic_byte(dev, 0x24E,0x01);
//by amy

}




u8
GetSupportedWirelessMode8185(
	struct net_device *dev
)
{
	u8			btSupportedWirelessMode = 0;
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);

	switch(priv->rf_chip)
	{
	case RF_ZEBRA2:
	case RF_ZEBRA4:
		btSupportedWirelessMode = (WIRELESS_MODE_B | WIRELESS_MODE_G);
		break;
	default:
		btSupportedWirelessMode = WIRELESS_MODE_B;
		break;
	}

	return btSupportedWirelessMode;
}

void
ActUpdateChannelAccessSetting(
	struct net_device *dev,
	WIRELESS_MODE			WirelessMode,
	PCHANNEL_ACCESS_SETTING	ChnlAccessSetting
	)
{
	struct r8180_priv *priv = ieee80211_priv(dev);
	struct ieee80211_device *ieee = priv->ieee80211;
	AC_CODING	eACI;
	AC_PARAM	AcParam;
	//PSTA_QOS	pStaQos = Adapter->MgntInfo.pStaQos;
	u8	bFollowLegacySetting = 0;
	u8   u1bAIFS;

	//
	// <RJ_TODO_8185B>
	// TODO: We still don't know how to set up these registers, just follow WMAC to
	// verify 8185B FPAG.
	//
	// <RJ_TODO_8185B>
	// Jong said CWmin/CWmax register are not functional in 8185B,
	// so we shall fill channel access realted register into AC parameter registers,
	// even in nQBss.
	//
	ChnlAccessSetting->SIFS_Timer = 0x22; // Suggested by Jong, 2005.12.08.
	ChnlAccessSetting->DIFS_Timer = 0x1C; // 2006.06.02, by rcnjko.
	ChnlAccessSetting->SlotTimeTimer = 9; // 2006.06.02, by rcnjko.
	ChnlAccessSetting->EIFS_Timer = 0x5B; // Suggested by wcchu, it is the default value of EIFS register, 2005.12.08.
	ChnlAccessSetting->CWminIndex = 3; // 2006.06.02, by rcnjko.
	ChnlAccessSetting->CWmaxIndex = 7; // 2006.06.02, by rcnjko.

	write_nic_byte(dev, SIFS, ChnlAccessSetting->SIFS_Timer);
	//Adapter->HalFunc.SetHwRegHandler( Adapter, HW_VAR_SLOT_TIME, &ChnlAccessSetting->SlotTimeTimer );	// Rewrited from directly use PlatformEFIOWrite1Byte(), by Annie, 2006-03-29.
	write_nic_byte(dev, SLOT, ChnlAccessSetting->SlotTimeTimer);	// Rewrited from directly use PlatformEFIOWrite1Byte(), by Annie, 2006-03-29.

	u1bAIFS = aSifsTime + (2 * ChnlAccessSetting->SlotTimeTimer );

	//write_nic_byte(dev, AC_VO_PARAM, u1bAIFS);
	//write_nic_byte(dev, AC_VI_PARAM, u1bAIFS);
	//write_nic_byte(dev, AC_BE_PARAM, u1bAIFS);
	//write_nic_byte(dev, AC_BK_PARAM, u1bAIFS);

	write_nic_byte(dev, EIFS, ChnlAccessSetting->EIFS_Timer);

	write_nic_byte(dev, AckTimeOutReg, 0x5B); // <RJ_EXPR_QOS> Suggested by wcchu, it is the default value of EIFS register, 2005.12.08.

#ifdef TODO
	// <RJ_TODO_NOW_8185B> Update ECWmin/ECWmax, AIFS, TXOP Limit of each AC to the value defined by SPEC.
	if( pStaQos->CurrentQosMode > QOS_DISABLE )
	{ // QoS mode.
		if(pStaQos->QBssWirelessMode == WirelessMode)
		{
			// Follow AC Parameters of the QBSS.
			for(eACI = 0; eACI < AC_MAX; eACI++)
			{
				Adapter->HalFunc.SetHwRegHandler(Adapter, HW_VAR_AC_PARAM, (pu1Byte)(&(pStaQos->WMMParamEle.AcParam[eACI])) );
			}
		}
		else
		{
			// Follow Default WMM AC Parameters.
			bFollowLegacySetting = 1;
		}
	}
	else
#endif
	{ // Legacy 802.11.
		bFollowLegacySetting = 1;

	}

	// this setting is copied from rtl8187B.  xiong-2006-11-13
	if(bFollowLegacySetting)
	{


		//
		// Follow 802.11 seeting to AC parameter, all AC shall use the same parameter.
		// 2005.12.01, by rcnjko.
		//
		AcParam.longData = 0;
		AcParam.f.AciAifsn.f.AIFSN = 2; // Follow 802.11 DIFS.
		AcParam.f.AciAifsn.f.ACM = 0;
		AcParam.f.Ecw.f.ECWmin = ChnlAccessSetting->CWminIndex; // Follow 802.11 CWmin.
		AcParam.f.Ecw.f.ECWmax = ChnlAccessSetting->CWmaxIndex; // Follow 802.11 CWmax.
		AcParam.f.TXOPLimit = 0;

		//lzm reserved 080826
#if 1
#ifdef THOMAS_TURBO
		// For turbo mode setting. port from 87B by Isaiah 2008-08-01
		if( ieee->current_network.Turbo_Enable == 1 )
			AcParam.f.TXOPLimit = 0x01FF;
#endif
		// For 87SE with Intel 4965  Ad-Hoc mode have poor throughput (19MB)
		if (ieee->iw_mode == IW_MODE_ADHOC)
			AcParam.f.TXOPLimit = 0x0020;
#endif

		for(eACI = 0; eACI < AC_MAX; eACI++)
		{
			AcParam.f.AciAifsn.f.ACI = (u8)eACI;
			{
				PAC_PARAM	pAcParam = (PAC_PARAM)(&AcParam);
				AC_CODING	eACI;
				u8		u1bAIFS;
				u32		u4bAcParam;

				// Retrive paramters to udpate.
				eACI = pAcParam->f.AciAifsn.f.ACI;
				u1bAIFS = pAcParam->f.AciAifsn.f.AIFSN * ChnlAccessSetting->SlotTimeTimer + aSifsTime;
				u4bAcParam = (	(((u32)(pAcParam->f.TXOPLimit)) << AC_PARAM_TXOP_LIMIT_OFFSET)	|
						(((u32)(pAcParam->f.Ecw.f.ECWmax)) << AC_PARAM_ECW_MAX_OFFSET)	|
						(((u32)(pAcParam->f.Ecw.f.ECWmin)) << AC_PARAM_ECW_MIN_OFFSET)	|
						(((u32)u1bAIFS) << AC_PARAM_AIFS_OFFSET));

				switch(eACI)
				{
					case AC1_BK:
						//write_nic_dword(dev, AC_BK_PARAM, u4bAcParam);
						break;

					case AC0_BE:
						//write_nic_dword(dev, AC_BE_PARAM, u4bAcParam);
						break;

					case AC2_VI:
						//write_nic_dword(dev, AC_VI_PARAM, u4bAcParam);
						break;

					case AC3_VO:
						//write_nic_dword(dev, AC_VO_PARAM, u4bAcParam);
						break;

					default:
						DMESGW( "SetHwReg8185(): invalid ACI: %d !\n", eACI);
						break;
				}

				// Cehck ACM bit.
				// If it is set, immediately set ACM control bit to downgrading AC for passing WMM testplan. Annie, 2005-12-13.
				//write_nic_byte(dev, ACM_CONTROL, pAcParam->f.AciAifsn);
				{
					PACI_AIFSN	pAciAifsn = (PACI_AIFSN)(&pAcParam->f.AciAifsn);
					AC_CODING	eACI = pAciAifsn->f.ACI;

					//modified Joseph
					//for 8187B AsynIORead issue
#ifdef TODO
					u8	AcmCtrl = pHalData->AcmControl;
#else
					u8	AcmCtrl = 0;
#endif
					if( pAciAifsn->f.ACM )
					{ // ACM bit is 1.
						switch(eACI)
						{
							case AC0_BE:
								AcmCtrl |= (BEQ_ACM_EN|BEQ_ACM_CTL|ACM_HW_EN);	// or 0x21
								break;

							case AC2_VI:
								AcmCtrl |= (VIQ_ACM_EN|VIQ_ACM_CTL|ACM_HW_EN);	// or 0x42
								break;

							case AC3_VO:
								AcmCtrl |= (VOQ_ACM_EN|VOQ_ACM_CTL|ACM_HW_EN);	// or 0x84
								break;

							default:
								DMESGW("SetHwReg8185(): [HW_VAR_ACM_CTRL] ACM set failed: eACI is %d\n", eACI );
								break;
						}
					}
					else
					{ // ACM bit is 0.
						switch(eACI)
						{
							case AC0_BE:
								AcmCtrl &= ( (~BEQ_ACM_EN) & (~BEQ_ACM_CTL) & (~ACM_HW_EN) );	// and 0xDE
								break;

							case AC2_VI:
								AcmCtrl &= ( (~VIQ_ACM_EN) & (~VIQ_ACM_CTL) & (~ACM_HW_EN) );	// and 0xBD
								break;

							case AC3_VO:
								AcmCtrl &= ( (~VOQ_ACM_EN) & (~VOQ_ACM_CTL) & (~ACM_HW_EN) );	// and 0x7B
								break;

							default:
								break;
						}
					}

					//printk(KERN_WARNING "SetHwReg8185(): [HW_VAR_ACM_CTRL] Write 0x%X\n", AcmCtrl);

#ifdef TO_DO
					pHalData->AcmControl = AcmCtrl;
#endif
					//write_nic_byte(dev, ACM_CONTROL, AcmCtrl);
					write_nic_byte(dev, ACM_CONTROL, 0);
				}
			}
		}


	}
}

void
ActSetWirelessMode8185(
	struct net_device *dev,
	u8				btWirelessMode
	)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	struct ieee80211_device *ieee = priv->ieee80211;
	//PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);
	u8	btSupportedWirelessMode = GetSupportedWirelessMode8185(dev);

	if( (btWirelessMode & btSupportedWirelessMode) == 0 )
	{ // Don't switch to unsupported wireless mode, 2006.02.15, by rcnjko.
		DMESGW("ActSetWirelessMode8185(): WirelessMode(%d) is not supported (%d)!\n",
			btWirelessMode, btSupportedWirelessMode);
		return;
	}

	// 1. Assign wireless mode to swtich if necessary.
	if (btWirelessMode == WIRELESS_MODE_AUTO)
	{
		if((btSupportedWirelessMode & WIRELESS_MODE_A))
		{
			btWirelessMode = WIRELESS_MODE_A;
		}
		else if((btSupportedWirelessMode & WIRELESS_MODE_G))
		{
			btWirelessMode = WIRELESS_MODE_G;
		}
		else if((btSupportedWirelessMode & WIRELESS_MODE_B))
		{
			btWirelessMode = WIRELESS_MODE_B;
		}
		else
		{
			DMESGW("ActSetWirelessMode8185(): No valid wireless mode supported, btSupportedWirelessMode(%x)!!!\n",
					 btSupportedWirelessMode);
			btWirelessMode = WIRELESS_MODE_B;
		}
	}


	// 2. Swtich band: RF or BB specific actions,
	// for example, refresh tables in omc8255, or change initial gain if necessary.
	switch(priv->rf_chip)
	{
	case RF_ZEBRA2:
	case RF_ZEBRA4:
		{
			// Nothing to do for Zebra to switch band.
			// Update current wireless mode if we swtich to specified band successfully.
			ieee->mode = (WIRELESS_MODE)btWirelessMode;
		}
		break;

	default:
		DMESGW("ActSetWirelessMode8185(): unsupported RF: 0x%X !!!\n", priv->rf_chip);
		break;
	}

	// 3. Change related setting.
	if( ieee->mode == WIRELESS_MODE_A ){
		DMESG("WIRELESS_MODE_A\n");
	}
	else if( ieee->mode == WIRELESS_MODE_B ){
		DMESG("WIRELESS_MODE_B\n");
	}
	else if( ieee->mode == WIRELESS_MODE_G ){
		DMESG("WIRELESS_MODE_G\n");
	}

	ActUpdateChannelAccessSetting( dev, ieee->mode, &priv->ChannelAccessSetting);
}

void rtl8185b_irq_enable(struct net_device *dev)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);

	priv->irq_enabled = 1;
	write_nic_dword(dev, IMR, priv->IntrMask);
}
//by amy for power save
void
DrvIFIndicateDisassociation(
	struct net_device *dev,
	u16			reason
	)
{
	//printk("==> DrvIFIndicateDisassociation()\n");

	// nothing is needed after disassociation request.

	//printk("<== DrvIFIndicateDisassociation()\n");
}
void
MgntDisconnectIBSS(
	struct net_device *dev
)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	u8			i;

	//printk("XXXXXXXXXX MgntDisconnect IBSS\n");

	DrvIFIndicateDisassociation(dev, unspec_reason);

//	PlatformZeroMemory( pMgntInfo->Bssid, 6 );
	for(i=0;i<6;i++)  priv->ieee80211->current_network.bssid[i] = 0x55;

	priv->ieee80211->state = IEEE80211_NOLINK;

	//Stop Beacon.

	// Vista add a Adhoc profile, HW radio off untill OID_DOT11_RESET_REQUEST
	// Driver would set MSR=NO_LINK, then HW Radio ON, MgntQueue Stuck.
	// Because Bcn DMA isn't complete, mgnt queue would stuck until Bcn packet send.

	// Disable Beacon Queue Own bit, suggested by jong
//	Adapter->HalFunc.SetTxDescOWNHandler(Adapter, BEACON_QUEUE, 0, 0);
	ieee80211_stop_send_beacons(priv->ieee80211);

	priv->ieee80211->link_change(dev);
	notify_wx_assoc_event(priv->ieee80211);

	// Stop SW Beacon.Use hw beacon so do not need to do so.by amy
#if 0
	if(pMgntInfo->bEnableSwBeaconTimer)
	{
		// SwBeaconTimer will stop if pMgntInfo->mIbss==FALSE, see SwBeaconCallback() for details.
// comment out by haich, 2007.10.01
//#if DEV_BUS_TYPE==USB_INTERFACE
		PlatformCancelTimer( Adapter, &pMgntInfo->SwBeaconTimer);
//#endif
	}
#endif

//		MgntIndicateMediaStatus( Adapter, RT_MEDIA_DISCONNECT, GENERAL_INDICATE );

}
void
MlmeDisassociateRequest(
	struct net_device *dev,
	u8*			asSta,
	u8			asRsn
	)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	u8 i;

	SendDisassociation(priv->ieee80211, asSta, asRsn );

	if( memcmp(priv->ieee80211->current_network.bssid, asSta, 6 ) == 0 ){
		//ShuChen TODO: change media status.
		//ShuChen TODO: What to do when disassociate.
		DrvIFIndicateDisassociation(dev, unspec_reason);


	//	pMgntInfo->AsocTimestamp = 0;
		for(i=0;i<6;i++)  priv->ieee80211->current_network.bssid[i] = 0x22;
//		pMgntInfo->mBrates.Length = 0;
//		Adapter->HalFunc.SetHwRegHandler( Adapter, HW_VAR_BASIC_RATE, (pu1Byte)(&pMgntInfo->mBrates) );

		ieee80211_disassociate(priv->ieee80211);


	}

}

void
MgntDisconnectAP(
	struct net_device *dev,
	u8			asRsn
)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);

//
// Commented out by rcnjko, 2005.01.27:
// I move SecClearAllKeys() to MgntActSet_802_11_DISASSOCIATE().
//
//	//2004/09/15, kcwu, the key should be cleared, or the new handshaking will not success
//	SecClearAllKeys(Adapter);

	// In WPA WPA2 need to Clear all key ... because new key will set after new handshaking.
#ifdef TODO
	if(   pMgntInfo->SecurityInfo.AuthMode > RT_802_11AuthModeAutoSwitch ||
		(pMgntInfo->bAPSuportCCKM && pMgntInfo->bCCX8021xenable) )  // In CCKM mode will Clear key
	{
		SecClearAllKeys(Adapter);
		RT_TRACE(COMP_SEC, DBG_LOUD,("======>CCKM clear key..."))
	}
#endif
	// 2004.10.11, by rcnjko.
	//MlmeDisassociateRequest( Adapter, pMgntInfo->Bssid, disas_lv_ss );
	MlmeDisassociateRequest( dev, priv->ieee80211->current_network.bssid, asRsn );

	priv->ieee80211->state = IEEE80211_NOLINK;
//	pMgntInfo->AsocTimestamp = 0;
}
bool
MgntDisconnect(
	struct net_device *dev,
	u8			asRsn
)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	//
	// Schedule an workitem to wake up for ps mode, 070109, by rcnjko.
	//
#ifdef TODO
	if(pMgntInfo->mPss != eAwake)
	{
		//
		// Using AwkaeTimer to prevent mismatch ps state.
		// In the timer the state will be changed according to the RF is being awoke or not. By Bruce, 2007-10-31.
		//
		// PlatformScheduleWorkItem( &(pMgntInfo->AwakeWorkItem) );
		PlatformSetTimer( Adapter, &(pMgntInfo->AwakeTimer), 0 );
	}
#endif

	// Indication of disassociation event.
	//DrvIFIndicateDisassociation(Adapter, asRsn);
#ifdef ENABLE_DOT11D
	if(IS_DOT11D_ENABLE(priv->ieee80211))
		Dot11d_Reset(priv->ieee80211);
#endif
	// In adhoc mode, update beacon frame.
	if( priv->ieee80211->state == IEEE80211_LINKED )
	{
		if( priv->ieee80211->iw_mode == IW_MODE_ADHOC )
		{
//			RT_TRACE(COMP_MLME, DBG_LOUD, ("MgntDisconnect() ===> MgntDisconnectIBSS\n"));
			//printk("MgntDisconnect() ===> MgntDisconnectIBSS\n");
			MgntDisconnectIBSS(dev);
		}
		if( priv->ieee80211->iw_mode == IW_MODE_INFRA )
		{
			// We clear key here instead of MgntDisconnectAP() because that
			// MgntActSet_802_11_DISASSOCIATE() is an interface called by OS,
			// e.g. OID_802_11_DISASSOCIATE in Windows while as MgntDisconnectAP() is
			// used to handle disassociation related things to AP, e.g. send Disassoc
			// frame to AP.  2005.01.27, by rcnjko.
//			SecClearAllKeys(Adapter);

//			RT_TRACE(COMP_MLME, DBG_LOUD, ("MgntDisconnect() ===> MgntDisconnectAP\n"));
			//printk("MgntDisconnect() ===> MgntDisconnectAP\n");
			MgntDisconnectAP(dev, asRsn);
		}

		// Inidicate Disconnect, 2005.02.23, by rcnjko.
//		MgntIndicateMediaStatus( Adapter, RT_MEDIA_DISCONNECT, GENERAL_INDICATE);
	}

	return true;
}
//
//	Description:
//		Chang RF Power State.
//		Note that, only MgntActSet_RF_State() is allowed to set HW_VAR_RF_STATE.
//
//	Assumption:
//		PASSIVE LEVEL.
//
bool
SetRFPowerState(
	struct net_device *dev,
	RT_RF_POWER_STATE	eRFPowerState
	)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	bool			bResult = false;

//	printk("---------> SetRFPowerState(): eRFPowerState(%d)\n", eRFPowerState);
	if(eRFPowerState == priv->eRFPowerState)
	{
//		printk("<--------- SetRFPowerState(): discard the request for eRFPowerState(%d) is the same.\n", eRFPowerState);
		return bResult;
	}

	switch(priv->rf_chip)
	{
		case RF_ZEBRA2:
		case RF_ZEBRA4:
			 bResult = SetZebraRFPowerState8185(dev, eRFPowerState);
			break;

		default:
			printk("SetRFPowerState8185(): unknown RFChipID: 0x%X!!!\n", priv->rf_chip);
			break;;
}
//	printk("<--------- SetRFPowerState(): bResult(%d)\n", bResult);

	return bResult;
}
void
HalEnableRx8185Dummy(
	struct net_device *dev
	)
{
}
void
HalDisableRx8185Dummy(
	struct net_device *dev
	)
{
}

bool
MgntActSet_RF_State(
	struct net_device *dev,
	RT_RF_POWER_STATE	StateToSet,
	u32	ChangeSource
	)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	bool				bActionAllowed = false;
	bool				bConnectBySSID = false;
	RT_RF_POWER_STATE 	rtState;
	u16				RFWaitCounter = 0;
	unsigned long flag;
//	 printk("===>MgntActSet_RF_State(): StateToSet(%d), ChangeSource(0x%x)\n",StateToSet, ChangeSource);
	//
	// Prevent the race condition of RF state change. By Bruce, 2007-11-28.
	// Only one thread can change the RF state at one time, and others should wait to be executed.
	//
#if 1
	while(true)
	{
//		down(&priv->rf_state);
		spin_lock_irqsave(&priv->rf_ps_lock,flag);
		if(priv->RFChangeInProgress)
		{
//			printk("====================>haha111111111\n");
//			up(&priv->rf_state);
//			RT_TRACE(COMP_RF, DBG_LOUD, ("MgntActSet_RF_State(): RF Change in progress! Wait to set..StateToSet(%d).\n", StateToSet));
			spin_unlock_irqrestore(&priv->rf_ps_lock,flag);
			// Set RF after the previous action is done.
			while(priv->RFChangeInProgress)
			{
				RFWaitCounter ++;
//				RT_TRACE(COMP_RF, DBG_LOUD, ("MgntActSet_RF_State(): Wait 1 ms (%d times)...\n", RFWaitCounter));
				udelay(1000); // 1 ms

				// Wait too long, return FALSE to avoid to be stuck here.
				if(RFWaitCounter > 1000) // 1sec
				{
//					RT_ASSERT(FALSE, ("MgntActSet_RF_State(): Wait too logn to set RF\n"));
					printk("MgntActSet_RF_State(): Wait too long to set RF\n");
					// TODO: Reset RF state?
					return false;
				}
			}
		}
		else
		{
//			printk("========================>haha2\n");
			priv->RFChangeInProgress = true;
//			up(&priv->rf_state);
			spin_unlock_irqrestore(&priv->rf_ps_lock,flag);
			break;
		}
	}
#endif
	rtState = priv->eRFPowerState;


	switch(StateToSet)
	{
	case eRfOn:
		//
		// Turn On RF no matter the IPS setting because we need to update the RF state to Ndis under Vista, or
		// the Windows does not allow the driver to perform site survey any more. By Bruce, 2007-10-02.
		//
		priv->RfOffReason &= (~ChangeSource);

		if(! priv->RfOffReason)
		{
			priv->RfOffReason = 0;
			bActionAllowed = true;

			if(rtState == eRfOff && ChangeSource >=RF_CHANGE_BY_HW && !priv->bInHctTest)
			{
				bConnectBySSID = true;
			}
		}
		else
//			RT_TRACE(COMP_RF, DBG_LOUD, ("MgntActSet_RF_State - eRfon reject pMgntInfo->RfOffReason= 0x%x, ChangeSource=0x%X\n", pMgntInfo->RfOffReason, ChangeSource));
			;
		break;

	case eRfOff:
		 // 070125, rcnjko: we always keep connected in AP mode.

			if (priv->RfOffReason > RF_CHANGE_BY_IPS)
			{
				//
				// 060808, Annie:
				// Disconnect to current BSS when radio off. Asked by QuanTa.
				//

				//
				// Calling MgntDisconnect() instead of MgntActSet_802_11_DISASSOCIATE(),
				// because we do NOT need to set ssid to dummy ones.
				// Revised by Roger, 2007.12.04.
				//
				MgntDisconnect( dev, disas_lv_ss );

				// Clear content of bssDesc[] and bssDesc4Query[] to avoid reporting old bss to UI.
				// 2007.05.28, by shien chang.
//				PlatformZeroMemory( pMgntInfo->bssDesc, sizeof(RT_WLAN_BSS)*MAX_BSS_DESC );
//				pMgntInfo->NumBssDesc = 0;
//				PlatformZeroMemory( pMgntInfo->bssDesc4Query, sizeof(RT_WLAN_BSS)*MAX_BSS_DESC );
//				pMgntInfo->NumBssDesc4Query = 0;
			}



		priv->RfOffReason |= ChangeSource;
		bActionAllowed = true;
		break;

	case eRfSleep:
		priv->RfOffReason |= ChangeSource;
		bActionAllowed = true;
		break;

	default:
		break;
	}

	if(bActionAllowed)
	{
//		RT_TRACE(COMP_RF, DBG_LOUD, ("MgntActSet_RF_State(): Action is allowed.... StateToSet(%d), RfOffReason(%#X)\n", StateToSet, pMgntInfo->RfOffReason));
                // Config HW to the specified mode.
//		printk("MgntActSet_RF_State(): Action is allowed.... StateToSet(%d), RfOffReason(%#X)\n", StateToSet, priv->RfOffReason);
		SetRFPowerState(dev, StateToSet);

		// Turn on RF.
		if(StateToSet == eRfOn)
		{
			HalEnableRx8185Dummy(dev);
			if(bConnectBySSID)
			{
			// by amy not supported
//				MgntActSet_802_11_SSID(Adapter, Adapter->MgntInfo.Ssid.Octet, Adapter->MgntInfo.Ssid.Length, TRUE );
			}
		}
		// Turn off RF.
		else if(StateToSet == eRfOff)
		{
			HalDisableRx8185Dummy(dev);
		}
	}
	else
	{
	//	printk("MgntActSet_RF_State(): Action is rejected.... StateToSet(%d), ChangeSource(%#X), RfOffReason(%#X)\n", StateToSet, ChangeSource, priv->RfOffReason);
	}

	// Release RF spinlock
//	down(&priv->rf_state);
	spin_lock_irqsave(&priv->rf_ps_lock,flag);
	priv->RFChangeInProgress = false;
//	up(&priv->rf_state);
	spin_unlock_irqrestore(&priv->rf_ps_lock,flag);
//	printk("<===MgntActSet_RF_State()\n");
	return bActionAllowed;
}
void
InactivePowerSave(
	struct net_device *dev
	)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	//u8 index = 0;

	//
	// This flag "bSwRfProcessing", indicates the status of IPS procedure, should be set if the IPS workitem
	// is really scheduled.
	// The old code, sets this flag before scheduling the IPS workitem and however, at the same time the
	// previous IPS workitem did not end yet, fails to schedule the current workitem. Thus, bSwRfProcessing
	// blocks the IPS procedure of switching RF.
	// By Bruce, 2007-12-25.
	//
	priv->bSwRfProcessing = true;

	MgntActSet_RF_State(dev, priv->eInactivePowerState, RF_CHANGE_BY_IPS);

	//
	// To solve CAM values miss in RF OFF, rewrite CAM values after RF ON. By Bruce, 2007-09-20.
	//
#if 0
	while( index < 4 )
	{
		if( ( pMgntInfo->SecurityInfo.PairwiseEncAlgorithm == WEP104_Encryption ) ||
			(pMgntInfo->SecurityInfo.PairwiseEncAlgorithm == WEP40_Encryption) )
		{
			if( pMgntInfo->SecurityInfo.KeyLen[index] != 0)
			pAdapter->HalFunc.SetKeyHandler(pAdapter, index, 0, FALSE, pMgntInfo->SecurityInfo.PairwiseEncAlgorithm, TRUE, FALSE);

		}
		index++;
	}
#endif
	priv->bSwRfProcessing = false;
}

//
//	Description:
//		Enter the inactive power save mode. RF will be off
//	2007.08.17, by shien chang.
//
void
IPSEnter(
	struct net_device *dev
	)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	RT_RF_POWER_STATE rtState;
	//printk("==============================>enter IPS\n");
	if (priv->bInactivePs)
	{
		rtState = priv->eRFPowerState;

		//
		// Added by Bruce, 2007-12-25.
		// Do not enter IPS in the following conditions:
		// (1) RF is already OFF or Sleep
		// (2) bSwRfProcessing (indicates the IPS is still under going)
		// (3) Connectted (only disconnected can trigger IPS)
		// (4) IBSS (send Beacon)
		// (5) AP mode (send Beacon)
		//
		if (rtState == eRfOn && !priv->bSwRfProcessing
			&& (priv->ieee80211->state != IEEE80211_LINKED ))
		{
	//		printk("IPSEnter(): Turn off RF.\n");
			priv->eInactivePowerState = eRfOff;
			InactivePowerSave(dev);
		}
	}
//	printk("priv->eRFPowerState is %d\n",priv->eRFPowerState);
}
void
IPSLeave(
	struct net_device *dev
	)
{
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
	RT_RF_POWER_STATE rtState;
	//printk("===================================>leave IPS\n");
	if (priv->bInactivePs)
	{
		rtState = priv->eRFPowerState;
		if ((rtState == eRfOff || rtState == eRfSleep) && (!priv->bSwRfProcessing) && priv->RfOffReason <= RF_CHANGE_BY_IPS)
		{
//			printk("IPSLeave(): Turn on RF.\n");
			priv->eInactivePowerState = eRfOn;
			InactivePowerSave(dev);
		}
	}
//	printk("priv->eRFPowerState is %d\n",priv->eRFPowerState);
}
//by amy for power save
void rtl8185b_adapter_start(struct net_device *dev)
{
      struct r8180_priv *priv = ieee80211_priv(dev);
	struct ieee80211_device *ieee = priv->ieee80211;

	u8 SupportedWirelessMode;
	u8			InitWirelessMode;
	u8			bInvalidWirelessMode = 0;
	//int i;
	u8 tmpu8;
    	//u8 u1tmp,u2tmp;
	u8 btCR9346;
	u8 TmpU1b;
	u8 btPSR;

	//rtl8180_rtx_disable(dev);
//{by amy 080312
	write_nic_byte(dev,0x24e, (BIT5|BIT6|BIT0));
//by amy 080312}
	rtl8180_reset(dev);

	priv->dma_poll_mask = 0;
	priv->dma_poll_stop_mask = 0;

	//rtl8180_beacon_tx_disable(dev);

	HwConfigureRTL8185(dev);

	write_nic_dword(dev, MAC0, ((u32*)dev->dev_addr)[0]);
	write_nic_word(dev, MAC4, ((u32*)dev->dev_addr)[1] & 0xffff );

	write_nic_byte(dev, MSR, read_nic_byte(dev, MSR) & 0xf3);	// default network type to 'No	Link'

	//write_nic_byte(dev, BRSR, 0x0);		// Set BRSR= 1M

	write_nic_word(dev, BcnItv, 100);
	write_nic_word(dev, AtimWnd, 2);

	//PlatformEFIOWrite2Byte(dev, FEMR, 0xFFFF);
	PlatformIOWrite2Byte(dev, FEMR, 0xFFFF);

	write_nic_byte(dev, WPA_CONFIG, 0);

	MacConfig_85BASIC(dev);

	// Override the RFSW_CTRL (MAC offset 0x272-0x273), 2006.06.07, by rcnjko.
	// BT_DEMO_BOARD type
	PlatformIOWrite2Byte(dev, RFSW_CTRL, 0x569a);
//by amy
//#ifdef CONFIG_RTL818X_S
		// for jong required
//	PlatformIOWrite2Byte(dev, RFSW_CTRL, 0x9a56);
//#endif
//by amy
	//BT_QA_BOARD
	//PlatformIOWrite2Byte(dev, RFSW_CTRL, 0x9a56);

	//-----------------------------------------------------------------------------
	// Set up PHY related.
	//-----------------------------------------------------------------------------
	// Enable Config3.PARAM_En to revise AnaaParm.
	write_nic_byte(dev, CR9346, 0xc0);	// enable config register write
//by amy
	tmpu8 = read_nic_byte(dev, CONFIG3);
#ifdef CONFIG_RTL818X_S
	write_nic_byte(dev, CONFIG3, (tmpu8 |CONFIG3_PARM_En) );
#else
	write_nic_byte(dev, CONFIG3, (tmpu8 |CONFIG3_PARM_En | CONFIG3_CLKRUN_En) );
#endif
//by amy
	// Turn on Analog power.
	// Asked for by William, otherwise, MAC 3-wire can't work, 2006.06.27, by rcnjko.
	write_nic_dword(dev, ANAPARAM2, ANAPARM2_ASIC_ON);
	write_nic_dword(dev, ANAPARAM, ANAPARM_ASIC_ON);
//by amy
#ifdef CONFIG_RTL818X_S
	write_nic_word(dev, ANAPARAM3, 0x0010);
#else
      write_nic_byte(dev, ANAPARAM3, 0x00);
#endif
//by amy

	write_nic_byte(dev, CONFIG3, tmpu8);
	write_nic_byte(dev, CR9346, 0x00);
//{by amy 080312 for led
	// enable EEM0 and EEM1 in 9346CR
	btCR9346 = read_nic_byte(dev, CR9346);
	write_nic_byte(dev, CR9346, (btCR9346|0xC0) );

	// B cut use LED1 to control HW RF on/off
	TmpU1b = read_nic_byte(dev, CONFIG5);
	TmpU1b = TmpU1b & ~BIT3;
	write_nic_byte(dev,CONFIG5, TmpU1b);

	// disable EEM0 and EEM1 in 9346CR
	btCR9346 &= ~(0xC0);
	write_nic_byte(dev, CR9346, btCR9346);

	//Enable Led (suggested by Jong)
	// B-cut RF Radio on/off  5e[3]=0
	btPSR = read_nic_byte(dev, PSR);
	write_nic_byte(dev, PSR, (btPSR | BIT3));
//by amy 080312 for led}
	// setup initial timing for RFE.
	write_nic_word(dev, RFPinsOutput, 0x0480);
	SetOutputEnableOfRfPins(dev);
	write_nic_word(dev, RFPinsSelect, 0x2488);

	// PHY config.
	PhyConfig8185(dev);

	// We assume RegWirelessMode has already been initialized before,
	// however, we has to validate the wireless mode here and provide a reasonble
	// initialized value if necessary. 2005.01.13, by rcnjko.
	SupportedWirelessMode = GetSupportedWirelessMode8185(dev);
	if(	(ieee->mode != WIRELESS_MODE_B) &&
		(ieee->mode != WIRELESS_MODE_G) &&
		(ieee->mode != WIRELESS_MODE_A) &&
		(ieee->mode != WIRELESS_MODE_AUTO))
	{ // It should be one of B, G, A, or AUTO.
		bInvalidWirelessMode = 1;
	}
	else
	{ // One of B, G, A, or AUTO.
		// Check if the wireless mode is supported by RF.
		if( (ieee->mode != WIRELESS_MODE_AUTO) &&
			(ieee->mode & SupportedWirelessMode) == 0 )
		{
			bInvalidWirelessMode = 1;
		}
	}

	if(bInvalidWirelessMode || ieee->mode==WIRELESS_MODE_AUTO)
	{ // Auto or other invalid value.
		// Assigne a wireless mode to initialize.
		if((SupportedWirelessMode & WIRELESS_MODE_A))
		{
			InitWirelessMode = WIRELESS_MODE_A;
		}
		else if((SupportedWirelessMode & WIRELESS_MODE_G))
		{
			InitWirelessMode = WIRELESS_MODE_G;
		}
		else if((SupportedWirelessMode & WIRELESS_MODE_B))
		{
			InitWirelessMode = WIRELESS_MODE_B;
		}
		else
		{
			DMESGW("InitializeAdapter8185(): No valid wireless mode supported, SupportedWirelessMode(%x)!!!\n",
				 SupportedWirelessMode);
			InitWirelessMode = WIRELESS_MODE_B;
		}

		// Initialize RegWirelessMode if it is not a valid one.
		if(bInvalidWirelessMode)
		{
			ieee->mode = (WIRELESS_MODE)InitWirelessMode;
		}
	}
	else
	{ // One of B, G, A.
		InitWirelessMode = ieee->mode;
	}
//by amy for power save
#ifdef ENABLE_IPS
//	printk("initialize ENABLE_IPS\n");
	priv->eRFPowerState = eRfOff;
	priv->RfOffReason = 0;
	{
	//	u32 tmp2;
	//	u32 tmp = jiffies;
		MgntActSet_RF_State(dev, eRfOn, 0);
	//	tmp2 = jiffies;
	//	printk("rf on cost jiffies:%lx\n", (tmp2-tmp)*1000/HZ);
	}
//	DrvIFIndicateCurrentPhyStatus(priv);
		//
		// If inactive power mode is enabled, disable rf while in disconnected state.
		// 2007.07.16, by shien chang.
		//
	if (priv->bInactivePs)
	{
	//	u32 tmp2;
	//	u32 tmp = jiffies;
		MgntActSet_RF_State(dev,eRfOff, RF_CHANGE_BY_IPS);
	//	tmp2 = jiffies;
	//	printk("rf off cost jiffies:%lx\n", (tmp2-tmp)*1000/HZ);

	}
#endif
//	IPSEnter(dev);
//by amy for power save
#ifdef TODO
	// Turn off RF if necessary. 2005.08.23, by rcnjko.
	// We shall turn off RF after setting CMDR, otherwise,
	// RF will be turnned on after we enable MAC Tx/Rx.
	if(Adapter->MgntInfo.RegRfOff == TRUE)
	{
		SetRFPowerState8185(Adapter, RF_OFF);
	}
	else
	{
		SetRFPowerState8185(Adapter, RF_ON);
	}
#endif

/*   //these is equal with above TODO.
	write_nic_byte(dev, CR9346, 0xc0);	// enable config register write
	write_nic_byte(dev, CONFIG3, read_nic_byte(dev, CONFIG3) | CONFIG3_PARM_En);
	RF_WriteReg(dev, 0x4, 0x9FF);
	write_nic_dword(dev, ANAPARAM2, ANAPARM2_ASIC_ON);
	write_nic_dword(dev, ANAPARAM, ANAPARM_ASIC_ON);
	write_nic_byte(dev, CONFIG3, (read_nic_byte(dev, CONFIG3)&(~CONFIG3_PARM_En)));
	write_nic_byte(dev, CR9346, 0x00);
*/

	ActSetWirelessMode8185(dev, (u8)(InitWirelessMode));

	//-----------------------------------------------------------------------------

	rtl8185b_irq_enable(dev);

	netif_start_queue(dev);

 }


void rtl8185b_rx_enable(struct net_device *dev)
{
	u8 cmd;
	//u32 rxconf;
	/* for now we accept data, management & ctl frame*/
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);
#if 0
	rxconf=read_nic_dword(dev,RX_CONF);
	rxconf = rxconf &~ MAC_FILTER_MASK;
	rxconf = rxconf | (1<<ACCEPT_MNG_FRAME_SHIFT);
	rxconf = rxconf | (1<<ACCEPT_DATA_FRAME_SHIFT);
	rxconf = rxconf | (1<<ACCEPT_BCAST_FRAME_SHIFT);
	rxconf = rxconf | (1<<ACCEPT_MCAST_FRAME_SHIFT);
//	rxconf = rxconf | (1<<ACCEPT_CRCERR_FRAME_SHIFT);
	if (dev->flags & IFF_PROMISC) DMESG ("NIC in promisc mode");

	if(priv->ieee80211->iw_mode == IW_MODE_MONITOR || \
	   dev->flags & IFF_PROMISC){
		rxconf = rxconf | (1<<ACCEPT_ALLMAC_FRAME_SHIFT);
	}else{
		rxconf = rxconf | (1<<ACCEPT_NICMAC_FRAME_SHIFT);
		if(priv->card_8185 == 0)
			rxconf = rxconf | (1<<RX_CHECK_BSSID_SHIFT);
	}

	/*if(priv->ieee80211->iw_mode == IW_MODE_MASTER){
		rxconf = rxconf | (1<<ACCEPT_ALLMAC_FRAME_SHIFT);
		rxconf = rxconf | (1<<RX_CHECK_BSSID_SHIFT);
	}*/

	if(priv->ieee80211->iw_mode == IW_MODE_MONITOR){
		rxconf = rxconf | (1<<ACCEPT_CTL_FRAME_SHIFT);
		rxconf = rxconf | (1<<ACCEPT_ICVERR_FRAME_SHIFT);
		rxconf = rxconf | (1<<ACCEPT_PWR_FRAME_SHIFT);
	}

	if( priv->crcmon == 1 && priv->ieee80211->iw_mode == IW_MODE_MONITOR)
		rxconf = rxconf | (1<<ACCEPT_CRCERR_FRAME_SHIFT);

	//if(!priv->card_8185){
		rxconf = rxconf &~ RX_FIFO_THRESHOLD_MASK;
		rxconf = rxconf | (RX_FIFO_THRESHOLD_NONE<<RX_FIFO_THRESHOLD_SHIFT);
	//}

	rxconf = rxconf | (1<<RX_AUTORESETPHY_SHIFT);
	rxconf = rxconf &~ MAX_RX_DMA_MASK;
	rxconf = rxconf | (MAX_RX_DMA_2048<<MAX_RX_DMA_SHIFT);

	//if(!priv->card_8185)
		rxconf = rxconf | RCR_ONLYERLPKT;

	rxconf = rxconf &~ RCR_CS_MASK;
	if(!priv->card_8185)
		rxconf |= (priv->rcr_csense<<RCR_CS_SHIFT);
//	rxconf &=~ 0xfff00000;
//	rxconf |= 0x90100000;//9014f76f;
	write_nic_dword(dev, RX_CONF, rxconf);
#endif

	if (dev->flags & IFF_PROMISC) DMESG ("NIC in promisc mode");

	if(priv->ieee80211->iw_mode == IW_MODE_MONITOR || \
	   dev->flags & IFF_PROMISC){
	   	priv->ReceiveConfig = priv->ReceiveConfig & (~RCR_APM);
		priv->ReceiveConfig = priv->ReceiveConfig | RCR_AAP;
	}

	/*if(priv->ieee80211->iw_mode == IW_MODE_MASTER){
		rxconf = rxconf | (1<<ACCEPT_ALLMAC_FRAME_SHIFT);
		rxconf = rxconf | (1<<RX_CHECK_BSSID_SHIFT);
	}*/

	if(priv->ieee80211->iw_mode == IW_MODE_MONITOR){
		priv->ReceiveConfig = priv->ReceiveConfig | RCR_ACF | RCR_APWRMGT | RCR_AICV;
	}

	if( priv->crcmon == 1 && priv->ieee80211->iw_mode == IW_MODE_MONITOR)
		priv->ReceiveConfig = priv->ReceiveConfig | RCR_ACRC32;

	write_nic_dword(dev, RCR, priv->ReceiveConfig);

	fix_rx_fifo(dev);

#ifdef DEBUG_RX
	DMESG("rxconf: %x %x",priv->ReceiveConfig ,read_nic_dword(dev,RCR));
#endif
	cmd=read_nic_byte(dev,CMD);
	write_nic_byte(dev,CMD,cmd | (1<<CMD_RX_ENABLE_SHIFT));

}

void rtl8185b_tx_enable(struct net_device *dev)
{
	u8 cmd;
	//u8 tx_agc_ctl;
	u8 byte;
	//u32 txconf;
	struct r8180_priv *priv = (struct r8180_priv *)ieee80211_priv(dev);

#if 0
	txconf= read_nic_dword(dev,TX_CONF);
	if(priv->card_8185){


		byte = read_nic_byte(dev,CW_CONF);
		byte &= ~(1<<CW_CONF_PERPACKET_CW_SHIFT);
		byte &= ~(1<<CW_CONF_PERPACKET_RETRY_SHIFT);
		write_nic_byte(dev, CW_CONF, byte);

		tx_agc_ctl = read_nic_byte(dev, TX_AGC_CTL);
		tx_agc_ctl &= ~(1<<TX_AGC_CTL_PERPACKET_GAIN_SHIFT);
		tx_agc_ctl &= ~(1<<TX_AGC_CTL_PERPACKET_ANTSEL_SHIFT);
		tx_agc_ctl |=(1<<TX_AGC_CTL_FEEDBACK_ANT);
		write_nic_byte(dev, TX_AGC_CTL, tx_agc_ctl);
		/*
		write_nic_word(dev, 0x5e, 0x01);
		force_pci_posting(dev);
		mdelay(1);
		write_nic_word(dev, 0xfe, 0x10);
		force_pci_posting(dev);
		mdelay(1);
		write_nic_word(dev, 0x5e, 0x00);
		force_pci_posting(dev);
		mdelay(1);
		*/
		write_nic_byte(dev, 0xec, 0x3f); /* Disable early TX */
	}

	if(priv->card_8185){

		txconf = txconf &~ (1<<TCR_PROBE_NOTIMESTAMP_SHIFT);

	}else{

		if(hwseqnum)
			txconf= txconf &~ (1<<TX_CONF_HEADER_AUTOICREMENT_SHIFT);
		else
			txconf= txconf | (1<<TX_CONF_HEADER_AUTOICREMENT_SHIFT);
	}

	txconf = txconf &~ TX_LOOPBACK_MASK;
	txconf = txconf | (TX_LOOPBACK_NONE <<TX_LOOPBACK_SHIFT);
	txconf = txconf &~ TCR_DPRETRY_MASK;
	txconf = txconf &~ TCR_RTSRETRY_MASK;
	txconf = txconf | (priv->retry_data<<TX_DPRETRY_SHIFT);
	txconf = txconf | (priv->retry_rts<<TX_RTSRETRY_SHIFT);
	txconf = txconf &~ (1<<TX_NOCRC_SHIFT);

	if(priv->card_8185){
		if(priv->hw_plcp_len)
			txconf = txconf &~ TCR_PLCP_LEN;
		else
			txconf = txconf | TCR_PLCP_LEN;
	}else{
		txconf = txconf &~ TCR_SAT;
	}
	txconf = txconf &~ TCR_MXDMA_MASK;
	txconf = txconf | (TCR_MXDMA_2048<<TCR_MXDMA_SHIFT);
	txconf = txconf | TCR_CWMIN;
	txconf = txconf | TCR_DISCW;

//	if(priv->ieee80211->hw_wep)
//		txconf=txconf &~ (1<<TX_NOICV_SHIFT);
//	else
		txconf=txconf | (1<<TX_NOICV_SHIFT);

	write_nic_dword(dev,TX_CONF,txconf);
#endif

	write_nic_dword(dev, TCR, priv->TransmitConfig);
	byte = read_nic_byte(dev, MSR);
	byte |= MSR_LINK_ENEDCA;
	write_nic_byte(dev, MSR, byte);

	fix_tx_fifo(dev);

#ifdef DEBUG_TX
	DMESG("txconf: %x %x",priv->TransmitConfig,read_nic_dword(dev,TCR));
#endif

	cmd=read_nic_byte(dev,CMD);
	write_nic_byte(dev,CMD,cmd | (1<<CMD_TX_ENABLE_SHIFT));

	//write_nic_dword(dev,TX_CONF,txconf);


/*
	rtl8180_set_mode(dev,EPROM_CMD_CONFIG);
	write_nic_byte(dev, TX_DMA_POLLING, priv->dma_poll_mask);
	rtl8180_set_mode(dev,EPROM_CMD_NORMAL);
	*/
}


#endif
