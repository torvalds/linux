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
void phydm_radar_detect_reset(void *p_dm_void)
{
	struct PHY_DM_STRUCT *p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	odm_set_bb_reg(p_dm_odm, 0x924, BIT(15), 0);
	odm_set_bb_reg(p_dm_odm, 0x924, BIT(15), 1);
}

void phydm_radar_detect_disable(void *p_dm_void)
{
	struct PHY_DM_STRUCT *p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	odm_set_bb_reg(p_dm_odm, 0x924, BIT(15), 0);
}

static void phydm_radar_detect_with_dbg_parm(void *p_dm_void)
{
	struct PHY_DM_STRUCT *p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	odm_set_bb_reg(p_dm_odm, 0x918, MASKDWORD, p_dm_odm->radar_detect_reg_918);
	odm_set_bb_reg(p_dm_odm, 0x91c, MASKDWORD, p_dm_odm->radar_detect_reg_91c);
	odm_set_bb_reg(p_dm_odm, 0x920, MASKDWORD, p_dm_odm->radar_detect_reg_920);
	odm_set_bb_reg(p_dm_odm, 0x924, MASKDWORD, p_dm_odm->radar_detect_reg_924);
}

/* Init radar detection parameters, called after ch, bw is set */
void phydm_radar_detect_enable(void *p_dm_void)
{
	struct PHY_DM_STRUCT *p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u8 region_domain = p_dm_odm->dfs_region_domain;
	u8 c_channel = *(p_dm_odm->p_channel);

	if (region_domain == PHYDM_DFS_DOMAIN_UNKNOWN) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DFS, ODM_DBG_LOUD, ("PHYDM_DFS_DOMAIN_UNKNOWN\n"));
		return;
	}

	if (p_dm_odm->support_ic_type & (ODM_RTL8821 | ODM_RTL8812 | ODM_RTL8881A)) {

		odm_set_bb_reg(p_dm_odm, 0x814, 0x3fffffff, 0x04cc4d10);
		odm_set_bb_reg(p_dm_odm, 0x834, MASKBYTE0, 0x06);

		if (p_dm_odm->radar_detect_dbg_parm_en) {
			phydm_radar_detect_with_dbg_parm(p_dm_odm);
			goto exit;
		}

		if (region_domain == PHYDM_DFS_DOMAIN_ETSI) {
			odm_set_bb_reg(p_dm_odm, 0x918, MASKDWORD, 0x1c17ecdf);
			odm_set_bb_reg(p_dm_odm, 0x924, MASKDWORD, 0x01528500);
			odm_set_bb_reg(p_dm_odm, 0x91c, MASKDWORD, 0x0fa21a20);
			odm_set_bb_reg(p_dm_odm, 0x920, MASKDWORD, 0xe0f69204);

		} else if (region_domain == PHYDM_DFS_DOMAIN_MKK) {
			odm_set_bb_reg(p_dm_odm, 0x924, MASKDWORD, 0x01528500);
			odm_set_bb_reg(p_dm_odm, 0x920, MASKDWORD, 0xe0d67234);

			if (c_channel >= 52 && c_channel <= 64) {
				odm_set_bb_reg(p_dm_odm, 0x918, MASKDWORD, 0x1c16ecdf);
				odm_set_bb_reg(p_dm_odm, 0x91c, MASKDWORD, 0x0f141a20);
			} else {
				odm_set_bb_reg(p_dm_odm, 0x918, MASKDWORD, 0x1c16acdf);
				if (p_dm_odm->p_band_width == ODM_BW20M)
					odm_set_bb_reg(p_dm_odm, 0x91c, MASKDWORD, 0x64721a20);
				else
					odm_set_bb_reg(p_dm_odm, 0x91c, MASKDWORD, 0x68721a20);
			}

		} else if (region_domain == PHYDM_DFS_DOMAIN_FCC) {
			odm_set_bb_reg(p_dm_odm, 0x918, MASKDWORD, 0x1c16acdf);
			odm_set_bb_reg(p_dm_odm, 0x924, MASKDWORD, 0x01528500);
			odm_set_bb_reg(p_dm_odm, 0x920, MASKDWORD, 0xe0d67231);
			if (p_dm_odm->p_band_width == ODM_BW20M)
				odm_set_bb_reg(p_dm_odm, 0x91c, MASKDWORD, 0x64741a20);
			else
				odm_set_bb_reg(p_dm_odm, 0x91c, MASKDWORD, 0x68741a20);
		} else {
			/* not supported */
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DFS, ODM_DBG_LOUD, ("Unsupported dfs_region_domain:%d\n", region_domain));
		}

	} else if (p_dm_odm->support_ic_type & (ODM_RTL8814A | ODM_RTL8822B)) {

		odm_set_bb_reg(p_dm_odm, 0x814, 0x3fffffff, 0x04cc4d10);
		odm_set_bb_reg(p_dm_odm, 0x834, MASKBYTE0, 0x06);

		/* 8822B only, when BW = 20M, DFIR output is 40Mhz, but DFS input is 80MMHz, so it need to upgrade to 80MHz */
		if (p_dm_odm->support_ic_type & ODM_RTL8822B) {
			if (p_dm_odm->p_band_width == ODM_BW20M)
				odm_set_bb_reg(p_dm_odm, 0x1984, BIT(26), 1);
			else
				odm_set_bb_reg(p_dm_odm, 0x1984, BIT(26), 0);
		}

		if (p_dm_odm->radar_detect_dbg_parm_en) {
			phydm_radar_detect_with_dbg_parm(p_dm_odm);
			goto exit;
		}

		if (region_domain == PHYDM_DFS_DOMAIN_ETSI) {
			odm_set_bb_reg(p_dm_odm, 0x918, MASKDWORD, 0x1c16acdf);
			odm_set_bb_reg(p_dm_odm, 0x924, MASKDWORD, 0x095a8500);
			odm_set_bb_reg(p_dm_odm, 0x91c, MASKDWORD, 0x0fa21a20);
			odm_set_bb_reg(p_dm_odm, 0x920, MASKDWORD, 0xe0f57204);

		} else if (region_domain == PHYDM_DFS_DOMAIN_MKK) {
			odm_set_bb_reg(p_dm_odm, 0x924, MASKDWORD, 0x095a8500);
			odm_set_bb_reg(p_dm_odm, 0x920, MASKDWORD, 0xe0d67234);

			if (c_channel >= 52 && c_channel <= 64) {
				odm_set_bb_reg(p_dm_odm, 0x918, MASKDWORD, 0x1c16ecdf);
				odm_set_bb_reg(p_dm_odm, 0x91c, MASKDWORD, 0x0f141a20);
			} else {
				odm_set_bb_reg(p_dm_odm, 0x918, MASKDWORD, 0x1c166cdf);
				if (p_dm_odm->p_band_width == ODM_BW20M)
					odm_set_bb_reg(p_dm_odm, 0x91c, MASKDWORD, 0x64721a20);
				else
					odm_set_bb_reg(p_dm_odm, 0x91c, MASKDWORD, 0x68721a20);
			}
		} else if (region_domain == PHYDM_DFS_DOMAIN_FCC) {
			odm_set_bb_reg(p_dm_odm, 0x918, MASKDWORD, 0x1c166cdf);
			odm_set_bb_reg(p_dm_odm, 0x924, MASKDWORD, 0x095a8500);
			odm_set_bb_reg(p_dm_odm, 0x920, MASKDWORD, 0xe0d67231);
			if (p_dm_odm->p_band_width == ODM_BW20M)
				odm_set_bb_reg(p_dm_odm, 0x91c, MASKDWORD, 0x64741a20);
			else
				odm_set_bb_reg(p_dm_odm, 0x91c, MASKDWORD, 0x68741a20);
		} else {
			/* not supported */
			ODM_RT_TRACE(p_dm_odm, ODM_COMP_DFS, ODM_DBG_LOUD, ("Unsupported dfs_region_domain:%d\n", region_domain));
		}
	} else {
		/* not supported IC type*/
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DFS, ODM_DBG_LOUD, ("Unsupported IC type:%d\n", p_dm_odm->support_ic_type));
	}

