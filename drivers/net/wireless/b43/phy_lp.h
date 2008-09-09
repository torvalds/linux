#ifndef LINUX_B43_PHY_LP_H_
#define LINUX_B43_PHY_LP_H_

/* Definitions for the LP-PHY */




#define B43_LP_RADIO(radio_reg)			(radio_reg)
#define B43_LP_NORTH(radio_reg)			B43_LP_RADIO(radio_reg)
#define B43_LP_SOUTH(radio_reg)			B43_LP_RADIO((radio_reg) | 0x4000)


/*** Broadcom 2062 NORTH radio registers ***/
#define B2062_N_COMM1				B43_LP_NORTH(0x000) /* Common 01 (north) */
#define B2062_N_COMM2				B43_LP_NORTH(0x002) /* Common 02 (north) */
#define B2062_N_COMM3				B43_LP_NORTH(0x003) /* Common 03 (north) */
#define B2062_N_COMM4				B43_LP_NORTH(0x004) /* Common 04 (north) */
#define B2062_N_COMM5				B43_LP_NORTH(0x005) /* Common 05 (north) */
#define B2062_N_COMM6				B43_LP_NORTH(0x006) /* Common 06 (north) */
#define B2062_N_COMM7				B43_LP_NORTH(0x007) /* Common 07 (north) */
#define B2062_N_COMM8				B43_LP_NORTH(0x008) /* Common 08 (north) */
#define B2062_N_COMM9				B43_LP_NORTH(0x009) /* Common 09 (north) */
#define B2062_N_COMM10				B43_LP_NORTH(0x00A) /* Common 10 (north) */
#define B2062_N_COMM11				B43_LP_NORTH(0x00B) /* Common 11 (north) */
#define B2062_N_COMM12				B43_LP_NORTH(0x00C) /* Common 12 (north) */
#define B2062_N_COMM13				B43_LP_NORTH(0x00D) /* Common 13 (north) */
#define B2062_N_COMM14				B43_LP_NORTH(0x00E) /* Common 14 (north) */
#define B2062_N_COMM15				B43_LP_NORTH(0x00F) /* Common 15 (north) */
#define B2062_N_PDN_CTL0			B43_LP_NORTH(0x010) /* PDN Control 0 (north) */
#define B2062_N_PDN_CTL1			B43_LP_NORTH(0x011) /* PDN Control 1 (north) */
#define B2062_N_PDN_CTL2			B43_LP_NORTH(0x012) /* PDN Control 2 (north) */
#define B2062_N_PDN_CTL3			B43_LP_NORTH(0x013) /* PDN Control 3 (north) */
#define B2062_N_PDN_CTL4			B43_LP_NORTH(0x014) /* PDN Control 4 (north) */
#define B2062_N_GEN_CTL0			B43_LP_NORTH(0x015) /* GEN Control 0 (north) */
#define B2062_N_IQ_CALIB			B43_LP_NORTH(0x016) /* IQ Calibration (north) */
#define B2062_N_LGENC				B43_LP_NORTH(0x017) /* LGENC (north) */
#define B2062_N_LGENA_LPF			B43_LP_NORTH(0x018) /* LGENA LPF (north) */
#define B2062_N_LGENA_BIAS0			B43_LP_NORTH(0x019) /* LGENA Bias 0 (north) */
#define B2062_N_LGNEA_BIAS1			B43_LP_NORTH(0x01A) /* LGNEA Bias 1 (north) */
#define B2062_N_LGENA_CTL0			B43_LP_NORTH(0x01B) /* LGENA Control 0 (north) */
#define B2062_N_LGENA_CTL1			B43_LP_NORTH(0x01C) /* LGENA Control 1 (north) */
#define B2062_N_LGENA_CTL2			B43_LP_NORTH(0x01D) /* LGENA Control 2 (north) */
#define B2062_N_LGENA_TUNE0			B43_LP_NORTH(0x01E) /* LGENA Tune 0 (north) */
#define B2062_N_LGENA_TUNE1			B43_LP_NORTH(0x01F) /* LGENA Tune 1 (north) */
#define B2062_N_LGENA_TUNE2			B43_LP_NORTH(0x020) /* LGENA Tune 2 (north) */
#define B2062_N_LGENA_TUNE3			B43_LP_NORTH(0x021) /* LGENA Tune 3 (north) */
#define B2062_N_LGENA_CTL3			B43_LP_NORTH(0x022) /* LGENA Control 3 (north) */
#define B2062_N_LGENA_CTL4			B43_LP_NORTH(0x023) /* LGENA Control 4 (north) */
#define B2062_N_LGENA_CTL5			B43_LP_NORTH(0x024) /* LGENA Control 5 (north) */
#define B2062_N_LGENA_CTL6			B43_LP_NORTH(0x025) /* LGENA Control 6 (north) */
#define B2062_N_LGENA_CTL7			B43_LP_NORTH(0x026) /* LGENA Control 7 (north) */
#define B2062_N_RXA_CTL0			B43_LP_NORTH(0x027) /* RXA Control 0 (north) */
#define B2062_N_RXA_CTL1			B43_LP_NORTH(0x028) /* RXA Control 1 (north) */
#define B2062_N_RXA_CTL2			B43_LP_NORTH(0x029) /* RXA Control 2 (north) */
#define B2062_N_RXA_CTL3			B43_LP_NORTH(0x02A) /* RXA Control 3 (north) */
#define B2062_N_RXA_CTL4			B43_LP_NORTH(0x02B) /* RXA Control 4 (north) */
#define B2062_N_RXA_CTL5			B43_LP_NORTH(0x02C) /* RXA Control 5 (north) */
#define B2062_N_RXA_CTL6			B43_LP_NORTH(0x02D) /* RXA Control 6 (north) */
#define B2062_N_RXA_CTL7			B43_LP_NORTH(0x02E) /* RXA Control 7 (north) */
#define B2062_N_RXBB_CTL0			B43_LP_NORTH(0x02F) /* RXBB Control 0 (north) */
#define B2062_N_RXBB_CTL1			B43_LP_NORTH(0x030) /* RXBB Control 1 (north) */
#define B2062_N_RXBB_CTL2			B43_LP_NORTH(0x031) /* RXBB Control 2 (north) */
#define B2062_N_RXBB_GAIN0			B43_LP_NORTH(0x032) /* RXBB Gain 0 (north) */
#define B2062_N_RXBB_GAIN1			B43_LP_NORTH(0x033) /* RXBB Gain 1 (north) */
#define B2062_N_RXBB_GAIN2			B43_LP_NORTH(0x034) /* RXBB Gain 2 (north) */
#define B2062_N_RXBB_GAIN3			B43_LP_NORTH(0x035) /* RXBB Gain 3 (north) */
#define B2062_N_RXBB_RSSI0			B43_LP_NORTH(0x036) /* RXBB RSSI 0 (north) */
#define B2062_N_RXBB_RSSI1			B43_LP_NORTH(0x037) /* RXBB RSSI 1 (north) */
#define B2062_N_RXBB_CALIB0			B43_LP_NORTH(0x038) /* RXBB Calibration0 (north) */
#define B2062_N_RXBB_CALIB1			B43_LP_NORTH(0x039) /* RXBB Calibration1 (north) */
#define B2062_N_RXBB_CALIB2			B43_LP_NORTH(0x03A) /* RXBB Calibration2 (north) */
#define B2062_N_RXBB_BIAS0			B43_LP_NORTH(0x03B) /* RXBB Bias 0 (north) */
#define B2062_N_RXBB_BIAS1			B43_LP_NORTH(0x03C) /* RXBB Bias 1 (north) */
#define B2062_N_RXBB_BIAS2			B43_LP_NORTH(0x03D) /* RXBB Bias 2 (north) */
#define B2062_N_RXBB_BIAS3			B43_LP_NORTH(0x03E) /* RXBB Bias 3 (north) */
#define B2062_N_RXBB_BIAS4			B43_LP_NORTH(0x03F) /* RXBB Bias 4 (north) */
#define B2062_N_RXBB_BIAS5			B43_LP_NORTH(0x040) /* RXBB Bias 5 (north) */
#define B2062_N_RXBB_RSSI2			B43_LP_NORTH(0x041) /* RXBB RSSI 2 (north) */
#define B2062_N_RXBB_RSSI3			B43_LP_NORTH(0x042) /* RXBB RSSI 3 (north) */
#define B2062_N_RXBB_RSSI4			B43_LP_NORTH(0x043) /* RXBB RSSI 4 (north) */
#define B2062_N_RXBB_RSSI5			B43_LP_NORTH(0x044) /* RXBB RSSI 5 (north) */
#define B2062_N_TX_CTL0				B43_LP_NORTH(0x045) /* TX Control 0 (north) */
#define B2062_N_TX_CTL1				B43_LP_NORTH(0x046) /* TX Control 1 (north) */
#define B2062_N_TX_CTL2				B43_LP_NORTH(0x047) /* TX Control 2 (north) */
#define B2062_N_TX_CTL3				B43_LP_NORTH(0x048) /* TX Control 3 (north) */
#define B2062_N_TX_CTL4				B43_LP_NORTH(0x049) /* TX Control 4 (north) */
#define B2062_N_TX_CTL5				B43_LP_NORTH(0x04A) /* TX Control 5 (north) */
#define B2062_N_TX_CTL6				B43_LP_NORTH(0x04B) /* TX Control 6 (north) */
#define B2062_N_TX_CTL7				B43_LP_NORTH(0x04C) /* TX Control 7 (north) */
#define B2062_N_TX_CTL8				B43_LP_NORTH(0x04D) /* TX Control 8 (north) */
#define B2062_N_TX_CTL9				B43_LP_NORTH(0x04E) /* TX Control 9 (north) */
#define B2062_N_TX_CTL_A			B43_LP_NORTH(0x04F) /* TX Control A (north) */
#define B2062_N_TX_GC2G				B43_LP_NORTH(0x050) /* TX GC2G (north) */
#define B2062_N_TX_GC5G				B43_LP_NORTH(0x051) /* TX GC5G (north) */
#define B2062_N_TX_TUNE				B43_LP_NORTH(0x052) /* TX Tune (north) */
#define B2062_N_TX_PAD				B43_LP_NORTH(0x053) /* TX PAD (north) */
#define B2062_N_TX_PGA				B43_LP_NORTH(0x054) /* TX PGA (north) */
#define B2062_N_TX_PADAUX			B43_LP_NORTH(0x055) /* TX PADAUX (north) */
#define B2062_N_TX_PGAAUX			B43_LP_NORTH(0x056) /* TX PGAAUX (north) */
#define B2062_N_TSSI_CTL0			B43_LP_NORTH(0x057) /* TSSI Control 0 (north) */
#define B2062_N_TSSI_CTL1			B43_LP_NORTH(0x058) /* TSSI Control 1 (north) */
#define B2062_N_TSSI_CTL2			B43_LP_NORTH(0x059) /* TSSI Control 2 (north) */
#define B2062_N_IQ_CALIB_CTL0			B43_LP_NORTH(0x05A) /* IQ Calibration Control 0 (north) */
#define B2062_N_IQ_CALIB_CTL1			B43_LP_NORTH(0x05B) /* IQ Calibration Control 1 (north) */
#define B2062_N_IQ_CALIB_CTL2			B43_LP_NORTH(0x05C) /* IQ Calibration Control 2 (north) */
#define B2062_N_CALIB_TS			B43_LP_NORTH(0x05D) /* Calibration TS (north) */
#define B2062_N_CALIB_CTL0			B43_LP_NORTH(0x05E) /* Calibration Control 0 (north) */
#define B2062_N_CALIB_CTL1			B43_LP_NORTH(0x05F) /* Calibration Control 1 (north) */
#define B2062_N_CALIB_CTL2			B43_LP_NORTH(0x060) /* Calibration Control 2 (north) */
#define B2062_N_CALIB_CTL3			B43_LP_NORTH(0x061) /* Calibration Control 3 (north) */
#define B2062_N_CALIB_CTL4			B43_LP_NORTH(0x062) /* Calibration Control 4 (north) */
#define B2062_N_CALIB_DBG0			B43_LP_NORTH(0x063) /* Calibration Debug 0 (north) */
#define B2062_N_CALIB_DBG1			B43_LP_NORTH(0x064) /* Calibration Debug 1 (north) */
#define B2062_N_CALIB_DBG2			B43_LP_NORTH(0x065) /* Calibration Debug 2 (north) */
#define B2062_N_CALIB_DBG3			B43_LP_NORTH(0x066) /* Calibration Debug 3 (north) */
#define B2062_N_PSENSE_CTL0			B43_LP_NORTH(0x069) /* PSENSE Control 0 (north) */
#define B2062_N_PSENSE_CTL1			B43_LP_NORTH(0x06A) /* PSENSE Control 1 (north) */
#define B2062_N_PSENSE_CTL2			B43_LP_NORTH(0x06B) /* PSENSE Control 2 (north) */
#define B2062_N_TEST_BUF0			B43_LP_NORTH(0x06C) /* TEST BUF0 (north) */

