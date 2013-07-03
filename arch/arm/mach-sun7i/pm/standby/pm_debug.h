#ifndef _PM_DEBUG_H
#define _PM_DEBUG_H

#include "pm_config.h"
/*
 * Copyright (c) 2011-2015 yanggq.young@allwinnertech.com
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 */
 
#ifdef CONFIG_ARCH_SUN4I 
	#define PERMANENT_REG (0xf1c20d20)
	#define PERMANENT_REG_PA (0x01c20d20)
	#define STATUS_REG (0xf1c20d20)
	#define STATUS_REG_PA (0x01c20d20)
#elif defined(CONFIG_ARCH_SUN5I)
	#define PERMANENT_REG (0xF1c0123c)
	#define PERMANENT_REG_PA (0x01c0123c)
	#define STATUS_REG (0xf0000740)
	#define STATUS_REG_PA (0x00000740)
	//notice: the address is located in the last word of (DRAM_BACKUP_BASE_ADDR + DRAM_BACKUP_SIZE)
	#define SUN5I_STATUS_REG (DRAM_BACKUP_BASE_ADDR + (DRAM_BACKUP_SIZE<<2) -0x4)
	#define SUN5I_STATUS_REG_PA (DRAM_BACKUP_BASE_ADDR_PA + (DRAM_BACKUP_SIZE<<2) -0x4)
#elif defined(CONFIG_ARCH_SUN6I)
	#define STATUS_REG (0xf1f00100)
	#define STATUS_REG_PA (0x01f00100)

#endif



//#define GET_CYCLE_CNT
//#define IO_MEASURE
extern volatile int print_flag;

enum counter_type_e{
	I_CACHE_MISS = 0X01,
	I_TLB_MISS = 0X02,
	D_CACHE_MISS = 0X03,
	D_TLB_MISS = 0X05,
};

void set_event_counter(enum counter_type_e type);
int get_event_counter(enum counter_type_e type);
void init_event_counter (__u32 do_reset, __u32 enable_divider);

/*
 * Check at compile time that something is of a particular type.
 * Always evaluates to 1 so you may use it easily in comparisons.
 */
#define typecheck(type,x) \
({	type __dummy; \
	typeof(x) __dummy2; \
	(void)(&__dummy == &__dummy2); \
	1; \
})

/*
 * if return true, means a is after b;
 */	
#define counter_after(a,b) \
(typecheck(__u32, a) && \
typecheck(__u32, b) && \
((__s32)(b) - (__s32)(a) < 0))
#define counter_before(a,b) counter_after(b,a)

#define counter_after_eq(a,b) \
(typecheck(__u32, a) && \
typecheck(__u32, b) && \
((__s32)(a) - (__s32)(b) >= 0))
#define counter_before_eq(a,b) counter_after_eq(b,a)


void busy_waiting(void);
/*
 * notice: when resume, boot0 need to clear the flag, 
 * in case the data in dram be destoryed result in the system is re-resume in cycle.
*/
void save_mem_flag(void);
void clear_mem_flag(void);
void save_mem_status(volatile __u32 val);
void save_mem_status_nommu(volatile __u32 val);
__u32 get_mem_status(void);
__u32 save_sun5i_mem_status_nommu(volatile __u32 val);
__u32 save_sun5i_mem_status(volatile __u32 val);

void io_init(void);
void io_init_high(void);
void io_init_low(void);
void io_high(int num);


void standby_enable_crc(int enabled);
void standby_set_crc(void *addr, int size);
unsigned int standby_get_crc(void);
void standby_dram_crc(int flag);


#endif /*_PM_DEBUG_H*/

