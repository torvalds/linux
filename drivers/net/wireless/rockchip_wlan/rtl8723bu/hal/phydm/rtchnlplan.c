/******************************************************************************
 *
 * Copyright(c) 2007 - 2012 Realtek Corporation. All rights reserved.
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

/******************************************************************************
     
 History:
	Data		Who		Remark (Internal History)
	
	05/14/2012	MH		Collect RTK inernal infromation and generate channel plan draft.
	
******************************************************************************/

//============================================================
// include files
//============================================================
#include "mp_precomp.h"
#include "rtchnlplan.h"



//
//	Channel Plan Domain Code
//

/*
	Channel Plan Contents					
	Domain Code		EEPROM	Countries in Specific Domain		
			2G RD		5G RD		Bit[6:0]	2G	5G	
	Case	Old Define				00h~1Fh	Old Define	Old Define	
	1		2G_WORLD	5G_NULL		20h		Worldwird 13	NA	
	2		2G_ETSI1	5G_NULL		21h		Europe 2G		NA	
	3		2G_FCC1		5G_NULL		22h		US 2G			NA	
	4		2G_MKK1		5G_NULL		23h		Japan 2G		NA	
	5		2G_ETSI2	5G_NULL		24h		France 2G		NA	
	6		2G_FCC1		5G_FCC1		25h		US 2G			US 5G					八大國認證
	7		2G_WORLD	5G_ETSI1	26h		Worldwird 13	Europe					八大國認證
	8		2G_MKK1		5G_MKK1		27h		Japan 2G		Japan 5G				八大國認證
	9		2G_WORLD	5G_KCC1		28h		Worldwird 13	Korea					八大國認證
	10		2G_WORLD	5G_FCC2		29h		Worldwird 13	US o/w DFS Channels		
	11		2G_WORLD	5G_FCC3		30h		Worldwird 13	India, Mexico	    	
	12		2G_WORLD	5G_FCC4		31h		Worldwird 13	Venezuela	        	
	13		2G_WORLD	5G_FCC5		32h		Worldwird 13	China	            	
	14		2G_WORLD	5G_FCC6		33h		Worldwird 13	Israel	            	
	15		2G_FCC1		5G_FCC7		34h		US 2G			US/Canada				八大國認證
	16		2G_WORLD	5G_ETSI2	35h		Worldwird 13	Australia, New Zealand	八大國認證
	17		2G_WORLD	5G_ETSI3	36h		Worldwird 13	Russia	
	18		2G_MKK1		5G_MKK2		37h		Japan 2G		Japan (W52, W53)	
	19		2G_MKK1		5G_MKK3		38h		Japan 2G		Japan (W56)	
	20		2G_FCC1		5G_NCC1		39h		US 2G			Taiwan					八大國認證
						
	NA		2G_WORLD	5G_FCC1		7F		FCC	FCC DFS Channels	Realtek Define
						
						
						
						
						
	2.4G 	Regulatory 	Domains					
	Case	2G RD		Regulation	Channels	Frequencyes		Note					Countries in Specific Domain
	1		2G_WORLD	ETSI		1~13		2412~2472		Passive scan CH 12, 13	Worldwird 13
	2		2G_ETSI1	ETSI		1~13		2412~2472								Europe
	3		2G_FCC1		FCC			1~11		2412~2462								US
	4		2G_MKK1		MKK			1~13, 14	2412~2472, 2484							Japan
	5		2G_ETSI2	ETSI		10~13		2457~2472								France
						
						
						
						
	5G Regulatory Domains					
	Case	5G RD		Regulation	Channels			Frequencyes					Note											Countries in Specific Domain
	1		5G_NULL		NA			NA					NA							Do not support 5GHz	
	2		5G_ETSI1	ETSI		"36~48, 52~64,  	
									100~140"			"5180~5240, 5260~5230
														5500~5700"					Band1, Ban2, Band3								Europe
	3		5G_ETSI2	ETSI		"36~48, 52~64, 
									100~140, 149~165"	"5180~5240, 5260~5230
														5500~5700, 5745~5825"		Band1, Ban2, Band3, Band4						Australia, New Zealand
	4		5G_ETSI3	ETSI		"36~48, 52~64, 
														100~132, 149~165"	
														"5180~5240, 5260~5230
														5500~5660, 5745~5825"		Band1, Ban2, Band3(except CH 136, 140), Band4"	Russia
	5		5G_FCC1		FCC			"36~48, 52~64, 
									100~140, 149~165"	
														"5180~5240, 5260~5230
														5500~5700, 5745~5825"		Band1(5150~5250MHz), 
																					Band2(5250~5350MHz),
																					Band3(5470~5725MHz),
																					Band4(5725~5850MHz)"							US
	6		5G_FCC2		FCC			36~48, 149~165		5180~5240, 5745~5825		Band1, Band4	FCC o/w DFS Channels
	7		5G_FCC3		FCC			"36~48, 52~64, 
									149~165"			"5180~5240, 5260~5230
														5745~5825"					Band1, Ban2, Band4								India, Mexico
	8		5G_FCC4		FCC			"36~48, 52~64, 
									149~161"			"5180~5240, 5260~5230
														5745~5805"					Band1, Ban2,
																					Band4(except CH 165)"							Venezuela
	9		5G_FCC5		FCC			149~165				5745~5825					Band4											China
	10		5G_FCC6		FCC			36~48, 52~64		5180~5240, 5260~5230		Band1, Band2									Israel
	11		5G_FCC7
			5G_IC1		FCC
						IC"			"36~48, 52~64, 
									100~116, 136, 140, 
									149~165"			"5180~5240, 5260~5230
														5500~5580, 5680, 5700, 
														5745~5825"					"Band1, Band2, 
																					Band3(except 5600~5650MHz),
																					Band4"											"US
																																	Canada"
	12		5G_KCC1		KCC			"36~48, 52~64, 
									100~124, 149~165"	"5180~5240, 5260~5230
														5500~5620, 5745~5825"		"Band1, Ban2, 
																					Band3(5470~5650MHz),
																					Band4"											Korea
	13		5G_MKK1		MKK			"36~48, 52~64, 
									100~140"			"5180~5240, 5260~5230
														5500~5700"					W52, W53, W56									Japan
	14		5G_MKK2		MKK			36~48, 52~64		5180~5240, 5260~5230		W52, W53										Japan (W52, W53)
	15		5G_MKK3		MKK			100~140				5500~5700					W56	Japan (W56)
	16		5G_NCC1		NCC			"56~64,
									100~116, 136, 140,
									149~165"			"5260~5320
														5500~5580, 5680, 5700, 
														5745~5825"					"Band2(except CH 52), 
																					Band3(except 5600~5650MHz),
																					Band4"											Taiwan
						
						
*/						
						
