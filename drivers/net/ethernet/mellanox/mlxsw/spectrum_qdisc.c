// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/netdevice.h>
#include <net/pkt_cls.h>
#include <net/red.h>

#include "spectrum.h"
#include "spectrum_span.h"
#include "reg.h"

#define MLXSW_SP_PRIO_BAND_TO_TCLASS(band) (IEEE_8021QAZ_MAX_TCS - band - 1)
#define MLXSW_SP_PRIO_CHILD_TO_TCLASS(child) \
	MLXSW_SP_PRIO_BAND_TO_TCLASS((child - 1))

enum mlxsw_sp_qdisc_type {
	MLXSW_SP_QDISC_NO_QDISC,
	MLXSW_SP_QDISC_RED,
	MLXSW_SP_QDISC_PRIO,
	MLXSW_SP_QDISC_ETS,
	MLXSW_SP_QDISC_TBF,
	MLXSW_SP_QDISC_FIFO,
};

struct mlxsw_sp_qdisc;

struct mlxsw_sp_qdisc_ops {
	enum mlxsw_sp_qdisc_type type;
	int (*check_params)(struct mlxsw_sp_port *mlxsw_sp_port,
			    void *params);
	int (*replace)(struct mlxsw_sp_port *mlxsw_sp_port, u32 handle,
		       struct mlxsw_sp_qdisc *mlxsw_sp_qdisc, void *params);
	int (*destroy)(struct mlxsw_sp_port *mlxsw_sp_port,
		       struct mlxsw_sp_qdisc *mlxsw_sp_qdisc);
	int (*get_stats)(struct mlxsw_sp_port *mlxsw_sp_port,
			 struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			 struct tc_qopt_offload_stats *stats_ptr);
	int (*get_xstats)(struct mlxsw_sp_port *mlxsw_sp_port,
			  struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			  void *xstats_ptr);
	void (*clean_stats)(struct mlxsw_sp_port *mlxsw_sp_port,
			    struct mlxsw_sp_qdisc *mlxsw_sp_qdisc);
	/* unoffload - to be used for a qdisc that stops being offloaded without
	 * being destroyed.
	 */
	void (*unoffload)(struct mlxsw_sp_port *mlxsw_sp_port,
			  struct mlxsw_sp_qdisc *mlxsw_sp_qdisc, void *params);
	struct mlxsw_sp_qdisc *(*find_class)(struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
					     u32 parent);
	unsigned int num_classes;

	u8 (*get_prio_bitmap)(struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			      struct mlxsw_sp_qdisc *child);
	int (*get_tclass_num)(struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			      struct mlxsw_sp_qdisc *child);
};

struct mlxsw_sp_qdisc_ets_band {
	u8 prio_bitmap;
	int tclass_num;
};

struct mlxsw_sp_qdisc_ets_data {
	struct mlxsw_sp_qdisc_ets_band bands[IEEE_8021QAZ_MAX_TCS];
};

struct mlxsw_sp_qdisc {
	u32 handle;
	union {
		struct red_stats red;
	} xstats_base;
	struct mlxsw_sp_qdisc_stats {
		u64 tx_bytes;
		u64 tx_packets;
		u64 drops;
		u64 overlimits;
		u64 backlog;
	} stats_base;

	union {
		struct mlxsw_sp_qdisc_ets_data *ets_data;
	};

	struct mlxsw_sp_qdisc_ops *ops;
	struct mlxsw_sp_qdisc *parent;
	struct mlxsw_sp_qdisc *qdiscs;
	unsigned int num_classes;
};

struct mlxsw_sp_qdisc_state {
	struct mlxsw_sp_qdisc root_qdisc;

	/* When a PRIO or ETS are added, the invisible FIFOs in their bands are
	 * created first. When notifications for these FIFOs arrive, it is not
	 * known what qdisc their parent handle refers to. It could be a
	 * newly-created PRIO that will replace the currently-offloaded one, or
	 * it could be e.g. a RED that will be attached below it.
	 *
	 * As the notifications start to arrive, use them to note what the
	 * future parent handle is, and keep track of which child FIFOs were
	 * seen. Then when the parent is known, retroactively offload those
	 * FIFOs.
	 */
	u32 future_handle;
	bool future_fifos[IEEE_8021QAZ_MAX_TCS];
	struct mutex lock; /* Protects qdisc state. */
};

static bool
mlxsw_sp_qdisc_compare(struct mlxsw_sp_qdisc *mlxsw_sp_qdisc, u32 handle)
{
	return mlxsw_sp_qdisc->ops && mlxsw_sp_qdisc->handle == handle;
}

static struct mlxsw_sp_qdisc *
mlxsw_sp_qdisc_walk(struct mlxsw_sp_qdisc *qdisc,
		    struct mlxsw_sp_qdisc *(*pre)(struct mlxsw_sp_qdisc *,
						  void *),
		    void *data)
{
	struct mlxsw_sp_qdisc *tmp;
	unsigned int i;

	if (pre) {
		tmp = pre(qdisc, data);
		if (tmp)
			return tmp;
	}

	if (qdisc->ops) {
		for (i = 0; i < qdisc->num_classes; i++) {
			tmp = &qdisc->qdiscs[i];
			if (qdisc->ops) {
				tmp = mlxsw_sp_qdisc_walk(tmp, pre, data);
				if (tmp)
					return tmp;
			}
		}
	}

	return NULL;
}

static struct mlxsw_sp_qdisc *
mlxsw_sp_qdisc_walk_cb_find(struct mlxsw_sp_qdisc *qdisc, void *data)
{
	u32 parent = *(u32 *)data;

	if (qdisc->ops && TC_H_MAJ(qdisc->handle) == TC_H_MAJ(parent)) {
		if (qdisc->ops->find_class)
			return qdisc->ops->find_class(qdisc, parent);
	}

	return NULL;
}

static struct mlxsw_sp_qdisc *
mlxsw_sp_qdisc_find(struct mlxsw_sp_port *mlxsw_sp_port, u32 parent)
{
	struct mlxsw_sp_qdisc_state *qdisc_state = mlxsw_sp_port->qdisc;

	if (!qdisc_state)
		return NULL;
	if (parent == TC_H_ROOT)
		return &qdisc_state->root_qdisc;
	return mlxsw_sp_qdisc_walk(&qdisc_state->root_qdisc,
				   mlxsw_sp_qdisc_walk_cb_find, &parent);
}

static struct mlxsw_sp_qdisc *
mlxsw_sp_qdisc_walk_cb_find_by_handle(struct mlxsw_sp_qdisc *qdisc, void *data)
{
	u32 handle = *(u32 *)data;

	if (qdisc->ops && qdisc->handle == handle)
		return qdisc;
	return NULL;
}

static struct mlxsw_sp_qdisc *
mlxsw_sp_qdisc_find_by_handle(struct mlxsw_sp_port *mlxsw_sp_port, u32 handle)
{
	struct mlxsw_sp_qdisc_state *qdisc_state = mlxsw_sp_port->qdisc;

	if (!qdisc_state)
		return NULL;
	return mlxsw_sp_qdisc_walk(&qdisc_state->root_qdisc,
				   mlxsw_sp_qdisc_walk_cb_find_by_handle,
				   &handle);
}

static void
mlxsw_sp_qdisc_reduce_parent_backlog(struct mlxsw_sp_qdisc *mlxsw_sp_qdisc)
{
	struct mlxsw_sp_qdisc *tmp;

	for (tmp = mlxsw_sp_qdisc->parent; tmp; tmp = tmp->parent)
		tmp->stats_base.backlog -= mlxsw_sp_qdisc->stats_base.backlog;
}

static u8 mlxsw_sp_qdisc_get_prio_bitmap(struct mlxsw_sp_port *mlxsw_sp_port,
					 struct mlxsw_sp_qdisc *mlxsw_sp_qdisc)
{
	struct mlxsw_sp_qdisc *parent = mlxsw_sp_qdisc->parent;

	if (!parent)
		return 0xff;
	return parent->ops->get_prio_bitmap(parent, mlxsw_sp_qdisc);
}

#define MLXSW_SP_PORT_DEFAULT_TCLASS 0

static int mlxsw_sp_qdisc_get_tclass_num(struct mlxsw_sp_port *mlxsw_sp_port,
					 struct mlxsw_sp_qdisc *mlxsw_sp_qdisc)
{
	struct mlxsw_sp_qdisc *parent = mlxsw_sp_qdisc->parent;

	if (!parent)
		return MLXSW_SP_PORT_DEFAULT_TCLASS;
	return parent->ops->get_tclass_num(parent, mlxsw_sp_qdisc);
}

static int
mlxsw_sp_qdisc_destroy(struct mlxsw_sp_port *mlxsw_sp_port,
		       struct mlxsw_sp_qdisc *mlxsw_sp_qdisc)
{
	struct mlxsw_sp_qdisc *root_qdisc = &mlxsw_sp_port->qdisc->root_qdisc;
	int err_hdroom = 0;
	int err = 0;
	int i;

	if (!mlxsw_sp_qdisc)
		return 0;

	if (root_qdisc == mlxsw_sp_qdisc) {
		struct mlxsw_sp_hdroom hdroom = *mlxsw_sp_port->hdroom;

		hdroom.mode = MLXSW_SP_HDROOM_MODE_DCB;
		mlxsw_sp_hdroom_prios_reset_buf_idx(&hdroom);
		mlxsw_sp_hdroom_bufs_reset_lossiness(&hdroom);
		mlxsw_sp_hdroom_bufs_reset_sizes(mlxsw_sp_port, &hdroom);
		err_hdroom = mlxsw_sp_hdroom_configure(mlxsw_sp_port, &hdroom);
	}

	if (!mlxsw_sp_qdisc->ops)
		return 0;

	for (i = 0; i < mlxsw_sp_qdisc->num_classes; i++)
		mlxsw_sp_qdisc_destroy(mlxsw_sp_port,
				       &mlxsw_sp_qdisc->qdiscs[i]);
	mlxsw_sp_qdisc_reduce_parent_backlog(mlxsw_sp_qdisc);
	if (mlxsw_sp_qdisc->ops->destroy)
		err = mlxsw_sp_qdisc->ops->destroy(mlxsw_sp_port,
						   mlxsw_sp_qdisc);
	if (mlxsw_sp_qdisc->ops->clean_stats)
		mlxsw_sp_qdisc->ops->clean_stats(mlxsw_sp_port, mlxsw_sp_qdisc);

	mlxsw_sp_qdisc->handle = TC_H_UNSPEC;
	mlxsw_sp_qdisc->ops = NULL;
	mlxsw_sp_qdisc->num_classes = 0;
	kfree(mlxsw_sp_qdisc->qdiscs);
	mlxsw_sp_qdisc->qdiscs = NULL;
	return err_hdroom ?: err;
}

struct mlxsw_sp_qdisc_tree_validate {
	bool forbid_ets;
	bool forbid_tbf;
	bool forbid_red;
};

static int
__mlxsw_sp_qdisc_tree_validate(struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			       struct mlxsw_sp_qdisc_tree_validate validate);

static int
mlxsw_sp_qdisc_tree_validate_children(struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
				      struct mlxsw_sp_qdisc_tree_validate validate)
{
	unsigned int i;
	int err;

	for (i = 0; i < mlxsw_sp_qdisc->num_classes; i++) {
		err = __mlxsw_sp_qdisc_tree_validate(&mlxsw_sp_qdisc->qdiscs[i],
						     validate);
		if (err)
			return err;
	}

	return 0;
}

