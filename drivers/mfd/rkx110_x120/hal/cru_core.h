/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2022 Rockchip Electronics Co. Ltd.
 *
 * Author: Joseph Chen <chenjh@rock-chips.com>
 */

#ifndef _CRU_CORE_H_
#define _CRU_CORE_H_

#include "hal_def.h"
#include "hal_os_def.h"

#define DEBUG_CRU_INIT 0
#define CRU_LOGLEVEL   3

#define __cru_print(level, fmt, ...)                                        \
({                                                                          \
    level < CRU_LOGLEVEL ? HAL_SYSLOG("[HAL CRU] " fmt, ##__VA_ARGS__) : 0; \
})
#define CRU_ERR(fmt, ...)  __cru_print(0, "ERROR: " fmt, ##__VA_ARGS__)
#define CRU_WARN(fmt, ...) __cru_print(1, "WARN: " fmt, ##__VA_ARGS__)
#define CRU_MSG(fmt, ...)  __cru_print(2, fmt, ##__VA_ARGS__)
#define CRU_DBG(fmt, ...)  __cru_print(3, fmt, ##__VA_ARGS__) /* driver */
#define CRU_DBGF(fmt, ...) __cru_print(4, fmt, ##__VA_ARGS__) /* core function */
#define CRU_DBGR(fmt, ...) __cru_print(5, fmt, ##__VA_ARGS__) /* core register */

#define HAL_ENABLE_SET_RATE_VERIFY 0

#define _MHZ(n)           ((n) * 1000000U)
#define OSC_24M           _MHZ(24)
#define HAL_HWCLK_MAX_NUM 16
#define PLL_MAX_NUM       2

typedef enum {
    RKX110_CPLL = 0,
    RKX110_RXPLL,
    RKX120_CPLL = 0,
    RKX120_TXPLL,
} HAL_PLLType;

typedef enum {
    CLK_UNDEF,
    CLK_RKX110,
    CLK_RKX120,
    CLK_ALL,
    CLK_MAX,
} HAL_ClockType;

/*
 * hwclk flags
 */
#define CLK_FLG_SET_RATE_VERIFY (1 << 0)  /* set and readback verify */

struct hwclk;

struct xferclk {
    HAL_ClockType type;
    u32 version;
    char *name; /* slave addr is expected */
    void *client;
    HAL_RegRead_t *read;
    HAL_RegWrite_t *write;
};

struct clkTable {
    uint8_t type;
    const char *name;
    uint32_t clk;
    const char * *parents;
    uint32_t numParents;
    const char *extParent;
    uint32_t (*getFreq)(struct hwclk *hw, uint32_t clockName);
};

struct clkOps {
    struct clkTable *clkTable;
    HAL_Status (*clkInit)(struct hwclk *hw, struct xferclk *xfer);
    HAL_Status (*clkInitTestout)(struct hwclk *hw, uint32_t clockName,
                                 uint32_t muxValue, uint32_t divValue);
    HAL_Status (*clkSetFreq)(struct hwclk *hw, uint32_t clockName, uint32_t rate);
    uint32_t (*clkGetFreq)(struct hwclk *hw, uint32_t clockName);
};

struct clkGate {
    uint8_t enable_count;
};

struct hwclk {
    char name[32];
    uint32_t flags;
    HAL_ClockType type;
    uint32_t pllRate[PLL_MAX_NUM];

    uint32_t cru_base;
    uint32_t gate_con0;
    uint32_t sel_con0;
    uint32_t softrst_con0;
    uint32_t gbl_srst_fst;

    struct xferclk xfer;
    struct clkOps *ops;
    struct clkGate *gate;
    uint32_t num_gate;

    HAL_Mutex lock;
};

#define MASK_TO_WE(msk)       ((msk) << 16)
#define VAL_MASK_WE(msk, val) ((MASK_TO_WE(msk)) | (val))
#define WRITE_REG_MASK_WE(reg, msk, val) \
                 WRITE_REG(reg, (VAL_MASK_WE(msk, val)))

#define _GENMASK(h, l)           (((~0UL) << (l)) & (~0UL >> (HAL_BITS_PER_LONG - 1 - (h))))
#define _GENVAL(x, h, l)         ((uint32_t)(((x) & _GENMASK(h, l)) >> (l)))
#define _GENVAL_D16(x, h, l)     ((uint32_t)(((x) & _GENMASK(h, l)) / 16))
#define _GENVAL_D16_REM(x, h, l) ((uint32_t)(((x) & _GENMASK(h, l)) % 16))
#define _WIDTH_TO_MASK(w)        ((1 << (w)) - 1)

/*
 * RESET/GATE fields:
 *   [31:16]: reserved
 *   [15:12]: bank
 *   [11:0]:  id
 */
#define CLK_RESET_GET_REG_OFFSET(x) _GENVAL_D16(x, 11, 0)
#define CLK_RESET_GET_BITS_SHIFT(x) _GENVAL_D16_REM(x, 11, 0)
#define CLK_RESET_GET_REG_BANK(x)   _GENVAL(x, 15, 12)

#define CLK_GATE_GET_REG_OFFSET(x) CLK_RESET_GET_REG_OFFSET(x)
#define CLK_GATE_GET_BITS_SHIFT(x) CLK_RESET_GET_BITS_SHIFT(x)
#define CLK_GATE_GET_REG_BANK(x)   CLK_RESET_GET_REG_BANK(x)

/*
 * MUX/DIV fields:
 *   [31:24]: width
 *   [23:16]: shift
 *   [15:12]: reserved
 *   [11:8]:  bank
 *   [7:0]:   reg
 */
#define CLK_MUX_GET_REG_OFFSET(x) _GENVAL(x, 7,  0)
#define CLK_MUX_GET_BANK(x)       _GENVAL(x, 11, 8)
#define CLK_MUX_GET_BITS_SHIFT(x) _GENVAL(x, 23, 16)
#define CLK_MUX_GET_WIDTH(x)      _GENVAL(x, 31, 24)
#define CLK_MUX_GET_MASK(x)       (_WIDTH_TO_MASK(CLK_MUX_GET_WIDTH(x)) << CLK_MUX_GET_BITS_SHIFT(x))

#define CLK_DIV_GET_REG_OFFSET(x) CLK_MUX_GET_REG_OFFSET(x)
#define CLK_DIV_GET_BANK(x)       CLK_MUX_GET_BANK(x)
#define CLK_DIV_GET_BITS_SHIFT(x) CLK_MUX_GET_BITS_SHIFT(x)
#define CLK_DIV_GET_WIDTH(x)      CLK_MUX_GET_WIDTH(x)
#define CLK_DIV_GET_MASK(x)       CLK_MUX_GET_MASK(x)
#define CLK_DIV_GET_MAXDIV(x)     ((1 << CLK_DIV_GET_WIDTH(x)) - 1)

#define CLK_GET_MUX(v32) \
        ((uint32_t)((v32) & 0x0F0F00FFU))
#define CLK_GET_DIV(v32)                           \
        ((uint32_t)((((v32) & 0x0000FF00U) >> 8) | \
        (((v32) & 0xF0F00000U) >> 4)))
#define COMPOSITE_CLK(mux, div)                           \
        (((mux) & 0x0F0F00FFU) | (((div) & 0xFFU) << 8) | \
        (((div) & 0x0F0F0000U) << 4))

#define PNAME(x) static const char *x[]

typedef enum {
    DUMP_UNDEF,
    DUMP_INT,
    DUMP_EXT,
} HAL_DumpType;

#define CLK_DECLARE(_type, _name, _clk, _parents, _numParents, _extParent, _getFreq) \
{                                                                                    \
    .type = _type,                                                                   \
    .name = _name,                                                                   \
    .clk = _clk,                                                                     \
    .parents = _parents,                                                             \
    .numParents = _numParents,                                                       \
    .extParent = _extParent,                                                         \
    .getFreq = _getFreq,                                                             \
}

#define CLK_DECLARE_INT(_name, _clk, _parents) \
    CLK_DECLARE(DUMP_INT, _name, _clk, _parents, HAL_ARRAY_SIZE(_parents), HAL_NULL, HAL_NULL)

#define CLK_DECLARE_EXT_PARENT(_name, _clk, _extParent, _getFreq) \
    CLK_DECLARE(DUMP_EXT, _name, _clk, HAL_NULL, 0, _extParent, _getFreq)

#define CLK_DECLARE_EXT(_name, _clk, _getFreq) \
    CLK_DECLARE_EXT_PARENT(_name, _clk, HAL_NULL, _getFreq)

#define RK_PLL_RATE(_rate, _refdiv, _fbdiv, _postdiv1, _postdiv2, _dsmpd, _frac) \
    {                                                                            \
        .rate = _rate##U, .fbDiv = _fbdiv, .postDiv1 = _postdiv1,                \
        .refDiv = _refdiv, .postDiv2 = _postdiv2, .dsmpd = _dsmpd,               \
        .frac = _frac,                                                           \
    }

#define DIV_NO_REM(pFreq, freq, maxDiv) \
        ((!((pFreq) % (freq))) && ((pFreq) / (freq) <= (maxDiv)))

#define HAL_DIV_ROUND_UP(x, y) (((x) + (y) - 1) / (y))

/***************************** Structure Definition **************************/

struct PLL_CONFIG {
    uint32_t rate;

    union {
        struct {
            uint32_t fbDiv;
            uint32_t postDiv1;
            uint32_t refDiv;
            uint32_t postDiv2;
            uint32_t dsmpd;
            uint32_t frac;
        };
    };
};

typedef enum {
    PLL_CPLL,
    PLL_RXPLL,
    PLL_TXPLL,
} eCRU_PLL;

struct PLL_SETUP {
    eCRU_PLL id;
    uint32_t conOffset0;
    uint32_t conOffset1;
    uint32_t conOffset2;
    uint32_t conOffset3;
    uint32_t conOffset6;
    uint32_t modeOffset;
    uint32_t stat0;
    uint32_t modeShift;
    uint32_t lockShift;
    uint32_t modeMask;
    const struct PLL_CONFIG *rateTable;
    uint8_t minRefdiv;
    uint8_t maxRefdiv;
    uint32_t minVco;
    uint32_t maxVco;
    uint32_t minFout;
    uint32_t maxFout;
    uint8_t sscgEn;
};

typedef enum {
    GLB_SRST_FST = 0xfdb9,
    GLB_SRST_SND = 0xeca8,
} eCRU_GlbSrstType;

/***************************** Function Declare ******************************/
uint32_t HAL_CRU_Read(struct hwclk *hw, uint32_t reg);
uint32_t HAL_CRU_Write(struct hwclk *hw, uint32_t reg, uint32_t val);
uint32_t HAL_CRU_WriteMask(struct hwclk *hw, uint32_t reg,
                           uint32_t msk, uint32_t val);

int HAL_CRU_FreqGetMux4(struct hwclk *hw,
                        uint32_t freq, uint32_t freq0,
                        uint32_t freq1, uint32_t freq2, uint32_t freq3);
int HAL_CRU_FreqGetMux3(struct hwclk *hw,
                        uint32_t freq, uint32_t freq0,
                        uint32_t freq1, uint32_t freq2);
int HAL_CRU_FreqGetMux2(struct hwclk *hw,
                        uint32_t freq, uint32_t freq0, uint32_t freq1);

uint32_t HAL_CRU_MuxGetFreq4(struct hwclk *hw, uint32_t muxName,
                             uint32_t freq0, uint32_t freq1,
                             uint32_t freq2, uint32_t freq3);
uint32_t HAL_CRU_MuxGetFreq3(struct hwclk *hw, uint32_t muxName,
                             uint32_t freq0, uint32_t freq1, uint32_t freq2);
uint32_t HAL_CRU_MuxGetFreq2(struct hwclk *hw, uint32_t muxName,
                             uint32_t freq0, uint32_t freq1);

int HAL_CRU_RoundFreqGetMux4(struct hwclk *hw, uint32_t freq,
                             uint32_t pFreq0, uint32_t pFreq1,
                             uint32_t pFreq2, uint32_t pFreq3, uint32_t *pFreqOut);
int HAL_CRU_RoundFreqGetMux3(struct hwclk *hw, uint32_t freq,
                             uint32_t pFreq0, uint32_t pFreq1,
                             uint32_t pFreq2, uint32_t *pFreqOut);
int HAL_CRU_RoundFreqGetMux2(struct hwclk *hw, uint32_t freq,
                             uint32_t pFreq0, uint32_t pFreq1, uint32_t *pFreqOut);

/**
 * @brief Get pll freq.
 * @param  pSetup: Contains PLL register parameters
 * @return pll rate.
 */
uint32_t HAL_CRU_GetPllFreq(struct hwclk *hw, struct PLL_SETUP *pSetup);

/**
 * @brief Set pll freq.
 * @param  pSetup: Contains PLL register parameters
 * @param  rate: pll set
 * @return HAL_Status.
 */
HAL_Status HAL_CRU_SetPllFreq(struct hwclk *hw, struct PLL_SETUP *pSetup, uint32_t rate);

/**
 * @brief Set pll power up.
 * @param  pSetup: Contains PLL register parameters
 * @return HAL_Status.
 */
HAL_Status HAL_CRU_SetPllPowerUp(struct hwclk *hw, struct PLL_SETUP *pSetup);

/**
 * @brief Set pll power down.
 * @param  pSetup: Contains PLL register parameters
 * @return HAL_Status.
 */
HAL_Status HAL_CRU_SetPllPowerDown(struct hwclk *hw, struct PLL_SETUP *pSetup);

/**
 * @brief Check if clk is enabled
 * @param  clk: clock to check
 * @return HAL_Check.
 */
HAL_Check HAL_CRU_ClkIsEnabled(struct hwclk *hw, uint32_t clk);

/**
 * @brief Enable clk
 * @param  clk: clock to set
 * @return HAL_Status.
 */
HAL_Status HAL_CRU_ClkEnable(struct hwclk *hw, uint32_t clk);

/**
 * @brief Disable clk
 * @param  clk: clock to set
 * @return HAL_Status.
 */
HAL_Status HAL_CRU_ClkDisable(struct hwclk *hw, uint32_t clk);

/**
 * @brief Check if clk is reset
 * @param  clk: clock to check
 * @return HAL_Check.
 */
HAL_Check HAL_CRU_ClkIsReset(struct hwclk *hw, uint32_t clk);

/**
 * @brief Assert the reset to the clk
 * @param  clk: clock to assert
 * @return HAL_Status.
 */
HAL_Status HAL_CRU_ClkResetAssert(struct hwclk *hw, uint32_t clk);

/**
 * @brief Deassert the reset to the clk
 * @param  clk: clock to deassert
 * @return HAL_Status.
 */
HAL_Status HAL_CRU_ClkResetDeassert(struct hwclk *hw, uint32_t clk);

/**
 * @brief  Set integer div
 * @param  divName: div id(struct hwclk *hw, Contains div offset, shift, mask information)
 * @param  divValue: div value
 * @return NONE
 */
HAL_Status HAL_CRU_ClkSetDiv(struct hwclk *hw, uint32_t divName, uint32_t divValue);

/**
 * @brief  Get integer div
 * @param  divName: div id (struct hwclk *hw,, Contains div offset, shift, mask information)
 * @return div value
 */
uint32_t HAL_CRU_ClkGetDiv(struct hwclk *hw, uint32_t divName);

/**
 * @brief  Set mux
 * @param  muxName: mux id (struct hwclk *hw,, Contains mux offset, shift, mask information)
 * @param  muxValue: mux value
 * @return NONE
 */
HAL_Status HAL_CRU_ClkSetMux(struct hwclk *hw, uint32_t muxName, uint32_t muxValue);

/**
 * @brief  Get mux
 * @param  muxName: mux id (struct hwclk *hw, Contains mux offset, shift, mask information)
 * @return mux value
 */
uint32_t HAL_CRU_ClkGetMux(struct hwclk *hw, uint32_t muxName);

/**
 * @brief Get clk freq.
 * @param  clockName: CLOCK_Name id.
 * @return rate.
 * @attention these APIs allow direct use in the HAL layer.
 */
uint32_t HAL_CRU_ClkGetFreq(struct hwclk *hw, uint32_t clockName);

/**
 * @brief Set clk freq.
 * @param  clockName: CLOCK_Name id.
 * @param  rate: clk rate.
 * @return HAL_Status.
 * @attention these APIs allow direct use in the HAL layer.
 */
HAL_Status HAL_CRU_ClkSetFreq(struct hwclk *hw, uint32_t clockName, uint32_t rate);

/**
 * @brief  assert CRU global software reset.
 * @param  type: global software reset type.
 * @return HAL_INVAL if the SoC does not support.
 */
HAL_Status HAL_CRU_SetGlbSrst(struct hwclk *hw, eCRU_GlbSrstType type);

/**
 * @brief  Set clk testout
 * @param  clockName: CLOCK_Name id.
 * @param  muxValue: mux value.
 * @param  divValue: div value.
 * @return HAL_INVAL if the SoC does not support.
 */
HAL_Status HAL_CRU_ClkSetTestout(struct hwclk *hw, uint32_t clockName,
                                 uint32_t muxValue, uint32_t divValue);
/**
 * @brief  Dump all clock tree
 */
void HAL_CRU_ClkDumpTree(HAL_ClockType type);

/**
 * @brief  CRU init for chip
 * @return HAL_INVAL if the SoC does not support.
 */
HAL_Status HAL_CRU_Init(void);

/**
 * @brief  Register CRU
 * @param  xfer: the data to register
 * @return hwclk descriptor.
 */
struct hwclk *HAL_CRU_Register(struct xferclk xfer);

/**
 * @brief  Get hwclk
 * @param  client: the unit data to find hwclk
 * @return hwclk descriptor.
 */
struct hwclk *HAL_CRU_ClkGet(void *client);

/************************** chip ops *************************************/
extern struct clkOps rkx110_clk_ops;
extern struct clkOps rkx120_clk_ops;
extern struct clkOps rkx111_clk_ops;
extern struct clkOps rkx121_clk_ops;
#endif