/*** Broadcom 2062 SOUTH radio registers ***/
#define B2062_S_COMM1				B43_LP_SOUTH(0x000) /* Common 01 (south) */
#define B2062_S_RADIO_ID_CODE			B43_LP_SOUTH(0x001) /* Radio ID code (south) */
#define B2062_S_COMM2				B43_LP_SOUTH(0x002) /* Common 02 (south) */
#define B2062_S_COMM3				B43_LP_SOUTH(0x003) /* Common 03 (south) */
#define B2062_S_COMM4				B43_LP_SOUTH(0x004) /* Common 04 (south) */
#define B2062_S_COMM5				B43_LP_SOUTH(0x005) /* Common 05 (south) */
#define B2062_S_COMM6				B43_LP_SOUTH(0x006) /* Common 06 (south) */
#define B2062_S_COMM7				B43_LP_SOUTH(0x007) /* Common 07 (south) */
#define B2062_S_COMM8				B43_LP_SOUTH(0x008) /* Common 08 (south) */
#define B2062_S_COMM9				B43_LP_SOUTH(0x009) /* Common 09 (south) */
#define B2062_S_COMM10				B43_LP_SOUTH(0x00A) /* Common 10 (south) */
#define B2062_S_COMM11				B43_LP_SOUTH(0x00B) /* Common 11 (south) */
#define B2062_S_COMM12				B43_LP_SOUTH(0x00C) /* Common 12 (south) */
#define B2062_S_COMM13				B43_LP_SOUTH(0x00D) /* Common 13 (south) */
#define B2062_S_COMM14				B43_LP_SOUTH(0x00E) /* Common 14 (south) */
#define B2062_S_COMM15				B43_LP_SOUTH(0x00F) /* Common 15 (south) */
#define B2062_S_PDS_CTL0			B43_LP_SOUTH(0x010) /* PDS Control 0 (south) */
#define B2062_S_PDS_CTL1			B43_LP_SOUTH(0x011) /* PDS Control 1 (south) */
#define B2062_S_PDS_CTL2			B43_LP_SOUTH(0x012) /* PDS Control 2 (south) */
#define B2062_S_PDS_CTL3			B43_LP_SOUTH(0x013) /* PDS Control 3 (south) */
#define B2062_S_BG_CTL0				B43_LP_SOUTH(0x014) /* BG Control 0 (south) */
#define B2062_S_BG_CTL1				B43_LP_SOUTH(0x015) /* BG Control 1 (south) */
#define B2062_S_BG_CTL2				B43_LP_SOUTH(0x016) /* BG Control 2 (south) */
#define B2062_S_LGENG_CTL0			B43_LP_SOUTH(0x017) /* LGENG Control 00 (south) */
#define B2062_S_LGENG_CTL1			B43_LP_SOUTH(0x018) /* LGENG Control 01 (south) */
#define B2062_S_LGENG_CTL2			B43_LP_SOUTH(0x019) /* LGENG Control 02 (south) */
#define B2062_S_LGENG_CTL3			B43_LP_SOUTH(0x01A) /* LGENG Control 03 (south) */
#define B2062_S_LGENG_CTL4			B43_LP_SOUTH(0x01B) /* LGENG Control 04 (south) */
#define B2062_S_LGENG_CTL5			B43_LP_SOUTH(0x01C) /* LGENG Control 05 (south) */
#define B2062_S_LGENG_CTL6			B43_LP_SOUTH(0x01D) /* LGENG Control 06 (south) */
#define B2062_S_LGENG_CTL7			B43_LP_SOUTH(0x01E) /* LGENG Control 07 (south) */
#define B2062_S_LGENG_CTL8			B43_LP_SOUTH(0x01F) /* LGENG Control 08 (south) */
#define B2062_S_LGENG_CTL9			B43_LP_SOUTH(0x020) /* LGENG Control 09 (south) */
#define B2062_S_LGENG_CTL10			B43_LP_SOUTH(0x021) /* LGENG Control 10 (south) */
#define B2062_S_LGENG_CTL11			B43_LP_SOUTH(0x022) /* LGENG Control 11 (south) */
#define B2062_S_REFPLL_CTL0			B43_LP_SOUTH(0x023) /* REFPLL Control 00 (south) */
#define B2062_S_REFPLL_CTL1			B43_LP_SOUTH(0x024) /* REFPLL Control 01 (south) */
#define B2062_S_REFPLL_CTL2			B43_LP_SOUTH(0x025) /* REFPLL Control 02 (south) */
#define B2062_S_REFPLL_CTL3			B43_LP_SOUTH(0x026) /* REFPLL Control 03 (south) */
#define B2062_S_REFPLL_CTL4			B43_LP_SOUTH(0x027) /* REFPLL Control 04 (south) */
#define B2062_S_REFPLL_CTL5			B43_LP_SOUTH(0x028) /* REFPLL Control 05 (south) */
#define B2062_S_REFPLL_CTL6			B43_LP_SOUTH(0x029) /* REFPLL Control 06 (south) */
#define B2062_S_REFPLL_CTL7			B43_LP_SOUTH(0x02A) /* REFPLL Control 07 (south) */
#define B2062_S_REFPLL_CTL8			B43_LP_SOUTH(0x02B) /* REFPLL Control 08 (south) */
#define B2062_S_REFPLL_CTL9			B43_LP_SOUTH(0x02C) /* REFPLL Control 09 (south) */
#define B2062_S_REFPLL_CTL10			B43_LP_SOUTH(0x02D) /* REFPLL Control 10 (south) */
#define B2062_S_REFPLL_CTL11			B43_LP_SOUTH(0x02E) /* REFPLL Control 11 (south) */
#define B2062_S_REFPLL_CTL12			B43_LP_SOUTH(0x02F) /* REFPLL Control 12 (south) */
#define B2062_S_REFPLL_CTL13			B43_LP_SOUTH(0x030) /* REFPLL Control 13 (south) */
#define B2062_S_REFPLL_CTL14			B43_LP_SOUTH(0x031) /* REFPLL Control 14 (south) */
#define B2062_S_REFPLL_CTL15			B43_LP_SOUTH(0x032) /* REFPLL Control 15 (south) */
#define B2062_S_REFPLL_CTL16			B43_LP_SOUTH(0x033) /* REFPLL Control 16 (south) */
#define B2062_S_RFPLL_CTL0			B43_LP_SOUTH(0x034) /* RFPLL Control 00 (south) */
#define B2062_S_RFPLL_CTL1			B43_LP_SOUTH(0x035) /* RFPLL Control 01 (south) */
#define B2062_S_RFPLL_CTL2			B43_LP_SOUTH(0x036) /* RFPLL Control 02 (south) */
#define B2062_S_RFPLL_CTL3			B43_LP_SOUTH(0x037) /* RFPLL Control 03 (south) */
#define B2062_S_RFPLL_CTL4			B43_LP_SOUTH(0x038) /* RFPLL Control 04 (south) */
#define B2062_S_RFPLL_CTL5			B43_LP_SOUTH(0x039) /* RFPLL Control 05 (south) */
#define B2062_S_RFPLL_CTL6			B43_LP_SOUTH(0x03A) /* RFPLL Control 06 (south) */
#define B2062_S_RFPLL_CTL7			B43_LP_SOUTH(0x03B) /* RFPLL Control 07 (south) */
#define B2062_S_RFPLL_CTL8			B43_LP_SOUTH(0x03C) /* RFPLL Control 08 (south) */
#define B2062_S_RFPLL_CTL9			B43_LP_SOUTH(0x03D) /* RFPLL Control 09 (south) */
#define B2062_S_RFPLL_CTL10			B43_LP_SOUTH(0x03E) /* RFPLL Control 10 (south) */
#define B2062_S_RFPLL_CTL11			B43_LP_SOUTH(0x03F) /* RFPLL Control 11 (south) */
#define B2062_S_RFPLL_CTL12			B43_LP_SOUTH(0x040) /* RFPLL Control 12 (south) */
#define B2062_S_RFPLL_CTL13			B43_LP_SOUTH(0x041) /* RFPLL Control 13 (south) */
#define B2062_S_RFPLL_CTL14			B43_LP_SOUTH(0x042) /* RFPLL Control 14 (south) */
#define B2062_S_RFPLL_CTL15			B43_LP_SOUTH(0x043) /* RFPLL Control 15 (south) */
#define B2062_S_RFPLL_CTL16			B43_LP_SOUTH(0x044) /* RFPLL Control 16 (south) */
#define B2062_S_RFPLL_CTL17			B43_LP_SOUTH(0x045) /* RFPLL Control 17 (south) */
#define B2062_S_RFPLL_CTL18			B43_LP_SOUTH(0x046) /* RFPLL Control 18 (south) */
#define B2062_S_RFPLL_CTL19			B43_LP_SOUTH(0x047) /* RFPLL Control 19 (south) */
#define B2062_S_RFPLL_CTL20			B43_LP_SOUTH(0x048) /* RFPLL Control 20 (south) */
#define B2062_S_RFPLL_CTL21			B43_LP_SOUTH(0x049) /* RFPLL Control 21 (south) */
#define B2062_S_RFPLL_CTL22			B43_LP_SOUTH(0x04A) /* RFPLL Control 22 (south) */
#define B2062_S_RFPLL_CTL23			B43_LP_SOUTH(0x04B) /* RFPLL Control 23 (south) */
#define B2062_S_RFPLL_CTL24			B43_LP_SOUTH(0x04C) /* RFPLL Control 24 (south) */
#define B2062_S_RFPLL_CTL25			B43_LP_SOUTH(0x04D) /* RFPLL Control 25 (south) */
#define B2062_S_RFPLL_CTL26			B43_LP_SOUTH(0x04E) /* RFPLL Control 26 (south) */
#define B2062_S_RFPLL_CTL27			B43_LP_SOUTH(0x04F) /* RFPLL Control 27 (south) */
#define B2062_S_RFPLL_CTL28			B43_LP_SOUTH(0x050) /* RFPLL Control 28 (south) */
#define B2062_S_RFPLL_CTL29			B43_LP_SOUTH(0x051) /* RFPLL Control 29 (south) */
#define B2062_S_RFPLL_CTL30			B43_LP_SOUTH(0x052) /* RFPLL Control 30 (south) */
#define B2062_S_RFPLL_CTL31			B43_LP_SOUTH(0x053) /* RFPLL Control 31 (south) */
#define B2062_S_RFPLL_CTL32			B43_LP_SOUTH(0x054) /* RFPLL Control 32 (south) */
#define B2062_S_RFPLL_CTL33			B43_LP_SOUTH(0x055) /* RFPLL Control 33 (south) */
#define B2062_S_RFPLL_CTL34			B43_LP_SOUTH(0x056) /* RFPLL Control 34 (south) */
#define B2062_S_RXG_CNT0			B43_LP_SOUTH(0x057) /* RXG Counter 00 (south) */
#define B2062_S_RXG_CNT1			B43_LP_SOUTH(0x058) /* RXG Counter 01 (south) */
#define B2062_S_RXG_CNT2			B43_LP_SOUTH(0x059) /* RXG Counter 02 (south) */
#define B2062_S_RXG_CNT3			B43_LP_SOUTH(0x05A) /* RXG Counter 03 (south) */
#define B2062_S_RXG_CNT4			B43_LP_SOUTH(0x05B) /* RXG Counter 04 (south) */
#define B2062_S_RXG_CNT5			B43_LP_SOUTH(0x05C) /* RXG Counter 05 (south) */
#define B2062_S_RXG_CNT6			B43_LP_SOUTH(0x05D) /* RXG Counter 06 (south) */
#define B2062_S_RXG_CNT7			B43_LP_SOUTH(0x05E) /* RXG Counter 07 (south) */
#define B2062_S_RXG_CNT8			B43_LP_SOUTH(0x05F) /* RXG Counter 08 (south) */
#define B2062_S_RXG_CNT9			B43_LP_SOUTH(0x060) /* RXG Counter 09 (south) */
#define B2062_S_RXG_CNT10			B43_LP_SOUTH(0x061) /* RXG Counter 10 (south) */
#define B2062_S_RXG_CNT11			B43_LP_SOUTH(0x062) /* RXG Counter 11 (south) */
#define B2062_S_RXG_CNT12			B43_LP_SOUTH(0x063) /* RXG Counter 12 (south) */
#define B2062_S_RXG_CNT13			B43_LP_SOUTH(0x064) /* RXG Counter 13 (south) */
#define B2062_S_RXG_CNT14			B43_LP_SOUTH(0x065) /* RXG Counter 14 (south) */
#define B2062_S_RXG_CNT15			B43_LP_SOUTH(0x066) /* RXG Counter 15 (south) */
#define B2062_S_RXG_CNT16			B43_LP_SOUTH(0x067) /* RXG Counter 16 (south) */
#define B2062_S_RXG_CNT17			B43_LP_SOUTH(0x068) /* RXG Counter 17 (south) */



