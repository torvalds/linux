/*
 * Secure Memory / Keystore Exemplification Module
 * Copyright (C) 2015 Freescale Semiconductor, Inc. All Rights Reserved
 *
 * Serves as a functional example, and as a self-contained unit test for
 * the functionality contained in sm_store.c.
 *
 * The example function, caam_sm_example_init(), runs a thread that:
 *
 * - initializes a set of fixed keys
 * - stores one copy in clear buffers
 * - stores them again in secure memory
 * - extracts stored keys back out for use
 * - intializes 3 data buffers for a test:
 *   (1) containing cleartext
 *   (2) to hold ciphertext encrypted with an extracted black key
 *   (3) to hold extracted cleartext decrypted with an equivalent clear key
 *
 * The function then builds simple job descriptors that reference the key
 * material and buffers as initialized, and executes an encryption job
 * with a black key, and a decryption job using a the same key held in the
 * clear. The output of the decryption job is compared to the original
 * cleartext; if they don't compare correctly, one can assume a key problem
 * exists, where the function will exit with an error.
 *
 * This module can use a substantial amount of refactoring, which may occur
 * after the API gets some mileage. Furthermore, expect this module to
 * eventually disappear once the API is integrated into "real" software.
 */

#include "compat.h"
#include "intern.h"
#include "desc.h"
#include "error.h"
#include "jr.h"
#include "sm.h"

static u8 skeymod[] = {
	0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08,
	0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00
};
static u8 symkey[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x05, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f
};

static u8 symdata[] = {
	0x00, 0x01, 0x02, 0x03, 0x04, 0x0f, 0x06, 0x07,
	0x08, 0x09, 0x0a, 0x0b, 0x0c, 0x0d, 0x0e, 0x0f,
	0x10, 0x11, 0x12, 0x13, 0x14, 0x15, 0x16, 0x17,
	0x18, 0x19, 0x1a, 0x1b, 0x1c, 0x1d, 0x1e, 0x1f,
	0x20, 0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27,
	0x28, 0x29, 0x2a, 0x2b, 0x2c, 0x2d, 0x2e, 0x2f,
	0x30, 0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37,
	0x38, 0x39, 0x3a, 0x3b, 0x3c, 0x3d, 0x3e, 0x3f,
	0x40, 0x41, 0x42, 0x43, 0x44, 0x45, 0x46, 0x47,
	0x48, 0x49, 0x4a, 0x4b, 0x4c, 0x4d, 0x4e, 0x4f,
	0x50, 0x51, 0x52, 0x53, 0x54, 0x55, 0x56, 0x57,
	0x58, 0x59, 0x5a, 0x5b, 0x5c, 0x5d, 0x5e, 0x5f,
	0x60, 0x61, 0x62, 0x63, 0x64, 0x65, 0x66, 0x67,
	0x68, 0x69, 0x6a, 0x6b, 0x6c, 0x6d, 0x6e, 0x6f,
	0x70, 0x71, 0x72, 0x73, 0x74, 0x75, 0x76, 0x77,
	0x78, 0x79, 0x7a, 0x7b, 0x7c, 0x7d, 0x7e, 0x7f,
	0x80, 0x81, 0x82, 0x83, 0x84, 0x85, 0x86, 0x87,
	0x88, 0x89, 0x8a, 0x8b, 0x8c, 0x8d, 0x8e, 0x8f,
	0x90, 0x91, 0x92, 0x93, 0x94, 0x95, 0x96, 0x97,
	0x98, 0x99, 0x9a, 0x9b, 0x9c, 0x9d, 0x9e, 0x9f,
	0xa0, 0xa1, 0xa2, 0xa3, 0xa4, 0xa5, 0xa6, 0xa7,
	0xa8, 0xa9, 0xaa, 0xab, 0xac, 0xad, 0xae, 0xaf,
	0xb0, 0xb1, 0xb2, 0xb3, 0xb4, 0xb5, 0xb6, 0xb7,
	0xb8, 0xb9, 0xba, 0xbb, 0xbc, 0xbd, 0xbe, 0xbf,
	0xc0, 0xc1, 0xc2, 0xc3, 0xc4, 0xc5, 0xc6, 0xc7,
	0xc8, 0xc9, 0xca, 0xcb, 0xcc, 0xcd, 0xce, 0xcf,
	0xd0, 0xd1, 0xd2, 0xd3, 0xd4, 0xd5, 0xd6, 0xd7,
	0xd8, 0xd9, 0xda, 0xdb, 0xdc, 0xdd, 0xde, 0xdf,
	0xe0, 0xe1, 0xe2, 0xe3, 0xe4, 0xe5, 0xe6, 0xe7,
	0xe8, 0xe9, 0xea, 0xeb, 0xec, 0xed, 0xee, 0xef,
	0xf0, 0xf1, 0xf2, 0xf3, 0xf4, 0xf5, 0xf6, 0xf7,
	0xf8, 0xf9, 0xfa, 0xfb, 0xfc, 0xfd, 0xfe, 0xff
};

