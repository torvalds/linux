
/*
 * CAAM Secure Memory/Keywrap API Definitions
 * Copyright (C) 2008-2015 Freescale Semiconductor, Inc.
 */

#ifndef SM_H
#define SM_H


/* Storage access permissions */
#define SM_PERM_READ 0x01
#define SM_PERM_WRITE 0x02
#define SM_PERM_BLOB 0x03

/* Define treatment of secure memory vs. general memory blobs */
#define SM_SECMEM 0
#define SM_GENMEM 1

/* Define treatment of red/black keys */
#define RED_KEY 0
#define BLACK_KEY 1

/* Define key encryption/covering options */
#define KEY_COVER_ECB 0	/* cover key in AES-ECB */
#define KEY_COVER_CCM 1 /* cover key with AES-CCM */

/*
 * Round a key size up to an AES blocksize boundary so to allow for
 * padding out to a full block
 */
#define AES_BLOCK_PAD(x) ((x % 16) ? ((x >> 4) + 1) << 4 : x)

#define BLOB_OVERHEAD (16 + 8) /* BKEK + MAC tag storage in any blob */

/* Keystore maintenance functions */
void sm_init_keystore(struct device *dev);
u32 sm_detect_keystore_units(struct device *dev);
int sm_establish_keystore(struct device *dev, u32 unit);
void sm_release_keystore(struct device *dev, u32 unit);
void caam_sm_shutdown(struct platform_device *pdev);
int caam_sm_example_init(struct platform_device *pdev);

/* Keystore accessor functions */
extern int sm_keystore_slot_alloc(struct device *dev, u32 unit, u32 size,
				  u32 *slot);
extern int sm_keystore_slot_dealloc(struct device *dev, u32 unit, u32 slot);
extern int sm_keystore_slot_load(struct device *dev, u32 unit, u32 slot,
				 const u8 *key_data, u32 key_length);
extern int sm_keystore_slot_read(struct device *dev, u32 unit, u32 slot,
				 u32 key_length, u8 *key_data);
extern int sm_keystore_cover_key(struct device *dev, u32 unit, u32 slot,
				 u16 key_length, u8 keyauth);
extern int sm_keystore_slot_export(struct device *dev, u32 unit, u32 slot,
				   u8 keycolor, u8 keyauth, u8 *outbuf,
				   u16 keylen, u8 *keymod);
extern int sm_keystore_slot_import(struct device *dev, u32 unit, u32 slot,
				   u8 keycolor, u8 keyauth, u8 *inbuf,
				   u16 keylen, u8 *keymod);

/* Prior functions from legacy API, deprecated */
extern int sm_keystore_slot_encapsulate(struct device *dev, u32 unit,
					u32 inslot, u32 outslot, u16 secretlen,
					u8 *keymod, u16 keymodlen);
extern int sm_keystore_slot_decapsulate(struct device *dev, u32 unit,
					u32 inslot, u32 outslot, u16 secretlen,
					u8 *keymod, u16 keymodlen);

/* Data structure to hold per-slot information */
struct keystore_data_slot_info {
	u8	allocated;	/* Track slot assignments */
	u32	key_length;	/* Size of the key */
};

/* Data structure to hold keystore information */
struct keystore_data {
	void	*base_address;	/* Virtual base of secure memory pages */
	void	*phys_address;	/* Physical base of secure memory pages */
	u32	slot_count;	/* Number of slots in the keystore */
	struct keystore_data_slot_info *slot; /* Per-slot information */
};

/* store the detected attributes of a secure memory page */
struct sm_page_descriptor {
	u16 phys_pagenum;	/* may be discontiguous */
	u16 own_part;		/* Owning partition */
	void *pg_base;		/* Calculated virtual address */
	void *pg_phys;		/* Calculated physical address */
	struct keystore_data *ksdata;
};

struct caam_drv_private_sm {
	struct device *parentdev;	/* this ends up as the controller */
	struct device *smringdev;	/* ring that owns this instance */
	spinlock_t kslock ____cacheline_aligned;

	/* Default parameters for geometry */
	u32 max_pages;		/* maximum pages this instance can support */
	u32 top_partition;	/* highest partition number in this instance */
	u32 top_page;		/* highest page number in this instance */
	u32 page_size;		/* page size */
	u32 slot_size;		/* selected size of each storage block */

	/* Partition/Page Allocation Map */
	u32 localpages;		/* Number of pages we can access */
	struct sm_page_descriptor *pagedesc;	/* Allocated per-page */

	/* Installed handlers for keystore access */
	int (*data_init)(struct device *dev, u32 unit);
	void (*data_cleanup)(struct device *dev, u32 unit);
	int (*slot_alloc)(struct device *dev, u32 unit, u32 size, u32 *slot);
	int (*slot_dealloc)(struct device *dev, u32 unit, u32 slot);
	void *(*slot_get_address)(struct device *dev, u32 unit, u32 handle);
	void *(*slot_get_physical)(struct device *dev, u32 unit, u32 handle);
	u32 (*slot_get_base)(struct device *dev, u32 unit, u32 handle);
	u32 (*slot_get_offset)(struct device *dev, u32 unit, u32 handle);
	u32 (*slot_get_slot_size)(struct device *dev, u32 unit, u32 handle);
};

#endif /* SM_H */
