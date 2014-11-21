/*
 * Cryptographic API.
 *
 * Tiger hashing Algorithm
 *
 *      Copyright (C) 1998 Free Software Foundation, Inc.
 *
 * The Tiger algorithm was developed by Ross Anderson and Eli Biham.
 * It was optimized for 64-bit processors while still delievering
 * decent performance on 32 and 16-bit processors.
 *
 * This version is derived from the GnuPG implementation and the
 * Tiger-Perl interface written by Rafael Sevilla
 *
 * Adapted for Linux Kernel Crypto  by Aaron Grothe 
 * ajgrothe@yahoo.com, February 22, 2005
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */
#include <crypto/internal/hash.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/mm.h>
#include <asm/byteorder.h>
#include <linux/types.h>

#define TGR192_DIGEST_SIZE 24
#define TGR160_DIGEST_SIZE 20
#define TGR128_DIGEST_SIZE 16

#define TGR192_BLOCK_SIZE  64

struct tgr192_ctx {
	u64 a, b, c;
	u8 hash[64];
	int count;
	u32 nblocks;
};

static const u64 sbox1[256] = {
	0x02aab17cf7e90c5eULL, 0xac424b03e243a8ecULL, 0x72cd5be30dd5fcd3ULL,
	0x6d019b93f6f97f3aULL, 0xcd9978ffd21f9193ULL, 0x7573a1c9708029e2ULL,
	0xb164326b922a83c3ULL, 0x46883eee04915870ULL, 0xeaace3057103ece6ULL,
	0xc54169b808a3535cULL, 0x4ce754918ddec47cULL, 0x0aa2f4dfdc0df40cULL,
	0x10b76f18a74dbefaULL, 0xc6ccb6235ad1ab6aULL, 0x13726121572fe2ffULL,
	0x1a488c6f199d921eULL, 0x4bc9f9f4da0007caULL, 0x26f5e6f6e85241c7ULL,
	0x859079dbea5947b6ULL, 0x4f1885c5c99e8c92ULL, 0xd78e761ea96f864bULL,
	0x8e36428c52b5c17dULL, 0x69cf6827373063c1ULL, 0xb607c93d9bb4c56eULL,
	0x7d820e760e76b5eaULL, 0x645c9cc6f07fdc42ULL, 0xbf38a078243342e0ULL,
	0x5f6b343c9d2e7d04ULL, 0xf2c28aeb600b0ec6ULL, 0x6c0ed85f7254bcacULL,
	0x71592281a4db4fe5ULL, 0x1967fa69ce0fed9fULL, 0xfd5293f8b96545dbULL,
	0xc879e9d7f2a7600bULL, 0x860248920193194eULL, 0xa4f9533b2d9cc0b3ULL,
	0x9053836c15957613ULL, 0xdb6dcf8afc357bf1ULL, 0x18beea7a7a370f57ULL,
	0x037117ca50b99066ULL, 0x6ab30a9774424a35ULL, 0xf4e92f02e325249bULL,
	0x7739db07061ccae1ULL, 0xd8f3b49ceca42a05ULL, 0xbd56be3f51382f73ULL,
	0x45faed5843b0bb28ULL, 0x1c813d5c11bf1f83ULL, 0x8af0e4b6d75fa169ULL,
	0x33ee18a487ad9999ULL, 0x3c26e8eab1c94410ULL, 0xb510102bc0a822f9ULL,
	0x141eef310ce6123bULL, 0xfc65b90059ddb154ULL, 0xe0158640c5e0e607ULL,
	0x884e079826c3a3cfULL, 0x930d0d9523c535fdULL, 0x35638d754e9a2b00ULL,
	0x4085fccf40469dd5ULL, 0xc4b17ad28be23a4cULL, 0xcab2f0fc6a3e6a2eULL,
	0x2860971a6b943fcdULL, 0x3dde6ee212e30446ULL, 0x6222f32ae01765aeULL,
	0x5d550bb5478308feULL, 0xa9efa98da0eda22aULL, 0xc351a71686c40da7ULL,
	0x1105586d9c867c84ULL, 0xdcffee85fda22853ULL, 0xccfbd0262c5eef76ULL,
	0xbaf294cb8990d201ULL, 0xe69464f52afad975ULL, 0x94b013afdf133e14ULL,
	0x06a7d1a32823c958ULL, 0x6f95fe5130f61119ULL, 0xd92ab34e462c06c0ULL,
	0xed7bde33887c71d2ULL, 0x79746d6e6518393eULL, 0x5ba419385d713329ULL,
	0x7c1ba6b948a97564ULL, 0x31987c197bfdac67ULL, 0xde6c23c44b053d02ULL,
	0x581c49fed002d64dULL, 0xdd474d6338261571ULL, 0xaa4546c3e473d062ULL,
	0x928fce349455f860ULL, 0x48161bbacaab94d9ULL, 0x63912430770e6f68ULL,
	0x6ec8a5e602c6641cULL, 0x87282515337ddd2bULL, 0x2cda6b42034b701bULL,
	0xb03d37c181cb096dULL, 0xe108438266c71c6fULL, 0x2b3180c7eb51b255ULL,
	0xdf92b82f96c08bbcULL, 0x5c68c8c0a632f3baULL, 0x5504cc861c3d0556ULL,
	0xabbfa4e55fb26b8fULL, 0x41848b0ab3baceb4ULL, 0xb334a273aa445d32ULL,
	0xbca696f0a85ad881ULL, 0x24f6ec65b528d56cULL, 0x0ce1512e90f4524aULL,
	0x4e9dd79d5506d35aULL, 0x258905fac6ce9779ULL, 0x2019295b3e109b33ULL,
	0xf8a9478b73a054ccULL, 0x2924f2f934417eb0ULL, 0x3993357d536d1bc4ULL,
	0x38a81ac21db6ff8bULL, 0x47c4fbf17d6016bfULL, 0x1e0faadd7667e3f5ULL,
	0x7abcff62938beb96ULL, 0xa78dad948fc179c9ULL, 0x8f1f98b72911e50dULL,
	0x61e48eae27121a91ULL, 0x4d62f7ad31859808ULL, 0xeceba345ef5ceaebULL,
	0xf5ceb25ebc9684ceULL, 0xf633e20cb7f76221ULL, 0xa32cdf06ab8293e4ULL,
	0x985a202ca5ee2ca4ULL, 0xcf0b8447cc8a8fb1ULL, 0x9f765244979859a3ULL,
	0xa8d516b1a1240017ULL, 0x0bd7ba3ebb5dc726ULL, 0xe54bca55b86adb39ULL,
	0x1d7a3afd6c478063ULL, 0x519ec608e7669eddULL, 0x0e5715a2d149aa23ULL,
	0x177d4571848ff194ULL, 0xeeb55f3241014c22ULL, 0x0f5e5ca13a6e2ec2ULL,
	0x8029927b75f5c361ULL, 0xad139fabc3d6e436ULL, 0x0d5df1a94ccf402fULL,
	0x3e8bd948bea5dfc8ULL, 0xa5a0d357bd3ff77eULL, 0xa2d12e251f74f645ULL,
	0x66fd9e525e81a082ULL, 0x2e0c90ce7f687a49ULL, 0xc2e8bcbeba973bc5ULL,
	0x000001bce509745fULL, 0x423777bbe6dab3d6ULL, 0xd1661c7eaef06eb5ULL,
	0xa1781f354daacfd8ULL, 0x2d11284a2b16affcULL, 0xf1fc4f67fa891d1fULL,
	0x73ecc25dcb920adaULL, 0xae610c22c2a12651ULL, 0x96e0a810d356b78aULL,
	0x5a9a381f2fe7870fULL, 0xd5ad62ede94e5530ULL, 0xd225e5e8368d1427ULL,
	0x65977b70c7af4631ULL, 0x99f889b2de39d74fULL, 0x233f30bf54e1d143ULL,
	0x9a9675d3d9a63c97ULL, 0x5470554ff334f9a8ULL, 0x166acb744a4f5688ULL,
	0x70c74caab2e4aeadULL, 0xf0d091646f294d12ULL, 0x57b82a89684031d1ULL,
	0xefd95a5a61be0b6bULL, 0x2fbd12e969f2f29aULL, 0x9bd37013feff9fe8ULL,
	0x3f9b0404d6085a06ULL, 0x4940c1f3166cfe15ULL, 0x09542c4dcdf3defbULL,
	0xb4c5218385cd5ce3ULL, 0xc935b7dc4462a641ULL, 0x3417f8a68ed3b63fULL,
	0xb80959295b215b40ULL, 0xf99cdaef3b8c8572ULL, 0x018c0614f8fcb95dULL,
	0x1b14accd1a3acdf3ULL, 0x84d471f200bb732dULL, 0xc1a3110e95e8da16ULL,
	0x430a7220bf1a82b8ULL, 0xb77e090d39df210eULL, 0x5ef4bd9f3cd05e9dULL,
	0x9d4ff6da7e57a444ULL, 0xda1d60e183d4a5f8ULL, 0xb287c38417998e47ULL,
	0xfe3edc121bb31886ULL, 0xc7fe3ccc980ccbefULL, 0xe46fb590189bfd03ULL,
	0x3732fd469a4c57dcULL, 0x7ef700a07cf1ad65ULL, 0x59c64468a31d8859ULL,
	0x762fb0b4d45b61f6ULL, 0x155baed099047718ULL, 0x68755e4c3d50baa6ULL,
	0xe9214e7f22d8b4dfULL, 0x2addbf532eac95f4ULL, 0x32ae3909b4bd0109ULL,
	0x834df537b08e3450ULL, 0xfa209da84220728dULL, 0x9e691d9b9efe23f7ULL,
	0x0446d288c4ae8d7fULL, 0x7b4cc524e169785bULL, 0x21d87f0135ca1385ULL,
	0xcebb400f137b8aa5ULL, 0x272e2b66580796beULL, 0x3612264125c2b0deULL,
	0x057702bdad1efbb2ULL, 0xd4babb8eacf84be9ULL, 0x91583139641bc67bULL,
	0x8bdc2de08036e024ULL, 0x603c8156f49f68edULL, 0xf7d236f7dbef5111ULL,
	0x9727c4598ad21e80ULL, 0xa08a0896670a5fd7ULL, 0xcb4a8f4309eba9cbULL,
	0x81af564b0f7036a1ULL, 0xc0b99aa778199abdULL, 0x959f1ec83fc8e952ULL,
	0x8c505077794a81b9ULL, 0x3acaaf8f056338f0ULL, 0x07b43f50627a6778ULL,
	0x4a44ab49f5eccc77ULL, 0x3bc3d6e4b679ee98ULL, 0x9cc0d4d1cf14108cULL,
	0x4406c00b206bc8a0ULL, 0x82a18854c8d72d89ULL, 0x67e366b35c3c432cULL,
	0xb923dd61102b37f2ULL, 0x56ab2779d884271dULL, 0xbe83e1b0ff1525afULL,
	0xfb7c65d4217e49a9ULL, 0x6bdbe0e76d48e7d4ULL, 0x08df828745d9179eULL,
	0x22ea6a9add53bd34ULL, 0xe36e141c5622200aULL, 0x7f805d1b8cb750eeULL,
	0xafe5c7a59f58e837ULL, 0xe27f996a4fb1c23cULL, 0xd3867dfb0775f0d0ULL,
	0xd0e673de6e88891aULL, 0x123aeb9eafb86c25ULL, 0x30f1d5d5c145b895ULL,
	0xbb434a2dee7269e7ULL, 0x78cb67ecf931fa38ULL, 0xf33b0372323bbf9cULL,
	0x52d66336fb279c74ULL, 0x505f33ac0afb4eaaULL, 0xe8a5cd99a2cce187ULL,
	0x534974801e2d30bbULL, 0x8d2d5711d5876d90ULL, 0x1f1a412891bc038eULL,
	0xd6e2e71d82e56648ULL, 0x74036c3a497732b7ULL, 0x89b67ed96361f5abULL,
	0xffed95d8f1ea02a2ULL, 0xe72b3bd61464d43dULL, 0xa6300f170bdc4820ULL,
	0xebc18760ed78a77aULL
};

