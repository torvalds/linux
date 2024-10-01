// SPDX-License-Identifier: GPL-2.0-only
/*
 * Trace points for the RDMA Connection Manager.
 *
 * Author: Chuck Lever <chuck.lever@oracle.com>
 *
 * Copyright (c) 2019, Oracle and/or its affiliates. All rights reserved.
 */

#define CREATE_TRACE_POINTS

#include <rdma/rdma_cm.h>
#include <rdma/ib_cm.h>
#include "cma_priv.h"

#include "cma_trace.h"
