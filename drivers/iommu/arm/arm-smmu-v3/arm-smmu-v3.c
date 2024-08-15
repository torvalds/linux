// SPDX-License-Identifier: GPL-2.0
/*
 * IOMMU API for ARM architected SMMUv3 implementations.
 *
 * Copyright (C) 2015 ARM Limited
 *
 * Author: Will Deacon <will.deacon@arm.com>
 *
 * This driver is powered by bad coffee and bombay mix.
 */

#include <linux/acpi.h>
#include <linux/acpi_iort.h>
#include <linux/bitops.h>
#include <linux/crash_dump.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io-pgtable.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/msi.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/pci-ats.h>
#include <linux/platform_device.h>
#include <kunit/visibility.h>
#include <uapi/linux/iommufd.h>

#include "arm-smmu-v3.h"
#include "../../dma-iommu.h"

static bool disable_msipolling;
module_param(disable_msipolling, bool, 0444);
MODULE_PARM_DESC(disable_msipolling,
	"Disable MSI-based polling for CMD_SYNC completion.");

static struct iommu_ops arm_smmu_ops;
static struct iommu_dirty_ops arm_smmu_dirty_ops;

enum arm_smmu_msi_index {
	EVTQ_MSI_INDEX,
	GERROR_MSI_INDEX,
	PRIQ_MSI_INDEX,
	ARM_SMMU_MAX_MSIS,
};

#define NUM_ENTRY_QWORDS 8
static_assert(sizeof(struct arm_smmu_ste) == NUM_ENTRY_QWORDS * sizeof(u64));
static_assert(sizeof(struct arm_smmu_cd) == NUM_ENTRY_QWORDS * sizeof(u64));

static phys_addr_t arm_smmu_msi_cfg[ARM_SMMU_MAX_MSIS][3] = {
	[EVTQ_MSI_INDEX] = {
		ARM_SMMU_EVTQ_IRQ_CFG0,
		ARM_SMMU_EVTQ_IRQ_CFG1,
		ARM_SMMU_EVTQ_IRQ_CFG2,
	},
	[GERROR_MSI_INDEX] = {
		ARM_SMMU_GERROR_IRQ_CFG0,
		ARM_SMMU_GERROR_IRQ_CFG1,
		ARM_SMMU_GERROR_IRQ_CFG2,
	},
	[PRIQ_MSI_INDEX] = {
		ARM_SMMU_PRIQ_IRQ_CFG0,
		ARM_SMMU_PRIQ_IRQ_CFG1,
		ARM_SMMU_PRIQ_IRQ_CFG2,
	},
};

struct arm_smmu_option_prop {
	u32 opt;
	const char *prop;
};

DEFINE_XARRAY_ALLOC1(arm_smmu_asid_xa);
DEFINE_MUTEX(arm_smmu_asid_lock);

static struct arm_smmu_option_prop arm_smmu_options[] = {
	{ ARM_SMMU_OPT_SKIP_PREFETCH, "hisilicon,broken-prefetch-cmd" },
	{ ARM_SMMU_OPT_PAGE0_REGS_ONLY, "cavium,cn9900-broken-page1-regspace"},
	{ 0, NULL},
};

static int arm_smmu_domain_finalise(struct arm_smmu_domain *smmu_domain,
				    struct arm_smmu_device *smmu, u32 flags);
static int arm_smmu_alloc_cd_tables(struct arm_smmu_master *master);

static void parse_driver_options(struct arm_smmu_device *smmu)
{
	int i = 0;

	do {
		if (of_property_read_bool(smmu->dev->of_node,
						arm_smmu_options[i].prop)) {
			smmu->options |= arm_smmu_options[i].opt;
			dev_notice(smmu->dev, "option %s\n",
				arm_smmu_options[i].prop);
		}
	} while (arm_smmu_options[++i].opt);
}

/* Low-level queue manipulation functions */
static bool queue_has_space(struct arm_smmu_ll_queue *q, u32 n)
{
	u32 space, prod, cons;

	prod = Q_IDX(q, q->prod);
	cons = Q_IDX(q, q->cons);

	if (Q_WRP(q, q->prod) == Q_WRP(q, q->cons))
		space = (1 << q->max_n_shift) - (prod - cons);
	else
		space = cons - prod;

	return space >= n;
}

static bool queue_full(struct arm_smmu_ll_queue *q)
{
	return Q_IDX(q, q->prod) == Q_IDX(q, q->cons) &&
	       Q_WRP(q, q->prod) != Q_WRP(q, q->cons);
}

static bool queue_empty(struct arm_smmu_ll_queue *q)
{
	return Q_IDX(q, q->prod) == Q_IDX(q, q->cons) &&
	       Q_WRP(q, q->prod) == Q_WRP(q, q->cons);
}

static bool queue_consumed(struct arm_smmu_ll_queue *q, u32 prod)
{
	return ((Q_WRP(q, q->cons) == Q_WRP(q, prod)) &&
		(Q_IDX(q, q->cons) > Q_IDX(q, prod))) ||
	       ((Q_WRP(q, q->cons) != Q_WRP(q, prod)) &&
		(Q_IDX(q, q->cons) <= Q_IDX(q, prod)));
}

static void queue_sync_cons_out(struct arm_smmu_queue *q)
{
	/*
	 * Ensure that all CPU accesses (reads and writes) to the queue
	 * are complete before we update the cons pointer.
	 */
	__iomb();
	writel_relaxed(q->llq.cons, q->cons_reg);
}

static void queue_inc_cons(struct arm_smmu_ll_queue *q)
{
	u32 cons = (Q_WRP(q, q->cons) | Q_IDX(q, q->cons)) + 1;
	q->cons = Q_OVF(q->cons) | Q_WRP(q, cons) | Q_IDX(q, cons);
}

static void queue_sync_cons_ovf(struct arm_smmu_queue *q)
{
	struct arm_smmu_ll_queue *llq = &q->llq;

	if (likely(Q_OVF(llq->prod) == Q_OVF(llq->cons)))
		return;

	llq->cons = Q_OVF(llq->prod) | Q_WRP(llq, llq->cons) |
		      Q_IDX(llq, llq->cons);
	queue_sync_cons_out(q);
}

static int queue_sync_prod_in(struct arm_smmu_queue *q)
{
	u32 prod;
	int ret = 0;

	/*
	 * We can't use the _relaxed() variant here, as we must prevent
	 * speculative reads of the queue before we have determined that
	 * prod has indeed moved.
	 */
	prod = readl(q->prod_reg);

	if (Q_OVF(prod) != Q_OVF(q->llq.prod))
		ret = -EOVERFLOW;

	q->llq.prod = prod;
	return ret;
}

static u32 queue_inc_prod_n(struct arm_smmu_ll_queue *q, int n)
{
	u32 prod = (Q_WRP(q, q->prod) | Q_IDX(q, q->prod)) + n;
	return Q_OVF(q->prod) | Q_WRP(q, prod) | Q_IDX(q, prod);
}

static void queue_poll_init(struct arm_smmu_device *smmu,
			    struct arm_smmu_queue_poll *qp)
{
	qp->delay = 1;
	qp->spin_cnt = 0;
	qp->wfe = !!(smmu->features & ARM_SMMU_FEAT_SEV);
	qp->timeout = ktime_add_us(ktime_get(), ARM_SMMU_POLL_TIMEOUT_US);
}

static int queue_poll(struct arm_smmu_queue_poll *qp)
{
	if (ktime_compare(ktime_get(), qp->timeout) > 0)
		return -ETIMEDOUT;

	if (qp->wfe) {
		wfe();
	} else if (++qp->spin_cnt < ARM_SMMU_POLL_SPIN_COUNT) {
		cpu_relax();
	} else {
		udelay(qp->delay);
		qp->delay *= 2;
		qp->spin_cnt = 0;
	}

	return 0;
}

static void queue_write(__le64 *dst, u64 *src, size_t n_dwords)
{
	int i;

	for (i = 0; i < n_dwords; ++i)
		*dst++ = cpu_to_le64(*src++);
}

static void queue_read(u64 *dst, __le64 *src, size_t n_dwords)
{
	int i;

	for (i = 0; i < n_dwords; ++i)
		*dst++ = le64_to_cpu(*src++);
}

static int queue_remove_raw(struct arm_smmu_queue *q, u64 *ent)
{
	if (queue_empty(&q->llq))
		return -EAGAIN;

	queue_read(ent, Q_ENT(q, q->llq.cons), q->ent_dwords);
	queue_inc_cons(&q->llq);
	queue_sync_cons_out(q);
	return 0;
}

/* High-level queue accessors */
static int arm_smmu_cmdq_build_cmd(u64 *cmd, struct arm_smmu_cmdq_ent *ent)
{
	memset(cmd, 0, 1 << CMDQ_ENT_SZ_SHIFT);
	cmd[0] |= FIELD_PREP(CMDQ_0_OP, ent->opcode);

	switch (ent->opcode) {
	case CMDQ_OP_TLBI_EL2_ALL:
	case CMDQ_OP_TLBI_NSNH_ALL:
		break;
	case CMDQ_OP_PREFETCH_CFG:
		cmd[0] |= FIELD_PREP(CMDQ_PREFETCH_0_SID, ent->prefetch.sid);
		break;
	case CMDQ_OP_CFGI_CD:
		cmd[0] |= FIELD_PREP(CMDQ_CFGI_0_SSID, ent->cfgi.ssid);
		fallthrough;
	case CMDQ_OP_CFGI_STE:
		cmd[0] |= FIELD_PREP(CMDQ_CFGI_0_SID, ent->cfgi.sid);
		cmd[1] |= FIELD_PREP(CMDQ_CFGI_1_LEAF, ent->cfgi.leaf);
		break;
	case CMDQ_OP_CFGI_CD_ALL:
		cmd[0] |= FIELD_PREP(CMDQ_CFGI_0_SID, ent->cfgi.sid);
		break;
	case CMDQ_OP_CFGI_ALL:
		/* Cover the entire SID range */
		cmd[1] |= FIELD_PREP(CMDQ_CFGI_1_RANGE, 31);
		break;
	case CMDQ_OP_TLBI_NH_VA:
		cmd[0] |= FIELD_PREP(CMDQ_TLBI_0_VMID, ent->tlbi.vmid);
		fallthrough;
	case CMDQ_OP_TLBI_EL2_VA:
		cmd[0] |= FIELD_PREP(CMDQ_TLBI_0_NUM, ent->tlbi.num);
		cmd[0] |= FIELD_PREP(CMDQ_TLBI_0_SCALE, ent->tlbi.scale);
		cmd[0] |= FIELD_PREP(CMDQ_TLBI_0_ASID, ent->tlbi.asid);
		cmd[1] |= FIELD_PREP(CMDQ_TLBI_1_LEAF, ent->tlbi.leaf);
		cmd[1] |= FIELD_PREP(CMDQ_TLBI_1_TTL, ent->tlbi.ttl);
		cmd[1] |= FIELD_PREP(CMDQ_TLBI_1_TG, ent->tlbi.tg);
		cmd[1] |= ent->tlbi.addr & CMDQ_TLBI_1_VA_MASK;
		break;
	case CMDQ_OP_TLBI_S2_IPA:
		cmd[0] |= FIELD_PREP(CMDQ_TLBI_0_NUM, ent->tlbi.num);
		cmd[0] |= FIELD_PREP(CMDQ_TLBI_0_SCALE, ent->tlbi.scale);
		cmd[0] |= FIELD_PREP(CMDQ_TLBI_0_VMID, ent->tlbi.vmid);
		cmd[1] |= FIELD_PREP(CMDQ_TLBI_1_LEAF, ent->tlbi.leaf);
		cmd[1] |= FIELD_PREP(CMDQ_TLBI_1_TTL, ent->tlbi.ttl);
		cmd[1] |= FIELD_PREP(CMDQ_TLBI_1_TG, ent->tlbi.tg);
		cmd[1] |= ent->tlbi.addr & CMDQ_TLBI_1_IPA_MASK;
		break;
	case CMDQ_OP_TLBI_NH_ASID:
		cmd[0] |= FIELD_PREP(CMDQ_TLBI_0_ASID, ent->tlbi.asid);
		fallthrough;
	case CMDQ_OP_TLBI_S12_VMALL:
		cmd[0] |= FIELD_PREP(CMDQ_TLBI_0_VMID, ent->tlbi.vmid);
		break;
	case CMDQ_OP_TLBI_EL2_ASID:
		cmd[0] |= FIELD_PREP(CMDQ_TLBI_0_ASID, ent->tlbi.asid);
		break;
	case CMDQ_OP_ATC_INV:
		cmd[0] |= FIELD_PREP(CMDQ_0_SSV, ent->substream_valid);
		cmd[0] |= FIELD_PREP(CMDQ_ATC_0_GLOBAL, ent->atc.global);
		cmd[0] |= FIELD_PREP(CMDQ_ATC_0_SSID, ent->atc.ssid);
		cmd[0] |= FIELD_PREP(CMDQ_ATC_0_SID, ent->atc.sid);
		cmd[1] |= FIELD_PREP(CMDQ_ATC_1_SIZE, ent->atc.size);
		cmd[1] |= ent->atc.addr & CMDQ_ATC_1_ADDR_MASK;
		break;
	case CMDQ_OP_PRI_RESP:
		cmd[0] |= FIELD_PREP(CMDQ_0_SSV, ent->substream_valid);
		cmd[0] |= FIELD_PREP(CMDQ_PRI_0_SSID, ent->pri.ssid);
		cmd[0] |= FIELD_PREP(CMDQ_PRI_0_SID, ent->pri.sid);
		cmd[1] |= FIELD_PREP(CMDQ_PRI_1_GRPID, ent->pri.grpid);
		switch (ent->pri.resp) {
		case PRI_RESP_DENY:
		case PRI_RESP_FAIL:
		case PRI_RESP_SUCC:
			break;
		default:
			return -EINVAL;
		}
		cmd[1] |= FIELD_PREP(CMDQ_PRI_1_RESP, ent->pri.resp);
		break;
	case CMDQ_OP_RESUME:
		cmd[0] |= FIELD_PREP(CMDQ_RESUME_0_SID, ent->resume.sid);
		cmd[0] |= FIELD_PREP(CMDQ_RESUME_0_RESP, ent->resume.resp);
		cmd[1] |= FIELD_PREP(CMDQ_RESUME_1_STAG, ent->resume.stag);
		break;
	case CMDQ_OP_CMD_SYNC:
		if (ent->sync.msiaddr) {
			cmd[0] |= FIELD_PREP(CMDQ_SYNC_0_CS, CMDQ_SYNC_0_CS_IRQ);
			cmd[1] |= ent->sync.msiaddr & CMDQ_SYNC_1_MSIADDR_MASK;
		} else {
			cmd[0] |= FIELD_PREP(CMDQ_SYNC_0_CS, CMDQ_SYNC_0_CS_SEV);
		}
		cmd[0] |= FIELD_PREP(CMDQ_SYNC_0_MSH, ARM_SMMU_SH_ISH);
		cmd[0] |= FIELD_PREP(CMDQ_SYNC_0_MSIATTR, ARM_SMMU_MEMATTR_OIWB);
		break;
	default:
		return -ENOENT;
	}

	return 0;
}

static struct arm_smmu_cmdq *arm_smmu_get_cmdq(struct arm_smmu_device *smmu)
{
	return &smmu->cmdq;
}

static void arm_smmu_cmdq_build_sync_cmd(u64 *cmd, struct arm_smmu_device *smmu,
					 struct arm_smmu_queue *q, u32 prod)
{
	struct arm_smmu_cmdq_ent ent = {
		.opcode = CMDQ_OP_CMD_SYNC,
	};

	/*
	 * Beware that Hi16xx adds an extra 32 bits of goodness to its MSI
	 * payload, so the write will zero the entire command on that platform.
	 */
	if (smmu->options & ARM_SMMU_OPT_MSIPOLL) {
		ent.sync.msiaddr = q->base_dma + Q_IDX(&q->llq, prod) *
				   q->ent_dwords * 8;
	}

	arm_smmu_cmdq_build_cmd(cmd, &ent);
}

static void __arm_smmu_cmdq_skip_err(struct arm_smmu_device *smmu,
				     struct arm_smmu_queue *q)
{
	static const char * const cerror_str[] = {
		[CMDQ_ERR_CERROR_NONE_IDX]	= "No error",
		[CMDQ_ERR_CERROR_ILL_IDX]	= "Illegal command",
		[CMDQ_ERR_CERROR_ABT_IDX]	= "Abort on command fetch",
		[CMDQ_ERR_CERROR_ATC_INV_IDX]	= "ATC invalidate timeout",
	};

	int i;
	u64 cmd[CMDQ_ENT_DWORDS];
	u32 cons = readl_relaxed(q->cons_reg);
	u32 idx = FIELD_GET(CMDQ_CONS_ERR, cons);
	struct arm_smmu_cmdq_ent cmd_sync = {
		.opcode = CMDQ_OP_CMD_SYNC,
	};

	dev_err(smmu->dev, "CMDQ error (cons 0x%08x): %s\n", cons,
		idx < ARRAY_SIZE(cerror_str) ?  cerror_str[idx] : "Unknown");

	switch (idx) {
	case CMDQ_ERR_CERROR_ABT_IDX:
		dev_err(smmu->dev, "retrying command fetch\n");
		return;
	case CMDQ_ERR_CERROR_NONE_IDX:
		return;
	case CMDQ_ERR_CERROR_ATC_INV_IDX:
		/*
		 * ATC Invalidation Completion timeout. CONS is still pointing
		 * at the CMD_SYNC. Attempt to complete other pending commands
		 * by repeating the CMD_SYNC, though we might well end up back
		 * here since the ATC invalidation may still be pending.
		 */
		return;
	case CMDQ_ERR_CERROR_ILL_IDX:
	default:
		break;
	}

	/*
	 * We may have concurrent producers, so we need to be careful
	 * not to touch any of the shadow cmdq state.
	 */
	queue_read(cmd, Q_ENT(q, cons), q->ent_dwords);
	dev_err(smmu->dev, "skipping command in error state:\n");
	for (i = 0; i < ARRAY_SIZE(cmd); ++i)
		dev_err(smmu->dev, "\t0x%016llx\n", (unsigned long long)cmd[i]);

	/* Convert the erroneous command into a CMD_SYNC */
	arm_smmu_cmdq_build_cmd(cmd, &cmd_sync);

	queue_write(Q_ENT(q, cons), cmd, q->ent_dwords);
}

static void arm_smmu_cmdq_skip_err(struct arm_smmu_device *smmu)
{
	__arm_smmu_cmdq_skip_err(smmu, &smmu->cmdq.q);
}

/*
 * Command queue locking.
 * This is a form of bastardised rwlock with the following major changes:
 *
 * - The only LOCK routines are exclusive_trylock() and shared_lock().
 *   Neither have barrier semantics, and instead provide only a control
 *   dependency.
 *
 * - The UNLOCK routines are supplemented with shared_tryunlock(), which
 *   fails if the caller appears to be the last lock holder (yes, this is
 *   racy). All successful UNLOCK routines have RELEASE semantics.
 */
static void arm_smmu_cmdq_shared_lock(struct arm_smmu_cmdq *cmdq)
{
	int val;

	/*
	 * We can try to avoid the cmpxchg() loop by simply incrementing the
	 * lock counter. When held in exclusive state, the lock counter is set
	 * to INT_MIN so these increments won't hurt as the value will remain
	 * negative.
	 */
	if (atomic_fetch_inc_relaxed(&cmdq->lock) >= 0)
		return;

	do {
		val = atomic_cond_read_relaxed(&cmdq->lock, VAL >= 0);
	} while (atomic_cmpxchg_relaxed(&cmdq->lock, val, val + 1) != val);
}

static void arm_smmu_cmdq_shared_unlock(struct arm_smmu_cmdq *cmdq)
{
	(void)atomic_dec_return_release(&cmdq->lock);
}

static bool arm_smmu_cmdq_shared_tryunlock(struct arm_smmu_cmdq *cmdq)
{
	if (atomic_read(&cmdq->lock) == 1)
		return false;

	arm_smmu_cmdq_shared_unlock(cmdq);
	return true;
}

#define arm_smmu_cmdq_exclusive_trylock_irqsave(cmdq, flags)		\
({									\
	bool __ret;							\
	local_irq_save(flags);						\
	__ret = !atomic_cmpxchg_relaxed(&cmdq->lock, 0, INT_MIN);	\
	if (!__ret)							\
		local_irq_restore(flags);				\
	__ret;								\
})

#define arm_smmu_cmdq_exclusive_unlock_irqrestore(cmdq, flags)		\
({									\
	atomic_set_release(&cmdq->lock, 0);				\
	local_irq_restore(flags);					\
})


/*
 * Command queue insertion.
 * This is made fiddly by our attempts to achieve some sort of scalability
 * since there is one queue shared amongst all of the CPUs in the system.  If
 * you like mixed-size concurrency, dependency ordering and relaxed atomics,
 * then you'll *love* this monstrosity.
 *
 * The basic idea is to split the queue up into ranges of commands that are
 * owned by a given CPU; the owner may not have written all of the commands
 * itself, but is responsible for advancing the hardware prod pointer when
 * the time comes. The algorithm is roughly:
 *
 * 	1. Allocate some space in the queue. At this point we also discover
 *	   whether the head of the queue is currently owned by another CPU,
 *	   or whether we are the owner.
 *
 *	2. Write our commands into our allocated slots in the queue.
 *
 *	3. Mark our slots as valid in arm_smmu_cmdq.valid_map.
 *
 *	4. If we are an owner:
 *		a. Wait for the previous owner to finish.
 *		b. Mark the queue head as unowned, which tells us the range
 *		   that we are responsible for publishing.
 *		c. Wait for all commands in our owned range to become valid.
 *		d. Advance the hardware prod pointer.
 *		e. Tell the next owner we've finished.
 *
 *	5. If we are inserting a CMD_SYNC (we may or may not have been an
 *	   owner), then we need to stick around until it has completed:
 *		a. If we have MSIs, the SMMU can write back into the CMD_SYNC
 *		   to clear the first 4 bytes.
 *		b. Otherwise, we spin waiting for the hardware cons pointer to
 *		   advance past our command.
 *
 * The devil is in the details, particularly the use of locking for handling
 * SYNC completion and freeing up space in the queue before we think that it is
 * full.
 */
