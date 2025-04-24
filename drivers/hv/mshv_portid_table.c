// SPDX-License-Identifier: GPL-2.0
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/idr.h>
#include <asm/mshyperv.h>

#include "mshv.h"
#include "mshv_root.h"

/*
 * Ports and connections are hypervisor struct used for inter-partition
 * communication. Port represents the source and connection represents
 * the destination. Partitions are responsible for managing the port and
 * connection ids.
 *
 */

#define PORTID_MIN	1
#define PORTID_MAX	INT_MAX

static DEFINE_IDR(port_table_idr);

void
mshv_port_table_fini(void)
{
	struct port_table_info *port_info;
	unsigned long i, tmp;

	idr_lock(&port_table_idr);
	if (!idr_is_empty(&port_table_idr)) {
		idr_for_each_entry_ul(&port_table_idr, port_info, tmp, i) {
			port_info = idr_remove(&port_table_idr, i);
			kfree_rcu(port_info, portbl_rcu);
		}
	}
	idr_unlock(&port_table_idr);
}

int
mshv_portid_alloc(struct port_table_info *info)
{
	int ret = 0;

	idr_lock(&port_table_idr);
	ret = idr_alloc(&port_table_idr, info, PORTID_MIN,
			PORTID_MAX, GFP_KERNEL);
	idr_unlock(&port_table_idr);

	return ret;
}

void
mshv_portid_free(int port_id)
{
	struct port_table_info *info;

	idr_lock(&port_table_idr);
	info = idr_remove(&port_table_idr, port_id);
	WARN_ON(!info);
	idr_unlock(&port_table_idr);

	synchronize_rcu();
	kfree(info);
}

int
mshv_portid_lookup(int port_id, struct port_table_info *info)
{
	struct port_table_info *_info;
	int ret = -ENOENT;

	rcu_read_lock();
	_info = idr_find(&port_table_idr, port_id);
	rcu_read_unlock();

	if (_info) {
		*info = *_info;
		ret = 0;
	}

	return ret;
}
