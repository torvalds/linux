/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2020 Synopsys, Inc. (www.synopsys.com)
 *
 * Author: Eugeniy Paltsev <Eugeniy.Paltsev@synopsys.com>
 */
#ifndef __ASM_ARC_DSP_H
#define __ASM_ARC_DSP_H

#ifndef __ASSEMBLY__

/*
 * DSP-related saved registers - need to be saved only when you are
 * scheduled out.
 * structure fields name must correspond to aux register defenitions for
 * automatic offset calculation in DSP_AUX_SAVE_RESTORE macros
 */
struct dsp_callee_regs {
	unsigned long ACC0_GLO, ACC0_GHI, DSP_BFLY0, DSP_FFT_CTRL;
};

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_ARC_DSP_H */
