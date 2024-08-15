.. SPDX-License-Identifier: GPL-2.0+

.. |ssh_ptl| replace:: :c:type:`struct ssh_ptl <ssh_ptl>`
.. |ssh_ptl_submit| replace:: :c:func:`ssh_ptl_submit`
.. |ssh_ptl_cancel| replace:: :c:func:`ssh_ptl_cancel`
.. |ssh_ptl_shutdown| replace:: :c:func:`ssh_ptl_shutdown`
.. |ssh_ptl_rx_rcvbuf| replace:: :c:func:`ssh_ptl_rx_rcvbuf`
.. |ssh_rtl| replace:: :c:type:`struct ssh_rtl <ssh_rtl>`
.. |ssh_rtl_submit| replace:: :c:func:`ssh_rtl_submit`
.. |ssh_rtl_cancel| replace:: :c:func:`ssh_rtl_cancel`
.. |ssh_rtl_shutdown| replace:: :c:func:`ssh_rtl_shutdown`
.. |ssh_packet| replace:: :c:type:`struct ssh_packet <ssh_packet>`
.. |ssh_packet_get| replace:: :c:func:`ssh_packet_get`
.. |ssh_packet_put| replace:: :c:func:`ssh_packet_put`
.. |ssh_packet_ops| replace:: :c:type:`struct ssh_packet_ops <ssh_packet_ops>`
.. |ssh_packet_base_priority| replace:: :c:type:`enum ssh_packet_base_priority <ssh_packet_base_priority>`
.. |ssh_packet_flags| replace:: :c:type:`enum ssh_packet_flags <ssh_packet_flags>`
.. |SSH_PACKET_PRIORITY| replace:: :c:func:`SSH_PACKET_PRIORITY`
.. |ssh_frame| replace:: :c:type:`struct ssh_frame <ssh_frame>`
.. |ssh_command| replace:: :c:type:`struct ssh_command <ssh_command>`
.. |ssh_request| replace:: :c:type:`struct ssh_request <ssh_request>`
.. |ssh_request_get| replace:: :c:func:`ssh_request_get`
.. |ssh_request_put| replace:: :c:func:`ssh_request_put`
.. |ssh_request_ops| replace:: :c:type:`struct ssh_request_ops <ssh_request_ops>`
.. |ssh_request_init| replace:: :c:func:`ssh_request_init`
.. |ssh_request_flags| replace:: :c:type:`enum ssh_request_flags <ssh_request_flags>`
.. |ssam_controller| replace:: :c:type:`struct ssam_controller <ssam_controller>`
.. |ssam_device| replace:: :c:type:`struct ssam_device <ssam_device>`
.. |ssam_device_driver| replace:: :c:type:`struct ssam_device_driver <ssam_device_driver>`
.. |ssam_client_bind| replace:: :c:func:`ssam_client_bind`
.. |ssam_client_link| replace:: :c:func:`ssam_client_link`
.. |ssam_request_sync| replace:: :c:type:`struct ssam_request_sync <ssam_request_sync>`
.. |ssam_event_registry| replace:: :c:type:`struct ssam_event_registry <ssam_event_registry>`
.. |ssam_event_id| replace:: :c:type:`struct ssam_event_id <ssam_event_id>`
.. |ssam_nf| replace:: :c:type:`struct ssam_nf <ssam_nf>`
.. |ssam_nf_refcount_inc| replace:: :c:func:`ssam_nf_refcount_inc`
.. |ssam_nf_refcount_dec| replace:: :c:func:`ssam_nf_refcount_dec`
.. |ssam_notifier_register| replace:: :c:func:`ssam_notifier_register`
.. |ssam_notifier_unregister| replace:: :c:func:`ssam_notifier_unregister`
.. |ssam_cplt| replace:: :c:type:`struct ssam_cplt <ssam_cplt>`
.. |ssam_event_queue| replace:: :c:type:`struct ssam_event_queue <ssam_event_queue>`
.. |ssam_request_sync_submit| replace:: :c:func:`ssam_request_sync_submit`

=====================
Core Driver Internals
=====================

Architectural overview of the Surface System Aggregator Module (SSAM) core
and Surface Serial Hub (SSH) driver. For the API documentation, refer to:

.. toctree::
   :maxdepth: 2

   internal-api


