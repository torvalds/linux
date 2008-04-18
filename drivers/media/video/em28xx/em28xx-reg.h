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
#define EM2800_AUDIOSRC_REG 0x08

/* em28xx registers */

	/* GPIO/GPO registers */
#define EM_R04_GPO	0x04    /* em2880-em2883 only */
#define EM_R08_GPIO	0x08	/* em2820 or upper */

#define I2C_CLK_REG	0x06
#define CHIPID_REG	0x0a
#define USBSUSP_REG	0x0c	/* */

#define AUDIOSRC_REG	0x0e
#define XCLK_REG	0x0f

#define VINMODE_REG	0x10
#define VINCTRL_REG	0x11
#define VINENABLE_REG	0x12	/* */

#define GAMMA_REG	0x14
#define RGAIN_REG	0x15
#define GGAIN_REG	0x16
#define BGAIN_REG	0x17
#define ROFFSET_REG	0x18
#define GOFFSET_REG	0x19
#define BOFFSET_REG	0x1a

#define OFLOW_REG	0x1b
#define HSTART_REG	0x1c
#define VSTART_REG	0x1d
#define CWIDTH_REG	0x1e
#define CHEIGHT_REG	0x1f

#define YGAIN_REG	0x20
#define YOFFSET_REG	0x21
#define UVGAIN_REG	0x22
#define UOFFSET_REG	0x23
#define VOFFSET_REG	0x24
#define SHARPNESS_REG	0x25

#define COMPR_REG	0x26
#define OUTFMT_REG	0x27

#define XMIN_REG	0x28
#define XMAX_REG	0x29
#define YMIN_REG	0x2a
#define YMAX_REG	0x2b

#define HSCALELOW_REG	0x30
#define HSCALEHIGH_REG	0x31
#define VSCALELOW_REG	0x32
#define VSCALEHIGH_REG	0x33

#define AC97LSB_REG	0x40
#define AC97MSB_REG	0x41
#define AC97ADDR_REG	0x42
#define AC97BUSY_REG	0x43

/* em202 registers */
#define MASTER_AC97	0x02
#define LINE_IN_AC97    0x10
#define VIDEO_AC97	0x14

/* register settings */
#define EM2800_AUDIO_SRC_TUNER  0x0d
#define EM2800_AUDIO_SRC_LINE   0x0c
#define EM28XX_AUDIO_SRC_TUNER	0xc0
#define EM28XX_AUDIO_SRC_LINE	0x80

/* FIXME: Need to be populated with the other chip ID's */
enum em28xx_chip_id {
	CHIP_ID_EM2883 = 36,
};
