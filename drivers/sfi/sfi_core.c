/* sfi_core.c Simple Firmware Interface - core internals */

/*

  This file is provided under a dual BSD/GPLv2 license.  When using or
  redistributing this file, you may do so under either license.

  GPL LICENSE SUMMARY

  Copyright(c) 2009 Intel Corporation. All rights reserved.

  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License as
  published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.
  The full GNU General Public License is included in this distribution
  in the file called LICENSE.GPL.

  BSD LICENSE

  Copyright(c) 2009 Intel Corporation. All rights reserved.

  Redistribution and use in source and binary forms, with or without
  modification, are permitted provided that the following conditions
  are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the
      distribution.
    * Neither the name of Intel Corporation nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

*/

#define KMSG_COMPONENT "SFI"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/bootmem.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/types.h>
#include <linux/acpi.h>
#include <linux/init.h>
#include <linux/sfi.h>
#include <linux/slab.h>

#include "sfi_core.h"

#define ON_SAME_PAGE(addr1, addr2) \
	(((unsigned long)(addr1) & PAGE_MASK) == \
	((unsigned long)(addr2) & PAGE_MASK))
#define TABLE_ON_PAGE(page, table, size) (ON_SAME_PAGE(page, table) && \
				ON_SAME_PAGE(page, table + size))

int sfi_disabled __read_mostly;
EXPORT_SYMBOL(sfi_disabled);

static u64 syst_pa __read_mostly;
static struct sfi_table_simple *syst_va __read_mostly;

/*
 * FW creates and saves the SFI tables in memory. When these tables get
 * used, they may need to be mapped to virtual address space, and the mapping
 * can happen before or after the ioremap() is ready, so a flag is needed
 * to indicating this
 */
static u32 sfi_use_ioremap __read_mostly;

/*
 * sfi_un/map_memory calls early_ioremap/iounmap which is a __init function
 * and introduces section mismatch. So use __ref to make it calm.
 */
static void __iomem * __ref sfi_map_memory(u64 phys, u32 size)
{
	if (!phys || !size)
		return NULL;

	if (sfi_use_ioremap)
		return ioremap_cache(phys, size);
	else
		return early_ioremap(phys, size);
}

static void __ref sfi_unmap_memory(void __iomem *virt, u32 size)
{
	if (!virt || !size)
		return;

	if (sfi_use_ioremap)
		iounmap(virt);
	else
		early_iounmap(virt, size);
}

static void sfi_print_table_header(unsigned long long pa,
				struct sfi_table_header *header)
{
	pr_info("%4.4s %llX, %04X (v%d %6.6s %8.8s)\n",
		header->sig, pa,
		header->len, header->rev, header->oem_id,
		header->oem_table_id);
}

/*
 * sfi_verify_table()
 * Sanity check table lengh, calculate checksum
 */
static int sfi_verify_table(struct sfi_table_header *table)
{

	u8 checksum = 0;
	u8 *puchar = (u8 *)table;
	u32 length = table->len;

	/* Sanity check table length against arbitrary 1MB limit */
	if (length > 0x100000) {
		pr_err("Invalid table length 0x%x\n", length);
		return -1;
	}

	while (length--)
		checksum += *puchar++;

	if (checksum) {
		pr_err("Checksum %2.2X should be %2.2X\n",
			table->csum, table->csum - checksum);
		return -1;
	}
	return 0;
}

/*
 * sfi_map_table()
 *
 * Return address of mapped table
 * Check for common case that we can re-use mapping to SYST,
 * which requires syst_pa, syst_va to be initialized.
 */
struct sfi_table_header *sfi_map_table(u64 pa)
{
	struct sfi_table_header *th;
	u32 length;

	if (!TABLE_ON_PAGE(syst_pa, pa, sizeof(struct sfi_table_header)))
		th = sfi_map_memory(pa, sizeof(struct sfi_table_header));
	else
		th = (void *)syst_va + (pa - syst_pa);

	 /* If table fits on same page as its header, we are done */
	if (TABLE_ON_PAGE(th, th, th->len))
		return th;

	/* Entire table does not fit on same page as SYST */
	length = th->len;
	if (!TABLE_ON_PAGE(syst_pa, pa, sizeof(struct sfi_table_header)))
		sfi_unmap_memory(th, sizeof(struct sfi_table_header));

	return sfi_map_memory(pa, length);
}

/*
 * sfi_unmap_table()
 *
 * Undoes effect of sfi_map_table() by unmapping table
 * if it did not completely fit on same page as SYST.
 */
void sfi_unmap_table(struct sfi_table_header *th)
{
	if (!TABLE_ON_PAGE(syst_va, th, th->len))
		sfi_unmap_memory(th, TABLE_ON_PAGE(th, th, th->len) ?
					sizeof(*th) : th->len);
}

