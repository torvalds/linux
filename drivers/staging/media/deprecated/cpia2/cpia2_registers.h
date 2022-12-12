/* SPDX-License-Identifier: GPL-2.0-or-later */
/****************************************************************************
 *
 *  Filename: cpia2registers.h
 *
 *  Copyright 2001, STMicrolectronics, Inc.
 *
 *  Description:
 *     Definitions for the CPia2 register set
 *
 ****************************************************************************/

#ifndef CPIA2_REGISTER_HEADER
#define CPIA2_REGISTER_HEADER

/***
 * System register set (Bank 0)
 ***/
#define CPIA2_SYSTEM_DEVICE_HI                     0x00
#define CPIA2_SYSTEM_DEVICE_LO                     0x01

#define CPIA2_SYSTEM_SYSTEM_CONTROL                0x02
#define CPIA2_SYSTEM_CONTROL_LOW_POWER       0x00
#define CPIA2_SYSTEM_CONTROL_HIGH_POWER      0x01
#define CPIA2_SYSTEM_CONTROL_SUSPEND         0x02
#define CPIA2_SYSTEM_CONTROL_V2W_ERR         0x10
#define CPIA2_SYSTEM_CONTROL_RB_ERR          0x10
#define CPIA2_SYSTEM_CONTROL_CLEAR_ERR       0x80

#define CPIA2_SYSTEM_INT_PACKET_CTRL                0x04
#define CPIA2_SYSTEM_INT_PACKET_CTRL_ENABLE_SW_XX 0x01
#define CPIA2_SYSTEM_INT_PACKET_CTRL_ENABLE_EOF   0x02
#define CPIA2_SYSTEM_INT_PACKET_CTRL_ENABLE_INT1  0x04

#define CPIA2_SYSTEM_CACHE_CTRL                     0x05
#define CPIA2_SYSTEM_CACHE_CTRL_CACHE_RESET      0x01
#define CPIA2_SYSTEM_CACHE_CTRL_CACHE_FLUSH      0x02

#define CPIA2_SYSTEM_SERIAL_CTRL                    0x06
#define CPIA2_SYSTEM_SERIAL_CTRL_NULL_CMD        0x00
#define CPIA2_SYSTEM_SERIAL_CTRL_START_CMD       0x01
#define CPIA2_SYSTEM_SERIAL_CTRL_STOP_CMD        0x02
#define CPIA2_SYSTEM_SERIAL_CTRL_WRITE_CMD       0x03
#define CPIA2_SYSTEM_SERIAL_CTRL_READ_ACK_CMD    0x04
#define CPIA2_SYSTEM_SERIAL_CTRL_READ_NACK_CMD   0x05

#define CPIA2_SYSTEM_SERIAL_DATA                     0x07

#define CPIA2_SYSTEM_VP_SERIAL_ADDR                  0x08

/***
 * I2C addresses for various devices in CPiA2
 ***/
#define CPIA2_SYSTEM_VP_SERIAL_ADDR_SENSOR           0x20
#define CPIA2_SYSTEM_VP_SERIAL_ADDR_VP               0x88
#define CPIA2_SYSTEM_VP_SERIAL_ADDR_676_VP           0x8A

#define CPIA2_SYSTEM_SPARE_REG1                      0x09
#define CPIA2_SYSTEM_SPARE_REG2                      0x0A
#define CPIA2_SYSTEM_SPARE_REG3                      0x0B

#define CPIA2_SYSTEM_MC_PORT_0                       0x0C
#define CPIA2_SYSTEM_MC_PORT_1                       0x0D
#define CPIA2_SYSTEM_MC_PORT_2                       0x0E
#define CPIA2_SYSTEM_MC_PORT_3                       0x0F

#define CPIA2_SYSTEM_STATUS_PKT                      0x20
#define CPIA2_SYSTEM_STATUS_PKT_END                  0x27

#define CPIA2_SYSTEM_DESCRIP_VID_HI                  0x30
#define CPIA2_SYSTEM_DESCRIP_VID_LO                  0x31
#define CPIA2_SYSTEM_DESCRIP_PID_HI                  0x32
#define CPIA2_SYSTEM_DESCRIP_PID_LO                  0x33

#define CPIA2_SYSTEM_FW_VERSION_HI                   0x34
#define CPIA2_SYSTEM_FW_VERSION_LO                   0x35

