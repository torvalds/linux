/*
 * Copyright (c) 2007-2008 Atheros Communications Inc.
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */
#include "../80211core/cprecomp.h"

typedef struct {
	u32_t	ackrcv_bad;
	u32_t	rts_bad;
	u32_t	rts_good;
	u32_t	fcs_bad;
	u32_t	beacons;
} ZM_HAL_MIB_STATS;

/*
 * Per-node statistics maintained by the driver for use in
 * optimizing signal quality and other operational aspects.
 */
typedef struct {
	u32_t	ns_avgbrssi;	/* average beacon rssi */
	u32_t	ns_avgrssi;	    /* average data rssi */
	u32_t	ns_avgtxrssi;	/* average tx rssi */
} ZM_HAL_NODE_STATS;

#define	ZM_HAL_RSSI_EP_MULTIPLIER	(1<<7)	/* pow2 to optimize out * and / */

struct zsAniStats {
    u32_t   ast_ani_niup;   /* ANI increased noise immunity */
    u32_t   ast_ani_nidown; /* ANI decreased noise immunity */
    u32_t   ast_ani_spurup; /* ANI increased spur immunity */
    u32_t   ast_ani_spurdown;/* ANI descreased spur immunity */
    u32_t   ast_ani_ofdmon; /* ANI OFDM weak signal detect on */
    u32_t   ast_ani_ofdmoff;/* ANI OFDM weak signal detect off */
    u32_t   ast_ani_cckhigh;/* ANI CCK weak signal threshold high */
    u32_t   ast_ani_ccklow; /* ANI CCK weak signal threshold low */
    u32_t   ast_ani_stepup; /* ANI increased first step level */
    u32_t   ast_ani_stepdown;/* ANI decreased first step level */
    u32_t   ast_ani_ofdmerrs;/* ANI cumulative ofdm phy err count */
    u32_t   ast_ani_cckerrs;/* ANI cumulative cck phy err count */
    u32_t   ast_ani_reset;  /* ANI parameters zero'd for non-STA */
    u32_t   ast_ani_lzero;  /* ANI listen time forced to zero */
    u32_t   ast_ani_lneg;   /* ANI listen time calculated < 0 */
    ZM_HAL_MIB_STATS   ast_mibstats;   /* MIB counter stats */
    ZM_HAL_NODE_STATS  ast_nodestats;  /* Latest rssi stats from driver */
};

/*
 * Per-channel ANI state private to the driver.
 */
struct zsAniState {
    ZM_HAL_CHANNEL c;
    u8_t    noiseImmunityLevel;
    u8_t    spurImmunityLevel;
    u8_t    firstepLevel;
    u8_t    ofdmWeakSigDetectOff;
    u8_t    cckWeakSigThreshold;

    /* Thresholds */
    u32_t   listenTime;
    u32_t   ofdmTrigHigh;
    u32_t   ofdmTrigLow;
    s32_t   cckTrigHigh;
    s32_t   cckTrigLow;
    s32_t   rssiThrLow;
    s32_t   rssiThrHigh;

    u32_t   noiseFloor; /* The current noise floor */
    u32_t   txFrameCount;   /* Last txFrameCount */
    u32_t   rxFrameCount;   /* Last rx Frame count */
    u32_t   cycleCount; /* Last cycleCount (can detect wrap-around) */
    u32_t   ofdmPhyErrCount;/* OFDM err count since last reset */
    u32_t   cckPhyErrCount; /* CCK err count since last reset */
    u32_t   ofdmPhyErrBase; /* Base value for ofdm err counter */
    u32_t   cckPhyErrBase;  /* Base value for cck err counters */
    s16_t   pktRssi[2]; /* Average rssi of pkts for 2 antennas */
    s16_t   ofdmErrRssi[2]; /* Average rssi of ofdm phy errs for 2 ant */
    s16_t   cckErrRssi[2];  /* Average rssi of cck phy errs for 2 ant */
};

typedef enum {
	ZM_HAL_ANI_PRESENT,			/* is ANI support present */
	ZM_HAL_ANI_NOISE_IMMUNITY_LEVEL,		/* set level */
	ZM_HAL_ANI_OFDM_WEAK_SIGNAL_DETECTION,	/* enable/disable */
	ZM_HAL_ANI_CCK_WEAK_SIGNAL_THR,		/* enable/disable */
	ZM_HAL_ANI_FIRSTEP_LEVEL,			/* set level */
	ZM_HAL_ANI_SPUR_IMMUNITY_LEVEL,		/* set level */
	ZM_HAL_ANI_MODE,				/* 0 => manual, 1 => auto */
	ZM_HAL_ANI_PHYERR_RESET,			/* reset phy error stats */
} ZM_HAL_ANI_CMD;