static int sfi_table_check_key(struct sfi_table_header *th,
				struct sfi_table_key *key)
{

	if (strncmp(th->sig, key->sig, SFI_SIGNATURE_SIZE)
		|| (key->oem_id && strncmp(th->oem_id,
				key->oem_id, SFI_OEM_ID_SIZE))
		|| (key->oem_table_id && strncmp(th->oem_table_id,
				key->oem_table_id, SFI_OEM_TABLE_ID_SIZE)))
		return -1;

	return 0;
}

/*
 * This function will be used in 2 cases:
 * 1. used to enumerate and verify the tables addressed by SYST/XSDT,
 *    thus no signature will be given (in kernel boot phase)
 * 2. used to parse one specific table, signature must exist, and
 *    the mapped virt address will be returned, and the virt space
 *    will be released by call sfi_put_table() later
 *
 * This two cases are from two different functions with two different
 * sections and causes section mismatch warning. So use __ref to tell
 * modpost not to make any noise.
 *
 * Return value:
 *	NULL:			when can't find a table matching the key
 *	ERR_PTR(error):		error value
 *	virt table address:	when a matched table is found
 */
struct sfi_table_header *
 __ref sfi_check_table(u64 pa, struct sfi_table_key *key)
{
	struct sfi_table_header *th;
	void *ret = NULL;

	th = sfi_map_table(pa);
	if (!th)
		return ERR_PTR(-ENOMEM);

	if (!key->sig) {
		sfi_print_table_header(pa, th);
		if (sfi_verify_table(th))
			ret = ERR_PTR(-EINVAL);
	} else {
		if (!sfi_table_check_key(th, key))
			return th;	/* Success */
	}

	sfi_unmap_table(th);
	return ret;
}

/*
 * sfi_get_table()
 *
 * Search SYST for the specified table with the signature in
 * the key, and return the mapped table
 */
struct sfi_table_header *sfi_get_table(struct sfi_table_key *key)
{
	struct sfi_table_header *th;
	u32 tbl_cnt, i;

	tbl_cnt = SFI_GET_NUM_ENTRIES(syst_va, u64);
	for (i = 0; i < tbl_cnt; i++) {
		th = sfi_check_table(syst_va->pentry[i], key);
		if (!IS_ERR(th) && th)
			return th;
	}

	return NULL;
}

void sfi_put_table(struct sfi_table_header *th)
{
	sfi_unmap_table(th);
}

/* Find table with signature, run handler on it */
int sfi_table_parse(char *signature, char *oem_id, char *oem_table_id,
			sfi_table_handler handler)
{
	struct sfi_table_header *table = NULL;
	struct sfi_table_key key;
	int ret = -EINVAL;

	if (sfi_disabled || !handler || !signature)
		goto exit;

	key.sig = signature;
	key.oem_id = oem_id;
	key.oem_table_id = oem_table_id;

	table = sfi_get_table(&key);
	if (!table)
		goto exit;

	ret = handler(table);
	sfi_put_table(table);
exit:
	return ret;
}
EXPORT_SYMBOL_GPL(sfi_table_parse);

/*
 * sfi_parse_syst()
 * Checksum all the tables in SYST and print their headers
 *
 * success: set syst_va, return 0
 */
static int __init sfi_parse_syst(void)
{
	struct sfi_table_key key = SFI_ANY_KEY;
	int tbl_cnt, i;
	void *ret;

	syst_va = sfi_map_memory(syst_pa, sizeof(struct sfi_table_simple));
	if (!syst_va)
		return -ENOMEM;

	tbl_cnt = SFI_GET_NUM_ENTRIES(syst_va, u64);
	for (i = 0; i < tbl_cnt; i++) {
		ret = sfi_check_table(syst_va->pentry[i], &key);
		if (IS_ERR(ret))
			return PTR_ERR(ret);
	}

	return 0;
}

/*
 * The OS finds the System Table by searching 16-byte boundaries between
 * physical address 0x000E0000 and 0x000FFFFF. The OS shall search this region
 * starting at the low address and shall stop searching when the 1st valid SFI
 * System Table is found.
 *
 * success: set syst_pa, return 0
 * fail: return -1
 */
