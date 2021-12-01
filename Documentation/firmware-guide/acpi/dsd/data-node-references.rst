.. SPDX-License-Identifier: GPL-2.0
.. include:: <isonum.txt>

===================================
Referencing hierarchical data nodes
===================================

:Copyright: |copy| 2018, 2021 Intel Corporation
:Author: Sakari Ailus <sakari.ailus@linux.intel.com>

ACPI in general allows referring to device objects in the tree only.
Hierarchical data extension nodes may not be referred to directly, hence this
document defines a scheme to implement such references.

A reference consist of the device object name followed by one or more
hierarchical data extension [1] keys. Specifically, the hierarchical data
extension node which is referred to by the key shall lie directly under the
parent object i.e. either the device object or another hierarchical data
extension node.

The keys in the hierarchical data nodes shall consist of the name of the node,
"@" character and the number of the node in hexadecimal notation (without pre-
or postfixes). The same ACPI object shall include the _DSD property extension
with a property "reg" that shall have the same numerical value as the number of
the node.

In case a hierarchical data extensions node has no numerical value, then the
"reg" property shall be omitted from the ACPI object's _DSD properties and the
"@" character and the number shall be omitted from the hierarchical data
extension key.


Example
=======

In the ASL snippet below, the "reference" _DSD property [2] contains a
device object reference to DEV0 and under that device object, a
hierarchical data extension key "node@1" referring to the NOD1 object
and lastly, a hierarchical data extension key "anothernode" referring to
the ANOD object which is also the final target node of the reference.
::

	Device (DEV0)
	{
	    Name (_DSD, Package () {
		ToUUID("dbb8e3e6-5886-4ba6-8795-1319f52a966b"),
		Package () {
		    Package () { "node@0", "NOD0" },
		    Package () { "node@1", "NOD1" },
		}
	    })
	    Name (NOD0, Package() {
		ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
		Package () {
		    Package () { "reg", 0 },
		    Package () { "random-property", 3 },
		}
	    })
	    Name (NOD1, Package() {
		ToUUID("dbb8e3e6-5886-4ba6-8795-1319f52a966b"),
		Package () {
		    Package () { "reg", 1 },
		    Package () { "anothernode", "ANOD" },
		}
	    })
	    Name (ANOD, Package() {
		ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
		Package () {
		    Package () { "random-property", 0 },
		}
	    })
	}

	Device (DEV1)
	{
	    Name (_DSD, Package () {
		ToUUID("daffd814-6eba-4d8c-8a91-bc9bbf4aa301"),
		Package () {
		    Package () {
			"reference", Package () {
			    ^DEV0, "node@1", "anothernode"
			}
		    },
		}
	    })
	}

Please also see a graph example in
Documentation/firmware-guide/acpi/dsd/graph.rst.

References
==========

[1] Hierarchical Data Extension UUID For _DSD.
<https://www.uefi.org/sites/default/files/resources/_DSD-hierarchical-data-extension-UUID-v1.1.pdf>,
referenced 2018-07-17.

[2] Device Properties UUID For _DSD.
<https://www.uefi.org/sites/default/files/resources/_DSD-device-properties-UUID.pdf>,
referenced 2016-10-04.
