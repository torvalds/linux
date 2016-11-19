/*
 * kvm guest debug support
 *
 * Copyright IBM Corp. 2014
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License (version 2 only)
 * as published by the Free Software Foundation.
 *
 *    Author(s): David Hildenbrand <dahi@linux.vnet.ibm.com>
 */
#include <linux/kvm_host.h>
#include <linux/errno.h>
#include "kvm-s390.h"
#include "gaccess.h"

/*
 * Extends the address range given by *start and *stop to include the address
 * range starting with estart and the length len. Takes care of overflowing
 * intervals and tries to minimize the overall interval size.
 */
static void extend_address_range(u64 *start, u64 *stop, u64 estart, int len)
{
	u64 estop;

	if (len > 0)
		len--;
	else
		len = 0;

	estop = estart + len;

	/* 0-0 range represents "not set" */
	if ((*start == 0) && (*stop == 0)) {
		*start = estart;
		*stop = estop;
	} else if (*start <= *stop) {
		/* increase the existing range */
		if (estart < *start)
			*start = estart;
		if (estop > *stop)
			*stop = estop;
	} else {
		/* "overflowing" interval, whereby *stop > *start */
		if (estart <= *stop) {
			if (estop > *stop)
				*stop = estop;
		} else if (estop > *start) {
			if (estart < *start)
				*start = estart;
		}
		/* minimize the range */
		else if ((estop - *stop) < (*start - estart))
			*stop = estop;
		else
			*start = estart;
	}
}

#define MAX_INST_SIZE 6

static void enable_all_hw_bp(struct kvm_vcpu *vcpu)
{
	unsigned long start, len;
	u64 *cr9 = &vcpu->arch.sie_block->gcr[9];
	u64 *cr10 = &vcpu->arch.sie_block->gcr[10];
	u64 *cr11 = &vcpu->arch.sie_block->gcr[11];
	int i;

	if (vcpu->arch.guestdbg.nr_hw_bp <= 0 ||
	    vcpu->arch.guestdbg.hw_bp_info == NULL)
		return;

	/*
	 * If the guest is not interested in branching events, we can safely
	 * limit them to the PER address range.
	 */
	if (!(*cr9 & PER_EVENT_BRANCH))
		*cr9 |= PER_CONTROL_BRANCH_ADDRESS;
	*cr9 |= PER_EVENT_IFETCH | PER_EVENT_BRANCH;

	for (i = 0; i < vcpu->arch.guestdbg.nr_hw_bp; i++) {
		start = vcpu->arch.guestdbg.hw_bp_info[i].addr;
		len = vcpu->arch.guestdbg.hw_bp_info[i].len;

		/*
		 * The instruction in front of the desired bp has to
		 * report instruction-fetching events
		 */
		if (start < MAX_INST_SIZE) {
			len += start;
			start = 0;
		} else {
			start -= MAX_INST_SIZE;
			len += MAX_INST_SIZE;
		}

		extend_address_range(cr10, cr11, start, len);
	}
}

static void enable_all_hw_wp(struct kvm_vcpu *vcpu)
{
	unsigned long start, len;
	u64 *cr9 = &vcpu->arch.sie_block->gcr[9];
	u64 *cr10 = &vcpu->arch.sie_block->gcr[10];
	u64 *cr11 = &vcpu->arch.sie_block->gcr[11];
	int i;

	if (vcpu->arch.guestdbg.nr_hw_wp <= 0 ||
	    vcpu->arch.guestdbg.hw_wp_info == NULL)
		return;

	/* if host uses storage alternation for special address
	 * spaces, enable all events and give all to the guest */
	if (*cr9 & PER_EVENT_STORE && *cr9 & PER_CONTROL_ALTERATION) {
		*cr9 &= ~PER_CONTROL_ALTERATION;
		*cr10 = 0;
		*cr11 = -1UL;
	} else {
		*cr9 &= ~PER_CONTROL_ALTERATION;
		*cr9 |= PER_EVENT_STORE;

		for (i = 0; i < vcpu->arch.guestdbg.nr_hw_wp; i++) {
			start = vcpu->arch.guestdbg.hw_wp_info[i].addr;
			len = vcpu->arch.guestdbg.hw_wp_info[i].len;

			extend_address_range(cr10, cr11, start, len);
		}
	}
}

