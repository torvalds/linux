.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

========================================
Describing and referring to LEDs in ACPI
========================================

Individual LEDs are described by hierarchical data extension [5] nodes under the
device node, the LED driver chip. The "reg" property in the LED specific nodes
tells the numerical ID of each individual LED output to which the LEDs are
connected. [leds] The hierarchical data nodes are named "led@X", where X is the
number of the LED output.

Referring to LEDs in Device tree is documented in [video-interfaces], in
"flash-leds" property documentation. In short, LEDs are directly referred to by
using phandles.

While Device tree allows referring to any node in the tree [devicetree], in
ACPI references are limited to device nodes only [acpi]. For this reason using
the same mechanism on ACPI is not possible. A mechanism to refer to non-device
ACPI nodes is documented in [data-node-ref].

ACPI allows (as does DT) using integer arguments after the reference. A
combination of the LED driver device reference and an integer argument,
referring to the "reg" property of the relevant LED, is used to identify
individual LEDs. The value of the "reg" property is a contract between the
firmware and software, it uniquely identifies the LED driver outputs.

Under the LED driver device, The first hierarchical data extension package list
entry shall contain the string "led@" followed by the number of the LED,
followed by the referred object name. That object shall be named "LED" followed
by the number of the LED.

Example
=======

An ASL example of a camera sensor device and a LED driver device for two LEDs is
show below. Objects not relevant for LEDs or the references to them have been
omitted. ::

	Device (LED)
	{
		Name (_DSD, Package () {
			ToUUID("dbb8e3e6-5886-4ba6-8795-1319f52a966b"),
			Package () {
				Package () { "led@0", LED0 },
				Package () { "led@1", LED1 },
			}
		})
		Name (LED0, Package () {
			ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
			Package () {
				Package () { "reg", 0 },
				Package () { "flash-max-microamp", 1000000 },
				Package () { "flash-timeout-us", 200000 },
				Package () { "led-max-microamp", 100000 },
				Package () { "label", "white:flash" },
			}
		})
		Name (LED1, Package () {
			ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
			Package () {
				Package () { "reg", 1 },
				Package () { "led-max-microamp", 10000 },
				Package () { "label", "red:indicator" },
			}
		})
	}

	Device (SEN)
	{
		Name (_DSD, Package () {
			ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
			Package () {
				Package () {
					"flash-leds",
					Package () { ^LED, "led@0", ^LED, "led@1" },
				}
			}
		})
	}

where
::

	LED	LED driver device
	LED0	First LED
	LED1	Second LED
	SEN	Camera sensor device (or another device the LED is related to)

References
==========

[acpi] Advanced Configuration and Power Interface Specification.
    https://uefi.org/specifications/ACPI/6.4/, referenced 2021-11-30.

[data-node-ref] Documentation/firmware-guide/acpi/dsd/data-node-references.rst

[devicetree] Devicetree. https://www.devicetree.org, referenced 2019-02-21.

[dsd-guide] DSD Guide.
    https://github.com/UEFI/DSD-Guide/blob/main/dsd-guide.adoc, referenced
    2021-11-30.

[leds] Documentation/devicetree/bindings/leds/common.yaml

[video-interfaces] Documentation/devicetree/bindings/media/video-interfaces.yaml