exit:
	phydm_radar_detect_reset(p_dm_odm);
}

bool phydm_radar_detect(void *p_dm_void)
{
	struct PHY_DM_STRUCT *p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	bool enable_DFS = false;
	bool radar_detected = false;
	u8 region_domain = p_dm_odm->dfs_region_domain;

	if (region_domain == PHYDM_DFS_DOMAIN_UNKNOWN) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DFS, ODM_DBG_LOUD, ("PHYDM_DFS_DOMAIN_UNKNOWN\n"));
		return false;
	}

	if (odm_get_bb_reg(p_dm_odm, 0x924, BIT(15)))
		enable_DFS = true;

	if ((odm_get_bb_reg(p_dm_odm, 0xf98, BIT(17)))
	    || (!(region_domain == PHYDM_DFS_DOMAIN_ETSI) && (odm_get_bb_reg(p_dm_odm, 0xf98, BIT(19)))))
		radar_detected = true;

	if (enable_DFS && radar_detected) {
		ODM_RT_TRACE(p_dm_odm, ODM_COMP_DFS, ODM_DBG_LOUD
			, ("Radar detect: enable_DFS:%d, radar_detected:%d\n"
				, enable_DFS, radar_detected));

		phydm_radar_detect_reset(p_dm_odm);
	}

