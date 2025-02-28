# SPDX-License-Identifier: GPL-2.0-only
config DRM_XE_JOB_TIMEOUT_MAX
	int "Default max job timeout (ms)"
	default 10000 # milliseconds
	help
	  Configures the default max job timeout after which job will
	  be forcefully taken away from scheduler.
config DRM_XE_JOB_TIMEOUT_MIN
	int "Default min job timeout (ms)"
	default 1 # milliseconds
	help
	  Configures the default min job timeout after which job will
	  be forcefully taken away from scheduler.
config DRM_XE_TIMESLICE_MAX
	int "Default max timeslice duration (us)"
	default 10000000 # microseconds
	help
	  Configures the default max timeslice duration between multiple
	  contexts by guc scheduling.
config DRM_XE_TIMESLICE_MIN
	int "Default min timeslice duration (us)"
	default 1 # microseconds
	help
	  Configures the default min timeslice duration between multiple
	  contexts by guc scheduling.
config DRM_XE_PREEMPT_TIMEOUT
	int "Preempt timeout (us, jiffy granularity)"
	default 640000 # microseconds
	help
	  How long to wait (in microseconds) for a preemption event to occur
	  when submitting a new context. If the current context does not hit
	  an arbitration point and yield to HW before the timer expires, the
	  HW will be reset to allow the more important context to execute.
config DRM_XE_PREEMPT_TIMEOUT_MAX
	int "Default max preempt timeout (us)"
	default 10000000 # microseconds
	help
	  Configures the default max preempt timeout after which context
	  will be forcefully taken away and higher priority context will
	  run.
config DRM_XE_PREEMPT_TIMEOUT_MIN
	int "Default min preempt timeout (us)"
	default 1 # microseconds
	help
	  Configures the default min preempt timeout after which context
	  will be forcefully taken away and higher priority context will
	  run.
config DRM_XE_ENABLE_SCHEDTIMEOUT_LIMIT
	bool "Default configuration of limitation on scheduler timeout"
	default y
	help
	  Configures the enablement of limitation on scheduler timeout
	  to apply to applicable user. For elevated user, all above MIN
	  and MAX values will apply when this configuration is enable to
	  apply limitation. By default limitation is applied.
