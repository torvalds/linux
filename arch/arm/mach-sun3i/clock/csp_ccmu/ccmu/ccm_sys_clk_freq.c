/*
 * arch/arm/mach-sun3i/clock/csp_ccmu/ccmu/ccm_sys_clk_freq.c
 *
 * (C) Copyright 2007-2012
 * Allwinner Technology Co., Ltd. <www.allwinnertech.com>
 * Kevin Zhang <kevin@allwinnertech.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.	 See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston,
 * MA 02111-1307 USA
 */

#include "ccm_i.h"
#include "spec_ccm.h"


#define _CORE_VE_PLL_MAX_FREQ    ( (6 * ((1U<<7) - 1 ) + 30 ) * MHz) ///???
#define _SDRAM_PLL_MAX_FREQ      (MHz*(60 + 12*63))//factor< (1<<6)

/*********************************************************************
* Method	 :    		uIntDiv
* Description: 试探法实现无符号数除数获得商
* Parameters :
	@unsigned divisor ：除数
	@unsigned n: 被除数
	@unsigned maxQuoLen ：商的最大位数，即quotient<= 2^N
* Returns    :   quotient
* Note       :
*********************************************************************/
static unsigned uIntDiv(unsigned divisor, unsigned n, unsigned maxQuoLen)
{
    unsigned quotient   = 0;
    unsigned remainder  = n;/////////////////////////////////

    do
    {
        maxQuoLen--;
        if ((remainder>>maxQuoLen) >= divisor){
            remainder -= (divisor<<maxQuoLen);  //update remainder
            quotient  += (1<<maxQuoLen);        //update quotient
        }
    } while (maxQuoLen);

    return quotient;
}

#define _CCM_PLL
#ifdef _CCM_PLL
#if 0 // bias current not used now.
static s32
_pll_set_bias_current( CSP_CCM_sysClkNo_t clkNo, __u8 biasCurrent)
{
    switch (clkNo)
    {
    case CSP_CCM_SYS_CLK_CORE_PLL:
        CORE_PLL_SET_BIAS_CURRENT(biasCurrent);
        break;

    case CSP_CCM_SYS_CLK_VE_PLL:
        VE_PLL_SET_BIAS_CURRENT(biasCurrent);
        break;

    case CSP_CCM_SYS_CLK_AUDIO_PLL:
        AUDIO_PLL_SET_BIAS_CURRENT(biasCurrent);
        break;

    case CSP_CCM_SYS_CLK_SDRAM_PLL:
        SET_BITS(CCM_SDRAM_PLL_R, 8, 2, biasCurrent);
        break;

    case CSP_CCM_SYS_CLK_VIDEO_PLL0:
        SET_BITS(CCM_VIDEO_PLL_R, 5, 2, biasCurrent);
        break;

    case CSP_CCM_SYS_CLK_VIDEO_PLL1:
        SET_BITS(CCM_VIDEO_PLL_R, 21, 2, biasCurrent);
        break;

    default:
        break;
    }

    return AW_CCMU_OK;
}

static u8
_pll_get_bias_current( CSP_CCM_sysClkNo_t clkNo)
{
    switch (clkNo)
    {
    case CSP_CCM_SYS_CLK_CORE_PLL:
        return CORE_PLL_GET_BIAS_CURRENT;

    case CSP_CCM_SYS_CLK_VE_PLL:
        return VE_PLL_GET_BIAS_CURRENT;

    case CSP_CCM_SYS_CLK_AUDIO_PLL:
        return AUDIO_PLL_GET_BIAS_CURRENT;

    case CSP_CCM_SYS_CLK_SDRAM_PLL:
        return GET_BITS_VAL(CCM_SDRAM_PLL_R, 8, 2);

    case CSP_CCM_SYS_CLK_VIDEO_PLL0:
        return GET_BITS_VAL(CCM_VIDEO_PLL_R, 5, 2);

    case CSP_CCM_SYS_CLK_VIDEO_PLL1:
        return GET_BITS_VAL(CCM_VIDEO_PLL_R, 21, 2);

    default:
        break;
    }

    return 0;
}
#endif//bias current end

static s32
_pll_set_bypass( CSP_CCM_sysClkNo_t clkNo, s32 enableBypass)
{
    switch (clkNo)
    {
    case CSP_CCM_SYS_CLK_CORE_PLL:
        enableBypass ? CORE_PLL_OUTPUT_BYPASS_ENABLE : CORE_PLL_OUTPUT_BYPASS_DISABLE;
        break;

    case CSP_CCM_SYS_CLK_VE_PLL:
        enableBypass ? VE_PLL_OUTPUT_BYPASS_ENABLE : VE_PLL_OUTPUT_BYPASS_DISABLE;
        break;

    case CSP_CCM_SYS_CLK_SDRAM_PLL:
        enableBypass ? SET_BIT(CCM_SDRAM_PLL_R, 10) : CLEAR_BIT(CCM_SDRAM_PLL_R, 10);
        break;

    case  CSP_CCM_SYS_CLK_AUDIO_PLL:
    case  CSP_CCM_SYS_CLK_VIDEO_PLL0:
    case  CSP_CCM_SYS_CLK_VIDEO_PLL1:
    case  CSP_CCM_SYS_CLK_AUDIO_PLL_4X:
    case  CSP_CCM_SYS_CLK_VIDEO_PLL0_2X:
    case  CSP_CCM_SYS_CLK_VIDEO_PLL1_2X:
    default:
        break;
    }

    return AW_CCMU_OK;
}

