#ifndef _INTELFB_H
#define _INTELFB_H

/* $DHD: intelfb/intelfb.h,v 1.40 2003/06/27 15:06:25 dawes Exp $ */

#include <linux/agp_backend.h>
#include <linux/fb.h>

#ifdef CONFIG_FB_INTEL_I2C
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#endif

/*** Version/name ***/
#define INTELFB_VERSION			"0.9.4"
#define INTELFB_MODULE_NAME		"intelfb"
#define SUPPORTED_CHIPSETS		"830M/845G/852GM/855GM/865G/915G/915GM/945G/945GM"


/*** Debug/feature defines ***/

#ifndef DEBUG
#define DEBUG				0
#endif

#ifndef VERBOSE
#define VERBOSE				0
#endif

#ifndef REGDUMP
#define REGDUMP				0
#endif

#ifndef DETECT_VGA_CLASS_ONLY
#define DETECT_VGA_CLASS_ONLY		1
#endif

#ifndef ALLOCATE_FOR_PANNING
#define ALLOCATE_FOR_PANNING		1
#endif

#ifndef PREFERRED_MODE
#define PREFERRED_MODE			"1024x768-32@70"
#endif

/*** hw-related values ***/

/* Resource Allocation */
#define INTELFB_FB_ACQUIRED                 1
#define INTELFB_MMIO_ACQUIRED               2

/* PCI ids for supported devices */
#define PCI_DEVICE_ID_INTEL_830M	0x3577
#define PCI_DEVICE_ID_INTEL_845G	0x2562
#define PCI_DEVICE_ID_INTEL_85XGM	0x3582
#define PCI_DEVICE_ID_INTEL_865G	0x2572
#define PCI_DEVICE_ID_INTEL_915G	0x2582
#define PCI_DEVICE_ID_INTEL_915GM	0x2592
#define PCI_DEVICE_ID_INTEL_945G	0x2772
#define PCI_DEVICE_ID_INTEL_945GM	0x27A2

/* Size of MMIO region */
#define INTEL_REG_SIZE			0x80000

#define STRIDE_ALIGNMENT		16
#define STRIDE_ALIGNMENT_I9XX		64

#define PALETTE_8_ENTRIES		256


/*** Macros ***/

/* basic arithmetic */
#define KB(x)			((x) * 1024)
#define MB(x)			((x) * 1024 * 1024)
#define BtoKB(x)		((x) / 1024)
#define BtoMB(x)		((x) / 1024 / 1024)

#define GTT_PAGE_SIZE           KB(4)

#define ROUND_UP_TO(x, y)	(((x) + (y) - 1) / (y) * (y))
#define ROUND_DOWN_TO(x, y)	((x) / (y) * (y))
#define ROUND_UP_TO_PAGE(x)	ROUND_UP_TO((x), GTT_PAGE_SIZE)
#define ROUND_DOWN_TO_PAGE(x)	ROUND_DOWN_TO((x), GTT_PAGE_SIZE)

/* messages */
#define PFX			INTELFB_MODULE_NAME ": "

#define ERR_MSG(fmt, args...)	printk(KERN_ERR PFX fmt, ## args)
#define WRN_MSG(fmt, args...)	printk(KERN_WARNING PFX fmt, ## args)
#define NOT_MSG(fmt, args...)	printk(KERN_NOTICE PFX fmt, ## args)
#define INF_MSG(fmt, args...)	printk(KERN_INFO PFX fmt, ## args)
#if DEBUG
#define DBG_MSG(fmt, args...)	printk(KERN_DEBUG PFX fmt, ## args)
#else
#define DBG_MSG(fmt, args...)	while (0) printk(fmt, ## args)
#endif

/* get commonly used pointers */
#define GET_DINFO(info)		(info)->par

/* misc macros */
#define ACCEL(d, i)                                                     \
	((d)->accel && !(d)->ring_lockup &&                             \
	 ((i)->var.accel_flags & FB_ACCELF_TEXT))

