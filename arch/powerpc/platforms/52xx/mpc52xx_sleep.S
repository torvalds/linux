#include <asm/reg.h>
#include <asm/ppc_asm.h>
#include <asm/processor.h>


.text

_GLOBAL(mpc52xx_deep_sleep)
mpc52xx_deep_sleep: /* args r3-r6: SRAM, SDRAM regs, CDM regs, INTR regs */

	/* enable interrupts */
	mfmsr	r7
	ori	r7, r7, 0x8000 /* EE */
	mtmsr	r7
	sync; isync;

	li	r10, 0 /* flag that irq handler sets */

	/* enable tmr7 (or any other) interrupt */
	lwz	r8, 0x14(r6) /* intr->main_mask */
	ori	r8, r8, 0x1
	xori	r8, r8, 0x1
	stw	r8, 0x14(r6)
	sync

	/* emulate tmr7 interrupt */
	li	r8, 0x1
	stw	r8, 0x40(r6) /* intr->main_emulate */
	sync

	/* wait for it to happen */
1:
	cmpi	cr0, r10, 1
	bne	cr0, 1b

	/* lock icache */
	mfspr	r10, SPRN_HID0
	ori	r10, r10, 0x2000
	sync; isync;
	mtspr	SPRN_HID0, r10
	sync; isync;


	mflr	r9 /* save LR */

	/* jump to sram */
	mtlr	r3
	blrl

	mtlr	r9 /* restore LR */

	/* unlock icache */
	mfspr	r10, SPRN_HID0
	ori	r10, r10, 0x2000
	xori	r10, r10, 0x2000
	sync; isync;
	mtspr	SPRN_HID0, r10
	sync; isync;


	/* return to C code */
	blr


_GLOBAL(mpc52xx_ds_sram)
mpc52xx_ds_sram:
	/* put SDRAM into self-refresh */
	lwz	r8, 0x4(r4)	/* sdram->ctrl */

	oris	r8, r8, 0x8000 /* mode_en */
	stw	r8, 0x4(r4)
	sync

	ori	r8, r8, 0x0002 /* soft_pre */
	stw	r8, 0x4(r4)
	sync
	xori	r8, r8, 0x0002

	xoris	r8, r8, 0x8000 /* !mode_en */
	stw	r8, 0x4(r4)
	sync

	oris	r8, r8, 0x5000
	xoris	r8, r8, 0x4000 /* ref_en !cke */
	stw	r8, 0x4(r4)
	sync

	/* disable SDRAM clock */
	lwz	r8, 0x14(r5) /* cdm->clkenable */
	ori	r8, r8, 0x0008
	xori	r8, r8, 0x0008
	stw	r8, 0x14(r5)
	sync


	/* put mpc5200 to sleep */
	mfmsr	r10
	oris	r10, r10, 0x0004	/* POW = 1 */
	sync; isync;
	mtmsr	r10
	sync; isync;


	/* enable clock */
	lwz	r8, 0x14(r5)
	ori	r8, r8, 0x0008
	stw	r8, 0x14(r5)
	sync

	/* get ram out of self-refresh */
	lwz	r8, 0x4(r4)
	oris	r8, r8, 0x5000 /* cke ref_en */
	stw	r8, 0x4(r4)
	sync

	blr
_GLOBAL(mpc52xx_ds_sram_size)
mpc52xx_ds_sram_size:
	.long $-mpc52xx_ds_sram


/* ### interrupt handler for wakeup from deep-sleep ### */
_GLOBAL(mpc52xx_ds_cached)
mpc52xx_ds_cached:
	mtspr	SPRN_SPRG0, r7
	mtspr	SPRN_SPRG1, r8

	/* disable emulated interrupt */
	mfspr	r7, 311 /* MBAR */
	addi	r7, r7, 0x540	/* intr->main_emul */
	li	r8, 0
	stw	r8, 0(r7)
	sync
	dcbf	0, r7

	/* acknowledge wakeup, so CCS releases power pown */
	mfspr	r7, 311	/* MBAR */
	addi	r7, r7, 0x524	/* intr->enc_status */
	lwz	r8, 0(r7)
	ori	r8, r8, 0x0400
	stw	r8, 0(r7)
	sync
	dcbf	0, r7

	/* flag - we handled the interrupt */
	li	r10, 1

	mfspr	r8, SPRN_SPRG1
	mfspr	r7, SPRN_SPRG0

	rfi
_GLOBAL(mpc52xx_ds_cached_size)
mpc52xx_ds_cached_size:
	.long $-mpc52xx_ds_cached
