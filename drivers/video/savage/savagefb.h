/*
 * linux/drivers/video/savagefb.h -- S3 Savage Framebuffer Driver
 *
 * Copyright (c) 2001  Denis Oliver Kropp <dok@convergence.de>
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License.  See the file COPYING in the main directory of this
 * archive for more details.
 */


#ifndef __SAVAGEFB_H__
#define __SAVAGEFB_H__

#include <linux/i2c.h>
#include <linux/i2c-id.h>
#include <linux/i2c-algo-bit.h>
#include "../edid.h"

#ifdef SAVAGEFB_DEBUG
# define DBG(x)		printk (KERN_DEBUG "savagefb: %s\n", (x));
#else
# define DBG(x)
# define SavagePrintRegs(...)
#endif


#define PCI_CHIP_SAVAGE4      0x8a22
#define PCI_CHIP_SAVAGE3D     0x8a20
#define PCI_CHIP_SAVAGE3D_MV  0x8a21
#define PCI_CHIP_SAVAGE2000   0x9102
#define PCI_CHIP_SAVAGE_MX_MV 0x8c10
#define PCI_CHIP_SAVAGE_MX    0x8c11
#define PCI_CHIP_SAVAGE_IX_MV 0x8c12
#define PCI_CHIP_SAVAGE_IX    0x8c13
#define PCI_CHIP_PROSAVAGE_PM 0x8a25
#define PCI_CHIP_PROSAVAGE_KM 0x8a26
 /* Twister is a code name; hope I get the real name soon. */
#define PCI_CHIP_S3TWISTER_P  0x8d01
#define PCI_CHIP_S3TWISTER_K  0x8d02
#define PCI_CHIP_PROSAVAGE_DDR          0x8d03
#define PCI_CHIP_PROSAVAGE_DDRK         0x8d04
#define PCI_CHIP_SUPSAV_MX128		0x8c22
#define PCI_CHIP_SUPSAV_MX64		0x8c24
#define PCI_CHIP_SUPSAV_MX64C		0x8c26
#define PCI_CHIP_SUPSAV_IX128SDR	0x8c2a
#define PCI_CHIP_SUPSAV_IX128DDR	0x8c2b
#define PCI_CHIP_SUPSAV_IX64SDR		0x8c2c
#define PCI_CHIP_SUPSAV_IX64DDR		0x8c2d
#define PCI_CHIP_SUPSAV_IXCSDR		0x8c2e
#define PCI_CHIP_SUPSAV_IXCDDR		0x8c2f



#define S3_SAVAGE3D_SERIES(chip)  ((chip>=S3_SAVAGE3D) && (chip<=S3_SAVAGE_MX))

#define S3_SAVAGE4_SERIES(chip)   ((chip==S3_SAVAGE4) || (chip==S3_PROSAVAGE))

#define S3_SAVAGE_MOBILE_SERIES(chip)  ((chip==S3_SAVAGE_MX) || (chip==S3_SUPERSAVAGE))

#define S3_SAVAGE_SERIES(chip)    ((chip>=S3_SAVAGE3D) && (chip<=S3_SAVAGE2000))


/* Chip tags.  These are used to group the adapters into
 * related families.
 */

typedef enum {
  S3_UNKNOWN = 0,
  S3_SAVAGE3D,
  S3_SAVAGE_MX,
  S3_SAVAGE4,
  S3_PROSAVAGE,
  S3_SUPERSAVAGE,
  S3_SAVAGE2000,
  S3_LAST
} savage_chipset;

#define BIOS_BSIZE		     1024
#define BIOS_BASE		     0xc0000

#define SAVAGE_NEWMMIO_REGBASE_S3    0x1000000  /* 16MB */
#define SAVAGE_NEWMMIO_REGBASE_S4    0x0000000
#define SAVAGE_NEWMMIO_REGSIZE	     0x0080000  /* 512kb */
#define SAVAGE_NEWMMIO_VGABASE	     0x8000

#define BASE_FREQ		     14318
#define HALF_BASE_FREQ               7159

#define FIFO_CONTROL_REG	     0x8200
#define MIU_CONTROL_REG		     0x8204
#define STREAMS_TIMEOUT_REG	     0x8208
#define MISC_TIMEOUT_REG	     0x820c

#define MONO_PAT_0                   0xa4e8
#define MONO_PAT_1                   0xa4ec

#define MAXFIFO                      0x7f00

