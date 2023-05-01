/* SPDX-License-Identifier: GPL-2.0
 *
 * Copyright 2016-2018 HabanaLabs, Ltd.
 * All Rights Reserved.
 *
 */

/************************************
 ** This is an auto-generated file **
 **       DO NOT EDIT BELOW        **
 ************************************/

#ifndef ASIC_REG_PSOC_GLOBAL_CONF_MASKS_H_
#define ASIC_REG_PSOC_GLOBAL_CONF_MASKS_H_

/*
 *****************************************
 *   PSOC_GLOBAL_CONF (Prototype: GLOBAL_CONF)
 *****************************************
 */

/* PSOC_GLOBAL_CONF_NON_RST_FLOPS */
#define PSOC_GLOBAL_CONF_NON_RST_FLOPS_VAL_SHIFT                     0
#define PSOC_GLOBAL_CONF_NON_RST_FLOPS_VAL_MASK                      0xFFFFFFFF

/* PSOC_GLOBAL_CONF_PCI_FW_FSM */
#define PSOC_GLOBAL_CONF_PCI_FW_FSM_EN_SHIFT                         0
#define PSOC_GLOBAL_CONF_PCI_FW_FSM_EN_MASK                          0x1

/* PSOC_GLOBAL_CONF_BOOT_SEQ_RE_START */
#define PSOC_GLOBAL_CONF_BOOT_SEQ_RE_START_IND_SHIFT                 0
#define PSOC_GLOBAL_CONF_BOOT_SEQ_RE_START_IND_MASK                  0x1

/* PSOC_GLOBAL_CONF_BTM_FSM */
#define PSOC_GLOBAL_CONF_BTM_FSM_STATE_SHIFT                         0
#define PSOC_GLOBAL_CONF_BTM_FSM_STATE_MASK                          0xF

/* PSOC_GLOBAL_CONF_SW_BTM_FSM */
#define PSOC_GLOBAL_CONF_SW_BTM_FSM_CTRL_SHIFT                       0
#define PSOC_GLOBAL_CONF_SW_BTM_FSM_CTRL_MASK                        0xF

/* PSOC_GLOBAL_CONF_SW_BOOT_SEQ_FSM */
#define PSOC_GLOBAL_CONF_SW_BOOT_SEQ_FSM_CTRL_SHIFT                  0
#define PSOC_GLOBAL_CONF_SW_BOOT_SEQ_FSM_CTRL_MASK                   0xF

/* PSOC_GLOBAL_CONF_BOOT_SEQ_TIMEOUT */
#define PSOC_GLOBAL_CONF_BOOT_SEQ_TIMEOUT_VAL_SHIFT                  0
#define PSOC_GLOBAL_CONF_BOOT_SEQ_TIMEOUT_VAL_MASK                   0xFFFFFFFF

/* PSOC_GLOBAL_CONF_SPI_MEM_EN */
#define PSOC_GLOBAL_CONF_SPI_MEM_EN_IND_SHIFT                        0
#define PSOC_GLOBAL_CONF_SPI_MEM_EN_IND_MASK                         0x1

/* PSOC_GLOBAL_CONF_PRSTN */
#define PSOC_GLOBAL_CONF_PRSTN_VAL_SHIFT                             0
#define PSOC_GLOBAL_CONF_PRSTN_VAL_MASK                              0x1

/* PSOC_GLOBAL_CONF_PCIE_EN */
#define PSOC_GLOBAL_CONF_PCIE_EN_MASK_SHIFT                          0
#define PSOC_GLOBAL_CONF_PCIE_EN_MASK_MASK                           0x1

