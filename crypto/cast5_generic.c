/* Kernel cryptographic api.
* cast5.c - Cast5 cipher algorithm (rfc2144).
*
* Derived from GnuPG implementation of cast5.
*
* Major Changes.
*	Complete conformance to rfc2144.
*	Supports key size from 40 to 128 bits.
*
* Copyright (C) 1998, 1999, 2000, 2001 Free Software Foundation, Inc.
* Copyright (C) 2003 Kartikey Mahendra Bhatt <kartik_me@hotmail.com>.
*
* This program is free software; you can redistribute it and/or modify it
* under the terms of GNU General Public License as published by the Free
* Software Foundation; either version 2 of the License, or (at your option)
* any later version.
*
* You should have received a copy of the GNU General Public License
* along with this program; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA
*/


#include <asm/byteorder.h>
#include <linux/init.h>
#include <linux/crypto.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/types.h>
#include <crypto/cast5.h>

static const u32 s5[256] = {
	0x7ec90c04, 0x2c6e74b9, 0x9b0e66df, 0xa6337911, 0xb86a7fff,
	0x1dd358f5, 0x44dd9d44, 0x1731167f,
	0x08fbf1fa, 0xe7f511cc, 0xd2051b00, 0x735aba00, 0x2ab722d8,
	0x386381cb, 0xacf6243a, 0x69befd7a,
	0xe6a2e77f, 0xf0c720cd, 0xc4494816, 0xccf5c180, 0x38851640,
	0x15b0a848, 0xe68b18cb, 0x4caadeff,
	0x5f480a01, 0x0412b2aa, 0x259814fc, 0x41d0efe2, 0x4e40b48d,
	0x248eb6fb, 0x8dba1cfe, 0x41a99b02,
	0x1a550a04, 0xba8f65cb, 0x7251f4e7, 0x95a51725, 0xc106ecd7,
	0x97a5980a, 0xc539b9aa, 0x4d79fe6a,
	0xf2f3f763, 0x68af8040, 0xed0c9e56, 0x11b4958b, 0xe1eb5a88,
	0x8709e6b0, 0xd7e07156, 0x4e29fea7,
	0x6366e52d, 0x02d1c000, 0xc4ac8e05, 0x9377f571, 0x0c05372a,
	0x578535f2, 0x2261be02, 0xd642a0c9,
	0xdf13a280, 0x74b55bd2, 0x682199c0, 0xd421e5ec, 0x53fb3ce8,
	0xc8adedb3, 0x28a87fc9, 0x3d959981,
	0x5c1ff900, 0xfe38d399, 0x0c4eff0b, 0x062407ea, 0xaa2f4fb1,
	0x4fb96976, 0x90c79505, 0xb0a8a774,
	0xef55a1ff, 0xe59ca2c2, 0xa6b62d27, 0xe66a4263, 0xdf65001f,
	0x0ec50966, 0xdfdd55bc, 0x29de0655,
	0x911e739a, 0x17af8975, 0x32c7911c, 0x89f89468, 0x0d01e980,
	0x524755f4, 0x03b63cc9, 0x0cc844b2,
	0xbcf3f0aa, 0x87ac36e9, 0xe53a7426, 0x01b3d82b, 0x1a9e7449,
	0x64ee2d7e, 0xcddbb1da, 0x01c94910,
	0xb868bf80, 0x0d26f3fd, 0x9342ede7, 0x04a5c284, 0x636737b6,
	0x50f5b616, 0xf24766e3, 0x8eca36c1,
	0x136e05db, 0xfef18391, 0xfb887a37, 0xd6e7f7d4, 0xc7fb7dc9,
	0x3063fcdf, 0xb6f589de, 0xec2941da,
	0x26e46695, 0xb7566419, 0xf654efc5, 0xd08d58b7, 0x48925401,
	0xc1bacb7f, 0xe5ff550f, 0xb6083049,
	0x5bb5d0e8, 0x87d72e5a, 0xab6a6ee1, 0x223a66ce, 0xc62bf3cd,
	0x9e0885f9, 0x68cb3e47, 0x086c010f,
	0xa21de820, 0xd18b69de, 0xf3f65777, 0xfa02c3f6, 0x407edac3,
	0xcbb3d550, 0x1793084d, 0xb0d70eba,
	0x0ab378d5, 0xd951fb0c, 0xded7da56, 0x4124bbe4, 0x94ca0b56,
	0x0f5755d1, 0xe0e1e56e, 0x6184b5be,
	0x580a249f, 0x94f74bc0, 0xe327888e, 0x9f7b5561, 0xc3dc0280,
	0x05687715, 0x646c6bd7, 0x44904db3,
	0x66b4f0a3, 0xc0f1648a, 0x697ed5af, 0x49e92ff6, 0x309e374f,
	0x2cb6356a, 0x85808573, 0x4991f840,
	0x76f0ae02, 0x083be84d, 0x28421c9a, 0x44489406, 0x736e4cb8,
	0xc1092910, 0x8bc95fc6, 0x7d869cf4,
	0x134f616f, 0x2e77118d, 0xb31b2be1, 0xaa90b472, 0x3ca5d717,
	0x7d161bba, 0x9cad9010, 0xaf462ba2,
	0x9fe459d2, 0x45d34559, 0xd9f2da13, 0xdbc65487, 0xf3e4f94e,
	0x176d486f, 0x097c13ea, 0x631da5c7,
	0x445f7382, 0x175683f4, 0xcdc66a97, 0x70be0288, 0xb3cdcf72,
	0x6e5dd2f3, 0x20936079, 0x459b80a5,
	0xbe60e2db, 0xa9c23101, 0xeba5315c, 0x224e42f2, 0x1c5c1572,
	0xf6721b2c, 0x1ad2fff3, 0x8c25404e,
	0x324ed72f, 0x4067b7fd, 0x0523138e, 0x5ca3bc78, 0xdc0fd66e,
	0x75922283, 0x784d6b17, 0x58ebb16e,
	0x44094f85, 0x3f481d87, 0xfcfeae7b, 0x77b5ff76, 0x8c2302bf,
	0xaaf47556, 0x5f46b02a, 0x2b092801,
	0x3d38f5f7, 0x0ca81f36, 0x52af4a8a, 0x66d5e7c0, 0xdf3b0874,
	0x95055110, 0x1b5ad7a8, 0xf61ed5ad,
	0x6cf6e479, 0x20758184, 0xd0cefa65, 0x88f7be58, 0x4a046826,
	0x0ff6f8f3, 0xa09c7f70, 0x5346aba0,
	0x5ce96c28, 0xe176eda3, 0x6bac307f, 0x376829d2, 0x85360fa9,
	0x17e3fe2a, 0x24b79767, 0xf5a96b20,
	0xd6cd2595, 0x68ff1ebf, 0x7555442c, 0xf19f06be, 0xf9e0659a,
	0xeeb9491d, 0x34010718, 0xbb30cab8,
	0xe822fe15, 0x88570983, 0x750e6249, 0xda627e55, 0x5e76ffa8,
	0xb1534546, 0x6d47de08, 0xefe9e7d4
};
static const u32 s6[256] = {
	0xf6fa8f9d, 0x2cac6ce1, 0x4ca34867, 0xe2337f7c, 0x95db08e7,
	0x016843b4, 0xeced5cbc, 0x325553ac,
	0xbf9f0960, 0xdfa1e2ed, 0x83f0579d, 0x63ed86b9, 0x1ab6a6b8,
	0xde5ebe39, 0xf38ff732, 0x8989b138,
	0x33f14961, 0xc01937bd, 0xf506c6da, 0xe4625e7e, 0xa308ea99,
	0x4e23e33c, 0x79cbd7cc, 0x48a14367,
	0xa3149619, 0xfec94bd5, 0xa114174a, 0xeaa01866, 0xa084db2d,
	0x09a8486f, 0xa888614a, 0x2900af98,
	0x01665991, 0xe1992863, 0xc8f30c60, 0x2e78ef3c, 0xd0d51932,
	0xcf0fec14, 0xf7ca07d2, 0xd0a82072,
	0xfd41197e, 0x9305a6b0, 0xe86be3da, 0x74bed3cd, 0x372da53c,
	0x4c7f4448, 0xdab5d440, 0x6dba0ec3,
	0x083919a7, 0x9fbaeed9, 0x49dbcfb0, 0x4e670c53, 0x5c3d9c01,
	0x64bdb941, 0x2c0e636a, 0xba7dd9cd,
	0xea6f7388, 0xe70bc762, 0x35f29adb, 0x5c4cdd8d, 0xf0d48d8c,
	0xb88153e2, 0x08a19866, 0x1ae2eac8,
	0x284caf89, 0xaa928223, 0x9334be53, 0x3b3a21bf, 0x16434be3,
	0x9aea3906, 0xefe8c36e, 0xf890cdd9,
	0x80226dae, 0xc340a4a3, 0xdf7e9c09, 0xa694a807, 0x5b7c5ecc,
	0x221db3a6, 0x9a69a02f, 0x68818a54,
	0xceb2296f, 0x53c0843a, 0xfe893655, 0x25bfe68a, 0xb4628abc,
	0xcf222ebf, 0x25ac6f48, 0xa9a99387,
	0x53bddb65, 0xe76ffbe7, 0xe967fd78, 0x0ba93563, 0x8e342bc1,
	0xe8a11be9, 0x4980740d, 0xc8087dfc,
	0x8de4bf99, 0xa11101a0, 0x7fd37975, 0xda5a26c0, 0xe81f994f,
	0x9528cd89, 0xfd339fed, 0xb87834bf,
	0x5f04456d, 0x22258698, 0xc9c4c83b, 0x2dc156be, 0x4f628daa,
	0x57f55ec5, 0xe2220abe, 0xd2916ebf,
	0x4ec75b95, 0x24f2c3c0, 0x42d15d99, 0xcd0d7fa0, 0x7b6e27ff,
	0xa8dc8af0, 0x7345c106, 0xf41e232f,
	0x35162386, 0xe6ea8926, 0x3333b094, 0x157ec6f2, 0x372b74af,
	0x692573e4, 0xe9a9d848, 0xf3160289,
	0x3a62ef1d, 0xa787e238, 0xf3a5f676, 0x74364853, 0x20951063,
	0x4576698d, 0xb6fad407, 0x592af950,
	0x36f73523, 0x4cfb6e87, 0x7da4cec0, 0x6c152daa, 0xcb0396a8,
	0xc50dfe5d, 0xfcd707ab, 0x0921c42f,
	0x89dff0bb, 0x5fe2be78, 0x448f4f33, 0x754613c9, 0x2b05d08d,
	0x48b9d585, 0xdc049441, 0xc8098f9b,
	0x7dede786, 0xc39a3373, 0x42410005, 0x6a091751, 0x0ef3c8a6,
	0x890072d6, 0x28207682, 0xa9a9f7be,
	0xbf32679d, 0xd45b5b75, 0xb353fd00, 0xcbb0e358, 0x830f220a,
	0x1f8fb214, 0xd372cf08, 0xcc3c4a13,
	0x8cf63166, 0x061c87be, 0x88c98f88, 0x6062e397, 0x47cf8e7a,
	0xb6c85283, 0x3cc2acfb, 0x3fc06976,
	0x4e8f0252, 0x64d8314d, 0xda3870e3, 0x1e665459, 0xc10908f0,
	0x513021a5, 0x6c5b68b7, 0x822f8aa0,
	0x3007cd3e, 0x74719eef, 0xdc872681, 0x073340d4, 0x7e432fd9,
	0x0c5ec241, 0x8809286c, 0xf592d891,
	0x08a930f6, 0x957ef305, 0xb7fbffbd, 0xc266e96f, 0x6fe4ac98,
	0xb173ecc0, 0xbc60b42a, 0x953498da,
	0xfba1ae12, 0x2d4bd736, 0x0f25faab, 0xa4f3fceb, 0xe2969123,
	0x257f0c3d, 0x9348af49, 0x361400bc,
	0xe8816f4a, 0x3814f200, 0xa3f94043, 0x9c7a54c2, 0xbc704f57,
	0xda41e7f9, 0xc25ad33a, 0x54f4a084,
	0xb17f5505, 0x59357cbe, 0xedbd15c8, 0x7f97c5ab, 0xba5ac7b5,
	0xb6f6deaf, 0x3a479c3a, 0x5302da25,
	0x653d7e6a, 0x54268d49, 0x51a477ea, 0x5017d55b, 0xd7d25d88,
	0x44136c76, 0x0404a8c8, 0xb8e5a121,
	0xb81a928a, 0x60ed5869, 0x97c55b96, 0xeaec991b, 0x29935913,
	0x01fdb7f1, 0x088e8dfa, 0x9ab6f6f5,
	0x3b4cbf9f, 0x4a5de3ab, 0xe6051d35, 0xa0e1d855, 0xd36b4cf1,
	0xf544edeb, 0xb0e93524, 0xbebb8fbd,
	0xa2d762cf, 0x49c92f54, 0x38b5f331, 0x7128a454, 0x48392905,
	0xa65b1db8, 0x851c97bd, 0xd675cf2f
};
static const u32 s7[256] = {
	0x85e04019, 0x332bf567, 0x662dbfff, 0xcfc65693, 0x2a8d7f6f,
	0xab9bc912, 0xde6008a1, 0x2028da1f,
	0x0227bce7, 0x4d642916, 0x18fac300, 0x50f18b82, 0x2cb2cb11,
	0xb232e75c, 0x4b3695f2, 0xb28707de,
	0xa05fbcf6, 0xcd4181e9, 0xe150210c, 0xe24ef1bd, 0xb168c381,
	0xfde4e789, 0x5c79b0d8, 0x1e8bfd43,
	0x4d495001, 0x38be4341, 0x913cee1d, 0x92a79c3f, 0x089766be,
	0xbaeeadf4, 0x1286becf, 0xb6eacb19,
	0x2660c200, 0x7565bde4, 0x64241f7a, 0x8248dca9, 0xc3b3ad66,
	0x28136086, 0x0bd8dfa8, 0x356d1cf2,
	0x107789be, 0xb3b2e9ce, 0x0502aa8f, 0x0bc0351e, 0x166bf52a,
	0xeb12ff82, 0xe3486911, 0xd34d7516,
	0x4e7b3aff, 0x5f43671b, 0x9cf6e037, 0x4981ac83, 0x334266ce,
	0x8c9341b7, 0xd0d854c0, 0xcb3a6c88,
	0x47bc2829, 0x4725ba37, 0xa66ad22b, 0x7ad61f1e, 0x0c5cbafa,
	0x4437f107, 0xb6e79962, 0x42d2d816,
	0x0a961288, 0xe1a5c06e, 0x13749e67, 0x72fc081a, 0xb1d139f7,
	0xf9583745, 0xcf19df58, 0xbec3f756,
	0xc06eba30, 0x07211b24, 0x45c28829, 0xc95e317f, 0xbc8ec511,
	0x38bc46e9, 0xc6e6fa14, 0xbae8584a,
	0xad4ebc46, 0x468f508b, 0x7829435f, 0xf124183b, 0x821dba9f,
	0xaff60ff4, 0xea2c4e6d, 0x16e39264,
	0x92544a8b, 0x009b4fc3, 0xaba68ced, 0x9ac96f78, 0x06a5b79a,
	0xb2856e6e, 0x1aec3ca9, 0xbe838688,
	0x0e0804e9, 0x55f1be56, 0xe7e5363b, 0xb3a1f25d, 0xf7debb85,
	0x61fe033c, 0x16746233, 0x3c034c28,
	0xda6d0c74, 0x79aac56c, 0x3ce4e1ad, 0x51f0c802, 0x98f8f35a,
	0x1626a49f, 0xeed82b29, 0x1d382fe3,
	0x0c4fb99a, 0xbb325778, 0x3ec6d97b, 0x6e77a6a9, 0xcb658b5c,
	0xd45230c7, 0x2bd1408b, 0x60c03eb7,
	0xb9068d78, 0xa33754f4, 0xf430c87d, 0xc8a71302, 0xb96d8c32,
	0xebd4e7be, 0xbe8b9d2d, 0x7979fb06,
	0xe7225308, 0x8b75cf77, 0x11ef8da4, 0xe083c858, 0x8d6b786f,
	0x5a6317a6, 0xfa5cf7a0, 0x5dda0033,
	0xf28ebfb0, 0xf5b9c310, 0xa0eac280, 0x08b9767a, 0xa3d9d2b0,
	0x79d34217, 0x021a718d, 0x9ac6336a,
	0x2711fd60, 0x438050e3, 0x069908a8, 0x3d7fedc4, 0x826d2bef,
	0x4eeb8476, 0x488dcf25, 0x36c9d566,
	0x28e74e41, 0xc2610aca, 0x3d49a9cf, 0xbae3b9df, 0xb65f8de6,
	0x92aeaf64, 0x3ac7d5e6, 0x9ea80509,
	0xf22b017d, 0xa4173f70, 0xdd1e16c3, 0x15e0d7f9, 0x50b1b887,
	0x2b9f4fd5, 0x625aba82, 0x6a017962,
	0x2ec01b9c, 0x15488aa9, 0xd716e740, 0x40055a2c, 0x93d29a22,
	0xe32dbf9a, 0x058745b9, 0x3453dc1e,
	0xd699296e, 0x496cff6f, 0x1c9f4986, 0xdfe2ed07, 0xb87242d1,
	0x19de7eae, 0x053e561a, 0x15ad6f8c,
	0x66626c1c, 0x7154c24c, 0xea082b2a, 0x93eb2939, 0x17dcb0f0,
	0x58d4f2ae, 0x9ea294fb, 0x52cf564c,
	0x9883fe66, 0x2ec40581, 0x763953c3, 0x01d6692e, 0xd3a0c108,
	0xa1e7160e, 0xe4f2dfa6, 0x693ed285,
	0x74904698, 0x4c2b0edd, 0x4f757656, 0x5d393378, 0xa132234f,
	0x3d321c5d, 0xc3f5e194, 0x4b269301,
	0xc79f022f, 0x3c997e7e, 0x5e4f9504, 0x3ffafbbd, 0x76f7ad0e,
	0x296693f4, 0x3d1fce6f, 0xc61e45be,
	0xd3b5ab34, 0xf72bf9b7, 0x1b0434c0, 0x4e72b567, 0x5592a33d,
	0xb5229301, 0xcfd2a87f, 0x60aeb767,
	0x1814386b, 0x30bcc33d, 0x38a0c07d, 0xfd1606f2, 0xc363519b,
	0x589dd390, 0x5479f8e6, 0x1cb8d647,
	0x97fd61a9, 0xea7759f4, 0x2d57539d, 0x569a58cf, 0xe84e63ad,
	0x462e1b78, 0x6580f87e, 0xf3817914,
	0x91da55f4, 0x40a230f3, 0xd1988f35, 0xb6e318d2, 0x3ffa50bc,
	0x3d40f021, 0xc3c0bdae, 0x4958c24c,
	0x518f36b2, 0x84b1d370, 0x0fedce83, 0x878ddada, 0xf2a279c7,
	0x94e01be8, 0x90716f4b, 0x954b8aa3
};
static const u32 sb8[256] = {
	0xe216300d, 0xbbddfffc, 0xa7ebdabd, 0x35648095, 0x7789f8b7,
	0xe6c1121b, 0x0e241600, 0x052ce8b5,
	0x11a9cfb0, 0xe5952f11, 0xece7990a, 0x9386d174, 0x2a42931c,
	0x76e38111, 0xb12def3a, 0x37ddddfc,
	0xde9adeb1, 0x0a0cc32c, 0xbe197029, 0x84a00940, 0xbb243a0f,
	0xb4d137cf, 0xb44e79f0, 0x049eedfd,
	0x0b15a15d, 0x480d3168, 0x8bbbde5a, 0x669ded42, 0xc7ece831,
	0x3f8f95e7, 0x72df191b, 0x7580330d,
	0x94074251, 0x5c7dcdfa, 0xabbe6d63, 0xaa402164, 0xb301d40a,
	0x02e7d1ca, 0x53571dae, 0x7a3182a2,
	0x12a8ddec, 0xfdaa335d, 0x176f43e8, 0x71fb46d4, 0x38129022,
	0xce949ad4, 0xb84769ad, 0x965bd862,
	0x82f3d055, 0x66fb9767, 0x15b80b4e, 0x1d5b47a0, 0x4cfde06f,
	0xc28ec4b8, 0x57e8726e, 0x647a78fc,
	0x99865d44, 0x608bd593, 0x6c200e03, 0x39dc5ff6, 0x5d0b00a3,
	0xae63aff2, 0x7e8bd632, 0x70108c0c,
	0xbbd35049, 0x2998df04, 0x980cf42a, 0x9b6df491, 0x9e7edd53,
	0x06918548, 0x58cb7e07, 0x3b74ef2e,
	0x522fffb1, 0xd24708cc, 0x1c7e27cd, 0xa4eb215b, 0x3cf1d2e2,
	0x19b47a38, 0x424f7618, 0x35856039,
	0x9d17dee7, 0x27eb35e6, 0xc9aff67b, 0x36baf5b8, 0x09c467cd,
	0xc18910b1, 0xe11dbf7b, 0x06cd1af8,
	0x7170c608, 0x2d5e3354, 0xd4de495a, 0x64c6d006, 0xbcc0c62c,
	0x3dd00db3, 0x708f8f34, 0x77d51b42,
	0x264f620f, 0x24b8d2bf, 0x15c1b79e, 0x46a52564, 0xf8d7e54e,
	0x3e378160, 0x7895cda5, 0x859c15a5,
	0xe6459788, 0xc37bc75f, 0xdb07ba0c, 0x0676a3ab, 0x7f229b1e,
	0x31842e7b, 0x24259fd7, 0xf8bef472,
	0x835ffcb8, 0x6df4c1f2, 0x96f5b195, 0xfd0af0fc, 0xb0fe134c,
	0xe2506d3d, 0x4f9b12ea, 0xf215f225,
	0xa223736f, 0x9fb4c428, 0x25d04979, 0x34c713f8, 0xc4618187,
	0xea7a6e98, 0x7cd16efc, 0x1436876c,
	0xf1544107, 0xbedeee14, 0x56e9af27, 0xa04aa441, 0x3cf7c899,
	0x92ecbae6, 0xdd67016d, 0x151682eb,
	0xa842eedf, 0xfdba60b4, 0xf1907b75, 0x20e3030f, 0x24d8c29e,
	0xe139673b, 0xefa63fb8, 0x71873054,
	0xb6f2cf3b, 0x9f326442, 0xcb15a4cc, 0xb01a4504, 0xf1e47d8d,
	0x844a1be5, 0xbae7dfdc, 0x42cbda70,
	0xcd7dae0a, 0x57e85b7a, 0xd53f5af6, 0x20cf4d8c, 0xcea4d428,
	0x79d130a4, 0x3486ebfb, 0x33d3cddc,
	0x77853b53, 0x37effcb5, 0xc5068778, 0xe580b3e6, 0x4e68b8f4,
	0xc5c8b37e, 0x0d809ea2, 0x398feb7c,
	0x132a4f94, 0x43b7950e, 0x2fee7d1c, 0x223613bd, 0xdd06caa2,
	0x37df932b, 0xc4248289, 0xacf3ebc3,
	0x5715f6b7, 0xef3478dd, 0xf267616f, 0xc148cbe4, 0x9052815e,
	0x5e410fab, 0xb48a2465, 0x2eda7fa4,
	0xe87b40e4, 0xe98ea084, 0x5889e9e1, 0xefd390fc, 0xdd07d35b,
	0xdb485694, 0x38d7e5b2, 0x57720101,
	0x730edebc, 0x5b643113, 0x94917e4f, 0x503c2fba, 0x646f1282,
	0x7523d24a, 0xe0779695, 0xf9c17a8f,
	0x7a5b2121, 0xd187b896, 0x29263a4d, 0xba510cdf, 0x81f47c9f,
	0xad1163ed, 0xea7b5965, 0x1a00726e,
	0x11403092, 0x00da6d77, 0x4a0cdd61, 0xad1f4603, 0x605bdfb0,
	0x9eedc364, 0x22ebe6a8, 0xcee7d28a,
	0xa0e736a0, 0x5564a6b9, 0x10853209, 0xc7eb8f37, 0x2de705ca,
	0x8951570f, 0xdf09822b, 0xbd691a6c,
	0xaa12e4f2, 0x87451c0f, 0xe0f6a27a, 0x3ada4819, 0x4cf1764f,
	0x0d771c2b, 0x67cdb156, 0x350d8384,
	0x5938fa0f, 0x42399ef3, 0x36997b07, 0x0e84093d, 0x4aa93e61,
	0x8360d87b, 0x1fa98b0c, 0x1149382c,
	0xe97625a5, 0x0614d1b7, 0x0e25244b, 0x0c768347, 0x589e8d82,
	0x0d2059d1, 0xa466bb1e, 0xf8da0a82,
	0x04f19130, 0xba6e4ec0, 0x99265164, 0x1ee7230d, 0x50b2ad80,
	0xeaee6801, 0x8db2a283, 0xea8bf59e
};

