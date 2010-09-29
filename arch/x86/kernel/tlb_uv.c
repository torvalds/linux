/*
 *	SGI UltraViolet TLB flush routines.
 *
 *	(c) 2008-2010 Cliff Wickman <cpw@sgi.com>, SGI.
 *
 *	This code is released under the GNU General Public License version 2 or
 *	later.
 */
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/slab.h>

#include <asm/mmu_context.h>
#include <asm/uv/uv.h>
#include <asm/uv/uv_mmrs.h>
#include <asm/uv/uv_hub.h>
#include <asm/uv/uv_bau.h>
#include <asm/apic.h>
#include <asm/idle.h>
#include <asm/tsc.h>
#include <asm/irq_vectors.h>
#include <asm/timer.h>

/* timeouts in nanoseconds (indexed by UVH_AGING_PRESCALE_SEL urgency7 30:28) */
static int timeout_base_ns[] = {
		20,
		160,
		1280,
		10240,
		81920,
		655360,
		5242880,
		167772160
};
static int timeout_us;
static int nobau;
static int baudisabled;
static spinlock_t disable_lock;
static cycles_t congested_cycles;

/* tunables: */
static int max_bau_concurrent = MAX_BAU_CONCURRENT;
static int max_bau_concurrent_constant = MAX_BAU_CONCURRENT;
static int plugged_delay = PLUGGED_DELAY;
static int plugsb4reset = PLUGSB4RESET;
static int timeoutsb4reset = TIMEOUTSB4RESET;
static int ipi_reset_limit = IPI_RESET_LIMIT;
static int complete_threshold = COMPLETE_THRESHOLD;
static int congested_response_us = CONGESTED_RESPONSE_US;
static int congested_reps = CONGESTED_REPS;
static int congested_period = CONGESTED_PERIOD;
static struct dentry *tunables_dir;
static struct dentry *tunables_file;

static int __init setup_nobau(char *arg)
{
	nobau = 1;
	return 0;
}
early_param("nobau", setup_nobau);

/* base pnode in this partition */
static int uv_partition_base_pnode __read_mostly;
/* position of pnode (which is nasid>>1): */
static int uv_nshift __read_mostly;
static unsigned long uv_mmask __read_mostly;

static DEFINE_PER_CPU(struct ptc_stats, ptcstats);
static DEFINE_PER_CPU(struct bau_control, bau_control);
static DEFINE_PER_CPU(cpumask_var_t, uv_flush_tlb_mask);

/*
 * Determine the first node on a uvhub. 'Nodes' are used for kernel
 * memory allocation.
 */
static int __init uvhub_to_first_node(int uvhub)
{
	int node, b;

	for_each_online_node(node) {
		b = uv_node_to_blade_id(node);
		if (uvhub == b)
			return node;
	}
	return -1;
}

/*
 * Determine the apicid of the first cpu on a uvhub.
 */
static int __init uvhub_to_first_apicid(int uvhub)
{
	int cpu;

	for_each_present_cpu(cpu)
		if (uvhub == uv_cpu_to_blade_id(cpu))
			return per_cpu(x86_cpu_to_apicid, cpu);
	return -1;
}

/*
 * Free a software acknowledge hardware resource by clearing its Pending
 * bit. This will return a reply to the sender.
 * If the message has timed out, a reply has already been sent by the
 * hardware but the resource has not been released. In that case our
 * clear of the Timeout bit (as well) will free the resource. No reply will
 * be sent (the hardware will only do one reply per message).
 */
static inline void uv_reply_to_message(struct msg_desc *mdp,
				       struct bau_control *bcp)
{
	unsigned long dw;
	struct bau_payload_queue_entry *msg;

	msg = mdp->msg;
	if (!msg->canceled) {
		dw = (msg->sw_ack_vector << UV_SW_ACK_NPENDING) |
						msg->sw_ack_vector;
		uv_write_local_mmr(
				UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_ALIAS, dw);
	}
	msg->replied_to = 1;
	msg->sw_ack_vector = 0;
}

/*
 * Process the receipt of a RETRY message
 */
static inline void uv_bau_process_retry_msg(struct msg_desc *mdp,
					    struct bau_control *bcp)
{
	int i;
	int cancel_count = 0;
	int slot2;
	unsigned long msg_res;
	unsigned long mmr = 0;
	struct bau_payload_queue_entry *msg;
	struct bau_payload_queue_entry *msg2;
	struct ptc_stats *stat;

	msg = mdp->msg;
	stat = bcp->statp;
	stat->d_retries++;
	/*
	 * cancel any message from msg+1 to the retry itself
	 */
	for (msg2 = msg+1, i = 0; i < DEST_Q_SIZE; msg2++, i++) {
		if (msg2 > mdp->va_queue_last)
			msg2 = mdp->va_queue_first;
		if (msg2 == msg)
			break;

		/* same conditions for cancellation as uv_do_reset */
		if ((msg2->replied_to == 0) && (msg2->canceled == 0) &&
		    (msg2->sw_ack_vector) && ((msg2->sw_ack_vector &
			msg->sw_ack_vector) == 0) &&
		    (msg2->sending_cpu == msg->sending_cpu) &&
		    (msg2->msg_type != MSG_NOOP)) {
			slot2 = msg2 - mdp->va_queue_first;
			mmr = uv_read_local_mmr
				(UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE);
			msg_res = msg2->sw_ack_vector;
			/*
			 * This is a message retry; clear the resources held
			 * by the previous message only if they timed out.
			 * If it has not timed out we have an unexpected
			 * situation to report.
			 */
			if (mmr & (msg_res << UV_SW_ACK_NPENDING)) {
				/*
				 * is the resource timed out?
				 * make everyone ignore the cancelled message.
				 */
				msg2->canceled = 1;
				stat->d_canceled++;
				cancel_count++;
				uv_write_local_mmr(
				    UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_ALIAS,
					(msg_res << UV_SW_ACK_NPENDING) |
					 msg_res);
			}
		}
	}
	if (!cancel_count)
		stat->d_nocanceled++;
}

/*
 * Do all the things a cpu should do for a TLB shootdown message.
 * Other cpu's may come here at the same time for this message.
 */
static void uv_bau_process_message(struct msg_desc *mdp,
				   struct bau_control *bcp)
{
	int msg_ack_count;
	short socket_ack_count = 0;
	struct ptc_stats *stat;
	struct bau_payload_queue_entry *msg;
	struct bau_control *smaster = bcp->socket_master;

	/*
	 * This must be a normal message, or retry of a normal message
	 */
	msg = mdp->msg;
	stat = bcp->statp;
	if (msg->address == TLB_FLUSH_ALL) {
		local_flush_tlb();
		stat->d_alltlb++;
	} else {
		__flush_tlb_one(msg->address);
		stat->d_onetlb++;
	}
	stat->d_requestee++;

	/*
	 * One cpu on each uvhub has the additional job on a RETRY
	 * of releasing the resource held by the message that is
	 * being retried.  That message is identified by sending
	 * cpu number.
	 */
	if (msg->msg_type == MSG_RETRY && bcp == bcp->uvhub_master)
		uv_bau_process_retry_msg(mdp, bcp);

