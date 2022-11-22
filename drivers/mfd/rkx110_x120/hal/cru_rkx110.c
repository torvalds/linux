/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 *
 * Author: Joseph Chen <chenjh@rock-chips.com>
 */

#include "cru_core.h"
#include "cru_rkx110.h"

/*
 *			[RKX110 CHIP]: RX
 *
 * ================= SECTION: Input clock from devices =========================
 *
 * ### 300M ###
 * clk_8x_pma2pcs0
 * clk_8x_pma2pcs1
 *
 *
 * ### 200M ###
 * sclk_i2s_link2pcs --|-- sclk_i2s_link2pcs_inter1
 *                     |-- sclk_i2s_link2pcs_inter2
 *
 *
 * ### 200M ###
 * clk_link_pcs0 --|-- clk_pma2pcs0                |-- clk_pma2link2pcs_link
 *                 |                               |
 *                 |-- clk_pma2link2pcs_cm (MUX) --|-- clk_pma2link2pcs_psc0
 * clk_link_pcs1 --|                               |
 *                 |-- clk_pma2pcs1                |-- clk_pma2link2pcs_psc1
 *
 * clk_rxbytehs_csihost0
 * clk_rxbytehs_csihost1
 * iclk_dsi0
 * iclk_dsi1
 * iclk_vicap
 *
 *
 * ### 150M ###
 * dclk_lvds0 -- clk_d_lvds0_rklink_tx_cm --|-- clk_d_lvds0_pattern_gen_en
 *                                          |-- clk_d_lvds0_rklink_tx
 *
 * dclk_lvds1 -- clk_d_lvds1_rklink_tx_cm --|-- clk_d_lvds1_pattern_gen_en
 *                                          |-- clk_d_lvds1_rklink_tx
 *
 * clk_d_rgb_rklink_tx
 * pclkin_vicap_dvp_rx
 * rxpclk_lvds_align
 * rxpclk_vicap_lvds
 */

#define RKX110_SSCG_RXPLL_EN 0
#define RKX110_SSCG_CPLL_EN  0
#define RKX110_TESTOUT_MUX   -1 /* valid options: RKX110_TEST_CLKOUT_IOUT_SEL_? */

#define RKX110_GRF_GPIO0B_IOMUX_H 0x0001000c
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

static struct PLL_SETUP RXPLL_SETUP = {
    .id = PLL_RXPLL,
    .conOffset0 = 0x00000020,
    .conOffset1 = 0x00000024,
    .conOffset2 = 0x00000028,
    .conOffset3 = 0x0000002c,
    .modeOffset = 0x00000600,
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
    .sscgEn = RKX110_SSCG_RXPLL_EN,
};

static struct PLL_SETUP CPLL_SETUP = {
    .id = PLL_CPLL,
    .conOffset0 = 0x00000000,
    .conOffset1 = 0x00000004,
    .conOffset2 = 0x00000008,
    .conOffset3 = 0x0000000c,
    .modeOffset = 0x00000600,
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
    .sscgEn = RKX110_SSCG_CPLL_EN,
};

