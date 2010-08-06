/* Subdriver for the GL860 chip with the MI2020 sensor
 * Author Olivier LORIN, from logs by Iceman/Soro2005 + Fret_saw/Hulkie/Tricid
 * with the help of Kytrix/BUGabundo/Blazercist.
 * Driver achieved thanks to a webcam gift by Kytrix.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/* Sensor : MI2020 */

#include "gl860.h"

static u8 dat_wbal1[] = {0x8c, 0xa2, 0x0c};

static u8 dat_bright1[] = {0x8c, 0xa2, 0x06};
static u8 dat_bright3[] = {0x8c, 0xa1, 0x02};
static u8 dat_bright4[] = {0x90, 0x00, 0x0f};
static u8 dat_bright5[] = {0x8c, 0xa1, 0x03};
static u8 dat_bright6[] = {0x90, 0x00, 0x05};

static u8 dat_hvflip1[] = {0x8c, 0x27, 0x19};
static u8 dat_hvflip3[] = {0x8c, 0x27, 0x3b};
static u8 dat_hvflip5[] = {0x8c, 0xa1, 0x03};
static u8 dat_hvflip6[] = {0x90, 0x00, 0x06};

static struct idxdata tbl_middle_hvflip_low[] = {
	{0x33, "\x90\x00\x06"},
	{6, "\xff\xff\xff"},
	{0x33, "\x90\x00\x06"},
	{6, "\xff\xff\xff"},
	{0x33, "\x90\x00\x06"},
	{6, "\xff\xff\xff"},
	{0x33, "\x90\x00\x06"},
	{6, "\xff\xff\xff"},
};

static struct idxdata tbl_middle_hvflip_big[] = {
	{0x33, "\x8c\xa1\x03"}, {0x33, "\x90\x00\x01"}, {0x33, "\x8c\xa1\x20"},
	{0x33, "\x90\x00\x00"}, {0x33, "\x8c\xa7\x02"}, {0x33, "\x90\x00\x00"},
	{102, "\xff\xff\xff"},
	{0x33, "\x8c\xa1\x03"}, {0x33, "\x90\x00\x02"}, {0x33, "\x8c\xa1\x20"},
	{0x33, "\x90\x00\x72"}, {0x33, "\x8c\xa7\x02"}, {0x33, "\x90\x00\x01"},
};

static struct idxdata tbl_end_hvflip[] = {
	{0x33, "\x8c\xa1\x02"}, {0x33, "\x90\x00\x1f"},
	{6, "\xff\xff\xff"},
	{0x33, "\x8c\xa1\x02"}, {0x33, "\x90\x00\x1f"},
	{6, "\xff\xff\xff"},
	{0x33, "\x8c\xa1\x02"}, {0x33, "\x90\x00\x1f"},
	{6, "\xff\xff\xff"},
	{0x33, "\x8c\xa1\x02"}, {0x33, "\x90\x00\x1f"},
};

static u8 dat_freq1[] = { 0x8c, 0xa4, 0x04 };

static u8 dat_multi5[] = { 0x8c, 0xa1, 0x03 };
static u8 dat_multi6[] = { 0x90, 0x00, 0x05 };

static struct validx tbl_init_at_startup[] = {
	{0x0000, 0x0000}, {0x0010, 0x0010}, {0x0008, 0x00c0}, {0x0001,0x00c1},
	{0x0001, 0x00c2}, {0x0020, 0x0006}, {0x006a, 0x000d},
	{53, 0xffff},
	{0x0040, 0x0000}, {0x0063, 0x0006},
};

static struct validx tbl_common_0B[] = {
	{0x0002, 0x0004}, {0x006a, 0x0007}, {0x00ef, 0x0006}, {0x006a,0x000d},
	{0x0000, 0x00c0}, {0x0010, 0x0010}, {0x0003, 0x00c1}, {0x0042,0x00c2},
	{0x0004, 0x00d8}, {0x0000, 0x0058}, {0x0041, 0x0000},
};