#define CPIA2_SYSTEM_CACHE_START_INDEX               0x80
#define CPIA2_SYSTEM_CACHE_MAX_WRITES                0x10

/***
 * VC register set (Bank 1)
 ***/
#define CPIA2_VC_ASIC_ID                 0x80

#define CPIA2_VC_ASIC_REV                0x81

#define CPIA2_VC_PW_CTRL                 0x82
#define CPIA2_VC_PW_CTRL_COLDSTART      0x01
#define CPIA2_VC_PW_CTRL_CP_CLK_EN      0x02
#define CPIA2_VC_PW_CTRL_VP_RESET_N     0x04
#define CPIA2_VC_PW_CTRL_VC_CLK_EN      0x08
#define CPIA2_VC_PW_CTRL_VC_RESET_N     0x10
#define CPIA2_VC_PW_CTRL_GOTO_SUSPEND   0x20
#define CPIA2_VC_PW_CTRL_UDC_SUSPEND    0x40
#define CPIA2_VC_PW_CTRL_PWR_DOWN       0x80

#define CPIA2_VC_WAKEUP                   0x83
#define CPIA2_VC_WAKEUP_SW_ENABLE       0x01
#define CPIA2_VC_WAKEUP_XX_ENABLE       0x02
#define CPIA2_VC_WAKEUP_SW_ATWAKEUP     0x04
#define CPIA2_VC_WAKEUP_XX_ATWAKEUP     0x08

#define CPIA2_VC_CLOCK_CTRL               0x84
#define CPIA2_VC_CLOCK_CTRL_TESTUP72    0x01

#define CPIA2_VC_INT_ENABLE                0x88
#define CPIA2_VC_INT_ENABLE_XX_IE       0x01
#define CPIA2_VC_INT_ENABLE_SW_IE       0x02
#define CPIA2_VC_INT_ENABLE_VC_IE       0x04
#define CPIA2_VC_INT_ENABLE_USBDATA_IE  0x08
#define CPIA2_VC_INT_ENABLE_USBSETUP_IE 0x10
#define CPIA2_VC_INT_ENABLE_USBCFG_IE   0x20

#define CPIA2_VC_INT_FLAG                  0x89
#define CPIA2_VC_INT_ENABLE_XX_FLAG       0x01
#define CPIA2_VC_INT_ENABLE_SW_FLAG       0x02
#define CPIA2_VC_INT_ENABLE_VC_FLAG       0x04
#define CPIA2_VC_INT_ENABLE_USBDATA_FLAG  0x08
#define CPIA2_VC_INT_ENABLE_USBSETUP_FLAG 0x10
#define CPIA2_VC_INT_ENABLE_USBCFG_FLAG   0x20
#define CPIA2_VC_INT_ENABLE_SET_RESET_BIT 0x80

#define CPIA2_VC_INT_STATE                 0x8A
#define CPIA2_VC_INT_STATE_XX_STATE     0x01
#define CPIA2_VC_INT_STATE_SW_STATE     0x02

#define CPIA2_VC_MP_DIR                    0x90
#define CPIA2_VC_MP_DIR_INPUT           0x00
#define CPIA2_VC_MP_DIR_OUTPUT          0x01

#define CPIA2_VC_MP_DATA                   0x91

#define CPIA2_VC_DP_CTRL                   0x98
#define CPIA2_VC_DP_CTRL_MODE_0         0x00
#define CPIA2_VC_DP_CTRL_MODE_A         0x01
#define CPIA2_VC_DP_CTRL_MODE_B         0x02
#define CPIA2_VC_DP_CTRL_MODE_C         0x03
#define CPIA2_VC_DP_CTRL_FAKE_FST       0x04

#define CPIA2_VC_AD_CTRL                   0x99
#define CPIA2_VC_AD_CTRL_SRC_0          0x00
#define CPIA2_VC_AD_CTRL_SRC_DIGI_A     0x01
#define CPIA2_VC_AD_CTRL_SRC_REG        0x02
#define CPIA2_VC_AD_CTRL_DST_USB        0x00
#define CPIA2_VC_AD_CTRL_DST_REG        0x04

#define CPIA2_VC_AD_TEST_IN                0x9B

#define CPIA2_VC_AD_TEST_OUT               0x9C

