CEC Kernel Support
==================

The CEC framework provides a unified kernel interface for use with HDMI CEC
hardware. It is designed to handle a multiple types of hardware (receivers,
transmitters, USB dongles). The framework also gives the option to decide
what to do in the kernel driver and what should be handled by userspace
applications. In addition it integrates the remote control passthrough
feature into the kernel's remote control framework.


The CEC Protocol
----------------

The CEC protocol enables consumer electronic devices to communicate with each
other through the HDMI connection. The protocol uses logical addresses in the
communication. The logical address is strictly connected with the functionality
provided by the device. The TV acting as the communication hub is always
assigned address 0. The physical address is determined by the physical
connection between devices.

The CEC framework described here is up to date with the CEC 2.0 specification.
It is documented in the HDMI 1.4 specification with the new 2.0 bits documented
in the HDMI 2.0 specification. But for most of the features the freely available
HDMI 1.3a specification is sufficient:

http://www.microprocessor.org/HDMISpecification13a.pdf


The Kernel Interface
====================

CEC Adapter
-----------

The struct cec_adapter represents the CEC adapter hardware. It is created by
calling cec_allocate_adapter() and deleted by calling cec_delete_adapter():

.. c:function::
   struct cec_adapter *cec_allocate_adapter(const struct cec_adap_ops *ops, void *priv,
   const char *name, u32 caps, u8 available_las);

.. c:function::
   void cec_delete_adapter(struct cec_adapter *adap);

To create an adapter you need to pass the following information:

ops:
	adapter operations which are called by the CEC framework and that you
	have to implement.

priv:
	will be stored in adap->priv and can be used by the adapter ops.

name:
	the name of the CEC adapter. Note: this name will be copied.

caps:
	capabilities of the CEC adapter. These capabilities determine the
	capabilities of the hardware and which parts are to be handled
	by userspace and which parts are handled by kernelspace. The
	capabilities are returned by CEC_ADAP_G_CAPS.

available_las:
	the number of simultaneous logical addresses that this
	adapter can handle. Must be 1 <= available_las <= CEC_MAX_LOG_ADDRS.


To register the /dev/cecX device node and the remote control device (if
CEC_CAP_RC is set) you call:

.. c:function::
	int cec_register_adapter(struct cec_adapter *adap, struct device *parent);

where parent is the parent device.

To unregister the devices call:

.. c:function::
	void cec_unregister_adapter(struct cec_adapter *adap);

Note: if cec_register_adapter() fails, then call cec_delete_adapter() to
clean up. But if cec_register_adapter() succeeded, then only call
cec_unregister_adapter() to clean up, never cec_delete_adapter(). The
unregister function will delete the adapter automatically once the last user
of that /dev/cecX device has closed its file handle.


Implementing the Low-Level CEC Adapter
--------------------------------------

The following low-level adapter operations have to be implemented in
your driver:

.. c:type:: struct cec_adap_ops

.. code-block:: none

	struct cec_adap_ops
	{
		/* Low-level callbacks */
		int (*adap_enable)(struct cec_adapter *adap, bool enable);
		int (*adap_monitor_all_enable)(struct cec_adapter *adap, bool enable);
		int (*adap_log_addr)(struct cec_adapter *adap, u8 logical_addr);
		int (*adap_transmit)(struct cec_adapter *adap, u8 attempts,
				      u32 signal_free_time, struct cec_msg *msg);
		void (*adap_status)(struct cec_adapter *adap, struct seq_file *file);

		/* High-level callbacks */
		...
	};

The five low-level ops deal with various aspects of controlling the CEC adapter
hardware:


To enable/disable the hardware:

.. c:function::
	int (*adap_enable)(struct cec_adapter *adap, bool enable);

This callback enables or disables the CEC hardware. Enabling the CEC hardware
means powering it up in a state where no logical addresses are claimed. This
op assumes that the physical address (adap->phys_addr) is valid when enable is
true and will not change while the CEC adapter remains enabled. The initial
state of the CEC adapter after calling cec_allocate_adapter() is disabled.

Note that adap_enable must return 0 if enable is false.


To enable/disable the 'monitor all' mode:

.. c:function::
	int (*adap_monitor_all_enable)(struct cec_adapter *adap, bool enable);

If enabled, then the adapter should be put in a mode to also monitor messages
that not for us. Not all hardware supports this and this function is only
called if the CEC_CAP_MONITOR_ALL capability is set. This callback is optional
(some hardware may always be in 'monitor all' mode).

Note that adap_monitor_all_enable must return 0 if enable is false.


To program a new logical address:

.. c:function::
	int (*adap_log_addr)(struct cec_adapter *adap, u8 logical_addr);

If logical_addr == CEC_LOG_ADDR_INVALID then all programmed logical addresses
are to be erased. Otherwise the given logical address should be programmed.
If the maximum number of available logical addresses is exceeded, then it
should return -ENXIO. Once a logical address is programmed the CEC hardware
can receive directed messages to that address.

Note that adap_log_addr must return 0 if logical_addr is CEC_LOG_ADDR_INVALID.


To transmit a new message:

.. c:function::
	int (*adap_transmit)(struct cec_adapter *adap, u8 attempts,
			     u32 signal_free_time, struct cec_msg *msg);

