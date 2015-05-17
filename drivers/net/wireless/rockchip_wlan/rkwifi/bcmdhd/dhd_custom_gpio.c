/*
* Customer code to add GPIO control during WLAN start/stop
* $Copyright Open Broadcom Corporation$
*
* $Id: dhd_custom_gpio.c 493822 2014-07-29 13:20:26Z $
*/

#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>
#include <bcmutils.h>
#include <dngl_stats.h>
#include <dhd.h>
#include <dhd_linux.h>

#include <wlioctl.h>
#include <wl_iw.h>

#define WL_ERROR(x) printf x
#define WL_TRACE(x)

#if defined(CUSTOMER_HW2)

#if defined(PLATFORM_MPS)
int __attribute__ ((weak)) wifi_get_fw_nv_path(char *fw, char *nv) { return 0;};
#endif

#endif 

#if defined(OOB_INTR_ONLY)

#if defined(BCMLXSDMMC)
extern int sdioh_mmc_irq(int irq);
#endif /* (BCMLXSDMMC)  */

#if defined(CUSTOMER_HW3) || defined(PLATFORM_MPS)
#include <mach/gpio.h>
#endif

/* Customer specific Host GPIO defintion  */
static int dhd_oob_gpio_num = -1;

module_param(dhd_oob_gpio_num, int, 0644);
MODULE_PARM_DESC(dhd_oob_gpio_num, "DHD oob gpio number");

/* This function will return:
 *  1) return :  Host gpio interrupt number per customer platform
 *  2) irq_flags_ptr : Type of Host interrupt as Level or Edge
 *
 *  NOTE :
 *  Customer should check his platform definitions
 *  and his Host Interrupt spec
 *  to figure out the proper setting for his platform.
 *  Broadcom provides just reference settings as example.
 *
 */
int dhd_customer_oob_irq_map(void *adapter, unsigned long *irq_flags_ptr)
{
	int  host_oob_irq = 0;

#if defined(CUSTOMER_HW2) && !defined(PLATFORM_MPS)
	host_oob_irq = wifi_platform_get_irq_number(adapter, irq_flags_ptr);

#else
#if defined(CUSTOM_OOB_GPIO_NUM)
	if (dhd_oob_gpio_num < 0) {
		dhd_oob_gpio_num = CUSTOM_OOB_GPIO_NUM;
	}
#endif /* CUSTOMER_OOB_GPIO_NUM */

	if (dhd_oob_gpio_num < 0) {
		WL_ERROR(("%s: ERROR customer specific Host GPIO is NOT defined \n",
		__FUNCTION__));
		return (dhd_oob_gpio_num);
	}

	WL_ERROR(("%s: customer specific Host GPIO number is (%d)\n",
	         __FUNCTION__, dhd_oob_gpio_num));

#if defined CUSTOMER_HW3 || defined(PLATFORM_MPS)
	gpio_request(dhd_oob_gpio_num, "oob irq");
	host_oob_irq = gpio_to_irq(dhd_oob_gpio_num);
	gpio_direction_input(dhd_oob_gpio_num);
#endif /* defined CUSTOMER_HW3 || defined(PLATFORM_MPS) */
#endif 

	return (host_oob_irq);
}
#endif 

/* Customer function to control hw specific wlan gpios */
int
dhd_customer_gpio_wlan_ctrl(void *adapter, int onoff)
{
	int err = 0;

	return err;
}

#ifdef GET_CUSTOM_MAC_ENABLE
/* Function to get custom MAC address */
int
dhd_custom_get_mac_address(void *adapter, unsigned char *buf)
{
	int ret = 0;

	WL_TRACE(("%s Enter\n", __FUNCTION__));
	if (!buf)
		return -EINVAL;

	/* Customer access to MAC address stored outside of DHD driver */
#if (defined(CUSTOMER_HW2) || defined(CUSTOMER_HW10)) && (LINUX_VERSION_CODE >= \
	KERNEL_VERSION(2, 6, 35))
	ret = wifi_platform_get_mac_addr(adapter, buf);
#endif

#ifdef EXAMPLE_GET_MAC
	/* EXAMPLE code */
	{
		struct ether_addr ea_example = {{0x00, 0x11, 0x22, 0x33, 0x44, 0xFF}};
		bcopy((char *)&ea_example, buf, sizeof(struct ether_addr));
	}
#endif /* EXAMPLE_GET_MAC */

	return ret;
}
#endif /* GET_CUSTOM_MAC_ENABLE */

