/*
 *
 * philips saa7134 registers
 */

/* ------------------------------------------------------------------ */
/*
 * PCI ID's
 */
#ifndef PCI_DEVICE_ID_PHILIPS_SAA7130
# define PCI_DEVICE_ID_PHILIPS_SAA7130 0x7130
#endif
#ifndef PCI_DEVICE_ID_PHILIPS_SAA7133
# define PCI_DEVICE_ID_PHILIPS_SAA7133 0x7133
#endif
#ifndef PCI_DEVICE_ID_PHILIPS_SAA7134
# define PCI_DEVICE_ID_PHILIPS_SAA7134 0x7134
#endif
#ifndef PCI_DEVICE_ID_PHILIPS_SAA7135
# define PCI_DEVICE_ID_PHILIPS_SAA7135 0x7135
#endif

/* ------------------------------------------------------------------ */
/*
 *  registers -- 32 bit
 */

/* DMA channels, n = 0 ... 6 */
#define SAA7134_RS_BA1(n)			((0x200 >> 2) + 4*n)
#define SAA7134_RS_BA2(n)			((0x204 >> 2) + 4*n)
#define SAA7134_RS_PITCH(n)			((0x208 >> 2) + 4*n)
#define SAA7134_RS_CONTROL(n)			((0x20c >> 2) + 4*n)
#define   SAA7134_RS_CONTROL_WSWAP		(0x01 << 25)
#define   SAA7134_RS_CONTROL_BSWAP		(0x01 << 24)
#define   SAA7134_RS_CONTROL_BURST_2		(0x01 << 21)
#define   SAA7134_RS_CONTROL_BURST_4		(0x02 << 21)
#define   SAA7134_RS_CONTROL_BURST_8		(0x03 << 21)
#define   SAA7134_RS_CONTROL_BURST_16		(0x04 << 21)
#define   SAA7134_RS_CONTROL_BURST_32		(0x05 << 21)
#define   SAA7134_RS_CONTROL_BURST_64		(0x06 << 21)
#define   SAA7134_RS_CONTROL_BURST_MAX		(0x07 << 21)
#define   SAA7134_RS_CONTROL_ME			(0x01 << 20)
#define SAA7134_FIFO_SIZE                       (0x2a0 >> 2)
#define SAA7134_THRESHOULD                      (0x2a4 >> 2)

#define SAA7133_NUM_SAMPLES			(0x588 >> 2)
#define SAA7133_AUDIO_CHANNEL			(0x58c >> 2)
#define SAA7133_AUDIO_FORMAT			(0x58f >> 2)
#define SAA7133_DIGITAL_OUTPUT_SEL1		(0x46c >> 2)
#define SAA7133_DIGITAL_OUTPUT_SEL2		(0x470 >> 2)
#define SAA7133_DIGITAL_INPUT_XBAR1		(0x464 >> 2)
#define SAA7133_ANALOG_IO_SELECT                (0x594 >> 2)

/* main control */
#define SAA7134_MAIN_CTRL                       (0x2a8 >> 2)
#define   SAA7134_MAIN_CTRL_VPLLE		(1 << 15)
#define   SAA7134_MAIN_CTRL_APLLE		(1 << 14)
#define   SAA7134_MAIN_CTRL_EXOSC		(1 << 13)
#define   SAA7134_MAIN_CTRL_EVFE1		(1 << 12)
#define   SAA7134_MAIN_CTRL_EVFE2		(1 << 11)
#define   SAA7134_MAIN_CTRL_ESFE		(1 << 10)
#define   SAA7134_MAIN_CTRL_EBADC		(1 << 9)
#define   SAA7134_MAIN_CTRL_EBDAC		(1 << 8)
#define   SAA7134_MAIN_CTRL_TE6			(1 << 6)
#define   SAA7134_MAIN_CTRL_TE5			(1 << 5)
#define   SAA7134_MAIN_CTRL_TE4			(1 << 4)
#define   SAA7134_MAIN_CTRL_TE3			(1 << 3)
#define   SAA7134_MAIN_CTRL_TE2			(1 << 2)
#define   SAA7134_MAIN_CTRL_TE1			(1 << 1)
#define   SAA7134_MAIN_CTRL_TE0			(1 << 0)

