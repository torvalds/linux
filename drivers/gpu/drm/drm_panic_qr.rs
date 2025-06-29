// SPDX-License-Identifier: MIT

//! This is a simple QR encoder for DRM panic.
//!
//! It is called from a panic handler, so it should't allocate memory and
//! does all the work on the stack or on the provided buffers. For
//! simplification, it only supports low error correction, and applies the
//! first mask (checkerboard). It will draw the smallest QR code that can
//! contain the string passed as parameter. To get the most compact
//! QR code, the start of the URL is encoded as binary, and the
//! compressed kmsg is encoded as numeric.
//!
//! The binary data must be a valid URL parameter, so the easiest way is
//! to use base64 encoding. But this wastes 25% of data space, so the
//! whole stack trace won't fit in the QR code. So instead it encodes
//! every 7 bytes of input into 17 decimal digits, and then uses the
//! efficient numeric encoding, that encode 3 decimal digits into
//! 10bits. This makes 168bits of compressed data into 51 decimal digits,
//! into 170bits in the QR code, so wasting only 1.17%. And the numbers are
//! valid URL parameter, so the website can do the reverse, to get the
//! binary data. This is the same algorithm used by Fido v2.2 QR-initiated
//! authentication specification.
//!
//! Inspired by these 3 projects, all under MIT license:
//!
//! * <https://github.com/kennytm/qrcode-rust>
//! * <https://github.com/erwanvivien/fast_qr>
//! * <https://github.com/bjguillot/qr>

use kernel::{prelude::*, str::CStr};

#[derive(Debug, Clone, Copy, PartialEq, Eq, Ord, PartialOrd)]
struct Version(usize);

// Generator polynomials for ECC, only those that are needed for low quality.
const P7: [u8; 7] = [87, 229, 146, 149, 238, 102, 21];
const P10: [u8; 10] = [251, 67, 46, 61, 118, 70, 64, 94, 32, 45];
const P15: [u8; 15] = [
    8, 183, 61, 91, 202, 37, 51, 58, 58, 237, 140, 124, 5, 99, 105,
];
const P18: [u8; 18] = [
    215, 234, 158, 94, 184, 97, 118, 170, 79, 187, 152, 148, 252, 179, 5, 98, 96, 153,
];
const P20: [u8; 20] = [
    17, 60, 79, 50, 61, 163, 26, 187, 202, 180, 221, 225, 83, 239, 156, 164, 212, 212, 188, 190,
];
const P22: [u8; 22] = [
    210, 171, 247, 242, 93, 230, 14, 109, 221, 53, 200, 74, 8, 172, 98, 80, 219, 134, 160, 105,
    165, 231,
];
const P24: [u8; 24] = [
    229, 121, 135, 48, 211, 117, 251, 126, 159, 180, 169, 152, 192, 226, 228, 218, 111, 0, 117,
    232, 87, 96, 227, 21,
];
const P26: [u8; 26] = [
    173, 125, 158, 2, 103, 182, 118, 17, 145, 201, 111, 28, 165, 53, 161, 21, 245, 142, 13, 102,
    48, 227, 153, 145, 218, 70,
];
const P28: [u8; 28] = [
    168, 223, 200, 104, 224, 234, 108, 180, 110, 190, 195, 147, 205, 27, 232, 201, 21, 43, 245, 87,
    42, 195, 212, 119, 242, 37, 9, 123,
];
const P30: [u8; 30] = [
    41, 173, 145, 152, 216, 31, 179, 182, 50, 48, 110, 86, 239, 96, 222, 125, 42, 173, 226, 193,
    224, 130, 156, 37, 251, 216, 238, 40, 192, 180,
];

