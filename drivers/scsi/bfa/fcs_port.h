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
 *  fcs_pport.h FCS physical port interfaces
 */


#ifndef __FCS_PPORT_H__
#define __FCS_PPORT_H__

/*
 * fcs friend functions: only between fcs modules
 */
void bfa_fcs_pport_modinit(struct bfa_fcs_s *fcs);
void bfa_fcs_pport_modexit(struct bfa_fcs_s *fcs);

#endif /* __FCS_PPORT_H__ */
