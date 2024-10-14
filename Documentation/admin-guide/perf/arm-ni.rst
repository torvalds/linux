====================================
Arm Network-on Chip Interconnect PMU
====================================

NI-700 and friends implement a distinct PMU for each clock domain within the
interconnect. Correspondingly, the driver exposes multiple PMU devices named
arm_ni_<x>_cd_<y>, where <x> is an (arbitrary) instance identifier and <y> is
the clock domain ID within that particular instance. If multiple NI instances
exist within a system, the PMU devices can be correlated with the underlying
hardware instance via sysfs parentage.

Each PMU exposes base event aliases for the interface types present in its clock
domain. These require qualifying with the "eventid" and "nodeid" parameters
to specify the event code to count and the interface at which to count it
(per the configured hardware ID as reflected in the xxNI_NODE_INFO register).
The exception is the "cycles" alias for the PMU cycle counter, which is encoded
with the PMU node type and needs no further qualification.
