/*
 * This file is part of the Chelsio T4 Ethernet driver for Linux.
 *
 * Copyright (c) 2016 Chelsio Communications, Inc. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/module.h>
#include <linux/netdevice.h>

#include "cxgb4.h"
#include "sched.h"

/* Spinlock must be held by caller */
static int t4_sched_class_fw_cmd(struct port_info *pi,
				 struct ch_sched_params *p,
				 enum sched_fw_ops op)
{
	struct adapter *adap = pi->adapter;
	struct sched_table *s = pi->sched_tbl;
	struct sched_class *e;
	int err = 0;

	e = &s->tab[p->u.params.class];
	switch (op) {
	case SCHED_FW_OP_ADD:
		err = t4_sched_params(adap, p->type,
				      p->u.params.level, p->u.params.mode,
				      p->u.params.rateunit,
				      p->u.params.ratemode,
				      p->u.params.channel, e->idx,
				      p->u.params.minrate, p->u.params.maxrate,
				      p->u.params.weight, p->u.params.pktsize);
		break;
	default:
		err = -ENOTSUPP;
		break;
	}

	return err;
}

/* If @p is NULL, fetch any available unused class */
static struct sched_class *t4_sched_class_lookup(struct port_info *pi,
						const struct ch_sched_params *p)
{
	struct sched_table *s = pi->sched_tbl;
	struct sched_class *e, *end;
	struct sched_class *found = NULL;

	if (!p) {
		/* Get any available unused class */
		end = &s->tab[s->sched_size];
		for (e = &s->tab[0]; e != end; ++e) {
			if (e->state == SCHED_STATE_UNUSED) {
				found = e;
				break;
			}
		}
	} else {
		/* Look for a class with matching scheduling parameters */
		struct ch_sched_params info;
		struct ch_sched_params tp;

		memset(&info, 0, sizeof(info));
		memset(&tp, 0, sizeof(tp));

		memcpy(&tp, p, sizeof(tp));
		/* Don't try to match class parameter */
		tp.u.params.class = SCHED_CLS_NONE;

		end = &s->tab[s->sched_size];
		for (e = &s->tab[0]; e != end; ++e) {
			if (e->state == SCHED_STATE_UNUSED)
				continue;

			memset(&info, 0, sizeof(info));
			memcpy(&info, &e->info, sizeof(info));
			/* Don't try to match class parameter */
			info.u.params.class = SCHED_CLS_NONE;

			if ((info.type == tp.type) &&
			    (!memcmp(&info.u.params, &tp.u.params,
				     sizeof(info.u.params)))) {
				found = e;
				break;
			}
		}
	}

	return found;
}

static struct sched_class *t4_sched_class_alloc(struct port_info *pi,
						struct ch_sched_params *p)
{
	struct sched_table *s = pi->sched_tbl;
	struct sched_class *e;
	u8 class_id;
	int err;

	if (!p)
		return NULL;

	class_id = p->u.params.class;

	/* Only accept search for existing class with matching params
	 * or allocation of new class with specified params
	 */
	if (class_id != SCHED_CLS_NONE)
		return NULL;

	write_lock(&s->rw_lock);
	/* See if there's an exisiting class with same
	 * requested sched params
	 */
	e = t4_sched_class_lookup(pi, p);
	if (!e) {
		struct ch_sched_params np;

		/* Fetch any available unused class */
		e = t4_sched_class_lookup(pi, NULL);
		if (!e)
			goto out;

		memset(&np, 0, sizeof(np));
		memcpy(&np, p, sizeof(np));
		np.u.params.class = e->idx;

		spin_lock(&e->lock);
		/* New class */
		err = t4_sched_class_fw_cmd(pi, &np, SCHED_FW_OP_ADD);
		if (err) {
			spin_unlock(&e->lock);
			e = NULL;
			goto out;
		}
		memcpy(&e->info, &np, sizeof(e->info));
		atomic_set(&e->refcnt, 0);
		e->state = SCHED_STATE_ACTIVE;
		spin_unlock(&e->lock);
	}

out:
	write_unlock(&s->rw_lock);
	return e;
}

/**
 * cxgb4_sched_class_alloc - allocate a scheduling class
 * @dev: net_device pointer
 * @p: new scheduling class to create.
 *
 * Returns pointer to the scheduling class created.  If @p is NULL, then
 * it allocates and returns any available unused scheduling class. If a
 * scheduling class with matching @p is found, then the matching class is
 * returned.
 */
struct sched_class *cxgb4_sched_class_alloc(struct net_device *dev,
					    struct ch_sched_params *p)
{
	struct port_info *pi = netdev2pinfo(dev);
	u8 class_id;

	if (!can_sched(dev))
		return NULL;

	class_id = p->u.params.class;
	if (!valid_class_id(dev, class_id))
		return NULL;

	return t4_sched_class_alloc(pi, p);
}

struct sched_table *t4_init_sched(unsigned int sched_size)
{
	struct sched_table *s;
	unsigned int i;

	s = t4_alloc_mem(sizeof(*s) + sched_size * sizeof(struct sched_class));
	if (!s)
		return NULL;

	s->sched_size = sched_size;
	rwlock_init(&s->rw_lock);

	for (i = 0; i < s->sched_size; i++) {
		memset(&s->tab[i], 0, sizeof(struct sched_class));
		s->tab[i].idx = i;
		s->tab[i].state = SCHED_STATE_UNUSED;
		spin_lock_init(&s->tab[i].lock);
		atomic_set(&s->tab[i].refcnt, 0);
	}
	return s;
}

void t4_cleanup_sched(struct adapter *adap)
{
	struct sched_table *s;
	unsigned int i;

	for_each_port(adap, i) {
		struct port_info *pi = netdev2pinfo(adap->port[i]);

		s = pi->sched_tbl;
		t4_free_mem(s);
	}
}
