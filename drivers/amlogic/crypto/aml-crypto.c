/* Copyright (C) 2004-2006, Advanced Micro Devices, Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/crypto.h>
#include <linux/spinlock.h>
#include <linux/dma-mapping.h>
#include <crypto/algapi.h>
#include <crypto/aes.h>
#include <crypto/des.h>

#include <asm/io.h>
#include <asm/delay.h>
#include <mach/am_regs.h>

#include "aml-crypto.h"

/* Static structures */

#define AML_CRYPTO_DEBUG    0

static spinlock_t lock;

#define Wr(addr, data)  *(volatile unsigned long *)(CBUS_REG_ADDR(addr))=data
#define Rd(addr)        *(volatile unsigned long *)(CBUS_REG_ADDR(addr))

// ---------------------------------------------------------------------
// DMA descriptor table support
// ---------------------------------------------------------------------
// There are 4 threads in the DMA...
// Allocate descriptor table pointers
#define NDMA_table_size     (32*4)
unsigned char    NDMA_table_block[NDMA_table_size+32];
unsigned long    NDMA_table_ptr;
unsigned long    NDMA_table_ptr_start[4];
unsigned long    NDMA_table_ptr_curr[4];
unsigned long    NDMA_table_ptr_end[4];

#define NDMA_TABLE_START    (((unsigned long)NDMA_table_block+0x1f)&(~0x1f))

static unsigned long swap_ulong32(unsigned long val)
{
    unsigned char *p = (unsigned char *)&val;

    return ((*p)<<24)+((*(p+1))<<16)+((*(p+2))<<8)+((*(p+3))<<0);
}


// --------------------------------------------------------
static void    write_KEY_IV( 
    unsigned long addr,
    unsigned long data_hi,
    unsigned long data_lo )
{
    unsigned long temp;

    temp = swap_ulong32(data_lo);
    Wr(NDMA_TDES_KEY_LO, temp);
    temp = swap_ulong32(data_hi);
    Wr(NDMA_TDES_KEY_HI, temp);

    // Write the key
    Wr(NDMA_TDES_CONTROL, ((1 << 31) | (addr << 0)) );
}

// --------------------------------------------------------
static void    write_modes( 
    unsigned long addr,
    unsigned long cbc_en,
    unsigned long decrypt,
    unsigned long des_modes )
{
    // Write the key
    Wr( NDMA_TDES_CONTROL, ((1 << 30)           | 
                            (des_modes << 6)    |
                            (cbc_en << 5)       |
                            (decrypt << 4)      |
                            (addr << 0)) );
}

void init_table_addr( unsigned long thread_num)
{
    memset((char *)NDMA_table_ptr,0,NDMA_table_size);
    NDMA_table_ptr_start[thread_num] = NDMA_table_ptr;
    NDMA_table_ptr_curr[thread_num]= NDMA_table_ptr;
    NDMA_table_ptr_end[thread_num]= NDMA_table_ptr+NDMA_table_size;

}

// --------------------------------------------
//          NDMA_set_table_position
// --------------------------------------------
static void    NDMA_set_table_position( unsigned long thread_num,unsigned long table_start, unsigned long size)
{
    unsigned long table_end;
    table_end = table_start+size;
 
    switch( thread_num ) {
        case 3: Wr( NDMA_THREAD_TABLE_START3, table_start );
                Wr( NDMA_THREAD_TABLE_END3, table_end );
                // Pulse thread init to register the new start/end locations
                Wr( NDMA_THREAD_REG, Rd(NDMA_THREAD_REG) | (1 << 27) );
                break;
        case 2: Wr( NDMA_THREAD_TABLE_START2, table_start );
                Wr( NDMA_THREAD_TABLE_END2, table_end );
                // Pulse thread init to register the new start/end locations
                Wr( NDMA_THREAD_REG, Rd(NDMA_THREAD_REG) | (1 << 26) );
                break;
        case 1: Wr( NDMA_THREAD_TABLE_START1, table_start );
                Wr( NDMA_THREAD_TABLE_END1, table_end );
                // Pulse thread init to register the new start/end locations
                Wr( NDMA_THREAD_REG, Rd(NDMA_THREAD_REG) | (1 << 25) );
                break;
        case 0: Wr( NDMA_THREAD_TABLE_START0, table_start );
                Wr( NDMA_THREAD_TABLE_END0, table_end );
                // Pulse thread init to register the new start/end locations
                Wr( NDMA_THREAD_REG, Rd(NDMA_THREAD_REG) | (1 << 24) );
                break;
    }
}


