/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004, 2005 MIPS Technologies, Inc.  All rights reserved.
 * Copyright (C) 2013 Imagination Technologies Ltd.
 */
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/export.h>

#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/mips_mt.h>
#include <asm/vpe.h>

static int major;

/* The number of TCs and VPEs physically available on the core */
static int hw_tcs, hw_vpes;

/* We are prepared so configure and start the VPE... */
int vpe_run(struct vpe *v)
{
	unsigned long flags, val, dmt_flag;
	struct vpe_notifications *notifier;
	unsigned int vpeflags;
	struct tc *t;

	/* check we are the Master VPE */
	local_irq_save(flags);
	val = read_c0_vpeconf0();
	if (!(val & VPECONF0_MVP)) {
		pr_warn("VPE loader: only Master VPE's are able to config MT\n");
		local_irq_restore(flags);

		return -1;
	}

	dmt_flag = dmt();
	vpeflags = dvpe();

	if (list_empty(&v->tc)) {
		evpe(vpeflags);
		emt(dmt_flag);
		local_irq_restore(flags);

		pr_warn("VPE loader: No TC's associated with VPE %d\n",
			v->minor);

		return -ENOEXEC;
	}

	t = list_first_entry(&v->tc, struct tc, tc);

	/* Put MVPE's into 'configuration state' */
	set_c0_mvpcontrol(MVPCONTROL_VPC);

	settc(t->index);

	/* should check it is halted, and not activated */
	if ((read_tc_c0_tcstatus() & TCSTATUS_A) ||
	   !(read_tc_c0_tchalt() & TCHALT_H)) {
		evpe(vpeflags);
		emt(dmt_flag);
		local_irq_restore(flags);

		pr_warn("VPE loader: TC %d is already active!\n",
			t->index);

		return -ENOEXEC;
	}

	/*
	 * Write the address we want it to start running from in the TCPC
	 * register.
	 */
	write_tc_c0_tcrestart((unsigned long)v->__start);
	write_tc_c0_tccontext((unsigned long)0);

	/*
	 * Mark the TC as activated, not interrupt exempt and not dynamically
	 * allocatable
	 */
	val = read_tc_c0_tcstatus();
	val = (val & ~(TCSTATUS_DA | TCSTATUS_IXMT)) | TCSTATUS_A;
	write_tc_c0_tcstatus(val);

	write_tc_c0_tchalt(read_tc_c0_tchalt() & ~TCHALT_H);

	/*
	 * The sde-kit passes 'memsize' to __start in $a3, so set something
	 * here...  Or set $a3 to zero and define DFLT_STACK_SIZE and
	 * DFLT_HEAP_SIZE when you compile your program
	 */
	mttgpr(6, v->ntcs);
	mttgpr(7, physical_memsize);

	/* set up VPE1 */
	/*
	 * bind the TC to VPE 1 as late as possible so we only have the final
	 * VPE registers to set up, and so an EJTAG probe can trigger on it
	 */
	write_tc_c0_tcbind((read_tc_c0_tcbind() & ~TCBIND_CURVPE) | 1);

	write_vpe_c0_vpeconf0(read_vpe_c0_vpeconf0() & ~(VPECONF0_VPA));

	back_to_back_c0_hazard();

	/* Set up the XTC bit in vpeconf0 to point at our tc */
	write_vpe_c0_vpeconf0((read_vpe_c0_vpeconf0() & ~(VPECONF0_XTC))
			      | (t->index << VPECONF0_XTC_SHIFT));

	back_to_back_c0_hazard();

	/* enable this VPE */
	write_vpe_c0_vpeconf0(read_vpe_c0_vpeconf0() | VPECONF0_VPA);

	/* clear out any left overs from a previous program */
	write_vpe_c0_status(0);
	write_vpe_c0_cause(0);

	/* take system out of configuration state */
	clear_c0_mvpcontrol(MVPCONTROL_VPC);

	/*
	 * SMVP kernels manage VPE enable independently, but uniprocessor
	 * kernels need to turn it on, even if that wasn't the pre-dvpe() state.
	 */
#ifdef CONFIG_SMP
	evpe(vpeflags);
#else
	evpe(EVPE_ENABLE);
#endif
	emt(dmt_flag);
	local_irq_restore(flags);

	list_for_each_entry(notifier, &v->notify, list)
		notifier->start(VPE_MODULE_MINOR);

	return 0;
}

void cleanup_tc(struct tc *tc)
{
	unsigned long flags;
	unsigned int mtflags, vpflags;
	int tmp;

	local_irq_save(flags);
	mtflags = dmt();
	vpflags = dvpe();
	/* Put MVPE's into 'configuration state' */
	set_c0_mvpcontrol(MVPCONTROL_VPC);

	settc(tc->index);
	tmp = read_tc_c0_tcstatus();

	/* mark not allocated and not dynamically allocatable */
	tmp &= ~(TCSTATUS_A | TCSTATUS_DA);
	tmp |= TCSTATUS_IXMT;	/* interrupt exempt */
	write_tc_c0_tcstatus(tmp);

	write_tc_c0_tchalt(TCHALT_H);
	mips_ihb();

	clear_c0_mvpcontrol(MVPCONTROL_VPC);
	evpe(vpflags);
	emt(mtflags);
	local_irq_restore(flags);
}

