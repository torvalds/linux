// $Id: strcop.h,v 1.3 2003/10/22 13:27:12 henriken Exp $

// Streamcop meta-data configuration structs

struct strcop_meta_out {
	unsigned char  csumsel  : 3;
	unsigned char  ciphsel  : 3;
	unsigned char  ciphconf : 2;
	unsigned char  hashsel  : 3;
	unsigned char  hashconf : 1;
	unsigned char  hashmode : 1;
	unsigned char  decrypt  : 1;
	unsigned char  dlkey    : 1;
	unsigned char  cbcmode  : 1;
};

struct strcop_meta_in {
	unsigned char  dmasel     : 3;
	unsigned char  sync       : 1;
	unsigned char  res1       : 5;
	unsigned char  res2;
};

// Source definitions

enum {
	src_none = 0,
	src_dma  = 1,
	src_des  = 2,
	src_sha1 = 3,
	src_csum = 4,
	src_aes  = 5,
	src_md5  = 6,
	src_res  = 7
};

// Cipher definitions

enum {
	ciph_des = 0,
	ciph_3des = 1,
	ciph_aes = 2
};

// Hash definitions

enum {
	hash_sha1 = 0,
	hash_md5 = 1
};

enum {
	hash_noiv = 0,
	hash_iv = 1
};


