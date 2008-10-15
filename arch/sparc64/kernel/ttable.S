/* ttable.S: Sparc V9 Trap Table(s) with SpitFire/Cheetah/SUN4V extensions.
 *
 * Copyright (C) 1996, 2001, 2006 David S. Miller (davem@davemloft.net)
 */


	.globl	sparc64_ttable_tl0, sparc64_ttable_tl1
	.globl	tl0_icpe, tl1_icpe
	.globl	tl0_dcpe, tl1_dcpe
	.globl	tl0_fecc, tl1_fecc
	.globl	tl0_cee, tl1_cee
	.globl	tl0_iae, tl1_iae
	.globl	tl0_dae, tl1_dae

sparc64_ttable_tl0:
tl0_resv000:	BOOT_KERNEL BTRAP(0x1) BTRAP(0x2) BTRAP(0x3)
tl0_resv004:	BTRAP(0x4)  BTRAP(0x5) BTRAP(0x6) BTRAP(0x7)
tl0_iax:	membar #Sync
		TRAP_NOSAVE_7INSNS(__spitfire_insn_access_exception)
tl0_itsb_4v:	SUN4V_ITSB_MISS
tl0_iae:	membar #Sync
		TRAP_NOSAVE_7INSNS(__spitfire_access_error)
tl0_resv00b:	BTRAP(0xb) BTRAP(0xc) BTRAP(0xd) BTRAP(0xe) BTRAP(0xf)
tl0_ill:	membar #Sync
		TRAP_7INSNS(do_illegal_instruction)
tl0_privop:	TRAP(do_privop)
tl0_resv012:	BTRAP(0x12) BTRAP(0x13) BTRAP(0x14) BTRAP(0x15) BTRAP(0x16) BTRAP(0x17)
tl0_resv018:	BTRAP(0x18) BTRAP(0x19) BTRAP(0x1a) BTRAP(0x1b) BTRAP(0x1c) BTRAP(0x1d)
tl0_resv01e:	BTRAP(0x1e) BTRAP(0x1f)
tl0_fpdis:	TRAP_NOSAVE(do_fpdis)
tl0_fpieee:	TRAP_SAVEFPU(do_fpieee)
tl0_fpother:	TRAP_NOSAVE(do_fpother_check_fitos)
tl0_tof:	TRAP(do_tof)
tl0_cwin:	CLEAN_WINDOW
tl0_div0:	TRAP(do_div0)
tl0_resv029:	BTRAP(0x29) BTRAP(0x2a) BTRAP(0x2b) BTRAP(0x2c) BTRAP(0x2d) BTRAP(0x2e)
tl0_resv02f:	BTRAP(0x2f)
tl0_dax:	TRAP_NOSAVE(__spitfire_data_access_exception)
tl0_dtsb_4v:	SUN4V_DTSB_MISS
tl0_dae:	membar #Sync
		TRAP_NOSAVE_7INSNS(__spitfire_access_error)
tl0_resv033:	BTRAP(0x33)
tl0_mna:	TRAP_NOSAVE(do_mna)
tl0_lddfmna:	TRAP_NOSAVE(do_lddfmna)
tl0_stdfmna:	TRAP_NOSAVE(do_stdfmna)
tl0_privact:	TRAP_NOSAVE(__do_privact)
tl0_resv038:	BTRAP(0x38) BTRAP(0x39) BTRAP(0x3a) BTRAP(0x3b) BTRAP(0x3c) BTRAP(0x3d)
tl0_resv03e:	BTRAP(0x3e) BTRAP(0x3f) BTRAP(0x40)
#ifdef CONFIG_SMP
tl0_irq1:	TRAP_IRQ(smp_call_function_client, 1)
tl0_irq2:	TRAP_IRQ(smp_receive_signal_client, 2)
tl0_irq3:	TRAP_IRQ(smp_penguin_jailcell, 3)
tl0_irq4:	TRAP_IRQ(smp_new_mmu_context_version_client, 4)
#else
tl0_irq1:	BTRAP(0x41)
tl0_irq2:	BTRAP(0x42)
tl0_irq3:	BTRAP(0x43)
tl0_irq4:	BTRAP(0x44)
#endif
tl0_irq5:	TRAP_IRQ(handler_irq, 5)
#ifdef CONFIG_SMP
tl0_irq6:	TRAP_IRQ(smp_call_function_single_client, 6)
#else
tl0_irq6:	BTRAP(0x46)
#endif
tl0_irq7:	BTRAP(0x47) BTRAP(0x48) BTRAP(0x49)
tl0_irq10:	BTRAP(0x4a) BTRAP(0x4b) BTRAP(0x4c) BTRAP(0x4d)
tl0_irq14:	TRAP_IRQ(timer_interrupt, 14)
tl0_irq15:	TRAP_IRQ(handler_irq, 15)
tl0_resv050:	BTRAP(0x50) BTRAP(0x51) BTRAP(0x52) BTRAP(0x53) BTRAP(0x54) BTRAP(0x55)
tl0_resv056:	BTRAP(0x56) BTRAP(0x57) BTRAP(0x58) BTRAP(0x59) BTRAP(0x5a) BTRAP(0x5b)
tl0_resv05c:	BTRAP(0x5c) BTRAP(0x5d) BTRAP(0x5e) BTRAP(0x5f)
tl0_ivec:	TRAP_IVEC
tl0_paw:	TRAP(do_paw)
tl0_vaw:	TRAP(do_vaw)
tl0_cee:	membar #Sync
		TRAP_NOSAVE_7INSNS(__spitfire_cee_trap)
