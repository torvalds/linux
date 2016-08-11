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

//============================================================
// include files
//============================================================
#include "mp_precomp.h"
#include "phydm_precomp.h"

#if(defined(CONFIG_PATH_DIVERSITY))
#if RTL8814A_SUPPORT

VOID
phydm_dtp_fix_tx_path(
	IN	PVOID	pDM_VOID,
	IN	u1Byte	path
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pPATHDIV_T		pDM_PathDiv = &pDM_Odm->DM_PathDiv;
	u1Byte			i,num_enable_path=0;

	if(path==pDM_PathDiv->pre_tx_path)
	{
		return;
	}
	else
	{
		pDM_PathDiv->pre_tx_path=path;
	}

	ODM_SetBBReg( pDM_Odm, 0x93c, BIT18|BIT19, 3);

	for(i=0; i<4; i++)
	{
		if(path&BIT(i))
			num_enable_path++;
	}
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, (" Number of trun-on path : (( %d ))\n", num_enable_path));

	if(num_enable_path == 1)
	{
		ODM_SetBBReg( pDM_Odm, 0x93c, 0xf00000, path);
	
		if(path==PHYDM_A)//1-1
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, (" Trun on path (( A ))\n"));
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT25|BIT24, 0);
		}
		else 	if(path==PHYDM_B)//1-2
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, (" Trun on path (( B ))\n"));
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT27|BIT26, 0);
		}
		else 	if(path==PHYDM_C)//1-3
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, (" Trun on path (( C ))\n"));
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT29|BIT28, 0);

		}
		else 	if(path==PHYDM_D)//1-4
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, (" Trun on path (( D ))\n"));
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT31|BIT30, 0);
		}

	}
	else 	if(num_enable_path == 2)
	{
		ODM_SetBBReg( pDM_Odm, 0x93c, 0xf00000, path);
		ODM_SetBBReg( pDM_Odm, 0x940, 0xf0, path);
	
		if(path==PHYDM_AB)//2-1
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, (" Trun on path (( A B ))\n"));
			//set for 1ss
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT25|BIT24, 0);
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT27|BIT26, 1);
			//set for 2ss
			ODM_SetBBReg( pDM_Odm, 0x940, BIT9|BIT8, 0);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT11|BIT10, 1);			
		}
		else 	if(path==PHYDM_AC)//2-2
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, (" Trun on path (( A C ))\n"));
			//set for 1ss
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT25|BIT24, 0);
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT29|BIT28, 1);
			//set for 2ss
			ODM_SetBBReg( pDM_Odm, 0x940, BIT9|BIT8, 0);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT13|BIT12, 1);	
		}
		else 	if(path==PHYDM_AD)//2-3
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, (" Trun on path (( A D ))\n"));
			//set for 1ss
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT25|BIT24, 0);
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT31|BIT30, 1);
			//set for 2ss
			ODM_SetBBReg( pDM_Odm, 0x940, BIT9|BIT8, 0);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT15|BIT14, 1);	
		}
		else 	if(path==PHYDM_BC)//2-4
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, (" Trun on path (( B C ))\n"));
			//set for 1ss
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT27|BIT26, 0);
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT29|BIT28, 1);
			//set for 2ss
			ODM_SetBBReg( pDM_Odm, 0x940, BIT11|BIT10, 0);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT13|BIT12, 1);	
		}
		else 	if(path==PHYDM_BD)//2-5
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, (" Trun on path (( B D ))\n"));
			//set for 1ss
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT27|BIT26, 0);
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT31|BIT30, 1);
			//set for 2ss
			ODM_SetBBReg( pDM_Odm, 0x940, BIT11|BIT10, 0);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT15|BIT14, 1);	
		}
		else 	if(path==PHYDM_CD)//2-6
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, (" Trun on path (( C D ))\n"));
			//set for 1ss
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT29|BIT28, 0);
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT31|BIT30, 1);
			//set for 2ss
			ODM_SetBBReg( pDM_Odm, 0x940, BIT13|BIT12, 0);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT15|BIT14, 1);	
		}

	}
	else 	if(num_enable_path == 3)
	{
		ODM_SetBBReg( pDM_Odm, 0x93c, 0xf00000, path);
		ODM_SetBBReg( pDM_Odm, 0x940, 0xf0, path);
		ODM_SetBBReg( pDM_Odm, 0x940, 0xf0000, path);
	
		if(path==PHYDM_ABC)//3-1
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, (" Trun on path (( A B C))\n"));
			//set for 1ss
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT25|BIT24, 0);
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT27|BIT26, 1);
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT29|BIT28, 2);			
			//set for 2ss
			ODM_SetBBReg( pDM_Odm, 0x940, BIT9|BIT8, 0);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT11|BIT10, 1);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT13|BIT12, 2);
			//set for 3ss
			ODM_SetBBReg( pDM_Odm, 0x940, BIT21|BIT20, 0);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT23|BIT22, 1);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT25|BIT24, 2);
		}
		else 	if(path==PHYDM_ABD)//3-2
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, (" Trun on path (( A B D ))\n"));
			//set for 1ss
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT25|BIT24, 0);
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT27|BIT26, 1);		
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT31|BIT30, 2);
			//set for 2ss
			ODM_SetBBReg( pDM_Odm, 0x940, BIT9|BIT8, 0);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT11|BIT10, 1);		
			ODM_SetBBReg( pDM_Odm, 0x940, BIT15|BIT14, 2);
			//set for 3ss
			ODM_SetBBReg( pDM_Odm, 0x940, BIT21|BIT20, 0);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT23|BIT22, 1);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT27|BIT26, 2);

		}
		else 	if(path==PHYDM_ACD)//3-3
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, (" Trun on path (( A C D ))\n"));
			//set for 1ss
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT25|BIT24, 0);
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT29|BIT28, 1);
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT31|BIT30, 2);			
			//set for 2ss
			ODM_SetBBReg( pDM_Odm, 0x940, BIT9|BIT8, 0);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT13|BIT12, 1);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT15|BIT14, 2);
			//set for 3ss
			ODM_SetBBReg( pDM_Odm, 0x940, BIT21|BIT20, 0);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT25|BIT24, 1);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT27|BIT26, 2);
		}
		else 	if(path==PHYDM_BCD)//3-4
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, (" Trun on path (( B C D))\n"));
			//set for 1ss
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT27|BIT26, 0);
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT29|BIT28, 1);
			ODM_SetBBReg( pDM_Odm, 0x93c, BIT31|BIT30, 2);			
			//set for 2ss
			ODM_SetBBReg( pDM_Odm, 0x940, BIT11|BIT10, 0);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT13|BIT12, 1);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT15|BIT14, 2);
			//set for 3ss
			ODM_SetBBReg( pDM_Odm, 0x940, BIT23|BIT22, 0);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT25|BIT24, 1);
			ODM_SetBBReg( pDM_Odm, 0x940, BIT27|BIT26, 2);
		}
	}
	else 	if(num_enable_path == 4)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, (" Trun on path ((A  B C D))\n"));
	}

}

VOID
phydm_find_default_path(
	IN	PVOID	pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pPATHDIV_T		pDM_PathDiv = &pDM_Odm->DM_PathDiv;
	u4Byte	rssi_avg_a=0, rssi_avg_b=0, rssi_avg_c=0, rssi_avg_d=0, rssi_avg_bcd=0;
	u4Byte	rssi_total_a=0, rssi_total_b=0, rssi_total_c=0, rssi_total_d=0;

	//2 Default Path Selection By RSSI

	rssi_avg_a = (pDM_PathDiv->path_a_cnt_all > 0)? (pDM_PathDiv->path_a_sum_all / pDM_PathDiv->path_a_cnt_all) :0 ;
	rssi_avg_b = (pDM_PathDiv->path_b_cnt_all > 0)? (pDM_PathDiv->path_b_sum_all / pDM_PathDiv->path_b_cnt_all) :0 ;
	rssi_avg_c = (pDM_PathDiv->path_c_cnt_all > 0)? (pDM_PathDiv->path_c_sum_all / pDM_PathDiv->path_c_cnt_all) :0 ;
	rssi_avg_d = (pDM_PathDiv->path_d_cnt_all > 0)? (pDM_PathDiv->path_d_sum_all / pDM_PathDiv->path_d_cnt_all) :0 ;


	pDM_PathDiv->path_a_sum_all = 0;
 	pDM_PathDiv->path_a_cnt_all = 0;
	pDM_PathDiv->path_b_sum_all = 0;
 	pDM_PathDiv->path_b_cnt_all = 0;
	pDM_PathDiv->path_c_sum_all = 0;
 	pDM_PathDiv->path_c_cnt_all = 0;
	pDM_PathDiv->path_d_sum_all = 0;
 	pDM_PathDiv->path_d_cnt_all = 0;

	if(pDM_PathDiv->use_path_a_as_default_ant == 1)
	{
		rssi_avg_bcd=(rssi_avg_b+rssi_avg_c+rssi_avg_d)/3;

		if( (rssi_avg_a + ANT_DECT_RSSI_TH) > rssi_avg_bcd  )
		{
			pDM_PathDiv->is_pathA_exist=TRUE;
			pDM_PathDiv->default_path=PATH_A;
		}
		else
		{
			pDM_PathDiv->is_pathA_exist=FALSE;
		}
	}
	else
	{
		if( (rssi_avg_a >=rssi_avg_b) && (rssi_avg_a >=rssi_avg_c)&&(rssi_avg_a >=rssi_avg_d))
			pDM_PathDiv->default_path=PATH_A;
		else if(  (rssi_avg_b >=rssi_avg_c)&&(rssi_avg_b >=rssi_avg_d))
			pDM_PathDiv->default_path=PATH_B;
		else if(  rssi_avg_c >=rssi_avg_d)
			pDM_PathDiv->default_path=PATH_C;
		else
			pDM_PathDiv->default_path=PATH_D;
	}


}


