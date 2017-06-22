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

#ifndef __ISP_GLOBAL_H_INCLUDED__
#define __ISP_GLOBAL_H_INCLUDED__

#include <system_types.h>

#if defined (HAS_ISP_2401_MAMOIADA)
#define IS_ISP_2401_MAMOIADA

#include "isp2401_mamoiada_params.h"
#elif defined (HAS_ISP_2400_MAMOIADA)
#define IS_ISP_2400_MAMOIADA

#include "isp2400_mamoiada_params.h"
#else
#error "isp_global_h: ISP_2400_MAMOIDA must be one of {2400, 2401 }"
#endif

#define ISP_PMEM_WIDTH_LOG2		ISP_LOG2_PMEM_WIDTH
#define ISP_PMEM_SIZE			ISP_PMEM_DEPTH

#define ISP_NWAY_LOG2			6
#define ISP_VEC_NELEMS_LOG2		ISP_NWAY_LOG2

#ifdef ISP2401
#ifdef PIPE_GENERATION
#define PIPEMEM(x) MEM(x)
#define ISP_NWAY   (1<<ISP_NWAY_LOG2)
#else
#define PIPEMEM(x)
#endif

#endif
/* The number of data bytes in a vector disregarding the reduced precision */
#define ISP_VEC_BYTES			(ISP_VEC_NELEMS*sizeof(uint16_t))

/* ISP SC Registers */
#define ISP_SC_REG			0x00
#define ISP_PC_REG			0x07
#define ISP_IRQ_READY_REG		0x00
#define ISP_IRQ_CLEAR_REG		0x00

/* ISP SC Register bits */
#define ISP_RST_BIT			0x00
#define ISP_START_BIT			0x01
#define ISP_BREAK_BIT			0x02
#define ISP_RUN_BIT			0x03
#define ISP_BROKEN_BIT			0x04
#define ISP_IDLE_BIT			0x05     /* READY */
#define ISP_SLEEPING_BIT		0x06
#define ISP_STALLING_BIT		0x07
#define ISP_IRQ_CLEAR_BIT		0x08
#define ISP_IRQ_READY_BIT		0x0A
#define ISP_IRQ_SLEEPING_BIT		0x0B

/* ISP Register bits */
#define ISP_CTRL_SINK_BIT		0x00
#define ISP_PMEM_SINK_BIT		0x01
#define ISP_DMEM_SINK_BIT		0x02
#define ISP_FIFO0_SINK_BIT		0x03
#define ISP_FIFO1_SINK_BIT		0x04
#define ISP_FIFO2_SINK_BIT		0x05
#define ISP_FIFO3_SINK_BIT		0x06
#define ISP_FIFO4_SINK_BIT		0x07
#define ISP_FIFO5_SINK_BIT		0x08
#define ISP_FIFO6_SINK_BIT		0x09
#define ISP_VMEM_SINK_BIT		0x0A
#define ISP_VAMEM1_SINK_BIT		0x0B
#define ISP_VAMEM2_SINK_BIT		0x0C
#define ISP_VAMEM3_SINK_BIT		0x0D
#define ISP_HMEM_SINK_BIT		0x0E

#define ISP_CTRL_SINK_REG		0x08
#define ISP_PMEM_SINK_REG		0x08
#define ISP_DMEM_SINK_REG		0x08
#define ISP_FIFO0_SINK_REG		0x08
#define ISP_FIFO1_SINK_REG		0x08
#define ISP_FIFO2_SINK_REG		0x08
#define ISP_FIFO3_SINK_REG		0x08
#define ISP_FIFO4_SINK_REG		0x08
#define ISP_FIFO5_SINK_REG		0x08
#define ISP_FIFO6_SINK_REG		0x08
#define ISP_VMEM_SINK_REG		0x08
#define ISP_VAMEM1_SINK_REG		0x08
#define ISP_VAMEM2_SINK_REG		0x08
#define ISP_VAMEM3_SINK_REG		0x08
#define ISP_HMEM_SINK_REG		0x08

#ifdef ISP2401
#define ISP_BAMEM_ALIGN_ELEM ISP_VMEM_ALIGN_ELEM
#define BAMEM VMEM

#define XNR3_DOWN_BAMEM_BASE_ADDRESS    (0x16880)
#define XNR3_UP_BAMEM_BASE_ADDRESS      (0x12880)

#define bmem_ldrow(fu, pid, offset, data) bmem_ldrow_s(fu, pid, offset, data)
#define bmem_strow(fu, pid, offset, data) bmem_strow_s(fu, pid, offset, data)
#define bmem_ldblk(fu, pid, offset, data) bmem_ldblk_s(fu, pid, offset, data)
#define bmem_stblk(fu, pid, offset, data) bmem_stblk_s(fu, pid, offset, data)

#endif
#endif /* __ISP_GLOBAL_H_INCLUDED__ */
