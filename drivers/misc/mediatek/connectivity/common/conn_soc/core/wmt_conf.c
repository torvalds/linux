/*
* Copyright (C) 2011-2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG         "[WMT-CONF]"

#include "osal_typedef.h"
/* #include "osal.h" */
#include "wmt_lib.h"
#include "wmt_dev.h"
#include "wmt_conf.h"

/*******************************************************************************
*                         D A T A   T Y P E S
********************************************************************************
*/
struct parse_data {
	PINT8 name;
	INT32 (*parser)(P_DEV_WMT pWmtDev, const struct parse_data *data, const PINT8 value);
	PINT8 (*writer)(P_DEV_WMT pWmtDev, const struct parse_data *data);
	/*PINT8 param1, *param2, *param3; */
	/* TODO:[FixMe][George] CLARIFY WHAT SHOULD BE USED HERE!!! */
	PINT8 param1;
	PINT8 param2;
	PINT8 param3;
};

/*******************************************************************************
*                        P U B L I C   D A T A
********************************************************************************
*/


/*******************************************************************************
*                       P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
*******************************************************************************
*/
static INT32 wmt_conf_parse_char(P_DEV_WMT pWmtDev, const struct parse_data *data, const PINT8 pos);

static PINT8 wmt_conf_write_char(P_DEV_WMT pWmtDev, const struct parse_data *data);

static INT32 wmt_conf_parse_short(P_DEV_WMT pWmtDev, const struct parse_data *data, const PINT8 pos);

static PINT8 wmt_conf_write_short(P_DEV_WMT pWmtDev, const struct parse_data *data);

static INT32 wmt_conf_parse_int(P_DEV_WMT pWmtDev, const struct parse_data *data, const PINT8 pos);

static PINT8 wmt_conf_write_int(P_DEV_WMT pWmtDev, const struct parse_data *data);

static INT32 wmt_conf_parse_pair(P_DEV_WMT pWmtDev, const PINT8 pKey, const PINT8 pVal);

static INT32 wmt_conf_parse(P_DEV_WMT pWmtDev, const PINT8 pInBuf, UINT32 size);

#define OFFSET(v) ((void *) &((P_DEV_WMT) 0)->v)

#define CHAR(f) \
{ \
	#f, \
	wmt_conf_parse_char, \
	wmt_conf_write_char, \
	OFFSET(rWmtGenConf.f), \
	NULL, \
	NULL \
}
/* #define CHAR(f) _CHAR(f), NULL, NULL} */

#define SHORT(f) \
{ \
	#f, \
	wmt_conf_parse_short, \
	wmt_conf_write_short, \
	OFFSET(rWmtGenConf.f), \
	NULL, \
	NULL \
}
/* #define SHORT(f) _SHORT(f), NULL, NULL */

#define INT(f) \
{ \
	#f, \
	wmt_conf_parse_int, \
	wmt_conf_write_int, \
	OFFSET(rWmtGenConf.f), \
	NULL, \
	NULL \
}
/* #define INT(f) _INT(f), NULL, NULL */

/*******************************************************************************
*                          F U N C T I O N S
********************************************************************************
*/

static const struct parse_data wmtcfg_fields[] = {
	CHAR(coex_wmt_ant_mode),
	CHAR(coex_wmt_ext_component),
	CHAR(coex_wmt_wifi_time_ctl),
	CHAR(coex_wmt_ext_pta_dev_on),
	CHAR(coex_wmt_filter_mode),

	CHAR(coex_bt_rssi_upper_limit),
	CHAR(coex_bt_rssi_mid_limit),
	CHAR(coex_bt_rssi_lower_limit),
	CHAR(coex_bt_pwr_high),
	CHAR(coex_bt_pwr_mid),
	CHAR(coex_bt_pwr_low),

	CHAR(coex_wifi_rssi_upper_limit),
	CHAR(coex_wifi_rssi_mid_limit),
	CHAR(coex_wifi_rssi_lower_limit),
	CHAR(coex_wifi_pwr_high),
	CHAR(coex_wifi_pwr_mid),
	CHAR(coex_wifi_pwr_low),

	CHAR(coex_ext_pta_hi_tx_tag),
	CHAR(coex_ext_pta_hi_rx_tag),
	CHAR(coex_ext_pta_lo_tx_tag),
	CHAR(coex_ext_pta_lo_rx_tag),
	SHORT(coex_ext_pta_sample_t1),
	SHORT(coex_ext_pta_sample_t2),
	CHAR(coex_ext_pta_wifi_bt_con_trx),

	INT(coex_misc_ext_pta_on),
	INT(coex_misc_ext_feature_set),

	CHAR(wmt_gps_lna_pin),
	CHAR(wmt_gps_lna_enable),

	CHAR(pwr_on_rtc_slot),
	CHAR(pwr_on_ldo_slot),
	CHAR(pwr_on_rst_slot),
	CHAR(pwr_on_off_slot),
	CHAR(pwr_on_on_slot),
	CHAR(co_clock_flag),

	INT(sdio_driving_cfg),

};