static void __arm_smmu_cmdq_poll_set_valid_map(struct arm_smmu_cmdq *cmdq,
					       u32 sprod, u32 eprod, bool set)
{
	u32 swidx, sbidx, ewidx, ebidx;
	struct arm_smmu_ll_queue llq = {
		.max_n_shift	= cmdq->q.llq.max_n_shift,
		.prod		= sprod,
	};

	ewidx = BIT_WORD(Q_IDX(&llq, eprod));
	ebidx = Q_IDX(&llq, eprod) % BITS_PER_LONG;

	while (llq.prod != eprod) {
		unsigned long mask;
		atomic_long_t *ptr;
		u32 limit = BITS_PER_LONG;

		swidx = BIT_WORD(Q_IDX(&llq, llq.prod));
		sbidx = Q_IDX(&llq, llq.prod) % BITS_PER_LONG;

		ptr = &cmdq->valid_map[swidx];

		if ((swidx == ewidx) && (sbidx < ebidx))
			limit = ebidx;

		mask = GENMASK(limit - 1, sbidx);

		/*
		 * The valid bit is the inverse of the wrap bit. This means
		 * that a zero-initialised queue is invalid and, after marking
		 * all entries as valid, they become invalid again when we
		 * wrap.
		 */
		if (set) {
			atomic_long_xor(mask, ptr);
		} else { /* Poll */
			unsigned long valid;

			valid = (ULONG_MAX + !!Q_WRP(&llq, llq.prod)) & mask;
			atomic_long_cond_read_relaxed(ptr, (VAL & mask) == valid);
		}

		llq.prod = queue_inc_prod_n(&llq, limit - sbidx);
	}
}

/* Mark all entries in the range [sprod, eprod) as valid */
static void arm_smmu_cmdq_set_valid_map(struct arm_smmu_cmdq *cmdq,
					u32 sprod, u32 eprod)
{
	__arm_smmu_cmdq_poll_set_valid_map(cmdq, sprod, eprod, true);
}

/* Wait for all entries in the range [sprod, eprod) to become valid */
static void arm_smmu_cmdq_poll_valid_map(struct arm_smmu_cmdq *cmdq,
					 u32 sprod, u32 eprod)
{
	__arm_smmu_cmdq_poll_set_valid_map(cmdq, sprod, eprod, false);
}

/* Wait for the command queue to become non-full */
static int arm_smmu_cmdq_poll_until_not_full(struct arm_smmu_device *smmu,
					     struct arm_smmu_ll_queue *llq)
{
	unsigned long flags;
	struct arm_smmu_queue_poll qp;
	struct arm_smmu_cmdq *cmdq = arm_smmu_get_cmdq(smmu);
	int ret = 0;

	/*
	 * Try to update our copy of cons by grabbing exclusive cmdq access. If
	 * that fails, spin until somebody else updates it for us.
	 */
	if (arm_smmu_cmdq_exclusive_trylock_irqsave(cmdq, flags)) {
		WRITE_ONCE(cmdq->q.llq.cons, readl_relaxed(cmdq->q.cons_reg));
		arm_smmu_cmdq_exclusive_unlock_irqrestore(cmdq, flags);
		llq->val = READ_ONCE(cmdq->q.llq.val);
		return 0;
	}

	queue_poll_init(smmu, &qp);
	do {
		llq->val = READ_ONCE(cmdq->q.llq.val);
		if (!queue_full(llq))
			break;

		ret = queue_poll(&qp);
	} while (!ret);

	return ret;
}

/*
 * Wait until the SMMU signals a CMD_SYNC completion MSI.
 * Must be called with the cmdq lock held in some capacity.
 */
static int __arm_smmu_cmdq_poll_until_msi(struct arm_smmu_device *smmu,
					  struct arm_smmu_ll_queue *llq)
{
	int ret = 0;
	struct arm_smmu_queue_poll qp;
	struct arm_smmu_cmdq *cmdq = arm_smmu_get_cmdq(smmu);
	u32 *cmd = (u32 *)(Q_ENT(&cmdq->q, llq->prod));

	queue_poll_init(smmu, &qp);

	/*
	 * The MSI won't generate an event, since it's being written back
	 * into the command queue.
	 */
	qp.wfe = false;
	smp_cond_load_relaxed(cmd, !VAL || (ret = queue_poll(&qp)));
	llq->cons = ret ? llq->prod : queue_inc_prod_n(llq, 1);
	return ret;
}

/*
 * Wait until the SMMU cons index passes llq->prod.
 * Must be called with the cmdq lock held in some capacity.
 */
static int __arm_smmu_cmdq_poll_until_consumed(struct arm_smmu_device *smmu,
					       struct arm_smmu_ll_queue *llq)
{
	struct arm_smmu_queue_poll qp;
	struct arm_smmu_cmdq *cmdq = arm_smmu_get_cmdq(smmu);
	u32 prod = llq->prod;
	int ret = 0;

	queue_poll_init(smmu, &qp);
	llq->val = READ_ONCE(cmdq->q.llq.val);
	do {
		if (queue_consumed(llq, prod))
			break;

		ret = queue_poll(&qp);

		/*
		 * This needs to be a readl() so that our subsequent call
		 * to arm_smmu_cmdq_shared_tryunlock() can fail accurately.
		 *
		 * Specifically, we need to ensure that we observe all
		 * shared_lock()s by other CMD_SYNCs that share our owner,
		 * so that a failing call to tryunlock() means that we're
		 * the last one out and therefore we can safely advance
		 * cmdq->q.llq.cons. Roughly speaking:
		 *
		 * CPU 0		CPU1			CPU2 (us)
		 *
		 * if (sync)
		 * 	shared_lock();
		 *
		 * dma_wmb();
		 * set_valid_map();
		 *
		 * 			if (owner) {
		 *				poll_valid_map();
		 *				<control dependency>
		 *				writel(prod_reg);
		 *
		 *						readl(cons_reg);
		 *						tryunlock();
		 *
		 * Requires us to see CPU 0's shared_lock() acquisition.
		 */
		llq->cons = readl(cmdq->q.cons_reg);
	} while (!ret);

	return ret;
}

static int arm_smmu_cmdq_poll_until_sync(struct arm_smmu_device *smmu,
					 struct arm_smmu_ll_queue *llq)
{
	if (smmu->options & ARM_SMMU_OPT_MSIPOLL)
		return __arm_smmu_cmdq_poll_until_msi(smmu, llq);

	return __arm_smmu_cmdq_poll_until_consumed(smmu, llq);
}

static void arm_smmu_cmdq_write_entries(struct arm_smmu_cmdq *cmdq, u64 *cmds,
					u32 prod, int n)
{
	int i;
	struct arm_smmu_ll_queue llq = {
		.max_n_shift	= cmdq->q.llq.max_n_shift,
		.prod		= prod,
	};

	for (i = 0; i < n; ++i) {
		u64 *cmd = &cmds[i * CMDQ_ENT_DWORDS];

		prod = queue_inc_prod_n(&llq, i);
		queue_write(Q_ENT(&cmdq->q, prod), cmd, CMDQ_ENT_DWORDS);
	}
}

/*
 * This is the actual insertion function, and provides the following
 * ordering guarantees to callers:
 *
 * - There is a dma_wmb() before publishing any commands to the queue.
 *   This can be relied upon to order prior writes to data structures
 *   in memory (such as a CD or an STE) before the command.
 *
 * - On completion of a CMD_SYNC, there is a control dependency.
 *   This can be relied upon to order subsequent writes to memory (e.g.
 *   freeing an IOVA) after completion of the CMD_SYNC.
 *
 * - Command insertion is totally ordered, so if two CPUs each race to
 *   insert their own list of commands then all of the commands from one
 *   CPU will appear before any of the commands from the other CPU.
 */
static int arm_smmu_cmdq_issue_cmdlist(struct arm_smmu_device *smmu,
				       u64 *cmds, int n, bool sync)
{
	u64 cmd_sync[CMDQ_ENT_DWORDS];
	u32 prod;
	unsigned long flags;
	bool owner;
	struct arm_smmu_cmdq *cmdq = arm_smmu_get_cmdq(smmu);
	struct arm_smmu_ll_queue llq, head;
	int ret = 0;

	llq.max_n_shift = cmdq->q.llq.max_n_shift;

	/* 1. Allocate some space in the queue */
	local_irq_save(flags);
	llq.val = READ_ONCE(cmdq->q.llq.val);
	do {
		u64 old;

		while (!queue_has_space(&llq, n + sync)) {
			local_irq_restore(flags);
			if (arm_smmu_cmdq_poll_until_not_full(smmu, &llq))
				dev_err_ratelimited(smmu->dev, "CMDQ timeout\n");
			local_irq_save(flags);
		}

		head.cons = llq.cons;
		head.prod = queue_inc_prod_n(&llq, n + sync) |
					     CMDQ_PROD_OWNED_FLAG;

		old = cmpxchg_relaxed(&cmdq->q.llq.val, llq.val, head.val);
		if (old == llq.val)
			break;

		llq.val = old;
	} while (1);
	owner = !(llq.prod & CMDQ_PROD_OWNED_FLAG);
	head.prod &= ~CMDQ_PROD_OWNED_FLAG;
	llq.prod &= ~CMDQ_PROD_OWNED_FLAG;

	/*
	 * 2. Write our commands into the queue
	 * Dependency ordering from the cmpxchg() loop above.
	 */
	arm_smmu_cmdq_write_entries(cmdq, cmds, llq.prod, n);
	if (sync) {
		prod = queue_inc_prod_n(&llq, n);
		arm_smmu_cmdq_build_sync_cmd(cmd_sync, smmu, &cmdq->q, prod);
		queue_write(Q_ENT(&cmdq->q, prod), cmd_sync, CMDQ_ENT_DWORDS);

		/*
		 * In order to determine completion of our CMD_SYNC, we must
		 * ensure that the queue can't wrap twice without us noticing.
		 * We achieve that by taking the cmdq lock as shared before
		 * marking our slot as valid.
		 */
		arm_smmu_cmdq_shared_lock(cmdq);
	}

	/* 3. Mark our slots as valid, ensuring commands are visible first */
	dma_wmb();
	arm_smmu_cmdq_set_valid_map(cmdq, llq.prod, head.prod);

	/* 4. If we are the owner, take control of the SMMU hardware */
	if (owner) {
		/* a. Wait for previous owner to finish */
		atomic_cond_read_relaxed(&cmdq->owner_prod, VAL == llq.prod);

		/* b. Stop gathering work by clearing the owned flag */
		prod = atomic_fetch_andnot_relaxed(CMDQ_PROD_OWNED_FLAG,
						   &cmdq->q.llq.atomic.prod);
		prod &= ~CMDQ_PROD_OWNED_FLAG;

		/*
		 * c. Wait for any gathered work to be written to the queue.
		 * Note that we read our own entries so that we have the control
		 * dependency required by (d).
		 */
		arm_smmu_cmdq_poll_valid_map(cmdq, llq.prod, prod);

		/*
		 * d. Advance the hardware prod pointer
		 * Control dependency ordering from the entries becoming valid.
		 */
		writel_relaxed(prod, cmdq->q.prod_reg);

		/*
		 * e. Tell the next owner we're done
		 * Make sure we've updated the hardware first, so that we don't
		 * race to update prod and potentially move it backwards.
		 */
		atomic_set_release(&cmdq->owner_prod, prod);
	}

	/* 5. If we are inserting a CMD_SYNC, we must wait for it to complete */
	if (sync) {
		llq.prod = queue_inc_prod_n(&llq, n);
		ret = arm_smmu_cmdq_poll_until_sync(smmu, &llq);
		if (ret) {
			dev_err_ratelimited(smmu->dev,
					    "CMD_SYNC timeout at 0x%08x [hwprod 0x%08x, hwcons 0x%08x]\n",
					    llq.prod,
					    readl_relaxed(cmdq->q.prod_reg),
					    readl_relaxed(cmdq->q.cons_reg));
		}

		/*
		 * Try to unlock the cmdq lock. This will fail if we're the last
		 * reader, in which case we can safely update cmdq->q.llq.cons
		 */
		if (!arm_smmu_cmdq_shared_tryunlock(cmdq)) {
			WRITE_ONCE(cmdq->q.llq.cons, llq.cons);
			arm_smmu_cmdq_shared_unlock(cmdq);
		}
	}

	local_irq_restore(flags);
	return ret;
}

static int __arm_smmu_cmdq_issue_cmd(struct arm_smmu_device *smmu,
				     struct arm_smmu_cmdq_ent *ent,
				     bool sync)
{
	u64 cmd[CMDQ_ENT_DWORDS];

	if (unlikely(arm_smmu_cmdq_build_cmd(cmd, ent))) {
		dev_warn(smmu->dev, "ignoring unknown CMDQ opcode 0x%x\n",
			 ent->opcode);
		return -EINVAL;
	}

	return arm_smmu_cmdq_issue_cmdlist(smmu, cmd, 1, sync);
}

static int arm_smmu_cmdq_issue_cmd(struct arm_smmu_device *smmu,
				   struct arm_smmu_cmdq_ent *ent)
{
	return __arm_smmu_cmdq_issue_cmd(smmu, ent, false);
}

static int arm_smmu_cmdq_issue_cmd_with_sync(struct arm_smmu_device *smmu,
					     struct arm_smmu_cmdq_ent *ent)
{
	return __arm_smmu_cmdq_issue_cmd(smmu, ent, true);
}

static void arm_smmu_cmdq_batch_add(struct arm_smmu_device *smmu,
				    struct arm_smmu_cmdq_batch *cmds,
				    struct arm_smmu_cmdq_ent *cmd)
{
	int index;

	if (cmds->num == CMDQ_BATCH_ENTRIES - 1 &&
	    (smmu->options & ARM_SMMU_OPT_CMDQ_FORCE_SYNC)) {
		arm_smmu_cmdq_issue_cmdlist(smmu, cmds->cmds, cmds->num, true);
		cmds->num = 0;
	}

	if (cmds->num == CMDQ_BATCH_ENTRIES) {
		arm_smmu_cmdq_issue_cmdlist(smmu, cmds->cmds, cmds->num, false);
		cmds->num = 0;
	}

	index = cmds->num * CMDQ_ENT_DWORDS;
	if (unlikely(arm_smmu_cmdq_build_cmd(&cmds->cmds[index], cmd))) {
		dev_warn(smmu->dev, "ignoring unknown CMDQ opcode 0x%x\n",
			 cmd->opcode);
		return;
	}

	cmds->num++;
}

static int arm_smmu_cmdq_batch_submit(struct arm_smmu_device *smmu,
				      struct arm_smmu_cmdq_batch *cmds)
{
	return arm_smmu_cmdq_issue_cmdlist(smmu, cmds->cmds, cmds->num, true);
}

static void arm_smmu_page_response(struct device *dev, struct iopf_fault *unused,
				   struct iommu_page_response *resp)
{
	struct arm_smmu_cmdq_ent cmd = {0};
	struct arm_smmu_master *master = dev_iommu_priv_get(dev);
	int sid = master->streams[0].id;

	if (WARN_ON(!master->stall_enabled))
		return;

	cmd.opcode		= CMDQ_OP_RESUME;
	cmd.resume.sid		= sid;
	cmd.resume.stag		= resp->grpid;
	switch (resp->code) {
	case IOMMU_PAGE_RESP_INVALID:
	case IOMMU_PAGE_RESP_FAILURE:
		cmd.resume.resp = CMDQ_RESUME_0_RESP_ABORT;
		break;
	case IOMMU_PAGE_RESP_SUCCESS:
		cmd.resume.resp = CMDQ_RESUME_0_RESP_RETRY;
		break;
	default:
		break;
	}

	arm_smmu_cmdq_issue_cmd(master->smmu, &cmd);
	/*
	 * Don't send a SYNC, it doesn't do anything for RESUME or PRI_RESP.
	 * RESUME consumption guarantees that the stalled transaction will be
	 * terminated... at some point in the future. PRI_RESP is fire and
	 * forget.
	 */
}

/* Context descriptor manipulation functions */
void arm_smmu_tlb_inv_asid(struct arm_smmu_device *smmu, u16 asid)
{
	struct arm_smmu_cmdq_ent cmd = {
		.opcode	= smmu->features & ARM_SMMU_FEAT_E2H ?
			CMDQ_OP_TLBI_EL2_ASID : CMDQ_OP_TLBI_NH_ASID,
		.tlbi.asid = asid,
	};

	arm_smmu_cmdq_issue_cmd_with_sync(smmu, &cmd);
}

/*
 * Based on the value of ent report which bits of the STE the HW will access. It
 * would be nice if this was complete according to the spec, but minimally it
 * has to capture the bits this driver uses.
 */
VISIBLE_IF_KUNIT
void arm_smmu_get_ste_used(const __le64 *ent, __le64 *used_bits)
{
	unsigned int cfg = FIELD_GET(STRTAB_STE_0_CFG, le64_to_cpu(ent[0]));

	used_bits[0] = cpu_to_le64(STRTAB_STE_0_V);
	if (!(ent[0] & cpu_to_le64(STRTAB_STE_0_V)))
		return;

	used_bits[0] |= cpu_to_le64(STRTAB_STE_0_CFG);

	/* S1 translates */
	if (cfg & BIT(0)) {
		used_bits[0] |= cpu_to_le64(STRTAB_STE_0_S1FMT |
					    STRTAB_STE_0_S1CTXPTR_MASK |
					    STRTAB_STE_0_S1CDMAX);
		used_bits[1] |=
			cpu_to_le64(STRTAB_STE_1_S1DSS | STRTAB_STE_1_S1CIR |
				    STRTAB_STE_1_S1COR | STRTAB_STE_1_S1CSH |
				    STRTAB_STE_1_S1STALLD | STRTAB_STE_1_STRW |
				    STRTAB_STE_1_EATS);
		used_bits[2] |= cpu_to_le64(STRTAB_STE_2_S2VMID);

		/*
		 * See 13.5 Summary of attribute/permission configuration fields
		 * for the SHCFG behavior.
		 */
		if (FIELD_GET(STRTAB_STE_1_S1DSS, le64_to_cpu(ent[1])) ==
		    STRTAB_STE_1_S1DSS_BYPASS)
			used_bits[1] |= cpu_to_le64(STRTAB_STE_1_SHCFG);
	}

	/* S2 translates */
	if (cfg & BIT(1)) {
		used_bits[1] |=
			cpu_to_le64(STRTAB_STE_1_EATS | STRTAB_STE_1_SHCFG);
		used_bits[2] |=
			cpu_to_le64(STRTAB_STE_2_S2VMID | STRTAB_STE_2_VTCR |
				    STRTAB_STE_2_S2AA64 | STRTAB_STE_2_S2ENDI |
				    STRTAB_STE_2_S2PTW | STRTAB_STE_2_S2R);
		used_bits[3] |= cpu_to_le64(STRTAB_STE_3_S2TTB_MASK);
	}

	if (cfg == STRTAB_STE_0_CFG_BYPASS)
		used_bits[1] |= cpu_to_le64(STRTAB_STE_1_SHCFG);
}
EXPORT_SYMBOL_IF_KUNIT(arm_smmu_get_ste_used);

/*
 * Figure out if we can do a hitless update of entry to become target. Returns a
 * bit mask where 1 indicates that qword needs to be set disruptively.
 * unused_update is an intermediate value of entry that has unused bits set to
 * their new values.
 */
static u8 arm_smmu_entry_qword_diff(struct arm_smmu_entry_writer *writer,
				    const __le64 *entry, const __le64 *target,
				    __le64 *unused_update)
{
	__le64 target_used[NUM_ENTRY_QWORDS] = {};
	__le64 cur_used[NUM_ENTRY_QWORDS] = {};
	u8 used_qword_diff = 0;
	unsigned int i;

	writer->ops->get_used(entry, cur_used);
	writer->ops->get_used(target, target_used);

	for (i = 0; i != NUM_ENTRY_QWORDS; i++) {
		/*
		 * Check that masks are up to date, the make functions are not
		 * allowed to set a bit to 1 if the used function doesn't say it
		 * is used.
		 */
		WARN_ON_ONCE(target[i] & ~target_used[i]);

		/* Bits can change because they are not currently being used */
		unused_update[i] = (entry[i] & cur_used[i]) |
				   (target[i] & ~cur_used[i]);
		/*
		 * Each bit indicates that a used bit in a qword needs to be
		 * changed after unused_update is applied.
		 */
		if ((unused_update[i] & target_used[i]) != target[i])
			used_qword_diff |= 1 << i;
	}
	return used_qword_diff;
}

static bool entry_set(struct arm_smmu_entry_writer *writer, __le64 *entry,
		      const __le64 *target, unsigned int start,
		      unsigned int len)
{
	bool changed = false;
	unsigned int i;

	for (i = start; len != 0; len--, i++) {
		if (entry[i] != target[i]) {
			WRITE_ONCE(entry[i], target[i]);
			changed = true;
		}
	}

	if (changed)
		writer->ops->sync(writer);
	return changed;
}

/*
 * Update the STE/CD to the target configuration. The transition from the
 * current entry to the target entry takes place over multiple steps that
 * attempts to make the transition hitless if possible. This function takes care
 * not to create a situation where the HW can perceive a corrupted entry. HW is
 * only required to have a 64 bit atomicity with stores from the CPU, while
 * entries are many 64 bit values big.
 *
 * The difference between the current value and the target value is analyzed to
 * determine which of three updates are required - disruptive, hitless or no
 * change.
 *
 * In the most general disruptive case we can make any update in three steps:
 *  - Disrupting the entry (V=0)
 *  - Fill now unused qwords, execpt qword 0 which contains V
 *  - Make qword 0 have the final value and valid (V=1) with a single 64
 *    bit store
 *
 * However this disrupts the HW while it is happening. There are several
 * interesting cases where a STE/CD can be updated without disturbing the HW
 * because only a small number of bits are changing (S1DSS, CONFIG, etc) or
 * because the used bits don't intersect. We can detect this by calculating how
 * many 64 bit values need update after adjusting the unused bits and skip the
 * V=0 process. This relies on the IGNORED behavior described in the
 * specification.
 */
