/*
 * OpenPIC emulation
 *
 * Copyright (c) 2004 Jocelyn Mayer
 *               2011 Alexander Graf
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#define MAX_CPU     32
#define MAX_SRC     256
#define MAX_TMR     4
#define MAX_IPI     4
#define MAX_MSI     8
#define MAX_IRQ     (MAX_SRC + MAX_IPI + MAX_TMR)
#define VID         0x03	/* MPIC version ID */

/* OpenPIC capability flags */
#define OPENPIC_FLAG_IDR_CRIT     (1 << 0)
#define OPENPIC_FLAG_ILR          (2 << 0)

/* OpenPIC address map */
#define OPENPIC_GLB_REG_START        0x0
#define OPENPIC_GLB_REG_SIZE         0x10F0
#define OPENPIC_TMR_REG_START        0x10F0
#define OPENPIC_TMR_REG_SIZE         0x220
#define OPENPIC_MSI_REG_START        0x1600
#define OPENPIC_MSI_REG_SIZE         0x200
#define OPENPIC_SUMMARY_REG_START    0x3800
#define OPENPIC_SUMMARY_REG_SIZE     0x800
#define OPENPIC_SRC_REG_START        0x10000
#define OPENPIC_SRC_REG_SIZE         (MAX_SRC * 0x20)
#define OPENPIC_CPU_REG_START        0x20000
#define OPENPIC_CPU_REG_SIZE         (0x100 + ((MAX_CPU - 1) * 0x1000))

struct fsl_mpic_info {
	int max_ext;
};

static struct fsl_mpic_info fsl_mpic_20 = {
	.max_ext = 12,
};

static struct fsl_mpic_info fsl_mpic_42 = {
	.max_ext = 12,
};

#define FRR_NIRQ_SHIFT    16
#define FRR_NCPU_SHIFT     8
#define FRR_VID_SHIFT      0

#define VID_REVISION_1_2   2
#define VID_REVISION_1_3   3

#define VIR_GENERIC      0x00000000	/* Generic Vendor ID */

#define GCR_RESET        0x80000000
#define GCR_MODE_PASS    0x00000000
#define GCR_MODE_MIXED   0x20000000
#define GCR_MODE_PROXY   0x60000000

#define TBCR_CI           0x80000000	/* count inhibit */
#define TCCR_TOG          0x80000000	/* toggles when decrement to zero */

#define IDR_EP_SHIFT      31
#define IDR_EP_MASK       (1 << IDR_EP_SHIFT)
#define IDR_CI0_SHIFT     30
#define IDR_CI1_SHIFT     29
#define IDR_P1_SHIFT      1
#define IDR_P0_SHIFT      0

#define ILR_INTTGT_MASK   0x000000ff
#define ILR_INTTGT_INT    0x00
#define ILR_INTTGT_CINT   0x01	/* critical */
#define ILR_INTTGT_MCP    0x02	/* machine check */

#define MSIIR_OFFSET       0x140
#define MSIIR_SRS_SHIFT    29
#define MSIIR_SRS_MASK     (0x7 << MSIIR_SRS_SHIFT)
#define MSIIR_IBS_SHIFT    24
#define MSIIR_IBS_MASK     (0x1f << MSIIR_IBS_SHIFT)

static int get_current_cpu(void)
{
	CPUState *cpu_single_cpu;

	if (!cpu_single_env)
		return -1;

	cpu_single_cpu = ENV_GET_CPU(cpu_single_env);
	return cpu_single_cpu->cpu_index;
}

static uint32_t openpic_cpu_read_internal(void *opaque, gpa_t addr, int idx);
static void openpic_cpu_write_internal(void *opaque, gpa_t addr,
				       uint32_t val, int idx);

enum irq_type {
	IRQ_TYPE_NORMAL = 0,
	IRQ_TYPE_FSLINT,	/* FSL internal interrupt -- level only */
	IRQ_TYPE_FSLSPECIAL,	/* FSL timer/IPI interrupt, edge, no polarity */
};

struct irq_queue {
	/* Round up to the nearest 64 IRQs so that the queue length
	 * won't change when moving between 32 and 64 bit hosts.
	 */
	unsigned long queue[BITS_TO_LONGS((MAX_IRQ + 63) & ~63)];
	int next;
	int priority;
};

struct irq_source {
	uint32_t ivpr;		/* IRQ vector/priority register */
	uint32_t idr;		/* IRQ destination register */
	uint32_t destmask;	/* bitmap of CPU destinations */
	int last_cpu;
	int output;		/* IRQ level, e.g. OPENPIC_OUTPUT_INT */
	int pending;		/* TRUE if IRQ is pending */
	enum irq_type type;
	bool level:1;		/* level-triggered */
	bool nomask:1;	/* critical interrupts ignore mask on some FSL MPICs */
};

#define IVPR_MASK_SHIFT       31
#define IVPR_MASK_MASK        (1 << IVPR_MASK_SHIFT)
#define IVPR_ACTIVITY_SHIFT   30
#define IVPR_ACTIVITY_MASK    (1 << IVPR_ACTIVITY_SHIFT)
#define IVPR_MODE_SHIFT       29
#define IVPR_MODE_MASK        (1 << IVPR_MODE_SHIFT)
#define IVPR_POLARITY_SHIFT   23
#define IVPR_POLARITY_MASK    (1 << IVPR_POLARITY_SHIFT)
#define IVPR_SENSE_SHIFT      22
#define IVPR_SENSE_MASK       (1 << IVPR_SENSE_SHIFT)

