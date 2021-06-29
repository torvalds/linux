=========================================
I915 GuC Submission/DRM Scheduler Section
=========================================

Upstream plan
=============
For upstream the overall plan for landing GuC submission and integrating the
i915 with the DRM scheduler is:

* Merge basic GuC submission
	* Basic submission support for all gen11+ platforms
	* Not enabled by default on any current platforms but can be enabled via
	  modparam enable_guc
	* Lots of rework will need to be done to integrate with DRM scheduler so
	  no need to nit pick everything in the code, it just should be
	  functional, no major coding style / layering errors, and not regress
	  execlists
	* Update IGTs / selftests as needed to work with GuC submission
	* Enable CI on supported platforms for a baseline
	* Rework / get CI heathly for GuC submission in place as needed
* Merge new parallel submission uAPI
	* Bonding uAPI completely incompatible with GuC submission, plus it has
	  severe design issues in general, which is why we want to retire it no
	  matter what
	* New uAPI adds I915_CONTEXT_ENGINES_EXT_PARALLEL context setup step
	  which configures a slot with N contexts
	* After I915_CONTEXT_ENGINES_EXT_PARALLEL a user can submit N batches to
	  a slot in a single execbuf IOCTL and the batches run on the GPU in
	  paralllel
	* Initially only for GuC submission but execlists can be supported if
	  needed
* Convert the i915 to use the DRM scheduler
	* GuC submission backend fully integrated with DRM scheduler
		* All request queues removed from backend (e.g. all backpressure
		  handled in DRM scheduler)
		* Resets / cancels hook in DRM scheduler
		* Watchdog hooks into DRM scheduler
		* Lots of complexity of the GuC backend can be pulled out once
		  integrated with DRM scheduler (e.g. state machine gets
		  simplier, locking gets simplier, etc...)
	* Execlists backend will minimum required to hook in the DRM scheduler
		* Legacy interface
		* Features like timeslicing / preemption / virtual engines would
		  be difficult to integrate with the DRM scheduler and these
		  features are not required for GuC submission as the GuC does
		  these things for us
		* ROI low on fully integrating into DRM scheduler
		* Fully integrating would add lots of complexity to DRM
		  scheduler
	* Port i915 priority inheritance / boosting feature in DRM scheduler
		* Used for i915 page flip, may be useful to other DRM drivers as
		  well
		* Will be an optional feature in the DRM scheduler
	* Remove in-order completion assumptions from DRM scheduler
		* Even when using the DRM scheduler the backends will handle
		  preemption, timeslicing, etc... so it is possible for jobs to
		  finish out of order
	* Pull out i915 priority levels and use DRM priority levels
	* Optimize DRM scheduler as needed

TODOs for GuC submission upstream
=================================

* Need an update to GuC firmware / i915 to enable error state capture
* Open source tool to decode GuC logs
* Public GuC spec

New uAPI for basic GuC submission
=================================
No major changes are required to the uAPI for basic GuC submission. The only
change is a new scheduler attribute: I915_SCHEDULER_CAP_STATIC_PRIORITY_MAP.
This attribute indicates the 2k i915 user priority levels are statically mapped
into 3 levels as follows:

* -1k to -1 Low priority
* 0 Medium priority
* 1 to 1k High priority

This is needed because the GuC only has 4 priority bands. The highest priority
band is reserved with the kernel. This aligns with the DRM scheduler priority
levels too.

Spec references:
----------------
* https://www.khronos.org/registry/EGL/extensions/IMG/EGL_IMG_context_priority.txt
* https://www.khronos.org/registry/vulkan/specs/1.2-extensions/html/chap5.html#devsandqueues-priority
* https://spec.oneapi.com/level-zero/latest/core/api.html#ze-command-queue-priority-t

New parallel submission uAPI
============================
Details to come in a following patch.
