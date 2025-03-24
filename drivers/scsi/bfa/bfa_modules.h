/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2005-2014 Brocade Communications Systems, Inc.
 * Copyright (c) 2014- QLogic Corporation.
 * All rights reserved
 * www.qlogic.com
 *
 * Linux driver for QLogic BR-series Fibre Channel Host Bus Adapter.
 */

/*
 *  bfa_modules.h BFA modules
 */

#ifndef __BFA_MODULES_H__
#define __BFA_MODULES_H__

#include "bfa_cs.h"
#include "bfa.h"
#include "bfa_svc.h"
#include "bfa_fcpim.h"
#include "bfa_port.h"

struct bfa_modules_s {
	struct bfa_fcdiag_s	fcdiag;		/* fcdiag module */
	struct bfa_fcport_s	fcport;		/*  fc port module	      */
	struct bfa_fcxp_mod_s	fcxp_mod;	/*  fcxp module	      */
	struct bfa_lps_mod_s	lps_mod;	/*  fcxp module	      */
	struct bfa_uf_mod_s	uf_mod;		/*  unsolicited frame module */
	struct bfa_rport_mod_s	rport_mod;	/*  remote port module	      */
	struct bfa_fcp_mod_s	fcp_mod;	/*  FCP initiator module     */
	struct bfa_sgpg_mod_s	sgpg_mod;	/*  SG page module	      */
	struct bfa_port_s	port;		/*  Physical port module     */
	struct bfa_ablk_s	ablk;		/*  ASIC block config module */
	struct bfa_cee_s	cee;		/*  CEE Module	*/
	struct bfa_sfp_s	sfp;		/*  SFP module	*/
	struct bfa_flash_s	flash;		/*  flash module */
	struct bfa_diag_s	diag_mod;	/*  diagnostics module	*/
	struct bfa_phy_s	phy;		/*  phy module		*/
	struct bfa_dconf_mod_s	dconf_mod;	/*  DCONF common module	*/
	struct bfa_fru_s	fru;		/*  fru module		*/
};

/*
 * !!! Only append to the enums defined here to avoid any versioning
 * !!! needed between trace utility and driver version
 */
enum {
	BFA_TRC_HAL_CORE	= 1,
	BFA_TRC_HAL_FCXP	= 2,
	BFA_TRC_HAL_FCPIM	= 3,
	BFA_TRC_HAL_IOCFC_CT	= 4,
	BFA_TRC_HAL_IOCFC_CB	= 5,
};

#define BFA_CACHELINE_SZ	(256)

struct bfa_s {
	void			*bfad;		/*  BFA driver instance    */
	struct bfa_plog_s	*plog;		/*  portlog buffer	    */
	struct bfa_trc_mod_s	*trcmod;	/*  driver tracing	    */
	struct bfa_ioc_s	ioc;		/*  IOC module		    */
	struct bfa_iocfc_s	iocfc;		/*  IOCFC module	    */
	struct bfa_timer_mod_s	timer_mod;	/*  timer module	    */
	struct bfa_modules_s	modules;	/*  BFA modules	    */
	struct list_head	comp_q;		/*  pending completions     */
	bfa_boolean_t		queue_process;	/*  queue processing enabled */
	struct list_head	reqq_waitq[BFI_IOC_MAX_CQS];
	bfa_boolean_t		fcs;		/*  FCS is attached to BFA */
	struct bfa_msix_s	msix;
	int			bfa_aen_seq;
	bfa_boolean_t		intr_enabled;	/*  Status of interrupts */
};

extern bfa_boolean_t bfa_auto_recover;

void bfa_dconf_attach(struct bfa_s *, void *, struct bfa_iocfc_cfg_s *);
void bfa_dconf_meminfo(struct bfa_iocfc_cfg_s *, struct bfa_meminfo_s *,
		  struct bfa_s *);
void bfa_dconf_iocdisable(struct bfa_s *);
void bfa_fcp_attach(struct bfa_s *, void *, struct bfa_iocfc_cfg_s *,
		struct bfa_pcidev_s *);
void bfa_fcp_iocdisable(struct bfa_s *bfa);
void bfa_fcp_meminfo(struct bfa_iocfc_cfg_s *, struct bfa_meminfo_s *,
		struct bfa_s *);
void bfa_fcpim_iocdisable(struct bfa_fcp_mod_s *);
void bfa_fcport_start(struct bfa_s *);
void bfa_fcport_iocdisable(struct bfa_s *);
void bfa_fcport_meminfo(struct bfa_iocfc_cfg_s *, struct bfa_meminfo_s *,
		   struct bfa_s *);
void bfa_fcport_attach(struct bfa_s *, void *, struct bfa_iocfc_cfg_s *,
		struct bfa_pcidev_s *);
void bfa_fcxp_iocdisable(struct bfa_s *);
void bfa_fcxp_meminfo(struct bfa_iocfc_cfg_s *, struct bfa_meminfo_s *,
		struct bfa_s *);
void bfa_fcxp_attach(struct bfa_s *, void *, struct bfa_iocfc_cfg_s *,
		struct bfa_pcidev_s *);
void bfa_fcdiag_iocdisable(struct bfa_s *);
void bfa_fcdiag_attach(struct bfa_s *bfa, void *, struct bfa_iocfc_cfg_s *,
		struct bfa_pcidev_s *);
void bfa_ioim_lm_init(struct bfa_s *);
void bfa_lps_iocdisable(struct bfa_s *bfa);
void bfa_lps_meminfo(struct bfa_iocfc_cfg_s *, struct bfa_meminfo_s *,
		struct bfa_s *);
void bfa_lps_attach(struct bfa_s *, void *, struct bfa_iocfc_cfg_s *,
	struct bfa_pcidev_s *);
void bfa_rport_iocdisable(struct bfa_s *bfa);
void bfa_rport_meminfo(struct bfa_iocfc_cfg_s *, struct bfa_meminfo_s *,
		struct bfa_s *);
void bfa_rport_attach(struct bfa_s *, void *, struct bfa_iocfc_cfg_s *,
		struct bfa_pcidev_s *);
void bfa_sgpg_meminfo(struct bfa_iocfc_cfg_s *, struct bfa_meminfo_s *,
		struct bfa_s *);
void bfa_sgpg_attach(struct bfa_s *, void *bfad, struct bfa_iocfc_cfg_s *,
		struct bfa_pcidev_s *);
void bfa_uf_meminfo(struct bfa_iocfc_cfg_s *, struct bfa_meminfo_s *,
		struct bfa_s *);
void bfa_uf_attach(struct bfa_s *, void *, struct bfa_iocfc_cfg_s *,
		struct bfa_pcidev_s *);
void bfa_uf_start(struct bfa_s *);

#endif /* __BFA_MODULES_H__ */