/* DMA status */
#define SAA7134_DMA_STATUS                      (0x2ac >> 2)

/* audio / video status */
#define SAA7134_AV_STATUS			(0x2c0 >> 2)
#define   SAA7134_AV_STATUS_STEREO		(1 << 17)
#define   SAA7134_AV_STATUS_DUAL                (1 << 16)
#define   SAA7134_AV_STATUS_PILOT               (1 << 15)
#define   SAA7134_AV_STATUS_SMB                 (1 << 14)
#define   SAA7134_AV_STATUS_DMB                 (1 << 13)
#define   SAA7134_AV_STATUS_VDSP                (1 << 12)
#define   SAA7134_AV_STATUS_IIC_STATUS          (3 << 10)
#define   SAA7134_AV_STATUS_MVM                 (7 << 7)
#define   SAA7134_AV_STATUS_FIDT                (1 << 6)
#define   SAA7134_AV_STATUS_INTL                (1 << 5)
#define   SAA7134_AV_STATUS_RDCAP               (1 << 4)
#define   SAA7134_AV_STATUS_PWR_ON              (1 << 3)
#define   SAA7134_AV_STATUS_LOAD_ERR            (1 << 2)
#define   SAA7134_AV_STATUS_TRIG_ERR            (1 << 1)
#define   SAA7134_AV_STATUS_CONF_ERR            (1 << 0)

/* interrupt */
#define SAA7134_IRQ1                            (0x2c4 >> 2)
#define   SAA7134_IRQ1_INTE_RA3_1               (1 << 25)
#define   SAA7134_IRQ1_INTE_RA3_0               (1 << 24)
#define   SAA7134_IRQ1_INTE_RA2_3               (1 << 19)
#define   SAA7134_IRQ1_INTE_RA2_2               (1 << 18)
#define   SAA7134_IRQ1_INTE_RA2_1               (1 << 17)
#define   SAA7134_IRQ1_INTE_RA2_0               (1 << 16)
#define   SAA7134_IRQ1_INTE_RA1_3               (1 << 11)
#define   SAA7134_IRQ1_INTE_RA1_2               (1 << 10)
#define   SAA7134_IRQ1_INTE_RA1_1               (1 <<  9)
#define   SAA7134_IRQ1_INTE_RA1_0               (1 <<  8)
#define   SAA7134_IRQ1_INTE_RA0_7               (1 <<  7)
#define   SAA7134_IRQ1_INTE_RA0_6               (1 <<  6)
#define   SAA7134_IRQ1_INTE_RA0_5               (1 <<  5)
#define   SAA7134_IRQ1_INTE_RA0_4               (1 <<  4)
#define   SAA7134_IRQ1_INTE_RA0_3               (1 <<  3)
#define   SAA7134_IRQ1_INTE_RA0_2               (1 <<  2)
#define   SAA7134_IRQ1_INTE_RA0_1               (1 <<  1)
#define   SAA7134_IRQ1_INTE_RA0_0               (1 <<  0)

