/*
 * arch/s390/kernel/dis.c
 *
 * Disassemble s390 instructions.
 *
 * Copyright IBM Corp. 2007
 * Author(s): Martin Schwidefsky (schwidefsky@de.ibm.com),
 */

#include <linux/sched.h>
#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/ptrace.h>
#include <linux/timer.h>
#include <linux/mm.h>
#include <linux/smp.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/reboot.h>
#include <linux/kprobes.h>
#include <linux/kdebug.h>

#include <asm/system.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <asm/atomic.h>
#include <asm/mathemu.h>
#include <asm/cpcmd.h>
#include <asm/s390_ext.h>
#include <asm/lowcore.h>
#include <asm/debug.h>

#ifndef CONFIG_64BIT
#define ONELONG "%08lx: "
#else /* CONFIG_64BIT */
#define ONELONG "%016lx: "
#endif /* CONFIG_64BIT */

#define OPERAND_GPR	0x1	/* Operand printed as %rx */
#define OPERAND_FPR	0x2	/* Operand printed as %fx */
#define OPERAND_AR	0x4	/* Operand printed as %ax */
#define OPERAND_CR	0x8	/* Operand printed as %cx */
#define OPERAND_DISP	0x10	/* Operand printed as displacement */
#define OPERAND_BASE	0x20	/* Operand printed as base register */
#define OPERAND_INDEX	0x40	/* Operand printed as index register */
#define OPERAND_PCREL	0x80	/* Operand printed as pc-relative symbol */
#define OPERAND_SIGNED	0x100	/* Operand printed as signed value */
#define OPERAND_LENGTH	0x200	/* Operand printed as length (+1) */

enum {
	UNUSED,	/* Indicates the end of the operand list */
	R_8,	/* GPR starting at position 8 */
	R_12,	/* GPR starting at position 12 */
	R_16,	/* GPR starting at position 16 */
	R_20,	/* GPR starting at position 20 */
	R_24,	/* GPR starting at position 24 */
	R_28,	/* GPR starting at position 28 */
	R_32,	/* GPR starting at position 32 */
	F_8,	/* FPR starting at position 8 */
	F_12,	/* FPR starting at position 12 */
	F_16,	/* FPR starting at position 16 */
	F_20,	/* FPR starting at position 16 */
	F_24,	/* FPR starting at position 24 */
	F_28,	/* FPR starting at position 28 */
	F_32,	/* FPR starting at position 32 */
	A_8,	/* Access reg. starting at position 8 */
	A_12,	/* Access reg. starting at position 12 */
	A_24,	/* Access reg. starting at position 24 */
	A_28,	/* Access reg. starting at position 28 */
	C_8,	/* Control reg. starting at position 8 */
	C_12,	/* Control reg. starting at position 12 */
	B_16,	/* Base register starting at position 16 */
	B_32,	/* Base register starting at position 32 */
	X_12,	/* Index register starting at position 12 */
	D_20,	/* Displacement starting at position 20 */
	D_36,	/* Displacement starting at position 36 */
	D20_20,	/* 20 bit displacement starting at 20 */
	L4_8,	/* 4 bit length starting at position 8 */
	L4_12,	/* 4 bit length starting at position 12 */
	L8_8,	/* 8 bit length starting at position 8 */
	U4_8,	/* 4 bit unsigned value starting at 8 */
	U4_12,	/* 4 bit unsigned value starting at 12 */
	U4_16,	/* 4 bit unsigned value starting at 16 */
	U4_20,	/* 4 bit unsigned value starting at 20 */
	U8_8,	/* 8 bit unsigned value starting at 8 */
	U8_16,	/* 8 bit unsigned value starting at 16 */
	I16_16,	/* 16 bit signed value starting at 16 */
	U16_16,	/* 16 bit unsigned value starting at 16 */
	J16_16,	/* PC relative jump offset at 16 */
	J32_16,	/* PC relative long offset at 16 */
	I32_16,	/* 32 bit signed value starting at 16 */
	U32_16,	/* 32 bit unsigned value starting at 16 */
	M_16,	/* 4 bit optional mask starting at 16 */
	RO_28,	/* optional GPR starting at position 28 */
};

/*
 * Enumeration of the different instruction formats.
 * For details consult the principles of operation.
 */
enum {
	INSTR_INVALID,
	INSTR_E, INSTR_RIE_RRP, INSTR_RIL_RI, INSTR_RIL_RP, INSTR_RIL_RU,
	INSTR_RIL_UP, INSTR_RI_RI, INSTR_RI_RP, INSTR_RI_RU, INSTR_RI_UP,
	INSTR_RRE_00, INSTR_RRE_0R, INSTR_RRE_AA, INSTR_RRE_AR, INSTR_RRE_F0,
	INSTR_RRE_FF, INSTR_RRE_R0, INSTR_RRE_RA, INSTR_RRE_RF, INSTR_RRE_RR,
	INSTR_RRE_RR_OPT, INSTR_RRF_F0FF, INSTR_RRF_FUFF, INSTR_RRF_M0RR,
	INSTR_RRF_R0RR, INSTR_RRF_RURR, INSTR_RRF_U0FF, INSTR_RRF_U0RF,
	INSTR_RR_FF, INSTR_RR_R0, INSTR_RR_RR, INSTR_RR_U0, INSTR_RR_UR,
	INSTR_RSE_CCRD, INSTR_RSE_RRRD, INSTR_RSE_RURD, INSTR_RSI_RRP,
	INSTR_RSL_R0RD, INSTR_RSY_AARD, INSTR_RSY_CCRD, INSTR_RSY_RRRD,
	INSTR_RSY_RURD, INSTR_RS_AARD, INSTR_RS_CCRD, INSTR_RS_R0RD,
	INSTR_RS_RRRD, INSTR_RS_RURD, INSTR_RXE_FRRD, INSTR_RXE_RRRD,
	INSTR_RXF_FRRDF, INSTR_RXY_FRRD, INSTR_RXY_RRRD, INSTR_RX_FRRD,
	INSTR_RX_RRRD, INSTR_RX_URRD, INSTR_SIY_URD, INSTR_SI_URD,
	INSTR_SSE_RDRD, INSTR_SSF_RRDRD, INSTR_SS_L0RDRD, INSTR_SS_LIRDRD,
	INSTR_SS_LLRDRD, INSTR_SS_RRRDRD, INSTR_SS_RRRDRD2, INSTR_SS_RRRDRD3,
	INSTR_S_00, INSTR_S_RD,
};

struct operand {
	int bits;		/* The number of bits in the operand. */
	int shift;		/* The number of bits to shift. */
	int flags;		/* One bit syntax flags. */
};

struct insn {
	const char name[5];
	unsigned char opfrag;
	unsigned char format;
};

static const struct operand operands[] =
{
	[UNUSED]  = { 0, 0, 0 },
	[R_8]	 = {  4,  8, OPERAND_GPR },
	[R_12]	 = {  4, 12, OPERAND_GPR },
	[R_16]	 = {  4, 16, OPERAND_GPR },
	[R_20]	 = {  4, 20, OPERAND_GPR },
	[R_24]	 = {  4, 24, OPERAND_GPR },
	[R_28]	 = {  4, 28, OPERAND_GPR },
	[R_32]	 = {  4, 32, OPERAND_GPR },
	[F_8]	 = {  4,  8, OPERAND_FPR },
	[F_12]	 = {  4, 12, OPERAND_FPR },
	[F_16]	 = {  4, 16, OPERAND_FPR },
	[F_20]	 = {  4, 16, OPERAND_FPR },
	[F_24]	 = {  4, 24, OPERAND_FPR },
	[F_28]	 = {  4, 28, OPERAND_FPR },
	[F_32]	 = {  4, 32, OPERAND_FPR },
	[A_8]	 = {  4,  8, OPERAND_AR },
	[A_12]	 = {  4, 12, OPERAND_AR },
	[A_24]	 = {  4, 24, OPERAND_AR },
	[A_28]	 = {  4, 28, OPERAND_AR },
	[C_8]	 = {  4,  8, OPERAND_CR },
	[C_12]	 = {  4, 12, OPERAND_CR },
	[B_16]	 = {  4, 16, OPERAND_BASE | OPERAND_GPR },
	[B_32]	 = {  4, 32, OPERAND_BASE | OPERAND_GPR },
	[X_12]	 = {  4, 12, OPERAND_INDEX | OPERAND_GPR },
	[D_20]	 = { 12, 20, OPERAND_DISP },
	[D_36]	 = { 12, 36, OPERAND_DISP },
	[D20_20] = { 20, 20, OPERAND_DISP | OPERAND_SIGNED },
	[L4_8]	 = {  4,  8, OPERAND_LENGTH },
	[L4_12]  = {  4, 12, OPERAND_LENGTH },
	[L8_8]	 = {  8,  8, OPERAND_LENGTH },
	[U4_8]	 = {  4,  8, 0 },
	[U4_12]  = {  4, 12, 0 },
	[U4_16]  = {  4, 16, 0 },
	[U4_20]  = {  4, 20, 0 },
	[U8_8]	 = {  8,  8, 0 },
	[U8_16]  = {  8, 16, 0 },
	[I16_16] = { 16, 16, OPERAND_SIGNED },
	[U16_16] = { 16, 16, 0 },
	[J16_16] = { 16, 16, OPERAND_PCREL },
	[J32_16] = { 32, 16, OPERAND_PCREL },
	[I32_16] = { 32, 16, OPERAND_SIGNED },
	[U32_16] = { 32, 16, 0 },
	[M_16]	 = {  4, 16, 0 },
	[RO_28]  = {  4, 28, OPERAND_GPR }
};

