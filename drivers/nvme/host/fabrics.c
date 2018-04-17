/*
 * NVMe over Fabrics common host code.
 * Copyright (c) 2015-2016 HGST, a Western Digital Company.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt
#include <linux/init.h>
#include <linux/miscdevice.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/parser.h>
#include <linux/seq_file.h>
#include "nvme.h"
#include "fabrics.h"

static LIST_HEAD(nvmf_transports);
static DECLARE_RWSEM(nvmf_transports_rwsem);

static LIST_HEAD(nvmf_hosts);
static DEFINE_MUTEX(nvmf_hosts_mutex);

static struct nvmf_host *nvmf_default_host;

static struct nvmf_host *__nvmf_host_find(const char *hostnqn)
{
	struct nvmf_host *host;

	list_for_each_entry(host, &nvmf_hosts, list) {
		if (!strcmp(host->nqn, hostnqn))
			return host;
	}

	return NULL;
}

static struct nvmf_host *nvmf_host_add(const char *hostnqn)
{
	struct nvmf_host *host;

	mutex_lock(&nvmf_hosts_mutex);
	host = __nvmf_host_find(hostnqn);
	if (host) {
		kref_get(&host->ref);
		goto out_unlock;
	}

	host = kmalloc(sizeof(*host), GFP_KERNEL);
	if (!host)
		goto out_unlock;

	kref_init(&host->ref);
	memcpy(host->nqn, hostnqn, NVMF_NQN_SIZE);

	list_add_tail(&host->list, &nvmf_hosts);
out_unlock:
	mutex_unlock(&nvmf_hosts_mutex);
	return host;
}

static struct nvmf_host *nvmf_host_default(void)
{
	struct nvmf_host *host;

	host = kmalloc(sizeof(*host), GFP_KERNEL);
	if (!host)
		return NULL;

	kref_init(&host->ref);
	uuid_gen(&host->id);
	snprintf(host->nqn, NVMF_NQN_SIZE,
		"nqn.2014-08.org.nvmexpress:uuid:%pUb", &host->id);

	mutex_lock(&nvmf_hosts_mutex);
	list_add_tail(&host->list, &nvmf_hosts);
	mutex_unlock(&nvmf_hosts_mutex);

	return host;
}

static void nvmf_host_destroy(struct kref *ref)
{
	struct nvmf_host *host = container_of(ref, struct nvmf_host, ref);

	mutex_lock(&nvmf_hosts_mutex);
	list_del(&host->list);
	mutex_unlock(&nvmf_hosts_mutex);

	kfree(host);
}

static void nvmf_host_put(struct nvmf_host *host)
{
	if (host)
		kref_put(&host->ref, nvmf_host_destroy);
}

/**
 * nvmf_get_address() -  Get address/port
 * @ctrl:	Host NVMe controller instance which we got the address
 * @buf:	OUTPUT parameter that will contain the address/port
 * @size:	buffer size
 */
int nvmf_get_address(struct nvme_ctrl *ctrl, char *buf, int size)
{
	int len = 0;

	if (ctrl->opts->mask & NVMF_OPT_TRADDR)
		len += snprintf(buf, size, "traddr=%s", ctrl->opts->traddr);
	if (ctrl->opts->mask & NVMF_OPT_TRSVCID)
		len += snprintf(buf + len, size - len, "%strsvcid=%s",
				(len) ? "," : "", ctrl->opts->trsvcid);
	if (ctrl->opts->mask & NVMF_OPT_HOST_TRADDR)
		len += snprintf(buf + len, size - len, "%shost_traddr=%s",
				(len) ? "," : "", ctrl->opts->host_traddr);
	len += snprintf(buf + len, size - len, "\n");

	return len;
}
EXPORT_SYMBOL_GPL(nvmf_get_address);

/**
 * nvmf_reg_read32() -  NVMe Fabrics "Property Get" API function.
 * @ctrl:	Host NVMe controller instance maintaining the admin
 *		queue used to submit the property read command to
 *		the allocated NVMe controller resource on the target system.
 * @off:	Starting offset value of the targeted property
 *		register (see the fabrics section of the NVMe standard).
 * @val:	OUTPUT parameter that will contain the value of
 *		the property after a successful read.
 *
 * Used by the host system to retrieve a 32-bit capsule property value
 * from an NVMe controller on the target system.
 *
 * ("Capsule property" is an "PCIe register concept" applied to the
 * NVMe fabrics space.)
 *
 * Return:
 *	0: successful read
 *	> 0: NVMe error status code
 *	< 0: Linux errno error code
 */