Overview
========

The SSAM core implementation is structured in layers, somewhat following the
SSH protocol structure:

Lower-level packet transport is implemented in the *packet transport layer
(PTL)*, directly building on top of the serial device (serdev)
infrastructure of the kernel. As the name indicates, this layer deals with
the packet transport logic and handles things like packet validation, packet
acknowledgment (ACKing), packet (retransmission) timeouts, and relaying
packet payloads to higher-level layers.

Above this sits the *request transport layer (RTL)*. This layer is centered
around command-type packet payloads, i.e. requests (sent from host to EC),
responses of the EC to those requests, and events (sent from EC to host).
It, specifically, distinguishes events from request responses, matches
responses to their corresponding requests, and implements request timeouts.

The *controller* layer is building on top of this and essentially decides
how request responses and, especially, events are dealt with. It provides an
event notifier system, handles event activation/deactivation, provides a
workqueue for event and asynchronous request completion, and also manages
the message counters required for building command messages (``SEQ``,
``RQID``). This layer basically provides a fundamental interface to the SAM
EC for use in other kernel drivers.

While the controller layer already provides an interface for other kernel
drivers, the client *bus* extends this interface to provide support for
native SSAM devices, i.e. devices that are not defined in ACPI and not
implemented as platform devices, via |ssam_device| and |ssam_device_driver|
simplify management of client devices and client drivers.

Refer to Documentation/driver-api/surface_aggregator/client.rst for
documentation regarding the client device/driver API and interface options
for other kernel drivers. It is recommended to familiarize oneself with
that chapter and the Documentation/driver-api/surface_aggregator/ssh.rst
before continuing with the architectural overview below.


Packet Transport Layer
======================

The packet transport layer is represented via |ssh_ptl| and is structured
around the following key concepts:

Packets
-------

Packets are the fundamental transmission unit of the SSH protocol. They are
managed by the packet transport layer, which is essentially the lowest layer
of the driver and is built upon by other components of the SSAM core.
Packets to be transmitted by the SSAM core are represented via |ssh_packet|
(in contrast, packets received by the core do not have any specific
structure and are managed entirely via the raw |ssh_frame|).

This structure contains the required fields to manage the packet inside the
transport layer, as well as a reference to the buffer containing the data to
be transmitted (i.e. the message wrapped in |ssh_frame|). Most notably, it
contains an internal reference count, which is used for managing its
lifetime (accessible via |ssh_packet_get| and |ssh_packet_put|). When this
counter reaches zero, the ``release()`` callback provided to the packet via
its |ssh_packet_ops| reference is executed, which may then deallocate the
packet or its enclosing structure (e.g. |ssh_request|).

In addition to the ``release`` callback, the |ssh_packet_ops| reference also
provides a ``complete()`` callback, which is run once the packet has been
completed and provides the status of this completion, i.e. zero on success
or a negative errno value in case of an error. Once the packet has been
submitted to the packet transport layer, the ``complete()`` callback is
always guaranteed to be executed before the ``release()`` callback, i.e. the
packet will always be completed, either successfully, with an error, or due
to cancellation, before it will be released.

The state of a packet is managed via its ``state`` flags
(|ssh_packet_flags|), which also contains the packet type. In particular,
the following bits are noteworthy:

* ``SSH_PACKET_SF_LOCKED_BIT``: This bit is set when completion, either
  through error or success, is imminent. It indicates that no further
  references of the packet should be taken and any existing references
  should be dropped as soon as possible. The process setting this bit is
  responsible for removing any references to this packet from the packet
  queue and pending set.

* ``SSH_PACKET_SF_COMPLETED_BIT``: This bit is set by the process running the
  ``complete()`` callback and is used to ensure that this callback only runs
  once.

* ``SSH_PACKET_SF_QUEUED_BIT``: This bit is set when the packet is queued on
  the packet queue and cleared when it is dequeued.

* ``SSH_PACKET_SF_PENDING_BIT``: This bit is set when the packet is added to
  the pending set and cleared when it is removed from it.

Packet Queue
------------

The packet queue is the first of the two fundamental collections in the
packet transport layer. It is a priority queue, with priority of the
respective packets based on the packet type (major) and number of tries
(minor). See |SSH_PACKET_PRIORITY| for more details on the priority value.