#define IVPR_PRIORITY_MASK     (0xF << 16)
#define IVPR_PRIORITY(_ivprr_) ((int)(((_ivprr_) & IVPR_PRIORITY_MASK) >> 16))
#define IVPR_VECTOR(opp, _ivprr_) ((_ivprr_) & (opp)->vector_mask)

/* IDR[EP/CI] are only for FSL MPIC prior to v4.0 */
#define IDR_EP      0x80000000	/* external pin */
#define IDR_CI      0x40000000	/* critical interrupt */

struct irq_dest {
	int32_t ctpr;		/* CPU current task priority */
	struct irq_queue raised;
	struct irq_queue servicing;
	qemu_irq *irqs;

	/* Count of IRQ sources asserting on non-INT outputs */
	uint32_t outputs_active[OPENPIC_OUTPUT_NB];
};

struct openpic {
	/* Behavior control */
	struct fsl_mpic_info *fsl;
	uint32_t model;
	uint32_t flags;
	uint32_t nb_irqs;
	uint32_t vid;
	uint32_t vir;		/* Vendor identification register */
	uint32_t vector_mask;
	uint32_t tfrr_reset;
	uint32_t ivpr_reset;
	uint32_t idr_reset;
	uint32_t brr1;
	uint32_t mpic_mode_mask;

	/* Global registers */
	uint32_t frr;		/* Feature reporting register */
	uint32_t gcr;		/* Global configuration register  */
	uint32_t pir;		/* Processor initialization register */
	uint32_t spve;		/* Spurious vector register */
	uint32_t tfrr;		/* Timer frequency reporting register */
	/* Source registers */
	struct irq_source src[MAX_IRQ];
	/* Local registers per output pin */
	struct irq_dest dst[MAX_CPU];
	uint32_t nb_cpus;
	/* Timer registers */
	struct {
		uint32_t tccr;	/* Global timer current count register */
		uint32_t tbcr;	/* Global timer base count register */
	} timers[MAX_TMR];
	/* Shared MSI registers */
	struct {
		uint32_t msir;	/* Shared Message Signaled Interrupt Register */
	} msi[MAX_MSI];
	uint32_t max_irq;
	uint32_t irq_ipi0;
	uint32_t irq_tim0;
	uint32_t irq_msi;
};

static inline void IRQ_setbit(struct irq_queue *q, int n_IRQ)
{
	set_bit(n_IRQ, q->queue);
}

static inline void IRQ_resetbit(struct irq_queue *q, int n_IRQ)
{
	clear_bit(n_IRQ, q->queue);
}

static inline int IRQ_testbit(struct irq_queue *q, int n_IRQ)
{
	return test_bit(n_IRQ, q->queue);
}

static void IRQ_check(struct openpic *opp, struct irq_queue *q)
{
	int irq = -1;
	int next = -1;
	int priority = -1;

	for (;;) {
		irq = find_next_bit(q->queue, opp->max_irq, irq + 1);
		if (irq == opp->max_irq)
			break;

		pr_debug("IRQ_check: irq %d set ivpr_pr=%d pr=%d\n",
			irq, IVPR_PRIORITY(opp->src[irq].ivpr), priority);

		if (IVPR_PRIORITY(opp->src[irq].ivpr) > priority) {
			next = irq;
			priority = IVPR_PRIORITY(opp->src[irq].ivpr);
		}
	}

	q->next = next;
	q->priority = priority;
}

static int IRQ_get_next(struct openpic *opp, struct irq_queue *q)
{
	/* XXX: optimize */
	IRQ_check(opp, q);

	return q->next;
}

static void IRQ_local_pipe(struct openpic *opp, int n_CPU, int n_IRQ,
			   bool active, bool was_active)
{
	struct irq_dest *dst;
	struct irq_source *src;
	int priority;

	dst = &opp->dst[n_CPU];
	src = &opp->src[n_IRQ];

	pr_debug("%s: IRQ %d active %d was %d\n",
		__func__, n_IRQ, active, was_active);

	if (src->output != OPENPIC_OUTPUT_INT) {
		pr_debug("%s: output %d irq %d active %d was %d count %d\n",
			__func__, src->output, n_IRQ, active, was_active,
			dst->outputs_active[src->output]);

		/* On Freescale MPIC, critical interrupts ignore priority,
		 * IACK, EOI, etc.  Before MPIC v4.1 they also ignore
		 * masking.
		 */
		if (active) {
			if (!was_active &&
			    dst->outputs_active[src->output]++ == 0) {
				pr_debug("%s: Raise OpenPIC output %d cpu %d irq %d\n",
					__func__, src->output, n_CPU, n_IRQ);
				qemu_irq_raise(dst->irqs[src->output]);
			}
		} else {
			if (was_active &&
			    --dst->outputs_active[src->output] == 0) {
				pr_debug("%s: Lower OpenPIC output %d cpu %d irq %d\n",
					__func__, src->output, n_CPU, n_IRQ);
				qemu_irq_lower(dst->irqs[src->output]);
			}
		}

		return;
	}

	priority = IVPR_PRIORITY(src->ivpr);

	/* Even if the interrupt doesn't have enough priority,
	 * it is still raised, in case ctpr is lowered later.
	 */
	if (active)
		IRQ_setbit(&dst->raised, n_IRQ);
	else
		IRQ_resetbit(&dst->raised, n_IRQ);

	IRQ_check(opp, &dst->raised);

	if (active && priority <= dst->ctpr) {
		pr_debug("%s: IRQ %d priority %d too low for ctpr %d on CPU %d\n",
			__func__, n_IRQ, priority, dst->ctpr, n_CPU);
		active = 0;
	}

	if (active) {
		if (IRQ_get_next(opp, &dst->servicing) >= 0 &&
		    priority <= dst->servicing.priority) {
			pr_debug("%s: IRQ %d is hidden by servicing IRQ %d on CPU %d\n",
				__func__, n_IRQ, dst->servicing.next, n_CPU);
		} else {
			pr_debug("%s: Raise OpenPIC INT output cpu %d irq %d/%d\n",
				__func__, n_CPU, n_IRQ, dst->raised.next);
			qemu_irq_raise(opp->dst[n_CPU].
				       irqs[OPENPIC_OUTPUT_INT]);
		}
	} else {
		IRQ_get_next(opp, &dst->servicing);
		if (dst->raised.priority > dst->ctpr &&
		    dst->raised.priority > dst->servicing.priority) {
			pr_debug("%s: IRQ %d inactive, IRQ %d prio %d above %d/%d, CPU %d\n",
				__func__, n_IRQ, dst->raised.next,
				dst->raised.priority, dst->ctpr,
				dst->servicing.priority, n_CPU);
			/* IRQ line stays asserted */
		} else {
			pr_debug("%s: IRQ %d inactive, current prio %d/%d, CPU %d\n",
				__func__, n_IRQ, dst->ctpr,
				dst->servicing.priority, n_CPU);
			qemu_irq_lower(opp->dst[n_CPU].
				       irqs[OPENPIC_OUTPUT_INT]);
		}
	}
}

