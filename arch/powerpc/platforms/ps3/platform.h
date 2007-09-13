/*
 *  PS3 platform declarations.
 *
 *  Copyright (C) 2006 Sony Computer Entertainment Inc.
 *  Copyright 2006 Sony Corp.
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#if !defined(_PS3_PLATFORM_H)
#define _PS3_PLATFORM_H

#include <linux/rtc.h>
#include <scsi/scsi.h>

#include <asm/ps3.h>

/* htab */

void __init ps3_hpte_init(unsigned long htab_size);
void __init ps3_map_htab(void);

/* mm */

void __init ps3_mm_init(void);
void __init ps3_mm_vas_create(unsigned long* htab_size);
void ps3_mm_vas_destroy(void);
void ps3_mm_shutdown(void);

/* irq */

void ps3_init_IRQ(void);
void ps3_shutdown_IRQ(int cpu);
void __init ps3_register_ipi_debug_brk(unsigned int cpu, unsigned int virq);

/* smp */

void smp_init_ps3(void);
void ps3_smp_cleanup_cpu(int cpu);

/* time */

void __init ps3_calibrate_decr(void);
unsigned long __init ps3_get_boot_time(void);
void ps3_get_rtc_time(struct rtc_time *time);
int ps3_set_rtc_time(struct rtc_time *time);

/* os area */

int __init ps3_os_area_init(void);
u64 ps3_os_area_rtc_diff(void);

/* spu */

#if defined(CONFIG_SPU_BASE)
void ps3_spu_set_platform (void);
#else
static inline void ps3_spu_set_platform (void) {}
#endif

/* repository bus info */

enum ps3_bus_type {
	PS3_BUS_TYPE_SB = 4,
	PS3_BUS_TYPE_STORAGE = 5,
};

enum ps3_dev_type {
	PS3_DEV_TYPE_STOR_DISK = TYPE_DISK,	/* 0 */
	PS3_DEV_TYPE_SB_GELIC = 3,
	PS3_DEV_TYPE_SB_USB = 4,
	PS3_DEV_TYPE_STOR_ROM = TYPE_ROM,	/* 5 */
	PS3_DEV_TYPE_SB_GPIO = 6,
	PS3_DEV_TYPE_STOR_FLASH = TYPE_RBC,	/* 14 */
	PS3_DEV_TYPE_STOR_DUMMY = 32,
	PS3_DEV_TYPE_NOACCESS = 255,
};

int ps3_repository_read_bus_str(unsigned int bus_index, const char *bus_str,
	u64 *value);
int ps3_repository_read_bus_id(unsigned int bus_index, unsigned int *bus_id);
int ps3_repository_read_bus_type(unsigned int bus_index,
	enum ps3_bus_type *bus_type);
int ps3_repository_read_bus_num_dev(unsigned int bus_index,
	unsigned int *num_dev);

/* repository bus device info */

enum ps3_interrupt_type {
	PS3_INTERRUPT_TYPE_EVENT_PORT = 2,
	PS3_INTERRUPT_TYPE_SB_OHCI = 3,
	PS3_INTERRUPT_TYPE_SB_EHCI = 4,
	PS3_INTERRUPT_TYPE_OTHER = 5,
};

enum ps3_reg_type {
	PS3_REG_TYPE_SB_OHCI = 3,
	PS3_REG_TYPE_SB_EHCI = 4,
	PS3_REG_TYPE_SB_GPIO = 5,
};

int ps3_repository_read_dev_str(unsigned int bus_index,
	unsigned int dev_index, const char *dev_str, u64 *value);
int ps3_repository_read_dev_id(unsigned int bus_index, unsigned int dev_index,
	unsigned int *dev_id);
int ps3_repository_read_dev_type(unsigned int bus_index,
	unsigned int dev_index, enum ps3_dev_type *dev_type);
int ps3_repository_read_dev_intr(unsigned int bus_index,
	unsigned int dev_index, unsigned int intr_index,
	enum ps3_interrupt_type *intr_type, unsigned int *interrupt_id);
int ps3_repository_read_dev_reg_type(unsigned int bus_index,
	unsigned int dev_index, unsigned int reg_index,
	enum ps3_reg_type *reg_type);
int ps3_repository_read_dev_reg_addr(unsigned int bus_index,
	unsigned int dev_index, unsigned int reg_index, u64 *bus_addr,
	u64 *len);
int ps3_repository_read_dev_reg(unsigned int bus_index,
	unsigned int dev_index, unsigned int reg_index,
	enum ps3_reg_type *reg_type, u64 *bus_addr, u64 *len);

/* repository bus enumerators */

struct ps3_repository_device {
	enum ps3_bus_type bus_type;
	unsigned int bus_index;
	unsigned int bus_id;
	enum ps3_dev_type dev_type;
	unsigned int dev_index;
	unsigned int dev_id;
};

