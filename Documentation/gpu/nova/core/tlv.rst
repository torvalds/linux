.. SPDX-License-Identifier: (GPL-2.0+ OR MIT)

==================================
TLV Tags in Nova Firmware Images
==================================

Nova firmware images use a Type-Length-Value (TLV) format to encapsulate
firmware components and metadata. The TLV file begins with a 4-byte "magic"
header that contains the string "NVFW".  Following the header is a sequence of
TLV blocks.

Each block consists of a 4-byte tag of ASCII characters, a 4-byte length
encoded as a little-endian unsigned integer, and a sequence of bytes, the size
of which is equal to the length rounded up to the next multiple of 4.

The driver code that reads the TLV and uses its contents is called the parser.
It is the responsibility of the parser to handle missing or malformed tags,
lengths, and values in the TLV.

::

    +------+------+------+------+
    |  'N' |  'V' |  'F' |  'W' |  Magic header
    +------+------+------+------+
    |  Tag (4 bytes, ASCII)     |  TLV block 0
    +---------------------------+
    |  Length (4 bytes, LE)     |
    +---------------------------+
    |                           |
    |  Value (length bytes,     |
    |  padded to 4-byte align)  |
    |                           |
    +---------------------------+
    |  Tag (4 bytes, ASCII)     |  TLV block 1
    +---------------------------+
    |  Length (4 bytes, LE)     |
    +---------------------------+
    |                           |
    |  Value (length bytes,     |
    |  padded to 4-byte align)  |
    |                           |
    +---------------------------+
    |         ...               |  More TLV blocks
    +---------------------------+

Tags and Length
===============
TLV tags are always four-character words, with all letters being upper case.
Duplicate tags are not allowed.

A TLV file may contain additional tags not described in this document.

Values
======
Values are one of four types.  The type is not encoded in the format; rather,
the parser expects a given tag to have a value of a given type.

1) Integers, encoded in 32-bit or 64-bit little-endian format.
2) Strings, encoded as-is and required to be only printable ASCII characters
   and without a null terminator.
3) An array of bytes, for binary data.
4) Boolean, encoded as single byte, with a value of 0 for False or 1 for True.

Common Tags
===========
These tags are shared across firmware types and carry the same meaning
wherever they appear.  Unlike the firmware-specific tags below, a common tag
is reserved: its meaning is fixed and may never be redefined for a particular
firmware type.

``VERS`` (string)
    Human-readable firmware version string.  Present in all TLV files.

A TLV image must contain either a single ``BLOB`` tag (firmware embedded
inline) or a ``SIZE``/``FILE`` pair (firmware stored in a separate file).

``BLOB`` (bytes)
    If the firmware microcode binary is stored in the TLV, this tag contains
    the actual firmware image bytes.

``FILE`` (string)
    If the firmware binary is stored as a separate file, this tag contains the
    name of that file, which is required to be in the same directory as the TLV,
    so no paths are allowed in the filename.  This tag is always paired with
    ``SIZE``, so as to allow the driver to pre-allocate the buffer before
    loading the file.

``SIZE`` (u32)
    Total size in bytes of the firmware image to be loaded from the companion
    file named by ``FILE``.  This tag is mandatory if ``FILE`` exists, so the
    size of the firmware image must be known when the TLV is created.  If the
    firmware image is updated and its size changes, then the TLV must be
    updated with it.

GSP Firmware Tags
=================
``SIGN`` (bytes)
    Cryptographic signature for the GSP firmware.

``BLID`` (string)
    The build ID, extracted from the ".note.gnu.build-id" section.

Booter Firmware Tags
====================
``DAOF`` (u32) - ``os_data_offset``
    OS data section offset within the firmware image (absolute byte offset).
    Maps to the DMEM load source.

``DASZ`` (u32) - ``os_data_size``
    OS data section size in bytes.

``CDOF`` (u32) - ``os_code_offset``
    OS code section offset within the firmware image (absolute byte offset).
    Maps to the non-secure IMEM load source.

``CDSZ`` (u32) - ``os_code_size``
    OS code section size in bytes.

``PLOC`` (u32) - ``patch_loc``
    Signature patch location -- byte offset within the firmware image where the
    selected signature should be written.

``FUSE`` (u32) - ``fuse_version``
    Fuse version of the firmware, used with the hardware fuse register to
    select the correct signature index.

``ENID`` (u32) - ``engine_id``
    Engine ID mask identifying the falcon engine this firmware targets.

``UCID`` (u32) - ``ucode_id``
    Microcode ID used together with the engine ID to query hardware signature
    fuse registers.

``A0CO`` (u32) - ``app0_code_offset``
    App0 code offset -- start of the secure code region within the firmware
    image. Used as the IMEM secure section source.

``A0CS`` (u32) - ``app0_code_size``
    App0 code size in bytes.

``NSIG`` (u32) - ``num_sigs``
    Number of signatures included in the ``SIGN`` tag.

``SIGN`` (bytes)
    Concatenated array of firmware signatures. The size of each signature is
    the total length of the ``SIGN`` value divided by ``NSIG``. The correct
    signature is selected using the fuse-version-derived index.

Generic Bootloader Tags
=======================
``CDSZ`` (u32) - ``code_size``
    Size in bytes of the bootloader code to copy from the ``BLOB`` tag and
    PIO-load into falcon IMEM.

``STRT`` (u32) - ``start_tag``
    Start tag identifying the IMEM block where execution begins.  The falcon
    boot address is derived as ``start_tag << 8``.

GSP Bootloader Tags
===================
``CDOF`` (u32) - ``code_offset``
    Offset within the firmware image at which the code section starts.

``DAOF`` (u32) - ``data_offset``
    Offset within the firmware image at which the data section starts.

``MFOF`` (u32) - ``manifest_offset``
    Offset within the firmware image at which the manifest starts.

``APPV`` (u32) - ``app_version``
    Application version of the firmware.

FMC Firmware Tags
=================
``HASH`` (bytes)
    SHA-384 hash of the FMC firmware, exactly 48 bytes long.

``PKEY`` (bytes)
    Public key used to verify the FMC firmware. At most 384 bytes (RSA-3072),
    but may be shorter.

``SIGN`` (bytes)
    Signature of the FMC firmware. At most 384 bytes (RSA-3072), but may
    be shorter.