#define NUM_WMTCFG_FIELDS (osal_sizeof(wmtcfg_fields) / osal_sizeof(wmtcfg_fields[0]))

static int wmt_conf_parse_char(P_DEV_WMT pWmtDev, const struct parse_data *data, const PINT8 pos)
{
	PINT8 dst;
	long res;
	int ret;

	dst = (PINT8) (((PUINT8) pWmtDev) + (long)data->param1);

	if ((osal_strlen(pos) > 2) && ((*pos) == '0') && (*(pos + 1) == 'x')) {
		ret = osal_strtol(pos + 2, 16, &res);
		if (ret)
			WMT_ERR_FUNC("fail(%d)\n", ret);
		*dst = res;
		WMT_DBG_FUNC("wmtcfg==> %s=0x%x\n", data->name, *dst);
	} else {
		ret = osal_strtol(pos, 10, &res);
		if (ret)
			WMT_ERR_FUNC("fail(%d)\n", ret);
		*dst = res;
		WMT_DBG_FUNC("wmtcfg==> %s=%d\n", data->name, *dst);
	}
	return 0;
}

static PINT8 wmt_conf_write_char(P_DEV_WMT pWmtDev, const struct parse_data *data)
{
	PINT8 src;
	INT32 res;
	PINT8 value;

	src = (PINT8) (((PUINT8) pWmtDev) + (long)data->param1);

	value = osal_malloc(20);
	if (value == NULL)
		return NULL;
	res = osal_snprintf(value, 20, "0x%x", *src);
	if (res < 0 || res >= 20) {
		osal_free(value);
		return NULL;
	}
	value[20 - 1] = '\0';
	return value;
}

static int wmt_conf_parse_short(P_DEV_WMT pWmtDev, const struct parse_data *data, const PINT8 pos)
{
	PUINT16 dst;
	long res;
	int ret;

	dst = (PINT16) (((PUINT8) pWmtDev) + (long)data->param1);

	/* WMT_INFO_FUNC(">strlen(pos)=%d\n", strlen(pos)); */

	if ((osal_strlen(pos) > 2) && ((*pos) == '0') && (*(pos + 1) == 'x')) {
		ret = osal_strtol(pos + 2, 16, &res);
		if (ret)
			WMT_ERR_FUNC("fail(%d)\n", ret);
		*dst = res;
		WMT_DBG_FUNC("wmtcfg==> %s=0x%x\n", data->name, *dst);
	} else {
		ret = osal_strtol(pos, 10, &res);
		if (ret)
			WMT_ERR_FUNC("fail(%d)\n", ret);
		*dst = res;
		WMT_DBG_FUNC("wmtcfg==> %s=%d\n", data->name, *dst);
	}

	return 0;
}

static PINT8 wmt_conf_write_short(P_DEV_WMT pWmtDev, const struct parse_data *data)
{
	PINT16 src;
	INT32 res;
	PINT8 value;

	/* TODO: [FixMe][George] FIX COMPILE WARNING HERE! */
	src = (PINT16) (((PUINT8) pWmtDev) + (long)data->param1);

	value = osal_malloc(20);
	if (value == NULL)
		return NULL;
	res = osal_snprintf(value, 20, "0x%x", *src);
	if (res < 0 || res >= 20) {
		osal_free(value);
		return NULL;
	}
	value[20 - 1] = '\0';
	return value;
}

static int wmt_conf_parse_int(P_DEV_WMT pWmtDev, const struct parse_data *data, const PINT8 pos)
{
	PUINT32 dst;
	long res;
	int ret;

	dst = (PINT32) (((PUINT8) pWmtDev) + (long)data->param1);

	/* WMT_INFO_FUNC(">strlen(pos)=%d\n", strlen(pos)); */

	if ((osal_strlen(pos) > 2) && ((*pos) == '0') && (*(pos + 1) == 'x')) {
		ret = osal_strtol(pos + 2, 16, &res);
		if (ret)
			WMT_ERR_FUNC("fail(%d)\n", ret);
		*dst = res;
		WMT_DBG_FUNC("wmtcfg==> %s=0x%x\n", data->name, *dst);
	} else {
		ret = osal_strtol(pos, 10, &res);
		if (ret)
			WMT_ERR_FUNC("fail(%d)\n", ret);
		*dst = res;
		WMT_DBG_FUNC("wmtcfg==> %s=%d\n", data->name, *dst);
	}

	return 0;
}

