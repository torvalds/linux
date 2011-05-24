#ifndef _ASM_X86_NOPS_H
#define _ASM_X86_NOPS_H

/*
 * Define nops for use with alternative() and for tracing.
 *
 * *_NOP5_ATOMIC must be a single instruction.
 */

#define NOP_DS_PREFIX 0x3e

/* generic versions from gas
   1: nop
   the following instructions are NOT nops in 64-bit mode,
   for 64-bit mode use K8 or P6 nops instead
   2: movl %esi,%esi
   3: leal 0x00(%esi),%esi
   4: leal 0x00(,%esi,1),%esi
   6: leal 0x00000000(%esi),%esi
   7: leal 0x00000000(,%esi,1),%esi
*/
#define GENERIC_NOP1 0x90
#define GENERIC_NOP2 0x89,0xf6
#define GENERIC_NOP3 0x8d,0x76,0x00
#define GENERIC_NOP4 0x8d,0x74,0x26,0x00
#define GENERIC_NOP5 GENERIC_NOP1,GENERIC_NOP4
#define GENERIC_NOP6 0x8d,0xb6,0x00,0x00,0x00,0x00
#define GENERIC_NOP7 0x8d,0xb4,0x26,0x00,0x00,0x00,0x00
#define GENERIC_NOP8 GENERIC_NOP1,GENERIC_NOP7
#define GENERIC_NOP5_ATOMIC NOP_DS_PREFIX,GENERIC_NOP4

/* Opteron 64bit nops
   1: nop
   2: osp nop
   3: osp osp nop
   4: osp osp osp nop
*/
#define K8_NOP1 GENERIC_NOP1
#define K8_NOP2	0x66,K8_NOP1
#define K8_NOP3	0x66,K8_NOP2
#define K8_NOP4	0x66,K8_NOP3
#define K8_NOP5	K8_NOP3,K8_NOP2
#define K8_NOP6	K8_NOP3,K8_NOP3
#define K8_NOP7	K8_NOP4,K8_NOP3
#define K8_NOP8	K8_NOP4,K8_NOP4
#define K8_NOP5_ATOMIC 0x66,K8_NOP4

/* K7 nops
   uses eax dependencies (arbitrary choice)
   1: nop
   2: movl %eax,%eax
   3: leal (,%eax,1),%eax
   4: leal 0x00(,%eax,1),%eax
   6: leal 0x00000000(%eax),%eax
   7: leal 0x00000000(,%eax,1),%eax
*/
#define K7_NOP1	GENERIC_NOP1
#define K7_NOP2	0x8b,0xc0
#define K7_NOP3	0x8d,0x04,0x20
#define K7_NOP4	0x8d,0x44,0x20,0x00
#define K7_NOP5	K7_NOP4,K7_NOP1
#define K7_NOP6	0x8d,0x80,0,0,0,0
#define K7_NOP7	0x8D,0x04,0x05,0,0,0,0
#define K7_NOP8	K7_NOP7,K7_NOP1
#define K7_NOP5_ATOMIC NOP_DS_PREFIX,K7_NOP4

/* P6 nops
   uses eax dependencies (Intel-recommended choice)
   1: nop
   2: osp nop
   3: nopl (%eax)
   4: nopl 0x00(%eax)
   5: nopl 0x00(%eax,%eax,1)
   6: osp nopl 0x00(%eax,%eax,1)
   7: nopl 0x00000000(%eax)
   8: nopl 0x00000000(%eax,%eax,1)
   Note: All the above are assumed to be a single instruction.
	There is kernel code that depends on this.
*/
#define P6_NOP1	GENERIC_NOP1
#define P6_NOP2	0x66,0x90
#define P6_NOP3	0x0f,0x1f,0x00
#define P6_NOP4	0x0f,0x1f,0x40,0
#define P6_NOP5	0x0f,0x1f,0x44,0x00,0
#define P6_NOP6	0x66,0x0f,0x1f,0x44,0x00,0
#define P6_NOP7	0x0f,0x1f,0x80,0,0,0,0
#define P6_NOP8	0x0f,0x1f,0x84,0x00,0,0,0,0
#define P6_NOP5_ATOMIC P6_NOP5

