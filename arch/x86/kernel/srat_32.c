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
#include <linux/mm.h>
#include <linux/bootmem.h>
#include <linux/mmzone.h>
#include <linux/acpi.h>
#include <linux/nodemask.h>
#include <asm/srat.h>
#include <asm/topology.h>
#include <asm/smp.h>

/*
 * proximity macros and definitions
 */
#define NODE_ARRAY_INDEX(x)	((x) / 8)	/* 8 bits/char */
#define NODE_ARRAY_OFFSET(x)	((x) % 8)	/* 8 bits/char */
#define BMAP_SET(bmap, bit)	((bmap)[NODE_ARRAY_INDEX(bit)] |= 1 << NODE_ARRAY_OFFSET(bit))
#define BMAP_TEST(bmap, bit)	((bmap)[NODE_ARRAY_INDEX(bit)] & (1 << NODE_ARRAY_OFFSET(bit)))
/* bitmap length; _PXM is at most 255 */
#define PXM_BITMAP_LEN (MAX_PXM_DOMAINS / 8) 
static u8 pxm_bitmap[PXM_BITMAP_LEN];	/* bitmap of proximity domains */

#define MAX_CHUNKS_PER_NODE	3
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
static u8 __initdata apicid_to_pxm[MAX_APICID];

/* Identify CPU proximity domains */
static void __init parse_cpu_affinity_structure(char *p)
{
	struct acpi_srat_cpu_affinity *cpu_affinity =
				(struct acpi_srat_cpu_affinity *) p;

	if ((cpu_affinity->flags & ACPI_SRAT_CPU_ENABLED) == 0)
		return;		/* empty entry */

	/* mark this node as "seen" in node bitmap */
	BMAP_SET(pxm_bitmap, cpu_affinity->proximity_domain_lo);

	apicid_to_pxm[cpu_affinity->apic_id] = cpu_affinity->proximity_domain_lo;

	printk("CPU 0x%02X in proximity domain 0x%02X\n",
		cpu_affinity->apic_id, cpu_affinity->proximity_domain_lo);
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
	struct acpi_srat_mem_affinity *memory_affinity =
			(struct acpi_srat_mem_affinity *) sratp;

	if ((memory_affinity->flags & ACPI_SRAT_MEM_ENABLED) == 0)
		return;		/* empty entry */

	pxm = memory_affinity->proximity_domain & 0xff;

	/* mark this node as "seen" in node bitmap */
	BMAP_SET(pxm_bitmap, pxm);

	/* calculate info for memory chunk structure */
	paddr = memory_affinity->base_address;
	size = memory_affinity->length;

	start_pfn = paddr >> PAGE_SHIFT;
	end_pfn = (paddr + size) >> PAGE_SHIFT;


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
		pxm,
		((memory_affinity->flags & ACPI_SRAT_MEM_HOT_PLUGGABLE) ?
		 "enabled and removable" : "enabled" ) );
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