static int
__mlxsw_sp_qdisc_tree_validate(struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			       struct mlxsw_sp_qdisc_tree_validate validate)
{
	if (!mlxsw_sp_qdisc->ops)
		return 0;

	switch (mlxsw_sp_qdisc->ops->type) {
	case MLXSW_SP_QDISC_FIFO:
		break;
	case MLXSW_SP_QDISC_RED:
		if (validate.forbid_red)
			return -EINVAL;
		validate.forbid_red = true;
		validate.forbid_ets = true;
		break;
	case MLXSW_SP_QDISC_TBF:
		if (validate.forbid_tbf)
			return -EINVAL;
		validate.forbid_tbf = true;
		validate.forbid_ets = true;
		break;
	case MLXSW_SP_QDISC_PRIO:
	case MLXSW_SP_QDISC_ETS:
		if (validate.forbid_ets)
			return -EINVAL;
		validate.forbid_ets = true;
		break;
	default:
		WARN_ON(1);
		return -EINVAL;
	}

	return mlxsw_sp_qdisc_tree_validate_children(mlxsw_sp_qdisc, validate);
}

static int mlxsw_sp_qdisc_tree_validate(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp_qdisc_tree_validate validate = {};
	struct mlxsw_sp_qdisc *mlxsw_sp_qdisc;

	mlxsw_sp_qdisc = &mlxsw_sp_port->qdisc->root_qdisc;
	return __mlxsw_sp_qdisc_tree_validate(mlxsw_sp_qdisc, validate);
}

static int mlxsw_sp_qdisc_create(struct mlxsw_sp_port *mlxsw_sp_port,
				 u32 handle,
				 struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
				 struct mlxsw_sp_qdisc_ops *ops, void *params)
{
	struct mlxsw_sp_qdisc *root_qdisc = &mlxsw_sp_port->qdisc->root_qdisc;
	struct mlxsw_sp_hdroom orig_hdroom;
	unsigned int i;
	int err;

	err = ops->check_params(mlxsw_sp_port, params);
	if (err)
		return err;

	if (ops->num_classes) {
		mlxsw_sp_qdisc->qdiscs = kcalloc(ops->num_classes,
						 sizeof(*mlxsw_sp_qdisc->qdiscs),
						 GFP_KERNEL);
		if (!mlxsw_sp_qdisc->qdiscs)
			return -ENOMEM;

		for (i = 0; i < ops->num_classes; i++)
			mlxsw_sp_qdisc->qdiscs[i].parent = mlxsw_sp_qdisc;
	}

	orig_hdroom = *mlxsw_sp_port->hdroom;
	if (root_qdisc == mlxsw_sp_qdisc) {
		struct mlxsw_sp_hdroom hdroom = orig_hdroom;

		hdroom.mode = MLXSW_SP_HDROOM_MODE_TC;
		mlxsw_sp_hdroom_prios_reset_buf_idx(&hdroom);
		mlxsw_sp_hdroom_bufs_reset_lossiness(&hdroom);
		mlxsw_sp_hdroom_bufs_reset_sizes(mlxsw_sp_port, &hdroom);

		err = mlxsw_sp_hdroom_configure(mlxsw_sp_port, &hdroom);
		if (err)
			goto err_hdroom_configure;
	}

	mlxsw_sp_qdisc->num_classes = ops->num_classes;
	mlxsw_sp_qdisc->ops = ops;
	mlxsw_sp_qdisc->handle = handle;
	err = mlxsw_sp_qdisc_tree_validate(mlxsw_sp_port);
	if (err)
		goto err_replace;

	err = ops->replace(mlxsw_sp_port, handle, mlxsw_sp_qdisc, params);
	if (err)
		goto err_replace;

	return 0;

err_replace:
	mlxsw_sp_qdisc->handle = TC_H_UNSPEC;
	mlxsw_sp_qdisc->ops = NULL;
	mlxsw_sp_qdisc->num_classes = 0;
	mlxsw_sp_hdroom_configure(mlxsw_sp_port, &orig_hdroom);
err_hdroom_configure:
	kfree(mlxsw_sp_qdisc->qdiscs);
	mlxsw_sp_qdisc->qdiscs = NULL;
	return err;
}

static int
mlxsw_sp_qdisc_change(struct mlxsw_sp_port *mlxsw_sp_port, u32 handle,
		      struct mlxsw_sp_qdisc *mlxsw_sp_qdisc, void *params)
{
	struct mlxsw_sp_qdisc_ops *ops = mlxsw_sp_qdisc->ops;
	int err;

	err = ops->check_params(mlxsw_sp_port, params);
	if (err)
		goto unoffload;

	err = ops->replace(mlxsw_sp_port, handle, mlxsw_sp_qdisc, params);
	if (err)
		goto unoffload;

	/* Check if the Qdisc changed. That includes a situation where an
	 * invisible Qdisc replaces another one, or is being added for the
	 * first time.
	 */
	if (mlxsw_sp_qdisc->handle != handle) {
		if (ops->clean_stats)
			ops->clean_stats(mlxsw_sp_port, mlxsw_sp_qdisc);
	}

	mlxsw_sp_qdisc->handle = handle;
	return 0;

unoffload:
	if (ops->unoffload)
		ops->unoffload(mlxsw_sp_port, mlxsw_sp_qdisc, params);

	mlxsw_sp_qdisc_destroy(mlxsw_sp_port, mlxsw_sp_qdisc);
	return err;
}

static int
mlxsw_sp_qdisc_replace(struct mlxsw_sp_port *mlxsw_sp_port, u32 handle,
		       struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
		       struct mlxsw_sp_qdisc_ops *ops, void *params)
{
	if (mlxsw_sp_qdisc->ops && mlxsw_sp_qdisc->ops->type != ops->type)
		/* In case this location contained a different qdisc of the
		 * same type we can override the old qdisc configuration.
		 * Otherwise, we need to remove the old qdisc before setting the
		 * new one.
		 */
		mlxsw_sp_qdisc_destroy(mlxsw_sp_port, mlxsw_sp_qdisc);

	if (!mlxsw_sp_qdisc->ops)
		return mlxsw_sp_qdisc_create(mlxsw_sp_port, handle,
					     mlxsw_sp_qdisc, ops, params);
	else
		return mlxsw_sp_qdisc_change(mlxsw_sp_port, handle,
					     mlxsw_sp_qdisc, params);
}

static int
mlxsw_sp_qdisc_get_stats(struct mlxsw_sp_port *mlxsw_sp_port,
			 struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			 struct tc_qopt_offload_stats *stats_ptr)
{
	if (mlxsw_sp_qdisc && mlxsw_sp_qdisc->ops &&
	    mlxsw_sp_qdisc->ops->get_stats)
		return mlxsw_sp_qdisc->ops->get_stats(mlxsw_sp_port,
						      mlxsw_sp_qdisc,
						      stats_ptr);

	return -EOPNOTSUPP;
}

static int
mlxsw_sp_qdisc_get_xstats(struct mlxsw_sp_port *mlxsw_sp_port,
			  struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			  void *xstats_ptr)
{
	if (mlxsw_sp_qdisc && mlxsw_sp_qdisc->ops &&
	    mlxsw_sp_qdisc->ops->get_xstats)
		return mlxsw_sp_qdisc->ops->get_xstats(mlxsw_sp_port,
						      mlxsw_sp_qdisc,
						      xstats_ptr);

	return -EOPNOTSUPP;
}

static u64
mlxsw_sp_xstats_backlog(struct mlxsw_sp_port_xstats *xstats, int tclass_num)
{
	return xstats->backlog[tclass_num] +
	       xstats->backlog[tclass_num + 8];
}

static u64
mlxsw_sp_xstats_tail_drop(struct mlxsw_sp_port_xstats *xstats, int tclass_num)
{
	return xstats->tail_drop[tclass_num] +
	       xstats->tail_drop[tclass_num + 8];
}

static void
mlxsw_sp_qdisc_bstats_per_priority_get(struct mlxsw_sp_port_xstats *xstats,
				       u8 prio_bitmap, u64 *tx_packets,
				       u64 *tx_bytes)
{
	int i;

	*tx_packets = 0;
	*tx_bytes = 0;
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		if (prio_bitmap & BIT(i)) {
			*tx_packets += xstats->tx_packets[i];
			*tx_bytes += xstats->tx_bytes[i];
		}
	}
}

static void
mlxsw_sp_qdisc_collect_tc_stats(struct mlxsw_sp_port *mlxsw_sp_port,
				struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
				u64 *p_tx_bytes, u64 *p_tx_packets,
				u64 *p_drops, u64 *p_backlog)
{
	struct mlxsw_sp_port_xstats *xstats;
	u64 tx_bytes, tx_packets;
	u8 prio_bitmap;
	int tclass_num;

	prio_bitmap = mlxsw_sp_qdisc_get_prio_bitmap(mlxsw_sp_port,
						     mlxsw_sp_qdisc);
	tclass_num = mlxsw_sp_qdisc_get_tclass_num(mlxsw_sp_port,
						   mlxsw_sp_qdisc);
	xstats = &mlxsw_sp_port->periodic_hw_stats.xstats;
	mlxsw_sp_qdisc_bstats_per_priority_get(xstats, prio_bitmap,
					       &tx_packets, &tx_bytes);

	*p_tx_packets += tx_packets;
	*p_tx_bytes += tx_bytes;
	*p_drops += xstats->wred_drop[tclass_num] +
		    mlxsw_sp_xstats_tail_drop(xstats, tclass_num);
	*p_backlog += mlxsw_sp_xstats_backlog(xstats, tclass_num);
}

static void
mlxsw_sp_qdisc_update_stats(struct mlxsw_sp *mlxsw_sp,
			    struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			    u64 tx_bytes, u64 tx_packets,
			    u64 drops, u64 backlog,
			    struct tc_qopt_offload_stats *stats_ptr)
{
	struct mlxsw_sp_qdisc_stats *stats_base = &mlxsw_sp_qdisc->stats_base;

	tx_bytes -= stats_base->tx_bytes;
	tx_packets -= stats_base->tx_packets;
	drops -= stats_base->drops;
	backlog -= stats_base->backlog;

	_bstats_update(stats_ptr->bstats, tx_bytes, tx_packets);
	stats_ptr->qstats->drops += drops;
	stats_ptr->qstats->backlog += mlxsw_sp_cells_bytes(mlxsw_sp, backlog);

	stats_base->backlog += backlog;
	stats_base->drops += drops;
	stats_base->tx_bytes += tx_bytes;
	stats_base->tx_packets += tx_packets;
}

static void
mlxsw_sp_qdisc_get_tc_stats(struct mlxsw_sp_port *mlxsw_sp_port,
			    struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			    struct tc_qopt_offload_stats *stats_ptr)
{
	u64 tx_packets = 0;
	u64 tx_bytes = 0;
	u64 backlog = 0;
	u64 drops = 0;

	mlxsw_sp_qdisc_collect_tc_stats(mlxsw_sp_port, mlxsw_sp_qdisc,
					&tx_bytes, &tx_packets,
					&drops, &backlog);
	mlxsw_sp_qdisc_update_stats(mlxsw_sp_port->mlxsw_sp, mlxsw_sp_qdisc,
				    tx_bytes, tx_packets, drops, backlog,
				    stats_ptr);
}

