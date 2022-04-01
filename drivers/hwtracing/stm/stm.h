/* SPDX-License-Identifier: GPL-2.0 */
/*
 * System Trace Module (STM) infrastructure
 * Copyright (c) 2014, Intel Corporation.
 *
 * STM class implements generic infrastructure for  System Trace Module devices
 * as defined in MIPI STPv2 specification.
 */

#ifndef _STM_STM_H_
#define _STM_STM_H_

#include <linux/configfs.h>

struct stp_policy;
struct stp_policy_node;
struct stm_protocol_driver;

int stp_configfs_init(void);
void stp_configfs_exit(void);

void *stp_policy_node_priv(struct stp_policy_node *pn);

struct stp_master {
	unsigned int	nr_free;
	unsigned long	chan_map[];
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
	/* framing protocol in use */
	const struct stm_protocol_driver	*pdrv;
	const struct config_item_type		*pdrv_node_type;
	/* master allocation */
	spinlock_t		mc_lock;
	struct stp_master	*masters[];
};

#define to_stm_device(_d)				\
	container_of((_d), struct stm_device, dev)

struct stp_policy_node *
stp_policy_node_lookup(struct stm_device *stm, char *s);
void stp_policy_node_put(struct stp_policy_node *policy_node);
void stp_policy_unbind(struct stp_policy *policy);

void stp_policy_node_get_ranges(struct stp_policy_node *policy_node,
				unsigned int *mstart, unsigned int *mend,
				unsigned int *cstart, unsigned int *cend);

const struct config_item_type *
get_policy_node_type(struct configfs_attribute **attrs);

struct stm_output {
	spinlock_t		lock;
	unsigned int		master;
	unsigned int		channel;
	unsigned int		nr_chans;
	void			*pdrv_private;
};

struct stm_file {
	struct stm_device	*stm;
	struct stm_output	output;
};

struct stm_device *stm_find_device(const char *name);
void stm_put_device(struct stm_device *stm);

struct stm_source_device {
	struct device		dev;
	struct stm_source_data	*data;
	struct mutex	link_mutex;
	spinlock_t		link_lock;
	struct stm_device __rcu	*link;
	struct list_head	link_entry;
	/* one output per stm_source device */
	struct stm_output	output;
};

#define to_stm_source_device(_d)				\
	container_of((_d), struct stm_source_device, dev)

void *to_pdrv_policy_node(struct config_item *item);

struct stm_protocol_driver {
	struct module	*owner;
	const char	*name;
	ssize_t		(*write)(struct stm_data *data,
				 struct stm_output *output, unsigned int chan,
				 const char *buf, size_t count);
	void		(*policy_node_init)(void *arg);
	int		(*output_open)(void *priv, struct stm_output *output);
	void		(*output_close)(struct stm_output *output);
	ssize_t		priv_sz;
	struct configfs_attribute	**policy_attr;
};

int stm_register_protocol(const struct stm_protocol_driver *pdrv);
void stm_unregister_protocol(const struct stm_protocol_driver *pdrv);
int stm_lookup_protocol(const char *name,
			const struct stm_protocol_driver **pdrv,
			const struct config_item_type **type);
void stm_put_protocol(const struct stm_protocol_driver *pdrv);
ssize_t stm_data_write(struct stm_data *data, unsigned int m,
		       unsigned int c, bool ts_first, const void *buf,
		       size_t count);

#endif /* _STM_STM_H_ */
