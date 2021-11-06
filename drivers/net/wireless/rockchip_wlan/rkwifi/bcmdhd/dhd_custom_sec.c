/*
 * Customer HW 4 dependant file
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Open:>>
 *
 * $Id: dhd_custom_sec.c 334946 2012-05-24 20:38:00Z chanyun $
 */
#if defined(CUSTOMER_HW4) || defined(CUSTOMER_HW40)
#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>

#include <ethernet.h>
#include <dngl_stats.h>
#include <bcmutils.h>
#include <dhd.h>
#include <dhd_dbg.h>
#include <dhd_linux.h>
#include <bcmdevs.h>
#include <bcmdevs_legacy.h>    /* need to still support chips no longer in trunk firmware */

#include <linux/fcntl.h>
#include <linux/fs.h>

const struct cntry_locales_custom translate_custom_table[] = {
	/* default ccode/regrev */
	{"",   "XZ", 11},	/* Universal if Country code is unknown or empty */
	{"IR", "XZ", 11},	/* Universal if Country code is IRAN, (ISLAMIC REPUBLIC OF) */
	{"SD", "XZ", 11},	/* Universal if Country code is SUDAN */
	{"PS", "XZ", 11},	/* Universal if Country code is PALESTINIAN TERRITORY, OCCUPIED */
	{"TL", "XZ", 11},	/* Universal if Country code is TIMOR-LESTE (EAST TIMOR) */
	{"MH", "XZ", 11},	/* Universal if Country code is MARSHALL ISLANDS */
	{"GL", "GP", 2},
	{"AL", "AL", 2},
#ifdef DHD_SUPPORT_GB_999
	{"DZ", "GB", 999},
#else
	{"DZ", "GB", 6},
#endif /* DHD_SUPPORT_GB_999 */
	{"AS", "AS", 12},
	{"AI", "AI", 1},
	{"AF", "AD", 0},
	{"AG", "AG", 2},
	{"AR", "AU", 6},
	{"AW", "AW", 2},
	{"AU", "AU", 6},
	{"AT", "AT", 4},
	{"AZ", "AZ", 2},
	{"BS", "BS", 2},
	{"BH", "BH", 4},
	{"BD", "BD", 1},
	{"BY", "BY", 3},
	{"BE", "BE", 4},
	{"BM", "BM", 12},
	{"BA", "BA", 2},
	{"BR", "BR", 2},
	{"VG", "VG", 2},
	{"BN", "BN", 4},
	{"BG", "BG", 4},
	{"KH", "KH", 2},
	{"KY", "KY", 3},
	{"CN", "CN", 38},
	{"CO", "CO", 17},
	{"CR", "CR", 17},
	{"HR", "HR", 4},
	{"CY", "CY", 4},
	{"CZ", "CZ", 4},
	{"DK", "DK", 4},
	{"EE", "EE", 4},
	{"ET", "ET", 2},
	{"FI", "FI", 4},
	{"FR", "FR", 5},
	{"GF", "GF", 2},
	{"DE", "DE", 7},
	{"GR", "GR", 4},
	{"GD", "GD", 2},
	{"GP", "GP", 2},
	{"GU", "GU", 30},
	{"HK", "HK", 2},
	{"HU", "HU", 4},
	{"IS", "IS", 4},
	{"IN", "IN", 3},
	{"ID", "ID", 1},
	{"IE", "IE", 5},
	{"IL", "IL", 14},
	{"IT", "IT", 4},
	{"JP", "JP", 45},
	{"JO", "JO", 3},
	{"KE", "SA", 0},
	{"KW", "KW", 5},
	{"LA", "LA", 2},
	{"LV", "LV", 4},
	{"LB", "LB", 5},
	{"LS", "LS", 2},
	{"LI", "LI", 4},
	{"LT", "LT", 4},
	{"LU", "LU", 3},
	{"MO", "SG", 0},
	{"MK", "MK", 2},
	{"MW", "MW", 1},
	{"MY", "MY", 3},
	{"MV", "MV", 3},
	{"MT", "MT", 4},
	{"MQ", "MQ", 2},
	{"MR", "MR", 2},
	{"MU", "MU", 2},
	{"YT", "YT", 2},
	{"MX", "MX", 44},
	{"MD", "MD", 2},
	{"MC", "MC", 1},
	{"ME", "ME", 2},
	{"MA", "MA", 2},
	{"NL", "NL", 4},
	{"AN", "GD", 2},
	{"NZ", "NZ", 4},
	{"NO", "NO", 4},
	{"OM", "OM", 4},
	{"PA", "PA", 17},
	{"PG", "AU", 6},
	{"PY", "PY", 2},
	{"PE", "PE", 20},
	{"PH", "PH", 5},
	{"PL", "PL", 4},
	{"PT", "PT", 4},
	{"PR", "PR", 38},
	{"RE", "RE", 2},
	{"RO", "RO", 4},
	{"SN", "MA", 2},
	{"RS", "RS", 2},
	{"SK", "SK", 4},
	{"SI", "SI", 4},
	{"ES", "ES", 4},
	{"LK", "LK", 1},
	{"SE", "SE", 4},
	{"CH", "CH", 4},
	{"TW", "TW", 1},
	{"TH", "TH", 5},
	{"TT", "TT", 3},
	{"TR", "TR", 7},
	{"AE", "AE", 6},
#ifdef DHD_SUPPORT_GB_999
	{"GB", "GB", 999},
#else
	{"GB", "GB", 6},
#endif /* DHD_SUPPORT_GB_999 */
	{"UY", "VE", 3},
	{"VI", "PR", 38},
	{"VA", "VA", 2},
	{"VE", "VE", 3},
	{"VN", "VN", 4},
	{"ZM", "LA", 2},
	{"EC", "EC", 21},
	{"SV", "SV", 25},
#if defined(BCM4358_CHIP) || defined(BCM4359_CHIP)
	{"KR", "KR", 70},
#else
	{"KR", "KR", 48},
#endif
	{"RU", "RU", 13},
	{"UA", "UA", 8},
	{"GT", "GT", 1},
	{"MN", "MN", 1},
	{"NI", "NI", 2},
	{"UZ", "MA", 2},
	{"ZA", "ZA", 6},
	{"EG", "EG", 13},
	{"TN", "TN", 1},
	{"AO", "AD", 0},
	{"BT", "BJ", 0},
	{"BW", "BJ", 0},
	{"LY", "LI", 4},
	{"BO", "NG", 0},
	{"UM", "PR", 38},
	/* Support FCC 15.407 (Part 15E) Changes, effective June 2 2014 */
	/* US/988, Q2/993 country codes with higher power on UNII-1 5G band */
	{"US", "US", 988},
	{"CU", "US", 988},
	{"CA", "Q2", 993},
};

