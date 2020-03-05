/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Synopsys, Inc. (www.synopsys.com)
 *
 * Author: Eugeniy Paltsev <Eugeniy.Paltsev@synopsys.com>
 */
#ifndef __ASM_ARC_DSP_IMPL_H
#define __ASM_ARC_DSP_IMPL_H

#define DSP_CTRL_DISABLED_ALL		0

#ifdef __ASSEMBLY__

/* clobbers r5 register */
.macro DSP_EARLY_INIT
	lr	r5, [ARC_AUX_DSP_BUILD]
	bmsk	r5, r5, 7
	breq    r5, 0, 1f
	mov	r5, DSP_CTRL_DISABLED_ALL
	sr	r5, [ARC_AUX_DSP_CTRL]
1:
.endm

/* clobbers r10, r11 registers pair */
.macro DSP_SAVE_REGFILE_IRQ
#if defined(CONFIG_ARC_DSP_KERNEL)
	/*
	 * Drop any changes to DSP_CTRL made by userspace so userspace won't be
	 * able to break kernel - reset it to DSP_CTRL_DISABLED_ALL value
	 */
	mov	r10, DSP_CTRL_DISABLED_ALL
	sr	r10, [ARC_AUX_DSP_CTRL]
#endif /* ARC_DSP_KERNEL */
.endm

#else /* __ASEMBLY__ */

#include <asm/asserts.h>

static inline bool dsp_exist(void)
{
	struct bcr_generic bcr;

	READ_BCR(ARC_AUX_DSP_BUILD, bcr);
	return !!bcr.ver;
}

static inline void dsp_config_check(void)
{
	CHK_OPT_STRICT(CONFIG_ARC_DSP_HANDLED, dsp_exist());
}

#endif /* __ASEMBLY__ */
#endif /* __ASM_ARC_DSP_IMPL_H */
