/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/linkage.h>
#include <asm/asm.h>

	.text

/**
 * vmx_vmenter - VM-Enter the current loaded VMCS
 *
 * %RFLAGS.ZF:	!VMCS.LAUNCHED, i.e. controls VMLAUNCH vs. VMRESUME
 *
 * Returns:
 *	%RFLAGS.CF is set on VM-Fail Invalid
 *	%RFLAGS.ZF is set on VM-Fail Valid
 *	%RFLAGS.{CF,ZF} are cleared on VM-Success, i.e. VM-Exit
 *
 * Note that VMRESUME/VMLAUNCH fall-through and return directly if
 * they VM-Fail, whereas a successful VM-Enter + VM-Exit will jump
 * to vmx_vmexit.
 */
ENTRY(vmx_vmenter)
	/* EFLAGS.ZF is set if VMCS.LAUNCHED == 0 */
	je 2f

1:	vmresume
	ret

2:	vmlaunch
	ret

3:	cmpb $0, kvm_rebooting
	jne 4f
	call kvm_spurious_fault
4:	ret

	.pushsection .fixup, "ax"
5:	jmp 3b
	.popsection

	_ASM_EXTABLE(1b, 5b)
	_ASM_EXTABLE(2b, 5b)

ENDPROC(vmx_vmenter)

/**
 * vmx_vmexit - Handle a VMX VM-Exit
 *
 * Returns:
 *	%RFLAGS.{CF,ZF} are cleared on VM-Success, i.e. VM-Exit
 *
 * This is vmx_vmenter's partner in crime.  On a VM-Exit, control will jump
 * here after hardware loads the host's state, i.e. this is the destination
 * referred to by VMCS.HOST_RIP.
 */
ENTRY(vmx_vmexit)
	ret
ENDPROC(vmx_vmexit)