#define CPIA2_VC_AD_STATUS                 0x9D
#define CPIA2_VC_AD_STATUS_EMPTY        0x01
#define CPIA2_VC_AD_STATUS_FULL         0x02

#define CPIA2_VC_DP_DATA                   0x9E

#define CPIA2_VC_ST_CTRL                   0xA0
#define CPIA2_VC_ST_CTRL_SRC_VC         0x00
#define CPIA2_VC_ST_CTRL_SRC_DP         0x01
#define CPIA2_VC_ST_CTRL_SRC_REG        0x02

#define CPIA2_VC_ST_CTRL_RAW_SELECT     0x04

#define CPIA2_VC_ST_CTRL_DST_USB        0x00
#define CPIA2_VC_ST_CTRL_DST_DP         0x08
#define CPIA2_VC_ST_CTRL_DST_REG        0x10

#define CPIA2_VC_ST_CTRL_FIFO_ENABLE    0x20
#define CPIA2_VC_ST_CTRL_EOF_DETECT     0x40

#define CPIA2_VC_ST_TEST                   0xA1
#define CPIA2_VC_ST_TEST_MODE_MANUAL    0x00
#define CPIA2_VC_ST_TEST_MODE_INCREMENT 0x02

#define CPIA2_VC_ST_TEST_AUTO_FILL      0x08

#define CPIA2_VC_ST_TEST_REPEAT_FIFO    0x10

#define CPIA2_VC_ST_TEST_IN                0xA2

#define CPIA2_VC_ST_TEST_OUT               0xA3

#define CPIA2_VC_ST_STATUS                 0xA4
#define CPIA2_VC_ST_STATUS_EMPTY        0x01
#define CPIA2_VC_ST_STATUS_FULL         0x02

#define CPIA2_VC_ST_FRAME_DETECT_1         0xA5

#define CPIA2_VC_ST_FRAME_DETECT_2         0xA6

#define CPIA2_VC_USB_CTRL                    0xA8
#define CPIA2_VC_USB_CTRL_CMD_STALLED      0x01
#define CPIA2_VC_USB_CTRL_CMD_READY        0x02
#define CPIA2_VC_USB_CTRL_CMD_STATUS       0x04
#define CPIA2_VC_USB_CTRL_CMD_STATUS_DIR   0x08
#define CPIA2_VC_USB_CTRL_CMD_NO_CLASH     0x10
#define CPIA2_VC_USB_CTRL_CMD_MICRO_ACCESS 0x80

#define CPIA2_VC_USB_STRM                  0xA9
#define CPIA2_VC_USB_STRM_ISO_ENABLE    0x01
#define CPIA2_VC_USB_STRM_BLK_ENABLE    0x02
#define CPIA2_VC_USB_STRM_INT_ENABLE    0x04
#define CPIA2_VC_USB_STRM_AUD_ENABLE    0x08

#define CPIA2_VC_USB_STATUS                   0xAA
#define CPIA2_VC_USB_STATUS_CMD_IN_PROGRESS  0x01
#define CPIA2_VC_USB_STATUS_CMD_STATUS_STALL 0x02
#define CPIA2_VC_USB_STATUS_CMD_HANDSHAKE    0x04
#define CPIA2_VC_USB_STATUS_CMD_OVERRIDE     0x08
#define CPIA2_VC_USB_STATUS_CMD_FIFO_BUSY    0x10
#define CPIA2_VC_USB_STATUS_BULK_REPEAT_TXN  0x20
#define CPIA2_VC_USB_STATUS_CONFIG_DONE      0x40
#define CPIA2_VC_USB_STATUS_USB_SUSPEND      0x80

#define CPIA2_VC_USB_CMDW                   0xAB

#define CPIA2_VC_USB_DATARW                 0xAC

#define CPIA2_VC_USB_INFO                   0xAD

#define CPIA2_VC_USB_CONFIG                 0xAE

#define CPIA2_VC_USB_SETTINGS                  0xAF
#define CPIA2_VC_USB_SETTINGS_CONFIG_MASK    0x03
#define CPIA2_VC_USB_SETTINGS_INTERFACE_MASK 0x0C
#define CPIA2_VC_USB_SETTINGS_ALTERNATE_MASK 0x70

#define CPIA2_VC_USB_ISOLIM                  0xB0

