// SPDX-License-Identifier: GPL-2.0

#ifndef pr_fmt
#define pr_fmt(fmt)	"stackprot: " fmt
#endif

#include <linux/export.h>
#include <linux/hex.h>
#include <linux/uaccess.h>
#include <linux/printk.h>
#include <asm/abs_lowcore.h>
#include <asm/sections.h>
#include <asm/machine.h>
#include <asm/asm-offsets.h>
#include <asm/arch-stackprotector.h>

#ifdef __DECOMPRESSOR

#define DEBUGP		boot_debug
#define EMERGP		boot_emerg
#define PANIC		boot_panic

#else /* __DECOMPRESSOR */

#define DEBUGP		pr_debug
#define EMERGP		pr_emerg
#define PANIC		panic

#endif /* __DECOMPRESSOR */

int __bootdata_preserved(stack_protector_debug);

unsigned long __stack_chk_guard;
EXPORT_SYMBOL(__stack_chk_guard);

struct insn_ril {
	u8 opc1 : 8;
	u8 r1	: 4;
	u8 opc2 : 4;
	u32 imm;
} __packed;

/*
 * Convert a virtual instruction address to a real instruction address. The
 * decompressor needs to patch instructions within the kernel image based on
 * their virtual addresses, while dynamic address translation is still
 * disabled. Therefore a translation from virtual kernel image addresses to
 * the corresponding physical addresses is required.
 *
 * After dynamic address translation is enabled and when the kernel needs to
 * patch instructions such a translation is not required since the addresses
 * are identical.
 */
static struct insn_ril *vaddress_to_insn(unsigned long vaddress)
{
#ifdef __DECOMPRESSOR
	return (struct insn_ril *)__kernel_pa(vaddress);
#else
	return (struct insn_ril *)vaddress;
#endif
}

static unsigned long insn_to_vaddress(struct insn_ril *insn)
{
#ifdef __DECOMPRESSOR
	return (unsigned long)__kernel_va(insn);
#else
	return (unsigned long)insn;
#endif
}

#define INSN_RIL_STRING_SIZE (sizeof(struct insn_ril) * 2 + 1)

static void insn_ril_to_string(char *str, struct insn_ril *insn)
{
	u8 *ptr = (u8 *)insn;
	int i;

	for (i = 0; i < sizeof(*insn); i++)
		hex_byte_pack(&str[2 * i], ptr[i]);
	str[2 * i] = 0;
}

static void stack_protector_dump(struct insn_ril *old, struct insn_ril *new)
{
	char ostr[INSN_RIL_STRING_SIZE];
	char nstr[INSN_RIL_STRING_SIZE];

	insn_ril_to_string(ostr, old);
	insn_ril_to_string(nstr, new);
	DEBUGP("%016lx: %s -> %s\n", insn_to_vaddress(old), ostr, nstr);
}

static int stack_protector_verify(struct insn_ril *insn, unsigned long kernel_start)
{
	char istr[INSN_RIL_STRING_SIZE];
	unsigned long vaddress, offset;

	/* larl */
	if (insn->opc1 == 0xc0 && insn->opc2 == 0x0)
		return 0;
	/* lgrl */
	if (insn->opc1 == 0xc4 && insn->opc2 == 0x8)
		return 0;
	insn_ril_to_string(istr, insn);
	vaddress = insn_to_vaddress(insn);
	if (__is_defined(__DECOMPRESSOR)) {
		offset = (unsigned long)insn - kernel_start + TEXT_OFFSET;
		EMERGP("Unexpected instruction at %016lx/%016lx: %s\n", vaddress, offset, istr);
		PANIC("Stackprotector error\n");
	} else {
		EMERGP("Unexpected instruction at %016lx: %s\n", vaddress, istr);
	}
	return -EINVAL;
}

int __stack_protector_apply(unsigned long *start, unsigned long *end, unsigned long kernel_start)
{
	unsigned long canary, *loc;
	struct insn_ril *insn, new;
	int rc;

	/*
	 * Convert LARL/LGRL instructions to LLILF so register R1 contains the
	 * address of the per-cpu / per-process stack canary:
	 *
	 * LARL/LGRL R1,__stack_chk_guard => LLILF R1,__lc_stack_canary
	 */
	canary = __LC_STACK_CANARY;
	if (machine_has_relocated_lowcore())
		canary += LOWCORE_ALT_ADDRESS;
	for (loc = start; loc < end; loc++) {
		insn = vaddress_to_insn(*loc);
		rc = stack_protector_verify(insn, kernel_start);
		if (rc)
			return rc;
		new = *insn;
		new.opc1 = 0xc0;
		new.opc2 = 0xf;
		new.imm = canary;
		if (stack_protector_debug)
			stack_protector_dump(insn, &new);
		s390_kernel_write(insn, &new, sizeof(*insn));
	}
	return 0;
}

#ifdef __DECOMPRESSOR
void __stack_protector_apply_early(unsigned long kernel_start)
{
	unsigned long *start, *end;

	start = (unsigned long *)vmlinux.stack_prot_start;
	end = (unsigned long *)vmlinux.stack_prot_end;
	__stack_protector_apply(start, end, kernel_start);
}
#endif
