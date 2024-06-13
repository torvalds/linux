/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
#ifndef _UAPI_ASM_S390_SIE_H
#define _UAPI_ASM_S390_SIE_H

#define diagnose_codes						\
	{ 0x10, "DIAG (0x10) release pages" },			\
	{ 0x44, "DIAG (0x44) time slice end" },			\
	{ 0x9c, "DIAG (0x9c) time slice end directed" },	\
	{ 0x204, "DIAG (0x204) logical-cpu utilization" },	\
	{ 0x258, "DIAG (0x258) page-reference services" },	\
	{ 0x288, "DIAG (0x288) watchdog functions" },		\
	{ 0x308, "DIAG (0x308) ipl functions" },		\
	{ 0x500, "DIAG (0x500) KVM virtio functions" },		\
	{ 0x501, "DIAG (0x501) KVM breakpoint" }

#define sigp_order_codes					\
	{ 0x01, "SIGP sense" },					\
	{ 0x02, "SIGP external call" },				\
	{ 0x03, "SIGP emergency signal" },			\
	{ 0x04, "SIGP start" },					\
	{ 0x05, "SIGP stop" },					\
	{ 0x06, "SIGP restart" },				\
	{ 0x09, "SIGP stop and store status" },			\
	{ 0x0b, "SIGP initial cpu reset" },			\
	{ 0x0c, "SIGP cpu reset" },				\
	{ 0x0d, "SIGP set prefix" },				\
	{ 0x0e, "SIGP store status at address" },		\
	{ 0x12, "SIGP set architecture" },			\
	{ 0x13, "SIGP conditional emergency signal" },		\
	{ 0x15, "SIGP sense running" },				\
	{ 0x16, "SIGP set multithreading"},			\
	{ 0x17, "SIGP store additional status at address"}

#define icpt_prog_codes						\
	{ 0x0001, "Prog Operation" },				\
	{ 0x0002, "Prog Privileged Operation" },		\
	{ 0x0003, "Prog Execute" },				\
	{ 0x0004, "Prog Protection" },				\
	{ 0x0005, "Prog Addressing" },				\
	{ 0x0006, "Prog Specification" },			\
	{ 0x0007, "Prog Data" },				\
	{ 0x0008, "Prog Fixedpoint overflow" },			\
	{ 0x0009, "Prog Fixedpoint divide" },			\
	{ 0x000A, "Prog Decimal overflow" },			\
	{ 0x000B, "Prog Decimal divide" },			\
	{ 0x000C, "Prog HFP exponent overflow" },		\
	{ 0x000D, "Prog HFP exponent underflow" },		\
	{ 0x000E, "Prog HFP significance" },			\
	{ 0x000F, "Prog HFP divide" },				\
	{ 0x0010, "Prog Segment translation" },			\
	{ 0x0011, "Prog Page translation" },			\
	{ 0x0012, "Prog Translation specification" },		\
	{ 0x0013, "Prog Special operation" },			\
	{ 0x0015, "Prog Operand" },				\
	{ 0x0016, "Prog Trace table" },				\
	{ 0x0017, "Prog ASNtranslation specification" },	\
	{ 0x001C, "Prog Spaceswitch event" },			\
	{ 0x001D, "Prog HFP square root" },			\
	{ 0x001F, "Prog PCtranslation specification" },		\
	{ 0x0020, "Prog AFX translation" },			\
	{ 0x0021, "Prog ASX translation" },			\
	{ 0x0022, "Prog LX translation" },			\
	{ 0x0023, "Prog EX translation" },			\
	{ 0x0024, "Prog Primary authority" },			\
	{ 0x0025, "Prog Secondary authority" },			\
	{ 0x0026, "Prog LFXtranslation exception" },		\
	{ 0x0027, "Prog LSXtranslation exception" },		\
	{ 0x0028, "Prog ALET specification" },			\
	{ 0x0029, "Prog ALEN translation" },			\
	{ 0x002A, "Prog ALE sequence" },			\
	{ 0x002B, "Prog ASTE validity" },			\
	{ 0x002C, "Prog ASTE sequence" },			\
	{ 0x002D, "Prog Extended authority" },			\
	{ 0x002E, "Prog LSTE sequence" },			\
	{ 0x002F, "Prog ASTE instance" },			\
	{ 0x0030, "Prog Stack full" },				\
	{ 0x0031, "Prog Stack empty" },				\
	{ 0x0032, "Prog Stack specification" },			\
	{ 0x0033, "Prog Stack type" },				\
	{ 0x0034, "Prog Stack operation" },			\
	{ 0x0039, "Prog Region first translation" },		\
	{ 0x003A, "Prog Region second translation" },		\
	{ 0x003B, "Prog Region third translation" },		\
	{ 0x0040, "Prog Monitor event" },			\
	{ 0x0080, "Prog PER event" },				\
	{ 0x0119, "Prog Crypto operation" }

