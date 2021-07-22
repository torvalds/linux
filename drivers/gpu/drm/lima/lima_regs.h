/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright 2010-2017 ARM Limited. All rights reserved.
 * Copyright 2017-2019 Qiang Yu <yuq825@gmail.com>
 */

#ifndef __LIMA_REGS_H__
#define __LIMA_REGS_H__

/* This file's register definition is collected from the
 * official ARM Mali Utgard GPU kernel driver source code
 */

/* PMU regs */
#define LIMA_PMU_POWER_UP                  0x00
#define LIMA_PMU_POWER_DOWN                0x04
#define   LIMA_PMU_POWER_GP0_MASK          BIT(0)
#define   LIMA_PMU_POWER_L2_MASK           BIT(1)
#define   LIMA_PMU_POWER_PP_MASK(i)        BIT(2 + i)

/*
 * On Mali450 each block automatically starts up its corresponding L2
 * and the PPs are not fully independent controllable.
 * Instead PP0, PP1-3 and PP4-7 can be turned on or off.
 */
#define   LIMA450_PMU_POWER_PP0_MASK       BIT(1)
#define   LIMA450_PMU_POWER_PP13_MASK      BIT(2)
#define   LIMA450_PMU_POWER_PP47_MASK      BIT(3)

#define LIMA_PMU_STATUS                    0x08
#define LIMA_PMU_INT_MASK                  0x0C
#define LIMA_PMU_INT_RAWSTAT               0x10
#define LIMA_PMU_INT_CLEAR                 0x18
#define   LIMA_PMU_INT_CMD_MASK            BIT(0)
#define LIMA_PMU_SW_DELAY                  0x1C

/* L2 cache regs */
#define LIMA_L2_CACHE_SIZE                   0x0004
#define LIMA_L2_CACHE_STATUS                 0x0008
#define   LIMA_L2_CACHE_STATUS_COMMAND_BUSY  BIT(0)
#define   LIMA_L2_CACHE_STATUS_DATA_BUSY     BIT(1)
#define LIMA_L2_CACHE_COMMAND                0x0010
#define   LIMA_L2_CACHE_COMMAND_CLEAR_ALL    BIT(0)
#define LIMA_L2_CACHE_CLEAR_PAGE             0x0014
#define LIMA_L2_CACHE_MAX_READS              0x0018
#define LIMA_L2_CACHE_ENABLE                 0x001C
#define   LIMA_L2_CACHE_ENABLE_ACCESS        BIT(0)
#define   LIMA_L2_CACHE_ENABLE_READ_ALLOCATE BIT(1)
#define LIMA_L2_CACHE_PERFCNT_SRC0           0x0020
#define LIMA_L2_CACHE_PERFCNT_VAL0           0x0024
#define LIMA_L2_CACHE_PERFCNT_SRC1           0x0028
#define LIMA_L2_CACHE_ERFCNT_VAL1            0x002C

