/* 
 * File...........: linux/drivers/s390/block/dasd_diag.h
 * Author(s)......: Holger Smolinski <Holger.Smolinski@de.ibm.com>
 * Based on.......: linux/drivers/s390/block/mdisk.h
 * ...............: by Hartmunt Penner <hpenner@de.ibm.com>
 * Bugreports.to..: <Linux390@de.ibm.com>
 * (C) IBM Corporation, IBM Deutschland Entwicklung GmbH, 1999,2000
 *
 * $Revision: 1.6 $
 */

#define MDSK_WRITE_REQ 0x01
#define MDSK_READ_REQ  0x02

#define INIT_BIO	0x00
#define RW_BIO		0x01
#define TERM_BIO	0x02

#define DEV_CLASS_FBA	0x01
#define DEV_CLASS_ECKD	0x04

struct dasd_diag_characteristics {
	u16 dev_nr;
	u16 rdc_len;
	u8 vdev_class;
	u8 vdev_type;
	u8 vdev_status;
	u8 vdev_flags;
	u8 rdev_class;
	u8 rdev_type;
	u8 rdev_model;
	u8 rdev_features;
} __attribute__ ((packed, aligned(4)));

struct dasd_diag_bio {
	u8 type;
	u8 status;
	u16 spare1;
	u32 block_number;
	u32 alet;
	u32 buffer;
} __attribute__ ((packed, aligned(8)));

struct dasd_diag_init_io {
	u16 dev_nr;
	u16 spare1[11];
	u32 block_size;
	u32 offset;
	u32 start_block;
	u32 end_block;
	u32 spare2[6];
} __attribute__ ((packed, aligned(8)));

struct dasd_diag_rw_io {
	u16 dev_nr;
	u16 spare1[11];
	u8 key;
	u8 flags;
	u16 spare2;
	u32 block_count;
	u32 alet;
	u32 bio_list;
	u32 interrupt_params;
	u32 spare3[5];
} __attribute__ ((packed, aligned(8)));