/* Customized Locale convertor
*  input : ISO 3166-1 country abbreviation
*  output: customized cspec
*/
void get_customized_country_code(void *adapter, char *country_iso_code, wl_country_t *cspec)
{
	int size, i;

	size = ARRAYSIZE(translate_custom_table);

	if (cspec == 0)
		 return;

	if (size == 0)
		 return;

	for (i = 0; i < size; i++) {
		if (strcmp(country_iso_code, translate_custom_table[i].iso_abbrev) == 0) {
			memcpy(cspec->ccode,
				translate_custom_table[i].custom_locale, WLC_CNTRY_BUF_SZ);
			cspec->rev = translate_custom_table[i].custom_locale_rev;
			return;
		}
	}
	return;
}

#define PSMINFO PLATFORM_PATH".psm.info"
#define ANTINFO PLATFORM_PATH".ant.info"
#define WIFIVERINFO     PLATFORM_PATH".wifiver.info"
#define LOGTRACEINFO    PLATFORM_PATH".logtrace.info"
#define SOFTAPINFO		PLATFORM_PATH".softap.info"

#ifdef DHD_PM_CONTROL_FROM_FILE
/* XXX This function used for setup PM related value control by read from file.
 * Normally, PM related value Turn Offed for MFG process
 */
extern bool g_pm_control;
#ifdef DHD_EXPORT_CNTL_FILE
extern uint32 pmmode_val;
#endif /* !DHD_EXPORT_CNTL_FILE */
void sec_control_pm(dhd_pub_t *dhd, uint *power_mode)
{
#ifndef DHD_EXPORT_CNTL_FILE
	struct file *fp = NULL;
	char *filepath = PSMINFO;
#endif /* DHD_EXPORT_CNTL_FILE */
	char power_val = 0;
	int ret = 0;
#ifdef DHD_ENABLE_LPC
	uint32 lpc = 0;
#endif /* DHD_ENABLE_LPC */

#ifndef DHD_EXPORT_CNTL_FILE
	g_pm_control = FALSE;
	fp = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(fp) || (fp == NULL)) {
		/* Enable PowerSave Mode */
		dhd_wl_ioctl_cmd(dhd, WLC_SET_PM, (char *)power_mode,
			sizeof(uint), TRUE, 0);
		DHD_ERROR(("[WIFI_SEC] %s: %s doesn't exist"
			" so set PM to %d\n",
			__FUNCTION__, filepath, *power_mode));
		return;
	} else {
		kernel_read_compat(fp, fp->f_pos, &power_val, 1);
		DHD_ERROR(("[WIFI_SEC] %s: POWER_VAL = %c \r\n", __FUNCTION__, power_val));
		filp_close(fp, NULL);
	}
#else
	g_pm_control = FALSE;
	/* Not set from the framework side */
	if (pmmode_val == 0xFFu) {
		/* Enable PowerSave Mode */
		dhd_wl_ioctl_cmd(dhd, WLC_SET_PM, (char *)power_mode,
			sizeof(uint), TRUE, 0);
		DHD_ERROR(("[WIFI_SEC] %s: doesn't set from sysfs"
			" so set PM to %d\n",
			__FUNCTION__, *power_mode));
		return;

	} else {
		power_val = (char)pmmode_val;
	}
#endif /* !DHD_EXPORT_CNTL_FILE */

