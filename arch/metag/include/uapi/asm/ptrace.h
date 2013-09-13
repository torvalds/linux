#ifndef _UAPI_METAG_PTRACE_H
#define _UAPI_METAG_PTRACE_H

#ifndef __ASSEMBLY__

/*
 * These are the layouts of the regsets returned by the GETREGSET ptrace call
 */

/* user_gp_regs::status */

/* CBMarker bit (indicates catch state / catch replay) */
#define USER_GP_REGS_STATUS_CATCH_BIT		(1 << 22)
#define USER_GP_REGS_STATUS_CATCH_S		22
/* LSM_STEP field (load/store multiple step) */
#define USER_GP_REGS_STATUS_LSM_STEP_BITS	(0x7 << 8)
#define USER_GP_REGS_STATUS_LSM_STEP_S		8
/* SCC bit (indicates split 16x16 condition flags) */
#define USER_GP_REGS_STATUS_SCC_BIT		(1 << 4)
#define USER_GP_REGS_STATUS_SCC_S		4

/* normal condition flags */
/* CF_Z bit (Zero flag) */
#define USER_GP_REGS_STATUS_CF_Z_BIT		(1 << 3)
#define USER_GP_REGS_STATUS_CF_Z_S		3
/* CF_N bit (Negative flag) */
#define USER_GP_REGS_STATUS_CF_N_BIT		(1 << 2)
#define USER_GP_REGS_STATUS_CF_N_S		2
/* CF_V bit (oVerflow flag) */
#define USER_GP_REGS_STATUS_CF_V_BIT		(1 << 1)
#define USER_GP_REGS_STATUS_CF_V_S		1
/* CF_C bit (Carry flag) */
#define USER_GP_REGS_STATUS_CF_C_BIT		(1 << 0)
#define USER_GP_REGS_STATUS_CF_C_S		0

/* split 16x16 condition flags */
/* SCF_LZ bit (Low Zero flag) */
#define USER_GP_REGS_STATUS_SCF_LZ_BIT		(1 << 3)
#define USER_GP_REGS_STATUS_SCF_LZ_S		3
/* SCF_HZ bit (High Zero flag) */
#define USER_GP_REGS_STATUS_SCF_HZ_BIT		(1 << 2)
#define USER_GP_REGS_STATUS_SCF_HZ_S		2
/* SCF_HC bit (High Carry flag) */
#define USER_GP_REGS_STATUS_SCF_HC_BIT		(1 << 1)
#define USER_GP_REGS_STATUS_SCF_HC_S		1
/* SCF_LC bit (Low Carry flag) */
#define USER_GP_REGS_STATUS_SCF_LC_BIT		(1 << 0)
#define USER_GP_REGS_STATUS_SCF_LC_S		0

/**
 * struct user_gp_regs - User general purpose registers
 * @dx:		GP data unit regs (dx[reg][unit] = D{unit:0-1}.{reg:0-7})
 * @ax:		GP address unit regs (ax[reg][unit] = A{unit:0-1}.{reg:0-3})
 * @pc:		PC register
 * @status:	TXSTATUS register (condition flags, LSM_STEP etc)
 * @rpt:	TXRPT registers (branch repeat counter)
 * @bpobits:	TXBPOBITS register ("branch prediction other" bits)
 * @mode:	TXMODE register
 * @_pad1:	Reserved padding to make sizeof obviously 64bit aligned
 *
 * This is the user-visible general purpose register state structure.
 *
 * It can be accessed through PTRACE_GETREGSET with NT_PRSTATUS.
 *
 * It is also used in the signal context.
 */
struct user_gp_regs {
	unsigned long dx[8][2];
	unsigned long ax[4][2];
	unsigned long pc;
	unsigned long status;
	unsigned long rpt;
	unsigned long bpobits;
	unsigned long mode;
	unsigned long _pad1;
};

/**
 * struct user_cb_regs - User catch buffer registers
 * @flags:	TXCATCH0 register (fault flags)
 * @addr:	TXCATCH1 register (fault address)
 * @data:	TXCATCH2 and TXCATCH3 registers (low and high data word)
 *
 * This is the user-visible catch buffer register state structure containing
 * information about a failed memory access, and allowing the access to be
 * modified and replayed.
 *
 * It can be accessed through PTRACE_GETREGSET with NT_METAG_CBUF.
 */
struct user_cb_regs {
	unsigned long flags;
	unsigned long addr;
	unsigned long long data;
};

/**
 * struct user_rp_state - User read pipeline state
 * @entries:	Read pipeline entries
 * @mask:	Mask of valid pipeline entries (RPMask from TXDIVTIME register)
 *
 * This is the user-visible read pipeline state structure containing the entries
 * currently in the read pipeline and the mask of valid entries.
 *
 * It can be accessed through PTRACE_GETREGSET with NT_METAG_RPIPE.
 */
struct user_rp_state {
	unsigned long long entries[6];
	unsigned long mask;
};

#endif /* __ASSEMBLY__ */

#endif /* _UAPI_METAG_PTRACE_H */
