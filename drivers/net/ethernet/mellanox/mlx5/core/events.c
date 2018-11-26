// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
// Copyright (c) 2018 Mellanox Technologies

#include <linux/mlx5/driver.h>

#include "mlx5_core.h"
#include "lib/eq.h"
#include "lib/mlx5.h"

struct mlx5_event_nb {
	struct mlx5_nb  nb;
	void           *ctx;
};

/* General events handlers for the low level mlx5_core driver
 *
 * Other Major feature specific events such as
 * clock/eswitch/fpga/FW trace and many others, are handled elsewhere, with
 * separate notifiers callbacks, specifically by those mlx5 components.
 */
static int any_notifier(struct notifier_block *, unsigned long, void *);
static int port_change(struct notifier_block *, unsigned long, void *);
static int general_event(struct notifier_block *, unsigned long, void *);
static int temp_warn(struct notifier_block *, unsigned long, void *);
static int port_module(struct notifier_block *, unsigned long, void *);

/* handler which forwards the event to events->nh, driver notifiers */
static int forward_event(struct notifier_block *, unsigned long, void *);

static struct mlx5_nb events_nbs_ref[] = {
	{.nb.notifier_call = any_notifier,  .event_type = MLX5_EVENT_TYPE_NOTIFY_ANY },
	{.nb.notifier_call = port_change,   .event_type = MLX5_EVENT_TYPE_PORT_CHANGE },
	{.nb.notifier_call = general_event, .event_type = MLX5_EVENT_TYPE_GENERAL_EVENT },
	{.nb.notifier_call = temp_warn,     .event_type = MLX5_EVENT_TYPE_TEMP_WARN_EVENT },
	{.nb.notifier_call = port_module,   .event_type = MLX5_EVENT_TYPE_PORT_MODULE_EVENT },

	/* Events to be forwarded (as is) to mlx5 core interfaces (mlx5e/mlx5_ib) */
	{.nb.notifier_call = forward_event,   .event_type = MLX5_EVENT_TYPE_PORT_CHANGE },
	{.nb.notifier_call = forward_event,   .event_type = MLX5_EVENT_TYPE_GENERAL_EVENT },
};

struct mlx5_events {
	struct mlx5_core_dev *dev;
	struct mlx5_event_nb  notifiers[ARRAY_SIZE(events_nbs_ref)];
	/* driver notifier chain */
	struct atomic_notifier_head nh;
	/* port module events stats */
	struct mlx5_pme_stats pme_stats;
};

