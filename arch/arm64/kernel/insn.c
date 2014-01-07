/*
 * Copyright (C) 2013 Huawei Ltd.
 * Author: Jiang Liu <liuj97@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/smp.h>
#include <linux/stop_machine.h>
#include <linux/uaccess.h>
#include <asm/cacheflush.h>
#include <asm/insn.h>

static int aarch64_insn_encoding_class[] = {
	AARCH64_INSN_CLS_UNKNOWN,
	AARCH64_INSN_CLS_UNKNOWN,
	AARCH64_INSN_CLS_UNKNOWN,
	AARCH64_INSN_CLS_UNKNOWN,
	AARCH64_INSN_CLS_LDST,
	AARCH64_INSN_CLS_DP_REG,
	AARCH64_INSN_CLS_LDST,
	AARCH64_INSN_CLS_DP_FPSIMD,
	AARCH64_INSN_CLS_DP_IMM,
	AARCH64_INSN_CLS_DP_IMM,
	AARCH64_INSN_CLS_BR_SYS,
	AARCH64_INSN_CLS_BR_SYS,
	AARCH64_INSN_CLS_LDST,
	AARCH64_INSN_CLS_DP_REG,
	AARCH64_INSN_CLS_LDST,
	AARCH64_INSN_CLS_DP_FPSIMD,
};

enum aarch64_insn_encoding_class __kprobes aarch64_get_insn_class(u32 insn)
{
	return aarch64_insn_encoding_class[(insn >> 25) & 0xf];
}

/* NOP is an alias of HINT */
bool __kprobes aarch64_insn_is_nop(u32 insn)
{
	if (!aarch64_insn_is_hint(insn))
		return false;

	switch (insn & 0xFE0) {
	case AARCH64_INSN_HINT_YIELD:
	case AARCH64_INSN_HINT_WFE:
	case AARCH64_INSN_HINT_WFI:
	case AARCH64_INSN_HINT_SEV:
	case AARCH64_INSN_HINT_SEVL:
		return false;
	default:
		return true;
	}
}

/*
 * In ARMv8-A, A64 instructions have a fixed length of 32 bits and are always
 * little-endian.
 */
int __kprobes aarch64_insn_read(void *addr, u32 *insnp)
{
	int ret;
	u32 val;

	ret = probe_kernel_read(&val, addr, AARCH64_INSN_SIZE);
	if (!ret)
		*insnp = le32_to_cpu(val);

	return ret;
}

int __kprobes aarch64_insn_write(void *addr, u32 insn)
{
	insn = cpu_to_le32(insn);
	return probe_kernel_write(addr, &insn, AARCH64_INSN_SIZE);
}

static bool __kprobes __aarch64_insn_hotpatch_safe(u32 insn)
{
	if (aarch64_get_insn_class(insn) != AARCH64_INSN_CLS_BR_SYS)
		return false;

	return	aarch64_insn_is_b(insn) ||
		aarch64_insn_is_bl(insn) ||
		aarch64_insn_is_svc(insn) ||
		aarch64_insn_is_hvc(insn) ||
		aarch64_insn_is_smc(insn) ||
		aarch64_insn_is_brk(insn) ||
		aarch64_insn_is_nop(insn);
}

/*
 * ARM Architecture Reference Manual for ARMv8 Profile-A, Issue A.a
 * Section B2.6.5 "Concurrent modification and execution of instructions":
 * Concurrent modification and execution of instructions can lead to the
 * resulting instruction performing any behavior that can be achieved by
 * executing any sequence of instructions that can be executed from the
 * same Exception level, except where the instruction before modification
 * and the instruction after modification is a B, BL, NOP, BKPT, SVC, HVC,
 * or SMC instruction.
 */
bool __kprobes aarch64_insn_hotpatch_safe(u32 old_insn, u32 new_insn)
{
	return __aarch64_insn_hotpatch_safe(old_insn) &&
	       __aarch64_insn_hotpatch_safe(new_insn);
}

int __kprobes aarch64_insn_patch_text_nosync(void *addr, u32 insn)
{
	u32 *tp = addr;
	int ret;

	/* A64 instructions must be word aligned */
	if ((uintptr_t)tp & 0x3)
		return -EINVAL;

	ret = aarch64_insn_write(tp, insn);
	if (ret == 0)
		flush_icache_range((uintptr_t)tp,
				   (uintptr_t)tp + AARCH64_INSN_SIZE);

	return ret;
}

struct aarch64_insn_patch {
	void		**text_addrs;
	u32		*new_insns;
	int		insn_cnt;
	atomic_t	cpu_count;
};

static int __kprobes aarch64_insn_patch_text_cb(void *arg)
{
	int i, ret = 0;
	struct aarch64_insn_patch *pp = arg;

	/* The first CPU becomes master */
	if (atomic_inc_return(&pp->cpu_count) == 1) {
		for (i = 0; ret == 0 && i < pp->insn_cnt; i++)
			ret = aarch64_insn_patch_text_nosync(pp->text_addrs[i],
							     pp->new_insns[i]);
		/*
		 * aarch64_insn_patch_text_nosync() calls flush_icache_range(),
		 * which ends with "dsb; isb" pair guaranteeing global
		 * visibility.
		 */
		atomic_set(&pp->cpu_count, -1);
	} else {
		while (atomic_read(&pp->cpu_count) != -1)
			cpu_relax();
		isb();
	}

	return ret;
}

int __kprobes aarch64_insn_patch_text_sync(void *addrs[], u32 insns[], int cnt)
{
	struct aarch64_insn_patch patch = {
		.text_addrs = addrs,
		.new_insns = insns,
		.insn_cnt = cnt,
		.cpu_count = ATOMIC_INIT(0),
	};

	if (cnt <= 0)
		return -EINVAL;

	return stop_machine(aarch64_insn_patch_text_cb, &patch,
			    cpu_online_mask);
}

int __kprobes aarch64_insn_patch_text(void *addrs[], u32 insns[], int cnt)
{
	int ret;
	u32 insn;

	/* Unsafe to patch multiple instructions without synchronizaiton */
	if (cnt == 1) {
		ret = aarch64_insn_read(addrs[0], &insn);
		if (ret)
			return ret;

		if (aarch64_insn_hotpatch_safe(insn, insns[0])) {
			/*
			 * ARMv8 architecture doesn't guarantee all CPUs see
			 * the new instruction after returning from function
			 * aarch64_insn_patch_text_nosync(). So send IPIs to
			 * all other CPUs to achieve instruction
			 * synchronization.
			 */
			ret = aarch64_insn_patch_text_nosync(addrs[0], insns[0]);
			kick_all_cpus_sync();
			return ret;
		}
	}

	return aarch64_insn_patch_text_sync(addrs, insns, cnt);
}
