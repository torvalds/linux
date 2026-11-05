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
#include <linux/interval_tree_generic.h>
#include <linux/iopoll.h>
#include <linux/ktime.h>
#include <linux/list.h>
#include <linux/mutex.h>
#include <linux/rbtree.h>
#include <linux/rwsem.h>
#include <linux/wait.h>
#include <linux/topology.h>
#include <linux/dmi.h>
#include <linux/units.h>
#include <linux/unaligned.h>

#include <acpi/cppc_acpi.h>

struct cppc_pcc_data {
	struct pcc_mbox_chan *pcc_channel;
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
	/* Serialize byte-oriented accesses to aliased PCC payload fields. */
	raw_spinlock_t payload_lock;

	/* Wait queue for CPUs whose requests were batched */
	wait_queue_head_t pcc_write_wait_q;
	ktime_t last_cmd_cmpl_time;
	ktime_t last_mpar_reset;
	int mpar_count;
	int refcount;
};

/* Array to represent the PCC channel per subspace ID */
static struct cppc_pcc_data *pcc_data[MAX_PCC_SUBSPACES];
static DEFINE_MUTEX(pcc_data_lock);
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

/* Protect immutable capability queries against descriptor removal. */
static DEFINE_MUTEX(cpc_desc_lock);

static void cpc_set_desc(unsigned int cpu, struct cpc_desc *desc)
{
	guard(mutex)(&cpc_desc_lock);
	per_cpu(cpc_desc_ptr, cpu) = desc;
}

struct cpc_sysmem_node {
	struct rb_node rb;
	u64 subtree_last;
	u64 start;
	u64 last;
	struct cpc_desc *desc;
	unsigned int reg_idx;
	struct list_head aliases;
	struct list_head alias_node;
	struct cpc_sysmem_node *alias_of;
	bool registered;
};

struct cpc_non_mmio_node {
	struct rb_node rb;
	u64 subtree_last;
	u64 start;
	u64 last;
	struct cpc_desc *desc;
	unsigned int reg_idx;
	u8 space_id;
	u8 pcc_ss_id;
	bool registered;
};

#define CPC_SYSMEM_START(node) ((node)->start)
#define CPC_SYSMEM_LAST(node) ((node)->last)

INTERVAL_TREE_DEFINE(struct cpc_sysmem_node, rb, u64, subtree_last,
		     CPC_SYSMEM_START, CPC_SYSMEM_LAST, static inline,
		     cpc_sysmem_itree)

static struct rb_root_cached cpc_sysmem_tree = RB_ROOT_CACHED;
static DEFINE_MUTEX(cpc_sysmem_lock);

#define CPC_NON_MMIO_START(node) ((node)->start)
#define CPC_NON_MMIO_LAST(node) ((node)->last)

INTERVAL_TREE_DEFINE(struct cpc_non_mmio_node, rb, u64, subtree_last,
		     CPC_NON_MMIO_START, CPC_NON_MMIO_LAST, static inline,
		     cpc_non_mmio_itree)

static struct rb_root_cached cpc_pcc_trees[MAX_PCC_SUBSPACES];
static struct rb_root_cached cpc_sysio_tree = RB_ROOT_CACHED;
static DEFINE_MUTEX(cpc_non_mmio_lock);

static struct cpc_sysmem_node *cpc_sysmem_first(u64 start, u64 last)
{
	return cpc_sysmem_itree_iter_first(&cpc_sysmem_tree, start, last);
}

static struct cpc_sysmem_node *cpc_sysmem_next(struct cpc_sysmem_node *node,
					       u64 start, u64 last)
{
	return cpc_sysmem_itree_iter_next(node, start, last);
}

#define CPC_PCC_HEADER_SIZE	0x8

/* pcc mapped address + header size + offset within PCC subspace */
#define GET_PCC_VADDR(offs, pcc_ss_id) (pcc_data[pcc_ss_id]->pcc_channel->shmem + \
						CPC_PCC_HEADER_SIZE + (offs))

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

static bool cpc_is_writable(const struct cpc_register_resource *cpc)
{
	return cpc->type == ACPI_TYPE_BUFFER &&
	       !IS_NULL_REG(&cpc->cpc_entry.reg) &&
	       !cpc->cpc_entry.write_unsupported;
}

static bool cpc_is_readable(const struct cpc_register_resource *cpc)
{
	return cpc->type != ACPI_TYPE_BUFFER ||
	       !cpc->cpc_entry.read_unsupported;
}

static bool cpc_entry_present(const struct cpc_register_resource *cpc)
{
	if (cpc->type == ACPI_TYPE_INTEGER)
		return true;

	return cpc->type == ACPI_TYPE_BUFFER &&
	       !IS_NULL_REG(&cpc->cpc_entry.reg);
}

/*
 * Each bit indicates the optionality of the register in per-cpu
 * cpc_regs[] with the corresponding index. 0 means mandatory and 1
 * means optional.
 */
#define REG_OPTIONAL (0x7FC7D0)

/*
 * Use the index of the register in per-cpu cpc_regs[] to check if
 * it's an optional one.
 */
#define IS_OPTIONAL_CPC_REG(reg_idx) (REG_OPTIONAL & (1U << (reg_idx)))

static bool cpc_integer_entry_valid(unsigned int reg_idx, u64 value,
				    bool *legacy_null)
{
	*legacy_null = false;

	switch (reg_idx) {
	case HIGHEST_PERF:
	case NOMINAL_PERF:
	case LOW_NON_LINEAR_PERF:
	case LOWEST_PERF:
	case REFERENCE_PERF:
	case LOWEST_FREQ:
	case NOMINAL_FREQ:
		return value <= U32_MAX;
	case CTR_WRAP_TIME:
		/* AML Integers and the kernel interface are both 64-bit. */
		return true;
	case AUTO_SEL_ENABLE:
		return value <= 1;
	case DESIRED_PERF:
		/* Validated against Autonomous Selection after parsing. */
		*legacy_null = value == 0;
		return *legacy_null;
	default:
		/* Tolerate legacy Integer 0 placeholders for absent options. */
		*legacy_null = value == 0 && IS_OPTIONAL_CPC_REG(reg_idx);
		return *legacy_null;
	}
}

/*
 * Arbitrary Retries in case the remote processor is slow to respond
 * to PCC commands. Keeping it high enough to cover emulators where
 * the processors run painfully slow.
 */
#define NUM_RETRIES 500ULL

#define CPC_GENERIC_REGISTER_DESCRIPTOR 0x82
#define CPC_GENERIC_REGISTER_LENGTH (sizeof(struct cpc_reg) - 3)

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
show_cppc_data(cppc_get_perf_caps, cppc_perf_caps, reference_perf);
show_cppc_data(cppc_get_perf_caps, cppc_perf_caps, lowest_nonlinear_perf);
show_cppc_data(cppc_get_perf_caps, cppc_perf_caps, guaranteed_perf);
show_cppc_data(cppc_get_perf_caps, cppc_perf_caps, lowest_freq);
show_cppc_data(cppc_get_perf_caps, cppc_perf_caps, nominal_freq);

show_cppc_data(cppc_get_perf_ctrs, cppc_perf_fb_ctrs, wraparound_time);

/*
 * PCC reuses the access_width field as the subspace id, so only decode access
 * size for non-PCC registers. Otherwise, use the bit_width.
 */
#define GET_BIT_WIDTH(reg) (((reg)->access_width &&				\
			     (reg)->space_id != ACPI_ADR_SPACE_PLATFORM_COMM) ? \
			    (8 << ((reg)->access_width - 1)) : (reg)->bit_width)

/* Shift and apply the mask for CPC reads/writes */
#define MASK_VAL_READ(reg, val) (((val) >> (reg)->bit_offset) &				\
					GENMASK(((reg)->bit_width) - 1, 0))
#define MASK_VAL_WRITE(reg, prev_val, val)						\
	((((val) & GENMASK(((reg)->bit_width) - 1, 0)) << (reg)->bit_offset) |		\
	((prev_val) & ~(GENMASK(((reg)->bit_width) - 1, 0) << (reg)->bit_offset)))	\

static unsigned int cpc_reg_access_width(const struct cpc_reg *reg)
{
	if (reg->access_width > 4)
		return 0;

	if (reg->access_width)
		return 8U << (reg->access_width - 1);

	return reg->bit_width;
}

enum cpc_platform_quirk {
	CPC_QUIRK_PERF_LIMITED_OWNS_UNIT = BIT(0),
};

static const struct acpi_platform_list cpc_platform_quirk_list[] = {
	{
		.oem_id = "NVIDIA",
		.oem_table_id = "T41",
		.table = ACPI_SIG_DSDT,
		.pred = all_versions,
		.reason = "Performance Limited owns its access unit",
		.data = CPC_QUIRK_PERF_LIMITED_OWNS_UNIT,
	},
	{ }
};

static DEFINE_MUTEX(cpc_platform_quirk_lock);
static bool cpc_platform_quirks_initialized;
static u32 cpc_platform_quirks;

static int cpc_get_platform_quirks(u32 *quirks)
{
	int idx, ret = 0;

	mutex_lock(&cpc_platform_quirk_lock);
	if (!cpc_platform_quirks_initialized) {
		idx = acpi_match_platform_list(cpc_platform_quirk_list);
		if (idx < 0 && idx != -ENODEV) {
			ret = idx;
			goto out;
		}
		if (idx >= 0)
			cpc_platform_quirks = cpc_platform_quirk_list[idx].data;
		cpc_platform_quirks_initialized = true;
	}
	*quirks = cpc_platform_quirks;
out:
	mutex_unlock(&cpc_platform_quirk_lock);

	return ret;
}

static void cpc_apply_platform_quirks(struct cpc_reg *reg,
				      unsigned int reg_idx, u32 quirks)
{
	unsigned int access_width;

	if (!(quirks & CPC_QUIRK_PERF_LIMITED_OWNS_UNIT) ||
	    reg_idx != PERF_LIMITED ||
	    reg->space_id != ACPI_ADR_SPACE_SYSTEM_MEMORY ||
	    reg->bit_width != 2 || reg->bit_offset)
		return;

	access_width = cpc_reg_access_width(reg);
	if (access_width != 32)
		return;

	reg->bit_width = access_width;
	pr_info_once("firmware quirk: Performance Limited owns its access unit, using Bit Width %u\n",
		     access_width);
}

static u64 cpc_sysmem_access_size(const struct cpc_register_resource *reg)
{
	unsigned int width = cpc_reg_access_width(&reg->cpc_entry.reg);

	if (width != 8 && width != 16 && width != 32 && width != 64)
		return 0;

	return width / 8;
}

static u64 cpc_sysmem_field_size(const struct cpc_reg *gas)
{
	return DIV_ROUND_UP((u64)gas->bit_offset + gas->bit_width, 8);
}

static u64 cpc_sysmem_claim_size(const struct cpc_register_resource *reg)
{
	const struct cpc_reg *gas = &reg->cpc_entry.reg;
	u64 access_size = cpc_sysmem_access_size(reg);

	if (!gas->bit_width)
		return access_size;

	return max(access_size, cpc_sysmem_field_size(gas));
}

static bool cpc_reg_access_aligned(const struct cpc_reg *reg, u64 access_size)
{
	/* x86 MMIO and port-I/O accessors support unaligned addresses. */
	return IS_ENABLED(CONFIG_X86) || IS_ALIGNED(reg->address, access_size);
}

static bool cpc_sysmem_access_units_overlap(const struct cpc_register_resource *a,
					    const struct cpc_register_resource *b)
{
	const struct cpc_reg *a_gas = &a->cpc_entry.reg;
	const struct cpc_reg *b_gas = &b->cpc_entry.reg;
	u64 a_size = cpc_sysmem_claim_size(a);
	u64 b_size = cpc_sysmem_claim_size(b);

	/* Keep the conservative locking path for malformed access widths. */
	if (!a_size || !b_size)
		return true;

	if (a_gas->address < b_gas->address)
		return b_gas->address - a_gas->address < a_size;

	return a_gas->address - b_gas->address < b_size;
}

static bool cpc_reg_is_writable(unsigned int reg_idx)
{
	/* Only controls written by this driver can be competing writers. */
	switch (reg_idx) {
	case DESIRED_PERF:
	case MIN_PERF:
	case MAX_PERF:
	case PERF_LIMITED:
	case ENABLE:
	case AUTO_SEL_ENABLE:
	case AUTO_ACT_WINDOW:
	case ENERGY_PERF:
		return true;
	default:
		return false;
	}
}

static bool cpc_reg_is_write_only(const struct cpc_desc *cpc_desc,
				  unsigned int reg_idx)
{
	return cpc_desc->version >= CPPC_V4_REV &&
	       (reg_idx == DESIRED_PERF || reg_idx == OSPM_NOMINAL_PERF);
}

static void cpc_disable_reg(struct cpc_desc *cpc_desc, unsigned int reg_idx)
{
	struct cpc_register_resource *reg = &cpc_desc->cpc_regs[reg_idx];

	reg->type = ACPI_TYPE_INTEGER;
	reg->cpc_entry.int_value = 0;
}

static bool cpc_optional_writer_can_be_disabled(unsigned int reg_idx)
{
	if (!IS_OPTIONAL_CPC_REG(reg_idx) || !cpc_reg_is_writable(reg_idx) ||
	    reg_idx == MIN_PERF || reg_idx == MAX_PERF || reg_idx == ENABLE ||
	    reg_idx == AUTO_SEL_ENABLE)
		return false;

	return true;
}

static bool cpc_sysmem_reg_needs_rmw(const struct cpc_register_resource *reg)
{
	const struct cpc_reg *gas = &reg->cpc_entry.reg;
	u64 access_size = cpc_sysmem_access_size(reg);

	return gas->bit_offset || gas->bit_width != access_size * 8;
}