#ifdef DHD_EXPORT_CNTL_FILE
	if (power_val == 0) {
#else
	/* XXX: power_val is compared with character type read from .psm.info file */
	if (power_val == '0') {
#endif /* DHD_EXPORT_CNTL_FILE */
#ifdef ROAM_ENABLE
		uint roamvar = 1;
#endif
		uint32 wl_updown = 1;

		*power_mode = PM_OFF;
		/* Disable PowerSave Mode */
		dhd_wl_ioctl_cmd(dhd, WLC_SET_PM, (char *)power_mode,
			sizeof(uint), TRUE, 0);
#ifndef CUSTOM_SET_ANTNPM
		/* Turn off MPC in AP mode */
		ret = dhd_iovar(dhd, 0, "mpc", (char *)power_mode, sizeof(*power_mode),
				NULL, 0, TRUE);
#endif /* !CUSTOM_SET_ANTNPM */
		g_pm_control = TRUE;
#ifdef ROAM_ENABLE
		/* Roaming off of dongle */
		ret = dhd_iovar(dhd, 0, "roam_off", (char *)&roamvar, sizeof(roamvar), NULL,
				0, TRUE);
#endif
#ifdef DHD_ENABLE_LPC
		/* Set lpc 0 */
		ret = dhd_iovar(dhd, 0, "lpc", (char *)&lpc, sizeof(lpc), NULL, 0, TRUE);
		if (ret < 0) {
			DHD_ERROR(("[WIFI_SEC] %s: Set lpc failed  %d\n",
			__FUNCTION__, ret));
		}
#endif /* DHD_ENABLE_LPC */
#ifdef DHD_PCIE_RUNTIMEPM
		DHD_ERROR(("[WIFI_SEC] %s : Turn Runtime PM off \n", __FUNCTION__));
		/* Turn Runtime PM off */
		dhdpcie_block_runtime_pm(dhd);
#endif /* DHD_PCIE_RUNTIMEPM */
		/* Disable ocl */
		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_UP, (char *)&wl_updown,
				sizeof(wl_updown), TRUE, 0)) < 0) {
			DHD_ERROR(("[WIFI_SEC] %s: WLC_UP faield %d\n", __FUNCTION__, ret));
		}
#ifndef CUSTOM_SET_OCLOFF
		{
			uint32 ocl_enable = 0;
			ret = dhd_iovar(dhd, 0, "ocl_enable", (char *)&ocl_enable,
					sizeof(ocl_enable), NULL, 0, TRUE);
			if (ret < 0) {
				DHD_ERROR(("[WIFI_SEC] %s: Set ocl_enable %d failed %d\n",
					__FUNCTION__, ocl_enable, ret));
			} else {
				DHD_ERROR(("[WIFI_SEC] %s: Set ocl_enable %d OK %d\n",
					__FUNCTION__, ocl_enable, ret));
			}
		}
#else
		dhd->ocl_off = TRUE;
#endif /* CUSTOM_SET_OCLOFF */
#ifdef WLADPS
		if ((ret = dhd_enable_adps(dhd, ADPS_DISABLE)) < 0) {
			DHD_ERROR(("[WIFI_SEC] %s: dhd_enable_adps failed %d\n",
					__FUNCTION__, ret));
		}
#endif /* WLADPS */

		if ((ret = dhd_wl_ioctl_cmd(dhd, WLC_DOWN, (char *)&wl_updown,
				sizeof(wl_updown), TRUE, 0)) < 0) {
			DHD_ERROR(("[WIFI_SEC] %s: WLC_DOWN faield %d\n",
					__FUNCTION__, ret));
		}
	} else {
		dhd_wl_ioctl_cmd(dhd, WLC_SET_PM, (char *)power_mode,
			sizeof(uint), TRUE, 0);
	}
}
#endif /* DHD_PM_CONTROL_FROM_FILE */

#ifdef MIMO_ANT_SETTING
int get_ant_val_from_file(uint32 *read_val)
{
	int ret = -1;
	struct file *fp = NULL;
	char *filepath = ANTINFO;
	char *p_ant_val = NULL;
	uint32 ant_val = 0;

	/* Read antenna settings from the file */
	fp = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		DHD_ERROR(("[WIFI_SEC] %s: File [%s] doesn't exist\n", __FUNCTION__, filepath));
		ret = -ENOENT;
		return ret;
	} else {
		ret = kernel_read_compat(fp, 0, (char *)&ant_val, sizeof(uint32));
		if (ret < 0) {
			DHD_ERROR(("[WIFI_SEC] %s: File read error, ret=%d\n", __FUNCTION__, ret));
			filp_close(fp, NULL);
			return ret;
		}

		p_ant_val = (char *)&ant_val;
		p_ant_val[sizeof(uint32) - 1] = '\0';
		ant_val = bcm_atoi(p_ant_val);

		DHD_ERROR(("[WIFI_SEC]%s: ANT val = %d\n", __FUNCTION__, ant_val));
		filp_close(fp, NULL);

		/* Check value from the file */
		if (ant_val < 1 || ant_val > 3) {
			DHD_ERROR(("[WIFI_SEC] %s: Invalid value %d read from the file %s\n",
				__FUNCTION__, ant_val, filepath));
			return -1;
		}
	}
	*read_val = ant_val;
	return ret;
}

