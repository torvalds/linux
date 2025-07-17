// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2021 Broadcom. All Rights Reserved. The term
 * “Broadcom” refers to Broadcom Inc. and/or its subsidiaries.
 */

#include "efct_driver.h"
#include "efct_unsol.h"

static struct dentry *efct_debugfs_root;
static atomic_t efct_debugfs_count;

static const struct scsi_host_template efct_template = {
	.module			= THIS_MODULE,
	.name			= EFCT_DRIVER_NAME,
	.supported_mode		= MODE_TARGET,
};

/* globals */
static struct fc_function_template efct_xport_functions;
static struct fc_function_template efct_vport_functions;

static struct scsi_transport_template *efct_xport_fc_tt;
static struct scsi_transport_template *efct_vport_fc_tt;

struct efct_xport *
efct_xport_alloc(struct efct *efct)
{
	struct efct_xport *xport;

	xport = kzalloc(sizeof(*xport), GFP_KERNEL);
	if (!xport)
		return xport;

	xport->efct = efct;
	return xport;
}

static int
efct_xport_init_debugfs(struct efct *efct)
{
	/* Setup efct debugfs root directory */
	if (!efct_debugfs_root) {
		efct_debugfs_root = debugfs_create_dir("efct", NULL);
		atomic_set(&efct_debugfs_count, 0);
	}

	/* Create a directory for sessions in root */
	if (!efct->sess_debugfs_dir) {
		efct->sess_debugfs_dir = debugfs_create_dir("sessions",
							efct_debugfs_root);
		if (IS_ERR(efct->sess_debugfs_dir)) {
			efc_log_err(efct,
				    "failed to create debugfs entry for sessions\n");
			goto debugfs_fail;
		}
		atomic_inc(&efct_debugfs_count);
	}

	return 0;

debugfs_fail:
	return -EIO;
}

static void efct_xport_delete_debugfs(struct efct *efct)
{
	/* Remove session debugfs directory */
	debugfs_remove(efct->sess_debugfs_dir);
	efct->sess_debugfs_dir = NULL;
	atomic_dec(&efct_debugfs_count);

	if (atomic_read(&efct_debugfs_count) == 0) {
		/* remove root debugfs directory */
		debugfs_remove(efct_debugfs_root);
		efct_debugfs_root = NULL;
	}
}

int
efct_xport_attach(struct efct_xport *xport)
{
	struct efct *efct = xport->efct;
	int rc;

	rc = efct_hw_setup(&efct->hw, efct, efct->pci);
	if (rc) {
		efc_log_err(efct, "%s: Can't setup hardware\n", efct->desc);
		return rc;
	}

	efct_hw_parse_filter(&efct->hw, (void *)efct->filter_def);

	xport->io_pool = efct_io_pool_create(efct, efct->hw.config.n_sgl);
	if (!xport->io_pool) {
		efc_log_err(efct, "Can't allocate IO pool\n");
		return -ENOMEM;
	}

	return 0;
}

static void
efct_xport_link_stats_cb(int status, u32 num_counters,
			 struct efct_hw_link_stat_counts *counters, void *arg)
{
	union efct_xport_stats_u *result = arg;

	result->stats.link_stats.link_failure_error_count =
		counters[EFCT_HW_LINK_STAT_LINK_FAILURE_COUNT].counter;
	result->stats.link_stats.loss_of_sync_error_count =
		counters[EFCT_HW_LINK_STAT_LOSS_OF_SYNC_COUNT].counter;
	result->stats.link_stats.primitive_sequence_error_count =
		counters[EFCT_HW_LINK_STAT_PRIMITIVE_SEQ_COUNT].counter;
	result->stats.link_stats.invalid_transmission_word_error_count =
		counters[EFCT_HW_LINK_STAT_INVALID_XMIT_WORD_COUNT].counter;
	result->stats.link_stats.crc_error_count =
		counters[EFCT_HW_LINK_STAT_CRC_COUNT].counter;

	complete(&result->stats.done);
}

static void
efct_xport_host_stats_cb(int status, u32 num_counters,
			 struct efct_hw_host_stat_counts *counters, void *arg)
{
	union efct_xport_stats_u *result = arg;

	result->stats.host_stats.transmit_kbyte_count =
		counters[EFCT_HW_HOST_STAT_TX_KBYTE_COUNT].counter;
	result->stats.host_stats.receive_kbyte_count =
		counters[EFCT_HW_HOST_STAT_RX_KBYTE_COUNT].counter;
	result->stats.host_stats.transmit_frame_count =
		counters[EFCT_HW_HOST_STAT_TX_FRAME_COUNT].counter;
	result->stats.host_stats.receive_frame_count =
		counters[EFCT_HW_HOST_STAT_RX_FRAME_COUNT].counter;

