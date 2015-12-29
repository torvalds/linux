/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include "eventmgr.h"
#include "eventinit.h"
#include "eventmanagement.h"
#include "eventmanager.h"
#include "power_state.h"
#include "hardwaremanager.h"

int psm_get_ui_state(struct pp_eventmgr *eventmgr, enum PP_StateUILabel ui_label, unsigned long *state_id);

int psm_get_state_by_classification(struct pp_eventmgr *eventmgr, enum PP_StateClassificationFlag flag, unsigned long *state_id);

int psm_set_states(struct pp_eventmgr *eventmgr, unsigned long *state_id);

int psm_adjust_power_state_dynamic(struct pp_eventmgr *eventmgr, bool skip);

int psm_adjust_power_state_static(struct pp_eventmgr *eventmgr, bool skip);
