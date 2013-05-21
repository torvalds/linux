KVM/MIPS Trap & Emulate Release Notes
=====================================

(1) KVM/MIPS should support MIPS32R2 and beyond. It has been tested on the following platforms:
    Malta Board with FPGA based 34K
    Sigma Designs TangoX board with a 24K based 8654 SoC.
    Malta Board with 74K @ 1GHz

(2) Both Guest kernel and Guest Userspace execute in UM.
    Guest User address space:   0x00000000 -> 0x40000000
    Guest Kernel Unmapped:      0x40000000 -> 0x60000000
    Guest Kernel Mapped:        0x60000000 -> 0x80000000

    Guest Usermode virtual memory is limited to 1GB.

(2) 16K Page Sizes: Both Host Kernel and Guest Kernel should have the same page size, currently at least 16K.
    Note that due to cache aliasing issues, 4K page sizes are NOT supported.

(3) No HugeTLB Support
    Both the host kernel and Guest kernel should have the page size set to 16K.
    This will be implemented in a future release.

(4) KVM/MIPS does not have support for SMP Guests
    Linux-3.7-rc2 based SMP guest hangs due to the following code sequence in the generated TLB handlers:
	LL/TLBP/SC.  Since the TLBP instruction causes a trap the reservation gets cleared
	when we ERET back to the guest. This causes the guest to hang in an infinite loop.
	This will be fixed in a future release.

(5) Use Host FPU
    Currently KVM/MIPS emulates a 24K CPU without a FPU.
    This will be fixed in a future release
