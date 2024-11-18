.. SPDX-License-Identifier: GPL-2.0

========
HDMI CEC
========

Supported hardware in mainline
==============================

HDMI Transmitters:

- Exynos4
- Exynos5
- STIH4xx HDMI CEC
- V4L2 adv7511 (same HW, but a different driver from the drm adv7511)
- stm32
- Allwinner A10 (sun4i)
- Raspberry Pi
- dw-hdmi (Synopsis IP)
- amlogic (meson ao-cec and ao-cec-g12a)
- drm adv7511/adv7533
- omap4
- tegra
- rk3288, rk3399
- tda998x
- DisplayPort CEC-Tunneling-over-AUX on i915, nouveau and amdgpu
- ChromeOS EC CEC
- CEC for SECO boards (UDOO x86).
- Chrontel CH7322


HDMI Receivers:

- adv7604/11/12
- adv7842
- tc358743

USB Dongles (see below for additional information on how to use these
dongles):

- Pulse-Eight: the pulse8-cec driver implements the following module option:
  ``persistent_config``: by default this is off, but when set to 1 the driver
  will store the current settings to the device's internal eeprom and restore
  it the next time the device is connected to the USB port.

- RainShadow Tech. Note: this driver does not support the persistent_config
  module option of the Pulse-Eight driver. The hardware supports it, but I
  have no plans to add this feature. But I accept patches :-)

- Extron DA HD 4K PLUS HDMI Distribution Amplifier. See
  :ref:`extron_da_hd_4k_plus` for more information.

Miscellaneous:

- vivid: emulates a CEC receiver and CEC transmitter.
  Can be used to test CEC applications without actual CEC hardware.

- cec-gpio. If the CEC pin is hooked up to a GPIO pin then
  you can control the CEC line through this driver. This supports error
  injection as well.

- cec-gpio and Allwinner A10 (or any other driver that uses the CEC pin
  framework to drive the CEC pin directly): the CEC pin framework uses
  high-resolution timers. These timers are affected by NTP daemons that
  speed up or slow down the clock to sync with the official time. The
  chronyd server will by default increase or decrease the clock by
  1/12th. This will cause the CEC timings to go out of spec. To fix this,
  add a 'maxslewrate 40000' line to chronyd.conf. This limits the clock
  frequency change to 1/25th, which keeps the CEC timings within spec.


Utilities
=========

Utilities are available here: https://git.linuxtv.org/v4l-utils.git

``utils/cec-ctl``: control a CEC device

``utils/cec-compliance``: test compliance of a remote CEC device

``utils/cec-follower``: emulate a CEC follower device

Note that ``cec-ctl`` has support for the CEC Hospitality Profile as is
used in some hotel displays. See http://www.htng.org.