static int cpc_validate_sysmem_reg(struct cpc_desc *cpc_desc,
				   const struct cpc_reg *gas,
				   unsigned int reg_idx)
{
	unsigned int access_width = cpc_reg_access_width(gas);
	u64 access_size;

	if (access_width != 8 && access_width != 16 &&
	    access_width != 32 && access_width != 64)
		goto invalid;

	if (!gas->bit_width || gas->bit_width > access_width ||
	    gas->bit_offset >= access_width ||
	    gas->bit_width > access_width - gas->bit_offset)
		goto invalid;

	access_size = access_width / 8;
	if (!gas->address || gas->address > U64_MAX - (access_size - 1))
		goto invalid;
	if (!cpc_reg_access_aligned(gas, access_size))
		goto invalid;

	if (reg_idx == PERF_LIMITED) {
		if (access_width == 64 && !IS_ENABLED(CONFIG_64BIT)) {
			pr_warn_once("CPU%d: Performance Limited register cannot be accessed atomically; keeping its range reserved\n",
				     cpc_desc->cpu_id);
			cpc_desc->cpc_regs[reg_idx].cpc_entry.read_unsupported = true;
			cpc_desc->cpc_regs[reg_idx].cpc_entry.write_unsupported = true;
			return 0;
		}

		if (gas->bit_offset || gas->bit_width != access_width) {
			pr_warn_once("CPU%d: Performance Limited register cannot be cleared safely; keeping it readable\n",
				     cpc_desc->cpu_id);
			cpc_desc->cpc_regs[reg_idx].cpc_entry.write_unsupported = true;
		}
	}

	return 0;

invalid:
	access_size = 0;
	if (access_width == 8 || access_width == 16 ||
	    access_width == 32 || access_width == 64)
		access_size = access_width / 8;
	if (gas->bit_width)
		access_size = max(access_size, cpc_sysmem_field_size(gas));
	if ((cpc_reg_is_write_only(cpc_desc, reg_idx) ||
	     reg_idx == PERF_LIMITED) && gas->address && access_size &&
	    gas->address <= U64_MAX - (access_size - 1)) {
		struct cpc_register_resource *reg = &cpc_desc->cpc_regs[reg_idx];

		if (reg_idx == PERF_LIMITED)
			pr_warn_once("CPU%d: _CPC v%d register %u is inaccessible; keeping its range reserved\n",
				     cpc_desc->cpu_id, cpc_desc->version, reg_idx);
		else
			pr_warn("CPU%d: _CPC v%d register %u is inaccessible; keeping its range reserved\n",
				cpc_desc->cpu_id, cpc_desc->version, reg_idx);
		reg->cpc_entry.read_unsupported = true;
		reg->cpc_entry.write_unsupported = true;
		return 0;
	}

	pr_debug("CPU:%d invalid SystemMemory GAS for _CPC register %u\n",
		 cpc_desc->cpu_id, reg_idx);
	return -EINVAL;
}

static bool cpc_immutable_autonomous(const struct cpc_desc *cpc_desc)
{
	const struct cpc_register_resource *reg;

	reg = &cpc_desc->cpc_regs[AUTO_SEL_ENABLE];
	return osc_sb_cppc2_support_acked && reg->type == ACPI_TYPE_INTEGER &&
	       reg->cpc_entry.int_value == 1;
}

static bool cpc_retain_pcc_status(struct cpc_desc *cpc_desc,
				  unsigned int reg_idx);

static int cpc_resolve_unsupported(struct cpc_desc *cpc_desc,
				   u32 unsupported)
{
	unsigned int i;
	u32 bounds = BIT(MIN_PERF) | BIT(MAX_PERF);
	bool min_unusable, max_unusable;

	if (unsupported & bounds) {
		min_unusable = (unsupported & BIT(MIN_PERF)) ||
			       !cpc_is_writable(&cpc_desc->cpc_regs[MIN_PERF]);
		max_unusable = (unsupported & BIT(MAX_PERF)) ||
			       !cpc_is_writable(&cpc_desc->cpc_regs[MAX_PERF]);
		if (min_unusable && max_unusable) {
			pr_warn("CPU%d: ignoring inaccessible Minimum and Maximum Performance registers\n",
				cpc_desc->cpu_id);
			cpc_disable_reg(cpc_desc, MIN_PERF);
			cpc_disable_reg(cpc_desc, MAX_PERF);
			unsupported &= ~bounds;
		}
	}

	for (i = 0; i < cpc_desc->num_entries - 2; i++) {
		if (!(unsupported & BIT(i)))
			continue;

		/* CPPC control does not depend on Performance Limited status. */
		if (i == PERF_LIMITED) {
			if (CPC_IN_PCC(&cpc_desc->cpc_regs[i]) &&
			    cpc_retain_pcc_status(cpc_desc, i))
				continue;

			pr_warn_once("CPU%d: ignoring inaccessible Performance Limited register\n",
				     cpc_desc->cpu_id);
			cpc_disable_reg(cpc_desc, i);
			continue;
		}

		if (i == DESIRED_PERF && cpc_immutable_autonomous(cpc_desc)) {
			pr_warn("CPU%d: ignoring inaccessible Desired Performance register in autonomous mode\n",
				cpc_desc->cpu_id);
			cpc_disable_reg(cpc_desc, i);
			continue;
		}

		/*
		 * A present Enable or Autonomous Selection control must remain
		 * usable.  Disabling the latter could leave autonomous selection
		 * enabled while OSPM believes that it has disabled it.
		 */
		if (i == ENABLE ||
		    (i == AUTO_SEL_ENABLE && cpc_entry_present(&cpc_desc->cpc_regs[i])) ||
		    i == MIN_PERF || i == MAX_PERF ||
		    !IS_OPTIONAL_CPC_REG(i)) {
			pr_err("CPU%d: cannot access _CPC register %u\n",
			       cpc_desc->cpu_id, i);
			return -EINVAL;
		}

		pr_warn("CPU%d: ignoring inaccessible optional _CPC register %u\n",
			cpc_desc->cpu_id, i);
		cpc_disable_reg(cpc_desc, i);
	}

	return 0;
}

static int cpc_validate_required_controls(struct cpc_desc *cpc_desc)
{
	unsigned int i;

	/*
	 * Performance Limited is required by the specification, but tolerate a
	 * NULL descriptor used by firmware which cannot report limiting events.
	 * CPPC control does not depend on this status.
	 */
	for (i = 0; i < cpc_desc->num_entries - 2; i++) {
		if (i != DESIRED_PERF && i != PERF_LIMITED &&
		    !IS_OPTIONAL_CPC_REG(i) &&
		    !cpc_entry_present(&cpc_desc->cpc_regs[i])) {
			pr_debug("CPU:%d lacks mandatory _CPC register %u\n",
				 cpc_desc->cpu_id, i);
			return -EINVAL;
		}
	}

	/* Desired may be absent only for immutable autonomous operation. */
	if (!cpc_is_writable(&cpc_desc->cpc_regs[DESIRED_PERF]) &&
	    !cpc_immutable_autonomous(cpc_desc)) {
		pr_debug("CPU:%d lacks a writable Desired Performance register\n",
			 cpc_desc->cpu_id);
		return -EINVAL;
	}

	return 0;
}

static int cpc_validate_bound_controls(struct cpc_desc *cpc_desc)
{
	bool have_min, have_max;

	have_min = cpc_is_writable(&cpc_desc->cpc_regs[MIN_PERF]);
	have_max = cpc_is_writable(&cpc_desc->cpc_regs[MAX_PERF]);
	if (have_min != have_max) {
		pr_err("CPU%d: _CPC must provide both Minimum and Maximum Performance or neither\n",
		       cpc_desc->cpu_id);
		return -EINVAL;
	}

	return 0;
}

static bool
cpc_retain_pcc_status(struct cpc_desc *cpc_desc, unsigned int reg_idx)
{
	struct cpc_register_resource *reg = &cpc_desc->cpc_regs[reg_idx];
	const struct cpc_reg *gas = &reg->cpc_entry.reg;
	u64 size;

	if (reg_idx != PERF_LIMITED || !gas->bit_width)
		return false;

	size = DIV_ROUND_UP((u64)gas->bit_offset + gas->bit_width, 8);
	if (!size || gas->address > U64_MAX - (size - 1))
		return false;

	reg->cpc_entry.read_unsupported = true;
	reg->cpc_entry.write_unsupported = true;
	pr_warn_once("CPU%d: Performance Limited register cannot be accessed; keeping its PCC range reserved\n",
		     cpc_desc->cpu_id);
	return true;
}

static bool
cpc_retain_sysio_status(struct cpc_desc *cpc_desc, unsigned int reg_idx)
{
	struct cpc_register_resource *reg = &cpc_desc->cpc_regs[reg_idx];
	const struct cpc_reg *gas = &reg->cpc_entry.reg;

	if (reg_idx != PERF_LIMITED || !gas->bit_width)
		return false;

	/* Retain any in-range portion for overlap validation only. */
	if (gas->address > U16_MAX)
		return false;

	pr_warn_once("CPU%d: Performance Limited register cannot be accessed; keeping its SystemIO range reserved\n",
		     cpc_desc->cpu_id);
	reg->cpc_entry.read_unsupported = true;
	reg->cpc_entry.write_unsupported = true;
	return true;
}

static u64 cpc_non_mmio_access_size(const struct cpc_register_resource *reg)
{
	const struct cpc_reg *gas = &reg->cpc_entry.reg;

	if (gas->space_id == ACPI_ADR_SPACE_PLATFORM_COMM)
		return DIV_ROUND_UP((u64)gas->bit_offset + gas->bit_width, 8);

	return max((u64)cpc_reg_access_width(gas) / 8,
		   DIV_ROUND_UP((u64)gas->bit_offset + gas->bit_width, 8));
}

static void cpc_validate_pcc_bounds(struct cpc_desc *cpc_desc,
				    int pcc_ss_id, struct cppc_pcc_data *data,
				    u32 *unsupported)
{
	u64 shmem_size = data->pcc_channel->shmem_size;
	unsigned int i;

	for (i = 0; i < cpc_desc->num_entries - 2; i++) {
		struct cpc_register_resource *reg = &cpc_desc->cpc_regs[i];
		struct cpc_reg *gas;
		u64 access_size;

		if ((*unsupported & BIT(i)) || !CPC_SUPPORTED(reg) ||
		    !CPC_IN_PCC(reg))
			continue;

		gas = &reg->cpc_entry.reg;
		if (gas->access_width != pcc_ss_id)
			continue;
		access_size = cpc_non_mmio_access_size(reg);
		if (shmem_size >= CPC_PCC_HEADER_SIZE &&
		    gas->address <= shmem_size - CPC_PCC_HEADER_SIZE &&
		    access_size <= shmem_size - CPC_PCC_HEADER_SIZE - gas->address)
			continue;

		pr_debug("CPU%d: _CPC register %u exceeds the PCC shared region\n",
			 cpc_desc->cpu_id, i);
		*unsupported |= BIT(i);
	}
}

static bool cpc_pcc_access_needed(const struct cpc_desc *cpc_desc)
{
	unsigned int i;

	for (i = 0; i < cpc_desc->num_entries - 2; i++) {
		const struct cpc_register_resource *reg = &cpc_desc->cpc_regs[i];

		if (CPC_SUPPORTED(reg) && CPC_IN_PCC(reg) &&
		    (cpc_is_readable(reg) || cpc_is_writable(reg)))
			return true;
	}

	return false;
}

static bool cpc_non_mmio_overlap_conflicts(u8 space_id, bool a_writable,
					   bool b_writable, bool a_write_only,
					   bool b_write_only)
{
	/* Only a write-only control can use a separate read-side port alias. */
	if (space_id == ACPI_ADR_SPACE_SYSTEM_IO && a_writable != b_writable)
		return a_writable ? !a_write_only : !b_write_only;

	return a_writable || b_writable;
}

static bool
cpc_sysio_perf_limited_conflicts(unsigned int a_idx, bool a_writable,
				 unsigned int b_idx, bool b_writable)
{
	return (a_idx == PERF_LIMITED && b_writable) ||
	       (b_idx == PERF_LIMITED && a_writable);
}

static struct rb_root_cached *cpc_non_mmio_tree(u8 space_id, u8 pcc_ss_id)
{
	if (space_id == ACPI_ADR_SPACE_PLATFORM_COMM)
		return &cpc_pcc_trees[pcc_ss_id];
	if (space_id == ACPI_ADR_SPACE_SYSTEM_IO)
		return &cpc_sysio_tree;
	return NULL;
}

static bool cpc_same_non_mmio_register(const struct cpc_non_mmio_node *a,
				       const struct cpc_non_mmio_node *b)
{
	const struct cpc_reg *a_gas =
		&a->desc->cpc_regs[a->reg_idx].cpc_entry.reg;
	const struct cpc_reg *b_gas =
		&b->desc->cpc_regs[b->reg_idx].cpc_entry.reg;

	return a->space_id == b->space_id && a->pcc_ss_id == b->pcc_ss_id &&
	       a->reg_idx == b->reg_idx && a->start == b->start &&
	       a->last == b->last && a_gas->bit_offset == b_gas->bit_offset &&
	       a_gas->bit_width == b_gas->bit_width &&
	       (a->space_id == ACPI_ADR_SPACE_PLATFORM_COMM ||
		cpc_reg_access_width(a_gas) == cpc_reg_access_width(b_gas));
}

