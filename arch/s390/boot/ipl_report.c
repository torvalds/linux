// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/ctype.h>
#include <asm/ebcdic.h>
#include <asm/sclp.h>
#include <asm/sections.h>
#include <asm/boot_data.h>
#include <asm/physmem_info.h>
#include <uapi/asm/ipl.h>
#include "boot.h"

int __bootdata_preserved(ipl_secure_flag);

unsigned long __bootdata_preserved(ipl_cert_list_addr);
unsigned long __bootdata_preserved(ipl_cert_list_size);

unsigned long __bootdata(early_ipl_comp_list_addr);
unsigned long __bootdata(early_ipl_comp_list_size);

static struct ipl_rb_certificates *certs;
static struct ipl_rb_components *comps;
static bool ipl_report_needs_saving;

#define for_each_rb_entry(entry, rb) \
	for (entry = rb->entries; \
	     (void *) entry + sizeof(*entry) <= (void *) rb + rb->len; \
	     entry++)

static unsigned long get_cert_comp_list_size(void)
{
	struct ipl_rb_certificate_entry *cert;
	struct ipl_rb_component_entry *comp;
	size_t size;

	/*
	 * Find the length for the IPL report boot data
	 */
	early_ipl_comp_list_size = 0;
	for_each_rb_entry(comp, comps)
		early_ipl_comp_list_size += sizeof(*comp);
	ipl_cert_list_size = 0;
	for_each_rb_entry(cert, certs)
		ipl_cert_list_size += sizeof(unsigned int) + cert->len;
	return ipl_cert_list_size + early_ipl_comp_list_size;
}

bool ipl_report_certs_intersects(unsigned long addr, unsigned long size,
				 unsigned long *intersection_start)
{
	struct ipl_rb_certificate_entry *cert;

	if (!ipl_report_needs_saving)
		return false;

	for_each_rb_entry(cert, certs) {
		if (intersects(addr, size, cert->addr, cert->len)) {
			*intersection_start = cert->addr;
			return true;
		}
	}
	return false;
}

static void copy_components_bootdata(void)
{
	struct ipl_rb_component_entry *comp, *ptr;

	ptr = (struct ipl_rb_component_entry *) early_ipl_comp_list_addr;
	for_each_rb_entry(comp, comps)
		memcpy(ptr++, comp, sizeof(*ptr));
}

static void copy_certificates_bootdata(void)
{
	struct ipl_rb_certificate_entry *cert;
	void *ptr;

	ptr = (void *) ipl_cert_list_addr;
	for_each_rb_entry(cert, certs) {
		*(unsigned int *) ptr = cert->len;
		ptr += sizeof(unsigned int);
		memcpy(ptr, (void *) cert->addr, cert->len);
		ptr += cert->len;
	}
}

int read_ipl_report(void)
{
	struct ipl_pl_hdr *pl_hdr;
	struct ipl_rl_hdr *rl_hdr;
	struct ipl_rb_hdr *rb_hdr;
	unsigned long tmp;
	void *rl_end;

	/*
	 * Check if there is a IPL report by looking at the copy
	 * of the IPL parameter information block.
	 */
	if (!ipl_block_valid ||
	    !(ipl_block.hdr.flags & IPL_PL_FLAG_IPLSR))
		return -1;
	ipl_secure_flag = !!(ipl_block.hdr.flags & IPL_PL_FLAG_SIPL);
	/*
	 * There is an IPL report, to find it load the pointer to the
	 * IPL parameter information block from lowcore and skip past
	 * the IPL parameter list, then align the address to a double
	 * word boundary.
	 */
	tmp = (unsigned long) S390_lowcore.ipl_parmblock_ptr;
	pl_hdr = (struct ipl_pl_hdr *) tmp;
	tmp = (tmp + pl_hdr->len + 7) & -8UL;
	rl_hdr = (struct ipl_rl_hdr *) tmp;
	/* Walk through the IPL report blocks in the IPL Report list */
	certs = NULL;
	comps = NULL;
	rl_end = (void *) rl_hdr + rl_hdr->len;
	rb_hdr = (void *) rl_hdr + sizeof(*rl_hdr);
	while ((void *) rb_hdr + sizeof(*rb_hdr) < rl_end &&
	       (void *) rb_hdr + rb_hdr->len <= rl_end) {

		switch (rb_hdr->rbt) {
		case IPL_RBT_CERTIFICATES:
			certs = (struct ipl_rb_certificates *) rb_hdr;
			break;
		case IPL_RBT_COMPONENTS:
			comps = (struct ipl_rb_components *) rb_hdr;
			break;
		default:
			break;
		}

		rb_hdr = (void *) rb_hdr + rb_hdr->len;
	}

	/*
	 * With either the component list or the certificate list
	 * missing the kernel will stay ignorant of secure IPL.
	 */
	if (!comps || !certs) {
		certs = NULL;
		return -1;
	}

	ipl_report_needs_saving = true;
	physmem_reserve(RR_IPLREPORT, (unsigned long)pl_hdr,
			(unsigned long)rl_end - (unsigned long)pl_hdr);
	return 0;
}

void save_ipl_cert_comp_list(void)
{
	unsigned long size;

	if (!ipl_report_needs_saving)
		return;

	size = get_cert_comp_list_size();
	early_ipl_comp_list_addr = physmem_alloc_top_down(RR_CERT_COMP_LIST, size, sizeof(int));
	ipl_cert_list_addr = early_ipl_comp_list_addr + early_ipl_comp_list_size;

	copy_components_bootdata();
	copy_certificates_bootdata();
	physmem_free(RR_IPLREPORT);
	ipl_report_needs_saving = false;
}
