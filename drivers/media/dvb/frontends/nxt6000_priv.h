/*
 * Public Include File for DRV6000 users
 * (ie. NxtWave Communications - NXT6000 demodulator driver)
 *
 * Copyright (C) 2001 NxtWave Communications, Inc.
 *
 */

/*  Nxt6000 Register Addresses and Bit Masks */

/* Maximum Register Number */
#define MAXNXT6000REG          (0x9A)

/* 0x1B A_VIT_BER_0  aka 0x3A */
#define A_VIT_BER_0            (0x1B)

/* 0x1D A_VIT_BER_TIMER_0 aka 0x38 */
#define A_VIT_BER_TIMER_0      (0x1D)

/* 0x21 RS_COR_STAT */
#define RS_COR_STAT            (0x21)
#define RSCORESTATUS           (0x03)

/* 0x22 RS_COR_INTEN */
#define RS_COR_INTEN           (0x22)

/* 0x23 RS_COR_INSTAT */
#define RS_COR_INSTAT          (0x23)
#define INSTAT_ERROR           (0x04)
#define LOCK_LOSS_BITS         (0x03)

/* 0x24 RS_COR_SYNC_PARAM */
#define RS_COR_SYNC_PARAM      (0x24)
#define SYNC_PARAM             (0x03)

/* 0x25 BER_CTRL */
#define BER_CTRL               (0x25)
#define BER_ENABLE             (0x02)
#define BER_RESET              (0x01)

/* 0x26 BER_PAY */
#define BER_PAY                (0x26)

/* 0x27 BER_PKT_L */
#define BER_PKT_L              (0x27)
#define BER_PKTOVERFLOW        (0x80)

/* 0x30 VIT_COR_CTL */
#define VIT_COR_CTL            (0x30)
#define BER_CONTROL            (0x02)
#define VIT_COR_MASK           (0x82)
#define VIT_COR_RESYNC         (0x80)


/* 0x32 VIT_SYNC_STATUS */
#define VIT_SYNC_STATUS        (0x32)
#define VITINSYNC              (0x80)

/* 0x33 VIT_COR_INTEN */
#define VIT_COR_INTEN          (0x33)
#define GLOBAL_ENABLE          (0x80)

/* 0x34 VIT_COR_INTSTAT */
#define VIT_COR_INTSTAT        (0x34)
#define BER_DONE               (0x08)
#define BER_OVERFLOW           (0x10)

			     /* 0x38 OFDM_BERTimer *//* Use the alias registers */
#define A_VIT_BER_TIMER_0      (0x1D)

			     /* 0x3A VIT_BER_TIMER_0 *//* Use the alias registers */
#define A_VIT_BER_0            (0x1B)

/* 0x40 OFDM_COR_CTL */
#define OFDM_COR_CTL           (0x40)
#define COREACT                (0x20)
#define HOLDSM                 (0x10)
#define WAIT_AGC               (0x02)
#define WAIT_SYR               (0x03)

/* 0x41 OFDM_COR_STAT */
#define OFDM_COR_STAT          (0x41)
#define COR_STATUS             (0x0F)
#define MONITOR_TPS            (0x06)
#define TPSLOCKED              (0x40)
#define AGCLOCKED              (0x10)

/* 0x42 OFDM_COR_INTEN */
#define OFDM_COR_INTEN         (0x42)
#define TPSRCVBAD              (0x04)
#define TPSRCVCHANGED         (0x02)
#define TPSRCVUPDATE           (0x01)

/* 0x43 OFDM_COR_INSTAT */
#define OFDM_COR_INSTAT        (0x43)

/* 0x44 OFDM_COR_MODEGUARD */
#define OFDM_COR_MODEGUARD     (0x44)
#define FORCEMODE              (0x08)
#define FORCEMODE8K			   (0x04)

/* 0x45 OFDM_AGC_CTL */
#define OFDM_AGC_CTL           (0x45)
#define INITIAL_AGC_BW		   (0x08)
#define AGCNEG                 (0x02)
#define AGCLAST				   (0x10)

/* 0x48 OFDM_AGC_TARGET */
#define OFDM_AGC_TARGET		   (0x48)
#define OFDM_AGC_TARGET_DEFAULT (0x28)
#define OFDM_AGC_TARGET_IMPULSE (0x38)

/* 0x49 OFDM_AGC_GAIN_1 */
#define OFDM_AGC_GAIN_1        (0x49)

/* 0x4B OFDM_ITB_CTL */
#define OFDM_ITB_CTL           (0x4B)
#define ITBINV                 (0x01)

/* 0x4C OFDM_ITB_FREQ_1 */
#define OFDM_ITB_FREQ_1        (0x4C)

/* 0x4D OFDM_ITB_FREQ_2 */
#define OFDM_ITB_FREQ_2        (0x4D)

/* 0x4E  OFDM_CAS_CTL */
#define OFDM_CAS_CTL           (0x4E)
#define ACSDIS                 (0x40)
#define CCSEN                  (0x80)

/* 0x4F CAS_FREQ */
#define CAS_FREQ               (0x4F)

/* 0x51 OFDM_SYR_CTL */
#define OFDM_SYR_CTL           (0x51)
#define SIXTH_ENABLE           (0x80)
#define SYR_TRACKING_DISABLE   (0x01)

