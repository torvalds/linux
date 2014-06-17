#ifndef TARGET_CORE_RD_H
#define TARGET_CORE_RD_H

#define RD_HBA_VERSION		"v4.0"
#define RD_MCP_VERSION		"4.0"

/* Largest piece of memory kmalloc can allocate */
#define RD_MAX_ALLOCATION_SIZE	65536
#define RD_DEVICE_QUEUE_DEPTH	32
#define RD_MAX_DEVICE_QUEUE_DEPTH 128
#define RD_BLOCKSIZE		512

/* Used in target_core_init_configfs() for virtual LUN 0 access */
int __init rd_module_init(void);
void rd_module_exit(void);

struct rd_dev_sg_table {
	u32		page_start_offset;
	u32		page_end_offset;
	u32		rd_sg_count;
	struct scatterlist *sg_table;
} ____cacheline_aligned;

#define RDF_HAS_PAGE_COUNT	0x01
#define RDF_NULLIO		0x02

struct rd_dev {
	struct se_device dev;
	u32		rd_flags;
	/* Unique Ramdisk Device ID in Ramdisk HBA */
	u32		rd_dev_id;
	/* Total page count for ramdisk device */
	u32		rd_page_count;
	/* Number of SG tables in sg_table_array */
	u32		sg_table_count;
	/* Number of SG tables in sg_prot_array */
	u32		sg_prot_count;
	/* Array of rd_dev_sg_table_t containing scatterlists */
	struct rd_dev_sg_table *sg_table_array;
	/* Array of rd_dev_sg_table containing protection scatterlists */
	struct rd_dev_sg_table *sg_prot_array;
	/* Ramdisk HBA device is connected to */
	struct rd_host *rd_host;
} ____cacheline_aligned;

struct rd_host {
	u32		rd_host_dev_id_count;
	u32		rd_host_id;		/* Unique Ramdisk Host ID */
} ____cacheline_aligned;

#endif /* TARGET_CORE_RD_H */
