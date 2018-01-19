/*
 * register description for HopeRf rf69 radio module
 *
 * Copyright (C) 2016 Wolf-Entwicklungen
 *	Marcus Wolf <linux@wolf-entwicklungen.de>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

/*******************************************/
/* RF69 register addresses		   */
/*******************************************/
#define  REG_FIFO			0x00
#define  REG_OPMODE			0x01
#define  REG_DATAMODUL			0x02
#define  REG_BITRATE_MSB		0x03
#define  REG_BITRATE_LSB		0x04
#define  REG_FDEV_MSB			0x05
#define  REG_FDEV_LSB			0x06
#define  REG_FRF_MSB			0x07
#define  REG_FRF_MID			0x08
#define  REG_FRF_LSB			0x09
#define  REG_OSC1			0x0A
#define  REG_AFCCTRL			0x0B
#define  REG_LOWBAT			0x0C
#define  REG_LISTEN1			0x0D
#define  REG_LISTEN2			0x0E
#define  REG_LISTEN3			0x0F
#define  REG_VERSION			0x10
#define  REG_PALEVEL			0x11
#define  REG_PARAMP			0x12
#define  REG_OCP			0x13
#define  REG_AGCREF			0x14 /* not available on RF69 */
#define  REG_AGCTHRESH1			0x15 /* not available on RF69 */
#define  REG_AGCTHRESH2			0x16 /* not available on RF69 */
#define  REG_AGCTHRESH3			0x17 /* not available on RF69 */
#define  REG_LNA			0x18
#define  REG_RXBW			0x19
#define  REG_AFCBW			0x1A
#define  REG_OOKPEAK			0x1B
#define  REG_OOKAVG			0x1C
#define  REG_OOKFIX			0x1D
#define  REG_AFCFEI			0x1E
#define  REG_AFCMSB			0x1F
#define  REG_AFCLSB			0x20
#define  REG_FEIMSB			0x21
#define  REG_FEILSB			0x22
#define  REG_RSSICONFIG			0x23
#define  REG_RSSIVALUE			0x24
#define  REG_DIOMAPPING1		0x25
#define  REG_DIOMAPPING2		0x26
#define  REG_IRQFLAGS1			0x27
#define  REG_IRQFLAGS2			0x28
#define  REG_RSSITHRESH			0x29
#define  REG_RXTIMEOUT1			0x2A
#define  REG_RXTIMEOUT2			0x2B
#define  REG_PREAMBLE_MSB		0x2C
#define  REG_PREAMBLE_LSB		0x2D
#define  REG_SYNC_CONFIG		0x2E
#define  REG_SYNCVALUE1			0x2F
#define  REG_SYNCVALUE2			0x30
#define  REG_SYNCVALUE3			0x31
#define  REG_SYNCVALUE4			0x32
#define  REG_SYNCVALUE5			0x33
#define  REG_SYNCVALUE6			0x34
#define  REG_SYNCVALUE7			0x35
#define  REG_SYNCVALUE8			0x36
#define  REG_PACKETCONFIG1		0x37
#define  REG_PAYLOAD_LENGTH		0x38
#define  REG_NODEADRS			0x39
#define  REG_BROADCASTADRS		0x3A
#define  REG_AUTOMODES			0x3B
#define  REG_FIFO_THRESH		0x3C
#define  REG_PACKETCONFIG2		0x3D
#define  REG_AESKEY1			0x3E
#define  REG_AESKEY2			0x3F
#define  REG_AESKEY3			0x40
#define  REG_AESKEY4			0x41
#define  REG_AESKEY5			0x42
#define  REG_AESKEY6			0x43
#define  REG_AESKEY7			0x44
#define  REG_AESKEY8			0x45
#define  REG_AESKEY9			0x46
#define  REG_AESKEY10			0x47
#define  REG_AESKEY11			0x48
#define  REG_AESKEY12			0x49
#define  REG_AESKEY13			0x4A
#define  REG_AESKEY14			0x4B
#define  REG_AESKEY15			0x4C
#define  REG_AESKEY16			0x4D
#define  REG_TEMP1			0x4E
#define  REG_TEMP2			0x4F
#define  REG_TESTPA1			0x5A /* only present on RFM69HW */
#define  REG_TESTPA2			0x5C /* only present on RFM69HW */
#define  REG_TESTDAGC			0x6F

