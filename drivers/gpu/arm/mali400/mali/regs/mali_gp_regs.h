/*
 * Copyright (C) 2010, 2012-2017 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _MALIGP2_CONROL_REGS_H_
#define _MALIGP2_CONROL_REGS_H_

/**
 * These are the different geometry processor control registers.
 * Their usage is to control and monitor the operation of the
 * Vertex Shader and the Polygon List Builder in the geometry processor.
 * Addresses are in 32-bit word relative sizes.
 * @see [P0081] "Geometry Processor Data Structures" for details
 */

typedef enum {
	MALIGP2_REG_ADDR_MGMT_VSCL_START_ADDR           = 0x00,
	MALIGP2_REG_ADDR_MGMT_VSCL_END_ADDR             = 0x04,
	MALIGP2_REG_ADDR_MGMT_PLBUCL_START_ADDR         = 0x08,
	MALIGP2_REG_ADDR_MGMT_PLBUCL_END_ADDR           = 0x0c,
	MALIGP2_REG_ADDR_MGMT_PLBU_ALLOC_START_ADDR     = 0x10,
	MALIGP2_REG_ADDR_MGMT_PLBU_ALLOC_END_ADDR       = 0x14,
	MALIGP2_REG_ADDR_MGMT_CMD                       = 0x20,
	MALIGP2_REG_ADDR_MGMT_INT_RAWSTAT               = 0x24,
	MALIGP2_REG_ADDR_MGMT_INT_CLEAR                 = 0x28,
	MALIGP2_REG_ADDR_MGMT_INT_MASK                  = 0x2C,
	MALIGP2_REG_ADDR_MGMT_INT_STAT                  = 0x30,
	MALIGP2_REG_ADDR_MGMT_PERF_CNT_0_ENABLE         = 0x3C,
	MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_ENABLE         = 0x40,
	MALIGP2_REG_ADDR_MGMT_PERF_CNT_0_SRC            = 0x44,
	MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_SRC            = 0x48,
	MALIGP2_REG_ADDR_MGMT_PERF_CNT_0_VALUE          = 0x4C,
	MALIGP2_REG_ADDR_MGMT_PERF_CNT_1_VALUE          = 0x50,
	MALIGP2_REG_ADDR_MGMT_PERF_CNT_0_LIMIT          = 0x54,
	MALIGP2_REG_ADDR_MGMT_STATUS                    = 0x68,
	MALIGP2_REG_ADDR_MGMT_VERSION                   = 0x6C,
	MALIGP2_REG_ADDR_MGMT_VSCL_START_ADDR_READ      = 0x80,
	MALIGP2_REG_ADDR_MGMT_PLBCL_START_ADDR_READ     = 0x84,
	MALIGP2_CONTR_AXI_BUS_ERROR_STAT                = 0x94,
	MALIGP2_REGISTER_ADDRESS_SPACE_SIZE             = 0x98,
} maligp_reg_addr_mgmt_addr;

#define MALIGP2_REG_VAL_PERF_CNT_ENABLE 1

/**
 * Commands to geometry processor.
 *  @see MALIGP2_CTRL_REG_CMD
 */
typedef enum {
	MALIGP2_REG_VAL_CMD_START_VS                    = (1 << 0),
	MALIGP2_REG_VAL_CMD_START_PLBU                  = (1 << 1),
	MALIGP2_REG_VAL_CMD_UPDATE_PLBU_ALLOC   = (1 << 4),
	MALIGP2_REG_VAL_CMD_RESET                               = (1 << 5),
	MALIGP2_REG_VAL_CMD_FORCE_HANG                  = (1 << 6),
	MALIGP2_REG_VAL_CMD_STOP_BUS                    = (1 << 9),
	MALI400GP_REG_VAL_CMD_SOFT_RESET                = (1 << 10), /* only valid for Mali-300 and later */
} mgp_contr_reg_val_cmd;


