/*
 * The device /dev/cryptocop is accessible using this driver using
 * CRYPTOCOP_MAJOR (254) and minor number 0.
 */
#ifndef CRYPTOCOP_H
#define CRYPTOCOP_H

#include <uapi/arch-v32/arch/cryptocop.h>


/********** The API to use from inside the kernel. ************/

#include <arch/hwregs/dma.h>

typedef enum {
	cryptocop_alg_csum = 0,
	cryptocop_alg_mem2mem,
	cryptocop_alg_md5,
	cryptocop_alg_sha1,
	cryptocop_alg_des,
	cryptocop_alg_3des,
	cryptocop_alg_aes,
	cryptocop_no_alg,
} cryptocop_algorithm;

typedef u8 cryptocop_tfrm_id;


struct cryptocop_operation;

typedef void (cryptocop_callback)(struct cryptocop_operation*, void*);

struct cryptocop_transform_init {
	cryptocop_algorithm    alg;
	/* Keydata for ciphers. */
	unsigned char          key[CRYPTOCOP_MAX_KEY_LENGTH];
	unsigned int           keylen;
	cryptocop_cipher_mode  cipher_mode;
	cryptocop_3des_mode    tdes_mode;
	cryptocop_csum_type    csum_mode; /* cryptocop_csum_none is not allowed when alg==cryptocop_alg_csum */

	cryptocop_tfrm_id tid; /* Locally unique in session; assigned by user, checked by driver. */
	struct cryptocop_transform_init *next;
};


typedef enum {
	cryptocop_source_dma = 0,
	cryptocop_source_des,
	cryptocop_source_3des,
	cryptocop_source_aes,
	cryptocop_source_md5,
	cryptocop_source_sha1,
	cryptocop_source_csum,
	cryptocop_source_none,
} cryptocop_source;


struct cryptocop_desc_cfg {
	cryptocop_tfrm_id tid;
	cryptocop_source src;
	unsigned int last:1; /* Last use of this transform in the operation.  Will push outdata when encountered. */
	struct cryptocop_desc_cfg *next;
};

struct cryptocop_desc {
	size_t length;
	struct cryptocop_desc_cfg *cfg;
	struct cryptocop_desc *next;
};


/* Flags for cryptocop_tfrm_cfg */
#define CRYPTOCOP_NO_FLAG     (0x00)
#define CRYPTOCOP_ENCRYPT     (0x01)
#define CRYPTOCOP_DECRYPT     (0x02)
#define CRYPTOCOP_EXPLICIT_IV (0x04)

struct cryptocop_tfrm_cfg {
	cryptocop_tfrm_id tid;

	unsigned int flags; /* DECRYPT, ENCRYPT, EXPLICIT_IV */

	/* CBC initialisation vector for cihers. */
	u8 iv[CRYPTOCOP_MAX_IV_LENGTH];

	/* The position in output where to write the transform output.  The order
	   in which the driver writes the output is unspecified, hence if several
	   transforms write on the same positions in the output the result is
	   unspecified. */
	size_t inject_ix;

	struct cryptocop_tfrm_cfg *next;
};



struct cryptocop_dma_list_operation{
	/* The consumer can provide DMA lists to send to the co-processor.  'use_dmalists' in
	   struct cryptocop_operation must be set for the driver to use them.  outlist,
	   out_data_buf, inlist and in_data_buf must all be physical addresses since they will
	   be loaded to DMA . */
	dma_descr_data *outlist; /* Out from memory to the co-processor. */
	char           *out_data_buf;
	dma_descr_data *inlist; /* In from the co-processor to memory. */
	char           *in_data_buf;

	cryptocop_3des_mode tdes_mode;
	cryptocop_csum_type csum_mode;
};


struct cryptocop_tfrm_operation{
	/* Operation configuration, if not 'use_dmalists' is set. */
	struct cryptocop_tfrm_cfg *tfrm_cfg;
	struct cryptocop_desc *desc;

	struct iovec *indata;
	size_t incount;
	size_t inlen; /* Total inlength. */

	struct iovec *outdata;
	size_t outcount;
	size_t outlen; /* Total outlength. */
};


struct cryptocop_operation {
	cryptocop_callback *cb;
	void *cb_data;

	cryptocop_session_id sid;

	/* The status of the operation when returned to consumer. */
	int operation_status; /* 0, -EAGAIN */

	/* Flags */
	unsigned int use_dmalists:1;  /* Use outlist and inlist instead of the desc/tfrm_cfg configuration. */
	unsigned int in_interrupt:1;  /* Set if inserting job from interrupt context. */
	unsigned int fast_callback:1; /* Set if fast callback wanted, i.e. from interrupt context. */

	union{
		struct cryptocop_dma_list_operation list_op;
		struct cryptocop_tfrm_operation tfrm_op;
	};
};


int cryptocop_new_session(cryptocop_session_id *sid, struct cryptocop_transform_init *tinit, int alloc_flag);
int cryptocop_free_session(cryptocop_session_id sid);

int cryptocop_job_queue_insert_csum(struct cryptocop_operation *operation);

int cryptocop_job_queue_insert_crypto(struct cryptocop_operation *operation);

int cryptocop_job_queue_insert_user_job(struct cryptocop_operation *operation);

#endif /* CRYPTOCOP_H */
