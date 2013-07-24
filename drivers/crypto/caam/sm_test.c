
/*
 * Secure Memory / Keystore Exemplification Module
 * Copyright (C) 2012-2015 Freescale Semiconductor, Inc. All Rights Reserved
 *
 * This module has been overloaded as an example to show:
 * - Secure memory subsystem initialization/shutdown
 * - Allocation/deallocation of "slots" in a secure memory page
 * - Loading and unloading of key material into slots
 * - Covering of secure memory objects into "black keys" (ECB only at present)
 * - Verification of key covering (by differentiation only)
 * - Exportation of keys into secure memory blobs (with display of result)
 * - Importation of keys from secure memory blobs (with display of result)
 * - Verification of re-imported keys where possible.
 *
 * The module does not show the use of key objects as working key register
 * source material at this time.
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

/* Fixed known pattern for a key modifier */
static u8 skeymod[] = {
	0x0f, 0x0e, 0x0d, 0x0c, 0x0b, 0x0a, 0x09, 0x08,
	0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, 0x00
};

/* Fixed known pattern for a key */
static u8 clrkey[] = {
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

static void key_display(struct device *dev, u8 *label, u16 size, u8 *key)
{
	unsigned i;

	dev_info(dev, label);
	for (i = 0; i < size; i += 8)
		dev_info(dev,
			 "[%04d] %02x %02x %02x %02x %02x %02x %02x %02x\n",
			 i, key[i], key[i + 1], key[i + 2], key[i + 3],
			 key[i + 4], key[i + 5], key[i + 6], key[i + 7]);
}

int caam_sm_example_init(struct platform_device *pdev)
{
	struct device *ctrldev, *ksdev;
	struct caam_drv_private *ctrlpriv;
	struct caam_drv_private_sm *kspriv;
	u32 unit, units;
	int rtnval = 0;
	u8 clrkey8[8], clrkey16[16], clrkey24[24], clrkey32[32];
	u8 blkkey8[16], blkkey16[16], blkkey24[24], blkkey32[32];
	u8 rstkey8[16], rstkey16[16], rstkey24[24], rstkey32[32];
	u8 __iomem *blob8, *blob16, *blob24, *blob32;
	u32 keyslot8, keyslot16, keyslot24, keyslot32 = 0;

	blob8 = blob16 = blob24 = blob32 = NULL;

	/*
	 * 3.5.x and later revs for MX6 should be able to ditch this
	 * and detect via dts property
	 */
	ctrldev = &pdev->dev;
	ctrlpriv = dev_get_drvdata(ctrldev);
	ksdev = ctrlpriv->smdev;
	kspriv = dev_get_drvdata(ksdev);
	if (kspriv == NULL)
		return -ENODEV;

	/* What keystores are available ? */
	units = sm_detect_keystore_units(ksdev);
	if (!units)
		dev_err(ksdev, "blkkey_ex: no keystore units available\n");

	/*
	 * MX6 bootloader stores some stuff in unit 0, so let's
	 * use 1 or above
	 */
	if (units < 2) {
		dev_err(ksdev, "blkkey_ex: insufficient keystore units\n");
		return -ENODEV;
	}
	unit = 1;

	dev_info(ksdev, "blkkey_ex: %d keystore units available\n", units);

	/* Initialize/Establish Keystore */
	sm_establish_keystore(ksdev, unit);	/* Initalize store in #1 */

	/*
	 * Now let's set up buffers for blobs in DMA-able memory. All are
	 * larger than need to be so that blob size can be seen.
	 */
	blob8 = kzalloc(128, GFP_KERNEL | GFP_DMA);
	blob16 = kzalloc(128, GFP_KERNEL | GFP_DMA);
	blob24 = kzalloc(128, GFP_KERNEL | GFP_DMA);
	blob32 = kzalloc(128, GFP_KERNEL | GFP_DMA);

	if ((blob8 == NULL) || (blob16 == NULL) || (blob24 == NULL) ||
	    (blob32 == NULL)) {
		rtnval = -ENOMEM;
		dev_err(ksdev, "blkkey_ex: can't get blob buffers\n");
		goto freemem;
	}

	/* Initialize clear keys with a known and recognizable pattern */
	memcpy(clrkey8, clrkey, 8);
	memcpy(clrkey16, clrkey, 16);
	memcpy(clrkey24, clrkey, 24);
	memcpy(clrkey32, clrkey, 32);

	memset(blkkey8, 0, AES_BLOCK_PAD(8));
	memset(blkkey16, 0, AES_BLOCK_PAD(16));
	memset(blkkey24, 0, AES_BLOCK_PAD(24));
	memset(blkkey32, 0, AES_BLOCK_PAD(32));

	memset(rstkey8, 0, AES_BLOCK_PAD(8));
	memset(rstkey16, 0, AES_BLOCK_PAD(16));
	memset(rstkey24, 0, AES_BLOCK_PAD(24));
	memset(rstkey32, 0, AES_BLOCK_PAD(32));

	/*
	 * Allocate keyslots. Since we're going to blacken keys in-place,
	 * we want slots big enough to pad out to the next larger AES blocksize
	 * so pad them out.
	 */
	if (sm_keystore_slot_alloc(ksdev, unit, AES_BLOCK_PAD(8), &keyslot8))
		goto dealloc;

	if (sm_keystore_slot_alloc(ksdev, unit, AES_BLOCK_PAD(16), &keyslot16))
		goto dealloc;

	if (sm_keystore_slot_alloc(ksdev, unit, AES_BLOCK_PAD(24), &keyslot24))
		goto dealloc;

	if (sm_keystore_slot_alloc(ksdev, unit, AES_BLOCK_PAD(32), &keyslot32))
		goto dealloc;


	/* Now load clear key data into the newly allocated slots */
	if (sm_keystore_slot_load(ksdev, unit, keyslot8, clrkey8, 8))
		goto dealloc;

	if (sm_keystore_slot_load(ksdev, unit, keyslot16, clrkey16, 16))
		goto dealloc;

	if (sm_keystore_slot_load(ksdev, unit, keyslot24, clrkey24, 24))
		goto dealloc;

	if (sm_keystore_slot_load(ksdev, unit, keyslot32, clrkey32, 32))
		goto dealloc;

	/*
	 * All cleartext keys are loaded into slots (in an unprotected
	 * partition at this time)
	 *
	 * Cover keys in-place
	 */
	if (sm_keystore_cover_key(ksdev, unit, keyslot8, 8, KEY_COVER_ECB)) {
		dev_info(ksdev, "blkkey_ex: can't cover 64-bit key\n");
		goto dealloc;
	}

	if (sm_keystore_cover_key(ksdev, unit, keyslot16, 16, KEY_COVER_ECB)) {
		dev_info(ksdev, "blkkey_ex: can't cover 128-bit key\n");
		goto dealloc;
	}

	if (sm_keystore_cover_key(ksdev, unit, keyslot24, 24, KEY_COVER_ECB)) {
		dev_info(ksdev, "blkkey_ex: can't cover 192-bit key\n");
		goto dealloc;
	}

	if (sm_keystore_cover_key(ksdev, unit, keyslot32, 32, KEY_COVER_ECB)) {
		dev_info(ksdev, "blkkey_ex: can't cover 256-bit key\n");
		goto dealloc;
	}

	/*
	 * Keys should be covered and appear sufficiently "random"
	 * as a result of the covering (blackening) process. Assuming
	 * non-secure mode, read them back out for examination; they should
	 * appear as random data, completely differing from the clear
	 * inputs. So, this will read them back from secure memory and
	 * compare them. If they match the clear key, then the covering
	 * operation didn't occur.
	 */

	if (sm_keystore_slot_read(ksdev, unit, keyslot8, AES_BLOCK_PAD(8),
				  blkkey8)) {
		dev_info(ksdev, "blkkey_ex: can't read 64-bit black key\n");
		goto dealloc;
	}

	if (sm_keystore_slot_read(ksdev, unit, keyslot16, AES_BLOCK_PAD(16),
				  blkkey16)) {
		dev_info(ksdev, "blkkey_ex: can't read 128-bit black key\n");
		goto dealloc;
	}

	if (sm_keystore_slot_read(ksdev, unit, keyslot24, AES_BLOCK_PAD(24),
				  blkkey24)) {
		dev_info(ksdev, "blkkey_ex: can't read 192-bit black key\n");
		goto dealloc;
	}

	if (sm_keystore_slot_read(ksdev, unit, keyslot32, AES_BLOCK_PAD(32),
				  blkkey32)) {
		dev_info(ksdev, "blkkey_ex: can't read 256-bit black key\n");
		goto dealloc;
	}


	if (!memcmp(blkkey8, clrkey8, 8)) {
		dev_info(ksdev, "blkkey_ex: 64-bit key cover failed\n");
		goto dealloc;
	}

	if (!memcmp(blkkey16, clrkey16, 16)) {
		dev_info(ksdev, "blkkey_ex: 128-bit key cover failed\n");
		goto dealloc;
	}

	if (!memcmp(blkkey24, clrkey24, 24)) {
		dev_info(ksdev, "blkkey_ex: 192-bit key cover failed\n");
		goto dealloc;
	}

	if (!memcmp(blkkey32, clrkey32, 32)) {
		dev_info(ksdev, "blkkey_ex: 256-bit key cover failed\n");
		goto dealloc;
	}


	key_display(ksdev, "64-bit clear key:", 8, clrkey8);
	key_display(ksdev, "64-bit black key:", AES_BLOCK_PAD(8), blkkey8);

	key_display(ksdev, "128-bit clear key:", 16, clrkey16);
	key_display(ksdev, "128-bit black key:", AES_BLOCK_PAD(16), blkkey16);

	key_display(ksdev, "192-bit clear key:", 24, clrkey24);
	key_display(ksdev, "192-bit black key:", AES_BLOCK_PAD(24), blkkey24);

	key_display(ksdev, "256-bit clear key:", 32, clrkey32);
	key_display(ksdev, "256-bit black key:", AES_BLOCK_PAD(32), blkkey32);

	/*
	 * Now encapsulate all keys as SM blobs out to external memory
	 * Blobs will appear as random-looking blocks of data different
	 * from the original source key, and 48 bytes longer than the
	 * original key, to account for the extra data encapsulated within.
	 */
	key_display(ksdev, "64-bit unwritten blob:", 96, blob8);
	key_display(ksdev, "128-bit unwritten blob:", 96, blob16);
	key_display(ksdev, "196-bit unwritten blob:", 96, blob24);
	key_display(ksdev, "256-bit unwritten blob:", 96, blob32);

	if (sm_keystore_slot_export(ksdev, unit, keyslot8, BLACK_KEY,
				    KEY_COVER_ECB, blob8, 8, skeymod)) {
		dev_info(ksdev, "blkkey_ex: can't encapsulate 64-bit key\n");
		goto dealloc;
	}

	if (sm_keystore_slot_export(ksdev, unit, keyslot16, BLACK_KEY,
				    KEY_COVER_ECB, blob16, 16, skeymod)) {
		dev_info(ksdev, "blkkey_ex: can't encapsulate 128-bit key\n");
		goto dealloc;
	}

	if (sm_keystore_slot_export(ksdev, unit, keyslot24, BLACK_KEY,
				    KEY_COVER_ECB, blob24, 24, skeymod)) {
		dev_info(ksdev, "blkkey_ex: can't encapsulate 192-bit key\n");
		goto dealloc;
	}

	if (sm_keystore_slot_export(ksdev, unit, keyslot32, BLACK_KEY,
				    KEY_COVER_ECB, blob32, 32, skeymod)) {
		dev_info(ksdev, "blkkey_ex: can't encapsulate 256-bit key\n");
		goto dealloc;
	}

	key_display(ksdev, "64-bit black key in blob:", 96, blob8);
	key_display(ksdev, "128-bit black key in blob:", 96, blob16);
	key_display(ksdev, "192-bit black key in blob:", 96, blob24);
	key_display(ksdev, "256-bit black key in blob:", 96, blob32);

	/*
	 * Now re-import black keys from secure-memory blobs stored
	 * in general memory from the previous operation. Since we are
	 * working with black keys, and since power has not cycled, the
	 * restored black keys should match the original blackened keys
	 * (this would not be true if the blobs were save in some non-volatile
	 * store, and power was cycled between the save and restore)
	 */
	if (sm_keystore_slot_import(ksdev, unit, keyslot8, BLACK_KEY,
				    KEY_COVER_ECB, blob8, 8, skeymod)) {
		dev_info(ksdev, "blkkey_ex: can't decapsulate 64-bit blob\n");
		goto dealloc;
	}

	if (sm_keystore_slot_import(ksdev, unit, keyslot16, BLACK_KEY,
				    KEY_COVER_ECB, blob16, 16, skeymod)) {
		dev_info(ksdev, "blkkey_ex: can't decapsulate 128-bit blob\n");
		goto dealloc;
	}

	if (sm_keystore_slot_import(ksdev, unit, keyslot24, BLACK_KEY,
				    KEY_COVER_ECB, blob24, 24, skeymod)) {
		dev_info(ksdev, "blkkey_ex: can't decapsulate 196-bit blob\n");
		goto dealloc;
	}

	if (sm_keystore_slot_import(ksdev, unit, keyslot32, BLACK_KEY,
				    KEY_COVER_ECB, blob32, 32, skeymod)) {
		dev_info(ksdev, "blkkey_ex: can't decapsulate 256-bit blob\n");
		goto dealloc;
	}


	/*
	 * Blobs are now restored as black keys. Read those black keys back
	 * for a comparison with the original black key, they should match
	 */
	if (sm_keystore_slot_read(ksdev, unit, keyslot8, AES_BLOCK_PAD(8),
				  rstkey8)) {
		dev_info(ksdev,
			"blkkey_ex: can't read restored 64-bit black key\n");
		goto dealloc;
	}

	if (sm_keystore_slot_read(ksdev, unit, keyslot16, AES_BLOCK_PAD(16),
				  rstkey16)) {
		dev_info(ksdev,
			 "blkkey_ex: can't read restored 128-bit black key\n");
		goto dealloc;
	}

	if (sm_keystore_slot_read(ksdev, unit, keyslot24, AES_BLOCK_PAD(24),
				  rstkey24)) {
		dev_info(ksdev,
			 "blkkey_ex: can't read restored 196-bit black key\n");
		goto dealloc;
	}

	if (sm_keystore_slot_read(ksdev, unit, keyslot32, AES_BLOCK_PAD(32),
				  rstkey32)) {
		dev_info(ksdev,
			 "blkkey_ex: can't read restored 256-bit black key\n");
		goto dealloc;
	}

	key_display(ksdev, "restored 64-bit black key:", AES_BLOCK_PAD(8),
		    rstkey8);
	key_display(ksdev, "restored 128-bit black key:", AES_BLOCK_PAD(16),
		    rstkey16);
	key_display(ksdev, "restored 192-bit black key:", AES_BLOCK_PAD(24),
		    rstkey24);
	key_display(ksdev, "restored 256-bit black key:", AES_BLOCK_PAD(32),
		    rstkey32);

	/*
	 * Compare the restored black keys with the original blackened keys
	 * As long as we're operating within the same power cycle, a black key
	 * restored from a blob should match the original black key IF the
	 * key happens to be of a size that matches a multiple of the AES
	 * blocksize. Any key that is padded to fill the block size will not
	 * match, excepting a key that exceeds a block; only the first full
	 * blocks will match (assuming ECB).
	 *
	 * Therefore, compare the 16 and 32 bit keys, they should match.
	 * The 24 bit key can only match within the first 16 byte block.
	 */

	if (memcmp(rstkey16, blkkey16, AES_BLOCK_PAD(16))) {
		dev_info(ksdev, "blkkey_ex: 128-bit restored key mismatch\n");
		rtnval--;
	}

	/* Only first AES block will match, remainder subject to padding */
	if (memcmp(rstkey24, blkkey24, 16)) {
		dev_info(ksdev, "blkkey_ex: 192-bit restored key mismatch\n");
		rtnval--;
	}

	if (memcmp(rstkey32, blkkey32, AES_BLOCK_PAD(32))) {
		dev_info(ksdev, "blkkey_ex: 256-bit restored key mismatch\n");
		rtnval--;
	}


	/* Remove keys from keystore */
dealloc:
	sm_keystore_slot_dealloc(ksdev, unit, keyslot8);
	sm_keystore_slot_dealloc(ksdev, unit, keyslot16);
	sm_keystore_slot_dealloc(ksdev, unit, keyslot24);
	sm_keystore_slot_dealloc(ksdev, unit, keyslot32);


	/* Free resources */
freemem:
	kfree(blob8);
	kfree(blob16);
	kfree(blob24);
	kfree(blob32);

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
MODULE_DESCRIPTION("FSL CAAM Black Key Usage Example");
MODULE_AUTHOR("Freescale Semiconductor - NMSG/MAD");
#endif
