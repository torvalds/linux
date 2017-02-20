/*
 * align.c - address exception handler for M32R
 *
 * Copyright (c) 2003 Hitoshi Yamamoto
 */

#include <asm/ptrace.h>
#include <linux/uaccess.h>

static int get_reg(struct pt_regs *regs, int nr)
{
	int val;

	if (nr < 4)
		val = *(unsigned long *)(&regs->r0 + nr);
	else if (nr < 7)
		val = *(unsigned long *)(&regs->r4 + (nr - 4));
	else if (nr < 13)
		val = *(unsigned long *)(&regs->r7 + (nr - 7));
	else
		val = *(unsigned long *)(&regs->fp + (nr - 13));

	return val;
}

static void set_reg(struct pt_regs *regs, int nr, int val)
{
	if (nr < 4)
		*(unsigned long *)(&regs->r0 + nr) = val;
	else if (nr < 7)
		*(unsigned long *)(&regs->r4 + (nr - 4)) = val;
	else if (nr < 13)
		*(unsigned long *)(&regs->r7 + (nr - 7)) = val;
	else
		*(unsigned long *)(&regs->fp + (nr - 13)) = val;
}

#define REG1(insn)	(((insn) & 0x0f00) >> 8)
#define REG2(insn)	((insn) & 0x000f)
#define PSW_BC		0x100

/* O- instruction */
#define ISA_LD1		0x20c0	/* ld Rdest, @Rsrc */
#define ISA_LD2		0x20e0	/* ld Rdest, @Rsrc+ */
#define ISA_LDH		0x20a0	/* ldh Rdest, @Rsrc */
#define ISA_LDUH	0x20b0	/* lduh Rdest, @Rsrc */
#define ISA_ST1		0x2040	/* st Rsrc1, @Rsrc2 */
#define ISA_ST2		0x2060	/* st Rsrc1, @+Rsrc2 */
#define ISA_ST3		0x2070	/* st Rsrc1, @-Rsrc2 */
#define ISA_STH1	0x2020	/* sth Rsrc1, @Rsrc2 */
#define ISA_STH2	0x2030	/* sth Rsrc1, @Rsrc2+ */

#ifdef CONFIG_ISA_DUAL_ISSUE

/* OS instruction */
#define ISA_ADD		0x00a0	/* add Rdest, Rsrc */
#define ISA_ADDI	0x4000	/* addi Rdest, #imm8 */
#define ISA_ADDX	0x0090	/* addx Rdest, Rsrc */
#define ISA_AND		0x00c0	/* and Rdest, Rsrc */
#define ISA_CMP		0x0040	/* cmp Rsrc1, Rsrc2 */
#define ISA_CMPEQ	0x0060	/* cmpeq Rsrc1, Rsrc2 */
#define ISA_CMPU	0x0050	/* cmpu Rsrc1, Rsrc2 */
#define ISA_CMPZ	0x0070	/* cmpz Rsrc */
#define ISA_LDI		0x6000	/* ldi Rdest, #imm8 */
#define ISA_MV		0x1080	/* mv Rdest, Rsrc */
#define ISA_NEG		0x0030	/* neg Rdest, Rsrc */
#define ISA_NOP		0x7000	/* nop */
#define ISA_NOT		0x00b0	/* not Rdest, Rsrc */
#define ISA_OR		0x00e0	/* or Rdest, Rsrc */
#define ISA_SUB		0x0020	/* sub Rdest, Rsrc */
#define ISA_SUBX	0x0010	/* subx Rdest, Rsrc */
#define ISA_XOR		0x00d0	/* xor Rdest, Rsrc */

/* -S instruction */
#define ISA_MUL		0x1060	/* mul Rdest, Rsrc */
#define ISA_MULLO_A0	0x3010	/* mullo Rsrc1, Rsrc2, A0 */
#define ISA_MULLO_A1	0x3090	/* mullo Rsrc1, Rsrc2, A1 */
#define ISA_MVFACMI_A0	0x50f2	/* mvfacmi Rdest, A0 */
#define ISA_MVFACMI_A1	0x50f6	/* mvfacmi Rdest, A1 */

static int emu_addi(unsigned short insn, struct pt_regs *regs)
{
	char imm = (char)(insn & 0xff);
	int dest = REG1(insn);
	int val;

	val = get_reg(regs, dest);
	val += imm;
	set_reg(regs, dest, val);

	return 0;
}

static int emu_ldi(unsigned short insn, struct pt_regs *regs)
{
	char imm = (char)(insn & 0xff);

	set_reg(regs, REG1(insn), (int)imm);

	return 0;
}

