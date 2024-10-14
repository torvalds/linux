// SPDX-License-Identifier: GPL-2.0-only
/*
 * CPPC (Collaborative Processor Performance Control) methods used by CPUfreq drivers.
 *
 * (C) Copyright 2014, 2015 Linaro Ltd.
 * Author: Ashwin Chaugule <ashwin.chaugule@linaro.org>
 *
 * CPPC describes a few methods for controlling CPU performance using
 * information from a per CPU table called CPC. This table is described in
 * the ACPI v5.0+ specification. The table consists of a list of
 * registers which may be memory mapped or hardware registers and also may
 * include some static integer values.
 *
 * CPU performance is on an abstract continuous scale as against a discretized
 * P-state scale which is tied to CPU frequency only. In brief, the basic
 * operation involves:
 *
 * - OS makes a CPU performance request. (Can provide min and max bounds)
 *
 * - Platform (such as BMC) is free to optimize request within requested bounds
 *   depending on power/thermal budgets etc.
 *
 * - Platform conveys its decision back to OS
 *
 * The communication between OS and platform occurs through another medium
 * called (PCC) Platform Communication Channel. This is a generic mailbox like
 * mechanism which includes doorbell semantics to indicate register updates.
 * See drivers/mailbox/pcc.c for details on PCC.
 *
 * Finer details about the PCC and CPPC spec are available in the ACPI v5.1 and
 * above specifications.
 */

#define pr_fmt(fmt)	"ACPI CPPC: " fmt

#include <linux/delay.h>
#include <linux/iopoll.h>
#include <linux/ktime.h>
#include <linux/rwsem.h>
#include <linux/wait.h>
#include <linux/topology.h>
#include <linux/dmi.h>
#include <linux/units.h>
#include <linux/unaligned.h>

#include <acpi/cppc_acpi.h>

struct cppc_pcc_data {
	struct pcc_mbox_chan *pcc_channel;
	void __iomem *pcc_comm_addr;
	bool pcc_channel_acquired;
	unsigned int deadline_us;
	unsigned int pcc_mpar, pcc_mrtt, pcc_nominal;

	bool pending_pcc_write_cmd;	/* Any pending/batched PCC write cmds? */
	bool platform_owns_pcc;		/* Ownership of PCC subspace */
	unsigned int pcc_write_cnt;	/* Running count of PCC write commands */

	/*
	 * Lock to provide controlled access to the PCC channel.
	 *
	 * For performance critical usecases(currently cppc_set_perf)
	 *	We need to take read_lock and check if channel belongs to OSPM
	 * before reading or writing to PCC subspace
	 *	We need to take write_lock before transferring the channel
	 * ownership to the platform via a Doorbell
	 *	This allows us to batch a number of CPPC requests if they happen
	 * to originate in about the same time
	 *
	 * For non-performance critical usecases(init)
	 *	Take write_lock for all purposes which gives exclusive access
	 */
	struct rw_semaphore pcc_lock;

	/* Wait queue for CPUs whose requests were batched */
	wait_queue_head_t pcc_write_wait_q;
	ktime_t last_cmd_cmpl_time;
	ktime_t last_mpar_reset;
	int mpar_count;
	int refcount;
};

/* Array to represent the PCC channel per subspace ID */
static struct cppc_pcc_data *pcc_data[MAX_PCC_SUBSPACES];
/* The cpu_pcc_subspace_idx contains per CPU subspace ID */
static DEFINE_PER_CPU(int, cpu_pcc_subspace_idx);

/*
 * The cpc_desc structure contains the ACPI register details
 * as described in the per CPU _CPC tables. The details
 * include the type of register (e.g. PCC, System IO, FFH etc.)
 * and destination addresses which lets us READ/WRITE CPU performance
 * information using the appropriate I/O methods.
 */
static DEFINE_PER_CPU(struct cpc_desc *, cpc_desc_ptr);

/* pcc mapped address + header size + offset within PCC subspace */
#define GET_PCC_VADDR(offs, pcc_ss_id) (pcc_data[pcc_ss_id]->pcc_comm_addr + \
						0x8 + (offs))

/* Check if a CPC register is in PCC */
#define CPC_IN_PCC(cpc) ((cpc)->type == ACPI_TYPE_BUFFER &&		\
				(cpc)->cpc_entry.reg.space_id ==	\
				ACPI_ADR_SPACE_PLATFORM_COMM)

/* Check if a CPC register is in FFH */
#define CPC_IN_FFH(cpc) ((cpc)->type == ACPI_TYPE_BUFFER &&		\
				(cpc)->cpc_entry.reg.space_id ==	\
				ACPI_ADR_SPACE_FIXED_HARDWARE)

/* Check if a CPC register is in SystemMemory */
#define CPC_IN_SYSTEM_MEMORY(cpc) ((cpc)->type == ACPI_TYPE_BUFFER &&	\
				(cpc)->cpc_entry.reg.space_id ==	\
				ACPI_ADR_SPACE_SYSTEM_MEMORY)

/* Check if a CPC register is in SystemIo */
#define CPC_IN_SYSTEM_IO(cpc) ((cpc)->type == ACPI_TYPE_BUFFER &&	\
				(cpc)->cpc_entry.reg.space_id ==	\
				ACPI_ADR_SPACE_SYSTEM_IO)

/* Evaluates to True if reg is a NULL register descriptor */
#define IS_NULL_REG(reg) ((reg)->space_id ==  ACPI_ADR_SPACE_SYSTEM_MEMORY && \
				(reg)->address == 0 &&			\
				(reg)->bit_width == 0 &&		\
				(reg)->bit_offset == 0 &&		\
				(reg)->access_width == 0)

/* Evaluates to True if an optional cpc field is supported */
#define CPC_SUPPORTED(cpc) ((cpc)->type == ACPI_TYPE_INTEGER ?		\
				!!(cpc)->cpc_entry.int_value :		\
				!IS_NULL_REG(&(cpc)->cpc_entry.reg))
/*
 * Arbitrary Retries in case the remote processor is slow to respond
 * to PCC commands. Keeping it high enough to cover emulators where
 * the processors run painfully slow.
 */
#define NUM_RETRIES 500ULL

#define OVER_16BTS_MASK ~0xFFFFULL

#define define_one_cppc_ro(_name)		\
static struct kobj_attribute _name =		\
__ATTR(_name, 0444, show_##_name, NULL)

#define to_cpc_desc(a) container_of(a, struct cpc_desc, kobj)

#define show_cppc_data(access_fn, struct_name, member_name)		\
	static ssize_t show_##member_name(struct kobject *kobj,		\
				struct kobj_attribute *attr, char *buf)	\
	{								\
		struct cpc_desc *cpc_ptr = to_cpc_desc(kobj);		\
		struct struct_name st_name = {0};			\
		int ret;						\
									\
		ret = access_fn(cpc_ptr->cpu_id, &st_name);		\
		if (ret)						\
			return ret;					\
									\
		return sysfs_emit(buf, "%llu\n",		\
				(u64)st_name.member_name);		\
	}								\
	define_one_cppc_ro(member_name)

show_cppc_data(cppc_get_perf_caps, cppc_perf_caps, highest_perf);
show_cppc_data(cppc_get_perf_caps, cppc_perf_caps, lowest_perf);
show_cppc_data(cppc_get_perf_caps, cppc_perf_caps, nominal_perf);
show_cppc_data(cppc_get_perf_caps, cppc_perf_caps, lowest_nonlinear_perf);
show_cppc_data(cppc_get_perf_caps, cppc_perf_caps, guaranteed_perf);
show_cppc_data(cppc_get_perf_caps, cppc_perf_caps, lowest_freq);
show_cppc_data(cppc_get_perf_caps, cppc_perf_caps, nominal_freq);

show_cppc_data(cppc_get_perf_ctrs, cppc_perf_fb_ctrs, reference_perf);
show_cppc_data(cppc_get_perf_ctrs, cppc_perf_fb_ctrs, wraparound_time);

/* Check for valid access_width, otherwise, fallback to using bit_width */
#define GET_BIT_WIDTH(reg) ((reg)->access_width ? (8 << ((reg)->access_width - 1)) : (reg)->bit_width)

/* Shift and apply the mask for CPC reads/writes */
#define MASK_VAL_READ(reg, val) (((val) >> (reg)->bit_offset) &				\
					GENMASK(((reg)->bit_width) - 1, 0))
#define MASK_VAL_WRITE(reg, prev_val, val)						\
	((((val) & GENMASK(((reg)->bit_width) - 1, 0)) << (reg)->bit_offset) |		\
	((prev_val) & ~(GENMASK(((reg)->bit_width) - 1, 0) << (reg)->bit_offset)))	\

static ssize_t show_feedback_ctrs(struct kobject *kobj,
		struct kobj_attribute *attr, char *buf)
{
	struct cpc_desc *cpc_ptr = to_cpc_desc(kobj);
	struct cppc_perf_fb_ctrs fb_ctrs = {0};
	int ret;

	ret = cppc_get_perf_ctrs(cpc_ptr->cpu_id, &fb_ctrs);
	if (ret)
		return ret;

	return sysfs_emit(buf, "ref:%llu del:%llu\n",
			fb_ctrs.reference, fb_ctrs.delivered);
}
define_one_cppc_ro(feedback_ctrs);