//
// 2.4G CHannel 
//						
/*

	2.4G Band		Regulatory Domains																RTL8192D	
	Channel Number	Channel Frequency	US		Canada	Europe	Spain	France	Japan	Japan		20M		40M
					(MHz)				(FCC)	(IC)	(ETSI)							(MPHPT)				
	1				2412				v		v		v								v			v		
	2				2417				v		v		v								v			v		
	3				2422				v		v		v								v			v		v
	4				2427				v		v		v								v			v		v
	5				2432				v		v		v								v			v		v
	6				2437				v		v		v								v			v		v
	7				2442				v		v		v								v			v		v
	8				2447				v		v		v								v			v		v
	9				2452				v		v		v								v			v		v
	10				2457				v		v		v		v		v				v			v		v
	11				2462				v		v		v		v		v				v			v		v
	12				2467								v				v				v			v		v
	13				2472								v				v				v			v	
	14				2484														v					v	


*/


//
// 5G Operating Channel
//
/*

	5G Band		RTL8192D	RTL8195 (Jaguar)				Jaguar 2	Regulatory Domains											
	Channel Number	Channel Frequency	Global	Global				Global	"US
(FCC 15.407)"	"Canada
(FCC, except 5.6~5.65GHz)"	Argentina, Australia, New Zealand, Brazil, S. Africa (FCC/ETSI)	"Europe
(CE 301 893)"	China	India, Mexico, Singapore	Israel, Turkey	"Japan
(MIC Item 19-3, 19-3-2)"	Korea	Russia, Ukraine	"Taiwan
(NCC)"	Venezuela
		(MHz)	(20MHz)	(20MHz)	(40MHz)	(80MHz)	(160MHz)	(20MHz)	(20MHz)	(20MHz)	(20MHz)	(20MHz)	(20MHz)	(20MHz)	(20MHz)	(20MHz)	(20MHz)	(20MHz)	(20MHz)	(20MHz)
"Band 1
5.15GHz
~
5.25GHz"	36	5180	v	v	v	v		v	Indoor	Indoor	v	Indoor		v	Indoor	Indoor	v	v		v
	40	5200	v	v				v	Indoor	Indoor	v	Indoor		v	Indoor	Indoor	v	v		v
	44	5220	v	v	v			v	Indoor	Indoor	v	Indoor		v	Indoor	Indoor	v	v		v
	48	5240	v	v				v	Indoor	Indoor	v	Indoor		v	Indoor	Indoor	v	v		v
"Band 2
5.25GHz
~
5.35GHz
(DFS)"	52	5260	v	v	v	v		v	v	v	v	Indoor		v	Indoor	Indoor	v	v		v
	56	5280	v	v				v	v	v	v	Indoor		v	Indoor	Indoor	v	v	Indoor	v
	60	5300	v	v	v			v	v	v	v	Indoor		v	Indoor	Indoor	v	v	Indoor	v
	64	5320	v	v				v	v	v	v	Indoor		v	Indoor	Indoor	v	v	Indoor	v
																				
"Band 3
5.47GHz
~
5.725GHz
(DFS)"	100	5500	v	v	v	v		v	v	v	v	v				v	v	v	v	
	104	5520	v	v				v	v	v	v	v				v	v	v	v	
	108	5540	v	v	v			v	v	v	v	v				v	v	v	v	
	112	5560	v	v				v	v	v	v	v				v	v	v	v	
	116	5580	v	v	v	v		v	v	v	v	v				v	v	v	v	
	120	5600	v	v				v	Indoor		v	Indoor				v	v	v		
	124	5620	v	v	v			v	Indoor		v	Indoor				v	v	v		
	128	5640	v	v				v	Indoor		v	Indoor				v		v		
	132	5660	v	v	v	E		v	Indoor		v	Indoor				v		v		
	136	5680	v	v				v	v	v	v	v				v			v	
	140	5700	v	v	E			v	v	v	v	v				v			v	
	144	5720	E	E				E												
"Band 4
5.725GHz
~
5.85GHz
(~5.9GHz)"	149	5745	v	v	v	v		v	v	v	v		v	v			v	v	v	v
	153	5765	v	v				v	v	v	v		v	v			v	v	v	v
	157	5785	v	v	v			v	v	v	v		v	v			v	v	v	v
	161	5805	v	v				v	v	v	v		v	v			v	v	v	v
	165	5825	v	v	P	P		v	v	v	v		v	v			v	v	v	
	169	5845	P	P				P												
	173	5865	P	P	P			P												
	177	5885	P	P				P												
Channel Count			28	28	14	7	0	28	24	20	24	19	5	13	8	19	20	22	15	12
			E: FCC accepted the ask for CH144 from Accord.					PS: 160MHz 用 80MHz+80MHz實現？			Argentina	Belgium (比利時)		India	Israel			Russia		
			P: Customer's requirement from James.								Australia	The Netherlands (荷蘭)		Mexico	Turkey			Ukraine		
											New Zealand	UK (英國)		Singapore						
											Brazil	Switzerland (瑞士)								


*/