static int mk_job_desc(u32 *desc, dma_addr_t key, u16 keysz, dma_addr_t indata,
		       dma_addr_t outdata, u16 sz, u32 cipherdir, u32 keymode)
{
	desc[1] = CMD_KEY | CLASS_1 | (keysz & KEY_LENGTH_MASK) | keymode;
	desc[2] = (u32)key;
	desc[3] = CMD_OPERATION | OP_TYPE_CLASS1_ALG | OP_ALG_AAI_ECB |
		  cipherdir;
	desc[4] = CMD_FIFO_LOAD | FIFOLD_CLASS_CLASS1 |
		  FIFOLD_TYPE_MSG | FIFOLD_TYPE_LAST1 | sz;
	desc[5] = (u32)indata;
	desc[6] = CMD_FIFO_STORE | FIFOST_TYPE_MESSAGE_DATA | sz;
	desc[7] = (u32)outdata;

	desc[0] = CMD_DESC_HDR | HDR_ONE | (8 & HDR_DESCLEN_MASK);
	return 8 * sizeof(u32);
}

struct exec_test_result {
	int error;
	struct completion completion;
};

void exec_test_done(struct device *dev, u32 *desc, u32 err, void *context)
{
	struct exec_test_result *res = context;

	if (err) {
		caam_jr_strstatus(dev, err);
	}

	res->error = err;
	complete(&res->completion);
}

static int exec_test_job(struct device *ksdev, u32 *jobdesc)
{
	struct exec_test_result testres;
	struct caam_drv_private_sm *kspriv;
	int rtn = 0;

	kspriv = dev_get_drvdata(ksdev);

	init_completion(&testres.completion);

	rtn = caam_jr_enqueue(kspriv->smringdev, jobdesc, exec_test_done,
			      &testres);
	if (!rtn) {
		wait_for_completion_interruptible(&testres.completion);
		rtn = testres.error;
	}
	return rtn;
}