VOID
phydm_candidate_dtp_update(
	IN	PVOID	pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pPATHDIV_T		pDM_PathDiv = &pDM_Odm->DM_PathDiv;

	pDM_PathDiv->num_candidate=3;
	
	if(pDM_PathDiv->use_path_a_as_default_ant == 1)
	{
		if(pDM_PathDiv->num_tx_path==3)
		{
			if(pDM_PathDiv->is_pathA_exist)
			{
				pDM_PathDiv->ant_candidate_1 =  PHYDM_ABC; 
				pDM_PathDiv->ant_candidate_2 =  PHYDM_ABD; 
				pDM_PathDiv->ant_candidate_3 =  PHYDM_ACD;
			}
			else // use path BCD 
			{
				pDM_PathDiv->num_candidate=1;
				phydm_dtp_fix_tx_path(pDM_Odm, PHYDM_BCD);
				return;
			}
		}
		else 	if(pDM_PathDiv->num_tx_path==2)
		{
			if(pDM_PathDiv->is_pathA_exist)
			{
				pDM_PathDiv->ant_candidate_1 =  PHYDM_AB; 
				pDM_PathDiv->ant_candidate_2 =  PHYDM_AC; 
				pDM_PathDiv->ant_candidate_3 =  PHYDM_AD; 
			}
			else
			{
				pDM_PathDiv->ant_candidate_1 =  PHYDM_BC; 
				pDM_PathDiv->ant_candidate_2 =  PHYDM_BD; 
				pDM_PathDiv->ant_candidate_3 =  PHYDM_CD; 
			}	
		}
	}
	else
	{
		//2 3 TX Mode 
		if(pDM_PathDiv->num_tx_path==3)//choose 3 ant form 4 
		{
			if(pDM_PathDiv->default_path == PATH_A) //choose 2 ant form 3
			{
				pDM_PathDiv->ant_candidate_1 =  PHYDM_ABC; 
				pDM_PathDiv->ant_candidate_2 =  PHYDM_ABD; 
				pDM_PathDiv->ant_candidate_3 =  PHYDM_ACD; 
			}
			else if(pDM_PathDiv->default_path==PATH_B)
			{
				pDM_PathDiv->ant_candidate_1 =  PHYDM_ABC; 
				pDM_PathDiv->ant_candidate_2 =  PHYDM_ABD; 
				pDM_PathDiv->ant_candidate_3 =  PHYDM_BCD; 
			}
			else if(pDM_PathDiv->default_path == PATH_C)
			{
				pDM_PathDiv->ant_candidate_1 =  PHYDM_ABC; 
				pDM_PathDiv->ant_candidate_2 =  PHYDM_ACD; 
				pDM_PathDiv->ant_candidate_3 =  PHYDM_BCD; 
			}
			else if(pDM_PathDiv->default_path == PATH_D)
			{
				pDM_PathDiv->ant_candidate_1 =  PHYDM_ABD; 
				pDM_PathDiv->ant_candidate_2 =  PHYDM_ACD; 
				pDM_PathDiv->ant_candidate_3 =  PHYDM_BCD; 
			}
		}
		
		//2 2 TX Mode 
		else if(pDM_PathDiv->num_tx_path==2)//choose 2 ant form 4 
		{
			if(pDM_PathDiv->default_path == PATH_A) //choose 2 ant form 3
			{
				pDM_PathDiv->ant_candidate_1 =  PHYDM_AB; 
				pDM_PathDiv->ant_candidate_2 =  PHYDM_AC; 
				pDM_PathDiv->ant_candidate_3 =  PHYDM_AD; 
			}
			else if(pDM_PathDiv->default_path==PATH_B)
			{
				pDM_PathDiv->ant_candidate_1 =  PHYDM_AB; 
				pDM_PathDiv->ant_candidate_2 =  PHYDM_BC; 
				pDM_PathDiv->ant_candidate_3 =  PHYDM_BD; 
			}
			else if(pDM_PathDiv->default_path == PATH_C)
			{
				pDM_PathDiv->ant_candidate_1 =  PHYDM_AC; 
				pDM_PathDiv->ant_candidate_2 =  PHYDM_BC; 
				pDM_PathDiv->ant_candidate_3 =  PHYDM_CD; 
			}
			else if(pDM_PathDiv->default_path == PATH_D)
			{
				pDM_PathDiv->ant_candidate_1=  PHYDM_AD; 
				pDM_PathDiv->ant_candidate_2 =  PHYDM_BD; 
				pDM_PathDiv->ant_candidate_3=  PHYDM_CD; 
			}
		}
	}
}


VOID
phydm_dynamic_tx_path(
	IN	PVOID	pDM_VOID
)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pPATHDIV_T		pDM_PathDiv = &pDM_Odm->DM_PathDiv;	
	
	PSTA_INFO_T   	pEntry;
	u4Byte	i;
	u1Byte	num_client=0;
	u1Byte	H2C_Parameter[6] ={0};


	if(!pDM_Odm->bLinked) //bLinked==False
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("DTP_8814 [No Link!!!]\n"));
		
		if(pDM_PathDiv->bBecomeLinked == TRUE)
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, (" [Be disconnected]----->\n"));
			pDM_PathDiv->bBecomeLinked = pDM_Odm->bLinked;
		}
		return;
	}	
	else
	{
		if(pDM_PathDiv->bBecomeLinked ==FALSE)
		{
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, (" [Be Linked !!!]----->\n"));
			pDM_PathDiv->bBecomeLinked = pDM_Odm->bLinked;
		}	
	}	
	
	//2 [Period CTRL]
	if(pDM_PathDiv->dtp_period >=2)
	{
		pDM_PathDiv->dtp_period=0;	
	}
	else
	{	
		//ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("Phydm_Dynamic_Tx_Path_8814A()  Stay = (( %d ))\n",pDM_PathDiv->dtp_period));
		pDM_PathDiv->dtp_period++;		
		return;
	}
	

	//2 [Fix Path]
	if (pDM_Odm->path_select != PHYDM_AUTO_PATH)
	{
		return;
	}
	
	//2 [Check Bfer]	
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	#if (BEAMFORMING_SUPPORT == 1)
	{
		BEAMFORMING_CAP		BeamformCap = (pDM_Odm->BeamformingInfo.BeamformCap);

		if( BeamformCap & BEAMFORMER_CAP ) //  BFmer On  &&   Div On ->  Div Off
		{	
			if( pDM_PathDiv->fix_path_bfer == 0) 
			{
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_PATH_DIV,ODM_DBG_LOUD,("[ PathDiv : OFF ]   BFmer ==1  \n"));
				pDM_PathDiv->fix_path_bfer = 1 ;			
			}
			return;
		}
		else // BFmer Off   &&   Div Off ->  Div On
		{
			if( pDM_PathDiv->fix_path_bfer == 1 ) 
			{
				ODM_RT_TRACE(pDM_Odm, ODM_COMP_PATH_DIV,ODM_DBG_LOUD,("[ PathDiv : ON ]   BFmer ==0 \n"));
				pDM_PathDiv->fix_path_bfer = 0;
			}
		}
	}
	#endif	
	#endif

	if(pDM_PathDiv->use_path_a_as_default_ant ==1)
	{
		phydm_find_default_path(pDM_Odm);
		phydm_candidate_dtp_update(pDM_Odm);	
	}
	else
	{
		if( pDM_PathDiv->dtp_state == PHYDM_DTP_INIT)
		{
			phydm_find_default_path(pDM_Odm);
			phydm_candidate_dtp_update(pDM_Odm);
			pDM_PathDiv->dtp_state = PHYDM_DTP_RUNNING_1;
		}
		
		else 	if( pDM_PathDiv->dtp_state == PHYDM_DTP_RUNNING_1)
		{
			pDM_PathDiv->dtp_check_patha_counter++;
			
			if(pDM_PathDiv->dtp_check_patha_counter>=NUM_RESET_DTP_PERIOD)
			{
				pDM_PathDiv->dtp_check_patha_counter=0;
				pDM_PathDiv->dtp_state = PHYDM_DTP_INIT;
			}
			//2 Search space update		
			else
			{
				// 1.  find the worst candidate
				

				// 2. repalce the worst candidate
			}
		}
	}

	//2 Dynamic Path Selection H2C

	if(pDM_PathDiv->num_candidate == 1)
	{
		return;
	}
	else
	{	
		H2C_Parameter[0] =  pDM_PathDiv->num_candidate;
		H2C_Parameter[1] =  pDM_PathDiv->num_tx_path;	
		H2C_Parameter[2] =  pDM_PathDiv->ant_candidate_1; 
		H2C_Parameter[3] =  pDM_PathDiv->ant_candidate_2; 
		H2C_Parameter[4] =  pDM_PathDiv->ant_candidate_3; 

		ODM_FillH2CCmd(pDM_Odm, PHYDM_H2C_DYNAMIC_TX_PATH, 6, H2C_Parameter);
	}

}



VOID
phydm_dynamic_tx_path_init(
	IN	PVOID	pDM_VOID
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pPATHDIV_T		pDM_PathDiv  = &(pDM_Odm->DM_PathDiv);
	PADAPTER		pAdapter = pDM_Odm->Adapter;
	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
	USB_MODE_MECH	*pUsbModeMech = &pAdapter->UsbModeMechanism;
	#endif
	u1Byte 			search_space_2[NUM_CHOOSE2_FROM4]= {PHYDM_AB, PHYDM_AC, PHYDM_AD, PHYDM_BC, PHYDM_BD, PHYDM_CD };
	u1Byte 			search_space_3[NUM_CHOOSE3_FROM4]= {PHYDM_BCD, PHYDM_ACD,  PHYDM_ABD, PHYDM_ABC};

	#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
		pDM_PathDiv->is_u3_mode = (pUsbModeMech->CurUsbMode==USB_MODE_U3)? 1 : 0 ;
	#else
		pDM_PathDiv->is_u3_mode = 1;
	#endif
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("Dynamic TX Path Init 8814\n"));
	ODM_RT_TRACE(pDM_Odm, ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("is_u3_mode = (( %d ))\n", pDM_PathDiv->is_u3_mode));

	memcpy(&(pDM_PathDiv->search_space_2[0]), &(search_space_2[0]), NUM_CHOOSE2_FROM4);
	memcpy(&(pDM_PathDiv->search_space_3[0]), &(search_space_3[0]), NUM_CHOOSE3_FROM4);

	pDM_PathDiv->use_path_a_as_default_ant= 1;
	pDM_PathDiv->dtp_state = PHYDM_DTP_INIT;	
	pDM_Odm->path_select = PHYDM_AUTO_PATH;
	pDM_PathDiv->path_div_type = PHYDM_4R_PATH_DIV;

	
	if(pDM_PathDiv->is_u3_mode )
	{
		pDM_PathDiv->num_tx_path=3;
		phydm_dtp_fix_tx_path(pDM_Odm, PHYDM_BCD);/* 3TX  Set Init TX Path*/
		
	}
	else
	{
		pDM_PathDiv->num_tx_path=2;
		phydm_dtp_fix_tx_path(pDM_Odm, PHYDM_BC);/* 2TX // Set Init TX Path*/
	}
	
}


VOID
phydm_process_rssi_for_path_div(	
	IN OUT		PVOID			pDM_VOID,	
	IN			PVOID			p_phy_info_void,
	IN			PVOID			p_pkt_info_void
	)
{
	PDM_ODM_T			pDM_Odm = (PDM_ODM_T)pDM_VOID;
	PODM_PHY_INFO_T 		pPhyInfo=(PODM_PHY_INFO_T)p_phy_info_void;
	PODM_PACKET_INFO_T	 pPktinfo=(PODM_PACKET_INFO_T)p_pkt_info_void;
	pPATHDIV_T			pDM_PathDiv  = &(pDM_Odm->DM_PathDiv);

	if(pPktinfo->bPacketToSelf || pPktinfo->bPacketMatchBSSID)
	{
		if(pPktinfo->DataRate > ODM_RATE11M)
		{
			if(pDM_PathDiv->path_div_type == PHYDM_4R_PATH_DIV)
			{
				#if RTL8814A_SUPPORT
				if(pDM_Odm->SupportICType & ODM_RTL8814A)
				{
					pDM_PathDiv->path_a_sum_all+=pPhyInfo->RxMIMOSignalStrength[0];
					pDM_PathDiv->path_a_cnt_all++;
					
					pDM_PathDiv->path_b_sum_all+=pPhyInfo->RxMIMOSignalStrength[1];
					pDM_PathDiv->path_b_cnt_all++;
					
					pDM_PathDiv->path_c_sum_all+=pPhyInfo->RxMIMOSignalStrength[2];
					pDM_PathDiv->path_c_cnt_all++;
					
					pDM_PathDiv->path_d_sum_all+=pPhyInfo->RxMIMOSignalStrength[3];
					pDM_PathDiv->path_d_cnt_all++;
				}
				#endif
			}
			else
			{
				pDM_PathDiv->PathA_Sum[pPktinfo->StationID]+=pPhyInfo->RxMIMOSignalStrength[0];
				pDM_PathDiv->PathA_Cnt[pPktinfo->StationID]++;

				pDM_PathDiv->PathB_Sum[pPktinfo->StationID]+=pPhyInfo->RxMIMOSignalStrength[1];
				pDM_PathDiv->PathB_Cnt[pPktinfo->StationID]++;
			}
		}
	}
	
	
}

#endif //#if RTL8814A_SUPPORT