/* GP regs */
#define LIMA_GP_VSCL_START_ADDR                0x00
#define LIMA_GP_VSCL_END_ADDR                  0x04
#define LIMA_GP_PLBUCL_START_ADDR              0x08
#define LIMA_GP_PLBUCL_END_ADDR                0x0c
#define LIMA_GP_PLBU_ALLOC_START_ADDR          0x10
#define LIMA_GP_PLBU_ALLOC_END_ADDR            0x14
#define LIMA_GP_CMD                            0x20
#define   LIMA_GP_CMD_START_VS                 BIT(0)
#define   LIMA_GP_CMD_START_PLBU               BIT(1)
#define   LIMA_GP_CMD_UPDATE_PLBU_ALLOC        BIT(4)
#define   LIMA_GP_CMD_RESET                    BIT(5)
#define   LIMA_GP_CMD_FORCE_HANG               BIT(6)
#define   LIMA_GP_CMD_STOP_BUS                 BIT(9)
#define   LIMA_GP_CMD_SOFT_RESET               BIT(10)
#define LIMA_GP_INT_RAWSTAT                    0x24
#define LIMA_GP_INT_CLEAR                      0x28
#define LIMA_GP_INT_MASK                       0x2C
#define LIMA_GP_INT_STAT                       0x30
#define   LIMA_GP_IRQ_VS_END_CMD_LST           BIT(0)
#define   LIMA_GP_IRQ_PLBU_END_CMD_LST         BIT(1)
#define   LIMA_GP_IRQ_PLBU_OUT_OF_MEM          BIT(2)
#define   LIMA_GP_IRQ_VS_SEM_IRQ               BIT(3)
#define   LIMA_GP_IRQ_PLBU_SEM_IRQ             BIT(4)
#define   LIMA_GP_IRQ_HANG                     BIT(5)
#define   LIMA_GP_IRQ_FORCE_HANG               BIT(6)
#define   LIMA_GP_IRQ_PERF_CNT_0_LIMIT         BIT(7)
#define   LIMA_GP_IRQ_PERF_CNT_1_LIMIT         BIT(8)
#define   LIMA_GP_IRQ_WRITE_BOUND_ERR          BIT(9)
#define   LIMA_GP_IRQ_SYNC_ERROR               BIT(10)
#define   LIMA_GP_IRQ_AXI_BUS_ERROR            BIT(11)
#define   LIMA_GP_IRQ_AXI_BUS_STOPPED          BIT(12)
#define   LIMA_GP_IRQ_VS_INVALID_CMD           BIT(13)
#define   LIMA_GP_IRQ_PLB_INVALID_CMD          BIT(14)
#define   LIMA_GP_IRQ_RESET_COMPLETED          BIT(19)
#define   LIMA_GP_IRQ_SEMAPHORE_UNDERFLOW      BIT(20)
#define   LIMA_GP_IRQ_SEMAPHORE_OVERFLOW       BIT(21)
#define   LIMA_GP_IRQ_PTR_ARRAY_OUT_OF_BOUNDS  BIT(22)
#define LIMA_GP_WRITE_BOUND_LOW                0x34
#define LIMA_GP_PERF_CNT_0_ENABLE              0x3C
#define LIMA_GP_PERF_CNT_1_ENABLE              0x40
#define LIMA_GP_PERF_CNT_0_SRC                 0x44
#define LIMA_GP_PERF_CNT_1_SRC                 0x48
#define LIMA_GP_PERF_CNT_0_VALUE               0x4C
#define LIMA_GP_PERF_CNT_1_VALUE               0x50
#define LIMA_GP_PERF_CNT_0_LIMIT               0x54
#define LIMA_GP_STATUS                         0x68
#define   LIMA_GP_STATUS_VS_ACTIVE             BIT(1)
#define   LIMA_GP_STATUS_BUS_STOPPED           BIT(2)
#define   LIMA_GP_STATUS_PLBU_ACTIVE           BIT(3)
#define   LIMA_GP_STATUS_BUS_ERROR             BIT(6)
#define   LIMA_GP_STATUS_WRITE_BOUND_ERR       BIT(8)
#define LIMA_GP_VERSION                        0x6C
#define LIMA_GP_VSCL_START_ADDR_READ           0x80
#define LIMA_GP_PLBCL_START_ADDR_READ          0x84
#define LIMA_GP_CONTR_AXI_BUS_ERROR_STAT       0x94

