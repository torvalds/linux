extern int mips_dsemul(struct pt_regs *regs, mips_instruction ir, unsigned long cpc);
extern int do_dsemulret(struct pt_regs *xcp);

/* Instruction which will always cause an address error */
#define AdELOAD 0x8c000001	/* lw $0,1($0) */
/* Instruction which will plainly cause a CP1 exception when FPU is disabled */
#define CP1UNDEF 0x44400001    /* cfc1 $0,$0 undef  */

/* Instruction inserted following the badinst to further tag the sequence */
#define BD_COOKIE 0x0000bd36 /* tne $0,$0 with baggage */

/* Setup which instruction to use for trampoline */
#ifdef STANDALONE_EMULATOR
#define BADINST CP1UNDEF
#else
#define BADINST AdELOAD
#endif
