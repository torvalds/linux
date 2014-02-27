#ifndef _TXPC_REG_REG_H_
#define _TXPC_REG_REG_H_

#define TXPC_TXPC_ADDRESS                        0x00000000
#define TXPC_TXPC_OFFSET                         0x00000000
#define TXPC_TXPC_SELINTPD_MSB                   31
#define TXPC_TXPC_SELINTPD_LSB                   31
#define TXPC_TXPC_SELINTPD_MASK                  0x80000000
#define TXPC_TXPC_SELINTPD_GET(x)                (((x) & TXPC_TXPC_SELINTPD_MASK) >> TXPC_TXPC_SELINTPD_LSB)
#define TXPC_TXPC_SELINTPD_SET(x)                (((x) << TXPC_TXPC_SELINTPD_LSB) & TXPC_TXPC_SELINTPD_MASK)
#define TXPC_TXPC_TEST_MSB                       30
#define TXPC_TXPC_TEST_LSB                       30
#define TXPC_TXPC_TEST_MASK                      0x40000000
#define TXPC_TXPC_TEST_GET(x)                    (((x) & TXPC_TXPC_TEST_MASK) >> TXPC_TXPC_TEST_LSB)
#define TXPC_TXPC_TEST_SET(x)                    (((x) << TXPC_TXPC_TEST_LSB) & TXPC_TXPC_TEST_MASK)
#define TXPC_TXPC_TESTGAIN_MSB                   29
#define TXPC_TXPC_TESTGAIN_LSB                   28
#define TXPC_TXPC_TESTGAIN_MASK                  0x30000000
#define TXPC_TXPC_TESTGAIN_GET(x)                (((x) & TXPC_TXPC_TESTGAIN_MASK) >> TXPC_TXPC_TESTGAIN_LSB)
#define TXPC_TXPC_TESTGAIN_SET(x)                (((x) << TXPC_TXPC_TESTGAIN_LSB) & TXPC_TXPC_TESTGAIN_MASK)
#define TXPC_TXPC_TESTDAC_MSB                    27
#define TXPC_TXPC_TESTDAC_LSB                    22
#define TXPC_TXPC_TESTDAC_MASK                   0x0fc00000
#define TXPC_TXPC_TESTDAC_GET(x)                 (((x) & TXPC_TXPC_TESTDAC_MASK) >> TXPC_TXPC_TESTDAC_LSB)
#define TXPC_TXPC_TESTDAC_SET(x)                 (((x) << TXPC_TXPC_TESTDAC_LSB) & TXPC_TXPC_TESTDAC_MASK)
#define TXPC_TXPC_TESTPWDPC_MSB                  21
#define TXPC_TXPC_TESTPWDPC_LSB                  21
#define TXPC_TXPC_TESTPWDPC_MASK                 0x00200000
#define TXPC_TXPC_TESTPWDPC_GET(x)               (((x) & TXPC_TXPC_TESTPWDPC_MASK) >> TXPC_TXPC_TESTPWDPC_LSB)
#define TXPC_TXPC_TESTPWDPC_SET(x)               (((x) << TXPC_TXPC_TESTPWDPC_LSB) & TXPC_TXPC_TESTPWDPC_MASK)
#define TXPC_TXPC_CURHALF_MSB                    20
#define TXPC_TXPC_CURHALF_LSB                    20
#define TXPC_TXPC_CURHALF_MASK                   0x00100000
#define TXPC_TXPC_CURHALF_GET(x)                 (((x) & TXPC_TXPC_CURHALF_MASK) >> TXPC_TXPC_CURHALF_LSB)
#define TXPC_TXPC_CURHALF_SET(x)                 (((x) << TXPC_TXPC_CURHALF_LSB) & TXPC_TXPC_CURHALF_MASK)
#define TXPC_TXPC_NEGOUT_MSB                     19
#define TXPC_TXPC_NEGOUT_LSB                     19
#define TXPC_TXPC_NEGOUT_MASK                    0x00080000
#define TXPC_TXPC_NEGOUT_GET(x)                  (((x) & TXPC_TXPC_NEGOUT_MASK) >> TXPC_TXPC_NEGOUT_LSB)
#define TXPC_TXPC_NEGOUT_SET(x)                  (((x) << TXPC_TXPC_NEGOUT_LSB) & TXPC_TXPC_NEGOUT_MASK)
#define TXPC_TXPC_CLKDELAY_MSB                   18
#define TXPC_TXPC_CLKDELAY_LSB                   18
#define TXPC_TXPC_CLKDELAY_MASK                  0x00040000
#define TXPC_TXPC_CLKDELAY_GET(x)                (((x) & TXPC_TXPC_CLKDELAY_MASK) >> TXPC_TXPC_CLKDELAY_LSB)
#define TXPC_TXPC_CLKDELAY_SET(x)                (((x) << TXPC_TXPC_CLKDELAY_LSB) & TXPC_TXPC_CLKDELAY_MASK)
#define TXPC_TXPC_SELMODREF_MSB                  17
#define TXPC_TXPC_SELMODREF_LSB                  17
#define TXPC_TXPC_SELMODREF_MASK                 0x00020000
#define TXPC_TXPC_SELMODREF_GET(x)               (((x) & TXPC_TXPC_SELMODREF_MASK) >> TXPC_TXPC_SELMODREF_LSB)
#define TXPC_TXPC_SELMODREF_SET(x)               (((x) << TXPC_TXPC_SELMODREF_LSB) & TXPC_TXPC_SELMODREF_MASK)
#define TXPC_TXPC_SELCMOUT_MSB                   16
#define TXPC_TXPC_SELCMOUT_LSB                   16
#define TXPC_TXPC_SELCMOUT_MASK                  0x00010000
#define TXPC_TXPC_SELCMOUT_GET(x)                (((x) & TXPC_TXPC_SELCMOUT_MASK) >> TXPC_TXPC_SELCMOUT_LSB)
#define TXPC_TXPC_SELCMOUT_SET(x)                (((x) << TXPC_TXPC_SELCMOUT_LSB) & TXPC_TXPC_SELCMOUT_MASK)
#define TXPC_TXPC_TSMODE_MSB                     15
#define TXPC_TXPC_TSMODE_LSB                     14
#define TXPC_TXPC_TSMODE_MASK                    0x0000c000
#define TXPC_TXPC_TSMODE_GET(x)                  (((x) & TXPC_TXPC_TSMODE_MASK) >> TXPC_TXPC_TSMODE_LSB)
#define TXPC_TXPC_TSMODE_SET(x)                  (((x) << TXPC_TXPC_TSMODE_LSB) & TXPC_TXPC_TSMODE_MASK)
#define TXPC_TXPC_N_MSB                          13
#define TXPC_TXPC_N_LSB                          6
#define TXPC_TXPC_N_MASK                         0x00003fc0
#define TXPC_TXPC_N_GET(x)                       (((x) & TXPC_TXPC_N_MASK) >> TXPC_TXPC_N_LSB)
#define TXPC_TXPC_N_SET(x)                       (((x) << TXPC_TXPC_N_LSB) & TXPC_TXPC_N_MASK)
#define TXPC_TXPC_ON1STSYNTHON_MSB               5
#define TXPC_TXPC_ON1STSYNTHON_LSB               5
#define TXPC_TXPC_ON1STSYNTHON_MASK              0x00000020
#define TXPC_TXPC_ON1STSYNTHON_GET(x)            (((x) & TXPC_TXPC_ON1STSYNTHON_MASK) >> TXPC_TXPC_ON1STSYNTHON_LSB)
#define TXPC_TXPC_ON1STSYNTHON_SET(x)            (((x) << TXPC_TXPC_ON1STSYNTHON_LSB) & TXPC_TXPC_ON1STSYNTHON_MASK)
#define TXPC_TXPC_SELINIT_MSB                    4
#define TXPC_TXPC_SELINIT_LSB                    3
#define TXPC_TXPC_SELINIT_MASK                   0x00000018
#define TXPC_TXPC_SELINIT_GET(x)                 (((x) & TXPC_TXPC_SELINIT_MASK) >> TXPC_TXPC_SELINIT_LSB)
#define TXPC_TXPC_SELINIT_SET(x)                 (((x) << TXPC_TXPC_SELINIT_LSB) & TXPC_TXPC_SELINIT_MASK)
#define TXPC_TXPC_SELCOUNT_MSB                   2
#define TXPC_TXPC_SELCOUNT_LSB                   2
#define TXPC_TXPC_SELCOUNT_MASK                  0x00000004
#define TXPC_TXPC_SELCOUNT_GET(x)                (((x) & TXPC_TXPC_SELCOUNT_MASK) >> TXPC_TXPC_SELCOUNT_LSB)
#define TXPC_TXPC_SELCOUNT_SET(x)                (((x) << TXPC_TXPC_SELCOUNT_LSB) & TXPC_TXPC_SELCOUNT_MASK)
#define TXPC_TXPC_ATBSEL_MSB                     1
#define TXPC_TXPC_ATBSEL_LSB                     0
#define TXPC_TXPC_ATBSEL_MASK                    0x00000003
#define TXPC_TXPC_ATBSEL_GET(x)                  (((x) & TXPC_TXPC_ATBSEL_MASK) >> TXPC_TXPC_ATBSEL_LSB)
#define TXPC_TXPC_ATBSEL_SET(x)                  (((x) << TXPC_TXPC_ATBSEL_LSB) & TXPC_TXPC_ATBSEL_MASK)