/* PSOC_GLOBAL_CONF_SPI_IMG_STS */
#define PSOC_GLOBAL_CONF_SPI_IMG_STS_PRI_SHIFT                       0
#define PSOC_GLOBAL_CONF_SPI_IMG_STS_PRI_MASK                        0x1
#define PSOC_GLOBAL_CONF_SPI_IMG_STS_SEC_SHIFT                       1
#define PSOC_GLOBAL_CONF_SPI_IMG_STS_SEC_MASK                        0x2
#define PSOC_GLOBAL_CONF_SPI_IMG_STS_PRSTN_SHIFT                     2
#define PSOC_GLOBAL_CONF_SPI_IMG_STS_PRSTN_MASK                      0x4
#define PSOC_GLOBAL_CONF_SPI_IMG_STS_PCI_SHIFT                       3
#define PSOC_GLOBAL_CONF_SPI_IMG_STS_PCI_MASK                        0x8

/* PSOC_GLOBAL_CONF_BOOT_SEQ_FSM */
#define PSOC_GLOBAL_CONF_BOOT_SEQ_FSM_IDLE_SHIFT                     0
#define PSOC_GLOBAL_CONF_BOOT_SEQ_FSM_IDLE_MASK                      0x1
#define PSOC_GLOBAL_CONF_BOOT_SEQ_FSM_BOOT_INIT_SHIFT                1
#define PSOC_GLOBAL_CONF_BOOT_SEQ_FSM_BOOT_INIT_MASK                 0x2
#define PSOC_GLOBAL_CONF_BOOT_SEQ_FSM_SPI_PRI_SHIFT                  2
#define PSOC_GLOBAL_CONF_BOOT_SEQ_FSM_SPI_PRI_MASK                   0x4
#define PSOC_GLOBAL_CONF_BOOT_SEQ_FSM_SPI_SEC_SHIFT                  3
#define PSOC_GLOBAL_CONF_BOOT_SEQ_FSM_SPI_SEC_MASK                   0x8
#define PSOC_GLOBAL_CONF_BOOT_SEQ_FSM_SPI_PRSTN_SHIFT                4
#define PSOC_GLOBAL_CONF_BOOT_SEQ_FSM_SPI_PRSTN_MASK                 0x10
#define PSOC_GLOBAL_CONF_BOOT_SEQ_FSM_SPI_PCIE_SHIFT                 5
#define PSOC_GLOBAL_CONF_BOOT_SEQ_FSM_SPI_PCIE_MASK                  0x20
#define PSOC_GLOBAL_CONF_BOOT_SEQ_FSM_ROM_SHIFT                      6
#define PSOC_GLOBAL_CONF_BOOT_SEQ_FSM_ROM_MASK                       0x40
#define PSOC_GLOBAL_CONF_BOOT_SEQ_FSM_PCLK_READY_SHIFT               7
#define PSOC_GLOBAL_CONF_BOOT_SEQ_FSM_PCLK_READY_MASK                0x80
#define PSOC_GLOBAL_CONF_BOOT_SEQ_FSM_LTSSM_EN_SHIFT                 8
#define PSOC_GLOBAL_CONF_BOOT_SEQ_FSM_LTSSM_EN_MASK                  0x100

/* PSOC_GLOBAL_CONF_SCRATCHPAD */
#define PSOC_GLOBAL_CONF_SCRATCHPAD_REG_SHIFT                        0
#define PSOC_GLOBAL_CONF_SCRATCHPAD_REG_MASK                         0xFFFFFFFF

/* PSOC_GLOBAL_CONF_SEMAPHORE */
#define PSOC_GLOBAL_CONF_SEMAPHORE_REG_SHIFT                         0
#define PSOC_GLOBAL_CONF_SEMAPHORE_REG_MASK                          0xFFFFFFFF

/* PSOC_GLOBAL_CONF_WARM_REBOOT */
#define PSOC_GLOBAL_CONF_WARM_REBOOT_CNTR_SHIFT                      0
#define PSOC_GLOBAL_CONF_WARM_REBOOT_CNTR_MASK                       0xFFFFFFFF

/* PSOC_GLOBAL_CONF_UBOOT_MAGIC */
#define PSOC_GLOBAL_CONF_UBOOT_MAGIC_VAL_SHIFT                       0
#define PSOC_GLOBAL_CONF_UBOOT_MAGIC_VAL_MASK                        0xFFFFFFFF

