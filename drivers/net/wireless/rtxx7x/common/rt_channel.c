/*
 *************************************************************************
 * Ralink Tech Inc.
 * 5F., No.36, Taiyuan St., Jhubei City,
 * Hsinchu County 302,
 * Taiwan, R.O.C.
 *
 * (c) Copyright 2002-2010, Ralink Technology, Inc.
 *
 * This program is free software; you can redistribute it and/or modify  *
 * it under the terms of the GNU General Public License as published by  *
 * the Free Software Foundation; either version 2 of the License, or     *
 * (at your option) any later version.                                   *
 *                                                                       *
 * This program is distributed in the hope that it will be useful,       *
 * but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 * GNU General Public License for more details.                          *
 *                                                                       *
 * You should have received a copy of the GNU General Public License     *
 * along with this program; if not, write to the                         *
 * Free Software Foundation, Inc.,                                       *
 * 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 *                                                                       *
 *************************************************************************/


#include "rt_config.h"


CH_FREQ_MAP CH_HZ_ID_MAP[]=
		{
			{1, 2412},
			{2, 2417},
			{3, 2422},
			{4, 2427},
			{5, 2432},
			{6, 2437},
			{7, 2442},
			{8, 2447},
			{9, 2452},
			{10, 2457},
			{11, 2462},
			{12, 2467},
			{13, 2472},
			{14, 2484},

			/*  UNII */
			{36, 5180},
			{40, 5200},
			{44, 5220},
			{48, 5240},
			{52, 5260},
			{56, 5280},
			{60, 5300},
			{64, 5320},
			{149, 5745},
			{153, 5765},
			{157, 5785},
			{161, 5805},
			{165, 5825},
			{167, 5835},
			{169, 5845},
			{171, 5855},
			{173, 5865},
						
			/* HiperLAN2 */
			{100, 5500},
			{104, 5520},
			{108, 5540},
			{112, 5560},
			{116, 5580},
			{120, 5600},
			{124, 5620},
			{128, 5640},
			{132, 5660},
			{136, 5680},
			{140, 5700},
						
			/* Japan MMAC */
			{34, 5170},
			{38, 5190},
			{42, 5210},
			{46, 5230},
					
			/*  Japan */
			{184, 4920},
			{188, 4940},
			{192, 4960},
			{196, 4980},
			
			{208, 5040},	/* Japan, means J08 */
			{212, 5060},	/* Japan, means J12 */   
			{216, 5080},	/* Japan, means J16 */
};

INT	CH_HZ_ID_MAP_NUM = (sizeof(CH_HZ_ID_MAP)/sizeof(CH_FREQ_MAP));

