/*
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 * Copyright IBM Corp. 2008
 *
 * Authors: Hollis Blanchard <hollisb@us.ibm.com>
 *          Christian Ehrhardt <ehrhardt@linux.vnet.ibm.com>
 */

#include <linux/kvm_host.h>
#include <linux/fs.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/uaccess.h>
#include <linux/module.h>

#include <asm/time.h>
#include <asm-generic/div64.h>

#include "timing.h"

void kvmppc_init_timing_stats(struct kvm_vcpu *vcpu)
{
	int i;

	/* pause guest execution to avoid concurrent updates */
	mutex_lock(&vcpu->mutex);

	vcpu->arch.last_exit_type = 0xDEAD;
	for (i = 0; i < __NUMBER_OF_KVM_EXIT_TYPES; i++) {
		vcpu->arch.timing_count_type[i] = 0;
		vcpu->arch.timing_max_duration[i] = 0;
		vcpu->arch.timing_min_duration[i] = 0xFFFFFFFF;
		vcpu->arch.timing_sum_duration[i] = 0;
		vcpu->arch.timing_sum_quad_duration[i] = 0;
	}
	vcpu->arch.timing_last_exit = 0;
	vcpu->arch.timing_exit.tv64 = 0;
	vcpu->arch.timing_last_enter.tv64 = 0;

	mutex_unlock(&vcpu->mutex);
}

static void add_exit_timing(struct kvm_vcpu *vcpu, u64 duration, int type)
{
	u64 old;

	do_div(duration, tb_ticks_per_usec);
	if (unlikely(duration > 0xFFFFFFFF)) {
		printk(KERN_ERR"%s - duration too big -> overflow"
			" duration %lld type %d exit #%d\n",
			__func__, duration, type,
			vcpu->arch.timing_count_type[type]);
		return;
	}

	vcpu->arch.timing_count_type[type]++;

	/* sum */
	old = vcpu->arch.timing_sum_duration[type];
	vcpu->arch.timing_sum_duration[type] += duration;
	if (unlikely(old > vcpu->arch.timing_sum_duration[type])) {
		printk(KERN_ERR"%s - wrap adding sum of durations"
			" old %lld new %lld type %d exit # of type %d\n",
			__func__, old, vcpu->arch.timing_sum_duration[type],
			type, vcpu->arch.timing_count_type[type]);
	}

	/* square sum */
	old = vcpu->arch.timing_sum_quad_duration[type];
	vcpu->arch.timing_sum_quad_duration[type] += (duration*duration);
	if (unlikely(old > vcpu->arch.timing_sum_quad_duration[type])) {
		printk(KERN_ERR"%s - wrap adding sum of squared durations"
			" old %lld new %lld type %d exit # of type %d\n",
			__func__, old,
			vcpu->arch.timing_sum_quad_duration[type],
			type, vcpu->arch.timing_count_type[type]);
	}

	/* set min/max */
	if (unlikely(duration < vcpu->arch.timing_min_duration[type]))
		vcpu->arch.timing_min_duration[type] = duration;
	if (unlikely(duration > vcpu->arch.timing_max_duration[type]))
		vcpu->arch.timing_max_duration[type] = duration;
}

void kvmppc_update_timing_stats(struct kvm_vcpu *vcpu)
{
	u64 exit = vcpu->arch.timing_last_exit;
	u64 enter = vcpu->arch.timing_last_enter.tv64;

	/* save exit time, used next exit when the reenter time is known */
	vcpu->arch.timing_last_exit = vcpu->arch.timing_exit.tv64;

	if (unlikely(vcpu->arch.last_exit_type == 0xDEAD || exit == 0))
		return; /* skip incomplete cycle (e.g. after reset) */

	/* update statistics for average and standard deviation */
	add_exit_timing(vcpu, (enter - exit), vcpu->arch.last_exit_type);
	/* enter -> timing_last_exit is time spent in guest - log this too */
	add_exit_timing(vcpu, (vcpu->arch.timing_last_exit - enter),
			TIMEINGUEST);
}