static int emu_add(unsigned short insn, struct pt_regs *regs)
{
	int dest = REG1(insn);
	int src = REG2(insn);
	int val;

	val = get_reg(regs, dest);
	val += get_reg(regs, src);
	set_reg(regs, dest, val);

	return 0;
}

static int emu_addx(unsigned short insn, struct pt_regs *regs)
{
	int dest = REG1(insn);
	unsigned int val, tmp;

	val = regs->psw & PSW_BC ? 1 : 0;
	tmp = get_reg(regs, dest);
	val += tmp;
	val += (unsigned int)get_reg(regs, REG2(insn));
	set_reg(regs, dest, val);

	/* C bit set */
	if (val < tmp)
		regs->psw |= PSW_BC;
	else
		regs->psw &= ~(PSW_BC);

	return 0;
}

static int emu_and(unsigned short insn, struct pt_regs *regs)
{
	int dest = REG1(insn);
	int val;

	val = get_reg(regs, dest);
	val &= get_reg(regs, REG2(insn));
	set_reg(regs, dest, val);

	return 0;
}

static int emu_cmp(unsigned short insn, struct pt_regs *regs)
{
	if (get_reg(regs, REG1(insn)) < get_reg(regs, REG2(insn)))
		regs->psw |= PSW_BC;
	else
		regs->psw &= ~(PSW_BC);

	return 0;
}

static int emu_cmpeq(unsigned short insn, struct pt_regs *regs)
{
	if (get_reg(regs, REG1(insn)) == get_reg(regs, REG2(insn)))
		regs->psw |= PSW_BC;
	else
		regs->psw &= ~(PSW_BC);

	return 0;
}

static int emu_cmpu(unsigned short insn, struct pt_regs *regs)
{
	if ((unsigned int)get_reg(regs, REG1(insn))
		< (unsigned int)get_reg(regs, REG2(insn)))
		regs->psw |= PSW_BC;
	else
		regs->psw &= ~(PSW_BC);

	return 0;
}

static int emu_cmpz(unsigned short insn, struct pt_regs *regs)
{
	if (!get_reg(regs, REG2(insn)))
		regs->psw |= PSW_BC;
	else
		regs->psw &= ~(PSW_BC);

	return 0;
}

static int emu_mv(unsigned short insn, struct pt_regs *regs)
{
	int val;

	val = get_reg(regs, REG2(insn));
	set_reg(regs, REG1(insn), val);

	return 0;
}

static int emu_neg(unsigned short insn, struct pt_regs *regs)
{
	int val;

	val = get_reg(regs, REG2(insn));
	set_reg(regs, REG1(insn), 0 - val);

	return 0;
}

static int emu_not(unsigned short insn, struct pt_regs *regs)
{
	int val;

	val = get_reg(regs, REG2(insn));
	set_reg(regs, REG1(insn), ~val);

	return 0;
}

static int emu_or(unsigned short insn, struct pt_regs *regs)
{
	int dest = REG1(insn);
	int val;

	val = get_reg(regs, dest);
	val |= get_reg(regs, REG2(insn));
	set_reg(regs, dest, val);

	return 0;
}

static int emu_sub(unsigned short insn, struct pt_regs *regs)
{
	int dest = REG1(insn);
	int val;

	val = get_reg(regs, dest);
	val -= get_reg(regs, REG2(insn));
	set_reg(regs, dest, val);

	return 0;
}

static int emu_subx(unsigned short insn, struct pt_regs *regs)
{
	int dest = REG1(insn);
	unsigned int val, tmp;

	val = tmp = get_reg(regs, dest);
	val -= (unsigned int)get_reg(regs, REG2(insn));
	val -= regs->psw & PSW_BC ? 1 : 0;
	set_reg(regs, dest, val);

	/* C bit set */
	if (val > tmp)
		regs->psw |= PSW_BC;
	else
		regs->psw &= ~(PSW_BC);

	return 0;
}

static int emu_xor(unsigned short insn, struct pt_regs *regs)
{
	int dest = REG1(insn);
	unsigned int val;

	val = (unsigned int)get_reg(regs, dest);
	val ^= (unsigned int)get_reg(regs, REG2(insn));
	set_reg(regs, dest, val);

	return 0;
}

static int emu_mul(unsigned short insn, struct pt_regs *regs)
{
	int dest = REG1(insn);
	int reg1, reg2;

	reg1 = get_reg(regs, dest);
	reg2 = get_reg(regs, REG2(insn));

	__asm__ __volatile__ (
		"mul	%0, %1;		\n\t"
		: "+r" (reg1) : "r" (reg2)
	);

	set_reg(regs, dest, reg1);

	return 0;
}

