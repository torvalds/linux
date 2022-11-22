// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2023 Rockchip Electronics Co. Ltd.
 *
 * Author: Joseph Chen <chenjh@rock-chips.com>
 */

#include "cru_core.h"
#include "cru_rkx121.h"

/*
 *			[RKX121 CHIP]: TX
 *
 * ================= SECITON: Input clock from devices =========================
 *
 * ### 300M ###
 * dclk_c_dvp_src
 * dclk_d_dsi_src --|-- dclk_d_ds
 *                  |-- dclk_d_dsi_pattern_gen
 *
 * clk_lvds0_src --|-- clk_lvds0
 *                 |-- clk_lvds0_pattern_gen
 *
 * clk_lvds1_src --|-- clk_lvds1
 *                 |-- clk_lvds1_pattern_gen
 *
 * dclk_rgc_src
 *
 *
 * ### 200M ###
 * clk_link_pcs0 --|-- clk_pma2pcs0                |-- clk_pma2link2pcs_link
 *                 |                               |
 *                 |-- clk_pma2link2pcs_cm (MUX) --|-- clk_pma2link2pcs_psc0
 * clk_link_pcs1 --|                               |
 *                 |-- clk_pma2pcs1                |-- clk_pma2link2pcs_psc1
 *
 * clk_txbytehs_dsitx_csitx0
 * clk_txbytehs_dsitx_csitx1
 *
 *
 *
 * ### 150M ###
 *
 * rxpclk_vicap_lvds
 *
 *
 * ### 100M ###
 *
 * clk_2x_pma2pcs0
 * clk_2x_pma2pcs1
 *
 *
 * ### 50M ###
 * clk_txesc_mipitxphy0
 * clk_rxesc_dsitx
 *
 */

#define RKX120_SSCG_TXPLL_EN 0
#define RKX120_SSCG_CPLL_EN  0
#define RKX120_TESTOUT_MUX   -1 /* valid options: RKX120_TEST_CLKOUT_IOUT_SEL_? */

#define RKX120_GRF_GPIO0B_IOMUX_H 0x0101000c
#define GPIO0B7_TEST_CLKOUT       0x01c00080

#define I2S_122888_BEST_PRATE 393216000
#define I2S_112896_BEST_PRATE 756403200

static struct PLL_CONFIG PLL_TABLE[] = {
    /* _mhz, _refDiv, _fbDiv, _postdDv1, _postDiv2, _dsmpd, _frac */
    RK_PLL_RATE(1200000000, 1, 50, 1, 1, 1, 0),
    RK_PLL_RATE(600000000, 1, 25, 1, 1, 1, 0),

    /* display */
    RK_PLL_RATE(1188000000, 2, 99, 1, 1, 1, 0),

    /* audio: 12.288M */
    RK_PLL_RATE(688128000, 1, 86, 3, 1, 0, 268435),   /* div=56, vco=2064.384M */
    RK_PLL_RATE(393216000, 2, 131, 4, 1, 0, 1207959), /* div=32, vco=1572.864M */
    RK_PLL_RATE(344064000, 1, 43, 3, 1, 0, 134217),   /* div=28, vco=1032.192M */
    /* audio: 11.2896M */
    RK_PLL_RATE(474163200, 1, 79, 4, 1, 0, 456340),   /* div=42, vco=1896.6528M */
    RK_PLL_RATE(756403200, 1, 63, 2, 1, 0, 563714),   /* div=67, vco=1512.8064M */
    RK_PLL_RATE(564480000, 1, 47, 2, 1, 0, 671088),   /* div=50, vco=1128.96M */

    { /* sentinel */ },
};

static struct PLL_SETUP TXPLL_SETUP = {
    .id = PLL_TXPLL,
    .conOffset0 = 0x01000020,
    .conOffset1 = 0x01000024,
    .conOffset2 = 0x01000028,
    .conOffset3 = 0x0100002c,
    .modeOffset = 0x01000600,
    .modeShift = 0,         /* 0: slow-mode, 1: normal-mode */
    .lockShift = 10,
    .modeMask = 0x1,
    .rateTable = PLL_TABLE,

