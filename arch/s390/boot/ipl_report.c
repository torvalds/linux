// SPDX-License-Identifier: GPL-2.0
#include <linux/init.h>
#include <linux/ctype.h>
#include <asm/ebcdic.h>
#include <asm/sclp.h>
#include <asm/sections.h>
#include <asm/boot_data.h>
#include <uapi/asm/ipl.h>
#include "boot.h"

int __bootdata_preserved(ipl_secure_flag);

unsigned long __bootdata_preserved(ipl_cert_list_addr);
unsigned long __bootdata_preserved(ipl_cert_list_size);

unsigned long __bootdata(early_ipl_comp_list_addr);
unsigned long __bootdata(early_ipl_comp_list_size);

#define for_each_rb_entry(entry, rb) \
	for (entry = rb->entries; \
	     (void *) entry + sizeof(*entry) <= (void *) rb + rb->len; \
	     entry++)

static inline bool intersects(unsigned long addr0, unsigned long size0,
			      unsigned long addr1, unsigned long size1)
{
	return addr0 + size0 > addr1 && addr1 + size1 > addr0;
}

static unsigned long find_bootdata_space(struct ipl_rb_components *comps,
					 struct ipl_rb_certificates *certs,
					 unsigned long safe_addr)
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
	size = ipl_cert_list_size + early_ipl_comp_list_size;

	/*
	 * Start from safe_addr to find a free memory area large
	 * enough for the IPL report boot data. This area is used
	 * for ipl_cert_list_addr/ipl_cert_list_size and
	 * early_ipl_comp_list_addr/early_ipl_comp_list_size. It must
	 * not overlap with any component or any certificate.
	 */
repeat:
	if (IS_ENABLED(CONFIG_BLK_DEV_INITRD) && INITRD_START && INITRD_SIZE &&
	    intersects(INITRD_START, INITRD_SIZE, safe_addr, size))
		safe_addr = INITRD_START + INITRD_SIZE;
	if (intersects(safe_addr, size, (unsigned long)comps, comps->len)) {
		safe_addr = (unsigned long)comps + comps->len;
		goto repeat;
	}
	for_each_rb_entry(comp, comps)
		if (intersects(safe_addr, size, comp->addr, comp->len)) {
			safe_addr = comp->addr + comp->len;
			goto repeat;
		}
	if (intersects(safe_addr, size, (unsigned long)certs, certs->len)) {
		safe_addr = (unsigned long)certs + certs->len;
		goto repeat;
	}
	for_each_rb_entry(cert, certs)
		if (intersects(safe_addr, size, cert->addr, cert->len)) {
			safe_addr = cert->addr + cert->len;
			goto repeat;
		}
	early_ipl_comp_list_addr = safe_addr;
	ipl_cert_list_addr = safe_addr + early_ipl_comp_list_size;

	return safe_addr + size;
}

static void copy_components_bootdata(struct ipl_rb_components *comps)
{
	struct ipl_rb_component_entry *comp, *ptr;

	ptr = (struct ipl_rb_component_entry *) early_ipl_comp_list_addr;
	for_each_rb_entry(comp, comps)
		memcpy(ptr++, comp, sizeof(*ptr));
}

static void copy_certificates_bootdata(struct ipl_rb_certificates *certs)
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

unsigned long read_ipl_report(unsigned long safe_addr)
{
	struct ipl_rb_certificates *certs;
	struct ipl_rb_components *comps;
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
		return safe_addr;
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
	if (!comps || !certs)
		return safe_addr;

	/*
	 * Copy component and certificate list to a safe area
	 * where the decompressed kernel can find them.
	 */
	safe_addr = find_bootdata_space(comps, certs, safe_addr);
	copy_components_bootdata(comps);
	copy_certificates_bootdata(certs);

	return safe_addr;
}
