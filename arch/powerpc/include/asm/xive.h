/*
 * Copyright 2016,2017 IBM Corporation.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#ifndef _ASM_POWERPC_XIVE_H
#define _ASM_POWERPC_XIVE_H

#define XIVE_INVALID_VP	0xffffffff

#ifdef CONFIG_PPC_XIVE

/*
 * Thread Interrupt Management Area (TIMA)
 *
 * This is a global MMIO region divided in 4 pages of varying access
 * permissions, providing access to per-cpu interrupt management
 * functions. It always identifies the CPU doing the access based
 * on the PowerBus initiator ID, thus we always access via the
 * same offset regardless of where the code is executing
 */
extern void __iomem *xive_tima;

/*
 * Offset in the TM area of our current execution level (provided by
 * the backend)
 */
extern u32 xive_tima_offset;

/*
 * Per-irq data (irq_get_handler_data for normal IRQs), IPIs
 * have it stored in the xive_cpu structure. We also cache
 * for normal interrupts the current target CPU.
 *
 * This structure is setup by the backend for each interrupt.
 */
struct xive_irq_data {
	u64 flags;
	u64 eoi_page;
	void __iomem *eoi_mmio;
	u64 trig_page;
	void __iomem *trig_mmio;
	u32 esb_shift;
	int src_chip;

	/* Setup/used by frontend */
	int target;
	bool saved_p;
};
#define XIVE_IRQ_FLAG_STORE_EOI	0x01
#define XIVE_IRQ_FLAG_LSI	0x02
#define XIVE_IRQ_FLAG_SHIFT_BUG	0x04
#define XIVE_IRQ_FLAG_MASK_FW	0x08
#define XIVE_IRQ_FLAG_EOI_FW	0x10

#define XIVE_INVALID_CHIP_ID	-1

/* A queue tracking structure in a CPU */
struct xive_q {
	__be32 			*qpage;
	u32			msk;
	u32			idx;
	u32			toggle;
	u64			eoi_phys;
	u32			esc_irq;
	atomic_t		count;
	atomic_t		pending_count;
};

/*
 * "magic" Event State Buffer (ESB) MMIO offsets.
 *
 * Each interrupt source has a 2-bit state machine called ESB
 * which can be controlled by MMIO. It's made of 2 bits, P and
 * Q. P indicates that an interrupt is pending (has been sent
 * to a queue and is waiting for an EOI). Q indicates that the
 * interrupt has been triggered while pending.
 *
 * This acts as a coalescing mechanism in order to guarantee
 * that a given interrupt only occurs at most once in a queue.
 *
 * When doing an EOI, the Q bit will indicate if the interrupt
 * needs to be re-triggered.
 *
 * The following offsets into the ESB MMIO allow to read or
 * manipulate the PQ bits. They must be used with an 8-bytes
 * load instruction. They all return the previous state of the
 * interrupt (atomically).
 *
 * Additionally, some ESB pages support doing an EOI via a
 * store at 0 and some ESBs support doing a trigger via a
 * separate trigger page.
 */
#define XIVE_ESB_GET		0x800
#define XIVE_ESB_SET_PQ_00	0xc00
#define XIVE_ESB_SET_PQ_01	0xd00
#define XIVE_ESB_SET_PQ_10	0xe00
#define XIVE_ESB_SET_PQ_11	0xf00
#define XIVE_ESB_MASK		XIVE_ESB_SET_PQ_01

#define XIVE_ESB_VAL_P		0x2
#define XIVE_ESB_VAL_Q		0x1

/* Global enable flags for the XIVE support */
extern bool __xive_enabled;

static inline bool xive_enabled(void) { return __xive_enabled; }

extern bool xive_native_init(void);
extern void xive_smp_probe(void);
extern int  xive_smp_prepare_cpu(unsigned int cpu);
extern void xive_smp_setup_cpu(void);
extern void xive_smp_disable_cpu(void);
extern void xive_kexec_teardown_cpu(int secondary);
extern void xive_shutdown(void);
extern void xive_flush_interrupt(void);

/* xmon hook */
extern void xmon_xive_do_dump(int cpu);

/* APIs used by KVM */
extern u32 xive_native_default_eq_shift(void);
extern u32 xive_native_alloc_vp_block(u32 max_vcpus);
extern void xive_native_free_vp_block(u32 vp_base);
extern int xive_native_populate_irq_data(u32 hw_irq,
					 struct xive_irq_data *data);
extern void xive_cleanup_irq_data(struct xive_irq_data *xd);
extern u32 xive_native_alloc_irq(void);
extern void xive_native_free_irq(u32 irq);
extern int xive_native_configure_irq(u32 hw_irq, u32 target, u8 prio, u32 sw_irq);

extern int xive_native_configure_queue(u32 vp_id, struct xive_q *q, u8 prio,
				       __be32 *qpage, u32 order, bool can_escalate);
extern void xive_native_disable_queue(u32 vp_id, struct xive_q *q, u8 prio);

extern bool __xive_irq_trigger(struct xive_irq_data *xd);
extern bool __xive_irq_retrigger(struct xive_irq_data *xd);
extern void xive_do_source_eoi(u32 hw_irq, struct xive_irq_data *xd);

extern bool is_xive_irq(struct irq_chip *chip);

#else

static inline bool xive_enabled(void) { return false; }

static inline bool xive_native_init(void) { return false; }
static inline void xive_smp_probe(void) { }
extern inline int  xive_smp_prepare_cpu(unsigned int cpu) { return -EINVAL; }
static inline void xive_smp_setup_cpu(void) { }
static inline void xive_smp_disable_cpu(void) { }
static inline void xive_kexec_teardown_cpu(int secondary) { }
static inline void xive_shutdown(void) { }
static inline void xive_flush_interrupt(void) { }

static inline u32 xive_native_alloc_vp_block(u32 max_vcpus) { return XIVE_INVALID_VP; }
static inline void xive_native_free_vp_block(u32 vp_base) { }

#endif

#endif /* _ASM_POWERPC_XIVE_H */