static const u64 sbox2[256] = {
	0xe6a6be5a05a12138ULL, 0xb5a122a5b4f87c98ULL, 0x563c6089140b6990ULL,
	0x4c46cb2e391f5dd5ULL, 0xd932addbc9b79434ULL, 0x08ea70e42015aff5ULL,
	0xd765a6673e478cf1ULL, 0xc4fb757eab278d99ULL, 0xdf11c6862d6e0692ULL,
	0xddeb84f10d7f3b16ULL, 0x6f2ef604a665ea04ULL, 0x4a8e0f0ff0e0dfb3ULL,
	0xa5edeef83dbcba51ULL, 0xfc4f0a2a0ea4371eULL, 0xe83e1da85cb38429ULL,
	0xdc8ff882ba1b1ce2ULL, 0xcd45505e8353e80dULL, 0x18d19a00d4db0717ULL,
	0x34a0cfeda5f38101ULL, 0x0be77e518887caf2ULL, 0x1e341438b3c45136ULL,
	0xe05797f49089ccf9ULL, 0xffd23f9df2591d14ULL, 0x543dda228595c5cdULL,
	0x661f81fd99052a33ULL, 0x8736e641db0f7b76ULL, 0x15227725418e5307ULL,
	0xe25f7f46162eb2faULL, 0x48a8b2126c13d9feULL, 0xafdc541792e76eeaULL,
	0x03d912bfc6d1898fULL, 0x31b1aafa1b83f51bULL, 0xf1ac2796e42ab7d9ULL,
	0x40a3a7d7fcd2ebacULL, 0x1056136d0afbbcc5ULL, 0x7889e1dd9a6d0c85ULL,
	0xd33525782a7974aaULL, 0xa7e25d09078ac09bULL, 0xbd4138b3eac6edd0ULL,
	0x920abfbe71eb9e70ULL, 0xa2a5d0f54fc2625cULL, 0xc054e36b0b1290a3ULL,
	0xf6dd59ff62fe932bULL, 0x3537354511a8ac7dULL, 0xca845e9172fadcd4ULL,
	0x84f82b60329d20dcULL, 0x79c62ce1cd672f18ULL, 0x8b09a2add124642cULL,
	0xd0c1e96a19d9e726ULL, 0x5a786a9b4ba9500cULL, 0x0e020336634c43f3ULL,
	0xc17b474aeb66d822ULL, 0x6a731ae3ec9baac2ULL, 0x8226667ae0840258ULL,
	0x67d4567691caeca5ULL, 0x1d94155c4875adb5ULL, 0x6d00fd985b813fdfULL,
	0x51286efcb774cd06ULL, 0x5e8834471fa744afULL, 0xf72ca0aee761ae2eULL,
	0xbe40e4cdaee8e09aULL, 0xe9970bbb5118f665ULL, 0x726e4beb33df1964ULL,
	0x703b000729199762ULL, 0x4631d816f5ef30a7ULL, 0xb880b5b51504a6beULL,
	0x641793c37ed84b6cULL, 0x7b21ed77f6e97d96ULL, 0x776306312ef96b73ULL,
	0xae528948e86ff3f4ULL, 0x53dbd7f286a3f8f8ULL, 0x16cadce74cfc1063ULL,
	0x005c19bdfa52c6ddULL, 0x68868f5d64d46ad3ULL, 0x3a9d512ccf1e186aULL,
	0x367e62c2385660aeULL, 0xe359e7ea77dcb1d7ULL, 0x526c0773749abe6eULL,
	0x735ae5f9d09f734bULL, 0x493fc7cc8a558ba8ULL, 0xb0b9c1533041ab45ULL,
	0x321958ba470a59bdULL, 0x852db00b5f46c393ULL, 0x91209b2bd336b0e5ULL,
	0x6e604f7d659ef19fULL, 0xb99a8ae2782ccb24ULL, 0xccf52ab6c814c4c7ULL,
	0x4727d9afbe11727bULL, 0x7e950d0c0121b34dULL, 0x756f435670ad471fULL,
	0xf5add442615a6849ULL, 0x4e87e09980b9957aULL, 0x2acfa1df50aee355ULL,
	0xd898263afd2fd556ULL, 0xc8f4924dd80c8fd6ULL, 0xcf99ca3d754a173aULL,
	0xfe477bacaf91bf3cULL, 0xed5371f6d690c12dULL, 0x831a5c285e687094ULL,
	0xc5d3c90a3708a0a4ULL, 0x0f7f903717d06580ULL, 0x19f9bb13b8fdf27fULL,
	0xb1bd6f1b4d502843ULL, 0x1c761ba38fff4012ULL, 0x0d1530c4e2e21f3bULL,
	0x8943ce69a7372c8aULL, 0xe5184e11feb5ce66ULL, 0x618bdb80bd736621ULL,
	0x7d29bad68b574d0bULL, 0x81bb613e25e6fe5bULL, 0x071c9c10bc07913fULL,
	0xc7beeb7909ac2d97ULL, 0xc3e58d353bc5d757ULL, 0xeb017892f38f61e8ULL,
	0xd4effb9c9b1cc21aULL, 0x99727d26f494f7abULL, 0xa3e063a2956b3e03ULL,
	0x9d4a8b9a4aa09c30ULL, 0x3f6ab7d500090fb4ULL, 0x9cc0f2a057268ac0ULL,
	0x3dee9d2dedbf42d1ULL, 0x330f49c87960a972ULL, 0xc6b2720287421b41ULL,
	0x0ac59ec07c00369cULL, 0xef4eac49cb353425ULL, 0xf450244eef0129d8ULL,
	0x8acc46e5caf4deb6ULL, 0x2ffeab63989263f7ULL, 0x8f7cb9fe5d7a4578ULL,
	0x5bd8f7644e634635ULL, 0x427a7315bf2dc900ULL, 0x17d0c4aa2125261cULL,
	0x3992486c93518e50ULL, 0xb4cbfee0a2d7d4c3ULL, 0x7c75d6202c5ddd8dULL,
	0xdbc295d8e35b6c61ULL, 0x60b369d302032b19ULL, 0xce42685fdce44132ULL,
	0x06f3ddb9ddf65610ULL, 0x8ea4d21db5e148f0ULL, 0x20b0fce62fcd496fULL,
	0x2c1b912358b0ee31ULL, 0xb28317b818f5a308ULL, 0xa89c1e189ca6d2cfULL,
	0x0c6b18576aaadbc8ULL, 0xb65deaa91299fae3ULL, 0xfb2b794b7f1027e7ULL,
	0x04e4317f443b5bebULL, 0x4b852d325939d0a6ULL, 0xd5ae6beefb207ffcULL,
	0x309682b281c7d374ULL, 0xbae309a194c3b475ULL, 0x8cc3f97b13b49f05ULL,
	0x98a9422ff8293967ULL, 0x244b16b01076ff7cULL, 0xf8bf571c663d67eeULL,
	0x1f0d6758eee30da1ULL, 0xc9b611d97adeb9b7ULL, 0xb7afd5887b6c57a2ULL,
	0x6290ae846b984fe1ULL, 0x94df4cdeacc1a5fdULL, 0x058a5bd1c5483affULL,
	0x63166cc142ba3c37ULL, 0x8db8526eb2f76f40ULL, 0xe10880036f0d6d4eULL,
	0x9e0523c9971d311dULL, 0x45ec2824cc7cd691ULL, 0x575b8359e62382c9ULL,
	0xfa9e400dc4889995ULL, 0xd1823ecb45721568ULL, 0xdafd983b8206082fULL,
	0xaa7d29082386a8cbULL, 0x269fcd4403b87588ULL, 0x1b91f5f728bdd1e0ULL,
	0xe4669f39040201f6ULL, 0x7a1d7c218cf04adeULL, 0x65623c29d79ce5ceULL,
	0x2368449096c00bb1ULL, 0xab9bf1879da503baULL, 0xbc23ecb1a458058eULL,
	0x9a58df01bb401eccULL, 0xa070e868a85f143dULL, 0x4ff188307df2239eULL,
	0x14d565b41a641183ULL, 0xee13337452701602ULL, 0x950e3dcf3f285e09ULL,
	0x59930254b9c80953ULL, 0x3bf299408930da6dULL, 0xa955943f53691387ULL,
	0xa15edecaa9cb8784ULL, 0x29142127352be9a0ULL, 0x76f0371fff4e7afbULL,
	0x0239f450274f2228ULL, 0xbb073af01d5e868bULL, 0xbfc80571c10e96c1ULL,
	0xd267088568222e23ULL, 0x9671a3d48e80b5b0ULL, 0x55b5d38ae193bb81ULL,
	0x693ae2d0a18b04b8ULL, 0x5c48b4ecadd5335fULL, 0xfd743b194916a1caULL,
	0x2577018134be98c4ULL, 0xe77987e83c54a4adULL, 0x28e11014da33e1b9ULL,
	0x270cc59e226aa213ULL, 0x71495f756d1a5f60ULL, 0x9be853fb60afef77ULL,
	0xadc786a7f7443dbfULL, 0x0904456173b29a82ULL, 0x58bc7a66c232bd5eULL,
	0xf306558c673ac8b2ULL, 0x41f639c6b6c9772aULL, 0x216defe99fda35daULL,
	0x11640cc71c7be615ULL, 0x93c43694565c5527ULL, 0xea038e6246777839ULL,
	0xf9abf3ce5a3e2469ULL, 0x741e768d0fd312d2ULL, 0x0144b883ced652c6ULL,
	0xc20b5a5ba33f8552ULL, 0x1ae69633c3435a9dULL, 0x97a28ca4088cfdecULL,
	0x8824a43c1e96f420ULL, 0x37612fa66eeea746ULL, 0x6b4cb165f9cf0e5aULL,
	0x43aa1c06a0abfb4aULL, 0x7f4dc26ff162796bULL, 0x6cbacc8e54ed9b0fULL,
	0xa6b7ffefd2bb253eULL, 0x2e25bc95b0a29d4fULL, 0x86d6a58bdef1388cULL,
	0xded74ac576b6f054ULL, 0x8030bdbc2b45805dULL, 0x3c81af70e94d9289ULL,
	0x3eff6dda9e3100dbULL, 0xb38dc39fdfcc8847ULL, 0x123885528d17b87eULL,
	0xf2da0ed240b1b642ULL, 0x44cefadcd54bf9a9ULL, 0x1312200e433c7ee6ULL,
	0x9ffcc84f3a78c748ULL, 0xf0cd1f72248576bbULL, 0xec6974053638cfe4ULL,
	0x2ba7b67c0cec4e4cULL, 0xac2f4df3e5ce32edULL, 0xcb33d14326ea4c11ULL,
	0xa4e9044cc77e58bcULL, 0x5f513293d934fcefULL, 0x5dc9645506e55444ULL,
	0x50de418f317de40aULL, 0x388cb31a69dde259ULL, 0x2db4a83455820a86ULL,
	0x9010a91e84711ae9ULL, 0x4df7f0b7b1498371ULL, 0xd62a2eabc0977179ULL,
	0x22fac097aa8d5c0eULL
};