All packets to be transmitted by the transport layer must be submitted to
this queue via |ssh_ptl_submit|. Note that this includes control packets
sent by the transport layer itself. Internally, data packets can be
re-submitted to this queue due to timeouts or NAK packets sent by the EC.

Pending Set
-----------

The pending set is the second of the two fundamental collections in the
packet transport layer. It stores references to packets that have already
been transmitted, but wait for acknowledgment (e.g. the corresponding ACK
packet) by the EC.

Note that a packet may both be pending and queued if it has been
re-submitted due to a packet acknowledgment timeout or NAK. On such a
re-submission, packets are not removed from the pending set.

Transmitter Thread
------------------

The transmitter thread is responsible for most of the actual work regarding
packet transmission. In each iteration, it (waits for and) checks if the
next packet on the queue (if any) can be transmitted and, if so, removes it
from the queue and increments its counter for the number of transmission
attempts, i.e. tries. If the packet is sequenced, i.e. requires an ACK by
the EC, the packet is added to the pending set. Next, the packet's data is
submitted to the serdev subsystem. In case of an error or timeout during
this submission, the packet is completed by the transmitter thread with the
status value of the callback set accordingly. In case the packet is
unsequenced, i.e. does not require an ACK by the EC, the packet is completed
with success on the transmitter thread.

Transmission of sequenced packets is limited by the number of concurrently
pending packets, i.e. a limit on how many packets may be waiting for an ACK
from the EC in parallel. This limit is currently set to one (see
Documentation/driver-api/surface_aggregator/ssh.rst for the reasoning behind
this). Control packets (i.e. ACK and NAK) can always be transmitted.

Receiver Thread
---------------

Any data received from the EC is put into a FIFO buffer for further
processing. This processing happens on the receiver thread. The receiver
thread parses and validates the received message into its |ssh_frame| and
corresponding payload. It prepares and submits the necessary ACK (and on
validation error or invalid data NAK) packets for the received messages.

This thread also handles further processing, such as matching ACK messages
to the corresponding pending packet (via sequence ID) and completing it, as
well as initiating re-submission of all currently pending packets on
receival of a NAK message (re-submission in case of a NAK is similar to
re-submission due to timeout, see below for more details on that). Note that
the successful completion of a sequenced packet will always run on the
receiver thread (whereas any failure-indicating completion will run on the
process where the failure occurred).

Any payload data is forwarded via a callback to the next upper layer, i.e.
the request transport layer.

Timeout Reaper
--------------

The packet acknowledgment timeout is a per-packet timeout for sequenced
packets, started when the respective packet begins (re-)transmission (i.e.
this timeout is armed once per transmission attempt on the transmitter
thread). It is used to trigger re-submission or, when the number of tries
has been exceeded, cancellation of the packet in question.

This timeout is handled via a dedicated reaper task, which is essentially a
work item (re-)scheduled to run when the next packet is set to time out. The
work item then checks the set of pending packets for any packets that have
exceeded the timeout and, if there are any remaining packets, re-schedules
itself to the next appropriate point in time.

If a timeout has been detected by the reaper, the packet will either be
re-submitted if it still has some remaining tries left, or completed with
``-ETIMEDOUT`` as status if not. Note that re-submission, in this case and
triggered by receival of a NAK, means that the packet is added to the queue
with a now incremented number of tries, yielding a higher priority. The
timeout for the packet will be disabled until the next transmission attempt
and the packet remains on the pending set.

Note that due to transmission and packet acknowledgment timeouts, the packet
transport layer is always guaranteed to make progress, if only through
timing out packets, and will never fully block.

Concurrency and Locking
-----------------------

There are two main locks in the packet transport layer: One guarding access
to the packet queue and one guarding access to the pending set. These
collections may only be accessed and modified under the respective lock. If
access to both collections is needed, the pending lock must be acquired
before the queue lock to avoid deadlocks.

In addition to guarding the collections, after initial packet submission
certain packet fields may only be accessed under one of the locks.
Specifically, the packet priority must only be accessed while holding the
queue lock and the packet timestamp must only be accessed while holding the
pending lock.

Other parts of the packet transport layer are guarded independently. State
flags are managed by atomic bit operations and, if necessary, memory
barriers. Modifications to the timeout reaper work item and expiration date
are guarded by their own lock.