VISIBLE_IF_KUNIT
void arm_smmu_write_entry(struct arm_smmu_entry_writer *writer, __le64 *entry,
			  const __le64 *target)
{
	__le64 unused_update[NUM_ENTRY_QWORDS];
	u8 used_qword_diff;

	used_qword_diff =
		arm_smmu_entry_qword_diff(writer, entry, target, unused_update);
	if (hweight8(used_qword_diff) == 1) {
		/*
		 * Only one qword needs its used bits to be changed. This is a
		 * hitless update, update all bits the current STE/CD is
		 * ignoring to their new values, then update a single "critical
		 * qword" to change the STE/CD and finally 0 out any bits that
		 * are now unused in the target configuration.
		 */
		unsigned int critical_qword_index = ffs(used_qword_diff) - 1;

		/*
		 * Skip writing unused bits in the critical qword since we'll be
		 * writing it in the next step anyways. This can save a sync
		 * when the only change is in that qword.
		 */
		unused_update[critical_qword_index] =
			entry[critical_qword_index];
		entry_set(writer, entry, unused_update, 0, NUM_ENTRY_QWORDS);
		entry_set(writer, entry, target, critical_qword_index, 1);
		entry_set(writer, entry, target, 0, NUM_ENTRY_QWORDS);
	} else if (used_qword_diff) {
		/*
		 * At least two qwords need their inuse bits to be changed. This
		 * requires a breaking update, zero the V bit, write all qwords
		 * but 0, then set qword 0
		 */
		unused_update[0] = 0;
		entry_set(writer, entry, unused_update, 0, 1);
		entry_set(writer, entry, target, 1, NUM_ENTRY_QWORDS - 1);
		entry_set(writer, entry, target, 0, 1);
	} else {
		/*
		 * No inuse bit changed. Sanity check that all unused bits are 0
		 * in the entry. The target was already sanity checked by
		 * compute_qword_diff().
		 */
		WARN_ON_ONCE(
			entry_set(writer, entry, target, 0, NUM_ENTRY_QWORDS));
	}
}
EXPORT_SYMBOL_IF_KUNIT(arm_smmu_write_entry);

static void arm_smmu_sync_cd(struct arm_smmu_master *master,
			     int ssid, bool leaf)
{
	size_t i;
	struct arm_smmu_cmdq_batch cmds;
	struct arm_smmu_device *smmu = master->smmu;
	struct arm_smmu_cmdq_ent cmd = {
		.opcode	= CMDQ_OP_CFGI_CD,
		.cfgi	= {
			.ssid	= ssid,
			.leaf	= leaf,
		},
	};

	cmds.num = 0;
	for (i = 0; i < master->num_streams; i++) {
		cmd.cfgi.sid = master->streams[i].id;
		arm_smmu_cmdq_batch_add(smmu, &cmds, &cmd);
	}

	arm_smmu_cmdq_batch_submit(smmu, &cmds);
}

static int arm_smmu_alloc_cd_leaf_table(struct arm_smmu_device *smmu,
					struct arm_smmu_l1_ctx_desc *l1_desc)
{
	size_t size = CTXDESC_L2_ENTRIES * (CTXDESC_CD_DWORDS << 3);

	l1_desc->l2ptr = dmam_alloc_coherent(smmu->dev, size,
					     &l1_desc->l2ptr_dma, GFP_KERNEL);
	if (!l1_desc->l2ptr) {
		dev_warn(smmu->dev,
			 "failed to allocate context descriptor table\n");
		return -ENOMEM;
	}
	return 0;
}

static void arm_smmu_write_cd_l1_desc(__le64 *dst,
				      struct arm_smmu_l1_ctx_desc *l1_desc)
{
	u64 val = (l1_desc->l2ptr_dma & CTXDESC_L1_DESC_L2PTR_MASK) |
		  CTXDESC_L1_DESC_V;

	/* The HW has 64 bit atomicity with stores to the L2 CD table */
	WRITE_ONCE(*dst, cpu_to_le64(val));
}

struct arm_smmu_cd *arm_smmu_get_cd_ptr(struct arm_smmu_master *master,
					u32 ssid)
{
	struct arm_smmu_l1_ctx_desc *l1_desc;
	struct arm_smmu_ctx_desc_cfg *cd_table = &master->cd_table;

	if (!cd_table->cdtab)
		return NULL;

	if (cd_table->s1fmt == STRTAB_STE_0_S1FMT_LINEAR)
		return (struct arm_smmu_cd *)(cd_table->cdtab +
					      ssid * CTXDESC_CD_DWORDS);

	l1_desc = &cd_table->l1_desc[ssid / CTXDESC_L2_ENTRIES];
	if (!l1_desc->l2ptr)
		return NULL;
	return &l1_desc->l2ptr[ssid % CTXDESC_L2_ENTRIES];
}

static struct arm_smmu_cd *arm_smmu_alloc_cd_ptr(struct arm_smmu_master *master,
						 u32 ssid)
{
	struct arm_smmu_ctx_desc_cfg *cd_table = &master->cd_table;
	struct arm_smmu_device *smmu = master->smmu;

	might_sleep();
	iommu_group_mutex_assert(master->dev);

	if (!cd_table->cdtab) {
		if (arm_smmu_alloc_cd_tables(master))
			return NULL;
	}

	if (cd_table->s1fmt == STRTAB_STE_0_S1FMT_64K_L2) {
		unsigned int idx = ssid / CTXDESC_L2_ENTRIES;
		struct arm_smmu_l1_ctx_desc *l1_desc;

		l1_desc = &cd_table->l1_desc[idx];
		if (!l1_desc->l2ptr) {
			__le64 *l1ptr;

			if (arm_smmu_alloc_cd_leaf_table(smmu, l1_desc))
				return NULL;

			l1ptr = cd_table->cdtab + idx * CTXDESC_L1_DESC_DWORDS;
			arm_smmu_write_cd_l1_desc(l1ptr, l1_desc);
			/* An invalid L1CD can be cached */
			arm_smmu_sync_cd(master, ssid, false);
		}
	}
	return arm_smmu_get_cd_ptr(master, ssid);
}

struct arm_smmu_cd_writer {
	struct arm_smmu_entry_writer writer;
	unsigned int ssid;
};

VISIBLE_IF_KUNIT
void arm_smmu_get_cd_used(const __le64 *ent, __le64 *used_bits)
{
	used_bits[0] = cpu_to_le64(CTXDESC_CD_0_V);
	if (!(ent[0] & cpu_to_le64(CTXDESC_CD_0_V)))
		return;
	memset(used_bits, 0xFF, sizeof(struct arm_smmu_cd));

	/*
	 * If EPD0 is set by the make function it means
	 * T0SZ/TG0/IR0/OR0/SH0/TTB0 are IGNORED
	 */
	if (ent[0] & cpu_to_le64(CTXDESC_CD_0_TCR_EPD0)) {
		used_bits[0] &= ~cpu_to_le64(
			CTXDESC_CD_0_TCR_T0SZ | CTXDESC_CD_0_TCR_TG0 |
			CTXDESC_CD_0_TCR_IRGN0 | CTXDESC_CD_0_TCR_ORGN0 |
			CTXDESC_CD_0_TCR_SH0);
		used_bits[1] &= ~cpu_to_le64(CTXDESC_CD_1_TTB0_MASK);
	}
}
EXPORT_SYMBOL_IF_KUNIT(arm_smmu_get_cd_used);

static void arm_smmu_cd_writer_sync_entry(struct arm_smmu_entry_writer *writer)
{
	struct arm_smmu_cd_writer *cd_writer =
		container_of(writer, struct arm_smmu_cd_writer, writer);

	arm_smmu_sync_cd(writer->master, cd_writer->ssid, true);
}

static const struct arm_smmu_entry_writer_ops arm_smmu_cd_writer_ops = {
	.sync = arm_smmu_cd_writer_sync_entry,
	.get_used = arm_smmu_get_cd_used,
};

void arm_smmu_write_cd_entry(struct arm_smmu_master *master, int ssid,
			     struct arm_smmu_cd *cdptr,
			     const struct arm_smmu_cd *target)
{
	bool target_valid = target->data[0] & cpu_to_le64(CTXDESC_CD_0_V);
	bool cur_valid = cdptr->data[0] & cpu_to_le64(CTXDESC_CD_0_V);
	struct arm_smmu_cd_writer cd_writer = {
		.writer = {
			.ops = &arm_smmu_cd_writer_ops,
			.master = master,
		},
		.ssid = ssid,
	};

	if (ssid != IOMMU_NO_PASID && cur_valid != target_valid) {
		if (cur_valid)
			master->cd_table.used_ssids--;
		else
			master->cd_table.used_ssids++;
	}

	arm_smmu_write_entry(&cd_writer.writer, cdptr->data, target->data);
}

void arm_smmu_make_s1_cd(struct arm_smmu_cd *target,
			 struct arm_smmu_master *master,
			 struct arm_smmu_domain *smmu_domain)
{
	struct arm_smmu_ctx_desc *cd = &smmu_domain->cd;
	const struct io_pgtable_cfg *pgtbl_cfg =
		&io_pgtable_ops_to_pgtable(smmu_domain->pgtbl_ops)->cfg;
	typeof(&pgtbl_cfg->arm_lpae_s1_cfg.tcr) tcr =
		&pgtbl_cfg->arm_lpae_s1_cfg.tcr;

	memset(target, 0, sizeof(*target));

	target->data[0] = cpu_to_le64(
		FIELD_PREP(CTXDESC_CD_0_TCR_T0SZ, tcr->tsz) |
		FIELD_PREP(CTXDESC_CD_0_TCR_TG0, tcr->tg) |
		FIELD_PREP(CTXDESC_CD_0_TCR_IRGN0, tcr->irgn) |
		FIELD_PREP(CTXDESC_CD_0_TCR_ORGN0, tcr->orgn) |
		FIELD_PREP(CTXDESC_CD_0_TCR_SH0, tcr->sh) |
#ifdef __BIG_ENDIAN
		CTXDESC_CD_0_ENDI |
#endif
		CTXDESC_CD_0_TCR_EPD1 |
		CTXDESC_CD_0_V |
		FIELD_PREP(CTXDESC_CD_0_TCR_IPS, tcr->ips) |
		CTXDESC_CD_0_AA64 |
		(master->stall_enabled ? CTXDESC_CD_0_S : 0) |
		CTXDESC_CD_0_R |
		CTXDESC_CD_0_A |
		CTXDESC_CD_0_ASET |
		FIELD_PREP(CTXDESC_CD_0_ASID, cd->asid)
		);

	/* To enable dirty flag update, set both Access flag and dirty state update */
	if (pgtbl_cfg->quirks & IO_PGTABLE_QUIRK_ARM_HD)
		target->data[0] |= cpu_to_le64(CTXDESC_CD_0_TCR_HA |
					       CTXDESC_CD_0_TCR_HD);

	target->data[1] = cpu_to_le64(pgtbl_cfg->arm_lpae_s1_cfg.ttbr &
				      CTXDESC_CD_1_TTB0_MASK);
	target->data[3] = cpu_to_le64(pgtbl_cfg->arm_lpae_s1_cfg.mair);
}
EXPORT_SYMBOL_IF_KUNIT(arm_smmu_make_s1_cd);

void arm_smmu_clear_cd(struct arm_smmu_master *master, ioasid_t ssid)
{
	struct arm_smmu_cd target = {};
	struct arm_smmu_cd *cdptr;

	if (!master->cd_table.cdtab)
		return;
	cdptr = arm_smmu_get_cd_ptr(master, ssid);
	if (WARN_ON(!cdptr))
		return;
	arm_smmu_write_cd_entry(master, ssid, cdptr, &target);
}

static int arm_smmu_alloc_cd_tables(struct arm_smmu_master *master)
{
	int ret;
	size_t l1size;
	size_t max_contexts;
	struct arm_smmu_device *smmu = master->smmu;
	struct arm_smmu_ctx_desc_cfg *cd_table = &master->cd_table;

	cd_table->s1cdmax = master->ssid_bits;
	max_contexts = 1 << cd_table->s1cdmax;

	if (!(smmu->features & ARM_SMMU_FEAT_2_LVL_CDTAB) ||
	    max_contexts <= CTXDESC_L2_ENTRIES) {
		cd_table->s1fmt = STRTAB_STE_0_S1FMT_LINEAR;
		cd_table->num_l1_ents = max_contexts;

		l1size = max_contexts * (CTXDESC_CD_DWORDS << 3);
	} else {
		cd_table->s1fmt = STRTAB_STE_0_S1FMT_64K_L2;
		cd_table->num_l1_ents = DIV_ROUND_UP(max_contexts,
						  CTXDESC_L2_ENTRIES);

		cd_table->l1_desc = devm_kcalloc(smmu->dev, cd_table->num_l1_ents,
					      sizeof(*cd_table->l1_desc),
					      GFP_KERNEL);
		if (!cd_table->l1_desc)
			return -ENOMEM;

		l1size = cd_table->num_l1_ents * (CTXDESC_L1_DESC_DWORDS << 3);
	}

	cd_table->cdtab = dmam_alloc_coherent(smmu->dev, l1size, &cd_table->cdtab_dma,
					   GFP_KERNEL);
	if (!cd_table->cdtab) {
		dev_warn(smmu->dev, "failed to allocate context descriptor\n");
		ret = -ENOMEM;
		goto err_free_l1;
	}

	return 0;

err_free_l1:
	if (cd_table->l1_desc) {
		devm_kfree(smmu->dev, cd_table->l1_desc);
		cd_table->l1_desc = NULL;
	}
	return ret;
}

static void arm_smmu_free_cd_tables(struct arm_smmu_master *master)
{
	int i;
	size_t size, l1size;
	struct arm_smmu_device *smmu = master->smmu;
	struct arm_smmu_ctx_desc_cfg *cd_table = &master->cd_table;

	if (cd_table->l1_desc) {
		size = CTXDESC_L2_ENTRIES * (CTXDESC_CD_DWORDS << 3);

		for (i = 0; i < cd_table->num_l1_ents; i++) {
			if (!cd_table->l1_desc[i].l2ptr)
				continue;

			dmam_free_coherent(smmu->dev, size,
					   cd_table->l1_desc[i].l2ptr,
					   cd_table->l1_desc[i].l2ptr_dma);
		}
		devm_kfree(smmu->dev, cd_table->l1_desc);
		cd_table->l1_desc = NULL;

		l1size = cd_table->num_l1_ents * (CTXDESC_L1_DESC_DWORDS << 3);
	} else {
		l1size = cd_table->num_l1_ents * (CTXDESC_CD_DWORDS << 3);
	}

	dmam_free_coherent(smmu->dev, l1size, cd_table->cdtab, cd_table->cdtab_dma);
	cd_table->cdtab_dma = 0;
	cd_table->cdtab = NULL;
}

/* Stream table manipulation functions */
static void arm_smmu_write_strtab_l1_desc(__le64 *dst, dma_addr_t l2ptr_dma)
{
	u64 val = 0;

	val |= FIELD_PREP(STRTAB_L1_DESC_SPAN, STRTAB_SPLIT + 1);
	val |= l2ptr_dma & STRTAB_L1_DESC_L2PTR_MASK;

	/* The HW has 64 bit atomicity with stores to the L2 STE table */
	WRITE_ONCE(*dst, cpu_to_le64(val));
}

struct arm_smmu_ste_writer {
	struct arm_smmu_entry_writer writer;
	u32 sid;
};

static void arm_smmu_ste_writer_sync_entry(struct arm_smmu_entry_writer *writer)
{
	struct arm_smmu_ste_writer *ste_writer =
		container_of(writer, struct arm_smmu_ste_writer, writer);
	struct arm_smmu_cmdq_ent cmd = {
		.opcode	= CMDQ_OP_CFGI_STE,
		.cfgi	= {
			.sid	= ste_writer->sid,
			.leaf	= true,
		},
	};

	arm_smmu_cmdq_issue_cmd_with_sync(writer->master->smmu, &cmd);
}

static const struct arm_smmu_entry_writer_ops arm_smmu_ste_writer_ops = {
	.sync = arm_smmu_ste_writer_sync_entry,
	.get_used = arm_smmu_get_ste_used,
};

static void arm_smmu_write_ste(struct arm_smmu_master *master, u32 sid,
			       struct arm_smmu_ste *ste,
			       const struct arm_smmu_ste *target)
{
	struct arm_smmu_device *smmu = master->smmu;
	struct arm_smmu_ste_writer ste_writer = {
		.writer = {
			.ops = &arm_smmu_ste_writer_ops,
			.master = master,
		},
		.sid = sid,
	};

	arm_smmu_write_entry(&ste_writer.writer, ste->data, target->data);

	/* It's likely that we'll want to use the new STE soon */
	if (!(smmu->options & ARM_SMMU_OPT_SKIP_PREFETCH)) {
		struct arm_smmu_cmdq_ent
			prefetch_cmd = { .opcode = CMDQ_OP_PREFETCH_CFG,
					 .prefetch = {
						 .sid = sid,
					 } };

		arm_smmu_cmdq_issue_cmd(smmu, &prefetch_cmd);
	}
}

VISIBLE_IF_KUNIT
void arm_smmu_make_abort_ste(struct arm_smmu_ste *target)
{
	memset(target, 0, sizeof(*target));
	target->data[0] = cpu_to_le64(
		STRTAB_STE_0_V |
		FIELD_PREP(STRTAB_STE_0_CFG, STRTAB_STE_0_CFG_ABORT));
}
EXPORT_SYMBOL_IF_KUNIT(arm_smmu_make_abort_ste);

VISIBLE_IF_KUNIT
void arm_smmu_make_bypass_ste(struct arm_smmu_device *smmu,
			      struct arm_smmu_ste *target)
{
	memset(target, 0, sizeof(*target));
	target->data[0] = cpu_to_le64(
		STRTAB_STE_0_V |
		FIELD_PREP(STRTAB_STE_0_CFG, STRTAB_STE_0_CFG_BYPASS));

	if (smmu->features & ARM_SMMU_FEAT_ATTR_TYPES_OVR)
		target->data[1] = cpu_to_le64(FIELD_PREP(STRTAB_STE_1_SHCFG,
							 STRTAB_STE_1_SHCFG_INCOMING));
}
EXPORT_SYMBOL_IF_KUNIT(arm_smmu_make_bypass_ste);

VISIBLE_IF_KUNIT
void arm_smmu_make_cdtable_ste(struct arm_smmu_ste *target,
			       struct arm_smmu_master *master, bool ats_enabled,
			       unsigned int s1dss)
{
	struct arm_smmu_ctx_desc_cfg *cd_table = &master->cd_table;
	struct arm_smmu_device *smmu = master->smmu;

	memset(target, 0, sizeof(*target));
	target->data[0] = cpu_to_le64(
		STRTAB_STE_0_V |
		FIELD_PREP(STRTAB_STE_0_CFG, STRTAB_STE_0_CFG_S1_TRANS) |
		FIELD_PREP(STRTAB_STE_0_S1FMT, cd_table->s1fmt) |
		(cd_table->cdtab_dma & STRTAB_STE_0_S1CTXPTR_MASK) |
		FIELD_PREP(STRTAB_STE_0_S1CDMAX, cd_table->s1cdmax));

	target->data[1] = cpu_to_le64(
		FIELD_PREP(STRTAB_STE_1_S1DSS, s1dss) |
		FIELD_PREP(STRTAB_STE_1_S1CIR, STRTAB_STE_1_S1C_CACHE_WBRA) |
		FIELD_PREP(STRTAB_STE_1_S1COR, STRTAB_STE_1_S1C_CACHE_WBRA) |
		FIELD_PREP(STRTAB_STE_1_S1CSH, ARM_SMMU_SH_ISH) |
		((smmu->features & ARM_SMMU_FEAT_STALLS &&
		  !master->stall_enabled) ?
			 STRTAB_STE_1_S1STALLD :
			 0) |
		FIELD_PREP(STRTAB_STE_1_EATS,
			   ats_enabled ? STRTAB_STE_1_EATS_TRANS : 0));

	if ((smmu->features & ARM_SMMU_FEAT_ATTR_TYPES_OVR) &&
	    s1dss == STRTAB_STE_1_S1DSS_BYPASS)
		target->data[1] |= cpu_to_le64(FIELD_PREP(
			STRTAB_STE_1_SHCFG, STRTAB_STE_1_SHCFG_INCOMING));

	if (smmu->features & ARM_SMMU_FEAT_E2H) {
		/*
		 * To support BTM the streamworld needs to match the
		 * configuration of the CPU so that the ASID broadcasts are
		 * properly matched. This means either S/NS-EL2-E2H (hypervisor)
		 * or NS-EL1 (guest). Since an SVA domain can be installed in a
		 * PASID this should always use a BTM compatible configuration
		 * if the HW supports it.
		 */
		target->data[1] |= cpu_to_le64(
			FIELD_PREP(STRTAB_STE_1_STRW, STRTAB_STE_1_STRW_EL2));
	} else {
		target->data[1] |= cpu_to_le64(
			FIELD_PREP(STRTAB_STE_1_STRW, STRTAB_STE_1_STRW_NSEL1));

		/*
		 * VMID 0 is reserved for stage-2 bypass EL1 STEs, see
		 * arm_smmu_domain_alloc_id()
		 */
		target->data[2] =
			cpu_to_le64(FIELD_PREP(STRTAB_STE_2_S2VMID, 0));
	}
}
EXPORT_SYMBOL_IF_KUNIT(arm_smmu_make_cdtable_ste);

