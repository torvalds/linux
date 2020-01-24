config DRM_I915_USERFAULT_AUTOSUSPEND
	int "Runtime autosuspend delay for userspace GGTT mmaps (ms)"
	default 250 # milliseconds
	help
	  On runtime suspend, as we suspend the device, we have to revoke
	  userspace GGTT mmaps and force userspace to take a pagefault on
	  their next access. The revocation and subsequent recreation of
	  the GGTT mmap can be very slow and so we impose a small hysteris
	  that complements the runtime-pm autosuspend and provides a lower
	  floor on the autosuspend delay.

	  May be 0 to disable the extra delay and solely use the device level
	  runtime pm autosuspend delay tunable.

config DRM_I915_HEARTBEAT_INTERVAL
	int "Interval between heartbeat pulses (ms)"
	default 2500 # milliseconds
	help
	  The driver sends a periodic heartbeat down all active engines to
	  check the health of the GPU and undertake regular house-keeping of
	  internal driver state.

	  May be 0 to disable heartbeats and therefore disable automatic GPU
	  hang detection.

config DRM_I915_PREEMPT_TIMEOUT
	int "Preempt timeout (ms, jiffy granularity)"
	default 640 # milliseconds
	help
	  How long to wait (in milliseconds) for a preemption event to occur
	  when submitting a new context via execlists. If the current context
	  does not hit an arbitration point and yield to HW before the timer
	  expires, the HW will be reset to allow the more important context
	  to execute.

	  May be 0 to disable the timeout.

config DRM_I915_SPIN_REQUEST
	int "Busywait for request completion (us)"
	default 5 # microseconds
	help
	  Before sleeping waiting for a request (GPU operation) to complete,
	  we may spend some time polling for its completion. As the IRQ may
	  take a non-negligible time to setup, we do a short spin first to
	  check if the request will complete in the time it would have taken
	  us to enable the interrupt.

	  May be 0 to disable the initial spin. In practice, we estimate
	  the cost of enabling the interrupt (if currently disabled) to be
	  a few microseconds.

config DRM_I915_STOP_TIMEOUT
	int "How long to wait for an engine to quiesce gracefully before reset (ms)"
	default 100 # milliseconds
	help
	  By stopping submission and sleeping for a short time before resetting
	  the GPU, we allow the innocent contexts also on the system to quiesce.
	  It is then less likely for a hanging context to cause collateral
	  damage as the system is reset in order to recover. The corollary is
	  that the reset itself may take longer and so be more disruptive to
	  interactive or low latency workloads.

config DRM_I915_TIMESLICE_DURATION
	int "Scheduling quantum for userspace batches (ms, jiffy granularity)"
	default 1 # milliseconds
	help
	  When two user batches of equal priority are executing, we will
	  alternate execution of each batch to ensure forward progress of
	  all users. This is necessary in some cases where there may be
	  an implicit dependency between those batches that requires
	  concurrent execution in order for them to proceed, e.g. they
	  interact with each other via userspace semaphores. Each context
	  is scheduled for execution for the timeslice duration, before
	  switching to the next context.

	  May be 0 to disable timeslicing.
