// SPDX-License-Identifier: GPL-2.0
/*
 * NVMe over Fabrics common host code.
 * Copyright (c) 2015-2016 HGST, a Western Digital Company.
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
#include <linux/nvme-keyring.h>

static LIST_HEAD(nvmf_transports);
static DECLARE_RWSEM(nvmf_transports_rwsem);

static LIST_HEAD(nvmf_hosts);
static DEFINE_MUTEX(nvmf_hosts_mutex);

static struct nvmf_host *nvmf_default_host;

static struct nvmf_host *nvmf_host_alloc(const char *hostnqn, uuid_t *id)
{
	struct nvmf_host *host;

	host = kmalloc(sizeof(*host), GFP_KERNEL);
	if (!host)
		return NULL;

	kref_init(&host->ref);
	uuid_copy(&host->id, id);
	strscpy(host->nqn, hostnqn, NVMF_NQN_SIZE);

	return host;
}

static struct nvmf_host *nvmf_host_add(const char *hostnqn, uuid_t *id)
{
	struct nvmf_host *host;

	mutex_lock(&nvmf_hosts_mutex);

	/*
	 * We have defined a host as how it is perceived by the target.
	 * Therefore, we don't allow different Host NQNs with the same Host ID.
	 * Similarly, we do not allow the usage of the same Host NQN with
	 * different Host IDs. This'll maintain unambiguous host identification.
	 */
	list_for_each_entry(host, &nvmf_hosts, list) {
		bool same_hostnqn = !strcmp(host->nqn, hostnqn);
		bool same_hostid = uuid_equal(&host->id, id);

		if (same_hostnqn && same_hostid) {
			kref_get(&host->ref);
			goto out_unlock;
		}
		if (same_hostnqn) {
			pr_err("found same hostnqn %s but different hostid %pUb\n",
			       hostnqn, id);
			host = ERR_PTR(-EINVAL);
			goto out_unlock;
		}
		if (same_hostid) {
			pr_err("found same hostid %pUb but different hostnqn %s\n",
			       id, hostnqn);
			host = ERR_PTR(-EINVAL);
			goto out_unlock;
		}
	}

	host = nvmf_host_alloc(hostnqn, id);
	if (!host) {
		host = ERR_PTR(-ENOMEM);
		goto out_unlock;
	}

	list_add_tail(&host->list, &nvmf_hosts);
out_unlock:
	mutex_unlock(&nvmf_hosts_mutex);
	return host;
}

static struct nvmf_host *nvmf_host_default(void)
{
	struct nvmf_host *host;
	char nqn[NVMF_NQN_SIZE];
	uuid_t id;

	uuid_gen(&id);
	snprintf(nqn, NVMF_NQN_SIZE,
		"nqn.2014-08.org.nvmexpress:uuid:%pUb", &id);

	host = nvmf_host_alloc(nqn, &id);
	if (!host)
		return NULL;

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
		len += scnprintf(buf, size, "traddr=%s", ctrl->opts->traddr);
	if (ctrl->opts->mask & NVMF_OPT_TRSVCID)
		len += scnprintf(buf + len, size - len, "%strsvcid=%s",
				(len) ? "," : "", ctrl->opts->trsvcid);
	if (ctrl->opts->mask & NVMF_OPT_HOST_TRADDR)
		len += scnprintf(buf + len, size - len, "%shost_traddr=%s",
				(len) ? "," : "", ctrl->opts->host_traddr);
	if (ctrl->opts->mask & NVMF_OPT_HOST_IFACE)
		len += scnprintf(buf + len, size - len, "%shost_iface=%s",
				(len) ? "," : "", ctrl->opts->host_iface);
	len += scnprintf(buf + len, size - len, "\n");

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
	struct nvme_command cmd = { };
	union nvme_result res;
	int ret;

	cmd.prop_get.opcode = nvme_fabrics_command;
	cmd.prop_get.fctype = nvme_fabrics_type_property_get;
	cmd.prop_get.offset = cpu_to_le32(off);

	ret = __nvme_submit_sync_cmd(ctrl->fabrics_q, &cmd, &res, NULL, 0,
			NVME_QID_ANY, 0);

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
	struct nvme_command cmd = { };
	union nvme_result res;
	int ret;