VISIBLE_IF_KUNIT
void arm_smmu_make_s2_domain_ste(struct arm_smmu_ste *target,
				 struct arm_smmu_master *master,
				 struct arm_smmu_domain *smmu_domain,
				 bool ats_enabled)
{
	struct arm_smmu_s2_cfg *s2_cfg = &smmu_domain->s2_cfg;
	const struct io_pgtable_cfg *pgtbl_cfg =
		&io_pgtable_ops_to_pgtable(smmu_domain->pgtbl_ops)->cfg;
	typeof(&pgtbl_cfg->arm_lpae_s2_cfg.vtcr) vtcr =
		&pgtbl_cfg->arm_lpae_s2_cfg.vtcr;
	u64 vtcr_val;
	struct arm_smmu_device *smmu = master->smmu;

	memset(target, 0, sizeof(*target));
	target->data[0] = cpu_to_le64(
		STRTAB_STE_0_V |
		FIELD_PREP(STRTAB_STE_0_CFG, STRTAB_STE_0_CFG_S2_TRANS));

	target->data[1] = cpu_to_le64(
		FIELD_PREP(STRTAB_STE_1_EATS,
			   ats_enabled ? STRTAB_STE_1_EATS_TRANS : 0));

	if (smmu->features & ARM_SMMU_FEAT_ATTR_TYPES_OVR)
		target->data[1] |= cpu_to_le64(FIELD_PREP(STRTAB_STE_1_SHCFG,
							  STRTAB_STE_1_SHCFG_INCOMING));

	vtcr_val = FIELD_PREP(STRTAB_STE_2_VTCR_S2T0SZ, vtcr->tsz) |
		   FIELD_PREP(STRTAB_STE_2_VTCR_S2SL0, vtcr->sl) |
		   FIELD_PREP(STRTAB_STE_2_VTCR_S2IR0, vtcr->irgn) |
		   FIELD_PREP(STRTAB_STE_2_VTCR_S2OR0, vtcr->orgn) |
		   FIELD_PREP(STRTAB_STE_2_VTCR_S2SH0, vtcr->sh) |
		   FIELD_PREP(STRTAB_STE_2_VTCR_S2TG, vtcr->tg) |
		   FIELD_PREP(STRTAB_STE_2_VTCR_S2PS, vtcr->ps);
	target->data[2] = cpu_to_le64(
		FIELD_PREP(STRTAB_STE_2_S2VMID, s2_cfg->vmid) |
		FIELD_PREP(STRTAB_STE_2_VTCR, vtcr_val) |
		STRTAB_STE_2_S2AA64 |
#ifdef __BIG_ENDIAN
		STRTAB_STE_2_S2ENDI |
#endif
		STRTAB_STE_2_S2PTW |
		STRTAB_STE_2_S2R);

	target->data[3] = cpu_to_le64(pgtbl_cfg->arm_lpae_s2_cfg.vttbr &
				      STRTAB_STE_3_S2TTB_MASK);
}
EXPORT_SYMBOL_IF_KUNIT(arm_smmu_make_s2_domain_ste);

/*
 * This can safely directly manipulate the STE memory without a sync sequence
 * because the STE table has not been installed in the SMMU yet.
 */
static void arm_smmu_init_initial_stes(struct arm_smmu_ste *strtab,
				       unsigned int nent)
{
	unsigned int i;

	for (i = 0; i < nent; ++i) {
		arm_smmu_make_abort_ste(strtab);
		strtab++;
	}
}

static int arm_smmu_init_l2_strtab(struct arm_smmu_device *smmu, u32 sid)
{
	size_t size;
	void *strtab;
	dma_addr_t l2ptr_dma;
	struct arm_smmu_strtab_cfg *cfg = &smmu->strtab_cfg;
	struct arm_smmu_strtab_l1_desc *desc = &cfg->l1_desc[sid >> STRTAB_SPLIT];

	if (desc->l2ptr)
		return 0;

	size = 1 << (STRTAB_SPLIT + ilog2(STRTAB_STE_DWORDS) + 3);
	strtab = &cfg->strtab[(sid >> STRTAB_SPLIT) * STRTAB_L1_DESC_DWORDS];

	desc->l2ptr = dmam_alloc_coherent(smmu->dev, size, &l2ptr_dma,
					  GFP_KERNEL);
	if (!desc->l2ptr) {
		dev_err(smmu->dev,
			"failed to allocate l2 stream table for SID %u\n",
			sid);
		return -ENOMEM;
	}

	arm_smmu_init_initial_stes(desc->l2ptr, 1 << STRTAB_SPLIT);
	arm_smmu_write_strtab_l1_desc(strtab, l2ptr_dma);
	return 0;
}

static struct arm_smmu_master *
arm_smmu_find_master(struct arm_smmu_device *smmu, u32 sid)
{
	struct rb_node *node;
	struct arm_smmu_stream *stream;

	lockdep_assert_held(&smmu->streams_mutex);

	node = smmu->streams.rb_node;
	while (node) {
		stream = rb_entry(node, struct arm_smmu_stream, node);
		if (stream->id < sid)
			node = node->rb_right;
		else if (stream->id > sid)
			node = node->rb_left;
		else
			return stream->master;
	}

	return NULL;
}

/* IRQ and event handlers */
static int arm_smmu_handle_evt(struct arm_smmu_device *smmu, u64 *evt)
{
	int ret = 0;
	u32 perm = 0;
	struct arm_smmu_master *master;
	bool ssid_valid = evt[0] & EVTQ_0_SSV;
	u32 sid = FIELD_GET(EVTQ_0_SID, evt[0]);
	struct iopf_fault fault_evt = { };
	struct iommu_fault *flt = &fault_evt.fault;

	switch (FIELD_GET(EVTQ_0_ID, evt[0])) {
	case EVT_ID_TRANSLATION_FAULT:
	case EVT_ID_ADDR_SIZE_FAULT:
	case EVT_ID_ACCESS_FAULT:
	case EVT_ID_PERMISSION_FAULT:
		break;
	default:
		return -EOPNOTSUPP;
	}

	/* Stage-2 is always pinned at the moment */
	if (evt[1] & EVTQ_1_S2)
		return -EFAULT;

	if (!(evt[1] & EVTQ_1_STALL))
		return -EOPNOTSUPP;

	if (evt[1] & EVTQ_1_RnW)
		perm |= IOMMU_FAULT_PERM_READ;
	else
		perm |= IOMMU_FAULT_PERM_WRITE;

	if (evt[1] & EVTQ_1_InD)
		perm |= IOMMU_FAULT_PERM_EXEC;

	if (evt[1] & EVTQ_1_PnU)
		perm |= IOMMU_FAULT_PERM_PRIV;

	flt->type = IOMMU_FAULT_PAGE_REQ;
	flt->prm = (struct iommu_fault_page_request) {
		.flags = IOMMU_FAULT_PAGE_REQUEST_LAST_PAGE,
		.grpid = FIELD_GET(EVTQ_1_STAG, evt[1]),
		.perm = perm,
		.addr = FIELD_GET(EVTQ_2_ADDR, evt[2]),
	};

	if (ssid_valid) {
		flt->prm.flags |= IOMMU_FAULT_PAGE_REQUEST_PASID_VALID;
		flt->prm.pasid = FIELD_GET(EVTQ_0_SSID, evt[0]);
	}

	mutex_lock(&smmu->streams_mutex);
	master = arm_smmu_find_master(smmu, sid);
	if (!master) {
		ret = -EINVAL;
		goto out_unlock;
	}

	ret = iommu_report_device_fault(master->dev, &fault_evt);
out_unlock:
	mutex_unlock(&smmu->streams_mutex);
	return ret;
}

static irqreturn_t arm_smmu_evtq_thread(int irq, void *dev)
{
	int i, ret;
	struct arm_smmu_device *smmu = dev;
	struct arm_smmu_queue *q = &smmu->evtq.q;
	struct arm_smmu_ll_queue *llq = &q->llq;
	static DEFINE_RATELIMIT_STATE(rs, DEFAULT_RATELIMIT_INTERVAL,
				      DEFAULT_RATELIMIT_BURST);
	u64 evt[EVTQ_ENT_DWORDS];

	do {
		while (!queue_remove_raw(q, evt)) {
			u8 id = FIELD_GET(EVTQ_0_ID, evt[0]);

			ret = arm_smmu_handle_evt(smmu, evt);
			if (!ret || !__ratelimit(&rs))
				continue;

			dev_info(smmu->dev, "event 0x%02x received:\n", id);
			for (i = 0; i < ARRAY_SIZE(evt); ++i)
				dev_info(smmu->dev, "\t0x%016llx\n",
					 (unsigned long long)evt[i]);

			cond_resched();
		}

		/*
		 * Not much we can do on overflow, so scream and pretend we're
		 * trying harder.
		 */
		if (queue_sync_prod_in(q) == -EOVERFLOW)
			dev_err(smmu->dev, "EVTQ overflow detected -- events lost\n");
	} while (!queue_empty(llq));

	/* Sync our overflow flag, as we believe we're up to speed */
	queue_sync_cons_ovf(q);
	return IRQ_HANDLED;
}

static void arm_smmu_handle_ppr(struct arm_smmu_device *smmu, u64 *evt)
{
	u32 sid, ssid;
	u16 grpid;
	bool ssv, last;

	sid = FIELD_GET(PRIQ_0_SID, evt[0]);
	ssv = FIELD_GET(PRIQ_0_SSID_V, evt[0]);
	ssid = ssv ? FIELD_GET(PRIQ_0_SSID, evt[0]) : IOMMU_NO_PASID;
	last = FIELD_GET(PRIQ_0_PRG_LAST, evt[0]);
	grpid = FIELD_GET(PRIQ_1_PRG_IDX, evt[1]);

	dev_info(smmu->dev, "unexpected PRI request received:\n");
	dev_info(smmu->dev,
		 "\tsid 0x%08x.0x%05x: [%u%s] %sprivileged %s%s%s access at iova 0x%016llx\n",
		 sid, ssid, grpid, last ? "L" : "",
		 evt[0] & PRIQ_0_PERM_PRIV ? "" : "un",
		 evt[0] & PRIQ_0_PERM_READ ? "R" : "",
		 evt[0] & PRIQ_0_PERM_WRITE ? "W" : "",
		 evt[0] & PRIQ_0_PERM_EXEC ? "X" : "",
		 evt[1] & PRIQ_1_ADDR_MASK);

	if (last) {
		struct arm_smmu_cmdq_ent cmd = {
			.opcode			= CMDQ_OP_PRI_RESP,
			.substream_valid	= ssv,
			.pri			= {
				.sid	= sid,
				.ssid	= ssid,
				.grpid	= grpid,
				.resp	= PRI_RESP_DENY,
			},
		};

		arm_smmu_cmdq_issue_cmd(smmu, &cmd);
	}
}

static irqreturn_t arm_smmu_priq_thread(int irq, void *dev)
{
	struct arm_smmu_device *smmu = dev;
	struct arm_smmu_queue *q = &smmu->priq.q;
	struct arm_smmu_ll_queue *llq = &q->llq;
	u64 evt[PRIQ_ENT_DWORDS];

	do {
		while (!queue_remove_raw(q, evt))
			arm_smmu_handle_ppr(smmu, evt);

		if (queue_sync_prod_in(q) == -EOVERFLOW)
			dev_err(smmu->dev, "PRIQ overflow detected -- requests lost\n");
	} while (!queue_empty(llq));

	/* Sync our overflow flag, as we believe we're up to speed */
	queue_sync_cons_ovf(q);
	return IRQ_HANDLED;
}

static int arm_smmu_device_disable(struct arm_smmu_device *smmu);

static irqreturn_t arm_smmu_gerror_handler(int irq, void *dev)
{
	u32 gerror, gerrorn, active;
	struct arm_smmu_device *smmu = dev;

	gerror = readl_relaxed(smmu->base + ARM_SMMU_GERROR);
	gerrorn = readl_relaxed(smmu->base + ARM_SMMU_GERRORN);

	active = gerror ^ gerrorn;
	if (!(active & GERROR_ERR_MASK))
		return IRQ_NONE; /* No errors pending */

	dev_warn(smmu->dev,
		 "unexpected global error reported (0x%08x), this could be serious\n",
		 active);

	if (active & GERROR_SFM_ERR) {
		dev_err(smmu->dev, "device has entered Service Failure Mode!\n");
		arm_smmu_device_disable(smmu);
	}

	if (active & GERROR_MSI_GERROR_ABT_ERR)
		dev_warn(smmu->dev, "GERROR MSI write aborted\n");

	if (active & GERROR_MSI_PRIQ_ABT_ERR)
		dev_warn(smmu->dev, "PRIQ MSI write aborted\n");

	if (active & GERROR_MSI_EVTQ_ABT_ERR)
		dev_warn(smmu->dev, "EVTQ MSI write aborted\n");

	if (active & GERROR_MSI_CMDQ_ABT_ERR)
		dev_warn(smmu->dev, "CMDQ MSI write aborted\n");

	if (active & GERROR_PRIQ_ABT_ERR)
		dev_err(smmu->dev, "PRIQ write aborted -- events may have been lost\n");

	if (active & GERROR_EVTQ_ABT_ERR)
		dev_err(smmu->dev, "EVTQ write aborted -- events may have been lost\n");

	if (active & GERROR_CMDQ_ERR)
		arm_smmu_cmdq_skip_err(smmu);

	writel(gerror, smmu->base + ARM_SMMU_GERRORN);
	return IRQ_HANDLED;
}

static irqreturn_t arm_smmu_combined_irq_thread(int irq, void *dev)
{
	struct arm_smmu_device *smmu = dev;

	arm_smmu_evtq_thread(irq, dev);
	if (smmu->features & ARM_SMMU_FEAT_PRI)
		arm_smmu_priq_thread(irq, dev);

	return IRQ_HANDLED;
}

static irqreturn_t arm_smmu_combined_irq_handler(int irq, void *dev)
{
	arm_smmu_gerror_handler(irq, dev);
	return IRQ_WAKE_THREAD;
}

static void
arm_smmu_atc_inv_to_cmd(int ssid, unsigned long iova, size_t size,
			struct arm_smmu_cmdq_ent *cmd)
{
	size_t log2_span;
	size_t span_mask;
	/* ATC invalidates are always on 4096-bytes pages */
	size_t inval_grain_shift = 12;
	unsigned long page_start, page_end;

	/*
	 * ATS and PASID:
	 *
	 * If substream_valid is clear, the PCIe TLP is sent without a PASID
	 * prefix. In that case all ATC entries within the address range are
	 * invalidated, including those that were requested with a PASID! There
	 * is no way to invalidate only entries without PASID.
	 *
	 * When using STRTAB_STE_1_S1DSS_SSID0 (reserving CD 0 for non-PASID
	 * traffic), translation requests without PASID create ATC entries
	 * without PASID, which must be invalidated with substream_valid clear.
	 * This has the unpleasant side-effect of invalidating all PASID-tagged
	 * ATC entries within the address range.
	 */
	*cmd = (struct arm_smmu_cmdq_ent) {
		.opcode			= CMDQ_OP_ATC_INV,
		.substream_valid	= (ssid != IOMMU_NO_PASID),
		.atc.ssid		= ssid,
	};

	if (!size) {
		cmd->atc.size = ATC_INV_SIZE_ALL;
		return;
	}

	page_start	= iova >> inval_grain_shift;
	page_end	= (iova + size - 1) >> inval_grain_shift;

	/*
	 * In an ATS Invalidate Request, the address must be aligned on the
	 * range size, which must be a power of two number of page sizes. We
	 * thus have to choose between grossly over-invalidating the region, or
	 * splitting the invalidation into multiple commands. For simplicity
	 * we'll go with the first solution, but should refine it in the future
	 * if multiple commands are shown to be more efficient.
	 *
	 * Find the smallest power of two that covers the range. The most
	 * significant differing bit between the start and end addresses,
	 * fls(start ^ end), indicates the required span. For example:
	 *
	 * We want to invalidate pages [8; 11]. This is already the ideal range:
	 *		x = 0b1000 ^ 0b1011 = 0b11
	 *		span = 1 << fls(x) = 4
	 *
	 * To invalidate pages [7; 10], we need to invalidate [0; 15]:
	 *		x = 0b0111 ^ 0b1010 = 0b1101
	 *		span = 1 << fls(x) = 16
	 */
	log2_span	= fls_long(page_start ^ page_end);
	span_mask	= (1ULL << log2_span) - 1;

	page_start	&= ~span_mask;

	cmd->atc.addr	= page_start << inval_grain_shift;
	cmd->atc.size	= log2_span;
}

static int arm_smmu_atc_inv_master(struct arm_smmu_master *master,
				   ioasid_t ssid)
{
	int i;
	struct arm_smmu_cmdq_ent cmd;
	struct arm_smmu_cmdq_batch cmds;

	arm_smmu_atc_inv_to_cmd(ssid, 0, 0, &cmd);

	cmds.num = 0;
	for (i = 0; i < master->num_streams; i++) {
		cmd.atc.sid = master->streams[i].id;
		arm_smmu_cmdq_batch_add(master->smmu, &cmds, &cmd);
	}

	return arm_smmu_cmdq_batch_submit(master->smmu, &cmds);
}

int arm_smmu_atc_inv_domain(struct arm_smmu_domain *smmu_domain,
			    unsigned long iova, size_t size)
{
	struct arm_smmu_master_domain *master_domain;
	int i;
	unsigned long flags;
	struct arm_smmu_cmdq_ent cmd;
	struct arm_smmu_cmdq_batch cmds;

	if (!(smmu_domain->smmu->features & ARM_SMMU_FEAT_ATS))
		return 0;

	/*
	 * Ensure that we've completed prior invalidation of the main TLBs
	 * before we read 'nr_ats_masters' in case of a concurrent call to
	 * arm_smmu_enable_ats():
	 *
	 *	// unmap()			// arm_smmu_enable_ats()
	 *	TLBI+SYNC			atomic_inc(&nr_ats_masters);
	 *	smp_mb();			[...]
	 *	atomic_read(&nr_ats_masters);	pci_enable_ats() // writel()
	 *
	 * Ensures that we always see the incremented 'nr_ats_masters' count if
	 * ATS was enabled at the PCI device before completion of the TLBI.
	 */
	smp_mb();
	if (!atomic_read(&smmu_domain->nr_ats_masters))
		return 0;

	cmds.num = 0;

	spin_lock_irqsave(&smmu_domain->devices_lock, flags);
	list_for_each_entry(master_domain, &smmu_domain->devices,
			    devices_elm) {
		struct arm_smmu_master *master = master_domain->master;

		if (!master->ats_enabled)
			continue;

		arm_smmu_atc_inv_to_cmd(master_domain->ssid, iova, size, &cmd);

		for (i = 0; i < master->num_streams; i++) {
			cmd.atc.sid = master->streams[i].id;
			arm_smmu_cmdq_batch_add(smmu_domain->smmu, &cmds, &cmd);
		}
	}
	spin_unlock_irqrestore(&smmu_domain->devices_lock, flags);

	return arm_smmu_cmdq_batch_submit(smmu_domain->smmu, &cmds);
}

/* IO_PGTABLE API */
static void arm_smmu_tlb_inv_context(void *cookie)
{
	struct arm_smmu_domain *smmu_domain = cookie;
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	struct arm_smmu_cmdq_ent cmd;

	/*
	 * NOTE: when io-pgtable is in non-strict mode, we may get here with
	 * PTEs previously cleared by unmaps on the current CPU not yet visible
	 * to the SMMU. We are relying on the dma_wmb() implicit during cmd
	 * insertion to guarantee those are observed before the TLBI. Do be
	 * careful, 007.
	 */
	if (smmu_domain->stage == ARM_SMMU_DOMAIN_S1) {
		arm_smmu_tlb_inv_asid(smmu, smmu_domain->cd.asid);
	} else {
		cmd.opcode	= CMDQ_OP_TLBI_S12_VMALL;
		cmd.tlbi.vmid	= smmu_domain->s2_cfg.vmid;
		arm_smmu_cmdq_issue_cmd_with_sync(smmu, &cmd);
	}
	arm_smmu_atc_inv_domain(smmu_domain, 0, 0);
}

static void __arm_smmu_tlb_inv_range(struct arm_smmu_cmdq_ent *cmd,
				     unsigned long iova, size_t size,
				     size_t granule,
				     struct arm_smmu_domain *smmu_domain)
{
	struct arm_smmu_device *smmu = smmu_domain->smmu;
	unsigned long end = iova + size, num_pages = 0, tg = 0;
	size_t inv_range = granule;
	struct arm_smmu_cmdq_batch cmds;

	if (!size)
		return;

	if (smmu->features & ARM_SMMU_FEAT_RANGE_INV) {
		/* Get the leaf page size */
		tg = __ffs(smmu_domain->domain.pgsize_bitmap);

		num_pages = size >> tg;

		/* Convert page size of 12,14,16 (log2) to 1,2,3 */
		cmd->tlbi.tg = (tg - 10) / 2;

		/*
		 * Determine what level the granule is at. For non-leaf, both
		 * io-pgtable and SVA pass a nominal last-level granule because
		 * they don't know what level(s) actually apply, so ignore that
		 * and leave TTL=0. However for various errata reasons we still
		 * want to use a range command, so avoid the SVA corner case
		 * where both scale and num could be 0 as well.
		 */
		if (cmd->tlbi.leaf)
			cmd->tlbi.ttl = 4 - ((ilog2(granule) - 3) / (tg - 3));
		else if ((num_pages & CMDQ_TLBI_RANGE_NUM_MAX) == 1)
			num_pages++;
	}

	cmds.num = 0;

	while (iova < end) {
		if (smmu->features & ARM_SMMU_FEAT_RANGE_INV) {
			/*
			 * On each iteration of the loop, the range is 5 bits
			 * worth of the aligned size remaining.
			 * The range in pages is:
			 *
			 * range = (num_pages & (0x1f << __ffs(num_pages)))
			 */
			unsigned long scale, num;

			/* Determine the power of 2 multiple number of pages */
			scale = __ffs(num_pages);
			cmd->tlbi.scale = scale;

			/* Determine how many chunks of 2^scale size we have */
			num = (num_pages >> scale) & CMDQ_TLBI_RANGE_NUM_MAX;
			cmd->tlbi.num = num - 1;

			/* range is num * 2^scale * pgsize */
			inv_range = num << (scale + tg);

			/* Clear out the lower order bits for the next iteration */
			num_pages -= num << scale;
		}

		cmd->tlbi.addr = iova;
		arm_smmu_cmdq_batch_add(smmu, &cmds, cmd);
		iova += inv_range;
	}
	arm_smmu_cmdq_batch_submit(smmu, &cmds);
}

