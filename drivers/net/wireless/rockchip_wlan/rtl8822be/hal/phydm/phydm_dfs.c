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

/*
============================================================
 include files
============================================================
*/

#include "mp_precomp.h"
#include "phydm_precomp.h"

#if defined(CONFIG_PHYDM_DFS_MASTER)
VOID phydm_radar_detect_reset(PVOID pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;

	ODM_SetBBReg(pDM_Odm, 0x924 , BIT15, 0);
	ODM_SetBBReg(pDM_Odm, 0x924 , BIT15, 1);
}

VOID phydm_radar_detect_disable(PVOID pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;

	ODM_SetBBReg(pDM_Odm, 0x924 , BIT15, 0);
}

static VOID phydm_radar_detect_with_dbg_parm(PVOID pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;

	ODM_SetBBReg(pDM_Odm, 0x918, bMaskDWord, pDM_Odm->radar_detect_reg_918);
	ODM_SetBBReg(pDM_Odm, 0x91c, bMaskDWord, pDM_Odm->radar_detect_reg_91c);
	ODM_SetBBReg(pDM_Odm, 0x920, bMaskDWord, pDM_Odm->radar_detect_reg_920);
	ODM_SetBBReg(pDM_Odm, 0x924, bMaskDWord, pDM_Odm->radar_detect_reg_924);
}

