// SPDX-License-Identifier: GPL-2.0
// Copyright (C) 2005-2017 Andes Technology Corporation

#include <linux/proc_fs.h>
#include <linux/uaccess.h>
#include <linux/sysctl.h>
#include <asm/unaligned.h>

#define DEBUG(enable, tagged, ...)				\
	do{							\
		if (enable) {					\
			if (tagged)				\
			pr_warn("[ %30s() ] ", __func__);	\
			pr_warn(__VA_ARGS__);			\
		}						\
	} while (0)

#define RT(inst)	(((inst) >> 20) & 0x1FUL)
#define RA(inst)	(((inst) >> 15) & 0x1FUL)
#define RB(inst)	(((inst) >> 10) & 0x1FUL)
#define SV(inst)	(((inst) >> 8) & 0x3UL)
#define IMM(inst)	(((inst) >> 0) & 0x7FFFUL)

#define RA3(inst)	(((inst) >> 3) & 0x7UL)
#define RT3(inst)	(((inst) >> 6) & 0x7UL)
#define IMM3U(inst)	(((inst) >> 0) & 0x7UL)

#define RA5(inst)	(((inst) >> 0) & 0x1FUL)
#define RT4(inst)	(((inst) >> 5) & 0xFUL)

#define GET_IMMSVAL(imm_value) \
	(((imm_value >> 14) & 0x1) ? (imm_value - 0x8000) : imm_value)

#define __get8_data(val,addr,err)	\
	__asm__(					\
	"1:	lbi.bi	%1, [%2], #1\n"			\
	"2:\n"						\
	"	.pushsection .text.fixup,\"ax\"\n"	\
	"	.align	2\n"				\
	"3:	movi	%0, #1\n"			\
	"	j	2b\n"				\
	"	.popsection\n"				\
	"	.pushsection __ex_table,\"a\"\n"	\
	"	.align	3\n"				\
	"	.long	1b, 3b\n"			\
	"	.popsection\n"				\
	: "=r" (err), "=&r" (val), "=r" (addr)		\
	: "0" (err), "2" (addr))

#define get16_data(addr, val_ptr)				\
	do {							\
		unsigned int err = 0, v, a = addr;		\
		__get8_data(v,a,err);				\
		*val_ptr =  v << 0;				\
		__get8_data(v,a,err);				\
		*val_ptr |= v << 8;				\
		if (err)					\
			goto fault;				\
		*val_ptr = le16_to_cpu(*val_ptr);		\
	} while(0)

#define get32_data(addr, val_ptr)				\
	do {							\
		unsigned int err = 0, v, a = addr;		\
		__get8_data(v,a,err);				\
		*val_ptr =  v << 0;				\
		__get8_data(v,a,err);				\
		*val_ptr |= v << 8;				\
		__get8_data(v,a,err);				\
		*val_ptr |= v << 16;				\
		__get8_data(v,a,err);				\
		*val_ptr |= v << 24;				\
		if (err)					\
			goto fault;				\
		*val_ptr = le32_to_cpu(*val_ptr);		\
	} while(0)

#define get_data(addr, val_ptr, len)				\
	if (len == 2)						\
		get16_data(addr, val_ptr);			\
	else							\
		get32_data(addr, val_ptr);

#define set16_data(addr, val)					\
	do {							\
		unsigned int err = 0, *ptr = addr ;		\
		val = le32_to_cpu(val);				\
		__asm__(					\
                "1:	sbi.bi 	%2, [%1], #1\n"			\
                "	srli 	%2, %2, #8\n"			\
                "2:	sbi	%2, [%1]\n"			\
		"3:\n"						\
		"	.pushsection .text.fixup,\"ax\"\n"	\
		"	.align	2\n"				\
		"4:	movi	%0, #1\n"			\
		"	j	3b\n"				\
		"	.popsection\n"				\
		"	.pushsection __ex_table,\"a\"\n"	\
		"	.align	3\n"				\
		"	.long	1b, 4b\n"			\
		"	.long	2b, 4b\n"			\
		"	.popsection\n"				\
		: "=r" (err), "+r" (ptr), "+r" (val)		\
		: "0" (err)					\
		);						\
		if (err)					\
			goto fault;				\
	} while(0)

