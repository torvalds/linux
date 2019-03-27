/*	$NetBSD: t_sha2.c,v 1.3 2012/09/26 22:23:30 joerg Exp $	*/
/*-
 * Copyright (c) 2010 The NetBSD Foundation, Inc.
 * All rights reserved.
 *
 * This code is derived from software contributed to The NetBSD Foundation
 * by Joerg Sonnenberger.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * COPYRIGHT HOLDERS OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY OR CONSEQUENTIAL DAMAGES (INCLUDING,
 * BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED
 * AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT
 * OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 */

#include <sys/cdefs.h>
__RCSID("$NetBSD: t_sha2.c,v 1.3 2012/09/26 22:23:30 joerg Exp $");

#include <atf-c.h>
#include <sys/types.h>
#include <sha2.h>
#include <string.h>

ATF_TC(t_sha256);
ATF_TC(t_sha384);
ATF_TC(t_sha512);

ATF_TP_ADD_TCS(tp)
{
	ATF_TP_ADD_TC(tp, t_sha256);
	ATF_TP_ADD_TC(tp, t_sha384);
	ATF_TP_ADD_TC(tp, t_sha512);

	return atf_no_error();
}

struct testvector {
	const char *vector;
	const char *hash;
};

static const struct testvector test256[] = {
	{ "hello, world", "09ca7e4eaa6e8ae9c7d261167129184883644d07dfba7cbfbc4c8a2e08360d5b" },
	{ "", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855" },
	{ "a", "ca978112ca1bbdcafac231b39a23dc4da786eff8147c4e72b9807785afee48bb" },
	{ "ab", "fb8e20fc2e4c3f248c60c39bd652f3c1347298bb977b8b4d5903b85055620603" },
	{ "abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad" },
	{ "abcd", "88d4266fd4e6338d13b845fcf289579d209c897823b9217da3e161936f031589" },
	{ "abcde", "36bbe50ed96841d10443bcb670d6554f0a34b761be67ec9c4a8ad2c0c44ca42c" },
	{ "abcdef", "bef57ec7f53a6d40beb640a780a639c83bc29ac8a9816f1fc6c5c6dcd93c4721" },
	{ "abcdefg", "7d1a54127b222502f5b79b5fb0803061152a44f92b37e23c6527baf665d4da9a" },
	{ "abcdefgh", "9c56cc51b374c3ba189210d5b6d4bf57790d351c96c47c02190ecf1e430635ab" },
	{ "abcdefghi", "19cc02f26df43cc571bc9ed7b0c4d29224a3ec229529221725ef76d021c8326f" },
	{ "abcdefghij", "72399361da6a7754fec986dca5b7cbaf1c810a28ded4abaf56b2106d06cb78b0" },
	{ "abcdefghijk", "ca2f2069ea0c6e4658222e06f8dd639659cbb5e67cbbba6734bc334a3799bc68" },
	{ "abcdefghijkl", "d682ed4ca4d989c134ec94f1551e1ec580dd6d5a6ecde9f3d35e6e4a717fbde4" },
	{ "abcdefghijklm", "ff10304f1af23606ede1e2d8abcdc94c229047a61458d809d8bbd53ede1f6598" },
	{ "abcdefghijklmn", "0653c7e992d7aad40cb2635738b870e4c154afb346340d02c797d490dd52d5f9" },
	{ "abcdefghijklmno", "41c7760c50efde99bf574ed8fffc7a6dd3405d546d3da929b214c8945acf8a97" },
	{ "abcdefghijklmnop", "f39dac6cbaba535e2c207cd0cd8f154974223c848f727f98b3564cea569b41cf" },
	{ "abcdefghijklmnopq", "918a954ac4dfb54ac39f068d9868227f69ab39bc362e2c9b0083bf6a109d6ad7" },
	{ "abcdefghijklmnopqr", "2d1222692afaf56e95a8ab00879ed023a00db3e26fa14236e542748579285efa" },
	{ "abcdefghijklmnopqrs", "e250f886728b77ba63722c7e65fc73e203101a84281b32332fd67cc6a1ae3e22" },
	{ "abcdefghijklmnopqrst", "dd65eea0329dcb94b17187af9dff28c31a1d78026737a16af75979a1fa4618e5" },
	{ "abcdefghijklmnopqrstu", "25f62a5a3d414ec6e20907df7f367f2b72625aade552db64c07933f6044fc49a" },
	{ "abcdefghijklmnopqrstuv", "f69f9b70d1c9a5442258ca76f8b0a7a45fcb4e31c36141b6357ec591328b0624" },
	{ "abcdefghijklmnopqrstuvw", "7f07818e14d08944ce145629ca54332f5cfad148c590efbcb5c377f4d336e5f4" },
	{ "abcdefghijklmnopqrstuvwq", "063132d7fbec0acb79b2f228777eec8885e7f09bc1896b3ce5aa1843e83de048" },
};

static const struct testvector test384[] = {
	{ "hello, world", "1fcdb6059ce05172a26bbe2a3ccc88ed5a8cd5fc53edfd9053304d429296a6da23b1cd9e5c9ed3bb34f00418a70cdb7e" },
	{ "", "38b060a751ac96384cd9327eb1b1e36a21fdb71114be07434c0cc7bf63f6e1da274edebfe76f65fbd51ad2f14898b95b" },
	{ "a", "54a59b9f22b0b80880d8427e548b7c23abd873486e1f035dce9cd697e85175033caa88e6d57bc35efae0b5afd3145f31" },
	{ "ab", "c7be03ba5bcaa384727076db0018e99248e1a6e8bd1b9ef58a9ec9dd4eeebb3f48b836201221175befa74ddc3d35afdd" },
	{ "abc", "cb00753f45a35e8bb5a03d699ac65007272c32ab0eded1631a8b605a43ff5bed8086072ba1e7cc2358baeca134c825a7" },
	{ "abcd", "1165b3406ff0b52a3d24721f785462ca2276c9f454a116c2b2ba20171a7905ea5a026682eb659c4d5f115c363aa3c79b" },
	{ "abcde", "4c525cbeac729eaf4b4665815bc5db0c84fe6300068a727cf74e2813521565abc0ec57a37ee4d8be89d097c0d2ad52f0" },
	{ "abcdef", "c6a4c65b227e7387b9c3e839d44869c4cfca3ef583dea64117859b808c1e3d8ae689e1e314eeef52a6ffe22681aa11f5" },
	{ "abcdefg", "9f11fc131123f844c1226f429b6a0a6af0525d9f40f056c7fc16cdf1b06bda08e302554417a59fa7dcf6247421959d22" },
	{ "abcdefgh", "9000cd7cada59d1d2eb82912f7f24e5e69cc5517f68283b005fa27c285b61e05edf1ad1a8a9bded6fd29eb87d75ad806" },
	{ "abcdefghi", "ef54915b60cf062b8dd0c29ae3cad69abe6310de63ac081f46ef019c5c90897caefd79b796cfa81139788a260ded52df" },
	{ "abcdefghij", "a12070030a02d86b0ddacd0d3a5b598344513d0a051e7355053e556a0055489c1555399b03342845c4adde2dc44ff66c" },
	{ "abcdefghijk", "2440d0e751fe5b8b1aba067e20be00b9deecc5e218b0b4b37202de824bcd04294d67c8d0b73e393afa844fa9ca25fa51" },
	{ "abcdefghijkl", "103ca96c06a1ce798f08f8eff0dfb0ccdb567d48b285b23d0cd773454667a3c2fa5f1b58d9cdf2329bd9979730bfaaff" },
	{ "abcdefghijklm", "89a7179df195462f047393c36e4843183eb38404bdfbacfd0b0f9c2556632a2799f19c3ecf48bdb7c9bdf95d3f6c3704" },
	{ "abcdefghijklmn", "3bc463b0a5614d39fd207cbfd108534bce68d5438235c6c577b34b70fe219954adceaf8808d1fad4a44fc9c420ea8ff1" },
	{ "abcdefghijklmno", "5149860ee76dd6666308189e60090d615e36ce0c0ef753a610cca0524a022900489d70167a47cc74c4dd9f9f340066af" },
	{ "abcdefghijklmnop", "96d3c1b54b1938600abe5b57232e185df1c5856f74656b8f9837c5317cf5b22ac38226fafc8c946b9d20aca1b0c53a98" },
	{ "abcdefghijklmnopq", "dae0d8c29d8f1137df3afb8f502dc474d3bbb56de0c10fc219547826f23f38f37ec29e4ed203908e6e7955c83a138129" },
	{ "abcdefghijklmnopqr", "5cfa62716d985d3b1efab0ed3460e7b7f6af9439ae8ee5c58b20e68607eeec3e8c6df8481f5f36e726eaa56512acea6e" },
	{ "abcdefghijklmnopqrs", "c5d404fc93b0e59ecb5f40446da201876faf18a0af46e577ae2f7a4fe56dc4c419afff7edec90ff3de160d0c5e7a5ec1" },
	{ "abcdefghijklmnopqrst", "bc1511cd8b813544cb60b13d1ceb63e81f46aa3ca114a23fc5c3aba54f9965cdf9afa68e2dc2a680934e429dff5aa7f2" },
	{ "abcdefghijklmnopqrstu", "8f18622d37e0aceabba191e3836b30e8970aca202ce6e811f586ec5f950edb7bf799cc88a18468a3effb063397242d95" },
	{ "abcdefghijklmnopqrstuv", "c8a4f46e609626543ce6c1362721fcbe95c8e7405aaee61da4f2da1740f0351172c98a66530f8607bf8609e387ff8456" },
	{ "abcdefghijklmnopqrstuvw", "2daff33b3bd67de61550070696b431d54b1397b40d053912b07a94260812185907726e3efbe9ae9fc078659cd2ce36db" },
	{ "abcdefghijklmnopqrstuvwq", "87dc70a2eaa0dd0b687f91f26383866161026e41bb310a9e32b7a17c99284db85b9743476a30caeedf3fbb3c8072bc5e" },
};

static const struct testvector test512[] = {
	{ "hello, world", "8710339dcb6814d0d9d2290ef422285c9322b7163951f9a0ca8f883d3305286f44139aa374848e4174f5aada663027e4548637b6d19894aec4fb6c46a139fbf9" },
	{ "", "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e" },
	{ "a", "1f40fc92da241694750979ee6cf582f2d5d7d28e18335de05abc54d0560e0f5302860c652bf08d560252aa5e74210546f369fbbbce8c12cfc7957b2652fe9a75" },
	{ "ab", "2d408a0717ec188158278a796c689044361dc6fdde28d6f04973b80896e1823975cdbf12eb63f9e0591328ee235d80e9b5bf1aa6a44f4617ff3caf6400eb172d" },
	{ "abc", "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f" },
	{ "abcd", "d8022f2060ad6efd297ab73dcc5355c9b214054b0d1776a136a669d26a7d3b14f73aa0d0ebff19ee333368f0164b6419a96da49e3e481753e7e96b716bdccb6f" },
	{ "abcde", "878ae65a92e86cac011a570d4c30a7eaec442b85ce8eca0c2952b5e3cc0628c2e79d889ad4d5c7c626986d452dd86374b6ffaa7cd8b67665bef2289a5c70b0a1" },
	{ "abcdef", "e32ef19623e8ed9d267f657a81944b3d07adbb768518068e88435745564e8d4150a0a703be2a7d88b61e3d390c2bb97e2d4c311fdc69d6b1267f05f59aa920e7" },
	{ "abcdefg", "d716a4188569b68ab1b6dfac178e570114cdf0ea3a1cc0e31486c3e41241bc6a76424e8c37ab26f096fc85ef9886c8cb634187f4fddff645fb099f1ff54c6b8c" },
	{ "abcdefgh", "a3a8c81bc97c2560010d7389bc88aac974a104e0e2381220c6e084c4dccd1d2d17d4f86db31c2a851dc80e6681d74733c55dcd03dd96f6062cdda12a291ae6ce" },
	{ "abcdefghi", "f22d51d25292ca1d0f68f69aedc7897019308cc9db46efb75a03dd494fc7f126c010e8ade6a00a0c1a5f1b75d81e0ed5a93ce98dc9b833db7839247b1d9c24fe" },
	{ "abcdefghij", "ef6b97321f34b1fea2169a7db9e1960b471aa13302a988087357c520be957ca119c3ba68e6b4982c019ec89de3865ccf6a3cda1fe11e59f98d99f1502c8b9745" },
	{ "abcdefghijk", "2798fd001ee8800e3da09ee99ae9600de2d0ccf464ab782c92fcc06ce3847cef0743365f1d49c2c8b4426db1635433f937d508672a9d0cb673b84f368eca1b23" },
	{ "abcdefghijkl", "17807c728ee3ba35e7cf7af823116d26e41e5d4d6c2ff1f3720d3d96aacb6f69de642e63d5b73fc396c12be38b2bd5d884257c32c8f6d0854ae6b540f86dda2e" },
	{ "abcdefghijklm", "e11a66f7b9a2acda5663e9434377137d73ea560a32782230412642a463c8558123bfb7c0dbf17851e9aa58cc9587c3b4f5e3f7f38dcb6f890702e5bed4d5b54a" },
	{ "abcdefghijklmn", "8334134081070bf7fcc8bf1c242d24bb3182a5119e5fb19d8bbf6b9d0cdb7fed5336e83415fce93094c0e55123cf69e14d7ae41b22289232699824e31125b6d9" },
	{ "abcdefghijklmno", "db723f341a042d8de1aa813efd5e02fc1745ccbe259486257514804e2ec4bcebb2a46f1e4ad442154943f9e97e1bc47c3ae0eddab7de0c01a9c51f15342a5b19" },
	{ "abcdefghijklmnop", "d0cadd6834fa0c157b36cca30ee8b0b1435d841aa5b5ac850c11ae80a1440f51743e98fb1f1e7376c70f2f65404f088c28bcb4a511df2e64111f8f7424364b60" },
	{ "abcdefghijklmnopq", "6196942a8495b721f82bbc385c74c1f10eeadf35db8adc9cf1a05ddeed19351228279644cd5d686ee48a31631ebb64747a2b68b733dd6015e3d27750878fa875" },
	{ "abcdefghijklmnopqr", "fb3bd1fc157ea6f7a6728986a59b271b766fb723f6b7cf2b4194437435f2c497f33b6a56ae7eb3830fa9e04d5ebb4cb5e3f4d4bd812c498bdf0167e125de3fba" },
	{ "abcdefghijklmnopqrs", "836f9ecf2aa02f522a94f1370af45a9fd538ac3c70e3b709d614b2f8981881d6b0070fc6387b74ee371fc2549309f82926e78084b401deb61a106c399089bee8" },
	{ "abcdefghijklmnopqrst", "8cd9c137651425fb32d193d99b281735ec68eb5fd296f16459d1b33eac7badcfce0dca22eadaa5f209fa4ac3bbecd41342bac8b8a5dc3626e7f22cdc96e17cb4" },
	{ "abcdefghijklmnopqrstu", "7079853a3e36241a8d83639f168ef38e883d7f72851a84ef3ed4d91c6a3896cf542b8b4518c2816fb19d4692a4b9aae65cb857e3642ce0a3936e20363bcbd4ca" },
	{ "abcdefghijklmnopqrstuv", "a4e8a90b7058fb078e6cdcfd0c6a33c366437eb9084eac657830356804c9f9b53f121496d8e972d8707a4cf02615e6f58ed1a770c28ac79ffd845401fe18a928" },
	{ "abcdefghijklmnopqrstuvw", "d91b1fd7c7785975493826719f333d090b214ff42351c84d8f8b2538509a28d2d59a36d0ac798d99d3908083b072a4be606ae391def5daa74156350fec71dd24" },
	{ "abcdefghijklmnopqrstuvwq", "404eb5652173323320cac6bf8d9714aef0747693a8ab4570700c6262268d367f30e31c44fa66860568ff058fe39c9aa8dac76bc78566c691a884cb9052c4aa0a" },
};

static void
digest2string(const uint8_t *digest, char *string, size_t len)
{
	while (len--) {
		if (*digest / 16 < 10)
			*string++ = '0' + *digest / 16;
		else
			*string++ = 'a' + *digest / 16 - 10;
		if (*digest % 16 < 10)
			*string++ = '0' + *digest % 16;
		else
			*string++ = 'a' + *digest % 16 - 10;
		++digest;
	}
	*string = '\0';
}

ATF_TC_HEAD(t_sha256, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test SHA256 functions for consistent results");
}

ATF_TC_BODY(t_sha256, tc)
{
	size_t i, j, len;
	SHA256_CTX ctx;
	unsigned char buf[256];
	unsigned char digest[8 + SHA256_DIGEST_LENGTH];
	char output[SHA256_DIGEST_STRING_LENGTH];

	for (i = 0; i < sizeof(test256) / sizeof(test256[0]); ++i) {
		len = strlen(test256[i].vector);
		for (j = 0; j < 8; ++j) {
			SHA256_Init(&ctx);
			memcpy(buf + j, test256[i].vector, len);
			SHA256_Update(&ctx, buf + j, len);
			SHA256_Final(digest + j, &ctx);
			digest2string(digest + j, output, SHA256_DIGEST_LENGTH);
			ATF_CHECK_STREQ(test256[i].hash, output);
		}
	}
}

ATF_TC_HEAD(t_sha384, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test SHA384 functions for consistent results");
}

ATF_TC_BODY(t_sha384, tc)
{
	size_t i, j, len;
	SHA384_CTX ctx;
	unsigned char buf[384];
	unsigned char digest[8 + SHA384_DIGEST_LENGTH];
	char output[SHA384_DIGEST_STRING_LENGTH];

	for (i = 0; i < sizeof(test384) / sizeof(test384[0]); ++i) {
		len = strlen(test384[i].vector);
		for (j = 0; j < 8; ++j) {
			SHA384_Init(&ctx);
			memcpy(buf + j, test384[i].vector, len);
			SHA384_Update(&ctx, buf + j, len);
			SHA384_Final(digest + j, &ctx);
			digest2string(digest + j, output, SHA384_DIGEST_LENGTH);
			ATF_CHECK_STREQ(test384[i].hash, output);
		}
	}
}

ATF_TC_HEAD(t_sha512, tc)
{
	atf_tc_set_md_var(tc, "descr",
	    "Test SHA512 functions for consistent results");
}

ATF_TC_BODY(t_sha512, tc)
{
	size_t i, j, len;
	SHA512_CTX ctx;
	unsigned char buf[512];
	unsigned char digest[8 + SHA512_DIGEST_LENGTH];
	char output[SHA512_DIGEST_STRING_LENGTH];

	for (i = 0; i < sizeof(test512) / sizeof(test512[0]); ++i) {
		len = strlen(test512[i].vector);
		for (j = 0; j < 8; ++j) {
			SHA512_Init(&ctx);
			memcpy(buf + j, test512[i].vector, len);
			SHA512_Update(&ctx, buf + j, len);
			SHA512_Final(digest + j, &ctx);
			digest2string(digest + j, output, SHA512_DIGEST_LENGTH);
			ATF_CHECK_STREQ(test512[i].hash, output);
		}
	}
}