	complete(&result->stats.done);
}

static void
efct_xport_async_link_stats_cb(int status, u32 num_counters,
			       struct efct_hw_link_stat_counts *counters,
			       void *arg)
{
	union efct_xport_stats_u *result = arg;

	result->stats.link_stats.link_failure_error_count =
		counters[EFCT_HW_LINK_STAT_LINK_FAILURE_COUNT].counter;
	result->stats.link_stats.loss_of_sync_error_count =
		counters[EFCT_HW_LINK_STAT_LOSS_OF_SYNC_COUNT].counter;
	result->stats.link_stats.primitive_sequence_error_count =
		counters[EFCT_HW_LINK_STAT_PRIMITIVE_SEQ_COUNT].counter;
	result->stats.link_stats.invalid_transmission_word_error_count =
		counters[EFCT_HW_LINK_STAT_INVALID_XMIT_WORD_COUNT].counter;
	result->stats.link_stats.crc_error_count =
		counters[EFCT_HW_LINK_STAT_CRC_COUNT].counter;
}

static void
efct_xport_async_host_stats_cb(int status, u32 num_counters,
			       struct efct_hw_host_stat_counts *counters,
			       void *arg)
{
	union efct_xport_stats_u *result = arg;

	result->stats.host_stats.transmit_kbyte_count =
		counters[EFCT_HW_HOST_STAT_TX_KBYTE_COUNT].counter;
	result->stats.host_stats.receive_kbyte_count =
		counters[EFCT_HW_HOST_STAT_RX_KBYTE_COUNT].counter;
	result->stats.host_stats.transmit_frame_count =
		counters[EFCT_HW_HOST_STAT_TX_FRAME_COUNT].counter;
	result->stats.host_stats.receive_frame_count =
		counters[EFCT_HW_HOST_STAT_RX_FRAME_COUNT].counter;
}

static void
efct_xport_config_stats_timer(struct efct *efct);

static void
efct_xport_stats_timer_cb(struct timer_list *t)
{
	struct efct_xport *xport = timer_container_of(xport, t, stats_timer);
	struct efct *efct = xport->efct;

	efct_xport_config_stats_timer(efct);
}

static void
efct_xport_config_stats_timer(struct efct *efct)
{
	u32 timeout = 3 * 1000;
	struct efct_xport *xport = NULL;

	if (!efct) {
		pr_err("%s: failed to locate EFCT device\n", __func__);
		return;
	}

	xport = efct->xport;
	efct_hw_get_link_stats(&efct->hw, 0, 0, 0,
			       efct_xport_async_link_stats_cb,
			       &xport->fc_xport_stats);
	efct_hw_get_host_stats(&efct->hw, 0, efct_xport_async_host_stats_cb,
			       &xport->fc_xport_stats);

	timer_setup(&xport->stats_timer,
		    &efct_xport_stats_timer_cb, 0);
	mod_timer(&xport->stats_timer,
		  jiffies + msecs_to_jiffies(timeout));
}

int
efct_xport_initialize(struct efct_xport *xport)
{
	struct efct *efct = xport->efct;
	int rc = 0;

	/* Initialize io lists */
	spin_lock_init(&xport->io_pending_lock);
	INIT_LIST_HEAD(&xport->io_pending_list);
	atomic_set(&xport->io_active_count, 0);
	atomic_set(&xport->io_pending_count, 0);
	atomic_set(&xport->io_total_free, 0);
	atomic_set(&xport->io_total_pending, 0);
	atomic_set(&xport->io_alloc_failed_count, 0);
	atomic_set(&xport->io_pending_recursing, 0);

	rc = efct_hw_init(&efct->hw);
	if (rc) {
		efc_log_err(efct, "efct_hw_init failure\n");
		goto out;
	}

	rc = efct_scsi_tgt_new_device(efct);
	if (rc) {
		efc_log_err(efct, "failed to initialize target\n");
		goto hw_init_out;
	}

	rc = efct_scsi_new_device(efct);
	if (rc) {
		efc_log_err(efct, "failed to initialize initiator\n");
		goto tgt_dev_out;
	}

	/* Get FC link and host statistics perodically*/
	efct_xport_config_stats_timer(efct);

	efct_xport_init_debugfs(efct);

	return rc;

tgt_dev_out:
	efct_scsi_tgt_del_device(efct);

hw_init_out:
	efct_hw_teardown(&efct->hw);
out:
	return rc;
}

int
efct_xport_status(struct efct_xport *xport, enum efct_xport_status cmd,
		  union efct_xport_stats_u *result)
{
	int rc = 0;
	struct efct *efct = NULL;
	union efct_xport_stats_u value;