static const char *eqe_type_str(u8 type)
{
	switch (type) {
	case MLX5_EVENT_TYPE_COMP:
		return "MLX5_EVENT_TYPE_COMP";
	case MLX5_EVENT_TYPE_PATH_MIG:
		return "MLX5_EVENT_TYPE_PATH_MIG";
	case MLX5_EVENT_TYPE_COMM_EST:
		return "MLX5_EVENT_TYPE_COMM_EST";
	case MLX5_EVENT_TYPE_SQ_DRAINED:
		return "MLX5_EVENT_TYPE_SQ_DRAINED";
	case MLX5_EVENT_TYPE_SRQ_LAST_WQE:
		return "MLX5_EVENT_TYPE_SRQ_LAST_WQE";
	case MLX5_EVENT_TYPE_SRQ_RQ_LIMIT:
		return "MLX5_EVENT_TYPE_SRQ_RQ_LIMIT";
	case MLX5_EVENT_TYPE_CQ_ERROR:
		return "MLX5_EVENT_TYPE_CQ_ERROR";
	case MLX5_EVENT_TYPE_WQ_CATAS_ERROR:
		return "MLX5_EVENT_TYPE_WQ_CATAS_ERROR";
	case MLX5_EVENT_TYPE_PATH_MIG_FAILED:
		return "MLX5_EVENT_TYPE_PATH_MIG_FAILED";
	case MLX5_EVENT_TYPE_WQ_INVAL_REQ_ERROR:
		return "MLX5_EVENT_TYPE_WQ_INVAL_REQ_ERROR";
	case MLX5_EVENT_TYPE_WQ_ACCESS_ERROR:
		return "MLX5_EVENT_TYPE_WQ_ACCESS_ERROR";
	case MLX5_EVENT_TYPE_SRQ_CATAS_ERROR:
		return "MLX5_EVENT_TYPE_SRQ_CATAS_ERROR";
	case MLX5_EVENT_TYPE_INTERNAL_ERROR:
		return "MLX5_EVENT_TYPE_INTERNAL_ERROR";
	case MLX5_EVENT_TYPE_PORT_CHANGE:
		return "MLX5_EVENT_TYPE_PORT_CHANGE";
	case MLX5_EVENT_TYPE_GPIO_EVENT:
		return "MLX5_EVENT_TYPE_GPIO_EVENT";
	case MLX5_EVENT_TYPE_PORT_MODULE_EVENT:
		return "MLX5_EVENT_TYPE_PORT_MODULE_EVENT";
	case MLX5_EVENT_TYPE_TEMP_WARN_EVENT:
		return "MLX5_EVENT_TYPE_TEMP_WARN_EVENT";
	case MLX5_EVENT_TYPE_REMOTE_CONFIG:
		return "MLX5_EVENT_TYPE_REMOTE_CONFIG";
	case MLX5_EVENT_TYPE_DB_BF_CONGESTION:
		return "MLX5_EVENT_TYPE_DB_BF_CONGESTION";
	case MLX5_EVENT_TYPE_STALL_EVENT:
		return "MLX5_EVENT_TYPE_STALL_EVENT";
	case MLX5_EVENT_TYPE_CMD:
		return "MLX5_EVENT_TYPE_CMD";
	case MLX5_EVENT_TYPE_PAGE_REQUEST:
		return "MLX5_EVENT_TYPE_PAGE_REQUEST";
	case MLX5_EVENT_TYPE_PAGE_FAULT:
		return "MLX5_EVENT_TYPE_PAGE_FAULT";
	case MLX5_EVENT_TYPE_PPS_EVENT:
		return "MLX5_EVENT_TYPE_PPS_EVENT";
	case MLX5_EVENT_TYPE_NIC_VPORT_CHANGE:
		return "MLX5_EVENT_TYPE_NIC_VPORT_CHANGE";
	case MLX5_EVENT_TYPE_FPGA_ERROR:
		return "MLX5_EVENT_TYPE_FPGA_ERROR";
	case MLX5_EVENT_TYPE_FPGA_QP_ERROR:
		return "MLX5_EVENT_TYPE_FPGA_QP_ERROR";
	case MLX5_EVENT_TYPE_GENERAL_EVENT:
		return "MLX5_EVENT_TYPE_GENERAL_EVENT";
	case MLX5_EVENT_TYPE_DEVICE_TRACER:
		return "MLX5_EVENT_TYPE_DEVICE_TRACER";
	default:
		return "Unrecognized event";
	}
}

/* handles all FW events, type == eqe->type */
static int any_notifier(struct notifier_block *nb,
			unsigned long type, void *data)
{
	struct mlx5_event_nb *event_nb = mlx5_nb_cof(nb, struct mlx5_event_nb, nb);
	struct mlx5_events   *events   = event_nb->ctx;
	struct mlx5_eqe      *eqe      = data;

	mlx5_core_dbg(events->dev, "Async eqe type %s, subtype (%d)\n",
		      eqe_type_str(eqe->type), eqe->sub_type);
	return NOTIFY_OK;
}

static enum mlx5_dev_event port_subtype2dev(u8 subtype)
{
	switch (subtype) {
	case MLX5_PORT_CHANGE_SUBTYPE_DOWN:
		return MLX5_DEV_EVENT_PORT_DOWN;
	case MLX5_PORT_CHANGE_SUBTYPE_ACTIVE:
		return MLX5_DEV_EVENT_PORT_UP;
	case MLX5_PORT_CHANGE_SUBTYPE_INITIALIZED:
		return MLX5_DEV_EVENT_PORT_INITIALIZED;
	case MLX5_PORT_CHANGE_SUBTYPE_LID:
		return MLX5_DEV_EVENT_LID_CHANGE;
	case MLX5_PORT_CHANGE_SUBTYPE_PKEY:
		return MLX5_DEV_EVENT_PKEY_CHANGE;
	case MLX5_PORT_CHANGE_SUBTYPE_GUID:
		return MLX5_DEV_EVENT_GUID_CHANGE;
	case MLX5_PORT_CHANGE_SUBTYPE_CLIENT_REREG:
		return MLX5_DEV_EVENT_CLIENT_REREG;
	}
	return -1;
}