//get byPass whose pll has "byPass" parameter
static s32
_pll_bypass_is_able( CSP_CCM_sysClkNo_t clkNo)
{
    switch (clkNo)
    {
    case CSP_CCM_SYS_CLK_CORE_PLL:
        return CORE_PLL_OUTPUT_BYPASS_IS_ABLE;

    case CSP_CCM_SYS_CLK_VE_PLL:
        return VE_PLL_OUTPUT_BYPASS_IS_ABLE;

    case CSP_CCM_SYS_CLK_SDRAM_PLL:
        return TEST_BIT(CCM_SDRAM_PLL_R, 10);

    case  CSP_CCM_SYS_CLK_AUDIO_PLL://not bypass property
    case  CSP_CCM_SYS_CLK_VIDEO_PLL0:
    case  CSP_CCM_SYS_CLK_VIDEO_PLL1:
    case  CSP_CCM_SYS_CLK_AUDIO_PLL_4X:
    case  CSP_CCM_SYS_CLK_VIDEO_PLL0_2X:
    case  CSP_CCM_SYS_CLK_VIDEO_PLL1_2X:
    default:
        break;
    }

    return AW_CCMU_FAIL;
}

//set factor whose pll has "factor" parameter
static s32
_pll_set_factor( CSP_CCM_sysClkNo_t clkNo, u8 factor )
{
    switch (clkNo)
    {
    case CSP_CCM_SYS_CLK_CORE_PLL:
        CORE_PLL_SET_FACTOR(factor);
        break;
    case CSP_CCM_SYS_CLK_VE_PLL:
        VE_PLL_SET_FACTOR(factor);
        break;

    case CSP_CCM_SYS_CLK_SDRAM_PLL:
        SET_BITS(CCM_SDRAM_PLL_R, 0, 6, factor);
        break;

    case CSP_CCM_SYS_CLK_VIDEO_PLL0:
    case CSP_CCM_SYS_CLK_VIDEO_PLL0_2X:
        SET_BITS(CCM_VIDEO_PLL_R, 8, 7, factor);
        break;
    case CSP_CCM_SYS_CLK_VIDEO_PLL1:
    case CSP_CCM_SYS_CLK_VIDEO_PLL1_2X:
        SET_BITS(CCM_VIDEO_PLL_R, 24, 7, factor);
        break;

    case CSP_CCM_SYS_CLK_AUDIO_PLL://not property factor
    case CSP_CCM_SYS_CLK_AUDIO_PLL_4X:
    default:
        return AW_CCMU_FAIL;
    }

    return AW_CCMU_OK;
}

//get factor whose pll has "factor" parameter
static u8
_pll_get_factor( CSP_CCM_sysClkNo_t clkNo)
{
    switch (clkNo)
    {
    case CSP_CCM_SYS_CLK_CORE_PLL:
        return CORE_PLL_GET_FACTOR;

    case CSP_CCM_SYS_CLK_VE_PLL:
        return VE_PLL_GET_FACTOR;

    case CSP_CCM_SYS_CLK_SDRAM_PLL:
        return GET_BITS_VAL(CCM_SDRAM_PLL_R, 0, 6);

    case CSP_CCM_SYS_CLK_VIDEO_PLL0:
    case CSP_CCM_SYS_CLK_VIDEO_PLL0_2X://///////////////////////////////
        return GET_BITS_VAL(CCM_VIDEO_PLL_R, 8, 7);

    case CSP_CCM_SYS_CLK_VIDEO_PLL1:
    case CSP_CCM_SYS_CLK_VIDEO_PLL1_2X://///////////////////////////////
        return GET_BITS_VAL(CCM_VIDEO_PLL_R, 24, 7);

    default:
        return 1;//
    }
}

//set the parameters of the pll:
//notes: pll_nx is got rid of in _pll_set_freq
CSP_CCM_err_t _pll_conifg( const CcmPllPara_t* pll )
{
    CSP_CCM_sysClkNo_t clkNo = CSP_CCM_SYS_CLK_TOTAL_NUM;
    u8 factor = 0;

    CCM_CHECK_NULL_PARA(pll, CSP_CCM_ERR_PARA_NULL);
    clkNo = pll->pllId;
    if (!(CSP_CCM_SYS_CLK_CORE_PLL <= clkNo && CSP_CCM_SYS_CLK_VIDEO_PLL1 >= clkNo)){
        CCM_msg("exception: clk number [%d] invalid!\n", clkNo);
        return CSP_CCM_ERR_CLK_NO_INVALID;
    }
    if (CSP_CCM_SYS_CLK_AUDIO_PLL == clkNo)
    {
        pll->para.freqHigh ? AUDIO_PLL_FREQ_SEL_HIGH : AUDIO_PLL_FREQ_SEL_LOW;
        return CSP_CCM_ERR_NONE;/////////////////////////////////
    }

    if (clkNo <= CSP_CCM_SYS_CLK_SDRAM_PLL){//bypass sensitive pll
        _pll_set_bypass(clkNo, pll->para.outPutPara.enableBypass);
    }

    //set factor
    factor = pll->para.outPutPara.factor;
    _pll_set_factor(clkNo, factor);

    return CSP_CCM_ERR_NONE;
}

