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

#ifndef __PHYDM_DFS_H__
#define __PHYDM_DFS_H__

#define DFS_VERSION	"0.0"

/* ============================================================
  Definition
 ============================================================
*/

/*
============================================================
1  structure
 ============================================================
*/

/* ============================================================
  enumeration
 ============================================================
*/

enum phydm_dfs_region_domain {
	PHYDM_DFS_DOMAIN_UNKNOWN = 0,
	PHYDM_DFS_DOMAIN_FCC = 1,
	PHYDM_DFS_DOMAIN_MKK = 2,
	PHYDM_DFS_DOMAIN_ETSI = 3,
};

/*
============================================================
  function prototype
============================================================
*/
#if defined(CONFIG_PHYDM_DFS_MASTER)
	void phydm_radar_detect_reset(void *p_dm_void);
	void phydm_radar_detect_disable(void *p_dm_void);
	void phydm_radar_detect_enable(void *p_dm_void);
	bool phydm_radar_detect(void *p_dm_void);
#endif /* defined(CONFIG_PHYDM_DFS_MASTER) */

bool
phydm_dfs_master_enabled(
	void		*p_dm_void
);

void
phydm_dfs_debug(
	void		*p_dm_void,
	u32		*const argv,
	u32		*_used,
	char		*output,
	u32		*_out_len
);

#endif /*#ifndef __PHYDM_DFS_H__ */
