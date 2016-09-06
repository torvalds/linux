/******************************************************************************
 *
 * Copyright(c) 2007 - 2011 Realtek Corporation. All rights reserved.
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
#ifndef __HAL_COM_PHYCFG_H__
#define __HAL_COM_PHYCFG_H__


#define MAX_POWER_INDEX 		0x3F

typedef enum _REGULATION_TXPWR_LMT {
	TXPWR_LMT_FCC = 0,
	TXPWR_LMT_MKK,
	TXPWR_LMT_ETSI,
} REGULATION_TXPWR_LMT;

/*------------------------------Define structure----------------------------*/ 
typedef struct _BB_REGISTER_DEFINITION{
	u32 rfintfs;			// set software control: 
						//		0x870~0x877[8 bytes]
							
	u32 rfintfo; 			// output data: 
						//		0x860~0x86f [16 bytes]
							
	u32 rfintfe; 			// output enable: 
						//		0x860~0x86f [16 bytes]
							
	u32 rf3wireOffset;	// LSSI data:
						//		0x840~0x84f [16 bytes]

	u32 rfHSSIPara2; 	// wire parameter control2 : 
						//		0x824~0x827,0x82c~0x82f, 0x834~0x837, 0x83c~0x83f [16 bytes]
								
	u32 rfLSSIReadBack; 	//LSSI RF readback data SI mode
						//		0x8a0~0x8af [16 bytes]

	u32 rfLSSIReadBackPi; 	//LSSI RF readback data PI mode 0x8b8-8bc for Path A and B

}BB_REGISTER_DEFINITION_T, *PBB_REGISTER_DEFINITION_T;

#ifndef CONFIG_EMBEDDED_FWIMG
int phy_ConfigMACWithParaFile(IN PADAPTER	Adapter, IN u8*	pFileName);

int PHY_ConfigBBWithPowerLimitTableParaFile(IN PADAPTER	Adapter, IN s8*	pFileName);

int phy_ConfigBBWithParaFile(IN PADAPTER	Adapter, IN u8*	pFileName);

int phy_ConfigBBWithPgParaFile(IN PADAPTER	Adapter, IN u8*	pFileName);

int phy_ConfigBBWithMpParaFile(IN PADAPTER	Adapter, IN u8*	pFileName);

int PHY_ConfigRFWithParaFile(IN	PADAPTER	Adapter, IN u8*	pFileName, IN u8	eRFPath);

int PHY_ConfigRFWithTxPwrTrackParaFile(IN PADAPTER	Adapter, IN u8*	pFileName);
#endif


#endif //__HAL_COMMON_H__

