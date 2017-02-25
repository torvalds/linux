/*
 *  Emulation of the "brl" instruction for IA64 processors that
 *  don't support it in hardware.
 *  Author: Stephan Zeisset, Intel Corp. <Stephan.Zeisset@intel.com>
 *
 *    02/22/02	D. Mosberger	Clear si_flgs, si_isr, and si_imm to avoid
 *				leaking kernel bits.
 */

#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/uaccess.h>
#include <asm/processor.h>

extern char ia64_set_b1, ia64_set_b2, ia64_set_b3, ia64_set_b4, ia64_set_b5;

struct illegal_op_return {
	unsigned long fkt, arg1, arg2, arg3;
};

/*
 *  The unimplemented bits of a virtual address must be set
 *  to the value of the most significant implemented bit.
 *  unimpl_va_mask includes all unimplemented bits and
 *  the most significant implemented bit, so the result
 *  of an and operation with the mask must be all 0's
 *  or all 1's for the address to be valid.
 */
#define unimplemented_virtual_address(va) (						\
	((va) & local_cpu_data->unimpl_va_mask) != 0 &&					\
	((va) & local_cpu_data->unimpl_va_mask) != local_cpu_data->unimpl_va_mask	\
)

/*
 *  The unimplemented bits of a physical address must be 0.
 *  unimpl_pa_mask includes all unimplemented bits, so the result
 *  of an and operation with the mask must be all 0's for the
 *  address to be valid.
 */
#define unimplemented_physical_address(pa) (		\
	((pa) & local_cpu_data->unimpl_pa_mask) != 0	\
)

/*
 *  Handle an illegal operation fault that was caused by an
 *  unimplemented "brl" instruction.
 *  If we are not successful (e.g because the illegal operation
 *  wasn't caused by a "brl" after all), we return -1.
 *  If we are successful, we return either 0 or the address
 *  of a "fixup" function for manipulating preserved register
 *  state.
 */

struct illegal_op_return
ia64_emulate_brl (struct pt_regs *regs, unsigned long ar_ec)
{
	unsigned long bundle[2];
	unsigned long opcode, btype, qp, offset, cpl;
	unsigned long next_ip;
	struct siginfo siginfo;
	struct illegal_op_return rv;
	long tmp_taken, unimplemented_address;

	rv.fkt = (unsigned long) -1;

	/*
	 *  Decode the instruction bundle.
	 */

	if (copy_from_user(bundle, (void *) (regs->cr_iip), sizeof(bundle)))
		return rv;

	next_ip = (unsigned long) regs->cr_iip + 16;

	/* "brl" must be in slot 2. */
	if (ia64_psr(regs)->ri != 1) return rv;

	/* Must be "mlx" template */
	if ((bundle[0] & 0x1e) != 0x4) return rv;

	opcode = (bundle[1] >> 60);
	btype = ((bundle[1] >> 29) & 0x7);
	qp = ((bundle[1] >> 23) & 0x3f);
	offset = ((bundle[1] & 0x0800000000000000L) << 4)
		| ((bundle[1] & 0x00fffff000000000L) >> 32)
		| ((bundle[1] & 0x00000000007fffffL) << 40)
		| ((bundle[0] & 0xffff000000000000L) >> 24);

	tmp_taken = regs->pr & (1L << qp);

	switch(opcode) {

		case 0xC:
			/*
			 *  Long Branch.
			 */
			if (btype != 0) return rv;
			rv.fkt = 0;
			if (!(tmp_taken)) {
				/*
				 *  Qualifying predicate is 0.
				 *  Skip instruction.
				 */
				regs->cr_iip = next_ip;
				ia64_psr(regs)->ri = 0;
				return rv;
			}
			break;

		case 0xD:
			/*
			 *  Long Call.
			 */
			rv.fkt = 0;
			if (!(tmp_taken)) {
				/*
				 *  Qualifying predicate is 0.
				 *  Skip instruction.
				 */
				regs->cr_iip = next_ip;
				ia64_psr(regs)->ri = 0;
				return rv;
			}

			/*
			 *  BR[btype] = IP+16
			 */
			switch(btype) {
				case 0:
					regs->b0 = next_ip;
					break;
				case 1:
					rv.fkt = (unsigned long) &ia64_set_b1;
					break;
				case 2:
					rv.fkt = (unsigned long) &ia64_set_b2;
					break;
				case 3:
					rv.fkt = (unsigned long) &ia64_set_b3;
					break;
				case 4:
					rv.fkt = (unsigned long) &ia64_set_b4;
					break;
				case 5:
					rv.fkt = (unsigned long) &ia64_set_b5;
					break;
				case 6:
					regs->b6 = next_ip;
					break;
				case 7:
					regs->b7 = next_ip;
					break;
			}
			rv.arg1 = next_ip;

			/*
			 *  AR[PFS].pfm = CFM
			 *  AR[PFS].pec = AR[EC]
			 *  AR[PFS].ppl = PSR.cpl
			 */
			cpl = ia64_psr(regs)->cpl;
			regs->ar_pfs = ((regs->cr_ifs & 0x3fffffffff)
					| (ar_ec << 52) | (cpl << 62));

			/*
			 *  CFM.sof -= CFM.sol
			 *  CFM.sol = 0
			 *  CFM.sor = 0
			 *  CFM.rrb.gr = 0
			 *  CFM.rrb.fr = 0
			 *  CFM.rrb.pr = 0
			 */
			regs->cr_ifs = ((regs->cr_ifs & 0xffffffc00000007f)
					- ((regs->cr_ifs >> 7) & 0x7f));

			break;

		default:
			/*
			 *  Unknown opcode.
			 */
			return rv;

	}

	regs->cr_iip += offset;
	ia64_psr(regs)->ri = 0;

	if (ia64_psr(regs)->it == 0)
		unimplemented_address = unimplemented_physical_address(regs->cr_iip);
	else
		unimplemented_address = unimplemented_virtual_address(regs->cr_iip);

	if (unimplemented_address) {
		/*
		 *  The target address contains unimplemented bits.
		 */
		printk(KERN_DEBUG "Woah! Unimplemented Instruction Address Trap!\n");
		siginfo.si_signo = SIGILL;
		siginfo.si_errno = 0;
		siginfo.si_flags = 0;
		siginfo.si_isr = 0;
		siginfo.si_imm = 0;
		siginfo.si_code = ILL_BADIADDR;
		force_sig_info(SIGILL, &siginfo, current);
	} else if (ia64_psr(regs)->tb) {
		/*
		 *  Branch Tracing is enabled.
		 *  Force a taken branch signal.
		 */
		siginfo.si_signo = SIGTRAP;
		siginfo.si_errno = 0;
		siginfo.si_code = TRAP_BRANCH;
		siginfo.si_flags = 0;
		siginfo.si_isr = 0;
		siginfo.si_addr = 0;
		siginfo.si_imm = 0;
		force_sig_info(SIGTRAP, &siginfo, current);
	} else if (ia64_psr(regs)->ss) {
		/*
		 *  Single Step is enabled.
		 *  Force a trace signal.
		 */
		siginfo.si_signo = SIGTRAP;
		siginfo.si_errno = 0;
		siginfo.si_code = TRAP_TRACE;
		siginfo.si_flags = 0;
		siginfo.si_isr = 0;
		siginfo.si_addr = 0;
		siginfo.si_imm = 0;
		force_sig_info(SIGTRAP, &siginfo, current);
	}
	return rv;
}
