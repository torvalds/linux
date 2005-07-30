/******************************************************************************
 *                  QLOGIC LINUX SOFTWARE
 *
 * QLogic ISP2x00 device driver for Linux 2.6.x
 * Copyright (C) 2003-2005 QLogic Corporation
 * (www.qlogic.com)
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation; either version 2, or (at your option) any
 * later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 ******************************************************************************/
/*
 * Compile time Options:
 *     0 - Disable and 1 - Enable
 */
#define DEBUG_QLA2100		0	/* For Debug of qla2x00 */

#define USE_ABORT_TGT		1	/* Use Abort Target mbx cmd */

#define MAX_RETRIES_OF_ISP_ABORT	5

/* Max time to wait for the loop to be in LOOP_READY state */
#define MAX_LOOP_TIMEOUT	(60 * 5)
#define EH_ACTIVE		1	/* Error handler active */

/*
 * Some vendor subsystems do not recover properly after a device reset.  Define
 * the following to force a logout after a successful device reset.
 */
#undef LOGOUT_AFTER_DEVICE_RESET

#include "qla_version.h"
