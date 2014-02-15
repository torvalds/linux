/*
 * Copyright (C) 2010, 2012-2013 ARM Limited. All rights reserved.
 * 
 * This program is free software and is provided to you under the terms of the GNU General Public License version 2
 * as published by the Free Software Foundation, and any use by you of this program is subject to the terms of such GNU licence.
 * 
 * A copy of the licence is included with the program, and can also be obtained from Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#ifndef _MALI200_REGS_H_
#define _MALI200_REGS_H_

/**
 *  Enum for management register addresses.
 */
enum mali200_mgmt_reg {
	MALI200_REG_ADDR_MGMT_VERSION                              = 0x1000,
	MALI200_REG_ADDR_MGMT_CURRENT_REND_LIST_ADDR               = 0x1004,
	MALI200_REG_ADDR_MGMT_STATUS                               = 0x1008,
	MALI200_REG_ADDR_MGMT_CTRL_MGMT                            = 0x100c,

	MALI200_REG_ADDR_MGMT_INT_RAWSTAT                          = 0x1020,
	MALI200_REG_ADDR_MGMT_INT_CLEAR                            = 0x1024,
	MALI200_REG_ADDR_MGMT_INT_MASK                             = 0x1028,
	MALI200_REG_ADDR_MGMT_INT_STATUS                           = 0x102c,

	MALI200_REG_ADDR_MGMT_WRITE_BOUNDARY_LOW                   = 0x1044,

	MALI200_REG_ADDR_MGMT_BUS_ERROR_STATUS                     = 0x1050,

	MALI200_REG_ADDR_MGMT_PERF_CNT_0_ENABLE                    = 0x1080,
	MALI200_REG_ADDR_MGMT_PERF_CNT_0_SRC                       = 0x1084,
	MALI200_REG_ADDR_MGMT_PERF_CNT_0_VALUE                     = 0x108c,

	MALI200_REG_ADDR_MGMT_PERF_CNT_1_ENABLE                    = 0x10a0,
	MALI200_REG_ADDR_MGMT_PERF_CNT_1_SRC                       = 0x10a4,
	MALI200_REG_ADDR_MGMT_PERF_CNT_1_VALUE                     = 0x10ac,

	MALI200_REG_ADDR_MGMT_PERFMON_CONTR                        = 0x10b0,
	MALI200_REG_ADDR_MGMT_PERFMON_BASE                         = 0x10b4,

	MALI200_REG_SIZEOF_REGISTER_BANK                           = 0x10f0

};

#define MALI200_REG_VAL_PERF_CNT_ENABLE 1

enum mali200_mgmt_ctrl_mgmt {
	MALI200_REG_VAL_CTRL_MGMT_STOP_BUS         = (1<<0),
	MALI200_REG_VAL_CTRL_MGMT_FLUSH_CACHES     = (1<<3),
	MALI200_REG_VAL_CTRL_MGMT_FORCE_RESET      = (1<<5),
	MALI200_REG_VAL_CTRL_MGMT_START_RENDERING  = (1<<6),
	MALI400PP_REG_VAL_CTRL_MGMT_SOFT_RESET     = (1<<7), /* Only valid for Mali-300 and later */
};

enum mali200_mgmt_irq {
	MALI200_REG_VAL_IRQ_END_OF_FRAME          = (1<<0),
	MALI200_REG_VAL_IRQ_END_OF_TILE           = (1<<1),
	MALI200_REG_VAL_IRQ_HANG                  = (1<<2),
	MALI200_REG_VAL_IRQ_FORCE_HANG            = (1<<3),
	MALI200_REG_VAL_IRQ_BUS_ERROR             = (1<<4),
	MALI200_REG_VAL_IRQ_BUS_STOP              = (1<<5),
	MALI200_REG_VAL_IRQ_CNT_0_LIMIT           = (1<<6),
	MALI200_REG_VAL_IRQ_CNT_1_LIMIT           = (1<<7),
	MALI200_REG_VAL_IRQ_WRITE_BOUNDARY_ERROR  = (1<<8),
	MALI400PP_REG_VAL_IRQ_INVALID_PLIST_COMMAND = (1<<9),
	MALI400PP_REG_VAL_IRQ_CALL_STACK_UNDERFLOW  = (1<<10),
	MALI400PP_REG_VAL_IRQ_CALL_STACK_OVERFLOW   = (1<<11),
	MALI400PP_REG_VAL_IRQ_RESET_COMPLETED       = (1<<12),
};