static PINT8 wmt_conf_write_int(P_DEV_WMT pWmtDev, const struct parse_data *data)
{
	PINT32 src;
	INT32 res;
	PINT8 value;

	src = (PUINT32) (((PUINT8) pWmtDev) + (long)data->param1);

	value = osal_malloc(20);
	if (value == NULL)
		return NULL;
	res = osal_snprintf(value, 20, "0x%x", *src);
	if (res < 0 || res >= 20) {
		osal_free(value);
		return NULL;
	}
	value[20 - 1] = '\0';
	return value;
}

static INT32 wmt_conf_parse_pair(P_DEV_WMT pWmtDev, const PINT8 pKey, const PINT8 pVal)
{
	int i = 0;
	int ret = 0;

	/* WMT_INFO_FUNC( DBG_NAME "cfg(%s) val(%s)\n", pKey, pVal); */

	for (i = 0; i < NUM_WMTCFG_FIELDS; i++) {
		const struct parse_data *field = &wmtcfg_fields[i];

		if (osal_strcmp(pKey, field->name) != 0)
			continue;
		if (field->parser(pWmtDev, field, pVal)) {
			WMT_ERR_FUNC("failed to parse %s '%s'.\n", pKey, pVal);
			ret = -1;
		}
		break;
	}
	if (i == NUM_WMTCFG_FIELDS) {
		WMT_ERR_FUNC("unknown field '%s'.\n", pKey);
		ret = -1;
	}

	return ret;
}

static INT32 wmt_conf_parse(P_DEV_WMT pWmtDev, const PINT8 pInBuf, UINT32 size)
{
	PINT8 pch;
	PINT8 pBuf;
	PINT8 pLine;
	PINT8 pKey;
	PINT8 pVal;
	PINT8 pPos;
	INT32 ret = 0;
	INT32 i = 0;
	PINT8 pa = NULL;

	pBuf = osal_malloc(size);
	if (!pBuf)
		return -1;

	osal_memcpy(pBuf, pInBuf, size);
	pBuf[size] = '\0';

	pch = pBuf;
	/* pch is to be updated by strsep(). Keep pBuf unchanged!! */

#if 0
	{
		PINT8 buf_ptr = pBuf;
		INT32 k = 0;

		WMT_INFO_FUNC("%s len=%d", "wmcfg.content:", size);
		for (k = 0; k < size; k++) {
			/* if(k%16 == 0)  WMT_INFO_FUNC("\n"); */
			WMT_INFO_FUNC("%c", buf_ptr[k]);
		}
		WMT_INFO_FUNC("--end\n");
	}
#endif

	while ((pLine = osal_strsep(&pch, "\r\n")) != NULL) {
		/* pch is updated to the end of pLine by strsep() and updated to '\0' */
		/*WMT_INFO_FUNC("strsep offset(%d), char(%d, '%c' )\n", pLine-pBuf, *pLine, *pLine); */
		/* parse each line */

		/* WMT_INFO_FUNC("==> Line = (%s)\n", pLine); */

		if (!*pLine)
			continue;

		pVal = osal_strchr(pLine, '=');
		if (!pVal) {
			WMT_WARN_FUNC("mal-format cfg string(%s)\n", pLine);
			continue;
		}

		/* |<-pLine->|'='<-pVal->|'\n' ('\0')|  */
		*pVal = '\0';	/* replace '=' with '\0' to get key */
		/* |<-pKey->|'\0'|<-pVal->|'\n' ('\0')|  */
		pKey = pLine;

		if ((pVal - pBuf) < size)
			pVal++;

		/*key handling */
		pPos = pKey;
		/*skip space characeter */
		while (((*pPos) == ' ') || ((*pPos) == '\t') || ((*pPos) == '\n')) {
			if ((pPos - pBuf) >= size)
				break;
			pPos++;
		}
		/*key head */
		pKey = pPos;
		while (((*pPos) != ' ') && ((*pPos) != '\t') && ((*pPos) != '\0') && ((*pPos) != '\n')) {
			if ((pPos - pBuf) >= size)
				break;
			pPos++;
		}
		/*key tail */
		(*pPos) = '\0';

		/*value handling */
		pPos = pVal;
		/*skip space characeter */
		while (((*pPos) == ' ') || ((*pPos) == '\t') || ((*pPos) == '\n')) {
			if ((pPos - pBuf) >= size)
				break;
			pPos++;
		}
		/*value head */
		pVal = pPos;
		while (((*pPos) != ' ') && ((*pPos) != '\t') && ((*pPos) != '\0') && ((*pPos) != '\n')) {
			if ((pPos - pBuf) >= size)
				break;
			pPos++;
		}
		/*value tail */
		(*pPos) = '\0';

		/* WMT_DBG_FUNC("parse (key: #%s#, value: #%s#)\n", pKey, pVal); */
		ret = wmt_conf_parse_pair(pWmtDev, pKey, pVal);
		WMT_DBG_FUNC("parse (%s, %s, %d)\n", pKey, pVal, ret);
		if (ret)
			WMT_WARN_FUNC("parse fail (%s, %s, %d)\n", pKey, pVal, ret);
	}

	for (i = 0; i < NUM_WMTCFG_FIELDS; i++) {
		const struct parse_data *field = &wmtcfg_fields[i];

		pa = field->writer(pWmtDev, field);
		if (pa) {
			WMT_DBG_FUNC("#%d(%s)=>%s\n", i, field->name, pa);
			osal_free(pa);
		} else {
			WMT_ERR_FUNC("failed to parse '%s'.\n", field->name);
		}
	}
	osal_free(pBuf);
	return 0;
}

