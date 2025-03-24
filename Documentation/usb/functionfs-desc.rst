======================
FunctionFS Descriptors
======================

Some of the descriptors that can be written to the FFS gadget are
described below. Device and configuration descriptors are handled
by the composite gadget and are not written by the user to the
FFS gadget.

Descriptors are written to the "ep0" file in the FFS gadget
following the descriptor header.

.. kernel-doc:: include/uapi/linux/usb/functionfs.h
   :doc: descriptors

Interface Descriptors
---------------------

Standard USB interface descriptors may be written. The class/subclass of the
most recent interface descriptor determines what type of class-specific
descriptors are accepted.

Class-Specific Descriptors
--------------------------

Class-specific descriptors are accepted only for the class/subclass of the
most recent interface descriptor. The following are some of the
class-specific descriptors that are supported.

DFU Functional Descriptor
~~~~~~~~~~~~~~~~~~~~~~~~~

When the interface class is USB_CLASS_APP_SPEC and the interface subclass
is USB_SUBCLASS_DFU, a DFU functional descriptor can be provided.
The DFU functional descriptor is a described in the USB specification for
Device Firmware Upgrade (DFU), version 1.1 as of this writing.

.. kernel-doc:: include/uapi/linux/usb/functionfs.h
   :doc: usb_dfu_functional_descriptor