static int emu_mullo_a0(unsigned short insn, struct pt_regs *regs)
{
	int reg1, reg2;

	reg1 = get_reg(regs, REG1(insn));
	reg2 = get_reg(regs, REG2(insn));

	__asm__ __volatile__ (
		"mullo		%0, %1, a0;	\n\t"
		"mvfachi	%0, a0;		\n\t"
		"mvfaclo	%1, a0;		\n\t"
		: "+r" (reg1), "+r" (reg2)
	);

	regs->acc0h = reg1;
	regs->acc0l = reg2;

	return 0;
}

static int emu_mullo_a1(unsigned short insn, struct pt_regs *regs)
{
	int reg1, reg2;

	reg1 = get_reg(regs, REG1(insn));
	reg2 = get_reg(regs, REG2(insn));

	__asm__ __volatile__ (
		"mullo		%0, %1, a0;	\n\t"
		"mvfachi	%0, a0;		\n\t"
		"mvfaclo	%1, a0;		\n\t"
		: "+r" (reg1), "+r" (reg2)
	);

	regs->acc1h = reg1;
	regs->acc1l = reg2;

	return 0;
}

static int emu_mvfacmi_a0(unsigned short insn, struct pt_regs *regs)
{
	unsigned long val;

	val = (regs->acc0h << 16) | (regs->acc0l >> 16);
	set_reg(regs, REG1(insn), (int)val);

	return 0;
}

static int emu_mvfacmi_a1(unsigned short insn, struct pt_regs *regs)
{
	unsigned long val;

	val = (regs->acc1h << 16) | (regs->acc1l >> 16);
	set_reg(regs, REG1(insn), (int)val);

	return 0;
}

static int emu_m32r2(unsigned short insn, struct pt_regs *regs)
{
	int res = -1;

	if ((insn & 0x7fff) == ISA_NOP)	/* nop */
		return 0;

	switch(insn & 0x7000) {
	case ISA_ADDI:		/* addi Rdest, #imm8 */
		res = emu_addi(insn, regs);
		break;
	case ISA_LDI:		/* ldi Rdest, #imm8 */
		res = emu_ldi(insn, regs);
		break;
	default:
		break;
	}

	if (!res)
		return 0;

	switch(insn & 0x70f0) {
	case ISA_ADD:		/* add Rdest, Rsrc */
		res = emu_add(insn, regs);
		break;
	case ISA_ADDX:		/* addx Rdest, Rsrc */
		res = emu_addx(insn, regs);
		break;
	case ISA_AND:		/* and Rdest, Rsrc */
		res = emu_and(insn, regs);
		break;
	case ISA_CMP:		/* cmp Rsrc1, Rsrc2 */
		res = emu_cmp(insn, regs);
		break;
	case ISA_CMPEQ:		/* cmpeq Rsrc1, Rsrc2 */
		res = emu_cmpeq(insn, regs);
		break;
	case ISA_CMPU:		/* cmpu Rsrc1, Rsrc2 */
		res = emu_cmpu(insn, regs);
		break;
	case ISA_CMPZ:		/* cmpz Rsrc */
		res = emu_cmpz(insn, regs);
		break;
	case ISA_MV:		/* mv Rdest, Rsrc */
		res = emu_mv(insn, regs);
		break;
	case ISA_NEG:		/* neg Rdest, Rsrc */
		res = emu_neg(insn, regs);
		break;
	case ISA_NOT:		/* not Rdest, Rsrc */
		res = emu_not(insn, regs);
		break;
	case ISA_OR:		/* or Rdest, Rsrc */
		res = emu_or(insn, regs);
		break;
	case ISA_SUB:		/* sub Rdest, Rsrc */
		res = emu_sub(insn, regs);
		break;
	case ISA_SUBX:		/* subx Rdest, Rsrc */
		res = emu_subx(insn, regs);
		break;
	case ISA_XOR:		/* xor Rdest, Rsrc */
		res = emu_xor(insn, regs);
		break;
	case ISA_MUL:		/* mul Rdest, Rsrc */
		res = emu_mul(insn, regs);
		break;
	case ISA_MULLO_A0:	/* mullo Rsrc1, Rsrc2 */
		res = emu_mullo_a0(insn, regs);
		break;
	case ISA_MULLO_A1:	/* mullo Rsrc1, Rsrc2 */
		res = emu_mullo_a1(insn, regs);
		break;
	default:
		break;
	}

	if (!res)
		return 0;

	switch(insn & 0x70ff) {
	case ISA_MVFACMI_A0:	/* mvfacmi Rdest */
		res = emu_mvfacmi_a0(insn, regs);
		break;
	case ISA_MVFACMI_A1:	/* mvfacmi Rdest */
		res = emu_mvfacmi_a1(insn, regs);
		break;
	default:
		break;
	}

	return res;
}

