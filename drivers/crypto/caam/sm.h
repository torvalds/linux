
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
	void	*base_address;	/* Base of the Secure Partition */
	u32	slot_count;	/* Number of slots in the keystore */
	struct keystore_data_slot_info *slot; /* Per-slot information */
};

/* store the detected attributes of a secure memory page */
struct sm_page_descriptor {
	u16 phys_pagenum;	/* may be discontiguous */
	u16 own_part;		/* Owning partition */
	void *pg_base;		/* Calculated virtual address */
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
	u32 (*slot_get_base)(struct device *dev, u32 unit, u32 handle);
	u32 (*slot_get_offset)(struct device *dev, u32 unit, u32 handle);
	u32 (*slot_get_slot_size)(struct device *dev, u32 unit, u32 handle);
};

#endif /* SM_H */