int nvmf_reg_read32(struct nvme_ctrl *ctrl, u32 off, u32 *val)
{
	struct nvme_command cmd;
	union nvme_result res;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.prop_get.opcode = nvme_fabrics_command;
	cmd.prop_get.fctype = nvme_fabrics_type_property_get;
	cmd.prop_get.offset = cpu_to_le32(off);

	ret = __nvme_submit_sync_cmd(ctrl->admin_q, &cmd, &res, NULL, 0, 0,
			NVME_QID_ANY, 0, 0);

	if (ret >= 0)
		*val = le64_to_cpu(res.u64);
	if (unlikely(ret != 0))
		dev_err(ctrl->device,
			"Property Get error: %d, offset %#x\n",
			ret > 0 ? ret & ~NVME_SC_DNR : ret, off);

	return ret;
}
EXPORT_SYMBOL_GPL(nvmf_reg_read32);

/**
 * nvmf_reg_read64() -  NVMe Fabrics "Property Get" API function.
 * @ctrl:	Host NVMe controller instance maintaining the admin
 *		queue used to submit the property read command to
 *		the allocated controller resource on the target system.
 * @off:	Starting offset value of the targeted property
 *		register (see the fabrics section of the NVMe standard).
 * @val:	OUTPUT parameter that will contain the value of
 *		the property after a successful read.
 *
 * Used by the host system to retrieve a 64-bit capsule property value
 * from an NVMe controller on the target system.
 *
 * ("Capsule property" is an "PCIe register concept" applied to the
 * NVMe fabrics space.)
 *
 * Return:
 *	0: successful read
 *	> 0: NVMe error status code
 *	< 0: Linux errno error code
 */
int nvmf_reg_read64(struct nvme_ctrl *ctrl, u32 off, u64 *val)
{
	struct nvme_command cmd;
	union nvme_result res;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.prop_get.opcode = nvme_fabrics_command;
	cmd.prop_get.fctype = nvme_fabrics_type_property_get;
	cmd.prop_get.attrib = 1;
	cmd.prop_get.offset = cpu_to_le32(off);

	ret = __nvme_submit_sync_cmd(ctrl->admin_q, &cmd, &res, NULL, 0, 0,
			NVME_QID_ANY, 0, 0);

	if (ret >= 0)
		*val = le64_to_cpu(res.u64);
	if (unlikely(ret != 0))
		dev_err(ctrl->device,
			"Property Get error: %d, offset %#x\n",
			ret > 0 ? ret & ~NVME_SC_DNR : ret, off);
	return ret;
}
EXPORT_SYMBOL_GPL(nvmf_reg_read64);

/**
 * nvmf_reg_write32() -  NVMe Fabrics "Property Write" API function.
 * @ctrl:	Host NVMe controller instance maintaining the admin
 *		queue used to submit the property read command to
 *		the allocated NVMe controller resource on the target system.
 * @off:	Starting offset value of the targeted property
 *		register (see the fabrics section of the NVMe standard).
 * @val:	Input parameter that contains the value to be
 *		written to the property.
 *
 * Used by the NVMe host system to write a 32-bit capsule property value
 * to an NVMe controller on the target system.
 *
 * ("Capsule property" is an "PCIe register concept" applied to the
 * NVMe fabrics space.)
 *
 * Return:
 *	0: successful write
 *	> 0: NVMe error status code
 *	< 0: Linux errno error code
 */
int nvmf_reg_write32(struct nvme_ctrl *ctrl, u32 off, u32 val)
{
	struct nvme_command cmd;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.prop_set.opcode = nvme_fabrics_command;
	cmd.prop_set.fctype = nvme_fabrics_type_property_set;
	cmd.prop_set.attrib = 0;
	cmd.prop_set.offset = cpu_to_le32(off);
	cmd.prop_set.value = cpu_to_le64(val);

	ret = __nvme_submit_sync_cmd(ctrl->admin_q, &cmd, NULL, NULL, 0, 0,
			NVME_QID_ANY, 0, 0);
	if (unlikely(ret))
		dev_err(ctrl->device,
			"Property Set error: %d, offset %#x\n",
			ret > 0 ? ret & ~NVME_SC_DNR : ret, off);
	return ret;
}
EXPORT_SYMBOL_GPL(nvmf_reg_write32);

