#ifndef _RF5G_REG_REG_H_
#define _RF5G_REG_REG_H_

#define RF5G_RF5G1_ADDRESS                       0x00000000
#define RF5G_RF5G1_OFFSET                        0x00000000
#define RF5G_RF5G1_PDTXLO5_MSB                   31
#define RF5G_RF5G1_PDTXLO5_LSB                   31
#define RF5G_RF5G1_PDTXLO5_MASK                  0x80000000
#define RF5G_RF5G1_PDTXLO5_GET(x)                (((x) & RF5G_RF5G1_PDTXLO5_MASK) >> RF5G_RF5G1_PDTXLO5_LSB)
#define RF5G_RF5G1_PDTXLO5_SET(x)                (((x) << RF5G_RF5G1_PDTXLO5_LSB) & RF5G_RF5G1_PDTXLO5_MASK)
#define RF5G_RF5G1_PDTXMIX5_MSB                  30
#define RF5G_RF5G1_PDTXMIX5_LSB                  30
#define RF5G_RF5G1_PDTXMIX5_MASK                 0x40000000
#define RF5G_RF5G1_PDTXMIX5_GET(x)               (((x) & RF5G_RF5G1_PDTXMIX5_MASK) >> RF5G_RF5G1_PDTXMIX5_LSB)
#define RF5G_RF5G1_PDTXMIX5_SET(x)               (((x) << RF5G_RF5G1_PDTXMIX5_LSB) & RF5G_RF5G1_PDTXMIX5_MASK)
#define RF5G_RF5G1_PDTXBUF5_MSB                  29
#define RF5G_RF5G1_PDTXBUF5_LSB                  29
#define RF5G_RF5G1_PDTXBUF5_MASK                 0x20000000
#define RF5G_RF5G1_PDTXBUF5_GET(x)               (((x) & RF5G_RF5G1_PDTXBUF5_MASK) >> RF5G_RF5G1_PDTXBUF5_LSB)
#define RF5G_RF5G1_PDTXBUF5_SET(x)               (((x) << RF5G_RF5G1_PDTXBUF5_LSB) & RF5G_RF5G1_PDTXBUF5_MASK)
#define RF5G_RF5G1_PDPADRV5_MSB                  28
#define RF5G_RF5G1_PDPADRV5_LSB                  28
#define RF5G_RF5G1_PDPADRV5_MASK                 0x10000000
#define RF5G_RF5G1_PDPADRV5_GET(x)               (((x) & RF5G_RF5G1_PDPADRV5_MASK) >> RF5G_RF5G1_PDPADRV5_LSB)
#define RF5G_RF5G1_PDPADRV5_SET(x)               (((x) << RF5G_RF5G1_PDPADRV5_LSB) & RF5G_RF5G1_PDPADRV5_MASK)
#define RF5G_RF5G1_PDPAOUT5_MSB                  27
#define RF5G_RF5G1_PDPAOUT5_LSB                  27
#define RF5G_RF5G1_PDPAOUT5_MASK                 0x08000000
#define RF5G_RF5G1_PDPAOUT5_GET(x)               (((x) & RF5G_RF5G1_PDPAOUT5_MASK) >> RF5G_RF5G1_PDPAOUT5_LSB)
#define RF5G_RF5G1_PDPAOUT5_SET(x)               (((x) << RF5G_RF5G1_PDPAOUT5_LSB) & RF5G_RF5G1_PDPAOUT5_MASK)
#define RF5G_RF5G1_TUNE_PADRV5_MSB               26
#define RF5G_RF5G1_TUNE_PADRV5_LSB               24
#define RF5G_RF5G1_TUNE_PADRV5_MASK              0x07000000
#define RF5G_RF5G1_TUNE_PADRV5_GET(x)            (((x) & RF5G_RF5G1_TUNE_PADRV5_MASK) >> RF5G_RF5G1_TUNE_PADRV5_LSB)
#define RF5G_RF5G1_TUNE_PADRV5_SET(x)            (((x) << RF5G_RF5G1_TUNE_PADRV5_LSB) & RF5G_RF5G1_TUNE_PADRV5_MASK)
#define RF5G_RF5G1_PWDTXPKD_MSB                  23
#define RF5G_RF5G1_PWDTXPKD_LSB                  21
#define RF5G_RF5G1_PWDTXPKD_MASK                 0x00e00000
#define RF5G_RF5G1_PWDTXPKD_GET(x)               (((x) & RF5G_RF5G1_PWDTXPKD_MASK) >> RF5G_RF5G1_PWDTXPKD_LSB)
#define RF5G_RF5G1_PWDTXPKD_SET(x)               (((x) << RF5G_RF5G1_PWDTXPKD_LSB) & RF5G_RF5G1_PWDTXPKD_MASK)
#define RF5G_RF5G1_DB5_MSB                       20
#define RF5G_RF5G1_DB5_LSB                       18
#define RF5G_RF5G1_DB5_MASK                      0x001c0000
#define RF5G_RF5G1_DB5_GET(x)                    (((x) & RF5G_RF5G1_DB5_MASK) >> RF5G_RF5G1_DB5_LSB)
#define RF5G_RF5G1_DB5_SET(x)                    (((x) << RF5G_RF5G1_DB5_LSB) & RF5G_RF5G1_DB5_MASK)
#define RF5G_RF5G1_OB5_MSB                       17
#define RF5G_RF5G1_OB5_LSB                       15
#define RF5G_RF5G1_OB5_MASK                      0x00038000
#define RF5G_RF5G1_OB5_GET(x)                    (((x) & RF5G_RF5G1_OB5_MASK) >> RF5G_RF5G1_OB5_LSB)
#define RF5G_RF5G1_OB5_SET(x)                    (((x) << RF5G_RF5G1_OB5_LSB) & RF5G_RF5G1_OB5_MASK)
#define RF5G_RF5G1_TX5_ATB_SEL_MSB               14
#define RF5G_RF5G1_TX5_ATB_SEL_LSB               12
#define RF5G_RF5G1_TX5_ATB_SEL_MASK              0x00007000
#define RF5G_RF5G1_TX5_ATB_SEL_GET(x)            (((x) & RF5G_RF5G1_TX5_ATB_SEL_MASK) >> RF5G_RF5G1_TX5_ATB_SEL_LSB)
#define RF5G_RF5G1_TX5_ATB_SEL_SET(x)            (((x) << RF5G_RF5G1_TX5_ATB_SEL_LSB) & RF5G_RF5G1_TX5_ATB_SEL_MASK)
#define RF5G_RF5G1_PDLO5DIV_MSB                  11
#define RF5G_RF5G1_PDLO5DIV_LSB                  11
#define RF5G_RF5G1_PDLO5DIV_MASK                 0x00000800
#define RF5G_RF5G1_PDLO5DIV_GET(x)               (((x) & RF5G_RF5G1_PDLO5DIV_MASK) >> RF5G_RF5G1_PDLO5DIV_LSB)
#define RF5G_RF5G1_PDLO5DIV_SET(x)               (((x) << RF5G_RF5G1_PDLO5DIV_LSB) & RF5G_RF5G1_PDLO5DIV_MASK)
#define RF5G_RF5G1_PDLO5MIX_MSB                  10
#define RF5G_RF5G1_PDLO5MIX_LSB                  10
#define RF5G_RF5G1_PDLO5MIX_MASK                 0x00000400
#define RF5G_RF5G1_PDLO5MIX_GET(x)               (((x) & RF5G_RF5G1_PDLO5MIX_MASK) >> RF5G_RF5G1_PDLO5MIX_LSB)
#define RF5G_RF5G1_PDLO5MIX_SET(x)               (((x) << RF5G_RF5G1_PDLO5MIX_LSB) & RF5G_RF5G1_PDLO5MIX_MASK)
#define RF5G_RF5G1_PDQBUF5_MSB                   9
#define RF5G_RF5G1_PDQBUF5_LSB                   9
#define RF5G_RF5G1_PDQBUF5_MASK                  0x00000200
#define RF5G_RF5G1_PDQBUF5_GET(x)                (((x) & RF5G_RF5G1_PDQBUF5_MASK) >> RF5G_RF5G1_PDQBUF5_LSB)
#define RF5G_RF5G1_PDQBUF5_SET(x)                (((x) << RF5G_RF5G1_PDQBUF5_LSB) & RF5G_RF5G1_PDQBUF5_MASK)
#define RF5G_RF5G1_PDLO5AGC_MSB                  8
#define RF5G_RF5G1_PDLO5AGC_LSB                  8
#define RF5G_RF5G1_PDLO5AGC_MASK                 0x00000100
#define RF5G_RF5G1_PDLO5AGC_GET(x)               (((x) & RF5G_RF5G1_PDLO5AGC_MASK) >> RF5G_RF5G1_PDLO5AGC_LSB)
#define RF5G_RF5G1_PDLO5AGC_SET(x)               (((x) << RF5G_RF5G1_PDLO5AGC_LSB) & RF5G_RF5G1_PDLO5AGC_MASK)
#define RF5G_RF5G1_PDREGLO5_MSB                  7
#define RF5G_RF5G1_PDREGLO5_LSB                  7
#define RF5G_RF5G1_PDREGLO5_MASK                 0x00000080
#define RF5G_RF5G1_PDREGLO5_GET(x)               (((x) & RF5G_RF5G1_PDREGLO5_MASK) >> RF5G_RF5G1_PDREGLO5_LSB)
#define RF5G_RF5G1_PDREGLO5_SET(x)               (((x) << RF5G_RF5G1_PDREGLO5_LSB) & RF5G_RF5G1_PDREGLO5_MASK)
#define RF5G_RF5G1_LO5_ATB_SEL_MSB               6
#define RF5G_RF5G1_LO5_ATB_SEL_LSB               4
#define RF5G_RF5G1_LO5_ATB_SEL_MASK              0x00000070
#define RF5G_RF5G1_LO5_ATB_SEL_GET(x)            (((x) & RF5G_RF5G1_LO5_ATB_SEL_MASK) >> RF5G_RF5G1_LO5_ATB_SEL_LSB)
#define RF5G_RF5G1_LO5_ATB_SEL_SET(x)            (((x) << RF5G_RF5G1_LO5_ATB_SEL_LSB) & RF5G_RF5G1_LO5_ATB_SEL_MASK)
#define RF5G_RF5G1_LO5CONTROL_MSB                3
#define RF5G_RF5G1_LO5CONTROL_LSB                3
#define RF5G_RF5G1_LO5CONTROL_MASK               0x00000008
#define RF5G_RF5G1_LO5CONTROL_GET(x)             (((x) & RF5G_RF5G1_LO5CONTROL_MASK) >> RF5G_RF5G1_LO5CONTROL_LSB)
#define RF5G_RF5G1_LO5CONTROL_SET(x)             (((x) << RF5G_RF5G1_LO5CONTROL_LSB) & RF5G_RF5G1_LO5CONTROL_MASK)
#define RF5G_RF5G1_REGLO_BYPASS5_MSB             2
#define RF5G_RF5G1_REGLO_BYPASS5_LSB             2
#define RF5G_RF5G1_REGLO_BYPASS5_MASK            0x00000004
#define RF5G_RF5G1_REGLO_BYPASS5_GET(x)          (((x) & RF5G_RF5G1_REGLO_BYPASS5_MASK) >> RF5G_RF5G1_REGLO_BYPASS5_LSB)
#define RF5G_RF5G1_REGLO_BYPASS5_SET(x)          (((x) << RF5G_RF5G1_REGLO_BYPASS5_LSB) & RF5G_RF5G1_REGLO_BYPASS5_MASK)
#define RF5G_RF5G1_SPARE_MSB                     1
#define RF5G_RF5G1_SPARE_LSB                     0
#define RF5G_RF5G1_SPARE_MASK                    0x00000003
#define RF5G_RF5G1_SPARE_GET(x)                  (((x) & RF5G_RF5G1_SPARE_MASK) >> RF5G_RF5G1_SPARE_LSB)
#define RF5G_RF5G1_SPARE_SET(x)                  (((x) << RF5G_RF5G1_SPARE_LSB) & RF5G_RF5G1_SPARE_MASK)

