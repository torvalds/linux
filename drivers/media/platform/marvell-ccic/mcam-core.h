/*
 * Marvell camera core structures.
 *
 * Copyright 2011 Jonathan Corbet corbet@lwn.net
 */
#ifndef _MCAM_CORE_H
#define _MCAM_CORE_H

#include <linux/list.h>
#include <media/v4l2-common.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-dev.h>
#include <media/videobuf2-v4l2.h>

/*
 * Create our own symbols for the supported buffer modes, but, for now,
 * base them entirely on which videobuf2 options have been selected.
 */
#if IS_ENABLED(CONFIG_VIDEOBUF2_VMALLOC)
#define MCAM_MODE_VMALLOC 1
#endif

#if IS_ENABLED(CONFIG_VIDEOBUF2_DMA_CONTIG)
#define MCAM_MODE_DMA_CONTIG 1
#endif

#if IS_ENABLED(CONFIG_VIDEOBUF2_DMA_SG)
#define MCAM_MODE_DMA_SG 1
#endif

#if !defined(MCAM_MODE_VMALLOC) && !defined(MCAM_MODE_DMA_CONTIG) && \
	!defined(MCAM_MODE_DMA_SG)
#error One of the videobuf buffer modes must be selected in the config
#endif


enum mcam_state {
	S_NOTREADY,	/* Not yet initialized */
	S_IDLE,		/* Just hanging around */
	S_FLAKED,	/* Some sort of problem */
	S_STREAMING,	/* Streaming data */
	S_BUFWAIT	/* streaming requested but no buffers yet */
};
#define MAX_DMA_BUFS 3

/*
 * Different platforms work best with different buffer modes, so we
 * let the platform pick.
 */
enum mcam_buffer_mode {
	B_vmalloc = 0,
	B_DMA_contig = 1,
	B_DMA_sg = 2
};

enum mcam_chip_id {
	MCAM_CAFE,
	MCAM_ARMADA610,
};

/*
 * Is a given buffer mode supported by the current kernel configuration?
 */
static inline int mcam_buffer_mode_supported(enum mcam_buffer_mode mode)
{
	switch (mode) {
#ifdef MCAM_MODE_VMALLOC
	case B_vmalloc:
#endif
#ifdef MCAM_MODE_DMA_CONTIG
	case B_DMA_contig:
#endif
#ifdef MCAM_MODE_DMA_SG
	case B_DMA_sg:
#endif
		return 1;
	default:
		return 0;
	}
}

/*
 * Basic frame states
 */
struct mcam_frame_state {
	unsigned int frames;
	unsigned int singles;
	unsigned int delivered;
};

#define NR_MCAM_CLK 3

/*
 * A description of one of our devices.
 * Locking: controlled by s_mutex.  Certain fields, however, require
 *          the dev_lock spinlock; they are marked as such by comments.
 *          dev_lock is also required for access to device registers.
 */
struct mcam_camera {
	/*
	 * These fields should be set by the platform code prior to
	 * calling mcam_register().
	 */
	struct i2c_adapter *i2c_adapter;
	unsigned char __iomem *regs;
	unsigned regs_size; /* size in bytes of the register space */
	spinlock_t dev_lock;
	struct device *dev; /* For messages, dma alloc */
	enum mcam_chip_id chip_id;
	short int clock_speed;	/* Sensor clock speed, default 30 */
	short int use_smbus;	/* SMBUS or straight I2c? */
	enum mcam_buffer_mode buffer_mode;

	int mclk_min;	/* The minimal value of mclk */
	int mclk_src;	/* which clock source the mclk derives from */
	int mclk_div;	/* Clock Divider Value for MCLK */

	int ccic_id;
	enum v4l2_mbus_type bus_type;
	/* MIPI support */
	/* The dphy config value, allocated in board file
	 * dphy[0]: DPHY3
	 * dphy[1]: DPHY5
	 * dphy[2]: DPHY6
	 */
	int *dphy;
	bool mipi_enabled;	/* flag whether mipi is enabled already */
	int lane;			/* lane number */