/**
 * nvmf_log_connect_error() - Error-parsing-diagnostic print
 * out function for connect() errors.
 *
 * @ctrl: the specific /dev/nvmeX device that had the error.
 *
 * @errval: Error code to be decoded in a more human-friendly
 *	    printout.
 *
 * @offset: For use with the NVMe error code NVME_SC_CONNECT_INVALID_PARAM.
 *
 * @cmd: This is the SQE portion of a submission capsule.
 *
 * @data: This is the "Data" portion of a submission capsule.
 */
static void nvmf_log_connect_error(struct nvme_ctrl *ctrl,
		int errval, int offset, struct nvme_command *cmd,
		struct nvmf_connect_data *data)
{
	int err_sctype = errval & (~NVME_SC_DNR);

	switch (err_sctype) {

	case (NVME_SC_CONNECT_INVALID_PARAM):
		if (offset >> 16) {
			char *inv_data = "Connect Invalid Data Parameter";

			switch (offset & 0xffff) {
			case (offsetof(struct nvmf_connect_data, cntlid)):
				dev_err(ctrl->device,
					"%s, cntlid: %d\n",
					inv_data, data->cntlid);
				break;
			case (offsetof(struct nvmf_connect_data, hostnqn)):
				dev_err(ctrl->device,
					"%s, hostnqn \"%s\"\n",
					inv_data, data->hostnqn);
				break;
			case (offsetof(struct nvmf_connect_data, subsysnqn)):
				dev_err(ctrl->device,
					"%s, subsysnqn \"%s\"\n",
					inv_data, data->subsysnqn);
				break;
			default:
				dev_err(ctrl->device,
					"%s, starting byte offset: %d\n",
				       inv_data, offset & 0xffff);
				break;
			}
		} else {
			char *inv_sqe = "Connect Invalid SQE Parameter";

			switch (offset) {
			case (offsetof(struct nvmf_connect_command, qid)):
				dev_err(ctrl->device,
				       "%s, qid %d\n",
					inv_sqe, cmd->connect.qid);
				break;
			default:
				dev_err(ctrl->device,
					"%s, starting byte offset: %d\n",
					inv_sqe, offset);
			}
		}
		break;

	case NVME_SC_CONNECT_INVALID_HOST:
		dev_err(ctrl->device,
			"Connect for subsystem %s is not allowed, hostnqn: %s\n",
			data->subsysnqn, data->hostnqn);
		break;

	case NVME_SC_CONNECT_CTRL_BUSY:
		dev_err(ctrl->device,
			"Connect command failed: controller is busy or not available\n");
		break;

	case NVME_SC_CONNECT_FORMAT:
		dev_err(ctrl->device,
			"Connect incompatible format: %d",
			cmd->connect.recfmt);
		break;

	default:
		dev_err(ctrl->device,
			"Connect command failed, error wo/DNR bit: %d\n",
			err_sctype);
		break;
	} /* switch (err_sctype) */
}

/**
 * nvmf_connect_admin_queue() - NVMe Fabrics Admin Queue "Connect"
 *				API function.
 * @ctrl:	Host nvme controller instance used to request
 *              a new NVMe controller allocation on the target
 *              system and  establish an NVMe Admin connection to
 *              that controller.
 *
 * This function enables an NVMe host device to request a new allocation of
 * an NVMe controller resource on a target system as well establish a
 * fabrics-protocol connection of the NVMe Admin queue between the
 * host system device and the allocated NVMe controller on the
 * target system via a NVMe Fabrics "Connect" command.
 *
 * Return:
 *	0: success
 *	> 0: NVMe error status code
 *	< 0: Linux errno error code
 *
 */