static struct attribute *cppc_attrs[] = {
	&feedback_ctrs.attr,
	&reference_perf.attr,
	&wraparound_time.attr,
	&highest_perf.attr,
	&lowest_perf.attr,
	&lowest_nonlinear_perf.attr,
	&guaranteed_perf.attr,
	&nominal_perf.attr,
	&nominal_freq.attr,
	&lowest_freq.attr,
	NULL
};
ATTRIBUTE_GROUPS(cppc);

static const struct kobj_type cppc_ktype = {
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = cppc_groups,
};

static int check_pcc_chan(int pcc_ss_id, bool chk_err_bit)
{
	int ret, status;
	struct cppc_pcc_data *pcc_ss_data = pcc_data[pcc_ss_id];
	struct acpi_pcct_shared_memory __iomem *generic_comm_base =
		pcc_ss_data->pcc_comm_addr;

	if (!pcc_ss_data->platform_owns_pcc)
		return 0;

	/*
	 * Poll PCC status register every 3us(delay_us) for maximum of
	 * deadline_us(timeout_us) until PCC command complete bit is set(cond)
	 */
	ret = readw_relaxed_poll_timeout(&generic_comm_base->status, status,
					status & PCC_CMD_COMPLETE_MASK, 3,
					pcc_ss_data->deadline_us);

	if (likely(!ret)) {
		pcc_ss_data->platform_owns_pcc = false;
		if (chk_err_bit && (status & PCC_ERROR_MASK))
			ret = -EIO;
	}

	if (unlikely(ret))
		pr_err("PCC check channel failed for ss: %d. ret=%d\n",
		       pcc_ss_id, ret);

	return ret;
}

/*
 * This function transfers the ownership of the PCC to the platform
 * So it must be called while holding write_lock(pcc_lock)
 */
static int send_pcc_cmd(int pcc_ss_id, u16 cmd)
{
	int ret = -EIO, i;
	struct cppc_pcc_data *pcc_ss_data = pcc_data[pcc_ss_id];
	struct acpi_pcct_shared_memory __iomem *generic_comm_base =
		pcc_ss_data->pcc_comm_addr;
	unsigned int time_delta;

	/*
	 * For CMD_WRITE we know for a fact the caller should have checked
	 * the channel before writing to PCC space
	 */
	if (cmd == CMD_READ) {
		/*
		 * If there are pending cpc_writes, then we stole the channel
		 * before write completion, so first send a WRITE command to
		 * platform
		 */
		if (pcc_ss_data->pending_pcc_write_cmd)
			send_pcc_cmd(pcc_ss_id, CMD_WRITE);

		ret = check_pcc_chan(pcc_ss_id, false);
		if (ret)
			goto end;
	} else /* CMD_WRITE */
		pcc_ss_data->pending_pcc_write_cmd = FALSE;

	/*
	 * Handle the Minimum Request Turnaround Time(MRTT)
	 * "The minimum amount of time that OSPM must wait after the completion
	 * of a command before issuing the next command, in microseconds"
	 */
	if (pcc_ss_data->pcc_mrtt) {
		time_delta = ktime_us_delta(ktime_get(),
					    pcc_ss_data->last_cmd_cmpl_time);
		if (pcc_ss_data->pcc_mrtt > time_delta)
			udelay(pcc_ss_data->pcc_mrtt - time_delta);
	}

	/*
	 * Handle the non-zero Maximum Periodic Access Rate(MPAR)
	 * "The maximum number of periodic requests that the subspace channel can
	 * support, reported in commands per minute. 0 indicates no limitation."
	 *
	 * This parameter should be ideally zero or large enough so that it can
	 * handle maximum number of requests that all the cores in the system can
	 * collectively generate. If it is not, we will follow the spec and just
	 * not send the request to the platform after hitting the MPAR limit in
	 * any 60s window
	 */
	if (pcc_ss_data->pcc_mpar) {
		if (pcc_ss_data->mpar_count == 0) {
			time_delta = ktime_ms_delta(ktime_get(),
						    pcc_ss_data->last_mpar_reset);
			if ((time_delta < 60 * MSEC_PER_SEC) && pcc_ss_data->last_mpar_reset) {
				pr_debug("PCC cmd for subspace %d not sent due to MPAR limit",
					 pcc_ss_id);
				ret = -EIO;
				goto end;
			}
			pcc_ss_data->last_mpar_reset = ktime_get();
			pcc_ss_data->mpar_count = pcc_ss_data->pcc_mpar;
		}
		pcc_ss_data->mpar_count--;
	}

	/* Write to the shared comm region. */
	writew_relaxed(cmd, &generic_comm_base->command);

	/* Flip CMD COMPLETE bit */
	writew_relaxed(0, &generic_comm_base->status);

	pcc_ss_data->platform_owns_pcc = true;

	/* Ring doorbell */
	ret = mbox_send_message(pcc_ss_data->pcc_channel->mchan, &cmd);
	if (ret < 0) {
		pr_err("Err sending PCC mbox message. ss: %d cmd:%d, ret:%d\n",
		       pcc_ss_id, cmd, ret);
		goto end;
	}

	/* wait for completion and check for PCC error bit */
	ret = check_pcc_chan(pcc_ss_id, true);

	if (pcc_ss_data->pcc_mrtt)
		pcc_ss_data->last_cmd_cmpl_time = ktime_get();

	if (pcc_ss_data->pcc_channel->mchan->mbox->txdone_irq)
		mbox_chan_txdone(pcc_ss_data->pcc_channel->mchan, ret);
	else
		mbox_client_txdone(pcc_ss_data->pcc_channel->mchan, ret);

end:
	if (cmd == CMD_WRITE) {
		if (unlikely(ret)) {
			for_each_possible_cpu(i) {
				struct cpc_desc *desc = per_cpu(cpc_desc_ptr, i);

				if (!desc)
					continue;

				if (desc->write_cmd_id == pcc_ss_data->pcc_write_cnt)
					desc->write_cmd_status = ret;
			}
		}
		pcc_ss_data->pcc_write_cnt++;
		wake_up_all(&pcc_ss_data->pcc_write_wait_q);
	}

	return ret;
}

static void cppc_chan_tx_done(struct mbox_client *cl, void *msg, int ret)
{
	if (ret < 0)
		pr_debug("TX did not complete: CMD sent:%x, ret:%d\n",
				*(u16 *)msg, ret);
	else
		pr_debug("TX completed. CMD sent:%x, ret:%d\n",
				*(u16 *)msg, ret);
}

static struct mbox_client cppc_mbox_cl = {
	.tx_done = cppc_chan_tx_done,
	.knows_txdone = true,
};

static int acpi_get_psd(struct cpc_desc *cpc_ptr, acpi_handle handle)
{
	int result = -EFAULT;
	acpi_status status = AE_OK;
	struct acpi_buffer buffer = {ACPI_ALLOCATE_BUFFER, NULL};
	struct acpi_buffer format = {sizeof("NNNNN"), "NNNNN"};
	struct acpi_buffer state = {0, NULL};
	union acpi_object  *psd = NULL;
	struct acpi_psd_package *pdomain;

	status = acpi_evaluate_object_typed(handle, "_PSD", NULL,
					    &buffer, ACPI_TYPE_PACKAGE);
	if (status == AE_NOT_FOUND)	/* _PSD is optional */
		return 0;
	if (ACPI_FAILURE(status))
		return -ENODEV;

	psd = buffer.pointer;
	if (!psd || psd->package.count != 1) {
		pr_debug("Invalid _PSD data\n");
		goto end;
	}

	pdomain = &(cpc_ptr->domain_info);

	state.length = sizeof(struct acpi_psd_package);
	state.pointer = pdomain;

	status = acpi_extract_package(&(psd->package.elements[0]),
		&format, &state);
	if (ACPI_FAILURE(status)) {
		pr_debug("Invalid _PSD data for CPU:%d\n", cpc_ptr->cpu_id);
		goto end;
	}

	if (pdomain->num_entries != ACPI_PSD_REV0_ENTRIES) {
		pr_debug("Unknown _PSD:num_entries for CPU:%d\n", cpc_ptr->cpu_id);
		goto end;
	}

	if (pdomain->revision != ACPI_PSD_REV0_REVISION) {
		pr_debug("Unknown _PSD:revision for CPU: %d\n", cpc_ptr->cpu_id);
		goto end;
	}

	if (pdomain->coord_type != DOMAIN_COORD_TYPE_SW_ALL &&
	    pdomain->coord_type != DOMAIN_COORD_TYPE_SW_ANY &&
	    pdomain->coord_type != DOMAIN_COORD_TYPE_HW_ALL) {
		pr_debug("Invalid _PSD:coord_type for CPU:%d\n", cpc_ptr->cpu_id);
		goto end;
	}

	result = 0;
end:
	kfree(buffer.pointer);
	return result;
}

bool acpi_cpc_valid(void)
{
	struct cpc_desc *cpc_ptr;
	int cpu;

	if (acpi_disabled)
		return false;

	for_each_present_cpu(cpu) {
		cpc_ptr = per_cpu(cpc_desc_ptr, cpu);
		if (!cpc_ptr)
			return false;
	}

	return true;
}
EXPORT_SYMBOL_GPL(acpi_cpc_valid);

bool cppc_allow_fast_switch(void)
{
	struct cpc_register_resource *desired_reg;
	struct cpc_desc *cpc_ptr;
	int cpu;

	for_each_possible_cpu(cpu) {
		cpc_ptr = per_cpu(cpc_desc_ptr, cpu);
		desired_reg = &cpc_ptr->cpc_regs[DESIRED_PERF];
		if (!CPC_IN_SYSTEM_MEMORY(desired_reg) &&
				!CPC_IN_SYSTEM_IO(desired_reg))
			return false;
	}

	return true;
}
EXPORT_SYMBOL_GPL(cppc_allow_fast_switch);

