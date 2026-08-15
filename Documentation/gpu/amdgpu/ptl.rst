=======================================
Peak Tops Limiter (PTL) sysfs Interface
=======================================

Overview
--------
The Peak Tops Limiter (PTL) sysfs interface enables users to control and
configure the PTL feature for each GPU individually.  All PTL-related
sysfs files are located under `/sys/class/drm/cardX/device/ptl/`, where
`X` is the GPU index.  Through these files, users can enable or disable
PTL, set preferred data formats, and query supported formats for each GPU.

PTL sysfs files
----------------
The following files are available under `/sys/class/drm/cardX/device/ptl/`:

- `ptl_enable`
- `ptl_format`
- `ptl_supported_formats`

PTL Enable/Disable
------------------
File: `ptl_enable`
Type: Read/Write (rw)

Read: Returns the current PTL status as a string: `enabled` if PTL
is active, or `disabled` if inactive.

Write:

- Write `1` or `enabled` to enable PTL
- Write `0` or `disabled` to disable PTL

Examples::

    # Query PTL status
    cat /sys/class/drm/card1/device/ptl/ptl_enable
    # Output: enabled

    # Enable PTL
    sudo bash -c "echo 1 > /sys/class/drm/card1/device/ptl/ptl_enable"

    # Disable PTL
    sudo bash -c "echo 0 > /sys/class/drm/card1/device/ptl/ptl_enable"

PTL Format (Preferred Data Formats)
-----------------------------------
File: `ptl_format`
Type: Read/Write (rw)

Read: Returns the two preferred formats, e.g. `I8,F32`.

Write: Accepts two formats separated by a comma, e.g. `I8,F32`.

- Both formats must be supported and different.
- If an invalid format is provided (not supported, or both formats are the
  same), the driver will return "write error: Invalid argument".

Examples::

    # Query PTL formats
    cat /sys/class/drm/card1/device/ptl/ptl_format
    # Output: I8,F32

    # Set PTL formats
    sudo bash -c "echo I8,F32 > /sys/class/drm/card1/device/ptl/ptl_format"

Supported Formats
-----------------
File: `ptl_supported_formats`
Type: Read-only (r)

Read: Returns a comma-separated list of supported formats, e.g.
`I8,F16,BF16,F32,F64`.

Example::

    # Check supported formats
    cat /sys/class/drm/card1/device/ptl/ptl_supported_formats
    # Output: I8,F16,BF16,F32,F64

Behavioral Notes
----------------
- PTL formats can only be set when PTL is enabled.
- If PTL is disabled, `ptl_format` returns `N/A`.
- Only two formats can be set at a time, and they must be from the supported set and different..
- All commands support per-GPU targeting.
- Root permission is required to enable/disable PTL or change formats.
- If the hardware does not support PTL, the PTL sysfs directory will not
  be created.

Implementation
--------------
The PTL sysfs nodes are implemented in `drivers/gpu/drm/amd/amdgpu/amdgpu_psp.c`.
