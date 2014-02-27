#ifndef _TOP_REG_REG_H_
#define _TOP_REG_REG_H_

#define TOP_GAIN_ADDRESS                         0x00000000
#define TOP_GAIN_OFFSET                          0x00000000
#define TOP_GAIN_TX6DBLOQGAIN_MSB                31
#define TOP_GAIN_TX6DBLOQGAIN_LSB                30
#define TOP_GAIN_TX6DBLOQGAIN_MASK               0xc0000000
#define TOP_GAIN_TX6DBLOQGAIN_GET(x)             (((x) & TOP_GAIN_TX6DBLOQGAIN_MASK) >> TOP_GAIN_TX6DBLOQGAIN_LSB)
#define TOP_GAIN_TX6DBLOQGAIN_SET(x)             (((x) << TOP_GAIN_TX6DBLOQGAIN_LSB) & TOP_GAIN_TX6DBLOQGAIN_MASK)
#define TOP_GAIN_TX1DBLOQGAIN_MSB                29
#define TOP_GAIN_TX1DBLOQGAIN_LSB                27
#define TOP_GAIN_TX1DBLOQGAIN_MASK               0x38000000
#define TOP_GAIN_TX1DBLOQGAIN_GET(x)             (((x) & TOP_GAIN_TX1DBLOQGAIN_MASK) >> TOP_GAIN_TX1DBLOQGAIN_LSB)
#define TOP_GAIN_TX1DBLOQGAIN_SET(x)             (((x) << TOP_GAIN_TX1DBLOQGAIN_LSB) & TOP_GAIN_TX1DBLOQGAIN_MASK)
#define TOP_GAIN_TXV2IGAIN_MSB                   26
#define TOP_GAIN_TXV2IGAIN_LSB                   25
#define TOP_GAIN_TXV2IGAIN_MASK                  0x06000000
#define TOP_GAIN_TXV2IGAIN_GET(x)                (((x) & TOP_GAIN_TXV2IGAIN_MASK) >> TOP_GAIN_TXV2IGAIN_LSB)
#define TOP_GAIN_TXV2IGAIN_SET(x)                (((x) << TOP_GAIN_TXV2IGAIN_LSB) & TOP_GAIN_TXV2IGAIN_MASK)
#define TOP_GAIN_PABUF5GN_MSB                    24
#define TOP_GAIN_PABUF5GN_LSB                    24
#define TOP_GAIN_PABUF5GN_MASK                   0x01000000
#define TOP_GAIN_PABUF5GN_GET(x)                 (((x) & TOP_GAIN_PABUF5GN_MASK) >> TOP_GAIN_PABUF5GN_LSB)
#define TOP_GAIN_PABUF5GN_SET(x)                 (((x) << TOP_GAIN_PABUF5GN_LSB) & TOP_GAIN_PABUF5GN_MASK)
#define TOP_GAIN_PADRVGN_MSB                     23
#define TOP_GAIN_PADRVGN_LSB                     21
#define TOP_GAIN_PADRVGN_MASK                    0x00e00000
#define TOP_GAIN_PADRVGN_GET(x)                  (((x) & TOP_GAIN_PADRVGN_MASK) >> TOP_GAIN_PADRVGN_LSB)
#define TOP_GAIN_PADRVGN_SET(x)                  (((x) << TOP_GAIN_PADRVGN_LSB) & TOP_GAIN_PADRVGN_MASK)
#define TOP_GAIN_PAOUT2GN_MSB                    20
#define TOP_GAIN_PAOUT2GN_LSB                    18
#define TOP_GAIN_PAOUT2GN_MASK                   0x001c0000
#define TOP_GAIN_PAOUT2GN_GET(x)                 (((x) & TOP_GAIN_PAOUT2GN_MASK) >> TOP_GAIN_PAOUT2GN_LSB)
#define TOP_GAIN_PAOUT2GN_SET(x)                 (((x) << TOP_GAIN_PAOUT2GN_LSB) & TOP_GAIN_PAOUT2GN_MASK)
#define TOP_GAIN_LNAON_MSB                       17
#define TOP_GAIN_LNAON_LSB                       17
#define TOP_GAIN_LNAON_MASK                      0x00020000
#define TOP_GAIN_LNAON_GET(x)                    (((x) & TOP_GAIN_LNAON_MASK) >> TOP_GAIN_LNAON_LSB)
#define TOP_GAIN_LNAON_SET(x)                    (((x) << TOP_GAIN_LNAON_LSB) & TOP_GAIN_LNAON_MASK)
#define TOP_GAIN_LNAGAIN_MSB                     16
#define TOP_GAIN_LNAGAIN_LSB                     13
#define TOP_GAIN_LNAGAIN_MASK                    0x0001e000
#define TOP_GAIN_LNAGAIN_GET(x)                  (((x) & TOP_GAIN_LNAGAIN_MASK) >> TOP_GAIN_LNAGAIN_LSB)
#define TOP_GAIN_LNAGAIN_SET(x)                  (((x) << TOP_GAIN_LNAGAIN_LSB) & TOP_GAIN_LNAGAIN_MASK)
#define TOP_GAIN_RFVGA5GAIN_MSB                  12
#define TOP_GAIN_RFVGA5GAIN_LSB                  11
#define TOP_GAIN_RFVGA5GAIN_MASK                 0x00001800
#define TOP_GAIN_RFVGA5GAIN_GET(x)               (((x) & TOP_GAIN_RFVGA5GAIN_MASK) >> TOP_GAIN_RFVGA5GAIN_LSB)
#define TOP_GAIN_RFVGA5GAIN_SET(x)               (((x) << TOP_GAIN_RFVGA5GAIN_LSB) & TOP_GAIN_RFVGA5GAIN_MASK)
#define TOP_GAIN_RFGMGN_MSB                      10
#define TOP_GAIN_RFGMGN_LSB                      8
#define TOP_GAIN_RFGMGN_MASK                     0x00000700
#define TOP_GAIN_RFGMGN_GET(x)                   (((x) & TOP_GAIN_RFGMGN_MASK) >> TOP_GAIN_RFGMGN_LSB)
#define TOP_GAIN_RFGMGN_SET(x)                   (((x) << TOP_GAIN_RFGMGN_LSB) & TOP_GAIN_RFGMGN_MASK)
#define TOP_GAIN_RX6DBLOQGAIN_MSB                7
#define TOP_GAIN_RX6DBLOQGAIN_LSB                6
#define TOP_GAIN_RX6DBLOQGAIN_MASK               0x000000c0
#define TOP_GAIN_RX6DBLOQGAIN_GET(x)             (((x) & TOP_GAIN_RX6DBLOQGAIN_MASK) >> TOP_GAIN_RX6DBLOQGAIN_LSB)
#define TOP_GAIN_RX6DBLOQGAIN_SET(x)             (((x) << TOP_GAIN_RX6DBLOQGAIN_LSB) & TOP_GAIN_RX6DBLOQGAIN_MASK)
#define TOP_GAIN_RX1DBLOQGAIN_MSB                5
#define TOP_GAIN_RX1DBLOQGAIN_LSB                3
#define TOP_GAIN_RX1DBLOQGAIN_MASK               0x00000038
#define TOP_GAIN_RX1DBLOQGAIN_GET(x)             (((x) & TOP_GAIN_RX1DBLOQGAIN_MASK) >> TOP_GAIN_RX1DBLOQGAIN_LSB)
#define TOP_GAIN_RX1DBLOQGAIN_SET(x)             (((x) << TOP_GAIN_RX1DBLOQGAIN_LSB) & TOP_GAIN_RX1DBLOQGAIN_MASK)
#define TOP_GAIN_RX6DBHIQGAIN_MSB                2
#define TOP_GAIN_RX6DBHIQGAIN_LSB                1
#define TOP_GAIN_RX6DBHIQGAIN_MASK               0x00000006
#define TOP_GAIN_RX6DBHIQGAIN_GET(x)             (((x) & TOP_GAIN_RX6DBHIQGAIN_MASK) >> TOP_GAIN_RX6DBHIQGAIN_LSB)
#define TOP_GAIN_RX6DBHIQGAIN_SET(x)             (((x) << TOP_GAIN_RX6DBHIQGAIN_LSB) & TOP_GAIN_RX6DBHIQGAIN_MASK)
#define TOP_GAIN_SPARE_MSB                       0
#define TOP_GAIN_SPARE_LSB                       0
#define TOP_GAIN_SPARE_MASK                      0x00000001
#define TOP_GAIN_SPARE_GET(x)                    (((x) & TOP_GAIN_SPARE_MASK) >> TOP_GAIN_SPARE_LSB)
#define TOP_GAIN_SPARE_SET(x)                    (((x) << TOP_GAIN_SPARE_LSB) & TOP_GAIN_SPARE_MASK)