/*********************************************************************
* Method	 :    		_pll_set_freq
* Description: set freq of PLL except xx_pll_nx
* Parameters :
	@CSP_CCM_sysClkNo_t clkNo
	@const u32 outFreq
* Returns    :   CSP_CCM_err_t
* Note       : in switch, statements divided by "break;" is independent to avoid error!
*********************************************************************/
CSP_CCM_err_t _pll_set_freq(CSP_CCM_sysClkNo_t clkNo, const u32 outFreq)
{
    CcmPllPara_t thePLL;
    s32 byPassAble = 0;
    u8 factor = 0;
    u32 dividend = 0;
    u32 divisor  = 1;
    u32 tmpFreq = outFreq;//for pll_nx

    if (!(CSP_CCM_SYS_CLK_CORE_PLL <= clkNo && clkNo <= CSP_CCM_SYS_CLK_VIDEO_PLL1)){
        CCM_msg("corresponding clkNo %d is not PLL!\n", clkNo);
        return CSP_CCM_ERR_CLK_NO_INVALID;
    }

    thePLL.pllId = clkNo;

    switch (clkNo)
    {
    case CSP_CCM_SYS_CLK_CORE_PLL:
        if (outFreq <= FREQ_HOSC)//bypass enabled and factor should be 1/2/4/8
        {
            if( !((FREQ_HOSC>>0) == outFreq || (FREQ_HOSC>>1) == outFreq ||
                  (FREQ_HOSC>>2 == outFreq) || (FREQ_HOSC>>3) == outFreq) ){
                    CCM_msg("corePLL freq must be %d/1/2/4/8 when <%d!\n", FREQ_HOSC, FREQ_HOSC);
                    return CSP_CCM_ERR_FREQ_NOT_STANDARD;
            }
            byPassAble = 1;
            factor = (FREQ_HOSC)/outFreq;
            break;/////////////////////////////////
        }//go to case CSP_CCM_SYS_CLK_VE_PLL
    case CSP_CCM_SYS_CLK_VE_PLL:
        if (CSP_CCM_SYS_CLK_VE_PLL == clkNo)
        {
            if (outFreq < FREQ_HOSC){
                CCM_msg("VE PLL freq must >=%d", FREQ_HOSC);
                return CSP_CCM_ERR_FREQ_NOT_STANDARD;
            }

            if (FREQ_HOSC == outFreq){
                byPassAble = 1;
                break;/////////////////////////////////
            }
        }

        /************************************************************************/
        /* Following are features of both CORE PLL and VE PLL have*/
        /************************************************************************/


        //CORE and VE PLL common expression.(bypass disabled)
        byPassAble = 0;
        if (outFreq < 30*MHz){
            CCM_msg("CORE/VE PLL freq range should not be in [24,30]!\n");
            return CSP_CCM_ERR_FREQ_NOT_STANDARD;
        }
        if (outFreq > _CORE_VE_PLL_MAX_FREQ){
            CCM_msg("CORE/VE PLL freq must less than %d!\n", _CORE_VE_PLL_MAX_FREQ);
            return CSP_CCM_ERR_PLL_FREQ_HIGH;
        }
        dividend    = outFreq - 30*MHz;
        divisor     = 6*MHz;
        factor = uIntDiv(divisor, dividend, 7);//factor<=2^7-1
        if ((factor*divisor) != dividend){//must be [6*factor + 30]*MHz
            CCM_msg("CORE/VE PLL freq must== 6M*factor + 30M if >24M!\n");
            return CSP_CCM_ERR_FREQ_NOT_STANDARD;
        }

        thePLL.para.outPutPara.enableBypass = byPassAble;
        thePLL.para.outPutPara.factor       = factor;
        break;//end case

    case CSP_CCM_SYS_CLK_AUDIO_PLL_8X:
        tmpFreq <<= 1;
    case CSP_CCM_SYS_CLK_AUDIO_PLL_4X:
        tmpFreq <<= 2;
    case CSP_CCM_SYS_CLK_AUDIO_PLL:
        if (!(FREQ_AUDIO_PLL_HIGH == tmpFreq || FREQ_AUDIO_PLL_LOW == tmpFreq)){
            CCM_msg("Audio PLL freq must be %d or %d!\n", FREQ_AUDIO_PLL_HIGH, FREQ_AUDIO_PLL_LOW);
            return CSP_CCM_ERR_FREQ_NOT_STANDARD;
        }
        thePLL.para.freqHigh = (FREQ_AUDIO_PLL_LOW == tmpFreq) ? 0 : 1;/////////////////////////////////
        break;//end case

    case CSP_CCM_SYS_CLK_SDRAM_PLL://source from HOSC
        if (FREQ_HOSC == outFreq){//if bypass enabled
            byPassAble = 1;
            break;/////////////////////////////////
        }
        //if bypass disabled, freq=12*factor + (36 + 24)
        //so freq >= 60
        byPassAble = 0;
        if (_SDRAM_PLL_MAX_FREQ < outFreq){//factor< (1<<6)
            CCM_msg("SDRAM PLL: freq must <(60+12*63) for factor is 6 bits!\n");
            return CSP_CCM_ERR_PLL_FREQ_HIGH;
        }
        if (60*MHz > outFreq){
            CCM_msg("SDRAM PLL: freq must >= 60MHz if not 24!\n");
            return CSP_CCM_ERR_FREQ_NOT_STANDARD;
        }
        dividend = outFreq - 60*MHz;
        divisor  = 12*MHz;
        factor = uIntDiv(divisor, dividend, 6);//factor is 6 bits len
        if (dividend != (divisor*factor)){
            CCM_msg("SDRAM PLL: freq must be 12*factor+60 if not 24!\n");
            return CSP_CCM_ERR_FREQ_NOT_STANDARD;
        }

        thePLL.para.outPutPara.enableBypass     = byPassAble;
        thePLL.para.outPutPara.factor           = factor;
        break;//end case

    case CSP_CCM_SYS_CLK_VIDEO_PLL0_2X:
    case CSP_CCM_SYS_CLK_VIDEO_PLL1_2X:
        tmpFreq <<= 1;
    case CSP_CCM_SYS_CLK_VIDEO_PLL0://freq=(9~110)*3M
    case CSP_CCM_SYS_CLK_VIDEO_PLL1:
        if ( (tmpFreq < 27*MHz) || (tmpFreq > 330*MHz) ){
            CCM_msg("Video pll0/1: must freq=(9~110)*3M.!\n");
            return CSP_CCM_ERR_FREQ_NOT_STANDARD;
        }

        divisor     = 3*MHz;
        dividend    = tmpFreq;
        factor      = uIntDiv(divisor, dividend, 7);//factor is 7 bits len
        if (dividend != (factor*divisor)){
            CCM_msg("Video pll0/1: must freq=(9~110)*3M.!\n");
            return CSP_CCM_ERR_FREQ_NOT_STANDARD;
        }

        thePLL.para.outPutPara.factor = factor;
        break;

    default:
        CCM_msg("The clock number[%d] is not PLL!\n", clkNo);
    	break;//end switch
    }

    return _pll_conifg(&thePLL);
}