int dhd_sel_ant_from_file(dhd_pub_t *dhd)
{
	int ret = -1;
	uint32 ant_val = 0;
	uint32 btc_mode = 0;
	uint chip_id = dhd_bus_chip_id(dhd);
#ifndef CUSTOM_SET_ANTNPM
	wl_config_t rsdb_mode;

	memset(&rsdb_mode, 0, sizeof(rsdb_mode));
#endif /* !CUSTOM_SET_ANTNPM */

	/* Check if this chip can support MIMO */
	if (chip_id != BCM4350_CHIP_ID &&
		chip_id != BCM4354_CHIP_ID &&
		chip_id != BCM43569_CHIP_ID &&
		chip_id != BCM4358_CHIP_ID &&
		chip_id != BCM4359_CHIP_ID &&
		chip_id != BCM4355_CHIP_ID &&
		chip_id != BCM4347_CHIP_ID &&
		chip_id != BCM4361_CHIP_ID &&
		chip_id != BCM4375_CHIP_ID &&
		chip_id != BCM4389_CHIP_ID) {
		DHD_ERROR(("[WIFI_SEC] %s: This chipset does not support MIMO\n",
			__FUNCTION__));
		return ret;
	}

#ifndef DHD_EXPORT_CNTL_FILE
	ret = get_ant_val_from_file(&ant_val);
#else
	ant_val = (uint32)antsel;
#endif /* !DHD_EXPORT_CNTL_FILE */
	if (ant_val == 0) {
#ifdef CUSTOM_SET_ANTNPM
		dhd->mimo_ant_set = 0;
#endif /* CUSTOM_SET_ANTNPM */
		return ret;
	}
	DHD_ERROR(("[WIFI_SEC]%s: ANT val = %d\n", __FUNCTION__, ant_val));

	/* bt coex mode off */
	if (dhd_get_fw_mode(dhd->info) == DHD_FLAG_MFG_MODE) {
		ret = dhd_iovar(dhd, 0, "btc_mode", (char *)&btc_mode, sizeof(btc_mode), NULL, 0,
				TRUE);
		if (ret) {
			DHD_ERROR(("[WIFI_SEC] %s: Fail to execute dhd_wl_ioctl_cmd(): "
				"btc_mode, ret=%d\n",
				__FUNCTION__, ret));
			return ret;
		}
	}

#ifndef CUSTOM_SET_ANTNPM
	/* rsdb mode off */
	DHD_ERROR(("[WIFI_SEC] %s: %s the RSDB mode!\n",
		__FUNCTION__, rsdb_mode.config ? "Enable" : "Disable"));
	ret = dhd_iovar(dhd, 0, "rsdb_mode", (char *)&rsdb_mode, sizeof(rsdb_mode), NULL, 0, TRUE);
	if (ret) {
		DHD_ERROR(("[WIFI_SEC] %s: Fail to execute dhd_wl_ioctl_cmd(): "
			"rsdb_mode, ret=%d\n", __FUNCTION__, ret));
		return ret;
	}

	/* Select Antenna */
	ret = dhd_iovar(dhd, 0, "txchain", (char *)&ant_val, sizeof(ant_val), NULL, 0, TRUE);
	if (ret) {
		DHD_ERROR(("[WIFI_SEC] %s: Fail to execute dhd_wl_ioctl_cmd(): txchain, ret=%d\n",
			__FUNCTION__, ret));
		return ret;
	}

	ret = dhd_iovar(dhd, 0, "rxchain", (char *)&ant_val, sizeof(ant_val), NULL, 0, TRUE);
	if (ret) {
		DHD_ERROR(("[WIFI_SEC] %s: Fail to execute dhd_wl_ioctl_cmd(): rxchain, ret=%d\n",
			__FUNCTION__, ret));
		return ret;
	}
#else
	dhd->mimo_ant_set = ant_val;
	DHD_ERROR(("[WIFI_SEC] %s: mimo_ant_set = %d\n", __FUNCTION__, dhd->mimo_ant_set));
#endif /* CUSTOM_SET_ANTNPM */

	return 0;
}
#endif /* MIMO_ANTENNA_SETTING */

#ifdef LOGTRACE_FROM_FILE
/*
 * LOGTRACEINFO = .logtrace.info
 *  - logtrace = 1            => Enable LOGTRACE Event
 *  - logtrace = 0            => Disable LOGTRACE Event
 *  - file not exist          => Disable LOGTRACE Event
 */
int dhd_logtrace_from_file(dhd_pub_t *dhd)
{
#ifndef DHD_EXPORT_CNTL_FILE
	struct file *fp = NULL;
	int ret = -1;
	uint32 logtrace = 0;
	char *filepath = LOGTRACEINFO;
	char *p_logtrace = NULL;

	/* Read LOGTRACE Event on/off request from the file */
	fp = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		DHD_ERROR(("[WIFI_SEC] %s: File [%s] doesn't exist\n", __FUNCTION__, filepath));
		return 0;
	} else {
		ret = kernel_read_compat(fp, 0, (char *)&logtrace, sizeof(uint32));
		if (ret < 0) {
			DHD_ERROR(("[WIFI_SEC] %s: File read error, ret=%d\n", __FUNCTION__, ret));
			filp_close(fp, NULL);
			return 0;
		}

		p_logtrace = (char *)&logtrace;
		p_logtrace[sizeof(uint32) - 1] = '\0';
		logtrace = bcm_atoi(p_logtrace);

		DHD_ERROR(("[WIFI_SEC] %s: LOGTRACE On/Off from file = %d\n",
			__FUNCTION__, logtrace));
		filp_close(fp, NULL);

		/* Check value from the file */
		if (logtrace > 2) {
			DHD_ERROR(("[WIFI_SEC] %s: Invalid value %d read from the file %s\n",
				__FUNCTION__, logtrace, filepath));
			return 0;
		}
	}

	return (int)logtrace;
