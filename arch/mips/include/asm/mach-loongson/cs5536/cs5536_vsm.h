/*
 * the read/write interfaces for Virtual Support Module(VSM)
 *
 * Copyright (C) 2009 Lemote, Inc.
 * Author: Wu Zhangjin <wuzhangjin@gmail.com>
 */

#ifndef	_CS5536_VSM_H
#define	_CS5536_VSM_H

#include <linux/types.h>

typedef void (*cs5536_pci_vsm_write)(int reg, u32 value);
typedef u32 (*cs5536_pci_vsm_read)(int reg);

#define DECLARE_CS5536_MODULE(name) \
extern void pci_##name##_write_reg(int reg, u32 value); \
extern u32 pci_##name##_read_reg(int reg);

/* ide module */
DECLARE_CS5536_MODULE(ide)
/* acc module */
DECLARE_CS5536_MODULE(acc)
/* ohci module */
DECLARE_CS5536_MODULE(ohci)
/* isa module */
DECLARE_CS5536_MODULE(isa)
/* ehci module */
DECLARE_CS5536_MODULE(ehci)

#endif				/* _CS5536_VSM_H */
