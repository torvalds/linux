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
static int temp_warn(struct notifier_block *, unsigned long, void *);
static int port_module(struct notifier_block *, unsigned long, void *);
static int pcie_core(struct notifier_block *, unsigned long, void *);

/* handler which forwards the event to events->nh, driver notifiers */
static int forward_event(struct notifier_block *, unsigned long, void *);

static struct mlx5_nb events_nbs_ref[] = {
	/* Events to be proccessed by mlx5_core */
	{.nb.notifier_call = any_notifier,  .event_type = MLX5_EVENT_TYPE_NOTIFY_ANY },
	{.nb.notifier_call = temp_warn,     .event_type = MLX5_EVENT_TYPE_TEMP_WARN_EVENT },
	{.nb.notifier_call = port_module,   .event_type = MLX5_EVENT_TYPE_PORT_MODULE_EVENT },
	{.nb.notifier_call = pcie_core,     .event_type = MLX5_EVENT_TYPE_GENERAL_EVENT },

	/* Events to be forwarded (as is) to mlx5 core interfaces (mlx5e/mlx5_ib) */
	{.nb.notifier_call = forward_event,   .event_type = MLX5_EVENT_TYPE_PORT_CHANGE },
	{.nb.notifier_call = forward_event,   .event_type = MLX5_EVENT_TYPE_GENERAL_EVENT },
	/* QP/WQ resource events to forward */
	{.nb.notifier_call = forward_event,   .event_type = MLX5_EVENT_TYPE_DCT_DRAINED },
	{.nb.notifier_call = forward_event,   .event_type = MLX5_EVENT_TYPE_PATH_MIG },
	{.nb.notifier_call = forward_event,   .event_type = MLX5_EVENT_TYPE_COMM_EST },
	{.nb.notifier_call = forward_event,   .event_type = MLX5_EVENT_TYPE_SQ_DRAINED },
	{.nb.notifier_call = forward_event,   .event_type = MLX5_EVENT_TYPE_SRQ_LAST_WQE },
	{.nb.notifier_call = forward_event,   .event_type = MLX5_EVENT_TYPE_WQ_CATAS_ERROR },
	{.nb.notifier_call = forward_event,   .event_type = MLX5_EVENT_TYPE_PATH_MIG_FAILED },
	{.nb.notifier_call = forward_event,   .event_type = MLX5_EVENT_TYPE_WQ_INVAL_REQ_ERROR },
	{.nb.notifier_call = forward_event,   .event_type = MLX5_EVENT_TYPE_WQ_ACCESS_ERROR },
	/* SRQ events */
	{.nb.notifier_call = forward_event,   .event_type = MLX5_EVENT_TYPE_SRQ_CATAS_ERROR },
	{.nb.notifier_call = forward_event,   .event_type = MLX5_EVENT_TYPE_SRQ_RQ_LIMIT },
};