Note that the libcec library (https://github.com/Pulse-Eight/libcec) supports
the linux CEC framework.

If you want to get the CEC specification, then look at the References of
the HDMI wikipedia page: https://en.wikipedia.org/wiki/HDMI. CEC is part
of the HDMI specification. HDMI 1.3 is freely available (very similar to
HDMI 1.4 w.r.t. CEC) and should be good enough for most things.


DisplayPort to HDMI Adapters with working CEC
=============================================

Background: most adapters do not support the CEC Tunneling feature,
and of those that do many did not actually connect the CEC pin.
Unfortunately, this means that while a CEC device is created, it
is actually all alone in the world and will never be able to see other
CEC devices.

This is a list of known working adapters that have CEC Tunneling AND
that properly connected the CEC pin. If you find adapters that work
but are not in this list, then drop me a note.

To test: hook up your DP-to-HDMI adapter to a CEC capable device
(typically a TV), then run::

	cec-ctl --playback	# Configure the PC as a CEC Playback device
	cec-ctl -S		# Show the CEC topology

The ``cec-ctl -S`` command should show at least two CEC devices,
ourselves and the CEC device you are connected to (i.e. typically the TV).

General note: I have only seen this work with the Parade PS175, PS176 and
PS186 chipsets and the MegaChips 2900. While MegaChips 28x0 claims CEC support,
I have never seen it work.

USB-C to HDMI
-------------

Samsung Multiport Adapter EE-PW700: https://www.samsung.com/ie/support/model/EE-PW700BBEGWW/

Kramer ADC-U31C/HF: https://www.kramerav.com/product/ADC-U31C/HF

Club3D CAC-2504: https://www.club-3d.com/en/detail/2449/usb_3.1_type_c_to_hdmi_2.0_uhd_4k_60hz_active_adapter/

DisplayPort to HDMI
-------------------

Club3D CAC-1080: https://www.club-3d.com/en/detail/2442/displayport_1.4_to_hdmi_2.0b_hdr/

CableCreation (SKU: CD0712): https://www.cablecreation.com/products/active-displayport-to-hdmi-adapter-4k-hdr

HP DisplayPort to HDMI True 4k Adapter (P/N 2JA63AA): https://www.hp.com/us-en/shop/pdp/hp-displayport-to-hdmi-true-4k-adapter

Mini-DisplayPort to HDMI
------------------------

Club3D CAC-1180: https://www.club-3d.com/en/detail/2443/mini_displayport_1.4_to_hdmi_2.0b_hdr/

Note that passive adapters will never work, you need an active adapter.

The Club3D adapters in this list are all MegaChips 2900 based. Other Club3D adapters
are PS176 based and do NOT have the CEC pin hooked up, so only the three Club3D
adapters above are known to work.

I suspect that MegaChips 2900 based designs in general are likely to work
whereas with the PS176 it is more hit-and-miss (mostly miss). The PS186 is
likely to have the CEC pin hooked up, it looks like they changed the reference
design for that chipset.


USB CEC Dongles
===============

These dongles appear as ``/dev/ttyACMX`` devices and need the ``inputattach``
utility to create the ``/dev/cecX`` devices. Support for the Pulse-Eight
has been added to ``inputattach`` 1.6.0. Support for the Rainshadow Tech has
been added to ``inputattach`` 1.6.1.

You also need udev rules to automatically start systemd services::

	SUBSYSTEM=="tty", KERNEL=="ttyACM[0-9]*", ATTRS{idVendor}=="2548", ATTRS{idProduct}=="1002", ACTION=="add", TAG+="systemd", ENV{SYSTEMD_WANTS}+="pulse8-cec-inputattach@%k.service"
	SUBSYSTEM=="tty", KERNEL=="ttyACM[0-9]*", ATTRS{idVendor}=="2548", ATTRS{idProduct}=="1001", ACTION=="add", TAG+="systemd", ENV{SYSTEMD_WANTS}+="pulse8-cec-inputattach@%k.service"
	SUBSYSTEM=="tty", KERNEL=="ttyACM[0-9]*", ATTRS{idVendor}=="04d8", ATTRS{idProduct}=="ff59", ACTION=="add", TAG+="systemd", ENV{SYSTEMD_WANTS}+="rainshadow-cec-inputattach@%k.service"

and these systemd services:

For Pulse-Eight make /lib/systemd/system/pulse8-cec-inputattach@.service::

	[Unit]
	Description=inputattach for pulse8-cec device on %I

	[Service]
	Type=simple
	ExecStart=/usr/bin/inputattach --pulse8-cec /dev/%I

For the RainShadow Tech make /lib/systemd/system/rainshadow-cec-inputattach@.service::

	[Unit]
	Description=inputattach for rainshadow-cec device on %I

	[Service]
	Type=simple
	ExecStart=/usr/bin/inputattach --rainshadow-cec /dev/%I


For proper suspend/resume support create: /lib/systemd/system/restart-cec-inputattach.service::

	[Unit]
	Description=restart inputattach for cec devices
	After=suspend.target

	[Service]
	Type=forking
	ExecStart=/bin/bash -c 'for d in /dev/serial/by-id/usb-Pulse-Eight*; do /usr/bin/inputattach --daemon --pulse8-cec $d; done; for d in /dev/serial/by-id/usb-RainShadow_Tech*; do /usr/bin/inputattach --daemon --rainshadow-cec $d; done'

	[Install]
	WantedBy=suspend.target

And run ``systemctl enable restart-cec-inputattach``.

To automatically set the physical address of the CEC device whenever the
EDID changes, you can use ``cec-ctl`` with the ``-E`` option::

	cec-ctl -E /sys/class/drm/card0-DP-1/edid

This assumes the dongle is connected to the card0-DP-1 output (``xrandr`` will tell
you which output is used) and it will poll for changes to the EDID and update
the Physical Address whenever they occur.

To automatically run this command you can use cron. Edit crontab with
``crontab -e`` and add this line::

	@reboot /usr/local/bin/cec-ctl -E /sys/class/drm/card0-DP-1/edid

This only works for display drivers that expose the EDID in ``/sys/class/drm``,
such as the i915 driver.


CEC Without HPD
===============

Some displays when in standby mode have no HDMI Hotplug Detect signal, but
CEC is still enabled so connected devices can send an <Image View On> CEC
message in order to wake up such displays. Unfortunately, not all CEC
adapters can support this. An example is the Odroid-U3 SBC that has a
level-shifter that is powered off when the HPD signal is low, thus
blocking the CEC pin. Even though the SoC can use CEC without a HPD,
the level-shifter will prevent this from functioning.

There is a CEC capability flag to signal this: ``CEC_CAP_NEEDS_HPD``.
If set, then the hardware cannot wake up displays with this behavior.

Note for CEC application implementers: the <Image View On> message must
be the first message you send, don't send any other messages before.
Certain very bad but unfortunately not uncommon CEC implementations
get very confused if they receive anything else but this message and
they won't wake up.

When writing a driver it can be tricky to test this. There are two
ways to do this:

1) Get a Pulse-Eight USB CEC dongle, connect an HDMI cable from your
   device to the Pulse-Eight, but do not connect the Pulse-Eight to
   the display.

   Now configure the Pulse-Eight dongle::

	cec-ctl -p0.0.0.0 --tv

   and start monitoring::

	sudo cec-ctl -M

   On the device you are testing run::

	cec-ctl --playback

   It should report a physical address of f.f.f.f. Now run this
   command::

	cec-ctl -t0 --image-view-on

   The Pulse-Eight should see the <Image View On> message. If not,
   then something (hardware and/or software) is preventing the CEC
   message from going out.

   To make sure you have the wiring correct just connect the
   Pulse-Eight to a CEC-enabled display and run the same command
   on your device: now there is a HPD, so you should see the command
   arriving at the Pulse-Eight.

