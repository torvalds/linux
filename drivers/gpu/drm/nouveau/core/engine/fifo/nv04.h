#ifndef __NV04_FIFO_H__
#define __NV04_FIFO_H__

#include <engine/fifo.h>

#define NV04_PFIFO_DELAY_0                                 0x00002040
#define NV04_PFIFO_DMA_TIMESLICE                           0x00002044
#define NV04_PFIFO_NEXT_CHANNEL                            0x00002050
#define NV03_PFIFO_INTR_0                                  0x00002100
#define NV03_PFIFO_INTR_EN_0                               0x00002140
#    define NV_PFIFO_INTR_CACHE_ERROR                          (1<<0)
#    define NV_PFIFO_INTR_RUNOUT                               (1<<4)
#    define NV_PFIFO_INTR_RUNOUT_OVERFLOW                      (1<<8)
#    define NV_PFIFO_INTR_DMA_PUSHER                          (1<<12)
#    define NV_PFIFO_INTR_DMA_PT                              (1<<16)
#    define NV_PFIFO_INTR_SEMAPHORE                           (1<<20)
#    define NV_PFIFO_INTR_ACQUIRE_TIMEOUT                     (1<<24)
#define NV03_PFIFO_RAMHT                                   0x00002210
#define NV03_PFIFO_RAMFC                                   0x00002214
#define NV03_PFIFO_RAMRO                                   0x00002218
#define NV40_PFIFO_RAMFC                                   0x00002220
#define NV03_PFIFO_CACHES                                  0x00002500
#define NV04_PFIFO_MODE                                    0x00002504
#define NV04_PFIFO_DMA                                     0x00002508
#define NV04_PFIFO_SIZE                                    0x0000250c
#define NV50_PFIFO_CTX_TABLE(c)                        (0x2600+(c)*4)
#define NV50_PFIFO_CTX_TABLE__SIZE                                128
#define NV50_PFIFO_CTX_TABLE_CHANNEL_ENABLED                  (1<<31)
#define NV50_PFIFO_CTX_TABLE_UNK30_BAD                        (1<<30)
#define NV50_PFIFO_CTX_TABLE_INSTANCE_MASK_G80             0x0FFFFFFF
#define NV50_PFIFO_CTX_TABLE_INSTANCE_MASK_G84             0x00FFFFFF
#define NV03_PFIFO_CACHE0_PUSH0                            0x00003000
#define NV03_PFIFO_CACHE0_PULL0                            0x00003040
#define NV04_PFIFO_CACHE0_PULL0                            0x00003050
#define NV04_PFIFO_CACHE0_PULL1                            0x00003054
#define NV03_PFIFO_CACHE1_PUSH0                            0x00003200
#define NV03_PFIFO_CACHE1_PUSH1                            0x00003204
#define NV03_PFIFO_CACHE1_PUSH1_DMA                            (1<<8)
#define NV40_PFIFO_CACHE1_PUSH1_DMA                           (1<<16)
#define NV03_PFIFO_CACHE1_PUSH1_CHID_MASK                  0x0000000f
#define NV10_PFIFO_CACHE1_PUSH1_CHID_MASK                  0x0000001f
#define NV50_PFIFO_CACHE1_PUSH1_CHID_MASK                  0x0000007f
#define NV03_PFIFO_CACHE1_PUT                              0x00003210
#define NV04_PFIFO_CACHE1_DMA_PUSH                         0x00003220
#define NV04_PFIFO_CACHE1_DMA_FETCH                        0x00003224
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_8_BYTES         0x00000000
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_16_BYTES        0x00000008
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_24_BYTES        0x00000010
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_32_BYTES        0x00000018
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_40_BYTES        0x00000020
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_48_BYTES        0x00000028
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_56_BYTES        0x00000030
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_64_BYTES        0x00000038
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_72_BYTES        0x00000040
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_80_BYTES        0x00000048
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_88_BYTES        0x00000050
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_96_BYTES        0x00000058
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_104_BYTES       0x00000060
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_112_BYTES       0x00000068
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_120_BYTES       0x00000070
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_128_BYTES       0x00000078
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_136_BYTES       0x00000080
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_144_BYTES       0x00000088
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_152_BYTES       0x00000090
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_160_BYTES       0x00000098
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_168_BYTES       0x000000A0
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_176_BYTES       0x000000A8
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_184_BYTES       0x000000B0
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_192_BYTES       0x000000B8
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_200_BYTES       0x000000C0
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_208_BYTES       0x000000C8
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_216_BYTES       0x000000D0
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_224_BYTES       0x000000D8
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_232_BYTES       0x000000E0
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_240_BYTES       0x000000E8
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_248_BYTES       0x000000F0
#    define NV_PFIFO_CACHE1_DMA_FETCH_TRIG_256_BYTES       0x000000F8
#    define NV_PFIFO_CACHE1_DMA_FETCH_SIZE                 0x0000E000
#    define NV_PFIFO_CACHE1_DMA_FETCH_SIZE_32_BYTES        0x00000000
#    define NV_PFIFO_CACHE1_DMA_FETCH_SIZE_64_BYTES        0x00002000
#    define NV_PFIFO_CACHE1_DMA_FETCH_SIZE_96_BYTES        0x00004000
#    define NV_PFIFO_CACHE1_DMA_FETCH_SIZE_128_BYTES       0x00006000
#    define NV_PFIFO_CACHE1_DMA_FETCH_SIZE_160_BYTES       0x00008000
#    define NV_PFIFO_CACHE1_DMA_FETCH_SIZE_192_BYTES       0x0000A000
#    define NV_PFIFO_CACHE1_DMA_FETCH_SIZE_224_BYTES       0x0000C000
#    define NV_PFIFO_CACHE1_DMA_FETCH_SIZE_256_BYTES       0x0000E000
#    define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS             0x001F0000
#    define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_0           0x00000000
#    define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_1           0x00010000
#    define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_2           0x00020000
#    define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_3           0x00030000
#    define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_4           0x00040000
#    define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_5           0x00050000
#    define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_6           0x00060000
#    define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_7           0x00070000
#    define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_8           0x00080000
#    define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_9           0x00090000
#    define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_10          0x000A0000
#    define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_11          0x000B0000
#    define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_12          0x000C0000
#    define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_13          0x000D0000
#    define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_14          0x000E0000
#    define NV_PFIFO_CACHE1_DMA_FETCH_MAX_REQS_15          0x000F0000
#    define NV_PFIFO_CACHE1_ENDIAN                         0x80000000
#    define NV_PFIFO_CACHE1_LITTLE_ENDIAN                  0x7FFFFFFF
#    define NV_PFIFO_CACHE1_BIG_ENDIAN                     0x80000000
#define NV04_PFIFO_CACHE1_DMA_STATE                        0x00003228
#define NV04_PFIFO_CACHE1_DMA_INSTANCE                     0x0000322c
#define NV04_PFIFO_CACHE1_DMA_CTL                          0x00003230
#define NV04_PFIFO_CACHE1_DMA_PUT                          0x00003240
#define NV04_PFIFO_CACHE1_DMA_GET                          0x00003244
#define NV10_PFIFO_CACHE1_REF_CNT                          0x00003248
#define NV10_PFIFO_CACHE1_DMA_SUBROUTINE                   0x0000324C
#define NV03_PFIFO_CACHE1_PULL0                            0x00003240
#define NV04_PFIFO_CACHE1_PULL0                            0x00003250
#    define NV04_PFIFO_CACHE1_PULL0_HASH_FAILED            0x00000010
#    define NV04_PFIFO_CACHE1_PULL0_HASH_BUSY              0x00001000
#define NV03_PFIFO_CACHE1_PULL1                            0x00003250
#define NV04_PFIFO_CACHE1_PULL1                            0x00003254
#define NV04_PFIFO_CACHE1_HASH                             0x00003258
#define NV10_PFIFO_CACHE1_ACQUIRE_TIMEOUT                  0x00003260
#define NV10_PFIFO_CACHE1_ACQUIRE_TIMESTAMP                0x00003264
#define NV10_PFIFO_CACHE1_ACQUIRE_VALUE                    0x00003268
#define NV10_PFIFO_CACHE1_SEMAPHORE                        0x0000326C
#define NV03_PFIFO_CACHE1_GET                              0x00003270
#define NV04_PFIFO_CACHE1_ENGINE                           0x00003280
#define NV04_PFIFO_CACHE1_DMA_DCOUNT                       0x000032A0
#define NV40_PFIFO_GRCTX_INSTANCE                          0x000032E0
#define NV40_PFIFO_UNK32E4                                 0x000032E4
#define NV04_PFIFO_CACHE1_METHOD(i)                (0x00003800+(i*8))
#define NV04_PFIFO_CACHE1_DATA(i)                  (0x00003804+(i*8))
#define NV40_PFIFO_CACHE1_METHOD(i)                (0x00090000+(i*8))
#define NV40_PFIFO_CACHE1_DATA(i)                  (0x00090004+(i*8))

