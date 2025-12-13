/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Copyright 2016,2017 IBM Corporation.
 */
#ifndef _ASM_POWERPC_XIVE_H
#define _ASM_POWERPC_XIVE_H

#include <asm/opal-api.h>

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
extern unsigned long xive_tima_os;

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
	/*
	 * saved_p means that there is a queue entry for this interrupt
	 * in some CPU's queue (not including guest vcpu queues), even
	 * if P is not set in the source ESB.
	 * stale_p means that there is no queue entry for this interrupt
	 * in some CPU's queue, even if P is set in the source ESB.
	 */
	bool saved_p;
	bool stale_p;
};
#define XIVE_IRQ_FLAG_STORE_EOI	0x01
#define XIVE_IRQ_FLAG_LSI	0x02
/* #define XIVE_IRQ_FLAG_SHIFT_BUG	0x04 */ /* P9 DD1.0 workaround */
/* #define XIVE_IRQ_FLAG_MASK_FW	0x08 */ /* P9 DD1.0 workaround */
/* #define XIVE_IRQ_FLAG_EOI_FW	0x10 */ /* P9 DD1.0 workaround */
#define XIVE_IRQ_FLAG_H_INT_ESB	0x20

/* Special flag set by KVM for excalation interrupts */
#define XIVE_IRQ_FLAG_NO_EOI	0x80

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
	u64			guest_qaddr;
	u32			guest_qshift;
};

/* Global enable flags for the XIVE support */
extern bool __xive_enabled;

static inline bool xive_enabled(void) { return __xive_enabled; }

bool xive_spapr_init(void);
bool xive_native_init(void);
void xive_smp_probe(void);
int  xive_smp_prepare_cpu(unsigned int cpu);
void xive_smp_setup_cpu(void);
void xive_smp_disable_cpu(void);
void xive_teardown_cpu(void);
void xive_shutdown(void);
void xive_flush_interrupt(void);

/* xmon hook */
void xmon_xive_do_dump(int cpu);
int xmon_xive_get_irq_config(u32 hw_irq, struct irq_data *d);
void xmon_xive_get_irq_all(void);

/* APIs used by KVM */
u32 xive_native_default_eq_shift(void);
u32 xive_native_alloc_vp_block(u32 max_vcpus);
void xive_native_free_vp_block(u32 vp_base);
int xive_native_populate_irq_data(u32 hw_irq,
				  struct xive_irq_data *data);
void xive_cleanup_irq_data(struct xive_irq_data *xd);
void xive_native_free_irq(u32 irq);
int xive_native_configure_irq(u32 hw_irq, u32 target, u8 prio, u32 sw_irq);

int xive_native_configure_queue(u32 vp_id, struct xive_q *q, u8 prio,
				__be32 *qpage, u32 order, bool can_escalate);
void xive_native_disable_queue(u32 vp_id, struct xive_q *q, u8 prio);

void xive_native_sync_source(u32 hw_irq);
void xive_native_sync_queue(u32 hw_irq);
bool is_xive_irq(struct irq_chip *chip);
int xive_native_enable_vp(u32 vp_id, bool single_escalation);
int xive_native_disable_vp(u32 vp_id);
int xive_native_get_vp_info(u32 vp_id, u32 *out_cam_id, u32 *out_chip_id);
bool xive_native_has_single_escalation(void);
bool xive_native_has_save_restore(void);

int xive_native_get_queue_info(u32 vp_id, uint32_t prio,
			       u64 *out_qpage,
			       u64 *out_qsize,
			       u64 *out_qeoi_page,
			       u32 *out_escalate_irq,
			       u64 *out_qflags);

int xive_native_get_queue_state(u32 vp_id, uint32_t prio, u32 *qtoggle,
				u32 *qindex);
int xive_native_set_queue_state(u32 vp_id, uint32_t prio, u32 qtoggle,
				u32 qindex);
int xive_native_get_vp_state(u32 vp_id, u64 *out_state);
bool xive_native_has_queue_state_support(void);
extern u32 xive_native_alloc_irq_on_chip(u32 chip_id);

static inline u32 xive_native_alloc_irq(void)
{
	return xive_native_alloc_irq_on_chip(OPAL_XIVE_ANY_CHIP);
}

#else

static inline bool xive_enabled(void) { return false; }

static inline bool xive_spapr_init(void) { return false; }
static inline bool xive_native_init(void) { return false; }
static inline void xive_smp_probe(void) { }
static inline int  xive_smp_prepare_cpu(unsigned int cpu) { return -EINVAL; }
static inline void xive_smp_setup_cpu(void) { }
static inline void xive_smp_disable_cpu(void) { }
static inline void xive_shutdown(void) { }
static inline void xive_flush_interrupt(void) { }

static inline u32 xive_native_alloc_vp_block(u32 max_vcpus) { return XIVE_INVALID_VP; }
static inline void xive_native_free_vp_block(u32 vp_base) { }

#endif

#endif /* _ASM_POWERPC_XIVE_H */