static int cpc_validate_non_mmio_pair(const struct cpc_non_mmio_node *a,
				      const struct cpc_non_mmio_node *b)
{
	const struct cpc_register_resource *a_reg;
	const struct cpc_register_resource *b_reg;
	bool a_writable, b_writable;
	const char *name;

	a_reg = &a->desc->cpc_regs[a->reg_idx];
	b_reg = &b->desc->cpc_regs[b->reg_idx];
	a_writable = cpc_reg_is_writable(a->reg_idx) && cpc_is_writable(a_reg);
	b_writable = cpc_reg_is_writable(b->reg_idx) && cpc_is_writable(b_reg);

	if (!cpc_non_mmio_overlap_conflicts(a->space_id, a_writable,
					    b_writable,
					    cpc_reg_is_write_only(a->desc, a->reg_idx),
					    cpc_reg_is_write_only(b->desc, b->reg_idx)) &&
	    !(a->space_id == ACPI_ADR_SPACE_SYSTEM_IO &&
	      cpc_sysio_perf_limited_conflicts(a->reg_idx, a_writable,
					       b->reg_idx, b_writable)))
		return 0;

	if (cpc_same_non_mmio_register(a, b))
		return 0;

	name = a->space_id == ACPI_ADR_SPACE_PLATFORM_COMM ?
	       "PCC" : "SystemIO";
	pr_err("CPU%d: %s _CPC register %u conflicts with CPU%d register %u\n",
	       a->desc->cpu_id, name, a->reg_idx, b->desc->cpu_id,
	       b->reg_idx);
	return -EINVAL;
}

static void cpc_unregister_non_mmio_desc_locked(struct cpc_desc *cpc_desc)
{
	unsigned int i;

	if (!cpc_desc->non_mmio_nodes)
		return;

	for (i = 0; i < cpc_desc->num_entries - 2; i++) {
		struct cpc_non_mmio_node *node = &cpc_desc->non_mmio_nodes[i];
		struct rb_root_cached *tree;

		if (!node->registered)
			continue;

		tree = cpc_non_mmio_tree(node->space_id, node->pcc_ss_id);
		cpc_non_mmio_itree_remove(node, tree);
	}

	kfree(cpc_desc->non_mmio_nodes);
	cpc_desc->non_mmio_nodes = NULL;
}

static int cpc_register_non_mmio_desc(struct cpc_desc *cpc_desc)
{
	unsigned int nr_regs = cpc_desc->num_entries - 2;
	unsigned int i;
	int ret = 0;
	bool found = false;

	for (i = 0; i < nr_regs; i++) {
		struct cpc_register_resource *reg = &cpc_desc->cpc_regs[i];
		u8 space_id;

		if (!CPC_SUPPORTED(reg) || reg->type != ACPI_TYPE_BUFFER)
			continue;
		space_id = reg->cpc_entry.reg.space_id;
		if (space_id == ACPI_ADR_SPACE_PLATFORM_COMM ||
		    space_id == ACPI_ADR_SPACE_SYSTEM_IO) {
			found = true;
			break;
		}
	}
	if (!found)
		return 0;

	cpc_desc->non_mmio_nodes = kcalloc(nr_regs,
					   sizeof(*cpc_desc->non_mmio_nodes),
					   GFP_KERNEL);
	if (!cpc_desc->non_mmio_nodes)
		return -ENOMEM;

	mutex_lock(&cpc_non_mmio_lock);

	for (i = 0; i < nr_regs; i++) {
		struct cpc_register_resource *reg = &cpc_desc->cpc_regs[i];
		struct cpc_non_mmio_node *match, *node;
		struct rb_root_cached *tree;
		u8 space_id;
		u64 size;

		if (!CPC_SUPPORTED(reg) || reg->type != ACPI_TYPE_BUFFER)
			continue;

		space_id = reg->cpc_entry.reg.space_id;
		if (space_id != ACPI_ADR_SPACE_PLATFORM_COMM &&
		    space_id != ACPI_ADR_SPACE_SYSTEM_IO)
			continue;

		node = &cpc_desc->non_mmio_nodes[i];
		size = cpc_non_mmio_access_size(reg);
		node->start = reg->cpc_entry.reg.address;
		node->last = node->start + size - 1;
		node->desc = cpc_desc;
		node->reg_idx = i;
		node->space_id = space_id;
		node->pcc_ss_id = space_id == ACPI_ADR_SPACE_PLATFORM_COMM ?
				      reg->cpc_entry.reg.access_width : 0;
		tree = cpc_non_mmio_tree(space_id, node->pcc_ss_id);

		match = cpc_non_mmio_itree_iter_first(tree, node->start,
						      node->last);
		while (match) {
			ret = cpc_validate_non_mmio_pair(node, match);
			if (ret)
				goto out_unregister;

			match = cpc_non_mmio_itree_iter_next(match, node->start,
							     node->last);
		}
		cpc_non_mmio_itree_insert(node, tree);
		node->registered = true;
	}

	mutex_unlock(&cpc_non_mmio_lock);
	return 0;

out_unregister:
	cpc_unregister_non_mmio_desc_locked(cpc_desc);
	mutex_unlock(&cpc_non_mmio_lock);
	return ret;
}

static void cpc_unregister_non_mmio_desc(struct cpc_desc *cpc_desc)
{
	if (!cpc_desc->non_mmio_nodes)
		return;

	mutex_lock(&cpc_non_mmio_lock);
	cpc_unregister_non_mmio_desc_locked(cpc_desc);
	mutex_unlock(&cpc_non_mmio_lock);
}

static void cpc_mark_rmw_lock_users(struct cpc_desc *cpc_desc)
{
	int i;

	for (i = 0; i < cpc_desc->num_entries - 2; i++) {
		struct cpc_register_resource *reg = &cpc_desc->cpc_regs[i];

		if (CPC_SUPPORTED(reg) && CPC_IN_SYSTEM_MEMORY(reg) &&
		    cpc_is_writable(reg))
			reg->cpc_entry.use_rmw_lock =
				cpc_sysmem_reg_needs_rmw(reg);
	}
}

struct cpc_bit_position {
	u64 byte;
	u8 bit;
};

static bool cpc_bit_position_before(const struct cpc_bit_position *a,
				    const struct cpc_bit_position *b)
{
	return a->byte < b->byte || (a->byte == b->byte && a->bit < b->bit);
}

static bool cpc_sysmem_fields_overlap(const struct cpc_register_resource *a,
				      const struct cpc_register_resource *b)
{
	const struct cpc_reg *a_gas = &a->cpc_entry.reg;
	const struct cpc_reg *b_gas = &b->cpc_entry.reg;
	unsigned int a_last_bit = a_gas->bit_offset + a_gas->bit_width - 1;
	unsigned int b_last_bit = b_gas->bit_offset + b_gas->bit_width - 1;
	struct cpc_bit_position a_start = {
		.byte = a_gas->address + a_gas->bit_offset / 8,
		.bit = a_gas->bit_offset % 8,
	};
	struct cpc_bit_position a_end = {
		.byte = a_gas->address + a_last_bit / 8,
		.bit = a_last_bit % 8,
	};
	struct cpc_bit_position b_start = {
		.byte = b_gas->address + b_gas->bit_offset / 8,
		.bit = b_gas->bit_offset % 8,
	};
	struct cpc_bit_position b_end = {
		.byte = b_gas->address + b_last_bit / 8,
		.bit = b_last_bit % 8,
	};

	return !cpc_bit_position_before(&a_end, &b_start) &&
	       !cpc_bit_position_before(&b_end, &a_start);
}

static bool cpc_sysmem_access_overlaps_field(const struct cpc_register_resource *access,
					     const struct cpc_register_resource *field)
{
	const struct cpc_reg *access_gas = &access->cpc_entry.reg;
	const struct cpc_reg *field_gas = &field->cpc_entry.reg;
	u64 access_last;
	u64 field_start;
	u64 field_last;

	if (!field_gas->bit_width)
		return cpc_sysmem_access_units_overlap(access, field);

	access_last = access_gas->address +
		      cpc_sysmem_access_size(access) - 1;
	field_start = field_gas->address + field_gas->bit_offset / 8;
	field_last = field_gas->address +
		     (field_gas->bit_offset + field_gas->bit_width - 1) / 8;

	return access_gas->address <= field_last && field_start <= access_last;
}

static bool cpc_same_sysmem_register(unsigned int a_idx,
				     const struct cpc_register_resource *a,
				     unsigned int b_idx,
				     const struct cpc_register_resource *b)
{
	const struct cpc_reg *a_gas = &a->cpc_entry.reg;
	const struct cpc_reg *b_gas = &b->cpc_entry.reg;

	return a_idx == b_idx &&
	       a_gas->address == b_gas->address &&
	       a_gas->bit_width == b_gas->bit_width &&
	       a_gas->bit_offset == b_gas->bit_offset &&
	       cpc_reg_access_width(a_gas) == cpc_reg_access_width(b_gas);
}

static int cpc_validate_sysmem_pair(const struct cpc_desc *a_desc,
				    unsigned int a_idx,
				    const struct cpc_desc *b_desc,
				    unsigned int b_idx)
{
	const struct cpc_register_resource *a = &a_desc->cpc_regs[a_idx];
	const struct cpc_register_resource *b = &b_desc->cpc_regs[b_idx];
	bool a_write_only, b_write_only;
	bool a_writable, b_writable;
	bool fields_overlap;

	/* The overlap helper includes each descriptor's conservative claim. */
	if (!CPC_SUPPORTED(a) || !CPC_IN_SYSTEM_MEMORY(a) ||
	    !CPC_SUPPORTED(b) || !CPC_IN_SYSTEM_MEMORY(b) ||
	    !cpc_sysmem_access_units_overlap(a, b))
		return 0;

	a_write_only = cpc_reg_is_write_only(a_desc, a_idx);
	b_write_only = cpc_reg_is_write_only(b_desc, b_idx);
	fields_overlap = !a->cpc_entry.reg.bit_width ||
			 !b->cpc_entry.reg.bit_width ||
			 cpc_sysmem_fields_overlap(a, b);
	/* A readable field must not expose another field's undefined bits. */
	if (a_write_only != b_write_only &&
	    cpc_is_readable(a_write_only ? b : a) &&
	    fields_overlap)
		goto conflict;

	a_writable = cpc_reg_is_writable(a_idx) && cpc_is_writable(a);
	b_writable = cpc_reg_is_writable(b_idx) && cpc_is_writable(b);
	if (!a_writable && !b_writable)
		return 0;

	if (cpc_same_sysmem_register(a_idx, a, b_idx, b)) {
		u64 access_size = cpc_sysmem_access_size(a);

		/*
		 * Exact partial aliases update the same field and retain
		 * last-writer-wins semantics when the complete access is one native
		 * transaction.  A 64-bit MMIO write may be split on 32-bit kernels,
		 * and an unaligned x86 access is not guaranteed to be one device
		 * transaction.
		 */
		if (!a_writable ||
		    (IS_ALIGNED(a->cpc_entry.reg.address, access_size) &&
		     (access_size < sizeof(u64) ||
		      IS_ENABLED(CONFIG_64BIT))))
			return 0;
		goto conflict;
	}

	/*
	 * The platform may set Performance Limited asynchronously.  A write to
	 * another field in the same access unit could write back stale status
	 * bits, which an OSPM lock cannot prevent.
	 */
	if ((a_idx == PERF_LIMITED && b_writable) ||
	    (b_idx == PERF_LIMITED && a_writable))
		goto conflict;

	/* A full-width writer must not overwrite another logical field. */
	if (fields_overlap &&
	    ((a_writable && b_writable) ||
	     (a_writable && !cpc_sysmem_reg_needs_rmw(a)) ||
	     (b_writable && !cpc_sysmem_reg_needs_rmw(b))))
		goto conflict;

	/* Different descriptors do not share their partial-write locks. */
	if (a_desc != b_desc && a_writable && b_writable)
		goto conflict;

	/*
	 * RMW of either writer preserves the other field.  If that field is
	 * write-only, its readback is undefined and cannot safely be replayed.
	 */
	if ((a_write_only && b_writable &&
	     cpc_sysmem_reg_needs_rmw(b) &&
	     cpc_sysmem_access_overlaps_field(b, a)) ||
	    (b_write_only && a_writable &&
	     cpc_sysmem_reg_needs_rmw(a) &&
	     cpc_sysmem_access_overlaps_field(a, b)))
		goto conflict;

	return 0;

conflict:
	pr_err("CPU%d: SystemMemory _CPC register %u conflicts with CPU%d register %u\n",
	       a_desc->cpu_id, a_idx, b_desc->cpu_id, b_idx);
	return -EINVAL;
}

static bool cpc_disable_new_sysmem_writer(struct cpc_desc *cpc_desc,
					  unsigned int reg_idx,
					  const struct cpc_sysmem_node *node)
{
	struct cpc_register_resource *reg = &cpc_desc->cpc_regs[reg_idx];
	struct cpc_sysmem_node *match;
	unsigned int status_cpu = 0;
	bool found = false;

	if (!cpc_optional_writer_can_be_disabled(reg_idx) ||
	    !cpc_is_writable(reg))
		return false;

	match = cpc_sysmem_first(node->start, node->last);
	while (match) {
		const struct cpc_register_resource *status;

		if (match->reg_idx == PERF_LIMITED) {
			status = &match->desc->cpc_regs[PERF_LIMITED];
			if (!status->cpc_entry.reg.bit_width ||
			    cpc_sysmem_fields_overlap(reg, status))
				return false;
			status_cpu = match->desc->cpu_id;
			found = true;
		}

		match = cpc_sysmem_next(match, node->start, node->last);
	}
	if (!found)
		return false;

	pr_warn_once("CPU%d: ignoring optional _CPC register %u sharing CPU%d Performance Limited access unit\n",
		     cpc_desc->cpu_id, reg_idx, status_cpu);
	cpc_disable_reg(cpc_desc, reg_idx);
	return true;
}