struct ramfc_desc {
	unsigned bits:6;
	unsigned ctxs:5;
	unsigned ctxp:8;
	unsigned regs:5;
	unsigned regp;
};

struct nv04_fifo_priv {
	struct nouveau_fifo base;
	struct ramfc_desc *ramfc_desc;
	struct nouveau_ramht  *ramht;
	struct nouveau_gpuobj *ramro;
	struct nouveau_gpuobj *ramfc;
};

struct nv04_fifo_base {
	struct nouveau_fifo_base base;
};

struct nv04_fifo_chan {
	struct nouveau_fifo_chan base;
	u32 subc[8];
	u32 ramfc;
};

int  nv04_fifo_object_attach(struct nouveau_object *,
			     struct nouveau_object *, u32);
void nv04_fifo_object_detach(struct nouveau_object *, int);

void nv04_fifo_chan_dtor(struct nouveau_object *);
int  nv04_fifo_chan_init(struct nouveau_object *);
int  nv04_fifo_chan_fini(struct nouveau_object *, bool suspend);

int  nv04_fifo_context_ctor(struct nouveau_object *, struct nouveau_object *,
			    struct nouveau_oclass *, void *, u32,
			    struct nouveau_object **);

void nv04_fifo_dtor(struct nouveau_object *);
int  nv04_fifo_init(struct nouveau_object *);
void nv04_fifo_pause(struct nouveau_fifo *, unsigned long *);
void nv04_fifo_start(struct nouveau_fifo *, unsigned long *);

#endif