	/* clock tree support */
	struct clk *clk[NR_MCAM_CLK];

	/*
	 * Callbacks from the core to the platform code.
	 */
	int (*plat_power_up) (struct mcam_camera *cam);
	void (*plat_power_down) (struct mcam_camera *cam);
	void (*calc_dphy) (struct mcam_camera *cam);
	void (*ctlr_reset) (struct mcam_camera *cam);

	/*
	 * Everything below here is private to the mcam core and
	 * should not be touched by the platform code.
	 */
	struct v4l2_device v4l2_dev;
	struct v4l2_ctrl_handler ctrl_handler;
	enum mcam_state state;
	unsigned long flags;		/* Buffer status, mainly (dev_lock) */

	struct mcam_frame_state frame_state;	/* Frame state counter */
	/*
	 * Subsystem structures.
	 */
	struct video_device vdev;
	struct v4l2_subdev *sensor;
	unsigned short sensor_addr;

	/* Videobuf2 stuff */
	struct vb2_queue vb_queue;
	struct list_head buffers;	/* Available frames */

	unsigned int nbufs;		/* How many are alloc'd */
	int next_buf;			/* Next to consume (dev_lock) */

	char bus_info[32];		/* querycap bus_info */

	/* DMA buffers - vmalloc mode */
#ifdef MCAM_MODE_VMALLOC
	unsigned int dma_buf_size;	/* allocated size */
	void *dma_bufs[MAX_DMA_BUFS];	/* Internal buffer addresses */
	dma_addr_t dma_handles[MAX_DMA_BUFS]; /* Buffer bus addresses */
	struct tasklet_struct s_tasklet;
#endif
	unsigned int sequence;		/* Frame sequence number */
	unsigned int buf_seq[MAX_DMA_BUFS]; /* Sequence for individual bufs */

	/* DMA buffers - DMA modes */
	struct mcam_vb_buffer *vb_bufs[MAX_DMA_BUFS];

	/* Mode-specific ops, set at open time */
	void (*dma_setup)(struct mcam_camera *cam);
	void (*frame_complete)(struct mcam_camera *cam, int frame);

	/* Current operating parameters */
	struct v4l2_pix_format pix_format;
	u32 mbus_code;

	/* Locks */
	struct mutex s_mutex; /* Access to this structure */
};


/*
 * Register I/O functions.  These are here because the platform code
 * may legitimately need to mess with the register space.
 */
/*
 * Device register I/O
 */
static inline void mcam_reg_write(struct mcam_camera *cam, unsigned int reg,
		unsigned int val)
{
	iowrite32(val, cam->regs + reg);
}

static inline unsigned int mcam_reg_read(struct mcam_camera *cam,
		unsigned int reg)
{
	return ioread32(cam->regs + reg);
}


static inline void mcam_reg_write_mask(struct mcam_camera *cam, unsigned int reg,
		unsigned int val, unsigned int mask)
{
	unsigned int v = mcam_reg_read(cam, reg);

	v = (v & ~mask) | (val & mask);
	mcam_reg_write(cam, reg, v);
}

static inline void mcam_reg_clear_bit(struct mcam_camera *cam,
		unsigned int reg, unsigned int val)
{
	mcam_reg_write_mask(cam, reg, 0, val);
}

static inline void mcam_reg_set_bit(struct mcam_camera *cam,
		unsigned int reg, unsigned int val)
{
	mcam_reg_write_mask(cam, reg, val, val);
}

/*
 * Functions for use by platform code.
 */
int mccic_register(struct mcam_camera *cam);
int mccic_irq(struct mcam_camera *cam, unsigned int irqs);
void mccic_shutdown(struct mcam_camera *cam);
#ifdef CONFIG_PM
void mccic_suspend(struct mcam_camera *cam);
int mccic_resume(struct mcam_camera *cam);
#endif

/*
 * Register definitions for the m88alp01 camera interface.  Offsets in bytes
 * as given in the spec.
 */
