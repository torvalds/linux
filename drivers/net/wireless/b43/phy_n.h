#ifndef B43_NPHY_H_
#define B43_NPHY_H_

#include "phy_common.h"


/* N-PHY registers. */

#define B43_NPHY_BBCFG				B43_PHY_N(0x001) /* BB config */
#define  B43_NPHY_BBCFG_RSTCCA			0x4000 /* Reset CCA */
#define  B43_NPHY_BBCFG_RSTRX			0x8000 /* Reset RX */
#define B43_NPHY_CHANNEL			B43_PHY_N(0x005) /* Channel */
#define B43_NPHY_TXERR				B43_PHY_N(0x007) /* TX error */
#define B43_NPHY_BANDCTL			B43_PHY_N(0x009) /* Band control */
#define  B43_NPHY_BANDCTL_5GHZ			0x0001 /* Use the 5GHz band */
#define B43_NPHY_4WI_ADDR			B43_PHY_N(0x00B) /* Four-wire bus address */
#define B43_NPHY_4WI_DATAHI			B43_PHY_N(0x00C) /* Four-wire bus data high */
#define B43_NPHY_4WI_DATALO			B43_PHY_N(0x00D) /* Four-wire bus data low */
#define B43_NPHY_BIST_STAT0			B43_PHY_N(0x00E) /* Built-in self test status 0 */
#define B43_NPHY_BIST_STAT1			B43_PHY_N(0x00F) /* Built-in self test status 1 */

#define B43_NPHY_C1_DESPWR			B43_PHY_N(0x018) /* Core 1 desired power */
#define B43_NPHY_C1_CCK_DESPWR			B43_PHY_N(0x019) /* Core 1 CCK desired power */
#define B43_NPHY_C1_BCLIPBKOFF			B43_PHY_N(0x01A) /* Core 1 barely clip backoff */
#define B43_NPHY_C1_CCK_BCLIPBKOFF		B43_PHY_N(0x01B) /* Core 1 CCK barely clip backoff */
#define B43_NPHY_C1_CGAINI			B43_PHY_N(0x01C) /* Core 1 compute gain info */
#define  B43_NPHY_C1_CGAINI_GAINBKOFF		0x001F /* Gain backoff */
#define  B43_NPHY_C1_CGAINI_GAINBKOFF_SHIFT	0
#define  B43_NPHY_C1_CGAINI_CLIPGBKOFF		0x03E0 /* Clip gain backoff */
#define  B43_NPHY_C1_CGAINI_CLIPGBKOFF_SHIFT	5
#define  B43_NPHY_C1_CGAINI_GAINSTEP		0x1C00 /* Gain step */
#define  B43_NPHY_C1_CGAINI_GAINSTEP_SHIFT	10
#define  B43_NPHY_C1_CGAINI_CL2DETECT		0x2000 /* Clip 2 detect mask */
#define B43_NPHY_C1_CCK_CGAINI			B43_PHY_N(0x01D) /* Core 1 CCK compute gain info */
#define  B43_NPHY_C1_CCK_CGAINI_GAINBKOFF	0x001F /* Gain backoff */
#define  B43_NPHY_C1_CCK_CGAINI_CLIPGBKOFF	0x01E0 /* CCK barely clip gain backoff */
#define B43_NPHY_C1_MINMAX_GAIN			B43_PHY_N(0x01E) /* Core 1 min/max gain */
#define  B43_NPHY_C1_MINGAIN			0x00FF /* Minimum gain */
#define  B43_NPHY_C1_MINGAIN_SHIFT		0
#define  B43_NPHY_C1_MAXGAIN			0xFF00 /* Maximum gain */
#define  B43_NPHY_C1_MAXGAIN_SHIFT		8
#define B43_NPHY_C1_CCK_MINMAX_GAIN		B43_PHY_N(0x01F) /* Core 1 CCK min/max gain */
#define  B43_NPHY_C1_CCK_MINGAIN		0x00FF /* Minimum gain */
#define  B43_NPHY_C1_CCK_MINGAIN_SHIFT		0
#define  B43_NPHY_C1_CCK_MAXGAIN		0xFF00 /* Maximum gain */
#define  B43_NPHY_C1_CCK_MAXGAIN_SHIFT		8
#define B43_NPHY_C1_INITGAIN			B43_PHY_N(0x020) /* Core 1 initial gain code */
#define  B43_NPHY_C1_INITGAIN_EXTLNA		0x0001 /* External LNA index */
#define  B43_NPHY_C1_INITGAIN_LNA		0x0006 /* LNA index */
#define  B43_NPHY_C1_INITGAIN_LNAIDX_SHIFT	1
#define  B43_NPHY_C1_INITGAIN_HPVGA1		0x0078 /* HPVGA1 index */
#define  B43_NPHY_C1_INITGAIN_HPVGA1_SHIFT	3
#define  B43_NPHY_C1_INITGAIN_HPVGA2		0x0F80 /* HPVGA2 index */
#define  B43_NPHY_C1_INITGAIN_HPVGA2_SHIFT	7
#define  B43_NPHY_C1_INITGAIN_TRRX		0x1000 /* TR RX index */
#define  B43_NPHY_C1_INITGAIN_TRTX		0x2000 /* TR TX index */
#define B43_NPHY_C1_CLIP1_HIGAIN		B43_PHY_N(0x021) /* Core 1 clip1 high gain code */
#define B43_NPHY_C1_CLIP1_MEDGAIN		B43_PHY_N(0x022) /* Core 1 clip1 medium gain code */
#define B43_NPHY_C1_CLIP1_LOGAIN		B43_PHY_N(0x023) /* Core 1 clip1 low gain code */
#define B43_NPHY_C1_CLIP2_GAIN			B43_PHY_N(0x024) /* Core 1 clip2 gain code */
#define B43_NPHY_C1_FILTERGAIN			B43_PHY_N(0x025) /* Core 1 filter gain */
#define B43_NPHY_C1_LPF_QHPF_BW			B43_PHY_N(0x026) /* Core 1 LPF Q HP F bandwidth */
#define B43_NPHY_C1_CLIPWBTHRES			B43_PHY_N(0x027) /* Core 1 clip wideband threshold */
#define  B43_NPHY_C1_CLIPWBTHRES_CLIP2		0x003F /* Clip 2 */
#define  B43_NPHY_C1_CLIPWBTHRES_CLIP2_SHIFT	0
#define  B43_NPHY_C1_CLIPWBTHRES_CLIP1		0x0FC0 /* Clip 1 */
#define  B43_NPHY_C1_CLIPWBTHRES_CLIP1_SHIFT	6
#define B43_NPHY_C1_W1THRES			B43_PHY_N(0x028) /* Core 1 W1 threshold */
#define B43_NPHY_C1_EDTHRES			B43_PHY_N(0x029) /* Core 1 ED threshold */
#define B43_NPHY_C1_SMSIGTHRES			B43_PHY_N(0x02A) /* Core 1 small sig threshold */
#define B43_NPHY_C1_NBCLIPTHRES			B43_PHY_N(0x02B) /* Core 1 NB clip threshold */
#define B43_NPHY_C1_CLIP1THRES			B43_PHY_N(0x02C) /* Core 1 clip1 threshold */
#define B43_NPHY_C1_CLIP2THRES			B43_PHY_N(0x02D) /* Core 1 clip2 threshold */

#define B43_NPHY_C2_DESPWR			B43_PHY_N(0x02E) /* Core 2 desired power */
#define B43_NPHY_C2_CCK_DESPWR			B43_PHY_N(0x02F) /* Core 2 CCK desired power */
#define B43_NPHY_C2_BCLIPBKOFF			B43_PHY_N(0x030) /* Core 2 barely clip backoff */
#define B43_NPHY_C2_CCK_BCLIPBKOFF		B43_PHY_N(0x031) /* Core 2 CCK barely clip backoff */
#define B43_NPHY_C2_CGAINI			B43_PHY_N(0x032) /* Core 2 compute gain info */
#define  B43_NPHY_C2_CGAINI_GAINBKOFF		0x001F /* Gain backoff */
#define  B43_NPHY_C2_CGAINI_GAINBKOFF_SHIFT	0
#define  B43_NPHY_C2_CGAINI_CLIPGBKOFF		0x03E0 /* Clip gain backoff */
#define  B43_NPHY_C2_CGAINI_CLIPGBKOFF_SHIFT	5
#define  B43_NPHY_C2_CGAINI_GAINSTEP		0x1C00 /* Gain step */
#define  B43_NPHY_C2_CGAINI_GAINSTEP_SHIFT	10
#define  B43_NPHY_C2_CGAINI_CL2DETECT		0x2000 /* Clip 2 detect mask */
#define B43_NPHY_C2_CCK_CGAINI			B43_PHY_N(0x033) /* Core 2 CCK compute gain info */
#define  B43_NPHY_C2_CCK_CGAINI_GAINBKOFF	0x001F /* Gain backoff */
#define  B43_NPHY_C2_CCK_CGAINI_CLIPGBKOFF	0x01E0 /* CCK barely clip gain backoff */
#define B43_NPHY_C2_MINMAX_GAIN			B43_PHY_N(0x034) /* Core 2 min/max gain */
#define  B43_NPHY_C2_MINGAIN			0x00FF /* Minimum gain */
#define  B43_NPHY_C2_MINGAIN_SHIFT		0
#define  B43_NPHY_C2_MAXGAIN			0xFF00 /* Maximum gain */
#define  B43_NPHY_C2_MAXGAIN_SHIFT		8
#define B43_NPHY_C2_CCK_MINMAX_GAIN		B43_PHY_N(0x035) /* Core 2 CCK min/max gain */
#define  B43_NPHY_C2_CCK_MINGAIN		0x00FF /* Minimum gain */
#define  B43_NPHY_C2_CCK_MINGAIN_SHIFT		0
#define  B43_NPHY_C2_CCK_MAXGAIN		0xFF00 /* Maximum gain */
#define  B43_NPHY_C2_CCK_MAXGAIN_SHIFT		8
#define B43_NPHY_C2_INITGAIN			B43_PHY_N(0x036) /* Core 2 initial gain code */
#define  B43_NPHY_C2_INITGAIN_EXTLNA		0x0001 /* External LNA index */
#define  B43_NPHY_C2_INITGAIN_LNA		0x0006 /* LNA index */
#define  B43_NPHY_C2_INITGAIN_LNAIDX_SHIFT	1
#define  B43_NPHY_C2_INITGAIN_HPVGA1		0x0078 /* HPVGA1 index */
#define  B43_NPHY_C2_INITGAIN_HPVGA1_SHIFT	3
#define  B43_NPHY_C2_INITGAIN_HPVGA2		0x0F80 /* HPVGA2 index */
#define  B43_NPHY_C2_INITGAIN_HPVGA2_SHIFT	7
#define  B43_NPHY_C2_INITGAIN_TRRX		0x1000 /* TR RX index */
#define  B43_NPHY_C2_INITGAIN_TRTX		0x2000 /* TR TX index */
#define B43_NPHY_C2_CLIP1_HIGAIN		B43_PHY_N(0x037) /* Core 2 clip1 high gain code */
#define B43_NPHY_C2_CLIP1_MEDGAIN		B43_PHY_N(0x038) /* Core 2 clip1 medium gain code */
#define B43_NPHY_C2_CLIP1_LOGAIN		B43_PHY_N(0x039) /* Core 2 clip1 low gain code */
#define B43_NPHY_C2_CLIP2_GAIN			B43_PHY_N(0x03A) /* Core 2 clip2 gain code */
#define B43_NPHY_C2_FILTERGAIN			B43_PHY_N(0x03B) /* Core 2 filter gain */
#define B43_NPHY_C2_LPF_QHPF_BW			B43_PHY_N(0x03C) /* Core 2 LPF Q HP F bandwidth */
#define B43_NPHY_C2_CLIPWBTHRES			B43_PHY_N(0x03D) /* Core 2 clip wideband threshold */
#define  B43_NPHY_C2_CLIPWBTHRES_CLIP2		0x003F /* Clip 2 */
#define  B43_NPHY_C2_CLIPWBTHRES_CLIP2_SHIFT	0
#define  B43_NPHY_C2_CLIPWBTHRES_CLIP1		0x0FC0 /* Clip 1 */
#define  B43_NPHY_C2_CLIPWBTHRES_CLIP1_SHIFT	6
#define B43_NPHY_C2_W1THRES			B43_PHY_N(0x03E) /* Core 2 W1 threshold */
#define B43_NPHY_C2_EDTHRES			B43_PHY_N(0x03F) /* Core 2 ED threshold */
#define B43_NPHY_C2_SMSIGTHRES			B43_PHY_N(0x040) /* Core 2 small sig threshold */
#define B43_NPHY_C2_NBCLIPTHRES			B43_PHY_N(0x041) /* Core 2 NB clip threshold */
#define B43_NPHY_C2_CLIP1THRES			B43_PHY_N(0x042) /* Core 2 clip1 threshold */
#define B43_NPHY_C2_CLIP2THRES			B43_PHY_N(0x043) /* Core 2 clip2 threshold */