/// QR Code parameters for Low quality ECC:
/// - Error Correction polynomial.
/// - Number of blocks in group 1.
/// - Number of blocks in group 2.
/// - Block size in group 1.
///
/// (Block size in group 2 is one more than group 1).
struct VersionParameter(&'static [u8], u8, u8, u8);
const VPARAM: [VersionParameter; 40] = [
    VersionParameter(&P7, 1, 0, 19),    // V1
    VersionParameter(&P10, 1, 0, 34),   // V2
    VersionParameter(&P15, 1, 0, 55),   // V3
    VersionParameter(&P20, 1, 0, 80),   // V4
    VersionParameter(&P26, 1, 0, 108),  // V5
    VersionParameter(&P18, 2, 0, 68),   // V6
    VersionParameter(&P20, 2, 0, 78),   // V7
    VersionParameter(&P24, 2, 0, 97),   // V8
    VersionParameter(&P30, 2, 0, 116),  // V9
    VersionParameter(&P18, 2, 2, 68),   // V10
    VersionParameter(&P20, 4, 0, 81),   // V11
    VersionParameter(&P24, 2, 2, 92),   // V12
    VersionParameter(&P26, 4, 0, 107),  // V13
    VersionParameter(&P30, 3, 1, 115),  // V14
    VersionParameter(&P22, 5, 1, 87),   // V15
    VersionParameter(&P24, 5, 1, 98),   // V16
    VersionParameter(&P28, 1, 5, 107),  // V17
    VersionParameter(&P30, 5, 1, 120),  // V18
    VersionParameter(&P28, 3, 4, 113),  // V19
    VersionParameter(&P28, 3, 5, 107),  // V20
    VersionParameter(&P28, 4, 4, 116),  // V21
    VersionParameter(&P28, 2, 7, 111),  // V22
    VersionParameter(&P30, 4, 5, 121),  // V23
    VersionParameter(&P30, 6, 4, 117),  // V24
    VersionParameter(&P26, 8, 4, 106),  // V25
    VersionParameter(&P28, 10, 2, 114), // V26
    VersionParameter(&P30, 8, 4, 122),  // V27
    VersionParameter(&P30, 3, 10, 117), // V28
    VersionParameter(&P30, 7, 7, 116),  // V29
    VersionParameter(&P30, 5, 10, 115), // V30
    VersionParameter(&P30, 13, 3, 115), // V31
    VersionParameter(&P30, 17, 0, 115), // V32
    VersionParameter(&P30, 17, 1, 115), // V33
    VersionParameter(&P30, 13, 6, 115), // V34
    VersionParameter(&P30, 12, 7, 121), // V35
    VersionParameter(&P30, 6, 14, 121), // V36
    VersionParameter(&P30, 17, 4, 122), // V37
    VersionParameter(&P30, 4, 18, 122), // V38
    VersionParameter(&P30, 20, 4, 117), // V39
    VersionParameter(&P30, 19, 6, 118), // V40
];

const MAX_EC_SIZE: usize = 30;
const MAX_BLK_SIZE: usize = 123;

/// Position of the alignment pattern grid.
const ALIGNMENT_PATTERNS: [&[u8]; 40] = [
    &[],
    &[6, 18],
    &[6, 22],
    &[6, 26],
    &[6, 30],
    &[6, 34],
    &[6, 22, 38],
    &[6, 24, 42],
    &[6, 26, 46],
    &[6, 28, 50],
    &[6, 30, 54],
    &[6, 32, 58],
    &[6, 34, 62],
    &[6, 26, 46, 66],
    &[6, 26, 48, 70],
    &[6, 26, 50, 74],
    &[6, 30, 54, 78],
    &[6, 30, 56, 82],
    &[6, 30, 58, 86],
    &[6, 34, 62, 90],
    &[6, 28, 50, 72, 94],
    &[6, 26, 50, 74, 98],
    &[6, 30, 54, 78, 102],
    &[6, 28, 54, 80, 106],
    &[6, 32, 58, 84, 110],
    &[6, 30, 58, 86, 114],
    &[6, 34, 62, 90, 118],
    &[6, 26, 50, 74, 98, 122],
    &[6, 30, 54, 78, 102, 126],
    &[6, 26, 52, 78, 104, 130],
    &[6, 30, 56, 82, 108, 134],
    &[6, 34, 60, 86, 112, 138],
    &[6, 30, 58, 86, 114, 142],
    &[6, 34, 62, 90, 118, 146],
    &[6, 30, 54, 78, 102, 126, 150],
    &[6, 24, 50, 76, 102, 128, 154],
    &[6, 28, 54, 80, 106, 132, 158],
    &[6, 32, 58, 84, 110, 136, 162],
    &[6, 26, 54, 82, 110, 138, 166],
    &[6, 30, 58, 86, 114, 142, 170],
];

/// Version information for format V7-V40.
const VERSION_INFORMATION: [u32; 34] = [
    0b00_0111_1100_1001_0100,
    0b00_1000_0101_1011_1100,
    0b00_1001_1010_1001_1001,
    0b00_1010_0100_1101_0011,
    0b00_1011_1011_1111_0110,
    0b00_1100_0111_0110_0010,
    0b00_1101_1000_0100_0111,
    0b00_1110_0110_0000_1101,
    0b00_1111_1001_0010_1000,
    0b01_0000_1011_0111_1000,
    0b01_0001_0100_0101_1101,
    0b01_0010_1010_0001_0111,
    0b01_0011_0101_0011_0010,
    0b01_0100_1001_1010_0110,
    0b01_0101_0110_1000_0011,
    0b01_0110_1000_1100_1001,
    0b01_0111_0111_1110_1100,
    0b01_1000_1110_1100_0100,
    0b01_1001_0001_1110_0001,
    0b01_1010_1111_1010_1011,
    0b01_1011_0000_1000_1110,
    0b01_1100_1100_0001_1010,
    0b01_1101_0011_0011_1111,
    0b01_1110_1101_0111_0101,
    0b01_1111_0010_0101_0000,
    0b10_0000_1001_1101_0101,
    0b10_0001_0110_1111_0000,
    0b10_0010_1000_1011_1010,
    0b10_0011_0111_1001_1111,
    0b10_0100_1011_0000_1011,
    0b10_0101_0100_0010_1110,
    0b10_0110_1010_0110_0100,
    0b10_0111_0101_0100_0001,
    0b10_1000_1100_0110_1001,
];

/// Format info for low quality ECC.
const FORMAT_INFOS_QR_L: [u16; 8] = [
    0x77c4, 0x72f3, 0x7daa, 0x789d, 0x662f, 0x6318, 0x6c41, 0x6976,
];

impl Version {
    /// Returns the smallest QR version than can hold these segments.
    fn from_segments(segments: &[&Segment<'_>]) -> Option<Version> {
        (1..=40)
            .map(Version)
            .find(|&v| v.max_data() * 8 >= segments.iter().map(|s| s.total_size_bits(v)).sum())
    }

    fn width(&self) -> u8 {
        (self.0 as u8) * 4 + 17
    }

    fn max_data(&self) -> usize {
        self.g1_blk_size() * self.g1_blocks() + (self.g1_blk_size() + 1) * self.g2_blocks()
    }

    fn ec_size(&self) -> usize {
        VPARAM[self.0 - 1].0.len()
    }

    fn g1_blocks(&self) -> usize {
        VPARAM[self.0 - 1].1 as usize
    }

    fn g2_blocks(&self) -> usize {
        VPARAM[self.0 - 1].2 as usize
    }

    fn g1_blk_size(&self) -> usize {
        VPARAM[self.0 - 1].3 as usize
    }

    fn alignment_pattern(&self) -> &'static [u8] {
        ALIGNMENT_PATTERNS[self.0 - 1]
    }

    fn poly(&self) -> &'static [u8] {
        VPARAM[self.0 - 1].0
    }

    fn version_info(&self) -> u32 {
        if *self >= Version(7) {
            VERSION_INFORMATION[self.0 - 7]
        } else {
            0
        }
    }
}