static inline struct ps3_repository_device *ps3_repository_bump_device(
	struct ps3_repository_device *repo)
{
	repo->dev_index++;
	return repo;
}
int ps3_repository_find_device(struct ps3_repository_device *repo);
int ps3_repository_find_devices(enum ps3_bus_type bus_type,
	int (*callback)(const struct ps3_repository_device *repo));
int ps3_repository_find_bus(enum ps3_bus_type bus_type, unsigned int from,
	unsigned int *bus_index);
int ps3_repository_find_interrupt(const struct ps3_repository_device *repo,
	enum ps3_interrupt_type intr_type, unsigned int *interrupt_id);
int ps3_repository_find_reg(const struct ps3_repository_device *repo,
	enum ps3_reg_type reg_type, u64 *bus_addr, u64 *len);

/* repository block device info */

int ps3_repository_read_stor_dev_port(unsigned int bus_index,
	unsigned int dev_index, u64 *port);
int ps3_repository_read_stor_dev_blk_size(unsigned int bus_index,
	unsigned int dev_index, u64 *blk_size);
int ps3_repository_read_stor_dev_num_blocks(unsigned int bus_index,
	unsigned int dev_index, u64 *num_blocks);
int ps3_repository_read_stor_dev_num_regions(unsigned int bus_index,
	unsigned int dev_index, unsigned int *num_regions);
int ps3_repository_read_stor_dev_region_id(unsigned int bus_index,
	unsigned int dev_index, unsigned int region_index,
	unsigned int *region_id);
int ps3_repository_read_stor_dev_region_size(unsigned int bus_index,
	unsigned int dev_index,	unsigned int region_index, u64 *region_size);
int ps3_repository_read_stor_dev_region_start(unsigned int bus_index,
	unsigned int dev_index, unsigned int region_index, u64 *region_start);
int ps3_repository_read_stor_dev_info(unsigned int bus_index,
	unsigned int dev_index, u64 *port, u64 *blk_size,
	u64 *num_blocks, unsigned int *num_regions);
int ps3_repository_read_stor_dev_region(unsigned int bus_index,
	unsigned int dev_index, unsigned int region_index,
	unsigned int *region_id, u64 *region_start, u64 *region_size);

/* repository pu and memory info */

int ps3_repository_read_num_pu(unsigned int *num_pu);
int ps3_repository_read_ppe_id(unsigned int *pu_index, unsigned int *ppe_id);
int ps3_repository_read_rm_base(unsigned int ppe_id, u64 *rm_base);
int ps3_repository_read_rm_size(unsigned int ppe_id, u64 *rm_size);
int ps3_repository_read_region_total(u64 *region_total);
int ps3_repository_read_mm_info(u64 *rm_base, u64 *rm_size,
	u64 *region_total);

/* repository pme info */

int ps3_repository_read_num_be(unsigned int *num_be);
int ps3_repository_read_be_node_id(unsigned int be_index, u64 *node_id);
int ps3_repository_read_tb_freq(u64 node_id, u64 *tb_freq);
int ps3_repository_read_be_tb_freq(unsigned int be_index, u64 *tb_freq);

/* repository 'Other OS' area */

int ps3_repository_read_boot_dat_addr(u64 *lpar_addr);
int ps3_repository_read_boot_dat_size(unsigned int *size);
int ps3_repository_read_boot_dat_info(u64 *lpar_addr, unsigned int *size);

/* repository spu info */

/**
 * enum spu_resource_type - Type of spu resource.
 * @spu_resource_type_shared: Logical spu is shared with other partions.
 * @spu_resource_type_exclusive: Logical spu is not shared with other partions.
 *
 * Returned by ps3_repository_read_spu_resource_id().
 */

enum ps3_spu_resource_type {
	PS3_SPU_RESOURCE_TYPE_SHARED = 0,
	PS3_SPU_RESOURCE_TYPE_EXCLUSIVE = 0x8000000000000000UL,
};

int ps3_repository_read_num_spu_reserved(unsigned int *num_spu_reserved);
int ps3_repository_read_num_spu_resource_id(unsigned int *num_resource_id);
int ps3_repository_read_spu_resource_id(unsigned int res_index,
	enum ps3_spu_resource_type* resource_type, unsigned int *resource_id);

/* repository vuart info */

int ps3_repository_read_vuart_av_port(unsigned int *port);
int ps3_repository_read_vuart_sysmgr_port(unsigned int *port);

/* Page table entries */
#define IOPTE_PP_W		0x8000000000000000ul /* protection: write */
#define IOPTE_PP_R		0x4000000000000000ul /* protection: read */
#define IOPTE_M			0x2000000000000000ul /* coherency required */
#define IOPTE_SO_R		0x1000000000000000ul /* ordering: writes */
#define IOPTE_SO_RW             0x1800000000000000ul /* ordering: r & w */
#define IOPTE_RPN_Mask		0x07fffffffffff000ul /* RPN */
#define IOPTE_H			0x0000000000000800ul /* cache hint */
#define IOPTE_IOID_Mask		0x00000000000007fful /* ioid */

#endif