static int
mlxsw_sp_tclass_congestion_enable(struct mlxsw_sp_port *mlxsw_sp_port,
				  int tclass_num, u32 min, u32 max,
				  u32 probability, bool is_wred, bool is_ecn)
{
	char cwtpm_cmd[MLXSW_REG_CWTPM_LEN];
	char cwtp_cmd[MLXSW_REG_CWTP_LEN];
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	int err;

	mlxsw_reg_cwtp_pack(cwtp_cmd, mlxsw_sp_port->local_port, tclass_num);
	mlxsw_reg_cwtp_profile_pack(cwtp_cmd, MLXSW_REG_CWTP_DEFAULT_PROFILE,
				    roundup(min, MLXSW_REG_CWTP_MIN_VALUE),
				    roundup(max, MLXSW_REG_CWTP_MIN_VALUE),
				    probability);

	err = mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(cwtp), cwtp_cmd);
	if (err)
		return err;

	mlxsw_reg_cwtpm_pack(cwtpm_cmd, mlxsw_sp_port->local_port, tclass_num,
			     MLXSW_REG_CWTP_DEFAULT_PROFILE, is_wred, is_ecn);

	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(cwtpm), cwtpm_cmd);
}

static int
mlxsw_sp_tclass_congestion_disable(struct mlxsw_sp_port *mlxsw_sp_port,
				   int tclass_num)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	char cwtpm_cmd[MLXSW_REG_CWTPM_LEN];

	mlxsw_reg_cwtpm_pack(cwtpm_cmd, mlxsw_sp_port->local_port, tclass_num,
			     MLXSW_REG_CWTPM_RESET_PROFILE, false, false);
	return mlxsw_reg_write(mlxsw_sp->core, MLXSW_REG(cwtpm), cwtpm_cmd);
}

static void
mlxsw_sp_setup_tc_qdisc_red_clean_stats(struct mlxsw_sp_port *mlxsw_sp_port,
					struct mlxsw_sp_qdisc *mlxsw_sp_qdisc)
{
	struct mlxsw_sp_qdisc_stats *stats_base;
	struct mlxsw_sp_port_xstats *xstats;
	struct red_stats *red_base;
	u8 prio_bitmap;
	int tclass_num;

	prio_bitmap = mlxsw_sp_qdisc_get_prio_bitmap(mlxsw_sp_port,
						     mlxsw_sp_qdisc);
	tclass_num = mlxsw_sp_qdisc_get_tclass_num(mlxsw_sp_port,
						   mlxsw_sp_qdisc);
	xstats = &mlxsw_sp_port->periodic_hw_stats.xstats;
	stats_base = &mlxsw_sp_qdisc->stats_base;
	red_base = &mlxsw_sp_qdisc->xstats_base.red;

	mlxsw_sp_qdisc_bstats_per_priority_get(xstats, prio_bitmap,
					       &stats_base->tx_packets,
					       &stats_base->tx_bytes);
	red_base->prob_mark = xstats->tc_ecn[tclass_num];
	red_base->prob_drop = xstats->wred_drop[tclass_num];
	red_base->pdrop = mlxsw_sp_xstats_tail_drop(xstats, tclass_num);

	stats_base->overlimits = red_base->prob_drop + red_base->prob_mark;
	stats_base->drops = red_base->prob_drop + red_base->pdrop;

	stats_base->backlog = 0;
}

static int
mlxsw_sp_qdisc_red_destroy(struct mlxsw_sp_port *mlxsw_sp_port,
			   struct mlxsw_sp_qdisc *mlxsw_sp_qdisc)
{
	int tclass_num = mlxsw_sp_qdisc_get_tclass_num(mlxsw_sp_port,
						       mlxsw_sp_qdisc);

	return mlxsw_sp_tclass_congestion_disable(mlxsw_sp_port, tclass_num);
}

static int
mlxsw_sp_qdisc_red_check_params(struct mlxsw_sp_port *mlxsw_sp_port,
				void *params)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct tc_red_qopt_offload_params *p = params;

	if (p->min > p->max) {
		dev_err(mlxsw_sp->bus_info->dev,
			"spectrum: RED: min %u is bigger then max %u\n", p->min,
			p->max);
		return -EINVAL;
	}
	if (p->max > MLXSW_CORE_RES_GET(mlxsw_sp->core,
					GUARANTEED_SHARED_BUFFER)) {
		dev_err(mlxsw_sp->bus_info->dev,
			"spectrum: RED: max value %u is too big\n", p->max);
		return -EINVAL;
	}
	if (p->min == 0 || p->max == 0) {
		dev_err(mlxsw_sp->bus_info->dev,
			"spectrum: RED: 0 value is illegal for min and max\n");
		return -EINVAL;
	}
	return 0;
}

static int
mlxsw_sp_qdisc_red_replace(struct mlxsw_sp_port *mlxsw_sp_port, u32 handle,
			   struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			   void *params)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct tc_red_qopt_offload_params *p = params;
	int tclass_num;
	u32 min, max;
	u64 prob;

	tclass_num = mlxsw_sp_qdisc_get_tclass_num(mlxsw_sp_port,
						   mlxsw_sp_qdisc);

	/* calculate probability in percentage */
	prob = p->probability;
	prob *= 100;
	prob = DIV_ROUND_UP(prob, 1 << 16);
	prob = DIV_ROUND_UP(prob, 1 << 16);
	min = mlxsw_sp_bytes_cells(mlxsw_sp, p->min);
	max = mlxsw_sp_bytes_cells(mlxsw_sp, p->max);
	return mlxsw_sp_tclass_congestion_enable(mlxsw_sp_port, tclass_num,
						 min, max, prob,
						 !p->is_nodrop, p->is_ecn);
}

static void
mlxsw_sp_qdisc_leaf_unoffload(struct mlxsw_sp_port *mlxsw_sp_port,
			      struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			      struct gnet_stats_queue *qstats)
{
	u64 backlog;

	backlog = mlxsw_sp_cells_bytes(mlxsw_sp_port->mlxsw_sp,
				       mlxsw_sp_qdisc->stats_base.backlog);
	qstats->backlog -= backlog;
	mlxsw_sp_qdisc->stats_base.backlog = 0;
}

static void
mlxsw_sp_qdisc_red_unoffload(struct mlxsw_sp_port *mlxsw_sp_port,
			     struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			     void *params)
{
	struct tc_red_qopt_offload_params *p = params;

	mlxsw_sp_qdisc_leaf_unoffload(mlxsw_sp_port, mlxsw_sp_qdisc, p->qstats);
}

static int
mlxsw_sp_qdisc_get_red_xstats(struct mlxsw_sp_port *mlxsw_sp_port,
			      struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			      void *xstats_ptr)
{
	struct red_stats *xstats_base = &mlxsw_sp_qdisc->xstats_base.red;
	struct mlxsw_sp_port_xstats *xstats;
	struct red_stats *res = xstats_ptr;
	int early_drops, marks, pdrops;
	int tclass_num;

	tclass_num = mlxsw_sp_qdisc_get_tclass_num(mlxsw_sp_port,
						   mlxsw_sp_qdisc);
	xstats = &mlxsw_sp_port->periodic_hw_stats.xstats;

	early_drops = xstats->wred_drop[tclass_num] - xstats_base->prob_drop;
	marks = xstats->tc_ecn[tclass_num] - xstats_base->prob_mark;
	pdrops = mlxsw_sp_xstats_tail_drop(xstats, tclass_num) -
		 xstats_base->pdrop;

	res->pdrop += pdrops;
	res->prob_drop += early_drops;
	res->prob_mark += marks;

	xstats_base->pdrop += pdrops;
	xstats_base->prob_drop += early_drops;
	xstats_base->prob_mark += marks;
	return 0;
}

static int
mlxsw_sp_qdisc_get_red_stats(struct mlxsw_sp_port *mlxsw_sp_port,
			     struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			     struct tc_qopt_offload_stats *stats_ptr)
{
	struct mlxsw_sp_qdisc_stats *stats_base;
	struct mlxsw_sp_port_xstats *xstats;
	u64 overlimits;
	int tclass_num;

	tclass_num = mlxsw_sp_qdisc_get_tclass_num(mlxsw_sp_port,
						   mlxsw_sp_qdisc);
	xstats = &mlxsw_sp_port->periodic_hw_stats.xstats;
	stats_base = &mlxsw_sp_qdisc->stats_base;

	mlxsw_sp_qdisc_get_tc_stats(mlxsw_sp_port, mlxsw_sp_qdisc, stats_ptr);
	overlimits = xstats->wred_drop[tclass_num] +
		     xstats->tc_ecn[tclass_num] - stats_base->overlimits;

	stats_ptr->qstats->overlimits += overlimits;
	stats_base->overlimits += overlimits;

	return 0;
}

static struct mlxsw_sp_qdisc *
mlxsw_sp_qdisc_leaf_find_class(struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			       u32 parent)
{
	return NULL;
}

static struct mlxsw_sp_qdisc_ops mlxsw_sp_qdisc_ops_red = {
	.type = MLXSW_SP_QDISC_RED,
	.check_params = mlxsw_sp_qdisc_red_check_params,
	.replace = mlxsw_sp_qdisc_red_replace,
	.unoffload = mlxsw_sp_qdisc_red_unoffload,
	.destroy = mlxsw_sp_qdisc_red_destroy,
	.get_stats = mlxsw_sp_qdisc_get_red_stats,
	.get_xstats = mlxsw_sp_qdisc_get_red_xstats,
	.clean_stats = mlxsw_sp_setup_tc_qdisc_red_clean_stats,
	.find_class = mlxsw_sp_qdisc_leaf_find_class,
};

static int mlxsw_sp_qdisc_graft(struct mlxsw_sp_port *mlxsw_sp_port,
				struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
				u8 band, u32 child_handle);

static int __mlxsw_sp_setup_tc_red(struct mlxsw_sp_port *mlxsw_sp_port,
				   struct tc_red_qopt_offload *p)
{
	struct mlxsw_sp_qdisc *mlxsw_sp_qdisc;

	mlxsw_sp_qdisc = mlxsw_sp_qdisc_find(mlxsw_sp_port, p->parent);
	if (!mlxsw_sp_qdisc)
		return -EOPNOTSUPP;

	if (p->command == TC_RED_REPLACE)
		return mlxsw_sp_qdisc_replace(mlxsw_sp_port, p->handle,
					      mlxsw_sp_qdisc,
					      &mlxsw_sp_qdisc_ops_red,
					      &p->set);

	if (!mlxsw_sp_qdisc_compare(mlxsw_sp_qdisc, p->handle))
		return -EOPNOTSUPP;

	switch (p->command) {
	case TC_RED_DESTROY:
		return mlxsw_sp_qdisc_destroy(mlxsw_sp_port, mlxsw_sp_qdisc);
	case TC_RED_XSTATS:
		return mlxsw_sp_qdisc_get_xstats(mlxsw_sp_port, mlxsw_sp_qdisc,
						 p->xstats);
	case TC_RED_STATS:
		return mlxsw_sp_qdisc_get_stats(mlxsw_sp_port, mlxsw_sp_qdisc,
						&p->stats);
	case TC_RED_GRAFT:
		return mlxsw_sp_qdisc_graft(mlxsw_sp_port, mlxsw_sp_qdisc, 0,
					    p->child_handle);
	default:
		return -EOPNOTSUPP;
	}
}

int mlxsw_sp_setup_tc_red(struct mlxsw_sp_port *mlxsw_sp_port,
			  struct tc_red_qopt_offload *p)
{
	int err;

	mutex_lock(&mlxsw_sp_port->qdisc->lock);
	err = __mlxsw_sp_setup_tc_red(mlxsw_sp_port, p);
	mutex_unlock(&mlxsw_sp_port->qdisc->lock);