static const u64 sbox3[256] = {
	0xf49fcc2ff1daf39bULL, 0x487fd5c66ff29281ULL, 0xe8a30667fcdca83fULL,
	0x2c9b4be3d2fcce63ULL, 0xda3ff74b93fbbbc2ULL, 0x2fa165d2fe70ba66ULL,
	0xa103e279970e93d4ULL, 0xbecdec77b0e45e71ULL, 0xcfb41e723985e497ULL,
	0xb70aaa025ef75017ULL, 0xd42309f03840b8e0ULL, 0x8efc1ad035898579ULL,
	0x96c6920be2b2abc5ULL, 0x66af4163375a9172ULL, 0x2174abdcca7127fbULL,
	0xb33ccea64a72ff41ULL, 0xf04a4933083066a5ULL, 0x8d970acdd7289af5ULL,
	0x8f96e8e031c8c25eULL, 0xf3fec02276875d47ULL, 0xec7bf310056190ddULL,
	0xf5adb0aebb0f1491ULL, 0x9b50f8850fd58892ULL, 0x4975488358b74de8ULL,
	0xa3354ff691531c61ULL, 0x0702bbe481d2c6eeULL, 0x89fb24057deded98ULL,
	0xac3075138596e902ULL, 0x1d2d3580172772edULL, 0xeb738fc28e6bc30dULL,
	0x5854ef8f63044326ULL, 0x9e5c52325add3bbeULL, 0x90aa53cf325c4623ULL,
	0xc1d24d51349dd067ULL, 0x2051cfeea69ea624ULL, 0x13220f0a862e7e4fULL,
	0xce39399404e04864ULL, 0xd9c42ca47086fcb7ULL, 0x685ad2238a03e7ccULL,
	0x066484b2ab2ff1dbULL, 0xfe9d5d70efbf79ecULL, 0x5b13b9dd9c481854ULL,
	0x15f0d475ed1509adULL, 0x0bebcd060ec79851ULL, 0xd58c6791183ab7f8ULL,
	0xd1187c5052f3eee4ULL, 0xc95d1192e54e82ffULL, 0x86eea14cb9ac6ca2ULL,
	0x3485beb153677d5dULL, 0xdd191d781f8c492aULL, 0xf60866baa784ebf9ULL,
	0x518f643ba2d08c74ULL, 0x8852e956e1087c22ULL, 0xa768cb8dc410ae8dULL,
	0x38047726bfec8e1aULL, 0xa67738b4cd3b45aaULL, 0xad16691cec0dde19ULL,
	0xc6d4319380462e07ULL, 0xc5a5876d0ba61938ULL, 0x16b9fa1fa58fd840ULL,
	0x188ab1173ca74f18ULL, 0xabda2f98c99c021fULL, 0x3e0580ab134ae816ULL,
	0x5f3b05b773645abbULL, 0x2501a2be5575f2f6ULL, 0x1b2f74004e7e8ba9ULL,
	0x1cd7580371e8d953ULL, 0x7f6ed89562764e30ULL, 0xb15926ff596f003dULL,
	0x9f65293da8c5d6b9ULL, 0x6ecef04dd690f84cULL, 0x4782275fff33af88ULL,
	0xe41433083f820801ULL, 0xfd0dfe409a1af9b5ULL, 0x4325a3342cdb396bULL,
	0x8ae77e62b301b252ULL, 0xc36f9e9f6655615aULL, 0x85455a2d92d32c09ULL,
	0xf2c7dea949477485ULL, 0x63cfb4c133a39ebaULL, 0x83b040cc6ebc5462ULL,
	0x3b9454c8fdb326b0ULL, 0x56f56a9e87ffd78cULL, 0x2dc2940d99f42bc6ULL,
	0x98f7df096b096e2dULL, 0x19a6e01e3ad852bfULL, 0x42a99ccbdbd4b40bULL,
	0xa59998af45e9c559ULL, 0x366295e807d93186ULL, 0x6b48181bfaa1f773ULL,
	0x1fec57e2157a0a1dULL, 0x4667446af6201ad5ULL, 0xe615ebcacfb0f075ULL,
	0xb8f31f4f68290778ULL, 0x22713ed6ce22d11eULL, 0x3057c1a72ec3c93bULL,
	0xcb46acc37c3f1f2fULL, 0xdbb893fd02aaf50eULL, 0x331fd92e600b9fcfULL,
	0xa498f96148ea3ad6ULL, 0xa8d8426e8b6a83eaULL, 0xa089b274b7735cdcULL,
	0x87f6b3731e524a11ULL, 0x118808e5cbc96749ULL, 0x9906e4c7b19bd394ULL,
	0xafed7f7e9b24a20cULL, 0x6509eadeeb3644a7ULL, 0x6c1ef1d3e8ef0edeULL,
	0xb9c97d43e9798fb4ULL, 0xa2f2d784740c28a3ULL, 0x7b8496476197566fULL,
	0x7a5be3e6b65f069dULL, 0xf96330ed78be6f10ULL, 0xeee60de77a076a15ULL,
	0x2b4bee4aa08b9bd0ULL, 0x6a56a63ec7b8894eULL, 0x02121359ba34fef4ULL,
	0x4cbf99f8283703fcULL, 0x398071350caf30c8ULL, 0xd0a77a89f017687aULL,
	0xf1c1a9eb9e423569ULL, 0x8c7976282dee8199ULL, 0x5d1737a5dd1f7abdULL,
	0x4f53433c09a9fa80ULL, 0xfa8b0c53df7ca1d9ULL, 0x3fd9dcbc886ccb77ULL,
	0xc040917ca91b4720ULL, 0x7dd00142f9d1dcdfULL, 0x8476fc1d4f387b58ULL,
	0x23f8e7c5f3316503ULL, 0x032a2244e7e37339ULL, 0x5c87a5d750f5a74bULL,
	0x082b4cc43698992eULL, 0xdf917becb858f63cULL, 0x3270b8fc5bf86ddaULL,
	0x10ae72bb29b5dd76ULL, 0x576ac94e7700362bULL, 0x1ad112dac61efb8fULL,
	0x691bc30ec5faa427ULL, 0xff246311cc327143ULL, 0x3142368e30e53206ULL,
	0x71380e31e02ca396ULL, 0x958d5c960aad76f1ULL, 0xf8d6f430c16da536ULL,
	0xc8ffd13f1be7e1d2ULL, 0x7578ae66004ddbe1ULL, 0x05833f01067be646ULL,
	0xbb34b5ad3bfe586dULL, 0x095f34c9a12b97f0ULL, 0x247ab64525d60ca8ULL,
	0xdcdbc6f3017477d1ULL, 0x4a2e14d4decad24dULL, 0xbdb5e6d9be0a1eebULL,
	0x2a7e70f7794301abULL, 0xdef42d8a270540fdULL, 0x01078ec0a34c22c1ULL,
	0xe5de511af4c16387ULL, 0x7ebb3a52bd9a330aULL, 0x77697857aa7d6435ULL,
	0x004e831603ae4c32ULL, 0xe7a21020ad78e312ULL, 0x9d41a70c6ab420f2ULL,
	0x28e06c18ea1141e6ULL, 0xd2b28cbd984f6b28ULL, 0x26b75f6c446e9d83ULL,
	0xba47568c4d418d7fULL, 0xd80badbfe6183d8eULL, 0x0e206d7f5f166044ULL,
	0xe258a43911cbca3eULL, 0x723a1746b21dc0bcULL, 0xc7caa854f5d7cdd3ULL,
	0x7cac32883d261d9cULL, 0x7690c26423ba942cULL, 0x17e55524478042b8ULL,
	0xe0be477656a2389fULL, 0x4d289b5e67ab2da0ULL, 0x44862b9c8fbbfd31ULL,
	0xb47cc8049d141365ULL, 0x822c1b362b91c793ULL, 0x4eb14655fb13dfd8ULL,
	0x1ecbba0714e2a97bULL, 0x6143459d5cde5f14ULL, 0x53a8fbf1d5f0ac89ULL,
	0x97ea04d81c5e5b00ULL, 0x622181a8d4fdb3f3ULL, 0xe9bcd341572a1208ULL,
	0x1411258643cce58aULL, 0x9144c5fea4c6e0a4ULL, 0x0d33d06565cf620fULL,
	0x54a48d489f219ca1ULL, 0xc43e5eac6d63c821ULL, 0xa9728b3a72770dafULL,
	0xd7934e7b20df87efULL, 0xe35503b61a3e86e5ULL, 0xcae321fbc819d504ULL,
	0x129a50b3ac60bfa6ULL, 0xcd5e68ea7e9fb6c3ULL, 0xb01c90199483b1c7ULL,
	0x3de93cd5c295376cULL, 0xaed52edf2ab9ad13ULL, 0x2e60f512c0a07884ULL,
	0xbc3d86a3e36210c9ULL, 0x35269d9b163951ceULL, 0x0c7d6e2ad0cdb5faULL,
	0x59e86297d87f5733ULL, 0x298ef221898db0e7ULL, 0x55000029d1a5aa7eULL,
	0x8bc08ae1b5061b45ULL, 0xc2c31c2b6c92703aULL, 0x94cc596baf25ef42ULL,
	0x0a1d73db22540456ULL, 0x04b6a0f9d9c4179aULL, 0xeffdafa2ae3d3c60ULL,
	0xf7c8075bb49496c4ULL, 0x9cc5c7141d1cd4e3ULL, 0x78bd1638218e5534ULL,
	0xb2f11568f850246aULL, 0xedfabcfa9502bc29ULL, 0x796ce5f2da23051bULL,
	0xaae128b0dc93537cULL, 0x3a493da0ee4b29aeULL, 0xb5df6b2c416895d7ULL,
	0xfcabbd25122d7f37ULL, 0x70810b58105dc4b1ULL, 0xe10fdd37f7882a90ULL,
	0x524dcab5518a3f5cULL, 0x3c9e85878451255bULL, 0x4029828119bd34e2ULL,
	0x74a05b6f5d3ceccbULL, 0xb610021542e13ecaULL, 0x0ff979d12f59e2acULL,
	0x6037da27e4f9cc50ULL, 0x5e92975a0df1847dULL, 0xd66de190d3e623feULL,
	0x5032d6b87b568048ULL, 0x9a36b7ce8235216eULL, 0x80272a7a24f64b4aULL,
	0x93efed8b8c6916f7ULL, 0x37ddbff44cce1555ULL, 0x4b95db5d4b99bd25ULL,
	0x92d3fda169812fc0ULL, 0xfb1a4a9a90660bb6ULL, 0x730c196946a4b9b2ULL,
	0x81e289aa7f49da68ULL, 0x64669a0f83b1a05fULL, 0x27b3ff7d9644f48bULL,
	0xcc6b615c8db675b3ULL, 0x674f20b9bcebbe95ULL, 0x6f31238275655982ULL,
	0x5ae488713e45cf05ULL, 0xbf619f9954c21157ULL, 0xeabac46040a8eae9ULL,
	0x454c6fe9f2c0c1cdULL, 0x419cf6496412691cULL, 0xd3dc3bef265b0f70ULL,
	0x6d0e60f5c3578a9eULL
};