int caam_sm_example_init(struct platform_device *pdev)
{
	struct device *ctrldev, *ksdev;
	struct caam_drv_private *ctrlpriv;
	struct caam_drv_private_sm *kspriv;
	u32 unit, units, jdescsz;
	int stat, jstat, rtnval = 0;
	u8 __iomem *syminp, *symint, *symout = NULL;
	dma_addr_t syminp_dma, symint_dma, symout_dma;
	u8 __iomem *black_key_des, *black_key_aes128;
	u8 __iomem  *black_key_aes256;
	dma_addr_t black_key_des_dma, black_key_aes128_dma;
	dma_addr_t black_key_aes256_dma;
	u8 __iomem *clear_key_des, *clear_key_aes128, *clear_key_aes256;
	dma_addr_t clear_key_des_dma, clear_key_aes128_dma;
	dma_addr_t clear_key_aes256_dma;
	u32 __iomem *jdesc;
	u32 keyslot_des, keyslot_aes128, keyslot_aes256 = 0;

	jdesc = NULL;
	black_key_des = black_key_aes128 = black_key_aes256 = NULL;
	clear_key_des = clear_key_aes128 = clear_key_aes256 = NULL;

	/* We can lose this cruft once we can get a pdev by name */
	ctrldev = &pdev->dev;
	ctrlpriv = dev_get_drvdata(ctrldev);
	ksdev = ctrlpriv->smdev;
	kspriv = dev_get_drvdata(ksdev);
	if (kspriv == NULL)
		return -ENODEV;

	/* Now that we have the dev for the single SM instance, connect */
#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "caam_sm_test_init() running\n");
#endif
	/* Probe to see what keystores are available to us */
	units = sm_detect_keystore_units(ksdev);
	if (!units)
		dev_err(ksdev, "caam_sm_test: no keystore units available\n");

	/*
	 * MX6 bootloader stores some stuff in unit 0, so let's
	 * use 1 or above
	 */
	if (units < 2) {
		dev_err(ksdev, "caam_sm_test: insufficient keystore units\n");
		return -ENODEV;
	}
	unit = 1;

#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "caam_sm_test: %d keystore units available\n", units);
#endif

	/* Initialize/Establish Keystore */
	sm_establish_keystore(ksdev, unit);	/* Initalize store in #1 */

	/*
	 * Top of main test thread
	 */

	/* Allocate test data blocks (input, intermediate, output) */
	syminp = kmalloc(256, GFP_KERNEL | GFP_DMA);
	symint = kmalloc(256, GFP_KERNEL | GFP_DMA);
	symout = kmalloc(256, GFP_KERNEL | GFP_DMA);
	if ((syminp == NULL) || (symint == NULL) || (symout == NULL)) {
		rtnval = -ENOMEM;
		dev_err(ksdev, "caam_sm_test: can't get test data buffers\n");
		goto freemem;
	}

	/* Allocate storage for 3 black keys: encapsulated 8, 16, 32 */
	black_key_des = kmalloc(16, GFP_KERNEL | GFP_DMA); /* padded to 16... */
	black_key_aes128 = kmalloc(16, GFP_KERNEL | GFP_DMA);
	black_key_aes256 = kmalloc(16, GFP_KERNEL | GFP_DMA);
	if ((black_key_des == NULL) || (black_key_aes128 == NULL) ||
	    (black_key_aes256 == NULL)) {
		rtnval = -ENOMEM;
		dev_err(ksdev, "caam_sm_test: can't black key buffers\n");
		goto freemem;
	}

	clear_key_des = kmalloc(8, GFP_KERNEL | GFP_DMA);
	clear_key_aes128 = kmalloc(16, GFP_KERNEL | GFP_DMA);
	clear_key_aes256 = kmalloc(32, GFP_KERNEL | GFP_DMA);
	if ((clear_key_des == NULL) || (clear_key_aes128 == NULL) ||
	    (clear_key_aes256 == NULL)) {
		rtnval = -ENOMEM;
		dev_err(ksdev, "caam_sm_test: can't get clear key buffers\n");
		goto freemem;
	}

	/* Allocate storage for job descriptor */
	jdesc = kmalloc(8 * sizeof(u32), GFP_KERNEL | GFP_DMA);
	if (jdesc == NULL) {
		rtnval = -ENOMEM;
		dev_err(ksdev, "caam_sm_test: can't get descriptor buffers\n");
		goto freemem;
	}

#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "caam_sm_test: all buffers allocated\n");
#endif

	/* Load up input data block, clear outputs */
	memcpy(syminp, symdata, 256);
	memset(symint, 0, 256);
	memset(symout, 0, 256);
#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 syminp[0], syminp[1], syminp[2], syminp[3],
		 syminp[4], syminp[5], syminp[6], syminp[7]);
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symint[0], symint[1], symint[2], symint[3],
		 symint[4], symint[5], symint[6], symint[7]);
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symout[0], symout[1], symout[2], symout[3],
		 symout[4], symout[5], symout[6], symout[7]);

	dev_info(ksdev, "caam_sm_test: data buffers initialized\n");
#endif

	/* Load up clear keys */
	memcpy(clear_key_des, symkey, 8);
	memcpy(clear_key_aes128, symkey, 16);
	memcpy(clear_key_aes256, symkey, 32);

#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "caam_sm_test: all clear keys loaded\n");
#endif

	/*
	 * Place clear keys in keystore.
	 * All the interesting stuff happens here.
	 */
	/* 8 bit DES key */
	stat = sm_keystore_slot_alloc(ksdev, unit, 8, &keyslot_des);
	if (stat)
		goto freemem;
#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "caam_sm_test: 8 byte key slot in %d\n", keyslot_des);
#endif
	stat = sm_keystore_slot_load(ksdev, unit, keyslot_des, clear_key_des,
				     8);
	if (stat) {
#ifdef SM_TEST_DETAIL
		dev_info(ksdev, "caam_sm_test: can't load 8 byte key in %d\n",
			 keyslot_des);
#endif
		sm_keystore_slot_dealloc(ksdev, unit, keyslot_des);
		goto freemem;
	}

	/* 16 bit AES key */
	stat = sm_keystore_slot_alloc(ksdev, unit, 16, &keyslot_aes128);
	if (stat) {
		sm_keystore_slot_dealloc(ksdev, unit, keyslot_des);
		goto freemem;
	}
#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "caam_sm_test: 16 byte key slot in %d\n",
		 keyslot_aes128);
#endif
	stat = sm_keystore_slot_load(ksdev, unit, keyslot_aes128,
				     clear_key_aes128, 16);
	if (stat) {
#ifdef SM_TEST_DETAIL
		dev_info(ksdev, "caam_sm_test: can't load 16 byte key in %d\n",
			 keyslot_aes128);
#endif
		sm_keystore_slot_dealloc(ksdev, unit, keyslot_aes128);
		sm_keystore_slot_dealloc(ksdev, unit, keyslot_des);
		goto freemem;
	}

	/* 32 bit AES key */
	stat = sm_keystore_slot_alloc(ksdev, unit, 32, &keyslot_aes256);
	if (stat) {
		sm_keystore_slot_dealloc(ksdev, unit, keyslot_aes128);
		sm_keystore_slot_dealloc(ksdev, unit, keyslot_des);
		goto freemem;
	}