static struct idxdata tbl_common_3B[] = {
	{0x33, "\x86\x25\x01"}, {0x33, "\x86\x25\x00"},
	{2, "\xff\xff\xff"},
	{0x30, "\x1a\x0a\xcc"}, {0x32, "\x02\x00\x08"}, {0x33, "\xf4\x03\x1d"},
	{6, "\xff\xff\xff"}, /* 12 */
	{0x34, "\x1e\x8f\x09"}, {0x34, "\x1c\x01\x28"}, {0x34, "\x1e\x8f\x09"},
	{2, "\xff\xff\xff"}, /* - */
	{0x34, "\x1e\x8f\x09"}, {0x32, "\x14\x06\xe6"}, {0x33, "\x8c\x22\x23"},
	{0x33, "\x90\x00\x00"}, {0x33, "\x8c\xa2\x0f"}, {0x33, "\x90\x00\x0d"},
	{0x33, "\x8c\xa2\x10"}, {0x33, "\x90\x00\x0b"}, {0x33, "\x8c\xa2\x11"},
	{0x33, "\x90\x00\x07"}, {0x33, "\xf4\x03\x1d"}, {0x35, "\xa2\x00\xe2"},
	{0x33, "\x8c\xab\x05"}, {0x33, "\x90\x00\x01"}, {0x32, "\x6e\x00\x86"},
	{0x32, "\x70\x0f\xaa"}, {0x32, "\x72\x0f\xe4"}, {0x33, "\x8c\xa3\x4a"},
	{0x33, "\x90\x00\x5a"}, {0x33, "\x8c\xa3\x4b"}, {0x33, "\x90\x00\xa6"},
	{0x33, "\x8c\xa3\x61"}, {0x33, "\x90\x00\xc8"}, {0x33, "\x8c\xa3\x62"},
	{0x33, "\x90\x00\xe1"}, {0x34, "\xce\x01\xa8"}, {0x34, "\xd0\x66\x33"},
	{0x34, "\xd2\x31\x9a"}, {0x34, "\xd4\x94\x63"}, {0x34, "\xd6\x4b\x25"},
	{0x34, "\xd8\x26\x70"}, {0x34, "\xda\x72\x4c"}, {0x34, "\xdc\xff\x04"},
	{0x34, "\xde\x01\x5b"}, {0x34, "\xe6\x01\x13"}, {0x34, "\xee\x0b\xf0"},
	{0x34, "\xf6\x0b\xa4"}, {0x35, "\x00\xf6\xe7"}, {0x35, "\x08\x0d\xfd"},
	{0x35, "\x10\x25\x63"}, {0x35, "\x18\x35\x6c"}, {0x35, "\x20\x42\x7e"},
	{0x35, "\x28\x19\x44"}, {0x35, "\x30\x39\xd4"}, {0x35, "\x38\xf5\xa8"},
	{0x35, "\x4c\x07\x90"}, {0x35, "\x44\x07\xb8"}, {0x35, "\x5c\x06\x88"},
	{0x35, "\x54\x07\xff"}, {0x34, "\xe0\x01\x52"}, {0x34, "\xe8\x00\xcc"},
	{0x34, "\xf0\x0d\x83"}, {0x34, "\xf8\x0c\xb3"}, {0x35, "\x02\xfe\xba"},
	{0x35, "\x0a\x04\xe0"}, {0x35, "\x12\x1c\x63"}, {0x35, "\x1a\x2b\x5a"},
	{0x35, "\x22\x32\x5e"}, {0x35, "\x2a\x0d\x28"}, {0x35, "\x32\x2c\x02"},
	{0x35, "\x3a\xf4\xfa"}, {0x35, "\x4e\x07\xef"}, {0x35, "\x46\x07\x88"},
	{0x35, "\x5e\x07\xc1"}, {0x35, "\x56\x04\x64"}, {0x34, "\xe4\x01\x15"},
	{0x34, "\xec\x00\x82"}, {0x34, "\xf4\x0c\xce"}, {0x34, "\xfc\x0c\xba"},
	{0x35, "\x06\x1f\x02"}, {0x35, "\x0e\x02\xe3"}, {0x35, "\x16\x1a\x50"},
	{0x35, "\x1e\x24\x39"}, {0x35, "\x26\x23\x4c"}, {0x35, "\x2e\xf9\x1b"},
	{0x35, "\x36\x23\x19"}, {0x35, "\x3e\x12\x08"}, {0x35, "\x52\x07\x22"},
	{0x35, "\x4a\x03\xd3"}, {0x35, "\x62\x06\x54"}, {0x35, "\x5a\x04\x5d"},
	{0x34, "\xe2\x01\x04"}, {0x34, "\xea\x00\xa0"}, {0x34, "\xf2\x0c\xbc"},
	{0x34, "\xfa\x0c\x5b"}, {0x35, "\x04\x17\xf2"}, {0x35, "\x0c\x02\x08"},
	{0x35, "\x14\x28\x43"}, {0x35, "\x1c\x28\x62"}, {0x35, "\x24\x2b\x60"},
	{0x35, "\x2c\x07\x33"}, {0x35, "\x34\x1f\xb0"}, {0x35, "\x3c\xed\xcd"},
	{0x35, "\x50\x00\x06"}, {0x35, "\x48\x07\xff"}, {0x35, "\x60\x05\x89"},
	{0x35, "\x58\x07\xff"}, {0x35, "\x40\x00\xa0"}, {0x35, "\x42\x00\x00"},
	{0x32, "\x10\x01\xfc"}, {0x33, "\x8c\xa1\x18"}, {0x33, "\x90\x00\x3c"},
	{0x33, "\x78\x00\x00"},
	{2, "\xff\xff\xff"},
	{0x35, "\xb8\x1f\x20"}, {0x33, "\x8c\xa2\x06"}, {0x33, "\x90\x00\x10"},
	{0x33, "\x8c\xa2\x07"}, {0x33, "\x90\x00\x08"}, {0x33, "\x8c\xa2\x42"},
	{0x33, "\x90\x00\x0b"}, {0x33, "\x8c\xa2\x4a"}, {0x33, "\x90\x00\x8c"},
	{0x35, "\xba\xfa\x08"}, {0x33, "\x8c\xa2\x02"}, {0x33, "\x90\x00\x22"},
	{0x33, "\x8c\xa2\x03"}, {0x33, "\x90\x00\xbb"}, {0x33, "\x8c\xa4\x04"},
	{0x33, "\x90\x00\x80"}, {0x33, "\x8c\xa7\x9d"}, {0x33, "\x90\x00\x00"},
	{0x33, "\x8c\xa7\x9e"}, {0x33, "\x90\x00\x00"}, {0x33, "\x8c\xa2\x0c"},
	{0x33, "\x90\x00\x17"}, {0x33, "\x8c\xa2\x15"}, {0x33, "\x90\x00\x04"},
	{0x33, "\x8c\xa2\x14"}, {0x33, "\x90\x00\x20"}, {0x33, "\x8c\xa1\x03"},
	{0x33, "\x90\x00\x00"}, {0x33, "\x8c\x27\x17"}, {0x33, "\x90\x21\x11"},
	{0x33, "\x8c\x27\x1b"}, {0x33, "\x90\x02\x4f"}, {0x33, "\x8c\x27\x25"},
	{0x33, "\x90\x06\x0f"}, {0x33, "\x8c\x27\x39"}, {0x33, "\x90\x21\x11"},
	{0x33, "\x8c\x27\x3d"}, {0x33, "\x90\x01\x20"}, {0x33, "\x8c\x27\x47"},
	{0x33, "\x90\x09\x4c"}, {0x33, "\x8c\x27\x03"}, {0x33, "\x90\x02\x84"},
	{0x33, "\x8c\x27\x05"}, {0x33, "\x90\x01\xe2"}, {0x33, "\x8c\x27\x07"},
	{0x33, "\x90\x06\x40"}, {0x33, "\x8c\x27\x09"}, {0x33, "\x90\x04\xb0"},
	{0x33, "\x8c\x27\x0d"}, {0x33, "\x90\x00\x00"}, {0x33, "\x8c\x27\x0f"},
	{0x33, "\x90\x00\x00"}, {0x33, "\x8c\x27\x11"}, {0x33, "\x90\x04\xbd"},
	{0x33, "\x8c\x27\x13"}, {0x33, "\x90\x06\x4d"}, {0x33, "\x8c\x27\x15"},
	{0x33, "\x90\x00\x00"}, {0x33, "\x8c\x27\x17"}, {0x33, "\x90\x21\x11"},
	{0x33, "\x8c\x27\x19"}, {0x33, "\x90\x04\x6c"}, {0x33, "\x8c\x27\x1b"},
	{0x33, "\x90\x02\x4f"}, {0x33, "\x8c\x27\x1d"}, {0x33, "\x90\x01\x02"},
	{0x33, "\x8c\x27\x1f"}, {0x33, "\x90\x02\x79"}, {0x33, "\x8c\x27\x21"},
	{0x33, "\x90\x01\x55"}, {0x33, "\x8c\x27\x23"}, {0x33, "\x90\x02\x85"},
	{0x33, "\x8c\x27\x25"}, {0x33, "\x90\x06\x0f"}, {0x33, "\x8c\x27\x27"},
	{0x33, "\x90\x20\x20"}, {0x33, "\x8c\x27\x29"}, {0x33, "\x90\x20\x20"},
	{0x33, "\x8c\x27\x2b"}, {0x33, "\x90\x10\x20"}, {0x33, "\x8c\x27\x2d"},
	{0x33, "\x90\x20\x07"}, {0x33, "\x8c\x27\x2f"}, {0x33, "\x90\x00\x04"},
	{0x33, "\x8c\x27\x31"}, {0x33, "\x90\x00\x04"}, {0x33, "\x8c\x27\x33"},
	{0x33, "\x90\x04\xbb"}, {0x33, "\x8c\x27\x35"}, {0x33, "\x90\x06\x4b"},
	{0x33, "\x8c\x27\x37"}, {0x33, "\x90\x00\x00"}, {0x33, "\x8c\x27\x39"},
	{0x33, "\x90\x21\x11"}, {0x33, "\x8c\x27\x3b"}, {0x33, "\x90\x00\x24"},
	{0x33, "\x8c\x27\x3d"}, {0x33, "\x90\x01\x20"}, {0x33, "\x8c\x27\x41"},
	{0x33, "\x90\x01\x69"}, {0x33, "\x8c\x27\x45"}, {0x33, "\x90\x04\xed"},
	{0x33, "\x8c\x27\x47"}, {0x33, "\x90\x09\x4c"}, {0x33, "\x8c\x27\x51"},
	{0x33, "\x90\x00\x00"}, {0x33, "\x8c\x27\x53"}, {0x33, "\x90\x03\x20"},
	{0x33, "\x8c\x27\x55"}, {0x33, "\x90\x00\x00"}, {0x33, "\x8c\x27\x57"},
	{0x33, "\x90\x02\x58"}, {0x33, "\x8c\x27\x5f"}, {0x33, "\x90\x00\x00"},
	{0x33, "\x8c\x27\x61"}, {0x33, "\x90\x06\x40"}, {0x33, "\x8c\x27\x63"},
	{0x33, "\x90\x00\x00"}, {0x33, "\x8c\x27\x65"}, {0x33, "\x90\x04\xb0"},
	{0x33, "\x8c\x22\x2e"}, {0x33, "\x90\x00\xa1"}, {0x33, "\x8c\xa4\x08"},
	{0x33, "\x90\x00\x1f"}, {0x33, "\x8c\xa4\x09"}, {0x33, "\x90\x00\x21"},
	{0x33, "\x8c\xa4\x0a"}, {0x33, "\x90\x00\x25"}, {0x33, "\x8c\xa4\x0b"},
	{0x33, "\x90\x00\x27"}, {0x33, "\x8c\x24\x11"}, {0x33, "\x90\x00\xa1"},
	{0x33, "\x8c\x24\x13"}, {0x33, "\x90\x00\xc1"}, {0x33, "\x8c\x24\x15"},
	{0x33, "\x90\x00\x6a"}, {0x33, "\x8c\x24\x17"}, {0x33, "\x90\x00\x80"},
	{0x33, "\x8c\xa1\x03"}, {0x33, "\x90\x00\x05"},
	{2, "\xff\xff\xff"},
	{0x33, "\x8c\xa1\x03"}, {0x33, "\x90\x00\x06"},
	{3, "\xff\xff\xff"},
};