#define B43_NPHY_CRS_THRES1			B43_PHY_N(0x044) /* CRS threshold 1 */
#define B43_NPHY_CRS_THRES2			B43_PHY_N(0x045) /* CRS threshold 2 */
#define B43_NPHY_CRS_THRES3			B43_PHY_N(0x046) /* CRS threshold 3 */
#define B43_NPHY_CRSCTL				B43_PHY_N(0x047) /* CRS control */
#define B43_NPHY_DCFADDR			B43_PHY_N(0x048) /* DC filter address */
#define B43_NPHY_RXF20_NUM0			B43_PHY_N(0x049) /* RX filter 20 numerator 0 */
#define B43_NPHY_RXF20_NUM1			B43_PHY_N(0x04A) /* RX filter 20 numerator 1 */
#define B43_NPHY_RXF20_NUM2			B43_PHY_N(0x04B) /* RX filter 20 numerator 2 */
#define B43_NPHY_RXF20_DENOM0			B43_PHY_N(0x04C) /* RX filter 20 denominator 0 */
#define B43_NPHY_RXF20_DENOM1			B43_PHY_N(0x04D) /* RX filter 20 denominator 1 */
#define B43_NPHY_RXF20_NUM10			B43_PHY_N(0x04E) /* RX filter 20 numerator 10 */
#define B43_NPHY_RXF20_NUM11			B43_PHY_N(0x04F) /* RX filter 20 numerator 11 */
#define B43_NPHY_RXF20_NUM12			B43_PHY_N(0x050) /* RX filter 20 numerator 12 */
#define B43_NPHY_RXF20_DENOM10			B43_PHY_N(0x051) /* RX filter 20 denominator 10 */
#define B43_NPHY_RXF20_DENOM11			B43_PHY_N(0x052) /* RX filter 20 denominator 11 */
#define B43_NPHY_RXF40_NUM0			B43_PHY_N(0x053) /* RX filter 40 numerator 0 */
#define B43_NPHY_RXF40_NUM1			B43_PHY_N(0x054) /* RX filter 40 numerator 1 */
#define B43_NPHY_RXF40_NUM2			B43_PHY_N(0x055) /* RX filter 40 numerator 2 */
#define B43_NPHY_RXF40_DENOM0			B43_PHY_N(0x056) /* RX filter 40 denominator 0 */
#define B43_NPHY_RXF40_DENOM1			B43_PHY_N(0x057) /* RX filter 40 denominator 1 */
#define B43_NPHY_RXF40_NUM10			B43_PHY_N(0x058) /* RX filter 40 numerator 10 */
#define B43_NPHY_RXF40_NUM11			B43_PHY_N(0x059) /* RX filter 40 numerator 11 */
#define B43_NPHY_RXF40_NUM12			B43_PHY_N(0x05A) /* RX filter 40 numerator 12 */
#define B43_NPHY_RXF40_DENOM10			B43_PHY_N(0x05B) /* RX filter 40 denominator 10 */
#define B43_NPHY_RXF40_DENOM11			B43_PHY_N(0x05C) /* RX filter 40 denominator 11 */
#define B43_NPHY_PPROC_RSTLEN			B43_PHY_N(0x060) /* Packet processing reset length */
#define B43_NPHY_INITCARR_DLEN			B43_PHY_N(0x061) /* Initial carrier detection length */
#define B43_NPHY_CLIP1CARR_DLEN			B43_PHY_N(0x062) /* Clip1 carrier detection length */
#define B43_NPHY_CLIP2CARR_DLEN			B43_PHY_N(0x063) /* Clip2 carrier detection length */
#define B43_NPHY_INITGAIN_SLEN			B43_PHY_N(0x064) /* Initial gain settle length */
#define B43_NPHY_CLIP1GAIN_SLEN			B43_PHY_N(0x065) /* Clip1 gain settle length */
#define B43_NPHY_CLIP2GAIN_SLEN			B43_PHY_N(0x066) /* Clip2 gain settle length */
#define B43_NPHY_PACKGAIN_SLEN			B43_PHY_N(0x067) /* Packet gain settle length */
#define B43_NPHY_CARRSRC_TLEN			B43_PHY_N(0x068) /* Carrier search timeout length */
#define B43_NPHY_TISRC_TLEN			B43_PHY_N(0x069) /* Timing search timeout length */
#define B43_NPHY_ENDROP_TLEN			B43_PHY_N(0x06A) /* Energy drop timeout length */
#define B43_NPHY_CLIP1_NBDWELL_LEN		B43_PHY_N(0x06B) /* Clip1 NB dwell length */
#define B43_NPHY_CLIP2_NBDWELL_LEN		B43_PHY_N(0x06C) /* Clip2 NB dwell length */
#define B43_NPHY_W1CLIP1_DWELL_LEN		B43_PHY_N(0x06D) /* W1 clip1 dwell length */
#define B43_NPHY_W1CLIP2_DWELL_LEN		B43_PHY_N(0x06E) /* W1 clip2 dwell length */
#define B43_NPHY_W2CLIP1_DWELL_LEN		B43_PHY_N(0x06F) /* W2 clip1 dwell length */
#define B43_NPHY_PLOAD_CSENSE_EXTLEN		B43_PHY_N(0x070) /* Payload carrier sense extension length */
#define B43_NPHY_EDROP_CSENSE_EXTLEN		B43_PHY_N(0x071) /* Energy drop carrier sense extension length */
#define B43_NPHY_TABLE_ADDR			B43_PHY_N(0x072) /* Table address */
#define B43_NPHY_TABLE_DATALO			B43_PHY_N(0x073) /* Table data low */
#define B43_NPHY_TABLE_DATAHI			B43_PHY_N(0x074) /* Table data high */
#define B43_NPHY_WWISE_LENIDX			B43_PHY_N(0x075) /* WWiSE length index */
#define B43_NPHY_TGNSYNC_LENIDX			B43_PHY_N(0x076) /* TGNsync length index */
#define B43_NPHY_TXMACIF_HOLDOFF		B43_PHY_N(0x077) /* TX MAC IF Hold off */
#define B43_NPHY_RFCTL_CMD			B43_PHY_N(0x078) /* RF control (command) */
#define  B43_NPHY_RFCTL_CMD_START		0x0001 /* Start sequence */
#define  B43_NPHY_RFCTL_CMD_RXTX		0x0002 /* RX/TX */
#define  B43_NPHY_RFCTL_CMD_CORESEL		0x0038 /* Core select */
#define  B43_NPHY_RFCTL_CMD_CORESEL_SHIFT	3
#define  B43_NPHY_RFCTL_CMD_PORFORCE		0x0040 /* POR force */
#define  B43_NPHY_RFCTL_CMD_OEPORFORCE		0x0080 /* OE POR force */
#define  B43_NPHY_RFCTL_CMD_RXEN		0x0100 /* RX enable */
#define  B43_NPHY_RFCTL_CMD_TXEN		0x0200 /* TX enable */
#define  B43_NPHY_RFCTL_CMD_CHIP0PU		0x0400 /* Chip0 PU */
#define  B43_NPHY_RFCTL_CMD_EN			0x0800 /* Radio enabled */
#define  B43_NPHY_RFCTL_CMD_SEQENCORE		0xF000 /* Seq en core */
#define  B43_NPHY_RFCTL_CMD_SEQENCORE_SHIFT	12
#define B43_NPHY_RFCTL_RSSIO1			B43_PHY_N(0x07A) /* RF control (RSSI others 1) */
#define  B43_NPHY_RFCTL_RSSIO1_RXPD		0x0001 /* RX PD */
#define  B43_NPHY_RFCTL_RSSIO1_TXPD		0x0002 /* TX PD */
#define  B43_NPHY_RFCTL_RSSIO1_PAPD		0x0004 /* PA PD */
#define  B43_NPHY_RFCTL_RSSIO1_RSSICTL		0x0030 /* RSSI control */
#define  B43_NPHY_RFCTL_RSSIO1_LPFBW		0x00C0 /* LPF bandwidth */
#define  B43_NPHY_RFCTL_RSSIO1_HPFBWHI		0x0100 /* HPF bandwidth high */
#define  B43_NPHY_RFCTL_RSSIO1_HIQDISCO		0x0200 /* HIQ dis core */
#define B43_NPHY_RFCTL_RXG1			B43_PHY_N(0x07B) /* RF control (RX gain 1) */
#define B43_NPHY_RFCTL_TXG1			B43_PHY_N(0x07C) /* RF control (TX gain 1) */
#define B43_NPHY_RFCTL_RSSIO2			B43_PHY_N(0x07D) /* RF control (RSSI others 2) */
#define  B43_NPHY_RFCTL_RSSIO2_RXPD		0x0001 /* RX PD */
#define  B43_NPHY_RFCTL_RSSIO2_TXPD		0x0002 /* TX PD */
#define  B43_NPHY_RFCTL_RSSIO2_PAPD		0x0004 /* PA PD */
#define  B43_NPHY_RFCTL_RSSIO2_RSSICTL		0x0030 /* RSSI control */
#define  B43_NPHY_RFCTL_RSSIO2_LPFBW		0x00C0 /* LPF bandwidth */
#define  B43_NPHY_RFCTL_RSSIO2_HPFBWHI		0x0100 /* HPF bandwidth high */
#define  B43_NPHY_RFCTL_RSSIO2_HIQDISCO		0x0200 /* HIQ dis core */
#define B43_NPHY_RFCTL_RXG2			B43_PHY_N(0x07E) /* RF control (RX gain 2) */
#define B43_NPHY_RFCTL_TXG2			B43_PHY_N(0x07F) /* RF control (TX gain 2) */
#define B43_NPHY_RFCTL_RSSIO3			B43_PHY_N(0x080) /* RF control (RSSI others 3) */
#define  B43_NPHY_RFCTL_RSSIO3_RXPD		0x0001 /* RX PD */
#define  B43_NPHY_RFCTL_RSSIO3_TXPD		0x0002 /* TX PD */
#define  B43_NPHY_RFCTL_RSSIO3_PAPD		0x0004 /* PA PD */
#define  B43_NPHY_RFCTL_RSSIO3_RSSICTL		0x0030 /* RSSI control */
#define  B43_NPHY_RFCTL_RSSIO3_LPFBW		0x00C0 /* LPF bandwidth */
#define  B43_NPHY_RFCTL_RSSIO3_HPFBWHI		0x0100 /* HPF bandwidth high */
#define  B43_NPHY_RFCTL_RSSIO3_HIQDISCO		0x0200 /* HIQ dis core */
#define B43_NPHY_RFCTL_RXG3			B43_PHY_N(0x081) /* RF control (RX gain 3) */
#define B43_NPHY_RFCTL_TXG3			B43_PHY_N(0x082) /* RF control (TX gain 3) */
#define B43_NPHY_RFCTL_RSSIO4			B43_PHY_N(0x083) /* RF control (RSSI others 4) */
#define  B43_NPHY_RFCTL_RSSIO4_RXPD		0x0001 /* RX PD */
#define  B43_NPHY_RFCTL_RSSIO4_TXPD		0x0002 /* TX PD */
#define  B43_NPHY_RFCTL_RSSIO4_PAPD		0x0004 /* PA PD */
#define  B43_NPHY_RFCTL_RSSIO4_RSSICTL		0x0030 /* RSSI control */
#define  B43_NPHY_RFCTL_RSSIO4_LPFBW		0x00C0 /* LPF bandwidth */
#define  B43_NPHY_RFCTL_RSSIO4_HPFBWHI		0x0100 /* HPF bandwidth high */
#define  B43_NPHY_RFCTL_RSSIO4_HIQDISCO		0x0200 /* HIQ dis core */
#define B43_NPHY_RFCTL_RXG4			B43_PHY_N(0x084) /* RF control (RX gain 4) */
#define B43_NPHY_RFCTL_TXG4			B43_PHY_N(0x085) /* RF control (TX gain 4) */
#define B43_NPHY_C1_TXIQ_COMP_OFF		B43_PHY_N(0x087) /* Core 1 TX I/Q comp offset */
#define B43_NPHY_C2_TXIQ_COMP_OFF		B43_PHY_N(0x088) /* Core 2 TX I/Q comp offset */
#define B43_NPHY_C1_TXCTL			B43_PHY_N(0x08B) /* Core 1 TX control */
#define B43_NPHY_C2_TXCTL			B43_PHY_N(0x08C) /* Core 2 TX control */
#define B43_NPHY_AFECTL_OVER1			B43_PHY_N(0x08F) /* AFE control override 1 */
#define B43_NPHY_SCRAM_SIGCTL			B43_PHY_N(0x090) /* Scram signal control */
#define  B43_NPHY_SCRAM_SIGCTL_INITST		0x007F /* Initial state value */
#define  B43_NPHY_SCRAM_SIGCTL_INITST_SHIFT	0
#define  B43_NPHY_SCRAM_SIGCTL_SCM		0x0080 /* Scram control mode */
#define  B43_NPHY_SCRAM_SIGCTL_SICE		0x0100 /* Scram index control enable */
#define  B43_NPHY_SCRAM_SIGCTL_START		0xFE00 /* Scram start bit */
#define  B43_NPHY_SCRAM_SIGCTL_START_SHIFT	9
#define B43_NPHY_RFCTL_INTC1			B43_PHY_N(0x091) /* RF control (intc 1) */
#define B43_NPHY_RFCTL_INTC2			B43_PHY_N(0x092) /* RF control (intc 2) */
#define B43_NPHY_RFCTL_INTC3			B43_PHY_N(0x093) /* RF control (intc 3) */
#define B43_NPHY_RFCTL_INTC4			B43_PHY_N(0x094) /* RF control (intc 4) */
#define B43_NPHY_NRDTO_WWISE			B43_PHY_N(0x095) /* # datatones WWiSE */
#define B43_NPHY_NRDTO_TGNSYNC			B43_PHY_N(0x096) /* # datatones TGNsync */
#define B43_NPHY_SIGFMOD_WWISE			B43_PHY_N(0x097) /* Signal field mod WWiSE */
#define B43_NPHY_LEG_SIGFMOD_11N		B43_PHY_N(0x098) /* Legacy signal field mod 11n */
#define B43_NPHY_HT_SIGFMOD_11N			B43_PHY_N(0x099) /* HT signal field mod 11n */
#define B43_NPHY_C1_RXIQ_COMPA0			B43_PHY_N(0x09A) /* Core 1 RX I/Q comp A0 */
#define B43_NPHY_C1_RXIQ_COMPB0			B43_PHY_N(0x09B) /* Core 1 RX I/Q comp B0 */
#define B43_NPHY_C2_RXIQ_COMPA1			B43_PHY_N(0x09C) /* Core 2 RX I/Q comp A1 */
#define B43_NPHY_C2_RXIQ_COMPB1			B43_PHY_N(0x09D) /* Core 2 RX I/Q comp B1 */
#define B43_NPHY_RXCTL				B43_PHY_N(0x0A0) /* RX control */
#define  B43_NPHY_RXCTL_BSELU20			0x0010 /* Band select upper 20 */
#define  B43_NPHY_RXCTL_RIFSEN			0x0080 /* RIFS enable */
#define B43_NPHY_RFSEQMODE			B43_PHY_N(0x0A1) /* RF seq mode */
#define  B43_NPHY_RFSEQMODE_CAOVER		0x0001 /* Core active override */
#define  B43_NPHY_RFSEQMODE_TROVER		0x0002 /* Trigger override */
#define B43_NPHY_RFSEQCA			B43_PHY_N(0x0A2) /* RF seq core active */
#define  B43_NPHY_RFSEQCA_TXEN			0x000F /* TX enable */
#define  B43_NPHY_RFSEQCA_TXEN_SHIFT		0
#define  B43_NPHY_RFSEQCA_RXEN			0x00F0 /* RX enable */
#define  B43_NPHY_RFSEQCA_RXEN_SHIFT		4
#define  B43_NPHY_RFSEQCA_TXDIS			0x0F00 /* TX disable */
#define  B43_NPHY_RFSEQCA_TXDIS_SHIFT		8
#define  B43_NPHY_RFSEQCA_RXDIS			0xF000 /* RX disable */
#define  B43_NPHY_RFSEQCA_RXDIS_SHIFT		12
#define B43_NPHY_RFSEQTR			B43_PHY_N(0x0A3) /* RF seq trigger */
#define  B43_NPHY_RFSEQTR_RX2TX			0x0001 /* RX2TX */
#define  B43_NPHY_RFSEQTR_TX2RX			0x0002 /* TX2RX */
#define  B43_NPHY_RFSEQTR_UPGH			0x0004 /* Update gain H */
#define  B43_NPHY_RFSEQTR_UPGL			0x0008 /* Update gain L */
#define  B43_NPHY_RFSEQTR_UPGU			0x0010 /* Update gain U */
#define  B43_NPHY_RFSEQTR_RST2RX		0x0020 /* Reset to RX */
#define B43_NPHY_RFSEQST			B43_PHY_N(0x0A4) /* RF seq status. Values same as trigger. */
#define B43_NPHY_AFECTL_OVER			B43_PHY_N(0x0A5) /* AFE control override */
#define B43_NPHY_AFECTL_C1			B43_PHY_N(0x0A6) /* AFE control core 1 */
#define B43_NPHY_AFECTL_C2			B43_PHY_N(0x0A7) /* AFE control core 2 */
#define B43_NPHY_AFECTL_C3			B43_PHY_N(0x0A8) /* AFE control core 3 */
#define B43_NPHY_AFECTL_C4			B43_PHY_N(0x0A9) /* AFE control core 4 */
#define B43_NPHY_AFECTL_DACGAIN1		B43_PHY_N(0x0AA) /* AFE control DAC gain 1 */
#define B43_NPHY_AFECTL_DACGAIN2		B43_PHY_N(0x0AB) /* AFE control DAC gain 2 */
#define B43_NPHY_AFECTL_DACGAIN3		B43_PHY_N(0x0AC) /* AFE control DAC gain 3 */
#define B43_NPHY_AFECTL_DACGAIN4		B43_PHY_N(0x0AD) /* AFE control DAC gain 4 */
#define B43_NPHY_STR_ADDR1			B43_PHY_N(0x0AE) /* STR address 1 */
#define B43_NPHY_STR_ADDR2			B43_PHY_N(0x0AF) /* STR address 2 */
#define B43_NPHY_CLASSCTL			B43_PHY_N(0x0B0) /* Classifier control */
#define  B43_NPHY_CLASSCTL_CCKEN		0x0001 /* CCK enable */
#define  B43_NPHY_CLASSCTL_OFDMEN		0x0002 /* OFDM enable */
#define  B43_NPHY_CLASSCTL_WAITEDEN		0x0004 /* Waited enable */
#define B43_NPHY_IQFLIP				B43_PHY_N(0x0B1) /* I/Q flip */
#define  B43_NPHY_IQFLIP_ADC1			0x0001 /* ADC1 */
#define  B43_NPHY_IQFLIP_ADC2			0x0010 /* ADC2 */
#define B43_NPHY_SISO_SNR_THRES			B43_PHY_N(0x0B2) /* SISO SNR threshold */
#define B43_NPHY_SIGMA_N_MULT			B43_PHY_N(0x0B3) /* Sigma N multiplier */
#define B43_NPHY_TXMACDELAY			B43_PHY_N(0x0B4) /* TX MAC delay */
#define B43_NPHY_TXFRAMEDELAY			B43_PHY_N(0x0B5) /* TX frame delay */
#define B43_NPHY_MLPARM				B43_PHY_N(0x0B6) /* ML parameters */
#define B43_NPHY_MLCTL				B43_PHY_N(0x0B7) /* ML control */
#define B43_NPHY_WWISE_20NCYCDAT		B43_PHY_N(0x0B8) /* WWiSE 20 N cyc data */
#define B43_NPHY_WWISE_40NCYCDAT		B43_PHY_N(0x0B9) /* WWiSE 40 N cyc data */
#define B43_NPHY_TGNSYNC_20NCYCDAT		B43_PHY_N(0x0BA) /* TGNsync 20 N cyc data */
#define B43_NPHY_TGNSYNC_40NCYCDAT		B43_PHY_N(0x0BB) /* TGNsync 40 N cyc data */
#define B43_NPHY_INITSWIZP			B43_PHY_N(0x0BC) /* Initial swizzle pattern */
#define B43_NPHY_TXTAILCNT			B43_PHY_N(0x0BD) /* TX tail count value */
#define B43_NPHY_BPHY_CTL1			B43_PHY_N(0x0BE) /* B PHY control 1 */
#define B43_NPHY_BPHY_CTL2			B43_PHY_N(0x0BF) /* B PHY control 2 */
#define  B43_NPHY_BPHY_CTL2_LUT			0x001F /* LUT index */
#define  B43_NPHY_BPHY_CTL2_LUT_SHIFT		0
#define  B43_NPHY_BPHY_CTL2_MACDEL		0x7FE0 /* MAC delay */
#define  B43_NPHY_BPHY_CTL2_MACDEL_SHIFT	5
#define B43_NPHY_IQLOCAL_CMD			B43_PHY_N(0x0C0) /* I/Q LO cal command */
#define  B43_NPHY_IQLOCAL_CMD_EN		0x8000
#define B43_NPHY_IQLOCAL_CMDNNUM		B43_PHY_N(0x0C1) /* I/Q LO cal command N num */
#define B43_NPHY_IQLOCAL_CMDGCTL		B43_PHY_N(0x0C2) /* I/Q LO cal command G control */
#define B43_NPHY_SAMP_CMD			B43_PHY_N(0x0C3) /* Sample command */
#define  B43_NPHY_SAMP_CMD_STOP			0x0002 /* Stop */
#define B43_NPHY_SAMP_LOOPCNT			B43_PHY_N(0x0C4) /* Sample loop count */
#define B43_NPHY_SAMP_WAITCNT			B43_PHY_N(0x0C5) /* Sample wait count */
#define B43_NPHY_SAMP_DEPCNT			B43_PHY_N(0x0C6) /* Sample depth count */
#define B43_NPHY_SAMP_STAT			B43_PHY_N(0x0C7) /* Sample status */
#define B43_NPHY_GPIO_LOOEN			B43_PHY_N(0x0C8) /* GPIO low out enable */
#define B43_NPHY_GPIO_HIOEN			B43_PHY_N(0x0C9) /* GPIO high out enable */
#define B43_NPHY_GPIO_SEL			B43_PHY_N(0x0CA) /* GPIO select */
#define B43_NPHY_GPIO_CLKCTL			B43_PHY_N(0x0CB) /* GPIO clock control */
#define B43_NPHY_TXF_20CO_AS0			B43_PHY_N(0x0CC) /* TX filter 20 coeff A stage 0 */
#define B43_NPHY_TXF_20CO_AS1			B43_PHY_N(0x0CD) /* TX filter 20 coeff A stage 1 */
#define B43_NPHY_TXF_20CO_AS2			B43_PHY_N(0x0CE) /* TX filter 20 coeff A stage 2 */
#define B43_NPHY_TXF_20CO_B32S0			B43_PHY_N(0x0CF) /* TX filter 20 coeff B32 stage 0 */
#define B43_NPHY_TXF_20CO_B1S0			B43_PHY_N(0x0D0) /* TX filter 20 coeff B1 stage 0 */
#define B43_NPHY_TXF_20CO_B32S1			B43_PHY_N(0x0D1) /* TX filter 20 coeff B32 stage 1 */
#define B43_NPHY_TXF_20CO_B1S1			B43_PHY_N(0x0D2) /* TX filter 20 coeff B1 stage 1 */
#define B43_NPHY_TXF_20CO_B32S2			B43_PHY_N(0x0D3) /* TX filter 20 coeff B32 stage 2 */
#define B43_NPHY_TXF_20CO_B1S2			B43_PHY_N(0x0D4) /* TX filter 20 coeff B1 stage 2 */
#define B43_NPHY_SIGFLDTOL			B43_PHY_N(0x0D5) /* Signal fld tolerance */
#define B43_NPHY_TXSERFLD			B43_PHY_N(0x0D6) /* TX service field */
#define B43_NPHY_AFESEQ_RX2TX_PUD		B43_PHY_N(0x0D7) /* AFE seq RX2TX power up/down delay */
#define B43_NPHY_AFESEQ_TX2RX_PUD		B43_PHY_N(0x0D8) /* AFE seq TX2RX power up/down delay */
#define B43_NPHY_TGNSYNC_SCRAMI0		B43_PHY_N(0x0D9) /* TGNsync scram init 0 */
#define B43_NPHY_TGNSYNC_SCRAMI1		B43_PHY_N(0x0DA) /* TGNsync scram init 1 */
#define B43_NPHY_INITSWIZPATTLEG		B43_PHY_N(0x0DB) /* Initial swizzle pattern leg */
#define B43_NPHY_BPHY_CTL3			B43_PHY_N(0x0DC) /* B PHY control 3 */
#define  B43_NPHY_BPHY_CTL3_SCALE		0x00FF /* Scale */
#define  B43_NPHY_BPHY_CTL3_SCALE_SHIFT		0
#define  B43_NPHY_BPHY_CTL3_FSC			0xFF00 /* Frame start count value */
#define  B43_NPHY_BPHY_CTL3_FSC_SHIFT		8
#define B43_NPHY_BPHY_CTL4			B43_PHY_N(0x0DD) /* B PHY control 4 */
#define B43_NPHY_C1_TXBBMULT			B43_PHY_N(0x0DE) /* Core 1 TX BB multiplier */
#define B43_NPHY_C2_TXBBMULT			B43_PHY_N(0x0DF) /* Core 2 TX BB multiplier */
#define B43_NPHY_TXF_40CO_AS0			B43_PHY_N(0x0E1) /* TX filter 40 coeff A stage 0 */
#define B43_NPHY_TXF_40CO_AS1			B43_PHY_N(0x0E2) /* TX filter 40 coeff A stage 1 */
#define B43_NPHY_TXF_40CO_AS2			B43_PHY_N(0x0E3) /* TX filter 40 coeff A stage 2 */
#define B43_NPHY_TXF_40CO_B32S0			B43_PHY_N(0x0E4) /* TX filter 40 coeff B32 stage 0 */
#define B43_NPHY_TXF_40CO_B1S0			B43_PHY_N(0x0E5) /* TX filter 40 coeff B1 stage 0 */
#define B43_NPHY_TXF_40CO_B32S1			B43_PHY_N(0x0E6) /* TX filter 40 coeff B32 stage 1 */
#define B43_NPHY_TXF_40CO_B1S1			B43_PHY_N(0x0E7) /* TX filter 40 coeff B1 stage 1 */
#define B43_NPHY_TXF_40CO_B32S2			B43_PHY_N(0x0E8) /* TX filter 40 coeff B32 stage 2 */
#define B43_NPHY_TXF_40CO_B1S2			B43_PHY_N(0x0E9) /* TX filter 40 coeff B1 stage 2 */
#define B43_NPHY_BIST_STAT2			B43_PHY_N(0x0EA) /* BIST status 2 */
#define B43_NPHY_BIST_STAT3			B43_PHY_N(0x0EB) /* BIST status 3 */
#define B43_NPHY_RFCTL_OVER			B43_PHY_N(0x0EC) /* RF control override */
#define B43_NPHY_MIMOCFG			B43_PHY_N(0x0ED) /* MIMO config */
#define  B43_NPHY_MIMOCFG_GFMIX			0x0004 /* Greenfield or mixed mode */
#define  B43_NPHY_MIMOCFG_AUTO			0x0100 /* Greenfield/mixed mode auto */
#define B43_NPHY_RADAR_BLNKCTL			B43_PHY_N(0x0EE) /* Radar blank control */
#define B43_NPHY_A0RADAR_FIFOCTL		B43_PHY_N(0x0EF) /* Antenna 0 radar FIFO control */
#define B43_NPHY_A1RADAR_FIFOCTL		B43_PHY_N(0x0F0) /* Antenna 1 radar FIFO control */
#define B43_NPHY_A0RADAR_FIFODAT		B43_PHY_N(0x0F1) /* Antenna 0 radar FIFO data */
#define B43_NPHY_A1RADAR_FIFODAT		B43_PHY_N(0x0F2) /* Antenna 1 radar FIFO data */
#define B43_NPHY_RADAR_THRES0			B43_PHY_N(0x0F3) /* Radar threshold 0 */
#define B43_NPHY_RADAR_THRES1			B43_PHY_N(0x0F4) /* Radar threshold 1 */
#define B43_NPHY_RADAR_THRES0R			B43_PHY_N(0x0F5) /* Radar threshold 0R */
#define B43_NPHY_RADAR_THRES1R			B43_PHY_N(0x0F6) /* Radar threshold 1R */
#define B43_NPHY_CSEN_20IN40_DLEN		B43_PHY_N(0x0F7) /* Carrier sense 20 in 40 dwell length */
#define B43_NPHY_RFCTL_LUT_TRSW_LO1		B43_PHY_N(0x0F8) /* RF control LUT TRSW lower 1 */
#define B43_NPHY_RFCTL_LUT_TRSW_UP1		B43_PHY_N(0x0F9) /* RF control LUT TRSW upper 1 */
#define B43_NPHY_RFCTL_LUT_TRSW_LO2		B43_PHY_N(0x0FA) /* RF control LUT TRSW lower 2 */
#define B43_NPHY_RFCTL_LUT_TRSW_UP2		B43_PHY_N(0x0FB) /* RF control LUT TRSW upper 2 */
#define B43_NPHY_RFCTL_LUT_TRSW_LO3		B43_PHY_N(0x0FC) /* RF control LUT TRSW lower 3 */
#define B43_NPHY_RFCTL_LUT_TRSW_UP3		B43_PHY_N(0x0FD) /* RF control LUT TRSW upper 3 */
#define B43_NPHY_RFCTL_LUT_TRSW_LO4		B43_PHY_N(0x0FE) /* RF control LUT TRSW lower 4 */
#define B43_NPHY_RFCTL_LUT_TRSW_UP4		B43_PHY_N(0x0FF) /* RF control LUT TRSW upper 4 */
#define B43_NPHY_RFCTL_LUT_LNAPA1		B43_PHY_N(0x100) /* RF control LUT LNA PA 1 */
#define B43_NPHY_RFCTL_LUT_LNAPA2		B43_PHY_N(0x101) /* RF control LUT LNA PA 2 */
#define B43_NPHY_RFCTL_LUT_LNAPA3		B43_PHY_N(0x102) /* RF control LUT LNA PA 3 */
#define B43_NPHY_RFCTL_LUT_LNAPA4		B43_PHY_N(0x103) /* RF control LUT LNA PA 4 */
#define B43_NPHY_TGNSYNC_CRCM0			B43_PHY_N(0x104) /* TGNsync CRC mask 0 */
#define B43_NPHY_TGNSYNC_CRCM1			B43_PHY_N(0x105) /* TGNsync CRC mask 1 */
#define B43_NPHY_TGNSYNC_CRCM2			B43_PHY_N(0x106) /* TGNsync CRC mask 2 */
#define B43_NPHY_TGNSYNC_CRCM3			B43_PHY_N(0x107) /* TGNsync CRC mask 3 */
#define B43_NPHY_TGNSYNC_CRCM4			B43_PHY_N(0x108) /* TGNsync CRC mask 4 */
#define B43_NPHY_CRCPOLY			B43_PHY_N(0x109) /* CRC polynomial */
#define B43_NPHY_SIGCNT				B43_PHY_N(0x10A) /* # sig count */
#define B43_NPHY_SIGSTARTBIT_CTL		B43_PHY_N(0x10B) /* Sig start bit control */
#define B43_NPHY_CRCPOLY_ORDER			B43_PHY_N(0x10C) /* CRC polynomial order */
#define B43_NPHY_RFCTL_CST0			B43_PHY_N(0x10D) /* RF control core swap table 0 */
#define B43_NPHY_RFCTL_CST1			B43_PHY_N(0x10E) /* RF control core swap table 1 */
#define B43_NPHY_RFCTL_CST2O			B43_PHY_N(0x10F) /* RF control core swap table 2 + others */
#define B43_NPHY_BPHY_CTL5			B43_PHY_N(0x111) /* B PHY control 5 */
#define B43_NPHY_RFSEQ_LPFBW			B43_PHY_N(0x112) /* RF seq LPF bandwidth */
#define B43_NPHY_TSSIBIAS1			B43_PHY_N(0x114) /* TSSI bias val 1 */
#define B43_NPHY_TSSIBIAS2			B43_PHY_N(0x115) /* TSSI bias val 2 */
#define  B43_NPHY_TSSIBIAS_BIAS			0x00FF /* Bias */
#define  B43_NPHY_TSSIBIAS_BIAS_SHIFT		0
#define  B43_NPHY_TSSIBIAS_VAL			0xFF00 /* Value */
#define  B43_NPHY_TSSIBIAS_VAL_SHIFT		8
#define B43_NPHY_ESTPWR1			B43_PHY_N(0x118) /* Estimated power 1 */
#define B43_NPHY_ESTPWR2			B43_PHY_N(0x119) /* Estimated power 2 */
#define  B43_NPHY_ESTPWR_PWR			0x00FF /* Estimated power */
#define  B43_NPHY_ESTPWR_PWR_SHIFT		0
#define  B43_NPHY_ESTPWR_VALID			0x0100 /* Estimated power valid */
#define B43_NPHY_TSSI_MAXTXFDT			B43_PHY_N(0x11C) /* TSSI max TX frame delay time */
#define  B43_NPHY_TSSI_MAXTXFDT_VAL		0x00FF /* max TX frame delay time */
#define  B43_NPHY_TSSI_MAXTXFDT_VAL_SHIFT	0
#define B43_NPHY_TSSI_MAXTDT			B43_PHY_N(0x11D) /* TSSI max TSSI delay time */
#define  B43_NPHY_TSSI_MAXTDT_VAL		0x00FF /* max TSSI delay time */
#define  B43_NPHY_TSSI_MAXTDT_VAL_SHIFT		0
#define B43_NPHY_ITSSI1				B43_PHY_N(0x11E) /* TSSI idle 1 */
#define B43_NPHY_ITSSI2				B43_PHY_N(0x11F) /* TSSI idle 2 */
#define  B43_NPHY_ITSSI_VAL			0x00FF /* Idle TSSI */
#define  B43_NPHY_ITSSI_VAL_SHIFT		0
#define B43_NPHY_TSSIMODE			B43_PHY_N(0x122) /* TSSI mode */
#define  B43_NPHY_TSSIMODE_EN			0x0001 /* TSSI enable */
#define  B43_NPHY_TSSIMODE_PDEN			0x0002 /* Power det enable */
#define B43_NPHY_RXMACIFM			B43_PHY_N(0x123) /* RX Macif mode */
#define B43_NPHY_CRSIT_COCNT_LO			B43_PHY_N(0x124) /* CRS idle time CRS-on count (low) */
#define B43_NPHY_CRSIT_COCNT_HI			B43_PHY_N(0x125) /* CRS idle time CRS-on count (high) */
#define B43_NPHY_CRSIT_MTCNT_LO			B43_PHY_N(0x126) /* CRS idle time measure time count (low) */
#define B43_NPHY_CRSIT_MTCNT_HI			B43_PHY_N(0x127) /* CRS idle time measure time count (high) */
#define B43_NPHY_SAMTWC				B43_PHY_N(0x128) /* Sample tail wait count */
#define B43_NPHY_IQEST_CMD			B43_PHY_N(0x129) /* I/Q estimate command */
#define  B43_NPHY_IQEST_CMD_START		0x0001 /* Start */
#define  B43_NPHY_IQEST_CMD_MODE		0x0002 /* Mode */
#define B43_NPHY_IQEST_WT			B43_PHY_N(0x12A) /* I/Q estimate wait time */
#define  B43_NPHY_IQEST_WT_VAL			0x00FF /* Wait time */
#define  B43_NPHY_IQEST_WT_VAL_SHIFT		0
#define B43_NPHY_IQEST_SAMCNT			B43_PHY_N(0x12B) /* I/Q estimate sample count */
#define B43_NPHY_IQEST_IQACC_LO0		B43_PHY_N(0x12C) /* I/Q estimate I/Q acc lo 0 */
#define B43_NPHY_IQEST_IQACC_HI0		B43_PHY_N(0x12D) /* I/Q estimate I/Q acc hi 0 */
#define B43_NPHY_IQEST_IPACC_LO0		B43_PHY_N(0x12E) /* I/Q estimate I power acc lo 0 */
#define B43_NPHY_IQEST_IPACC_HI0		B43_PHY_N(0x12F) /* I/Q estimate I power acc hi 0 */
#define B43_NPHY_IQEST_QPACC_LO0		B43_PHY_N(0x130) /* I/Q estimate Q power acc lo 0 */
#define B43_NPHY_IQEST_QPACC_HI0		B43_PHY_N(0x131) /* I/Q estimate Q power acc hi 0 */
#define B43_NPHY_IQEST_IQACC_LO1		B43_PHY_N(0x134) /* I/Q estimate I/Q acc lo 1 */
#define B43_NPHY_IQEST_IQACC_HI1		B43_PHY_N(0x135) /* I/Q estimate I/Q acc hi 1 */
#define B43_NPHY_IQEST_IPACC_LO1		B43_PHY_N(0x136) /* I/Q estimate I power acc lo 1 */
#define B43_NPHY_IQEST_IPACC_HI1		B43_PHY_N(0x137) /* I/Q estimate I power acc hi 1 */
#define B43_NPHY_IQEST_QPACC_LO1		B43_PHY_N(0x138) /* I/Q estimate Q power acc lo 1 */
#define B43_NPHY_IQEST_QPACC_HI1		B43_PHY_N(0x139) /* I/Q estimate Q power acc hi 1 */
#define B43_NPHY_MIMO_CRSTXEXT			B43_PHY_N(0x13A) /* MIMO PHY CRS TX extension */
#define B43_NPHY_PWRDET1			B43_PHY_N(0x13B) /* Power det 1 */
#define B43_NPHY_PWRDET2			B43_PHY_N(0x13C) /* Power det 2 */
#define B43_NPHY_MAXRSSI_DTIME			B43_PHY_N(0x13F) /* RSSI max RSSI delay time */
#define B43_NPHY_PIL_DW0			B43_PHY_N(0x141) /* Pilot data weight 0 */
#define B43_NPHY_PIL_DW1			B43_PHY_N(0x142) /* Pilot data weight 1 */
#define B43_NPHY_PIL_DW2			B43_PHY_N(0x143) /* Pilot data weight 2 */
#define  B43_NPHY_PIL_DW_BPSK			0x000F /* BPSK */
#define  B43_NPHY_PIL_DW_BPSK_SHIFT		0
#define  B43_NPHY_PIL_DW_QPSK			0x00F0 /* QPSK */
#define  B43_NPHY_PIL_DW_QPSK_SHIFT		4
#define  B43_NPHY_PIL_DW_16QAM			0x0F00 /* 16-QAM */
#define  B43_NPHY_PIL_DW_16QAM_SHIFT		8
#define  B43_NPHY_PIL_DW_64QAM			0xF000 /* 64-QAM */
#define  B43_NPHY_PIL_DW_64QAM_SHIFT		12
#define B43_NPHY_FMDEM_CFG			B43_PHY_N(0x144) /* FM demodulation config */
#define B43_NPHY_PHASETR_A0			B43_PHY_N(0x145) /* Phase track alpha 0 */
#define B43_NPHY_PHASETR_A1			B43_PHY_N(0x146) /* Phase track alpha 1 */
#define B43_NPHY_PHASETR_A2			B43_PHY_N(0x147) /* Phase track alpha 2 */
#define B43_NPHY_PHASETR_B0			B43_PHY_N(0x148) /* Phase track beta 0 */
#define B43_NPHY_PHASETR_B1			B43_PHY_N(0x149) /* Phase track beta 1 */
#define B43_NPHY_PHASETR_B2			B43_PHY_N(0x14A) /* Phase track beta 2 */
#define B43_NPHY_PHASETR_CHG0			B43_PHY_N(0x14B) /* Phase track change 0 */
#define B43_NPHY_PHASETR_CHG1			B43_PHY_N(0x14C) /* Phase track change 1 */
#define B43_NPHY_PHASETW_OFF			B43_PHY_N(0x14D) /* Phase track offset */
#define B43_NPHY_RFCTL_DBG			B43_PHY_N(0x14E) /* RF control debug */
#define B43_NPHY_CCK_SHIFTB_REF			B43_PHY_N(0x150) /* CCK shiftbits reference var */
#define B43_NPHY_OVER_DGAIN0			B43_PHY_N(0x152) /* Override digital gain 0 */
#define B43_NPHY_OVER_DGAIN1			B43_PHY_N(0x153) /* Override digital gain 1 */
#define  B43_NPHY_OVER_DGAIN_FDGV		0x0007 /* Force digital gain value */
#define  B43_NPHY_OVER_DGAIN_FDGV_SHIFT		0
#define  B43_NPHY_OVER_DGAIN_FDGEN		0x0008 /* Force digital gain enable */
#define  B43_NPHY_OVER_DGAIN_CCKDGECV		0xFF00 /* CCK digital gain enable count value */
#define  B43_NPHY_OVER_DGAIN_CCKDGECV_SHIFT	8
#define B43_NPHY_BIST_STAT4			B43_PHY_N(0x156) /* BIST status 4 */
#define B43_NPHY_RADAR_MAL			B43_PHY_N(0x157) /* Radar MA length */
#define B43_NPHY_RADAR_SRCCTL			B43_PHY_N(0x158) /* Radar search control */
#define B43_NPHY_VLD_DTSIG			B43_PHY_N(0x159) /* VLD data tones sig */
#define B43_NPHY_VLD_DTDAT			B43_PHY_N(0x15A) /* VLD data tones data */
#define B43_NPHY_C1_BPHY_RXIQCA0		B43_PHY_N(0x15B) /* Core 1 B PHY RX I/Q comp A0 */
#define B43_NPHY_C1_BPHY_RXIQCB0		B43_PHY_N(0x15C) /* Core 1 B PHY RX I/Q comp B0 */
#define B43_NPHY_C2_BPHY_RXIQCA1		B43_PHY_N(0x15D) /* Core 2 B PHY RX I/Q comp A1 */
#define B43_NPHY_C2_BPHY_RXIQCB1		B43_PHY_N(0x15E) /* Core 2 B PHY RX I/Q comp B1 */
#define B43_NPHY_FREQGAIN0			B43_PHY_N(0x160) /* Frequency gain 0 */
#define B43_NPHY_FREQGAIN1			B43_PHY_N(0x161) /* Frequency gain 1 */
#define B43_NPHY_FREQGAIN2			B43_PHY_N(0x162) /* Frequency gain 2 */
#define B43_NPHY_FREQGAIN3			B43_PHY_N(0x163) /* Frequency gain 3 */
#define B43_NPHY_FREQGAIN4			B43_PHY_N(0x164) /* Frequency gain 4 */
#define B43_NPHY_FREQGAIN5			B43_PHY_N(0x165) /* Frequency gain 5 */
#define B43_NPHY_FREQGAIN6			B43_PHY_N(0x166) /* Frequency gain 6 */
#define B43_NPHY_FREQGAIN7			B43_PHY_N(0x167) /* Frequency gain 7 */
#define B43_NPHY_FREQGAIN_BYPASS		B43_PHY_N(0x168) /* Frequency gain bypass */
#define B43_NPHY_TRLOSS				B43_PHY_N(0x169) /* TR loss value */
#define B43_NPHY_C1_ADCCLIP			B43_PHY_N(0x16A) /* Core 1 ADC clip */
#define B43_NPHY_C2_ADCCLIP			B43_PHY_N(0x16B) /* Core 2 ADC clip */
#define B43_NPHY_LTRN_OFFGAIN			B43_PHY_N(0x16F) /* LTRN offset gain */
#define B43_NPHY_LTRN_OFF			B43_PHY_N(0x170) /* LTRN offset */
#define B43_NPHY_NRDATAT_WWISE20SIG		B43_PHY_N(0x171) /* # data tones WWiSE 20 sig */
#define B43_NPHY_NRDATAT_WWISE40SIG		B43_PHY_N(0x172) /* # data tones WWiSE 40 sig */
#define B43_NPHY_NRDATAT_TGNSYNC20SIG		B43_PHY_N(0x173) /* # data tones TGNsync 20 sig */
#define B43_NPHY_NRDATAT_TGNSYNC40SIG		B43_PHY_N(0x174) /* # data tones TGNsync 40 sig */
#define B43_NPHY_WWISE_CRCM0			B43_PHY_N(0x175) /* WWiSE CRC mask 0 */
#define B43_NPHY_WWISE_CRCM1			B43_PHY_N(0x176) /* WWiSE CRC mask 1 */
#define B43_NPHY_WWISE_CRCM2			B43_PHY_N(0x177) /* WWiSE CRC mask 2 */
#define B43_NPHY_WWISE_CRCM3			B43_PHY_N(0x178) /* WWiSE CRC mask 3 */
#define B43_NPHY_WWISE_CRCM4			B43_PHY_N(0x179) /* WWiSE CRC mask 4 */
#define B43_NPHY_CHANEST_CDDSH			B43_PHY_N(0x17A) /* Channel estimate CDD shift */
#define B43_NPHY_HTAGC_WCNT			B43_PHY_N(0x17B) /* HT ADC wait counters */
#define B43_NPHY_SQPARM				B43_PHY_N(0x17C) /* SQ params */
#define B43_NPHY_MCSDUP6M			B43_PHY_N(0x17D) /* MCS dup 6M */
#define B43_NPHY_NDATAT_DUP40			B43_PHY_N(0x17E) /* # data tones dup 40 */
#define B43_NPHY_DUP40_TGNSYNC_CYCD		B43_PHY_N(0x17F) /* Dup40 TGNsync cycle data */
#define B43_NPHY_DUP40_GFBL			B43_PHY_N(0x180) /* Dup40 GF format BL address */
#define B43_NPHY_DUP40_BL			B43_PHY_N(0x181) /* Dup40 format BL address */
#define B43_NPHY_LEGDUP_FTA			B43_PHY_N(0x182) /* Legacy dup frm table address */
#define B43_NPHY_PACPROC_DBG			B43_PHY_N(0x183) /* Packet processing debug */
#define B43_NPHY_PIL_CYC1			B43_PHY_N(0x184) /* Pilot cycle counter 1 */
#define B43_NPHY_PIL_CYC2			B43_PHY_N(0x185) /* Pilot cycle counter 2 */
#define B43_NPHY_TXF_20CO_S0A1			B43_PHY_N(0x186) /* TX filter 20 coeff stage 0 A1 */
#define B43_NPHY_TXF_20CO_S0A2			B43_PHY_N(0x187) /* TX filter 20 coeff stage 0 A2 */
#define B43_NPHY_TXF_20CO_S1A1			B43_PHY_N(0x188) /* TX filter 20 coeff stage 1 A1 */
#define B43_NPHY_TXF_20CO_S1A2			B43_PHY_N(0x189) /* TX filter 20 coeff stage 1 A2 */
#define B43_NPHY_TXF_20CO_S2A1			B43_PHY_N(0x18A) /* TX filter 20 coeff stage 2 A1 */
#define B43_NPHY_TXF_20CO_S2A2			B43_PHY_N(0x18B) /* TX filter 20 coeff stage 2 A2 */
#define B43_NPHY_TXF_20CO_S0B1			B43_PHY_N(0x18C) /* TX filter 20 coeff stage 0 B1 */
#define B43_NPHY_TXF_20CO_S0B2			B43_PHY_N(0x18D) /* TX filter 20 coeff stage 0 B2 */
#define B43_NPHY_TXF_20CO_S0B3			B43_PHY_N(0x18E) /* TX filter 20 coeff stage 0 B3 */
#define B43_NPHY_TXF_20CO_S1B1			B43_PHY_N(0x18F) /* TX filter 20 coeff stage 1 B1 */
#define B43_NPHY_TXF_20CO_S1B2			B43_PHY_N(0x190) /* TX filter 20 coeff stage 1 B2 */
#define B43_NPHY_TXF_20CO_S1B3			B43_PHY_N(0x191) /* TX filter 20 coeff stage 1 B3 */
#define B43_NPHY_TXF_20CO_S2B1			B43_PHY_N(0x192) /* TX filter 20 coeff stage 2 B1 */
#define B43_NPHY_TXF_20CO_S2B2			B43_PHY_N(0x193) /* TX filter 20 coeff stage 2 B2 */
#define B43_NPHY_TXF_20CO_S2B3			B43_PHY_N(0x194) /* TX filter 20 coeff stage 2 B3 */
#define B43_NPHY_TXF_40CO_S0A1			B43_PHY_N(0x195) /* TX filter 40 coeff stage 0 A1 */
#define B43_NPHY_TXF_40CO_S0A2			B43_PHY_N(0x196) /* TX filter 40 coeff stage 0 A2 */
#define B43_NPHY_TXF_40CO_S1A1			B43_PHY_N(0x197) /* TX filter 40 coeff stage 1 A1 */
#define B43_NPHY_TXF_40CO_S1A2			B43_PHY_N(0x198) /* TX filter 40 coeff stage 1 A2 */
#define B43_NPHY_TXF_40CO_S2A1			B43_PHY_N(0x199) /* TX filter 40 coeff stage 2 A1 */
#define B43_NPHY_TXF_40CO_S2A2			B43_PHY_N(0x19A) /* TX filter 40 coeff stage 2 A2 */
#define B43_NPHY_TXF_40CO_S0B1			B43_PHY_N(0x19B) /* TX filter 40 coeff stage 0 B1 */
#define B43_NPHY_TXF_40CO_S0B2			B43_PHY_N(0x19C) /* TX filter 40 coeff stage 0 B2 */
#define B43_NPHY_TXF_40CO_S0B3			B43_PHY_N(0x19D) /* TX filter 40 coeff stage 0 B3 */
#define B43_NPHY_TXF_40CO_S1B1			B43_PHY_N(0x19E) /* TX filter 40 coeff stage 1 B1 */
#define B43_NPHY_TXF_40CO_S1B2			B43_PHY_N(0x19F) /* TX filter 40 coeff stage 1 B2 */
#define B43_NPHY_TXF_40CO_S1B3			B43_PHY_N(0x1A0) /* TX filter 40 coeff stage 1 B3 */
#define B43_NPHY_TXF_40CO_S2B1			B43_PHY_N(0x1A1) /* TX filter 40 coeff stage 2 B1 */
#define B43_NPHY_TXF_40CO_S2B2			B43_PHY_N(0x1A2) /* TX filter 40 coeff stage 2 B2 */
#define B43_NPHY_TXF_40CO_S2B3			B43_PHY_N(0x1A3) /* TX filter 40 coeff stage 2 B3 */
#define B43_NPHY_RSSIMC_0I_RSSI_X		B43_PHY_N(0x1A4) /* RSSI multiplication coefficient 0 I RSSI X */
#define B43_NPHY_RSSIMC_0I_RSSI_Y		B43_PHY_N(0x1A5) /* RSSI multiplication coefficient 0 I RSSI Y */
#define B43_NPHY_RSSIMC_0I_RSSI_Z		B43_PHY_N(0x1A6) /* RSSI multiplication coefficient 0 I RSSI Z */
#define B43_NPHY_RSSIMC_0I_TBD			B43_PHY_N(0x1A7) /* RSSI multiplication coefficient 0 I TBD */
#define B43_NPHY_RSSIMC_0I_PWRDET		B43_PHY_N(0x1A8) /* RSSI multiplication coefficient 0 I power det */
#define B43_NPHY_RSSIMC_0I_TSSI			B43_PHY_N(0x1A9) /* RSSI multiplication coefficient 0 I TSSI */
#define B43_NPHY_RSSIMC_0Q_RSSI_X		B43_PHY_N(0x1AA) /* RSSI multiplication coefficient 0 Q RSSI X */
#define B43_NPHY_RSSIMC_0Q_RSSI_Y		B43_PHY_N(0x1AB) /* RSSI multiplication coefficient 0 Q RSSI Y */
#define B43_NPHY_RSSIMC_0Q_RSSI_Z		B43_PHY_N(0x1AC) /* RSSI multiplication coefficient 0 Q RSSI Z */
#define B43_NPHY_RSSIMC_0Q_TBD			B43_PHY_N(0x1AD) /* RSSI multiplication coefficient 0 Q TBD */
#define B43_NPHY_RSSIMC_0Q_PWRDET		B43_PHY_N(0x1AE) /* RSSI multiplication coefficient 0 Q power det */
#define B43_NPHY_RSSIMC_0Q_TSSI			B43_PHY_N(0x1AF) /* RSSI multiplication coefficient 0 Q TSSI */
#define B43_NPHY_RSSIMC_1I_RSSI_X		B43_PHY_N(0x1B0) /* RSSI multiplication coefficient 1 I RSSI X */
#define B43_NPHY_RSSIMC_1I_RSSI_Y		B43_PHY_N(0x1B1) /* RSSI multiplication coefficient 1 I RSSI Y */
#define B43_NPHY_RSSIMC_1I_RSSI_Z		B43_PHY_N(0x1B2) /* RSSI multiplication coefficient 1 I RSSI Z */
#define B43_NPHY_RSSIMC_1I_TBD			B43_PHY_N(0x1B3) /* RSSI multiplication coefficient 1 I TBD */
#define B43_NPHY_RSSIMC_1I_PWRDET		B43_PHY_N(0x1B4) /* RSSI multiplication coefficient 1 I power det */
#define B43_NPHY_RSSIMC_1I_TSSI			B43_PHY_N(0x1B5) /* RSSI multiplication coefficient 1 I TSSI */
#define B43_NPHY_RSSIMC_1Q_RSSI_X		B43_PHY_N(0x1B6) /* RSSI multiplication coefficient 1 Q RSSI X */
#define B43_NPHY_RSSIMC_1Q_RSSI_Y		B43_PHY_N(0x1B7) /* RSSI multiplication coefficient 1 Q RSSI Y */
#define B43_NPHY_RSSIMC_1Q_RSSI_Z		B43_PHY_N(0x1B8) /* RSSI multiplication coefficient 1 Q RSSI Z */
#define B43_NPHY_RSSIMC_1Q_TBD			B43_PHY_N(0x1B9) /* RSSI multiplication coefficient 1 Q TBD */
#define B43_NPHY_RSSIMC_1Q_PWRDET		B43_PHY_N(0x1BA) /* RSSI multiplication coefficient 1 Q power det */
#define B43_NPHY_RSSIMC_1Q_TSSI			B43_PHY_N(0x1BB) /* RSSI multiplication coefficient 1 Q TSSI */
#define B43_NPHY_SAMC_WCNT			B43_PHY_N(0x1BC) /* Sample collect wait counter */
#define B43_NPHY_PTHROUGH_CNT			B43_PHY_N(0x1BD) /* Pass-through counter */
#define B43_NPHY_LTRN_OFF_G20L			B43_PHY_N(0x1C4) /* LTRN offset gain 20L */
#define B43_NPHY_LTRN_OFF_20L			B43_PHY_N(0x1C5) /* LTRN offset 20L */
#define B43_NPHY_LTRN_OFF_G20U			B43_PHY_N(0x1C6) /* LTRN offset gain 20U */
#define B43_NPHY_LTRN_OFF_20U			B43_PHY_N(0x1C7) /* LTRN offset 20U */
#define B43_NPHY_DSSSCCK_GAINSL			B43_PHY_N(0x1C8) /* DSSS/CCK gain settle length */
#define B43_NPHY_GPIO_LOOUT			B43_PHY_N(0x1C9) /* GPIO low out */
#define B43_NPHY_GPIO_HIOUT			B43_PHY_N(0x1CA) /* GPIO high out */
#define B43_NPHY_CRS_CHECK			B43_PHY_N(0x1CB) /* CRS check */
#define B43_NPHY_ML_LOGSS_RAT			B43_PHY_N(0x1CC) /* ML/logss ratio */
#define B43_NPHY_DUPSCALE			B43_PHY_N(0x1CD) /* Dup scale */
#define B43_NPHY_BW1A				B43_PHY_N(0x1CE) /* BW 1A */
#define B43_NPHY_BW2				B43_PHY_N(0x1CF) /* BW 2 */
#define B43_NPHY_BW3				B43_PHY_N(0x1D0) /* BW 3 */
#define B43_NPHY_BW4				B43_PHY_N(0x1D1) /* BW 4 */
#define B43_NPHY_BW5				B43_PHY_N(0x1D2) /* BW 5 */
#define B43_NPHY_BW6				B43_PHY_N(0x1D3) /* BW 6 */
#define B43_NPHY_COALEN0			B43_PHY_N(0x1D4) /* Coarse length 0 */
#define B43_NPHY_COALEN1			B43_PHY_N(0x1D5) /* Coarse length 1 */
#define B43_NPHY_CRSTHRES_1U			B43_PHY_N(0x1D6) /* CRS threshold 1 U */
#define B43_NPHY_CRSTHRES_2U			B43_PHY_N(0x1D7) /* CRS threshold 2 U */
#define B43_NPHY_CRSTHRES_3U			B43_PHY_N(0x1D8) /* CRS threshold 3 U */
#define B43_NPHY_CRSCTL_U			B43_PHY_N(0x1D9) /* CRS control U */
#define B43_NPHY_CRSTHRES_1L			B43_PHY_N(0x1DA) /* CRS threshold 1 L */
#define B43_NPHY_CRSTHRES_2L			B43_PHY_N(0x1DB) /* CRS threshold 2 L */
#define B43_NPHY_CRSTHRES_3L			B43_PHY_N(0x1DC) /* CRS threshold 3 L */
#define B43_NPHY_CRSCTL_L			B43_PHY_N(0x1DD) /* CRS control L */
#define B43_NPHY_STRA_1U			B43_PHY_N(0x1DE) /* STR address 1 U */
#define B43_NPHY_STRA_2U			B43_PHY_N(0x1DF) /* STR address 2 U */
#define B43_NPHY_STRA_1L			B43_PHY_N(0x1E0) /* STR address 1 L */
#define B43_NPHY_STRA_2L			B43_PHY_N(0x1E1) /* STR address 2 L */
#define B43_NPHY_CRSCHECK1			B43_PHY_N(0x1E2) /* CRS check 1 */
#define B43_NPHY_CRSCHECK2			B43_PHY_N(0x1E3) /* CRS check 2 */
#define B43_NPHY_CRSCHECK3			B43_PHY_N(0x1E4) /* CRS check 3 */
#define B43_NPHY_JMPSTP0			B43_PHY_N(0x1E5) /* Jump step 0 */
#define B43_NPHY_JMPSTP1			B43_PHY_N(0x1E6) /* Jump step 1 */
#define B43_NPHY_TXPCTL_CMD			B43_PHY_N(0x1E7) /* TX power control command */
#define  B43_NPHY_TXPCTL_CMD_INIT		0x007F /* Init */
#define  B43_NPHY_TXPCTL_CMD_INIT_SHIFT		0
#define  B43_NPHY_TXPCTL_CMD_COEFF		0x2000 /* Power control coefficients */
#define  B43_NPHY_TXPCTL_CMD_HWPCTLEN		0x4000 /* Hardware TX power control enable */
#define  B43_NPHY_TXPCTL_CMD_PCTLEN		0x8000 /* TX power control enable */
#define B43_NPHY_TXPCTL_N			B43_PHY_N(0x1E8) /* TX power control N num */
#define  B43_NPHY_TXPCTL_N_TSSID		0x00FF /* N TSSI delay */
#define  B43_NPHY_TXPCTL_N_TSSID_SHIFT		0
#define  B43_NPHY_TXPCTL_N_NPTIL2		0x0700 /* N PT integer log2 */
#define  B43_NPHY_TXPCTL_N_NPTIL2_SHIFT		8
#define B43_NPHY_TXPCTL_ITSSI			B43_PHY_N(0x1E9) /* TX power control idle TSSI */
#define  B43_NPHY_TXPCTL_ITSSI_0		0x003F /* Idle TSSI 0 */
#define  B43_NPHY_TXPCTL_ITSSI_0_SHIFT		0
#define  B43_NPHY_TXPCTL_ITSSI_1		0x3F00 /* Idle TSSI 1 */
#define  B43_NPHY_TXPCTL_ITSSI_1_SHIFT		8
#define  B43_NPHY_TXPCTL_ITSSI_BINF		0x8000 /* Raw TSSI offset bin format */
#define B43_NPHY_TXPCTL_TPWR			B43_PHY_N(0x1EA) /* TX power control target power */
#define  B43_NPHY_TXPCTL_TPWR_0			0x00FF /* Power 0 */
#define  B43_NPHY_TXPCTL_TPWR_0_SHIFT		0
#define  B43_NPHY_TXPCTL_TPWR_1			0xFF00 /* Power 1 */
#define  B43_NPHY_TXPCTL_TPWR_1_SHIFT		8
#define B43_NPHY_TXPCTL_BIDX			B43_PHY_N(0x1EB) /* TX power control base index */
#define  B43_NPHY_TXPCTL_BIDX_0			0x007F /* uC base index 0 */
#define  B43_NPHY_TXPCTL_BIDX_0_SHIFT		0
#define  B43_NPHY_TXPCTL_BIDX_1			0x7F00 /* uC base index 1 */
#define  B43_NPHY_TXPCTL_BIDX_1_SHIFT		8
#define  B43_NPHY_TXPCTL_BIDX_LOAD		0x8000 /* Load base index */
#define B43_NPHY_TXPCTL_PIDX			B43_PHY_N(0x1EC) /* TX power control power index */
#define  B43_NPHY_TXPCTL_PIDX_0			0x007F /* uC power index 0 */
#define  B43_NPHY_TXPCTL_PIDX_0_SHIFT		0
#define  B43_NPHY_TXPCTL_PIDX_1			0x7F00 /* uC power index 1 */
#define  B43_NPHY_TXPCTL_PIDX_1_SHIFT		8
#define B43_NPHY_C1_TXPCTL_STAT			B43_PHY_N(0x1ED) /* Core 1 TX power control status */
#define B43_NPHY_C2_TXPCTL_STAT			B43_PHY_N(0x1EE) /* Core 2 TX power control status */
#define  B43_NPHY_TXPCTL_STAT_EST		0x00FF /* Estimated power */
#define  B43_NPHY_TXPCTL_STAT_EST_SHIFT		0
#define  B43_NPHY_TXPCTL_STAT_BIDX		0x7F00 /* Base index */
#define  B43_NPHY_TXPCTL_STAT_BIDX_SHIFT	8
#define  B43_NPHY_TXPCTL_STAT_ESTVALID		0x8000 /* Estimated power valid */
#define B43_NPHY_SMALLSGS_LEN			B43_PHY_N(0x1EF) /* Small sig gain settle length */
#define B43_NPHY_PHYSTAT_GAIN0			B43_PHY_N(0x1F0) /* PHY stats gain info 0 */
#define B43_NPHY_PHYSTAT_GAIN1			B43_PHY_N(0x1F1) /* PHY stats gain info 1 */
#define B43_NPHY_PHYSTAT_FREQEST		B43_PHY_N(0x1F2) /* PHY stats frequency estimate */
#define B43_NPHY_PHYSTAT_ADVRET			B43_PHY_N(0x1F3) /* PHY stats ADV retard */
#define B43_NPHY_PHYLB_MODE			B43_PHY_N(0x1F4) /* PHY loopback mode */
#define B43_NPHY_TONE_MIDX20_1			B43_PHY_N(0x1F5) /* Tone map index 20/1 */
#define B43_NPHY_TONE_MIDX20_2			B43_PHY_N(0x1F6) /* Tone map index 20/2 */
#define B43_NPHY_TONE_MIDX20_3			B43_PHY_N(0x1F7) /* Tone map index 20/3 */
#define B43_NPHY_TONE_MIDX40_1			B43_PHY_N(0x1F8) /* Tone map index 40/1 */
#define B43_NPHY_TONE_MIDX40_2			B43_PHY_N(0x1F9) /* Tone map index 40/2 */
#define B43_NPHY_TONE_MIDX40_3			B43_PHY_N(0x1FA) /* Tone map index 40/3 */
#define B43_NPHY_TONE_MIDX40_4			B43_PHY_N(0x1FB) /* Tone map index 40/4 */
#define B43_NPHY_PILTONE_MIDX1			B43_PHY_N(0x1FC) /* Pilot tone map index 1 */
#define B43_NPHY_PILTONE_MIDX2			B43_PHY_N(0x1FD) /* Pilot tone map index 2 */
#define B43_NPHY_PILTONE_MIDX3			B43_PHY_N(0x1FE) /* Pilot tone map index 3 */
#define B43_NPHY_TXRIFS_FRDEL			B43_PHY_N(0x1FF) /* TX RIFS frame delay */
#define B43_NPHY_AFESEQ_RX2TX_PUD_40M		B43_PHY_N(0x200) /* AFE seq rx2tx power up/down delay 40M */
#define B43_NPHY_AFESEQ_TX2RX_PUD_40M		B43_PHY_N(0x201) /* AFE seq tx2rx power up/down delay 40M */
#define B43_NPHY_AFESEQ_RX2TX_PUD_20M		B43_PHY_N(0x202) /* AFE seq rx2tx power up/down delay 20M */
#define B43_NPHY_AFESEQ_TX2RX_PUD_20M		B43_PHY_N(0x203) /* AFE seq tx2rx power up/down delay 20M */
#define B43_NPHY_RX_SIGCTL			B43_PHY_N(0x204) /* RX signal control */
#define B43_NPHY_RXPIL_CYCNT0			B43_PHY_N(0x205) /* RX pilot cycle counter 0 */
#define B43_NPHY_RXPIL_CYCNT1			B43_PHY_N(0x206) /* RX pilot cycle counter 1 */
#define B43_NPHY_RXPIL_CYCNT2			B43_PHY_N(0x207) /* RX pilot cycle counter 2 */
#define B43_NPHY_AFESEQ_RX2TX_PUD_10M		B43_PHY_N(0x208) /* AFE seq rx2tx power up/down delay 10M */
#define B43_NPHY_AFESEQ_TX2RX_PUD_10M		B43_PHY_N(0x209) /* AFE seq tx2rx power up/down delay 10M */
#define B43_NPHY_DSSSCCK_CRSEXTL		B43_PHY_N(0x20A) /* DSSS/CCK CRS extension length */
#define B43_NPHY_ML_LOGSS_RATSLOPE		B43_PHY_N(0x20B) /* ML/logss ratio slope */
#define B43_NPHY_RIFS_SRCTL			B43_PHY_N(0x20C) /* RIFS search timeout length */
#define B43_NPHY_TXREALFD			B43_PHY_N(0x20D) /* TX real frame delay */
#define B43_NPHY_HPANT_SWTHRES			B43_PHY_N(0x20E) /* High power antenna switch threshold */
#define B43_NPHY_EDCRS_ASSTHRES0		B43_PHY_N(0x210) /* ED CRS assert threshold 0 */
#define B43_NPHY_EDCRS_ASSTHRES1		B43_PHY_N(0x211) /* ED CRS assert threshold 1 */
#define B43_NPHY_EDCRS_DEASSTHRES0		B43_PHY_N(0x212) /* ED CRS deassert threshold 0 */
#define B43_NPHY_EDCRS_DEASSTHRES1		B43_PHY_N(0x213) /* ED CRS deassert threshold 1 */
#define B43_NPHY_STR_WTIME20U			B43_PHY_N(0x214) /* STR wait time 20U */
#define B43_NPHY_STR_WTIME20L			B43_PHY_N(0x215) /* STR wait time 20L */
#define B43_NPHY_TONE_MIDX657M			B43_PHY_N(0x216) /* Tone map index 657M */
#define B43_NPHY_HTSIGTONES			B43_PHY_N(0x217) /* HT signal tones */
#define B43_NPHY_RSSI1				B43_PHY_N(0x219) /* RSSI value 1 */
#define B43_NPHY_RSSI2				B43_PHY_N(0x21A) /* RSSI value 2 */
#define B43_NPHY_CHAN_ESTHANG			B43_PHY_N(0x21D) /* Channel estimate hang */
#define B43_NPHY_FINERX2_CGC			B43_PHY_N(0x221) /* Fine RX 2 clock gate control */
#define  B43_NPHY_FINERX2_CGC_DECGC		0x0008 /* Decode gated clocks */
#define B43_NPHY_TXPCTL_INIT			B43_PHY_N(0x222) /* TX power control init */
#define  B43_NPHY_TXPCTL_INIT_PIDXI1		0x00FF /* Power index init 1 */
#define  B43_NPHY_TXPCTL_INIT_PIDXI1_SHIFT	0
#define B43_NPHY_PAPD_EN0			B43_PHY_N(0x297) /* PAPD Enable0 TBD */
#define B43_NPHY_EPS_TABLE_ADJ0			B43_PHY_N(0x298) /* EPS Table Adj0 TBD */
#define B43_NPHY_PAPD_EN1			B43_PHY_N(0x29B) /* PAPD Enable1 TBD */
#define B43_NPHY_EPS_TABLE_ADJ1			B43_PHY_N(0x29C) /* EPS Table Adj1 TBD */