/*---------------------------Define Local Constant---------------------------*/


// define Maximum Power v.s each band for each region 
// ISRAEL
// Format:
// RT_CHANNEL_DOMAIN_Region ={{{Chnl_Start, Chnl_end, Pwr_dB_Max}, {Chn2_Start, Chn2_end, Pwr_dB_Max}, {Chn3_Start, Chn3_end, Pwr_dB_Max}, {Chn4_Start, Chn4_end, Pwr_dB_Max}, {Chn5_Start, Chn5_end, Pwr_dB_Max}}, Limit_Num} */
// RT_CHANNEL_DOMAIN_FCC ={{{01,11,30}, {36,48,17}, {52,64,24}, {100,140,24}, {149,165,30}}, 5} 
// "NR" is non-release channle.
// Issue--- Israel--Russia--New Zealand
// DOMAIN_01= (2G_WORLD, 5G_NULL)
// DOMAIN_02= (2G_ETSI1, 5G_NULL)
// DOMAIN_03= (2G_FCC1, 5G_NULL)
// DOMAIN_04= (2G_MKK1, 5G_NULL)
// DOMAIN_05= (2G_ETSI2, 5G_NULL)
// DOMAIN_06= (2G_FCC1, 5G_FCC1)
// DOMAIN_07= (2G_WORLD, 5G_ETSI1)
// DOMAIN_08= (2G_MKK1, 5G_MKK1)
// DOMAIN_09= (2G_WORLD, 5G_KCC1)
// DOMAIN_10= (2G_WORLD, 5G_FCC2)
// DOMAIN_11= (2G_WORLD, 5G_FCC3)----india
// DOMAIN_12= (2G_WORLD, 5G_FCC4)----Venezuela
// DOMAIN_13= (2G_WORLD, 5G_FCC5)----China
// DOMAIN_14= (2G_WORLD, 5G_FCC6)----Israel
// DOMAIN_15= (2G_FCC1, 5G_FCC7)-----Canada
// DOMAIN_16= (2G_WORLD, 5G_ETSI2)---Australia
// DOMAIN_17= (2G_WORLD, 5G_ETSI3)---Russia
// DOMAIN_18= (2G_MKK1, 5G_MKK2)-----Japan
// DOMAIN_19= (2G_MKK1, 5G_MKK3)-----Japan
// DOMAIN_20= (2G_FCC1, 5G_NCC1)-----Taiwan
// DOMAIN_21= (2G_FCC1, 5G_NCC1)-----Taiwan