static const unsigned char formats[][7] = {
	[INSTR_E]	  = { 0xff, 0,0,0,0,0,0 },	       /* e.g. pr    */
	[INSTR_RIE_RRP]	  = { 0xff, R_8,R_12,J16_16,0,0,0 },   /* e.g. brxhg */
	[INSTR_RIL_RP]	  = { 0x0f, R_8,J32_16,0,0,0,0 },      /* e.g. brasl */
	[INSTR_RIL_UP]	  = { 0x0f, U4_8,J32_16,0,0,0,0 },     /* e.g. brcl  */
	[INSTR_RIL_RI]	  = { 0x0f, R_8,I32_16,0,0,0,0 },      /* e.g. afi   */
	[INSTR_RIL_RU]	  = { 0x0f, R_8,U32_16,0,0,0,0 },      /* e.g. alfi  */
	[INSTR_RI_RI]	  = { 0x0f, R_8,I16_16,0,0,0,0 },      /* e.g. ahi   */
	[INSTR_RI_RP]	  = { 0x0f, R_8,J16_16,0,0,0,0 },      /* e.g. brct  */
	[INSTR_RI_RU]	  = { 0x0f, R_8,U16_16,0,0,0,0 },      /* e.g. tml   */
	[INSTR_RI_UP]	  = { 0x0f, U4_8,J16_16,0,0,0,0 },     /* e.g. brc   */
	[INSTR_RRE_00]	  = { 0xff, 0,0,0,0,0,0 },	       /* e.g. palb  */
	[INSTR_RRE_0R]	  = { 0xff, R_28,0,0,0,0,0 },	       /* e.g. tb    */
	[INSTR_RRE_AA]	  = { 0xff, A_24,A_28,0,0,0,0 },       /* e.g. cpya  */
	[INSTR_RRE_AR]	  = { 0xff, A_24,R_28,0,0,0,0 },       /* e.g. sar   */
	[INSTR_RRE_F0]	  = { 0xff, F_24,0,0,0,0,0 },	       /* e.g. sqer  */
	[INSTR_RRE_FF]	  = { 0xff, F_24,F_28,0,0,0,0 },       /* e.g. debr  */
	[INSTR_RRE_R0]	  = { 0xff, R_24,0,0,0,0,0 },	       /* e.g. ipm   */
	[INSTR_RRE_RA]	  = { 0xff, R_24,A_28,0,0,0,0 },       /* e.g. ear   */
	[INSTR_RRE_RF]	  = { 0xff, R_24,F_28,0,0,0,0 },       /* e.g. cefbr */
	[INSTR_RRE_RR]	  = { 0xff, R_24,R_28,0,0,0,0 },       /* e.g. lura  */
	[INSTR_RRE_RR_OPT]= { 0xff, R_24,RO_28,0,0,0,0 },      /* efpc, sfpc */
	[INSTR_RRF_F0FF]  = { 0xff, F_16,F_24,F_28,0,0,0 },    /* e.g. madbr */
	[INSTR_RRF_FUFF]  = { 0xff, F_24,F_16,F_28,U4_20,0,0 },/* e.g. didbr */
	[INSTR_RRF_RURR]  = { 0xff, R_24,R_28,R_16,U4_20,0,0 },/* e.g. .insn */
	[INSTR_RRF_R0RR]  = { 0xff, R_24,R_16,R_28,0,0,0 },    /* e.g. idte  */
	[INSTR_RRF_U0FF]  = { 0xff, F_24,U4_16,F_28,0,0,0 },   /* e.g. fixr  */
	[INSTR_RRF_U0RF]  = { 0xff, R_24,U4_16,F_28,0,0,0 },   /* e.g. cfebr */
	[INSTR_RRF_M0RR]  = { 0xff, R_24,R_28,M_16,0,0,0 },    /* e.g. sske  */
	[INSTR_RR_FF]	  = { 0xff, F_8,F_12,0,0,0,0 },        /* e.g. adr   */
	[INSTR_RR_R0]	  = { 0xff, R_8, 0,0,0,0,0 },	       /* e.g. spm   */
	[INSTR_RR_RR]	  = { 0xff, R_8,R_12,0,0,0,0 },        /* e.g. lr    */
	[INSTR_RR_U0]	  = { 0xff, U8_8, 0,0,0,0,0 },	       /* e.g. svc   */
	[INSTR_RR_UR]	  = { 0xff, U4_8,R_12,0,0,0,0 },       /* e.g. bcr   */
	[INSTR_RSE_RRRD]  = { 0xff, R_8,R_12,D_20,B_16,0,0 },  /* e.g. lmh   */
	[INSTR_RSE_CCRD]  = { 0xff, C_8,C_12,D_20,B_16,0,0 },  /* e.g. lmh   */
	[INSTR_RSE_RURD]  = { 0xff, R_8,U4_12,D_20,B_16,0,0 }, /* e.g. icmh  */
	[INSTR_RSL_R0RD]  = { 0xff, R_8,D_20,B_16,0,0,0 },     /* e.g. tp    */
	[INSTR_RSI_RRP]	  = { 0xff, R_8,R_12,J16_16,0,0,0 },   /* e.g. brxh  */
	[INSTR_RSY_RRRD]  = { 0xff, R_8,R_12,D20_20,B_16,0,0 },/* e.g. stmy  */
	[INSTR_RSY_RURD]  = { 0xff, R_8,U4_12,D20_20,B_16,0,0 },
							       /* e.g. icmh  */
	[INSTR_RSY_AARD]  = { 0xff, A_8,A_12,D20_20,B_16,0,0 },/* e.g. lamy  */
	[INSTR_RSY_CCRD]  = { 0xff, C_8,C_12,D20_20,B_16,0,0 },/* e.g. lamy  */
	[INSTR_RS_AARD]	  = { 0xff, A_8,A_12,D_20,B_16,0,0 },  /* e.g. lam   */
	[INSTR_RS_CCRD]	  = { 0xff, C_8,C_12,D_20,B_16,0,0 },  /* e.g. lctl  */
	[INSTR_RS_R0RD]	  = { 0xff, R_8,D_20,B_16,0,0,0 },     /* e.g. sll   */
	[INSTR_RS_RRRD]	  = { 0xff, R_8,R_12,D_20,B_16,0,0 },  /* e.g. cs    */
	[INSTR_RS_RURD]	  = { 0xff, R_8,U4_12,D_20,B_16,0,0 }, /* e.g. icm   */
	[INSTR_RXE_FRRD]  = { 0xff, F_8,D_20,X_12,B_16,0,0 },  /* e.g. axbr  */
	[INSTR_RXE_RRRD]  = { 0xff, R_8,D_20,X_12,B_16,0,0 },  /* e.g. lg    */
	[INSTR_RXF_FRRDF] = { 0xff, F_32,F_8,D_20,X_12,B_16,0 },
							       /* e.g. madb  */
	[INSTR_RXY_RRRD]  = { 0xff, R_8,D20_20,X_12,B_16,0,0 },/* e.g. ly    */
	[INSTR_RXY_FRRD]  = { 0xff, F_8,D20_20,X_12,B_16,0,0 },/* e.g. ley   */
	[INSTR_RX_FRRD]	  = { 0xff, F_8,D_20,X_12,B_16,0,0 },  /* e.g. ae    */
	[INSTR_RX_RRRD]	  = { 0xff, R_8,D_20,X_12,B_16,0,0 },  /* e.g. l     */
	[INSTR_RX_URRD]	  = { 0xff, U4_8,D_20,X_12,B_16,0,0 }, /* e.g. bc    */
	[INSTR_SI_URD]	  = { 0xff, D_20,B_16,U8_8,0,0,0 },    /* e.g. cli   */
	[INSTR_SIY_URD]	  = { 0xff, D20_20,B_16,U8_8,0,0,0 },  /* e.g. tmy   */
	[INSTR_SSE_RDRD]  = { 0xff, D_20,B_16,D_36,B_32,0,0 }, /* e.g. mvsdk */
	[INSTR_SS_L0RDRD] = { 0xff, D_20,L8_8,B_16,D_36,B_32,0 },
							       /* e.g. mvc   */
	[INSTR_SS_LIRDRD] = { 0xff, D_20,L4_8,B_16,D_36,B_32,U4_12 },
							       /* e.g. srp   */
	[INSTR_SS_LLRDRD] = { 0xff, D_20,L4_8,B_16,D_36,L4_12,B_32 },
							       /* e.g. pack  */
	[INSTR_SS_RRRDRD] = { 0xff, D_20,R_8,B_16,D_36,B_32,R_12 },
							       /* e.g. mvck  */
	[INSTR_SS_RRRDRD2]= { 0xff, R_8,D_20,B_16,R_12,D_36,B_32 },
							       /* e.g. plo   */
	[INSTR_SS_RRRDRD3]= { 0xff, R_8,R_12,D_20,B_16,D_36,B_32 },
							       /* e.g. lmd   */
	[INSTR_S_00]	  = { 0xff, 0,0,0,0,0,0 },	       /* e.g. hsch  */
	[INSTR_S_RD]	  = { 0xff, D_20,B_16,0,0,0,0 },       /* e.g. lpsw  */
	[INSTR_SSF_RRDRD] = { 0x00, D_20,B_16,D_36,B_32,R_8,0 },
							       /* e.g. mvcos */
};

