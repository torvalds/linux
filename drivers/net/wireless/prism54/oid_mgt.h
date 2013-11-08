/*
 *  Copyright (C) 2003 Aurelien Alleaume <slts@free.fr>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 *
 */

#if !defined(_OID_MGT_H)
#define _OID_MGT_H

#include "isl_oid.h"
#include "islpci_dev.h"

extern struct oid_t isl_oid[];

int mgt_init(islpci_private *);

void mgt_clean(islpci_private *);

/* I don't know where to put these 2 */
extern const int frequency_list_a[];
int channel_of_freq(int);

void mgt_le_to_cpu(int, void *);

int mgt_set_request(islpci_private *, enum oid_num_t, int, void *);
int mgt_set_varlen(islpci_private *, enum oid_num_t, void *, int);


int mgt_get_request(islpci_private *, enum oid_num_t, int, void *,
		    union oid_res_t *);

int mgt_commit_list(islpci_private *, enum oid_num_t *, int);

void mgt_set(islpci_private *, enum oid_num_t, void *);

void mgt_get(islpci_private *, enum oid_num_t, void *);

int mgt_commit(islpci_private *);

int mgt_mlme_answer(islpci_private *);

enum oid_num_t mgt_oidtonum(u32 oid);

int mgt_response_to_str(enum oid_num_t, union oid_res_t *, char *);

#endif				/* !defined(_OID_MGT_H) */
/* EOF */
