.. SPDX-License-Identifier: GPL-2.0

=======================
Energy Model of devices
=======================

1. Overview
-----------

The Energy Model (EM) framework serves as an interface between drivers knowing
the power consumed by devices at various performance levels, and the kernel
subsystems willing to use that information to make energy-aware decisions.

The source of the information about the power consumed by devices can vary greatly
from one platform to another. These power costs can be estimated using
devicetree data in some cases. In others, the firmware will know better.
Alternatively, userspace might be best positioned. And so on. In order to avoid
each and every client subsystem to re-implement support for each and every
possible source of information on its own, the EM framework intervenes as an
abstraction layer which standardizes the format of power cost tables in the
kernel, hence enabling to avoid redundant work.

The power values might be expressed in micro-Watts or in an 'abstract scale'.
Multiple subsystems might use the EM and it is up to the system integrator to
check that the requirements for the power value scale types are met. An example
can be found in the Energy-Aware Scheduler documentation
Documentation/scheduler/sched-energy.rst. For some subsystems like thermal or
powercap power values expressed in an 'abstract scale' might cause issues.
These subsystems are more interested in estimation of power used in the past,
thus the real micro-Watts might be needed. An example of these requirements can
be found in the Intelligent Power Allocation in
Documentation/driver-api/thermal/power_allocator.rst.
Kernel subsystems might implement automatic detection to check whether EM
registered devices have inconsistent scale (based on EM internal flag).
Important thing to keep in mind is that when the power values are expressed in
an 'abstract scale' deriving real energy in micro-Joules would not be possible.

The figure below depicts an example of drivers (Arm-specific here, but the
approach is applicable to any architecture) providing power costs to the EM
framework, and interested clients reading the data from it::

       +---------------+  +-----------------+  +---------------+
       | Thermal (IPA) |  | Scheduler (EAS) |  |     Other     |
       +---------------+  +-----------------+  +---------------+
               |                   | em_cpu_energy()   |
               |                   | em_cpu_get()      |
               +---------+         |         +---------+
                         |         |         |
                         v         v         v
                        +---------------------+
                        |    Energy Model     |
                        |     Framework       |
                        +---------------------+
                           ^       ^       ^
                           |       |       | em_dev_register_perf_domain()
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

In case of CPU devices the EM framework manages power cost tables per
'performance domain' in the system. A performance domain is a group of CPUs
whose performance is scaled together. Performance domains generally have a
1-to-1 mapping with CPUFreq policies. All CPUs in a performance domain are
required to have the same micro-architecture. CPUs in different performance
domains can have different micro-architectures.

To better reflect power variation due to static power (leakage) the EM
supports runtime modifications of the power values. The mechanism relies on
RCU to free the modifiable EM perf_state table memory. Its user, the task
scheduler, also uses RCU to access this memory. The EM framework provides
API for allocating/freeing the new memory for the modifiable EM table.
The old memory is freed automatically using RCU callback mechanism when there
are no owners anymore for the given EM runtime table instance. This is tracked
using kref mechanism. The device driver which provided the new EM at runtime,
should call EM API to free it safely when it's no longer needed. The EM
framework will handle the clean-up when it's possible.

The kernel code which want to modify the EM values is protected from concurrent
access using a mutex. Therefore, the device driver code must run in sleeping
context when it tries to modify the EM.

With the runtime modifiable EM we switch from a 'single and during the entire
runtime static EM' (system property) design to a 'single EM which can be
changed during runtime according e.g. to the workload' (system and workload
property) design.

It is possible also to modify the CPU performance values for each EM's
performance state. Thus, the full power and performance profile (which
is an exponential curve) can be changed according e.g. to the workload
or system property.


2. Core APIs
------------

2.1 Config options
^^^^^^^^^^^^^^^^^^

CONFIG_ENERGY_MODEL must be enabled to use the EM framework.


2.2 Registration of performance domains
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

Registration of 'advanced' EM
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The 'advanced' EM gets its name due to the fact that the driver is allowed
to provide more precised power model. It's not limited to some implemented math
formula in the framework (like it is in 'simple' EM case). It can better reflect
the real power measurements performed for each performance state. Thus, this
registration method should be preferred in case considering EM static power
(leakage) is important.

Drivers are expected to register performance domains into the EM framework by
calling the following API::

  int em_dev_register_perf_domain(struct device *dev, unsigned int nr_states,
		struct em_data_callback *cb, cpumask_t *cpus, bool microwatts);

Drivers must provide a callback function returning <frequency, power> tuples
for each performance state. The callback function provided by the driver is free
to fetch data from any relevant location (DT, firmware, ...), and by any mean
deemed necessary. Only for CPU devices, drivers must specify the CPUs of the
performance domains using cpumask. For other devices than CPUs the last
argument must be set to NULL.
The last argument 'microwatts' is important to set with correct value. Kernel
subsystems which use EM might rely on this flag to check if all EM devices use
the same scale. If there are different scales, these subsystems might decide
to return warning/error, stop working or panic.
See Section 3. for an example of driver implementing this
callback, or Section 2.4 for further documentation on this API