	efct = xport->efct;

	switch (cmd) {
	case EFCT_XPORT_CONFIG_PORT_STATUS:
		if (xport->configured_link_state == 0) {
			/*
			 * Initial state is offline. configured_link_state is
			 * set to online explicitly when port is brought online
			 */
			xport->configured_link_state = EFCT_XPORT_PORT_OFFLINE;
		}
		result->value = xport->configured_link_state;
		break;

	case EFCT_XPORT_PORT_STATUS:
		/* Determine port status based on link speed. */
		value.value = efct_hw_get_link_speed(&efct->hw);
		if (value.value == 0)
			result->value = EFCT_XPORT_PORT_OFFLINE;
		else
			result->value = EFCT_XPORT_PORT_ONLINE;
		break;

	case EFCT_XPORT_LINK_SPEED:
		result->value = efct_hw_get_link_speed(&efct->hw);
		break;

	case EFCT_XPORT_LINK_STATISTICS:
		memcpy((void *)result, &efct->xport->fc_xport_stats,
		       sizeof(union efct_xport_stats_u));
		break;
	case EFCT_XPORT_LINK_STAT_RESET: {
		/* Create a completion to synchronize the stat reset process */
		init_completion(&result->stats.done);

		/* First reset the link stats */
		rc = efct_hw_get_link_stats(&efct->hw, 0, 1, 1,
					    efct_xport_link_stats_cb, result);
		if (rc)
			break;

		/* Wait for completion to be signaled when the cmd completes */
		if (wait_for_completion_interruptible(&result->stats.done)) {
			/* Undefined failure */
			efc_log_debug(efct, "sem wait failed\n");
			rc = -EIO;
			break;
		}

		/* Next reset the host stats */
		rc = efct_hw_get_host_stats(&efct->hw, 1,
					    efct_xport_host_stats_cb, result);

		if (rc)
			break;

		/* Wait for completion to be signaled when the cmd completes */
		if (wait_for_completion_interruptible(&result->stats.done)) {
			/* Undefined failure */
			efc_log_debug(efct, "sem wait failed\n");
			rc = -EIO;
			break;
		}
		break;
	}
	default:
		rc = -EIO;
		break;
	}

	return rc;
}

static int
efct_get_link_supported_speeds(struct efct *efct)
{
	u32 supported_speeds = 0;
	u32 link_module_type, i;
	struct {
		u32 lmt_speed;
		u32 speed;
	} supported_speed_list[] = {
		{SLI4_LINK_MODULE_TYPE_1GB, FC_PORTSPEED_1GBIT},
		{SLI4_LINK_MODULE_TYPE_2GB, FC_PORTSPEED_2GBIT},
		{SLI4_LINK_MODULE_TYPE_4GB, FC_PORTSPEED_4GBIT},
		{SLI4_LINK_MODULE_TYPE_8GB, FC_PORTSPEED_8GBIT},
		{SLI4_LINK_MODULE_TYPE_16GB, FC_PORTSPEED_16GBIT},
		{SLI4_LINK_MODULE_TYPE_32GB, FC_PORTSPEED_32GBIT},
		{SLI4_LINK_MODULE_TYPE_64GB, FC_PORTSPEED_64GBIT},
		{SLI4_LINK_MODULE_TYPE_128GB, FC_PORTSPEED_128GBIT},
	};

	link_module_type = sli_get_lmt(&efct->hw.sli);

	/* populate link supported speeds */
	for (i = 0; i < ARRAY_SIZE(supported_speed_list); i++) {
		if (link_module_type & supported_speed_list[i].lmt_speed)
			supported_speeds |= supported_speed_list[i].speed;
	}

	return supported_speeds;
}