static struct idxdata tbl_init_post_alt_low1[] = {
	{0x33, "\x8c\x27\x15"}, {0x33, "\x90\x00\x25"}, {0x33, "\x8c\x22\x2e"},
	{0x33, "\x90\x00\x81"}, {0x33, "\x8c\xa4\x08"}, {0x33, "\x90\x00\x17"},
	{0x33, "\x8c\xa4\x09"}, {0x33, "\x90\x00\x1a"}, {0x33, "\x8c\xa4\x0a"},
	{0x33, "\x90\x00\x1d"}, {0x33, "\x8c\xa4\x0b"}, {0x33, "\x90\x00\x20"},
	{0x33, "\x8c\x24\x11"}, {0x33, "\x90\x00\x81"}, {0x33, "\x8c\x24\x13"},
	{0x33, "\x90\x00\x9b"},
};

static struct idxdata tbl_init_post_alt_low2[] = {
	{0x33, "\x8c\x27\x03"}, {0x33, "\x90\x03\x24"}, {0x33, "\x8c\x27\x05"},
	{0x33, "\x90\x02\x58"}, {0x33, "\x8c\xa1\x03"}, {0x33, "\x90\x00\x05"},
	{2, "\xff\xff\xff"},
	{0x33, "\x8c\xa1\x03"}, {0x33, "\x90\x00\x06"},
	{2, "\xff\xff\xff"},
};

