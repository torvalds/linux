/*
 * Copyright (C) Ericsson AB 2007-2008
 * Copyright (C) ST-Ericsson SA 2008-2010
 * Author: Per Forlin <per.forlin@stericsson.com> for ST-Ericsson
 * Author: Jonas Aaberg <jonas.aberg@stericsson.com> for ST-Ericsson
 * License terms: GNU General Public License (GPL) version 2
 */

#include <linux/dma-mapping.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/export.h>
#include <linux/dmaengine.h>
#include <linux/platform_device.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/log2.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/err.h>
#include <linux/of.h>
#include <linux/of_dma.h>
#include <linux/amba/bus.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_data/dma-ste-dma40.h>

#include "dmaengine.h"
#include "ste_dma40_ll.h"

#define D40_NAME "dma40"

#define D40_PHY_CHAN -1

/* For masking out/in 2 bit channel positions */
#define D40_CHAN_POS(chan)  (2 * (chan / 2))
#define D40_CHAN_POS_MASK(chan) (0x3 << D40_CHAN_POS(chan))

/* Maximum iterations taken before giving up suspending a channel */
#define D40_SUSPEND_MAX_IT 500

/* Milliseconds */
#define DMA40_AUTOSUSPEND_DELAY	100

/* Hardware requirement on LCLA alignment */
#define LCLA_ALIGNMENT 0x40000

/* Max number of links per event group */
#define D40_LCLA_LINK_PER_EVENT_GRP 128
#define D40_LCLA_END D40_LCLA_LINK_PER_EVENT_GRP

/* Max number of logical channels per physical channel */
#define D40_MAX_LOG_CHAN_PER_PHY 32

/* Attempts before giving up to trying to get pages that are aligned */
#define MAX_LCLA_ALLOC_ATTEMPTS 256

/* Bit markings for allocation map */
#define D40_ALLOC_FREE		BIT(31)
#define D40_ALLOC_PHY		BIT(30)
#define D40_ALLOC_LOG_FREE	0

#define D40_MEMCPY_MAX_CHANS	8

/* Reserved event lines for memcpy only. */
#define DB8500_DMA_MEMCPY_EV_0	51
#define DB8500_DMA_MEMCPY_EV_1	56
#define DB8500_DMA_MEMCPY_EV_2	57
#define DB8500_DMA_MEMCPY_EV_3	58
#define DB8500_DMA_MEMCPY_EV_4	59
#define DB8500_DMA_MEMCPY_EV_5	60

static int dma40_memcpy_channels[] = {
	DB8500_DMA_MEMCPY_EV_0,
	DB8500_DMA_MEMCPY_EV_1,
	DB8500_DMA_MEMCPY_EV_2,
	DB8500_DMA_MEMCPY_EV_3,
	DB8500_DMA_MEMCPY_EV_4,
	DB8500_DMA_MEMCPY_EV_5,
};

/* Default configuration for physcial memcpy */
static const struct stedma40_chan_cfg dma40_memcpy_conf_phy = {
	.mode = STEDMA40_MODE_PHYSICAL,
	.dir = DMA_MEM_TO_MEM,

	.src_info.data_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
	.src_info.psize = STEDMA40_PSIZE_PHY_1,
	.src_info.flow_ctrl = STEDMA40_NO_FLOW_CTRL,

	.dst_info.data_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
	.dst_info.psize = STEDMA40_PSIZE_PHY_1,
	.dst_info.flow_ctrl = STEDMA40_NO_FLOW_CTRL,
};

/* Default configuration for logical memcpy */
static const struct stedma40_chan_cfg dma40_memcpy_conf_log = {
	.mode = STEDMA40_MODE_LOGICAL,
	.dir = DMA_MEM_TO_MEM,

	.src_info.data_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
	.src_info.psize = STEDMA40_PSIZE_LOG_1,
	.src_info.flow_ctrl = STEDMA40_NO_FLOW_CTRL,

	.dst_info.data_width = DMA_SLAVE_BUSWIDTH_1_BYTE,
	.dst_info.psize = STEDMA40_PSIZE_LOG_1,
	.dst_info.flow_ctrl = STEDMA40_NO_FLOW_CTRL,
};

/**
 * enum 40_command - The different commands and/or statuses.
 *
 * @D40_DMA_STOP: DMA channel command STOP or status STOPPED,
 * @D40_DMA_RUN: The DMA channel is RUNNING of the command RUN.
 * @D40_DMA_SUSPEND_REQ: Request the DMA to SUSPEND as soon as possible.
 * @D40_DMA_SUSPENDED: The DMA channel is SUSPENDED.
 */
enum d40_command {
	D40_DMA_STOP		= 0,
	D40_DMA_RUN		= 1,
	D40_DMA_SUSPEND_REQ	= 2,
	D40_DMA_SUSPENDED	= 3
};

/*
 * enum d40_events - The different Event Enables for the event lines.
 *
 * @D40_DEACTIVATE_EVENTLINE: De-activate Event line, stopping the logical chan.
 * @D40_ACTIVATE_EVENTLINE: Activate the Event line, to start a logical chan.
 * @D40_SUSPEND_REQ_EVENTLINE: Requesting for suspending a event line.
 * @D40_ROUND_EVENTLINE: Status check for event line.
 */

enum d40_events {
	D40_DEACTIVATE_EVENTLINE	= 0,
	D40_ACTIVATE_EVENTLINE		= 1,
	D40_SUSPEND_REQ_EVENTLINE	= 2,
	D40_ROUND_EVENTLINE		= 3
};

/*
 * These are the registers that has to be saved and later restored
 * when the DMA hw is powered off.
 * TODO: Add save/restore of D40_DREG_GCC on dma40 v3 or later, if that works.
 */
static u32 d40_backup_regs[] = {
	D40_DREG_LCPA,
	D40_DREG_LCLA,
	D40_DREG_PRMSE,
	D40_DREG_PRMSO,
	D40_DREG_PRMOE,
	D40_DREG_PRMOO,
};

#define BACKUP_REGS_SZ ARRAY_SIZE(d40_backup_regs)

/*
 * since 9540 and 8540 has the same HW revision
 * use v4a for 9540 or ealier
 * use v4b for 8540 or later
 * HW revision:
 * DB8500ed has revision 0
 * DB8500v1 has revision 2
 * DB8500v2 has revision 3
 * AP9540v1 has revision 4
 * DB8540v1 has revision 4
 * TODO: Check if all these registers have to be saved/restored on dma40 v4a
 */
static u32 d40_backup_regs_v4a[] = {
	D40_DREG_PSEG1,
	D40_DREG_PSEG2,
	D40_DREG_PSEG3,
	D40_DREG_PSEG4,
	D40_DREG_PCEG1,
	D40_DREG_PCEG2,
	D40_DREG_PCEG3,
	D40_DREG_PCEG4,
	D40_DREG_RSEG1,
	D40_DREG_RSEG2,
	D40_DREG_RSEG3,
	D40_DREG_RSEG4,
	D40_DREG_RCEG1,
	D40_DREG_RCEG2,
	D40_DREG_RCEG3,
	D40_DREG_RCEG4,
};

#define BACKUP_REGS_SZ_V4A ARRAY_SIZE(d40_backup_regs_v4a)

static u32 d40_backup_regs_v4b[] = {
	D40_DREG_CPSEG1,
	D40_DREG_CPSEG2,
	D40_DREG_CPSEG3,
	D40_DREG_CPSEG4,
	D40_DREG_CPSEG5,
	D40_DREG_CPCEG1,
	D40_DREG_CPCEG2,
	D40_DREG_CPCEG3,
	D40_DREG_CPCEG4,
	D40_DREG_CPCEG5,
	D40_DREG_CRSEG1,
	D40_DREG_CRSEG2,
	D40_DREG_CRSEG3,
	D40_DREG_CRSEG4,
	D40_DREG_CRSEG5,
	D40_DREG_CRCEG1,
	D40_DREG_CRCEG2,
	D40_DREG_CRCEG3,
	D40_DREG_CRCEG4,
	D40_DREG_CRCEG5,
};

#define BACKUP_REGS_SZ_V4B ARRAY_SIZE(d40_backup_regs_v4b)

static u32 d40_backup_regs_chan[] = {
	D40_CHAN_REG_SSCFG,
	D40_CHAN_REG_SSELT,
	D40_CHAN_REG_SSPTR,
	D40_CHAN_REG_SSLNK,
	D40_CHAN_REG_SDCFG,
	D40_CHAN_REG_SDELT,
	D40_CHAN_REG_SDPTR,
	D40_CHAN_REG_SDLNK,
};

#define BACKUP_REGS_SZ_MAX ((BACKUP_REGS_SZ_V4A > BACKUP_REGS_SZ_V4B) ? \
			     BACKUP_REGS_SZ_V4A : BACKUP_REGS_SZ_V4B)

/**
 * struct d40_interrupt_lookup - lookup table for interrupt handler
 *
 * @src: Interrupt mask register.
 * @clr: Interrupt clear register.
 * @is_error: true if this is an error interrupt.
 * @offset: start delta in the lookup_log_chans in d40_base. If equals to
 * D40_PHY_CHAN, the lookup_phy_chans shall be used instead.
 */
struct d40_interrupt_lookup {
	u32 src;
	u32 clr;
	bool is_error;
	int offset;
};


static struct d40_interrupt_lookup il_v4a[] = {
	{D40_DREG_LCTIS0, D40_DREG_LCICR0, false,  0},
	{D40_DREG_LCTIS1, D40_DREG_LCICR1, false, 32},
	{D40_DREG_LCTIS2, D40_DREG_LCICR2, false, 64},
	{D40_DREG_LCTIS3, D40_DREG_LCICR3, false, 96},
	{D40_DREG_LCEIS0, D40_DREG_LCICR0, true,   0},
	{D40_DREG_LCEIS1, D40_DREG_LCICR1, true,  32},
	{D40_DREG_LCEIS2, D40_DREG_LCICR2, true,  64},
	{D40_DREG_LCEIS3, D40_DREG_LCICR3, true,  96},
	{D40_DREG_PCTIS,  D40_DREG_PCICR,  false, D40_PHY_CHAN},
	{D40_DREG_PCEIS,  D40_DREG_PCICR,  true,  D40_PHY_CHAN},
};

static struct d40_interrupt_lookup il_v4b[] = {
	{D40_DREG_CLCTIS1, D40_DREG_CLCICR1, false,  0},
	{D40_DREG_CLCTIS2, D40_DREG_CLCICR2, false, 32},
	{D40_DREG_CLCTIS3, D40_DREG_CLCICR3, false, 64},
	{D40_DREG_CLCTIS4, D40_DREG_CLCICR4, false, 96},
	{D40_DREG_CLCTIS5, D40_DREG_CLCICR5, false, 128},
	{D40_DREG_CLCEIS1, D40_DREG_CLCICR1, true,   0},
	{D40_DREG_CLCEIS2, D40_DREG_CLCICR2, true,  32},
	{D40_DREG_CLCEIS3, D40_DREG_CLCICR3, true,  64},
	{D40_DREG_CLCEIS4, D40_DREG_CLCICR4, true,  96},
	{D40_DREG_CLCEIS5, D40_DREG_CLCICR5, true,  128},
	{D40_DREG_CPCTIS,  D40_DREG_CPCICR,  false, D40_PHY_CHAN},
	{D40_DREG_CPCEIS,  D40_DREG_CPCICR,  true,  D40_PHY_CHAN},
};

/**
 * struct d40_reg_val - simple lookup struct
 *
 * @reg: The register.
 * @val: The value that belongs to the register in reg.
 */
struct d40_reg_val {
	unsigned int reg;
	unsigned int val;
};

static __initdata struct d40_reg_val dma_init_reg_v4a[] = {
	/* Clock every part of the DMA block from start */
	{ .reg = D40_DREG_GCC,    .val = D40_DREG_GCC_ENABLE_ALL},

	/* Interrupts on all logical channels */
	{ .reg = D40_DREG_LCMIS0, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_LCMIS1, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_LCMIS2, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_LCMIS3, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_LCICR0, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_LCICR1, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_LCICR2, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_LCICR3, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_LCTIS0, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_LCTIS1, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_LCTIS2, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_LCTIS3, .val = 0xFFFFFFFF}
};
static __initdata struct d40_reg_val dma_init_reg_v4b[] = {
	/* Clock every part of the DMA block from start */
	{ .reg = D40_DREG_GCC,    .val = D40_DREG_GCC_ENABLE_ALL},

	/* Interrupts on all logical channels */
	{ .reg = D40_DREG_CLCMIS1, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_CLCMIS2, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_CLCMIS3, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_CLCMIS4, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_CLCMIS5, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_CLCICR1, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_CLCICR2, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_CLCICR3, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_CLCICR4, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_CLCICR5, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_CLCTIS1, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_CLCTIS2, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_CLCTIS3, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_CLCTIS4, .val = 0xFFFFFFFF},
	{ .reg = D40_DREG_CLCTIS5, .val = 0xFFFFFFFF}
};

/**
 * struct d40_lli_pool - Structure for keeping LLIs in memory
 *
 * @base: Pointer to memory area when the pre_alloc_lli's are not large
 * enough, IE bigger than the most common case, 1 dst and 1 src. NULL if
 * pre_alloc_lli is used.
 * @dma_addr: DMA address, if mapped
 * @size: The size in bytes of the memory at base or the size of pre_alloc_lli.
 * @pre_alloc_lli: Pre allocated area for the most common case of transfers,
 * one buffer to one buffer.
 */
struct d40_lli_pool {
	void	*base;
	int	 size;
	dma_addr_t	dma_addr;
	/* Space for dst and src, plus an extra for padding */
	u8	 pre_alloc_lli[3 * sizeof(struct d40_phy_lli)];
};

/**
 * struct d40_desc - A descriptor is one DMA job.
 *
 * @lli_phy: LLI settings for physical channel. Both src and dst=
 * points into the lli_pool, to base if lli_len > 1 or to pre_alloc_lli if
 * lli_len equals one.
 * @lli_log: Same as above but for logical channels.
 * @lli_pool: The pool with two entries pre-allocated.
 * @lli_len: Number of llis of current descriptor.
 * @lli_current: Number of transferred llis.
 * @lcla_alloc: Number of LCLA entries allocated.
 * @txd: DMA engine struct. Used for among other things for communication
 * during a transfer.
 * @node: List entry.
 * @is_in_client_list: true if the client owns this descriptor.
 * @cyclic: true if this is a cyclic job
 *
 * This descriptor is used for both logical and physical transfers.
 */
struct d40_desc {
	/* LLI physical */
	struct d40_phy_lli_bidir	 lli_phy;
	/* LLI logical */
	struct d40_log_lli_bidir	 lli_log;

	struct d40_lli_pool		 lli_pool;
	int				 lli_len;
	int				 lli_current;
	int				 lcla_alloc;

	struct dma_async_tx_descriptor	 txd;
	struct list_head		 node;

	bool				 is_in_client_list;
	bool				 cyclic;
};

/**
 * struct d40_lcla_pool - LCLA pool settings and data.
 *
 * @base: The virtual address of LCLA. 18 bit aligned.
 * @base_unaligned: The orignal kmalloc pointer, if kmalloc is used.
 * This pointer is only there for clean-up on error.
 * @pages: The number of pages needed for all physical channels.
 * Only used later for clean-up on error
 * @lock: Lock to protect the content in this struct.
 * @alloc_map: big map over which LCLA entry is own by which job.
 */
struct d40_lcla_pool {
	void		*base;
	dma_addr_t	dma_addr;
	void		*base_unaligned;
	int		 pages;
	spinlock_t	 lock;
	struct d40_desc	**alloc_map;
};

/**
 * struct d40_phy_res - struct for handling eventlines mapped to physical
 * channels.
 *
 * @lock: A lock protection this entity.
 * @reserved: True if used by secure world or otherwise.
 * @num: The physical channel number of this entity.
 * @allocated_src: Bit mapped to show which src event line's are mapped to
 * this physical channel. Can also be free or physically allocated.
 * @allocated_dst: Same as for src but is dst.
 * allocated_dst and allocated_src uses the D40_ALLOC* defines as well as
 * event line number.
 * @use_soft_lli: To mark if the linked lists of channel are managed by SW.
 */
