/*
 * Some of the code in this file has been gleaned from the 64 bit 
 * discontigmem support code base.
 *
 * Copyright (C) 2002, IBM Corp.
 *
 * All rights reserved.          
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to Pat Gaughen <gone@us.ibm.com>
 */
#include <linux/config.h>
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <linux/acpi.h>
#include <linux/nodemask.h>
#include <asm/srat.h>
#include <asm/topology.h>

/*
 * proximity macros and definitions
 */
#define NODE_ARRAY_INDEX(x)	((x) / 8)	/* 8 bits/char */
#define NODE_ARRAY_OFFSET(x)	((x) % 8)	/* 8 bits/char */
#define BMAP_SET(bmap, bit)	((bmap)[NODE_ARRAY_INDEX(bit)] |= 1 << NODE_ARRAY_OFFSET(bit))
#define BMAP_TEST(bmap, bit)	((bmap)[NODE_ARRAY_INDEX(bit)] & (1 << NODE_ARRAY_OFFSET(bit)))
#define MAX_PXM_DOMAINS		256	/* 1 byte and no promises about values */
/* bitmap length; _PXM is at most 255 */
#define PXM_BITMAP_LEN (MAX_PXM_DOMAINS / 8) 
static u8 pxm_bitmap[PXM_BITMAP_LEN];	/* bitmap of proximity domains */

#define MAX_CHUNKS_PER_NODE	4
#define MAXCHUNKS		(MAX_CHUNKS_PER_NODE * MAX_NUMNODES)
struct node_memory_chunk_s {
	unsigned long	start_pfn;
	unsigned long	end_pfn;
	u8	pxm;		// proximity domain of node
	u8	nid;		// which cnode contains this chunk?
	u8	bank;		// which mem bank on this node
};
static struct node_memory_chunk_s node_memory_chunk[MAXCHUNKS];

static int num_memory_chunks;		/* total number of memory chunks */
static int zholes_size_init;
static unsigned long zholes_size[MAX_NUMNODES * MAX_NR_ZONES];

extern void * boot_ioremap(unsigned long, unsigned long);

/* Identify CPU proximity domains */
static void __init parse_cpu_affinity_structure(char *p)
{
	struct acpi_table_processor_affinity *cpu_affinity = 
				(struct acpi_table_processor_affinity *) p;

	if (!cpu_affinity->flags.enabled)
		return;		/* empty entry */

	/* mark this node as "seen" in node bitmap */
	BMAP_SET(pxm_bitmap, cpu_affinity->proximity_domain);

	printk("CPU 0x%02X in proximity domain 0x%02X\n",
		cpu_affinity->apic_id, cpu_affinity->proximity_domain);
}

/*
 * Identify memory proximity domains and hot-remove capabilities.
 * Fill node memory chunk list structure.
 */
static void __init parse_memory_affinity_structure (char *sratp)
{
	unsigned long long paddr, size;
	unsigned long start_pfn, end_pfn; 
	u8 pxm;
	struct node_memory_chunk_s *p, *q, *pend;
	struct acpi_table_memory_affinity *memory_affinity =
			(struct acpi_table_memory_affinity *) sratp;

	if (!memory_affinity->flags.enabled)
		return;		/* empty entry */

	/* mark this node as "seen" in node bitmap */
	BMAP_SET(pxm_bitmap, memory_affinity->proximity_domain);

	/* calculate info for memory chunk structure */
	paddr = memory_affinity->base_addr_hi;
	paddr = (paddr << 32) | memory_affinity->base_addr_lo;
	size = memory_affinity->length_hi;
	size = (size << 32) | memory_affinity->length_lo;
	
	start_pfn = paddr >> PAGE_SHIFT;
	end_pfn = (paddr + size) >> PAGE_SHIFT;
	
	pxm = memory_affinity->proximity_domain;

	if (num_memory_chunks >= MAXCHUNKS) {
		printk("Too many mem chunks in SRAT. Ignoring %lld MBytes at %llx\n",
			size/(1024*1024), paddr);
		return;
	}

	/* Insertion sort based on base address */
	pend = &node_memory_chunk[num_memory_chunks];
	for (p = &node_memory_chunk[0]; p < pend; p++) {
		if (start_pfn < p->start_pfn)
			break;
	}
	if (p < pend) {
		for (q = pend; q >= p; q--)
			*(q + 1) = *q;
	}
	p->start_pfn = start_pfn;
	p->end_pfn = end_pfn;
	p->pxm = pxm;

	num_memory_chunks++;

	printk("Memory range 0x%lX to 0x%lX (type 0x%X) in proximity domain 0x%02X %s\n",
		start_pfn, end_pfn,
		memory_affinity->memory_type,
		memory_affinity->proximity_domain,
		(memory_affinity->flags.hot_pluggable ?
		 "enabled and removable" : "enabled" ) );
}