static const u64 sbox4[256] = {
	0x5b0e608526323c55ULL, 0x1a46c1a9fa1b59f5ULL, 0xa9e245a17c4c8ffaULL,
	0x65ca5159db2955d7ULL, 0x05db0a76ce35afc2ULL, 0x81eac77ea9113d45ULL,
	0x528ef88ab6ac0a0dULL, 0xa09ea253597be3ffULL, 0x430ddfb3ac48cd56ULL,
	0xc4b3a67af45ce46fULL, 0x4ececfd8fbe2d05eULL, 0x3ef56f10b39935f0ULL,
	0x0b22d6829cd619c6ULL, 0x17fd460a74df2069ULL, 0x6cf8cc8e8510ed40ULL,
	0xd6c824bf3a6ecaa7ULL, 0x61243d581a817049ULL, 0x048bacb6bbc163a2ULL,
	0xd9a38ac27d44cc32ULL, 0x7fddff5baaf410abULL, 0xad6d495aa804824bULL,
	0xe1a6a74f2d8c9f94ULL, 0xd4f7851235dee8e3ULL, 0xfd4b7f886540d893ULL,
	0x247c20042aa4bfdaULL, 0x096ea1c517d1327cULL, 0xd56966b4361a6685ULL,
	0x277da5c31221057dULL, 0x94d59893a43acff7ULL, 0x64f0c51ccdc02281ULL,
	0x3d33bcc4ff6189dbULL, 0xe005cb184ce66af1ULL, 0xff5ccd1d1db99beaULL,
	0xb0b854a7fe42980fULL, 0x7bd46a6a718d4b9fULL, 0xd10fa8cc22a5fd8cULL,
	0xd31484952be4bd31ULL, 0xc7fa975fcb243847ULL, 0x4886ed1e5846c407ULL,
	0x28cddb791eb70b04ULL, 0xc2b00be2f573417fULL, 0x5c9590452180f877ULL,
	0x7a6bddfff370eb00ULL, 0xce509e38d6d9d6a4ULL, 0xebeb0f00647fa702ULL,
	0x1dcc06cf76606f06ULL, 0xe4d9f28ba286ff0aULL, 0xd85a305dc918c262ULL,
	0x475b1d8732225f54ULL, 0x2d4fb51668ccb5feULL, 0xa679b9d9d72bba20ULL,
	0x53841c0d912d43a5ULL, 0x3b7eaa48bf12a4e8ULL, 0x781e0e47f22f1ddfULL,
	0xeff20ce60ab50973ULL, 0x20d261d19dffb742ULL, 0x16a12b03062a2e39ULL,
	0x1960eb2239650495ULL, 0x251c16fed50eb8b8ULL, 0x9ac0c330f826016eULL,
	0xed152665953e7671ULL, 0x02d63194a6369570ULL, 0x5074f08394b1c987ULL,
	0x70ba598c90b25ce1ULL, 0x794a15810b9742f6ULL, 0x0d5925e9fcaf8c6cULL,
	0x3067716cd868744eULL, 0x910ab077e8d7731bULL, 0x6a61bbdb5ac42f61ULL,
	0x93513efbf0851567ULL, 0xf494724b9e83e9d5ULL, 0xe887e1985c09648dULL,
	0x34b1d3c675370cfdULL, 0xdc35e433bc0d255dULL, 0xd0aab84234131be0ULL,
	0x08042a50b48b7eafULL, 0x9997c4ee44a3ab35ULL, 0x829a7b49201799d0ULL,
	0x263b8307b7c54441ULL, 0x752f95f4fd6a6ca6ULL, 0x927217402c08c6e5ULL,
	0x2a8ab754a795d9eeULL, 0xa442f7552f72943dULL, 0x2c31334e19781208ULL,
	0x4fa98d7ceaee6291ULL, 0x55c3862f665db309ULL, 0xbd0610175d53b1f3ULL,
	0x46fe6cb840413f27ULL, 0x3fe03792df0cfa59ULL, 0xcfe700372eb85e8fULL,
	0xa7be29e7adbce118ULL, 0xe544ee5cde8431ddULL, 0x8a781b1b41f1873eULL,
	0xa5c94c78a0d2f0e7ULL, 0x39412e2877b60728ULL, 0xa1265ef3afc9a62cULL,
	0xbcc2770c6a2506c5ULL, 0x3ab66dd5dce1ce12ULL, 0xe65499d04a675b37ULL,
	0x7d8f523481bfd216ULL, 0x0f6f64fcec15f389ULL, 0x74efbe618b5b13c8ULL,
	0xacdc82b714273e1dULL, 0xdd40bfe003199d17ULL, 0x37e99257e7e061f8ULL,
	0xfa52626904775aaaULL, 0x8bbbf63a463d56f9ULL, 0xf0013f1543a26e64ULL,
	0xa8307e9f879ec898ULL, 0xcc4c27a4150177ccULL, 0x1b432f2cca1d3348ULL,
	0xde1d1f8f9f6fa013ULL, 0x606602a047a7ddd6ULL, 0xd237ab64cc1cb2c7ULL,
	0x9b938e7225fcd1d3ULL, 0xec4e03708e0ff476ULL, 0xfeb2fbda3d03c12dULL,
	0xae0bced2ee43889aULL, 0x22cb8923ebfb4f43ULL, 0x69360d013cf7396dULL,
	0x855e3602d2d4e022ULL, 0x073805bad01f784cULL, 0x33e17a133852f546ULL,
	0xdf4874058ac7b638ULL, 0xba92b29c678aa14aULL, 0x0ce89fc76cfaadcdULL,
	0x5f9d4e0908339e34ULL, 0xf1afe9291f5923b9ULL, 0x6e3480f60f4a265fULL,
	0xeebf3a2ab29b841cULL, 0xe21938a88f91b4adULL, 0x57dfeff845c6d3c3ULL,
	0x2f006b0bf62caaf2ULL, 0x62f479ef6f75ee78ULL, 0x11a55ad41c8916a9ULL,
	0xf229d29084fed453ULL, 0x42f1c27b16b000e6ULL, 0x2b1f76749823c074ULL,
	0x4b76eca3c2745360ULL, 0x8c98f463b91691bdULL, 0x14bcc93cf1ade66aULL,
	0x8885213e6d458397ULL, 0x8e177df0274d4711ULL, 0xb49b73b5503f2951ULL,
	0x10168168c3f96b6bULL, 0x0e3d963b63cab0aeULL, 0x8dfc4b5655a1db14ULL,
	0xf789f1356e14de5cULL, 0x683e68af4e51dac1ULL, 0xc9a84f9d8d4b0fd9ULL,
	0x3691e03f52a0f9d1ULL, 0x5ed86e46e1878e80ULL, 0x3c711a0e99d07150ULL,
	0x5a0865b20c4e9310ULL, 0x56fbfc1fe4f0682eULL, 0xea8d5de3105edf9bULL,
	0x71abfdb12379187aULL, 0x2eb99de1bee77b9cULL, 0x21ecc0ea33cf4523ULL,
	0x59a4d7521805c7a1ULL, 0x3896f5eb56ae7c72ULL, 0xaa638f3db18f75dcULL,
	0x9f39358dabe9808eULL, 0xb7defa91c00b72acULL, 0x6b5541fd62492d92ULL,
	0x6dc6dee8f92e4d5bULL, 0x353f57abc4beea7eULL, 0x735769d6da5690ceULL,
	0x0a234aa642391484ULL, 0xf6f9508028f80d9dULL, 0xb8e319a27ab3f215ULL,
	0x31ad9c1151341a4dULL, 0x773c22a57bef5805ULL, 0x45c7561a07968633ULL,
	0xf913da9e249dbe36ULL, 0xda652d9b78a64c68ULL, 0x4c27a97f3bc334efULL,
	0x76621220e66b17f4ULL, 0x967743899acd7d0bULL, 0xf3ee5bcae0ed6782ULL,
	0x409f753600c879fcULL, 0x06d09a39b5926db6ULL, 0x6f83aeb0317ac588ULL,
	0x01e6ca4a86381f21ULL, 0x66ff3462d19f3025ULL, 0x72207c24ddfd3bfbULL,
	0x4af6b6d3e2ece2ebULL, 0x9c994dbec7ea08deULL, 0x49ace597b09a8bc4ULL,
	0xb38c4766cf0797baULL, 0x131b9373c57c2a75ULL, 0xb1822cce61931e58ULL,
	0x9d7555b909ba1c0cULL, 0x127fafdd937d11d2ULL, 0x29da3badc66d92e4ULL,
	0xa2c1d57154c2ecbcULL, 0x58c5134d82f6fe24ULL, 0x1c3ae3515b62274fULL,
	0xe907c82e01cb8126ULL, 0xf8ed091913e37fcbULL, 0x3249d8f9c80046c9ULL,
	0x80cf9bede388fb63ULL, 0x1881539a116cf19eULL, 0x5103f3f76bd52457ULL,
	0x15b7e6f5ae47f7a8ULL, 0xdbd7c6ded47e9ccfULL, 0x44e55c410228bb1aULL,
	0xb647d4255edb4e99ULL, 0x5d11882bb8aafc30ULL, 0xf5098bbb29d3212aULL,
	0x8fb5ea14e90296b3ULL, 0x677b942157dd025aULL, 0xfb58e7c0a390acb5ULL,
	0x89d3674c83bd4a01ULL, 0x9e2da4df4bf3b93bULL, 0xfcc41e328cab4829ULL,
	0x03f38c96ba582c52ULL, 0xcad1bdbd7fd85db2ULL, 0xbbb442c16082ae83ULL,
	0xb95fe86ba5da9ab0ULL, 0xb22e04673771a93fULL, 0x845358c9493152d8ULL,
	0xbe2a488697b4541eULL, 0x95a2dc2dd38e6966ULL, 0xc02c11ac923c852bULL,
	0x2388b1990df2a87bULL, 0x7c8008fa1b4f37beULL, 0x1f70d0c84d54e503ULL,
	0x5490adec7ece57d4ULL, 0x002b3c27d9063a3aULL, 0x7eaea3848030a2bfULL,
	0xc602326ded2003c0ULL, 0x83a7287d69a94086ULL, 0xc57a5fcb30f57a8aULL,
	0xb56844e479ebe779ULL, 0xa373b40f05dcbce9ULL, 0xd71a786e88570ee2ULL,
	0x879cbacdbde8f6a0ULL, 0x976ad1bcc164a32fULL, 0xab21e25e9666d78bULL,
	0x901063aae5e5c33cULL, 0x9818b34448698d90ULL, 0xe36487ae3e1e8abbULL,
	0xafbdf931893bdcb4ULL, 0x6345a0dc5fbbd519ULL, 0x8628fe269b9465caULL,
	0x1e5d01603f9c51ecULL, 0x4de44006a15049b7ULL, 0xbf6c70e5f776cbb1ULL,
	0x411218f2ef552bedULL, 0xcb0c0708705a36a3ULL, 0xe74d14754f986044ULL,
	0xcd56d9430ea8280eULL, 0xc12591d7535f5065ULL, 0xc83223f1720aef96ULL,
	0xc3a0396f7363a51fULL
};