/* update pic state because registers for n_IRQ have changed value */
static void openpic_update_irq(struct openpic *opp, int n_IRQ)
{
	struct irq_source *src;
	bool active, was_active;
	int i;

	src = &opp->src[n_IRQ];
	active = src->pending;

	if ((src->ivpr & IVPR_MASK_MASK) && !src->nomask) {
		/* Interrupt source is disabled */
		pr_debug("%s: IRQ %d is disabled\n", __func__, n_IRQ);
		active = false;
	}

	was_active = !!(src->ivpr & IVPR_ACTIVITY_MASK);

	/*
	 * We don't have a similar check for already-active because
	 * ctpr may have changed and we need to withdraw the interrupt.
	 */
	if (!active && !was_active) {
		pr_debug("%s: IRQ %d is already inactive\n", __func__, n_IRQ);
		return;
	}

	if (active)
		src->ivpr |= IVPR_ACTIVITY_MASK;
	else
		src->ivpr &= ~IVPR_ACTIVITY_MASK;

	if (src->destmask == 0) {
		/* No target */
		pr_debug("%s: IRQ %d has no target\n", __func__, n_IRQ);
		return;
	}

	if (src->destmask == (1 << src->last_cpu)) {
		/* Only one CPU is allowed to receive this IRQ */
		IRQ_local_pipe(opp, src->last_cpu, n_IRQ, active, was_active);
	} else if (!(src->ivpr & IVPR_MODE_MASK)) {
		/* Directed delivery mode */
		for (i = 0; i < opp->nb_cpus; i++) {
			if (src->destmask & (1 << i)) {
				IRQ_local_pipe(opp, i, n_IRQ, active,
					       was_active);
			}
		}
	} else {
		/* Distributed delivery mode */
		for (i = src->last_cpu + 1; i != src->last_cpu; i++) {
			if (i == opp->nb_cpus)
				i = 0;

			if (src->destmask & (1 << i)) {
				IRQ_local_pipe(opp, i, n_IRQ, active,
					       was_active);
				src->last_cpu = i;
				break;
			}
		}
	}
}

static void openpic_set_irq(void *opaque, int n_IRQ, int level)
{
	struct openpic *opp = opaque;
	struct irq_source *src;

	if (n_IRQ >= MAX_IRQ) {
		pr_err("%s: IRQ %d out of range\n", __func__, n_IRQ);
		abort();
	}

	src = &opp->src[n_IRQ];
	pr_debug("openpic: set irq %d = %d ivpr=0x%08x\n",
		n_IRQ, level, src->ivpr);
	if (src->level) {
		/* level-sensitive irq */
		src->pending = level;
		openpic_update_irq(opp, n_IRQ);
	} else {
		/* edge-sensitive irq */
		if (level) {
			src->pending = 1;
			openpic_update_irq(opp, n_IRQ);
		}

		if (src->output != OPENPIC_OUTPUT_INT) {
			/* Edge-triggered interrupts shouldn't be used
			 * with non-INT delivery, but just in case,
			 * try to make it do something sane rather than
			 * cause an interrupt storm.  This is close to
			 * what you'd probably see happen in real hardware.
			 */
			src->pending = 0;
			openpic_update_irq(opp, n_IRQ);
		}
	}
}

