/*
 * fake_mem.c
 *
 * Copyright (C) 2015 FUJITSU LIMITED
 * Author: Taku Izumi <izumi.taku@jp.fujitsu.com>
 *
 * This code introduces new boot option named "efi_fake_mem"
 * By specifying this parameter, you can add arbitrary attribute to
 * specific memory range by updating original (firmware provided) EFI
 * memmap.
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under the terms and conditions of the GNU General Public License,
 *  version 2, as published by the Free Software Foundation.
 *
 *  This program is distributed in the hope it will be useful, but WITHOUT
 *  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 *  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 *  more details.
 *
 *  You should have received a copy of the GNU General Public License along with
 *  this program; if not, see <http://www.gnu.org/licenses/>.
 *
 *  The full GNU General Public License is included in this distribution in
 *  the file called "COPYING".
 */

#include <linux/kernel.h>
#include <linux/efi.h>
#include <linux/init.h>
#include <linux/memblock.h>
#include <linux/types.h>
#include <linux/sort.h>
#include <asm/efi.h>

#define EFI_MAX_FAKEMEM CONFIG_EFI_MAX_FAKE_MEM

struct fake_mem {
	struct range range;
	u64 attribute;
};
static struct fake_mem fake_mems[EFI_MAX_FAKEMEM];
static int nr_fake_mem;

static int __init cmp_fake_mem(const void *x1, const void *x2)
{
	const struct fake_mem *m1 = x1;
	const struct fake_mem *m2 = x2;

	if (m1->range.start < m2->range.start)
		return -1;
	if (m1->range.start > m2->range.start)
		return 1;
	return 0;
}

void __init efi_fake_memmap(void)
{
	u64 start, end, m_start, m_end, m_attr;
	int new_nr_map = memmap.nr_map;
	efi_memory_desc_t *md;
	phys_addr_t new_memmap_phy;
	void *new_memmap;
	void *old, *new;
	int i;

	if (!nr_fake_mem || !efi_enabled(EFI_MEMMAP))
		return;

	/* count up the number of EFI memory descriptor */
	for (old = memmap.map; old < memmap.map_end; old += memmap.desc_size) {
		md = old;
		start = md->phys_addr;
		end = start + (md->num_pages << EFI_PAGE_SHIFT) - 1;

		for (i = 0; i < nr_fake_mem; i++) {
			/* modifying range */
			m_start = fake_mems[i].range.start;
			m_end = fake_mems[i].range.end;

			if (m_start <= start) {
				/* split into 2 parts */
				if (start < m_end && m_end < end)
					new_nr_map++;
			}
			if (start < m_start && m_start < end) {
				/* split into 3 parts */
				if (m_end < end)
					new_nr_map += 2;
				/* split into 2 parts */
				if (end <= m_end)
					new_nr_map++;
			}
		}
	}

	/* allocate memory for new EFI memmap */
	new_memmap_phy = memblock_alloc(memmap.desc_size * new_nr_map,
					PAGE_SIZE);
	if (!new_memmap_phy)
		return;

	/* create new EFI memmap */
	new_memmap = early_memremap(new_memmap_phy,
				    memmap.desc_size * new_nr_map);
	if (!new_memmap) {
		memblock_free(new_memmap_phy, memmap.desc_size * new_nr_map);
		return;
	}

	for (old = memmap.map, new = new_memmap;
	     old < memmap.map_end;
	     old += memmap.desc_size, new += memmap.desc_size) {

		/* copy original EFI memory descriptor */
		memcpy(new, old, memmap.desc_size);
		md = new;
		start = md->phys_addr;
		end = md->phys_addr + (md->num_pages << EFI_PAGE_SHIFT) - 1;

		for (i = 0; i < nr_fake_mem; i++) {
			/* modifying range */
			m_start = fake_mems[i].range.start;
			m_end = fake_mems[i].range.end;
			m_attr = fake_mems[i].attribute;

			if (m_start <= start && end <= m_end)
				md->attribute |= m_attr;

			if (m_start <= start &&
			    (start < m_end && m_end < end)) {
				/* first part */
				md->attribute |= m_attr;
				md->num_pages = (m_end - md->phys_addr + 1) >>
					EFI_PAGE_SHIFT;
				/* latter part */
				new += memmap.desc_size;
				memcpy(new, old, memmap.desc_size);
				md = new;
				md->phys_addr = m_end + 1;
				md->num_pages = (end - md->phys_addr + 1) >>
					EFI_PAGE_SHIFT;
			}

			if ((start < m_start && m_start < end) && m_end < end) {
				/* first part */
				md->num_pages = (m_start - md->phys_addr) >>
					EFI_PAGE_SHIFT;
				/* middle part */
				new += memmap.desc_size;
				memcpy(new, old, memmap.desc_size);
				md = new;
				md->attribute |= m_attr;
				md->phys_addr = m_start;
				md->num_pages = (m_end - m_start + 1) >>
					EFI_PAGE_SHIFT;
				/* last part */
				new += memmap.desc_size;
				memcpy(new, old, memmap.desc_size);
				md = new;
				md->phys_addr = m_end + 1;
				md->num_pages = (end - m_end) >>
					EFI_PAGE_SHIFT;
			}

			if ((start < m_start && m_start < end) &&
			    (end <= m_end)) {
				/* first part */
				md->num_pages = (m_start - md->phys_addr) >>
					EFI_PAGE_SHIFT;
				/* latter part */
				new += memmap.desc_size;
				memcpy(new, old, memmap.desc_size);
				md = new;
				md->phys_addr = m_start;
				md->num_pages = (end - md->phys_addr + 1) >>
					EFI_PAGE_SHIFT;
				md->attribute |= m_attr;
			}
		}
	}

	/* swap into new EFI memmap */
	efi_unmap_memmap();
	memmap.map = new_memmap;
	memmap.phys_map = new_memmap_phy;
	memmap.nr_map = new_nr_map;
	memmap.map_end = memmap.map + memmap.nr_map * memmap.desc_size;
	set_bit(EFI_MEMMAP, &efi.flags);

	/* print new EFI memmap */
	efi_print_memmap();
}

static int __init setup_fake_mem(char *p)
{
	u64 start = 0, mem_size = 0, attribute = 0;
	int i;

	if (!p)
		return -EINVAL;

	while (*p != '\0') {
		mem_size = memparse(p, &p);
		if (*p == '@')
			start = memparse(p+1, &p);
		else
			break;

		if (*p == ':')
			attribute = simple_strtoull(p+1, &p, 0);
		else
			break;

		if (nr_fake_mem >= EFI_MAX_FAKEMEM)
			break;

		fake_mems[nr_fake_mem].range.start = start;
		fake_mems[nr_fake_mem].range.end = start + mem_size - 1;
		fake_mems[nr_fake_mem].attribute = attribute;
		nr_fake_mem++;

		if (*p == ',')
			p++;
	}

	sort(fake_mems, nr_fake_mem, sizeof(struct fake_mem),
	     cmp_fake_mem, NULL);

	for (i = 0; i < nr_fake_mem; i++)
		pr_info("efi_fake_mem: add attr=0x%016llx to [mem 0x%016llx-0x%016llx]",
			fake_mems[i].attribute, fake_mems[i].range.start,
			fake_mems[i].range.end);

	return *p == '\0' ? 0 : -EINVAL;
}

early_param("efi_fake_mem", setup_fake_mem);