static void tgr192_round(u64 * ra, u64 * rb, u64 * rc, u64 x, int mul)
{
	u64 a = *ra;
	u64 b = *rb;
	u64 c = *rc;

	c ^= x;
	a -= sbox1[c         & 0xff] ^ sbox2[(c >> 16) & 0xff]
	   ^ sbox3[(c >> 32) & 0xff] ^ sbox4[(c >> 48) & 0xff];
	b += sbox4[(c >>  8) & 0xff] ^ sbox3[(c >> 24) & 0xff]
	   ^ sbox2[(c >> 40) & 0xff] ^ sbox1[(c >> 56) & 0xff];
	b *= mul;

	*ra = a;
	*rb = b;
	*rc = c;
}


static void tgr192_pass(u64 * ra, u64 * rb, u64 * rc, u64 * x, int mul)
{
	u64 a = *ra;
	u64 b = *rb;
	u64 c = *rc;

	tgr192_round(&a, &b, &c, x[0], mul);
	tgr192_round(&b, &c, &a, x[1], mul);
	tgr192_round(&c, &a, &b, x[2], mul);
	tgr192_round(&a, &b, &c, x[3], mul);
	tgr192_round(&b, &c, &a, x[4], mul);
	tgr192_round(&c, &a, &b, x[5], mul);
	tgr192_round(&a, &b, &c, x[6], mul);
	tgr192_round(&b, &c, &a, x[7], mul);

	*ra = a;
	*rb = b;
	*rc = c;
}


