/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ASM_ALPHA_UNALIGNED_H
#define _ASM_ALPHA_UNALIGNED_H

#include <linux/unaligned/le_struct.h>
#include <linux/unaligned/be_byteshift.h>
#include <linux/unaligned/generic.h>

#define get_unaligned __get_unaligned_le
#define put_unaligned __put_unaligned_le

#endif /* _ASM_ALPHA_UNALIGNED_H */