tl0_iamiss:
#include	"itlb_miss.S"
tl0_damiss:
#include	"dtlb_miss.S"
tl0_daprot:
#include	"dtlb_prot.S"
tl0_fecc:	BTRAP(0x70)	/* Fast-ECC on Cheetah */
tl0_dcpe:	BTRAP(0x71)	/* D-cache Parity Error on Cheetah+ */
tl0_icpe:	BTRAP(0x72)	/* I-cache Parity Error on Cheetah+ */
tl0_resv073:	BTRAP(0x73) BTRAP(0x74) BTRAP(0x75)
tl0_resv076:	BTRAP(0x76) BTRAP(0x77) BTRAP(0x78) BTRAP(0x79) BTRAP(0x7a) BTRAP(0x7b)
tl0_cpu_mondo:	TRAP_NOSAVE(sun4v_cpu_mondo)
tl0_dev_mondo:	TRAP_NOSAVE(sun4v_dev_mondo)
tl0_res_mondo:	TRAP_NOSAVE(sun4v_res_mondo)
tl0_nres_mondo:	TRAP_NOSAVE(sun4v_nonres_mondo)
tl0_s0n:	SPILL_0_NORMAL
tl0_s1n:	SPILL_1_NORMAL
tl0_s2n:	SPILL_2_NORMAL
tl0_s3n:	SPILL_0_NORMAL_ETRAP
tl0_s4n:	SPILL_1_GENERIC_ETRAP
tl0_s5n:	SPILL_1_GENERIC_ETRAP_FIXUP
tl0_s6n:	SPILL_2_GENERIC_ETRAP
tl0_s7n:	SPILL_2_GENERIC_ETRAP_FIXUP
tl0_s0o:	SPILL_0_OTHER
tl0_s1o:	SPILL_1_OTHER
tl0_s2o:	SPILL_2_OTHER
tl0_s3o:	SPILL_3_OTHER
tl0_s4o:	SPILL_4_OTHER
tl0_s5o:	SPILL_5_OTHER
tl0_s6o:	SPILL_6_OTHER
tl0_s7o:	SPILL_7_OTHER
tl0_f0n:	FILL_0_NORMAL
tl0_f1n:	FILL_1_NORMAL
tl0_f2n:	FILL_2_NORMAL
tl0_f3n:	FILL_3_NORMAL
tl0_f4n:	FILL_4_NORMAL
tl0_f5n:	FILL_0_NORMAL_RTRAP
tl0_f6n:	FILL_1_GENERIC_RTRAP
tl0_f7n:	FILL_2_GENERIC_RTRAP
tl0_f0o:	FILL_0_OTHER
tl0_f1o:	FILL_1_OTHER
tl0_f2o:	FILL_2_OTHER
tl0_f3o:	FILL_3_OTHER
tl0_f4o:	FILL_4_OTHER
tl0_f5o:	FILL_5_OTHER
tl0_f6o:	FILL_6_OTHER
tl0_f7o:	FILL_7_OTHER
tl0_resv100:	BTRAP(0x100)
tl0_bkpt:	BREAKPOINT_TRAP
tl0_divz:	TRAP(do_div0)
tl0_flushw:	FLUSH_WINDOW_TRAP
tl0_resv104:	BTRAP(0x104) BTRAP(0x105) BTRAP(0x106) BTRAP(0x107) BTRAP(0x108)
tl0_resv109:	BTRAP(0x109) BTRAP(0x10a) BTRAP(0x10b) BTRAP(0x10c) BTRAP(0x10d)
tl0_resv10e:	BTRAP(0x10e) BTRAP(0x10f)
tl0_linux32:	LINUX_32BIT_SYSCALL_TRAP
tl0_oldlinux64:	LINUX_64BIT_SYSCALL_TRAP
tl0_resv112:	TRAP_UTRAP(UT_TRAP_INSTRUCTION_18,0x112) TRAP_UTRAP(UT_TRAP_INSTRUCTION_19,0x113)
tl0_resv114:	TRAP_UTRAP(UT_TRAP_INSTRUCTION_20,0x114) TRAP_UTRAP(UT_TRAP_INSTRUCTION_21,0x115)
tl0_resv116:	TRAP_UTRAP(UT_TRAP_INSTRUCTION_22,0x116) TRAP_UTRAP(UT_TRAP_INSTRUCTION_23,0x117)
tl0_resv118:	TRAP_UTRAP(UT_TRAP_INSTRUCTION_24,0x118) TRAP_UTRAP(UT_TRAP_INSTRUCTION_25,0x119)
tl0_resv11a:	TRAP_UTRAP(UT_TRAP_INSTRUCTION_26,0x11a) TRAP_UTRAP(UT_TRAP_INSTRUCTION_27,0x11b)
tl0_resv11c:	TRAP_UTRAP(UT_TRAP_INSTRUCTION_28,0x11c) TRAP_UTRAP(UT_TRAP_INSTRUCTION_29,0x11d)
tl0_resv11e:	TRAP_UTRAP(UT_TRAP_INSTRUCTION_30,0x11e) TRAP_UTRAP(UT_TRAP_INSTRUCTION_31,0x11f)
tl0_getcc:	GETCC_TRAP
tl0_setcc:	SETCC_TRAP
tl0_getpsr:	TRAP(do_getpsr)
tl0_resv123:	BTRAP(0x123) BTRAP(0x124) BTRAP(0x125) BTRAP(0x126) BTRAP(0x127)
tl0_resv128:	BTRAP(0x128) BTRAP(0x129) BTRAP(0x12a) BTRAP(0x12b) BTRAP(0x12c)
tl0_resv12d:	BTRAP(0x12d) BTRAP(0x12e) BTRAP(0x12f) BTRAP(0x130) BTRAP(0x131)
tl0_resv132:	BTRAP(0x132) BTRAP(0x133) BTRAP(0x134) BTRAP(0x135) BTRAP(0x136)
tl0_resv137:	BTRAP(0x137) BTRAP(0x138) BTRAP(0x139) BTRAP(0x13a) BTRAP(0x13b)
tl0_resv13c:	BTRAP(0x13c) BTRAP(0x13d) BTRAP(0x13e) BTRAP(0x13f) BTRAP(0x140)
tl0_resv141:	BTRAP(0x141) BTRAP(0x142) BTRAP(0x143) BTRAP(0x144) BTRAP(0x145)
tl0_resv146:	BTRAP(0x146) BTRAP(0x147) BTRAP(0x148) BTRAP(0x149) BTRAP(0x14a)
tl0_resv14b:	BTRAP(0x14b) BTRAP(0x14c) BTRAP(0x14d) BTRAP(0x14e) BTRAP(0x14f)
tl0_resv150:	BTRAP(0x150) BTRAP(0x151) BTRAP(0x152) BTRAP(0x153) BTRAP(0x154)
tl0_resv155:	BTRAP(0x155) BTRAP(0x156) BTRAP(0x157) BTRAP(0x158) BTRAP(0x159)
tl0_resv15a:	BTRAP(0x15a) BTRAP(0x15b) BTRAP(0x15c) BTRAP(0x15d) BTRAP(0x15e)
tl0_resv15f:	BTRAP(0x15f) BTRAP(0x160) BTRAP(0x161) BTRAP(0x162) BTRAP(0x163)
tl0_resv164:	BTRAP(0x164) BTRAP(0x165) BTRAP(0x166) BTRAP(0x167) BTRAP(0x168)
tl0_resv169:	BTRAP(0x169) BTRAP(0x16a) BTRAP(0x16b) BTRAP(0x16c)
tl0_linux64:	LINUX_64BIT_SYSCALL_TRAP
tl0_gsctx:	TRAP(sparc64_get_context) TRAP(sparc64_set_context)
tl0_resv170:	KPROBES_TRAP(0x170) KPROBES_TRAP(0x171) KGDB_TRAP(0x172)
tl0_resv173:	BTRAP(0x173) BTRAP(0x174) BTRAP(0x175) BTRAP(0x176) BTRAP(0x177)
tl0_resv178:	BTRAP(0x178) BTRAP(0x179) BTRAP(0x17a) BTRAP(0x17b) BTRAP(0x17c)
tl0_resv17d:	BTRAP(0x17d) BTRAP(0x17e) BTRAP(0x17f)
#define BTRAPS(x) BTRAP(x) BTRAP(x+1) BTRAP(x+2) BTRAP(x+3) BTRAP(x+4) BTRAP(x+5) BTRAP(x+6) BTRAP(x+7)
tl0_resv180:	BTRAPS(0x180) BTRAPS(0x188)
tl0_resv190:	BTRAPS(0x190) BTRAPS(0x198)
tl0_resv1a0:	BTRAPS(0x1a0) BTRAPS(0x1a8)
tl0_resv1b0:	BTRAPS(0x1b0) BTRAPS(0x1b8)
tl0_resv1c0:	BTRAPS(0x1c0) BTRAPS(0x1c8)
tl0_resv1d0:	BTRAPS(0x1d0) BTRAPS(0x1d8)
tl0_resv1e0:	BTRAPS(0x1e0) BTRAPS(0x1e8)
tl0_resv1f0:	BTRAPS(0x1f0) BTRAPS(0x1f8)

