#include <linux/mm.h>
#include <linux/init.h>
#include <asm/io.h>
#include <asm/mtrr.h>
#include <asm/msr.h>
#include <asm/processor-cyrix.h>
#include <asm/processor-flags.h>
#include "mtrr.h"


/*  Put the processor into a state where MTRRs can be safely set  */
void set_mtrr_prepare_save(struct set_mtrr_context *ctxt)
{
	unsigned int cr0;

	/*  Disable interrupts locally  */
	local_irq_save(ctxt->flags);

	if (use_intel() || is_cpu(CYRIX)) {

		/*  Save value of CR4 and clear Page Global Enable (bit 7)  */
		if ( cpu_has_pge ) {
			ctxt->cr4val = read_cr4();
			write_cr4(ctxt->cr4val & ~X86_CR4_PGE);
		}

		/*  Disable and flush caches. Note that wbinvd flushes the TLBs as
		    a side-effect  */
		cr0 = read_cr0() | X86_CR0_CD;
		wbinvd();
		write_cr0(cr0);
		wbinvd();

		if (use_intel())
			/*  Save MTRR state */
			rdmsr(MTRRdefType_MSR, ctxt->deftype_lo, ctxt->deftype_hi);
		else
			/* Cyrix ARRs - everything else were excluded at the top */
			ctxt->ccr3 = getCx86(CX86_CCR3);
	}
}

void set_mtrr_cache_disable(struct set_mtrr_context *ctxt)
{
	if (use_intel()) 
		/*  Disable MTRRs, and set the default type to uncached  */
		mtrr_wrmsr(MTRRdefType_MSR, ctxt->deftype_lo & 0xf300UL,
		      ctxt->deftype_hi);
	else if (is_cpu(CYRIX))
		/* Cyrix ARRs - everything else were excluded at the top */
		setCx86(CX86_CCR3, (ctxt->ccr3 & 0x0f) | 0x10);
}

/*  Restore the processor after a set_mtrr_prepare  */
void set_mtrr_done(struct set_mtrr_context *ctxt)
{
	if (use_intel() || is_cpu(CYRIX)) {

		/*  Flush caches and TLBs  */
		wbinvd();

		/*  Restore MTRRdefType  */
		if (use_intel())
			/* Intel (P6) standard MTRRs */
			mtrr_wrmsr(MTRRdefType_MSR, ctxt->deftype_lo, ctxt->deftype_hi);
		else
			/* Cyrix ARRs - everything else was excluded at the top */
			setCx86(CX86_CCR3, ctxt->ccr3);
		
		/*  Enable caches  */
		write_cr0(read_cr0() & 0xbfffffff);

		/*  Restore value of CR4  */
		if ( cpu_has_pge )
			write_cr4(ctxt->cr4val);
	}
	/*  Re-enable interrupts locally (if enabled previously)  */
	local_irq_restore(ctxt->flags);
}