	return err;
}

static void
mlxsw_sp_setup_tc_qdisc_leaf_clean_stats(struct mlxsw_sp_port *mlxsw_sp_port,
					 struct mlxsw_sp_qdisc *mlxsw_sp_qdisc)
{
	u64 backlog_cells = 0;
	u64 tx_packets = 0;
	u64 tx_bytes = 0;
	u64 drops = 0;

	mlxsw_sp_qdisc_collect_tc_stats(mlxsw_sp_port, mlxsw_sp_qdisc,
					&tx_bytes, &tx_packets,
					&drops, &backlog_cells);

	mlxsw_sp_qdisc->stats_base.tx_packets = tx_packets;
	mlxsw_sp_qdisc->stats_base.tx_bytes = tx_bytes;
	mlxsw_sp_qdisc->stats_base.drops = drops;
	mlxsw_sp_qdisc->stats_base.backlog = 0;
}

static int
mlxsw_sp_qdisc_tbf_destroy(struct mlxsw_sp_port *mlxsw_sp_port,
			   struct mlxsw_sp_qdisc *mlxsw_sp_qdisc)
{
	int tclass_num = mlxsw_sp_qdisc_get_tclass_num(mlxsw_sp_port,
						       mlxsw_sp_qdisc);

	return mlxsw_sp_port_ets_maxrate_set(mlxsw_sp_port,
					     MLXSW_REG_QEEC_HR_SUBGROUP,
					     tclass_num, 0,
					     MLXSW_REG_QEEC_MAS_DIS, 0);
}

static int
mlxsw_sp_qdisc_tbf_bs(struct mlxsw_sp_port *mlxsw_sp_port,
		      u32 max_size, u8 *p_burst_size)
{
	/* TBF burst size is configured in bytes. The ASIC burst size value is
	 * ((2 ^ bs) * 512 bits. Convert the TBF bytes to 512-bit units.
	 */
	u32 bs512 = max_size / 64;
	u8 bs = fls(bs512);

	if (!bs)
		return -EINVAL;
	--bs;

	/* Demand a power of two. */
	if ((1 << bs) != bs512)
		return -EINVAL;

	if (bs < mlxsw_sp_port->mlxsw_sp->lowest_shaper_bs ||
	    bs > MLXSW_REG_QEEC_HIGHEST_SHAPER_BS)
		return -EINVAL;

	*p_burst_size = bs;
	return 0;
}

static u32
mlxsw_sp_qdisc_tbf_max_size(u8 bs)
{
	return (1U << bs) * 64;
}

static u64
mlxsw_sp_qdisc_tbf_rate_kbps(struct tc_tbf_qopt_offload_replace_params *p)
{
	/* TBF interface is in bytes/s, whereas Spectrum ASIC is configured in
	 * Kbits/s.
	 */
	return div_u64(p->rate.rate_bytes_ps, 1000) * 8;
}

static int
mlxsw_sp_qdisc_tbf_check_params(struct mlxsw_sp_port *mlxsw_sp_port,
				void *params)
{
	struct tc_tbf_qopt_offload_replace_params *p = params;
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	u64 rate_kbps = mlxsw_sp_qdisc_tbf_rate_kbps(p);
	u8 burst_size;
	int err;

	if (rate_kbps >= MLXSW_REG_QEEC_MAS_DIS) {
		dev_err(mlxsw_sp_port->mlxsw_sp->bus_info->dev,
			"spectrum: TBF: rate of %lluKbps must be below %u\n",
			rate_kbps, MLXSW_REG_QEEC_MAS_DIS);
		return -EINVAL;
	}

	err = mlxsw_sp_qdisc_tbf_bs(mlxsw_sp_port, p->max_size, &burst_size);
	if (err) {
		u8 highest_shaper_bs = MLXSW_REG_QEEC_HIGHEST_SHAPER_BS;

		dev_err(mlxsw_sp->bus_info->dev,
			"spectrum: TBF: invalid burst size of %u, must be a power of two between %u and %u",
			p->max_size,
			mlxsw_sp_qdisc_tbf_max_size(mlxsw_sp->lowest_shaper_bs),
			mlxsw_sp_qdisc_tbf_max_size(highest_shaper_bs));
		return -EINVAL;
	}

	return 0;
}

static int
mlxsw_sp_qdisc_tbf_replace(struct mlxsw_sp_port *mlxsw_sp_port, u32 handle,
			   struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			   void *params)
{
	struct tc_tbf_qopt_offload_replace_params *p = params;
	u64 rate_kbps = mlxsw_sp_qdisc_tbf_rate_kbps(p);
	int tclass_num;
	u8 burst_size;
	int err;

	tclass_num = mlxsw_sp_qdisc_get_tclass_num(mlxsw_sp_port,
						   mlxsw_sp_qdisc);

	err = mlxsw_sp_qdisc_tbf_bs(mlxsw_sp_port, p->max_size, &burst_size);
	if (WARN_ON_ONCE(err))
		/* check_params above was supposed to reject this value. */
		return -EINVAL;

	/* Configure subgroup shaper, so that both UC and MC traffic is subject
	 * to shaping. That is unlike RED, however UC queue lengths are going to
	 * be different than MC ones due to different pool and quota
	 * configurations, so the configuration is not applicable. For shaper on
	 * the other hand, subjecting the overall stream to the configured
	 * shaper makes sense. Also note that that is what we do for
	 * ieee_setmaxrate().
	 */
	return mlxsw_sp_port_ets_maxrate_set(mlxsw_sp_port,
					     MLXSW_REG_QEEC_HR_SUBGROUP,
					     tclass_num, 0,
					     rate_kbps, burst_size);
}

static void
mlxsw_sp_qdisc_tbf_unoffload(struct mlxsw_sp_port *mlxsw_sp_port,
			     struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			     void *params)
{
	struct tc_tbf_qopt_offload_replace_params *p = params;

	mlxsw_sp_qdisc_leaf_unoffload(mlxsw_sp_port, mlxsw_sp_qdisc, p->qstats);
}

static int
mlxsw_sp_qdisc_get_tbf_stats(struct mlxsw_sp_port *mlxsw_sp_port,
			     struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			     struct tc_qopt_offload_stats *stats_ptr)
{
	mlxsw_sp_qdisc_get_tc_stats(mlxsw_sp_port, mlxsw_sp_qdisc,
				    stats_ptr);
	return 0;
}

static struct mlxsw_sp_qdisc_ops mlxsw_sp_qdisc_ops_tbf = {
	.type = MLXSW_SP_QDISC_TBF,
	.check_params = mlxsw_sp_qdisc_tbf_check_params,
	.replace = mlxsw_sp_qdisc_tbf_replace,
	.unoffload = mlxsw_sp_qdisc_tbf_unoffload,
	.destroy = mlxsw_sp_qdisc_tbf_destroy,
	.get_stats = mlxsw_sp_qdisc_get_tbf_stats,
	.clean_stats = mlxsw_sp_setup_tc_qdisc_leaf_clean_stats,
	.find_class = mlxsw_sp_qdisc_leaf_find_class,
};

static int __mlxsw_sp_setup_tc_tbf(struct mlxsw_sp_port *mlxsw_sp_port,
				   struct tc_tbf_qopt_offload *p)
{
	struct mlxsw_sp_qdisc *mlxsw_sp_qdisc;

	mlxsw_sp_qdisc = mlxsw_sp_qdisc_find(mlxsw_sp_port, p->parent);
	if (!mlxsw_sp_qdisc)
		return -EOPNOTSUPP;

	if (p->command == TC_TBF_REPLACE)
		return mlxsw_sp_qdisc_replace(mlxsw_sp_port, p->handle,
					      mlxsw_sp_qdisc,
					      &mlxsw_sp_qdisc_ops_tbf,
					      &p->replace_params);

	if (!mlxsw_sp_qdisc_compare(mlxsw_sp_qdisc, p->handle))
		return -EOPNOTSUPP;

	switch (p->command) {
	case TC_TBF_DESTROY:
		return mlxsw_sp_qdisc_destroy(mlxsw_sp_port, mlxsw_sp_qdisc);
	case TC_TBF_STATS:
		return mlxsw_sp_qdisc_get_stats(mlxsw_sp_port, mlxsw_sp_qdisc,
						&p->stats);
	case TC_TBF_GRAFT:
		return mlxsw_sp_qdisc_graft(mlxsw_sp_port, mlxsw_sp_qdisc, 0,
					    p->child_handle);
	default:
		return -EOPNOTSUPP;
	}
}

int mlxsw_sp_setup_tc_tbf(struct mlxsw_sp_port *mlxsw_sp_port,
			  struct tc_tbf_qopt_offload *p)
{
	int err;

	mutex_lock(&mlxsw_sp_port->qdisc->lock);
	err = __mlxsw_sp_setup_tc_tbf(mlxsw_sp_port, p);
	mutex_unlock(&mlxsw_sp_port->qdisc->lock);

	return err;
}

static int
mlxsw_sp_qdisc_fifo_check_params(struct mlxsw_sp_port *mlxsw_sp_port,
				 void *params)
{
	return 0;
}

static int
mlxsw_sp_qdisc_fifo_replace(struct mlxsw_sp_port *mlxsw_sp_port, u32 handle,
			    struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			    void *params)
{
	return 0;
}

static int
mlxsw_sp_qdisc_get_fifo_stats(struct mlxsw_sp_port *mlxsw_sp_port,
			      struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			      struct tc_qopt_offload_stats *stats_ptr)
{
	mlxsw_sp_qdisc_get_tc_stats(mlxsw_sp_port, mlxsw_sp_qdisc,
				    stats_ptr);
	return 0;
}

static struct mlxsw_sp_qdisc_ops mlxsw_sp_qdisc_ops_fifo = {
	.type = MLXSW_SP_QDISC_FIFO,
	.check_params = mlxsw_sp_qdisc_fifo_check_params,
	.replace = mlxsw_sp_qdisc_fifo_replace,
	.get_stats = mlxsw_sp_qdisc_get_fifo_stats,
	.clean_stats = mlxsw_sp_setup_tc_qdisc_leaf_clean_stats,
};

static int
mlxsw_sp_qdisc_future_fifo_replace(struct mlxsw_sp_port *mlxsw_sp_port,
				   u32 handle, unsigned int band,
				   struct mlxsw_sp_qdisc *child_qdisc)
{
	struct mlxsw_sp_qdisc_state *qdisc_state = mlxsw_sp_port->qdisc;

	if (handle == qdisc_state->future_handle &&
	    qdisc_state->future_fifos[band])
		return mlxsw_sp_qdisc_replace(mlxsw_sp_port, TC_H_UNSPEC,
					      child_qdisc,
					      &mlxsw_sp_qdisc_ops_fifo,
					      NULL);
	return 0;
}

static void
mlxsw_sp_qdisc_future_fifos_init(struct mlxsw_sp_port *mlxsw_sp_port,
				 u32 handle)
{
	struct mlxsw_sp_qdisc_state *qdisc_state = mlxsw_sp_port->qdisc;

	qdisc_state->future_handle = handle;
	memset(qdisc_state->future_fifos, 0, sizeof(qdisc_state->future_fifos));
}

static int __mlxsw_sp_setup_tc_fifo(struct mlxsw_sp_port *mlxsw_sp_port,
				    struct tc_fifo_qopt_offload *p)
{
	struct mlxsw_sp_qdisc_state *qdisc_state = mlxsw_sp_port->qdisc;
	struct mlxsw_sp_qdisc *mlxsw_sp_qdisc;
	unsigned int band;
	u32 parent_handle;

