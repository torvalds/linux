=============================
Netlink interface for ethtool
=============================


Basic information
=================

Netlink interface for ethtool uses generic netlink family ``ethtool``
(userspace application should use macros ``ETHTOOL_GENL_NAME`` and
``ETHTOOL_GENL_VERSION`` defined in ``<linux/ethtool_netlink.h>`` uapi
header). This family does not use a specific header, all information in
requests and replies is passed using netlink attributes.

The ethtool netlink interface uses extended ACK for error and warning
reporting, userspace application developers are encouraged to make these
messages available to user in a suitable way.

Requests can be divided into three categories: "get" (retrieving information),
"set" (setting parameters) and "action" (invoking an action).

All "set" and "action" type requests require admin privileges
(``CAP_NET_ADMIN`` in the namespace). Most "get" type requests are allowed for
anyone but there are exceptions (where the response contains sensitive
information). In some cases, the request as such is allowed for anyone but
unprivileged users have attributes with sensitive information (e.g.
wake-on-lan password) omitted.


Conventions
===========

Attributes which represent a boolean value usually use NLA_U8 type so that we
can distinguish three states: "on", "off" and "not present" (meaning the
information is not available in "get" requests or value is not to be changed
in "set" requests). For these attributes, the "true" value should be passed as
number 1 but any non-zero value should be understood as "true" by recipient.
In the tables below, "bool" denotes NLA_U8 attributes interpreted in this way.

In the message structure descriptions below, if an attribute name is suffixed
with "+", parent nest can contain multiple attributes of the same type. This
implements an array of entries.


Request header
==============

Each request or reply message contains a nested attribute with common header.
Structure of this header is

  ==============================  ======  =============================
  ``ETHTOOL_A_HEADER_DEV_INDEX``  u32     device ifindex
  ``ETHTOOL_A_HEADER_DEV_NAME``   string  device name
  ``ETHTOOL_A_HEADER_FLAGS``      u32     flags common for all requests
  ==============================  ======  =============================

``ETHTOOL_A_HEADER_DEV_INDEX`` and ``ETHTOOL_A_HEADER_DEV_NAME`` identify the
device message relates to. One of them is sufficient in requests, if both are
used, they must identify the same device. Some requests, e.g. global string
sets, do not require device identification. Most ``GET`` requests also allow
dump requests without device identification to query the same information for
all devices providing it (each device in a separate message).

``ETHTOOL_A_HEADER_FLAGS`` is a bitmap of request flags common for all request
types. The interpretation of these flags is the same for all request types but
the flags may not apply to requests. Recognized flags are:

  =================================  ===================================
  ``ETHTOOL_FLAG_COMPACT_BITSETS``   use compact format bitsets in reply
  ``ETHTOOL_FLAG_OMIT_REPLY``        omit optional reply (_SET and _ACT)
  =================================  ===================================

New request flags should follow the general idea that if the flag is not set,
the behaviour is backward compatible, i.e. requests from old clients not aware
of the flag should be interpreted the way the client expects. A client must
not set flags it does not understand.


Bit sets
========

For short bitmaps of (reasonably) fixed length, standard ``NLA_BITFIELD32``
type is used. For arbitrary length bitmaps, ethtool netlink uses a nested
attribute with contents of one of two forms: compact (two binary bitmaps
representing bit values and mask of affected bits) and bit-by-bit (list of
bits identified by either index or name).

Verbose (bit-by-bit) bitsets allow sending symbolic names for bits together
with their values which saves a round trip (when the bitset is passed in a
request) or at least a second request (when the bitset is in a reply). This is
useful for one shot applications like traditional ethtool command. On the
other hand, long running applications like ethtool monitor (displaying
notifications) or network management daemons may prefer fetching the names
only once and using compact form to save message size. Notifications from
ethtool netlink interface always use compact form for bitsets.

A bitset can represent either a value/mask pair (``ETHTOOL_A_BITSET_NOMASK``
not set) or a single bitmap (``ETHTOOL_A_BITSET_NOMASK`` set). In requests
modifying a bitmap, the former changes the bit set in mask to values set in
value and preserves the rest; the latter sets the bits set in the bitmap and
clears the rest.

Compact form: nested (bitset) atrribute contents:

  ============================  ======  ============================
  ``ETHTOOL_A_BITSET_NOMASK``   flag    no mask, only a list
  ``ETHTOOL_A_BITSET_SIZE``     u32     number of significant bits
  ``ETHTOOL_A_BITSET_VALUE``    binary  bitmap of bit values
  ``ETHTOOL_A_BITSET_MASK``     binary  bitmap of valid bits
  ============================  ======  ============================

Value and mask must have length at least ``ETHTOOL_A_BITSET_SIZE`` bits
rounded up to a multiple of 32 bits. They consist of 32-bit words in host byte
order, words ordered from least significant to most significant (i.e. the same
way as bitmaps are passed with ioctl interface).

