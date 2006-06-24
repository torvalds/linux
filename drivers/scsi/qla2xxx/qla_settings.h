/*
 * QLogic Fibre Channel HBA Driver
 * Copyright (c)  2003-2005 QLogic Corporation
 *
 * See LICENSE.qla2xxx for copyright and licensing details.
 */
/*
 * Compile time Options:
 *     0 - Disable and 1 - Enable
 */
#define DEBUG_QLA2100		0	/* For Debug of qla2x00 */

#define USE_ABORT_TGT		1	/* Use Abort Target mbx cmd */

#define MAX_RETRIES_OF_ISP_ABORT	5

/* Max time to wait for the loop to be in LOOP_READY state */
#define MAX_LOOP_TIMEOUT	(60 * 5)

/*
 * Some vendor subsystems do not recover properly after a device reset.  Define
 * the following to force a logout after a successful device reset.
 */
#undef LOGOUT_AFTER_DEVICE_RESET

#include "qla_version.h"