/* type == MLX5_EVENT_TYPE_PORT_CHANGE */
static int port_change(struct notifier_block *nb,
		       unsigned long type, void *data)
{
	struct mlx5_event_nb *event_nb = mlx5_nb_cof(nb, struct mlx5_event_nb, nb);
	struct mlx5_events   *events   = event_nb->ctx;
	struct mlx5_core_dev *dev      = events->dev;

	bool dev_event_dispatch = false;
	enum mlx5_dev_event dev_event;
	unsigned long dev_event_data;
	struct mlx5_eqe *eqe = data;
	u8 port = (eqe->data.port.port >> 4) & 0xf;

	switch (eqe->sub_type) {
	case MLX5_PORT_CHANGE_SUBTYPE_DOWN:
	case MLX5_PORT_CHANGE_SUBTYPE_ACTIVE:
	case MLX5_PORT_CHANGE_SUBTYPE_LID:
	case MLX5_PORT_CHANGE_SUBTYPE_PKEY:
	case MLX5_PORT_CHANGE_SUBTYPE_GUID:
	case MLX5_PORT_CHANGE_SUBTYPE_CLIENT_REREG:
	case MLX5_PORT_CHANGE_SUBTYPE_INITIALIZED:
		dev_event = port_subtype2dev(eqe->sub_type);
		dev_event_data = (unsigned long)port;
		dev_event_dispatch = true;
		break;
	default:
		mlx5_core_warn(dev, "Port event with unrecognized subtype: port %d, sub_type %d\n",
			       port, eqe->sub_type);
	}

	if (dev_event_dispatch)
		mlx5_notifier_call_chain(events, dev_event, (void *)dev_event_data);

	return NOTIFY_OK;
}

/* type == MLX5_EVENT_TYPE_GENERAL_EVENT */
static int general_event(struct notifier_block *nb, unsigned long type, void *data)
{
	struct mlx5_event_nb *event_nb = mlx5_nb_cof(nb, struct mlx5_event_nb, nb);
	struct mlx5_events   *events   = event_nb->ctx;
	struct mlx5_core_dev *dev      = events->dev;

	bool dev_event_dispatch = false;
	enum mlx5_dev_event dev_event;
	unsigned long dev_event_data;
	struct mlx5_eqe *eqe = data;

	switch (eqe->sub_type) {
	case MLX5_GENERAL_SUBTYPE_DELAY_DROP_TIMEOUT:
		dev_event = MLX5_DEV_EVENT_DELAY_DROP_TIMEOUT;
		dev_event_data = 0;
		dev_event_dispatch = true;
		break;
	default:
		mlx5_core_dbg(dev, "General event with unrecognized subtype: sub_type %d\n",
			      eqe->sub_type);
	}

	if (dev_event_dispatch)
		mlx5_notifier_call_chain(events, dev_event, (void *)dev_event_data);

	return NOTIFY_OK;
}

/* type == MLX5_EVENT_TYPE_TEMP_WARN_EVENT */
static int temp_warn(struct notifier_block *nb, unsigned long type, void *data)
{
	struct mlx5_event_nb *event_nb = mlx5_nb_cof(nb, struct mlx5_event_nb, nb);
	struct mlx5_events   *events   = event_nb->ctx;
	struct mlx5_eqe      *eqe      = data;
	u64 value_lsb;
	u64 value_msb;

	value_lsb = be64_to_cpu(eqe->data.temp_warning.sensor_warning_lsb);
	value_msb = be64_to_cpu(eqe->data.temp_warning.sensor_warning_msb);

	mlx5_core_warn(events->dev,
		       "High temperature on sensors with bit set %llx %llx",
		       value_msb, value_lsb);

	return NOTIFY_OK;
}

/* MLX5_EVENT_TYPE_PORT_MODULE_EVENT */
static const char *mlx5_pme_status[MLX5_MODULE_STATUS_NUM] = {
	"Cable plugged",   /* MLX5_MODULE_STATUS_PLUGGED    = 0x1 */
	"Cable unplugged", /* MLX5_MODULE_STATUS_UNPLUGGED  = 0x2 */
	"Cable error",     /* MLX5_MODULE_STATUS_ERROR      = 0x3 */
};

static const char *mlx5_pme_error[MLX5_MODULE_EVENT_ERROR_NUM] = {
	"Power budget exceeded",
	"Long Range for non MLNX cable",
	"Bus stuck(I2C or data shorted)",
	"No EEPROM/retry timeout",
	"Enforce part number list",
	"Unknown identifier",
	"High Temperature",
	"Bad or shorted cable/module",
	"Unknown status",
};