#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "caam_sm_test: 32 byte key slot in %d\n",
		 keyslot_aes256);
#endif
	stat = sm_keystore_slot_load(ksdev, unit, keyslot_aes256,
				     clear_key_aes256, 32);
	if (stat) {
#ifdef SM_TEST_DETAIL
		dev_info(ksdev, "caam_sm_test: can't load 32 byte key in %d\n",
			 keyslot_aes128);
#endif
		sm_keystore_slot_dealloc(ksdev, unit, keyslot_aes256);
		sm_keystore_slot_dealloc(ksdev, unit, keyslot_aes128);
		sm_keystore_slot_dealloc(ksdev, unit, keyslot_des);
		goto freemem;
	}

	/* Encapsulate all keys as SM blobs */
	stat = sm_keystore_slot_encapsulate(ksdev, unit, keyslot_des,
					    keyslot_des, 8, skeymod, 8);
	if (stat) {
		dev_info(ksdev, "caam_sm_test: can't encapsulate DES key\n");
		goto freekeys;
	}

	stat = sm_keystore_slot_encapsulate(ksdev, unit, keyslot_aes128,
					    keyslot_aes128, 16, skeymod, 8);
	if (stat) {
		dev_info(ksdev, "caam_sm_test: can't encapsulate AES128 key\n");
		goto freekeys;
	}

	stat = sm_keystore_slot_encapsulate(ksdev, unit, keyslot_aes256,
					    keyslot_aes256, 32, skeymod, 8);
	if (stat) {
		dev_info(ksdev, "caam_sm_test: can't encapsulate AES256 key\n");
		goto freekeys;
	}

	/* Now decapsulate as black key blobs */
	stat = sm_keystore_slot_decapsulate(ksdev, unit, keyslot_des,
					    keyslot_des, 8, skeymod, 8);
	if (stat) {
		dev_info(ksdev, "caam_sm_test: can't decapsulate DES key\n");
		goto freekeys;
	}

	stat = sm_keystore_slot_decapsulate(ksdev, unit, keyslot_aes128,
					    keyslot_aes128, 16, skeymod, 8);
	if (stat) {
		dev_info(ksdev, "caam_sm_test: can't decapsulate AES128 key\n");
		goto freekeys;
	}

	stat = sm_keystore_slot_decapsulate(ksdev, unit, keyslot_aes256,
					    keyslot_aes256, 32, skeymod, 8);
	if (stat) {
		dev_info(ksdev, "caam_sm_test: can't decapsulate AES128 key\n");
		goto freekeys;
	}

	/* Extract 8/16/32 byte black keys */
	sm_keystore_slot_read(ksdev, unit, keyslot_des, 8, black_key_des);
	sm_keystore_slot_read(ksdev, unit, keyslot_aes128, 16,
			      black_key_aes128);
	sm_keystore_slot_read(ksdev, unit, keyslot_aes256, 32,
			      black_key_aes256);

#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "caam_sm_test: all black keys extracted\n");
#endif

	/* DES encrypt using 8 byte black key */
	black_key_des_dma = dma_map_single(ksdev, black_key_des, 8,
					   DMA_TO_DEVICE);
	dma_sync_single_for_device(ksdev, black_key_des_dma, 8, DMA_TO_DEVICE);
	syminp_dma = dma_map_single(ksdev, syminp, 256, DMA_TO_DEVICE);
	dma_sync_single_for_device(ksdev, syminp_dma, 256, DMA_TO_DEVICE);
	symint_dma = dma_map_single(ksdev, symint, 256, DMA_FROM_DEVICE);

	jdescsz = mk_job_desc(jdesc, black_key_des_dma, 8, syminp_dma,
			      symint_dma, 256,
			      OP_ALG_ENCRYPT | OP_ALG_ALGSEL_DES, 0);

#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "jobdesc:\n");
	dev_info(ksdev, "0x%08x\n", jdesc[0]);
	dev_info(ksdev, "0x%08x\n", jdesc[1]);
	dev_info(ksdev, "0x%08x\n", jdesc[2]);
	dev_info(ksdev, "0x%08x\n", jdesc[3]);
	dev_info(ksdev, "0x%08x\n", jdesc[4]);
	dev_info(ksdev, "0x%08x\n", jdesc[5]);
	dev_info(ksdev, "0x%08x\n", jdesc[6]);
	dev_info(ksdev, "0x%08x\n", jdesc[7]);
