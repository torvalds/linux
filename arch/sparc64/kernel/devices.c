/* devices.c: Initial scan of the prom device tree for important
 *            Sparc device nodes which we need to find.
 *
 * Copyright (C) 1996 David S. Miller (davem@caip.rutgers.edu)
 */

#include <linux/config.h>
#include <linux/kernel.h>
#include <linux/threads.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/string.h>
#include <linux/spinlock.h>
#include <linux/errno.h>
#include <linux/bootmem.h>

#include <asm/page.h>
#include <asm/oplib.h>
#include <asm/system.h>
#include <asm/smp.h>
#include <asm/spitfire.h>
#include <asm/timer.h>
#include <asm/cpudata.h>
#include <asm/vdev.h>
#include <asm/irq.h>

/* Used to synchronize acceses to NatSemi SUPER I/O chip configure
 * operations in asm/ns87303.h
 */
DEFINE_SPINLOCK(ns87303_lock);

extern void cpu_probe(void);
extern void central_probe(void);

u32 sun4v_vdev_devhandle;
struct device_node *sun4v_vdev_root;

struct vdev_intmap {
	unsigned int phys;
	unsigned int irq;
	unsigned int cnode;
	unsigned int cinterrupt;
};

struct vdev_intmask {
	unsigned int phys;
	unsigned int interrupt;
	unsigned int __unused;
};

static struct vdev_intmap *vdev_intmap;
static int vdev_num_intmap;
static struct vdev_intmask *vdev_intmask;

static void __init sun4v_virtual_device_probe(void)
{
	struct linux_prom64_registers *regs;
	struct property *prop;
	struct device_node *dp;
	int sz;

	if (tlb_type != hypervisor)
		return;

	dp = of_find_node_by_name(NULL, "virtual-devices");
	if (!dp) {
		prom_printf("SUN4V: Fatal error, no virtual-devices node.\n");
		prom_halt();
	}

	sun4v_vdev_root = dp;

	prop = of_find_property(dp, "reg", NULL);
	regs = prop->value;
	sun4v_vdev_devhandle = (regs[0].phys_addr >> 32UL) & 0x0fffffff;

	prop = of_find_property(dp, "interrupt-map", &sz);
	vdev_intmap = prop->value;
	vdev_num_intmap = sz / sizeof(struct vdev_intmap);

	prop = of_find_property(dp, "interrupt-map-mask", NULL);
	vdev_intmask = prop->value;

	printk("%s: Virtual Device Bus devhandle[%x]\n",
	       dp->full_name, sun4v_vdev_devhandle);
}

unsigned int sun4v_vdev_device_interrupt(struct device_node *dev_node)
{
	struct property *prop;
	unsigned int irq, reg;
	int i;

	prop = of_find_property(dev_node, "interrupts", NULL);
	if (!prop) {
		printk("VDEV: Cannot get \"interrupts\" "
		       "property for OBP node %s\n",
		       dev_node->full_name);
		return 0;
	}
	irq = *(unsigned int *) prop->value;

	prop = of_find_property(dev_node, "reg", NULL);
	if (!prop) {
		printk("VDEV: Cannot get \"reg\" "
		       "property for OBP node %s\n",
		       dev_node->full_name);
		return 0;
	}
	reg = *(unsigned int *) prop->value;

	for (i = 0; i < vdev_num_intmap; i++) {
		if (vdev_intmap[i].phys == (reg & vdev_intmask->phys) &&
		    vdev_intmap[i].irq == (irq & vdev_intmask->interrupt)) {
			irq = vdev_intmap[i].cinterrupt;
			break;
		}
	}

	if (i == vdev_num_intmap) {
		printk("VDEV: No matching interrupt map entry "
		       "for OBP node %s\n", dev_node->full_name);
		return 0;
	}

	return sun4v_build_irq(sun4v_vdev_devhandle, irq);
}

static const char *cpu_mid_prop(void)
{
	if (tlb_type == spitfire)
		return "upa-portid";
	return "portid";
}

static int get_cpu_mid(int prom_node)
{
	if (tlb_type == hypervisor) {
		struct linux_prom64_registers reg;

		if (prom_getproplen(prom_node, "cpuid") == 4)
			return prom_getintdefault(prom_node, "cpuid", 0);

		prom_getproperty(prom_node, "reg", (char *) &reg, sizeof(reg));
		return (reg.phys_addr >> 32) & 0x0fffffffUL;
	} else {
		const char *prop_name = cpu_mid_prop();

		return prom_getintdefault(prom_node, prop_name, 0);
	}
}

