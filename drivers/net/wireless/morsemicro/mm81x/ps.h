/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2017-2026 Morse Micro
 */

#ifndef _MM81X_PS_H_
#define _MM81X_PS_H_

#include "core.h"

/* This should be nominally <= the dynamic ps timeout */
#define NETWORK_BUS_TIMEOUT_MS (90)

/* The default period of time to wait to re-evaluate powersave */
#define DEFAULT_BUS_TIMEOUT_MS (50)

void mm81x_ps_disable(struct mm81x *mors);
void mm81x_ps_enable(struct mm81x *mors);
int mm81x_ps_init(struct mm81x *mors);
void mm81x_ps_finish(struct mm81x *mors);

#endif /* !_MM81X_PS_H_ */