#endif

	jstat = exec_test_job(ksdev, jdesc);

	dma_sync_single_for_cpu(ksdev, symint_dma, 256, DMA_FROM_DEVICE);
	dma_unmap_single(ksdev, symint_dma, 256, DMA_FROM_DEVICE);
	dma_unmap_single(ksdev, syminp_dma, 256, DMA_TO_DEVICE);
	dma_unmap_single(ksdev, black_key_des_dma, 8, DMA_TO_DEVICE);

#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "input block:\n");
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 syminp[0], syminp[1], syminp[2], syminp[3],
		 syminp[4], syminp[5], syminp[6], syminp[7]);
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 syminp[8], syminp[9], syminp[10], syminp[11],
		 syminp[12], syminp[13], syminp[14], syminp[15]);
	dev_info(ksdev, "intermediate block:\n");
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symint[0], symint[1], symint[2], symint[3],
		 symint[4], symint[5], symint[6], symint[7]);
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symint[8], symint[9], symint[10], symint[11],
		 symint[12], symint[13], symint[14], symint[15]);
	dev_info(ksdev, "caam_sm_test: encrypt cycle with 8 byte key\n");
#endif

	/* DES decrypt using 8 byte clear key */
	clear_key_des_dma = dma_map_single(ksdev, clear_key_des, 8,
					   DMA_TO_DEVICE);
	dma_sync_single_for_device(ksdev, clear_key_des_dma, 8, DMA_TO_DEVICE);
	symint_dma = dma_map_single(ksdev, symint, 256, DMA_TO_DEVICE);
	dma_sync_single_for_device(ksdev, symint_dma, 256, DMA_TO_DEVICE);
	symout_dma = dma_map_single(ksdev, symout, 256, DMA_FROM_DEVICE);

	jdescsz = mk_job_desc(jdesc, clear_key_des_dma, 8, symint_dma,
			      symout_dma, 256,
			      OP_ALG_DECRYPT | OP_ALG_ALGSEL_DES, 0);

#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "jobdesc:\n");
	dev_info(ksdev, "0x%08x\n", jdesc[0]);
	dev_info(ksdev, "0x%08x\n", jdesc[1]);
	dev_info(ksdev, "0x%08x\n", jdesc[2]);
	dev_info(ksdev, "0x%08x\n", jdesc[3]);
	dev_info(ksdev, "0x%08x\n", jdesc[4]);
	dev_info(ksdev, "0x%08x\n", jdesc[5]);
	dev_info(ksdev, "0x%08x\n", jdesc[6]);
	dev_info(ksdev, "0x%08x\n", jdesc[7]);
#endif

	jstat = exec_test_job(ksdev, jdesc);

	dma_sync_single_for_cpu(ksdev, symout_dma, 256, DMA_FROM_DEVICE);
	dma_unmap_single(ksdev, symout_dma, 256, DMA_FROM_DEVICE);
	dma_unmap_single(ksdev, symint_dma, 256, DMA_TO_DEVICE);
	dma_unmap_single(ksdev, clear_key_des_dma, 8, DMA_TO_DEVICE);

#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "intermediate block:\n");
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symint[0], symint[1], symint[2], symint[3],
		 symint[4], symint[5], symint[6], symint[7]);
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symint[8], symint[9], symint[10], symint[11],
		 symint[12], symint[13], symint[14], symint[15]);
	dev_info(ksdev, "decrypted block:\n");
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symout[0], symout[1], symout[2], symout[3],
		 symout[4], symout[5], symout[6], symout[7]);
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symout[8], symout[9], symout[10], symout[11],
		 symout[12], symout[13], symout[14], symout[15]);
	dev_info(ksdev, "caam_sm_test: decrypt cycle with 8 byte key\n");
#endif

	/* Check result */
	if (memcmp(symout, syminp, 256)) {
		dev_info(ksdev, "caam_sm_test: 8-byte key test mismatch\n");
		rtnval = -1;
		goto freekeys;
	} else
		dev_info(ksdev, "caam_sm_test: 8-byte key test match OK\n");

	/* AES-128 encrypt using 16 byte black key */
	black_key_aes128_dma = dma_map_single(ksdev, black_key_aes128, 16,
					      DMA_TO_DEVICE);
	dma_sync_single_for_device(ksdev, black_key_aes128_dma, 16,
				   DMA_TO_DEVICE);
	syminp_dma = dma_map_single(ksdev, syminp, 256, DMA_TO_DEVICE);
	dma_sync_single_for_device(ksdev, syminp_dma, 256, DMA_TO_DEVICE);
	symint_dma = dma_map_single(ksdev, symint, 256, DMA_FROM_DEVICE);

	jdescsz = mk_job_desc(jdesc, black_key_aes128_dma, 16, syminp_dma,
			      symint_dma, 256,
			      OP_ALG_ENCRYPT | OP_ALG_ALGSEL_AES, 0);

