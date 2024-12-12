struct pt_regs;
struct callee_regs;

#ifdef CONFIG_ARC_EMUL_UNALIGNED
int misaligned_fixup(unsigned long address, struct pt_regs *regs,
		     struct callee_regs *cregs);
#else
static inline int
misaligned_fixup(unsigned long address, struct pt_regs *regs,
		 struct callee_regs *cregs)
{
	/* Not fixed */
	return 1;
}
#endif

