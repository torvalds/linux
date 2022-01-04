/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2005-2008 Cavium Networks, Inc
 */
#ifndef __ASM_MACH_CAVIUM_OCTEON_KERNEL_ENTRY_H
#define __ASM_MACH_CAVIUM_OCTEON_KERNEL_ENTRY_H

#define CP0_CVMCTL_REG $9, 7
#define CP0_CVMMEMCTL_REG $11,7
#define CP0_PRID_REG $15, 0
#define CP0_DCACHE_ERR_REG $27, 1
#define CP0_PRID_OCTEON_PASS1 0x000d0000
#define CP0_PRID_OCTEON_CN30XX 0x000d0200

.macro	kernel_entry_setup
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
	dmfc0	v0, CP0_CVMMEMCTL_REG
	# Clear the lower 6 bits, the CVMSEG size
	dins	v0, $0, 0, 6
	ori	v0, CONFIG_CAVIUM_OCTEON_CVMSEG_SIZE
	dmtc0	v0, CP0_CVMMEMCTL_REG	# Write the cavium mem control register
	dmfc0	v0, CP0_CVMCTL_REG	# Read the cavium control register
	# Disable unaligned load/store support but leave HW fixup enabled
	# Needed for octeon specific memcpy
	or  v0, v0, 0x5001
	xor v0, v0, 0x1001
	# First clear off CvmCtl[IPPCI] bit and move the performance
	# counters interrupt to IRQ 6
	dli	v1, ~(7 << 7)
	and	v0, v0, v1
	ori	v0, v0, (6 << 7)

	mfc0	v1, CP0_PRID_REG
	and	t1, v1, 0xfff8
	xor	t1, t1, 0x9000		# 63-P1
	beqz	t1, 4f
	and	t1, v1, 0xfff8
	xor	t1, t1, 0x9008		# 63-P2
	beqz	t1, 4f
	and	t1, v1, 0xfff8
	xor	t1, t1, 0x9100		# 68-P1
	beqz	t1, 4f
	and	t1, v1, 0xff00
	xor	t1, t1, 0x9200		# 66-PX
	bnez	t1, 5f			# Skip WAR for others.
	and	t1, v1, 0x00ff
	slti	t1, t1, 2		# 66-P1.2 and later good.
	beqz	t1, 5f

4:	# core-16057 work around
	or	v0, v0, 0x2000		# Set IPREF bit.

5:	# No core-16057 work around
	# Write the cavium control register
	dmtc0	v0, CP0_CVMCTL_REG
	sync
	# Flush dcache after config change
	cache	9, 0($0)
	# Zero all of CVMSEG to make sure parity is correct
	dli	v0, CONFIG_CAVIUM_OCTEON_CVMSEG_SIZE
	dsll	v0, 7
	beqz	v0, 2f
1:	dsubu	v0, 8
	sd	$0, -32768(v0)
	bnez	v0, 1b
2:
	mfc0	v0, CP0_PRID_REG
	bbit0	v0, 15, 1f
	# OCTEON II or better have bit 15 set.  Clear the error bits.
	and	t1, v0, 0xff00
	dli	v0, 0x9500
	bge	t1, v0, 1f  # OCTEON III has no DCACHE_ERR_REG COP0
	dli	v0, 0x27
	dmtc0	v0, CP0_DCACHE_ERR_REG
1:
	# Get my core id
	rdhwr	v0, $0
	# Jump the master to kernel_entry
	bne	a2, zero, octeon_main_processor
	nop

#ifdef CONFIG_SMP

	#
	# All cores other than the master need to wait here for SMP bootstrap
	# to begin
	#

octeon_spin_wait_boot:
#ifdef CONFIG_RELOCATABLE
	PTR_LA	t0, octeon_processor_relocated_kernel_entry
	LONG_L	t0, (t0)
	beq	zero, t0, 1f
	nop

	jr	t0
	nop
1:
#endif /* CONFIG_RELOCATABLE */

	# This is the variable where the next core to boot is stored
	PTR_LA	t0, octeon_processor_boot
	# Get the core id of the next to be booted
	LONG_L	t1, (t0)
	# Keep looping if it isn't me
	bne t1, v0, octeon_spin_wait_boot
	nop
	# Get my GP from the global variable
	PTR_LA	t0, octeon_processor_gp
	LONG_L	gp, (t0)
	# Get my SP from the global variable
	PTR_LA	t0, octeon_processor_sp
	LONG_L	sp, (t0)
	# Set the SP global variable to zero so the master knows we've started
	LONG_S	zero, (t0)
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
 * Do SMP slave processor setup necessary before we can safely execute C code.
 */
	.macro	smp_slave_setup
	.endm

#define USE_KEXEC_SMP_WAIT_FINAL
	.macro  kexec_smp_wait_final
	.set push
	.set noreorder
	synci		0($0)
	.set pop
	.endm

#endif /* __ASM_MACH_CAVIUM_OCTEON_KERNEL_ENTRY_H */
