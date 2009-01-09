/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005-2008 Cavium Networks, Inc
 */
#ifndef __ASM_MACH_CAVIUM_OCTEON_KERNEL_ENTRY_H
#define __ASM_MACH_CAVIUM_OCTEON_KERNEL_ENTRY_H


#define CP0_CYCLE_COUNTER $9, 6
#define CP0_CVMCTL_REG $9, 7
#define CP0_CVMMEMCTL_REG $11,7
#define CP0_PRID_REG $15, 0
#define CP0_PRID_OCTEON_PASS1 0x000d0000
#define CP0_PRID_OCTEON_CN30XX 0x000d0200

.macro  kernel_entry_setup
	# Registers set by bootloader:
	# (only 32 bits set by bootloader, all addresses are physical
	# addresses, and need to have the appropriate memory region set
	# by the kernel
	# a0 = argc
	# a1 = argv (kseg0 compat addr)
	# a2 = 1 if init core, zero otherwise
	# a3 = address of boot descriptor block
	.set push
	.set arch=octeon
	# Read the cavium mem control register
	dmfc0   v0, CP0_CVMMEMCTL_REG
	# Clear the lower 6 bits, the CVMSEG size
	dins    v0, $0, 0, 6
	ori     v0, CONFIG_CAVIUM_OCTEON_CVMSEG_SIZE
	dmtc0   v0, CP0_CVMMEMCTL_REG	# Write the cavium mem control register
	dmfc0   v0, CP0_CVMCTL_REG	# Read the cavium control register
#ifdef CONFIG_CAVIUM_OCTEON_HW_FIX_UNALIGNED
	# Disable unaligned load/store support but leave HW fixup enabled
	or  v0, v0, 0x5001
	xor v0, v0, 0x1001
#else
	# Disable unaligned load/store and HW fixup support
	or  v0, v0, 0x5001
	xor v0, v0, 0x5001
#endif
	# Read the processor ID register
	mfc0 v1, CP0_PRID_REG
	# Disable instruction prefetching (Octeon Pass1 errata)
	or  v0, v0, 0x2000
	# Skip reenable of prefetching for Octeon Pass1
	beq v1, CP0_PRID_OCTEON_PASS1, skip
	nop
	# Reenable instruction prefetching, not on Pass1
	xor v0, v0, 0x2000
	# Strip off pass number off of processor id
	srl v1, 8
	sll v1, 8
	# CN30XX needs some extra stuff turned off for better performance
	bne v1, CP0_PRID_OCTEON_CN30XX, skip
	nop
	# CN30XX Use random Icache replacement
	or  v0, v0, 0x400
	# CN30XX Disable instruction prefetching
	or  v0, v0, 0x2000
skip:
	# Write the cavium control register
	dmtc0   v0, CP0_CVMCTL_REG
	sync
	# Flush dcache after config change
	cache   9, 0($0)
	# Get my core id
	rdhwr   v0, $0
	# Jump the master to kernel_entry
	bne     a2, zero, octeon_main_processor
	nop

#ifdef CONFIG_SMP

	#
	# All cores other than the master need to wait here for SMP bootstrap
	# to begin
	#

	# This is the variable where the next core to boot os stored
	PTR_LA  t0, octeon_processor_boot
octeon_spin_wait_boot:
	# Get the core id of the next to be booted
	LONG_L  t1, (t0)
	# Keep looping if it isn't me
	bne t1, v0, octeon_spin_wait_boot
	nop
	# Get my GP from the global variable
	PTR_LA  t0, octeon_processor_gp
	LONG_L  gp, (t0)
	# Get my SP from the global variable
	PTR_LA  t0, octeon_processor_sp
	LONG_L  sp, (t0)
	# Set the SP global variable to zero so the master knows we've started
	LONG_S  zero, (t0)
#ifdef __OCTEON__
	syncw
	syncw
#else
	sync
#endif
	# Jump to the normal Linux SMP entry point
	j   smp_bootstrap
	nop
#else /* CONFIG_SMP */

	#
	# Someone tried to boot SMP with a non SMP kernel. All extra cores
	# will halt here.
	#
octeon_wait_forever:
	wait
	b   octeon_wait_forever
	nop

#endif /* CONFIG_SMP */
octeon_main_processor:
	.set pop
.endm

/*
 * Do SMP slave processor setup necessary before we can savely execute C code.
 */
	.macro  smp_slave_setup
	.endm

#endif /* __ASM_MACH_CAVIUM_OCTEON_KERNEL_ENTRY_H */