/* Init radar detection parameters, called after ch, bw is set */
VOID phydm_radar_detect_enable(PVOID pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u1Byte region_domain = pDM_Odm->DFS_RegionDomain;
	u1Byte c_channel = *(pDM_Odm->pChannel);

	if (region_domain == PHYDM_DFS_DOMAIN_UNKNOWN) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DFS, ODM_DBG_LOUD, ("PHYDM_DFS_DOMAIN_UNKNOWN\n"));
		return;
	}

	 if (pDM_Odm->SupportICType & (ODM_RTL8821 | ODM_RTL8812 | ODM_RTL8881A)) {

		ODM_SetBBReg(pDM_Odm, 0x814, 0x3fffffff, 0x04cc4d10);
		ODM_SetBBReg(pDM_Odm, 0x834, bMaskByte0, 0x06);

		if (pDM_Odm->radar_detect_dbg_parm_en) {
			phydm_radar_detect_with_dbg_parm(pDM_Odm);
			goto exit;
		}

		if (region_domain == PHYDM_DFS_DOMAIN_ETSI) {
			ODM_SetBBReg(pDM_Odm, 0x918, bMaskDWord, 0x1c17ecdf);
			ODM_SetBBReg(pDM_Odm, 0x924, bMaskDWord, 0x01528500);
			ODM_SetBBReg(pDM_Odm, 0x91c, bMaskDWord, 0x0fa21a20);
			ODM_SetBBReg(pDM_Odm, 0x920, bMaskDWord, 0xe0f69204);

		} else if (region_domain == PHYDM_DFS_DOMAIN_MKK) {
			ODM_SetBBReg(pDM_Odm, 0x924, bMaskDWord, 0x01528500);
			ODM_SetBBReg(pDM_Odm, 0x920, bMaskDWord, 0xe0d67234);

			if (c_channel >= 52 && c_channel <= 64) {
				ODM_SetBBReg(pDM_Odm, 0x918, bMaskDWord, 0x1c16ecdf);
				ODM_SetBBReg(pDM_Odm, 0x91c, bMaskDWord, 0x0f141a20);
			} else {
				ODM_SetBBReg(pDM_Odm, 0x918, bMaskDWord, 0x1c16acdf);
				if (pDM_Odm->pBandWidth == ODM_BW20M)
					ODM_SetBBReg(pDM_Odm, 0x91c, bMaskDWord, 0x64721a20);
				else
					ODM_SetBBReg(pDM_Odm, 0x91c, bMaskDWord, 0x68721a20);
			}

		} else if (region_domain == PHYDM_DFS_DOMAIN_FCC) {
			ODM_SetBBReg(pDM_Odm, 0x918, bMaskDWord, 0x1c16acdf);
			ODM_SetBBReg(pDM_Odm, 0x924, bMaskDWord, 0x01528500);
			ODM_SetBBReg(pDM_Odm, 0x920, bMaskDWord, 0xe0d67231);
			if (pDM_Odm->pBandWidth == ODM_BW20M)
				ODM_SetBBReg(pDM_Odm, 0x91c, bMaskDWord, 0x64741a20);
			else
				ODM_SetBBReg(pDM_Odm, 0x91c, bMaskDWord, 0x68741a20);
		} else {
			/* not supported */
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DFS, ODM_DBG_LOUD, ("Unsupported DFS_RegionDomain:%d\n", region_domain));
		}

	} else if (pDM_Odm->SupportICType & (ODM_RTL8814A | ODM_RTL8822B)) {
	
		ODM_SetBBReg(pDM_Odm, 0x814, 0x3fffffff, 0x04cc4d10);
		ODM_SetBBReg(pDM_Odm, 0x834, bMaskByte0, 0x06);
		
		/* 8822B only, when BW = 20M, DFIR output is 40Mhz, but DFS input is 80MMHz, so it need to upgrade to 80MHz */
		if (pDM_Odm->SupportICType & ODM_RTL8822B) {
			if (pDM_Odm->pBandWidth == ODM_BW20M)
				ODM_SetBBReg(pDM_Odm, 0x1984, BIT26, 1);
			else
				ODM_SetBBReg(pDM_Odm, 0x1984, BIT26, 0);
		}

		if (pDM_Odm->radar_detect_dbg_parm_en) {
			phydm_radar_detect_with_dbg_parm(pDM_Odm);
			goto exit;
		}

		if (region_domain == PHYDM_DFS_DOMAIN_ETSI) {
			ODM_SetBBReg(pDM_Odm, 0x918, bMaskDWord, 0x1c16acdf);
			ODM_SetBBReg(pDM_Odm, 0x924, bMaskDWord, 0x095a8500);
			ODM_SetBBReg(pDM_Odm, 0x91c, bMaskDWord, 0x0fa21a20);
			ODM_SetBBReg(pDM_Odm, 0x920, bMaskDWord, 0xe0f57204);

		} else if (region_domain == PHYDM_DFS_DOMAIN_MKK) {
			ODM_SetBBReg(pDM_Odm, 0x924, bMaskDWord, 0x095a8500);
			ODM_SetBBReg(pDM_Odm, 0x920, bMaskDWord, 0xe0d67234);

			if (c_channel >= 52 && c_channel <= 64) {
				ODM_SetBBReg(pDM_Odm, 0x918, bMaskDWord, 0x1c16ecdf);
				ODM_SetBBReg(pDM_Odm, 0x91c, bMaskDWord, 0x0f141a20);
			} else {
				ODM_SetBBReg(pDM_Odm, 0x918, bMaskDWord, 0x1c166cdf);
				if (pDM_Odm->pBandWidth == ODM_BW20M)
					ODM_SetBBReg(pDM_Odm, 0x91c, bMaskDWord, 0x64721a20);
				else
					ODM_SetBBReg(pDM_Odm, 0x91c, bMaskDWord, 0x68721a20);
			}
		} else if (region_domain == PHYDM_DFS_DOMAIN_FCC) {
			ODM_SetBBReg(pDM_Odm, 0x918, bMaskDWord, 0x1c166cdf);
			ODM_SetBBReg(pDM_Odm, 0x924, bMaskDWord, 0x095a8500);
			ODM_SetBBReg(pDM_Odm, 0x920, bMaskDWord, 0xe0d67231);
			if (pDM_Odm->pBandWidth == ODM_BW20M)
				ODM_SetBBReg(pDM_Odm, 0x91c, bMaskDWord, 0x64741a20);
			else
				ODM_SetBBReg(pDM_Odm, 0x91c, bMaskDWord, 0x68741a20);
		} else {
			/* not supported */
			ODM_RT_TRACE(pDM_Odm, ODM_COMP_DFS, ODM_DBG_LOUD, ("Unsupported DFS_RegionDomain:%d\n", region_domain));
		}
	} else {
		/* not supported IC type*/
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DFS, ODM_DBG_LOUD, ("Unsupported IC Type:%d\n", pDM_Odm->SupportICType));
	}

