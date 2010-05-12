/*
 * Copyright (c) 2010 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef INCLUDED_AVP_H
#define INCLUDED_AVP_H

#include "ap15/arictlr.h"
#include "ap15/artimer.h"
// FIXME: get the ararmev header

// 3 controllers in contiguous memory starting at INTERRUPT_BASE, each
// controller's aperture is INTERRUPT_SIZE large
#define INTERRUPT_BASE 0x60004000
#define INTERRUPT_SIZE 0x100
#define INTERRUPT_NUM_CONTROLLERS 3

#define INTERRUPT_PENDING( ctlr ) \
    (INTERRUPT_BASE + ((ctlr) * INTERRUPT_SIZE) + ICTLR_VIRQ_COP_0)

#define INTERRUPT_SET( ctlr ) \
    (INTERRUPT_BASE + ((ctlr) * INTERRUPT_SIZE) + ICTLR_COP_IER_SET_0)

#define INTERRUPT_CLR( ctlr ) \
    (INTERRUPT_BASE + ((ctlr) * INTERRUPT_SIZE) + ICTLR_COP_IER_CLR_0)

#define OSC_CTRL        ( 0x60006000 + 0x50 )
#define OSC_FREQ_DET    ( 0x60006000 + 0x58 )
#define OSC_DET_STATUS  ( 0x60006000 + 0x5C )

#define TIMER_USEC      ( 0x60005010 )
#define TIMER_CFG       ( 0x60005014 )
#define TIMER_0_BASE    ( 0x60005000 )
#define TIMER_0         ( TIMER_0_BASE + TIMER_TMR_PTV_0 )
#define TIMER_0_CLEAR   ( TIMER_0_BASE + TIMER_TMR_PCR_0 )
#define TIMER_1_BASE    ( 0x60005008 )
#define TIMER_1         ( TIMER_1_BASE + TIMER_TMR_PTV_0 )
#define TIMER_1_CLEAR   ( TIMER_1_BASE + TIMER_TMR_PCR_0 )

#define CLOCK_RST_LO    (0x60006004)
#define CLOCK_CTLR_HI   (0x60006014)
#define CLOCK_CTLR_LO   (0x60006010)

#define CACHE_CTLR      (0x6000C000)
#define CACHE_CONTROL_0         (0x0)

#define PPI_INTR_ID_TIMER_0     (0)
#define PPI_INTR_ID_TIMER_1     (1)
#define PPI_INTR_ID_TIMER_2     (9)
#define PPI_INTR_ID_TIMER_3     (10)

/* flow controller */
#define FLOW_CONTROLLER     (0x60007004)

/* exception vectors */
#define VECTOR_BASE             ( 0x6000F200 )
#define VECTOR_RESET            ( VECTOR_BASE + 0 )
#define VECTOR_UNDEF            ( VECTOR_BASE + 4 )
#define VECTOR_SWI              ( VECTOR_BASE + 8 )
#define VECTOR_PREFETCH_ABORT   ( VECTOR_BASE + 12 )
#define VECTOR_DATA_ABORT       ( VECTOR_BASE + 16 )
#define VECTOR_IRQ              ( VECTOR_BASE + 24 )
#define VECTOR_FIQ              ( VECTOR_BASE + 28 )

#define MODE_DISABLE_INTR 0xc0
#define MODE_USR 0x10
#define MODE_FIQ 0x11
#define MODE_IRQ 0x12
#define MODE_SVC 0x13
#define MODE_ABT 0x17
#define MODE_UND 0x1B
#define MODE_SYS 0x1F

#define AP15_CACHE_LINE_SIZE            32

#define AP15_APB_L2_CACHE_BASE 0x7000e800 
#define AP15_APB_CLK_RST_BASE  0x60006000
#define AP15_APB_MISC_BASE     0x70000000

#define AP10_APB_CLK_RST_BASE  0x60006000
#define AP10_APB_MISC_BASE     0x70000000

#define MMU_TLB_BASE              0xf000f000
#define MMU_TLB_CACHE_WINDOW_0    0x40
#define MMU_TLB_CACHE_OPTIONS_0   0x44

#define AP15_PINMUX_CFG_CTL_0   0x70000024
#define AP15_AVP_JTAG_ENABLE    0xC0

#define PMC_SCRATCH22_REG_LP0   0x7000e4a8

#define AVP_WDT_RESET   0x2F00BAD0

/* Cached to uncached offset for AVP
 *
 * Hardware has uncached remap aperture for AVP as AVP doesn't have MMU
 * but still has cache (named COP cache).
 *
 * This aperture moved between AP15 and AP20.
 */
#define AP15_CACHED_TO_UNCACHED_OFFSET 0x90000000
#define AP20_CACHED_TO_UNCACHED_OFFSET 0x80000000

#define APXX_EXT_MEM_START      0x00000000
#define APXX_EXT_MEM_END        0x40000000

#define APXX_MMIO_START         0x40000000
#define APXX_MMIO_END           0xFFF00000

#define TXX_EXT_MEM_START       0x80000000
#define TXX_EXT_MEM_END         0xc0000000

#define TXX_MMIO_START          0x40000000
#define TXX_MMIO_END            0x80000000

#endif
