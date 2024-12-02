// SPDX-License-Identifier: GPL-2.0-only
/*
 * VFIO ZPCI devices support
 *
 * Copyright (C) IBM Corp. 2022.  All rights reserved.
 *	Author(s): Pierre Morel <pmorel@linux.ibm.com>
 */
#include <linux/kvm_host.h>

struct zpci_kvm_hook zpci_kvm_hook;
EXPORT_SYMBOL_GPL(zpci_kvm_hook);