	mlxsw_sp_qdisc = mlxsw_sp_qdisc_find(mlxsw_sp_port, p->parent);
	if (!mlxsw_sp_qdisc && p->handle == TC_H_UNSPEC) {
		parent_handle = TC_H_MAJ(p->parent);
		if (parent_handle != qdisc_state->future_handle) {
			/* This notifications is for a different Qdisc than
			 * previously. Wipe the future cache.
			 */
			mlxsw_sp_qdisc_future_fifos_init(mlxsw_sp_port,
							 parent_handle);
		}

		band = TC_H_MIN(p->parent) - 1;
		if (band < IEEE_8021QAZ_MAX_TCS) {
			if (p->command == TC_FIFO_REPLACE)
				qdisc_state->future_fifos[band] = true;
			else if (p->command == TC_FIFO_DESTROY)
				qdisc_state->future_fifos[band] = false;
		}
	}
	if (!mlxsw_sp_qdisc)
		return -EOPNOTSUPP;

	if (p->command == TC_FIFO_REPLACE) {
		return mlxsw_sp_qdisc_replace(mlxsw_sp_port, p->handle,
					      mlxsw_sp_qdisc,
					      &mlxsw_sp_qdisc_ops_fifo, NULL);
	}

	if (!mlxsw_sp_qdisc_compare(mlxsw_sp_qdisc, p->handle))
		return -EOPNOTSUPP;

	switch (p->command) {
	case TC_FIFO_DESTROY:
		return mlxsw_sp_qdisc_destroy(mlxsw_sp_port, mlxsw_sp_qdisc);
	case TC_FIFO_STATS:
		return mlxsw_sp_qdisc_get_stats(mlxsw_sp_port, mlxsw_sp_qdisc,
						&p->stats);
	case TC_FIFO_REPLACE: /* Handled above. */
		break;
	}

	return -EOPNOTSUPP;
}

int mlxsw_sp_setup_tc_fifo(struct mlxsw_sp_port *mlxsw_sp_port,
			   struct tc_fifo_qopt_offload *p)
{
	int err;

	mutex_lock(&mlxsw_sp_port->qdisc->lock);
	err = __mlxsw_sp_setup_tc_fifo(mlxsw_sp_port, p);
	mutex_unlock(&mlxsw_sp_port->qdisc->lock);

	return err;
}

static int __mlxsw_sp_qdisc_ets_destroy(struct mlxsw_sp_port *mlxsw_sp_port,
					struct mlxsw_sp_qdisc *mlxsw_sp_qdisc)
{
	int i;

	for (i = 0; i < mlxsw_sp_qdisc->num_classes; i++) {
		mlxsw_sp_port_prio_tc_set(mlxsw_sp_port, i,
					  MLXSW_SP_PORT_DEFAULT_TCLASS);
		mlxsw_sp_port_ets_set(mlxsw_sp_port,
				      MLXSW_REG_QEEC_HR_SUBGROUP,
				      i, 0, false, 0);
	}

	kfree(mlxsw_sp_qdisc->ets_data);
	mlxsw_sp_qdisc->ets_data = NULL;
	return 0;
}

static int
mlxsw_sp_qdisc_prio_destroy(struct mlxsw_sp_port *mlxsw_sp_port,
			    struct mlxsw_sp_qdisc *mlxsw_sp_qdisc)
{
	return __mlxsw_sp_qdisc_ets_destroy(mlxsw_sp_port, mlxsw_sp_qdisc);
}

static int
__mlxsw_sp_qdisc_ets_check_params(unsigned int nbands)
{
	if (nbands > IEEE_8021QAZ_MAX_TCS)
		return -EOPNOTSUPP;

	return 0;
}

static int
mlxsw_sp_qdisc_prio_check_params(struct mlxsw_sp_port *mlxsw_sp_port,
				 void *params)
{
	struct tc_prio_qopt_offload_params *p = params;

	return __mlxsw_sp_qdisc_ets_check_params(p->bands);
}

static struct mlxsw_sp_qdisc *
mlxsw_sp_qdisc_walk_cb_clean_stats(struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
				   void *mlxsw_sp_port)
{
	u64 backlog;

	if (mlxsw_sp_qdisc->ops) {
		backlog = mlxsw_sp_qdisc->stats_base.backlog;
		if (mlxsw_sp_qdisc->ops->clean_stats)
			mlxsw_sp_qdisc->ops->clean_stats(mlxsw_sp_port,
							 mlxsw_sp_qdisc);
		mlxsw_sp_qdisc->stats_base.backlog = backlog;
	}

	return NULL;
}

static void
mlxsw_sp_qdisc_tree_clean_stats(struct mlxsw_sp_port *mlxsw_sp_port,
				struct mlxsw_sp_qdisc *mlxsw_sp_qdisc)
{
	mlxsw_sp_qdisc_walk(mlxsw_sp_qdisc, mlxsw_sp_qdisc_walk_cb_clean_stats,
			    mlxsw_sp_port);
}

static int
__mlxsw_sp_qdisc_ets_replace(struct mlxsw_sp_port *mlxsw_sp_port,
			     struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			     u32 handle, unsigned int nbands,
			     const unsigned int *quanta,
			     const unsigned int *weights,
			     const u8 *priomap)
{
	struct mlxsw_sp_qdisc_ets_data *ets_data = mlxsw_sp_qdisc->ets_data;
	struct mlxsw_sp_qdisc_ets_band *ets_band;
	struct mlxsw_sp_qdisc *child_qdisc;
	u8 old_priomap, new_priomap;
	int i, band;
	int err;

	if (!ets_data) {
		ets_data = kzalloc(sizeof(*ets_data), GFP_KERNEL);
		if (!ets_data)
			return -ENOMEM;
		mlxsw_sp_qdisc->ets_data = ets_data;

		for (band = 0; band < mlxsw_sp_qdisc->num_classes; band++) {
			int tclass_num = MLXSW_SP_PRIO_BAND_TO_TCLASS(band);

			ets_band = &ets_data->bands[band];
			ets_band->tclass_num = tclass_num;
		}
	}

	for (band = 0; band < nbands; band++) {
		int tclass_num;

		child_qdisc = &mlxsw_sp_qdisc->qdiscs[band];
		ets_band = &ets_data->bands[band];

		tclass_num = ets_band->tclass_num;
		old_priomap = ets_band->prio_bitmap;
		new_priomap = 0;

		err = mlxsw_sp_port_ets_set(mlxsw_sp_port,
					    MLXSW_REG_QEEC_HR_SUBGROUP,
					    tclass_num, 0, !!quanta[band],
					    weights[band]);
		if (err)
			return err;

		for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
			if (priomap[i] == band) {
				new_priomap |= BIT(i);
				if (BIT(i) & old_priomap)
					continue;
				err = mlxsw_sp_port_prio_tc_set(mlxsw_sp_port,
								i, tclass_num);
				if (err)
					return err;
			}
		}

		ets_band->prio_bitmap = new_priomap;

		if (old_priomap != new_priomap)
			mlxsw_sp_qdisc_tree_clean_stats(mlxsw_sp_port,
							child_qdisc);

		err = mlxsw_sp_qdisc_future_fifo_replace(mlxsw_sp_port, handle,
							 band, child_qdisc);
		if (err)
			return err;
	}
	for (; band < IEEE_8021QAZ_MAX_TCS; band++) {
		ets_band = &ets_data->bands[band];
		ets_band->prio_bitmap = 0;

		child_qdisc = &mlxsw_sp_qdisc->qdiscs[band];
		mlxsw_sp_qdisc_destroy(mlxsw_sp_port, child_qdisc);

		mlxsw_sp_port_ets_set(mlxsw_sp_port,
				      MLXSW_REG_QEEC_HR_SUBGROUP,
				      ets_band->tclass_num, 0, false, 0);
	}

	mlxsw_sp_qdisc_future_fifos_init(mlxsw_sp_port, TC_H_UNSPEC);
	return 0;
}

static int
mlxsw_sp_qdisc_prio_replace(struct mlxsw_sp_port *mlxsw_sp_port, u32 handle,
			    struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			    void *params)
{
	struct tc_prio_qopt_offload_params *p = params;
	unsigned int zeroes[TCQ_ETS_MAX_BANDS] = {0};

	return __mlxsw_sp_qdisc_ets_replace(mlxsw_sp_port, mlxsw_sp_qdisc,
					    handle, p->bands, zeroes,
					    zeroes, p->priomap);
}

static void
__mlxsw_sp_qdisc_ets_unoffload(struct mlxsw_sp_port *mlxsw_sp_port,
			       struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			       struct gnet_stats_queue *qstats)
{
	u64 backlog;

	backlog = mlxsw_sp_cells_bytes(mlxsw_sp_port->mlxsw_sp,
				       mlxsw_sp_qdisc->stats_base.backlog);
	qstats->backlog -= backlog;
}

static void
mlxsw_sp_qdisc_prio_unoffload(struct mlxsw_sp_port *mlxsw_sp_port,
			      struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			      void *params)
{
	struct tc_prio_qopt_offload_params *p = params;

	__mlxsw_sp_qdisc_ets_unoffload(mlxsw_sp_port, mlxsw_sp_qdisc,
				       p->qstats);
}

static int
mlxsw_sp_qdisc_get_prio_stats(struct mlxsw_sp_port *mlxsw_sp_port,
			      struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			      struct tc_qopt_offload_stats *stats_ptr)
{
	struct mlxsw_sp_qdisc *tc_qdisc;
	u64 tx_packets = 0;
	u64 tx_bytes = 0;
	u64 backlog = 0;
	u64 drops = 0;
	int i;

	for (i = 0; i < mlxsw_sp_qdisc->num_classes; i++) {
		tc_qdisc = &mlxsw_sp_qdisc->qdiscs[i];
		mlxsw_sp_qdisc_collect_tc_stats(mlxsw_sp_port, tc_qdisc,
						&tx_bytes, &tx_packets,
						&drops, &backlog);
	}

	mlxsw_sp_qdisc_update_stats(mlxsw_sp_port->mlxsw_sp, mlxsw_sp_qdisc,
				    tx_bytes, tx_packets, drops, backlog,
				    stats_ptr);
	return 0;
}

static void
mlxsw_sp_setup_tc_qdisc_prio_clean_stats(struct mlxsw_sp_port *mlxsw_sp_port,
					 struct mlxsw_sp_qdisc *mlxsw_sp_qdisc)
{
	struct mlxsw_sp_qdisc_stats *stats_base;
	struct mlxsw_sp_port_xstats *xstats;
	struct rtnl_link_stats64 *stats;
	int i;

	xstats = &mlxsw_sp_port->periodic_hw_stats.xstats;
	stats = &mlxsw_sp_port->periodic_hw_stats.stats;
	stats_base = &mlxsw_sp_qdisc->stats_base;

	stats_base->tx_packets = stats->tx_packets;
	stats_base->tx_bytes = stats->tx_bytes;

	stats_base->drops = 0;
	for (i = 0; i < IEEE_8021QAZ_MAX_TCS; i++) {
		stats_base->drops += mlxsw_sp_xstats_tail_drop(xstats, i);
		stats_base->drops += xstats->wred_drop[i];
	}

	mlxsw_sp_qdisc->stats_base.backlog = 0;
}

