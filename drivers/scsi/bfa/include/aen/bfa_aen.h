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
#ifndef __BFA_AEN_H__
#define __BFA_AEN_H__

#include "defs/bfa_defs_aen.h"
#include "defs/bfa_defs_status.h"
#include "cs/bfa_debug.h"

#define BFA_AEN_MAX_ENTRY	512

extern int bfa_aen_max_cfg_entry;
struct bfa_aen_s {
	void		*bfad;
	int		max_entry;
	int		write_index;
	int		read_index;
	int		bfad_num;
	int		seq_num;
	void		(*aen_cb_notify)(void *bfad);
	void		(*gettimeofday)(struct bfa_timeval_s *tv);
	struct bfa_trc_mod_s *trcmod;
	int		app_ri[BFA_AEN_MAX_APP]; /* For multiclient support */
	struct bfa_aen_entry_s list[BFA_AEN_MAX_ENTRY]; /* Must be the last */
};


/**
 * Public APIs
 */
static inline void
bfa_aen_set_max_cfg_entry(int max_entry)
{
	bfa_aen_max_cfg_entry = max_entry;
}

static inline int
bfa_aen_get_max_cfg_entry(void)
{
	return bfa_aen_max_cfg_entry;
}

static inline int
bfa_aen_get_meminfo(void)
{
	return sizeof(struct bfa_aen_entry_s) * bfa_aen_get_max_cfg_entry();
}

static inline int
bfa_aen_get_wi(struct bfa_aen_s *aen)
{
	return aen->write_index;
}

static inline int
bfa_aen_get_ri(struct bfa_aen_s *aen)
{
	return aen->read_index;
}

static inline int
bfa_aen_fetch_count(struct bfa_aen_s *aen, enum bfa_aen_app  app_id)
{
	bfa_assert((app_id < BFA_AEN_MAX_APP) && (app_id >= bfa_aen_app_bcu));
	return ((aen->write_index + aen->max_entry) - aen->app_ri[app_id])
		% aen->max_entry;
}

int bfa_aen_init(struct bfa_aen_s *aen, struct bfa_trc_mod_s *trcmod,
		void *bfad, int bfad_num, void (*aen_cb_notify)(void *),
		void (*gettimeofday)(struct bfa_timeval_s *));

void bfa_aen_post(struct bfa_aen_s *aen, enum bfa_aen_category aen_category,
		     int aen_type, union bfa_aen_data_u *aen_data);

bfa_status_t bfa_aen_fetch(struct bfa_aen_s *aen,
			struct bfa_aen_entry_s *aen_entry,
			int entry_req, enum bfa_aen_app app_id, int *entry_ret);

int bfa_aen_get_inst(struct bfa_aen_s *aen);

#endif /* __BFA_AEN_H__ */