    .minRefdiv = 1,
    .maxRefdiv = 2,
    .minVco = _MHZ(375),
    .maxVco = _MHZ(2400),
    .minFout = _MHZ(24),
    .maxFout = _MHZ(1200),
    .sscgEn = RKX120_SSCG_TXPLL_EN,
};

static struct PLL_SETUP CPLL_SETUP = {
    .id = PLL_CPLL,
    .conOffset0 = 0x01000000,
    .conOffset1 = 0x01000004,
    .conOffset2 = 0x01000008,
    .conOffset3 = 0x0100000c,
    .modeOffset = 0x01000600,
    .modeShift = 2,
    .lockShift = 10,
    .modeMask = 0x1 << 2,
    .rateTable = PLL_TABLE,

    .minRefdiv = 1,
    .maxRefdiv = 2,
    .minVco = _MHZ(375),
    .maxVco = _MHZ(2400),
    .minFout = _MHZ(24),
    .maxFout = _MHZ(1200),
    .sscgEn = RKX120_SSCG_CPLL_EN,
};

static uint32_t RKX12x_HAL_CRU_ClkGetFreq(struct hwclk *hw, uint32_t clockName)
{
    uint32_t clkMux = CLK_GET_MUX(clockName);
    uint32_t clkDiv = CLK_GET_DIV(clockName);
    uint32_t pRate = 0, freq = 0;
    uint32_t pRate0 = 0, pRate1 = 0;

    switch (clockName) {
    case RKX120_CPS_PLL_TXPLL:
        freq = HAL_CRU_GetPllFreq(hw, &TXPLL_SETUP);
        hw->pllRate[RKX120_TXPLL] = freq;

        return freq;

    case RKX120_CPS_PLL_CPLL:
        freq = HAL_CRU_GetPllFreq(hw, &CPLL_SETUP);
        hw->pllRate[RKX120_CPLL] = freq;

        return freq;

    /* lvds: 200M */
    case RKX121_CPS_CLK_LVDS_C_LVDS_TX:
    /* link: 300M */
    case RKX120_CPS_E0_CLK_RKLINK_RX_PRE:
    case RKX120_CPS_E1_CLK_RKLINK_RX_PRE:
    /* csi: 50M */
    case RKX120_CPS_CLK_TXESC_CSITX0:
    case RKX120_CPS_CLK_TXESC_CSITX1:
    /* i2s: 600M */
    case RKX120_CPS_CLK_I2S_SRC_RKLINK_RX:
    /* pwm: 100M */
    case RKX120_CPS_CLK_PWM_TX:
    case RKX120_CPS_PCLKOUT_DVPTX:
        freq = HAL_CRU_MuxGetFreq3(hw, clkMux,
                                   hw->pllRate[RKX120_TXPLL],
                                   hw->pllRate[RKX120_CPLL], OSC_24M);
        break;

    /* gate clock from TX_E0_CLK_RKLINK_RX_PRE */
    case RKX120_CPS_ICLK_C_CSI0:

        return RKX12x_HAL_CRU_ClkGetFreq(hw, RKX120_CPS_E0_CLK_RKLINK_RX_PRE);

    /* gate clock from TX_E1_CLK_RKLINK_RX_PRE */
    case RKX120_CPS_ICLK_C_CSI1:

        return RKX12x_HAL_CRU_ClkGetFreq(hw, RKX120_CPS_E1_CLK_RKLINK_RX_PRE);

    case RKX121_CPS_CLK_LVDS0:
    case RKX121_CPS_CLK_LVDS1:
        pRate0 = _MHZ(150); /* input clock: clk_lvds1_cm */
        pRate1 = RKX12x_HAL_CRU_ClkGetFreq(hw, RKX121_CPS_CLK_LVDS_C_LVDS_TX);
        freq = HAL_CRU_MuxGetFreq2(hw, clkMux, pRate0, pRate1);
        break;

    /* pre-bus: 100M */
    case RKX120_CPS_BUSCLK_TX_PRE0:
        freq = HAL_CRU_MuxGetFreq2(hw, clkMux,
                                   hw->pllRate[RKX120_TXPLL],
                                   hw->pllRate[RKX120_CPLL]);
        break;
    /*
     * bus: 100MHZ
     *
     * === TX_BUSCLK_TX_PRE gate children ===
     *
     * pclk_tx_cru
     * pclk_tx_grf
     * pclk_tx_gpio0/1
     * pclk_tx_efuse
     * pclk_mipi_grf_tx0/1
     * pclk_tx_i2c2apb
     * pclk_tx_i2c2apb_debug
     * hclk_dvp_tx
     * pclk_csitx0/1
     * pclk_dsitx
     * pclk_rklink_rx
     * pclk_d_dsi_pattern_gen
     * pclk_lvds{0, 1}_pattern_gen
     * pclk_pcs0/1
     * pclk_pcs{0,1}_ada
     * pclk_mipitxphy0/1
     * pclk_pwm_tx
     * pclk_dft2apb
     * pclk_lbist_ada_tx
     */
    case RKX120_CPS_BUSCLK_TX_PRE:
        pRate = RKX12x_HAL_CRU_ClkGetFreq(hw, RKX120_CPS_BUSCLK_TX_PRE0);
        freq = HAL_CRU_MuxGetFreq2(hw, clkMux, OSC_24M, pRate);
        break;

    /* gpio: 24M */
    case RKX120_CPS_DCLK_TX_GPIO0:
    case RKX120_CPS_DCLK_TX_GPIO1:
    /* efuse: 24M */
    case RKX120_CPS_CLK_TX_EFUSE:
    /* pcs_ada: 24M */
    case RKX120_CPS_CLK_PCS0_ADA:
    case RKX120_CPS_CLK_PCS1_ADA:
    /* capture_pwm: 24M */
    case RKX120_CPS_CLK_CAPTURE_PWM_TX:

        return OSC_24M;

    case RKX120_CPS_CLK_PMA2PCS2LINK_CM:

        return _MHZ(200);

    default:
        CRU_ERR("%s: %s: Unknown clk 0x%08x\n", __func__, hw->name, clockName);

        return HAL_INVAL;
    }

    if (!clkMux && !clkDiv) {
        return 0;
    }

    if (clkDiv) {
        freq /= (HAL_CRU_ClkGetDiv(hw, clkDiv));
    }

    return freq;
}