#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "jobdesc:\n");
	dev_info(ksdev, "0x%08x\n", jdesc[0]);
	dev_info(ksdev, "0x%08x\n", jdesc[1]);
	dev_info(ksdev, "0x%08x\n", jdesc[2]);
	dev_info(ksdev, "0x%08x\n", jdesc[3]);
	dev_info(ksdev, "0x%08x\n", jdesc[4]);
	dev_info(ksdev, "0x%08x\n", jdesc[5]);
	dev_info(ksdev, "0x%08x\n", jdesc[6]);
	dev_info(ksdev, "0x%08x\n", jdesc[7]);
#endif

	jstat = exec_test_job(ksdev, jdesc);

	dma_sync_single_for_cpu(ksdev, symint_dma, 256, DMA_FROM_DEVICE);
	dma_unmap_single(ksdev, symint_dma, 256, DMA_FROM_DEVICE);
	dma_unmap_single(ksdev, syminp_dma, 256, DMA_TO_DEVICE);
	dma_unmap_single(ksdev, black_key_aes128_dma, 16, DMA_TO_DEVICE);

#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "input block:\n");
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 syminp[0], syminp[1], syminp[2], syminp[3],
		 syminp[4], syminp[5], syminp[6], syminp[7]);
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 syminp[8], syminp[9], syminp[10], syminp[11],
		 syminp[12], syminp[13], syminp[14], syminp[15]);
	dev_info(ksdev, "intermediate block:\n");
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symint[0], symint[1], symint[2], symint[3],
		 symint[4], symint[5], symint[6], symint[7]);
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symint[8], symint[9], symint[10], symint[11],
		 symint[12], symint[13], symint[14], symint[15]);
	dev_info(ksdev, "caam_sm_test: encrypt cycle with 16 byte key\n");
#endif

	/* AES-128 decrypt using 16 byte clear key */
	clear_key_aes128_dma = dma_map_single(ksdev, clear_key_aes128, 16,
					      DMA_TO_DEVICE);
	dma_sync_single_for_device(ksdev, clear_key_aes128_dma, 16,
				   DMA_TO_DEVICE);
	symint_dma = dma_map_single(ksdev, symint, 256, DMA_TO_DEVICE);
	dma_sync_single_for_device(ksdev, symint_dma, 256, DMA_TO_DEVICE);
	symout_dma = dma_map_single(ksdev, symout, 256, DMA_FROM_DEVICE);

	jdescsz = mk_job_desc(jdesc, clear_key_aes128_dma, 16, symint_dma,
			      symout_dma, 256,
			      OP_ALG_DECRYPT | OP_ALG_ALGSEL_AES, 0);

#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "jobdesc:\n");
	dev_info(ksdev, "0x%08x\n", jdesc[0]);
	dev_info(ksdev, "0x%08x\n", jdesc[1]);
	dev_info(ksdev, "0x%08x\n", jdesc[2]);
	dev_info(ksdev, "0x%08x\n", jdesc[3]);
	dev_info(ksdev, "0x%08x\n", jdesc[4]);
	dev_info(ksdev, "0x%08x\n", jdesc[5]);
	dev_info(ksdev, "0x%08x\n", jdesc[6]);
	dev_info(ksdev, "0x%08x\n", jdesc[7]);
#endif
	jstat = exec_test_job(ksdev, jdesc);

	dma_sync_single_for_cpu(ksdev, symout_dma, 256, DMA_FROM_DEVICE);
	dma_unmap_single(ksdev, symout_dma, 256, DMA_FROM_DEVICE);
	dma_unmap_single(ksdev, symint_dma, 256, DMA_TO_DEVICE);
	dma_unmap_single(ksdev, clear_key_aes128_dma, 16, DMA_TO_DEVICE);

#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "intermediate block:\n");
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symint[0], symint[1], symint[2], symint[3],
		 symint[4], symint[5], symint[6], symint[7]);
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symint[8], symint[9], symint[10], symint[11],
		 symint[12], symint[13], symint[14], symint[15]);
	dev_info(ksdev, "decrypted block:\n");
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symout[0], symout[1], symout[2], symout[3],
		 symout[4], symout[5], symout[6], symout[7]);
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symout[8], symout[9], symout[10], symout[11],
		 symout[12], symout[13], symout[14], symout[15]);
	dev_info(ksdev, "caam_sm_test: decrypt cycle with 16 byte key\n");