static struct insn opcode[] = {
#ifdef CONFIG_64BIT
	{ "lmd", 0xef, INSTR_SS_RRRDRD3 },
#endif
	{ "spm", 0x04, INSTR_RR_R0 },
	{ "balr", 0x05, INSTR_RR_RR },
	{ "bctr", 0x06, INSTR_RR_RR },
	{ "bcr", 0x07, INSTR_RR_UR },
	{ "svc", 0x0a, INSTR_RR_U0 },
	{ "bsm", 0x0b, INSTR_RR_RR },
	{ "bassm", 0x0c, INSTR_RR_RR },
	{ "basr", 0x0d, INSTR_RR_RR },
	{ "mvcl", 0x0e, INSTR_RR_RR },
	{ "clcl", 0x0f, INSTR_RR_RR },
	{ "lpr", 0x10, INSTR_RR_RR },
	{ "lnr", 0x11, INSTR_RR_RR },
	{ "ltr", 0x12, INSTR_RR_RR },
	{ "lcr", 0x13, INSTR_RR_RR },
	{ "nr", 0x14, INSTR_RR_RR },
	{ "clr", 0x15, INSTR_RR_RR },
	{ "or", 0x16, INSTR_RR_RR },
	{ "xr", 0x17, INSTR_RR_RR },
	{ "lr", 0x18, INSTR_RR_RR },
	{ "cr", 0x19, INSTR_RR_RR },
	{ "ar", 0x1a, INSTR_RR_RR },
	{ "sr", 0x1b, INSTR_RR_RR },
	{ "mr", 0x1c, INSTR_RR_RR },
	{ "dr", 0x1d, INSTR_RR_RR },
	{ "alr", 0x1e, INSTR_RR_RR },
	{ "slr", 0x1f, INSTR_RR_RR },
	{ "lpdr", 0x20, INSTR_RR_FF },
	{ "lndr", 0x21, INSTR_RR_FF },
	{ "ltdr", 0x22, INSTR_RR_FF },
	{ "lcdr", 0x23, INSTR_RR_FF },
	{ "hdr", 0x24, INSTR_RR_FF },
	{ "ldxr", 0x25, INSTR_RR_FF },
	{ "lrdr", 0x25, INSTR_RR_FF },
	{ "mxr", 0x26, INSTR_RR_FF },
	{ "mxdr", 0x27, INSTR_RR_FF },
	{ "ldr", 0x28, INSTR_RR_FF },
	{ "cdr", 0x29, INSTR_RR_FF },
	{ "adr", 0x2a, INSTR_RR_FF },
	{ "sdr", 0x2b, INSTR_RR_FF },
	{ "mdr", 0x2c, INSTR_RR_FF },
	{ "ddr", 0x2d, INSTR_RR_FF },
	{ "awr", 0x2e, INSTR_RR_FF },
	{ "swr", 0x2f, INSTR_RR_FF },
	{ "lper", 0x30, INSTR_RR_FF },
	{ "lner", 0x31, INSTR_RR_FF },
	{ "lter", 0x32, INSTR_RR_FF },
	{ "lcer", 0x33, INSTR_RR_FF },
	{ "her", 0x34, INSTR_RR_FF },
	{ "ledr", 0x35, INSTR_RR_FF },
	{ "lrer", 0x35, INSTR_RR_FF },
	{ "axr", 0x36, INSTR_RR_FF },
	{ "sxr", 0x37, INSTR_RR_FF },
	{ "ler", 0x38, INSTR_RR_FF },
	{ "cer", 0x39, INSTR_RR_FF },
	{ "aer", 0x3a, INSTR_RR_FF },
	{ "ser", 0x3b, INSTR_RR_FF },
	{ "mder", 0x3c, INSTR_RR_FF },
	{ "mer", 0x3c, INSTR_RR_FF },
	{ "der", 0x3d, INSTR_RR_FF },
	{ "aur", 0x3e, INSTR_RR_FF },
	{ "sur", 0x3f, INSTR_RR_FF },
	{ "sth", 0x40, INSTR_RX_RRRD },
	{ "la", 0x41, INSTR_RX_RRRD },
	{ "stc", 0x42, INSTR_RX_RRRD },
	{ "ic", 0x43, INSTR_RX_RRRD },
	{ "ex", 0x44, INSTR_RX_RRRD },
	{ "bal", 0x45, INSTR_RX_RRRD },
	{ "bct", 0x46, INSTR_RX_RRRD },
	{ "bc", 0x47, INSTR_RX_URRD },
	{ "lh", 0x48, INSTR_RX_RRRD },
	{ "ch", 0x49, INSTR_RX_RRRD },
	{ "ah", 0x4a, INSTR_RX_RRRD },
	{ "sh", 0x4b, INSTR_RX_RRRD },
	{ "mh", 0x4c, INSTR_RX_RRRD },
	{ "bas", 0x4d, INSTR_RX_RRRD },
	{ "cvd", 0x4e, INSTR_RX_RRRD },
	{ "cvb", 0x4f, INSTR_RX_RRRD },
	{ "st", 0x50, INSTR_RX_RRRD },
	{ "lae", 0x51, INSTR_RX_RRRD },
	{ "n", 0x54, INSTR_RX_RRRD },
	{ "cl", 0x55, INSTR_RX_RRRD },
	{ "o", 0x56, INSTR_RX_RRRD },
	{ "x", 0x57, INSTR_RX_RRRD },
	{ "l", 0x58, INSTR_RX_RRRD },
	{ "c", 0x59, INSTR_RX_RRRD },
	{ "a", 0x5a, INSTR_RX_RRRD },
	{ "s", 0x5b, INSTR_RX_RRRD },
	{ "m", 0x5c, INSTR_RX_RRRD },
	{ "d", 0x5d, INSTR_RX_RRRD },
	{ "al", 0x5e, INSTR_RX_RRRD },
	{ "sl", 0x5f, INSTR_RX_RRRD },
	{ "std", 0x60, INSTR_RX_FRRD },
	{ "mxd", 0x67, INSTR_RX_FRRD },
	{ "ld", 0x68, INSTR_RX_FRRD },
	{ "cd", 0x69, INSTR_RX_FRRD },
	{ "ad", 0x6a, INSTR_RX_FRRD },
	{ "sd", 0x6b, INSTR_RX_FRRD },
	{ "md", 0x6c, INSTR_RX_FRRD },
	{ "dd", 0x6d, INSTR_RX_FRRD },
	{ "aw", 0x6e, INSTR_RX_FRRD },
	{ "sw", 0x6f, INSTR_RX_FRRD },
	{ "ste", 0x70, INSTR_RX_FRRD },
	{ "ms", 0x71, INSTR_RX_RRRD },
	{ "le", 0x78, INSTR_RX_FRRD },
	{ "ce", 0x79, INSTR_RX_FRRD },
	{ "ae", 0x7a, INSTR_RX_FRRD },
	{ "se", 0x7b, INSTR_RX_FRRD },
	{ "mde", 0x7c, INSTR_RX_FRRD },
	{ "me", 0x7c, INSTR_RX_FRRD },
	{ "de", 0x7d, INSTR_RX_FRRD },
	{ "au", 0x7e, INSTR_RX_FRRD },
	{ "su", 0x7f, INSTR_RX_FRRD },
	{ "ssm", 0x80, INSTR_S_RD },
	{ "lpsw", 0x82, INSTR_S_RD },
	{ "diag", 0x83, INSTR_RS_RRRD },
	{ "brxh", 0x84, INSTR_RSI_RRP },
	{ "brxle", 0x85, INSTR_RSI_RRP },
	{ "bxh", 0x86, INSTR_RS_RRRD },
	{ "bxle", 0x87, INSTR_RS_RRRD },
	{ "srl", 0x88, INSTR_RS_R0RD },
	{ "sll", 0x89, INSTR_RS_R0RD },
	{ "sra", 0x8a, INSTR_RS_R0RD },
	{ "sla", 0x8b, INSTR_RS_R0RD },
	{ "srdl", 0x8c, INSTR_RS_R0RD },
	{ "sldl", 0x8d, INSTR_RS_R0RD },
	{ "srda", 0x8e, INSTR_RS_R0RD },
	{ "slda", 0x8f, INSTR_RS_R0RD },
	{ "stm", 0x90, INSTR_RS_RRRD },
	{ "tm", 0x91, INSTR_SI_URD },
	{ "mvi", 0x92, INSTR_SI_URD },
	{ "ts", 0x93, INSTR_S_RD },
	{ "ni", 0x94, INSTR_SI_URD },
	{ "cli", 0x95, INSTR_SI_URD },
	{ "oi", 0x96, INSTR_SI_URD },
	{ "xi", 0x97, INSTR_SI_URD },
	{ "lm", 0x98, INSTR_RS_RRRD },
	{ "trace", 0x99, INSTR_RS_RRRD },
	{ "lam", 0x9a, INSTR_RS_AARD },
	{ "stam", 0x9b, INSTR_RS_AARD },
	{ "mvcle", 0xa8, INSTR_RS_RRRD },
	{ "clcle", 0xa9, INSTR_RS_RRRD },
	{ "stnsm", 0xac, INSTR_SI_URD },
	{ "stosm", 0xad, INSTR_SI_URD },
	{ "sigp", 0xae, INSTR_RS_RRRD },
	{ "mc", 0xaf, INSTR_SI_URD },
	{ "lra", 0xb1, INSTR_RX_RRRD },
	{ "stctl", 0xb6, INSTR_RS_CCRD },
	{ "lctl", 0xb7, INSTR_RS_CCRD },
	{ "cs", 0xba, INSTR_RS_RRRD },
	{ "cds", 0xbb, INSTR_RS_RRRD },
	{ "clm", 0xbd, INSTR_RS_RURD },
	{ "stcm", 0xbe, INSTR_RS_RURD },
	{ "icm", 0xbf, INSTR_RS_RURD },
	{ "mvn", 0xd1, INSTR_SS_L0RDRD },
	{ "mvc", 0xd2, INSTR_SS_L0RDRD },
	{ "mvz", 0xd3, INSTR_SS_L0RDRD },
	{ "nc", 0xd4, INSTR_SS_L0RDRD },
	{ "clc", 0xd5, INSTR_SS_L0RDRD },
	{ "oc", 0xd6, INSTR_SS_L0RDRD },
	{ "xc", 0xd7, INSTR_SS_L0RDRD },
	{ "mvck", 0xd9, INSTR_SS_RRRDRD },
	{ "mvcp", 0xda, INSTR_SS_RRRDRD },
	{ "mvcs", 0xdb, INSTR_SS_RRRDRD },
	{ "tr", 0xdc, INSTR_SS_L0RDRD },
	{ "trt", 0xdd, INSTR_SS_L0RDRD },
	{ "ed", 0xde, INSTR_SS_L0RDRD },
	{ "edmk", 0xdf, INSTR_SS_L0RDRD },
	{ "pku", 0xe1, INSTR_SS_L0RDRD },
	{ "unpku", 0xe2, INSTR_SS_L0RDRD },
	{ "mvcin", 0xe8, INSTR_SS_L0RDRD },
	{ "pka", 0xe9, INSTR_SS_L0RDRD },
	{ "unpka", 0xea, INSTR_SS_L0RDRD },
	{ "plo", 0xee, INSTR_SS_RRRDRD2 },
	{ "srp", 0xf0, INSTR_SS_LIRDRD },
	{ "mvo", 0xf1, INSTR_SS_LLRDRD },
	{ "pack", 0xf2, INSTR_SS_LLRDRD },
	{ "unpk", 0xf3, INSTR_SS_LLRDRD },
	{ "zap", 0xf8, INSTR_SS_LLRDRD },
	{ "cp", 0xf9, INSTR_SS_LLRDRD },
	{ "ap", 0xfa, INSTR_SS_LLRDRD },
	{ "sp", 0xfb, INSTR_SS_LLRDRD },
	{ "mp", 0xfc, INSTR_SS_LLRDRD },
	{ "dp", 0xfd, INSTR_SS_LLRDRD },
	{ "", 0, INSTR_INVALID }
};

