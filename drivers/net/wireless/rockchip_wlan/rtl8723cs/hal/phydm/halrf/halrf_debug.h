/******************************************************************************
 *
 * Copyright(c) 2007 - 2017  Realtek Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * The full GNU General Public License is included in this distribution in the
 * file called LICENSE.
 *
 * Contact Information:
 * wlanfae <wlanfae@realtek.com>
 * Realtek Corporation, No. 2, Innovation Road II, Hsinchu Science Park,
 * Hsinchu 300, Taiwan.
 *
 * Larry Finger <Larry.Finger@lwfinger.net>
 *
 *****************************************************************************/

#ifndef __HALRF_DEBUG_H__
#define __HALRF_DEBUG_H__

/*@============================================================*/
/*@include files*/
/*@============================================================*/

/*@============================================================*/
/*@Definition */
/*@============================================================*/

#if DBG

#if (DM_ODM_SUPPORT_TYPE == ODM_AP)
#define RF_DBG(dm, comp, fmt, args...)                     \
	do {                                               \
		if ((comp) & dm->rf_table.rf_dbg_comp) { \
			pr_debug("[RF] ");                 \
			RT_PRINTK(fmt, ##args);            \
		}                                          \
	} while (0)

#elif (DM_ODM_SUPPORT_TYPE == ODM_WIN)

static __inline void RF_DBG(PDM_ODM_T dm, int comp, char *fmt, ...)
{
	RT_STATUS rt_status;
	va_list args;
	char buf[PRINT_MAX_SIZE] = {0};

	if ((comp & dm->rf_table.rf_dbg_comp) == 0)
		return;

	if (fmt == NULL)
		return;

	va_start(args, fmt);
	rt_status = (RT_STATUS)RtlStringCbVPrintfA(buf, PRINT_MAX_SIZE, fmt, args);
	va_end(args);

	if (rt_status != RT_STATUS_SUCCESS) {
		DbgPrint("Failed (%d) to print message to buffer\n", rt_status);
		return;
	}

	DbgPrint("[RF] %s", buf);
}

#elif (DM_ODM_SUPPORT_TYPE == ODM_IOT)

#define RF_DBG(dm, comp, fmt, args...)                     \
	do {                                               \
		if ((comp) & dm->rf_table.rf_dbg_comp) { \
			RT_DEBUG(COMP_PHYDM, DBG_DMESG, "[RF] " fmt, ##args);  \
		}                                          \
	} while (0)

#else
#define RF_DBG(dm, comp, fmt, args...)                                         \
	do {                                                                   \
		struct dm_struct *__dm = dm;                                   \
		if ((comp) & __dm->rf_table.rf_dbg_comp) {                     \
			RT_TRACE(((struct rtl_priv *)__dm->adapter),           \
				 COMP_PHYDM, DBG_DMESG, "[RF] " fmt, ##args);  \
		}                                                              \
	} while (0)
#endif

#else /*#if DBG*/

#if (DM_ODM_SUPPORT_TYPE == ODM_WIN)
static __inline void RF_DBG(struct dm_struct *dm, int comp, char *fmt, ...)
{
#if 0
	RT_STATUS rt_status;
	va_list args;
	char buf[128] = {0};/*PRINT_MAX_SIZE*/

	if ((comp & dm->rf_table.rf_dbg_comp) == 0)
		return;

	if (NULL != fmt) {
		va_start(args, fmt);
		rt_status = (RT_STATUS)RtlStringCbVPrintfA(buf, sizeof(buf), fmt, args);
		va_end(args);
		if (rt_status == RT_STATUS_SUCCESS) {
			halrf_rt_trace(buf);
		}
	}
#endif
}
#else
#define RF_DBG(dm, comp, fmt, args...)
#endif

#endif /*#if DBG*/

/*@============================================================*/
/*@ enumeration */
/*@============================================================*/

/*@============================================================*/
/*@ structure */
/*@============================================================*/

/*@============================================================*/
/*@ function prototype */
/*@============================================================*/

void halrf_cmd_parser(void *dm_void, char input[][16], u32 *_used, char *output,
		      u32 *_out_len, u32 input_num);

void halrf_init_debug_setting(void *dm_void);

#endif /*__HALRF_H__*/
