// This file is dual licensed under the MIT and the University of Illinois Open
// Source Licenses. See LICENSE.TXT for details.

// Avoid being marked as needing an executable stack:
#if defined(__linux__) && defined(__ELF__)
.section .note.GNU-stack,"",%progbits
#endif

// Further contents are mips64 only:
#if defined(__linux__) && defined(__mips64)

.section .text
.set noreorder
.globl internal_sigreturn
.type internal_sigreturn, @function
internal_sigreturn:

        li $v0,5211 // #5211 is for SYS_rt_sigreturn
        syscall

.size internal_sigreturn, .-internal_sigreturn

#endif // defined(__linux__) && defined(__mips64)