#if MAX_NR_ZONES != 3
#error "MAX_NR_ZONES != 3, chunk_to_zone requires review"
#endif
/* Take a chunk of pages from page frame cstart to cend and count the number
 * of pages in each zone, returned via zones[].
 */
static __init void chunk_to_zones(unsigned long cstart, unsigned long cend, 
		unsigned long *zones)
{
	unsigned long max_dma;
	extern unsigned long max_low_pfn;

	int z;
	unsigned long rend;

	/* FIXME: MAX_DMA_ADDRESS and max_low_pfn are trying to provide
	 * similarly scoped information and should be handled in a consistant
	 * manner.
	 */
	max_dma = virt_to_phys((char *)MAX_DMA_ADDRESS) >> PAGE_SHIFT;

	/* Split the hole into the zones in which it falls.  Repeatedly
	 * take the segment in which the remaining hole starts, round it
	 * to the end of that zone.
	 */
	memset(zones, 0, MAX_NR_ZONES * sizeof(long));
	while (cstart < cend) {
		if (cstart < max_dma) {
			z = ZONE_DMA;
			rend = (cend < max_dma)? cend : max_dma;

		} else if (cstart < max_low_pfn) {
			z = ZONE_NORMAL;
			rend = (cend < max_low_pfn)? cend : max_low_pfn;

		} else {
			z = ZONE_HIGHMEM;
			rend = cend;
		}
		zones[z] += rend - cstart;
		cstart = rend;
	}
}

/*
 * The SRAT table always lists ascending addresses, so can always
 * assume that the first "start" address that you see is the real
 * start of the node, and that the current "end" address is after
 * the previous one.
 */
static __init void node_read_chunk(int nid, struct node_memory_chunk_s *memory_chunk)
{
	/*
	 * Only add present memory as told by the e820.
	 * There is no guarantee from the SRAT that the memory it
	 * enumerates is present at boot time because it represents
	 * *possible* memory hotplug areas the same as normal RAM.
	 */
	if (memory_chunk->start_pfn >= max_pfn) {
		printk (KERN_INFO "Ignoring SRAT pfns: 0x%08lx -> %08lx\n",
			memory_chunk->start_pfn, memory_chunk->end_pfn);
		return;
	}
	if (memory_chunk->nid != nid)
		return;

	if (!node_has_online_mem(nid))
		node_start_pfn[nid] = memory_chunk->start_pfn;

	if (node_start_pfn[nid] > memory_chunk->start_pfn)
		node_start_pfn[nid] = memory_chunk->start_pfn;

	if (node_end_pfn[nid] < memory_chunk->end_pfn)
		node_end_pfn[nid] = memory_chunk->end_pfn;
}

static u8 pxm_to_nid_map[MAX_PXM_DOMAINS];/* _PXM to logical node ID map */

int pxm_to_node(int pxm)
{
	return pxm_to_nid_map[pxm];
}

