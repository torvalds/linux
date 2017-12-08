/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/stddef.h>
#include <linux/sched.h>
#include <linux/elf.h>
#include <linux/crypto.h>
#include <linux/kbuild.h>
#include <asm/mman.h>

void foo(void)
{
#include <common-offsets.h>
}