	/*
	 * This is a sw_ack message, so we have to reply to it.
	 * Count each responding cpu on the socket. This avoids
	 * pinging the count's cache line back and forth between
	 * the sockets.
	 */
	socket_ack_count = atomic_add_short_return(1, (struct atomic_short *)
			&smaster->socket_acknowledge_count[mdp->msg_slot]);
	if (socket_ack_count == bcp->cpus_in_socket) {
		/*
		 * Both sockets dump their completed count total into
		 * the message's count.
		 */
		smaster->socket_acknowledge_count[mdp->msg_slot] = 0;
		msg_ack_count = atomic_add_short_return(socket_ack_count,
				(struct atomic_short *)&msg->acknowledge_count);

		if (msg_ack_count == bcp->cpus_in_uvhub) {
			/*
			 * All cpus in uvhub saw it; reply
			 */
			uv_reply_to_message(mdp, bcp);
		}
	}

	return;
}

/*
 * Determine the first cpu on a uvhub.
 */
static int uvhub_to_first_cpu(int uvhub)
{
	int cpu;
	for_each_present_cpu(cpu)
		if (uvhub == uv_cpu_to_blade_id(cpu))
			return cpu;
	return -1;
}

/*
 * Last resort when we get a large number of destination timeouts is
 * to clear resources held by a given cpu.
 * Do this with IPI so that all messages in the BAU message queue
 * can be identified by their nonzero sw_ack_vector field.
 *
 * This is entered for a single cpu on the uvhub.
 * The sender want's this uvhub to free a specific message's
 * sw_ack resources.
 */
static void
uv_do_reset(void *ptr)
{
	int i;
	int slot;
	int count = 0;
	unsigned long mmr;
	unsigned long msg_res;
	struct bau_control *bcp;
	struct reset_args *rap;
	struct bau_payload_queue_entry *msg;
	struct ptc_stats *stat;

	bcp = &per_cpu(bau_control, smp_processor_id());
	rap = (struct reset_args *)ptr;
	stat = bcp->statp;
	stat->d_resets++;

	/*
	 * We're looking for the given sender, and
	 * will free its sw_ack resource.
	 * If all cpu's finally responded after the timeout, its
	 * message 'replied_to' was set.
	 */
	for (msg = bcp->va_queue_first, i = 0; i < DEST_Q_SIZE; msg++, i++) {
		/* uv_do_reset: same conditions for cancellation as
		   uv_bau_process_retry_msg() */
		if ((msg->replied_to == 0) &&
		    (msg->canceled == 0) &&
		    (msg->sending_cpu == rap->sender) &&
		    (msg->sw_ack_vector) &&
		    (msg->msg_type != MSG_NOOP)) {
			/*
			 * make everyone else ignore this message
			 */
			msg->canceled = 1;
			slot = msg - bcp->va_queue_first;
			count++;
			/*
			 * only reset the resource if it is still pending
			 */
			mmr = uv_read_local_mmr
					(UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE);
			msg_res = msg->sw_ack_vector;
			if (mmr & msg_res) {
				stat->d_rcanceled++;
				uv_write_local_mmr(
				    UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_ALIAS,
					(msg_res << UV_SW_ACK_NPENDING) |
					 msg_res);
			}
		}
	}
	return;
}

/*
 * Use IPI to get all target uvhubs to release resources held by
 * a given sending cpu number.
 */
static void uv_reset_with_ipi(struct bau_target_uvhubmask *distribution,
			      int sender)
{
	int uvhub;
	int cpu;
	cpumask_t mask;
	struct reset_args reset_args;

	reset_args.sender = sender;

	cpus_clear(mask);
	/* find a single cpu for each uvhub in this distribution mask */
	for (uvhub = 0;
		    uvhub < sizeof(struct bau_target_uvhubmask) * BITSPERBYTE;
		    uvhub++) {
		if (!bau_uvhub_isset(uvhub, distribution))
			continue;
		/* find a cpu for this uvhub */
		cpu = uvhub_to_first_cpu(uvhub);
		cpu_set(cpu, mask);
	}
	/* IPI all cpus; Preemption is already disabled */
	smp_call_function_many(&mask, uv_do_reset, (void *)&reset_args, 1);
	return;
}

static inline unsigned long
cycles_2_us(unsigned long long cyc)
{
	unsigned long long ns;
	unsigned long us;
	ns =  (cyc * per_cpu(cyc2ns, smp_processor_id()))
						>> CYC2NS_SCALE_FACTOR;
	us = ns / 1000;
	return us;
}

/*
 * wait for all cpus on this hub to finish their sends and go quiet
 * leaves uvhub_quiesce set so that no new broadcasts are started by
 * bau_flush_send_and_wait()
 */
static inline void
quiesce_local_uvhub(struct bau_control *hmaster)
{
	atomic_add_short_return(1, (struct atomic_short *)
		 &hmaster->uvhub_quiesce);
}

/*
 * mark this quiet-requestor as done
 */
static inline void
end_uvhub_quiesce(struct bau_control *hmaster)
{
	atomic_add_short_return(-1, (struct atomic_short *)
		&hmaster->uvhub_quiesce);
}

/*
 * Wait for completion of a broadcast software ack message
 * return COMPLETE, RETRY(PLUGGED or TIMEOUT) or GIVEUP
 */
static int uv_wait_completion(struct bau_desc *bau_desc,
	unsigned long mmr_offset, int right_shift, int this_cpu,
	struct bau_control *bcp, struct bau_control *smaster, long try)
{
	unsigned long descriptor_status;
	cycles_t ttime;
	struct ptc_stats *stat = bcp->statp;
	struct bau_control *hmaster;

	hmaster = bcp->uvhub_master;

	/* spin on the status MMR, waiting for it to go idle */
	while ((descriptor_status = (((unsigned long)
		uv_read_local_mmr(mmr_offset) >>
			right_shift) & UV_ACT_STATUS_MASK)) !=
			DESC_STATUS_IDLE) {
		/*
		 * Our software ack messages may be blocked because there are
		 * no swack resources available.  As long as none of them
		 * has timed out hardware will NACK our message and its
		 * state will stay IDLE.
		 */
		if (descriptor_status == DESC_STATUS_SOURCE_TIMEOUT) {
			stat->s_stimeout++;
			return FLUSH_GIVEUP;
		} else if (descriptor_status ==
					DESC_STATUS_DESTINATION_TIMEOUT) {
			stat->s_dtimeout++;
			ttime = get_cycles();

			/*
			 * Our retries may be blocked by all destination
			 * swack resources being consumed, and a timeout
			 * pending.  In that case hardware returns the
			 * ERROR that looks like a destination timeout.
			 */
			if (cycles_2_us(ttime - bcp->send_message) <
							timeout_us) {
				bcp->conseccompletes = 0;
				return FLUSH_RETRY_PLUGGED;
			}

			bcp->conseccompletes = 0;
			return FLUSH_RETRY_TIMEOUT;
		} else {
			/*
			 * descriptor_status is still BUSY
			 */
			cpu_relax();
		}
	}
	bcp->conseccompletes++;
	return FLUSH_COMPLETE;
}

static inline cycles_t
sec_2_cycles(unsigned long sec)
{
	unsigned long ns;
	cycles_t cyc;

	ns = sec * 1000000000;
	cyc = (ns << CYC2NS_SCALE_FACTOR)/(per_cpu(cyc2ns, smp_processor_id()));
	return cyc;
}