/// Exponential table for Galois Field GF(256).
const EXP_TABLE: [u8; 256] = [
    1, 2, 4, 8, 16, 32, 64, 128, 29, 58, 116, 232, 205, 135, 19, 38, 76, 152, 45, 90, 180, 117,
    234, 201, 143, 3, 6, 12, 24, 48, 96, 192, 157, 39, 78, 156, 37, 74, 148, 53, 106, 212, 181,
    119, 238, 193, 159, 35, 70, 140, 5, 10, 20, 40, 80, 160, 93, 186, 105, 210, 185, 111, 222, 161,
    95, 190, 97, 194, 153, 47, 94, 188, 101, 202, 137, 15, 30, 60, 120, 240, 253, 231, 211, 187,
    107, 214, 177, 127, 254, 225, 223, 163, 91, 182, 113, 226, 217, 175, 67, 134, 17, 34, 68, 136,
    13, 26, 52, 104, 208, 189, 103, 206, 129, 31, 62, 124, 248, 237, 199, 147, 59, 118, 236, 197,
    151, 51, 102, 204, 133, 23, 46, 92, 184, 109, 218, 169, 79, 158, 33, 66, 132, 21, 42, 84, 168,
    77, 154, 41, 82, 164, 85, 170, 73, 146, 57, 114, 228, 213, 183, 115, 230, 209, 191, 99, 198,
    145, 63, 126, 252, 229, 215, 179, 123, 246, 241, 255, 227, 219, 171, 75, 150, 49, 98, 196, 149,
    55, 110, 220, 165, 87, 174, 65, 130, 25, 50, 100, 200, 141, 7, 14, 28, 56, 112, 224, 221, 167,
    83, 166, 81, 162, 89, 178, 121, 242, 249, 239, 195, 155, 43, 86, 172, 69, 138, 9, 18, 36, 72,
    144, 61, 122, 244, 245, 247, 243, 251, 235, 203, 139, 11, 22, 44, 88, 176, 125, 250, 233, 207,
    131, 27, 54, 108, 216, 173, 71, 142, 1,
];