	cmd.prop_get.opcode = nvme_fabrics_command;
	cmd.prop_get.fctype = nvme_fabrics_type_property_get;
	cmd.prop_get.attrib = 1;
	cmd.prop_get.offset = cpu_to_le32(off);

	ret = __nvme_submit_sync_cmd(ctrl->fabrics_q, &cmd, &res, NULL, 0,
			NVME_QID_ANY, 0);

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
	struct nvme_command cmd = { };
	int ret;

	cmd.prop_set.opcode = nvme_fabrics_command;
	cmd.prop_set.fctype = nvme_fabrics_type_property_set;
	cmd.prop_set.attrib = 0;
	cmd.prop_set.offset = cpu_to_le32(off);
	cmd.prop_set.value = cpu_to_le64(val);

	ret = __nvme_submit_sync_cmd(ctrl->fabrics_q, &cmd, NULL, NULL, 0,
			NVME_QID_ANY, 0);
	if (unlikely(ret))
		dev_err(ctrl->device,
			"Property Set error: %d, offset %#x\n",
			ret > 0 ? ret & ~NVME_SC_DNR : ret, off);
	return ret;
}
EXPORT_SYMBOL_GPL(nvmf_reg_write32);

/**
 * nvmf_log_connect_error() - Error-parsing-diagnostic print out function for
 * 				connect() errors.
 * @ctrl:	The specific /dev/nvmeX device that had the error.
 * @errval:	Error code to be decoded in a more human-friendly
 * 		printout.
 * @offset:	For use with the NVMe error code
 * 		NVME_SC_CONNECT_INVALID_PARAM.
 * @cmd:	This is the SQE portion of a submission capsule.
 * @data:	This is the "Data" portion of a submission capsule.
 */
static void nvmf_log_connect_error(struct nvme_ctrl *ctrl,
		int errval, int offset, struct nvme_command *cmd,
		struct nvmf_connect_data *data)
{
	int err_sctype = errval & ~NVME_SC_DNR;

	if (errval < 0) {
		dev_err(ctrl->device,
			"Connect command failed, errno: %d\n", errval);
		return;
	}

	switch (err_sctype) {
	case NVME_SC_CONNECT_INVALID_PARAM:
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
	case NVME_SC_HOST_PATH_ERROR:
		dev_err(ctrl->device,
			"Connect command failed: host path error\n");
		break;
	case NVME_SC_AUTH_REQUIRED:
		dev_err(ctrl->device,
			"Connect command failed: authentication required\n");
		break;
	default:
		dev_err(ctrl->device,
			"Connect command failed, error wo/DNR bit: %d\n",
			err_sctype);
		break;
	}
}

static struct nvmf_connect_data *nvmf_connect_data_prep(struct nvme_ctrl *ctrl,
		u16 cntlid)
{
	struct nvmf_connect_data *data;

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	if (!data)
		return NULL;

	uuid_copy(&data->hostid, &ctrl->opts->host->id);
	data->cntlid = cpu_to_le16(cntlid);
	strscpy(data->subsysnqn, ctrl->opts->subsysnqn, NVMF_NQN_SIZE);
	strscpy(data->hostnqn, ctrl->opts->host->nqn, NVMF_NQN_SIZE);

	return data;
}

static void nvmf_connect_cmd_prep(struct nvme_ctrl *ctrl, u16 qid,
		struct nvme_command *cmd)
{
	cmd->connect.opcode = nvme_fabrics_command;
	cmd->connect.fctype = nvme_fabrics_type_connect;
	cmd->connect.qid = cpu_to_le16(qid);

	if (qid) {
		cmd->connect.sqsize = cpu_to_le16(ctrl->sqsize);
	} else {
		cmd->connect.sqsize = cpu_to_le16(NVME_AQ_DEPTH - 1);

		/*
		 * set keep-alive timeout in seconds granularity (ms * 1000)
		 */
		cmd->connect.kato = cpu_to_le32(ctrl->kato * 1000);
	}