static struct mlxsw_sp_qdisc *
mlxsw_sp_qdisc_prio_find_class(struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			       u32 parent)
{
	int child_index = TC_H_MIN(parent);
	int band = child_index - 1;

	if (band < 0 || band >= mlxsw_sp_qdisc->num_classes)
		return NULL;
	return &mlxsw_sp_qdisc->qdiscs[band];
}

static struct mlxsw_sp_qdisc_ets_band *
mlxsw_sp_qdisc_ets_get_band(struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			    struct mlxsw_sp_qdisc *child)
{
	unsigned int band = child - mlxsw_sp_qdisc->qdiscs;

	if (WARN_ON(band >= IEEE_8021QAZ_MAX_TCS))
		band = 0;
	return &mlxsw_sp_qdisc->ets_data->bands[band];
}

static u8
mlxsw_sp_qdisc_ets_get_prio_bitmap(struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
				   struct mlxsw_sp_qdisc *child)
{
	return mlxsw_sp_qdisc_ets_get_band(mlxsw_sp_qdisc, child)->prio_bitmap;
}

static int
mlxsw_sp_qdisc_ets_get_tclass_num(struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
				  struct mlxsw_sp_qdisc *child)
{
	return mlxsw_sp_qdisc_ets_get_band(mlxsw_sp_qdisc, child)->tclass_num;
}

static struct mlxsw_sp_qdisc_ops mlxsw_sp_qdisc_ops_prio = {
	.type = MLXSW_SP_QDISC_PRIO,
	.check_params = mlxsw_sp_qdisc_prio_check_params,
	.replace = mlxsw_sp_qdisc_prio_replace,
	.unoffload = mlxsw_sp_qdisc_prio_unoffload,
	.destroy = mlxsw_sp_qdisc_prio_destroy,
	.get_stats = mlxsw_sp_qdisc_get_prio_stats,
	.clean_stats = mlxsw_sp_setup_tc_qdisc_prio_clean_stats,
	.find_class = mlxsw_sp_qdisc_prio_find_class,
	.num_classes = IEEE_8021QAZ_MAX_TCS,
	.get_prio_bitmap = mlxsw_sp_qdisc_ets_get_prio_bitmap,
	.get_tclass_num = mlxsw_sp_qdisc_ets_get_tclass_num,
};

static int
mlxsw_sp_qdisc_ets_check_params(struct mlxsw_sp_port *mlxsw_sp_port,
				void *params)
{
	struct tc_ets_qopt_offload_replace_params *p = params;

	return __mlxsw_sp_qdisc_ets_check_params(p->bands);
}

static int
mlxsw_sp_qdisc_ets_replace(struct mlxsw_sp_port *mlxsw_sp_port, u32 handle,
			   struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			   void *params)
{
	struct tc_ets_qopt_offload_replace_params *p = params;

	return __mlxsw_sp_qdisc_ets_replace(mlxsw_sp_port, mlxsw_sp_qdisc,
					    handle, p->bands, p->quanta,
					    p->weights, p->priomap);
}

static void
mlxsw_sp_qdisc_ets_unoffload(struct mlxsw_sp_port *mlxsw_sp_port,
			     struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
			     void *params)
{
	struct tc_ets_qopt_offload_replace_params *p = params;

	__mlxsw_sp_qdisc_ets_unoffload(mlxsw_sp_port, mlxsw_sp_qdisc,
				       p->qstats);
}

static int
mlxsw_sp_qdisc_ets_destroy(struct mlxsw_sp_port *mlxsw_sp_port,
			   struct mlxsw_sp_qdisc *mlxsw_sp_qdisc)
{
	return __mlxsw_sp_qdisc_ets_destroy(mlxsw_sp_port, mlxsw_sp_qdisc);
}

static struct mlxsw_sp_qdisc_ops mlxsw_sp_qdisc_ops_ets = {
	.type = MLXSW_SP_QDISC_ETS,
	.check_params = mlxsw_sp_qdisc_ets_check_params,
	.replace = mlxsw_sp_qdisc_ets_replace,
	.unoffload = mlxsw_sp_qdisc_ets_unoffload,
	.destroy = mlxsw_sp_qdisc_ets_destroy,
	.get_stats = mlxsw_sp_qdisc_get_prio_stats,
	.clean_stats = mlxsw_sp_setup_tc_qdisc_prio_clean_stats,
	.find_class = mlxsw_sp_qdisc_prio_find_class,
	.num_classes = IEEE_8021QAZ_MAX_TCS,
	.get_prio_bitmap = mlxsw_sp_qdisc_ets_get_prio_bitmap,
	.get_tclass_num = mlxsw_sp_qdisc_ets_get_tclass_num,
};

/* Linux allows linking of Qdiscs to arbitrary classes (so long as the resulting
 * graph is free of cycles). These operations do not change the parent handle
 * though, which means it can be incomplete (if there is more than one class
 * where the Qdisc in question is grafted) or outright wrong (if the Qdisc was
 * linked to a different class and then removed from the original class).
 *
 * E.g. consider this sequence of operations:
 *
 *  # tc qdisc add dev swp1 root handle 1: prio
 *  # tc qdisc add dev swp1 parent 1:3 handle 13: red limit 1000000 avpkt 10000
 *  RED: set bandwidth to 10Mbit
 *  # tc qdisc link dev swp1 handle 13: parent 1:2
 *
 * At this point, both 1:2 and 1:3 have the same RED Qdisc instance as their
 * child. But RED will still only claim that 1:3 is its parent. If it's removed
 * from that band, its only parent will be 1:2, but it will continue to claim
 * that it is in fact 1:3.
 *
 * The notification for child Qdisc replace (e.g. TC_RED_REPLACE) comes before
 * the notification for parent graft (e.g. TC_PRIO_GRAFT). We take the replace
 * notification to offload the child Qdisc, based on its parent handle, and use
 * the graft operation to validate that the class where the child is actually
 * grafted corresponds to the parent handle. If the two don't match, we
 * unoffload the child.
 */
static int mlxsw_sp_qdisc_graft(struct mlxsw_sp_port *mlxsw_sp_port,
				struct mlxsw_sp_qdisc *mlxsw_sp_qdisc,
				u8 band, u32 child_handle)
{
	struct mlxsw_sp_qdisc *old_qdisc;
	u32 parent;

	if (band < mlxsw_sp_qdisc->num_classes &&
	    mlxsw_sp_qdisc->qdiscs[band].handle == child_handle)
		return 0;

	if (!child_handle) {
		/* This is an invisible FIFO replacing the original Qdisc.
		 * Ignore it--the original Qdisc's destroy will follow.
		 */
		return 0;
	}

	/* See if the grafted qdisc is already offloaded on any tclass. If so,
	 * unoffload it.
	 */
	old_qdisc = mlxsw_sp_qdisc_find_by_handle(mlxsw_sp_port,
						  child_handle);
	if (old_qdisc)
		mlxsw_sp_qdisc_destroy(mlxsw_sp_port, old_qdisc);

	parent = TC_H_MAKE(mlxsw_sp_qdisc->handle, band + 1);
	mlxsw_sp_qdisc = mlxsw_sp_qdisc->ops->find_class(mlxsw_sp_qdisc,
							 parent);
	if (!WARN_ON(!mlxsw_sp_qdisc))
		mlxsw_sp_qdisc_destroy(mlxsw_sp_port, mlxsw_sp_qdisc);

	return -EOPNOTSUPP;
}

static int __mlxsw_sp_setup_tc_prio(struct mlxsw_sp_port *mlxsw_sp_port,
				    struct tc_prio_qopt_offload *p)
{
	struct mlxsw_sp_qdisc *mlxsw_sp_qdisc;

	mlxsw_sp_qdisc = mlxsw_sp_qdisc_find(mlxsw_sp_port, p->parent);
	if (!mlxsw_sp_qdisc)
		return -EOPNOTSUPP;

	if (p->command == TC_PRIO_REPLACE)
		return mlxsw_sp_qdisc_replace(mlxsw_sp_port, p->handle,
					      mlxsw_sp_qdisc,
					      &mlxsw_sp_qdisc_ops_prio,
					      &p->replace_params);

	if (!mlxsw_sp_qdisc_compare(mlxsw_sp_qdisc, p->handle))
		return -EOPNOTSUPP;

	switch (p->command) {
	case TC_PRIO_DESTROY:
		return mlxsw_sp_qdisc_destroy(mlxsw_sp_port, mlxsw_sp_qdisc);
	case TC_PRIO_STATS:
		return mlxsw_sp_qdisc_get_stats(mlxsw_sp_port, mlxsw_sp_qdisc,
						&p->stats);
	case TC_PRIO_GRAFT:
		return mlxsw_sp_qdisc_graft(mlxsw_sp_port, mlxsw_sp_qdisc,
					    p->graft_params.band,
					    p->graft_params.child_handle);
	default:
		return -EOPNOTSUPP;
	}
}

int mlxsw_sp_setup_tc_prio(struct mlxsw_sp_port *mlxsw_sp_port,
			   struct tc_prio_qopt_offload *p)
{
	int err;

	mutex_lock(&mlxsw_sp_port->qdisc->lock);
	err = __mlxsw_sp_setup_tc_prio(mlxsw_sp_port, p);
	mutex_unlock(&mlxsw_sp_port->qdisc->lock);

	return err;
}

static int __mlxsw_sp_setup_tc_ets(struct mlxsw_sp_port *mlxsw_sp_port,
				   struct tc_ets_qopt_offload *p)
{
	struct mlxsw_sp_qdisc *mlxsw_sp_qdisc;

	mlxsw_sp_qdisc = mlxsw_sp_qdisc_find(mlxsw_sp_port, p->parent);
	if (!mlxsw_sp_qdisc)
		return -EOPNOTSUPP;

	if (p->command == TC_ETS_REPLACE)
		return mlxsw_sp_qdisc_replace(mlxsw_sp_port, p->handle,
					      mlxsw_sp_qdisc,
					      &mlxsw_sp_qdisc_ops_ets,
					      &p->replace_params);

	if (!mlxsw_sp_qdisc_compare(mlxsw_sp_qdisc, p->handle))
		return -EOPNOTSUPP;

	switch (p->command) {
	case TC_ETS_DESTROY:
		return mlxsw_sp_qdisc_destroy(mlxsw_sp_port, mlxsw_sp_qdisc);
	case TC_ETS_STATS:
		return mlxsw_sp_qdisc_get_stats(mlxsw_sp_port, mlxsw_sp_qdisc,
						&p->stats);
	case TC_ETS_GRAFT:
		return mlxsw_sp_qdisc_graft(mlxsw_sp_port, mlxsw_sp_qdisc,
					    p->graft_params.band,
					    p->graft_params.child_handle);
	default:
		return -EOPNOTSUPP;
	}
}

int mlxsw_sp_setup_tc_ets(struct mlxsw_sp_port *mlxsw_sp_port,
			  struct tc_ets_qopt_offload *p)
{
	int err;

	mutex_lock(&mlxsw_sp_port->qdisc->lock);
	err = __mlxsw_sp_setup_tc_ets(mlxsw_sp_port, p);
	mutex_unlock(&mlxsw_sp_port->qdisc->lock);

	return err;
}

struct mlxsw_sp_qevent_block {
	struct list_head binding_list;
	struct list_head mall_entry_list;
	struct mlxsw_sp *mlxsw_sp;
};

struct mlxsw_sp_qevent_binding {
	struct list_head list;
	struct mlxsw_sp_port *mlxsw_sp_port;
	u32 handle;
	int tclass_num;
	enum mlxsw_sp_span_trigger span_trigger;
	unsigned int action_mask;
};

static LIST_HEAD(mlxsw_sp_qevent_block_cb_list);

