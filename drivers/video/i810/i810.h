/*-*- linux-c -*-
 *  linux/drivers/video/i810.h -- Intel 810 General Definitions/Declarations
 *
 *      Copyright (C) 2001 Antonino Daplas<adaplas@pol.net>
 *      All Rights Reserved      
 *
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License. See the file COPYING in the main directory of this archive for
 *  more details.
 */

#ifndef __I810_H__
#define __I810_H__

#include <linux/list.h>
#include <linux/agp_backend.h>
#include <linux/fb.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <video/vga.h>

/* Fence */
#define TILEWALK_X            (0 << 12)
#define TILEWALK_Y            (1 << 12)

/* Raster ops */
#define COLOR_COPY_ROP        0xF0
#define PAT_COPY_ROP          0xCC
#define CLEAR_ROP             0x00
#define WHITE_ROP             0xFF
#define INVERT_ROP            0x55
#define XOR_ROP               0x5A

/* 2D Engine definitions */
#define SOLIDPATTERN          0x80000000
#define NONSOLID              0x00000000
#define BPP8                  (0 << 24)
#define BPP16                 (1 << 24)
#define BPP24                 (2 << 24)

#define PIXCONF8              (2 << 16)
#define PIXCONF15             (4 << 16)
#define PIXCONF16             (5 << 16)
#define PIXCONF24             (6 << 16)
#define PIXCONF32             (7 << 16)

#define DYN_COLOR_EN          (1 << 26)
#define DYN_COLOR_DIS         (0 << 26)
#define INCREMENT             0x00000000
#define DECREMENT             (0x01 << 30)
#define ARB_ON                0x00000001
#define ARB_OFF               0x00000000
#define SYNC_FLIP             0x00000000
#define ASYNC_FLIP            0x00000040
#define OPTYPE_MASK           0xE0000000
#define PARSER_MASK           0x001F8000 
#define D2_MASK               0x001FC000         /* 2D mask */

/* Instruction type */
/* There are more but pertains to 3D */
#define PARSER                0x00000000
#define BLIT                  (0x02 << 29)
#define RENDER                (0x03 << 29)
            
/* Parser */
#define NOP                   0x00               /* No operation, padding */
#define BP_INT                (0x01 << 23)         /* Breakpoint interrupt */
#define USR_INT               (0x02 << 23)         /* User interrupt */
#define WAIT_FOR_EVNT         (0x03 << 23)         /* Wait for event */
#define FLUSH                 (0x04 << 23)              
#define CONTEXT_SEL           (0x05 << 23)
#define REPORT_HEAD           (0x07 << 23)
#define ARB_ON_OFF            (0x08 << 23)
#define OVERLAY_FLIP          (0x11 << 23)
#define LOAD_SCAN_INC         (0x12 << 23)
#define LOAD_SCAN_EX          (0x13 << 23)
#define FRONT_BUFFER          (0x14 << 23)
#define DEST_BUFFER           (0x15 << 23)
#define Z_BUFFER              (0x16 << 23)       

#define STORE_DWORD_IMM       (0x20 << 23)
#define STORE_DWORD_IDX       (0x21 << 23)
#define BATCH_BUFFER          (0x30 << 23)

/* Blit */
#define SETUP_BLIT                      0x00
#define SETUP_MONO_PATTERN_SL_BLT       (0x10 << 22)
#define PIXEL_BLT                       (0x20 << 22)
#define SCANLINE_BLT                    (0x21 << 22)
#define TEXT_BLT                        (0x22 << 22)
#define TEXT_IMM_BLT                    (0x30 << 22)
#define COLOR_BLT                       (0x40 << 22)
#define MONO_PAT_BLIT                   (0x42 << 22)
#define SOURCE_COPY_BLIT                (0x43 << 22)
#define MONO_SOURCE_COPY_BLIT           (0x44 << 22)
#define SOURCE_COPY_IMMEDIATE           (0x60 << 22)
#define MONO_SOURCE_COPY_IMMEDIATE      (0x61 << 22)

#define VERSION_MAJOR            0
#define VERSION_MINOR            9
#define VERSION_TEENIE           0
#define BRANCH_VERSION           ""


/* mvo: intel i815 */
#ifndef PCI_DEVICE_ID_INTEL_82815_100
  #define PCI_DEVICE_ID_INTEL_82815_100           0x1102
#endif
#ifndef PCI_DEVICE_ID_INTEL_82815_NOAGP
  #define PCI_DEVICE_ID_INTEL_82815_NOAGP         0x1112
#endif
#ifndef PCI_DEVICE_ID_INTEL_82815_FULL_CTRL
  #define PCI_DEVICE_ID_INTEL_82815_FULL_CTRL     0x1130
#endif 

/* General Defines */
#define I810_PAGESIZE               4096
#define MAX_DMA_SIZE                (1024 * 4096)
#define SAREA_SIZE                  4096
#define PCI_I810_MISCC              0x72
#define MMIO_SIZE                   (512*1024)
#define GTT_SIZE                    (16*1024) 
#define RINGBUFFER_SIZE             (64*1024)
#define CURSOR_SIZE                 4096 
#define OFF                         0
#define ON                          1
#define MAX_KEY                     256
#define WAIT_COUNT                  10000000
#define IRING_PAD                   8
#define FONTDATAMAX                 8192
/* Masks (AND ops) and OR's */
#define FB_START_MASK               (0x3f << (32 - 6))
#define MMIO_ADDR_MASK              (0x1FFF << (32 - 13))
#define FREQ_MASK                   (1 << 4)
#define SCR_OFF                     0x20
#define DRAM_ON                     0x08            
#define DRAM_OFF                    0xE7
#define PG_ENABLE_MASK              0x01
#define RING_SIZE_MASK              (RINGBUFFER_SIZE - 1)

