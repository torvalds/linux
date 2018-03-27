// SPDX-License-Identifier: GPL-2.0+
//
// Security related flags and so on.
//
// Copyright 2018, Michael Ellerman, IBM Corporation.

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/seq_buf.h>

#include <asm/security_features.h>


unsigned long powerpc_security_features __read_mostly = \
	SEC_FTR_L1D_FLUSH_HV | \
	SEC_FTR_L1D_FLUSH_PR | \
	SEC_FTR_BNDS_CHK_SPEC_BAR | \
	SEC_FTR_FAVOUR_SECURITY;


ssize_t cpu_show_meltdown(struct device *dev, struct device_attribute *attr, char *buf)
{
	bool thread_priv;

	thread_priv = security_ftr_enabled(SEC_FTR_L1D_THREAD_PRIV);

	if (rfi_flush || thread_priv) {
		struct seq_buf s;
		seq_buf_init(&s, buf, PAGE_SIZE - 1);

		seq_buf_printf(&s, "Mitigation: ");

		if (rfi_flush)
			seq_buf_printf(&s, "RFI Flush");

		if (rfi_flush && thread_priv)
			seq_buf_printf(&s, ", ");

		if (thread_priv)
			seq_buf_printf(&s, "L1D private per thread");

		seq_buf_printf(&s, "\n");

		return s.len;
	}

	if (!security_ftr_enabled(SEC_FTR_L1D_FLUSH_HV) &&
	    !security_ftr_enabled(SEC_FTR_L1D_FLUSH_PR))
		return sprintf(buf, "Not affected\n");

	return sprintf(buf, "Vulnerable\n");
}

ssize_t cpu_show_spectre_v1(struct device *dev, struct device_attribute *attr, char *buf)
{
	if (!security_ftr_enabled(SEC_FTR_BNDS_CHK_SPEC_BAR))
		return sprintf(buf, "Not affected\n");

	return sprintf(buf, "Vulnerable\n");
}
