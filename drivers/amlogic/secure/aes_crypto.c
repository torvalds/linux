#include <linux/err.h>
#include <linux/scatterlist.h>
#include <linux/crypto.h>

#include <linux/slab.h>
#include <linux/string.h>

#ifdef CONFIG_CRYPTO_AES
/* the aes(cbc mode) algorithm in kernel is same as polarssl aes(cbc mode),
 * in kernel the aes is possessed by kernel itself, in uboot the aes is polarssl aes
 * */

//AES key table
static unsigned char default_AESkey[] = {
0xAD,0x93,0x00,0xC4,0x8E,0x50,0x20,0xC5,0x3F,0xBF,0x23,0x32,0x80,0x5A,0xC6,0xDF,
0x2F,0x7D,0x49,0xD9,0x15,0x8B,0x7F,0x04,0x2C,0x80,0xB0,0x62,0x78,0x25,0x8D,0x9C,
0x13,0x22,0x02,0x4A,0x55,0x23,0xBB,0xCB,0xF1,0xFB,0x2A,0xCC,0xBB,0x95,0xF4,0x50,
0xAE,0x08,0xD7,0xFB,0x80,0xF2,0x64,0x72,0xE3,0x3C,0xC4,0xB4,0xA3,0x50,0xD9,0xF1,
0x2A,0xDE,0xFC,0xD7,0x67,0xC8,0xDE,0xD0,0xF0,0x1E,0xE8,0x12,0xF9,0x57,0x25,0x36,
0x6D,0x71,0xD2,0xF8,0x1E,0x32,0x25,0x59,0x89,0x80,0xA3,0x59,0xD4,0xB6,0xDA,0x00,
0x8D,0xB8,0x5B,0x95,0x96,0x47,0x07,0xBD,0xED,0x68,0xDF,0xB9,0xD5,0x93,0x34,0x8F,
0xC6,0x66,0x06,0x64,0x94,0xCC,0x27,0x29,0x3A,0x8F,0x58,0x2E,0x70,0x7D,0x22,0xE7,
0x9D,0x62,0xAA,0xD1,0x0C,0xD2,0xD7,0x76,0xBD,0x40,0xCD,0x87,0x4E,0xC8,0x4C,0x80,
0x86,0xC2,0xB8,0x97,0xA3,0xDC,0x8F,0x8C,0x45,0xCC,0x26,0x40,0xBD,0xEB,0x3F,0xAF,
0x55,0x1E,0x88,0xFC,0x38,0xC0,0x06,0x1C,0xDA,0xDB,0xE4,0xFA,0x2B,0xFB,0x6D,0x6F,
0x19,0x62,0x0A,0xC4,0xEA,0xF0,0xE3,0x47,0xDB,0x47,0x83,0xE8,0x50,0x17,0xDF,0xA8,
0x29,0x37,0xB4,0x0A,0x19,0x1B,0x2D,0xDB,0x86,0xC8,0xBB,0xD1,0x52,0xD5,0x8F,0xC8,
0x2B,0xBC,0xE7,0x8A,0xF4,0xA1,0xE2,0x4D,0xAC,0xFC,0xB2,0x6F,0xDA,0x82,0xAB,0x86,
0xB7,0x95,0x6B,0xD7,0xA9,0x07,0xC7,0xB8,0x2D,0xBF,0x86,0xB4,0xBF,0xF4,0xC8,0xFD,
0x50,0x43,0xEB,0x8D,0xAB,0x16,0x91,0xBB,0x6B,0x5E,0x60,0x21,0x57,0x44,0x61,0x06,
};


/**
 * Crypto API
 */
static struct crypto_blkcipher *aml_keybox_crypto_alloc_cipher(void)
{
    return crypto_alloc_blkcipher("cbc(aes)", 0, CRYPTO_ALG_ASYNC);
}