#define REG_Y0BAR	0x00
#define REG_Y1BAR	0x04
#define REG_Y2BAR	0x08
#define REG_U0BAR	0x0c
#define REG_U1BAR	0x10
#define REG_U2BAR	0x14
#define REG_V0BAR	0x18
#define REG_V1BAR	0x1C
#define REG_V2BAR	0x20

/*
 * register definitions for MIPI support
 */
#define REG_CSI2_CTRL0	0x100
#define   CSI2_C0_MIPI_EN (0x1 << 0)
#define   CSI2_C0_ACT_LANE(n) ((n-1) << 1)
#define REG_CSI2_DPHY3	0x12c
#define REG_CSI2_DPHY5	0x134
#define REG_CSI2_DPHY6	0x138

/* ... */

#define REG_IMGPITCH	0x24	/* Image pitch register */
#define   IMGP_YP_SHFT	  2		/* Y pitch params */
#define   IMGP_YP_MASK	  0x00003ffc	/* Y pitch field */
#define	  IMGP_UVP_SHFT	  18		/* UV pitch (planar) */
#define   IMGP_UVP_MASK   0x3ffc0000
#define REG_IRQSTATRAW	0x28	/* RAW IRQ Status */
#define   IRQ_EOF0	  0x00000001	/* End of frame 0 */
#define   IRQ_EOF1	  0x00000002	/* End of frame 1 */
#define   IRQ_EOF2	  0x00000004	/* End of frame 2 */
#define   IRQ_SOF0	  0x00000008	/* Start of frame 0 */
#define   IRQ_SOF1	  0x00000010	/* Start of frame 1 */
#define   IRQ_SOF2	  0x00000020	/* Start of frame 2 */
#define   IRQ_OVERFLOW	  0x00000040	/* FIFO overflow */
#define   IRQ_TWSIW	  0x00010000	/* TWSI (smbus) write */
#define   IRQ_TWSIR	  0x00020000	/* TWSI read */
#define   IRQ_TWSIE	  0x00040000	/* TWSI error */
#define   TWSIIRQS (IRQ_TWSIW|IRQ_TWSIR|IRQ_TWSIE)
#define   FRAMEIRQS (IRQ_EOF0|IRQ_EOF1|IRQ_EOF2|IRQ_SOF0|IRQ_SOF1|IRQ_SOF2)
#define   ALLIRQS (TWSIIRQS|FRAMEIRQS|IRQ_OVERFLOW)
#define REG_IRQMASK	0x2c	/* IRQ mask - same bits as IRQSTAT */
#define REG_IRQSTAT	0x30	/* IRQ status / clear */

#define REG_IMGSIZE	0x34	/* Image size */
#define  IMGSZ_V_MASK	  0x1fff0000
#define  IMGSZ_V_SHIFT	  16
#define	 IMGSZ_H_MASK	  0x00003fff
#define REG_IMGOFFSET	0x38	/* IMage offset */

#define REG_CTRL0	0x3c	/* Control 0 */
#define   C0_ENABLE	  0x00000001	/* Makes the whole thing go */

/* Mask for all the format bits */
#define   C0_DF_MASK	  0x00fffffc    /* Bits 2-23 */

/* RGB ordering */
#define	  C0_RGB4_RGBX	  0x00000000
#define	  C0_RGB4_XRGB	  0x00000004
#define	  C0_RGB4_BGRX	  0x00000008
#define	  C0_RGB4_XBGR	  0x0000000c
#define	  C0_RGB5_RGGB	  0x00000000
#define	  C0_RGB5_GRBG	  0x00000004
#define	  C0_RGB5_GBRG	  0x00000008
#define	  C0_RGB5_BGGR	  0x0000000c

/* Spec has two fields for DIN and DOUT, but they must match, so
   combine them here. */
