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

#include "../odm_precomp.h"

#ifdef CONFIG_IOL_IOREG_CFG
#include <rtw_iol.h>
#endif

#if (RTL8188E_SUPPORT == 1)
static BOOLEAN
CheckCondition(
    const u4Byte  Condition,
    const u4Byte  Hex
    )
{
    u4Byte _board     = (Hex & 0x000000FF);
    u4Byte _interface = (Hex & 0x0000FF00) >> 8;
    u4Byte _platform  = (Hex & 0x00FF0000) >> 16;
    u4Byte cond = Condition;

    if ( Condition == 0xCDCDCDCD )
        return TRUE;

    cond = Condition & 0x000000FF;
    if ( (_board == cond) && cond != 0x00)
        return FALSE;

    cond = Condition & 0x0000FF00;
    cond = cond >> 8;
    if ( (_interface & cond) == 0 && cond != 0x07)
        return FALSE;

    cond = Condition & 0x00FF0000;
    cond = cond >> 16;
    if ( (_platform & cond) == 0 && cond != 0x0F)
        return FALSE;
    return TRUE;
}


/******************************************************************************
*                           RadioA_1T.TXT
******************************************************************************/

u4Byte Array_RadioA_1T_8188E[] = { 
		0x000, 0x00030000,
		0x008, 0x00084000,
		0x018, 0x00000407,
		0x019, 0x00000012,
		0x01E, 0x00080009,
		0x01F, 0x00000880,
		0x02F, 0x0001A060,
		0x03F, 0x00000000,
		0x042, 0x000060C0,
		0x057, 0x000D0000,
		0x058, 0x000BE180,
		0x067, 0x00001552,
		0x083, 0x00000000,
		0x0B0, 0x000FF8FC,
		0x0B1, 0x00054400,
		0x0B2, 0x000CCC19,
		0x0B4, 0x00043003,
		0x0B6, 0x0004953E,
		0x0B7, 0x0001C718,
		0x0B8, 0x000060FF,
		0x0B9, 0x00080001,
		0x0BA, 0x00040000,
		0x0BB, 0x00000400,
		0x0BF, 0x000C0000,
		0x0C2, 0x00002400,
		0x0C3, 0x00000009,
		0x0C4, 0x00040C91,
		0x0C5, 0x00099999,
		0x0C6, 0x000000A3,
		0x0C7, 0x00088820,
		0x0C8, 0x00076C06,
		0x0C9, 0x00000000,
		0x0CA, 0x00080000,
		0x0DF, 0x00000180,
		0x0EF, 0x000001A0,
		0x051, 0x0006B27D,
	0xFF0F041F, 0xABCD,
		0x052, 0x0007E4DD,
	0xCDCDCDCD, 0xCDCD,
		0x052, 0x0007E49D,
	0xFF0F041F, 0xDEAD,
		0x053, 0x00000073,
		0x056, 0x00051FF3,
		0x035, 0x00000086,
		0x035, 0x00000186,
		0x035, 0x00000286,
		0x036, 0x00001C25,
		0x036, 0x00009C25,
		0x036, 0x00011C25,
		0x036, 0x00019C25,
		0x0B6, 0x00048538,
		0x018, 0x00000C07,
		0x05A, 0x0004BD00,
		0x019, 0x000739D0,
		0x034, 0x0000ADF3,
		0x034, 0x00009DF0,
		0x034, 0x00008DED,
		0x034, 0x00007DEA,
		0x034, 0x00006DE7,
		0x034, 0x000054EE,
		0x034, 0x000044EB,
		0x034, 0x000034E8,
		0x034, 0x0000246B,
		0x034, 0x00001468,
		0x034, 0x0000006D,
		0x000, 0x00030159,
		0x084, 0x00068200,
		0x086, 0x000000CE,
		0x087, 0x00048A00,
		0x08E, 0x00065540,
		0x08F, 0x00088000,
		0x0EF, 0x000020A0,
		0x03B, 0x000F02B0,
		0x03B, 0x000EF7B0,
		0x03B, 0x000D4FB0,
		0x03B, 0x000CF060,
		0x03B, 0x000B0090,
		0x03B, 0x000A0080,
		0x03B, 0x00090080,
		0x03B, 0x0008F780,
		0x03B, 0x000722B0,
		0x03B, 0x0006F7B0,
		0x03B, 0x00054FB0,
		0x03B, 0x0004F060,
		0x03B, 0x00030090,
		0x03B, 0x00020080,
		0x03B, 0x00010080,
		0x03B, 0x0000F780,
		0x0EF, 0x000000A0,
		0x000, 0x00010159,
		0x018, 0x0000F407,
		0xFFE, 0x00000000,
		0xFFE, 0x00000000,
		0x01F, 0x00080003,
		0xFFE, 0x00000000,
		0xFFE, 0x00000000,
		0x01E, 0x00000001,
		0x01F, 0x00080000,
		0x000, 0x00033E60,
};