/**
 * acpi_get_psd_map - Map the CPUs in the freq domain of a given cpu
 * @cpu: Find all CPUs that share a domain with cpu.
 * @cpu_data: Pointer to CPU specific CPPC data including PSD info.
 *
 *	Return: 0 for success or negative value for err.
 */
int acpi_get_psd_map(unsigned int cpu, struct cppc_cpudata *cpu_data)
{
	struct cpc_desc *cpc_ptr, *match_cpc_ptr;
	struct acpi_psd_package *match_pdomain;
	struct acpi_psd_package *pdomain;
	int count_target, i;

	/*
	 * Now that we have _PSD data from all CPUs, let's setup P-state
	 * domain info.
	 */
	cpc_ptr = per_cpu(cpc_desc_ptr, cpu);
	if (!cpc_ptr)
		return -EFAULT;

	pdomain = &(cpc_ptr->domain_info);
	cpumask_set_cpu(cpu, cpu_data->shared_cpu_map);
	if (pdomain->num_processors <= 1)
		return 0;

	/* Validate the Domain info */
	count_target = pdomain->num_processors;
	if (pdomain->coord_type == DOMAIN_COORD_TYPE_SW_ALL)
		cpu_data->shared_type = CPUFREQ_SHARED_TYPE_ALL;
	else if (pdomain->coord_type == DOMAIN_COORD_TYPE_HW_ALL)
		cpu_data->shared_type = CPUFREQ_SHARED_TYPE_HW;
	else if (pdomain->coord_type == DOMAIN_COORD_TYPE_SW_ANY)
		cpu_data->shared_type = CPUFREQ_SHARED_TYPE_ANY;

	for_each_possible_cpu(i) {
		if (i == cpu)
			continue;

		match_cpc_ptr = per_cpu(cpc_desc_ptr, i);
		if (!match_cpc_ptr)
			goto err_fault;

		match_pdomain = &(match_cpc_ptr->domain_info);
		if (match_pdomain->domain != pdomain->domain)
			continue;

		/* Here i and cpu are in the same domain */
		if (match_pdomain->num_processors != count_target)
			goto err_fault;

		if (pdomain->coord_type != match_pdomain->coord_type)
			goto err_fault;

		cpumask_set_cpu(i, cpu_data->shared_cpu_map);
	}

	return 0;

err_fault:
	/* Assume no coordination on any error parsing domain info */
	cpumask_clear(cpu_data->shared_cpu_map);
	cpumask_set_cpu(cpu, cpu_data->shared_cpu_map);
	cpu_data->shared_type = CPUFREQ_SHARED_TYPE_NONE;

	return -EFAULT;
}
EXPORT_SYMBOL_GPL(acpi_get_psd_map);

static int register_pcc_channel(int pcc_ss_idx)
{
	struct pcc_mbox_chan *pcc_chan;
	u64 usecs_lat;

	if (pcc_ss_idx >= 0) {
		pcc_chan = pcc_mbox_request_channel(&cppc_mbox_cl, pcc_ss_idx);

		if (IS_ERR(pcc_chan)) {
			pr_err("Failed to find PCC channel for subspace %d\n",
			       pcc_ss_idx);
			return -ENODEV;
		}

		pcc_data[pcc_ss_idx]->pcc_channel = pcc_chan;
		/*
		 * cppc_ss->latency is just a Nominal value. In reality
		 * the remote processor could be much slower to reply.
		 * So add an arbitrary amount of wait on top of Nominal.
		 */
		usecs_lat = NUM_RETRIES * pcc_chan->latency;
		pcc_data[pcc_ss_idx]->deadline_us = usecs_lat;
		pcc_data[pcc_ss_idx]->pcc_mrtt = pcc_chan->min_turnaround_time;
		pcc_data[pcc_ss_idx]->pcc_mpar = pcc_chan->max_access_rate;
		pcc_data[pcc_ss_idx]->pcc_nominal = pcc_chan->latency;

		pcc_data[pcc_ss_idx]->pcc_comm_addr =
			acpi_os_ioremap(pcc_chan->shmem_base_addr,
					pcc_chan->shmem_size);
		if (!pcc_data[pcc_ss_idx]->pcc_comm_addr) {
			pr_err("Failed to ioremap PCC comm region mem for %d\n",
			       pcc_ss_idx);
			return -ENOMEM;
		}

		/* Set flag so that we don't come here for each CPU. */
		pcc_data[pcc_ss_idx]->pcc_channel_acquired = true;
	}

	return 0;
}

/**
 * cpc_ffh_supported() - check if FFH reading supported
 *
 * Check if the architecture has support for functional fixed hardware
 * read/write capability.
 *
 * Return: true for supported, false for not supported
 */
bool __weak cpc_ffh_supported(void)
{
	return false;
}

/**
 * cpc_supported_by_cpu() - check if CPPC is supported by CPU
 *
 * Check if the architectural support for CPPC is present even
 * if the _OSC hasn't prescribed it
 *
 * Return: true for supported, false for not supported
 */
bool __weak cpc_supported_by_cpu(void)
{
	return false;
}

/**
 * pcc_data_alloc() - Allocate the pcc_data memory for pcc subspace
 * @pcc_ss_id: PCC Subspace index as in the PCC client ACPI package.
 *
 * Check and allocate the cppc_pcc_data memory.
 * In some processor configurations it is possible that same subspace
 * is shared between multiple CPUs. This is seen especially in CPUs
 * with hardware multi-threading support.
 *
 * Return: 0 for success, errno for failure
 */
static int pcc_data_alloc(int pcc_ss_id)
{
	if (pcc_ss_id < 0 || pcc_ss_id >= MAX_PCC_SUBSPACES)
		return -EINVAL;

	if (pcc_data[pcc_ss_id]) {
		pcc_data[pcc_ss_id]->refcount++;
	} else {
		pcc_data[pcc_ss_id] = kzalloc(sizeof(struct cppc_pcc_data),
					      GFP_KERNEL);
		if (!pcc_data[pcc_ss_id])
			return -ENOMEM;
		pcc_data[pcc_ss_id]->refcount++;
	}

	return 0;
}

/*
 * An example CPC table looks like the following.
 *
 *  Name (_CPC, Package() {
 *      17,							// NumEntries
 *      1,							// Revision
 *      ResourceTemplate() {Register(PCC, 32, 0, 0x120, 2)},	// Highest Performance
 *      ResourceTemplate() {Register(PCC, 32, 0, 0x124, 2)},	// Nominal Performance
 *      ResourceTemplate() {Register(PCC, 32, 0, 0x128, 2)},	// Lowest Nonlinear Performance
 *      ResourceTemplate() {Register(PCC, 32, 0, 0x12C, 2)},	// Lowest Performance
 *      ResourceTemplate() {Register(PCC, 32, 0, 0x130, 2)},	// Guaranteed Performance Register
 *      ResourceTemplate() {Register(PCC, 32, 0, 0x110, 2)},	// Desired Performance Register
 *      ResourceTemplate() {Register(SystemMemory, 0, 0, 0, 0)},
 *      ...
 *      ...
 *      ...
 *  }
 * Each Register() encodes how to access that specific register.
 * e.g. a sample PCC entry has the following encoding:
 *
 *  Register (
 *      PCC,	// AddressSpaceKeyword
 *      8,	// RegisterBitWidth
 *      8,	// RegisterBitOffset
 *      0x30,	// RegisterAddress
 *      9,	// AccessSize (subspace ID)
 *  )
 */

#ifndef arch_init_invariance_cppc
static inline void arch_init_invariance_cppc(void) { }
#endif

/**
 * acpi_cppc_processor_probe - Search for per CPU _CPC objects.
 * @pr: Ptr to acpi_processor containing this CPU's logical ID.
 *
 *	Return: 0 for success or negative value for err.
 */
