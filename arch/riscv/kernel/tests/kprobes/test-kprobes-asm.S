/* SPDX-License-Identifier: GPL-2.0+ */

#include <linux/linkage.h>
#include <asm/asm.h>
#include "test-kprobes.h"

SYM_FUNC_START(test_kprobes_add)
	li a1, KPROBE_TEST_MAGIC_UPPER
	li a2, KPROBE_TEST_MAGIC_LOWER
test_kprobes_add_addr1:
	add a1, a1, a2
test_kprobes_add_addr2:
	add a0, a1, x0
	ret
SYM_FUNC_END(test_kprobes_add)

SYM_FUNC_START(test_kprobes_jal)
	li a0, 0
	mv a1, ra
	.option push
	.option norvc
test_kprobes_jal_addr1:
	jal x0, 2f
	ret
	.option pop
1:	li a0, KPROBE_TEST_MAGIC_UPPER
	ret
	.option push
	.option norvc
test_kprobes_jal_addr2:
2:	jal 1b
	.option pop
	li a2, KPROBE_TEST_MAGIC_LOWER
	add a0, a0, a2
	jr a1
SYM_FUNC_END(test_kprobes_jal)

SYM_FUNC_START(test_kprobes_jalr)
	la a0, 1f
	mv a1, ra
	.option push
	.option norvc
test_kprobes_jalr_addr:
	jalr a0
	.option pop
	li t0, KPROBE_TEST_MAGIC_UPPER
	add a0, a0, t0
	jr a1
1:	li a0, KPROBE_TEST_MAGIC_LOWER
	ret
SYM_FUNC_END(test_kprobes_jalr)

SYM_FUNC_START(test_kprobes_auipc)
test_kprobes_auipc_addr:
	auipc a0, KPROBE_TEST_MAGIC_LOWER
	la a1, test_kprobes_auipc_addr
	sub a0, a0, a1
	srli a0, a0, 12
	li a1, KPROBE_TEST_MAGIC_UPPER
	add a0, a0, a1
	ret
SYM_FUNC_END(test_kprobes_auipc)

SYM_FUNC_START(test_kprobes_branch)
	.option push
	.option norvc
	li a0, 0
	li a1, 1
	li a2, 2
test_kprobes_branch_addr1:
	beqz a0, 1f
	ret
1:
test_kprobes_branch_addr2:
	beqz a1, 3f
test_kprobes_branch_addr3:
	bnez a0, 3f
test_kprobes_branch_addr4:
	bnez a2, 1f
	ret
1:
test_kprobes_branch_addr5:
	bge a1, a2, 3f
test_kprobes_branch_addr6:
	bge a2, a1, 2f
	ret
1:
	li t0, KPROBE_TEST_MAGIC_UPPER
	add a0, a0, t0
	ret
2:
test_kprobes_branch_addr7:
	blt a2, a1, 3f
	li a0, KPROBE_TEST_MAGIC_LOWER
test_kprobes_branch_addr8:
	blt a1, a2, 1b
3:
	li a0, 0
	ret
	.option pop
SYM_FUNC_END(test_kprobes_branch)

#ifdef CONFIG_RISCV_ISA_C

SYM_FUNC_START(test_kprobes_c_j)
	li a0, 0
test_kprobes_branch_c_j_addr1:
	c.j 2f
1:
	li a1, KPROBE_TEST_MAGIC_UPPER
	add a0, a0, a1
	ret
2:	li a0, KPROBE_TEST_MAGIC_LOWER
test_kprobes_branch_c_j_addr2:
	c.j 1b
SYM_FUNC_END(test_kprobes_c_j)

SYM_FUNC_START(test_kprobes_c_jr)
	la a0, 2f
test_kprobes_c_jr_addr1:
	c.jr a0
	ret
1:	li a1, KPROBE_TEST_MAGIC_LOWER
	add a0, a0, a1
	ret
2:
	li a0, KPROBE_TEST_MAGIC_UPPER
	la a1, 1b
test_kprobes_c_jr_addr2:
	c.jr a1
SYM_FUNC_END(test_kprobes_c_jr)

SYM_FUNC_START(test_kprobes_c_jalr)
	mv a1, ra
	la a0, 1f