#define BCI_CMD_NOP                  0x40000000
#define BCI_CMD_SETREG               0x96000000
#define BCI_CMD_RECT                 0x48000000
#define BCI_CMD_RECT_XP              0x01000000
#define BCI_CMD_RECT_YP              0x02000000
#define BCI_CMD_SEND_COLOR           0x00008000
#define BCI_CMD_DEST_GBD             0x00000000
#define BCI_CMD_SRC_GBD              0x00000020
#define BCI_CMD_SRC_SOLID            0x00000000
#define BCI_CMD_SRC_MONO             0x00000060
#define BCI_CMD_CLIP_NEW             0x00006000
#define BCI_CMD_CLIP_LR              0x00004000

#define BCI_CLIP_LR(l, r)            ((((r) << 16) | (l)) & 0x0FFF0FFF)
#define BCI_CLIP_TL(t, l)            ((((t) << 16) | (l)) & 0x0FFF0FFF)
#define BCI_CLIP_BR(b, r)            ((((b) << 16) | (r)) & 0x0FFF0FFF)
#define BCI_W_H(w, h)                (((h) << 16) | ((w) & 0xFFF))
#define BCI_X_Y(x, y)                (((y) << 16) | ((x) & 0xFFF))

#define BCI_GBD1                     0xE0
#define BCI_GBD2                     0xE1

#define BCI_BUFFER_OFFSET            0x10000
#define BCI_SIZE                     0x4000

#define BCI_SEND(dw)                 writel(dw, par->bci_base + par->bci_ptr++)

#define BCI_CMD_GET_ROP(cmd)         (((cmd) >> 16) & 0xFF)
#define BCI_CMD_SET_ROP(cmd, rop)    ((cmd) |= ((rop & 0xFF) << 16))
#define BCI_CMD_SEND_COLOR           0x00008000

struct xtimings {
	unsigned int Clock;
	unsigned int HDisplay;
	unsigned int HSyncStart;
	unsigned int HSyncEnd;
	unsigned int HTotal;
	unsigned int HAdjusted;
	unsigned int VDisplay;
	unsigned int VSyncStart;
	unsigned int VSyncEnd;
	unsigned int VTotal;
	unsigned int sync;
	int	       dblscan;
	int	       interlaced;
};


/* --------------------------------------------------------------------- */

#define NR_PALETTE	256


struct savagefb_par;

struct savagefb_i2c_chan {
	struct savagefb_par *par;
	struct i2c_adapter adapter;
	struct i2c_algo_bit_data algo;
	volatile u8 __iomem *ioaddr;
	u32   reg;
};

struct savagefb_par {
	struct pci_dev *pcidev;
	savage_chipset  chip;
	struct savagefb_i2c_chan chan;
	unsigned char   *edid;
	u32 pseudo_palette[16];
	int dacSpeedBpp;
	int maxClock;
	int minClock;
	int numClocks;
	int clock[4];
	struct {
		u8   __iomem *vbase;
		u32    pbase;
		u32    len;
#ifdef CONFIG_MTRR
		int    mtrr;
#endif
	} video;

	struct {
		volatile u8  __iomem *vbase;
		u32           pbase;
		u32           len;
	} mmio;

	volatile u32  __iomem *bci_base;
	unsigned int  bci_ptr;

	u32           cob_offset;
	u32           cob_size;
	int           cob_index;

	void (*SavageWaitIdle) (struct savagefb_par *par);
	void (*SavageWaitFifo) (struct savagefb_par *par, int space);

	int MCLK, REFCLK, LCDclk;
	int HorizScaleFactor;

	/* Panels size */
	int SavagePanelWidth;
	int SavagePanelHeight;

	struct {
		u16 red, green, blue, transp;
	} palette[NR_PALETTE];

	int depth;
	int vwidth;

	unsigned char MiscOutReg;     /* Misc */
	unsigned char CRTC[25];       /* Crtc Controller */
	unsigned char Sequencer[5];   /* Video Sequencer */
	unsigned char Graphics[9];    /* Video Graphics */
	unsigned char Attribute[21];  /* Video Atribute */