/* module wrapper entry points */
/* give me a vpe */
void *vpe_alloc(void)
{
	int i;
	struct vpe *v;

	/* find a vpe */
	for (i = 1; i < MAX_VPES; i++) {
		v = get_vpe(i);
		if (v != NULL) {
			v->state = VPE_STATE_INUSE;
			return v;
		}
	}
	return NULL;
}
EXPORT_SYMBOL(vpe_alloc);

/* start running from here */
int vpe_start(void *vpe, unsigned long start)
{
	struct vpe *v = vpe;

	v->__start = start;
	return vpe_run(v);
}
EXPORT_SYMBOL(vpe_start);

/* halt it for now */
int vpe_stop(void *vpe)
{
	struct vpe *v = vpe;
	struct tc *t;
	unsigned int evpe_flags;

	evpe_flags = dvpe();

	t = list_entry(v->tc.next, struct tc, tc);
	if (t != NULL) {
		settc(t->index);
		write_vpe_c0_vpeconf0(read_vpe_c0_vpeconf0() & ~VPECONF0_VPA);
	}

	evpe(evpe_flags);

	return 0;
}
EXPORT_SYMBOL(vpe_stop);

/* I've done with it thank you */
int vpe_free(void *vpe)
{
	struct vpe *v = vpe;
	struct tc *t;
	unsigned int evpe_flags;

	t = list_entry(v->tc.next, struct tc, tc);
	if (t == NULL)
		return -ENOEXEC;

	evpe_flags = dvpe();

	/* Put MVPE's into 'configuration state' */
	set_c0_mvpcontrol(MVPCONTROL_VPC);

	settc(t->index);
	write_vpe_c0_vpeconf0(read_vpe_c0_vpeconf0() & ~VPECONF0_VPA);

	/* halt the TC */
	write_tc_c0_tchalt(TCHALT_H);
	mips_ihb();

	/* mark the TC unallocated */
	write_tc_c0_tcstatus(read_tc_c0_tcstatus() & ~TCSTATUS_A);

	v->state = VPE_STATE_UNUSED;

	clear_c0_mvpcontrol(MVPCONTROL_VPC);
	evpe(evpe_flags);

	return 0;
}
EXPORT_SYMBOL(vpe_free);

static ssize_t store_kill(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t len)
{
	struct vpe *vpe = get_vpe(aprp_cpu_index());
	struct vpe_notifications *notifier;

	list_for_each_entry(notifier, &vpe->notify, list)
		notifier->stop(aprp_cpu_index());

	release_progmem(vpe->load_addr);
	cleanup_tc(get_tc(aprp_cpu_index()));
	vpe_stop(vpe);
	vpe_free(vpe);

	return len;
}
static DEVICE_ATTR(kill, S_IWUSR, NULL, store_kill);

static ssize_t ntcs_show(struct device *cd, struct device_attribute *attr,
			 char *buf)
{
	struct vpe *vpe = get_vpe(aprp_cpu_index());

	return sprintf(buf, "%d\n", vpe->ntcs);
}

static ssize_t ntcs_store(struct device *dev, struct device_attribute *attr,
			  const char *buf, size_t len)
{
	struct vpe *vpe = get_vpe(aprp_cpu_index());
	unsigned long new;
	int ret;

	ret = kstrtoul(buf, 0, &new);
	if (ret < 0)
		return ret;

	if (new == 0 || new > (hw_tcs - aprp_cpu_index()))
		return -EINVAL;

	vpe->ntcs = new;

	return len;
}
static DEVICE_ATTR_RW(ntcs);

static struct attribute *vpe_attrs[] = {
	&dev_attr_kill.attr,
	&dev_attr_ntcs.attr,
	NULL,
};
ATTRIBUTE_GROUPS(vpe);

static void vpe_device_release(struct device *cd)
{
	kfree(cd);
}

static struct class vpe_class = {
	.name = "vpe",
	.owner = THIS_MODULE,
	.dev_release = vpe_device_release,
	.dev_groups = vpe_groups,
};

static struct device vpe_device;