/*
 * conditionally add 1 to *v, unless *v is >= u
 * return 0 if we cannot add 1 to *v because it is >= u
 * return 1 if we can add 1 to *v because it is < u
 * the add is atomic
 *
 * This is close to atomic_add_unless(), but this allows the 'u' value
 * to be lowered below the current 'v'.  atomic_add_unless can only stop
 * on equal.
 */
static inline int atomic_inc_unless_ge(spinlock_t *lock, atomic_t *v, int u)
{
	spin_lock(lock);
	if (atomic_read(v) >= u) {
		spin_unlock(lock);
		return 0;
	}
	atomic_inc(v);
	spin_unlock(lock);
	return 1;
}

/*
 * Our retries are blocked by all destination swack resources being
 * in use, and a timeout is pending. In that case hardware immediately
 * returns the ERROR that looks like a destination timeout.
 */
static void
destination_plugged(struct bau_desc *bau_desc, struct bau_control *bcp,
			struct bau_control *hmaster, struct ptc_stats *stat)
{
	udelay(bcp->plugged_delay);
	bcp->plugged_tries++;
	if (bcp->plugged_tries >= bcp->plugsb4reset) {
		bcp->plugged_tries = 0;
		quiesce_local_uvhub(hmaster);
		spin_lock(&hmaster->queue_lock);
		uv_reset_with_ipi(&bau_desc->distribution, bcp->cpu);
		spin_unlock(&hmaster->queue_lock);
		end_uvhub_quiesce(hmaster);
		bcp->ipi_attempts++;
		stat->s_resets_plug++;
	}
}

static void
destination_timeout(struct bau_desc *bau_desc, struct bau_control *bcp,
			struct bau_control *hmaster, struct ptc_stats *stat)
{
	hmaster->max_bau_concurrent = 1;
	bcp->timeout_tries++;
	if (bcp->timeout_tries >= bcp->timeoutsb4reset) {
		bcp->timeout_tries = 0;
		quiesce_local_uvhub(hmaster);
		spin_lock(&hmaster->queue_lock);
		uv_reset_with_ipi(&bau_desc->distribution, bcp->cpu);
		spin_unlock(&hmaster->queue_lock);
		end_uvhub_quiesce(hmaster);
		bcp->ipi_attempts++;
		stat->s_resets_timeout++;
	}
}

/*
 * Completions are taking a very long time due to a congested numalink
 * network.
 */
static void
disable_for_congestion(struct bau_control *bcp, struct ptc_stats *stat)
{
	int tcpu;
	struct bau_control *tbcp;

	/* let only one cpu do this disabling */
	spin_lock(&disable_lock);
	if (!baudisabled && bcp->period_requests &&
	    ((bcp->period_time / bcp->period_requests) > congested_cycles)) {
		/* it becomes this cpu's job to turn on the use of the
		   BAU again */
		baudisabled = 1;
		bcp->set_bau_off = 1;
		bcp->set_bau_on_time = get_cycles() +
			sec_2_cycles(bcp->congested_period);
		stat->s_bau_disabled++;
		for_each_present_cpu(tcpu) {
			tbcp = &per_cpu(bau_control, tcpu);
				tbcp->baudisabled = 1;
		}
	}
	spin_unlock(&disable_lock);
}

/**
 * uv_flush_send_and_wait
 *
 * Send a broadcast and wait for it to complete.
 *
 * The flush_mask contains the cpus the broadcast is to be sent to including
 * cpus that are on the local uvhub.
 *
 * Returns 0 if all flushing represented in the mask was done.
 * Returns 1 if it gives up entirely and the original cpu mask is to be
 * returned to the kernel.
 */
int uv_flush_send_and_wait(struct bau_desc *bau_desc,
			   struct cpumask *flush_mask, struct bau_control *bcp)
{
	int right_shift;
	int completion_status = 0;
	int seq_number = 0;
	long try = 0;
	int cpu = bcp->uvhub_cpu;
	int this_cpu = bcp->cpu;
	unsigned long mmr_offset;
	unsigned long index;
	cycles_t time1;
	cycles_t time2;
	cycles_t elapsed;
	struct ptc_stats *stat = bcp->statp;
	struct bau_control *smaster = bcp->socket_master;
	struct bau_control *hmaster = bcp->uvhub_master;

	if (!atomic_inc_unless_ge(&hmaster->uvhub_lock,
			&hmaster->active_descriptor_count,
			hmaster->max_bau_concurrent)) {
		stat->s_throttles++;
		do {
			cpu_relax();
		} while (!atomic_inc_unless_ge(&hmaster->uvhub_lock,
			&hmaster->active_descriptor_count,
			hmaster->max_bau_concurrent));
	}
	while (hmaster->uvhub_quiesce)
		cpu_relax();

	if (cpu < UV_CPUS_PER_ACT_STATUS) {
		mmr_offset = UVH_LB_BAU_SB_ACTIVATION_STATUS_0;
		right_shift = cpu * UV_ACT_STATUS_SIZE;
	} else {
		mmr_offset = UVH_LB_BAU_SB_ACTIVATION_STATUS_1;
		right_shift =
		    ((cpu - UV_CPUS_PER_ACT_STATUS) * UV_ACT_STATUS_SIZE);
	}
	time1 = get_cycles();
	do {
		if (try == 0) {
			bau_desc->header.msg_type = MSG_REGULAR;
			seq_number = bcp->message_number++;
		} else {
			bau_desc->header.msg_type = MSG_RETRY;
			stat->s_retry_messages++;
		}
		bau_desc->header.sequence = seq_number;
		index = (1UL << UVH_LB_BAU_SB_ACTIVATION_CONTROL_PUSH_SHFT) |
			bcp->uvhub_cpu;
		bcp->send_message = get_cycles();
		uv_write_local_mmr(UVH_LB_BAU_SB_ACTIVATION_CONTROL, index);
		try++;
		completion_status = uv_wait_completion(bau_desc, mmr_offset,
			right_shift, this_cpu, bcp, smaster, try);

		if (completion_status == FLUSH_RETRY_PLUGGED) {
			destination_plugged(bau_desc, bcp, hmaster, stat);
		} else if (completion_status == FLUSH_RETRY_TIMEOUT) {
			destination_timeout(bau_desc, bcp, hmaster, stat);
		}
		if (bcp->ipi_attempts >= bcp->ipi_reset_limit) {
			bcp->ipi_attempts = 0;
			completion_status = FLUSH_GIVEUP;
			break;
		}
		cpu_relax();
	} while ((completion_status == FLUSH_RETRY_PLUGGED) ||
		 (completion_status == FLUSH_RETRY_TIMEOUT));
	time2 = get_cycles();
	bcp->plugged_tries = 0;
	bcp->timeout_tries = 0;
	if ((completion_status == FLUSH_COMPLETE) &&
	    (bcp->conseccompletes > bcp->complete_threshold) &&
	    (hmaster->max_bau_concurrent <
					hmaster->max_bau_concurrent_constant))
			hmaster->max_bau_concurrent++;
	while (hmaster->uvhub_quiesce)
		cpu_relax();
	atomic_dec(&hmaster->active_descriptor_count);
	if (time2 > time1) {
		elapsed = time2 - time1;
		stat->s_time += elapsed;
		if ((completion_status == FLUSH_COMPLETE) && (try == 1)) {
			bcp->period_requests++;
			bcp->period_time += elapsed;
			if ((elapsed > congested_cycles) &&
			    (bcp->period_requests > bcp->congested_reps)) {
				disable_for_congestion(bcp, stat);
			}
		}
	} else
		stat->s_requestor--;
	if (completion_status == FLUSH_COMPLETE && try > 1)
		stat->s_retriesok++;
	else if (completion_status == FLUSH_GIVEUP) {
		stat->s_giveup++;
		return 1;
	}
	return 0;
}