static	RT_CHANNEL_PLAN_MAXPWR	ChnlPlanPwrMax_2G[] = {

	// 2G_WORLD, 
	{{1, 13, 20}, 1},	

	// 2G_ETSI1
	{{1, 13, 20}, 1},

	/* RT_CHANNEL_DOMAIN_ETSI */
	{{{1, 11, 17}, {40, 56, 17}, {60, 128, 17}, {0, 0, 0}, {149, 165, 17}}, 4},

	// RT_CHANNEL_DOMAIN_MKK
	{{{1, 11, 17}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}, 1},

	// Add new channel plan mex power table.
	// ......
	};


/*
//===========================================1:(2G_WORLD, 5G_NULL)

RT_CHANNEL_PLAN_MAXPWR	RT_DOMAIN_01 ={{{01,13,20}, {NR,NR,0}, {NR,NR,0}, {NR,NR,0}, {NR,NR,0}}, 1} 

//===========================================2:(2G_ETSI1, 5G_NULL)

RT_DOMAIN_02 ={{{01,13,20}, {NR,NR,0}, {NR,NR,0}, {NR,NR,0}, {NR,NR,0}}, 1}

//===========================================3:(2G_FCC1, 5G_NULL)

RT_DOMAIN_03 ={{{01,11,30}, {NR,NR,0}, {NR,NR,0}, {NR,NR,0}, {NR,NR,0}}, 1}

//===========================================4:(2G_MKK1, 5G_NULL)

RT_DOMAIN_04 ={{{01,14,23}, {NR,NR,0}, {NR,NR,0}, {NR,NR,0}, {NR,NR,0}}, 1}

//===========================================5:(2G_ETSI2, 5G_NULL)

RT_DOMAIN_05 ={{{10,13,20}, {NR,NR,0}, {NR,NR,0}, {NR,NR,0}, {NR,NR,0}}, 1}

//===========================================6:(2G_FCC1, 5G_FCC1)

RT_DOMAIN_06 ={{{01,13,30}, {36,48,17}, {52,64,24}, {100,140,24}, {149,165,30}}, 5}

//===========================================7:(2G_WORLD, 5G_ETSI1)

RT_DOMAIN_07 ={{{01,13,20}, {36,48,23}, {52,64,23}, {100,140,30}, {NR,NR,0}}, 4}

//===========================================8:(2G_MKK1, 5G_MKK1)

RT_DOMAIN_08 ={{{01,14,23}, {36,48,23}, {52,64,23}, {100,140,23}, {NR,NR,0}}, 4}

//===========================================9:(2G_WORLD, 5G_KCC1)

RT_DOMAIN_09 ={{{01,13,20}, {36,48,17}, {52,64,23}, {100,124,23}, {149,165,23}}, 5}

//===========================================10:(2G_WORLD, 5G_FCC2)

RT_DOMAIN_10 ={{{01,13,20}, {36,48,17}, {NR,NR,0}, {NR,NR,0}, {149,165,30}}, 3}

//===========================================11:(2G_WORLD, 5G_FCC3)
RT_DOMAIN_11 ={{{01,13,20}, {36,48,23}, {52,64,23}, {NR,NR,0}, {149,165,23}}, 4}

//===========================================12:(2G_WORLD, 5G_FCC4)
RT_DOMAIN_12 ={{{01,13,20}, {36,48,24}, {52,64,24}, {NR,NR,0}, {149,161,27}}, 4}

//===========================================13:(2G_WORLD, 5G_FCC5)
RT_DOMAIN_13 ={{{01,13,20}, {NR,NR,0}, {NR,NR,0}, {NR,NR,0}, {149,165,27}}, 2}

//===========================================14:(2G_WORLD, 5G_FCC6)
RT_DOMAIN_14 ={{{01,13,20}, {36,48,17}, {52,64,17}, {NR,NR,0}, {NR,NR,0}}, 3}

//===========================================15:(2G_FCC1, 5G_FCC7)
RT_DOMAIN_15 ={{{01,11,30}, {36,48,23}, {52,64,24}, {100,140,24}, {149,165,30}}, 5}

//===========================================16:(2G_WORLD, 5G_ETSI2)
RT_DOMAIN_16 ={{{01,13,20}, {36,48,23}, {52,64,23}, {100,140,30}, {149,165,30}}, 5}

//===========================================17:(2G_WORLD, 5G_ETSI3)
RT_DOMAIN_17 ={{{01,13,20}, {36,48,23}, {52,64,23}, {100,132,30}, {149,165,20}}, 5}

//===========================================18:(2G_MKK1, 5G_MKK2)
RT_DOMAIN_18 ={{{01,14,23}, {36,48,23}, {52,64,23}, {NR,NR,0}, {NR,NR,0}}, 3}

//===========================================19:(2G_MKK1, 5G_MKK3)
RT_DOMAIN_19 ={{{01,14,23}, {NR,NR,0}, {NR,NR,0}, {100,140,23}, {NR,NR,0}}, 2}

//===========================================20:(2G_FCC1, 5G_NCC1)
RT_DOMAIN_20 ={{{01,11,30}, {NR,NR,0}, {56,64,23}, {100,140,24}, {149,165,30}}, 4}

//===========================================21:(2G_FCC1, 5G_NCC2)
RT_DOMAIN_21 ={{{01,11,30}, {NR,NR,0}, {56,64,23}, {NR,NR,0}, {149,165,30}}, 3}

//===========================================22:(2G_WORLD, 5G_FCC3)
RT_DOMAIN_22 ={{{01,13,24}, {36,48,20}, {52,64,24}, {NR,NR,0}, {149,165,30}}, 4}

//===========================================23:(2G_WORLD, 5G_ETSI2)
RT_DOMAIN_23 ={{{01,13,20}, {36,48,23}, {52,64,23}, {100,140,30}, {149,165,30}}, 5}

*/