#define CPIA2_VC_USB_ISOFAILS                0xB1

#define CPIA2_VC_USB_ISOMAXPKTHI             0xB2

#define CPIA2_VC_USB_ISOMAXPKTLO             0xB3

#define CPIA2_VC_V2W_CTRL                    0xB8
#define CPIA2_VC_V2W_SELECT               0x01

#define CPIA2_VC_V2W_SCL                     0xB9

#define CPIA2_VC_V2W_SDA                     0xBA

#define CPIA2_VC_VC_CTRL                     0xC0
#define CPIA2_VC_VC_CTRL_RUN              0x01
#define CPIA2_VC_VC_CTRL_SINGLESHOT       0x02
#define CPIA2_VC_VC_CTRL_IDLING           0x04
#define CPIA2_VC_VC_CTRL_INHIBIT_H_TABLES 0x10
#define CPIA2_VC_VC_CTRL_INHIBIT_Q_TABLES 0x20
#define CPIA2_VC_VC_CTRL_INHIBIT_PRIVATE  0x40

#define CPIA2_VC_VC_RESTART_IVAL_HI          0xC1

#define CPIA2_VC_VC_RESTART_IVAL_LO          0xC2

#define CPIA2_VC_VC_FORMAT                   0xC3
#define CPIA2_VC_VC_FORMAT_UFIRST         0x01
#define CPIA2_VC_VC_FORMAT_MONO           0x02
#define CPIA2_VC_VC_FORMAT_DECIMATING     0x04
#define CPIA2_VC_VC_FORMAT_SHORTLINE      0x08
#define CPIA2_VC_VC_FORMAT_SELFTEST       0x10

#define CPIA2_VC_VC_CLOCKS                         0xC4
#define CPIA2_VC_VC_CLOCKS_CLKDIV_MASK        0x03
#define CPIA2_VC_VC_672_CLOCKS_CIF_DIV_BY_3   0x04
#define CPIA2_VC_VC_672_CLOCKS_SCALING        0x08
#define CPIA2_VC_VC_CLOCKS_LOGDIV0        0x00
#define CPIA2_VC_VC_CLOCKS_LOGDIV1        0x01
#define CPIA2_VC_VC_CLOCKS_LOGDIV2        0x02
#define CPIA2_VC_VC_CLOCKS_LOGDIV3        0x03
#define CPIA2_VC_VC_676_CLOCKS_CIF_DIV_BY_3   0x08
#define CPIA2_VC_VC_676_CLOCKS_SCALING	      0x10

#define CPIA2_VC_VC_IHSIZE_LO                0xC5

#define CPIA2_VC_VC_XLIM_HI                  0xC6

#define CPIA2_VC_VC_XLIM_LO                  0xC7

#define CPIA2_VC_VC_YLIM_HI                  0xC8

#define CPIA2_VC_VC_YLIM_LO                  0xC9

#define CPIA2_VC_VC_OHSIZE                   0xCA

#define CPIA2_VC_VC_OVSIZE                   0xCB

#define CPIA2_VC_VC_HCROP                    0xCC

#define CPIA2_VC_VC_VCROP                    0xCD

#define CPIA2_VC_VC_HPHASE                   0xCE

#define CPIA2_VC_VC_VPHASE                   0xCF

#define CPIA2_VC_VC_HISPAN                   0xD0

#define CPIA2_VC_VC_VISPAN                   0xD1

#define CPIA2_VC_VC_HICROP                   0xD2

#define CPIA2_VC_VC_VICROP                   0xD3

#define CPIA2_VC_VC_HFRACT                   0xD4
#define CPIA2_VC_VC_HFRACT_DEN_MASK       0x0F
#define CPIA2_VC_VC_HFRACT_NUM_MASK       0xF0

#define CPIA2_VC_VC_VFRACT                   0xD5
#define CPIA2_VC_VC_VFRACT_DEN_MASK       0x0F
#define CPIA2_VC_VC_VFRACT_NUM_MASK       0xF0