/// Reverse exponential table for Galois Field GF(256).
const LOG_TABLE: [u8; 256] = [
    175, 0, 1, 25, 2, 50, 26, 198, 3, 223, 51, 238, 27, 104, 199, 75, 4, 100, 224, 14, 52, 141,
    239, 129, 28, 193, 105, 248, 200, 8, 76, 113, 5, 138, 101, 47, 225, 36, 15, 33, 53, 147, 142,
    218, 240, 18, 130, 69, 29, 181, 194, 125, 106, 39, 249, 185, 201, 154, 9, 120, 77, 228, 114,
    166, 6, 191, 139, 98, 102, 221, 48, 253, 226, 152, 37, 179, 16, 145, 34, 136, 54, 208, 148,
    206, 143, 150, 219, 189, 241, 210, 19, 92, 131, 56, 70, 64, 30, 66, 182, 163, 195, 72, 126,
    110, 107, 58, 40, 84, 250, 133, 186, 61, 202, 94, 155, 159, 10, 21, 121, 43, 78, 212, 229, 172,
    115, 243, 167, 87, 7, 112, 192, 247, 140, 128, 99, 13, 103, 74, 222, 237, 49, 197, 254, 24,
    227, 165, 153, 119, 38, 184, 180, 124, 17, 68, 146, 217, 35, 32, 137, 46, 55, 63, 209, 91, 149,
    188, 207, 205, 144, 135, 151, 178, 220, 252, 190, 97, 242, 86, 211, 171, 20, 42, 93, 158, 132,
    60, 57, 83, 71, 109, 65, 162, 31, 45, 67, 216, 183, 123, 164, 118, 196, 23, 73, 236, 127, 12,
    111, 246, 108, 161, 59, 82, 41, 157, 85, 170, 251, 96, 134, 177, 187, 204, 62, 90, 203, 89, 95,
    176, 156, 169, 160, 81, 11, 245, 22, 235, 122, 117, 44, 215, 79, 174, 213, 233, 230, 231, 173,
    232, 116, 214, 244, 234, 168, 80, 88, 175,
];

// 4 bits segment header.
const MODE_STOP: u16 = 0;
const MODE_NUMERIC: u16 = 1;
const MODE_BINARY: u16 = 4;
/// Padding bytes.
const PADDING: [u8; 2] = [236, 17];

/// Number of bits to encode characters in numeric mode.
const NUM_CHARS_BITS: [usize; 4] = [0, 4, 7, 10];
/// Number of decimal digits required to encode n bytes of binary data.
/// eg: you need 15 decimal digits to fit 6 bytes of binary data.
const BYTES_TO_DIGITS: [usize; 8] = [0, 3, 5, 8, 10, 13, 15, 17];

enum Segment<'a> {
    Numeric(&'a [u8]),
    Binary(&'a [u8]),
}

impl Segment<'_> {
    fn get_header(&self) -> (u16, usize) {
        match self {
            Segment::Binary(_) => (MODE_BINARY, 4),
            Segment::Numeric(_) => (MODE_NUMERIC, 4),
        }
    }

    /// Returns the size of the length field in bits, depending on QR Version.
    fn length_bits_count(&self, version: Version) -> usize {
        let Version(v) = version;
        match self {
            Segment::Binary(_) => match v {
                1..=9 => 8,
                _ => 16,
            },
            Segment::Numeric(_) => match v {
                1..=9 => 10,
                10..=26 => 12,
                _ => 14,
            },
        }
    }

    /// Number of characters in the segment.
    fn character_count(&self) -> usize {
        match self {
            Segment::Binary(data) => data.len(),
            Segment::Numeric(data) => {
                let last_chars = BYTES_TO_DIGITS[data.len() % 7];
                // 17 decimal numbers per 7bytes + remainder.
                17 * (data.len() / 7) + last_chars
            }
        }
    }

    fn get_length_field(&self, version: Version) -> (u16, usize) {
        (
            self.character_count() as u16,
            self.length_bits_count(version),
        )
    }

    fn total_size_bits(&self, version: Version) -> usize {
        let data_size = match self {
            Segment::Binary(data) => data.len() * 8,
            Segment::Numeric(_) => {
                let digits = self.character_count();
                10 * (digits / 3) + NUM_CHARS_BITS[digits % 3]
            }
        };
        // header + length + data.
        4 + self.length_bits_count(version) + data_size
    }

    fn iter(&self) -> SegmentIterator<'_> {
        SegmentIterator {
            segment: self,
            offset: 0,
            decfifo: Default::default(),
        }
    }
}

/// Max fifo size is 17 (max push) + 2 (max remaining)
const MAX_FIFO_SIZE: usize = 19;

/// A simple Decimal digit FIFO
#[derive(Default)]
struct DecFifo {
    decimals: [u8; MAX_FIFO_SIZE],
    len: usize,
}

impl DecFifo {
    fn push(&mut self, data: u64, len: usize) {
        let mut chunk = data;
        for i in (0..self.len).rev() {
            self.decimals[i + len] = self.decimals[i];
        }
        for i in 0..len {
            self.decimals[i] = (chunk % 10) as u8;
            chunk /= 10;
        }
        self.len += len;
    }

    /// Pop 3 decimal digits from the FIFO
    fn pop3(&mut self) -> Option<(u16, usize)> {
        if self.len == 0 {
            None
        } else {
            let poplen = 3.min(self.len);
            self.len -= poplen;
            let mut out = 0;
            let mut exp = 1;
            for i in 0..poplen {
                out += self.decimals[self.len + i] as u16 * exp;
                exp *= 10;
            }
            Some((out, NUM_CHARS_BITS[poplen]))
        }
    }
}

