// SPDX-License-Identifier: GPL-2.0-only

extern char __data_loc[];
extern char _edata_loc[];
extern char _sdata[];

int __init __inflate_kernel_data(void);