#define AR_PHY_COUNTMAX		(3 << 22)	/* Max counted before intr */
#define ZM_HAL_PROCESS_ANI	0x00000001	/* ANI state setup */
#define ZM_RSSI_DUMMY_MARKER	0x127

/* PHY registers in ar5416, related base and register offsets
   may need to be changed in otus BB */
#define AR_PHY_BASE     0x1C5800      /* base address of phy regs */
#define AR_PHY(_n)      (AR_PHY_BASE + ((_n)<<2))

#define AR_PHY_TEST             0x1C5800          /* PHY test control */
#define PHY_AGC_CLR             0x10000000      /* disable AGC to A2 */
#define RFSILENT_BB             0x00002000      /* shush bb */

#define AR_PHY_TURBO        0x1C5804      /* frame control register */
#define AR_PHY_FC_TURBO_MODE        0x00000001  /* Set turbo mode bits */
#define AR_PHY_FC_TURBO_SHORT       0x00000002  /* Set short symbols to turbo mode setting */
#define AR_PHY_FC_DYN2040_EN        0x00000004      /* Enable dyn 20/40 mode */
#define AR_PHY_FC_DYN2040_PRI_ONLY      0x00000008      /* dyn 20/40 - primary only */
#define AR_PHY_FC_DYN2040_PRI_CH    0x00000010      /* dyn 20/40 - primary ch offset (0=+10MHz, 1=-10MHz)*/
#define AR_PHY_FC_DYN2040_EXT_CH        0x00000020      /* dyn 20/40 - ext ch spacing (0=20MHz/ 1=25MHz) */
#define AR_PHY_FC_HT_EN             0x00000040      /* ht enable */
#define AR_PHY_FC_SHORT_GI_40       0x00000080      /* allow short GI for HT 40 */
#define AR_PHY_FC_WALSH             0x00000100      /* walsh spatial spreading for 2 chains,2 streams TX */
#define AR_PHY_FC_SINGLE_HT_LTF1        0x00000200      /* single length (4us) 1st HT long training symbol */

#define AR_PHY_TIMING2      0x1C5810      /* Timing Control 2 */
#define AR_PHY_TIMING2_USE_FORCE    0x00001000
#define AR_PHY_TIMING2_FORCE_VAL    0x00000fff

#define AR_PHY_TIMING3      0x1C5814      /* Timing control 3 */
#define AR_PHY_TIMING3_DSC_MAN  0xFFFE0000
#define AR_PHY_TIMING3_DSC_MAN_S    17
#define AR_PHY_TIMING3_DSC_EXP  0x0001E000
#define AR_PHY_TIMING3_DSC_EXP_S    13

#define AR_PHY_CHIP_ID          0x1C5818      /* PHY chip revision ID */
#define AR_PHY_CHIP_ID_REV_0    0x80        /* 5416 Rev 0 (owl 1.0) BB */
#define AR_PHY_CHIP_ID_REV_1    0x81        /* 5416 Rev 1 (owl 2.0) BB */

#define AR_PHY_ACTIVE       0x1C581C      /* activation register */
#define AR_PHY_ACTIVE_EN    0x00000001  /* Activate PHY chips */
#define AR_PHY_ACTIVE_DIS   0x00000000  /* Deactivate PHY chips */

#define AR_PHY_RF_CTL2                      0x1C5824
#define AR_PHY_TX_END_DATA_START  0x000000FF
#define AR_PHY_TX_END_DATA_START_S  0
#define AR_PHY_TX_END_PA_ON       0x0000FF00
#define AR_PHY_TX_END_PA_ON_S       8


#define AR_PHY_RF_CTL3                  0x1C5828
#define AR_PHY_TX_END_TO_A2_RX_ON       0x00FF0000
#define AR_PHY_TX_END_TO_A2_RX_ON_S     16