static uint32_t RKX11x_HAL_CRU_ClkGetFreq(struct hwclk *hw, uint32_t clockName)
{
    uint32_t pRate = 0, freq = 0;
    uint32_t clkMux, clkDiv;

    if (clockName == RKX110_CLK_D_DSI_0_PATTERN_GEN ||
        clockName == RKX110_CLK_D_DSI_1_PATTERN_GEN) {
        clockName = RKX110_CPS_DCLK_RX_PRE;
    }

    clkMux = CLK_GET_MUX(clockName);
    clkDiv = CLK_GET_DIV(clockName);

    switch (clockName) {
    case RKX110_CPS_PLL_RXPLL:
        hw->pllRate[RKX110_RXPLL] = HAL_CRU_GetPllFreq(hw, &RXPLL_SETUP);

        return hw->pllRate[RKX110_RXPLL];

    case RKX110_CPS_PLL_CPLL:
        hw->pllRate[RKX110_CPLL] = HAL_CRU_GetPllFreq(hw, &CPLL_SETUP);

        return hw->pllRate[RKX110_CPLL];

    /*
     * 400MHZ => down to 200M
     * === RX_DCLK_RX_PRE gate children ===
     *
     * dclk_dsi0
     * dclk_dsi1
     * dclk_vicap_csi
     *
     * clk_c_dvp_rklink_tx
     * clk_c_csi_rklink_tx
     *
     * clk_d_dsi_0_rklink_tx
     * clk_d_dsi_1_rklink_tx
     * clk_d_dsi_0_pattern_gen
     * clk_d_dsi_1_pattern_gen
     * clk_c_lvds_rklink_tx
     *
     * NOTE: `clk_d_rgb_rklink_tx` is an input clock.
     *
     */
    case RKX110_CPS_DCLK_RX_PRE:
    /* camera: 150M */
    case RKX110_CPS_CLK_CAM0_OUT2IO:
    case RKX110_CPS_CLK_CAM1_OUT2IO:
    case RKX110_CPS_CLK_CIF_OUT2IO:
    /* dsi: 200M */
    case RKX110_CPS_DCLK_D_DSI_0_REC_RKLINK_TX:
    case RKX110_CPS_DCLK_D_DSI_1_REC_RKLINK_TX:
    /* lvds: 300M */
    case RKX110_CPS_CLK_2X_LVDS_RKLINK_TX:
    /* i2s: 600M => down to 300M */
    case RKX110_CPS_CLK_I2S_SRC_RKLINK_TX:
        freq = HAL_CRU_MuxGetFreq3(hw, clkMux, hw->pllRate[RKX110_RXPLL],
                                   hw->pllRate[RKX110_CPLL], OSC_24M);
        break;

    /* mipi: ref_1000M, cfg_100M */
    case RKX110_CPS_CKREF_MIPIRXPHY0:
    case RKX110_CPS_CKREF_MIPIRXPHY1:
    case RKX110_CPS_CFGCLK_MIPIRXPHY0:
    case RKX110_CPS_CFGCLK_MIPIRXPHY1:
    /* pre-bus: 100M */
    case RKX110_CPS_BUSCLK_RX_PRE0:
        freq = HAL_CRU_MuxGetFreq2(hw, clkMux, hw->pllRate[RKX110_RXPLL],
                                   hw->pllRate[RKX110_CPLL]);
        break;

    /*
     * bus: 100MHZ => down to 24M
     *
     * === RX_BUSCLK_RX_PRE gate children ===
     *
     * pclk_rx_cru
     * pclk_rx_grf
     * pclk_rx_gpio0/1
     * pclk_rx_efuse
     * pclk_mipi_grf_rx0/1
     * pclk_rx_i2c2apb
     * pclk_rx_i2c2apb_debug
     * pclk_csihost0/1
     * pclk_rklink_tx
     * pclk_dsi_{0,1}_pattern_gen
     * pclk_lvds_{0,1}_pattern_gen
     * pclk_pcs0/1
     * pclk_pcs{0,1}_ada
     * pclk_mipirxphy0/1
     * pclk_dft2apb
     *
     * hclk_vicap
     * hclk_dsi0/1
     */
    case RKX110_CPS_BUSCLK_RX_PRE:
        pRate = RKX11x_HAL_CRU_ClkGetFreq(hw, RKX110_CPS_BUSCLK_RX_PRE0);
        freq = HAL_CRU_MuxGetFreq2(hw, clkMux, OSC_24M, pRate);
        break;

    case RKX110_CPS_CLK_PMA2LINK2PCS_CM:

        return _MHZ(200);

    /* gpio: 24M */
    case RKX110_CPS_DCLK_RX_GPIO0:
    case RKX110_CPS_DCLK_RX_GPIO1:
    /* efuse: 24M */
    case RKX110_CPS_RX_EFUSE:
    /* pcs_ada: 24M */
    case RKX110_CPS_PCS0_ADA:
    case RKX110_CPS_PCS1_ADA:

        return OSC_24M;

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

static HAL_Status RKX11x_HAL_CRU_ClkSetFreq(struct hwclk *hw, uint32_t clockName, uint32_t rate)
{
    uint32_t clkMux, clkDiv;
    uint32_t mux = 0, div = 1;
    uint32_t pRate = 0;
    uint32_t maxDiv;
    uint32_t pll;
    uint8_t overMax = 0;
    HAL_Status ret = HAL_OK;

    if (clockName == RKX110_CLK_D_DSI_0_PATTERN_GEN ||
        clockName == RKX110_CLK_D_DSI_1_PATTERN_GEN) {
        clockName = RKX110_CPS_DCLK_RX_PRE;
    }

    clkMux = CLK_GET_MUX(clockName);
    clkDiv = CLK_GET_DIV(clockName);

    switch (clockName) {
    case RKX110_CPS_PLL_RXPLL:
        ret = HAL_CRU_SetPllFreq(hw, &RXPLL_SETUP, rate);
        if (ret != HAL_OK) {
            CRU_ERR("%s: RXPLL set rate: %d failed\n", hw->name, rate);
        } else {
            hw->pllRate[RKX110_RXPLL] = rate;
            CRU_MSG("%s: RXPLL set rate: %d\n", hw->name, rate);
        }

        return ret;

    case RKX110_CPS_PLL_CPLL:
        ret = HAL_CRU_SetPllFreq(hw, &CPLL_SETUP, rate);
        if (ret != HAL_OK) {
            CRU_ERR("%s: CPLL set rate: %d failed\n", hw->name, rate);
        } else {
            hw->pllRate[RKX110_CPLL] = rate;
            CRU_MSG("%s: CPLL set rate: %d\n", hw->name, rate);
        }

        return ret;

    /* link(dclk): Allowed to change PLL rate if need ! */
    case RKX110_CPS_DCLK_D_DSI_0_REC_RKLINK_TX:
    case RKX110_CPS_DCLK_D_DSI_1_REC_RKLINK_TX:
    case RKX110_CPS_CLK_2X_LVDS_RKLINK_TX:
    /* i2s */
    case RKX110_CPS_CLK_I2S_SRC_RKLINK_TX:
        maxDiv = CLK_DIV_GET_MAXDIV(clkDiv);

        if (DIV_NO_REM(hw->pllRate[RKX110_RXPLL], rate, maxDiv)) {
            mux = 0;
            pRate = hw->pllRate[RKX110_RXPLL];
        } else if (DIV_NO_REM(hw->pllRate[RKX110_CPLL], rate, maxDiv)) {
            mux = 1;
            pRate = hw->pllRate[RKX110_CPLL];
        } else if (DIV_NO_REM(OSC_24M, rate, maxDiv)) {
            mux = 2;
            pRate = OSC_24M;
        } else {
            if (clockName == RKX110_CPS_CLK_I2S_SRC_RKLINK_TX) {
                pll = RKX110_CPS_PLL_CPLL;
                if (DIV_NO_REM(I2S_122888_BEST_PRATE, rate, maxDiv)) {
                    pRate = I2S_122888_BEST_PRATE;
                } else if (DIV_NO_REM(I2S_112896_BEST_PRATE, rate, maxDiv)) {
                    pRate = I2S_112896_BEST_PRATE;
                }
            } else {
                pll = RKX110_CPS_PLL_RXPLL;
            }

            /* PLL change closest new rate <= 1200M if need */
            if (!pRate) {
                pRate = (_MHZ(1200) / rate) * rate;
            }

            ret = RKX11x_HAL_CRU_ClkSetFreq(hw, pll, pRate);
            if (ret != HAL_OK) {
                return ret;
            }

            /* if success, continue to set divider */
        }
        break;

    /* bus */
    case RKX110_CPS_DCLK_RX_PRE:
    /* camera */
    case RKX110_CPS_CLK_CAM0_OUT2IO:
    case RKX110_CPS_CLK_CAM1_OUT2IO:
    case RKX110_CPS_CLK_CIF_OUT2IO:
        mux = HAL_CRU_RoundFreqGetMux3(hw, rate, hw->pllRate[RKX110_RXPLL],
                                       hw->pllRate[RKX110_CPLL], OSC_24M, &pRate);
        break;

    /* mipi */
    case RKX110_CPS_CKREF_MIPIRXPHY0:
    case RKX110_CPS_CKREF_MIPIRXPHY1:
    case RKX110_CPS_CFGCLK_MIPIRXPHY0:
    case RKX110_CPS_CFGCLK_MIPIRXPHY1:
    /* pre-bus */
    case RKX110_CPS_BUSCLK_RX_PRE0:
        mux = HAL_CRU_RoundFreqGetMux2(hw, rate, hw->pllRate[RKX110_RXPLL],
                                       hw->pllRate[RKX110_CPLL], &pRate);
        break;

    /* bus */
    case RKX110_CPS_BUSCLK_RX_PRE:
        if (rate == OSC_24M) {
            return HAL_CRU_ClkSetMux(hw, clkMux, 0);
        } else {
            RKX11x_HAL_CRU_ClkSetFreq(hw, RKX110_CPS_BUSCLK_RX_PRE0, rate);

            return HAL_CRU_ClkSetMux(hw, clkMux, 1);
        }
        break;

    /* gpio */
    case RKX110_CPS_DCLK_RX_GPIO0:
    case RKX110_CPS_DCLK_RX_GPIO1:
    /* efuse */
    case RKX110_CPS_RX_EFUSE:
    /* pcs_ada */
    case RKX110_CPS_PCS0_ADA:
    case RKX110_CPS_PCS1_ADA:

        return rate == OSC_24M ? 0 : HAL_INVAL;

    case RKX110_CPS_CLK_PMA2LINK2PCS_CM:
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

#if RKX110_SSCG_CPLL_EN || RKX110_SSCG_RXPLL_EN
static void RKX11x_HAL_CRU_Init_SSCG(struct hwclk *hw)
{
    uint8_t down_spread = 1; /* 0: center spread */
    uint8_t amplitude = 8;   /* range: 0x00 - 0x1f */

#if RKX110_SSCG_CPLL_EN
    /* down-spread, 0.8%, 37.5khz */
    HAL_CRU_Write(hw, hw->cru_base + 0x0c, 0x1f000000 | ((amplitude & 0x1f) << 8));
    HAL_CRU_Write(hw, hw->cru_base + 0x0c, 0x00f00050);
    HAL_CRU_Write(hw, hw->cru_base + 0x0c, 0x00080000 | ((down_spread & 0x1) << 3));
    HAL_CRU_Write(hw, hw->cru_base + 0x04, 0x10000000);
    HAL_CRU_Write(hw, hw->cru_base + 0x0c, 0x00070000);
    CRU_MSG("%s: CPLL enable SSCG\n", hw->name);
#endif
#if RKX110_SSCG_RXPLL_EN
    /* down-spread, 0.8%, 37.5khz */
    HAL_CRU_Write(hw, hw->cru_base + 0x2c, 0x1f000000 | ((amplitude & 0x1f) << 8));
    HAL_CRU_Write(hw, hw->cru_base + 0x2c, 0x00f00050);
    HAL_CRU_Write(hw, hw->cru_base + 0x2c, 0x00080000 | ((down_spread & 0x1) << 3));
    HAL_CRU_Write(hw, hw->cru_base + 0x24, 0x10000000);
    HAL_CRU_Write(hw, hw->cru_base + 0x2c, 0x00070000);
    CRU_MSG("%s: RXPLL enable SSCG\n", hw->name);
#endif
}
#endif

static HAL_Status RKX11x_HAL_CRU_InitTestout(struct hwclk *hw, uint32_t clockName,
                                             uint32_t muxValue, uint32_t divValue)
{
    uint32_t clkMux = CLK_GET_MUX(clockName);
    uint32_t clkDiv = CLK_GET_DIV(clockName);

    /* gpio0_b7: iomux to clk_testout */
    HAL_CRU_Write(hw, RKX110_GRF_GPIO0B_IOMUX_H, GPIO0B7_TEST_CLKOUT);

    /* Enable clock */
    HAL_CRU_ClkEnable(hw, RKX110_CLK_TESTOUT_TOP_GATE);

    /* Mux, div */
    HAL_CRU_ClkSetDiv(hw, clkDiv, divValue);
    HAL_CRU_ClkSetMux(hw, clkMux, muxValue);

    CRU_MSG("%s: Testout div=%d, mux=%d\n", hw->name, divValue, muxValue);

    return HAL_OK;
}

static HAL_Status RKX11x_HAL_CRU_Init(struct hwclk *hw, struct xferclk *xfer)
{
    hw->cru_base = 0x0;
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
    strcat(hw->name, "<CRU.110@");
    strcat(hw->name, xfer->name);
    strcat(hw->name, ">");

    /* Don't change order */
#if RKX110_SSCG_CPLL_EN || RKX110_SSCG_RXPLL_EN
    RKX11x_HAL_CRU_Init_SSCG(hw);
#endif
    RKX11x_HAL_CRU_ClkSetFreq(hw, RKX110_CPS_PLL_CPLL, _MHZ(1200));
    RKX11x_HAL_CRU_ClkSetFreq(hw, RKX110_CPS_PLL_RXPLL, _MHZ(1188));
    RKX11x_HAL_CRU_ClkSetFreq(hw, RKX110_CPS_BUSCLK_RX_PRE, _MHZ(24));
    RKX11x_HAL_CRU_ClkSetFreq(hw, RKX110_CPS_CLK_PMA2LINK2PCS_CM, _MHZ(200));
    RKX11x_HAL_CRU_ClkSetFreq(hw, RKX110_CPS_DCLK_RX_PRE, _MHZ(200));
    RKX11x_HAL_CRU_ClkSetFreq(hw, RKX110_CPS_CLK_I2S_SRC_RKLINK_TX, _MHZ(300));
    RKX11x_HAL_CRU_ClkSetFreq(hw, RKX110_CPS_CFGCLK_MIPIRXPHY0, _MHZ(100));

    HAL_CRU_ClkEnable(hw, RKX110_CLK_TESTOUT_TOP_GATE);

#if RKX110_TESTOUT_MUX >= 0
    /* clk_testout support max 150M output, so set div=10 by default if not 24M */
    RKX11x_HAL_CRU_InitTestout(hw, RKX110_CPS_TEST_CLKOUT, RKX110_TESTOUT_MUX,
                               RKX110_TESTOUT_MUX == 0 ? 1 : 10);
#endif

    return HAL_OK;
}

PNAME(mux_24m_p) = { "xin24m" };
PNAME(mux_rxpll_cpll_p) = { "rxpll", "cpll" };
PNAME(mux_rxpll_cpll_24m_p) = { "rxpll", "cpll", "xin24m" };
PNAME(mux_rxpre0_24m_p) = { "xin24m", "busclk_rx_pre0" };

#define CAL_FREQ_REG 0xf00

static uint32_t RKX11x_HAL_CRU_ClkGetExtFreq(struct hwclk *hw, uint32_t clk)
{
    uint32_t clkMux = CLK_GET_MUX(RKX110_CPS_TEST_CLKOUT);
    uint32_t clkDiv = CLK_GET_DIV(RKX110_CPS_TEST_CLKOUT);
    uint32_t freq, mhz;
    uint8_t div = 10;

    HAL_CRU_ClkSetDiv(hw, clkDiv, div);
    HAL_CRU_ClkSetMux(hw, clkMux, 0);
    HAL_SleepMs(2);
    HAL_CRU_ClkSetMux(hw, clkMux, clk);
    HAL_SleepMs(2);
    freq = HAL_CRU_Read(hw, CAL_FREQ_REG);

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

static struct clkTable rkx11x_clkTable[] = {
    /* internal */
    CLK_DECLARE_INT("rxpll", RKX110_CPS_PLL_RXPLL, mux_24m_p),
    CLK_DECLARE_INT("cpll", RKX110_CPS_PLL_CPLL, mux_24m_p),
    CLK_DECLARE_INT("dclk_rx_pre", RKX110_CPS_DCLK_RX_PRE, mux_rxpll_cpll_24m_p),
    CLK_DECLARE_INT("clk_d_dsi_0_pattern", RKX110_CPS_DCLK_RX_PRE, mux_rxpll_cpll_24m_p),
    CLK_DECLARE_INT("clk_d_dsi_1_pattern", RKX110_CPS_DCLK_RX_PRE, mux_rxpll_cpll_24m_p),
    CLK_DECLARE_INT("clk_cam0_out2io", RKX110_CPS_CLK_CAM0_OUT2IO, mux_rxpll_cpll_24m_p),
    CLK_DECLARE_INT("clk_cam1_out2io", RKX110_CPS_CLK_CAM1_OUT2IO, mux_rxpll_cpll_24m_p),
    CLK_DECLARE_INT("clk_cif_out2io", RKX110_CPS_CLK_CIF_OUT2IO, mux_rxpll_cpll_24m_p),
    CLK_DECLARE_INT("dclk_d_dsi_0_rec_rklink_tx", RKX110_CPS_DCLK_D_DSI_0_REC_RKLINK_TX, mux_rxpll_cpll_24m_p),
    CLK_DECLARE_INT("dclk_d_dsi_1_rec_rklink_tx", RKX110_CPS_DCLK_D_DSI_1_REC_RKLINK_TX, mux_rxpll_cpll_24m_p),
    CLK_DECLARE_INT("clk_2x_lvds_rklink_tx", RKX110_CPS_CLK_2X_LVDS_RKLINK_TX, mux_rxpll_cpll_24m_p),
    CLK_DECLARE_INT("clk_i2s_src_rklink_tx", RKX110_CPS_CLK_I2S_SRC_RKLINK_TX, mux_rxpll_cpll_24m_p),
    CLK_DECLARE_INT("ckref_mipirxphy0", RKX110_CPS_CKREF_MIPIRXPHY0, mux_rxpll_cpll_p),
    CLK_DECLARE_INT("ckref_mipirxphy1", RKX110_CPS_CKREF_MIPIRXPHY1, mux_rxpll_cpll_p),
    CLK_DECLARE_INT("cfgclk_mipirxphy0", RKX110_CPS_CFGCLK_MIPIRXPHY0, mux_rxpll_cpll_p),
    CLK_DECLARE_INT("cfgclk_mipirxphy1", RKX110_CPS_CFGCLK_MIPIRXPHY1, mux_rxpll_cpll_p),
    CLK_DECLARE_INT("busclk_rx_pre0", RKX110_CPS_BUSCLK_RX_PRE0, mux_rxpll_cpll_p),
    CLK_DECLARE_INT("busclk_rx_pre", RKX110_CPS_BUSCLK_RX_PRE, mux_rxpre0_24m_p),
    CLK_DECLARE_INT("dclk_rx_gpio0", RKX110_CPS_DCLK_RX_GPIO0, mux_24m_p),
    CLK_DECLARE_INT("dclk_rx_gpio1", RKX110_CPS_DCLK_RX_GPIO1, mux_24m_p),
    CLK_DECLARE_INT("rx_efuse", RKX110_CPS_RX_EFUSE, mux_24m_p),
    CLK_DECLARE_INT("pcs0_ada", RKX110_CPS_PCS0_ADA, mux_24m_p),
    CLK_DECLARE_INT("pcs1_ada", RKX110_CPS_PCS1_ADA, mux_24m_p),

    /* external */
    CLK_DECLARE_EXT("xin24m", 0, RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("rxpclk_vicap_lvds", 3, RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_rxbytehs_csihost0", 4, RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_rxbytehs_csihost1", 5, RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_d_rgb_rklink_tx", 6, RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("dclk_lvds0", 7, RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("dclk_lvds1", 8, RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_link_pcs0", 9, RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_link_pcs1", 10, RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("pclkin_vicap_dvp_rx", 21, RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("iclk_dsi0", 22, RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("iclk_dsi1", 23, RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("rxpclk_lvds_align", 24, RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_8x_pma2pcs0", 25, RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT("clk_8x_pma2pcs1", 26, RKX11x_HAL_CRU_ClkGetExtFreq),

    CLK_DECLARE_EXT_PARENT("clk_d_lvds0_rklink_tx_cm", 7, "dclk_lvds0", RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT_PARENT("clk_d_lvds0_rklink_tx", 7, "clk_d_lvds0_rklink_tx_cm", RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT_PARENT("clk_d_lvds0_pattern_gen_en", 7, "clk_d_lvds0_rklink_tx_cm", RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT_PARENT("clk_d_lvds1_rklink_tx_cm", 8, "dclk_lvds1", RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT_PARENT("clk_d_lvds1_rklink_tx", 8, "clk_d_lvds1_rklink_tx_cm", RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT_PARENT("clk_d_lvds1_pattern_gen_en", 8, "clk_d_lvds1_rklink_tx_cm", RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT_PARENT("clk_pma2pcs0", 9, "clk_link_pcs0", RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT_PARENT("clk_pma2link2pcs_cm", 9, "clk_link_pcs0", RKX11x_HAL_CRU_ClkGetExtFreq),
    CLK_DECLARE_EXT_PARENT("clk_pma2pcs1", 9, "clk_link_pcs1", RKX11x_HAL_CRU_ClkGetExtFreq),

    { /* sentinel */ },
};

struct clkOps rkx110_clk_ops =
{
    .clkTable = rkx11x_clkTable,
    .clkInit = RKX11x_HAL_CRU_Init,
    .clkGetFreq = RKX11x_HAL_CRU_ClkGetFreq,
    .clkSetFreq = RKX11x_HAL_CRU_ClkSetFreq,
    .clkInitTestout = RKX11x_HAL_CRU_InitTestout,
};