VOID
odm_pathdiv_debug(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		*const dm_value,
	IN		u4Byte		*_used,
	OUT		char			*output,
	IN		u4Byte		*_out_len
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pPATHDIV_T			pDM_PathDiv  = &(pDM_Odm->DM_PathDiv);
	u4Byte used = *_used;
	u4Byte out_len = *_out_len;
	
	pDM_Odm->path_select = (dm_value[0] & 0xf);
	PHYDM_SNPRINTF((output+used, out_len-used,"Path_select = (( 0x%x ))\n",pDM_Odm->path_select ));
	
	//2 [Fix Path]
	if (pDM_Odm->path_select != PHYDM_AUTO_PATH)
	{
		PHYDM_SNPRINTF((output+used, out_len-used,"Trun on path  [%s%s%s%s]\n",
			((pDM_Odm->path_select) & 0x1)?"A":"",
			((pDM_Odm->path_select) & 0x2)?"B":"",
			((pDM_Odm->path_select) & 0x4)?"C":"",
			((pDM_Odm->path_select) & 0x8)?"D":"" ));
		
		phydm_dtp_fix_tx_path( pDM_Odm, pDM_Odm->path_select );
	}
	else
	{
		PHYDM_SNPRINTF((output+used, out_len-used,"%s\n","Auto Path"));
	}
}

#endif // #if(defined(CONFIG_PATH_DIVERSITY))

VOID
phydm_c2h_dtp_handler(
 IN	PVOID	pDM_VOID,
 IN 	pu1Byte   CmdBuf,
 IN 	u1Byte	CmdLen
)
{
#if(defined(CONFIG_PATH_DIVERSITY))
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	pPATHDIV_T		pDM_PathDiv  = &(pDM_Odm->DM_PathDiv);

	u1Byte  macid = CmdBuf[0]; 
	u1Byte  target = CmdBuf[1];	
	u1Byte  nsc_1 = CmdBuf[2];
	u1Byte  nsc_2 = CmdBuf[3];
	u1Byte  nsc_3 = CmdBuf[4];

	ODM_RT_TRACE(pDM_Odm, ODM_COMP_PATH_DIV,ODM_DBG_LOUD,("Target_candidate = (( %d ))\n", target));
	/*
	if( (nsc_1 >= nsc_2) &&  (nsc_1 >= nsc_3))
	{
		phydm_dtp_fix_tx_path(pDM_Odm, pDM_PathDiv->ant_candidate_1);
	}
	else 	if( nsc_2 >= nsc_3)
	{
		phydm_dtp_fix_tx_path(pDM_Odm, pDM_PathDiv->ant_candidate_2);
	}
	else
	{
		phydm_dtp_fix_tx_path(pDM_Odm, pDM_PathDiv->ant_candidate_3);	
	}
	*/
#endif	
}

VOID
odm_PathDiversity(
	IN	PVOID	pDM_VOID
)
{
#if(defined(CONFIG_PATH_DIVERSITY))
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	if(!(pDM_Odm->SupportAbility & ODM_BB_PATH_DIV))
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_PATH_DIV,ODM_DBG_LOUD,("Return: Not Support PathDiv\n"));
		return;
	}

	#if RTL8812A_SUPPORT

	if(pDM_Odm->SupportICType & ODM_RTL8812)
			ODM_PathDiversity_8812A(pDM_Odm);
		else
	#endif

	#if RTL8814A_SUPPORT
		if(pDM_Odm->SupportICType & ODM_RTL8814A)
			phydm_dynamic_tx_path(pDM_Odm);
		else
	#endif
    		{}
#endif
}

VOID
odm_PathDiversityInit(
	IN	PVOID	pDM_VOID
)
{
#if(defined(CONFIG_PATH_DIVERSITY))
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;

	/*pDM_Odm->SupportAbility |= ODM_BB_PATH_DIV;*/
	
	if(pDM_Odm->mp_mode == TRUE)
		return;

	if(!(pDM_Odm->SupportAbility & ODM_BB_PATH_DIV))
	{
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_PATH_DIV,ODM_DBG_LOUD,("Return: Not Support PathDiv\n"));
		return;
	}

#if RTL8812A_SUPPORT
		if(pDM_Odm->SupportICType & ODM_RTL8812)
			ODM_PathDiversityInit_8812A(pDM_Odm);
		else
	#endif

	#if RTL8814A_SUPPORT
		if(pDM_Odm->SupportICType & ODM_RTL8814A)
			phydm_dynamic_tx_path_init(pDM_Odm);
		else
	#endif	
		{}
#endif
}



#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
//
// 2011/12/02 MH Copy from MP oursrc for temporarily test.
//
#if RTL8192C_SUPPORT
BOOLEAN
odm_IsConnected_92C(
	IN	PADAPTER	Adapter
)
{
	PRT_WLAN_STA	pEntry;
	PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);
	u4Byte		i;
	BOOLEAN		bConnected=FALSE;
	
	if(pMgntInfo->mAssoc)
	{
		bConnected = TRUE;
	}
	else
	{
		for(i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++)
		{
			if(IsAPModeExist(Adapter)  && GetFirstExtAdapter(Adapter) != NULL)
				pEntry = AsocEntry_EnumStation(GetFirstExtAdapter(Adapter), i);
			else
				pEntry = AsocEntry_EnumStation(GetDefaultAdapter(Adapter), i);

			if(pEntry!=NULL)
			{
				if(pEntry->bAssociated)
				{
					bConnected = TRUE;
					break;
				}
			}
			else
			{
				break;
			}
		}
	}
	return	bConnected;
}

BOOLEAN
ODM_PathDiversityBeforeLink92C(
	//IN	PADAPTER	Adapter
	IN		PDM_ODM_T		pDM_Odm
	)
{
#if (RT_MEM_SIZE_LEVEL != RT_MEM_SIZE_MINIMUM)
	PADAPTER		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE*	pHalData = NULL;
	PMGNT_INFO		pMgntInfo = NULL;
	//pSWAT_T		pDM_SWAT_Table = &Adapter->DM_SWAT_Table;
	pPD_T			pDM_PDTable = NULL;

	s1Byte			Score = 0;
	PRT_WLAN_BSS	pTmpBssDesc;
	PRT_WLAN_BSS	pTestBssDesc;

	u1Byte			target_chnl = 0;
	u2Byte			index;

	if (pDM_Odm->Adapter == NULL)  //For BSOD when plug/unplug fast.  //By YJ,120413
	{	// The ODM structure is not initialized.
		return FALSE;
	}
	pHalData = GET_HAL_DATA(Adapter);
	pMgntInfo = &Adapter->MgntInfo;
	pDM_PDTable = &Adapter->DM_PDTable;
	
	// Condition that does not need to use path diversity.
	if((!(pHalData->CVID_Version==VERSION_1_BEFORE_8703B && IS_92C_SERIAL(pHalData->VersionID))) || (pHalData->PathDivCfg!=1) || pMgntInfo->AntennaTest )
	{
		RT_TRACE(COMP_INIT, DBG_LOUD, 
				("ODM_PathDiversityBeforeLink92C(): No PathDiv Mechanism before link.\n"));
		return FALSE;
	}

	// Since driver is going to set BB register, it shall check if there is another thread controlling BB/RF.
	PlatformAcquireSpinLock(Adapter, RT_RF_STATE_SPINLOCK);
	if(pHalData->eRFPowerState!=eRfOn || pMgntInfo->RFChangeInProgress || pMgntInfo->bMediaConnect)
	{
		PlatformReleaseSpinLock(Adapter, RT_RF_STATE_SPINLOCK);
	
		RT_TRACE(COMP_INIT, DBG_LOUD, 
				("ODM_PathDiversityBeforeLink92C(): RFChangeInProgress(%x), eRFPowerState(%x)\n", 
				pMgntInfo->RFChangeInProgress,
				pHalData->eRFPowerState));
	
		//pDM_SWAT_Table->SWAS_NoLink_State = 0;
		pDM_PDTable->PathDiv_NoLink_State = 0;
		
		return FALSE;
	}
	else
	{
		PlatformReleaseSpinLock(Adapter, RT_RF_STATE_SPINLOCK);
	}

	//1 Run AntDiv mechanism "Before Link" part.
	//if(pDM_SWAT_Table->SWAS_NoLink_State == 0)
	if(pDM_PDTable->PathDiv_NoLink_State == 0)
	{
		//1 Prepare to do Scan again to check current antenna state.

		// Set check state to next step.
		//pDM_SWAT_Table->SWAS_NoLink_State = 1;
		pDM_PDTable->PathDiv_NoLink_State = 1;
	
		// Copy Current Scan list.
		Adapter->MgntInfo.tmpNumBssDesc = pMgntInfo->NumBssDesc;
		PlatformMoveMemory((PVOID)Adapter->MgntInfo.tmpbssDesc, (PVOID)pMgntInfo->bssDesc, sizeof(RT_WLAN_BSS)*MAX_BSS_DESC);

		// Switch Antenna to another one.
		if(pDM_PDTable->DefaultRespPath == 0)
		{
			PHY_SetBBReg(Adapter, rCCK0_AFESetting  , 0x0F000000, 0x05); // TRX path = PathB
			odm_SetRespPath_92C(Adapter, 1);
			pDM_PDTable->OFDMTXPath = 0xFFFFFFFF;
			pDM_PDTable->CCKTXPath = 0xFFFFFFFF;
		}
		else
		{
			PHY_SetBBReg(Adapter, rCCK0_AFESetting  , 0x0F000000, 0x00); // TRX path = PathA
			odm_SetRespPath_92C(Adapter, 0);
			pDM_PDTable->OFDMTXPath = 0x0;
			pDM_PDTable->CCKTXPath = 0x0;
		}
#if 0	

		pDM_SWAT_Table->PreAntenna = pDM_SWAT_Table->CurAntenna;
		pDM_SWAT_Table->CurAntenna = (pDM_SWAT_Table->CurAntenna==Antenna_A)?Antenna_B:Antenna_A;
		
		RT_TRACE(COMP_INIT, DBG_LOUD, 
			("ODM_SwAntDivCheckBeforeLink: Change to Ant(%s) for testing.\n", (pDM_SWAT_Table->CurAntenna==Antenna_A)?"A":"B"));
		//PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, 0x300, DM_SWAT_Table.CurAntenna);
		pDM_SWAT_Table->SWAS_NoLink_BK_Reg860 = ((pDM_SWAT_Table->SWAS_NoLink_BK_Reg860 & 0xfffffcff) | (pDM_SWAT_Table->CurAntenna<<8));
		PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, bMaskDWord, pDM_SWAT_Table->SWAS_NoLink_BK_Reg860);
#endif

		// Go back to scan function again.
		RT_TRACE(COMP_INIT, DBG_LOUD, ("ODM_PathDiversityBeforeLink92C: Scan one more time\n"));
		pMgntInfo->ScanStep=0;
		target_chnl = odm_SwAntDivSelectScanChnl(Adapter);
		odm_SwAntDivConstructScanChnl(Adapter, target_chnl);
		PlatformSetTimer(Adapter, &pMgntInfo->ScanTimer, 5);

		return TRUE;
	}
	else
	{
		//1 ScanComple() is called after antenna swiched.
		//1 Check scan result and determine which antenna is going
		//1 to be used.

		for(index=0; index<Adapter->MgntInfo.tmpNumBssDesc; index++)
		{
			pTmpBssDesc = &(Adapter->MgntInfo.tmpbssDesc[index]);
			pTestBssDesc = &(pMgntInfo->bssDesc[index]);

			if(PlatformCompareMemory(pTestBssDesc->bdBssIdBuf, pTmpBssDesc->bdBssIdBuf, 6)!=0)
			{
				RT_TRACE(COMP_INIT, DBG_LOUD, ("ODM_PathDiversityBeforeLink92C(): ERROR!! This shall not happen.\n"));
				continue;
			}

			if(pTmpBssDesc->RecvSignalPower > pTestBssDesc->RecvSignalPower)
			{
				RT_TRACE(COMP_INIT, DBG_LOUD, ("ODM_PathDiversityBeforeLink92C: Compare scan entry: Score++\n"));
				RT_PRINT_STR(COMP_INIT, DBG_LOUD, "SSID: ", pTestBssDesc->bdSsIdBuf, pTestBssDesc->bdSsIdLen);
				RT_TRACE(COMP_INIT, DBG_LOUD, ("Original: %d, Test: %d\n", pTmpBssDesc->RecvSignalPower, pTestBssDesc->RecvSignalPower));
			
				Score++;
				PlatformMoveMemory(pTestBssDesc, pTmpBssDesc, sizeof(RT_WLAN_BSS));
			}
			else if(pTmpBssDesc->RecvSignalPower < pTestBssDesc->RecvSignalPower)
			{
				RT_TRACE(COMP_INIT, DBG_LOUD, ("ODM_PathDiversityBeforeLink92C: Compare scan entry: Score--\n"));
				RT_PRINT_STR(COMP_INIT, DBG_LOUD, "SSID: ", pTestBssDesc->bdSsIdBuf, pTestBssDesc->bdSsIdLen);
				RT_TRACE(COMP_INIT, DBG_LOUD, ("Original: %d, Test: %d\n", pTmpBssDesc->RecvSignalPower, pTestBssDesc->RecvSignalPower));
				Score--;
			}

		}

		if(pMgntInfo->NumBssDesc!=0 && Score<=0)
		{
			RT_TRACE(COMP_INIT, DBG_LOUD,
				("ODM_PathDiversityBeforeLink92C(): DefaultRespPath=%d\n", pDM_PDTable->DefaultRespPath));

			//pDM_SWAT_Table->PreAntenna = pDM_SWAT_Table->CurAntenna;
		}
		else
		{
			RT_TRACE(COMP_INIT, DBG_LOUD, 
				("ODM_PathDiversityBeforeLink92C(): DefaultRespPath=%d\n", pDM_PDTable->DefaultRespPath));

			if(pDM_PDTable->DefaultRespPath == 0)
			{
				pDM_PDTable->OFDMTXPath = 0xFFFFFFFF;
				pDM_PDTable->CCKTXPath = 0xFFFFFFFF;
				odm_SetRespPath_92C(Adapter, 1);
			}
			else
			{
				pDM_PDTable->OFDMTXPath = 0x0;
				pDM_PDTable->CCKTXPath = 0x0;
				odm_SetRespPath_92C(Adapter, 0);
			}
			PHY_SetBBReg(Adapter, rCCK0_AFESetting  , 0x0F000000, 0x01); // RX path = PathAB

			//pDM_SWAT_Table->CurAntenna = pDM_SWAT_Table->PreAntenna;

			//PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, 0x300, DM_SWAT_Table.CurAntenna);
			//pDM_SWAT_Table->SWAS_NoLink_BK_Reg860 = ((pDM_SWAT_Table->SWAS_NoLink_BK_Reg860 & 0xfffffcff) | (pDM_SWAT_Table->CurAntenna<<8));
			//PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, bMaskDWord, pDM_SWAT_Table->SWAS_NoLink_BK_Reg860);
		}

		// Check state reset to default and wait for next time.
		//pDM_SWAT_Table->SWAS_NoLink_State = 0;
		pDM_PDTable->PathDiv_NoLink_State = 0;

		return FALSE;
	}