static void arm_smmu_tlb_inv_range_domain(unsigned long iova, size_t size,
					  size_t granule, bool leaf,
					  struct arm_smmu_domain *smmu_domain)
{
	struct arm_smmu_cmdq_ent cmd = {
		.tlbi = {
			.leaf	= leaf,
		},
	};

	if (smmu_domain->stage == ARM_SMMU_DOMAIN_S1) {
		cmd.opcode	= smmu_domain->smmu->features & ARM_SMMU_FEAT_E2H ?
				  CMDQ_OP_TLBI_EL2_VA : CMDQ_OP_TLBI_NH_VA;
		cmd.tlbi.asid	= smmu_domain->cd.asid;
	} else {
		cmd.opcode	= CMDQ_OP_TLBI_S2_IPA;
		cmd.tlbi.vmid	= smmu_domain->s2_cfg.vmid;
	}
	__arm_smmu_tlb_inv_range(&cmd, iova, size, granule, smmu_domain);

	/*
	 * Unfortunately, this can't be leaf-only since we may have
	 * zapped an entire table.
	 */
	arm_smmu_atc_inv_domain(smmu_domain, iova, size);
}

void arm_smmu_tlb_inv_range_asid(unsigned long iova, size_t size, int asid,
				 size_t granule, bool leaf,
				 struct arm_smmu_domain *smmu_domain)
{
	struct arm_smmu_cmdq_ent cmd = {
		.opcode	= smmu_domain->smmu->features & ARM_SMMU_FEAT_E2H ?
			  CMDQ_OP_TLBI_EL2_VA : CMDQ_OP_TLBI_NH_VA,
		.tlbi = {
			.asid	= asid,
			.leaf	= leaf,
		},
	};

	__arm_smmu_tlb_inv_range(&cmd, iova, size, granule, smmu_domain);
}

static void arm_smmu_tlb_inv_page_nosync(struct iommu_iotlb_gather *gather,
					 unsigned long iova, size_t granule,
					 void *cookie)
{
	struct arm_smmu_domain *smmu_domain = cookie;
	struct iommu_domain *domain = &smmu_domain->domain;

	iommu_iotlb_gather_add_page(domain, gather, iova, granule);
}

static void arm_smmu_tlb_inv_walk(unsigned long iova, size_t size,
				  size_t granule, void *cookie)
{
	arm_smmu_tlb_inv_range_domain(iova, size, granule, false, cookie);
}

static const struct iommu_flush_ops arm_smmu_flush_ops = {
	.tlb_flush_all	= arm_smmu_tlb_inv_context,
	.tlb_flush_walk = arm_smmu_tlb_inv_walk,
	.tlb_add_page	= arm_smmu_tlb_inv_page_nosync,
};

static bool arm_smmu_dbm_capable(struct arm_smmu_device *smmu)
{
	u32 features = (ARM_SMMU_FEAT_HD | ARM_SMMU_FEAT_COHERENCY);

	return (smmu->features & features) == features;
}

/* IOMMU API */
static bool arm_smmu_capable(struct device *dev, enum iommu_cap cap)
{
	struct arm_smmu_master *master = dev_iommu_priv_get(dev);

	switch (cap) {
	case IOMMU_CAP_CACHE_COHERENCY:
		/* Assume that a coherent TCU implies coherent TBUs */
		return master->smmu->features & ARM_SMMU_FEAT_COHERENCY;
	case IOMMU_CAP_NOEXEC:
	case IOMMU_CAP_DEFERRED_FLUSH:
		return true;
	case IOMMU_CAP_DIRTY_TRACKING:
		return arm_smmu_dbm_capable(master->smmu);
	default:
		return false;
	}
}

struct arm_smmu_domain *arm_smmu_domain_alloc(void)
{
	struct arm_smmu_domain *smmu_domain;

	smmu_domain = kzalloc(sizeof(*smmu_domain), GFP_KERNEL);
	if (!smmu_domain)
		return ERR_PTR(-ENOMEM);

	mutex_init(&smmu_domain->init_mutex);
	INIT_LIST_HEAD(&smmu_domain->devices);
	spin_lock_init(&smmu_domain->devices_lock);

	return smmu_domain;
}

static struct iommu_domain *arm_smmu_domain_alloc_paging(struct device *dev)
{
	struct arm_smmu_domain *smmu_domain;

	/*
	 * Allocate the domain and initialise some of its data structures.
	 * We can't really do anything meaningful until we've added a
	 * master.
	 */
	smmu_domain = arm_smmu_domain_alloc();
	if (IS_ERR(smmu_domain))
		return ERR_CAST(smmu_domain);

	if (dev) {
		struct arm_smmu_master *master = dev_iommu_priv_get(dev);
		int ret;

		ret = arm_smmu_domain_finalise(smmu_domain, master->smmu, 0);
		if (ret) {
			kfree(smmu_domain);
			return ERR_PTR(ret);
		}
	}
	return &smmu_domain->domain;
}

static void arm_smmu_domain_free_paging(struct iommu_domain *domain)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct arm_smmu_device *smmu = smmu_domain->smmu;

	free_io_pgtable_ops(smmu_domain->pgtbl_ops);

	/* Free the ASID or VMID */
	if (smmu_domain->stage == ARM_SMMU_DOMAIN_S1) {
		/* Prevent SVA from touching the CD while we're freeing it */
		mutex_lock(&arm_smmu_asid_lock);
		xa_erase(&arm_smmu_asid_xa, smmu_domain->cd.asid);
		mutex_unlock(&arm_smmu_asid_lock);
	} else {
		struct arm_smmu_s2_cfg *cfg = &smmu_domain->s2_cfg;
		if (cfg->vmid)
			ida_free(&smmu->vmid_map, cfg->vmid);
	}

	kfree(smmu_domain);
}

static int arm_smmu_domain_finalise_s1(struct arm_smmu_device *smmu,
				       struct arm_smmu_domain *smmu_domain)
{
	int ret;
	u32 asid = 0;
	struct arm_smmu_ctx_desc *cd = &smmu_domain->cd;

	/* Prevent SVA from modifying the ASID until it is written to the CD */
	mutex_lock(&arm_smmu_asid_lock);
	ret = xa_alloc(&arm_smmu_asid_xa, &asid, smmu_domain,
		       XA_LIMIT(1, (1 << smmu->asid_bits) - 1), GFP_KERNEL);
	cd->asid	= (u16)asid;
	mutex_unlock(&arm_smmu_asid_lock);
	return ret;
}

static int arm_smmu_domain_finalise_s2(struct arm_smmu_device *smmu,
				       struct arm_smmu_domain *smmu_domain)
{
	int vmid;
	struct arm_smmu_s2_cfg *cfg = &smmu_domain->s2_cfg;

	/* Reserve VMID 0 for stage-2 bypass STEs */
	vmid = ida_alloc_range(&smmu->vmid_map, 1, (1 << smmu->vmid_bits) - 1,
			       GFP_KERNEL);
	if (vmid < 0)
		return vmid;

	cfg->vmid	= (u16)vmid;
	return 0;
}

static int arm_smmu_domain_finalise(struct arm_smmu_domain *smmu_domain,
				    struct arm_smmu_device *smmu, u32 flags)
{
	int ret;
	enum io_pgtable_fmt fmt;
	struct io_pgtable_cfg pgtbl_cfg;
	struct io_pgtable_ops *pgtbl_ops;
	int (*finalise_stage_fn)(struct arm_smmu_device *smmu,
				 struct arm_smmu_domain *smmu_domain);
	bool enable_dirty = flags & IOMMU_HWPT_ALLOC_DIRTY_TRACKING;

	/* Restrict the stage to what we can actually support */
	if (!(smmu->features & ARM_SMMU_FEAT_TRANS_S1))
		smmu_domain->stage = ARM_SMMU_DOMAIN_S2;
	if (!(smmu->features & ARM_SMMU_FEAT_TRANS_S2))
		smmu_domain->stage = ARM_SMMU_DOMAIN_S1;

	pgtbl_cfg = (struct io_pgtable_cfg) {
		.pgsize_bitmap	= smmu->pgsize_bitmap,
		.coherent_walk	= smmu->features & ARM_SMMU_FEAT_COHERENCY,
		.tlb		= &arm_smmu_flush_ops,
		.iommu_dev	= smmu->dev,
	};

	switch (smmu_domain->stage) {
	case ARM_SMMU_DOMAIN_S1: {
		unsigned long ias = (smmu->features &
				     ARM_SMMU_FEAT_VAX) ? 52 : 48;

		pgtbl_cfg.ias = min_t(unsigned long, ias, VA_BITS);
		pgtbl_cfg.oas = smmu->ias;
		if (enable_dirty)
			pgtbl_cfg.quirks |= IO_PGTABLE_QUIRK_ARM_HD;
		fmt = ARM_64_LPAE_S1;
		finalise_stage_fn = arm_smmu_domain_finalise_s1;
		break;
	}
	case ARM_SMMU_DOMAIN_S2:
		if (enable_dirty)
			return -EOPNOTSUPP;
		pgtbl_cfg.ias = smmu->ias;
		pgtbl_cfg.oas = smmu->oas;
		fmt = ARM_64_LPAE_S2;
		finalise_stage_fn = arm_smmu_domain_finalise_s2;
		break;
	default:
		return -EINVAL;
	}

	pgtbl_ops = alloc_io_pgtable_ops(fmt, &pgtbl_cfg, smmu_domain);
	if (!pgtbl_ops)
		return -ENOMEM;

	smmu_domain->domain.pgsize_bitmap = pgtbl_cfg.pgsize_bitmap;
	smmu_domain->domain.geometry.aperture_end = (1UL << pgtbl_cfg.ias) - 1;
	smmu_domain->domain.geometry.force_aperture = true;
	if (enable_dirty && smmu_domain->stage == ARM_SMMU_DOMAIN_S1)
		smmu_domain->domain.dirty_ops = &arm_smmu_dirty_ops;

	ret = finalise_stage_fn(smmu, smmu_domain);
	if (ret < 0) {
		free_io_pgtable_ops(pgtbl_ops);
		return ret;
	}

	smmu_domain->pgtbl_ops = pgtbl_ops;
	smmu_domain->smmu = smmu;
	return 0;
}

static struct arm_smmu_ste *
arm_smmu_get_step_for_sid(struct arm_smmu_device *smmu, u32 sid)
{
	struct arm_smmu_strtab_cfg *cfg = &smmu->strtab_cfg;

	if (smmu->features & ARM_SMMU_FEAT_2_LVL_STRTAB) {
		unsigned int idx1, idx2;

		/* Two-level walk */
		idx1 = (sid >> STRTAB_SPLIT) * STRTAB_L1_DESC_DWORDS;
		idx2 = sid & ((1 << STRTAB_SPLIT) - 1);
		return &cfg->l1_desc[idx1].l2ptr[idx2];
	} else {
		/* Simple linear lookup */
		return (struct arm_smmu_ste *)&cfg
			       ->strtab[sid * STRTAB_STE_DWORDS];
	}
}

static void arm_smmu_install_ste_for_dev(struct arm_smmu_master *master,
					 const struct arm_smmu_ste *target)
{
	int i, j;
	struct arm_smmu_device *smmu = master->smmu;

	master->cd_table.in_ste =
		FIELD_GET(STRTAB_STE_0_CFG, le64_to_cpu(target->data[0])) ==
		STRTAB_STE_0_CFG_S1_TRANS;
	master->ste_ats_enabled =
		FIELD_GET(STRTAB_STE_1_EATS, le64_to_cpu(target->data[1])) ==
		STRTAB_STE_1_EATS_TRANS;

	for (i = 0; i < master->num_streams; ++i) {
		u32 sid = master->streams[i].id;
		struct arm_smmu_ste *step =
			arm_smmu_get_step_for_sid(smmu, sid);

		/* Bridged PCI devices may end up with duplicated IDs */
		for (j = 0; j < i; j++)
			if (master->streams[j].id == sid)
				break;
		if (j < i)
			continue;

		arm_smmu_write_ste(master, sid, step, target);
	}
}

static bool arm_smmu_ats_supported(struct arm_smmu_master *master)
{
	struct device *dev = master->dev;
	struct arm_smmu_device *smmu = master->smmu;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	if (!(smmu->features & ARM_SMMU_FEAT_ATS))
		return false;

	if (!(fwspec->flags & IOMMU_FWSPEC_PCI_RC_ATS))
		return false;

	return dev_is_pci(dev) && pci_ats_supported(to_pci_dev(dev));
}

static void arm_smmu_enable_ats(struct arm_smmu_master *master)
{
	size_t stu;
	struct pci_dev *pdev;
	struct arm_smmu_device *smmu = master->smmu;

	/* Smallest Translation Unit: log2 of the smallest supported granule */
	stu = __ffs(smmu->pgsize_bitmap);
	pdev = to_pci_dev(master->dev);

	/*
	 * ATC invalidation of PASID 0 causes the entire ATC to be flushed.
	 */
	arm_smmu_atc_inv_master(master, IOMMU_NO_PASID);
	if (pci_enable_ats(pdev, stu))
		dev_err(master->dev, "Failed to enable ATS (STU %zu)\n", stu);
}

static int arm_smmu_enable_pasid(struct arm_smmu_master *master)
{
	int ret;
	int features;
	int num_pasids;
	struct pci_dev *pdev;

	if (!dev_is_pci(master->dev))
		return -ENODEV;

	pdev = to_pci_dev(master->dev);

	features = pci_pasid_features(pdev);
	if (features < 0)
		return features;

	num_pasids = pci_max_pasids(pdev);
	if (num_pasids <= 0)
		return num_pasids;

	ret = pci_enable_pasid(pdev, features);
	if (ret) {
		dev_err(&pdev->dev, "Failed to enable PASID\n");
		return ret;
	}

	master->ssid_bits = min_t(u8, ilog2(num_pasids),
				  master->smmu->ssid_bits);
	return 0;
}

static void arm_smmu_disable_pasid(struct arm_smmu_master *master)
{
	struct pci_dev *pdev;

	if (!dev_is_pci(master->dev))
		return;

	pdev = to_pci_dev(master->dev);

	if (!pdev->pasid_enabled)
		return;

	master->ssid_bits = 0;
	pci_disable_pasid(pdev);
}

static struct arm_smmu_master_domain *
arm_smmu_find_master_domain(struct arm_smmu_domain *smmu_domain,
			    struct arm_smmu_master *master,
			    ioasid_t ssid)
{
	struct arm_smmu_master_domain *master_domain;

	lockdep_assert_held(&smmu_domain->devices_lock);

	list_for_each_entry(master_domain, &smmu_domain->devices,
			    devices_elm) {
		if (master_domain->master == master &&
		    master_domain->ssid == ssid)
			return master_domain;
	}
	return NULL;
}

/*
 * If the domain uses the smmu_domain->devices list return the arm_smmu_domain
 * structure, otherwise NULL. These domains track attached devices so they can
 * issue invalidations.
 */
static struct arm_smmu_domain *
to_smmu_domain_devices(struct iommu_domain *domain)
{
	/* The domain can be NULL only when processing the first attach */
	if (!domain)
		return NULL;
	if ((domain->type & __IOMMU_DOMAIN_PAGING) ||
	    domain->type == IOMMU_DOMAIN_SVA)
		return to_smmu_domain(domain);
	return NULL;
}

static void arm_smmu_remove_master_domain(struct arm_smmu_master *master,
					  struct iommu_domain *domain,
					  ioasid_t ssid)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain_devices(domain);
	struct arm_smmu_master_domain *master_domain;
	unsigned long flags;

	if (!smmu_domain)
		return;

	spin_lock_irqsave(&smmu_domain->devices_lock, flags);
	master_domain = arm_smmu_find_master_domain(smmu_domain, master, ssid);
	if (master_domain) {
		list_del(&master_domain->devices_elm);
		kfree(master_domain);
		if (master->ats_enabled)
			atomic_dec(&smmu_domain->nr_ats_masters);
	}
	spin_unlock_irqrestore(&smmu_domain->devices_lock, flags);
}

struct arm_smmu_attach_state {
	/* Inputs */
	struct iommu_domain *old_domain;
	struct arm_smmu_master *master;
	bool cd_needs_ats;
	ioasid_t ssid;
	/* Resulting state */
	bool ats_enabled;
};

/*
 * Start the sequence to attach a domain to a master. The sequence contains three
 * steps:
 *  arm_smmu_attach_prepare()
 *  arm_smmu_install_ste_for_dev()
 *  arm_smmu_attach_commit()
 *
 * If prepare succeeds then the sequence must be completed. The STE installed
 * must set the STE.EATS field according to state.ats_enabled.
 *
 * If the device supports ATS then this determines if EATS should be enabled
 * in the STE, and starts sequencing EATS disable if required.
 *
 * The change of the EATS in the STE and the PCI ATS config space is managed by
 * this sequence to be in the right order so that if PCI ATS is enabled then
 * STE.ETAS is enabled.
 *
 * new_domain can be a non-paging domain. In this case ATS will not be enabled,
 * and invalidations won't be tracked.
 */
static int arm_smmu_attach_prepare(struct arm_smmu_attach_state *state,
				   struct iommu_domain *new_domain)
{
	struct arm_smmu_master *master = state->master;
	struct arm_smmu_master_domain *master_domain;
	struct arm_smmu_domain *smmu_domain =
		to_smmu_domain_devices(new_domain);
	unsigned long flags;

	/*
	 * arm_smmu_share_asid() must not see two domains pointing to the same
	 * arm_smmu_master_domain contents otherwise it could randomly write one
	 * or the other to the CD.
	 */
	lockdep_assert_held(&arm_smmu_asid_lock);

	if (smmu_domain || state->cd_needs_ats) {
		/*
		 * The SMMU does not support enabling ATS with bypass/abort.
		 * When the STE is in bypass (STE.Config[2:0] == 0b100), ATS
		 * Translation Requests and Translated transactions are denied
		 * as though ATS is disabled for the stream (STE.EATS == 0b00),
		 * causing F_BAD_ATS_TREQ and F_TRANSL_FORBIDDEN events
		 * (IHI0070Ea 5.2 Stream Table Entry). Thus ATS can only be
		 * enabled if we have arm_smmu_domain, those always have page
		 * tables.
		 */
		state->ats_enabled = arm_smmu_ats_supported(master);
	}

	if (smmu_domain) {
		master_domain = kzalloc(sizeof(*master_domain), GFP_KERNEL);
		if (!master_domain)
			return -ENOMEM;
		master_domain->master = master;
		master_domain->ssid = state->ssid;

		/*
		 * During prepare we want the current smmu_domain and new
		 * smmu_domain to be in the devices list before we change any
		 * HW. This ensures that both domains will send ATS
		 * invalidations to the master until we are done.
		 *
		 * It is tempting to make this list only track masters that are
		 * using ATS, but arm_smmu_share_asid() also uses this to change
		 * the ASID of a domain, unrelated to ATS.
		 *
		 * Notice if we are re-attaching the same domain then the list
		 * will have two identical entries and commit will remove only
		 * one of them.
		 */
		spin_lock_irqsave(&smmu_domain->devices_lock, flags);
		if (state->ats_enabled)
			atomic_inc(&smmu_domain->nr_ats_masters);
		list_add(&master_domain->devices_elm, &smmu_domain->devices);
		spin_unlock_irqrestore(&smmu_domain->devices_lock, flags);
	}

	if (!state->ats_enabled && master->ats_enabled) {
		pci_disable_ats(to_pci_dev(master->dev));
		/*
		 * This is probably overkill, but the config write for disabling
		 * ATS should complete before the STE is configured to generate
		 * UR to avoid AER noise.
		 */
		wmb();
	}
	return 0;
}

/*
 * Commit is done after the STE/CD are configured with the EATS setting. It
 * completes synchronizing the PCI device's ATC and finishes manipulating the
 * smmu_domain->devices list.
 */
static void arm_smmu_attach_commit(struct arm_smmu_attach_state *state)
{
	struct arm_smmu_master *master = state->master;

	lockdep_assert_held(&arm_smmu_asid_lock);

	if (state->ats_enabled && !master->ats_enabled) {
		arm_smmu_enable_ats(master);
	} else if (state->ats_enabled && master->ats_enabled) {
		/*
		 * The translation has changed, flush the ATC. At this point the
		 * SMMU is translating for the new domain and both the old&new
		 * domain will issue invalidations.
		 */
		arm_smmu_atc_inv_master(master, state->ssid);
	} else if (!state->ats_enabled && master->ats_enabled) {
		/* ATS is being switched off, invalidate the entire ATC */
		arm_smmu_atc_inv_master(master, IOMMU_NO_PASID);
	}
	master->ats_enabled = state->ats_enabled;

	arm_smmu_remove_master_domain(master, state->old_domain, state->ssid);
}

static int arm_smmu_attach_dev(struct iommu_domain *domain, struct device *dev)
{
	int ret = 0;
	struct arm_smmu_ste target;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);
	struct arm_smmu_device *smmu;
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct arm_smmu_attach_state state = {
		.old_domain = iommu_get_domain_for_dev(dev),
		.ssid = IOMMU_NO_PASID,
	};
	struct arm_smmu_master *master;
	struct arm_smmu_cd *cdptr;

	if (!fwspec)
		return -ENOENT;

	state.master = master = dev_iommu_priv_get(dev);
	smmu = master->smmu;

	mutex_lock(&smmu_domain->init_mutex);

	if (!smmu_domain->smmu) {
		ret = arm_smmu_domain_finalise(smmu_domain, smmu, 0);
	} else if (smmu_domain->smmu != smmu)
		ret = -EINVAL;

	mutex_unlock(&smmu_domain->init_mutex);
	if (ret)
		return ret;

	if (smmu_domain->stage == ARM_SMMU_DOMAIN_S1) {
		cdptr = arm_smmu_alloc_cd_ptr(master, IOMMU_NO_PASID);
		if (!cdptr)
			return -ENOMEM;
	} else if (arm_smmu_ssids_in_use(&master->cd_table))
		return -EBUSY;

	/*
	 * Prevent arm_smmu_share_asid() from trying to change the ASID
	 * of either the old or new domain while we are working on it.
	 * This allows the STE and the smmu_domain->devices list to
	 * be inconsistent during this routine.
	 */
	mutex_lock(&arm_smmu_asid_lock);

	ret = arm_smmu_attach_prepare(&state, domain);
	if (ret) {
		mutex_unlock(&arm_smmu_asid_lock);
		return ret;
	}

	switch (smmu_domain->stage) {
	case ARM_SMMU_DOMAIN_S1: {
		struct arm_smmu_cd target_cd;

		arm_smmu_make_s1_cd(&target_cd, master, smmu_domain);
		arm_smmu_write_cd_entry(master, IOMMU_NO_PASID, cdptr,
					&target_cd);
		arm_smmu_make_cdtable_ste(&target, master, state.ats_enabled,
					  STRTAB_STE_1_S1DSS_SSID0);
		arm_smmu_install_ste_for_dev(master, &target);
		break;
	}
	case ARM_SMMU_DOMAIN_S2:
		arm_smmu_make_s2_domain_ste(&target, master, smmu_domain,
					    state.ats_enabled);
		arm_smmu_install_ste_for_dev(master, &target);
		arm_smmu_clear_cd(master, IOMMU_NO_PASID);
		break;
	}

	arm_smmu_attach_commit(&state);
	mutex_unlock(&arm_smmu_asid_lock);
	return 0;
}

