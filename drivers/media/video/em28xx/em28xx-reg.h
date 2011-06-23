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

#define EM28XX_R00_CHIPCFG	0x00

/* em28xx Chip Configuration 0x00 */
#define EM28XX_CHIPCFG_VENDOR_AUDIO		0x80
#define EM28XX_CHIPCFG_I2S_VOLUME_CAPABLE	0x40
#define EM28XX_CHIPCFG_I2S_5_SAMPRATES		0x30
#define EM28XX_CHIPCFG_I2S_3_SAMPRATES		0x20
#define EM28XX_CHIPCFG_AC97			0x10
#define EM28XX_CHIPCFG_AUDIOMASK		0x30

#define EM28XX_R01_CHIPCFG2	0x01

/* em28xx Chip Configuration 2 0x01 */
#define EM28XX_CHIPCFG2_TS_PRESENT		0x10
#define EM28XX_CHIPCFG2_TS_REQ_INTERVAL_MASK	0x0c /* bits 3-2 */
#define EM28XX_CHIPCFG2_TS_REQ_INTERVAL_1MF	0x00
#define EM28XX_CHIPCFG2_TS_REQ_INTERVAL_2MF	0x04
#define EM28XX_CHIPCFG2_TS_REQ_INTERVAL_4MF	0x08
#define EM28XX_CHIPCFG2_TS_REQ_INTERVAL_8MF	0x0c
#define EM28XX_CHIPCFG2_TS_PACKETSIZE_MASK	0x03 /* bits 0-1 */
#define EM28XX_CHIPCFG2_TS_PACKETSIZE_188	0x00
#define EM28XX_CHIPCFG2_TS_PACKETSIZE_376	0x01
#define EM28XX_CHIPCFG2_TS_PACKETSIZE_564	0x02
#define EM28XX_CHIPCFG2_TS_PACKETSIZE_752	0x03


	/* GPIO/GPO registers */
#define EM2880_R04_GPO	0x04    /* em2880-em2883 only */
#define EM28XX_R08_GPIO	0x08	/* em2820 or upper */

#define EM28XX_R06_I2C_CLK	0x06

/* em28xx I2C Clock Register (0x06) */
#define EM28XX_I2C_CLK_ACK_LAST_READ	0x80
#define EM28XX_I2C_CLK_WAIT_ENABLE	0x40
#define EM28XX_I2C_EEPROM_ON_BOARD	0x08
#define EM28XX_I2C_EEPROM_KEY_VALID	0x04
#define EM2874_I2C_SECONDARY_BUS_SELECT	0x04 /* em2874 has two i2c busses */
#define EM28XX_I2C_FREQ_1_5_MHZ		0x03 /* bus frequency (bits [1-0]) */
#define EM28XX_I2C_FREQ_25_KHZ		0x02
#define EM28XX_I2C_FREQ_400_KHZ		0x01
#define EM28XX_I2C_FREQ_100_KHZ		0x00


#define EM28XX_R0A_CHIPID	0x0a
#define EM28XX_R0C_USBSUSP	0x0c	/* */

#define EM28XX_R0E_AUDIOSRC	0x0e
#define EM28XX_R0F_XCLK	0x0f

/* em28xx XCLK Register (0x0f) */
#define EM28XX_XCLK_AUDIO_UNMUTE	0x80 /* otherwise audio muted */
#define EM28XX_XCLK_I2S_MSB_TIMING	0x40 /* otherwise standard timing */
#define EM28XX_XCLK_IR_RC5_MODE		0x20 /* otherwise NEC mode */
#define EM28XX_XCLK_IR_NEC_CHK_PARITY	0x10
#define EM28XX_XCLK_FREQUENCY_30MHZ	0x00 /* Freq. select (bits [3-0]) */
#define EM28XX_XCLK_FREQUENCY_15MHZ	0x01
#define EM28XX_XCLK_FREQUENCY_10MHZ	0x02
#define EM28XX_XCLK_FREQUENCY_7_5MHZ	0x03
#define EM28XX_XCLK_FREQUENCY_6MHZ	0x04
#define EM28XX_XCLK_FREQUENCY_5MHZ	0x05
#define EM28XX_XCLK_FREQUENCY_4_3MHZ	0x06
#define EM28XX_XCLK_FREQUENCY_12MHZ	0x07
#define EM28XX_XCLK_FREQUENCY_20MHZ	0x08
#define EM28XX_XCLK_FREQUENCY_20MHZ_2	0x09
#define EM28XX_XCLK_FREQUENCY_48MHZ	0x0a
#define EM28XX_XCLK_FREQUENCY_24MHZ	0x0b

