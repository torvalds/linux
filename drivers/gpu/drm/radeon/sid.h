/*
 * Copyright 2011 Advanced Micro Devices, Inc.
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
 * Authors: Alex Deucher
 */
#ifndef SI_H
#define SI_H

#define TAHITI_RB_BITMAP_WIDTH_PER_SH  2

#define TAHITI_GB_ADDR_CONFIG_GOLDEN        0x12011003
#define VERDE_GB_ADDR_CONFIG_GOLDEN         0x12010002

#define	CG_MULT_THERMAL_STATUS					0x714
#define		ASIC_MAX_TEMP(x)				((x) << 0)
#define		ASIC_MAX_TEMP_MASK				0x000001ff
#define		ASIC_MAX_TEMP_SHIFT				0
#define		CTF_TEMP(x)					((x) << 9)
#define		CTF_TEMP_MASK					0x0003fe00
#define		CTF_TEMP_SHIFT					9

#define SI_MAX_SH_GPRS           256
#define SI_MAX_TEMP_GPRS         16
#define SI_MAX_SH_THREADS        256
#define SI_MAX_SH_STACK_ENTRIES  4096
#define SI_MAX_FRC_EOV_CNT       16384
#define SI_MAX_BACKENDS          8
#define SI_MAX_BACKENDS_MASK     0xFF
#define SI_MAX_BACKENDS_PER_SE_MASK     0x0F
#define SI_MAX_SIMDS             12
#define SI_MAX_SIMDS_MASK        0x0FFF
#define SI_MAX_SIMDS_PER_SE_MASK        0x00FF
#define SI_MAX_PIPES             8
#define SI_MAX_PIPES_MASK        0xFF
#define SI_MAX_PIPES_PER_SIMD_MASK      0x3F
#define SI_MAX_LDS_NUM           0xFFFF
#define SI_MAX_TCC               16
#define SI_MAX_TCC_MASK          0xFFFF

#define VGA_HDP_CONTROL  				0x328
#define		VGA_MEMORY_DISABLE				(1 << 4)

#define DMIF_ADDR_CONFIG  				0xBD4

#define	SRBM_STATUS				        0xE50

#define	CC_SYS_RB_BACKEND_DISABLE			0xe80
#define	GC_USER_SYS_RB_BACKEND_DISABLE			0xe84

#define VM_L2_CNTL					0x1400
#define		ENABLE_L2_CACHE					(1 << 0)
#define		ENABLE_L2_FRAGMENT_PROCESSING			(1 << 1)
#define		L2_CACHE_PTE_ENDIAN_SWAP_MODE(x)		((x) << 2)
#define		L2_CACHE_PDE_ENDIAN_SWAP_MODE(x)		((x) << 4)
#define		ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE		(1 << 9)
#define		ENABLE_L2_PDE0_CACHE_LRU_UPDATE_BY_WRITE	(1 << 10)
#define		EFFECTIVE_L2_QUEUE_SIZE(x)			(((x) & 7) << 15)
#define		CONTEXT1_IDENTITY_ACCESS_MODE(x)		(((x) & 3) << 19)
#define VM_L2_CNTL2					0x1404
#define		INVALIDATE_ALL_L1_TLBS				(1 << 0)
#define		INVALIDATE_L2_CACHE				(1 << 1)
#define		INVALIDATE_CACHE_MODE(x)			((x) << 26)
#define			INVALIDATE_PTE_AND_PDE_CACHES		0
#define			INVALIDATE_ONLY_PTE_CACHES		1
#define			INVALIDATE_ONLY_PDE_CACHES		2
#define VM_L2_CNTL3					0x1408
#define		BANK_SELECT(x)					((x) << 0)
#define		L2_CACHE_UPDATE_MODE(x)				((x) << 6)
#define		L2_CACHE_BIGK_FRAGMENT_SIZE(x)			((x) << 15)
#define		L2_CACHE_BIGK_ASSOCIATIVITY			(1 << 20)
#define	VM_L2_STATUS					0x140C
#define		L2_BUSY						(1 << 0)
#define VM_CONTEXT0_CNTL				0x1410
#define		ENABLE_CONTEXT					(1 << 0)
#define		PAGE_TABLE_DEPTH(x)				(((x) & 3) << 1)
#define		RANGE_PROTECTION_FAULT_ENABLE_DEFAULT		(1 << 4)
#define VM_CONTEXT1_CNTL				0x1414
#define VM_CONTEXT0_CNTL2				0x1430
#define VM_CONTEXT1_CNTL2				0x1434
#define	VM_CONTEXT8_PAGE_TABLE_BASE_ADDR		0x1438
#define	VM_CONTEXT9_PAGE_TABLE_BASE_ADDR		0x143c
#define	VM_CONTEXT10_PAGE_TABLE_BASE_ADDR		0x1440
#define	VM_CONTEXT11_PAGE_TABLE_BASE_ADDR		0x1444
#define	VM_CONTEXT12_PAGE_TABLE_BASE_ADDR		0x1448
#define	VM_CONTEXT13_PAGE_TABLE_BASE_ADDR		0x144c
#define	VM_CONTEXT14_PAGE_TABLE_BASE_ADDR		0x1450
#define	VM_CONTEXT15_PAGE_TABLE_BASE_ADDR		0x1454

#define VM_INVALIDATE_REQUEST				0x1478
#define VM_INVALIDATE_RESPONSE				0x147c

#define VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR	0x1518
#define VM_CONTEXT1_PROTECTION_FAULT_DEFAULT_ADDR	0x151c

