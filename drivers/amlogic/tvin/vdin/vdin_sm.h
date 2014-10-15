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

#ifndef __TVIN_STATE_MACHINE_H
#define __TVIN_STATE_MACHINE_H

#include "vdin_drv.h"

typedef enum tvin_sm_status_e {
    TVIN_SM_STATUS_NULL = 0, // processing status from init to the finding of the 1st confirmed status
    TVIN_SM_STATUS_NOSIG,    // no signal - physically no signal
    TVIN_SM_STATUS_UNSTABLE, // unstable - physically bad signal
    TVIN_SM_STATUS_NOTSUP,   // not supported - physically good signal & not supported
    TVIN_SM_STATUS_PRESTABLE,
    TVIN_SM_STATUS_STABLE,   // stable - physically good signal & supported
} tvin_sm_status_t;
typedef struct tvin_sm_s {
    enum tvin_sm_status_e state;
    unsigned int state_counter; // STATE_NOSIG, STATE_UNSTABLE
    unsigned int exit_nosig_counter; // STATE_NOSIG
    unsigned int back_nosig_counter; // STATE_UNSTABLE
    unsigned int back_stable_counter; // STATE_UNSTABLE
    unsigned int exit_prestable_counter; // STATE_PRESTABLE
    //thresholds of state switchted  
    int back_nosig_max_cnt ;
    int atv_unstable_in_cnt ;
    int atv_unstable_out_cnt;
    int atv_stable_out_cnt;
    int hdmi_unstable_out_cnt;
}tvin_sm_t;
void tvin_smr(struct vdin_dev_s *pdev);
void tvin_smr_init(int index);

enum tvin_sm_status_e tvin_get_sm_status(int index);

#endif