/* PSOC_GLOBAL_CONF_SPL_SOURCE */
#define PSOC_GLOBAL_CONF_SPL_SOURCE_VAL_SHIFT                        0
#define PSOC_GLOBAL_CONF_SPL_SOURCE_VAL_MASK                         0x7

/* PSOC_GLOBAL_CONF_I2C_MSTR1_DBG */
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_S_GEN_SHIFT                   0
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_S_GEN_MASK                    0x1
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_P_GEN_SHIFT                   1
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_P_GEN_MASK                    0x2
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_DATA_SHIFT                    2
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_DATA_MASK                     0x4
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_ADDR_SHIFT                    3
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_ADDR_MASK                     0x8
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_RD_SHIFT                      4
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_RD_MASK                       0x10
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_WR_SHIFT                      5
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_WR_MASK                       0x20
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_HS_SHIFT                      6
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_HS_MASK                       0x40
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_MASTER_ACT_SHIFT              7
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_MASTER_ACT_MASK               0x80
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_SLAVE_ACT_SHIFT               8
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_SLAVE_ACT_MASK                0x100
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_ADDR_10BIT_SHIFT              9
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_ADDR_10BIT_MASK               0x200
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_MST_CSTATE_SHIFT              10
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_MST_CSTATE_MASK               0x7C00
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_SLV_CSTATE_SHIFT              15
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_SLV_CSTATE_MASK               0x78000
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_IC_EN_SHIFT                   19
#define PSOC_GLOBAL_CONF_I2C_MSTR1_DBG_IC_EN_MASK                    0x80000

/* PSOC_GLOBAL_CONF_I2C_SLV */
#define PSOC_GLOBAL_CONF_I2C_SLV_CPU_CTRL_SHIFT                      0
#define PSOC_GLOBAL_CONF_I2C_SLV_CPU_CTRL_MASK                       0x1

/* PSOC_GLOBAL_CONF_I2C_SLV_INTR_MASK */
#define PSOC_GLOBAL_CONF_I2C_SLV_INTR_MASK_FLD_INT_SHIFT             0
#define PSOC_GLOBAL_CONF_I2C_SLV_INTR_MASK_FLD_INT_MASK              0x1

/* PSOC_GLOBAL_CONF_APP_STATUS */
#define PSOC_GLOBAL_CONF_APP_STATUS_IND_SHIFT                        0
#define PSOC_GLOBAL_CONF_APP_STATUS_IND_MASK                         0xFFFFFFFF

/* PSOC_GLOBAL_CONF_BTL_STS */
#define PSOC_GLOBAL_CONF_BTL_STS_DONE_SHIFT                          0
#define PSOC_GLOBAL_CONF_BTL_STS_DONE_MASK                           0x1
#define PSOC_GLOBAL_CONF_BTL_STS_FAIL_SHIFT                          4
#define PSOC_GLOBAL_CONF_BTL_STS_FAIL_MASK                           0x10
#define PSOC_GLOBAL_CONF_BTL_STS_FAIL_CODE_SHIFT                     8
#define PSOC_GLOBAL_CONF_BTL_STS_FAIL_CODE_MASK                      0xF00

/* PSOC_GLOBAL_CONF_TIMEOUT_INTR */
#define PSOC_GLOBAL_CONF_TIMEOUT_INTR_GPIO_0_SHIFT                   0
#define PSOC_GLOBAL_CONF_TIMEOUT_INTR_GPIO_0_MASK                    0x1
#define PSOC_GLOBAL_CONF_TIMEOUT_INTR_GPIO_1_SHIFT                   1
#define PSOC_GLOBAL_CONF_TIMEOUT_INTR_GPIO_1_MASK                    0x2
#define PSOC_GLOBAL_CONF_TIMEOUT_INTR_GPIO_2_SHIFT                   2
#define PSOC_GLOBAL_CONF_TIMEOUT_INTR_GPIO_2_MASK                    0x4
#define PSOC_GLOBAL_CONF_TIMEOUT_INTR_GPIO_3_SHIFT                   3
#define PSOC_GLOBAL_CONF_TIMEOUT_INTR_GPIO_3_MASK                    0x8
#define PSOC_GLOBAL_CONF_TIMEOUT_INTR_GPIO_4_SHIFT                   4
#define PSOC_GLOBAL_CONF_TIMEOUT_INTR_GPIO_4_MASK                    0x10
#define PSOC_GLOBAL_CONF_TIMEOUT_INTR_TIMER_SHIFT                    5
#define PSOC_GLOBAL_CONF_TIMEOUT_INTR_TIMER_MASK                     0x20
#define PSOC_GLOBAL_CONF_TIMEOUT_INTR_UART_0_SHIFT                   6
#define PSOC_GLOBAL_CONF_TIMEOUT_INTR_UART_0_MASK                    0x40
#define PSOC_GLOBAL_CONF_TIMEOUT_INTR_UART_1_SHIFT                   7
#define PSOC_GLOBAL_CONF_TIMEOUT_INTR_UART_1_MASK                    0x80