#else
	DHD_ERROR(("[WIFI_SEC] %s : LOGTRACE On/Off from sysfs = %d\n",
		__FUNCTION__, (int)logtrace_val));
	return (int)logtrace_val;
#endif /* !DHD_EXPORT_CNTL_FILE */
}
#endif /* LOGTRACE_FROM_FILE */

#ifdef USE_WFA_CERT_CONF
#ifndef DHD_EXPORT_CNTL_FILE
int sec_get_param_wfa_cert(dhd_pub_t *dhd, int mode, uint* read_val)
{
	struct file *fp = NULL;
	char *filepath = NULL;
	int val = 0;
	char *p_val = NULL;

	if (!dhd || (mode < SET_PARAM_BUS_TXGLOM_MODE) ||
		(mode >= PARAM_LAST_VALUE)) {
		DHD_ERROR(("[WIFI_SEC] %s: invalid argument\n", __FUNCTION__));
		return BCME_ERROR;
	}

	switch (mode) {
#ifdef BCMSDIO
		case SET_PARAM_BUS_TXGLOM_MODE:
			filepath = PLATFORM_PATH".bustxglom.info";
			break;
#endif /* BCMSDIO */
#if defined(ROAM_ENABLE) || defined(DISABLE_BUILTIN_ROAM)
		case SET_PARAM_ROAMOFF:
			filepath = PLATFORM_PATH".roamoff.info";
			break;
#endif /* ROAM_ENABLE || DISABLE_BUILTIN_ROAM */
#ifdef USE_WL_FRAMEBURST
		case SET_PARAM_FRAMEBURST:
			filepath = PLATFORM_PATH".frameburst.info";
			break;
#endif /* USE_WL_FRAMEBURST */
#ifdef USE_WL_TXBF
		case SET_PARAM_TXBF:
			filepath = PLATFORM_PATH".txbf.info";
			break;
#endif /* USE_WL_TXBF */
#ifdef PROP_TXSTATUS
		case SET_PARAM_PROPTX:
			filepath = PLATFORM_PATH".proptx.info";
			break;
#endif /* PROP_TXSTATUS */
		default:
			DHD_ERROR(("[WIFI_SEC] %s: File to find file name for index=%d\n",
				__FUNCTION__, mode));
			return BCME_ERROR;
	}
	fp = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(fp) || (fp == NULL)) {
		DHD_ERROR(("[WIFI_SEC] %s: File open failed, file path=%s\n",
			__FUNCTION__, filepath));
		return BCME_ERROR;
	} else {
		if (kernel_read_compat(fp, fp->f_pos, (char *)&val, sizeof(uint32)) < 0) {
			filp_close(fp, NULL);
			/* File operation is failed so we will return error code */
			DHD_ERROR(("[WIFI_SEC] %s: read failed, file path=%s\n",
				__FUNCTION__, filepath));
			return BCME_ERROR;
		}
		filp_close(fp, NULL);
	}

	p_val = (char *)&val;
	p_val[sizeof(uint32) - 1] = '\0';
	val = bcm_atoi(p_val);

	switch (mode) {
#if defined(ROAM_ENABLE) || defined(DISABLE_BUILTIN_ROAM)
		case SET_PARAM_ROAMOFF:
#endif /* ROAM_ENABLE || DISABLE_BUILTIN_ROAM */
#ifdef USE_WL_FRAMEBURST
		case SET_PARAM_FRAMEBURST:
#endif /* USE_WL_FRAMEBURST */
#ifdef USE_WL_TXBF
		case SET_PARAM_TXBF:
#endif /* USE_WL_TXBF */
#ifdef PROP_TXSTATUS
		case SET_PARAM_PROPTX:
#endif /* PROP_TXSTATUS */
		if (val < 0 || val > 1) {
			DHD_ERROR(("[WIFI_SEC] %s: value[%d] is out of range\n",
				__FUNCTION__, *read_val));
			return BCME_ERROR;
		}
			break;
		default:
			return BCME_ERROR;
	}
	*read_val = (uint)val;
	return BCME_OK;
}
#else
int sec_get_param_wfa_cert(dhd_pub_t *dhd, int mode, uint* read_val)
{
	uint val = 0;

	if (!dhd || (mode < SET_PARAM_BUS_TXGLOM_MODE) ||
		(mode >= PARAM_LAST_VALUE)) {
		DHD_ERROR(("[WIFI_SEC] %s: invalid argument\n", __FUNCTION__));
		return BCME_ERROR;
	}

	switch (mode) {
#ifdef BCMSDIO
		case SET_PARAM_BUS_TXGLOM_MODE:
			if (bus_txglom == VALUENOTSET)
				return BCME_ERROR;
			else
			    val = (uint)bus_txglom;
			break;
#endif /* BCMSDIO */
#if defined(ROAM_ENABLE) || defined(DISABLE_BUILTIN_ROAM)
		case SET_PARAM_ROAMOFF:
			if (roam_off == VALUENOTSET)
				return BCME_ERROR;
			else
			    val = (uint)roam_off;
			break;
#endif /* ROAM_ENABLE || DISABLE_BUILTIN_ROAM */
#ifdef USE_WL_FRAMEBURST
		case SET_PARAM_FRAMEBURST:
			if (frameburst == VALUENOTSET)
				return BCME_ERROR;
			else
			    val = (uint)frameburst;
			break;
#endif /* USE_WL_FRAMEBURST */
#ifdef USE_WL_TXBF
		case SET_PARAM_TXBF:
			if (txbf == VALUENOTSET)
				return BCME_ERROR;
			else
			    val = (uint)txbf;
			break;
#endif /* USE_WL_TXBF */
#ifdef PROP_TXSTATUS
		case SET_PARAM_PROPTX:
			if (proptx == VALUENOTSET)
				return BCME_ERROR;
			else
			    val = (uint)proptx;
			break;
#endif /* PROP_TXSTATUS */
		default:
		    return BCME_ERROR;
	}
	*read_val = val;
	return BCME_OK;
}
#endif /* !DHD_EXPORT_CNTL_FILE */
#endif /* USE_WFA_CERT_CONF */