static HAL_Status RKX12x_HAL_CRU_ClkSetFreq(struct hwclk *hw, uint32_t clockName, uint32_t rate)
{
    uint32_t clkMux = CLK_GET_MUX(clockName);
    uint32_t clkDiv = CLK_GET_DIV(clockName);
    uint32_t mux = 0, div = 1;
    uint32_t pRate = 0;
    uint32_t maxDiv;
    uint32_t pll;
    uint8_t overMax;
    HAL_Status ret = HAL_OK;

    switch (clockName) {
    case RKX120_CPS_PLL_TXPLL:
        ret = HAL_CRU_SetPllFreq(hw, &TXPLL_SETUP, rate);
        hw->pllRate[RKX120_TXPLL] = rate;
        CRU_MSG("%s: TXPLL set rate: %d\n", hw->name, rate);

        return ret;

    case RKX120_CPS_PLL_CPLL:
        ret = HAL_CRU_SetPllFreq(hw, &CPLL_SETUP, rate);
        hw->pllRate[RKX120_CPLL] = rate;
        CRU_MSG("%s: CPLL set rate: %d\n", hw->name, rate);

        return ret;

    /* link(dclk): Allowed to change PLL rate if need ! */
    case RKX120_CPS_E0_CLK_RKLINK_RX_PRE:
    case RKX120_CPS_E1_CLK_RKLINK_RX_PRE:
    /* i2s */
    case RKX120_CPS_CLK_I2S_SRC_RKLINK_RX:
        maxDiv = CLK_DIV_GET_MAXDIV(clkDiv);

        if (DIV_NO_REM(hw->pllRate[RKX120_TXPLL], rate, maxDiv)) {
            mux = 0;
            pRate = hw->pllRate[RKX120_TXPLL];
        } else if (DIV_NO_REM(hw->pllRate[RKX120_CPLL], rate, maxDiv)) {
            mux = 1;
            pRate = hw->pllRate[RKX120_CPLL];
        } else if (DIV_NO_REM(OSC_24M, rate, maxDiv)) {
            mux = 2;
            pRate = OSC_24M;
        } else {
            if (clockName == RKX120_CPS_CLK_I2S_SRC_RKLINK_RX) {
                pll = RKX120_CPS_PLL_CPLL;
                if (DIV_NO_REM(I2S_122888_BEST_PRATE, rate, maxDiv)) {
                    pRate = I2S_122888_BEST_PRATE;
                } else if (DIV_NO_REM(I2S_112896_BEST_PRATE, rate, maxDiv)) {
                    pRate = I2S_112896_BEST_PRATE;
                }
            } else {
                pll = RKX120_CPS_PLL_TXPLL;
            }

            /* PLL change closest new rate <= 1200M if need */
            if (!pRate) {
                pRate = (_MHZ(1200) / rate) * rate;
            }

            ret = RKX12x_HAL_CRU_ClkSetFreq(hw, pll, pRate);
            if (ret != HAL_OK) {
                return ret;
            }

            /* if success, continue to set divider */
        }
        break;

    /* lvds */
    case RKX121_CPS_CLK_LVDS_C_LVDS_TX:
    /* csi */
    case RKX120_CPS_CLK_TXESC_CSITX0:
    case RKX120_CPS_CLK_TXESC_CSITX1:
    /* pwm */
    case RKX120_CPS_CLK_PWM_TX:
    case RKX120_CPS_PCLKOUT_DVPTX:
        mux = HAL_CRU_RoundFreqGetMux3(hw, rate,
                                       hw->pllRate[RKX120_TXPLL],
                                       hw->pllRate[RKX120_CPLL], OSC_24M, &pRate);
        break;
    /* gate clock from TX_E0_CLK_RKLINK_RX_PRE */
    case RKX120_CPS_ICLK_C_CSI0:

        return RKX12x_HAL_CRU_ClkSetFreq(hw, RKX120_CPS_E0_CLK_RKLINK_RX_PRE, rate);

    /* gate clock from TX_E1_CLK_RKLINK_RX_PRE */
    case RKX120_CPS_ICLK_C_CSI1:

        return RKX12x_HAL_CRU_ClkSetFreq(hw, RKX120_CPS_E1_CLK_RKLINK_RX_PRE, rate);

    /* lvds */
    case RKX121_CPS_CLK_LVDS0:
    case RKX121_CPS_CLK_LVDS1:
        if (rate == _MHZ(150)) {
            return HAL_CRU_ClkSetMux(hw, clkMux, 0); /* input clock: clk_lvds1_cm */
        } else {
            RKX12x_HAL_CRU_ClkSetFreq(hw, RKX121_CPS_CLK_LVDS_C_LVDS_TX, rate);

            return HAL_CRU_ClkSetMux(hw, clkMux, 1);
        }
        break;

    /* pre-bus */
    case RKX120_CPS_BUSCLK_TX_PRE0:
        mux = HAL_CRU_RoundFreqGetMux2(hw, rate,
                                       hw->pllRate[RKX120_TXPLL],
                                       hw->pllRate[RKX120_CPLL], &pRate);
        break;

    case RKX120_CPS_BUSCLK_TX_PRE:
        if (rate == OSC_24M) {
            return HAL_CRU_ClkSetMux(hw, clkMux, 0);
        } else {
            RKX12x_HAL_CRU_ClkSetFreq(hw, RKX120_CPS_BUSCLK_TX_PRE0, rate);

            return HAL_CRU_ClkSetMux(hw, clkMux, 1);
        }
        break;

    /* gpio: 24M */
    case RKX120_CPS_DCLK_TX_GPIO0:
    case RKX120_CPS_DCLK_TX_GPIO1:
    /* efuse: 24M */
    case RKX120_CPS_CLK_TX_EFUSE:
    /* pcs_ada: 24M */
    case RKX120_CPS_CLK_PCS0_ADA:
    case RKX120_CPS_CLK_PCS1_ADA:
    /* capture_pwm: 24M */
    case RKX120_CPS_CLK_CAPTURE_PWM_TX:

        return rate == OSC_24M ? 0 : HAL_INVAL;

    case RKX120_CPS_CLK_PMA2PCS2LINK_CM:
        if (rate != _MHZ(200)) {
            return HAL_INVAL;
        }

        HAL_CRU_ClkSetMux(hw, clkMux, 0);

        return HAL_OK;

    default:
        CRU_ERR("%s: %s: Unknown clk 0x%08x\n", __func__, hw->name, clockName);

        return HAL_INVAL;
    }

    if (!clkMux && !clkDiv) {
        return HAL_INVAL;
    }

    if (pRate) {
        div = HAL_DIV_ROUND_UP(pRate, rate);
    }

    if (clkDiv) {
        overMax = div > CLK_DIV_GET_MAXDIV(clkDiv);
        if (overMax) {
            CRU_MSG("%s: %s: Clk '0x%08x' req div(%d) over max(%d)!\n",
                    __func__, hw->name, clockName, div, CLK_DIV_GET_MAXDIV(clkDiv));
            div = CLK_DIV_GET_MAXDIV(clkDiv);
        }
        HAL_CRU_ClkSetDiv(hw, clkDiv, div);
    }

    if (clkMux) {
        HAL_CRU_ClkSetMux(hw, clkMux, mux);
    }

    return HAL_OK;
}