/* PSOC_GLOBAL_CONF_COMB_TIMEOUT_INTR */
#define PSOC_GLOBAL_CONF_COMB_TIMEOUT_INTR_IND_SHIFT                 0
#define PSOC_GLOBAL_CONF_COMB_TIMEOUT_INTR_IND_MASK                  0x1

/* PSOC_GLOBAL_CONF_PERIPH_INTR */
#define PSOC_GLOBAL_CONF_PERIPH_INTR_UART_0_TX_SHIFT                 0
#define PSOC_GLOBAL_CONF_PERIPH_INTR_UART_0_TX_MASK                  0x1
#define PSOC_GLOBAL_CONF_PERIPH_INTR_UART_0_RX_SHIFT                 1
#define PSOC_GLOBAL_CONF_PERIPH_INTR_UART_0_RX_MASK                  0x2
#define PSOC_GLOBAL_CONF_PERIPH_INTR_UART_0_TXOVR_SHIFT              2
#define PSOC_GLOBAL_CONF_PERIPH_INTR_UART_0_TXOVR_MASK               0x4
#define PSOC_GLOBAL_CONF_PERIPH_INTR_UART_0_RXOVR_SHIFT              3
#define PSOC_GLOBAL_CONF_PERIPH_INTR_UART_0_RXOVR_MASK               0x8
#define PSOC_GLOBAL_CONF_PERIPH_INTR_UART_1_TX_SHIFT                 4
#define PSOC_GLOBAL_CONF_PERIPH_INTR_UART_1_TX_MASK                  0x10
#define PSOC_GLOBAL_CONF_PERIPH_INTR_UART_1_RX_SHIFT                 5
#define PSOC_GLOBAL_CONF_PERIPH_INTR_UART_1_RX_MASK                  0x20
#define PSOC_GLOBAL_CONF_PERIPH_INTR_UART_1_TXOVR_SHIFT              6
#define PSOC_GLOBAL_CONF_PERIPH_INTR_UART_1_TXOVR_MASK               0x40
#define PSOC_GLOBAL_CONF_PERIPH_INTR_UART_1_RXOVR_SHIFT              7
#define PSOC_GLOBAL_CONF_PERIPH_INTR_UART_1_RXOVR_MASK               0x80
#define PSOC_GLOBAL_CONF_PERIPH_INTR_EMMC_SHIFT                      12
#define PSOC_GLOBAL_CONF_PERIPH_INTR_EMMC_MASK                       0x1000
#define PSOC_GLOBAL_CONF_PERIPH_INTR_EMMC_WAKEUP_SHIFT               13
#define PSOC_GLOBAL_CONF_PERIPH_INTR_EMMC_WAKEUP_MASK                0x2000
#define PSOC_GLOBAL_CONF_PERIPH_INTR_MII_SHIFT                       16
#define PSOC_GLOBAL_CONF_PERIPH_INTR_MII_MASK                        0x10000

/* PSOC_GLOBAL_CONF_COMB_PERIPH_INTR */
#define PSOC_GLOBAL_CONF_COMB_PERIPH_INTR_IND_SHIFT                  0
#define PSOC_GLOBAL_CONF_COMB_PERIPH_INTR_IND_MASK                   0x1

