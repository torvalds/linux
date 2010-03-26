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

#ifndef __BFA_DEFS_DRIVER_H__
#define __BFA_DEFS_DRIVER_H__

/**
 * Driver statistics
 */
struct bfa_driver_stats_s {
	u16    tm_io_abort;
    u16    tm_io_abort_comp;
    u16    tm_lun_reset;
    u16    tm_lun_reset_comp;
    u16    tm_target_reset;
    u16    tm_bus_reset;
    u16    ioc_restart;        /*  IOC restart count */
    u16    io_pending;         /*  outstanding io count per-IOC */
    u64    control_req;
    u64    input_req;
    u64    output_req;
    u64    input_words;
    u64    output_words;
};


#endif /* __BFA_DEFS_DRIVER_H__ */