void kvm_s390_backup_guest_per_regs(struct kvm_vcpu *vcpu)
{
	vcpu->arch.guestdbg.cr0 = vcpu->arch.sie_block->gcr[0];
	vcpu->arch.guestdbg.cr9 = vcpu->arch.sie_block->gcr[9];
	vcpu->arch.guestdbg.cr10 = vcpu->arch.sie_block->gcr[10];
	vcpu->arch.guestdbg.cr11 = vcpu->arch.sie_block->gcr[11];
}

void kvm_s390_restore_guest_per_regs(struct kvm_vcpu *vcpu)
{
	vcpu->arch.sie_block->gcr[0] = vcpu->arch.guestdbg.cr0;
	vcpu->arch.sie_block->gcr[9] = vcpu->arch.guestdbg.cr9;
	vcpu->arch.sie_block->gcr[10] = vcpu->arch.guestdbg.cr10;
	vcpu->arch.sie_block->gcr[11] = vcpu->arch.guestdbg.cr11;
}

void kvm_s390_patch_guest_per_regs(struct kvm_vcpu *vcpu)
{
	/*
	 * TODO: if guest psw has per enabled, otherwise 0s!
	 * This reduces the amount of reported events.
	 * Need to intercept all psw changes!
	 */

	if (guestdbg_sstep_enabled(vcpu)) {
		/* disable timer (clock-comparator) interrupts */
		vcpu->arch.sie_block->gcr[0] &= ~0x800ul;
		vcpu->arch.sie_block->gcr[9] |= PER_EVENT_IFETCH;
		vcpu->arch.sie_block->gcr[10] = 0;
		vcpu->arch.sie_block->gcr[11] = -1UL;
	}

	if (guestdbg_hw_bp_enabled(vcpu)) {
		enable_all_hw_bp(vcpu);
		enable_all_hw_wp(vcpu);
	}

	/* TODO: Instruction-fetching-nullification not allowed for now */
	if (vcpu->arch.sie_block->gcr[9] & PER_EVENT_NULLIFICATION)
		vcpu->arch.sie_block->gcr[9] &= ~PER_EVENT_NULLIFICATION;
}

#define MAX_WP_SIZE 100

static int __import_wp_info(struct kvm_vcpu *vcpu,
			    struct kvm_hw_breakpoint *bp_data,
			    struct kvm_hw_wp_info_arch *wp_info)
{
	int ret = 0;
	wp_info->len = bp_data->len;
	wp_info->addr = bp_data->addr;
	wp_info->phys_addr = bp_data->phys_addr;
	wp_info->old_data = NULL;

	if (wp_info->len < 0 || wp_info->len > MAX_WP_SIZE)
		return -EINVAL;

	wp_info->old_data = kmalloc(bp_data->len, GFP_KERNEL);
	if (!wp_info->old_data)
		return -ENOMEM;
	/* try to backup the original value */
	ret = read_guest_abs(vcpu, wp_info->phys_addr, wp_info->old_data,
			     wp_info->len);
	if (ret) {
		kfree(wp_info->old_data);
		wp_info->old_data = NULL;
	}

	return ret;
}

#define MAX_BP_COUNT 50

int kvm_s390_import_bp_data(struct kvm_vcpu *vcpu,
			    struct kvm_guest_debug *dbg)
{
	int ret = 0, nr_wp = 0, nr_bp = 0, i;
	struct kvm_hw_breakpoint *bp_data = NULL;
	struct kvm_hw_wp_info_arch *wp_info = NULL;
	struct kvm_hw_bp_info_arch *bp_info = NULL;

	if (dbg->arch.nr_hw_bp <= 0 || !dbg->arch.hw_bp)
		return 0;
	else if (dbg->arch.nr_hw_bp > MAX_BP_COUNT)
		return -EINVAL;