static int arm_smmu_s1_set_dev_pasid(struct iommu_domain *domain,
				      struct device *dev, ioasid_t id)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct arm_smmu_master *master = dev_iommu_priv_get(dev);
	struct arm_smmu_device *smmu = master->smmu;
	struct arm_smmu_cd target_cd;
	int ret = 0;

	mutex_lock(&smmu_domain->init_mutex);
	if (!smmu_domain->smmu)
		ret = arm_smmu_domain_finalise(smmu_domain, smmu, 0);
	else if (smmu_domain->smmu != smmu)
		ret = -EINVAL;
	mutex_unlock(&smmu_domain->init_mutex);
	if (ret)
		return ret;

	if (smmu_domain->stage != ARM_SMMU_DOMAIN_S1)
		return -EINVAL;

	/*
	 * We can read cd.asid outside the lock because arm_smmu_set_pasid()
	 * will fix it
	 */
	arm_smmu_make_s1_cd(&target_cd, master, smmu_domain);
	return arm_smmu_set_pasid(master, to_smmu_domain(domain), id,
				  &target_cd);
}

static void arm_smmu_update_ste(struct arm_smmu_master *master,
				struct iommu_domain *sid_domain,
				bool ats_enabled)
{
	unsigned int s1dss = STRTAB_STE_1_S1DSS_TERMINATE;
	struct arm_smmu_ste ste;

	if (master->cd_table.in_ste && master->ste_ats_enabled == ats_enabled)
		return;

	if (sid_domain->type == IOMMU_DOMAIN_IDENTITY)
		s1dss = STRTAB_STE_1_S1DSS_BYPASS;
	else
		WARN_ON(sid_domain->type != IOMMU_DOMAIN_BLOCKED);

	/*
	 * Change the STE into a cdtable one with SID IDENTITY/BLOCKED behavior
	 * using s1dss if necessary. If the cd_table is already installed then
	 * the S1DSS is correct and this will just update the EATS. Otherwise it
	 * installs the entire thing. This will be hitless.
	 */
	arm_smmu_make_cdtable_ste(&ste, master, ats_enabled, s1dss);
	arm_smmu_install_ste_for_dev(master, &ste);
}

int arm_smmu_set_pasid(struct arm_smmu_master *master,
		       struct arm_smmu_domain *smmu_domain, ioasid_t pasid,
		       struct arm_smmu_cd *cd)
{
	struct iommu_domain *sid_domain = iommu_get_domain_for_dev(master->dev);
	struct arm_smmu_attach_state state = {
		.master = master,
		/*
		 * For now the core code prevents calling this when a domain is
		 * already attached, no need to set old_domain.
		 */
		.ssid = pasid,
	};
	struct arm_smmu_cd *cdptr;
	int ret;

	/* The core code validates pasid */

	if (smmu_domain->smmu != master->smmu)
		return -EINVAL;

	if (!master->cd_table.in_ste &&
	    sid_domain->type != IOMMU_DOMAIN_IDENTITY &&
	    sid_domain->type != IOMMU_DOMAIN_BLOCKED)
		return -EINVAL;

	cdptr = arm_smmu_alloc_cd_ptr(master, pasid);
	if (!cdptr)
		return -ENOMEM;

	mutex_lock(&arm_smmu_asid_lock);
	ret = arm_smmu_attach_prepare(&state, &smmu_domain->domain);
	if (ret)
		goto out_unlock;

	/*
	 * We don't want to obtain to the asid_lock too early, so fix up the
	 * caller set ASID under the lock in case it changed.
	 */
	cd->data[0] &= ~cpu_to_le64(CTXDESC_CD_0_ASID);
	cd->data[0] |= cpu_to_le64(
		FIELD_PREP(CTXDESC_CD_0_ASID, smmu_domain->cd.asid));

	arm_smmu_write_cd_entry(master, pasid, cdptr, cd);
	arm_smmu_update_ste(master, sid_domain, state.ats_enabled);

	arm_smmu_attach_commit(&state);

out_unlock:
	mutex_unlock(&arm_smmu_asid_lock);
	return ret;
}

static void arm_smmu_remove_dev_pasid(struct device *dev, ioasid_t pasid,
				      struct iommu_domain *domain)
{
	struct arm_smmu_master *master = dev_iommu_priv_get(dev);
	struct arm_smmu_domain *smmu_domain;

	smmu_domain = to_smmu_domain(domain);

	mutex_lock(&arm_smmu_asid_lock);
	arm_smmu_clear_cd(master, pasid);
	if (master->ats_enabled)
		arm_smmu_atc_inv_master(master, pasid);
	arm_smmu_remove_master_domain(master, &smmu_domain->domain, pasid);
	mutex_unlock(&arm_smmu_asid_lock);

	/*
	 * When the last user of the CD table goes away downgrade the STE back
	 * to a non-cd_table one.
	 */
	if (!arm_smmu_ssids_in_use(&master->cd_table)) {
		struct iommu_domain *sid_domain =
			iommu_get_domain_for_dev(master->dev);

		if (sid_domain->type == IOMMU_DOMAIN_IDENTITY ||
		    sid_domain->type == IOMMU_DOMAIN_BLOCKED)
			sid_domain->ops->attach_dev(sid_domain, dev);
	}
}

static void arm_smmu_attach_dev_ste(struct iommu_domain *domain,
				    struct device *dev,
				    struct arm_smmu_ste *ste,
				    unsigned int s1dss)
{
	struct arm_smmu_master *master = dev_iommu_priv_get(dev);
	struct arm_smmu_attach_state state = {
		.master = master,
		.old_domain = iommu_get_domain_for_dev(dev),
		.ssid = IOMMU_NO_PASID,
	};

	/*
	 * Do not allow any ASID to be changed while are working on the STE,
	 * otherwise we could miss invalidations.
	 */
	mutex_lock(&arm_smmu_asid_lock);

	/*
	 * If the CD table is not in use we can use the provided STE, otherwise
	 * we use a cdtable STE with the provided S1DSS.
	 */
	if (arm_smmu_ssids_in_use(&master->cd_table)) {
		/*
		 * If a CD table has to be present then we need to run with ATS
		 * on even though the RID will fail ATS queries with UR. This is
		 * because we have no idea what the PASID's need.
		 */
		state.cd_needs_ats = true;
		arm_smmu_attach_prepare(&state, domain);
		arm_smmu_make_cdtable_ste(ste, master, state.ats_enabled, s1dss);
	} else {
		arm_smmu_attach_prepare(&state, domain);
	}
	arm_smmu_install_ste_for_dev(master, ste);
	arm_smmu_attach_commit(&state);
	mutex_unlock(&arm_smmu_asid_lock);

	/*
	 * This has to be done after removing the master from the
	 * arm_smmu_domain->devices to avoid races updating the same context
	 * descriptor from arm_smmu_share_asid().
	 */
	arm_smmu_clear_cd(master, IOMMU_NO_PASID);
}

static int arm_smmu_attach_dev_identity(struct iommu_domain *domain,
					struct device *dev)
{
	struct arm_smmu_ste ste;
	struct arm_smmu_master *master = dev_iommu_priv_get(dev);

	arm_smmu_make_bypass_ste(master->smmu, &ste);
	arm_smmu_attach_dev_ste(domain, dev, &ste, STRTAB_STE_1_S1DSS_BYPASS);
	return 0;
}

static const struct iommu_domain_ops arm_smmu_identity_ops = {
	.attach_dev = arm_smmu_attach_dev_identity,
};

static struct iommu_domain arm_smmu_identity_domain = {
	.type = IOMMU_DOMAIN_IDENTITY,
	.ops = &arm_smmu_identity_ops,
};

static int arm_smmu_attach_dev_blocked(struct iommu_domain *domain,
					struct device *dev)
{
	struct arm_smmu_ste ste;

	arm_smmu_make_abort_ste(&ste);
	arm_smmu_attach_dev_ste(domain, dev, &ste,
				STRTAB_STE_1_S1DSS_TERMINATE);
	return 0;
}

static const struct iommu_domain_ops arm_smmu_blocked_ops = {
	.attach_dev = arm_smmu_attach_dev_blocked,
};

static struct iommu_domain arm_smmu_blocked_domain = {
	.type = IOMMU_DOMAIN_BLOCKED,
	.ops = &arm_smmu_blocked_ops,
};

static struct iommu_domain *
arm_smmu_domain_alloc_user(struct device *dev, u32 flags,
			   struct iommu_domain *parent,
			   const struct iommu_user_data *user_data)
{
	struct arm_smmu_master *master = dev_iommu_priv_get(dev);
	const u32 PAGING_FLAGS = IOMMU_HWPT_ALLOC_DIRTY_TRACKING;
	struct arm_smmu_domain *smmu_domain;
	int ret;

	if (flags & ~PAGING_FLAGS)
		return ERR_PTR(-EOPNOTSUPP);
	if (parent || user_data)
		return ERR_PTR(-EOPNOTSUPP);

	smmu_domain = arm_smmu_domain_alloc();
	if (IS_ERR(smmu_domain))
		return ERR_CAST(smmu_domain);

	smmu_domain->domain.type = IOMMU_DOMAIN_UNMANAGED;
	smmu_domain->domain.ops = arm_smmu_ops.default_domain_ops;
	ret = arm_smmu_domain_finalise(smmu_domain, master->smmu, flags);
	if (ret)
		goto err_free;
	return &smmu_domain->domain;

err_free:
	kfree(smmu_domain);
	return ERR_PTR(ret);
}

static int arm_smmu_map_pages(struct iommu_domain *domain, unsigned long iova,
			      phys_addr_t paddr, size_t pgsize, size_t pgcount,
			      int prot, gfp_t gfp, size_t *mapped)
{
	struct io_pgtable_ops *ops = to_smmu_domain(domain)->pgtbl_ops;

	if (!ops)
		return -ENODEV;

	return ops->map_pages(ops, iova, paddr, pgsize, pgcount, prot, gfp, mapped);
}

static size_t arm_smmu_unmap_pages(struct iommu_domain *domain, unsigned long iova,
				   size_t pgsize, size_t pgcount,
				   struct iommu_iotlb_gather *gather)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct io_pgtable_ops *ops = smmu_domain->pgtbl_ops;

	if (!ops)
		return 0;

	return ops->unmap_pages(ops, iova, pgsize, pgcount, gather);
}

static void arm_smmu_flush_iotlb_all(struct iommu_domain *domain)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);

	if (smmu_domain->smmu)
		arm_smmu_tlb_inv_context(smmu_domain);
}

static void arm_smmu_iotlb_sync(struct iommu_domain *domain,
				struct iommu_iotlb_gather *gather)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);

	if (!gather->pgsize)
		return;

	arm_smmu_tlb_inv_range_domain(gather->start,
				      gather->end - gather->start + 1,
				      gather->pgsize, true, smmu_domain);
}

static phys_addr_t
arm_smmu_iova_to_phys(struct iommu_domain *domain, dma_addr_t iova)
{
	struct io_pgtable_ops *ops = to_smmu_domain(domain)->pgtbl_ops;

	if (!ops)
		return 0;

	return ops->iova_to_phys(ops, iova);
}

static struct platform_driver arm_smmu_driver;

static
struct arm_smmu_device *arm_smmu_get_by_fwnode(struct fwnode_handle *fwnode)
{
	struct device *dev = driver_find_device_by_fwnode(&arm_smmu_driver.driver,
							  fwnode);
	put_device(dev);
	return dev ? dev_get_drvdata(dev) : NULL;
}

static bool arm_smmu_sid_in_range(struct arm_smmu_device *smmu, u32 sid)
{
	unsigned long limit = smmu->strtab_cfg.num_l1_ents;

	if (smmu->features & ARM_SMMU_FEAT_2_LVL_STRTAB)
		limit *= 1UL << STRTAB_SPLIT;

	return sid < limit;
}

static int arm_smmu_init_sid_strtab(struct arm_smmu_device *smmu, u32 sid)
{
	/* Check the SIDs are in range of the SMMU and our stream table */
	if (!arm_smmu_sid_in_range(smmu, sid))
		return -ERANGE;

	/* Ensure l2 strtab is initialised */
	if (smmu->features & ARM_SMMU_FEAT_2_LVL_STRTAB)
		return arm_smmu_init_l2_strtab(smmu, sid);

	return 0;
}

static int arm_smmu_insert_master(struct arm_smmu_device *smmu,
				  struct arm_smmu_master *master)
{
	int i;
	int ret = 0;
	struct arm_smmu_stream *new_stream, *cur_stream;
	struct rb_node **new_node, *parent_node = NULL;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(master->dev);

	master->streams = kcalloc(fwspec->num_ids, sizeof(*master->streams),
				  GFP_KERNEL);
	if (!master->streams)
		return -ENOMEM;
	master->num_streams = fwspec->num_ids;

	mutex_lock(&smmu->streams_mutex);
	for (i = 0; i < fwspec->num_ids; i++) {
		u32 sid = fwspec->ids[i];

		new_stream = &master->streams[i];
		new_stream->id = sid;
		new_stream->master = master;

		ret = arm_smmu_init_sid_strtab(smmu, sid);
		if (ret)
			break;

		/* Insert into SID tree */
		new_node = &(smmu->streams.rb_node);
		while (*new_node) {
			cur_stream = rb_entry(*new_node, struct arm_smmu_stream,
					      node);
			parent_node = *new_node;
			if (cur_stream->id > new_stream->id) {
				new_node = &((*new_node)->rb_left);
			} else if (cur_stream->id < new_stream->id) {
				new_node = &((*new_node)->rb_right);
			} else {
				dev_warn(master->dev,
					 "stream %u already in tree\n",
					 cur_stream->id);
				ret = -EINVAL;
				break;
			}
		}
		if (ret)
			break;

		rb_link_node(&new_stream->node, parent_node, new_node);
		rb_insert_color(&new_stream->node, &smmu->streams);
	}

	if (ret) {
		for (i--; i >= 0; i--)
			rb_erase(&master->streams[i].node, &smmu->streams);
		kfree(master->streams);
	}
	mutex_unlock(&smmu->streams_mutex);

	return ret;
}

static void arm_smmu_remove_master(struct arm_smmu_master *master)
{
	int i;
	struct arm_smmu_device *smmu = master->smmu;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(master->dev);

	if (!smmu || !master->streams)
		return;

	mutex_lock(&smmu->streams_mutex);
	for (i = 0; i < fwspec->num_ids; i++)
		rb_erase(&master->streams[i].node, &smmu->streams);
	mutex_unlock(&smmu->streams_mutex);

	kfree(master->streams);
}

static struct iommu_device *arm_smmu_probe_device(struct device *dev)
{
	int ret;
	struct arm_smmu_device *smmu;
	struct arm_smmu_master *master;
	struct iommu_fwspec *fwspec = dev_iommu_fwspec_get(dev);

	if (WARN_ON_ONCE(dev_iommu_priv_get(dev)))
		return ERR_PTR(-EBUSY);

	smmu = arm_smmu_get_by_fwnode(fwspec->iommu_fwnode);
	if (!smmu)
		return ERR_PTR(-ENODEV);

	master = kzalloc(sizeof(*master), GFP_KERNEL);
	if (!master)
		return ERR_PTR(-ENOMEM);

	master->dev = dev;
	master->smmu = smmu;
	dev_iommu_priv_set(dev, master);

	ret = arm_smmu_insert_master(smmu, master);
	if (ret)
		goto err_free_master;

	device_property_read_u32(dev, "pasid-num-bits", &master->ssid_bits);
	master->ssid_bits = min(smmu->ssid_bits, master->ssid_bits);

	/*
	 * Note that PASID must be enabled before, and disabled after ATS:
	 * PCI Express Base 4.0r1.0 - 10.5.1.3 ATS Control Register
	 *
	 *   Behavior is undefined if this bit is Set and the value of the PASID
	 *   Enable, Execute Requested Enable, or Privileged Mode Requested bits
	 *   are changed.
	 */
	arm_smmu_enable_pasid(master);

	if (!(smmu->features & ARM_SMMU_FEAT_2_LVL_CDTAB))
		master->ssid_bits = min_t(u8, master->ssid_bits,
					  CTXDESC_LINEAR_CDMAX);

	if ((smmu->features & ARM_SMMU_FEAT_STALLS &&
	     device_property_read_bool(dev, "dma-can-stall")) ||
	    smmu->features & ARM_SMMU_FEAT_STALL_FORCE)
		master->stall_enabled = true;

	return &smmu->iommu;

err_free_master:
	kfree(master);
	return ERR_PTR(ret);
}

static void arm_smmu_release_device(struct device *dev)
{
	struct arm_smmu_master *master = dev_iommu_priv_get(dev);

	if (WARN_ON(arm_smmu_master_sva_enabled(master)))
		iopf_queue_remove_device(master->smmu->evtq.iopf, dev);

	/* Put the STE back to what arm_smmu_init_strtab() sets */
	if (dev->iommu->require_direct)
		arm_smmu_attach_dev_identity(&arm_smmu_identity_domain, dev);
	else
		arm_smmu_attach_dev_blocked(&arm_smmu_blocked_domain, dev);

	arm_smmu_disable_pasid(master);
	arm_smmu_remove_master(master);
	if (master->cd_table.cdtab)
		arm_smmu_free_cd_tables(master);
	kfree(master);
}

static int arm_smmu_read_and_clear_dirty(struct iommu_domain *domain,
					 unsigned long iova, size_t size,
					 unsigned long flags,
					 struct iommu_dirty_bitmap *dirty)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	struct io_pgtable_ops *ops = smmu_domain->pgtbl_ops;

	return ops->read_and_clear_dirty(ops, iova, size, flags, dirty);
}

static int arm_smmu_set_dirty_tracking(struct iommu_domain *domain,
				       bool enabled)
{
	/*
	 * Always enabled and the dirty bitmap is cleared prior to
	 * set_dirty_tracking().
	 */
	return 0;
}

static struct iommu_group *arm_smmu_device_group(struct device *dev)
{
	struct iommu_group *group;

	/*
	 * We don't support devices sharing stream IDs other than PCI RID
	 * aliases, since the necessary ID-to-device lookup becomes rather
	 * impractical given a potential sparse 32-bit stream ID space.
	 */
	if (dev_is_pci(dev))
		group = pci_device_group(dev);
	else
		group = generic_device_group(dev);

	return group;
}

static int arm_smmu_enable_nesting(struct iommu_domain *domain)
{
	struct arm_smmu_domain *smmu_domain = to_smmu_domain(domain);
	int ret = 0;

	mutex_lock(&smmu_domain->init_mutex);
	if (smmu_domain->smmu)
		ret = -EPERM;
	else
		smmu_domain->stage = ARM_SMMU_DOMAIN_S2;
	mutex_unlock(&smmu_domain->init_mutex);

	return ret;
}

static int arm_smmu_of_xlate(struct device *dev,
			     const struct of_phandle_args *args)
{
	return iommu_fwspec_add_ids(dev, args->args, 1);
}

static void arm_smmu_get_resv_regions(struct device *dev,
				      struct list_head *head)
{
	struct iommu_resv_region *region;
	int prot = IOMMU_WRITE | IOMMU_NOEXEC | IOMMU_MMIO;

	region = iommu_alloc_resv_region(MSI_IOVA_BASE, MSI_IOVA_LENGTH,
					 prot, IOMMU_RESV_SW_MSI, GFP_KERNEL);
	if (!region)
		return;

	list_add_tail(&region->list, head);

	iommu_dma_get_resv_regions(dev, head);
}

static int arm_smmu_dev_enable_feature(struct device *dev,
				       enum iommu_dev_features feat)
{
	struct arm_smmu_master *master = dev_iommu_priv_get(dev);

	if (!master)
		return -ENODEV;

	switch (feat) {
	case IOMMU_DEV_FEAT_IOPF:
		if (!arm_smmu_master_iopf_supported(master))
			return -EINVAL;
		if (master->iopf_enabled)
			return -EBUSY;
		master->iopf_enabled = true;
		return 0;
	case IOMMU_DEV_FEAT_SVA:
		if (!arm_smmu_master_sva_supported(master))
			return -EINVAL;
		if (arm_smmu_master_sva_enabled(master))
			return -EBUSY;
		return arm_smmu_master_enable_sva(master);
	default:
		return -EINVAL;
	}
}

static int arm_smmu_dev_disable_feature(struct device *dev,
					enum iommu_dev_features feat)
{
	struct arm_smmu_master *master = dev_iommu_priv_get(dev);

	if (!master)
		return -EINVAL;

	switch (feat) {
	case IOMMU_DEV_FEAT_IOPF:
		if (!master->iopf_enabled)
			return -EINVAL;
		if (master->sva_enabled)
			return -EBUSY;
		master->iopf_enabled = false;
		return 0;
	case IOMMU_DEV_FEAT_SVA:
		if (!arm_smmu_master_sva_enabled(master))
			return -EINVAL;
		return arm_smmu_master_disable_sva(master);
	default:
		return -EINVAL;
	}
}

