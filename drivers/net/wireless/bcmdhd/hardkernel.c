/*
* Customer code to add GPIO control during WLAN start/stop
* Copyright (C) 1999-2011, Broadcom Corporation
* 
*         Unless you and Broadcom execute a separate written software license
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
*      Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a license
* other than the GPL, without Broadcom's express prior written consent.
*
* $Id: dhd_custom_gpio.c,v 1.2.42.1 2010-10-19 00:41:09 Exp $
*/

#include <typedefs.h>
#include <linuxver.h>
#include <osl.h>
#include <bcmutils.h>

#include <dngl_stats.h>
#include <dhd.h>

#include <wlioctl.h>
#include <wl_iw.h>

#ifdef CUSTOMER_HW
//----------------------------------------------------------------------------------------------------------------
//
// ADD Hardkernel
//
//----------------------------------------------------------------------------------------------------------------
#if defined(CONFIG_MACH_ODROID_4X12)||defined(CONFIG_MACH_ODROID_4210)
#include <plat/sdhci.h>
#include <plat/devs.h>	// modifed plat-samsung/dev-hsmmcX.c EXPORT_SYMBOL(s3c_device_hsmmcx) added

#if defined(CONFIG_MACH_ODROID_4210)
	#define	sdmmc_channel	s3c_device_hsmmc0
#endif

#if defined(CONFIG_MACH_ODROID_4X12)
	#define	sdmmc_channel	s3c_device_hsmmc3
#endif

void bcm_wlan_power_on(int flag)
{
	if (flag == 1)	{
		printk("%s : device power on!\n", __func__);
		sdhci_s3c_force_presence_change(&sdmmc_channel, 1);
	}
	else	{
		printk("%s : device power on skip!! (flag = %d)\n", __func__, flag);
	}
}

void bcm_wlan_power_off(int flag)
{
	if (flag == 1)	{
		printk("%s : device power off!\n", __func__);
		sdhci_s3c_force_presence_change(&sdmmc_channel, 0);
	}
	else	{
		printk("%s : device power off skip!! (flag = %d)\n", __func__, flag);
	}
}	
#else

void bcm_wlan_power_on(int flag)
{
	printk("%s : device power on error!! (no sdmmc define) (flag = %d)\n", __func__, flag);
}

void bcm_wlan_power_off(int flag)
{
	printk("%s : device power off error!! (no sdmmc define) (flag = %d)\n", __func__, flag);
}	

#endif	//#if defined(CONFIG_MACH_ODROID_4X12)||defined(CONFIG_MACH_ODROID_4210)
//----------------------------------------------------------------------------------------------------------------
//
// END Hardkernel
//
//----------------------------------------------------------------------------------------------------------------
EXPORT_SYMBOL(bcm_wlan_power_off);
EXPORT_SYMBOL(bcm_wlan_power_on);

#endif /* CUSTOMER_HW */