/******************************************************/
/* RF69/SX1231 bit definition				*/
/******************************************************/
/* write bit */
#define WRITE_BIT				0x80

/* RegOpMode */
#define  MASK_OPMODE_SEQUENCER_OFF		0x80
#define  MASK_OPMODE_LISTEN_ON			0x40
#define  MASK_OPMODE_LISTEN_ABORT		0x20
#define  MASK_OPMODE_MODE			0x1C

#define  OPMODE_MODE_SLEEP			0x00
#define  OPMODE_MODE_STANDBY			0x04 /* default */
#define  OPMODE_MODE_SYNTHESIZER		0x08
#define  OPMODE_MODE_TRANSMIT			0x0C
#define  OPMODE_MODE_RECEIVE			0x10

/* RegDataModul */
#define  MASK_DATAMODUL_MODE			0x06
#define  MASK_DATAMODUL_MODULATION_TYPE		0x18
#define  MASK_DATAMODUL_MODULATION_SHAPE	0x03

#define  DATAMODUL_MODE_PACKET			0x00 /* default */
#define  DATAMODUL_MODE_CONTINUOUS		0x40
#define  DATAMODUL_MODE_CONTINUOUS_NOSYNC	0x60

#define  DATAMODUL_MODULATION_TYPE_FSK		0x00 /* default */
#define  DATAMODUL_MODULATION_TYPE_OOK		0x08

#define  DATAMODUL_MODULATION_SHAPE_NONE	0x00 /* default */
#define  DATAMODUL_MODULATION_SHAPE_1_0		0x01
#define  DATAMODUL_MODULATION_SHAPE_0_5		0x02
#define  DATAMODUL_MODULATION_SHAPE_0_3		0x03
#define  DATAMODUL_MODULATION_SHAPE_BR		0x01
#define  DATAMODUL_MODULATION_SHAPE_2BR		0x02

/* RegFDevMsb (0x05)*/
#define FDEVMASB_MASK				0x3f

/*
 * // RegOsc1
 * #define  OSC1_RCCAL_START			0x80
 * #define  OSC1_RCCAL_DONE			0x40
 *
 * // RegLowBat
 * #define  LOWBAT_MONITOR				0x10
 * #define  LOWBAT_ON				0x08
 * #define  LOWBAT_OFF				0x00  // Default
 *
 * #define  LOWBAT_TRIM_1695			0x00
 * #define  LOWBAT_TRIM_1764			0x01
 * #define  LOWBAT_TRIM_1835			0x02  // Default
 * #define  LOWBAT_TRIM_1905			0x03
 * #define  LOWBAT_TRIM_1976			0x04
 * #define  LOWBAT_TRIM_2045			0x05
 * #define  LOWBAT_TRIM_2116			0x06
 * #define  LOWBAT_TRIM_2185			0x07
 *
 *
 * // RegListen1
 * #define  LISTEN1_RESOL_64			0x50
 * #define  LISTEN1_RESOL_4100			0xA0  // Default
 * #define  LISTEN1_RESOL_262000			0xF0
 *
 * #define  LISTEN1_CRITERIA_RSSI			0x00  // Default
 * #define  LISTEN1_CRITERIA_RSSIANDSYNC		0x08
 *
 * #define  LISTEN1_END_00				0x00
 * #define  LISTEN1_END_01				0x02  // Default
 * #define  LISTEN1_END_10				0x04
 *
 *
 * // RegListen2
 * #define  LISTEN2_COEFIDLE_VALUE			0xF5 // Default
 *
 * // RegListen3
 * #define  LISTEN3_COEFRX_VALUE			0x20 // Default
 */