struct SegmentIterator<'a> {
    segment: &'a Segment<'a>,
    offset: usize,
    decfifo: DecFifo,
}

impl Iterator for SegmentIterator<'_> {
    type Item = (u16, usize);

    fn next(&mut self) -> Option<Self::Item> {
        match self.segment {
            Segment::Binary(data) => {
                if self.offset < data.len() {
                    let byte = data[self.offset] as u16;
                    self.offset += 1;
                    Some((byte, 8))
                } else {
                    None
                }
            }
            Segment::Numeric(data) => {
                if self.decfifo.len < 3 && self.offset < data.len() {
                    // If there are less than 3 decimal digits in the fifo,
                    // take the next 7 bytes of input, and push them to the fifo.
                    let mut buf = [0u8; 8];
                    let len = 7.min(data.len() - self.offset);
                    buf[..len].copy_from_slice(&data[self.offset..self.offset + len]);
                    let chunk = u64::from_le_bytes(buf);
                    self.decfifo.push(chunk, BYTES_TO_DIGITS[len]);
                    self.offset += len;
                }
                self.decfifo.pop3()
            }
        }
    }
}

struct EncodedMsg<'a> {
    data: &'a mut [u8],
    ec_size: usize,
    g1_blocks: usize,
    g2_blocks: usize,
    g1_blk_size: usize,
    g2_blk_size: usize,
    poly: &'static [u8],
    version: Version,
}

/// Data to be put in the QR code, with correct segment encoding, padding, and
/// Error Code Correction.
impl EncodedMsg<'_> {
    fn new<'a>(segments: &[&Segment<'_>], data: &'a mut [u8]) -> Option<EncodedMsg<'a>> {
        let version = Version::from_segments(segments)?;
        let ec_size = version.ec_size();
        let g1_blocks = version.g1_blocks();
        let g2_blocks = version.g2_blocks();
        let g1_blk_size = version.g1_blk_size();
        let g2_blk_size = g1_blk_size + 1;
        let poly = version.poly();

        // clear the output.
        data.fill(0);

        let mut em = EncodedMsg {
            data,
            ec_size,
            g1_blocks,
            g2_blocks,
            g1_blk_size,
            g2_blk_size,
            poly,
            version,
        };
        em.encode(segments);
        Some(em)
    }

    /// Push bits of data at an offset (in bits).
    fn push(&mut self, offset: &mut usize, bits: (u16, usize)) {
        let (number, len_bits) = bits;
        let byte_off = *offset / 8;
        let bit_off = *offset % 8;
        let b = bit_off + len_bits;

        match (bit_off, b) {
            (0, 0..=8) => {
                self.data[byte_off] = (number << (8 - b)) as u8;
            }
            (0, _) => {
                self.data[byte_off] = (number >> (b - 8)) as u8;
                self.data[byte_off + 1] = (number << (16 - b)) as u8;
            }
            (_, 0..=8) => {
                self.data[byte_off] |= (number << (8 - b)) as u8;
            }
            (_, 9..=16) => {
                self.data[byte_off] |= (number >> (b - 8)) as u8;
                self.data[byte_off + 1] = (number << (16 - b)) as u8;
            }
            _ => {
                self.data[byte_off] |= (number >> (b - 8)) as u8;
                self.data[byte_off + 1] = (number >> (b - 16)) as u8;
                self.data[byte_off + 2] = (number << (24 - b)) as u8;
            }
        }
        *offset += len_bits;
    }

    fn add_segments(&mut self, segments: &[&Segment<'_>]) {
        let mut offset: usize = 0;

        for s in segments.iter() {
            self.push(&mut offset, s.get_header());
            self.push(&mut offset, s.get_length_field(self.version));
            for bits in s.iter() {
                self.push(&mut offset, bits);
            }
        }
        self.push(&mut offset, (MODE_STOP, 4));

        let pad_offset = offset.div_ceil(8);
        for i in pad_offset..self.version.max_data() {
            self.data[i] = PADDING[(i & 1) ^ (pad_offset & 1)];
        }
    }

    fn error_code_for_blocks(&mut self, offset: usize, size: usize, ec_offset: usize) {
        let mut tmp: [u8; MAX_BLK_SIZE + MAX_EC_SIZE] = [0; MAX_BLK_SIZE + MAX_EC_SIZE];

        tmp[0..size].copy_from_slice(&self.data[offset..offset + size]);
        for i in 0..size {
            let lead_coeff = tmp[i] as usize;
            if lead_coeff == 0 {
                continue;
            }
            let log_lead_coeff = usize::from(LOG_TABLE[lead_coeff]);
            for (u, &v) in tmp[i + 1..].iter_mut().zip(self.poly.iter()) {
                *u ^= EXP_TABLE[(usize::from(v) + log_lead_coeff) % 255];
            }
        }
        self.data[ec_offset..ec_offset + self.ec_size]
            .copy_from_slice(&tmp[size..size + self.ec_size]);
    }

    fn compute_error_code(&mut self) {
        let mut offset = 0;
        let mut ec_offset = self.g1_blocks * self.g1_blk_size + self.g2_blocks * self.g2_blk_size;

        for _ in 0..self.g1_blocks {
            self.error_code_for_blocks(offset, self.g1_blk_size, ec_offset);
            offset += self.g1_blk_size;
            ec_offset += self.ec_size;
        }
        for _ in 0..self.g2_blocks {
            self.error_code_for_blocks(offset, self.g2_blk_size, ec_offset);
            offset += self.g2_blk_size;
            ec_offset += self.ec_size;
        }
    }

    fn encode(&mut self, segments: &[&Segment<'_>]) {
        self.add_segments(segments);
        self.compute_error_code();
    }

    fn iter(&self) -> EncodedMsgIterator<'_> {
        EncodedMsgIterator {
            em: self,
            offset: 0,
        }
    }
}