/* Customized Locale table : OPTIONAL feature */
const struct cntry_locales_custom translate_custom_table[] = {
/* Table should be filled out based on custom platform regulatory requirement */
#ifdef EXAMPLE_TABLE
	{"",   "XY", 4},  /* Universal if Country code is unknown or empty */
	{"US", "US", 69}, /* input ISO "US" to : US regrev 69 */
	{"CA", "US", 69}, /* input ISO "CA" to : US regrev 69 */
	{"EU", "EU", 5},  /* European union countries to : EU regrev 05 */
	{"AT", "EU", 5},
	{"BE", "EU", 5},
	{"BG", "EU", 5},
	{"CY", "EU", 5},
	{"CZ", "EU", 5},
	{"DK", "EU", 5},
	{"EE", "EU", 5},
	{"FI", "EU", 5},
	{"FR", "EU", 5},
	{"DE", "EU", 5},
	{"GR", "EU", 5},
	{"HU", "EU", 5},
	{"IE", "EU", 5},
	{"IT", "EU", 5},
	{"LV", "EU", 5},
	{"LI", "EU", 5},
	{"LT", "EU", 5},
	{"LU", "EU", 5},
	{"MT", "EU", 5},
	{"NL", "EU", 5},
	{"PL", "EU", 5},
	{"PT", "EU", 5},
	{"RO", "EU", 5},
	{"SK", "EU", 5},
	{"SI", "EU", 5},
	{"ES", "EU", 5},
	{"SE", "EU", 5},
	{"GB", "EU", 5},
	{"KR", "XY", 3},
	{"AU", "XY", 3},
	{"CN", "XY", 3}, /* input ISO "CN" to : XY regrev 03 */
	{"TW", "XY", 3},
	{"AR", "XY", 3},
	{"MX", "XY", 3},
	{"IL", "IL", 0},
	{"CH", "CH", 0},
	{"TR", "TR", 0},
	{"NO", "NO", 0},
#endif /* EXMAPLE_TABLE */
#if defined(CUSTOMER_HW2) && !defined(CUSTOMER_HW5)
#if defined(BCM4335_CHIP)
	{"",   "XZ", 11},  /* Universal if Country code is unknown or empty */
#endif
	{"AE", "AE", 1},
	{"AR", "AR", 1},
	{"AT", "AT", 1},
	{"AU", "AU", 2},
	{"BE", "BE", 1},
	{"BG", "BG", 1},
	{"BN", "BN", 1},
	{"CA", "CA", 2},
	{"CH", "CH", 1},
	{"CY", "CY", 1},
	{"CZ", "CZ", 1},
	{"DE", "DE", 3},
	{"DK", "DK", 1},
	{"EE", "EE", 1},
	{"ES", "ES", 1},
	{"FI", "FI", 1},
	{"FR", "FR", 1},
	{"GB", "GB", 1},
	{"GR", "GR", 1},
	{"HR", "HR", 1},
	{"HU", "HU", 1},
	{"IE", "IE", 1},
	{"IS", "IS", 1},
	{"IT", "IT", 1},
	{"ID", "ID", 1},
	{"JP", "JP", 8},
	{"KR", "KR", 24},
	{"KW", "KW", 1},
	{"LI", "LI", 1},
	{"LT", "LT", 1},
	{"LU", "LU", 1},
	{"LV", "LV", 1},
	{"MA", "MA", 1},
	{"MT", "MT", 1},
	{"MX", "MX", 1},
	{"NL", "NL", 1},
	{"NO", "NO", 1},
	{"PL", "PL", 1},
	{"PT", "PT", 1},
	{"PY", "PY", 1},
	{"RO", "RO", 1},
	{"SE", "SE", 1},
	{"SI", "SI", 1},
	{"SK", "SK", 1},
	{"TR", "TR", 7},
	{"TW", "TW", 1},
	{"IR", "XZ", 11},	/* Universal if Country code is IRAN, (ISLAMIC REPUBLIC OF) */
	{"SD", "XZ", 11},	/* Universal if Country code is SUDAN */
	{"SY", "XZ", 11},	/* Universal if Country code is SYRIAN ARAB REPUBLIC */
	{"GL", "XZ", 11},	/* Universal if Country code is GREENLAND */
	{"PS", "XZ", 11},	/* Universal if Country code is PALESTINIAN TERRITORY, OCCUPIED */
	{"TL", "XZ", 11},	/* Universal if Country code is TIMOR-LESTE (EAST TIMOR) */
	{"MH", "XZ", 11},	/* Universal if Country code is MARSHALL ISLANDS */
#ifdef BCM4330_CHIP
	{"RU", "RU", 1},
	{"US", "US", 5}
#endif

#elif defined(CUSTOMER_HW5)
	{"",   "XZ", 11},
	{"AE", "AE", 212},
	{"AG", "AG", 2},
	{"AI", "AI", 2},
	{"AL", "AL", 2},
	{"AN", "AN", 3},
	{"AR", "AR", 212},
	{"AS", "AS", 15},
	{"AT", "AT", 4},
	{"AU", "AU", 212},
	{"AW", "AW", 2},
	{"AZ", "AZ", 2},
	{"BA", "BA", 2},
	{"BD", "BD", 2},
	{"BE", "BE", 4},
	{"BG", "BG", 4},
	{"BH", "BH", 4},
	{"BM", "BM", 15},
	{"BN", "BN", 4},
	{"BR", "BR", 212},
	{"BS", "BS", 2},
	{"BY", "BY", 3},
	{"BW", "BW", 1},
	{"CA", "CA", 212},
	{"CH", "CH", 212},
	{"CL", "CL", 212},
	{"CN", "CN", 212},
	{"CO", "CO", 212},
	{"CR", "CR", 21},
	{"CY", "CY", 212},
	{"CZ", "CZ", 212},
	{"DE", "DE", 212},
	{"DK", "DK", 4},
	{"DZ", "DZ", 1},
	{"EC", "EC", 23},
	{"EE", "EE", 4},
	{"EG", "EG", 212},
	{"ES", "ES", 212},
	{"ET", "ET", 2},
	{"FI", "FI", 4},
	{"FR", "FR", 212},
	{"GB", "GB", 212},
	{"GD", "GD", 2},
	{"GF", "GF", 2},
	{"GP", "GP", 2},
	{"GR", "GR", 212},
	{"GT", "GT", 0},
	{"GU", "GU", 17},
	{"HK", "HK", 212},
	{"HR", "HR", 4},
	{"HU", "HU", 4},
	{"IN", "IN", 212},
	{"ID", "ID", 212},
	{"IE", "IE", 5},
	{"IL", "IL", 7},
	{"IN", "IN", 212},
	{"IS", "IS", 4},
	{"IT", "IT", 212},
	{"JO", "JO", 3},
	{"JP", "JP", 212},
	{"KH", "KH", 4},
	{"KI", "KI", 1},
	{"KR", "KR", 212},
	{"KW", "KW", 5},
	{"KY", "KY", 4},
	{"KZ", "KZ", 212},
	{"LA", "LA", 4},
	{"LB", "LB", 6},
	{"LI", "LI", 4},
	{"LK", "LK", 3},
	{"LS", "LS", 2},
	{"LT", "LT", 4},
	{"LR", "LR", 2},
	{"LU", "LU", 3},
	{"LV", "LV", 4},
	{"MA", "MA", 2},
	{"MC", "MC", 1},
	{"MD", "MD", 2},
	{"ME", "ME", 2},
	{"MK", "MK", 2},
	{"MN", "MN", 0},
	{"MO", "MO", 2},
	{"MR", "MR", 2},
	{"MT", "MT", 4},
	{"MQ", "MQ", 2},
	{"MU", "MU", 2},
	{"MV", "MV", 3},
	{"MX", "MX", 212},
	{"MY", "MY", 212},
	{"NI", "NI", 0},
	{"NL", "NL", 212},
	{"NO", "NO", 4},
	{"NP", "NP", 3},
	{"NZ", "NZ", 9},
	{"OM", "OM", 4},
	{"PA", "PA", 17},
	{"PE", "PE", 212},
	{"PG", "PG", 2},
	{"PH", "PH", 212},
	{"PL", "PL", 212},
	{"PR", "PR", 25},
	{"PT", "PT", 212},
	{"PY", "PY", 4},
	{"RE", "RE", 2},
	{"RO", "RO", 212},
	{"RS", "RS", 2},
	{"RU", "RU", 212},
	{"SA", "SA", 212},
	{"SE", "SE", 212},
	{"SG", "SG", 212},
	{"SI", "SI", 4},
	{"SK", "SK", 212},
	{"SN", "SN", 2},
	{"SV", "SV", 25},
	{"TH", "TH", 212},
	{"TR", "TR", 212},
	{"TT", "TT", 5},
	{"TW", "TW", 212},
	{"UA", "UA", 212},
	{"UG", "UG", 2},
	{"US", "US", 212},
	{"UY", "UY", 5},
	{"VA", "VA", 2},
	{"VE", "VE", 3},
	{"VG", "VG", 2},
	{"VI", "VI", 18},
	{"VN", "VN", 4},
	{"YT", "YT", 2},
	{"ZA", "ZA", 212},
	{"ZM", "ZM", 2},
	{"XT", "XT", 212},
	{"XZ", "XZ", 11},
	{"XV", "XV", 17},
	{"Q1", "Q1", 77},
#endif /* CUSTOMER_HW2 and  CUSTOMER_HW5 */
};


/* Customized Locale convertor
*  input : ISO 3166-1 country abbreviation
*  output: customized cspec
*/
void get_customized_country_code(void *adapter, char *country_iso_code, wl_country_t *cspec)
{
#if defined(CUSTOMER_HW2) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 39))

	struct cntry_locales_custom *cloc_ptr;

	if (!cspec)
		return;

	cloc_ptr = wifi_platform_get_country_code(adapter, country_iso_code);
	if (cloc_ptr) {
		strlcpy(cspec->ccode, cloc_ptr->custom_locale, WLC_CNTRY_BUF_SZ);
		cspec->rev = cloc_ptr->custom_locale_rev;
	}
	return;
#else
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
#ifdef EXAMPLE_TABLE
	/* if no country code matched return first universal code from translate_custom_table */
	memcpy(cspec->ccode, translate_custom_table[0].custom_locale, WLC_CNTRY_BUF_SZ);
	cspec->rev = translate_custom_table[0].custom_locale_rev;
#endif /* EXMAPLE_TABLE */
	return;
#endif /* defined(CUSTOMER_HW2) && (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 36)) */
}
