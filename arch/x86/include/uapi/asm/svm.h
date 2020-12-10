/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI__SVM_H
#define _UAPI__SVM_H

#define SVM_EXIT_READ_CR0      0x000
#define SVM_EXIT_READ_CR2      0x002
#define SVM_EXIT_READ_CR3      0x003
#define SVM_EXIT_READ_CR4      0x004
#define SVM_EXIT_READ_CR8      0x008
#define SVM_EXIT_WRITE_CR0     0x010
#define SVM_EXIT_WRITE_CR2     0x012
#define SVM_EXIT_WRITE_CR3     0x013
#define SVM_EXIT_WRITE_CR4     0x014
#define SVM_EXIT_WRITE_CR8     0x018
#define SVM_EXIT_READ_DR0      0x020
#define SVM_EXIT_READ_DR1      0x021
#define SVM_EXIT_READ_DR2      0x022
#define SVM_EXIT_READ_DR3      0x023
#define SVM_EXIT_READ_DR4      0x024
#define SVM_EXIT_READ_DR5      0x025
#define SVM_EXIT_READ_DR6      0x026
#define SVM_EXIT_READ_DR7      0x027
#define SVM_EXIT_WRITE_DR0     0x030
#define SVM_EXIT_WRITE_DR1     0x031
#define SVM_EXIT_WRITE_DR2     0x032
#define SVM_EXIT_WRITE_DR3     0x033
#define SVM_EXIT_WRITE_DR4     0x034
#define SVM_EXIT_WRITE_DR5     0x035
#define SVM_EXIT_WRITE_DR6     0x036
#define SVM_EXIT_WRITE_DR7     0x037
#define SVM_EXIT_EXCP_BASE     0x040
#define SVM_EXIT_LAST_EXCP     0x05f
#define SVM_EXIT_INTR          0x060
#define SVM_EXIT_NMI           0x061
#define SVM_EXIT_SMI           0x062
#define SVM_EXIT_INIT          0x063
#define SVM_EXIT_VINTR         0x064
#define SVM_EXIT_CR0_SEL_WRITE 0x065
#define SVM_EXIT_IDTR_READ     0x066
#define SVM_EXIT_GDTR_READ     0x067
#define SVM_EXIT_LDTR_READ     0x068
#define SVM_EXIT_TR_READ       0x069
#define SVM_EXIT_IDTR_WRITE    0x06a
#define SVM_EXIT_GDTR_WRITE    0x06b
#define SVM_EXIT_LDTR_WRITE    0x06c
#define SVM_EXIT_TR_WRITE      0x06d
#define SVM_EXIT_RDTSC         0x06e
#define SVM_EXIT_RDPMC         0x06f
#define SVM_EXIT_PUSHF         0x070
#define SVM_EXIT_POPF          0x071
#define SVM_EXIT_CPUID         0x072
#define SVM_EXIT_RSM           0x073
#define SVM_EXIT_IRET          0x074
#define SVM_EXIT_SWINT         0x075
#define SVM_EXIT_INVD          0x076
#define SVM_EXIT_PAUSE         0x077
#define SVM_EXIT_HLT           0x078
#define SVM_EXIT_INVLPG        0x079
#define SVM_EXIT_INVLPGA       0x07a
#define SVM_EXIT_IOIO          0x07b
#define SVM_EXIT_MSR           0x07c
#define SVM_EXIT_TASK_SWITCH   0x07d
#define SVM_EXIT_FERR_FREEZE   0x07e
#define SVM_EXIT_SHUTDOWN      0x07f
#define SVM_EXIT_VMRUN         0x080
#define SVM_EXIT_VMMCALL       0x081
#define SVM_EXIT_VMLOAD        0x082
#define SVM_EXIT_VMSAVE        0x083
#define SVM_EXIT_STGI          0x084
#define SVM_EXIT_CLGI          0x085
#define SVM_EXIT_SKINIT        0x086
#define SVM_EXIT_RDTSCP        0x087
#define SVM_EXIT_ICEBP         0x088
#define SVM_EXIT_WBINVD        0x089
#define SVM_EXIT_MONITOR       0x08a
#define SVM_EXIT_MWAIT         0x08b
#define SVM_EXIT_MWAIT_COND    0x08c
#define SVM_EXIT_XSETBV        0x08d
#define SVM_EXIT_RDPRU         0x08e
#define SVM_EXIT_EFER_WRITE_TRAP		0x08f
#define SVM_EXIT_CR0_WRITE_TRAP			0x090
#define SVM_EXIT_CR1_WRITE_TRAP			0x091
#define SVM_EXIT_CR2_WRITE_TRAP			0x092
#define SVM_EXIT_CR3_WRITE_TRAP			0x093
#define SVM_EXIT_CR4_WRITE_TRAP			0x094
#define SVM_EXIT_CR5_WRITE_TRAP			0x095
#define SVM_EXIT_CR6_WRITE_TRAP			0x096
#define SVM_EXIT_CR7_WRITE_TRAP			0x097
#define SVM_EXIT_CR8_WRITE_TRAP			0x098
#define SVM_EXIT_CR9_WRITE_TRAP			0x099
#define SVM_EXIT_CR10_WRITE_TRAP		0x09a
#define SVM_EXIT_CR11_WRITE_TRAP		0x09b
#define SVM_EXIT_CR12_WRITE_TRAP		0x09c
#define SVM_EXIT_CR13_WRITE_TRAP		0x09d
#define SVM_EXIT_CR14_WRITE_TRAP		0x09e
#define SVM_EXIT_CR15_WRITE_TRAP		0x09f
#define SVM_EXIT_INVPCID       0x0a2
#define SVM_EXIT_NPF           0x400
#define SVM_EXIT_AVIC_INCOMPLETE_IPI		0x401
#define SVM_EXIT_AVIC_UNACCELERATED_ACCESS	0x402
#define SVM_EXIT_VMGEXIT       0x403