2) If you have another linux device supporting CEC without HPD, then
   you can just connect your device to that device. Yes, you can connect
   two HDMI outputs together. You won't have a HPD (which is what we
   want for this test), but the second device can monitor the CEC pin.

   Otherwise use the same commands as in 1.

If CEC messages do not come through when there is no HPD, then you
need to figure out why. Typically it is either a hardware restriction
or the software powers off the CEC core when the HPD goes low. The
first cannot be corrected of course, the second will likely required
driver changes.


Microcontrollers & CEC
======================

We have seen some CEC implementations in displays that use a microcontroller
to sample the bus. This does not have to be a problem, but some implementations
have timing issues. This is hard to discover unless you can hook up a low-level
CEC debugger (see the next section).

You will see cases where the CEC transmitter holds the CEC line high or low for
a longer time than is allowed. For directed messages this is not a problem since
if that happens the message will not be Acked and it will be retransmitted.
For broadcast messages no such mechanism exists.

It's not clear what to do about this. It is probably wise to transmit some
broadcast messages twice to reduce the chance of them being lost. Specifically
<Standby> and <Active Source> are candidates for that.


Making a CEC debugger
=====================

By using a Raspberry Pi 4B and some cheap components you can make
your own low-level CEC debugger.

The critical component is one of these HDMI female-female passthrough connectors
(full soldering type 1):

https://elabbay.myshopify.com/collections/camera/products/hdmi-af-af-v1a-hdmi-type-a-female-to-hdmi-type-a-female-pass-through-adapter-breakout-board?variant=45533926147

The video quality is variable and certainly not enough to pass-through 4kp60
(594 MHz) video. You might be able to support 4kp30, but more likely you will
be limited to 1080p60 (148.5 MHz). But for CEC testing that is fine.

You need a breadboard and some breadboard wires:

http://www.dx.com/p/diy-40p-male-to-female-male-to-male-female-to-female-dupont-line-wire-3pcs-356089#.WYLOOXWGN7I

If you want to monitor the HPD and/or 5V lines as well, then you need one of
these 5V to 3.3V level shifters:

https://www.adafruit.com/product/757

(This is just where I got these components, there are many other places you
can get similar things).

The ground pin of the HDMI connector needs to be connected to a ground
pin of the Raspberry Pi, of course.

The CEC pin of the HDMI connector needs to be connected to these pins:
GPIO 6 and GPIO 7. The optional HPD pin of the HDMI connector should
be connected via the level shifter to these pins: GPIO 23 and GPIO 12.
The optional 5V pin of the HDMI connector should be connected via the
level shifter to these pins: GPIO 25 and GPIO 22. Monitoring the HPD and
5V lines is not necessary, but it is helpful.