//
// Counter & Realtek Channel plan transfer table.
//
RT_CHNL_CTRY_TBL	RtCtryChnlTbl[] = 
{

	{
		RT_CTRY_AL,							//	"Albania阿爾巴尼亞"					
		"AL",
		RT_2G_WORLD,
		RT_5G_WORLD,		
		RT_CHANNEL_DOMAIN_UNDEFINED			// 2G/5G world.
	},
#if 0	
	{
		RT_CTRY_BB,							//  "Barbados巴巴多斯"				
		"BB",
		RT_2G_WORLD,
		RT_5G_NULL,		
		RT_CHANNEL_DOMAIN_EFUSE_0x20		// 2G world. 5G_NULL
	},
	
	{
		RT_CTRY_DE,							//  "Germany德國"					
		"DE",
		RT_2G_WORLD,
		RT_5G_ETSI1,		
		RT_CHANNEL_DOMAIN_EFUSE_0x26
	},
	
	{
		RT_CTRY_US,							//  "Germany德國"					
		"US",
		RT_2G_FCC1,
		RT_5G_FCC7,		
		RT_CHANNEL_DOMAIN_EFUSE_0x34
	},

	{
		RT_CTRY_JP,							//  "Germany德國"					
		"JP",
		RT_2G_MKK1,
		RT_5G_MKK1,		
		RT_CHANNEL_DOMAIN_EFUSE_0x34
	},
		
	{
		RT_CTRY_TW,							//  "Germany德國"					
		"TW",
		RT_2G_FCC1,
		RT_5G_NCC1,		
		RT_CHANNEL_DOMAIN_EFUSE_0x39
	},	
#endif

};	// RtCtryChnlTbl