#define CPIA2_VC_VC_JPEG_OPT                      0xD6
#define CPIA2_VC_VC_JPEG_OPT_DOUBLE_SQUEEZE     0x01
#define CPIA2_VC_VC_JPEG_OPT_NO_DC_AUTO_SQUEEZE 0x02
#define CPIA2_VC_VC_JPEG_OPT_AUTO_SQUEEZE       0x04
#define CPIA2_VC_VC_JPEG_OPT_DEFAULT      (CPIA2_VC_VC_JPEG_OPT_DOUBLE_SQUEEZE|\
					   CPIA2_VC_VC_JPEG_OPT_AUTO_SQUEEZE)


#define CPIA2_VC_VC_CREEP_PERIOD             0xD7
#define CPIA2_VC_VC_USER_SQUEEZE             0xD8
#define CPIA2_VC_VC_TARGET_KB                0xD9

#define CPIA2_VC_VC_AUTO_SQUEEZE             0xE6


/***
 * VP register set (Bank 2)
 ***/
#define CPIA2_VP_DEVICEH                             0
#define CPIA2_VP_DEVICEL                             1

#define CPIA2_VP_SYSTEMSTATE                         0x02
#define CPIA2_VP_SYSTEMSTATE_HK_ALIVE             0x01

#define CPIA2_VP_SYSTEMCTRL                          0x03
#define CPIA2_VP_SYSTEMCTRL_REQ_CLEAR_ERROR       0x80
#define CPIA2_VP_SYSTEMCTRL_POWER_DOWN_PLL        0x20
#define CPIA2_VP_SYSTEMCTRL_REQ_SUSPEND_STATE     0x10
#define CPIA2_VP_SYSTEMCTRL_REQ_SERIAL_WAKEUP     0x08
#define CPIA2_VP_SYSTEMCTRL_REQ_AUTOLOAD          0x04
#define CPIA2_VP_SYSTEMCTRL_HK_CONTROL            0x02
#define CPIA2_VP_SYSTEMCTRL_POWER_CONTROL         0x01

#define CPIA2_VP_SENSOR_FLAGS                        0x05
#define CPIA2_VP_SENSOR_FLAGS_404                 0x01
#define CPIA2_VP_SENSOR_FLAGS_407                 0x02
#define CPIA2_VP_SENSOR_FLAGS_409                 0x04
#define CPIA2_VP_SENSOR_FLAGS_410                 0x08
#define CPIA2_VP_SENSOR_FLAGS_500                 0x10

#define CPIA2_VP_SENSOR_REV                          0x06

#define CPIA2_VP_DEVICE_CONFIG                       0x07
#define CPIA2_VP_DEVICE_CONFIG_SERIAL_BRIDGE      0x01

#define CPIA2_VP_GPIO_DIRECTION                      0x08
#define CPIA2_VP_GPIO_READ                        0xFF
#define CPIA2_VP_GPIO_WRITE                       0x00

#define CPIA2_VP_GPIO_DATA                           0x09

#define CPIA2_VP_RAM_ADDR_H                          0x0A
#define CPIA2_VP_RAM_ADDR_L                          0x0B
#define CPIA2_VP_RAM_DATA                            0x0C

#define CPIA2_VP_PATCH_REV                           0x0F

#define CPIA2_VP4_USER_MODE                           0x10
#define CPIA2_VP5_USER_MODE                           0x13
#define CPIA2_VP_USER_MODE_CIF                    0x01
#define CPIA2_VP_USER_MODE_QCIFDS                 0x02
#define CPIA2_VP_USER_MODE_QCIFPTC                0x04
#define CPIA2_VP_USER_MODE_QVGADS                 0x08
#define CPIA2_VP_USER_MODE_QVGAPTC                0x10
#define CPIA2_VP_USER_MODE_VGA                    0x20

#define CPIA2_VP4_FRAMERATE_REQUEST                    0x11
#define CPIA2_VP5_FRAMERATE_REQUEST                    0x14
#define CPIA2_VP_FRAMERATE_60                     0x80
#define CPIA2_VP_FRAMERATE_50                     0x40
#define CPIA2_VP_FRAMERATE_30                     0x20
#define CPIA2_VP_FRAMERATE_25                     0x10
#define CPIA2_VP_FRAMERATE_15                     0x08
#define CPIA2_VP_FRAMERATE_12_5                   0x04
#define CPIA2_VP_FRAMERATE_7_5                    0x02
#define CPIA2_VP_FRAMERATE_6_25                   0x01

