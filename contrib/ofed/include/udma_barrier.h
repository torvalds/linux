/*
 * Copyright (c) 2005 Topspin Communications.  All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef __UTIL_UDMA_BARRIER_H
#define __UTIL_UDMA_BARRIER_H

#include <pthread.h>

/* Barriers for DMA.

   These barriers are expliclty only for use with user DMA operations. If you
   are looking for barriers to use with cache-coherent multi-threaded
   consitency then look in stdatomic.h. If you need both kinds of synchronicity
   for the same address then use an atomic operation followed by one
   of these barriers.

   When reasoning about these barriers there are two objects:
     - CPU attached address space (the CPU memory could be a range of things:
       cached/uncached/non-temporal CPU DRAM, uncached MMIO space in another
       device, pMEM). Generally speaking the ordering is only relative
       to the local CPU's view of the system. Eg if the local CPU
       is not guaranteed to see a write from another CPU then it is also
       OK for the DMA device to also not see the write after the barrier.
     - A DMA initiator on a bus. For instance a PCI-E device issuing
       MemRd/MemWr TLPs.

   The ordering guarantee is always stated between those two streams. Eg what
   happens if a MemRd TLP is sent in via PCI-E relative to a CPU WRITE to the
   same memory location.

   The providers have a very regular and predictable use of these barriers,
   to make things very clear each narrow use is given a name and the proper
   name should be used in the provider as a form of documentation.
*/

/* Ensure that the device's view of memory matches the CPU's view of memory.
   This should be placed before any MMIO store that could trigger the device
   to begin doing DMA, such as a device doorbell ring.

   eg
    *dma_buf = 1;
    udma_to_device_barrier();
    mmio_write(DO_DMA_REG, dma_buf);
   Must ensure that the device sees the '1'.

   This is required to fence writes created by the libibverbs user. Those
   writes could be to any CPU mapped memory object with any cachability mode.

   NOTE: x86 has historically used a weaker semantic for this barrier, and
   only fenced normal stores to normal memory. libibverbs users using other
   memory types or non-temporal stores are required to use SFENCE in their own
   code prior to calling verbs to start a DMA.
*/
#if defined(__i386__)
#define udma_to_device_barrier() asm volatile("" ::: "memory")
#elif defined(__x86_64__)
#define udma_to_device_barrier() asm volatile("" ::: "memory")
#elif defined(__PPC64__)
#define udma_to_device_barrier() asm volatile("sync" ::: "memory")
#elif defined(__PPC__)
#define udma_to_device_barrier() asm volatile("sync" ::: "memory")
#elif defined(__ia64__)
#define udma_to_device_barrier() asm volatile("mf" ::: "memory")
#elif defined(__sparc_v9__)
#define udma_to_device_barrier() asm volatile("membar #StoreStore" ::: "memory")
#elif defined(__aarch64__)
#define udma_to_device_barrier() asm volatile("dsb st" ::: "memory");
#elif defined(__sparc__) || defined(__s390x__)
#define udma_to_device_barrier() asm volatile("" ::: "memory")
#elif defined(__mips__)
#include <sys/types.h>
#include <machine/atomic.h>
#define udma_to_device_barrier() mips_sync()
#elif defined(__arm__)
#include <sys/types.h>
#include <machine/atomic.h>
#define udma_to_device_barrier() dmb()
#elif defined(__riscv)
#include <sys/types.h>
#include <machine/atomic.h>
#define udma_to_device_barrier() fence()
#else
#error No architecture specific memory barrier defines found!
#endif

/* Ensure that all ordered stores from the device are observable from the
   CPU. This only makes sense after something that observes an ordered store
   from the device - eg by reading a MMIO register or seeing that CPU memory is
   updated.

   This guarantees that all reads that follow the barrier see the ordered
   stores that preceded the observation.

   For instance, this would be used after testing a valid bit in a memory
   that is a DMA target, to ensure that the following reads see the
   data written before the MemWr TLP that set the valid bit.
*/
#if defined(__i386__)
#define udma_from_device_barrier() asm volatile("lock; addl $0,0(%%esp) " ::: "memory")
#elif defined(__x86_64__)
#define udma_from_device_barrier() asm volatile("lfence" ::: "memory")
#elif defined(__PPC64__)
#define udma_from_device_barrier() asm volatile("lwsync" ::: "memory")
#elif defined(__PPC__)
#define udma_from_device_barrier() asm volatile("sync" ::: "memory")
#elif defined(__ia64__)
#define udma_from_device_barrier() asm volatile("mf" ::: "memory")
#elif defined(__sparc_v9__)
#define udma_from_device_barrier() asm volatile("membar #LoadLoad" ::: "memory")
#elif defined(__aarch64__)
#define udma_from_device_barrier() asm volatile("dsb ld" ::: "memory");
#elif defined(__sparc__) || defined(__s390x__)
#define udma_from_device_barrier() asm volatile("" ::: "memory")
#elif defined(__mips__)
#define udma_from_device_barrier() mips_sync()
#elif defined(__arm__)
#define udma_from_device_barrier() dmb()
#elif defined(__riscv)
#define udma_from_device_barrier() fence()
#else
#error No architecture specific memory barrier defines found!
#endif

