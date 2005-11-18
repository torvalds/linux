#define TVP5150_VD_IN_SRC_SEL_1      0x00 /* Video input source selection #1 */
#define TVP5150_ANAL_CHL_CTL         0x01 /* Analog channel controls */
#define TVP5150_OP_MODE_CTL          0x02 /* Operation mode controls */
#define TVP5150_MISC_CTL             0x03 /* Miscellaneous controls */
#define TVP5150_AUTOSW_MSK           0x04 /* Autoswitch mask: TVP5150A / TVP5150AM */

/* Reserved 05h */

#define TVP5150_COLOR_KIL_THSH_CTL   0x06 /* Color killer threshold control */
#define TVP5150_LUMA_PROC_CTL_1      0x07 /* Luminance processing control #1 */
#define TVP5150_LUMA_PROC_CTL_2      0x08 /* Luminance processing control #2 */
#define TVP5150_BRIGHT_CTL           0x09 /* Brightness control */
#define TVP5150_SATURATION_CTL       0x0a /* Color saturation control */
#define TVP5150_HUE_CTL              0x0b /* Hue control */
#define TVP5150_CONTRAST_CTL         0x0c /* Contrast control */
#define TVP5150_DATA_RATE_SEL        0x0d /* Outputs and data rates select */
#define TVP5150_LUMA_PROC_CTL_3      0x0e /* Luminance processing control #3 */
#define TVP5150_CONF_SHARED_PIN      0x0f /* Configuration shared pins */

/* Reserved 10h */

#define TVP5150_ACT_VD_CROP_ST_MSB   0x11 /* Active video cropping start MSB */
#define TVP5150_ACT_VD_CROP_ST_LSB   0x12 /* Active video cropping start LSB */
#define TVP5150_ACT_VD_CROP_STP_MSB  0x13 /* Active video cropping stop MSB */
#define TVP5150_ACT_VD_CROP_STP_LSB  0x14 /* Active video cropping stop LSB */
#define TVP5150_GENLOCK              0x15 /* Genlock/RTC */
#define TVP5150_HORIZ_SYNC_START     0x16 /* Horizontal sync start */

/* Reserved 17h */

#define TVP5150_VERT_BLANKING_START 0x18 /* Vertical blanking start */
#define TVP5150_VERT_BLANKING_STOP  0x19 /* Vertical blanking stop */
#define TVP5150_CHROMA_PROC_CTL_1   0x1a /* Chrominance processing control #1 */
#define TVP5150_CHROMA_PROC_CTL_2   0x1b /* Chrominance processing control #2 */
#define TVP5150_INT_RESET_REG_B     0x1c /* Interrupt reset register B */
#define TVP5150_INT_ENABLE_REG_B    0x1d /* Interrupt enable register B */
#define TVP5150_INTT_CONFIG_REG_B   0x1e /* Interrupt configuration register B */

/* Reserved 1Fh-27h */

#define TVP5150_VIDEO_STD           0x28 /* Video standard */

/* Reserved 29h-2bh */

#define TVP5150_CB_GAIN_FACT        0x2c /* Cb gain factor */
#define TVP5150_CR_GAIN_FACTOR      0x2d /* Cr gain factor */
#define TVP5150_MACROVISION_ON_CTR  0x2e /* Macrovision on counter */
#define TVP5150_MACROVISION_OFF_CTR 0x2f /* Macrovision off counter */
#define TVP5150_REV_SELECT          0x30 /* revision select (TVP5150AM1 only) */

/* Reserved	31h-7Fh */