int nvmf_connect_admin_queue(struct nvme_ctrl *ctrl)
{
	struct nvme_command cmd;
	union nvme_result res;
	struct nvmf_connect_data *data;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.connect.opcode = nvme_fabrics_command;
	cmd.connect.fctype = nvme_fabrics_type_connect;
	cmd.connect.qid = 0;
	cmd.connect.sqsize = cpu_to_le16(NVME_AQ_DEPTH - 1);

	/*
	 * Set keep-alive timeout in seconds granularity (ms * 1000)
	 * and add a grace period for controller kato enforcement
	 */
	cmd.connect.kato = ctrl->opts->discovery_nqn ? 0 :
		cpu_to_le32((ctrl->kato + NVME_KATO_GRACE) * 1000);

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	uuid_copy(&data->hostid, &ctrl->opts->host->id);
	data->cntlid = cpu_to_le16(0xffff);
	strncpy(data->subsysnqn, ctrl->opts->subsysnqn, NVMF_NQN_SIZE);
	strncpy(data->hostnqn, ctrl->opts->host->nqn, NVMF_NQN_SIZE);

	ret = __nvme_submit_sync_cmd(ctrl->admin_q, &cmd, &res,
			data, sizeof(*data), 0, NVME_QID_ANY, 1,
			BLK_MQ_REQ_RESERVED | BLK_MQ_REQ_NOWAIT);
	if (ret) {
		nvmf_log_connect_error(ctrl, ret, le32_to_cpu(res.u32),
				       &cmd, data);
		goto out_free_data;
	}

	ctrl->cntlid = le16_to_cpu(res.u16);

out_free_data:
	kfree(data);
	return ret;
}
EXPORT_SYMBOL_GPL(nvmf_connect_admin_queue);

/**
 * nvmf_connect_io_queue() - NVMe Fabrics I/O Queue "Connect"
 *			     API function.
 * @ctrl:	Host nvme controller instance used to establish an
 *		NVMe I/O queue connection to the already allocated NVMe
 *		controller on the target system.
 * @qid:	NVMe I/O queue number for the new I/O connection between
 *		host and target (note qid == 0 is illegal as this is
 *		the Admin queue, per NVMe standard).
 *
 * This function issues a fabrics-protocol connection
 * of a NVMe I/O queue (via NVMe Fabrics "Connect" command)
 * between the host system device and the allocated NVMe controller
 * on the target system.
 *
 * Return:
 *	0: success
 *	> 0: NVMe error status code
 *	< 0: Linux errno error code
 */
int nvmf_connect_io_queue(struct nvme_ctrl *ctrl, u16 qid)
{
	struct nvme_command cmd;
	struct nvmf_connect_data *data;
	union nvme_result res;
	int ret;

	memset(&cmd, 0, sizeof(cmd));
	cmd.connect.opcode = nvme_fabrics_command;
	cmd.connect.fctype = nvme_fabrics_type_connect;
	cmd.connect.qid = cpu_to_le16(qid);
	cmd.connect.sqsize = cpu_to_le16(ctrl->sqsize);

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	uuid_copy(&data->hostid, &ctrl->opts->host->id);
	data->cntlid = cpu_to_le16(ctrl->cntlid);
	strncpy(data->subsysnqn, ctrl->opts->subsysnqn, NVMF_NQN_SIZE);
	strncpy(data->hostnqn, ctrl->opts->host->nqn, NVMF_NQN_SIZE);

	ret = __nvme_submit_sync_cmd(ctrl->connect_q, &cmd, &res,
			data, sizeof(*data), 0, qid, 1,
			BLK_MQ_REQ_RESERVED | BLK_MQ_REQ_NOWAIT);
	if (ret) {
		nvmf_log_connect_error(ctrl, ret, le32_to_cpu(res.u32),
				       &cmd, data);
	}
	kfree(data);
	return ret;
}
EXPORT_SYMBOL_GPL(nvmf_connect_io_queue);

bool nvmf_should_reconnect(struct nvme_ctrl *ctrl)
{
	if (ctrl->opts->max_reconnects != -1 &&
	    ctrl->nr_reconnects < ctrl->opts->max_reconnects)
		return true;

	return false;
}
EXPORT_SYMBOL_GPL(nvmf_should_reconnect);

/**
 * nvmf_register_transport() - NVMe Fabrics Library registration function.
 * @ops:	Transport ops instance to be registered to the
 *		common fabrics library.
 *
 * API function that registers the type of specific transport fabric
 * being implemented to the common NVMe fabrics library. Part of
 * the overall init sequence of starting up a fabrics driver.
 */
int nvmf_register_transport(struct nvmf_transport_ops *ops)
{
	if (!ops->create_ctrl || !ops->module)
		return -EINVAL;

	down_write(&nvmf_transports_rwsem);
	list_add_tail(&ops->entry, &nvmf_transports);
	up_write(&nvmf_transports_rwsem);

	return 0;
}
EXPORT_SYMBOL_GPL(nvmf_register_transport);