/* Order writes to CPU memory so that a DMA device cannot view writes after
   the barrier without also seeing all writes before the barrier. This does
   not guarantee any writes are visible to DMA.

   This would be used in cases where a DMA buffer might have a valid bit and
   data, this barrier is placed after writing the data but before writing the
   valid bit to ensure the DMA device cannot observe a set valid bit with
   unwritten data.

   Compared to udma_to_device_barrier() this barrier is not required to fence
   anything but normal stores to normal malloc memory. Usage should be:

   write_wqe
      udma_to_device_barrier();    // Get user memory ready for DMA
      wqe->addr = ...;
      wqe->flags = ...;
      udma_ordering_write_barrier();  // Guarantee WQE written in order
      wqe->valid = 1;
*/
#define udma_ordering_write_barrier() udma_to_device_barrier()

/* Promptly flush writes to MMIO Write Cominbing memory.
   This should be used after a write to WC memory. This is both a barrier
   and a hint to the CPU to flush any buffers to reduce latency to TLP
   generation.

   This is not required to have any effect on CPU memory.

   If done while holding a lock then the ordering of MMIO writes across CPUs
   must be guaranteed to follow the natural ordering implied by the lock.

   This must also act as a barrier that prevents write combining, eg
     *wc_mem = 1;
     mmio_flush_writes();
     *wc_mem = 2;
   Must always produce two MemWr TLPs, '1' and '2'. Without the barrier
   the CPU is allowed to produce a single TLP '2'.

   Note that there is no order guarantee for writes to WC memory without
   barriers.

   This is intended to be used in conjunction with WC memory to generate large
   PCI-E MemWr TLPs from the CPU.
*/
#if defined(__i386__)
#define mmio_flush_writes() asm volatile("lock; addl $0,0(%%esp) " ::: "memory")
#elif defined(__x86_64__)
#define mmio_flush_writes() asm volatile("sfence" ::: "memory")
#elif defined(__PPC64__)
#define mmio_flush_writes() asm volatile("sync" ::: "memory")
#elif defined(__PPC__)
#define mmio_flush_writes() asm volatile("sync" ::: "memory")
#elif defined(__ia64__)
#define mmio_flush_writes() asm volatile("fwb" ::: "memory")
#elif defined(__sparc_v9__)
#define mmio_flush_writes() asm volatile("membar #StoreStore" ::: "memory")
#elif defined(__aarch64__)
#define mmio_flush_writes() asm volatile("dsb st" ::: "memory");
#elif defined(__sparc__) || defined(__s390x__)
#define mmio_flush_writes() asm volatile("" ::: "memory")
#elif defined(__mips__)
#define mmio_flush_writes() mips_sync()
#elif defined(__arm__)
#define mmio_flush_writes() dmb()
#elif defined(__riscv)
#define mmio_flush_writes() fence()
#else
#error No architecture specific memory barrier defines found!
#endif

/* Prevent WC writes from being re-ordered relative to other MMIO
   writes. This should be used before a write to WC memory.

   This must act as a barrier to prevent write re-ordering from different
   memory types:
     *mmio_mem = 1;
     mmio_flush_writes();
     *wc_mem = 2;
   Must always produce a TLP '1' followed by '2'.

   This barrier implies udma_to_device_barrier()

   This is intended to be used in conjunction with WC memory to generate large
   PCI-E MemWr TLPs from the CPU.
*/
#define mmio_wc_start() mmio_flush_writes()

/* Keep MMIO writes in order.
   Currently we lack writel macros that universally guarantee MMIO
   writes happen in order, like the kernel does. Even worse many
   providers haphazardly open code writes to MMIO memory omitting even
   volatile.

   Until this can be fixed with a proper writel macro, this barrier
   is a stand in to indicate places where MMIO writes should be switched
   to some future writel.
*/
#define mmio_ordered_writes_hack() mmio_flush_writes()

/* Write Combining Spinlock primitive

   Any access to a multi-value WC region must ensure that multiple cpus do not
   write to the same values concurrently, these macros make that
   straightforward and efficient if the choosen exclusion is a spinlock.

   The spinlock guarantees that the WC writes issued within the critical
   section are made visible as TLP to the device. The TLP must be seen by the
   device strictly in the order that the spinlocks are acquired, and combining
   WC writes between different sections is not permitted.

   Use of these macros allow the fencing inside the spinlock to be combined
   with the fencing required for DMA.
 */
static inline void mmio_wc_spinlock(pthread_spinlock_t *lock)
{
	pthread_spin_lock(lock);
#if !defined(__i386__) && !defined(__x86_64__)
	/* For x86 the serialization within the spin lock is enough to
	 * strongly order WC and other memory types. */
	mmio_wc_start();
#endif
}

static inline void mmio_wc_spinunlock(pthread_spinlock_t *lock)
{
	/* It is possible that on x86 the atomic in the lock is strong enough
	 * to force-flush the WC buffers quickly, and this SFENCE can be
	 * omitted too. */
	mmio_flush_writes();
	pthread_spin_unlock(lock);
}

#endif