sparc64_ttable_tl1:
tl1_resv000:	BOOT_KERNEL    BTRAPTL1(0x1) BTRAPTL1(0x2) BTRAPTL1(0x3)
tl1_resv004:	BTRAPTL1(0x4)  BTRAPTL1(0x5) BTRAPTL1(0x6) BTRAPTL1(0x7)
tl1_iax:	TRAP_NOSAVE(__spitfire_insn_access_exception_tl1)
tl1_itsb_4v:	SUN4V_ITSB_MISS
tl1_iae:	membar #Sync
		TRAP_NOSAVE_7INSNS(__spitfire_access_error)
tl1_resv00b:	BTRAPTL1(0xb) BTRAPTL1(0xc) BTRAPTL1(0xd) BTRAPTL1(0xe) BTRAPTL1(0xf)
tl1_ill:	TRAPTL1(do_ill_tl1)
tl1_privop:	BTRAPTL1(0x11)
tl1_resv012:	BTRAPTL1(0x12) BTRAPTL1(0x13) BTRAPTL1(0x14) BTRAPTL1(0x15)
tl1_resv016:	BTRAPTL1(0x16) BTRAPTL1(0x17) BTRAPTL1(0x18) BTRAPTL1(0x19)
tl1_resv01a:	BTRAPTL1(0x1a) BTRAPTL1(0x1b) BTRAPTL1(0x1c) BTRAPTL1(0x1d)
tl1_resv01e:	BTRAPTL1(0x1e) BTRAPTL1(0x1f)
tl1_fpdis:	TRAP_NOSAVE(do_fpdis)
tl1_fpieee:	TRAPTL1(do_fpieee_tl1)
tl1_fpother:	TRAPTL1(do_fpother_tl1)
tl1_tof:	TRAPTL1(do_tof_tl1)
tl1_cwin:	CLEAN_WINDOW
tl1_div0:	TRAPTL1(do_div0_tl1)
tl1_resv029:	BTRAPTL1(0x29) BTRAPTL1(0x2a) BTRAPTL1(0x2b) BTRAPTL1(0x2c)
tl1_resv02d:	BTRAPTL1(0x2d) BTRAPTL1(0x2e) BTRAPTL1(0x2f)
tl1_dax:	TRAP_NOSAVE(__spitfire_data_access_exception_tl1)
tl1_dtsb_4v:	SUN4V_DTSB_MISS
tl1_dae:	membar #Sync
		TRAP_NOSAVE_7INSNS(__spitfire_access_error)