#define RF5G_RF5G2_ADDRESS                       0x00000004
#define RF5G_RF5G2_OFFSET                        0x00000004
#define RF5G_RF5G2_AGCLO_B_MSB                   31
#define RF5G_RF5G2_AGCLO_B_LSB                   29
#define RF5G_RF5G2_AGCLO_B_MASK                  0xe0000000
#define RF5G_RF5G2_AGCLO_B_GET(x)                (((x) & RF5G_RF5G2_AGCLO_B_MASK) >> RF5G_RF5G2_AGCLO_B_LSB)
#define RF5G_RF5G2_AGCLO_B_SET(x)                (((x) << RF5G_RF5G2_AGCLO_B_LSB) & RF5G_RF5G2_AGCLO_B_MASK)
#define RF5G_RF5G2_RX5_ATB_SEL_MSB               28
#define RF5G_RF5G2_RX5_ATB_SEL_LSB               26
#define RF5G_RF5G2_RX5_ATB_SEL_MASK              0x1c000000
#define RF5G_RF5G2_RX5_ATB_SEL_GET(x)            (((x) & RF5G_RF5G2_RX5_ATB_SEL_MASK) >> RF5G_RF5G2_RX5_ATB_SEL_LSB)
#define RF5G_RF5G2_RX5_ATB_SEL_SET(x)            (((x) << RF5G_RF5G2_RX5_ATB_SEL_LSB) & RF5G_RF5G2_RX5_ATB_SEL_MASK)
#define RF5G_RF5G2_PDCMOSLO5_MSB                 25
#define RF5G_RF5G2_PDCMOSLO5_LSB                 25
#define RF5G_RF5G2_PDCMOSLO5_MASK                0x02000000
#define RF5G_RF5G2_PDCMOSLO5_GET(x)              (((x) & RF5G_RF5G2_PDCMOSLO5_MASK) >> RF5G_RF5G2_PDCMOSLO5_LSB)
#define RF5G_RF5G2_PDCMOSLO5_SET(x)              (((x) << RF5G_RF5G2_PDCMOSLO5_LSB) & RF5G_RF5G2_PDCMOSLO5_MASK)
#define RF5G_RF5G2_PDVGM5_MSB                    24
#define RF5G_RF5G2_PDVGM5_LSB                    24
#define RF5G_RF5G2_PDVGM5_MASK                   0x01000000
#define RF5G_RF5G2_PDVGM5_GET(x)                 (((x) & RF5G_RF5G2_PDVGM5_MASK) >> RF5G_RF5G2_PDVGM5_LSB)
#define RF5G_RF5G2_PDVGM5_SET(x)                 (((x) << RF5G_RF5G2_PDVGM5_LSB) & RF5G_RF5G2_PDVGM5_MASK)
#define RF5G_RF5G2_PDCSLNA5_MSB                  23
#define RF5G_RF5G2_PDCSLNA5_LSB                  23
#define RF5G_RF5G2_PDCSLNA5_MASK                 0x00800000
#define RF5G_RF5G2_PDCSLNA5_GET(x)               (((x) & RF5G_RF5G2_PDCSLNA5_MASK) >> RF5G_RF5G2_PDCSLNA5_LSB)
#define RF5G_RF5G2_PDCSLNA5_SET(x)               (((x) << RF5G_RF5G2_PDCSLNA5_LSB) & RF5G_RF5G2_PDCSLNA5_MASK)
#define RF5G_RF5G2_PDRFVGA5_MSB                  22
#define RF5G_RF5G2_PDRFVGA5_LSB                  22
#define RF5G_RF5G2_PDRFVGA5_MASK                 0x00400000
#define RF5G_RF5G2_PDRFVGA5_GET(x)               (((x) & RF5G_RF5G2_PDRFVGA5_MASK) >> RF5G_RF5G2_PDRFVGA5_LSB)
#define RF5G_RF5G2_PDRFVGA5_SET(x)               (((x) << RF5G_RF5G2_PDRFVGA5_LSB) & RF5G_RF5G2_PDRFVGA5_MASK)
#define RF5G_RF5G2_PDREGFE5_MSB                  21
#define RF5G_RF5G2_PDREGFE5_LSB                  21
#define RF5G_RF5G2_PDREGFE5_MASK                 0x00200000
#define RF5G_RF5G2_PDREGFE5_GET(x)               (((x) & RF5G_RF5G2_PDREGFE5_MASK) >> RF5G_RF5G2_PDREGFE5_LSB)
#define RF5G_RF5G2_PDREGFE5_SET(x)               (((x) << RF5G_RF5G2_PDREGFE5_LSB) & RF5G_RF5G2_PDREGFE5_MASK)
#define RF5G_RF5G2_TUNE_RFVGA5_MSB               20
#define RF5G_RF5G2_TUNE_RFVGA5_LSB               18
#define RF5G_RF5G2_TUNE_RFVGA5_MASK              0x001c0000
#define RF5G_RF5G2_TUNE_RFVGA5_GET(x)            (((x) & RF5G_RF5G2_TUNE_RFVGA5_MASK) >> RF5G_RF5G2_TUNE_RFVGA5_LSB)
#define RF5G_RF5G2_TUNE_RFVGA5_SET(x)            (((x) << RF5G_RF5G2_TUNE_RFVGA5_LSB) & RF5G_RF5G2_TUNE_RFVGA5_MASK)
#define RF5G_RF5G2_BRFVGA5_MSB                   17
#define RF5G_RF5G2_BRFVGA5_LSB                   15
#define RF5G_RF5G2_BRFVGA5_MASK                  0x00038000
#define RF5G_RF5G2_BRFVGA5_GET(x)                (((x) & RF5G_RF5G2_BRFVGA5_MASK) >> RF5G_RF5G2_BRFVGA5_LSB)
#define RF5G_RF5G2_BRFVGA5_SET(x)                (((x) << RF5G_RF5G2_BRFVGA5_LSB) & RF5G_RF5G2_BRFVGA5_MASK)
#define RF5G_RF5G2_BCSLNA5_MSB                   14
#define RF5G_RF5G2_BCSLNA5_LSB                   12
#define RF5G_RF5G2_BCSLNA5_MASK                  0x00007000
#define RF5G_RF5G2_BCSLNA5_GET(x)                (((x) & RF5G_RF5G2_BCSLNA5_MASK) >> RF5G_RF5G2_BCSLNA5_LSB)
#define RF5G_RF5G2_BCSLNA5_SET(x)                (((x) << RF5G_RF5G2_BCSLNA5_LSB) & RF5G_RF5G2_BCSLNA5_MASK)
#define RF5G_RF5G2_BVGM5_MSB                     11
#define RF5G_RF5G2_BVGM5_LSB                     9
#define RF5G_RF5G2_BVGM5_MASK                    0x00000e00
#define RF5G_RF5G2_BVGM5_GET(x)                  (((x) & RF5G_RF5G2_BVGM5_MASK) >> RF5G_RF5G2_BVGM5_LSB)
#define RF5G_RF5G2_BVGM5_SET(x)                  (((x) << RF5G_RF5G2_BVGM5_LSB) & RF5G_RF5G2_BVGM5_MASK)
#define RF5G_RF5G2_REGFE_BYPASS5_MSB             8
#define RF5G_RF5G2_REGFE_BYPASS5_LSB             8
#define RF5G_RF5G2_REGFE_BYPASS5_MASK            0x00000100
#define RF5G_RF5G2_REGFE_BYPASS5_GET(x)          (((x) & RF5G_RF5G2_REGFE_BYPASS5_MASK) >> RF5G_RF5G2_REGFE_BYPASS5_LSB)
#define RF5G_RF5G2_REGFE_BYPASS5_SET(x)          (((x) << RF5G_RF5G2_REGFE_BYPASS5_LSB) & RF5G_RF5G2_REGFE_BYPASS5_MASK)
#define RF5G_RF5G2_LNA5_ATTENMODE_MSB            7
#define RF5G_RF5G2_LNA5_ATTENMODE_LSB            6
#define RF5G_RF5G2_LNA5_ATTENMODE_MASK           0x000000c0
#define RF5G_RF5G2_LNA5_ATTENMODE_GET(x)         (((x) & RF5G_RF5G2_LNA5_ATTENMODE_MASK) >> RF5G_RF5G2_LNA5_ATTENMODE_LSB)
#define RF5G_RF5G2_LNA5_ATTENMODE_SET(x)         (((x) << RF5G_RF5G2_LNA5_ATTENMODE_LSB) & RF5G_RF5G2_LNA5_ATTENMODE_MASK)
#define RF5G_RF5G2_ENABLE_PCA_MSB                5
#define RF5G_RF5G2_ENABLE_PCA_LSB                5
#define RF5G_RF5G2_ENABLE_PCA_MASK               0x00000020
#define RF5G_RF5G2_ENABLE_PCA_GET(x)             (((x) & RF5G_RF5G2_ENABLE_PCA_MASK) >> RF5G_RF5G2_ENABLE_PCA_LSB)
#define RF5G_RF5G2_ENABLE_PCA_SET(x)             (((x) << RF5G_RF5G2_ENABLE_PCA_LSB) & RF5G_RF5G2_ENABLE_PCA_MASK)
#define RF5G_RF5G2_TUNE_LO_MSB                   4
#define RF5G_RF5G2_TUNE_LO_LSB                   2
#define RF5G_RF5G2_TUNE_LO_MASK                  0x0000001c
#define RF5G_RF5G2_TUNE_LO_GET(x)                (((x) & RF5G_RF5G2_TUNE_LO_MASK) >> RF5G_RF5G2_TUNE_LO_LSB)
#define RF5G_RF5G2_TUNE_LO_SET(x)                (((x) << RF5G_RF5G2_TUNE_LO_LSB) & RF5G_RF5G2_TUNE_LO_MASK)
#define RF5G_RF5G2_SPARE_MSB                     1
#define RF5G_RF5G2_SPARE_LSB                     0
#define RF5G_RF5G2_SPARE_MASK                    0x00000003
#define RF5G_RF5G2_SPARE_GET(x)                  (((x) & RF5G_RF5G2_SPARE_MASK) >> RF5G_RF5G2_SPARE_LSB)
#define RF5G_RF5G2_SPARE_SET(x)                  (((x) << RF5G_RF5G2_SPARE_LSB) & RF5G_RF5G2_SPARE_MASK)


#ifndef __ASSEMBLER__

typedef struct rf5g_reg_reg_s {
  volatile unsigned int rf5g_rf5g1;
  volatile unsigned int rf5g_rf5g2;
} rf5g_reg_reg_t;

#endif /* __ASSEMBLER__ */

#endif /* _RF5G_REG_H_ */
