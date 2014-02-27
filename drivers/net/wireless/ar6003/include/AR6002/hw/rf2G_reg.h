#ifndef _RF2G_REG_REG_H_
#define _RF2G_REG_REG_H_

#define RF2G_RF2G1_ADDRESS                       0x00000000
#define RF2G_RF2G1_OFFSET                        0x00000000
#define RF2G_RF2G1_BLNA1_MSB                     31
#define RF2G_RF2G1_BLNA1_LSB                     29
#define RF2G_RF2G1_BLNA1_MASK                    0xe0000000
#define RF2G_RF2G1_BLNA1_GET(x)                  (((x) & RF2G_RF2G1_BLNA1_MASK) >> RF2G_RF2G1_BLNA1_LSB)
#define RF2G_RF2G1_BLNA1_SET(x)                  (((x) << RF2G_RF2G1_BLNA1_LSB) & RF2G_RF2G1_BLNA1_MASK)
#define RF2G_RF2G1_BLNA1F_MSB                    28
#define RF2G_RF2G1_BLNA1F_LSB                    26
#define RF2G_RF2G1_BLNA1F_MASK                   0x1c000000
#define RF2G_RF2G1_BLNA1F_GET(x)                 (((x) & RF2G_RF2G1_BLNA1F_MASK) >> RF2G_RF2G1_BLNA1F_LSB)
#define RF2G_RF2G1_BLNA1F_SET(x)                 (((x) << RF2G_RF2G1_BLNA1F_LSB) & RF2G_RF2G1_BLNA1F_MASK)
#define RF2G_RF2G1_BLNA1BUF_MSB                  25
#define RF2G_RF2G1_BLNA1BUF_LSB                  23
#define RF2G_RF2G1_BLNA1BUF_MASK                 0x03800000
#define RF2G_RF2G1_BLNA1BUF_GET(x)               (((x) & RF2G_RF2G1_BLNA1BUF_MASK) >> RF2G_RF2G1_BLNA1BUF_LSB)
#define RF2G_RF2G1_BLNA1BUF_SET(x)               (((x) << RF2G_RF2G1_BLNA1BUF_LSB) & RF2G_RF2G1_BLNA1BUF_MASK)
#define RF2G_RF2G1_BLNA2_MSB                     22
#define RF2G_RF2G1_BLNA2_LSB                     20
#define RF2G_RF2G1_BLNA2_MASK                    0x00700000
#define RF2G_RF2G1_BLNA2_GET(x)                  (((x) & RF2G_RF2G1_BLNA2_MASK) >> RF2G_RF2G1_BLNA2_LSB)
#define RF2G_RF2G1_BLNA2_SET(x)                  (((x) << RF2G_RF2G1_BLNA2_LSB) & RF2G_RF2G1_BLNA2_MASK)
#define RF2G_RF2G1_DB_MSB                        19
#define RF2G_RF2G1_DB_LSB                        17
#define RF2G_RF2G1_DB_MASK                       0x000e0000
#define RF2G_RF2G1_DB_GET(x)                     (((x) & RF2G_RF2G1_DB_MASK) >> RF2G_RF2G1_DB_LSB)
#define RF2G_RF2G1_DB_SET(x)                     (((x) << RF2G_RF2G1_DB_LSB) & RF2G_RF2G1_DB_MASK)
#define RF2G_RF2G1_OB_MSB                        16
#define RF2G_RF2G1_OB_LSB                        14
#define RF2G_RF2G1_OB_MASK                       0x0001c000
#define RF2G_RF2G1_OB_GET(x)                     (((x) & RF2G_RF2G1_OB_MASK) >> RF2G_RF2G1_OB_LSB)
#define RF2G_RF2G1_OB_SET(x)                     (((x) << RF2G_RF2G1_OB_LSB) & RF2G_RF2G1_OB_MASK)
#define RF2G_RF2G1_FE_ATB_SEL_MSB                13
#define RF2G_RF2G1_FE_ATB_SEL_LSB                11
#define RF2G_RF2G1_FE_ATB_SEL_MASK               0x00003800
#define RF2G_RF2G1_FE_ATB_SEL_GET(x)             (((x) & RF2G_RF2G1_FE_ATB_SEL_MASK) >> RF2G_RF2G1_FE_ATB_SEL_LSB)
#define RF2G_RF2G1_FE_ATB_SEL_SET(x)             (((x) << RF2G_RF2G1_FE_ATB_SEL_LSB) & RF2G_RF2G1_FE_ATB_SEL_MASK)
#define RF2G_RF2G1_RF_ATB_SEL_MSB                10
#define RF2G_RF2G1_RF_ATB_SEL_LSB                8
#define RF2G_RF2G1_RF_ATB_SEL_MASK               0x00000700
#define RF2G_RF2G1_RF_ATB_SEL_GET(x)             (((x) & RF2G_RF2G1_RF_ATB_SEL_MASK) >> RF2G_RF2G1_RF_ATB_SEL_LSB)
#define RF2G_RF2G1_RF_ATB_SEL_SET(x)             (((x) << RF2G_RF2G1_RF_ATB_SEL_LSB) & RF2G_RF2G1_RF_ATB_SEL_MASK)
#define RF2G_RF2G1_SELLNA_MSB                    7
#define RF2G_RF2G1_SELLNA_LSB                    7
#define RF2G_RF2G1_SELLNA_MASK                   0x00000080
#define RF2G_RF2G1_SELLNA_GET(x)                 (((x) & RF2G_RF2G1_SELLNA_MASK) >> RF2G_RF2G1_SELLNA_LSB)
#define RF2G_RF2G1_SELLNA_SET(x)                 (((x) << RF2G_RF2G1_SELLNA_LSB) & RF2G_RF2G1_SELLNA_MASK)
#define RF2G_RF2G1_LOCONTROL_MSB                 6
#define RF2G_RF2G1_LOCONTROL_LSB                 6
#define RF2G_RF2G1_LOCONTROL_MASK                0x00000040
#define RF2G_RF2G1_LOCONTROL_GET(x)              (((x) & RF2G_RF2G1_LOCONTROL_MASK) >> RF2G_RF2G1_LOCONTROL_LSB)
#define RF2G_RF2G1_LOCONTROL_SET(x)              (((x) << RF2G_RF2G1_LOCONTROL_LSB) & RF2G_RF2G1_LOCONTROL_MASK)
#define RF2G_RF2G1_SHORTLNA2_MSB                 5
#define RF2G_RF2G1_SHORTLNA2_LSB                 5
#define RF2G_RF2G1_SHORTLNA2_MASK                0x00000020
#define RF2G_RF2G1_SHORTLNA2_GET(x)              (((x) & RF2G_RF2G1_SHORTLNA2_MASK) >> RF2G_RF2G1_SHORTLNA2_LSB)
#define RF2G_RF2G1_SHORTLNA2_SET(x)              (((x) << RF2G_RF2G1_SHORTLNA2_LSB) & RF2G_RF2G1_SHORTLNA2_MASK)
#define RF2G_RF2G1_SPARE_MSB                     4
#define RF2G_RF2G1_SPARE_LSB                     0
#define RF2G_RF2G1_SPARE_MASK                    0x0000001f
#define RF2G_RF2G1_SPARE_GET(x)                  (((x) & RF2G_RF2G1_SPARE_MASK) >> RF2G_RF2G1_SPARE_LSB)
#define RF2G_RF2G1_SPARE_SET(x)                  (((x) << RF2G_RF2G1_SPARE_LSB) & RF2G_RF2G1_SPARE_MASK)