static void openpic_reset(DeviceState *d)
{
	struct openpic *opp = FROM_SYSBUS(typeof(*opp), SYS_BUS_DEVICE(d));
	int i;

	opp->gcr = GCR_RESET;
	/* Initialise controller registers */
	opp->frr = ((opp->nb_irqs - 1) << FRR_NIRQ_SHIFT) |
	    ((opp->nb_cpus - 1) << FRR_NCPU_SHIFT) |
	    (opp->vid << FRR_VID_SHIFT);

	opp->pir = 0;
	opp->spve = -1 & opp->vector_mask;
	opp->tfrr = opp->tfrr_reset;
	/* Initialise IRQ sources */
	for (i = 0; i < opp->max_irq; i++) {
		opp->src[i].ivpr = opp->ivpr_reset;
		opp->src[i].idr = opp->idr_reset;

		switch (opp->src[i].type) {
		case IRQ_TYPE_NORMAL:
			opp->src[i].level =
			    !!(opp->ivpr_reset & IVPR_SENSE_MASK);
			break;

		case IRQ_TYPE_FSLINT:
			opp->src[i].ivpr |= IVPR_POLARITY_MASK;
			break;

		case IRQ_TYPE_FSLSPECIAL:
			break;
		}
	}
	/* Initialise IRQ destinations */
	for (i = 0; i < MAX_CPU; i++) {
		opp->dst[i].ctpr = 15;
		memset(&opp->dst[i].raised, 0, sizeof(struct irq_queue));
		opp->dst[i].raised.next = -1;
		memset(&opp->dst[i].servicing, 0, sizeof(struct irq_queue));
		opp->dst[i].servicing.next = -1;
	}
	/* Initialise timers */
	for (i = 0; i < MAX_TMR; i++) {
		opp->timers[i].tccr = 0;
		opp->timers[i].tbcr = TBCR_CI;
	}
	/* Go out of RESET state */
	opp->gcr = 0;
}

static inline uint32_t read_IRQreg_idr(struct openpic *opp, int n_IRQ)
{
	return opp->src[n_IRQ].idr;
}

static inline uint32_t read_IRQreg_ilr(struct openpic *opp, int n_IRQ)
{
	if (opp->flags & OPENPIC_FLAG_ILR)
		return output_to_inttgt(opp->src[n_IRQ].output);

	return 0xffffffff;
}

static inline uint32_t read_IRQreg_ivpr(struct openpic *opp, int n_IRQ)
{
	return opp->src[n_IRQ].ivpr;
}

static inline void write_IRQreg_idr(struct openpic *opp, int n_IRQ,
				    uint32_t val)
{
	struct irq_source *src = &opp->src[n_IRQ];
	uint32_t normal_mask = (1UL << opp->nb_cpus) - 1;
	uint32_t crit_mask = 0;
	uint32_t mask = normal_mask;
	int crit_shift = IDR_EP_SHIFT - opp->nb_cpus;
	int i;

	if (opp->flags & OPENPIC_FLAG_IDR_CRIT) {
		crit_mask = mask << crit_shift;
		mask |= crit_mask | IDR_EP;
	}

	src->idr = val & mask;
	pr_debug("Set IDR %d to 0x%08x\n", n_IRQ, src->idr);

	if (opp->flags & OPENPIC_FLAG_IDR_CRIT) {
		if (src->idr & crit_mask) {
			if (src->idr & normal_mask) {
				pr_debug("%s: IRQ configured for multiple output types, using critical\n",
					__func__);
			}

			src->output = OPENPIC_OUTPUT_CINT;
			src->nomask = true;
			src->destmask = 0;

			for (i = 0; i < opp->nb_cpus; i++) {
				int n_ci = IDR_CI0_SHIFT - i;

				if (src->idr & (1UL << n_ci))
					src->destmask |= 1UL << i;
			}
		} else {
			src->output = OPENPIC_OUTPUT_INT;
			src->nomask = false;
			src->destmask = src->idr & normal_mask;
		}
	} else {
		src->destmask = src->idr;
	}
}

static inline void write_IRQreg_ilr(struct openpic *opp, int n_IRQ,
				    uint32_t val)
{
	if (opp->flags & OPENPIC_FLAG_ILR) {
		struct irq_source *src = &opp->src[n_IRQ];

		src->output = inttgt_to_output(val & ILR_INTTGT_MASK);
		pr_debug("Set ILR %d to 0x%08x, output %d\n", n_IRQ, src->idr,
			src->output);

		/* TODO: on MPIC v4.0 only, set nomask for non-INT */
	}
}

static inline void write_IRQreg_ivpr(struct openpic *opp, int n_IRQ,
				     uint32_t val)
{
	uint32_t mask;

	/* NOTE when implementing newer FSL MPIC models: starting with v4.0,
	 * the polarity bit is read-only on internal interrupts.
	 */
	mask = IVPR_MASK_MASK | IVPR_PRIORITY_MASK | IVPR_SENSE_MASK |
	    IVPR_POLARITY_MASK | opp->vector_mask;

	/* ACTIVITY bit is read-only */
	opp->src[n_IRQ].ivpr =
	    (opp->src[n_IRQ].ivpr & IVPR_ACTIVITY_MASK) | (val & mask);

	/* For FSL internal interrupts, The sense bit is reserved and zero,
	 * and the interrupt is always level-triggered.  Timers and IPIs
	 * have no sense or polarity bits, and are edge-triggered.
	 */
	switch (opp->src[n_IRQ].type) {
	case IRQ_TYPE_NORMAL:
		opp->src[n_IRQ].level =
		    !!(opp->src[n_IRQ].ivpr & IVPR_SENSE_MASK);
		break;

	case IRQ_TYPE_FSLINT:
		opp->src[n_IRQ].ivpr &= ~IVPR_SENSE_MASK;
		break;

	case IRQ_TYPE_FSLSPECIAL:
		opp->src[n_IRQ].ivpr &= ~(IVPR_POLARITY_MASK | IVPR_SENSE_MASK);
		break;
	}

	openpic_update_irq(opp, n_IRQ);
	pr_debug("Set IVPR %d to 0x%08x -> 0x%08x\n", n_IRQ, val,
		opp->src[n_IRQ].ivpr);
}

