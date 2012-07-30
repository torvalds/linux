/*
 * drivers/block/sunxi_nand/src/include/nand_drv_cfg.h
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#ifndef __NAND_DRV_CFG_H
#define __NAND_DRV_CFG_H

#include "nand_oal.h"
//==============================================================================
//  define the value of some variable for
//==============================================================================

#define  NAND_VERSION_0                 0x02
#define  NAND_VERSION_1                 0x09


//define the max value of the count of chip select
#if(0)
  #define MAX_CHIP_SELECT_CNT                 (4)
#elif(1)
  #define MAX_CHIP_SELECT_CNT                 (8)
#endif

//define the max value the count of the zone
#define MAX_ZONE_CNT                        (32)

//define the max value of the count of the log block in a zone, the recommended value is 8
#define MAX_LOG_BLK_CNT                     (16)

//define the value of the count of the block mapping table cache
#define BLOCK_MAP_TBL_CACHE_CNT             (MAX_ZONE_CNT)

//define the max value of the count of the page mapping table cache
#define PAGE_MAP_TBL_CACHE_CNT              (MAX_LOG_BLK_CNT * MAX_ZONE_CNT)

//check if block mapping table cache is valid
#if (BLOCK_MAP_TBL_CACHE_CNT < 1)
#error BLOCK_MAP_TBL_CACHE_CNT config error, the value must be larger than 0!!!
#endif
//check if page mapping table cache is valid
#if (PAGE_MAP_TBL_CACHE_CNT < 1)
#error PAGE_MAP_TBL_CACHE_CNT config error, the value must be larger than 0!!!
#endif

//define the frequency of the doing wear-levelling
#define WEAR_LEVELLING_FREQUENCY            (10)

//define the number of the chip select which connecting the boot chip
#define BOOT_CHIP_SELECT_NUM                (0)

//define the default value the count of the data block in one zone
#define DEFAUL_DATA_BLK_CNT_PER_ZONE        (1000)
#if (DEFAUL_DATA_BLK_CNT_PER_ZONE > 1000)
#error  DEFAUL_DATA_BLK_CNT_PER_ZONE config error, the value must not be larger than 1000!!!
#endif

//==============================================================================
//  define some sitch to decide if need support some operation
//==============================================================================

//define the switch that if need support multi-plane program
#define CFG_SUPPORT_MULTI_PLANE_PROGRAM         (1)

//define the switch that if need support multi-plane read
#define CFG_SUPPORT_MULTI_PLANE_READ            (0)

//define the switch that if need support internal inter-leave
#define CFG_SUPPORT_INT_INTERLEAVE              (0)

//define the switch that if need support external inter-leave
#define CFG_SUPPORT_EXT_INTERLEAVE              (1)

//define the switch that if need support cache program
#define CFG_SUPPORT_CACHE_PROGRAM               (0)

//define the switch that if need support doing page copyback by send command
#define CFG_SUPPORT_PAGE_COPYBACK               (1)

//define the switch that if need support wear-levelling
#define CFG_SUPPORT_WEAR_LEVELLING              (0)

//define the switch that if need support read-reclaim
#define CFG_SUPPORT_READ_RECLAIM                (1)

//define if need check the page program status after page program immediately
#define CFG_SUPPORT_CHECK_WRITE_SYNCH           (0)

//define if need support align bank when allocating the log page
#define CFG_SUPPORT_ALIGN_NAND_BNK              (1)

//define if need support Randomizer
#define CFG_SUPPORT_RANDOM                      (1)

//define if need support ReadRetry
#define CFG_SUPPORT_READ_RETRY                  (1)


#define SUPPORT_DMA_IRQ							(0)
#define SUPPORT_RB_IRQ							(0)

//==============================================================================
//  define some pr__s32 switch
//==============================================================================

//define if need pr__s32 the physic operation module debug message
#define PHY_DBG_MESSAGE_ON                  (0)

//define if need pr__s32 the physic operation module error message
#define PHY_ERR_MESSAGE_ON                  (1)

//define if need pr__s32 the nand hardware scan module debug message
#define SCAN_DBG_MESSAGE_ON                 (0)

//define if need pr__s32 the nand hardware scan module error message
#define SCAN_ERR_MESSAGE_ON                 (1)

//define if need pr__s32 the nand disk format module debug message
#define FORMAT_DBG_MESSAGE_ON               (0)

//define if need pr__s32 the nand disk format module error message
#define FORMAT_ERR_MESSAGE_ON               (1)

//define if need pr__s32 the mapping manage module debug message
#define MAPPING_DBG_MESSAGE_ON              (0)

//define if need pr__s32 the mapping manage module error message
#define MAPPING_ERR_MESSAGE_ON              (1)

//define if need pr__s32 the logic control layer debug message
#define LOGICCTL_DBG_MESSAGE_ON             (0)

//define if need pr__s32 the logic control layer error message
#define LOGICCTL_ERR_MESSAGE_ON             (1)


#if PHY_DBG_MESSAGE_ON
#define	   PHY_DBG(...)        			PRINT(__VA_ARGS__)
#else
#define     PHY_DBG(...)
#endif

#if PHY_ERR_MESSAGE_ON
#define     PHY_ERR(...)        		PRINT(__VA_ARGS__)
#else
#define     PHY_ERR(...)
#endif


#if SCAN_DBG_MESSAGE_ON
#define     SCAN_DBG(...)          		PRINT(__VA_ARGS__)
#else
#define     SCAN_DBG(...)
#endif

#if SCAN_ERR_MESSAGE_ON
#define     SCAN_ERR(...)         		PRINT(__VA_ARGS__)
#else
#define     SCAN_ERR(...)
#endif


#if FORMAT_DBG_MESSAGE_ON
#define     FORMAT_DBG(...)         	PRINT(__VA_ARGS__)
#else
#define     FORMAT_DBG(...)
#endif

#if FORMAT_ERR_MESSAGE_ON
#define     FORMAT_ERR(...)        		PRINT(__VA_ARGS__)
#else
#define     FORMAT_ERR(...)
#endif


#if MAPPING_DBG_MESSAGE_ON
#define     MAPPING_DBG(...)        	PRINT(__VA_ARGS__)
#else
#define     MAPPING_DBG(...)
#endif

#if MAPPING_ERR_MESSAGE_ON
#define     MAPPING_ERR(...)       		PRINT(__VA_ARGS__)
#else
#define     MAPPING_ERR(...)
#endif


#if LOGICCTL_DBG_MESSAGE_ON
#define     LOGICCTL_DBG(...)       	PRINT(__VA_ARGS__)
#else
#define     LOGICCTL_DBG(...)
#endif

#if LOGICCTL_ERR_MESSAGE_ON
#define     LOGICCTL_ERR(...)       	PRINT(__VA_ARGS__)
#else
#define     LOGICCTL_ERR(...)
#endif


#endif //ifndef __NAND_DRV_CFG_H

