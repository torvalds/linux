====================
USB port LED trigger
====================

This LED trigger can be used for signalling to the user a presence of USB device
in a given port. It simply turns on LED when device appears and turns it off
when it disappears.

It requires selecting USB ports that should be observed. All available ones are
listed as separated entries in a "ports" subdirectory. Selecting is handled by
echoing "1" to a chosen port.

Please note that this trigger allows selecting multiple USB ports for a single
LED.

This can be useful in two cases:

1) Device with single USB LED and few physical ports
====================================================

In such a case LED will be turned on as long as there is at least one connected
USB device.

2) Device with a physical port handled by few controllers
=========================================================

Some devices may have one controller per PHY standard. E.g. USB 3.0 physical
port may be handled by ohci-platform, ehci-platform and xhci-hcd. If there is
only one LED user will most likely want to assign ports from all 3 hubs.


This trigger can be activated from user space on led class devices as shown
below::

  echo usbport > trigger

This adds sysfs attributes to the LED that are documented in:
Documentation/ABI/testing/sysfs-class-led-trigger-usbport

Example use-case::

  echo usbport > trigger
  echo 1 > ports/usb1-port1
  echo 1 > ports/usb2-port1
  cat ports/usb1-port1
  echo 0 > ports/usb1-port1
