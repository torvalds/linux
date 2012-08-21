/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 *
 * MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */


/*! \file   hw_test.h
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/

#ifndef _HW_TEST_H
#define _HW_TEST_H

/*******************************************************************************
*                         G L O B A L   F L A G S
********************************************************************************
*/


static unsigned int g_state_flag = 0;//power on and power off
static unsigned int g_sdio_init_count = 0;//if it is the 1st init sdio card

//extern unsigned char* g_read_card_info_bysdio[8];
/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#define HWTEST_DRIVER_NAME "mtk_hw_test"


/* debug information */
#define PFX "[HWTEST]"
#define DBG_NAME "[hw_test]"
#define HWTEST_LOG_LOUD    4
#define HWTEST_LOG_DBG 	   3
#define HWTEST_LOG_INFO    2
#define HWTEST_LOG_WARN    1
#define HWTEST_LOG_ERR 	   0

static unsigned int gHWDbgLvl = HWTEST_LOG_INFO;

#define HWTEST_IOCTL_MAGIC 0xc4

#define HWTEST_IOCTL_POWER_ON         		_IO(HWTEST_IOCTL_MAGIC,0)
#define HWTEST_IOCTL_POWER_OFF        		_IO(HWTEST_IOCTL_MAGIC,1)
#define HWTEST_IOCTL_CHIP_RESET       		_IO(HWTEST_IOCTL_MAGIC,2)
#define HWTEST_IOCTL_SDIO_INIT        		_IO(HWTEST_IOCTL_MAGIC,3)
#define HWTEST_IOCTL_SDIO_REMOVE      		_IO(HWTEST_IOCTL_MAGIC,4)
#define HWTEST_IOCTL_SDIO_READ_IOISREADY    _IOR(HWTEST_IOCTL_MAGIC,5,UINT8)
#define HWTEST_IOCTL_SDIO_READ_BLKSIZE      _IOR(HWTEST_IOCTL_MAGIC,6,UINT16)
#define HWTEST_IOCTL_SDIO_READ_INT      	_IOR(HWTEST_IOCTL_MAGIC,7,UINT32)





/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************/
/* Type definition for signed integers */
typedef signed int INT32, *PINT32;
typedef signed char INT8, *PINT8;
typedef signed short INT16, *PINT16;
/* Type definition for unsigned integers */
typedef unsigned int UINT32, *PUINT32;
typedef unsigned char UINT8, *PUINT8;
typedef unsigned short UINT16,*PUINT16;

typedef enum{
	HWTEST_POWER_ON = 0,
	HWTEST_POWER_OFF = 1,
	HWTEST_HW_REST = 2,
	HWTEST_SDIO_INIT = 3,
	HWTEST_CMD_MAX
}HWTEST_CMD;

typedef enum _HWTEST_CHIPVERSION{
    HWVER_MT6620_E1 = 0x0,
    HWVER_MT6620_E2 = 0x1,
    HWVER_MT6620_E3 = 0x2,
    HWVER_MT6620_E4 = 0x3,
    HWVER_MT6620_E5 = 0x4,
    HWVER_MT6620_E6 = 0x5,
    HWVER_MT6620_MAX,
    HWVER_INVALID = 0xff
} ENUM_HWTEST_CHIPVERSION_T;

/* bit field offset definition */
typedef enum {
    HWTEST_STAT_PWRON     = 0, /* is powered on */
    HWTEST_STAT_PWROFF    = 1,
    HWTEST_STAT_SDIO1_ON  = 2, /* is SDIO1 on */
    HWTEST_STAT_SDIO2_ON  = 3, /* is SDIO2 on */
    HWTEST_STAT_MAX
} HWTEST_STAT;


/* FIXME: apply KERN_* definition? */
#define HWTEST_LOUD_FUNC(fmt, arg...)   if (gHWDbgLvl >= HWTEST_LOG_LOUD) { printk(DBG_NAME"%s:"  fmt, __FUNCTION__ ,##arg);}
#define HWTEST_DBG_FUNC(fmt, arg...)    if (gHWDbgLvl >= HWTEST_LOG_DBG) { printk(DBG_NAME "%s:"  fmt, __FUNCTION__ ,##arg);}
#define HWTEST_INFO_FUNC(fmt, arg...)   if (gHWDbgLvl >= HWTEST_LOG_INFO) { printk(DBG_NAME "%s:"  fmt, __FUNCTION__ ,##arg);}
#define HWTEST_WARN_FUNC(fmt, arg...)   if (gHWDbgLvl >= HWTEST_LOG_WARN) { printk(DBG_NAME "%s:"  fmt, __FUNCTION__ ,##arg);}
#define HWTEST_ERR_FUNC(fmt, arg...)    if (gHWDbgLvl >= HWTEST_LOG_ERR) { printk(DBG_NAME "%s(%d):"  fmt, __FUNCTION__ , __LINE__, ##arg);}
#define HWTEST_TRC_FUNC(f)              if (gHWDbgLvl >= HWTEST_LOG_DBG) { printk(DBG_NAME "<%s> <%d>\n", __FUNCTION__, __LINE__);}
// for Ingenic
#define GPIO_PMU	        (32 * 3 + 3) /* GPD3*/
#define GPIO_RST	        (32 * 3 + 0) /* GPD0 */

extern int sdio_card_detect();
extern int sdio_card_remove();
// end for Ingenic

#endif /* _HW_TETS_H */