/* type == MLX5_EVENT_TYPE_PORT_MODULE_EVENT */
static int port_module(struct notifier_block *nb, unsigned long type, void *data)
{
	struct mlx5_event_nb *event_nb = mlx5_nb_cof(nb, struct mlx5_event_nb, nb);
	struct mlx5_events   *events   = event_nb->ctx;
	struct mlx5_eqe      *eqe      = data;

	enum port_module_event_status_type module_status;
	enum port_module_event_error_type error_type;
	struct mlx5_eqe_port_module *module_event_eqe;
	u8 module_num;

	module_event_eqe = &eqe->data.port_module;
	module_num = module_event_eqe->module;
	module_status = module_event_eqe->module_status &
			PORT_MODULE_EVENT_MODULE_STATUS_MASK;
	error_type = module_event_eqe->error_type &
		     PORT_MODULE_EVENT_ERROR_TYPE_MASK;
	if (module_status < MLX5_MODULE_STATUS_ERROR) {
		events->pme_stats.status_counters[module_status - 1]++;
	} else if (module_status == MLX5_MODULE_STATUS_ERROR) {
		if (error_type >= MLX5_MODULE_EVENT_ERROR_UNKNOWN)
			/* Unknown error type */
			error_type = MLX5_MODULE_EVENT_ERROR_UNKNOWN;
		events->pme_stats.error_counters[error_type]++;
	}

	if (!printk_ratelimit())
		return NOTIFY_OK;

	if (module_status < MLX5_MODULE_STATUS_ERROR)
		mlx5_core_info(events->dev,
			       "Port module event: module %u, %s\n",
			       module_num, mlx5_pme_status[module_status - 1]);

	else if (module_status == MLX5_MODULE_STATUS_ERROR)
		mlx5_core_info(events->dev,
			       "Port module event[error]: module %u, %s, %s\n",
			       module_num, mlx5_pme_status[module_status - 1],
			       mlx5_pme_error[error_type]);

	return NOTIFY_OK;
}

void mlx5_get_pme_stats(struct mlx5_core_dev *dev, struct mlx5_pme_stats *stats)
{
	*stats = dev->priv.events->pme_stats;
}

/* forward event as is to registered interfaces (mlx5e/mlx5_ib) */
static int forward_event(struct notifier_block *nb, unsigned long event, void *data)
{
	struct mlx5_event_nb *event_nb = mlx5_nb_cof(nb, struct mlx5_event_nb, nb);
	struct mlx5_events   *events   = event_nb->ctx;

	atomic_notifier_call_chain(&events->nh, event, data);
	return NOTIFY_OK;
}

int mlx5_events_init(struct mlx5_core_dev *dev)
{
	struct mlx5_events *events = kzalloc(sizeof(*events), GFP_KERNEL);

	if (!events)
		return -ENOMEM;

	ATOMIC_INIT_NOTIFIER_HEAD(&events->nh);
	events->dev = dev;
	dev->priv.events = events;
	return 0;
}

void mlx5_events_cleanup(struct mlx5_core_dev *dev)
{
	kvfree(dev->priv.events);
}

void mlx5_events_start(struct mlx5_core_dev *dev)
{
	struct mlx5_events *events = dev->priv.events;
	int i;

	for (i = 0; i < ARRAY_SIZE(events_nbs_ref); i++) {
		events->notifiers[i].nb  = events_nbs_ref[i];
		events->notifiers[i].ctx = events;
		mlx5_eq_notifier_register(dev, &events->notifiers[i].nb);
	}
}

void mlx5_events_stop(struct mlx5_core_dev *dev)
{
	struct mlx5_events *events = dev->priv.events;
	int i;

	for (i = ARRAY_SIZE(events_nbs_ref) - 1; i >= 0 ; i--)
		mlx5_eq_notifier_unregister(dev, &events->notifiers[i].nb);
}

int mlx5_notifier_register(struct mlx5_core_dev *dev, struct notifier_block *nb)
{
	struct mlx5_events *events = dev->priv.events;

	return atomic_notifier_chain_register(&events->nh, nb);
}
EXPORT_SYMBOL(mlx5_notifier_register);

int mlx5_notifier_unregister(struct mlx5_core_dev *dev, struct notifier_block *nb)
{
	struct mlx5_events *events = dev->priv.events;

	return atomic_notifier_chain_unregister(&events->nh, nb);
}
EXPORT_SYMBOL(mlx5_notifier_unregister);

int mlx5_notifier_call_chain(struct mlx5_events *events, unsigned int event, void *data)
{
	return atomic_notifier_call_chain(&events->nh, event, data);
}
