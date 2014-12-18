/*
 *  Copyright (C) 2002 ARM Ltd.
 *  All Rights Reserved
 *  Copyright (c) 2010, Code Aurora Forum. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
 * Secure Definition
 *
 * Copyright (C) 2013 Amlogic, Inc.
 *
 * Author: Platform-SH@amlogic.com
 *
 */

#ifndef MESON_ARCH_MESON_SECURE_H
#define MESON_ARCH_MESON_SECURE_H

/* Meson Secure Monitor/HAL APIs */
#define CALL_TRUSTZONE_API                      0x1
#define CALL_TRUSTZONE_MON                      0x4
#define CALL_TRUSTZONE_HAL_API                  0x5

/* Secure Monitor mode APIs */
#define TRUSTZONE_MON_TYPE_MASK                 0xF00
#define TRUSTZONE_MON_FUNC_MASK                 0x0FF
#define TRUSTZONE_MON_L2X0                      0x100
#define TRUSTZONE_MON_L2X0_CTRL_INDEX           0x101
#define TRUSTZONE_MON_L2X0_AUXCTRL_INDEX        0x102
#define TRUSTZONE_MON_L2X0_PREFETCH_INDEX       0x103
#define TRUSTZONE_MON_L2X0_TAGLATENCY_INDEX     0x104
#define TRUSTZONE_MON_L2X0_DATALATENCY_INDEX    0x105
#define TRUSTZONE_MON_L2X0_FILTERSTART_INDEX    0x106
#define TRUSTZONE_MON_L2X0_FILTEREND_INDEX      0x107
#define TRUSTZONE_MON_L2X0_DEBUG_INDEX          0x108
#define TRUSTZONE_MON_L2X0_POWER_INDEX          0x109

#define TRUSTZONE_MON_CORE                      0x200
#define TRUSTZONE_MON_CORE_RD_CTRL_INDEX        0x201
#define TRUSTZONE_MON_CORE_WR_CTRL_INDEX        0x202
#define TRUSTZONE_MON_CORE_RD_STATUS0_INDEX     0x203
#define TRUSTZONE_MON_CORE_WR_STATUS0_INDEX     0x204
#define TRUSTZONE_MON_CORE_RD_STATUS1_INDEX     0x205
#define TRUSTZONE_MON_CORE_WR_STATUS1_INDEX     0x206
#define TRUSTZONE_MON_CORE_BOOTADDR_INDEX       0x207
#define TRUSTZONE_MON_CORE_DDR_INDEX            0x208
#define TRUSTZONE_MON_CORE_RD_SOC_REV1          0x209
#define TRUSTZONE_MON_CORE_RD_SOC_REV2          0x20A

#define TRUSTZONE_MON_SUSPNED_FIRMWARE          0x300
#define TRUSTZONE_MON_SAVE_CPU_GIC              0x400

#define TRUSTZONE_MON_RTC                       0x500
#define TRUSTZONE_MON_RTC_RD_REG_INDEX          0x501
#define TRUSTZONE_MON_RTC_WR_REG_INDEX          0x502

#define TRUSTZONE_MON_REG                       0x600
#define TRUSTZONE_MON_REG_RD_INDEX              0x601
#define TRUSTZONE_MON_REG_WR_INDEX              0x602

#define TRUSTZONE_MON_MEM                       0x700
#define TRUSTZONE_MON_MEM_BASE                  0x701
#define TRUSTZONE_MON_MEM_TOTAL_SIZE            0x702
#define TRUSTZONE_MON_MEM_FLASH                 0x703
#define TRUSTZONE_MON_MEM_FLASH_SIZE            0x704
#define TRUSTZONE_MON_MEM_GE2D                  0x705

/* Secure HAL APIs*/
#define TRUSTZONE_HAL_API_EFUSE                 0x100
#define TRUSTZONE_HAL_API_STORAGE               0x200
#define TRUSTZONE_HAL_API_MEMCONFIG             0x300
#define TRUSTZONE_HAL_API_MEMCONFIG_GE2D        0x301

#ifndef __ASSEMBLER__
extern int meson_smc1(u32 fn, u32 arg);
extern int meson_smc_hal_api(u32 cmdidx, u32 arg);
extern int meson_smc2(u32 arg);
extern int meson_smc3(u32 arg1, u32 arg2);
extern u32 meson_read_corectrl(void);
extern u32 meson_modify_corectrl(u32 arg);
extern u32 meson_read_corestatus(u32 cpu);
extern u32 meson_modify_corestatus(u32 cpu, u32 arg);
extern void meson_auxcoreboot_addr(u32 arg1, u32 arg2);
extern void meson_suspend_firmware(void);
extern uint32_t meson_secure_reg_read(uint32_t addr);
extern uint32_t meson_secure_reg_write(uint32_t addr, uint32_t val);
extern u32 meson_read_socrev1(void);
extern u32 meson_read_socrev2(void);
extern uint32_t meson_secure_mem_base_start(void);
extern uint32_t meson_secure_mem_total_size(void);
extern uint32_t meson_secure_mem_flash_start(void);
extern uint32_t meson_secure_mem_flash_size(void);
extern int32_t meson_secure_mem_ge2d_access(uint32_t msec);

// efuse HAL_API arg
struct efuse_hal_api_arg{
	unsigned int cmd;		// R/W
	unsigned int offset;
	unsigned int size;
	unsigned int buffer_phy;
	unsigned int retcnt_phy;	
};
#define EFUSE_HAL_API_READ	0
#define EFUSE_HAL_API_WRITE 1
extern int meson_trustzone_efuse(struct efuse_hal_api_arg* arg);


//memconfig HAL_API arg
struct memconfig{
	unsigned char name[64];
	unsigned int start_phy_addr;
	unsigned int end_phy_addr;
};
struct memconfig_hal_api_arg{
	unsigned int memconfigbuf_phy_addr;
	unsigned int memconfigbuf_count;
};
#define MEMCONFIG_NUM	2
extern int meson_trustzone_memconfig(void);
extern unsigned int meson_trustzone_getmemsecure_size(void);
extern int meson_trustzone_getmemconfig(unsigned char* name, unsigned int* startphyaddr, unsigned int* endphyaddr);

#endif

#endif
