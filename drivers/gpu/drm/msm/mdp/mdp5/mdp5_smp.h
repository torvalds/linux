/*
 * Copyright (C) 2013 Red Hat
 * Author: Rob Clark <robdclark@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __MDP5_SMP_H__
#define __MDP5_SMP_H__

#include "msm_drv.h"

#define MAX_SMP_BLOCKS  22
#define SMP_BLK_SIZE    4096
#define SMP_ENTRIES_PER_BLK (SMP_BLK_SIZE / 16)

typedef DECLARE_BITMAP(mdp5_smp_state_t, MAX_SMP_BLOCKS);

struct mdp5_client_smp_state {
	mdp5_smp_state_t inuse;
	mdp5_smp_state_t pending;
};

struct mdp5_kms;

int mdp5_smp_request(struct mdp5_kms *mdp5_kms, enum mdp5_client_id cid, int nblks);
void mdp5_smp_configure(struct mdp5_kms *mdp5_kms, enum mdp5_client_id cid);
void mdp5_smp_commit(struct mdp5_kms *mdp5_kms, enum mdp5_client_id cid);


#endif /* __MDP5_SMP_H__ */