/**  @defgroup MALIGP2_IRQ
 * Interrupt status of geometry processor.
 *  @see MALIGP2_CTRL_REG_INT_RAWSTAT, MALIGP2_REG_ADDR_MGMT_INT_CLEAR,
 *       MALIGP2_REG_ADDR_MGMT_INT_MASK, MALIGP2_REG_ADDR_MGMT_INT_STAT
 * @{
 */
#define MALIGP2_REG_VAL_IRQ_VS_END_CMD_LST      (1 << 0)
#define MALIGP2_REG_VAL_IRQ_PLBU_END_CMD_LST    (1 << 1)
#define MALIGP2_REG_VAL_IRQ_PLBU_OUT_OF_MEM     (1 << 2)
#define MALIGP2_REG_VAL_IRQ_VS_SEM_IRQ          (1 << 3)
#define MALIGP2_REG_VAL_IRQ_PLBU_SEM_IRQ        (1 << 4)
#define MALIGP2_REG_VAL_IRQ_HANG                (1 << 5)
#define MALIGP2_REG_VAL_IRQ_FORCE_HANG          (1 << 6)
#define MALIGP2_REG_VAL_IRQ_PERF_CNT_0_LIMIT    (1 << 7)
#define MALIGP2_REG_VAL_IRQ_PERF_CNT_1_LIMIT    (1 << 8)
#define MALIGP2_REG_VAL_IRQ_WRITE_BOUND_ERR     (1 << 9)
#define MALIGP2_REG_VAL_IRQ_SYNC_ERROR          (1 << 10)
#define MALIGP2_REG_VAL_IRQ_AXI_BUS_ERROR       (1 << 11)
#define MALI400GP_REG_VAL_IRQ_AXI_BUS_STOPPED     (1 << 12)
#define MALI400GP_REG_VAL_IRQ_VS_INVALID_CMD      (1 << 13)
#define MALI400GP_REG_VAL_IRQ_PLB_INVALID_CMD     (1 << 14)
#define MALI400GP_REG_VAL_IRQ_RESET_COMPLETED     (1 << 19)
#define MALI400GP_REG_VAL_IRQ_SEMAPHORE_UNDERFLOW (1 << 20)
#define MALI400GP_REG_VAL_IRQ_SEMAPHORE_OVERFLOW  (1 << 21)
#define MALI400GP_REG_VAL_IRQ_PTR_ARRAY_OUT_OF_BOUNDS  (1 << 22)

/* Mask defining all IRQs in Mali GP */
#define MALIGP2_REG_VAL_IRQ_MASK_ALL \
	(\
	 MALIGP2_REG_VAL_IRQ_VS_END_CMD_LST      | \
	 MALIGP2_REG_VAL_IRQ_PLBU_END_CMD_LST    | \
	 MALIGP2_REG_VAL_IRQ_PLBU_OUT_OF_MEM     | \
	 MALIGP2_REG_VAL_IRQ_VS_SEM_IRQ          | \
	 MALIGP2_REG_VAL_IRQ_PLBU_SEM_IRQ        | \
	 MALIGP2_REG_VAL_IRQ_HANG                | \
	 MALIGP2_REG_VAL_IRQ_FORCE_HANG          | \
	 MALIGP2_REG_VAL_IRQ_PERF_CNT_0_LIMIT    | \
	 MALIGP2_REG_VAL_IRQ_PERF_CNT_1_LIMIT    | \
	 MALIGP2_REG_VAL_IRQ_WRITE_BOUND_ERR     | \
	 MALIGP2_REG_VAL_IRQ_SYNC_ERROR          | \
	 MALIGP2_REG_VAL_IRQ_AXI_BUS_ERROR       | \
	 MALI400GP_REG_VAL_IRQ_AXI_BUS_STOPPED     | \
	 MALI400GP_REG_VAL_IRQ_VS_INVALID_CMD      | \
	 MALI400GP_REG_VAL_IRQ_PLB_INVALID_CMD     | \
	 MALI400GP_REG_VAL_IRQ_RESET_COMPLETED     | \
	 MALI400GP_REG_VAL_IRQ_SEMAPHORE_UNDERFLOW | \
	 MALI400GP_REG_VAL_IRQ_SEMAPHORE_OVERFLOW  | \
	 MALI400GP_REG_VAL_IRQ_PTR_ARRAY_OUT_OF_BOUNDS)