#ifdef WRITE_WLANINFO
#define FIRM_PREFIX "Firm_ver:"
#define DHD_PREFIX "DHD_ver:"
#define NV_PREFIX "Nv_info:"
#define CLM_PREFIX "CLM_ver:"
#define max_len(a, b) ((sizeof(a)/(2)) - (strlen(b)) - (3))
#define tstr_len(a, b) ((strlen(a)) + (strlen(b)) + (3))

char version_info[MAX_VERSION_LEN];
char version_old_info[MAX_VERSION_LEN];

int write_filesystem(struct file *file, unsigned long long offset,
	unsigned char* data, unsigned int size)
{
	mm_segment_t oldfs;
	int ret;

	oldfs = get_fs();
	set_fs(KERNEL_DS);

	ret = vfs_write(file, data, size, &offset);

	set_fs(oldfs);
	return ret;
}

uint32 sec_save_wlinfo(char *firm_ver, char *dhd_ver, char *nvram_p, char *clm_ver)
{
#ifndef DHD_EXPORT_CNTL_FILE
	struct file *fp = NULL;
	char *filepath = WIFIVERINFO;
#endif /* DHD_EXPORT_CNTL_FILE */
	struct file *nvfp = NULL;
	int min_len, str_len = 0;
	int ret = 0;
	char* nvram_buf;
	char temp_buf[256];

	DHD_TRACE(("[WIFI_SEC] %s: Entered.\n", __FUNCTION__));

	DHD_INFO(("[WIFI_SEC] firmware version   : %s\n", firm_ver));
	DHD_INFO(("[WIFI_SEC] dhd driver version : %s\n", dhd_ver));
	DHD_INFO(("[WIFI_SEC] nvram path : %s\n", nvram_p));
	DHD_INFO(("[WIFI_SEC] clm version : %s\n", clm_ver));

	memset(version_info, 0, sizeof(version_info));

	if (strlen(dhd_ver)) {
		min_len = min(strlen(dhd_ver), max_len(temp_buf, DHD_PREFIX));
		min_len += strlen(DHD_PREFIX) + 3;
		DHD_INFO(("[WIFI_SEC] DHD ver length : %d\n", min_len));
		snprintf(version_info+str_len, min_len, DHD_PREFIX " %s\n", dhd_ver);
		str_len = strlen(version_info);

		DHD_INFO(("[WIFI_SEC] Driver version_info len : %d\n", str_len));
		DHD_INFO(("[WIFI_SEC] Driver version_info : %s\n", version_info));
	} else {
		DHD_ERROR(("[WIFI_SEC] Driver version is missing.\n"));
	}

	if (strlen(firm_ver)) {
		min_len = min(strlen(firm_ver), max_len(temp_buf, FIRM_PREFIX));
		min_len += strlen(FIRM_PREFIX) + 3;
		DHD_INFO(("[WIFI_SEC] firmware ver length : %d\n", min_len));
		snprintf(version_info+str_len, min_len, FIRM_PREFIX " %s\n", firm_ver);
		str_len = strlen(version_info);

		DHD_INFO(("[WIFI_SEC] Firmware version_info len : %d\n", str_len));
		DHD_INFO(("[WIFI_SEC] Firmware version_info : %s\n", version_info));
	} else {
		DHD_ERROR(("[WIFI_SEC] Firmware version is missing.\n"));
	}

	if (nvram_p) {
		memset(temp_buf, 0, sizeof(temp_buf));
		nvfp = filp_open(nvram_p, O_RDONLY, 0);
		if (IS_ERR(nvfp) || (nvfp == NULL)) {
			DHD_ERROR(("[WIFI_SEC] %s: Nvarm File open failed.\n", __FUNCTION__));
			return -1;
		} else {
			ret = kernel_read_compat(nvfp, nvfp->f_pos, temp_buf, sizeof(temp_buf));
			filp_close(nvfp, NULL);
		}

		if (strlen(temp_buf)) {
			nvram_buf = temp_buf;
			bcmstrtok(&nvram_buf, "\n", 0);
			DHD_INFO(("[WIFI_SEC] nvram tolkening : %s(%zu) \n",
				temp_buf, strlen(temp_buf)));
			snprintf(version_info+str_len, tstr_len(temp_buf, NV_PREFIX),
				NV_PREFIX " %s\n", temp_buf);
			str_len = strlen(version_info);
			DHD_INFO(("[WIFI_SEC] NVRAM version_info : %s\n", version_info));
			DHD_INFO(("[WIFI_SEC] NVRAM version_info len : %d, nvram len : %zu\n",
				str_len, strlen(temp_buf)));
		} else {
			DHD_ERROR(("[WIFI_SEC] NVRAM info is missing.\n"));
		}
	} else {
		DHD_ERROR(("[WIFI_SEC] Not exist nvram path\n"));
	}

	if (strlen(clm_ver)) {
		min_len = min(strlen(clm_ver), max_len(temp_buf, CLM_PREFIX));
		min_len += strlen(CLM_PREFIX) + 3;
		DHD_INFO(("[WIFI_SEC] clm ver length : %d\n", min_len));
		snprintf(version_info+str_len, min_len, CLM_PREFIX " %s\n", clm_ver);
		str_len = strlen(version_info);

		DHD_INFO(("[WIFI_SEC] CLM version_info len : %d\n", str_len));
		DHD_INFO(("[WIFI_SEC] CLM version_info : %s\n", version_info));
	} else {
		DHD_ERROR(("[WIFI_SEC] CLM version is missing.\n"));
	}

	DHD_INFO(("[WIFI_SEC] version_info : %s, strlen : %zu\n",
		version_info, strlen(version_info)));

#ifndef DHD_EXPORT_CNTL_FILE
	fp = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(fp) || (fp == NULL)) {
		DHD_ERROR(("[WIFI_SEC] %s: .wifiver.info File open failed.\n", __FUNCTION__));
	} else {
		memset(version_old_info, 0, sizeof(version_old_info));
		ret = kernel_read_compat(fp, fp->f_pos, version_old_info, sizeof(version_info));
		filp_close(fp, NULL);
		DHD_INFO(("[WIFI_SEC] kernel_read ret : %d.\n", ret));
		if (strcmp(version_info, version_old_info) == 0) {
			DHD_ERROR(("[WIFI_SEC] .wifiver.info already saved.\n"));
			return 0;
		}
	}

	fp = filp_open(filepath, O_RDWR | O_CREAT, 0664);
	if (IS_ERR(fp) || (fp == NULL)) {
		DHD_ERROR(("[WIFI_SEC] %s: .wifiver.info File open failed.\n",
			__FUNCTION__));
	} else {
		ret = write_filesystem(fp, fp->f_pos, version_info, sizeof(version_info));
		DHD_INFO(("[WIFI_SEC] sec_save_wlinfo done. ret : %d\n", ret));
		DHD_ERROR(("[WIFI_SEC] save .wifiver.info file.\n"));
		filp_close(fp, NULL);
	}
