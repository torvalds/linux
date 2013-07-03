/*
*********************************************************************************************************
*                                                    LINUX-KERNEL
*                                        AllWinner Linux Platform Develop Kits
*                                                   Kernel Module
*
*                                    (c) Copyright 2006-2011, gq.yang China
*                                             All Rights Reserved
*
* File    : mem_timing.h
* By      : 
* Version : v1.0
* Date    : 2012-5-31 14:34
* Descript:
* Update  : date                auther      ver     notes
*********************************************************************************************************
*/
#ifndef __MEM_TIMING_H__
#define __MEM_TIMING_H__

#include "pm_debug.h"
#include "pm_types.h" 

__u32 get_cyclecount (void);
void backup_perfcounter(void);
void init_perfcounters (__u32 do_reset, __u32 enable_divider);
void restore_perfcounter(void);
void reset_counter(void);
void change_runtime_env(__u32 mmu_flag);
void delay_us(__u32 us);
void delay_ms(__u32 ms);

void init_event_counter (__u32 do_reset, __u32 enable_divider);
void set_event_counter(enum counter_type_e type);
int get_event_counter(enum counter_type_e type);

#endif  /* __MEM_TIMING_H__ */