#define s1 cast_s1
#define s2 cast_s2
#define s3 cast_s3
#define s4 cast_s4

#define F1(D, m, r)  ((I = ((m) + (D))), (I = rol32(I, (r))),   \
	(((s1[I >> 24] ^ s2[(I>>16)&0xff]) - s3[(I>>8)&0xff]) + s4[I&0xff]))
#define F2(D, m, r)  ((I = ((m) ^ (D))), (I = rol32(I, (r))),   \
	(((s1[I >> 24] - s2[(I>>16)&0xff]) + s3[(I>>8)&0xff]) ^ s4[I&0xff]))
#define F3(D, m, r)  ((I = ((m) - (D))), (I = rol32(I, (r))),   \
	(((s1[I >> 24] + s2[(I>>16)&0xff]) ^ s3[(I>>8)&0xff]) - s4[I&0xff]))


void __cast5_encrypt(struct cast5_ctx *c, u8 *outbuf, const u8 *inbuf)
{
	const __be32 *src = (const __be32 *)inbuf;
	__be32 *dst = (__be32 *)outbuf;
	u32 l, r, t;
	u32 I;			/* used by the Fx macros */
	u32 *Km;
	u8 *Kr;

	Km = c->Km;
	Kr = c->Kr;

	/* (L0,R0) <-- (m1...m64).  (Split the plaintext into left and
	 * right 32-bit halves L0 = m1...m32 and R0 = m33...m64.)
	 */
	l = be32_to_cpu(src[0]);
	r = be32_to_cpu(src[1]);

	/* (16 rounds) for i from 1 to 16, compute Li and Ri as follows:
	 *  Li = Ri-1;
	 *  Ri = Li-1 ^ f(Ri-1,Kmi,Kri), where f is defined in Section 2.2
	 * Rounds 1, 4, 7, 10, 13, and 16 use f function Type 1.
	 * Rounds 2, 5, 8, 11, and 14 use f function Type 2.
	 * Rounds 3, 6, 9, 12, and 15 use f function Type 3.
	 */

	t = l; l = r; r = t ^ F1(r, Km[0], Kr[0]);
	t = l; l = r; r = t ^ F2(r, Km[1], Kr[1]);
	t = l; l = r; r = t ^ F3(r, Km[2], Kr[2]);
	t = l; l = r; r = t ^ F1(r, Km[3], Kr[3]);
	t = l; l = r; r = t ^ F2(r, Km[4], Kr[4]);
	t = l; l = r; r = t ^ F3(r, Km[5], Kr[5]);
	t = l; l = r; r = t ^ F1(r, Km[6], Kr[6]);
	t = l; l = r; r = t ^ F2(r, Km[7], Kr[7]);
	t = l; l = r; r = t ^ F3(r, Km[8], Kr[8]);
	t = l; l = r; r = t ^ F1(r, Km[9], Kr[9]);
	t = l; l = r; r = t ^ F2(r, Km[10], Kr[10]);
	t = l; l = r; r = t ^ F3(r, Km[11], Kr[11]);
	if (!(c->rr)) {
		t = l; l = r; r = t ^ F1(r, Km[12], Kr[12]);
		t = l; l = r; r = t ^ F2(r, Km[13], Kr[13]);
		t = l; l = r; r = t ^ F3(r, Km[14], Kr[14]);
		t = l; l = r; r = t ^ F1(r, Km[15], Kr[15]);
	}

	/* c1...c64 <-- (R16,L16).  (Exchange final blocks L16, R16 and
	 *  concatenate to form the ciphertext.) */
	dst[0] = cpu_to_be32(r);
	dst[1] = cpu_to_be32(l);
}
EXPORT_SYMBOL_GPL(__cast5_encrypt);