/**
 * nvmf_unregister_transport() - NVMe Fabrics Library unregistration function.
 * @ops:	Transport ops instance to be unregistered from the
 *		common fabrics library.
 *
 * Fabrics API function that unregisters the type of specific transport
 * fabric being implemented from the common NVMe fabrics library.
 * Part of the overall exit sequence of unloading the implemented driver.
 */
void nvmf_unregister_transport(struct nvmf_transport_ops *ops)
{
	down_write(&nvmf_transports_rwsem);
	list_del(&ops->entry);
	up_write(&nvmf_transports_rwsem);
}
EXPORT_SYMBOL_GPL(nvmf_unregister_transport);

static struct nvmf_transport_ops *nvmf_lookup_transport(
		struct nvmf_ctrl_options *opts)
{
	struct nvmf_transport_ops *ops;

	lockdep_assert_held(&nvmf_transports_rwsem);

	list_for_each_entry(ops, &nvmf_transports, entry) {
		if (strcmp(ops->name, opts->transport) == 0)
			return ops;
	}

	return NULL;
}

static const match_table_t opt_tokens = {
	{ NVMF_OPT_TRANSPORT,		"transport=%s"		},
	{ NVMF_OPT_TRADDR,		"traddr=%s"		},
	{ NVMF_OPT_TRSVCID,		"trsvcid=%s"		},
	{ NVMF_OPT_NQN,			"nqn=%s"		},
	{ NVMF_OPT_QUEUE_SIZE,		"queue_size=%d"		},
	{ NVMF_OPT_NR_IO_QUEUES,	"nr_io_queues=%d"	},
	{ NVMF_OPT_RECONNECT_DELAY,	"reconnect_delay=%d"	},
	{ NVMF_OPT_CTRL_LOSS_TMO,	"ctrl_loss_tmo=%d"	},
	{ NVMF_OPT_KATO,		"keep_alive_tmo=%d"	},
	{ NVMF_OPT_HOSTNQN,		"hostnqn=%s"		},
	{ NVMF_OPT_HOST_TRADDR,		"host_traddr=%s"	},
	{ NVMF_OPT_HOST_ID,		"hostid=%s"		},
	{ NVMF_OPT_DUP_CONNECT,		"duplicate_connect"	},
	{ NVMF_OPT_ERR,			NULL			}
};

