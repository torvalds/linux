.. SPDX-License-Identifier: GPL-2.0

===================================================================
TDX Guest API Documentation
===================================================================

1. General description
======================

The TDX guest driver exposes IOCTL interfaces via the /dev/tdx-guest misc
device to allow userspace to get certain TDX guest-specific details.

2. API description
==================

In this section, for each supported IOCTL, the following information is
provided along with a generic description.

:Input parameters: Parameters passed to the IOCTL and related details.
:Output: Details about output data and return value (with details about
         the non common error values).

2.1 TDX_CMD_GET_REPORT0
-----------------------

:Input parameters: struct tdx_report_req
:Output: Upon successful execution, TDREPORT data is copied to
         tdx_report_req.tdreport and return 0. Return -EINVAL for invalid
         operands, -EIO on TDCALL failure or standard error number on other
         common failures.

The TDX_CMD_GET_REPORT0 IOCTL can be used by the attestation software to get
the TDREPORT0 (a.k.a. TDREPORT subtype 0) from the TDX module using
TDCALL[TDG.MR.REPORT].

A subtype index is added at the end of this IOCTL CMD to uniquely identify the
subtype-specific TDREPORT request. Although the subtype option is mentioned in
the TDX Module v1.0 specification, section titled "TDG.MR.REPORT", it is not
currently used, and it expects this value to be 0. So to keep the IOCTL
implementation simple, the subtype option was not included as part of the input
ABI. However, in the future, if the TDX Module supports more than one subtype,
a new IOCTL CMD will be created to handle it. To keep the IOCTL naming
consistent, a subtype index is added as part of the IOCTL CMD.

Reference
---------

TDX reference material is collected here:

https://www.intel.com/content/www/us/en/developer/articles/technical/intel-trust-domain-extensions.html

The driver is based on TDX module specification v1.0 and TDX GHCI specification v1.0.
