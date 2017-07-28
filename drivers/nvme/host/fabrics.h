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
};

/**
 * struct nvmf_ctrl_options - Used to hold the options specified
 *			      with the parsing opts enum.
 * @mask:	Used by the fabrics library to parse through sysfs options
 *		on adding a NVMe controller.
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
 *              to use for the connection to the controller.
 * @queue_size: Number of IO queue elements.
 * @nr_io_queues: Number of controller IO queues that will be established.
 * @reconnect_delay: Time between two consecutive reconnect attempts.
 * @discovery_nqn: indicates if the subsysnqn is the well-known discovery NQN.
 * @kato:	Keep-alive timeout.
 * @host:	Virtual NVMe host, contains the NQN and Host ID.
 * @max_reconnects: maximum number of allowed reconnect attempts before removing
 *              the controller, (-1) means reconnect forever, zero means remove
 *              immediately;
 */
struct nvmf_ctrl_options {
	unsigned		mask;
	char			*transport;
	char			*subsysnqn;
	char			*traddr;
	char			*trsvcid;
	char			*host_traddr;
	size_t			queue_size;
	unsigned int		nr_io_queues;
	unsigned int		reconnect_delay;
	bool			discovery_nqn;
	unsigned int		kato;
	struct nvmf_host	*host;
	int			max_reconnects;
};

/*
 * struct nvmf_transport_ops - used to register a specific
 *			       fabric implementation of NVMe fabrics.
 * @entry:		Used by the fabrics library to add the new
 *			registration entry to its linked-list internal tree.
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
 */
struct nvmf_transport_ops {
	struct list_head	entry;
	const char		*name;
	int			required_opts;
	int			allowed_opts;
	struct nvme_ctrl	*(*create_ctrl)(struct device *dev,
					struct nvmf_ctrl_options *opts);
};

int nvmf_reg_read32(struct nvme_ctrl *ctrl, u32 off, u32 *val);
int nvmf_reg_read64(struct nvme_ctrl *ctrl, u32 off, u64 *val);
int nvmf_reg_write32(struct nvme_ctrl *ctrl, u32 off, u32 val);
int nvmf_connect_admin_queue(struct nvme_ctrl *ctrl);
int nvmf_connect_io_queue(struct nvme_ctrl *ctrl, u16 qid);
int nvmf_register_transport(struct nvmf_transport_ops *ops);
void nvmf_unregister_transport(struct nvmf_transport_ops *ops);
void nvmf_free_options(struct nvmf_ctrl_options *opts);
int nvmf_get_address(struct nvme_ctrl *ctrl, char *buf, int size);
bool nvmf_should_reconnect(struct nvme_ctrl *ctrl);

#endif /* _NVME_FABRICS_H */