#define	VM_CONTEXT0_PAGE_TABLE_BASE_ADDR		0x153c
#define	VM_CONTEXT1_PAGE_TABLE_BASE_ADDR		0x1540
#define	VM_CONTEXT2_PAGE_TABLE_BASE_ADDR		0x1544
#define	VM_CONTEXT3_PAGE_TABLE_BASE_ADDR		0x1548
#define	VM_CONTEXT4_PAGE_TABLE_BASE_ADDR		0x154c
#define	VM_CONTEXT5_PAGE_TABLE_BASE_ADDR		0x1550
#define	VM_CONTEXT6_PAGE_TABLE_BASE_ADDR		0x1554
#define	VM_CONTEXT7_PAGE_TABLE_BASE_ADDR		0x1558
#define	VM_CONTEXT0_PAGE_TABLE_START_ADDR		0x155c
#define	VM_CONTEXT1_PAGE_TABLE_START_ADDR		0x1560

#define	VM_CONTEXT0_PAGE_TABLE_END_ADDR			0x157C
#define	VM_CONTEXT1_PAGE_TABLE_END_ADDR			0x1580

#define MC_SHARED_CHMAP						0x2004
#define		NOOFCHAN_SHIFT					12
#define		NOOFCHAN_MASK					0x0000f000
#define MC_SHARED_CHREMAP					0x2008

#define	MC_VM_FB_LOCATION				0x2024
#define	MC_VM_AGP_TOP					0x2028
#define	MC_VM_AGP_BOT					0x202C
#define	MC_VM_AGP_BASE					0x2030
#define	MC_VM_SYSTEM_APERTURE_LOW_ADDR			0x2034
#define	MC_VM_SYSTEM_APERTURE_HIGH_ADDR			0x2038
#define	MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR		0x203C

#define	MC_VM_MX_L1_TLB_CNTL				0x2064
#define		ENABLE_L1_TLB					(1 << 0)
#define		ENABLE_L1_FRAGMENT_PROCESSING			(1 << 1)
#define		SYSTEM_ACCESS_MODE_PA_ONLY			(0 << 3)
#define		SYSTEM_ACCESS_MODE_USE_SYS_MAP			(1 << 3)
#define		SYSTEM_ACCESS_MODE_IN_SYS			(2 << 3)
#define		SYSTEM_ACCESS_MODE_NOT_IN_SYS			(3 << 3)
#define		SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU	(0 << 5)
#define		ENABLE_ADVANCED_DRIVER_MODEL			(1 << 6)

#define MC_SHARED_BLACKOUT_CNTL           		0x20ac

#define	MC_ARB_RAMCFG					0x2760
#define		NOOFBANK_SHIFT					0
#define		NOOFBANK_MASK					0x00000003
#define		NOOFRANK_SHIFT					2
#define		NOOFRANK_MASK					0x00000004
#define		NOOFROWS_SHIFT					3
#define		NOOFROWS_MASK					0x00000038
#define		NOOFCOLS_SHIFT					6
#define		NOOFCOLS_MASK					0x000000C0
#define		CHANSIZE_SHIFT					8
#define		CHANSIZE_MASK					0x00000100
#define		CHANSIZE_OVERRIDE				(1 << 11)
#define		NOOFGROUPS_SHIFT				12
#define		NOOFGROUPS_MASK					0x00001000

#define	MC_SEQ_TRAIN_WAKEUP_CNTL			0x2808
#define		TRAIN_DONE_D0      			(1 << 30)
#define		TRAIN_DONE_D1      			(1 << 31)

#define MC_SEQ_SUP_CNTL           			0x28c8
#define		RUN_MASK      				(1 << 0)
#define MC_SEQ_SUP_PGM           			0x28cc

#define MC_IO_PAD_CNTL_D0           			0x29d0
#define		MEM_FALL_OUT_CMD      			(1 << 8)

#define MC_SEQ_IO_DEBUG_INDEX           		0x2a44
#define MC_SEQ_IO_DEBUG_DATA           			0x2a48

#define	HDP_HOST_PATH_CNTL				0x2C00
#define	HDP_NONSURFACE_BASE				0x2C04
#define	HDP_NONSURFACE_INFO				0x2C08
#define	HDP_NONSURFACE_SIZE				0x2C0C

#define HDP_ADDR_CONFIG  				0x2F48
#define HDP_MISC_CNTL					0x2F4C
#define 	HDP_FLUSH_INVALIDATE_CACHE			(1 << 0)

#define IH_RB_CNTL                                        0x3e00
#       define IH_RB_ENABLE                               (1 << 0)
#       define IH_IB_SIZE(x)                              ((x) << 1) /* log2 */
#       define IH_RB_FULL_DRAIN_ENABLE                    (1 << 6)
#       define IH_WPTR_WRITEBACK_ENABLE                   (1 << 8)
#       define IH_WPTR_WRITEBACK_TIMER(x)                 ((x) << 9) /* log2 */
#       define IH_WPTR_OVERFLOW_ENABLE                    (1 << 16)
#       define IH_WPTR_OVERFLOW_CLEAR                     (1 << 31)
#define IH_RB_BASE                                        0x3e04
#define IH_RB_RPTR                                        0x3e08
#define IH_RB_WPTR                                        0x3e0c
#       define RB_OVERFLOW                                (1 << 0)
#       define WPTR_OFFSET_MASK                           0x3fffc
#define IH_RB_WPTR_ADDR_HI                                0x3e10
#define IH_RB_WPTR_ADDR_LO                                0x3e14
#define IH_CNTL                                           0x3e18
#       define ENABLE_INTR                                (1 << 0)
#       define IH_MC_SWAP(x)                              ((x) << 1)
#       define IH_MC_SWAP_NONE                            0
#       define IH_MC_SWAP_16BIT                           1
#       define IH_MC_SWAP_32BIT                           2
#       define IH_MC_SWAP_64BIT                           3
#       define RPTR_REARM                                 (1 << 4)
#       define MC_WRREQ_CREDIT(x)                         ((x) << 15)
#       define MC_WR_CLEAN_CNT(x)                         ((x) << 20)
#       define MC_VMID(x)                                 ((x) << 25)