/*#define NOACCEL_CHIPSET(d)						\
	((d)->chipset != INTEL_865G)*/
#define NOACCEL_CHIPSET(d)						\
	(0)

#define FIXED_MODE(d) ((d)->fixed_mode)

/*** Driver paramters ***/

#define RINGBUFFER_SIZE		KB(64)
#define HW_CURSOR_SIZE		KB(4)

/* Intel agpgart driver */
#define AGP_PHYSICAL_MEMORY     2

/* store information about an Ixxx DVO */
/* The i830->i865 use multiple DVOs with multiple i2cs */
/* the i915, i945 have a single sDVO i2c bus - which is different */
#define MAX_OUTPUTS 6

/* these are outputs from the chip - integrated only
   external chips are via DVO or SDVO output */
#define INTELFB_OUTPUT_UNUSED 0
#define INTELFB_OUTPUT_ANALOG 1
#define INTELFB_OUTPUT_DVO 2
#define INTELFB_OUTPUT_SDVO 3
#define INTELFB_OUTPUT_LVDS 4
#define INTELFB_OUTPUT_TVOUT 5

#define INTELFB_DVO_CHIP_NONE 0
#define INTELFB_DVO_CHIP_LVDS 1
#define INTELFB_DVO_CHIP_TMDS 2
#define INTELFB_DVO_CHIP_TVOUT 4

#define INTELFB_OUTPUT_PIPE_NC  0
#define INTELFB_OUTPUT_PIPE_A   1
#define INTELFB_OUTPUT_PIPE_B   2

/*** Data Types ***/

/* supported chipsets */
enum intel_chips {
	INTEL_830M,
	INTEL_845G,
	INTEL_85XGM,
	INTEL_852GM,
	INTEL_852GME,
	INTEL_855GM,
	INTEL_855GME,
	INTEL_865G,
	INTEL_915G,
	INTEL_915GM,
	INTEL_945G,
	INTEL_945GM,
};

struct intelfb_hwstate {
	u32 vga0_divisor;
	u32 vga1_divisor;
	u32 vga_pd;
	u32 dpll_a;
	u32 dpll_b;
	u32 fpa0;
	u32 fpa1;
	u32 fpb0;
	u32 fpb1;
	u32 palette_a[PALETTE_8_ENTRIES];
	u32 palette_b[PALETTE_8_ENTRIES];
	u32 htotal_a;
	u32 hblank_a;
	u32 hsync_a;
	u32 vtotal_a;
	u32 vblank_a;
	u32 vsync_a;
	u32 src_size_a;
	u32 bclrpat_a;
	u32 htotal_b;
	u32 hblank_b;
	u32 hsync_b;
	u32 vtotal_b;
	u32 vblank_b;
	u32 vsync_b;
	u32 src_size_b;
	u32 bclrpat_b;
	u32 adpa;
	u32 dvoa;
	u32 dvob;
	u32 dvoc;
	u32 dvoa_srcdim;
	u32 dvob_srcdim;
	u32 dvoc_srcdim;
	u32 lvds;
	u32 pipe_a_conf;
	u32 pipe_b_conf;
	u32 disp_arb;
	u32 cursor_a_control;
	u32 cursor_b_control;
	u32 cursor_a_base;
	u32 cursor_b_base;
	u32 cursor_size;
	u32 disp_a_ctrl;
	u32 disp_b_ctrl;
	u32 disp_a_base;
	u32 disp_b_base;
	u32 cursor_a_palette[4];
	u32 cursor_b_palette[4];
	u32 disp_a_stride;
	u32 disp_b_stride;
	u32 vgacntrl;
	u32 add_id;
	u32 swf0x[7];
	u32 swf1x[7];
	u32 swf3x[3];
	u32 fence[8];
	u32 instpm;
	u32 mem_mode;
	u32 fw_blc_0;
	u32 fw_blc_1;
	u16 hwstam;
	u16 ier;
	u16 iir;
	u16 imr;
};