static void cpc_unregister_sysmem_desc_locked(struct cpc_desc *cpc_desc)
{
	unsigned int i;

	if (!cpc_desc->sysmem_nodes)
		return;

	for (i = 0; i < cpc_desc->num_entries - 2; i++) {
		struct cpc_sysmem_node *node = &cpc_desc->sysmem_nodes[i];
		struct cpc_sysmem_node *alias, *child;

		if (node->alias_of) {
			list_del(&node->alias_node);
			continue;
		}
		if (!node->registered)
			continue;

		cpc_sysmem_itree_remove(node, &cpc_sysmem_tree);
		node->registered = false;
		if (list_empty(&node->aliases))
			continue;

		/* Keep one representative for aliases owned by live descriptors. */
		alias = list_first_entry(&node->aliases,
					 struct cpc_sysmem_node, alias_node);
		list_del_init(&alias->alias_node);
		alias->alias_of = NULL;
		alias->registered = true;
		list_splice_init(&node->aliases, &alias->aliases);
		list_for_each_entry(child, &alias->aliases, alias_node)
			child->alias_of = alias;
		cpc_sysmem_itree_insert(alias, &cpc_sysmem_tree);
	}

	kfree(cpc_desc->sysmem_nodes);
	cpc_desc->sysmem_nodes = NULL;
}

static int cpc_register_sysmem_desc(struct cpc_desc *cpc_desc)
{
	unsigned int nr_regs = cpc_desc->num_entries - 2;
	unsigned int i;
	int ret = 0;
	bool found = false;

	for (i = 0; i < nr_regs; i++) {
		struct cpc_register_resource *reg = &cpc_desc->cpc_regs[i];

		if (CPC_SUPPORTED(reg) && CPC_IN_SYSTEM_MEMORY(reg)) {
			found = true;
			break;
		}
	}
	if (!found)
		return 0;

	cpc_desc->sysmem_nodes = kcalloc(nr_regs,
					 sizeof(*cpc_desc->sysmem_nodes),
					 GFP_KERNEL);
	if (!cpc_desc->sysmem_nodes)
		return -ENOMEM;

	mutex_lock(&cpc_sysmem_lock);

	for (i = 0; i < nr_regs; i++) {
		struct cpc_register_resource *reg = &cpc_desc->cpc_regs[i];
		struct cpc_sysmem_node *alias = NULL, *match, *node;
		u64 size;

		if (!CPC_SUPPORTED(reg) || !CPC_IN_SYSTEM_MEMORY(reg))
			continue;

		node = &cpc_desc->sysmem_nodes[i];
		size = cpc_sysmem_claim_size(reg);
		node->start = reg->cpc_entry.reg.address;
		node->last = node->start + size - 1;
		node->desc = cpc_desc;
		node->reg_idx = i;
		INIT_LIST_HEAD(&node->aliases);
		INIT_LIST_HEAD(&node->alias_node);

		/* Performance Limited precedes every optional writer we may disable. */
		if (cpc_disable_new_sysmem_writer(cpc_desc, i, node))
			continue;

		match = cpc_sysmem_first(node->start, node->last);
		while (match) {
			struct cpc_register_resource *match_reg;

			match_reg = &match->desc->cpc_regs[match->reg_idx];
			ret = cpc_validate_sysmem_pair(cpc_desc, i, match->desc,
						       match->reg_idx);
			if (ret)
				goto out_unregister;
			if (cpc_desc == match->desc) {
				reg->cpc_entry.use_rmw_lock = true;
				match_reg->cpc_entry.use_rmw_lock = true;
			}
			if (cpc_same_sysmem_register(i, reg, match->reg_idx, match_reg))
				alias = match;

			match = cpc_sysmem_next(match, node->start, node->last);
		}
		if (alias) {
			node->alias_of = alias;
			list_add_tail(&node->alias_node, &alias->aliases);
			continue;
		}

		cpc_sysmem_itree_insert(node, &cpc_sysmem_tree);
		node->registered = true;
	}

	mutex_unlock(&cpc_sysmem_lock);
	return 0;

out_unregister:
	cpc_unregister_sysmem_desc_locked(cpc_desc);
	mutex_unlock(&cpc_sysmem_lock);
	return ret;
}

static void cpc_unregister_sysmem_desc(struct cpc_desc *cpc_desc)
{
	if (!cpc_desc->sysmem_nodes)
		return;

	mutex_lock(&cpc_sysmem_lock);
	cpc_unregister_sysmem_desc_locked(cpc_desc);
	mutex_unlock(&cpc_sysmem_lock);
}

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

static void cppc_free_desc(struct cpc_desc *cpc_ptr)
{
	unsigned int i;

	cpc_unregister_non_mmio_desc(cpc_ptr);
	cpc_unregister_sysmem_desc(cpc_ptr);

	for (i = 2; i < cpc_ptr->num_entries; i++) {
		void __iomem *addr = cpc_ptr->cpc_regs[i - 2].sys_mem_vaddr;

		if (addr)
			iounmap(addr);
	}

	kfree(cpc_ptr);
}

static void cppc_kobj_release(struct kobject *kobj)
{
	cppc_free_desc(to_cpc_desc(kobj));
}

static const struct kobj_type cppc_ktype = {
	.release = cppc_kobj_release,
	.sysfs_ops = &kobj_sysfs_ops,
	.default_groups = cppc_groups,
};

static int check_pcc_chan(int pcc_ss_id, bool chk_err_bit)
{
	int ret, status;
	struct cppc_pcc_data *pcc_ss_data = pcc_data[pcc_ss_id];
	struct acpi_pcct_shared_memory __iomem *generic_comm_base =
					pcc_ss_data->pcc_channel->shmem;

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
		/* Order completion status before reading the returned payload. */
		rmb();
		pcc_ss_data->platform_owns_pcc = false;
		if (chk_err_bit && (status & PCC_ERROR_MASK))
			ret = -EIO;
	}

	if (unlikely(ret))
		pr_err("PCC check channel failed for ss: %d. ret=%d\n",
		       pcc_ss_id, ret);

	return ret;
}

static void cppc_complete_pcc_write(int pcc_ss_id,
				    struct cppc_pcc_data *pcc_ss_data, int ret)
{
	int i;

	if (unlikely(ret)) {
		for_each_possible_cpu(i) {
			struct cpc_desc *desc = per_cpu(cpc_desc_ptr, i);

			if (!desc ||
			    per_cpu(cpu_pcc_subspace_idx, i) != pcc_ss_id)
				continue;

			if (desc->write_cmd_id == pcc_ss_data->pcc_write_cnt)
				desc->write_cmd_status = ret;
		}
	}

	pcc_ss_data->pcc_write_cnt++;
	wake_up_all(&pcc_ss_data->pcc_write_wait_q);
}

/* The caller must hold pcc_lock for write. */
static void cppc_abort_pending_pcc_write(int pcc_ss_id,
					 struct cppc_pcc_data *pcc_ss_data,
					 int ret)
{
	if (!pcc_ss_data->pending_pcc_write_cmd)
		return;

	pcc_ss_data->pending_pcc_write_cmd = false;
	cppc_complete_pcc_write(pcc_ss_id, pcc_ss_data, ret);
}

/*
 * This function transfers the ownership of the PCC to the platform
 * So it must be called while holding write_lock(pcc_lock)
 */
static int send_pcc_cmd(int pcc_ss_id, u16 cmd)
{
	int ret = -EIO;
	struct cppc_pcc_data *pcc_ss_data = pcc_data[pcc_ss_id];
	struct acpi_pcct_shared_memory __iomem *generic_comm_base =
					pcc_ss_data->pcc_channel->shmem;
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
	if (cmd == CMD_WRITE)
		cppc_complete_pcc_write(pcc_ss_id, pcc_ss_data, ret);

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

	for_each_online_cpu(cpu) {
		cpc_ptr = per_cpu(cpc_desc_ptr, cpu);
		if (!cpc_ptr)
			return false;
	}

	return true;
}
EXPORT_SYMBOL_GPL(acpi_cpc_valid);