int
efct_scsi_new_device(struct efct *efct)
{
	struct Scsi_Host *shost = NULL;
	int error = 0;
	struct efct_vport *vport = NULL;

	shost = scsi_host_alloc(&efct_template, sizeof(*vport));
	if (!shost) {
		efc_log_err(efct, "failed to allocate Scsi_Host struct\n");
		return -ENOMEM;
	}

	/* save shost to initiator-client context */
	efct->shost = shost;

	/* save efct information to shost LLD-specific space */
	vport = (struct efct_vport *)shost->hostdata;
	vport->efct = efct;

	/*
	 * Set initial can_queue value to the max SCSI IOs. This is the maximum
	 * global queue depth (as opposed to the per-LUN queue depth --
	 * .cmd_per_lun This may need to be adjusted for I+T mode.
	 */
	shost->can_queue = efct->hw.config.n_io;
	shost->max_cmd_len = 16; /* 16-byte CDBs */
	shost->max_id = 0xffff;
	shost->max_lun = 0xffffffff;

	/*
	 * can only accept (from mid-layer) as many SGEs as we've
	 * pre-registered
	 */
	shost->sg_tablesize = sli_get_max_sgl(&efct->hw.sli);

	/* attach FC Transport template to shost */
	shost->transportt = efct_xport_fc_tt;
	efc_log_debug(efct, "transport template=%p\n", efct_xport_fc_tt);

	/* get pci_dev structure and add host to SCSI ML */
	error = scsi_add_host_with_dma(shost, &efct->pci->dev,
				       &efct->pci->dev);
	if (error) {
		efc_log_debug(efct, "failed scsi_add_host_with_dma\n");
		return -EIO;
	}

	/* Set symbolic name for host port */
	snprintf(fc_host_symbolic_name(shost),
		 sizeof(fc_host_symbolic_name(shost)),
		     "Emulex %s FV%s DV%s", efct->model,
		     efct->hw.sli.fw_name[0], EFCT_DRIVER_VERSION);

	/* Set host port supported classes */
	fc_host_supported_classes(shost) = FC_COS_CLASS3;

	fc_host_supported_speeds(shost) = efct_get_link_supported_speeds(efct);

	fc_host_node_name(shost) = efct_get_wwnn(&efct->hw);
	fc_host_port_name(shost) = efct_get_wwpn(&efct->hw);
	fc_host_max_npiv_vports(shost) = 128;

	return 0;
}

struct scsi_transport_template *
efct_attach_fc_transport(void)
{
	struct scsi_transport_template *efct_fc_template = NULL;

	efct_fc_template = fc_attach_transport(&efct_xport_functions);

	if (!efct_fc_template)
		pr_err("failed to attach EFCT with fc transport\n");

	return efct_fc_template;
}

struct scsi_transport_template *
efct_attach_vport_fc_transport(void)
{
	struct scsi_transport_template *efct_fc_template = NULL;

	efct_fc_template = fc_attach_transport(&efct_vport_functions);

	if (!efct_fc_template)
		pr_err("failed to attach EFCT with fc transport\n");

	return efct_fc_template;
}

int
efct_scsi_reg_fc_transport(void)
{
	/* attach to appropriate scsi_tranport_* module */
	efct_xport_fc_tt = efct_attach_fc_transport();
	if (!efct_xport_fc_tt) {
		pr_err("%s: failed to attach to scsi_transport_*", __func__);
		return -EIO;
	}

	efct_vport_fc_tt = efct_attach_vport_fc_transport();
	if (!efct_vport_fc_tt) {
		pr_err("%s: failed to attach to scsi_transport_*", __func__);
		efct_release_fc_transport(efct_xport_fc_tt);
		efct_xport_fc_tt = NULL;
		return -EIO;
	}

	return 0;
}

void
efct_scsi_release_fc_transport(void)
{
	/* detach from scsi_transport_* */
	efct_release_fc_transport(efct_xport_fc_tt);
	efct_xport_fc_tt = NULL;
	if (efct_vport_fc_tt)
		efct_release_fc_transport(efct_vport_fc_tt);

	efct_vport_fc_tt = NULL;
}

void
efct_xport_detach(struct efct_xport *xport)
{
	struct efct *efct = xport->efct;

	/* free resources associated with target-server and initiator-client */
	efct_scsi_tgt_del_device(efct);

	efct_scsi_del_device(efct);

	/*Shutdown FC Statistics timer*/
	if (timer_pending(&xport->stats_timer))
		timer_delete(&xport->stats_timer);

	efct_hw_teardown(&efct->hw);

	efct_xport_delete_debugfs(efct);
}

static void
efct_xport_domain_free_cb(struct efc *efc, void *arg)
{
	struct completion *done = arg;

	complete(done);
}