#define EM28XX_R10_VINMODE	0x10

#define EM28XX_R11_VINCTRL	0x11

/* em28xx Video Input Control Register 0x11 */
#define EM28XX_VINCTRL_VBI_SLICED	0x80
#define EM28XX_VINCTRL_VBI_RAW		0x40
#define EM28XX_VINCTRL_VOUT_MODE_IN	0x20 /* HREF,VREF,VACT in output */
#define EM28XX_VINCTRL_CCIR656_ENABLE	0x10
#define EM28XX_VINCTRL_VBI_16BIT_RAW	0x08 /* otherwise 8-bit raw */
#define EM28XX_VINCTRL_FID_ON_HREF	0x04
#define EM28XX_VINCTRL_DUAL_EDGE_STROBE	0x02
#define EM28XX_VINCTRL_INTERLACED	0x01

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

/* em28xx Output Format Register (0x27) */
#define EM28XX_OUTFMT_RGB_8_RGRG	0x00
#define EM28XX_OUTFMT_RGB_8_GRGR	0x01
#define EM28XX_OUTFMT_RGB_8_GBGB	0x02
#define EM28XX_OUTFMT_RGB_8_BGBG	0x03
#define EM28XX_OUTFMT_RGB_16_656	0x04
#define EM28XX_OUTFMT_RGB_8_BAYER	0x08 /* Pattern in Reg 0x10[1-0] */
#define EM28XX_OUTFMT_YUV211		0x10
#define EM28XX_OUTFMT_YUV422_Y0UY1V	0x14
#define EM28XX_OUTFMT_YUV422_Y1UY0V	0x15
#define EM28XX_OUTFMT_YUV411		0x18


#define EM28XX_R28_XMIN	0x28
#define EM28XX_R29_XMAX	0x29
#define EM28XX_R2A_YMIN	0x2a
#define EM28XX_R2B_YMAX	0x2b

#define EM28XX_R30_HSCALELOW	0x30
#define EM28XX_R31_HSCALEHIGH	0x31
#define EM28XX_R32_VSCALELOW	0x32
#define EM28XX_R33_VSCALEHIGH	0x33
#define EM28XX_R34_VBI_START_H	0x34
#define EM28XX_R35_VBI_START_V	0x35
#define EM28XX_R36_VBI_WIDTH	0x36
#define EM28XX_R37_VBI_HEIGHT	0x37

#define EM28XX_R40_AC97LSB	0x40
#define EM28XX_R41_AC97MSB	0x41
#define EM28XX_R42_AC97ADDR	0x42
#define EM28XX_R43_AC97BUSY	0x43

#define EM28XX_R45_IR		0x45
	/* 0x45  bit 7    - parity bit
		 bits 6-0 - count
	   0x46  IR brand
	   0x47  IR data
	 */

/* em2874 registers */
#define EM2874_R50_IR_CONFIG    0x50
#define EM2874_R51_IR           0x51
#define EM2874_R5F_TS_ENABLE    0x5f
#define EM2874_R80_GPIO         0x80

/* em2874 IR config register (0x50) */
#define EM2874_IR_NEC           0x00
#define EM2874_IR_RC5           0x04
#define EM2874_IR_RC6_MODE_0    0x08
#define EM2874_IR_RC6_MODE_6A   0x0b