#endif

	/* Check result */
	if (memcmp(symout, syminp, 256)) {
		dev_info(ksdev, "caam_sm_test: 16-byte key test mismatch\n");
		rtnval = -1;
		goto freekeys;
	} else
		dev_info(ksdev, "caam_sm_test: 16-byte key test match OK\n");

	/* AES-256 encrypt using 32 byte black key */
	black_key_aes256_dma = dma_map_single(ksdev, black_key_aes256, 32,
					      DMA_TO_DEVICE);
	dma_sync_single_for_device(ksdev, black_key_aes256_dma, 32,
				   DMA_TO_DEVICE);
	syminp_dma = dma_map_single(ksdev, syminp, 256, DMA_TO_DEVICE);
	dma_sync_single_for_device(ksdev, syminp_dma, 256, DMA_TO_DEVICE);
	symint_dma = dma_map_single(ksdev, symint, 256, DMA_FROM_DEVICE);

	jdescsz = mk_job_desc(jdesc, black_key_aes256_dma, 32, syminp_dma,
			      symint_dma, 256,
			      OP_ALG_ENCRYPT | OP_ALG_ALGSEL_AES, 0);

#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "jobdesc:\n");
	dev_info(ksdev, "0x%08x\n", jdesc[0]);
	dev_info(ksdev, "0x%08x\n", jdesc[1]);
	dev_info(ksdev, "0x%08x\n", jdesc[2]);
	dev_info(ksdev, "0x%08x\n", jdesc[3]);
	dev_info(ksdev, "0x%08x\n", jdesc[4]);
	dev_info(ksdev, "0x%08x\n", jdesc[5]);
	dev_info(ksdev, "0x%08x\n", jdesc[6]);
	dev_info(ksdev, "0x%08x\n", jdesc[7]);
#endif

	jstat = exec_test_job(ksdev, jdesc);

	dma_sync_single_for_cpu(ksdev, symint_dma, 256, DMA_FROM_DEVICE);
	dma_unmap_single(ksdev, symint_dma, 256, DMA_FROM_DEVICE);
	dma_unmap_single(ksdev, syminp_dma, 256, DMA_TO_DEVICE);
	dma_unmap_single(ksdev, black_key_aes256_dma, 32, DMA_TO_DEVICE);

#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "input block:\n");
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 syminp[0], syminp[1], syminp[2], syminp[3],
		 syminp[4], syminp[5], syminp[6], syminp[7]);
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 syminp[8], syminp[9], syminp[10], syminp[11],
		 syminp[12], syminp[13], syminp[14], syminp[15]);
	dev_info(ksdev, "intermediate block:\n");
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symint[0], symint[1], symint[2], symint[3],
		 symint[4], symint[5], symint[6], symint[7]);
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symint[8], symint[9], symint[10], symint[11],
		 symint[12], symint[13], symint[14], symint[15]);
	dev_info(ksdev, "caam_sm_test: encrypt cycle with 32 byte key\n");
#endif

	/* AES-256 decrypt using 32-byte black key */
	clear_key_aes256_dma = dma_map_single(ksdev, clear_key_aes256, 32,
					      DMA_TO_DEVICE);
	dma_sync_single_for_device(ksdev, clear_key_aes256_dma, 32,
				   DMA_TO_DEVICE);
	symint_dma = dma_map_single(ksdev, symint, 256, DMA_TO_DEVICE);
	dma_sync_single_for_device(ksdev, symint_dma, 256, DMA_TO_DEVICE);
	symout_dma = dma_map_single(ksdev, symout, 256, DMA_FROM_DEVICE);

	jdescsz = mk_job_desc(jdesc, clear_key_aes256_dma, 32, symint_dma,
			      symout_dma, 256,
			      OP_ALG_DECRYPT | OP_ALG_ALGSEL_AES, 0);

#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "jobdesc:\n");
	dev_info(ksdev, "0x%08x\n", jdesc[0]);
	dev_info(ksdev, "0x%08x\n", jdesc[1]);
	dev_info(ksdev, "0x%08x\n", jdesc[2]);
	dev_info(ksdev, "0x%08x\n", jdesc[3]);
	dev_info(ksdev, "0x%08x\n", jdesc[4]);
	dev_info(ksdev, "0x%08x\n", jdesc[5]);
	dev_info(ksdev, "0x%08x\n", jdesc[6]);
	dev_info(ksdev, "0x%08x\n", jdesc[7]);