/* 0x52 OFDM_SYR_STAT */
#define OFDM_SYR_STAT		   (0x52)
#define GI14_2K_SYR_LOCK	   (0x13)
#define GI14_8K_SYR_LOCK	   (0x17)
#define GI14_SYR_LOCK		   (0x10)

/* 0x55 OFDM_SYR_OFFSET_1 */
#define OFDM_SYR_OFFSET_1      (0x55)

/* 0x56 OFDM_SYR_OFFSET_2 */
#define OFDM_SYR_OFFSET_2      (0x56)

/* 0x58 OFDM_SCR_CTL */
#define OFDM_SCR_CTL           (0x58)
#define SYR_ADJ_DECAY_MASK     (0x70)
#define SYR_ADJ_DECAY          (0x30)

/* 0x59 OFDM_PPM_CTL_1 */
#define OFDM_PPM_CTL_1         (0x59)
#define PPMMAX_MASK            (0x30)
#define PPM256				   (0x30)

/* 0x5B OFDM_TRL_NOMINALRATE_1 */
#define OFDM_TRL_NOMINALRATE_1 (0x5B)

/* 0x5C OFDM_TRL_NOMINALRATE_2 */
#define OFDM_TRL_NOMINALRATE_2 (0x5C)

/* 0x5D OFDM_TRL_TIME_1 */
#define OFDM_TRL_TIME_1        (0x5D)

/* 0x60 OFDM_CRL_FREQ_1 */
#define OFDM_CRL_FREQ_1        (0x60)

/* 0x63 OFDM_CHC_CTL_1 */
#define OFDM_CHC_CTL_1         (0x63)
#define MANMEAN1               (0xF0);
#define CHCFIR                 (0x01)

/* 0x64 OFDM_CHC_SNR */
#define OFDM_CHC_SNR           (0x64)

/* 0x65 OFDM_BDI_CTL */
#define OFDM_BDI_CTL           (0x65)
#define LP_SELECT              (0x02)

/* 0x67 OFDM_TPS_RCVD_1 */
#define OFDM_TPS_RCVD_1        (0x67)
#define TPSFRAME               (0x03)

/* 0x68 OFDM_TPS_RCVD_2 */
#define OFDM_TPS_RCVD_2        (0x68)

/* 0x69 OFDM_TPS_RCVD_3 */
#define OFDM_TPS_RCVD_3        (0x69)

/* 0x6A OFDM_TPS_RCVD_4 */
#define OFDM_TPS_RCVD_4        (0x6A)

/* 0x6B OFDM_TPS_RESERVED_1 */
#define OFDM_TPS_RESERVED_1    (0x6B)

/* 0x6C OFDM_TPS_RESERVED_2 */
#define OFDM_TPS_RESERVED_2    (0x6C)

/* 0x73 OFDM_MSC_REV */
#define OFDM_MSC_REV           (0x73)

/* 0x76 OFDM_SNR_CARRIER_2 */
#define OFDM_SNR_CARRIER_2     (0x76)
#define MEAN_MASK              (0x80)
#define MEANBIT                (0x80)

/* 0x80 ANALOG_CONTROL_0 */
#define ANALOG_CONTROL_0       (0x80)
#define POWER_DOWN_ADC         (0x40)

/* 0x81 ENABLE_TUNER_IIC */
#define ENABLE_TUNER_IIC       (0x81)
#define ENABLE_TUNER_BIT       (0x01)

/* 0x82 EN_DMD_RACQ */
#define EN_DMD_RACQ            (0x82)
#define EN_DMD_RACQ_REG_VAL    (0x81)
#define EN_DMD_RACQ_REG_VAL_14 (0x01)

/* 0x84 SNR_COMMAND */
#define SNR_COMMAND            (0x84)
#define SNRStat                (0x80)

/* 0x85 SNRCARRIERNUMBER_LSB */
#define SNRCARRIERNUMBER_LSB   (0x85)

/* 0x87 SNRMINTHRESHOLD_LSB */
#define SNRMINTHRESHOLD_LSB    (0x87)

/* 0x89 SNR_PER_CARRIER_LSB */
#define SNR_PER_CARRIER_LSB    (0x89)

/* 0x8B SNRBELOWTHRESHOLD_LSB */
#define SNRBELOWTHRESHOLD_LSB  (0x8B)

/* 0x91 RF_AGC_VAL_1 */
#define RF_AGC_VAL_1           (0x91)

/* 0x92 RF_AGC_STATUS */
#define RF_AGC_STATUS          (0x92)

/* 0x98 DIAG_CONFIG */
#define DIAG_CONFIG            (0x98)
#define DIAG_MASK              (0x70)
#define TB_SET                 (0x10)
#define TRAN_SELECT            (0x07)
#define SERIAL_SELECT          (0x01)

/* 0x99 SUB_DIAG_MODE_SEL */
#define SUB_DIAG_MODE_SEL      (0x99)
#define CLKINVERSION           (0x01)

/* 0x9A TS_FORMAT */
#define TS_FORMAT              (0x9A)
#define ERROR_SENSE            (0x08)
#define VALID_SENSE            (0x04)
#define SYNC_SENSE             (0x02)
#define GATED_CLOCK            (0x01)

#define NXT6000ASICDEVICE      (0x0b)
