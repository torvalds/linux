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