int acpi_cppc_processor_probe(struct acpi_processor *pr)
{
	struct acpi_buffer output = {ACPI_ALLOCATE_BUFFER, NULL};
	union acpi_object *out_obj, *cpc_obj;
	struct cpc_desc *cpc_ptr;
	struct cpc_reg *gas_t;
	struct device *cpu_dev;
	acpi_handle handle = pr->handle;
	unsigned int num_ent, i, cpc_rev;
	int pcc_subspace_id = -1;
	acpi_status status;
	int ret = -ENODATA;

	if (!osc_sb_cppc2_support_acked) {
		pr_debug("CPPC v2 _OSC not acked\n");
		if (!cpc_supported_by_cpu()) {
			pr_debug("CPPC is not supported by the CPU\n");
			return -ENODEV;
		}
	}

	/* Parse the ACPI _CPC table for this CPU. */
	status = acpi_evaluate_object_typed(handle, "_CPC", NULL, &output,
			ACPI_TYPE_PACKAGE);
	if (ACPI_FAILURE(status)) {
		ret = -ENODEV;
		goto out_buf_free;
	}

	out_obj = (union acpi_object *) output.pointer;

	cpc_ptr = kzalloc(sizeof(struct cpc_desc), GFP_KERNEL);
	if (!cpc_ptr) {
		ret = -ENOMEM;
		goto out_buf_free;
	}

	/* First entry is NumEntries. */
	cpc_obj = &out_obj->package.elements[0];
	if (cpc_obj->type == ACPI_TYPE_INTEGER)	{
		num_ent = cpc_obj->integer.value;
		if (num_ent <= 1) {
			pr_debug("Unexpected _CPC NumEntries value (%d) for CPU:%d\n",
				 num_ent, pr->id);
			goto out_free;
		}
	} else {
		pr_debug("Unexpected _CPC NumEntries entry type (%d) for CPU:%d\n",
			 cpc_obj->type, pr->id);
		goto out_free;
	}

	/* Second entry should be revision. */
	cpc_obj = &out_obj->package.elements[1];
	if (cpc_obj->type == ACPI_TYPE_INTEGER)	{
		cpc_rev = cpc_obj->integer.value;
	} else {
		pr_debug("Unexpected _CPC Revision entry type (%d) for CPU:%d\n",
			 cpc_obj->type, pr->id);
		goto out_free;
	}

	if (cpc_rev < CPPC_V2_REV) {
		pr_debug("Unsupported _CPC Revision (%d) for CPU:%d\n", cpc_rev,
			 pr->id);
		goto out_free;
	}

	/*
	 * Disregard _CPC if the number of entries in the return pachage is not
	 * as expected, but support future revisions being proper supersets of
	 * the v3 and only causing more entries to be returned by _CPC.
	 */
	if ((cpc_rev == CPPC_V2_REV && num_ent != CPPC_V2_NUM_ENT) ||
	    (cpc_rev == CPPC_V3_REV && num_ent != CPPC_V3_NUM_ENT) ||
	    (cpc_rev > CPPC_V3_REV && num_ent <= CPPC_V3_NUM_ENT)) {
		pr_debug("Unexpected number of _CPC return package entries (%d) for CPU:%d\n",
			 num_ent, pr->id);
		goto out_free;
	}
	if (cpc_rev > CPPC_V3_REV) {
		num_ent = CPPC_V3_NUM_ENT;
		cpc_rev = CPPC_V3_REV;
	}

	cpc_ptr->num_entries = num_ent;
	cpc_ptr->version = cpc_rev;

	/* Iterate through remaining entries in _CPC */
	for (i = 2; i < num_ent; i++) {
		cpc_obj = &out_obj->package.elements[i];

		if (cpc_obj->type == ACPI_TYPE_INTEGER)	{
			cpc_ptr->cpc_regs[i-2].type = ACPI_TYPE_INTEGER;
			cpc_ptr->cpc_regs[i-2].cpc_entry.int_value = cpc_obj->integer.value;
		} else if (cpc_obj->type == ACPI_TYPE_BUFFER) {
			gas_t = (struct cpc_reg *)
				cpc_obj->buffer.pointer;

			/*
			 * The PCC Subspace index is encoded inside
			 * the CPC table entries. The same PCC index
			 * will be used for all the PCC entries,
			 * so extract it only once.
			 */
			if (gas_t->space_id == ACPI_ADR_SPACE_PLATFORM_COMM) {
				if (pcc_subspace_id < 0) {
					pcc_subspace_id = gas_t->access_width;
					if (pcc_data_alloc(pcc_subspace_id))
						goto out_free;
				} else if (pcc_subspace_id != gas_t->access_width) {
					pr_debug("Mismatched PCC ids in _CPC for CPU:%d\n",
						 pr->id);
					goto out_free;
				}
			} else if (gas_t->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY) {
				if (gas_t->address) {
					void __iomem *addr;
					size_t access_width;

					if (!osc_cpc_flexible_adr_space_confirmed) {
						pr_debug("Flexible address space capability not supported\n");
						if (!cpc_supported_by_cpu())
							goto out_free;
					}

					access_width = GET_BIT_WIDTH(gas_t) / 8;
					addr = ioremap(gas_t->address, access_width);
					if (!addr)
						goto out_free;
					cpc_ptr->cpc_regs[i-2].sys_mem_vaddr = addr;
				}
			} else if (gas_t->space_id == ACPI_ADR_SPACE_SYSTEM_IO) {
				if (gas_t->access_width < 1 || gas_t->access_width > 3) {
					/*
					 * 1 = 8-bit, 2 = 16-bit, and 3 = 32-bit.
					 * SystemIO doesn't implement 64-bit
					 * registers.
					 */
					pr_debug("Invalid access width %d for SystemIO register in _CPC\n",
						 gas_t->access_width);
					goto out_free;
				}
				if (gas_t->address & OVER_16BTS_MASK) {
					/* SystemIO registers use 16-bit integer addresses */
					pr_debug("Invalid IO port %llu for SystemIO register in _CPC\n",
						 gas_t->address);
					goto out_free;
				}
				if (!osc_cpc_flexible_adr_space_confirmed) {
					pr_debug("Flexible address space capability not supported\n");
					if (!cpc_supported_by_cpu())
						goto out_free;
				}
			} else {
				if (gas_t->space_id != ACPI_ADR_SPACE_FIXED_HARDWARE || !cpc_ffh_supported()) {
					/* Support only PCC, SystemMemory, SystemIO, and FFH type regs. */
					pr_debug("Unsupported register type (%d) in _CPC\n",
						 gas_t->space_id);
					goto out_free;
				}
			}

			cpc_ptr->cpc_regs[i-2].type = ACPI_TYPE_BUFFER;
			memcpy(&cpc_ptr->cpc_regs[i-2].cpc_entry.reg, gas_t, sizeof(*gas_t));
		} else {
			pr_debug("Invalid entry type (%d) in _CPC for CPU:%d\n",
				 i, pr->id);
			goto out_free;
		}
	}
	per_cpu(cpu_pcc_subspace_idx, pr->id) = pcc_subspace_id;

	/*
	 * Initialize the remaining cpc_regs as unsupported.
	 * Example: In case FW exposes CPPC v2, the below loop will initialize
	 * LOWEST_FREQ and NOMINAL_FREQ regs as unsupported
	 */
	for (i = num_ent - 2; i < MAX_CPC_REG_ENT; i++) {
		cpc_ptr->cpc_regs[i].type = ACPI_TYPE_INTEGER;
		cpc_ptr->cpc_regs[i].cpc_entry.int_value = 0;
	}


	/* Store CPU Logical ID */
	cpc_ptr->cpu_id = pr->id;
	spin_lock_init(&cpc_ptr->rmw_lock);

	/* Parse PSD data for this CPU */
	ret = acpi_get_psd(cpc_ptr, handle);
	if (ret)
		goto out_free;

	/* Register PCC channel once for all PCC subspace ID. */
	if (pcc_subspace_id >= 0 && !pcc_data[pcc_subspace_id]->pcc_channel_acquired) {
		ret = register_pcc_channel(pcc_subspace_id);
		if (ret)
			goto out_free;

		init_rwsem(&pcc_data[pcc_subspace_id]->pcc_lock);
		init_waitqueue_head(&pcc_data[pcc_subspace_id]->pcc_write_wait_q);
	}

	/* Everything looks okay */
	pr_debug("Parsed CPC struct for CPU: %d\n", pr->id);

	/* Add per logical CPU nodes for reading its feedback counters. */
	cpu_dev = get_cpu_device(pr->id);
	if (!cpu_dev) {
		ret = -EINVAL;
		goto out_free;
	}

	/* Plug PSD data into this CPU's CPC descriptor. */
	per_cpu(cpc_desc_ptr, pr->id) = cpc_ptr;

	ret = kobject_init_and_add(&cpc_ptr->kobj, &cppc_ktype, &cpu_dev->kobj,
			"acpi_cppc");
	if (ret) {
		per_cpu(cpc_desc_ptr, pr->id) = NULL;
		kobject_put(&cpc_ptr->kobj);
		goto out_free;
	}

	arch_init_invariance_cppc();

	kfree(output.pointer);
	return 0;

out_free:
	/* Free all the mapped sys mem areas for this CPU */
	for (i = 2; i < cpc_ptr->num_entries; i++) {
		void __iomem *addr = cpc_ptr->cpc_regs[i-2].sys_mem_vaddr;

		if (addr)
			iounmap(addr);
	}
	kfree(cpc_ptr);

out_buf_free:
	kfree(output.pointer);
	return ret;
}
EXPORT_SYMBOL_GPL(acpi_cppc_processor_probe);

/**
 * acpi_cppc_processor_exit - Cleanup CPC structs.
 * @pr: Ptr to acpi_processor containing this CPU's logical ID.
 *
 * Return: Void
 */
void acpi_cppc_processor_exit(struct acpi_processor *pr)
{
	struct cpc_desc *cpc_ptr;
	unsigned int i;
	void __iomem *addr;
	int pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, pr->id);

	if (pcc_ss_id >= 0 && pcc_data[pcc_ss_id]) {
		if (pcc_data[pcc_ss_id]->pcc_channel_acquired) {
			pcc_data[pcc_ss_id]->refcount--;
			if (!pcc_data[pcc_ss_id]->refcount) {
				pcc_mbox_free_channel(pcc_data[pcc_ss_id]->pcc_channel);
				kfree(pcc_data[pcc_ss_id]);
				pcc_data[pcc_ss_id] = NULL;
			}
		}
	}

	cpc_ptr = per_cpu(cpc_desc_ptr, pr->id);
	if (!cpc_ptr)
		return;

	/* Free all the mapped sys mem areas for this CPU */
	for (i = 2; i < cpc_ptr->num_entries; i++) {
		addr = cpc_ptr->cpc_regs[i-2].sys_mem_vaddr;
		if (addr)
			iounmap(addr);
	}

	kobject_put(&cpc_ptr->kobj);
	kfree(cpc_ptr);
}
EXPORT_SYMBOL_GPL(acpi_cppc_processor_exit);