static __init int sfi_find_syst(void)
{
	unsigned long offset, len;
	void *start;

	len = SFI_SYST_SEARCH_END - SFI_SYST_SEARCH_BEGIN;
	start = sfi_map_memory(SFI_SYST_SEARCH_BEGIN, len);
	if (!start)
		return -1;

	for (offset = 0; offset < len; offset += 16) {
		struct sfi_table_header *syst_hdr;

		syst_hdr = start + offset;
		if (strncmp(syst_hdr->sig, SFI_SIG_SYST,
				SFI_SIGNATURE_SIZE))
			continue;

		if (syst_hdr->len > PAGE_SIZE)
			continue;

		sfi_print_table_header(SFI_SYST_SEARCH_BEGIN + offset,
					syst_hdr);

		if (sfi_verify_table(syst_hdr))
			continue;

		/*
		 * Enforce SFI spec mandate that SYST reside within a page.
		 */
		if (!ON_SAME_PAGE(syst_pa, syst_pa + syst_hdr->len)) {
			pr_info("SYST 0x%llx + 0x%x crosses page\n",
					syst_pa, syst_hdr->len);
			continue;
		}

		/* Success */
		syst_pa = SFI_SYST_SEARCH_BEGIN + offset;
		sfi_unmap_memory(start, len);
		return 0;
	}

	sfi_unmap_memory(start, len);
	return -1;
}

static struct kobject *sfi_kobj;
static struct kobject *tables_kobj;

static ssize_t sfi_table_show(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *bin_attr, char *buf,
			       loff_t offset, size_t count)
{
	struct sfi_table_attr *tbl_attr =
	    container_of(bin_attr, struct sfi_table_attr, attr);
	struct sfi_table_header *th = NULL;
	struct sfi_table_key key;
	ssize_t cnt;

	key.sig = tbl_attr->name;
	key.oem_id = NULL;
	key.oem_table_id = NULL;

	if (strncmp(SFI_SIG_SYST, tbl_attr->name, SFI_SIGNATURE_SIZE)) {
		th = sfi_get_table(&key);
		if (!th)
			return 0;

		cnt =  memory_read_from_buffer(buf, count, &offset,
						th, th->len);
		sfi_put_table(th);
	} else
		cnt =  memory_read_from_buffer(buf, count, &offset,
					syst_va, syst_va->header.len);

	return cnt;
}

struct sfi_table_attr __init *sfi_sysfs_install_table(u64 pa)
{
	struct sfi_table_attr *tbl_attr;
	struct sfi_table_header *th;
	int ret;

	tbl_attr = kzalloc(sizeof(struct sfi_table_attr), GFP_KERNEL);
	if (!tbl_attr)
		return NULL;

	th = sfi_map_table(pa);
	if (!th || !th->sig[0]) {
		kfree(tbl_attr);
		return NULL;
	}

	sysfs_attr_init(&tbl_attr->attr.attr);
	memcpy(tbl_attr->name, th->sig, SFI_SIGNATURE_SIZE);

	tbl_attr->attr.size = 0;
	tbl_attr->attr.read = sfi_table_show;
	tbl_attr->attr.attr.name = tbl_attr->name;
	tbl_attr->attr.attr.mode = 0400;

	ret = sysfs_create_bin_file(tables_kobj,
				  &tbl_attr->attr);
	if (ret) {
		kfree(tbl_attr);
		tbl_attr = NULL;
	}

	sfi_unmap_table(th);
	return tbl_attr;
}

static int __init sfi_sysfs_init(void)
{
	int tbl_cnt, i;

	if (sfi_disabled)
		return 0;

	sfi_kobj = kobject_create_and_add("sfi", firmware_kobj);
	if (!sfi_kobj)
		return 0;

	tables_kobj = kobject_create_and_add("tables", sfi_kobj);
	if (!tables_kobj) {
		kobject_put(sfi_kobj);
		return 0;
	}

	sfi_sysfs_install_table(syst_pa);

	tbl_cnt = SFI_GET_NUM_ENTRIES(syst_va, u64);

	for (i = 0; i < tbl_cnt; i++)
		sfi_sysfs_install_table(syst_va->pentry[i]);

	sfi_acpi_sysfs_init();
	kobject_uevent(sfi_kobj, KOBJ_ADD);
	kobject_uevent(tables_kobj, KOBJ_ADD);
	pr_info("SFI sysfs interfaces init success\n");
	return 0;
}

void __init sfi_init(void)
{
	if (!acpi_disabled)
		disable_sfi();

	if (sfi_disabled)
		return;

	pr_info("Simple Firmware Interface v0.81 http://simplefirmware.org\n");

	if (sfi_find_syst() || sfi_parse_syst() || sfi_platform_init())
		disable_sfi();

	return;
}

void __init sfi_init_late(void)
{
	int length;

	if (sfi_disabled)
		return;

	length = syst_va->header.len;
	sfi_unmap_memory(syst_va, sizeof(struct sfi_table_simple));

	/* Use ioremap now after it is ready */
	sfi_use_ioremap = 1;
	syst_va = sfi_map_memory(syst_pa, length);

	sfi_acpi_init();
}

/*
 * The reason we put it here becasue we need wait till the /sys/firmware
 * is setup, then our interface can be registered in /sys/firmware/sfi
 */
core_initcall(sfi_sysfs_init);
