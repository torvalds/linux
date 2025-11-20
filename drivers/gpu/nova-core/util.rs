// SPDX-License-Identifier: GPL-2.0

/// Converts a null-terminated byte slice to a string, or `None` if the array does not
/// contains any null byte or contains invalid characters.
///
/// Contrary to [`kernel::str::CStr::from_bytes_with_nul`], the null byte can be anywhere in the
/// slice, and not only in the last position.
pub(crate) fn str_from_null_terminated(bytes: &[u8]) -> Option<&str> {
    use kernel::str::CStr;

    bytes
        .iter()
        .position(|&b| b == 0)
        .and_then(|null_pos| CStr::from_bytes_with_nul(&bytes[..=null_pos]).ok())
        .and_then(|cstr| cstr.to_str().ok())
}