static void tgr192_key_schedule(u64 * x)
{
	x[0] -= x[7] ^ 0xa5a5a5a5a5a5a5a5ULL;
	x[1] ^= x[0];
	x[2] += x[1];
	x[3] -= x[2] ^ ((~x[1]) << 19);
	x[4] ^= x[3];
	x[5] += x[4];
	x[6] -= x[5] ^ ((~x[4]) >> 23);
	x[7] ^= x[6];
	x[0] += x[7];
	x[1] -= x[0] ^ ((~x[7]) << 19);
	x[2] ^= x[1];
	x[3] += x[2];
	x[4] -= x[3] ^ ((~x[2]) >> 23);
	x[5] ^= x[4];
	x[6] += x[5];
	x[7] -= x[6] ^ 0x0123456789abcdefULL;
}


/****************
 * Transform the message DATA which consists of 512 bytes (8 words)
 */

static void tgr192_transform(struct tgr192_ctx *tctx, const u8 * data)
{
	u64 a, b, c, aa, bb, cc;
	u64 x[8];
	int i;
	const __le64 *ptr = (const __le64 *)data;

	for (i = 0; i < 8; i++)
		x[i] = le64_to_cpu(ptr[i]);

	/* save */
	a = aa = tctx->a;
	b = bb = tctx->b;
	c = cc = tctx->c;

	tgr192_pass(&a, &b, &c, x, 5);
	tgr192_key_schedule(x);
	tgr192_pass(&c, &a, &b, x, 7);
	tgr192_key_schedule(x);
	tgr192_pass(&b, &c, &a, x, 9);


	/* feedforward */
	a ^= aa;
	b -= bb;
	c += cc;
	/* store */
	tctx->a = a;
	tctx->b = b;
	tctx->c = c;
}

