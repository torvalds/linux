/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __NV_TYPE_H__
#define __NV_TYPE_H__

#include <linux/fb.h>
#include <linux/types.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <video/vga.h>

#define NV_ARCH_04  0x04
#define NV_ARCH_10  0x10
#define NV_ARCH_20  0x20
#define NV_ARCH_30  0x30
#define NV_ARCH_40  0x40

#define BITMASK(t,b) (((unsigned)(1U << (((t)-(b)+1)))-1)  << (b))
#define MASKEXPAND(mask) BITMASK(1?mask,0?mask)
#define SetBF(mask,value) ((value) << (0?mask))
#define GetBF(var,mask) (((unsigned)((var) & MASKEXPAND(mask))) >> (0?mask) )
#define SetBitField(value,from,to) SetBF(to, GetBF(value,from))
#define SetBit(n) (1<<(n))
#define Set8Bits(value) ((value)&0xff)

#define V_DBLSCAN  1

typedef struct {
	int bitsPerPixel;
	int depth;
	int displayWidth;
	int weight;
} NVFBLayout;

#define NUM_SEQ_REGS		0x05
#define NUM_CRT_REGS		0x41
#define NUM_GRC_REGS		0x09
#define NUM_ATC_REGS		0x15

struct nvidia_par;

struct nvidia_i2c_chan {
	struct nvidia_par *par;
	unsigned long ddc_base;
	struct i2c_adapter adapter;
	struct i2c_algo_bit_data algo;
};

typedef struct _riva_hw_state {
	u8 attr[NUM_ATC_REGS];
	u8 crtc[NUM_CRT_REGS];
	u8 gra[NUM_GRC_REGS];
	u8 seq[NUM_SEQ_REGS];
	u8 misc_output;
	u32 bpp;
	u32 width;
	u32 height;
	u32 interlace;
	u32 repaint0;
	u32 repaint1;
	u32 screen;
	u32 scale;
	u32 dither;
	u32 extra;
	u32 fifo;
	u32 pixel;
	u32 horiz;
	u32 arbitration0;
	u32 arbitration1;
	u32 pll;
	u32 pllB;
	u32 vpll;
	u32 vpll2;
	u32 vpllB;
	u32 vpll2B;
	u32 pllsel;
	u32 general;
	u32 crtcOwner;
	u32 head;
	u32 head2;
	u32 config;
	u32 cursorConfig;
	u32 cursor0;
	u32 cursor1;
	u32 cursor2;
	u32 timingH;
	u32 timingV;
	u32 displayV;
	u32 crtcSync;
	u32 control;
} RIVA_HW_STATE;

struct riva_regs {
	RIVA_HW_STATE ext;
};

struct nvidia_par {
	RIVA_HW_STATE SavedReg;
	RIVA_HW_STATE ModeReg;
	RIVA_HW_STATE initial_state;
	RIVA_HW_STATE *CurrentState;
	struct vgastate vgastate;
	u32 pseudo_palette[16];
	struct pci_dev *pci_dev;
	u32 Architecture;
	u32 CursorStart;
	int Chipset;
	unsigned long FbAddress;
	u8 __iomem *FbStart;
	u32 FbMapSize;
	u32 FbUsableSize;
	u32 ScratchBufferSize;
	u32 ScratchBufferStart;
	int FpScale;
	u32 MinVClockFreqKHz;
	u32 MaxVClockFreqKHz;
	u32 CrystalFreqKHz;
	u32 RamAmountKBytes;
	u32 IOBase;
	NVFBLayout CurrentLayout;
	int cursor_reset;
	int lockup;
	int videoKey;
	int FlatPanel;
	int FPDither;
	int Television;
	int CRTCnumber;
	int alphaCursor;
	int twoHeads;
	int twoStagePLL;
	int fpScaler;
	int fpWidth;
	int fpHeight;
	int PanelTweak;
	int paneltweak;
	int LVDS;
	int pm_state;
	int reverse_i2c;
	u32 crtcSync_read;
	u32 fpSyncs;
	u32 dmaPut;
	u32 dmaCurrent;
	u32 dmaFree;
	u32 dmaMax;
	u32 __iomem *dmaBase;
	u32 currentRop;
	int WaitVSyncPossible;
	int BlendingPossible;
	u32 paletteEnabled;
	u32 forceCRTC;
	u32 open_count;
	u8 DDCBase;
	int wc_cookie;
	struct nvidia_i2c_chan chan[3];

	volatile u32 __iomem *REGS;
	volatile u32 __iomem *PCRTC0;
	volatile u32 __iomem *PCRTC;
	volatile u32 __iomem *PRAMDAC0;
	volatile u32 __iomem *PFB;
	volatile u32 __iomem *PFIFO;
	volatile u32 __iomem *PGRAPH;
	volatile u32 __iomem *PEXTDEV;
	volatile u32 __iomem *PTIMER;
	volatile u32 __iomem *PMC;
	volatile u32 __iomem *PRAMIN;
	volatile u32 __iomem *FIFO;
	volatile u32 __iomem *CURSOR;
	volatile u8 __iomem *PCIO0;
	volatile u8 __iomem *PCIO;
	volatile u8 __iomem *PVIO;
	volatile u8 __iomem *PDIO0;
	volatile u8 __iomem *PDIO;
	volatile u32 __iomem *PRAMDAC;
};

#endif				/* __NV_TYPE_H__ */