#define	  C0_DF_YUV	  0x00000000	/* Data is YUV	    */
#define	  C0_DF_RGB	  0x000000a0	/* ... RGB		    */
#define	  C0_DF_BAYER	  0x00000140	/* ... Bayer		    */
/* 8-8-8 must be missing from the below - ask */
#define	  C0_RGBF_565	  0x00000000
#define	  C0_RGBF_444	  0x00000800
#define	  C0_RGB_BGR	  0x00001000	/* Blue comes first */
#define	  C0_YUV_PLANAR	  0x00000000	/* YUV 422 planar format */
#define	  C0_YUV_PACKED	  0x00008000	/* YUV 422 packed	*/
#define	  C0_YUV_420PL	  0x0000a000	/* YUV 420 planar	*/
/* Think that 420 packed must be 111 - ask */
#define	  C0_YUVE_YUYV	  0x00000000	/* Y1CbY0Cr		*/
#define	  C0_YUVE_YVYU	  0x00010000	/* Y1CrY0Cb		*/
#define	  C0_YUVE_VYUY	  0x00020000	/* CrY1CbY0		*/
#define	  C0_YUVE_UYVY	  0x00030000	/* CbY1CrY0		*/
#define	  C0_YUVE_NOSWAP  0x00000000	/* no bytes swapping	*/
#define	  C0_YUVE_SWAP13  0x00010000	/* swap byte 1 and 3	*/
#define	  C0_YUVE_SWAP24  0x00020000	/* swap byte 2 and 4	*/
#define	  C0_YUVE_SWAP1324 0x00030000	/* swap bytes 1&3 and 2&4 */
/* Bayer bits 18,19 if needed */
#define	  C0_EOF_VSYNC	  0x00400000	/* Generate EOF by VSYNC */
#define	  C0_VEDGE_CTRL   0x00800000	/* Detect falling edge of VSYNC */
#define	  C0_HPOL_LOW	  0x01000000	/* HSYNC polarity active low */
#define	  C0_VPOL_LOW	  0x02000000	/* VSYNC polarity active low */
#define	  C0_VCLK_LOW	  0x04000000	/* VCLK on falling edge */
#define	  C0_DOWNSCALE	  0x08000000	/* Enable downscaler */
/* SIFMODE */
#define	  C0_SIF_HVSYNC	  0x00000000	/* Use H/VSYNC */
#define	  C0_SOF_NOSYNC	  0x40000000	/* Use inband active signaling */
#define	  C0_SIFM_MASK	  0xc0000000	/* SIF mode bits */

/* Bits below C1_444ALPHA are not present in Cafe */
#define REG_CTRL1	0x40	/* Control 1 */
#define	  C1_CLKGATE	  0x00000001	/* Sensor clock gate */
#define   C1_DESC_ENA	  0x00000100	/* DMA descriptor enable */
#define   C1_DESC_3WORD   0x00000200	/* Three-word descriptors used */
#define	  C1_444ALPHA	  0x00f00000	/* Alpha field in RGB444 */
#define	  C1_ALPHA_SHFT	  20
#define	  C1_DMAB32	  0x00000000	/* 32-byte DMA burst */
#define	  C1_DMAB16	  0x02000000	/* 16-byte DMA burst */
#define	  C1_DMAB64	  0x04000000	/* 64-byte DMA burst */
#define	  C1_DMAB_MASK	  0x06000000
#define	  C1_TWOBUFS	  0x08000000	/* Use only two DMA buffers */
#define	  C1_PWRDWN	  0x10000000	/* Power down */

#define REG_CLKCTRL	0x88	/* Clock control */
#define	  CLK_DIV_MASK	  0x0000ffff	/* Upper bits RW "reserved" */

/* This appears to be a Cafe-only register */
#define REG_UBAR	0xc4	/* Upper base address register */

/* Armada 610 DMA descriptor registers */
#define	REG_DMA_DESC_Y	0x200
#define	REG_DMA_DESC_U	0x204
#define	REG_DMA_DESC_V	0x208
#define REG_DESC_LEN_Y	0x20c	/* Lengths are in bytes */
#define	REG_DESC_LEN_U	0x210
#define REG_DESC_LEN_V	0x214

/*
 * Useful stuff that probably belongs somewhere global.
 */
#define VGA_WIDTH	640
#define VGA_HEIGHT	480

#endif /* _MCAM_CORE_H */
