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

#ifndef _WMT_DETECT_H_
#define _WMT_DETECT_H_

#include <linux/version.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/sched.h>
#include <linux/poll.h>
#include <asm/current.h>
#include <asm/uaccess.h>
#include <linux/proc_fs.h>
#include <linux/wait.h>
#include <linux/time.h>
#include <linux/delay.h>
/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#ifdef MTK_WCN_REMOVE_KERNEL_MODULE
#define MTK_WCN_REMOVE_KO 1
#else
#define MTK_WCN_REMOVE_KO 0
#endif

#include "sdio_detect.h"
#include "wmt_detect_pwr.h"
#include <mtk_wcn_cmb_stub.h>

#define WMT_DETECT_LOG_LOUD    4
#define WMT_DETECT_LOG_DBG     3
#define WMT_DETECT_LOG_INFO    2
#define WMT_DETECT_LOG_WARN    1
#define WMT_DETECT_LOG_ERR     0

extern unsigned int gWmtDetectDbgLvl;

#define WMT_DETECT_LOUD_FUNC(fmt, arg...) \
do { \
	if (gWmtDetectDbgLvl >= WMT_DETECT_LOG_LOUD) \
		pr_debug(DFT_TAG"[L]%s:"  fmt, __func__ , ##arg); \
} while (0)
#define WMT_DETECT_DBG_FUNC(fmt, arg...) \
do { \
	if (gWmtDetectDbgLvl >= WMT_DETECT_LOG_DBG) \
		pr_debug(DFT_TAG"[D]%s:"  fmt, __func__ , ##arg); \
} while (0)
#define WMT_DETECT_INFO_FUNC(fmt, arg...) \
do { \
	if (gWmtDetectDbgLvl >= WMT_DETECT_LOG_INFO) \
		pr_err(DFT_TAG"[I]%s:"  fmt, __func__ , ##arg); \
} while (0)
#define WMT_DETECT_WARN_FUNC(fmt, arg...) \
do { \
	if (gWmtDetectDbgLvl >= WMT_DETECT_LOG_WARN) \
		pr_warn(DFT_TAG"[W]%s(%d):"  fmt, __func__ , __LINE__, ##arg); \
} while (0)
#define WMT_DETECT_ERR_FUNC(fmt, arg...) \
do { \
	if (gWmtDetectDbgLvl >= WMT_DETECT_LOG_ERR) \
		pr_err(DFT_TAG"[E]%s(%d):"  fmt, __func__ , __LINE__, ##arg); \
} while (0)

#define WMT_IOC_MAGIC			'w'
#define COMBO_IOCTL_GET_CHIP_ID		  _IOR(WMT_IOC_MAGIC, 0, int)
#define COMBO_IOCTL_SET_CHIP_ID		  _IOW(WMT_IOC_MAGIC, 1, int)
#define COMBO_IOCTL_EXT_CHIP_DETECT   _IOR(WMT_IOC_MAGIC, 2, int)
#define COMBO_IOCTL_GET_SOC_CHIP_ID   _IOR(WMT_IOC_MAGIC, 3, int)
#define COMBO_IOCTL_DO_MODULE_INIT    _IOR(WMT_IOC_MAGIC, 4, int)
#define COMBO_IOCTL_MODULE_CLEANUP    _IOR(WMT_IOC_MAGIC, 5, int)
#define COMBO_IOCTL_EXT_CHIP_PWR_ON   _IOR(WMT_IOC_MAGIC, 6, int)
#define COMBO_IOCTL_EXT_CHIP_PWR_OFF  _IOR(WMT_IOC_MAGIC, 7, int)
#define COMBO_IOCTL_DO_SDIO_AUDOK     _IOR(WMT_IOC_MAGIC, 8, int)

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************/
extern int wmt_detect_ext_chip_detect(void);
extern int wmt_detect_ext_chip_pwr_on(void);
extern int wmt_detect_ext_chip_pwr_off(void);

#ifdef MTK_WCN_SOC_CHIP_SUPPORT
extern unsigned int wmt_plat_get_soc_chipid(void);
#endif

#ifdef MTK_WCN_COMBO_CHIP_SUPPORT
/* mtk_uart_pdn_enable -- request uart port enter/exit deep idle mode, this API is defined in uart driver
 *
 * @ port - uart port name, Eg: "ttyMT0", "ttyMT1", "ttyMT2"
 * @ enable - "1", enable deep idle; "0", disable deep idle
 *
 * Return 0 if success, else -1
 */
extern unsigned int mtk_uart_pdn_enable(char *port, int enable);
#endif

#endif