	bp_data = memdup_user(dbg->arch.hw_bp,
			      sizeof(*bp_data) * dbg->arch.nr_hw_bp);
	if (IS_ERR(bp_data))
		return PTR_ERR(bp_data);

	for (i = 0; i < dbg->arch.nr_hw_bp; i++) {
		switch (bp_data[i].type) {
		case KVM_HW_WP_WRITE:
			nr_wp++;
			break;
		case KVM_HW_BP:
			nr_bp++;
			break;
		default:
			break;
		}
	}

	if (nr_wp > 0) {
		wp_info = kmalloc_array(nr_wp,
					sizeof(*wp_info),
					GFP_KERNEL);
		if (!wp_info) {
			ret = -ENOMEM;
			goto error;
		}
	}
	if (nr_bp > 0) {
		bp_info = kmalloc_array(nr_bp,
					sizeof(*bp_info),
					GFP_KERNEL);
		if (!bp_info) {
			ret = -ENOMEM;
			goto error;
		}
	}

	for (nr_wp = 0, nr_bp = 0, i = 0; i < dbg->arch.nr_hw_bp; i++) {
		switch (bp_data[i].type) {
		case KVM_HW_WP_WRITE:
			ret = __import_wp_info(vcpu, &bp_data[i],
					       &wp_info[nr_wp]);
			if (ret)
				goto error;
			nr_wp++;
			break;
		case KVM_HW_BP:
			bp_info[nr_bp].len = bp_data[i].len;
			bp_info[nr_bp].addr = bp_data[i].addr;
			nr_bp++;
			break;
		}
	}

	vcpu->arch.guestdbg.nr_hw_bp = nr_bp;
	vcpu->arch.guestdbg.hw_bp_info = bp_info;
	vcpu->arch.guestdbg.nr_hw_wp = nr_wp;
	vcpu->arch.guestdbg.hw_wp_info = wp_info;
	return 0;
error:
	kfree(bp_data);
	kfree(wp_info);
	kfree(bp_info);
	return ret;
}

void kvm_s390_clear_bp_data(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvm_hw_wp_info_arch *hw_wp_info = NULL;

	for (i = 0; i < vcpu->arch.guestdbg.nr_hw_wp; i++) {
		hw_wp_info = &vcpu->arch.guestdbg.hw_wp_info[i];
		kfree(hw_wp_info->old_data);
		hw_wp_info->old_data = NULL;
	}
	kfree(vcpu->arch.guestdbg.hw_wp_info);
	vcpu->arch.guestdbg.hw_wp_info = NULL;

	kfree(vcpu->arch.guestdbg.hw_bp_info);
	vcpu->arch.guestdbg.hw_bp_info = NULL;

	vcpu->arch.guestdbg.nr_hw_wp = 0;
	vcpu->arch.guestdbg.nr_hw_bp = 0;
}

static inline int in_addr_range(u64 addr, u64 a, u64 b)
{
	if (a <= b)
		return (addr >= a) && (addr <= b);
	else
		/* "overflowing" interval */
		return (addr <= a) && (addr >= b);
}

#define end_of_range(bp_info) (bp_info->addr + bp_info->len - 1)

static struct kvm_hw_bp_info_arch *find_hw_bp(struct kvm_vcpu *vcpu,
					      unsigned long addr)
{
	struct kvm_hw_bp_info_arch *bp_info = vcpu->arch.guestdbg.hw_bp_info;
	int i;

	if (vcpu->arch.guestdbg.nr_hw_bp == 0)
		return NULL;

	for (i = 0; i < vcpu->arch.guestdbg.nr_hw_bp; i++) {
		/* addr is directly the start or in the range of a bp */
		if (addr == bp_info->addr)
			goto found;
		if (bp_info->len > 0 &&
		    in_addr_range(addr, bp_info->addr, end_of_range(bp_info)))
			goto found;

		bp_info++;
	}

	return NULL;
found:
	return bp_info;
}

static struct kvm_hw_wp_info_arch *any_wp_changed(struct kvm_vcpu *vcpu)
{
	int i;
	struct kvm_hw_wp_info_arch *wp_info = NULL;
	void *temp = NULL;

