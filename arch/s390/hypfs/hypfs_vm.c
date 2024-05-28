// SPDX-License-Identifier: GPL-2.0
/*
 *    Hypervisor filesystem for Linux on s390. z/VM implementation.
 *
 *    Copyright IBM Corp. 2006
 *    Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/vmalloc.h>
#include <asm/extable.h>
#include <asm/diag.h>
#include <asm/ebcdic.h>
#include <asm/timex.h>
#include "hypfs_vm.h"
#include "hypfs.h"

#define DBFS_D2FC_HDR_VERSION 0

static char local_guest[] = "        ";
static char all_guests[] = "*       ";
static char *all_groups = all_guests;
char *diag2fc_guest_query;

static int diag2fc(int size, char* query, void *addr)
{
	unsigned long residual_cnt;
	unsigned long rc;
	struct diag2fc_parm_list parm_list;

	memcpy(parm_list.userid, query, DIAG2FC_NAME_LEN);
	ASCEBC(parm_list.userid, DIAG2FC_NAME_LEN);
	memcpy(parm_list.aci_grp, all_groups, DIAG2FC_NAME_LEN);
	ASCEBC(parm_list.aci_grp, DIAG2FC_NAME_LEN);
	parm_list.addr = (unsigned long)addr;
	parm_list.size = size;
	parm_list.fmt = 0x02;
	rc = -1;

	diag_stat_inc(DIAG_STAT_X2FC);
	asm volatile(
		"	diag    %0,%1,0x2fc\n"
		"0:	nopr	%%r7\n"
		EX_TABLE(0b,0b)
		: "=d" (residual_cnt), "+d" (rc) : "0" (&parm_list) : "memory");

	if ((rc != 0 ) && (rc != -2))
		return rc;
	else
		return -residual_cnt;
}

/*
 * Allocate buffer for "query" and store diag 2fc at "offset"
 */
void *diag2fc_store(char *query, unsigned int *count, int offset)
{
	void *data;
	int size;

	do {
		size = diag2fc(0, query, NULL);
		if (size < 0)
			return ERR_PTR(-EACCES);
		data = vmalloc(size + offset);
		if (!data)
			return ERR_PTR(-ENOMEM);
		if (diag2fc(size, query, data + offset) == 0)
			break;
		vfree(data);
	} while (1);
	*count = (size / sizeof(struct diag2fc_data));

	return data;
}

void diag2fc_free(const void *data)
{
	vfree(data);
}

struct dbfs_d2fc_hdr {
	u64	len;		/* Length of d2fc buffer without header */
	u16	version;	/* Version of header */
	union tod_clock tod_ext; /* TOD clock for d2fc */
	u64	count;		/* Number of VM guests in d2fc buffer */
	char	reserved[30];
} __attribute__ ((packed));

struct dbfs_d2fc {
	struct dbfs_d2fc_hdr	hdr;	/* 64 byte header */
	char			buf[];	/* d2fc buffer */
} __attribute__ ((packed));

static int dbfs_diag2fc_create(void **data, void **data_free_ptr, size_t *size)
{
	struct dbfs_d2fc *d2fc;
	unsigned int count;

	d2fc = diag2fc_store(diag2fc_guest_query, &count, sizeof(d2fc->hdr));
	if (IS_ERR(d2fc))
		return PTR_ERR(d2fc);
	store_tod_clock_ext(&d2fc->hdr.tod_ext);
	d2fc->hdr.len = count * sizeof(struct diag2fc_data);
	d2fc->hdr.version = DBFS_D2FC_HDR_VERSION;
	d2fc->hdr.count = count;
	memset(&d2fc->hdr.reserved, 0, sizeof(d2fc->hdr.reserved));
	*data = d2fc;
	*data_free_ptr = d2fc;
	*size = d2fc->hdr.len + sizeof(struct dbfs_d2fc_hdr);
	return 0;
}

static struct hypfs_dbfs_file dbfs_file_2fc = {
	.name		= "diag_2fc",
	.data_create	= dbfs_diag2fc_create,
	.data_free	= diag2fc_free,
};

int hypfs_vm_init(void)
{
	if (!MACHINE_IS_VM)
		return 0;
	if (diag2fc(0, all_guests, NULL) > 0)
		diag2fc_guest_query = all_guests;
	else if (diag2fc(0, local_guest, NULL) > 0)
		diag2fc_guest_query = local_guest;
	else
		return -EACCES;
	hypfs_dbfs_create_file(&dbfs_file_2fc);
	return 0;
}

void hypfs_vm_exit(void)
{
	if (!MACHINE_IS_VM)
		return;
	hypfs_dbfs_remove_file(&dbfs_file_2fc);
}