#define TVP5150_MSB_DEV_ID          0x80 /* MSB of device ID */
#define TVP5150_LSB_DEV_ID          0x81 /* LSB of device ID */
#define TVP5150_ROM_MAJOR_VER       0x82 /* ROM major version */
#define TVP5150_ROM_MINOR_VER       0x83 /* ROM minor version */
#define TVP5150_VERT_LN_COUNT_MSB   0x84 /* Vertical line count MSB */
#define TVP5150_VERT_LN_COUNT_LSB   0x85 /* Vertical line count LSB */
#define TVP5150_INT_STATUS_REG_B    0x86 /* Interrupt status register B */
#define TVP5150_INT_ACTIVE_REG_B    0x87 /* Interrupt active register B */
#define TVP5150_STATUS_REG_1        0x88 /* Status register #1 */
#define TVP5150_STATUS_REG_2        0x89 /* Status register #2 */
#define TVP5150_STATUS_REG_3        0x8a /* Status register #3 */
#define TVP5150_STATUS_REG_4        0x8b /* Status register #4 */
#define TVP5150_STATUS_REG_5        0x8c /* Status register #5 */
/* Reserved	8Dh-8Fh */
#define TVP5150_CC_DATA_REG1        0x90 /* Closed caption data registers */
#define TVP5150_CC_DATA_REG2        0x91 /* Closed caption data registers */
#define TVP5150_CC_DATA_REG3        0x92 /* Closed caption data registers */
#define TVP5150_CC_DATA_REG4        0x93 /* Closed caption data registers */
#define TVP5150_WSS_DATA_REG1       0X94 /* WSS data registers */
#define TVP5150_WSS_DATA_REG2       0X95 /* WSS data registers */
#define TVP5150_WSS_DATA_REG3       0X96 /* WSS data registers */
#define TVP5150_WSS_DATA_REG4       0X97 /* WSS data registers */
#define TVP5150_WSS_DATA_REG5       0X98 /* WSS data registers */
#define TVP5150_WSS_DATA_REG6       0X99 /* WSS data registers */
#define TVP5150_VPS_DATA_REG1       0x9a /* VPS data registers */
#define TVP5150_VPS_DATA_REG2       0x9b /* VPS data registers */
#define TVP5150_VPS_DATA_REG3       0x9c /* VPS data registers */
#define TVP5150_VPS_DATA_REG4       0x9d /* VPS data registers */
#define TVP5150_VPS_DATA_REG5       0x9e /* VPS data registers */
#define TVP5150_VPS_DATA_REG6       0x9f /* VPS data registers */
#define TVP5150_VPS_DATA_REG7       0xa0 /* VPS data registers */
#define TVP5150_VPS_DATA_REG8       0xa1 /* VPS data registers */
#define TVP5150_VPS_DATA_REG9       0xa2 /* VPS data registers */
#define TVP5150_VPS_DATA_REG10      0xa3 /* VPS data registers */
#define TVP5150_VPS_DATA_REG11      0xa4 /* VPS data registers */
#define TVP5150_VPS_DATA_REG12      0xa5 /* VPS data registers */
#define TVP5150_VPS_DATA_REG13      0xa6 /* VPS data registers */
#define TVP5150_VITC_DATA_REG1      0xa7 /* VITC data registers */
#define TVP5150_VITC_DATA_REG2      0xa8 /* VITC data registers */
#define TVP5150_VITC_DATA_REG3      0xa9 /* VITC data registers */
#define TVP5150_VITC_DATA_REG4      0xaa /* VITC data registers */
#define TVP5150_VITC_DATA_REG5      0xab /* VITC data registers */
#define TVP5150_VITC_DATA_REG6      0xac /* VITC data registers */
#define TVP5150_VITC_DATA_REG7      0xad /* VITC data registers */
#define TVP5150_VITC_DATA_REG8      0xae /* VITC data registers */
#define TVP5150_VITC_DATA_REG9      0xaf /* VITC data registers */
#define TVP5150_VBI_FIFO_READ_DATA  0xb0 /* VBI FIFO read data */
#define TVP5150_TELETEXT_FIL_1_1    0xb1 /* Teletext filter 1 */
#define TVP5150_TELETEXT_FIL_1_2    0xb2 /* Teletext filter 1 */
#define TVP5150_TELETEXT_FIL_1_3    0xb3 /* Teletext filter 1 */
#define TVP5150_TELETEXT_FIL_1_4    0xb4 /* Teletext filter 1 */
#define TVP5150_TELETEXT_FIL_1_5    0xb5 /* Teletext filter 1 */
#define TVP5150_TELETEXT_FIL_2_1    0xb6 /* Teletext filter 2 */
#define TVP5150_TELETEXT_FIL_2_2    0xb7 /* Teletext filter 2 */
#define TVP5150_TELETEXT_FIL_2_3    0xb8 /* Teletext filter 2 */
#define TVP5150_TELETEXT_FIL_2_4    0xb9 /* Teletext filter 2 */
#define TVP5150_TELETEXT_FIL_2_5    0xba /* Teletext filter 2 */
#define TVP5150_TELETEXT_FIL_ENA    0xbb /* Teletext filter enable */
/* Reserved	BCh-BFh */
#define TVP5150_INT_STATUS_REG_A    0xc0 /* Interrupt status register A */
#define TVP5150_INT_ENABLE_REG_A    0xc1 /* Interrupt enable register A */
#define TVP5150_INT_CONF            0xc2 /* Interrupt configuration */
#define TVP5150_VDP_CONF_RAM_DATA   0xc3 /* VDP configuration RAM data */
#define TVP5150_CONF_RAM_ADDR_LOW   0xc4 /* Configuration RAM address low byte */
#define TVP5150_CONF_RAM_ADDR_HIGH  0xc5 /* Configuration RAM address high byte */
#define TVP5150_VDP_STATUS_REG      0xc6 /* VDP status register */
#define TVP5150_FIFO_WORD_COUNT     0xc7 /* FIFO word count */
#define TVP5150_FIFO_INT_THRESHOLD  0xc8 /* FIFO interrupt threshold */
#define TVP5150_FIFO_RESET          0xc9 /* FIFO reset */
#define TVP5150_LINE_NUMBER_INT     0xca /* Line number interrupt */
#define TVP5150_PIX_ALIGN_REG_LOW   0xcb /* Pixel alignment register low byte */
#define TVP5150_PIX_ALIGN_REG_HIGH  0xcc /* Pixel alignment register high byte */
#define TVP5150_FIFO_OUT_CTRL       0xcd /* FIFO output control */
/* Reserved	CEh */
#define TVP5150_FULL_FIELD_ENA_1    0xcf /* Full field enable 1 */
#define TVP5150_FULL_FIELD_ENA_2    0xd0 /* Full field enable 2 */
#define TVP5150_LINE_MODE_REG_1     0xd1 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_2     0xd2 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_3     0xd3 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_4     0xd4 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_5     0xd5 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_6     0xd6 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_7     0xd7 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_8     0xd8 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_9     0xd9 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_10    0xda /* Line mode registers */
#define TVP5150_LINE_MODE_REG_11    0xdb /* Line mode registers */
#define TVP5150_LINE_MODE_REG_12    0xdc /* Line mode registers */
#define TVP5150_LINE_MODE_REG_13    0xdd /* Line mode registers */
#define TVP5150_LINE_MODE_REG_14    0xde /* Line mode registers */
#define TVP5150_LINE_MODE_REG_15    0xdf /* Line mode registers */
#define TVP5150_LINE_MODE_REG_16    0xe0 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_17    0xe1 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_18    0xe2 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_19    0xe3 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_20    0xe4 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_21    0xe5 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_22    0xe6 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_23    0xe7 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_24    0xe8 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_25    0xe9 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_27    0xea /* Line mode registers */
#define TVP5150_LINE_MODE_REG_28    0xeb /* Line mode registers */
#define TVP5150_LINE_MODE_REG_29    0xec /* Line mode registers */
#define TVP5150_LINE_MODE_REG_30    0xed /* Line mode registers */
#define TVP5150_LINE_MODE_REG_31    0xee /* Line mode registers */
#define TVP5150_LINE_MODE_REG_32    0xef /* Line mode registers */
#define TVP5150_LINE_MODE_REG_33    0xf0 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_34    0xf1 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_35    0xf2 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_36    0xf3 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_37    0xf4 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_38    0xf5 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_39    0xf6 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_40    0xf7 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_41    0xf8 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_42    0xf9 /* Line mode registers */
#define TVP5150_LINE_MODE_REG_43    0xfa /* Line mode registers */
#define TVP5150_LINE_MODE_REG_44    0xfb /* Line mode registers */
#define TVP5150_FULL_FIELD_MODE_REG 0xfc /* Full field mode register */
/* Reserved	FDh-FFh */