	if (vcpu->arch.guestdbg.nr_hw_wp == 0)
		return NULL;

	for (i = 0; i < vcpu->arch.guestdbg.nr_hw_wp; i++) {
		wp_info = &vcpu->arch.guestdbg.hw_wp_info[i];
		if (!wp_info || !wp_info->old_data || wp_info->len <= 0)
			continue;

		temp = kmalloc(wp_info->len, GFP_KERNEL);
		if (!temp)
			continue;

		/* refetch the wp data and compare it to the old value */
		if (!read_guest_abs(vcpu, wp_info->phys_addr, temp,
				    wp_info->len)) {
			if (memcmp(temp, wp_info->old_data, wp_info->len)) {
				kfree(temp);
				return wp_info;
			}
		}
		kfree(temp);
		temp = NULL;
	}

	return NULL;
}

void kvm_s390_prepare_debug_exit(struct kvm_vcpu *vcpu)
{
	vcpu->run->exit_reason = KVM_EXIT_DEBUG;
	vcpu->guest_debug &= ~KVM_GUESTDBG_EXIT_PENDING;
}

#define PER_CODE_MASK		(PER_EVENT_MASK >> 24)
#define PER_CODE_BRANCH		(PER_EVENT_BRANCH >> 24)
#define PER_CODE_IFETCH		(PER_EVENT_IFETCH >> 24)
#define PER_CODE_STORE		(PER_EVENT_STORE >> 24)
#define PER_CODE_STORE_REAL	(PER_EVENT_STORE_REAL >> 24)

#define per_bp_event(code) \
			(code & (PER_CODE_IFETCH | PER_CODE_BRANCH))
#define per_write_wp_event(code) \
			(code & (PER_CODE_STORE | PER_CODE_STORE_REAL))

static int debug_exit_required(struct kvm_vcpu *vcpu)
{
	u8 perc = vcpu->arch.sie_block->perc;
	struct kvm_debug_exit_arch *debug_exit = &vcpu->run->debug.arch;
	struct kvm_hw_wp_info_arch *wp_info = NULL;
	struct kvm_hw_bp_info_arch *bp_info = NULL;
	unsigned long addr = vcpu->arch.sie_block->gpsw.addr;
	unsigned long peraddr = vcpu->arch.sie_block->peraddr;

	if (guestdbg_hw_bp_enabled(vcpu)) {
		if (per_write_wp_event(perc) &&
		    vcpu->arch.guestdbg.nr_hw_wp > 0) {
			wp_info = any_wp_changed(vcpu);
			if (wp_info) {
				debug_exit->addr = wp_info->addr;
				debug_exit->type = KVM_HW_WP_WRITE;
				goto exit_required;
			}
		}
		if (per_bp_event(perc) &&
			 vcpu->arch.guestdbg.nr_hw_bp > 0) {
			bp_info = find_hw_bp(vcpu, addr);
			/* remove duplicate events if PC==PER address */
			if (bp_info && (addr != peraddr)) {
				debug_exit->addr = addr;
				debug_exit->type = KVM_HW_BP;
				vcpu->arch.guestdbg.last_bp = addr;
				goto exit_required;
			}
			/* breakpoint missed */
			bp_info = find_hw_bp(vcpu, peraddr);
			if (bp_info && vcpu->arch.guestdbg.last_bp != peraddr) {
				debug_exit->addr = peraddr;
				debug_exit->type = KVM_HW_BP;
				goto exit_required;
			}
		}
	}
	if (guestdbg_sstep_enabled(vcpu) && per_bp_event(perc)) {
		debug_exit->addr = addr;
		debug_exit->type = KVM_SINGLESTEP;
		goto exit_required;
	}

	return 0;
exit_required:
	return 1;
}

#define guest_per_enabled(vcpu) \
			     (vcpu->arch.sie_block->gpsw.mask & PSW_MASK_PER)

