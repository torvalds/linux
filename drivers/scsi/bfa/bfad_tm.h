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

/*
 * Brocade Fibre Channel HBA Linux Target Mode Driver
 */

/**
 *  tm/dummy/bfad_tm.h BFA callback dummy header file for BFA Linux target mode PCI interface module.
 */

#ifndef __BFAD_TM_H__
#define __BFAD_TM_H__

#include <defs/bfa_defs_status.h>

#define FCPT_NAME 		""

/*
 * Called from base Linux driver on (De)Init events
 */

/* attach tgt template with scst */
#define bfad_tm_module_init()	do {} while (0)

/* detach/release tgt template */
#define bfad_tm_module_exit()	do {} while (0)

#define bfad_tm_probe(x)	do {} while (0)
#define bfad_tm_probe_undo(x)	do {} while (0)
#define bfad_tm_probe_post(x)	do {} while (0)

/*
 * Called by base Linux driver but triggered by BFA FCS on config events
 */
#define bfad_tm_port_new(x, y)		BFA_STATUS_OK
#define bfad_tm_port_delete(x, y)	do {} while (0)

/*
 * Called by base Linux driver but triggered by BFA FCS on PLOGI/O events
 */
#define bfad_tm_port_online(x, y)	do {} while (0)
#define bfad_tm_port_offline(x, y)	do {} while (0)

#endif