test_kprobes_c_jalr_addr:
	c.jalr a0
	li a2, KPROBE_TEST_MAGIC_UPPER
	add a0, a0, a2
	jr a1
1:	li a0, KPROBE_TEST_MAGIC_LOWER
	ret
SYM_FUNC_END(test_kprobes_c_jalr)

SYM_FUNC_START(test_kprobes_c_beqz)
	li a0, 0
	li a1, 1
test_kprobes_c_beqz_addr1:
	c.beqz a0, 2f
	ret
1:	li a1, KPROBE_TEST_MAGIC_UPPER
	add a0, a0, a1
	ret
test_kprobes_c_beqz_addr2:
2:	c.beqz a1, 3f
	li a0, KPROBE_TEST_MAGIC_LOWER
	mv a1, x0
test_kprobes_c_beqz_addr3:
	c.beqz a1, 1b
3:	li a0, 0
	ret
SYM_FUNC_END(test_kprobes_c_beqz)

SYM_FUNC_START(test_kprobes_c_bnez)
	li a0, 0
	li a1, 1
test_kprobes_c_bnez_addr1:
	c.bnez a1, 2f
	ret
1:	li a1, KPROBE_TEST_MAGIC_UPPER
	add a0, a0, a1
	ret
test_kprobes_c_bnez_addr2:
2:	c.bnez a0, 3f
	li a0, KPROBE_TEST_MAGIC_LOWER
test_kprobes_c_bnez_addr3:
	c.bnez a0, 1b
3:	li a0, 0
	ret
SYM_FUNC_END(test_kprobes_c_bnez)

#endif /* CONFIG_RISCV_ISA_C */

SYM_DATA_START(test_kprobes_addresses)
	RISCV_PTR test_kprobes_add_addr1
	RISCV_PTR test_kprobes_add_addr2
	RISCV_PTR test_kprobes_jal_addr1
	RISCV_PTR test_kprobes_jal_addr2
	RISCV_PTR test_kprobes_jalr_addr
	RISCV_PTR test_kprobes_auipc_addr
	RISCV_PTR test_kprobes_branch_addr1
	RISCV_PTR test_kprobes_branch_addr2
	RISCV_PTR test_kprobes_branch_addr3
	RISCV_PTR test_kprobes_branch_addr4
	RISCV_PTR test_kprobes_branch_addr5
	RISCV_PTR test_kprobes_branch_addr6
	RISCV_PTR test_kprobes_branch_addr7
	RISCV_PTR test_kprobes_branch_addr8
#ifdef CONFIG_RISCV_ISA_C
	RISCV_PTR test_kprobes_branch_c_j_addr1
	RISCV_PTR test_kprobes_branch_c_j_addr2
	RISCV_PTR test_kprobes_c_jr_addr1
	RISCV_PTR test_kprobes_c_jr_addr2
	RISCV_PTR test_kprobes_c_jalr_addr
	RISCV_PTR test_kprobes_c_beqz_addr1
	RISCV_PTR test_kprobes_c_beqz_addr2
	RISCV_PTR test_kprobes_c_beqz_addr3
	RISCV_PTR test_kprobes_c_bnez_addr1
	RISCV_PTR test_kprobes_c_bnez_addr2
	RISCV_PTR test_kprobes_c_bnez_addr3
#endif /* CONFIG_RISCV_ISA_C */
	RISCV_PTR 0
SYM_DATA_END(test_kprobes_addresses)

SYM_DATA_START(test_kprobes_functions)
	RISCV_PTR test_kprobes_add
	RISCV_PTR test_kprobes_jal
	RISCV_PTR test_kprobes_jalr
	RISCV_PTR test_kprobes_auipc
	RISCV_PTR test_kprobes_branch
#ifdef CONFIG_RISCV_ISA_C
	RISCV_PTR test_kprobes_c_j
	RISCV_PTR test_kprobes_c_jr
	RISCV_PTR test_kprobes_c_jalr
	RISCV_PTR test_kprobes_c_beqz
	RISCV_PTR test_kprobes_c_bnez
#endif /* CONFIG_RISCV_ISA_C */
	RISCV_PTR 0
SYM_DATA_END(test_kprobes_functions)