// --------------------------------------------
//          NDMA_add_descriptor_1d
// --------------------------------------------
static void    NDMA_add_descriptor_1d(
unsigned long   thread_num,
unsigned long   irq,
unsigned long   restart,
unsigned long   pre_endian,
unsigned long   post_endian,
unsigned long   type,
unsigned long   bytes_to_move,
unsigned long   inlinetype,
unsigned long   src_addr,
unsigned long   dest_addr )
{
    volatile unsigned long *p = (volatile unsigned long *)NDMA_table_ptr_curr[thread_num];
    unsigned long curr_key;
    unsigned long keytype;
    unsigned long cryptodir;
    unsigned long mode;
    
    (*p++) =  (0x01 << 30)          |       // owned by CPU
              (pre_endian << 27)    |
              (0 << 26)             |
              (0 << 25)             |
              (inlinetype << 22)             |       // TDES in-place processing
              (irq  << 21)          |
              (0 << 0);                     // thread slice

    (*p++) =  src_addr;

    (*p++) =  dest_addr;

    (*p++) =  bytes_to_move & 0x01FFFFFF;

    (*p++) =  0x00000000;       // 2D entry
  
    (*p++) =  0x00000000;       // 2D entry
 
    // Prepare the pointer for the next descriptor boundary
    // inline processing + bytes to move extension
    switch(inlinetype) {
        case INLINE_TYPE_TDES: // TDES
        		curr_key = (type&0x3);
            (*p++) =  (restart << 6)      |
              (curr_key << 3)     |
              (post_endian << 0);
            break;
        case INLINE_TYPE_DIVX: // DIVX
            (*p++) = post_endian & 0x7;
            break;
        case INLINE_TYPE_CRC: // CRC
            (*p++) = post_endian & 0x7;
            break;
        case INLINE_TYPE_AES: // AES 
        		keytype= (type&0x3);
        		cryptodir = ((type>>2)&1);
			mode = ((type>>3)&3);
            (*p++) = (pre_endian<<0) |
               (post_endian<<4)  |
               (keytype<<8)  |
               (cryptodir<<10)|
               (((restart)?1:0)<<11)|
               (mode<<12);		
            break;    
        default:
            *p++ = 0;
            break;
    }
    *p=0;


    if( NDMA_table_ptr_curr[thread_num] == NDMA_table_ptr_end[thread_num] ) {
        NDMA_table_ptr_curr[thread_num] = NDMA_table_ptr_start[thread_num];
    } else {
        NDMA_table_ptr_curr[thread_num] += 32; // point to the next location (8-4 byte table entries)
    }
}

void NDMA_set_table_count(unsigned long thread_num,unsigned int cnt)
{
    Wr(NDMA_TABLE_ADD_REG, (thread_num << 8) | (cnt << 0) ); 
}

// --------------------------------------------
//              NDMA_start()
// --------------------------------------------
// Start the block move procedure
//
static void    NDMA_start(unsigned long thread_num)
{
    Wr( NDMA_CNTL_REG0,  (Rd(NDMA_CNTL_REG0)  | (1 << 14))                  );   // Enable
    Wr( NDMA_THREAD_REG, (Rd(NDMA_THREAD_REG) | (1 << (thread_num + 8)))    );
}

// --------------------------------------------
//              NDMA_wait_for_completion()
// --------------------------------------------
// Wait for all block moves to complete
//
static void    NDMA_wait_for_completion(unsigned long thread_num)
{   
    if( !NDMA_table_ptr_start[thread_num] ) {  // there are no table entries
        return;
    }

    while( (Rd(NDMA_TABLE_ADD_REG) & (0xF << (thread_num*8))) ) { }
}

static void set_tdes_key(unsigned long *key,unsigned int keylen,int idx,int dir)
{
    idx = (idx*4);

    if(dir)
    {
        write_KEY_IV(idx+0, *(key+2*2),*(key+2*2+1));
        write_KEY_IV(idx+1, *(key+1*2),*(key+1*2+1));    
        write_KEY_IV(idx+2, *(key),*(key+1));
    }
    else
    {
        write_KEY_IV(idx+0, *(key),*(key+1));
        write_KEY_IV(idx+1, *(key+1*2),*(key+1*2+1));
        write_KEY_IV(idx+2, *(key+2*2),*(key+2*2+1));
    }

    if(keylen == DES_KEY_SIZE*3)
    {
        write_KEY_IV(idx+3, 0xffffffff,0x00000000); 
    }
    else
    {
        write_KEY_IV(idx+3, *(key+3*2),*(key+3*2+1));
    }
}

unsigned int
aml_tdes_crypt(struct aml_crypto_ctx *op)
{
    unsigned long iflags;
    unsigned int tab_start;
#if AML_CRYPTO_DEBUG 
    int i;
#endif
    //int ret;
    
    if (op->len == 0)
        return 0;

    /* Start the critical section */
    spin_lock_irqsave(&lock, iflags);

    set_tdes_key(op->key, op->key_len,0,op->dir);
    write_modes(0,          // unsigned long addr, 
                    op->mode,          // unsigned long cbc_en, 
                    op->dir,          // unsigned long decrypt, 
                    (op->dir==1)? 5:2);        // unsigned long des_modes );

    init_table_addr(op->threadidx);
    
    op->dma_src = dma_map_single(NULL, (void *)op->src, op->len, DMA_TO_DEVICE);
    if(op->src != op->dst)
        op->dma_dest = dma_map_single(NULL, (void *)op->dst, op->len, DMA_TO_DEVICE);
    else
        op->dma_dest = op->dma_src;
    
    NDMA_add_descriptor_1d(
                        op->threadidx,              // unsigned long    thread_num
                        0,              // unsigned long   irq,
                        0,              // unsigned long   restart
                        7,              // 0 unsigned long   pre_endian,
                        7,              // unsigned long   post_endian,
                        0,              // unsigned long   curr_key,
                        op->len,              // unsigned long   bytes_to_move,
                        INLINE_TYPE_TDES,   //inlinetype
                        op->dma_src,     // unsigned long   src_addr,
                        op->dma_dest);   // unsigned long   dest_addr )

    tab_start = dma_map_single(NULL, (void *)NDMA_table_ptr, NDMA_table_size, DMA_TO_DEVICE);
    //printk("bus addr = %8x,phy addr=%8x\n",NDMA_table_ptr,tab_start);
    NDMA_set_table_position( 0, tab_start, NDMA_table_size );
    NDMA_set_table_count(op->threadidx,1);  

#if AML_CRYPTO_DEBUG
    for(i=0x2270;i<0x228D;i++)
        printk("reg(%4x) = 0x%8x\n",i,Rd(i));

    for(i=0;i<8;i++)
    {
        printk("table addr(%d) = 0x%8x\n",i,*(unsigned long *)(NDMA_table_ptr+i*4));
    }
#endif

    NDMA_start(op->threadidx);
    NDMA_wait_for_completion(op->threadidx);

    dma_unmap_single(NULL, tab_start, NDMA_table_size, DMA_FROM_DEVICE);
    dma_unmap_single(NULL, op->dma_src, op->len,DMA_FROM_DEVICE);
    if(op->dma_dest!=op->dma_src)
        dma_unmap_single(NULL, op->dma_dest, op->len,DMA_FROM_DEVICE);
#if AML_CRYPTO_DEBUG
    for(i=0x2270;i<0x228D;i++)
        printk("reg(%4x) = 0x%8x\n",i,Rd(i));

    for(i=0;i<8;i++)
    {
        printk("table addr(%d) = 0x%8x\n",i,*(unsigned long *)(NDMA_table_ptr+i*4));
    }
#endif
    spin_unlock_irqrestore(&lock, iflags);

    return op->len;
}