#else
		return	FALSE;
#endif
	
}



VOID
odm_PathDiversityAfterLink_92C(
	IN	PADAPTER	Adapter
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	pPD_T		pDM_PDTable = &Adapter->DM_PDTable;
	u1Byte		DefaultRespPath=0;

	if((!(pHalData->CVID_Version==VERSION_1_BEFORE_8703B && IS_92C_SERIAL(pHalData->VersionID))) || (pHalData->PathDivCfg != 1) || (pHalData->eRFPowerState == eRfOff))
	{
		if(pHalData->PathDivCfg == 0)
		{
			RT_TRACE(	COMP_INIT, DBG_LOUD, ("No ODM_TXPathDiversity()\n"));
		}
		else
		{
			RT_TRACE(	COMP_INIT, DBG_LOUD, ("2T ODM_TXPathDiversity()\n"));
		}
		return;
	}
	if(!odm_IsConnected_92C(Adapter))
	{
		RT_TRACE(	COMP_INIT, DBG_LOUD, ("ODM_TXPathDiversity(): No Connections\n"));
		return;
	}
	
	
	if(pDM_PDTable->TrainingState == 0)
	{
		RT_TRACE(	COMP_INIT, DBG_LOUD, ("ODM_TXPathDiversity() ==>\n"));
		odm_OFDMTXPathDiversity_92C(Adapter);

		if((pDM_PDTable->CCKPathDivEnable == TRUE) && (pDM_PDTable->OFDM_Pkt_Cnt < 100))
		{
			//RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_CCKTXPathDiversity_92C: TrainingState=0\n"));
			
			if(pDM_PDTable->CCK_Pkt_Cnt > 300)
				pDM_PDTable->Timer = 20;
			else if(pDM_PDTable->CCK_Pkt_Cnt > 100)
				pDM_PDTable->Timer = 60;
			else
				pDM_PDTable->Timer = 250;
			RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_CCKTXPathDiversity_92C: timer=%d\n",pDM_PDTable->Timer));

			PHY_SetBBReg(Adapter, rCCK0_AFESetting  , 0x0F000000, 0x00); // RX path = PathA
			pDM_PDTable->TrainingState = 1;
			ODM_SetTimer( pDM_Odm, &pDM_Odm->CCKPathDiversityTimer, pDM_PDTable->Timer); //ms
		}
		else
		{
			pDM_PDTable->CCKTXPath = pDM_PDTable->OFDMTXPath;
			DefaultRespPath = pDM_PDTable->OFDMDefaultRespPath;
			RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_SetRespPath_92C: Skip odm_CCKTXPathDiversity_92C, DefaultRespPath is OFDM\n"));
			odm_SetRespPath_92C(Adapter, DefaultRespPath);
			odm_ResetPathDiversity_92C(Adapter);
			RT_TRACE(	COMP_INIT, DBG_LOUD, ("ODM_TXPathDiversity() <==\n"));
		}
	}
	else if(pDM_PDTable->TrainingState == 1)
	{		
		//RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_CCKTXPathDiversity_92C: TrainingState=1\n"));
		PHY_SetBBReg(Adapter, rCCK0_AFESetting  , 0x0F000000, 0x05); // RX path = PathB
		pDM_PDTable->TrainingState = 2;
		ODM_SetTimer( pDM_Odm, &pDM_Odm->CCKPathDiversityTimer, pDM_PDTable->Timer); //ms
	}
	else
	{
		//RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_CCKTXPathDiversity_92C: TrainingState=2\n"));
		pDM_PDTable->TrainingState = 0;	
		odm_CCKTXPathDiversity_92C(Adapter); 
		if(pDM_PDTable->OFDM_Pkt_Cnt != 0)
		{
			DefaultRespPath = pDM_PDTable->OFDMDefaultRespPath;
			RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_SetRespPath_92C: DefaultRespPath is OFDM\n"));
		}
		else
		{
			DefaultRespPath = pDM_PDTable->CCKDefaultRespPath;
			RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_SetRespPath_92C: DefaultRespPath is CCK\n"));
		}
		odm_SetRespPath_92C(Adapter, DefaultRespPath);
		odm_ResetPathDiversity_92C(Adapter);
		RT_TRACE(	COMP_INIT, DBG_LOUD, ("ODM_TXPathDiversity() <==\n"));
	}

}

VOID
odm_SetRespPath_92C(
	IN	PADAPTER	Adapter,
	IN	u1Byte	DefaultRespPath
	)
{
	pPD_T	pDM_PDTable = &Adapter->DM_PDTable;

	RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_SetRespPath_92C: Select Response Path=%d\n",DefaultRespPath));
	if(DefaultRespPath != pDM_PDTable->DefaultRespPath)
	{
		if(DefaultRespPath == 0)
		{
			PlatformEFIOWrite1Byte(Adapter, 0x6D8, (PlatformEFIORead1Byte(Adapter, 0x6D8)&0xc0)|0x15);	
		}
		else
		{
			PlatformEFIOWrite1Byte(Adapter, 0x6D8, (PlatformEFIORead1Byte(Adapter, 0x6D8)&0xc0)|0x2A);
		}	
	}
	pDM_PDTable->DefaultRespPath = DefaultRespPath;
}

VOID
odm_OFDMTXPathDiversity_92C(
	IN	PADAPTER	Adapter)
{
//	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);
	PRT_WLAN_STA	pEntry;
	u1Byte	i, DefaultRespPath = 0;
	s4Byte	MinRSSI = 0xFF;
	pPD_T	pDM_PDTable = &Adapter->DM_PDTable;
	pDM_PDTable->OFDMTXPath = 0;
	
	//1 Default Port
	if(pMgntInfo->mAssoc)
	{
		RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_OFDMTXPathDiversity_92C: Default port RSSI[0]=%d, RSSI[1]=%d\n",
			Adapter->RxStats.RxRSSIPercentage[0], Adapter->RxStats.RxRSSIPercentage[1]));
		if(Adapter->RxStats.RxRSSIPercentage[0] > Adapter->RxStats.RxRSSIPercentage[1])
		{
			pDM_PDTable->OFDMTXPath = pDM_PDTable->OFDMTXPath & (~BIT0);
			MinRSSI =  Adapter->RxStats.RxRSSIPercentage[1];
			DefaultRespPath = 0;
			RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_OFDMTXPathDiversity_92C: Default port Select Path-0\n"));
		}
		else
		{
			pDM_PDTable->OFDMTXPath =  pDM_PDTable->OFDMTXPath | BIT0;
			MinRSSI =  Adapter->RxStats.RxRSSIPercentage[0];
			DefaultRespPath = 1;
			RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_OFDMTXPathDiversity_92C: Default port Select Path-1\n"));
		}
			//RT_TRACE(	COMP_INIT, DBG_LOUD, ("pDM_PDTable->OFDMTXPath =0x%x\n",pDM_PDTable->OFDMTXPath));
	}
	//1 Extension Port
	for(i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
		if(IsAPModeExist(Adapter)  && GetFirstExtAdapter(Adapter) != NULL)
			pEntry = AsocEntry_EnumStation(GetFirstExtAdapter(Adapter), i);
		else
			pEntry = AsocEntry_EnumStation(GetDefaultAdapter(Adapter), i);

		if(pEntry!=NULL)
		{
			if(pEntry->bAssociated)
			{
				RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_OFDMTXPathDiversity_92C: MACID=%d, RSSI_0=%d, RSSI_1=%d\n", 
					pEntry->AssociatedMacId, pEntry->rssi_stat.RxRSSIPercentage[0], pEntry->rssi_stat.RxRSSIPercentage[1]));
				
				if(pEntry->rssi_stat.RxRSSIPercentage[0] > pEntry->rssi_stat.RxRSSIPercentage[1])
				{
					pDM_PDTable->OFDMTXPath = pDM_PDTable->OFDMTXPath & ~(BIT(pEntry->AssociatedMacId));
					//pHalData->TXPath = pHalData->TXPath & ~(1<<(pEntry->AssociatedMacId));
					RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_OFDMTXPathDiversity_92C: MACID=%d Select Path-0\n", pEntry->AssociatedMacId));
					if(pEntry->rssi_stat.RxRSSIPercentage[1] < MinRSSI)
					{
						MinRSSI = pEntry->rssi_stat.RxRSSIPercentage[1];
						DefaultRespPath = 0;
					}
				}
				else
				{
					pDM_PDTable->OFDMTXPath = pDM_PDTable->OFDMTXPath | BIT(pEntry->AssociatedMacId);
					//pHalData->TXPath = pHalData->TXPath | (1 << (pEntry->AssociatedMacId));
					RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_OFDMTXPathDiversity_92C: MACID=%d Select Path-1\n", pEntry->AssociatedMacId));
					if(pEntry->rssi_stat.RxRSSIPercentage[0] < MinRSSI)
					{
						MinRSSI = pEntry->rssi_stat.RxRSSIPercentage[0];
						DefaultRespPath = 1;
					}
				}
			}
		}
		else
		{
			break;
		}
	}

	pDM_PDTable->OFDMDefaultRespPath = DefaultRespPath;
}