#define B43_PHY_B_BBCFG				B43_PHY_N_BMODE(0x001) /* BB config */
#define B43_PHY_B_TEST				B43_PHY_N_BMODE(0x00A)

struct b43_wldev;

enum b43_nphy_spur_avoid {
	B43_SPUR_AVOID_DISABLE,
	B43_SPUR_AVOID_AUTO,
	B43_SPUR_AVOID_FORCE,
};

struct b43_chanspec {
	u16 center_freq;
	enum nl80211_channel_type channel_type;
};

struct b43_phy_n_iq_comp {
	s16 a0;
	s16 b0;
	s16 a1;
	s16 b1;
};

struct b43_phy_n_rssical_cache {
	u16 rssical_radio_regs_2G[2];
	u16 rssical_phy_regs_2G[12];

	u16 rssical_radio_regs_5G[2];
	u16 rssical_phy_regs_5G[12];
};

struct b43_phy_n_cal_cache {
	u16 txcal_radio_regs_2G[8];
	u16 txcal_coeffs_2G[8];
	struct b43_phy_n_iq_comp rxcal_coeffs_2G;

	u16 txcal_radio_regs_5G[8];
	u16 txcal_coeffs_5G[8];
	struct b43_phy_n_iq_comp rxcal_coeffs_5G;
};