#if RKX120_SSCG_CPLL_EN || RKX120_SSCG_TXPLL_EN
static void RKX12x_HAL_CRU_Init_SSCG(struct hwclk *hw)
{
    uint8_t down_spread = 1; /* 0: center spread */
    uint8_t amplitude = 8;   /* range: 0x00 - 0x1f */

#if RKX120_SSCG_CPLL_EN
    /* down-spread, 0.8%, 37.5khz */
    HAL_CRU_Write(hw, hw->cru_base + 0x0c, 0x1f000000 | ((amplitude & 0x1f) << 8));
    HAL_CRU_Write(hw, hw->cru_base + 0x0c, 0x00f00050);
    HAL_CRU_Write(hw, hw->cru_base + 0x0c, 0x00080000 | ((down_spread & 0x1) << 3));
    HAL_CRU_Write(hw, hw->cru_base + 0x04, 0x10000000);
    HAL_CRU_Write(hw, hw->cru_base + 0x0c, 0x00070000);
    CRU_MSG("%s: CPLL enable SSCG\n", hw->name);
#endif
#if RKX120_SSCG_TXPLL_EN
    /* down-spread, 0.8%, 37.5khz */
    HAL_CRU_Write(hw, hw->cru_base + 0x2c, 0x1f000000 | ((amplitude & 0x1f) << 8));
    HAL_CRU_Write(hw, hw->cru_base + 0x2c, 0x00f00050);
    HAL_CRU_Write(hw, hw->cru_base + 0x2c, 0x00080000 | ((down_spread & 0x1) << 3));
    HAL_CRU_Write(hw, hw->cru_base + 0x24, 0x10000000);
    HAL_CRU_Write(hw, hw->cru_base + 0x2c, 0x00070000);
    CRU_MSG("%s: TXPLL enable SSCG\n", hw->name);
#endif
}
#endif