static int mlxsw_sp_qevent_span_configure(struct mlxsw_sp *mlxsw_sp,
					  struct mlxsw_sp_mall_entry *mall_entry,
					  struct mlxsw_sp_qevent_binding *qevent_binding,
					  const struct mlxsw_sp_span_agent_parms *agent_parms,
					  int *p_span_id)
{
	enum mlxsw_sp_span_trigger span_trigger = qevent_binding->span_trigger;
	struct mlxsw_sp_port *mlxsw_sp_port = qevent_binding->mlxsw_sp_port;
	struct mlxsw_sp_span_trigger_parms trigger_parms = {};
	bool ingress;
	int span_id;
	int err;

	err = mlxsw_sp_span_agent_get(mlxsw_sp, &span_id, agent_parms);
	if (err)
		return err;

	ingress = mlxsw_sp_span_trigger_is_ingress(span_trigger);
	err = mlxsw_sp_span_analyzed_port_get(mlxsw_sp_port, ingress);
	if (err)
		goto err_analyzed_port_get;

	trigger_parms.span_id = span_id;
	trigger_parms.probability_rate = 1;
	err = mlxsw_sp_span_agent_bind(mlxsw_sp, span_trigger, mlxsw_sp_port,
				       &trigger_parms);
	if (err)
		goto err_agent_bind;

	err = mlxsw_sp_span_trigger_enable(mlxsw_sp_port, span_trigger,
					   qevent_binding->tclass_num);
	if (err)
		goto err_trigger_enable;

	*p_span_id = span_id;
	return 0;

err_trigger_enable:
	mlxsw_sp_span_agent_unbind(mlxsw_sp, span_trigger, mlxsw_sp_port,
				   &trigger_parms);
err_agent_bind:
	mlxsw_sp_span_analyzed_port_put(mlxsw_sp_port, ingress);
err_analyzed_port_get:
	mlxsw_sp_span_agent_put(mlxsw_sp, span_id);
	return err;
}

static void mlxsw_sp_qevent_span_deconfigure(struct mlxsw_sp *mlxsw_sp,
					     struct mlxsw_sp_qevent_binding *qevent_binding,
					     int span_id)
{
	enum mlxsw_sp_span_trigger span_trigger = qevent_binding->span_trigger;
	struct mlxsw_sp_port *mlxsw_sp_port = qevent_binding->mlxsw_sp_port;
	struct mlxsw_sp_span_trigger_parms trigger_parms = {
		.span_id = span_id,
	};
	bool ingress;

	ingress = mlxsw_sp_span_trigger_is_ingress(span_trigger);

	mlxsw_sp_span_trigger_disable(mlxsw_sp_port, span_trigger,
				      qevent_binding->tclass_num);
	mlxsw_sp_span_agent_unbind(mlxsw_sp, span_trigger, mlxsw_sp_port,
				   &trigger_parms);
	mlxsw_sp_span_analyzed_port_put(mlxsw_sp_port, ingress);
	mlxsw_sp_span_agent_put(mlxsw_sp, span_id);
}

static int mlxsw_sp_qevent_mirror_configure(struct mlxsw_sp *mlxsw_sp,
					    struct mlxsw_sp_mall_entry *mall_entry,
					    struct mlxsw_sp_qevent_binding *qevent_binding)
{
	struct mlxsw_sp_span_agent_parms agent_parms = {
		.to_dev = mall_entry->mirror.to_dev,
	};

	return mlxsw_sp_qevent_span_configure(mlxsw_sp, mall_entry, qevent_binding,
					      &agent_parms, &mall_entry->mirror.span_id);
}

static void mlxsw_sp_qevent_mirror_deconfigure(struct mlxsw_sp *mlxsw_sp,
					       struct mlxsw_sp_mall_entry *mall_entry,
					       struct mlxsw_sp_qevent_binding *qevent_binding)
{
	mlxsw_sp_qevent_span_deconfigure(mlxsw_sp, qevent_binding, mall_entry->mirror.span_id);
}

static int mlxsw_sp_qevent_trap_configure(struct mlxsw_sp *mlxsw_sp,
					  struct mlxsw_sp_mall_entry *mall_entry,
					  struct mlxsw_sp_qevent_binding *qevent_binding)
{
	struct mlxsw_sp_span_agent_parms agent_parms = {
		.session_id = MLXSW_SP_SPAN_SESSION_ID_BUFFER,
	};
	int err;

	err = mlxsw_sp_trap_group_policer_hw_id_get(mlxsw_sp,
						    DEVLINK_TRAP_GROUP_GENERIC_ID_BUFFER_DROPS,
						    &agent_parms.policer_enable,
						    &agent_parms.policer_id);
	if (err)
		return err;

	return mlxsw_sp_qevent_span_configure(mlxsw_sp, mall_entry, qevent_binding,
					      &agent_parms, &mall_entry->trap.span_id);
}

static void mlxsw_sp_qevent_trap_deconfigure(struct mlxsw_sp *mlxsw_sp,
					     struct mlxsw_sp_mall_entry *mall_entry,
					     struct mlxsw_sp_qevent_binding *qevent_binding)
{
	mlxsw_sp_qevent_span_deconfigure(mlxsw_sp, qevent_binding, mall_entry->trap.span_id);
}

static int
mlxsw_sp_qevent_entry_configure(struct mlxsw_sp *mlxsw_sp,
				struct mlxsw_sp_mall_entry *mall_entry,
				struct mlxsw_sp_qevent_binding *qevent_binding,
				struct netlink_ext_ack *extack)
{
	if (!(BIT(mall_entry->type) & qevent_binding->action_mask)) {
		NL_SET_ERR_MSG(extack, "Action not supported at this qevent");
		return -EOPNOTSUPP;
	}

	switch (mall_entry->type) {
	case MLXSW_SP_MALL_ACTION_TYPE_MIRROR:
		return mlxsw_sp_qevent_mirror_configure(mlxsw_sp, mall_entry, qevent_binding);
	case MLXSW_SP_MALL_ACTION_TYPE_TRAP:
		return mlxsw_sp_qevent_trap_configure(mlxsw_sp, mall_entry, qevent_binding);
	default:
		/* This should have been validated away. */
		WARN_ON(1);
		return -EOPNOTSUPP;
	}
}

static void mlxsw_sp_qevent_entry_deconfigure(struct mlxsw_sp *mlxsw_sp,
					      struct mlxsw_sp_mall_entry *mall_entry,
					      struct mlxsw_sp_qevent_binding *qevent_binding)
{
	switch (mall_entry->type) {
	case MLXSW_SP_MALL_ACTION_TYPE_MIRROR:
		return mlxsw_sp_qevent_mirror_deconfigure(mlxsw_sp, mall_entry, qevent_binding);
	case MLXSW_SP_MALL_ACTION_TYPE_TRAP:
		return mlxsw_sp_qevent_trap_deconfigure(mlxsw_sp, mall_entry, qevent_binding);
	default:
		WARN_ON(1);
		return;
	}
}

static int
mlxsw_sp_qevent_binding_configure(struct mlxsw_sp_qevent_block *qevent_block,
				  struct mlxsw_sp_qevent_binding *qevent_binding,
				  struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_mall_entry *mall_entry;
	int err;

	list_for_each_entry(mall_entry, &qevent_block->mall_entry_list, list) {
		err = mlxsw_sp_qevent_entry_configure(qevent_block->mlxsw_sp, mall_entry,
						      qevent_binding, extack);
		if (err)
			goto err_entry_configure;
	}

	return 0;

err_entry_configure:
	list_for_each_entry_continue_reverse(mall_entry, &qevent_block->mall_entry_list, list)
		mlxsw_sp_qevent_entry_deconfigure(qevent_block->mlxsw_sp, mall_entry,
						  qevent_binding);
	return err;
}

static void mlxsw_sp_qevent_binding_deconfigure(struct mlxsw_sp_qevent_block *qevent_block,
						struct mlxsw_sp_qevent_binding *qevent_binding)
{
	struct mlxsw_sp_mall_entry *mall_entry;

	list_for_each_entry(mall_entry, &qevent_block->mall_entry_list, list)
		mlxsw_sp_qevent_entry_deconfigure(qevent_block->mlxsw_sp, mall_entry,
						  qevent_binding);
}

static int
mlxsw_sp_qevent_block_configure(struct mlxsw_sp_qevent_block *qevent_block,
				struct netlink_ext_ack *extack)
{
	struct mlxsw_sp_qevent_binding *qevent_binding;
	int err;

	list_for_each_entry(qevent_binding, &qevent_block->binding_list, list) {
		err = mlxsw_sp_qevent_binding_configure(qevent_block,
							qevent_binding,
							extack);
		if (err)
			goto err_binding_configure;
	}

	return 0;

err_binding_configure:
	list_for_each_entry_continue_reverse(qevent_binding, &qevent_block->binding_list, list)
		mlxsw_sp_qevent_binding_deconfigure(qevent_block, qevent_binding);
	return err;
}

static void mlxsw_sp_qevent_block_deconfigure(struct mlxsw_sp_qevent_block *qevent_block)
{
	struct mlxsw_sp_qevent_binding *qevent_binding;

	list_for_each_entry(qevent_binding, &qevent_block->binding_list, list)
		mlxsw_sp_qevent_binding_deconfigure(qevent_block, qevent_binding);
}

static struct mlxsw_sp_mall_entry *
mlxsw_sp_qevent_mall_entry_find(struct mlxsw_sp_qevent_block *block, unsigned long cookie)
{
	struct mlxsw_sp_mall_entry *mall_entry;

	list_for_each_entry(mall_entry, &block->mall_entry_list, list)
		if (mall_entry->cookie == cookie)
			return mall_entry;

	return NULL;
}

static int mlxsw_sp_qevent_mall_replace(struct mlxsw_sp *mlxsw_sp,
					struct mlxsw_sp_qevent_block *qevent_block,
					struct tc_cls_matchall_offload *f)
{
	struct mlxsw_sp_mall_entry *mall_entry;
	struct flow_action_entry *act;
	int err;

	/* It should not currently be possible to replace a matchall rule. So
	 * this must be a new rule.
	 */
	if (!list_empty(&qevent_block->mall_entry_list)) {
		NL_SET_ERR_MSG(f->common.extack, "At most one filter supported");
		return -EOPNOTSUPP;
	}
	if (f->rule->action.num_entries != 1) {
		NL_SET_ERR_MSG(f->common.extack, "Only singular actions supported");
		return -EOPNOTSUPP;
	}
	if (f->common.chain_index) {
		NL_SET_ERR_MSG(f->common.extack, "Only chain 0 is supported");
		return -EOPNOTSUPP;
	}
	if (f->common.protocol != htons(ETH_P_ALL)) {
		NL_SET_ERR_MSG(f->common.extack, "Protocol matching not supported");
		return -EOPNOTSUPP;
	}

	act = &f->rule->action.entries[0];
	if (!(act->hw_stats & FLOW_ACTION_HW_STATS_DISABLED)) {
		NL_SET_ERR_MSG(f->common.extack, "HW counters not supported on qevents");
		return -EOPNOTSUPP;
	}

	mall_entry = kzalloc(sizeof(*mall_entry), GFP_KERNEL);
	if (!mall_entry)
		return -ENOMEM;
	mall_entry->cookie = f->cookie;