#define exit_code_ipa0(ipa0, opcode, mnemonic)		\
	{ (ipa0 << 8 | opcode), #ipa0 " " mnemonic }
#define exit_code(opcode, mnemonic)			\
	{ opcode, mnemonic }

#define icpt_insn_codes				\
	exit_code_ipa0(0x01, 0x01, "PR"),	\
	exit_code_ipa0(0x01, 0x04, "PTFF"),	\
	exit_code_ipa0(0x01, 0x07, "SCKPF"),	\
	exit_code_ipa0(0xAA, 0x00, "RINEXT"),	\
	exit_code_ipa0(0xAA, 0x01, "RION"),	\
	exit_code_ipa0(0xAA, 0x02, "TRIC"),	\
	exit_code_ipa0(0xAA, 0x03, "RIOFF"),	\
	exit_code_ipa0(0xAA, 0x04, "RIEMIT"),	\
	exit_code_ipa0(0xB2, 0x02, "STIDP"),	\
	exit_code_ipa0(0xB2, 0x04, "SCK"),	\
	exit_code_ipa0(0xB2, 0x05, "STCK"),	\
	exit_code_ipa0(0xB2, 0x06, "SCKC"),	\
	exit_code_ipa0(0xB2, 0x07, "STCKC"),	\
	exit_code_ipa0(0xB2, 0x08, "SPT"),	\
	exit_code_ipa0(0xB2, 0x09, "STPT"),	\
	exit_code_ipa0(0xB2, 0x0d, "PTLB"),	\
	exit_code_ipa0(0xB2, 0x10, "SPX"),	\
	exit_code_ipa0(0xB2, 0x11, "STPX"),	\
	exit_code_ipa0(0xB2, 0x12, "STAP"),	\
	exit_code_ipa0(0xB2, 0x14, "SIE"),	\
	exit_code_ipa0(0xB2, 0x16, "SETR"),	\
	exit_code_ipa0(0xB2, 0x17, "STETR"),	\
	exit_code_ipa0(0xB2, 0x18, "PC"),	\
	exit_code_ipa0(0xB2, 0x20, "SERVC"),	\
	exit_code_ipa0(0xB2, 0x21, "IPTE"),	\
	exit_code_ipa0(0xB2, 0x28, "PT"),	\
	exit_code_ipa0(0xB2, 0x29, "ISKE"),	\
	exit_code_ipa0(0xB2, 0x2a, "RRBE"),	\
	exit_code_ipa0(0xB2, 0x2b, "SSKE"),	\
	exit_code_ipa0(0xB2, 0x2c, "TB"),	\
	exit_code_ipa0(0xB2, 0x2e, "PGIN"),	\
	exit_code_ipa0(0xB2, 0x2f, "PGOUT"),	\
	exit_code_ipa0(0xB2, 0x30, "CSCH"),	\
	exit_code_ipa0(0xB2, 0x31, "HSCH"),	\
	exit_code_ipa0(0xB2, 0x32, "MSCH"),	\
	exit_code_ipa0(0xB2, 0x33, "SSCH"),	\
	exit_code_ipa0(0xB2, 0x34, "STSCH"),	\
	exit_code_ipa0(0xB2, 0x35, "TSCH"),	\
	exit_code_ipa0(0xB2, 0x36, "TPI"),	\
	exit_code_ipa0(0xB2, 0x37, "SAL"),	\
	exit_code_ipa0(0xB2, 0x38, "RSCH"),	\
	exit_code_ipa0(0xB2, 0x39, "STCRW"),	\
	exit_code_ipa0(0xB2, 0x3a, "STCPS"),	\
	exit_code_ipa0(0xB2, 0x3b, "RCHP"),	\
	exit_code_ipa0(0xB2, 0x3c, "SCHM"),	\
	exit_code_ipa0(0xB2, 0x40, "BAKR"),	\
	exit_code_ipa0(0xB2, 0x48, "PALB"),	\
	exit_code_ipa0(0xB2, 0x4c, "TAR"),	\
	exit_code_ipa0(0xB2, 0x50, "CSP"),	\
	exit_code_ipa0(0xB2, 0x54, "MVPG"),	\
	exit_code_ipa0(0xB2, 0x56, "STHYI"),	\
	exit_code_ipa0(0xB2, 0x58, "BSG"),	\
	exit_code_ipa0(0xB2, 0x5a, "BSA"),	\
	exit_code_ipa0(0xB2, 0x5f, "CHSC"),	\
	exit_code_ipa0(0xB2, 0x74, "SIGA"),	\
	exit_code_ipa0(0xB2, 0x76, "XSCH"),	\
	exit_code_ipa0(0xB2, 0x78, "STCKE"),	\
	exit_code_ipa0(0xB2, 0x7c, "STCKF"),	\
	exit_code_ipa0(0xB2, 0x7d, "STSI"),	\
	exit_code_ipa0(0xB2, 0xb0, "STFLE"),	\
	exit_code_ipa0(0xB2, 0xb1, "STFL"),	\
	exit_code_ipa0(0xB2, 0xb2, "LPSWE"),	\
	exit_code_ipa0(0xB2, 0xf8, "TEND"),	\
	exit_code_ipa0(0xB2, 0xfc, "TABORT"),	\
	exit_code_ipa0(0xB9, 0x1e, "KMAC"),	\
	exit_code_ipa0(0xB9, 0x28, "PCKMO"),	\
	exit_code_ipa0(0xB9, 0x2a, "KMF"),	\
	exit_code_ipa0(0xB9, 0x2b, "KMO"),	\
	exit_code_ipa0(0xB9, 0x2d, "KMCTR"),	\
	exit_code_ipa0(0xB9, 0x2e, "KM"),	\
	exit_code_ipa0(0xB9, 0x2f, "KMC"),	\
	exit_code_ipa0(0xB9, 0x3e, "KIMD"),	\
	exit_code_ipa0(0xB9, 0x3f, "KLMD"),	\
	exit_code_ipa0(0xB9, 0x8a, "CSPG"),	\
	exit_code_ipa0(0xB9, 0x8d, "EPSW"),	\
	exit_code_ipa0(0xB9, 0x8e, "IDTE"),	\
	exit_code_ipa0(0xB9, 0x8f, "CRDTE"),	\
	exit_code_ipa0(0xB9, 0x9c, "EQBS"),	\
	exit_code_ipa0(0xB9, 0xa2, "PTF"),	\
	exit_code_ipa0(0xB9, 0xab, "ESSA"),	\
	exit_code_ipa0(0xB9, 0xae, "RRBM"),	\
	exit_code_ipa0(0xB9, 0xaf, "PFMF"),	\
	exit_code_ipa0(0xE3, 0x03, "LRAG"),	\
	exit_code_ipa0(0xE3, 0x13, "LRAY"),	\
	exit_code_ipa0(0xE3, 0x25, "NTSTG"),	\
	exit_code_ipa0(0xE5, 0x00, "LASP"),	\
	exit_code_ipa0(0xE5, 0x01, "TPROT"),	\
	exit_code_ipa0(0xE5, 0x60, "TBEGIN"),	\
	exit_code_ipa0(0xE5, 0x61, "TBEGINC"),	\
	exit_code_ipa0(0xEB, 0x25, "STCTG"),	\
	exit_code_ipa0(0xEB, 0x2f, "LCTLG"),	\
	exit_code_ipa0(0xEB, 0x60, "LRIC"),	\
	exit_code_ipa0(0xEB, 0x61, "STRIC"),	\
	exit_code_ipa0(0xEB, 0x62, "MRIC"),	\
	exit_code_ipa0(0xEB, 0x8a, "SQBS"),	\
	exit_code_ipa0(0xC8, 0x01, "ECTG"),	\
	exit_code(0x0a, "SVC"),			\
	exit_code(0x80, "SSM"),			\
	exit_code(0x82, "LPSW"),		\
	exit_code(0x83, "DIAG"),		\
	exit_code(0xae, "SIGP"),		\
	exit_code(0xac, "STNSM"),		\
	exit_code(0xad, "STOSM"),		\
	exit_code(0xb1, "LRA"),			\
	exit_code(0xb6, "STCTL"),		\
	exit_code(0xb7, "LCTL"),		\
	exit_code(0xee, "PLO")

#define sie_intercept_code					\
	{ 0x00, "Host interruption" },				\
	{ 0x04, "Instruction" },				\
	{ 0x08, "Program interruption" },			\
	{ 0x0c, "Instruction and program interruption" },	\
	{ 0x10, "External request" },				\
	{ 0x14, "External interruption" },			\
	{ 0x18, "I/O request" },				\
	{ 0x1c, "Wait state" },					\
	{ 0x20, "Validity" },					\
	{ 0x28, "Stop request" },				\
	{ 0x2c, "Operation exception" },			\
	{ 0x38, "Partial-execution" },				\
	{ 0x3c, "I/O interruption" },				\
	{ 0x40, "I/O instruction" },				\
	{ 0x48, "Timing subset" }

/*
 * This is the simple interceptable instructions decoder.
 *
 * It will be used as userspace interface and it can be used in places
 * that does not allow to use general decoder functions,
 * such as trace events declarations.
 *
 * Some userspace tools may want to parse this code
 * and would be confused by switch(), if() and other statements,
 * but they can understand conditional operator.
 */
#define INSN_DECODE_IPA0(ipa0, insn, rshift, mask)		\
	(insn >> 56) == (ipa0) ?				\
		((ipa0 << 8) | ((insn >> rshift) & mask)) :

#define INSN_DECODE(insn) (insn >> 56)

/*
 * The macro icpt_insn_decoder() takes an intercepted instruction
 * and returns a key, which can be used to find a mnemonic name
 * of the instruction in the icpt_insn_codes table.
 */
#define icpt_insn_decoder(insn) (		\
	INSN_DECODE_IPA0(0x01, insn, 48, 0xff)	\
	INSN_DECODE_IPA0(0xaa, insn, 48, 0x0f)	\
	INSN_DECODE_IPA0(0xb2, insn, 48, 0xff)	\
	INSN_DECODE_IPA0(0xb9, insn, 48, 0xff)	\
	INSN_DECODE_IPA0(0xe3, insn, 48, 0xff)	\
	INSN_DECODE_IPA0(0xe5, insn, 48, 0xff)	\
	INSN_DECODE_IPA0(0xeb, insn, 16, 0xff)	\
	INSN_DECODE_IPA0(0xc8, insn, 48, 0x0f)	\
	INSN_DECODE(insn))

#endif /* _UAPI_ASM_S390_SIE_H */