/**
 * cpc_read_ffh() - Read FFH register
 * @cpunum:	CPU number to read
 * @reg:	cppc register information
 * @val:	place holder for return value
 *
 * Read bit_width bits from a specified address and bit_offset
 *
 * Return: 0 for success and error code
 */
int __weak cpc_read_ffh(int cpunum, struct cpc_reg *reg, u64 *val)
{
	return -ENOTSUPP;
}

/**
 * cpc_write_ffh() - Write FFH register
 * @cpunum:	CPU number to write
 * @reg:	cppc register information
 * @val:	value to write
 *
 * Write value of bit_width bits to a specified address and bit_offset
 *
 * Return: 0 for success and error code
 */
int __weak cpc_write_ffh(int cpunum, struct cpc_reg *reg, u64 val)
{
	return -ENOTSUPP;
}

/*
 * Since cpc_read and cpc_write are called while holding pcc_lock, it should be
 * as fast as possible. We have already mapped the PCC subspace during init, so
 * we can directly write to it.
 */

static int cpc_read(int cpu, struct cpc_register_resource *reg_res, u64 *val)
{
	void __iomem *vaddr = NULL;
	int size;
	int pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, cpu);
	struct cpc_reg *reg = &reg_res->cpc_entry.reg;

	if (reg_res->type == ACPI_TYPE_INTEGER) {
		*val = reg_res->cpc_entry.int_value;
		return 0;
	}

	*val = 0;
	size = GET_BIT_WIDTH(reg);

	if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_IO) {
		u32 val_u32;
		acpi_status status;

		status = acpi_os_read_port((acpi_io_address)reg->address,
					   &val_u32, size);
		if (ACPI_FAILURE(status)) {
			pr_debug("Error: Failed to read SystemIO port %llx\n",
				 reg->address);
			return -EFAULT;
		}

		*val = val_u32;
		return 0;
	} else if (reg->space_id == ACPI_ADR_SPACE_PLATFORM_COMM && pcc_ss_id >= 0) {
		/*
		 * For registers in PCC space, the register size is determined
		 * by the bit width field; the access size is used to indicate
		 * the PCC subspace id.
		 */
		size = reg->bit_width;
		vaddr = GET_PCC_VADDR(reg->address, pcc_ss_id);
	}
	else if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY)
		vaddr = reg_res->sys_mem_vaddr;
	else if (reg->space_id == ACPI_ADR_SPACE_FIXED_HARDWARE)
		return cpc_read_ffh(cpu, reg, val);
	else
		return acpi_os_read_memory((acpi_physical_address)reg->address,
				val, size);

	switch (size) {
	case 8:
		*val = readb_relaxed(vaddr);
		break;
	case 16:
		*val = readw_relaxed(vaddr);
		break;
	case 32:
		*val = readl_relaxed(vaddr);
		break;
	case 64:
		*val = readq_relaxed(vaddr);
		break;
	default:
		if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY) {
			pr_debug("Error: Cannot read %u bit width from system memory: 0x%llx\n",
				size, reg->address);
		} else if (reg->space_id == ACPI_ADR_SPACE_PLATFORM_COMM) {
			pr_debug("Error: Cannot read %u bit width from PCC for ss: %d\n",
				size, pcc_ss_id);
		}
		return -EFAULT;
	}

	if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY)
		*val = MASK_VAL_READ(reg, *val);

	return 0;
}

static int cpc_write(int cpu, struct cpc_register_resource *reg_res, u64 val)
{
	int ret_val = 0;
	int size;
	u64 prev_val;
	void __iomem *vaddr = NULL;
	int pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, cpu);
	struct cpc_reg *reg = &reg_res->cpc_entry.reg;
	struct cpc_desc *cpc_desc;

	size = GET_BIT_WIDTH(reg);

	if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_IO) {
		acpi_status status;

		status = acpi_os_write_port((acpi_io_address)reg->address,
					    (u32)val, size);
		if (ACPI_FAILURE(status)) {
			pr_debug("Error: Failed to write SystemIO port %llx\n",
				 reg->address);
			return -EFAULT;
		}

		return 0;
	} else if (reg->space_id == ACPI_ADR_SPACE_PLATFORM_COMM && pcc_ss_id >= 0) {
		/*
		 * For registers in PCC space, the register size is determined
		 * by the bit width field; the access size is used to indicate
		 * the PCC subspace id.
		 */
		size = reg->bit_width;
		vaddr = GET_PCC_VADDR(reg->address, pcc_ss_id);
	}
	else if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY)
		vaddr = reg_res->sys_mem_vaddr;
	else if (reg->space_id == ACPI_ADR_SPACE_FIXED_HARDWARE)
		return cpc_write_ffh(cpu, reg, val);
	else
		return acpi_os_write_memory((acpi_physical_address)reg->address,
				val, size);

	if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY) {
		cpc_desc = per_cpu(cpc_desc_ptr, cpu);
		if (!cpc_desc) {
			pr_debug("No CPC descriptor for CPU:%d\n", cpu);
			return -ENODEV;
		}

		spin_lock(&cpc_desc->rmw_lock);
		switch (size) {
		case 8:
			prev_val = readb_relaxed(vaddr);
			break;
		case 16:
			prev_val = readw_relaxed(vaddr);
			break;
		case 32:
			prev_val = readl_relaxed(vaddr);
			break;
		case 64:
			prev_val = readq_relaxed(vaddr);
			break;
		default:
			spin_unlock(&cpc_desc->rmw_lock);
			return -EFAULT;
		}
		val = MASK_VAL_WRITE(reg, prev_val, val);
		val |= prev_val;
	}

	switch (size) {
	case 8:
		writeb_relaxed(val, vaddr);
		break;
	case 16:
		writew_relaxed(val, vaddr);
		break;
	case 32:
		writel_relaxed(val, vaddr);
		break;
	case 64:
		writeq_relaxed(val, vaddr);
		break;
	default:
		if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY) {
			pr_debug("Error: Cannot write %u bit width to system memory: 0x%llx\n",
				size, reg->address);
		} else if (reg->space_id == ACPI_ADR_SPACE_PLATFORM_COMM) {
			pr_debug("Error: Cannot write %u bit width to PCC for ss: %d\n",
				size, pcc_ss_id);
		}
		ret_val = -EFAULT;
		break;
	}

	if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY)
		spin_unlock(&cpc_desc->rmw_lock);

	return ret_val;
}

static int cppc_get_perf(int cpunum, enum cppc_regs reg_idx, u64 *perf)
{
	struct cpc_desc *cpc_desc = per_cpu(cpc_desc_ptr, cpunum);
	struct cpc_register_resource *reg;

	if (!cpc_desc) {
		pr_debug("No CPC descriptor for CPU:%d\n", cpunum);
		return -ENODEV;
	}

	reg = &cpc_desc->cpc_regs[reg_idx];

	if (CPC_IN_PCC(reg)) {
		int pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, cpunum);
		struct cppc_pcc_data *pcc_ss_data = NULL;
		int ret = 0;

		if (pcc_ss_id < 0)
			return -EIO;

		pcc_ss_data = pcc_data[pcc_ss_id];

		down_write(&pcc_ss_data->pcc_lock);

		if (send_pcc_cmd(pcc_ss_id, CMD_READ) >= 0)
			cpc_read(cpunum, reg, perf);
		else
			ret = -EIO;

		up_write(&pcc_ss_data->pcc_lock);

		return ret;
	}

	cpc_read(cpunum, reg, perf);

	return 0;
}

/**
 * cppc_get_desired_perf - Get the desired performance register value.
 * @cpunum: CPU from which to get desired performance.
 * @desired_perf: Return address.
 *
 * Return: 0 for success, -EIO otherwise.
 */
int cppc_get_desired_perf(int cpunum, u64 *desired_perf)
{
	return cppc_get_perf(cpunum, DESIRED_PERF, desired_perf);
}
EXPORT_SYMBOL_GPL(cppc_get_desired_perf);

/**
 * cppc_get_nominal_perf - Get the nominal performance register value.
 * @cpunum: CPU from which to get nominal performance.
 * @nominal_perf: Return address.
 *
 * Return: 0 for success, -EIO otherwise.
 */
int cppc_get_nominal_perf(int cpunum, u64 *nominal_perf)
{
	return cppc_get_perf(cpunum, NOMINAL_PERF, nominal_perf);
}

/**
 * cppc_get_highest_perf - Get the highest performance register value.
 * @cpunum: CPU from which to get highest performance.
 * @highest_perf: Return address.
 *
 * Return: 0 for success, -EIO otherwise.
 */
int cppc_get_highest_perf(int cpunum, u64 *highest_perf)
{
	return cppc_get_perf(cpunum, HIGHEST_PERF, highest_perf);
}
EXPORT_SYMBOL_GPL(cppc_get_highest_perf);

/**
 * cppc_get_epp_perf - Get the epp register value.
 * @cpunum: CPU from which to get epp preference value.
 * @epp_perf: Return address.
 *
 * Return: 0 for success, -EIO otherwise.
 */
int cppc_get_epp_perf(int cpunum, u64 *epp_perf)
{
	return cppc_get_perf(cpunum, ENERGY_PERF, epp_perf);
}
EXPORT_SYMBOL_GPL(cppc_get_epp_perf);