// RegPaLevel
#define  MASK_PALEVEL_PA0			0x80
#define  MASK_PALEVEL_PA1			0x40
#define  MASK_PALEVEL_PA2			0x20
#define  MASK_PALEVEL_OUTPUT_POWER		0x1F



// RegPaRamp
#define  PARAMP_3400				0x00
#define  PARAMP_2000				0x01
#define  PARAMP_1000				0x02
#define  PARAMP_500				0x03
#define  PARAMP_250				0x04
#define  PARAMP_125				0x05
#define  PARAMP_100				0x06
#define  PARAMP_62				0x07
#define  PARAMP_50				0x08
#define  PARAMP_40				0x09 /* default */
#define  PARAMP_31				0x0A
#define  PARAMP_25				0x0B
#define  PARAMP_20				0x0C
#define  PARAMP_15				0x0D
#define  PARAMP_12				0x0E
#define  PARAMP_10				0x0F

#define  MASK_PARAMP				0x0F

/*
 * // RegOcp
 * #define  OCP_OFF				0x0F
 * #define  OCP_ON					0x1A  // Default
 *
 * #define  OCP_TRIM_45				0x00
 * #define  OCP_TRIM_50				0x01
 * #define  OCP_TRIM_55				0x02
 * #define  OCP_TRIM_60				0x03
 * #define  OCP_TRIM_65				0x04
 * #define  OCP_TRIM_70				0x05
 * #define  OCP_TRIM_75				0x06
 * #define  OCP_TRIM_80				0x07
 * #define  OCP_TRIM_85				0x08
 * #define  OCP_TRIM_90				0x09
 * #define  OCP_TRIM_95				0x0A
 * #define  OCP_TRIM_100				0x0B  // Default
 * #define  OCP_TRIM_105				0x0C
 * #define  OCP_TRIM_110				0x0D
 * #define  OCP_TRIM_115				0x0E
 * #define  OCP_TRIM_120				0x0F
 */

/* RegLna (0x18) */
#define  MASK_LNA_ZIN				0x80
#define  MASK_LNA_CURRENT_GAIN			0x38
#define  MASK_LNA_GAIN				0x07

#define  LNA_GAIN_AUTO				0x00 /* default */
#define  LNA_GAIN_MAX				0x01
#define  LNA_GAIN_MAX_MINUS_6			0x02
#define  LNA_GAIN_MAX_MINUS_12			0x03
#define  LNA_GAIN_MAX_MINUS_24			0x04
#define  LNA_GAIN_MAX_MINUS_36			0x05
#define  LNA_GAIN_MAX_MINUS_48			0x06


/* RegRxBw (0x19) and RegAfcBw (0x1A) */
#define  MASK_BW_DCC_FREQ			0xE0
#define  MASK_BW_MANTISSE			0x18
#define  MASK_BW_EXPONENT			0x07

#define  BW_DCC_16_PERCENT			0x00
#define  BW_DCC_8_PERCENT			0x20
#define  BW_DCC_4_PERCENT			0x40 /* default */
#define  BW_DCC_2_PERCENT			0x60
#define  BW_DCC_1_PERCENT			0x80
#define  BW_DCC_0_5_PERCENT			0xA0
#define  BW_DCC_0_25_PERCENT			0xC0
#define  BW_DCC_0_125_PERCENT			0xE0

#define  BW_MANT_16				0x00
#define  BW_MANT_20				0x08
#define  BW_MANT_24				0x10 /* default */


/* RegOokPeak (0x1B) */
#define  MASK_OOKPEAK_THRESTYPE			0xc0
#define  MASK_OOKPEAK_THRESSTEP			0x38
#define  MASK_OOKPEAK_THRESDEC			0x07

#define  OOKPEAK_THRESHTYPE_FIXED		0x00
#define  OOKPEAK_THRESHTYPE_PEAK		0x40 /* default */
#define  OOKPEAK_THRESHTYPE_AVERAGE		0x80