tl1_resv033:	BTRAPTL1(0x33)
tl1_mna:	TRAP_NOSAVE(do_mna)
tl1_lddfmna:	TRAPTL1(do_lddfmna_tl1)
tl1_stdfmna:	TRAPTL1(do_stdfmna_tl1)
tl1_privact:	BTRAPTL1(0x37)
tl1_resv038:	BTRAPTL1(0x38) BTRAPTL1(0x39) BTRAPTL1(0x3a) BTRAPTL1(0x3b)
tl1_resv03c:	BTRAPTL1(0x3c) BTRAPTL1(0x3d) BTRAPTL1(0x3e) BTRAPTL1(0x3f)
tl1_resv040:	BTRAPTL1(0x40)
tl1_irq1:	TRAP_IRQ(do_irq_tl1, 1)  TRAP_IRQ(do_irq_tl1, 2)  TRAP_IRQ(do_irq_tl1, 3)
tl1_irq4:	TRAP_IRQ(do_irq_tl1, 4)  TRAP_IRQ(do_irq_tl1, 5)  TRAP_IRQ(do_irq_tl1, 6)
tl1_irq7:	TRAP_IRQ(do_irq_tl1, 7)  TRAP_IRQ(do_irq_tl1, 8)  TRAP_IRQ(do_irq_tl1, 9)
tl1_irq10:	TRAP_IRQ(do_irq_tl1, 10) TRAP_IRQ(do_irq_tl1, 11)
tl1_irq12:	TRAP_IRQ(do_irq_tl1, 12) TRAP_IRQ(do_irq_tl1, 13)
tl1_irq14:	TRAP_IRQ(do_irq_tl1, 14) TRAP_IRQ(do_irq_tl1, 15)
tl1_resv050:	BTRAPTL1(0x50) BTRAPTL1(0x51) BTRAPTL1(0x52) BTRAPTL1(0x53)
tl1_resv054:	BTRAPTL1(0x54) BTRAPTL1(0x55) BTRAPTL1(0x56) BTRAPTL1(0x57)
tl1_resv058:	BTRAPTL1(0x58) BTRAPTL1(0x59) BTRAPTL1(0x5a) BTRAPTL1(0x5b)
tl1_resv05c:	BTRAPTL1(0x5c) BTRAPTL1(0x5d) BTRAPTL1(0x5e) BTRAPTL1(0x5f)
tl1_ivec:	TRAP_IVEC
tl1_paw:	TRAPTL1(do_paw_tl1)
tl1_vaw:	TRAPTL1(do_vaw_tl1)
tl1_cee:	BTRAPTL1(0x63)
tl1_iamiss:	BTRAPTL1(0x64) BTRAPTL1(0x65) BTRAPTL1(0x66) BTRAPTL1(0x67)
tl1_damiss:
#include	"dtlb_miss.S"
tl1_daprot:
#include	"dtlb_prot.S"
tl1_fecc:	BTRAPTL1(0x70)	/* Fast-ECC on Cheetah */
tl1_dcpe:	BTRAPTL1(0x71)	/* D-cache Parity Error on Cheetah+ */
tl1_icpe:	BTRAPTL1(0x72)	/* I-cache Parity Error on Cheetah+ */
tl1_resv073:	BTRAPTL1(0x73)
tl1_resv074:	BTRAPTL1(0x74) BTRAPTL1(0x75) BTRAPTL1(0x76) BTRAPTL1(0x77)
tl1_resv078:	BTRAPTL1(0x78) BTRAPTL1(0x79) BTRAPTL1(0x7a) BTRAPTL1(0x7b)
tl1_resv07c:	BTRAPTL1(0x7c) BTRAPTL1(0x7d) BTRAPTL1(0x7e) BTRAPTL1(0x7f)
tl1_s0n:	SPILL_0_NORMAL
tl1_s1n:	SPILL_1_NORMAL
tl1_s2n:	SPILL_2_NORMAL
tl1_s3n:	SPILL_3_NORMAL
tl1_s4n:	SPILL_4_NORMAL
tl1_s5n:	SPILL_5_NORMAL
tl1_s6n:	SPILL_6_NORMAL
tl1_s7n:	SPILL_7_NORMAL
tl1_s0o:	SPILL_0_OTHER
tl1_s1o:	SPILL_1_OTHER
tl1_s2o:	SPILL_2_OTHER
tl1_s3o:	SPILL_3_OTHER
tl1_s4o:	SPILL_4_OTHER
tl1_s5o:	SPILL_5_OTHER
tl1_s6o:	SPILL_6_OTHER
tl1_s7o:	SPILL_7_OTHER
tl1_f0n:	FILL_0_NORMAL
tl1_f1n:	FILL_1_NORMAL
tl1_f2n:	FILL_2_NORMAL
tl1_f3n:	FILL_3_NORMAL
tl1_f4n:	FILL_4_NORMAL
tl1_f5n:	FILL_5_NORMAL
tl1_f6n:	FILL_6_NORMAL
tl1_f7n:	FILL_7_NORMAL
tl1_f0o:	FILL_0_OTHER
tl1_f1o:	FILL_1_OTHER
tl1_f2o:	FILL_2_OTHER
tl1_f3o:	FILL_3_OTHER
tl1_f4o:	FILL_4_OTHER
tl1_f5o:	FILL_5_OTHER
tl1_f6o:	FILL_6_OTHER
tl1_f7o:	FILL_7_OTHER
