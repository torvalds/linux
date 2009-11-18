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

#define BFA_AEN_MAX_ENTRY   512

extern s32 bfa_aen_max_cfg_entry;
struct bfa_aen_s {
	void		*bfad;
	s32		max_entry;
	s32		write_index;
	s32		read_index;
	u32	bfad_num;
	u32	seq_num;
	void		(*aen_cb_notify)(void *bfad);
	void		(*gettimeofday)(struct bfa_timeval_s *tv);
	struct bfa_trc_mod_s 	*trcmod;
	struct bfa_aen_entry_s	list[BFA_AEN_MAX_ENTRY]; /* Must be the last */
};


/**
 * Public APIs
 */
static inline void
bfa_aen_set_max_cfg_entry(int max_entry)
{
	bfa_aen_max_cfg_entry = max_entry;
}

static inline s32
bfa_aen_get_max_cfg_entry(void)
{
	return bfa_aen_max_cfg_entry;
}

static inline s32
bfa_aen_get_meminfo(void)
{
	return (sizeof(struct bfa_aen_entry_s) * bfa_aen_get_max_cfg_entry());
}

static inline s32
bfa_aen_get_wi(struct bfa_aen_s *aen)
{
	return aen->write_index;
}

static inline s32
bfa_aen_get_ri(struct bfa_aen_s *aen)
{
	return aen->read_index;
}

static inline s32
bfa_aen_fetch_count(struct bfa_aen_s *aen, s32 read_index)
{
	return ((aen->write_index + aen->max_entry) - read_index)
		% aen->max_entry;
}

s32 bfa_aen_init(struct bfa_aen_s *aen, struct bfa_trc_mod_s *trcmod,
		void *bfad, u32 inst_id, void (*aen_cb_notify)(void *),
		void (*gettimeofday)(struct bfa_timeval_s *));

s32 bfa_aen_post(struct bfa_aen_s *aen, enum bfa_aen_category aen_category,
		     int aen_type, union bfa_aen_data_u *aen_data);

s32 bfa_aen_fetch(struct bfa_aen_s *aen, struct bfa_aen_entry_s *aen_entry,
		      s32 entry_space, s32 rii, s32 *ri_arr,
		      s32 ri_arr_cnt);

s32 bfa_aen_get_inst(struct bfa_aen_s *aen);

#endif /* __BFA_AEN_H__ */