#define SAA7134_IRQ2                            (0x2c8 >> 2)
#define   SAA7134_IRQ2_INTE_GPIO23A             (1 << 17)
#define   SAA7134_IRQ2_INTE_GPIO23              (1 << 16)
#define   SAA7134_IRQ2_INTE_GPIO22A             (1 << 15)
#define   SAA7134_IRQ2_INTE_GPIO22              (1 << 14)
#define   SAA7134_IRQ2_INTE_GPIO18A             (1 << 13)
#define   SAA7134_IRQ2_INTE_GPIO18              (1 << 12)
#define   SAA7134_IRQ2_INTE_GPIO16              (1 << 11) /* not certain */
#define   SAA7134_IRQ2_INTE_SC2                 (1 << 10)
#define   SAA7134_IRQ2_INTE_SC1                 (1 <<  9)
#define   SAA7134_IRQ2_INTE_SC0                 (1 <<  8)
#define   SAA7134_IRQ2_INTE_DEC5                (1 <<  7)
#define   SAA7134_IRQ2_INTE_DEC4                (1 <<  6)
#define   SAA7134_IRQ2_INTE_DEC3                (1 <<  5)
#define   SAA7134_IRQ2_INTE_DEC2                (1 <<  4)
#define   SAA7134_IRQ2_INTE_DEC1                (1 <<  3)
#define   SAA7134_IRQ2_INTE_DEC0                (1 <<  2)
#define   SAA7134_IRQ2_INTE_PE                  (1 <<  1)
#define   SAA7134_IRQ2_INTE_AR                  (1 <<  0)

#define SAA7134_IRQ_REPORT                      (0x2cc >> 2)
#define   SAA7134_IRQ_REPORT_GPIO23             (1 << 17)
#define   SAA7134_IRQ_REPORT_GPIO22             (1 << 16)
#define   SAA7134_IRQ_REPORT_GPIO18             (1 << 15)
#define   SAA7134_IRQ_REPORT_GPIO16             (1 << 14) /* not certain */
#define   SAA7134_IRQ_REPORT_LOAD_ERR           (1 << 13)
#define   SAA7134_IRQ_REPORT_CONF_ERR           (1 << 12)
#define   SAA7134_IRQ_REPORT_TRIG_ERR           (1 << 11)
#define   SAA7134_IRQ_REPORT_MMC                (1 << 10)
#define   SAA7134_IRQ_REPORT_FIDT               (1 <<  9)
#define   SAA7134_IRQ_REPORT_INTL               (1 <<  8)
#define   SAA7134_IRQ_REPORT_RDCAP              (1 <<  7)
#define   SAA7134_IRQ_REPORT_PWR_ON             (1 <<  6)
#define   SAA7134_IRQ_REPORT_PE                 (1 <<  5)
#define   SAA7134_IRQ_REPORT_AR                 (1 <<  4)
#define   SAA7134_IRQ_REPORT_DONE_RA3           (1 <<  3)
#define   SAA7134_IRQ_REPORT_DONE_RA2           (1 <<  2)
#define   SAA7134_IRQ_REPORT_DONE_RA1           (1 <<  1)
#define   SAA7134_IRQ_REPORT_DONE_RA0           (1 <<  0)
#define SAA7134_IRQ_STATUS                      (0x2d0 >> 2)


/* ------------------------------------------------------------------ */
/*
 *  registers -- 8 bit
 */

/* video decoder */
#define SAA7134_INCR_DELAY                      0x101
#define SAA7134_ANALOG_IN_CTRL1                 0x102
#define SAA7134_ANALOG_IN_CTRL2                 0x103
#define SAA7134_ANALOG_IN_CTRL3                 0x104
#define SAA7134_ANALOG_IN_CTRL4                 0x105
#define SAA7134_HSYNC_START                     0x106
#define SAA7134_HSYNC_STOP                      0x107
#define SAA7134_SYNC_CTRL                       0x108
#define SAA7134_LUMA_CTRL                       0x109
#define SAA7134_DEC_LUMA_BRIGHT                 0x10a
#define SAA7134_DEC_LUMA_CONTRAST               0x10b
#define SAA7134_DEC_CHROMA_SATURATION           0x10c
#define SAA7134_DEC_CHROMA_HUE                  0x10d
#define SAA7134_CHROMA_CTRL1                    0x10e
#define SAA7134_CHROMA_GAIN                     0x10f
#define SAA7134_CHROMA_CTRL2                    0x110
#define SAA7134_MODE_DELAY_CTRL                 0x111