static struct idxdata tbl_init_post_alt_low3[] = {
	{0x34, "\x1e\x8f\x09"}, {0x34, "\x1c\x01\x28"}, {0x34, "\x1e\x8f\x09"},
	{2, "\xff\xff\xff"},
	{0x34, "\x1e\x8f\x09"}, {0x32, "\x14\x06\xe6"}, {0x33, "\x8c\xa1\x20"},
	{0x33, "\x90\x00\x00"}, {0x33, "\x8c\xa1\x03"}, {0x33, "\x90\x00\x01"},
	{0x33, "\x2e\x01\x00"}, {0x34, "\x04\x00\x2a"}, {0x33, "\x8c\xa7\x02"},
	{0x33, "\x90\x00\x00"}, {0x33, "\x8c\x27\x95"}, {0x33, "\x90\x01\x00"},
	{2, "\xff\xff\xff"},
	{0x33, "\x8c\xa1\x20"}, {0x33, "\x90\x00\x72"}, {0x33, "\x8c\xa1\x03"},
	{0x33, "\x90\x00\x02"}, {0x33, "\x8c\xa7\x02"}, {0x33, "\x90\x00\x01"},
	{2, "\xff\xff\xff"},
	{0x33, "\x8c\xa1\x20"}, {0x33, "\x90\x00\x00"}, {0x33, "\x8c\xa1\x03"},
	{0x33, "\x90\x00\x01"}, {0x33, "\x8c\xa7\x02"}, {0x33, "\x90\x00\x00"},
	{2, "\xff\xff\xff"},
	{0x33, "\x8c\xa1\x03"}, {0x33, "\x90\x00\x05"},
	{2, "\xff\xff\xff"},
	{0x33, "\x8c\xa1\x03"}, {0x33, "\x90\x00\x06"},
	{2, "\xff\xff\xff"},
	{0x33, "\x8c\xa1\x03"}, {0x33, "\x90\x00\x05"},
	{2, "\xff\xff\xff"},
	{0x33, "\x8c\xa1\x03"}, {0x33, "\x90\x00\x06"},
};

static struct idxdata tbl_init_post_alt_big[] = {
	{0x33, "\x8c\xa1\x03"}, {0x33, "\x90\x00\x05"},
	{2, "\xff\xff\xff"},
	{0x33, "\x8c\xa1\x03"}, {0x33, "\x90\x00\x06"},
	{2, "\xff\xff\xff"},
	{0x34, "\x1e\x8f\x09"}, {0x34, "\x1c\x01\x28"}, {0x34, "\x1e\x8f\x09"},
	{2, "\xff\xff\xff"},
	{0x34, "\x1e\x8f\x09"}, {0x32, "\x14\x06\xe6"}, {0x33, "\x8c\xa1\x03"},
	{0x33, "\x90\x00\x05"},
	{2, "\xff\xff\xff"},
	{0x33, "\x8c\xa1\x03"}, {0x33, "\x90\x00\x06"},
	{2, "\xff\xff\xff"},
	{0x33, "\x8c\xa1\x03"}, {0x33, "\x90\x00\x05"},
	{2, "\xff\xff\xff"},
	{0x33, "\x8c\xa1\x03"}, {0x33, "\x90\x00\x06"}, {0x33, "\x8c\xa1\x20"},
	{0x33, "\x90\x00\x72"}, {0x33, "\x8c\xa1\x30"}, {0x33, "\x90\x00\x03"},
	{0x33, "\x8c\xa1\x31"}, {0x33, "\x90\x00\x02"}, {0x33, "\x8c\xa1\x32"},
	{0x33, "\x90\x00\x03"}, {0x33, "\x8c\xa1\x34"}, {0x33, "\x90\x00\x03"},
	{0x33, "\x8c\xa1\x03"}, {0x33, "\x90\x00\x02"}, {0x33, "\x2e\x01\x00"},
	{0x34, "\x04\x00\x2a"}, {0x33, "\x8c\xa7\x02"}, {0x33, "\x90\x00\x01"},
	{0x33, "\x8c\x27\x97"}, {0x33, "\x90\x01\x00"},
	{51, "\xff\xff\xff"},
	{0x33, "\x8c\xa1\x20"}, {0x33, "\x90\x00\x00"}, {0x33, "\x8c\xa1\x03"},
	{0x33, "\x90\x00\x01"}, {0x33, "\x8c\xa7\x02"}, {0x33, "\x90\x00\x00"},
	{51, "\xff\xff\xff"},
	{0x33, "\x8c\xa1\x20"}, {0x33, "\x90\x00\x72"}, {0x33, "\x8c\xa1\x03"},
	{0x33, "\x90\x00\x02"}, {0x33, "\x8c\xa7\x02"}, {0x33, "\x90\x00\x01"},
	{51, "\xff\xff\xff"},
};