u32 _pll_get_freq(CSP_CCM_sysClkNo_t sysClkNo)
{
    u32     factor = 0;
    s32  bypassEnable = 0;
    u32     nx = 0;

    bypassEnable = _pll_bypass_is_able(sysClkNo);
    factor       = _pll_get_factor(sysClkNo);

    switch (sysClkNo)
    {
    case CSP_CCM_SYS_CLK_CORE_PLL:
        {
            //if bypass enabled, freq=24M/factor(1,2,4,8), else freq = 6*(factor + 1) + 24
            if (1 == bypassEnable){
                if (1 == factor){
                    return FREQ_HOSC>>0;
                }
                else if (2 == factor){
                    return FREQ_HOSC>>1;
                }
                else if (4 == factor){
                    return FREQ_HOSC>>2;
                }
                else if (8 == factor){
                    return FREQ_HOSC>>3;
                }
                else{
                    CCM_msg("exception! core pll: bypass enabled, but factor not 1/2/4/8!n");
                    return FREQ_0;
                }
            }
            return MHz*6*(factor + 1) + FREQ_HOSC;
        }

    case CSP_CCM_SYS_CLK_VE_PLL:
        //if bypass enabled, freq=24M, else freq = 6*(factor + 1) + 24!!!
        if (1 == bypassEnable){
            return FREQ_HOSC;
        }
        return MHz*6*(factor + 1) + FREQ_HOSC;//if bypass disabled

    case CSP_CCM_SYS_CLK_AUDIO_PLL_8X:
        nx += 1;/////////////////////////////////
    case CSP_CCM_SYS_CLK_AUDIO_PLL_4X:
        nx += 2;/////////////////////////////////
    case CSP_CCM_SYS_CLK_AUDIO_PLL:
        if (AUDIO_PLL_FREQ_IS_HIGH){
            return FREQ_AUDIO_PLL_HIGH<<nx;
        }
        return FREQ_AUDIO_PLL_LOW<<nx;

    case CSP_CCM_SYS_CLK_SDRAM_PLL:
        if (1 == bypassEnable){
            return _get_HOSC_rate();
        }
        return MHz*12*(factor + 3) + _get_HOSC_rate();

    case CSP_CCM_SYS_CLK_VIDEO_PLL0_2X:
    case CSP_CCM_SYS_CLK_VIDEO_PLL1_2X:
        nx = 1;/////////////////////////////////
    case CSP_CCM_SYS_CLK_VIDEO_PLL0:
    case CSP_CCM_SYS_CLK_VIDEO_PLL1:
        if (!(9<= factor && factor <= 110)){
            CCM_msg("Exception! video pll factor range invalid for clock[%d]!\n", sysClkNo);
            return FREQ_0;
        }
        return (MHz*factor*3)<<nx;

    default:
    	return FREQ_0;
    }
}

#endif // CCM_PLL

#define _CCM_SDRAM
#ifdef _CCM_SDRAM
//divideRatio should be 1/2/4, sourced from SDRAM PLL
s32 _sdram_config(u32 divideRatio)
{
    switch (divideRatio)
    {
    case 1:
    case 2:
    case 4:
        SET_BITS(CCM_SDRAM_PLL_R, 12, 2, divideRatio - 1);
        return AW_CCMU_OK;

    default:
        CCM_msg("Error:divide ratio for sdram should be 1/2/4!\n");
        return AW_CCMU_FAIL;
    }
}

/*********************************************************************
* Method	 :    		_sdram_set_rate
* Description:
* Parameters :
	@const u32 outFreq
* Returns    :   CSP_CCM_err_t
* Note       : rate of sdram must be SDramPll/(1/2/4)
*********************************************************************/
CSP_CCM_err_t _sdram_set_rate(const u32 outFreq)
{
    u32 srcRate     = 0;
    u8 divideRatio   = 0;

    srcRate = _pll_get_freq(CSP_CCM_SYS_CLK_SDRAM_PLL);
    CCM_inf("sdram: source rate is %d!\n", srcRate);

    if ((srcRate>>0) == outFreq){
        divideRatio = 1;
    }
    else if ((srcRate>>1) == outFreq){
        divideRatio = 2;
    }
    else if ((srcRate>>2) == outFreq){
        divideRatio = 4;
    }
    else{
        CCM_msg("SDRAM: freq must be SDramPll/(1/2/4) !\n");
        return CSP_CCM_ERR_FREQ_NOT_STANDARD;
    }
    _sdram_config(divideRatio);

    return CSP_CCM_ERR_NONE;
}

u32 _sdram_get_rate( void ) //rate of sdram must be SDramPll/(1/2/4)
{
    u32 divideRatio = 0;
    u32 srcRate = 0;

    divideRatio = GET_BITS_VAL(CCM_SDRAM_PLL_R, 12, 2);
    divideRatio += 1;

    srcRate = _pll_get_freq(CSP_CCM_SYS_CLK_SDRAM_PLL);
    //CCM_inf("sdram src rate is %d, divide ratio is %d!\n", srcRate, divideRatio);

    return srcRate/divideRatio;
}