static struct insn opcode_01[] = {
#ifdef CONFIG_64BIT
	{ "sam64", 0x0e, INSTR_E },
#endif
	{ "pr", 0x01, INSTR_E },
	{ "upt", 0x02, INSTR_E },
	{ "sckpf", 0x07, INSTR_E },
	{ "tam", 0x0b, INSTR_E },
	{ "sam24", 0x0c, INSTR_E },
	{ "sam31", 0x0d, INSTR_E },
	{ "trap2", 0xff, INSTR_E },
	{ "", 0, INSTR_INVALID }
};

static struct insn opcode_a5[] = {
#ifdef CONFIG_64BIT
	{ "iihh", 0x00, INSTR_RI_RU },
	{ "iihl", 0x01, INSTR_RI_RU },
	{ "iilh", 0x02, INSTR_RI_RU },
	{ "iill", 0x03, INSTR_RI_RU },
	{ "nihh", 0x04, INSTR_RI_RU },
	{ "nihl", 0x05, INSTR_RI_RU },
	{ "nilh", 0x06, INSTR_RI_RU },
	{ "nill", 0x07, INSTR_RI_RU },
	{ "oihh", 0x08, INSTR_RI_RU },
	{ "oihl", 0x09, INSTR_RI_RU },
	{ "oilh", 0x0a, INSTR_RI_RU },
	{ "oill", 0x0b, INSTR_RI_RU },
	{ "llihh", 0x0c, INSTR_RI_RU },
	{ "llihl", 0x0d, INSTR_RI_RU },
	{ "llilh", 0x0e, INSTR_RI_RU },
	{ "llill", 0x0f, INSTR_RI_RU },
#endif
	{ "", 0, INSTR_INVALID }
};

static struct insn opcode_a7[] = {
#ifdef CONFIG_64BIT
	{ "tmhh", 0x02, INSTR_RI_RU },
	{ "tmhl", 0x03, INSTR_RI_RU },
	{ "brctg", 0x07, INSTR_RI_RP },
	{ "lghi", 0x09, INSTR_RI_RI },
	{ "aghi", 0x0b, INSTR_RI_RI },
	{ "mghi", 0x0d, INSTR_RI_RI },
	{ "cghi", 0x0f, INSTR_RI_RI },
#endif
	{ "tmlh", 0x00, INSTR_RI_RU },
	{ "tmll", 0x01, INSTR_RI_RU },
	{ "brc", 0x04, INSTR_RI_UP },
	{ "bras", 0x05, INSTR_RI_RP },
	{ "brct", 0x06, INSTR_RI_RP },
	{ "lhi", 0x08, INSTR_RI_RI },
	{ "ahi", 0x0a, INSTR_RI_RI },
	{ "mhi", 0x0c, INSTR_RI_RI },
	{ "chi", 0x0e, INSTR_RI_RI },
	{ "", 0, INSTR_INVALID }
};

static struct insn opcode_b2[] = {
#ifdef CONFIG_64BIT
	{ "sske", 0x2b, INSTR_RRF_M0RR },
	{ "stckf", 0x7c, INSTR_S_RD },
	{ "cu21", 0xa6, INSTR_RRF_M0RR },
	{ "cuutf", 0xa6, INSTR_RRF_M0RR },
	{ "cu12", 0xa7, INSTR_RRF_M0RR },
	{ "cutfu", 0xa7, INSTR_RRF_M0RR },
	{ "stfle", 0xb0, INSTR_S_RD },
	{ "lpswe", 0xb2, INSTR_S_RD },
#endif
	{ "stidp", 0x02, INSTR_S_RD },
	{ "sck", 0x04, INSTR_S_RD },
	{ "stck", 0x05, INSTR_S_RD },
	{ "sckc", 0x06, INSTR_S_RD },
	{ "stckc", 0x07, INSTR_S_RD },
	{ "spt", 0x08, INSTR_S_RD },
	{ "stpt", 0x09, INSTR_S_RD },
	{ "spka", 0x0a, INSTR_S_RD },
	{ "ipk", 0x0b, INSTR_S_00 },
	{ "ptlb", 0x0d, INSTR_S_00 },
	{ "spx", 0x10, INSTR_S_RD },
	{ "stpx", 0x11, INSTR_S_RD },
	{ "stap", 0x12, INSTR_S_RD },
	{ "sie", 0x14, INSTR_S_RD },
	{ "pc", 0x18, INSTR_S_RD },
	{ "sac", 0x19, INSTR_S_RD },
	{ "cfc", 0x1a, INSTR_S_RD },
	{ "ipte", 0x21, INSTR_RRE_RR },
	{ "ipm", 0x22, INSTR_RRE_R0 },
	{ "ivsk", 0x23, INSTR_RRE_RR },
	{ "iac", 0x24, INSTR_RRE_R0 },
	{ "ssar", 0x25, INSTR_RRE_R0 },
	{ "epar", 0x26, INSTR_RRE_R0 },
	{ "esar", 0x27, INSTR_RRE_R0 },
	{ "pt", 0x28, INSTR_RRE_RR },
	{ "iske", 0x29, INSTR_RRE_RR },
	{ "rrbe", 0x2a, INSTR_RRE_RR },
	{ "sske", 0x2b, INSTR_RRE_RR },
	{ "tb", 0x2c, INSTR_RRE_0R },
	{ "dxr", 0x2d, INSTR_RRE_F0 },
	{ "pgin", 0x2e, INSTR_RRE_RR },
	{ "pgout", 0x2f, INSTR_RRE_RR },
	{ "csch", 0x30, INSTR_S_00 },
	{ "hsch", 0x31, INSTR_S_00 },
	{ "msch", 0x32, INSTR_S_RD },
	{ "ssch", 0x33, INSTR_S_RD },
	{ "stsch", 0x34, INSTR_S_RD },
	{ "tsch", 0x35, INSTR_S_RD },
	{ "tpi", 0x36, INSTR_S_RD },
	{ "sal", 0x37, INSTR_S_00 },
	{ "rsch", 0x38, INSTR_S_00 },
	{ "stcrw", 0x39, INSTR_S_RD },
	{ "stcps", 0x3a, INSTR_S_RD },
	{ "rchp", 0x3b, INSTR_S_00 },
	{ "schm", 0x3c, INSTR_S_00 },
	{ "bakr", 0x40, INSTR_RRE_RR },
	{ "cksm", 0x41, INSTR_RRE_RR },
	{ "sqdr", 0x44, INSTR_RRE_F0 },
	{ "sqer", 0x45, INSTR_RRE_F0 },
	{ "stura", 0x46, INSTR_RRE_RR },
	{ "msta", 0x47, INSTR_RRE_R0 },
	{ "palb", 0x48, INSTR_RRE_00 },
	{ "ereg", 0x49, INSTR_RRE_RR },
	{ "esta", 0x4a, INSTR_RRE_RR },
	{ "lura", 0x4b, INSTR_RRE_RR },
	{ "tar", 0x4c, INSTR_RRE_AR },
	{ "cpya", 0x4d, INSTR_RRE_AA },
	{ "sar", 0x4e, INSTR_RRE_AR },
	{ "ear", 0x4f, INSTR_RRE_RA },
	{ "csp", 0x50, INSTR_RRE_RR },
	{ "msr", 0x52, INSTR_RRE_RR },
	{ "mvpg", 0x54, INSTR_RRE_RR },
	{ "mvst", 0x55, INSTR_RRE_RR },
	{ "cuse", 0x57, INSTR_RRE_RR },
	{ "bsg", 0x58, INSTR_RRE_RR },
	{ "bsa", 0x5a, INSTR_RRE_RR },
	{ "clst", 0x5d, INSTR_RRE_RR },
	{ "srst", 0x5e, INSTR_RRE_RR },
	{ "cmpsc", 0x63, INSTR_RRE_RR },
	{ "cmpsc", 0x63, INSTR_RRE_RR },
	{ "siga", 0x74, INSTR_S_RD },
	{ "xsch", 0x76, INSTR_S_00 },
	{ "rp", 0x77, INSTR_S_RD },
	{ "stcke", 0x78, INSTR_S_RD },
	{ "sacf", 0x79, INSTR_S_RD },
	{ "stsi", 0x7d, INSTR_S_RD },
	{ "srnm", 0x99, INSTR_S_RD },
	{ "stfpc", 0x9c, INSTR_S_RD },
	{ "lfpc", 0x9d, INSTR_S_RD },
	{ "tre", 0xa5, INSTR_RRE_RR },
	{ "cuutf", 0xa6, INSTR_RRE_RR },
	{ "cutfu", 0xa7, INSTR_RRE_RR },
	{ "stfl", 0xb1, INSTR_S_RD },
	{ "trap4", 0xff, INSTR_S_RD },
	{ "", 0, INSTR_INVALID }
};