#endif

	jstat = exec_test_job(ksdev, jdesc);

	dma_sync_single_for_cpu(ksdev, symout_dma, 256, DMA_FROM_DEVICE);
	dma_unmap_single(ksdev, symout_dma, 256, DMA_FROM_DEVICE);
	dma_unmap_single(ksdev, symint_dma, 256, DMA_TO_DEVICE);
	dma_unmap_single(ksdev, clear_key_aes256_dma, 32, DMA_TO_DEVICE);

#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "intermediate block:\n");
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symint[0], symint[1], symint[2], symint[3],
		 symint[4], symint[5], symint[6], symint[7]);
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symint[8], symint[9], symint[10], symint[11],
		 symint[12], symint[13], symint[14], symint[15]);
	dev_info(ksdev, "decrypted block:\n");
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symout[0], symout[1], symout[2], symout[3],
		 symout[4], symout[5], symout[6], symout[7]);
	dev_info(ksdev, "0x%02x 0x%02x 0x%02x 0x%02x " \
			"0x%02x 0x%02x 0x%02x 0x%02x\n",
		 symout[8], symout[9], symout[10], symout[11],
		 symout[12], symout[13], symout[14], symout[15]);
	dev_info(ksdev, "caam_sm_test: decrypt cycle with 32 byte key\n");
#endif

	/* Check result */
	if (memcmp(symout, syminp, 256)) {
		dev_info(ksdev, "caam_sm_test: 32-byte key test mismatch\n");
		rtnval = -1;
		goto freekeys;
	} else
		dev_info(ksdev, "caam_sm_test: 32-byte key test match OK\n");


	/* Remove 8/16/32 byte keys from keystore */
freekeys:
	stat = sm_keystore_slot_dealloc(ksdev, unit, keyslot_des);
	if (stat)
		dev_info(ksdev, "caam_sm_test: can't release slot %d\n",
			 keyslot_des);

	stat = sm_keystore_slot_dealloc(ksdev, unit, keyslot_aes128);
	if (stat)
		dev_info(ksdev, "caam_sm_test: can't release slot %d\n",
			 keyslot_aes128);

	stat = sm_keystore_slot_dealloc(ksdev, unit, keyslot_aes256);
	if (stat)
		dev_info(ksdev, "caam_sm_test: can't release slot %d\n",
			 keyslot_aes256);


	/* Free resources */
freemem:
#ifdef SM_TEST_DETAIL
	dev_info(ksdev, "caam_sm_test: cleaning up\n");
#endif
	kfree(syminp);
	kfree(symint);
	kfree(symout);
	kfree(clear_key_des);
	kfree(clear_key_aes128);
	kfree(clear_key_aes256);
	kfree(black_key_des);
	kfree(black_key_aes128);
	kfree(black_key_aes256);
	kfree(jdesc);

	/* Disconnect from keystore and leave */
	sm_release_keystore(ksdev, unit);

	return rtnval;
}
EXPORT_SYMBOL(caam_sm_example_init);

void caam_sm_example_shutdown(void)
{
	/* unused in present version */
	struct device_node *dev_node;
	struct platform_device *pdev;

	/*
	 * Do of_find_compatible_node() then of_find_device_by_node()
	 * once a functional device tree is available
	 */
	dev_node = of_find_compatible_node(NULL, NULL, "fsl,sec-v4.0");
	if (!dev_node) {
		dev_node = of_find_compatible_node(NULL, NULL, "fsl,sec4.0");
		if (!dev_node)
			return;
	}

	pdev = of_find_device_by_node(dev_node);
	if (!pdev)
		return;

	of_node_get(dev_node);

}

static int __init caam_sm_test_init(void)
{
	struct device_node *dev_node;
	struct platform_device *pdev;

	/*
	 * Do of_find_compatible_node() then of_find_device_by_node()
	 * once a functional device tree is available
	 */
	dev_node = of_find_compatible_node(NULL, NULL, "fsl,sec-v4.0");
	if (!dev_node) {
		dev_node = of_find_compatible_node(NULL, NULL, "fsl,sec4.0");
		if (!dev_node)
			return -ENODEV;
	}

	pdev = of_find_device_by_node(dev_node);
	if (!pdev)
		return -ENODEV;

	of_node_put(dev_node);

	caam_sm_example_init(pdev);

	return 0;
}


/* Module-based initialization needs to wait for dev tree */
#ifdef CONFIG_OF
module_init(caam_sm_test_init);
module_exit(caam_sm_example_shutdown);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_DESCRIPTION("FSL CAAM Keystore Usage Example");
MODULE_AUTHOR("Freescale Semiconductor - NMSG/MAD");
#endif
