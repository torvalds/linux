/* SPDX-License-Identifier: GPL-2.0-only */
/*******************************************************************************

  EZNPS CPU startup Code
  Copyright(c) 2012 EZchip Technologies.


*******************************************************************************/
#include <linux/linkage.h>
#include <asm/entry.h>
#include <asm/cache.h>
#include <plat/ctop.h>

	.cpu A7

	.section .init.text, "ax",@progbits
	.align 1024	; HW requierment for restart first PC

ENTRY(res_service)
#if defined(CONFIG_EZNPS_MTM_EXT) && defined(CONFIG_EZNPS_SHARED_AUX_REGS)
	; There is no work for HW thread id != 0
	lr	r3, [CTOP_AUX_THREAD_ID]
	cmp	r3, 0
	jne	stext
#endif

#ifdef CONFIG_ARC_HAS_DCACHE
	; With no cache coherency mechanism D$ need to be used very carefully.
	; Address space:
	; 0G-2G: We disable CONFIG_ARC_CACHE_PAGES.
	; 2G-3G: We disable D$ by setting this bit.
	; 3G-4G: D$ is disabled by architecture.
	; FMT are huge pages for user application reside at 0-2G.
	; Only FMT left as one who can use D$ where each such page got
	; disable/enable bit for cachability.
	; Programmer will use FMT pages for private data so cache coherency
	; would not be a problem.
	; First thing we invalidate D$
	sr	1, [ARC_REG_DC_IVDC]
	sr	HW_COMPLY_KRN_NOT_D_CACHED, [CTOP_AUX_HW_COMPLY]
#endif

#ifdef CONFIG_SMP
	; We set logical cpuid to be used by GET_CPUID
	; We do not use physical cpuid since we want ids to be continious when
	; it comes to cpus on the same quad cluster.
	; This is useful for applications that used shared resources of a quad
	; cluster such SRAMS.
	lr 	r3, [CTOP_AUX_CORE_ID]
	sr	r3, [CTOP_AUX_LOGIC_CORE_ID]
	lr	r3, [CTOP_AUX_CLUSTER_ID]
	; Set logical is acheived by swap of 2 middle bits of cluster id (4 bit)
	; r3 is used since we use short instruction and we need q-class reg
	.short	CTOP_INST_MOV2B_FLIP_R3_B1_B2_INST
	.word 	CTOP_INST_MOV2B_FLIP_R3_B1_B2_LIMM
	 sr	r3, [CTOP_AUX_LOGIC_CLUSTER_ID]
#endif

	j	stext
END(res_service)