static void openpic_gcr_write(struct openpic *opp, uint64_t val)
{
	bool mpic_proxy = false;

	if (val & GCR_RESET) {
		openpic_reset(&opp->busdev.qdev);
		return;
	}

	opp->gcr &= ~opp->mpic_mode_mask;
	opp->gcr |= val & opp->mpic_mode_mask;

	/* Set external proxy mode */
	if ((val & opp->mpic_mode_mask) == GCR_MODE_PROXY)
		mpic_proxy = true;

	ppce500_set_mpic_proxy(mpic_proxy);
}

static void openpic_gbl_write(void *opaque, gpa_t addr, uint64_t val,
			      unsigned len)
{
	struct openpic *opp = opaque;
	struct irq_dest *dst;
	int idx;

	pr_debug("%s: addr %#" HWADDR_PRIx " <= %08" PRIx64 "\n",
		__func__, addr, val);
	if (addr & 0xF)
		return;

	switch (addr) {
	case 0x00:	/* Block Revision Register1 (BRR1) is Readonly */
		break;
	case 0x40:
	case 0x50:
	case 0x60:
	case 0x70:
	case 0x80:
	case 0x90:
	case 0xA0:
	case 0xB0:
		openpic_cpu_write_internal(opp, addr, val, get_current_cpu());
		break;
	case 0x1000:		/* FRR */
		break;
	case 0x1020:		/* GCR */
		openpic_gcr_write(opp, val);
		break;
	case 0x1080:		/* VIR */
		break;
	case 0x1090:		/* PIR */
		for (idx = 0; idx < opp->nb_cpus; idx++) {
			if ((val & (1 << idx)) && !(opp->pir & (1 << idx))) {
				pr_debug("Raise OpenPIC RESET output for CPU %d\n",
					idx);
				dst = &opp->dst[idx];
				qemu_irq_raise(dst->irqs[OPENPIC_OUTPUT_RESET]);
			} else if (!(val & (1 << idx)) &&
				   (opp->pir & (1 << idx))) {
				pr_debug("Lower OpenPIC RESET output for CPU %d\n",
					idx);
				dst = &opp->dst[idx];
				qemu_irq_lower(dst->irqs[OPENPIC_OUTPUT_RESET]);
			}
		}
		opp->pir = val;
		break;
	case 0x10A0:		/* IPI_IVPR */
	case 0x10B0:
	case 0x10C0:
	case 0x10D0: {
		int idx;
		idx = (addr - 0x10A0) >> 4;
		write_IRQreg_ivpr(opp, opp->irq_ipi0 + idx, val);
		break;
	}
	case 0x10E0:		/* SPVE */
		opp->spve = val & opp->vector_mask;
		break;
	default:
		break;
	}
}

static uint64_t openpic_gbl_read(void *opaque, gpa_t addr, unsigned len)
{
	struct openpic *opp = opaque;
	uint32_t retval;

	pr_debug("%s: addr %#" HWADDR_PRIx "\n", __func__, addr);
	retval = 0xFFFFFFFF;
	if (addr & 0xF)
		return retval;

	switch (addr) {
	case 0x1000:		/* FRR */
		retval = opp->frr;
		break;
	case 0x1020:		/* GCR */
		retval = opp->gcr;
		break;
	case 0x1080:		/* VIR */
		retval = opp->vir;
		break;
	case 0x1090:		/* PIR */
		retval = 0x00000000;
		break;
	case 0x00:		/* Block Revision Register1 (BRR1) */
		retval = opp->brr1;
		break;
	case 0x40:
	case 0x50:
	case 0x60:
	case 0x70:
	case 0x80:
	case 0x90:
	case 0xA0:
	case 0xB0:
		retval =
		    openpic_cpu_read_internal(opp, addr, get_current_cpu());
		break;
	case 0x10A0:		/* IPI_IVPR */
	case 0x10B0:
	case 0x10C0:
	case 0x10D0:
		{
			int idx;
			idx = (addr - 0x10A0) >> 4;
			retval = read_IRQreg_ivpr(opp, opp->irq_ipi0 + idx);
		}
		break;
	case 0x10E0:		/* SPVE */
		retval = opp->spve;
		break;
	default:
		break;
	}
	pr_debug("%s: => 0x%08x\n", __func__, retval);

	return retval;
}

static void openpic_tmr_write(void *opaque, gpa_t addr, uint64_t val,
			      unsigned len)
{
	struct openpic *opp = opaque;
	int idx;

	addr += 0x10f0;

	pr_debug("%s: addr %#" HWADDR_PRIx " <= %08" PRIx64 "\n",
		__func__, addr, val);
	if (addr & 0xF)
		return;

	if (addr == 0x10f0) {
		/* TFRR */
		opp->tfrr = val;
		return;
	}

	idx = (addr >> 6) & 0x3;
	addr = addr & 0x30;

	switch (addr & 0x30) {
	case 0x00:		/* TCCR */
		break;
	case 0x10:		/* TBCR */
		if ((opp->timers[idx].tccr & TCCR_TOG) != 0 &&
		    (val & TBCR_CI) == 0 &&
		    (opp->timers[idx].tbcr & TBCR_CI) != 0)
			opp->timers[idx].tccr &= ~TCCR_TOG;

		opp->timers[idx].tbcr = val;
		break;
	case 0x20:		/* TVPR */
		write_IRQreg_ivpr(opp, opp->irq_tim0 + idx, val);
		break;
	case 0x30:		/* TDR */
		write_IRQreg_idr(opp, opp->irq_tim0 + idx, val);
		break;
	}
}