struct b43_phy_n_txpwrindex {
	s8 index;
	s8 index_internal;
	s8 index_internal_save;
	u16 AfectrlOverride;
	u16 AfeCtrlDacGain;
	u16 rad_gain;
	u8 bbmult;
	u16 iqcomp_a;
	u16 iqcomp_b;
	u16 locomp;
};

struct b43_phy_n_pwr_ctl_info {
	u8 idle_tssi_2g;
	u8 idle_tssi_5g;
};

struct b43_phy_n {
	u8 antsel_type;
	u8 cal_orig_pwr_idx[2];
	u8 measure_hold;
	u8 phyrxchain;
	u8 hw_phyrxchain;
	u8 hw_phytxchain;
	u8 perical;
	u32 deaf_count;
	u32 rxcalparams;
	bool hang_avoid;
	bool mute;
	u16 papd_epsilon_offset[2];
	s32 preamble_override;
	u32 bb_mult_save;

	bool gain_boost;
	bool elna_gain_config;
	bool band5g_pwrgain;

	u8 mphase_cal_phase_id;
	u16 mphase_txcal_cmdidx;
	u16 mphase_txcal_numcmds;
	u16 mphase_txcal_bestcoeffs[11];

	bool txpwrctrl;
	bool pwg_gain_5ghz;
	u8 tx_pwr_idx[2];
	u16 adj_pwr_tbl[84];
	u16 txcal_bbmult;
	u16 txiqlocal_bestc[11];
	bool txiqlocal_coeffsvalid;
	struct b43_phy_n_txpwrindex txpwrindex[2];
	struct b43_phy_n_pwr_ctl_info pwr_ctl_info[2];
	struct b43_chanspec txiqlocal_chanspec;

	u8 txrx_chain;
	u16 tx_rx_cal_phy_saveregs[11];
	u16 tx_rx_cal_radio_saveregs[22];

	u16 rfctrl_intc1_save;
	u16 rfctrl_intc2_save;

	u16 classifier_state;
	u16 clip_state[2];

	enum b43_nphy_spur_avoid spur_avoid;
	bool aband_spurwar_en;
	bool gband_spurwar_en;

	bool ipa2g_on;
	struct b43_chanspec iqcal_chanspec_2G;
	struct b43_chanspec rssical_chanspec_2G;

	bool ipa5g_on;
	struct b43_chanspec iqcal_chanspec_5G;
	struct b43_chanspec rssical_chanspec_5G;

	struct b43_phy_n_rssical_cache rssical_cache;
	struct b43_phy_n_cal_cache cal_cache;
	bool crsminpwr_adjusted;
	bool noisevars_adjusted;
};


struct b43_phy_operations;
extern const struct b43_phy_operations b43_phyops_n;

#endif /* B43_NPHY_H_ */