static struct idxdata tbl_init_post_alt_3B[] = {
	{0x32, "\x10\x01\xf8"}, {0x34, "\xce\x01\xa8"}, {0x34, "\xd0\x66\x33"},
	{0x34, "\xd2\x31\x9a"}, {0x34, "\xd4\x94\x63"}, {0x34, "\xd6\x4b\x25"},
	{0x34, "\xd8\x26\x70"}, {0x34, "\xda\x72\x4c"}, {0x34, "\xdc\xff\x04"},
	{0x34, "\xde\x01\x5b"}, {0x34, "\xe6\x01\x13"}, {0x34, "\xee\x0b\xf0"},
	{0x34, "\xf6\x0b\xa4"}, {0x35, "\x00\xf6\xe7"}, {0x35, "\x08\x0d\xfd"},
	{0x35, "\x10\x25\x63"}, {0x35, "\x18\x35\x6c"}, {0x35, "\x20\x42\x7e"},
	{0x35, "\x28\x19\x44"}, {0x35, "\x30\x39\xd4"}, {0x35, "\x38\xf5\xa8"},
	{0x35, "\x4c\x07\x90"}, {0x35, "\x44\x07\xb8"}, {0x35, "\x5c\x06\x88"},
	{0x35, "\x54\x07\xff"}, {0x34, "\xe0\x01\x52"}, {0x34, "\xe8\x00\xcc"},
	{0x34, "\xf0\x0d\x83"}, {0x34, "\xf8\x0c\xb3"}, {0x35, "\x02\xfe\xba"},
	{0x35, "\x0a\x04\xe0"}, {0x35, "\x12\x1c\x63"}, {0x35, "\x1a\x2b\x5a"},
	{0x35, "\x22\x32\x5e"}, {0x35, "\x2a\x0d\x28"}, {0x35, "\x32\x2c\x02"},
	{0x35, "\x3a\xf4\xfa"}, {0x35, "\x4e\x07\xef"}, {0x35, "\x46\x07\x88"},
	{0x35, "\x5e\x07\xc1"}, {0x35, "\x56\x04\x64"}, {0x34, "\xe4\x01\x15"},
	{0x34, "\xec\x00\x82"}, {0x34, "\xf4\x0c\xce"}, {0x34, "\xfc\x0c\xba"},
	{0x35, "\x06\x1f\x02"}, {0x35, "\x0e\x02\xe3"}, {0x35, "\x16\x1a\x50"},
	{0x35, "\x1e\x24\x39"}, {0x35, "\x26\x23\x4c"}, {0x35, "\x2e\xf9\x1b"},
	{0x35, "\x36\x23\x19"}, {0x35, "\x3e\x12\x08"}, {0x35, "\x52\x07\x22"},
	{0x35, "\x4a\x03\xd3"}, {0x35, "\x62\x06\x54"}, {0x35, "\x5a\x04\x5d"},
	{0x34, "\xe2\x01\x04"}, {0x34, "\xea\x00\xa0"}, {0x34, "\xf2\x0c\xbc"},
	{0x34, "\xfa\x0c\x5b"}, {0x35, "\x04\x17\xf2"}, {0x35, "\x0c\x02\x08"},
	{0x35, "\x14\x28\x43"}, {0x35, "\x1c\x28\x62"}, {0x35, "\x24\x2b\x60"},
	{0x35, "\x2c\x07\x33"}, {0x35, "\x34\x1f\xb0"}, {0x35, "\x3c\xed\xcd"},
	{0x35, "\x50\x00\x06"}, {0x35, "\x48\x07\xff"}, {0x35, "\x60\x05\x89"},
	{0x35, "\x58\x07\xff"}, {0x35, "\x40\x00\xa0"}, {0x35, "\x42\x00\x00"},
	{0x32, "\x10\x01\xfc"}, {0x33, "\x8c\xa1\x18"}, {0x33, "\x90\x00\x3c"},
};

static u8 *dat_640  = "\xd0\x02\xd1\x08\xd2\xe1\xd3\x02\xd4\x10\xd5\x81";
static u8 *dat_800  = "\xd0\x02\xd1\x10\xd2\x57\xd3\x02\xd4\x18\xd5\x21";
static u8 *dat_1280 = "\xd0\x02\xd1\x20\xd2\x01\xd3\x02\xd4\x28\xd5\x01";
static u8 *dat_1600 = "\xd0\x02\xd1\x20\xd2\xaf\xd3\x02\xd4\x30\xd5\x41";

static int  mi2020_init_at_startup(struct gspca_dev *gspca_dev);
static int  mi2020_configure_alt(struct gspca_dev *gspca_dev);
static int  mi2020_init_pre_alt(struct gspca_dev *gspca_dev);
static int  mi2020_init_post_alt(struct gspca_dev *gspca_dev);
static void mi2020_post_unset_alt(struct gspca_dev *gspca_dev);
static int  mi2020_camera_settings(struct gspca_dev *gspca_dev);
/*==========================================================================*/