#define LIMA_GP_IRQ_MASK_ALL		   \
	(				   \
	 LIMA_GP_IRQ_VS_END_CMD_LST      | \
	 LIMA_GP_IRQ_PLBU_END_CMD_LST    | \
	 LIMA_GP_IRQ_PLBU_OUT_OF_MEM     | \
	 LIMA_GP_IRQ_VS_SEM_IRQ          | \
	 LIMA_GP_IRQ_PLBU_SEM_IRQ        | \
	 LIMA_GP_IRQ_HANG                | \
	 LIMA_GP_IRQ_FORCE_HANG          | \
	 LIMA_GP_IRQ_PERF_CNT_0_LIMIT    | \
	 LIMA_GP_IRQ_PERF_CNT_1_LIMIT    | \
	 LIMA_GP_IRQ_WRITE_BOUND_ERR     | \
	 LIMA_GP_IRQ_SYNC_ERROR          | \
	 LIMA_GP_IRQ_AXI_BUS_ERROR       | \
	 LIMA_GP_IRQ_AXI_BUS_STOPPED     | \
	 LIMA_GP_IRQ_VS_INVALID_CMD      | \
	 LIMA_GP_IRQ_PLB_INVALID_CMD     | \
	 LIMA_GP_IRQ_RESET_COMPLETED     | \
	 LIMA_GP_IRQ_SEMAPHORE_UNDERFLOW | \
	 LIMA_GP_IRQ_SEMAPHORE_OVERFLOW  | \
	 LIMA_GP_IRQ_PTR_ARRAY_OUT_OF_BOUNDS)

#define LIMA_GP_IRQ_MASK_ERROR             \
	(                                  \
	 LIMA_GP_IRQ_PLBU_OUT_OF_MEM     | \
	 LIMA_GP_IRQ_FORCE_HANG          | \
	 LIMA_GP_IRQ_WRITE_BOUND_ERR     | \
	 LIMA_GP_IRQ_SYNC_ERROR          | \
	 LIMA_GP_IRQ_AXI_BUS_ERROR       | \
	 LIMA_GP_IRQ_VS_INVALID_CMD      | \
	 LIMA_GP_IRQ_PLB_INVALID_CMD     | \
	 LIMA_GP_IRQ_SEMAPHORE_UNDERFLOW | \
	 LIMA_GP_IRQ_SEMAPHORE_OVERFLOW  | \
	 LIMA_GP_IRQ_PTR_ARRAY_OUT_OF_BOUNDS)

#define LIMA_GP_IRQ_MASK_USED		   \
	(				   \
	 LIMA_GP_IRQ_VS_END_CMD_LST      | \
	 LIMA_GP_IRQ_PLBU_END_CMD_LST    | \
	 LIMA_GP_IRQ_MASK_ERROR)

/* PP regs */
#define LIMA_PP_FRAME                        0x0000
#define LIMA_PP_RSW			     0x0004
#define LIMA_PP_STACK			     0x0030
#define LIMA_PP_STACK_SIZE		     0x0034
#define LIMA_PP_ORIGIN_OFFSET_X	             0x0040
#define LIMA_PP_WB(i)                        (0x0100 * (i + 1))
#define   LIMA_PP_WB_SOURCE_SELECT           0x0000
#define	  LIMA_PP_WB_SOURCE_ADDR             0x0004