/* PSOC_GLOBAL_CONF_AXI_ERR_INTR */
#define PSOC_GLOBAL_CONF_AXI_ERR_INTR_IND_SHIFT                      0
#define PSOC_GLOBAL_CONF_AXI_ERR_INTR_IND_MASK                       0x1

/* PSOC_GLOBAL_CONF_TARGETID */
#define PSOC_GLOBAL_CONF_TARGETID_TDESIGNER_SHIFT                    1
#define PSOC_GLOBAL_CONF_TARGETID_TDESIGNER_MASK                     0xFFE
#define PSOC_GLOBAL_CONF_TARGETID_TPARTNO_SHIFT                      12
#define PSOC_GLOBAL_CONF_TARGETID_TPARTNO_MASK                       0xFFFF000
#define PSOC_GLOBAL_CONF_TARGETID_TREVISION_SHIFT                    28
#define PSOC_GLOBAL_CONF_TARGETID_TREVISION_MASK                     0xF0000000

/* PSOC_GLOBAL_CONF_EMMC_INT_VOL_STABLE */
#define PSOC_GLOBAL_CONF_EMMC_INT_VOL_STABLE_IND_SHIFT               0
#define PSOC_GLOBAL_CONF_EMMC_INT_VOL_STABLE_IND_MASK                0x1

/* PSOC_GLOBAL_CONF_MII_ADDR */
#define PSOC_GLOBAL_CONF_MII_ADDR_VAL_SHIFT                          0
#define PSOC_GLOBAL_CONF_MII_ADDR_VAL_MASK                           0xFF

/* PSOC_GLOBAL_CONF_MII_SPEED */
#define PSOC_GLOBAL_CONF_MII_SPEED_VAL_SHIFT                         0
#define PSOC_GLOBAL_CONF_MII_SPEED_VAL_MASK                          0x3

/* PSOC_GLOBAL_CONF_BOOT_STRAP_PINS */
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_CPOL_SHIFT                  0
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_CPOL_MASK                   0x1
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_CPHA_SHIFT                  1
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_CPHA_MASK                   0x2
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_BTL_EN_SHIFT                2
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_BTL_EN_MASK                 0x4
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_BTL_ROM_EN_SHIFT            3
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_BTL_ROM_EN_MASK             0x8
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_PCIE_EN_SHIFT               4
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_PCIE_EN_MASK                0x10
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_I2C_SLV_ADDR_SHIFT          5
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_I2C_SLV_ADDR_MASK           0xFE0
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_BOOT_STG2_SRC_SHIFT         12
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_BOOT_STG2_SRC_MASK          0x3000
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_PLL_BPS_SHIFT               14
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_PLL_BPS_MASK                0x1FC000
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_SRIOV_EN_SHIFT              21
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_SRIOV_EN_MASK               0x200000
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_PLL_CFG_SHIFT               22
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_PLL_CFG_MASK                0x1C00000
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_MEM_REPAIR_BPS_SHIFT        25
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_MEM_REPAIR_BPS_MASK         0x2000000
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_SPARE_SHIFT                 26
#define PSOC_GLOBAL_CONF_BOOT_STRAP_PINS_SPARE_MASK                  0x1C000000

/* PSOC_GLOBAL_CONF_MEM_REPAIR_CTRL */
#define PSOC_GLOBAL_CONF_MEM_REPAIR_CTRL_SET_SHIFT                   0
#define PSOC_GLOBAL_CONF_MEM_REPAIR_CTRL_SET_MASK                    0x1
#define PSOC_GLOBAL_CONF_MEM_REPAIR_CTRL_CLR_SHIFT                   1
#define PSOC_GLOBAL_CONF_MEM_REPAIR_CTRL_CLR_MASK                    0x2

/* PSOC_GLOBAL_CONF_MEM_REPAIR_STS */
#define PSOC_GLOBAL_CONF_MEM_REPAIR_STS_IND_SHIFT                    0
#define PSOC_GLOBAL_CONF_MEM_REPAIR_STS_IND_MASK                     0x1

