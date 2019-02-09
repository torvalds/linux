/* SPDX-License-Identifier: GPL-2.0 */
/* DMA controller registers */
#define REG8_1(a0) ((const u16[8]) { a0, a0 + 1, a0 + 2, a0 + 3, \
				     a0 + 4, a0 + 5, a0 + 6, a0 + 7})
#define REG8_2(a0) ((const u16[8]) { a0, a0 + 2, a0 + 4, a0 + 6,	\
				     a0 + 8, a0 + 0xa, a0 + 0xc, a0 + 0xe})
#define REG8_8(a0) ((const u16[8]) { a0, a0 + 8, a0 + 0x10, a0 + 0x18, \
				     a0 + 0x20, a0 + 0x28, a0 + 0x30, \
				     a0 + 0x38})
#define INT_STATUS		0x00
#define PB_STATUS		0x01
#define DMA_CMD			0x02
#define VIDEO_FIFO_STATUS	0x03
#define VIDEO_CHANNEL_ID	0x04
#define VIDEO_PARSER_STATUS	0x05
#define SYS_SOFT_RST		0x06
#define DMA_PAGE_TABLE0_ADDR	((const u16[8]) { 0x08, 0xd0, 0xd2, 0xd4, \
						  0xd6, 0xd8, 0xda, 0xdc })
#define DMA_PAGE_TABLE1_ADDR	((const u16[8]) { 0x09, 0xd1, 0xd3, 0xd5, \
						  0xd7, 0xd9, 0xdb, 0xdd })
#define DMA_CHANNEL_ENABLE	0x0a
#define DMA_CONFIG		0x0b
#define DMA_TIMER_INTERVAL	0x0c
#define DMA_CHANNEL_TIMEOUT	0x0d
#define VDMA_CHANNEL_CONFIG	REG8_1(0x10)
#define ADMA_P_ADDR		REG8_2(0x18)
#define ADMA_B_ADDR		REG8_2(0x19)
#define DMA10_P_ADDR		0x28
#define DMA10_B_ADDR		0x29
#define VIDEO_CONTROL1		0x2a
#define VIDEO_CONTROL2		0x2b
#define AUDIO_CONTROL1		0x2c
#define AUDIO_CONTROL2		0x2d
#define PHASE_REF		0x2e
#define GPIO_REG		0x2f
#define INTL_HBAR_CTRL		REG8_1(0x30)
#define AUDIO_CONTROL3		0x38
#define VIDEO_FIELD_CTRL	REG8_1(0x39)
#define HSCALER_CTRL		REG8_1(0x42)
#define VIDEO_SIZE		REG8_1(0x4A)
#define VIDEO_SIZE_F2		REG8_1(0x52)
#define MD_CONF			REG8_1(0x60)
#define MD_INIT			REG8_1(0x68)
#define MD_MAP0			REG8_1(0x70)
#define VDMA_P_ADDR		REG8_8(0x80) /* not used in DMA SG mode */
#define VDMA_WHP		REG8_8(0x81)
#define VDMA_B_ADDR		REG8_8(0x82)
#define VDMA_F2_P_ADDR		REG8_8(0x84)
#define VDMA_F2_WHP		REG8_8(0x85)
#define VDMA_F2_B_ADDR		REG8_8(0x86)
#define EP_REG_ADDR		0xfe
#define EP_REG_DATA		0xff

/* Video decoder registers */
#define VDREG8(a0) ((const u16[8]) { \
	a0 + 0x000, a0 + 0x010, a0 + 0x020, a0 + 0x030,	\
	a0 + 0x100, a0 + 0x110, a0 + 0x120, a0 + 0x130})
#define VIDSTAT			VDREG8(0x100)
#define BRIGHT			VDREG8(0x101)
#define CONTRAST		VDREG8(0x102)
#define SHARPNESS		VDREG8(0x103)
#define SAT_U			VDREG8(0x104)
#define SAT_V			VDREG8(0x105)
#define HUE			VDREG8(0x106)
#define CROP_HI			VDREG8(0x107)
#define VDELAY_LO		VDREG8(0x108)
#define VACTIVE_LO		VDREG8(0x109)
#define HDELAY_LO		VDREG8(0x10a)
#define HACTIVE_LO		VDREG8(0x10b)
#define MVSN			VDREG8(0x10c)
#define STATUS2			VDREG8(0x10d)
#define SDT			VDREG8(0x10e)
#define SDT_EN			VDREG8(0x10f)

#define VSCALE_LO		VDREG8(0x144)
#define SCALE_HI		VDREG8(0x145)
#define HSCALE_LO		VDREG8(0x146)
#define F2CROP_HI		VDREG8(0x147)
#define F2VDELAY_LO		VDREG8(0x148)
#define F2VACTIVE_LO		VDREG8(0x149)
#define F2HDELAY_LO		VDREG8(0x14a)
#define F2HACTIVE_LO		VDREG8(0x14b)
#define F2VSCALE_LO		VDREG8(0x14c)
#define F2SCALE_HI		VDREG8(0x14d)
#define F2HSCALE_LO		VDREG8(0x14e)
#define F2CNT			VDREG8(0x14f)

#define VDREG2(a0) ((const u16[2]) { a0, a0 + 0x100 })
#define SRST			VDREG2(0x180)
#define ACNTL			VDREG2(0x181)
#define ACNTL2			VDREG2(0x182)
#define CNTRL1			VDREG2(0x183)
#define CKHY			VDREG2(0x184)
#define SHCOR			VDREG2(0x185)
#define CORING			VDREG2(0x186)
#define CLMPG			VDREG2(0x187)
#define IAGC			VDREG2(0x188)
#define VCTRL1			VDREG2(0x18f)
#define MISC1			VDREG2(0x194)
#define LOOP			VDREG2(0x195)
#define MISC2			VDREG2(0x196)

#define CLMD			VDREG2(0x197)
#define ANPWRDOWN		VDREG2(0x1ce)
#define AIGAIN			((const u16[8]) { 0x1d0, 0x1d1, 0x1d2, 0x1d3, \
						  0x2d0, 0x2d1, 0x2d2, 0x2d3 })

#define SYS_MODE_DMA_SHIFT	13
#define AUDIO_DMA_SIZE_SHIFT	19
#define AUDIO_DMA_SIZE_MIN	SZ_512
#define AUDIO_DMA_SIZE_MAX	SZ_4K
#define AUDIO_DMA_SIZE_MASK	(SZ_8K - 1)

#define DMA_CMD_ENABLE		BIT(31)
#define INT_STATUS_DMA_TOUT	BIT(17)
#define TW686X_VIDSTAT_HLOCK	BIT(6)
#define TW686X_VIDSTAT_VDLOSS	BIT(7)

#define TW686X_STD_NTSC_M	0
#define TW686X_STD_PAL		1
#define TW686X_STD_SECAM	2
#define TW686X_STD_NTSC_443	3
#define TW686X_STD_PAL_M	4
#define TW686X_STD_PAL_CN	5
#define TW686X_STD_PAL_60	6

#define TW686X_FIELD_MODE	0x3
#define TW686X_FRAME_MODE	0x2
/* 0x1 is reserved */
#define TW686X_SG_MODE		0x0

#define TW686X_FIFO_ERROR(x)	(x & ~(0xff))