#define  OOKPEAK_THRESHSTEP_0_5_DB		0x00 /* default */
#define  OOKPEAK_THRESHSTEP_1_0_DB		0x08
#define  OOKPEAK_THRESHSTEP_1_5_DB		0x10
#define  OOKPEAK_THRESHSTEP_2_0_DB		0x18
#define  OOKPEAK_THRESHSTEP_3_0_DB		0x20
#define  OOKPEAK_THRESHSTEP_4_0_DB		0x28
#define  OOKPEAK_THRESHSTEP_5_0_DB		0x30
#define  OOKPEAK_THRESHSTEP_6_0_DB		0x38

#define  OOKPEAK_THRESHDEC_ONCE			0x00 /* default */
#define  OOKPEAK_THRESHDEC_EVERY_2ND		0x01
#define  OOKPEAK_THRESHDEC_EVERY_4TH		0x02
#define  OOKPEAK_THRESHDEC_EVERY_8TH		0x03
#define  OOKPEAK_THRESHDEC_TWICE		0x04
#define  OOKPEAK_THRESHDEC_4_TIMES		0x05
#define  OOKPEAK_THRESHDEC_8_TIMES		0x06
#define  OOKPEAK_THRESHDEC_16_TIMES		0x07

/*
 * // RegOokAvg
 * #define  OOKAVG_AVERAGETHRESHFILT_00		0x00
 * #define  OOKAVG_AVERAGETHRESHFILT_01		0x40
 * #define  OOKAVG_AVERAGETHRESHFILT_10		0x80  // Default
 * #define  OOKAVG_AVERAGETHRESHFILT_11		0xC0
 *
 *
 * // RegAfcFei
 * #define  AFCFEI_FEI_DONE			0x40
 * #define  AFCFEI_FEI_START			0x20
 * #define  AFCFEI_AFC_DONE			0x10
 * #define  AFCFEI_AFCAUTOCLEAR_ON			0x08
 * #define  AFCFEI_AFCAUTOCLEAR_OFF		0x00  // Default
 *
 * #define  AFCFEI_AFCAUTO_ON			0x04
 * #define  AFCFEI_AFCAUTO_OFF			0x00  // Default
 *
 * #define  AFCFEI_AFC_CLEAR			0x02
 * #define  AFCFEI_AFC_START			0x01
 *
 * // RegRssiConfig
 * #define  RSSI_FASTRX_ON				0x08
 * #define  RSSI_FASTRX_OFF			0x00  // Default
 * #define  RSSI_DONE				0x02
 * #define  RSSI_START				0x01
 */

/* RegDioMapping1 */
#define  MASK_DIO0				0xC0
#define  MASK_DIO1				0x30
#define  MASK_DIO2				0x0C
#define  MASK_DIO3				0x03
#define  SHIFT_DIO0				6
#define  SHIFT_DIO1				4
#define  SHIFT_DIO2				2
#define  SHIFT_DIO3				0

/* RegDioMapping2 */
#define  MASK_DIO4				0xC0
#define  MASK_DIO5				0x30
#define  SHIFT_DIO4				6
#define  SHIFT_DIO5				4

/* DIO numbers */
#define  DIO0					0
#define  DIO1					1
#define  DIO2					2
#define  DIO3					3
#define  DIO4					4
#define  DIO5					5

/* DIO Mapping values (packet mode) */
#define  DIO_ModeReady_DIO4			0x00
#define  DIO_ModeReady_DIO5			0x03
#define  DIO_ClkOut				0x00
#define  DIO_Data				0x01
#define  DIO_TimeOut_DIO1			0x03
#define  DIO_TimeOut_DIO4			0x00
#define  DIO_Rssi_DIO0				0x03
#define  DIO_Rssi_DIO3_4			0x01
#define  DIO_RxReady				0x02
#define  DIO_PLLLock				0x03
#define  DIO_TxReady				0x01
#define  DIO_FifoFull_DIO1			0x01
#define  DIO_FifoFull_DIO3			0x00
#define  DIO_SyncAddress			0x02
#define  DIO_FifoNotEmpty_DIO1			0x02
#define  DIO_FifoNotEmpty_FIO2			0x00
#define  DIO_Automode				0x04
#define  DIO_FifoLevel				0x00
#define  DIO_CrcOk				0x00
#define  DIO_PayloadReady			0x01
#define  DIO_PacketSent				0x00
#define  DIO_Dclk				0x00