/// Iterator, to retrieve the data in the interleaved order needed by QR code.
struct EncodedMsgIterator<'a> {
    em: &'a EncodedMsg<'a>,
    offset: usize,
}

impl Iterator for EncodedMsgIterator<'_> {
    type Item = u8;

    /// Send the bytes in interleaved mode, first byte of first block of group1,
    /// then first byte of second block of group1, ...
    fn next(&mut self) -> Option<Self::Item> {
        let em = self.em;
        let blocks = em.g1_blocks + em.g2_blocks;
        let g1_end = em.g1_blocks * em.g1_blk_size;
        let g2_end = g1_end + em.g2_blocks * em.g2_blk_size;
        let ec_end = g2_end + em.ec_size * blocks;

        if self.offset >= ec_end {
            return None;
        }

        let offset = if self.offset < em.g1_blk_size * blocks {
            // group1 and group2 interleaved
            let blk = self.offset % blocks;
            let blk_off = self.offset / blocks;
            if blk < em.g1_blocks {
                blk * em.g1_blk_size + blk_off
            } else {
                g1_end + em.g2_blk_size * (blk - em.g1_blocks) + blk_off
            }
        } else if self.offset < g2_end {
            // last byte of group2 blocks
            let blk2 = self.offset - blocks * em.g1_blk_size;
            em.g1_blk_size * em.g1_blocks + blk2 * em.g2_blk_size + em.g2_blk_size - 1
        } else {
            // EC blocks
            let ec_offset = self.offset - g2_end;
            let blk = ec_offset % blocks;
            let blk_off = ec_offset / blocks;

            g2_end + blk * em.ec_size + blk_off
        };
        self.offset += 1;
        Some(em.data[offset])
    }
}

/// A QR code image, encoded as a linear binary framebuffer.
/// 1 bit per module (pixel), each new line start at next byte boundary.
/// Max width is 177 for V40 QR code, so `u8` is enough for coordinate.
struct QrImage<'a> {
    data: &'a mut [u8],
    width: u8,
    stride: u8,
    version: Version,
}