#define AR_PHY_ADC_CTL      0x1C582C
#define AR_PHY_ADC_CTL_OFF_INBUFGAIN    0x00000003
#define AR_PHY_ADC_CTL_OFF_INBUFGAIN_S  0
#define AR_PHY_ADC_CTL_OFF_PWDDAC   0x00002000
#define AR_PHY_ADC_CTL_OFF_PWDBANDGAP   0x00004000 /* BB Rev 4.2+ only */
#define AR_PHY_ADC_CTL_OFF_PWDADC   0x00008000 /* BB Rev 4.2+ only */
#define AR_PHY_ADC_CTL_ON_INBUFGAIN 0x00030000
#define AR_PHY_ADC_CTL_ON_INBUFGAIN_S   16

#define AR_PHY_ADC_SERIAL_CTL       0x1C5830
#define AR_PHY_SEL_INTERNAL_ADDAC   0x00000000
#define AR_PHY_SEL_EXTERNAL_RADIO   0x00000001

#define AR_PHY_RF_CTL4                    0x1C5834
#define AR_PHY_RF_CTL4_TX_END_XPAB_OFF    0xFF000000
#define AR_PHY_RF_CTL4_TX_END_XPAB_OFF_S  24
#define AR_PHY_RF_CTL4_TX_END_XPAA_OFF    0x00FF0000
#define AR_PHY_RF_CTL4_TX_END_XPAA_OFF_S  16
#define AR_PHY_RF_CTL4_FRAME_XPAB_ON      0x0000FF00
#define AR_PHY_RF_CTL4_FRAME_XPAB_ON_S    8
#define AR_PHY_RF_CTL4_FRAME_XPAA_ON      0x000000FF
#define AR_PHY_RF_CTL4_FRAME_XPAA_ON_S    0

#define AR_PHY_SETTLING     0x1C5844
#define AR_PHY_SETTLING_SWITCH  0x00003F80
#define AR_PHY_SETTLING_SWITCH_S    7

#define AR_PHY_RXGAIN       0x1C5848
#define AR_PHY_RXGAIN_TXRX_ATTEN    0x0003F000
#define AR_PHY_RXGAIN_TXRX_ATTEN_S  12
#define AR_PHY_RXGAIN_TXRX_RF_MAX   0x007C0000
#define AR_PHY_RXGAIN_TXRX_RF_MAX_S 18

#define AR_PHY_DESIRED_SZ   0x1C5850
#define AR_PHY_DESIRED_SZ_ADC       0x000000FF
#define AR_PHY_DESIRED_SZ_ADC_S     0
#define AR_PHY_DESIRED_SZ_PGA       0x0000FF00
#define AR_PHY_DESIRED_SZ_PGA_S     8
#define AR_PHY_DESIRED_SZ_TOT_DES   0x0FF00000
#define AR_PHY_DESIRED_SZ_TOT_DES_S 20

#define AR_PHY_FIND_SIG      0x1C5858
#define AR_PHY_FIND_SIG_FIRSTEP  0x0003F000
#define AR_PHY_FIND_SIG_FIRSTEP_S        12
#define AR_PHY_FIND_SIG_FIRPWR   0x03FC0000
#define AR_PHY_FIND_SIG_FIRPWR_S         18

#define AR_PHY_AGC_CTL1      0x1C585C
#define AR_PHY_AGC_CTL1_COARSE_LOW       0x00007F80
#define AR_PHY_AGC_CTL1_COARSE_LOW_S         7
#define AR_PHY_AGC_CTL1_COARSE_HIGH      0x003F8000
#define AR_PHY_AGC_CTL1_COARSE_HIGH_S        15

#define AR_PHY_AGC_CONTROL  0x1C5860      /* chip calibration and noise floor setting */
#define AR_PHY_AGC_CONTROL_CAL  0x00000001  /* do internal calibration */
#define AR_PHY_AGC_CONTROL_NF   0x00000002  /* do noise-floor calculation */

#define AR_PHY_CCA              0x1C5864
#define AR_PHY_MINCCA_PWR       0x1FF00000
#define AR_PHY_MINCCA_PWR_S     19
#define AR_PHY_CCA_THRESH62     0x0007F000
#define AR_PHY_CCA_THRESH62_S   12

#define AR_PHY_SFCORR_LOW    0x1C586C
#define AR_PHY_SFCORR_LOW_USE_SELF_CORR_LOW  0x00000001
#define AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW    0x00003F00
#define AR_PHY_SFCORR_LOW_M2COUNT_THR_LOW_S  8
#define AR_PHY_SFCORR_LOW_M1_THRESH_LOW  0x001FC000
#define AR_PHY_SFCORR_LOW_M1_THRESH_LOW_S    14
#define AR_PHY_SFCORR_LOW_M2_THRESH_LOW  0x0FE00000
#define AR_PHY_SFCORR_LOW_M2_THRESH_LOW_S    21