This device tree addition in ``arch/arm/boot/dts/bcm2711-rpi-4-b.dts``
will hook up the cec-gpio driver correctly::

	cec@6 {
		compatible = "cec-gpio";
		cec-gpios = <&gpio 6 (GPIO_ACTIVE_HIGH|GPIO_OPEN_DRAIN)>;
		hpd-gpios = <&gpio 23 GPIO_ACTIVE_HIGH>;
		v5-gpios = <&gpio 25 GPIO_ACTIVE_HIGH>;
	};

	cec@7 {
		compatible = "cec-gpio";
		cec-gpios = <&gpio 7 (GPIO_ACTIVE_HIGH|GPIO_OPEN_DRAIN)>;
		hpd-gpios = <&gpio 12 GPIO_ACTIVE_HIGH>;
		v5-gpios = <&gpio 22 GPIO_ACTIVE_HIGH>;
	};

If you haven't hooked up the HPD and/or 5V lines, then just delete those
lines.

This dts change will enable two cec GPIO devices: I typically use one to
send/receive CEC commands and the other to monitor. If you monitor using
an unconfigured CEC adapter then it will use GPIO interrupts which makes
monitoring very accurate.

If you just want to monitor traffic, then a single instance is sufficient.
The minimum configuration is one HDMI female-female passthrough connector
and two female-female breadboard wires: one for connecting the HDMI ground
pin to a ground pin on the Raspberry Pi, and the other to connect the HDMI
CEC pin to GPIO 6 on the Raspberry Pi.

The documentation on how to use the error injection is here: :ref:`cec_pin_error_inj`.

``cec-ctl --monitor-pin`` will do low-level CEC bus sniffing and analysis.
You can also store the CEC traffic to file using ``--store-pin`` and analyze
it later using ``--analyze-pin``.

You can also use this as a full-fledged CEC device by configuring it
using ``cec-ctl --tv -p0.0.0.0`` or ``cec-ctl --playback -p1.0.0.0``.

.. _extron_da_hd_4k_plus:

Extron DA HD 4K PLUS CEC Adapter driver
=======================================

This driver is for the Extron DA HD 4K PLUS series of HDMI Distribution
Amplifiers: https://www.extron.com/product/dahd4kplusseries

The 2, 4 and 6 port models are supported.

Firmware version 1.02.0001 or higher is required.

Note that older Extron hardware revisions have a problem with the CEC voltage,
which may mean that CEC will not work. This is fixed in hardware revisions
E34814 and up.

The CEC support has two modes: the first is a manual mode where userspace has
to manually control CEC for the HDMI Input and all HDMI Outputs. While this gives
full control, it is also complicated.

The second mode is an automatic mode, which is selected if the module option
``vendor_id`` is set. In that case the driver controls CEC and CEC messages
received in the input will be distributed to the outputs. It is still possible
to use the /dev/cecX devices to talk to the connected devices directly, but it is
the driver that configures everything and deals with things like Hotplug Detect
changes.

The driver also takes care of the EDIDs: /dev/videoX devices are created to
read the EDIDs and (for the HDMI Input port) to set the EDID.

By default userspace is responsible to set the EDID for the HDMI Input
according to the EDIDs of the connected displays. But if the ``manufacturer_name``
module option is set, then the driver will take care of setting the EDID
of the HDMI Input based on the supported resolutions of the connected displays.
Currently the driver only supports resolutions 1080p60 and 4kp60: if all connected
displays support 4kp60, then it will advertise 4kp60 on the HDMI input, otherwise
it will fall back to an EDID that just reports 1080p60.

The status of the Extron is reported in ``/sys/kernel/debug/cec/cecX/status``.

The extron-da-hd-4k-plus driver implements the following module options:

``debug``
---------

If set to 1, then all serial port traffic is shown.

``vendor_id``
-------------

The CEC Vendor ID to report to connected displays.

If set, then the driver will take care of distributing CEC messages received
on the input to the HDMI outputs. This is done for the following CEC messages:

- <Standby>
- <Image View On> and <Text View On>
- <Give Device Power Status>
- <Set System Audio Mode>
- <Request Current Latency>

If not set, then userspace is responsible for this, and it will have to
configure the CEC devices for HDMI Input and the HDMI Outputs manually.

``manufacturer_name``
---------------------

A three character manufacturer name that is used in the EDID for the HDMI
Input. If not set, then userspace is reponsible for configuring an EDID.
If set, then the driver will update the EDID automatically based on the
resolutions supported by the connected displays, and it will not be possible
anymore to manually set the EDID for the HDMI Input.

``hpd_never_low``
-----------------

If set, then the Hotplug Detect pin of the HDMI Input will always be high,
even if nothing is connected to the HDMI Outputs. If not set (the default)
then the Hotplug Detect pin of the HDMI input will go low if all the detected
Hotplug Detect pins of the HDMI Outputs are also low.

This option may be changed dynamically.