static HAL_Status RKX12x_HAL_CRU_InitTestout(struct hwclk *hw, uint32_t clockName,
                                             uint32_t muxValue, uint32_t divValue)
{
    uint32_t clkMux = CLK_GET_MUX(RKX120_CPS_TEST_CLKOUT);
    uint32_t clkDiv = CLK_GET_DIV(RKX120_CPS_TEST_CLKOUT);

    /* gpio0_b7: iomux to clk_testout */
    HAL_CRU_Write(hw, RKX120_GRF_GPIO0B_IOMUX_H, GPIO0B7_TEST_CLKOUT);

    /* Enable clock */
    HAL_CRU_ClkEnable(hw, RKX120_CLK_TESTOUT_TOP_GATE);

    /* Mux, div */
    HAL_CRU_ClkSetDiv(hw, clkDiv, divValue);
    HAL_CRU_ClkSetMux(hw, clkMux, muxValue);

    CRU_MSG("%s: Testout div=%d, mux=%d\n", hw->name, divValue, muxValue);

    return HAL_OK;
}

static HAL_Status RKX12x_HAL_CRU_Init(struct hwclk *hw, struct xferclk *xfer)
{
    hw->cru_base = 0x01000000;
    hw->sel_con0 = hw->cru_base + 0x100;
    hw->gate_con0 = hw->cru_base + 0x300;
    hw->softrst_con0 = hw->cru_base + 0x400;
    hw->gbl_srst_fst = 0x0614;
    hw->flags = 0;
    hw->num_gate = 16 * 12;
    hw->gate = HAL_KCALLOC(hw->num_gate, sizeof(struct clkGate));
    if (!hw->gate) {
        return HAL_NOMEM;
    }
    strcat(hw->name, "<CRU.121@");
    strcat(hw->name, xfer->name);
    strcat(hw->name, ">");

    /* Don't change order */
#if RKX120_SSCG_CPLL_EN || RKX120_SSCG_TXPLL_EN
    RKX12x_HAL_CRU_Init_SSCG(hw);
#endif
    RKX12x_HAL_CRU_ClkSetFreq(hw, RKX120_CPS_PLL_CPLL, _MHZ(1200));
    RKX12x_HAL_CRU_ClkSetFreq(hw, RKX120_CPS_PLL_TXPLL, _MHZ(1188));
    RKX12x_HAL_CRU_ClkSetFreq(hw, RKX120_CPS_BUSCLK_TX_PRE, _MHZ(100));
    RKX12x_HAL_CRU_ClkSetFreq(hw, RKX121_CPS_CLK_LVDS_C_LVDS_TX, _MHZ(200));
    RKX12x_HAL_CRU_ClkSetFreq(hw, RKX120_CPS_CLK_PMA2PCS2LINK_CM, _MHZ(200));
    RKX12x_HAL_CRU_ClkSetFreq(hw, RKX120_CPS_CLK_I2S_SRC_RKLINK_RX, _MHZ(400));
    RKX12x_HAL_CRU_ClkSetFreq(hw, RKX120_CPS_CLK_PWM_TX, _MHZ(24));
    RKX12x_HAL_CRU_ClkSetFreq(hw, RKX120_CPS_CLK_TXESC_CSITX0, _MHZ(20));
    RKX12x_HAL_CRU_ClkSetFreq(hw, RKX120_CPS_CLK_TXESC_CSITX1, _MHZ(20));

    /* Must be the same rate as RKX110_CPS_DCLK_RX_PRE when camera mode */
    RKX12x_HAL_CRU_ClkSetFreq(hw, RKX120_CPS_E0_CLK_RKLINK_RX_PRE, _MHZ(200));
    RKX12x_HAL_CRU_ClkSetFreq(hw, RKX120_CPS_E1_CLK_RKLINK_RX_PRE, _MHZ(200));

    HAL_CRU_ClkEnable(hw, RKX120_CLK_TESTOUT_TOP_GATE);

#if RKX120_TESTOUT_MUX >= 0
    /* clk_testout support max 150M output, so set div=10 by default if not 24M */
    RKX12x_HAL_CRU_InitTestout(hw, RKX120_CPS_TEST_CLKOUT, RKX120_TESTOUT_MUX,
                               RKX120_TESTOUT_MUX == 0 ? 1 : 10);
#endif

    return HAL_OK;
}