static int tgr192_init(struct shash_desc *desc)
{
	struct tgr192_ctx *tctx = shash_desc_ctx(desc);

	tctx->a = 0x0123456789abcdefULL;
	tctx->b = 0xfedcba9876543210ULL;
	tctx->c = 0xf096a5b4c3b2e187ULL;
	tctx->nblocks = 0;
	tctx->count = 0;

	return 0;
}


/* Update the message digest with the contents
 * of INBUF with length INLEN. */
static int tgr192_update(struct shash_desc *desc, const u8 *inbuf,
			  unsigned int len)
{
	struct tgr192_ctx *tctx = shash_desc_ctx(desc);

	if (tctx->count == 64) {	/* flush the buffer */
		tgr192_transform(tctx, tctx->hash);
		tctx->count = 0;
		tctx->nblocks++;
	}
	if (!inbuf) {
		return 0;
	}
	if (tctx->count) {
		for (; len && tctx->count < 64; len--) {
			tctx->hash[tctx->count++] = *inbuf++;
		}
		tgr192_update(desc, NULL, 0);
		if (!len) {
			return 0;
		}

	}

	while (len >= 64) {
		tgr192_transform(tctx, inbuf);
		tctx->count = 0;
		tctx->nblocks++;
		len -= 64;
		inbuf += 64;
	}
	for (; len && tctx->count < 64; len--) {
		tctx->hash[tctx->count++] = *inbuf++;
	}

	return 0;
}