/* RegDioMapping2 CLK_OUT part */
#define  MASK_DIOMAPPING2_CLK_OUT		0x07

#define  DIOMAPPING2_CLK_OUT_NO_DIV		0x00
#define  DIOMAPPING2_CLK_OUT_DIV_2		0x01
#define  DIOMAPPING2_CLK_OUT_DIV_4		0x02
#define  DIOMAPPING2_CLK_OUT_DIV_8		0x03
#define  DIOMAPPING2_CLK_OUT_DIV_16		0x04
#define  DIOMAPPING2_CLK_OUT_DIV_32		0x05
#define  DIOMAPPING2_CLK_OUT_RC			0x06
#define  DIOMAPPING2_CLK_OUT_OFF		0x07 /* default */

/* RegIrqFlags1 */
#define  MASK_IRQFLAGS1_MODE_READY		0x80
#define  MASK_IRQFLAGS1_RX_READY		0x40
#define  MASK_IRQFLAGS1_TX_READY		0x20
#define  MASK_IRQFLAGS1_PLL_LOCK		0x10
#define  MASK_IRQFLAGS1_RSSI			0x08
#define  MASK_IRQFLAGS1_TIMEOUT			0x04
#define  MASK_IRQFLAGS1_AUTOMODE		0x02
#define  MASK_IRQFLAGS1_SYNC_ADDRESS_MATCH	0x01

/* RegIrqFlags2 */
#define  MASK_IRQFLAGS2_FIFO_FULL		0x80
#define  MASK_IRQFLAGS2_FIFO_NOT_EMPTY		0x40
#define  MASK_IRQFLAGS2_FIFO_LEVEL		0x20
#define  MASK_IRQFLAGS2_FIFO_OVERRUN		0x10
#define  MASK_IRQFLAGS2_PACKET_SENT		0x08
#define  MASK_IRQFLAGS2_PAYLOAD_READY		0x04
#define  MASK_IRQFLAGS2_CRC_OK			0x02
#define  MASK_IRQFLAGS2_LOW_BAT			0x01

/* RegSyncConfig */
#define  MASK_SYNC_CONFIG_SYNC_ON		0x80 /* default */
#define  MASK_SYNC_CONFIG_FIFO_FILL_CONDITION	0x40
#define  MASK_SYNC_CONFIG_SYNC_SIZE		0x38
#define  MASK_SYNC_CONFIG_SYNC_TOLERANCE	0x07

/* RegPacketConfig1 */
#define  MASK_PACKETCONFIG1_PAKET_FORMAT_VARIABLE	0x80
#define  MASK_PACKETCONFIG1_DCFREE			0x60
#define  MASK_PACKETCONFIG1_CRC_ON			0x10 /* default */
#define  MASK_PACKETCONFIG1_CRCAUTOCLEAR_OFF		0x08
#define  MASK_PACKETCONFIG1_ADDRESSFILTERING		0x06

#define  PACKETCONFIG1_DCFREE_OFF			0x00 /* default */
#define  PACKETCONFIG1_DCFREE_MANCHESTER		0x20
#define  PACKETCONFIG1_DCFREE_WHITENING			0x40
#define  PACKETCONFIG1_ADDRESSFILTERING_OFF		0x00 /* default */
#define  PACKETCONFIG1_ADDRESSFILTERING_NODE		0x02
#define  PACKETCONFIG1_ADDRESSFILTERING_NODEBROADCAST	0x04

