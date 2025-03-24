// SPDX-License-Identifier: GPL-2.0+
#include "sparx5_main.h"

void sparx5_pgid_init(struct sparx5 *spx5)
{
	int i;

	for (i = 0; i < spx5->data->consts->n_pgids; i++)
		spx5->pgid_map[i] = SPX5_PGID_FREE;

	/* Reserved for unicast, flood control, broadcast, and CPU.
	 * These cannot be freed.
	 */
	for (i = 0; i <= sparx5_get_pgid(spx5, PGID_CPU); i++)
		spx5->pgid_map[i] = SPX5_PGID_RESERVED;
}

int sparx5_pgid_alloc_mcast(struct sparx5 *spx5, u16 *idx)
{
	int i;

	/* The multicast area starts at index 65, but the first 7
	 * are reserved for flood masks and CPU. Start alloc after that.
	 */
	for (i = sparx5_get_pgid(spx5, PGID_MCAST_START);
	     i < spx5->data->consts->n_pgids; i++) {
		if (spx5->pgid_map[i] == SPX5_PGID_FREE) {
			spx5->pgid_map[i] = SPX5_PGID_MULTICAST;
			*idx = i;
			return 0;
		}
	}

	return -EBUSY;
}

int sparx5_pgid_free(struct sparx5 *spx5, u16 idx)
{
	if (idx <= sparx5_get_pgid(spx5, PGID_CPU) ||
	    idx >= spx5->data->consts->n_pgids)
		return -EINVAL;

	if (spx5->pgid_map[idx] == SPX5_PGID_FREE)
		return -EINVAL;

	spx5->pgid_map[idx] = SPX5_PGID_FREE;
	return 0;
}

int sparx5_get_pgid(struct sparx5 *sparx5, int pgid)
{
	return sparx5->data->consts->n_ports + pgid;
}
