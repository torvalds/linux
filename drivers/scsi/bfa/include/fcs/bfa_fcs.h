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

#ifndef __BFA_FCS_H__
#define __BFA_FCS_H__

#include <cs/bfa_debug.h>
#include <defs/bfa_defs_status.h>
#include <defs/bfa_defs_version.h>
#include <bfa.h>
#include <fcs/bfa_fcs_fabric.h>

#define BFA_FCS_OS_STR_LEN  		64

struct bfa_fcs_stats_s {
	struct {
		u32        untagged; /*  untagged receive frames */
		u32        tagged;	/*  tagged receive frames */
		u32        vfid_unknown;	/*  VF id is unknown */
	} uf;
};

struct bfa_fcs_driver_info_s {
	u8  version[BFA_VERSION_LEN];		/*  Driver Version */
	u8  host_machine_name[BFA_FCS_OS_STR_LEN];
	u8  host_os_name[BFA_FCS_OS_STR_LEN]; /*  OS name and version */
	u8  host_os_patch[BFA_FCS_OS_STR_LEN];/*  patch or service pack */
	u8  os_device_name[BFA_FCS_OS_STR_LEN]; /*  Driver Device Name */
};

struct bfa_fcs_s {
	struct bfa_s      *bfa;	/*  corresponding BFA bfa instance */
	struct bfad_s         *bfad; /*  corresponding BDA driver instance */
	struct bfa_log_mod_s  *logm;	/*  driver logging module instance */
	struct bfa_trc_mod_s  *trcmod;	/*  tracing module */
	struct bfa_aen_s      *aen;	/*  aen component */
	bfa_boolean_t   vf_enabled;	/*  VF mode is enabled */
	bfa_boolean_t   fdmi_enabled;   /*!< FDMI is enabled */
	bfa_boolean_t min_cfg;		/* min cfg enabled/disabled */
	u16        port_vfid;	/*  port default VF ID */
	struct bfa_fcs_driver_info_s driver_info;
	struct bfa_fcs_fabric_s fabric; /*  base fabric state machine */
	struct bfa_fcs_stats_s	stats;	/*  FCS statistics */
	struct bfa_wc_s       	wc;	/*  waiting counter */
};

/*
 * bfa fcs API functions
 */
void bfa_fcs_attach(struct bfa_fcs_s *fcs, struct bfa_s *bfa,
			struct bfad_s *bfad, bfa_boolean_t min_cfg);
void bfa_fcs_init(struct bfa_fcs_s *fcs);
void bfa_fcs_driver_info_init(struct bfa_fcs_s *fcs,
			struct bfa_fcs_driver_info_s *driver_info);
void bfa_fcs_set_fdmi_param(struct bfa_fcs_s *fcs, bfa_boolean_t fdmi_enable);
void bfa_fcs_exit(struct bfa_fcs_s *fcs);
void bfa_fcs_trc_init(struct bfa_fcs_s *fcs, struct bfa_trc_mod_s *trcmod);
void bfa_fcs_log_init(struct bfa_fcs_s *fcs, struct bfa_log_mod_s *logmod);
void bfa_fcs_aen_init(struct bfa_fcs_s *fcs, struct bfa_aen_s *aen);
void 	  	bfa_fcs_start(struct bfa_fcs_s *fcs);

#endif /* __BFA_FCS_H__ */