static struct insn opcode_b3[] = {
#ifdef CONFIG_64BIT
	{ "maylr", 0x38, INSTR_RRF_F0FF },
	{ "mylr", 0x39, INSTR_RRF_F0FF },
	{ "mayr", 0x3a, INSTR_RRF_F0FF },
	{ "myr", 0x3b, INSTR_RRF_F0FF },
	{ "mayhr", 0x3c, INSTR_RRF_F0FF },
	{ "myhr", 0x3d, INSTR_RRF_F0FF },
	{ "cegbr", 0xa4, INSTR_RRE_RR },
	{ "cdgbr", 0xa5, INSTR_RRE_RR },
	{ "cxgbr", 0xa6, INSTR_RRE_RR },
	{ "cgebr", 0xa8, INSTR_RRF_U0RF },
	{ "cgdbr", 0xa9, INSTR_RRF_U0RF },
	{ "cgxbr", 0xaa, INSTR_RRF_U0RF },
	{ "cfer", 0xb8, INSTR_RRF_U0RF },
	{ "cfdr", 0xb9, INSTR_RRF_U0RF },
	{ "cfxr", 0xba, INSTR_RRF_U0RF },
	{ "cegr", 0xc4, INSTR_RRE_RR },
	{ "cdgr", 0xc5, INSTR_RRE_RR },
	{ "cxgr", 0xc6, INSTR_RRE_RR },
	{ "cger", 0xc8, INSTR_RRF_U0RF },
	{ "cgdr", 0xc9, INSTR_RRF_U0RF },
	{ "cgxr", 0xca, INSTR_RRF_U0RF },
#endif
	{ "lpebr", 0x00, INSTR_RRE_FF },
	{ "lnebr", 0x01, INSTR_RRE_FF },
	{ "ltebr", 0x02, INSTR_RRE_FF },
	{ "lcebr", 0x03, INSTR_RRE_FF },
	{ "ldebr", 0x04, INSTR_RRE_FF },
	{ "lxdbr", 0x05, INSTR_RRE_FF },
	{ "lxebr", 0x06, INSTR_RRE_FF },
	{ "mxdbr", 0x07, INSTR_RRE_FF },
	{ "kebr", 0x08, INSTR_RRE_FF },
	{ "cebr", 0x09, INSTR_RRE_FF },
	{ "aebr", 0x0a, INSTR_RRE_FF },
	{ "sebr", 0x0b, INSTR_RRE_FF },
	{ "mdebr", 0x0c, INSTR_RRE_FF },
	{ "debr", 0x0d, INSTR_RRE_FF },
	{ "maebr", 0x0e, INSTR_RRF_F0FF },
	{ "msebr", 0x0f, INSTR_RRF_F0FF },
	{ "lpdbr", 0x10, INSTR_RRE_FF },
	{ "lndbr", 0x11, INSTR_RRE_FF },
	{ "ltdbr", 0x12, INSTR_RRE_FF },
	{ "lcdbr", 0x13, INSTR_RRE_FF },
	{ "sqebr", 0x14, INSTR_RRE_FF },
	{ "sqdbr", 0x15, INSTR_RRE_FF },
	{ "sqxbr", 0x16, INSTR_RRE_FF },
	{ "meebr", 0x17, INSTR_RRE_FF },
	{ "kdbr", 0x18, INSTR_RRE_FF },
	{ "cdbr", 0x19, INSTR_RRE_FF },
	{ "adbr", 0x1a, INSTR_RRE_FF },
	{ "sdbr", 0x1b, INSTR_RRE_FF },
	{ "mdbr", 0x1c, INSTR_RRE_FF },
	{ "ddbr", 0x1d, INSTR_RRE_FF },
	{ "madbr", 0x1e, INSTR_RRF_F0FF },
	{ "msdbr", 0x1f, INSTR_RRF_F0FF },
	{ "lder", 0x24, INSTR_RRE_FF },
	{ "lxdr", 0x25, INSTR_RRE_FF },
	{ "lxer", 0x26, INSTR_RRE_FF },
	{ "maer", 0x2e, INSTR_RRF_F0FF },
	{ "mser", 0x2f, INSTR_RRF_F0FF },
	{ "sqxr", 0x36, INSTR_RRE_FF },
	{ "meer", 0x37, INSTR_RRE_FF },
	{ "madr", 0x3e, INSTR_RRF_F0FF },
	{ "msdr", 0x3f, INSTR_RRF_F0FF },
	{ "lpxbr", 0x40, INSTR_RRE_FF },
	{ "lnxbr", 0x41, INSTR_RRE_FF },
	{ "ltxbr", 0x42, INSTR_RRE_FF },
	{ "lcxbr", 0x43, INSTR_RRE_FF },
	{ "ledbr", 0x44, INSTR_RRE_FF },
	{ "ldxbr", 0x45, INSTR_RRE_FF },
	{ "lexbr", 0x46, INSTR_RRE_FF },
	{ "fixbr", 0x47, INSTR_RRF_U0FF },
	{ "kxbr", 0x48, INSTR_RRE_FF },
	{ "cxbr", 0x49, INSTR_RRE_FF },
	{ "axbr", 0x4a, INSTR_RRE_FF },
	{ "sxbr", 0x4b, INSTR_RRE_FF },
	{ "mxbr", 0x4c, INSTR_RRE_FF },
	{ "dxbr", 0x4d, INSTR_RRE_FF },
	{ "tbedr", 0x50, INSTR_RRF_U0FF },
	{ "tbdr", 0x51, INSTR_RRF_U0FF },
	{ "diebr", 0x53, INSTR_RRF_FUFF },
	{ "fiebr", 0x57, INSTR_RRF_U0FF },
	{ "thder", 0x58, INSTR_RRE_RR },
	{ "thdr", 0x59, INSTR_RRE_RR },
	{ "didbr", 0x5b, INSTR_RRF_FUFF },
	{ "fidbr", 0x5f, INSTR_RRF_U0FF },
	{ "lpxr", 0x60, INSTR_RRE_FF },
	{ "lnxr", 0x61, INSTR_RRE_FF },
	{ "ltxr", 0x62, INSTR_RRE_FF },
	{ "lcxr", 0x63, INSTR_RRE_FF },
	{ "lxr", 0x65, INSTR_RRE_RR },
	{ "lexr", 0x66, INSTR_RRE_FF },
	{ "fixr", 0x67, INSTR_RRF_U0FF },
	{ "cxr", 0x69, INSTR_RRE_FF },
	{ "lzer", 0x74, INSTR_RRE_R0 },
	{ "lzdr", 0x75, INSTR_RRE_R0 },
	{ "lzxr", 0x76, INSTR_RRE_R0 },
	{ "fier", 0x77, INSTR_RRF_U0FF },
	{ "fidr", 0x7f, INSTR_RRF_U0FF },
	{ "sfpc", 0x84, INSTR_RRE_RR_OPT },
	{ "efpc", 0x8c, INSTR_RRE_RR_OPT },
	{ "cefbr", 0x94, INSTR_RRE_RF },
	{ "cdfbr", 0x95, INSTR_RRE_RF },
	{ "cxfbr", 0x96, INSTR_RRE_RF },
	{ "cfebr", 0x98, INSTR_RRF_U0RF },
	{ "cfdbr", 0x99, INSTR_RRF_U0RF },
	{ "cfxbr", 0x9a, INSTR_RRF_U0RF },
	{ "cefr", 0xb4, INSTR_RRE_RF },
	{ "cdfr", 0xb5, INSTR_RRE_RF },
	{ "cxfr", 0xb6, INSTR_RRE_RF },
	{ "", 0, INSTR_INVALID }
};