/* PSOC_GLOBAL_CONF_OUTSTANT_TRANS */
#define PSOC_GLOBAL_CONF_OUTSTANT_TRANS_RD_SHIFT                     0
#define PSOC_GLOBAL_CONF_OUTSTANT_TRANS_RD_MASK                      0x1
#define PSOC_GLOBAL_CONF_OUTSTANT_TRANS_WR_SHIFT                     1
#define PSOC_GLOBAL_CONF_OUTSTANT_TRANS_WR_MASK                      0x2

/* PSOC_GLOBAL_CONF_MASK_REQ */
#define PSOC_GLOBAL_CONF_MASK_REQ_IND_SHIFT                          0
#define PSOC_GLOBAL_CONF_MASK_REQ_IND_MASK                           0x1

/* PSOC_GLOBAL_CONF_PRSTN_RST_CFG */
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_PCI_SHIFT                     0
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_PCI_MASK                      0x1
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_PCI_IF_SHIFT                  1
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_PCI_IF_MASK                   0x2
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_PLL_SHIFT                     2
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_PLL_MASK                      0x1FC
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_TPC_SHIFT                     9
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_TPC_MASK                      0x200
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_MME_SHIFT                     10
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_MME_MASK                      0x400
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_MC_SHIFT                      11
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_MC_MASK                       0x800
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_CPU_SHIFT                     12
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_CPU_MASK                      0x1000
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_IC_IF_SHIFT                   13
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_IC_IF_MASK                    0x2000
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_PSOC_SHIFT                    14
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_PSOC_MASK                     0x4000
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_SRAM_SHIFT                    15
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_SRAM_MASK                     0x1F8000
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_DMA_SHIFT                     21
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_DMA_MASK                      0x200000
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_DMA_IF_SHIFT                  22
#define PSOC_GLOBAL_CONF_PRSTN_RST_CFG_DMA_IF_MASK                   0x400000

/* PSOC_GLOBAL_CONF_SW_ALL_RST_CFG */
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_PCI_SHIFT                    0
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_PCI_MASK                     0x1
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_PCI_IF_SHIFT                 1
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_PCI_IF_MASK                  0x2
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_PLL_SHIFT                    2
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_PLL_MASK                     0x1FC
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_TPC_SHIFT                    9
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_TPC_MASK                     0x200
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_MME_SHIFT                    10
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_MME_MASK                     0x400
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_MC_SHIFT                     11
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_MC_MASK                      0x800
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_CPU_SHIFT                    12
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_CPU_MASK                     0x1000
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_IC_IF_SHIFT                  13
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_IC_IF_MASK                   0x2000
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_PSOC_SHIFT                   14
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_PSOC_MASK                    0x4000
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_SRAM_SHIFT                   15
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_SRAM_MASK                    0x1F8000
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_DMA_SHIFT                    21
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_DMA_MASK                     0x200000
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_DMA_IF_SHIFT                 22
#define PSOC_GLOBAL_CONF_SW_ALL_RST_CFG_DMA_IF_MASK                  0x400000