/*
 * // RegAutoModes
 * #define  AUTOMODES_ENTER_OFF			0x00  // Default
 * #define  AUTOMODES_ENTER_FIFONOTEMPTY		0x20
 * #define  AUTOMODES_ENTER_FIFOLEVEL		0x40
 * #define  AUTOMODES_ENTER_CRCOK			0x60
 * #define  AUTOMODES_ENTER_PAYLOADREADY		0x80
 * #define  AUTOMODES_ENTER_SYNCADRSMATCH		0xA0
 * #define  AUTOMODES_ENTER_PACKETSENT		0xC0
 * #define  AUTOMODES_ENTER_FIFOEMPTY		0xE0
 *
 * #define  AUTOMODES_EXIT_OFF			0x00  // Default
 * #define  AUTOMODES_EXIT_FIFOEMPTY		0x04
 * #define  AUTOMODES_EXIT_FIFOLEVEL		0x08
 * #define  AUTOMODES_EXIT_CRCOK			0x0C
 * #define  AUTOMODES_EXIT_PAYLOADREADY		0x10
 * #define  AUTOMODES_EXIT_SYNCADRSMATCH		0x14
 * #define  AUTOMODES_EXIT_PACKETSENT		0x18
 * #define  AUTOMODES_EXIT_RXTIMEOUT		0x1C
 *
 * #define  AUTOMODES_INTERMEDIATE_SLEEP		0x00  // Default
 * #define  AUTOMODES_INTERMEDIATE_STANDBY		0x01
 * #define  AUTOMODES_INTERMEDIATE_RECEIVER	0x02
 * #define  AUTOMODES_INTERMEDIATE_TRANSMITTER	0x03
 *
 */
/* RegFifoThresh (0x3c) */
#define  MASK_FIFO_THRESH_TXSTART		0x80
#define  MASK_FIFO_THRESH_VALUE			0x7F

/*
 *
 * // RegPacketConfig2
 * #define  PACKET2_RXRESTARTDELAY_1BIT		0x00  // Default
 * #define  PACKET2_RXRESTARTDELAY_2BITS		0x10
 * #define  PACKET2_RXRESTARTDELAY_4BITS		0x20
 * #define  PACKET2_RXRESTARTDELAY_8BITS		0x30
 * #define  PACKET2_RXRESTARTDELAY_16BITS		0x40
 * #define  PACKET2_RXRESTARTDELAY_32BITS		0x50
 * #define  PACKET2_RXRESTARTDELAY_64BITS		0x60
 * #define  PACKET2_RXRESTARTDELAY_128BITS		0x70
 * #define  PACKET2_RXRESTARTDELAY_256BITS		0x80
 * #define  PACKET2_RXRESTARTDELAY_512BITS		0x90
 * #define  PACKET2_RXRESTARTDELAY_1024BITS	0xA0
 * #define  PACKET2_RXRESTARTDELAY_2048BITS	0xB0
 * #define  PACKET2_RXRESTARTDELAY_NONE		0xC0
 * #define  PACKET2_RXRESTART			0x04
 *
 * #define  PACKET2_AUTORXRESTART_ON		0x02  // Default
 * #define  PACKET2_AUTORXRESTART_OFF		0x00
 *
 * #define  PACKET2_AES_ON				0x01
 * #define  PACKET2_AES_OFF			0x00  // Default
 *
 *
 * // RegTemp1
 * #define  TEMP1_MEAS_START			0x08
 * #define  TEMP1_MEAS_RUNNING			0x04
 * #define  TEMP1_ADCLOWPOWER_ON			0x01  // Default
 * #define  TEMP1_ADCLOWPOWER_OFF			0x00
 */

// RegTestDagc (0x6F)
#define  DAGC_NORMAL				0x00 /* Reset value */
#define  DAGC_IMPROVED_LOWBETA1			0x20
#define  DAGC_IMPROVED_LOWBETA0			0x30 /* Recommended val */