CH_DESC Country_Region0_ChDesc_2GHZ[] =
{
	{1, 11, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region1_ChDesc_2GHZ[] =
{
	{1, 13, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region2_ChDesc_2GHZ[] =
{
	{10, 2, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region3_ChDesc_2GHZ[] =
{
	{10, 4, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region4_ChDesc_2GHZ[] =
{
	{14, 1, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region5_ChDesc_2GHZ[] =
{
	{1, 14, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region6_ChDesc_2GHZ[] =
{
	{3, 7, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region7_ChDesc_2GHZ[] =
{
	{5, 9, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region31_ChDesc_2GHZ[] =
{
	{1, 11, CHANNEL_DEFAULT_PROP},
	{12, 3, CHANNEL_PASSIVE_SCAN},
	{}
};

CH_DESC Country_Region32_ChDesc_2GHZ[] =
{
	{1, 11, CHANNEL_DEFAULT_PROP},
	{12, 2, CHANNEL_PASSIVE_SCAN},	
	{}
};

CH_DESC Country_Region33_ChDesc_2GHZ[] =
{
	{1, 14, CHANNEL_DEFAULT_PROP},
	{}
};

COUNTRY_REGION_CH_DESC Country_Region_ChDesc_2GHZ[] =
{
	{REGION_0_BG_BAND, Country_Region0_ChDesc_2GHZ},
	{REGION_1_BG_BAND, Country_Region1_ChDesc_2GHZ},
	{REGION_2_BG_BAND, Country_Region2_ChDesc_2GHZ},
	{REGION_3_BG_BAND, Country_Region3_ChDesc_2GHZ},
	{REGION_4_BG_BAND, Country_Region4_ChDesc_2GHZ},
	{REGION_5_BG_BAND, Country_Region5_ChDesc_2GHZ},
	{REGION_6_BG_BAND, Country_Region6_ChDesc_2GHZ},
	{REGION_7_BG_BAND, Country_Region7_ChDesc_2GHZ},
	{REGION_31_BG_BAND, Country_Region31_ChDesc_2GHZ},
	{REGION_32_BG_BAND, Country_Region32_ChDesc_2GHZ},
	{REGION_33_BG_BAND, Country_Region33_ChDesc_2GHZ},
	{}
};

UINT16 const Country_Region_GroupNum_2GHZ = sizeof(Country_Region_ChDesc_2GHZ) / sizeof(COUNTRY_REGION_CH_DESC);

CH_DESC Country_Region0_ChDesc_5GHZ[] =
{
	{36, 8, CHANNEL_DEFAULT_PROP},
	{149, 5, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region1_ChDesc_5GHZ[] =
{
	{36, 8, CHANNEL_DEFAULT_PROP},
	{100, 11, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region2_ChDesc_5GHZ[] =
{
	{36, 8, CHANNEL_DEFAULT_PROP},
	{}	
};

CH_DESC Country_Region3_ChDesc_5GHZ[] =
{
	{52, 4, CHANNEL_DEFAULT_PROP},
	{149, 4, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region4_ChDesc_5GHZ[] =
{
	{149, 5, CHANNEL_DEFAULT_PROP},
	{}
};
CH_DESC Country_Region5_ChDesc_5GHZ[] =
{
	{149, 4, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region6_ChDesc_5GHZ[] =
{
	{36, 4, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region7_ChDesc_5GHZ[] =
{
	{36, 8, CHANNEL_DEFAULT_PROP},
	{100, 11, CHANNEL_DEFAULT_PROP},
	{149, 7, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region8_ChDesc_5GHZ[] =
{
	{52, 4, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region9_ChDesc_5GHZ[] =
{
	{36, 8 , CHANNEL_DEFAULT_PROP},
	{100, 5, CHANNEL_DEFAULT_PROP},
	{132, 3, CHANNEL_DEFAULT_PROP},
	{149, 5, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region10_ChDesc_5GHZ[] =
{
	{36,4, CHANNEL_DEFAULT_PROP},
	{149, 5, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region11_ChDesc_5GHZ[] =
{
	{36, 8, CHANNEL_DEFAULT_PROP},
	{100, 6, CHANNEL_DEFAULT_PROP},
	{149, 4, CHANNEL_DEFAULT_PROP},
	{}		
};

CH_DESC Country_Region12_ChDesc_5GHZ[] =
{
	{36, 8, CHANNEL_DEFAULT_PROP},
	{100, 11, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region13_ChDesc_5GHZ[] =
{
	{52, 4, CHANNEL_DEFAULT_PROP},
	{100, 11, CHANNEL_DEFAULT_PROP},
	{149, 4, CHANNEL_DEFAULT_PROP},
	{}	
};

CH_DESC Country_Region14_ChDesc_5GHZ[] =
{
	{36, 8, CHANNEL_DEFAULT_PROP},
	{100, 5, CHANNEL_DEFAULT_PROP},
	{136, 2, CHANNEL_DEFAULT_PROP},
	{149, 5, CHANNEL_DEFAULT_PROP},
	{}	
};

CH_DESC Country_Region15_ChDesc_5GHZ[] =
{
	{149, 7, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region16_ChDesc_5GHZ[] =
{
	{52, 4, CHANNEL_DEFAULT_PROP},
	{149, 5, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region17_ChDesc_5GHZ[] =
{
	{36, 4, CHANNEL_DEFAULT_PROP},
	{149, 4, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region18_ChDesc_5GHZ[] =
{
	{36, 8, CHANNEL_DEFAULT_PROP},
	{100, 5, CHANNEL_DEFAULT_PROP},
	{132, 3, CHANNEL_DEFAULT_PROP},
	{}	
};

CH_DESC Country_Region19_ChDesc_5GHZ[] =
{
	{56, 3, CHANNEL_DEFAULT_PROP},
	{100, 11, CHANNEL_DEFAULT_PROP},
	{149, 4, CHANNEL_DEFAULT_PROP},
	{}
};

CH_DESC Country_Region20_ChDesc_5GHZ[] =
{
	{36, 8, CHANNEL_DEFAULT_PROP},
	{100, 7, CHANNEL_DEFAULT_PROP},
	{149, 4, CHANNEL_DEFAULT_PROP},
	{}		
};

CH_DESC Country_Region21_ChDesc_5GHZ[] =
{
	{36, 8, CHANNEL_DEFAULT_PROP},
	{100, 11, CHANNEL_DEFAULT_PROP},
	{149, 4, CHANNEL_DEFAULT_PROP},
	{}		
};


COUNTRY_REGION_CH_DESC Country_Region_ChDesc_5GHZ[] =
{
	{REGION_0_A_BAND, Country_Region0_ChDesc_5GHZ},
	{REGION_1_A_BAND, Country_Region1_ChDesc_5GHZ},
	{REGION_2_A_BAND, Country_Region2_ChDesc_5GHZ},
	{REGION_3_A_BAND, Country_Region3_ChDesc_5GHZ},
	{REGION_4_A_BAND, Country_Region4_ChDesc_5GHZ},
	{REGION_5_A_BAND, Country_Region5_ChDesc_5GHZ},
	{REGION_6_A_BAND, Country_Region6_ChDesc_5GHZ},
	{REGION_7_A_BAND, Country_Region7_ChDesc_5GHZ},
	{REGION_8_A_BAND, Country_Region8_ChDesc_5GHZ},
	{REGION_9_A_BAND, Country_Region9_ChDesc_5GHZ},
	{REGION_10_A_BAND, Country_Region10_ChDesc_5GHZ},
	{REGION_11_A_BAND, Country_Region11_ChDesc_5GHZ},
	{REGION_12_A_BAND, Country_Region12_ChDesc_5GHZ},
	{REGION_13_A_BAND, Country_Region13_ChDesc_5GHZ},
	{REGION_14_A_BAND, Country_Region14_ChDesc_5GHZ},
	{REGION_15_A_BAND, Country_Region15_ChDesc_5GHZ},
	{REGION_16_A_BAND, Country_Region16_ChDesc_5GHZ},
	{REGION_17_A_BAND, Country_Region17_ChDesc_5GHZ},
	{REGION_18_A_BAND, Country_Region18_ChDesc_5GHZ},
	{REGION_19_A_BAND, Country_Region19_ChDesc_5GHZ},
	{REGION_20_A_BAND, Country_Region20_ChDesc_5GHZ},
	{REGION_21_A_BAND, Country_Region21_ChDesc_5GHZ},
	{}
};

UINT16 const Country_Region_GroupNum_5GHZ = sizeof(Country_Region_ChDesc_5GHZ) / sizeof(COUNTRY_REGION_CH_DESC);

UINT16 TotalChNum(PCH_DESC pChDesc)
{
	UINT16 TotalChNum = 0;
	
	while(pChDesc->FirstChannel)
	{
		TotalChNum += pChDesc->NumOfCh;
		pChDesc++;
	}
	
	return TotalChNum;
}

UCHAR GetChannel_5GHZ(PCH_DESC pChDesc, UCHAR index)
{
	while (pChDesc->FirstChannel)
	{
		if (index < pChDesc->NumOfCh)
			return pChDesc->FirstChannel + index * 4;
		else
		{
			index -= pChDesc->NumOfCh;
			pChDesc++;
		}
	}

	return 0;
}

UCHAR GetChannel_2GHZ(PCH_DESC pChDesc, UCHAR index)
{

	while (pChDesc->FirstChannel)
	{
		if (index < pChDesc->NumOfCh)
			return pChDesc->FirstChannel + index;
		else
		{
			index -= pChDesc->NumOfCh;
			pChDesc++;
		}
	}

	return 0;
}

UCHAR GetChannelFlag(PCH_DESC pChDesc, UCHAR index)
{

	while (pChDesc->FirstChannel)
	{
		if (index < pChDesc->NumOfCh)
			return pChDesc->ChannelProp;
		else
		{
			index -= pChDesc->NumOfCh;
			pChDesc++;
		}
	}

	return 0;
}

#ifdef EXT_BUILD_CHANNEL_LIST
CH_REGION ChRegion[] =
{
		{	/* Antigua and Berbuda*/
			"AG",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, FALSE},	/* 5G, ch 100~140*/
				{ 0},							/* end*/
			}
		},

		{	/* Argentina*/ 
			"AR",
			CE,
			{
				{ 1,   11, 30, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 52,  4,  30, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 149, 5,  30, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Aruba*/
			"AW",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, FALSE},	/* 5G, ch 100~140*/
				{ 0},							/* end*/
			}
		},

		{	/* Australia*/ 
			"AU",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, FALSE},	/* 5G, ch 100~140*/
			/*	{ 149,    4,  N/A, BOTH, FALSE},		5G, ch 149~161 No Rf power */
				{ 0},							/* end*/
			}
		},

		{	/* Austria*/ 
			"AT",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE},		/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE},		/* 5G, ch 100~140*/
			/*	{ 149,     7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Bahamas*/
			"BS",
			CE,
			{
				{ 1,   11, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  17, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  24, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 100, 11, 24, BOTH, TRUE},		/* 5G, ch 100~140*/
				{ 149, 5,  30, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Barbados*/
			"BB",
			CE,
			{
				{ 1,   11, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  17, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  24, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 100, 11, 24, BOTH, TRUE}, 	/* 5G, ch 100~140*/
				{ 149, 5,  30, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Bermuda*/
			"BM",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  24, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, FALSE},	/* 5G, ch 100~140*/
				{ 0},							/* end*/
			}
		},

		{	/* Brazil*/
			"BR",
			CE,
			{
				{ 1,   11, 30, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, FALSE},	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE},		/* 5G, ch 100~140*/
				{ 149, 5,  30, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Belgium*/
			"BE",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Bulgaria*/
			"BG",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},
		{	/* Bolivia*/
			"BO",
			CE,
			{
				{ 1,   13, 30, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  17, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  24, BOTH, FALSE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 24, BOTH, FALSE}, 	/* 5G, ch 100~140*/
				{ 149, 5,  30, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Canada*/
			"CA",
			CE,
			{
				{ 1,   13, 30, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  17, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  24, BOTH, TRUE},		/* 5G, ch 52~64*/
				{ 100, 8,  24, BOTH, TRUE}, 	/* 5G, ch 100~140, no ch 120~128 */
				{ 149, 5,  30, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Cayman IsLands*/
			"KY",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  24, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, FALSE},	/* 5G, ch 100~140*/
				{ 0},							/* end*/
			}
		},

		{	/* Chile*/
			"CL",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  22, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  22, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 149, 5,  22, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* China*/
			"CN",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 149, 5,  33, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Colombia*/
			"CO",
			CE,
			{
				{ 1,   11, 30, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  17, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  24, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, FALSE},	/* 5G, ch 100~140*/
				{ 0},							/* end*/
			}
		},

		{	/* Costa Rica*/
			"CR",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  17, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  24, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 149, 4,  30, BOTH, FALSE},	/* 5G, ch 149~161*/
				{ 0},							/* end*/
			}
		},

		{	/* Cyprus*/
			"CY",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  24, IDOR, TRUE},		/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE},		/* 5G, ch 100~140*/
				{ 0},							/* end*/
			}
		},

		{	/* Czech_Republic*/
			"CZ",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/

			}
		},

		{	/* Denmark*/
			"DK",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/

			}
		},

		{	/* Dominican Republic*/
			"DO",
			CE,
			{
				{ 1,   11, 30, BOTH, FALSE},	/* 2.4 G, ch 0*/
				{ 36,  4,  17, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  24, IDOR, FALSE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 24, BOTH, FALSE}, 	/* 5G, ch 100~140*/
				{ 149, 5,  30, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Equador*/
			"EC",
			CE,
			{
				{   1, 13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{  36,  4, 17, IDOR, FALSE},	/* 5G, ch 36~48*/
				{  52,  4, 24, IDOR, FALSE},	/* 5G, ch 52~64*/
				{ 100, 11, 24, BOTH, FALSE},	/* 5G, ch 100~140*/
				{ 149,  5, 30, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* El Salvador*/
			"SV",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,   23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,   30, BOTH, TRUE},	/* 5G, ch 52~64*/
				{ 149, 4,   36, BOTH, TRUE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Finland*/
			"FI",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* France*/
			"FR",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,   4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,   4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Germany*/
			"DE",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,   4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,   4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Greece*/
			"GR",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,   4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,   4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Guam*/
			"GU",
			CE,
			{
				{ 1,   11,  20, BOTH, FALSE},	/* 2.4 G, ch 1~11*/
				{ 36,  4,   17, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,   24, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 100, 11,  30, BOTH, FALSE},	/* 5G, ch 100~140*/
				{ 149,  5,  30, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Guatemala*/
			"GT",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,   17, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,   24, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 149,  4,  30, BOTH, FALSE},	/* 5G, ch 149~161*/
				{ 0},							/* end*/
			}
		},

		{	/* Haiti*/
			"HT",
			CE,
			{
				{   1, 13,  20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{  36,  4,  17, BOTH, FALSE},	/* 5G, ch 36~48*/
				{  52,  4,  24, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 100, 11,  24, BOTH, FALSE}, 	/* 5G, ch 100~140*/
				{ 149,  5,  30, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Honduras*/
			"HN",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 149,  4,  27, BOTH, FALSE},	/* 5G, ch 149~161*/
				{ 0},							/* end*/
			}
		},

		{	/* Hong Kong*/
			"HK",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,   4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,   4,  23, IDOR, FALSE},	/* 5G, ch 52~64*/
				{ 100, 11,  30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
				{ 149,  4,  36, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Hungary*/
			"HU",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Iceland*/
			"IS",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,   4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,   4,  23, IDOR, TRUE},	/* 5G, ch 52~64*/
				{ 100, 11,  30, BOTH, TRUE},	/* 5G, ch 100~140*/
				{ 0},							/* end*/
			}
		},

		{	/* India*/
			"IN",
			CE,
			{
				{ 1,   11,  20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,   4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,   4,  23, IDOR, FALSE},	/* 5G, ch 52~64*/
				{ 149, 	5,  23, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Indonesia*/
			"ID",
			CE,
			{
				{ 149, 	4,  36, BOTH, FALSE},	/* 5G, ch 149~161*/
				{ 0},							/* end*/
			}
		},

		{	/* Ireland*/
			"IE",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Israel*/
			"IL",
			CE,
			{
				{ 1,   13,  20, IDOR, FALSE},	/* 2.4 G, ch 1~3*/
				{ 36, 	4,  23, IDOR, FALSE},	/* 2.4 G, ch 4~9*/
				{ 52, 	4,  23, IDOR, FALSE},	/* 2.4 G, ch 10~13*/
				{ 0},							/* end*/
			}
		},

		{	/* Italy*/
			"IT",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Japan*/
			"JP",
			JAP,
			{
				{ 1,   14,  20, BOTH, FALSE},	/* 2.4 G, ch 1~14*/
				{ 36, 	4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,   4,  23, IDOR, FALSE}, 	/* 5G, ch 52~64*/
				{ 100, 11,  23, BOTH, TRUE}, 	/* 5G, ch 100~140*/
				{ 0},							/* end*/
			}
		},

		{	/* Jordan*/
			"JO",
			CE,
			{
				{ 1,   13,  20, IDOR, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36, 	4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 0},							/* end*/
			}
		},

		{	/* Kuwait*/
			"KW",
			CE,
			{
				{ 1,   14,  20, BOTH, FALSE},	/* 2.4 G, ch 1~14*/
				{ 36, 	4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 0},							/* end*/
			}
		},

		{	/* Latvia*/
			"LV",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Liechtenstein*/
			"LI",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Lithuania*/
			"LT",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Luxemburg*/
			"LU",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Malaysia*/
			"MY",
			CE,
			{
				{ 1,   13,  27, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36, 	4,  30, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52, 	4,  30, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 149,  5,  30, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Malta*/
			"MT",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36, 	4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52, 	4,  23, IDOR, TRUE},	/* 5G, ch 52~64*/
				{ 100, 11,  30, BOTH, TRUE},	/* 5G, ch 100~140*/
				{ 0},							/* end*/
			}
		},

		{	/* Morocco*/
			"MA",
			CE,
			{
				{ 1,   13,  10, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36, 	4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 149,  5,  30, IDOR, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Mexico*/
			"MX",
			CE,
			{
				{ 1,   11,  20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36, 	4,  17, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52, 	4,  23, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 100, 11,  30, BOTH, FALSE}, 	/* 5G, ch 100~140*/
				{ 149,  5,  30, ODOR, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Netherlands*/
			"NL",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* New Zealand*/
			"NZ",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36, 	4,  23, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52, 	4,  23, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 100, 11,  30, BOTH, FALSE}, 	/* 5G, ch 100~140*/
				{ 149,  4,  30, BOTH, FALSE},	/* 5G, ch 149~161*/
				{ 0},							/* end*/
			}
		},

		{	/* Norway*/
			"NO",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Peru*/
			"PE",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,   4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,   4,  23, IDOR, FALSE}, 	/* 5G, ch 52~64*/
				{ 100, 11,  30, BOTH, FALSE}, 	/* 5G, ch 100~140*/
				{ 149,  5,  27, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Portugal*/
			"PT",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Poland*/
			"PL",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Romania*/
			"RO",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Russia*/
			"RU",
			CE,
			{
				{ 1,   11,  20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,   4,  17, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,   4,  24, IDOR, FALSE}, 	/* 5G, ch 52~64*/	
				{ 100, 11,  30, BOTH, FALSE}, 	/* 5G, ch 100~140*/
				{ 149,  4,  30, IDOR, FALSE},	/* 5G, ch 149~161*/
				{ 0},							/* end*/
			}
		},

		{	/* Saudi Arabia*/
			"SA",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,   4,  23, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52,   4,  23, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 149,  5,  23, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Serbia_and_Montenegro*/
			"CS",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Singapore*/
			"SG",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,   4,  23, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52,   4,  23, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 149,  5,  20, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Slovakia*/
			"SK",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Slovenia*/
			"SI",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* South Africa*/
			"ZA",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,   4,  30, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52,   4,  30, IDOR, FALSE},	/* 5G, ch 52~64*/
				{ 100, 11,  30, BOTH, TRUE},	/* 5G, ch 100~140*/
				{ 149,  4,  30, BOTH, FALSE},	/* 5G, ch 149~161*/
				{ 0},							/* end*/
			}
		},

		{	/* South Korea*/
			"KR",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,   4,  20, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52,   4,  20, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 100,  8,  20, BOTH, FALSE},	/* 5G, ch 100~128*/
				{ 149,  5,  20, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Spain*/
			"ES",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Sweden*/
			"SE",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Switzerland*/
			"CH",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Taiwan*/
			"TW",
			CE,
			{
				{ 1,   11,  30, BOTH, FALSE},	/* 2.4 G, ch 1~11*/
				{ 52,   4,  23, IDOR, FALSE},	/* 5G, ch 52~64*/
				{ 100, 11,  20, BOTH, TRUE},	/* 5G, ch 100~140*/
				{ 149,  5,  20, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Turkey*/
			"TR",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* UK*/
			"GB",
			CE,
			{
				{ 1,   13, 20, BOTH, FALSE},	/* 2.4 G, ch 1~13*/
				{ 36,  4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,  4,  23, IDOR, TRUE}, 	/* 5G, ch 52~64*/
				{ 100, 11, 30, BOTH, TRUE}, 	/* 5G, ch 100~140*/
			/*	{ 149,	   7,  36, N/A, N/A},		5G, ch 149~173 No Geography  */
				{ 0},							/* end*/
			}
		},

		{	/* Ukraine*/
			"UA",
			CE,
			{
				{ 1,   11,  20, BOTH, FALSE},	/* 2.4 G, ch 1~11*/
				{ 36,   4,  23, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,   4,  23, IDOR, FALSE}, 	/* 5G, ch 52~64*/
				{ 0},							/* end*/
			}
		},

		{	/* United_Arab_Emirates*/
			"AE",
			CE,
			{
				{ 1,   13,  20, BOTH, FALSE},	/* 2.4 G, ch 1~11*/
				{ 0},							/* end*/
			}
		},

		{	/* United_States*/
			"US",
			FCC,
			{
				{ 1,   11,  30, BOTH, FALSE},	/* 2.4 G, ch 1~11*/
				{ 36,   4,  17, IDOR, FALSE},	/* 5G, ch 36~48*/
				{ 52,   4,  24, BOTH, TRUE},	/* 5G, ch 52~64*/
				{ 100, 11,  24, BOTH, TRUE},	/* 5G, ch 100~140*/
				{ 149,  5,  30, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},

		{	/* Venezuela*/
			"VE",
			CE,
			{
				{ 1,   11,  20, BOTH, FALSE},	/* 2.4 G, ch 1~11*/
				{ 149,  4,  27, BOTH, FALSE},	/* 5G, ch 149~161*/
				{ 0},							/* end*/
			}
		},

		{	/* Default*/
			"",
			CE,
			{
				{ 1,   14,  255, BOTH, FALSE},	/* 2.4 G, ch 1~14*/
				{ 36,   4,  255, BOTH, FALSE},	/* 5G, ch 36~48*/
				{ 52,   4,  255, BOTH, FALSE},	/* 5G, ch 52~64*/
				{ 100, 11,  255, BOTH, FALSE},	/* 5G, ch 100~140*/
				{ 149,  5,  255, BOTH, FALSE},	/* 5G, ch 149~165*/
				{ 0},							/* end*/
			}
		},
};


static PCH_REGION GetChRegion(
	IN PUCHAR CntryCode)
{
	INT loop = 0;
	PCH_REGION pChRegion = NULL;

	while (strcmp((PSTRING) ChRegion[loop].CountReg, "") != 0)
	{
		if (strncmp((PSTRING) ChRegion[loop].CountReg, (PSTRING) CntryCode, 2) == 0)
		{
			pChRegion = &ChRegion[loop];
			break;
		}
		loop++;
	}

	if (pChRegion == NULL)
		pChRegion = &ChRegion[loop];
	return pChRegion;
}

static VOID ChBandCheck(
	IN UCHAR PhyMode,
	OUT PUCHAR pChType)
{
	switch(PhyMode)
	{
		case PHY_11A:
#ifdef DOT11_N_SUPPORT
		case PHY_11AN_MIXED:
#endif /* DOT11_N_SUPPORT */
			*pChType = BAND_5G;
			break;
		case PHY_11ABG_MIXED:
#ifdef DOT11_N_SUPPORT
		case PHY_11AGN_MIXED:
		case PHY_11ABGN_MIXED:
#endif /* DOT11_N_SUPPORT */
			*pChType = BAND_BOTH;
			break;

		default:
			*pChType = BAND_24G;
			break;
	}
}

static UCHAR FillChList(
	IN PRTMP_ADAPTER pAd,
	IN PCH_DESP pChDesp,
	IN UCHAR Offset, 
	IN UCHAR increment,
	IN UCHAR regulatoryDomain)
{
	INT i, j, l;
	UCHAR channel;

	j = Offset;
	for (i = 0; i < pChDesp->NumOfCh; i++)
	{
		channel = pChDesp->FirstChannel + i * increment;
/*New FCC spec restrict the used channel under DFS */
		for (l=0; l<MAX_NUM_OF_CHANNELS; l++)
		{
			if (channel == pAd->TxPower[l].Channel)
			{
				pAd->ChannelList[j].Power = pAd->TxPower[l].Power;
				pAd->ChannelList[j].Power2 = pAd->TxPower[l].Power2;
#ifdef DOT11N_SS3_SUPPORT
					pAd->ChannelList[j].Power3 = pAd->TxPower[l].Power3;
#endif /* DOT11N_SS3_SUPPORT */
				break;
			}
		}
		if (l == MAX_NUM_OF_CHANNELS)
			continue;

		pAd->ChannelList[j].Channel = pChDesp->FirstChannel + i * increment;
		pAd->ChannelList[j].MaxTxPwr = pChDesp->MaxTxPwr;
		pAd->ChannelList[j].DfsReq = pChDesp->DfsReq;
		pAd->ChannelList[j].RegulatoryDomain = regulatoryDomain;
		j++;
	}
	pAd->ChannelListNum = j;

	return j;
}


static inline VOID CreateChList(
	IN PRTMP_ADAPTER pAd,
	IN PCH_REGION pChRegion,
	IN UCHAR Geography)
{
	INT i;
	UCHAR offset = 0;
	PCH_DESP pChDesp;
	UCHAR ChType;
	UCHAR increment;
	UCHAR regulatoryDomain;

	if (pChRegion == NULL)
		return;

	ChBandCheck(pAd->CommonCfg.PhyMode, &ChType);

	for (i=0; i<10; i++)
	{
		pChDesp = &pChRegion->ChDesp[i];
		if (pChDesp->FirstChannel == 0)
			break;

		if (ChType == BAND_5G)
		{
			if (pChDesp->FirstChannel <= 14)
				continue;
		}
		else if (ChType == BAND_24G)
		{
			if (pChDesp->FirstChannel > 14)
				continue;
		}

		if ((pChDesp->Geography == BOTH)
			|| (pChDesp->Geography == Geography))
        {
			if (pChDesp->FirstChannel > 14)
                increment = 4;
            else
                increment = 1;
			regulatoryDomain = pChRegion->DfsType;
			offset = FillChList(pAd, pChDesp, offset, increment, regulatoryDomain);
        }
	}
}


VOID BuildChannelListEx(
	IN PRTMP_ADAPTER pAd)
{
	PCH_REGION pChReg;

	pChReg = GetChRegion(pAd->CommonCfg.CountryCode);
	CreateChList(pAd, pChReg, pAd->CommonCfg.Geography);
}

VOID BuildBeaconChList(
	IN PRTMP_ADAPTER pAd,
	OUT PUCHAR pBuf,
	OUT	PULONG pBufLen)
{
	INT i;
	ULONG TmpLen;
	PCH_REGION pChRegion;
	PCH_DESP pChDesp;
	UCHAR ChType;

	pChRegion = GetChRegion(pAd->CommonCfg.CountryCode);

	if (pChRegion == NULL)
		return;

	ChBandCheck(pAd->CommonCfg.PhyMode, &ChType);
	*pBufLen = 0;

	for (i=0; i<10; i++)
	{
		pChDesp = &pChRegion->ChDesp[i];
		if (pChDesp->FirstChannel == 0)
			break;

		if (ChType == BAND_5G)
		{
			if (pChDesp->FirstChannel <= 14)
				continue;
		}
		else if (ChType == BAND_24G)
		{
			if (pChDesp->FirstChannel > 14)
				continue;
		}

		if ((pChDesp->Geography == BOTH)
			|| (pChDesp->Geography == pAd->CommonCfg.Geography))
		{
			MakeOutgoingFrame(pBuf + *pBufLen,		&TmpLen,
								1,                 	&pChDesp->FirstChannel,
								1,                 	&pChDesp->NumOfCh,
								1,                 	&pChDesp->MaxTxPwr,
								END_OF_ARGS);
			*pBufLen += TmpLen;
		}
	}
}
#endif /* EXT_BUILD_CHANNEL_LIST */

#ifdef DOT11_N_SUPPORT
static BOOLEAN IsValidChannel(
	IN PRTMP_ADAPTER pAd,
	IN UCHAR channel)

{
	INT i;

	for (i = 0; i < pAd->ChannelListNum; i++)
	{
		if (pAd->ChannelList[i].Channel == channel)
			break;
	}

	if (i == pAd->ChannelListNum)
		return FALSE;
	else
		return TRUE;
}


static UCHAR GetExtCh(
	IN UCHAR Channel,
	IN UCHAR Direction)
{
	CHAR ExtCh;

	if (Direction == EXTCHA_ABOVE)
		ExtCh = Channel + 4;
	else
		ExtCh = (Channel - 4) > 0 ? (Channel - 4) : 0;

	return ExtCh;
}


VOID N_ChannelCheck(
	IN PRTMP_ADAPTER pAd)
{
	/*UCHAR ChannelNum = pAd->ChannelListNum;*/
	UCHAR Channel = pAd->CommonCfg.Channel;

	if ((pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED) && (pAd->CommonCfg.RegTransmitSetting.field.BW  == BW_40))
	{
		if (Channel > 14)
		{
			if ((Channel == 36) || (Channel == 44) || (Channel == 52) || (Channel == 60) || (Channel == 100) || (Channel == 108) ||
			    (Channel == 116) || (Channel == 124) || (Channel == 132) || (Channel == 149) || (Channel == 157))
			{
				pAd->CommonCfg.RegTransmitSetting.field.EXTCHA = EXTCHA_ABOVE;
			}
			else if ((Channel == 40) || (Channel == 48) || (Channel == 56) || (Channel == 64) || (Channel == 104) || (Channel == 112) ||
					(Channel == 120) || (Channel == 128) || (Channel == 136) || (Channel == 153) || (Channel == 161))
			{
				pAd->CommonCfg.RegTransmitSetting.field.EXTCHA = EXTCHA_BELOW;
			}
			else
			{
				pAd->CommonCfg.RegTransmitSetting.field.BW  = BW_20;
			}
		}
		else
		{
			do
			{
				UCHAR ExtCh;
				UCHAR Dir = pAd->CommonCfg.RegTransmitSetting.field.EXTCHA;
				ExtCh = GetExtCh(Channel, Dir);
				if (IsValidChannel(pAd, ExtCh))
					break;

				Dir = (Dir == EXTCHA_ABOVE) ? EXTCHA_BELOW : EXTCHA_ABOVE;
				ExtCh = GetExtCh(Channel, Dir);
				if (IsValidChannel(pAd, ExtCh))
				{
					pAd->CommonCfg.RegTransmitSetting.field.EXTCHA = Dir;
					break;
				}
				pAd->CommonCfg.RegTransmitSetting.field.BW  = BW_20;
			} while(FALSE);

			if (Channel == 14)
			{
				pAd->CommonCfg.RegTransmitSetting.field.BW  = BW_20;
				/*pAd->CommonCfg.RegTransmitSetting.field.EXTCHA = EXTCHA_NONE; We didn't set the ExtCh as NONE due to it'll set in RTMPSetHT()*/
			}
		}
	}


}


VOID N_SetCenCh(
	IN PRTMP_ADAPTER pAd)
{
	if (pAd->CommonCfg.RegTransmitSetting.field.BW == BW_40)
	{
		if (pAd->CommonCfg.RegTransmitSetting.field.EXTCHA == EXTCHA_ABOVE)
		{

			pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel + 2;
		}
		else
		{
			if (pAd->CommonCfg.Channel == 14)
				pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel - 1;
			else
				pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel - 2;
		}
	}
	else
	{
		pAd->CommonCfg.CentralChannel = pAd->CommonCfg.Channel;
	}
}
#endif /* DOT11_N_SUPPORT */


UINT8 GetCuntryMaxTxPwr(
	IN PRTMP_ADAPTER pAd,
	IN UINT8 channel)
{
	int i;
	for (i = 0; i < pAd->ChannelListNum; i++)
	{
		if (pAd->ChannelList[i].Channel == channel)
			break;
	}

	if (i == pAd->ChannelListNum)
		return 0xff;
#ifdef SINGLE_SKU
	if (pAd->CommonCfg.bSKUMode == TRUE)
	{
		UINT deltaTxStreamPwr = 0;

#ifdef DOT11_N_SUPPORT
		if ((pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED) && (pAd->CommonCfg.TxStream == 2))
			deltaTxStreamPwr = 3; /* If 2Tx case, antenna gain will increase 3dBm*/
#endif /* DOT11_N_SUPPORT */
		if (pAd->ChannelList[i].RegulatoryDomain == FCC)
		{
			/* FCC should maintain 20/40 Bandwidth, and without antenna gain */
#ifdef DOT11_N_SUPPORT
			if ((pAd->CommonCfg.PhyMode >= PHY_11ABGN_MIXED) &&
				(pAd->CommonCfg.RegTransmitSetting.field.BW == BW_40) &&
				(channel == 1 || channel == 11))
				return (pAd->ChannelList[i].MaxTxPwr - pAd->CommonCfg.BandedgeDelta - deltaTxStreamPwr);
			else
#endif /* DOT11_N_SUPPORT */
				return (pAd->ChannelList[i].MaxTxPwr - deltaTxStreamPwr);
		}
		else if (pAd->ChannelList[i].RegulatoryDomain == CE)
		{
			return (pAd->ChannelList[i].MaxTxPwr - pAd->CommonCfg.AntGain - deltaTxStreamPwr);
		}
		else
			return 0xff;
	}
	else
#endif /* SINGLE_SKU */
		return pAd->ChannelList[i].MaxTxPwr;
}


/* for OS_ABL */
VOID RTMP_MapChannelID2KHZ(
	IN UCHAR Ch,
	OUT UINT32 *pFreq)
{
	int chIdx;
	for (chIdx = 0; chIdx < CH_HZ_ID_MAP_NUM; chIdx++)
	{
		if ((Ch) == CH_HZ_ID_MAP[chIdx].channel)
		{
			(*pFreq) = CH_HZ_ID_MAP[chIdx].freqKHz * 1000;
			break;
		}
	}
	if (chIdx == CH_HZ_ID_MAP_NUM)
		(*pFreq) = 2412000;
}

/* for OS_ABL */
VOID RTMP_MapKHZ2ChannelID(
	IN ULONG Freq,
	OUT INT *pCh)
{
	int chIdx;
	for (chIdx = 0; chIdx < CH_HZ_ID_MAP_NUM; chIdx++)
	{
		if ((Freq) == CH_HZ_ID_MAP[chIdx].freqKHz)
		{
			(*pCh) = CH_HZ_ID_MAP[chIdx].channel;
			break;
		}
	}
	if (chIdx == CH_HZ_ID_MAP_NUM)
		(*pCh) = 1;
}