bool cppc_allow_fast_switch(const struct cpumask *cpus)
{
	struct cpc_register_resource *desired_reg, *min_reg, *max_reg;
	struct cpc_desc *cpc_ptr;
	int cpu;

	for_each_cpu(cpu, cpus) {
		cpc_ptr = per_cpu(cpc_desc_ptr, cpu);
		if (!cpc_ptr)
			return false;
		desired_reg = &cpc_ptr->cpc_regs[DESIRED_PERF];
		min_reg = &cpc_ptr->cpc_regs[MIN_PERF];
		max_reg = &cpc_ptr->cpc_regs[MAX_PERF];

		if (!cpc_is_writable(desired_reg) ||
		    (!CPC_IN_SYSTEM_MEMORY(desired_reg) &&
		     !CPC_IN_SYSTEM_IO(desired_reg)) ||
		    (CPC_SUPPORTED(min_reg) &&
		     !CPC_IN_SYSTEM_MEMORY(min_reg) &&
		     !CPC_IN_SYSTEM_IO(min_reg)) ||
		    (CPC_SUPPORTED(max_reg) &&
		     !CPC_IN_SYSTEM_MEMORY(max_reg) &&
		     !CPC_IN_SYSTEM_IO(max_reg)))
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
			continue;

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
	struct cppc_pcc_data *data;
	struct pcc_mbox_chan *pcc_chan;
	u64 usecs_lat;
	int ret = 0;

	if (pcc_ss_idx < 0 || pcc_ss_idx >= MAX_PCC_SUBSPACES)
		return -EINVAL;

	mutex_lock(&pcc_data_lock);
	data = pcc_data[pcc_ss_idx];
	if (!data) {
		ret = -ENODEV;
		goto out_unlock;
	}
	if (data->pcc_channel_acquired)
		goto out_unlock;

	pcc_chan = pcc_mbox_request_channel(&cppc_mbox_cl, pcc_ss_idx);
	if (IS_ERR(pcc_chan)) {
		ret = -ENODEV;
		goto out_unlock;
	}

	data->pcc_channel = pcc_chan;
	/*
	 * cppc_ss->latency is just a Nominal value. In reality
	 * the remote processor could be much slower to reply.
	 * So add an arbitrary amount of wait on top of Nominal.
	 */
	usecs_lat = NUM_RETRIES * pcc_chan->latency;
	data->deadline_us = usecs_lat;
	data->pcc_mrtt = pcc_chan->min_turnaround_time;
	data->pcc_mpar = pcc_chan->max_access_rate;
	data->pcc_nominal = pcc_chan->latency;
	init_rwsem(&data->pcc_lock);
	init_waitqueue_head(&data->pcc_write_wait_q);

	/* Reuse this channel when another CPU references the same subspace. */
	data->pcc_channel_acquired = true;

out_unlock:
	mutex_unlock(&pcc_data_lock);
	return ret;
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
	struct cppc_pcc_data *data;
	int ret = 0;

	if (pcc_ss_id < 0 || pcc_ss_id >= MAX_PCC_SUBSPACES)
		return -EINVAL;

	mutex_lock(&pcc_data_lock);
	data = pcc_data[pcc_ss_id];
	if (!data) {
		data = kzalloc_obj(struct cppc_pcc_data);
		if (!data) {
			ret = -ENOMEM;
			goto out_unlock;
		}
		raw_spin_lock_init(&data->payload_lock);
		pcc_data[pcc_ss_id] = data;
	}
	data->refcount++;

out_unlock:
	mutex_unlock(&pcc_data_lock);
	return ret;
}

static void pcc_data_put(int pcc_ss_id)
{
	struct cppc_pcc_data *data;

	if (pcc_ss_id < 0 || pcc_ss_id >= MAX_PCC_SUBSPACES)
		return;

	mutex_lock(&pcc_data_lock);
	data = pcc_data[pcc_ss_id];
	if (!data || --data->refcount)
		goto out_unlock;

	pcc_data[pcc_ss_id] = NULL;
	if (data->pcc_channel_acquired)
		pcc_mbox_free_channel(data->pcc_channel);

	kfree(data);

out_unlock:
	mutex_unlock(&pcc_data_lock);
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
	u32 unsupported_regs = 0;
	u32 platform_quirks;
	int pcc_subspace_id = -1;
	bool pcc_data_ref = false;
	bool cpc_present = false;
	acpi_status status;
	int ret = -EINVAL;
	int err;

	if (per_cpu(cpc_desc_ptr, pr->id))
		return 0;
	ret = cpc_get_platform_quirks(&platform_quirks);
	if (ret) {
		pr_err("CPU%d: failed to match CPPC platform quirks: %d\n",
		       pr->id, ret);
		return ret;
	}
	per_cpu(cpu_pcc_subspace_idx, pr->id) = -1;

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
	cpc_present = true;
	ret = -EINVAL;

	out_obj = (union acpi_object *) output.pointer;
	if (out_obj->package.count < 2) {
		pr_debug("Unexpected _CPC package count (%u) for CPU:%d\n",
			 out_obj->package.count, pr->id);
		goto out_buf_free;
	}

	cpc_ptr = kzalloc_obj(struct cpc_desc);
	if (!cpc_ptr) {
		ret = -ENOMEM;
		goto out_buf_free;
	}
	cpc_ptr->cpu_id = pr->id;

	/* First entry is NumEntries. */
	cpc_obj = &out_obj->package.elements[0];
	if (cpc_obj->type == ACPI_TYPE_INTEGER)	{
		if (cpc_obj->integer.value < 2 ||
		    cpc_obj->integer.value > out_obj->package.count) {
			pr_debug("Invalid _CPC NumEntries (%llu) for package count (%u) on CPU:%d\n",
				 cpc_obj->integer.value, out_obj->package.count,
				 pr->id);
			goto out_free;
		}

		num_ent = cpc_obj->integer.value;
	} else {
		pr_debug("Unexpected _CPC NumEntries entry type (%d) for CPU:%d\n",
			 cpc_obj->type, pr->id);
		goto out_free;
	}

	/* Second entry should be revision. */
	cpc_obj = &out_obj->package.elements[1];
	if (cpc_obj->type == ACPI_TYPE_INTEGER)	{
		if (cpc_obj->integer.value > U8_MAX) {
			pr_debug("Invalid _CPC Revision (%llu) for CPU:%d\n",
				 cpc_obj->integer.value, pr->id);
			ret = -EINVAL;
			goto out_free;
		}
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
	 * Disregard _CPC if the number of entries in the return package is not
	 * as expected, but support future revisions being proper supersets of
	 * the v4 and only causing more entries to be returned by _CPC.
	 */
	if ((cpc_rev == CPPC_V2_REV && num_ent != CPPC_V2_NUM_ENT) ||
	    (cpc_rev == CPPC_V3_REV && num_ent != CPPC_V3_NUM_ENT) ||
	    (cpc_rev == CPPC_V4_REV && num_ent != CPPC_V4_NUM_ENT) ||
	    (cpc_rev > CPPC_V4_REV && num_ent <= CPPC_V4_NUM_ENT)) {
		pr_debug("Unexpected number of _CPC return package entries (%d) for CPU:%d\n",
			 num_ent, pr->id);
		goto out_free;
	}
	if (cpc_rev > CPPC_V4_REV) {
		num_ent = CPPC_V4_NUM_ENT;
		cpc_rev = CPPC_V4_REV;
	}

	cpc_ptr->num_entries = num_ent;
	cpc_ptr->version = cpc_rev;

	/* Iterate through remaining entries in _CPC */
	for (i = 2; i < num_ent; i++) {
		cpc_obj = &out_obj->package.elements[i];

		if (cpc_obj->type == ACPI_TYPE_INTEGER)	{
			bool legacy_null;

			if (!cpc_integer_entry_valid(i - 2,
						     cpc_obj->integer.value,
						     &legacy_null)) {
				pr_debug("Invalid Integer _CPC register %u for CPU:%d\n",
					 i - 2, pr->id);
				ret = -EINVAL;
				goto out_free;
			}
			if (legacy_null)
				pr_warn_once(FW_BUG "_CPC register %u uses Integer 0 for an absent Buffer\n",
					     i - 2);
			cpc_ptr->cpc_regs[i - 2].type = ACPI_TYPE_INTEGER;
			cpc_ptr->cpc_regs[i - 2].cpc_entry.int_value = cpc_obj->integer.value;
		} else if (cpc_obj->type == ACPI_TYPE_BUFFER) {
			if (cpc_obj->buffer.length < sizeof(*gas_t)) {
				pr_debug("Invalid register descriptor for CPU:%d\n",
					 pr->id);
				ret = -EINVAL;
				goto out_free;
			}

			gas_t = (struct cpc_reg *)
				cpc_obj->buffer.pointer;
			if (gas_t->descriptor != CPC_GENERIC_REGISTER_DESCRIPTOR ||
			    gas_t->length != CPC_GENERIC_REGISTER_LENGTH) {
				pr_debug("Invalid register resource for CPU:%d\n",
					 pr->id);
				ret = -EINVAL;
				goto out_free;
			}

			cpc_ptr->cpc_regs[i - 2].type = ACPI_TYPE_BUFFER;
			memcpy(&cpc_ptr->cpc_regs[i - 2].cpc_entry.reg, gas_t,
			       sizeof(*gas_t));
			gas_t = &cpc_ptr->cpc_regs[i - 2].cpc_entry.reg;
			cpc_apply_platform_quirks(gas_t, i - 2,
						  platform_quirks);

			/*
			 * The PCC Subspace index is encoded inside
			 * the CPC table entries. The same PCC index
			 * will be used for all the PCC entries,
			 * so extract it only once.
			 */
			if (gas_t->space_id == ACPI_ADR_SPACE_PLATFORM_COMM) {
				/* These registers have no specified 32-bit upper bound. */
				bool wide_write = i - 2 == PERF_LIMITED ||
						  i - 2 == ENABLE ||
						  i - 2 == AUTO_SEL_ENABLE;
				bool write_width_supported = gas_t->bit_width == 8 ||
						     gas_t->bit_width == 16 ||
						     gas_t->bit_width == 32 ||
						     gas_t->bit_width == 64;
				bool unsupported;

				unsupported = !gas_t->bit_width ||
					      gas_t->bit_width > 64 ||
					      gas_t->bit_offset ||
					      gas_t->bit_width % 8 ||
					      (cpc_reg_is_writable(i - 2) &&
					       (!write_width_supported ||
						(!wide_write && gas_t->bit_width > 32)));
				if (unsupported) {
					if (!cpc_retain_pcc_status(cpc_ptr, i - 2))
						unsupported_regs |= BIT(i - 2);
					continue;
				}

				if (pcc_subspace_id < 0) {
					pcc_subspace_id = gas_t->access_width;
				} else if (pcc_subspace_id != gas_t->access_width) {
					pr_debug("Mismatched PCC ids in _CPC for CPU:%d\n",
						 pr->id);
					ret = -EINVAL;
					goto out_free;
				}

				if (!pcc_data_ref) {
					err = pcc_data_alloc(pcc_subspace_id);
					if (err) {
						ret = err;
						goto out_free;
					}
					pcc_data_ref = true;
				}
			} else if (gas_t->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY) {
				if (!IS_NULL_REG(gas_t)) {
					void __iomem *addr;
					size_t access_width;

					err = cpc_validate_sysmem_reg(cpc_ptr, gas_t,
								      i - 2);
					if (err) {
						unsupported_regs |= BIT(i - 2);
						continue;
					}
					if (!cpc_is_readable(&cpc_ptr->cpc_regs[i - 2]) &&
					    !cpc_is_writable(&cpc_ptr->cpc_regs[i - 2]))
						continue;

					if (!osc_cpc_flexible_adr_space_confirmed) {
						pr_debug("Flexible address space capability not supported\n");
						ret = -EOPNOTSUPP;
						if (!cpc_supported_by_cpu())
							goto out_free;
						ret = -EINVAL;
					}

					access_width = cpc_reg_access_width(gas_t);
					access_width /= 8;
					addr = ioremap(gas_t->address, access_width);
					if (!addr) {
						ret = -ENOMEM;
						goto out_free;
					}
					cpc_ptr->cpc_regs[i - 2].sys_mem_vaddr = addr;
				}
			} else if (gas_t->space_id == ACPI_ADR_SPACE_SYSTEM_IO) {
				u64 access_size;
				const char *reason = "uses unsupported SystemIO geometry";
				unsigned int access_width;
				bool partial = false;
				bool unsupported;

				access_width = cpc_reg_access_width(gas_t);
				unsupported = !IS_ENABLED(CONFIG_HAS_IOPORT);
				if (unsupported)
					reason = "requires unavailable SystemIO support";
				else
					unsupported = access_width != 8 &&
					      access_width != 16 &&
					      access_width != 32;
				if (!unsupported) {
					access_size = access_width / 8;
					unsupported = !gas_t->bit_width ||
						gas_t->bit_width > access_width ||
						gas_t->bit_offset >= access_width ||
						gas_t->bit_width > access_width -
									   gas_t->bit_offset;
					partial = gas_t->bit_offset ||
						  gas_t->bit_width != access_width;
				}
				if (!unsupported) {
					unsupported = (cpc_reg_is_writable(i - 2) &&
						i - 2 != PERF_LIMITED &&
						cpc_is_writable(&cpc_ptr->cpc_regs[i - 2]) &&
						partial) ||
						!cpc_reg_access_aligned(gas_t,
									access_size) ||
						gas_t->address >
						U16_MAX - (access_size - 1);
				}
				if (unsupported) {
					if (cpc_retain_sysio_status(cpc_ptr, i - 2))
						continue;
					pr_debug("CPU%d: _CPC register %u %s\n",
						 pr->id, i - 2, reason);
					unsupported_regs |= BIT(i - 2);
					continue;
				}
				if (i - 2 == PERF_LIMITED && partial) {
					pr_warn_once("CPU%d: Performance Limited register cannot be cleared safely; keeping it readable\n",
						     cpc_ptr->cpu_id);
					cpc_ptr->cpc_regs[i - 2].cpc_entry.write_unsupported = true;
				}
				if (!osc_cpc_flexible_adr_space_confirmed) {
					pr_debug("Flexible address space capability not supported\n");
					ret = -EOPNOTSUPP;
					if (!cpc_supported_by_cpu())
						goto out_free;
					ret = -EINVAL;
				}
			} else {
				if (gas_t->space_id != ACPI_ADR_SPACE_FIXED_HARDWARE || !cpc_ffh_supported()) {
					/* Support only PCC, SystemMemory, SystemIO, and FFH type regs. */
					pr_debug("Unsupported register type (%d) in _CPC\n",
						 gas_t->space_id);
					ret = -EOPNOTSUPP;
					goto out_free;
				}
			}
		} else if (cpc_obj->type == ACPI_TYPE_PACKAGE && (i - 2) == RESOURCE_PRIORITY) {
			/*
			 * ACPI 6.6, s8.4.6.1.2.7 defines Resource Priority as a
			 * Package of Resource Priority Register Descriptor sub-packages.
			 * Parsing the full structure is not yet supported.
			 * Mark the register as unsupported for now.
			 */
			pr_debug("CPU:%d Resource Priority not supported\n", pr->id);
			cpc_ptr->cpc_regs[i-2].type = ACPI_TYPE_INTEGER;
			cpc_ptr->cpc_regs[i-2].cpc_entry.int_value = 0;
		} else {
			pr_debug("Invalid entry type (%d) in _CPC for CPU:%d\n",
				 i, pr->id);
			goto out_free;
		}
	}

	per_cpu(cpu_pcc_subspace_idx, pr->id) = pcc_data_ref ?
						 pcc_subspace_id : -1;

	ret = cpc_resolve_unsupported(cpc_ptr, unsupported_regs);
	if (ret)
		goto out_free;
	unsupported_regs = 0;

	ret = cpc_validate_required_controls(cpc_ptr);
	if (ret)
		goto out_free;

	/*
	 * Initialize the remaining cpc_regs as unsupported.
	 * Example: In case FW exposes CPPC v2, the below loop will initialize
	 * LOWEST_FREQ and NOMINAL_FREQ regs as unsupported
	 */
	for (i = num_ent - 2; i < MAX_CPC_REG_ENT; i++) {
		cpc_ptr->cpc_regs[i].type = ACPI_TYPE_INTEGER;
		cpc_ptr->cpc_regs[i].cpc_entry.int_value = 0;
	}


	cpc_mark_rmw_lock_users(cpc_ptr);
	raw_spin_lock_init(&cpc_ptr->rmw_lock);

	/* Parse PSD data for this CPU */
	ret = acpi_get_psd(cpc_ptr, handle);
	if (ret)
		goto out_free;

	ret = cpc_register_sysmem_desc(cpc_ptr);
	if (ret)
		goto out_free;

	/* Register PCC channel once for all PCC subspace ID. */
	if (pcc_data_ref) {
		ret = register_pcc_channel(pcc_subspace_id);
		if (ret) {
			pr_err("Failed to find PCC channel for subspace %d\n",
			       pcc_subspace_id);
			goto out_free;
		}

		cpc_validate_pcc_bounds(cpc_ptr, pcc_subspace_id,
					pcc_data[pcc_subspace_id],
					&unsupported_regs);

		ret = cpc_resolve_unsupported(cpc_ptr, unsupported_regs);
		if (ret)
			goto out_free;

		/* A range-only status entry needs the channel only for bounds. */
		if (!cpc_pcc_access_needed(cpc_ptr)) {
			pcc_data_put(pcc_subspace_id);
			pcc_data_ref = false;
			per_cpu(cpu_pcc_subspace_idx, pr->id) = -1;
		}
	}

	ret = cpc_validate_bound_controls(cpc_ptr);
	if (ret)
		goto out_free;

	ret = cpc_register_non_mmio_desc(cpc_ptr);
	if (ret)
		goto out_free;

	/* Everything looks okay */
	pr_debug("Parsed CPC struct for CPU: %d\n", pr->id);

	/* Add per logical CPU nodes for reading its feedback counters. */
	cpu_dev = get_cpu_device(pr->id);
	if (!cpu_dev) {
		ret = -EINVAL;
		goto out_free;
	}

	/* Plug PSD data into this CPU's CPC descriptor. */
	cpc_set_desc(pr->id, cpc_ptr);

	ret = kobject_init_and_add(&cpc_ptr->kobj, &cppc_ktype, &cpu_dev->kobj,
			"acpi_cppc");
	if (ret) {
		cpc_set_desc(pr->id, NULL);
		cpc_unregister_non_mmio_desc(cpc_ptr);
		cpc_unregister_sysmem_desc(cpc_ptr);
		kobject_put(&cpc_ptr->kobj);
		goto out_pcc_put;
	}

	kfree(output.pointer);
	return 0;

out_free:
	cppc_free_desc(cpc_ptr);

out_pcc_put:
	if (pcc_data_ref)
		pcc_data_put(pcc_subspace_id);
	per_cpu(cpu_pcc_subspace_idx, pr->id) = -1;

out_buf_free:
	if (cpc_present)
		pr_err("CPU%d: failed to initialize _CPC: %d\n", pr->id, ret);
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
	int pcc_ss_id;

	cpc_ptr = per_cpu(cpc_desc_ptr, pr->id);
	if (!cpc_ptr) {
		per_cpu(cpu_pcc_subspace_idx, pr->id) = -1;
		return;
	}

	pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, pr->id);
	cpc_set_desc(pr->id, NULL);
	kobject_del(&cpc_ptr->kobj);
	cpc_unregister_non_mmio_desc(cpc_ptr);
	cpc_unregister_sysmem_desc(cpc_ptr);

	pcc_data_put(pcc_ss_id);
	per_cpu(cpu_pcc_subspace_idx, pr->id) = -1;

	kobject_put(&cpc_ptr->kobj);
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
 * cpc_read_ffh_fb_ctrs() - Read FFH feedback counters together
 * @cpunum:	Target CPU
 * @reg1:	first CPPC register information
 * @val1:	place holder for first return value
 * @reg2:	second CPPC register information
 * @val2:	place holder for second return value
 *
 * Return: 0 on success, error code otherwise
 */
int __weak cpc_read_ffh_fb_ctrs(int cpunum, struct cpc_reg *reg1,
				u64 *val1, struct cpc_reg *reg2, u64 *val2)
{
	return -EOPNOTSUPP;
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
	unsigned long flags;
	u8 buf[sizeof(*val)];
	unsigned int i;
	int size;
	int pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, cpu);
	struct cpc_reg *reg = &reg_res->cpc_entry.reg;

	if (!cpc_is_readable(reg_res))
		return -EOPNOTSUPP;

	if (reg_res->type == ACPI_TYPE_INTEGER) {
		*val = reg_res->cpc_entry.int_value;
		return 0;
	}

	*val = 0;
	if (reg->space_id == ACPI_ADR_SPACE_FIXED_HARDWARE)
		return cpc_read_ffh(cpu, reg, val);

	size = GET_BIT_WIDTH(reg);

	if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_IO) {
		u32 val_u32;
		acpi_status status;

		if (!IS_ENABLED(CONFIG_HAS_IOPORT))
			return -EOPNOTSUPP;

		status = acpi_os_read_port((acpi_io_address)reg->address,
					   &val_u32, size);
		if (ACPI_FAILURE(status)) {
			pr_debug("Error: Failed to read SystemIO port %llx\n",
				 reg->address);
			return -EFAULT;
		}

		*val = MASK_VAL_READ(reg, val_u32);
		return 0;
	} else if (reg->space_id == ACPI_ADR_SPACE_PLATFORM_COMM) {
		if (pcc_ss_id < 0 || !pcc_data[pcc_ss_id])
			return -ENODEV;

		/*
		 * For registers in PCC space, the register size is determined
		 * by the bit width field; the access size is used to indicate
		 * the PCC subspace id.
		 */
		vaddr = GET_PCC_VADDR(reg->address, pcc_ss_id);
		size = reg->bit_width / 8;
		if (!size || size > sizeof(buf) || reg->bit_width % 8)
			return -EFAULT;

		raw_spin_lock_irqsave(&pcc_data[pcc_ss_id]->payload_lock, flags);
		memcpy_fromio(buf, vaddr, size);
		raw_spin_unlock_irqrestore(&pcc_data[pcc_ss_id]->payload_lock,
					   flags);

		*val = 0;
		for (i = 0; i < size; i++)
			*val |= (u64)buf[i] << (i * 8);
		return 0;
	} else if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY)
		vaddr = reg_res->sys_mem_vaddr;
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
		pr_debug("Error: Cannot read %u bit width from system memory: 0x%llx\n",
			 size, reg->address);
		return -EFAULT;
	}

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
	struct cpc_reg *reg;
	struct cpc_desc *cpc_desc;
	unsigned long flags;
	u8 buf[sizeof(val)];
	unsigned int i;
	bool locked = false;

	if (!cpc_is_writable(reg_res))
		return -EOPNOTSUPP;

	reg = &reg_res->cpc_entry.reg;
	if (reg->space_id == ACPI_ADR_SPACE_FIXED_HARDWARE)
		return cpc_write_ffh(cpu, reg, val);

	size = GET_BIT_WIDTH(reg);

	if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_IO) {
		acpi_status status;

		if (!IS_ENABLED(CONFIG_HAS_IOPORT))
			return -EOPNOTSUPP;

		status = acpi_os_write_port((acpi_io_address)reg->address,
					    (u32)val, size);
		if (ACPI_FAILURE(status)) {
			pr_debug("Error: Failed to write SystemIO port %llx\n",
				 reg->address);
			return -EFAULT;
		}

		return 0;
	} else if (reg->space_id == ACPI_ADR_SPACE_PLATFORM_COMM) {
		if (pcc_ss_id < 0 || !pcc_data[pcc_ss_id])
			return -ENODEV;

		/*
		 * For registers in PCC space, the register size is determined
		 * by the bit width field; the access size is used to indicate
		 * the PCC subspace id.
		 */
		vaddr = GET_PCC_VADDR(reg->address, pcc_ss_id);
		size = reg->bit_width / 8;
		if (!size || size > sizeof(buf) || reg->bit_width % 8)
			return -EFAULT;

		for (i = 0; i < size; i++)
			buf[i] = val >> (i * 8);

		raw_spin_lock_irqsave(&pcc_data[pcc_ss_id]->payload_lock, flags);
		memcpy_toio(vaddr, buf, size);
		/* Publish every payload byte before another CPU can ring the doorbell. */
		wmb();
		raw_spin_unlock_irqrestore(&pcc_data[pcc_ss_id]->payload_lock,
					   flags);
		return 0;
	} else if (reg->space_id == ACPI_ADR_SPACE_SYSTEM_MEMORY)
		vaddr = reg_res->sys_mem_vaddr;
	else
		return acpi_os_write_memory((acpi_physical_address)reg->address,
				val, size);

	/* Partial fields and local overlaps use the descriptor lock. */
	locked = reg_res->cpc_entry.use_rmw_lock;
	if (locked) {
		cpc_desc = per_cpu(cpc_desc_ptr, cpu);
		if (!cpc_desc) {
			pr_debug("No CPC descriptor for CPU:%d\n", cpu);
			return -ENODEV;
		}
		raw_spin_lock_irqsave(&cpc_desc->rmw_lock, flags);
	}

	if (reg->bit_offset || reg->bit_width != size) {
		/*
		 * MASK_VAL_WRITE() discards the field's old bits, so undefined
		 * readback from a write-only field is not propagated.
		 */
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
			if (locked)
				raw_spin_unlock_irqrestore(&cpc_desc->rmw_lock,
							   flags);
			return -EFAULT;
		}
		val = MASK_VAL_WRITE(reg, prev_val, val);
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
		pr_debug("Error: Cannot write %u bit width to system memory: 0x%llx\n",
			 size, reg->address);
		ret_val = -EFAULT;
		break;
	}

	if (locked) {
		if (!ret_val)
			mmiowb_set_pending();
		raw_spin_unlock_irqrestore(&cpc_desc->rmw_lock, flags);
	}

	return ret_val;
}