static int nvmf_parse_options(struct nvmf_ctrl_options *opts,
		const char *buf)
{
	substring_t args[MAX_OPT_ARGS];
	char *options, *o, *p;
	int token, ret = 0;
	size_t nqnlen  = 0;
	int ctrl_loss_tmo = NVMF_DEF_CTRL_LOSS_TMO;
	uuid_t hostid;

	/* Set defaults */
	opts->queue_size = NVMF_DEF_QUEUE_SIZE;
	opts->nr_io_queues = num_online_cpus();
	opts->reconnect_delay = NVMF_DEF_RECONNECT_DELAY;
	opts->kato = NVME_DEFAULT_KATO;
	opts->duplicate_connect = false;

	options = o = kstrdup(buf, GFP_KERNEL);
	if (!options)
		return -ENOMEM;

	uuid_gen(&hostid);

	while ((p = strsep(&o, ",\n")) != NULL) {
		if (!*p)
			continue;

		token = match_token(p, opt_tokens, args);
		opts->mask |= token;
		switch (token) {
		case NVMF_OPT_TRANSPORT:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			opts->transport = p;
			break;
		case NVMF_OPT_NQN:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			opts->subsysnqn = p;
			nqnlen = strlen(opts->subsysnqn);
			if (nqnlen >= NVMF_NQN_SIZE) {
				pr_err("%s needs to be < %d bytes\n",
					opts->subsysnqn, NVMF_NQN_SIZE);
				ret = -EINVAL;
				goto out;
			}
			opts->discovery_nqn =
				!(strcmp(opts->subsysnqn,
					 NVME_DISC_SUBSYS_NAME));
			if (opts->discovery_nqn)
				opts->nr_io_queues = 0;
			break;
		case NVMF_OPT_TRADDR:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			opts->traddr = p;
			break;
		case NVMF_OPT_TRSVCID:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			opts->trsvcid = p;
			break;
		case NVMF_OPT_QUEUE_SIZE:
			if (match_int(args, &token)) {
				ret = -EINVAL;
				goto out;
			}
			if (token < NVMF_MIN_QUEUE_SIZE ||
			    token > NVMF_MAX_QUEUE_SIZE) {
				pr_err("Invalid queue_size %d\n", token);
				ret = -EINVAL;
				goto out;
			}
			opts->queue_size = token;
			break;
		case NVMF_OPT_NR_IO_QUEUES:
			if (match_int(args, &token)) {
				ret = -EINVAL;
				goto out;
			}
			if (token <= 0) {
				pr_err("Invalid number of IOQs %d\n", token);
				ret = -EINVAL;
				goto out;
			}
			opts->nr_io_queues = min_t(unsigned int,
					num_online_cpus(), token);
			break;
		case NVMF_OPT_KATO:
			if (match_int(args, &token)) {
				ret = -EINVAL;
				goto out;
			}

			if (token < 0) {
				pr_err("Invalid keep_alive_tmo %d\n", token);
				ret = -EINVAL;
				goto out;
			} else if (token == 0 && !opts->discovery_nqn) {
				/* Allowed for debug */
				pr_warn("keep_alive_tmo 0 won't execute keep alives!!!\n");
			}
			opts->kato = token;

			if (opts->discovery_nqn && opts->kato) {
				pr_err("Discovery controllers cannot accept KATO != 0\n");
				ret = -EINVAL;
				goto out;
			}

			break;
		case NVMF_OPT_CTRL_LOSS_TMO:
			if (match_int(args, &token)) {
				ret = -EINVAL;
				goto out;
			}

			if (token < 0)
				pr_warn("ctrl_loss_tmo < 0 will reconnect forever\n");
			ctrl_loss_tmo = token;
			break;
		case NVMF_OPT_HOSTNQN:
			if (opts->host) {
				pr_err("hostnqn already user-assigned: %s\n",
				       opts->host->nqn);
				ret = -EADDRINUSE;
				goto out;
			}
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			nqnlen = strlen(p);
			if (nqnlen >= NVMF_NQN_SIZE) {
				pr_err("%s needs to be < %d bytes\n",
					p, NVMF_NQN_SIZE);
				kfree(p);
				ret = -EINVAL;
				goto out;
			}
			opts->host = nvmf_host_add(p);
			kfree(p);
			if (!opts->host) {
				ret = -ENOMEM;
				goto out;
			}
			break;
		case NVMF_OPT_RECONNECT_DELAY:
			if (match_int(args, &token)) {
				ret = -EINVAL;
				goto out;
			}
			if (token <= 0) {
				pr_err("Invalid reconnect_delay %d\n", token);
				ret = -EINVAL;
				goto out;
			}
			opts->reconnect_delay = token;
			break;
		case NVMF_OPT_HOST_TRADDR:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			opts->host_traddr = p;
			break;
		case NVMF_OPT_HOST_ID:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			ret = uuid_parse(p, &hostid);
			if (ret) {
				pr_err("Invalid hostid %s\n", p);
				ret = -EINVAL;
				kfree(p);
				goto out;
			}
			kfree(p);
			break;
		case NVMF_OPT_DUP_CONNECT:
			opts->duplicate_connect = true;
			break;
		default:
			pr_warn("unknown parameter or missing value '%s' in ctrl creation request\n",
				p);
			ret = -EINVAL;
			goto out;
		}
	}

	if (ctrl_loss_tmo < 0)
		opts->max_reconnects = -1;
	else
		opts->max_reconnects = DIV_ROUND_UP(ctrl_loss_tmo,
						opts->reconnect_delay);

	if (!opts->host) {
		kref_get(&nvmf_default_host->ref);
		opts->host = nvmf_default_host;
	}

	uuid_copy(&opts->host->id, &hostid);

out:
	kfree(options);
	return ret;
}

static int nvmf_check_required_opts(struct nvmf_ctrl_options *opts,
		unsigned int required_opts)
{
	if ((opts->mask & required_opts) != required_opts) {
		int i;

		for (i = 0; i < ARRAY_SIZE(opt_tokens); i++) {
			if ((opt_tokens[i].token & required_opts) &&
			    !(opt_tokens[i].token & opts->mask)) {
				pr_warn("missing parameter '%s'\n",
					opt_tokens[i].pattern);
			}
		}

		return -EINVAL;
	}

	return 0;
}