static void cast5_encrypt(struct crypto_tfm *tfm, u8 *outbuf, const u8 *inbuf)
{
	__cast5_encrypt(crypto_tfm_ctx(tfm), outbuf, inbuf);
}

void __cast5_decrypt(struct cast5_ctx *c, u8 *outbuf, const u8 *inbuf)
{
	const __be32 *src = (const __be32 *)inbuf;
	__be32 *dst = (__be32 *)outbuf;
	u32 l, r, t;
	u32 I;
	u32 *Km;
	u8 *Kr;

	Km = c->Km;
	Kr = c->Kr;

	l = be32_to_cpu(src[0]);
	r = be32_to_cpu(src[1]);

	if (!(c->rr)) {
		t = l; l = r; r = t ^ F1(r, Km[15], Kr[15]);
		t = l; l = r; r = t ^ F3(r, Km[14], Kr[14]);
		t = l; l = r; r = t ^ F2(r, Km[13], Kr[13]);
		t = l; l = r; r = t ^ F1(r, Km[12], Kr[12]);
	}
	t = l; l = r; r = t ^ F3(r, Km[11], Kr[11]);
	t = l; l = r; r = t ^ F2(r, Km[10], Kr[10]);
	t = l; l = r; r = t ^ F1(r, Km[9], Kr[9]);
	t = l; l = r; r = t ^ F3(r, Km[8], Kr[8]);
	t = l; l = r; r = t ^ F2(r, Km[7], Kr[7]);
	t = l; l = r; r = t ^ F1(r, Km[6], Kr[6]);
	t = l; l = r; r = t ^ F3(r, Km[5], Kr[5]);
	t = l; l = r; r = t ^ F2(r, Km[4], Kr[4]);
	t = l; l = r; r = t ^ F1(r, Km[3], Kr[3]);
	t = l; l = r; r = t ^ F3(r, Km[2], Kr[2]);
	t = l; l = r; r = t ^ F2(r, Km[1], Kr[1]);
	t = l; l = r; r = t ^ F1(r, Km[0], Kr[0]);

	dst[0] = cpu_to_be32(r);
	dst[1] = cpu_to_be32(l);
}
EXPORT_SYMBOL_GPL(__cast5_decrypt);

