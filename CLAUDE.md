We are editing the Linux Kernel. The checked out version was mainline around 6.15.6.

Our goal is to add support to the resctrl subsystem for reading cache occupancy using perf counters.

The main implementation of resctrl is in:
- arch/x86/include/asm/resctrl.h 
- include/linux/resctrl_types.h
- include/linux/resctrl.h
- arch/x86/kernel/cpu/resctrl/**
- arch/x86/kernel/cpu/amd.c
- arch/x86/kernel/cpu/intel.c
- fs/resctrl/**

Tests in:
- tools/testing/selftests/resctrl/

Documentation in:
- Documentation/filesystems/resctrl.rst
- tools/testing/selftests/resctrl/README


----
# Understanding the Perf Subsystem in the Linux Kernel

The perf_event_open System Call
-------------------------------

The [perf_event_open](https://elixir.bootlin.com/linux/v6.15.6/source/kernel/events/core.c#L13121) system call is defined in `kernel/events/core.c`. You can check the [man page](https://man7.org/linux/man-pages/man2/perf_event_open.2.html) for detailed documentation.

The caller can supply a process, a CPU, and a cgroup ID for the entities they want to measure. **The cgroup and process ID share the same input parameter, with the flag `PERF_FLAG_PID_CGROUP` controlling whether the parameter refers to a cgroup or a pid.** The implementation handles locating these resources - for a process, it finds the task struct; for a CPU, it verifies the CPU is online; and for a cgroup, it passes the file descriptor to the cgroup.

Group Leaders in Perf
---------------------

Since there's significant code dealing with group leaders, it's worth understanding how they work. The user passes the parameter `group_fd` to perf_event_open. **Groups allow scheduling of events onto the PMU (Performance Monitoring Unit) hardware as a group - all or none.** This matters because many PMUs have limits on the number of events they can track simultaneously, so the kernel time-multiplexes these groups onto PMUs.

A perf event can optionally have a group leader specified, but a group leader cannot have another leader. This creates a limited hierarchy - not a tree structure, just leaders with events under them. The perf_event_open handles most of this for PMU implementers. It resolves the group leader if specified and ensures all events in the group belong to a single hardware PMU. **The perf subsystem doesn't allow events from multiple PMUs in the same group because it's hard to schedule events across multiple PMUs atomically.**

Software events can be added to a hardware PMU group - this is allowed. When you add the first hardware event to a group that previously only had software events, there's logic to move all the events to the hardware PMU.

Key Data Structures
-------------------

Two important data structures link the perf_event struct to the PMU performing measurements and to the entity being monitored:

-   **[perf_event_context](https://elixir.bootlin.com/linux/v6.15.6/source/include/linux/perf_event.h#L945)** - associated with the measured entity (task or CPU)
-   **[perf_event_pmu_context](https://elixir.bootlin.com/linux/v6.15.6/source/include/linux/perf_event.h#L906)** - associated with a perf_event_context and the PMU

Multiple perf events can point to the same perf_event_context and perf_event_pmu_context. The perf_event holds a reference count on both structs it refers to. You can find documentation for `struct perf_event_pmu_context` in `include/linux/perf_event.h`.

Event Allocation and PMU Lookup
-------------------------------

The [perf_event_alloc](https://elixir.bootlin.com/linux/v6.15.6/source/kernel/events/core.c#L12598) function (called from perf_event_open) allocates and initializes the struct perf_event. It calls [perf_init_event](https://elixir.bootlin.com/linux/v6.15.6/source/kernel/events/core.c#L12413), which returns the PMU for the event. **This is where the lookup happens from the event type to the PMU associated with that event type.**

The `perf_init_event` function includes functionality that tries to initialize the event on different PMUs using the event type. Since PMUs can override the type field, `perf_init_event` follows these type redirections to find the actual PMU associated with the event. This is also where the kernel resolves dynamic PMU IDs - PMUs dynamically registered with the kernel are assigned an ID when registered, as mentioned in the man page.

Registering PMUs
----------------

Kernel code can register PMUs with [perf_pmu_register](https://elixir.bootlin.com/linux/v6.15.6/source/kernel/events/core.c#L12218). PMU developers specify their PMU's behavior using fields on [struct pmu](https://elixir.bootlin.com/linux/v6.15.6/source/include/linux/perf_event.h#L322), defined in `include/linux/perf_event.h`. The code in `perf_event_open` ensures the user has proper permissions and that the PMU supports all requested features.

Key fields that control supported behavior include:

-   **`task_ctx_nr`** - When set to `perf_invalid_context`, the PMU doesn't support task context
-   **`capabilities`** - Encodes PMU capabilities. For example, when `event->pmu->capabilities & PERF_PMU_CAP_NO_INTERRUPT` is set, the PMU doesn't support sampling mode (only counter mode where users read values)

PMU Function Pointers
---------------------

Struct pmu contains several function pointers for different PMU functionality:

-   `event_init` - initializes the perf_event struct
-   `add` and `del` - add and delete events to/from the PMU
-   `start` and `stop` - control event counting
-   `read` - read event values

There are also optional functions. One function not marked as optional but actually is: `sched_task`. **This allows the PMU to request a callback on every context switch on a specific CPU.** To enable this, the PMU needs to call [perf_sched_cb_inc](https://elixir.bootlin.com/linux/v6.15.6/source/kernel/events/core.c#L3723), which enqueues the PMU's CPU context onto a per-CPU callback list. If you don't need this functionality, you can skip implementing the scheduling callback.

Example: Intel Uncore PMU
-------------------------

The [Intel uncore PMU registration](https://elixir.bootlin.com/linux/v6.15.6/source/arch/x86/events/intel/uncore.c#L913) provides a small PMU configuration example. It's not minimal because it implements the optional `pmu_enable` and `pmu_disable` functions, and passes attributes.

The `attr_groups` field is also optional - it allows PMU developers to create attribute files and directories in `/sys/bus/event_source/devices/[pmu_name]/` for users to read PMU information. However, this can be left as NULL, as shown in the [Alpha architecture example](https://elixir.bootlin.com/linux/v6.15.6/source/arch/alpha/kernel/perf_event.c#L755).

Summary
-------

Understanding these perf subsystem internals is crucial when adding new PMU support to the kernel. We covered the major inputs to `perf_event_open`, the data structures associated with perf events, and how users specify their PMU functionality. Hope this serves as useful background when interacting with the code!