static int cppc_get_reg_val_in_pcc(int cpu, struct cpc_register_resource *reg, u64 *val)
{
	int pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, cpu);
	struct cppc_pcc_data *pcc_ss_data = NULL;
	int ret;

	if (pcc_ss_id < 0) {
		pr_debug("Invalid pcc_ss_id\n");
		return -ENODEV;
	}

	pcc_ss_data = pcc_data[pcc_ss_id];

	down_write(&pcc_ss_data->pcc_lock);

	if (send_pcc_cmd(pcc_ss_id, CMD_READ) >= 0)
		ret = cpc_read(cpu, reg, val);
	else
		ret = -EIO;

	up_write(&pcc_ss_data->pcc_lock);

	return ret;
}

static int cppc_get_reg_val(int cpu, enum cppc_regs reg_idx, u64 *val)
{
	struct cpc_desc *cpc_desc = per_cpu(cpc_desc_ptr, cpu);
	struct cpc_register_resource *reg;

	if (val == NULL)
		return -EINVAL;

	if (!cpc_desc) {
		pr_debug("No CPC descriptor for CPU:%d\n", cpu);
		return -ENODEV;
	}
	if (cpc_reg_is_write_only(cpc_desc, reg_idx))
		return -EOPNOTSUPP;

	reg = &cpc_desc->cpc_regs[reg_idx];

	/*
	 * Desired and Performance Limited may be disabled despite not being
	 * generally optional.
	 */
	if ((reg->type == ACPI_TYPE_INTEGER &&
	     (IS_OPTIONAL_CPC_REG(reg_idx) || reg_idx == DESIRED_PERF ||
	      reg_idx == PERF_LIMITED) &&
	     !reg->cpc_entry.int_value) || (reg->type != ACPI_TYPE_INTEGER &&
	     IS_NULL_REG(&reg->cpc_entry.reg))) {
		pr_debug("CPC register is not supported\n");
		return -EOPNOTSUPP;
	}
	if (!cpc_is_readable(reg))
		return -EOPNOTSUPP;

	if (CPC_IN_PCC(reg))
		return cppc_get_reg_val_in_pcc(cpu, reg, val);

	return cpc_read(cpu, reg, val);
}

static int cppc_set_reg_val_in_pcc(int cpu, struct cpc_register_resource *reg, u64 val)
{
	int pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, cpu);
	struct cppc_pcc_data *pcc_ss_data;
	int ret;

	if (pcc_ss_id < 0) {
		pr_debug("Invalid pcc_ss_id\n");
		return -ENODEV;
	}
	pcc_ss_data = pcc_data[pcc_ss_id];
	if (!pcc_ss_data)
		return -ENODEV;

	down_write(&pcc_ss_data->pcc_lock);

	ret = check_pcc_chan(pcc_ss_id, false);
	if (ret)
		goto out;

	ret = cpc_write(cpu, reg, val);
	if (ret)
		goto out;

	/* after writing CPC, transfer the ownership of PCC to platform */
	ret = send_pcc_cmd(pcc_ss_id, CMD_WRITE);

out:
	if (ret)
		cppc_abort_pending_pcc_write(pcc_ss_id, pcc_ss_data, ret);
	up_write(&pcc_ss_data->pcc_lock);

	return ret;
}

static int cppc_set_reg_val(int cpu, enum cppc_regs reg_idx, u64 val)
{
	struct cpc_desc *cpc_desc = per_cpu(cpc_desc_ptr, cpu);
	struct cpc_register_resource *reg;

	if (!cpc_desc) {
		pr_debug("No CPC descriptor for CPU:%d\n", cpu);
		return -ENODEV;
	}

	reg = &cpc_desc->cpc_regs[reg_idx];

	/* Integer 1 describes autonomous selection that is always enabled. */
	if (reg_idx == AUTO_SEL_ENABLE && reg->type == ACPI_TYPE_INTEGER &&
	    reg->cpc_entry.int_value == 1)
		return val == 1 ? 0 : -EOPNOTSUPP;

	/* if a register is writeable, it must be a buffer and not null */
	if (!cpc_is_writable(reg)) {
		pr_debug("CPC register is not supported\n");
		return -EOPNOTSUPP;
	}

	if (CPC_IN_PCC(reg))
		return cppc_set_reg_val_in_pcc(cpu, reg, val);

	return cpc_write(cpu, reg, val);
}

/**
 * cppc_get_desired_perf - Get the desired performance register value.
 * @cpunum: CPU from which to get desired performance.
 * @desired_perf: Return address.
 *
 * Return: 0 for success, -EOPNOTSUPP for _CPC revision 4 or later, and a
 * negative errno otherwise.
 */