/* defines for restoring registers partially */
#define ADDR_MAP_MASK               (0x07 << 5)
#define DISP_CTRL                   ~0
#define PIXCONF_0                   (0x64 << 8)
#define PIXCONF_2                   (0xF3 << 24)
#define PIXCONF_1                   (0xF0 << 16)
#define MN_MASK                     0x3FF03FF
#define P_OR                        (0x7 << 4)                    
#define DAC_BIT                     (1 << 16)
#define INTERLACE_BIT               (1 << 7)
#define IER_MASK                    (3 << 13)
#define IMR_MASK                    (3 << 13)

/* Power Management */
#define DPMS_MASK                   0xF0000
#define POWERON                     0x00000
#define STANDBY                     0x20000
#define SUSPEND                     0x80000
#define POWERDOWN                   0xA0000
#define EMR_MASK                    ~0x3F
#define FW_BLC_MASK                 ~(0x3F|(7 << 8)|(0x3F << 12)|(7 << 20))

/* Ringbuffer */
#define RBUFFER_START_MASK          0xFFFFF000
#define RBUFFER_SIZE_MASK           0x001FF000
#define RBUFFER_HEAD_MASK           0x001FFFFC
#define RBUFFER_TAIL_MASK           0x001FFFF8

/* Video Timings */
#define REF_FREQ                    24000000
#define TARGET_N_MAX                30

#define MAX_PIXELCLOCK              230000000
#define MIN_PIXELCLOCK               15000000
#define VFMAX                       60
#define VFMIN                       60
#define HFMAX                       30000
#define HFMIN                       29000

/* Cursor */
#define CURSOR_ENABLE_MASK          0x1000             
#define CURSOR_MODE_64_TRANS        4
#define CURSOR_MODE_64_XOR	    5
#define CURSOR_MODE_64_3C	    6	
#define COORD_INACTIVE              0
#define COORD_ACTIVE                (1 << 4)
#define EXTENDED_PALETTE	    1
  
/* AGP Memory Types*/
#define AGP_NORMAL_MEMORY           0
#define AGP_DCACHE_MEMORY	    1
#define AGP_PHYSICAL_MEMORY         2

/* Allocated resource Flags */
#define FRAMEBUFFER_REQ             1
#define MMIO_REQ                    2
#define PCI_DEVICE_ENABLED          4
#define HAS_FONTCACHE               8 

/* driver flags */
#define HAS_MTRR                    1
#define HAS_ACCELERATION            2
#define ALWAYS_SYNC                 4
#define LOCKUP                      8

struct gtt_data {
	struct agp_memory *i810_fb_memory;
	struct agp_memory *i810_cursor_memory;
};

struct mode_registers {
	u32 pixclock, M, N, P;
	u8 cr00, cr01, cr02, cr03;
	u8 cr04, cr05, cr06, cr07;
	u8 cr09, cr10, cr11, cr12;
	u8 cr13, cr15, cr16, cr30;
	u8 cr31, cr32, cr33, cr35, cr39;
	u32 bpp8_100, bpp16_100;
	u32 bpp24_100, bpp8_133;
	u32 bpp16_133, bpp24_133;
	u8 msr;
};

struct heap_data {
        unsigned long physical;
	__u8 __iomem *virtual;
	u32 offset;
	u32 size;
};	

struct state_registers {
	u32 dclk_1d, dclk_2d, dclk_0ds;
	u32 pixconf, fw_blc, pgtbl_ctl;
	u32 fence0, hws_pga, dplystas;
	u16 bltcntl, hwstam, ier, iir, imr;
	u8 cr00, cr01, cr02, cr03, cr04;
	u8 cr05, cr06, cr07, cr08, cr09;
	u8 cr10, cr11, cr12, cr13, cr14;
	u8 cr15, cr16, cr17, cr80, gr10;
	u8 cr30, cr31, cr32, cr33, cr35;
	u8 cr39, cr41, cr70, sr01, msr;
};

struct i810fb_par;

struct i810fb_i2c_chan {
	struct i810fb_par *par;
	struct i2c_adapter adapter;
	struct i2c_algo_bit_data algo;
	unsigned long ddc_base;
};

struct i810fb_par {
	struct mode_registers    regs;
	struct state_registers   hw_state;
	struct gtt_data          i810_gtt;
	struct fb_ops            i810fb_ops;
	struct pci_dev           *dev;
	struct heap_data         aperture;
	struct heap_data         fb;
	struct heap_data         iring;
	struct heap_data         cursor_heap;
	struct vgastate          state;
	struct i810fb_i2c_chan   chan[3];
	struct mutex		 open_lock;
	unsigned int		 use_count;
	u32 pseudo_palette[16];
	unsigned long mmio_start_phys;
	u8 __iomem *mmio_start_virtual;
	u8 *edid;
	u32 pitch;
	u32 pixconf;
	u32 watermark;
	u32 mem_freq;
	u32 res_flags;
	u32 dev_flags;
	u32 cur_tail;
	u32 depth;
	u32 blit_bpp;
	u32 ovract;
	u32 cur_state;
	u32 ddc_num;
	int mtrr_reg;
	u16 bltcntl;
	u8 interlace;
};

/* 
 * Register I/O
 */
#define i810_readb(where, mmio) readb(mmio + where)
#define i810_readw(where, mmio) readw(mmio + where)
#define i810_readl(where, mmio) readl(mmio + where)
#define i810_writeb(where, mmio, val) writeb(val, mmio + where) 
#define i810_writew(where, mmio, val) writew(val, mmio + where)
#define i810_writel(where, mmio, val) writel(val, mmio + where)

#endif /* __I810_H__ */