/**
 * cppc_get_perf_caps - Get a CPU's performance capabilities.
 * @cpunum: CPU from which to get capabilities info.
 * @perf_caps: ptr to cppc_perf_caps. See cppc_acpi.h
 *
 * Return: 0 for success with perf_caps populated else -ERRNO.
 */
int cppc_get_perf_caps(int cpunum, struct cppc_perf_caps *perf_caps)
{
	struct cpc_desc *cpc_desc = per_cpu(cpc_desc_ptr, cpunum);
	struct cpc_register_resource *highest_reg, *lowest_reg,
		*lowest_non_linear_reg, *nominal_reg, *guaranteed_reg,
		*low_freq_reg = NULL, *nom_freq_reg = NULL;
	u64 high, low, guaranteed, nom, min_nonlinear, low_f = 0, nom_f = 0;
	int pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, cpunum);
	struct cppc_pcc_data *pcc_ss_data = NULL;
	int ret = 0, regs_in_pcc = 0;

	if (!cpc_desc) {
		pr_debug("No CPC descriptor for CPU:%d\n", cpunum);
		return -ENODEV;
	}

	highest_reg = &cpc_desc->cpc_regs[HIGHEST_PERF];
	lowest_reg = &cpc_desc->cpc_regs[LOWEST_PERF];
	lowest_non_linear_reg = &cpc_desc->cpc_regs[LOW_NON_LINEAR_PERF];
	nominal_reg = &cpc_desc->cpc_regs[NOMINAL_PERF];
	low_freq_reg = &cpc_desc->cpc_regs[LOWEST_FREQ];
	nom_freq_reg = &cpc_desc->cpc_regs[NOMINAL_FREQ];
	guaranteed_reg = &cpc_desc->cpc_regs[GUARANTEED_PERF];

	/* Are any of the regs PCC ?*/
	if (CPC_IN_PCC(highest_reg) || CPC_IN_PCC(lowest_reg) ||
		CPC_IN_PCC(lowest_non_linear_reg) || CPC_IN_PCC(nominal_reg) ||
		CPC_IN_PCC(low_freq_reg) || CPC_IN_PCC(nom_freq_reg)) {
		if (pcc_ss_id < 0) {
			pr_debug("Invalid pcc_ss_id\n");
			return -ENODEV;
		}
		pcc_ss_data = pcc_data[pcc_ss_id];
		regs_in_pcc = 1;
		down_write(&pcc_ss_data->pcc_lock);
		/* Ring doorbell once to update PCC subspace */
		if (send_pcc_cmd(pcc_ss_id, CMD_READ) < 0) {
			ret = -EIO;
			goto out_err;
		}
	}

	cpc_read(cpunum, highest_reg, &high);
	perf_caps->highest_perf = high;

	cpc_read(cpunum, lowest_reg, &low);
	perf_caps->lowest_perf = low;

	cpc_read(cpunum, nominal_reg, &nom);
	perf_caps->nominal_perf = nom;

	if (guaranteed_reg->type != ACPI_TYPE_BUFFER  ||
	    IS_NULL_REG(&guaranteed_reg->cpc_entry.reg)) {
		perf_caps->guaranteed_perf = 0;
	} else {
		cpc_read(cpunum, guaranteed_reg, &guaranteed);
		perf_caps->guaranteed_perf = guaranteed;
	}

	cpc_read(cpunum, lowest_non_linear_reg, &min_nonlinear);
	perf_caps->lowest_nonlinear_perf = min_nonlinear;

	if (!high || !low || !nom || !min_nonlinear)
		ret = -EFAULT;

	/* Read optional lowest and nominal frequencies if present */
	if (CPC_SUPPORTED(low_freq_reg))
		cpc_read(cpunum, low_freq_reg, &low_f);

	if (CPC_SUPPORTED(nom_freq_reg))
		cpc_read(cpunum, nom_freq_reg, &nom_f);

	perf_caps->lowest_freq = low_f;
	perf_caps->nominal_freq = nom_f;


