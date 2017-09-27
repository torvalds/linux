/*
 * caam - Freescale FSL CAAM support for Public Key Cryptography descriptors
 *
 * Copyright 2016 Freescale Semiconductor, Inc.
 *
 * There is no Shared Descriptor for PKC so that the Job Descriptor must carry
 * all the desired key parameters, input and output pointers.
 */
#include "caampkc.h"
#include "desc_constr.h"

/* Descriptor for RSA Public operation */
void init_rsa_pub_desc(u32 *desc, struct rsa_pub_pdb *pdb)
{
	init_job_desc_pdb(desc, 0, sizeof(*pdb));
	append_cmd(desc, pdb->sgf);
	append_ptr(desc, pdb->f_dma);
	append_ptr(desc, pdb->g_dma);
	append_ptr(desc, pdb->n_dma);
	append_ptr(desc, pdb->e_dma);
	append_cmd(desc, pdb->f_len);
	append_operation(desc, OP_TYPE_UNI_PROTOCOL | OP_PCLID_RSAENC_PUBKEY);
}

/* Descriptor for RSA Private operation - Private Key Form #1 */
void init_rsa_priv_f1_desc(u32 *desc, struct rsa_priv_f1_pdb *pdb)
{
	init_job_desc_pdb(desc, 0, sizeof(*pdb));
	append_cmd(desc, pdb->sgf);
	append_ptr(desc, pdb->g_dma);
	append_ptr(desc, pdb->f_dma);
	append_ptr(desc, pdb->n_dma);
	append_ptr(desc, pdb->d_dma);
	append_operation(desc, OP_TYPE_UNI_PROTOCOL | OP_PCLID_RSADEC_PRVKEY |
			 RSA_PRIV_KEY_FRM_1);
}

/* Descriptor for RSA Private operation - Private Key Form #2 */
void init_rsa_priv_f2_desc(u32 *desc, struct rsa_priv_f2_pdb *pdb)
{
	init_job_desc_pdb(desc, 0, sizeof(*pdb));
	append_cmd(desc, pdb->sgf);
	append_ptr(desc, pdb->g_dma);
	append_ptr(desc, pdb->f_dma);
	append_ptr(desc, pdb->d_dma);
	append_ptr(desc, pdb->p_dma);
	append_ptr(desc, pdb->q_dma);
	append_ptr(desc, pdb->tmp1_dma);
	append_ptr(desc, pdb->tmp2_dma);
	append_cmd(desc, pdb->p_q_len);
	append_operation(desc, OP_TYPE_UNI_PROTOCOL | OP_PCLID_RSADEC_PRVKEY |
			 RSA_PRIV_KEY_FRM_2);
}

/* Descriptor for RSA Private operation - Private Key Form #3 */
void init_rsa_priv_f3_desc(u32 *desc, struct rsa_priv_f3_pdb *pdb)
{
	init_job_desc_pdb(desc, 0, sizeof(*pdb));
	append_cmd(desc, pdb->sgf);
	append_ptr(desc, pdb->g_dma);
	append_ptr(desc, pdb->f_dma);
	append_ptr(desc, pdb->c_dma);
	append_ptr(desc, pdb->p_dma);
	append_ptr(desc, pdb->q_dma);
	append_ptr(desc, pdb->dp_dma);
	append_ptr(desc, pdb->dq_dma);
	append_ptr(desc, pdb->tmp1_dma);
	append_ptr(desc, pdb->tmp2_dma);
	append_cmd(desc, pdb->p_q_len);
	append_operation(desc, OP_TYPE_UNI_PROTOCOL | OP_PCLID_RSADEC_PRVKEY |
			 RSA_PRIV_KEY_FRM_3);
}