	if (act->id == FLOW_ACTION_MIRRED) {
		mall_entry->type = MLXSW_SP_MALL_ACTION_TYPE_MIRROR;
		mall_entry->mirror.to_dev = act->dev;
	} else if (act->id == FLOW_ACTION_TRAP) {
		mall_entry->type = MLXSW_SP_MALL_ACTION_TYPE_TRAP;
	} else {
		NL_SET_ERR_MSG(f->common.extack, "Unsupported action");
		err = -EOPNOTSUPP;
		goto err_unsupported_action;
	}

	list_add_tail(&mall_entry->list, &qevent_block->mall_entry_list);

	err = mlxsw_sp_qevent_block_configure(qevent_block, f->common.extack);
	if (err)
		goto err_block_configure;

	return 0;

err_block_configure:
	list_del(&mall_entry->list);
err_unsupported_action:
	kfree(mall_entry);
	return err;
}

static void mlxsw_sp_qevent_mall_destroy(struct mlxsw_sp_qevent_block *qevent_block,
					 struct tc_cls_matchall_offload *f)
{
	struct mlxsw_sp_mall_entry *mall_entry;

	mall_entry = mlxsw_sp_qevent_mall_entry_find(qevent_block, f->cookie);
	if (!mall_entry)
		return;

	mlxsw_sp_qevent_block_deconfigure(qevent_block);

	list_del(&mall_entry->list);
	kfree(mall_entry);
}

static int mlxsw_sp_qevent_block_mall_cb(struct mlxsw_sp_qevent_block *qevent_block,
					 struct tc_cls_matchall_offload *f)
{
	struct mlxsw_sp *mlxsw_sp = qevent_block->mlxsw_sp;

	switch (f->command) {
	case TC_CLSMATCHALL_REPLACE:
		return mlxsw_sp_qevent_mall_replace(mlxsw_sp, qevent_block, f);
	case TC_CLSMATCHALL_DESTROY:
		mlxsw_sp_qevent_mall_destroy(qevent_block, f);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

static int mlxsw_sp_qevent_block_cb(enum tc_setup_type type, void *type_data, void *cb_priv)
{
	struct mlxsw_sp_qevent_block *qevent_block = cb_priv;

	switch (type) {
	case TC_SETUP_CLSMATCHALL:
		return mlxsw_sp_qevent_block_mall_cb(qevent_block, type_data);
	default:
		return -EOPNOTSUPP;
	}
}

static struct mlxsw_sp_qevent_block *mlxsw_sp_qevent_block_create(struct mlxsw_sp *mlxsw_sp,
								  struct net *net)
{
	struct mlxsw_sp_qevent_block *qevent_block;

	qevent_block = kzalloc(sizeof(*qevent_block), GFP_KERNEL);
	if (!qevent_block)
		return NULL;

	INIT_LIST_HEAD(&qevent_block->binding_list);
	INIT_LIST_HEAD(&qevent_block->mall_entry_list);
	qevent_block->mlxsw_sp = mlxsw_sp;
	return qevent_block;
}

static void
mlxsw_sp_qevent_block_destroy(struct mlxsw_sp_qevent_block *qevent_block)
{
	WARN_ON(!list_empty(&qevent_block->binding_list));
	WARN_ON(!list_empty(&qevent_block->mall_entry_list));
	kfree(qevent_block);
}

static void mlxsw_sp_qevent_block_release(void *cb_priv)
{
	struct mlxsw_sp_qevent_block *qevent_block = cb_priv;

	mlxsw_sp_qevent_block_destroy(qevent_block);
}

static struct mlxsw_sp_qevent_binding *
mlxsw_sp_qevent_binding_create(struct mlxsw_sp_port *mlxsw_sp_port, u32 handle, int tclass_num,
			       enum mlxsw_sp_span_trigger span_trigger,
			       unsigned int action_mask)
{
	struct mlxsw_sp_qevent_binding *binding;

	binding = kzalloc(sizeof(*binding), GFP_KERNEL);
	if (!binding)
		return ERR_PTR(-ENOMEM);

	binding->mlxsw_sp_port = mlxsw_sp_port;
	binding->handle = handle;
	binding->tclass_num = tclass_num;
	binding->span_trigger = span_trigger;
	binding->action_mask = action_mask;
	return binding;
}

static void
mlxsw_sp_qevent_binding_destroy(struct mlxsw_sp_qevent_binding *binding)
{
	kfree(binding);
}

static struct mlxsw_sp_qevent_binding *
mlxsw_sp_qevent_binding_lookup(struct mlxsw_sp_qevent_block *block,
			       struct mlxsw_sp_port *mlxsw_sp_port,
			       u32 handle,
			       enum mlxsw_sp_span_trigger span_trigger)
{
	struct mlxsw_sp_qevent_binding *qevent_binding;

	list_for_each_entry(qevent_binding, &block->binding_list, list)
		if (qevent_binding->mlxsw_sp_port == mlxsw_sp_port &&
		    qevent_binding->handle == handle &&
		    qevent_binding->span_trigger == span_trigger)
			return qevent_binding;
	return NULL;
}

static int
mlxsw_sp_setup_tc_block_qevent_bind(struct mlxsw_sp_port *mlxsw_sp_port,
				    struct flow_block_offload *f,
				    enum mlxsw_sp_span_trigger span_trigger,
				    unsigned int action_mask)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_qevent_binding *qevent_binding;
	struct mlxsw_sp_qevent_block *qevent_block;
	struct flow_block_cb *block_cb;
	struct mlxsw_sp_qdisc *qdisc;
	bool register_block = false;
	int tclass_num;
	int err;

	block_cb = flow_block_cb_lookup(f->block, mlxsw_sp_qevent_block_cb, mlxsw_sp);
	if (!block_cb) {
		qevent_block = mlxsw_sp_qevent_block_create(mlxsw_sp, f->net);
		if (!qevent_block)
			return -ENOMEM;
		block_cb = flow_block_cb_alloc(mlxsw_sp_qevent_block_cb, mlxsw_sp, qevent_block,
					       mlxsw_sp_qevent_block_release);
		if (IS_ERR(block_cb)) {
			mlxsw_sp_qevent_block_destroy(qevent_block);
			return PTR_ERR(block_cb);
		}
		register_block = true;
	} else {
		qevent_block = flow_block_cb_priv(block_cb);
	}
	flow_block_cb_incref(block_cb);

	qdisc = mlxsw_sp_qdisc_find_by_handle(mlxsw_sp_port, f->sch->handle);
	if (!qdisc) {
		NL_SET_ERR_MSG(f->extack, "Qdisc not offloaded");
		err = -ENOENT;
		goto err_find_qdisc;
	}

	if (WARN_ON(mlxsw_sp_qevent_binding_lookup(qevent_block, mlxsw_sp_port, f->sch->handle,
						   span_trigger))) {
		err = -EEXIST;
		goto err_binding_exists;
	}

	tclass_num = mlxsw_sp_qdisc_get_tclass_num(mlxsw_sp_port, qdisc);
	qevent_binding = mlxsw_sp_qevent_binding_create(mlxsw_sp_port,
							f->sch->handle,
							tclass_num,
							span_trigger,
							action_mask);
	if (IS_ERR(qevent_binding)) {
		err = PTR_ERR(qevent_binding);
		goto err_binding_create;
	}

	err = mlxsw_sp_qevent_binding_configure(qevent_block, qevent_binding,
						f->extack);
	if (err)
		goto err_binding_configure;

	list_add(&qevent_binding->list, &qevent_block->binding_list);

	if (register_block) {
		flow_block_cb_add(block_cb, f);
		list_add_tail(&block_cb->driver_list, &mlxsw_sp_qevent_block_cb_list);
	}

	return 0;

err_binding_configure:
	mlxsw_sp_qevent_binding_destroy(qevent_binding);
err_binding_create:
err_binding_exists:
err_find_qdisc:
	if (!flow_block_cb_decref(block_cb))
		flow_block_cb_free(block_cb);
	return err;
}

static void mlxsw_sp_setup_tc_block_qevent_unbind(struct mlxsw_sp_port *mlxsw_sp_port,
						  struct flow_block_offload *f,
						  enum mlxsw_sp_span_trigger span_trigger)
{
	struct mlxsw_sp *mlxsw_sp = mlxsw_sp_port->mlxsw_sp;
	struct mlxsw_sp_qevent_binding *qevent_binding;
	struct mlxsw_sp_qevent_block *qevent_block;
	struct flow_block_cb *block_cb;

	block_cb = flow_block_cb_lookup(f->block, mlxsw_sp_qevent_block_cb, mlxsw_sp);
	if (!block_cb)
		return;
	qevent_block = flow_block_cb_priv(block_cb);

	qevent_binding = mlxsw_sp_qevent_binding_lookup(qevent_block, mlxsw_sp_port, f->sch->handle,
							span_trigger);
	if (!qevent_binding)
		return;

	list_del(&qevent_binding->list);
	mlxsw_sp_qevent_binding_deconfigure(qevent_block, qevent_binding);
	mlxsw_sp_qevent_binding_destroy(qevent_binding);

	if (!flow_block_cb_decref(block_cb)) {
		flow_block_cb_remove(block_cb, f);
		list_del(&block_cb->driver_list);
	}
}

static int
mlxsw_sp_setup_tc_block_qevent(struct mlxsw_sp_port *mlxsw_sp_port,
			       struct flow_block_offload *f,
			       enum mlxsw_sp_span_trigger span_trigger,
			       unsigned int action_mask)
{
	f->driver_block_list = &mlxsw_sp_qevent_block_cb_list;

	switch (f->command) {
	case FLOW_BLOCK_BIND:
		return mlxsw_sp_setup_tc_block_qevent_bind(mlxsw_sp_port, f,
							   span_trigger,
							   action_mask);
	case FLOW_BLOCK_UNBIND:
		mlxsw_sp_setup_tc_block_qevent_unbind(mlxsw_sp_port, f, span_trigger);
		return 0;
	default:
		return -EOPNOTSUPP;
	}
}

int mlxsw_sp_setup_tc_block_qevent_early_drop(struct mlxsw_sp_port *mlxsw_sp_port,
					      struct flow_block_offload *f)
{
	unsigned int action_mask = BIT(MLXSW_SP_MALL_ACTION_TYPE_MIRROR) |
				   BIT(MLXSW_SP_MALL_ACTION_TYPE_TRAP);

	return mlxsw_sp_setup_tc_block_qevent(mlxsw_sp_port, f,
					      MLXSW_SP_SPAN_TRIGGER_EARLY_DROP,
					      action_mask);
}

int mlxsw_sp_setup_tc_block_qevent_mark(struct mlxsw_sp_port *mlxsw_sp_port,
					struct flow_block_offload *f)
{
	unsigned int action_mask = BIT(MLXSW_SP_MALL_ACTION_TYPE_MIRROR);

	return mlxsw_sp_setup_tc_block_qevent(mlxsw_sp_port, f,
					      MLXSW_SP_SPAN_TRIGGER_ECN,
					      action_mask);
}

int mlxsw_sp_tc_qdisc_init(struct mlxsw_sp_port *mlxsw_sp_port)
{
	struct mlxsw_sp_qdisc_state *qdisc_state;

	qdisc_state = kzalloc(sizeof(*qdisc_state), GFP_KERNEL);
	if (!qdisc_state)
		return -ENOMEM;

	mutex_init(&qdisc_state->lock);
	mlxsw_sp_port->qdisc = qdisc_state;
	return 0;
}

void mlxsw_sp_tc_qdisc_fini(struct mlxsw_sp_port *mlxsw_sp_port)
{
	mutex_destroy(&mlxsw_sp_port->qdisc->lock);
	kfree(mlxsw_sp_port->qdisc);
}