#define set32_data(addr, val)					\
	do {							\
		unsigned int err = 0, *ptr = addr ;		\
		val = le32_to_cpu(val);				\
		__asm__(					\
                "1:	sbi.bi 	%2, [%1], #1\n"			\
                "	srli 	%2, %2, #8\n"			\
                "2:	sbi.bi 	%2, [%1], #1\n"			\
                "	srli 	%2, %2, #8\n"			\
                "3:	sbi.bi 	%2, [%1], #1\n"			\
                "	srli 	%2, %2, #8\n"			\
                "4:	sbi 	%2, [%1]\n"			\
		"5:\n"						\
		"	.pushsection .text.fixup,\"ax\"\n"	\
		"	.align	2\n"				\
		"6:	movi	%0, #1\n"			\
		"	j	5b\n"				\
		"	.popsection\n"				\
		"	.pushsection __ex_table,\"a\"\n"	\
		"	.align	3\n"				\
		"	.long	1b, 6b\n"			\
		"	.long	2b, 6b\n"			\
		"	.long	3b, 6b\n"			\
		"	.long	4b, 6b\n"			\
		"	.popsection\n"				\
		: "=r" (err), "+r" (ptr), "+r" (val)		\
		: "0" (err)					\
		);						\
		if (err)					\
			goto fault;				\
	} while(0)
#define set_data(addr, val, len)				\
	if (len == 2)						\
		set16_data(addr, val);				\
	else							\
		set32_data(addr, val);
#define NDS32_16BIT_INSTRUCTION	0x80000000

extern pte_t va_present(struct mm_struct *mm, unsigned long addr);
extern pte_t va_kernel_present(unsigned long addr);
extern int va_readable(struct pt_regs *regs, unsigned long addr);
extern int va_writable(struct pt_regs *regs, unsigned long addr);

int unalign_access_mode = 0, unalign_access_debug = 0;

static inline unsigned long *idx_to_addr(struct pt_regs *regs, int idx)
{
	/* this should be consistent with ptrace.h */
	if (idx >= 0 && idx <= 25)	/* R0-R25 */
		return &regs->uregs[0] + idx;
	else if (idx >= 28 && idx <= 30)	/* FP, GP, LP */
		return &regs->fp + (idx - 28);
	else if (idx == 31)	/* SP */
		return &regs->sp;
	else
		return NULL;	/* cause a segfault */
}

static inline unsigned long get_inst(unsigned long addr)
{
	return be32_to_cpu(get_unaligned((u32 *) addr));
}

static inline unsigned long sign_extend(unsigned long val, int len)
{
	unsigned long ret = 0;
	unsigned char *s, *t;
	int i = 0;

	val = cpu_to_le32(val);

	s = (void *)&val;
	t = (void *)&ret;

	while (i++ < len)
		*t++ = *s++;

	if (((*(t - 1)) & 0x80) && (i < 4)) {

		while (i++ <= 4)
			*t++ = 0xff;
	}

	return le32_to_cpu(ret);
}

