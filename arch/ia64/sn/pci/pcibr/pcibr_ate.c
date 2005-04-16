/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2001-2004 Silicon Graphics, Inc. All rights reserved.
 */

#include <linux/types.h>
#include <asm/sn/sn_sal.h>
#include "pci/pcibus_provider_defs.h"
#include "pci/pcidev.h"
#include "pci/pcibr_provider.h"

int pcibr_invalidate_ate = 0;	/* by default don't invalidate ATE on free */

/*
 * mark_ate: Mark the ate as either free or inuse.
 */
static void mark_ate(struct ate_resource *ate_resource, int start, int number,
		     uint64_t value)
{

	uint64_t *ate = ate_resource->ate;
	int index;
	int length = 0;

	for (index = start; length < number; index++, length++)
		ate[index] = value;

}

/*
 * find_free_ate:  Find the first free ate index starting from the given
 *		   index for the desired consequtive count.
 */
static int find_free_ate(struct ate_resource *ate_resource, int start,
			 int count)
{

	uint64_t *ate = ate_resource->ate;
	int index;
	int start_free;

	for (index = start; index < ate_resource->num_ate;) {
		if (!ate[index]) {
			int i;
			int free;
			free = 0;
			start_free = index;	/* Found start free ate */
			for (i = start_free; i < ate_resource->num_ate; i++) {
				if (!ate[i]) {	/* This is free */
					if (++free == count)
						return start_free;
				} else {
					index = i + 1;
					break;
				}
			}
		} else
			index++;	/* Try next ate */
	}

	return -1;
}

/*
 * free_ate_resource:  Free the requested number of ATEs.
 */
static inline void free_ate_resource(struct ate_resource *ate_resource,
				     int start)
{

	mark_ate(ate_resource, start, ate_resource->ate[start], 0);
	if ((ate_resource->lowest_free_index > start) ||
	    (ate_resource->lowest_free_index < 0))
		ate_resource->lowest_free_index = start;

}

/*
 * alloc_ate_resource:  Allocate the requested number of ATEs.
 */
static inline int alloc_ate_resource(struct ate_resource *ate_resource,
				     int ate_needed)
{

	int start_index;

	/*
	 * Check for ate exhaustion.
	 */
	if (ate_resource->lowest_free_index < 0)
		return -1;

	/*
	 * Find the required number of free consequtive ates.
	 */
	start_index =
	    find_free_ate(ate_resource, ate_resource->lowest_free_index,
			  ate_needed);
	if (start_index >= 0)
		mark_ate(ate_resource, start_index, ate_needed, ate_needed);

	ate_resource->lowest_free_index =
	    find_free_ate(ate_resource, ate_resource->lowest_free_index, 1);

	return start_index;
}

/*
 * Allocate "count" contiguous Bridge Address Translation Entries
 * on the specified bridge to be used for PCI to XTALK mappings.
 * Indices in rm map range from 1..num_entries.  Indicies returned
 * to caller range from 0..num_entries-1.
 *
 * Return the start index on success, -1 on failure.
 */
int pcibr_ate_alloc(struct pcibus_info *pcibus_info, int count)
{
	int status = 0;
	uint64_t flag;

	flag = pcibr_lock(pcibus_info);
	status = alloc_ate_resource(&pcibus_info->pbi_int_ate_resource, count);

	if (status < 0) {
		/* Failed to allocate */
		pcibr_unlock(pcibus_info, flag);
		return -1;
	}

	pcibr_unlock(pcibus_info, flag);

	return status;
}

/*
 * Setup an Address Translation Entry as specified.  Use either the Bridge
 * internal maps or the external map RAM, as appropriate.
 */
static inline uint64_t *pcibr_ate_addr(struct pcibus_info *pcibus_info,
				       int ate_index)
{
	if (ate_index < pcibus_info->pbi_int_ate_size) {
		return pcireg_int_ate_addr(pcibus_info, ate_index);
	}
	panic("pcibr_ate_addr: invalid ate_index 0x%x", ate_index);
}

/*
 * Update the ate.
 */
void inline
ate_write(struct pcibus_info *pcibus_info, int ate_index, int count,
	  volatile uint64_t ate)
{
	while (count-- > 0) {
		if (ate_index < pcibus_info->pbi_int_ate_size) {
			pcireg_int_ate_set(pcibus_info, ate_index, ate);
		} else {
			panic("ate_write: invalid ate_index 0x%x", ate_index);
		}
		ate_index++;
		ate += IOPGSIZE;
	}

	pcireg_tflush_get(pcibus_info);	/* wait until Bridge PIO complete */
}

void pcibr_ate_free(struct pcibus_info *pcibus_info, int index)
{

	volatile uint64_t ate;
	int count;
	uint64_t flags;

	if (pcibr_invalidate_ate) {
		/* For debugging purposes, clear the valid bit in the ATE */
		ate = *pcibr_ate_addr(pcibus_info, index);
		count = pcibus_info->pbi_int_ate_resource.ate[index];
		ate_write(pcibus_info, index, count, (ate & ~PCI32_ATE_V));
	}

	flags = pcibr_lock(pcibus_info);
	free_ate_resource(&pcibus_info->pbi_int_ate_resource, index);
	pcibr_unlock(pcibus_info, flags);
}