The reference of the packet to the packet transport layer (``ptl``) is
somewhat special. It is either set when the upper layer request is submitted
or, if there is none, when the packet is first submitted. After it is set,
it will not change its value. Functions that may run concurrently with
submission, i.e. cancellation, can not rely on the ``ptl`` reference to be
set. Access to it in these functions is guarded by ``READ_ONCE()``, whereas
setting ``ptl`` is equally guarded with ``WRITE_ONCE()`` for symmetry.

Some packet fields may be read outside of the respective locks guarding
them, specifically priority and state for tracing. In those cases, proper
access is ensured by employing ``WRITE_ONCE()`` and ``READ_ONCE()``. Such
read-only access is only allowed when stale values are not critical.

With respect to the interface for higher layers, packet submission
(|ssh_ptl_submit|), packet cancellation (|ssh_ptl_cancel|), data receival
(|ssh_ptl_rx_rcvbuf|), and layer shutdown (|ssh_ptl_shutdown|) may always be
executed concurrently with respect to each other. Note that packet
submission may not run concurrently with itself for the same packet.
Equally, shutdown and data receival may also not run concurrently with
themselves (but may run concurrently with each other).


Request Transport Layer
=======================

The request transport layer is represented via |ssh_rtl| and builds on top
of the packet transport layer. It deals with requests, i.e. SSH packets sent
by the host containing a |ssh_command| as frame payload. This layer
separates responses to requests from events, which are also sent by the EC
via a |ssh_command| payload. While responses are handled in this layer,
events are relayed to the next upper layer, i.e. the controller layer, via
the corresponding callback. The request transport layer is structured around
the following key concepts:

Request
-------

Requests are packets with a command-type payload, sent from host to EC to
query data from or trigger an action on it (or both simultaneously). They
are represented by |ssh_request|, wrapping the underlying |ssh_packet|
storing its message data (i.e. SSH frame with command payload). Note that
all top-level representations, e.g. |ssam_request_sync| are built upon this
struct.

As |ssh_request| extends |ssh_packet|, its lifetime is also managed by the
reference counter inside the packet struct (which can be accessed via
|ssh_request_get| and |ssh_request_put|). Once the counter reaches zero, the
``release()`` callback of the |ssh_request_ops| reference of the request is
called.

Requests can have an optional response that is equally sent via a SSH
message with command-type payload (from EC to host). The party constructing
the request must know if a response is expected and mark this in the request
flags provided to |ssh_request_init|, so that the request transport layer
can wait for this response.

Similar to |ssh_packet|, |ssh_request| also has a ``complete()`` callback
provided via its request ops reference and is guaranteed to be completed
before it is released once it has been submitted to the request transport
layer via |ssh_rtl_submit|. For a request without a response, successful
completion will occur once the underlying packet has been successfully
transmitted by the packet transport layer (i.e. from within the packet
completion callback). For a request with response, successful completion
will occur once the response has been received and matched to the request
via its request ID (which happens on the packet layer's data-received
callback running on the receiver thread). If the request is completed with
an error, the status value will be set to the corresponding (negative) errno
value.

The state of a request is again managed via its ``state`` flags
(|ssh_request_flags|), which also encode the request type. In particular,
the following bits are noteworthy:

* ``SSH_REQUEST_SF_LOCKED_BIT``: This bit is set when completion, either
  through error or success, is imminent. It indicates that no further
  references of the request should be taken and any existing references
  should be dropped as soon as possible. The process setting this bit is
  responsible for removing any references to this request from the request
  queue and pending set.

* ``SSH_REQUEST_SF_COMPLETED_BIT``: This bit is set by the process running the
  ``complete()`` callback and is used to ensure that this callback only runs
  once.

* ``SSH_REQUEST_SF_QUEUED_BIT``: This bit is set when the request is queued on
  the request queue and cleared when it is dequeued.

* ``SSH_REQUEST_SF_PENDING_BIT``: This bit is set when the request is added to
  the pending set and cleared when it is removed from it.

Request Queue
-------------

The request queue is the first of the two fundamental collections in the
request transport layer. In contrast to the packet queue of the packet
transport layer, it is not a priority queue and the simple first come first
serve principle applies.