#define	CONFIG_MEMSIZE					0x5428

#define INTERRUPT_CNTL                                    0x5468
#       define IH_DUMMY_RD_OVERRIDE                       (1 << 0)
#       define IH_DUMMY_RD_EN                             (1 << 1)
#       define IH_REQ_NONSNOOP_EN                         (1 << 3)
#       define GEN_IH_INT_EN                              (1 << 8)
#define INTERRUPT_CNTL2                                   0x546c

#define HDP_MEM_COHERENCY_FLUSH_CNTL			0x5480

#define	BIF_FB_EN						0x5490
#define		FB_READ_EN					(1 << 0)
#define		FB_WRITE_EN					(1 << 1)

#define HDP_REG_COHERENCY_FLUSH_CNTL			0x54A0

#define	DC_LB_MEMORY_SPLIT					0x6b0c
#define		DC_LB_MEMORY_CONFIG(x)				((x) << 20)

#define	PRIORITY_A_CNT						0x6b18
#define		PRIORITY_MARK_MASK				0x7fff
#define		PRIORITY_OFF					(1 << 16)
#define		PRIORITY_ALWAYS_ON				(1 << 20)
#define	PRIORITY_B_CNT						0x6b1c

#define	DPG_PIPE_ARBITRATION_CONTROL3				0x6cc8
#       define LATENCY_WATERMARK_MASK(x)			((x) << 16)
#define	DPG_PIPE_LATENCY_CONTROL				0x6ccc
#       define LATENCY_LOW_WATERMARK(x)				((x) << 0)
#       define LATENCY_HIGH_WATERMARK(x)			((x) << 16)

/* 0x6bb8, 0x77b8, 0x103b8, 0x10fb8, 0x11bb8, 0x127b8 */
#define VLINE_STATUS                                    0x6bb8
#       define VLINE_OCCURRED                           (1 << 0)
#       define VLINE_ACK                                (1 << 4)
#       define VLINE_STAT                               (1 << 12)
#       define VLINE_INTERRUPT                          (1 << 16)
#       define VLINE_INTERRUPT_TYPE                     (1 << 17)
/* 0x6bbc, 0x77bc, 0x103bc, 0x10fbc, 0x11bbc, 0x127bc */
#define VBLANK_STATUS                                   0x6bbc
#       define VBLANK_OCCURRED                          (1 << 0)
#       define VBLANK_ACK                               (1 << 4)
#       define VBLANK_STAT                              (1 << 12)
#       define VBLANK_INTERRUPT                         (1 << 16)
#       define VBLANK_INTERRUPT_TYPE                    (1 << 17)

/* 0x6b40, 0x7740, 0x10340, 0x10f40, 0x11b40, 0x12740 */
#define INT_MASK                                        0x6b40
#       define VBLANK_INT_MASK                          (1 << 0)
#       define VLINE_INT_MASK                           (1 << 4)

#define DISP_INTERRUPT_STATUS                           0x60f4
#       define LB_D1_VLINE_INTERRUPT                    (1 << 2)
#       define LB_D1_VBLANK_INTERRUPT                   (1 << 3)
#       define DC_HPD1_INTERRUPT                        (1 << 17)
#       define DC_HPD1_RX_INTERRUPT                     (1 << 18)
#       define DACA_AUTODETECT_INTERRUPT                (1 << 22)
#       define DACB_AUTODETECT_INTERRUPT                (1 << 23)
#       define DC_I2C_SW_DONE_INTERRUPT                 (1 << 24)
#       define DC_I2C_HW_DONE_INTERRUPT                 (1 << 25)
#define DISP_INTERRUPT_STATUS_CONTINUE                  0x60f8
#       define LB_D2_VLINE_INTERRUPT                    (1 << 2)
#       define LB_D2_VBLANK_INTERRUPT                   (1 << 3)
#       define DC_HPD2_INTERRUPT                        (1 << 17)
#       define DC_HPD2_RX_INTERRUPT                     (1 << 18)
#       define DISP_TIMER_INTERRUPT                     (1 << 24)
#define DISP_INTERRUPT_STATUS_CONTINUE2                 0x60fc
#       define LB_D3_VLINE_INTERRUPT                    (1 << 2)
#       define LB_D3_VBLANK_INTERRUPT                   (1 << 3)
#       define DC_HPD3_INTERRUPT                        (1 << 17)
#       define DC_HPD3_RX_INTERRUPT                     (1 << 18)
#define DISP_INTERRUPT_STATUS_CONTINUE3                 0x6100
#       define LB_D4_VLINE_INTERRUPT                    (1 << 2)
#       define LB_D4_VBLANK_INTERRUPT                   (1 << 3)
#       define DC_HPD4_INTERRUPT                        (1 << 17)
#       define DC_HPD4_RX_INTERRUPT                     (1 << 18)
#define DISP_INTERRUPT_STATUS_CONTINUE4                 0x614c
#       define LB_D5_VLINE_INTERRUPT                    (1 << 2)
#       define LB_D5_VBLANK_INTERRUPT                   (1 << 3)
#       define DC_HPD5_INTERRUPT                        (1 << 17)
#       define DC_HPD5_RX_INTERRUPT                     (1 << 18)
#define DISP_INTERRUPT_STATUS_CONTINUE5                 0x6150
#       define LB_D6_VLINE_INTERRUPT                    (1 << 2)
#       define LB_D6_VBLANK_INTERRUPT                   (1 << 3)
#       define DC_HPD6_INTERRUPT                        (1 << 17)
#       define DC_HPD6_RX_INTERRUPT                     (1 << 18)