/* em2874 Transport Stream Enable Register (0x5f) */
#define EM2874_TS1_CAPTURE_ENABLE (1 << 0)
#define EM2874_TS1_FILTER_ENABLE  (1 << 1)
#define EM2874_TS1_NULL_DISCARD   (1 << 2)
#define EM2874_TS2_CAPTURE_ENABLE (1 << 4)
#define EM2874_TS2_FILTER_ENABLE  (1 << 5)
#define EM2874_TS2_NULL_DISCARD   (1 << 6)

/* register settings */
#define EM2800_AUDIO_SRC_TUNER  0x0d
#define EM2800_AUDIO_SRC_LINE   0x0c
#define EM28XX_AUDIO_SRC_TUNER	0xc0
#define EM28XX_AUDIO_SRC_LINE	0x80

/* FIXME: Need to be populated with the other chip ID's */
enum em28xx_chip_id {
	CHIP_ID_EM2800 = 7,
	CHIP_ID_EM2710 = 17,
	CHIP_ID_EM2820 = 18,	/* Also used by some em2710 */
	CHIP_ID_EM2840 = 20,
	CHIP_ID_EM2750 = 33,
	CHIP_ID_EM2860 = 34,
	CHIP_ID_EM2870 = 35,
	CHIP_ID_EM2883 = 36,
	CHIP_ID_EM2874 = 65,
	CHIP_ID_EM28174 = 113,
};

/*
 * Registers used by em202 and other AC97 chips
 */

/* Standard AC97 registers */
#define AC97_RESET               0x00

	/* Output volumes */
#define AC97_MASTER_VOL          0x02
#define AC97_LINE_LEVEL_VOL      0x04	/* Some devices use for headphones */
#define AC97_MASTER_MONO_VOL     0x06

	/* Input volumes */
#define AC97_PC_BEEP_VOL         0x0a
#define AC97_PHONE_VOL           0x0c
#define AC97_MIC_VOL             0x0e
#define AC97_LINEIN_VOL          0x10
#define AC97_CD_VOL              0x12
#define AC97_VIDEO_VOL           0x14
#define AC97_AUX_VOL             0x16
#define AC97_PCM_OUT_VOL         0x18

	/* capture registers */
#define AC97_RECORD_SELECT       0x1a
#define AC97_RECORD_GAIN         0x1c

	/* control registers */
#define AC97_GENERAL_PURPOSE     0x20
#define AC97_3D_CTRL             0x22
#define AC97_AUD_INT_AND_PAG     0x24
#define AC97_POWER_DOWN_CTRL     0x26
#define AC97_EXT_AUD_ID          0x28
#define AC97_EXT_AUD_CTRL        0x2a

/* Supported rate varies for each AC97 device
   if write an unsupported value, it will return the closest one
 */
#define AC97_PCM_OUT_FRONT_SRATE 0x2c
#define AC97_PCM_OUT_SURR_SRATE  0x2e
#define AC97_PCM_OUT_LFE_SRATE   0x30
#define AC97_PCM_IN_SRATE        0x32

	/* For devices with more than 2 channels, extra output volumes */
#define AC97_LFE_MASTER_VOL      0x36
#define AC97_SURR_MASTER_VOL     0x38

	/* Digital SPDIF output control */
#define AC97_SPDIF_OUT_CTRL      0x3a

	/* Vendor ID identifier */
#define AC97_VENDOR_ID1          0x7c
#define AC97_VENDOR_ID2          0x7e

/* EMP202 vendor registers */
#define EM202_EXT_MODEM_CTRL     0x3e
#define EM202_GPIO_CONF          0x4c
#define EM202_GPIO_POLARITY      0x4e
#define EM202_GPIO_STICKY        0x50
#define EM202_GPIO_MASK          0x52
#define EM202_GPIO_STATUS        0x54
#define EM202_SPDIF_OUT_SEL      0x6a
#define EM202_ANTIPOP            0x72
#define EM202_EAPD_GPIO_ACCESS   0x74