/* PSOC_GLOBAL_CONF_WD_RST_CFG */
#define PSOC_GLOBAL_CONF_WD_RST_CFG_PCI_SHIFT                        0
#define PSOC_GLOBAL_CONF_WD_RST_CFG_PCI_MASK                         0x1
#define PSOC_GLOBAL_CONF_WD_RST_CFG_PCI_IF_SHIFT                     1
#define PSOC_GLOBAL_CONF_WD_RST_CFG_PCI_IF_MASK                      0x2
#define PSOC_GLOBAL_CONF_WD_RST_CFG_PLL_SHIFT                        2
#define PSOC_GLOBAL_CONF_WD_RST_CFG_PLL_MASK                         0x1FC
#define PSOC_GLOBAL_CONF_WD_RST_CFG_TPC_SHIFT                        9
#define PSOC_GLOBAL_CONF_WD_RST_CFG_TPC_MASK                         0x200
#define PSOC_GLOBAL_CONF_WD_RST_CFG_MME_SHIFT                        10
#define PSOC_GLOBAL_CONF_WD_RST_CFG_MME_MASK                         0x400
#define PSOC_GLOBAL_CONF_WD_RST_CFG_MC_SHIFT                         11
#define PSOC_GLOBAL_CONF_WD_RST_CFG_MC_MASK                          0x800
#define PSOC_GLOBAL_CONF_WD_RST_CFG_CPU_SHIFT                        12
#define PSOC_GLOBAL_CONF_WD_RST_CFG_CPU_MASK                         0x1000
#define PSOC_GLOBAL_CONF_WD_RST_CFG_IC_IF_SHIFT                      13
#define PSOC_GLOBAL_CONF_WD_RST_CFG_IC_IF_MASK                       0x2000
#define PSOC_GLOBAL_CONF_WD_RST_CFG_PSOC_SHIFT                       14
#define PSOC_GLOBAL_CONF_WD_RST_CFG_PSOC_MASK                        0x4000
#define PSOC_GLOBAL_CONF_WD_RST_CFG_SRAM_SHIFT                       15
#define PSOC_GLOBAL_CONF_WD_RST_CFG_SRAM_MASK                        0x1F8000
#define PSOC_GLOBAL_CONF_WD_RST_CFG_DMA_SHIFT                        21
#define PSOC_GLOBAL_CONF_WD_RST_CFG_DMA_MASK                         0x200000
#define PSOC_GLOBAL_CONF_WD_RST_CFG_DMA_IF_SHIFT                     22
#define PSOC_GLOBAL_CONF_WD_RST_CFG_DMA_IF_MASK                      0x400000

/* PSOC_GLOBAL_CONF_MNL_RST_CFG */
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_PCI_SHIFT                       0
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_PCI_MASK                        0x1
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_PCI_IF_SHIFT                    1
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_PCI_IF_MASK                     0x2
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_PLL_SHIFT                       2
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_PLL_MASK                        0x1FC
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_TPC_SHIFT                       9
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_TPC_MASK                        0x200
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_MME_SHIFT                       10
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_MME_MASK                        0x400
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_MC_SHIFT                        11
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_MC_MASK                         0x800
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_CPU_SHIFT                       12
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_CPU_MASK                        0x1000
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_IC_IF_SHIFT                     13
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_IC_IF_MASK                      0x2000
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_PSOC_SHIFT                      14
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_PSOC_MASK                       0x4000
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_SRAM_SHIFT                      15
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_SRAM_MASK                       0x1F8000
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_DMA_SHIFT                       21
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_DMA_MASK                        0x200000
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_DMA_IF_SHIFT                    22
#define PSOC_GLOBAL_CONF_MNL_RST_CFG_DMA_IF_MASK                     0x400000