#define SAA7134_ANALOG_ADC                      0x114
#define SAA7134_VGATE_START                     0x115
#define SAA7134_VGATE_STOP                      0x116
#define SAA7134_MISC_VGATE_MSB                  0x117
#define SAA7134_RAW_DATA_GAIN                   0x118
#define SAA7134_RAW_DATA_OFFSET                 0x119
#define SAA7134_STATUS_VIDEO1                   0x11e
#define SAA7134_STATUS_VIDEO2                   0x11f

/* video scaler */
#define SAA7134_SOURCE_TIMING1                  0x000
#define SAA7134_SOURCE_TIMING2                  0x001
#define SAA7134_REGION_ENABLE                   0x004
#define SAA7134_SCALER_STATUS0                  0x006
#define SAA7134_SCALER_STATUS1                  0x007
#define SAA7134_START_GREEN                     0x00c
#define SAA7134_START_BLUE                      0x00d
#define SAA7134_START_RED                       0x00e
#define SAA7134_GREEN_PATH(x)                   (0x010 +x)
#define SAA7134_BLUE_PATH(x)                    (0x020 +x)
#define SAA7134_RED_PATH(x)                     (0x030 +x)

#define TASK_A                                  0x040
#define TASK_B                                  0x080
#define SAA7134_TASK_CONDITIONS(t)              (0x000 +t)
#define SAA7134_FIELD_HANDLING(t)               (0x001 +t)
#define SAA7134_DATA_PATH(t)                    (0x002 +t)
#define SAA7134_VBI_H_START1(t)                 (0x004 +t)
#define SAA7134_VBI_H_START2(t)                 (0x005 +t)
#define SAA7134_VBI_H_STOP1(t)                  (0x006 +t)
#define SAA7134_VBI_H_STOP2(t)                  (0x007 +t)
#define SAA7134_VBI_V_START1(t)                 (0x008 +t)
#define SAA7134_VBI_V_START2(t)                 (0x009 +t)
#define SAA7134_VBI_V_STOP1(t)                  (0x00a +t)
#define SAA7134_VBI_V_STOP2(t)                  (0x00b +t)
#define SAA7134_VBI_H_LEN1(t)                   (0x00c +t)
#define SAA7134_VBI_H_LEN2(t)                   (0x00d +t)
#define SAA7134_VBI_V_LEN1(t)                   (0x00e +t)
#define SAA7134_VBI_V_LEN2(t)                   (0x00f +t)

#define SAA7134_VIDEO_H_START1(t)               (0x014 +t)
#define SAA7134_VIDEO_H_START2(t)               (0x015 +t)
#define SAA7134_VIDEO_H_STOP1(t)                (0x016 +t)
#define SAA7134_VIDEO_H_STOP2(t)                (0x017 +t)
#define SAA7134_VIDEO_V_START1(t)               (0x018 +t)
#define SAA7134_VIDEO_V_START2(t)               (0x019 +t)
#define SAA7134_VIDEO_V_STOP1(t)                (0x01a +t)
#define SAA7134_VIDEO_V_STOP2(t)                (0x01b +t)
#define SAA7134_VIDEO_PIXELS1(t)                (0x01c +t)
#define SAA7134_VIDEO_PIXELS2(t)                (0x01d +t)
#define SAA7134_VIDEO_LINES1(t)                 (0x01e +t)
#define SAA7134_VIDEO_LINES2(t)                 (0x01f +t)