static void cast5_decrypt(struct crypto_tfm *tfm, u8 *outbuf, const u8 *inbuf)
{
	__cast5_decrypt(crypto_tfm_ctx(tfm), outbuf, inbuf);
}

static void key_schedule(u32 *x, u32 *z, u32 *k)
{

#define xi(i)   ((x[(i)/4] >> (8*(3-((i)%4)))) & 0xff)
#define zi(i)   ((z[(i)/4] >> (8*(3-((i)%4)))) & 0xff)

	z[0] = x[0] ^ s5[xi(13)] ^ s6[xi(15)] ^ s7[xi(12)] ^ sb8[xi(14)] ^
	    s7[xi(8)];
	z[1] = x[2] ^ s5[zi(0)] ^ s6[zi(2)] ^ s7[zi(1)] ^ sb8[zi(3)] ^
	    sb8[xi(10)];
	z[2] = x[3] ^ s5[zi(7)] ^ s6[zi(6)] ^ s7[zi(5)] ^ sb8[zi(4)] ^
	    s5[xi(9)];
	z[3] = x[1] ^ s5[zi(10)] ^ s6[zi(9)] ^ s7[zi(11)] ^ sb8[zi(8)] ^
	    s6[xi(11)];
	k[0] = s5[zi(8)] ^ s6[zi(9)] ^ s7[zi(7)] ^ sb8[zi(6)] ^ s5[zi(2)];
	k[1] = s5[zi(10)] ^ s6[zi(11)] ^ s7[zi(5)] ^ sb8[zi(4)] ^
	    s6[zi(6)];
	k[2] = s5[zi(12)] ^ s6[zi(13)] ^ s7[zi(3)] ^ sb8[zi(2)] ^
	    s7[zi(9)];
	k[3] = s5[zi(14)] ^ s6[zi(15)] ^ s7[zi(1)] ^ sb8[zi(0)] ^
	    sb8[zi(12)];

	x[0] = z[2] ^ s5[zi(5)] ^ s6[zi(7)] ^ s7[zi(4)] ^ sb8[zi(6)] ^
	    s7[zi(0)];
	x[1] = z[0] ^ s5[xi(0)] ^ s6[xi(2)] ^ s7[xi(1)] ^ sb8[xi(3)] ^
	    sb8[zi(2)];
	x[2] = z[1] ^ s5[xi(7)] ^ s6[xi(6)] ^ s7[xi(5)] ^ sb8[xi(4)] ^
	    s5[zi(1)];
	x[3] = z[3] ^ s5[xi(10)] ^ s6[xi(9)] ^ s7[xi(11)] ^ sb8[xi(8)] ^
	    s6[zi(3)];
	k[4] = s5[xi(3)] ^ s6[xi(2)] ^ s7[xi(12)] ^ sb8[xi(13)] ^
	    s5[xi(8)];
	k[5] = s5[xi(1)] ^ s6[xi(0)] ^ s7[xi(14)] ^ sb8[xi(15)] ^
	    s6[xi(13)];
	k[6] = s5[xi(7)] ^ s6[xi(6)] ^ s7[xi(8)] ^ sb8[xi(9)] ^ s7[xi(3)];
	k[7] = s5[xi(5)] ^ s6[xi(4)] ^ s7[xi(10)] ^ sb8[xi(11)] ^
	    sb8[xi(7)];

	z[0] = x[0] ^ s5[xi(13)] ^ s6[xi(15)] ^ s7[xi(12)] ^ sb8[xi(14)] ^
	    s7[xi(8)];
	z[1] = x[2] ^ s5[zi(0)] ^ s6[zi(2)] ^ s7[zi(1)] ^ sb8[zi(3)] ^
	    sb8[xi(10)];
	z[2] = x[3] ^ s5[zi(7)] ^ s6[zi(6)] ^ s7[zi(5)] ^ sb8[zi(4)] ^
	    s5[xi(9)];
	z[3] = x[1] ^ s5[zi(10)] ^ s6[zi(9)] ^ s7[zi(11)] ^ sb8[zi(8)] ^
	    s6[xi(11)];
	k[8] = s5[zi(3)] ^ s6[zi(2)] ^ s7[zi(12)] ^ sb8[zi(13)] ^
	    s5[zi(9)];
	k[9] = s5[zi(1)] ^ s6[zi(0)] ^ s7[zi(14)] ^ sb8[zi(15)] ^
	    s6[zi(12)];
	k[10] = s5[zi(7)] ^ s6[zi(6)] ^ s7[zi(8)] ^ sb8[zi(9)] ^ s7[zi(2)];
	k[11] = s5[zi(5)] ^ s6[zi(4)] ^ s7[zi(10)] ^ sb8[zi(11)] ^
	    sb8[zi(6)];

	x[0] = z[2] ^ s5[zi(5)] ^ s6[zi(7)] ^ s7[zi(4)] ^ sb8[zi(6)] ^
	    s7[zi(0)];
	x[1] = z[0] ^ s5[xi(0)] ^ s6[xi(2)] ^ s7[xi(1)] ^ sb8[xi(3)] ^
	    sb8[zi(2)];
	x[2] = z[1] ^ s5[xi(7)] ^ s6[xi(6)] ^ s7[xi(5)] ^ sb8[xi(4)] ^
	    s5[zi(1)];
	x[3] = z[3] ^ s5[xi(10)] ^ s6[xi(9)] ^ s7[xi(11)] ^ sb8[xi(8)] ^
	    s6[zi(3)];
	k[12] = s5[xi(8)] ^ s6[xi(9)] ^ s7[xi(7)] ^ sb8[xi(6)] ^ s5[xi(3)];
	k[13] = s5[xi(10)] ^ s6[xi(11)] ^ s7[xi(5)] ^ sb8[xi(4)] ^
	    s6[xi(7)];
	k[14] = s5[xi(12)] ^ s6[xi(13)] ^ s7[xi(3)] ^ sb8[xi(2)] ^
	    s7[xi(8)];
	k[15] = s5[xi(14)] ^ s6[xi(15)] ^ s7[xi(1)] ^ sb8[xi(0)] ^
	    sb8[xi(13)];

#undef xi
#undef zi
}


