#define pr_fmt(fmt) "xen:" KBUILD_MODNAME ": " fmt

#include <linux/notifier.h>

#include <xen/xen.h>
#include <xen/xenbus.h>

#include <asm/xen/hypervisor.h>
#include <asm/cpu.h>

static void enable_hotplug_cpu(int cpu)
{
	if (!cpu_present(cpu))
		xen_arch_register_cpu(cpu);

	set_cpu_present(cpu, true);
}

static void disable_hotplug_cpu(int cpu)
{
	if (cpu_online(cpu)) {
		lock_device_hotplug();
		device_offline(get_cpu_device(cpu));
		unlock_device_hotplug();
	}
	if (cpu_present(cpu))
		xen_arch_unregister_cpu(cpu);

	set_cpu_present(cpu, false);
}

static int vcpu_online(unsigned int cpu)
{
	int err;
	char dir[16], state[16];

	sprintf(dir, "cpu/%u", cpu);
	err = xenbus_scanf(XBT_NIL, dir, "availability", "%15s", state);
	if (err != 1) {
		if (!xen_initial_domain())
			pr_err("Unable to read cpu state\n");
		return err;
	}

	if (strcmp(state, "online") == 0)
		return 1;
	else if (strcmp(state, "offline") == 0)
		return 0;

	pr_err("unknown state(%s) on CPU%d\n", state, cpu);
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
			set_cpu_present(cpu, false);
		}
	}

	return NOTIFY_DONE;
}

static int __init setup_vcpu_hotplug_event(void)
{
	static struct notifier_block xsn_cpu = {
		.notifier_call = setup_cpu_watcher };

#ifdef CONFIG_X86
	if (!xen_pv_domain())
#else
	if (!xen_domain())
#endif
		return -ENODEV;

	register_xenstore_notifier(&xsn_cpu);

	return 0;
}

arch_initcall(setup_vcpu_hotplug_event);

