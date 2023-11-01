/* SPDX-License-Identifier: GPL-2.0-only */

#include <linux/linkage.h>

struct frame;

asmlinkage void buserr_c(struct frame *fp);
asmlinkage void fpemu_signal(int signal, int code, void *addr);
asmlinkage void fpsp040_die(void);
asmlinkage void set_esp0(unsigned long ssp);