static struct insn opcode_b9[] = {
#ifdef CONFIG_64BIT
	{ "lpgr", 0x00, INSTR_RRE_RR },
	{ "lngr", 0x01, INSTR_RRE_RR },
	{ "ltgr", 0x02, INSTR_RRE_RR },
	{ "lcgr", 0x03, INSTR_RRE_RR },
	{ "lgr", 0x04, INSTR_RRE_RR },
	{ "lurag", 0x05, INSTR_RRE_RR },
	{ "lgbr", 0x06, INSTR_RRE_RR },
	{ "lghr", 0x07, INSTR_RRE_RR },
	{ "agr", 0x08, INSTR_RRE_RR },
	{ "sgr", 0x09, INSTR_RRE_RR },
	{ "algr", 0x0a, INSTR_RRE_RR },
	{ "slgr", 0x0b, INSTR_RRE_RR },
	{ "msgr", 0x0c, INSTR_RRE_RR },
	{ "dsgr", 0x0d, INSTR_RRE_RR },
	{ "eregg", 0x0e, INSTR_RRE_RR },
	{ "lrvgr", 0x0f, INSTR_RRE_RR },
	{ "lpgfr", 0x10, INSTR_RRE_RR },
	{ "lngfr", 0x11, INSTR_RRE_RR },
	{ "ltgfr", 0x12, INSTR_RRE_RR },
	{ "lcgfr", 0x13, INSTR_RRE_RR },
	{ "lgfr", 0x14, INSTR_RRE_RR },
	{ "llgfr", 0x16, INSTR_RRE_RR },
	{ "llgtr", 0x17, INSTR_RRE_RR },
	{ "agfr", 0x18, INSTR_RRE_RR },
	{ "sgfr", 0x19, INSTR_RRE_RR },
	{ "algfr", 0x1a, INSTR_RRE_RR },
	{ "slgfr", 0x1b, INSTR_RRE_RR },
	{ "msgfr", 0x1c, INSTR_RRE_RR },
	{ "dsgfr", 0x1d, INSTR_RRE_RR },
	{ "cgr", 0x20, INSTR_RRE_RR },
	{ "clgr", 0x21, INSTR_RRE_RR },
	{ "sturg", 0x25, INSTR_RRE_RR },
	{ "lbr", 0x26, INSTR_RRE_RR },
	{ "lhr", 0x27, INSTR_RRE_RR },
	{ "cgfr", 0x30, INSTR_RRE_RR },
	{ "clgfr", 0x31, INSTR_RRE_RR },
	{ "bctgr", 0x46, INSTR_RRE_RR },
	{ "ngr", 0x80, INSTR_RRE_RR },
	{ "ogr", 0x81, INSTR_RRE_RR },
	{ "xgr", 0x82, INSTR_RRE_RR },
	{ "flogr", 0x83, INSTR_RRE_RR },
	{ "llgcr", 0x84, INSTR_RRE_RR },
	{ "llghr", 0x85, INSTR_RRE_RR },
	{ "mlgr", 0x86, INSTR_RRE_RR },
	{ "dlgr", 0x87, INSTR_RRE_RR },
	{ "alcgr", 0x88, INSTR_RRE_RR },
	{ "slbgr", 0x89, INSTR_RRE_RR },
	{ "cspg", 0x8a, INSTR_RRE_RR },
	{ "idte", 0x8e, INSTR_RRF_R0RR },
	{ "llcr", 0x94, INSTR_RRE_RR },
	{ "llhr", 0x95, INSTR_RRE_RR },
	{ "esea", 0x9d, INSTR_RRE_R0 },
	{ "lptea", 0xaa, INSTR_RRF_RURR },
	{ "cu14", 0xb0, INSTR_RRF_M0RR },
	{ "cu24", 0xb1, INSTR_RRF_M0RR },
	{ "cu41", 0xb2, INSTR_RRF_M0RR },
	{ "cu42", 0xb3, INSTR_RRF_M0RR },
#endif
	{ "kmac", 0x1e, INSTR_RRE_RR },
	{ "lrvr", 0x1f, INSTR_RRE_RR },
	{ "km", 0x2e, INSTR_RRE_RR },
	{ "kmc", 0x2f, INSTR_RRE_RR },
	{ "kimd", 0x3e, INSTR_RRE_RR },
	{ "klmd", 0x3f, INSTR_RRE_RR },
	{ "epsw", 0x8d, INSTR_RRE_RR },
	{ "trtt", 0x90, INSTR_RRE_RR },
	{ "trtt", 0x90, INSTR_RRF_M0RR },
	{ "trto", 0x91, INSTR_RRE_RR },
	{ "trto", 0x91, INSTR_RRF_M0RR },
	{ "trot", 0x92, INSTR_RRE_RR },
	{ "trot", 0x92, INSTR_RRF_M0RR },
	{ "troo", 0x93, INSTR_RRE_RR },
	{ "troo", 0x93, INSTR_RRF_M0RR },
	{ "mlr", 0x96, INSTR_RRE_RR },
	{ "dlr", 0x97, INSTR_RRE_RR },
	{ "alcr", 0x98, INSTR_RRE_RR },
	{ "slbr", 0x99, INSTR_RRE_RR },
	{ "", 0, INSTR_INVALID }
};

static struct insn opcode_c0[] = {
#ifdef CONFIG_64BIT
	{ "lgfi", 0x01, INSTR_RIL_RI },
	{ "xihf", 0x06, INSTR_RIL_RU },
	{ "xilf", 0x07, INSTR_RIL_RU },
	{ "iihf", 0x08, INSTR_RIL_RU },
	{ "iilf", 0x09, INSTR_RIL_RU },
	{ "nihf", 0x0a, INSTR_RIL_RU },
	{ "nilf", 0x0b, INSTR_RIL_RU },
	{ "oihf", 0x0c, INSTR_RIL_RU },
	{ "oilf", 0x0d, INSTR_RIL_RU },
	{ "llihf", 0x0e, INSTR_RIL_RU },
	{ "llilf", 0x0f, INSTR_RIL_RU },
#endif
	{ "larl", 0x00, INSTR_RIL_RP },
	{ "brcl", 0x04, INSTR_RIL_UP },
	{ "brasl", 0x05, INSTR_RIL_RP },
	{ "", 0, INSTR_INVALID }
};

static struct insn opcode_c2[] = {
#ifdef CONFIG_64BIT
	{ "slgfi", 0x04, INSTR_RIL_RU },
	{ "slfi", 0x05, INSTR_RIL_RU },
	{ "agfi", 0x08, INSTR_RIL_RI },
	{ "afi", 0x09, INSTR_RIL_RI },
	{ "algfi", 0x0a, INSTR_RIL_RU },
	{ "alfi", 0x0b, INSTR_RIL_RU },
	{ "cgfi", 0x0c, INSTR_RIL_RI },
	{ "cfi", 0x0d, INSTR_RIL_RI },
	{ "clgfi", 0x0e, INSTR_RIL_RU },
	{ "clfi", 0x0f, INSTR_RIL_RU },
#endif
	{ "", 0, INSTR_INVALID }
};

static struct insn opcode_c8[] = {
#ifdef CONFIG_64BIT
	{ "mvcos", 0x00, INSTR_SSF_RRDRD },
#endif
	{ "", 0, INSTR_INVALID }
};