int cast5_setkey(struct crypto_tfm *tfm, const u8 *key, unsigned int key_len)
{
	struct cast5_ctx *c = crypto_tfm_ctx(tfm);
	int i;
	u32 x[4];
	u32 z[4];
	u32 k[16];
	__be32 p_key[4];

	c->rr = key_len <= 10 ? 1 : 0;

	memset(p_key, 0, 16);
	memcpy(p_key, key, key_len);


	x[0] = be32_to_cpu(p_key[0]);
	x[1] = be32_to_cpu(p_key[1]);
	x[2] = be32_to_cpu(p_key[2]);
	x[3] = be32_to_cpu(p_key[3]);

	key_schedule(x, z, k);
	for (i = 0; i < 16; i++)
		c->Km[i] = k[i];
	key_schedule(x, z, k);
	for (i = 0; i < 16; i++)
		c->Kr[i] = k[i] & 0x1f;
	return 0;
}
EXPORT_SYMBOL_GPL(cast5_setkey);

static struct crypto_alg alg = {
	.cra_name		= "cast5",
	.cra_driver_name	= "cast5-generic",
	.cra_priority		= 100,
	.cra_flags		= CRYPTO_ALG_TYPE_CIPHER,
	.cra_blocksize		= CAST5_BLOCK_SIZE,
	.cra_ctxsize		= sizeof(struct cast5_ctx),
	.cra_alignmask		= 3,
	.cra_module		= THIS_MODULE,
	.cra_u			= {
		.cipher = {
			.cia_min_keysize = CAST5_MIN_KEY_SIZE,
			.cia_max_keysize = CAST5_MAX_KEY_SIZE,
			.cia_setkey  = cast5_setkey,
			.cia_encrypt = cast5_encrypt,
			.cia_decrypt = cast5_decrypt
		}
	}
};

static int __init cast5_mod_init(void)
{
	return crypto_register_alg(&alg);
}

static void __exit cast5_mod_fini(void)
{
	crypto_unregister_alg(&alg);
}

module_init(cast5_mod_init);
module_exit(cast5_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Cast5 Cipher Algorithm");
MODULE_ALIAS_CRYPTO("cast5");
MODULE_ALIAS_CRYPTO("cast5-generic");