/* 0x6858, 0x7458, 0x10058, 0x10c58, 0x11858, 0x12458 */
#define GRPH_INT_STATUS                                 0x6858
#       define GRPH_PFLIP_INT_OCCURRED                  (1 << 0)
#       define GRPH_PFLIP_INT_CLEAR                     (1 << 8)
/* 0x685c, 0x745c, 0x1005c, 0x10c5c, 0x1185c, 0x1245c */
#define	GRPH_INT_CONTROL			        0x685c
#       define GRPH_PFLIP_INT_MASK                      (1 << 0)
#       define GRPH_PFLIP_INT_TYPE                      (1 << 8)

#define	DACA_AUTODETECT_INT_CONTROL			0x66c8

#define DC_HPD1_INT_STATUS                              0x601c
#define DC_HPD2_INT_STATUS                              0x6028
#define DC_HPD3_INT_STATUS                              0x6034
#define DC_HPD4_INT_STATUS                              0x6040
#define DC_HPD5_INT_STATUS                              0x604c
#define DC_HPD6_INT_STATUS                              0x6058
#       define DC_HPDx_INT_STATUS                       (1 << 0)
#       define DC_HPDx_SENSE                            (1 << 1)
#       define DC_HPDx_RX_INT_STATUS                    (1 << 8)

#define DC_HPD1_INT_CONTROL                             0x6020
#define DC_HPD2_INT_CONTROL                             0x602c
#define DC_HPD3_INT_CONTROL                             0x6038
#define DC_HPD4_INT_CONTROL                             0x6044
#define DC_HPD5_INT_CONTROL                             0x6050
#define DC_HPD6_INT_CONTROL                             0x605c
#       define DC_HPDx_INT_ACK                          (1 << 0)
#       define DC_HPDx_INT_POLARITY                     (1 << 8)
#       define DC_HPDx_INT_EN                           (1 << 16)
#       define DC_HPDx_RX_INT_ACK                       (1 << 20)
#       define DC_HPDx_RX_INT_EN                        (1 << 24)

#define DC_HPD1_CONTROL                                   0x6024
#define DC_HPD2_CONTROL                                   0x6030
#define DC_HPD3_CONTROL                                   0x603c
#define DC_HPD4_CONTROL                                   0x6048
#define DC_HPD5_CONTROL                                   0x6054
#define DC_HPD6_CONTROL                                   0x6060
#       define DC_HPDx_CONNECTION_TIMER(x)                ((x) << 0)
#       define DC_HPDx_RX_INT_TIMER(x)                    ((x) << 16)
#       define DC_HPDx_EN                                 (1 << 28)

/* 0x6e98, 0x7a98, 0x10698, 0x11298, 0x11e98, 0x12a98 */
#define CRTC_STATUS_FRAME_COUNT                         0x6e98

#define	GRBM_CNTL					0x8000
#define		GRBM_READ_TIMEOUT(x)				((x) << 0)

#define	GRBM_STATUS2					0x8008
#define		RLC_RQ_PENDING 					(1 << 0)
#define		RLC_BUSY 					(1 << 8)
#define		TC_BUSY 					(1 << 9)

#define	GRBM_STATUS					0x8010
#define		CMDFIFO_AVAIL_MASK				0x0000000F
#define		RING2_RQ_PENDING				(1 << 4)
#define		SRBM_RQ_PENDING					(1 << 5)
#define		RING1_RQ_PENDING				(1 << 6)
#define		CF_RQ_PENDING					(1 << 7)
#define		PF_RQ_PENDING					(1 << 8)
#define		GDS_DMA_RQ_PENDING				(1 << 9)
#define		GRBM_EE_BUSY					(1 << 10)
#define		DB_CLEAN					(1 << 12)
#define		CB_CLEAN					(1 << 13)
#define		TA_BUSY 					(1 << 14)
#define		GDS_BUSY 					(1 << 15)
#define		VGT_BUSY					(1 << 17)
#define		IA_BUSY_NO_DMA					(1 << 18)
#define		IA_BUSY						(1 << 19)
#define		SX_BUSY 					(1 << 20)
#define		SPI_BUSY					(1 << 22)
#define		BCI_BUSY					(1 << 23)
#define		SC_BUSY 					(1 << 24)
#define		PA_BUSY 					(1 << 25)
#define		DB_BUSY 					(1 << 26)
#define		CP_COHERENCY_BUSY      				(1 << 28)
#define		CP_BUSY 					(1 << 29)
#define		CB_BUSY 					(1 << 30)
#define		GUI_ACTIVE					(1 << 31)
#define	GRBM_STATUS_SE0					0x8014
#define	GRBM_STATUS_SE1					0x8018
#define		SE_DB_CLEAN					(1 << 1)
#define		SE_CB_CLEAN					(1 << 2)
#define		SE_BCI_BUSY					(1 << 22)
#define		SE_VGT_BUSY					(1 << 23)
#define		SE_PA_BUSY					(1 << 24)
#define		SE_TA_BUSY					(1 << 25)
#define		SE_SX_BUSY					(1 << 26)
#define		SE_SPI_BUSY					(1 << 27)
#define		SE_SC_BUSY					(1 << 29)
#define		SE_DB_BUSY					(1 << 30)
#define		SE_CB_BUSY					(1 << 31)

#define	GRBM_SOFT_RESET					0x8020
#define		SOFT_RESET_CP					(1 << 0)
#define		SOFT_RESET_CB					(1 << 1)
#define		SOFT_RESET_RLC					(1 << 2)
#define		SOFT_RESET_DB					(1 << 3)
#define		SOFT_RESET_GDS					(1 << 4)
#define		SOFT_RESET_PA					(1 << 5)
#define		SOFT_RESET_SC					(1 << 6)
#define		SOFT_RESET_BCI					(1 << 7)
#define		SOFT_RESET_SPI					(1 << 8)
#define		SOFT_RESET_SX					(1 << 10)
#define		SOFT_RESET_TC					(1 << 11)
#define		SOFT_RESET_TA					(1 << 12)
#define		SOFT_RESET_VGT					(1 << 14)
#define		SOFT_RESET_IA					(1 << 15)

