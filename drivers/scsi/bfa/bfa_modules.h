/*
 * Copyright (c) 2005-2010 Brocade Communications Systems, Inc.
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

/*
 * Macro to define a new BFA module
 */
#define BFA_MODULE(__mod)						\
	static void bfa_ ## __mod ## _meminfo(				\
			struct bfa_iocfc_cfg_s *cfg,			\
			struct bfa_meminfo_s *meminfo,			\
			struct bfa_s *bfa);				\
	static void bfa_ ## __mod ## _attach(struct bfa_s *bfa,		\
			void *bfad, struct bfa_iocfc_cfg_s *cfg,	\
			struct bfa_pcidev_s *pcidev);      \
	static void bfa_ ## __mod ## _detach(struct bfa_s *bfa);      \
	static void bfa_ ## __mod ## _start(struct bfa_s *bfa);      \
	static void bfa_ ## __mod ## _stop(struct bfa_s *bfa);      \
	static void bfa_ ## __mod ## _iocdisable(struct bfa_s *bfa);      \
									\
	extern struct bfa_module_s hal_mod_ ## __mod;			\
	struct bfa_module_s hal_mod_ ## __mod = {			\
		bfa_ ## __mod ## _meminfo,				\
		bfa_ ## __mod ## _attach,				\
		bfa_ ## __mod ## _detach,				\
		bfa_ ## __mod ## _start,				\
		bfa_ ## __mod ## _stop,					\
		bfa_ ## __mod ## _iocdisable,				\
	}

#define BFA_CACHELINE_SZ	(256)

/*
 * Structure used to interact between different BFA sub modules
 *
 * Each sub module needs to implement only the entry points relevant to it (and
 * can leave entry points as NULL)
 */
struct bfa_module_s {
	void (*meminfo) (struct bfa_iocfc_cfg_s *cfg,
			 struct bfa_meminfo_s *meminfo,
			 struct bfa_s *bfa);
	void (*attach) (struct bfa_s *bfa, void *bfad,
			struct bfa_iocfc_cfg_s *cfg,
			struct bfa_pcidev_s *pcidev);
	void (*detach) (struct bfa_s *bfa);
	void (*start) (struct bfa_s *bfa);
	void (*stop) (struct bfa_s *bfa);
	void (*iocdisable) (struct bfa_s *bfa);
};


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
extern struct bfa_module_s hal_mod_fcdiag;
extern struct bfa_module_s hal_mod_sgpg;
extern struct bfa_module_s hal_mod_fcport;
extern struct bfa_module_s hal_mod_fcxp;
extern struct bfa_module_s hal_mod_lps;
extern struct bfa_module_s hal_mod_uf;
extern struct bfa_module_s hal_mod_rport;
extern struct bfa_module_s hal_mod_fcp;
extern struct bfa_module_s hal_mod_dconf;

#endif /* __BFA_MODULES_H__ */
