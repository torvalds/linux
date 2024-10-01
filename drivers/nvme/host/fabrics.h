/* SPDX-License-Identifier: GPL-2.0 */
/*
 * NVMe over Fabrics common host code.
 * Copyright (c) 2015-2016 HGST, a Western Digital Company.
 */
#ifndef _NVME_FABRICS_H
#define _NVME_FABRICS_H 1

#include <linux/in.h>
#include <linux/inet.h>

#define NVMF_MIN_QUEUE_SIZE	16
#define NVMF_MAX_QUEUE_SIZE	1024
#define NVMF_DEF_QUEUE_SIZE	128
#define NVMF_DEF_RECONNECT_DELAY	10
/* default to 600 seconds of reconnect attempts before giving up */
#define NVMF_DEF_CTRL_LOSS_TMO		600
/* default is -1: the fail fast mechanism is disabled  */
#define NVMF_DEF_FAIL_FAST_TMO		-1

/*
 * Define a host as seen by the target.  We allocate one at boot, but also
 * allow the override it when creating controllers.  This is both to provide
 * persistence of the Host NQN over multiple boots, and to allow using
 * multiple ones, for example in a container scenario.  Because we must not
 * use different Host NQNs with the same Host ID we generate a Host ID and
 * use this structure to keep track of the relation between the two.
 */
struct nvmf_host {
	struct kref		ref;
	struct list_head	list;
	char			nqn[NVMF_NQN_SIZE];
	uuid_t			id;
};

/**
 * enum nvmf_parsing_opts - used to define the sysfs parsing options used.
 */
enum {
	NVMF_OPT_ERR		= 0,
	NVMF_OPT_TRANSPORT	= 1 << 0,
	NVMF_OPT_NQN		= 1 << 1,
	NVMF_OPT_TRADDR		= 1 << 2,
	NVMF_OPT_TRSVCID	= 1 << 3,
	NVMF_OPT_QUEUE_SIZE	= 1 << 4,
	NVMF_OPT_NR_IO_QUEUES	= 1 << 5,
	NVMF_OPT_TL_RETRY_COUNT	= 1 << 6,
	NVMF_OPT_KATO		= 1 << 7,
	NVMF_OPT_HOSTNQN	= 1 << 8,
	NVMF_OPT_RECONNECT_DELAY = 1 << 9,
	NVMF_OPT_HOST_TRADDR	= 1 << 10,
	NVMF_OPT_CTRL_LOSS_TMO	= 1 << 11,
	NVMF_OPT_HOST_ID	= 1 << 12,
	NVMF_OPT_DUP_CONNECT	= 1 << 13,
	NVMF_OPT_DISABLE_SQFLOW = 1 << 14,
	NVMF_OPT_HDR_DIGEST	= 1 << 15,
	NVMF_OPT_DATA_DIGEST	= 1 << 16,
	NVMF_OPT_NR_WRITE_QUEUES = 1 << 17,
	NVMF_OPT_NR_POLL_QUEUES = 1 << 18,
	NVMF_OPT_TOS		= 1 << 19,
	NVMF_OPT_FAIL_FAST_TMO	= 1 << 20,
	NVMF_OPT_HOST_IFACE	= 1 << 21,
	NVMF_OPT_DISCOVERY	= 1 << 22,
	NVMF_OPT_DHCHAP_SECRET	= 1 << 23,
	NVMF_OPT_DHCHAP_CTRL_SECRET = 1 << 24,
	NVMF_OPT_TLS		= 1 << 25,
	NVMF_OPT_KEYRING	= 1 << 26,
	NVMF_OPT_TLS_KEY	= 1 << 27,
};

/**
 * struct nvmf_ctrl_options - Used to hold the options specified
 *			      with the parsing opts enum.
 * @mask:	Used by the fabrics library to parse through sysfs options
 *		on adding a NVMe controller.
 * @max_reconnects: maximum number of allowed reconnect attempts before removing
 *		the controller, (-1) means reconnect forever, zero means remove
 *		immediately;
 * @transport:	Holds the fabric transport "technology name" (for a lack of
 *		better description) that will be used by an NVMe controller
 *		being added.
 * @subsysnqn:	Hold the fully qualified NQN subystem name (format defined
 *		in the NVMe specification, "NVMe Qualified Names").
 * @traddr:	The transport-specific TRADDR field for a port on the
 *              subsystem which is adding a controller.
 * @trsvcid:	The transport-specific TRSVCID field for a port on the
 *              subsystem which is adding a controller.
 * @host_traddr: A transport-specific field identifying the NVME host port
 *     to use for the connection to the controller.
 * @host_iface: A transport-specific field identifying the NVME host
 *     interface to use for the connection to the controller.
 * @queue_size: Number of IO queue elements.
 * @nr_io_queues: Number of controller IO queues that will be established.
 * @reconnect_delay: Time between two consecutive reconnect attempts.
 * @discovery_nqn: indicates if the subsysnqn is the well-known discovery NQN.
 * @kato:	Keep-alive timeout.
 * @host:	Virtual NVMe host, contains the NQN and Host ID.
 * @dhchap_secret: DH-HMAC-CHAP secret
 * @dhchap_ctrl_secret: DH-HMAC-CHAP controller secret for bi-directional
 *              authentication
 * @keyring:    Keyring to use for key lookups
 * @tls_key:    TLS key for encrypted connections (TCP)
 * @tls:        Start TLS encrypted connections (TCP)
 * @disable_sqflow: disable controller sq flow control
 * @hdr_digest: generate/verify header digest (TCP)
 * @data_digest: generate/verify data digest (TCP)
 * @nr_write_queues: number of queues for write I/O
 * @nr_poll_queues: number of queues for polling I/O
 * @tos: type of service
 * @fast_io_fail_tmo: Fast I/O fail timeout in seconds
 */