#define GRBM_GFX_INDEX          			0x802C
#define		INSTANCE_INDEX(x)			((x) << 0)
#define		SH_INDEX(x)     			((x) << 8)
#define		SE_INDEX(x)     			((x) << 16)
#define		SH_BROADCAST_WRITES      		(1 << 29)
#define		INSTANCE_BROADCAST_WRITES      		(1 << 30)
#define		SE_BROADCAST_WRITES      		(1 << 31)

#define GRBM_INT_CNTL                                   0x8060
#       define RDERR_INT_ENABLE                         (1 << 0)
#       define GUI_IDLE_INT_ENABLE                      (1 << 19)

#define	SCRATCH_REG0					0x8500
#define	SCRATCH_REG1					0x8504
#define	SCRATCH_REG2					0x8508
#define	SCRATCH_REG3					0x850C
#define	SCRATCH_REG4					0x8510
#define	SCRATCH_REG5					0x8514
#define	SCRATCH_REG6					0x8518
#define	SCRATCH_REG7					0x851C

#define	SCRATCH_UMSK					0x8540
#define	SCRATCH_ADDR					0x8544

#define	CP_SEM_WAIT_TIMER				0x85BC

#define	CP_SEM_INCOMPLETE_TIMER_CNTL			0x85C8

#define CP_ME_CNTL					0x86D8
#define		CP_CE_HALT					(1 << 24)
#define		CP_PFP_HALT					(1 << 26)
#define		CP_ME_HALT					(1 << 28)

#define	CP_COHER_CNTL2					0x85E8

#define	CP_RB2_RPTR					0x86f8
#define	CP_RB1_RPTR					0x86fc
#define	CP_RB0_RPTR					0x8700
#define	CP_RB_WPTR_DELAY				0x8704

#define	CP_QUEUE_THRESHOLDS				0x8760
#define		ROQ_IB1_START(x)				((x) << 0)
#define		ROQ_IB2_START(x)				((x) << 8)
#define CP_MEQ_THRESHOLDS				0x8764
#define		MEQ1_START(x)				((x) << 0)
#define		MEQ2_START(x)				((x) << 8)

#define	CP_PERFMON_CNTL					0x87FC

#define	VGT_VTX_VECT_EJECT_REG				0x88B0

#define	VGT_CACHE_INVALIDATION				0x88C4
#define		CACHE_INVALIDATION(x)				((x) << 0)
#define			VC_ONLY						0
#define			TC_ONLY						1
#define			VC_AND_TC					2
#define		AUTO_INVLD_EN(x)				((x) << 6)
#define			NO_AUTO						0
#define			ES_AUTO						1
#define			GS_AUTO						2
#define			ES_AND_GS_AUTO					3
#define	VGT_ESGS_RING_SIZE				0x88C8
#define	VGT_GSVS_RING_SIZE				0x88CC

#define	VGT_GS_VERTEX_REUSE				0x88D4

#define	VGT_PRIMITIVE_TYPE				0x8958
#define	VGT_INDEX_TYPE					0x895C

#define	VGT_NUM_INDICES					0x8970
#define	VGT_NUM_INSTANCES				0x8974

#define	VGT_TF_RING_SIZE				0x8988

#define	VGT_HS_OFFCHIP_PARAM				0x89B0

#define	VGT_TF_MEMORY_BASE				0x89B8

#define CC_GC_SHADER_ARRAY_CONFIG			0x89bc
#define		INACTIVE_CUS_MASK			0xFFFF0000
#define		INACTIVE_CUS_SHIFT			16
#define GC_USER_SHADER_ARRAY_CONFIG			0x89c0

#define	PA_CL_ENHANCE					0x8A14
#define		CLIP_VTX_REORDER_ENA				(1 << 0)
#define		NUM_CLIP_SEQ(x)					((x) << 1)

#define	PA_SU_LINE_STIPPLE_VALUE			0x8A60

#define	PA_SC_LINE_STIPPLE_STATE			0x8B10

#define	PA_SC_FORCE_EOV_MAX_CNTS			0x8B24
#define		FORCE_EOV_MAX_CLK_CNT(x)			((x) << 0)
#define		FORCE_EOV_MAX_REZ_CNT(x)			((x) << 16)

#define	PA_SC_FIFO_SIZE					0x8BCC
#define		SC_FRONTEND_PRIM_FIFO_SIZE(x)			((x) << 0)
#define		SC_BACKEND_PRIM_FIFO_SIZE(x)			((x) << 6)
#define		SC_HIZ_TILE_FIFO_SIZE(x)			((x) << 15)
#define		SC_EARLYZ_TILE_FIFO_SIZE(x)			((x) << 23)

#define	PA_SC_ENHANCE					0x8BF0

#define	SQ_CONFIG					0x8C00

#define	SQC_CACHES					0x8C08

#define	SX_DEBUG_1					0x9060

#define	SPI_STATIC_THREAD_MGMT_1			0x90E0
#define	SPI_STATIC_THREAD_MGMT_2			0x90E4
#define	SPI_STATIC_THREAD_MGMT_3			0x90E8
#define	SPI_PS_MAX_WAVE_ID				0x90EC

#define	SPI_CONFIG_CNTL					0x9100

#define	SPI_CONFIG_CNTL_1				0x913C
#define		VTX_DONE_DELAY(x)				((x) << 0)
#define		INTERP_ONE_PRIM_PER_ROW				(1 << 4)