#endif // CCM_SDRAM

#define _CCM_CPU_AHB_APB

#ifdef _CCM_CPU_AHB_APB
/*********************************************************************
* Method	 :    		_set_cpu_rate
* Description:
* Parameters :
@clkSrc : should be LOSC/HOSC/AUDIO_PLL/CORE_PLL
@u8 divideRatio : should be 1/2/4
* Returns    :   s32
* Note       :
*********************************************************************/
static s32
_cpu_config(CSP_CCM_sysClkNo_t clkSrc, u8 divideRatio)
{
    u8 srcBits = 0;

    switch (clkSrc)
    {
    case CSP_CCM_SYS_CLK_LOSC:
        srcBits = _CPU_CLK_SRC_LOSC;
        break;

    case CSP_CCM_SYS_CLK_HOSC:
        srcBits = _CPU_CLK_SRC_HOSC;
        break;

    case CSP_CCM_SYS_CLK_CORE_PLL:
        srcBits = _CPU_CLK_SRC_CORE_PLL;
        break;

    case CSP_CCM_SYS_CLK_AUDIO_PLL_4X:
        srcBits = _CPU_CLK_SRC_AUDIO_PLL_4X;
        break;

    default:
        return AW_CCMU_FAIL;
    }
    SET_BITS(CCM_AHB_APB_RATIO_CFG_R, 9, 2, srcBits);

    if (!(1U == divideRatio || 2U == divideRatio || 4U == divideRatio )){
        return AW_CCMU_FAIL;
    }
    SET_BITS(CCM_AHB_APB_RATIO_CFG_R, 0, 2, divideRatio - 1);

    return AW_CCMU_OK;
}

CSP_CCM_err_t _cpu_set_rate(const u32 outFreq)
{
    u32 srcRate = 0;
    u8 divideRatio  = 0;//1/2/4/8
    CSP_CCM_sysClkNo_t srcNO = CSP_CCM_SYS_CLK_TOTAL_NUM;
    CSP_CCM_sysClkNo_t sources[4] = {CSP_CCM_SYS_CLK_LOSC, CSP_CCM_SYS_CLK_HOSC,
                                     CSP_CCM_SYS_CLK_CORE_PLL, CSP_CCM_SYS_CLK_AUDIO_PLL_4X};
    int i = 0;
    int k = 0;

    for (i = 0; i < 4; i++)
    {
        srcNO   = sources[i];
        srcRate = CSP_CCM_get_sys_clk_freq(srcNO);/////////////////////////////////
    	for (k = 0; k < 4; k++)
    	{
    		if (srcRate>>k == outFreq){
                divideRatio = 1<<k;
                break;
    		}
    	}
        if (divideRatio > 0){
            break;
        }
    }
    if (0 == divideRatio){
        CCM_msg("cpu rate not standard!\n");
        return CSP_CCM_ERR_FREQ_NOT_STANDARD;
    }
    _cpu_config(srcNO, divideRatio);

    return CSP_CCM_ERR_NONE;
}

static u32
_cpu_get_rate(void)
{
    u32 srcBits = 0;
    u32 srcRate = 0;
    u32 divBits = 0;

    srcBits = GET_BITS_VAL(CCM_AHB_APB_RATIO_CFG_R, 9, 2);

    switch (srcBits)
    {
    case _CPU_CLK_SRC_LOSC:
        srcRate =  _get_LOSC_rate();
        break;

    case _CPU_CLK_SRC_HOSC:
        srcRate =  _get_HOSC_rate();
        break;

    case _CPU_CLK_SRC_CORE_PLL:
        srcRate = _pll_get_freq(CSP_CCM_SYS_CLK_CORE_PLL);
        break;

    case _CPU_CLK_SRC_AUDIO_PLL_4X:
        srcRate = _pll_get_freq(CSP_CCM_SYS_CLK_AUDIO_PLL_4X);
        break;

    default:
        break;
    }

    divBits = GET_BITS_VAL(CCM_AHB_APB_RATIO_CFG_R, 0, 2);
    divBits = (2 == divBits) ? 2U : (divBits + 1);

    return srcRate/divBits;
}

static s32
_aHb_config(u32 divideRatio)
{
    switch (divideRatio)
    {
    case 1:
    case 2:
        AHB_CLK_DIV_RATIO_SET(divideRatio - 1);
        break;

    case 4:
        AHB_CLK_DIV_RATIO_SET(2U);
        break;

    case 8:
        AHB_CLK_DIV_RATIO_SET(3U);
        break;

    default:
        return AW_CCMU_FAIL;
    }

    return AW_CCMU_OK;
}

CSP_CCM_err_t _aHb_set_rate(const u32 outFreq)
{
    u32 srcRate = 0;
    u8 divideRatio = 0;//should be 1/2/4/8

    srcRate = _cpu_get_rate();

    if (srcRate>>0 == outFreq){
        divideRatio = 1;
    }
    else if (srcRate>>1 == outFreq){
        divideRatio = 2;
    }
    else if (srcRate>>2 == outFreq){
        divideRatio = 4;
    }
    else if (srcRate>>3 == outFreq){
        divideRatio = 8;
    }
    else{
        CCM_msg("aHb: The cpu rate is %d, and ahb rate must be cpu/(1/2/4/8)!n", srcRate);
        return CSP_CCM_ERR_FREQ_NOT_STANDARD;
    }
    _aHb_config(divideRatio);

    return CSP_CCM_ERR_NONE;
}