int __init vpe_module_init(void)
{
	unsigned int mtflags, vpflags;
	unsigned long flags, val;
	struct vpe *v = NULL;
	struct tc *t;
	int tc, err;

	if (!cpu_has_mipsmt) {
		pr_warn("VPE loader: not a MIPS MT capable processor\n");
		return -ENODEV;
	}

	if (vpelimit == 0) {
		pr_warn("No VPEs reserved for AP/SP, not initialize VPE loader\n"
			"Pass maxvpes=<n> argument as kernel argument\n");

		return -ENODEV;
	}

	if (aprp_cpu_index() == 0) {
		pr_warn("No TCs reserved for AP/SP, not initialize VPE loader\n"
			"Pass maxtcs=<n> argument as kernel argument\n");

		return -ENODEV;
	}

	major = register_chrdev(0, VPE_MODULE_NAME, &vpe_fops);
	if (major < 0) {
		pr_warn("VPE loader: unable to register character device\n");
		return major;
	}

	err = class_register(&vpe_class);
	if (err) {
		pr_err("vpe_class registration failed\n");
		goto out_chrdev;
	}

	device_initialize(&vpe_device);
	vpe_device.class	= &vpe_class;
	vpe_device.parent	= NULL;
	dev_set_name(&vpe_device, "vpe1");
	vpe_device.devt = MKDEV(major, VPE_MODULE_MINOR);
	err = device_add(&vpe_device);
	if (err) {
		pr_err("Adding vpe_device failed\n");
		goto out_class;
	}

	local_irq_save(flags);
	mtflags = dmt();
	vpflags = dvpe();

	/* Put MVPE's into 'configuration state' */
	set_c0_mvpcontrol(MVPCONTROL_VPC);

	val = read_c0_mvpconf0();
	hw_tcs = (val & MVPCONF0_PTC) + 1;
	hw_vpes = ((val & MVPCONF0_PVPE) >> MVPCONF0_PVPE_SHIFT) + 1;

	for (tc = aprp_cpu_index(); tc < hw_tcs; tc++) {
		/*
		 * Must re-enable multithreading temporarily or in case we
		 * reschedule send IPIs or similar we might hang.
		 */
		clear_c0_mvpcontrol(MVPCONTROL_VPC);
		evpe(vpflags);
		emt(mtflags);
		local_irq_restore(flags);
		t = alloc_tc(tc);
		if (!t) {
			err = -ENOMEM;
			goto out_dev;
		}

		local_irq_save(flags);
		mtflags = dmt();
		vpflags = dvpe();
		set_c0_mvpcontrol(MVPCONTROL_VPC);

		/* VPE's */
		if (tc < hw_tcs) {
			settc(tc);

			v = alloc_vpe(tc);
			if (v == NULL) {
				pr_warn("VPE: unable to allocate VPE\n");
				goto out_reenable;
			}

			v->ntcs = hw_tcs - aprp_cpu_index();

			/* add the tc to the list of this vpe's tc's. */
			list_add(&t->tc, &v->tc);

			/* deactivate all but vpe0 */
			if (tc >= aprp_cpu_index()) {
				unsigned long tmp = read_vpe_c0_vpeconf0();

				tmp &= ~VPECONF0_VPA;

				/* master VPE */
				tmp |= VPECONF0_MVP;
				write_vpe_c0_vpeconf0(tmp);
			}

			/* disable multi-threading with TC's */
			write_vpe_c0_vpecontrol(read_vpe_c0_vpecontrol() &
						~VPECONTROL_TE);

			if (tc >= vpelimit) {
				/*
				 * Set config to be the same as vpe0,
				 * particularly kseg0 coherency alg
				 */
				write_vpe_c0_config(read_c0_config());
			}
		}

		/* TC's */
		t->pvpe = v;	/* set the parent vpe */

		if (tc >= aprp_cpu_index()) {
			unsigned long tmp;

			settc(tc);

			/*
			 * A TC that is bound to any other VPE gets bound to
			 * VPE0, ideally I'd like to make it homeless but it
			 * doesn't appear to let me bind a TC to a non-existent
			 * VPE. Which is perfectly reasonable.
			 *
			 * The (un)bound state is visible to an EJTAG probe so
			 * may notify GDB...
			 */
			tmp = read_tc_c0_tcbind();
			if (tmp & TCBIND_CURVPE) {
				/* tc is bound >vpe0 */
				write_tc_c0_tcbind(tmp & ~TCBIND_CURVPE);

				t->pvpe = get_vpe(0);	/* set the parent vpe */
			}

			/* halt the TC */
			write_tc_c0_tchalt(TCHALT_H);
			mips_ihb();

			tmp = read_tc_c0_tcstatus();

			/* mark not activated and not dynamically allocatable */
			tmp &= ~(TCSTATUS_A | TCSTATUS_DA);
			tmp |= TCSTATUS_IXMT;	/* interrupt exempt */
			write_tc_c0_tcstatus(tmp);
		}
	}

out_reenable:
	/* release config state */
	clear_c0_mvpcontrol(MVPCONTROL_VPC);

	evpe(vpflags);
	emt(mtflags);
	local_irq_restore(flags);

	return 0;

out_dev:
	device_del(&vpe_device);

out_class:
	class_unregister(&vpe_class);

out_chrdev:
	unregister_chrdev(major, VPE_MODULE_NAME);

	return err;
}

void __exit vpe_module_exit(void)
{
	struct vpe *v, *n;

	device_del(&vpe_device);
	class_unregister(&vpe_class);
	unregister_chrdev(major, VPE_MODULE_NAME);

	/* No locking needed here */
	list_for_each_entry_safe(v, n, &vpecontrol.vpe_list, list) {
		if (v->state != VPE_STATE_UNUSED)
			release_vpe(v);
	}
}