/* The routine terminates the computation */
static int tgr192_final(struct shash_desc *desc, u8 * out)
{
	struct tgr192_ctx *tctx = shash_desc_ctx(desc);
	__be64 *dst = (__be64 *)out;
	__be64 *be64p;
	__le32 *le32p;
	u32 t, msb, lsb;

	tgr192_update(desc, NULL, 0); /* flush */ ;

	msb = 0;
	t = tctx->nblocks;
	if ((lsb = t << 6) < t) { /* multiply by 64 to make a byte count */
		msb++;
	}
	msb += t >> 26;
	t = lsb;
	if ((lsb = t + tctx->count) < t) {	/* add the count */
		msb++;
	}
	t = lsb;
	if ((lsb = t << 3) < t)	{ /* multiply by 8 to make a bit count */
		msb++;
	}
	msb += t >> 29;

	if (tctx->count < 56) {	/* enough room */
		tctx->hash[tctx->count++] = 0x01;	/* pad */
		while (tctx->count < 56) {
			tctx->hash[tctx->count++] = 0;	/* pad */
		}
	} else {		/* need one extra block */
		tctx->hash[tctx->count++] = 0x01;	/* pad character */
		while (tctx->count < 64) {
			tctx->hash[tctx->count++] = 0;
		}
		tgr192_update(desc, NULL, 0); /* flush */ ;
		memset(tctx->hash, 0, 56);    /* fill next block with zeroes */
	}
	/* append the 64 bit count */
	le32p = (__le32 *)&tctx->hash[56];
	le32p[0] = cpu_to_le32(lsb);
	le32p[1] = cpu_to_le32(msb);

	tgr192_transform(tctx, tctx->hash);

	be64p = (__be64 *)tctx->hash;
	dst[0] = be64p[0] = cpu_to_be64(tctx->a);
	dst[1] = be64p[1] = cpu_to_be64(tctx->b);
	dst[2] = be64p[2] = cpu_to_be64(tctx->c);

	return 0;
}

static int tgr160_final(struct shash_desc *desc, u8 * out)
{
	u8 D[64];

	tgr192_final(desc, D);
	memcpy(out, D, TGR160_DIGEST_SIZE);
	memset(D, 0, TGR192_DIGEST_SIZE);

	return 0;
}

static int tgr128_final(struct shash_desc *desc, u8 * out)
{
	u8 D[64];

	tgr192_final(desc, D);
	memcpy(out, D, TGR128_DIGEST_SIZE);
	memset(D, 0, TGR192_DIGEST_SIZE);

	return 0;
}

static struct shash_alg tgr_algs[3] = { {
	.digestsize	=	TGR192_DIGEST_SIZE,
	.init		=	tgr192_init,
	.update		=	tgr192_update,
	.final		=	tgr192_final,
	.descsize	=	sizeof(struct tgr192_ctx),
	.base		=	{
		.cra_name	=	"tgr192",
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize	=	TGR192_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
}, {
	.digestsize	=	TGR160_DIGEST_SIZE,
	.init		=	tgr192_init,
	.update		=	tgr192_update,
	.final		=	tgr160_final,
	.descsize	=	sizeof(struct tgr192_ctx),
	.base		=	{
		.cra_name	=	"tgr160",
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize	=	TGR192_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
}, {
	.digestsize	=	TGR128_DIGEST_SIZE,
	.init		=	tgr192_init,
	.update		=	tgr192_update,
	.final		=	tgr128_final,
	.descsize	=	sizeof(struct tgr192_ctx),
	.base		=	{
		.cra_name	=	"tgr128",
		.cra_flags	=	CRYPTO_ALG_TYPE_SHASH,
		.cra_blocksize	=	TGR192_BLOCK_SIZE,
		.cra_module	=	THIS_MODULE,
	}
} };

static int __init tgr192_mod_init(void)
{
	return crypto_register_shashes(tgr_algs, ARRAY_SIZE(tgr_algs));
}

static void __exit tgr192_mod_fini(void)
{
	crypto_unregister_shashes(tgr_algs, ARRAY_SIZE(tgr_algs));
}

MODULE_ALIAS_CRYPTO("tgr160");
MODULE_ALIAS_CRYPTO("tgr128");

module_init(tgr192_mod_init);
module_exit(tgr192_mod_fini);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Tiger Message Digest Algorithm");