static void set_aes_key(unsigned long *key,unsigned int keylen)
{
    Wr(NDMA_AES_KEY_0, *key);
    Wr(NDMA_AES_KEY_1, *(key+1));
    Wr(NDMA_AES_KEY_2, *(key+2));
    Wr(NDMA_AES_KEY_3, *(key+3));
    if(keylen>AES_KEYSIZE_128)
    {
    	Wr(NDMA_AES_KEY_4, *(key+4));
    	Wr(NDMA_AES_KEY_5, *(key+5));
    }
    if(keylen>AES_KEYSIZE_192)
    {
      Wr(NDMA_AES_KEY_6, *(key+6));
      Wr(NDMA_AES_KEY_7, *(key+7));
    }    
}

static void set_aes_iv(unsigned long *iv)
{
    Wr(NDMA_AES_IV_0, *iv);
    Wr(NDMA_AES_IV_1, *(iv+1));
    Wr(NDMA_AES_IV_2, *(iv+2));
    Wr(NDMA_AES_IV_3, *(iv+3));
}

static void set_aes_iv_big_endian(unsigned long *iv)
{
    Wr(NDMA_AES_IV_3, swap_ulong32(*iv));
    Wr(NDMA_AES_IV_2, swap_ulong32(*(iv+1)));
    Wr(NDMA_AES_IV_1, swap_ulong32(*(iv+2)));
    Wr(NDMA_AES_IV_0, swap_ulong32(*(iv+3)));
}
//now only support decrypto
unsigned int
aml_aes_crypt(struct aml_crypto_ctx *op, unsigned long restart)
{
    unsigned long iflags;
    unsigned int tab_start;
    unsigned long type;
#if AML_CRYPTO_DEBUG 
    int i;
#endif
    //int ret;
#if 0
	char aes_key[16] = {0x7e,0x24,0x06,0x78,0x17,0xfa,0xe0,0xd7,
	                                 0x43,0xd6,0xce,0x1f,0x32,0x53,0x91,0x63};
       char aes_counter[16]= {0x00,0x6c,0xb6,0xdb,0xc0,0x54,0x3b,0x59,
	   	                          0xda,0x48,0xd9,0x0b,0x00,0x00,0x00,0x01};
       char aes_string[32] = {0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,
	   	                        0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f,
	   	                        0x10,0x11,0x12,0x13,0x14,0x15,0x16,0x17,
	   	                        0x18,0x19,0x1a,0x1b,0x1c,0x1d,0x1e,0x1f};

	op->len =32;
	memcpy(op->src,aes_string,op->len);
	memcpy(op->key,aes_key,16);
	memcpy(op->iv,aes_counter,16);
#endif

    if (op->len == 0)
        return 0;

    /* Start the critical section */
    spin_lock_irqsave(&lock, iflags);

    set_aes_key(op->key, op->key_len);
    if (op->mode == MODE_CBC)
    	set_aes_iv(op->iv);
    else if (op->mode == MODE_CTR) {
        set_aes_iv_big_endian(op->iv);
    }
    	                    	
    init_table_addr(op->threadidx);

    op->dma_src = dma_map_single(NULL, (void *)op->src, op->len, DMA_TO_DEVICE);
    if(op->src != op->dst)
        op->dma_dest = dma_map_single(NULL, (void *)op->dst, op->len, DMA_TO_DEVICE);
    else
        op->dma_dest = op->dma_src;
    
    type = ((op->mode&0x3)<<3)|
		(((op->dir==DIR_DECRYPT)?0:1)<<2)|
    	     ((op->key_len==AES_KEYSIZE_128)?0:((op->key_len==AES_KEYSIZE_192)?1:2));
    NDMA_add_descriptor_1d(
                        op->threadidx,              // unsigned long    thread_num
                        0,              // unsigned long   irq,
                        restart,              // unsigned long   restart
                        0,              // 0 unsigned long   pre_endian,
                        0,              // unsigned long   post_endian,
                        type,              // unsigned long   keytype + cryptodir,
                        op->len,              // unsigned long   bytes_to_move,
                        INLINE_TYPE_AES,   //inlinetype
                        op->dma_src,     // unsigned long   src_addr,
                        op->dma_dest);   // unsigned long   dest_addr )

    tab_start = dma_map_single(NULL, (void *)NDMA_table_ptr, NDMA_table_size, DMA_TO_DEVICE);
    //printk("bus addr = %8x,phy addr=%8x\n",NDMA_table_ptr,tab_start);
    NDMA_set_table_position( 0, tab_start, NDMA_table_size );
    NDMA_set_table_count(op->threadidx,1);  

#if AML_CRYPTO_DEBUG
    for(i=0x2270;i<0x229d;i++)
        printk("reg(%4x) = 0x%8x\n",i,Rd(i));

    for(i=0;i<8;i++)
    {
        printk("table addr(%d) = 0x%8x\n",i,*(unsigned long *)(NDMA_table_ptr+i*4));
    }
#endif

    NDMA_start(op->threadidx);
    NDMA_wait_for_completion(op->threadidx);

    dma_unmap_single(NULL, tab_start, NDMA_table_size, DMA_FROM_DEVICE);
    dma_unmap_single(NULL, op->dma_src, op->len,DMA_FROM_DEVICE);
    if(op->dma_dest!=op->dma_src)
        dma_unmap_single(NULL, op->dma_dest, op->len,DMA_FROM_DEVICE);
#if AML_CRYPTO_DEBUG
    for(i=0x2270;i<0x229d;i++)
        printk("reg(%4x) = 0x%8x\n",i,Rd(i));

    for(i=0;i<8;i++)
    {
        printk("table addr(%d) = 0x%8x\n",i,*(unsigned long *)(NDMA_table_ptr+i*4));
    }	
#endif
#if 0
    for(i=0;i<32;i++)
    {
        printk("dst(%d) = 0x%x\n",i,*((unsigned char *)op->dst + i));
    }
#endif
    spin_unlock_irqrestore(&lock, iflags);

    return op->len;
}