	if (ctrl->opts->disable_sqflow)
		cmd->connect.cattr |= NVME_CONNECT_DISABLE_SQFLOW;
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
 */
int nvmf_connect_admin_queue(struct nvme_ctrl *ctrl)
{
	struct nvme_command cmd = { };
	union nvme_result res;
	struct nvmf_connect_data *data;
	int ret;
	u32 result;

	nvmf_connect_cmd_prep(ctrl, 0, &cmd);

	data = nvmf_connect_data_prep(ctrl, 0xffff);
	if (!data)
		return -ENOMEM;

	ret = __nvme_submit_sync_cmd(ctrl->fabrics_q, &cmd, &res,
			data, sizeof(*data), NVME_QID_ANY,
			NVME_SUBMIT_AT_HEAD |
			NVME_SUBMIT_NOWAIT |
			NVME_SUBMIT_RESERVED);
	if (ret) {
		nvmf_log_connect_error(ctrl, ret, le32_to_cpu(res.u32),
				       &cmd, data);
		goto out_free_data;
	}

	result = le32_to_cpu(res.u32);
	ctrl->cntlid = result & 0xFFFF;
	if (result & (NVME_CONNECT_AUTHREQ_ATR | NVME_CONNECT_AUTHREQ_ASCR)) {
		/* Secure concatenation is not implemented */
		if (result & NVME_CONNECT_AUTHREQ_ASCR) {
			dev_warn(ctrl->device,
				 "qid 0: secure concatenation is not supported\n");
			ret = -EOPNOTSUPP;
			goto out_free_data;
		}
		/* Authentication required */
		ret = nvme_auth_negotiate(ctrl, 0);
		if (ret) {
			dev_warn(ctrl->device,
				 "qid 0: authentication setup failed\n");
			goto out_free_data;
		}
		ret = nvme_auth_wait(ctrl, 0);
		if (ret) {
			dev_warn(ctrl->device,
				 "qid 0: authentication failed, error %d\n",
				 ret);
		} else
			dev_info(ctrl->device,
				 "qid 0: authenticated\n");
	}
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
	struct nvme_command cmd = { };
	struct nvmf_connect_data *data;
	union nvme_result res;
	int ret;
	u32 result;

	nvmf_connect_cmd_prep(ctrl, qid, &cmd);

	data = nvmf_connect_data_prep(ctrl, ctrl->cntlid);
	if (!data)
		return -ENOMEM;

	ret = __nvme_submit_sync_cmd(ctrl->connect_q, &cmd, &res,
			data, sizeof(*data), qid,
			NVME_SUBMIT_AT_HEAD |
			NVME_SUBMIT_RESERVED |
			NVME_SUBMIT_NOWAIT);
	if (ret) {
		nvmf_log_connect_error(ctrl, ret, le32_to_cpu(res.u32),
				       &cmd, data);
		goto out_free_data;
	}
	result = le32_to_cpu(res.u32);
	if (result & (NVME_CONNECT_AUTHREQ_ATR | NVME_CONNECT_AUTHREQ_ASCR)) {
		/* Secure concatenation is not implemented */
		if (result & NVME_CONNECT_AUTHREQ_ASCR) {
			dev_warn(ctrl->device,
				 "qid 0: secure concatenation is not supported\n");
			ret = -EOPNOTSUPP;
			goto out_free_data;
		}
		/* Authentication required */
		ret = nvme_auth_negotiate(ctrl, qid);
		if (ret) {
			dev_warn(ctrl->device,
				 "qid %d: authentication setup failed\n", qid);
			goto out_free_data;
		}
		ret = nvme_auth_wait(ctrl, qid);
		if (ret) {
			dev_warn(ctrl->device,
				 "qid %u: authentication failed, error %d\n",
				 qid, ret);
		}
	}
out_free_data:
	kfree(data);
	return ret;
}
EXPORT_SYMBOL_GPL(nvmf_connect_io_queue);

/*
 * Evaluate the status information returned by the transport in order to decided
 * if a reconnect attempt should be scheduled.
 *
 * Do not retry when:
 *
 * - the DNR bit is set and the specification states no further connect
 *   attempts with the same set of paramenters should be attempted.
 *
 * - when the authentication attempt fails, because the key was invalid.
 *   This error code is set on the host side.
 */
bool nvmf_should_reconnect(struct nvme_ctrl *ctrl, int status)
{
	if (status > 0 && (status & NVME_SC_DNR))
		return false;

	if (status == -EKEYREJECTED)
		return false;

	if (ctrl->opts->max_reconnects == -1 ||
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
	if (!ops->create_ctrl)
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

static struct key *nvmf_parse_key(int key_id)
{
	struct key *key;

	if (!IS_ENABLED(CONFIG_NVME_TCP_TLS)) {
		pr_err("TLS is not supported\n");
		return ERR_PTR(-EINVAL);
	}

	key = key_lookup(key_id);
	if (IS_ERR(key))
		pr_err("key id %08x not found\n", key_id);
	else
		pr_debug("Using key id %08x\n", key_id);
	return key;
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
	{ NVMF_OPT_HOST_IFACE,		"host_iface=%s"		},
	{ NVMF_OPT_HOST_ID,		"hostid=%s"		},
	{ NVMF_OPT_DUP_CONNECT,		"duplicate_connect"	},
	{ NVMF_OPT_DISABLE_SQFLOW,	"disable_sqflow"	},
	{ NVMF_OPT_HDR_DIGEST,		"hdr_digest"		},
	{ NVMF_OPT_DATA_DIGEST,		"data_digest"		},
	{ NVMF_OPT_NR_WRITE_QUEUES,	"nr_write_queues=%d"	},
	{ NVMF_OPT_NR_POLL_QUEUES,	"nr_poll_queues=%d"	},
	{ NVMF_OPT_TOS,			"tos=%d"		},
#ifdef CONFIG_NVME_TCP_TLS
	{ NVMF_OPT_KEYRING,		"keyring=%d"		},
	{ NVMF_OPT_TLS_KEY,		"tls_key=%d"		},
#endif
	{ NVMF_OPT_FAIL_FAST_TMO,	"fast_io_fail_tmo=%d"	},
	{ NVMF_OPT_DISCOVERY,		"discovery"		},
#ifdef CONFIG_NVME_HOST_AUTH
	{ NVMF_OPT_DHCHAP_SECRET,	"dhchap_secret=%s"	},
	{ NVMF_OPT_DHCHAP_CTRL_SECRET,	"dhchap_ctrl_secret=%s"	},
#endif
#ifdef CONFIG_NVME_TCP_TLS
	{ NVMF_OPT_TLS,			"tls"			},
#endif
	{ NVMF_OPT_ERR,			NULL			}
};

static int nvmf_parse_options(struct nvmf_ctrl_options *opts,
		const char *buf)
{
	substring_t args[MAX_OPT_ARGS];
	char *options, *o, *p;
	int token, ret = 0;
	size_t nqnlen  = 0;
	int ctrl_loss_tmo = NVMF_DEF_CTRL_LOSS_TMO, key_id;
	uuid_t hostid;
	char hostnqn[NVMF_NQN_SIZE];
	struct key *key;

	/* Set defaults */
	opts->queue_size = NVMF_DEF_QUEUE_SIZE;
	opts->nr_io_queues = num_online_cpus();
	opts->reconnect_delay = NVMF_DEF_RECONNECT_DELAY;
	opts->kato = 0;
	opts->duplicate_connect = false;
	opts->fast_io_fail_tmo = NVMF_DEF_FAIL_FAST_TMO;
	opts->hdr_digest = false;
	opts->data_digest = false;
	opts->tos = -1; /* < 0 == use transport default */
	opts->tls = false;
	opts->tls_key = NULL;
	opts->keyring = NULL;

	options = o = kstrdup(buf, GFP_KERNEL);
	if (!options)
		return -ENOMEM;

	/* use default host if not given by user space */
	uuid_copy(&hostid, &nvmf_default_host->id);
	strscpy(hostnqn, nvmf_default_host->nqn, NVMF_NQN_SIZE);

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
			kfree(opts->transport);
			opts->transport = p;
			break;
		case NVMF_OPT_NQN:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			kfree(opts->subsysnqn);
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
			break;
		case NVMF_OPT_TRADDR:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			kfree(opts->traddr);
			opts->traddr = p;
			break;
		case NVMF_OPT_TRSVCID:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			kfree(opts->trsvcid);
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
			if (opts->discovery_nqn) {
				pr_debug("Ignoring nr_io_queues value for discovery controller\n");
				break;
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
		case NVMF_OPT_FAIL_FAST_TMO:
			if (match_int(args, &token)) {
				ret = -EINVAL;
				goto out;
			}

			if (token >= 0)
				pr_warn("I/O fail on reconnect controller after %d sec\n",
					token);
			else
				token = -1;

			opts->fast_io_fail_tmo = token;
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
			strscpy(hostnqn, p, NVMF_NQN_SIZE);
			kfree(p);
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
			kfree(opts->host_traddr);
			opts->host_traddr = p;
			break;
		case NVMF_OPT_HOST_IFACE:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			kfree(opts->host_iface);
			opts->host_iface = p;
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
		case NVMF_OPT_DISABLE_SQFLOW:
			opts->disable_sqflow = true;
			break;
		case NVMF_OPT_HDR_DIGEST:
			opts->hdr_digest = true;
			break;
		case NVMF_OPT_DATA_DIGEST:
			opts->data_digest = true;
			break;
		case NVMF_OPT_NR_WRITE_QUEUES:
			if (match_int(args, &token)) {
				ret = -EINVAL;
				goto out;
			}
			if (token <= 0) {
				pr_err("Invalid nr_write_queues %d\n", token);
				ret = -EINVAL;
				goto out;
			}
			opts->nr_write_queues = token;
			break;
		case NVMF_OPT_NR_POLL_QUEUES:
			if (match_int(args, &token)) {
				ret = -EINVAL;
				goto out;
			}
			if (token <= 0) {
				pr_err("Invalid nr_poll_queues %d\n", token);
				ret = -EINVAL;
				goto out;
			}
			opts->nr_poll_queues = token;
			break;
		case NVMF_OPT_TOS:
			if (match_int(args, &token)) {
				ret = -EINVAL;
				goto out;
			}
			if (token < 0) {
				pr_err("Invalid type of service %d\n", token);
				ret = -EINVAL;
				goto out;
			}
			if (token > 255) {
				pr_warn("Clamping type of service to 255\n");
				token = 255;
			}
			opts->tos = token;
			break;
		case NVMF_OPT_KEYRING:
			if (match_int(args, &key_id) || key_id <= 0) {
				ret = -EINVAL;
				goto out;
			}
			key = nvmf_parse_key(key_id);
			if (IS_ERR(key)) {
				ret = PTR_ERR(key);
				goto out;
			}
			key_put(opts->keyring);
			opts->keyring = key;
			break;
		case NVMF_OPT_TLS_KEY:
			if (match_int(args, &key_id) || key_id <= 0) {
				ret = -EINVAL;
				goto out;
			}
			key = nvmf_parse_key(key_id);
			if (IS_ERR(key)) {
				ret = PTR_ERR(key);
				goto out;
			}
			key_put(opts->tls_key);
			opts->tls_key = key;
			break;
		case NVMF_OPT_DISCOVERY:
			opts->discovery_nqn = true;
			break;
		case NVMF_OPT_DHCHAP_SECRET:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			if (strlen(p) < 11 || strncmp(p, "DHHC-1:", 7)) {
				pr_err("Invalid DH-CHAP secret %s\n", p);
				ret = -EINVAL;
				goto out;
			}
			kfree(opts->dhchap_secret);
			opts->dhchap_secret = p;
			break;
		case NVMF_OPT_DHCHAP_CTRL_SECRET:
			p = match_strdup(args);
			if (!p) {
				ret = -ENOMEM;
				goto out;
			}
			if (strlen(p) < 11 || strncmp(p, "DHHC-1:", 7)) {
				pr_err("Invalid DH-CHAP secret %s\n", p);
				ret = -EINVAL;
				goto out;
			}
			kfree(opts->dhchap_ctrl_secret);
			opts->dhchap_ctrl_secret = p;
			break;
		case NVMF_OPT_TLS:
			if (!IS_ENABLED(CONFIG_NVME_TCP_TLS)) {
				pr_err("TLS is not supported\n");
				ret = -EINVAL;
				goto out;
			}
			opts->tls = true;
			break;
		default:
			pr_warn("unknown parameter or missing value '%s' in ctrl creation request\n",
				p);
			ret = -EINVAL;
			goto out;
		}
	}

	if (opts->discovery_nqn) {
		opts->nr_io_queues = 0;
		opts->nr_write_queues = 0;
		opts->nr_poll_queues = 0;
		opts->duplicate_connect = true;
	} else {
		if (!opts->kato)
			opts->kato = NVME_DEFAULT_KATO;
	}
	if (ctrl_loss_tmo < 0) {
		opts->max_reconnects = -1;
	} else {
		opts->max_reconnects = DIV_ROUND_UP(ctrl_loss_tmo,
						opts->reconnect_delay);
		if (ctrl_loss_tmo < opts->fast_io_fail_tmo)
			pr_warn("failfast tmo (%d) larger than controller loss tmo (%d)\n",
				opts->fast_io_fail_tmo, ctrl_loss_tmo);
	}

	opts->host = nvmf_host_add(hostnqn, &hostid);
	if (IS_ERR(opts->host)) {
		ret = PTR_ERR(opts->host);
		opts->host = NULL;
		goto out;
	}

out:
	kfree(options);
	return ret;
}

void nvmf_set_io_queues(struct nvmf_ctrl_options *opts, u32 nr_io_queues,
			u32 io_queues[HCTX_MAX_TYPES])
{
	if (opts->nr_write_queues && opts->nr_io_queues < nr_io_queues) {
		/*
		 * separate read/write queues
		 * hand out dedicated default queues only after we have
		 * sufficient read queues.
		 */
		io_queues[HCTX_TYPE_READ] = opts->nr_io_queues;
		nr_io_queues -= io_queues[HCTX_TYPE_READ];
		io_queues[HCTX_TYPE_DEFAULT] =
			min(opts->nr_write_queues, nr_io_queues);
		nr_io_queues -= io_queues[HCTX_TYPE_DEFAULT];
	} else {
		/*
		 * shared read/write queues
		 * either no write queues were requested, or we don't have
		 * sufficient queue count to have dedicated default queues.
		 */
		io_queues[HCTX_TYPE_DEFAULT] =
			min(opts->nr_io_queues, nr_io_queues);
		nr_io_queues -= io_queues[HCTX_TYPE_DEFAULT];
	}

	if (opts->nr_poll_queues && nr_io_queues) {
		/* map dedicated poll queues only if we have queues left */
		io_queues[HCTX_TYPE_POLL] =
			min(opts->nr_poll_queues, nr_io_queues);
	}
}
EXPORT_SYMBOL_GPL(nvmf_set_io_queues);

void nvmf_map_queues(struct blk_mq_tag_set *set, struct nvme_ctrl *ctrl,
		     u32 io_queues[HCTX_MAX_TYPES])
{
	struct nvmf_ctrl_options *opts = ctrl->opts;

	if (opts->nr_write_queues && io_queues[HCTX_TYPE_READ]) {
		/* separate read/write queues */
		set->map[HCTX_TYPE_DEFAULT].nr_queues =
			io_queues[HCTX_TYPE_DEFAULT];
		set->map[HCTX_TYPE_DEFAULT].queue_offset = 0;
		set->map[HCTX_TYPE_READ].nr_queues =
			io_queues[HCTX_TYPE_READ];
		set->map[HCTX_TYPE_READ].queue_offset =
			io_queues[HCTX_TYPE_DEFAULT];
	} else {
		/* shared read/write queues */
		set->map[HCTX_TYPE_DEFAULT].nr_queues =
			io_queues[HCTX_TYPE_DEFAULT];
		set->map[HCTX_TYPE_DEFAULT].queue_offset = 0;
		set->map[HCTX_TYPE_READ].nr_queues =
			io_queues[HCTX_TYPE_DEFAULT];
		set->map[HCTX_TYPE_READ].queue_offset = 0;
	}

	blk_mq_map_queues(&set->map[HCTX_TYPE_DEFAULT]);
	blk_mq_map_queues(&set->map[HCTX_TYPE_READ]);
	if (opts->nr_poll_queues && io_queues[HCTX_TYPE_POLL]) {
		/* map dedicated poll queues only if we have queues left */
		set->map[HCTX_TYPE_POLL].nr_queues = io_queues[HCTX_TYPE_POLL];
		set->map[HCTX_TYPE_POLL].queue_offset =
			io_queues[HCTX_TYPE_DEFAULT] +
			io_queues[HCTX_TYPE_READ];
		blk_mq_map_queues(&set->map[HCTX_TYPE_POLL]);
	}

	dev_info(ctrl->device,
		"mapped %d/%d/%d default/read/poll queues.\n",
		io_queues[HCTX_TYPE_DEFAULT],
		io_queues[HCTX_TYPE_READ],
		io_queues[HCTX_TYPE_POLL]);
}
EXPORT_SYMBOL_GPL(nvmf_map_queues);

static int nvmf_check_required_opts(struct nvmf_ctrl_options *opts,
		unsigned int required_opts)
{
	if ((opts->mask & required_opts) != required_opts) {
		unsigned int i;

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

bool nvmf_ip_options_match(struct nvme_ctrl *ctrl,
		struct nvmf_ctrl_options *opts)
{
	if (!nvmf_ctlr_matches_baseopts(ctrl, opts) ||
	    strcmp(opts->traddr, ctrl->opts->traddr) ||
	    strcmp(opts->trsvcid, ctrl->opts->trsvcid))
		return false;

	/*
	 * Checking the local address or host interfaces is rough.
	 *
	 * In most cases, none is specified and the host port or
	 * host interface is selected by the stack.
	 *
	 * Assume no match if:
	 * -  local address or host interface is specified and address
	 *    or host interface is not the same
	 * -  local address or host interface is not specified but
	 *    remote is, or vice versa (admin using specific
	 *    host_traddr/host_iface when it matters).
	 */
	if ((opts->mask & NVMF_OPT_HOST_TRADDR) &&
	    (ctrl->opts->mask & NVMF_OPT_HOST_TRADDR)) {
		if (strcmp(opts->host_traddr, ctrl->opts->host_traddr))
			return false;
	} else if ((opts->mask & NVMF_OPT_HOST_TRADDR) ||
		   (ctrl->opts->mask & NVMF_OPT_HOST_TRADDR)) {
		return false;
	}

	if ((opts->mask & NVMF_OPT_HOST_IFACE) &&
	    (ctrl->opts->mask & NVMF_OPT_HOST_IFACE)) {
		if (strcmp(opts->host_iface, ctrl->opts->host_iface))
			return false;
	} else if ((opts->mask & NVMF_OPT_HOST_IFACE) ||
		   (ctrl->opts->mask & NVMF_OPT_HOST_IFACE)) {
		return false;
	}

	return true;
}
EXPORT_SYMBOL_GPL(nvmf_ip_options_match);

static int nvmf_check_allowed_opts(struct nvmf_ctrl_options *opts,
		unsigned int allowed_opts)
{
	if (opts->mask & ~allowed_opts) {
		unsigned int i;

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
	key_put(opts->keyring);
	key_put(opts->tls_key);
	kfree(opts->transport);
	kfree(opts->traddr);
	kfree(opts->trsvcid);
	kfree(opts->subsysnqn);
	kfree(opts->host_traddr);
	kfree(opts->host_iface);
	kfree(opts->dhchap_secret);
	kfree(opts->dhchap_ctrl_secret);
	kfree(opts);
}
EXPORT_SYMBOL_GPL(nvmf_free_options);

#define NVMF_REQUIRED_OPTS	(NVMF_OPT_TRANSPORT | NVMF_OPT_NQN)
#define NVMF_ALLOWED_OPTS	(NVMF_OPT_QUEUE_SIZE | NVMF_OPT_NR_IO_QUEUES | \
				 NVMF_OPT_KATO | NVMF_OPT_HOSTNQN | \
				 NVMF_OPT_HOST_ID | NVMF_OPT_DUP_CONNECT |\
				 NVMF_OPT_DISABLE_SQFLOW | NVMF_OPT_DISCOVERY |\
				 NVMF_OPT_FAIL_FAST_TMO | NVMF_OPT_DHCHAP_SECRET |\
				 NVMF_OPT_DHCHAP_CTRL_SECRET)

static struct nvme_ctrl *
nvmf_create_ctrl(struct device *dev, const char *buf)
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
	up_read(&nvmf_transports_rwsem);

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

	module_put(ops->module);
	return ctrl;

out_module_put:
	module_put(ops->module);
	goto out_free_opts;
out_unlock:
	up_read(&nvmf_transports_rwsem);
out_free_opts:
	nvmf_free_options(opts);
	return ERR_PTR(ret);
}

static const struct class nvmf_class = {
	.name = "nvme-fabrics",
};

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

	ctrl = nvmf_create_ctrl(nvmf_device, buf);
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

static void __nvmf_concat_opt_tokens(struct seq_file *seq_file)
{
	const struct match_token *tok;
	int idx;

	/*
	 * Add dummy entries for instance and cntlid to
	 * signal an invalid/non-existing controller
	 */
	seq_puts(seq_file, "instance=-1,cntlid=-1");
	for (idx = 0; idx < ARRAY_SIZE(opt_tokens); idx++) {
		tok = &opt_tokens[idx];
		if (tok->token == NVMF_OPT_ERR)
			continue;
		seq_puts(seq_file, ",");
		seq_puts(seq_file, tok->pattern);
	}
	seq_puts(seq_file, "\n");
}

static int nvmf_dev_show(struct seq_file *seq_file, void *private)
{
	struct nvme_ctrl *ctrl;

	mutex_lock(&nvmf_dev_mutex);
	ctrl = seq_file->private;
	if (!ctrl) {
		__nvmf_concat_opt_tokens(seq_file);
		goto out_unlock;
	}

	seq_printf(seq_file, "instance=%d,cntlid=%d\n",
			ctrl->instance, ctrl->cntlid);

out_unlock:
	mutex_unlock(&nvmf_dev_mutex);
	return 0;
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

	ret = class_register(&nvmf_class);
	if (ret) {
		pr_err("couldn't register class nvme-fabrics\n");
		goto out_free_host;
	}

	nvmf_device =
		device_create(&nvmf_class, NULL, MKDEV(0, 0), NULL, "ctl");
	if (IS_ERR(nvmf_device)) {
		pr_err("couldn't create nvme-fabrics device!\n");
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
	device_destroy(&nvmf_class, MKDEV(0, 0));
out_destroy_class:
	class_unregister(&nvmf_class);
out_free_host:
	nvmf_host_put(nvmf_default_host);
	return ret;
}

static void __exit nvmf_exit(void)
{
	misc_deregister(&nvmf_misc);
	device_destroy(&nvmf_class, MKDEV(0, 0));
	class_unregister(&nvmf_class);
	nvmf_host_put(nvmf_default_host);

	BUILD_BUG_ON(sizeof(struct nvmf_common_command) != 64);
	BUILD_BUG_ON(sizeof(struct nvmf_connect_command) != 64);
	BUILD_BUG_ON(sizeof(struct nvmf_property_get_command) != 64);
	BUILD_BUG_ON(sizeof(struct nvmf_property_set_command) != 64);
	BUILD_BUG_ON(sizeof(struct nvmf_auth_send_command) != 64);
	BUILD_BUG_ON(sizeof(struct nvmf_auth_receive_command) != 64);
	BUILD_BUG_ON(sizeof(struct nvmf_connect_data) != 1024);
	BUILD_BUG_ON(sizeof(struct nvmf_auth_dhchap_negotiate_data) != 8);
	BUILD_BUG_ON(sizeof(struct nvmf_auth_dhchap_challenge_data) != 16);
	BUILD_BUG_ON(sizeof(struct nvmf_auth_dhchap_reply_data) != 16);
	BUILD_BUG_ON(sizeof(struct nvmf_auth_dhchap_success1_data) != 16);
	BUILD_BUG_ON(sizeof(struct nvmf_auth_dhchap_success2_data) != 16);
}

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("NVMe host fabrics library");

module_init(nvmf_init);
module_exit(nvmf_exit);
