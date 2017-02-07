/*
 * Copyright 2016 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#ifndef SI_ENUMS_H
#define SI_ENUMS_H

#define VBLANK_INT_MASK                (1 << 0)
#define DC_HPDx_INT_EN                 (1 << 16)
#define VBLANK_ACK                     (1 << 4)
#define VLINE_ACK                      (1 << 4)

#define CURSOR_WIDTH 64
#define CURSOR_HEIGHT 64

#define VGA_VSTATUS_CNTL               0xFFFCFFFF
#define PRIORITY_MARK_MASK             0x7fff
#define PRIORITY_OFF                   (1 << 16)
#define PRIORITY_ALWAYS_ON             (1 << 20)
#define INTERLEAVE_EN                  (1 << 0)

#define LATENCY_WATERMARK_MASK(x)      ((x) << 16)
#define DC_LB_MEMORY_CONFIG(x)         ((x) << 20)
#define ICON_DEGAMMA_MODE(x)           (((x) & 0x3) << 8)

#define GRPH_ENDIAN_SWAP(x)            (((x) & 0x3) << 0)
#define GRPH_ENDIAN_NONE               0
#define GRPH_ENDIAN_8IN16              1
#define GRPH_ENDIAN_8IN32              2
#define GRPH_ENDIAN_8IN64              3

#define GRPH_DEPTH(x)                  (((x) & 0x3) << 0)
#define GRPH_DEPTH_8BPP                0
#define GRPH_DEPTH_16BPP               1
#define GRPH_DEPTH_32BPP               2

#define GRPH_FORMAT(x)                 (((x) & 0x7) << 8)
#define GRPH_FORMAT_INDEXED            0
#define GRPH_FORMAT_ARGB1555           0
#define GRPH_FORMAT_ARGB565            1
#define GRPH_FORMAT_ARGB4444           2
#define GRPH_FORMAT_AI88               3
#define GRPH_FORMAT_MONO16             4
#define GRPH_FORMAT_BGRA5551           5
#define GRPH_FORMAT_ARGB8888           0
#define GRPH_FORMAT_ARGB2101010        1
#define GRPH_FORMAT_32BPP_DIG          2
#define GRPH_FORMAT_8B_ARGB2101010     3
#define GRPH_FORMAT_BGRA1010102        4
#define GRPH_FORMAT_8B_BGRA1010102     5
#define GRPH_FORMAT_RGB111110          6
#define GRPH_FORMAT_BGR101111          7

#define GRPH_NUM_BANKS(x)              (((x) & 0x3) << 2)
#define GRPH_ARRAY_MODE(x)             (((x) & 0x7) << 20)
#define GRPH_ARRAY_LINEAR_GENERAL      0
#define GRPH_ARRAY_LINEAR_ALIGNED      1
#define GRPH_ARRAY_1D_TILED_THIN1      2
#define GRPH_ARRAY_2D_TILED_THIN1      4
#define GRPH_TILE_SPLIT(x)             (((x) & 0x7) << 13)
#define GRPH_BANK_WIDTH(x)             (((x) & 0x3) << 6)
#define GRPH_BANK_HEIGHT(x)            (((x) & 0x3) << 11)
#define GRPH_MACRO_TILE_ASPECT(x)      (((x) & 0x3) << 18)
#define GRPH_ARRAY_MODE(x)             (((x) & 0x7) << 20)
#define GRPH_PIPE_CONFIG(x)                   (((x) & 0x1f) << 24)

#define CURSOR_EN                      (1 << 0)
#define CURSOR_MODE(x)                 (((x) & 0x3) << 8)
#define CURSOR_MONO                    0
#define CURSOR_24_1                    1
#define CURSOR_24_8_PRE_MULT           2
#define CURSOR_24_8_UNPRE_MULT         3
#define CURSOR_2X_MAGNIFY              (1 << 16)
#define CURSOR_FORCE_MC_ON             (1 << 20)
#define CURSOR_URGENT_CONTROL(x)       (((x) & 0x7) << 24)
#define CURSOR_URGENT_ALWAYS           0
#define CURSOR_URGENT_1_8              1
#define CURSOR_URGENT_1_4              2
#define CURSOR_URGENT_3_8              3
#define CURSOR_URGENT_1_2              4
#define CURSOR_UPDATE_PENDING          (1 << 0)
#define CURSOR_UPDATE_TAKEN            (1 << 1)
#define CURSOR_UPDATE_LOCK             (1 << 16)
#define CURSOR_DISABLE_MULTIPLE_UPDATE (1 << 24)

#define AMDGPU_NUM_OF_VMIDS                     8
#define SI_CRTC0_REGISTER_OFFSET                0
#define SI_CRTC1_REGISTER_OFFSET                0x300
#define SI_CRTC2_REGISTER_OFFSET                0x2600
#define SI_CRTC3_REGISTER_OFFSET                0x2900
#define SI_CRTC4_REGISTER_OFFSET                0x2c00
#define SI_CRTC5_REGISTER_OFFSET                0x2f00

#define DMA0_REGISTER_OFFSET 0x000
#define DMA1_REGISTER_OFFSET 0x200
#define ES_AND_GS_AUTO       3
#define RADEON_PACKET_TYPE3  3
#define CE_PARTITION_BASE    3
#define BUF_SWAP_32BIT       (2 << 16)

#define GFX_POWER_STATUS                           (1 << 1)
#define GFX_CLOCK_STATUS                           (1 << 2)
#define GFX_LS_STATUS                              (1 << 3)
#define RLC_BUSY_STATUS                            (1 << 0)

#define RLC_PUD(x)                               ((x) << 0)
#define RLC_PUD_MASK                             (0xff << 0)
#define RLC_PDD(x)                               ((x) << 8)
#define RLC_PDD_MASK                             (0xff << 8)
#define RLC_TTPD(x)                              ((x) << 16)
#define RLC_TTPD_MASK                            (0xff << 16)
#define RLC_MSD(x)                               ((x) << 24)
#define RLC_MSD_MASK                             (0xff << 24)
#define WRITE_DATA_ENGINE_SEL(x) ((x) << 30)
#define WRITE_DATA_DST_SEL(x) ((x) << 8)
#define EVENT_TYPE(x) ((x) << 0)
#define EVENT_INDEX(x) ((x) << 8)
#define WAIT_REG_MEM_MEM_SPACE(x)               ((x) << 4)
#define WAIT_REG_MEM_FUNCTION(x)                ((x) << 0)
#define WAIT_REG_MEM_ENGINE(x)                  ((x) << 8)

#define GFX6_NUM_GFX_RINGS     1
#define GFX6_NUM_COMPUTE_RINGS 2
#define RLC_SAVE_AND_RESTORE_STARTING_OFFSET 0x90
#define RLC_CLEAR_STATE_DESCRIPTOR_OFFSET    0x3D

#define TAHITI_GB_ADDR_CONFIG_GOLDEN        0x12011003
#define VERDE_GB_ADDR_CONFIG_GOLDEN         0x02010002
#define HAINAN_GB_ADDR_CONFIG_GOLDEN        0x02010001

#define PACKET3(op, n)  ((RADEON_PACKET_TYPE3 << 30) |                  \
                         (((op) & 0xFF) << 8) |                         \
                         ((n) & 0x3FFF) << 16)
#define PACKET3_COMPUTE(op, n) (PACKET3(op, n) | 1 << 1)
#define	PACKET3_NOP					0x10
#define	PACKET3_SET_BASE				0x11
#define		PACKET3_BASE_INDEX(x)                  ((x) << 0)
#define	PACKET3_CLEAR_STATE				0x12
#define	PACKET3_INDEX_BUFFER_SIZE			0x13
#define	PACKET3_DISPATCH_DIRECT				0x15
#define	PACKET3_DISPATCH_INDIRECT			0x16
#define	PACKET3_ALLOC_GDS				0x1B
#define	PACKET3_WRITE_GDS_RAM				0x1C
#define	PACKET3_ATOMIC_GDS				0x1D
#define	PACKET3_ATOMIC					0x1E
#define	PACKET3_OCCLUSION_QUERY				0x1F
#define	PACKET3_SET_PREDICATION				0x20
#define	PACKET3_REG_RMW					0x21
#define	PACKET3_COND_EXEC				0x22
#define	PACKET3_PRED_EXEC				0x23
#define	PACKET3_DRAW_INDIRECT				0x24
#define	PACKET3_DRAW_INDEX_INDIRECT			0x25
#define	PACKET3_INDEX_BASE				0x26
#define	PACKET3_DRAW_INDEX_2				0x27
#define	PACKET3_CONTEXT_CONTROL				0x28
#define	PACKET3_INDEX_TYPE				0x2A
#define	PACKET3_DRAW_INDIRECT_MULTI			0x2C
#define	PACKET3_DRAW_INDEX_AUTO				0x2D
#define	PACKET3_DRAW_INDEX_IMMD				0x2E
#define	PACKET3_NUM_INSTANCES				0x2F
#define	PACKET3_DRAW_INDEX_MULTI_AUTO			0x30
#define	PACKET3_INDIRECT_BUFFER_CONST			0x31
#define	PACKET3_INDIRECT_BUFFER				0x3F
#define	PACKET3_STRMOUT_BUFFER_UPDATE			0x34
#define	PACKET3_DRAW_INDEX_OFFSET_2			0x35
#define	PACKET3_DRAW_INDEX_MULTI_ELEMENT		0x36
#define	PACKET3_WRITE_DATA				0x37
#define	PACKET3_DRAW_INDEX_INDIRECT_MULTI		0x38
#define	PACKET3_MEM_SEMAPHORE				0x39
#define	PACKET3_MPEG_INDEX				0x3A
#define	PACKET3_COPY_DW					0x3B
#define	PACKET3_WAIT_REG_MEM				0x3C
#define	PACKET3_MEM_WRITE				0x3D
#define	PACKET3_COPY_DATA				0x40
#define	PACKET3_CP_DMA					0x41
#              define PACKET3_CP_DMA_DST_SEL(x)    ((x) << 20)
#              define PACKET3_CP_DMA_ENGINE(x)     ((x) << 27)
#              define PACKET3_CP_DMA_SRC_SEL(x)    ((x) << 29)
#              define PACKET3_CP_DMA_CP_SYNC       (1 << 31)
#              define PACKET3_CP_DMA_DIS_WC        (1 << 21)
#              define PACKET3_CP_DMA_CMD_SRC_SWAP(x) ((x) << 22)
#              define PACKET3_CP_DMA_CMD_DST_SWAP(x) ((x) << 24)
#              define PACKET3_CP_DMA_CMD_SAS       (1 << 26)
#              define PACKET3_CP_DMA_CMD_DAS       (1 << 27)
#              define PACKET3_CP_DMA_CMD_SAIC      (1 << 28)
#              define PACKET3_CP_DMA_CMD_DAIC      (1 << 29)
#              define PACKET3_CP_DMA_CMD_RAW_WAIT  (1 << 30)
#define	PACKET3_PFP_SYNC_ME				0x42
#define	PACKET3_SURFACE_SYNC				0x43
#              define PACKET3_DEST_BASE_0_ENA      (1 << 0)
#              define PACKET3_DEST_BASE_1_ENA      (1 << 1)
#              define PACKET3_CB0_DEST_BASE_ENA    (1 << 6)
#              define PACKET3_CB1_DEST_BASE_ENA    (1 << 7)
#              define PACKET3_CB2_DEST_BASE_ENA    (1 << 8)
#              define PACKET3_CB3_DEST_BASE_ENA    (1 << 9)
#              define PACKET3_CB4_DEST_BASE_ENA    (1 << 10)
#              define PACKET3_CB5_DEST_BASE_ENA    (1 << 11)
#              define PACKET3_CB6_DEST_BASE_ENA    (1 << 12)
#              define PACKET3_CB7_DEST_BASE_ENA    (1 << 13)
#              define PACKET3_DB_DEST_BASE_ENA     (1 << 14)
#              define PACKET3_DEST_BASE_2_ENA      (1 << 19)
#              define PACKET3_DEST_BASE_3_ENA      (1 << 21)
#              define PACKET3_TCL1_ACTION_ENA      (1 << 22)
#              define PACKET3_TC_ACTION_ENA        (1 << 23)
#              define PACKET3_CB_ACTION_ENA        (1 << 25)
#              define PACKET3_DB_ACTION_ENA        (1 << 26)
#              define PACKET3_SH_KCACHE_ACTION_ENA (1 << 27)
#              define PACKET3_SH_ICACHE_ACTION_ENA (1 << 29)
#define	PACKET3_ME_INITIALIZE				0x44
#define		PACKET3_ME_INITIALIZE_DEVICE_ID(x) ((x) << 16)
#define	PACKET3_COND_WRITE				0x45
#define	PACKET3_EVENT_WRITE				0x46
#define	PACKET3_EVENT_WRITE_EOP				0x47
#define	PACKET3_EVENT_WRITE_EOS				0x48
#define	PACKET3_PREAMBLE_CNTL				0x4A
#              define PACKET3_PREAMBLE_BEGIN_CLEAR_STATE     (2 << 28)
#              define PACKET3_PREAMBLE_END_CLEAR_STATE       (3 << 28)
#define	PACKET3_ONE_REG_WRITE				0x57
#define	PACKET3_LOAD_CONFIG_REG				0x5F
#define	PACKET3_LOAD_CONTEXT_REG			0x60
#define	PACKET3_LOAD_SH_REG				0x61
#define	PACKET3_SET_CONFIG_REG				0x68
#define		PACKET3_SET_CONFIG_REG_START			0x00002000
#define		PACKET3_SET_CONFIG_REG_END			0x00002c00
#define	PACKET3_SET_CONTEXT_REG				0x69
#define		PACKET3_SET_CONTEXT_REG_START			0x000a000
#define		PACKET3_SET_CONTEXT_REG_END			0x000a400
#define	PACKET3_SET_CONTEXT_REG_INDIRECT		0x73
#define	PACKET3_SET_RESOURCE_INDIRECT			0x74
#define	PACKET3_SET_SH_REG				0x76
#define		PACKET3_SET_SH_REG_START			0x00002c00
#define		PACKET3_SET_SH_REG_END				0x00003000
#define	PACKET3_SET_SH_REG_OFFSET			0x77
#define	PACKET3_ME_WRITE				0x7A
#define	PACKET3_SCRATCH_RAM_WRITE			0x7D
#define	PACKET3_SCRATCH_RAM_READ			0x7E
#define	PACKET3_CE_WRITE				0x7F
#define	PACKET3_LOAD_CONST_RAM				0x80
#define	PACKET3_WRITE_CONST_RAM				0x81
#define	PACKET3_WRITE_CONST_RAM_OFFSET			0x82
#define	PACKET3_DUMP_CONST_RAM				0x83
#define	PACKET3_INCREMENT_CE_COUNTER			0x84
#define	PACKET3_INCREMENT_DE_COUNTER			0x85
#define	PACKET3_WAIT_ON_CE_COUNTER			0x86
#define	PACKET3_WAIT_ON_DE_COUNTER			0x87
#define	PACKET3_WAIT_ON_DE_COUNTER_DIFF			0x88
#define	PACKET3_SET_CE_DE_COUNTERS			0x89
#define	PACKET3_WAIT_ON_AVAIL_BUFFER			0x8A
#define	PACKET3_SWITCH_BUFFER				0x8B
#define PACKET3_SEM_WAIT_ON_SIGNAL    (0x1 << 12)
#define PACKET3_SEM_SEL_SIGNAL	    (0x6 << 29)
#define PACKET3_SEM_SEL_WAIT	    (0x7 << 29)

#endif