static int aml_tdes_setkey_blk(struct crypto_tfm *tfm, const u8 *key,
		unsigned int len)
{
    struct aml_crypto_ctx *op = crypto_tfm_ctx(tfm);
    
    op->key_len = len;

    if ((len > TDES_KEY_LENGTH) ||(len<DES_KEY_SIZE*3)){
        printk("aml_ecb_tdes key len is wrong.\n");
        return 1;
    }

    memcpy(op->key, key, len);
    return 0;
}

/************************
tdes ecb  crypto
*************************/
static int
aml_ecb_tdes_decrypt(struct blkcipher_desc *desc,
		  struct scatterlist *dst, struct scatterlist *src,
		  unsigned int nbytes)
{
    struct aml_crypto_ctx *op = crypto_blkcipher_ctx(desc->tfm);
    struct blkcipher_walk walk;
    int err=0, ret;

    if ((op->key_len > TDES_KEY_LENGTH) ||(op->key_len<DES_KEY_SIZE*3)){
        printk("aml_ecb_tdes key len is wrong.\n");
        return 1;
    }

    blkcipher_walk_init(&walk, dst, src, nbytes);
    err = blkcipher_walk_virt(desc, &walk);

    //printk("blkcipher_walk_virt return %d\n",err);
    while((nbytes = walk.nbytes)) {  
        op->src = walk.src.virt.addr,
        op->dst = walk.dst.virt.addr;
        op->mode = MODE_ECB;
        op->len = nbytes - (nbytes % TDES_MIN_BLOCK_SIZE);
        op->dir = DIR_DECRYPT;
        op->threadidx = 0;
        ret = aml_tdes_crypt(op);

        nbytes -= ret;
        err = blkcipher_walk_done(desc, &walk, nbytes);
        //printk("blkcipher_walk_done return %d nbyte = 0x%x\n",err,nbytes);
    }

    return err;
}

static int
aml_ecb_tdes_encrypt(struct blkcipher_desc *desc,
		  struct scatterlist *dst, struct scatterlist *src,
		  unsigned int nbytes)
{
    struct aml_crypto_ctx *op = crypto_blkcipher_ctx(desc->tfm);
    struct blkcipher_walk walk;
    int err=0, ret;

    if ((op->key_len > TDES_KEY_LENGTH) ||(op->key_len<DES_KEY_SIZE*3)){
        printk("aml_ecb_tdes key len is wrong.\n");
        return 1;
    }

    blkcipher_walk_init(&walk, dst, src, nbytes);
    err = blkcipher_walk_virt(desc, &walk);
    //printk("blkcipher_walk_virt return %d\n",err);
    while((nbytes = walk.nbytes)) {
        op->src = walk.src.virt.addr,
        op->dst = walk.dst.virt.addr;
        op->mode = MODE_ECB;
        op->len = nbytes - (nbytes % TDES_MIN_BLOCK_SIZE);
        op->dir = DIR_ENCRYPT;
        op->threadidx = 0;
        ret = aml_tdes_crypt(op);

        nbytes -= ret;
        err = blkcipher_walk_done(desc, &walk, nbytes);
        //printk("blkcipher_walk_done return %d nbyte = 0x%x\n",err,nbytes);
    }

    return err;
}