#define LIMA_PP_VERSION                      0x1000
#define LIMA_PP_CURRENT_REND_LIST_ADDR       0x1004
#define LIMA_PP_STATUS                       0x1008
#define   LIMA_PP_STATUS_RENDERING_ACTIVE    BIT(0)
#define   LIMA_PP_STATUS_BUS_STOPPED         BIT(4)
#define LIMA_PP_CTRL                         0x100c
#define   LIMA_PP_CTRL_STOP_BUS              BIT(0)
#define   LIMA_PP_CTRL_FLUSH_CACHES          BIT(3)
#define   LIMA_PP_CTRL_FORCE_RESET           BIT(5)
#define   LIMA_PP_CTRL_START_RENDERING       BIT(6)
#define   LIMA_PP_CTRL_SOFT_RESET            BIT(7)
#define LIMA_PP_INT_RAWSTAT                  0x1020
#define LIMA_PP_INT_CLEAR                    0x1024
#define LIMA_PP_INT_MASK                     0x1028
#define LIMA_PP_INT_STATUS                   0x102c
#define   LIMA_PP_IRQ_END_OF_FRAME           BIT(0)
#define   LIMA_PP_IRQ_END_OF_TILE            BIT(1)
#define   LIMA_PP_IRQ_HANG                   BIT(2)
#define   LIMA_PP_IRQ_FORCE_HANG             BIT(3)
#define   LIMA_PP_IRQ_BUS_ERROR              BIT(4)
#define   LIMA_PP_IRQ_BUS_STOP               BIT(5)
#define   LIMA_PP_IRQ_CNT_0_LIMIT            BIT(6)
#define   LIMA_PP_IRQ_CNT_1_LIMIT            BIT(7)
#define   LIMA_PP_IRQ_WRITE_BOUNDARY_ERROR   BIT(8)
#define   LIMA_PP_IRQ_INVALID_PLIST_COMMAND  BIT(9)
#define   LIMA_PP_IRQ_CALL_STACK_UNDERFLOW   BIT(10)
#define   LIMA_PP_IRQ_CALL_STACK_OVERFLOW    BIT(11)
#define   LIMA_PP_IRQ_RESET_COMPLETED        BIT(12)
#define LIMA_PP_WRITE_BOUNDARY_LOW           0x1044
#define LIMA_PP_BUS_ERROR_STATUS             0x1050
#define LIMA_PP_PERF_CNT_0_ENABLE            0x1080
#define LIMA_PP_PERF_CNT_0_SRC               0x1084
#define LIMA_PP_PERF_CNT_0_LIMIT             0x1088
#define LIMA_PP_PERF_CNT_0_VALUE             0x108c
#define LIMA_PP_PERF_CNT_1_ENABLE            0x10a0
#define LIMA_PP_PERF_CNT_1_SRC               0x10a4
#define LIMA_PP_PERF_CNT_1_LIMIT             0x10a8
#define LIMA_PP_PERF_CNT_1_VALUE             0x10ac
#define LIMA_PP_PERFMON_CONTR                0x10b0
#define LIMA_PP_PERFMON_BASE                 0x10b4

#define LIMA_PP_IRQ_MASK_ALL                 \
	(                                    \
	 LIMA_PP_IRQ_END_OF_FRAME          | \
	 LIMA_PP_IRQ_END_OF_TILE           | \
	 LIMA_PP_IRQ_HANG                  | \
	 LIMA_PP_IRQ_FORCE_HANG            | \
	 LIMA_PP_IRQ_BUS_ERROR             | \
	 LIMA_PP_IRQ_BUS_STOP              | \
	 LIMA_PP_IRQ_CNT_0_LIMIT           | \
	 LIMA_PP_IRQ_CNT_1_LIMIT           | \
	 LIMA_PP_IRQ_WRITE_BOUNDARY_ERROR  | \
	 LIMA_PP_IRQ_INVALID_PLIST_COMMAND | \
	 LIMA_PP_IRQ_CALL_STACK_UNDERFLOW  | \
	 LIMA_PP_IRQ_CALL_STACK_OVERFLOW   | \
	 LIMA_PP_IRQ_RESET_COMPLETED)

#define LIMA_PP_IRQ_MASK_ERROR               \
	(                                    \
	 LIMA_PP_IRQ_FORCE_HANG            | \
	 LIMA_PP_IRQ_BUS_ERROR             | \
	 LIMA_PP_IRQ_WRITE_BOUNDARY_ERROR  | \
	 LIMA_PP_IRQ_INVALID_PLIST_COMMAND | \
	 LIMA_PP_IRQ_CALL_STACK_UNDERFLOW  | \
	 LIMA_PP_IRQ_CALL_STACK_OVERFLOW)

#define LIMA_PP_IRQ_MASK_USED                \
	(                                    \
	 LIMA_PP_IRQ_END_OF_FRAME          | \
	 LIMA_PP_IRQ_MASK_ERROR)

