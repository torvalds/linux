/*
 * TVIN Signal State Machine
 *
 * Author: Lin Xu <lin.xu@amlogic.com>
 *
 * Copyright (C) 2010 Amlogic Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Standard Linux Headers */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/moduleparam.h>

/* Amlogic Headers */
#include <linux/amlogic/tvin/tvin.h>

/* Local Headers */
#include "../tvin_frontend.h"
#include "../tvin_format_table.h"
#include "vdin_sm.h"
#include "vdin_ctl.h"
#include "vdin_drv.h"


/* Stay in TVIN_SIG_STATE_NOSIG for some cycles => be sure TVIN_SIG_STATE_NOSIG */
#define NOSIG_MAX_CNT               8
/* Stay in TVIN_SIG_STATE_UNSTABLE for some cycles => be sure TVIN_SIG_STATE_UNSTABLE */
#define UNSTABLE_MAX_CNT            2// 4
/* Have signal for some cycles  => exit TVIN_SIG_STATE_NOSIG */
#define EXIT_NOSIG_MAX_CNT          2// 1
/* No signal for some cycles  => back to TVAFE_STATE_NOSIG */
#define BACK_NOSIG_MAX_CNT          24 //8
/* Signal unstable for some cycles => exit TVAFE_STATE_STABLE */
#define EXIT_STABLE_MAX_CNT         1
/* Signal stable for some cycles  => back to TVAFE_STATE_STABLE */
#define BACK_STABLE_MAX_CNT         50  //must >=500ms,for new api function

#define EXIT_PRESTABLE_MAX_CNT      50

static struct tvin_sm_s sm_dev[VDIN_MAX_DEVS];

#if 0
static enum tvin_sm_status_e state = TVIN_SM_STATUS_NULL;    //TVIN_SIG_STATUS_NOSIG;

static unsigned int state_counter          = 0; // STATE_NOSIG, STATE_UNSTABLE
static unsigned int exit_nosig_counter     = 0; // STATE_NOSIG
static unsigned int back_nosig_counter     = 0; // STATE_UNSTABLE
static unsigned int back_stable_counter    = 0; // STATE_UNSTABLE
static unsigned int exit_prestable_counter = 0; // STATE_PRESTABLE
#endif
static bool sm_debug_enable = true;

static int sm_print_nosig  = 0;
static int sm_print_notsup = 0;
static int sm_print_unstable = 0;
static int sm_print_fmt_nosig = 0;
static int sm_print_fmt_chg = 0;

module_param(sm_debug_enable, bool, 0664);
MODULE_PARM_DESC(sm_debug_enable, "enable/disable state machine debug message");
#if 1
static int back_nosig_max_cnt = BACK_NOSIG_MAX_CNT;
module_param(back_nosig_max_cnt, int, 0664);
MODULE_PARM_DESC(back_nosig_max_cnt, "unstable enter nosignal state max count");

static int atv_unstable_in_cnt = 45;
module_param(atv_unstable_in_cnt, int, 0664);
MODULE_PARM_DESC(atv_unstable_in_cnt, "atv_unstable_in_cnt");

static int atv_unstable_out_cnt = 50;
module_param(atv_unstable_out_cnt, int, 0664);
MODULE_PARM_DESC(atv_unstable_out_cnt, "atv_unstable_out_cnt");
static int hdmi_unstable_out_cnt = 25;
module_param(hdmi_unstable_out_cnt, int, 0664);
MODULE_PARM_DESC(hdmi_unstable_out_cnt, "hdmi_unstable_out_cnt");

static int atv_stable_out_cnt = 10;
module_param(atv_stable_out_cnt, int, 0664);
MODULE_PARM_DESC(atv_stable_out_cnt, "atv_stable_out_cnt");

static int other_stable_out_cnt = EXIT_STABLE_MAX_CNT;
module_param(other_stable_out_cnt, int, 0664);
MODULE_PARM_DESC(other_stable_out_cnt, "other_stable_out_cnt");