#endif /* DHD_EXPORT_CNTL_FILE */
	return ret;
}
#endif /* WRITE_WLANINFO */

#ifdef SUPPORT_MULTIPLE_BOARD_REV_FROM_HW
unsigned int system_hw_rev;
static int
__init get_hw_rev(char *arg)
{
	get_option(&arg, &system_hw_rev);
	printk("dhd : hw_rev : %d\n", system_hw_rev);
	return 0;
}

early_param("androidboot.hw_rev", get_hw_rev);
#endif /* SUPPORT_MULTIPLE_BOARD_REV_FROM_HW */

#ifdef GEN_SOFTAP_INFO_FILE
#define SOFTAP_INFO_FILE_FIRST_LINE		"#.softap.info"
/*
 * # Does RSDB Wifi sharing support?
 * DualBandConcurrency
 * # Both wifi and hotspot can be turned on at the same time?
 * DualInterface
 * # 5Ghz band support?
 * 5G
 * # How many clients can be connected?
 * maxClient
 * # Does hotspot support PowerSave mode?
 * PowerSave
 * # Does android_net_wifi_set_Country_Code_Hal feature supported?
 * HalFn_setCountryCodeHal
 * # Does android_net_wifi_getValidChannels supported?
 * HalFn_getValidChannels
 */
const char *softap_info_items[] = {
	"DualBandConcurrency",
#ifdef DHD_SOFTAP_DUAL_IF_INFO
	"DualInterface",
#endif /* DHD_SOFTAP_DUAL_IF_INFO */
	"5G", "maxClient", "PowerSave",
	"HalFn_setCountryCodeHal", "HalFn_getValidChannels", NULL
};
#if defined(BCM4361_CHIP) || defined(BCM4375_CHIP) || defined(BCM4389_CHIP_DEF)
const char *softap_info_values[] = {
	"yes",
#ifdef DHD_SOFTAP_DUAL_IF_INFO
	"yes",
#endif /* DHD_SOFTAP_DUAL_IF_INFO */
	"yes", "10", "yes", "yes", "yes", NULL
};
#elif defined(BCM43455_CHIP)
const char *softap_info_values[] = {
	"no",
#ifdef DHD_SOFTAP_DUAL_IF_INFO
	"no",
#endif /* DHD_SOFTAP_DUAL_IF_INFO */
	"yes", "10", "no", "yes", "yes", NULL
};
#elif defined(BCM43430_CHIP)
const char *softap_info_values[] = {
	"no",
#ifdef DHD_SOFTAP_DUAL_IF_INFO
	"no",
#endif /* DHD_SOFTAP_DUAL_IF_INFO */
	"no", "10", "no", "yes", "yes", NULL
};
#else
const char *softap_info_values[] = {
	"UNDEF",
#ifdef DHD_SOFTAP_DUAL_IF_INFO
	"UNDEF",
#endif /* DHD_SOFTAP_DUAL_IF_INFO */
	"UNDEF", "UNDEF", "UNDEF", "UNDEF", "UNDEF", NULL
};
#endif /* defined(BCM4361_CHIP) || defined(BCM4375_CHIP) || defined(BCM4389_CHIP_DEF) */
#endif /* GEN_SOFTAP_INFO_FILE */