VOID
odm_CCKTXPathDiversity_92C(
	IN	PADAPTER	Adapter
)
{
	HAL_DATA_TYPE		*pHalData = GET_HAL_DATA(Adapter);
	PMGNT_INFO		pMgntInfo = &(Adapter->MgntInfo);
	PRT_WLAN_STA	pEntry;
	s4Byte	MinRSSI = 0xFF;
	u1Byte	i, DefaultRespPath = 0;
//	BOOLEAN	bBModePathDiv = FALSE;
	pPD_T	pDM_PDTable = &Adapter->DM_PDTable;

	//1 Default Port
	if(pMgntInfo->mAssoc)
	{
		if(pHalData->OFDM_Pkt_Cnt == 0)
		{
			for(i=0; i<2; i++)
			{
				if(pDM_PDTable->RSSI_CCK_Path_cnt[i] > 1) //Because the first packet is discarded
					pDM_PDTable->RSSI_CCK_Path[i] = pDM_PDTable->RSSI_CCK_Path[i] / (pDM_PDTable->RSSI_CCK_Path_cnt[i]-1);
				else
					pDM_PDTable->RSSI_CCK_Path[i] = 0;
			}
			RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_CCKTXPathDiversity_92C: pDM_PDTable->RSSI_CCK_Path[0]=%d, pDM_PDTable->RSSI_CCK_Path[1]=%d\n",
				pDM_PDTable->RSSI_CCK_Path[0], pDM_PDTable->RSSI_CCK_Path[1]));
			RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_CCKTXPathDiversity_92C: pDM_PDTable->RSSI_CCK_Path_cnt[0]=%d, pDM_PDTable->RSSI_CCK_Path_cnt[1]=%d\n",
				pDM_PDTable->RSSI_CCK_Path_cnt[0], pDM_PDTable->RSSI_CCK_Path_cnt[1]));
		
			if(pDM_PDTable->RSSI_CCK_Path[0] > pDM_PDTable->RSSI_CCK_Path[1])
			{
				pDM_PDTable->CCKTXPath = pDM_PDTable->CCKTXPath & (~BIT0);
				MinRSSI =  pDM_PDTable->RSSI_CCK_Path[1];
				DefaultRespPath = 0;
				RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_CCKTXPathDiversity_92C: Default port Select CCK Path-0\n"));
			}
			else if(pDM_PDTable->RSSI_CCK_Path[0] < pDM_PDTable->RSSI_CCK_Path[1])
			{
				pDM_PDTable->CCKTXPath =  pDM_PDTable->CCKTXPath | BIT0;
				MinRSSI =  pDM_PDTable->RSSI_CCK_Path[0];
				DefaultRespPath = 1;
				RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_CCKTXPathDiversity_92C: Default port Select CCK Path-1\n"));
			}
			else
			{
				if((pDM_PDTable->RSSI_CCK_Path[0] != 0) && (pDM_PDTable->RSSI_CCK_Path[0] < MinRSSI))
				{
					pDM_PDTable->CCKTXPath = pDM_PDTable->CCKTXPath & (~BIT0);
					RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_CCKTXPathDiversity_92C: Default port Select CCK Path-0\n"));
					MinRSSI =  pDM_PDTable->RSSI_CCK_Path[1];
					DefaultRespPath = 0;
				}
				else
				{
					RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_CCKTXPathDiversity_92C: Default port unchange CCK Path\n"));
				}
			}
		}
		else //Follow OFDM decision
		{
			pDM_PDTable->CCKTXPath = (pDM_PDTable->CCKTXPath & (~BIT0)) | (pDM_PDTable->OFDMTXPath &BIT0);
			RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_CCKTXPathDiversity_92C: Follow OFDM decision, Default port Select CCK Path-%d\n",
				pDM_PDTable->CCKTXPath &BIT0));
		}
	}
	//1 Extension Port
	for(i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
		if(IsAPModeExist(Adapter)  && GetFirstExtAdapter(Adapter) != NULL)
			pEntry = AsocEntry_EnumStation(GetFirstExtAdapter(Adapter), i);
		else
			pEntry = AsocEntry_EnumStation(GetDefaultAdapter(Adapter), i);

		if(pEntry!=NULL)
		{
			if(pEntry->bAssociated)
			{
				if(pEntry->rssi_stat.OFDM_Pkt_Cnt == 0)
				{
					u1Byte j=0;
					for(j=0; j<2; j++)
					{
						if(pEntry->rssi_stat.RSSI_CCK_Path_cnt[j] > 1)
							pEntry->rssi_stat.RSSI_CCK_Path[j] = pEntry->rssi_stat.RSSI_CCK_Path[j] / (pEntry->rssi_stat.RSSI_CCK_Path_cnt[j]-1);
						else
							pEntry->rssi_stat.RSSI_CCK_Path[j] = 0;
					}
					RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_CCKTXPathDiversity_92C: MACID=%d, RSSI_CCK0=%d, RSSI_CCK1=%d\n", 
						pEntry->AssociatedMacId, pEntry->rssi_stat.RSSI_CCK_Path[0], pEntry->rssi_stat.RSSI_CCK_Path[1]));
					
					if(pEntry->rssi_stat.RSSI_CCK_Path[0] >pEntry->rssi_stat.RSSI_CCK_Path[1])
					{
						pDM_PDTable->CCKTXPath = pDM_PDTable->CCKTXPath & ~(BIT(pEntry->AssociatedMacId));
						RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_CCKTXPathDiversity_92C: MACID=%d Select CCK Path-0\n", pEntry->AssociatedMacId));
						if(pEntry->rssi_stat.RSSI_CCK_Path[1] < MinRSSI)
						{
							MinRSSI = pEntry->rssi_stat.RSSI_CCK_Path[1];
							DefaultRespPath = 0;
						}
					}
					else if(pEntry->rssi_stat.RSSI_CCK_Path[0] <pEntry->rssi_stat.RSSI_CCK_Path[1])
					{
						pDM_PDTable->CCKTXPath = pDM_PDTable->CCKTXPath | BIT(pEntry->AssociatedMacId);
						RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_CCKTXPathDiversity_92C: MACID=%d Select CCK Path-1\n", pEntry->AssociatedMacId));
						if(pEntry->rssi_stat.RSSI_CCK_Path[0] < MinRSSI)
						{
							MinRSSI = pEntry->rssi_stat.RSSI_CCK_Path[0];
							DefaultRespPath = 1;
						}
					}
					else
					{
						if((pEntry->rssi_stat.RSSI_CCK_Path[0] != 0) && (pEntry->rssi_stat.RSSI_CCK_Path[0] < MinRSSI))
						{
							pDM_PDTable->CCKTXPath = pDM_PDTable->CCKTXPath & ~(BIT(pEntry->AssociatedMacId));
							MinRSSI = pEntry->rssi_stat.RSSI_CCK_Path[1];
							DefaultRespPath = 0;
							RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_CCKTXPathDiversity_92C: MACID=%d Select CCK Path-0\n", pEntry->AssociatedMacId));
						}
						else
						{
							RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_CCKTXPathDiversity_92C: MACID=%d unchange CCK Path\n", pEntry->AssociatedMacId));
						}
					}
				}
				else //Follow OFDM decision
				{
					pDM_PDTable->CCKTXPath = (pDM_PDTable->CCKTXPath & (~(BIT(pEntry->AssociatedMacId)))) | (pDM_PDTable->OFDMTXPath & BIT(pEntry->AssociatedMacId));
					RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_CCKTXPathDiversity_92C: Follow OFDM decision, MACID=%d Select CCK Path-%d\n",
						pEntry->AssociatedMacId, (pDM_PDTable->CCKTXPath & BIT(pEntry->AssociatedMacId))>>(pEntry->AssociatedMacId)));
				}
			}
		}
		else
		{
			break;
		}
	}

	RT_TRACE(	COMP_INIT, DBG_LOUD, ("odm_CCKTXPathDiversity_92C:MinRSSI=%d\n",MinRSSI));

	if(MinRSSI == 0xFF)
		DefaultRespPath = pDM_PDTable->CCKDefaultRespPath;

	pDM_PDTable->CCKDefaultRespPath = DefaultRespPath;
}


VOID
odm_ResetPathDiversity_92C(
		IN	PADAPTER	Adapter
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	pPD_T	pDM_PDTable = &Adapter->DM_PDTable;
	PRT_WLAN_STA	pEntry;
	u4Byte	i,j;

	pDM_PDTable->CCK_Pkt_Cnt = 0;
	pDM_PDTable->OFDM_Pkt_Cnt = 0;
	pHalData->CCK_Pkt_Cnt =0;
	pHalData->OFDM_Pkt_Cnt =0;
	
	if(pDM_PDTable->CCKPathDivEnable == TRUE)	
		PHY_SetBBReg(Adapter, rCCK0_AFESetting  , 0x0F000000, 0x01); //RX path = PathAB

	for(i=0; i<2; i++)
	{
		pDM_PDTable->RSSI_CCK_Path_cnt[i]=0;
		pDM_PDTable->RSSI_CCK_Path[i] = 0;
	}
	for(i = 0; i < ODM_ASSOCIATE_ENTRY_NUM; i++)
	{
		if(IsAPModeExist(Adapter)  && GetFirstExtAdapter(Adapter) != NULL)
			pEntry = AsocEntry_EnumStation(GetFirstExtAdapter(Adapter), i);
		else
			pEntry = AsocEntry_EnumStation(GetDefaultAdapter(Adapter), i);

		if(pEntry!=NULL)
		{
			pEntry->rssi_stat.CCK_Pkt_Cnt = 0;
			pEntry->rssi_stat.OFDM_Pkt_Cnt = 0;
			for(j=0; j<2; j++)
			{
				pEntry->rssi_stat.RSSI_CCK_Path_cnt[j] = 0;
				pEntry->rssi_stat.RSSI_CCK_Path[j] = 0;
			}
		}
		else
			break;
	}
}





VOID
odm_CCKTXPathDiversityCallback(
	PRT_TIMER		pTimer
)
{
#if USE_WORKITEM
       PADAPTER	Adapter = (PADAPTER)pTimer->Adapter;
       HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	   PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
#else
	PADAPTER	Adapter = (PADAPTER)pTimer->Adapter;
#endif

#if DEV_BUS_TYPE==RT_PCI_INTERFACE
#if USE_WORKITEM
	PlatformScheduleWorkItem(&pDM_Odm->CCKPathDiversityWorkitem);
#else
	odm_PathDiversityAfterLink_92C(Adapter);
#endif
#else
	PlatformScheduleWorkItem(&pDM_Odm->CCKPathDiversityWorkitem);
#endif

}


VOID
odm_CCKTXPathDiversityWorkItemCallback(
    IN PVOID            pContext
    )
{
	PADAPTER	Adapter = (PADAPTER)pContext;

	odm_CCKTXPathDiversity_92C(Adapter);
}

//
// 20100514 Luke/Joseph:
// Callback function for 500ms antenna test trying.
//
VOID
odm_PathDivChkAntSwitchCallback(
	PRT_TIMER		pTimer
)
{
	PADAPTER		Adapter = (PADAPTER)pTimer->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;

#if DEV_BUS_TYPE==RT_PCI_INTERFACE

#if USE_WORKITEM
	PlatformScheduleWorkItem(&pDM_Odm->PathDivSwitchWorkitem);
#else
	odm_PathDivChkAntSwitch(pDM_Odm);
#endif
#else
	PlatformScheduleWorkItem(&pDM_Odm->PathDivSwitchWorkitem);
#endif

//odm_SwAntDivChkAntSwitch(Adapter, SWAW_STEP_DETERMINE);

}