static int nvmf_check_allowed_opts(struct nvmf_ctrl_options *opts,
		unsigned int allowed_opts)
{
	if (opts->mask & ~allowed_opts) {
		int i;

		for (i = 0; i < ARRAY_SIZE(opt_tokens); i++) {
			if ((opt_tokens[i].token & opts->mask) &&
			    (opt_tokens[i].token & ~allowed_opts)) {
				pr_warn("invalid parameter '%s'\n",
					opt_tokens[i].pattern);
			}
		}

		return -EINVAL;
	}

	return 0;
}

void nvmf_free_options(struct nvmf_ctrl_options *opts)
{
	nvmf_host_put(opts->host);
	kfree(opts->transport);
	kfree(opts->traddr);
	kfree(opts->trsvcid);
	kfree(opts->subsysnqn);
	kfree(opts->host_traddr);
	kfree(opts);
}
EXPORT_SYMBOL_GPL(nvmf_free_options);

#define NVMF_REQUIRED_OPTS	(NVMF_OPT_TRANSPORT | NVMF_OPT_NQN)
#define NVMF_ALLOWED_OPTS	(NVMF_OPT_QUEUE_SIZE | NVMF_OPT_NR_IO_QUEUES | \
				 NVMF_OPT_KATO | NVMF_OPT_HOSTNQN | \
				 NVMF_OPT_HOST_ID | NVMF_OPT_DUP_CONNECT)

static struct nvme_ctrl *
nvmf_create_ctrl(struct device *dev, const char *buf, size_t count)
{
	struct nvmf_ctrl_options *opts;
	struct nvmf_transport_ops *ops;
	struct nvme_ctrl *ctrl;
	int ret;

	opts = kzalloc(sizeof(*opts), GFP_KERNEL);
	if (!opts)
		return ERR_PTR(-ENOMEM);

	ret = nvmf_parse_options(opts, buf);
	if (ret)
		goto out_free_opts;


	request_module("nvme-%s", opts->transport);

	/*
	 * Check the generic options first as we need a valid transport for
	 * the lookup below.  Then clear the generic flags so that transport
	 * drivers don't have to care about them.
	 */
	ret = nvmf_check_required_opts(opts, NVMF_REQUIRED_OPTS);
	if (ret)
		goto out_free_opts;
	opts->mask &= ~NVMF_REQUIRED_OPTS;

	down_read(&nvmf_transports_rwsem);
	ops = nvmf_lookup_transport(opts);
	if (!ops) {
		pr_info("no handler found for transport %s.\n",
			opts->transport);
		ret = -EINVAL;
		goto out_unlock;
	}

	if (!try_module_get(ops->module)) {
		ret = -EBUSY;
		goto out_unlock;
	}

	ret = nvmf_check_required_opts(opts, ops->required_opts);
	if (ret)
		goto out_module_put;
	ret = nvmf_check_allowed_opts(opts, NVMF_ALLOWED_OPTS |
				ops->allowed_opts | ops->required_opts);
	if (ret)
		goto out_module_put;

	ctrl = ops->create_ctrl(dev, opts);
	if (IS_ERR(ctrl)) {
		ret = PTR_ERR(ctrl);
		goto out_module_put;
	}

	if (strcmp(ctrl->subsys->subnqn, opts->subsysnqn)) {
		dev_warn(ctrl->device,
			"controller returned incorrect NQN: \"%s\".\n",
			ctrl->subsys->subnqn);
		module_put(ops->module);
		up_read(&nvmf_transports_rwsem);
		nvme_delete_ctrl_sync(ctrl);
		return ERR_PTR(-EINVAL);
	}

	module_put(ops->module);
	up_read(&nvmf_transports_rwsem);
	return ctrl;

out_module_put:
	module_put(ops->module);
out_unlock:
	up_read(&nvmf_transports_rwsem);
out_free_opts:
	nvmf_free_options(opts);
	return ERR_PTR(ret);
}

static struct class *nvmf_class;
static struct device *nvmf_device;
static DEFINE_MUTEX(nvmf_dev_mutex);

