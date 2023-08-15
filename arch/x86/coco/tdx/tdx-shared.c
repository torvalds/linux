#include <asm/tdx.h>
#include <asm/pgtable.h>

static unsigned long try_accept_one(phys_addr_t start, unsigned long len,
				    enum pg_level pg_level)
{
	unsigned long accept_size = page_level_size(pg_level);
	struct tdx_module_args args = {};
	u8 page_size;

	if (!IS_ALIGNED(start, accept_size))
		return 0;

	if (len < accept_size)
		return 0;

	/*
	 * Pass the page physical address to the TDX module to accept the
	 * pending, private page.
	 *
	 * Bits 2:0 of RCX encode page size: 0 - 4K, 1 - 2M, 2 - 1G.
	 */
	switch (pg_level) {
	case PG_LEVEL_4K:
		page_size = 0;
		break;
	case PG_LEVEL_2M:
		page_size = 1;
		break;
	case PG_LEVEL_1G:
		page_size = 2;
		break;
	default:
		return 0;
	}

	args.rcx = start | page_size;
	if (__tdcall(TDG_MEM_PAGE_ACCEPT, &args))
		return 0;

	return accept_size;
}

bool tdx_accept_memory(phys_addr_t start, phys_addr_t end)
{
	/*
	 * For shared->private conversion, accept the page using
	 * TDG_MEM_PAGE_ACCEPT TDX module call.
	 */
	while (start < end) {
		unsigned long len = end - start;
		unsigned long accept_size;

		/*
		 * Try larger accepts first. It gives chance to VMM to keep
		 * 1G/2M Secure EPT entries where possible and speeds up
		 * process by cutting number of hypercalls (if successful).
		 */

		accept_size = try_accept_one(start, len, PG_LEVEL_1G);
		if (!accept_size)
			accept_size = try_accept_one(start, len, PG_LEVEL_2M);
		if (!accept_size)
			accept_size = try_accept_one(start, len, PG_LEVEL_4K);
		if (!accept_size)
			return false;
		start += accept_size;
	}

	return true;
}

noinstr u64 __tdx_hypercall(struct tdx_hypercall_args *args)
{
	struct tdx_module_args margs = {
		.rcx = TDVMCALL_EXPOSE_REGS_MASK,
		.rdx = args->rdx,
		.r8  = args->r8,
		.r9  = args->r9,
		.r10 = args->r10,
		.r11 = args->r11,
		.r12 = args->r12,
		.r13 = args->r13,
		.r14 = args->r14,
		.r15 = args->r15,
		.rbx = args->rbx,
		.rdi = args->rdi,
		.rsi = args->rsi,
	};

	/*
	 * Failure of __tdcall_saved_ret() indicates a failure of the TDVMCALL
	 * mechanism itself and that something has gone horribly wrong with
	 * the TDX module.  __tdx_hypercall_failed() never returns.
	 */
	if (__tdcall_saved_ret(TDG_VP_VMCALL, &margs))
		__tdx_hypercall_failed();

	args->r8  = margs.r8;
	args->r9  = margs.r9;
	args->r10 = margs.r10;
	args->r11 = margs.r11;
	args->r12 = margs.r12;
	args->r13 = margs.r13;
	args->r14 = margs.r14;
	args->r15 = margs.r15;
	args->rdi = margs.rdi;
	args->rsi = margs.rsi;
	args->rbx = margs.rbx;
	args->rdx = margs.rdx;

	/* TDVMCALL leaf return code is in R10 */
	return args->r10;
}