out_err:
	if (regs_in_pcc)
		up_write(&pcc_ss_data->pcc_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(cppc_get_perf_caps);

/**
 * cppc_perf_ctrs_in_pcc - Check if any perf counters are in a PCC region.
 *
 * CPPC has flexibility about how CPU performance counters are accessed.
 * One of the choices is PCC regions, which can have a high access latency. This
 * routine allows callers of cppc_get_perf_ctrs() to know this ahead of time.
 *
 * Return: true if any of the counters are in PCC regions, false otherwise
 */
bool cppc_perf_ctrs_in_pcc(void)
{
	int cpu;

	for_each_present_cpu(cpu) {
		struct cpc_register_resource *ref_perf_reg;
		struct cpc_desc *cpc_desc;

		cpc_desc = per_cpu(cpc_desc_ptr, cpu);

		if (CPC_IN_PCC(&cpc_desc->cpc_regs[DELIVERED_CTR]) ||
		    CPC_IN_PCC(&cpc_desc->cpc_regs[REFERENCE_CTR]) ||
		    CPC_IN_PCC(&cpc_desc->cpc_regs[CTR_WRAP_TIME]))
			return true;


		ref_perf_reg = &cpc_desc->cpc_regs[REFERENCE_PERF];

		/*
		 * If reference perf register is not supported then we should
		 * use the nominal perf value
		 */
		if (!CPC_SUPPORTED(ref_perf_reg))
			ref_perf_reg = &cpc_desc->cpc_regs[NOMINAL_PERF];

		if (CPC_IN_PCC(ref_perf_reg))
			return true;
	}

	return false;
}
EXPORT_SYMBOL_GPL(cppc_perf_ctrs_in_pcc);

/**
 * cppc_get_perf_ctrs - Read a CPU's performance feedback counters.
 * @cpunum: CPU from which to read counters.
 * @perf_fb_ctrs: ptr to cppc_perf_fb_ctrs. See cppc_acpi.h
 *
 * Return: 0 for success with perf_fb_ctrs populated else -ERRNO.
 */
int cppc_get_perf_ctrs(int cpunum, struct cppc_perf_fb_ctrs *perf_fb_ctrs)
{
	struct cpc_desc *cpc_desc = per_cpu(cpc_desc_ptr, cpunum);
	struct cpc_register_resource *delivered_reg, *reference_reg,
		*ref_perf_reg, *ctr_wrap_reg;
	int pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, cpunum);
	struct cppc_pcc_data *pcc_ss_data = NULL;
	u64 delivered, reference, ref_perf, ctr_wrap_time;
	int ret = 0, regs_in_pcc = 0;

	if (!cpc_desc) {
		pr_debug("No CPC descriptor for CPU:%d\n", cpunum);
		return -ENODEV;
	}

	delivered_reg = &cpc_desc->cpc_regs[DELIVERED_CTR];
	reference_reg = &cpc_desc->cpc_regs[REFERENCE_CTR];
	ref_perf_reg = &cpc_desc->cpc_regs[REFERENCE_PERF];
	ctr_wrap_reg = &cpc_desc->cpc_regs[CTR_WRAP_TIME];

	/*
	 * If reference perf register is not supported then we should
	 * use the nominal perf value
	 */
	if (!CPC_SUPPORTED(ref_perf_reg))
		ref_perf_reg = &cpc_desc->cpc_regs[NOMINAL_PERF];

	/* Are any of the regs PCC ?*/
	if (CPC_IN_PCC(delivered_reg) || CPC_IN_PCC(reference_reg) ||
		CPC_IN_PCC(ctr_wrap_reg) || CPC_IN_PCC(ref_perf_reg)) {
		if (pcc_ss_id < 0) {
			pr_debug("Invalid pcc_ss_id\n");
			return -ENODEV;
		}
		pcc_ss_data = pcc_data[pcc_ss_id];
		down_write(&pcc_ss_data->pcc_lock);
		regs_in_pcc = 1;
		/* Ring doorbell once to update PCC subspace */
		if (send_pcc_cmd(pcc_ss_id, CMD_READ) < 0) {
			ret = -EIO;
			goto out_err;
		}
	}

	cpc_read(cpunum, delivered_reg, &delivered);
	cpc_read(cpunum, reference_reg, &reference);
	cpc_read(cpunum, ref_perf_reg, &ref_perf);

	/*
	 * Per spec, if ctr_wrap_time optional register is unsupported, then the
	 * performance counters are assumed to never wrap during the lifetime of
	 * platform
	 */
	ctr_wrap_time = (u64)(~((u64)0));
	if (CPC_SUPPORTED(ctr_wrap_reg))
		cpc_read(cpunum, ctr_wrap_reg, &ctr_wrap_time);

	if (!delivered || !reference ||	!ref_perf) {
		ret = -EFAULT;
		goto out_err;
	}

	perf_fb_ctrs->delivered = delivered;
	perf_fb_ctrs->reference = reference;
	perf_fb_ctrs->reference_perf = ref_perf;
	perf_fb_ctrs->wraparound_time = ctr_wrap_time;
out_err:
	if (regs_in_pcc)
		up_write(&pcc_ss_data->pcc_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(cppc_get_perf_ctrs);

/*
 * Set Energy Performance Preference Register value through
 * Performance Controls Interface
 */
int cppc_set_epp_perf(int cpu, struct cppc_perf_ctrls *perf_ctrls, bool enable)
{
	int pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, cpu);
	struct cpc_register_resource *epp_set_reg;
	struct cpc_register_resource *auto_sel_reg;
	struct cpc_desc *cpc_desc = per_cpu(cpc_desc_ptr, cpu);
	struct cppc_pcc_data *pcc_ss_data = NULL;
	int ret;

	if (!cpc_desc) {
		pr_debug("No CPC descriptor for CPU:%d\n", cpu);
		return -ENODEV;
	}

	auto_sel_reg = &cpc_desc->cpc_regs[AUTO_SEL_ENABLE];
	epp_set_reg = &cpc_desc->cpc_regs[ENERGY_PERF];

	if (CPC_IN_PCC(epp_set_reg) || CPC_IN_PCC(auto_sel_reg)) {
		if (pcc_ss_id < 0) {
			pr_debug("Invalid pcc_ss_id for CPU:%d\n", cpu);
			return -ENODEV;
		}

		if (CPC_SUPPORTED(auto_sel_reg)) {
			ret = cpc_write(cpu, auto_sel_reg, enable);
			if (ret)
				return ret;
		}

		if (CPC_SUPPORTED(epp_set_reg)) {
			ret = cpc_write(cpu, epp_set_reg, perf_ctrls->energy_perf);
			if (ret)
				return ret;
		}

		pcc_ss_data = pcc_data[pcc_ss_id];

		down_write(&pcc_ss_data->pcc_lock);
		/* after writing CPC, transfer the ownership of PCC to platform */
		ret = send_pcc_cmd(pcc_ss_id, CMD_WRITE);
		up_write(&pcc_ss_data->pcc_lock);
	} else if (osc_cpc_flexible_adr_space_confirmed &&
		   CPC_SUPPORTED(epp_set_reg) && CPC_IN_FFH(epp_set_reg)) {
		ret = cpc_write(cpu, epp_set_reg, perf_ctrls->energy_perf);
	} else {
		ret = -ENOTSUPP;
		pr_debug("_CPC in PCC and _CPC in FFH are not supported\n");
	}

	return ret;
}
EXPORT_SYMBOL_GPL(cppc_set_epp_perf);

/**
 * cppc_get_auto_sel_caps - Read autonomous selection register.
 * @cpunum : CPU from which to read register.
 * @perf_caps : struct where autonomous selection register value is updated.
 */
int cppc_get_auto_sel_caps(int cpunum, struct cppc_perf_caps *perf_caps)
{
	struct cpc_desc *cpc_desc = per_cpu(cpc_desc_ptr, cpunum);
	struct cpc_register_resource *auto_sel_reg;
	u64  auto_sel;

	if (!cpc_desc) {
		pr_debug("No CPC descriptor for CPU:%d\n", cpunum);
		return -ENODEV;
	}

	auto_sel_reg = &cpc_desc->cpc_regs[AUTO_SEL_ENABLE];

	if (!CPC_SUPPORTED(auto_sel_reg))
		pr_warn_once("Autonomous mode is not unsupported!\n");

	if (CPC_IN_PCC(auto_sel_reg)) {
		int pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, cpunum);
		struct cppc_pcc_data *pcc_ss_data = NULL;
		int ret = 0;

		if (pcc_ss_id < 0)
			return -ENODEV;

		pcc_ss_data = pcc_data[pcc_ss_id];

		down_write(&pcc_ss_data->pcc_lock);

		if (send_pcc_cmd(pcc_ss_id, CMD_READ) >= 0) {
			cpc_read(cpunum, auto_sel_reg, &auto_sel);
			perf_caps->auto_sel = (bool)auto_sel;
		} else {
			ret = -EIO;
		}

		up_write(&pcc_ss_data->pcc_lock);

		return ret;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(cppc_get_auto_sel_caps);

/**
 * cppc_set_auto_sel - Write autonomous selection register.
 * @cpu    : CPU to which to write register.
 * @enable : the desired value of autonomous selection resiter to be updated.
 */
int cppc_set_auto_sel(int cpu, bool enable)
{
	int pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, cpu);
	struct cpc_register_resource *auto_sel_reg;
	struct cpc_desc *cpc_desc = per_cpu(cpc_desc_ptr, cpu);
	struct cppc_pcc_data *pcc_ss_data = NULL;
	int ret = -EINVAL;

	if (!cpc_desc) {
		pr_debug("No CPC descriptor for CPU:%d\n", cpu);
		return -ENODEV;
	}

	auto_sel_reg = &cpc_desc->cpc_regs[AUTO_SEL_ENABLE];

	if (CPC_IN_PCC(auto_sel_reg)) {
		if (pcc_ss_id < 0) {
			pr_debug("Invalid pcc_ss_id\n");
			return -ENODEV;
		}

		if (CPC_SUPPORTED(auto_sel_reg)) {
			ret = cpc_write(cpu, auto_sel_reg, enable);
			if (ret)
				return ret;
		}

		pcc_ss_data = pcc_data[pcc_ss_id];

		down_write(&pcc_ss_data->pcc_lock);
		/* after writing CPC, transfer the ownership of PCC to platform */
		ret = send_pcc_cmd(pcc_ss_id, CMD_WRITE);
		up_write(&pcc_ss_data->pcc_lock);
	} else {
		ret = -ENOTSUPP;
		pr_debug("_CPC in PCC is not supported\n");
	}

	return ret;
}
EXPORT_SYMBOL_GPL(cppc_set_auto_sel);

/**
 * cppc_set_enable - Set to enable CPPC on the processor by writing the
 * Continuous Performance Control package EnableRegister field.
 * @cpu: CPU for which to enable CPPC register.
 * @enable: 0 - disable, 1 - enable CPPC feature on the processor.
 *
 * Return: 0 for success, -ERRNO or -EIO otherwise.
 */
int cppc_set_enable(int cpu, bool enable)
{
	int pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, cpu);
	struct cpc_register_resource *enable_reg;
	struct cpc_desc *cpc_desc = per_cpu(cpc_desc_ptr, cpu);
	struct cppc_pcc_data *pcc_ss_data = NULL;
	int ret = -EINVAL;

	if (!cpc_desc) {
		pr_debug("No CPC descriptor for CPU:%d\n", cpu);
		return -EINVAL;
	}

	enable_reg = &cpc_desc->cpc_regs[ENABLE];

	if (CPC_IN_PCC(enable_reg)) {

		if (pcc_ss_id < 0)
			return -EIO;

		ret = cpc_write(cpu, enable_reg, enable);
		if (ret)
			return ret;

		pcc_ss_data = pcc_data[pcc_ss_id];

		down_write(&pcc_ss_data->pcc_lock);
		/* after writing CPC, transfer the ownership of PCC to platfrom */
		ret = send_pcc_cmd(pcc_ss_id, CMD_WRITE);
		up_write(&pcc_ss_data->pcc_lock);
		return ret;
	}

	return cpc_write(cpu, enable_reg, enable);
}
EXPORT_SYMBOL_GPL(cppc_set_enable);

/**
 * cppc_set_perf - Set a CPU's performance controls.
 * @cpu: CPU for which to set performance controls.
 * @perf_ctrls: ptr to cppc_perf_ctrls. See cppc_acpi.h
 *
 * Return: 0 for success, -ERRNO otherwise.
 */