static ssize_t nvmf_dev_write(struct file *file, const char __user *ubuf,
		size_t count, loff_t *pos)
{
	struct seq_file *seq_file = file->private_data;
	struct nvme_ctrl *ctrl;
	const char *buf;
	int ret = 0;

	if (count > PAGE_SIZE)
		return -ENOMEM;

	buf = memdup_user_nul(ubuf, count);
	if (IS_ERR(buf))
		return PTR_ERR(buf);

	mutex_lock(&nvmf_dev_mutex);
	if (seq_file->private) {
		ret = -EINVAL;
		goto out_unlock;
	}

	ctrl = nvmf_create_ctrl(nvmf_device, buf, count);
	if (IS_ERR(ctrl)) {
		ret = PTR_ERR(ctrl);
		goto out_unlock;
	}

	seq_file->private = ctrl;

out_unlock:
	mutex_unlock(&nvmf_dev_mutex);
	kfree(buf);
	return ret ? ret : count;
}

static int nvmf_dev_show(struct seq_file *seq_file, void *private)
{
	struct nvme_ctrl *ctrl;
	int ret = 0;

	mutex_lock(&nvmf_dev_mutex);
	ctrl = seq_file->private;
	if (!ctrl) {
		ret = -EINVAL;
		goto out_unlock;
	}

	seq_printf(seq_file, "instance=%d,cntlid=%d\n",
			ctrl->instance, ctrl->cntlid);

out_unlock:
	mutex_unlock(&nvmf_dev_mutex);
	return ret;
}

static int nvmf_dev_open(struct inode *inode, struct file *file)
{
	/*
	 * The miscdevice code initializes file->private_data, but doesn't
	 * make use of it later.
	 */
	file->private_data = NULL;
	return single_open(file, nvmf_dev_show, NULL);
}

static int nvmf_dev_release(struct inode *inode, struct file *file)
{
	struct seq_file *seq_file = file->private_data;
	struct nvme_ctrl *ctrl = seq_file->private;

	if (ctrl)
		nvme_put_ctrl(ctrl);
	return single_release(inode, file);
}

static const struct file_operations nvmf_dev_fops = {
	.owner		= THIS_MODULE,
	.write		= nvmf_dev_write,
	.read		= seq_read,
	.open		= nvmf_dev_open,
	.release	= nvmf_dev_release,
};

static struct miscdevice nvmf_misc = {
	.minor		= MISC_DYNAMIC_MINOR,
	.name           = "nvme-fabrics",
	.fops		= &nvmf_dev_fops,
};

static int __init nvmf_init(void)
{
	int ret;

	nvmf_default_host = nvmf_host_default();
	if (!nvmf_default_host)
		return -ENOMEM;

	nvmf_class = class_create(THIS_MODULE, "nvme-fabrics");
	if (IS_ERR(nvmf_class)) {
		pr_err("couldn't register class nvme-fabrics\n");
		ret = PTR_ERR(nvmf_class);
		goto out_free_host;
	}

	nvmf_device =
		device_create(nvmf_class, NULL, MKDEV(0, 0), NULL, "ctl");
	if (IS_ERR(nvmf_device)) {
		pr_err("couldn't create nvme-fabris device!\n");
		ret = PTR_ERR(nvmf_device);
		goto out_destroy_class;
	}

	ret = misc_register(&nvmf_misc);
	if (ret) {
		pr_err("couldn't register misc device: %d\n", ret);
		goto out_destroy_device;
	}

	return 0;

out_destroy_device:
	device_destroy(nvmf_class, MKDEV(0, 0));
out_destroy_class:
	class_destroy(nvmf_class);
out_free_host:
	nvmf_host_put(nvmf_default_host);
	return ret;
}

static void __exit nvmf_exit(void)
{
	misc_deregister(&nvmf_misc);
	device_destroy(nvmf_class, MKDEV(0, 0));
	class_destroy(nvmf_class);
	nvmf_host_put(nvmf_default_host);

	BUILD_BUG_ON(sizeof(struct nvmf_connect_command) != 64);
	BUILD_BUG_ON(sizeof(struct nvmf_property_get_command) != 64);
	BUILD_BUG_ON(sizeof(struct nvmf_property_set_command) != 64);
	BUILD_BUG_ON(sizeof(struct nvmf_connect_data) != 1024);
}

MODULE_LICENSE("GPL v2");

module_init(nvmf_init);
module_exit(nvmf_exit);