#define	CGTS_TCC_DISABLE				0x9148
#define	CGTS_USER_TCC_DISABLE				0x914C
#define		TCC_DISABLE_MASK				0xFFFF0000
#define		TCC_DISABLE_SHIFT				16

#define	TA_CNTL_AUX					0x9508

#define CC_RB_BACKEND_DISABLE				0x98F4
#define		BACKEND_DISABLE(x)     			((x) << 16)
#define GB_ADDR_CONFIG  				0x98F8
#define		NUM_PIPES(x)				((x) << 0)
#define		NUM_PIPES_MASK				0x00000007
#define		NUM_PIPES_SHIFT				0
#define		PIPE_INTERLEAVE_SIZE(x)			((x) << 4)
#define		PIPE_INTERLEAVE_SIZE_MASK		0x00000070
#define		PIPE_INTERLEAVE_SIZE_SHIFT		4
#define		NUM_SHADER_ENGINES(x)			((x) << 12)
#define		NUM_SHADER_ENGINES_MASK			0x00003000
#define		NUM_SHADER_ENGINES_SHIFT		12
#define		SHADER_ENGINE_TILE_SIZE(x)     		((x) << 16)
#define		SHADER_ENGINE_TILE_SIZE_MASK		0x00070000
#define		SHADER_ENGINE_TILE_SIZE_SHIFT		16
#define		NUM_GPUS(x)     			((x) << 20)
#define		NUM_GPUS_MASK				0x00700000
#define		NUM_GPUS_SHIFT				20
#define		MULTI_GPU_TILE_SIZE(x)     		((x) << 24)
#define		MULTI_GPU_TILE_SIZE_MASK		0x03000000
#define		MULTI_GPU_TILE_SIZE_SHIFT		24
#define		ROW_SIZE(x)             		((x) << 28)
#define		ROW_SIZE_MASK				0x30000000
#define		ROW_SIZE_SHIFT				28

#define	GB_TILE_MODE0					0x9910
#       define MICRO_TILE_MODE(x)				((x) << 0)
#              define	ADDR_SURF_DISPLAY_MICRO_TILING		0
#              define	ADDR_SURF_THIN_MICRO_TILING		1
#              define	ADDR_SURF_DEPTH_MICRO_TILING		2
#       define ARRAY_MODE(x)					((x) << 2)
#              define	ARRAY_LINEAR_GENERAL			0
#              define	ARRAY_LINEAR_ALIGNED			1
#              define	ARRAY_1D_TILED_THIN1			2
#              define	ARRAY_2D_TILED_THIN1			4
#       define PIPE_CONFIG(x)					((x) << 6)
#              define	ADDR_SURF_P2				0
#              define	ADDR_SURF_P4_8x16			4
#              define	ADDR_SURF_P4_16x16			5
#              define	ADDR_SURF_P4_16x32			6
#              define	ADDR_SURF_P4_32x32			7
#              define	ADDR_SURF_P8_16x16_8x16			8
#              define	ADDR_SURF_P8_16x32_8x16			9
#              define	ADDR_SURF_P8_32x32_8x16			10
#              define	ADDR_SURF_P8_16x32_16x16		11
#              define	ADDR_SURF_P8_32x32_16x16		12
#              define	ADDR_SURF_P8_32x32_16x32		13
#              define	ADDR_SURF_P8_32x64_32x32		14
#       define TILE_SPLIT(x)					((x) << 11)
#              define	ADDR_SURF_TILE_SPLIT_64B		0
#              define	ADDR_SURF_TILE_SPLIT_128B		1
#              define	ADDR_SURF_TILE_SPLIT_256B		2
#              define	ADDR_SURF_TILE_SPLIT_512B		3
#              define	ADDR_SURF_TILE_SPLIT_1KB		4
#              define	ADDR_SURF_TILE_SPLIT_2KB		5
#              define	ADDR_SURF_TILE_SPLIT_4KB		6
#       define BANK_WIDTH(x)					((x) << 14)
#              define	ADDR_SURF_BANK_WIDTH_1			0
#              define	ADDR_SURF_BANK_WIDTH_2			1
#              define	ADDR_SURF_BANK_WIDTH_4			2
#              define	ADDR_SURF_BANK_WIDTH_8			3
#       define BANK_HEIGHT(x)					((x) << 16)
#              define	ADDR_SURF_BANK_HEIGHT_1			0
#              define	ADDR_SURF_BANK_HEIGHT_2			1
#              define	ADDR_SURF_BANK_HEIGHT_4			2
#              define	ADDR_SURF_BANK_HEIGHT_8			3
#       define MACRO_TILE_ASPECT(x)				((x) << 18)
#              define	ADDR_SURF_MACRO_ASPECT_1		0
#              define	ADDR_SURF_MACRO_ASPECT_2		1
#              define	ADDR_SURF_MACRO_ASPECT_4		2
#              define	ADDR_SURF_MACRO_ASPECT_8		3
#       define NUM_BANKS(x)					((x) << 20)
#              define	ADDR_SURF_2_BANK			0
#              define	ADDR_SURF_4_BANK			1
#              define	ADDR_SURF_8_BANK			2
#              define	ADDR_SURF_16_BANK			3

#define	CB_PERFCOUNTER0_SELECT0				0x9a20
#define	CB_PERFCOUNTER0_SELECT1				0x9a24
#define	CB_PERFCOUNTER1_SELECT0				0x9a28
#define	CB_PERFCOUNTER1_SELECT1				0x9a2c
#define	CB_PERFCOUNTER2_SELECT0				0x9a30
#define	CB_PERFCOUNTER2_SELECT1				0x9a34
#define	CB_PERFCOUNTER3_SELECT0				0x9a38
#define	CB_PERFCOUNTER3_SELECT1				0x9a3c