/* Parse the ACPI Static Resource Affinity Table */
static int __init acpi20_parse_srat(struct acpi_table_srat *sratp)
{
	u8 *start, *end, *p;
	int i, j, nid;
	u8 nid_to_pxm_map[MAX_NUMNODES];/* logical node ID to _PXM map */

	start = (u8 *)(&(sratp->reserved) + 1);	/* skip header */
	p = start;
	end = (u8 *)sratp + sratp->header.length;

	memset(pxm_bitmap, 0, sizeof(pxm_bitmap));	/* init proximity domain bitmap */
	memset(node_memory_chunk, 0, sizeof(node_memory_chunk));
	memset(zholes_size, 0, sizeof(zholes_size));

	/* -1 in these maps means not available */
	memset(pxm_to_nid_map, -1, sizeof(pxm_to_nid_map));
	memset(nid_to_pxm_map, -1, sizeof(nid_to_pxm_map));

	num_memory_chunks = 0;
	while (p < end) {
		switch (*p) {
		case ACPI_SRAT_PROCESSOR_AFFINITY:
			parse_cpu_affinity_structure(p);
			break;
		case ACPI_SRAT_MEMORY_AFFINITY:
			parse_memory_affinity_structure(p);
			break;
		default:
			printk("ACPI 2.0 SRAT: unknown entry skipped: type=0x%02X, len=%d\n", p[0], p[1]);
			break;
		}
		p += p[1];
		if (p[1] == 0) {
			printk("acpi20_parse_srat: Entry length value is zero;"
				" can't parse any further!\n");
			break;
		}
	}

	if (num_memory_chunks == 0) {
		printk("could not finy any ACPI SRAT memory areas.\n");
		goto out_fail;
	}

	/* Calculate total number of nodes in system from PXM bitmap and create
	 * a set of sequential node IDs starting at zero.  (ACPI doesn't seem
	 * to specify the range of _PXM values.)
	 */
	/*
	 * MCD - we no longer HAVE to number nodes sequentially.  PXM domain
	 * numbers could go as high as 256, and MAX_NUMNODES for i386 is typically
	 * 32, so we will continue numbering them in this manner until MAX_NUMNODES
	 * approaches MAX_PXM_DOMAINS for i386.
	 */
	nodes_clear(node_online_map);
	for (i = 0; i < MAX_PXM_DOMAINS; i++) {
		if (BMAP_TEST(pxm_bitmap, i)) {
			nid = num_online_nodes();
			pxm_to_nid_map[i] = nid;
			nid_to_pxm_map[nid] = i;
			node_set_online(nid);
		}
	}
	BUG_ON(num_online_nodes() == 0);

	/* set cnode id in memory chunk structure */
	for (i = 0; i < num_memory_chunks; i++)
		node_memory_chunk[i].nid = pxm_to_nid_map[node_memory_chunk[i].pxm];

	printk("pxm bitmap: ");
	for (i = 0; i < sizeof(pxm_bitmap); i++) {
		printk("%02X ", pxm_bitmap[i]);
	}
	printk("\n");
	printk("Number of logical nodes in system = %d\n", num_online_nodes());
	printk("Number of memory chunks in system = %d\n", num_memory_chunks);

	for (j = 0; j < num_memory_chunks; j++){
		struct node_memory_chunk_s * chunk = &node_memory_chunk[j];
		printk("chunk %d nid %d start_pfn %08lx end_pfn %08lx\n",
		       j, chunk->nid, chunk->start_pfn, chunk->end_pfn);
		node_read_chunk(chunk->nid, chunk);
	}
 
	for_each_online_node(nid) {
		unsigned long start = node_start_pfn[nid];
		unsigned long end = node_end_pfn[nid];

		memory_present(nid, start, end);
		node_remap_size[nid] = node_memmap_size_bytes(nid, start, end);
	}
	return 1;
out_fail:
	return 0;
}