static int aml_keybox_aes_encrypt(const void *key, int key_len,
                                  const u8 *aes_iv, void *dst, size_t *dst_len,
                                  const void *src, size_t src_len)
{
    struct scatterlist sg_in[1], sg_out[1];
    struct crypto_blkcipher *tfm = aml_keybox_crypto_alloc_cipher();
    struct blkcipher_desc desc =
        { .tfm = tfm, .flags = 0 };
	int ret;
	void *iv;
	int ivsize;
	if(src_len & 0x0f){
		printk("%s:%d,src_len %d is not 16byte align",__func__,__LINE__,src_len);
		return -1;
	}

	if (IS_ERR(tfm)){
		printk("%s:%d,crypto_alloc fail\n",__func__,__LINE__);
		return PTR_ERR(tfm);
	}

	*dst_len = src_len ;

	crypto_blkcipher_setkey((void *) tfm, key, key_len);
	sg_init_table(sg_in, 1);
	sg_set_buf(&sg_in[0], src, src_len);
	sg_init_table(sg_out, 1);
	sg_set_buf(sg_out, dst, *dst_len);
	iv = crypto_blkcipher_crt(tfm)->iv;
	ivsize = crypto_blkcipher_ivsize(tfm);

	//printk("key_len:%d,ivsize:%d\n",key_len,ivsize);

	memcpy(iv, aes_iv, ivsize);

    ret = crypto_blkcipher_encrypt(&desc, sg_out, sg_in,src_len);
    crypto_free_blkcipher(tfm);
    if (ret < 0){
        printk("%s:%d,ceph_aes_crypt failed %d\n", __func__,__LINE__,ret);
        return ret;
	}
    return 0;
}
static int aml_keybox_aes_decrypt(const void *key, int key_len,
                                      const u8 *aes_iv, void *dst,
                                      size_t *dst_len, const void *src,
                                      size_t src_len)
{
    struct scatterlist sg_in[1], sg_out[1];
    struct crypto_blkcipher *tfm = aml_keybox_crypto_alloc_cipher();
    struct blkcipher_desc desc =
        { .tfm = tfm };
    void *iv;
    int ivsize;
    int ret;
//    int last_byte;
	if(src_len &0x0f){
		printk("%s:%d,src_len %d is not 16byte align",__func__,__LINE__,src_len);
		return -1;
	}

	if (IS_ERR(tfm)){
		printk("%s:%d,crypto_alloc fail\n",__func__,__LINE__);
		return PTR_ERR(tfm);
	}

	crypto_blkcipher_setkey((void *) tfm, key, key_len);
	sg_init_table(sg_in, 1);
	sg_init_table(sg_out, 1);
	sg_set_buf(sg_in, src, src_len);
	sg_set_buf(&sg_out[0], dst, *dst_len);

	iv = crypto_blkcipher_crt(tfm)->iv;
	ivsize = crypto_blkcipher_ivsize(tfm);

	//printk("key_len:%d,ivsize:%d\n",key_len,ivsize);

	memcpy(iv, aes_iv, ivsize);

	ret = crypto_blkcipher_decrypt(&desc, sg_out, sg_in, src_len);
	crypto_free_blkcipher(tfm);
	if (ret < 0){
		printk("%s:%d,ceph_aes_decrypt failed %d\n", __func__,__LINE__,ret);
		return ret;
	}
	*dst_len = src_len;
    return 0;
}

int aes_crypto_encrypt(void *dst,size_t *dst_len,const void *src,size_t src_len)
{
	int ret;
	unsigned char iv_aes[16];
	unsigned char key_aes[32];
	memcpy(iv_aes,&default_AESkey[0],16);
	memcpy(key_aes,&default_AESkey[16],32);

	ret = aml_keybox_aes_encrypt(key_aes,sizeof(key_aes),iv_aes,dst,dst_len,src,src_len);
	return ret;
}

int aes_crypto_decrypt(void *dst,size_t *dst_len,const void *src,size_t src_len)
{
	int ret;
	unsigned char iv_aes[16];
	unsigned char key_aes[32];
	memcpy(iv_aes,&default_AESkey[0],16);
	memcpy(key_aes,&default_AESkey[16],32);

	ret = aml_keybox_aes_decrypt(key_aes,sizeof(key_aes),iv_aes,dst,dst_len,src,src_len);
	return ret;
}

//int __aml_aes_encrypt(unsigned char *output,unsigned char *input,int size)
int aml_aes_encrypt(unsigned char *output,unsigned char *input,int size)
{
	size_t out_len = size;
	return aes_crypto_encrypt(output,&out_len,input,size);
}
//int __aml_aes_decrypt(unsigned char *output,unsigned char *input,int size)
int aml_aes_decrypt(unsigned char *output,unsigned char *input,int size)
{
	size_t out_len = size;
	return aes_crypto_decrypt(output,&out_len,input,size);
}
#else
int aml_aes_encrypt(unsigned char *output,unsigned char *input,int size)
{
	return -1;
}
int aml_aes_decrypt(unsigned char *output,unsigned char *input,int size)
{
	return -1;
}

#endif  //CONFIG_CRYPTO_AES