#define MALI200_REG_VAL_IRQ_MASK_ALL  ((enum mali200_mgmt_irq) (\
    MALI200_REG_VAL_IRQ_END_OF_FRAME                           |\
    MALI200_REG_VAL_IRQ_END_OF_TILE                            |\
    MALI200_REG_VAL_IRQ_HANG                                   |\
    MALI200_REG_VAL_IRQ_FORCE_HANG                             |\
    MALI200_REG_VAL_IRQ_BUS_ERROR                              |\
    MALI200_REG_VAL_IRQ_BUS_STOP                               |\
    MALI200_REG_VAL_IRQ_CNT_0_LIMIT                            |\
    MALI200_REG_VAL_IRQ_CNT_1_LIMIT                            |\
    MALI200_REG_VAL_IRQ_WRITE_BOUNDARY_ERROR                   |\
    MALI400PP_REG_VAL_IRQ_INVALID_PLIST_COMMAND                  |\
    MALI400PP_REG_VAL_IRQ_CALL_STACK_UNDERFLOW                   |\
    MALI400PP_REG_VAL_IRQ_CALL_STACK_OVERFLOW                    |\
    MALI400PP_REG_VAL_IRQ_RESET_COMPLETED))

#define MALI200_REG_VAL_IRQ_MASK_USED ((enum mali200_mgmt_irq) (\
    MALI200_REG_VAL_IRQ_END_OF_FRAME                           |\
    MALI200_REG_VAL_IRQ_FORCE_HANG                             |\
    MALI200_REG_VAL_IRQ_BUS_ERROR                              |\
    MALI200_REG_VAL_IRQ_WRITE_BOUNDARY_ERROR                   |\
    MALI400PP_REG_VAL_IRQ_INVALID_PLIST_COMMAND                  |\
    MALI400PP_REG_VAL_IRQ_CALL_STACK_UNDERFLOW                   |\
    MALI400PP_REG_VAL_IRQ_CALL_STACK_OVERFLOW))

#define MALI200_REG_VAL_IRQ_MASK_NONE ((enum mali200_mgmt_irq)(0))

enum mali200_mgmt_status {
	MALI200_REG_VAL_STATUS_RENDERING_ACTIVE     = (1<<0),
	MALI200_REG_VAL_STATUS_BUS_STOPPED          = (1<<4),
};

enum mali200_render_unit {
	MALI200_REG_ADDR_FRAME = 0x0000,
	MALI200_REG_ADDR_RSW   = 0x0004,
	MALI200_REG_ADDR_STACK = 0x0030,
	MALI200_REG_ADDR_STACK_SIZE = 0x0034,
	MALI200_REG_ADDR_ORIGIN_OFFSET_X  = 0x0040
};

enum mali200_wb_unit {
	MALI200_REG_ADDR_WB0 = 0x0100,
	MALI200_REG_ADDR_WB1 = 0x0200,
	MALI200_REG_ADDR_WB2 = 0x0300
};

enum mali200_wb_unit_regs {
	MALI200_REG_ADDR_WB_SOURCE_SELECT = 0x0000,
	MALI200_REG_ADDR_WB_SOURCE_ADDR   = 0x0004,
};

/* This should be in the top 16 bit of the version register of Mali PP */
#define MALI200_PP_PRODUCT_ID 0xC807
#define MALI300_PP_PRODUCT_ID 0xCE07
#define MALI400_PP_PRODUCT_ID 0xCD07
#define MALI450_PP_PRODUCT_ID 0xCF07


#endif /* _MALI200_REGS_H_ */