#ifdef GEN_SOFTAP_INFO_FILE
uint32 sec_save_softap_info(void)
{
#ifndef DHD_EXPORT_CNTL_FILE
	struct file *fp = NULL;
	char *filepath = SOFTAPINFO;
#endif /* DHD_EXPORT_CNTL_FILE */
	char temp_buf[SOFTAP_INFO_BUF_SZ];
	int ret = -1, idx = 0, rem = 0, written = 0;
	char *pos = NULL;

	DHD_TRACE(("[WIFI_SEC] %s: Entered.\n", __FUNCTION__));
	memset(temp_buf, 0, sizeof(temp_buf));

	pos = temp_buf;
	rem = sizeof(temp_buf);
	written = snprintf(pos, sizeof(temp_buf), "%s\n",
		SOFTAP_INFO_FILE_FIRST_LINE);
	do {
		int len = strlen(softap_info_items[idx]) +
			strlen(softap_info_values[idx]) + 2;
		pos += written;
		rem -= written;
		if (len > rem) {
			break;
		}
		written = snprintf(pos, rem, "%s=%s\n",
			softap_info_items[idx], softap_info_values[idx]);
	} while (softap_info_items[++idx] != NULL);

#ifndef DHD_EXPORT_CNTL_FILE
	fp = filp_open(filepath, O_RDWR | O_CREAT, 0664);
	if (IS_ERR(fp) || (fp == NULL)) {
		DHD_ERROR(("[WIFI_SEC] %s: %s File open failed.\n",
			SOFTAPINFO, __FUNCTION__));
	} else {
		ret = write_filesystem(fp, fp->f_pos, temp_buf, strlen(temp_buf));
		DHD_INFO(("[WIFI_SEC] %s done. ret : %d\n", __FUNCTION__, ret));
		DHD_ERROR(("[WIFI_SEC] save %s file.\n", SOFTAPINFO));
		filp_close(fp, NULL);
	}
#else
	strlcpy(softapinfostr, temp_buf, SOFTAP_INFO_BUF_SZ);

	ret = BCME_OK;
#endif /* !DHD_EXPORT_CNTL_FILE */
	return ret;
}
#endif /* GEN_SOFTAP_INFO_FILE */
#endif /* CUSTOMER_HW4 || CUSTOMER_HW40 */

/* XXX WAR: disable pm_bcnrx , scan_ps for BCM4354 WISOL module.
 * WISOL module have ANT_1 Rx sensitivity issue.
*/
#if defined(FORCE_DISABLE_SINGLECORE_SCAN)
void
dhd_force_disable_singlcore_scan(dhd_pub_t *dhd)
{
	int ret = 0;
	struct file *fp = NULL;
	char *filepath = PLATFORM_PATH".cid.info";
	char vender[10] = {0, };
	uint32 pm_bcnrx = 0;
	uint32 scan_ps = 0;

	if (BCM4354_CHIP_ID != dhd_bus_chip_id(dhd))
		return;

	fp = filp_open(filepath, O_RDONLY, 0);
	if (IS_ERR(fp)) {
		DHD_ERROR(("%s file open error\n", filepath));
	} else {
		ret = kernel_read_compat(fp, 0, (char *)vender, 5);

		if (ret > 0 && NULL != strstr(vender, "wisol")) {
			DHD_ERROR(("wisol module : set pm_bcnrx=0, set scan_ps=0\n"));

			ret = dhd_iovar(dhd, 0, "pm_bcnrx", (char *)&pm_bcnrx, sizeof(pm_bcnrx),
					NULL, 0, TRUE);
			if (ret < 0)
				DHD_ERROR(("Set pm_bcnrx error (%d)\n", ret));

			ret = dhd_iovar(dhd, 0, "scan_ps", (char *)&scan_ps, sizeof(scan_ps), NULL,
					0, TRUE);
			if (ret < 0)
				DHD_ERROR(("Set scan_ps error (%d)\n", ret));
		}
		filp_close(fp, NULL);
	}
}
#endif /* FORCE_DISABLE_SINGLECORE_SCAN */

#ifdef BCM4335_XTAL_WAR
bool
check_bcm4335_rev(void)
{
	int ret = -1;
	struct file *fp = NULL;
	char *filepath = "/data/.rev";
	char chip_rev[10] = {0, };
	bool is_revb0 = TRUE;

	DHD_ERROR(("check BCM4335, check_bcm4335_rev \n"));
	fp = filp_open(filepath, O_RDONLY, 0);

	if (IS_ERR(fp)) {
		DHD_ERROR(("/data/.rev file open error\n"));
		is_revb0 = TRUE;
	} else {
		DHD_ERROR(("/data/.rev file Found\n"));
		ret = kernel_read_compat(fp, 0, (char *)chip_rev, 9);
		if (ret != -1 && NULL != strstr(chip_rev, "BCM4335B0")) {
			DHD_ERROR(("Found BCM4335B0\n"));
			is_revb0 = TRUE;
		} else {
			is_revb0 = FALSE;
		}
		filp_close(fp, NULL);
	}
	return is_revb0;
}
#endif /* BCM4335_XTAL_WAR */