/*
 * HiSilicon PCIe tune and trace device can be used to trace TLP headers on the
 * PCIe link and save the data to memory by DMA. The hardware is restricted to
 * use identity mapping only.
 */
#define IS_HISI_PTT_DEVICE(pdev)	((pdev)->vendor == PCI_VENDOR_ID_HUAWEI && \
					 (pdev)->device == 0xa12e)

static int arm_smmu_def_domain_type(struct device *dev)
{
	if (dev_is_pci(dev)) {
		struct pci_dev *pdev = to_pci_dev(dev);

		if (IS_HISI_PTT_DEVICE(pdev))
			return IOMMU_DOMAIN_IDENTITY;
	}

	return 0;
}

static struct iommu_ops arm_smmu_ops = {
	.identity_domain	= &arm_smmu_identity_domain,
	.blocked_domain		= &arm_smmu_blocked_domain,
	.capable		= arm_smmu_capable,
	.domain_alloc_paging    = arm_smmu_domain_alloc_paging,
	.domain_alloc_sva       = arm_smmu_sva_domain_alloc,
	.domain_alloc_user	= arm_smmu_domain_alloc_user,
	.probe_device		= arm_smmu_probe_device,
	.release_device		= arm_smmu_release_device,
	.device_group		= arm_smmu_device_group,
	.of_xlate		= arm_smmu_of_xlate,
	.get_resv_regions	= arm_smmu_get_resv_regions,
	.remove_dev_pasid	= arm_smmu_remove_dev_pasid,
	.dev_enable_feat	= arm_smmu_dev_enable_feature,
	.dev_disable_feat	= arm_smmu_dev_disable_feature,
	.page_response		= arm_smmu_page_response,
	.def_domain_type	= arm_smmu_def_domain_type,
	.pgsize_bitmap		= -1UL, /* Restricted during device attach */
	.owner			= THIS_MODULE,
	.default_domain_ops = &(const struct iommu_domain_ops) {
		.attach_dev		= arm_smmu_attach_dev,
		.set_dev_pasid		= arm_smmu_s1_set_dev_pasid,
		.map_pages		= arm_smmu_map_pages,
		.unmap_pages		= arm_smmu_unmap_pages,
		.flush_iotlb_all	= arm_smmu_flush_iotlb_all,
		.iotlb_sync		= arm_smmu_iotlb_sync,
		.iova_to_phys		= arm_smmu_iova_to_phys,
		.enable_nesting		= arm_smmu_enable_nesting,
		.free			= arm_smmu_domain_free_paging,
	}
};

static struct iommu_dirty_ops arm_smmu_dirty_ops = {
	.read_and_clear_dirty	= arm_smmu_read_and_clear_dirty,
	.set_dirty_tracking     = arm_smmu_set_dirty_tracking,
};

/* Probing and initialisation functions */
static int arm_smmu_init_one_queue(struct arm_smmu_device *smmu,
				   struct arm_smmu_queue *q,
				   void __iomem *page,
				   unsigned long prod_off,
				   unsigned long cons_off,
				   size_t dwords, const char *name)
{
	size_t qsz;

	do {
		qsz = ((1 << q->llq.max_n_shift) * dwords) << 3;
		q->base = dmam_alloc_coherent(smmu->dev, qsz, &q->base_dma,
					      GFP_KERNEL);
		if (q->base || qsz < PAGE_SIZE)
			break;

		q->llq.max_n_shift--;
	} while (1);

	if (!q->base) {
		dev_err(smmu->dev,
			"failed to allocate queue (0x%zx bytes) for %s\n",
			qsz, name);
		return -ENOMEM;
	}

	if (!WARN_ON(q->base_dma & (qsz - 1))) {
		dev_info(smmu->dev, "allocated %u entries for %s\n",
			 1 << q->llq.max_n_shift, name);
	}

	q->prod_reg	= page + prod_off;
	q->cons_reg	= page + cons_off;
	q->ent_dwords	= dwords;

	q->q_base  = Q_BASE_RWA;
	q->q_base |= q->base_dma & Q_BASE_ADDR_MASK;
	q->q_base |= FIELD_PREP(Q_BASE_LOG2SIZE, q->llq.max_n_shift);

	q->llq.prod = q->llq.cons = 0;
	return 0;
}

static int arm_smmu_cmdq_init(struct arm_smmu_device *smmu)
{
	struct arm_smmu_cmdq *cmdq = &smmu->cmdq;
	unsigned int nents = 1 << cmdq->q.llq.max_n_shift;

	atomic_set(&cmdq->owner_prod, 0);
	atomic_set(&cmdq->lock, 0);

	cmdq->valid_map = (atomic_long_t *)devm_bitmap_zalloc(smmu->dev, nents,
							      GFP_KERNEL);
	if (!cmdq->valid_map)
		return -ENOMEM;

	return 0;
}

static int arm_smmu_init_queues(struct arm_smmu_device *smmu)
{
	int ret;

	/* cmdq */
	ret = arm_smmu_init_one_queue(smmu, &smmu->cmdq.q, smmu->base,
				      ARM_SMMU_CMDQ_PROD, ARM_SMMU_CMDQ_CONS,
				      CMDQ_ENT_DWORDS, "cmdq");
	if (ret)
		return ret;

	ret = arm_smmu_cmdq_init(smmu);
	if (ret)
		return ret;

	/* evtq */
	ret = arm_smmu_init_one_queue(smmu, &smmu->evtq.q, smmu->page1,
				      ARM_SMMU_EVTQ_PROD, ARM_SMMU_EVTQ_CONS,
				      EVTQ_ENT_DWORDS, "evtq");
	if (ret)
		return ret;

	if ((smmu->features & ARM_SMMU_FEAT_SVA) &&
	    (smmu->features & ARM_SMMU_FEAT_STALLS)) {
		smmu->evtq.iopf = iopf_queue_alloc(dev_name(smmu->dev));
		if (!smmu->evtq.iopf)
			return -ENOMEM;
	}

	/* priq */
	if (!(smmu->features & ARM_SMMU_FEAT_PRI))
		return 0;

	return arm_smmu_init_one_queue(smmu, &smmu->priq.q, smmu->page1,
				       ARM_SMMU_PRIQ_PROD, ARM_SMMU_PRIQ_CONS,
				       PRIQ_ENT_DWORDS, "priq");
}

static int arm_smmu_init_strtab_2lvl(struct arm_smmu_device *smmu)
{
	void *strtab;
	u64 reg;
	u32 size, l1size;
	struct arm_smmu_strtab_cfg *cfg = &smmu->strtab_cfg;

	/* Calculate the L1 size, capped to the SIDSIZE. */
	size = STRTAB_L1_SZ_SHIFT - (ilog2(STRTAB_L1_DESC_DWORDS) + 3);
	size = min(size, smmu->sid_bits - STRTAB_SPLIT);
	cfg->num_l1_ents = 1 << size;

	size += STRTAB_SPLIT;
	if (size < smmu->sid_bits)
		dev_warn(smmu->dev,
			 "2-level strtab only covers %u/%u bits of SID\n",
			 size, smmu->sid_bits);

	l1size = cfg->num_l1_ents * (STRTAB_L1_DESC_DWORDS << 3);
	strtab = dmam_alloc_coherent(smmu->dev, l1size, &cfg->strtab_dma,
				     GFP_KERNEL);
	if (!strtab) {
		dev_err(smmu->dev,
			"failed to allocate l1 stream table (%u bytes)\n",
			l1size);
		return -ENOMEM;
	}
	cfg->strtab = strtab;

	/* Configure strtab_base_cfg for 2 levels */
	reg  = FIELD_PREP(STRTAB_BASE_CFG_FMT, STRTAB_BASE_CFG_FMT_2LVL);
	reg |= FIELD_PREP(STRTAB_BASE_CFG_LOG2SIZE, size);
	reg |= FIELD_PREP(STRTAB_BASE_CFG_SPLIT, STRTAB_SPLIT);
	cfg->strtab_base_cfg = reg;

	cfg->l1_desc = devm_kcalloc(smmu->dev, cfg->num_l1_ents,
				    sizeof(*cfg->l1_desc), GFP_KERNEL);
	if (!cfg->l1_desc)
		return -ENOMEM;

	return 0;
}

static int arm_smmu_init_strtab_linear(struct arm_smmu_device *smmu)
{
	void *strtab;
	u64 reg;
	u32 size;
	struct arm_smmu_strtab_cfg *cfg = &smmu->strtab_cfg;

	size = (1 << smmu->sid_bits) * (STRTAB_STE_DWORDS << 3);
	strtab = dmam_alloc_coherent(smmu->dev, size, &cfg->strtab_dma,
				     GFP_KERNEL);
	if (!strtab) {
		dev_err(smmu->dev,
			"failed to allocate linear stream table (%u bytes)\n",
			size);
		return -ENOMEM;
	}
	cfg->strtab = strtab;
	cfg->num_l1_ents = 1 << smmu->sid_bits;

	/* Configure strtab_base_cfg for a linear table covering all SIDs */
	reg  = FIELD_PREP(STRTAB_BASE_CFG_FMT, STRTAB_BASE_CFG_FMT_LINEAR);
	reg |= FIELD_PREP(STRTAB_BASE_CFG_LOG2SIZE, smmu->sid_bits);
	cfg->strtab_base_cfg = reg;

	arm_smmu_init_initial_stes(strtab, cfg->num_l1_ents);
	return 0;
}

static int arm_smmu_init_strtab(struct arm_smmu_device *smmu)
{
	u64 reg;
	int ret;

	if (smmu->features & ARM_SMMU_FEAT_2_LVL_STRTAB)
		ret = arm_smmu_init_strtab_2lvl(smmu);
	else
		ret = arm_smmu_init_strtab_linear(smmu);

	if (ret)
		return ret;

	/* Set the strtab base address */
	reg  = smmu->strtab_cfg.strtab_dma & STRTAB_BASE_ADDR_MASK;
	reg |= STRTAB_BASE_RA;
	smmu->strtab_cfg.strtab_base = reg;

	ida_init(&smmu->vmid_map);

	return 0;
}

static int arm_smmu_init_structures(struct arm_smmu_device *smmu)
{
	int ret;

	mutex_init(&smmu->streams_mutex);
	smmu->streams = RB_ROOT;

	ret = arm_smmu_init_queues(smmu);
	if (ret)
		return ret;

	return arm_smmu_init_strtab(smmu);
}

static int arm_smmu_write_reg_sync(struct arm_smmu_device *smmu, u32 val,
				   unsigned int reg_off, unsigned int ack_off)
{
	u32 reg;

	writel_relaxed(val, smmu->base + reg_off);
	return readl_relaxed_poll_timeout(smmu->base + ack_off, reg, reg == val,
					  1, ARM_SMMU_POLL_TIMEOUT_US);
}

/* GBPA is "special" */
static int arm_smmu_update_gbpa(struct arm_smmu_device *smmu, u32 set, u32 clr)
{
	int ret;
	u32 reg, __iomem *gbpa = smmu->base + ARM_SMMU_GBPA;

	ret = readl_relaxed_poll_timeout(gbpa, reg, !(reg & GBPA_UPDATE),
					 1, ARM_SMMU_POLL_TIMEOUT_US);
	if (ret)
		return ret;

	reg &= ~clr;
	reg |= set;
	writel_relaxed(reg | GBPA_UPDATE, gbpa);
	ret = readl_relaxed_poll_timeout(gbpa, reg, !(reg & GBPA_UPDATE),
					 1, ARM_SMMU_POLL_TIMEOUT_US);

	if (ret)
		dev_err(smmu->dev, "GBPA not responding to update\n");
	return ret;
}

static void arm_smmu_free_msis(void *data)
{
	struct device *dev = data;

	platform_device_msi_free_irqs_all(dev);
}

static void arm_smmu_write_msi_msg(struct msi_desc *desc, struct msi_msg *msg)
{
	phys_addr_t doorbell;
	struct device *dev = msi_desc_to_dev(desc);
	struct arm_smmu_device *smmu = dev_get_drvdata(dev);
	phys_addr_t *cfg = arm_smmu_msi_cfg[desc->msi_index];

	doorbell = (((u64)msg->address_hi) << 32) | msg->address_lo;
	doorbell &= MSI_CFG0_ADDR_MASK;

	writeq_relaxed(doorbell, smmu->base + cfg[0]);
	writel_relaxed(msg->data, smmu->base + cfg[1]);
	writel_relaxed(ARM_SMMU_MEMATTR_DEVICE_nGnRE, smmu->base + cfg[2]);
}

static void arm_smmu_setup_msis(struct arm_smmu_device *smmu)
{
	int ret, nvec = ARM_SMMU_MAX_MSIS;
	struct device *dev = smmu->dev;

	/* Clear the MSI address regs */
	writeq_relaxed(0, smmu->base + ARM_SMMU_GERROR_IRQ_CFG0);
	writeq_relaxed(0, smmu->base + ARM_SMMU_EVTQ_IRQ_CFG0);

	if (smmu->features & ARM_SMMU_FEAT_PRI)
		writeq_relaxed(0, smmu->base + ARM_SMMU_PRIQ_IRQ_CFG0);
	else
		nvec--;

	if (!(smmu->features & ARM_SMMU_FEAT_MSI))
		return;

	if (!dev->msi.domain) {
		dev_info(smmu->dev, "msi_domain absent - falling back to wired irqs\n");
		return;
	}

	/* Allocate MSIs for evtq, gerror and priq. Ignore cmdq */
	ret = platform_device_msi_init_and_alloc_irqs(dev, nvec, arm_smmu_write_msi_msg);
	if (ret) {
		dev_warn(dev, "failed to allocate MSIs - falling back to wired irqs\n");
		return;
	}

	smmu->evtq.q.irq = msi_get_virq(dev, EVTQ_MSI_INDEX);
	smmu->gerr_irq = msi_get_virq(dev, GERROR_MSI_INDEX);
	smmu->priq.q.irq = msi_get_virq(dev, PRIQ_MSI_INDEX);

	/* Add callback to free MSIs on teardown */
	devm_add_action_or_reset(dev, arm_smmu_free_msis, dev);
}

static void arm_smmu_setup_unique_irqs(struct arm_smmu_device *smmu)
{
	int irq, ret;

	arm_smmu_setup_msis(smmu);

	/* Request interrupt lines */
	irq = smmu->evtq.q.irq;
	if (irq) {
		ret = devm_request_threaded_irq(smmu->dev, irq, NULL,
						arm_smmu_evtq_thread,
						IRQF_ONESHOT,
						"arm-smmu-v3-evtq", smmu);
		if (ret < 0)
			dev_warn(smmu->dev, "failed to enable evtq irq\n");
	} else {
		dev_warn(smmu->dev, "no evtq irq - events will not be reported!\n");
	}

	irq = smmu->gerr_irq;
	if (irq) {
		ret = devm_request_irq(smmu->dev, irq, arm_smmu_gerror_handler,
				       0, "arm-smmu-v3-gerror", smmu);
		if (ret < 0)
			dev_warn(smmu->dev, "failed to enable gerror irq\n");
	} else {
		dev_warn(smmu->dev, "no gerr irq - errors will not be reported!\n");
	}

	if (smmu->features & ARM_SMMU_FEAT_PRI) {
		irq = smmu->priq.q.irq;
		if (irq) {
			ret = devm_request_threaded_irq(smmu->dev, irq, NULL,
							arm_smmu_priq_thread,
							IRQF_ONESHOT,
							"arm-smmu-v3-priq",
							smmu);
			if (ret < 0)
				dev_warn(smmu->dev,
					 "failed to enable priq irq\n");
		} else {
			dev_warn(smmu->dev, "no priq irq - PRI will be broken\n");
		}
	}
}

static int arm_smmu_setup_irqs(struct arm_smmu_device *smmu)
{
	int ret, irq;
	u32 irqen_flags = IRQ_CTRL_EVTQ_IRQEN | IRQ_CTRL_GERROR_IRQEN;

	/* Disable IRQs first */
	ret = arm_smmu_write_reg_sync(smmu, 0, ARM_SMMU_IRQ_CTRL,
				      ARM_SMMU_IRQ_CTRLACK);
	if (ret) {
		dev_err(smmu->dev, "failed to disable irqs\n");
		return ret;
	}

	irq = smmu->combined_irq;
	if (irq) {
		/*
		 * Cavium ThunderX2 implementation doesn't support unique irq
		 * lines. Use a single irq line for all the SMMUv3 interrupts.
		 */
		ret = devm_request_threaded_irq(smmu->dev, irq,
					arm_smmu_combined_irq_handler,
					arm_smmu_combined_irq_thread,
					IRQF_ONESHOT,
					"arm-smmu-v3-combined-irq", smmu);
		if (ret < 0)
			dev_warn(smmu->dev, "failed to enable combined irq\n");
	} else
		arm_smmu_setup_unique_irqs(smmu);

	if (smmu->features & ARM_SMMU_FEAT_PRI)
		irqen_flags |= IRQ_CTRL_PRIQ_IRQEN;

	/* Enable interrupt generation on the SMMU */
	ret = arm_smmu_write_reg_sync(smmu, irqen_flags,
				      ARM_SMMU_IRQ_CTRL, ARM_SMMU_IRQ_CTRLACK);
	if (ret)
		dev_warn(smmu->dev, "failed to enable irqs\n");

	return 0;
}

static int arm_smmu_device_disable(struct arm_smmu_device *smmu)
{
	int ret;

	ret = arm_smmu_write_reg_sync(smmu, 0, ARM_SMMU_CR0, ARM_SMMU_CR0ACK);
	if (ret)
		dev_err(smmu->dev, "failed to clear cr0\n");

	return ret;
}

static int arm_smmu_device_reset(struct arm_smmu_device *smmu)
{
	int ret;
	u32 reg, enables;
	struct arm_smmu_cmdq_ent cmd;

	/* Clear CR0 and sync (disables SMMU and queue processing) */
	reg = readl_relaxed(smmu->base + ARM_SMMU_CR0);
	if (reg & CR0_SMMUEN) {
		dev_warn(smmu->dev, "SMMU currently enabled! Resetting...\n");
		arm_smmu_update_gbpa(smmu, GBPA_ABORT, 0);
	}

	ret = arm_smmu_device_disable(smmu);
	if (ret)
		return ret;

	/* CR1 (table and queue memory attributes) */
	reg = FIELD_PREP(CR1_TABLE_SH, ARM_SMMU_SH_ISH) |
	      FIELD_PREP(CR1_TABLE_OC, CR1_CACHE_WB) |
	      FIELD_PREP(CR1_TABLE_IC, CR1_CACHE_WB) |
	      FIELD_PREP(CR1_QUEUE_SH, ARM_SMMU_SH_ISH) |
	      FIELD_PREP(CR1_QUEUE_OC, CR1_CACHE_WB) |
	      FIELD_PREP(CR1_QUEUE_IC, CR1_CACHE_WB);
	writel_relaxed(reg, smmu->base + ARM_SMMU_CR1);

	/* CR2 (random crap) */
	reg = CR2_PTM | CR2_RECINVSID;

	if (smmu->features & ARM_SMMU_FEAT_E2H)
		reg |= CR2_E2H;

	writel_relaxed(reg, smmu->base + ARM_SMMU_CR2);

	/* Stream table */
	writeq_relaxed(smmu->strtab_cfg.strtab_base,
		       smmu->base + ARM_SMMU_STRTAB_BASE);
	writel_relaxed(smmu->strtab_cfg.strtab_base_cfg,
		       smmu->base + ARM_SMMU_STRTAB_BASE_CFG);

	/* Command queue */
	writeq_relaxed(smmu->cmdq.q.q_base, smmu->base + ARM_SMMU_CMDQ_BASE);
	writel_relaxed(smmu->cmdq.q.llq.prod, smmu->base + ARM_SMMU_CMDQ_PROD);
	writel_relaxed(smmu->cmdq.q.llq.cons, smmu->base + ARM_SMMU_CMDQ_CONS);

	enables = CR0_CMDQEN;
	ret = arm_smmu_write_reg_sync(smmu, enables, ARM_SMMU_CR0,
				      ARM_SMMU_CR0ACK);
	if (ret) {
		dev_err(smmu->dev, "failed to enable command queue\n");
		return ret;
	}

	/* Invalidate any cached configuration */
	cmd.opcode = CMDQ_OP_CFGI_ALL;
	arm_smmu_cmdq_issue_cmd_with_sync(smmu, &cmd);

	/* Invalidate any stale TLB entries */
	if (smmu->features & ARM_SMMU_FEAT_HYP) {
		cmd.opcode = CMDQ_OP_TLBI_EL2_ALL;
		arm_smmu_cmdq_issue_cmd_with_sync(smmu, &cmd);
	}

	cmd.opcode = CMDQ_OP_TLBI_NSNH_ALL;
	arm_smmu_cmdq_issue_cmd_with_sync(smmu, &cmd);

	/* Event queue */
	writeq_relaxed(smmu->evtq.q.q_base, smmu->base + ARM_SMMU_EVTQ_BASE);
	writel_relaxed(smmu->evtq.q.llq.prod, smmu->page1 + ARM_SMMU_EVTQ_PROD);
	writel_relaxed(smmu->evtq.q.llq.cons, smmu->page1 + ARM_SMMU_EVTQ_CONS);

	enables |= CR0_EVTQEN;
	ret = arm_smmu_write_reg_sync(smmu, enables, ARM_SMMU_CR0,
				      ARM_SMMU_CR0ACK);
	if (ret) {
		dev_err(smmu->dev, "failed to enable event queue\n");
		return ret;
	}

	/* PRI queue */
	if (smmu->features & ARM_SMMU_FEAT_PRI) {
		writeq_relaxed(smmu->priq.q.q_base,
			       smmu->base + ARM_SMMU_PRIQ_BASE);
		writel_relaxed(smmu->priq.q.llq.prod,
			       smmu->page1 + ARM_SMMU_PRIQ_PROD);
		writel_relaxed(smmu->priq.q.llq.cons,
			       smmu->page1 + ARM_SMMU_PRIQ_CONS);

		enables |= CR0_PRIQEN;
		ret = arm_smmu_write_reg_sync(smmu, enables, ARM_SMMU_CR0,
					      ARM_SMMU_CR0ACK);
		if (ret) {
			dev_err(smmu->dev, "failed to enable PRI queue\n");
			return ret;
		}
	}

	if (smmu->features & ARM_SMMU_FEAT_ATS) {
		enables |= CR0_ATSCHK;
		ret = arm_smmu_write_reg_sync(smmu, enables, ARM_SMMU_CR0,
					      ARM_SMMU_CR0ACK);
		if (ret) {
			dev_err(smmu->dev, "failed to enable ATS check\n");
			return ret;
		}
	}

	ret = arm_smmu_setup_irqs(smmu);
	if (ret) {
		dev_err(smmu->dev, "failed to setup irqs\n");
		return ret;
	}

	if (is_kdump_kernel())
		enables &= ~(CR0_EVTQEN | CR0_PRIQEN);

	/* Enable the SMMU interface */
	enables |= CR0_SMMUEN;
	ret = arm_smmu_write_reg_sync(smmu, enables, ARM_SMMU_CR0,
				      ARM_SMMU_CR0ACK);
	if (ret) {
		dev_err(smmu->dev, "failed to enable SMMU interface\n");
		return ret;
	}

	return 0;
}