/* MMU regs */
#define LIMA_MMU_DTE_ADDR                     0x0000
#define LIMA_MMU_STATUS                       0x0004
#define   LIMA_MMU_STATUS_PAGING_ENABLED      BIT(0)
#define   LIMA_MMU_STATUS_PAGE_FAULT_ACTIVE   BIT(1)
#define   LIMA_MMU_STATUS_STALL_ACTIVE        BIT(2)
#define   LIMA_MMU_STATUS_IDLE                BIT(3)
#define   LIMA_MMU_STATUS_REPLAY_BUFFER_EMPTY BIT(4)
#define   LIMA_MMU_STATUS_PAGE_FAULT_IS_WRITE BIT(5)
#define   LIMA_MMU_STATUS_BUS_ID(x)           ((x >> 6) & 0x1F)
#define   LIMA_MMU_STATUS_STALL_NOT_ACTIVE    BIT(31)
#define LIMA_MMU_COMMAND                      0x0008
#define   LIMA_MMU_COMMAND_ENABLE_PAGING      0x00
#define   LIMA_MMU_COMMAND_DISABLE_PAGING     0x01
#define   LIMA_MMU_COMMAND_ENABLE_STALL       0x02
#define   LIMA_MMU_COMMAND_DISABLE_STALL      0x03
#define   LIMA_MMU_COMMAND_ZAP_CACHE          0x04
#define   LIMA_MMU_COMMAND_PAGE_FAULT_DONE    0x05
#define   LIMA_MMU_COMMAND_HARD_RESET         0x06
#define LIMA_MMU_PAGE_FAULT_ADDR              0x000C
#define LIMA_MMU_ZAP_ONE_LINE                 0x0010
#define LIMA_MMU_INT_RAWSTAT                  0x0014
#define LIMA_MMU_INT_CLEAR                    0x0018
#define LIMA_MMU_INT_MASK                     0x001C
#define   LIMA_MMU_INT_PAGE_FAULT             BIT(0)
#define   LIMA_MMU_INT_READ_BUS_ERROR         BIT(1)
#define LIMA_MMU_INT_STATUS                   0x0020

#define LIMA_VM_FLAG_PRESENT          BIT(0)
#define LIMA_VM_FLAG_READ_PERMISSION  BIT(1)
#define LIMA_VM_FLAG_WRITE_PERMISSION BIT(2)
#define LIMA_VM_FLAG_OVERRIDE_CACHE   BIT(3)
#define LIMA_VM_FLAG_WRITE_CACHEABLE  BIT(4)
#define LIMA_VM_FLAG_WRITE_ALLOCATE   BIT(5)
#define LIMA_VM_FLAG_WRITE_BUFFERABLE BIT(6)
#define LIMA_VM_FLAG_READ_CACHEABLE   BIT(7)
#define LIMA_VM_FLAG_READ_ALLOCATE    BIT(8)
#define LIMA_VM_FLAG_MASK             0x1FF

#define LIMA_VM_FLAGS_CACHE (			 \
		LIMA_VM_FLAG_PRESENT |		 \
		LIMA_VM_FLAG_READ_PERMISSION |	 \
		LIMA_VM_FLAG_WRITE_PERMISSION |	 \
		LIMA_VM_FLAG_OVERRIDE_CACHE |	 \
		LIMA_VM_FLAG_WRITE_CACHEABLE |	 \
		LIMA_VM_FLAG_WRITE_BUFFERABLE |	 \
		LIMA_VM_FLAG_READ_CACHEABLE |	 \
		LIMA_VM_FLAG_READ_ALLOCATE)

#define LIMA_VM_FLAGS_UNCACHE (			\
		LIMA_VM_FLAG_PRESENT |		\
		LIMA_VM_FLAG_READ_PERMISSION |	\
		LIMA_VM_FLAG_WRITE_PERMISSION)

/* DLBU regs */
#define LIMA_DLBU_MASTER_TLLIST_PHYS_ADDR  0x0000
#define	LIMA_DLBU_MASTER_TLLIST_VADDR      0x0004
#define	LIMA_DLBU_TLLIST_VBASEADDR         0x0008
#define	LIMA_DLBU_FB_DIM                   0x000C
#define	LIMA_DLBU_TLLIST_CONF              0x0010
#define	LIMA_DLBU_START_TILE_POS           0x0014
#define	LIMA_DLBU_PP_ENABLE_MASK           0x0018

/* BCAST regs */
#define LIMA_BCAST_BROADCAST_MASK    0x0
#define LIMA_BCAST_INTERRUPT_MASK    0x4

#endif
