/*
 * Debug Store support - selftest
 *
 *
 * Copyright (C) 2009 Intel Corporation.
 * Markus Metzger <markus.t.metzger@intel.com>, 2009
 */

#ifdef CONFIG_X86_DS_SELFTEST
extern int ds_selftest_bts(void);
extern int ds_selftest_pebs(void);
#else
static inline int ds_selftest_bts(void) { return 0; }
static inline int ds_selftest_pebs(void) { return 0; }
#endif