static struct insn opcode_e3[] = {
#ifdef CONFIG_64BIT
	{ "ltg", 0x02, INSTR_RXY_RRRD },
	{ "lrag", 0x03, INSTR_RXY_RRRD },
	{ "lg", 0x04, INSTR_RXY_RRRD },
	{ "cvby", 0x06, INSTR_RXY_RRRD },
	{ "ag", 0x08, INSTR_RXY_RRRD },
	{ "sg", 0x09, INSTR_RXY_RRRD },
	{ "alg", 0x0a, INSTR_RXY_RRRD },
	{ "slg", 0x0b, INSTR_RXY_RRRD },
	{ "msg", 0x0c, INSTR_RXY_RRRD },
	{ "dsg", 0x0d, INSTR_RXY_RRRD },
	{ "cvbg", 0x0e, INSTR_RXY_RRRD },
	{ "lrvg", 0x0f, INSTR_RXY_RRRD },
	{ "lt", 0x12, INSTR_RXY_RRRD },
	{ "lray", 0x13, INSTR_RXY_RRRD },
	{ "lgf", 0x14, INSTR_RXY_RRRD },
	{ "lgh", 0x15, INSTR_RXY_RRRD },
	{ "llgf", 0x16, INSTR_RXY_RRRD },
	{ "llgt", 0x17, INSTR_RXY_RRRD },
	{ "agf", 0x18, INSTR_RXY_RRRD },
	{ "sgf", 0x19, INSTR_RXY_RRRD },
	{ "algf", 0x1a, INSTR_RXY_RRRD },
	{ "slgf", 0x1b, INSTR_RXY_RRRD },
	{ "msgf", 0x1c, INSTR_RXY_RRRD },
	{ "dsgf", 0x1d, INSTR_RXY_RRRD },
	{ "cg", 0x20, INSTR_RXY_RRRD },
	{ "clg", 0x21, INSTR_RXY_RRRD },
	{ "stg", 0x24, INSTR_RXY_RRRD },
	{ "cvdy", 0x26, INSTR_RXY_RRRD },
	{ "cvdg", 0x2e, INSTR_RXY_RRRD },
	{ "strvg", 0x2f, INSTR_RXY_RRRD },
	{ "cgf", 0x30, INSTR_RXY_RRRD },
	{ "clgf", 0x31, INSTR_RXY_RRRD },
	{ "strvh", 0x3f, INSTR_RXY_RRRD },
	{ "bctg", 0x46, INSTR_RXY_RRRD },
	{ "sty", 0x50, INSTR_RXY_RRRD },
	{ "msy", 0x51, INSTR_RXY_RRRD },
	{ "ny", 0x54, INSTR_RXY_RRRD },
	{ "cly", 0x55, INSTR_RXY_RRRD },
	{ "oy", 0x56, INSTR_RXY_RRRD },
	{ "xy", 0x57, INSTR_RXY_RRRD },
	{ "ly", 0x58, INSTR_RXY_RRRD },
	{ "cy", 0x59, INSTR_RXY_RRRD },
	{ "ay", 0x5a, INSTR_RXY_RRRD },
	{ "sy", 0x5b, INSTR_RXY_RRRD },
	{ "aly", 0x5e, INSTR_RXY_RRRD },
	{ "sly", 0x5f, INSTR_RXY_RRRD },
	{ "sthy", 0x70, INSTR_RXY_RRRD },
	{ "lay", 0x71, INSTR_RXY_RRRD },
	{ "stcy", 0x72, INSTR_RXY_RRRD },
	{ "icy", 0x73, INSTR_RXY_RRRD },
	{ "lb", 0x76, INSTR_RXY_RRRD },
	{ "lgb", 0x77, INSTR_RXY_RRRD },
	{ "lhy", 0x78, INSTR_RXY_RRRD },
	{ "chy", 0x79, INSTR_RXY_RRRD },
	{ "ahy", 0x7a, INSTR_RXY_RRRD },
	{ "shy", 0x7b, INSTR_RXY_RRRD },
	{ "ng", 0x80, INSTR_RXY_RRRD },
	{ "og", 0x81, INSTR_RXY_RRRD },
	{ "xg", 0x82, INSTR_RXY_RRRD },
	{ "mlg", 0x86, INSTR_RXY_RRRD },
	{ "dlg", 0x87, INSTR_RXY_RRRD },
	{ "alcg", 0x88, INSTR_RXY_RRRD },
	{ "slbg", 0x89, INSTR_RXY_RRRD },
	{ "stpq", 0x8e, INSTR_RXY_RRRD },
	{ "lpq", 0x8f, INSTR_RXY_RRRD },
	{ "llgc", 0x90, INSTR_RXY_RRRD },
	{ "llgh", 0x91, INSTR_RXY_RRRD },
	{ "llc", 0x94, INSTR_RXY_RRRD },
	{ "llh", 0x95, INSTR_RXY_RRRD },
#endif
	{ "lrv", 0x1e, INSTR_RXY_RRRD },
	{ "lrvh", 0x1f, INSTR_RXY_RRRD },
	{ "strv", 0x3e, INSTR_RXY_RRRD },
	{ "ml", 0x96, INSTR_RXY_RRRD },
	{ "dl", 0x97, INSTR_RXY_RRRD },
	{ "alc", 0x98, INSTR_RXY_RRRD },
	{ "slb", 0x99, INSTR_RXY_RRRD },
	{ "", 0, INSTR_INVALID }
};

static struct insn opcode_e5[] = {
#ifdef CONFIG_64BIT
	{ "strag", 0x02, INSTR_SSE_RDRD },
#endif
	{ "lasp", 0x00, INSTR_SSE_RDRD },
	{ "tprot", 0x01, INSTR_SSE_RDRD },
	{ "mvcsk", 0x0e, INSTR_SSE_RDRD },
	{ "mvcdk", 0x0f, INSTR_SSE_RDRD },
	{ "", 0, INSTR_INVALID }
};

static struct insn opcode_eb[] = {
#ifdef CONFIG_64BIT
	{ "lmg", 0x04, INSTR_RSY_RRRD },
	{ "srag", 0x0a, INSTR_RSY_RRRD },
	{ "slag", 0x0b, INSTR_RSY_RRRD },
	{ "srlg", 0x0c, INSTR_RSY_RRRD },
	{ "sllg", 0x0d, INSTR_RSY_RRRD },
	{ "tracg", 0x0f, INSTR_RSY_RRRD },
	{ "csy", 0x14, INSTR_RSY_RRRD },
	{ "rllg", 0x1c, INSTR_RSY_RRRD },
	{ "clmh", 0x20, INSTR_RSY_RURD },
	{ "clmy", 0x21, INSTR_RSY_RURD },
	{ "stmg", 0x24, INSTR_RSY_RRRD },
	{ "stctg", 0x25, INSTR_RSY_CCRD },
	{ "stmh", 0x26, INSTR_RSY_RRRD },
	{ "stcmh", 0x2c, INSTR_RSY_RURD },
	{ "stcmy", 0x2d, INSTR_RSY_RURD },
	{ "lctlg", 0x2f, INSTR_RSY_CCRD },
	{ "csg", 0x30, INSTR_RSY_RRRD },
	{ "cdsy", 0x31, INSTR_RSY_RRRD },
	{ "cdsg", 0x3e, INSTR_RSY_RRRD },
	{ "bxhg", 0x44, INSTR_RSY_RRRD },
	{ "bxleg", 0x45, INSTR_RSY_RRRD },
	{ "tmy", 0x51, INSTR_SIY_URD },
	{ "mviy", 0x52, INSTR_SIY_URD },
	{ "niy", 0x54, INSTR_SIY_URD },
	{ "cliy", 0x55, INSTR_SIY_URD },
	{ "oiy", 0x56, INSTR_SIY_URD },
	{ "xiy", 0x57, INSTR_SIY_URD },
	{ "icmh", 0x80, INSTR_RSE_RURD },
	{ "icmh", 0x80, INSTR_RSY_RURD },
	{ "icmy", 0x81, INSTR_RSY_RURD },
	{ "clclu", 0x8f, INSTR_RSY_RRRD },
	{ "stmy", 0x90, INSTR_RSY_RRRD },
	{ "lmh", 0x96, INSTR_RSY_RRRD },
	{ "lmy", 0x98, INSTR_RSY_RRRD },
	{ "lamy", 0x9a, INSTR_RSY_AARD },
	{ "stamy", 0x9b, INSTR_RSY_AARD },
#endif
	{ "rll", 0x1d, INSTR_RSY_RRRD },
	{ "mvclu", 0x8e, INSTR_RSY_RRRD },
	{ "tp", 0xc0, INSTR_RSL_R0RD },
	{ "", 0, INSTR_INVALID }
};

static struct insn opcode_ec[] = {
#ifdef CONFIG_64BIT
	{ "brxhg", 0x44, INSTR_RIE_RRP },
	{ "brxlg", 0x45, INSTR_RIE_RRP },
#endif
	{ "", 0, INSTR_INVALID }
};

