/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2015-2016 Synopsys, Inc. (www.synopsys.com)
 */

#ifndef _ASM_ARC_PCI_H
#define _ASM_ARC_PCI_H

#ifdef __KERNEL__
#include <linux/ioport.h>

#define PCIBIOS_MIN_IO 0x100
#define PCIBIOS_MIN_MEM 0x100000

#define pcibios_assign_all_busses()	1

#endif /* __KERNEL__ */

#endif /* _ASM_ARC_PCI_H */