u32 _aHb_get_rate( void )
{
    u32 divideRatio = 0;

    divideRatio = 1<<GET_BITS_VAL(CCM_AHB_APB_RATIO_CFG_R, 3, 2);

    return _cpu_get_rate()/divideRatio;
}

/*********************************************************************
* Method	 :    		_set_apb_rate
* Description:
* Parameters :
@u32 divideRatio : should be 2/4/8
* Returns    :   s32
* Note       :
*********************************************************************/
static s32 _aPb_config(u32 divideRatio)
{
    u8 bitsVal = 0;

    switch(divideRatio){
        case 2:
            bitsVal = 0;
            break;
        case 4:
            bitsVal = 2U;
            break;
        case 8:
            bitsVal = 3U;
            break;
        default:
            return AW_CCMU_FAIL;
    }
    SET_BITS(CCM_AHB_APB_RATIO_CFG_R,6, 2, bitsVal);//////////////////////////

    return AW_CCMU_OK;
}

CSP_CCM_err_t _aPb_set_rate(const u32 outFreq)//ahb/2/4/8
{
    u32 srcRate = 0;
    u8 divideRatio = 0;

    srcRate = _aHb_get_rate();
    if (srcRate>>1 == outFreq){
        divideRatio = 2;
    }
    else if (srcRate>>2 == outFreq){
        divideRatio = 4;
    }
    else if (srcRate>>3 == outFreq){
        divideRatio = 8;
    }
    else{
        CCM_msg("apb: the ahb rate is %d, and must apb=ahb/(2/4/8)!n", srcRate);
        return CSP_CCM_ERR_FREQ_NOT_STANDARD;
    }
    _aPb_config(divideRatio);

    return CSP_CCM_ERR_NONE;
}

u32 _aPb_get_rate( void )
{
    u32 divideRatio = 0;

    divideRatio = APB_CLK_RATIO_GET;
    divideRatio = (!divideRatio) ? 2 : (1<<divideRatio);

    return _aHb_get_rate()/divideRatio;
}

#endif // _CCM_CPU_AHB_APB

#define _CCM_TVENC
#ifdef _CCM_TVENC

s32 _tvenc_config(CSP_CCM_sysClkNo_t clkNo, CSP_CCM_sysClkNo_t srcNo, u8 divideRatio)
{
    u8 srcBitsPos = 0;
    u8 srcBitsVal = 0;

    if (!(CSP_CCM_SYS_CLK_TVENC_1 == clkNo || CSP_CCM_SYS_CLK_TVENC_0 == clkNo)){
        CCM_msg("error clock number!\n");
        return AW_CCMU_FAIL;
    }
//get source
    if (CSP_CCM_SYS_CLK_VIDEO_PLL0 == srcNo){
        srcBitsVal = 0;
    }
    else if (CSP_CCM_SYS_CLK_VIDEO_PLL1 == srcNo){
        srcBitsVal = 1;
    }
    else if (CSP_CCM_SYS_CLK_VIDEO_PLL0_2X == srcNo){
        srcBitsVal = 2;
    }
    else if (CSP_CCM_SYS_CLK_VIDEO_PLL1_2X == srcNo){
        srcBitsVal = 3;
    }
    else{
        CCM_msg("error source clock number for tvenc!\n");
        return AW_CCMU_FAIL;
    }
//check divide ratio
    if (!(divideRatio >= 1 && divideRatio <= 16)){
        CCM_msg("tvenc:divid Ratio should be in [1, 16]!\n");
        return AW_CCMU_FAIL;
    }

    srcBitsPos = (CSP_CCM_SYS_CLK_TVENC_1 == clkNo) ? 12 : 4;
    SET_BITS(CCM_TVENC_CLK_R, srcBitsPos, 2, srcBitsVal);

    srcBitsPos = (CSP_CCM_SYS_CLK_TVENC_1 == clkNo) ? 8 : 0;
    SET_BITS(CCM_TVENC_CLK_R, srcBitsPos, 4, divideRatio - 1);

    return AW_CCMU_OK;
}

CSP_CCM_err_t _tvenc_set_freq(CSP_CCM_sysClkNo_t clkNo, const u32 outFreq)
{
    u32 srcRate = 0;
    u8 divideRatio = 0;//1~16
    CSP_CCM_sysClkNo_t srcNo = CSP_CCM_SYS_CLK_TOTAL_NUM;
    CSP_CCM_sysClkNo_t sources[] = {CSP_CCM_SYS_CLK_VIDEO_PLL0, CSP_CCM_SYS_CLK_VIDEO_PLL1,
                                    CSP_CCM_SYS_CLK_VIDEO_PLL0_2X, CSP_CCM_SYS_CLK_VIDEO_PLL1_2X};
    const unsigned freqErrRange = 5;//some @outFreq cann't not divide exactly by PLL!
    int i = 0;
    int k = 0;

    if (!(CSP_CCM_SYS_CLK_TVENC_1 == clkNo || CSP_CCM_SYS_CLK_TVENC_0 == clkNo)){
        CCM_msg("error clock number!\n");
        return CSP_CCM_ERR_CLK_NUM_NOT_SUPPORTED;
    }

    for (; i < 4; i++)
    {
        srcNo = sources[i];
    	srcRate = _pll_get_freq(srcNo);

        for (k = 1; k < 17; k++)
        {
            if (outFreq * k >= srcRate - freqErrRange && outFreq * k <= srcRate + freqErrRange)
            {
                divideRatio = k;
                break;
        	}
        }

        if (divideRatio > 0){
            break;
        }
    }
    if (0 == divideRatio){
        CCM_msg("TVENC:Erro freq %d!\n", outFreq);
        return CSP_CCM_ERR_FREQ_NOT_STANDARD;
    }

    _tvenc_config(clkNo, srcNo, divideRatio);/////////////////////////////////

    return CSP_CCM_ERR_NONE;
}