void mi2020_init_settings(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->vcur.backlight  =  0;
	sd->vcur.brightness = 70;
	sd->vcur.sharpness  = 20;
	sd->vcur.contrast   =  0;
	sd->vcur.gamma      =  0;
	sd->vcur.hue        =  0;
	sd->vcur.saturation = 60;
	sd->vcur.whitebal   =  0; /* 50, not done by hardware */
	sd->vcur.mirror = 0;
	sd->vcur.flip   = 0;
	sd->vcur.AC50Hz = 1;

	sd->vmax.backlight  =  64;
	sd->vmax.brightness = 128;
	sd->vmax.sharpness  =  40;
	sd->vmax.contrast   =   3;
	sd->vmax.gamma      =   2;
	sd->vmax.hue        =   0 + 1; /* 200, not done by hardware */
	sd->vmax.saturation =   0;     /* 100, not done by hardware */
	sd->vmax.whitebal   =   2;     /* 100, not done by hardware */
	sd->vmax.mirror = 1;
	sd->vmax.flip   = 1;
	sd->vmax.AC50Hz = 1;

	sd->dev_camera_settings = mi2020_camera_settings;
	sd->dev_init_at_startup = mi2020_init_at_startup;
	sd->dev_configure_alt   = mi2020_configure_alt;
	sd->dev_init_pre_alt    = mi2020_init_pre_alt;
	sd->dev_post_unset_alt  = mi2020_post_unset_alt;
}

/*==========================================================================*/

static void common(struct gspca_dev *gspca_dev)
{
	fetch_validx(gspca_dev, tbl_common_0B, ARRAY_SIZE(tbl_common_0B));
	fetch_idxdata(gspca_dev, tbl_common_3B, ARRAY_SIZE(tbl_common_3B));
	ctrl_out(gspca_dev, 0x40, 1, 0x0041, 0x0000, 0, NULL);
}

static int mi2020_init_at_startup(struct gspca_dev *gspca_dev)
{
	u8 c;

	ctrl_in(gspca_dev, 0xc0, 2, 0x0000, 0x0004, 1, &c);
	ctrl_in(gspca_dev, 0xc0, 2, 0x0000, 0x0004, 1, &c);

	fetch_validx(gspca_dev, tbl_init_at_startup,
			ARRAY_SIZE(tbl_init_at_startup));

	ctrl_out(gspca_dev, 0x40,  1, 0x7a00, 0x8030,  0, NULL);
	ctrl_in(gspca_dev, 0xc0,  2, 0x7a00, 0x8030,  1, &c);

	common(gspca_dev);

	msleep(61);
/*	ctrl_out(gspca_dev, 0x40, 11, 0x0000, 0x0000,  0, NULL); */
/*	msleep(36); */
	ctrl_out(gspca_dev, 0x40,  1, 0x0001, 0x0000,  0, NULL);

	return 0;
}

static int mi2020_init_pre_alt(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;

	sd->mirrorMask =  0;
	sd->vold.hue   = -1;

	/* These controls need to be reset */
	sd->vold.brightness = -1;
	sd->vold.sharpness  = -1;

	/* If not different from default, they do not need to be set */
	sd->vold.contrast  = 0;
	sd->vold.gamma     = 0;
	sd->vold.backlight = 0;

	mi2020_init_post_alt(gspca_dev);

	return 0;
}