#define AR_PHY_SFCORR       0x1C5868
#define AR_PHY_SFCORR_M2COUNT_THR    0x0000001F
#define AR_PHY_SFCORR_M2COUNT_THR_S  0
#define AR_PHY_SFCORR_M1_THRESH  0x00FE0000
#define AR_PHY_SFCORR_M1_THRESH_S    17
#define AR_PHY_SFCORR_M2_THRESH  0x7F000000
#define AR_PHY_SFCORR_M2_THRESH_S    24

#define AR_PHY_SLEEP_CTR_CONTROL    0x1C5870
#define AR_PHY_SLEEP_CTR_LIMIT      0x1C5874
#define AR_PHY_SLEEP_SCAL       0x1C5878

#define AR_PHY_PLL_CTL          0x1C587c      /* PLL control register */
#define AR_PHY_PLL_CTL_40       0xaa        /* 40 MHz */
#define AR_PHY_PLL_CTL_40_5413  0x04
#define AR_PHY_PLL_CTL_44       0xab        /* 44 MHz for 11b, 11g */
#define AR_PHY_PLL_CTL_44_2133  0xeb        /* 44 MHz for 11b, 11g */
#define AR_PHY_PLL_CTL_40_2133  0xea        /* 40 MHz for 11a, turbos */

#define AR_PHY_RX_DELAY     0x1C5914      /* analog pow-on time (100ns) */
#define AR_PHY_RX_DELAY_DELAY   0x00003FFF  /* delay from wakeup to rx ena */

#define AR_PHY_TIMING_CTRL4     0x1C5920      /* timing control */
#define AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF 0x01F   /* Mask for kcos_theta-1 for q correction */
#define AR_PHY_TIMING_CTRL4_IQCORR_Q_Q_COFF_S   0   /* shift for Q_COFF */
#define AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF 0x7E0   /* Mask for sin_theta for i correction */
#define AR_PHY_TIMING_CTRL4_IQCORR_Q_I_COFF_S   5   /* Shift for sin_theta for i correction */
#define AR_PHY_TIMING_CTRL4_IQCORR_ENABLE   0x800   /* enable IQ correction */
#define AR_PHY_TIMING_CTRL4_IQCAL_LOG_COUNT_MAX 0xF000  /* Mask for max number of samples (logarithmic) */
#define AR_PHY_TIMING_CTRL4_IQCAL_LOG_COUNT_MAX_S   12  /* Shift for max number of samples */
#define AR_PHY_TIMING_CTRL4_DO_IQCAL    0x10000     /* perform IQ calibration */

#define AR_PHY_TIMING5      0x1C5924
#define AR_PHY_TIMING5_CYCPWR_THR1  0x000000FE
#define AR_PHY_TIMING5_CYCPWR_THR1_S    1

#define AR_PHY_POWER_TX_RATE1   0x1C5934
#define AR_PHY_POWER_TX_RATE2   0x1C5938
#define AR_PHY_POWER_TX_RATE_MAX    0x1C593c
#define AR_PHY_POWER_TX_RATE_MAX_TPC_ENABLE 0x00000040

#define AR_PHY_FRAME_CTL    0x1C5944
#define AR_PHY_FRAME_CTL_TX_CLIP    0x00000038
#define AR_PHY_FRAME_CTL_TX_CLIP_S  3

#define AR_PHY_TXPWRADJ     0x1C594C      /* BB Rev 4.2+ only */
#define AR_PHY_TXPWRADJ_CCK_GAIN_DELTA  0x00000FC0
#define AR_PHY_TXPWRADJ_CCK_GAIN_DELTA_S    6
#define AR_PHY_TXPWRADJ_CCK_PCDAC_INDEX 0x00FC0000
#define AR_PHY_TXPWRADJ_CCK_PCDAC_INDEX_S   18