HAL_STATUS
ODM_ReadAndConfig_RadioA_1T_8188E(
 	IN   PDM_ODM_T  pDM_Odm
 	)
{
	#define READ_NEXT_PAIR(v1, v2, i) do { i += 2; v1 = Array[i]; v2 = Array[i+1]; } while(0)

	u4Byte     hex         = 0;
	u4Byte     i           = 0;
	u2Byte     count       = 0;
	pu4Byte    ptr_array   = NULL;
	u1Byte     platform    = pDM_Odm->SupportPlatform;
	u1Byte     interfaceValue   = pDM_Odm->SupportInterface;
	u1Byte     board       = pDM_Odm->BoardType;  
	u4Byte     ArrayLen    = sizeof(Array_RadioA_1T_8188E)/sizeof(u4Byte);
	pu4Byte    Array       = Array_RadioA_1T_8188E;
	BOOLEAN		biol = FALSE;
#ifdef CONFIG_IOL_IOREG_CFG 
	PADAPTER	Adapter =  pDM_Odm->Adapter;	
	struct xmit_frame	*pxmit_frame;	
	u8 bndy_cnt = 1;
	#ifdef CONFIG_IOL_IOREG_CFG_DBG
	struct cmd_cmp cmpdata[ArrayLen];
	u4Byte	cmpdata_idx=0;
	#endif
#endif//#ifdef CONFIG_IOL_IOREG_CFG 
	HAL_STATUS rst =HAL_STATUS_SUCCESS;

	hex += board;
	hex += interfaceValue << 8;
	hex += platform << 16;
	hex += 0xFF000000;
#ifdef CONFIG_IOL_IOREG_CFG 
	biol = rtw_IOL_applied(Adapter);
	
	if(biol){		
		if((pxmit_frame=rtw_IOL_accquire_xmit_frame(Adapter)) == NULL)
		{
			printk("rtw_IOL_accquire_xmit_frame failed\n");
			return HAL_STATUS_FAILURE;
		}
	}		
#endif//#ifdef CONFIG_IOL_IOREG_CFG 
	
	for (i = 0; i < ArrayLen; i += 2 )
	{
	    u4Byte v1 = Array[i];
	    u4Byte v2 = Array[i+1];
	
	    // This (offset, data) pair meets the condition.
	    if ( v1 < 0xCDCDCDCD )
	    {
	    		#ifdef CONFIG_IOL_IOREG_CFG 	
	 		if(biol){	
				if(rtw_IOL_cmd_boundary_handle(pxmit_frame))
					bndy_cnt++;	
				
				if(v1 == 0xffe)
				{ 					
					rtw_IOL_append_DELAY_MS_cmd(pxmit_frame,50);				
				}
				else if (v1 == 0xfd){
					rtw_IOL_append_DELAY_MS_cmd(pxmit_frame,5);
				}
				else if (v1 == 0xfc){
					rtw_IOL_append_DELAY_MS_cmd(pxmit_frame,1);
				}
				else if (v1 == 0xfb){
					rtw_IOL_append_DELAY_US_cmd(pxmit_frame,50);
				}
				else if (v1 == 0xfa){
					rtw_IOL_append_DELAY_US_cmd(pxmit_frame,5);
				}
				else if (v1 == 0xf9){
					rtw_IOL_append_DELAY_US_cmd(pxmit_frame,1);
				}
				else{
					rtw_IOL_append_WRF_cmd(pxmit_frame, ODM_RF_PATH_A,(u2Byte)v1, v2,bRFRegOffsetMask) ;
					#ifdef CONFIG_IOL_IOREG_CFG_DBG
					cmpdata[cmpdata_idx].addr = v1;
					cmpdata[cmpdata_idx].value= v2;
					cmpdata_idx++;
					#endif
				}				
 
	 		}
			else
			#endif	//#ifdef CONFIG_IOL_IOREG_CFG 
			{
		    		odm_ConfigRF_RadioA_8188E(pDM_Odm, v1, v2);
			}
		    continue;
	 	}
		else
		{ // This line is the start line of branch.
		    if ( !CheckCondition(Array[i], hex) )
		    { // Discard the following (offset, data) pairs.
		        READ_NEXT_PAIR(v1, v2, i);
		        while (v2 != 0xDEAD && 
		               v2 != 0xCDEF && 
		               v2 != 0xCDCD && i < ArrayLen -2)
		        {
		            READ_NEXT_PAIR(v1, v2, i);
		        }
		        i -= 2; // prevent from for-loop += 2
		    }
		    else // Configure matched pairs and skip to end of if-else.
		    {
		        READ_NEXT_PAIR(v1, v2, i);
		        while (v2 != 0xDEAD && 
		               v2 != 0xCDEF && 
		               v2 != 0xCDCD && i < ArrayLen -2)
		        {
		    		#ifdef CONFIG_IOL_IOREG_CFG 	
		 		if(biol){	
					if(rtw_IOL_cmd_boundary_handle(pxmit_frame))
						bndy_cnt++;	
					
					if(v1 == 0xffe)
					{ 							
						rtw_IOL_append_DELAY_MS_cmd(pxmit_frame,50);					
					}
					else if (v1 == 0xfd){
						rtw_IOL_append_DELAY_MS_cmd(pxmit_frame,5);
					}
					else if (v1 == 0xfc){
						rtw_IOL_append_DELAY_MS_cmd(pxmit_frame,1);
					}
					else if (v1 == 0xfb){
						rtw_IOL_append_DELAY_US_cmd(pxmit_frame,50);
					}
					else if (v1 == 0xfa){
						rtw_IOL_append_DELAY_US_cmd(pxmit_frame,5);
					}
					else if (v1 == 0xf9){
						rtw_IOL_append_DELAY_US_cmd(pxmit_frame,1);
					}
					else{
						rtw_IOL_append_WRF_cmd(pxmit_frame, ODM_RF_PATH_A,(u2Byte)v1, v2,bRFRegOffsetMask) ;
						#ifdef CONFIG_IOL_IOREG_CFG_DBG
						cmpdata[cmpdata_idx].addr = v1;
						cmpdata[cmpdata_idx].value= v2;
						cmpdata_idx++;
						#endif

					}				
	 
		 		}
				else
				#endif	//#ifdef CONFIG_IOL_IOREG_CFG 
				{
		    			odm_ConfigRF_RadioA_8188E(pDM_Odm, v1, v2);
				}
		            READ_NEXT_PAIR(v1, v2, i);
		        }

		        while (v2 != 0xDEAD && i < ArrayLen -2)
		        {
		            READ_NEXT_PAIR(v1, v2, i);
		        }
		        
		    }
		}	
	}
#ifdef CONFIG_IOL_IOREG_CFG 		
	if(biol){
		//printk("==> %s, pktlen = %d,bndy_cnt = %d\n",__FUNCTION__,pxmit_frame->attrib.pktlen+4+32,bndy_cnt);
		if(rtw_IOL_exec_cmds_sync(pDM_Odm->Adapter, pxmit_frame, 1000, bndy_cnt))
		{			
			#ifdef CONFIG_IOL_IOREG_CFG_DBG
			printk("~~~ %s Success !!! \n",__FUNCTION__);
			{
				u4Byte idx;
				u4Byte cdata;
				printk("  %s data compare => array_len:%d \n",__FUNCTION__,cmpdata_idx);
				printk("### %s data compared !!###\n",__FUNCTION__);
				for(idx=0;idx< cmpdata_idx;idx++)
				{
					cdata = ODM_GetRFReg(pDM_Odm, ODM_RF_PATH_A,cmpdata[idx].addr,bRFRegOffsetMask);
					if(cdata != cmpdata[idx].value){
						printk("addr:0x%04x, data:(0x%02x : 0x%02x) \n",
							cmpdata[idx].addr,cmpdata[idx].value,cdata);
						rst = HAL_STATUS_FAILURE;
					}					
				}	
				printk("### %s data compared !!###\n",__FUNCTION__);
				//if(rst == HAL_STATUS_FAILURE)
				{//dump data from TX packet buffer				
					rtw_IOL_cmd_tx_pkt_buf_dump(pDM_Odm->Adapter,pxmit_frame->attrib.pktlen+32);
				}
			}
			#endif //CONFIG_IOL_IOREG_CFG_DBG
		
		}
		else{
			rst = HAL_STATUS_FAILURE;
			printk("~~~ IOL Config %s Failed !!! \n",__FUNCTION__);
			#ifdef CONFIG_IOL_IOREG_CFG_DBG
			{
				//dump data from TX packet buffer				
				rtw_IOL_cmd_tx_pkt_buf_dump(pDM_Odm->Adapter,pxmit_frame->attrib.pktlen+32);
			}
			#endif //CONFIG_IOL_IOREG_CFG_DBG
		}
	}

	 
#endif	//#ifdef CONFIG_IOL_IOREG_CFG 
	return rst;
}

#endif // end of HWIMG_SUPPORT