int
efct_xport_control(struct efct_xport *xport, enum efct_xport_ctrl cmd, ...)
{
	u32 rc = 0;
	struct efct *efct = NULL;
	va_list argp;

	efct = xport->efct;

	switch (cmd) {
	case EFCT_XPORT_PORT_ONLINE: {
		/* Bring the port on-line */
		rc = efct_hw_port_control(&efct->hw, EFCT_HW_PORT_INIT, 0,
					  NULL, NULL);
		if (rc)
			efc_log_err(efct,
				    "%s: Can't init port\n", efct->desc);
		else
			xport->configured_link_state = cmd;
		break;
	}
	case EFCT_XPORT_PORT_OFFLINE: {
		if (efct_hw_port_control(&efct->hw, EFCT_HW_PORT_SHUTDOWN, 0,
					 NULL, NULL))
			efc_log_err(efct, "port shutdown failed\n");
		else
			xport->configured_link_state = cmd;
		break;
	}

	case EFCT_XPORT_SHUTDOWN: {
		struct completion done;
		unsigned long timeout;

		/* if a PHYSDEV reset was performed (e.g. hw dump), will affect
		 * all PCI functions; orderly shutdown won't work,
		 * just force free
		 */
		if (sli_reset_required(&efct->hw.sli)) {
			struct efc_domain *domain = efct->efcport->domain;

			if (domain)
				efc_domain_cb(efct->efcport, EFC_HW_DOMAIN_LOST,
					      domain);
		} else {
			efct_hw_port_control(&efct->hw, EFCT_HW_PORT_SHUTDOWN,
					     0, NULL, NULL);
		}

		init_completion(&done);

		efc_register_domain_free_cb(efct->efcport,
					    efct_xport_domain_free_cb, &done);

		efc_log_debug(efct, "Waiting %d seconds for domain shutdown\n",
			      (EFC_SHUTDOWN_TIMEOUT_USEC / 1000000));

		timeout = usecs_to_jiffies(EFC_SHUTDOWN_TIMEOUT_USEC);
		if (!wait_for_completion_timeout(&done, timeout)) {
			efc_log_err(efct, "Domain shutdown timed out!!\n");
			WARN_ON(1);
		}

		efc_register_domain_free_cb(efct->efcport, NULL, NULL);

		/* Free up any saved virtual ports */
		efc_vport_del_all(efct->efcport);
		break;
	}

	/*
	 * Set wwnn for the port. This will be used instead of the default
	 * provided by FW.
	 */
	case EFCT_XPORT_WWNN_SET: {
		u64 wwnn;

		/* Retrieve arguments */
		va_start(argp, cmd);
		wwnn = va_arg(argp, uint64_t);
		va_end(argp);

		efc_log_debug(efct, " WWNN %016llx\n", wwnn);
		xport->req_wwnn = wwnn;

		break;
	}
	/*
	 * Set wwpn for the port. This will be used instead of the default
	 * provided by FW.
	 */
	case EFCT_XPORT_WWPN_SET: {
		u64 wwpn;

		/* Retrieve arguments */
		va_start(argp, cmd);
		wwpn = va_arg(argp, uint64_t);
		va_end(argp);

		efc_log_debug(efct, " WWPN %016llx\n", wwpn);
		xport->req_wwpn = wwpn;

		break;
	}

	default:
		break;
	}
	return rc;
}

void
efct_xport_free(struct efct_xport *xport)
{
	if (xport) {
		efct_io_pool_free(xport->io_pool);

		kfree(xport);
	}
}

void
efct_release_fc_transport(struct scsi_transport_template *transport_template)
{
	if (transport_template)
		pr_err("releasing transport layer\n");

	/* Releasing FC transport */
	fc_release_transport(transport_template);
}

static void
efct_xport_remove_host(struct Scsi_Host *shost)
{
	fc_remove_host(shost);
}

void
efct_scsi_del_device(struct efct *efct)
{
	if (!efct->shost)
		return;

	efc_log_debug(efct, "Unregistering with Transport Layer\n");
	efct_xport_remove_host(efct->shost);
	efc_log_debug(efct, "Unregistering with SCSI Midlayer\n");
	scsi_remove_host(efct->shost);
	scsi_host_put(efct->shost);
	efct->shost = NULL;
}

static void
efct_get_host_port_id(struct Scsi_Host *shost)
{
	struct efct_vport *vport = (struct efct_vport *)shost->hostdata;
	struct efct *efct = vport->efct;
	struct efc *efc = efct->efcport;
	struct efc_nport *nport;

	if (efc->domain && efc->domain->nport) {
		nport = efc->domain->nport;
		fc_host_port_id(shost) = nport->fc_id;
	}
}

static void
efct_get_host_port_type(struct Scsi_Host *shost)
{
	struct efct_vport *vport = (struct efct_vport *)shost->hostdata;
	struct efct *efct = vport->efct;
	struct efc *efc = efct->efcport;
	int type = FC_PORTTYPE_UNKNOWN;

	if (efc->domain && efc->domain->nport) {
		if (efc->domain->is_loop) {
			type = FC_PORTTYPE_LPORT;
		} else {
			struct efc_nport *nport = efc->domain->nport;

			if (nport->is_vport)
				type = FC_PORTTYPE_NPIV;
			else if (nport->topology == EFC_NPORT_TOPO_P2P)
				type = FC_PORTTYPE_PTP;
			else if (nport->topology == EFC_NPORT_TOPO_UNKNOWN)
				type = FC_PORTTYPE_UNKNOWN;
			else
				type = FC_PORTTYPE_NPORT;
		}
	}
	fc_host_port_type(shost) = type;
}