struct mlx5_events {
	struct mlx5_core_dev *dev;
	struct workqueue_struct *wq;
	struct mlx5_event_nb  notifiers[ARRAY_SIZE(events_nbs_ref)];
	/* driver notifier chain */
	struct atomic_notifier_head nh;
	/* port module events stats */
	struct mlx5_pme_stats pme_stats;
	/*pcie_core*/
	struct work_struct pcie_core_work;
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
	case MLX5_EVENT_TYPE_ESW_FUNCTIONS_CHANGED:
		return "MLX5_EVENT_TYPE_ESW_FUNCTIONS_CHANGED";
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
	case MLX5_EVENT_TYPE_MONITOR_COUNTER:
		return "MLX5_EVENT_TYPE_MONITOR_COUNTER";
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
static const char *mlx5_pme_status_to_string(enum port_module_event_status_type status)
{
	switch (status) {
	case MLX5_MODULE_STATUS_PLUGGED:
		return "Cable plugged";
	case MLX5_MODULE_STATUS_UNPLUGGED:
		return "Cable unplugged";
	case MLX5_MODULE_STATUS_ERROR:
		return "Cable error";
	case MLX5_MODULE_STATUS_DISABLED:
		return "Cable disabled";
	default:
		return "Unknown status";
	}
}

static const char *mlx5_pme_error_to_string(enum port_module_event_error_type error)
{
	switch (error) {
	case MLX5_MODULE_EVENT_ERROR_POWER_BUDGET_EXCEEDED:
		return "Power budget exceeded";
	case MLX5_MODULE_EVENT_ERROR_LONG_RANGE_FOR_NON_MLNX:
		return "Long Range for non MLNX cable";
	case MLX5_MODULE_EVENT_ERROR_BUS_STUCK:
		return "Bus stuck (I2C or data shorted)";
	case MLX5_MODULE_EVENT_ERROR_NO_EEPROM_RETRY_TIMEOUT:
		return "No EEPROM/retry timeout";
	case MLX5_MODULE_EVENT_ERROR_ENFORCE_PART_NUMBER_LIST:
		return "Enforce part number list";
	case MLX5_MODULE_EVENT_ERROR_UNKNOWN_IDENTIFIER:
		return "Unknown identifier";
	case MLX5_MODULE_EVENT_ERROR_HIGH_TEMPERATURE:
		return "High Temperature";
	case MLX5_MODULE_EVENT_ERROR_BAD_CABLE:
		return "Bad or shorted cable/module";
	case MLX5_MODULE_EVENT_ERROR_PCIE_POWER_SLOT_EXCEEDED:
		return "One or more network ports have been powered down due to insufficient/unadvertised power on the PCIe slot";
	default:
		return "Unknown error";
	}
}

/* type == MLX5_EVENT_TYPE_PORT_MODULE_EVENT */
static int port_module(struct notifier_block *nb, unsigned long type, void *data)
{
	struct mlx5_event_nb *event_nb = mlx5_nb_cof(nb, struct mlx5_event_nb, nb);
	struct mlx5_events   *events   = event_nb->ctx;
	struct mlx5_eqe      *eqe      = data;

	enum port_module_event_status_type module_status;
	enum port_module_event_error_type error_type;
	struct mlx5_eqe_port_module *module_event_eqe;
	const char *status_str;
	u8 module_num;

	module_event_eqe = &eqe->data.port_module;
	module_status = module_event_eqe->module_status &
			PORT_MODULE_EVENT_MODULE_STATUS_MASK;
	error_type = module_event_eqe->error_type &
		     PORT_MODULE_EVENT_ERROR_TYPE_MASK;

	if (module_status < MLX5_MODULE_STATUS_NUM)
		events->pme_stats.status_counters[module_status]++;

	if (module_status == MLX5_MODULE_STATUS_ERROR)
		if (error_type < MLX5_MODULE_EVENT_ERROR_NUM)
			events->pme_stats.error_counters[error_type]++;

	if (!printk_ratelimit())
		return NOTIFY_OK;

	module_num = module_event_eqe->module;
	status_str = mlx5_pme_status_to_string(module_status);
	if (module_status == MLX5_MODULE_STATUS_ERROR) {
		const char *error_str = mlx5_pme_error_to_string(error_type);

		mlx5_core_err(events->dev,
			      "Port module event[error]: module %u, %s, %s\n",
			      module_num, status_str, error_str);
	} else {
		mlx5_core_info(events->dev,
			       "Port module event: module %u, %s\n",
			       module_num, status_str);
	}

	return NOTIFY_OK;
}

enum {
	MLX5_PCI_POWER_COULD_NOT_BE_READ = 0x0,
	MLX5_PCI_POWER_SUFFICIENT_REPORTED = 0x1,
	MLX5_PCI_POWER_INSUFFICIENT_REPORTED = 0x2,
};

static void mlx5_pcie_event(struct work_struct *work)
{
	u32 out[MLX5_ST_SZ_DW(mpein_reg)] = {0};
	u32 in[MLX5_ST_SZ_DW(mpein_reg)] = {0};
	struct mlx5_events *events;
	struct mlx5_core_dev *dev;
	u8 power_status;
	u16 pci_power;

	events = container_of(work, struct mlx5_events, pcie_core_work);
	dev  = events->dev;

	if (!MLX5_CAP_MCAM_FEATURE(dev, pci_status_and_power))
		return;

	mlx5_core_access_reg(dev, in, sizeof(in), out, sizeof(out),
			     MLX5_REG_MPEIN, 0, 0);
	power_status = MLX5_GET(mpein_reg, out, pwr_status);
	pci_power = MLX5_GET(mpein_reg, out, pci_power);

	switch (power_status) {
	case MLX5_PCI_POWER_COULD_NOT_BE_READ:
		mlx5_core_info_rl(dev,
				  "PCIe slot power capability was not advertised.\n");
		break;
	case MLX5_PCI_POWER_INSUFFICIENT_REPORTED:
		mlx5_core_warn_rl(dev,
				  "Detected insufficient power on the PCIe slot (%uW).\n",
				  pci_power);
		break;
	case MLX5_PCI_POWER_SUFFICIENT_REPORTED:
		mlx5_core_info_rl(dev,
				  "PCIe slot advertised sufficient power (%uW).\n",
				  pci_power);
		break;
	}
}

static int pcie_core(struct notifier_block *nb, unsigned long type, void *data)
{
	struct mlx5_event_nb    *event_nb = mlx5_nb_cof(nb,
							struct mlx5_event_nb,
							nb);
	struct mlx5_events      *events   = event_nb->ctx;
	struct mlx5_eqe         *eqe      = data;

	switch (eqe->sub_type) {
	case MLX5_GENERAL_SUBTYPE_PCI_POWER_CHANGE_EVENT:
			queue_work(events->wq, &events->pcie_core_work);
		break;
	default:
		return NOTIFY_DONE;
	}

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
	struct mlx5_eqe      *eqe      = data;

	mlx5_core_dbg(events->dev, "Async eqe type %s, subtype (%d) forward to interfaces\n",
		      eqe_type_str(eqe->type), eqe->sub_type);
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
	events->wq = create_singlethread_workqueue("mlx5_events");
	if (!events->wq) {
		kfree(events);
		return -ENOMEM;
	}
	INIT_WORK(&events->pcie_core_work, mlx5_pcie_event);

	return 0;
}

void mlx5_events_cleanup(struct mlx5_core_dev *dev)
{
	destroy_workqueue(dev->priv.events->wq);
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
	flush_workqueue(events->wq);
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