int cppc_set_perf(int cpu, struct cppc_perf_ctrls *perf_ctrls)
{
	struct cpc_desc *cpc_desc = per_cpu(cpc_desc_ptr, cpu);
	struct cpc_register_resource *desired_reg, *min_perf_reg, *max_perf_reg;
	int pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, cpu);
	struct cppc_pcc_data *pcc_ss_data = NULL;
	int ret = 0;

	if (!cpc_desc) {
		pr_debug("No CPC descriptor for CPU:%d\n", cpu);
		return -ENODEV;
	}

	desired_reg = &cpc_desc->cpc_regs[DESIRED_PERF];
	min_perf_reg = &cpc_desc->cpc_regs[MIN_PERF];
	max_perf_reg = &cpc_desc->cpc_regs[MAX_PERF];

	/*
	 * This is Phase-I where we want to write to CPC registers
	 * -> We want all CPUs to be able to execute this phase in parallel
	 *
	 * Since read_lock can be acquired by multiple CPUs simultaneously we
	 * achieve that goal here
	 */
	if (CPC_IN_PCC(desired_reg) || CPC_IN_PCC(min_perf_reg) || CPC_IN_PCC(max_perf_reg)) {
		if (pcc_ss_id < 0) {
			pr_debug("Invalid pcc_ss_id\n");
			return -ENODEV;
		}
		pcc_ss_data = pcc_data[pcc_ss_id];
		down_read(&pcc_ss_data->pcc_lock); /* BEGIN Phase-I */
		if (pcc_ss_data->platform_owns_pcc) {
			ret = check_pcc_chan(pcc_ss_id, false);
			if (ret) {
				up_read(&pcc_ss_data->pcc_lock);
				return ret;
			}
		}
		/*
		 * Update the pending_write to make sure a PCC CMD_READ will not
		 * arrive and steal the channel during the switch to write lock
		 */
		pcc_ss_data->pending_pcc_write_cmd = true;
		cpc_desc->write_cmd_id = pcc_ss_data->pcc_write_cnt;
		cpc_desc->write_cmd_status = 0;
	}

	cpc_write(cpu, desired_reg, perf_ctrls->desired_perf);

	/*
	 * Only write if min_perf and max_perf not zero. Some drivers pass zero
	 * value to min and max perf, but they don't mean to set the zero value,
	 * they just don't want to write to those registers.
	 */
	if (perf_ctrls->min_perf)
		cpc_write(cpu, min_perf_reg, perf_ctrls->min_perf);
	if (perf_ctrls->max_perf)
		cpc_write(cpu, max_perf_reg, perf_ctrls->max_perf);

	if (CPC_IN_PCC(desired_reg) || CPC_IN_PCC(min_perf_reg) || CPC_IN_PCC(max_perf_reg))
		up_read(&pcc_ss_data->pcc_lock);	/* END Phase-I */
	/*
	 * This is Phase-II where we transfer the ownership of PCC to Platform
	 *
	 * Short Summary: Basically if we think of a group of cppc_set_perf
	 * requests that happened in short overlapping interval. The last CPU to
	 * come out of Phase-I will enter Phase-II and ring the doorbell.
	 *
	 * We have the following requirements for Phase-II:
	 *     1. We want to execute Phase-II only when there are no CPUs
	 * currently executing in Phase-I
	 *     2. Once we start Phase-II we want to avoid all other CPUs from
	 * entering Phase-I.
	 *     3. We want only one CPU among all those who went through Phase-I
	 * to run phase-II
	 *
	 * If write_trylock fails to get the lock and doesn't transfer the
	 * PCC ownership to the platform, then one of the following will be TRUE
	 *     1. There is at-least one CPU in Phase-I which will later execute
	 * write_trylock, so the CPUs in Phase-I will be responsible for
	 * executing the Phase-II.
	 *     2. Some other CPU has beaten this CPU to successfully execute the
	 * write_trylock and has already acquired the write_lock. We know for a
	 * fact it (other CPU acquiring the write_lock) couldn't have happened
	 * before this CPU's Phase-I as we held the read_lock.
	 *     3. Some other CPU executing pcc CMD_READ has stolen the
	 * down_write, in which case, send_pcc_cmd will check for pending
	 * CMD_WRITE commands by checking the pending_pcc_write_cmd.
	 * So this CPU can be certain that its request will be delivered
	 *    So in all cases, this CPU knows that its request will be delivered
	 * by another CPU and can return
	 *
	 * After getting the down_write we still need to check for
	 * pending_pcc_write_cmd to take care of the following scenario
	 *    The thread running this code could be scheduled out between
	 * Phase-I and Phase-II. Before it is scheduled back on, another CPU
	 * could have delivered the request to Platform by triggering the
	 * doorbell and transferred the ownership of PCC to platform. So this
	 * avoids triggering an unnecessary doorbell and more importantly before
	 * triggering the doorbell it makes sure that the PCC channel ownership
	 * is still with OSPM.
	 *   pending_pcc_write_cmd can also be cleared by a different CPU, if
	 * there was a pcc CMD_READ waiting on down_write and it steals the lock
	 * before the pcc CMD_WRITE is completed. send_pcc_cmd checks for this
	 * case during a CMD_READ and if there are pending writes it delivers
	 * the write command before servicing the read command
	 */
	if (CPC_IN_PCC(desired_reg) || CPC_IN_PCC(min_perf_reg) || CPC_IN_PCC(max_perf_reg)) {
		if (down_write_trylock(&pcc_ss_data->pcc_lock)) {/* BEGIN Phase-II */
			/* Update only if there are pending write commands */
			if (pcc_ss_data->pending_pcc_write_cmd)
				send_pcc_cmd(pcc_ss_id, CMD_WRITE);
			up_write(&pcc_ss_data->pcc_lock);	/* END Phase-II */
		} else
			/* Wait until pcc_write_cnt is updated by send_pcc_cmd */
			wait_event(pcc_ss_data->pcc_write_wait_q,
				   cpc_desc->write_cmd_id != pcc_ss_data->pcc_write_cnt);

		/* send_pcc_cmd updates the status in case of failure */
		ret = cpc_desc->write_cmd_status;
	}
	return ret;
}
EXPORT_SYMBOL_GPL(cppc_set_perf);

/**
 * cppc_get_transition_latency - returns frequency transition latency in ns
 * @cpu_num: CPU number for per_cpu().
 *
 * ACPI CPPC does not explicitly specify how a platform can specify the
 * transition latency for performance change requests. The closest we have
 * is the timing information from the PCCT tables which provides the info
 * on the number and frequency of PCC commands the platform can handle.
 *
 * If desired_reg is in the SystemMemory or SystemIo ACPI address space,
 * then assume there is no latency.
 */
unsigned int cppc_get_transition_latency(int cpu_num)
{
	/*
	 * Expected transition latency is based on the PCCT timing values
	 * Below are definition from ACPI spec:
	 * pcc_nominal- Expected latency to process a command, in microseconds
	 * pcc_mpar   - The maximum number of periodic requests that the subspace
	 *              channel can support, reported in commands per minute. 0
	 *              indicates no limitation.
	 * pcc_mrtt   - The minimum amount of time that OSPM must wait after the
	 *              completion of a command before issuing the next command,
	 *              in microseconds.
	 */
	unsigned int latency_ns = 0;
	struct cpc_desc *cpc_desc;
	struct cpc_register_resource *desired_reg;
	int pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, cpu_num);
	struct cppc_pcc_data *pcc_ss_data;

	cpc_desc = per_cpu(cpc_desc_ptr, cpu_num);
	if (!cpc_desc)
		return CPUFREQ_ETERNAL;

	desired_reg = &cpc_desc->cpc_regs[DESIRED_PERF];
	if (CPC_IN_SYSTEM_MEMORY(desired_reg) || CPC_IN_SYSTEM_IO(desired_reg))
		return 0;
	else if (!CPC_IN_PCC(desired_reg))
		return CPUFREQ_ETERNAL;

	if (pcc_ss_id < 0)
		return CPUFREQ_ETERNAL;

	pcc_ss_data = pcc_data[pcc_ss_id];
	if (pcc_ss_data->pcc_mpar)
		latency_ns = 60 * (1000 * 1000 * 1000 / pcc_ss_data->pcc_mpar);

	latency_ns = max(latency_ns, pcc_ss_data->pcc_nominal * 1000);
	latency_ns = max(latency_ns, pcc_ss_data->pcc_mrtt * 1000);

	return latency_ns;
}
EXPORT_SYMBOL_GPL(cppc_get_transition_latency);

/* Minimum struct length needed for the DMI processor entry we want */
#define DMI_ENTRY_PROCESSOR_MIN_LENGTH	48

/* Offset in the DMI processor structure for the max frequency */
#define DMI_PROCESSOR_MAX_SPEED		0x14

/* Callback function used to retrieve the max frequency from DMI */
static void cppc_find_dmi_mhz(const struct dmi_header *dm, void *private)
{
	const u8 *dmi_data = (const u8 *)dm;
	u16 *mhz = (u16 *)private;

	if (dm->type == DMI_ENTRY_PROCESSOR &&
	    dm->length >= DMI_ENTRY_PROCESSOR_MIN_LENGTH) {
		u16 val = (u16)get_unaligned((const u16 *)
				(dmi_data + DMI_PROCESSOR_MAX_SPEED));
		*mhz = umax(val, *mhz);
	}
}

/* Look up the max frequency in DMI */
static u64 cppc_get_dmi_max_khz(void)
{
	u16 mhz = 0;

	dmi_walk(cppc_find_dmi_mhz, &mhz);

	/*
	 * Real stupid fallback value, just in case there is no
	 * actual value set.
	 */
	mhz = mhz ? mhz : 1;

	return KHZ_PER_MHZ * mhz;
}

/*
 * If CPPC lowest_freq and nominal_freq registers are exposed then we can
 * use them to convert perf to freq and vice versa. The conversion is
 * extrapolated as an affine function passing by the 2 points:
 *  - (Low perf, Low freq)
 *  - (Nominal perf, Nominal freq)
 */
unsigned int cppc_perf_to_khz(struct cppc_perf_caps *caps, unsigned int perf)
{
	s64 retval, offset = 0;
	static u64 max_khz;
	u64 mul, div;

	if (caps->lowest_freq && caps->nominal_freq) {
		mul = caps->nominal_freq - caps->lowest_freq;
		mul *= KHZ_PER_MHZ;
		div = caps->nominal_perf - caps->lowest_perf;
		offset = caps->nominal_freq * KHZ_PER_MHZ -
			 div64_u64(caps->nominal_perf * mul, div);
	} else {
		if (!max_khz)
			max_khz = cppc_get_dmi_max_khz();
		mul = max_khz;
		div = caps->highest_perf;
	}

	retval = offset + div64_u64(perf * mul, div);
	if (retval >= 0)
		return retval;
	return 0;
}
EXPORT_SYMBOL_GPL(cppc_perf_to_khz);

unsigned int cppc_khz_to_perf(struct cppc_perf_caps *caps, unsigned int freq)
{
	s64 retval, offset = 0;
	static u64 max_khz;
	u64  mul, div;

	if (caps->lowest_freq && caps->nominal_freq) {
		mul = caps->nominal_perf - caps->lowest_perf;
		div = caps->nominal_freq - caps->lowest_freq;
		/*
		 * We don't need to convert to kHz for computing offset and can
		 * directly use nominal_freq and lowest_freq as the div64_u64
		 * will remove the frequency unit.
		 */
		offset = caps->nominal_perf -
			 div64_u64(caps->nominal_freq * mul, div);
		/* But we need it for computing the perf level. */
		div *= KHZ_PER_MHZ;
	} else {
		if (!max_khz)
			max_khz = cppc_get_dmi_max_khz();
		mul = caps->highest_perf;
		div = max_khz;
	}

	retval = offset + div64_u64(freq * mul, div);
	if (retval >= 0)
		return retval;
	return 0;
}
EXPORT_SYMBOL_GPL(cppc_khz_to_perf);