PNAME(mux_24m_p) = { "xin24m" };
PNAME(mux_txpll_cpll_p) = { "txpll", "cpll" };
PNAME(mux_txpll_cpll_24m_p) = { "txpll", "cpll", "xin24m" };
PNAME(mux_24m_txpre0_p) = { "xin24m", "busclk_tx_pre0" };

#define CAL_FREQ_REG    0x01000f00
#define CAL_FREQ_EN_REG 0x01000f04

static uint32_t RKX12x_HAL_CRU_ClkGetExtFreq(struct hwclk *hw, uint32_t clk)
{
    uint32_t clkMux = CLK_GET_MUX(RKX120_CPS_TEST_CLKOUT);
    uint32_t clkDiv = CLK_GET_DIV(RKX120_CPS_TEST_CLKOUT);
    uint32_t freq, mhz;
    uint8_t div = 10;

    HAL_CRU_ClkSetDiv(hw, clkDiv, div);
    HAL_CRU_ClkSetMux(hw, clkMux, clk);
    HAL_CRU_Write(hw, CAL_FREQ_EN_REG, 1); /* NOTE: 1: enable, 0: disable */
    HAL_SleepMs(2);
    freq = HAL_CRU_Read(hw, CAL_FREQ_REG);
    HAL_CRU_Write(hw, CAL_FREQ_EN_REG, 0x0);

    /* Fix accuracy */
    if ((freq % 10) == 0x9) {
        freq++;
    }

    freq *= (1000 * div);

    /* If no external input, freq is close to 24M */
    mhz = freq / 1000000;
    if ((clk != 0) && (mhz == 23 || mhz == 24)) {
        freq = 0;
    }

    return freq;
}