static uint64_t openpic_tmr_read(void *opaque, gpa_t addr, unsigned len)
{
	struct openpic *opp = opaque;
	uint32_t retval = -1;
	int idx;

	pr_debug("%s: addr %#" HWADDR_PRIx "\n", __func__, addr);
	if (addr & 0xF)
		goto out;

	idx = (addr >> 6) & 0x3;
	if (addr == 0x0) {
		/* TFRR */
		retval = opp->tfrr;
		goto out;
	}
	switch (addr & 0x30) {
	case 0x00:		/* TCCR */
		retval = opp->timers[idx].tccr;
		break;
	case 0x10:		/* TBCR */
		retval = opp->timers[idx].tbcr;
		break;
	case 0x20:		/* TIPV */
		retval = read_IRQreg_ivpr(opp, opp->irq_tim0 + idx);
		break;
	case 0x30:		/* TIDE (TIDR) */
		retval = read_IRQreg_idr(opp, opp->irq_tim0 + idx);
		break;
	}

out:
	pr_debug("%s: => 0x%08x\n", __func__, retval);

	return retval;
}

static void openpic_src_write(void *opaque, gpa_t addr, uint64_t val,
			      unsigned len)
{
	struct openpic *opp = opaque;
	int idx;

	pr_debug("%s: addr %#" HWADDR_PRIx " <= %08" PRIx64 "\n",
		__func__, addr, val);

	addr = addr & 0xffff;
	idx = addr >> 5;

	switch (addr & 0x1f) {
	case 0x00:
		write_IRQreg_ivpr(opp, idx, val);
		break;
	case 0x10:
		write_IRQreg_idr(opp, idx, val);
		break;
	case 0x18:
		write_IRQreg_ilr(opp, idx, val);
		break;
	}
}

static uint64_t openpic_src_read(void *opaque, uint64_t addr, unsigned len)
{
	struct openpic *opp = opaque;
	uint32_t retval;
	int idx;

	pr_debug("%s: addr %#" HWADDR_PRIx "\n", __func__, addr);
	retval = 0xFFFFFFFF;

	addr = addr & 0xffff;
	idx = addr >> 5;

	switch (addr & 0x1f) {
	case 0x00:
		retval = read_IRQreg_ivpr(opp, idx);
		break;
	case 0x10:
		retval = read_IRQreg_idr(opp, idx);
		break;
	case 0x18:
		retval = read_IRQreg_ilr(opp, idx);
		break;
	}

	pr_debug("%s: => 0x%08x\n", __func__, retval);
	return retval;
}

static void openpic_msi_write(void *opaque, gpa_t addr, uint64_t val,
			      unsigned size)
{
	struct openpic *opp = opaque;
	int idx = opp->irq_msi;
	int srs, ibs;

	pr_debug("%s: addr %#" HWADDR_PRIx " <= 0x%08" PRIx64 "\n",
		__func__, addr, val);
	if (addr & 0xF)
		return;

	switch (addr) {
	case MSIIR_OFFSET:
		srs = val >> MSIIR_SRS_SHIFT;
		idx += srs;
		ibs = (val & MSIIR_IBS_MASK) >> MSIIR_IBS_SHIFT;
		opp->msi[srs].msir |= 1 << ibs;
		openpic_set_irq(opp, idx, 1);
		break;
	default:
		/* most registers are read-only, thus ignored */
		break;
	}
}

static uint64_t openpic_msi_read(void *opaque, gpa_t addr, unsigned size)
{
	struct openpic *opp = opaque;
	uint64_t r = 0;
	int i, srs;

	pr_debug("%s: addr %#" HWADDR_PRIx "\n", __func__, addr);
	if (addr & 0xF)
		return -1;

	srs = addr >> 4;

	switch (addr) {
	case 0x00:
	case 0x10:
	case 0x20:
	case 0x30:
	case 0x40:
	case 0x50:
	case 0x60:
	case 0x70:		/* MSIRs */
		r = opp->msi[srs].msir;
		/* Clear on read */
		opp->msi[srs].msir = 0;
		openpic_set_irq(opp, opp->irq_msi + srs, 0);
		break;
	case 0x120:		/* MSISR */
		for (i = 0; i < MAX_MSI; i++)
			r |= (opp->msi[i].msir ? 1 : 0) << i;
		break;
	}

	return r;
}

static uint64_t openpic_summary_read(void *opaque, gpa_t addr, unsigned size)
{
	uint64_t r = 0;

	pr_debug("%s: addr %#" HWADDR_PRIx "\n", __func__, addr);

	/* TODO: EISR/EIMR */

	return r;
}

static void openpic_summary_write(void *opaque, gpa_t addr, uint64_t val,
				  unsigned size)
{
	pr_debug("%s: addr %#" HWADDR_PRIx " <= 0x%08" PRIx64 "\n",
		__func__, addr, val);

	/* TODO: EISR/EIMR */
}

