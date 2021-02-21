=====================================================================
Platform Profile Selection (e.g. /sys/firmware/acpi/platform_profile)
=====================================================================

On modern systems the platform performance, temperature, fan and other
hardware related characteristics are often dynamically configurable. The
platform configuration is often automatically adjusted to the current
conditions by some automatic mechanism (which may very well live outside
the kernel).

These auto platform adjustment mechanisms often can be configured with
one of several platform profiles, with either a bias towards low power
operation or towards performance.

The purpose of the platform_profile attribute is to offer a generic sysfs
API for selecting the platform profile of these automatic mechanisms.

Note that this API is only for selecting the platform profile, it is
NOT a goal of this API to allow monitoring the resulting performance
characteristics. Monitoring performance is best done with device/vendor
specific tools such as e.g. turbostat.

Specifically when selecting a high performance profile the actual achieved
performance may be limited by various factors such as: the heat generated
by other components, room temperature, free air flow at the bottom of a
laptop, etc. It is explicitly NOT a goal of this API to let userspace know
about any sub-optimal conditions which are impeding reaching the requested
performance level.

Since numbers on their own cannot represent the multiple variables that a
profile will adjust (power consumption, heat generation, etc) this API
uses strings to describe the various profiles. To make sure that userspace
gets a consistent experience the sysfs-platform_profile ABI document defines
a fixed set of profile names. Drivers *must* map their internal profile
representation onto this fixed set.

If there is no good match when mapping then a new profile name may be
added. Drivers which wish to introduce new profile names must:

 1. Explain why the existing profile names canot be used.
 2. Add the new profile name, along with a clear description of the
    expected behaviour, to the sysfs-platform_profile ABI documentation.