static const char *kvm_exit_names[__NUMBER_OF_KVM_EXIT_TYPES] = {
	[MMIO_EXITS] =              "MMIO",
	[DCR_EXITS] =               "DCR",
	[SIGNAL_EXITS] =            "SIGNAL",
	[ITLB_REAL_MISS_EXITS] =    "ITLBREAL",
	[ITLB_VIRT_MISS_EXITS] =    "ITLBVIRT",
	[DTLB_REAL_MISS_EXITS] =    "DTLBREAL",
	[DTLB_VIRT_MISS_EXITS] =    "DTLBVIRT",
	[SYSCALL_EXITS] =           "SYSCALL",
	[ISI_EXITS] =               "ISI",
	[DSI_EXITS] =               "DSI",
	[EMULATED_INST_EXITS] =     "EMULINST",
	[EMULATED_MTMSRWE_EXITS] =  "EMUL_WAIT",
	[EMULATED_WRTEE_EXITS] =    "EMUL_WRTEE",
	[EMULATED_MTSPR_EXITS] =    "EMUL_MTSPR",
	[EMULATED_MFSPR_EXITS] =    "EMUL_MFSPR",
	[EMULATED_MTMSR_EXITS] =    "EMUL_MTMSR",
	[EMULATED_MFMSR_EXITS] =    "EMUL_MFMSR",
	[EMULATED_TLBSX_EXITS] =    "EMUL_TLBSX",
	[EMULATED_TLBWE_EXITS] =    "EMUL_TLBWE",
	[EMULATED_RFI_EXITS] =      "EMUL_RFI",
	[DEC_EXITS] =               "DEC",
	[EXT_INTR_EXITS] =          "EXTINT",
	[HALT_WAKEUP] =             "HALT",
	[USR_PR_INST] =             "USR_PR_INST",
	[FP_UNAVAIL] =              "FP_UNAVAIL",
	[DEBUG_EXITS] =             "DEBUG",
	[TIMEINGUEST] =             "TIMEINGUEST"
};

static int kvmppc_exit_timing_show(struct seq_file *m, void *private)
{
	struct kvm_vcpu *vcpu = m->private;
	int i;

	seq_printf(m, "%s", "type	count	min	max	sum	sum_squared\n");

	for (i = 0; i < __NUMBER_OF_KVM_EXIT_TYPES; i++) {
		seq_printf(m, "%12s	%10d	%10lld	%10lld	%20lld	%20lld\n",
			kvm_exit_names[i],
			vcpu->arch.timing_count_type[i],
			vcpu->arch.timing_min_duration[i],
			vcpu->arch.timing_max_duration[i],
			vcpu->arch.timing_sum_duration[i],
			vcpu->arch.timing_sum_quad_duration[i]);
	}
	return 0;
}

/* Write 'c' to clear the timing statistics. */
static ssize_t kvmppc_exit_timing_write(struct file *file,
				       const char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	int err = -EINVAL;
	char c;

	if (count > 1) {
		goto done;
	}

	if (get_user(c, user_buf)) {
		err = -EFAULT;
		goto done;
	}

	if (c == 'c') {
		struct seq_file *seqf = file->private_data;
		struct kvm_vcpu *vcpu = seqf->private;
		/* Write does not affect our buffers previously generated with
		 * show. seq_file is locked here to prevent races of init with
		 * a show call */
		mutex_lock(&seqf->lock);
		kvmppc_init_timing_stats(vcpu);
		mutex_unlock(&seqf->lock);
		err = count;
	}

done:
	return err;
}

static int kvmppc_exit_timing_open(struct inode *inode, struct file *file)
{
	return single_open(file, kvmppc_exit_timing_show, inode->i_private);
}

static const struct file_operations kvmppc_exit_timing_fops = {
	.owner   = THIS_MODULE,
	.open    = kvmppc_exit_timing_open,
	.read    = seq_read,
	.write   = kvmppc_exit_timing_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

void kvmppc_create_vcpu_debugfs(struct kvm_vcpu *vcpu, unsigned int id)
{
	static char dbg_fname[50];
	struct dentry *debugfs_file;

	snprintf(dbg_fname, sizeof(dbg_fname), "vm%u_vcpu%u_timing",
		 current->pid, id);
	debugfs_file = debugfs_create_file(dbg_fname, 0666,
					kvm_debugfs_dir, vcpu,
					&kvmppc_exit_timing_fops);

	if (!debugfs_file) {
		printk(KERN_ERR"%s: error creating debugfs file %s\n",
			__func__, dbg_fname);
		return;
	}

	vcpu->arch.debugfs_exit_timing = debugfs_file;
}

void kvmppc_remove_vcpu_debugfs(struct kvm_vcpu *vcpu)
{
	if (vcpu->arch.debugfs_exit_timing) {
		debugfs_remove(vcpu->arch.debugfs_exit_timing);
		vcpu->arch.debugfs_exit_timing = NULL;
	}
}