static int other_unstable_out_cnt = BACK_STABLE_MAX_CNT;
module_param(other_unstable_out_cnt, int, 0664);
MODULE_PARM_DESC(other_unstable_out_cnt, "other_unstable_out_cnt");

static int other_unstable_in_cnt = UNSTABLE_MAX_CNT;
module_param(other_unstable_in_cnt, int, 0664);
MODULE_PARM_DESC(other_unstable_in_cnt, "other_unstable_in_cnt");

static int comp_pre2_stable_cnt = EXIT_PRESTABLE_MAX_CNT;
module_param(comp_pre2_stable_cnt, int, 0664);
MODULE_PARM_DESC(comp_pre2_stable_cnt, "comp_pre2_stable_cnt");

static int nosig_in_cnt = NOSIG_MAX_CNT;
module_param(nosig_in_cnt, int, 0664);
MODULE_PARM_DESC(nosig_in_cnt, "nosig_in_cnt");

static int nosig2_unstable_cnt = EXIT_NOSIG_MAX_CNT;
module_param(nosig2_unstable_cnt, int, 0664);
MODULE_PARM_DESC(nosig2_unstable_cnt, "nosig2_unstable_cnt");

/*
   void tvin_smr_init_counter(void)
   {
   state_counter          = 0;
   exit_nosig_counter     = 0;
   back_nosig_counter     = 0;
   back_stable_counter    = 0;
   exit_prestable_counter = 0;
   }
 */
#endif
/*
 * check hdmirx color format
 */
static void hdmirx_color_fmt_handler(struct vdin_dev_s *devp)
{
    struct tvin_state_machine_ops_s *sm_ops;
    enum tvin_port_e port = TVIN_PORT_NULL;

    if (!devp || !devp->frontend) {
            sm_dev[devp->index].state = TVIN_SM_STATUS_NULL;
            return;
    }

    sm_ops = devp->frontend->sm_ops;
    port = devp->parm.port;

    if ((port < TVIN_PORT_HDMI0) || (port > TVIN_PORT_HDMI7))
        return;

    if (devp->flags & VDIN_FLAG_DEC_STARTED) {
        if (sm_ops->get_sig_propery) {
            sm_ops->get_sig_propery(devp->frontend, &devp->prop);
            if ((devp->prop.color_format != devp->pre_prop.color_format)) {
                pr_info("[smr.%d] : config hdmi color fmt(%d->%d)\n",
                        devp->index, devp->pre_prop.color_format, devp->prop.color_format);
                devp->pre_prop.color_format = devp->prop.color_format;
                vdin_get_format_convert(devp);
                vdin_set_matrix(devp);
            }
        }
    }
}

void tvin_smr_init_counter(int index)
{
        sm_dev[index].state_counter          = 0;
        sm_dev[index].exit_nosig_counter     = 0;
        sm_dev[index].back_nosig_counter     = 0;
        sm_dev[index].back_stable_counter    = 0;
        sm_dev[index].exit_prestable_counter = 0;
}

/*
 * tvin state machine routine
 *
 */