Registration of EM using DT
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

The  EM can also be registered using OPP framework and information in DT
"operating-points-v2". Each OPP entry in DT can be extended with a property
"opp-microwatt" containing micro-Watts power value. This OPP DT property
allows a platform to register EM power values which are reflecting total power
(static + dynamic). These power values might be coming directly from
experiments and measurements.

Registration of 'artificial' EM
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

There is an option to provide a custom callback for drivers missing detailed
knowledge about power value for each performance state. The callback
.get_cost() is optional and provides the 'cost' values used by the EAS.
This is useful for platforms that only provide information on relative
efficiency between CPU types, where one could use the information to
create an abstract power model. But even an abstract power model can
sometimes be hard to fit in, given the input power value size restrictions.
The .get_cost() allows to provide the 'cost' values which reflect the
efficiency of the CPUs. This would allow to provide EAS information which
has different relation than what would be forced by the EM internal
formulas calculating 'cost' values. To register an EM for such platform, the
driver must set the flag 'microwatts' to 0, provide .get_power() callback
and provide .get_cost() callback. The EM framework would handle such platform
properly during registration. A flag EM_PERF_DOMAIN_ARTIFICIAL is set for such
platform. Special care should be taken by other frameworks which are using EM
to test and treat this flag properly.

Registration of 'simple' EM
~~~~~~~~~~~~~~~~~~~~~~~~~~~

The 'simple' EM is registered using the framework helper function
cpufreq_register_em_with_opp(). It implements a power model which is tight to
math formula::

	Power = C * V^2 * f

The EM which is registered using this method might not reflect correctly the
physics of a real device, e.g. when static power (leakage) is important.


2.3 Accessing performance domains
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

There are two API functions which provide the access to the energy model:
em_cpu_get() which takes CPU id as an argument and em_pd_get() with device
pointer as an argument. It depends on the subsystem which interface it is
going to use, but in case of CPU devices both functions return the same
performance domain.

Subsystems interested in the energy model of a CPU can retrieve it using the
em_cpu_get() API. The energy model tables are allocated once upon creation of
the performance domains, and kept in memory untouched.

The energy consumed by a performance domain can be estimated using the
em_cpu_energy() API. The estimation is performed assuming that the schedutil
CPUfreq governor is in use in case of CPU device. Currently this calculation is
not provided for other type of devices.

More details about the above APIs can be found in ``<linux/energy_model.h>``
or in Section 2.5


2.4 Runtime modifications
^^^^^^^^^^^^^^^^^^^^^^^^^

Drivers willing to update the EM at runtime should use the following dedicated
function to allocate a new instance of the modified EM. The API is listed
below::

  struct em_perf_table __rcu *em_table_alloc(struct em_perf_domain *pd);

This allows to allocate a structure which contains the new EM table with
also RCU and kref needed by the EM framework. The 'struct em_perf_table'
contains array 'struct em_perf_state state[]' which is a list of performance
states in ascending order. That list must be populated by the device driver
which wants to update the EM. The list of frequencies can be taken from
existing EM (created during boot). The content in the 'struct em_perf_state'
must be populated by the driver as well.

This is the API which does the EM update, using RCU pointers swap::

  int em_dev_update_perf_domain(struct device *dev,
			struct em_perf_table __rcu *new_table);

Drivers must provide a pointer to the allocated and initialized new EM
'struct em_perf_table'. That new EM will be safely used inside the EM framework
and will be visible to other sub-systems in the kernel (thermal, powercap).
The main design goal for this API is to be fast and avoid extra calculations
or memory allocations at runtime. When pre-computed EMs are available in the
device driver, than it should be possible to simply re-use them with low
performance overhead.

In order to free the EM, provided earlier by the driver (e.g. when the module
is unloaded), there is a need to call the API::

  void em_table_free(struct em_perf_table __rcu *table);

It will allow the EM framework to safely remove the memory, when there is
no other sub-system using it, e.g. EAS.

To use the power values in other sub-systems (like thermal, powercap) there is
a need to call API which protects the reader and provide consistency of the EM
table data::

  struct em_perf_state *em_perf_state_from_pd(struct em_perf_domain *pd);

It returns the 'struct em_perf_state' pointer which is an array of performance
states in ascending order.
This function must be called in the RCU read lock section (after the
rcu_read_lock()). When the EM table is not needed anymore there is a need to
call rcu_real_unlock(). In this way the EM safely uses the RCU read section
and protects the users. It also allows the EM framework to manage the memory
and free it. More details how to use it can be found in Section 3.2 in the
example driver.

There is dedicated API for device drivers to calculate em_perf_state::cost
values::

  int em_dev_compute_costs(struct device *dev, struct em_perf_state *table,
                           int nr_states);