VOID
odm_PathDivChkAntSwitchWorkitemCallback(
    IN PVOID            pContext
    )
{
	PADAPTER	pAdapter = (PADAPTER)pContext;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(pAdapter);
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;

	odm_PathDivChkAntSwitch(pDM_Odm);
}


 //MAC0_ACCESS_PHY1

// 2011-06-22 Neil Chen & Gary Hsin
// Refer to Jr.Luke's SW ANT DIV
// 92D Path Diversity Main function
// refer to 88C software antenna diversity
// 
VOID
odm_PathDivChkAntSwitch(
	PDM_ODM_T		pDM_Odm
	//PADAPTER		Adapter,
	//u1Byte			Step
)
{
	PADAPTER		Adapter = pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	PMGNT_INFO		pMgntInfo = &Adapter->MgntInfo;


	pSWAT_T			pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;
	s4Byte			curRSSI=100, RSSI_A, RSSI_B;
	u1Byte			nextAntenna=AUX_ANT;
	static u8Byte		lastTxOkCnt=0, lastRxOkCnt=0;
	u8Byte			curTxOkCnt, curRxOkCnt;
	static u8Byte		TXByteCnt_A=0, TXByteCnt_B=0, RXByteCnt_A=0, RXByteCnt_B=0;
	u8Byte			CurByteCnt=0, PreByteCnt=0;
	static u1Byte		TrafficLoad = TRAFFIC_LOW;
	u1Byte			Score_A=0, Score_B=0;
	u1Byte			i=0x0;
       // Neil Chen
       static u1Byte        pathdiv_para=0x0;     
       static u1Byte        switchfirsttime=0x00;
	// u1Byte                 regB33 = (u1Byte) PHY_QueryBBReg(Adapter, 0xB30,BIT27);
	u1Byte			regB33 = (u1Byte)ODM_GetBBReg(pDM_Odm, PATHDIV_REG, BIT27);


       //u1Byte                 reg637 =0x0;   
       static u1Byte        fw_value=0x0;         
	//u8Byte			curTxOkCnt_tmp, curRxOkCnt_tmp;
       PADAPTER            BuddyAdapter = Adapter->BuddyAdapter;     // another adapter MAC
        // Path Diversity   //Neil Chen--2011--06--22

	//u1Byte                 PathDiv_Trigger = (u1Byte) PHY_QueryBBReg(Adapter, 0xBA0,BIT31);
	u1Byte                 PathDiv_Trigger = (u1Byte) ODM_GetBBReg(pDM_Odm, PATHDIV_TRI,BIT31);
	u1Byte                 PathDiv_Enable = pHalData->bPathDiv_Enable;


	//DbgPrint("Path Div PG Value:%x \n",PathDiv_Enable);	
       if((BuddyAdapter==NULL)||(!PathDiv_Enable)||(PathDiv_Trigger)||(pHalData->CurrentBandType == BAND_ON_2_4G))
       {
           return;
       }
	ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD,("===================>odm_PathDivChkAntSwitch()\n"));

       // The first time to switch path excluding 2nd, 3rd, ....etc....
	if(switchfirsttime==0)
	{
	    if(regB33==0)
	    {
	       pDM_SWAT_Table->CurAntenna = MAIN_ANT;    // Default MAC0_5G-->Path A (current antenna)     
	    }	    
	}

	// Condition that does not need to use antenna diversity.
	if(pDM_Odm->SupportICType != ODM_RTL8192D)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("odm_PathDiversityMechanims(): No PathDiv Mechanism.\n"));
		return;
	}

	// Radio off: Status reset to default and return.
	if(pHalData->eRFPowerState==eRfOff)
	{
		//ODM_SwAntDivRestAfterLink(Adapter);
		return;
	}

       /*
	// Handling step mismatch condition.
	// Peak step is not finished at last time. Recover the variable and check again.
	if(	Step != pDM_SWAT_Table->try_flag	)
	{
		ODM_SwAntDivRestAfterLink(Adapter);
	} */
	
	if(pDM_SWAT_Table->try_flag == 0xff)
	{
		// Select RSSI checking target
		if(pMgntInfo->mAssoc && !ACTING_AS_AP(Adapter))
		{
			// Target: Infrastructure mode AP.
			pHalData->RSSI_target = NULL;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("odm_PathDivMechanism(): RSSI_target is DEF AP!\n"));
		}
		else
		{
			u1Byte			index = 0;
			PRT_WLAN_STA	pEntry = NULL;
			PADAPTER		pTargetAdapter = NULL;
		
			if(	pMgntInfo->mIbss || ACTING_AS_AP(Adapter) )
			{
				// Target: AP/IBSS peer.
				pTargetAdapter = Adapter;
			}
			else if(IsAPModeExist(Adapter)  && GetFirstExtAdapter(Adapter) != NULL)
			{
				// Target: VWIFI peer.
				pTargetAdapter = GetFirstExtAdapter(Adapter);
			}

			if(pTargetAdapter != NULL)
			{
				for(index=0; index<ODM_ASSOCIATE_ENTRY_NUM; index++)
				{
					pEntry = AsocEntry_EnumStation(pTargetAdapter, index);
					if(pEntry != NULL)
					{
						if(pEntry->bAssociated)
							break;			
					}
				}
			}

			if(pEntry == NULL)
			{
				ODM_PathDivRestAfterLink(pDM_Odm);
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("odm_SwAntDivChkAntSwitch(): No Link.\n"));
				return;
			}
			else
			{
				pHalData->RSSI_target = pEntry;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("odm_SwAntDivChkAntSwitch(): RSSI_target is PEER STA\n"));
			}
		}
			
		pHalData->RSSI_cnt_A = 0;
		pHalData->RSSI_cnt_B = 0;
		pDM_SWAT_Table->try_flag = 0;
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("odm_SwAntDivChkAntSwitch(): Set try_flag to 0 prepare for peak!\n"));
		return;
	}
	else
	{
	       // 1st step
		curTxOkCnt = Adapter->TxStats.NumTxBytesUnicast - lastTxOkCnt;
		curRxOkCnt = Adapter->RxStats.NumRxBytesUnicast - lastRxOkCnt;
		lastTxOkCnt = Adapter->TxStats.NumTxBytesUnicast;
		lastRxOkCnt = Adapter->RxStats.NumRxBytesUnicast;
	
		if(pDM_SWAT_Table->try_flag == 1)   // Training State
		{
			if(pDM_SWAT_Table->CurAntenna == MAIN_ANT)
			{
				TXByteCnt_A += curTxOkCnt;
				RXByteCnt_A += curRxOkCnt;
			}
			else
			{
				TXByteCnt_B += curTxOkCnt;
				RXByteCnt_B += curRxOkCnt;
			}
		
			nextAntenna = (pDM_SWAT_Table->CurAntenna == MAIN_ANT)? AUX_ANT : MAIN_ANT;
			pDM_SWAT_Table->RSSI_Trying--;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH DIV=: RSSI_Trying = %d\n",pDM_SWAT_Table->RSSI_Trying));
			if(pDM_SWAT_Table->RSSI_Trying == 0)
			{
				CurByteCnt = (pDM_SWAT_Table->CurAntenna == MAIN_ANT)? (TXByteCnt_A+RXByteCnt_A) : (TXByteCnt_B+RXByteCnt_B);
				PreByteCnt = (pDM_SWAT_Table->CurAntenna == MAIN_ANT)? (TXByteCnt_B+RXByteCnt_B) : (TXByteCnt_A+RXByteCnt_A);
				
				if(TrafficLoad == TRAFFIC_HIGH)
				{
					//CurByteCnt = PlatformDivision64(CurByteCnt, 9);
					PreByteCnt =PreByteCnt*9;
				}
				else if(TrafficLoad == TRAFFIC_LOW)
				{
					//CurByteCnt = PlatformDivision64(CurByteCnt, 2);
					PreByteCnt =PreByteCnt*2;
				}
				if(pHalData->RSSI_cnt_A > 0)
					RSSI_A = pHalData->RSSI_sum_A/pHalData->RSSI_cnt_A; 
				else
					RSSI_A = 0;
				if(pHalData->RSSI_cnt_B > 0)
					RSSI_B = pHalData->RSSI_sum_B/pHalData->RSSI_cnt_B; 
		             else
					RSSI_B = 0;
				curRSSI = (pDM_SWAT_Table->CurAntenna == MAIN_ANT)? RSSI_A : RSSI_B;
				pDM_SWAT_Table->PreRSSI =  (pDM_SWAT_Table->CurAntenna == MAIN_ANT)? RSSI_B : RSSI_A;
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH DIV=: PreRSSI = %d, CurRSSI = %d\n",pDM_SWAT_Table->PreRSSI, curRSSI));
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH DIV=: preAntenna= %s, curAntenna= %s \n", 
				(pDM_SWAT_Table->PreAntenna == MAIN_ANT?"MAIN":"AUX"), (pDM_SWAT_Table->CurAntenna == MAIN_ANT?"MAIN":"AUX")));
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH DIV=: RSSI_A= %d, RSSI_cnt_A = %d, RSSI_B= %d, RSSI_cnt_B = %d\n",
					RSSI_A, pHalData->RSSI_cnt_A, RSSI_B, pHalData->RSSI_cnt_B));
			}

		}
		else   // try_flag=0
		{
		
			if(pHalData->RSSI_cnt_A > 0)
				RSSI_A = pHalData->RSSI_sum_A/pHalData->RSSI_cnt_A; 
			else
				RSSI_A = 0;
			if(pHalData->RSSI_cnt_B > 0)
				RSSI_B = pHalData->RSSI_sum_B/pHalData->RSSI_cnt_B; 
			else
				RSSI_B = 0;	
			curRSSI = (pDM_SWAT_Table->CurAntenna == MAIN_ANT)? RSSI_A : RSSI_B;
			pDM_SWAT_Table->PreRSSI =  (pDM_SWAT_Table->PreAntenna == MAIN_ANT)? RSSI_A : RSSI_B;
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH DIV=: PreRSSI = %d, CurRSSI = %d\n", pDM_SWAT_Table->PreRSSI, curRSSI));
		       ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH DIV=: preAntenna= %s, curAntenna= %s \n", 
			(pDM_SWAT_Table->PreAntenna == MAIN_ANT?"MAIN":"AUX"), (pDM_SWAT_Table->CurAntenna == MAIN_ANT?"MAIN":"AUX")));

			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH DIV=: RSSI_A= %d, RSSI_cnt_A = %d, RSSI_B= %d, RSSI_cnt_B = %d\n",
				RSSI_A, pHalData->RSSI_cnt_A, RSSI_B, pHalData->RSSI_cnt_B));
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("Ekul:curTxOkCnt = %d\n", curTxOkCnt));
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("Ekul:curRxOkCnt = %d\n", curRxOkCnt));
		}

		//1 Trying State
		if((pDM_SWAT_Table->try_flag == 1)&&(pDM_SWAT_Table->RSSI_Trying == 0))
		{

			if(pDM_SWAT_Table->TestMode == TP_MODE)
			{
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH=: TestMode = TP_MODE"));
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH= TRY:CurByteCnt = %"i64fmt"d,", CurByteCnt));
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH= TRY:PreByteCnt = %"i64fmt"d\n",PreByteCnt));		
				if(CurByteCnt < PreByteCnt)
				{
					if(pDM_SWAT_Table->CurAntenna == MAIN_ANT)
						pDM_SWAT_Table->SelectAntennaMap=pDM_SWAT_Table->SelectAntennaMap<<1;
					else
						pDM_SWAT_Table->SelectAntennaMap=(pDM_SWAT_Table->SelectAntennaMap<<1)+1;
				}
				else
				{
					if(pDM_SWAT_Table->CurAntenna == MAIN_ANT)
						pDM_SWAT_Table->SelectAntennaMap=(pDM_SWAT_Table->SelectAntennaMap<<1)+1;
					else
						pDM_SWAT_Table->SelectAntennaMap=pDM_SWAT_Table->SelectAntennaMap<<1;
				}
				for (i= 0; i<8; i++)
				{
					if(((pDM_SWAT_Table->SelectAntennaMap>>i)&BIT0) == 1)
						Score_A++;
					else
						Score_B++;
				}
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("SelectAntennaMap=%x\n ",pDM_SWAT_Table->SelectAntennaMap));
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH=: Score_A=%d, Score_B=%d\n", Score_A, Score_B));
			
				if(pDM_SWAT_Table->CurAntenna == MAIN_ANT)
				{
					nextAntenna = (Score_A >= Score_B)?MAIN_ANT:AUX_ANT;
				}
				else
				{
					nextAntenna = (Score_B >= Score_A)?AUX_ANT:MAIN_ANT;
				}
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH=: nextAntenna=%s\n",(nextAntenna==MAIN_ANT)?"MAIN":"AUX"));
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH=: preAntenna= %s, curAntenna= %s \n", 
				(pDM_SWAT_Table->PreAntenna == MAIN_ANT?"MAIN":"AUX"), (pDM_SWAT_Table->CurAntenna == MAIN_ANT?"MAIN":"AUX")));

				if(nextAntenna != pDM_SWAT_Table->CurAntenna)
				{
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH=: Switch back to another antenna"));
				}
				else
				{
					ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH=: current anntena is good\n"));
				}	
			}

                    
			if(pDM_SWAT_Table->TestMode == RSSI_MODE)
			{	
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH=: TestMode = RSSI_MODE"));
				pDM_SWAT_Table->SelectAntennaMap=0xAA;
				if(curRSSI < pDM_SWAT_Table->PreRSSI) //Current antenna is worse than previous antenna
				{
					//RT_TRACE(COMP_INIT, DBG_LOUD, ("SWAS: Switch back to another antenna"));
					nextAntenna = (pDM_SWAT_Table->CurAntenna == MAIN_ANT)?AUX_ANT : MAIN_ANT;
				}
				else // current anntena is good
				{
					nextAntenna =pDM_SWAT_Table->CurAntenna;
					//RT_TRACE(COMP_INIT, DBG_LOUD, ("SWAS: current anntena is good\n"));
				}
			}
			
			pDM_SWAT_Table->try_flag = 0;
			pHalData->RSSI_sum_A = 0;
			pHalData->RSSI_cnt_A = 0;
			pHalData->RSSI_sum_B = 0;
			pHalData->RSSI_cnt_B = 0;
			TXByteCnt_A = 0;
			TXByteCnt_B = 0;
			RXByteCnt_A = 0;
			RXByteCnt_B = 0;
			
		}

		//1 Normal State
		else if(pDM_SWAT_Table->try_flag == 0)
		{
			if(TrafficLoad == TRAFFIC_HIGH)
			{
				if ((curTxOkCnt+curRxOkCnt) > 3750000)//if(PlatformDivision64(curTxOkCnt+curRxOkCnt, 2) > 1875000)
					TrafficLoad = TRAFFIC_HIGH;
				else
					TrafficLoad = TRAFFIC_LOW;
			}
			else if(TrafficLoad == TRAFFIC_LOW)
				{
				if ((curTxOkCnt+curRxOkCnt) > 3750000)//if(PlatformDivision64(curTxOkCnt+curRxOkCnt, 2) > 1875000)
					TrafficLoad = TRAFFIC_HIGH;
				else
					TrafficLoad = TRAFFIC_LOW;
			}
			if(TrafficLoad == TRAFFIC_HIGH)
				pDM_SWAT_Table->bTriggerAntennaSwitch = 0;
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("Normal:TrafficLoad = %llu\n", curTxOkCnt+curRxOkCnt));

			//Prepare To Try Antenna		
				nextAntenna = (pDM_SWAT_Table->CurAntenna == MAIN_ANT)? AUX_ANT : MAIN_ANT;
				pDM_SWAT_Table->try_flag = 1;
			if((curRxOkCnt+curTxOkCnt) > 1000)
			{
#if DEV_BUS_TYPE==RT_PCI_INTERFACE
	                    pDM_SWAT_Table->RSSI_Trying = 4;                           
#else
	                    pDM_SWAT_Table->RSSI_Trying = 2;
#endif
				pDM_SWAT_Table->TestMode = TP_MODE;
			}
			else
			{
				pDM_SWAT_Table->RSSI_Trying = 2;
				pDM_SWAT_Table->TestMode = RSSI_MODE;

			}
                          
			//RT_TRACE(COMP_INIT, DBG_LOUD, ("SWAS: Normal State -> Begin Trying!\n"));			
			pHalData->RSSI_sum_A = 0;
			pHalData->RSSI_cnt_A = 0;
			pHalData->RSSI_sum_B = 0;
			pHalData->RSSI_cnt_B = 0;
		} // end of try_flag=0
	}
	
	//1 4.Change TRX antenna
	if(nextAntenna != pDM_SWAT_Table->CurAntenna)
	{
	
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH=: Change TX Antenna!\n "));
		//PHY_SetBBReg(Adapter, rFPGA0_XA_RFInterfaceOE, 0x300, nextAntenna); for 88C
		if(nextAntenna==MAIN_ANT)
		{
		    ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH=: Next Antenna is RF PATH A\n "));
		    pathdiv_para = 0x02;   //02 to switchback to RF path A
		    fw_value = 0x03;
#if DEV_BUS_TYPE==RT_PCI_INTERFACE
                 odm_PathDiversity_8192D(pDM_Odm, pathdiv_para);
#else
                 ODM_FillH2CCmd(pDM_Odm, ODM_H2C_PathDiv,1,(pu1Byte)(&fw_value));	
#endif
		}	
	       else if(nextAntenna==AUX_ANT)
	       {
	           ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH=: Next Antenna is RF PATH B\n "));
	           if(switchfirsttime==0)  // First Time To Enter Path Diversity
	           {
	               switchfirsttime=0x01;
                      pathdiv_para = 0x00;
			  fw_value=0x00;    // to backup RF Path A Releated Registers		  
					  
#if DEV_BUS_TYPE==RT_PCI_INTERFACE
                     odm_PathDiversity_8192D(pDM_Odm, pathdiv_para);
#else
                     ODM_FillH2CCmd(pDM_Odm, ODM_H2C_PathDiv,1,(pu1Byte)(&fw_value));	
                     //for(u1Byte n=0; n<80,n++)
                     //{
                     //delay_us(500);
			  ODM_delay_ms(500);
                     odm_PathDiversity_8192D(pDM_Odm, pathdiv_para);
			 		 
			 fw_value=0x01;   	// to backup RF Path A Releated Registers		 
                     ODM_FillH2CCmd(pDM_Odm, ODM_H2C_PathDiv,1,(pu1Byte)(&fw_value));	
#endif	
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH=: FIRST TIME To DO PATH SWITCH!\n "));	
	           }		   
		    else
		    {
		        pathdiv_para = 0x01;
			 fw_value = 0x02;	
#if DEV_BUS_TYPE==RT_PCI_INTERFACE
                     odm_PathDiversity_8192D(pDM_Odm, pathdiv_para);
#else
                     ODM_FillH2CCmd(pDM_Odm, ODM_H2C_PathDiv,1,(pu1Byte)(&fw_value));	
#endif	
		    }		
	       }
           //   odm_PathDiversity_8192D(Adapter, pathdiv_para);
	}

	//1 5.Reset Statistics
	pDM_SWAT_Table->PreAntenna = pDM_SWAT_Table->CurAntenna;
	pDM_SWAT_Table->CurAntenna = nextAntenna;
	pDM_SWAT_Table->PreRSSI = curRSSI;

	//1 6.Set next timer

	if(pDM_SWAT_Table->RSSI_Trying == 0)
		return;

	if(pDM_SWAT_Table->RSSI_Trying%2 == 0)
	{
		if(pDM_SWAT_Table->TestMode == TP_MODE)
		{
			if(TrafficLoad == TRAFFIC_HIGH)
			{
#if DEV_BUS_TYPE==RT_PCI_INTERFACE
				ODM_SetTimer( pDM_Odm, &pDM_Odm->PathDivSwitchTimer, 10 ); //ms
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH=: Test another antenna for 10 ms\n"));
#else
				ODM_SetTimer( pDM_Odm, &pDM_Odm->PathDivSwitchTimer, 20 ); //ms
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH=: Test another antenna for 20 ms\n"));
#endif				
			}
			else if(TrafficLoad == TRAFFIC_LOW)
			{
				ODM_SetTimer( pDM_Odm, &pDM_Odm->PathDivSwitchTimer, 50 ); //ms
				ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH=: Test another antenna for 50 ms\n"));
			}
		}
		else   // TestMode == RSSI_MODE
		{
			ODM_SetTimer( pDM_Odm, &pDM_Odm->PathDivSwitchTimer, 500 ); //ms
			ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH=: Test another antenna for 500 ms\n"));
		}
	}
	else
	{
		if(pDM_SWAT_Table->TestMode == TP_MODE)
		{
			if(TrafficLoad == TRAFFIC_HIGH)
				
#if DEV_BUS_TYPE==RT_PCI_INTERFACE
				ODM_SetTimer( pDM_Odm, &pDM_Odm->PathDivSwitchTimer, 90 ); //ms
				//ODM_RT_TRACE(pDM_Odm,ODM_COMP_PATH_DIV, ODM_DBG_LOUD, ("=PATH=: Test another antenna for 90 ms\n"));
#else		
				ODM_SetTimer( pDM_Odm, &pDM_Odm->PathDivSwitchTimer, 180); //ms
#endif				
			else if(TrafficLoad == TRAFFIC_LOW)
				ODM_SetTimer( pDM_Odm, &pDM_Odm->PathDivSwitchTimer, 100 ); //ms
		}
		else
			ODM_SetTimer( pDM_Odm, &pDM_Odm->PathDivSwitchTimer, 500 ); //ms
	}
}



