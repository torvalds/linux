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
	u32 hw_irq;

	/* Setup/used by frontend */
	int target;
	bool saved_p;
};
#define XIVE_IRQ_FLAG_STORE_EOI	0x01
#define XIVE_IRQ_FLAG_LSI	0x02
#define XIVE_IRQ_FLAG_SHIFT_BUG	0x04
#define XIVE_IRQ_FLAG_MASK_FW	0x08
#define XIVE_IRQ_FLAG_EOI_FW	0x10
#define XIVE_IRQ_FLAG_H_INT_ESB	0x20

/* Special flag set by KVM for excalation interrupts */
#define XIVE_IRQ_NO_EOI		0x80

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

/* Global enable flags for the XIVE support */
extern bool __xive_enabled;

static inline bool xive_enabled(void) { return __xive_enabled; }

extern bool xive_spapr_init(void);
extern bool xive_native_init(void);
extern void xive_smp_probe(void);
extern int  xive_smp_prepare_cpu(unsigned int cpu);
extern void xive_smp_setup_cpu(void);
extern void xive_smp_disable_cpu(void);
extern void xive_teardown_cpu(void);
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

extern void xive_native_sync_source(u32 hw_irq);
extern void xive_native_sync_queue(u32 hw_irq);
extern bool is_xive_irq(struct irq_chip *chip);
extern int xive_native_enable_vp(u32 vp_id, bool single_escalation);
extern int xive_native_disable_vp(u32 vp_id);
extern int xive_native_get_vp_info(u32 vp_id, u32 *out_cam_id, u32 *out_chip_id);
extern bool xive_native_has_single_escalation(void);

extern int xive_native_get_queue_info(u32 vp_id, uint32_t prio,
				      u64 *out_qpage,
				      u64 *out_qsize,
				      u64 *out_qeoi_page,
				      u32 *out_escalate_irq,
				      u64 *out_qflags);

extern int xive_native_get_queue_state(u32 vp_id, uint32_t prio, u32 *qtoggle,
				       u32 *qindex);
extern int xive_native_set_queue_state(u32 vp_id, uint32_t prio, u32 qtoggle,
				       u32 qindex);
extern int xive_native_get_vp_state(u32 vp_id, u64 *out_state);

#else

static inline bool xive_enabled(void) { return false; }

static inline bool xive_spapr_init(void) { return false; }
static inline bool xive_native_init(void) { return false; }
static inline void xive_smp_probe(void) { }
static inline int  xive_smp_prepare_cpu(unsigned int cpu) { return -EINVAL; }
static inline void xive_smp_setup_cpu(void) { }
static inline void xive_smp_disable_cpu(void) { }
static inline void xive_kexec_teardown_cpu(int secondary) { }
static inline void xive_shutdown(void) { }
static inline void xive_flush_interrupt(void) { }

static inline u32 xive_native_alloc_vp_block(u32 max_vcpus) { return XIVE_INVALID_VP; }
static inline void xive_native_free_vp_block(u32 vp_base) { }

#endif

#endif /* _ASM_POWERPC_XIVE_H */