static struct crypto_alg aml_ecb_tdes_alg = {
	.cra_name			=	"ecb(tdes)",
	.cra_driver_name	=	"ecb-tdes-aml",
	.cra_priority		=	300,
	.cra_flags			=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	TDES_MIN_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct aml_crypto_ctx),
	.cra_alignmask		=	0,
	.cra_type			=	&crypto_blkcipher_type,
	.cra_module			=	THIS_MODULE,
	.cra_list			=	LIST_HEAD_INIT(aml_ecb_tdes_alg.cra_list),
	.cra_u				=	{
		.blkcipher	=	{
                     .min_keysize	=	TDES_KEY_LENGTH-8,
			.max_keysize	=	TDES_KEY_LENGTH,
			.setkey			=	aml_tdes_setkey_blk,
			.encrypt		=	aml_ecb_tdes_encrypt,
			.decrypt		=	aml_ecb_tdes_decrypt,
		}
	}
};

/************************
tdes cbc  crypto
*************************/
static int
aml_cbc_tdes_decrypt(struct blkcipher_desc *desc,
		  struct scatterlist *dst, struct scatterlist *src,
		  unsigned int nbytes)
{
    struct aml_crypto_ctx *op = crypto_blkcipher_ctx(desc->tfm);
    struct blkcipher_walk walk;
    int err=0, ret;

    if ((op->key_len > TDES_KEY_LENGTH) ||(op->key_len<DES_KEY_SIZE*3)){
        printk("aml_cbc_tdes key len is wrong.\n");
        return 1;
    }

    blkcipher_walk_init(&walk, dst, src, nbytes);
    err = blkcipher_walk_virt(desc, &walk);
    
    while((nbytes = walk.nbytes)) {  
        op->src = walk.src.virt.addr,
        op->dst = walk.dst.virt.addr;
        op->mode = MODE_CBC;
        op->len = nbytes - (nbytes % TDES_MIN_BLOCK_SIZE);
        op->dir = DIR_DECRYPT;
        op->threadidx = 0;
        ret = aml_tdes_crypt(op);

        nbytes -= ret;
        err = blkcipher_walk_done(desc, &walk, nbytes);
    }

    return err;
}

static int
aml_cbc_tdes_encrypt(struct blkcipher_desc *desc,
		  struct scatterlist *dst, struct scatterlist *src,
		  unsigned int nbytes)
{
    struct aml_crypto_ctx *op = crypto_blkcipher_ctx(desc->tfm);
    struct blkcipher_walk walk;
    int err=0, ret;

    if ((op->key_len > TDES_KEY_LENGTH) ||(op->key_len<DES_KEY_SIZE*3)){
        printk("aml_cbc_tdes key len is wrong.\n");
        return 1;
    }

    blkcipher_walk_init(&walk, dst, src, nbytes);
    err = blkcipher_walk_virt(desc, &walk);
    while((nbytes = walk.nbytes)) {
        op->src = walk.src.virt.addr,
        op->dst = walk.dst.virt.addr;
        op->mode = MODE_CBC;
        op->len = nbytes - (nbytes % TDES_MIN_BLOCK_SIZE);
        op->dir = DIR_ENCRYPT;
        op->threadidx = 0;
        ret = aml_tdes_crypt(op);

        nbytes -= ret;
        err = blkcipher_walk_done(desc, &walk, nbytes);
    }

    return err;
}

static struct crypto_alg aml_cbc_tdes_alg = {
	.cra_name			=	"cbc(tdes)",
	.cra_driver_name	=	"cbc-tdes-aml",
	.cra_priority		=	300,
	.cra_flags			=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	TDES_MIN_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct aml_crypto_ctx),
	.cra_alignmask		=	0,
	.cra_type			=	&crypto_blkcipher_type,
	.cra_module			=	THIS_MODULE,
	.cra_list			=	LIST_HEAD_INIT(aml_cbc_tdes_alg.cra_list),
	.cra_u				=	{
		.blkcipher	=	{
                     .min_keysize	=	TDES_KEY_LENGTH-8,
			.max_keysize	=	TDES_KEY_LENGTH,
			.setkey			=	aml_tdes_setkey_blk,
			.encrypt		=	aml_cbc_tdes_encrypt,
			.decrypt		=	aml_cbc_tdes_decrypt,
		}
	}
};

/************************
aes ecb  crypto
*************************/
static int aml_ecb_aes_setkey_blk(struct crypto_tfm *tfm, const u8 *key,
		unsigned int len)
{
#if MESON_CPU_TYPE <= MESON_CPU_TYPE_MESON6
		printk("aes alg by hardware is not support on this chip.\n");
    return 1;
#else	
    struct aml_crypto_ctx *op = crypto_tfm_ctx(tfm);
    
    if (len != AES_KEYSIZE_192 && len != AES_KEYSIZE_256 && len != AES_KEYSIZE_128) {
        printk("aml_ecb_aes key len is wrong.\n");
        return 1;
    }
    op->key_len = len;
    memcpy(op->key, key, len);
    return 0;
#endif
}

static int
aml_ecb_aes_encrypt(struct blkcipher_desc *desc,
		  struct scatterlist *dst, struct scatterlist *src,
		  unsigned int nbytes)
{
#if MESON_CPU_TYPE <= MESON_CPU_TYPE_MESON6
		printk("ecb-aes alg by hardware is not support on this chip.\n");
    return 1;
#else	 
    struct aml_crypto_ctx *op = crypto_blkcipher_ctx(desc->tfm);
    struct blkcipher_walk walk;
    int err=0, ret;

    if (op->key_len != AES_KEYSIZE_192 && op->key_len != AES_KEYSIZE_256 && op->key_len != AES_KEYSIZE_128) {
        printk("aml_ecb_aes key len is wrong.\n");
        return 1;
    }

    blkcipher_walk_init(&walk, dst, src, nbytes);
    err = blkcipher_walk_virt(desc, &walk);
    while((nbytes = walk.nbytes)) {
        op->src = walk.src.virt.addr,
        op->dst = walk.dst.virt.addr;
        op->mode = MODE_ECB;
        op->len = nbytes - (nbytes % AES_BLOCK_SIZE);
        op->dir = DIR_ENCRYPT;
        op->threadidx = 0;
        ret = aml_aes_crypt(op, 0);

        nbytes -= ret;
        err = blkcipher_walk_done(desc, &walk, nbytes);
    }

    return err;
#endif
}

