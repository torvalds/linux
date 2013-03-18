/*
 *	SGI UltraViolet TLB flush routines.
 *
 *	(c) 2008-2012 Cliff Wickman <cpw@sgi.com>, SGI.
 *
 *	This code is released under the GNU General Public License version 2 or
 *	later.
 */
#include <linux/seq_file.h>
#include <linux/proc_fs.h>
#include <linux/debugfs.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/delay.h>

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
static int nobau_perm;
static cycles_t congested_cycles;

/* tunables: */
static int max_concurr		= MAX_BAU_CONCURRENT;
static int max_concurr_const	= MAX_BAU_CONCURRENT;
static int plugged_delay	= PLUGGED_DELAY;
static int plugsb4reset		= PLUGSB4RESET;
static int giveup_limit		= GIVEUP_LIMIT;
static int timeoutsb4reset	= TIMEOUTSB4RESET;
static int ipi_reset_limit	= IPI_RESET_LIMIT;
static int complete_threshold	= COMPLETE_THRESHOLD;
static int congested_respns_us	= CONGESTED_RESPONSE_US;
static int congested_reps	= CONGESTED_REPS;
static int disabled_period	= DISABLED_PERIOD;

static struct tunables tunables[] = {
	{&max_concurr, MAX_BAU_CONCURRENT}, /* must be [0] */
	{&plugged_delay, PLUGGED_DELAY},
	{&plugsb4reset, PLUGSB4RESET},
	{&timeoutsb4reset, TIMEOUTSB4RESET},
	{&ipi_reset_limit, IPI_RESET_LIMIT},
	{&complete_threshold, COMPLETE_THRESHOLD},
	{&congested_respns_us, CONGESTED_RESPONSE_US},
	{&congested_reps, CONGESTED_REPS},
	{&disabled_period, DISABLED_PERIOD},
	{&giveup_limit, GIVEUP_LIMIT}
};

static struct dentry *tunables_dir;
static struct dentry *tunables_file;

/* these correspond to the statistics printed by ptc_seq_show() */
static char *stat_description[] = {
	"sent:     number of shootdown messages sent",
	"stime:    time spent sending messages",
	"numuvhubs: number of hubs targeted with shootdown",
	"numuvhubs16: number times 16 or more hubs targeted",
	"numuvhubs8: number times 8 or more hubs targeted",
	"numuvhubs4: number times 4 or more hubs targeted",
	"numuvhubs2: number times 2 or more hubs targeted",
	"numuvhubs1: number times 1 hub targeted",
	"numcpus:  number of cpus targeted with shootdown",
	"dto:      number of destination timeouts",
	"retries:  destination timeout retries sent",
	"rok:   :  destination timeouts successfully retried",
	"resetp:   ipi-style resource resets for plugs",
	"resett:   ipi-style resource resets for timeouts",
	"giveup:   fall-backs to ipi-style shootdowns",
	"sto:      number of source timeouts",
	"bz:       number of stay-busy's",
	"throt:    number times spun in throttle",
	"swack:   image of UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE",
	"recv:     shootdown messages received",
	"rtime:    time spent processing messages",
	"all:      shootdown all-tlb messages",
	"one:      shootdown one-tlb messages",
	"mult:     interrupts that found multiple messages",
	"none:     interrupts that found no messages",
	"retry:    number of retry messages processed",
	"canc:     number messages canceled by retries",
	"nocan:    number retries that found nothing to cancel",
	"reset:    number of ipi-style reset requests processed",
	"rcan:     number messages canceled by reset requests",
	"disable:  number times use of the BAU was disabled",
	"enable:   number times use of the BAU was re-enabled"
};

static int __init
setup_nobau(char *arg)
{
	nobau = 1;
	return 0;
}
early_param("nobau", setup_nobau);

/* base pnode in this partition */
static int uv_base_pnode __read_mostly;

static DEFINE_PER_CPU(struct ptc_stats, ptcstats);
static DEFINE_PER_CPU(struct bau_control, bau_control);
static DEFINE_PER_CPU(cpumask_var_t, uv_flush_tlb_mask);

static void
set_bau_on(void)
{
	int cpu;
	struct bau_control *bcp;

	if (nobau_perm) {
		pr_info("BAU not initialized; cannot be turned on\n");
		return;
	}
	nobau = 0;
	for_each_present_cpu(cpu) {
		bcp = &per_cpu(bau_control, cpu);
		bcp->nobau = 0;
	}
	pr_info("BAU turned on\n");
	return;
}

static void
set_bau_off(void)
{
	int cpu;
	struct bau_control *bcp;

	nobau = 1;
	for_each_present_cpu(cpu) {
		bcp = &per_cpu(bau_control, cpu);
		bcp->nobau = 1;
	}
	pr_info("BAU turned off\n");
	return;
}

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
static void reply_to_message(struct msg_desc *mdp, struct bau_control *bcp,
						int do_acknowledge)
{
	unsigned long dw;
	struct bau_pq_entry *msg;

	msg = mdp->msg;
	if (!msg->canceled && do_acknowledge) {
		dw = (msg->swack_vec << UV_SW_ACK_NPENDING) | msg->swack_vec;
		write_mmr_sw_ack(dw);
	}
	msg->replied_to = 1;
	msg->swack_vec = 0;
}

/*
 * Process the receipt of a RETRY message
 */
static void bau_process_retry_msg(struct msg_desc *mdp,
					struct bau_control *bcp)
{
	int i;
	int cancel_count = 0;
	unsigned long msg_res;
	unsigned long mmr = 0;
	struct bau_pq_entry *msg = mdp->msg;
	struct bau_pq_entry *msg2;
	struct ptc_stats *stat = bcp->statp;