exit:
	return enable_DFS && radar_detected;
}
#endif /* defined(CONFIG_PHYDM_DFS_MASTER) */

bool
phydm_dfs_master_enabled(
	void		*p_dm_void
)
{
#ifdef CONFIG_PHYDM_DFS_MASTER
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;

	return *p_dm_odm->dfs_master_enabled ? true : false;
#else
	return false;
#endif
}

void
phydm_dfs_debug(
	void		*p_dm_void,
	u32		*const argv,
	u32		*_used,
	char		*output,
	u32		*_out_len
)
{
	struct PHY_DM_STRUCT		*p_dm_odm = (struct PHY_DM_STRUCT *)p_dm_void;
	u32 used = *_used;
	u32 out_len = *_out_len;

	switch (argv[0]) {
	case 1:
#if defined(CONFIG_PHYDM_DFS_MASTER)
		/* set dbg parameters for radar detection instead of the default value */
		if (argv[1] == 1) {
			p_dm_odm->radar_detect_reg_918 = argv[2];
			p_dm_odm->radar_detect_reg_91c = argv[3];
			p_dm_odm->radar_detect_reg_920 = argv[4];
			p_dm_odm->radar_detect_reg_924 = argv[5];
			p_dm_odm->radar_detect_dbg_parm_en = 1;

			PHYDM_SNPRINTF((output + used, out_len - used, "Radar detection with dbg parameter\n"));
			PHYDM_SNPRINTF((output + used, out_len - used, "reg918:0x%08X\n", p_dm_odm->radar_detect_reg_918));
			PHYDM_SNPRINTF((output + used, out_len - used, "reg91c:0x%08X\n", p_dm_odm->radar_detect_reg_91c));
			PHYDM_SNPRINTF((output + used, out_len - used, "reg920:0x%08X\n", p_dm_odm->radar_detect_reg_920));
			PHYDM_SNPRINTF((output + used, out_len - used, "reg924:0x%08X\n", p_dm_odm->radar_detect_reg_924));
		} else {
			p_dm_odm->radar_detect_dbg_parm_en = 0;
			PHYDM_SNPRINTF((output + used, out_len - used, "Radar detection with default parameter\n"));
		}
		phydm_radar_detect_enable(p_dm_odm);
#endif /* defined(CONFIG_PHYDM_DFS_MASTER) */

		break;
	default:
		break;
	}
}
