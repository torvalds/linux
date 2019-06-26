.. SPDX-License-Identifier: GPL-2.0

===================
MIPI SyS-T over STP
===================

The MIPI SyS-T protocol driver can be used with STM class devices to
generate standardized trace stream. Aside from being a standard, it
provides better trace source identification and timestamp correlation.

In order to use the MIPI SyS-T protocol driver with your STM device,
first, you'll need CONFIG_STM_PROTO_SYS_T.

Now, you can select which protocol driver you want to use when you create
a policy for your STM device, by specifying it in the policy name:

# mkdir /config/stp-policy/dummy_stm.0:p_sys-t.my-policy/

In other words, the policy name format is extended like this:

  <device_name>:<protocol_name>.<policy_name>

With Intel TH, therefore it can look like "0-sth:p_sys-t.my-policy".

If the protocol name is omitted, the STM class will chose whichever
protocol driver was loaded first.

You can also double check that everything is working as expected by

# cat /config/stp-policy/dummy_stm.0:p_sys-t.my-policy/protocol
p_sys-t

Now, with the MIPI SyS-T protocol driver, each policy node in the
configfs gets a few additional attributes, which determine per-source
parameters specific to the protocol:

# mkdir /config/stp-policy/dummy_stm.0:p_sys-t.my-policy/default
# ls /config/stp-policy/dummy_stm.0:p_sys-t.my-policy/default
channels
clocksync_interval
do_len
masters
ts_interval
uuid

The most important one here is the "uuid", which determines the UUID
that will be used to tag all data coming from this source. It is
automatically generated when a new node is created, but it is likely
that you would want to change it.

do_len switches on/off the additional "payload length" field in the
MIPI SyS-T message header. It is off by default as the STP already
marks message boundaries.

ts_interval and clocksync_interval determine how much time in milliseconds
can pass before we need to include a protocol (not transport, aka STP)
timestamp in a message header or send a CLOCKSYNC packet, respectively.

See Documentation/ABI/testing/configfs-stp-policy-p_sys-t for more
details.

* [1] https://www.mipi.org/specifications/sys-t