static int mi2020_init_post_alt(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 reso = gspca_dev->cam.cam_mode[(s32) gspca_dev->curr_mode].priv;

	s32 mirror = (((sd->vcur.mirror > 0) ^ sd->mirrorMask) > 0);
	s32 flip   = (((sd->vcur.flip   > 0) ^ sd->mirrorMask) > 0);
	s32 freq   = (sd->vcur.AC50Hz  > 0);
	s32 wbal   = sd->vcur.whitebal;

	u8 dat_freq2[] = {0x90, 0x00, 0x80};
	u8 dat_multi1[] = {0x8c, 0xa7, 0x00};
	u8 dat_multi2[] = {0x90, 0x00, 0x00};
	u8 dat_multi3[] = {0x8c, 0xa7, 0x00};
	u8 dat_multi4[] = {0x90, 0x00, 0x00};
	u8 dat_hvflip2[] = {0x90, 0x04, 0x6c};
	u8 dat_hvflip4[] = {0x90, 0x00, 0x24};
	u8 dat_wbal2[] = {0x90, 0x00, 0x00};
	u8 c;

	sd->nbIm = -1;

	dat_freq2[2] = freq ? 0xc0 : 0x80;
	dat_multi1[2] = 0x9d;
	dat_multi3[2] = dat_multi1[2] + 1;
	if (wbal == 0) {
		dat_multi4[2] = dat_multi2[2] = 0;
		dat_wbal2[2] = 0x17;
	} else if (wbal == 1) {
		dat_multi4[2] = dat_multi2[2] = 0;
		dat_wbal2[2] = 0x35;
	} else if (wbal == 2) {
		dat_multi4[2] = dat_multi2[2] = 0x20;
		dat_wbal2[2] = 0x17;
	}
	dat_hvflip2[2] = 0x6c + 2 * (1 - flip) + (1 - mirror);
	dat_hvflip4[2] = 0x24 + 2 * (1 - flip) + (1 - mirror);

	msleep(200);
	ctrl_out(gspca_dev, 0x40, 5, 0x0001, 0x0000, 0, NULL);
	msleep(2);

	common(gspca_dev);

	msleep(142);
	ctrl_out(gspca_dev, 0x40,  1, 0x0010, 0x0010,  0, NULL);
	ctrl_out(gspca_dev, 0x40,  1, 0x0003, 0x00c1,  0, NULL);
	ctrl_out(gspca_dev, 0x40,  1, 0x0042, 0x00c2,  0, NULL);
	ctrl_out(gspca_dev, 0x40,  1, 0x006a, 0x000d,  0, NULL);

	switch (reso) {
	case IMAGE_640:
	case IMAGE_800:
		if (reso != IMAGE_800)
			ctrl_out(gspca_dev, 0x40,  3, 0x0000, 0x0200,
				12, dat_640);
		else
			ctrl_out(gspca_dev, 0x40,  3, 0x0000, 0x0200,
				12, dat_800);

		fetch_idxdata(gspca_dev, tbl_init_post_alt_low1,
					ARRAY_SIZE(tbl_init_post_alt_low1));

		if (reso == IMAGE_800)
			fetch_idxdata(gspca_dev, tbl_init_post_alt_low2,
					ARRAY_SIZE(tbl_init_post_alt_low2));

		fetch_idxdata(gspca_dev, tbl_init_post_alt_low3,
				ARRAY_SIZE(tbl_init_post_alt_low3));

		ctrl_out(gspca_dev, 0x40, 1, 0x0010, 0x0010, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0x0000, 0x00c1, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0x0041, 0x00c2, 0, NULL);
		msleep(120);
		break;

	case IMAGE_1280:
	case IMAGE_1600:
		if (reso == IMAGE_1280) {
			ctrl_out(gspca_dev, 0x40, 3, 0x0000, 0x0200,
					12, dat_1280);
			ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033,
					3, "\x8c\x27\x07");
			ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033,
					3, "\x90\x05\x04");
			ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033,
					3, "\x8c\x27\x09");
			ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033,
					3, "\x90\x04\x02");
		} else {
			ctrl_out(gspca_dev, 0x40, 3, 0x0000, 0x0200,
					12, dat_1600);
			ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033,
					3, "\x8c\x27\x07");
			ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033,
					3, "\x90\x06\x40");
			ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033,
					3, "\x8c\x27\x09");
			ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033,
					3, "\x90\x04\xb0");
		}

		fetch_idxdata(gspca_dev, tbl_init_post_alt_big,
				ARRAY_SIZE(tbl_init_post_alt_big));

		ctrl_out(gspca_dev, 0x40, 1, 0x0001, 0x0010, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0x0000, 0x00c1, 0, NULL);
		ctrl_out(gspca_dev, 0x40, 1, 0x0041, 0x00c2, 0, NULL);
		msleep(1850);
	}

	ctrl_out(gspca_dev, 0x40, 1, 0x0040, 0x0000, 0, NULL);
	msleep(40);

	/* AC power frequency */
	ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_freq1);
	ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_freq2);
	msleep(33);
	/* light source */
	ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi1);
	ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi2);
	ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi3);
	ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi4);
	ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_wbal1);
	ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_wbal2);
	ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi5);
	ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi6);
	msleep(7);
	ctrl_in(gspca_dev, 0xc0, 2, 0x0000, 0x0000, 1, &c);

	fetch_idxdata(gspca_dev, tbl_init_post_alt_3B,
			ARRAY_SIZE(tbl_init_post_alt_3B));

	/* hvflip */
	ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_hvflip1);
	ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_hvflip2);
	ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_hvflip3);
	ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_hvflip4);
	ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_hvflip5);
	ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_hvflip6);
	msleep(250);

	if (reso == IMAGE_640 || reso == IMAGE_800)
		fetch_idxdata(gspca_dev, tbl_middle_hvflip_low,
				ARRAY_SIZE(tbl_middle_hvflip_low));
	else
		fetch_idxdata(gspca_dev, tbl_middle_hvflip_big,
				ARRAY_SIZE(tbl_middle_hvflip_big));

	fetch_idxdata(gspca_dev, tbl_end_hvflip,
			ARRAY_SIZE(tbl_end_hvflip));

	sd->nbIm = 0;

	sd->vold.mirror    = mirror;
	sd->vold.flip      = flip;
	sd->vold.AC50Hz    = freq;
	sd->vold.whitebal  = wbal;

	mi2020_camera_settings(gspca_dev);

	return 0;
}

static int mi2020_configure_alt(struct gspca_dev *gspca_dev)
{
	s32 reso = gspca_dev->cam.cam_mode[(s32) gspca_dev->curr_mode].priv;

	switch (reso) {
	case IMAGE_640:
		gspca_dev->alt = 3 + 1;
		break;

	case IMAGE_800:
	case IMAGE_1280:
	case IMAGE_1600:
		gspca_dev->alt = 1 + 1;
		break;
	}
	return 0;
}

