config DRM_I915_SPIN_REQUEST
	int
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