For compact form, ``ETHTOOL_A_BITSET_SIZE`` and ``ETHTOOL_A_BITSET_VALUE`` are
mandatory. ``ETHTOOL_A_BITSET_MASK`` attribute is mandatory if
``ETHTOOL_A_BITSET_NOMASK`` is not set (bitset represents a value/mask pair);
if ``ETHTOOL_A_BITSET_NOMASK`` is not set, ``ETHTOOL_A_BITSET_MASK`` is not
allowed (bitset represents a single bitmap.

Kernel bit set length may differ from userspace length if older application is
used on newer kernel or vice versa. If userspace bitmap is longer, an error is
issued only if the request actually tries to set values of some bits not
recognized by kernel.

Bit-by-bit form: nested (bitset) attribute contents:

 +------------------------------------+--------+-----------------------------+
 | ``ETHTOOL_A_BITSET_NOMASK``        | flag   | no mask, only a list        |
 +------------------------------------+--------+-----------------------------+
 | ``ETHTOOL_A_BITSET_SIZE``          | u32    | number of significant bits  |
 +------------------------------------+--------+-----------------------------+
 | ``ETHTOOL_A_BITSET_BITS``          | nested | array of bits               |
 +-+----------------------------------+--------+-----------------------------+
 | | ``ETHTOOL_A_BITSET_BITS_BIT+``   | nested | one bit                     |
 +-+-+--------------------------------+--------+-----------------------------+
 | | | ``ETHTOOL_A_BITSET_BIT_INDEX`` | u32    | bit index (0 for LSB)       |
 +-+-+--------------------------------+--------+-----------------------------+
 | | | ``ETHTOOL_A_BITSET_BIT_NAME``  | string | bit name                    |
 +-+-+--------------------------------+--------+-----------------------------+
 | | | ``ETHTOOL_A_BITSET_BIT_VALUE`` | flag   | present if bit is set       |
 +-+-+--------------------------------+--------+-----------------------------+

Bit size is optional for bit-by-bit form. ``ETHTOOL_A_BITSET_BITS`` nest can
only contain ``ETHTOOL_A_BITSET_BITS_BIT`` attributes but there can be an
arbitrary number of them.  A bit may be identified by its index or by its
name. When used in requests, listed bits are set to 0 or 1 according to
``ETHTOOL_A_BITSET_BIT_VALUE``, the rest is preserved. A request fails if
index exceeds kernel bit length or if name is not recognized.

When ``ETHTOOL_A_BITSET_NOMASK`` flag is present, bitset is interpreted as
a simple bitmap. ``ETHTOOL_A_BITSET_BIT_VALUE`` attributes are not used in
such case. Such bitset represents a bitmap with listed bits set and the rest
zero.

In requests, application can use either form. Form used by kernel in reply is
determined by ``ETHTOOL_FLAG_COMPACT_BITSETS`` flag in flags field of request
header. Semantics of value and mask depends on the attribute.


List of message types
=====================

All constants identifying message types use ``ETHTOOL_CMD_`` prefix and suffix
according to message purpose:

  ==============    ======================================
  ``_GET``          userspace request to retrieve data
  ``_SET``          userspace request to set data
  ``_ACT``          userspace request to perform an action
  ``_GET_REPLY``    kernel reply to a ``GET`` request
  ``_SET_REPLY``    kernel reply to a ``SET`` request
  ``_ACT_REPLY``    kernel reply to an ``ACT`` request
  ``_NTF``          kernel notification
  ==============    ======================================

``GET`` requests are sent by userspace applications to retrieve device
information. They usually do not contain any message specific attributes.
Kernel replies with corresponding "GET_REPLY" message. For most types, ``GET``
request with ``NLM_F_DUMP`` and no device identification can be used to query
the information for all devices supporting the request.

If the data can be also modified, corresponding ``SET`` message with the same
layout as corresponding ``GET_REPLY`` is used to request changes. Only
attributes where a change is requested are included in such request (also, not
all attributes may be changed). Replies to most ``SET`` request consist only
of error code and extack; if kernel provides additional data, it is sent in
the form of corresponding ``SET_REPLY`` message which can be suppressed by
setting ``ETHTOOL_FLAG_OMIT_REPLY`` flag in request header.

Data modification also triggers sending a ``NTF`` message with a notification.
These usually bear only a subset of attributes which was affected by the
change. The same notification is issued if the data is modified using other
means (mostly ioctl ethtool interface). Unlike notifications from ethtool
netlink code which are only sent if something actually changed, notifications
triggered by ioctl interface may be sent even if the request did not actually
change any data.

``ACT`` messages request kernel (driver) to perform a specific action. If some
information is reported by kernel (which can be suppressed by setting
``ETHTOOL_FLAG_OMIT_REPLY`` flag in request header), the reply takes form of
an ``ACT_REPLY`` message. Performing an action also triggers a notification
(``NTF`` message).

Later sections describe the format and semantics of these messages.


Request translation
===================

The following table maps ioctl commands to netlink commands providing their
functionality. Entries with "n/a" in right column are commands which do not
have their netlink replacement yet.

  =================================== =====================================
  ioctl command                       netlink command
  =================================== =====================================
  ``ETHTOOL_GSET``                    n/a
  ``ETHTOOL_SSET``                    n/a
  ``ETHTOOL_GDRVINFO``                n/a
  ``ETHTOOL_GREGS``                   n/a
  ``ETHTOOL_GWOL``                    n/a
  ``ETHTOOL_SWOL``                    n/a
  ``ETHTOOL_GMSGLVL``                 n/a
  ``ETHTOOL_SMSGLVL``                 n/a
  ``ETHTOOL_NWAY_RST``                n/a
  ``ETHTOOL_GLINK``                   n/a
  ``ETHTOOL_GEEPROM``                 n/a
  ``ETHTOOL_SEEPROM``                 n/a
  ``ETHTOOL_GCOALESCE``               n/a
  ``ETHTOOL_SCOALESCE``               n/a
  ``ETHTOOL_GRINGPARAM``              n/a
  ``ETHTOOL_SRINGPARAM``              n/a
  ``ETHTOOL_GPAUSEPARAM``             n/a
  ``ETHTOOL_SPAUSEPARAM``             n/a
  ``ETHTOOL_GRXCSUM``                 n/a
  ``ETHTOOL_SRXCSUM``                 n/a
  ``ETHTOOL_GTXCSUM``                 n/a
  ``ETHTOOL_STXCSUM``                 n/a
  ``ETHTOOL_GSG``                     n/a
  ``ETHTOOL_SSG``                     n/a
  ``ETHTOOL_TEST``                    n/a
  ``ETHTOOL_GSTRINGS``                n/a
  ``ETHTOOL_PHYS_ID``                 n/a
  ``ETHTOOL_GSTATS``                  n/a
  ``ETHTOOL_GTSO``                    n/a
  ``ETHTOOL_STSO``                    n/a
  ``ETHTOOL_GPERMADDR``               rtnetlink ``RTM_GETLINK``
  ``ETHTOOL_GUFO``                    n/a
  ``ETHTOOL_SUFO``                    n/a
  ``ETHTOOL_GGSO``                    n/a
  ``ETHTOOL_SGSO``                    n/a
  ``ETHTOOL_GFLAGS``                  n/a
  ``ETHTOOL_SFLAGS``                  n/a
  ``ETHTOOL_GPFLAGS``                 n/a
  ``ETHTOOL_SPFLAGS``                 n/a
  ``ETHTOOL_GRXFH``                   n/a
  ``ETHTOOL_SRXFH``                   n/a
  ``ETHTOOL_GGRO``                    n/a
  ``ETHTOOL_SGRO``                    n/a
  ``ETHTOOL_GRXRINGS``                n/a
  ``ETHTOOL_GRXCLSRLCNT``             n/a
  ``ETHTOOL_GRXCLSRULE``              n/a
  ``ETHTOOL_GRXCLSRLALL``             n/a
  ``ETHTOOL_SRXCLSRLDEL``             n/a
  ``ETHTOOL_SRXCLSRLINS``             n/a
  ``ETHTOOL_FLASHDEV``                n/a
  ``ETHTOOL_RESET``                   n/a
  ``ETHTOOL_SRXNTUPLE``               n/a
  ``ETHTOOL_GRXNTUPLE``               n/a
  ``ETHTOOL_GSSET_INFO``              n/a
  ``ETHTOOL_GRXFHINDIR``              n/a
  ``ETHTOOL_SRXFHINDIR``              n/a
  ``ETHTOOL_GFEATURES``               n/a
  ``ETHTOOL_SFEATURES``               n/a
  ``ETHTOOL_GCHANNELS``               n/a
  ``ETHTOOL_SCHANNELS``               n/a
  ``ETHTOOL_SET_DUMP``                n/a
  ``ETHTOOL_GET_DUMP_FLAG``           n/a
  ``ETHTOOL_GET_DUMP_DATA``           n/a
  ``ETHTOOL_GET_TS_INFO``             n/a
  ``ETHTOOL_GMODULEINFO``             n/a
  ``ETHTOOL_GMODULEEEPROM``           n/a
  ``ETHTOOL_GEEE``                    n/a
  ``ETHTOOL_SEEE``                    n/a
  ``ETHTOOL_GRSSH``                   n/a
  ``ETHTOOL_SRSSH``                   n/a
  ``ETHTOOL_GTUNABLE``                n/a
  ``ETHTOOL_STUNABLE``                n/a
  ``ETHTOOL_GPHYSTATS``               n/a
  ``ETHTOOL_PERQUEUE``                n/a
  ``ETHTOOL_GLINKSETTINGS``           n/a
  ``ETHTOOL_SLINKSETTINGS``           n/a
  ``ETHTOOL_PHY_GTUNABLE``            n/a
  ``ETHTOOL_PHY_STUNABLE``            n/a
  ``ETHTOOL_GFECPARAM``               n/a
  ``ETHTOOL_SFECPARAM``               n/a
  =================================== =====================================