	stat->d_retries++;
	/*
	 * cancel any message from msg+1 to the retry itself
	 */
	for (msg2 = msg+1, i = 0; i < DEST_Q_SIZE; msg2++, i++) {
		if (msg2 > mdp->queue_last)
			msg2 = mdp->queue_first;
		if (msg2 == msg)
			break;

		/* same conditions for cancellation as do_reset */
		if ((msg2->replied_to == 0) && (msg2->canceled == 0) &&
		    (msg2->swack_vec) && ((msg2->swack_vec &
			msg->swack_vec) == 0) &&
		    (msg2->sending_cpu == msg->sending_cpu) &&
		    (msg2->msg_type != MSG_NOOP)) {
			mmr = read_mmr_sw_ack();
			msg_res = msg2->swack_vec;
			/*
			 * This is a message retry; clear the resources held
			 * by the previous message only if they timed out.
			 * If it has not timed out we have an unexpected
			 * situation to report.
			 */
			if (mmr & (msg_res << UV_SW_ACK_NPENDING)) {
				unsigned long mr;
				/*
				 * Is the resource timed out?
				 * Make everyone ignore the cancelled message.
				 */
				msg2->canceled = 1;
				stat->d_canceled++;
				cancel_count++;
				mr = (msg_res << UV_SW_ACK_NPENDING) | msg_res;
				write_mmr_sw_ack(mr);
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
static void bau_process_message(struct msg_desc *mdp, struct bau_control *bcp,
						int do_acknowledge)
{
	short socket_ack_count = 0;
	short *sp;
	struct atomic_short *asp;
	struct ptc_stats *stat = bcp->statp;
	struct bau_pq_entry *msg = mdp->msg;
	struct bau_control *smaster = bcp->socket_master;

	/*
	 * This must be a normal message, or retry of a normal message
	 */
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
		bau_process_retry_msg(mdp, bcp);

	/*
	 * This is a swack message, so we have to reply to it.
	 * Count each responding cpu on the socket. This avoids
	 * pinging the count's cache line back and forth between
	 * the sockets.
	 */
	sp = &smaster->socket_acknowledge_count[mdp->msg_slot];
	asp = (struct atomic_short *)sp;
	socket_ack_count = atom_asr(1, asp);
	if (socket_ack_count == bcp->cpus_in_socket) {
		int msg_ack_count;
		/*
		 * Both sockets dump their completed count total into
		 * the message's count.
		 */
		*sp = 0;
		asp = (struct atomic_short *)&msg->acknowledge_count;
		msg_ack_count = atom_asr(socket_ack_count, asp);

		if (msg_ack_count == bcp->cpus_in_uvhub) {
			/*
			 * All cpus in uvhub saw it; reply
			 * (unless we are in the UV2 workaround)
			 */
			reply_to_message(mdp, bcp, do_acknowledge);
		}
	}

	return;
}

/*
 * Determine the first cpu on a pnode.
 */
static int pnode_to_first_cpu(int pnode, struct bau_control *smaster)
{
	int cpu;
	struct hub_and_pnode *hpp;

	for_each_present_cpu(cpu) {
		hpp = &smaster->thp[cpu];
		if (pnode == hpp->pnode)
			return cpu;
	}
	return -1;
}

/*
 * Last resort when we get a large number of destination timeouts is
 * to clear resources held by a given cpu.
 * Do this with IPI so that all messages in the BAU message queue
 * can be identified by their nonzero swack_vec field.
 *
 * This is entered for a single cpu on the uvhub.
 * The sender want's this uvhub to free a specific message's
 * swack resources.
 */
static void do_reset(void *ptr)
{
	int i;
	struct bau_control *bcp = &per_cpu(bau_control, smp_processor_id());
	struct reset_args *rap = (struct reset_args *)ptr;
	struct bau_pq_entry *msg;
	struct ptc_stats *stat = bcp->statp;

	stat->d_resets++;
	/*
	 * We're looking for the given sender, and
	 * will free its swack resource.
	 * If all cpu's finally responded after the timeout, its
	 * message 'replied_to' was set.
	 */
	for (msg = bcp->queue_first, i = 0; i < DEST_Q_SIZE; msg++, i++) {
		unsigned long msg_res;
		/* do_reset: same conditions for cancellation as
		   bau_process_retry_msg() */
		if ((msg->replied_to == 0) &&
		    (msg->canceled == 0) &&
		    (msg->sending_cpu == rap->sender) &&
		    (msg->swack_vec) &&
		    (msg->msg_type != MSG_NOOP)) {
			unsigned long mmr;
			unsigned long mr;
			/*
			 * make everyone else ignore this message
			 */
			msg->canceled = 1;
			/*
			 * only reset the resource if it is still pending
			 */
			mmr = read_mmr_sw_ack();
			msg_res = msg->swack_vec;
			mr = (msg_res << UV_SW_ACK_NPENDING) | msg_res;
			if (mmr & msg_res) {
				stat->d_rcanceled++;
				write_mmr_sw_ack(mr);
			}
		}
	}
	return;
}

/*
 * Use IPI to get all target uvhubs to release resources held by
 * a given sending cpu number.
 */
static void reset_with_ipi(struct pnmask *distribution, struct bau_control *bcp)
{
	int pnode;
	int apnode;
	int maskbits;
	int sender = bcp->cpu;
	cpumask_t *mask = bcp->uvhub_master->cpumask;
	struct bau_control *smaster = bcp->socket_master;
	struct reset_args reset_args;

	reset_args.sender = sender;
	cpus_clear(*mask);
	/* find a single cpu for each uvhub in this distribution mask */
	maskbits = sizeof(struct pnmask) * BITSPERBYTE;
	/* each bit is a pnode relative to the partition base pnode */
	for (pnode = 0; pnode < maskbits; pnode++) {
		int cpu;
		if (!bau_uvhub_isset(pnode, distribution))
			continue;
		apnode = pnode + bcp->partition_base_pnode;
		cpu = pnode_to_first_cpu(apnode, smaster);
		cpu_set(cpu, *mask);
	}

	/* IPI all cpus; preemption is already disabled */
	smp_call_function_many(mask, do_reset, (void *)&reset_args, 1);
	return;
}

static inline unsigned long cycles_2_us(unsigned long long cyc)
{
	unsigned long long ns;
	unsigned long us;
	int cpu = smp_processor_id();

	ns =  (cyc * per_cpu(cyc2ns, cpu)) >> CYC2NS_SCALE_FACTOR;
	us = ns / 1000;
	return us;
}

/*
 * wait for all cpus on this hub to finish their sends and go quiet
 * leaves uvhub_quiesce set so that no new broadcasts are started by
 * bau_flush_send_and_wait()
 */
static inline void quiesce_local_uvhub(struct bau_control *hmaster)
{
	atom_asr(1, (struct atomic_short *)&hmaster->uvhub_quiesce);
}

/*
 * mark this quiet-requestor as done
 */
static inline void end_uvhub_quiesce(struct bau_control *hmaster)
{
	atom_asr(-1, (struct atomic_short *)&hmaster->uvhub_quiesce);
}

static unsigned long uv1_read_status(unsigned long mmr_offset, int right_shift)
{
	unsigned long descriptor_status;

	descriptor_status = uv_read_local_mmr(mmr_offset);
	descriptor_status >>= right_shift;
	descriptor_status &= UV_ACT_STATUS_MASK;
	return descriptor_status;
}

/*
 * Wait for completion of a broadcast software ack message
 * return COMPLETE, RETRY(PLUGGED or TIMEOUT) or GIVEUP
 */
static int uv1_wait_completion(struct bau_desc *bau_desc,
				unsigned long mmr_offset, int right_shift,
				struct bau_control *bcp, long try)
{
	unsigned long descriptor_status;
	cycles_t ttm;
	struct ptc_stats *stat = bcp->statp;

	descriptor_status = uv1_read_status(mmr_offset, right_shift);
	/* spin on the status MMR, waiting for it to go idle */
	while ((descriptor_status != DS_IDLE)) {
		/*
		 * Our software ack messages may be blocked because
		 * there are no swack resources available.  As long
		 * as none of them has timed out hardware will NACK
		 * our message and its state will stay IDLE.
		 */
		if (descriptor_status == DS_SOURCE_TIMEOUT) {
			stat->s_stimeout++;
			return FLUSH_GIVEUP;
		} else if (descriptor_status == DS_DESTINATION_TIMEOUT) {
			stat->s_dtimeout++;
			ttm = get_cycles();

			/*
			 * Our retries may be blocked by all destination
			 * swack resources being consumed, and a timeout
			 * pending.  In that case hardware returns the
			 * ERROR that looks like a destination timeout.
			 */
			if (cycles_2_us(ttm - bcp->send_message) < timeout_us) {
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
		descriptor_status = uv1_read_status(mmr_offset, right_shift);
	}
	bcp->conseccompletes++;
	return FLUSH_COMPLETE;
}

/*
 * UV2 could have an extra bit of status in the ACTIVATION_STATUS_2 register.
 * But not currently used.
 */
static unsigned long uv2_read_status(unsigned long offset, int rshft, int desc)
{
	unsigned long descriptor_status;

	descriptor_status =
		((read_lmmr(offset) >> rshft) & UV_ACT_STATUS_MASK) << 1;
	return descriptor_status;
}

/*
 * Return whether the status of the descriptor that is normally used for this
 * cpu (the one indexed by its hub-relative cpu number) is busy.
 * The status of the original 32 descriptors is always reflected in the 64
 * bits of UVH_LB_BAU_SB_ACTIVATION_STATUS_0.
 * The bit provided by the activation_status_2 register is irrelevant to
 * the status if it is only being tested for busy or not busy.
 */
int normal_busy(struct bau_control *bcp)
{
	int cpu = bcp->uvhub_cpu;
	int mmr_offset;
	int right_shift;

	mmr_offset = UVH_LB_BAU_SB_ACTIVATION_STATUS_0;
	right_shift = cpu * UV_ACT_STATUS_SIZE;
	return (((((read_lmmr(mmr_offset) >> right_shift) &
				UV_ACT_STATUS_MASK)) << 1) == UV2H_DESC_BUSY);
}

/*
 * Entered when a bau descriptor has gone into a permanent busy wait because
 * of a hardware bug.
 * Workaround the bug.
 */
int handle_uv2_busy(struct bau_control *bcp)
{
	struct ptc_stats *stat = bcp->statp;

	stat->s_uv2_wars++;
	bcp->busy = 1;
	return FLUSH_GIVEUP;
}

static int uv2_wait_completion(struct bau_desc *bau_desc,
				unsigned long mmr_offset, int right_shift,
				struct bau_control *bcp, long try)
{
	unsigned long descriptor_stat;
	cycles_t ttm;
	int desc = bcp->uvhub_cpu;
	long busy_reps = 0;
	struct ptc_stats *stat = bcp->statp;

	descriptor_stat = uv2_read_status(mmr_offset, right_shift, desc);

	/* spin on the status MMR, waiting for it to go idle */
	while (descriptor_stat != UV2H_DESC_IDLE) {
		if ((descriptor_stat == UV2H_DESC_SOURCE_TIMEOUT)) {
			/*
			 * A h/w bug on the destination side may
			 * have prevented the message being marked
			 * pending, thus it doesn't get replied to
			 * and gets continually nacked until it times
			 * out with a SOURCE_TIMEOUT.
			 */
			stat->s_stimeout++;
			return FLUSH_GIVEUP;
		} else if (descriptor_stat == UV2H_DESC_DEST_TIMEOUT) {
			ttm = get_cycles();

			/*
			 * Our retries may be blocked by all destination
			 * swack resources being consumed, and a timeout
			 * pending.  In that case hardware returns the
			 * ERROR that looks like a destination timeout.
			 * Without using the extended status we have to
			 * deduce from the short time that this was a
			 * strong nack.
			 */
			if (cycles_2_us(ttm - bcp->send_message) < timeout_us) {
				bcp->conseccompletes = 0;
				stat->s_plugged++;
				/* FLUSH_RETRY_PLUGGED causes hang on boot */
				return FLUSH_GIVEUP;
			}
			stat->s_dtimeout++;
			bcp->conseccompletes = 0;
			/* FLUSH_RETRY_TIMEOUT causes hang on boot */
			return FLUSH_GIVEUP;
		} else {
			busy_reps++;
			if (busy_reps > 1000000) {
				/* not to hammer on the clock */
				busy_reps = 0;
				ttm = get_cycles();
				if ((ttm - bcp->send_message) >
						bcp->timeout_interval)
					return handle_uv2_busy(bcp);
			}
			/*
			 * descriptor_stat is still BUSY
			 */
			cpu_relax();
		}
		descriptor_stat = uv2_read_status(mmr_offset, right_shift,
									desc);
	}
	bcp->conseccompletes++;
	return FLUSH_COMPLETE;
}

/*
 * There are 2 status registers; each and array[32] of 2 bits. Set up for
 * which register to read and position in that register based on cpu in
 * current hub.
 */
static int wait_completion(struct bau_desc *bau_desc,
				struct bau_control *bcp, long try)
{
	int right_shift;
	unsigned long mmr_offset;
	int desc = bcp->uvhub_cpu;

	if (desc < UV_CPUS_PER_AS) {
		mmr_offset = UVH_LB_BAU_SB_ACTIVATION_STATUS_0;
		right_shift = desc * UV_ACT_STATUS_SIZE;
	} else {
		mmr_offset = UVH_LB_BAU_SB_ACTIVATION_STATUS_1;
		right_shift = ((desc - UV_CPUS_PER_AS) * UV_ACT_STATUS_SIZE);
	}

	if (bcp->uvhub_version == 1)
		return uv1_wait_completion(bau_desc, mmr_offset, right_shift,
								bcp, try);
	else
		return uv2_wait_completion(bau_desc, mmr_offset, right_shift,
								bcp, try);
}

static inline cycles_t sec_2_cycles(unsigned long sec)
{
	unsigned long ns;
	cycles_t cyc;

	ns = sec * 1000000000;
	cyc = (ns << CYC2NS_SCALE_FACTOR)/(per_cpu(cyc2ns, smp_processor_id()));
	return cyc;
}

/*
 * Our retries are blocked by all destination sw ack resources being
 * in use, and a timeout is pending. In that case hardware immediately
 * returns the ERROR that looks like a destination timeout.
 */
static void destination_plugged(struct bau_desc *bau_desc,
			struct bau_control *bcp,
			struct bau_control *hmaster, struct ptc_stats *stat)
{
	udelay(bcp->plugged_delay);
	bcp->plugged_tries++;

	if (bcp->plugged_tries >= bcp->plugsb4reset) {
		bcp->plugged_tries = 0;

		quiesce_local_uvhub(hmaster);

		spin_lock(&hmaster->queue_lock);
		reset_with_ipi(&bau_desc->distribution, bcp);
		spin_unlock(&hmaster->queue_lock);

		end_uvhub_quiesce(hmaster);

		bcp->ipi_attempts++;
		stat->s_resets_plug++;
	}
}

static void destination_timeout(struct bau_desc *bau_desc,
			struct bau_control *bcp, struct bau_control *hmaster,
			struct ptc_stats *stat)
{
	hmaster->max_concurr = 1;
	bcp->timeout_tries++;
	if (bcp->timeout_tries >= bcp->timeoutsb4reset) {
		bcp->timeout_tries = 0;

		quiesce_local_uvhub(hmaster);

		spin_lock(&hmaster->queue_lock);
		reset_with_ipi(&bau_desc->distribution, bcp);
		spin_unlock(&hmaster->queue_lock);

		end_uvhub_quiesce(hmaster);

		bcp->ipi_attempts++;
		stat->s_resets_timeout++;
	}
}

/*
 * Stop all cpus on a uvhub from using the BAU for a period of time.
 * This is reversed by check_enable.
 */
static void disable_for_period(struct bau_control *bcp, struct ptc_stats *stat)
{
	int tcpu;
	struct bau_control *tbcp;
	struct bau_control *hmaster;
	cycles_t tm1;

	hmaster = bcp->uvhub_master;
	spin_lock(&hmaster->disable_lock);
	if (!bcp->baudisabled) {
		stat->s_bau_disabled++;
		tm1 = get_cycles();
		for_each_present_cpu(tcpu) {
			tbcp = &per_cpu(bau_control, tcpu);
			if (tbcp->uvhub_master == hmaster) {
				tbcp->baudisabled = 1;
				tbcp->set_bau_on_time =
					tm1 + bcp->disabled_period;
			}
		}
	}
	spin_unlock(&hmaster->disable_lock);
}

static void count_max_concurr(int stat, struct bau_control *bcp,
				struct bau_control *hmaster)
{
	bcp->plugged_tries = 0;
	bcp->timeout_tries = 0;
	if (stat != FLUSH_COMPLETE)
		return;
	if (bcp->conseccompletes <= bcp->complete_threshold)
		return;
	if (hmaster->max_concurr >= hmaster->max_concurr_const)
		return;
	hmaster->max_concurr++;
}

static void record_send_stats(cycles_t time1, cycles_t time2,
		struct bau_control *bcp, struct ptc_stats *stat,
		int completion_status, int try)
{
	cycles_t elapsed;

	if (time2 > time1) {
		elapsed = time2 - time1;
		stat->s_time += elapsed;

		if ((completion_status == FLUSH_COMPLETE) && (try == 1)) {
			bcp->period_requests++;
			bcp->period_time += elapsed;
			if ((elapsed > congested_cycles) &&
			    (bcp->period_requests > bcp->cong_reps) &&
			    ((bcp->period_time / bcp->period_requests) >
							congested_cycles)) {
				stat->s_congested++;
				disable_for_period(bcp, stat);
			}
		}
	} else
		stat->s_requestor--;

	if (completion_status == FLUSH_COMPLETE && try > 1)
		stat->s_retriesok++;
	else if (completion_status == FLUSH_GIVEUP) {
		stat->s_giveup++;
		if (get_cycles() > bcp->period_end)
			bcp->period_giveups = 0;
		bcp->period_giveups++;
		if (bcp->period_giveups == 1)
			bcp->period_end = get_cycles() + bcp->disabled_period;
		if (bcp->period_giveups > bcp->giveup_limit) {
			disable_for_period(bcp, stat);
			stat->s_giveuplimit++;
		}
	}
}

/*
 * Because of a uv1 hardware bug only a limited number of concurrent
 * requests can be made.
 */
static void uv1_throttle(struct bau_control *hmaster, struct ptc_stats *stat)
{
	spinlock_t *lock = &hmaster->uvhub_lock;
	atomic_t *v;

	v = &hmaster->active_descriptor_count;
	if (!atomic_inc_unless_ge(lock, v, hmaster->max_concurr)) {
		stat->s_throttles++;
		do {
			cpu_relax();
		} while (!atomic_inc_unless_ge(lock, v, hmaster->max_concurr));
	}
}

/*
 * Handle the completion status of a message send.
 */
static void handle_cmplt(int completion_status, struct bau_desc *bau_desc,
			struct bau_control *bcp, struct bau_control *hmaster,
			struct ptc_stats *stat)
{
	if (completion_status == FLUSH_RETRY_PLUGGED)
		destination_plugged(bau_desc, bcp, hmaster, stat);
	else if (completion_status == FLUSH_RETRY_TIMEOUT)
		destination_timeout(bau_desc, bcp, hmaster, stat);
}

/*
 * Send a broadcast and wait for it to complete.
 *
 * The flush_mask contains the cpus the broadcast is to be sent to including
 * cpus that are on the local uvhub.
 *
 * Returns 0 if all flushing represented in the mask was done.
 * Returns 1 if it gives up entirely and the original cpu mask is to be
 * returned to the kernel.
 */
int uv_flush_send_and_wait(struct cpumask *flush_mask, struct bau_control *bcp,
	struct bau_desc *bau_desc)
{
	int seq_number = 0;
	int completion_stat = 0;
	int uv1 = 0;
	long try = 0;
	unsigned long index;
	cycles_t time1;
	cycles_t time2;
	struct ptc_stats *stat = bcp->statp;
	struct bau_control *hmaster = bcp->uvhub_master;
	struct uv1_bau_msg_header *uv1_hdr = NULL;
	struct uv2_bau_msg_header *uv2_hdr = NULL;

	if (bcp->uvhub_version == 1) {
		uv1 = 1;
		uv1_throttle(hmaster, stat);
	}

	while (hmaster->uvhub_quiesce)
		cpu_relax();

	time1 = get_cycles();
	if (uv1)
		uv1_hdr = &bau_desc->header.uv1_hdr;
	else
		uv2_hdr = &bau_desc->header.uv2_hdr;

	do {
		if (try == 0) {
			if (uv1)
				uv1_hdr->msg_type = MSG_REGULAR;
			else
				uv2_hdr->msg_type = MSG_REGULAR;
			seq_number = bcp->message_number++;
		} else {
			if (uv1)
				uv1_hdr->msg_type = MSG_RETRY;
			else
				uv2_hdr->msg_type = MSG_RETRY;
			stat->s_retry_messages++;
		}

		if (uv1)
			uv1_hdr->sequence = seq_number;
		else
			uv2_hdr->sequence = seq_number;
		index = (1UL << AS_PUSH_SHIFT) | bcp->uvhub_cpu;
		bcp->send_message = get_cycles();

		write_mmr_activation(index);

		try++;
		completion_stat = wait_completion(bau_desc, bcp, try);

		handle_cmplt(completion_stat, bau_desc, bcp, hmaster, stat);

		if (bcp->ipi_attempts >= bcp->ipi_reset_limit) {
			bcp->ipi_attempts = 0;
			stat->s_overipilimit++;
			completion_stat = FLUSH_GIVEUP;
			break;
		}
		cpu_relax();
	} while ((completion_stat == FLUSH_RETRY_PLUGGED) ||
		 (completion_stat == FLUSH_RETRY_TIMEOUT));

	time2 = get_cycles();

	count_max_concurr(completion_stat, bcp, hmaster);

	while (hmaster->uvhub_quiesce)
		cpu_relax();

	atomic_dec(&hmaster->active_descriptor_count);

	record_send_stats(time1, time2, bcp, stat, completion_stat, try);

	if (completion_stat == FLUSH_GIVEUP)
		/* FLUSH_GIVEUP will fall back to using IPI's for tlb flush */
		return 1;
	return 0;
}

/*
 * The BAU is disabled for this uvhub. When the disabled time period has
 * expired re-enable it.
 * Return 0 if it is re-enabled for all cpus on this uvhub.
 */
static int check_enable(struct bau_control *bcp, struct ptc_stats *stat)
{
	int tcpu;
	struct bau_control *tbcp;
	struct bau_control *hmaster;

	hmaster = bcp->uvhub_master;
	spin_lock(&hmaster->disable_lock);
	if (bcp->baudisabled && (get_cycles() >= bcp->set_bau_on_time)) {
		stat->s_bau_reenabled++;
		for_each_present_cpu(tcpu) {
			tbcp = &per_cpu(bau_control, tcpu);
			if (tbcp->uvhub_master == hmaster) {
				tbcp->baudisabled = 0;
				tbcp->period_requests = 0;
				tbcp->period_time = 0;
				tbcp->period_giveups = 0;
			}
		}
		spin_unlock(&hmaster->disable_lock);
		return 0;
	}
	spin_unlock(&hmaster->disable_lock);
	return -1;
}

static void record_send_statistics(struct ptc_stats *stat, int locals, int hubs,
				int remotes, struct bau_desc *bau_desc)
{
	stat->s_requestor++;
	stat->s_ntargcpu += remotes + locals;
	stat->s_ntargremotes += remotes;
	stat->s_ntarglocals += locals;

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
}

/*
 * Translate a cpu mask to the uvhub distribution mask in the BAU
 * activation descriptor.
 */
static int set_distrib_bits(struct cpumask *flush_mask, struct bau_control *bcp,
			struct bau_desc *bau_desc, int *localsp, int *remotesp)
{
	int cpu;
	int pnode;
	int cnt = 0;
	struct hub_and_pnode *hpp;

	for_each_cpu(cpu, flush_mask) {
		/*
		 * The distribution vector is a bit map of pnodes, relative
		 * to the partition base pnode (and the partition base nasid
		 * in the header).
		 * Translate cpu to pnode and hub using a local memory array.
		 */
		hpp = &bcp->socket_master->thp[cpu];
		pnode = hpp->pnode - bcp->partition_base_pnode;
		bau_uvhub_set(pnode, &bau_desc->distribution);
		cnt++;
		if (hpp->uvhub == bcp->uvhub)
			(*localsp)++;
		else
			(*remotesp)++;
	}
	if (!cnt)
		return 1;
	return 0;
}

/*
 * globally purge translation cache of a virtual address or all TLB's
 * @cpumask: mask of all cpu's in which the address is to be removed
 * @mm: mm_struct containing virtual address range
 * @start: start virtual address to be removed from TLB
 * @end: end virtual address to be remove from TLB
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
				struct mm_struct *mm, unsigned long start,
				unsigned long end, unsigned int cpu)
{
	int locals = 0;
	int remotes = 0;
	int hubs = 0;
	struct bau_desc *bau_desc;
	struct cpumask *flush_mask;
	struct ptc_stats *stat;
	struct bau_control *bcp;
	unsigned long descriptor_status;
	unsigned long status;

	bcp = &per_cpu(bau_control, cpu);
	stat = bcp->statp;
	stat->s_enters++;

	if (bcp->nobau)
		return cpumask;

	if (bcp->busy) {
		descriptor_status =
			read_lmmr(UVH_LB_BAU_SB_ACTIVATION_STATUS_0);
		status = ((descriptor_status >> (bcp->uvhub_cpu *
			UV_ACT_STATUS_SIZE)) & UV_ACT_STATUS_MASK) << 1;
		if (status == UV2H_DESC_BUSY)
			return cpumask;
		bcp->busy = 0;
	}

	/* bau was disabled due to slow response */
	if (bcp->baudisabled) {
		if (check_enable(bcp, stat)) {
			stat->s_ipifordisabled++;
			return cpumask;
		}
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
	bau_desc += (ITEMS_PER_DESC * bcp->uvhub_cpu);
	bau_uvhubs_clear(&bau_desc->distribution, UV_DISTRIBUTION_SIZE);
	if (set_distrib_bits(flush_mask, bcp, bau_desc, &locals, &remotes))
		return NULL;

	record_send_statistics(stat, locals, hubs, remotes, bau_desc);

	if (!end || (end - start) <= PAGE_SIZE)
		bau_desc->payload.address = start;
	else
		bau_desc->payload.address = TLB_FLUSH_ALL;
	bau_desc->payload.sending_cpu = cpu;
	/*
	 * uv_flush_send_and_wait returns 0 if all cpu's were messaged,
	 * or 1 if it gave up and the original cpumask should be returned.
	 */
	if (!uv_flush_send_and_wait(flush_mask, bcp, bau_desc))
		return NULL;
	else
		return cpumask;
}

/*
 * Search the message queue for any 'other' unprocessed message with the
 * same software acknowledge resource bit vector as the 'msg' message.
 */
struct bau_pq_entry *find_another_by_swack(struct bau_pq_entry *msg,
					   struct bau_control *bcp)
{
	struct bau_pq_entry *msg_next = msg + 1;
	unsigned char swack_vec = msg->swack_vec;

	if (msg_next > bcp->queue_last)
		msg_next = bcp->queue_first;
	while (msg_next != msg) {
		if ((msg_next->canceled == 0) && (msg_next->replied_to == 0) &&
				(msg_next->swack_vec == swack_vec))
			return msg_next;
		msg_next++;
		if (msg_next > bcp->queue_last)
			msg_next = bcp->queue_first;
	}
	return NULL;
}

/*
 * UV2 needs to work around a bug in which an arriving message has not
 * set a bit in the UVH_LB_BAU_INTD_SOFTWARE_ACKNOWLEDGE register.
 * Such a message must be ignored.
 */
void process_uv2_message(struct msg_desc *mdp, struct bau_control *bcp)
{
	unsigned long mmr_image;
	unsigned char swack_vec;
	struct bau_pq_entry *msg = mdp->msg;
	struct bau_pq_entry *other_msg;

	mmr_image = read_mmr_sw_ack();
	swack_vec = msg->swack_vec;

	if ((swack_vec & mmr_image) == 0) {
		/*
		 * This message was assigned a swack resource, but no
		 * reserved acknowlegment is pending.
		 * The bug has prevented this message from setting the MMR.
		 */
		/*
		 * Some message has set the MMR 'pending' bit; it might have
		 * been another message.  Look for that message.
		 */
		other_msg = find_another_by_swack(msg, bcp);
		if (other_msg) {
			/*
			 * There is another. Process this one but do not
			 * ack it.
			 */
			bau_process_message(mdp, bcp, 0);
			/*
			 * Let the natural processing of that other message
			 * acknowledge it. Don't get the processing of sw_ack's
			 * out of order.
			 */
			return;
		}
	}

	/*
	 * Either the MMR shows this one pending a reply or there is no
	 * other message using this sw_ack, so it is safe to acknowledge it.
	 */
	bau_process_message(mdp, bcp, 1);

	return;
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
	struct bau_pq_entry *msg;
	struct bau_control *bcp;
	struct ptc_stats *stat;
	struct msg_desc msgdesc;

	ack_APIC_irq();
	time_start = get_cycles();

	bcp = &per_cpu(bau_control, smp_processor_id());
	stat = bcp->statp;

	msgdesc.queue_first = bcp->queue_first;
	msgdesc.queue_last = bcp->queue_last;

	msg = bcp->bau_msg_head;
	while (msg->swack_vec) {
		count++;

		msgdesc.msg_slot = msg - msgdesc.queue_first;
		msgdesc.msg = msg;
		if (bcp->uvhub_version == 2)
			process_uv2_message(&msgdesc, bcp);
		else
			bau_process_message(&msgdesc, bcp, 1);

		msg++;
		if (msg > msgdesc.queue_last)
			msg = msgdesc.queue_first;
		bcp->bau_msg_head = msg;
	}
	stat->d_time += (get_cycles() - time_start);
	if (!count)
		stat->d_nomsg++;
	else if (count > 1)
		stat->d_multmsg++;
}

/*
 * Each target uvhub (i.e. a uvhub that has cpu's) needs to have
 * shootdown message timeouts enabled.  The timeout does not cause
 * an interrupt, but causes an error message to be returned to
 * the sender.
 */
static void __init enable_timeouts(void)
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
		mmr_image = read_mmr_misc_control(pnode);
		/*
		 * Set the timeout period and then lock it in, in three
		 * steps; captures and locks in the period.
		 *
		 * To program the period, the SOFT_ACK_MODE must be off.
		 */
		mmr_image &= ~(1L << SOFTACK_MSHIFT);
		write_mmr_misc_control(pnode, mmr_image);
		/*
		 * Set the 4-bit period.
		 */
		mmr_image &= ~((unsigned long)0xf << SOFTACK_PSHIFT);
		mmr_image |= (SOFTACK_TIMEOUT_PERIOD << SOFTACK_PSHIFT);
		write_mmr_misc_control(pnode, mmr_image);
		/*
		 * UV1:
		 * Subsequent reversals of the timebase bit (3) cause an
		 * immediate timeout of one or all INTD resources as
		 * indicated in bits 2:0 (7 causes all of them to timeout).
		 */
		mmr_image |= (1L << SOFTACK_MSHIFT);
		if (is_uv2_hub()) {
			/* hw bug workaround; do not use extended status */
			mmr_image &= ~(1L << UV2_EXT_SHFT);
		}
		write_mmr_misc_control(pnode, mmr_image);
	}
}

static void *ptc_seq_start(struct seq_file *file, loff_t *offset)
{
	if (*offset < num_possible_cpus())
		return offset;
	return NULL;
}

static void *ptc_seq_next(struct seq_file *file, void *data, loff_t *offset)
{
	(*offset)++;
	if (*offset < num_possible_cpus())
		return offset;
	return NULL;
}

static void ptc_seq_stop(struct seq_file *file, void *data)
{
}

static inline unsigned long long usec_2_cycles(unsigned long microsec)
{
	unsigned long ns;
	unsigned long long cyc;

	ns = microsec * 1000;
	cyc = (ns << CYC2NS_SCALE_FACTOR)/(per_cpu(cyc2ns, smp_processor_id()));
	return cyc;
}

/*
 * Display the statistics thru /proc/sgi_uv/ptc_statistics
 * 'data' points to the cpu number
 * Note: see the descriptions in stat_description[].
 */
static int ptc_seq_show(struct seq_file *file, void *data)
{
	struct ptc_stats *stat;
	struct bau_control *bcp;
	int cpu;

	cpu = *(loff_t *)data;
	if (!cpu) {
		seq_printf(file,
		 "# cpu bauoff sent stime self locals remotes ncpus localhub ");
		seq_printf(file,
			"remotehub numuvhubs numuvhubs16 numuvhubs8 ");
		seq_printf(file,
			"numuvhubs4 numuvhubs2 numuvhubs1 dto snacks retries ");
		seq_printf(file,
			"rok resetp resett giveup sto bz throt disable ");
		seq_printf(file,
			"enable wars warshw warwaits enters ipidis plugged ");
		seq_printf(file,
			"ipiover glim cong swack recv rtime all one mult ");
		seq_printf(file,
			"none retry canc nocan reset rcan\n");
	}
	if (cpu < num_possible_cpus() && cpu_online(cpu)) {
		bcp = &per_cpu(bau_control, cpu);
		stat = bcp->statp;
		/* source side statistics */
		seq_printf(file,
			"cpu %d %d %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld ",
			   cpu, bcp->nobau, stat->s_requestor,
			   cycles_2_us(stat->s_time),
			   stat->s_ntargself, stat->s_ntarglocals,
			   stat->s_ntargremotes, stat->s_ntargcpu,
			   stat->s_ntarglocaluvhub, stat->s_ntargremoteuvhub,
			   stat->s_ntarguvhub, stat->s_ntarguvhub16);
		seq_printf(file, "%ld %ld %ld %ld %ld %ld ",
			   stat->s_ntarguvhub8, stat->s_ntarguvhub4,
			   stat->s_ntarguvhub2, stat->s_ntarguvhub1,
			   stat->s_dtimeout, stat->s_strongnacks);
		seq_printf(file, "%ld %ld %ld %ld %ld %ld %ld %ld ",
			   stat->s_retry_messages, stat->s_retriesok,
			   stat->s_resets_plug, stat->s_resets_timeout,
			   stat->s_giveup, stat->s_stimeout,
			   stat->s_busy, stat->s_throttles);
		seq_printf(file, "%ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld ",
			   stat->s_bau_disabled, stat->s_bau_reenabled,
			   stat->s_uv2_wars, stat->s_uv2_wars_hw,
			   stat->s_uv2_war_waits, stat->s_enters,
			   stat->s_ipifordisabled, stat->s_plugged,
			   stat->s_overipilimit, stat->s_giveuplimit,
			   stat->s_congested);

		/* destination side statistics */
		seq_printf(file,
			"%lx %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld %ld\n",
			   read_gmmr_sw_ack(uv_cpu_to_pnode(cpu)),
			   stat->d_requestee, cycles_2_us(stat->d_time),
			   stat->d_alltlb, stat->d_onetlb, stat->d_multmsg,
			   stat->d_nomsg, stat->d_retries, stat->d_canceled,
			   stat->d_nocanceled, stat->d_resets,
			   stat->d_rcanceled);
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

	buf = kasprintf(GFP_KERNEL, "%s %s %s\n%d %d %d %d %d %d %d %d %d %d\n",
		"max_concur plugged_delay plugsb4reset timeoutsb4reset",
		"ipi_reset_limit complete_threshold congested_response_us",
		"congested_reps disabled_period giveup_limit",
		max_concurr, plugged_delay, plugsb4reset,
		timeoutsb4reset, ipi_reset_limit, complete_threshold,
		congested_respns_us, congested_reps, disabled_period,
		giveup_limit);

	if (!buf)
		return -ENOMEM;

	ret = simple_read_from_buffer(userbuf, count, ppos, buf, strlen(buf));
	kfree(buf);
	return ret;
}

/*
 * handle a write to /proc/sgi_uv/ptc_statistics
 * -1: reset the statistics
 *  0: display meaning of the statistics
 */
static ssize_t ptc_proc_write(struct file *file, const char __user *user,
				size_t count, loff_t *data)
{
	int cpu;
	int i;
	int elements;
	long input_arg;
	char optstr[64];
	struct ptc_stats *stat;

	if (count == 0 || count > sizeof(optstr))
		return -EINVAL;
	if (copy_from_user(optstr, user, count))
		return -EFAULT;
	optstr[count - 1] = '\0';

	if (!strcmp(optstr, "on")) {
		set_bau_on();
		return count;
	} else if (!strcmp(optstr, "off")) {
		set_bau_off();
		return count;
	}

	if (strict_strtol(optstr, 10, &input_arg) < 0) {
		printk(KERN_DEBUG "%s is invalid\n", optstr);
		return -EINVAL;
	}

	if (input_arg == 0) {
		elements = ARRAY_SIZE(stat_description);
		printk(KERN_DEBUG "# cpu:      cpu number\n");
		printk(KERN_DEBUG "Sender statistics:\n");
		for (i = 0; i < elements; i++)
			printk(KERN_DEBUG "%s\n", stat_description[i]);
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
 * Parse the values written to /sys/kernel/debug/sgi_uv/bau_tunables.
 * Zero values reset them to defaults.
 */
static int parse_tunables_write(struct bau_control *bcp, char *instr,
				int count)
{
	char *p;
	char *q;
	int cnt = 0;
	int val;
	int e = ARRAY_SIZE(tunables);

	p = instr + strspn(instr, WHITESPACE);
	q = p;
	for (; *p; p = q + strspn(q, WHITESPACE)) {
		q = p + strcspn(p, WHITESPACE);
		cnt++;
		if (q == p)
			break;
	}
	if (cnt != e) {
		printk(KERN_INFO "bau tunable error: should be %d values\n", e);
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
				max_concurr = MAX_BAU_CONCURRENT;
				max_concurr_const = MAX_BAU_CONCURRENT;
				continue;
			}
			if (val < 1 || val > bcp->cpus_in_uvhub) {
				printk(KERN_DEBUG
				"Error: BAU max concurrent %d is invalid\n",
				val);
				return -EINVAL;
			}
			max_concurr = val;
			max_concurr_const = val;
			continue;
		default:
			if (val == 0)
				*tunables[cnt].tunp = tunables[cnt].deflt;
			else
				*tunables[cnt].tunp = val;
			continue;
		}
		if (q == p)
			break;
	}
	return 0;
}

/*
 * Handle a write to debugfs. (/sys/kernel/debug/sgi_uv/bau_tunables)
 */
static ssize_t tunables_write(struct file *file, const char __user *user,
				size_t count, loff_t *data)
{
	int cpu;
	int ret;
	char instr[100];
	struct bau_control *bcp;

	if (count == 0 || count > sizeof(instr)-1)
		return -EINVAL;
	if (copy_from_user(instr, user, count))
		return -EFAULT;

	instr[count] = '\0';

	cpu = get_cpu();
	bcp = &per_cpu(bau_control, cpu);
	ret = parse_tunables_write(bcp, instr, count);
	put_cpu();
	if (ret)
		return ret;

	for_each_present_cpu(cpu) {
		bcp = &per_cpu(bau_control, cpu);
		bcp->max_concurr =		max_concurr;
		bcp->max_concurr_const =	max_concurr;
		bcp->plugged_delay =		plugged_delay;
		bcp->plugsb4reset =		plugsb4reset;
		bcp->timeoutsb4reset =		timeoutsb4reset;
		bcp->ipi_reset_limit =		ipi_reset_limit;
		bcp->complete_threshold =	complete_threshold;
		bcp->cong_response_us =		congested_respns_us;
		bcp->cong_reps =		congested_reps;
		bcp->disabled_period =		sec_2_cycles(disabled_period);
		bcp->giveup_limit =		giveup_limit;
	}
	return count;
}

static const struct seq_operations uv_ptc_seq_ops = {
	.start		= ptc_seq_start,
	.next		= ptc_seq_next,
	.stop		= ptc_seq_stop,
	.show		= ptc_seq_show
};

static int ptc_proc_open(struct inode *inode, struct file *file)
{
	return seq_open(file, &uv_ptc_seq_ops);
}

static int tunables_open(struct inode *inode, struct file *file)
{
	return 0;
}

static const struct file_operations proc_uv_ptc_operations = {
	.open		= ptc_proc_open,
	.read		= seq_read,
	.write		= ptc_proc_write,
	.llseek		= seq_lseek,
	.release	= seq_release,
};

static const struct file_operations tunables_fops = {
	.open		= tunables_open,
	.read		= tunables_read,
	.write		= tunables_write,
	.llseek		= default_llseek,
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
 * Initialize the sending side's sending buffers.
 */
static void activation_descriptor_init(int node, int pnode, int base_pnode)
{
	int i;
	int cpu;
	int uv1 = 0;
	unsigned long gpa;
	unsigned long m;
	unsigned long n;
	size_t dsize;
	struct bau_desc *bau_desc;
	struct bau_desc *bd2;
	struct uv1_bau_msg_header *uv1_hdr;
	struct uv2_bau_msg_header *uv2_hdr;
	struct bau_control *bcp;

	/*
	 * each bau_desc is 64 bytes; there are 8 (ITEMS_PER_DESC)
	 * per cpu; and one per cpu on the uvhub (ADP_SZ)
	 */
	dsize = sizeof(struct bau_desc) * ADP_SZ * ITEMS_PER_DESC;
	bau_desc = kmalloc_node(dsize, GFP_KERNEL, node);
	BUG_ON(!bau_desc);

	gpa = uv_gpa(bau_desc);
	n = uv_gpa_to_gnode(gpa);
	m = uv_gpa_to_offset(gpa);
	if (is_uv1_hub())
		uv1 = 1;

	/* the 14-bit pnode */
	write_mmr_descriptor_base(pnode, (n << UV_DESC_PSHIFT | m));
	/*
	 * Initializing all 8 (ITEMS_PER_DESC) descriptors for each
	 * cpu even though we only use the first one; one descriptor can
	 * describe a broadcast to 256 uv hubs.
	 */
	for (i = 0, bd2 = bau_desc; i < (ADP_SZ * ITEMS_PER_DESC); i++, bd2++) {
		memset(bd2, 0, sizeof(struct bau_desc));
		if (uv1) {
			uv1_hdr = &bd2->header.uv1_hdr;
			uv1_hdr->swack_flag =	1;
			/*
			 * The base_dest_nasid set in the message header
			 * is the nasid of the first uvhub in the partition.
			 * The bit map will indicate destination pnode numbers
			 * relative to that base. They may not be consecutive
			 * if nasid striding is being used.
			 */
			uv1_hdr->base_dest_nasid =
						UV_PNODE_TO_NASID(base_pnode);
			uv1_hdr->dest_subnodeid =	UV_LB_SUBNODEID;
			uv1_hdr->command =		UV_NET_ENDPOINT_INTD;
			uv1_hdr->int_both =		1;
			/*
			 * all others need to be set to zero:
			 *   fairness chaining multilevel count replied_to
			 */
		} else {
			/*
			 * BIOS uses legacy mode, but UV2 hardware always
			 * uses native mode for selective broadcasts.
			 */
			uv2_hdr = &bd2->header.uv2_hdr;
			uv2_hdr->swack_flag =	1;
			uv2_hdr->base_dest_nasid =
						UV_PNODE_TO_NASID(base_pnode);
			uv2_hdr->dest_subnodeid =	UV_LB_SUBNODEID;
			uv2_hdr->command =		UV_NET_ENDPOINT_INTD;
		}
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
static void pq_init(int node, int pnode)
{
	int cpu;
	size_t plsize;
	char *cp;
	void *vp;
	unsigned long pn;
	unsigned long first;
	unsigned long pn_first;
	unsigned long last;
	struct bau_pq_entry *pqp;
	struct bau_control *bcp;

	plsize = (DEST_Q_SIZE + 1) * sizeof(struct bau_pq_entry);
	vp = kmalloc_node(plsize, GFP_KERNEL, node);
	pqp = (struct bau_pq_entry *)vp;
	BUG_ON(!pqp);

	cp = (char *)pqp + 31;
	pqp = (struct bau_pq_entry *)(((unsigned long)cp >> 5) << 5);

	for_each_present_cpu(cpu) {
		if (pnode != uv_cpu_to_pnode(cpu))
			continue;
		/* for every cpu on this pnode: */
		bcp = &per_cpu(bau_control, cpu);
		bcp->queue_first	= pqp;
		bcp->bau_msg_head	= pqp;
		bcp->queue_last		= pqp + (DEST_Q_SIZE - 1);
	}
	/*
	 * need the gnode of where the memory was really allocated
	 */
	pn = uv_gpa_to_gnode(uv_gpa(pqp));
	first = uv_physnodeaddr(pqp);
	pn_first = ((unsigned long)pn << UV_PAYLOADQ_PNODE_SHIFT) | first;
	last = uv_physnodeaddr(pqp + (DEST_Q_SIZE - 1));
	write_mmr_payload_first(pnode, pn_first);
	write_mmr_payload_tail(pnode, first);
	write_mmr_payload_last(pnode, last);
	write_gmmr_sw_ack(pnode, 0xffffUL);

	/* in effect, all msg_type's are set to MSG_NOOP */
	memset(pqp, 0, sizeof(struct bau_pq_entry) * DEST_Q_SIZE);
}

/*
 * Initialization of each UV hub's structures
 */
static void __init init_uvhub(int uvhub, int vector, int base_pnode)
{
	int node;
	int pnode;
	unsigned long apicid;

	node = uvhub_to_first_node(uvhub);
	pnode = uv_blade_to_pnode(uvhub);

	activation_descriptor_init(node, pnode, base_pnode);

	pq_init(node, pnode);
	/*
	 * The below initialization can't be in firmware because the
	 * messaging IRQ will be determined by the OS.
	 */
	apicid = uvhub_to_first_apicid(uvhub) | uv_apicid_hibits;
	write_mmr_data_config(pnode, ((apicid << 32) | vector));
}

/*
 * We will set BAU_MISC_CONTROL with a timeout period.
 * But the BIOS has set UVH_AGING_PRESCALE_SEL and UVH_TRANSACTION_TIMEOUT.
 * So the destination timeout period has to be calculated from them.
 */
static int calculate_destination_timeout(void)
{
	unsigned long mmr_image;
	int mult1;
	int mult2;
	int index;
	int base;
	int ret;
	unsigned long ts_ns;

	if (is_uv1_hub()) {
		mult1 = SOFTACK_TIMEOUT_PERIOD & BAU_MISC_CONTROL_MULT_MASK;
		mmr_image = uv_read_local_mmr(UVH_AGING_PRESCALE_SEL);
		index = (mmr_image >> BAU_URGENCY_7_SHIFT) & BAU_URGENCY_7_MASK;
		mmr_image = uv_read_local_mmr(UVH_TRANSACTION_TIMEOUT);
		mult2 = (mmr_image >> BAU_TRANS_SHIFT) & BAU_TRANS_MASK;
		ts_ns = timeout_base_ns[index];
		ts_ns *= (mult1 * mult2);
		ret = ts_ns / 1000;
	} else {
		/* 4 bits  0/1 for 10/80us base, 3 bits of multiplier */
		mmr_image = uv_read_local_mmr(UVH_LB_BAU_MISC_CONTROL);
		mmr_image = (mmr_image & UV_SA_MASK) >> UV_SA_SHFT;
		if (mmr_image & (1L << UV2_ACK_UNITS_SHFT))
			base = 80;
		else
			base = 10;
		mult1 = mmr_image & UV2_ACK_MASK;
		ret = mult1 * base;
	}
	return ret;
}

static void __init init_per_cpu_tunables(void)
{
	int cpu;
	struct bau_control *bcp;

	for_each_present_cpu(cpu) {
		bcp = &per_cpu(bau_control, cpu);
		bcp->baudisabled		= 0;
		if (nobau)
			bcp->nobau		= 1;
		bcp->statp			= &per_cpu(ptcstats, cpu);
		/* time interval to catch a hardware stay-busy bug */
		bcp->timeout_interval		= usec_2_cycles(2*timeout_us);
		bcp->max_concurr		= max_concurr;
		bcp->max_concurr_const		= max_concurr;
		bcp->plugged_delay		= plugged_delay;
		bcp->plugsb4reset		= plugsb4reset;
		bcp->timeoutsb4reset		= timeoutsb4reset;
		bcp->ipi_reset_limit		= ipi_reset_limit;
		bcp->complete_threshold		= complete_threshold;
		bcp->cong_response_us		= congested_respns_us;
		bcp->cong_reps			= congested_reps;
		bcp->disabled_period =		sec_2_cycles(disabled_period);
		bcp->giveup_limit =		giveup_limit;
		spin_lock_init(&bcp->queue_lock);
		spin_lock_init(&bcp->uvhub_lock);
		spin_lock_init(&bcp->disable_lock);
	}
}

/*
 * Scan all cpus to collect blade and socket summaries.
 */
static int __init get_cpu_topology(int base_pnode,
					struct uvhub_desc *uvhub_descs,
					unsigned char *uvhub_mask)
{
	int cpu;
	int pnode;
	int uvhub;
	int socket;
	struct bau_control *bcp;
	struct uvhub_desc *bdp;
	struct socket_desc *sdp;

	for_each_present_cpu(cpu) {
		bcp = &per_cpu(bau_control, cpu);

		memset(bcp, 0, sizeof(struct bau_control));

		pnode = uv_cpu_hub_info(cpu)->pnode;
		if ((pnode - base_pnode) >= UV_DISTRIBUTION_SIZE) {
			printk(KERN_EMERG
				"cpu %d pnode %d-%d beyond %d; BAU disabled\n",
				cpu, pnode, base_pnode, UV_DISTRIBUTION_SIZE);
			return 1;
		}

		bcp->osnode = cpu_to_node(cpu);
		bcp->partition_base_pnode = base_pnode;

		uvhub = uv_cpu_hub_info(cpu)->numa_blade_id;
		*(uvhub_mask + (uvhub/8)) |= (1 << (uvhub%8));
		bdp = &uvhub_descs[uvhub];

		bdp->num_cpus++;
		bdp->uvhub = uvhub;
		bdp->pnode = pnode;

		/* kludge: 'assuming' one node per socket, and assuming that
		   disabling a socket just leaves a gap in node numbers */
		socket = bcp->osnode & 1;
		bdp->socket_mask |= (1 << socket);
		sdp = &bdp->socket[socket];
		sdp->cpu_number[sdp->num_cpus] = cpu;
		sdp->num_cpus++;
		if (sdp->num_cpus > MAX_CPUS_PER_SOCKET) {
			printk(KERN_EMERG "%d cpus per socket invalid\n",
				sdp->num_cpus);
			return 1;
		}
	}
	return 0;
}

/*
 * Each socket is to get a local array of pnodes/hubs.
 */
static void make_per_cpu_thp(struct bau_control *smaster)
{
	int cpu;
	size_t hpsz = sizeof(struct hub_and_pnode) * num_possible_cpus();

	smaster->thp = kmalloc_node(hpsz, GFP_KERNEL, smaster->osnode);
	memset(smaster->thp, 0, hpsz);
	for_each_present_cpu(cpu) {
		smaster->thp[cpu].pnode = uv_cpu_hub_info(cpu)->pnode;
		smaster->thp[cpu].uvhub = uv_cpu_hub_info(cpu)->numa_blade_id;
	}
}

/*
 * Each uvhub is to get a local cpumask.
 */
static void make_per_hub_cpumask(struct bau_control *hmaster)
{
	int sz = sizeof(cpumask_t);

	hmaster->cpumask = kzalloc_node(sz, GFP_KERNEL, hmaster->osnode);
}

/*
 * Initialize all the per_cpu information for the cpu's on a given socket,
 * given what has been gathered into the socket_desc struct.
 * And reports the chosen hub and socket masters back to the caller.
 */
static int scan_sock(struct socket_desc *sdp, struct uvhub_desc *bdp,
			struct bau_control **smasterp,
			struct bau_control **hmasterp)
{
	int i;
	int cpu;
	struct bau_control *bcp;

	for (i = 0; i < sdp->num_cpus; i++) {
		cpu = sdp->cpu_number[i];
		bcp = &per_cpu(bau_control, cpu);
		bcp->cpu = cpu;
		if (i == 0) {
			*smasterp = bcp;
			if (!(*hmasterp))
				*hmasterp = bcp;
		}
		bcp->cpus_in_uvhub = bdp->num_cpus;
		bcp->cpus_in_socket = sdp->num_cpus;
		bcp->socket_master = *smasterp;
		bcp->uvhub = bdp->uvhub;
		if (is_uv1_hub())
			bcp->uvhub_version = 1;
		else if (is_uv2_hub())
			bcp->uvhub_version = 2;
		else {
			printk(KERN_EMERG "uvhub version not 1 or 2\n");
			return 1;
		}
		bcp->uvhub_master = *hmasterp;
		bcp->uvhub_cpu = uv_cpu_hub_info(cpu)->blade_processor_id;
		if (bcp->uvhub_cpu >= MAX_CPUS_PER_UVHUB) {
			printk(KERN_EMERG "%d cpus per uvhub invalid\n",
				bcp->uvhub_cpu);
			return 1;
		}
	}
	return 0;
}

/*
 * Summarize the blade and socket topology into the per_cpu structures.
 */
static int __init summarize_uvhub_sockets(int nuvhubs,
			struct uvhub_desc *uvhub_descs,
			unsigned char *uvhub_mask)
{
	int socket;
	int uvhub;
	unsigned short socket_mask;

	for (uvhub = 0; uvhub < nuvhubs; uvhub++) {
		struct uvhub_desc *bdp;
		struct bau_control *smaster = NULL;
		struct bau_control *hmaster = NULL;

		if (!(*(uvhub_mask + (uvhub/8)) & (1 << (uvhub%8))))
			continue;

		bdp = &uvhub_descs[uvhub];
		socket_mask = bdp->socket_mask;
		socket = 0;
		while (socket_mask) {
			struct socket_desc *sdp;
			if ((socket_mask & 1)) {
				sdp = &bdp->socket[socket];
				if (scan_sock(sdp, bdp, &smaster, &hmaster))
					return 1;
				make_per_cpu_thp(smaster);
			}
			socket++;
			socket_mask = (socket_mask >> 1);
		}
		make_per_hub_cpumask(hmaster);
	}
	return 0;
}

/*
 * initialize the bau_control structure for each cpu
 */
static int __init init_per_cpu(int nuvhubs, int base_part_pnode)
{
	unsigned char *uvhub_mask;
	void *vp;
	struct uvhub_desc *uvhub_descs;

	timeout_us = calculate_destination_timeout();

	vp = kmalloc(nuvhubs * sizeof(struct uvhub_desc), GFP_KERNEL);
	uvhub_descs = (struct uvhub_desc *)vp;
	memset(uvhub_descs, 0, nuvhubs * sizeof(struct uvhub_desc));
	uvhub_mask = kzalloc((nuvhubs+7)/8, GFP_KERNEL);

	if (get_cpu_topology(base_part_pnode, uvhub_descs, uvhub_mask))
		goto fail;

	if (summarize_uvhub_sockets(nuvhubs, uvhub_descs, uvhub_mask))
		goto fail;

	kfree(uvhub_descs);
	kfree(uvhub_mask);
	init_per_cpu_tunables();
	return 0;

fail:
	kfree(uvhub_descs);
	kfree(uvhub_mask);
	return 1;
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
	int cpus;
	int vector;
	cpumask_var_t *mask;

	if (!is_uv_system())
		return 0;

	for_each_possible_cpu(cur_cpu) {
		mask = &per_cpu(uv_flush_tlb_mask, cur_cpu);
		zalloc_cpumask_var_node(mask, GFP_KERNEL, cpu_to_node(cur_cpu));
	}

	nuvhubs = uv_num_possible_blades();
	congested_cycles = usec_2_cycles(congested_respns_us);

	uv_base_pnode = 0x7fffffff;
	for (uvhub = 0; uvhub < nuvhubs; uvhub++) {
		cpus = uv_blade_nr_possible_cpus(uvhub);
		if (cpus && (uv_blade_to_pnode(uvhub) < uv_base_pnode))
			uv_base_pnode = uv_blade_to_pnode(uvhub);
	}

	enable_timeouts();

	if (init_per_cpu(nuvhubs, uv_base_pnode)) {
		set_bau_off();
		nobau_perm = 1;
		return 0;
	}

	vector = UV_BAU_MESSAGE;
	for_each_possible_blade(uvhub)
		if (uv_blade_nr_possible_cpus(uvhub))
			init_uvhub(uvhub, vector, uv_base_pnode);

	alloc_intr_gate(vector, uv_bau_message_intr1);

	for_each_possible_blade(uvhub) {
		if (uv_blade_nr_possible_cpus(uvhub)) {
			unsigned long val;
			unsigned long mmr;
			pnode = uv_blade_to_pnode(uvhub);
			/* INIT the bau */
			val = 1L << 63;
			write_gmmr_activation(pnode, val);
			mmr = 1; /* should be 1 to broadcast to both sockets */
			if (!is_uv1_hub())
				write_mmr_data_broadcast(pnode, mmr);
		}
	}

	return 0;
}
core_initcall(uv_bau_init);
fs_initcall(uv_ptc_init);