#define TOP_TOP_ADDRESS                          0x00000004
#define TOP_TOP_OFFSET                           0x00000004
#define TOP_TOP_LOCALTXGAIN_MSB                  31
#define TOP_TOP_LOCALTXGAIN_LSB                  31
#define TOP_TOP_LOCALTXGAIN_MASK                 0x80000000
#define TOP_TOP_LOCALTXGAIN_GET(x)               (((x) & TOP_TOP_LOCALTXGAIN_MASK) >> TOP_TOP_LOCALTXGAIN_LSB)
#define TOP_TOP_LOCALTXGAIN_SET(x)               (((x) << TOP_TOP_LOCALTXGAIN_LSB) & TOP_TOP_LOCALTXGAIN_MASK)
#define TOP_TOP_LOCALRXGAIN_MSB                  30
#define TOP_TOP_LOCALRXGAIN_LSB                  30
#define TOP_TOP_LOCALRXGAIN_MASK                 0x40000000
#define TOP_TOP_LOCALRXGAIN_GET(x)               (((x) & TOP_TOP_LOCALRXGAIN_MASK) >> TOP_TOP_LOCALRXGAIN_LSB)
#define TOP_TOP_LOCALRXGAIN_SET(x)               (((x) << TOP_TOP_LOCALRXGAIN_LSB) & TOP_TOP_LOCALRXGAIN_MASK)
#define TOP_TOP_LOCALMODE_MSB                    29
#define TOP_TOP_LOCALMODE_LSB                    29
#define TOP_TOP_LOCALMODE_MASK                   0x20000000
#define TOP_TOP_LOCALMODE_GET(x)                 (((x) & TOP_TOP_LOCALMODE_MASK) >> TOP_TOP_LOCALMODE_LSB)
#define TOP_TOP_LOCALMODE_SET(x)                 (((x) << TOP_TOP_LOCALMODE_LSB) & TOP_TOP_LOCALMODE_MASK)
#define TOP_TOP_CALFC_MSB                        28
#define TOP_TOP_CALFC_LSB                        28
#define TOP_TOP_CALFC_MASK                       0x10000000
#define TOP_TOP_CALFC_GET(x)                     (((x) & TOP_TOP_CALFC_MASK) >> TOP_TOP_CALFC_LSB)
#define TOP_TOP_CALFC_SET(x)                     (((x) << TOP_TOP_CALFC_LSB) & TOP_TOP_CALFC_MASK)
#define TOP_TOP_CALDC_MSB                        27
#define TOP_TOP_CALDC_LSB                        27
#define TOP_TOP_CALDC_MASK                       0x08000000
#define TOP_TOP_CALDC_GET(x)                     (((x) & TOP_TOP_CALDC_MASK) >> TOP_TOP_CALDC_LSB)
#define TOP_TOP_CALDC_SET(x)                     (((x) << TOP_TOP_CALDC_LSB) & TOP_TOP_CALDC_MASK)
#define TOP_TOP_CAL_RESIDUE_MSB                  26
#define TOP_TOP_CAL_RESIDUE_LSB                  26
#define TOP_TOP_CAL_RESIDUE_MASK                 0x04000000
#define TOP_TOP_CAL_RESIDUE_GET(x)               (((x) & TOP_TOP_CAL_RESIDUE_MASK) >> TOP_TOP_CAL_RESIDUE_LSB)
#define TOP_TOP_CAL_RESIDUE_SET(x)               (((x) << TOP_TOP_CAL_RESIDUE_LSB) & TOP_TOP_CAL_RESIDUE_MASK)
#define TOP_TOP_BMODE_MSB                        25
#define TOP_TOP_BMODE_LSB                        25
#define TOP_TOP_BMODE_MASK                       0x02000000
#define TOP_TOP_BMODE_GET(x)                     (((x) & TOP_TOP_BMODE_MASK) >> TOP_TOP_BMODE_LSB)
#define TOP_TOP_BMODE_SET(x)                     (((x) << TOP_TOP_BMODE_LSB) & TOP_TOP_BMODE_MASK)
#define TOP_TOP_SYNTHON_MSB                      24
#define TOP_TOP_SYNTHON_LSB                      24
#define TOP_TOP_SYNTHON_MASK                     0x01000000
#define TOP_TOP_SYNTHON_GET(x)                   (((x) & TOP_TOP_SYNTHON_MASK) >> TOP_TOP_SYNTHON_LSB)
#define TOP_TOP_SYNTHON_SET(x)                   (((x) << TOP_TOP_SYNTHON_LSB) & TOP_TOP_SYNTHON_MASK)
#define TOP_TOP_RXON_MSB                         23
#define TOP_TOP_RXON_LSB                         23
#define TOP_TOP_RXON_MASK                        0x00800000
#define TOP_TOP_RXON_GET(x)                      (((x) & TOP_TOP_RXON_MASK) >> TOP_TOP_RXON_LSB)
#define TOP_TOP_RXON_SET(x)                      (((x) << TOP_TOP_RXON_LSB) & TOP_TOP_RXON_MASK)
#define TOP_TOP_TXON_MSB                         22
#define TOP_TOP_TXON_LSB                         22
#define TOP_TOP_TXON_MASK                        0x00400000
#define TOP_TOP_TXON_GET(x)                      (((x) & TOP_TOP_TXON_MASK) >> TOP_TOP_TXON_LSB)
#define TOP_TOP_TXON_SET(x)                      (((x) << TOP_TOP_TXON_LSB) & TOP_TOP_TXON_MASK)
#define TOP_TOP_PAON_MSB                         21
#define TOP_TOP_PAON_LSB                         21
#define TOP_TOP_PAON_MASK                        0x00200000
#define TOP_TOP_PAON_GET(x)                      (((x) & TOP_TOP_PAON_MASK) >> TOP_TOP_PAON_LSB)
#define TOP_TOP_PAON_SET(x)                      (((x) << TOP_TOP_PAON_LSB) & TOP_TOP_PAON_MASK)
#define TOP_TOP_CALTX_MSB                        20
#define TOP_TOP_CALTX_LSB                        20
#define TOP_TOP_CALTX_MASK                       0x00100000
#define TOP_TOP_CALTX_GET(x)                     (((x) & TOP_TOP_CALTX_MASK) >> TOP_TOP_CALTX_LSB)
#define TOP_TOP_CALTX_SET(x)                     (((x) << TOP_TOP_CALTX_LSB) & TOP_TOP_CALTX_MASK)
#define TOP_TOP_LOCALADDAC_MSB                   19
#define TOP_TOP_LOCALADDAC_LSB                   19
#define TOP_TOP_LOCALADDAC_MASK                  0x00080000
#define TOP_TOP_LOCALADDAC_GET(x)                (((x) & TOP_TOP_LOCALADDAC_MASK) >> TOP_TOP_LOCALADDAC_LSB)
#define TOP_TOP_LOCALADDAC_SET(x)                (((x) << TOP_TOP_LOCALADDAC_LSB) & TOP_TOP_LOCALADDAC_MASK)
#define TOP_TOP_PWDPLL_MSB                       18
#define TOP_TOP_PWDPLL_LSB                       18
#define TOP_TOP_PWDPLL_MASK                      0x00040000
#define TOP_TOP_PWDPLL_GET(x)                    (((x) & TOP_TOP_PWDPLL_MASK) >> TOP_TOP_PWDPLL_LSB)
#define TOP_TOP_PWDPLL_SET(x)                    (((x) << TOP_TOP_PWDPLL_LSB) & TOP_TOP_PWDPLL_MASK)
#define TOP_TOP_PWDADC_MSB                       17
#define TOP_TOP_PWDADC_LSB                       17
#define TOP_TOP_PWDADC_MASK                      0x00020000
#define TOP_TOP_PWDADC_GET(x)                    (((x) & TOP_TOP_PWDADC_MASK) >> TOP_TOP_PWDADC_LSB)
#define TOP_TOP_PWDADC_SET(x)                    (((x) << TOP_TOP_PWDADC_LSB) & TOP_TOP_PWDADC_MASK)
#define TOP_TOP_PWDDAC_MSB                       16
#define TOP_TOP_PWDDAC_LSB                       16
#define TOP_TOP_PWDDAC_MASK                      0x00010000
#define TOP_TOP_PWDDAC_GET(x)                    (((x) & TOP_TOP_PWDDAC_MASK) >> TOP_TOP_PWDDAC_LSB)
#define TOP_TOP_PWDDAC_SET(x)                    (((x) << TOP_TOP_PWDDAC_LSB) & TOP_TOP_PWDDAC_MASK)
#define TOP_TOP_LOCALXTAL_MSB                    15
#define TOP_TOP_LOCALXTAL_LSB                    15
#define TOP_TOP_LOCALXTAL_MASK                   0x00008000
#define TOP_TOP_LOCALXTAL_GET(x)                 (((x) & TOP_TOP_LOCALXTAL_MASK) >> TOP_TOP_LOCALXTAL_LSB)
#define TOP_TOP_LOCALXTAL_SET(x)                 (((x) << TOP_TOP_LOCALXTAL_LSB) & TOP_TOP_LOCALXTAL_MASK)
#define TOP_TOP_PWDCLKIN_MSB                     14
#define TOP_TOP_PWDCLKIN_LSB                     14
#define TOP_TOP_PWDCLKIN_MASK                    0x00004000
#define TOP_TOP_PWDCLKIN_GET(x)                  (((x) & TOP_TOP_PWDCLKIN_MASK) >> TOP_TOP_PWDCLKIN_LSB)
#define TOP_TOP_PWDCLKIN_SET(x)                  (((x) << TOP_TOP_PWDCLKIN_LSB) & TOP_TOP_PWDCLKIN_MASK)
#define TOP_TOP_OSCON_MSB                        13
#define TOP_TOP_OSCON_LSB                        13
#define TOP_TOP_OSCON_MASK                       0x00002000
#define TOP_TOP_OSCON_GET(x)                     (((x) & TOP_TOP_OSCON_MASK) >> TOP_TOP_OSCON_LSB)
#define TOP_TOP_OSCON_SET(x)                     (((x) << TOP_TOP_OSCON_LSB) & TOP_TOP_OSCON_MASK)
#define TOP_TOP_SCLKEN_FORCE_MSB                 12
#define TOP_TOP_SCLKEN_FORCE_LSB                 12
#define TOP_TOP_SCLKEN_FORCE_MASK                0x00001000
#define TOP_TOP_SCLKEN_FORCE_GET(x)              (((x) & TOP_TOP_SCLKEN_FORCE_MASK) >> TOP_TOP_SCLKEN_FORCE_LSB)
#define TOP_TOP_SCLKEN_FORCE_SET(x)              (((x) << TOP_TOP_SCLKEN_FORCE_LSB) & TOP_TOP_SCLKEN_FORCE_MASK)
#define TOP_TOP_SYNTHON_FORCE_MSB                11
#define TOP_TOP_SYNTHON_FORCE_LSB                11
#define TOP_TOP_SYNTHON_FORCE_MASK               0x00000800
#define TOP_TOP_SYNTHON_FORCE_GET(x)             (((x) & TOP_TOP_SYNTHON_FORCE_MASK) >> TOP_TOP_SYNTHON_FORCE_LSB)
#define TOP_TOP_SYNTHON_FORCE_SET(x)             (((x) << TOP_TOP_SYNTHON_FORCE_LSB) & TOP_TOP_SYNTHON_FORCE_MASK)
#define TOP_TOP_PDBIAS_MSB                       10
#define TOP_TOP_PDBIAS_LSB                       10
#define TOP_TOP_PDBIAS_MASK                      0x00000400
#define TOP_TOP_PDBIAS_GET(x)                    (((x) & TOP_TOP_PDBIAS_MASK) >> TOP_TOP_PDBIAS_LSB)
#define TOP_TOP_PDBIAS_SET(x)                    (((x) << TOP_TOP_PDBIAS_LSB) & TOP_TOP_PDBIAS_MASK)
#define TOP_TOP_DATAOUTSEL_MSB                   9
#define TOP_TOP_DATAOUTSEL_LSB                   8
#define TOP_TOP_DATAOUTSEL_MASK                  0x00000300
#define TOP_TOP_DATAOUTSEL_GET(x)                (((x) & TOP_TOP_DATAOUTSEL_MASK) >> TOP_TOP_DATAOUTSEL_LSB)
#define TOP_TOP_DATAOUTSEL_SET(x)                (((x) << TOP_TOP_DATAOUTSEL_LSB) & TOP_TOP_DATAOUTSEL_MASK)
#define TOP_TOP_REVID_MSB                        7
#define TOP_TOP_REVID_LSB                        5
#define TOP_TOP_REVID_MASK                       0x000000e0
#define TOP_TOP_REVID_GET(x)                     (((x) & TOP_TOP_REVID_MASK) >> TOP_TOP_REVID_LSB)
#define TOP_TOP_REVID_SET(x)                     (((x) << TOP_TOP_REVID_LSB) & TOP_TOP_REVID_MASK)
#define TOP_TOP_INT2PAD_MSB                      4
#define TOP_TOP_INT2PAD_LSB                      4
#define TOP_TOP_INT2PAD_MASK                     0x00000010
#define TOP_TOP_INT2PAD_GET(x)                   (((x) & TOP_TOP_INT2PAD_MASK) >> TOP_TOP_INT2PAD_LSB)
#define TOP_TOP_INT2PAD_SET(x)                   (((x) << TOP_TOP_INT2PAD_LSB) & TOP_TOP_INT2PAD_MASK)
#define TOP_TOP_INTH2PAD_MSB                     3
#define TOP_TOP_INTH2PAD_LSB                     3
#define TOP_TOP_INTH2PAD_MASK                    0x00000008
#define TOP_TOP_INTH2PAD_GET(x)                  (((x) & TOP_TOP_INTH2PAD_MASK) >> TOP_TOP_INTH2PAD_LSB)
#define TOP_TOP_INTH2PAD_SET(x)                  (((x) << TOP_TOP_INTH2PAD_LSB) & TOP_TOP_INTH2PAD_MASK)
#define TOP_TOP_PAD2GND_MSB                      2
#define TOP_TOP_PAD2GND_LSB                      2
#define TOP_TOP_PAD2GND_MASK                     0x00000004
#define TOP_TOP_PAD2GND_GET(x)                   (((x) & TOP_TOP_PAD2GND_MASK) >> TOP_TOP_PAD2GND_LSB)
#define TOP_TOP_PAD2GND_SET(x)                   (((x) << TOP_TOP_PAD2GND_LSB) & TOP_TOP_PAD2GND_MASK)
#define TOP_TOP_INT2GND_MSB                      1
#define TOP_TOP_INT2GND_LSB                      1
#define TOP_TOP_INT2GND_MASK                     0x00000002
#define TOP_TOP_INT2GND_GET(x)                   (((x) & TOP_TOP_INT2GND_MASK) >> TOP_TOP_INT2GND_LSB)
#define TOP_TOP_INT2GND_SET(x)                   (((x) << TOP_TOP_INT2GND_LSB) & TOP_TOP_INT2GND_MASK)
#define TOP_TOP_FORCE_XPAON_MSB                  0
#define TOP_TOP_FORCE_XPAON_LSB                  0
#define TOP_TOP_FORCE_XPAON_MASK                 0x00000001
#define TOP_TOP_FORCE_XPAON_GET(x)               (((x) & TOP_TOP_FORCE_XPAON_MASK) >> TOP_TOP_FORCE_XPAON_LSB)
#define TOP_TOP_FORCE_XPAON_SET(x)               (((x) << TOP_TOP_FORCE_XPAON_LSB) & TOP_TOP_FORCE_XPAON_MASK)


#ifndef __ASSEMBLER__

typedef struct top_reg_reg_s {
  volatile unsigned int top_gain;
  volatile unsigned int top_top;
} top_reg_reg_t;

#endif /* __ASSEMBLER__ */

#endif /* _TOP_REG_H_ */