static struct clkTable rkx12x_clkTable[] = {
    /* internal */
    CLK_DECLARE_INT("txpll", RKX120_CPS_PLL_TXPLL, mux_24m_p),
    CLK_DECLARE_INT("cpll", RKX120_CPS_PLL_CPLL, mux_24m_p),
    CLK_DECLARE_INT("e0_clk_rklink_rx_pre", RKX120_CPS_E0_CLK_RKLINK_RX_PRE, mux_txpll_cpll_24m_p),
    CLK_DECLARE_INT("e1_clk_rklink_rx_pre", RKX120_CPS_E1_CLK_RKLINK_RX_PRE, mux_txpll_cpll_24m_p),
    CLK_DECLARE_INT("iclk_c_csi0", RKX120_CPS_ICLK_C_CSI0, mux_txpll_cpll_24m_p),
    CLK_DECLARE_INT("iclk_c_csi1", RKX120_CPS_ICLK_C_CSI1, mux_txpll_cpll_24m_p),
    CLK_DECLARE_INT("clk_txesc_csitx0", RKX120_CPS_CLK_TXESC_CSITX0, mux_txpll_cpll_24m_p),
    CLK_DECLARE_INT("clk_txesc_csitx1", RKX120_CPS_CLK_TXESC_CSITX1, mux_txpll_cpll_24m_p),
    CLK_DECLARE_INT("clk_i2s_src_rklink_rx", RKX120_CPS_CLK_I2S_SRC_RKLINK_RX, mux_txpll_cpll_24m_p),
    CLK_DECLARE_INT("clk_pwm_tx", RKX120_CPS_CLK_PWM_TX, mux_txpll_cpll_24m_p),
    CLK_DECLARE_INT("pclkout_dvptx", RKX120_CPS_PCLKOUT_DVPTX, mux_txpll_cpll_24m_p),
    CLK_DECLARE_INT("busclk_tx_pre0", RKX120_CPS_BUSCLK_TX_PRE0, mux_txpll_cpll_p),
    CLK_DECLARE_INT("busclk_tx_pre", RKX120_CPS_BUSCLK_TX_PRE, mux_24m_txpre0_p),
    CLK_DECLARE_INT("dclk_tx_gpio0", RKX120_CPS_DCLK_TX_GPIO0, mux_24m_p),
    CLK_DECLARE_INT("dclk_tx_gpio1", RKX120_CPS_DCLK_TX_GPIO1, mux_24m_p),
    CLK_DECLARE_INT("clk_tx_efuse", RKX120_CPS_CLK_TX_EFUSE, mux_24m_p),
    CLK_DECLARE_INT("clk_pcs0_ada", RKX120_CPS_CLK_PCS0_ADA, mux_24m_p),
    CLK_DECLARE_INT("clk_pcs1_ada", RKX120_CPS_CLK_PCS1_ADA, mux_24m_p),
    CLK_DECLARE_INT("clk_capture_pwm_tx", RKX120_CPS_CLK_CAPTURE_PWM_TX, mux_24m_p),