static void openpic_cpu_write_internal(void *opaque, gpa_t addr,
				       uint32_t val, int idx)
{
	struct openpic *opp = opaque;
	struct irq_source *src;
	struct irq_dest *dst;
	int s_IRQ, n_IRQ;

	pr_debug("%s: cpu %d addr %#" HWADDR_PRIx " <= 0x%08x\n", __func__, idx,
		addr, val);

	if (idx < 0)
		return;

	if (addr & 0xF)
		return;

	dst = &opp->dst[idx];
	addr &= 0xFF0;
	switch (addr) {
	case 0x40:		/* IPIDR */
	case 0x50:
	case 0x60:
	case 0x70:
		idx = (addr - 0x40) >> 4;
		/* we use IDE as mask which CPUs to deliver the IPI to still. */
		opp->src[opp->irq_ipi0 + idx].destmask |= val;
		openpic_set_irq(opp, opp->irq_ipi0 + idx, 1);
		openpic_set_irq(opp, opp->irq_ipi0 + idx, 0);
		break;
	case 0x80:		/* CTPR */
		dst->ctpr = val & 0x0000000F;

		pr_debug("%s: set CPU %d ctpr to %d, raised %d servicing %d\n",
			__func__, idx, dst->ctpr, dst->raised.priority,
			dst->servicing.priority);

		if (dst->raised.priority <= dst->ctpr) {
			pr_debug("%s: Lower OpenPIC INT output cpu %d due to ctpr\n",
				__func__, idx);
			qemu_irq_lower(dst->irqs[OPENPIC_OUTPUT_INT]);
		} else if (dst->raised.priority > dst->servicing.priority) {
			pr_debug("%s: Raise OpenPIC INT output cpu %d irq %d\n",
				__func__, idx, dst->raised.next);
			qemu_irq_raise(dst->irqs[OPENPIC_OUTPUT_INT]);
		}

		break;
	case 0x90:		/* WHOAMI */
		/* Read-only register */
		break;
	case 0xA0:		/* IACK */
		/* Read-only register */
		break;
	case 0xB0:		/* EOI */
		pr_debug("EOI\n");
		s_IRQ = IRQ_get_next(opp, &dst->servicing);

		if (s_IRQ < 0) {
			pr_debug("%s: EOI with no interrupt in service\n",
				__func__);
			break;
		}

		IRQ_resetbit(&dst->servicing, s_IRQ);
		/* Set up next servicing IRQ */
		s_IRQ = IRQ_get_next(opp, &dst->servicing);
		/* Check queued interrupts. */
		n_IRQ = IRQ_get_next(opp, &dst->raised);
		src = &opp->src[n_IRQ];
		if (n_IRQ != -1 &&
		    (s_IRQ == -1 ||
		     IVPR_PRIORITY(src->ivpr) > dst->servicing.priority)) {
			pr_debug("Raise OpenPIC INT output cpu %d irq %d\n",
				idx, n_IRQ);
			qemu_irq_raise(opp->dst[idx].irqs[OPENPIC_OUTPUT_INT]);
		}
		break;
	default:
		break;
	}
}

static void openpic_cpu_write(void *opaque, gpa_t addr, uint64_t val,
			      unsigned len)
{
	openpic_cpu_write_internal(opaque, addr, val, (addr & 0x1f000) >> 12);
}

static uint32_t openpic_iack(struct openpic *opp, struct irq_dest *dst,
			     int cpu)
{
	struct irq_source *src;
	int retval, irq;

	pr_debug("Lower OpenPIC INT output\n");
	qemu_irq_lower(dst->irqs[OPENPIC_OUTPUT_INT]);

	irq = IRQ_get_next(opp, &dst->raised);
	pr_debug("IACK: irq=%d\n", irq);

	if (irq == -1)
		/* No more interrupt pending */
		return opp->spve;

	src = &opp->src[irq];
	if (!(src->ivpr & IVPR_ACTIVITY_MASK) ||
	    !(IVPR_PRIORITY(src->ivpr) > dst->ctpr)) {
		pr_err("%s: bad raised IRQ %d ctpr %d ivpr 0x%08x\n",
			__func__, irq, dst->ctpr, src->ivpr);
		openpic_update_irq(opp, irq);
		retval = opp->spve;
	} else {
		/* IRQ enter servicing state */
		IRQ_setbit(&dst->servicing, irq);
		retval = IVPR_VECTOR(opp, src->ivpr);
	}

	if (!src->level) {
		/* edge-sensitive IRQ */
		src->ivpr &= ~IVPR_ACTIVITY_MASK;
		src->pending = 0;
		IRQ_resetbit(&dst->raised, irq);
	}

	if ((irq >= opp->irq_ipi0) && (irq < (opp->irq_ipi0 + MAX_IPI))) {
		src->destmask &= ~(1 << cpu);
		if (src->destmask && !src->level) {
			/* trigger on CPUs that didn't know about it yet */
			openpic_set_irq(opp, irq, 1);
			openpic_set_irq(opp, irq, 0);
			/* if all CPUs knew about it, set active bit again */
			src->ivpr |= IVPR_ACTIVITY_MASK;
		}
	}

	return retval;
}

static uint32_t openpic_cpu_read_internal(void *opaque, gpa_t addr, int idx)
{
	struct openpic *opp = opaque;
	struct irq_dest *dst;
	uint32_t retval;

	pr_debug("%s: cpu %d addr %#" HWADDR_PRIx "\n", __func__, idx, addr);
	retval = 0xFFFFFFFF;

	if (idx < 0)
		return retval;

	if (addr & 0xF)
		return retval;

	dst = &opp->dst[idx];
	addr &= 0xFF0;
	switch (addr) {
	case 0x80:		/* CTPR */
		retval = dst->ctpr;
		break;
	case 0x90:		/* WHOAMI */
		retval = idx;
		break;
	case 0xA0:		/* IACK */
		retval = openpic_iack(opp, dst, idx);
		break;
	case 0xB0:		/* EOI */
		retval = 0;
		break;
	default:
		break;
	}
	pr_debug("%s: => 0x%08x\n", __func__, retval);

	return retval;
}

static uint64_t openpic_cpu_read(void *opaque, gpa_t addr, unsigned len)
{
	return openpic_cpu_read_internal(opaque, addr, (addr & 0x1f000) >> 12);
}

static const struct kvm_io_device_ops openpic_glb_ops_be = {
	.write = openpic_gbl_write,
	.read = openpic_gbl_read,
};

