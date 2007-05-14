#include <linux/cpu.h>
#include <linux/cpumask.h>
#include <linux/init.h>
#include <linux/percpu.h>

static DEFINE_PER_CPU(struct cpu, cpu_devices);

static int __init topology_init(void)
{
	int i, ret;

	for_each_present_cpu(i) {
		ret = register_cpu(&per_cpu(cpu_devices, i), i);
		if (unlikely(ret))
			printk(KERN_WARNING "%s: register_cpu %d failed (%d)\n",
			       __FUNCTION__, i, ret);
	}

	return 0;
}
subsys_initcall(topology_init);