u32 _tvenc_get_freq(CSP_CCM_sysClkNo_t clkNo)
{
    CSP_CCM_sysClkNo_t srcNo = CSP_CCM_SYS_CLK_TOTAL_NUM;
    u32 bitsPos      = 0;
    u32 srcBitsVal   = 0;
    u32 divideRatio  = 0;

    if (!(CSP_CCM_SYS_CLK_TVENC_1 == clkNo || CSP_CCM_SYS_CLK_TVENC_0 == clkNo)){
        CCM_msg("error clock number!\n");
        return FREQ_0;
    }
    bitsPos = (CSP_CCM_SYS_CLK_TVENC_0 == clkNo) ? 4 : 12;
    srcBitsVal = GET_BITS_VAL(CCM_TVENC_CLK_R, bitsPos, 2);

    if (!srcBitsVal){
        srcNo = CSP_CCM_SYS_CLK_VIDEO_PLL0;
    }
    else if (1U == srcBitsVal){
        srcNo = CSP_CCM_SYS_CLK_VIDEO_PLL1;
    }
    else if (2U == srcBitsVal){
        srcNo = CSP_CCM_SYS_CLK_VIDEO_PLL0_2X;
    }
    else if (3U == srcBitsVal){
        srcNo = CSP_CCM_SYS_CLK_VIDEO_PLL1_2X;
    }
    else return CSP_CCM_ERR_GET_CLK_FREQ;

    bitsPos     = (CSP_CCM_SYS_CLK_TVENC_0 == clkNo) ? 0 : 8;
    divideRatio = GET_BITS_VAL(CCM_TVENC_CLK_R, bitsPos, 4);
    divideRatio++;

    return _pll_get_freq(srcNo)/divideRatio;
}

#endif // _CCM_TVENC


u32
CSP_CCM_get_sys_clk_freq(CSP_CCM_sysClkNo_t sysClkNo )
{
    if (CSP_CCM_SYS_CLK_CORE_PLL <= sysClkNo && sysClkNo <= CSP_CCM_SYS_CLK_VIDEO_PLL1_2X)
    {
        return _pll_get_freq(sysClkNo);/////////////////////////////////
    }

    switch (sysClkNo)
    {
    case CSP_CCM_SYS_CLK_HOSC:
        return _get_HOSC_rate();
    case CSP_CCM_SYS_CLK_LOSC:
        return _get_LOSC_rate();

    case CSP_CCM_SYS_CLK_CPU:
        return _cpu_get_rate();
    case CSP_CCM_SYS_CLK_AHB:
        return _aHb_get_rate();
    case CSP_CCM_SYS_CLK_APB:
        return _aPb_get_rate();
    case CSP_CCM_SYS_CLK_SDRAM:
        return _sdram_get_rate();

    case CSP_CCM_SYS_CLK_TVENC_0 ://TVE_CLK0
    case CSP_CCM_SYS_CLK_TVENC_1://TVE_CLK1
        return _tvenc_get_freq(sysClkNo);

    default:
        return FREQ_0;
    }
}


CSP_CCM_err_t
CSP_CCM_set_sys_clk_freq(CSP_CCM_sysClkNo_t sysClkNo, const u32 outFreq)
{
    if (CSP_CCM_SYS_CLK_CORE_PLL <= sysClkNo && sysClkNo <= CSP_CCM_SYS_CLK_VIDEO_PLL1_2X)
    {
        return _pll_set_freq(sysClkNo, outFreq);/////////////////////////////////
    }

    switch (sysClkNo)
    {
    case CSP_CCM_SYS_CLK_HOSC:
    case CSP_CCM_SYS_CLK_LOSC:
        return CSP_CCM_ERR_OSC_FREQ_CANNOT_BE_SET;

    case CSP_CCM_SYS_CLK_CPU:
        return _cpu_set_rate(outFreq);

    case CSP_CCM_SYS_CLK_AHB:
        return _aHb_set_rate(outFreq);

    case CSP_CCM_SYS_CLK_APB:
        return _aPb_set_rate(outFreq);

    case CSP_CCM_SYS_CLK_SDRAM:
        return _sdram_set_rate(outFreq);

    case CSP_CCM_SYS_CLK_TVENC_0 ://TVE_CLK0
    case CSP_CCM_SYS_CLK_TVENC_1://TVE_CLK1
        //To use CSP_CCM_set_sys_clk_freq_ex recommended!!
        return _tvenc_set_freq(sysClkNo, outFreq);

    default:
    	return CSP_CCM_ERR_CLK_NUM_NOT_SUPPORTED;
    }
}

/*********************************************************************
* Method	 :    		CSP_CCM_set_sys_clk_freq_ex
* Description: Extended interface to set System clock frequency
* Parameters :
	@CSP_CCM_sysClkNo_t sysClkNo
	@const u32 outFreq
* Returns    :   CSP_CCM_err_t
* Note       :
*********************************************************************/
CSP_CCM_err_t     CSP_CCM_set_sys_clk_freq_ex(CSP_CCM_sysClkNo_t sysClkNo,
                                              CSP_CCM_sysClkNo_t srcClkNo,
                                              const u32 divideRatio)
{
    switch (sysClkNo)
    {
    case CSP_CCM_SYS_CLK_CPU:
        _cpu_config(srcClkNo, divideRatio);
    	break;

    case CSP_CCM_SYS_CLK_AHB:
        _aHb_config(divideRatio);
    	break;

    case CSP_CCM_SYS_CLK_APB:
        _aPb_config(divideRatio);
        break;

    case CSP_CCM_SYS_CLK_SDRAM:
        _sdram_config(divideRatio);
        break;

    case CSP_CCM_SYS_CLK_TVENC_0:
    case CSP_CCM_SYS_CLK_TVENC_1:
        _tvenc_config(sysClkNo, srcClkNo, divideRatio);
        break;

    default:
        return CSP_CCM_ERR_CLK_NUM_NOT_SUPPORTED;
    }

    return CSP_CCM_ERR_NONE;
}