impl QrImage<'_> {
    fn new<'a, 'b>(em: &'b EncodedMsg<'b>, qrdata: &'a mut [u8]) -> QrImage<'a> {
        let width = em.version.width();
        let stride = width.div_ceil(8);
        let data = qrdata;

        let mut qr_image = QrImage {
            data,
            width,
            stride,
            version: em.version,
        };
        qr_image.draw_all(em.iter());
        qr_image
    }

    fn clear(&mut self) {
        self.data.fill(0);
    }

    /// Set pixel to light color.
    fn set(&mut self, x: u8, y: u8) {
        let off = y as usize * self.stride as usize + x as usize / 8;
        let mut v = self.data[off];
        v |= 0x80 >> (x % 8);
        self.data[off] = v;
    }

    /// Invert a module color.
    fn xor(&mut self, x: u8, y: u8) {
        let off = y as usize * self.stride as usize + x as usize / 8;
        self.data[off] ^= 0x80 >> (x % 8);
    }

    /// Draw a light square at (x, y) top left corner.
    fn draw_square(&mut self, x: u8, y: u8, size: u8) {
        for k in 0..size {
            self.set(x + k, y);
            self.set(x, y + k + 1);
            self.set(x + size, y + k);
            self.set(x + k + 1, y + size);
        }
    }

    // Finder pattern: 3 8x8 square at the corners.
    fn draw_finders(&mut self) {
        self.draw_square(1, 1, 4);
        self.draw_square(self.width - 6, 1, 4);
        self.draw_square(1, self.width - 6, 4);
        for k in 0..8 {
            self.set(k, 7);
            self.set(self.width - k - 1, 7);
            self.set(k, self.width - 8);
        }
        for k in 0..7 {
            self.set(7, k);
            self.set(self.width - 8, k);
            self.set(7, self.width - 1 - k);
        }
    }

    fn is_finder(&self, x: u8, y: u8) -> bool {
        let end = self.width - 8;
        #[expect(clippy::nonminimal_bool)]
        {
            (x < 8 && y < 8) || (x < 8 && y >= end) || (x >= end && y < 8)
        }
    }

    // Alignment pattern: 5x5 squares in a grid.
    fn draw_alignments(&mut self) {
        let positions = self.version.alignment_pattern();
        for &x in positions.iter() {
            for &y in positions.iter() {
                if !self.is_finder(x, y) {
                    self.draw_square(x - 1, y - 1, 2);
                }
            }
        }
    }

    fn is_alignment(&self, x: u8, y: u8) -> bool {
        let positions = self.version.alignment_pattern();
        for &ax in positions.iter() {
            for &ay in positions.iter() {
                if self.is_finder(ax, ay) {
                    continue;
                }
                if x >= ax - 2 && x <= ax + 2 && y >= ay - 2 && y <= ay + 2 {
                    return true;
                }
            }
        }
        false
    }

    // Timing pattern: 2 dotted line between the finder patterns.
    fn draw_timing_patterns(&mut self) {
        let end = self.width - 8;

        for x in (9..end).step_by(2) {
            self.set(x, 6);
            self.set(6, x);
        }
    }

    fn is_timing(&self, x: u8, y: u8) -> bool {
        x == 6 || y == 6
    }

    // Mask info: 15 bits around the finders, written twice for redundancy.
    fn draw_maskinfo(&mut self) {
        let info: u16 = FORMAT_INFOS_QR_L[0];
        let mut skip = 0;

        for k in 0..7 {
            if k == 6 {
                skip = 1;
            }
            if info & (1 << (14 - k)) == 0 {
                self.set(k + skip, 8);
                self.set(8, self.width - 1 - k);
            }
        }
        skip = 0;
        for k in 0..8 {
            if k == 2 {
                skip = 1;
            }
            if info & (1 << (7 - k)) == 0 {
                self.set(8, 8 - skip - k);
                self.set(self.width - 8 + k, 8);
            }
        }
    }

    fn is_maskinfo(&self, x: u8, y: u8) -> bool {
        let end = self.width - 8;
        // Count the dark module as mask info.
        (x <= 8 && y == 8) || (y <= 8 && x == 8) || (x == 8 && y >= end) || (x >= end && y == 8)
    }

    // Version info: 18bits written twice, close to the finders.
    fn draw_version_info(&mut self) {
        let vinfo = self.version.version_info();
        let pos = self.width - 11;

        if vinfo != 0 {
            for x in 0..3 {
                for y in 0..6 {
                    if vinfo & (1 << (x + y * 3)) == 0 {
                        self.set(x + pos, y);
                        self.set(y, x + pos);
                    }
                }
            }
        }
    }

    fn is_version_info(&self, x: u8, y: u8) -> bool {
        let vinfo = self.version.version_info();
        let pos = self.width - 11;

        vinfo != 0 && ((x >= pos && x < pos + 3 && y < 6) || (y >= pos && y < pos + 3 && x < 6))
    }

    /// Returns true if the module is reserved (Not usable for data and EC).
    fn is_reserved(&self, x: u8, y: u8) -> bool {
        self.is_alignment(x, y)
            || self.is_finder(x, y)
            || self.is_timing(x, y)
            || self.is_maskinfo(x, y)
            || self.is_version_info(x, y)
    }

    /// Last module to draw, at bottom left corner.
    fn is_last(&self, x: u8, y: u8) -> bool {
        x == 0 && y == self.width - 1
    }

    /// Move to the next module according to QR code order.
    ///
    /// From bottom right corner, to bottom left corner.
    fn next(&self, x: u8, y: u8) -> (u8, u8) {
        let x_adj = if x <= 6 { x + 1 } else { x };
        let column_type = (self.width - x_adj) % 4;

        match column_type {
            2 if y > 0 => (x + 1, y - 1),
            0 if y < self.width - 1 => (x + 1, y + 1),
            0 | 2 if x == 7 => (x - 2, y),
            _ => (x - 1, y),
        }
    }

    /// Find next module that can hold data.
    fn next_available(&self, x: u8, y: u8) -> (u8, u8) {
        let (mut x, mut y) = self.next(x, y);
        while self.is_reserved(x, y) && !self.is_last(x, y) {
            (x, y) = self.next(x, y);
        }
        (x, y)
    }

    fn draw_data(&mut self, data: impl Iterator<Item = u8>) {
        let (mut x, mut y) = (self.width - 1, self.width - 1);
        for byte in data {
            for s in 0..8 {
                if byte & (0x80 >> s) == 0 {
                    self.set(x, y);
                }
                (x, y) = self.next_available(x, y);
            }
        }
        // Set the remaining modules (0, 3 or 7 depending on version).
        // because 0 correspond to a light module.
        while !self.is_last(x, y) {
            if !self.is_reserved(x, y) {
                self.set(x, y);
            }
            (x, y) = self.next(x, y);
        }
    }

    /// Apply checkerboard mask to all non-reserved modules.
    fn apply_mask(&mut self) {
        for x in 0..self.width {
            for y in 0..self.width {
                if (x ^ y) % 2 == 0 && !self.is_reserved(x, y) {
                    self.xor(x, y);
                }
            }
        }
    }

    /// Draw the QR code with the provided data iterator.
    fn draw_all(&mut self, data: impl Iterator<Item = u8>) {
        // First clear the table, as it may have already some data.
        self.clear();
        self.draw_finders();
        self.draw_alignments();
        self.draw_timing_patterns();
        self.draw_version_info();
        self.draw_data(data);
        self.draw_maskinfo();
        self.apply_mask();
    }
}

