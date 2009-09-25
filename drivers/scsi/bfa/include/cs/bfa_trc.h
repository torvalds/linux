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
#ifndef __BFA_TRC_H__
#define __BFA_TRC_H__

#include <bfa_os_inc.h>

#ifndef BFA_TRC_MAX
#define BFA_TRC_MAX	(4 * 1024)
#endif

#ifndef BFA_TRC_TS
#define BFA_TRC_TS(_trcm)	((_trcm)->ticks++)
#endif

struct bfa_trc_s {
#ifdef __BIGENDIAN
	u16	fileno;
	u16	line;
#else
	u16	line;
	u16	fileno;
#endif
	u32	timestamp;
	union {
		struct {
			u32	rsvd;
			u32	u32;
		} u32;
		u64	u64;
	} data;
};


struct bfa_trc_mod_s {
	u32	head;
	u32	tail;
	u32	ntrc;
	u32	stopped;
	u32	ticks;
	u32	rsvd[3];
	struct bfa_trc_s trc[BFA_TRC_MAX];
};


enum {
	BFA_TRC_FW   = 1,	/*  firmware modules */
	BFA_TRC_HAL  = 2,	/*  BFA modules */
	BFA_TRC_FCS  = 3,	/*  BFA FCS modules */
	BFA_TRC_LDRV = 4,	/*  Linux driver modules */
	BFA_TRC_SDRV = 5,	/*  Solaris driver modules */
	BFA_TRC_VDRV = 6,	/*  vmware driver modules */
	BFA_TRC_WDRV = 7,	/*  windows driver modules */
	BFA_TRC_AEN  = 8,	/*  AEN module */
	BFA_TRC_BIOS = 9,	/*  bios driver modules */
	BFA_TRC_EFI  = 10,	/*  EFI driver modules */
	BNA_TRC_WDRV = 11,	/*  BNA windows driver modules */
	BNA_TRC_VDRV = 12,	/*  BNA vmware driver modules */
	BNA_TRC_SDRV = 13,	/*  BNA Solaris driver modules */
	BNA_TRC_LDRV = 14,	/*  BNA Linux driver modules */
	BNA_TRC_HAL  = 15,	/*  BNA modules */
	BFA_TRC_CNA  = 16,	/*  Common modules */
	BNA_TRC_IMDRV = 17	/*  BNA windows intermediate driver modules */
};
#define BFA_TRC_MOD_SH	10
#define BFA_TRC_MOD(__mod)	((BFA_TRC_ ## __mod) << BFA_TRC_MOD_SH)

/**
 * Define a new tracing file (module). Module should match one defined above.
 */
#define BFA_TRC_FILE(__mod, __submod)					\
	static int __trc_fileno = ((BFA_TRC_ ## __mod ## _ ## __submod) | \
						 BFA_TRC_MOD(__mod))


#define bfa_trc32(_trcp, _data)	\
	__bfa_trc((_trcp)->trcmod, __trc_fileno, __LINE__, (u32)_data)


#ifndef BFA_BOOT_BUILD
#define bfa_trc(_trcp, _data)	\
	__bfa_trc((_trcp)->trcmod, __trc_fileno, __LINE__, (u64)_data)
#else
void bfa_boot_trc(struct bfa_trc_mod_s *trcmod, u16 fileno,
			u16 line, u32 data);
#define bfa_trc(_trcp, _data)	\
	bfa_boot_trc((_trcp)->trcmod, __trc_fileno, __LINE__, (u32)_data)
#endif


static inline void
bfa_trc_init(struct bfa_trc_mod_s *trcm)
{
	trcm->head = trcm->tail = trcm->stopped = 0;
	trcm->ntrc = BFA_TRC_MAX;
}


static inline void
bfa_trc_stop(struct bfa_trc_mod_s *trcm)
{
	trcm->stopped = 1;
}

#ifdef FWTRC
extern void dc_flush(void *data);
#else
#define dc_flush(data)
#endif


static inline void
__bfa_trc(struct bfa_trc_mod_s *trcm, int fileno, int line, u64 data)
{
	int		tail = trcm->tail;
	struct bfa_trc_s 	*trc = &trcm->trc[tail];

	if (trcm->stopped)
		return;

	trc->fileno = (u16) fileno;
	trc->line = (u16) line;
	trc->data.u64 = data;
	trc->timestamp = BFA_TRC_TS(trcm);
	dc_flush(trc);

	trcm->tail = (trcm->tail + 1) & (BFA_TRC_MAX - 1);
	if (trcm->tail == trcm->head)
		trcm->head = (trcm->head + 1) & (BFA_TRC_MAX - 1);
	dc_flush(trcm);
}


static inline void
__bfa_trc32(struct bfa_trc_mod_s *trcm, int fileno, int line, u32 data)
{
	int		tail = trcm->tail;
	struct bfa_trc_s *trc = &trcm->trc[tail];

	if (trcm->stopped)
		return;

	trc->fileno = (u16) fileno;
	trc->line = (u16) line;
	trc->data.u32.u32 = data;
	trc->timestamp = BFA_TRC_TS(trcm);
	dc_flush(trc);

	trcm->tail = (trcm->tail + 1) & (BFA_TRC_MAX - 1);
	if (trcm->tail == trcm->head)
		trcm->head = (trcm->head + 1) & (BFA_TRC_MAX - 1);
	dc_flush(trcm);
}

#ifndef BFA_PERF_BUILD
#define bfa_trc_fp(_trcp, _data)	bfa_trc(_trcp, _data)
#else
#define bfa_trc_fp(_trcp, _data)
#endif

#endif /* __BFA_TRC_H__ */