static void
efct_get_host_vport_type(struct Scsi_Host *shost)
{
	fc_host_port_type(shost) = FC_PORTTYPE_NPIV;
}

static void
efct_get_host_port_state(struct Scsi_Host *shost)
{
	struct efct_vport *vport = (struct efct_vport *)shost->hostdata;
	struct efct *efct = vport->efct;
	union efct_xport_stats_u status;
	int rc;

	rc = efct_xport_status(efct->xport, EFCT_XPORT_PORT_STATUS, &status);
	if ((!rc) && (status.value == EFCT_XPORT_PORT_ONLINE))
		fc_host_port_state(shost) = FC_PORTSTATE_ONLINE;
	else
		fc_host_port_state(shost) = FC_PORTSTATE_OFFLINE;
}

static void
efct_get_host_speed(struct Scsi_Host *shost)
{
	struct efct_vport *vport = (struct efct_vport *)shost->hostdata;
	struct efct *efct = vport->efct;
	struct efc *efc = efct->efcport;
	union efct_xport_stats_u speed;
	u32 fc_speed = FC_PORTSPEED_UNKNOWN;
	int rc;

	if (!efc->domain || !efc->domain->nport) {
		fc_host_speed(shost) = fc_speed;
		return;
	}

	rc = efct_xport_status(efct->xport, EFCT_XPORT_LINK_SPEED, &speed);
	if (!rc) {
		switch (speed.value) {
		case 1000:
			fc_speed = FC_PORTSPEED_1GBIT;
			break;
		case 2000:
			fc_speed = FC_PORTSPEED_2GBIT;
			break;
		case 4000:
			fc_speed = FC_PORTSPEED_4GBIT;
			break;
		case 8000:
			fc_speed = FC_PORTSPEED_8GBIT;
			break;
		case 10000:
			fc_speed = FC_PORTSPEED_10GBIT;
			break;
		case 16000:
			fc_speed = FC_PORTSPEED_16GBIT;
			break;
		case 32000:
			fc_speed = FC_PORTSPEED_32GBIT;
			break;
		case 64000:
			fc_speed = FC_PORTSPEED_64GBIT;
			break;
		case 128000:
			fc_speed = FC_PORTSPEED_128GBIT;
			break;
		}
	}

	fc_host_speed(shost) = fc_speed;
}

static void
efct_get_host_fabric_name(struct Scsi_Host *shost)
{
	struct efct_vport *vport = (struct efct_vport *)shost->hostdata;
	struct efct *efct = vport->efct;
	struct efc *efc = efct->efcport;

	if (efc->domain) {
		struct fc_els_flogi  *sp =
			(struct fc_els_flogi  *)
				efc->domain->flogi_service_params;

		fc_host_fabric_name(shost) = be64_to_cpu(sp->fl_wwnn);
	}
}

static struct fc_host_statistics *
efct_get_stats(struct Scsi_Host *shost)
{
	struct efct_vport *vport = (struct efct_vport *)shost->hostdata;
	struct efct *efct = vport->efct;
	union efct_xport_stats_u stats;
	struct efct_xport *xport = efct->xport;
	int rc = 0;

	rc = efct_xport_status(xport, EFCT_XPORT_LINK_STATISTICS, &stats);
	if (rc) {
		pr_err("efct_xport_status returned non 0 - %d\n", rc);
		return NULL;
	}

	vport->fc_host_stats.loss_of_sync_count =
		stats.stats.link_stats.loss_of_sync_error_count;
	vport->fc_host_stats.link_failure_count =
		stats.stats.link_stats.link_failure_error_count;
	vport->fc_host_stats.prim_seq_protocol_err_count =
		stats.stats.link_stats.primitive_sequence_error_count;
	vport->fc_host_stats.invalid_tx_word_count =
		stats.stats.link_stats.invalid_transmission_word_error_count;
	vport->fc_host_stats.invalid_crc_count =
		stats.stats.link_stats.crc_error_count;
	/* mbox returns kbyte count so we need to convert to words */
	vport->fc_host_stats.tx_words =
		stats.stats.host_stats.transmit_kbyte_count * 256;
	/* mbox returns kbyte count so we need to convert to words */
	vport->fc_host_stats.rx_words =
		stats.stats.host_stats.receive_kbyte_count * 256;
	vport->fc_host_stats.tx_frames =
		stats.stats.host_stats.transmit_frame_count;
	vport->fc_host_stats.rx_frames =
		stats.stats.host_stats.receive_frame_count;

	vport->fc_host_stats.fcp_input_requests =
			xport->fcp_stats.input_requests;
	vport->fc_host_stats.fcp_output_requests =
			xport->fcp_stats.output_requests;
	vport->fc_host_stats.fcp_output_megabytes =
			xport->fcp_stats.output_bytes >> 20;
	vport->fc_host_stats.fcp_input_megabytes =
			xport->fcp_stats.input_bytes >> 20;
	vport->fc_host_stats.fcp_control_requests =
			xport->fcp_stats.control_requests;

	return &vport->fc_host_stats;
}