/* Parse the ACPI Static Resource Affinity Table */
static int __init acpi20_parse_srat(struct acpi_table_srat *sratp)
{
	u8 *start, *end, *p;
	int i, j, nid;

	start = (u8 *)(&(sratp->reserved) + 1);	/* skip header */
	p = start;
	end = (u8 *)sratp + sratp->header.length;

	memset(pxm_bitmap, 0, sizeof(pxm_bitmap));	/* init proximity domain bitmap */
	memset(node_memory_chunk, 0, sizeof(node_memory_chunk));

	num_memory_chunks = 0;
	while (p < end) {
		switch (*p) {
		case ACPI_SRAT_TYPE_CPU_AFFINITY:
			parse_cpu_affinity_structure(p);
			break;
		case ACPI_SRAT_TYPE_MEMORY_AFFINITY:
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
			int nid = acpi_map_pxm_to_node(i);
			node_set_online(nid);
		}
	}
	BUG_ON(num_online_nodes() == 0);

	/* set cnode id in memory chunk structure */
	for (i = 0; i < num_memory_chunks; i++)
		node_memory_chunk[i].nid = pxm_to_node(node_memory_chunk[i].pxm);

	printk("pxm bitmap: ");
	for (i = 0; i < sizeof(pxm_bitmap); i++) {
		printk("%02X ", pxm_bitmap[i]);
	}
	printk("\n");
	printk("Number of logical nodes in system = %d\n", num_online_nodes());
	printk("Number of memory chunks in system = %d\n", num_memory_chunks);

	for (i = 0; i < MAX_APICID; i++)
		apicid_2_node[i] = pxm_to_node(apicid_to_pxm[i]);

	for (j = 0; j < num_memory_chunks; j++){
		struct node_memory_chunk_s * chunk = &node_memory_chunk[j];
		printk("chunk %d nid %d start_pfn %08lx end_pfn %08lx\n",
		       j, chunk->nid, chunk->start_pfn, chunk->end_pfn);
		node_read_chunk(chunk->nid, chunk);
		add_active_range(chunk->nid, chunk->start_pfn, chunk->end_pfn);
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

struct acpi_static_rsdt {
	struct acpi_table_rsdt table;
	u32 padding[7]; /* Allow for 7 more table entries */
};

int __init get_memcfg_from_srat(void)
{
	struct acpi_table_header *header = NULL;
	struct acpi_table_rsdp *rsdp = NULL;
	struct acpi_table_rsdt *rsdt = NULL;
	acpi_native_uint rsdp_address = 0;
	struct acpi_static_rsdt saved_rsdt;
	int tables = 0;
	int i = 0;

	rsdp_address = acpi_find_rsdp();
	if (!rsdp_address) {
		printk("%s: System description tables not found\n",
		       __FUNCTION__);
		goto out_err;
	}

	printk("%s: assigning address to rsdp\n", __FUNCTION__);
	rsdp = (struct acpi_table_rsdp *)(u32)rsdp_address;
	if (!rsdp) {
		printk("%s: Didn't find ACPI root!\n", __FUNCTION__);
		goto out_err;
	}

	printk(KERN_INFO "%.8s v%d [%.6s]\n", rsdp->signature, rsdp->revision,
		rsdp->oem_id);

	if (strncmp(rsdp->signature, ACPI_SIG_RSDP,strlen(ACPI_SIG_RSDP))) {
		printk(KERN_WARNING "%s: RSDP table signature incorrect\n", __FUNCTION__);
		goto out_err;
	}

	rsdt = (struct acpi_table_rsdt *)
	    early_ioremap(rsdp->rsdt_physical_address, sizeof(struct acpi_table_rsdt));

	if (!rsdt) {
		printk(KERN_WARNING
		       "%s: ACPI: Invalid root system description tables (RSDT)\n",
		       __FUNCTION__);
		goto out_err;
	}

	header = &rsdt->header;

	if (strncmp(header->signature, ACPI_SIG_RSDT, strlen(ACPI_SIG_RSDT))) {
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

	if (saved_rsdt.table.header.length > sizeof(saved_rsdt)) {
		printk(KERN_WARNING "ACPI: Too big length in RSDT: %d\n",
		       saved_rsdt.table.header.length);
		goto out_err;
	}

	printk("Begin SRAT table scan....\n");

	for (i = 0; i < tables; i++) {
		/* Map in header, then map in full table length. */
		header = (struct acpi_table_header *)
			early_ioremap(saved_rsdt.table.table_offset_entry[i], sizeof(struct acpi_table_header));
		if (!header)
			break;
		header = (struct acpi_table_header *)
			early_ioremap(saved_rsdt.table.table_offset_entry[i], header->length);
		if (!header)
			break;

		if (strncmp((char *) &header->signature, ACPI_SIG_SRAT, 4))
			continue;

		/* we've found the srat table. don't need to look at any more tables */
		return acpi20_parse_srat((struct acpi_table_srat *)header);
	}
out_err:
	remove_all_active_ranges();
	printk("failed to get NUMA memory information from SRAT table\n");
	return 0;
}