struct d40_phy_res {
	spinlock_t lock;
	bool	   reserved;
	int	   num;
	u32	   allocated_src;
	u32	   allocated_dst;
	bool	   use_soft_lli;
};

struct d40_base;

/**
 * struct d40_chan - Struct that describes a channel.
 *
 * @lock: A spinlock to protect this struct.
 * @log_num: The logical number, if any of this channel.
 * @pending_tx: The number of pending transfers. Used between interrupt handler
 * and tasklet.
 * @busy: Set to true when transfer is ongoing on this channel.
 * @phy_chan: Pointer to physical channel which this instance runs on. If this
 * point is NULL, then the channel is not allocated.
 * @chan: DMA engine handle.
 * @tasklet: Tasklet that gets scheduled from interrupt context to complete a
 * transfer and call client callback.
 * @client: Cliented owned descriptor list.
 * @pending_queue: Submitted jobs, to be issued by issue_pending()
 * @active: Active descriptor.
 * @done: Completed jobs
 * @queue: Queued jobs.
 * @prepare_queue: Prepared jobs.
 * @dma_cfg: The client configuration of this dma channel.
 * @slave_config: DMA slave configuration.
 * @configured: whether the dma_cfg configuration is valid
 * @base: Pointer to the device instance struct.
 * @src_def_cfg: Default cfg register setting for src.
 * @dst_def_cfg: Default cfg register setting for dst.
 * @log_def: Default logical channel settings.
 * @lcpa: Pointer to dst and src lcpa settings.
 * @runtime_addr: runtime configured address.
 * @runtime_direction: runtime configured direction.
 *
 * This struct can either "be" a logical or a physical channel.
 */
struct d40_chan {
	spinlock_t			 lock;
	int				 log_num;
	int				 pending_tx;
	bool				 busy;
	struct d40_phy_res		*phy_chan;
	struct dma_chan			 chan;
	struct tasklet_struct		 tasklet;
	struct list_head		 client;
	struct list_head		 pending_queue;
	struct list_head		 active;
	struct list_head		 done;
	struct list_head		 queue;
	struct list_head		 prepare_queue;
	struct stedma40_chan_cfg	 dma_cfg;
	struct dma_slave_config		 slave_config;
	bool				 configured;
	struct d40_base			*base;
	/* Default register configurations */
	u32				 src_def_cfg;
	u32				 dst_def_cfg;
	struct d40_def_lcsp		 log_def;
	struct d40_log_lli_full		*lcpa;
	/* Runtime reconfiguration */
	dma_addr_t			runtime_addr;
	enum dma_transfer_direction	runtime_direction;
};

/**
 * struct d40_gen_dmac - generic values to represent u8500/u8540 DMA
 * controller
 *
 * @backup: the pointer to the registers address array for backup
 * @backup_size: the size of the registers address array for backup
 * @realtime_en: the realtime enable register
 * @realtime_clear: the realtime clear register
 * @high_prio_en: the high priority enable register
 * @high_prio_clear: the high priority clear register
 * @interrupt_en: the interrupt enable register
 * @interrupt_clear: the interrupt clear register
 * @il: the pointer to struct d40_interrupt_lookup
 * @il_size: the size of d40_interrupt_lookup array
 * @init_reg: the pointer to the struct d40_reg_val
 * @init_reg_size: the size of d40_reg_val array
 */
struct d40_gen_dmac {
	u32				*backup;
	u32				 backup_size;
	u32				 realtime_en;
	u32				 realtime_clear;
	u32				 high_prio_en;
	u32				 high_prio_clear;
	u32				 interrupt_en;
	u32				 interrupt_clear;
	struct d40_interrupt_lookup	*il;
	u32				 il_size;
	struct d40_reg_val		*init_reg;
	u32				 init_reg_size;
};

/**
 * struct d40_base - The big global struct, one for each probe'd instance.
 *
 * @interrupt_lock: Lock used to make sure one interrupt is handle a time.
 * @execmd_lock: Lock for execute command usage since several channels share
 * the same physical register.
 * @dev: The device structure.
 * @virtbase: The virtual base address of the DMA's register.
 * @rev: silicon revision detected.
 * @clk: Pointer to the DMA clock structure.
 * @phy_start: Physical memory start of the DMA registers.
 * @phy_size: Size of the DMA register map.
 * @irq: The IRQ number.
 * @num_memcpy_chans: The number of channels used for memcpy (mem-to-mem
 * transfers).
 * @num_phy_chans: The number of physical channels. Read from HW. This
 * is the number of available channels for this driver, not counting "Secure
 * mode" allocated physical channels.
 * @num_log_chans: The number of logical channels. Calculated from
 * num_phy_chans.
 * @dma_both: dma_device channels that can do both memcpy and slave transfers.
 * @dma_slave: dma_device channels that can do only do slave transfers.
 * @dma_memcpy: dma_device channels that can do only do memcpy transfers.
 * @phy_chans: Room for all possible physical channels in system.
 * @log_chans: Room for all possible logical channels in system.
 * @lookup_log_chans: Used to map interrupt number to logical channel. Points
 * to log_chans entries.
 * @lookup_phy_chans: Used to map interrupt number to physical channel. Points
 * to phy_chans entries.
 * @plat_data: Pointer to provided platform_data which is the driver
 * configuration.
 * @lcpa_regulator: Pointer to hold the regulator for the esram bank for lcla.
 * @phy_res: Vector containing all physical channels.
 * @lcla_pool: lcla pool settings and data.
 * @lcpa_base: The virtual mapped address of LCPA.
 * @phy_lcpa: The physical address of the LCPA.
 * @lcpa_size: The size of the LCPA area.
 * @desc_slab: cache for descriptors.
 * @reg_val_backup: Here the values of some hardware registers are stored
 * before the DMA is powered off. They are restored when the power is back on.
 * @reg_val_backup_v4: Backup of registers that only exits on dma40 v3 and
 * later
 * @reg_val_backup_chan: Backup data for standard channel parameter registers.
 * @regs_interrupt: Scratch space for registers during interrupt.
 * @gcc_pwr_off_mask: Mask to maintain the channels that can be turned off.
 * @gen_dmac: the struct for generic registers values to represent u8500/8540
 * DMA controller
 */
struct d40_base {
	spinlock_t			 interrupt_lock;
	spinlock_t			 execmd_lock;
	struct device			 *dev;
	void __iomem			 *virtbase;
	u8				  rev:4;
	struct clk			 *clk;
	phys_addr_t			  phy_start;
	resource_size_t			  phy_size;
	int				  irq;
	int				  num_memcpy_chans;
	int				  num_phy_chans;
	int				  num_log_chans;
	struct device_dma_parameters	  dma_parms;
	struct dma_device		  dma_both;
	struct dma_device		  dma_slave;
	struct dma_device		  dma_memcpy;
	struct d40_chan			 *phy_chans;
	struct d40_chan			 *log_chans;
	struct d40_chan			**lookup_log_chans;
	struct d40_chan			**lookup_phy_chans;
	struct stedma40_platform_data	 *plat_data;
	struct regulator		 *lcpa_regulator;
	/* Physical half channels */
	struct d40_phy_res		 *phy_res;
	struct d40_lcla_pool		  lcla_pool;
	void				 *lcpa_base;
	dma_addr_t			  phy_lcpa;
	resource_size_t			  lcpa_size;
	struct kmem_cache		 *desc_slab;
	u32				  reg_val_backup[BACKUP_REGS_SZ];
	u32				  reg_val_backup_v4[BACKUP_REGS_SZ_MAX];
	u32				 *reg_val_backup_chan;
	u32				 *regs_interrupt;
	u16				  gcc_pwr_off_mask;
	struct d40_gen_dmac		  gen_dmac;
};

static struct device *chan2dev(struct d40_chan *d40c)
{
	return &d40c->chan.dev->device;
}

static bool chan_is_physical(struct d40_chan *chan)
{
	return chan->log_num == D40_PHY_CHAN;
}

static bool chan_is_logical(struct d40_chan *chan)
{
	return !chan_is_physical(chan);
}

static void __iomem *chan_base(struct d40_chan *chan)
{
	return chan->base->virtbase + D40_DREG_PCBASE +
	       chan->phy_chan->num * D40_DREG_PCDELTA;
}