static const struct kvm_io_device_ops openpic_tmr_ops_be = {
	.write = openpic_tmr_write,
	.read = openpic_tmr_read,
};

static const struct kvm_io_device_ops openpic_cpu_ops_be = {
	.write = openpic_cpu_write,
	.read = openpic_cpu_read,
};

static const struct kvm_io_device_ops openpic_src_ops_be = {
	.write = openpic_src_write,
	.read = openpic_src_read,
};

static const struct kvm_io_device_ops openpic_msi_ops_be = {
	.read = openpic_msi_read,
	.write = openpic_msi_write,
};

static const struct kvm_io_device_ops openpic_summary_ops_be = {
	.read = openpic_summary_read,
	.write = openpic_summary_write,
};

struct mem_reg {
	const char *name;
	const struct kvm_io_device_ops *ops;
	gpa_t start_addr;
	int size;
};

static void fsl_common_init(struct openpic *opp)
{
	int i;
	int virq = MAX_SRC;

	opp->vid = VID_REVISION_1_2;
	opp->vir = VIR_GENERIC;
	opp->vector_mask = 0xFFFF;
	opp->tfrr_reset = 0;
	opp->ivpr_reset = IVPR_MASK_MASK;
	opp->idr_reset = 1 << 0;
	opp->max_irq = MAX_IRQ;

	opp->irq_ipi0 = virq;
	virq += MAX_IPI;
	opp->irq_tim0 = virq;
	virq += MAX_TMR;

	assert(virq <= MAX_IRQ);

	opp->irq_msi = 224;

	msi_supported = true;
	for (i = 0; i < opp->fsl->max_ext; i++)
		opp->src[i].level = false;

	/* Internal interrupts, including message and MSI */
	for (i = 16; i < MAX_SRC; i++) {
		opp->src[i].type = IRQ_TYPE_FSLINT;
		opp->src[i].level = true;
	}

	/* timers and IPIs */
	for (i = MAX_SRC; i < virq; i++) {
		opp->src[i].type = IRQ_TYPE_FSLSPECIAL;
		opp->src[i].level = false;
	}
}

static void map_list(struct openpic *opp, const struct mem_reg *list,
		     int *count)
{
	while (list->name) {
		assert(*count < ARRAY_SIZE(opp->sub_io_mem));

		memory_region_init_io(&opp->sub_io_mem[*count], list->ops, opp,
				      list->name, list->size);

		memory_region_add_subregion(&opp->mem, list->start_addr,
					    &opp->sub_io_mem[*count]);

		(*count)++;
		list++;
	}
}

static int openpic_init(SysBusDevice *dev)
{
	struct openpic *opp = FROM_SYSBUS(typeof(*opp), dev);
	int i, j;
	int list_count = 0;
	static const struct mem_reg list_le[] = {
		{"glb", &openpic_glb_ops_le,
		 OPENPIC_GLB_REG_START, OPENPIC_GLB_REG_SIZE},
		{"tmr", &openpic_tmr_ops_le,
		 OPENPIC_TMR_REG_START, OPENPIC_TMR_REG_SIZE},
		{"src", &openpic_src_ops_le,
		 OPENPIC_SRC_REG_START, OPENPIC_SRC_REG_SIZE},
		{"cpu", &openpic_cpu_ops_le,
		 OPENPIC_CPU_REG_START, OPENPIC_CPU_REG_SIZE},
		{NULL}
	};
	static const struct mem_reg list_be[] = {
		{"glb", &openpic_glb_ops_be,
		 OPENPIC_GLB_REG_START, OPENPIC_GLB_REG_SIZE},
		{"tmr", &openpic_tmr_ops_be,
		 OPENPIC_TMR_REG_START, OPENPIC_TMR_REG_SIZE},
		{"src", &openpic_src_ops_be,
		 OPENPIC_SRC_REG_START, OPENPIC_SRC_REG_SIZE},
		{"cpu", &openpic_cpu_ops_be,
		 OPENPIC_CPU_REG_START, OPENPIC_CPU_REG_SIZE},
		{NULL}
	};
	static const struct mem_reg list_fsl[] = {
		{"msi", &openpic_msi_ops_be,
		 OPENPIC_MSI_REG_START, OPENPIC_MSI_REG_SIZE},
		{"summary", &openpic_summary_ops_be,
		 OPENPIC_SUMMARY_REG_START, OPENPIC_SUMMARY_REG_SIZE},
		{NULL}
	};

	memory_region_init(&opp->mem, "openpic", 0x40000);

	switch (opp->model) {
	case OPENPIC_MODEL_FSL_MPIC_20:
	default:
		opp->fsl = &fsl_mpic_20;
		opp->brr1 = 0x00400200;
		opp->flags |= OPENPIC_FLAG_IDR_CRIT;
		opp->nb_irqs = 80;
		opp->mpic_mode_mask = GCR_MODE_MIXED;

		fsl_common_init(opp);
		map_list(opp, list_be, &list_count);
		map_list(opp, list_fsl, &list_count);

		break;

	case OPENPIC_MODEL_FSL_MPIC_42:
		opp->fsl = &fsl_mpic_42;
		opp->brr1 = 0x00400402;
		opp->flags |= OPENPIC_FLAG_ILR;
		opp->nb_irqs = 196;
		opp->mpic_mode_mask = GCR_MODE_PROXY;

		fsl_common_init(opp);
		map_list(opp, list_be, &list_count);
		map_list(opp, list_fsl, &list_count);

		break;
	}

	return 0;
}
