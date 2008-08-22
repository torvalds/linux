#include <linux/notifier.h>

#include <xen/xenbus.h>

#include <asm-x86/xen/hypervisor.h>
#include <asm/cpu.h>

static void enable_hotplug_cpu(int cpu)
{
	if (!cpu_present(cpu))
		arch_register_cpu(cpu);

	cpu_set(cpu, cpu_present_map);
}

static void disable_hotplug_cpu(int cpu)
{
	if (cpu_present(cpu))
		arch_unregister_cpu(cpu);

	cpu_clear(cpu, cpu_present_map);
}

static void vcpu_hotplug(unsigned int cpu)
{
	int err;
	char dir[32], state[32];

	if (!cpu_possible(cpu))
		return;

	sprintf(dir, "cpu/%u", cpu);
	err = xenbus_scanf(XBT_NIL, dir, "availability", "%s", state);
	if (err != 1) {
		printk(KERN_ERR "XENBUS: Unable to read cpu state\n");
		return;
	}

	if (strcmp(state, "online") == 0) {
		enable_hotplug_cpu(cpu);
	} else if (strcmp(state, "offline") == 0) {
		(void)cpu_down(cpu);
		disable_hotplug_cpu(cpu);
	} else {
		printk(KERN_ERR "XENBUS: unknown state(%s) on CPU%d\n",
		       state, cpu);
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
	static struct xenbus_watch cpu_watch = {
		.node = "cpu",
		.callback = handle_vcpu_hotplug_event};

	(void)register_xenbus_watch(&cpu_watch);

	return NOTIFY_DONE;
}

static int __init setup_vcpu_hotplug_event(void)
{
	static struct notifier_block xsn_cpu = {
		.notifier_call = setup_cpu_watcher };

	if (!is_running_on_xen())
		return -ENODEV;

	register_xenstore_notifier(&xsn_cpu);

	return 0;
}

arch_initcall(setup_vcpu_hotplug_event);

