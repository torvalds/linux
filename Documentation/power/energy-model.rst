====================
Energy Model of CPUs
====================

1. Overview
-----------

The Energy Model (EM) framework serves as an interface between drivers knowing
the power consumed by CPUs at various performance levels, and the kernel
subsystems willing to use that information to make energy-aware decisions.

The source of the information about the power consumed by CPUs can vary greatly
from one platform to another. These power costs can be estimated using
devicetree data in some cases. In others, the firmware will know better.
Alternatively, userspace might be best positioned. And so on. In order to avoid
each and every client subsystem to re-implement support for each and every
possible source of information on its own, the EM framework intervenes as an
abstraction layer which standardizes the format of power cost tables in the
kernel, hence enabling to avoid redundant work.

The figure below depicts an example of drivers (Arm-specific here, but the
approach is applicable to any architecture) providing power costs to the EM
framework, and interested clients reading the data from it::

       +---------------+  +-----------------+  +---------------+
       | Thermal (IPA) |  | Scheduler (EAS) |  |     Other     |
       +---------------+  +-----------------+  +---------------+
               |                   | em_pd_energy()    |
               |                   | em_cpu_get()      |
               +---------+         |         +---------+
                         |         |         |
                         v         v         v
                        +---------------------+
                        |    Energy Model     |
                        |     Framework       |
                        +---------------------+
                           ^       ^       ^
                           |       |       | em_register_perf_domain()
                +----------+       |       +---------+
                |                  |                 |
        +---------------+  +---------------+  +--------------+
        |  cpufreq-dt   |  |   arm_scmi    |  |    Other     |
        +---------------+  +---------------+  +--------------+
                ^                  ^                 ^
                |                  |                 |
        +--------------+   +---------------+  +--------------+
        | Device Tree  |   |   Firmware    |  |      ?       |
        +--------------+   +---------------+  +--------------+

The EM framework manages power cost tables per 'performance domain' in the
system. A performance domain is a group of CPUs whose performance is scaled
together. Performance domains generally have a 1-to-1 mapping with CPUFreq
policies. All CPUs in a performance domain are required to have the same
micro-architecture. CPUs in different performance domains can have different
micro-architectures.


2. Core APIs
------------

2.1 Config options
^^^^^^^^^^^^^^^^^^

CONFIG_ENERGY_MODEL must be enabled to use the EM framework.


2.2 Registration of performance domains
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Drivers are expected to register performance domains into the EM framework by
calling the following API::

  int em_register_perf_domain(cpumask_t *span, unsigned int nr_states,
			      struct em_data_callback *cb);

Drivers must specify the CPUs of the performance domains using the cpumask
argument, and provide a callback function returning <frequency, power> tuples
for each capacity state. The callback function provided by the driver is free
to fetch data from any relevant location (DT, firmware, ...), and by any mean
deemed necessary. See Section 3. for an example of driver implementing this
callback, and kernel/power/energy_model.c for further documentation on this
API.


2.3 Accessing performance domains
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Subsystems interested in the energy model of a CPU can retrieve it using the
em_cpu_get() API. The energy model tables are allocated once upon creation of
the performance domains, and kept in memory untouched.

The energy consumed by a performance domain can be estimated using the
em_pd_energy() API. The estimation is performed assuming that the schedutil
CPUfreq governor is in use.

More details about the above APIs can be found in include/linux/energy_model.h.


3. Example driver
-----------------

This section provides a simple example of a CPUFreq driver registering a
performance domain in the Energy Model framework using the (fake) 'foo'
protocol. The driver implements an est_power() function to be provided to the
EM framework::

  -> drivers/cpufreq/foo_cpufreq.c

  01	static int est_power(unsigned long *mW, unsigned long *KHz, int cpu)
  02	{
  03		long freq, power;
  04
  05		/* Use the 'foo' protocol to ceil the frequency */
  06		freq = foo_get_freq_ceil(cpu, *KHz);
  07		if (freq < 0);
  08			return freq;
  09
  10		/* Estimate the power cost for the CPU at the relevant freq. */
  11		power = foo_estimate_power(cpu, freq);
  12		if (power < 0);
  13			return power;
  14
  15		/* Return the values to the EM framework */
  16		*mW = power;
  17		*KHz = freq;
  18
  19		return 0;
  20	}
  21
  22	static int foo_cpufreq_init(struct cpufreq_policy *policy)
  23	{
  24		struct em_data_callback em_cb = EM_DATA_CB(est_power);
  25		int nr_opp, ret;
  26
  27		/* Do the actual CPUFreq init work ... */
  28		ret = do_foo_cpufreq_init(policy);
  29		if (ret)
  30			return ret;
  31
  32		/* Find the number of OPPs for this policy */
  33		nr_opp = foo_get_nr_opp(policy);
  34
  35		/* And register the new performance domain */
  36		em_register_perf_domain(policy->cpus, nr_opp, &em_cb);
  37
  38	        return 0;
  39	}