#define AR_PHY_RADAR_0      0x1C5954      /* radar detection settings */
#define AR_PHY_RADAR_0_ENA  0x00000001  /* Enable radar detection */
#define AR_PHY_RADAR_0_INBAND   0x0000003e  /* Inband pulse threshold */
#define AR_PHY_RADAR_0_INBAND_S 1
#define AR_PHY_RADAR_0_PRSSI    0x00000FC0  /* Pulse rssi threshold */
#define AR_PHY_RADAR_0_PRSSI_S  6
#define AR_PHY_RADAR_0_HEIGHT   0x0003F000  /* Pulse height threshold */
#define AR_PHY_RADAR_0_HEIGHT_S 12
#define AR_PHY_RADAR_0_RRSSI    0x00FC0000  /* Radar rssi threshold */
#define AR_PHY_RADAR_0_RRSSI_S  18
#define AR_PHY_RADAR_0_FIRPWR   0x7F000000  /* Radar firpwr threshold */
#define AR_PHY_RADAR_0_FIRPWR_S 24

#define AR_PHY_SWITCH_CHAIN_0     0x1C5960
#define AR_PHY_SWITCH_COM         0x1C5964

#define AR_PHY_SIGMA_DELTA  0x1C596C      /* AR5312 only */
#define AR_PHY_SIGMA_DELTA_ADC_SEL  0x00000003
#define AR_PHY_SIGMA_DELTA_ADC_SEL_S    0
#define AR_PHY_SIGMA_DELTA_FILT2    0x000000F8
#define AR_PHY_SIGMA_DELTA_FILT2_S  3
#define AR_PHY_SIGMA_DELTA_FILT1    0x00001F00
#define AR_PHY_SIGMA_DELTA_FILT1_S  8
#define AR_PHY_SIGMA_DELTA_ADC_CLIP 0x01FFE000
#define AR_PHY_SIGMA_DELTA_ADC_CLIP_S   13

#define AR_PHY_RESTART      0x1C5970      /* restart */
#define AR_PHY_RESTART_DIV_GC   0x001C0000  /* bb_ant_fast_div_gc_limit */
#define AR_PHY_RESTART_DIV_GC_S 18

#define AR_PHY_RFBUS_REQ        0x1C597C
#define AR_PHY_RFBUS_REQ_EN     0x00000001

#define AR_PHY_RX_CHAINMASK     0x1C59a4

#define AR_PHY_EXT_CCA          0x1C59bc
#define AR_PHY_EXT_MINCCA_PWR   0xFF800000
#define AR_PHY_EXT_MINCCA_PWR_S 23

#define AR_PHY_HALFGI           0x1C59D0      /* Timing control 3 */
#define AR_PHY_HALFGI_DSC_MAN   0x0007FFF0
#define AR_PHY_HALFGI_DSC_MAN_S 4
#define AR_PHY_HALFGI_DSC_EXP   0x0000000F
#define AR_PHY_HALFGI_DSC_EXP_S 0

#define AR_PHY_HEAVY_CLIP_ENABLE    0x1C59E0

#define AR_PHY_M_SLEEP      0x1C59f0      /* sleep control registers */
#define AR_PHY_REFCLKDLY    0x1C59f4
#define AR_PHY_REFCLKPD     0x1C59f8

/* PHY IQ calibration results */
#define AR_PHY_IQCAL_RES_PWR_MEAS_I 0x1C5C10  /* power measurement for I */
#define AR_PHY_IQCAL_RES_PWR_MEAS_Q 0x1C5C14  /* power measurement for Q */
#define AR_PHY_IQCAL_RES_IQ_CORR_MEAS   0x1C5C18  /* IQ correlation measurement */

#define AR_PHY_CURRENT_RSSI 0x1C5C1c      /* rssi of current frame rx'd */

#define AR_PHY_RFBUS_GRANT       0x1C5C20
#define AR_PHY_RFBUS_GRANT_EN    0x00000001

#define AR_PHY_MODE     0x1C6200  /* Mode register */
#define AR_PHY_MODE_AR2133  0x08    /* AR2133 */
#define AR_PHY_MODE_AR5111  0x00    /* AR5111/AR2111 */
#define AR_PHY_MODE_AR5112  0x08    /* AR5112*/
#define AR_PHY_MODE_DYNAMIC 0x04    /* dynamic CCK/OFDM mode */
#define AR_PHY_MODE_RF2GHZ  0x02    /* 2.4 GHz */
#define AR_PHY_MODE_RF5GHZ  0x00    /* 5 GHz */
#define AR_PHY_MODE_CCK     0x01    /* CCK */
#define AR_PHY_MODE_OFDM    0x00    /* OFDM */

#define AR_PHY_CCK_TX_CTRL  0x1C6204
#define AR_PHY_CCK_TX_CTRL_JAPAN    0x00000010