/// C entry point for the rust QR Code generator.
///
/// Write the QR code image in the data buffer, and return the QR code width,
/// or 0, if the data doesn't fit in a QR code.
///
/// * `url`: The base URL of the QR code. It will be encoded as Binary segment.
/// * `data`: A pointer to the binary data, to be encoded. if URL is NULL, it
///   will be encoded as binary segment, otherwise it will be encoded
///   efficiently as a numeric segment, and appended to the URL.
/// * `data_len`: Length of the data, that needs to be encoded, must be less
///   than `data_size`.
/// * `data_size`: Size of data buffer, it should be at least 4071 bytes to hold
///   a V40 QR code. It will then be overwritten with the QR code image.
/// * `tmp`: A temporary buffer that the QR code encoder will use, to write the
///   segments and ECC.
/// * `tmp_size`: Size of the temporary buffer, it must be at least 3706 bytes
///   long for V40.
///
/// # Safety
///
/// * `url` must be null or point at a nul-terminated string.
/// * `data` must be valid for reading and writing for `data_size` bytes.
/// * `tmp` must be valid for reading and writing for `tmp_size` bytes.
///
/// They must remain valid for the duration of the function call.
#[export]
pub unsafe extern "C" fn drm_panic_qr_generate(
    url: *const kernel::ffi::c_char,
    data: *mut u8,
    data_len: usize,
    data_size: usize,
    tmp: *mut u8,
    tmp_size: usize,
) -> u8 {
    if data_size < 4071 || tmp_size < 3706 || data_len > data_size {
        return 0;
    }
    // SAFETY: The caller ensures that `data` is a valid pointer for reading and
    // writing `data_size` bytes.
    let data_slice: &mut [u8] = unsafe { core::slice::from_raw_parts_mut(data, data_size) };
    // SAFETY: The caller ensures that `tmp` is a valid pointer for reading and
    // writing `tmp_size` bytes.
    let tmp_slice: &mut [u8] = unsafe { core::slice::from_raw_parts_mut(tmp, tmp_size) };
    if url.is_null() {
        match EncodedMsg::new(&[&Segment::Binary(&data_slice[0..data_len])], tmp_slice) {
            None => 0,
            Some(em) => {
                let qr_image = QrImage::new(&em, data_slice);
                qr_image.width
            }
        }
    } else {
        // SAFETY: The caller ensures that `url` is a valid pointer to a
        // nul-terminated string.
        let url_cstr: &CStr = unsafe { CStr::from_char_ptr(url) };
        let segments = &[
            &Segment::Binary(url_cstr.as_bytes()),
            &Segment::Numeric(&data_slice[0..data_len]),
        ];
        match EncodedMsg::new(segments, tmp_slice) {
            None => 0,
            Some(em) => {
                let qr_image = QrImage::new(&em, data_slice);
                qr_image.width
            }
        }
    }
}

/// Returns the maximum data size that can fit in a QR code of this version.
/// * `version`: QR code version, between 1-40.
/// * `url_len`: Length of the URL.
///
/// * If `url_len` > 0, remove the 2 segments header/length and also count the
///   conversion to numeric segments.
/// * If `url_len` = 0, only removes 3 bytes for 1 binary segment.
///
/// # Safety
///
/// Always safe to call.
// Required to be unsafe due to the `#[export]` annotation.
#[export]
pub unsafe extern "C" fn drm_panic_qr_max_data_size(version: u8, url_len: usize) -> usize {
    #[expect(clippy::manual_range_contains)]
    if version < 1 || version > 40 {
        return 0;
    }
    let max_data = Version(version as usize).max_data();

    if url_len > 0 {
        // Binary segment (URL) 4 + 16 bits, numeric segment (kmsg) 4 + 12 bits => 5 bytes.
        if url_len + 5 >= max_data {
            0
        } else {
            let max = max_data - url_len - 5;
            (max * 39) / 40
        }
    } else {
        // Remove 3 bytes for the binary segment (header 4 bits, length 16 bits, stop 4bits).
        max_data - 3
    }
}
