/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef __MIPS_UAPI_ASM_UCONTEXT_H
#define __MIPS_UAPI_ASM_UCONTEXT_H

/**
 * struct extcontext - extended context header structure
 * @magic:	magic value identifying the type of extended context
 * @size:	the size in bytes of the enclosing structure
 *
 * Extended context structures provide context which does not fit within struct
 * sigcontext. They are placed sequentially in memory at the end of struct
 * ucontext and struct sigframe, with each extended context structure beginning
 * with a header defined by this struct. The type of context represented is
 * indicated by the magic field. Userland may check each extended context
 * structure against magic values that it recognises. The size field allows any
 * unrecognised context to be skipped, allowing for future expansion. The end
 * of the extended context data is indicated by the magic value
 * END_EXTCONTEXT_MAGIC.
 */
struct extcontext {
	unsigned int		magic;
	unsigned int		size;
};

/**
 * struct msa_extcontext - MSA extended context structure
 * @ext:	the extended context header, with magic == MSA_EXTCONTEXT_MAGIC
 * @wr:		the most significant 64 bits of each MSA vector register
 * @csr:	the value of the MSA control & status register
 *
 * If MSA context is live for a task at the time a signal is delivered to it,
 * this structure will hold the MSA context of the task as it was prior to the
 * signal delivery.
 */
struct msa_extcontext {
	struct extcontext	ext;
#define MSA_EXTCONTEXT_MAGIC	0x784d5341	/* xMSA */

	unsigned long long	wr[32];
	unsigned int		csr;
};

#define END_EXTCONTEXT_MAGIC	0x78454e44	/* xEND */

/**
 * struct ucontext - user context structure
 * @uc_flags:
 * @uc_link:
 * @uc_stack:
 * @uc_mcontext:	holds basic processor state
 * @uc_sigmask:
 * @uc_extcontext:	holds extended processor state
 */
struct ucontext {
	/* Historic fields matching asm-generic */
	unsigned long		uc_flags;
	struct ucontext		*uc_link;
	stack_t			uc_stack;
	struct sigcontext	uc_mcontext;
	sigset_t		uc_sigmask;

	/* Extended context structures may follow ucontext */
	unsigned long long	uc_extcontext[];
};

#endif /* __MIPS_UAPI_ASM_UCONTEXT_H */
