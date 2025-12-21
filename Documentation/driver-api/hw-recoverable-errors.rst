.. SPDX-License-Identifier: GPL-2.0

=================================================
Recoverable Hardware Error Tracking in vmcoreinfo
=================================================

Overview
--------

This feature provides a generic infrastructure within the Linux kernel to track
and log recoverable hardware errors. These are hardware recoverable errors
visible that might not cause immediate panics but may influence health, mainly
because new code path will be executed in the kernel.

By recording counts and timestamps of recoverable errors into the vmcoreinfo
crash dump notes, this infrastructure aids post-mortem crash analysis tools in
correlating hardware events with kernel failures. This enables faster triage
and better understanding of root causes, especially in large-scale cloud
environments where hardware issues are common.

Benefits
--------

- Facilitates correlation of hardware recoverable errors with kernel panics or
  unusual code paths that lead to system crashes.
- Provides operators and cloud providers quick insights, improving reliability
  and reducing troubleshooting time.
- Complements existing full hardware diagnostics without replacing them.

Data Exposure and Consumption
-----------------------------

- The tracked error data consists of per-error-type counts and timestamps of
  last occurrence.
- This data is stored in the `hwerror_data` array, categorized by error source
  types like CPU, memory, PCI, CXL, and others.
- It is exposed via vmcoreinfo crash dump notes and can be read using tools
  like `crash`, `drgn`, or other kernel crash analysis utilities.
- There is no other way to read these data other than from crash dumps.
- These errors are divided by area, which includes CPU, Memory, PCI, CXL and
  others.

Typical usage example (in drgn REPL):

.. code-block:: python

    >>> prog['hwerror_data']
    (struct hwerror_info[HWERR_RECOV_MAX]){
        {
            .count = (int)844,
            .timestamp = (time64_t)1752852018,
        },
        ...
    }

Enabling
--------

- This feature is enabled when CONFIG_VMCORE_INFO is set.

