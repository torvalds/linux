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
	#if ((DM_ODM_SUPPORT_TYPE == ODM_WIN) && USB_SWITCH_SUPPORT)
	USB_MODE_MECH	*pUsbModeMech = &pAdapter->UsbModeMechanism;
	#endif
	u1Byte 			search_space_2[NUM_CHOOSE2_FROM4]= {PHYDM_AB, PHYDM_AC, PHYDM_AD, PHYDM_BC, PHYDM_BD, PHYDM_CD };
	u1Byte 			search_space_3[NUM_CHOOSE3_FROM4]= {PHYDM_BCD, PHYDM_ACD,  PHYDM_ABD, PHYDM_ABC};

	#if ((DM_ODM_SUPPORT_TYPE == ODM_WIN) && USB_SWITCH_SUPPORT)
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

#endif	// #if (DM_ODM_SUPPORT_TYPE == ODM_WIN)