#define AR_PHY_CCK_DETECT                           0x1C6208
#define AR_PHY_CCK_DETECT_WEAK_SIG_THR_CCK          0x0000003F
#define AR_PHY_CCK_DETECT_WEAK_SIG_THR_CCK_S        0
#define AR_PHY_CCK_DETECT_ANT_SWITCH_TIME           0x00001FC0 /* [12:6] settling time for antenna switch */
#define AR_PHY_CCK_DETECT_ANT_SWITCH_TIME_S         6
#define AR_PHY_CCK_DETECT_BB_ENABLE_ANT_FAST_DIV    0x2000

#define AR_PHY_GAIN_2GHZ    0x1C620C
#define AR_PHY_GAIN_2GHZ_RXTX_MARGIN    0x00FC0000
#define AR_PHY_GAIN_2GHZ_RXTX_MARGIN_S  18
#define AR_PHY_GAIN_2GHZ_BSW_MARGIN     0x00003C00
#define AR_PHY_GAIN_2GHZ_BSW_MARGIN_S   10
#define AR_PHY_GAIN_2GHZ_BSW_ATTEN      0x0000001F
#define AR_PHY_GAIN_2GHZ_BSW_ATTEN_S    0

#define AR_PHY_CCK_RXCTRL4  0x1C621C
#define AR_PHY_CCK_RXCTRL4_FREQ_EST_SHORT   0x01F80000
#define AR_PHY_CCK_RXCTRL4_FREQ_EST_SHORT_S 19

#define AR_PHY_DAG_CTRLCCK  0x1C6228
#define AR_PHY_DAG_CTRLCCK_EN_RSSI_THR  0x00000200 /* BB Rev 4.2+ only */
#define AR_PHY_DAG_CTRLCCK_RSSI_THR 0x0001FC00 /* BB Rev 4.2+ only */
#define AR_PHY_DAG_CTRLCCK_RSSI_THR_S   10     /* BB Rev 4.2+ only */

#define AR_PHY_POWER_TX_RATE3   0x1C6234
#define AR_PHY_POWER_TX_RATE4   0x1C6238

#define AR_PHY_SCRM_SEQ_XR  0x1C623C
#define AR_PHY_HEADER_DETECT_XR 0x1C6240
#define AR_PHY_CHIRP_DETECTED_XR    0x1C6244
#define AR_PHY_BLUETOOTH    0x1C6254

#define AR_PHY_TPCRG1   0x1C6258  /* ar2413 power control */
#define AR_PHY_TPCRG1_NUM_PD_GAIN   0x0000c000
#define AR_PHY_TPCRG1_NUM_PD_GAIN_S 14

#define AR_PHY_TPCRG1_PD_GAIN_1    0x00030000
#define AR_PHY_TPCRG1_PD_GAIN_1_S  16
#define AR_PHY_TPCRG1_PD_GAIN_2    0x000C0000
#define AR_PHY_TPCRG1_PD_GAIN_2_S  18
#define AR_PHY_TPCRG1_PD_GAIN_3    0x00300000
#define AR_PHY_TPCRG1_PD_GAIN_3_S  20

#define AR_PHY_ANALOG_SWAP      0xa268
#define AR_PHY_SWAP_ALT_CHAIN   0x00000040

#define AR_PHY_TPCRG5   0x1C626C /* ar2413 power control */
#define AR_PHY_TPCRG5_PD_GAIN_OVERLAP   0x0000000F
#define AR_PHY_TPCRG5_PD_GAIN_OVERLAP_S     0
#define AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_1    0x000003F0
#define AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_1_S  4
#define AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_2    0x0000FC00
#define AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_2_S  10
#define AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_3    0x003F0000
#define AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_3_S  16
#define AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_4    0x0FC00000
#define AR_PHY_TPCRG5_PD_GAIN_BOUNDARY_4_S  22

#define AR_PHY_POWER_TX_RATE5   0x1C638C
#define AR_PHY_POWER_TX_RATE6   0x1C6390

#define AR_PHY_CAL_CHAINMASK    0x1C639C

#define AR_PHY_POWER_TX_SUB     0x1C63C8
#define AR_PHY_POWER_TX_RATE7   0x1C63CC
#define AR_PHY_POWER_TX_RATE8   0x1C63D0
#define AR_PHY_POWER_TX_RATE9   0x1C63D4
