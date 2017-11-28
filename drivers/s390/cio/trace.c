// SPDX-License-Identifier: GPL-2.0
/*
 * Tracepoint definitions for s390_cio
 *
 * Copyright IBM Corp. 2015
 * Author(s): Peter Oberparleiter <oberpar@linux.vnet.ibm.com>
 */

#include <asm/crw.h>
#include "cio.h"

#define CREATE_TRACE_POINTS
#include "trace.h"

EXPORT_TRACEPOINT_SYMBOL(s390_cio_stsch);
EXPORT_TRACEPOINT_SYMBOL(s390_cio_msch);
EXPORT_TRACEPOINT_SYMBOL(s390_cio_tsch);
EXPORT_TRACEPOINT_SYMBOL(s390_cio_tpi);
EXPORT_TRACEPOINT_SYMBOL(s390_cio_ssch);
EXPORT_TRACEPOINT_SYMBOL(s390_cio_csch);
EXPORT_TRACEPOINT_SYMBOL(s390_cio_hsch);
EXPORT_TRACEPOINT_SYMBOL(s390_cio_xsch);
EXPORT_TRACEPOINT_SYMBOL(s390_cio_rsch);
EXPORT_TRACEPOINT_SYMBOL(s390_cio_rchp);
EXPORT_TRACEPOINT_SYMBOL(s390_cio_chsc);
