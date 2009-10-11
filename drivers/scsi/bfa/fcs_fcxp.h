/*
 * Copyright (c) 2005-2009 Brocade Communications Systems, Inc.
 * All rights reserved
 * www.brocade.com
 *
 * Linux driver for Brocade Fibre Channel Host Bus Adapter.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License (GPL) Version 2 as
 * published by the Free Software Foundation
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 */

/**
 *  fcs_fcxp.h FCXP helper macros for FCS
 */


#ifndef __FCS_FCXP_H__
#define __FCS_FCXP_H__

#define bfa_fcs_fcxp_alloc(__fcs)	\
	bfa_fcxp_alloc(NULL, (__fcs)->bfa, 0, 0, NULL, NULL, NULL, NULL)

#endif /* __FCS_FCXP_H__ */