#define	GC_USER_RB_BACKEND_DISABLE			0x9B7C
#define		BACKEND_DISABLE_MASK			0x00FF0000
#define		BACKEND_DISABLE_SHIFT			16

#define	TCP_CHAN_STEER_LO				0xac0c
#define	TCP_CHAN_STEER_HI				0xac10

#define	CP_RB0_BASE					0xC100
#define	CP_RB0_CNTL					0xC104
#define		RB_BUFSZ(x)					((x) << 0)
#define		RB_BLKSZ(x)					((x) << 8)
#define		BUF_SWAP_32BIT					(2 << 16)
#define		RB_NO_UPDATE					(1 << 27)
#define		RB_RPTR_WR_ENA					(1 << 31)

#define	CP_RB0_RPTR_ADDR				0xC10C
#define	CP_RB0_RPTR_ADDR_HI				0xC110
#define	CP_RB0_WPTR					0xC114

#define	CP_PFP_UCODE_ADDR				0xC150
#define	CP_PFP_UCODE_DATA				0xC154
#define	CP_ME_RAM_RADDR					0xC158
#define	CP_ME_RAM_WADDR					0xC15C
#define	CP_ME_RAM_DATA					0xC160

#define	CP_CE_UCODE_ADDR				0xC168
#define	CP_CE_UCODE_DATA				0xC16C

#define	CP_RB1_BASE					0xC180
#define	CP_RB1_CNTL					0xC184
#define	CP_RB1_RPTR_ADDR				0xC188
#define	CP_RB1_RPTR_ADDR_HI				0xC18C
#define	CP_RB1_WPTR					0xC190
#define	CP_RB2_BASE					0xC194
#define	CP_RB2_CNTL					0xC198
#define	CP_RB2_RPTR_ADDR				0xC19C
#define	CP_RB2_RPTR_ADDR_HI				0xC1A0
#define	CP_RB2_WPTR					0xC1A4
#define CP_INT_CNTL_RING0                               0xC1A8
#define CP_INT_CNTL_RING1                               0xC1AC
#define CP_INT_CNTL_RING2                               0xC1B0
#       define CNTX_BUSY_INT_ENABLE                     (1 << 19)
#       define CNTX_EMPTY_INT_ENABLE                    (1 << 20)
#       define WAIT_MEM_SEM_INT_ENABLE                  (1 << 21)
#       define TIME_STAMP_INT_ENABLE                    (1 << 26)
#       define CP_RINGID2_INT_ENABLE                    (1 << 29)
#       define CP_RINGID1_INT_ENABLE                    (1 << 30)
#       define CP_RINGID0_INT_ENABLE                    (1 << 31)
#define CP_INT_STATUS_RING0                             0xC1B4
#define CP_INT_STATUS_RING1                             0xC1B8
#define CP_INT_STATUS_RING2                             0xC1BC
#       define WAIT_MEM_SEM_INT_STAT                    (1 << 21)
#       define TIME_STAMP_INT_STAT                      (1 << 26)
#       define CP_RINGID2_INT_STAT                      (1 << 29)
#       define CP_RINGID1_INT_STAT                      (1 << 30)
#       define CP_RINGID0_INT_STAT                      (1 << 31)

#define	CP_DEBUG					0xC1FC

#define RLC_CNTL                                          0xC300
#       define RLC_ENABLE                                 (1 << 0)
#define RLC_RL_BASE                                       0xC304
#define RLC_RL_SIZE                                       0xC308
#define RLC_LB_CNTL                                       0xC30C
#define RLC_SAVE_AND_RESTORE_BASE                         0xC310
#define RLC_LB_CNTR_MAX                                   0xC314
#define RLC_LB_CNTR_INIT                                  0xC318

#define RLC_CLEAR_STATE_RESTORE_BASE                      0xC320

#define RLC_UCODE_ADDR                                    0xC32C
#define RLC_UCODE_DATA                                    0xC330

#define RLC_GPU_CLOCK_COUNT_LSB                           0xC338
#define RLC_GPU_CLOCK_COUNT_MSB                           0xC33C
#define RLC_CAPTURE_GPU_CLOCK_COUNT                       0xC340
#define RLC_MC_CNTL                                       0xC344
#define RLC_UCODE_CNTL                                    0xC348

#define PA_SC_RASTER_CONFIG                             0x28350
#       define RASTER_CONFIG_RB_MAP_0                   0
#       define RASTER_CONFIG_RB_MAP_1                   1
#       define RASTER_CONFIG_RB_MAP_2                   2
#       define RASTER_CONFIG_RB_MAP_3                   3

