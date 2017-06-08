/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2012  MIPS Technologies, Inc.  All rights reserved.
 * Authors: Yann Le Du <ledu@kymasys.com>
 */

#include <linux/export.h>
#include <linux/kvm_host.h>

struct kvm_mips_callbacks *kvm_mips_callbacks;
EXPORT_SYMBOL_GPL(kvm_mips_callbacks);
