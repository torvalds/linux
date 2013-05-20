/* mt6626_fm_eint.c
 *
 * (C) Copyright 2009
 * MediaTek <www.MediaTek.com>
 * Hongcheng <hongcheng.xia@MediaTek.com>
 *
 * mt6626 FM Radio Driver -- EINT functions
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "fm_typedef.h"
#include "fm_dbg.h"
#include "fm_err.h"
#include "fm_eint.h"

#ifdef MT6516
#include <mach/mt6516_eint.h>
#include <mach/mt6516_gpio.h>
#endif
#ifdef MT6573
#include <mach/mt6573_eint.h>
#include <mach/mt6573_gpio.h>
#endif
#ifdef MT6575
#include <mach/eint.h>
#include <mach/mt6575_gpio.h>
#endif

#include <cust_eint.h>
#include <cust_gpio_usage.h>

struct fm_eint_interface {
    void (*mask)(fm_u32 eint_num);
    void (*unmask)(fm_u32 eint_num);
    void (*set_hw_debounce)(fm_u32 eint_num, fm_u32 ms);
    void (*set_polarity)(fm_u32 eint_num, fm_u32 pol);
    fm_u32(*set_sens)(fm_u32 eint_num, fm_u32 sens);
    void (*registration)(fm_u32 eint_num, fm_u32 is_deb_en, fm_u32 pol, void (EINT_FUNC_PTR)(void), fm_u32 is_auto_umask);
    fm_s32(*init)(void);
};


#ifdef MT6516
extern void MT6516_EINTIRQUnmask(fm_u32 line);
extern void MT6516_EINTIRQMask(fm_u32 line);
extern void MT6516_EINT_Set_HW_Debounce(fm_u8 eintno, fm_u32 ms);
extern fm_u32 MT6516_EINT_Set_Sensitivity(fm_u8 eintno, kal_bool sens);
extern void MT6516_EINT_Registration(fm_u8 eintno, kal_bool Dbounce_En,
                                     kal_bool ACT_Polarity, void (EINT_FUNC_PTR)(void),
                                     kal_bool auto_umask);
#endif

static struct fm_eint_interface fm_eint_ops = {
#ifdef MT6516
    .mask = MT6516_EINTIRQMask,
    .unmask = MT6516_EINTIRQUnmask,
    .set_hw_debounce = MT6516_EINT_Set_HW_Debounce,
    .set_polarity = NULL,
    .set_sens = MT6516_EINT_Set_Sensitivity,
    .registration = MT6516_EINT_Registration,
    .init = NULL,
#else
    .mask = mt65xx_eint_mask,
    .unmask = mt65xx_eint_unmask,
    .set_hw_debounce = mt65xx_eint_set_hw_debounce,
    .set_polarity = mt65xx_eint_set_polarity,
    .set_sens = mt65xx_eint_set_sens,
    .registration = mt65xx_eint_registration,
    .init = mt65xx_eint_init,
#endif
};
fm_s32 fm_enable_eint(void)
{
    WCN_DBG(FM_INF | EINT, "%s\n", __func__);
    fm_eint_ops.unmask(CUST_EINT_FM_RDS_NUM);
    return 0;
}

fm_s32 fm_disable_eint(void)
{
    WCN_DBG(FM_INF | EINT, "%s\n", __func__);
    fm_eint_ops.mask(CUST_EINT_FM_RDS_NUM);
    return 0;
}

fm_s32 fm_request_eint(void (*parser)(void))
{
    WCN_DBG(FM_NTC | EINT, "%s\n", __func__);
    fm_eint_ops.set_sens(CUST_EINT_FM_RDS_NUM, CUST_EINT_FM_RDS_SENSITIVE);
    fm_eint_ops.set_hw_debounce(CUST_EINT_FM_RDS_NUM, CUST_EINT_FM_RDS_DEBOUNCE_CN);
    fm_eint_ops.registration(CUST_EINT_FM_RDS_NUM,
                             CUST_EINT_FM_RDS_DEBOUNCE_EN,
                             CUST_EINT_FM_RDS_POLARITY,
                             parser,
                             0);
    fm_eint_ops.mask(CUST_EINT_FM_RDS_NUM);
    return 0;
}

fm_s32 fm_eint_pin_cfg(fm_s32 mode)
{
    int ret = 0;

    WCN_DBG(FM_NTC | EINT, "%s\n", __func__);

    switch (mode) {
    case FM_EINT_PIN_EINT_MODE:
        mt_set_gpio_mode(GPIO_FM_RDS_PIN, GPIO_FM_RDS_PIN_M_GPIO);
        mt_set_gpio_pull_enable(GPIO_FM_RDS_PIN, GPIO_PULL_ENABLE);
        mt_set_gpio_pull_select(GPIO_FM_RDS_PIN, GPIO_PULL_UP);
        mt_set_gpio_mode(GPIO_FM_RDS_PIN, GPIO_FM_RDS_PIN_M_EINT);
        break;
    case FM_EINT_PIN_GPIO_MODE:
        mt_set_gpio_mode(GPIO_FM_RDS_PIN, GPIO_FM_RDS_PIN_M_GPIO);
        mt_set_gpio_dir(GPIO_FM_RDS_PIN, GPIO_DIR_IN);
        break;
    default:
        ret = -1;
        break;
    }

    return ret;
}
