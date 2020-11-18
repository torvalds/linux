==========================
ARM Cache Coherent Network
==========================

CCN-504 is a ring-bus interconnect consisting of 11 crosspoints
(XPs), with each crosspoint supporting up to two device ports,
so nodes (devices) 0 and 1 are connected to crosspoint 0,
nodes 2 and 3 to crosspoint 1 etc.

PMU (perf) driver
-----------------

The CCN driver registers a perf PMU driver, which provides
description of available events and configuration options
in sysfs, see /sys/bus/event_source/devices/ccn*.

The "format" directory describes format of the config, config1
and config2 fields of the perf_event_attr structure. The "events"
directory provides configuration templates for all documented
events, that can be used with perf tool. For example "xp_valid_flit"
is an equivalent of "type=0x8,event=0x4". Other parameters must be
explicitly specified.

For events originating from device, "node" defines its index.

Crosspoint PMU events require "xp" (index), "bus" (bus number)
and "vc" (virtual channel ID).

Crosspoint watchpoint-based events (special "event" value 0xfe)
require "xp" and "vc" as above plus "port" (device port index),
"dir" (transmit/receive direction), comparator values ("cmp_l"
and "cmp_h") and "mask", being index of the comparator mask.

Masks are defined separately from the event description
(due to limited number of the config values) in the "cmp_mask"
directory, with first 8 configurable by user and additional
4 hardcoded for the most frequent use cases.

Cycle counter is described by a "type" value 0xff and does
not require any other settings.

The driver also provides a "cpumask" sysfs attribute, which contains
a single CPU ID, of the processor which will be used to handle all
the CCN PMU events. It is recommended that the user space tools
request the events on this processor (if not, the perf_event->cpu value
will be overwritten anyway). In case of this processor being offlined,
the events are migrated to another one and the attribute is updated.

Example of perf tool use::

  / # perf list | grep ccn
    ccn/cycles/                                        [Kernel PMU event]
  <...>
    ccn/xp_valid_flit,xp=?,port=?,vc=?,dir=?/          [Kernel PMU event]
  <...>

  / # perf stat -a -e ccn/cycles/,ccn/xp_valid_flit,xp=1,port=0,vc=1,dir=1/ \
                                                                         sleep 1

The driver does not support sampling, therefore "perf record" will
not work. Per-task (without "-a") perf sessions are not supported.