static struct insn opcode_ed[] = {
#ifdef CONFIG_64BIT
	{ "mayl", 0x38, INSTR_RXF_FRRDF },
	{ "myl", 0x39, INSTR_RXF_FRRDF },
	{ "may", 0x3a, INSTR_RXF_FRRDF },
	{ "my", 0x3b, INSTR_RXF_FRRDF },
	{ "mayh", 0x3c, INSTR_RXF_FRRDF },
	{ "myh", 0x3d, INSTR_RXF_FRRDF },
	{ "ley", 0x64, INSTR_RXY_FRRD },
	{ "ldy", 0x65, INSTR_RXY_FRRD },
	{ "stey", 0x66, INSTR_RXY_FRRD },
	{ "stdy", 0x67, INSTR_RXY_FRRD },
#endif
	{ "ldeb", 0x04, INSTR_RXE_FRRD },
	{ "lxdb", 0x05, INSTR_RXE_FRRD },
	{ "lxeb", 0x06, INSTR_RXE_FRRD },
	{ "mxdb", 0x07, INSTR_RXE_FRRD },
	{ "keb", 0x08, INSTR_RXE_FRRD },
	{ "ceb", 0x09, INSTR_RXE_FRRD },
	{ "aeb", 0x0a, INSTR_RXE_FRRD },
	{ "seb", 0x0b, INSTR_RXE_FRRD },
	{ "mdeb", 0x0c, INSTR_RXE_FRRD },
	{ "deb", 0x0d, INSTR_RXE_FRRD },
	{ "maeb", 0x0e, INSTR_RXF_FRRDF },
	{ "mseb", 0x0f, INSTR_RXF_FRRDF },
	{ "tceb", 0x10, INSTR_RXE_FRRD },
	{ "tcdb", 0x11, INSTR_RXE_FRRD },
	{ "tcxb", 0x12, INSTR_RXE_FRRD },
	{ "sqeb", 0x14, INSTR_RXE_FRRD },
	{ "sqdb", 0x15, INSTR_RXE_FRRD },
	{ "meeb", 0x17, INSTR_RXE_FRRD },
	{ "kdb", 0x18, INSTR_RXE_FRRD },
	{ "cdb", 0x19, INSTR_RXE_FRRD },
	{ "adb", 0x1a, INSTR_RXE_FRRD },
	{ "sdb", 0x1b, INSTR_RXE_FRRD },
	{ "mdb", 0x1c, INSTR_RXE_FRRD },
	{ "ddb", 0x1d, INSTR_RXE_FRRD },
	{ "madb", 0x1e, INSTR_RXF_FRRDF },
	{ "msdb", 0x1f, INSTR_RXF_FRRDF },
	{ "lde", 0x24, INSTR_RXE_FRRD },
	{ "lxd", 0x25, INSTR_RXE_FRRD },
	{ "lxe", 0x26, INSTR_RXE_FRRD },
	{ "mae", 0x2e, INSTR_RXF_FRRDF },
	{ "mse", 0x2f, INSTR_RXF_FRRDF },
	{ "sqe", 0x34, INSTR_RXE_FRRD },
	{ "mee", 0x37, INSTR_RXE_FRRD },
	{ "mad", 0x3e, INSTR_RXF_FRRDF },
	{ "msd", 0x3f, INSTR_RXF_FRRDF },
	{ "", 0, INSTR_INVALID }
};

/* Extracts an operand value from an instruction.  */
static unsigned int extract_operand(unsigned char *code,
				    const struct operand *operand)
{
	unsigned int val;
	int bits;

	/* Extract fragments of the operand byte for byte.  */
	code += operand->shift / 8;
	bits = (operand->shift & 7) + operand->bits;
	val = 0;
	do {
		val <<= 8;
		val |= (unsigned int) *code++;
		bits -= 8;
	} while (bits > 0);
	val >>= -bits;
	val &= ((1U << (operand->bits - 1)) << 1) - 1;

	/* Check for special long displacement case.  */
	if (operand->bits == 20 && operand->shift == 20)
		val = (val & 0xff) << 12 | (val & 0xfff00) >> 8;

	/* Sign extend value if the operand is signed or pc relative.  */
	if ((operand->flags & (OPERAND_SIGNED | OPERAND_PCREL)) &&
	    (val & (1U << (operand->bits - 1))))
		val |= (-1U << (operand->bits - 1)) << 1;

	/* Double value if the operand is pc relative.	*/
	if (operand->flags & OPERAND_PCREL)
		val <<= 1;

	/* Length x in an instructions has real length x + 1.  */
	if (operand->flags & OPERAND_LENGTH)
		val++;
	return val;
}

static inline int insn_length(unsigned char code)
{
	return ((((int) code + 64) >> 7) + 1) << 1;
}

static struct insn *find_insn(unsigned char *code)
{
	unsigned char opfrag = code[1];
	unsigned char opmask;
	struct insn *table;

	switch (code[0]) {
	case 0x01:
		table = opcode_01;
		break;
	case 0xa5:
		table = opcode_a5;
		break;
	case 0xa7:
		table = opcode_a7;
		break;
	case 0xb2:
		table = opcode_b2;
		break;
	case 0xb3:
		table = opcode_b3;
		break;
	case 0xb9:
		table = opcode_b9;
		break;
	case 0xc0:
		table = opcode_c0;
		break;
	case 0xc2:
		table = opcode_c2;
		break;
	case 0xc8:
		table = opcode_c8;
		break;
	case 0xe3:
		table = opcode_e3;
		opfrag = code[5];
		break;
	case 0xe5:
		table = opcode_e5;
		break;
	case 0xeb:
		table = opcode_eb;
		opfrag = code[5];
		break;
	case 0xec:
		table = opcode_ec;
		opfrag = code[5];
		break;
	case 0xed:
		table = opcode_ed;
		opfrag = code[5];
		break;
	default:
		table = opcode;
		opfrag = code[0];
		break;
	}
	while (table->format != INSTR_INVALID) {
		opmask = formats[table->format][0];
		if (table->opfrag == (opfrag & opmask))
			return table;
		table++;
	}
	return NULL;
}

static int print_insn(char *buffer, unsigned char *code, unsigned long addr)
{
	struct insn *insn;
	const unsigned char *ops;
	const struct operand *operand;
	unsigned int value;
	char separator;
	char *ptr;
	int i;

	ptr = buffer;
	insn = find_insn(code);
	if (insn) {
		ptr += sprintf(ptr, "%.5s\t", insn->name);
		/* Extract the operands. */
		separator = 0;
		for (ops = formats[insn->format] + 1, i = 0;
		     *ops != 0 && i < 6; ops++, i++) {
			operand = operands + *ops;
			value = extract_operand(code, operand);
			if ((operand->flags & OPERAND_INDEX)  && value == 0)
				continue;
			if ((operand->flags & OPERAND_BASE) &&
			    value == 0 && separator == '(') {
				separator = ',';
				continue;
			}
			if (separator)
				ptr += sprintf(ptr, "%c", separator);
			if (operand->flags & OPERAND_GPR)
				ptr += sprintf(ptr, "%%r%i", value);
			else if (operand->flags & OPERAND_FPR)
				ptr += sprintf(ptr, "%%f%i", value);
			else if (operand->flags & OPERAND_AR)
				ptr += sprintf(ptr, "%%a%i", value);
			else if (operand->flags & OPERAND_CR)
				ptr += sprintf(ptr, "%%c%i", value);
			else if (operand->flags & OPERAND_PCREL)
				ptr += sprintf(ptr, "%lx", (signed int) value
								      + addr);
			else if (operand->flags & OPERAND_SIGNED)
				ptr += sprintf(ptr, "%i", value);
			else
				ptr += sprintf(ptr, "%u", value);
			if (operand->flags & OPERAND_DISP)
				separator = '(';
			else if (operand->flags & OPERAND_BASE) {
				ptr += sprintf(ptr, ")");
				separator = ',';
			} else
				separator = ',';
		}
	} else
		ptr += sprintf(ptr, "unknown");
	return (int) (ptr - buffer);
}

void show_code(struct pt_regs *regs)
{
	char *mode = (regs->psw.mask & PSW_MASK_PSTATE) ? "User" : "Krnl";
	unsigned char code[64];
	char buffer[64], *ptr;
	mm_segment_t old_fs;
	unsigned long addr;
	int start, end, opsize, hops, i;

	/* Get a snapshot of the 64 bytes surrounding the fault address. */
	old_fs = get_fs();
	set_fs((regs->psw.mask & PSW_MASK_PSTATE) ? USER_DS : KERNEL_DS);
	for (start = 32; start && regs->psw.addr >= 34 - start; start -= 2) {
		addr = regs->psw.addr - 34 + start;
		if (__copy_from_user(code + start - 2,
				     (char __user *) addr, 2))
			break;
	}
	for (end = 32; end < 64; end += 2) {
		addr = regs->psw.addr + end - 32;
		if (__copy_from_user(code + end,
				     (char __user *) addr, 2))
			break;
	}
	set_fs(old_fs);
	/* Code snapshot useable ? */
	if ((regs->psw.addr & 1) || start >= end) {
		printk("%s Code: Bad PSW.\n", mode);
		return;
	}
	/* Find a starting point for the disassembly. */
	while (start < 32) {
		for (i = 0, hops = 0; start + i < 32 && hops < 3; hops++) {
			if (!find_insn(code + start + i))
				break;
			i += insn_length(code[start + i]);
		}
		if (start + i == 32)
			/* Looks good, sequence ends at PSW. */
			break;
		start += 2;
	}
	/* Decode the instructions. */
	ptr = buffer;
	ptr += sprintf(ptr, "%s Code:", mode);
	hops = 0;
	while (start < end && hops < 8) {
		*ptr++ = (start == 32) ? '>' : ' ';
		addr = regs->psw.addr + start - 32;
		ptr += sprintf(ptr, ONELONG, addr);
		opsize = insn_length(code[start]);
		if (start + opsize >= end)
			break;
		for (i = 0; i < opsize; i++)
			ptr += sprintf(ptr, "%02x", code[start + i]);
		*ptr++ = '\t';
		if (i < 6)
			*ptr++ = '\t';
		ptr += print_insn(ptr, code + start, addr);
		start += opsize;
		printk(buffer);
		ptr = buffer;
		ptr += sprintf(ptr, "\n          ");
		hops++;
	}
	printk("\n");
}
