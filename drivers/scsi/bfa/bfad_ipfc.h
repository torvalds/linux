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
#ifndef __BFA_DRV_IPFC_H__
#define __BFA_DRV_IPFC_H__


#define IPFC_NAME ""

#define bfad_ipfc_module_init(x) do {} while (0)
#define bfad_ipfc_module_exit(x) do {} while (0)
#define bfad_ipfc_probe(x) do {} while (0)
#define bfad_ipfc_probe_undo(x) do {} while (0)
#define bfad_ipfc_port_config(x, y) BFA_STATUS_OK
#define bfad_ipfc_port_unconfig(x, y) do {} while (0)

#define bfad_ipfc_probe_post(x) do {} while (0)
#define bfad_ipfc_port_new(x, y, z) BFA_STATUS_OK
#define bfad_ipfc_port_delete(x, y) do {} while (0)
#define bfad_ipfc_port_online(x, y) do {} while (0)
#define bfad_ipfc_port_offline(x, y) do {} while (0)

#define bfad_ip_get_attr(x) BFA_STATUS_FAILED
#define bfad_ip_reset_drv_stats(x) BFA_STATUS_FAILED
#define bfad_ip_get_drv_stats(x, y) BFA_STATUS_FAILED
#define bfad_ip_enable_ipfc(x, y, z) BFA_STATUS_FAILED


#endif