All requests to be transmitted by the request transport layer must be
submitted to this queue via |ssh_rtl_submit|. Once submitted, requests may
not be re-submitted, and will not be re-submitted automatically on timeout.
Instead, the request is completed with a timeout error. If desired, the
caller can create and submit a new request for another try, but it must not
submit the same request again.

Pending Set
-----------

The pending set is the second of the two fundamental collections in the
request transport layer. This collection stores references to all pending
requests, i.e. requests awaiting a response from the EC (similar to what the
pending set of the packet transport layer does for packets).

Transmitter Task
----------------

The transmitter task is scheduled when a new request is available for
transmission. It checks if the next request on the request queue can be
transmitted and, if so, submits its underlying packet to the packet
transport layer. This check ensures that only a limited number of
requests can be pending, i.e. waiting for a response, at the same time. If
the request requires a response, the request is added to the pending set
before its packet is submitted.

Packet Completion Callback
--------------------------

The packet completion callback is executed once the underlying packet of a
request has been completed. In case of an error completion, the
corresponding request is completed with the error value provided in this
callback.

On successful packet completion, further processing depends on the request.
If the request expects a response, it is marked as transmitted and the
request timeout is started. If the request does not expect a response, it is
completed with success.

Data-Received Callback
----------------------

The data received callback notifies the request transport layer of data
being received by the underlying packet transport layer via a data-type
frame. In general, this is expected to be a command-type payload.

If the request ID of the command is one of the request IDs reserved for
events (one to ``SSH_NUM_EVENTS``, inclusively), it is forwarded to the
event callback registered in the request transport layer. If the request ID
indicates a response to a request, the respective request is looked up in
the pending set and, if found and marked as transmitted, completed with
success.

Timeout Reaper
--------------

The request-response-timeout is a per-request timeout for requests expecting
a response. It is used to ensure that a request does not wait indefinitely
on a response from the EC and is started after the underlying packet has
been successfully completed.

This timeout is, similar to the packet acknowledgment timeout on the packet
transport layer, handled via a dedicated reaper task. This task is
essentially a work-item (re-)scheduled to run when the next request is set
to time out. The work item then scans the set of pending requests for any
requests that have timed out and completes them with ``-ETIMEDOUT`` as
status. Requests will not be re-submitted automatically. Instead, the issuer
of the request must construct and submit a new request, if so desired.

Note that this timeout, in combination with packet transmission and
acknowledgment timeouts, guarantees that the request layer will always make
progress, even if only through timing out packets, and never fully block.

Concurrency and Locking
-----------------------

Similar to the packet transport layer, there are two main locks in the
request transport layer: One guarding access to the request queue and one
guarding access to the pending set. These collections may only be accessed
and modified under the respective lock.

Other parts of the request transport layer are guarded independently. State
flags are (again) managed by atomic bit operations and, if necessary, memory
barriers. Modifications to the timeout reaper work item and expiration date
are guarded by their own lock.

Some request fields may be read outside of the respective locks guarding
them, specifically the state for tracing. In those cases, proper access is
ensured by employing ``WRITE_ONCE()`` and ``READ_ONCE()``. Such read-only
access is only allowed when stale values are not critical.

With respect to the interface for higher layers, request submission
(|ssh_rtl_submit|), request cancellation (|ssh_rtl_cancel|), and layer
shutdown (|ssh_rtl_shutdown|) may always be executed concurrently with
respect to each other. Note that request submission may not run concurrently
with itself for the same request (and also may only be called once per
request). Equally, shutdown may also not run concurrently with itself.


Controller Layer
================

The controller layer extends on the request transport layer to provide an
easy-to-use interface for client drivers. It is represented by
|ssam_controller| and the SSH driver. While the lower level transport layers
take care of transmitting and handling packets and requests, the controller
layer takes on more of a management role. Specifically, it handles device
initialization, power management, and event handling, including event
delivery and registration via the (event) completion system (|ssam_cplt|).

Event Registration
------------------

In general, an event (or rather a class of events) has to be explicitly
requested by the host before the EC will send it (HID input events seem to
be the exception). This is done via an event-enable request (similarly,
events should be disabled via an event-disable request once no longer
desired).

