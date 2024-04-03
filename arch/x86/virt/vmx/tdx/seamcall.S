/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/frame.h>

#include "tdxcall.S"

/*
 * __seamcall() - Host-side interface functions to SEAM software
 * (the P-SEAMLDR or the TDX module).
 *
 * __seamcall() function ABI:
 *
 * @fn   (RDI)  - SEAMCALL Leaf number, moved to RAX
 * @args (RSI)  - struct tdx_module_args for input
 *
 * Only RCX/RDX/R8-R11 are used as input registers.
 *
 * Return (via RAX) TDX_SEAMCALL_VMFAILINVALID if the SEAMCALL itself
 * fails, or the completion status of the SEAMCALL leaf function.
 */
SYM_FUNC_START(__seamcall)
	TDX_MODULE_CALL host=1
SYM_FUNC_END(__seamcall)

/*
 * __seamcall_ret() - Host-side interface functions to SEAM software
 * (the P-SEAMLDR or the TDX module), with saving output registers to
 * the 'struct tdx_module_args' used as input.
 *
 * __seamcall_ret() function ABI:
 *
 * @fn   (RDI)  - SEAMCALL Leaf number, moved to RAX
 * @args (RSI)  - struct tdx_module_args for input and output
 *
 * Only RCX/RDX/R8-R11 are used as input/output registers.
 *
 * Return (via RAX) TDX_SEAMCALL_VMFAILINVALID if the SEAMCALL itself
 * fails, or the completion status of the SEAMCALL leaf function.
 */
SYM_FUNC_START(__seamcall_ret)
	TDX_MODULE_CALL host=1 ret=1
SYM_FUNC_END(__seamcall_ret)

/*
 * __seamcall_saved_ret() - Host-side interface functions to SEAM software
 * (the P-SEAMLDR or the TDX module), with saving output registers to the
 * 'struct tdx_module_args' used as input.
 *
 * __seamcall_saved_ret() function ABI:
 *
 * @fn   (RDI)  - SEAMCALL Leaf number, moved to RAX
 * @args (RSI)  - struct tdx_module_args for input and output
 *
 * All registers in @args are used as input/output registers.
 *
 * Return (via RAX) TDX_SEAMCALL_VMFAILINVALID if the SEAMCALL itself
 * fails, or the completion status of the SEAMCALL leaf function.
 */
SYM_FUNC_START(__seamcall_saved_ret)
	TDX_MODULE_CALL host=1 ret=1 saved=1
SYM_FUNC_END(__seamcall_saved_ret)
