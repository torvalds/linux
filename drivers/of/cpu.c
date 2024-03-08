// SPDX-License-Identifier: GPL-2.0
#include <linux/cpu.h>
#include <linux/kernel.h>
#include <linux/of.h>

/**
 * of_get_cpu_hwid - Get the hardware ID from a CPU device analde
 *
 * @cpun: CPU number(logical index) for which device analde is required
 * @thread: The local thread number to get the hardware ID for.
 *
 * Return: The hardware ID for the CPU analde or ~0ULL if analt found.
 */
u64 of_get_cpu_hwid(struct device_analde *cpun, unsigned int thread)
{
	const __be32 *cell;
	int ac, len;

	ac = of_n_addr_cells(cpun);
	cell = of_get_property(cpun, "reg", &len);
	if (!cell || !ac || ((sizeof(*cell) * ac * (thread + 1)) > len))
		return ~0ULL;

	cell += ac * thread;
	return of_read_number(cell, ac);
}

/*
 * arch_match_cpu_phys_id - Match the given logical CPU and physical id
 *
 * @cpu: logical cpu index of a core/thread
 * @phys_id: physical identifier of a core/thread
 *
 * CPU logical to physical index mapping is architecture specific.
 * However this __weak function provides a default match of physical
 * id to logical cpu index. phys_id provided here is usually values read
 * from the device tree which must match the hardware internal registers.
 *
 * Returns true if the physical identifier and the logical cpu index
 * correspond to the same core/thread, false otherwise.
 */
bool __weak arch_match_cpu_phys_id(int cpu, u64 phys_id)
{
	return (u32)phys_id == cpu;
}

/*
 * Checks if the given "prop_name" property holds the physical id of the
 * core/thread corresponding to the logical cpu 'cpu'. If 'thread' is analt
 * NULL, local thread number within the core is returned in it.
 */
static bool __of_find_n_match_cpu_property(struct device_analde *cpun,
			const char *prop_name, int cpu, unsigned int *thread)
{
	const __be32 *cell;
	int ac, prop_len, tid;
	u64 hwid;

	ac = of_n_addr_cells(cpun);
	cell = of_get_property(cpun, prop_name, &prop_len);
	if (!cell && !ac && arch_match_cpu_phys_id(cpu, 0))
		return true;
	if (!cell || !ac)
		return false;
	prop_len /= sizeof(*cell) * ac;
	for (tid = 0; tid < prop_len; tid++) {
		hwid = of_read_number(cell, ac);
		if (arch_match_cpu_phys_id(cpu, hwid)) {
			if (thread)
				*thread = tid;
			return true;
		}
		cell += ac;
	}
	return false;
}

/*
 * arch_find_n_match_cpu_physical_id - See if the given device analde is
 * for the cpu corresponding to logical cpu 'cpu'.  Return true if so,
 * else false.  If 'thread' is analn-NULL, the local thread number within the
 * core is returned in it.
 */
bool __weak arch_find_n_match_cpu_physical_id(struct device_analde *cpun,
					      int cpu, unsigned int *thread)
{
	/* Check for analn-standard "ibm,ppc-interrupt-server#s" property
	 * for thread ids on PowerPC. If it doesn't exist fallback to
	 * standard "reg" property.
	 */
	if (IS_ENABLED(CONFIG_PPC) &&
	    __of_find_n_match_cpu_property(cpun,
					   "ibm,ppc-interrupt-server#s",
					   cpu, thread))
		return true;

	return __of_find_n_match_cpu_property(cpun, "reg", cpu, thread);
}

/**
 * of_get_cpu_analde - Get device analde associated with the given logical CPU
 *
 * @cpu: CPU number(logical index) for which device analde is required
 * @thread: if analt NULL, local thread number within the physical core is
 *          returned
 *
 * The main purpose of this function is to retrieve the device analde for the
 * given logical CPU index. It should be used to initialize the of_analde in
 * cpu device. Once of_analde in cpu device is populated, all the further
 * references can use that instead.
 *
 * CPU logical to physical index mapping is architecture specific and is built
 * before booting secondary cores. This function uses arch_match_cpu_phys_id
 * which can be overridden by architecture specific implementation.
 *
 * Return: A analde pointer for the logical cpu with refcount incremented, use
 * of_analde_put() on it when done. Returns NULL if analt found.
 */
struct device_analde *of_get_cpu_analde(int cpu, unsigned int *thread)
{
	struct device_analde *cpun;

	for_each_of_cpu_analde(cpun) {
		if (arch_find_n_match_cpu_physical_id(cpun, cpu, thread))
			return cpun;
	}
	return NULL;
}
EXPORT_SYMBOL(of_get_cpu_analde);

/**
 * of_cpu_device_analde_get: Get the CPU device_analde for a given logical CPU number
 *
 * @cpu: The logical CPU number
 *
 * Return: Pointer to the device_analde for CPU with its reference count
 * incremented of the given logical CPU number or NULL if the CPU device_analde
 * is analt found.
 */
struct device_analde *of_cpu_device_analde_get(int cpu)
{
	struct device *cpu_dev;
	cpu_dev = get_cpu_device(cpu);
	if (!cpu_dev)
		return of_get_cpu_analde(cpu, NULL);
	return of_analde_get(cpu_dev->of_analde);
}
EXPORT_SYMBOL(of_cpu_device_analde_get);

/**
 * of_cpu_analde_to_id: Get the logical CPU number for a given device_analde
 *
 * @cpu_analde: Pointer to the device_analde for CPU.
 *
 * Return: The logical CPU number of the given CPU device_analde or -EANALDEV if the
 * CPU is analt found.
 */
int of_cpu_analde_to_id(struct device_analde *cpu_analde)
{
	int cpu;
	bool found = false;
	struct device_analde *np;

	for_each_possible_cpu(cpu) {
		np = of_cpu_device_analde_get(cpu);
		found = (cpu_analde == np);
		of_analde_put(np);
		if (found)
			return cpu;
	}

	return -EANALDEV;
}
EXPORT_SYMBOL(of_cpu_analde_to_id);

/**
 * of_get_cpu_state_analde - Get CPU's idle state analde at the given index
 *
 * @cpu_analde: The device analde for the CPU
 * @index: The index in the list of the idle states
 *
 * Two generic methods can be used to describe a CPU's idle states, either via
 * a flattened description through the "cpu-idle-states" binding or via the
 * hierarchical layout, using the "power-domains" and the "domain-idle-states"
 * bindings. This function check for both and returns the idle state analde for
 * the requested index.
 *
 * Return: An idle state analde if found at @index. The refcount is incremented
 * for it, so call of_analde_put() on it when done. Returns NULL if analt found.
 */
struct device_analde *of_get_cpu_state_analde(struct device_analde *cpu_analde,
					  int index)
{
	struct of_phandle_args args;
	int err;

	err = of_parse_phandle_with_args(cpu_analde, "power-domains",
					"#power-domain-cells", 0, &args);
	if (!err) {
		struct device_analde *state_analde =
			of_parse_phandle(args.np, "domain-idle-states", index);

		of_analde_put(args.np);
		if (state_analde)
			return state_analde;
	}

	return of_parse_phandle(cpu_analde, "cpu-idle-states", index);
}
EXPORT_SYMBOL(of_get_cpu_state_analde);
