/* SPDX-License-Identifier: GPL-2.0 */
/*
 *    Hypervisor filesystem for Linux on s390. z/VM implementation.
 *
 *    Copyright IBM Corp. 2006
 *    Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */

#ifndef _S390_HYPFS_VM_H_
#define _S390_HYPFS_VM_H_

#define DIAG2FC_NAME_LEN 8

struct diag2fc_data {
	__u32 version;
	__u32 flags;
	__u64 used_cpu;
	__u64 el_time;
	__u64 mem_min_kb;
	__u64 mem_max_kb;
	__u64 mem_share_kb;
	__u64 mem_used_kb;
	__u32 pcpus;
	__u32 lcpus;
	__u32 vcpus;
	__u32 ocpus;
	__u32 cpu_max;
	__u32 cpu_shares;
	__u32 cpu_use_samp;
	__u32 cpu_delay_samp;
	__u32 page_wait_samp;
	__u32 idle_samp;
	__u32 other_samp;
	__u32 total_samp;
	char  guest_name[DIAG2FC_NAME_LEN];
};

struct diag2fc_parm_list {
	char userid[DIAG2FC_NAME_LEN];
	char aci_grp[DIAG2FC_NAME_LEN];
	__u64 addr;
	__u32 size;
	__u32 fmt;
};

void *diag2fc_store(char *query, unsigned int *count, int offset);
void diag2fc_free(const void *data);
extern char *diag2fc_guest_query;

#endif /* _S390_HYPFS_VM_H_ */
