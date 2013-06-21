#ifndef _PM_CONFIG_H
#define _PM_CONFIG_H

#include <generated/autoconf.h> /* liugang,for CONFIG_AW_FPGA_PLATFORM,2013-1-22 */
#include "mach/memory.h"
#include "asm-generic/sizes.h"
/*
 * Copyright (c) 2011-2015 yanggq.young@newbietech.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
 #ifdef CONFIG_ARCH_SUN7I
 #undef CONFIG_ARCH_SUN7I
 #endif
 
#define CONFIG_ARCH_SUN7I
#define ENABLE_SUPER_STANDBY
//#define SUN7I_FPGA_SIM
//#define CHECK_IC_VERSION
 
 //#define RETURN_FROM_RESUME0_WITH_MMU    //suspend: 0xf000, resume0: 0xc010, resume1: 0xf000
//#define RETURN_FROM_RESUME0_WITH_NOMMU // suspend: 0x0000, resume0: 0x4010, resume1: 0x0000
//#define DIRECT_RETURN_FROM_SUSPEND //not support yet
//#define ENTER_SUPER_STANDBY    //suspend: 0xf000, resume0: 0x4010, resume1: 0x0000
#define ENTER_SUPER_STANDBY_WITH_NOMMU //not support yet, suspend: 0x0000, resume0: 0x4010, resume1: 0x0000
//#define WATCH_DOG_RESET

/**start address for function run in sram*/
#define SRAM_FUNC_START     SW_VA_SRAM_BASE
#define SRAM_FUNC_START_PA (0x00000000)

#define DRAM_BASE_ADDR      0xc0000000
#define DRAM_BASE_ADDR_PA   0x40000000
#define DRAM_TRANING_SIZE   (64)	//64bytes == 16 words.

#define DRAM_BACKUP_BASE_ADDR_PA (SUPER_STANDBY_BASE) 			// offset
#define DRAM_BACKUP_BASE_ADDR (MEM_ADDRESS(DRAM_BACKUP_BASE_ADDR_PA)) 	// offset
#define DRAM_BACKUP_SIZE (SUPER_STANDBY_SIZE - SZ_16K - SZ_2K) //32K - 1K.

//#define DRAM_STANDBY_PGD_PA SW_PA_SRAM_A3_BASE
//#define DRAM_STANDBY_PGD_ADDR SW_VA_SRAM_A3_BASE

#define DRAM_STANDBY_PGD_PA (DRAM_BACKUP_BASE_ADDR_PA + SUPER_STANDBY_SIZE - SZ_16K)
#define DRAM_STANDBY_PGD_ADDR (MEM_ADDRESS(DRAM_STANDBY_PGD_PA))
#define DRAM_STANDBY_PGD_SIZE (SZ_16K) // 16K bytes.


#define DRAM_BACKUP_BASE_ADDR1_PA (DRAM_BACKUP_BASE_ADDR_PA + DRAM_BACKUP_SIZE)
#define DRAM_BACKUP_BASE_ADDR1 (MEM_ADDRESS(DRAM_BACKUP_BASE_ADDR1_PA))
#define DRAM_BACKUP_SIZE1 (SZ_1K) // 1K bytes.

#define DRAM_BACKUP_BASE_ADDR2_PA (DRAM_BACKUP_BASE_ADDR1_PA + DRAM_BACKUP_SIZE1)
#define DRAM_BACKUP_BASE_ADDR2 (MEM_ADDRESS(DRAM_BACKUP_BASE_ADDR2_PA))
#define DRAM_BACKUP_SIZE2 (SZ_1K) // 1K bytes. 

#define RUNTIME_CONTEXT_SIZE (14) 	//note: r0-r13, 14*4 bytes

#define DRAM_COMPARE_DATA_ADDR (0xc0100000) //1Mbytes offset
#define DRAM_COMPARE_SIZE (0x10000) //?


//for mem mapping
#define MEM_SW_VA_SRAM_BASE (0x00000000)
#define MEM_SW_PA_SRAM_BASE (0x00000000)

#define AXP_WAKEUP_KEY          (1<<0)
#define AXP_WAKEUP_LOWBATT      (1<<1)
#define AXP_WAKEUP_USB          (1<<2)
#define AXP_WAKEUP_AC           (1<<3)
#define AXP_WAKEUP_ASCEND       (1<<4)
#define AXP_WAKEUP_DESCEND      (1<<5)
#define AXP_WAKEUP_SHORT_KEY    (1<<6)
#define AXP_WAKEUP_LONG_KEY     (1<<7)
 
#define AXP_MEM_WAKEUP              (AXP_WAKEUP_LOWBATT | AXP_WAKEUP_USB | AXP_WAKEUP_AC | AXP_WAKEUP_DESCEND | AXP_WAKEUP_ASCEND)
#define AXP_BOOTFAST_WAKEUP         (AXP_WAKEUP_LOWBATT | AXP_WAKEUP_LONG_KEY)
#define __AC(X,Y)	(X##Y)
#define _AC(X,Y)	__AC(X,Y)
#define _AT(T,X)	((T)(X))
#define UL(x) _AC(x, UL)
#define IO_ADDRESS(x)		((x) + 0xf0000000)
#define MEM_ADDRESS(x)		((x) + 0x80000000)

#define SUSPEND_FREQ (720000)	//720M
#define SUSPEND_DELAY_MS (10)

#endif /*_PM_CONFIG_H*/
