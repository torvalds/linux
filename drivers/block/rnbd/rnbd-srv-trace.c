// SPDX-License-Identifier: GPL-2.0+
/*
 * RDMA Network Block Driver
 *
 * Copyright (c) 2022 1&1 IONOS SE. All rights reserved.
 */
#include "rtrs.h"
#include "rtrs-srv.h"
#include "rnbd-srv.h"
#include "rnbd-proto.h"

/*
 * We include this last to have the helpers above available for the trace
 * event implementations.
 */
#define CREATE_TRACE_POINTS
#include "rnbd-srv-trace.h"
