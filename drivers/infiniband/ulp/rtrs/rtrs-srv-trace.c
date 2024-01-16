// SPDX-License-Identifier: GPL-2.0+
/*
 * RDMA Network Block Driver
 *
 * Copyright (c) 2022 1&1 IONOS SE. All rights reserved.
 */
#include "rtrs.h"
#include "rtrs-pri.h"
#include "rtrs-srv.h"

/*
 * We include this last to have the helpers above available for the trace
 * event implementations.
 */
#define CREATE_TRACE_POINTS
#include "rtrs-srv-trace.h"