//
// Realtek Defined Channel plan.
//
#if 0

static	RT_CHANNEL_PLAN_NEW		RtChnlPlan[] =
{
	// Channel Plan   0x20.
	{
		&RtCtryChnlTbl[1],					// RT_CHNL_CTRY_TBL Country & channel plan transfer table.		
		RT_CHANNEL_DOMAIN_EFUSE_0x20,		// RT_CHANNEL_DOMAIN RT Channel Plan Define 
		RT_2G_WORLD,						// RT_REGULATION_2G
		RT_5G_NULL,							// RT_REGULATION_5G
		RT_WORLD,							// RT_REGULATION_CMN RT Regulatory domain definition.
		RT_SREQ_NA,							// RT Channel plan special & customerize requirement.
		
		CHNL_RT_2G_WORLD,
		CHNL_RT_2G_WORLD_SCAN_TYPE,
		&ChnlPlanPwrMax_2G[0],

		CHNL_RT_5G_NULL,
		CHNL_RT_5G_NULL_SCAN_TYPE,

		
	},
	
	// Channel Plan   0x26.
	{
		&RtCtryChnlTbl[1],					// RT_CHNL_CTRY_TBL Country & channel plan transfer table.		
		RT_CHANNEL_DOMAIN_EFUSE_0x26,		// RT_CHANNEL_DOMAIN RT Channel Plan Define 
		RT_2G_WORLD,						// RT_REGULATION_2G
		RT_5G_ETSI1,						// RT_REGULATION_5G
		RT_WORLD,							// RT_REGULATION_CMN RT Regulatory domain definition.
		RT_SREQ_NA,							// RT Channel plan special & customerize requirement.
		
		CHNL_RT_2G_WORLD,					// 2G workd cannel
		CHNL_RT_2G_WORLD_SCAN_TYPE,
		&ChnlPlanPwrMax_2G[1],
		
		CHNL_RT_5G_ETSI1,
		CHNL_RT_5G_ETSI1_SCAN_TYPE,
		
	}
	
	
};
#endif




