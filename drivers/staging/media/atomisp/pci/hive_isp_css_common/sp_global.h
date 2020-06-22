/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __SP_GLOBAL_H_INCLUDED__
#define __SP_GLOBAL_H_INCLUDED__

#include <system_types.h>

#if defined(HAS_SP_2401)
#define IS_SP_2401
/* 2401 uses 2400 */
#include <scalar_processor_2400_params.h>
#elif defined(HAS_SP_2400)
#define IS_SP_2400

#include <scalar_processor_2400_params.h>
#else
#error "sp_global.h: SP_2400 must be one of {2400, 2401 }"
#endif

#define SP_PMEM_WIDTH_LOG2		SP_PMEM_LOG_WIDTH_BITS
#define SP_PMEM_SIZE			SP_PMEM_DEPTH

#define SP_DMEM_SIZE			0x4000

/* SP Registers */
#define SP_PC_REG				0x09
#define SP_SC_REG				0x00
#define SP_START_ADDR_REG		0x01
#define SP_ICACHE_ADDR_REG		0x05
#define SP_IRQ_READY_REG		0x00
#define SP_IRQ_CLEAR_REG		0x00
#define SP_ICACHE_INV_REG		0x00
#define SP_CTRL_SINK_REG		0x0A

/* SP Register bits */
#define SP_RST_BIT			0x00
#define SP_START_BIT			0x01
#define SP_BREAK_BIT			0x02
#define SP_RUN_BIT			0x03
#define SP_BROKEN_BIT			0x04
#define SP_IDLE_BIT			0x05     /* READY */
#define SP_SLEEPING_BIT			0x06
#define SP_STALLING_BIT			0x07
#define SP_IRQ_CLEAR_BIT		0x08
#define SP_IRQ_READY_BIT		0x0A
#define SP_IRQ_SLEEPING_BIT		0x0B

#define SP_ICACHE_INV_BIT		0x0C
#define SP_IPREFETCH_EN_BIT		0x0D

#define SP_FIFO0_SINK_BIT		0x00
#define SP_FIFO1_SINK_BIT		0x01
#define SP_FIFO2_SINK_BIT		0x02
#define SP_FIFO3_SINK_BIT		0x03
#define SP_FIFO4_SINK_BIT		0x04
#define SP_FIFO5_SINK_BIT		0x05
#define SP_FIFO6_SINK_BIT		0x06
#define SP_FIFO7_SINK_BIT		0x07
#define SP_FIFO8_SINK_BIT		0x08
#define SP_FIFO9_SINK_BIT		0x09
#define SP_FIFOA_SINK_BIT		0x0A
#define SP_DMEM_SINK_BIT		0x0B
#define SP_CTRL_MT_SINK_BIT		0x0C
#define SP_ICACHE_MT_SINK_BIT	0x0D

#define SP_FIFO0_SINK_REG		0x0A
#define SP_FIFO1_SINK_REG		0x0A
#define SP_FIFO2_SINK_REG		0x0A
#define SP_FIFO3_SINK_REG		0x0A
#define SP_FIFO4_SINK_REG		0x0A
#define SP_FIFO5_SINK_REG		0x0A
#define SP_FIFO6_SINK_REG		0x0A
#define SP_FIFO7_SINK_REG		0x0A
#define SP_FIFO8_SINK_REG		0x0A
#define SP_FIFO9_SINK_REG		0x0A
#define SP_FIFOA_SINK_REG		0x0A
#define SP_DMEM_SINK_REG		0x0A
#define SP_CTRL_MT_SINK_REG		0x0A
#define SP_ICACHE_MT_SINK_REG	0x0A

#endif /* __SP_GLOBAL_H_INCLUDED__ */