static inline int do_16(unsigned long inst, struct pt_regs *regs)
{
	int imm, regular, load, len, addr_mode, idx_mode;
	unsigned long unaligned_addr, target_val, source_idx, target_idx,
	    shift = 0;
	switch ((inst >> 9) & 0x3F) {

	case 0x12:		/* LHI333    */
		imm = 1;
		regular = 1;
		load = 1;
		len = 2;
		addr_mode = 3;
		idx_mode = 3;
		break;
	case 0x10:		/* LWI333    */
		imm = 1;
		regular = 1;
		load = 1;
		len = 4;
		addr_mode = 3;
		idx_mode = 3;
		break;
	case 0x11:		/* LWI333.bi */
		imm = 1;
		regular = 0;
		load = 1;
		len = 4;
		addr_mode = 3;
		idx_mode = 3;
		break;
	case 0x1A:		/* LWI450    */
		imm = 0;
		regular = 1;
		load = 1;
		len = 4;
		addr_mode = 5;
		idx_mode = 4;
		break;
	case 0x16:		/* SHI333    */
		imm = 1;
		regular = 1;
		load = 0;
		len = 2;
		addr_mode = 3;
		idx_mode = 3;
		break;
	case 0x14:		/* SWI333    */
		imm = 1;
		regular = 1;
		load = 0;
		len = 4;
		addr_mode = 3;
		idx_mode = 3;
		break;
	case 0x15:		/* SWI333.bi */
		imm = 1;
		regular = 0;
		load = 0;
		len = 4;
		addr_mode = 3;
		idx_mode = 3;
		break;
	case 0x1B:		/* SWI450    */
		imm = 0;
		regular = 1;
		load = 0;
		len = 4;
		addr_mode = 5;
		idx_mode = 4;
		break;

	default:
		return -EFAULT;
	}

	if (addr_mode == 3) {
		unaligned_addr = *idx_to_addr(regs, RA3(inst));
		source_idx = RA3(inst);
	} else {
		unaligned_addr = *idx_to_addr(regs, RA5(inst));
		source_idx = RA5(inst);
	}

	if (idx_mode == 3)
		target_idx = RT3(inst);
	else
		target_idx = RT4(inst);

	if (imm)
		shift = IMM3U(inst) * len;

	if (regular)
		unaligned_addr += shift;

	if (load) {
		if (!access_ok((void *)unaligned_addr, len))
			return -EACCES;

		get_data(unaligned_addr, &target_val, len);
		*idx_to_addr(regs, target_idx) = target_val;
	} else {
		if (!access_ok((void *)unaligned_addr, len))
			return -EACCES;
		target_val = *idx_to_addr(regs, target_idx);
		set_data((void *)unaligned_addr, target_val, len);
	}

	if (!regular)
		*idx_to_addr(regs, source_idx) = unaligned_addr + shift;
	regs->ipc += 2;

	return 0;
fault:
	return -EACCES;
}

