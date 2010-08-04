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

struct msg_desc {
	struct bau_payload_queue_entry *msg;
	int msg_slot;
	int sw_ack_slot;
	struct bau_payload_queue_entry *va_queue_first;
	struct bau_payload_queue_entry *va_queue_last;
};

#define UV_INTD_SOFT_ACK_TIMEOUT_PERIOD	0x000000000bUL

static int uv_bau_max_concurrent __read_mostly;

static int nobau;
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

struct reset_args {
	int sender;
};

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
	stat = &per_cpu(ptcstats, bcp->cpu);
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
			msg_res = ((msg2->sw_ack_vector << 8) |
				   msg2->sw_ack_vector);
			/*
			 * This is a message retry; clear the resources held
			 * by the previous message only if they timed out.
			 * If it has not timed out we have an unexpected
			 * situation to report.
			 */
			if (mmr & (msg_res << 8)) {
				/*
				 * is the resource timed out?
				 * make everyone ignore the cancelled message.
				 */
				msg2->canceled = 1;
				stat->d_canceled++;
				cancel_count++;
				uv_write_local_mmr(
				    UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_ALIAS,
					(msg_res << 8) | msg_res);
			} else
				printk(KERN_INFO "note bau retry: no effect\n");
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
	stat = &per_cpu(ptcstats, bcp->cpu);
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
	stat = &per_cpu(ptcstats, bcp->cpu);
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
			msg_res = ((msg->sw_ack_vector << 8) |
						   msg->sw_ack_vector);
			if (mmr & msg_res) {
				stat->d_rcanceled++;
				uv_write_local_mmr(
				    UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE_ALIAS,
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
	int relaxes = 0;
	unsigned long descriptor_status;
	unsigned long mmr;
	unsigned long mask;
	cycles_t ttime;
	cycles_t timeout_time;
	struct ptc_stats *stat = &per_cpu(ptcstats, this_cpu);
	struct bau_control *hmaster;

	hmaster = bcp->uvhub_master;
	timeout_time = get_cycles() + bcp->timeout_interval;

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
			if (cycles_2_us(ttime - bcp->send_message) < BIOS_TO) {
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
			relaxes++;
			if (relaxes >= 10000) {
				relaxes = 0;
				if (get_cycles() > timeout_time) {
					quiesce_local_uvhub(hmaster);

					/* single-thread the register change */
					spin_lock(&hmaster->masks_lock);
					mmr = uv_read_local_mmr(mmr_offset);
					mask = 0UL;
					mask |= (3UL < right_shift);
					mask = ~mask;
					mmr &= mask;
					uv_write_local_mmr(mmr_offset, mmr);
					spin_unlock(&hmaster->masks_lock);
					end_uvhub_quiesce(hmaster);
					stat->s_busy++;
					return FLUSH_GIVEUP;
				}
			}
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

/**
 * uv_flush_send_and_wait
 *
 * Send a broadcast and wait for it to complete.
 *
 * The flush_mask contains the cpus the broadcast is to be sent to, plus
 * cpus that are on the local uvhub.
 *
 * Returns NULL if all flushing represented in the mask was done. The mask
 * is zeroed.
 * Returns @flush_mask if some remote flushing remains to be done. The
 * mask will have some bits still set, representing any cpus on the local
 * uvhub (not current cpu) and any on remote uvhubs if the broadcast failed.
 */
const struct cpumask *uv_flush_send_and_wait(struct bau_desc *bau_desc,
					     struct cpumask *flush_mask,
					     struct bau_control *bcp)
{
	int right_shift;
	int uvhub;
	int bit;
	int completion_status = 0;
	int seq_number = 0;
	long try = 0;
	int cpu = bcp->uvhub_cpu;
	int this_cpu = bcp->cpu;
	int this_uvhub = bcp->uvhub;
	unsigned long mmr_offset;
	unsigned long index;
	cycles_t time1;
	cycles_t time2;
	struct ptc_stats *stat = &per_cpu(ptcstats, bcp->cpu);
	struct bau_control *smaster = bcp->socket_master;
	struct bau_control *hmaster = bcp->uvhub_master;

	/*
	 * Spin here while there are hmaster->max_concurrent or more active
	 * descriptors. This is the per-uvhub 'throttle'.
	 */
	if (!atomic_inc_unless_ge(&hmaster->uvhub_lock,
			&hmaster->active_descriptor_count,
			hmaster->max_concurrent)) {
		stat->s_throttles++;
		do {
			cpu_relax();
		} while (!atomic_inc_unless_ge(&hmaster->uvhub_lock,
			&hmaster->active_descriptor_count,
			hmaster->max_concurrent));
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
		/*
		 * Every message from any given cpu gets a unique message
		 * sequence number. But retries use that same number.
		 * Our message may have timed out at the destination because
		 * all sw-ack resources are in use and there is a timeout
		 * pending there.  In that case, our last send never got
		 * placed into the queue and we need to persist until it
		 * does.
		 *
		 * Make any retry a type MSG_RETRY so that the destination will
		 * free any resource held by a previous message from this cpu.
		 */
		if (try == 0) {
			/* use message type set by the caller the first time */
			seq_number = bcp->message_number++;
		} else {
			/* use RETRY type on all the rest; same sequence */
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
			/*
			 * Our retries may be blocked by all destination swack
			 * resources being consumed, and a timeout pending. In
			 * that case hardware immediately returns the ERROR
			 * that looks like a destination timeout.
			 */
			udelay(TIMEOUT_DELAY);
			bcp->plugged_tries++;
			if (bcp->plugged_tries >= PLUGSB4RESET) {
				bcp->plugged_tries = 0;
				quiesce_local_uvhub(hmaster);
				spin_lock(&hmaster->queue_lock);
				uv_reset_with_ipi(&bau_desc->distribution,
							this_cpu);
				spin_unlock(&hmaster->queue_lock);
				end_uvhub_quiesce(hmaster);
				bcp->ipi_attempts++;
				stat->s_resets_plug++;
			}
		} else if (completion_status == FLUSH_RETRY_TIMEOUT) {
			hmaster->max_concurrent = 1;
			bcp->timeout_tries++;
			udelay(TIMEOUT_DELAY);
			if (bcp->timeout_tries >= TIMEOUTSB4RESET) {
				bcp->timeout_tries = 0;
				quiesce_local_uvhub(hmaster);
				spin_lock(&hmaster->queue_lock);
				uv_reset_with_ipi(&bau_desc->distribution,
								this_cpu);
				spin_unlock(&hmaster->queue_lock);
				end_uvhub_quiesce(hmaster);
				bcp->ipi_attempts++;
				stat->s_resets_timeout++;
			}
		}
		if (bcp->ipi_attempts >= 3) {
			bcp->ipi_attempts = 0;
			completion_status = FLUSH_GIVEUP;
			break;
		}
		cpu_relax();
	} while ((completion_status == FLUSH_RETRY_PLUGGED) ||
		 (completion_status == FLUSH_RETRY_TIMEOUT));
	time2 = get_cycles();

	if ((completion_status == FLUSH_COMPLETE) && (bcp->conseccompletes > 5)
	    && (hmaster->max_concurrent < hmaster->max_concurrent_constant))
			hmaster->max_concurrent++;

	/*
	 * hold any cpu not timing out here; no other cpu currently held by
	 * the 'throttle' should enter the activation code
	 */
	while (hmaster->uvhub_quiesce)
		cpu_relax();
	atomic_dec(&hmaster->active_descriptor_count);

	/* guard against cycles wrap */
	if (time2 > time1)
		stat->s_time += (time2 - time1);
	else
		stat->s_requestor--; /* don't count this one */
	if (completion_status == FLUSH_COMPLETE && try > 1)
		stat->s_retriesok++;
	else if (completion_status == FLUSH_GIVEUP) {
		/*
		 * Cause the caller to do an IPI-style TLB shootdown on
		 * the target cpu's, all of which are still in the mask.
		 */
		stat->s_giveup++;
		return flush_mask;
	}

	/*
	 * Success, so clear the remote cpu's from the mask so we don't
	 * use the IPI method of shootdown on them.
	 */
	for_each_cpu(bit, flush_mask) {
		uvhub = uv_cpu_to_blade_id(bit);
		if (uvhub == this_uvhub)
			continue;
		cpumask_clear_cpu(bit, flush_mask);
	}
	if (!cpumask_empty(flush_mask))
		return flush_mask;

	return NULL;
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
	int remotes;
	int tcpu;
	int uvhub;
	int locals = 0;
	struct bau_desc *bau_desc;
	struct cpumask *flush_mask;
	struct ptc_stats *stat;
	struct bau_control *bcp;

	if (nobau)
		return cpumask;

	bcp = &per_cpu(bau_control, cpu);
	/*
	 * Each sending cpu has a per-cpu mask which it fills from the caller's
	 * cpu mask.  Only remote cpus are converted to uvhubs and copied.
	 */
	flush_mask = (struct cpumask *)per_cpu(uv_flush_tlb_mask, cpu);
	/*
	 * copy cpumask to flush_mask, removing current cpu
	 * (current cpu should already have been flushed by the caller and
	 *  should never be returned if we return flush_mask)
	 */
	cpumask_andnot(flush_mask, cpumask, cpumask_of(cpu));
	if (cpu_isset(cpu, *cpumask))
		locals++;  /* current cpu was targeted */

	bau_desc = bcp->descriptor_base;
	bau_desc += UV_ITEMS_PER_DESCRIPTOR * bcp->uvhub_cpu;

	bau_uvhubs_clear(&bau_desc->distribution, UV_DISTRIBUTION_SIZE);
	remotes = 0;
	for_each_cpu(tcpu, flush_mask) {
		uvhub = uv_cpu_to_blade_id(tcpu);
		if (uvhub == bcp->uvhub) {
			locals++;
			continue;
		}
		bau_uvhub_set(uvhub, &bau_desc->distribution);
		remotes++;
	}
	if (remotes == 0) {
		/*
		 * No off_hub flushing; return status for local hub.
		 * Return the caller's mask if all were local (the current
		 * cpu may be in that mask).
		 */
		if (locals)
			return cpumask;
		else
			return NULL;
	}
	stat = &per_cpu(ptcstats, cpu);
	stat->s_requestor++;
	stat->s_ntargcpu += remotes;
	remotes = bau_uvhub_weight(&bau_desc->distribution);
	stat->s_ntarguvhub += remotes;
	if (remotes >= 16)
		stat->s_ntarguvhub16++;
	else if (remotes >= 8)
		stat->s_ntarguvhub8++;
	else if (remotes >= 4)
		stat->s_ntarguvhub4++;
	else if (remotes >= 2)
		stat->s_ntarguvhub2++;
	else
		stat->s_ntarguvhub1++;

	bau_desc->payload.address = va;
	bau_desc->payload.sending_cpu = cpu;

	/*
	 * uv_flush_send_and_wait returns null if all cpu's were messaged, or
	 * the adjusted flush_mask if any cpu's were not messaged.
	 */
	return uv_flush_send_and_wait(bau_desc, flush_mask, bcp);
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
	stat = &per_cpu(ptcstats, smp_processor_id());
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
millisec_2_cycles(unsigned long millisec)
{
	unsigned long ns;
	unsigned long long cyc;

	ns = millisec * 1000;
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
			"# cpu sent stime numuvhubs numuvhubs16 numuvhubs8 ");
		seq_printf(file,
			"numuvhubs4 numuvhubs2 numuvhubs1 numcpus dto ");
		seq_printf(file,
			"retries rok resetp resett giveup sto bz throt ");
		seq_printf(file,
			"sw_ack recv rtime all ");
		seq_printf(file,
			"one mult none retry canc nocan reset rcan\n");
	}
	if (cpu < num_possible_cpus() && cpu_online(cpu)) {
		stat = &per_cpu(ptcstats, cpu);
		/* source side statistics */
		seq_printf(file,
			"cpu %d %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld ",
			   cpu, stat->s_requestor, cycles_2_us(stat->s_time),
			   stat->s_ntarguvhub, stat->s_ntarguvhub16,
			   stat->s_ntarguvhub8, stat->s_ntarguvhub4,
			   stat->s_ntarguvhub2, stat->s_ntarguvhub1,
			   stat->s_ntargcpu, stat->s_dtimeout);
		seq_printf(file, "%ld %ld %ld %ld %ld %ld %ld %ld ",
			   stat->s_retry_messages, stat->s_retriesok,
			   stat->s_resets_plug, stat->s_resets_timeout,
			   stat->s_giveup, stat->s_stimeout,
			   stat->s_busy, stat->s_throttles);
		/* destination side statistics */
		seq_printf(file,
			   "%lx %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld\n",
			   uv_read_global_mmr64(uv_cpu_to_pnode(cpu),
					UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE),
			   stat->d_requestee, cycles_2_us(stat->d_time),
			   stat->d_alltlb, stat->d_onetlb, stat->d_multmsg,
			   stat->d_nomsg, stat->d_retries, stat->d_canceled,
			   stat->d_nocanceled, stat->d_resets,
			   stat->d_rcanceled);
	}

	return 0;
}

/*
 * -1: resetf the statistics
 *  0: display meaning of the statistics
 * >0: maximum concurrent active descriptors per uvhub (throttle)
 */
static ssize_t uv_ptc_proc_write(struct file *file, const char __user *user,
				 size_t count, loff_t *data)
{
	int cpu;
	long input_arg;
	char optstr[64];
	struct ptc_stats *stat;
	struct bau_control *bcp;

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
	} else if (input_arg == -1) {
		for_each_present_cpu(cpu) {
			stat = &per_cpu(ptcstats, cpu);
			memset(stat, 0, sizeof(struct ptc_stats));
		}
	} else {
		uv_bau_max_concurrent = input_arg;
		bcp = &per_cpu(bau_control, smp_processor_id());
		if (uv_bau_max_concurrent < 1 ||
		    uv_bau_max_concurrent > bcp->cpus_in_uvhub) {
			printk(KERN_DEBUG
				"Error: BAU max concurrent %d; %d is invalid\n",
				bcp->max_concurrent, uv_bau_max_concurrent);
			return -EINVAL;
		}
		printk(KERN_DEBUG "Set BAU max concurrent:%d\n",
		       uv_bau_max_concurrent);
		for_each_present_cpu(cpu) {
			bcp = &per_cpu(bau_control, cpu);
			bcp->max_concurrent = uv_bau_max_concurrent;
		}
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

static const struct file_operations proc_uv_ptc_operations = {
	.open		= uv_ptc_proc_open,
	.read		= seq_read,
	.write		= uv_ptc_proc_write,
	.llseek		= seq_lseek,
	.release	= seq_release,
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
 * initialize the bau_control structure for each cpu
 */
static void uv_init_per_cpu(int nuvhubs)
{
	int i, j, k;
	int cpu;
	int pnode;
	int uvhub;
	short socket = 0;
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
		short num_sockets;
		short num_cpus;
		short uvhub;
		short pnode;
		struct socket_desc socket[2];
	};
	struct uvhub_desc *uvhub_descs;

	uvhub_descs = (struct uvhub_desc *)
		kmalloc(nuvhubs * sizeof(struct uvhub_desc), GFP_KERNEL);
	memset(uvhub_descs, 0, nuvhubs * sizeof(struct uvhub_desc));
	for_each_present_cpu(cpu) {
		bcp = &per_cpu(bau_control, cpu);
		memset(bcp, 0, sizeof(struct bau_control));
		spin_lock_init(&bcp->masks_lock);
		bcp->max_concurrent = uv_bau_max_concurrent;
		pnode = uv_cpu_hub_info(cpu)->pnode;
		uvhub = uv_cpu_hub_info(cpu)->numa_blade_id;
		bdp = &uvhub_descs[uvhub];
		bdp->num_cpus++;
		bdp->uvhub = uvhub;
		bdp->pnode = pnode;
		/* time interval to catch a hardware stay-busy bug */
		bcp->timeout_interval = millisec_2_cycles(3);
		/* kludge: assume uv_hub.h is constant */
		socket = (cpu_physical_id(cpu)>>5)&1;
		if (socket >= bdp->num_sockets)
			bdp->num_sockets = socket+1;
		sdp = &bdp->socket[socket];
		sdp->cpu_number[sdp->num_cpus] = cpu;
		sdp->num_cpus++;
	}
	socket = 0;
	for_each_possible_blade(uvhub) {
		bdp = &uvhub_descs[uvhub];
		for (i = 0; i < bdp->num_sockets; i++) {
			sdp = &bdp->socket[i];
			for (j = 0; j < sdp->num_cpus; j++) {
				cpu = sdp->cpu_number[j];
				bcp = &per_cpu(bau_control, cpu);
				bcp->cpu = cpu;
				if (j == 0) {
					smaster = bcp;
					if (i == 0)
						hmaster = bcp;
				}
				bcp->cpus_in_uvhub = bdp->num_cpus;
				bcp->cpus_in_socket = sdp->num_cpus;
				bcp->socket_master = smaster;
				bcp->uvhub_master = hmaster;
				for (k = 0; k < DEST_Q_SIZE; k++)
					bcp->socket_acknowledge_count[k] = 0;
				bcp->uvhub_cpu =
				  uv_cpu_hub_info(cpu)->blade_processor_id;
			}
			socket++;
		}
	}
	kfree(uvhub_descs);
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

	uv_bau_max_concurrent = MAX_BAU_CONCURRENT;
	uv_nshift = uv_hub_info->m_val;
	uv_mmask = (1UL << uv_hub_info->m_val) - 1;
	nuvhubs = uv_num_possible_blades();

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
		pnode = uv_blade_to_pnode(uvhub);
		/* INIT the bau */
		uv_write_global_mmr64(pnode, UVH_LB_BAU_SB_ACTIVATION_CONTROL,
				      ((unsigned long)1 << 63));
		mmr = 1; /* should be 1 to broadcast to both sockets */
		uv_write_global_mmr64(pnode, UVH_BAU_DATA_BROADCAST, mmr);
	}

	return 0;
}
core_initcall(uv_bau_init);
core_initcall(uv_ptc_init);