exit:
	phydm_radar_detect_reset(pDM_Odm);
}

BOOLEAN phydm_radar_detect(PVOID pDM_VOID)
{
	PDM_ODM_T pDM_Odm = (PDM_ODM_T)pDM_VOID;
	BOOLEAN enable_DFS = FALSE;
	BOOLEAN radar_detected = FALSE;
	u1Byte region_domain = pDM_Odm->DFS_RegionDomain;

	if (region_domain == PHYDM_DFS_DOMAIN_UNKNOWN) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DFS, ODM_DBG_LOUD, ("PHYDM_DFS_DOMAIN_UNKNOWN\n"));
		return FALSE;
	}

	if (ODM_GetBBReg(pDM_Odm , 0x924, BIT15))
		enable_DFS = TRUE;

	if ((ODM_GetBBReg(pDM_Odm , 0xf98, BIT17))
		|| (!(region_domain == PHYDM_DFS_DOMAIN_ETSI) && (ODM_GetBBReg(pDM_Odm , 0xf98, BIT19))))
		radar_detected = TRUE;

	if (enable_DFS && radar_detected) {
		ODM_RT_TRACE(pDM_Odm, ODM_COMP_DFS, ODM_DBG_LOUD
			, ("Radar detect: enable_DFS:%d, radar_detected:%d\n"
				, enable_DFS, radar_detected));

		phydm_radar_detect_reset(pDM_Odm);
	}

exit:
	return (enable_DFS && radar_detected);
}
#endif /* defined(CONFIG_PHYDM_DFS_MASTER) */

BOOLEAN
phydm_dfs_master_enabled(
	IN		PVOID		pDM_VOID
	)
{
#ifdef CONFIG_PHYDM_DFS_MASTER
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;

	return *pDM_Odm->dfs_master_enabled ? TRUE : FALSE;
#else
	return FALSE;
#endif
}

VOID
phydm_dfs_debug(
	IN		PVOID		pDM_VOID,
	IN		u4Byte		*const argv,
	IN		u4Byte		*_used,
	OUT		char		*output,
	IN		u4Byte		*_out_len
	)
{
	PDM_ODM_T		pDM_Odm = (PDM_ODM_T)pDM_VOID;
	u4Byte used = *_used;
	u4Byte out_len = *_out_len;

	switch (argv[0]) {
	case 1:
		#if defined(CONFIG_PHYDM_DFS_MASTER)
		/* set dbg parameters for radar detection instead of the default value */
		if (argv[1] == 1) {
			pDM_Odm->radar_detect_reg_918 = argv[2];
			pDM_Odm->radar_detect_reg_91c = argv[3];
			pDM_Odm->radar_detect_reg_920 = argv[4];
			pDM_Odm->radar_detect_reg_924 = argv[5];
			pDM_Odm->radar_detect_dbg_parm_en = 1;

			PHYDM_SNPRINTF((output+used, out_len-used, "Radar detection with dbg parameter\n"));
			PHYDM_SNPRINTF((output+used, out_len-used, "reg918:0x%08X\n", pDM_Odm->radar_detect_reg_918));
			PHYDM_SNPRINTF((output+used, out_len-used, "reg91c:0x%08X\n", pDM_Odm->radar_detect_reg_91c));
			PHYDM_SNPRINTF((output+used, out_len-used, "reg920:0x%08X\n", pDM_Odm->radar_detect_reg_920));
			PHYDM_SNPRINTF((output+used, out_len-used, "reg924:0x%08X\n", pDM_Odm->radar_detect_reg_924));
		} else {
			pDM_Odm->radar_detect_dbg_parm_en = 0;
			PHYDM_SNPRINTF((output+used, out_len-used, "Radar detection with default parameter\n"));
		}
		phydm_radar_detect_enable(pDM_Odm);
		#endif /* defined(CONFIG_PHYDM_DFS_MASTER) */

		break;
	default:
		break;
	}
}