static int check_cpu_node(int nd, int *cur_inst,
			  int (*compare)(int, int, void *), void *compare_arg,
			  int *prom_node, int *mid)
{
	char node_str[128];

	prom_getstring(nd, "device_type", node_str, sizeof(node_str));
	if (strcmp(node_str, "cpu"))
		return -ENODEV;

	if (!compare(nd, *cur_inst, compare_arg)) {
		if (prom_node)
			*prom_node = nd;
		if (mid)
			*mid = get_cpu_mid(nd);
		return 0;
	}

	(*cur_inst)++;

	return -ENODEV;
}

static int __cpu_find_by(int (*compare)(int, int, void *), void *compare_arg,
			 int *prom_node, int *mid)
{
	int nd, cur_inst, err;

	nd = prom_root_node;
	cur_inst = 0;

	err = check_cpu_node(nd, &cur_inst,
			     compare, compare_arg,
			     prom_node, mid);
	if (err == 0)
		return 0;

	nd = prom_getchild(nd);
	while ((nd = prom_getsibling(nd)) != 0) {
		err = check_cpu_node(nd, &cur_inst,
				     compare, compare_arg,
				     prom_node, mid);
		if (err == 0)
			return 0;
	}

	return -ENODEV;
}

static int cpu_instance_compare(int nd, int instance, void *_arg)
{
	int desired_instance = (int) (long) _arg;

	if (instance == desired_instance)
		return 0;
	return -ENODEV;
}

int cpu_find_by_instance(int instance, int *prom_node, int *mid)
{
	return __cpu_find_by(cpu_instance_compare, (void *)(long)instance,
			     prom_node, mid);
}

static int cpu_mid_compare(int nd, int instance, void *_arg)
{
	int desired_mid = (int) (long) _arg;
	int this_mid;

	this_mid = get_cpu_mid(nd);
	if (this_mid == desired_mid)
		return 0;
	return -ENODEV;
}

int cpu_find_by_mid(int mid, int *prom_node)
{
	return __cpu_find_by(cpu_mid_compare, (void *)(long)mid,
			     prom_node, NULL);
}

void __init device_scan(void)
{
	/* FIX ME FAST... -DaveM */
	ioport_resource.end = 0xffffffffffffffffUL;

	prom_printf("Booting Linux...\n");

#ifndef CONFIG_SMP
	{
		int err, cpu_node, def;

		err = cpu_find_by_instance(0, &cpu_node, NULL);
		if (err) {
			prom_printf("No cpu nodes, cannot continue\n");
			prom_halt();
		}
		cpu_data(0).clock_tick = prom_getintdefault(cpu_node,
							    "clock-frequency",
							    0);

		def = ((tlb_type == hypervisor) ?
		       (8 * 1024) :
		       (16 * 1024));
		cpu_data(0).dcache_size = prom_getintdefault(cpu_node,
							     "dcache-size",
							     def);

		def = 32;
		cpu_data(0).dcache_line_size =
			prom_getintdefault(cpu_node, "dcache-line-size",
					   def);

		def = 16 * 1024;
		cpu_data(0).icache_size = prom_getintdefault(cpu_node,
							     "icache-size",
							     def);

		def = 32;
		cpu_data(0).icache_line_size =
			prom_getintdefault(cpu_node, "icache-line-size",
					   def);

		def = ((tlb_type == hypervisor) ?
		       (3 * 1024 * 1024) :
		       (4 * 1024 * 1024));
		cpu_data(0).ecache_size = prom_getintdefault(cpu_node,
							     "ecache-size",
							     def);

		def = 64;
		cpu_data(0).ecache_line_size =
			prom_getintdefault(cpu_node, "ecache-line-size",
					   def);
		printk("CPU[0]: Caches "
		       "D[sz(%d):line_sz(%d)] "
		       "I[sz(%d):line_sz(%d)] "
		       "E[sz(%d):line_sz(%d)]\n",
		       cpu_data(0).dcache_size, cpu_data(0).dcache_line_size,
		       cpu_data(0).icache_size, cpu_data(0).icache_line_size,
		       cpu_data(0).ecache_size, cpu_data(0).ecache_line_size);
	}
#endif

	sun4v_virtual_device_probe();
	central_probe();

	cpu_probe();
}