#define SAA7134_H_PRESCALE(t)                   (0x020 +t)
#define SAA7134_ACC_LENGTH(t)                   (0x021 +t)
#define SAA7134_LEVEL_CTRL(t)                   (0x022 +t)
#define SAA7134_FIR_PREFILTER_CTRL(t)           (0x023 +t)
#define SAA7134_LUMA_BRIGHT(t)                  (0x024 +t)
#define SAA7134_LUMA_CONTRAST(t)                (0x025 +t)
#define SAA7134_CHROMA_SATURATION(t)            (0x026 +t)
#define SAA7134_VBI_H_SCALE_INC1(t)             (0x028 +t)
#define SAA7134_VBI_H_SCALE_INC2(t)             (0x029 +t)
#define SAA7134_VBI_PHASE_OFFSET_LUMA(t)        (0x02a +t)
#define SAA7134_VBI_PHASE_OFFSET_CHROMA(t)      (0x02b +t)
#define SAA7134_H_SCALE_INC1(t)                 (0x02c +t)
#define SAA7134_H_SCALE_INC2(t)                 (0x02d +t)
#define SAA7134_H_PHASE_OFF_LUMA(t)             (0x02e +t)
#define SAA7134_H_PHASE_OFF_CHROMA(t)           (0x02f +t)
#define SAA7134_V_SCALE_RATIO1(t)               (0x030 +t)
#define SAA7134_V_SCALE_RATIO2(t)               (0x031 +t)
#define SAA7134_V_FILTER(t)                     (0x032 +t)
#define SAA7134_V_PHASE_OFFSET0(t)              (0x034 +t)
#define SAA7134_V_PHASE_OFFSET1(t)              (0x035 +t)
#define SAA7134_V_PHASE_OFFSET2(t)              (0x036 +t)
#define SAA7134_V_PHASE_OFFSET3(t)              (0x037 +t)

/* clipping & dma */
#define SAA7134_OFMT_VIDEO_A                    0x300
#define SAA7134_OFMT_DATA_A                     0x301
#define SAA7134_OFMT_VIDEO_B                    0x302
#define SAA7134_OFMT_DATA_B                     0x303
#define SAA7134_ALPHA_NOCLIP                    0x304
#define SAA7134_ALPHA_CLIP                      0x305
#define SAA7134_UV_PIXEL                        0x308
#define SAA7134_CLIP_RED                        0x309
#define SAA7134_CLIP_GREEN                      0x30a
#define SAA7134_CLIP_BLUE                       0x30b

/* i2c bus */
#define SAA7134_I2C_ATTR_STATUS                 0x180
#define SAA7134_I2C_DATA                        0x181
#define SAA7134_I2C_CLOCK_SELECT                0x182
#define SAA7134_I2C_TIMER                       0x183

/* audio */
#define SAA7134_NICAM_ADD_DATA1                 0x140
#define SAA7134_NICAM_ADD_DATA2                 0x141
#define SAA7134_NICAM_STATUS                    0x142
#define SAA7134_AUDIO_STATUS                    0x143
#define SAA7134_NICAM_ERROR_COUNT               0x144
#define SAA7134_IDENT_SIF                       0x145
#define SAA7134_LEVEL_READOUT1                  0x146
#define SAA7134_LEVEL_READOUT2                  0x147
#define SAA7134_NICAM_ERROR_LOW                 0x148
#define SAA7134_NICAM_ERROR_HIGH                0x149
#define SAA7134_DCXO_IDENT_CTRL                 0x14a
#define SAA7134_DEMODULATOR                     0x14b
#define SAA7134_AGC_GAIN_SELECT                 0x14c
#define SAA7134_CARRIER1_FREQ0                  0x150
#define SAA7134_CARRIER1_FREQ1                  0x151
#define SAA7134_CARRIER1_FREQ2                  0x152
#define SAA7134_CARRIER2_FREQ0                  0x154
#define SAA7134_CARRIER2_FREQ1                  0x155
#define SAA7134_CARRIER2_FREQ2                  0x156
#define SAA7134_NUM_SAMPLES0                    0x158
#define SAA7134_NUM_SAMPLES1                    0x159
#define SAA7134_NUM_SAMPLES2                    0x15a
#define SAA7134_AUDIO_FORMAT_CTRL               0x15b
#define SAA7134_MONITOR_SELECT                  0x160
#define SAA7134_FM_DEEMPHASIS                   0x161
#define SAA7134_FM_DEMATRIX                     0x162
#define SAA7134_CHANNEL1_LEVEL                  0x163
#define SAA7134_CHANNEL2_LEVEL                  0x164
#define SAA7134_NICAM_CONFIG                    0x165
#define SAA7134_NICAM_LEVEL_ADJUST              0x166
#define SAA7134_STEREO_DAC_OUTPUT_SELECT        0x167
#define SAA7134_I2S_OUTPUT_FORMAT               0x168
#define SAA7134_I2S_OUTPUT_SELECT               0x169
#define SAA7134_I2S_OUTPUT_LEVEL                0x16a
#define SAA7134_DSP_OUTPUT_SELECT               0x16b
#define SAA7134_AUDIO_MUTE_CTRL                 0x16c
#define SAA7134_SIF_SAMPLE_FREQ                 0x16d
#define SAA7134_ANALOG_IO_SELECT                0x16e
#define SAA7134_AUDIO_CLOCK0                    0x170
#define SAA7134_AUDIO_CLOCK1                    0x171
#define SAA7134_AUDIO_CLOCK2                    0x172
#define SAA7134_AUDIO_PLL_CTRL                  0x173
#define SAA7134_AUDIO_CLOCKS_PER_FIELD0         0x174
#define SAA7134_AUDIO_CLOCKS_PER_FIELD1         0x175
#define SAA7134_AUDIO_CLOCKS_PER_FIELD2         0x176