#define CPIA2_VP4_USER_EFFECTS                         0x12
#define CPIA2_VP5_USER_EFFECTS                         0x15
#define CPIA2_VP_USER_EFFECTS_COLBARS             0x01
#define CPIA2_VP_USER_EFFECTS_COLBARS_GRAD        0x02
#define CPIA2_VP_USER_EFFECTS_MIRROR              0x04
#define CPIA2_VP_USER_EFFECTS_FLIP                0x40  // VP5 only

/* NOTE: CPIA2_VP_EXPOSURE_MODES shares the same register as VP5 User
 * Effects */
#define CPIA2_VP_EXPOSURE_MODES                       0x15
#define CPIA2_VP_EXPOSURE_MODES_INHIBIT_FLICKER   0x20
#define CPIA2_VP_EXPOSURE_MODES_COMPILE_EXP       0x10

#define CPIA2_VP4_EXPOSURE_TARGET                     0x16    // VP4
#define CPIA2_VP5_EXPOSURE_TARGET		      0x20    // VP5

#define CPIA2_VP_FLICKER_MODES                        0x1B
#define CPIA2_VP_FLICKER_MODES_50HZ               0x80
#define CPIA2_VP_FLICKER_MODES_CUSTOM_FLT_FFREQ   0x40
#define CPIA2_VP_FLICKER_MODES_NEVER_FLICKER      0x20
#define CPIA2_VP_FLICKER_MODES_INHIBIT_RUB        0x10
#define CPIA2_VP_FLICKER_MODES_ADJUST_LINE_FREQ   0x08
#define CPIA2_VP_FLICKER_MODES_CUSTOM_INT_FFREQ   0x04

#define CPIA2_VP_UMISC                                0x1D
#define CPIA2_VP_UMISC_FORCE_MONO                 0x80
#define CPIA2_VP_UMISC_FORCE_ID_MASK              0x40
#define CPIA2_VP_UMISC_INHIBIT_AUTO_FGS           0x20
#define CPIA2_VP_UMISC_INHIBIT_AUTO_DIMS          0x08
#define CPIA2_VP_UMISC_OPT_FOR_SENSOR_DS          0x04
#define CPIA2_VP_UMISC_INHIBIT_AUTO_MODE_INT      0x02

#define CPIA2_VP5_ANTIFLKRSETUP                       0x22  //34

#define CPIA2_VP_INTERPOLATION                        0x24
#define CPIA2_VP_INTERPOLATION_EVEN_FIRST         0x40
#define CPIA2_VP_INTERPOLATION_HJOG               0x20
#define CPIA2_VP_INTERPOLATION_VJOG               0x10

#define CPIA2_VP_GAMMA                                0x25
#define CPIA2_VP_DEFAULT_GAMMA                    0x10

#define CPIA2_VP_YRANGE                               0x26

#define CPIA2_VP_SATURATION                           0x27

#define CPIA2_VP5_MYBLACK_LEVEL                       0x3A   //58
#define CPIA2_VP5_MCYRANGE                            0x3B   //59
#define CPIA2_VP5_MYCEILING                           0x3C   //60
#define CPIA2_VP5_MCUVSATURATION                      0x3D   //61


#define CPIA2_VP_REHASH_VALUES                        0x60


/***
 * Common sensor registers
 ***/
#define CPIA2_SENSOR_DEVICE_H                         0x00
#define CPIA2_SENSOR_DEVICE_L                         0x01

#define CPIA2_SENSOR_DATA_FORMAT                      0x16
#define CPIA2_SENSOR_DATA_FORMAT_HMIRROR      0x08
#define CPIA2_SENSOR_DATA_FORMAT_VMIRROR      0x10

#define CPIA2_SENSOR_CR1                              0x76
#define CPIA2_SENSOR_CR1_STAND_BY             0x01
#define CPIA2_SENSOR_CR1_DOWN_RAMP_GEN        0x02
#define CPIA2_SENSOR_CR1_DOWN_COLUMN_ADC      0x04
#define CPIA2_SENSOR_CR1_DOWN_CAB_REGULATOR   0x08
#define CPIA2_SENSOR_CR1_DOWN_AUDIO_REGULATOR 0x10
#define CPIA2_SENSOR_CR1_DOWN_VRT_AMP         0x20
#define CPIA2_SENSOR_CR1_DOWN_BAND_GAP        0x40

#endif