/**
 * uv_flush_tlb_others - globally purge translation cache of a virtual
 * address or all TLB's
 * @cpumask: mask of all cpu's in which the address is to be removed
 * @mm: mm_struct containing virtual address range
 * @va: virtual address to be removed (or TLB_FLUSH_ALL for all TLB's on cpu)
 * @cpu: the current cpu
 *
 * This is the entry point for initiating any UV global TLB shootdown.
 *
 * Purges the translation caches of all specified processors of the given
 * virtual address, or purges all TLB's on specified processors.
 *
 * The caller has derived the cpumask from the mm_struct.  This function
 * is called only if there are bits set in the mask. (e.g. flush_tlb_page())
 *
 * The cpumask is converted into a uvhubmask of the uvhubs containing
 * those cpus.
 *
 * Note that this function should be called with preemption disabled.
 *
 * Returns NULL if all remote flushing was done.
 * Returns pointer to cpumask if some remote flushing remains to be
 * done.  The returned pointer is valid till preemption is re-enabled.
 */
const struct cpumask *uv_flush_tlb_others(const struct cpumask *cpumask,
					  struct mm_struct *mm,
					  unsigned long va, unsigned int cpu)
{
	int tcpu;
	int uvhub;
	int locals = 0;
	int remotes = 0;
	int hubs = 0;
	struct bau_desc *bau_desc;
	struct cpumask *flush_mask;
	struct ptc_stats *stat;
	struct bau_control *bcp;
	struct bau_control *tbcp;

	/* kernel was booted 'nobau' */
	if (nobau)
		return cpumask;

	bcp = &per_cpu(bau_control, cpu);
	stat = bcp->statp;

	/* bau was disabled due to slow response */
	if (bcp->baudisabled) {
		/* the cpu that disabled it must re-enable it */
		if (bcp->set_bau_off) {
			if (get_cycles() >= bcp->set_bau_on_time) {
				stat->s_bau_reenabled++;
				baudisabled = 0;
				for_each_present_cpu(tcpu) {
					tbcp = &per_cpu(bau_control, tcpu);
					tbcp->baudisabled = 0;
					tbcp->period_requests = 0;
					tbcp->period_time = 0;
				}
			}
		}
		return cpumask;
	}

	/*
	 * Each sending cpu has a per-cpu mask which it fills from the caller's
	 * cpu mask.  All cpus are converted to uvhubs and copied to the
	 * activation descriptor.
	 */
	flush_mask = (struct cpumask *)per_cpu(uv_flush_tlb_mask, cpu);
	/* don't actually do a shootdown of the local cpu */
	cpumask_andnot(flush_mask, cpumask, cpumask_of(cpu));
	if (cpu_isset(cpu, *cpumask))
		stat->s_ntargself++;

	bau_desc = bcp->descriptor_base;
	bau_desc += UV_ITEMS_PER_DESCRIPTOR * bcp->uvhub_cpu;
	bau_uvhubs_clear(&bau_desc->distribution, UV_DISTRIBUTION_SIZE);

	/* cpu statistics */
	for_each_cpu(tcpu, flush_mask) {
		uvhub = uv_cpu_to_blade_id(tcpu);
		bau_uvhub_set(uvhub, &bau_desc->distribution);
		if (uvhub == bcp->uvhub)
			locals++;
		else
			remotes++;
	}
	if ((locals + remotes) == 0)
		return NULL;
	stat->s_requestor++;
	stat->s_ntargcpu += remotes + locals;
	stat->s_ntargremotes += remotes;
	stat->s_ntarglocals += locals;
	remotes = bau_uvhub_weight(&bau_desc->distribution);

	/* uvhub statistics */
	hubs = bau_uvhub_weight(&bau_desc->distribution);
	if (locals) {
		stat->s_ntarglocaluvhub++;
		stat->s_ntargremoteuvhub += (hubs - 1);
	} else
		stat->s_ntargremoteuvhub += hubs;
	stat->s_ntarguvhub += hubs;
	if (hubs >= 16)
		stat->s_ntarguvhub16++;
	else if (hubs >= 8)
		stat->s_ntarguvhub8++;
	else if (hubs >= 4)
		stat->s_ntarguvhub4++;
	else if (hubs >= 2)
		stat->s_ntarguvhub2++;
	else
		stat->s_ntarguvhub1++;

	bau_desc->payload.address = va;
	bau_desc->payload.sending_cpu = cpu;

	/*
	 * uv_flush_send_and_wait returns 0 if all cpu's were messaged,
	 * or 1 if it gave up and the original cpumask should be returned.
	 */
	if (!uv_flush_send_and_wait(bau_desc, flush_mask, bcp))
		return NULL;
	else
		return cpumask;
}

/*
 * The BAU message interrupt comes here. (registered by set_intr_gate)
 * See entry_64.S
 *
 * We received a broadcast assist message.
 *
 * Interrupts are disabled; this interrupt could represent
 * the receipt of several messages.
 *
 * All cores/threads on this hub get this interrupt.
 * The last one to see it does the software ack.
 * (the resource will not be freed until noninterruptable cpus see this
 *  interrupt; hardware may timeout the s/w ack and reply ERROR)
 */
void uv_bau_message_interrupt(struct pt_regs *regs)
{
	int count = 0;
	cycles_t time_start;
	struct bau_payload_queue_entry *msg;
	struct bau_control *bcp;
	struct ptc_stats *stat;
	struct msg_desc msgdesc;

	time_start = get_cycles();
	bcp = &per_cpu(bau_control, smp_processor_id());
	stat = bcp->statp;
	msgdesc.va_queue_first = bcp->va_queue_first;
	msgdesc.va_queue_last = bcp->va_queue_last;
	msg = bcp->bau_msg_head;
	while (msg->sw_ack_vector) {
		count++;
		msgdesc.msg_slot = msg - msgdesc.va_queue_first;
		msgdesc.sw_ack_slot = ffs(msg->sw_ack_vector) - 1;
		msgdesc.msg = msg;
		uv_bau_process_message(&msgdesc, bcp);
		msg++;
		if (msg > msgdesc.va_queue_last)
			msg = msgdesc.va_queue_first;
		bcp->bau_msg_head = msg;
	}
	stat->d_time += (get_cycles() - time_start);
	if (!count)
		stat->d_nomsg++;
	else if (count > 1)
		stat->d_multmsg++;
	ack_APIC_irq();
}

/*
 * uv_enable_timeouts
 *
 * Each target uvhub (i.e. a uvhub that has no cpu's) needs to have
 * shootdown message timeouts enabled.  The timeout does not cause
 * an interrupt, but causes an error message to be returned to
 * the sender.
 */