These 'cost' values from EM are used in EAS. The new EM table should be passed
together with the number of entries and device pointer. When the computation
of the cost values is done properly the return value from the function is 0.
The function takes care for right setting of inefficiency for each performance
state as well. It updates em_perf_state::flags accordingly.
Then such prepared new EM can be passed to the em_dev_update_perf_domain()
function, which will allow to use it.

More details about the above APIs can be found in ``<linux/energy_model.h>``
or in Section 3.2 with an example code showing simple implementation of the
updating mechanism in a device driver.


2.5 Description details of this API
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. kernel-doc:: include/linux/energy_model.h
   :internal:

.. kernel-doc:: kernel/power/energy_model.c
   :export:


3. Examples
-----------

3.1 Example driver with EM registration
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

The CPUFreq framework supports dedicated callback for registering
the EM for a given CPU(s) 'policy' object: cpufreq_driver::register_em().
That callback has to be implemented properly for a given driver,
because the framework would call it at the right time during setup.
This section provides a simple example of a CPUFreq driver registering a
performance domain in the Energy Model framework using the (fake) 'foo'
protocol. The driver implements an est_power() function to be provided to the
EM framework::

  -> drivers/cpufreq/foo_cpufreq.c

  01	static int est_power(struct device *dev, unsigned long *mW,
  02			unsigned long *KHz)
  03	{
  04		long freq, power;
  05
  06		/* Use the 'foo' protocol to ceil the frequency */
  07		freq = foo_get_freq_ceil(dev, *KHz);
  08		if (freq < 0);
  09			return freq;
  10
  11		/* Estimate the power cost for the dev at the relevant freq. */
  12		power = foo_estimate_power(dev, freq);
  13		if (power < 0);
  14			return power;
  15
  16		/* Return the values to the EM framework */
  17		*mW = power;
  18		*KHz = freq;
  19
  20		return 0;
  21	}
  22
  23	static void foo_cpufreq_register_em(struct cpufreq_policy *policy)
  24	{
  25		struct em_data_callback em_cb = EM_DATA_CB(est_power);
  26		struct device *cpu_dev;
  27		int nr_opp;
  28
  29		cpu_dev = get_cpu_device(cpumask_first(policy->cpus));
  30
  31     	/* Find the number of OPPs for this policy */
  32     	nr_opp = foo_get_nr_opp(policy);
  33
  34     	/* And register the new performance domain */
  35     	em_dev_register_perf_domain(cpu_dev, nr_opp, &em_cb, policy->cpus,
  36					    true);
  37	}
  38
  39	static struct cpufreq_driver foo_cpufreq_driver = {
  40		.register_em = foo_cpufreq_register_em,
  41	};


3.2 Example driver with EM modification
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^

This section provides a simple example of a thermal driver modifying the EM.
The driver implements a foo_thermal_em_update() function. The driver is woken
up periodically to check the temperature and modify the EM data::

  -> drivers/soc/example/example_em_mod.c

  01	static void foo_get_new_em(struct foo_context *ctx)
  02	{
  03		struct em_perf_table __rcu *em_table;
  04		struct em_perf_state *table, *new_table;
  05		struct device *dev = ctx->dev;
  06		struct em_perf_domain *pd;
  07		unsigned long freq;
  08		int i, ret;
  09
  10		pd = em_pd_get(dev);
  11		if (!pd)
  12			return;
  13
  14		em_table = em_table_alloc(pd);
  15		if (!em_table)
  16			return;
  17
  18		new_table = em_table->state;
  19
  20		rcu_read_lock();
  21		table = em_perf_state_from_pd(pd);
  22		for (i = 0; i < pd->nr_perf_states; i++) {
  23			freq = table[i].frequency;
  24			foo_get_power_perf_values(dev, freq, &new_table[i]);
  25		}
  26		rcu_read_unlock();
  27
  28		/* Calculate 'cost' values for EAS */
  29		ret = em_dev_compute_costs(dev, table, pd->nr_perf_states);
  30		if (ret) {
  31			dev_warn(dev, "EM: compute costs failed %d\n", ret);
  32			em_free_table(em_table);
  33			return;
  34		}
  35
  36		ret = em_dev_update_perf_domain(dev, em_table);
  37		if (ret) {
  38			dev_warn(dev, "EM: update failed %d\n", ret);
  39			em_free_table(em_table);
  40			return;
  41		}
  42
  43		/*
  44		 * Since it's one-time-update drop the usage counter.
  45		 * The EM framework will later free the table when needed.
  46		 */
  47		em_table_free(em_table);
  48	}
  49
  50	/*
  51	 * Function called periodically to check the temperature and
  52	 * update the EM if needed
  53	 */
  54	static void foo_thermal_em_update(struct foo_context *ctx)
  55	{
  56		struct device *dev = ctx->dev;
  57		int cpu;
  58
  59		ctx->temperature = foo_get_temp(dev, ctx);
  60		if (ctx->temperature < FOO_EM_UPDATE_TEMP_THRESHOLD)
  61			return;
  62
  63		foo_get_new_em(ctx);
  64	}