#define d40_err(dev, format, arg...)		\
	dev_err(dev, "[%s] " format, __func__, ## arg)

#define chan_err(d40c, format, arg...)		\
	d40_err(chan2dev(d40c), format, ## arg)

static int d40_set_runtime_config_write(struct dma_chan *chan,
				  struct dma_slave_config *config,
				  enum dma_transfer_direction direction);

static int d40_pool_lli_alloc(struct d40_chan *d40c, struct d40_desc *d40d,
			      int lli_len)
{
	bool is_log = chan_is_logical(d40c);
	u32 align;
	void *base;

	if (is_log)
		align = sizeof(struct d40_log_lli);
	else
		align = sizeof(struct d40_phy_lli);

	if (lli_len == 1) {
		base = d40d->lli_pool.pre_alloc_lli;
		d40d->lli_pool.size = sizeof(d40d->lli_pool.pre_alloc_lli);
		d40d->lli_pool.base = NULL;
	} else {
		d40d->lli_pool.size = lli_len * 2 * align;

		base = kmalloc(d40d->lli_pool.size + align, GFP_NOWAIT);
		d40d->lli_pool.base = base;

		if (d40d->lli_pool.base == NULL)
			return -ENOMEM;
	}

	if (is_log) {
		d40d->lli_log.src = PTR_ALIGN(base, align);
		d40d->lli_log.dst = d40d->lli_log.src + lli_len;

		d40d->lli_pool.dma_addr = 0;
	} else {
		d40d->lli_phy.src = PTR_ALIGN(base, align);
		d40d->lli_phy.dst = d40d->lli_phy.src + lli_len;

		d40d->lli_pool.dma_addr = dma_map_single(d40c->base->dev,
							 d40d->lli_phy.src,
							 d40d->lli_pool.size,
							 DMA_TO_DEVICE);

		if (dma_mapping_error(d40c->base->dev,
				      d40d->lli_pool.dma_addr)) {
			kfree(d40d->lli_pool.base);
			d40d->lli_pool.base = NULL;
			d40d->lli_pool.dma_addr = 0;
			return -ENOMEM;
		}
	}

	return 0;
}

static void d40_pool_lli_free(struct d40_chan *d40c, struct d40_desc *d40d)
{
	if (d40d->lli_pool.dma_addr)
		dma_unmap_single(d40c->base->dev, d40d->lli_pool.dma_addr,
				 d40d->lli_pool.size, DMA_TO_DEVICE);

	kfree(d40d->lli_pool.base);
	d40d->lli_pool.base = NULL;
	d40d->lli_pool.size = 0;
	d40d->lli_log.src = NULL;
	d40d->lli_log.dst = NULL;
	d40d->lli_phy.src = NULL;
	d40d->lli_phy.dst = NULL;
}

static int d40_lcla_alloc_one(struct d40_chan *d40c,
			      struct d40_desc *d40d)
{
	unsigned long flags;
	int i;
	int ret = -EINVAL;

	spin_lock_irqsave(&d40c->base->lcla_pool.lock, flags);

	/*
	 * Allocate both src and dst at the same time, therefore the half
	 * start on 1 since 0 can't be used since zero is used as end marker.
	 */
	for (i = 1 ; i < D40_LCLA_LINK_PER_EVENT_GRP / 2; i++) {
		int idx = d40c->phy_chan->num * D40_LCLA_LINK_PER_EVENT_GRP + i;

		if (!d40c->base->lcla_pool.alloc_map[idx]) {
			d40c->base->lcla_pool.alloc_map[idx] = d40d;
			d40d->lcla_alloc++;
			ret = i;
			break;
		}
	}

	spin_unlock_irqrestore(&d40c->base->lcla_pool.lock, flags);

	return ret;
}

static int d40_lcla_free_all(struct d40_chan *d40c,
			     struct d40_desc *d40d)
{
	unsigned long flags;
	int i;
	int ret = -EINVAL;

	if (chan_is_physical(d40c))
		return 0;

	spin_lock_irqsave(&d40c->base->lcla_pool.lock, flags);

	for (i = 1 ; i < D40_LCLA_LINK_PER_EVENT_GRP / 2; i++) {
		int idx = d40c->phy_chan->num * D40_LCLA_LINK_PER_EVENT_GRP + i;

		if (d40c->base->lcla_pool.alloc_map[idx] == d40d) {
			d40c->base->lcla_pool.alloc_map[idx] = NULL;
			d40d->lcla_alloc--;
			if (d40d->lcla_alloc == 0) {
				ret = 0;
				break;
			}
		}
	}

	spin_unlock_irqrestore(&d40c->base->lcla_pool.lock, flags);

	return ret;

}

static void d40_desc_remove(struct d40_desc *d40d)
{
	list_del(&d40d->node);
}

static struct d40_desc *d40_desc_get(struct d40_chan *d40c)
{
	struct d40_desc *desc = NULL;

	if (!list_empty(&d40c->client)) {
		struct d40_desc *d;
		struct d40_desc *_d;

		list_for_each_entry_safe(d, _d, &d40c->client, node) {
			if (async_tx_test_ack(&d->txd)) {
				d40_desc_remove(d);
				desc = d;
				memset(desc, 0, sizeof(*desc));
				break;
			}
		}
	}

	if (!desc)
		desc = kmem_cache_zalloc(d40c->base->desc_slab, GFP_NOWAIT);

	if (desc)
		INIT_LIST_HEAD(&desc->node);

	return desc;
}

static void d40_desc_free(struct d40_chan *d40c, struct d40_desc *d40d)
{

	d40_pool_lli_free(d40c, d40d);
	d40_lcla_free_all(d40c, d40d);
	kmem_cache_free(d40c->base->desc_slab, d40d);
}

static void d40_desc_submit(struct d40_chan *d40c, struct d40_desc *desc)
{
	list_add_tail(&desc->node, &d40c->active);
}

static void d40_phy_lli_load(struct d40_chan *chan, struct d40_desc *desc)
{
	struct d40_phy_lli *lli_dst = desc->lli_phy.dst;
	struct d40_phy_lli *lli_src = desc->lli_phy.src;
	void __iomem *base = chan_base(chan);

	writel(lli_src->reg_cfg, base + D40_CHAN_REG_SSCFG);
	writel(lli_src->reg_elt, base + D40_CHAN_REG_SSELT);
	writel(lli_src->reg_ptr, base + D40_CHAN_REG_SSPTR);
	writel(lli_src->reg_lnk, base + D40_CHAN_REG_SSLNK);

	writel(lli_dst->reg_cfg, base + D40_CHAN_REG_SDCFG);
	writel(lli_dst->reg_elt, base + D40_CHAN_REG_SDELT);
	writel(lli_dst->reg_ptr, base + D40_CHAN_REG_SDPTR);
	writel(lli_dst->reg_lnk, base + D40_CHAN_REG_SDLNK);
}

static void d40_desc_done(struct d40_chan *d40c, struct d40_desc *desc)
{
	list_add_tail(&desc->node, &d40c->done);
}

static void d40_log_lli_to_lcxa(struct d40_chan *chan, struct d40_desc *desc)
{
	struct d40_lcla_pool *pool = &chan->base->lcla_pool;
	struct d40_log_lli_bidir *lli = &desc->lli_log;
	int lli_current = desc->lli_current;
	int lli_len = desc->lli_len;
	bool cyclic = desc->cyclic;
	int curr_lcla = -EINVAL;
	int first_lcla = 0;
	bool use_esram_lcla = chan->base->plat_data->use_esram_lcla;
	bool linkback;

	/*
	 * We may have partially running cyclic transfers, in case we did't get
	 * enough LCLA entries.
	 */
	linkback = cyclic && lli_current == 0;

	/*
	 * For linkback, we need one LCLA even with only one link, because we
	 * can't link back to the one in LCPA space
	 */
	if (linkback || (lli_len - lli_current > 1)) {
		/*
		 * If the channel is expected to use only soft_lli don't
		 * allocate a lcla. This is to avoid a HW issue that exists
		 * in some controller during a peripheral to memory transfer
		 * that uses linked lists.
		 */
		if (!(chan->phy_chan->use_soft_lli &&
			chan->dma_cfg.dir == DMA_DEV_TO_MEM))
			curr_lcla = d40_lcla_alloc_one(chan, desc);

		first_lcla = curr_lcla;
	}

	/*
	 * For linkback, we normally load the LCPA in the loop since we need to
	 * link it to the second LCLA and not the first.  However, if we
	 * couldn't even get a first LCLA, then we have to run in LCPA and
	 * reload manually.
	 */
	if (!linkback || curr_lcla == -EINVAL) {
		unsigned int flags = 0;

		if (curr_lcla == -EINVAL)
			flags |= LLI_TERM_INT;

		d40_log_lli_lcpa_write(chan->lcpa,
				       &lli->dst[lli_current],
				       &lli->src[lli_current],
				       curr_lcla,
				       flags);
		lli_current++;
	}

	if (curr_lcla < 0)
		goto set_current;

	for (; lli_current < lli_len; lli_current++) {
		unsigned int lcla_offset = chan->phy_chan->num * 1024 +
					   8 * curr_lcla * 2;
		struct d40_log_lli *lcla = pool->base + lcla_offset;
		unsigned int flags = 0;
		int next_lcla;

		if (lli_current + 1 < lli_len)
			next_lcla = d40_lcla_alloc_one(chan, desc);
		else
			next_lcla = linkback ? first_lcla : -EINVAL;

		if (cyclic || next_lcla == -EINVAL)
			flags |= LLI_TERM_INT;

		if (linkback && curr_lcla == first_lcla) {
			/* First link goes in both LCPA and LCLA */
			d40_log_lli_lcpa_write(chan->lcpa,
					       &lli->dst[lli_current],
					       &lli->src[lli_current],
					       next_lcla, flags);
		}

		/*
		 * One unused LCLA in the cyclic case if the very first
		 * next_lcla fails...
		 */
		d40_log_lli_lcla_write(lcla,
				       &lli->dst[lli_current],
				       &lli->src[lli_current],
				       next_lcla, flags);

		/*
		 * Cache maintenance is not needed if lcla is
		 * mapped in esram
		 */
		if (!use_esram_lcla) {
			dma_sync_single_range_for_device(chan->base->dev,
						pool->dma_addr, lcla_offset,
						2 * sizeof(struct d40_log_lli),
						DMA_TO_DEVICE);
		}
		curr_lcla = next_lcla;

		if (curr_lcla == -EINVAL || curr_lcla == first_lcla) {
			lli_current++;
			break;
		}
	}
 set_current:
	desc->lli_current = lli_current;
}

static void d40_desc_load(struct d40_chan *d40c, struct d40_desc *d40d)
{
	if (chan_is_physical(d40c)) {
		d40_phy_lli_load(d40c, d40d);
		d40d->lli_current = d40d->lli_len;
	} else
		d40_log_lli_to_lcxa(d40c, d40d);
}

static struct d40_desc *d40_first_active_get(struct d40_chan *d40c)
{
	return list_first_entry_or_null(&d40c->active, struct d40_desc, node);
}

/* remove desc from current queue and add it to the pending_queue */
static void d40_desc_queue(struct d40_chan *d40c, struct d40_desc *desc)
{
	d40_desc_remove(desc);
	desc->is_in_client_list = false;
	list_add_tail(&desc->node, &d40c->pending_queue);
}

static struct d40_desc *d40_first_pending(struct d40_chan *d40c)
{
	return list_first_entry_or_null(&d40c->pending_queue, struct d40_desc,
					node);
}

static struct d40_desc *d40_first_queued(struct d40_chan *d40c)
{
	return list_first_entry_or_null(&d40c->queue, struct d40_desc, node);
}

static struct d40_desc *d40_first_done(struct d40_chan *d40c)
{
	return list_first_entry_or_null(&d40c->done, struct d40_desc, node);
}

static int d40_psize_2_burst_size(bool is_log, int psize)
{
	if (is_log) {
		if (psize == STEDMA40_PSIZE_LOG_1)
			return 1;
	} else {
		if (psize == STEDMA40_PSIZE_PHY_1)
			return 1;
	}

	return 2 << psize;
}

/*
 * The dma only supports transmitting packages up to
 * STEDMA40_MAX_SEG_SIZE * data_width, where data_width is stored in Bytes.
 *
 * Calculate the total number of dma elements required to send the entire sg list.
 */
static int d40_size_2_dmalen(int size, u32 data_width1, u32 data_width2)
{
	int dmalen;
	u32 max_w = max(data_width1, data_width2);
	u32 min_w = min(data_width1, data_width2);
	u32 seg_max = ALIGN(STEDMA40_MAX_SEG_SIZE * min_w, max_w);

	if (seg_max > STEDMA40_MAX_SEG_SIZE)
		seg_max -= max_w;

	if (!IS_ALIGNED(size, max_w))
		return -EINVAL;

	if (size <= seg_max)
		dmalen = 1;
	else {
		dmalen = size / seg_max;
		if (dmalen * seg_max < size)
			dmalen++;
	}
	return dmalen;
}

static int d40_sg_2_dmalen(struct scatterlist *sgl, int sg_len,
			   u32 data_width1, u32 data_width2)
{
	struct scatterlist *sg;
	int i;
	int len = 0;
	int ret;

	for_each_sg(sgl, sg, sg_len, i) {
		ret = d40_size_2_dmalen(sg_dma_len(sg),
					data_width1, data_width2);
		if (ret < 0)
			return ret;
		len += ret;
	}
	return len;
}

static int __d40_execute_command_phy(struct d40_chan *d40c,
				     enum d40_command command)
{
	u32 status;
	int i;
	void __iomem *active_reg;
	int ret = 0;
	unsigned long flags;
	u32 wmask;

	if (command == D40_DMA_STOP) {
		ret = __d40_execute_command_phy(d40c, D40_DMA_SUSPEND_REQ);
		if (ret)
			return ret;
	}

	spin_lock_irqsave(&d40c->base->execmd_lock, flags);

	if (d40c->phy_chan->num % 2 == 0)
		active_reg = d40c->base->virtbase + D40_DREG_ACTIVE;
	else
		active_reg = d40c->base->virtbase + D40_DREG_ACTIVO;

	if (command == D40_DMA_SUSPEND_REQ) {
		status = (readl(active_reg) &
			  D40_CHAN_POS_MASK(d40c->phy_chan->num)) >>
			D40_CHAN_POS(d40c->phy_chan->num);

		if (status == D40_DMA_SUSPENDED || status == D40_DMA_STOP)
			goto unlock;
	}

	wmask = 0xffffffff & ~(D40_CHAN_POS_MASK(d40c->phy_chan->num));
	writel(wmask | (command << D40_CHAN_POS(d40c->phy_chan->num)),
	       active_reg);

	if (command == D40_DMA_SUSPEND_REQ) {

		for (i = 0 ; i < D40_SUSPEND_MAX_IT; i++) {
			status = (readl(active_reg) &
				  D40_CHAN_POS_MASK(d40c->phy_chan->num)) >>
				D40_CHAN_POS(d40c->phy_chan->num);

			cpu_relax();
			/*
			 * Reduce the number of bus accesses while
			 * waiting for the DMA to suspend.
			 */
			udelay(3);

			if (status == D40_DMA_STOP ||
			    status == D40_DMA_SUSPENDED)
				break;
		}

		if (i == D40_SUSPEND_MAX_IT) {
			chan_err(d40c,
				"unable to suspend the chl %d (log: %d) status %x\n",
				d40c->phy_chan->num, d40c->log_num,
				status);
			dump_stack();
			ret = -EBUSY;
		}

	}
 unlock:
	spin_unlock_irqrestore(&d40c->base->execmd_lock, flags);
	return ret;
}

static void d40_term_all(struct d40_chan *d40c)
{
	struct d40_desc *d40d;
	struct d40_desc *_d;

	/* Release completed descriptors */
	while ((d40d = d40_first_done(d40c))) {
		d40_desc_remove(d40d);
		d40_desc_free(d40c, d40d);
	}

	/* Release active descriptors */
	while ((d40d = d40_first_active_get(d40c))) {
		d40_desc_remove(d40d);
		d40_desc_free(d40c, d40d);
	}

	/* Release queued descriptors waiting for transfer */
	while ((d40d = d40_first_queued(d40c))) {
		d40_desc_remove(d40d);
		d40_desc_free(d40c, d40d);
	}

	/* Release pending descriptors */
	while ((d40d = d40_first_pending(d40c))) {
		d40_desc_remove(d40d);
		d40_desc_free(d40c, d40d);
	}

	/* Release client owned descriptors */
	if (!list_empty(&d40c->client))
		list_for_each_entry_safe(d40d, _d, &d40c->client, node) {
			d40_desc_remove(d40d);
			d40_desc_free(d40c, d40d);
		}

	/* Release descriptors in prepare queue */
	if (!list_empty(&d40c->prepare_queue))
		list_for_each_entry_safe(d40d, _d,
					 &d40c->prepare_queue, node) {
			d40_desc_remove(d40d);
			d40_desc_free(d40c, d40d);
		}

	d40c->pending_tx = 0;
}

static void __d40_config_set_event(struct d40_chan *d40c,
				   enum d40_events event_type, u32 event,
				   int reg)
{
	void __iomem *addr = chan_base(d40c) + reg;
	int tries;
	u32 status;

	switch (event_type) {

	case D40_DEACTIVATE_EVENTLINE:

		writel((D40_DEACTIVATE_EVENTLINE << D40_EVENTLINE_POS(event))
		       | ~D40_EVENTLINE_MASK(event), addr);
		break;

	case D40_SUSPEND_REQ_EVENTLINE:
		status = (readl(addr) & D40_EVENTLINE_MASK(event)) >>
			  D40_EVENTLINE_POS(event);

		if (status == D40_DEACTIVATE_EVENTLINE ||
		    status == D40_SUSPEND_REQ_EVENTLINE)
			break;

		writel((D40_SUSPEND_REQ_EVENTLINE << D40_EVENTLINE_POS(event))
		       | ~D40_EVENTLINE_MASK(event), addr);

		for (tries = 0 ; tries < D40_SUSPEND_MAX_IT; tries++) {

			status = (readl(addr) & D40_EVENTLINE_MASK(event)) >>
				  D40_EVENTLINE_POS(event);

			cpu_relax();
			/*
			 * Reduce the number of bus accesses while
			 * waiting for the DMA to suspend.
			 */
			udelay(3);

			if (status == D40_DEACTIVATE_EVENTLINE)
				break;
		}

		if (tries == D40_SUSPEND_MAX_IT) {
			chan_err(d40c,
				"unable to stop the event_line chl %d (log: %d)"
				"status %x\n", d40c->phy_chan->num,
				 d40c->log_num, status);
		}
		break;

	case D40_ACTIVATE_EVENTLINE:
	/*
	 * The hardware sometimes doesn't register the enable when src and dst
	 * event lines are active on the same logical channel.  Retry to ensure
	 * it does.  Usually only one retry is sufficient.
	 */
		tries = 100;
		while (--tries) {
			writel((D40_ACTIVATE_EVENTLINE <<
				D40_EVENTLINE_POS(event)) |
				~D40_EVENTLINE_MASK(event), addr);

			if (readl(addr) & D40_EVENTLINE_MASK(event))
				break;
		}

		if (tries != 99)
			dev_dbg(chan2dev(d40c),
				"[%s] workaround enable S%cLNK (%d tries)\n",
				__func__, reg == D40_CHAN_REG_SSLNK ? 'S' : 'D',
				100 - tries);

		WARN_ON(!tries);
		break;

	case D40_ROUND_EVENTLINE:
		BUG();
		break;

	}
}

static void d40_config_set_event(struct d40_chan *d40c,
				 enum d40_events event_type)
{
	u32 event = D40_TYPE_TO_EVENT(d40c->dma_cfg.dev_type);

	/* Enable event line connected to device (or memcpy) */
	if ((d40c->dma_cfg.dir == DMA_DEV_TO_MEM) ||
	    (d40c->dma_cfg.dir == DMA_DEV_TO_DEV))
		__d40_config_set_event(d40c, event_type, event,
				       D40_CHAN_REG_SSLNK);

	if (d40c->dma_cfg.dir !=  DMA_DEV_TO_MEM)
		__d40_config_set_event(d40c, event_type, event,
				       D40_CHAN_REG_SDLNK);
}

static u32 d40_chan_has_events(struct d40_chan *d40c)
{
	void __iomem *chanbase = chan_base(d40c);
	u32 val;

	val = readl(chanbase + D40_CHAN_REG_SSLNK);
	val |= readl(chanbase + D40_CHAN_REG_SDLNK);

	return val;
}

static int
__d40_execute_command_log(struct d40_chan *d40c, enum d40_command command)
{
	unsigned long flags;
	int ret = 0;
	u32 active_status;
	void __iomem *active_reg;

	if (d40c->phy_chan->num % 2 == 0)
		active_reg = d40c->base->virtbase + D40_DREG_ACTIVE;
	else
		active_reg = d40c->base->virtbase + D40_DREG_ACTIVO;


	spin_lock_irqsave(&d40c->phy_chan->lock, flags);

	switch (command) {
	case D40_DMA_STOP:
	case D40_DMA_SUSPEND_REQ:

		active_status = (readl(active_reg) &
				 D40_CHAN_POS_MASK(d40c->phy_chan->num)) >>
				 D40_CHAN_POS(d40c->phy_chan->num);

		if (active_status == D40_DMA_RUN)
			d40_config_set_event(d40c, D40_SUSPEND_REQ_EVENTLINE);
		else
			d40_config_set_event(d40c, D40_DEACTIVATE_EVENTLINE);

		if (!d40_chan_has_events(d40c) && (command == D40_DMA_STOP))
			ret = __d40_execute_command_phy(d40c, command);

		break;

	case D40_DMA_RUN:

		d40_config_set_event(d40c, D40_ACTIVATE_EVENTLINE);
		ret = __d40_execute_command_phy(d40c, command);
		break;

	case D40_DMA_SUSPENDED:
		BUG();
		break;
	}

	spin_unlock_irqrestore(&d40c->phy_chan->lock, flags);
	return ret;
}

static int d40_channel_execute_command(struct d40_chan *d40c,
				       enum d40_command command)
{
	if (chan_is_logical(d40c))
		return __d40_execute_command_log(d40c, command);
	else
		return __d40_execute_command_phy(d40c, command);
}

static u32 d40_get_prmo(struct d40_chan *d40c)
{
	static const unsigned int phy_map[] = {
		[STEDMA40_PCHAN_BASIC_MODE]
			= D40_DREG_PRMO_PCHAN_BASIC,
		[STEDMA40_PCHAN_MODULO_MODE]
			= D40_DREG_PRMO_PCHAN_MODULO,
		[STEDMA40_PCHAN_DOUBLE_DST_MODE]
			= D40_DREG_PRMO_PCHAN_DOUBLE_DST,
	};
	static const unsigned int log_map[] = {
		[STEDMA40_LCHAN_SRC_PHY_DST_LOG]
			= D40_DREG_PRMO_LCHAN_SRC_PHY_DST_LOG,
		[STEDMA40_LCHAN_SRC_LOG_DST_PHY]
			= D40_DREG_PRMO_LCHAN_SRC_LOG_DST_PHY,
		[STEDMA40_LCHAN_SRC_LOG_DST_LOG]
			= D40_DREG_PRMO_LCHAN_SRC_LOG_DST_LOG,
	};

	if (chan_is_physical(d40c))
		return phy_map[d40c->dma_cfg.mode_opt];
	else
		return log_map[d40c->dma_cfg.mode_opt];
}

static void d40_config_write(struct d40_chan *d40c)
{
	u32 addr_base;
	u32 var;

	/* Odd addresses are even addresses + 4 */
	addr_base = (d40c->phy_chan->num % 2) * 4;
	/* Setup channel mode to logical or physical */
	var = ((u32)(chan_is_logical(d40c)) + 1) <<
		D40_CHAN_POS(d40c->phy_chan->num);
	writel(var, d40c->base->virtbase + D40_DREG_PRMSE + addr_base);

	/* Setup operational mode option register */
	var = d40_get_prmo(d40c) << D40_CHAN_POS(d40c->phy_chan->num);

	writel(var, d40c->base->virtbase + D40_DREG_PRMOE + addr_base);

	if (chan_is_logical(d40c)) {
		int lidx = (d40c->phy_chan->num << D40_SREG_ELEM_LOG_LIDX_POS)
			   & D40_SREG_ELEM_LOG_LIDX_MASK;
		void __iomem *chanbase = chan_base(d40c);

		/* Set default config for CFG reg */
		writel(d40c->src_def_cfg, chanbase + D40_CHAN_REG_SSCFG);
		writel(d40c->dst_def_cfg, chanbase + D40_CHAN_REG_SDCFG);

		/* Set LIDX for lcla */
		writel(lidx, chanbase + D40_CHAN_REG_SSELT);
		writel(lidx, chanbase + D40_CHAN_REG_SDELT);

		/* Clear LNK which will be used by d40_chan_has_events() */
		writel(0, chanbase + D40_CHAN_REG_SSLNK);
		writel(0, chanbase + D40_CHAN_REG_SDLNK);
	}
}

static u32 d40_residue(struct d40_chan *d40c)
{
	u32 num_elt;

	if (chan_is_logical(d40c))
		num_elt = (readl(&d40c->lcpa->lcsp2) & D40_MEM_LCSP2_ECNT_MASK)
			>> D40_MEM_LCSP2_ECNT_POS;
	else {
		u32 val = readl(chan_base(d40c) + D40_CHAN_REG_SDELT);
		num_elt = (val & D40_SREG_ELEM_PHY_ECNT_MASK)
			  >> D40_SREG_ELEM_PHY_ECNT_POS;
	}

	return num_elt * d40c->dma_cfg.dst_info.data_width;
}

static bool d40_tx_is_linked(struct d40_chan *d40c)
{
	bool is_link;

	if (chan_is_logical(d40c))
		is_link = readl(&d40c->lcpa->lcsp3) &  D40_MEM_LCSP3_DLOS_MASK;
	else
		is_link = readl(chan_base(d40c) + D40_CHAN_REG_SDLNK)
			  & D40_SREG_LNK_PHYS_LNK_MASK;

	return is_link;
}

static int d40_pause(struct dma_chan *chan)
{
	struct d40_chan *d40c = container_of(chan, struct d40_chan, chan);
	int res = 0;
	unsigned long flags;

	if (d40c->phy_chan == NULL) {
		chan_err(d40c, "Channel is not allocated!\n");
		return -EINVAL;
	}

	if (!d40c->busy)
		return 0;

	spin_lock_irqsave(&d40c->lock, flags);
	pm_runtime_get_sync(d40c->base->dev);

	res = d40_channel_execute_command(d40c, D40_DMA_SUSPEND_REQ);

	pm_runtime_mark_last_busy(d40c->base->dev);
	pm_runtime_put_autosuspend(d40c->base->dev);
	spin_unlock_irqrestore(&d40c->lock, flags);
	return res;
}

static int d40_resume(struct dma_chan *chan)
{
	struct d40_chan *d40c = container_of(chan, struct d40_chan, chan);
	int res = 0;
	unsigned long flags;

	if (d40c->phy_chan == NULL) {
		chan_err(d40c, "Channel is not allocated!\n");
		return -EINVAL;
	}

	if (!d40c->busy)
		return 0;

	spin_lock_irqsave(&d40c->lock, flags);
	pm_runtime_get_sync(d40c->base->dev);

	/* If bytes left to transfer or linked tx resume job */
	if (d40_residue(d40c) || d40_tx_is_linked(d40c))
		res = d40_channel_execute_command(d40c, D40_DMA_RUN);

	pm_runtime_mark_last_busy(d40c->base->dev);
	pm_runtime_put_autosuspend(d40c->base->dev);
	spin_unlock_irqrestore(&d40c->lock, flags);
	return res;
}

static dma_cookie_t d40_tx_submit(struct dma_async_tx_descriptor *tx)
{
	struct d40_chan *d40c = container_of(tx->chan,
					     struct d40_chan,
					     chan);
	struct d40_desc *d40d = container_of(tx, struct d40_desc, txd);
	unsigned long flags;
	dma_cookie_t cookie;

	spin_lock_irqsave(&d40c->lock, flags);
	cookie = dma_cookie_assign(tx);
	d40_desc_queue(d40c, d40d);
	spin_unlock_irqrestore(&d40c->lock, flags);

	return cookie;
}

static int d40_start(struct d40_chan *d40c)
{
	return d40_channel_execute_command(d40c, D40_DMA_RUN);
}

static struct d40_desc *d40_queue_start(struct d40_chan *d40c)
{
	struct d40_desc *d40d;
	int err;

	/* Start queued jobs, if any */
	d40d = d40_first_queued(d40c);

	if (d40d != NULL) {
		if (!d40c->busy) {
			d40c->busy = true;
			pm_runtime_get_sync(d40c->base->dev);
		}

		/* Remove from queue */
		d40_desc_remove(d40d);

		/* Add to active queue */
		d40_desc_submit(d40c, d40d);

		/* Initiate DMA job */
		d40_desc_load(d40c, d40d);

		/* Start dma job */
		err = d40_start(d40c);

		if (err)
			return NULL;
	}

	return d40d;
}

/* called from interrupt context */
static void dma_tc_handle(struct d40_chan *d40c)
{
	struct d40_desc *d40d;

	/* Get first active entry from list */
	d40d = d40_first_active_get(d40c);

	if (d40d == NULL)
		return;

	if (d40d->cyclic) {
		/*
		 * If this was a paritially loaded list, we need to reloaded
		 * it, and only when the list is completed.  We need to check
		 * for done because the interrupt will hit for every link, and
		 * not just the last one.
		 */
		if (d40d->lli_current < d40d->lli_len
		    && !d40_tx_is_linked(d40c)
		    && !d40_residue(d40c)) {
			d40_lcla_free_all(d40c, d40d);
			d40_desc_load(d40c, d40d);
			(void) d40_start(d40c);

			if (d40d->lli_current == d40d->lli_len)
				d40d->lli_current = 0;
		}
	} else {
		d40_lcla_free_all(d40c, d40d);

		if (d40d->lli_current < d40d->lli_len) {
			d40_desc_load(d40c, d40d);
			/* Start dma job */
			(void) d40_start(d40c);
			return;
		}

		if (d40_queue_start(d40c) == NULL) {
			d40c->busy = false;

			pm_runtime_mark_last_busy(d40c->base->dev);
			pm_runtime_put_autosuspend(d40c->base->dev);
		}

		d40_desc_remove(d40d);
		d40_desc_done(d40c, d40d);
	}

	d40c->pending_tx++;
	tasklet_schedule(&d40c->tasklet);

}

static void dma_tasklet(unsigned long data)
{
	struct d40_chan *d40c = (struct d40_chan *) data;
	struct d40_desc *d40d;
	unsigned long flags;
	bool callback_active;
	struct dmaengine_desc_callback cb;

	spin_lock_irqsave(&d40c->lock, flags);

	/* Get first entry from the done list */
	d40d = d40_first_done(d40c);
	if (d40d == NULL) {
		/* Check if we have reached here for cyclic job */
		d40d = d40_first_active_get(d40c);
		if (d40d == NULL || !d40d->cyclic)
			goto check_pending_tx;
	}

	if (!d40d->cyclic)
		dma_cookie_complete(&d40d->txd);

	/*
	 * If terminating a channel pending_tx is set to zero.
	 * This prevents any finished active jobs to return to the client.
	 */
	if (d40c->pending_tx == 0) {
		spin_unlock_irqrestore(&d40c->lock, flags);
		return;
	}

	/* Callback to client */
	callback_active = !!(d40d->txd.flags & DMA_PREP_INTERRUPT);
	dmaengine_desc_get_callback(&d40d->txd, &cb);

	if (!d40d->cyclic) {
		if (async_tx_test_ack(&d40d->txd)) {
			d40_desc_remove(d40d);
			d40_desc_free(d40c, d40d);
		} else if (!d40d->is_in_client_list) {
			d40_desc_remove(d40d);
			d40_lcla_free_all(d40c, d40d);
			list_add_tail(&d40d->node, &d40c->client);
			d40d->is_in_client_list = true;
		}
	}

	d40c->pending_tx--;

	if (d40c->pending_tx)
		tasklet_schedule(&d40c->tasklet);

	spin_unlock_irqrestore(&d40c->lock, flags);

	if (callback_active)
		dmaengine_desc_callback_invoke(&cb, NULL);

	return;
 check_pending_tx:
	/* Rescue manouver if receiving double interrupts */
	if (d40c->pending_tx > 0)
		d40c->pending_tx--;
	spin_unlock_irqrestore(&d40c->lock, flags);
}

static irqreturn_t d40_handle_interrupt(int irq, void *data)
{
	int i;
	u32 idx;
	u32 row;
	long chan = -1;
	struct d40_chan *d40c;
	unsigned long flags;
	struct d40_base *base = data;
	u32 *regs = base->regs_interrupt;
	struct d40_interrupt_lookup *il = base->gen_dmac.il;
	u32 il_size = base->gen_dmac.il_size;

	spin_lock_irqsave(&base->interrupt_lock, flags);

	/* Read interrupt status of both logical and physical channels */
	for (i = 0; i < il_size; i++)
		regs[i] = readl(base->virtbase + il[i].src);

	for (;;) {

		chan = find_next_bit((unsigned long *)regs,
				     BITS_PER_LONG * il_size, chan + 1);

		/* No more set bits found? */
		if (chan == BITS_PER_LONG * il_size)
			break;

		row = chan / BITS_PER_LONG;
		idx = chan & (BITS_PER_LONG - 1);

		if (il[row].offset == D40_PHY_CHAN)
			d40c = base->lookup_phy_chans[idx];
		else
			d40c = base->lookup_log_chans[il[row].offset + idx];

		if (!d40c) {
			/*
			 * No error because this can happen if something else
			 * in the system is using the channel.
			 */
			continue;
		}

		/* ACK interrupt */
		writel(BIT(idx), base->virtbase + il[row].clr);

		spin_lock(&d40c->lock);

		if (!il[row].is_error)
			dma_tc_handle(d40c);
		else
			d40_err(base->dev, "IRQ chan: %ld offset %d idx %d\n",
				chan, il[row].offset, idx);

		spin_unlock(&d40c->lock);
	}

	spin_unlock_irqrestore(&base->interrupt_lock, flags);

	return IRQ_HANDLED;
}

static int d40_validate_conf(struct d40_chan *d40c,
			     struct stedma40_chan_cfg *conf)
{
	int res = 0;
	bool is_log = conf->mode == STEDMA40_MODE_LOGICAL;

	if (!conf->dir) {
		chan_err(d40c, "Invalid direction.\n");
		res = -EINVAL;
	}

	if ((is_log && conf->dev_type > d40c->base->num_log_chans)  ||
	    (!is_log && conf->dev_type > d40c->base->num_phy_chans) ||
	    (conf->dev_type < 0)) {
		chan_err(d40c, "Invalid device type (%d)\n", conf->dev_type);
		res = -EINVAL;
	}

	if (conf->dir == DMA_DEV_TO_DEV) {
		/*
		 * DMAC HW supports it. Will be added to this driver,
		 * in case any dma client requires it.
		 */
		chan_err(d40c, "periph to periph not supported\n");
		res = -EINVAL;
	}

	if (d40_psize_2_burst_size(is_log, conf->src_info.psize) *
	    conf->src_info.data_width !=
	    d40_psize_2_burst_size(is_log, conf->dst_info.psize) *
	    conf->dst_info.data_width) {
		/*
		 * The DMAC hardware only supports
		 * src (burst x width) == dst (burst x width)
		 */

		chan_err(d40c, "src (burst x width) != dst (burst x width)\n");
		res = -EINVAL;
	}

	return res;
}

static bool d40_alloc_mask_set(struct d40_phy_res *phy,
			       bool is_src, int log_event_line, bool is_log,
			       bool *first_user)
{
	unsigned long flags;
	spin_lock_irqsave(&phy->lock, flags);

	*first_user = ((phy->allocated_src | phy->allocated_dst)
			== D40_ALLOC_FREE);

	if (!is_log) {
		/* Physical interrupts are masked per physical full channel */
		if (phy->allocated_src == D40_ALLOC_FREE &&
		    phy->allocated_dst == D40_ALLOC_FREE) {
			phy->allocated_dst = D40_ALLOC_PHY;
			phy->allocated_src = D40_ALLOC_PHY;
			goto found_unlock;
		} else
			goto not_found_unlock;
	}

	/* Logical channel */
	if (is_src) {
		if (phy->allocated_src == D40_ALLOC_PHY)
			goto not_found_unlock;

		if (phy->allocated_src == D40_ALLOC_FREE)
			phy->allocated_src = D40_ALLOC_LOG_FREE;

		if (!(phy->allocated_src & BIT(log_event_line))) {
			phy->allocated_src |= BIT(log_event_line);
			goto found_unlock;
		} else
			goto not_found_unlock;
	} else {
		if (phy->allocated_dst == D40_ALLOC_PHY)
			goto not_found_unlock;

		if (phy->allocated_dst == D40_ALLOC_FREE)
			phy->allocated_dst = D40_ALLOC_LOG_FREE;

		if (!(phy->allocated_dst & BIT(log_event_line))) {
			phy->allocated_dst |= BIT(log_event_line);
			goto found_unlock;
		}
	}
 not_found_unlock:
	spin_unlock_irqrestore(&phy->lock, flags);
	return false;
 found_unlock:
	spin_unlock_irqrestore(&phy->lock, flags);
	return true;
}

static bool d40_alloc_mask_free(struct d40_phy_res *phy, bool is_src,
			       int log_event_line)
{
	unsigned long flags;
	bool is_free = false;

	spin_lock_irqsave(&phy->lock, flags);
	if (!log_event_line) {
		phy->allocated_dst = D40_ALLOC_FREE;
		phy->allocated_src = D40_ALLOC_FREE;
		is_free = true;
		goto unlock;
	}

	/* Logical channel */
	if (is_src) {
		phy->allocated_src &= ~BIT(log_event_line);
		if (phy->allocated_src == D40_ALLOC_LOG_FREE)
			phy->allocated_src = D40_ALLOC_FREE;
	} else {
		phy->allocated_dst &= ~BIT(log_event_line);
		if (phy->allocated_dst == D40_ALLOC_LOG_FREE)
			phy->allocated_dst = D40_ALLOC_FREE;
	}

	is_free = ((phy->allocated_src | phy->allocated_dst) ==
		   D40_ALLOC_FREE);
 unlock:
	spin_unlock_irqrestore(&phy->lock, flags);

	return is_free;
}

static int d40_allocate_channel(struct d40_chan *d40c, bool *first_phy_user)
{
	int dev_type = d40c->dma_cfg.dev_type;
	int event_group;
	int event_line;
	struct d40_phy_res *phys;
	int i;
	int j;
	int log_num;
	int num_phy_chans;
	bool is_src;
	bool is_log = d40c->dma_cfg.mode == STEDMA40_MODE_LOGICAL;

	phys = d40c->base->phy_res;
	num_phy_chans = d40c->base->num_phy_chans;

	if (d40c->dma_cfg.dir == DMA_DEV_TO_MEM) {
		log_num = 2 * dev_type;
		is_src = true;
	} else if (d40c->dma_cfg.dir == DMA_MEM_TO_DEV ||
		   d40c->dma_cfg.dir == DMA_MEM_TO_MEM) {
		/* dst event lines are used for logical memcpy */
		log_num = 2 * dev_type + 1;
		is_src = false;
	} else
		return -EINVAL;

	event_group = D40_TYPE_TO_GROUP(dev_type);
	event_line = D40_TYPE_TO_EVENT(dev_type);

	if (!is_log) {
		if (d40c->dma_cfg.dir == DMA_MEM_TO_MEM) {
			/* Find physical half channel */
			if (d40c->dma_cfg.use_fixed_channel) {
				i = d40c->dma_cfg.phy_channel;
				if (d40_alloc_mask_set(&phys[i], is_src,
						       0, is_log,
						       first_phy_user))
					goto found_phy;
			} else {
				for (i = 0; i < num_phy_chans; i++) {
					if (d40_alloc_mask_set(&phys[i], is_src,
						       0, is_log,
						       first_phy_user))
						goto found_phy;
				}
			}
		} else
			for (j = 0; j < d40c->base->num_phy_chans; j += 8) {
				int phy_num = j  + event_group * 2;
				for (i = phy_num; i < phy_num + 2; i++) {
					if (d40_alloc_mask_set(&phys[i],
							       is_src,
							       0,
							       is_log,
							       first_phy_user))
						goto found_phy;
				}
			}
		return -EINVAL;
found_phy:
		d40c->phy_chan = &phys[i];
		d40c->log_num = D40_PHY_CHAN;
		goto out;
	}
	if (dev_type == -1)
		return -EINVAL;

	/* Find logical channel */
	for (j = 0; j < d40c->base->num_phy_chans; j += 8) {
		int phy_num = j + event_group * 2;

		if (d40c->dma_cfg.use_fixed_channel) {
			i = d40c->dma_cfg.phy_channel;

			if ((i != phy_num) && (i != phy_num + 1)) {
				dev_err(chan2dev(d40c),
					"invalid fixed phy channel %d\n", i);
				return -EINVAL;
			}

			if (d40_alloc_mask_set(&phys[i], is_src, event_line,
					       is_log, first_phy_user))
				goto found_log;

			dev_err(chan2dev(d40c),
				"could not allocate fixed phy channel %d\n", i);
			return -EINVAL;
		}

		/*
		 * Spread logical channels across all available physical rather
		 * than pack every logical channel at the first available phy
		 * channels.
		 */
		if (is_src) {
			for (i = phy_num; i < phy_num + 2; i++) {
				if (d40_alloc_mask_set(&phys[i], is_src,
						       event_line, is_log,
						       first_phy_user))
					goto found_log;
			}
		} else {
			for (i = phy_num + 1; i >= phy_num; i--) {
				if (d40_alloc_mask_set(&phys[i], is_src,
						       event_line, is_log,
						       first_phy_user))
					goto found_log;
			}
		}
	}
	return -EINVAL;

found_log:
	d40c->phy_chan = &phys[i];
	d40c->log_num = log_num;
out:

	if (is_log)
		d40c->base->lookup_log_chans[d40c->log_num] = d40c;
	else
		d40c->base->lookup_phy_chans[d40c->phy_chan->num] = d40c;

	return 0;

}

static int d40_config_memcpy(struct d40_chan *d40c)
{
	dma_cap_mask_t cap = d40c->chan.device->cap_mask;

	if (dma_has_cap(DMA_MEMCPY, cap) && !dma_has_cap(DMA_SLAVE, cap)) {
		d40c->dma_cfg = dma40_memcpy_conf_log;
		d40c->dma_cfg.dev_type = dma40_memcpy_channels[d40c->chan.chan_id];

		d40_log_cfg(&d40c->dma_cfg,
			    &d40c->log_def.lcsp1, &d40c->log_def.lcsp3);

	} else if (dma_has_cap(DMA_MEMCPY, cap) &&
		   dma_has_cap(DMA_SLAVE, cap)) {
		d40c->dma_cfg = dma40_memcpy_conf_phy;

		/* Generate interrrupt at end of transfer or relink. */
		d40c->dst_def_cfg |= BIT(D40_SREG_CFG_TIM_POS);

		/* Generate interrupt on error. */
		d40c->src_def_cfg |= BIT(D40_SREG_CFG_EIM_POS);
		d40c->dst_def_cfg |= BIT(D40_SREG_CFG_EIM_POS);

	} else {
		chan_err(d40c, "No memcpy\n");
		return -EINVAL;
	}

	return 0;
}

static int d40_free_dma(struct d40_chan *d40c)
{

	int res = 0;
	u32 event = D40_TYPE_TO_EVENT(d40c->dma_cfg.dev_type);
	struct d40_phy_res *phy = d40c->phy_chan;
	bool is_src;

	/* Terminate all queued and active transfers */
	d40_term_all(d40c);

	if (phy == NULL) {
		chan_err(d40c, "phy == null\n");
		return -EINVAL;
	}

	if (phy->allocated_src == D40_ALLOC_FREE &&
	    phy->allocated_dst == D40_ALLOC_FREE) {
		chan_err(d40c, "channel already free\n");
		return -EINVAL;
	}

	if (d40c->dma_cfg.dir == DMA_MEM_TO_DEV ||
	    d40c->dma_cfg.dir == DMA_MEM_TO_MEM)
		is_src = false;
	else if (d40c->dma_cfg.dir == DMA_DEV_TO_MEM)
		is_src = true;
	else {
		chan_err(d40c, "Unknown direction\n");
		return -EINVAL;
	}

	pm_runtime_get_sync(d40c->base->dev);
	res = d40_channel_execute_command(d40c, D40_DMA_STOP);
	if (res) {
		chan_err(d40c, "stop failed\n");
		goto mark_last_busy;
	}

	d40_alloc_mask_free(phy, is_src, chan_is_logical(d40c) ? event : 0);

	if (chan_is_logical(d40c))
		d40c->base->lookup_log_chans[d40c->log_num] = NULL;
	else
		d40c->base->lookup_phy_chans[phy->num] = NULL;

	if (d40c->busy) {
		pm_runtime_mark_last_busy(d40c->base->dev);
		pm_runtime_put_autosuspend(d40c->base->dev);
	}

	d40c->busy = false;
	d40c->phy_chan = NULL;
	d40c->configured = false;
 mark_last_busy:
	pm_runtime_mark_last_busy(d40c->base->dev);
	pm_runtime_put_autosuspend(d40c->base->dev);
	return res;
}

static bool d40_is_paused(struct d40_chan *d40c)
{
	void __iomem *chanbase = chan_base(d40c);
	bool is_paused = false;
	unsigned long flags;
	void __iomem *active_reg;
	u32 status;
	u32 event = D40_TYPE_TO_EVENT(d40c->dma_cfg.dev_type);

	spin_lock_irqsave(&d40c->lock, flags);

	if (chan_is_physical(d40c)) {
		if (d40c->phy_chan->num % 2 == 0)
			active_reg = d40c->base->virtbase + D40_DREG_ACTIVE;
		else
			active_reg = d40c->base->virtbase + D40_DREG_ACTIVO;

		status = (readl(active_reg) &
			  D40_CHAN_POS_MASK(d40c->phy_chan->num)) >>
			D40_CHAN_POS(d40c->phy_chan->num);
		if (status == D40_DMA_SUSPENDED || status == D40_DMA_STOP)
			is_paused = true;
		goto unlock;
	}

	if (d40c->dma_cfg.dir == DMA_MEM_TO_DEV ||
	    d40c->dma_cfg.dir == DMA_MEM_TO_MEM) {
		status = readl(chanbase + D40_CHAN_REG_SDLNK);
	} else if (d40c->dma_cfg.dir == DMA_DEV_TO_MEM) {
		status = readl(chanbase + D40_CHAN_REG_SSLNK);
	} else {
		chan_err(d40c, "Unknown direction\n");
		goto unlock;
	}

	status = (status & D40_EVENTLINE_MASK(event)) >>
		D40_EVENTLINE_POS(event);

	if (status != D40_DMA_RUN)
		is_paused = true;
 unlock:
	spin_unlock_irqrestore(&d40c->lock, flags);
	return is_paused;

}

static u32 stedma40_residue(struct dma_chan *chan)
{
	struct d40_chan *d40c =
		container_of(chan, struct d40_chan, chan);
	u32 bytes_left;
	unsigned long flags;

	spin_lock_irqsave(&d40c->lock, flags);
	bytes_left = d40_residue(d40c);
	spin_unlock_irqrestore(&d40c->lock, flags);

	return bytes_left;
}

static int
d40_prep_sg_log(struct d40_chan *chan, struct d40_desc *desc,
		struct scatterlist *sg_src, struct scatterlist *sg_dst,
		unsigned int sg_len, dma_addr_t src_dev_addr,
		dma_addr_t dst_dev_addr)
{
	struct stedma40_chan_cfg *cfg = &chan->dma_cfg;
	struct stedma40_half_channel_info *src_info = &cfg->src_info;
	struct stedma40_half_channel_info *dst_info = &cfg->dst_info;
	int ret;

	ret = d40_log_sg_to_lli(sg_src, sg_len,
				src_dev_addr,
				desc->lli_log.src,
				chan->log_def.lcsp1,
				src_info->data_width,
				dst_info->data_width);

	ret = d40_log_sg_to_lli(sg_dst, sg_len,
				dst_dev_addr,
				desc->lli_log.dst,
				chan->log_def.lcsp3,
				dst_info->data_width,
				src_info->data_width);

	return ret < 0 ? ret : 0;
}

static int
d40_prep_sg_phy(struct d40_chan *chan, struct d40_desc *desc,
		struct scatterlist *sg_src, struct scatterlist *sg_dst,
		unsigned int sg_len, dma_addr_t src_dev_addr,
		dma_addr_t dst_dev_addr)
{
	struct stedma40_chan_cfg *cfg = &chan->dma_cfg;
	struct stedma40_half_channel_info *src_info = &cfg->src_info;
	struct stedma40_half_channel_info *dst_info = &cfg->dst_info;
	unsigned long flags = 0;
	int ret;

	if (desc->cyclic)
		flags |= LLI_CYCLIC | LLI_TERM_INT;

	ret = d40_phy_sg_to_lli(sg_src, sg_len, src_dev_addr,
				desc->lli_phy.src,
				virt_to_phys(desc->lli_phy.src),
				chan->src_def_cfg,
				src_info, dst_info, flags);

	ret = d40_phy_sg_to_lli(sg_dst, sg_len, dst_dev_addr,
				desc->lli_phy.dst,
				virt_to_phys(desc->lli_phy.dst),
				chan->dst_def_cfg,
				dst_info, src_info, flags);

	dma_sync_single_for_device(chan->base->dev, desc->lli_pool.dma_addr,
				   desc->lli_pool.size, DMA_TO_DEVICE);

	return ret < 0 ? ret : 0;
}

static struct d40_desc *
d40_prep_desc(struct d40_chan *chan, struct scatterlist *sg,
	      unsigned int sg_len, unsigned long dma_flags)
{
	struct stedma40_chan_cfg *cfg;
	struct d40_desc *desc;
	int ret;

	desc = d40_desc_get(chan);
	if (!desc)
		return NULL;

	cfg = &chan->dma_cfg;
	desc->lli_len = d40_sg_2_dmalen(sg, sg_len, cfg->src_info.data_width,
					cfg->dst_info.data_width);
	if (desc->lli_len < 0) {
		chan_err(chan, "Unaligned size\n");
		goto free_desc;
	}

	ret = d40_pool_lli_alloc(chan, desc, desc->lli_len);
	if (ret < 0) {
		chan_err(chan, "Could not allocate lli\n");
		goto free_desc;
	}

	desc->lli_current = 0;
	desc->txd.flags = dma_flags;
	desc->txd.tx_submit = d40_tx_submit;

	dma_async_tx_descriptor_init(&desc->txd, &chan->chan);

	return desc;
 free_desc:
	d40_desc_free(chan, desc);
	return NULL;
}

static struct dma_async_tx_descriptor *
d40_prep_sg(struct dma_chan *dchan, struct scatterlist *sg_src,
	    struct scatterlist *sg_dst, unsigned int sg_len,
	    enum dma_transfer_direction direction, unsigned long dma_flags)
{
	struct d40_chan *chan = container_of(dchan, struct d40_chan, chan);
	dma_addr_t src_dev_addr;
	dma_addr_t dst_dev_addr;
	struct d40_desc *desc;
	unsigned long flags;
	int ret;

	if (!chan->phy_chan) {
		chan_err(chan, "Cannot prepare unallocated channel\n");
		return NULL;
	}

	d40_set_runtime_config_write(dchan, &chan->slave_config, direction);

	spin_lock_irqsave(&chan->lock, flags);

	desc = d40_prep_desc(chan, sg_src, sg_len, dma_flags);
	if (desc == NULL)
		goto unlock;

	if (sg_next(&sg_src[sg_len - 1]) == sg_src)
		desc->cyclic = true;

	src_dev_addr = 0;
	dst_dev_addr = 0;
	if (direction == DMA_DEV_TO_MEM)
		src_dev_addr = chan->runtime_addr;
	else if (direction == DMA_MEM_TO_DEV)
		dst_dev_addr = chan->runtime_addr;

	if (chan_is_logical(chan))
		ret = d40_prep_sg_log(chan, desc, sg_src, sg_dst,
				      sg_len, src_dev_addr, dst_dev_addr);
	else
		ret = d40_prep_sg_phy(chan, desc, sg_src, sg_dst,
				      sg_len, src_dev_addr, dst_dev_addr);

	if (ret) {
		chan_err(chan, "Failed to prepare %s sg job: %d\n",
			 chan_is_logical(chan) ? "log" : "phy", ret);
		goto free_desc;
	}

	/*
	 * add descriptor to the prepare queue in order to be able
	 * to free them later in terminate_all
	 */
	list_add_tail(&desc->node, &chan->prepare_queue);

	spin_unlock_irqrestore(&chan->lock, flags);

	return &desc->txd;
 free_desc:
	d40_desc_free(chan, desc);
 unlock:
	spin_unlock_irqrestore(&chan->lock, flags);
	return NULL;
}

bool stedma40_filter(struct dma_chan *chan, void *data)
{
	struct stedma40_chan_cfg *info = data;
	struct d40_chan *d40c =
		container_of(chan, struct d40_chan, chan);
	int err;

	if (data) {
		err = d40_validate_conf(d40c, info);
		if (!err)
			d40c->dma_cfg = *info;
	} else
		err = d40_config_memcpy(d40c);

	if (!err)
		d40c->configured = true;

	return err == 0;
}
EXPORT_SYMBOL(stedma40_filter);

static void __d40_set_prio_rt(struct d40_chan *d40c, int dev_type, bool src)
{
	bool realtime = d40c->dma_cfg.realtime;
	bool highprio = d40c->dma_cfg.high_priority;
	u32 rtreg;
	u32 event = D40_TYPE_TO_EVENT(dev_type);
	u32 group = D40_TYPE_TO_GROUP(dev_type);
	u32 bit = BIT(event);
	u32 prioreg;
	struct d40_gen_dmac *dmac = &d40c->base->gen_dmac;

	rtreg = realtime ? dmac->realtime_en : dmac->realtime_clear;
	/*
	 * Due to a hardware bug, in some cases a logical channel triggered by
	 * a high priority destination event line can generate extra packet
	 * transactions.
	 *
	 * The workaround is to not set the high priority level for the
	 * destination event lines that trigger logical channels.
	 */
	if (!src && chan_is_logical(d40c))
		highprio = false;

	prioreg = highprio ? dmac->high_prio_en : dmac->high_prio_clear;

	/* Destination event lines are stored in the upper halfword */
	if (!src)
		bit <<= 16;

	writel(bit, d40c->base->virtbase + prioreg + group * 4);
	writel(bit, d40c->base->virtbase + rtreg + group * 4);
}

static void d40_set_prio_realtime(struct d40_chan *d40c)
{
	if (d40c->base->rev < 3)
		return;

	if ((d40c->dma_cfg.dir ==  DMA_DEV_TO_MEM) ||
	    (d40c->dma_cfg.dir == DMA_DEV_TO_DEV))
		__d40_set_prio_rt(d40c, d40c->dma_cfg.dev_type, true);

	if ((d40c->dma_cfg.dir ==  DMA_MEM_TO_DEV) ||
	    (d40c->dma_cfg.dir == DMA_DEV_TO_DEV))
		__d40_set_prio_rt(d40c, d40c->dma_cfg.dev_type, false);
}

#define D40_DT_FLAGS_MODE(flags)       ((flags >> 0) & 0x1)
#define D40_DT_FLAGS_DIR(flags)        ((flags >> 1) & 0x1)
#define D40_DT_FLAGS_BIG_ENDIAN(flags) ((flags >> 2) & 0x1)
#define D40_DT_FLAGS_FIXED_CHAN(flags) ((flags >> 3) & 0x1)
#define D40_DT_FLAGS_HIGH_PRIO(flags)  ((flags >> 4) & 0x1)

static struct dma_chan *d40_xlate(struct of_phandle_args *dma_spec,
				  struct of_dma *ofdma)
{
	struct stedma40_chan_cfg cfg;
	dma_cap_mask_t cap;
	u32 flags;

	memset(&cfg, 0, sizeof(struct stedma40_chan_cfg));

	dma_cap_zero(cap);
	dma_cap_set(DMA_SLAVE, cap);

	cfg.dev_type = dma_spec->args[0];
	flags = dma_spec->args[2];

	switch (D40_DT_FLAGS_MODE(flags)) {
	case 0: cfg.mode = STEDMA40_MODE_LOGICAL; break;
	case 1: cfg.mode = STEDMA40_MODE_PHYSICAL; break;
	}

	switch (D40_DT_FLAGS_DIR(flags)) {
	case 0:
		cfg.dir = DMA_MEM_TO_DEV;
		cfg.dst_info.big_endian = D40_DT_FLAGS_BIG_ENDIAN(flags);
		break;
	case 1:
		cfg.dir = DMA_DEV_TO_MEM;
		cfg.src_info.big_endian = D40_DT_FLAGS_BIG_ENDIAN(flags);
		break;
	}

	if (D40_DT_FLAGS_FIXED_CHAN(flags)) {
		cfg.phy_channel = dma_spec->args[1];
		cfg.use_fixed_channel = true;
	}

	if (D40_DT_FLAGS_HIGH_PRIO(flags))
		cfg.high_priority = true;

	return dma_request_channel(cap, stedma40_filter, &cfg);
}

/* DMA ENGINE functions */
static int d40_alloc_chan_resources(struct dma_chan *chan)
{
	int err;
	unsigned long flags;
	struct d40_chan *d40c =
		container_of(chan, struct d40_chan, chan);
	bool is_free_phy;
	spin_lock_irqsave(&d40c->lock, flags);

	dma_cookie_init(chan);

	/* If no dma configuration is set use default configuration (memcpy) */
	if (!d40c->configured) {
		err = d40_config_memcpy(d40c);
		if (err) {
			chan_err(d40c, "Failed to configure memcpy channel\n");
			goto mark_last_busy;
		}
	}

	err = d40_allocate_channel(d40c, &is_free_phy);
	if (err) {
		chan_err(d40c, "Failed to allocate channel\n");
		d40c->configured = false;
		goto mark_last_busy;
	}

	pm_runtime_get_sync(d40c->base->dev);

	d40_set_prio_realtime(d40c);

	if (chan_is_logical(d40c)) {
		if (d40c->dma_cfg.dir == DMA_DEV_TO_MEM)
			d40c->lcpa = d40c->base->lcpa_base +
				d40c->dma_cfg.dev_type * D40_LCPA_CHAN_SIZE;
		else
			d40c->lcpa = d40c->base->lcpa_base +
				d40c->dma_cfg.dev_type *
				D40_LCPA_CHAN_SIZE + D40_LCPA_CHAN_DST_DELTA;

		/* Unmask the Global Interrupt Mask. */
		d40c->src_def_cfg |= BIT(D40_SREG_CFG_LOG_GIM_POS);
		d40c->dst_def_cfg |= BIT(D40_SREG_CFG_LOG_GIM_POS);
	}

	dev_dbg(chan2dev(d40c), "allocated %s channel (phy %d%s)\n",
		 chan_is_logical(d40c) ? "logical" : "physical",
		 d40c->phy_chan->num,
		 d40c->dma_cfg.use_fixed_channel ? ", fixed" : "");


	/*
	 * Only write channel configuration to the DMA if the physical
	 * resource is free. In case of multiple logical channels
	 * on the same physical resource, only the first write is necessary.
	 */
	if (is_free_phy)
		d40_config_write(d40c);
 mark_last_busy:
	pm_runtime_mark_last_busy(d40c->base->dev);
	pm_runtime_put_autosuspend(d40c->base->dev);
	spin_unlock_irqrestore(&d40c->lock, flags);
	return err;
}

static void d40_free_chan_resources(struct dma_chan *chan)
{
	struct d40_chan *d40c =
		container_of(chan, struct d40_chan, chan);
	int err;
	unsigned long flags;

	if (d40c->phy_chan == NULL) {
		chan_err(d40c, "Cannot free unallocated channel\n");
		return;
	}

	spin_lock_irqsave(&d40c->lock, flags);

	err = d40_free_dma(d40c);

	if (err)
		chan_err(d40c, "Failed to free channel\n");
	spin_unlock_irqrestore(&d40c->lock, flags);
}

static struct dma_async_tx_descriptor *d40_prep_memcpy(struct dma_chan *chan,
						       dma_addr_t dst,
						       dma_addr_t src,
						       size_t size,
						       unsigned long dma_flags)
{
	struct scatterlist dst_sg;
	struct scatterlist src_sg;

	sg_init_table(&dst_sg, 1);
	sg_init_table(&src_sg, 1);

	sg_dma_address(&dst_sg) = dst;
	sg_dma_address(&src_sg) = src;

	sg_dma_len(&dst_sg) = size;
	sg_dma_len(&src_sg) = size;

	return d40_prep_sg(chan, &src_sg, &dst_sg, 1,
			   DMA_MEM_TO_MEM, dma_flags);
}

static struct dma_async_tx_descriptor *
d40_prep_slave_sg(struct dma_chan *chan, struct scatterlist *sgl,
		  unsigned int sg_len, enum dma_transfer_direction direction,
		  unsigned long dma_flags, void *context)
{
	if (!is_slave_direction(direction))
		return NULL;

	return d40_prep_sg(chan, sgl, sgl, sg_len, direction, dma_flags);
}

static struct dma_async_tx_descriptor *
dma40_prep_dma_cyclic(struct dma_chan *chan, dma_addr_t dma_addr,
		     size_t buf_len, size_t period_len,
		     enum dma_transfer_direction direction, unsigned long flags)
{
	unsigned int periods = buf_len / period_len;
	struct dma_async_tx_descriptor *txd;
	struct scatterlist *sg;
	int i;

	sg = kcalloc(periods + 1, sizeof(struct scatterlist), GFP_NOWAIT);
	if (!sg)
		return NULL;

	for (i = 0; i < periods; i++) {
		sg_dma_address(&sg[i]) = dma_addr;
		sg_dma_len(&sg[i]) = period_len;
		dma_addr += period_len;
	}

	sg_chain(sg, periods + 1, sg);

	txd = d40_prep_sg(chan, sg, sg, periods, direction,
			  DMA_PREP_INTERRUPT);

	kfree(sg);

	return txd;
}

static enum dma_status d40_tx_status(struct dma_chan *chan,
				     dma_cookie_t cookie,
				     struct dma_tx_state *txstate)
{
	struct d40_chan *d40c = container_of(chan, struct d40_chan, chan);
	enum dma_status ret;

	if (d40c->phy_chan == NULL) {
		chan_err(d40c, "Cannot read status of unallocated channel\n");
		return -EINVAL;
	}

	ret = dma_cookie_status(chan, cookie, txstate);
	if (ret != DMA_COMPLETE && txstate)
		dma_set_residue(txstate, stedma40_residue(chan));

	if (d40_is_paused(d40c))
		ret = DMA_PAUSED;

	return ret;
}

static void d40_issue_pending(struct dma_chan *chan)
{
	struct d40_chan *d40c = container_of(chan, struct d40_chan, chan);
	unsigned long flags;

	if (d40c->phy_chan == NULL) {
		chan_err(d40c, "Channel is not allocated!\n");
		return;
	}

	spin_lock_irqsave(&d40c->lock, flags);

	list_splice_tail_init(&d40c->pending_queue, &d40c->queue);

	/* Busy means that queued jobs are already being processed */
	if (!d40c->busy)
		(void) d40_queue_start(d40c);

	spin_unlock_irqrestore(&d40c->lock, flags);
}

static int d40_terminate_all(struct dma_chan *chan)
{
	unsigned long flags;
	struct d40_chan *d40c = container_of(chan, struct d40_chan, chan);
	int ret;

	if (d40c->phy_chan == NULL) {
		chan_err(d40c, "Channel is not allocated!\n");
		return -EINVAL;
	}

	spin_lock_irqsave(&d40c->lock, flags);

	pm_runtime_get_sync(d40c->base->dev);
	ret = d40_channel_execute_command(d40c, D40_DMA_STOP);
	if (ret)
		chan_err(d40c, "Failed to stop channel\n");

	d40_term_all(d40c);
	pm_runtime_mark_last_busy(d40c->base->dev);
	pm_runtime_put_autosuspend(d40c->base->dev);
	if (d40c->busy) {
		pm_runtime_mark_last_busy(d40c->base->dev);
		pm_runtime_put_autosuspend(d40c->base->dev);
	}
	d40c->busy = false;

	spin_unlock_irqrestore(&d40c->lock, flags);
	return 0;
}

static int
dma40_config_to_halfchannel(struct d40_chan *d40c,
			    struct stedma40_half_channel_info *info,
			    u32 maxburst)
{
	int psize;

	if (chan_is_logical(d40c)) {
		if (maxburst >= 16)
			psize = STEDMA40_PSIZE_LOG_16;
		else if (maxburst >= 8)
			psize = STEDMA40_PSIZE_LOG_8;
		else if (maxburst >= 4)
			psize = STEDMA40_PSIZE_LOG_4;
		else
			psize = STEDMA40_PSIZE_LOG_1;
	} else {
		if (maxburst >= 16)
			psize = STEDMA40_PSIZE_PHY_16;
		else if (maxburst >= 8)
			psize = STEDMA40_PSIZE_PHY_8;
		else if (maxburst >= 4)
			psize = STEDMA40_PSIZE_PHY_4;
		else
			psize = STEDMA40_PSIZE_PHY_1;
	}

	info->psize = psize;
	info->flow_ctrl = STEDMA40_NO_FLOW_CTRL;

	return 0;
}

static int d40_set_runtime_config(struct dma_chan *chan,
				  struct dma_slave_config *config)
{
	struct d40_chan *d40c = container_of(chan, struct d40_chan, chan);

	memcpy(&d40c->slave_config, config, sizeof(*config));

	return 0;
}

/* Runtime reconfiguration extension */
static int d40_set_runtime_config_write(struct dma_chan *chan,
				  struct dma_slave_config *config,
				  enum dma_transfer_direction direction)
{
	struct d40_chan *d40c = container_of(chan, struct d40_chan, chan);
	struct stedma40_chan_cfg *cfg = &d40c->dma_cfg;
	enum dma_slave_buswidth src_addr_width, dst_addr_width;
	dma_addr_t config_addr;
	u32 src_maxburst, dst_maxburst;
	int ret;

	if (d40c->phy_chan == NULL) {
		chan_err(d40c, "Channel is not allocated!\n");
		return -EINVAL;
	}

	src_addr_width = config->src_addr_width;
	src_maxburst = config->src_maxburst;
	dst_addr_width = config->dst_addr_width;
	dst_maxburst = config->dst_maxburst;

	if (direction == DMA_DEV_TO_MEM) {
		config_addr = config->src_addr;

		if (cfg->dir != DMA_DEV_TO_MEM)
			dev_dbg(d40c->base->dev,
				"channel was not configured for peripheral "
				"to memory transfer (%d) overriding\n",
				cfg->dir);
		cfg->dir = DMA_DEV_TO_MEM;

		/* Configure the memory side */
		if (dst_addr_width == DMA_SLAVE_BUSWIDTH_UNDEFINED)
			dst_addr_width = src_addr_width;
		if (dst_maxburst == 0)
			dst_maxburst = src_maxburst;

	} else if (direction == DMA_MEM_TO_DEV) {
		config_addr = config->dst_addr;

		if (cfg->dir != DMA_MEM_TO_DEV)
			dev_dbg(d40c->base->dev,
				"channel was not configured for memory "
				"to peripheral transfer (%d) overriding\n",
				cfg->dir);
		cfg->dir = DMA_MEM_TO_DEV;

		/* Configure the memory side */
		if (src_addr_width == DMA_SLAVE_BUSWIDTH_UNDEFINED)
			src_addr_width = dst_addr_width;
		if (src_maxburst == 0)
			src_maxburst = dst_maxburst;
	} else {
		dev_err(d40c->base->dev,
			"unrecognized channel direction %d\n",
			direction);
		return -EINVAL;
	}

	if (config_addr <= 0) {
		dev_err(d40c->base->dev, "no address supplied\n");
		return -EINVAL;
	}

	if (src_maxburst * src_addr_width != dst_maxburst * dst_addr_width) {
		dev_err(d40c->base->dev,
			"src/dst width/maxburst mismatch: %d*%d != %d*%d\n",
			src_maxburst,
			src_addr_width,
			dst_maxburst,
			dst_addr_width);
		return -EINVAL;
	}

	if (src_maxburst > 16) {
		src_maxburst = 16;
		dst_maxburst = src_maxburst * src_addr_width / dst_addr_width;
	} else if (dst_maxburst > 16) {
		dst_maxburst = 16;
		src_maxburst = dst_maxburst * dst_addr_width / src_addr_width;
	}

	/* Only valid widths are; 1, 2, 4 and 8. */
	if (src_addr_width <= DMA_SLAVE_BUSWIDTH_UNDEFINED ||
	    src_addr_width >  DMA_SLAVE_BUSWIDTH_8_BYTES   ||
	    dst_addr_width <= DMA_SLAVE_BUSWIDTH_UNDEFINED ||
	    dst_addr_width >  DMA_SLAVE_BUSWIDTH_8_BYTES   ||
	    !is_power_of_2(src_addr_width) ||
	    !is_power_of_2(dst_addr_width))
		return -EINVAL;

	cfg->src_info.data_width = src_addr_width;
	cfg->dst_info.data_width = dst_addr_width;

	ret = dma40_config_to_halfchannel(d40c, &cfg->src_info,
					  src_maxburst);
	if (ret)
		return ret;

	ret = dma40_config_to_halfchannel(d40c, &cfg->dst_info,
					  dst_maxburst);
	if (ret)
		return ret;

	/* Fill in register values */
	if (chan_is_logical(d40c))
		d40_log_cfg(cfg, &d40c->log_def.lcsp1, &d40c->log_def.lcsp3);
	else
		d40_phy_cfg(cfg, &d40c->src_def_cfg, &d40c->dst_def_cfg);

	/* These settings will take precedence later */
	d40c->runtime_addr = config_addr;
	d40c->runtime_direction = direction;
	dev_dbg(d40c->base->dev,
		"configured channel %s for %s, data width %d/%d, "
		"maxburst %d/%d elements, LE, no flow control\n",
		dma_chan_name(chan),
		(direction == DMA_DEV_TO_MEM) ? "RX" : "TX",
		src_addr_width, dst_addr_width,
		src_maxburst, dst_maxburst);

	return 0;
}

/* Initialization functions */

static void __init d40_chan_init(struct d40_base *base, struct dma_device *dma,
				 struct d40_chan *chans, int offset,
				 int num_chans)
{
	int i = 0;
	struct d40_chan *d40c;

	INIT_LIST_HEAD(&dma->channels);

	for (i = offset; i < offset + num_chans; i++) {
		d40c = &chans[i];
		d40c->base = base;
		d40c->chan.device = dma;

		spin_lock_init(&d40c->lock);

		d40c->log_num = D40_PHY_CHAN;

		INIT_LIST_HEAD(&d40c->done);
		INIT_LIST_HEAD(&d40c->active);
		INIT_LIST_HEAD(&d40c->queue);
		INIT_LIST_HEAD(&d40c->pending_queue);
		INIT_LIST_HEAD(&d40c->client);
		INIT_LIST_HEAD(&d40c->prepare_queue);

		tasklet_init(&d40c->tasklet, dma_tasklet,
			     (unsigned long) d40c);

		list_add_tail(&d40c->chan.device_node,
			      &dma->channels);
	}
}

static void d40_ops_init(struct d40_base *base, struct dma_device *dev)
{
	if (dma_has_cap(DMA_SLAVE, dev->cap_mask)) {
		dev->device_prep_slave_sg = d40_prep_slave_sg;
		dev->directions = BIT(DMA_DEV_TO_MEM) | BIT(DMA_MEM_TO_DEV);
	}

	if (dma_has_cap(DMA_MEMCPY, dev->cap_mask)) {
		dev->device_prep_dma_memcpy = d40_prep_memcpy;
		dev->directions = BIT(DMA_MEM_TO_MEM);
		/*
		 * This controller can only access address at even
		 * 32bit boundaries, i.e. 2^2
		 */
		dev->copy_align = DMAENGINE_ALIGN_4_BYTES;
	}

	if (dma_has_cap(DMA_CYCLIC, dev->cap_mask))
		dev->device_prep_dma_cyclic = dma40_prep_dma_cyclic;

	dev->device_alloc_chan_resources = d40_alloc_chan_resources;
	dev->device_free_chan_resources = d40_free_chan_resources;
	dev->device_issue_pending = d40_issue_pending;
	dev->device_tx_status = d40_tx_status;
	dev->device_config = d40_set_runtime_config;
	dev->device_pause = d40_pause;
	dev->device_resume = d40_resume;
	dev->device_terminate_all = d40_terminate_all;
	dev->residue_granularity = DMA_RESIDUE_GRANULARITY_BURST;
	dev->dev = base->dev;
}

static int __init d40_dmaengine_init(struct d40_base *base,
				     int num_reserved_chans)
{
	int err ;

	d40_chan_init(base, &base->dma_slave, base->log_chans,
		      0, base->num_log_chans);

	dma_cap_zero(base->dma_slave.cap_mask);
	dma_cap_set(DMA_SLAVE, base->dma_slave.cap_mask);
	dma_cap_set(DMA_CYCLIC, base->dma_slave.cap_mask);

	d40_ops_init(base, &base->dma_slave);

	err = dmaenginem_async_device_register(&base->dma_slave);

	if (err) {
		d40_err(base->dev, "Failed to register slave channels\n");
		goto exit;
	}

	d40_chan_init(base, &base->dma_memcpy, base->log_chans,
		      base->num_log_chans, base->num_memcpy_chans);

	dma_cap_zero(base->dma_memcpy.cap_mask);
	dma_cap_set(DMA_MEMCPY, base->dma_memcpy.cap_mask);

	d40_ops_init(base, &base->dma_memcpy);

	err = dmaenginem_async_device_register(&base->dma_memcpy);

	if (err) {
		d40_err(base->dev,
			"Failed to register memcpy only channels\n");
		goto exit;
	}

	d40_chan_init(base, &base->dma_both, base->phy_chans,
		      0, num_reserved_chans);

	dma_cap_zero(base->dma_both.cap_mask);
	dma_cap_set(DMA_SLAVE, base->dma_both.cap_mask);
	dma_cap_set(DMA_MEMCPY, base->dma_both.cap_mask);
	dma_cap_set(DMA_CYCLIC, base->dma_slave.cap_mask);

	d40_ops_init(base, &base->dma_both);
	err = dmaenginem_async_device_register(&base->dma_both);

	if (err) {
		d40_err(base->dev,
			"Failed to register logical and physical capable channels\n");
		goto exit;
	}
	return 0;
 exit:
	return err;
}

/* Suspend resume functionality */
#ifdef CONFIG_PM_SLEEP
static int dma40_suspend(struct device *dev)
{
	struct d40_base *base = dev_get_drvdata(dev);
	int ret;

	ret = pm_runtime_force_suspend(dev);
	if (ret)
		return ret;

	if (base->lcpa_regulator)
		ret = regulator_disable(base->lcpa_regulator);
	return ret;
}

static int dma40_resume(struct device *dev)
{
	struct d40_base *base = dev_get_drvdata(dev);
	int ret = 0;

	if (base->lcpa_regulator) {
		ret = regulator_enable(base->lcpa_regulator);
		if (ret)
			return ret;
	}

	return pm_runtime_force_resume(dev);
}
#endif

#ifdef CONFIG_PM
static void dma40_backup(void __iomem *baseaddr, u32 *backup,
			 u32 *regaddr, int num, bool save)
{
	int i;

	for (i = 0; i < num; i++) {
		void __iomem *addr = baseaddr + regaddr[i];

		if (save)
			backup[i] = readl_relaxed(addr);
		else
			writel_relaxed(backup[i], addr);
	}
}

static void d40_save_restore_registers(struct d40_base *base, bool save)
{
	int i;

	/* Save/Restore channel specific registers */
	for (i = 0; i < base->num_phy_chans; i++) {
		void __iomem *addr;
		int idx;

		if (base->phy_res[i].reserved)
			continue;

		addr = base->virtbase + D40_DREG_PCBASE + i * D40_DREG_PCDELTA;
		idx = i * ARRAY_SIZE(d40_backup_regs_chan);

		dma40_backup(addr, &base->reg_val_backup_chan[idx],
			     d40_backup_regs_chan,
			     ARRAY_SIZE(d40_backup_regs_chan),
			     save);
	}

	/* Save/Restore global registers */
	dma40_backup(base->virtbase, base->reg_val_backup,
		     d40_backup_regs, ARRAY_SIZE(d40_backup_regs),
		     save);

	/* Save/Restore registers only existing on dma40 v3 and later */
	if (base->gen_dmac.backup)
		dma40_backup(base->virtbase, base->reg_val_backup_v4,
			     base->gen_dmac.backup,
			base->gen_dmac.backup_size,
			save);
}

static int dma40_runtime_suspend(struct device *dev)
{
	struct d40_base *base = dev_get_drvdata(dev);

	d40_save_restore_registers(base, true);

	/* Don't disable/enable clocks for v1 due to HW bugs */
	if (base->rev != 1)
		writel_relaxed(base->gcc_pwr_off_mask,
			       base->virtbase + D40_DREG_GCC);

	return 0;
}

static int dma40_runtime_resume(struct device *dev)
{
	struct d40_base *base = dev_get_drvdata(dev);

	d40_save_restore_registers(base, false);

	writel_relaxed(D40_DREG_GCC_ENABLE_ALL,
		       base->virtbase + D40_DREG_GCC);
	return 0;
}
#endif

static const struct dev_pm_ops dma40_pm_ops = {
	SET_LATE_SYSTEM_SLEEP_PM_OPS(dma40_suspend, dma40_resume)
	SET_RUNTIME_PM_OPS(dma40_runtime_suspend,
				dma40_runtime_resume,
				NULL)
};

/* Initialization functions. */

static int __init d40_phy_res_init(struct d40_base *base)
{
	int i;
	int num_phy_chans_avail = 0;
	u32 val[2];
	int odd_even_bit = -2;
	int gcc = D40_DREG_GCC_ENA;

	val[0] = readl(base->virtbase + D40_DREG_PRSME);
	val[1] = readl(base->virtbase + D40_DREG_PRSMO);

	for (i = 0; i < base->num_phy_chans; i++) {
		base->phy_res[i].num = i;
		odd_even_bit += 2 * ((i % 2) == 0);
		if (((val[i % 2] >> odd_even_bit) & 3) == 1) {
			/* Mark security only channels as occupied */
			base->phy_res[i].allocated_src = D40_ALLOC_PHY;
			base->phy_res[i].allocated_dst = D40_ALLOC_PHY;
			base->phy_res[i].reserved = true;
			gcc |= D40_DREG_GCC_EVTGRP_ENA(D40_PHYS_TO_GROUP(i),
						       D40_DREG_GCC_SRC);
			gcc |= D40_DREG_GCC_EVTGRP_ENA(D40_PHYS_TO_GROUP(i),
						       D40_DREG_GCC_DST);


		} else {
			base->phy_res[i].allocated_src = D40_ALLOC_FREE;
			base->phy_res[i].allocated_dst = D40_ALLOC_FREE;
			base->phy_res[i].reserved = false;
			num_phy_chans_avail++;
		}
		spin_lock_init(&base->phy_res[i].lock);
	}

	/* Mark disabled channels as occupied */
	for (i = 0; base->plat_data->disabled_channels[i] != -1; i++) {
		int chan = base->plat_data->disabled_channels[i];

		base->phy_res[chan].allocated_src = D40_ALLOC_PHY;
		base->phy_res[chan].allocated_dst = D40_ALLOC_PHY;
		base->phy_res[chan].reserved = true;
		gcc |= D40_DREG_GCC_EVTGRP_ENA(D40_PHYS_TO_GROUP(chan),
					       D40_DREG_GCC_SRC);
		gcc |= D40_DREG_GCC_EVTGRP_ENA(D40_PHYS_TO_GROUP(chan),
					       D40_DREG_GCC_DST);
		num_phy_chans_avail--;
	}

	/* Mark soft_lli channels */
	for (i = 0; i < base->plat_data->num_of_soft_lli_chans; i++) {
		int chan = base->plat_data->soft_lli_chans[i];

		base->phy_res[chan].use_soft_lli = true;
	}

	dev_info(base->dev, "%d of %d physical DMA channels available\n",
		 num_phy_chans_avail, base->num_phy_chans);

	/* Verify settings extended vs standard */
	val[0] = readl(base->virtbase + D40_DREG_PRTYP);

	for (i = 0; i < base->num_phy_chans; i++) {

		if (base->phy_res[i].allocated_src == D40_ALLOC_FREE &&
		    (val[0] & 0x3) != 1)
			dev_info(base->dev,
				 "[%s] INFO: channel %d is misconfigured (%d)\n",
				 __func__, i, val[0] & 0x3);

		val[0] = val[0] >> 2;
	}

	/*
	 * To keep things simple, Enable all clocks initially.
	 * The clocks will get managed later post channel allocation.
	 * The clocks for the event lines on which reserved channels exists
	 * are not managed here.
	 */
	writel(D40_DREG_GCC_ENABLE_ALL, base->virtbase + D40_DREG_GCC);
	base->gcc_pwr_off_mask = gcc;

	return num_phy_chans_avail;
}

static struct d40_base * __init d40_hw_detect_init(struct platform_device *pdev)
{
	struct stedma40_platform_data *plat_data = dev_get_platdata(&pdev->dev);
	struct clk *clk;
	void __iomem *virtbase;
	struct resource *res;
	struct d40_base *base;
	int num_log_chans;
	int num_phy_chans;
	int num_memcpy_chans;
	int clk_ret = -EINVAL;
	int i;
	u32 pid;
	u32 cid;
	u8 rev;

	clk = clk_get(&pdev->dev, NULL);
	if (IS_ERR(clk)) {
		d40_err(&pdev->dev, "No matching clock found\n");
		goto check_prepare_enabled;
	}

	clk_ret = clk_prepare_enable(clk);
	if (clk_ret) {
		d40_err(&pdev->dev, "Failed to prepare/enable clock\n");
		goto disable_unprepare;
	}

	/* Get IO for DMAC base address */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "base");
	if (!res)
		goto disable_unprepare;

	if (request_mem_region(res->start, resource_size(res),
			       D40_NAME " I/O base") == NULL)
		goto release_region;

	virtbase = ioremap(res->start, resource_size(res));
	if (!virtbase)
		goto release_region;

	/* This is just a regular AMBA PrimeCell ID actually */
	for (pid = 0, i = 0; i < 4; i++)
		pid |= (readl(virtbase + resource_size(res) - 0x20 + 4 * i)
			& 255) << (i * 8);
	for (cid = 0, i = 0; i < 4; i++)
		cid |= (readl(virtbase + resource_size(res) - 0x10 + 4 * i)
			& 255) << (i * 8);

	if (cid != AMBA_CID) {
		d40_err(&pdev->dev, "Unknown hardware! No PrimeCell ID\n");
		goto unmap_io;
	}
	if (AMBA_MANF_BITS(pid) != AMBA_VENDOR_ST) {
		d40_err(&pdev->dev, "Unknown designer! Got %x wanted %x\n",
			AMBA_MANF_BITS(pid),
			AMBA_VENDOR_ST);
		goto unmap_io;
	}
	/*
	 * HW revision:
	 * DB8500ed has revision 0
	 * ? has revision 1
	 * DB8500v1 has revision 2
	 * DB8500v2 has revision 3
	 * AP9540v1 has revision 4
	 * DB8540v1 has revision 4
	 */
	rev = AMBA_REV_BITS(pid);
	if (rev < 2) {
		d40_err(&pdev->dev, "hardware revision: %d is not supported", rev);
		goto unmap_io;
	}

	/* The number of physical channels on this HW */
	if (plat_data->num_of_phy_chans)
		num_phy_chans = plat_data->num_of_phy_chans;
	else
		num_phy_chans = 4 * (readl(virtbase + D40_DREG_ICFG) & 0x7) + 4;

	/* The number of channels used for memcpy */
	if (plat_data->num_of_memcpy_chans)
		num_memcpy_chans = plat_data->num_of_memcpy_chans;
	else
		num_memcpy_chans = ARRAY_SIZE(dma40_memcpy_channels);

	num_log_chans = num_phy_chans * D40_MAX_LOG_CHAN_PER_PHY;

	dev_info(&pdev->dev,
		 "hardware rev: %d @ %pa with %d physical and %d logical channels\n",
		 rev, &res->start, num_phy_chans, num_log_chans);

	base = kzalloc(ALIGN(sizeof(struct d40_base), 4) +
		       (num_phy_chans + num_log_chans + num_memcpy_chans) *
		       sizeof(struct d40_chan), GFP_KERNEL);

	if (base == NULL)
		goto unmap_io;

	base->rev = rev;
	base->clk = clk;
	base->num_memcpy_chans = num_memcpy_chans;
	base->num_phy_chans = num_phy_chans;
	base->num_log_chans = num_log_chans;
	base->phy_start = res->start;
	base->phy_size = resource_size(res);
	base->virtbase = virtbase;
	base->plat_data = plat_data;
	base->dev = &pdev->dev;
	base->phy_chans = ((void *)base) + ALIGN(sizeof(struct d40_base), 4);
	base->log_chans = &base->phy_chans[num_phy_chans];

	if (base->plat_data->num_of_phy_chans == 14) {
		base->gen_dmac.backup = d40_backup_regs_v4b;
		base->gen_dmac.backup_size = BACKUP_REGS_SZ_V4B;
		base->gen_dmac.interrupt_en = D40_DREG_CPCMIS;
		base->gen_dmac.interrupt_clear = D40_DREG_CPCICR;
		base->gen_dmac.realtime_en = D40_DREG_CRSEG1;
		base->gen_dmac.realtime_clear = D40_DREG_CRCEG1;
		base->gen_dmac.high_prio_en = D40_DREG_CPSEG1;
		base->gen_dmac.high_prio_clear = D40_DREG_CPCEG1;
		base->gen_dmac.il = il_v4b;
		base->gen_dmac.il_size = ARRAY_SIZE(il_v4b);
		base->gen_dmac.init_reg = dma_init_reg_v4b;
		base->gen_dmac.init_reg_size = ARRAY_SIZE(dma_init_reg_v4b);
	} else {
		if (base->rev >= 3) {
			base->gen_dmac.backup = d40_backup_regs_v4a;
			base->gen_dmac.backup_size = BACKUP_REGS_SZ_V4A;
		}
		base->gen_dmac.interrupt_en = D40_DREG_PCMIS;
		base->gen_dmac.interrupt_clear = D40_DREG_PCICR;
		base->gen_dmac.realtime_en = D40_DREG_RSEG1;
		base->gen_dmac.realtime_clear = D40_DREG_RCEG1;
		base->gen_dmac.high_prio_en = D40_DREG_PSEG1;
		base->gen_dmac.high_prio_clear = D40_DREG_PCEG1;
		base->gen_dmac.il = il_v4a;
		base->gen_dmac.il_size = ARRAY_SIZE(il_v4a);
		base->gen_dmac.init_reg = dma_init_reg_v4a;
		base->gen_dmac.init_reg_size = ARRAY_SIZE(dma_init_reg_v4a);
	}

	base->phy_res = kcalloc(num_phy_chans,
				sizeof(*base->phy_res),
				GFP_KERNEL);
	if (!base->phy_res)
		goto free_base;

	base->lookup_phy_chans = kcalloc(num_phy_chans,
					 sizeof(*base->lookup_phy_chans),
					 GFP_KERNEL);
	if (!base->lookup_phy_chans)
		goto free_phy_res;

	base->lookup_log_chans = kcalloc(num_log_chans,
					 sizeof(*base->lookup_log_chans),
					 GFP_KERNEL);
	if (!base->lookup_log_chans)
		goto free_phy_chans;

	base->reg_val_backup_chan = kmalloc_array(base->num_phy_chans,
						  sizeof(d40_backup_regs_chan),
						  GFP_KERNEL);
	if (!base->reg_val_backup_chan)
		goto free_log_chans;

	base->lcla_pool.alloc_map = kcalloc(num_phy_chans
					    * D40_LCLA_LINK_PER_EVENT_GRP,
					    sizeof(*base->lcla_pool.alloc_map),
					    GFP_KERNEL);
	if (!base->lcla_pool.alloc_map)
		goto free_backup_chan;

	base->regs_interrupt = kmalloc_array(base->gen_dmac.il_size,
					     sizeof(*base->regs_interrupt),
					     GFP_KERNEL);
	if (!base->regs_interrupt)
		goto free_map;

	base->desc_slab = kmem_cache_create(D40_NAME, sizeof(struct d40_desc),
					    0, SLAB_HWCACHE_ALIGN,
					    NULL);
	if (base->desc_slab == NULL)
		goto free_regs;


	return base;
 free_regs:
	kfree(base->regs_interrupt);
 free_map:
	kfree(base->lcla_pool.alloc_map);
 free_backup_chan:
	kfree(base->reg_val_backup_chan);
 free_log_chans:
	kfree(base->lookup_log_chans);
 free_phy_chans:
	kfree(base->lookup_phy_chans);
 free_phy_res:
	kfree(base->phy_res);
 free_base:
	kfree(base);
 unmap_io:
	iounmap(virtbase);
 release_region:
	release_mem_region(res->start, resource_size(res));
 check_prepare_enabled:
	if (!clk_ret)
 disable_unprepare:
		clk_disable_unprepare(clk);
	if (!IS_ERR(clk))
		clk_put(clk);
	return NULL;
}

static void __init d40_hw_init(struct d40_base *base)
{

	int i;
	u32 prmseo[2] = {0, 0};
	u32 activeo[2] = {0xFFFFFFFF, 0xFFFFFFFF};
	u32 pcmis = 0;
	u32 pcicr = 0;
	struct d40_reg_val *dma_init_reg = base->gen_dmac.init_reg;
	u32 reg_size = base->gen_dmac.init_reg_size;

	for (i = 0; i < reg_size; i++)
		writel(dma_init_reg[i].val,
		       base->virtbase + dma_init_reg[i].reg);

	/* Configure all our dma channels to default settings */
	for (i = 0; i < base->num_phy_chans; i++) {

		activeo[i % 2] = activeo[i % 2] << 2;

		if (base->phy_res[base->num_phy_chans - i - 1].allocated_src
		    == D40_ALLOC_PHY) {
			activeo[i % 2] |= 3;
			continue;
		}

		/* Enable interrupt # */
		pcmis = (pcmis << 1) | 1;

		/* Clear interrupt # */
		pcicr = (pcicr << 1) | 1;

		/* Set channel to physical mode */
		prmseo[i % 2] = prmseo[i % 2] << 2;
		prmseo[i % 2] |= 1;

	}

	writel(prmseo[1], base->virtbase + D40_DREG_PRMSE);
	writel(prmseo[0], base->virtbase + D40_DREG_PRMSO);
	writel(activeo[1], base->virtbase + D40_DREG_ACTIVE);
	writel(activeo[0], base->virtbase + D40_DREG_ACTIVO);

	/* Write which interrupt to enable */
	writel(pcmis, base->virtbase + base->gen_dmac.interrupt_en);

	/* Write which interrupt to clear */
	writel(pcicr, base->virtbase + base->gen_dmac.interrupt_clear);

	/* These are __initdata and cannot be accessed after init */
	base->gen_dmac.init_reg = NULL;
	base->gen_dmac.init_reg_size = 0;
}

static int __init d40_lcla_allocate(struct d40_base *base)
{
	struct d40_lcla_pool *pool = &base->lcla_pool;
	unsigned long *page_list;
	int i, j;
	int ret;

	/*
	 * This is somewhat ugly. We need 8192 bytes that are 18 bit aligned,
	 * To full fill this hardware requirement without wasting 256 kb
	 * we allocate pages until we get an aligned one.
	 */
	page_list = kmalloc_array(MAX_LCLA_ALLOC_ATTEMPTS,
				  sizeof(*page_list),
				  GFP_KERNEL);
	if (!page_list)
		return -ENOMEM;

	/* Calculating how many pages that are required */
	base->lcla_pool.pages = SZ_1K * base->num_phy_chans / PAGE_SIZE;

	for (i = 0; i < MAX_LCLA_ALLOC_ATTEMPTS; i++) {
		page_list[i] = __get_free_pages(GFP_KERNEL,
						base->lcla_pool.pages);
		if (!page_list[i]) {

			d40_err(base->dev, "Failed to allocate %d pages.\n",
				base->lcla_pool.pages);
			ret = -ENOMEM;

			for (j = 0; j < i; j++)
				free_pages(page_list[j], base->lcla_pool.pages);
			goto free_page_list;
		}

		if ((virt_to_phys((void *)page_list[i]) &
		     (LCLA_ALIGNMENT - 1)) == 0)
			break;
	}

	for (j = 0; j < i; j++)
		free_pages(page_list[j], base->lcla_pool.pages);

	if (i < MAX_LCLA_ALLOC_ATTEMPTS) {
		base->lcla_pool.base = (void *)page_list[i];
	} else {
		/*
		 * After many attempts and no succees with finding the correct
		 * alignment, try with allocating a big buffer.
		 */
		dev_warn(base->dev,
			 "[%s] Failed to get %d pages @ 18 bit align.\n",
			 __func__, base->lcla_pool.pages);
		base->lcla_pool.base_unaligned = kmalloc(SZ_1K *
							 base->num_phy_chans +
							 LCLA_ALIGNMENT,
							 GFP_KERNEL);
		if (!base->lcla_pool.base_unaligned) {
			ret = -ENOMEM;
			goto free_page_list;
		}

		base->lcla_pool.base = PTR_ALIGN(base->lcla_pool.base_unaligned,
						 LCLA_ALIGNMENT);
	}

	pool->dma_addr = dma_map_single(base->dev, pool->base,
					SZ_1K * base->num_phy_chans,
					DMA_TO_DEVICE);
	if (dma_mapping_error(base->dev, pool->dma_addr)) {
		pool->dma_addr = 0;
		ret = -ENOMEM;
		goto free_page_list;
	}

	writel(virt_to_phys(base->lcla_pool.base),
	       base->virtbase + D40_DREG_LCLA);
	ret = 0;
 free_page_list:
	kfree(page_list);
	return ret;
}

static int __init d40_of_probe(struct platform_device *pdev,
			       struct device_node *np)
{
	struct stedma40_platform_data *pdata;
	int num_phy = 0, num_memcpy = 0, num_disabled = 0;
	const __be32 *list;

	pdata = devm_kzalloc(&pdev->dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	/* If absent this value will be obtained from h/w. */
	of_property_read_u32(np, "dma-channels", &num_phy);
	if (num_phy > 0)
		pdata->num_of_phy_chans = num_phy;

	list = of_get_property(np, "memcpy-channels", &num_memcpy);
	num_memcpy /= sizeof(*list);

	if (num_memcpy > D40_MEMCPY_MAX_CHANS || num_memcpy <= 0) {
		d40_err(&pdev->dev,
			"Invalid number of memcpy channels specified (%d)\n",
			num_memcpy);
		return -EINVAL;
	}
	pdata->num_of_memcpy_chans = num_memcpy;

	of_property_read_u32_array(np, "memcpy-channels",
				   dma40_memcpy_channels,
				   num_memcpy);

	list = of_get_property(np, "disabled-channels", &num_disabled);
	num_disabled /= sizeof(*list);

	if (num_disabled >= STEDMA40_MAX_PHYS || num_disabled < 0) {
		d40_err(&pdev->dev,
			"Invalid number of disabled channels specified (%d)\n",
			num_disabled);
		return -EINVAL;
	}

	of_property_read_u32_array(np, "disabled-channels",
				   pdata->disabled_channels,
				   num_disabled);
	pdata->disabled_channels[num_disabled] = -1;

	pdev->dev.platform_data = pdata;

	return 0;
}

static int __init d40_probe(struct platform_device *pdev)
{
	struct stedma40_platform_data *plat_data = dev_get_platdata(&pdev->dev);
	struct device_node *np = pdev->dev.of_node;
	int ret = -ENOENT;
	struct d40_base *base;
	struct resource *res;
	int num_reserved_chans;
	u32 val;

	if (!plat_data) {
		if (np) {
			if (d40_of_probe(pdev, np)) {
				ret = -ENOMEM;
				goto report_failure;
			}
		} else {
			d40_err(&pdev->dev, "No pdata or Device Tree provided\n");
			goto report_failure;
		}
	}

	base = d40_hw_detect_init(pdev);
	if (!base)
		goto report_failure;

	num_reserved_chans = d40_phy_res_init(base);

	platform_set_drvdata(pdev, base);

	spin_lock_init(&base->interrupt_lock);
	spin_lock_init(&base->execmd_lock);

	/* Get IO for logical channel parameter address */
	res = platform_get_resource_byname(pdev, IORESOURCE_MEM, "lcpa");
	if (!res) {
		ret = -ENOENT;
		d40_err(&pdev->dev, "No \"lcpa\" memory resource\n");
		goto destroy_cache;
	}
	base->lcpa_size = resource_size(res);
	base->phy_lcpa = res->start;

	if (request_mem_region(res->start, resource_size(res),
			       D40_NAME " I/O lcpa") == NULL) {
		ret = -EBUSY;
		d40_err(&pdev->dev, "Failed to request LCPA region %pR\n", res);
		goto destroy_cache;
	}

	/* We make use of ESRAM memory for this. */
	val = readl(base->virtbase + D40_DREG_LCPA);
	if (res->start != val && val != 0) {
		dev_warn(&pdev->dev,
			 "[%s] Mismatch LCPA dma 0x%x, def %pa\n",
			 __func__, val, &res->start);
	} else
		writel(res->start, base->virtbase + D40_DREG_LCPA);

	base->lcpa_base = ioremap(res->start, resource_size(res));
	if (!base->lcpa_base) {
		ret = -ENOMEM;
		d40_err(&pdev->dev, "Failed to ioremap LCPA region\n");
		goto destroy_cache;
	}
	/* If lcla has to be located in ESRAM we don't need to allocate */
	if (base->plat_data->use_esram_lcla) {
		res = platform_get_resource_byname(pdev, IORESOURCE_MEM,
							"lcla_esram");
		if (!res) {
			ret = -ENOENT;
			d40_err(&pdev->dev,
				"No \"lcla_esram\" memory resource\n");
			goto destroy_cache;
		}
		base->lcla_pool.base = ioremap(res->start,
						resource_size(res));
		if (!base->lcla_pool.base) {
			ret = -ENOMEM;
			d40_err(&pdev->dev, "Failed to ioremap LCLA region\n");
			goto destroy_cache;
		}
		writel(res->start, base->virtbase + D40_DREG_LCLA);

	} else {
		ret = d40_lcla_allocate(base);
		if (ret) {
			d40_err(&pdev->dev, "Failed to allocate LCLA area\n");
			goto destroy_cache;
		}
	}

	spin_lock_init(&base->lcla_pool.lock);

	base->irq = platform_get_irq(pdev, 0);

	ret = request_irq(base->irq, d40_handle_interrupt, 0, D40_NAME, base);
	if (ret) {
		d40_err(&pdev->dev, "No IRQ defined\n");
		goto destroy_cache;
	}

	if (base->plat_data->use_esram_lcla) {

		base->lcpa_regulator = regulator_get(base->dev, "lcla_esram");
		if (IS_ERR(base->lcpa_regulator)) {
			d40_err(&pdev->dev, "Failed to get lcpa_regulator\n");
			ret = PTR_ERR(base->lcpa_regulator);
			base->lcpa_regulator = NULL;
			goto destroy_cache;
		}

		ret = regulator_enable(base->lcpa_regulator);
		if (ret) {
			d40_err(&pdev->dev,
				"Failed to enable lcpa_regulator\n");
			regulator_put(base->lcpa_regulator);
			base->lcpa_regulator = NULL;
			goto destroy_cache;
		}
	}

	writel_relaxed(D40_DREG_GCC_ENABLE_ALL, base->virtbase + D40_DREG_GCC);

	pm_runtime_irq_safe(base->dev);
	pm_runtime_set_autosuspend_delay(base->dev, DMA40_AUTOSUSPEND_DELAY);
	pm_runtime_use_autosuspend(base->dev);
	pm_runtime_mark_last_busy(base->dev);
	pm_runtime_set_active(base->dev);
	pm_runtime_enable(base->dev);

	ret = d40_dmaengine_init(base, num_reserved_chans);
	if (ret)
		goto destroy_cache;

	base->dev->dma_parms = &base->dma_parms;
	ret = dma_set_max_seg_size(base->dev, STEDMA40_MAX_SEG_SIZE);
	if (ret) {
		d40_err(&pdev->dev, "Failed to set dma max seg size\n");
		goto destroy_cache;
	}

	d40_hw_init(base);

	if (np) {
		ret = of_dma_controller_register(np, d40_xlate, NULL);
		if (ret)
			dev_err(&pdev->dev,
				"could not register of_dma_controller\n");
	}

	dev_info(base->dev, "initialized\n");
	return 0;
 destroy_cache:
	kmem_cache_destroy(base->desc_slab);
	if (base->virtbase)
		iounmap(base->virtbase);

	if (base->lcla_pool.base && base->plat_data->use_esram_lcla) {
		iounmap(base->lcla_pool.base);
		base->lcla_pool.base = NULL;
	}

	if (base->lcla_pool.dma_addr)
		dma_unmap_single(base->dev, base->lcla_pool.dma_addr,
				 SZ_1K * base->num_phy_chans,
				 DMA_TO_DEVICE);

	if (!base->lcla_pool.base_unaligned && base->lcla_pool.base)
		free_pages((unsigned long)base->lcla_pool.base,
			   base->lcla_pool.pages);

	kfree(base->lcla_pool.base_unaligned);

	if (base->phy_lcpa)
		release_mem_region(base->phy_lcpa,
				   base->lcpa_size);
	if (base->phy_start)
		release_mem_region(base->phy_start,
				   base->phy_size);
	if (base->clk) {
		clk_disable_unprepare(base->clk);
		clk_put(base->clk);
	}

	if (base->lcpa_regulator) {
		regulator_disable(base->lcpa_regulator);
		regulator_put(base->lcpa_regulator);
	}

	kfree(base->lcla_pool.alloc_map);
	kfree(base->lookup_log_chans);
	kfree(base->lookup_phy_chans);
	kfree(base->phy_res);
	kfree(base);
 report_failure:
	d40_err(&pdev->dev, "probe failed\n");
	return ret;
}

static const struct of_device_id d40_match[] = {
        { .compatible = "stericsson,dma40", },
        {}
};

static struct platform_driver d40_driver = {
	.driver = {
		.name  = D40_NAME,
		.pm = &dma40_pm_ops,
		.of_match_table = d40_match,
	},
};

static int __init stedma40_init(void)
{
	return platform_driver_probe(&d40_driver, d40_probe);
}
subsys_initcall(stedma40_init);