#define _ASM_MK_NOP(x) ".byte " __stringify(x) "\n"

#if defined(CONFIG_MK7)
#define ASM_NOP1 _ASM_MK_NOP(K7_NOP1)
#define ASM_NOP2 _ASM_MK_NOP(K7_NOP2)
#define ASM_NOP3 _ASM_MK_NOP(K7_NOP3)
#define ASM_NOP4 _ASM_MK_NOP(K7_NOP4)
#define ASM_NOP5 _ASM_MK_NOP(K7_NOP5)
#define ASM_NOP6 _ASM_MK_NOP(K7_NOP6)
#define ASM_NOP7 _ASM_MK_NOP(K7_NOP7)
#define ASM_NOP8 _ASM_MK_NOP(K7_NOP8)
#define ASM_NOP5_ATOMIC _ASM_MK_NOP(K7_NOP5_ATOMIC)
#elif defined(CONFIG_X86_P6_NOP)
#define ASM_NOP1 _ASM_MK_NOP(P6_NOP1)
#define ASM_NOP2 _ASM_MK_NOP(P6_NOP2)
#define ASM_NOP3 _ASM_MK_NOP(P6_NOP3)
#define ASM_NOP4 _ASM_MK_NOP(P6_NOP4)
#define ASM_NOP5 _ASM_MK_NOP(P6_NOP5)
#define ASM_NOP6 _ASM_MK_NOP(P6_NOP6)
#define ASM_NOP7 _ASM_MK_NOP(P6_NOP7)
#define ASM_NOP8 _ASM_MK_NOP(P6_NOP8)
#define ASM_NOP5_ATOMIC _ASM_MK_NOP(P6_NOP5_ATOMIC)
#elif defined(CONFIG_X86_64)
#define ASM_NOP1 _ASM_MK_NOP(K8_NOP1)
#define ASM_NOP2 _ASM_MK_NOP(K8_NOP2)
#define ASM_NOP3 _ASM_MK_NOP(K8_NOP3)
#define ASM_NOP4 _ASM_MK_NOP(K8_NOP4)
#define ASM_NOP5 _ASM_MK_NOP(K8_NOP5)
#define ASM_NOP6 _ASM_MK_NOP(K8_NOP6)
#define ASM_NOP7 _ASM_MK_NOP(K8_NOP7)
#define ASM_NOP8 _ASM_MK_NOP(K8_NOP8)
#define ASM_NOP5_ATOMIC _ASM_MK_NOP(K8_NOP5_ATOMIC)
#else
#define ASM_NOP1 _ASM_MK_NOP(GENERIC_NOP1)
#define ASM_NOP2 _ASM_MK_NOP(GENERIC_NOP2)
#define ASM_NOP3 _ASM_MK_NOP(GENERIC_NOP3)
#define ASM_NOP4 _ASM_MK_NOP(GENERIC_NOP4)
#define ASM_NOP5 _ASM_MK_NOP(GENERIC_NOP5)
#define ASM_NOP6 _ASM_MK_NOP(GENERIC_NOP6)
#define ASM_NOP7 _ASM_MK_NOP(GENERIC_NOP7)
#define ASM_NOP8 _ASM_MK_NOP(GENERIC_NOP8)
#define ASM_NOP5_ATOMIC _ASM_MK_NOP(GENERIC_NOP5_ATOMIC)
#endif

#define ASM_NOP_MAX 8
#define NOP_ATOMIC5 (ASM_NOP_MAX+1)	/* Entry for the 5-byte atomic NOP */

#ifndef __ASSEMBLY__
extern const unsigned char * const *ideal_nops;
extern void arch_init_ideal_nops(void);
#endif

#endif /* _ASM_X86_NOPS_H */