#endif	/* CONFIG_ISA_DUAL_ISSUE */

/*
 * ld   : ?010 dest 1100 src
 *        0010 dest 1110 src : ld Rdest, @Rsrc+
 * ldh  : ?010 dest 1010 src
 * lduh : ?010 dest 1011 src
 * st   : ?010 src1 0100 src2
 *        0010 src1 0110 src2 : st Rsrc1, @+Rsrc2
 *        0010 src1 0111 src2 : st Rsrc1, @-Rsrc2
 * sth  : ?010 src1 0010 src2
 */

static int insn_check(unsigned long insn, struct pt_regs *regs,
	unsigned char **ucp)
{
	int res = 0;

	/*
	 * 32bit insn
	 *  ld Rdest, @(disp16, Rsrc)
	 *  st Rdest, @(disp16, Rsrc)
	 */
	if (insn & 0x80000000) {	/* 32bit insn */
		*ucp += (short)(insn & 0x0000ffff);
		regs->bpc += 4;
	} else {			/* 16bit insn */
#ifdef CONFIG_ISA_DUAL_ISSUE
		/* parallel exec check */
		if (!(regs->bpc & 0x2) && insn & 0x8000) {
			res = emu_m32r2((unsigned short)insn, regs);
			regs->bpc += 4;
		} else
#endif	/* CONFIG_ISA_DUAL_ISSUE */
			regs->bpc += 2;
	}

	return res;
}

static int emu_ld(unsigned long insn32, struct pt_regs *regs)
{
	unsigned char *ucp;
	unsigned long val;
	unsigned short insn16;
	int size, src;

	insn16 = insn32 >> 16;
	src = REG2(insn16);
	ucp = (unsigned char *)get_reg(regs, src);

	if (insn_check(insn32, regs, &ucp))
		return -1;

	size = insn16 & 0x0040 ? 4 : 2;
	if (copy_from_user(&val, ucp, size))
		return -1;

	if (size == 2)
		val >>= 16;

	/* ldh sign check */
	if ((insn16 & 0x00f0) == 0x00a0 && (val & 0x8000))
		val |= 0xffff0000;

	set_reg(regs, REG1(insn16), val);

	/* ld increment check */
	if ((insn16 & 0xf0f0) == ISA_LD2)	/* ld Rdest, @Rsrc+ */
		set_reg(regs, src, (unsigned long)(ucp + 4));

	return 0;
}

static int emu_st(unsigned long insn32, struct pt_regs *regs)
{
	unsigned char *ucp;
	unsigned long val;
	unsigned short insn16;
	int size, src2;

	insn16 = insn32 >> 16;
	src2 = REG2(insn16);

	ucp = (unsigned char *)get_reg(regs, src2);

	if (insn_check(insn32, regs, &ucp))
		return -1;

	size = insn16 & 0x0040 ? 4 : 2;
	val = get_reg(regs, REG1(insn16));
	if (size == 2)
		val <<= 16;

	/* st inc/dec check */
	if ((insn16 & 0xf0e0) == 0x2060) {
		if (insn16 & 0x0010)
			ucp -= 4;
		else
			ucp += 4;

		set_reg(regs, src2, (unsigned long)ucp);
	}

	if (copy_to_user(ucp, &val, size))
		return -1;

	/* sth inc check */
	if ((insn16 & 0xf0f0) == ISA_STH2) {
		ucp += 2;
		set_reg(regs, src2, (unsigned long)ucp);
	}

	return 0;
}

int handle_unaligned_access(unsigned long insn32, struct pt_regs *regs)
{
	unsigned short insn16;
	int res;

	insn16 = insn32 >> 16;

	/* ld or st check */
	if ((insn16 & 0x7000) != 0x2000)
		return -1;

	/* insn alignment check */
	if ((insn16 & 0x8000) && (regs->bpc & 3))
		return -1;

	if (insn16 & 0x0080)	/* ld */
		res = emu_ld(insn32, regs);
	else			/* st */
		res = emu_st(insn32, regs);

	return res;
}