static int mi2020_camera_settings(struct gspca_dev *gspca_dev)
{
	struct sd *sd = (struct sd *) gspca_dev;
	s32 reso = gspca_dev->cam.cam_mode[(s32) gspca_dev->curr_mode].priv;

	s32 backlight = sd->vcur.backlight;
	s32 bright =  sd->vcur.brightness;
	s32 sharp  =  sd->vcur.sharpness;
	s32 cntr   =  sd->vcur.contrast;
	s32 gam	   =  sd->vcur.gamma;
	s32 hue    = (sd->vcur.hue > 0);
	s32 mirror = (((sd->vcur.mirror > 0) ^ sd->mirrorMask) > 0);
	s32 flip   = (((sd->vcur.flip   > 0) ^ sd->mirrorMask) > 0);
	s32 freq   = (sd->vcur.AC50Hz > 0);
	s32 wbal   = sd->vcur.whitebal;

	u8 dat_sharp[] = {0x6c, 0x00, 0x08};
	u8 dat_bright2[] = {0x90, 0x00, 0x00};
	u8 dat_freq2[] = {0x90, 0x00, 0x80};
	u8 dat_multi1[] = {0x8c, 0xa7, 0x00};
	u8 dat_multi2[] = {0x90, 0x00, 0x00};
	u8 dat_multi3[] = {0x8c, 0xa7, 0x00};
	u8 dat_multi4[] = {0x90, 0x00, 0x00};
	u8 dat_hvflip2[] = {0x90, 0x04, 0x6c};
	u8 dat_hvflip4[] = {0x90, 0x00, 0x24};
	u8 dat_wbal2[] = {0x90, 0x00, 0x00};

	/* Less than 4 images received -> too early to set the settings */
	if (sd->nbIm < 4) {
		sd->waitSet = 1;
		return 0;
	}
	sd->waitSet = 0;

	if (freq != sd->vold.AC50Hz) {
		sd->vold.AC50Hz = freq;

		dat_freq2[2] = freq ? 0xc0 : 0x80;
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_freq1);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_freq2);
		msleep(20);
	}

	if (wbal != sd->vold.whitebal) {
		sd->vold.whitebal = wbal;
		if (wbal < 0 || wbal > sd->vmax.whitebal)
			wbal = 0;

		dat_multi1[2] = 0x9d;
		dat_multi3[2] = dat_multi1[2] + 1;
		if (wbal == 0) {
			dat_multi4[2] = dat_multi2[2] = 0;
			dat_wbal2[2] = 0x17;
		} else if (wbal == 1) {
			dat_multi4[2] = dat_multi2[2] = 0;
			dat_wbal2[2] = 0x35;
		} else if (wbal == 2) {
			dat_multi4[2] = dat_multi2[2] = 0x20;
			dat_wbal2[2] = 0x17;
		}
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi1);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi2);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi3);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi4);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_wbal1);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_wbal2);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi5);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi6);
	}

	if (mirror != sd->vold.mirror || flip != sd->vold.flip) {
		sd->vold.mirror = mirror;
		sd->vold.flip   = flip;

		dat_hvflip2[2] = 0x6c + 2 * (1 - flip) + (1 - mirror);
		dat_hvflip4[2] = 0x24 + 2 * (1 - flip) + (1 - mirror);

		fetch_idxdata(gspca_dev, tbl_init_post_alt_3B,
				ARRAY_SIZE(tbl_init_post_alt_3B));

		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_hvflip1);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_hvflip2);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_hvflip3);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_hvflip4);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_hvflip5);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_hvflip6);
		msleep(40);

		if (reso == IMAGE_640 || reso == IMAGE_800)
			fetch_idxdata(gspca_dev, tbl_middle_hvflip_low,
					ARRAY_SIZE(tbl_middle_hvflip_low));
		else
			fetch_idxdata(gspca_dev, tbl_middle_hvflip_big,
					ARRAY_SIZE(tbl_middle_hvflip_big));

		fetch_idxdata(gspca_dev, tbl_end_hvflip,
				ARRAY_SIZE(tbl_end_hvflip));
	}

	if (bright != sd->vold.brightness) {
		sd->vold.brightness = bright;
		if (bright < 0 || bright > sd->vmax.brightness)
			bright = 0;

		dat_bright2[2] = bright;
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_bright1);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_bright2);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_bright3);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_bright4);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_bright5);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_bright6);
	}

	if (cntr != sd->vold.contrast || gam != sd->vold.gamma) {
		sd->vold.contrast = cntr;
		if (cntr < 0 || cntr > sd->vmax.contrast)
			cntr = 0;
		sd->vold.gamma = gam;
		if (gam < 0 || gam > sd->vmax.gamma)
			gam = 0;

		dat_multi1[2] = 0x6d;
		dat_multi3[2] = dat_multi1[2] + 1;
		if (cntr == 0)
			cntr = 4;
		dat_multi4[2] = dat_multi2[2] = cntr * 0x10 + 2 - gam;
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi1);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi2);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi3);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi4);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi5);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi6);
	}

	if (backlight != sd->vold.backlight) {
		sd->vold.backlight = backlight;
		if (backlight < 0 || backlight > sd->vmax.backlight)
			backlight = 0;

		dat_multi1[2] = 0x9d;
		dat_multi3[2] = dat_multi1[2] + 1;
		dat_multi4[2] = dat_multi2[2] = backlight;
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi1);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi2);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi3);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi4);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi5);
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0033, 3, dat_multi6);
	}

	if (sharp != sd->vold.sharpness) {
		sd->vold.sharpness = sharp;
		if (sharp < 0 || sharp > sd->vmax.sharpness)
			sharp = 0;

		dat_sharp[1] = sharp;
		ctrl_out(gspca_dev, 0x40, 3, 0x7a00, 0x0032, 3, dat_sharp);
	}

	if (hue != sd->vold.hue) {
		sd->swapRB = hue;
		sd->vold.hue = hue;
	}

	return 0;
}

static void mi2020_post_unset_alt(struct gspca_dev *gspca_dev)
{
	ctrl_out(gspca_dev, 0x40, 5, 0x0000, 0x0000, 0, NULL);
	msleep(40);
	ctrl_out(gspca_dev, 0x40, 1, 0x0001, 0x0000, 0, NULL);
}