#define IIDR_IMPLEMENTER_ARM		0x43b
#define IIDR_PRODUCTID_ARM_MMU_600	0x483
#define IIDR_PRODUCTID_ARM_MMU_700	0x487

static void arm_smmu_device_iidr_probe(struct arm_smmu_device *smmu)
{
	u32 reg;
	unsigned int implementer, productid, variant, revision;

	reg = readl_relaxed(smmu->base + ARM_SMMU_IIDR);
	implementer = FIELD_GET(IIDR_IMPLEMENTER, reg);
	productid = FIELD_GET(IIDR_PRODUCTID, reg);
	variant = FIELD_GET(IIDR_VARIANT, reg);
	revision = FIELD_GET(IIDR_REVISION, reg);

	switch (implementer) {
	case IIDR_IMPLEMENTER_ARM:
		switch (productid) {
		case IIDR_PRODUCTID_ARM_MMU_600:
			/* Arm erratum 1076982 */
			if (variant == 0 && revision <= 2)
				smmu->features &= ~ARM_SMMU_FEAT_SEV;
			/* Arm erratum 1209401 */
			if (variant < 2)
				smmu->features &= ~ARM_SMMU_FEAT_NESTING;
			break;
		case IIDR_PRODUCTID_ARM_MMU_700:
			/* Arm erratum 2812531 */
			smmu->features &= ~ARM_SMMU_FEAT_BTM;
			smmu->options |= ARM_SMMU_OPT_CMDQ_FORCE_SYNC;
			/* Arm errata 2268618, 2812531 */
			smmu->features &= ~ARM_SMMU_FEAT_NESTING;
			break;
		}
		break;
	}
}

static void arm_smmu_get_httu(struct arm_smmu_device *smmu, u32 reg)
{
	u32 fw_features = smmu->features & (ARM_SMMU_FEAT_HA | ARM_SMMU_FEAT_HD);
	u32 hw_features = 0;

	switch (FIELD_GET(IDR0_HTTU, reg)) {
	case IDR0_HTTU_ACCESS_DIRTY:
		hw_features |= ARM_SMMU_FEAT_HD;
		fallthrough;
	case IDR0_HTTU_ACCESS:
		hw_features |= ARM_SMMU_FEAT_HA;
	}

	if (smmu->dev->of_node)
		smmu->features |= hw_features;
	else if (hw_features != fw_features)
		/* ACPI IORT sets the HTTU bits */
		dev_warn(smmu->dev,
			 "IDR0.HTTU features(0x%x) overridden by FW configuration (0x%x)\n",
			  hw_features, fw_features);
}

static int arm_smmu_device_hw_probe(struct arm_smmu_device *smmu)
{
	u32 reg;
	bool coherent = smmu->features & ARM_SMMU_FEAT_COHERENCY;

	/* IDR0 */
	reg = readl_relaxed(smmu->base + ARM_SMMU_IDR0);

	/* 2-level structures */
	if (FIELD_GET(IDR0_ST_LVL, reg) == IDR0_ST_LVL_2LVL)
		smmu->features |= ARM_SMMU_FEAT_2_LVL_STRTAB;

	if (reg & IDR0_CD2L)
		smmu->features |= ARM_SMMU_FEAT_2_LVL_CDTAB;

	/*
	 * Translation table endianness.
	 * We currently require the same endianness as the CPU, but this
	 * could be changed later by adding a new IO_PGTABLE_QUIRK.
	 */
	switch (FIELD_GET(IDR0_TTENDIAN, reg)) {
	case IDR0_TTENDIAN_MIXED:
		smmu->features |= ARM_SMMU_FEAT_TT_LE | ARM_SMMU_FEAT_TT_BE;
		break;
#ifdef __BIG_ENDIAN
	case IDR0_TTENDIAN_BE:
		smmu->features |= ARM_SMMU_FEAT_TT_BE;
		break;
#else
	case IDR0_TTENDIAN_LE:
		smmu->features |= ARM_SMMU_FEAT_TT_LE;
		break;
#endif
	default:
		dev_err(smmu->dev, "unknown/unsupported TT endianness!\n");
		return -ENXIO;
	}

	/* Boolean feature flags */
	if (IS_ENABLED(CONFIG_PCI_PRI) && reg & IDR0_PRI)
		smmu->features |= ARM_SMMU_FEAT_PRI;

	if (IS_ENABLED(CONFIG_PCI_ATS) && reg & IDR0_ATS)
		smmu->features |= ARM_SMMU_FEAT_ATS;

	if (reg & IDR0_SEV)
		smmu->features |= ARM_SMMU_FEAT_SEV;

	if (reg & IDR0_MSI) {
		smmu->features |= ARM_SMMU_FEAT_MSI;
		if (coherent && !disable_msipolling)
			smmu->options |= ARM_SMMU_OPT_MSIPOLL;
	}

	if (reg & IDR0_HYP) {
		smmu->features |= ARM_SMMU_FEAT_HYP;
		if (cpus_have_cap(ARM64_HAS_VIRT_HOST_EXTN))
			smmu->features |= ARM_SMMU_FEAT_E2H;
	}

	arm_smmu_get_httu(smmu, reg);

	/*
	 * The coherency feature as set by FW is used in preference to the ID
	 * register, but warn on mismatch.
	 */
	if (!!(reg & IDR0_COHACC) != coherent)
		dev_warn(smmu->dev, "IDR0.COHACC overridden by FW configuration (%s)\n",
			 coherent ? "true" : "false");

	switch (FIELD_GET(IDR0_STALL_MODEL, reg)) {
	case IDR0_STALL_MODEL_FORCE:
		smmu->features |= ARM_SMMU_FEAT_STALL_FORCE;
		fallthrough;
	case IDR0_STALL_MODEL_STALL:
		smmu->features |= ARM_SMMU_FEAT_STALLS;
	}

	if (reg & IDR0_S1P)
		smmu->features |= ARM_SMMU_FEAT_TRANS_S1;

	if (reg & IDR0_S2P)
		smmu->features |= ARM_SMMU_FEAT_TRANS_S2;

	if (!(reg & (IDR0_S1P | IDR0_S2P))) {
		dev_err(smmu->dev, "no translation support!\n");
		return -ENXIO;
	}

	/* We only support the AArch64 table format at present */
	switch (FIELD_GET(IDR0_TTF, reg)) {
	case IDR0_TTF_AARCH32_64:
		smmu->ias = 40;
		fallthrough;
	case IDR0_TTF_AARCH64:
		break;
	default:
		dev_err(smmu->dev, "AArch64 table format not supported!\n");
		return -ENXIO;
	}

	/* ASID/VMID sizes */
	smmu->asid_bits = reg & IDR0_ASID16 ? 16 : 8;
	smmu->vmid_bits = reg & IDR0_VMID16 ? 16 : 8;

	/* IDR1 */
	reg = readl_relaxed(smmu->base + ARM_SMMU_IDR1);
	if (reg & (IDR1_TABLES_PRESET | IDR1_QUEUES_PRESET | IDR1_REL)) {
		dev_err(smmu->dev, "embedded implementation not supported\n");
		return -ENXIO;
	}

	if (reg & IDR1_ATTR_TYPES_OVR)
		smmu->features |= ARM_SMMU_FEAT_ATTR_TYPES_OVR;

	/* Queue sizes, capped to ensure natural alignment */
	smmu->cmdq.q.llq.max_n_shift = min_t(u32, CMDQ_MAX_SZ_SHIFT,
					     FIELD_GET(IDR1_CMDQS, reg));
	if (smmu->cmdq.q.llq.max_n_shift <= ilog2(CMDQ_BATCH_ENTRIES)) {
		/*
		 * We don't support splitting up batches, so one batch of
		 * commands plus an extra sync needs to fit inside the command
		 * queue. There's also no way we can handle the weird alignment
		 * restrictions on the base pointer for a unit-length queue.
		 */
		dev_err(smmu->dev, "command queue size <= %d entries not supported\n",
			CMDQ_BATCH_ENTRIES);
		return -ENXIO;
	}

	smmu->evtq.q.llq.max_n_shift = min_t(u32, EVTQ_MAX_SZ_SHIFT,
					     FIELD_GET(IDR1_EVTQS, reg));
	smmu->priq.q.llq.max_n_shift = min_t(u32, PRIQ_MAX_SZ_SHIFT,
					     FIELD_GET(IDR1_PRIQS, reg));

	/* SID/SSID sizes */
	smmu->ssid_bits = FIELD_GET(IDR1_SSIDSIZE, reg);
	smmu->sid_bits = FIELD_GET(IDR1_SIDSIZE, reg);
	smmu->iommu.max_pasids = 1UL << smmu->ssid_bits;

	/*
	 * If the SMMU supports fewer bits than would fill a single L2 stream
	 * table, use a linear table instead.
	 */
	if (smmu->sid_bits <= STRTAB_SPLIT)
		smmu->features &= ~ARM_SMMU_FEAT_2_LVL_STRTAB;

	/* IDR3 */
	reg = readl_relaxed(smmu->base + ARM_SMMU_IDR3);
	if (FIELD_GET(IDR3_RIL, reg))
		smmu->features |= ARM_SMMU_FEAT_RANGE_INV;

	/* IDR5 */
	reg = readl_relaxed(smmu->base + ARM_SMMU_IDR5);

	/* Maximum number of outstanding stalls */
	smmu->evtq.max_stalls = FIELD_GET(IDR5_STALL_MAX, reg);

	/* Page sizes */
	if (reg & IDR5_GRAN64K)
		smmu->pgsize_bitmap |= SZ_64K | SZ_512M;
	if (reg & IDR5_GRAN16K)
		smmu->pgsize_bitmap |= SZ_16K | SZ_32M;
	if (reg & IDR5_GRAN4K)
		smmu->pgsize_bitmap |= SZ_4K | SZ_2M | SZ_1G;

	/* Input address size */
	if (FIELD_GET(IDR5_VAX, reg) == IDR5_VAX_52_BIT)
		smmu->features |= ARM_SMMU_FEAT_VAX;

	/* Output address size */
	switch (FIELD_GET(IDR5_OAS, reg)) {
	case IDR5_OAS_32_BIT:
		smmu->oas = 32;
		break;
	case IDR5_OAS_36_BIT:
		smmu->oas = 36;
		break;
	case IDR5_OAS_40_BIT:
		smmu->oas = 40;
		break;
	case IDR5_OAS_42_BIT:
		smmu->oas = 42;
		break;
	case IDR5_OAS_44_BIT:
		smmu->oas = 44;
		break;
	case IDR5_OAS_52_BIT:
		smmu->oas = 52;
		smmu->pgsize_bitmap |= 1ULL << 42; /* 4TB */
		break;
	default:
		dev_info(smmu->dev,
			"unknown output address size. Truncating to 48-bit\n");
		fallthrough;
	case IDR5_OAS_48_BIT:
		smmu->oas = 48;
	}

	if (arm_smmu_ops.pgsize_bitmap == -1UL)
		arm_smmu_ops.pgsize_bitmap = smmu->pgsize_bitmap;
	else
		arm_smmu_ops.pgsize_bitmap |= smmu->pgsize_bitmap;

	/* Set the DMA mask for our table walker */
	if (dma_set_mask_and_coherent(smmu->dev, DMA_BIT_MASK(smmu->oas)))
		dev_warn(smmu->dev,
			 "failed to set DMA mask for table walker\n");

	smmu->ias = max(smmu->ias, smmu->oas);

	if ((smmu->features & ARM_SMMU_FEAT_TRANS_S1) &&
	    (smmu->features & ARM_SMMU_FEAT_TRANS_S2))
		smmu->features |= ARM_SMMU_FEAT_NESTING;

	arm_smmu_device_iidr_probe(smmu);

	if (arm_smmu_sva_supported(smmu))
		smmu->features |= ARM_SMMU_FEAT_SVA;

	dev_info(smmu->dev, "ias %lu-bit, oas %lu-bit (features 0x%08x)\n",
		 smmu->ias, smmu->oas, smmu->features);
	return 0;
}

#ifdef CONFIG_ACPI
static void acpi_smmu_get_options(u32 model, struct arm_smmu_device *smmu)
{
	switch (model) {
	case ACPI_IORT_SMMU_V3_CAVIUM_CN99XX:
		smmu->options |= ARM_SMMU_OPT_PAGE0_REGS_ONLY;
		break;
	case ACPI_IORT_SMMU_V3_HISILICON_HI161X:
		smmu->options |= ARM_SMMU_OPT_SKIP_PREFETCH;
		break;
	}

	dev_notice(smmu->dev, "option mask 0x%x\n", smmu->options);
}

static int arm_smmu_device_acpi_probe(struct platform_device *pdev,
				      struct arm_smmu_device *smmu)
{
	struct acpi_iort_smmu_v3 *iort_smmu;
	struct device *dev = smmu->dev;
	struct acpi_iort_node *node;

	node = *(struct acpi_iort_node **)dev_get_platdata(dev);

	/* Retrieve SMMUv3 specific data */
	iort_smmu = (struct acpi_iort_smmu_v3 *)node->node_data;

	acpi_smmu_get_options(iort_smmu->model, smmu);

	if (iort_smmu->flags & ACPI_IORT_SMMU_V3_COHACC_OVERRIDE)
		smmu->features |= ARM_SMMU_FEAT_COHERENCY;

	switch (FIELD_GET(ACPI_IORT_SMMU_V3_HTTU_OVERRIDE, iort_smmu->flags)) {
	case IDR0_HTTU_ACCESS_DIRTY:
		smmu->features |= ARM_SMMU_FEAT_HD;
		fallthrough;
	case IDR0_HTTU_ACCESS:
		smmu->features |= ARM_SMMU_FEAT_HA;
	}

	return 0;
}
#else
static inline int arm_smmu_device_acpi_probe(struct platform_device *pdev,
					     struct arm_smmu_device *smmu)
{
	return -ENODEV;
}
#endif

static int arm_smmu_device_dt_probe(struct platform_device *pdev,
				    struct arm_smmu_device *smmu)
{
	struct device *dev = &pdev->dev;
	u32 cells;
	int ret = -EINVAL;

	if (of_property_read_u32(dev->of_node, "#iommu-cells", &cells))
		dev_err(dev, "missing #iommu-cells property\n");
	else if (cells != 1)
		dev_err(dev, "invalid #iommu-cells value (%d)\n", cells);
	else
		ret = 0;

	parse_driver_options(smmu);

	if (of_dma_is_coherent(dev->of_node))
		smmu->features |= ARM_SMMU_FEAT_COHERENCY;

	return ret;
}

static unsigned long arm_smmu_resource_size(struct arm_smmu_device *smmu)
{
	if (smmu->options & ARM_SMMU_OPT_PAGE0_REGS_ONLY)
		return SZ_64K;
	else
		return SZ_128K;
}

static void __iomem *arm_smmu_ioremap(struct device *dev, resource_size_t start,
				      resource_size_t size)
{
	struct resource res = DEFINE_RES_MEM(start, size);

	return devm_ioremap_resource(dev, &res);
}

static void arm_smmu_rmr_install_bypass_ste(struct arm_smmu_device *smmu)
{
	struct list_head rmr_list;
	struct iommu_resv_region *e;

	INIT_LIST_HEAD(&rmr_list);
	iort_get_rmr_sids(dev_fwnode(smmu->dev), &rmr_list);

	list_for_each_entry(e, &rmr_list, list) {
		struct iommu_iort_rmr_data *rmr;
		int ret, i;

		rmr = container_of(e, struct iommu_iort_rmr_data, rr);
		for (i = 0; i < rmr->num_sids; i++) {
			ret = arm_smmu_init_sid_strtab(smmu, rmr->sids[i]);
			if (ret) {
				dev_err(smmu->dev, "RMR SID(0x%x) bypass failed\n",
					rmr->sids[i]);
				continue;
			}

			/*
			 * STE table is not programmed to HW, see
			 * arm_smmu_initial_bypass_stes()
			 */
			arm_smmu_make_bypass_ste(smmu,
				arm_smmu_get_step_for_sid(smmu, rmr->sids[i]));
		}
	}

	iort_put_rmr_sids(dev_fwnode(smmu->dev), &rmr_list);
}

static int arm_smmu_device_probe(struct platform_device *pdev)
{
	int irq, ret;
	struct resource *res;
	resource_size_t ioaddr;
	struct arm_smmu_device *smmu;
	struct device *dev = &pdev->dev;

	smmu = devm_kzalloc(dev, sizeof(*smmu), GFP_KERNEL);
	if (!smmu)
		return -ENOMEM;
	smmu->dev = dev;

	if (dev->of_node) {
		ret = arm_smmu_device_dt_probe(pdev, smmu);
	} else {
		ret = arm_smmu_device_acpi_probe(pdev, smmu);
	}
	if (ret)
		return ret;

	/* Base address */
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -EINVAL;
	if (resource_size(res) < arm_smmu_resource_size(smmu)) {
		dev_err(dev, "MMIO region too small (%pr)\n", res);
		return -EINVAL;
	}
	ioaddr = res->start;

	/*
	 * Don't map the IMPLEMENTATION DEFINED regions, since they may contain
	 * the PMCG registers which are reserved by the PMU driver.
	 */
	smmu->base = arm_smmu_ioremap(dev, ioaddr, ARM_SMMU_REG_SZ);
	if (IS_ERR(smmu->base))
		return PTR_ERR(smmu->base);

	if (arm_smmu_resource_size(smmu) > SZ_64K) {
		smmu->page1 = arm_smmu_ioremap(dev, ioaddr + SZ_64K,
					       ARM_SMMU_REG_SZ);
		if (IS_ERR(smmu->page1))
			return PTR_ERR(smmu->page1);
	} else {
		smmu->page1 = smmu->base;
	}

	/* Interrupt lines */

	irq = platform_get_irq_byname_optional(pdev, "combined");
	if (irq > 0)
		smmu->combined_irq = irq;
	else {
		irq = platform_get_irq_byname_optional(pdev, "eventq");
		if (irq > 0)
			smmu->evtq.q.irq = irq;

		irq = platform_get_irq_byname_optional(pdev, "priq");
		if (irq > 0)
			smmu->priq.q.irq = irq;

		irq = platform_get_irq_byname_optional(pdev, "gerror");
		if (irq > 0)
			smmu->gerr_irq = irq;
	}
	/* Probe the h/w */
	ret = arm_smmu_device_hw_probe(smmu);
	if (ret)
		return ret;

	/* Initialise in-memory data structures */
	ret = arm_smmu_init_structures(smmu);
	if (ret)
		return ret;

	/* Record our private device structure */
	platform_set_drvdata(pdev, smmu);

	/* Check for RMRs and install bypass STEs if any */
	arm_smmu_rmr_install_bypass_ste(smmu);

	/* Reset the device */
	ret = arm_smmu_device_reset(smmu);
	if (ret)
		return ret;

	/* And we're up. Go go go! */
	ret = iommu_device_sysfs_add(&smmu->iommu, dev, NULL,
				     "smmu3.%pa", &ioaddr);
	if (ret)
		return ret;

	ret = iommu_device_register(&smmu->iommu, &arm_smmu_ops, dev);
	if (ret) {
		dev_err(dev, "Failed to register iommu\n");
		iommu_device_sysfs_remove(&smmu->iommu);
		return ret;
	}

	return 0;
}

static void arm_smmu_device_remove(struct platform_device *pdev)
{
	struct arm_smmu_device *smmu = platform_get_drvdata(pdev);

	iommu_device_unregister(&smmu->iommu);
	iommu_device_sysfs_remove(&smmu->iommu);
	arm_smmu_device_disable(smmu);
	iopf_queue_free(smmu->evtq.iopf);
	ida_destroy(&smmu->vmid_map);
}

static void arm_smmu_device_shutdown(struct platform_device *pdev)
{
	struct arm_smmu_device *smmu = platform_get_drvdata(pdev);

	arm_smmu_device_disable(smmu);
}

static const struct of_device_id arm_smmu_of_match[] = {
	{ .compatible = "arm,smmu-v3", },
	{ },
};
MODULE_DEVICE_TABLE(of, arm_smmu_of_match);

static void arm_smmu_driver_unregister(struct platform_driver *drv)
{
	arm_smmu_sva_notifier_synchronize();
	platform_driver_unregister(drv);
}

static struct platform_driver arm_smmu_driver = {
	.driver	= {
		.name			= "arm-smmu-v3",
		.of_match_table		= arm_smmu_of_match,
		.suppress_bind_attrs	= true,
	},
	.probe	= arm_smmu_device_probe,
	.remove_new = arm_smmu_device_remove,
	.shutdown = arm_smmu_device_shutdown,
};
module_driver(arm_smmu_driver, platform_driver_register,
	      arm_smmu_driver_unregister);

MODULE_DESCRIPTION("IOMMU API for ARM architected SMMUv3 implementations");
MODULE_AUTHOR("Will Deacon <will@kernel.org>");
MODULE_ALIAS("platform:arm-smmu-v3");
MODULE_LICENSE("GPL v2");