static int
aml_ecb_aes_decrypt(struct blkcipher_desc *desc,
		  struct scatterlist *dst, struct scatterlist *src,
		  unsigned int nbytes)
{
#if MESON_CPU_TYPE <= MESON_CPU_TYPE_MESON6
		printk("ecb-aes alg by hardware is not support on this chip.\n");
    return 1;
#else	
    struct aml_crypto_ctx *op = crypto_blkcipher_ctx(desc->tfm);
    struct blkcipher_walk walk;
    int err=0, ret;

    if (op->key_len != AES_KEYSIZE_192 && op->key_len != AES_KEYSIZE_256 && op->key_len != AES_KEYSIZE_128) {
        printk("aml_ecb_aes key len is wrong.\n");
        return 1;
    }

    blkcipher_walk_init(&walk, dst, src, nbytes);
    err = blkcipher_walk_virt(desc, &walk);
    while((nbytes = walk.nbytes)) {
        op->src = walk.src.virt.addr,
        op->dst = walk.dst.virt.addr;
        op->mode = MODE_ECB;
        op->len = nbytes - (nbytes % AES_BLOCK_SIZE);
        op->dir = DIR_DECRYPT;
        op->threadidx = 0;
        ret = aml_aes_crypt(op, 0);

        nbytes -= ret;
        err = blkcipher_walk_done(desc, &walk, nbytes);
    }

    return err;
#endif
}

static struct crypto_alg aml_ecb_aes_alg = {
	.cra_name			=	"ecb-aes-aml",
	.cra_driver_name	=	"ecb-aes-aml",
	.cra_priority		=	300,
	.cra_flags			=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct aml_crypto_ctx),
	.cra_alignmask		=	0,
	.cra_type			=	&crypto_blkcipher_type,
	.cra_module			=	THIS_MODULE,
	.cra_list			=	LIST_HEAD_INIT(aml_ecb_aes_alg.cra_list),
	.cra_u				=	{
		.blkcipher	=	{
                     .min_keysize	=	AES_MIN_KEY_SIZE,
			.max_keysize	=	AES_MAX_KEY_SIZE,
			.setkey			=	aml_ecb_aes_setkey_blk,
			.encrypt		=	aml_ecb_aes_encrypt,
			.decrypt		=	aml_ecb_aes_decrypt,
		}
	}
};


/************************
aes cbc  crypto
*************************/
static int aml_cbc_aes_setkey_blk(struct crypto_tfm *tfm, const u8 *key,
		unsigned int len)
{
#if MESON_CPU_TYPE <= MESON_CPU_TYPE_MESON6
		printk("aes alg by hardware is not support on this chip.\n");
    return 1;
#else	
    struct aml_crypto_ctx *op = crypto_tfm_ctx(tfm);
    int ivsize = AES_BLOCK_SIZE;
    int keysize = len-ivsize;
    
    if (keysize != AES_KEYSIZE_192 && keysize != AES_KEYSIZE_256 && keysize != AES_KEYSIZE_128) {
        printk("aml_ecb_aes key len is wrong.\n");
        return 1;
    }
    op->key_len = keysize;
    memcpy(op->key, key, keysize);
    memcpy(op->iv,key+keysize,ivsize);
    return 0;
#endif
}

static int
aml_cbc_aes_encrypt(struct blkcipher_desc *desc,
		  struct scatterlist *dst, struct scatterlist *src,
		  unsigned int nbytes)
{
#if MESON_CPU_TYPE <= MESON_CPU_TYPE_MESON6
		printk("cbc-aes alg by hardware is not support on this chip.\n");
    return 1;
#else	 
    struct aml_crypto_ctx *op = crypto_blkcipher_ctx(desc->tfm);
    struct blkcipher_walk walk;
    int err=0, ret;
    unsigned long restart = 1;

    if (op->key_len != AES_KEYSIZE_192 && op->key_len != AES_KEYSIZE_256 && op->key_len != AES_KEYSIZE_128) {
        printk("aml_cbc_aes key len is wrong.\n");
        return 1;
    }

    blkcipher_walk_init(&walk, dst, src, nbytes);
    err = blkcipher_walk_virt(desc, &walk);
    while((nbytes = walk.nbytes)) {
        op->src = walk.src.virt.addr,
        op->dst = walk.dst.virt.addr;
        op->mode = MODE_CBC;
        op->len = nbytes - (nbytes % AES_BLOCK_SIZE);
        op->dir = DIR_ENCRYPT;
        op->threadidx = 0;
        ret = aml_aes_crypt(op, restart);
        restart = 0;

        nbytes -= ret;
        err = blkcipher_walk_done(desc, &walk, nbytes);
    }

    return err;
#endif
}