The specific request used to enable (or disable) an event is given via an
event registry, i.e. the governing authority of this event (so to speak),
represented by |ssam_event_registry|. As parameters to this request, the
target category and, depending on the event registry, instance ID of the
event to be enabled must be provided. This (optional) instance ID must be
zero if the registry does not use it. Together, target category and instance
ID form the event ID, represented by |ssam_event_id|. In short, both, event
registry and event ID, are required to uniquely identify a respective class
of events.

Note that a further *request ID* parameter must be provided for the
enable-event request. This parameter does not influence the class of events
being enabled, but instead is set as the request ID (RQID) on each event of
this class sent by the EC. It is used to identify events (as a limited
number of request IDs is reserved for use in events only, specifically one
to ``SSH_NUM_EVENTS`` inclusively) and also map events to their specific
class. Currently, the controller always sets this parameter to the target
category specified in |ssam_event_id|.

As multiple client drivers may rely on the same (or overlapping) classes of
events and enable/disable calls are strictly binary (i.e. on/off), the
controller has to manage access to these events. It does so via reference
counting, storing the counter inside an RB-tree based mapping with event
registry and ID as key (there is no known list of valid event registry and
event ID combinations). See |ssam_nf|, |ssam_nf_refcount_inc|, and
|ssam_nf_refcount_dec| for details.

This management is done together with notifier registration (described in
the next section) via the top-level |ssam_notifier_register| and
|ssam_notifier_unregister| functions.

Event Delivery
--------------

To receive events, a client driver has to register an event notifier via
|ssam_notifier_register|. This increments the reference counter for that
specific class of events (as detailed in the previous section), enables the
class on the EC (if it has not been enabled already), and installs the
provided notifier callback.

Notifier callbacks are stored in lists, with one (RCU) list per target
category (provided via the event ID; NB: there is a fixed known number of
target categories). There is no known association from the combination of
event registry and event ID to the command data (target ID, target category,
command ID, and instance ID) that can be provided by an event class, apart
from target category and instance ID given via the event ID.

Note that due to the way notifiers are (or rather have to be) stored, client
drivers may receive events that they have not requested and need to account
for them. Specifically, they will, by default, receive all events from the
same target category. To simplify dealing with this, filtering of events by
target ID (provided via the event registry) and instance ID (provided via
the event ID) can be requested when registering a notifier. This filtering
is applied when iterating over the notifiers at the time they are executed.

All notifier callbacks are executed on a dedicated workqueue, the so-called
completion workqueue. After an event has been received via the callback
installed in the request layer (running on the receiver thread of the packet
transport layer), it will be put on its respective event queue
(|ssam_event_queue|). From this event queue the completion work item of that
queue (running on the completion workqueue) will pick up the event and
execute the notifier callback. This is done to avoid blocking on the
receiver thread.

There is one event queue per combination of target ID and target category.
This is done to ensure that notifier callbacks are executed in sequence for
events of the same target ID and target category. Callbacks can be executed
in parallel for events with a different combination of target ID and target
category.

Concurrency and Locking
-----------------------

Most of the concurrency related safety guarantees of the controller are
provided by the lower-level request transport layer. In addition to this,
event (un-)registration is guarded by its own lock.

Access to the controller state is guarded by the state lock. This lock is a
read/write semaphore. The reader part can be used to ensure that the state
does not change while functions depending on the state to stay the same
(e.g. |ssam_notifier_register|, |ssam_notifier_unregister|,
|ssam_request_sync_submit|, and derivatives) are executed and this guarantee
is not already provided otherwise (e.g. through |ssam_client_bind| or
|ssam_client_link|). The writer part guards any transitions that will change
the state, i.e. initialization, destruction, suspension, and resumption.

The controller state may be accessed (read-only) outside the state lock for
smoke-testing against invalid API usage (e.g. in |ssam_request_sync_submit|).
Note that such checks are not supposed to (and will not) protect against all
invalid usages, but rather aim to help catch them. In those cases, proper
variable access is ensured by employing ``WRITE_ONCE()`` and ``READ_ONCE()``.

Assuming any preconditions on the state not changing have been satisfied,
all non-initialization and non-shutdown functions may run concurrently with
each other. This includes |ssam_notifier_register|, |ssam_notifier_unregister|,
|ssam_request_sync_submit|, as well as all functions building on top of those.