VOID
ODM_CCKPathDiversityChkPerPktRssi(
	PADAPTER		Adapter,
	BOOLEAN			bIsDefPort,
	BOOLEAN			bMatchBSSID,
	PRT_WLAN_STA	pEntry,
	PRT_RFD			pRfd,
	pu1Byte			pDesc
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	BOOLEAN			bCount = FALSE;
	pPD_T	pDM_PDTable = &Adapter->DM_PDTable;
	//BOOLEAN	isCCKrate = RX_HAL_IS_CCK_RATE_92C(pDesc);
#if DEV_BUS_TYPE != RT_SDIO_INTERFACE
	BOOLEAN	isCCKrate = RX_HAL_IS_CCK_RATE(Adapter, pDesc);
#else  //below code would be removed if we have verified SDIO
	BOOLEAN	isCCKrate = IS_HARDWARE_TYPE_8188E(Adapter) ? RX_HAL_IS_CCK_RATE_88E(pDesc) : RX_HAL_IS_CCK_RATE_92C(pDesc);
#endif

	if ((pHalData->PathDivCfg != 1))
		return;
		
	if(pHalData->RSSI_target==NULL && bIsDefPort && bMatchBSSID)
		bCount = TRUE;
	else if(pHalData->RSSI_target!=NULL && pEntry!=NULL && pHalData->RSSI_target==pEntry)
		bCount = TRUE;

	if(bCount && isCCKrate)
	{
		if(pDM_PDTable->TrainingState == 1 )
		{
			if(pEntry)
			{
				if(pEntry->rssi_stat.RSSI_CCK_Path_cnt[0] != 0)
					pEntry->rssi_stat.RSSI_CCK_Path[0] += pRfd->Status.RxPWDBAll;
				pEntry->rssi_stat.RSSI_CCK_Path_cnt[0]++;
			}
			else
			{
				if(pDM_PDTable->RSSI_CCK_Path_cnt[0] != 0)
					pDM_PDTable->RSSI_CCK_Path[0] += pRfd->Status.RxPWDBAll;
				pDM_PDTable->RSSI_CCK_Path_cnt[0]++;
			}
		}
		else if(pDM_PDTable->TrainingState == 2 )
		{
			if(pEntry)
			{
				if(pEntry->rssi_stat.RSSI_CCK_Path_cnt[1] != 0)
					pEntry->rssi_stat.RSSI_CCK_Path[1] += pRfd->Status.RxPWDBAll;
				pEntry->rssi_stat.RSSI_CCK_Path_cnt[1]++;
			}
			else
			{
				if(pDM_PDTable->RSSI_CCK_Path_cnt[1] != 0)
					pDM_PDTable->RSSI_CCK_Path[1] += pRfd->Status.RxPWDBAll;
				pDM_PDTable->RSSI_CCK_Path_cnt[1]++;
			}
		}
	}
}