#ifdef CONFIG_ADC_CAL_SIGNALED
void tvin_smr(struct vdin_dev_s *devp)
{
        struct tvin_state_machine_ops_s *sm_ops;
        struct tvin_info_s *info;
        enum tvin_port_e port = TVIN_PORT_NULL;
        unsigned int unstable_in_cnt;
        struct tvin_sm_s *sm_p;
        if (!devp || !devp->frontend)
        {
                sm_dev[devp->index].state = TVIN_SM_STATUS_NULL;
                return;
        }

        sm_ops = devp->frontend->sm_ops;
        info = &devp->parm.info;
        port = devp->parm.port;
        sm_p = &sm_dev[devp->index];

                        if (devp->parm.flag & TVIN_PARM_FLAG_CAL)
                        {
                                if ((((port >= TVIN_PORT_COMP0) && (port <= TVIN_PORT_COMP7)) ||
                                                        ((port >= TVIN_PORT_VGA0 ) && (port <= TVIN_PORT_VGA7 ))
                                    ) &&
                                                (sm_ops->adc_cal)
                                   )
                                {
                                        if (!sm_ops->adc_cal(devp->frontend))
                                                devp->parm.flag &= ~TVIN_PARM_FLAG_CAL;
                                }
                                else
                                        devp->parm.flag &= ~TVIN_PARM_FLAG_CAL;
                        }
        switch (sm_p->state)
        {
                case TVIN_SM_STATUS_NOSIG:
                        ++sm_p->state_counter;
                        if (sm_ops->nosig(devp->frontend))
                        {
                                sm_p->exit_nosig_counter = 0;
                                if (sm_p->state_counter >= nosig_in_cnt)
                                {
                                        sm_p->state_counter       = nosig_in_cnt;
                                        info->status        = TVIN_SIG_STATUS_NOSIG;
                                        info->fmt           = TVIN_SIG_FMT_NULL;
                                        if (sm_debug_enable && !sm_print_nosig) {
                                                pr_info("[smr.%d] no signal\n",devp->index);
                                                sm_print_nosig = 1;
                                        }
                                        sm_print_unstable = 0;
                                }
                        }
                        else
                        {
                                ++sm_p->exit_nosig_counter;
                                if (sm_p->exit_nosig_counter >= nosig2_unstable_cnt)
                                {
                                        tvin_smr_init_counter(devp->index);
                                        sm_p->state = TVIN_SM_STATUS_UNSTABLE;
                                        if (sm_debug_enable)
                                                pr_info("[smr.%d] no signal --> unstable\n",devp->index);
                                        sm_print_nosig  = 0;
                                        sm_print_unstable = 0;
                                }
                        }
                        break;

                case TVIN_SM_STATUS_UNSTABLE:
                        ++sm_p->state_counter;
                        if (sm_ops->nosig(devp->frontend))
                        {
                                sm_p->back_stable_counter = 0;
                                ++sm_p->back_nosig_counter;
                                if (sm_p->back_nosig_counter >= sm_p->back_nosig_max_cnt)
                                {
                                        tvin_smr_init_counter(devp->index);
                                        sm_p->state = TVIN_SM_STATUS_NOSIG;
                                        info->status = TVIN_SIG_STATUS_NOSIG;
                                        info->fmt    = TVIN_SIG_FMT_NULL;
                                        if (sm_debug_enable)
                                                pr_info("[smr.%d] unstable --> no signal\n",devp->index);
                                        sm_print_nosig  = 0;
                                        sm_print_unstable = 0;
                                }
                        }
                        else
                        {
                                sm_p->back_nosig_counter = 0;
                                if (sm_ops->fmt_changed(devp->frontend) )

                                {
                                        sm_p->back_stable_counter = 0;
                                        if((port == TVIN_PORT_CVBS0)&&devp->unstable_flag)
                                                unstable_in_cnt = sm_p->atv_unstable_in_cnt;//UNSTABLE_ATV_MAX_CNT;
                                        else
                                                unstable_in_cnt = other_unstable_in_cnt;
                                        if (sm_p->state_counter >= unstable_in_cnt)
                                        {
                                                sm_p->state_counter  = unstable_in_cnt;
                                                info->status   = TVIN_SIG_STATUS_UNSTABLE;
                                                info->fmt      = TVIN_SIG_FMT_NULL;
                                                if (sm_debug_enable && !sm_print_unstable) {
                                                        pr_info("[smr.%d] unstable\n",devp->index);
                                                        sm_print_unstable = 1;
                                                }
                                                sm_print_nosig  = 0;
                                        }
                                }
                                else
                                {
                                        ++sm_p->back_stable_counter;
                                        if(port == TVIN_PORT_CVBS0)
                                                unstable_in_cnt = sm_p->atv_unstable_out_cnt;//UNSTABLE_ATV_MAX_CNT;
                                        else if ((port >= TVIN_PORT_HDMI0) && (port <= TVIN_PORT_HDMI7 ))
                                                unstable_in_cnt = sm_p->hdmi_unstable_out_cnt;
                                        else
                                                unstable_in_cnt = other_unstable_out_cnt;
                                        if (sm_p->back_stable_counter >= unstable_in_cnt)
                                        {   //must wait enough time for cvd signal lock
                                                sm_p->back_stable_counter    = 0;
                                                sm_p->state_counter               = 0;
                                                if (sm_ops->get_fmt) {
                                                        info->fmt   = sm_ops->get_fmt(devp->frontend);
                                                        if (sm_ops->get_sig_propery)
                                                        {
                                                                sm_ops->get_sig_propery(devp->frontend, &devp->prop);
                                                                devp->parm.info.trans_fmt = devp->prop.trans_fmt;
                                                                devp->parm.info.reserved = devp->prop.dvi_info;
								devp->pre_prop.color_format = devp->prop.color_format;
                                                        }
                                                }
                                                else
                                                        info->fmt   = TVIN_SIG_FMT_NULL;

                                                /* set signal status */
                                                if(info->fmt == TVIN_SIG_FMT_NULL)
                                                {
                                                        info->status = TVIN_SIG_STATUS_NOTSUP;
                                                        if (sm_debug_enable && !sm_print_notsup) {
                                                                pr_info("[smr.%d] unstable --> not support\n",devp->index);
                                                                sm_print_notsup = 1;
                                                        }
                                                }
                                                else
                                                {
                                                        if (sm_ops->fmt_config)
                                                                sm_ops->fmt_config(devp->frontend);
                                                        tvin_smr_init_counter(devp->index);
                                                        sm_p->state = TVIN_SM_STATUS_PRESTABLE;
                                                        if (sm_debug_enable)
                                                                pr_info("[smr.%d] unstable --> prestable, and format is %d(%s)\n",
                                                                                devp->index,info->fmt, tvin_sig_fmt_str(info->fmt));
                                                        sm_print_nosig  = 0;
                                                        sm_print_unstable = 0;
                                                        sm_print_fmt_nosig = 0;
                                                        sm_print_fmt_chg = 0;
                                                }
                                        }
                                }
                        }
                        break;

                case TVIN_SM_STATUS_PRESTABLE:
                        {
                                bool nosig = false, fmt_changed = false;//, pll_lock = false;
                                devp->unstable_flag = true;

                                if (sm_ops->nosig(devp->frontend)) {
                                        nosig = true;
                                        if (sm_debug_enable)
                                                pr_info("[smr.%d] warning: no signal\n",devp->index);
                                }

                                if (sm_ops->fmt_changed(devp->frontend)) {
                                        fmt_changed = true;
                                        if (sm_debug_enable)
                                                pr_info("[smr.%d] warning: format changed\n",devp->index);
                                }

                                if (nosig || fmt_changed)
                                {
                                        ++sm_p->state_counter;
                                        if (sm_p->state_counter >= other_stable_out_cnt)
                                        {
                                                tvin_smr_init_counter(devp->index);
                                                sm_p->state = TVIN_SM_STATUS_UNSTABLE;
                                                if (sm_debug_enable)
                                                        pr_info("[smr.%d] prestable --> unstable\n",devp->index);
                                                sm_print_nosig  = 0;
                                                sm_print_notsup = 0;
                                                sm_print_unstable = 0;

                                                break;
                                        }
                                }
                                else
                                {
                                        sm_p->state_counter = 0;
                                }

                                /* wait comp stable */
                                if ((port >= TVIN_PORT_COMP0) &&
                                                (port <= TVIN_PORT_COMP7))
                                {
                                        ++sm_p->exit_prestable_counter;
                                        if (sm_p->exit_prestable_counter >= comp_pre2_stable_cnt)
                                        {
                                                tvin_smr_init_counter(devp->index);
                                                sm_p->state       = TVIN_SM_STATUS_STABLE;
                                                info->status        = TVIN_SIG_STATUS_STABLE;
                                                if (sm_debug_enable)
                                                        pr_info("[smr.%d] prestable --> stable\n",devp->index);
                                                sm_print_nosig  = 0;
                                                sm_print_notsup = 0;
                                        }
                                }
                                else
                                {
                                        sm_p->state       = TVIN_SM_STATUS_STABLE;
                                        info->status        = TVIN_SIG_STATUS_STABLE;
                                        if (sm_debug_enable)
                                                pr_info("[smr.%d] prestable --> stable\n",devp->index);
                                        sm_print_nosig  = 0;
                                        sm_print_notsup = 0;
                                }
                                break;
                        }
                case TVIN_SM_STATUS_STABLE:
                        {
                                bool nosig = false, fmt_changed = false;//, pll_lock = false;
                                unsigned int stable_out_cnt = 0;

                                devp->unstable_flag = true;
                                if (sm_ops->nosig(devp->frontend)) {
                                        nosig = true;
                                        if (sm_debug_enable && !sm_print_fmt_nosig)
                                        {
                                                pr_info("[smr.%d] warning: no signal\n",devp->index);
                                                sm_print_fmt_nosig = 1;
                                        }
                                }

                                if (sm_ops->fmt_changed(devp->frontend)) {
                                        fmt_changed = true;
                                        if (sm_debug_enable && !sm_print_fmt_chg)
                                        {
                                                pr_info("[smr.%d] warning: format changed\n",devp->index);
                                                sm_print_fmt_chg = 1;
                                        }
                                }
                            hdmirx_color_fmt_handler(devp);
#if 0
                                if (sm_ops->pll_lock(devp->frontend)) {
                                        pll_lock = true;
                                }
                                else {
                                        pll_lock = false;
                                        if (sm_debug_enable)
                                                pr_info("[smr] warning: pll lock failed\n");
                                }
#endif

                                if (nosig || fmt_changed /* || !pll_lock */)
                                {
                                        ++sm_p->state_counter;
                                        if (port == TVIN_PORT_CVBS0)
                                                stable_out_cnt = sm_p->atv_stable_out_cnt;
                                        else
                                                stable_out_cnt = other_stable_out_cnt;
                                        if (sm_p->state_counter >= stable_out_cnt)
                                        {
                                                tvin_smr_init_counter(devp->index);
                                                sm_p->state = TVIN_SM_STATUS_UNSTABLE;
                                                if (sm_debug_enable)
                                                        pr_info("[smr.%d] stable --> unstable\n",devp->index);
                                                sm_print_nosig  = 0;
                                                sm_print_notsup = 0;
                                                sm_print_unstable = 0;
                                                sm_print_fmt_nosig = 0;
                                                sm_print_fmt_chg = 0;
                                        }
                                }
                                else
                                {
                                        sm_p->state_counter = 0;
                                }
                                break;
                        }
                case TVIN_SM_STATUS_NULL:
                default:
                        sm_p->state = TVIN_SM_STATUS_NOSIG;
                        break;
        }
}
#else
void tvin_smr(struct vdin_dev_s *devp)
{
        struct tvin_state_machine_ops_s *sm_ops;
        struct tvin_info_s *info;
        enum tvin_port_e port = TVIN_PORT_NULL;
        unsigned int unstable_in_cnt;
        struct tvin_sm_s *sm_p;
        if (!devp || !devp->frontend)
        {
                sm_dev[devp->index].state = TVIN_SM_STATUS_NULL;
                return;
        }

        sm_ops = devp->frontend->sm_ops;
        info = &devp->parm.info;
        port = devp->parm.port;
        sm_p = &sm_dev[devp->index];

        switch (sm_p->state)
        {
                case TVIN_SM_STATUS_NOSIG:
                        ++sm_p->state_counter;
                        if (devp->parm.flag & TVIN_PARM_FLAG_CAL)
                        {
                                if ((((port >= TVIN_PORT_COMP0) && (port <= TVIN_PORT_COMP7)) ||
                                                        ((port >= TVIN_PORT_VGA0 ) && (port <= TVIN_PORT_VGA7 ))
                                    ) &&
                                                (sm_ops->adc_cal)
                                   )
                                {
                                        if (!sm_ops->adc_cal(devp->frontend))
                                                devp->parm.flag &= ~TVIN_PARM_FLAG_CAL;
                                }
                                else
                                        devp->parm.flag &= ~TVIN_PARM_FLAG_CAL;
                        }
                        else if (sm_ops->nosig(devp->frontend))
                        {
                                sm_p->exit_nosig_counter = 0;
                                if (sm_p->state_counter >= nosig_in_cnt)
                                {
                                        sm_p->state_counter       = nosig_in_cnt;
                                        info->status        = TVIN_SIG_STATUS_NOSIG;
                                        info->fmt           = TVIN_SIG_FMT_NULL;
                                        if (sm_debug_enable && !sm_print_nosig) {
                                                pr_info("[smr.%d] no signal\n",devp->index);
                                                sm_print_nosig = 1;
                                        }
                                        sm_print_unstable = 0;
                                }
                        }
                        else
                        {
                                ++sm_p->exit_nosig_counter;
                                if (sm_p->exit_nosig_counter >= nosig2_unstable_cnt)
                                {
                                        tvin_smr_init_counter(devp->index);
                                        sm_p->state = TVIN_SM_STATUS_UNSTABLE;
                                        if (sm_debug_enable)
                                                pr_info("[smr.%d] no signal --> unstable\n",devp->index);
                                        sm_print_nosig  = 0;
                                        sm_print_unstable = 0;
                                }
                        }
                        break;

                case TVIN_SM_STATUS_UNSTABLE:
                        ++sm_p->state_counter;
                        if (devp->parm.flag & TVIN_PARM_FLAG_CAL)
                                devp->parm.flag &= ~TVIN_PARM_FLAG_CAL;
                        if (sm_ops->nosig(devp->frontend))
                        {
                                sm_p->back_stable_counter = 0;
                                ++sm_p->back_nosig_counter;
                                if (sm_p->back_nosig_counter >= sm_p->back_nosig_max_cnt)
                                {
                                        tvin_smr_init_counter(devp->index);
                                        sm_p->state = TVIN_SM_STATUS_NOSIG;
                                        info->status = TVIN_SIG_STATUS_NOSIG;
                                        info->fmt    = TVIN_SIG_FMT_NULL;
                                        if (sm_debug_enable)
                                                pr_info("[smr.%d] unstable --> no signal\n",devp->index);
                                        sm_print_nosig  = 0;
                                        sm_print_unstable = 0;
                                }
                        }
                        else
                        {
                                sm_p->back_nosig_counter = 0;
                                if (sm_ops->fmt_changed(devp->frontend) )

                                {
                                        sm_p->back_stable_counter = 0;
                                        if((port == TVIN_PORT_CVBS0)&&devp->unstable_flag)
                                                unstable_in_cnt = sm_p->atv_unstable_in_cnt;//UNSTABLE_ATV_MAX_CNT;
                                        else
                                                unstable_in_cnt = other_unstable_in_cnt;
                                        if (sm_p->state_counter >= unstable_in_cnt)
                                        {
                                                sm_p->state_counter  = unstable_in_cnt;
                                                info->status   = TVIN_SIG_STATUS_UNSTABLE;
                                                info->fmt      = TVIN_SIG_FMT_NULL;
                                                if (sm_debug_enable && !sm_print_unstable) {
                                                        pr_info("[smr.%d] unstable\n",devp->index);
                                                        sm_print_unstable = 1;
                                                }
                                                sm_print_nosig  = 0;
                                        }
                                }
                                else
                                {
                                        ++sm_p->back_stable_counter;
                                        if(port == TVIN_PORT_CVBS0)
                                                unstable_in_cnt = sm_p->atv_unstable_out_cnt;//UNSTABLE_ATV_MAX_CNT;
                                        else if ((port >= TVIN_PORT_HDMI0) && (port <= TVIN_PORT_HDMI7 ))
                                                unstable_in_cnt = sm_p->hdmi_unstable_out_cnt;
                                        else
                                                unstable_in_cnt = other_unstable_out_cnt;
                                        if (sm_p->back_stable_counter >= unstable_in_cnt)
                                        {   //must wait enough time for cvd signal lock
                                                sm_p->back_stable_counter    = 0;
                                                sm_p->state_counter               = 0;
                                                if (sm_ops->get_fmt) {
                                                        info->fmt   = sm_ops->get_fmt(devp->frontend);
                                                        if (sm_ops->get_sig_propery)
                                                        {
                                                                sm_ops->get_sig_propery(devp->frontend, &devp->prop);
                                                                devp->parm.info.trans_fmt = devp->prop.trans_fmt;
                                                                devp->parm.info.reserved = devp->prop.dvi_info;
								devp->pre_prop.color_format = devp->prop.color_format;
                                                        }
                                                }
                                                else
                                                        info->fmt   = TVIN_SIG_FMT_NULL;

                                                /* set signal status */
                                                if(info->fmt == TVIN_SIG_FMT_NULL)
                                                {
                                                        info->status = TVIN_SIG_STATUS_NOTSUP;
                                                        if (sm_debug_enable && !sm_print_notsup) {
                                                                pr_info("[smr.%d] unstable --> not support\n",devp->index);
                                                                sm_print_notsup = 1;
                                                        }
                                                }
                                                else
                                                {
                                                        if (sm_ops->fmt_config)
                                                                sm_ops->fmt_config(devp->frontend);
                                                        tvin_smr_init_counter(devp->index);
                                                        sm_p->state = TVIN_SM_STATUS_PRESTABLE;
                                                        if (sm_debug_enable)
                                                                pr_info("[smr.%d] unstable --> prestable, and format is %d(%s)\n",
                                                                                devp->index,info->fmt, tvin_sig_fmt_str(info->fmt));
                                                        sm_print_nosig  = 0;
                                                        sm_print_unstable = 0;
                                                        sm_print_fmt_nosig = 0;
                                                        sm_print_fmt_chg = 0;
                                                }
                                        }
                                }
                        }
                        break;

                case TVIN_SM_STATUS_PRESTABLE:
                        {
                                bool nosig = false, fmt_changed = false;//, pll_lock = false;
                                devp->unstable_flag = true;

                                if (devp->parm.flag & TVIN_PARM_FLAG_CAL)
                                        devp->parm.flag &= ~TVIN_PARM_FLAG_CAL;
                                if (sm_ops->nosig(devp->frontend)) {
                                        nosig = true;
                                        if (sm_debug_enable)
                                                pr_info("[smr.%d] warning: no signal\n",devp->index);
                                }

                                if (sm_ops->fmt_changed(devp->frontend)) {
                                        fmt_changed = true;
                                        if (sm_debug_enable)
                                                pr_info("[smr.%d] warning: format changed\n",devp->index);
                                }

                                if (nosig || fmt_changed)
                                {
                                        ++sm_p->state_counter;
                                        if (sm_p->state_counter >= other_stable_out_cnt)
                                        {
                                                tvin_smr_init_counter(devp->index);
                                                sm_p->state = TVIN_SM_STATUS_UNSTABLE;
                                                if (sm_debug_enable)
                                                        pr_info("[smr.%d] prestable --> unstable\n",devp->index);
                                                sm_print_nosig  = 0;
                                                sm_print_notsup = 0;
                                                sm_print_unstable = 0;

                                                break;
                                        }
                                }
                                else
                                {
                                        sm_p->state_counter = 0;
                                }

                                /* wait comp stable */
                                if ((port >= TVIN_PORT_COMP0) &&
                                                (port <= TVIN_PORT_COMP7))
                                {
                                        ++sm_p->exit_prestable_counter;
                                        if (sm_p->exit_prestable_counter >= comp_pre2_stable_cnt)
                                        {
                                                tvin_smr_init_counter(devp->index);
                                                sm_p->state       = TVIN_SM_STATUS_STABLE;
                                                info->status        = TVIN_SIG_STATUS_STABLE;
                                                if (sm_debug_enable)
                                                        pr_info("[smr.%d] prestable --> stable\n",devp->index);
                                                sm_print_nosig  = 0;
                                                sm_print_notsup = 0;
                                        }
                                }
                                else
                                {
                                        sm_p->state       = TVIN_SM_STATUS_STABLE;
                                        info->status        = TVIN_SIG_STATUS_STABLE;
                                        if (sm_debug_enable)
                                                pr_info("[smr.%d] prestable --> stable\n",devp->index);
                                        sm_print_nosig  = 0;
                                        sm_print_notsup = 0;
                                }
                                break;
                        }
                case TVIN_SM_STATUS_STABLE:
                        {
                                bool nosig = false, fmt_changed = false;//, pll_lock = false;
                                unsigned int stable_out_cnt = 0;

                                devp->unstable_flag = true;

                                if (devp->parm.flag & TVIN_PARM_FLAG_CAL)
                                        devp->parm.flag &= ~TVIN_PARM_FLAG_CAL;
                                if (sm_ops->nosig(devp->frontend)) {
                                        nosig = true;
                                        if (sm_debug_enable && !sm_print_fmt_nosig)
                                        {
                                                pr_info("[smr.%d] warning: no signal\n",devp->index);
                                                sm_print_fmt_nosig = 1;
                                        }
                                }

                                if (sm_ops->fmt_changed(devp->frontend)) {
                                        fmt_changed = true;
                                        if (sm_debug_enable && !sm_print_fmt_chg)
                                        {
                                                pr_info("[smr.%d] warning: format changed\n",devp->index);
                                                sm_print_fmt_chg = 1;
                                        }
                                }
                            hdmirx_color_fmt_handler(devp);
#if 0
                                if (sm_ops->pll_lock(devp->frontend)) {
                                        pll_lock = true;
                                }
                                else {
                                        pll_lock = false;
                                        if (sm_debug_enable)
                                                pr_info("[smr] warning: pll lock failed\n");
                                }
#endif

                                if (nosig || fmt_changed /* || !pll_lock */)
                                {
                                        ++sm_p->state_counter;
                                        if (port == TVIN_PORT_CVBS0)
                                                stable_out_cnt = sm_p->atv_stable_out_cnt;
                                        else
                                                stable_out_cnt = other_stable_out_cnt;
                                        if (sm_p->state_counter >= stable_out_cnt)
                                        {
                                                tvin_smr_init_counter(devp->index);
                                                sm_p->state = TVIN_SM_STATUS_UNSTABLE;
                                                if (sm_debug_enable)
                                                        pr_info("[smr.%d] stable --> unstable\n",devp->index);
                                                sm_print_nosig  = 0;
                                                sm_print_notsup = 0;
                                                sm_print_unstable = 0;
                                                sm_print_fmt_nosig = 0;
                                                sm_print_fmt_chg = 0;
                                        }
                                }
                                else
                                {
                                        sm_p->state_counter = 0;
                                }
                                break;
                        }
                case TVIN_SM_STATUS_NULL:
                default:
                        if (devp->parm.flag & TVIN_PARM_FLAG_CAL)
                                devp->parm.flag &= ~TVIN_PARM_FLAG_CAL;
                        sm_p->state = TVIN_SM_STATUS_NOSIG;
                        break;
        }
}
#endif
/*
 * tvin state machine routine init
 *
 */

void tvin_smr_init(int index)
{
        sm_dev[index].state = TVIN_SM_STATUS_NULL;
        sm_dev[index].atv_stable_out_cnt = atv_stable_out_cnt;
        sm_dev[index].atv_unstable_in_cnt = atv_unstable_in_cnt;
        sm_dev[index].back_nosig_max_cnt =back_nosig_max_cnt;
        sm_dev[index].atv_unstable_out_cnt = atv_unstable_out_cnt;
        sm_dev[index].hdmi_unstable_out_cnt = hdmi_unstable_out_cnt;
        tvin_smr_init_counter(index);
}

enum tvin_sm_status_e tvin_get_sm_status(int index)
{
        return sm_dev[index].state;
}


