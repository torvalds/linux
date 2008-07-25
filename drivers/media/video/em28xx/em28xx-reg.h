#define EM_GPIO_0  (1 << 0)
#define EM_GPIO_1  (1 << 1)
#define EM_GPIO_2  (1 << 2)
#define EM_GPIO_3  (1 << 3)
#define EM_GPIO_4  (1 << 4)
#define EM_GPIO_5  (1 << 5)
#define EM_GPIO_6  (1 << 6)
#define EM_GPIO_7  (1 << 7)

#define EM_GPO_0   (1 << 0)
#define EM_GPO_1   (1 << 1)
#define EM_GPO_2   (1 << 2)
#define EM_GPO_3   (1 << 3)

/* em2800 registers */
#define EM2800_R08_AUDIOSRC 0x08

/* em28xx registers */

	/* GPIO/GPO registers */
#define EM2880_R04_GPO	0x04    /* em2880-em2883 only */
#define EM28XX_R08_GPIO	0x08	/* em2820 or upper */

#define EM28XX_R06_I2C_CLK	0x06
#define EM28XX_R0A_CHIPID	0x0a
#define EM28XX_R0C_USBSUSP	0x0c	/* */

#define EM28XX_R0E_AUDIOSRC	0x0e
#define EM28XX_R0F_XCLK	0x0f

#define EM28XX_R10_VINMODE	0x10
#define EM28XX_R11_VINCTRL	0x11
#define EM28XX_R12_VINENABLE	0x12	/* */

#define EM28XX_R14_GAMMA	0x14
#define EM28XX_R15_RGAIN	0x15
#define EM28XX_R16_GGAIN	0x16
#define EM28XX_R17_BGAIN	0x17
#define EM28XX_R18_ROFFSET	0x18
#define EM28XX_R19_GOFFSET	0x19
#define EM28XX_R1A_BOFFSET	0x1a

#define EM28XX_R1B_OFLOW	0x1b
#define EM28XX_R1C_HSTART	0x1c
#define EM28XX_R1D_VSTART	0x1d
#define EM28XX_R1E_CWIDTH	0x1e
#define EM28XX_R1F_CHEIGHT	0x1f

#define EM28XX_R20_YGAIN	0x20
#define EM28XX_R21_YOFFSET	0x21
#define EM28XX_R22_UVGAIN	0x22
#define EM28XX_R23_UOFFSET	0x23
#define EM28XX_R24_VOFFSET	0x24
#define EM28XX_R25_SHARPNESS	0x25

#define EM28XX_R26_COMPR	0x26
#define EM28XX_R27_OUTFMT	0x27

#define EM28XX_R28_XMIN	0x28
#define EM28XX_R29_XMAX	0x29
#define EM28XX_R2A_YMIN	0x2a
#define EM28XX_R2B_YMAX	0x2b

#define EM28XX_R30_HSCALELOW	0x30
#define EM28XX_R31_HSCALEHIGH	0x31
#define EM28XX_R32_VSCALELOW	0x32
#define EM28XX_R33_VSCALEHIGH	0x33

#define EM28XX_R40_AC97LSB	0x40
#define EM28XX_R41_AC97MSB	0x41
#define EM28XX_R42_AC97ADDR	0x42
#define EM28XX_R43_AC97BUSY	0x43

/* em202 registers */
#define EM28XX_R02_MASTER_AC97	0x02
#define EM28XX_R10_LINE_IN_AC97    0x10
#define EM28XX_R14_VIDEO_AC97	0x14

/* register settings */
#define EM2800_AUDIO_SRC_TUNER  0x0d
#define EM2800_AUDIO_SRC_LINE   0x0c
#define EM28XX_AUDIO_SRC_TUNER	0xc0
#define EM28XX_AUDIO_SRC_LINE	0x80

/* FIXME: Need to be populated with the other chip ID's */
enum em28xx_chip_id {
	CHIP_ID_EM2860 = 34,
	CHIP_ID_EM2883 = 36,
};