static void uv_enable_timeouts(void)
{
	int uvhub;
	int nuvhubs;
	int pnode;
	unsigned long mmr_image;

	nuvhubs = uv_num_possible_blades();

	for (uvhub = 0; uvhub < nuvhubs; uvhub++) {
		if (!uv_blade_nr_possible_cpus(uvhub))
			continue;

		pnode = uv_blade_to_pnode(uvhub);
		mmr_image =
		    uv_read_global_mmr64(pnode, UVH_LB_BAU_MISC_CONTROL);
		/*
		 * Set the timeout period and then lock it in, in three
		 * steps; captures and locks in the period.
		 *
		 * To program the period, the SOFT_ACK_MODE must be off.
		 */
		mmr_image &= ~((unsigned long)1 <<
		    UVH_LB_BAU_MISC_CONTROL_ENABLE_INTD_SOFT_ACK_MODE_SHFT);
		uv_write_global_mmr64
		    (pnode, UVH_LB_BAU_MISC_CONTROL, mmr_image);
		/*
		 * Set the 4-bit period.
		 */
		mmr_image &= ~((unsigned long)0xf <<
		     UVH_LB_BAU_MISC_CONTROL_INTD_SOFT_ACK_TIMEOUT_PERIOD_SHFT);
		mmr_image |= (UV_INTD_SOFT_ACK_TIMEOUT_PERIOD <<
		     UVH_LB_BAU_MISC_CONTROL_INTD_SOFT_ACK_TIMEOUT_PERIOD_SHFT);
		uv_write_global_mmr64
		    (pnode, UVH_LB_BAU_MISC_CONTROL, mmr_image);
		/*
		 * Subsequent reversals of the timebase bit (3) cause an
		 * immediate timeout of one or all INTD resources as
		 * indicated in bits 2:0 (7 causes all of them to timeout).
		 */
		mmr_image |= ((unsigned long)1 <<
		    UVH_LB_BAU_MISC_CONTROL_ENABLE_INTD_SOFT_ACK_MODE_SHFT);
		uv_write_global_mmr64
		    (pnode, UVH_LB_BAU_MISC_CONTROL, mmr_image);
	}
}

static void *uv_ptc_seq_start(struct seq_file *file, loff_t *offset)
{
	if (*offset < num_possible_cpus())
		return offset;
	return NULL;
}

static void *uv_ptc_seq_next(struct seq_file *file, void *data, loff_t *offset)
{
	(*offset)++;
	if (*offset < num_possible_cpus())
		return offset;
	return NULL;
}

static void uv_ptc_seq_stop(struct seq_file *file, void *data)
{
}

static inline unsigned long long
microsec_2_cycles(unsigned long microsec)
{
	unsigned long ns;
	unsigned long long cyc;

	ns = microsec * 1000;
	cyc = (ns << CYC2NS_SCALE_FACTOR)/(per_cpu(cyc2ns, smp_processor_id()));
	return cyc;
}

/*
 * Display the statistics thru /proc.
 * 'data' points to the cpu number
 */
static int uv_ptc_seq_show(struct seq_file *file, void *data)
{
	struct ptc_stats *stat;
	int cpu;

	cpu = *(loff_t *)data;

	if (!cpu) {
		seq_printf(file,
			"# cpu sent stime self locals remotes ncpus localhub ");
		seq_printf(file,
			"remotehub numuvhubs numuvhubs16 numuvhubs8 ");
		seq_printf(file,
			"numuvhubs4 numuvhubs2 numuvhubs1 dto ");
		seq_printf(file,
			"retries rok resetp resett giveup sto bz throt ");
		seq_printf(file,
			"sw_ack recv rtime all ");
		seq_printf(file,
			"one mult none retry canc nocan reset rcan ");
		seq_printf(file,
			"disable enable\n");
	}
	if (cpu < num_possible_cpus() && cpu_online(cpu)) {
		stat = &per_cpu(ptcstats, cpu);
		/* source side statistics */
		seq_printf(file,
			"cpu %d %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld ",
			   cpu, stat->s_requestor, cycles_2_us(stat->s_time),
			   stat->s_ntargself, stat->s_ntarglocals,
			   stat->s_ntargremotes, stat->s_ntargcpu,
			   stat->s_ntarglocaluvhub, stat->s_ntargremoteuvhub,
			   stat->s_ntarguvhub, stat->s_ntarguvhub16);
		seq_printf(file, "%ld %ld %ld %ld %ld ",
			   stat->s_ntarguvhub8, stat->s_ntarguvhub4,
			   stat->s_ntarguvhub2, stat->s_ntarguvhub1,
			   stat->s_dtimeout);
		seq_printf(file, "%ld %ld %ld %ld %ld %ld %ld %ld ",
			   stat->s_retry_messages, stat->s_retriesok,
			   stat->s_resets_plug, stat->s_resets_timeout,
			   stat->s_giveup, stat->s_stimeout,
			   stat->s_busy, stat->s_throttles);

		/* destination side statistics */
		seq_printf(file,
			   "%lx %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld ",
			   uv_read_global_mmr64(uv_cpu_to_pnode(cpu),
					UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE),
			   stat->d_requestee, cycles_2_us(stat->d_time),
			   stat->d_alltlb, stat->d_onetlb, stat->d_multmsg,
			   stat->d_nomsg, stat->d_retries, stat->d_canceled,
			   stat->d_nocanceled, stat->d_resets,
			   stat->d_rcanceled);
		seq_printf(file, "%ld %ld\n",
			stat->s_bau_disabled, stat->s_bau_reenabled);
	}

	return 0;
}

/*
 * Display the tunables thru debugfs
 */
static ssize_t tunables_read(struct file *file, char __user *userbuf,
						size_t count, loff_t *ppos)
{
	char *buf;
	int ret;

	buf = kasprintf(GFP_KERNEL, "%s %s %s\n%d %d %d %d %d %d %d %d %d\n",
		"max_bau_concurrent plugged_delay plugsb4reset",
		"timeoutsb4reset ipi_reset_limit complete_threshold",
		"congested_response_us congested_reps congested_period",
		max_bau_concurrent, plugged_delay, plugsb4reset,
		timeoutsb4reset, ipi_reset_limit, complete_threshold,
		congested_response_us, congested_reps, congested_period);

	if (!buf)
		return -ENOMEM;

	ret = simple_read_from_buffer(userbuf, count, ppos, buf, strlen(buf));
	kfree(buf);
	return ret;
}

/*
 * -1: resetf the statistics
 *  0: display meaning of the statistics
 */
