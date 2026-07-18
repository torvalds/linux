==============
dm-inlinecrypt
==============

Device-Mapper's "inlinecrypt" target provides transparent encryption of block devices
using the inline encryption hardware.

For a more detailed description of inline encryption, see:
https://docs.kernel.org/block/inline-encryption.html

Parameters::

	      <cipher> <key> <iv_offset> <device path> \
	      <offset> [<#opt_params> <opt_params>]

<cipher>
    Encryption cipher type.

    The cipher specifications format is::

       cipher

    Examples::

       aes-xts-plain64

    The cipher type corresponds to the encryption modes supported by
    inline crypto in the block layer. Currently, only
    BLK_ENCRYPTION_MODE_AES_256_XTS (i.e. aes-xts-plain64) is supported.

<key>
    Key used for encryption. It is encoded either as a hexadecimal number
    or it can be passed as <key_string> prefixed with single colon
    character (':') for keys residing in kernel keyring service.
    You can only use key sizes that are valid for the selected cipher.
    Note that the size in bytes of a valid key must be in bellow range.

        [BLK_CRYPTO_KEY_TYPE_RAW, BLK_CRYPTO_KEY_TYPE_HW_WRAPPED]

<key_string>
    The kernel keyring key is identified by string in following format:
    <key_size>:<keyring_type>:<key_description>.

<key_size>
    The encryption key size in bytes. The kernel key payload size must match
    the value passed in <key_size>.

<keyring_type>
    The type of the key inside the kernel keyring. It can be either 'logon',
    or 'trusted' kernel key type.

<key_description>
    The kernel keyring key description inlinecrypt target should look for
    when loading key of <keyring_type>.

<iv_offset>
    The IV offset is a sector count that is added to the sector number
    before creating the IV.

<device path>
    This is the device that is going to be used as backend and contains the
    encrypted data.  You can specify it as a path like /dev/xxx or a device
    number <major>:<minor>.

<offset>
    Starting sector within the device where the encrypted data begins.

<#opt_params>
    Number of optional parameters. If there are no optional parameters,
    the optional parameters section can be skipped or #opt_params can be zero.
    Otherwise #opt_params is the number of following arguments.

    Example of optional parameters section:
        keytype:raw allow_discards sector_size:4096 iv_large_sectors

<key_type>
    The type of the key as seen by the block layer, either standard or
    hardware-wrapped. The string is supplied in the table as <keytype:raw>
    or <keytype:hw-wrapped>.

allow_discards
    Block discard requests (a.k.a. TRIM) are passed through the inlinecrypt
    device. The default is to ignore discard requests.

    WARNING: Assess the specific security risks carefully before enabling this
    option.  For example, allowing discards on encrypted devices may lead to
    the leak of information about the ciphertext device (filesystem type,
    used space etc.) if the discarded blocks can be located easily on the
    device later.

sector_size:<bytes>
    Use <bytes> as the encryption unit instead of 512 bytes sectors.
    This option can be in range 512 - 4096 bytes and must be power of two.
    Virtual device will announce this size as a minimal IO and logical sector.

iv_large_sectors
    Use <sector_size>-based sector numbers for IV generation instead of
    512-byte sectors.

    For dm-inlinecrypt, this flag must be specified when <sector_size>
    is larger than 512 bytes. The legacy 512-byte-based IV behavior is
    not supported.

    When specified, if <sector_size> is 4096 bytes, plain64 IV for the
    second sector will be 1, and <iv_offset> must be a multiple of
    <sector_size> (in 512-byte units).

Example scripts
===============
Currently, dm-inlinecrypt devices must be set up directly using dmsetup.
There is no userspace support yet to integrate dm-inlinecrypt with LUKS
or cryptsetup. In particular, cryptsetup currently only supports
dm-crypt, and cannot be used to create dm-inlinecrypt mappings.

The following examples demonstrate how to create dm-inlinecrypt devices
using dmsetup

::

	#!/bin/sh
	# Create a inlinecrypt device using dmsetup
	dmsetup create inlinecrypt1 --table "0 `blockdev --getsz $1` inlinecrypt aes-xts-plain64 babebabebabebabebabebabebabebabebabebabebabebabebabebabebabebabe 0 0 $1 0 1 keytype:raw"

::

	#!/bin/sh
	# Create a inlinecrypt device using dmsetup when encryption key is stored in keyring service
	dmsetup create inlinecrypt2 --table "0 `blockdev --getsz $1` inlinecrypt aes-xts-plain64 :64:logon:fde:dminlinecrypt_test_key 0 0 $1 0 1 keytype:raw"