#define VGT_EVENT_INITIATOR                             0x28a90
#       define SAMPLE_STREAMOUTSTATS1                   (1 << 0)
#       define SAMPLE_STREAMOUTSTATS2                   (2 << 0)
#       define SAMPLE_STREAMOUTSTATS3                   (3 << 0)
#       define CACHE_FLUSH_TS                           (4 << 0)
#       define CACHE_FLUSH                              (6 << 0)
#       define CS_PARTIAL_FLUSH                         (7 << 0)
#       define VGT_STREAMOUT_RESET                      (10 << 0)
#       define END_OF_PIPE_INCR_DE                      (11 << 0)
#       define END_OF_PIPE_IB_END                       (12 << 0)
#       define RST_PIX_CNT                              (13 << 0)
#       define VS_PARTIAL_FLUSH                         (15 << 0)
#       define PS_PARTIAL_FLUSH                         (16 << 0)
#       define CACHE_FLUSH_AND_INV_TS_EVENT             (20 << 0)
#       define ZPASS_DONE                               (21 << 0)
#       define CACHE_FLUSH_AND_INV_EVENT                (22 << 0)
#       define PERFCOUNTER_START                        (23 << 0)
#       define PERFCOUNTER_STOP                         (24 << 0)
#       define PIPELINESTAT_START                       (25 << 0)
#       define PIPELINESTAT_STOP                        (26 << 0)
#       define PERFCOUNTER_SAMPLE                       (27 << 0)
#       define SAMPLE_PIPELINESTAT                      (30 << 0)
#       define SAMPLE_STREAMOUTSTATS                    (32 << 0)
#       define RESET_VTX_CNT                            (33 << 0)
#       define VGT_FLUSH                                (36 << 0)
#       define BOTTOM_OF_PIPE_TS                        (40 << 0)
#       define DB_CACHE_FLUSH_AND_INV                   (42 << 0)
#       define FLUSH_AND_INV_DB_DATA_TS                 (43 << 0)
#       define FLUSH_AND_INV_DB_META                    (44 << 0)
#       define FLUSH_AND_INV_CB_DATA_TS                 (45 << 0)
#       define FLUSH_AND_INV_CB_META                    (46 << 0)
#       define CS_DONE                                  (47 << 0)
#       define PS_DONE                                  (48 << 0)
#       define FLUSH_AND_INV_CB_PIXEL_DATA              (49 << 0)
#       define THREAD_TRACE_START                       (51 << 0)
#       define THREAD_TRACE_STOP                        (52 << 0)
#       define THREAD_TRACE_FLUSH                       (54 << 0)
#       define THREAD_TRACE_FINISH                      (55 << 0)

/*
 * PM4
 */
#define	PACKET_TYPE0	0
#define	PACKET_TYPE1	1
#define	PACKET_TYPE2	2
#define	PACKET_TYPE3	3

#define CP_PACKET_GET_TYPE(h) (((h) >> 30) & 3)
#define CP_PACKET_GET_COUNT(h) (((h) >> 16) & 0x3FFF)
#define CP_PACKET0_GET_REG(h) (((h) & 0xFFFF) << 2)
#define CP_PACKET3_GET_OPCODE(h) (((h) >> 8) & 0xFF)
#define PACKET0(reg, n)	((PACKET_TYPE0 << 30) |				\
			 (((reg) >> 2) & 0xFFFF) |			\
			 ((n) & 0x3FFF) << 16)
#define CP_PACKET2			0x80000000
#define		PACKET2_PAD_SHIFT		0
#define		PACKET2_PAD_MASK		(0x3fffffff << 0)

#define PACKET2(v)	(CP_PACKET2 | REG_SET(PACKET2_PAD, (v)))

#define PACKET3(op, n)	((PACKET_TYPE3 << 30) |				\
			 (((op) & 0xFF) << 8) |				\
			 ((n) & 0x3FFF) << 16)

#define PACKET3_COMPUTE(op, n) (PACKET3(op, n) | 1 << 1)

/* Packet 3 types */
#define	PACKET3_NOP					0x10
#define	PACKET3_SET_BASE				0x11
#define		PACKET3_BASE_INDEX(x)                  ((x) << 0)
#define			GDS_PARTITION_BASE		2
#define			CE_PARTITION_BASE		3
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
#define	PACKET3_INDIRECT_BUFFER				0x32
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
#define		EVENT_TYPE(x)                           ((x) << 0)
#define		EVENT_INDEX(x)                          ((x) << 8)
                /* 0 - any non-TS event
		 * 1 - ZPASS_DONE
		 * 2 - SAMPLE_PIPELINESTAT
		 * 3 - SAMPLE_STREAMOUTSTAT*
		 * 4 - *S_PARTIAL_FLUSH
		 * 5 - EOP events
		 * 6 - EOS events
		 * 7 - CACHE_FLUSH, CACHE_FLUSH_AND_INV_EVENT
		 */
#define		INV_L2                                  (1 << 20)
                /* INV TC L2 cache when EVENT_INDEX = 7 */
#define	PACKET3_EVENT_WRITE_EOP				0x47
#define		DATA_SEL(x)                             ((x) << 29)
                /* 0 - discard
		 * 1 - send low 32bit data
		 * 2 - send 64bit data
		 * 3 - send 64bit counter value
		 */
#define		INT_SEL(x)                              ((x) << 24)
                /* 0 - none
		 * 1 - interrupt only (DATA_SEL = 0)
		 * 2 - interrupt when data write is confirmed
		 */
#define	PACKET3_EVENT_WRITE_EOS				0x48
#define	PACKET3_PREAMBLE_CNTL				0x4A
#              define PACKET3_PREAMBLE_BEGIN_CLEAR_STATE     (2 << 28)
#              define PACKET3_PREAMBLE_END_CLEAR_STATE       (3 << 28)
#define	PACKET3_ONE_REG_WRITE				0x57
#define	PACKET3_LOAD_CONFIG_REG				0x5F
#define	PACKET3_LOAD_CONTEXT_REG			0x60
#define	PACKET3_LOAD_SH_REG				0x61
#define	PACKET3_SET_CONFIG_REG				0x68
#define		PACKET3_SET_CONFIG_REG_START			0x00008000
#define		PACKET3_SET_CONFIG_REG_END			0x0000b000
#define	PACKET3_SET_CONTEXT_REG				0x69
#define		PACKET3_SET_CONTEXT_REG_START			0x00028000
#define		PACKET3_SET_CONTEXT_REG_END			0x00029000
#define	PACKET3_SET_CONTEXT_REG_INDIRECT		0x73
#define	PACKET3_SET_RESOURCE_INDIRECT			0x74
#define	PACKET3_SET_SH_REG				0x76
#define		PACKET3_SET_SH_REG_START			0x0000b000
#define		PACKET3_SET_SH_REG_END				0x0000c000
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

#endif
