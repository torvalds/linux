/*
 * System Trace Module (STM) infrastructure
 * Copyright (c) 2014, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * STM class implements generic infrastructure for  System Trace Module devices
 * as defined in MIPI STPv2 specification.
 */

#ifndef _STM_STM_H_
#define _STM_STM_H_

struct stp_policy;
struct stp_policy_node;

struct stp_policy_node *
stp_policy_node_lookup(struct stm_device *stm, char *s);
void stp_policy_node_put(struct stp_policy_node *policy_node);
void stp_policy_unbind(struct stp_policy *policy);

void stp_policy_node_get_ranges(struct stp_policy_node *policy_node,
				unsigned int *mstart, unsigned int *mend,
				unsigned int *cstart, unsigned int *cend);
int stp_configfs_init(void);
void stp_configfs_exit(void);

struct stp_master {
	unsigned int	nr_free;
	unsigned long	chan_map[0];
};

struct stm_device {
	struct device		dev;
	struct module		*owner;
	struct stp_policy	*policy;
	struct mutex		policy_mutex;
	int			major;
	unsigned int		sw_nmasters;
	struct stm_data		*data;
	struct mutex		link_mutex;
	spinlock_t		link_lock;
	struct list_head	link_list;
	/* master allocation */
	spinlock_t		mc_lock;
	struct stp_master	*masters[0];
};

#define to_stm_device(_d)				\
	container_of((_d), struct stm_device, dev)

struct stm_output {
	spinlock_t		lock;
	unsigned int		master;
	unsigned int		channel;
	unsigned int		nr_chans;
};

struct stm_file {
	struct stm_device	*stm;
	struct stp_policy_node	*policy_node;
	struct stm_output	output;
};

struct stm_device *stm_find_device(const char *name);
void stm_put_device(struct stm_device *stm);

struct stm_source_device {
	struct device		dev;
	struct stm_source_data	*data;
	spinlock_t		link_lock;
	struct stm_device __rcu	*link;
	struct list_head	link_entry;
	/* one output per stm_source device */
	struct stp_policy_node	*policy_node;
	struct stm_output	output;
};

#define to_stm_source_device(_d)				\
	container_of((_d), struct stm_source_device, dev)

#endif /* _STM_STM_H_ */