struct intelfb_heap_data {
	u32 physical;
	u8 __iomem *virtual;
	u32 offset;		/* in GATT pages */
	u32 size;		/* in bytes */
};

#ifdef CONFIG_FB_INTEL_I2C
struct intelfb_i2c_chan {
    struct intelfb_info *dinfo;
    u32 reg;
    struct i2c_adapter adapter;
    struct i2c_algo_bit_data algo;
};
#endif

struct intelfb_output_rec {
    int type;
    int pipe;
    int flags;

#ifdef CONFIG_FB_INTEL_I2C
    struct intelfb_i2c_chan i2c_bus;
    struct intelfb_i2c_chan ddc_bus;
#endif
};

struct intelfb_vsync {
	wait_queue_head_t wait;
	unsigned int count;
	int pan_display;
	u32 pan_offset;
};

struct intelfb_info {
	struct fb_info *info;
	struct fb_ops  *fbops;
	struct pci_dev *pdev;

	struct intelfb_hwstate save_state;

	/* agpgart structs */
	struct agp_memory *gtt_fb_mem;     /* use all stolen memory or vram */
	struct agp_memory *gtt_ring_mem;   /* ring buffer */
	struct agp_memory *gtt_cursor_mem; /* hw cursor */

	/* use a gart reserved fb mem */
	u8 fbmem_gart;

	/* mtrr support */
	int mtrr_reg;
	u32 has_mtrr;

	/* heap data */
	struct intelfb_heap_data aperture;
	struct intelfb_heap_data fb;
	struct intelfb_heap_data ring;
	struct intelfb_heap_data cursor;

	/* mmio regs */
	u32 mmio_base_phys;
	u8 __iomem *mmio_base;

	/* fb start offset (in bytes) */
	u32 fb_start;

	/* ring buffer */
	u32 ring_head;
	u32 ring_tail;
	u32 ring_tail_mask;
	u32 ring_space;
	u32 ring_lockup;

	/* palette */
	u32 pseudo_palette[16];

	/* chip info */
	int pci_chipset;
	int chipset;
	const char *name;
	int mobile;

	/* current mode */
	int bpp, depth;
	u32 visual;
	int xres, yres, pitch;
	int pixclock;

	/* current pipe */
	int pipe;

	/* some flags */
	int accel;
	int hwcursor;
	int fixed_mode;
	int ring_active;
	int flag;
	unsigned long irq_flags;
	int open;

	/* vsync */
	struct intelfb_vsync vsync;
	spinlock_t int_lock;

	/* hw cursor */
	int cursor_on;
	int cursor_blanked;
	u8  cursor_src[64];

	/* initial parameters */
	int initial_vga;
	struct fb_var_screeninfo initial_var;
	u32 initial_fb_base;
	u32 initial_video_ram;
	u32 initial_pitch;

	/* driver registered */
	int registered;

	/* index into plls */
	int pll_index;

	/* outputs */
	int num_outputs;
	struct intelfb_output_rec output[MAX_OUTPUTS];
};

#define IS_I9XX(dinfo) (((dinfo)->chipset == INTEL_915G) ||	\
			((dinfo)->chipset == INTEL_915GM) ||	\
			((dinfo)->chipset == INTEL_945G) ||	\
			((dinfo)->chipset==INTEL_945GM))

#ifndef FBIO_WAITFORVSYNC
#define FBIO_WAITFORVSYNC	_IOW('F', 0x20, __u32)
#endif

/*** function prototypes ***/

extern int intelfb_var_to_depth(const struct fb_var_screeninfo *var);

#ifdef CONFIG_FB_INTEL_I2C
extern void intelfb_create_i2c_busses(struct intelfb_info *dinfo);
extern void intelfb_delete_i2c_busses(struct intelfb_info *dinfo);
#endif

#endif /* _INTELFB_H */
