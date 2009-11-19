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
#ifndef __BFA_DEFS_FCPIM_H__
#define __BFA_DEFS_FCPIM_H__

struct bfa_fcpim_stats_s {
	u32        total_ios;	/*  Total IO count */
	u32        qresumes;	/*  IO waiting for CQ space */
	u32        no_iotags;	/*  NO IO contexts */
	u32        io_aborts;	/*  IO abort requests */
	u32        no_tskims;	/*  NO task management contexts */
	u32        iocomp_ok;	/*  IO completions with OK status */
	u32        iocomp_underrun;	/*  IO underrun (good) */
	u32        iocomp_overrun;	/*  IO overrun (good) */
	u32        iocomp_aborted;	/*  Aborted IO requests */
	u32        iocomp_timedout;	/*  IO timeouts */
	u32        iocom_nexus_abort;	/*  IO selection timeouts */
	u32        iocom_proto_err;	/*  IO protocol errors */
	u32        iocom_dif_err;	/*  IO SBC-3 protection errors */
	u32        iocom_tm_abort;	/*  IO aborted by TM requests */
	u32        iocom_sqer_needed;	/*  IO retry for SQ error
						 *recovery */
	u32        iocom_res_free;	/*  Delayed freeing of IO resources */
	u32        iocomp_scsierr;	/*  IO with non-good SCSI status */
	u32        iocom_hostabrts;	/*  Host IO abort requests */
	u32        iocom_utags;	/*  IO comp with unknown tags */
	u32        io_cleanups;	/*  IO implicitly aborted */
	u32        io_tmaborts;	/*  IO aborted due to TM commands */
	u32        rsvd;
};
#endif /*__BFA_DEFS_FCPIM_H__*/
