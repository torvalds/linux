/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * NVRAM definitions and access functions.
 */
#ifndef _ASM_POWERPC_NVRAM_H
#define _ASM_POWERPC_NVRAM_H

#include <linux/types.h>
#include <linux/errno.h>
#include <linux/list.h>
#include <uapi/asm/nvram.h>

/*
 * Set oops header version to distinguish between old and new format header.
 * lnx,oops-log partition max size is 4000, header version > 4000 will
 * help in identifying new header.
 */
#define OOPS_HDR_VERSION 5000

struct err_log_info {
	__be32 error_type;
	__be32 seq_num;
};

struct nvram_os_partition {
	const char *name;
	int req_size;	/* desired size, in bytes */
	int min_size;	/* minimum acceptable size (0 means req_size) */
	long size;	/* size of data portion (excluding err_log_info) */
	long index;	/* offset of data portion of partition */
	bool os_partition; /* partition initialized by OS, not FW */
};

struct oops_log_info {
	__be16 version;
	__be16 report_length;
	__be64 timestamp;
} __attribute__((packed));

extern struct nvram_os_partition oops_log_partition;

#ifdef CONFIG_PPC_PSERIES
extern struct nvram_os_partition rtas_log_partition;

extern int nvram_write_error_log(char * buff, int length,
					 unsigned int err_type, unsigned int err_seq);
extern int nvram_read_error_log(char * buff, int length,
					 unsigned int * err_type, unsigned int *err_seq);
extern int nvram_clear_error_log(void);
extern int pSeries_nvram_init(void);
#endif /* CONFIG_PPC_PSERIES */

#ifdef CONFIG_MMIO_NVRAM
extern int mmio_nvram_init(void);
#else
static inline int mmio_nvram_init(void)
{
	return -ENODEV;
}
#endif

extern int __init nvram_scan_partitions(void);
extern loff_t nvram_create_partition(const char *name, int sig,
				     int req_size, int min_size);
extern int nvram_remove_partition(const char *name, int sig,
					const char *exceptions[]);
extern int nvram_get_partition_size(loff_t data_index);
extern loff_t nvram_find_partition(const char *name, int sig, int *out_size);

/* Return partition offset in nvram */
extern int	pmac_get_partition(int partition);

/* Direct access to XPRAM on PowerMacs */
extern u8	pmac_xpram_read(int xpaddr);
extern void	pmac_xpram_write(int xpaddr, u8 data);

/* Initialize NVRAM OS partition */
extern int __init nvram_init_os_partition(struct nvram_os_partition *part);

/* Initialize NVRAM oops partition */
extern void __init nvram_init_oops_partition(int rtas_partition_exists);

/* Read a NVRAM partition */
extern int nvram_read_partition(struct nvram_os_partition *part, char *buff,
				int length, unsigned int *err_type,
				unsigned int *error_log_cnt);

/* Write to NVRAM OS partition */
extern int nvram_write_os_partition(struct nvram_os_partition *part,
				    char *buff, int length,
				    unsigned int err_type,
				    unsigned int error_log_cnt);

#endif /* _ASM_POWERPC_NVRAM_H */