/* PSOC_GLOBAL_CONF_UNIT_RST_N */
#define PSOC_GLOBAL_CONF_UNIT_RST_N_PCI_SHIFT                        0
#define PSOC_GLOBAL_CONF_UNIT_RST_N_PCI_MASK                         0x1
#define PSOC_GLOBAL_CONF_UNIT_RST_N_PCI_IF_SHIFT                     1
#define PSOC_GLOBAL_CONF_UNIT_RST_N_PCI_IF_MASK                      0x2
#define PSOC_GLOBAL_CONF_UNIT_RST_N_PLL_SHIFT                        2
#define PSOC_GLOBAL_CONF_UNIT_RST_N_PLL_MASK                         0x1FC
#define PSOC_GLOBAL_CONF_UNIT_RST_N_TPC_SHIFT                        9
#define PSOC_GLOBAL_CONF_UNIT_RST_N_TPC_MASK                         0x200
#define PSOC_GLOBAL_CONF_UNIT_RST_N_MME_SHIFT                        10
#define PSOC_GLOBAL_CONF_UNIT_RST_N_MME_MASK                         0x400
#define PSOC_GLOBAL_CONF_UNIT_RST_N_MC_SHIFT                         11
#define PSOC_GLOBAL_CONF_UNIT_RST_N_MC_MASK                          0x800
#define PSOC_GLOBAL_CONF_UNIT_RST_N_CPU_SHIFT                        12
#define PSOC_GLOBAL_CONF_UNIT_RST_N_CPU_MASK                         0x1000
#define PSOC_GLOBAL_CONF_UNIT_RST_N_IC_IF_SHIFT                      13
#define PSOC_GLOBAL_CONF_UNIT_RST_N_IC_IF_MASK                       0x2000
#define PSOC_GLOBAL_CONF_UNIT_RST_N_PSOC_SHIFT                       14
#define PSOC_GLOBAL_CONF_UNIT_RST_N_PSOC_MASK                        0x4000
#define PSOC_GLOBAL_CONF_UNIT_RST_N_SRAM_SHIFT                       15
#define PSOC_GLOBAL_CONF_UNIT_RST_N_SRAM_MASK                        0x1F8000
#define PSOC_GLOBAL_CONF_UNIT_RST_N_DMA_SHIFT                        21
#define PSOC_GLOBAL_CONF_UNIT_RST_N_DMA_MASK                         0x200000
#define PSOC_GLOBAL_CONF_UNIT_RST_N_DMA_IF_SHIFT                     22
#define PSOC_GLOBAL_CONF_UNIT_RST_N_DMA_IF_MASK                      0x400000

/* PSOC_GLOBAL_CONF_PRSTN_MASK */
#define PSOC_GLOBAL_CONF_PRSTN_MASK_IND_SHIFT                        0
#define PSOC_GLOBAL_CONF_PRSTN_MASK_IND_MASK                         0x1

/* PSOC_GLOBAL_CONF_WD_MASK */
#define PSOC_GLOBAL_CONF_WD_MASK_IND_SHIFT                           0
#define PSOC_GLOBAL_CONF_WD_MASK_IND_MASK                            0x1

/* PSOC_GLOBAL_CONF_RST_SRC */
#define PSOC_GLOBAL_CONF_RST_SRC_VAL_SHIFT                           0
#define PSOC_GLOBAL_CONF_RST_SRC_VAL_MASK                            0xF

/* PSOC_GLOBAL_CONF_PAD_1V8_CFG */
#define PSOC_GLOBAL_CONF_PAD_1V8_CFG_VAL_SHIFT                       0
#define PSOC_GLOBAL_CONF_PAD_1V8_CFG_VAL_MASK                        0x7F

/* PSOC_GLOBAL_CONF_PAD_3V3_CFG */
#define PSOC_GLOBAL_CONF_PAD_3V3_CFG_VAL_SHIFT                       0
#define PSOC_GLOBAL_CONF_PAD_3V3_CFG_VAL_MASK                        0x7F

/* PSOC_GLOBAL_CONF_PAD_1V8_INPUT */
#define PSOC_GLOBAL_CONF_PAD_1V8_INPUT_CFG_SHIFT                     0
#define PSOC_GLOBAL_CONF_PAD_1V8_INPUT_CFG_MASK                      0x7

/* PSOC_GLOBAL_CONF_BNK3V3_MS */
#define PSOC_GLOBAL_CONF_BNK3V3_MS_VAL_SHIFT                         0
#define PSOC_GLOBAL_CONF_BNK3V3_MS_VAL_MASK                          0x3

/* PSOC_GLOBAL_CONF_PAD_DEFAULT */
#define PSOC_GLOBAL_CONF_PAD_DEFAULT_VAL_SHIFT                       0
#define PSOC_GLOBAL_CONF_PAD_DEFAULT_VAL_MASK                        0xF

/* PSOC_GLOBAL_CONF_PAD_SEL */
#define PSOC_GLOBAL_CONF_PAD_SEL_VAL_SHIFT                           0
#define PSOC_GLOBAL_CONF_PAD_SEL_VAL_MASK                            0x3

#endif /* ASIC_REG_PSOC_GLOBAL_CONF_MASKS_H_ */