static inline int do_32(unsigned long inst, struct pt_regs *regs)
{
	int imm, regular, load, len, sign_ext;
	unsigned long unaligned_addr, target_val, shift;

	unaligned_addr = *idx_to_addr(regs, RA(inst));

	switch ((inst >> 25) << 1) {

	case 0x02:		/* LHI       */
		imm = 1;
		regular = 1;
		load = 1;
		len = 2;
		sign_ext = 0;
		break;
	case 0x0A:		/* LHI.bi    */
		imm = 1;
		regular = 0;
		load = 1;
		len = 2;
		sign_ext = 0;
		break;
	case 0x22:		/* LHSI      */
		imm = 1;
		regular = 1;
		load = 1;
		len = 2;
		sign_ext = 1;
		break;
	case 0x2A:		/* LHSI.bi   */
		imm = 1;
		regular = 0;
		load = 1;
		len = 2;
		sign_ext = 1;
		break;
	case 0x04:		/* LWI       */
		imm = 1;
		regular = 1;
		load = 1;
		len = 4;
		sign_ext = 0;
		break;
	case 0x0C:		/* LWI.bi    */
		imm = 1;
		regular = 0;
		load = 1;
		len = 4;
		sign_ext = 0;
		break;
	case 0x12:		/* SHI       */
		imm = 1;
		regular = 1;
		load = 0;
		len = 2;
		sign_ext = 0;
		break;
	case 0x1A:		/* SHI.bi    */
		imm = 1;
		regular = 0;
		load = 0;
		len = 2;
		sign_ext = 0;
		break;
	case 0x14:		/* SWI       */
		imm = 1;
		regular = 1;
		load = 0;
		len = 4;
		sign_ext = 0;
		break;
	case 0x1C:		/* SWI.bi    */
		imm = 1;
		regular = 0;
		load = 0;
		len = 4;
		sign_ext = 0;
		break;

	default:
		switch (inst & 0xff) {

		case 0x01:	/* LH        */
			imm = 0;
			regular = 1;
			load = 1;
			len = 2;
			sign_ext = 0;
			break;
		case 0x05:	/* LH.bi     */
			imm = 0;
			regular = 0;
			load = 1;
			len = 2;
			sign_ext = 0;
			break;
		case 0x11:	/* LHS       */
			imm = 0;
			regular = 1;
			load = 1;
			len = 2;
			sign_ext = 1;
			break;
		case 0x15:	/* LHS.bi    */
			imm = 0;
			regular = 0;
			load = 1;
			len = 2;
			sign_ext = 1;
			break;
		case 0x02:	/* LW        */
			imm = 0;
			regular = 1;
			load = 1;
			len = 4;
			sign_ext = 0;
			break;
		case 0x06:	/* LW.bi     */
			imm = 0;
			regular = 0;
			load = 1;
			len = 4;
			sign_ext = 0;
			break;
		case 0x09:	/* SH        */
			imm = 0;
			regular = 1;
			load = 0;
			len = 2;
			sign_ext = 0;
			break;
		case 0x0D:	/* SH.bi     */
			imm = 0;
			regular = 0;
			load = 0;
			len = 2;
			sign_ext = 0;
			break;
		case 0x0A:	/* SW        */
			imm = 0;
			regular = 1;
			load = 0;
			len = 4;
			sign_ext = 0;
			break;
		case 0x0E:	/* SW.bi     */
			imm = 0;
			regular = 0;
			load = 0;
			len = 4;
			sign_ext = 0;
			break;

		default:
			return -EFAULT;
		}
	}

	if (imm)
		shift = GET_IMMSVAL(IMM(inst)) * len;
	else
		shift = *idx_to_addr(regs, RB(inst)) << SV(inst);

	if (regular)
		unaligned_addr += shift;

	if (load) {

		if (!access_ok((void *)unaligned_addr, len))
			return -EACCES;

		get_data(unaligned_addr, &target_val, len);

		if (sign_ext)
			*idx_to_addr(regs, RT(inst)) =
			    sign_extend(target_val, len);
		else
			*idx_to_addr(regs, RT(inst)) = target_val;
	} else {

		if (!access_ok((void *)unaligned_addr, len))
			return -EACCES;

		target_val = *idx_to_addr(regs, RT(inst));
		set_data((void *)unaligned_addr, target_val, len);
	}

	if (!regular)
		*idx_to_addr(regs, RA(inst)) = unaligned_addr + shift;

	regs->ipc += 4;

	return 0;
fault:
	return -EACCES;
}

int do_unaligned_access(unsigned long addr, struct pt_regs *regs)
{
	unsigned long inst;
	int ret = -EFAULT;
	mm_segment_t seg = get_fs();

	inst = get_inst(regs->ipc);

	DEBUG((unalign_access_debug > 0), 1,
	      "Faulting addr: 0x%08lx, pc: 0x%08lx [inst: 0x%08lx ]\n", addr,
	      regs->ipc, inst);

	set_fs(USER_DS);

	if (inst & NDS32_16BIT_INSTRUCTION)
		ret = do_16((inst >> 16) & 0xffff, regs);
	else
		ret = do_32(inst, regs);
	set_fs(seg);

	return ret;
}

#ifdef CONFIG_PROC_FS

static struct ctl_table alignment_tbl[3] = {
	{
	 .procname = "enable",
	 .data = &unalign_access_mode,
	 .maxlen = sizeof(unalign_access_mode),
	 .mode = 0666,
	 .proc_handler = &proc_dointvec
	}
	,
	{
	 .procname = "debug_info",
	 .data = &unalign_access_debug,
	 .maxlen = sizeof(unalign_access_debug),
	 .mode = 0644,
	 .proc_handler = &proc_dointvec
	}
	,
	{}
};

static struct ctl_table nds32_sysctl_table[2] = {
	{
	 .procname = "unaligned_access",
	 .mode = 0555,
	 .child = alignment_tbl},
	{}
};

static struct ctl_path nds32_path[2] = {
	{.procname = "nds32"},
	{}
};

/*
 * Initialize nds32 alignment-correction interface
 */
static int __init nds32_sysctl_init(void)
{
	register_sysctl_paths(nds32_path, nds32_sysctl_table);
	return 0;
}

__initcall(nds32_sysctl_init);
#endif /* CONFIG_PROC_FS */