/*** Broadcom 2063 radio registers ***/
#define B2063_RADIO_ID_CODE			B43_LP_RADIO(0x001) /* Radio ID code */
#define B2063_COMM1				B43_LP_RADIO(0x000) /* Common 01 */
#define B2063_COMM2				B43_LP_RADIO(0x002) /* Common 02 */
#define B2063_COMM3				B43_LP_RADIO(0x003) /* Common 03 */
#define B2063_COMM4				B43_LP_RADIO(0x004) /* Common 04 */
#define B2063_COMM5				B43_LP_RADIO(0x005) /* Common 05 */
#define B2063_COMM6				B43_LP_RADIO(0x006) /* Common 06 */
#define B2063_COMM7				B43_LP_RADIO(0x007) /* Common 07 */
#define B2063_COMM8				B43_LP_RADIO(0x008) /* Common 08 */
#define B2063_COMM9				B43_LP_RADIO(0x009) /* Common 09 */
#define B2063_COMM10				B43_LP_RADIO(0x00A) /* Common 10 */
#define B2063_COMM11				B43_LP_RADIO(0x00B) /* Common 11 */
#define B2063_COMM12				B43_LP_RADIO(0x00C) /* Common 12 */
#define B2063_COMM13				B43_LP_RADIO(0x00D) /* Common 13 */
#define B2063_COMM14				B43_LP_RADIO(0x00E) /* Common 14 */
#define B2063_COMM15				B43_LP_RADIO(0x00F) /* Common 15 */
#define B2063_COMM16				B43_LP_RADIO(0x010) /* Common 16 */
#define B2063_COMM17				B43_LP_RADIO(0x011) /* Common 17 */
#define B2063_COMM18				B43_LP_RADIO(0x012) /* Common 18 */
#define B2063_COMM19				B43_LP_RADIO(0x013) /* Common 19 */
#define B2063_COMM20				B43_LP_RADIO(0x014) /* Common 20 */
#define B2063_COMM21				B43_LP_RADIO(0x015) /* Common 21 */
#define B2063_COMM22				B43_LP_RADIO(0x016) /* Common 22 */
#define B2063_COMM23				B43_LP_RADIO(0x017) /* Common 23 */
#define B2063_COMM24				B43_LP_RADIO(0x018) /* Common 24 */
#define B2063_PWR_SWITCH_CTL			B43_LP_RADIO(0x019) /* POWER SWITCH Control */
#define B2063_PLL_SP1				B43_LP_RADIO(0x01A) /* PLL SP 1 */
#define B2063_PLL_SP2				B43_LP_RADIO(0x01B) /* PLL SP 2 */
#define B2063_LOGEN_SP1				B43_LP_RADIO(0x01C) /* LOGEN SP 1 */
#define B2063_LOGEN_SP2				B43_LP_RADIO(0x01D) /* LOGEN SP 2 */
#define B2063_LOGEN_SP3				B43_LP_RADIO(0x01E) /* LOGEN SP 3 */
#define B2063_LOGEN_SP4				B43_LP_RADIO(0x01F) /* LOGEN SP 4 */
#define B2063_LOGEN_SP5				B43_LP_RADIO(0x020) /* LOGEN SP 5 */
#define B2063_G_RX_SP1				B43_LP_RADIO(0x021) /* G RX SP 1 */
#define B2063_G_RX_SP2				B43_LP_RADIO(0x022) /* G RX SP 2 */
#define B2063_G_RX_SP3				B43_LP_RADIO(0x023) /* G RX SP 3 */
#define B2063_G_RX_SP4				B43_LP_RADIO(0x024) /* G RX SP 4 */
#define B2063_G_RX_SP5				B43_LP_RADIO(0x025) /* G RX SP 5 */
#define B2063_G_RX_SP6				B43_LP_RADIO(0x026) /* G RX SP 6 */
#define B2063_G_RX_SP7				B43_LP_RADIO(0x027) /* G RX SP 7 */
#define B2063_G_RX_SP8				B43_LP_RADIO(0x028) /* G RX SP 8 */
#define B2063_G_RX_SP9				B43_LP_RADIO(0x029) /* G RX SP 9 */
#define B2063_G_RX_SP10				B43_LP_RADIO(0x02A) /* G RX SP 10 */
#define B2063_G_RX_SP11				B43_LP_RADIO(0x02B) /* G RX SP 11 */
#define B2063_A_RX_SP1				B43_LP_RADIO(0x02C) /* A RX SP 1 */
#define B2063_A_RX_SP2				B43_LP_RADIO(0x02D) /* A RX SP 2 */
#define B2063_A_RX_SP3				B43_LP_RADIO(0x02E) /* A RX SP 3 */
#define B2063_A_RX_SP4				B43_LP_RADIO(0x02F) /* A RX SP 4 */
#define B2063_A_RX_SP5				B43_LP_RADIO(0x030) /* A RX SP 5 */
#define B2063_A_RX_SP6				B43_LP_RADIO(0x031) /* A RX SP 6 */
#define B2063_A_RX_SP7				B43_LP_RADIO(0x032) /* A RX SP 7 */
#define B2063_RX_BB_SP1				B43_LP_RADIO(0x033) /* RX BB SP 1 */
#define B2063_RX_BB_SP2				B43_LP_RADIO(0x034) /* RX BB SP 2 */
#define B2063_RX_BB_SP3				B43_LP_RADIO(0x035) /* RX BB SP 3 */
#define B2063_RX_BB_SP4				B43_LP_RADIO(0x036) /* RX BB SP 4 */
#define B2063_RX_BB_SP5				B43_LP_RADIO(0x037) /* RX BB SP 5 */
#define B2063_RX_BB_SP6				B43_LP_RADIO(0x038) /* RX BB SP 6 */
#define B2063_RX_BB_SP7				B43_LP_RADIO(0x039) /* RX BB SP 7 */
#define B2063_RX_BB_SP8				B43_LP_RADIO(0x03A) /* RX BB SP 8 */
#define B2063_TX_RF_SP1				B43_LP_RADIO(0x03B) /* TX RF SP 1 */
#define B2063_TX_RF_SP2				B43_LP_RADIO(0x03C) /* TX RF SP 2 */
#define B2063_TX_RF_SP3				B43_LP_RADIO(0x03D) /* TX RF SP 3 */
#define B2063_TX_RF_SP4				B43_LP_RADIO(0x03E) /* TX RF SP 4 */
#define B2063_TX_RF_SP5				B43_LP_RADIO(0x03F) /* TX RF SP 5 */
#define B2063_TX_RF_SP6				B43_LP_RADIO(0x040) /* TX RF SP 6 */
#define B2063_TX_RF_SP7				B43_LP_RADIO(0x041) /* TX RF SP 7 */
#define B2063_TX_RF_SP8				B43_LP_RADIO(0x042) /* TX RF SP 8 */
#define B2063_TX_RF_SP9				B43_LP_RADIO(0x043) /* TX RF SP 9 */
#define B2063_TX_RF_SP10			B43_LP_RADIO(0x044) /* TX RF SP 10 */
#define B2063_TX_RF_SP11			B43_LP_RADIO(0x045) /* TX RF SP 11 */
#define B2063_TX_RF_SP12			B43_LP_RADIO(0x046) /* TX RF SP 12 */
#define B2063_TX_RF_SP13			B43_LP_RADIO(0x047) /* TX RF SP 13 */
#define B2063_TX_RF_SP14			B43_LP_RADIO(0x048) /* TX RF SP 14 */
#define B2063_TX_RF_SP15			B43_LP_RADIO(0x049) /* TX RF SP 15 */
#define B2063_TX_RF_SP16			B43_LP_RADIO(0x04A) /* TX RF SP 16 */
#define B2063_TX_RF_SP17			B43_LP_RADIO(0x04B) /* TX RF SP 17 */
#define B2063_PA_SP1				B43_LP_RADIO(0x04C) /* PA SP 1 */
#define B2063_PA_SP2				B43_LP_RADIO(0x04D) /* PA SP 2 */
#define B2063_PA_SP3				B43_LP_RADIO(0x04E) /* PA SP 3 */
#define B2063_PA_SP4				B43_LP_RADIO(0x04F) /* PA SP 4 */
#define B2063_PA_SP5				B43_LP_RADIO(0x050) /* PA SP 5 */
#define B2063_PA_SP6				B43_LP_RADIO(0x051) /* PA SP 6 */
#define B2063_PA_SP7				B43_LP_RADIO(0x052) /* PA SP 7 */
#define B2063_TX_BB_SP1				B43_LP_RADIO(0x053) /* TX BB SP 1 */
#define B2063_TX_BB_SP2				B43_LP_RADIO(0x054) /* TX BB SP 2 */
#define B2063_TX_BB_SP3				B43_LP_RADIO(0x055) /* TX BB SP 3 */
#define B2063_REG_SP1				B43_LP_RADIO(0x056) /* REG SP 1 */
#define B2063_BANDGAP_CTL1			B43_LP_RADIO(0x057) /* BANDGAP Control 1 */
#define B2063_BANDGAP_CTL2			B43_LP_RADIO(0x058) /* BANDGAP Control 2 */
#define B2063_LPO_CTL1				B43_LP_RADIO(0x059) /* LPO Control 1 */
#define B2063_RC_CALIB_CTL1			B43_LP_RADIO(0x05A) /* RC Calibration Control 1 */
#define B2063_RC_CALIB_CTL2			B43_LP_RADIO(0x05B) /* RC Calibration Control 2 */
#define B2063_RC_CALIB_CTL3			B43_LP_RADIO(0x05C) /* RC Calibration Control 3 */
#define B2063_RC_CALIB_CTL4			B43_LP_RADIO(0x05D) /* RC Calibration Control 4 */
#define B2063_RC_CALIB_CTL5			B43_LP_RADIO(0x05E) /* RC Calibration Control 5 */
#define B2063_RC_CALIB_CTL6			B43_LP_RADIO(0x05F) /* RC Calibration Control 6 */
#define B2063_RC_CALIB_CTL7			B43_LP_RADIO(0x060) /* RC Calibration Control 7 */
#define B2063_RC_CALIB_CTL8			B43_LP_RADIO(0x061) /* RC Calibration Control 8 */
#define B2063_RC_CALIB_CTL9			B43_LP_RADIO(0x062) /* RC Calibration Control 9 */
#define B2063_RC_CALIB_CTL10			B43_LP_RADIO(0x063) /* RC Calibration Control 10 */
#define B2063_PLL_JTAG_CALNRST			B43_LP_RADIO(0x064) /* PLL JTAG CALNRST */
#define B2063_PLL_JTAG_IN_PLL1			B43_LP_RADIO(0x065) /* PLL JTAG IN PLL 1 */
#define B2063_PLL_JTAG_IN_PLL2			B43_LP_RADIO(0x066) /* PLL JTAG IN PLL 2 */
#define B2063_PLL_JTAG_PLL_CP1			B43_LP_RADIO(0x067) /* PLL JTAG PLL CP 1 */
#define B2063_PLL_JTAG_PLL_CP2			B43_LP_RADIO(0x068) /* PLL JTAG PLL CP 2 */
#define B2063_PLL_JTAG_PLL_CP3			B43_LP_RADIO(0x069) /* PLL JTAG PLL CP 3 */
#define B2063_PLL_JTAG_PLL_CP4			B43_LP_RADIO(0x06A) /* PLL JTAG PLL CP 4 */
#define B2063_PLL_JTAG_PLL_CTL1			B43_LP_RADIO(0x06B) /* PLL JTAG PLL Control 1 */
#define B2063_PLL_JTAG_PLL_LF1			B43_LP_RADIO(0x06C) /* PLL JTAG PLL LF 1 */
#define B2063_PLL_JTAG_PLL_LF2			B43_LP_RADIO(0x06D) /* PLL JTAG PLL LF 2 */
#define B2063_PLL_JTAG_PLL_LF3			B43_LP_RADIO(0x06E) /* PLL JTAG PLL LF 3 */
#define B2063_PLL_JTAG_PLL_LF4			B43_LP_RADIO(0x06F) /* PLL JTAG PLL LF 4 */
#define B2063_PLL_JTAG_PLL_SG1			B43_LP_RADIO(0x070) /* PLL JTAG PLL SG 1 */
#define B2063_PLL_JTAG_PLL_SG2			B43_LP_RADIO(0x071) /* PLL JTAG PLL SG 2 */
#define B2063_PLL_JTAG_PLL_SG3			B43_LP_RADIO(0x072) /* PLL JTAG PLL SG 3 */
#define B2063_PLL_JTAG_PLL_SG4			B43_LP_RADIO(0x073) /* PLL JTAG PLL SG 4 */
#define B2063_PLL_JTAG_PLL_SG5			B43_LP_RADIO(0x074) /* PLL JTAG PLL SG 5 */
#define B2063_PLL_JTAG_PLL_VCO1			B43_LP_RADIO(0x075) /* PLL JTAG PLL VCO 1 */
#define B2063_PLL_JTAG_PLL_VCO2			B43_LP_RADIO(0x076) /* PLL JTAG PLL VCO 2 */
#define B2063_PLL_JTAG_PLL_VCO_CALIB1		B43_LP_RADIO(0x077) /* PLL JTAG PLL VCO Calibration 1 */
#define B2063_PLL_JTAG_PLL_VCO_CALIB2		B43_LP_RADIO(0x078) /* PLL JTAG PLL VCO Calibration 2 */
#define B2063_PLL_JTAG_PLL_VCO_CALIB3		B43_LP_RADIO(0x079) /* PLL JTAG PLL VCO Calibration 3 */
#define B2063_PLL_JTAG_PLL_VCO_CALIB4		B43_LP_RADIO(0x07A) /* PLL JTAG PLL VCO Calibration 4 */
#define B2063_PLL_JTAG_PLL_VCO_CALIB5		B43_LP_RADIO(0x07B) /* PLL JTAG PLL VCO Calibration 5 */
#define B2063_PLL_JTAG_PLL_VCO_CALIB6		B43_LP_RADIO(0x07C) /* PLL JTAG PLL VCO Calibration 6 */
#define B2063_PLL_JTAG_PLL_VCO_CALIB7		B43_LP_RADIO(0x07D) /* PLL JTAG PLL VCO Calibration 7 */
#define B2063_PLL_JTAG_PLL_VCO_CALIB8		B43_LP_RADIO(0x07E) /* PLL JTAG PLL VCO Calibration 8 */
#define B2063_PLL_JTAG_PLL_VCO_CALIB9		B43_LP_RADIO(0x07F) /* PLL JTAG PLL VCO Calibration 9 */
#define B2063_PLL_JTAG_PLL_VCO_CALIB10		B43_LP_RADIO(0x080) /* PLL JTAG PLL VCO Calibration 10 */
#define B2063_PLL_JTAG_PLL_XTAL_12		B43_LP_RADIO(0x081) /* PLL JTAG PLL XTAL 1 2 */
#define B2063_PLL_JTAG_PLL_XTAL3		B43_LP_RADIO(0x082) /* PLL JTAG PLL XTAL 3 */
#define B2063_LOGEN_ACL1			B43_LP_RADIO(0x083) /* LOGEN ACL 1 */
#define B2063_LOGEN_ACL2			B43_LP_RADIO(0x084) /* LOGEN ACL 2 */
#define B2063_LOGEN_ACL3			B43_LP_RADIO(0x085) /* LOGEN ACL 3 */
#define B2063_LOGEN_ACL4			B43_LP_RADIO(0x086) /* LOGEN ACL 4 */
#define B2063_LOGEN_ACL5			B43_LP_RADIO(0x087) /* LOGEN ACL 5 */
#define B2063_LO_CALIB_INPUTS			B43_LP_RADIO(0x088) /* LO Calibration INPUTS */
#define B2063_LO_CALIB_CTL1			B43_LP_RADIO(0x089) /* LO Calibration Control 1 */
#define B2063_LO_CALIB_CTL2			B43_LP_RADIO(0x08A) /* LO Calibration Control 2 */
#define B2063_LO_CALIB_CTL3			B43_LP_RADIO(0x08B) /* LO Calibration Control 3 */
#define B2063_LO_CALIB_WAITCNT			B43_LP_RADIO(0x08C) /* LO Calibration WAITCNT */
#define B2063_LO_CALIB_OVR1			B43_LP_RADIO(0x08D) /* LO Calibration OVR 1 */
#define B2063_LO_CALIB_OVR2			B43_LP_RADIO(0x08E) /* LO Calibration OVR 2 */
#define B2063_LO_CALIB_OVAL1			B43_LP_RADIO(0x08F) /* LO Calibration OVAL 1 */
#define B2063_LO_CALIB_OVAL2			B43_LP_RADIO(0x090) /* LO Calibration OVAL 2 */
#define B2063_LO_CALIB_OVAL3			B43_LP_RADIO(0x091) /* LO Calibration OVAL 3 */
#define B2063_LO_CALIB_OVAL4			B43_LP_RADIO(0x092) /* LO Calibration OVAL 4 */
#define B2063_LO_CALIB_OVAL5			B43_LP_RADIO(0x093) /* LO Calibration OVAL 5 */
#define B2063_LO_CALIB_OVAL6			B43_LP_RADIO(0x094) /* LO Calibration OVAL 6 */
#define B2063_LO_CALIB_OVAL7			B43_LP_RADIO(0x095) /* LO Calibration OVAL 7 */
#define B2063_LO_CALIB_CALVLD1			B43_LP_RADIO(0x096) /* LO Calibration CALVLD 1 */
#define B2063_LO_CALIB_CALVLD2			B43_LP_RADIO(0x097) /* LO Calibration CALVLD 2 */
#define B2063_LO_CALIB_CVAL1			B43_LP_RADIO(0x098) /* LO Calibration CVAL 1 */
#define B2063_LO_CALIB_CVAL2			B43_LP_RADIO(0x099) /* LO Calibration CVAL 2 */
#define B2063_LO_CALIB_CVAL3			B43_LP_RADIO(0x09A) /* LO Calibration CVAL 3 */
#define B2063_LO_CALIB_CVAL4			B43_LP_RADIO(0x09B) /* LO Calibration CVAL 4 */
#define B2063_LO_CALIB_CVAL5			B43_LP_RADIO(0x09C) /* LO Calibration CVAL 5 */
#define B2063_LO_CALIB_CVAL6			B43_LP_RADIO(0x09D) /* LO Calibration CVAL 6 */
#define B2063_LO_CALIB_CVAL7			B43_LP_RADIO(0x09E) /* LO Calibration CVAL 7 */
#define B2063_LOGEN_CALIB_EN			B43_LP_RADIO(0x09F) /* LOGEN Calibration EN */
#define B2063_LOGEN_PEAKDET1			B43_LP_RADIO(0x0A0) /* LOGEN PEAKDET 1 */
#define B2063_LOGEN_RCCR1			B43_LP_RADIO(0x0A1) /* LOGEN RCCR 1 */
#define B2063_LOGEN_VCOBUF1			B43_LP_RADIO(0x0A2) /* LOGEN VCOBUF 1 */
#define B2063_LOGEN_MIXER1			B43_LP_RADIO(0x0A3) /* LOGEN MIXER 1 */
#define B2063_LOGEN_MIXER2			B43_LP_RADIO(0x0A4) /* LOGEN MIXER 2 */
#define B2063_LOGEN_BUF1			B43_LP_RADIO(0x0A5) /* LOGEN BUF 1 */
#define B2063_LOGEN_BUF2			B43_LP_RADIO(0x0A6) /* LOGEN BUF 2 */
#define B2063_LOGEN_DIV1			B43_LP_RADIO(0x0A7) /* LOGEN DIV 1 */
#define B2063_LOGEN_DIV2			B43_LP_RADIO(0x0A8) /* LOGEN DIV 2 */
#define B2063_LOGEN_DIV3			B43_LP_RADIO(0x0A9) /* LOGEN DIV 3 */
#define B2063_LOGEN_CBUFRX1			B43_LP_RADIO(0x0AA) /* LOGEN CBUFRX 1 */
#define B2063_LOGEN_CBUFRX2			B43_LP_RADIO(0x0AB) /* LOGEN CBUFRX 2 */
#define B2063_LOGEN_CBUFTX1			B43_LP_RADIO(0x0AC) /* LOGEN CBUFTX 1 */
#define B2063_LOGEN_CBUFTX2			B43_LP_RADIO(0x0AD) /* LOGEN CBUFTX 2 */
#define B2063_LOGEN_IDAC1			B43_LP_RADIO(0x0AE) /* LOGEN IDAC 1 */
#define B2063_LOGEN_SPARE1			B43_LP_RADIO(0x0AF) /* LOGEN SPARE 1 */
#define B2063_LOGEN_SPARE2			B43_LP_RADIO(0x0B0) /* LOGEN SPARE 2 */
#define B2063_LOGEN_SPARE3			B43_LP_RADIO(0x0B1) /* LOGEN SPARE 3 */
#define B2063_G_RX_1ST1				B43_LP_RADIO(0x0B2) /* G RX 1ST 1 */
#define B2063_G_RX_1ST2				B43_LP_RADIO(0x0B3) /* G RX 1ST 2 */
#define B2063_G_RX_1ST3				B43_LP_RADIO(0x0B4) /* G RX 1ST 3 */
#define B2063_G_RX_2ND1				B43_LP_RADIO(0x0B5) /* G RX 2ND 1 */
#define B2063_G_RX_2ND2				B43_LP_RADIO(0x0B6) /* G RX 2ND 2 */
#define B2063_G_RX_2ND3				B43_LP_RADIO(0x0B7) /* G RX 2ND 3 */
#define B2063_G_RX_2ND4				B43_LP_RADIO(0x0B8) /* G RX 2ND 4 */
#define B2063_G_RX_2ND5				B43_LP_RADIO(0x0B9) /* G RX 2ND 5 */
#define B2063_G_RX_2ND6				B43_LP_RADIO(0x0BA) /* G RX 2ND 6 */
#define B2063_G_RX_2ND7				B43_LP_RADIO(0x0BB) /* G RX 2ND 7 */
#define B2063_G_RX_2ND8				B43_LP_RADIO(0x0BC) /* G RX 2ND 8 */
#define B2063_G_RX_PS1				B43_LP_RADIO(0x0BD) /* G RX PS 1 */
#define B2063_G_RX_PS2				B43_LP_RADIO(0x0BE) /* G RX PS 2 */
#define B2063_G_RX_PS3				B43_LP_RADIO(0x0BF) /* G RX PS 3 */
#define B2063_G_RX_PS4				B43_LP_RADIO(0x0C0) /* G RX PS 4 */
#define B2063_G_RX_PS5				B43_LP_RADIO(0x0C1) /* G RX PS 5 */
#define B2063_G_RX_MIX1				B43_LP_RADIO(0x0C2) /* G RX MIX 1 */
#define B2063_G_RX_MIX2				B43_LP_RADIO(0x0C3) /* G RX MIX 2 */
#define B2063_G_RX_MIX3				B43_LP_RADIO(0x0C4) /* G RX MIX 3 */
#define B2063_G_RX_MIX4				B43_LP_RADIO(0x0C5) /* G RX MIX 4 */
#define B2063_G_RX_MIX5				B43_LP_RADIO(0x0C6) /* G RX MIX 5 */
#define B2063_G_RX_MIX6				B43_LP_RADIO(0x0C7) /* G RX MIX 6 */
#define B2063_G_RX_MIX7				B43_LP_RADIO(0x0C8) /* G RX MIX 7 */
#define B2063_G_RX_MIX8				B43_LP_RADIO(0x0C9) /* G RX MIX 8 */
#define B2063_G_RX_PDET1			B43_LP_RADIO(0x0CA) /* G RX PDET 1 */
#define B2063_G_RX_SPARES1			B43_LP_RADIO(0x0CB) /* G RX SPARES 1 */
#define B2063_G_RX_SPARES2			B43_LP_RADIO(0x0CC) /* G RX SPARES 2 */
#define B2063_G_RX_SPARES3			B43_LP_RADIO(0x0CD) /* G RX SPARES 3 */
#define B2063_A_RX_1ST1				B43_LP_RADIO(0x0CE) /* A RX 1ST 1 */
#define B2063_A_RX_1ST2				B43_LP_RADIO(0x0CF) /* A RX 1ST 2 */
#define B2063_A_RX_1ST3				B43_LP_RADIO(0x0D0) /* A RX 1ST 3 */
#define B2063_A_RX_1ST4				B43_LP_RADIO(0x0D1) /* A RX 1ST 4 */
#define B2063_A_RX_1ST5				B43_LP_RADIO(0x0D2) /* A RX 1ST 5 */
#define B2063_A_RX_2ND1				B43_LP_RADIO(0x0D3) /* A RX 2ND 1 */
#define B2063_A_RX_2ND2				B43_LP_RADIO(0x0D4) /* A RX 2ND 2 */
#define B2063_A_RX_2ND3				B43_LP_RADIO(0x0D5) /* A RX 2ND 3 */
#define B2063_A_RX_2ND4				B43_LP_RADIO(0x0D6) /* A RX 2ND 4 */
#define B2063_A_RX_2ND5				B43_LP_RADIO(0x0D7) /* A RX 2ND 5 */
#define B2063_A_RX_2ND6				B43_LP_RADIO(0x0D8) /* A RX 2ND 6 */
#define B2063_A_RX_2ND7				B43_LP_RADIO(0x0D9) /* A RX 2ND 7 */
#define B2063_A_RX_PS1				B43_LP_RADIO(0x0DA) /* A RX PS 1 */
#define B2063_A_RX_PS2				B43_LP_RADIO(0x0DB) /* A RX PS 2 */
#define B2063_A_RX_PS3				B43_LP_RADIO(0x0DC) /* A RX PS 3 */
#define B2063_A_RX_PS4				B43_LP_RADIO(0x0DD) /* A RX PS 4 */
#define B2063_A_RX_PS5				B43_LP_RADIO(0x0DE) /* A RX PS 5 */
#define B2063_A_RX_PS6				B43_LP_RADIO(0x0DF) /* A RX PS 6 */
#define B2063_A_RX_MIX1				B43_LP_RADIO(0x0E0) /* A RX MIX 1 */
#define B2063_A_RX_MIX2				B43_LP_RADIO(0x0E1) /* A RX MIX 2 */
#define B2063_A_RX_MIX3				B43_LP_RADIO(0x0E2) /* A RX MIX 3 */
#define B2063_A_RX_MIX4				B43_LP_RADIO(0x0E3) /* A RX MIX 4 */
#define B2063_A_RX_MIX5				B43_LP_RADIO(0x0E4) /* A RX MIX 5 */
#define B2063_A_RX_MIX6				B43_LP_RADIO(0x0E5) /* A RX MIX 6 */
#define B2063_A_RX_MIX7				B43_LP_RADIO(0x0E6) /* A RX MIX 7 */
#define B2063_A_RX_MIX8				B43_LP_RADIO(0x0E7) /* A RX MIX 8 */
#define B2063_A_RX_PWRDET1			B43_LP_RADIO(0x0E8) /* A RX PWRDET 1 */
#define B2063_A_RX_SPARE1			B43_LP_RADIO(0x0E9) /* A RX SPARE 1 */
#define B2063_A_RX_SPARE2			B43_LP_RADIO(0x0EA) /* A RX SPARE 2 */
#define B2063_A_RX_SPARE3			B43_LP_RADIO(0x0EB) /* A RX SPARE 3 */
#define B2063_RX_TIA_CTL1			B43_LP_RADIO(0x0EC) /* RX TIA Control 1 */
#define B2063_RX_TIA_CTL2			B43_LP_RADIO(0x0ED) /* RX TIA Control 2 */
#define B2063_RX_TIA_CTL3			B43_LP_RADIO(0x0EE) /* RX TIA Control 3 */
#define B2063_RX_TIA_CTL4			B43_LP_RADIO(0x0EF) /* RX TIA Control 4 */
#define B2063_RX_TIA_CTL5			B43_LP_RADIO(0x0F0) /* RX TIA Control 5 */
#define B2063_RX_TIA_CTL6			B43_LP_RADIO(0x0F1) /* RX TIA Control 6 */
#define B2063_RX_BB_CTL1			B43_LP_RADIO(0x0F2) /* RX BB Control 1 */
#define B2063_RX_BB_CTL2			B43_LP_RADIO(0x0F3) /* RX BB Control 2 */
#define B2063_RX_BB_CTL3			B43_LP_RADIO(0x0F4) /* RX BB Control 3 */
#define B2063_RX_BB_CTL4			B43_LP_RADIO(0x0F5) /* RX BB Control 4 */
#define B2063_RX_BB_CTL5			B43_LP_RADIO(0x0F6) /* RX BB Control 5 */
#define B2063_RX_BB_CTL6			B43_LP_RADIO(0x0F7) /* RX BB Control 6 */
#define B2063_RX_BB_CTL7			B43_LP_RADIO(0x0F8) /* RX BB Control 7 */
#define B2063_RX_BB_CTL8			B43_LP_RADIO(0x0F9) /* RX BB Control 8 */
#define B2063_RX_BB_CTL9			B43_LP_RADIO(0x0FA) /* RX BB Control 9 */
#define B2063_TX_RF_CTL1			B43_LP_RADIO(0x0FB) /* TX RF Control 1 */
#define B2063_TX_RF_IDAC_LO_RF_I		B43_LP_RADIO(0x0FC) /* TX RF IDAC LO RF I */
#define B2063_TX_RF_IDAC_LO_RF_Q		B43_LP_RADIO(0x0FD) /* TX RF IDAC LO RF Q */
#define B2063_TX_RF_IDAC_LO_BB_I		B43_LP_RADIO(0x0FE) /* TX RF IDAC LO BB I */
#define B2063_TX_RF_IDAC_LO_BB_Q		B43_LP_RADIO(0x0FF) /* TX RF IDAC LO BB Q */
#define B2063_TX_RF_CTL2			B43_LP_RADIO(0x100) /* TX RF Control 2 */
#define B2063_TX_RF_CTL3			B43_LP_RADIO(0x101) /* TX RF Control 3 */
#define B2063_TX_RF_CTL4			B43_LP_RADIO(0x102) /* TX RF Control 4 */
#define B2063_TX_RF_CTL5			B43_LP_RADIO(0x103) /* TX RF Control 5 */
#define B2063_TX_RF_CTL6			B43_LP_RADIO(0x104) /* TX RF Control 6 */
#define B2063_TX_RF_CTL7			B43_LP_RADIO(0x105) /* TX RF Control 7 */
#define B2063_TX_RF_CTL8			B43_LP_RADIO(0x106) /* TX RF Control 8 */
#define B2063_TX_RF_CTL9			B43_LP_RADIO(0x107) /* TX RF Control 9 */
#define B2063_TX_RF_CTL10			B43_LP_RADIO(0x108) /* TX RF Control 10 */
#define B2063_TX_RF_CTL14			B43_LP_RADIO(0x109) /* TX RF Control 14 */
#define B2063_TX_RF_CTL15			B43_LP_RADIO(0x10A) /* TX RF Control 15 */
#define B2063_PA_CTL1				B43_LP_RADIO(0x10B) /* PA Control 1 */
#define B2063_PA_CTL2				B43_LP_RADIO(0x10C) /* PA Control 2 */
#define B2063_PA_CTL3				B43_LP_RADIO(0x10D) /* PA Control 3 */
#define B2063_PA_CTL4				B43_LP_RADIO(0x10E) /* PA Control 4 */
#define B2063_PA_CTL5				B43_LP_RADIO(0x10F) /* PA Control 5 */
#define B2063_PA_CTL6				B43_LP_RADIO(0x110) /* PA Control 6 */
#define B2063_PA_CTL7				B43_LP_RADIO(0x111) /* PA Control 7 */
#define B2063_PA_CTL8				B43_LP_RADIO(0x112) /* PA Control 8 */
#define B2063_PA_CTL9				B43_LP_RADIO(0x113) /* PA Control 9 */
#define B2063_PA_CTL10				B43_LP_RADIO(0x114) /* PA Control 10 */
#define B2063_PA_CTL11				B43_LP_RADIO(0x115) /* PA Control 11 */
#define B2063_PA_CTL12				B43_LP_RADIO(0x116) /* PA Control 12 */
#define B2063_PA_CTL13				B43_LP_RADIO(0x117) /* PA Control 13 */
#define B2063_TX_BB_CTL1			B43_LP_RADIO(0x118) /* TX BB Control 1 */
#define B2063_TX_BB_CTL2			B43_LP_RADIO(0x119) /* TX BB Control 2 */
#define B2063_TX_BB_CTL3			B43_LP_RADIO(0x11A) /* TX BB Control 3 */
#define B2063_TX_BB_CTL4			B43_LP_RADIO(0x11B) /* TX BB Control 4 */
#define B2063_GPIO_CTL1				B43_LP_RADIO(0x11C) /* GPIO Control 1 */
#define B2063_VREG_CTL1				B43_LP_RADIO(0x11D) /* VREG Control 1 */
#define B2063_AMUX_CTL1				B43_LP_RADIO(0x11E) /* AMUX Control 1 */
#define B2063_IQ_CALIB_GVAR			B43_LP_RADIO(0x11F) /* IQ Calibration GVAR */
#define B2063_IQ_CALIB_CTL1			B43_LP_RADIO(0x120) /* IQ Calibration Control 1 */
#define B2063_IQ_CALIB_CTL2			B43_LP_RADIO(0x121) /* IQ Calibration Control 2 */
#define B2063_TEMPSENSE_CTL1			B43_LP_RADIO(0x122) /* TEMPSENSE Control 1 */
#define B2063_TEMPSENSE_CTL2			B43_LP_RADIO(0x123) /* TEMPSENSE Control 2 */
#define B2063_TX_RX_LOOPBACK1			B43_LP_RADIO(0x124) /* TX/RX LOOPBACK 1 */
#define B2063_TX_RX_LOOPBACK2			B43_LP_RADIO(0x125) /* TX/RX LOOPBACK 2 */
#define B2063_EXT_TSSI_CTL1			B43_LP_RADIO(0x126) /* EXT TSSI Control 1 */
#define B2063_EXT_TSSI_CTL2			B43_LP_RADIO(0x127) /* EXT TSSI Control 2 */
#define B2063_AFE_CTL				B43_LP_RADIO(0x128) /* AFE Control */



struct b43_phy_lp {
	//TODO
};


struct b43_phy_operations;
extern const struct b43_phy_operations b43_phyops_lp;

#endif /* LINUX_B43_PHY_LP_H_ */