struct nvmf_ctrl_options {
	unsigned		mask;
	int			max_reconnects;
	char			*transport;
	char			*subsysnqn;
	char			*traddr;
	char			*trsvcid;
	char			*host_traddr;
	char			*host_iface;
	size_t			queue_size;
	unsigned int		nr_io_queues;
	unsigned int		reconnect_delay;
	bool			discovery_nqn;
	bool			duplicate_connect;
	unsigned int		kato;
	struct nvmf_host	*host;
	char			*dhchap_secret;
	char			*dhchap_ctrl_secret;
	struct key		*keyring;
	struct key		*tls_key;
	bool			tls;
	bool			disable_sqflow;
	bool			hdr_digest;
	bool			data_digest;
	unsigned int		nr_write_queues;
	unsigned int		nr_poll_queues;
	int			tos;
	int			fast_io_fail_tmo;
};

/*
 * struct nvmf_transport_ops - used to register a specific
 *			       fabric implementation of NVMe fabrics.
 * @entry:		Used by the fabrics library to add the new
 *			registration entry to its linked-list internal tree.
 * @module:             Transport module reference
 * @name:		Name of the NVMe fabric driver implementation.
 * @required_opts:	sysfs command-line options that must be specified
 *			when adding a new NVMe controller.
 * @allowed_opts:	sysfs command-line options that can be specified
 *			when adding a new NVMe controller.
 * @create_ctrl():	function pointer that points to a non-NVMe
 *			implementation-specific fabric technology
 *			that would go into starting up that fabric
 *			for the purpose of conneciton to an NVMe controller
 *			using that fabric technology.
 *
 * Notes:
 *	1. At minimum, 'required_opts' and 'allowed_opts' should
 *	   be set to the same enum parsing options defined earlier.
 *	2. create_ctrl() must be defined (even if it does nothing)
 *	3. struct nvmf_transport_ops must be statically allocated in the
 *	   modules .bss section so that a pure module_get on @module
 *	   prevents the memory from beeing freed.
 */
struct nvmf_transport_ops {
	struct list_head	entry;
	struct module		*module;
	const char		*name;
	int			required_opts;
	int			allowed_opts;
	struct nvme_ctrl	*(*create_ctrl)(struct device *dev,
					struct nvmf_ctrl_options *opts);
};

static inline bool
nvmf_ctlr_matches_baseopts(struct nvme_ctrl *ctrl,
			struct nvmf_ctrl_options *opts)
{
	enum nvme_ctrl_state state = nvme_ctrl_state(ctrl);

	if (state == NVME_CTRL_DELETING ||
	    state == NVME_CTRL_DELETING_NOIO ||
	    state == NVME_CTRL_DEAD ||
	    strcmp(opts->subsysnqn, ctrl->opts->subsysnqn) ||
	    strcmp(opts->host->nqn, ctrl->opts->host->nqn) ||
	    !uuid_equal(&opts->host->id, &ctrl->opts->host->id))
		return false;

	return true;
}

static inline char *nvmf_ctrl_subsysnqn(struct nvme_ctrl *ctrl)
{
	if (!ctrl->subsys ||
	    !strcmp(ctrl->opts->subsysnqn, NVME_DISC_SUBSYS_NAME))
		return ctrl->opts->subsysnqn;
	return ctrl->subsys->subnqn;
}

static inline void nvmf_complete_timed_out_request(struct request *rq)
{
	if (blk_mq_request_started(rq) && !blk_mq_request_completed(rq)) {
		nvme_req(rq)->status = NVME_SC_HOST_ABORTED_CMD;
		blk_mq_complete_request(rq);
	}
}

static inline unsigned int nvmf_nr_io_queues(struct nvmf_ctrl_options *opts)
{
	return min(opts->nr_io_queues, num_online_cpus()) +
		min(opts->nr_write_queues, num_online_cpus()) +
		min(opts->nr_poll_queues, num_online_cpus());
}

int nvmf_reg_read32(struct nvme_ctrl *ctrl, u32 off, u32 *val);
int nvmf_reg_read64(struct nvme_ctrl *ctrl, u32 off, u64 *val);
int nvmf_reg_write32(struct nvme_ctrl *ctrl, u32 off, u32 val);
int nvmf_subsystem_reset(struct nvme_ctrl *ctrl);
int nvmf_connect_admin_queue(struct nvme_ctrl *ctrl);
int nvmf_connect_io_queue(struct nvme_ctrl *ctrl, u16 qid);
int nvmf_register_transport(struct nvmf_transport_ops *ops);
void nvmf_unregister_transport(struct nvmf_transport_ops *ops);
void nvmf_free_options(struct nvmf_ctrl_options *opts);
int nvmf_get_address(struct nvme_ctrl *ctrl, char *buf, int size);
bool nvmf_should_reconnect(struct nvme_ctrl *ctrl, int status);
bool nvmf_ip_options_match(struct nvme_ctrl *ctrl,
		struct nvmf_ctrl_options *opts);
void nvmf_set_io_queues(struct nvmf_ctrl_options *opts, u32 nr_io_queues,
			u32 io_queues[HCTX_MAX_TYPES]);
void nvmf_map_queues(struct blk_mq_tag_set *set, struct nvme_ctrl *ctrl,
		     u32 io_queues[HCTX_MAX_TYPES]);

#endif /* _NVME_FABRICS_H */