/* SEV-ES software-defined VMGEXIT events */
#define SVM_VMGEXIT_MMIO_READ			0x80000001
#define SVM_VMGEXIT_MMIO_WRITE			0x80000002
#define SVM_VMGEXIT_NMI_COMPLETE		0x80000003
#define SVM_VMGEXIT_AP_HLT_LOOP			0x80000004
#define SVM_VMGEXIT_AP_JUMP_TABLE		0x80000005
#define SVM_VMGEXIT_SET_AP_JUMP_TABLE		0
#define SVM_VMGEXIT_GET_AP_JUMP_TABLE		1
#define SVM_VMGEXIT_UNSUPPORTED_EVENT		0x8000ffff

#define SVM_EXIT_ERR           -1

#define SVM_EXIT_REASONS \
	{ SVM_EXIT_READ_CR0,    "read_cr0" }, \
	{ SVM_EXIT_READ_CR2,    "read_cr2" }, \
	{ SVM_EXIT_READ_CR3,    "read_cr3" }, \
	{ SVM_EXIT_READ_CR4,    "read_cr4" }, \
	{ SVM_EXIT_READ_CR8,    "read_cr8" }, \
	{ SVM_EXIT_WRITE_CR0,   "write_cr0" }, \
	{ SVM_EXIT_WRITE_CR2,   "write_cr2" }, \
	{ SVM_EXIT_WRITE_CR3,   "write_cr3" }, \
	{ SVM_EXIT_WRITE_CR4,   "write_cr4" }, \
	{ SVM_EXIT_WRITE_CR8,   "write_cr8" }, \
	{ SVM_EXIT_READ_DR0,    "read_dr0" }, \
	{ SVM_EXIT_READ_DR1,    "read_dr1" }, \
	{ SVM_EXIT_READ_DR2,    "read_dr2" }, \
	{ SVM_EXIT_READ_DR3,    "read_dr3" }, \
	{ SVM_EXIT_READ_DR4,    "read_dr4" }, \
	{ SVM_EXIT_READ_DR5,    "read_dr5" }, \
	{ SVM_EXIT_READ_DR6,    "read_dr6" }, \
	{ SVM_EXIT_READ_DR7,    "read_dr7" }, \
	{ SVM_EXIT_WRITE_DR0,   "write_dr0" }, \
	{ SVM_EXIT_WRITE_DR1,   "write_dr1" }, \
	{ SVM_EXIT_WRITE_DR2,   "write_dr2" }, \
	{ SVM_EXIT_WRITE_DR3,   "write_dr3" }, \
	{ SVM_EXIT_WRITE_DR4,   "write_dr4" }, \
	{ SVM_EXIT_WRITE_DR5,   "write_dr5" }, \
	{ SVM_EXIT_WRITE_DR6,   "write_dr6" }, \
	{ SVM_EXIT_WRITE_DR7,   "write_dr7" }, \
	{ SVM_EXIT_EXCP_BASE + DE_VECTOR,       "DE excp" }, \
	{ SVM_EXIT_EXCP_BASE + DB_VECTOR,       "DB excp" }, \
	{ SVM_EXIT_EXCP_BASE + BP_VECTOR,       "BP excp" }, \
	{ SVM_EXIT_EXCP_BASE + OF_VECTOR,       "OF excp" }, \
	{ SVM_EXIT_EXCP_BASE + BR_VECTOR,       "BR excp" }, \
	{ SVM_EXIT_EXCP_BASE + UD_VECTOR,       "UD excp" }, \
	{ SVM_EXIT_EXCP_BASE + NM_VECTOR,       "NM excp" }, \
	{ SVM_EXIT_EXCP_BASE + DF_VECTOR,       "DF excp" }, \
	{ SVM_EXIT_EXCP_BASE + TS_VECTOR,       "TS excp" }, \
	{ SVM_EXIT_EXCP_BASE + NP_VECTOR,       "NP excp" }, \
	{ SVM_EXIT_EXCP_BASE + SS_VECTOR,       "SS excp" }, \
	{ SVM_EXIT_EXCP_BASE + GP_VECTOR,       "GP excp" }, \
	{ SVM_EXIT_EXCP_BASE + PF_VECTOR,       "PF excp" }, \
	{ SVM_EXIT_EXCP_BASE + MF_VECTOR,       "MF excp" }, \
	{ SVM_EXIT_EXCP_BASE + AC_VECTOR,       "AC excp" }, \
	{ SVM_EXIT_EXCP_BASE + MC_VECTOR,       "MC excp" }, \
	{ SVM_EXIT_EXCP_BASE + XM_VECTOR,       "XF excp" }, \
	{ SVM_EXIT_INTR,        "interrupt" }, \
	{ SVM_EXIT_NMI,         "nmi" }, \
	{ SVM_EXIT_SMI,         "smi" }, \
	{ SVM_EXIT_INIT,        "init" }, \
	{ SVM_EXIT_VINTR,       "vintr" }, \
	{ SVM_EXIT_CR0_SEL_WRITE, "cr0_sel_write" }, \
	{ SVM_EXIT_IDTR_READ,   "read_idtr" }, \
	{ SVM_EXIT_GDTR_READ,   "read_gdtr" }, \
	{ SVM_EXIT_LDTR_READ,   "read_ldtr" }, \
	{ SVM_EXIT_TR_READ,     "read_rt" }, \
	{ SVM_EXIT_IDTR_WRITE,  "write_idtr" }, \
	{ SVM_EXIT_GDTR_WRITE,  "write_gdtr" }, \
	{ SVM_EXIT_LDTR_WRITE,  "write_ldtr" }, \
	{ SVM_EXIT_TR_WRITE,    "write_rt" }, \
	{ SVM_EXIT_RDTSC,       "rdtsc" }, \
	{ SVM_EXIT_RDPMC,       "rdpmc" }, \
	{ SVM_EXIT_PUSHF,       "pushf" }, \
	{ SVM_EXIT_POPF,        "popf" }, \
	{ SVM_EXIT_CPUID,       "cpuid" }, \
	{ SVM_EXIT_RSM,         "rsm" }, \
	{ SVM_EXIT_IRET,        "iret" }, \
	{ SVM_EXIT_SWINT,       "swint" }, \
	{ SVM_EXIT_INVD,        "invd" }, \
	{ SVM_EXIT_PAUSE,       "pause" }, \
	{ SVM_EXIT_HLT,         "hlt" }, \
	{ SVM_EXIT_INVLPG,      "invlpg" }, \
	{ SVM_EXIT_INVLPGA,     "invlpga" }, \
	{ SVM_EXIT_IOIO,        "io" }, \
	{ SVM_EXIT_MSR,         "msr" }, \
	{ SVM_EXIT_TASK_SWITCH, "task_switch" }, \
	{ SVM_EXIT_FERR_FREEZE, "ferr_freeze" }, \
	{ SVM_EXIT_SHUTDOWN,    "shutdown" }, \
	{ SVM_EXIT_VMRUN,       "vmrun" }, \
	{ SVM_EXIT_VMMCALL,     "hypercall" }, \
	{ SVM_EXIT_VMLOAD,      "vmload" }, \
	{ SVM_EXIT_VMSAVE,      "vmsave" }, \
	{ SVM_EXIT_STGI,        "stgi" }, \
	{ SVM_EXIT_CLGI,        "clgi" }, \
	{ SVM_EXIT_SKINIT,      "skinit" }, \
	{ SVM_EXIT_RDTSCP,      "rdtscp" }, \
	{ SVM_EXIT_ICEBP,       "icebp" }, \
	{ SVM_EXIT_WBINVD,      "wbinvd" }, \
	{ SVM_EXIT_MONITOR,     "monitor" }, \
	{ SVM_EXIT_MWAIT,       "mwait" }, \
	{ SVM_EXIT_XSETBV,      "xsetbv" }, \
	{ SVM_EXIT_EFER_WRITE_TRAP,	"write_efer_trap" }, \
	{ SVM_EXIT_CR0_WRITE_TRAP,	"write_cr0_trap" }, \
	{ SVM_EXIT_INVPCID,     "invpcid" }, \
	{ SVM_EXIT_NPF,         "npf" }, \
	{ SVM_EXIT_AVIC_INCOMPLETE_IPI,		"avic_incomplete_ipi" }, \
	{ SVM_EXIT_AVIC_UNACCELERATED_ACCESS,   "avic_unaccelerated_access" }, \
	{ SVM_EXIT_VMGEXIT,		"vmgexit" }, \
	{ SVM_VMGEXIT_MMIO_READ,	"vmgexit_mmio_read" }, \
	{ SVM_VMGEXIT_MMIO_WRITE,	"vmgexit_mmio_write" }, \
	{ SVM_VMGEXIT_NMI_COMPLETE,	"vmgexit_nmi_complete" }, \
	{ SVM_VMGEXIT_AP_HLT_LOOP,	"vmgexit_ap_hlt_loop" }, \
	{ SVM_VMGEXIT_AP_JUMP_TABLE,	"vmgexit_ap_jump_table" }, \
	{ SVM_EXIT_ERR,         "invalid_guest_state" }


#endif /* _UAPI__SVM_H */