/* Mask defining the IRQs in Mali GP which we use */
#define MALIGP2_REG_VAL_IRQ_MASK_USED \
	(\
	 MALIGP2_REG_VAL_IRQ_VS_END_CMD_LST      | \
	 MALIGP2_REG_VAL_IRQ_PLBU_END_CMD_LST    | \
	 MALIGP2_REG_VAL_IRQ_PLBU_OUT_OF_MEM     | \
	 MALIGP2_REG_VAL_IRQ_FORCE_HANG          | \
	 MALIGP2_REG_VAL_IRQ_WRITE_BOUND_ERR     | \
	 MALIGP2_REG_VAL_IRQ_SYNC_ERROR          | \
	 MALIGP2_REG_VAL_IRQ_AXI_BUS_ERROR       | \
	 MALI400GP_REG_VAL_IRQ_VS_INVALID_CMD      | \
	 MALI400GP_REG_VAL_IRQ_PLB_INVALID_CMD     | \
	 MALI400GP_REG_VAL_IRQ_SEMAPHORE_UNDERFLOW | \
	 MALI400GP_REG_VAL_IRQ_SEMAPHORE_OVERFLOW  | \
	 MALI400GP_REG_VAL_IRQ_PTR_ARRAY_OUT_OF_BOUNDS)

/* Mask defining non IRQs on MaliGP2*/
#define MALIGP2_REG_VAL_IRQ_MASK_NONE 0

/** }@ defgroup MALIGP2_IRQ*/

/** @defgroup MALIGP2_STATUS
 * The different Status values to the geometry processor.
 *  @see MALIGP2_CTRL_REG_STATUS
 * @{
 */
#define MALIGP2_REG_VAL_STATUS_VS_ACTIVE         0x0002
#define MALIGP2_REG_VAL_STATUS_BUS_STOPPED       0x0004
#define MALIGP2_REG_VAL_STATUS_PLBU_ACTIVE       0x0008
#define MALIGP2_REG_VAL_STATUS_BUS_ERROR         0x0040
#define MALIGP2_REG_VAL_STATUS_WRITE_BOUND_ERR   0x0100
/** }@ defgroup MALIGP2_STATUS*/

#define MALIGP2_REG_VAL_STATUS_MASK_ACTIVE (\
		MALIGP2_REG_VAL_STATUS_VS_ACTIVE|\
		MALIGP2_REG_VAL_STATUS_PLBU_ACTIVE)


#define MALIGP2_REG_VAL_STATUS_MASK_ERROR (\
		MALIGP2_REG_VAL_STATUS_BUS_ERROR |\
		MALIGP2_REG_VAL_STATUS_WRITE_BOUND_ERR )

/* This should be in the top 16 bit of the version register of gp.*/
#define MALI200_GP_PRODUCT_ID 0xA07
#define MALI300_GP_PRODUCT_ID 0xC07
#define MALI400_GP_PRODUCT_ID 0xB07
#define MALI450_GP_PRODUCT_ID 0xD07

/**
 * The different sources for instrumented on the geometry processor.
 *  @see MALIGP2_REG_ADDR_MGMT_PERF_CNT_0_SRC
 */

enum MALIGP2_cont_reg_perf_cnt_src {
	MALIGP2_REG_VAL_PERF_CNT1_SRC_NUMBER_OF_VERTICES_PROCESSED = 0x0a,
};

#endif