static ssize_t uv_ptc_proc_write(struct file *file, const char __user *user,
				 size_t count, loff_t *data)
{
	int cpu;
	long input_arg;
	char optstr[64];
	struct ptc_stats *stat;

	if (count == 0 || count > sizeof(optstr))
		return -EINVAL;
	if (copy_from_user(optstr, user, count))
		return -EFAULT;
	optstr[count - 1] = '\0';
	if (strict_strtol(optstr, 10, &input_arg) < 0) {
		printk(KERN_DEBUG "%s is invalid\n", optstr);
		return -EINVAL;
	}

	if (input_arg == 0) {
		printk(KERN_DEBUG "# cpu:      cpu number\n");
		printk(KERN_DEBUG "Sender statistics:\n");
		printk(KERN_DEBUG
		"sent:     number of shootdown messages sent\n");
		printk(KERN_DEBUG
		"stime:    time spent sending messages\n");
		printk(KERN_DEBUG
		"numuvhubs: number of hubs targeted with shootdown\n");
		printk(KERN_DEBUG
		"numuvhubs16: number times 16 or more hubs targeted\n");
		printk(KERN_DEBUG
		"numuvhubs8: number times 8 or more hubs targeted\n");
		printk(KERN_DEBUG
		"numuvhubs4: number times 4 or more hubs targeted\n");
		printk(KERN_DEBUG
		"numuvhubs2: number times 2 or more hubs targeted\n");
		printk(KERN_DEBUG
		"numuvhubs1: number times 1 hub targeted\n");
		printk(KERN_DEBUG
		"numcpus:  number of cpus targeted with shootdown\n");
		printk(KERN_DEBUG
		"dto:      number of destination timeouts\n");
		printk(KERN_DEBUG
		"retries:  destination timeout retries sent\n");
		printk(KERN_DEBUG
		"rok:   :  destination timeouts successfully retried\n");
		printk(KERN_DEBUG
		"resetp:   ipi-style resource resets for plugs\n");
		printk(KERN_DEBUG
		"resett:   ipi-style resource resets for timeouts\n");
		printk(KERN_DEBUG
		"giveup:   fall-backs to ipi-style shootdowns\n");
		printk(KERN_DEBUG
		"sto:      number of source timeouts\n");
		printk(KERN_DEBUG
		"bz:       number of stay-busy's\n");
		printk(KERN_DEBUG
		"throt:    number times spun in throttle\n");
		printk(KERN_DEBUG "Destination side statistics:\n");
		printk(KERN_DEBUG
		"sw_ack:   image of UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE\n");
		printk(KERN_DEBUG
		"recv:     shootdown messages received\n");
		printk(KERN_DEBUG
		"rtime:    time spent processing messages\n");
		printk(KERN_DEBUG
		"all:      shootdown all-tlb messages\n");
		printk(KERN_DEBUG
		"one:      shootdown one-tlb messages\n");
		printk(KERN_DEBUG
		"mult:     interrupts that found multiple messages\n");
		printk(KERN_DEBUG
		"none:     interrupts that found no messages\n");
		printk(KERN_DEBUG
		"retry:    number of retry messages processed\n");
		printk(KERN_DEBUG
		"canc:     number messages canceled by retries\n");
		printk(KERN_DEBUG
		"nocan:    number retries that found nothing to cancel\n");
		printk(KERN_DEBUG
		"reset:    number of ipi-style reset requests processed\n");
		printk(KERN_DEBUG
		"rcan:     number messages canceled by reset requests\n");
		printk(KERN_DEBUG
		"disable:  number times use of the BAU was disabled\n");
		printk(KERN_DEBUG
		"enable:   number times use of the BAU was re-enabled\n");
	} else if (input_arg == -1) {
		for_each_present_cpu(cpu) {
			stat = &per_cpu(ptcstats, cpu);
			memset(stat, 0, sizeof(struct ptc_stats));
		}
	}

	return count;
}

static int local_atoi(const char *name)
{
	int val = 0;

	for (;; name++) {
		switch (*name) {
		case '0' ... '9':
			val = 10*val+(*name-'0');
			break;
		default:
			return val;
		}
	}
}

/*
 * set the tunables
 * 0 values reset them to defaults
 */
static ssize_t tunables_write(struct file *file, const char __user *user,
				 size_t count, loff_t *data)
{
	int cpu;
	int cnt = 0;
	int val;
	char *p;
	char *q;
	char instr[64];
	struct bau_control *bcp;

	if (count == 0 || count > sizeof(instr)-1)
		return -EINVAL;
	if (copy_from_user(instr, user, count))
		return -EFAULT;

	instr[count] = '\0';
	/* count the fields */
	p = instr + strspn(instr, WHITESPACE);
	q = p;
	for (; *p; p = q + strspn(q, WHITESPACE)) {
		q = p + strcspn(p, WHITESPACE);
		cnt++;
		if (q == p)
			break;
	}
	if (cnt != 9) {
		printk(KERN_INFO "bau tunable error: should be 9 numbers\n");
		return -EINVAL;
	}

	p = instr + strspn(instr, WHITESPACE);
	q = p;
	for (cnt = 0; *p; p = q + strspn(q, WHITESPACE), cnt++) {
		q = p + strcspn(p, WHITESPACE);
		val = local_atoi(p);
		switch (cnt) {
		case 0:
			if (val == 0) {
				max_bau_concurrent = MAX_BAU_CONCURRENT;
				max_bau_concurrent_constant =
							MAX_BAU_CONCURRENT;
				continue;
			}
			bcp = &per_cpu(bau_control, smp_processor_id());
			if (val < 1 || val > bcp->cpus_in_uvhub) {
				printk(KERN_DEBUG
				"Error: BAU max concurrent %d is invalid\n",
				val);
				return -EINVAL;
			}
			max_bau_concurrent = val;
			max_bau_concurrent_constant = val;
			continue;
		case 1:
			if (val == 0)
				plugged_delay = PLUGGED_DELAY;
			else
				plugged_delay = val;
			continue;
		case 2:
			if (val == 0)
				plugsb4reset = PLUGSB4RESET;
			else
				plugsb4reset = val;
			continue;
		case 3:
			if (val == 0)
				timeoutsb4reset = TIMEOUTSB4RESET;
			else
				timeoutsb4reset = val;
			continue;
		case 4:
			if (val == 0)
				ipi_reset_limit = IPI_RESET_LIMIT;
			else
				ipi_reset_limit = val;
			continue;
		case 5:
			if (val == 0)
				complete_threshold = COMPLETE_THRESHOLD;
			else
				complete_threshold = val;
			continue;
		case 6:
			if (val == 0)
				congested_response_us = CONGESTED_RESPONSE_US;
			else
				congested_response_us = val;
			continue;
		case 7:
			if (val == 0)
				congested_reps = CONGESTED_REPS;
			else
				congested_reps = val;
			continue;
		case 8:
			if (val == 0)
				congested_period = CONGESTED_PERIOD;
			else
				congested_period = val;
			continue;
		}
		if (q == p)
			break;
	}
	for_each_present_cpu(cpu) {
		bcp = &per_cpu(bau_control, cpu);
		bcp->max_bau_concurrent = max_bau_concurrent;
		bcp->max_bau_concurrent_constant = max_bau_concurrent;
		bcp->plugged_delay = plugged_delay;
		bcp->plugsb4reset = plugsb4reset;
		bcp->timeoutsb4reset = timeoutsb4reset;
		bcp->ipi_reset_limit = ipi_reset_limit;
		bcp->complete_threshold = complete_threshold;
		bcp->congested_response_us = congested_response_us;
		bcp->congested_reps = congested_reps;
		bcp->congested_period = congested_period;
	}
	return count;
}

static const struct seq_operations uv_ptc_seq_ops = {
	.start		= uv_ptc_seq_start,
	.next		= uv_ptc_seq_next,
	.stop		= uv_ptc_seq_stop,
	.show		= uv_ptc_seq_show
};

static int uv_ptc_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &uv_ptc_seq_ops);
}