static void
efct_reset_stats(struct Scsi_Host *shost)
{
	struct efct_vport *vport = (struct efct_vport *)shost->hostdata;
	struct efct *efct = vport->efct;
	/* argument has no purpose for this action */
	union efct_xport_stats_u dummy;
	int rc;

	rc = efct_xport_status(efct->xport, EFCT_XPORT_LINK_STAT_RESET, &dummy);
	if (rc)
		pr_err("efct_xport_status returned non 0 - %d\n", rc);
}

static int
efct_issue_lip(struct Scsi_Host *shost)
{
	struct efct_vport *vport =
			shost ? (struct efct_vport *)shost->hostdata : NULL;
	struct efct *efct = vport ? vport->efct : NULL;

	if (!shost || !vport || !efct) {
		pr_err("%s: shost=%p vport=%p efct=%p\n", __func__,
		       shost, vport, efct);
		return -EPERM;
	}

	/*
	 * Bring the link down gracefully then re-init the link.
	 * The firmware will re-initialize the Fibre Channel interface as
	 * required. It does not issue a LIP.
	 */

	if (efct_xport_control(efct->xport, EFCT_XPORT_PORT_OFFLINE))
		efc_log_debug(efct, "EFCT_XPORT_PORT_OFFLINE failed\n");

	if (efct_xport_control(efct->xport, EFCT_XPORT_PORT_ONLINE))
		efc_log_debug(efct, "EFCT_XPORT_PORT_ONLINE failed\n");

	return 0;
}

struct efct_vport *
efct_scsi_new_vport(struct efct *efct, struct device *dev)
{
	struct Scsi_Host *shost = NULL;
	int error = 0;
	struct efct_vport *vport = NULL;

	shost = scsi_host_alloc(&efct_template, sizeof(*vport));
	if (!shost) {
		efc_log_err(efct, "failed to allocate Scsi_Host struct\n");
		return NULL;
	}

	/* save efct information to shost LLD-specific space */
	vport = (struct efct_vport *)shost->hostdata;
	vport->efct = efct;
	vport->is_vport = true;

	shost->can_queue = efct->hw.config.n_io;
	shost->max_cmd_len = 16; /* 16-byte CDBs */
	shost->max_id = 0xffff;
	shost->max_lun = 0xffffffff;

	/* can only accept (from mid-layer) as many SGEs as we've pre-regited*/
	shost->sg_tablesize = sli_get_max_sgl(&efct->hw.sli);

	/* attach FC Transport template to shost */
	shost->transportt = efct_vport_fc_tt;
	efc_log_debug(efct, "vport transport template=%p\n",
		      efct_vport_fc_tt);

	/* get pci_dev structure and add host to SCSI ML */
	error = scsi_add_host_with_dma(shost, dev, &efct->pci->dev);
	if (error) {
		efc_log_debug(efct, "failed scsi_add_host_with_dma\n");
		return NULL;
	}

	/* Set symbolic name for host port */
	snprintf(fc_host_symbolic_name(shost),
		 sizeof(fc_host_symbolic_name(shost)),
		 "Emulex %s FV%s DV%s", efct->model, efct->hw.sli.fw_name[0],
		 EFCT_DRIVER_VERSION);

	/* Set host port supported classes */
	fc_host_supported_classes(shost) = FC_COS_CLASS3;

	fc_host_supported_speeds(shost) = efct_get_link_supported_speeds(efct);
	vport->shost = shost;

	return vport;
}

int efct_scsi_del_vport(struct efct *efct, struct Scsi_Host *shost)
{
	if (shost) {
		efc_log_debug(efct,
			      "Unregistering vport with Transport Layer\n");
		efct_xport_remove_host(shost);
		efc_log_debug(efct, "Unregistering vport with SCSI Midlayer\n");
		scsi_remove_host(shost);
		scsi_host_put(shost);
		return 0;
	}
	return -EIO;
}