#define RF2G_RF2G2_ADDRESS                       0x00000004
#define RF2G_RF2G2_OFFSET                        0x00000004
#define RF2G_RF2G2_PDCGLNA_MSB                   31
#define RF2G_RF2G2_PDCGLNA_LSB                   31
#define RF2G_RF2G2_PDCGLNA_MASK                  0x80000000
#define RF2G_RF2G2_PDCGLNA_GET(x)                (((x) & RF2G_RF2G2_PDCGLNA_MASK) >> RF2G_RF2G2_PDCGLNA_LSB)
#define RF2G_RF2G2_PDCGLNA_SET(x)                (((x) << RF2G_RF2G2_PDCGLNA_LSB) & RF2G_RF2G2_PDCGLNA_MASK)
#define RF2G_RF2G2_PDCGLNABUF_MSB                30
#define RF2G_RF2G2_PDCGLNABUF_LSB                30
#define RF2G_RF2G2_PDCGLNABUF_MASK               0x40000000
#define RF2G_RF2G2_PDCGLNABUF_GET(x)             (((x) & RF2G_RF2G2_PDCGLNABUF_MASK) >> RF2G_RF2G2_PDCGLNABUF_LSB)
#define RF2G_RF2G2_PDCGLNABUF_SET(x)             (((x) << RF2G_RF2G2_PDCGLNABUF_LSB) & RF2G_RF2G2_PDCGLNABUF_MASK)
#define RF2G_RF2G2_PDCSLNA_MSB                   29
#define RF2G_RF2G2_PDCSLNA_LSB                   29
#define RF2G_RF2G2_PDCSLNA_MASK                  0x20000000
#define RF2G_RF2G2_PDCSLNA_GET(x)                (((x) & RF2G_RF2G2_PDCSLNA_MASK) >> RF2G_RF2G2_PDCSLNA_LSB)
#define RF2G_RF2G2_PDCSLNA_SET(x)                (((x) << RF2G_RF2G2_PDCSLNA_LSB) & RF2G_RF2G2_PDCSLNA_MASK)
#define RF2G_RF2G2_PDDIV_MSB                     28
#define RF2G_RF2G2_PDDIV_LSB                     28
#define RF2G_RF2G2_PDDIV_MASK                    0x10000000
#define RF2G_RF2G2_PDDIV_GET(x)                  (((x) & RF2G_RF2G2_PDDIV_MASK) >> RF2G_RF2G2_PDDIV_LSB)
#define RF2G_RF2G2_PDDIV_SET(x)                  (((x) << RF2G_RF2G2_PDDIV_LSB) & RF2G_RF2G2_PDDIV_MASK)
#define RF2G_RF2G2_PDPADRV_MSB                   27
#define RF2G_RF2G2_PDPADRV_LSB                   27
#define RF2G_RF2G2_PDPADRV_MASK                  0x08000000
#define RF2G_RF2G2_PDPADRV_GET(x)                (((x) & RF2G_RF2G2_PDPADRV_MASK) >> RF2G_RF2G2_PDPADRV_LSB)
#define RF2G_RF2G2_PDPADRV_SET(x)                (((x) << RF2G_RF2G2_PDPADRV_LSB) & RF2G_RF2G2_PDPADRV_MASK)
#define RF2G_RF2G2_PDPAOUT_MSB                   26
#define RF2G_RF2G2_PDPAOUT_LSB                   26
#define RF2G_RF2G2_PDPAOUT_MASK                  0x04000000
#define RF2G_RF2G2_PDPAOUT_GET(x)                (((x) & RF2G_RF2G2_PDPAOUT_MASK) >> RF2G_RF2G2_PDPAOUT_LSB)
#define RF2G_RF2G2_PDPAOUT_SET(x)                (((x) << RF2G_RF2G2_PDPAOUT_LSB) & RF2G_RF2G2_PDPAOUT_MASK)
#define RF2G_RF2G2_PDREGLNA_MSB                  25
#define RF2G_RF2G2_PDREGLNA_LSB                  25
#define RF2G_RF2G2_PDREGLNA_MASK                 0x02000000
#define RF2G_RF2G2_PDREGLNA_GET(x)               (((x) & RF2G_RF2G2_PDREGLNA_MASK) >> RF2G_RF2G2_PDREGLNA_LSB)
#define RF2G_RF2G2_PDREGLNA_SET(x)               (((x) << RF2G_RF2G2_PDREGLNA_LSB) & RF2G_RF2G2_PDREGLNA_MASK)
#define RF2G_RF2G2_PDREGLO_MSB                   24
#define RF2G_RF2G2_PDREGLO_LSB                   24
#define RF2G_RF2G2_PDREGLO_MASK                  0x01000000
#define RF2G_RF2G2_PDREGLO_GET(x)                (((x) & RF2G_RF2G2_PDREGLO_MASK) >> RF2G_RF2G2_PDREGLO_LSB)
#define RF2G_RF2G2_PDREGLO_SET(x)                (((x) << RF2G_RF2G2_PDREGLO_LSB) & RF2G_RF2G2_PDREGLO_MASK)
#define RF2G_RF2G2_PDRFGM_MSB                    23
#define RF2G_RF2G2_PDRFGM_LSB                    23
#define RF2G_RF2G2_PDRFGM_MASK                   0x00800000
#define RF2G_RF2G2_PDRFGM_GET(x)                 (((x) & RF2G_RF2G2_PDRFGM_MASK) >> RF2G_RF2G2_PDRFGM_LSB)
#define RF2G_RF2G2_PDRFGM_SET(x)                 (((x) << RF2G_RF2G2_PDRFGM_LSB) & RF2G_RF2G2_PDRFGM_MASK)
#define RF2G_RF2G2_PDRXLO_MSB                    22
#define RF2G_RF2G2_PDRXLO_LSB                    22
#define RF2G_RF2G2_PDRXLO_MASK                   0x00400000
#define RF2G_RF2G2_PDRXLO_GET(x)                 (((x) & RF2G_RF2G2_PDRXLO_MASK) >> RF2G_RF2G2_PDRXLO_LSB)
#define RF2G_RF2G2_PDRXLO_SET(x)                 (((x) << RF2G_RF2G2_PDRXLO_LSB) & RF2G_RF2G2_PDRXLO_MASK)
#define RF2G_RF2G2_PDTXLO_MSB                    21
#define RF2G_RF2G2_PDTXLO_LSB                    21
#define RF2G_RF2G2_PDTXLO_MASK                   0x00200000
#define RF2G_RF2G2_PDTXLO_GET(x)                 (((x) & RF2G_RF2G2_PDTXLO_MASK) >> RF2G_RF2G2_PDTXLO_LSB)
#define RF2G_RF2G2_PDTXLO_SET(x)                 (((x) << RF2G_RF2G2_PDTXLO_LSB) & RF2G_RF2G2_PDTXLO_MASK)
#define RF2G_RF2G2_PDTXMIX_MSB                   20
#define RF2G_RF2G2_PDTXMIX_LSB                   20
#define RF2G_RF2G2_PDTXMIX_MASK                  0x00100000
#define RF2G_RF2G2_PDTXMIX_GET(x)                (((x) & RF2G_RF2G2_PDTXMIX_MASK) >> RF2G_RF2G2_PDTXMIX_LSB)
#define RF2G_RF2G2_PDTXMIX_SET(x)                (((x) << RF2G_RF2G2_PDTXMIX_LSB) & RF2G_RF2G2_PDTXMIX_MASK)
#define RF2G_RF2G2_REGLNA_BYPASS_MSB             19
#define RF2G_RF2G2_REGLNA_BYPASS_LSB             19
#define RF2G_RF2G2_REGLNA_BYPASS_MASK            0x00080000
#define RF2G_RF2G2_REGLNA_BYPASS_GET(x)          (((x) & RF2G_RF2G2_REGLNA_BYPASS_MASK) >> RF2G_RF2G2_REGLNA_BYPASS_LSB)
#define RF2G_RF2G2_REGLNA_BYPASS_SET(x)          (((x) << RF2G_RF2G2_REGLNA_BYPASS_LSB) & RF2G_RF2G2_REGLNA_BYPASS_MASK)
#define RF2G_RF2G2_REGLO_BYPASS_MSB              18
#define RF2G_RF2G2_REGLO_BYPASS_LSB              18
#define RF2G_RF2G2_REGLO_BYPASS_MASK             0x00040000
#define RF2G_RF2G2_REGLO_BYPASS_GET(x)           (((x) & RF2G_RF2G2_REGLO_BYPASS_MASK) >> RF2G_RF2G2_REGLO_BYPASS_LSB)
#define RF2G_RF2G2_REGLO_BYPASS_SET(x)           (((x) << RF2G_RF2G2_REGLO_BYPASS_LSB) & RF2G_RF2G2_REGLO_BYPASS_MASK)
#define RF2G_RF2G2_ENABLE_PCB_MSB                17
#define RF2G_RF2G2_ENABLE_PCB_LSB                17
#define RF2G_RF2G2_ENABLE_PCB_MASK               0x00020000
#define RF2G_RF2G2_ENABLE_PCB_GET(x)             (((x) & RF2G_RF2G2_ENABLE_PCB_MASK) >> RF2G_RF2G2_ENABLE_PCB_LSB)
#define RF2G_RF2G2_ENABLE_PCB_SET(x)             (((x) << RF2G_RF2G2_ENABLE_PCB_LSB) & RF2G_RF2G2_ENABLE_PCB_MASK)
#define RF2G_RF2G2_SPARE_MSB                     16
#define RF2G_RF2G2_SPARE_LSB                     0
#define RF2G_RF2G2_SPARE_MASK                    0x0001ffff
#define RF2G_RF2G2_SPARE_GET(x)                  (((x) & RF2G_RF2G2_SPARE_MASK) >> RF2G_RF2G2_SPARE_LSB)
#define RF2G_RF2G2_SPARE_SET(x)                  (((x) << RF2G_RF2G2_SPARE_LSB) & RF2G_RF2G2_SPARE_MASK)


#ifndef __ASSEMBLER__

typedef struct rf2g_reg_reg_s {
  volatile unsigned int rf2g_rf2g1;
  volatile unsigned int rf2g_rf2g2;
} rf2g_reg_reg_t;

#endif /* __ASSEMBLER__ */

#endif /* _RF2G_REG_H_ */