//Neil Chen---2011--06--22
//----92D Path Diversity----//
//#ifdef PathDiv92D
//==================================
//3 Path Diversity 
//==================================
//
// 20100514 Luke/Joseph:
// Add new function for antenna diversity after link.
// This is the main function of antenna diversity after link.
// This function is called in HalDmWatchDog() and ODM_SwAntDivChkAntSwitchCallback().
// HalDmWatchDog() calls this function with SWAW_STEP_PEAK to initialize the antenna test.
// In SWAW_STEP_PEAK, another antenna and a 500ms timer will be set for testing.
// After 500ms, ODM_SwAntDivChkAntSwitchCallback() calls this function to compare the signal just
// listened on the air with the RSSI of original antenna.
// It chooses the antenna with better RSSI.
// There is also a aged policy for error trying. Each error trying will cost more 5 seconds waiting 
// penalty to get next try.
//
//
// 20100503 Joseph:
// Add new function SwAntDivCheck8192C().
// This is the main function of Antenna diversity function before link.
// Mainly, it just retains last scan result and scan again.
// After that, it compares the scan result to see which one gets better RSSI.
// It selects antenna with better receiving power and returns better scan result.
//


//
// 20100514 Luke/Joseph:
// This function is used to gather the RSSI information for antenna testing.
// It selects the RSSI of the peer STA that we want to know.
//
VOID
ODM_PathDivChkPerPktRssi(
	PADAPTER		Adapter,
	BOOLEAN			bIsDefPort,
	BOOLEAN			bMatchBSSID,
	PRT_WLAN_STA	pEntry,
	PRT_RFD			pRfd
	)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);	
	BOOLEAN			bCount = FALSE;
	PDM_ODM_T		pDM_Odm = &pHalData->DM_OutSrc;
	pSWAT_T			pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;

	if(pHalData->RSSI_target==NULL && bIsDefPort && bMatchBSSID)
		bCount = TRUE;
	else if(pHalData->RSSI_target!=NULL && pEntry!=NULL && pHalData->RSSI_target==pEntry)
		bCount = TRUE;

	if(bCount)
	{
		//1 RSSI for SW Antenna Switch
		if(pDM_SWAT_Table->CurAntenna == MAIN_ANT)
		{
			pHalData->RSSI_sum_A += pRfd->Status.RxPWDBAll;
			pHalData->RSSI_cnt_A++;
		}
		else
		{
			pHalData->RSSI_sum_B += pRfd->Status.RxPWDBAll;
			pHalData->RSSI_cnt_B++;

		}
	}
}


//
// 20100514 Luke/Joseph:
// Add new function to reset antenna diversity state after link.
//
VOID
ODM_PathDivRestAfterLink(
	IN	PDM_ODM_T		pDM_Odm
	)
{
	PADAPTER		Adapter=pDM_Odm->Adapter;
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	pSWAT_T			pDM_SWAT_Table = &pDM_Odm->DM_SWAT_Table;

	pHalData->RSSI_cnt_A = 0;
	pHalData->RSSI_cnt_B = 0;
	pDM_SWAT_Table->try_flag = 0x0;       // NOT 0xff
	pDM_SWAT_Table->RSSI_Trying = 0;
	pDM_SWAT_Table->SelectAntennaMap=0xAA;
	pDM_SWAT_Table->CurAntenna = MAIN_ANT;  
}


//==================================================
//3 PathDiv End
//==================================================


VOID
ODM_FillTXPathInTXDESC(
		IN	PADAPTER	Adapter,
		IN	PRT_TCB		pTcb,
		IN	pu1Byte		pDesc
)
{
	HAL_DATA_TYPE	*pHalData = GET_HAL_DATA(Adapter);
	u4Byte	TXPath;
	pPD_T	pDM_PDTable = &Adapter->DM_PDTable;

	//2011.09.05  Add by Luke Lee for path diversity
	if(pHalData->PathDivCfg == 1)
	{	
		TXPath = (pDM_PDTable->OFDMTXPath >> pTcb->macId) & BIT0;
		//RT_TRACE(	COMP_INIT, DBG_LOUD, ("Fill TXDESC: macID=%d, TXPath=%d\n", pTcb->macId, TXPath));
		//SET_TX_DESC_TX_ANT_CCK(pDesc,TXPath);
		if(TXPath == 0)
		{
			SET_TX_DESC_TX_ANTL_92C(pDesc,1);
			SET_TX_DESC_TX_ANT_HT_92C(pDesc,1);
		}
		else
		{
			SET_TX_DESC_TX_ANTL_92C(pDesc,2);
			SET_TX_DESC_TX_ANT_HT_92C(pDesc,2);
		}
		TXPath = (pDM_PDTable->CCKTXPath >> pTcb->macId) & BIT0;
		if(TXPath == 0)
		{
			SET_TX_DESC_TX_ANT_CCK_92C(pDesc,1);
		}
		else
		{
			SET_TX_DESC_TX_ANT_CCK_92C(pDesc,2);
		}
	}
}

//Only for MP //Neil Chen--2012--0502--
VOID
odm_PathDivInit_92D(
IN	PDM_ODM_T 	pDM_Odm)
{
	pPATHDIV_PARA	pathIQK = &pDM_Odm->pathIQK;

	pathIQK->org_2g_RegC14=0x0;
	pathIQK->org_2g_RegC4C=0x0;
	pathIQK->org_2g_RegC80=0x0;
	pathIQK->org_2g_RegC94=0x0;
	pathIQK->org_2g_RegCA0=0x0;
	pathIQK->org_5g_RegC14=0x0;
	pathIQK->org_5g_RegCA0=0x0;
	pathIQK->org_5g_RegE30=0x0;
	pathIQK->swt_2g_RegC14=0x0;
	pathIQK->swt_2g_RegC4C=0x0;
	pathIQK->swt_2g_RegC80=0x0;
	pathIQK->swt_2g_RegC94=0x0;
	pathIQK->swt_2g_RegCA0=0x0;
	pathIQK->swt_5g_RegC14=0x0;
	pathIQK->swt_5g_RegCA0=0x0;
	pathIQK->swt_5g_RegE30=0x0;

}


u1Byte
odm_SwAntDivSelectScanChnl(
	IN	PADAPTER	Adapter
	)
{
#if (RT_MEM_SIZE_LEVEL != RT_MEM_SIZE_MINIMUM)
	PHAL_DATA_TYPE		pHalData = GET_HAL_DATA(Adapter);
	PMGNT_INFO			pMgntInfo = &(Adapter->MgntInfo);
	PDM_ODM_T			pDM_Odm = &pHalData->DM_OutSrc;
	u2Byte 				i;
	u1Byte				j, ScanChannel = 0, ChannelNum = 0;
	PRT_CHANNEL_LIST	pChannelList = GET_RT_CHANNEL_LIST(pMgntInfo);
	u1Byte 				EachChannelSTAs[MAX_SCAN_CHANNEL_NUM] = {0};

	if(pMgntInfo->tmpNumBssDesc == 0)
		return 0;

	for(i = 0; i < pMgntInfo->tmpNumBssDesc; i++)
	{		
		ChannelNum = pMgntInfo->tmpbssDesc[i].ChannelNumber;
		for(j = 0; j < pChannelList->ChannelLen; j++)
		{
			if(pChannelList->ChnlListEntry[j].ChannelNum == ChannelNum)
			{
				EachChannelSTAs[j]++;
				break;
			}
		}
	}
	
	for(i = 0; i < MAX_SCAN_CHANNEL_NUM; i++)
		{
		if(EachChannelSTAs[i] > EachChannelSTAs[ScanChannel])
			ScanChannel = (u1Byte)i;
		}

	if(EachChannelSTAs[ScanChannel] == 0)
	{
		ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, DBG_LOUD, ("odm_SwAntDivSelectScanChnl(): Scan List is empty.\n"));
		return 0;
	}
	
	ScanChannel = pChannelList->ChnlListEntry[ScanChannel].ChannelNum;

	

	ODM_RT_TRACE(pDM_Odm,ODM_COMP_ANT_DIV, DBG_LOUD, 
		("odm_SwAntDivSelectScanChnl(): Channel (( %d )) is select as scan channel.\n", ScanChannel));

	return ScanChannel;
#else
	return	0;
#endif	
}


VOID
odm_SwAntDivConstructScanChnl(
	IN	PADAPTER	Adapter,
	IN	u1Byte		ScanChnl
	)
{

	PMGNT_INFO			pMgntInfo = &Adapter->MgntInfo;

	if(ScanChnl == 0)
	{
		u1Byte				i;		
		PRT_CHANNEL_LIST	pChannelList = GET_RT_CHANNEL_LIST(pMgntInfo);
	
		// 20100519 Joseph: Original antenna scanned nothing. 
		// Test antenna shall scan all channel with half period in this condition.

		RT_TRACE_F(COMP_SCAN, DBG_TRACE, (" RT_CHNL_LIST_ACTION_CONSTRUCT chnl %d \n", ScanChnl));

		RtActChannelList(Adapter, RT_CHNL_LIST_ACTION_CONSTRUCT_SCAN_LIST, NULL, NULL);
		for(i = 0; i < pChannelList->ChannelLen; i++)
			pChannelList->ChnlListEntry[i].ScanPeriod /= 2;
	}
	else
	{
		// The using of this CustomizedScanRequest is a trick to rescan the two channels 
		//	under the NORMAL scanning process. It will not affect MGNT_INFO.CustomizedScanRequest.
		CUSTOMIZED_SCAN_REQUEST CustomScanReq;

		CustomScanReq.bEnabled = TRUE;
		CustomScanReq.Channels[0] = ScanChnl;
		CustomScanReq.Channels[1] = pMgntInfo->dot11CurrentChannelNumber;
		CustomScanReq.nChannels = 2;
		CustomScanReq.ScanType = SCAN_ACTIVE;
		CustomScanReq.Duration = DEFAULT_PASSIVE_SCAN_PERIOD;

		RT_TRACE_F(COMP_SCAN, DBG_TRACE, (" RT_CHNL_LIST_ACTION_CONSTRUCT chnl %d \n", ScanChnl));

		RtActChannelList(Adapter, RT_CHNL_LIST_ACTION_CONSTRUCT_SCAN_LIST, &CustomScanReq, NULL);
	}

}
#else

VOID
odm_PathDivChkAntSwitchCallback(
	PRT_TIMER		pTimer
)
{
}

VOID
odm_PathDivChkAntSwitchWorkitemCallback(
    IN PVOID            pContext
    )
{
}

VOID
odm_CCKTXPathDiversityCallback(
	PRT_TIMER		pTimer
)
{
}

VOID
odm_CCKTXPathDiversityWorkItemCallback(
    IN PVOID            pContext
    )
{
}
u1Byte
odm_SwAntDivSelectScanChnl(
	IN	PADAPTER	Adapter
	)
{
	return	0;
}
VOID
odm_SwAntDivConstructScanChnl(
	IN	PADAPTER	Adapter,
	IN	u1Byte		ScanChnl
	)
{
}


#endif

#endif	// #if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