INT32 wmt_conf_set_cfg_file(const char *name)
{
	if (NULL == name) {
		WMT_ERR_FUNC("name is NULL\n");
		return -1;
	}
	if (osal_strlen(name) >= osal_sizeof(gDevWmt.cWmtcfgName)) {
		WMT_ERR_FUNC("name is too long, length=%d, expect to < %d\n", osal_strlen(name),
			     osal_sizeof(gDevWmt.cWmtcfgName));
		return -2;
	}
	osal_memset(&gDevWmt.cWmtcfgName[0], 0, osal_sizeof(gDevWmt.cWmtcfgName));
	osal_strcpy(&(gDevWmt.cWmtcfgName[0]), name);
	WMT_ERR_FUNC("WMT config file is set to (%s)\n", &(gDevWmt.cWmtcfgName[0]));

	return 0;
}

INT32 wmt_conf_read_file(VOID)
{
	INT32 ret = -1;

	osal_memset(&gDevWmt.rWmtGenConf, 0, osal_sizeof(gDevWmt.rWmtGenConf));
	osal_memset(&gDevWmt.pWmtCfg, 0, osal_sizeof(gDevWmt.pWmtCfg));

#if 1
	osal_memset(&gDevWmt.cWmtcfgName[0], 0, osal_sizeof(gDevWmt.cWmtcfgName));

	osal_strncat(&(gDevWmt.cWmtcfgName[0]), CUST_CFG_WMT_PREFIX, osal_sizeof(CUST_CFG_WMT_PREFIX));
	osal_strncat(&(gDevWmt.cWmtcfgName[0]), CUST_CFG_WMT, osal_sizeof(CUST_CFG_WMT));
#endif

	if (!osal_strlen(&(gDevWmt.cWmtcfgName[0]))) {
		WMT_ERR_FUNC("empty Wmtcfg name\n");
		osal_assert(0);
		return ret;
	}
	WMT_DBG_FUNC("WMT config file:%s\n", &(gDevWmt.cWmtcfgName[0]));
	if (0 == wmt_dev_patch_get(&gDevWmt.cWmtcfgName[0], (osal_firmware **) &gDevWmt.pWmtCfg, 0)) {
		/*get full name patch success */
		WMT_DBG_FUNC("get full file name(%s) buf(0x%p) size(%d)\n",
			      &gDevWmt.cWmtcfgName[0], gDevWmt.pWmtCfg->data, gDevWmt.pWmtCfg->size);

		if (0 == wmt_conf_parse(&gDevWmt, (const PINT8)gDevWmt.pWmtCfg->data, gDevWmt.pWmtCfg->size)) {
			/*config file exists */
			gDevWmt.rWmtGenConf.cfgExist = 1;

			WMT_DBG_FUNC("&gDevWmt.rWmtGenConf=%p\n", &gDevWmt.rWmtGenConf);
			ret = 0;
		} else {
			WMT_ERR_FUNC("wmt conf parsing fail\n");
			osal_assert(0);
			ret = -1;
		}
		wmt_dev_patch_put((osal_firmware **) &gDevWmt.pWmtCfg);
/*
	if (gDevWmt.pWmtCfg)
	{
	    if (gDevWmt.pWmtCfg->data)
	    {
		osal_free(gDevWmt.pWmtCfg->data);
	    }
	    osal_free(gDevWmt.pWmtCfg);
	    gDevWmt.pWmtCfg = 0;
	}
*/
		return ret;
	}
	WMT_ERR_FUNC("read %s file fails\n", &(gDevWmt.cWmtcfgName[0]));
	osal_assert(0);

	gDevWmt.rWmtGenConf.cfgExist = 0;
	return ret;
}

P_WMT_GEN_CONF wmt_conf_get_cfg(VOID)
{
	if (0 == gDevWmt.rWmtGenConf.cfgExist)
		return NULL;

	return &gDevWmt.rWmtGenConf;
}