int cppc_get_desired_perf(int cpunum, u64 *desired_perf)
{
	return cppc_get_reg_val(cpunum, DESIRED_PERF, desired_perf);
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
	return cppc_get_reg_val(cpunum, NOMINAL_PERF, nominal_perf);
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
	return cppc_get_reg_val(cpunum, HIGHEST_PERF, highest_perf);
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
	return cppc_get_reg_val(cpunum, ENERGY_PERF, epp_perf);
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
		*lowest_non_linear_reg, *nominal_reg, *reference_reg,
		*guaranteed_reg, *low_freq_reg = NULL, *nom_freq_reg = NULL;
	u64 high, low, guaranteed = 0, nom, ref, min_nonlinear,
	    low_f = 0, nom_f = 0;
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
	reference_reg = &cpc_desc->cpc_regs[REFERENCE_PERF];
	low_freq_reg = &cpc_desc->cpc_regs[LOWEST_FREQ];
	nom_freq_reg = &cpc_desc->cpc_regs[NOMINAL_FREQ];
	guaranteed_reg = &cpc_desc->cpc_regs[GUARANTEED_PERF];

	/* Are any of the regs PCC ?*/
	if (CPC_IN_PCC(highest_reg) || CPC_IN_PCC(lowest_reg) ||
		CPC_IN_PCC(lowest_non_linear_reg) || CPC_IN_PCC(nominal_reg) ||
		(CPC_SUPPORTED(reference_reg) && CPC_IN_PCC(reference_reg)) ||
		CPC_IN_PCC(low_freq_reg) || CPC_IN_PCC(nom_freq_reg) ||
		CPC_IN_PCC(guaranteed_reg)) {
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

	ret = cpc_read(cpunum, highest_reg, &high);
	if (ret)
		goto out_err;
	perf_caps->highest_perf = high;

	ret = cpc_read(cpunum, lowest_reg, &low);
	if (ret)
		goto out_err;
	perf_caps->lowest_perf = low;

	ret = cpc_read(cpunum, nominal_reg, &nom);
	if (ret)
		goto out_err;
	perf_caps->nominal_perf = nom;

	/*
	 * If reference perf register is not supported then we should
	 * use the nominal perf value
	 */
	if (CPC_SUPPORTED(reference_reg)) {
		ret = cpc_read(cpunum, reference_reg, &ref);
		if (ret)
			goto out_err;
	} else {
		ref = nom;
	}
	perf_caps->reference_perf = ref;

	if (guaranteed_reg->type != ACPI_TYPE_BUFFER  ||
	    IS_NULL_REG(&guaranteed_reg->cpc_entry.reg)) {
		perf_caps->guaranteed_perf = 0;
	} else {
		ret = cpc_read(cpunum, guaranteed_reg, &guaranteed);
		if (ret)
			goto out_err;
		perf_caps->guaranteed_perf = guaranteed;
	}

	ret = cpc_read(cpunum, lowest_non_linear_reg, &min_nonlinear);
	if (ret)
		goto out_err;
	perf_caps->lowest_nonlinear_perf = min_nonlinear;

	if (!high || !low || !nom || !ref || !min_nonlinear ||
	    high > U32_MAX || low > U32_MAX || guaranteed > U32_MAX ||
	    nom > U32_MAX || ref > U32_MAX || min_nonlinear > U32_MAX ||
	    high < nom || nom < min_nonlinear || min_nonlinear < low ||
	    (CPC_SUPPORTED(guaranteed_reg) &&
	     (guaranteed < low || guaranteed > nom))) {
		ret = -EFAULT;
		goto out_err;
	}

	/* Read optional lowest and nominal frequencies if present */
	if (CPC_SUPPORTED(low_freq_reg)) {
		ret = cpc_read(cpunum, low_freq_reg, &low_f);
		if (ret)
			goto out_err;
	}

	if (CPC_SUPPORTED(nom_freq_reg)) {
		ret = cpc_read(cpunum, nom_freq_reg, &nom_f);
		if (ret)
			goto out_err;
	}
	/* Require ordered anchors and a nonzero slope when frequencies differ. */
	if (low_f > U32_MAX || nom_f > U32_MAX ||
	    (low_f && nom_f &&
	     (nom_f < low_f || nom < low ||
	      (nom_f != low_f && nom == low)))) {
		ret = -EFAULT;
		goto out_err;
	}

	perf_caps->lowest_freq = low_f;
	perf_caps->nominal_freq = nom_f;


out_err:
	if (regs_in_pcc)
		up_write(&pcc_ss_data->pcc_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(cppc_get_perf_caps);

/**
 * cppc_perf_ctrs_in_pcc_cpu - Check if any perf counters of a CPU are in PCC.
 * @cpu: CPU on which to check perf counters.
 *
 * Return: true if any of the counters are in PCC regions, false otherwise
 */
bool cppc_perf_ctrs_in_pcc_cpu(unsigned int cpu)
{
	struct cpc_desc *cpc_desc = per_cpu(cpc_desc_ptr, cpu);

	if (!cpc_desc)
		return false;

	return CPC_IN_PCC(&cpc_desc->cpc_regs[DELIVERED_CTR]) ||
		CPC_IN_PCC(&cpc_desc->cpc_regs[REFERENCE_CTR]) ||
		CPC_IN_PCC(&cpc_desc->cpc_regs[CTR_WRAP_TIME]);
}
EXPORT_SYMBOL_GPL(cppc_perf_ctrs_in_pcc_cpu);

static int cppc_read_fb_ctrs(int cpunum,
			     struct cpc_register_resource *delivered_reg,
			     struct cpc_register_resource *reference_reg,
			     u64 *delivered, u64 *reference)
{
	int ret;

	/*
	 * For FFH feedback counters, try a paired read first to reduce
	 * sampling skew between delivered and reference counters. Fall
	 * back to the existing per-register reads if unsupported.
	 */
	if (CPC_IN_FFH(delivered_reg) && CPC_IN_FFH(reference_reg)) {
		ret = cpc_read_ffh_fb_ctrs(cpunum,
					&delivered_reg->cpc_entry.reg, delivered,
					&reference_reg->cpc_entry.reg, reference);
		if (ret != -EOPNOTSUPP)
			return ret;
	}

	ret = cpc_read(cpunum, delivered_reg, delivered);
	if (ret)
		return ret;

	return cpc_read(cpunum, reference_reg, reference);
}

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

	for_each_online_cpu(cpu) {
		if (cppc_perf_ctrs_in_pcc_cpu(cpu))
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
		*ctr_wrap_reg;
	int pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, cpunum);
	struct cppc_pcc_data *pcc_ss_data = NULL;
	u64 delivered, reference, ctr_wrap_time;
	int ret = 0, regs_in_pcc = 0;

	if (!cpc_desc) {
		pr_debug("No CPC descriptor for CPU:%d\n", cpunum);
		return -ENODEV;
	}

	delivered_reg = &cpc_desc->cpc_regs[DELIVERED_CTR];
	reference_reg = &cpc_desc->cpc_regs[REFERENCE_CTR];
	ctr_wrap_reg = &cpc_desc->cpc_regs[CTR_WRAP_TIME];

	/* Are any of the regs PCC ?*/
	if (CPC_IN_PCC(delivered_reg) || CPC_IN_PCC(reference_reg) ||
		CPC_IN_PCC(ctr_wrap_reg)) {
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

	ret = cppc_read_fb_ctrs(cpunum, delivered_reg, reference_reg,
				&delivered, &reference);
	if (ret)
		goto out_err;

	/*
	 * Per spec, if ctr_wrap_time optional register is unsupported, then the
	 * performance counters are assumed to never wrap during the lifetime of
	 * platform
	 */
	ctr_wrap_time = (u64)(~((u64)0));
	if (CPC_SUPPORTED(ctr_wrap_reg)) {
		ret = cpc_read(cpunum, ctr_wrap_reg, &ctr_wrap_time);
		if (ret)
			goto out_err;
	}

	if (!delivered || !reference) {
		ret = -EFAULT;
		goto out_err;
	}

	perf_fb_ctrs->delivered = delivered;
	perf_fb_ctrs->reference = reference;
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
	bool auto_sel_pcc;
	bool auto_sel_non_pcc;
	bool epp_pcc;
	bool epp_non_pcc;
	int ret;

	if (!cpc_desc) {
		pr_debug("No CPC descriptor for CPU:%d\n", cpu);
		return -ENODEV;
	}

	auto_sel_reg = &cpc_desc->cpc_regs[AUTO_SEL_ENABLE];
	epp_set_reg = &cpc_desc->cpc_regs[ENERGY_PERF];
	if (!enable && auto_sel_reg->type == ACPI_TYPE_INTEGER &&
	    auto_sel_reg->cpc_entry.int_value == 1)
		return -EOPNOTSUPP;

	auto_sel_pcc = cpc_is_writable(auto_sel_reg) &&
		CPC_IN_PCC(auto_sel_reg);
	epp_pcc = cpc_is_writable(epp_set_reg) && CPC_IN_PCC(epp_set_reg);

	auto_sel_non_pcc = cpc_is_writable(auto_sel_reg) && !auto_sel_pcc;
	epp_non_pcc = cpc_is_writable(epp_set_reg) && !epp_pcc;

	/* Complete fallible non-PCC writes before staging PCC data. */
	if (auto_sel_non_pcc) {
		ret = cpc_write(cpu, auto_sel_reg, enable);
		if (ret)
			return ret;
	}
	if (epp_non_pcc) {
		ret = cpc_write(cpu, epp_set_reg, perf_ctrls->energy_perf);
		if (ret)
			return ret;
	}

	if (epp_pcc || auto_sel_pcc) {
		if (pcc_ss_id < 0) {
			pr_debug("Invalid pcc_ss_id for CPU:%d\n", cpu);
			return -ENODEV;
		}

		pcc_ss_data = pcc_data[pcc_ss_id];
		if (!pcc_ss_data)
			return -ENODEV;

		down_write(&pcc_ss_data->pcc_lock);

		ret = check_pcc_chan(pcc_ss_id, false);
		if (ret)
			goto out_unlock;

		if (auto_sel_pcc) {
			ret = cpc_write(cpu, auto_sel_reg, enable);
			if (ret)
				goto out_unlock;
		}

		if (epp_pcc) {
			ret = cpc_write(cpu, epp_set_reg, perf_ctrls->energy_perf);
			if (ret)
				goto out_unlock;
		}

		/* after writing CPC, transfer the ownership of PCC to platform */
		ret = send_pcc_cmd(pcc_ss_id, CMD_WRITE);

out_unlock:
		if (ret)
			cppc_abort_pending_pcc_write(pcc_ss_id, pcc_ss_data, ret);
		up_write(&pcc_ss_data->pcc_lock);
	} else if (epp_non_pcc || auto_sel_non_pcc) {
		ret = 0;
	} else {
		ret = -EOPNOTSUPP;
		pr_debug("No writable EPP controls for CPU:%d\n", cpu);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(cppc_set_epp_perf);

/**
 * cppc_set_epp() - Write the EPP register.
 * @cpu: CPU on which to write register.
 * @epp_val: Value to write to the EPP register.
 */
int cppc_set_epp(int cpu, u64 epp_val)
{
	if (epp_val > CPPC_EPP_ENERGY_EFFICIENCY_PREF)
		return -EINVAL;

	return cppc_set_reg_val(cpu, ENERGY_PERF, epp_val);
}
EXPORT_SYMBOL_GPL(cppc_set_epp);

/**
 * cppc_get_auto_act_window() - Read autonomous activity window register.
 * @cpu: CPU from which to read register.
 * @auto_act_window: Return address.
 *
 * According to ACPI 6.5, s8.4.6.1.6, the value read from the autonomous
 * activity window register consists of two parts: a 7 bits value indicate
 * significand and a 3 bits value indicate exponent.
 */
int cppc_get_auto_act_window(int cpu, u64 *auto_act_window)
{
	unsigned int exp;
	u64 val, sig;
	int ret;

	if (auto_act_window == NULL)
		return -EINVAL;

	ret = cppc_get_reg_val(cpu, AUTO_ACT_WINDOW, &val);
	if (ret)
		return ret;

	sig = val & CPPC_AUTO_ACT_WINDOW_MAX_SIG;
	exp = (val >> CPPC_AUTO_ACT_WINDOW_SIG_BIT_SIZE) & CPPC_AUTO_ACT_WINDOW_MAX_EXP;
	*auto_act_window = sig * int_pow(10, exp);

	return 0;
}
EXPORT_SYMBOL_GPL(cppc_get_auto_act_window);

/**
 * cppc_set_auto_act_window() - Write autonomous activity window register.
 * @cpu: CPU on which to write register.
 * @auto_act_window: usec value to write to the autonomous activity window register.
 *
 * According to ACPI 6.5, s8.4.6.1.6, the value to write to the autonomous
 * activity window register consists of two parts: a 7 bits value indicate
 * significand and a 3 bits value indicate exponent.
 */
int cppc_set_auto_act_window(int cpu, u64 auto_act_window)
{
	/* The max value to store is 1270000000 */
	u64 max_val = CPPC_AUTO_ACT_WINDOW_MAX_SIG * int_pow(10, CPPC_AUTO_ACT_WINDOW_MAX_EXP);
	int exp = 0;
	u64 val;

	if (auto_act_window > max_val)
		return -EINVAL;

	/*
	 * The max significand is 127, when auto_act_window is larger than
	 * 129, discard the precision of the last digit and increase the
	 * exponent by 1.
	 */
	while (auto_act_window > CPPC_AUTO_ACT_WINDOW_SIG_CARRY_THRESH) {
		auto_act_window /= 10;
		exp += 1;
	}

	/* For 128 and 129, cut it to 127. */
	if (auto_act_window > CPPC_AUTO_ACT_WINDOW_MAX_SIG)
		auto_act_window = CPPC_AUTO_ACT_WINDOW_MAX_SIG;

	val = (exp << CPPC_AUTO_ACT_WINDOW_SIG_BIT_SIZE) + auto_act_window;

	return cppc_set_reg_val(cpu, AUTO_ACT_WINDOW, val);
}
EXPORT_SYMBOL_GPL(cppc_set_auto_act_window);

/**
 * cppc_get_auto_sel() - Read autonomous selection register.
 * @cpu: CPU from which to read register.
 * @enable: Return address.
 */
int cppc_get_auto_sel(int cpu, bool *enable)
{
	u64 auto_sel;
	int ret;

	if (enable == NULL)
		return -EINVAL;

	ret = cppc_get_reg_val(cpu, AUTO_SEL_ENABLE, &auto_sel);
	if (ret)
		return ret;

	*enable = (bool)auto_sel;

	return 0;
}
EXPORT_SYMBOL_GPL(cppc_get_auto_sel);

/**
 * cppc_auto_sel_is_immutable - Check for always-enabled autonomous selection.
 * @cpu: CPU whose _CPC descriptor to check.
 *
 * Context: Process context.
 * Return: true for Integer 1, false for a register or an absent descriptor.
 */
bool cppc_auto_sel_is_immutable(int cpu)
{
	struct cpc_desc *cpc_desc;
	struct cpc_register_resource *reg;

	guard(mutex)(&cpc_desc_lock);
	cpc_desc = per_cpu(cpc_desc_ptr, cpu);
	if (!cpc_desc)
		return false;

	reg = &cpc_desc->cpc_regs[AUTO_SEL_ENABLE];
	return reg->type == ACPI_TYPE_INTEGER && reg->cpc_entry.int_value == 1;
}
EXPORT_SYMBOL_GPL(cppc_auto_sel_is_immutable);

/**
 * cppc_set_auto_sel - Write autonomous selection register.
 * @cpu    : CPU to which to write register.
 * @enable : the desired value of autonomous selection resiter to be updated.
 */
int cppc_set_auto_sel(int cpu, bool enable)
{
	return cppc_set_reg_val(cpu, AUTO_SEL_ENABLE, enable);
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
	return cppc_set_reg_val(cpu, ENABLE, enable);
}
EXPORT_SYMBOL_GPL(cppc_set_enable);

/**
 * cppc_get_perf - Get a CPU's performance controls.
 * @cpu: CPU for which to get performance controls.
 * @perf_ctrls: ptr to cppc_perf_ctrls. See cppc_acpi.h
 *
 * Desired Performance is not read and is returned as 0.
 *
 * Return: 0 for success with perf_ctrls, -ERRNO otherwise.
 */
int cppc_get_perf(int cpu, struct cppc_perf_ctrls *perf_ctrls)
{
	struct cpc_desc *cpc_desc = per_cpu(cpc_desc_ptr, cpu);
	struct cpc_register_resource *min_perf_reg, *max_perf_reg,
				     *energy_perf_reg, *auto_sel_reg;
	u64 min = 0, max = 0, energy_perf = 0, auto_sel = 0;
	int pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, cpu);
	struct cppc_pcc_data *pcc_ss_data = NULL;
	int ret = 0, regs_in_pcc = 0;

	if (!cpc_desc) {
		pr_debug("No CPC descriptor for CPU:%d\n", cpu);
		return -ENODEV;
	}

	if (!perf_ctrls) {
		pr_debug("Invalid perf_ctrls pointer\n");
		return -EINVAL;
	}

	min_perf_reg = &cpc_desc->cpc_regs[MIN_PERF];
	max_perf_reg = &cpc_desc->cpc_regs[MAX_PERF];
	energy_perf_reg = &cpc_desc->cpc_regs[ENERGY_PERF];
	auto_sel_reg = &cpc_desc->cpc_regs[AUTO_SEL_ENABLE];
	perf_ctrls->min_perf_valid = false;

	/* Are any of the regs PCC ?*/
	if (CPC_IN_PCC(min_perf_reg) || CPC_IN_PCC(max_perf_reg) ||
	    CPC_IN_PCC(energy_perf_reg) ||
	    CPC_IN_PCC(auto_sel_reg)) {
		if (pcc_ss_id < 0) {
			pr_debug("Invalid pcc_ss_id for CPU:%d\n", cpu);
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

	/* Read optional elements if present */
	if (CPC_SUPPORTED(max_perf_reg)) {
		ret = cpc_read(cpu, max_perf_reg, &max);
		if (ret)
			goto out_err;
		if (max > U32_MAX) {
			ret = -EFAULT;
			goto out_err;
		}
	}
	perf_ctrls->max_perf = max;

	if (CPC_SUPPORTED(min_perf_reg)) {
		ret = cpc_read(cpu, min_perf_reg, &min);
		if (ret)
			goto out_err;
		if (min > U32_MAX) {
			ret = -EFAULT;
			goto out_err;
		}
		perf_ctrls->min_perf_valid = true;
	}
	perf_ctrls->min_perf = min;

	perf_ctrls->desired_perf = 0;

	if (CPC_SUPPORTED(energy_perf_reg)) {
		ret = cpc_read(cpu, energy_perf_reg, &energy_perf);
		if (ret)
			goto out_err;
	}
	perf_ctrls->energy_perf = energy_perf;

	if (CPC_SUPPORTED(auto_sel_reg)) {
		ret = cpc_read(cpu, auto_sel_reg, &auto_sel);
		if (ret)
			goto out_err;
	}
	perf_ctrls->auto_sel = (bool)auto_sel;

out_err:
	if (regs_in_pcc)
		up_write(&pcc_ss_data->pcc_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(cppc_get_perf);

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
	bool desired_update, min_update, max_update;
	bool desired_pcc, min_pcc, max_pcc, pcc_update;
	bool pcc_layout, direct_layout, mixed_layout;
	int ret = 0;

	if (!cpc_desc) {
		pr_debug("No CPC descriptor for CPU:%d\n", cpu);
		return -ENODEV;
	}

	desired_reg = &cpc_desc->cpc_regs[DESIRED_PERF];
	min_perf_reg = &cpc_desc->cpc_regs[MIN_PERF];
	max_perf_reg = &cpc_desc->cpc_regs[MAX_PERF];
	desired_update = cpc_is_writable(desired_reg);
	min_update = cpc_is_writable(min_perf_reg) &&
		     (perf_ctrls->min_perf || perf_ctrls->min_perf_valid);
	max_update = cpc_is_writable(max_perf_reg) &&
		     perf_ctrls->max_perf;
	desired_pcc = desired_update && CPC_IN_PCC(desired_reg);
	min_pcc = min_update && CPC_IN_PCC(min_perf_reg);
	max_pcc = max_update && CPC_IN_PCC(max_perf_reg);
	pcc_update = desired_pcc || min_pcc || max_pcc;
	pcc_layout = (cpc_is_writable(desired_reg) && CPC_IN_PCC(desired_reg)) ||
		     (cpc_is_writable(min_perf_reg) && CPC_IN_PCC(min_perf_reg)) ||
		     (cpc_is_writable(max_perf_reg) && CPC_IN_PCC(max_perf_reg));
	direct_layout = (cpc_is_writable(desired_reg) &&
			 !CPC_IN_PCC(desired_reg)) ||
			(cpc_is_writable(min_perf_reg) &&
			 !CPC_IN_PCC(min_perf_reg)) ||
			(cpc_is_writable(max_perf_reg) &&
			 !CPC_IN_PCC(max_perf_reg));
	mixed_layout = pcc_layout && direct_layout;

	if (mixed_layout || pcc_update) {
		if (pcc_ss_id < 0) {
			pr_debug("Invalid pcc_ss_id\n");
			return -ENODEV;
		}
		pcc_ss_data = pcc_data[pcc_ss_id];
		if (!pcc_ss_data)
			return -ENODEV;
	}

	/*
	 * A mixed layout cannot batch fallible direct writes safely: another
	 * CPU's staged PCC values may no longer match if a direct write fails.
	 * Serialize the complete mixed transaction and drain an older batch
	 * before changing a direct control.
	 */
	if (mixed_layout) {
		down_write(&pcc_ss_data->pcc_lock);
		if (pcc_ss_data->pending_pcc_write_cmd) {
			ret = send_pcc_cmd(pcc_ss_id, CMD_WRITE);
			if (ret)
				goto out_mixed_unlock;
		}

		if (pcc_ss_data->platform_owns_pcc) {
			ret = check_pcc_chan(pcc_ss_id, false);
			if (ret)
				goto out_mixed_unlock;
		}

		if (desired_update && !desired_pcc) {
			ret = cpc_write(cpu, desired_reg,
					perf_ctrls->desired_perf);
			if (ret)
				goto out_mixed_unlock;
		}
		if (min_update && !min_pcc) {
			ret = cpc_write(cpu, min_perf_reg,
					perf_ctrls->min_perf);
			if (ret)
				goto out_mixed_unlock;
		}
		if (max_update && !max_pcc) {
			ret = cpc_write(cpu, max_perf_reg,
					perf_ctrls->max_perf);
			if (ret)
				goto out_mixed_unlock;
		}

		if (desired_pcc) {
			ret = cpc_write(cpu, desired_reg,
					perf_ctrls->desired_perf);
			if (ret)
				goto out_mixed_unlock;
		}
		if (min_pcc) {
			ret = cpc_write(cpu, min_perf_reg,
					perf_ctrls->min_perf);
			if (ret)
				goto out_mixed_unlock;
		}
		if (max_pcc) {
			ret = cpc_write(cpu, max_perf_reg,
					perf_ctrls->max_perf);
			if (ret)
				goto out_mixed_unlock;
		}

		if (pcc_update) {
			WRITE_ONCE(pcc_ss_data->pending_pcc_write_cmd, true);
			cpc_desc->write_cmd_id = pcc_ss_data->pcc_write_cnt;
			cpc_desc->write_cmd_status = 0;
			ret = send_pcc_cmd(pcc_ss_id, CMD_WRITE);
		}

out_mixed_unlock:
		up_write(&pcc_ss_data->pcc_lock);
		return ret;
	}

	/* A request without PCC updates has no payload to coordinate. */
	if (!pcc_update) {
		if (desired_update) {
			ret = cpc_write(cpu, desired_reg,
					perf_ctrls->desired_perf);
			if (ret)
				return ret;
		}
		if (min_update) {
			ret = cpc_write(cpu, min_perf_reg,
					perf_ctrls->min_perf);
			if (ret)
				return ret;
		}
		if (max_update)
			ret = cpc_write(cpu, max_perf_reg,
					perf_ctrls->max_perf);
		return ret;
	}

	down_read(&pcc_ss_data->pcc_lock); /* BEGIN Phase-I */
	if (pcc_ss_data->platform_owns_pcc) {
		ret = check_pcc_chan(pcc_ss_id, false);
		if (ret)
			goto out_pcc_read_unlock;
	}

	/*
	 * This is Phase-I where we want to write to CPC registers
	 * -> We want all CPUs to be able to execute this phase in parallel
	 *
	 * Since read_lock can be acquired by multiple CPUs simultaneously we
	 * achieve that goal here.
	 */
	if (desired_pcc) {
		ret = cpc_write(cpu, desired_reg, perf_ctrls->desired_perf);
		if (ret)
			goto out_pcc_read_unlock;
	}

	if (min_pcc) {
		ret = cpc_write(cpu, min_perf_reg, perf_ctrls->min_perf);
		if (ret)
			goto out_pcc_read_unlock;
	}
	if (max_pcc) {
		ret = cpc_write(cpu, max_perf_reg, perf_ctrls->max_perf);
		if (ret)
			goto out_pcc_read_unlock;
	}

	/* Block a PCC read until the staged payload has been submitted. */
	WRITE_ONCE(pcc_ss_data->pending_pcc_write_cmd, true);
	cpc_desc->write_cmd_id = pcc_ss_data->pcc_write_cnt;
	cpc_desc->write_cmd_status = 0;
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
	if (down_write_trylock(&pcc_ss_data->pcc_lock)) {/* BEGIN Phase-II */
		/* Update only if there are pending write commands */
		if (pcc_ss_data->pending_pcc_write_cmd)
			send_pcc_cmd(pcc_ss_id, CMD_WRITE);
		up_write(&pcc_ss_data->pcc_lock);	/* END Phase-II */
	} else {
		/* Wait until pcc_write_cnt is updated by send_pcc_cmd */
		wait_event(pcc_ss_data->pcc_write_wait_q,
			   cpc_desc->write_cmd_id != pcc_ss_data->pcc_write_cnt);
	}

	/* send_pcc_cmd updates the status in case of failure */
	return cpc_desc->write_cmd_status;

out_pcc_read_unlock:
	up_read(&pcc_ss_data->pcc_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(cppc_set_perf);

/**
 * cppc_get_perf_limited - Get the Performance Limited register value.
 * @cpu: CPU from which to get Performance Limited register.
 * @perf_limited: Pointer to store the Performance Limited value.
 *
 * The returned value contains sticky status bits indicating platform-imposed
 * performance limitations.
 *
 * Return: 0 for success, -EIO on failure, -EOPNOTSUPP if not supported.
 */
int cppc_get_perf_limited(int cpu, u64 *perf_limited)
{
	return cppc_get_reg_val(cpu, PERF_LIMITED, perf_limited);
}
EXPORT_SYMBOL_GPL(cppc_get_perf_limited);

/**
 * cppc_set_perf_limited() - Clear bits in the Performance Limited register.
 * @cpu: CPU on which to write register.
 * @bits_to_clear: Zero for no-op or CPPC_PERF_LIMITED_MASK to clear both bits.
 *
 * The Performance Limited register contains two sticky bits set by platform:
 *   - Bit 0 (Desired_Excursion): Set when delivered performance is constrained
 *     below desired performance. Not used when Autonomous Selection is enabled.
 *   - Bit 1 (Minimum_Excursion): Set when delivered performance is constrained
 *     below minimum performance.
 *
 * These bits are sticky and remain set until OSPM explicitly clears them.
 * Selective clears are unsupported because they require an interlocked RMW.
 *
 * Return: 0 for success, -EINVAL for invalid bits, -EIO on register
 *         access failure, -EOPNOTSUPP if not supported.
 */
int cppc_set_perf_limited(int cpu, u64 bits_to_clear)
{
	/* Only bits 0 and 1 are valid */
	if (bits_to_clear & ~(u64)CPPC_PERF_LIMITED_MASK)
		return -EINVAL;

	if (!bits_to_clear)
		return 0;

	/*
	 * Writing zero clears both bits without depending on how a platform
	 * treats written ones. ACPI does not define the effect of writing one,
	 * so a selective clear cannot be implemented without an interlocked RMW.
	 */
	if (bits_to_clear != CPPC_PERF_LIMITED_MASK)
		return -EOPNOTSUPP;

	return cppc_set_reg_val(cpu, PERF_LIMITED, 0);
}
EXPORT_SYMBOL_GPL(cppc_set_perf_limited);

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
int cppc_get_transition_latency(int cpu_num)
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
	struct cpc_desc *cpc_desc;
	struct cpc_register_resource *desired_reg;
	int pcc_ss_id = per_cpu(cpu_pcc_subspace_idx, cpu_num);
	struct cppc_pcc_data *pcc_ss_data;
	int latency_ns = 0;

	cpc_desc = per_cpu(cpc_desc_ptr, cpu_num);
	if (!cpc_desc)
		return -ENODATA;

	desired_reg = &cpc_desc->cpc_regs[DESIRED_PERF];
	if (!cpc_is_writable(desired_reg))
		return -ENODATA;

	if (CPC_IN_SYSTEM_MEMORY(desired_reg) || CPC_IN_SYSTEM_IO(desired_reg))
		return 0;

	if (!CPC_IN_PCC(desired_reg) || pcc_ss_id < 0)
		return -ENODATA;

	pcc_ss_data = pcc_data[pcc_ss_id];
	if (pcc_ss_data->pcc_mpar)
		latency_ns = 60 * (1000 * 1000 * 1000 / pcc_ss_data->pcc_mpar);

	latency_ns = max_t(int, latency_ns, pcc_ss_data->pcc_nominal * 1000);
	latency_ns = max_t(int, latency_ns, pcc_ss_data->pcc_mrtt * 1000);

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
u64 cppc_get_dmi_max_khz(void)
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
EXPORT_SYMBOL_GPL(cppc_get_dmi_max_khz);

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
		/* Avoid special case when nominal_freq is equal to lowest_freq */
		if (caps->lowest_freq == caps->nominal_freq) {
			mul = caps->nominal_freq;
			div = caps->nominal_perf;
		} else {
			mul = caps->nominal_freq - caps->lowest_freq;
			div = caps->nominal_perf - caps->lowest_perf;
		}
		mul *= KHZ_PER_MHZ;
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
	u64 mul, div;

	if (caps->lowest_freq && caps->nominal_freq) {
		/* Avoid special case when nominal_freq is equal to lowest_freq */
		if (caps->lowest_freq == caps->nominal_freq) {
			mul = caps->nominal_perf;
			div = caps->nominal_freq;
		} else {
			mul = caps->nominal_perf - caps->lowest_perf;
			div = caps->nominal_freq - caps->lowest_freq;
		}
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
