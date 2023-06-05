// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Copyright (C) 2023 SiFive
 * Author: Andy Chiu <andy.chiu@sifive.com>
 */
#include <linux/export.h>

#include <asm/vector.h>
#include <asm/csr.h>
#include <asm/elf.h>
#include <asm/bug.h>

unsigned long riscv_v_vsize __read_mostly;
EXPORT_SYMBOL_GPL(riscv_v_vsize);

int riscv_v_setup_vsize(void)
{
	unsigned long this_vsize;

	/* There are 32 vector registers with vlenb length. */
	riscv_v_enable();
	this_vsize = csr_read(CSR_VLENB) * 32;
	riscv_v_disable();

	if (!riscv_v_vsize) {
		riscv_v_vsize = this_vsize;
		return 0;
	}

	if (riscv_v_vsize != this_vsize) {
		WARN(1, "RISCV_ISA_V only supports one vlenb on SMP systems");
		return -EOPNOTSUPP;
	}

	return 0;
}