static int tunables_open(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations proc_uv_ptc_operations = {
	.open		= uv_ptc_proc_open,
	.read		= seq_read,
	.write		= uv_ptc_proc_write,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static const struct file_operations tunables_fops = {
	.open		= tunables_open,
	.read		= tunables_read,
	.write		= tunables_write,
};

static int __init uv_ptc_init(void)
{
	struct proc_dir_entry *proc_uv_ptc;

	if (!is_uv_system())
		return 0;

	proc_uv_ptc = proc_create(UV_PTC_BASENAME, 0444, NULL,
				  &proc_uv_ptc_operations);
	if (!proc_uv_ptc) {
		printk(KERN_ERR "unable to create %s proc entry\n",
		       UV_PTC_BASENAME);
		return -EINVAL;
	}

	tunables_dir = debugfs_create_dir(UV_BAU_TUNABLES_DIR, NULL);
	if (!tunables_dir) {
		printk(KERN_ERR "unable to create debugfs directory %s\n",
		       UV_BAU_TUNABLES_DIR);
		return -EINVAL;
	}
	tunables_file = debugfs_create_file(UV_BAU_TUNABLES_FILE, 0600,
			tunables_dir, NULL, &tunables_fops);
	if (!tunables_file) {
		printk(KERN_ERR "unable to create debugfs file %s\n",
		       UV_BAU_TUNABLES_FILE);
		return -EINVAL;
	}
	return 0;
}

/*
 * initialize the sending side's sending buffers
 */
static void
uv_activation_descriptor_init(int node, int pnode)
{
	int i;
	int cpu;
	unsigned long pa;
	unsigned long m;
	unsigned long n;
	struct bau_desc *bau_desc;
	struct bau_desc *bd2;
	struct bau_control *bcp;

	/*
	 * each bau_desc is 64 bytes; there are 8 (UV_ITEMS_PER_DESCRIPTOR)
	 * per cpu; and up to 32 (UV_ADP_SIZE) cpu's per uvhub
	 */
	bau_desc = (struct bau_desc *)kmalloc_node(sizeof(struct bau_desc)*
		UV_ADP_SIZE*UV_ITEMS_PER_DESCRIPTOR, GFP_KERNEL, node);
	BUG_ON(!bau_desc);

	pa = uv_gpa(bau_desc); /* need the real nasid*/
	n = pa >> uv_nshift;
	m = pa & uv_mmask;

	uv_write_global_mmr64(pnode, UVH_LB_BAU_SB_DESCRIPTOR_BASE,
			      (n << UV_DESC_BASE_PNODE_SHIFT | m));

	/*
	 * initializing all 8 (UV_ITEMS_PER_DESCRIPTOR) descriptors for each
	 * cpu even though we only use the first one; one descriptor can
	 * describe a broadcast to 256 uv hubs.
	 */
	for (i = 0, bd2 = bau_desc; i < (UV_ADP_SIZE*UV_ITEMS_PER_DESCRIPTOR);
		i++, bd2++) {
		memset(bd2, 0, sizeof(struct bau_desc));
		bd2->header.sw_ack_flag = 1;
		/*
		 * base_dest_nodeid is the nasid (pnode<<1) of the first uvhub
		 * in the partition. The bit map will indicate uvhub numbers,
		 * which are 0-N in a partition. Pnodes are unique system-wide.
		 */
		bd2->header.base_dest_nodeid = uv_partition_base_pnode << 1;
		bd2->header.dest_subnodeid = 0x10; /* the LB */
		bd2->header.command = UV_NET_ENDPOINT_INTD;
		bd2->header.int_both = 1;
		/*
		 * all others need to be set to zero:
		 *   fairness chaining multilevel count replied_to
		 */
	}
	for_each_present_cpu(cpu) {
		if (pnode != uv_blade_to_pnode(uv_cpu_to_blade_id(cpu)))
			continue;
		bcp = &per_cpu(bau_control, cpu);
		bcp->descriptor_base = bau_desc;
	}
}

/*
 * initialize the destination side's receiving buffers
 * entered for each uvhub in the partition
 * - node is first node (kernel memory notion) on the uvhub
 * - pnode is the uvhub's physical identifier
 */
static void
uv_payload_queue_init(int node, int pnode)
{
	int pn;
	int cpu;
	char *cp;
	unsigned long pa;
	struct bau_payload_queue_entry *pqp;
	struct bau_payload_queue_entry *pqp_malloc;
	struct bau_control *bcp;

	pqp = (struct bau_payload_queue_entry *) kmalloc_node(
		(DEST_Q_SIZE + 1) * sizeof(struct bau_payload_queue_entry),
		GFP_KERNEL, node);
	BUG_ON(!pqp);
	pqp_malloc = pqp;

	cp = (char *)pqp + 31;
	pqp = (struct bau_payload_queue_entry *)(((unsigned long)cp >> 5) << 5);

	for_each_present_cpu(cpu) {
		if (pnode != uv_cpu_to_pnode(cpu))
			continue;
		/* for every cpu on this pnode: */
		bcp = &per_cpu(bau_control, cpu);
		bcp->va_queue_first = pqp;
		bcp->bau_msg_head = pqp;
		bcp->va_queue_last = pqp + (DEST_Q_SIZE - 1);
	}
	/*
	 * need the pnode of where the memory was really allocated
	 */
	pa = uv_gpa(pqp);
	pn = pa >> uv_nshift;
	uv_write_global_mmr64(pnode,
			      UVH_LB_BAU_INTD_PAYLOAD_QUEUE_FIRST,
			      ((unsigned long)pn << UV_PAYLOADQ_PNODE_SHIFT) |
			      uv_physnodeaddr(pqp));
	uv_write_global_mmr64(pnode, UVH_LB_BAU_INTD_PAYLOAD_QUEUE_TAIL,
			      uv_physnodeaddr(pqp));
	uv_write_global_mmr64(pnode, UVH_LB_BAU_INTD_PAYLOAD_QUEUE_LAST,
			      (unsigned long)
			      uv_physnodeaddr(pqp + (DEST_Q_SIZE - 1)));
	/* in effect, all msg_type's are set to MSG_NOOP */
	memset(pqp, 0, sizeof(struct bau_payload_queue_entry) * DEST_Q_SIZE);
}

/*
 * Initialization of each UV hub's structures
 */
static void __init uv_init_uvhub(int uvhub, int vector)
{
	int node;
	int pnode;
	unsigned long apicid;

	node = uvhub_to_first_node(uvhub);
	pnode = uv_blade_to_pnode(uvhub);
	uv_activation_descriptor_init(node, pnode);
	uv_payload_queue_init(node, pnode);
	/*
	 * the below initialization can't be in firmware because the
	 * messaging IRQ will be determined by the OS
	 */
	apicid = uvhub_to_first_apicid(uvhub);
	uv_write_global_mmr64(pnode, UVH_BAU_DATA_CONFIG,
				      ((apicid << 32) | vector));
}

/*
 * We will set BAU_MISC_CONTROL with a timeout period.
 * But the BIOS has set UVH_AGING_PRESCALE_SEL and UVH_TRANSACTION_TIMEOUT.
 * So the destination timeout period has be be calculated from them.
 */
static int
calculate_destination_timeout(void)
{
	unsigned long mmr_image;
	int mult1;
	int mult2;
	int index;
	int base;
	int ret;
	unsigned long ts_ns;

	mult1 = UV_INTD_SOFT_ACK_TIMEOUT_PERIOD & BAU_MISC_CONTROL_MULT_MASK;
	mmr_image = uv_read_local_mmr(UVH_AGING_PRESCALE_SEL);
	index = (mmr_image >> BAU_URGENCY_7_SHIFT) & BAU_URGENCY_7_MASK;
	mmr_image = uv_read_local_mmr(UVH_TRANSACTION_TIMEOUT);
	mult2 = (mmr_image >> BAU_TRANS_SHIFT) & BAU_TRANS_MASK;
	base = timeout_base_ns[index];
	ts_ns = base * mult1 * mult2;
	ret = ts_ns / 1000;
	return ret;
}

/*
 * initialize the bau_control structure for each cpu
 */
static void __init uv_init_per_cpu(int nuvhubs)
{
	int i;
	int cpu;
	int pnode;
	int uvhub;
	int have_hmaster;
	short socket = 0;
	unsigned short socket_mask;
	unsigned char *uvhub_mask;
	struct bau_control *bcp;
	struct uvhub_desc *bdp;
	struct socket_desc *sdp;
	struct bau_control *hmaster = NULL;
	struct bau_control *smaster = NULL;
	struct socket_desc {
		short num_cpus;
		short cpu_number[16];
	};
	struct uvhub_desc {
		unsigned short socket_mask;
		short num_cpus;
		short uvhub;
		short pnode;
		struct socket_desc socket[2];
	};
	struct uvhub_desc *uvhub_descs;

	timeout_us = calculate_destination_timeout();

	uvhub_descs = (struct uvhub_desc *)
		kmalloc(nuvhubs * sizeof(struct uvhub_desc), GFP_KERNEL);
	memset(uvhub_descs, 0, nuvhubs * sizeof(struct uvhub_desc));
	uvhub_mask = kzalloc((nuvhubs+7)/8, GFP_KERNEL);
	for_each_present_cpu(cpu) {
		bcp = &per_cpu(bau_control, cpu);
		memset(bcp, 0, sizeof(struct bau_control));
		pnode = uv_cpu_hub_info(cpu)->pnode;
		uvhub = uv_cpu_hub_info(cpu)->numa_blade_id;
		*(uvhub_mask + (uvhub/8)) |= (1 << (uvhub%8));
		bdp = &uvhub_descs[uvhub];
		bdp->num_cpus++;
		bdp->uvhub = uvhub;
		bdp->pnode = pnode;
		/* kludge: 'assuming' one node per socket, and assuming that
		   disabling a socket just leaves a gap in node numbers */
		socket = (cpu_to_node(cpu) & 1);
		bdp->socket_mask |= (1 << socket);
		sdp = &bdp->socket[socket];
		sdp->cpu_number[sdp->num_cpus] = cpu;
		sdp->num_cpus++;
	}
	for (uvhub = 0; uvhub < nuvhubs; uvhub++) {
		if (!(*(uvhub_mask + (uvhub/8)) & (1 << (uvhub%8))))
			continue;
		have_hmaster = 0;
		bdp = &uvhub_descs[uvhub];
		socket_mask = bdp->socket_mask;
		socket = 0;
		while (socket_mask) {
			if (!(socket_mask & 1))
				goto nextsocket;
			sdp = &bdp->socket[socket];
			for (i = 0; i < sdp->num_cpus; i++) {
				cpu = sdp->cpu_number[i];
				bcp = &per_cpu(bau_control, cpu);
				bcp->cpu = cpu;
				if (i == 0) {
					smaster = bcp;
					if (!have_hmaster) {
						have_hmaster++;
						hmaster = bcp;
					}
				}
				bcp->cpus_in_uvhub = bdp->num_cpus;
				bcp->cpus_in_socket = sdp->num_cpus;
				bcp->socket_master = smaster;
				bcp->uvhub = bdp->uvhub;
				bcp->uvhub_master = hmaster;
				bcp->uvhub_cpu = uv_cpu_hub_info(cpu)->
						blade_processor_id;
			}
nextsocket:
			socket++;
			socket_mask = (socket_mask >> 1);
		}
	}
	kfree(uvhub_descs);
	kfree(uvhub_mask);
	for_each_present_cpu(cpu) {
		bcp = &per_cpu(bau_control, cpu);
		bcp->baudisabled = 0;
		bcp->statp = &per_cpu(ptcstats, cpu);
		/* time interval to catch a hardware stay-busy bug */
		bcp->timeout_interval = microsec_2_cycles(2*timeout_us);
		bcp->max_bau_concurrent = max_bau_concurrent;
		bcp->max_bau_concurrent_constant = max_bau_concurrent;
		bcp->plugged_delay = plugged_delay;
		bcp->plugsb4reset = plugsb4reset;
		bcp->timeoutsb4reset = timeoutsb4reset;
		bcp->ipi_reset_limit = ipi_reset_limit;
		bcp->complete_threshold = complete_threshold;
		bcp->congested_response_us = congested_response_us;
		bcp->congested_reps = congested_reps;
		bcp->congested_period = congested_period;
	}
}

/*
 * Initialization of BAU-related structures
 */
static int __init uv_bau_init(void)
{
	int uvhub;
	int pnode;
	int nuvhubs;
	int cur_cpu;
	int vector;
	unsigned long mmr;

	if (!is_uv_system())
		return 0;

	if (nobau)
		return 0;

	for_each_possible_cpu(cur_cpu)
		zalloc_cpumask_var_node(&per_cpu(uv_flush_tlb_mask, cur_cpu),
				       GFP_KERNEL, cpu_to_node(cur_cpu));

	uv_nshift = uv_hub_info->m_val;
	uv_mmask = (1UL << uv_hub_info->m_val) - 1;
	nuvhubs = uv_num_possible_blades();
	spin_lock_init(&disable_lock);
	congested_cycles = microsec_2_cycles(congested_response_us);

	uv_init_per_cpu(nuvhubs);

	uv_partition_base_pnode = 0x7fffffff;
	for (uvhub = 0; uvhub < nuvhubs; uvhub++)
		if (uv_blade_nr_possible_cpus(uvhub) &&
			(uv_blade_to_pnode(uvhub) < uv_partition_base_pnode))
			uv_partition_base_pnode = uv_blade_to_pnode(uvhub);

	vector = UV_BAU_MESSAGE;
	for_each_possible_blade(uvhub)
		if (uv_blade_nr_possible_cpus(uvhub))
			uv_init_uvhub(uvhub, vector);

	uv_enable_timeouts();
	alloc_intr_gate(vector, uv_bau_message_intr1);

	for_each_possible_blade(uvhub) {
		if (uv_blade_nr_possible_cpus(uvhub)) {
			pnode = uv_blade_to_pnode(uvhub);
			/* INIT the bau */
			uv_write_global_mmr64(pnode,
					UVH_LB_BAU_SB_ACTIVATION_CONTROL,
					((unsigned long)1 << 63));
			mmr = 1; /* should be 1 to broadcast to both sockets */
			uv_write_global_mmr64(pnode, UVH_BAU_DATA_BROADCAST,
						mmr);
		}
	}

	return 0;
}
core_initcall(uv_bau_init);
fs_initcall(uv_ptc_init);
