/* SPDX-License-Identifier: GPL-2.0 */
/*
 * dsp_hwec.h
 */

extern struct mISDN_dsp_element *dsp_hwec;
extern void dsp_hwec_enable(struct dsp *dsp, const char *arg);
extern void dsp_hwec_disable(struct dsp *dsp);
extern int  dsp_hwec_init(void);
extern void dsp_hwec_exit(void);