#define TXPC_MISC_ADDRESS                        0x00000004
#define TXPC_MISC_OFFSET                         0x00000004
#define TXPC_MISC_FLIPBMODE_MSB                  31
#define TXPC_MISC_FLIPBMODE_LSB                  31
#define TXPC_MISC_FLIPBMODE_MASK                 0x80000000
#define TXPC_MISC_FLIPBMODE_GET(x)               (((x) & TXPC_MISC_FLIPBMODE_MASK) >> TXPC_MISC_FLIPBMODE_LSB)
#define TXPC_MISC_FLIPBMODE_SET(x)               (((x) << TXPC_MISC_FLIPBMODE_LSB) & TXPC_MISC_FLIPBMODE_MASK)
#define TXPC_MISC_LEVEL_MSB                      30
#define TXPC_MISC_LEVEL_LSB                      29
#define TXPC_MISC_LEVEL_MASK                     0x60000000
#define TXPC_MISC_LEVEL_GET(x)                   (((x) & TXPC_MISC_LEVEL_MASK) >> TXPC_MISC_LEVEL_LSB)
#define TXPC_MISC_LEVEL_SET(x)                   (((x) << TXPC_MISC_LEVEL_LSB) & TXPC_MISC_LEVEL_MASK)
#define TXPC_MISC_LDO_TEST_MODE_MSB              28
#define TXPC_MISC_LDO_TEST_MODE_LSB              28
#define TXPC_MISC_LDO_TEST_MODE_MASK             0x10000000
#define TXPC_MISC_LDO_TEST_MODE_GET(x)           (((x) & TXPC_MISC_LDO_TEST_MODE_MASK) >> TXPC_MISC_LDO_TEST_MODE_LSB)
#define TXPC_MISC_LDO_TEST_MODE_SET(x)           (((x) << TXPC_MISC_LDO_TEST_MODE_LSB) & TXPC_MISC_LDO_TEST_MODE_MASK)
#define TXPC_MISC_NOTCXODET_MSB                  27
#define TXPC_MISC_NOTCXODET_LSB                  27
#define TXPC_MISC_NOTCXODET_MASK                 0x08000000
#define TXPC_MISC_NOTCXODET_GET(x)               (((x) & TXPC_MISC_NOTCXODET_MASK) >> TXPC_MISC_NOTCXODET_LSB)
#define TXPC_MISC_NOTCXODET_SET(x)               (((x) << TXPC_MISC_NOTCXODET_LSB) & TXPC_MISC_NOTCXODET_MASK)
#define TXPC_MISC_PWDCLKIND_MSB                  26
#define TXPC_MISC_PWDCLKIND_LSB                  26
#define TXPC_MISC_PWDCLKIND_MASK                 0x04000000
#define TXPC_MISC_PWDCLKIND_GET(x)               (((x) & TXPC_MISC_PWDCLKIND_MASK) >> TXPC_MISC_PWDCLKIND_LSB)
#define TXPC_MISC_PWDCLKIND_SET(x)               (((x) << TXPC_MISC_PWDCLKIND_LSB) & TXPC_MISC_PWDCLKIND_MASK)
#define TXPC_MISC_PWDXINPAD_MSB                  25
#define TXPC_MISC_PWDXINPAD_LSB                  25
#define TXPC_MISC_PWDXINPAD_MASK                 0x02000000
#define TXPC_MISC_PWDXINPAD_GET(x)               (((x) & TXPC_MISC_PWDXINPAD_MASK) >> TXPC_MISC_PWDXINPAD_LSB)
#define TXPC_MISC_PWDXINPAD_SET(x)               (((x) << TXPC_MISC_PWDXINPAD_LSB) & TXPC_MISC_PWDXINPAD_MASK)
#define TXPC_MISC_LOCALBIAS_MSB                  24
#define TXPC_MISC_LOCALBIAS_LSB                  24
#define TXPC_MISC_LOCALBIAS_MASK                 0x01000000
#define TXPC_MISC_LOCALBIAS_GET(x)               (((x) & TXPC_MISC_LOCALBIAS_MASK) >> TXPC_MISC_LOCALBIAS_LSB)
#define TXPC_MISC_LOCALBIAS_SET(x)               (((x) << TXPC_MISC_LOCALBIAS_LSB) & TXPC_MISC_LOCALBIAS_MASK)
#define TXPC_MISC_LOCALBIAS2X_MSB                23
#define TXPC_MISC_LOCALBIAS2X_LSB                23
#define TXPC_MISC_LOCALBIAS2X_MASK               0x00800000
#define TXPC_MISC_LOCALBIAS2X_GET(x)             (((x) & TXPC_MISC_LOCALBIAS2X_MASK) >> TXPC_MISC_LOCALBIAS2X_LSB)
#define TXPC_MISC_LOCALBIAS2X_SET(x)             (((x) << TXPC_MISC_LOCALBIAS2X_LSB) & TXPC_MISC_LOCALBIAS2X_MASK)
#define TXPC_MISC_SELTSP_MSB                     22
#define TXPC_MISC_SELTSP_LSB                     22
#define TXPC_MISC_SELTSP_MASK                    0x00400000
#define TXPC_MISC_SELTSP_GET(x)                  (((x) & TXPC_MISC_SELTSP_MASK) >> TXPC_MISC_SELTSP_LSB)
#define TXPC_MISC_SELTSP_SET(x)                  (((x) << TXPC_MISC_SELTSP_LSB) & TXPC_MISC_SELTSP_MASK)
#define TXPC_MISC_SELTSN_MSB                     21
#define TXPC_MISC_SELTSN_LSB                     21
#define TXPC_MISC_SELTSN_MASK                    0x00200000
#define TXPC_MISC_SELTSN_GET(x)                  (((x) & TXPC_MISC_SELTSN_MASK) >> TXPC_MISC_SELTSN_LSB)
#define TXPC_MISC_SELTSN_SET(x)                  (((x) << TXPC_MISC_SELTSN_LSB) & TXPC_MISC_SELTSN_MASK)
#define TXPC_MISC_SPARE_A_MSB                    20
#define TXPC_MISC_SPARE_A_LSB                    18
#define TXPC_MISC_SPARE_A_MASK                   0x001c0000
#define TXPC_MISC_SPARE_A_GET(x)                 (((x) & TXPC_MISC_SPARE_A_MASK) >> TXPC_MISC_SPARE_A_LSB)
#define TXPC_MISC_SPARE_A_SET(x)                 (((x) << TXPC_MISC_SPARE_A_LSB) & TXPC_MISC_SPARE_A_MASK)
#define TXPC_MISC_DECOUT_MSB                     17
#define TXPC_MISC_DECOUT_LSB                     8
#define TXPC_MISC_DECOUT_MASK                    0x0003ff00
#define TXPC_MISC_DECOUT_GET(x)                  (((x) & TXPC_MISC_DECOUT_MASK) >> TXPC_MISC_DECOUT_LSB)
#define TXPC_MISC_DECOUT_SET(x)                  (((x) << TXPC_MISC_DECOUT_LSB) & TXPC_MISC_DECOUT_MASK)
#define TXPC_MISC_XTALDIV_MSB                    7
#define TXPC_MISC_XTALDIV_LSB                    6
#define TXPC_MISC_XTALDIV_MASK                   0x000000c0
#define TXPC_MISC_XTALDIV_GET(x)                 (((x) & TXPC_MISC_XTALDIV_MASK) >> TXPC_MISC_XTALDIV_LSB)
#define TXPC_MISC_XTALDIV_SET(x)                 (((x) << TXPC_MISC_XTALDIV_LSB) & TXPC_MISC_XTALDIV_MASK)
#define TXPC_MISC_SPARE_MSB                      5
#define TXPC_MISC_SPARE_LSB                      0
#define TXPC_MISC_SPARE_MASK                     0x0000003f
#define TXPC_MISC_SPARE_GET(x)                   (((x) & TXPC_MISC_SPARE_MASK) >> TXPC_MISC_SPARE_LSB)
#define TXPC_MISC_SPARE_SET(x)                   (((x) << TXPC_MISC_SPARE_LSB) & TXPC_MISC_SPARE_MASK)


#ifndef __ASSEMBLER__

typedef struct txpc_reg_reg_s {
  volatile unsigned int txpc_txpc;
  volatile unsigned int txpc_misc;
} txpc_reg_reg_t;

#endif /* __ASSEMBLER__ */

#endif /* _TXPC_REG_H_ */