int __init get_memcfg_from_srat(void)
{
	struct acpi_table_header *header = NULL;
	struct acpi_table_rsdp *rsdp = NULL;
	struct acpi_table_rsdt *rsdt = NULL;
	struct acpi_pointer *rsdp_address = NULL;
	struct acpi_table_rsdt saved_rsdt;
	int tables = 0;
	int i = 0;

	if (ACPI_FAILURE(acpi_find_root_pointer(ACPI_PHYSICAL_ADDRESSING,
						rsdp_address))) {
		printk("%s: System description tables not found\n",
		       __FUNCTION__);
		goto out_err;
	}

	if (rsdp_address->pointer_type == ACPI_PHYSICAL_POINTER) {
		printk("%s: assigning address to rsdp\n", __FUNCTION__);
		rsdp = (struct acpi_table_rsdp *)
				(u32)rsdp_address->pointer.physical;
	} else {
		printk("%s: rsdp_address is not a physical pointer\n", __FUNCTION__);
		goto out_err;
	}
	if (!rsdp) {
		printk("%s: Didn't find ACPI root!\n", __FUNCTION__);
		goto out_err;
	}

	printk(KERN_INFO "%.8s v%d [%.6s]\n", rsdp->signature, rsdp->revision,
		rsdp->oem_id);

	if (strncmp(rsdp->signature, RSDP_SIG,strlen(RSDP_SIG))) {
		printk(KERN_WARNING "%s: RSDP table signature incorrect\n", __FUNCTION__);
		goto out_err;
	}

	rsdt = (struct acpi_table_rsdt *)
	    boot_ioremap(rsdp->rsdt_address, sizeof(struct acpi_table_rsdt));

	if (!rsdt) {
		printk(KERN_WARNING
		       "%s: ACPI: Invalid root system description tables (RSDT)\n",
		       __FUNCTION__);
		goto out_err;
	}

	header = & rsdt->header;

	if (strncmp(header->signature, RSDT_SIG, strlen(RSDT_SIG))) {
		printk(KERN_WARNING "ACPI: RSDT signature incorrect\n");
		goto out_err;
	}

	/* 
	 * The number of tables is computed by taking the 
	 * size of all entries (header size minus total 
	 * size of RSDT) divided by the size of each entry
	 * (4-byte table pointers).
	 */
	tables = (header->length - sizeof(struct acpi_table_header)) / 4;

	if (!tables)
		goto out_err;

	memcpy(&saved_rsdt, rsdt, sizeof(saved_rsdt));

	if (saved_rsdt.header.length > sizeof(saved_rsdt)) {
		printk(KERN_WARNING "ACPI: Too big length in RSDT: %d\n",
		       saved_rsdt.header.length);
		goto out_err;
	}

	printk("Begin SRAT table scan....\n");

	for (i = 0; i < tables; i++) {
		/* Map in header, then map in full table length. */
		header = (struct acpi_table_header *)
			boot_ioremap(saved_rsdt.entry[i], sizeof(struct acpi_table_header));
		if (!header)
			break;
		header = (struct acpi_table_header *)
			boot_ioremap(saved_rsdt.entry[i], header->length);
		if (!header)
			break;

		if (strncmp((char *) &header->signature, "SRAT", 4))
			continue;

		/* we've found the srat table. don't need to look at any more tables */
		return acpi20_parse_srat((struct acpi_table_srat *)header);
	}
out_err:
	printk("failed to get NUMA memory information from SRAT table\n");
	return 0;
}

/* For each node run the memory list to determine whether there are
 * any memory holes.  For each hole determine which ZONE they fall
 * into.
 *
 * NOTE#1: this requires knowledge of the zone boundries and so
 * _cannot_ be performed before those are calculated in setup_memory.
 * 
 * NOTE#2: we rely on the fact that the memory chunks are ordered by
 * start pfn number during setup.
 */
static void __init get_zholes_init(void)
{
	int nid;
	int c;
	int first;
	unsigned long end = 0;

	for_each_online_node(nid) {
		first = 1;
		for (c = 0; c < num_memory_chunks; c++){
			if (node_memory_chunk[c].nid == nid) {
				if (first) {
					end = node_memory_chunk[c].end_pfn;
					first = 0;

				} else {
					/* Record any gap between this chunk
					 * and the previous chunk on this node
					 * against the zones it spans.
					 */
					chunk_to_zones(end,
						node_memory_chunk[c].start_pfn,
						&zholes_size[nid * MAX_NR_ZONES]);
				}
			}
		}
	}
}

unsigned long * __init get_zholes_size(int nid)
{
	if (!zholes_size_init) {
		zholes_size_init++;
		get_zholes_init();
	}
	if (nid >= MAX_NUMNODES || !node_online(nid))
		printk("%s: nid = %d is invalid/offline. num_online_nodes = %d",
		       __FUNCTION__, nid, num_online_nodes());
	return &zholes_size[nid * MAX_NR_ZONES];
}
