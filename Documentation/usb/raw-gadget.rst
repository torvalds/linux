==============
USB Raw Gadget
==============

USB Raw Gadget is a kernel module that provides a userspace interface for
the USB Gadget subsystem. Essentially it allows to emulate USB devices
from userspace. Enabled with CONFIG_USB_RAW_GADGET. Raw Gadget is
currently a strictly debugging feature and shouldn't be used in
production, use GadgetFS instead.

Comparison to GadgetFS
~~~~~~~~~~~~~~~~~~~~~~

Raw Gadget is similar to GadgetFS, but provides a more low-level and
direct access to the USB Gadget layer for the userspace. The key
differences are:

1. Every USB request is passed to the userspace to get a response, while
   GadgetFS responds to some USB requests internally based on the provided
   descriptors. However note, that the UDC driver might respond to some
   requests on its own and never forward them to the Gadget layer.

2. GadgetFS performs some sanity checks on the provided USB descriptors,
   while Raw Gadget allows you to provide arbitrary data as responses to
   USB requests.

3. Raw Gadget provides a way to select a UDC device/driver to bind to,
   while GadgetFS currently binds to the first available UDC.

4. Raw Gadget explicitly exposes information about endpoints addresses and
   capabilities allowing a user to write UDC-agnostic gadgets.

5. Raw Gadget has ioctl-based interface instead of a filesystem-based one.

Userspace interface
~~~~~~~~~~~~~~~~~~~

To create a Raw Gadget instance open /dev/raw-gadget. Multiple raw-gadget
instances (bound to different UDCs) can be used at the same time. The
interaction with the opened file happens through the ioctl() calls, see
comments in include/uapi/linux/usb/raw_gadget.h for details.

The typical usage of Raw Gadget looks like:

1. Open Raw Gadget instance via /dev/raw-gadget.
2. Initialize the instance via USB_RAW_IOCTL_INIT.
3. Launch the instance with USB_RAW_IOCTL_RUN.
4. In a loop issue USB_RAW_IOCTL_EVENT_FETCH calls to receive events from
   Raw Gadget and react to those depending on what kind of USB device
   needs to be emulated.

Note, that some UDC drivers have fixed addresses assigned to endpoints, and
therefore arbitrary endpoint addresses can't be used in the descriptors.
Nevertheles, Raw Gadget provides a UDC-agnostic way to write USB gadgets.
Once a USB_RAW_EVENT_CONNECT event is received via USB_RAW_IOCTL_EVENT_FETCH,
the USB_RAW_IOCTL_EPS_INFO ioctl can be used to find out information about
endpoints that the UDC driver has. Based on that information, the user must
chose UDC endpoints that will be used for the gadget being emulated, and
properly assign addresses in endpoint descriptors.

You can find usage examples (along with a test suite) here:

https://github.com/xairy/raw-gadget

Internal details
~~~~~~~~~~~~~~~~

Currently every endpoint read/write ioctl submits a USB request and waits until
its completion. This is the desired mode for coverage-guided fuzzing (as we'd
like all USB request processing happen during the lifetime of a syscall),
and must be kept in the implementation. (This might be slow for real world
applications, thus the O_NONBLOCK improvement suggestion below.)

Potential future improvements
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

- Report more events (suspend, resume, etc.) through USB_RAW_IOCTL_EVENT_FETCH.

- Support O_NONBLOCK I/O.

- Support USB 3 features (accept SS endpoint companion descriptor when
  enabling endpoints; allow providing stream_id for bulk transfers).

- Support ISO transfer features (expose frame_number for completed requests).