char* g_sysClkName[CSP_CCM_SYS_CLK_TOTAL_NUM] =
{
    "hosc",
    "losc",

    "core_pll",
    "ve_pll",
    "sdram_pll",
    "audio_pll",
    "video_pll0",
    "video_pll1",

    "audio_pll_4x",
    "audio_pll_8x",
    "video_pll0_2x",
    "video_pll1_2x",

    "cpu",
    "ahb",
    "apb",
    "sdram",
    "tvenc_0",
    "tvenc_1",
};


static CSP_CCM_sysClkNo_t
_get_sys_clk_src( CSP_CCM_sysClkNo_t clkNo )
{
    switch (clkNo)
    {
    case  CSP_CCM_SYS_CLK_HOSC:
    case  CSP_CCM_SYS_CLK_LOSC:
    case  CSP_CCM_SYS_CLK_AUDIO_PLL:
    case CSP_CCM_SYS_CLK_AUDIO_PLL_4X:
    case CSP_CCM_SYS_CLK_VIDEO_PLL0:
    case CSP_CCM_SYS_CLK_VIDEO_PLL1:
    case CSP_CCM_SYS_CLK_VIDEO_PLL0_2X:
    case CSP_CCM_SYS_CLK_VIDEO_PLL1_2X:
        return CSP_CCM_SYS_CLK_TOTAL_NUM;

    case  CSP_CCM_SYS_CLK_CORE_PLL:
    case  CSP_CCM_SYS_CLK_VE_PLL:
    case  CSP_CCM_SYS_CLK_SDRAM_PLL:
        return CSP_CCM_SYS_CLK_HOSC;

    case CSP_CCM_SYS_CLK_CPU:
        {
            u32 srcBits = 0;
            srcBits = GET_BITS_VAL(CCM_AHB_APB_RATIO_CFG_R, 9, 2);

            switch (srcBits)
            {
            case _CPU_CLK_SRC_LOSC:
                return CSP_CCM_SYS_CLK_LOSC;
            case _CPU_CLK_SRC_HOSC:
                return CSP_CCM_SYS_CLK_HOSC;
            case _CPU_CLK_SRC_CORE_PLL:
                return CSP_CCM_SYS_CLK_CORE_PLL;
            case _CPU_CLK_SRC_AUDIO_PLL_4X:
                return CSP_CCM_SYS_CLK_AUDIO_PLL_4X;
            default:
                return CSP_CCM_SYS_CLK_TOTAL_NUM;
            }
        }
    case CSP_CCM_SYS_CLK_AHB:
        return CSP_CCM_SYS_CLK_CPU;
    case CSP_CCM_SYS_CLK_APB:
        return CSP_CCM_SYS_CLK_AHB;
    case CSP_CCM_SYS_CLK_SDRAM:
        return CSP_CCM_SYS_CLK_SDRAM_PLL;
    case CSP_CCM_SYS_CLK_TVENC_0:
    {   u32 srcBits = 0;
        srcBits = GET_BITS_VAL(CCM_TVENC_CLK_R, 4, 2);
        if (0 == srcBits){
            return CSP_CCM_SYS_CLK_VIDEO_PLL0;
        }
        if (1U == srcBits){
            return CSP_CCM_SYS_CLK_VIDEO_PLL1;
        }
        if (2U == srcBits){
            return CSP_CCM_SYS_CLK_VIDEO_PLL0_2X;
        }
        if (3U == srcBits){
            return CSP_CCM_SYS_CLK_VIDEO_PLL1_2X;
        }
    	break;
    }
    case CSP_CCM_SYS_CLK_TVENC_1:
    {   u32 srcBits = 0;
        srcBits = GET_BITS_VAL(CCM_TVENC_CLK_R, 12, 2);
        if (0 == srcBits){
            return CSP_CCM_SYS_CLK_VIDEO_PLL0;
        }
        if (1U == srcBits){
            return CSP_CCM_SYS_CLK_VIDEO_PLL1;
        }
        if (2U == srcBits){
            return CSP_CCM_SYS_CLK_VIDEO_PLL0_2X;
        }
        if (3U == srcBits){
            return CSP_CCM_SYS_CLK_VIDEO_PLL1_2X;
        }
    	break;
    }
    default:
        return CSP_CCM_SYS_CLK_TOTAL_NUM;
    }

    return CSP_CCM_SYS_CLK_TOTAL_NUM;
}


CSP_CCM_err_t CSP_CCM_get_sys_clk_info( CSP_CCM_sysClkNo_t clkNo, CSP_CCM_sysClkInfo_t* pInfo )
{
    CCM_CHECK_NULL_PARA(pInfo, CSP_CCM_ERR_NULL_PARA);
    if (clkNo >= CSP_CCM_SYS_CLK_TOTAL_NUM){
        CCM_msg("Wrong clk id !\n");
    }
    pInfo->clkId    = clkNo;
    pInfo->pName    = g_sysClkName[clkNo];
    pInfo->freq     = CSP_CCM_get_sys_clk_freq(clkNo);
    pInfo->srcClkId = _get_sys_clk_src(clkNo);

    return CSP_CCM_ERR_NONE;
}