static int
aml_cbc_aes_decrypt(struct blkcipher_desc *desc,
		  struct scatterlist *dst, struct scatterlist *src,
		  unsigned int nbytes)
{
#if MESON_CPU_TYPE <= MESON_CPU_TYPE_MESON6
		printk("cbc-aes alg by hardware is not support on this chip.\n");
    return 1;
#else	
    struct aml_crypto_ctx *op = crypto_blkcipher_ctx(desc->tfm);
    struct blkcipher_walk walk;
    int err=0, ret;
    unsigned long restart = 1;

    if (op->key_len != AES_KEYSIZE_192 && op->key_len != AES_KEYSIZE_256 && op->key_len != AES_KEYSIZE_128) {
        printk("aml_cbc_aes key len is wrong.\n");
        return 1;
    }

    blkcipher_walk_init(&walk, dst, src, nbytes);
    err = blkcipher_walk_virt(desc, &walk);
    while((nbytes = walk.nbytes)) {
        op->src = walk.src.virt.addr,
        op->dst = walk.dst.virt.addr;
        op->mode = MODE_CBC;
        op->len = nbytes - (nbytes % AES_BLOCK_SIZE);
        op->dir = DIR_DECRYPT;
        op->threadidx = 0;
        ret = aml_aes_crypt(op, restart);
        restart = 0;

        nbytes -= ret;
        err = blkcipher_walk_done(desc, &walk, nbytes);
    }

    return err;
#endif
}

static struct crypto_alg aml_cbc_aes_alg = {
	.cra_name			=	"cbc-aes-aml",
	.cra_driver_name	=	"cbc-aes-aml",
	.cra_priority		=	300,
	.cra_flags			=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct aml_crypto_ctx),
	.cra_alignmask		=	0,
	.cra_type			=	&crypto_blkcipher_type,
	.cra_module			=	THIS_MODULE,
	.cra_list			=	LIST_HEAD_INIT(aml_cbc_aes_alg.cra_list),
	.cra_u				=	{
		.blkcipher	=	{
                     .min_keysize	=	AES_MIN_KEY_SIZE+AES_BLOCK_SIZE,
			.max_keysize	=	AES_MAX_KEY_SIZE+AES_BLOCK_SIZE,
			.setkey			=	aml_cbc_aes_setkey_blk,
			.encrypt		=	aml_cbc_aes_encrypt,
			.decrypt		=	aml_cbc_aes_decrypt,
		}
	}
};

/************************
aes ctr  crypto
*************************/
static int aml_ctr_aes_setkey_blk(struct crypto_tfm *tfm, const u8 *key,
		unsigned int len)
{
#if MESON_CPU_TYPE <= MESON_CPU_TYPE_MESON6
		printk("aes alg by hardware is not support on this chip.\n");
    return 1;
#else	
    struct aml_crypto_ctx *op = crypto_tfm_ctx(tfm);
    int ivsize = AES_BLOCK_SIZE;
    int keysize = len-ivsize;
    
    if (keysize != AES_KEYSIZE_192 && keysize != AES_KEYSIZE_256 && keysize != AES_KEYSIZE_128) {
        printk("aml_ecb_aes key len is wrong.\n");
        return 1;
    }
    op->key_len = keysize;
    memcpy(op->key, key, keysize);
    memcpy(op->iv,key+keysize,ivsize);
    return 0;
#endif
}

static int
aml_ctr_aes_encrypt(struct blkcipher_desc *desc,
		  struct scatterlist *dst, struct scatterlist *src,
		  unsigned int nbytes)
{
#if MESON_CPU_TYPE <= MESON_CPU_TYPE_MESON6
		printk("cbc-aes alg by hardware is not support on this chip.\n");
    return 1;
#else	 
    struct aml_crypto_ctx *op = crypto_blkcipher_ctx(desc->tfm);
    struct blkcipher_walk walk;
    int err=0, ret;

    unsigned long restart = 1;
    if (op->key_len != AES_KEYSIZE_192 && op->key_len != AES_KEYSIZE_256 && op->key_len != AES_KEYSIZE_128) {
        printk("aml_cbc_aes key len is wrong.\n");
        return 1;
    }

    blkcipher_walk_init(&walk, dst, src, nbytes);
    err = blkcipher_walk_virt(desc, &walk);
    while((nbytes = walk.nbytes)) {
        op->src = walk.src.virt.addr,
        op->dst = walk.dst.virt.addr;
        op->mode = MODE_CTR;
        op->len = nbytes - (nbytes % AES_BLOCK_SIZE);
        op->dir = DIR_ENCRYPT;
        op->threadidx = 0;
        ret = aml_aes_crypt(op, restart);
        restart = 0;

        nbytes -= ret;
        err = blkcipher_walk_done(desc, &walk, nbytes);
    }

    return err;
#endif
}

static int
aml_ctr_aes_decrypt(struct blkcipher_desc *desc,
		  struct scatterlist *dst, struct scatterlist *src,
		  unsigned int nbytes)
{
#if MESON_CPU_TYPE <= MESON_CPU_TYPE_MESON6
		printk("cbc-aes alg by hardware is not support on this chip.\n");
    return 1;
#else	
    struct aml_crypto_ctx *op = crypto_blkcipher_ctx(desc->tfm);
    struct blkcipher_walk walk;
    int err=0, ret;
    unsigned long restart = 1;

    if (op->key_len != AES_KEYSIZE_192 && op->key_len != AES_KEYSIZE_256 && op->key_len != AES_KEYSIZE_128) {
        printk("aml_cbc_aes key len is wrong.\n");
        return 1;
    }

    blkcipher_walk_init(&walk, dst, src, nbytes);
    err = blkcipher_walk_virt(desc, &walk);
    while((nbytes = walk.nbytes)) {
        op->src = walk.src.virt.addr,
        op->dst = walk.dst.virt.addr;
        op->mode = MODE_CTR;
        op->len = nbytes - (nbytes % AES_BLOCK_SIZE);
        op->dir = DIR_DECRYPT;
        op->threadidx = 0;
        ret = aml_aes_crypt(op, restart);
        restart = 0;

        nbytes -= ret;
        err = blkcipher_walk_done(desc, &walk, nbytes);
    }

    return err;
#endif
}

