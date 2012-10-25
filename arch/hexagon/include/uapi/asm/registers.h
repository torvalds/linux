/*
 * Register definitions for the Hexagon architecture
 */


#ifndef _ASM_REGISTERS_H
#define _ASM_REGISTERS_H

#define SP r29

#ifndef __ASSEMBLY__

/*  See kernel/entry.S for further documentation.  */

/*
 * Entry code copies the event record out of guest registers into
 * this structure (which is on the stack).
 */

struct hvm_event_record {
	unsigned long vmel;     /* Event Linkage (return address) */
	unsigned long vmest;    /* Event context - pre-event SSR values */
	unsigned long vmpsp;    /* Previous stack pointer */
	unsigned long vmbadva;  /* Bad virtual address for addressing events */
};

struct pt_regs {
	long restart_r0;        /* R0 checkpoint for syscall restart */
	long syscall_nr;        /* Only used in system calls */
	union {
		struct {
			unsigned long usr;
			unsigned long preds;
		};
		long long int predsusr;
	};
	union {
		struct {
			unsigned long m0;
			unsigned long m1;
		};
		long long int m1m0;
	};
	union {
		struct {
			unsigned long sa1;
			unsigned long lc1;
		};
		long long int lc1sa1;
	};
	union {
		struct {
			unsigned long sa0;
			unsigned long lc0;
		};
		long long int lc0sa0;
	};
	union {
		struct {
			unsigned long gp;
			unsigned long ugp;
		};
		long long int ugpgp;
	};
	/*
	* Be extremely careful with rearranging these, if at all.  Some code
	* assumes the 32 registers exist exactly like this in memory;
	* e.g. kernel/ptrace.c
	* e.g. kernel/signal.c (restore_sigcontext)
	*/
	union {
		struct {
			unsigned long r00;
			unsigned long r01;
		};
		long long int r0100;
	};
	union {
		struct {
			unsigned long r02;
			unsigned long r03;
		};
		long long int r0302;
	};
	union {
		struct {
			unsigned long r04;
			unsigned long r05;
		};
		long long int r0504;
	};
	union {
		struct {
			unsigned long r06;
			unsigned long r07;
		};
		long long int r0706;
	};
	union {
		struct {
			unsigned long r08;
			unsigned long r09;
		};
		long long int r0908;
	};
	union {
	       struct {
			unsigned long r10;
			unsigned long r11;
	       };
	       long long int r1110;
	};
	union {
	       struct {
			unsigned long r12;
			unsigned long r13;
	       };
	       long long int r1312;
	};
	union {
	       struct {
			unsigned long r14;
			unsigned long r15;
	       };
	       long long int r1514;
	};
	union {
		struct {
			unsigned long r16;
			unsigned long r17;
		};
		long long int r1716;
	};
	union {
		struct {
			unsigned long r18;
			unsigned long r19;
		};
		long long int r1918;
	};
	union {
		struct {
			unsigned long r20;
			unsigned long r21;
		};
		long long int r2120;
	};
	union {
		struct {
			unsigned long r22;
			unsigned long r23;
		};
		long long int r2322;
	};
	union {
		struct {
			unsigned long r24;
			unsigned long r25;
		};
		long long int r2524;
	};
	union {
		struct {
			unsigned long r26;
			unsigned long r27;
		};
		long long int r2726;
	};
	union {
		struct {
			unsigned long r28;
			unsigned long r29;
	       };
	       long long int r2928;
	};
	union {
		struct {
			unsigned long r30;
			unsigned long r31;
		};
		long long int r3130;
	};
	/* VM dispatch pushes event record onto stack - we can build on it */
	struct hvm_event_record hvmer;
};

/* Defines to conveniently access the values  */

/*
 * As of the VM spec 0.5, these registers are now set/retrieved via a
 * VM call.  On the in-bound side, we just fetch the values
 * at the entry points and stuff them into the old record in pt_regs.
 * However, on the outbound side, probably at VM rte, we set the
 * registers back.
 */

#define pt_elr(regs) ((regs)->hvmer.vmel)
#define pt_set_elr(regs, val) ((regs)->hvmer.vmel = (val))
#define pt_cause(regs) ((regs)->hvmer.vmest & (HVM_VMEST_CAUSE_MSK))
#define user_mode(regs) \
	(((regs)->hvmer.vmest & (HVM_VMEST_UM_MSK << HVM_VMEST_UM_SFT)) != 0)
#define ints_enabled(regs) \
	(((regs)->hvmer.vmest & (HVM_VMEST_IE_MSK << HVM_VMEST_IE_SFT)) != 0)
#define pt_psp(regs) ((regs)->hvmer.vmpsp)
#define pt_badva(regs) ((regs)->hvmer.vmbadva)

#define pt_set_rte_sp(regs, sp) do {\
	pt_psp(regs) = (sp);\
	(regs)->SP = (unsigned long) &((regs)->hvmer);\
	} while (0)

#define pt_set_kmode(regs) \
	(regs)->hvmer.vmest = (HVM_VMEST_IE_MSK << HVM_VMEST_IE_SFT)

#define pt_set_usermode(regs) \
	(regs)->hvmer.vmest = (HVM_VMEST_UM_MSK << HVM_VMEST_UM_SFT) \
			    | (HVM_VMEST_IE_MSK << HVM_VMEST_IE_SFT)

#endif  /*  ifndef __ASSEMBLY  */

#endif