This transmits a new message. The attempts argument is the suggested number of
attempts for the transmit.

The signal_free_time is the number of data bit periods that the adapter should
wait when the line is free before attempting to send a message. This value
depends on whether this transmit is a retry, a message from a new initiator or
a new message for the same initiator. Most hardware will handle this
automatically, but in some cases this information is needed.

The CEC_FREE_TIME_TO_USEC macro can be used to convert signal_free_time to
microseconds (one data bit period is 2.4 ms).


To log the current CEC hardware status:

.. c:function::
	void (*adap_status)(struct cec_adapter *adap, struct seq_file *file);

This optional callback can be used to show the status of the CEC hardware.
The status is available through debugfs: cat /sys/kernel/debug/cec/cecX/status


Your adapter driver will also have to react to events (typically interrupt
driven) by calling into the framework in the following situations:

When a transmit finished (successfully or otherwise):

.. c:function::
	void cec_transmit_done(struct cec_adapter *adap, u8 status, u8 arb_lost_cnt,
		       u8 nack_cnt, u8 low_drive_cnt, u8 error_cnt);

The status can be one of:

CEC_TX_STATUS_OK:
	the transmit was successful.

CEC_TX_STATUS_ARB_LOST:
	arbitration was lost: another CEC initiator
	took control of the CEC line and you lost the arbitration.

CEC_TX_STATUS_NACK:
	the message was nacked (for a directed message) or
	acked (for a broadcast message). A retransmission is needed.

CEC_TX_STATUS_LOW_DRIVE:
	low drive was detected on the CEC bus. This indicates that
	a follower detected an error on the bus and requested a
	retransmission.

CEC_TX_STATUS_ERROR:
	some unspecified error occurred: this can be one of
	the previous two if the hardware cannot differentiate or something
	else entirely.

CEC_TX_STATUS_MAX_RETRIES:
	could not transmit the message after trying multiple times.
	Should only be set by the driver if it has hardware support for
	retrying messages. If set, then the framework assumes that it
	doesn't have to make another attempt to transmit the message
	since the hardware did that already.

The \*_cnt arguments are the number of error conditions that were seen.
This may be 0 if no information is available. Drivers that do not support
hardware retry can just set the counter corresponding to the transmit error
to 1, if the hardware does support retry then either set these counters to
0 if the hardware provides no feedback of which errors occurred and how many
times, or fill in the correct values as reported by the hardware.

When a CEC message was received:

.. c:function::
	void cec_received_msg(struct cec_adapter *adap, struct cec_msg *msg);

Speaks for itself.

Implementing the interrupt handler
----------------------------------

Typically the CEC hardware provides interrupts that signal when a transmit
finished and whether it was successful or not, and it provides and interrupt
when a CEC message was received.

The CEC driver should always process the transmit interrupts first before
handling the receive interrupt. The framework expects to see the cec_transmit_done
call before the cec_received_msg call, otherwise it can get confused if the
received message was in reply to the transmitted message.

Implementing the High-Level CEC Adapter
---------------------------------------

The low-level operations drive the hardware, the high-level operations are
CEC protocol driven. The following high-level callbacks are available:

.. code-block:: none

	struct cec_adap_ops {
		/* Low-level callbacks */
		...

		/* High-level CEC message callback */
		int (*received)(struct cec_adapter *adap, struct cec_msg *msg);
	};

The received() callback allows the driver to optionally handle a newly
received CEC message

.. c:function::
	int (*received)(struct cec_adapter *adap, struct cec_msg *msg);

If the driver wants to process a CEC message, then it can implement this
callback. If it doesn't want to handle this message, then it should return
-ENOMSG, otherwise the CEC framework assumes it processed this message and
it will not do anything with it.


CEC framework functions
-----------------------

CEC Adapter drivers can call the following CEC framework functions:

.. c:function::
	int cec_transmit_msg(struct cec_adapter *adap, struct cec_msg *msg,
			     bool block);

Transmit a CEC message. If block is true, then wait until the message has been
transmitted, otherwise just queue it and return.

.. c:function::
	void cec_s_phys_addr(struct cec_adapter *adap, u16 phys_addr,
			     bool block);

Change the physical address. This function will set adap->phys_addr and
send an event if it has changed. If cec_s_log_addrs() has been called and
the physical address has become valid, then the CEC framework will start
claiming the logical addresses. If block is true, then this function won't
return until this process has finished.

When the physical address is set to a valid value the CEC adapter will
be enabled (see the adap_enable op). When it is set to CEC_PHYS_ADDR_INVALID,
then the CEC adapter will be disabled. If you change a valid physical address
to another valid physical address, then this function will first set the
address to CEC_PHYS_ADDR_INVALID before enabling the new physical address.

.. c:function::
	int cec_s_log_addrs(struct cec_adapter *adap,
			    struct cec_log_addrs *log_addrs, bool block);

Claim the CEC logical addresses. Should never be called if CEC_CAP_LOG_ADDRS
is set. If block is true, then wait until the logical addresses have been
claimed, otherwise just queue it and return. To unconfigure all logical
addresses call this function with log_addrs set to NULL or with
log_addrs->num_log_addrs set to 0. The block argument is ignored when
unconfiguring. This function will just return if the physical address is
invalid. Once the physical address becomes valid, then the framework will
attempt to claim these logical addresses.