/* video port output */
#define SAA7134_VIDEO_PORT_CTRL0                0x190
#define SAA7134_VIDEO_PORT_CTRL1                0x191
#define SAA7134_VIDEO_PORT_CTRL2                0x192
#define SAA7134_VIDEO_PORT_CTRL3                0x193
#define SAA7134_VIDEO_PORT_CTRL4                0x194
#define SAA7134_VIDEO_PORT_CTRL5                0x195
#define SAA7134_VIDEO_PORT_CTRL6                0x196
#define SAA7134_VIDEO_PORT_CTRL7                0x197
#define SAA7134_VIDEO_PORT_CTRL8                0x198

/* transport stream interface */
#define SAA7134_TS_PARALLEL                     0x1a0
#define SAA7134_TS_PARALLEL_SERIAL              0x1a1
#define SAA7134_TS_SERIAL0                      0x1a2
#define SAA7134_TS_SERIAL1                      0x1a3
#define SAA7134_TS_DMA0                         0x1a4
#define SAA7134_TS_DMA1                         0x1a5
#define SAA7134_TS_DMA2                         0x1a6

/* GPIO Controls */
#define SAA7134_GPIO_GPRESCAN                   0x80
#define SAA7134_GPIO_27_25                      0x0E

#define SAA7134_GPIO_GPMODE0                    0x1B0
#define SAA7134_GPIO_GPMODE1                    0x1B1
#define SAA7134_GPIO_GPMODE2                    0x1B2
#define SAA7134_GPIO_GPMODE3                    0x1B3
#define SAA7134_GPIO_GPSTATUS0                  0x1B4
#define SAA7134_GPIO_GPSTATUS1                  0x1B5
#define SAA7134_GPIO_GPSTATUS2                  0x1B6
#define SAA7134_GPIO_GPSTATUS3                  0x1B7

/* I2S output */
#define SAA7134_I2S_AUDIO_OUTPUT                0x1c0

/* test modes */
#define SAA7134_SPECIAL_MODE                    0x1d0
#define SAA7134_PRODUCTION_TEST_MODE            0x1d1

/* audio -- saa7133 + saa7135 only */
#define SAA7135_DSP_RWSTATE                     0x580
#define SAA7135_DSP_RWSTATE_ERR                 (1 << 3)
#define SAA7135_DSP_RWSTATE_IDA                 (1 << 2)
#define SAA7135_DSP_RWSTATE_RDB                 (1 << 1)
#define SAA7135_DSP_RWSTATE_WRR                 (1 << 0)

#define SAA7135_DSP_RWCLEAR			0x586
#define SAA7135_DSP_RWCLEAR_RERR		    1

#define SAA7133_I2S_AUDIO_CONTROL               0x591
/* ------------------------------------------------------------------ */
/*
 * Local variables:
 * c-basic-offset: 8
 * End:
 */