static struct crypto_alg aml_ctr_aes_alg = {
	.cra_name			=	"ctr-aes-aml",
	.cra_driver_name	=	"ctr-aes-aml",
	.cra_priority		=	300,
	.cra_flags			=	CRYPTO_ALG_TYPE_BLKCIPHER,
	.cra_blocksize		=	AES_BLOCK_SIZE,
	.cra_ctxsize		=	sizeof(struct aml_crypto_ctx),
	.cra_alignmask		=	0,
	.cra_type			=	&crypto_blkcipher_type,
	.cra_module			=	THIS_MODULE,
	.cra_list			=	LIST_HEAD_INIT(aml_ctr_aes_alg.cra_list),
	.cra_u				=	{
		.blkcipher	=	{
                     .min_keysize	=	AES_MIN_KEY_SIZE+AES_BLOCK_SIZE,
			.max_keysize	=	AES_MAX_KEY_SIZE+AES_BLOCK_SIZE,
			.setkey			=	aml_ctr_aes_setkey_blk,
			.encrypt		=	aml_ctr_aes_encrypt,
			.decrypt		=	aml_ctr_aes_decrypt,
		}
	}
};
#define blkmove_thread_idx  2
unsigned int
aml_blkmove(unsigned char * dst,unsigned char * src,unsigned long count)
{
    unsigned long iflags;
    unsigned int tab_start;
    unsigned int dma_src;
    unsigned int dma_dest;
    
    if (count == 0)
        return 0;

    if(dst==src)
        return 0;
    
    if(count>0x1ffffff)
    {
        printk("aml_blkmove count is too large!\n");
        return -1;
    }
    /* Start the critical section */
    spin_lock_irqsave(&lock, iflags);
    init_table_addr(blkmove_thread_idx);
    
    dma_src = dma_map_single(NULL, (void *)src, count, DMA_TO_DEVICE);
    dma_dest = dma_map_single(NULL, (void *)dst, count, DMA_TO_DEVICE);
    NDMA_add_descriptor_1d(
                        blkmove_thread_idx,              // unsigned long    thread_num
                        0,              // unsigned long   irq,
                        0,              // unsigned long   restart
                        0,              // 0 unsigned long   pre_endian,
                        0,              // unsigned long   post_endian,
                        0,              // unsigned long   curr_key,
                        count,              // unsigned long   bytes_to_move,
                        INLINE_TYPE_NORMAL,   //inlinetype
                        dma_src,     // unsigned long   src_addr,
                        dma_dest);   // unsigned long   dest_addr )

    tab_start = dma_map_single(NULL, (void *)NDMA_table_ptr, NDMA_table_size, DMA_TO_DEVICE);
    //printk("bus addr = %8x,phy addr=%8x\n",NDMA_table_ptr,tab_start);
    NDMA_set_table_position( blkmove_thread_idx, tab_start, NDMA_table_size );
    NDMA_set_table_count(blkmove_thread_idx,1);  

    NDMA_start(blkmove_thread_idx);
    NDMA_wait_for_completion(blkmove_thread_idx);

    dma_unmap_single(NULL, tab_start, NDMA_table_size, DMA_FROM_DEVICE);
    dma_unmap_single(NULL, dma_src, count,DMA_FROM_DEVICE);
    dma_unmap_single(NULL, dma_dest, count,DMA_FROM_DEVICE);
    spin_unlock_irqrestore(&lock, iflags);

    return 0;
}
EXPORT_SYMBOL(aml_blkmove);

static int __init aml_hw_crypto_init(void)
{
    int ret;

    NDMA_table_ptr = NDMA_TABLE_START;
    
    printk("aml_hw_crypto initialization.\n");   
    if ((ret = crypto_register_alg(&aml_ecb_tdes_alg)))
        goto efail;

    if ((ret = crypto_register_alg(&aml_cbc_tdes_alg)))
        goto efail;

    if ((ret = crypto_register_alg(&aml_ecb_aes_alg)))
        goto efail;

    if ((ret = crypto_register_alg(&aml_cbc_aes_alg)))
        goto efail;	
	
    if ((ret = crypto_register_alg(&aml_ctr_aes_alg)))
        goto efail;		
	
    return 0;
efail:
    printk(KERN_ERR "aml_hw_crypto initialization failed.\n");
    return ret;
}
module_init(aml_hw_crypto_init);

static void __exit aml_hw_crypto_exit(void)
{
    crypto_unregister_alg(&aml_ecb_tdes_alg);
    crypto_unregister_alg(&aml_cbc_tdes_alg);
    crypto_unregister_alg(&aml_ecb_aes_alg);
    crypto_unregister_alg(&aml_cbc_aes_alg);
    crypto_unregister_alg(&aml_ctr_aes_alg);
}
module_exit(aml_hw_crypto_exit);

MODULE_AUTHOR("qi.duan <qi.duan@amlogic.om>");
MODULE_DESCRIPTION("Support for amlogic's cryptographic engine");
MODULE_LICENSE("GPL");

