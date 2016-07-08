/* DMA controller registers */
#define REG8_1(a0) ((const u16[8]) {a0, a0 + 1, a0 + 2, a0 + 3,	\
				   a0 + 4, a0 + 5, a0 + 6, a0 + 7})
#define REG8_2(a0) ((const u16[8]) {a0, a0 + 2, a0 + 4, a0 + 6,	\
				   a0 + 8, a0 + 0xA, a0 + 0xC, a0 + 0xE})
#define REG8_8(a0) ((const u16[8]) {a0, a0 + 8, a0 + 0x10, a0 + 0x18,	\
				   a0 + 0x20, a0 + 0x28, a0 + 0x30, a0 + 0x38})
#define INT_STATUS		0x00
#define PB_STATUS		0x01
#define DMA_CMD			0x02
#define VIDEO_FIFO_STATUS	0x03
#define VIDEO_CHANNEL_ID	0x04
#define VIDEO_PARSER_STATUS	0x05
#define SYS_SOFT_RST		0x06
#define DMA_PAGE_TABLE0_ADDR	((const u16[8]) {0x08, 0xD0, 0xD2, 0xD4, \
						0xD6, 0xD8, 0xDA, 0xDC})
#define DMA_PAGE_TABLE1_ADDR	((const u16[8]) {0x09, 0xD1, 0xD3, 0xD5, \
						0xD7, 0xD9, 0xDB, 0xDD})
#define DMA_CHANNEL_ENABLE	0x0A
#define DMA_CONFIG		0x0B
#define DMA_TIMER_INTERVAL	0x0C
#define DMA_CHANNEL_TIMEOUT	0x0D
#define VDMA_CHANNEL_CONFIG	REG8_1(0x10)
#define ADMA_P_ADDR		REG8_2(0x18)
#define ADMA_B_ADDR		REG8_2(0x19)
#define DMA10_P_ADDR		0x28 /* ??? */
#define DMA10_B_ADDR		0x29
#define VIDEO_CONTROL1		0x2A
#define VIDEO_CONTROL2		0x2B
#define AUDIO_CONTROL1		0x2C
#define AUDIO_CONTROL2		0x2D
#define PHASE_REF		0x2E
#define GPIO_REG		0x2F
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
#define EP_REG_ADDR		0xFE
#define EP_REG_DATA		0xFF

/* Video decoder registers */
#define VDREG8(a0) ((const u16[8]) {			\
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
#define HDELAY_LO		VDREG8(0x10A)
#define HACTIVE_LO		VDREG8(0x10B)
#define MVSN			VDREG8(0x10C)
#define STATUS2			VDREG8(0x10C)
#define SDT			VDREG8(0x10E)
#define SDT_EN			VDREG8(0x10F)

#define VSCALE_LO		VDREG8(0x144)
#define SCALE_HI		VDREG8(0x145)
#define HSCALE_LO		VDREG8(0x146)
#define F2CROP_HI		VDREG8(0x147)
#define F2VDELAY_LO		VDREG8(0x148)
#define F2VACTIVE_LO		VDREG8(0x149)
#define F2HDELAY_LO		VDREG8(0x14A)
#define F2HACTIVE_LO		VDREG8(0x14B)
#define F2VSCALE_LO		VDREG8(0x14C)
#define F2SCALE_HI		VDREG8(0x14D)
#define F2HSCALE_LO		VDREG8(0x14E)
#define F2CNT			VDREG8(0x14F)

#define VDREG2(a0) ((const u16[2]) {a0, a0 + 0x100})
#define SRST			VDREG2(0x180)
#define ACNTL			VDREG2(0x181)
#define ACNTL2			VDREG2(0x182)
#define CNTRL1			VDREG2(0x183)
#define CKHY			VDREG2(0x184)
#define SHCOR			VDREG2(0x185)
#define CORING			VDREG2(0x186)
#define CLMPG			VDREG2(0x187)
#define IAGC			VDREG2(0x188)
#define VCTRL1			VDREG2(0x18F)
#define MISC1			VDREG2(0x194)
#define LOOP			VDREG2(0x195)
#define MISC2			VDREG2(0x196)

#define CLMD			VDREG2(0x197)
#define AIGAIN			((const u16[8]) {0x1D0, 0x1D1, 0x1D2, 0x1D3, \
						 0x2D0, 0x2D1, 0x2D2, 0x2D3})