    /* external */
    CLK_DECLARE_EXT("xin24m", 0, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_txbytehs_csitx0", 3, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_txbytehs_csitx1", 5, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_txbytehs_dsitx", 7, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_rxesc_dsitx", 8, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_link_pcs0", 9, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_link_pcs1", 10, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_pmarx0_pixel", 11, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_pmarx1_pixel", 12, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_mipitxphy0_lvds", 13, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_mipitxphy0_pixel", 14, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_mipitxphy1_lvds", 15, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_mipitxphy1_pixel", 16, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_txbytehs_dsitx_csitx0", 23, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("dclk_c_dvp_src", 24, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("dclk_d_dsi_src", 25, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_lvds0_src", 26, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_lvds1_src", 27, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("dclk_rgb_src", 28, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_2x_pma2pcs0", 29, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_2x_pma2pcs1", 30, RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_txesc_mipitxphy0", 31, RKX12x_HAL_CRU_ClkGetExtFreq),

    CLK_DECLARE_EXT_PARENT("dclk_d_ds", 25, "dclk_d_dsi_src", RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT_PARENT("dclk_d_dsi_pattern_gen", 25, "dclk_d_dsi_src", RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT_PARENT("clk_lvds0", 26, "clk_lvds0_src", RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT_PARENT("clk_lvds0_pattern_gen", 26, "clk_lvds0_src", RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT_PARENT("clk_lvds1", 27, "clk_lvds1_src", RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT_PARENT("clk_lvds1_pattern_gen", 27, "clk_lvds1_src", RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT_PARENT("clk_pma2pcs0", 9, "clk_link_pcs0", RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT_PARENT("clk_pma2link2pcs_cm", 9, "clk_link_pcs0", RKX12x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT_PARENT("clk_pma2pcs1", 10, "clk_link_pcs1", RKX12x_HAL_CRU_ClkGetExtFreq),

    { /* sentinel */ },
};

struct clkOps rkx121_clk_ops =
{
    .clkTable = rkx12x_clkTable,
    .clkInit = RKX12x_HAL_CRU_Init,
    .clkGetFreq = RKX12x_HAL_CRU_ClkGetFreq,
    .clkSetFreq = RKX12x_HAL_CRU_ClkSetFreq,
    .clkInitTestout = RKX12x_HAL_CRU_InitTestout,
};
