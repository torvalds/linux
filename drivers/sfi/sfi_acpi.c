/* sfi_acpi.c Simple Firmware Interface - ACPI extensions */

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

#include <linux/kernel.h>
#include <acpi/acpi.h>

#include <linux/sfi.h>
#include "sfi_core.h"

/*
 * SFI can access ACPI-defined tables via an optional ACPI XSDT.
 *
 * This allows re-use, and avoids re-definition, of standard tables.
 * For example, the "MCFG" table is defined by PCI, reserved by ACPI,
 * and is expected to be present many SFI-only systems.
 */

static struct acpi_table_xsdt *xsdt_va __read_mostly;

#define XSDT_GET_NUM_ENTRIES(ptable, entry_type) \
	((ptable->header.length - sizeof(struct acpi_table_header)) / \
	(sizeof(entry_type)))

static inline struct sfi_table_header *acpi_to_sfi_th(
				struct acpi_table_header *th)
{
	return (struct sfi_table_header *)th;
}

static inline struct acpi_table_header *sfi_to_acpi_th(
				struct sfi_table_header *th)
{
	return (struct acpi_table_header *)th;
}

/*
 * sfi_acpi_parse_xsdt()
 *
 * Parse the ACPI XSDT for later access by sfi_acpi_table_parse().
 */
static int __init sfi_acpi_parse_xsdt(struct sfi_table_header *th)
{
	struct sfi_table_key key = SFI_ANY_KEY;
	int tbl_cnt, i;
	void *ret;

	xsdt_va = (struct acpi_table_xsdt *)th;
	tbl_cnt = XSDT_GET_NUM_ENTRIES(xsdt_va, u64);
	for (i = 0; i < tbl_cnt; i++) {
		ret = sfi_check_table(xsdt_va->table_offset_entry[i], &key);
		if (IS_ERR(ret)) {
			disable_sfi();
			return -1;
		}
	}

	return 0;
}

int __init sfi_acpi_init(void)
{
	struct sfi_table_key xsdt_key = { .sig = SFI_SIG_XSDT };

	sfi_table_parse(SFI_SIG_XSDT, NULL, NULL, sfi_acpi_parse_xsdt);

	/* Only call the get_table to keep the table mapped */
	xsdt_va = (struct acpi_table_xsdt *)sfi_get_table(&xsdt_key);
	return 0;
}

static struct acpi_table_header *sfi_acpi_get_table(struct sfi_table_key *key)
{
	u32 tbl_cnt, i;
	void *ret;

	tbl_cnt = XSDT_GET_NUM_ENTRIES(xsdt_va, u64);
	for (i = 0; i < tbl_cnt; i++) {
		ret = sfi_check_table(xsdt_va->table_offset_entry[i], key);
		if (!IS_ERR(ret) && ret)
			return sfi_to_acpi_th(ret);
	}

	return NULL;
}

static void sfi_acpi_put_table(struct acpi_table_header *table)
{
	sfi_put_table(acpi_to_sfi_th(table));
}

/*
 * sfi_acpi_table_parse()
 *
 * Find specified table in XSDT, run handler on it and return its return value
 */
int sfi_acpi_table_parse(char *signature, char *oem_id, char *oem_table_id,
			int(*handler)(struct acpi_table_header *))
{
	struct acpi_table_header *table = NULL;
	struct sfi_table_key key;
	int ret = 0;

	if (sfi_disabled)
		return -1;

	key.sig = signature;
	key.oem_id = oem_id;
	key.oem_table_id = oem_table_id;

	table = sfi_acpi_get_table(&key);
	if (!table)
		return -EINVAL;

	ret = handler(table);
	sfi_acpi_put_table(table);
	return ret;
}

static ssize_t sfi_acpi_table_show(struct file *filp, struct kobject *kobj,
			       struct bin_attribute *bin_attr, char *buf,
			       loff_t offset, size_t count)
{
	struct sfi_table_attr *tbl_attr =
	    container_of(bin_attr, struct sfi_table_attr, attr);
	struct acpi_table_header *th = NULL;
	struct sfi_table_key key;
	ssize_t cnt;

	key.sig = tbl_attr->name;
	key.oem_id = NULL;
	key.oem_table_id = NULL;

	th = sfi_acpi_get_table(&key);
	if (!th)
		return 0;

	cnt =  memory_read_from_buffer(buf, count, &offset,
					th, th->length);
	sfi_acpi_put_table(th);

	return cnt;
}


void __init sfi_acpi_sysfs_init(void)
{
	u32 tbl_cnt, i;
	struct sfi_table_attr *tbl_attr;

	tbl_cnt = XSDT_GET_NUM_ENTRIES(xsdt_va, u64);
	for (i = 0; i < tbl_cnt; i++) {
		tbl_attr =
			sfi_sysfs_install_table(xsdt_va->table_offset_entry[i]);
		tbl_attr->attr.read = sfi_acpi_table_show;
	}

	return;
}