int kvm_s390_handle_per_ifetch_icpt(struct kvm_vcpu *vcpu)
{
	const u8 ilen = kvm_s390_get_ilen(vcpu);
	struct kvm_s390_pgm_info pgm_info = {
		.code = PGM_PER,
		.per_code = PER_CODE_IFETCH,
		.per_address = __rewind_psw(vcpu->arch.sie_block->gpsw, ilen),
	};

	/*
	 * The PSW points to the next instruction, therefore the intercepted
	 * instruction generated a PER i-fetch event. PER address therefore
	 * points at the previous PSW address (could be an EXECUTE function).
	 */
	return kvm_s390_inject_prog_irq(vcpu, &pgm_info);
}

static void filter_guest_per_event(struct kvm_vcpu *vcpu)
{
	const u8 perc = vcpu->arch.sie_block->perc;
	u64 peraddr = vcpu->arch.sie_block->peraddr;
	u64 addr = vcpu->arch.sie_block->gpsw.addr;
	u64 cr9 = vcpu->arch.sie_block->gcr[9];
	u64 cr10 = vcpu->arch.sie_block->gcr[10];
	u64 cr11 = vcpu->arch.sie_block->gcr[11];
	/* filter all events, demanded by the guest */
	u8 guest_perc = perc & (cr9 >> 24) & PER_CODE_MASK;

	if (!guest_per_enabled(vcpu))
		guest_perc = 0;

	/* filter "successful-branching" events */
	if (guest_perc & PER_CODE_BRANCH &&
	    cr9 & PER_CONTROL_BRANCH_ADDRESS &&
	    !in_addr_range(addr, cr10, cr11))
		guest_perc &= ~PER_CODE_BRANCH;

	/* filter "instruction-fetching" events */
	if (guest_perc & PER_CODE_IFETCH &&
	    !in_addr_range(peraddr, cr10, cr11))
		guest_perc &= ~PER_CODE_IFETCH;

	/* All other PER events will be given to the guest */
	/* TODO: Check altered address/address space */

	vcpu->arch.sie_block->perc = guest_perc;

	if (!guest_perc)
		vcpu->arch.sie_block->iprcc &= ~PGM_PER;
}

#define pssec(vcpu) (vcpu->arch.sie_block->gcr[1] & _ASCE_SPACE_SWITCH)
#define hssec(vcpu) (vcpu->arch.sie_block->gcr[13] & _ASCE_SPACE_SWITCH)
#define old_ssec(vcpu) ((vcpu->arch.sie_block->tecmc >> 31) & 0x1)
#define old_as_is_home(vcpu) !(vcpu->arch.sie_block->tecmc & 0xffff)

void kvm_s390_handle_per_event(struct kvm_vcpu *vcpu)
{
	int new_as;

	if (debug_exit_required(vcpu))
		vcpu->guest_debug |= KVM_GUESTDBG_EXIT_PENDING;

	filter_guest_per_event(vcpu);

	/*
	 * Only RP, SAC, SACF, PT, PTI, PR, PC instructions can trigger
	 * a space-switch event. PER events enforce space-switch events
	 * for these instructions. So if no PER event for the guest is left,
	 * we might have to filter the space-switch element out, too.
	 */
	if (vcpu->arch.sie_block->iprcc == PGM_SPACE_SWITCH) {
		vcpu->arch.sie_block->iprcc = 0;
		new_as = psw_bits(vcpu->arch.sie_block->gpsw).as;

		/*
		 * If the AS changed from / to home, we had RP, SAC or SACF
		 * instruction. Check primary and home space-switch-event
		 * controls. (theoretically home -> home produced no event)
		 */
		if (((new_as == PSW_AS_HOME) ^ old_as_is_home(vcpu)) &&
		     (pssec(vcpu) || hssec(vcpu)))
			vcpu->arch.sie_block->iprcc = PGM_SPACE_SWITCH;

		/*
		 * PT, PTI, PR, PC instruction operate on primary AS only. Check
		 * if the primary-space-switch-event control was or got set.
		 */
		if (new_as == PSW_AS_PRIMARY && !old_as_is_home(vcpu) &&
		    (pssec(vcpu) || old_ssec(vcpu)))
			vcpu->arch.sie_block->iprcc = PGM_SPACE_SWITCH;
	}
}
