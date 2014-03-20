/*
 *	Intel CPU Microcode Update Driver for Linux
 *
 *	Copyright (C) 2012 Fenghua Yu <fenghua.yu@intel.com>
 *			   H Peter Anvin" <hpa@zytor.com>
 *
 *	This driver allows to upgrade microcode on Intel processors
 *	belonging to IA-32 family - PentiumPro, Pentium II,
 *	Pentium III, Xeon, Pentium 4, etc.
 *
 *	Reference: Section 8.11 of Volume 3a, IA-32 Intel? Architecture
 *	Software Developer's Manual
 *	Order Number 253668 or free download from:
 *
 *	http://developer.intel.com/Assets/PDF/manual/253668.pdf
 *
 *	For more information, go to http://www.urbanmyth.org/microcode
 *
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License
 *	as published by the Free Software Foundation; either version
 *	2 of the License, or (at your option) any later version.
 *
 */
#include <linux/firmware.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include <asm/microcode_intel.h>
#include <asm/processor.h>
#include <asm/msr.h>

static inline int
update_match_cpu(unsigned int csig, unsigned int cpf,
		 unsigned int sig, unsigned int pf)
{
	return (!sigmatch(sig, csig, pf, cpf)) ? 0 : 1;
}

int
update_match_revision(struct microcode_header_intel *mc_header, int rev)
{
	return (mc_header->rev <= rev) ? 0 : 1;
}

int microcode_sanity_check(void *mc, int print_err)
{
	unsigned long total_size, data_size, ext_table_size;
	struct microcode_header_intel *mc_header = mc;
	struct extended_sigtable *ext_header = NULL;
	int sum, orig_sum, ext_sigcount = 0, i;
	struct extended_signature *ext_sig;

	total_size = get_totalsize(mc_header);
	data_size = get_datasize(mc_header);

	if (data_size + MC_HEADER_SIZE > total_size) {
		if (print_err)
			pr_err("error! Bad data size in microcode data file\n");
		return -EINVAL;
	}

	if (mc_header->ldrver != 1 || mc_header->hdrver != 1) {
		if (print_err)
			pr_err("error! Unknown microcode update format\n");
		return -EINVAL;
	}
	ext_table_size = total_size - (MC_HEADER_SIZE + data_size);
	if (ext_table_size) {
		if ((ext_table_size < EXT_HEADER_SIZE)
		 || ((ext_table_size - EXT_HEADER_SIZE) % EXT_SIGNATURE_SIZE)) {
			if (print_err)
				pr_err("error! Small exttable size in microcode data file\n");
			return -EINVAL;
		}
		ext_header = mc + MC_HEADER_SIZE + data_size;
		if (ext_table_size != exttable_size(ext_header)) {
			if (print_err)
				pr_err("error! Bad exttable size in microcode data file\n");
			return -EFAULT;
		}
		ext_sigcount = ext_header->count;
	}

	/* check extended table checksum */
	if (ext_table_size) {
		int ext_table_sum = 0;
		int *ext_tablep = (int *)ext_header;

		i = ext_table_size / DWSIZE;
		while (i--)
			ext_table_sum += ext_tablep[i];
		if (ext_table_sum) {
			if (print_err)
				pr_warn("aborting, bad extended signature table checksum\n");
			return -EINVAL;
		}
	}

	/* calculate the checksum */
	orig_sum = 0;
	i = (MC_HEADER_SIZE + data_size) / DWSIZE;
	while (i--)
		orig_sum += ((int *)mc)[i];
	if (orig_sum) {
		if (print_err)
			pr_err("aborting, bad checksum\n");
		return -EINVAL;
	}
	if (!ext_table_size)
		return 0;
	/* check extended signature checksum */
	for (i = 0; i < ext_sigcount; i++) {
		ext_sig = (void *)ext_header + EXT_HEADER_SIZE +
			  EXT_SIGNATURE_SIZE * i;
		sum = orig_sum
			- (mc_header->sig + mc_header->pf + mc_header->cksum)
			+ (ext_sig->sig + ext_sig->pf + ext_sig->cksum);
		if (sum) {
			if (print_err)
				pr_err("aborting, bad checksum\n");
			return -EINVAL;
		}
	}
	return 0;
}
EXPORT_SYMBOL_GPL(microcode_sanity_check);

/*
 * return 0 - no update found
 * return 1 - found update
 */
int get_matching_sig(unsigned int csig, int cpf, void *mc, int rev)
{
	struct microcode_header_intel *mc_header = mc;
	struct extended_sigtable *ext_header;
	unsigned long total_size = get_totalsize(mc_header);
	int ext_sigcount, i;
	struct extended_signature *ext_sig;

	if (update_match_cpu(csig, cpf, mc_header->sig, mc_header->pf))
		return 1;

	/* Look for ext. headers: */
	if (total_size <= get_datasize(mc_header) + MC_HEADER_SIZE)
		return 0;

	ext_header = mc + get_datasize(mc_header) + MC_HEADER_SIZE;
	ext_sigcount = ext_header->count;
	ext_sig = (void *)ext_header + EXT_HEADER_SIZE;

	for (i = 0; i < ext_sigcount; i++) {
		if (update_match_cpu(csig, cpf, ext_sig->sig, ext_sig->pf))
			return 1;
		ext_sig++;
	}
	return 0;
}

/*
 * return 0 - no update found
 * return 1 - found update
 */
int get_matching_microcode(unsigned int csig, int cpf, void *mc, int rev)
{
	struct microcode_header_intel *mc_header = mc;

	if (!update_match_revision(mc_header, rev))
		return 0;

	return get_matching_sig(csig, cpf, mc, rev);
}
EXPORT_SYMBOL_GPL(get_matching_microcode);
