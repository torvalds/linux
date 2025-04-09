// SPDX-License-Identifier: GPL-2.0

pub(crate) const fn to_lowercase_bytes<const N: usize>(s: &str) -> [u8; N] {
    let src = s.as_bytes();
    let mut dst = [0; N];
    let mut i = 0;

    while i < src.len() && i < N {
        dst[i] = (src[i] as char).to_ascii_lowercase() as u8;
        i += 1;
    }

    dst
}

pub(crate) const fn const_bytes_to_str(bytes: &[u8]) -> &str {
    match core::str::from_utf8(bytes) {
        Ok(string) => string,
        Err(_) => kernel::build_error!("Bytes are not valid UTF-8."),
    }
}
