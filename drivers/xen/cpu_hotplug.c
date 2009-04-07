#include <linux/notifier.h>

#include <xen/xenbus.h>

#include <asm/xen/hypervisor.h>
#include <asm/cpu.h>

static void enable_hotplug_cpu(int cpu)
{
	if (!cpu_present(cpu))
		arch_register_cpu(cpu);

	set_cpu_present(cpu, true);
}

static void disable_hotplug_cpu(int cpu)
{
	if (cpu_present(cpu))
		arch_unregister_cpu(cpu);

	set_cpu_present(cpu, false);
}

static int vcpu_online(unsigned int cpu)
{
	int err;
	char dir[32], state[32];

	sprintf(dir, "cpu/%u", cpu);
	err = xenbus_scanf(XBT_NIL, dir, "availability", "%s", state);
	if (err != 1) {
		printk(KERN_ERR "XENBUS: Unable to read cpu state\n");
		return err;
	}

	if (strcmp(state, "online") == 0)
		return 1;
	else if (strcmp(state, "offline") == 0)
		return 0;

	printk(KERN_ERR "XENBUS: unknown state(%s) on CPU%d\n", state, cpu);
	return -EINVAL;
}
static void vcpu_hotplug(unsigned int cpu)
{
	if (!cpu_possible(cpu))
		return;

	switch (vcpu_online(cpu)) {
	case 1:
		enable_hotplug_cpu(cpu);
		break;
	case 0:
		(void)cpu_down(cpu);
		disable_hotplug_cpu(cpu);
		break;
	default:
		break;
	}
}

static void handle_vcpu_hotplug_event(struct xenbus_watch *watch,
					const char **vec, unsigned int len)
{
	unsigned int cpu;
	char *cpustr;
	const char *node = vec[XS_WATCH_PATH];

	cpustr = strstr(node, "cpu/");
	if (cpustr != NULL) {
		sscanf(cpustr, "cpu/%u", &cpu);
		vcpu_hotplug(cpu);
	}
}

static int setup_cpu_watcher(struct notifier_block *notifier,
			      unsigned long event, void *data)
{
	int cpu;
	static struct xenbus_watch cpu_watch = {
		.node = "cpu",
		.callback = handle_vcpu_hotplug_event};

	(void)register_xenbus_watch(&cpu_watch);

	for_each_possible_cpu(cpu) {
		if (vcpu_online(cpu) == 0) {
			(void)cpu_down(cpu);
			cpu_clear(cpu, cpu_present_map);
		}
	}

	return NOTIFY_DONE;
}

static int __init setup_vcpu_hotplug_event(void)
{
	static struct notifier_block xsn_cpu = {
		.notifier_call = setup_cpu_watcher };

	if (!xen_pv_domain())
		return -ENODEV;

	register_xenstore_notifier(&xsn_cpu);

	return 0;
}

arch_initcall(setup_vcpu_hotplug_event);