static int
efct_vport_create(struct fc_vport *fc_vport, bool disable)
{
	struct Scsi_Host *shost = fc_vport ? fc_vport->shost : NULL;
	struct efct_vport *pport = shost ?
					(struct efct_vport *)shost->hostdata :
					NULL;
	struct efct *efct = pport ? pport->efct : NULL;
	struct efct_vport *vport = NULL;

	if (!fc_vport || !shost || !efct)
		goto fail;

	vport = efct_scsi_new_vport(efct, &fc_vport->dev);
	if (!vport) {
		efc_log_err(efct, "failed to create vport\n");
		goto fail;
	}

	vport->fc_vport = fc_vport;
	vport->npiv_wwpn = fc_vport->port_name;
	vport->npiv_wwnn = fc_vport->node_name;
	fc_host_node_name(vport->shost) = vport->npiv_wwnn;
	fc_host_port_name(vport->shost) = vport->npiv_wwpn;
	*(struct efct_vport **)fc_vport->dd_data = vport;

	return 0;

fail:
	return -EIO;
}

static int
efct_vport_delete(struct fc_vport *fc_vport)
{
	struct efct_vport *vport = *(struct efct_vport **)fc_vport->dd_data;
	struct Scsi_Host *shost = vport ? vport->shost : NULL;
	struct efct *efct = vport ? vport->efct : NULL;
	int rc;

	rc = efct_scsi_del_vport(efct, shost);

	if (rc)
		pr_err("%s: vport delete failed\n", __func__);

	return rc;
}

static int
efct_vport_disable(struct fc_vport *fc_vport, bool disable)
{
	return 0;
}

static struct fc_function_template efct_xport_functions = {
	.get_host_port_id = efct_get_host_port_id,
	.get_host_port_type = efct_get_host_port_type,
	.get_host_port_state = efct_get_host_port_state,
	.get_host_speed = efct_get_host_speed,
	.get_host_fabric_name = efct_get_host_fabric_name,

	.get_fc_host_stats = efct_get_stats,
	.reset_fc_host_stats = efct_reset_stats,

	.issue_fc_host_lip = efct_issue_lip,

	.vport_disable = efct_vport_disable,

	/* allocation lengths for host-specific data */
	.dd_fcrport_size = sizeof(struct efct_rport_data),
	.dd_fcvport_size = 128, /* should be sizeof(...) */

	/* remote port fixed attributes */
	.show_rport_maxframe_size = 1,
	.show_rport_supported_classes = 1,
	.show_rport_dev_loss_tmo = 1,

	/* target dynamic attributes */
	.show_starget_node_name = 1,
	.show_starget_port_name = 1,
	.show_starget_port_id = 1,

	/* host fixed attributes */
	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_supported_fc4s = 1,
	.show_host_supported_speeds = 1,
	.show_host_maxframe_size = 1,

	/* host dynamic attributes */
	.show_host_port_id = 1,
	.show_host_port_type = 1,
	.show_host_port_state = 1,
	/* active_fc4s is shown but doesn't change (thus no get function) */
	.show_host_active_fc4s = 1,
	.show_host_speed = 1,
	.show_host_fabric_name = 1,
	.show_host_symbolic_name = 1,
	.vport_create = efct_vport_create,
	.vport_delete = efct_vport_delete,
};

static struct fc_function_template efct_vport_functions = {
	.get_host_port_id = efct_get_host_port_id,
	.get_host_port_type = efct_get_host_vport_type,
	.get_host_port_state = efct_get_host_port_state,
	.get_host_speed = efct_get_host_speed,
	.get_host_fabric_name = efct_get_host_fabric_name,

	.get_fc_host_stats = efct_get_stats,
	.reset_fc_host_stats = efct_reset_stats,

	.issue_fc_host_lip = efct_issue_lip,

	/* allocation lengths for host-specific data */
	.dd_fcrport_size = sizeof(struct efct_rport_data),
	.dd_fcvport_size = 128, /* should be sizeof(...) */

	/* remote port fixed attributes */
	.show_rport_maxframe_size = 1,
	.show_rport_supported_classes = 1,
	.show_rport_dev_loss_tmo = 1,

	/* target dynamic attributes */
	.show_starget_node_name = 1,
	.show_starget_port_name = 1,
	.show_starget_port_id = 1,

	/* host fixed attributes */
	.show_host_node_name = 1,
	.show_host_port_name = 1,
	.show_host_supported_classes = 1,
	.show_host_supported_fc4s = 1,
	.show_host_supported_speeds = 1,
	.show_host_maxframe_size = 1,

	/* host dynamic attributes */
	.show_host_port_id = 1,
	.show_host_port_type = 1,
	.show_host_port_state = 1,
	/* active_fc4s is shown but doesn't change (thus no get function) */
	.show_host_active_fc4s = 1,
	.show_host_speed = 1,
	.show_host_fabric_name = 1,
	.show_host_symbolic_name = 1,
};
