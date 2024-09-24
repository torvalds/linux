/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2024  Realtek Corporation.*/

#ifndef __RTL92DU_TABLE_H__
#define __RTL92DU_TABLE_H__

#define PHY_REG_2T_ARRAYLENGTH 372
#define PHY_REG_ARRAY_PG_LENGTH 624
#define RADIOA_2T_ARRAYLENGTH 378
#define RADIOB_2T_ARRAYLENGTH 384
#define RADIOA_2T_INT_PA_ARRAYLENGTH 378
#define RADIOB_2T_INT_PA_ARRAYLENGTH 384
#define MAC_2T_ARRAYLENGTH 192
#define AGCTAB_ARRAYLENGTH 386
#define AGCTAB_5G_ARRAYLENGTH 194
#define AGCTAB_2G_ARRAYLENGTH 194

extern const u32 rtl8192du_phy_reg_2tarray[PHY_REG_2T_ARRAYLENGTH];
extern const u32 rtl8192du_phy_reg_array_pg[PHY_REG_ARRAY_PG_LENGTH];
extern const u32 rtl8192du_radioa_2tarray[RADIOA_2T_ARRAYLENGTH];
extern const u32 rtl8192du_radiob_2tarray[RADIOB_2T_ARRAYLENGTH];
extern const u32 rtl8192du_radioa_2t_int_paarray[RADIOA_2T_INT_PA_ARRAYLENGTH];
extern const u32 rtl8192du_radiob_2t_int_paarray[RADIOB_2T_INT_PA_ARRAYLENGTH];
extern const u32 rtl8192du_mac_2tarray[MAC_2T_ARRAYLENGTH];
extern const u32 rtl8192du_agctab_array[AGCTAB_ARRAYLENGTH];
extern const u32 rtl8192du_agctab_5garray[AGCTAB_5G_ARRAYLENGTH];
extern const u32 rtl8192du_agctab_2garray[AGCTAB_2G_ARRAYLENGTH];

#endif