	unsigned int mode, refresh;
	unsigned char SR08, SR0E, SR0F;
	unsigned char SR10, SR11, SR12, SR13, SR15, SR18, SR29, SR30;
	unsigned char SR54[8];
	unsigned char Clock;
	unsigned char CR31, CR32, CR33, CR34, CR36, CR3A, CR3B, CR3C;
	unsigned char CR40, CR41, CR42, CR43, CR45;
	unsigned char CR50, CR51, CR53, CR55, CR58, CR5B, CR5D, CR5E;
	unsigned char CR60, CR63, CR65, CR66, CR67, CR68, CR69, CR6D, CR6F;
	unsigned char CR86, CR88;
	unsigned char CR90, CR91, CRB0;
	unsigned int  STREAMS[22];	/* yuck, streams regs */
	unsigned int  MMPR0, MMPR1, MMPR2, MMPR3;
};

#define BCI_BD_BW_DISABLE            0x10000000
#define BCI_BD_SET_BPP(bd, bpp)      ((bd) |= (((bpp) & 0xFF) << 16))
#define BCI_BD_SET_STRIDE(bd, st)    ((bd) |= ((st) & 0xFFFF))


/* IO functions */

#define  vga_in8(addr)         (inb (addr))
#define vga_in16(addr)         (inw (addr))
#define vga_in32(addr)         (inl (addr))

#define  vga_out8(addr,val)    (outb ((val), (addr)))
#define vga_out16(addr,val)    (outw ((val), (addr)))
#define vga_out32(addr,val)    (outl ((val), (addr)))

#define savage_in16(addr)      readw(par->mmio.vbase + (addr))
#define savage_in32(addr)      readl(par->mmio.vbase + (addr))

#define savage_out16(addr,val) writew((val), par->mmio.vbase + (addr))
#define savage_out32(addr,val) writel((val), par->mmio.vbase + (addr))

static inline u8 VGArCR (u8 index)
{
  outb (index, 0x3d4);
  return inb (0x3d5);
}

static inline u8 VGArGR (u8 index)
{
  outb (index, 0x3ce);
  return inb (0x3cf);
}

static inline u8 VGArSEQ (u8 index)
{
  outb (index, 0x3c4);
  return inb (0x3c5);
}

#define VGAwCR(index, val) \
do {                       \
  vga_out8 (0x3d4, index); \
  vga_out8 (0x3d5, val);   \
} while (0)

#define VGAwGR(index, val) \
do {                       \
  vga_out8 (0x3ce, index); \
  vga_out8 (0x3cf, val);   \
} while (0)

#define VGAwSEQ(index, val) \
do {                        \
  vga_out8 (0x3c4, index);  \
  vga_out8 (0x3c5, val);    \
} while (0)

#define VGAenablePalette() \
do {                       \
  u8 tmp;                  \
                           \
  tmp = vga_in8 (0x3da);   \
  vga_out8 (0x3c0, 0x00);  \
  paletteEnabled = 1;      \
} while (0)

#define VGAdisablePalette() \
do {                        \
  u8 tmp;                   \
                            \
  tmp = vga_in8 (0x3da);    \
  vga_out8 (0x3c0, 0x20);   \
  paletteEnabled = 0;       \
} while (0)

#define VGAwATTR(index, value) \
do {                           \
  u8 tmp;                      \
                               \
  if (paletteEnabled)          \
    index &= ~0x20;            \
  else                         \
    index |= 0x20;             \
                               \
  tmp = vga_in8 (0x3da);       \
  vga_out8 (0x3c0, index);     \
  vga_out8 (0x3c0, value);     \
} while (0)

#define VGAwMISC(value)    \
do {                       \
  vga_out8 (0x3c2, value); \
} while (0)

#ifndef CONFIG_FB_SAVAGE_ACCEL
#define savagefb_set_clip(x)
#endif

#define VerticalRetraceWait() \
{ \
	vga_out8 (0x3d4, 0x17); \
	if (vga_in8 (0x3d5) & 0x80) { \
		while ((vga_in8(0x3da) & 0x08) == 0x08) ; \
		while ((vga_in8(0x3da) & 0x08) == 0x00) ; \
	} \
}

extern int savagefb_probe_i2c_connector(struct savagefb_par *par,
					u8 **out_edid);
extern void savagefb_create_i2c_busses(struct fb_info *info);
extern void savagefb_delete_i2c_busses(struct fb_info *info);
extern int  savagefb_sync(struct fb_info *info);
extern void savagefb_copyarea(struct fb_info *info,
			      const struct fb_copyarea *region);
extern void savagefb_fillrect(struct fb_info *info,
			      const struct fb_fillrect *rect);
extern void savagefb_imageblit(struct fb_info *info,
			       const struct fb_image *image);


#endif /* __SAVAGEFB_H__ */
