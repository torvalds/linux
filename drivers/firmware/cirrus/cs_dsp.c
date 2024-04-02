// SPDX-License-Identifier: GPL-2.0-only
/*
 * cs_dsp.c  --  Cirrus Logic DSP firmware support
 *
 * Based on sound/soc/codecs/wm_adsp.c
 *
 * Copyright 2012 Wolfson Microelectronics plc
 * Copyright (C) 2015-2021 Cirrus Logic, Inc. and
 *                         Cirrus Logic International Semiconductor Ltd.
 */

#include <linux/ctype.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/vmalloc.h>

#include <linux/firmware/cirrus/cs_dsp.h>
#include <linux/firmware/cirrus/wmfw.h>

#define cs_dsp_err(_dsp, fmt, ...) \
	dev_err(_dsp->dev, "%s: " fmt, _dsp->name, ##__VA_ARGS__)
#define cs_dsp_warn(_dsp, fmt, ...) \
	dev_warn(_dsp->dev, "%s: " fmt, _dsp->name, ##__VA_ARGS__)
#define cs_dsp_info(_dsp, fmt, ...) \
	dev_info(_dsp->dev, "%s: " fmt, _dsp->name, ##__VA_ARGS__)
#define cs_dsp_dbg(_dsp, fmt, ...) \
	dev_dbg(_dsp->dev, "%s: " fmt, _dsp->name, ##__VA_ARGS__)

#define ADSP1_CONTROL_1                   0x00
#define ADSP1_CONTROL_2                   0x02
#define ADSP1_CONTROL_3                   0x03
#define ADSP1_CONTROL_4                   0x04
#define ADSP1_CONTROL_5                   0x06
#define ADSP1_CONTROL_6                   0x07
#define ADSP1_CONTROL_7                   0x08
#define ADSP1_CONTROL_8                   0x09
#define ADSP1_CONTROL_9                   0x0A
#define ADSP1_CONTROL_10                  0x0B
#define ADSP1_CONTROL_11                  0x0C
#define ADSP1_CONTROL_12                  0x0D
#define ADSP1_CONTROL_13                  0x0F
#define ADSP1_CONTROL_14                  0x10
#define ADSP1_CONTROL_15                  0x11
#define ADSP1_CONTROL_16                  0x12
#define ADSP1_CONTROL_17                  0x13
#define ADSP1_CONTROL_18                  0x14
#define ADSP1_CONTROL_19                  0x16
#define ADSP1_CONTROL_20                  0x17
#define ADSP1_CONTROL_21                  0x18
#define ADSP1_CONTROL_22                  0x1A
#define ADSP1_CONTROL_23                  0x1B
#define ADSP1_CONTROL_24                  0x1C
#define ADSP1_CONTROL_25                  0x1E
#define ADSP1_CONTROL_26                  0x20
#define ADSP1_CONTROL_27                  0x21
#define ADSP1_CONTROL_28                  0x22
#define ADSP1_CONTROL_29                  0x23
#define ADSP1_CONTROL_30                  0x24
#define ADSP1_CONTROL_31                  0x26

/*
 * ADSP1 Control 19
 */
#define ADSP1_WDMA_BUFFER_LENGTH_MASK     0x00FF  /* DSP1_WDMA_BUFFER_LENGTH - [7:0] */
#define ADSP1_WDMA_BUFFER_LENGTH_SHIFT         0  /* DSP1_WDMA_BUFFER_LENGTH - [7:0] */
#define ADSP1_WDMA_BUFFER_LENGTH_WIDTH         8  /* DSP1_WDMA_BUFFER_LENGTH - [7:0] */

/*
 * ADSP1 Control 30
 */
#define ADSP1_DBG_CLK_ENA                 0x0008  /* DSP1_DBG_CLK_ENA */
#define ADSP1_DBG_CLK_ENA_MASK            0x0008  /* DSP1_DBG_CLK_ENA */
#define ADSP1_DBG_CLK_ENA_SHIFT                3  /* DSP1_DBG_CLK_ENA */
#define ADSP1_DBG_CLK_ENA_WIDTH                1  /* DSP1_DBG_CLK_ENA */
#define ADSP1_SYS_ENA                     0x0004  /* DSP1_SYS_ENA */
#define ADSP1_SYS_ENA_MASK                0x0004  /* DSP1_SYS_ENA */
#define ADSP1_SYS_ENA_SHIFT                    2  /* DSP1_SYS_ENA */
#define ADSP1_SYS_ENA_WIDTH                    1  /* DSP1_SYS_ENA */
#define ADSP1_CORE_ENA                    0x0002  /* DSP1_CORE_ENA */
#define ADSP1_CORE_ENA_MASK               0x0002  /* DSP1_CORE_ENA */
#define ADSP1_CORE_ENA_SHIFT                   1  /* DSP1_CORE_ENA */
#define ADSP1_CORE_ENA_WIDTH                   1  /* DSP1_CORE_ENA */
#define ADSP1_START                       0x0001  /* DSP1_START */
#define ADSP1_START_MASK                  0x0001  /* DSP1_START */
#define ADSP1_START_SHIFT                      0  /* DSP1_START */
#define ADSP1_START_WIDTH                      1  /* DSP1_START */

/*
 * ADSP1 Control 31
 */
#define ADSP1_CLK_SEL_MASK                0x0007  /* CLK_SEL_ENA */
#define ADSP1_CLK_SEL_SHIFT                    0  /* CLK_SEL_ENA */
#define ADSP1_CLK_SEL_WIDTH                    3  /* CLK_SEL_ENA */

#define ADSP2_CONTROL                     0x0
#define ADSP2_CLOCKING                    0x1
#define ADSP2V2_CLOCKING                  0x2
#define ADSP2_STATUS1                     0x4
#define ADSP2_WDMA_CONFIG_1               0x30
#define ADSP2_WDMA_CONFIG_2               0x31
#define ADSP2V2_WDMA_CONFIG_2             0x32
#define ADSP2_RDMA_CONFIG_1               0x34

#define ADSP2_SCRATCH0                    0x40
#define ADSP2_SCRATCH1                    0x41
#define ADSP2_SCRATCH2                    0x42
#define ADSP2_SCRATCH3                    0x43

#define ADSP2V2_SCRATCH0_1                0x40
#define ADSP2V2_SCRATCH2_3                0x42

/*
 * ADSP2 Control
 */
#define ADSP2_MEM_ENA                     0x0010  /* DSP1_MEM_ENA */
#define ADSP2_MEM_ENA_MASK                0x0010  /* DSP1_MEM_ENA */
#define ADSP2_MEM_ENA_SHIFT                    4  /* DSP1_MEM_ENA */
#define ADSP2_MEM_ENA_WIDTH                    1  /* DSP1_MEM_ENA */
#define ADSP2_SYS_ENA                     0x0004  /* DSP1_SYS_ENA */
#define ADSP2_SYS_ENA_MASK                0x0004  /* DSP1_SYS_ENA */
#define ADSP2_SYS_ENA_SHIFT                    2  /* DSP1_SYS_ENA */
#define ADSP2_SYS_ENA_WIDTH                    1  /* DSP1_SYS_ENA */
#define ADSP2_CORE_ENA                    0x0002  /* DSP1_CORE_ENA */
#define ADSP2_CORE_ENA_MASK               0x0002  /* DSP1_CORE_ENA */
#define ADSP2_CORE_ENA_SHIFT                   1  /* DSP1_CORE_ENA */
#define ADSP2_CORE_ENA_WIDTH                   1  /* DSP1_CORE_ENA */
#define ADSP2_START                       0x0001  /* DSP1_START */
#define ADSP2_START_MASK                  0x0001  /* DSP1_START */
#define ADSP2_START_SHIFT                      0  /* DSP1_START */
#define ADSP2_START_WIDTH                      1  /* DSP1_START */

/*
 * ADSP2 clocking
 */
#define ADSP2_CLK_SEL_MASK                0x0007  /* CLK_SEL_ENA */
#define ADSP2_CLK_SEL_SHIFT                    0  /* CLK_SEL_ENA */
#define ADSP2_CLK_SEL_WIDTH                    3  /* CLK_SEL_ENA */

/*
 * ADSP2V2 clocking
 */
#define ADSP2V2_CLK_SEL_MASK             0x70000  /* CLK_SEL_ENA */
#define ADSP2V2_CLK_SEL_SHIFT                 16  /* CLK_SEL_ENA */
#define ADSP2V2_CLK_SEL_WIDTH                  3  /* CLK_SEL_ENA */

#define ADSP2V2_RATE_MASK                 0x7800  /* DSP_RATE */
#define ADSP2V2_RATE_SHIFT                    11  /* DSP_RATE */
#define ADSP2V2_RATE_WIDTH                     4  /* DSP_RATE */

/*
 * ADSP2 Status 1
 */
#define ADSP2_RAM_RDY                     0x0001
#define ADSP2_RAM_RDY_MASK                0x0001
#define ADSP2_RAM_RDY_SHIFT                    0
#define ADSP2_RAM_RDY_WIDTH                    1

/*
 * ADSP2 Lock support
 */
#define ADSP2_LOCK_CODE_0                    0x5555
#define ADSP2_LOCK_CODE_1                    0xAAAA

#define ADSP2_WATCHDOG                       0x0A
#define ADSP2_BUS_ERR_ADDR                   0x52
#define ADSP2_REGION_LOCK_STATUS             0x64
#define ADSP2_LOCK_REGION_1_LOCK_REGION_0    0x66
#define ADSP2_LOCK_REGION_3_LOCK_REGION_2    0x68
#define ADSP2_LOCK_REGION_5_LOCK_REGION_4    0x6A
#define ADSP2_LOCK_REGION_7_LOCK_REGION_6    0x6C
#define ADSP2_LOCK_REGION_9_LOCK_REGION_8    0x6E
#define ADSP2_LOCK_REGION_CTRL               0x7A
#define ADSP2_PMEM_ERR_ADDR_XMEM_ERR_ADDR    0x7C

#define ADSP2_REGION_LOCK_ERR_MASK           0x8000
#define ADSP2_ADDR_ERR_MASK                  0x4000
#define ADSP2_WDT_TIMEOUT_STS_MASK           0x2000
#define ADSP2_CTRL_ERR_PAUSE_ENA             0x0002
#define ADSP2_CTRL_ERR_EINT                  0x0001

#define ADSP2_BUS_ERR_ADDR_MASK              0x00FFFFFF
#define ADSP2_XMEM_ERR_ADDR_MASK             0x0000FFFF
#define ADSP2_PMEM_ERR_ADDR_MASK             0x7FFF0000
#define ADSP2_PMEM_ERR_ADDR_SHIFT            16
#define ADSP2_WDT_ENA_MASK                   0xFFFFFFFD

#define ADSP2_LOCK_REGION_SHIFT              16

/*
 * Event control messages
 */
#define CS_DSP_FW_EVENT_SHUTDOWN             0x000001

/*
 * HALO system info
 */
#define HALO_AHBM_WINDOW_DEBUG_0             0x02040
#define HALO_AHBM_WINDOW_DEBUG_1             0x02044

/*
 * HALO core
 */
#define HALO_SCRATCH1                        0x005c0
#define HALO_SCRATCH2                        0x005c8
#define HALO_SCRATCH3                        0x005d0
#define HALO_SCRATCH4                        0x005d8
#define HALO_CCM_CORE_CONTROL                0x41000
#define HALO_CORE_SOFT_RESET                 0x00010
#define HALO_WDT_CONTROL                     0x47000

/*
 * HALO MPU banks
 */
#define HALO_MPU_XMEM_ACCESS_0               0x43000
#define HALO_MPU_YMEM_ACCESS_0               0x43004
#define HALO_MPU_WINDOW_ACCESS_0             0x43008
#define HALO_MPU_XREG_ACCESS_0               0x4300C
#define HALO_MPU_YREG_ACCESS_0               0x43014
#define HALO_MPU_XMEM_ACCESS_1               0x43018
#define HALO_MPU_YMEM_ACCESS_1               0x4301C
#define HALO_MPU_WINDOW_ACCESS_1             0x43020
#define HALO_MPU_XREG_ACCESS_1               0x43024
#define HALO_MPU_YREG_ACCESS_1               0x4302C
#define HALO_MPU_XMEM_ACCESS_2               0x43030
#define HALO_MPU_YMEM_ACCESS_2               0x43034
#define HALO_MPU_WINDOW_ACCESS_2             0x43038
#define HALO_MPU_XREG_ACCESS_2               0x4303C
#define HALO_MPU_YREG_ACCESS_2               0x43044
#define HALO_MPU_XMEM_ACCESS_3               0x43048
#define HALO_MPU_YMEM_ACCESS_3               0x4304C
#define HALO_MPU_WINDOW_ACCESS_3             0x43050
#define HALO_MPU_XREG_ACCESS_3               0x43054
#define HALO_MPU_YREG_ACCESS_3               0x4305C
#define HALO_MPU_XM_VIO_ADDR                 0x43100
#define HALO_MPU_XM_VIO_STATUS               0x43104
#define HALO_MPU_YM_VIO_ADDR                 0x43108
#define HALO_MPU_YM_VIO_STATUS               0x4310C
#define HALO_MPU_PM_VIO_ADDR                 0x43110
#define HALO_MPU_PM_VIO_STATUS               0x43114
#define HALO_MPU_LOCK_CONFIG                 0x43140

/*
 * HALO_AHBM_WINDOW_DEBUG_1
 */
#define HALO_AHBM_CORE_ERR_ADDR_MASK         0x0fffff00
#define HALO_AHBM_CORE_ERR_ADDR_SHIFT                 8
#define HALO_AHBM_FLAGS_ERR_MASK             0x000000ff

/*
 * HALO_CCM_CORE_CONTROL
 */
#define HALO_CORE_RESET                     0x00000200
#define HALO_CORE_EN                        0x00000001

/*
 * HALO_CORE_SOFT_RESET
 */
#define HALO_CORE_SOFT_RESET_MASK           0x00000001

/*
 * HALO_WDT_CONTROL
 */
#define HALO_WDT_EN_MASK                    0x00000001

/*
 * HALO_MPU_?M_VIO_STATUS
 */
#define HALO_MPU_VIO_STS_MASK               0x007e0000
#define HALO_MPU_VIO_STS_SHIFT                      17
#define HALO_MPU_VIO_ERR_WR_MASK            0x00008000
#define HALO_MPU_VIO_ERR_SRC_MASK           0x00007fff
#define HALO_MPU_VIO_ERR_SRC_SHIFT                   0

struct cs_dsp_ops {
	bool (*validate_version)(struct cs_dsp *dsp, unsigned int version);
	unsigned int (*parse_sizes)(struct cs_dsp *dsp,
				    const char * const file,
				    unsigned int pos,
				    const struct firmware *firmware);
	int (*setup_algs)(struct cs_dsp *dsp);
	unsigned int (*region_to_reg)(struct cs_dsp_region const *mem,
				      unsigned int offset);

	void (*show_fw_status)(struct cs_dsp *dsp);
	void (*stop_watchdog)(struct cs_dsp *dsp);

	int (*enable_memory)(struct cs_dsp *dsp);
	void (*disable_memory)(struct cs_dsp *dsp);
	int (*lock_memory)(struct cs_dsp *dsp, unsigned int lock_regions);

	int (*enable_core)(struct cs_dsp *dsp);
	void (*disable_core)(struct cs_dsp *dsp);

	int (*start_core)(struct cs_dsp *dsp);
	void (*stop_core)(struct cs_dsp *dsp);
};

static const struct cs_dsp_ops cs_dsp_adsp1_ops;
static const struct cs_dsp_ops cs_dsp_adsp2_ops[];
static const struct cs_dsp_ops cs_dsp_halo_ops;
static const struct cs_dsp_ops cs_dsp_halo_ao_ops;

struct cs_dsp_buf {
	struct list_head list;
	void *buf;
};

static struct cs_dsp_buf *cs_dsp_buf_alloc(const void *src, size_t len,
					   struct list_head *list)
{
	struct cs_dsp_buf *buf = kzalloc(sizeof(*buf), GFP_KERNEL);

	if (buf == NULL)
		return NULL;

	buf->buf = vmalloc(len);
	if (!buf->buf) {
		kfree(buf);
		return NULL;
	}
	memcpy(buf->buf, src, len);

	if (list)
		list_add_tail(&buf->list, list);

	return buf;
}

static void cs_dsp_buf_free(struct list_head *list)
{
	while (!list_empty(list)) {
		struct cs_dsp_buf *buf = list_first_entry(list,
							  struct cs_dsp_buf,
							  list);
		list_del(&buf->list);
		vfree(buf->buf);
		kfree(buf);
	}
}

/**
 * cs_dsp_mem_region_name() - Return a name string for a memory type
 * @type: the memory type to match
 *
 * Return: A const string identifying the memory region.
 */
const char *cs_dsp_mem_region_name(unsigned int type)
{
	switch (type) {
	case WMFW_ADSP1_PM:
		return "PM";
	case WMFW_HALO_PM_PACKED:
		return "PM_PACKED";
	case WMFW_ADSP1_DM:
		return "DM";
	case WMFW_ADSP2_XM:
		return "XM";
	case WMFW_HALO_XM_PACKED:
		return "XM_PACKED";
	case WMFW_ADSP2_YM:
		return "YM";
	case WMFW_HALO_YM_PACKED:
		return "YM_PACKED";
	case WMFW_ADSP1_ZM:
		return "ZM";
	default:
		return NULL;
	}
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_mem_region_name, FW_CS_DSP);

#ifdef CONFIG_DEBUG_FS
static void cs_dsp_debugfs_save_wmfwname(struct cs_dsp *dsp, const char *s)
{
	char *tmp = kasprintf(GFP_KERNEL, "%s\n", s);

	kfree(dsp->wmfw_file_name);
	dsp->wmfw_file_name = tmp;
}

static void cs_dsp_debugfs_save_binname(struct cs_dsp *dsp, const char *s)
{
	char *tmp = kasprintf(GFP_KERNEL, "%s\n", s);

	kfree(dsp->bin_file_name);
	dsp->bin_file_name = tmp;
}

static void cs_dsp_debugfs_clear(struct cs_dsp *dsp)
{
	kfree(dsp->wmfw_file_name);
	kfree(dsp->bin_file_name);
	dsp->wmfw_file_name = NULL;
	dsp->bin_file_name = NULL;
}

static ssize_t cs_dsp_debugfs_wmfw_read(struct file *file,
					char __user *user_buf,
					size_t count, loff_t *ppos)
{
	struct cs_dsp *dsp = file->private_data;
	ssize_t ret;

	mutex_lock(&dsp->pwr_lock);

	if (!dsp->wmfw_file_name || !dsp->booted)
		ret = 0;
	else
		ret = simple_read_from_buffer(user_buf, count, ppos,
					      dsp->wmfw_file_name,
					      strlen(dsp->wmfw_file_name));

	mutex_unlock(&dsp->pwr_lock);
	return ret;
}

static ssize_t cs_dsp_debugfs_bin_read(struct file *file,
				       char __user *user_buf,
				       size_t count, loff_t *ppos)
{
	struct cs_dsp *dsp = file->private_data;
	ssize_t ret;

	mutex_lock(&dsp->pwr_lock);

	if (!dsp->bin_file_name || !dsp->booted)
		ret = 0;
	else
		ret = simple_read_from_buffer(user_buf, count, ppos,
					      dsp->bin_file_name,
					      strlen(dsp->bin_file_name));

	mutex_unlock(&dsp->pwr_lock);
	return ret;
}

static const struct {
	const char *name;
	const struct file_operations fops;
} cs_dsp_debugfs_fops[] = {
	{
		.name = "wmfw_file_name",
		.fops = {
			.open = simple_open,
			.read = cs_dsp_debugfs_wmfw_read,
		},
	},
	{
		.name = "bin_file_name",
		.fops = {
			.open = simple_open,
			.read = cs_dsp_debugfs_bin_read,
		},
	},
};

static int cs_dsp_coeff_base_reg(struct cs_dsp_coeff_ctl *ctl, unsigned int *reg,
				 unsigned int off);

static int cs_dsp_debugfs_read_controls_show(struct seq_file *s, void *ignored)
{
	struct cs_dsp *dsp = s->private;
	struct cs_dsp_coeff_ctl *ctl;
	unsigned int reg;

	list_for_each_entry(ctl, &dsp->ctl_list, list) {
		cs_dsp_coeff_base_reg(ctl, &reg, 0);
		seq_printf(s, "%22.*s: %#8zx %s:%08x %#8x %s %#8x %#4x %c%c%c%c %s %s\n",
			   ctl->subname_len, ctl->subname, ctl->len,
			   cs_dsp_mem_region_name(ctl->alg_region.type),
			   ctl->offset, reg, ctl->fw_name, ctl->alg_region.alg, ctl->type,
			   ctl->flags & WMFW_CTL_FLAG_VOLATILE ? 'V' : '-',
			   ctl->flags & WMFW_CTL_FLAG_SYS ? 'S' : '-',
			   ctl->flags & WMFW_CTL_FLAG_READABLE ? 'R' : '-',
			   ctl->flags & WMFW_CTL_FLAG_WRITEABLE ? 'W' : '-',
			   ctl->enabled ? "enabled" : "disabled",
			   ctl->set ? "dirty" : "clean");
	}

	return 0;
}
DEFINE_SHOW_ATTRIBUTE(cs_dsp_debugfs_read_controls);

/**
 * cs_dsp_init_debugfs() - Create and populate DSP representation in debugfs
 * @dsp: pointer to DSP structure
 * @debugfs_root: pointer to debugfs directory in which to create this DSP
 *                representation
 */
void cs_dsp_init_debugfs(struct cs_dsp *dsp, struct dentry *debugfs_root)
{
	struct dentry *root = NULL;
	int i;

	root = debugfs_create_dir(dsp->name, debugfs_root);

	debugfs_create_bool("booted", 0444, root, &dsp->booted);
	debugfs_create_bool("running", 0444, root, &dsp->running);
	debugfs_create_x32("fw_id", 0444, root, &dsp->fw_id);
	debugfs_create_x32("fw_version", 0444, root, &dsp->fw_id_version);

	for (i = 0; i < ARRAY_SIZE(cs_dsp_debugfs_fops); ++i)
		debugfs_create_file(cs_dsp_debugfs_fops[i].name, 0444, root,
				    dsp, &cs_dsp_debugfs_fops[i].fops);

	debugfs_create_file("controls", 0444, root, dsp,
			    &cs_dsp_debugfs_read_controls_fops);

	dsp->debugfs_root = root;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_init_debugfs, FW_CS_DSP);

/**
 * cs_dsp_cleanup_debugfs() - Removes DSP representation from debugfs
 * @dsp: pointer to DSP structure
 */
void cs_dsp_cleanup_debugfs(struct cs_dsp *dsp)
{
	cs_dsp_debugfs_clear(dsp);
	debugfs_remove_recursive(dsp->debugfs_root);
	dsp->debugfs_root = ERR_PTR(-ENODEV);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_cleanup_debugfs, FW_CS_DSP);
#else
void cs_dsp_init_debugfs(struct cs_dsp *dsp, struct dentry *debugfs_root)
{
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_init_debugfs, FW_CS_DSP);

void cs_dsp_cleanup_debugfs(struct cs_dsp *dsp)
{
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_cleanup_debugfs, FW_CS_DSP);

static inline void cs_dsp_debugfs_save_wmfwname(struct cs_dsp *dsp,
						const char *s)
{
}

static inline void cs_dsp_debugfs_save_binname(struct cs_dsp *dsp,
					       const char *s)
{
}

static inline void cs_dsp_debugfs_clear(struct cs_dsp *dsp)
{
}
#endif

static const struct cs_dsp_region *cs_dsp_find_region(struct cs_dsp *dsp,
						      int type)
{
	int i;

	for (i = 0; i < dsp->num_mems; i++)
		if (dsp->mem[i].type == type)
			return &dsp->mem[i];

	return NULL;
}

static unsigned int cs_dsp_region_to_reg(struct cs_dsp_region const *mem,
					 unsigned int offset)
{
	switch (mem->type) {
	case WMFW_ADSP1_PM:
		return mem->base + (offset * 3);
	case WMFW_ADSP1_DM:
	case WMFW_ADSP2_XM:
	case WMFW_ADSP2_YM:
	case WMFW_ADSP1_ZM:
		return mem->base + (offset * 2);
	default:
		WARN(1, "Unknown memory region type");
		return offset;
	}
}

static unsigned int cs_dsp_halo_region_to_reg(struct cs_dsp_region const *mem,
					      unsigned int offset)
{
	switch (mem->type) {
	case WMFW_ADSP2_XM:
	case WMFW_ADSP2_YM:
		return mem->base + (offset * 4);
	case WMFW_HALO_XM_PACKED:
	case WMFW_HALO_YM_PACKED:
		return (mem->base + (offset * 3)) & ~0x3;
	case WMFW_HALO_PM_PACKED:
		return mem->base + (offset * 5);
	default:
		WARN(1, "Unknown memory region type");
		return offset;
	}
}

static void cs_dsp_read_fw_status(struct cs_dsp *dsp,
				  int noffs, unsigned int *offs)
{
	unsigned int i;
	int ret;

	for (i = 0; i < noffs; ++i) {
		ret = regmap_read(dsp->regmap, dsp->base + offs[i], &offs[i]);
		if (ret) {
			cs_dsp_err(dsp, "Failed to read SCRATCH%u: %d\n", i, ret);
			return;
		}
	}
}

static void cs_dsp_adsp2_show_fw_status(struct cs_dsp *dsp)
{
	unsigned int offs[] = {
		ADSP2_SCRATCH0, ADSP2_SCRATCH1, ADSP2_SCRATCH2, ADSP2_SCRATCH3,
	};

	cs_dsp_read_fw_status(dsp, ARRAY_SIZE(offs), offs);

	cs_dsp_dbg(dsp, "FW SCRATCH 0:0x%x 1:0x%x 2:0x%x 3:0x%x\n",
		   offs[0], offs[1], offs[2], offs[3]);
}

static void cs_dsp_adsp2v2_show_fw_status(struct cs_dsp *dsp)
{
	unsigned int offs[] = { ADSP2V2_SCRATCH0_1, ADSP2V2_SCRATCH2_3 };

	cs_dsp_read_fw_status(dsp, ARRAY_SIZE(offs), offs);

	cs_dsp_dbg(dsp, "FW SCRATCH 0:0x%x 1:0x%x 2:0x%x 3:0x%x\n",
		   offs[0] & 0xFFFF, offs[0] >> 16,
		   offs[1] & 0xFFFF, offs[1] >> 16);
}

static void cs_dsp_halo_show_fw_status(struct cs_dsp *dsp)
{
	unsigned int offs[] = {
		HALO_SCRATCH1, HALO_SCRATCH2, HALO_SCRATCH3, HALO_SCRATCH4,
	};

	cs_dsp_read_fw_status(dsp, ARRAY_SIZE(offs), offs);

	cs_dsp_dbg(dsp, "FW SCRATCH 0:0x%x 1:0x%x 2:0x%x 3:0x%x\n",
		   offs[0], offs[1], offs[2], offs[3]);
}

static int cs_dsp_coeff_base_reg(struct cs_dsp_coeff_ctl *ctl, unsigned int *reg,
				 unsigned int off)
{
	const struct cs_dsp_alg_region *alg_region = &ctl->alg_region;
	struct cs_dsp *dsp = ctl->dsp;
	const struct cs_dsp_region *mem;

	mem = cs_dsp_find_region(dsp, alg_region->type);
	if (!mem) {
		cs_dsp_err(dsp, "No base for region %x\n",
			   alg_region->type);
		return -EINVAL;
	}

	*reg = dsp->ops->region_to_reg(mem, ctl->alg_region.base + ctl->offset + off);

	return 0;
}

/**
 * cs_dsp_coeff_write_acked_control() - Sends event_id to the acked control
 * @ctl: pointer to acked coefficient control
 * @event_id: the value to write to the given acked control
 *
 * Once the value has been written to the control the function shall block
 * until the running firmware acknowledges the write or timeout is exceeded.
 *
 * Must be called with pwr_lock held.
 *
 * Return: Zero for success, a negative number on error.
 */
int cs_dsp_coeff_write_acked_control(struct cs_dsp_coeff_ctl *ctl, unsigned int event_id)
{
	struct cs_dsp *dsp = ctl->dsp;
	__be32 val = cpu_to_be32(event_id);
	unsigned int reg;
	int i, ret;

	lockdep_assert_held(&dsp->pwr_lock);

	if (!dsp->running)
		return -EPERM;

	ret = cs_dsp_coeff_base_reg(ctl, &reg, 0);
	if (ret)
		return ret;

	cs_dsp_dbg(dsp, "Sending 0x%x to acked control alg 0x%x %s:0x%x\n",
		   event_id, ctl->alg_region.alg,
		   cs_dsp_mem_region_name(ctl->alg_region.type), ctl->offset);

	ret = regmap_raw_write(dsp->regmap, reg, &val, sizeof(val));
	if (ret) {
		cs_dsp_err(dsp, "Failed to write %x: %d\n", reg, ret);
		return ret;
	}

	/*
	 * Poll for ack, we initially poll at ~1ms intervals for firmwares
	 * that respond quickly, then go to ~10ms polls. A firmware is unlikely
	 * to ack instantly so we do the first 1ms delay before reading the
	 * control to avoid a pointless bus transaction
	 */
	for (i = 0; i < CS_DSP_ACKED_CTL_TIMEOUT_MS;) {
		switch (i) {
		case 0 ... CS_DSP_ACKED_CTL_N_QUICKPOLLS - 1:
			usleep_range(1000, 2000);
			i++;
			break;
		default:
			usleep_range(10000, 20000);
			i += 10;
			break;
		}

		ret = regmap_raw_read(dsp->regmap, reg, &val, sizeof(val));
		if (ret) {
			cs_dsp_err(dsp, "Failed to read %x: %d\n", reg, ret);
			return ret;
		}

		if (val == 0) {
			cs_dsp_dbg(dsp, "Acked control ACKED at poll %u\n", i);
			return 0;
		}
	}

	cs_dsp_warn(dsp, "Acked control @0x%x alg:0x%x %s:0x%x timed out\n",
		    reg, ctl->alg_region.alg,
		    cs_dsp_mem_region_name(ctl->alg_region.type),
		    ctl->offset);

	return -ETIMEDOUT;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_coeff_write_acked_control, FW_CS_DSP);

static int cs_dsp_coeff_write_ctrl_raw(struct cs_dsp_coeff_ctl *ctl,
				       unsigned int off, const void *buf, size_t len)
{
	struct cs_dsp *dsp = ctl->dsp;
	void *scratch;
	int ret;
	unsigned int reg;

	ret = cs_dsp_coeff_base_reg(ctl, &reg, off);
	if (ret)
		return ret;

	scratch = kmemdup(buf, len, GFP_KERNEL | GFP_DMA);
	if (!scratch)
		return -ENOMEM;

	ret = regmap_raw_write(dsp->regmap, reg, scratch,
			       len);
	if (ret) {
		cs_dsp_err(dsp, "Failed to write %zu bytes to %x: %d\n",
			   len, reg, ret);
		kfree(scratch);
		return ret;
	}
	cs_dsp_dbg(dsp, "Wrote %zu bytes to %x\n", len, reg);

	kfree(scratch);

	return 0;
}

/**
 * cs_dsp_coeff_write_ctrl() - Writes the given buffer to the given coefficient control
 * @ctl: pointer to coefficient control
 * @off: word offset at which data should be written
 * @buf: the buffer to write to the given control
 * @len: the length of the buffer in bytes
 *
 * Must be called with pwr_lock held.
 *
 * Return: < 0 on error, 1 when the control value changed and 0 when it has not.
 */
int cs_dsp_coeff_write_ctrl(struct cs_dsp_coeff_ctl *ctl,
			    unsigned int off, const void *buf, size_t len)
{
	int ret = 0;

	if (!ctl)
		return -ENOENT;

	lockdep_assert_held(&ctl->dsp->pwr_lock);

	if (len + off * sizeof(u32) > ctl->len)
		return -EINVAL;

	if (ctl->flags & WMFW_CTL_FLAG_VOLATILE) {
		ret = -EPERM;
	} else if (buf != ctl->cache) {
		if (memcmp(ctl->cache + off * sizeof(u32), buf, len))
			memcpy(ctl->cache + off * sizeof(u32), buf, len);
		else
			return 0;
	}

	ctl->set = 1;
	if (ctl->enabled && ctl->dsp->running)
		ret = cs_dsp_coeff_write_ctrl_raw(ctl, off, buf, len);

	if (ret < 0)
		return ret;

	return 1;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_coeff_write_ctrl, FW_CS_DSP);

static int cs_dsp_coeff_read_ctrl_raw(struct cs_dsp_coeff_ctl *ctl,
				      unsigned int off, void *buf, size_t len)
{
	struct cs_dsp *dsp = ctl->dsp;
	void *scratch;
	int ret;
	unsigned int reg;

	ret = cs_dsp_coeff_base_reg(ctl, &reg, off);
	if (ret)
		return ret;

	scratch = kmalloc(len, GFP_KERNEL | GFP_DMA);
	if (!scratch)
		return -ENOMEM;

	ret = regmap_raw_read(dsp->regmap, reg, scratch, len);
	if (ret) {
		cs_dsp_err(dsp, "Failed to read %zu bytes from %x: %d\n",
			   len, reg, ret);
		kfree(scratch);
		return ret;
	}
	cs_dsp_dbg(dsp, "Read %zu bytes from %x\n", len, reg);

	memcpy(buf, scratch, len);
	kfree(scratch);

	return 0;
}

/**
 * cs_dsp_coeff_read_ctrl() - Reads the given coefficient control into the given buffer
 * @ctl: pointer to coefficient control
 * @off: word offset at which data should be read
 * @buf: the buffer to store to the given control
 * @len: the length of the buffer in bytes
 *
 * Must be called with pwr_lock held.
 *
 * Return: Zero for success, a negative number on error.
 */
int cs_dsp_coeff_read_ctrl(struct cs_dsp_coeff_ctl *ctl,
			   unsigned int off, void *buf, size_t len)
{
	int ret = 0;

	if (!ctl)
		return -ENOENT;

	lockdep_assert_held(&ctl->dsp->pwr_lock);

	if (len + off * sizeof(u32) > ctl->len)
		return -EINVAL;

	if (ctl->flags & WMFW_CTL_FLAG_VOLATILE) {
		if (ctl->enabled && ctl->dsp->running)
			return cs_dsp_coeff_read_ctrl_raw(ctl, off, buf, len);
		else
			return -EPERM;
	} else {
		if (!ctl->flags && ctl->enabled && ctl->dsp->running)
			ret = cs_dsp_coeff_read_ctrl_raw(ctl, 0, ctl->cache, ctl->len);

		if (buf != ctl->cache)
			memcpy(buf, ctl->cache + off * sizeof(u32), len);
	}

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_coeff_read_ctrl, FW_CS_DSP);

static int cs_dsp_coeff_init_control_caches(struct cs_dsp *dsp)
{
	struct cs_dsp_coeff_ctl *ctl;
	int ret;

	list_for_each_entry(ctl, &dsp->ctl_list, list) {
		if (!ctl->enabled || ctl->set)
			continue;
		if (ctl->flags & WMFW_CTL_FLAG_VOLATILE)
			continue;

		/*
		 * For readable controls populate the cache from the DSP memory.
		 * For non-readable controls the cache was zero-filled when
		 * created so we don't need to do anything.
		 */
		if (!ctl->flags || (ctl->flags & WMFW_CTL_FLAG_READABLE)) {
			ret = cs_dsp_coeff_read_ctrl_raw(ctl, 0, ctl->cache, ctl->len);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static int cs_dsp_coeff_sync_controls(struct cs_dsp *dsp)
{
	struct cs_dsp_coeff_ctl *ctl;
	int ret;

	list_for_each_entry(ctl, &dsp->ctl_list, list) {
		if (!ctl->enabled)
			continue;
		if (ctl->set && !(ctl->flags & WMFW_CTL_FLAG_VOLATILE)) {
			ret = cs_dsp_coeff_write_ctrl_raw(ctl, 0, ctl->cache,
							  ctl->len);
			if (ret < 0)
				return ret;
		}
	}

	return 0;
}

static void cs_dsp_signal_event_controls(struct cs_dsp *dsp,
					 unsigned int event)
{
	struct cs_dsp_coeff_ctl *ctl;
	int ret;

	list_for_each_entry(ctl, &dsp->ctl_list, list) {
		if (ctl->type != WMFW_CTL_TYPE_HOSTEVENT)
			continue;

		if (!ctl->enabled)
			continue;

		ret = cs_dsp_coeff_write_acked_control(ctl, event);
		if (ret)
			cs_dsp_warn(dsp,
				    "Failed to send 0x%x event to alg 0x%x (%d)\n",
				    event, ctl->alg_region.alg, ret);
	}
}

static void cs_dsp_free_ctl_blk(struct cs_dsp_coeff_ctl *ctl)
{
	kfree(ctl->cache);
	kfree(ctl->subname);
	kfree(ctl);
}

static int cs_dsp_create_control(struct cs_dsp *dsp,
				 const struct cs_dsp_alg_region *alg_region,
				 unsigned int offset, unsigned int len,
				 const char *subname, unsigned int subname_len,
				 unsigned int flags, unsigned int type)
{
	struct cs_dsp_coeff_ctl *ctl;
	int ret;

	list_for_each_entry(ctl, &dsp->ctl_list, list) {
		if (ctl->fw_name == dsp->fw_name &&
		    ctl->alg_region.alg == alg_region->alg &&
		    ctl->alg_region.type == alg_region->type) {
			if ((!subname && !ctl->subname) ||
			    (subname && (ctl->subname_len == subname_len) &&
			     !strncmp(ctl->subname, subname, ctl->subname_len))) {
				if (!ctl->enabled)
					ctl->enabled = 1;
				return 0;
			}
		}
	}

	ctl = kzalloc(sizeof(*ctl), GFP_KERNEL);
	if (!ctl)
		return -ENOMEM;

	ctl->fw_name = dsp->fw_name;
	ctl->alg_region = *alg_region;
	if (subname && dsp->fw_ver >= 2) {
		ctl->subname_len = subname_len;
		ctl->subname = kasprintf(GFP_KERNEL, "%.*s", subname_len, subname);
		if (!ctl->subname) {
			ret = -ENOMEM;
			goto err_ctl;
		}
	}
	ctl->enabled = 1;
	ctl->set = 0;
	ctl->dsp = dsp;

	ctl->flags = flags;
	ctl->type = type;
	ctl->offset = offset;
	ctl->len = len;
	ctl->cache = kzalloc(ctl->len, GFP_KERNEL);
	if (!ctl->cache) {
		ret = -ENOMEM;
		goto err_ctl_subname;
	}

	list_add(&ctl->list, &dsp->ctl_list);

	if (dsp->client_ops->control_add) {
		ret = dsp->client_ops->control_add(ctl);
		if (ret)
			goto err_list_del;
	}

	return 0;

err_list_del:
	list_del(&ctl->list);
	kfree(ctl->cache);
err_ctl_subname:
	kfree(ctl->subname);
err_ctl:
	kfree(ctl);

	return ret;
}

struct cs_dsp_coeff_parsed_alg {
	int id;
	const u8 *name;
	int name_len;
	int ncoeff;
};

struct cs_dsp_coeff_parsed_coeff {
	int offset;
	int mem_type;
	const u8 *name;
	int name_len;
	unsigned int ctl_type;
	int flags;
	int len;
};

static int cs_dsp_coeff_parse_string(int bytes, const u8 **pos, const u8 **str)
{
	int length;

	switch (bytes) {
	case 1:
		length = **pos;
		break;
	case 2:
		length = le16_to_cpu(*((__le16 *)*pos));
		break;
	default:
		return 0;
	}

	if (str)
		*str = *pos + bytes;

	*pos += ((length + bytes) + 3) & ~0x03;

	return length;
}

static int cs_dsp_coeff_parse_int(int bytes, const u8 **pos)
{
	int val = 0;

	switch (bytes) {
	case 2:
		val = le16_to_cpu(*((__le16 *)*pos));
		break;
	case 4:
		val = le32_to_cpu(*((__le32 *)*pos));
		break;
	default:
		break;
	}

	*pos += bytes;

	return val;
}

static inline void cs_dsp_coeff_parse_alg(struct cs_dsp *dsp, const u8 **data,
					  struct cs_dsp_coeff_parsed_alg *blk)
{
	const struct wmfw_adsp_alg_data *raw;

	switch (dsp->fw_ver) {
	case 0:
	case 1:
		raw = (const struct wmfw_adsp_alg_data *)*data;
		*data = raw->data;

		blk->id = le32_to_cpu(raw->id);
		blk->name = raw->name;
		blk->name_len = strlen(raw->name);
		blk->ncoeff = le32_to_cpu(raw->ncoeff);
		break;
	default:
		blk->id = cs_dsp_coeff_parse_int(sizeof(raw->id), data);
		blk->name_len = cs_dsp_coeff_parse_string(sizeof(u8), data,
							  &blk->name);
		cs_dsp_coeff_parse_string(sizeof(u16), data, NULL);
		blk->ncoeff = cs_dsp_coeff_parse_int(sizeof(raw->ncoeff), data);
		break;
	}

	cs_dsp_dbg(dsp, "Algorithm ID: %#x\n", blk->id);
	cs_dsp_dbg(dsp, "Algorithm name: %.*s\n", blk->name_len, blk->name);
	cs_dsp_dbg(dsp, "# of coefficient descriptors: %#x\n", blk->ncoeff);
}

static inline void cs_dsp_coeff_parse_coeff(struct cs_dsp *dsp, const u8 **data,
					    struct cs_dsp_coeff_parsed_coeff *blk)
{
	const struct wmfw_adsp_coeff_data *raw;
	const u8 *tmp;
	int length;

	switch (dsp->fw_ver) {
	case 0:
	case 1:
		raw = (const struct wmfw_adsp_coeff_data *)*data;
		*data = *data + sizeof(raw->hdr) + le32_to_cpu(raw->hdr.size);

		blk->offset = le16_to_cpu(raw->hdr.offset);
		blk->mem_type = le16_to_cpu(raw->hdr.type);
		blk->name = raw->name;
		blk->name_len = strlen(raw->name);
		blk->ctl_type = le16_to_cpu(raw->ctl_type);
		blk->flags = le16_to_cpu(raw->flags);
		blk->len = le32_to_cpu(raw->len);
		break;
	default:
		tmp = *data;
		blk->offset = cs_dsp_coeff_parse_int(sizeof(raw->hdr.offset), &tmp);
		blk->mem_type = cs_dsp_coeff_parse_int(sizeof(raw->hdr.type), &tmp);
		length = cs_dsp_coeff_parse_int(sizeof(raw->hdr.size), &tmp);
		blk->name_len = cs_dsp_coeff_parse_string(sizeof(u8), &tmp,
							  &blk->name);
		cs_dsp_coeff_parse_string(sizeof(u8), &tmp, NULL);
		cs_dsp_coeff_parse_string(sizeof(u16), &tmp, NULL);
		blk->ctl_type = cs_dsp_coeff_parse_int(sizeof(raw->ctl_type), &tmp);
		blk->flags = cs_dsp_coeff_parse_int(sizeof(raw->flags), &tmp);
		blk->len = cs_dsp_coeff_parse_int(sizeof(raw->len), &tmp);

		*data = *data + sizeof(raw->hdr) + length;
		break;
	}

	cs_dsp_dbg(dsp, "\tCoefficient type: %#x\n", blk->mem_type);
	cs_dsp_dbg(dsp, "\tCoefficient offset: %#x\n", blk->offset);
	cs_dsp_dbg(dsp, "\tCoefficient name: %.*s\n", blk->name_len, blk->name);
	cs_dsp_dbg(dsp, "\tCoefficient flags: %#x\n", blk->flags);
	cs_dsp_dbg(dsp, "\tALSA control type: %#x\n", blk->ctl_type);
	cs_dsp_dbg(dsp, "\tALSA control len: %#x\n", blk->len);
}

static int cs_dsp_check_coeff_flags(struct cs_dsp *dsp,
				    const struct cs_dsp_coeff_parsed_coeff *coeff_blk,
				    unsigned int f_required,
				    unsigned int f_illegal)
{
	if ((coeff_blk->flags & f_illegal) ||
	    ((coeff_blk->flags & f_required) != f_required)) {
		cs_dsp_err(dsp, "Illegal flags 0x%x for control type 0x%x\n",
			   coeff_blk->flags, coeff_blk->ctl_type);
		return -EINVAL;
	}

	return 0;
}

static int cs_dsp_parse_coeff(struct cs_dsp *dsp,
			      const struct wmfw_region *region)
{
	struct cs_dsp_alg_region alg_region = {};
	struct cs_dsp_coeff_parsed_alg alg_blk;
	struct cs_dsp_coeff_parsed_coeff coeff_blk;
	const u8 *data = region->data;
	int i, ret;

	cs_dsp_coeff_parse_alg(dsp, &data, &alg_blk);
	for (i = 0; i < alg_blk.ncoeff; i++) {
		cs_dsp_coeff_parse_coeff(dsp, &data, &coeff_blk);

		switch (coeff_blk.ctl_type) {
		case WMFW_CTL_TYPE_BYTES:
			break;
		case WMFW_CTL_TYPE_ACKED:
			if (coeff_blk.flags & WMFW_CTL_FLAG_SYS)
				continue;	/* ignore */

			ret = cs_dsp_check_coeff_flags(dsp, &coeff_blk,
						       WMFW_CTL_FLAG_VOLATILE |
						       WMFW_CTL_FLAG_WRITEABLE |
						       WMFW_CTL_FLAG_READABLE,
						       0);
			if (ret)
				return -EINVAL;
			break;
		case WMFW_CTL_TYPE_HOSTEVENT:
		case WMFW_CTL_TYPE_FWEVENT:
			ret = cs_dsp_check_coeff_flags(dsp, &coeff_blk,
						       WMFW_CTL_FLAG_SYS |
						       WMFW_CTL_FLAG_VOLATILE |
						       WMFW_CTL_FLAG_WRITEABLE |
						       WMFW_CTL_FLAG_READABLE,
						       0);
			if (ret)
				return -EINVAL;
			break;
		case WMFW_CTL_TYPE_HOST_BUFFER:
			ret = cs_dsp_check_coeff_flags(dsp, &coeff_blk,
						       WMFW_CTL_FLAG_SYS |
						       WMFW_CTL_FLAG_VOLATILE |
						       WMFW_CTL_FLAG_READABLE,
						       0);
			if (ret)
				return -EINVAL;
			break;
		default:
			cs_dsp_err(dsp, "Unknown control type: %d\n",
				   coeff_blk.ctl_type);
			return -EINVAL;
		}

		alg_region.type = coeff_blk.mem_type;
		alg_region.alg = alg_blk.id;

		ret = cs_dsp_create_control(dsp, &alg_region,
					    coeff_blk.offset,
					    coeff_blk.len,
					    coeff_blk.name,
					    coeff_blk.name_len,
					    coeff_blk.flags,
					    coeff_blk.ctl_type);
		if (ret < 0)
			cs_dsp_err(dsp, "Failed to create control: %.*s, %d\n",
				   coeff_blk.name_len, coeff_blk.name, ret);
	}

	return 0;
}

static unsigned int cs_dsp_adsp1_parse_sizes(struct cs_dsp *dsp,
					     const char * const file,
					     unsigned int pos,
					     const struct firmware *firmware)
{
	const struct wmfw_adsp1_sizes *adsp1_sizes;

	adsp1_sizes = (void *)&firmware->data[pos];

	cs_dsp_dbg(dsp, "%s: %d DM, %d PM, %d ZM\n", file,
		   le32_to_cpu(adsp1_sizes->dm), le32_to_cpu(adsp1_sizes->pm),
		   le32_to_cpu(adsp1_sizes->zm));

	return pos + sizeof(*adsp1_sizes);
}

static unsigned int cs_dsp_adsp2_parse_sizes(struct cs_dsp *dsp,
					     const char * const file,
					     unsigned int pos,
					     const struct firmware *firmware)
{
	const struct wmfw_adsp2_sizes *adsp2_sizes;

	adsp2_sizes = (void *)&firmware->data[pos];

	cs_dsp_dbg(dsp, "%s: %d XM, %d YM %d PM, %d ZM\n", file,
		   le32_to_cpu(adsp2_sizes->xm), le32_to_cpu(adsp2_sizes->ym),
		   le32_to_cpu(adsp2_sizes->pm), le32_to_cpu(adsp2_sizes->zm));

	return pos + sizeof(*adsp2_sizes);
}

static bool cs_dsp_validate_version(struct cs_dsp *dsp, unsigned int version)
{
	switch (version) {
	case 0:
		cs_dsp_warn(dsp, "Deprecated file format %d\n", version);
		return true;
	case 1:
	case 2:
		return true;
	default:
		return false;
	}
}

static bool cs_dsp_halo_validate_version(struct cs_dsp *dsp, unsigned int version)
{
	switch (version) {
	case 3:
		return true;
	default:
		return false;
	}
}

static int cs_dsp_load(struct cs_dsp *dsp, const struct firmware *firmware,
		       const char *file)
{
	LIST_HEAD(buf_list);
	struct regmap *regmap = dsp->regmap;
	unsigned int pos = 0;
	const struct wmfw_header *header;
	const struct wmfw_adsp1_sizes *adsp1_sizes;
	const struct wmfw_footer *footer;
	const struct wmfw_region *region;
	const struct cs_dsp_region *mem;
	const char *region_name;
	char *text = NULL;
	struct cs_dsp_buf *buf;
	unsigned int reg;
	int regions = 0;
	int ret, offset, type;

	if (!firmware)
		return 0;

	ret = -EINVAL;

	pos = sizeof(*header) + sizeof(*adsp1_sizes) + sizeof(*footer);
	if (pos >= firmware->size) {
		cs_dsp_err(dsp, "%s: file too short, %zu bytes\n",
			   file, firmware->size);
		goto out_fw;
	}

	header = (void *)&firmware->data[0];

	if (memcmp(&header->magic[0], "WMFW", 4) != 0) {
		cs_dsp_err(dsp, "%s: invalid magic\n", file);
		goto out_fw;
	}

	if (!dsp->ops->validate_version(dsp, header->ver)) {
		cs_dsp_err(dsp, "%s: unknown file format %d\n",
			   file, header->ver);
		goto out_fw;
	}

	cs_dsp_info(dsp, "Firmware version: %d\n", header->ver);
	dsp->fw_ver = header->ver;

	if (header->core != dsp->type) {
		cs_dsp_err(dsp, "%s: invalid core %d != %d\n",
			   file, header->core, dsp->type);
		goto out_fw;
	}

	pos = sizeof(*header);
	pos = dsp->ops->parse_sizes(dsp, file, pos, firmware);

	footer = (void *)&firmware->data[pos];
	pos += sizeof(*footer);

	if (le32_to_cpu(header->len) != pos) {
		cs_dsp_err(dsp, "%s: unexpected header length %d\n",
			   file, le32_to_cpu(header->len));
		goto out_fw;
	}

	cs_dsp_dbg(dsp, "%s: timestamp %llu\n", file,
		   le64_to_cpu(footer->timestamp));

	while (pos < firmware->size &&
	       sizeof(*region) < firmware->size - pos) {
		region = (void *)&(firmware->data[pos]);
		region_name = "Unknown";
		reg = 0;
		text = NULL;
		offset = le32_to_cpu(region->offset) & 0xffffff;
		type = be32_to_cpu(region->type) & 0xff;

		switch (type) {
		case WMFW_NAME_TEXT:
			region_name = "Firmware name";
			text = kzalloc(le32_to_cpu(region->len) + 1,
				       GFP_KERNEL);
			break;
		case WMFW_ALGORITHM_DATA:
			region_name = "Algorithm";
			ret = cs_dsp_parse_coeff(dsp, region);
			if (ret != 0)
				goto out_fw;
			break;
		case WMFW_INFO_TEXT:
			region_name = "Information";
			text = kzalloc(le32_to_cpu(region->len) + 1,
				       GFP_KERNEL);
			break;
		case WMFW_ABSOLUTE:
			region_name = "Absolute";
			reg = offset;
			break;
		case WMFW_ADSP1_PM:
		case WMFW_ADSP1_DM:
		case WMFW_ADSP2_XM:
		case WMFW_ADSP2_YM:
		case WMFW_ADSP1_ZM:
		case WMFW_HALO_PM_PACKED:
		case WMFW_HALO_XM_PACKED:
		case WMFW_HALO_YM_PACKED:
			mem = cs_dsp_find_region(dsp, type);
			if (!mem) {
				cs_dsp_err(dsp, "No region of type: %x\n", type);
				ret = -EINVAL;
				goto out_fw;
			}

			region_name = cs_dsp_mem_region_name(type);
			reg = dsp->ops->region_to_reg(mem, offset);
			break;
		default:
			cs_dsp_warn(dsp,
				    "%s.%d: Unknown region type %x at %d(%x)\n",
				    file, regions, type, pos, pos);
			break;
		}

		cs_dsp_dbg(dsp, "%s.%d: %d bytes at %d in %s\n", file,
			   regions, le32_to_cpu(region->len), offset,
			   region_name);

		if (le32_to_cpu(region->len) >
		    firmware->size - pos - sizeof(*region)) {
			cs_dsp_err(dsp,
				   "%s.%d: %s region len %d bytes exceeds file length %zu\n",
				   file, regions, region_name,
				   le32_to_cpu(region->len), firmware->size);
			ret = -EINVAL;
			goto out_fw;
		}

		if (text) {
			memcpy(text, region->data, le32_to_cpu(region->len));
			cs_dsp_info(dsp, "%s: %s\n", file, text);
			kfree(text);
			text = NULL;
		}

		if (reg) {
			buf = cs_dsp_buf_alloc(region->data,
					       le32_to_cpu(region->len),
					       &buf_list);
			if (!buf) {
				cs_dsp_err(dsp, "Out of memory\n");
				ret = -ENOMEM;
				goto out_fw;
			}

			ret = regmap_raw_write_async(regmap, reg, buf->buf,
						     le32_to_cpu(region->len));
			if (ret != 0) {
				cs_dsp_err(dsp,
					   "%s.%d: Failed to write %d bytes at %d in %s: %d\n",
					   file, regions,
					   le32_to_cpu(region->len), offset,
					   region_name, ret);
				goto out_fw;
			}
		}

		pos += le32_to_cpu(region->len) + sizeof(*region);
		regions++;
	}

	ret = regmap_async_complete(regmap);
	if (ret != 0) {
		cs_dsp_err(dsp, "Failed to complete async write: %d\n", ret);
		goto out_fw;
	}

	if (pos > firmware->size)
		cs_dsp_warn(dsp, "%s.%d: %zu bytes at end of file\n",
			    file, regions, pos - firmware->size);

	cs_dsp_debugfs_save_wmfwname(dsp, file);

out_fw:
	regmap_async_complete(regmap);
	cs_dsp_buf_free(&buf_list);
	kfree(text);

	return ret;
}

/**
 * cs_dsp_get_ctl() - Finds a matching coefficient control
 * @dsp: pointer to DSP structure
 * @name: pointer to string to match with a control's subname
 * @type: the algorithm type to match
 * @alg: the algorithm id to match
 *
 * Find cs_dsp_coeff_ctl with input name as its subname
 *
 * Return: pointer to the control on success, NULL if not found
 */
struct cs_dsp_coeff_ctl *cs_dsp_get_ctl(struct cs_dsp *dsp, const char *name, int type,
					unsigned int alg)
{
	struct cs_dsp_coeff_ctl *pos, *rslt = NULL;

	lockdep_assert_held(&dsp->pwr_lock);

	list_for_each_entry(pos, &dsp->ctl_list, list) {
		if (!pos->subname)
			continue;
		if (strncmp(pos->subname, name, pos->subname_len) == 0 &&
		    pos->fw_name == dsp->fw_name &&
		    pos->alg_region.alg == alg &&
		    pos->alg_region.type == type) {
			rslt = pos;
			break;
		}
	}

	return rslt;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_get_ctl, FW_CS_DSP);

static void cs_dsp_ctl_fixup_base(struct cs_dsp *dsp,
				  const struct cs_dsp_alg_region *alg_region)
{
	struct cs_dsp_coeff_ctl *ctl;

	list_for_each_entry(ctl, &dsp->ctl_list, list) {
		if (ctl->fw_name == dsp->fw_name &&
		    alg_region->alg == ctl->alg_region.alg &&
		    alg_region->type == ctl->alg_region.type) {
			ctl->alg_region.base = alg_region->base;
		}
	}
}

static void *cs_dsp_read_algs(struct cs_dsp *dsp, size_t n_algs,
			      const struct cs_dsp_region *mem,
			      unsigned int pos, unsigned int len)
{
	void *alg;
	unsigned int reg;
	int ret;
	__be32 val;

	if (n_algs == 0) {
		cs_dsp_err(dsp, "No algorithms\n");
		return ERR_PTR(-EINVAL);
	}

	if (n_algs > 1024) {
		cs_dsp_err(dsp, "Algorithm count %zx excessive\n", n_algs);
		return ERR_PTR(-EINVAL);
	}

	/* Read the terminator first to validate the length */
	reg = dsp->ops->region_to_reg(mem, pos + len);

	ret = regmap_raw_read(dsp->regmap, reg, &val, sizeof(val));
	if (ret != 0) {
		cs_dsp_err(dsp, "Failed to read algorithm list end: %d\n",
			   ret);
		return ERR_PTR(ret);
	}

	if (be32_to_cpu(val) != 0xbedead)
		cs_dsp_warn(dsp, "Algorithm list end %x 0x%x != 0xbedead\n",
			    reg, be32_to_cpu(val));

	/* Convert length from DSP words to bytes */
	len *= sizeof(u32);

	alg = kzalloc(len, GFP_KERNEL | GFP_DMA);
	if (!alg)
		return ERR_PTR(-ENOMEM);

	reg = dsp->ops->region_to_reg(mem, pos);

	ret = regmap_raw_read(dsp->regmap, reg, alg, len);
	if (ret != 0) {
		cs_dsp_err(dsp, "Failed to read algorithm list: %d\n", ret);
		kfree(alg);
		return ERR_PTR(ret);
	}

	return alg;
}

/**
 * cs_dsp_find_alg_region() - Finds a matching algorithm region
 * @dsp: pointer to DSP structure
 * @type: the algorithm type to match
 * @id: the algorithm id to match
 *
 * Return: Pointer to matching algorithm region, or NULL if not found.
 */
struct cs_dsp_alg_region *cs_dsp_find_alg_region(struct cs_dsp *dsp,
						 int type, unsigned int id)
{
	struct cs_dsp_alg_region *alg_region;

	lockdep_assert_held(&dsp->pwr_lock);

	list_for_each_entry(alg_region, &dsp->alg_regions, list) {
		if (id == alg_region->alg && type == alg_region->type)
			return alg_region;
	}

	return NULL;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_find_alg_region, FW_CS_DSP);

static struct cs_dsp_alg_region *cs_dsp_create_region(struct cs_dsp *dsp,
						      int type, __be32 id,
						      __be32 ver, __be32 base)
{
	struct cs_dsp_alg_region *alg_region;

	alg_region = kzalloc(sizeof(*alg_region), GFP_KERNEL);
	if (!alg_region)
		return ERR_PTR(-ENOMEM);

	alg_region->type = type;
	alg_region->alg = be32_to_cpu(id);
	alg_region->ver = be32_to_cpu(ver);
	alg_region->base = be32_to_cpu(base);

	list_add_tail(&alg_region->list, &dsp->alg_regions);

	if (dsp->fw_ver > 0)
		cs_dsp_ctl_fixup_base(dsp, alg_region);

	return alg_region;
}

static void cs_dsp_free_alg_regions(struct cs_dsp *dsp)
{
	struct cs_dsp_alg_region *alg_region;

	while (!list_empty(&dsp->alg_regions)) {
		alg_region = list_first_entry(&dsp->alg_regions,
					      struct cs_dsp_alg_region,
					      list);
		list_del(&alg_region->list);
		kfree(alg_region);
	}
}

static void cs_dsp_parse_wmfw_id_header(struct cs_dsp *dsp,
					struct wmfw_id_hdr *fw, int nalgs)
{
	dsp->fw_id = be32_to_cpu(fw->id);
	dsp->fw_id_version = be32_to_cpu(fw->ver);

	cs_dsp_info(dsp, "Firmware: %x v%d.%d.%d, %d algorithms\n",
		    dsp->fw_id, (dsp->fw_id_version & 0xff0000) >> 16,
		    (dsp->fw_id_version & 0xff00) >> 8, dsp->fw_id_version & 0xff,
		    nalgs);
}

static void cs_dsp_parse_wmfw_v3_id_header(struct cs_dsp *dsp,
					   struct wmfw_v3_id_hdr *fw, int nalgs)
{
	dsp->fw_id = be32_to_cpu(fw->id);
	dsp->fw_id_version = be32_to_cpu(fw->ver);
	dsp->fw_vendor_id = be32_to_cpu(fw->vendor_id);

	cs_dsp_info(dsp, "Firmware: %x vendor: 0x%x v%d.%d.%d, %d algorithms\n",
		    dsp->fw_id, dsp->fw_vendor_id,
		    (dsp->fw_id_version & 0xff0000) >> 16,
		    (dsp->fw_id_version & 0xff00) >> 8, dsp->fw_id_version & 0xff,
		    nalgs);
}

static int cs_dsp_create_regions(struct cs_dsp *dsp, __be32 id, __be32 ver,
				 int nregions, const int *type, __be32 *base)
{
	struct cs_dsp_alg_region *alg_region;
	int i;

	for (i = 0; i < nregions; i++) {
		alg_region = cs_dsp_create_region(dsp, type[i], id, ver, base[i]);
		if (IS_ERR(alg_region))
			return PTR_ERR(alg_region);
	}

	return 0;
}

static int cs_dsp_adsp1_setup_algs(struct cs_dsp *dsp)
{
	struct wmfw_adsp1_id_hdr adsp1_id;
	struct wmfw_adsp1_alg_hdr *adsp1_alg;
	struct cs_dsp_alg_region *alg_region;
	const struct cs_dsp_region *mem;
	unsigned int pos, len;
	size_t n_algs;
	int i, ret;

	mem = cs_dsp_find_region(dsp, WMFW_ADSP1_DM);
	if (WARN_ON(!mem))
		return -EINVAL;

	ret = regmap_raw_read(dsp->regmap, mem->base, &adsp1_id,
			      sizeof(adsp1_id));
	if (ret != 0) {
		cs_dsp_err(dsp, "Failed to read algorithm info: %d\n",
			   ret);
		return ret;
	}

	n_algs = be32_to_cpu(adsp1_id.n_algs);

	cs_dsp_parse_wmfw_id_header(dsp, &adsp1_id.fw, n_algs);

	alg_region = cs_dsp_create_region(dsp, WMFW_ADSP1_ZM,
					  adsp1_id.fw.id, adsp1_id.fw.ver,
					  adsp1_id.zm);
	if (IS_ERR(alg_region))
		return PTR_ERR(alg_region);

	alg_region = cs_dsp_create_region(dsp, WMFW_ADSP1_DM,
					  adsp1_id.fw.id, adsp1_id.fw.ver,
					  adsp1_id.dm);
	if (IS_ERR(alg_region))
		return PTR_ERR(alg_region);

	/* Calculate offset and length in DSP words */
	pos = sizeof(adsp1_id) / sizeof(u32);
	len = (sizeof(*adsp1_alg) * n_algs) / sizeof(u32);

	adsp1_alg = cs_dsp_read_algs(dsp, n_algs, mem, pos, len);
	if (IS_ERR(adsp1_alg))
		return PTR_ERR(adsp1_alg);

	for (i = 0; i < n_algs; i++) {
		cs_dsp_info(dsp, "%d: ID %x v%d.%d.%d DM@%x ZM@%x\n",
			    i, be32_to_cpu(adsp1_alg[i].alg.id),
			    (be32_to_cpu(adsp1_alg[i].alg.ver) & 0xff0000) >> 16,
			    (be32_to_cpu(adsp1_alg[i].alg.ver) & 0xff00) >> 8,
			    be32_to_cpu(adsp1_alg[i].alg.ver) & 0xff,
			    be32_to_cpu(adsp1_alg[i].dm),
			    be32_to_cpu(adsp1_alg[i].zm));

		alg_region = cs_dsp_create_region(dsp, WMFW_ADSP1_DM,
						  adsp1_alg[i].alg.id,
						  adsp1_alg[i].alg.ver,
						  adsp1_alg[i].dm);
		if (IS_ERR(alg_region)) {
			ret = PTR_ERR(alg_region);
			goto out;
		}
		if (dsp->fw_ver == 0) {
			if (i + 1 < n_algs) {
				len = be32_to_cpu(adsp1_alg[i + 1].dm);
				len -= be32_to_cpu(adsp1_alg[i].dm);
				len *= 4;
				cs_dsp_create_control(dsp, alg_region, 0,
						      len, NULL, 0, 0,
						      WMFW_CTL_TYPE_BYTES);
			} else {
				cs_dsp_warn(dsp, "Missing length info for region DM with ID %x\n",
					    be32_to_cpu(adsp1_alg[i].alg.id));
			}
		}

		alg_region = cs_dsp_create_region(dsp, WMFW_ADSP1_ZM,
						  adsp1_alg[i].alg.id,
						  adsp1_alg[i].alg.ver,
						  adsp1_alg[i].zm);
		if (IS_ERR(alg_region)) {
			ret = PTR_ERR(alg_region);
			goto out;
		}
		if (dsp->fw_ver == 0) {
			if (i + 1 < n_algs) {
				len = be32_to_cpu(adsp1_alg[i + 1].zm);
				len -= be32_to_cpu(adsp1_alg[i].zm);
				len *= 4;
				cs_dsp_create_control(dsp, alg_region, 0,
						      len, NULL, 0, 0,
						      WMFW_CTL_TYPE_BYTES);
			} else {
				cs_dsp_warn(dsp, "Missing length info for region ZM with ID %x\n",
					    be32_to_cpu(adsp1_alg[i].alg.id));
			}
		}
	}

out:
	kfree(adsp1_alg);
	return ret;
}

static int cs_dsp_adsp2_setup_algs(struct cs_dsp *dsp)
{
	struct wmfw_adsp2_id_hdr adsp2_id;
	struct wmfw_adsp2_alg_hdr *adsp2_alg;
	struct cs_dsp_alg_region *alg_region;
	const struct cs_dsp_region *mem;
	unsigned int pos, len;
	size_t n_algs;
	int i, ret;

	mem = cs_dsp_find_region(dsp, WMFW_ADSP2_XM);
	if (WARN_ON(!mem))
		return -EINVAL;

	ret = regmap_raw_read(dsp->regmap, mem->base, &adsp2_id,
			      sizeof(adsp2_id));
	if (ret != 0) {
		cs_dsp_err(dsp, "Failed to read algorithm info: %d\n",
			   ret);
		return ret;
	}

	n_algs = be32_to_cpu(adsp2_id.n_algs);

	cs_dsp_parse_wmfw_id_header(dsp, &adsp2_id.fw, n_algs);

	alg_region = cs_dsp_create_region(dsp, WMFW_ADSP2_XM,
					  adsp2_id.fw.id, adsp2_id.fw.ver,
					  adsp2_id.xm);
	if (IS_ERR(alg_region))
		return PTR_ERR(alg_region);

	alg_region = cs_dsp_create_region(dsp, WMFW_ADSP2_YM,
					  adsp2_id.fw.id, adsp2_id.fw.ver,
					  adsp2_id.ym);
	if (IS_ERR(alg_region))
		return PTR_ERR(alg_region);

	alg_region = cs_dsp_create_region(dsp, WMFW_ADSP2_ZM,
					  adsp2_id.fw.id, adsp2_id.fw.ver,
					  adsp2_id.zm);
	if (IS_ERR(alg_region))
		return PTR_ERR(alg_region);

	/* Calculate offset and length in DSP words */
	pos = sizeof(adsp2_id) / sizeof(u32);
	len = (sizeof(*adsp2_alg) * n_algs) / sizeof(u32);

	adsp2_alg = cs_dsp_read_algs(dsp, n_algs, mem, pos, len);
	if (IS_ERR(adsp2_alg))
		return PTR_ERR(adsp2_alg);

	for (i = 0; i < n_algs; i++) {
		cs_dsp_dbg(dsp,
			   "%d: ID %x v%d.%d.%d XM@%x YM@%x ZM@%x\n",
			   i, be32_to_cpu(adsp2_alg[i].alg.id),
			   (be32_to_cpu(adsp2_alg[i].alg.ver) & 0xff0000) >> 16,
			   (be32_to_cpu(adsp2_alg[i].alg.ver) & 0xff00) >> 8,
			   be32_to_cpu(adsp2_alg[i].alg.ver) & 0xff,
			   be32_to_cpu(adsp2_alg[i].xm),
			   be32_to_cpu(adsp2_alg[i].ym),
			   be32_to_cpu(adsp2_alg[i].zm));

		alg_region = cs_dsp_create_region(dsp, WMFW_ADSP2_XM,
						  adsp2_alg[i].alg.id,
						  adsp2_alg[i].alg.ver,
						  adsp2_alg[i].xm);
		if (IS_ERR(alg_region)) {
			ret = PTR_ERR(alg_region);
			goto out;
		}
		if (dsp->fw_ver == 0) {
			if (i + 1 < n_algs) {
				len = be32_to_cpu(adsp2_alg[i + 1].xm);
				len -= be32_to_cpu(adsp2_alg[i].xm);
				len *= 4;
				cs_dsp_create_control(dsp, alg_region, 0,
						      len, NULL, 0, 0,
						      WMFW_CTL_TYPE_BYTES);
			} else {
				cs_dsp_warn(dsp, "Missing length info for region XM with ID %x\n",
					    be32_to_cpu(adsp2_alg[i].alg.id));
			}
		}

		alg_region = cs_dsp_create_region(dsp, WMFW_ADSP2_YM,
						  adsp2_alg[i].alg.id,
						  adsp2_alg[i].alg.ver,
						  adsp2_alg[i].ym);
		if (IS_ERR(alg_region)) {
			ret = PTR_ERR(alg_region);
			goto out;
		}
		if (dsp->fw_ver == 0) {
			if (i + 1 < n_algs) {
				len = be32_to_cpu(adsp2_alg[i + 1].ym);
				len -= be32_to_cpu(adsp2_alg[i].ym);
				len *= 4;
				cs_dsp_create_control(dsp, alg_region, 0,
						      len, NULL, 0, 0,
						      WMFW_CTL_TYPE_BYTES);
			} else {
				cs_dsp_warn(dsp, "Missing length info for region YM with ID %x\n",
					    be32_to_cpu(adsp2_alg[i].alg.id));
			}
		}

		alg_region = cs_dsp_create_region(dsp, WMFW_ADSP2_ZM,
						  adsp2_alg[i].alg.id,
						  adsp2_alg[i].alg.ver,
						  adsp2_alg[i].zm);
		if (IS_ERR(alg_region)) {
			ret = PTR_ERR(alg_region);
			goto out;
		}
		if (dsp->fw_ver == 0) {
			if (i + 1 < n_algs) {
				len = be32_to_cpu(adsp2_alg[i + 1].zm);
				len -= be32_to_cpu(adsp2_alg[i].zm);
				len *= 4;
				cs_dsp_create_control(dsp, alg_region, 0,
						      len, NULL, 0, 0,
						      WMFW_CTL_TYPE_BYTES);
			} else {
				cs_dsp_warn(dsp, "Missing length info for region ZM with ID %x\n",
					    be32_to_cpu(adsp2_alg[i].alg.id));
			}
		}
	}

out:
	kfree(adsp2_alg);
	return ret;
}

static int cs_dsp_halo_create_regions(struct cs_dsp *dsp, __be32 id, __be32 ver,
				      __be32 xm_base, __be32 ym_base)
{
	static const int types[] = {
		WMFW_ADSP2_XM, WMFW_HALO_XM_PACKED,
		WMFW_ADSP2_YM, WMFW_HALO_YM_PACKED
	};
	__be32 bases[] = { xm_base, xm_base, ym_base, ym_base };

	return cs_dsp_create_regions(dsp, id, ver, ARRAY_SIZE(types), types, bases);
}

static int cs_dsp_halo_setup_algs(struct cs_dsp *dsp)
{
	struct wmfw_halo_id_hdr halo_id;
	struct wmfw_halo_alg_hdr *halo_alg;
	const struct cs_dsp_region *mem;
	unsigned int pos, len;
	size_t n_algs;
	int i, ret;

	mem = cs_dsp_find_region(dsp, WMFW_ADSP2_XM);
	if (WARN_ON(!mem))
		return -EINVAL;

	ret = regmap_raw_read(dsp->regmap, mem->base, &halo_id,
			      sizeof(halo_id));
	if (ret != 0) {
		cs_dsp_err(dsp, "Failed to read algorithm info: %d\n",
			   ret);
		return ret;
	}

	n_algs = be32_to_cpu(halo_id.n_algs);

	cs_dsp_parse_wmfw_v3_id_header(dsp, &halo_id.fw, n_algs);

	ret = cs_dsp_halo_create_regions(dsp, halo_id.fw.id, halo_id.fw.ver,
					 halo_id.xm_base, halo_id.ym_base);
	if (ret)
		return ret;

	/* Calculate offset and length in DSP words */
	pos = sizeof(halo_id) / sizeof(u32);
	len = (sizeof(*halo_alg) * n_algs) / sizeof(u32);

	halo_alg = cs_dsp_read_algs(dsp, n_algs, mem, pos, len);
	if (IS_ERR(halo_alg))
		return PTR_ERR(halo_alg);

	for (i = 0; i < n_algs; i++) {
		cs_dsp_dbg(dsp,
			   "%d: ID %x v%d.%d.%d XM@%x YM@%x\n",
			   i, be32_to_cpu(halo_alg[i].alg.id),
			   (be32_to_cpu(halo_alg[i].alg.ver) & 0xff0000) >> 16,
			   (be32_to_cpu(halo_alg[i].alg.ver) & 0xff00) >> 8,
			   be32_to_cpu(halo_alg[i].alg.ver) & 0xff,
			   be32_to_cpu(halo_alg[i].xm_base),
			   be32_to_cpu(halo_alg[i].ym_base));

		ret = cs_dsp_halo_create_regions(dsp, halo_alg[i].alg.id,
						 halo_alg[i].alg.ver,
						 halo_alg[i].xm_base,
						 halo_alg[i].ym_base);
		if (ret)
			goto out;
	}

out:
	kfree(halo_alg);
	return ret;
}

static int cs_dsp_load_coeff(struct cs_dsp *dsp, const struct firmware *firmware,
			     const char *file)
{
	LIST_HEAD(buf_list);
	struct regmap *regmap = dsp->regmap;
	struct wmfw_coeff_hdr *hdr;
	struct wmfw_coeff_item *blk;
	const struct cs_dsp_region *mem;
	struct cs_dsp_alg_region *alg_region;
	const char *region_name;
	int ret, pos, blocks, type, offset, reg, version;
	char *text = NULL;
	struct cs_dsp_buf *buf;

	if (!firmware)
		return 0;

	ret = -EINVAL;

	if (sizeof(*hdr) >= firmware->size) {
		cs_dsp_err(dsp, "%s: coefficient file too short, %zu bytes\n",
			   file, firmware->size);
		goto out_fw;
	}

	hdr = (void *)&firmware->data[0];
	if (memcmp(hdr->magic, "WMDR", 4) != 0) {
		cs_dsp_err(dsp, "%s: invalid coefficient magic\n", file);
		goto out_fw;
	}

	switch (be32_to_cpu(hdr->rev) & 0xff) {
	case 1:
	case 2:
		break;
	default:
		cs_dsp_err(dsp, "%s: Unsupported coefficient file format %d\n",
			   file, be32_to_cpu(hdr->rev) & 0xff);
		ret = -EINVAL;
		goto out_fw;
	}

	cs_dsp_info(dsp, "%s: v%d.%d.%d\n", file,
		    (le32_to_cpu(hdr->ver) >> 16) & 0xff,
		    (le32_to_cpu(hdr->ver) >>  8) & 0xff,
		    le32_to_cpu(hdr->ver) & 0xff);

	pos = le32_to_cpu(hdr->len);

	blocks = 0;
	while (pos < firmware->size &&
	       sizeof(*blk) < firmware->size - pos) {
		blk = (void *)(&firmware->data[pos]);

		type = le16_to_cpu(blk->type);
		offset = le16_to_cpu(blk->offset);
		version = le32_to_cpu(blk->ver) >> 8;

		cs_dsp_dbg(dsp, "%s.%d: %x v%d.%d.%d\n",
			   file, blocks, le32_to_cpu(blk->id),
			   (le32_to_cpu(blk->ver) >> 16) & 0xff,
			   (le32_to_cpu(blk->ver) >>  8) & 0xff,
			   le32_to_cpu(blk->ver) & 0xff);
		cs_dsp_dbg(dsp, "%s.%d: %d bytes at 0x%x in %x\n",
			   file, blocks, le32_to_cpu(blk->len), offset, type);

		reg = 0;
		region_name = "Unknown";
		switch (type) {
		case (WMFW_NAME_TEXT << 8):
			text = kzalloc(le32_to_cpu(blk->len) + 1, GFP_KERNEL);
			break;
		case (WMFW_INFO_TEXT << 8):
		case (WMFW_METADATA << 8):
			break;
		case (WMFW_ABSOLUTE << 8):
			/*
			 * Old files may use this for global
			 * coefficients.
			 */
			if (le32_to_cpu(blk->id) == dsp->fw_id &&
			    offset == 0) {
				region_name = "global coefficients";
				mem = cs_dsp_find_region(dsp, type);
				if (!mem) {
					cs_dsp_err(dsp, "No ZM\n");
					break;
				}
				reg = dsp->ops->region_to_reg(mem, 0);

			} else {
				region_name = "register";
				reg = offset;
			}
			break;

		case WMFW_ADSP1_DM:
		case WMFW_ADSP1_ZM:
		case WMFW_ADSP2_XM:
		case WMFW_ADSP2_YM:
		case WMFW_HALO_XM_PACKED:
		case WMFW_HALO_YM_PACKED:
		case WMFW_HALO_PM_PACKED:
			cs_dsp_dbg(dsp, "%s.%d: %d bytes in %x for %x\n",
				   file, blocks, le32_to_cpu(blk->len),
				   type, le32_to_cpu(blk->id));

			region_name = cs_dsp_mem_region_name(type);
			mem = cs_dsp_find_region(dsp, type);
			if (!mem) {
				cs_dsp_err(dsp, "No base for region %x\n", type);
				break;
			}

			alg_region = cs_dsp_find_alg_region(dsp, type,
							    le32_to_cpu(blk->id));
			if (alg_region) {
				if (version != alg_region->ver)
					cs_dsp_warn(dsp,
						    "Algorithm coefficient version %d.%d.%d but expected %d.%d.%d\n",
						   (version >> 16) & 0xFF,
						   (version >> 8) & 0xFF,
						   version & 0xFF,
						   (alg_region->ver >> 16) & 0xFF,
						   (alg_region->ver >> 8) & 0xFF,
						   alg_region->ver & 0xFF);

				reg = alg_region->base;
				reg = dsp->ops->region_to_reg(mem, reg);
				reg += offset;
			} else {
				cs_dsp_err(dsp, "No %s for algorithm %x\n",
					   region_name, le32_to_cpu(blk->id));
			}
			break;

		default:
			cs_dsp_err(dsp, "%s.%d: Unknown region type %x at %d\n",
				   file, blocks, type, pos);
			break;
		}

		if (text) {
			memcpy(text, blk->data, le32_to_cpu(blk->len));
			cs_dsp_info(dsp, "%s: %s\n", dsp->fw_name, text);
			kfree(text);
			text = NULL;
		}

		if (reg) {
			if (le32_to_cpu(blk->len) >
			    firmware->size - pos - sizeof(*blk)) {
				cs_dsp_err(dsp,
					   "%s.%d: %s region len %d bytes exceeds file length %zu\n",
					   file, blocks, region_name,
					   le32_to_cpu(blk->len),
					   firmware->size);
				ret = -EINVAL;
				goto out_fw;
			}

			buf = cs_dsp_buf_alloc(blk->data,
					       le32_to_cpu(blk->len),
					       &buf_list);
			if (!buf) {
				cs_dsp_err(dsp, "Out of memory\n");
				ret = -ENOMEM;
				goto out_fw;
			}

			cs_dsp_dbg(dsp, "%s.%d: Writing %d bytes at %x\n",
				   file, blocks, le32_to_cpu(blk->len),
				   reg);
			ret = regmap_raw_write_async(regmap, reg, buf->buf,
						     le32_to_cpu(blk->len));
			if (ret != 0) {
				cs_dsp_err(dsp,
					   "%s.%d: Failed to write to %x in %s: %d\n",
					   file, blocks, reg, region_name, ret);
			}
		}

		pos += (le32_to_cpu(blk->len) + sizeof(*blk) + 3) & ~0x03;
		blocks++;
	}

	ret = regmap_async_complete(regmap);
	if (ret != 0)
		cs_dsp_err(dsp, "Failed to complete async write: %d\n", ret);

	if (pos > firmware->size)
		cs_dsp_warn(dsp, "%s.%d: %zu bytes at end of file\n",
			    file, blocks, pos - firmware->size);

	cs_dsp_debugfs_save_binname(dsp, file);

out_fw:
	regmap_async_complete(regmap);
	cs_dsp_buf_free(&buf_list);
	kfree(text);
	return ret;
}

static int cs_dsp_create_name(struct cs_dsp *dsp)
{
	if (!dsp->name) {
		dsp->name = devm_kasprintf(dsp->dev, GFP_KERNEL, "DSP%d",
					   dsp->num);
		if (!dsp->name)
			return -ENOMEM;
	}

	return 0;
}

static int cs_dsp_common_init(struct cs_dsp *dsp)
{
	int ret;

	ret = cs_dsp_create_name(dsp);
	if (ret)
		return ret;

	INIT_LIST_HEAD(&dsp->alg_regions);
	INIT_LIST_HEAD(&dsp->ctl_list);

	mutex_init(&dsp->pwr_lock);

#ifdef CONFIG_DEBUG_FS
	/* Ensure this is invalid if client never provides a debugfs root */
	dsp->debugfs_root = ERR_PTR(-ENODEV);
#endif

	return 0;
}

/**
 * cs_dsp_adsp1_init() - Initialise a cs_dsp structure representing a ADSP1 device
 * @dsp: pointer to DSP structure
 *
 * Return: Zero for success, a negative number on error.
 */
int cs_dsp_adsp1_init(struct cs_dsp *dsp)
{
	dsp->ops = &cs_dsp_adsp1_ops;

	return cs_dsp_common_init(dsp);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_adsp1_init, FW_CS_DSP);

/**
 * cs_dsp_adsp1_power_up() - Load and start the named firmware
 * @dsp: pointer to DSP structure
 * @wmfw_firmware: the firmware to be sent
 * @wmfw_filename: file name of firmware to be sent
 * @coeff_firmware: the coefficient data to be sent
 * @coeff_filename: file name of coefficient to data be sent
 * @fw_name: the user-friendly firmware name
 *
 * Return: Zero for success, a negative number on error.
 */
int cs_dsp_adsp1_power_up(struct cs_dsp *dsp,
			  const struct firmware *wmfw_firmware, char *wmfw_filename,
			  const struct firmware *coeff_firmware, char *coeff_filename,
			  const char *fw_name)
{
	unsigned int val;
	int ret;

	mutex_lock(&dsp->pwr_lock);

	dsp->fw_name = fw_name;

	regmap_update_bits(dsp->regmap, dsp->base + ADSP1_CONTROL_30,
			   ADSP1_SYS_ENA, ADSP1_SYS_ENA);

	/*
	 * For simplicity set the DSP clock rate to be the
	 * SYSCLK rate rather than making it configurable.
	 */
	if (dsp->sysclk_reg) {
		ret = regmap_read(dsp->regmap, dsp->sysclk_reg, &val);
		if (ret != 0) {
			cs_dsp_err(dsp, "Failed to read SYSCLK state: %d\n", ret);
			goto err_mutex;
		}

		val = (val & dsp->sysclk_mask) >> dsp->sysclk_shift;

		ret = regmap_update_bits(dsp->regmap,
					 dsp->base + ADSP1_CONTROL_31,
					 ADSP1_CLK_SEL_MASK, val);
		if (ret != 0) {
			cs_dsp_err(dsp, "Failed to set clock rate: %d\n", ret);
			goto err_mutex;
		}
	}

	ret = cs_dsp_load(dsp, wmfw_firmware, wmfw_filename);
	if (ret != 0)
		goto err_ena;

	ret = cs_dsp_adsp1_setup_algs(dsp);
	if (ret != 0)
		goto err_ena;

	ret = cs_dsp_load_coeff(dsp, coeff_firmware, coeff_filename);
	if (ret != 0)
		goto err_ena;

	/* Initialize caches for enabled and unset controls */
	ret = cs_dsp_coeff_init_control_caches(dsp);
	if (ret != 0)
		goto err_ena;

	/* Sync set controls */
	ret = cs_dsp_coeff_sync_controls(dsp);
	if (ret != 0)
		goto err_ena;

	dsp->booted = true;

	/* Start the core running */
	regmap_update_bits(dsp->regmap, dsp->base + ADSP1_CONTROL_30,
			   ADSP1_CORE_ENA | ADSP1_START,
			   ADSP1_CORE_ENA | ADSP1_START);

	dsp->running = true;

	mutex_unlock(&dsp->pwr_lock);

	return 0;

err_ena:
	regmap_update_bits(dsp->regmap, dsp->base + ADSP1_CONTROL_30,
			   ADSP1_SYS_ENA, 0);
err_mutex:
	mutex_unlock(&dsp->pwr_lock);
	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_adsp1_power_up, FW_CS_DSP);

/**
 * cs_dsp_adsp1_power_down() - Halts the DSP
 * @dsp: pointer to DSP structure
 */
void cs_dsp_adsp1_power_down(struct cs_dsp *dsp)
{
	struct cs_dsp_coeff_ctl *ctl;

	mutex_lock(&dsp->pwr_lock);

	dsp->running = false;
	dsp->booted = false;

	/* Halt the core */
	regmap_update_bits(dsp->regmap, dsp->base + ADSP1_CONTROL_30,
			   ADSP1_CORE_ENA | ADSP1_START, 0);

	regmap_update_bits(dsp->regmap, dsp->base + ADSP1_CONTROL_19,
			   ADSP1_WDMA_BUFFER_LENGTH_MASK, 0);

	regmap_update_bits(dsp->regmap, dsp->base + ADSP1_CONTROL_30,
			   ADSP1_SYS_ENA, 0);

	list_for_each_entry(ctl, &dsp->ctl_list, list)
		ctl->enabled = 0;

	cs_dsp_free_alg_regions(dsp);

	mutex_unlock(&dsp->pwr_lock);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_adsp1_power_down, FW_CS_DSP);

static int cs_dsp_adsp2v2_enable_core(struct cs_dsp *dsp)
{
	unsigned int val;
	int ret, count;

	/* Wait for the RAM to start, should be near instantaneous */
	for (count = 0; count < 10; ++count) {
		ret = regmap_read(dsp->regmap, dsp->base + ADSP2_STATUS1, &val);
		if (ret != 0)
			return ret;

		if (val & ADSP2_RAM_RDY)
			break;

		usleep_range(250, 500);
	}

	if (!(val & ADSP2_RAM_RDY)) {
		cs_dsp_err(dsp, "Failed to start DSP RAM\n");
		return -EBUSY;
	}

	cs_dsp_dbg(dsp, "RAM ready after %d polls\n", count);

	return 0;
}

static int cs_dsp_adsp2_enable_core(struct cs_dsp *dsp)
{
	int ret;

	ret = regmap_update_bits_async(dsp->regmap, dsp->base + ADSP2_CONTROL,
				       ADSP2_SYS_ENA, ADSP2_SYS_ENA);
	if (ret != 0)
		return ret;

	return cs_dsp_adsp2v2_enable_core(dsp);
}

static int cs_dsp_adsp2_lock(struct cs_dsp *dsp, unsigned int lock_regions)
{
	struct regmap *regmap = dsp->regmap;
	unsigned int code0, code1, lock_reg;

	if (!(lock_regions & CS_ADSP2_REGION_ALL))
		return 0;

	lock_regions &= CS_ADSP2_REGION_ALL;
	lock_reg = dsp->base + ADSP2_LOCK_REGION_1_LOCK_REGION_0;

	while (lock_regions) {
		code0 = code1 = 0;
		if (lock_regions & BIT(0)) {
			code0 = ADSP2_LOCK_CODE_0;
			code1 = ADSP2_LOCK_CODE_1;
		}
		if (lock_regions & BIT(1)) {
			code0 |= ADSP2_LOCK_CODE_0 << ADSP2_LOCK_REGION_SHIFT;
			code1 |= ADSP2_LOCK_CODE_1 << ADSP2_LOCK_REGION_SHIFT;
		}
		regmap_write(regmap, lock_reg, code0);
		regmap_write(regmap, lock_reg, code1);
		lock_regions >>= 2;
		lock_reg += 2;
	}

	return 0;
}

static int cs_dsp_adsp2_enable_memory(struct cs_dsp *dsp)
{
	return regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CONTROL,
				  ADSP2_MEM_ENA, ADSP2_MEM_ENA);
}

static void cs_dsp_adsp2_disable_memory(struct cs_dsp *dsp)
{
	regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CONTROL,
			   ADSP2_MEM_ENA, 0);
}

static void cs_dsp_adsp2_disable_core(struct cs_dsp *dsp)
{
	regmap_write(dsp->regmap, dsp->base + ADSP2_RDMA_CONFIG_1, 0);
	regmap_write(dsp->regmap, dsp->base + ADSP2_WDMA_CONFIG_1, 0);
	regmap_write(dsp->regmap, dsp->base + ADSP2_WDMA_CONFIG_2, 0);

	regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CONTROL,
			   ADSP2_SYS_ENA, 0);
}

static void cs_dsp_adsp2v2_disable_core(struct cs_dsp *dsp)
{
	regmap_write(dsp->regmap, dsp->base + ADSP2_RDMA_CONFIG_1, 0);
	regmap_write(dsp->regmap, dsp->base + ADSP2_WDMA_CONFIG_1, 0);
	regmap_write(dsp->regmap, dsp->base + ADSP2V2_WDMA_CONFIG_2, 0);
}

static int cs_dsp_halo_configure_mpu(struct cs_dsp *dsp, unsigned int lock_regions)
{
	struct reg_sequence config[] = {
		{ dsp->base + HALO_MPU_LOCK_CONFIG,     0x5555 },
		{ dsp->base + HALO_MPU_LOCK_CONFIG,     0xAAAA },
		{ dsp->base + HALO_MPU_XMEM_ACCESS_0,   0xFFFFFFFF },
		{ dsp->base + HALO_MPU_YMEM_ACCESS_0,   0xFFFFFFFF },
		{ dsp->base + HALO_MPU_WINDOW_ACCESS_0, lock_regions },
		{ dsp->base + HALO_MPU_XREG_ACCESS_0,   lock_regions },
		{ dsp->base + HALO_MPU_YREG_ACCESS_0,   lock_regions },
		{ dsp->base + HALO_MPU_XMEM_ACCESS_1,   0xFFFFFFFF },
		{ dsp->base + HALO_MPU_YMEM_ACCESS_1,   0xFFFFFFFF },
		{ dsp->base + HALO_MPU_WINDOW_ACCESS_1, lock_regions },
		{ dsp->base + HALO_MPU_XREG_ACCESS_1,   lock_regions },
		{ dsp->base + HALO_MPU_YREG_ACCESS_1,   lock_regions },
		{ dsp->base + HALO_MPU_XMEM_ACCESS_2,   0xFFFFFFFF },
		{ dsp->base + HALO_MPU_YMEM_ACCESS_2,   0xFFFFFFFF },
		{ dsp->base + HALO_MPU_WINDOW_ACCESS_2, lock_regions },
		{ dsp->base + HALO_MPU_XREG_ACCESS_2,   lock_regions },
		{ dsp->base + HALO_MPU_YREG_ACCESS_2,   lock_regions },
		{ dsp->base + HALO_MPU_XMEM_ACCESS_3,   0xFFFFFFFF },
		{ dsp->base + HALO_MPU_YMEM_ACCESS_3,   0xFFFFFFFF },
		{ dsp->base + HALO_MPU_WINDOW_ACCESS_3, lock_regions },
		{ dsp->base + HALO_MPU_XREG_ACCESS_3,   lock_regions },
		{ dsp->base + HALO_MPU_YREG_ACCESS_3,   lock_regions },
		{ dsp->base + HALO_MPU_LOCK_CONFIG,     0 },
	};

	return regmap_multi_reg_write(dsp->regmap, config, ARRAY_SIZE(config));
}

/**
 * cs_dsp_set_dspclk() - Applies the given frequency to the given cs_dsp
 * @dsp: pointer to DSP structure
 * @freq: clock rate to set
 *
 * This is only for use on ADSP2 cores.
 *
 * Return: Zero for success, a negative number on error.
 */
int cs_dsp_set_dspclk(struct cs_dsp *dsp, unsigned int freq)
{
	int ret;

	ret = regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CLOCKING,
				 ADSP2_CLK_SEL_MASK,
				 freq << ADSP2_CLK_SEL_SHIFT);
	if (ret)
		cs_dsp_err(dsp, "Failed to set clock rate: %d\n", ret);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_set_dspclk, FW_CS_DSP);

static void cs_dsp_stop_watchdog(struct cs_dsp *dsp)
{
	regmap_update_bits(dsp->regmap, dsp->base + ADSP2_WATCHDOG,
			   ADSP2_WDT_ENA_MASK, 0);
}

static void cs_dsp_halo_stop_watchdog(struct cs_dsp *dsp)
{
	regmap_update_bits(dsp->regmap, dsp->base + HALO_WDT_CONTROL,
			   HALO_WDT_EN_MASK, 0);
}

/**
 * cs_dsp_power_up() - Downloads firmware to the DSP
 * @dsp: pointer to DSP structure
 * @wmfw_firmware: the firmware to be sent
 * @wmfw_filename: file name of firmware to be sent
 * @coeff_firmware: the coefficient data to be sent
 * @coeff_filename: file name of coefficient to data be sent
 * @fw_name: the user-friendly firmware name
 *
 * This function is used on ADSP2 and Halo DSP cores, it powers-up the DSP core
 * and downloads the firmware but does not start the firmware running. The
 * cs_dsp booted flag will be set once completed and if the core has a low-power
 * memory retention mode it will be put into this state after the firmware is
 * downloaded.
 *
 * Return: Zero for success, a negative number on error.
 */
int cs_dsp_power_up(struct cs_dsp *dsp,
		    const struct firmware *wmfw_firmware, char *wmfw_filename,
		    const struct firmware *coeff_firmware, char *coeff_filename,
		    const char *fw_name)
{
	int ret;

	mutex_lock(&dsp->pwr_lock);

	dsp->fw_name = fw_name;

	if (dsp->ops->enable_memory) {
		ret = dsp->ops->enable_memory(dsp);
		if (ret != 0)
			goto err_mutex;
	}

	if (dsp->ops->enable_core) {
		ret = dsp->ops->enable_core(dsp);
		if (ret != 0)
			goto err_mem;
	}

	ret = cs_dsp_load(dsp, wmfw_firmware, wmfw_filename);
	if (ret != 0)
		goto err_ena;

	ret = dsp->ops->setup_algs(dsp);
	if (ret != 0)
		goto err_ena;

	ret = cs_dsp_load_coeff(dsp, coeff_firmware, coeff_filename);
	if (ret != 0)
		goto err_ena;

	/* Initialize caches for enabled and unset controls */
	ret = cs_dsp_coeff_init_control_caches(dsp);
	if (ret != 0)
		goto err_ena;

	if (dsp->ops->disable_core)
		dsp->ops->disable_core(dsp);

	dsp->booted = true;

	mutex_unlock(&dsp->pwr_lock);

	return 0;
err_ena:
	if (dsp->ops->disable_core)
		dsp->ops->disable_core(dsp);
err_mem:
	if (dsp->ops->disable_memory)
		dsp->ops->disable_memory(dsp);
err_mutex:
	mutex_unlock(&dsp->pwr_lock);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_power_up, FW_CS_DSP);

/**
 * cs_dsp_power_down() - Powers-down the DSP
 * @dsp: pointer to DSP structure
 *
 * cs_dsp_stop() must have been called before this function. The core will be
 * fully powered down and so the memory will not be retained.
 */
void cs_dsp_power_down(struct cs_dsp *dsp)
{
	struct cs_dsp_coeff_ctl *ctl;

	mutex_lock(&dsp->pwr_lock);

	cs_dsp_debugfs_clear(dsp);

	dsp->fw_id = 0;
	dsp->fw_id_version = 0;

	dsp->booted = false;

	if (dsp->ops->disable_memory)
		dsp->ops->disable_memory(dsp);

	list_for_each_entry(ctl, &dsp->ctl_list, list)
		ctl->enabled = 0;

	cs_dsp_free_alg_regions(dsp);

	mutex_unlock(&dsp->pwr_lock);

	cs_dsp_dbg(dsp, "Shutdown complete\n");
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_power_down, FW_CS_DSP);

static int cs_dsp_adsp2_start_core(struct cs_dsp *dsp)
{
	return regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CONTROL,
				  ADSP2_CORE_ENA | ADSP2_START,
				  ADSP2_CORE_ENA | ADSP2_START);
}

static void cs_dsp_adsp2_stop_core(struct cs_dsp *dsp)
{
	regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CONTROL,
			   ADSP2_CORE_ENA | ADSP2_START, 0);
}

/**
 * cs_dsp_run() - Starts the firmware running
 * @dsp: pointer to DSP structure
 *
 * cs_dsp_power_up() must have previously been called successfully.
 *
 * Return: Zero for success, a negative number on error.
 */
int cs_dsp_run(struct cs_dsp *dsp)
{
	int ret;

	mutex_lock(&dsp->pwr_lock);

	if (!dsp->booted) {
		ret = -EIO;
		goto err;
	}

	if (dsp->ops->enable_core) {
		ret = dsp->ops->enable_core(dsp);
		if (ret != 0)
			goto err;
	}

	if (dsp->client_ops->pre_run) {
		ret = dsp->client_ops->pre_run(dsp);
		if (ret)
			goto err;
	}

	/* Sync set controls */
	ret = cs_dsp_coeff_sync_controls(dsp);
	if (ret != 0)
		goto err;

	if (dsp->ops->lock_memory) {
		ret = dsp->ops->lock_memory(dsp, dsp->lock_regions);
		if (ret != 0) {
			cs_dsp_err(dsp, "Error configuring MPU: %d\n", ret);
			goto err;
		}
	}

	if (dsp->ops->start_core) {
		ret = dsp->ops->start_core(dsp);
		if (ret != 0)
			goto err;
	}

	dsp->running = true;

	if (dsp->client_ops->post_run) {
		ret = dsp->client_ops->post_run(dsp);
		if (ret)
			goto err;
	}

	mutex_unlock(&dsp->pwr_lock);

	return 0;

err:
	if (dsp->ops->stop_core)
		dsp->ops->stop_core(dsp);
	if (dsp->ops->disable_core)
		dsp->ops->disable_core(dsp);
	mutex_unlock(&dsp->pwr_lock);

	return ret;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_run, FW_CS_DSP);

/**
 * cs_dsp_stop() - Stops the firmware
 * @dsp: pointer to DSP structure
 *
 * Memory will not be disabled so firmware will remain loaded.
 */
void cs_dsp_stop(struct cs_dsp *dsp)
{
	/* Tell the firmware to cleanup */
	cs_dsp_signal_event_controls(dsp, CS_DSP_FW_EVENT_SHUTDOWN);

	if (dsp->ops->stop_watchdog)
		dsp->ops->stop_watchdog(dsp);

	/* Log firmware state, it can be useful for analysis */
	if (dsp->ops->show_fw_status)
		dsp->ops->show_fw_status(dsp);

	mutex_lock(&dsp->pwr_lock);

	if (dsp->client_ops->pre_stop)
		dsp->client_ops->pre_stop(dsp);

	dsp->running = false;

	if (dsp->ops->stop_core)
		dsp->ops->stop_core(dsp);
	if (dsp->ops->disable_core)
		dsp->ops->disable_core(dsp);

	if (dsp->client_ops->post_stop)
		dsp->client_ops->post_stop(dsp);

	mutex_unlock(&dsp->pwr_lock);

	cs_dsp_dbg(dsp, "Execution stopped\n");
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_stop, FW_CS_DSP);

static int cs_dsp_halo_start_core(struct cs_dsp *dsp)
{
	int ret;

	ret = regmap_update_bits(dsp->regmap, dsp->base + HALO_CCM_CORE_CONTROL,
				 HALO_CORE_RESET | HALO_CORE_EN,
				 HALO_CORE_RESET | HALO_CORE_EN);
	if (ret)
		return ret;

	return regmap_update_bits(dsp->regmap, dsp->base + HALO_CCM_CORE_CONTROL,
				  HALO_CORE_RESET, 0);
}

static void cs_dsp_halo_stop_core(struct cs_dsp *dsp)
{
	regmap_update_bits(dsp->regmap, dsp->base + HALO_CCM_CORE_CONTROL,
			   HALO_CORE_EN, 0);

	/* reset halo core with CORE_SOFT_RESET */
	regmap_update_bits(dsp->regmap, dsp->base + HALO_CORE_SOFT_RESET,
			   HALO_CORE_SOFT_RESET_MASK, 1);
}

/**
 * cs_dsp_adsp2_init() - Initialise a cs_dsp structure representing a ADSP2 core
 * @dsp: pointer to DSP structure
 *
 * Return: Zero for success, a negative number on error.
 */
int cs_dsp_adsp2_init(struct cs_dsp *dsp)
{
	int ret;

	switch (dsp->rev) {
	case 0:
		/*
		 * Disable the DSP memory by default when in reset for a small
		 * power saving.
		 */
		ret = regmap_update_bits(dsp->regmap, dsp->base + ADSP2_CONTROL,
					 ADSP2_MEM_ENA, 0);
		if (ret) {
			cs_dsp_err(dsp,
				   "Failed to clear memory retention: %d\n", ret);
			return ret;
		}

		dsp->ops = &cs_dsp_adsp2_ops[0];
		break;
	case 1:
		dsp->ops = &cs_dsp_adsp2_ops[1];
		break;
	default:
		dsp->ops = &cs_dsp_adsp2_ops[2];
		break;
	}

	return cs_dsp_common_init(dsp);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_adsp2_init, FW_CS_DSP);

/**
 * cs_dsp_halo_init() - Initialise a cs_dsp structure representing a HALO Core DSP
 * @dsp: pointer to DSP structure
 *
 * Return: Zero for success, a negative number on error.
 */
int cs_dsp_halo_init(struct cs_dsp *dsp)
{
	if (dsp->no_core_startstop)
		dsp->ops = &cs_dsp_halo_ao_ops;
	else
		dsp->ops = &cs_dsp_halo_ops;

	return cs_dsp_common_init(dsp);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_halo_init, FW_CS_DSP);

/**
 * cs_dsp_remove() - Clean a cs_dsp before deletion
 * @dsp: pointer to DSP structure
 */
void cs_dsp_remove(struct cs_dsp *dsp)
{
	struct cs_dsp_coeff_ctl *ctl;

	while (!list_empty(&dsp->ctl_list)) {
		ctl = list_first_entry(&dsp->ctl_list, struct cs_dsp_coeff_ctl, list);

		if (dsp->client_ops->control_remove)
			dsp->client_ops->control_remove(ctl);

		list_del(&ctl->list);
		cs_dsp_free_ctl_blk(ctl);
	}
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_remove, FW_CS_DSP);

/**
 * cs_dsp_read_raw_data_block() - Reads a block of data from DSP memory
 * @dsp: pointer to DSP structure
 * @mem_type: the type of DSP memory containing the data to be read
 * @mem_addr: the address of the data within the memory region
 * @num_words: the length of the data to read
 * @data: a buffer to store the fetched data
 *
 * If this is used to read unpacked 24-bit memory, each 24-bit DSP word will
 * occupy 32-bits in data (MSbyte will be 0). This padding can be removed using
 * cs_dsp_remove_padding()
 *
 * Return: Zero for success, a negative number on error.
 */
int cs_dsp_read_raw_data_block(struct cs_dsp *dsp, int mem_type, unsigned int mem_addr,
			       unsigned int num_words, __be32 *data)
{
	struct cs_dsp_region const *mem = cs_dsp_find_region(dsp, mem_type);
	unsigned int reg;
	int ret;

	lockdep_assert_held(&dsp->pwr_lock);

	if (!mem)
		return -EINVAL;

	reg = dsp->ops->region_to_reg(mem, mem_addr);

	ret = regmap_raw_read(dsp->regmap, reg, data,
			      sizeof(*data) * num_words);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_read_raw_data_block, FW_CS_DSP);

/**
 * cs_dsp_read_data_word() - Reads a word from DSP memory
 * @dsp: pointer to DSP structure
 * @mem_type: the type of DSP memory containing the data to be read
 * @mem_addr: the address of the data within the memory region
 * @data: a buffer to store the fetched data
 *
 * Return: Zero for success, a negative number on error.
 */
int cs_dsp_read_data_word(struct cs_dsp *dsp, int mem_type, unsigned int mem_addr, u32 *data)
{
	__be32 raw;
	int ret;

	ret = cs_dsp_read_raw_data_block(dsp, mem_type, mem_addr, 1, &raw);
	if (ret < 0)
		return ret;

	*data = be32_to_cpu(raw) & 0x00ffffffu;

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_read_data_word, FW_CS_DSP);

/**
 * cs_dsp_write_data_word() - Writes a word to DSP memory
 * @dsp: pointer to DSP structure
 * @mem_type: the type of DSP memory containing the data to be written
 * @mem_addr: the address of the data within the memory region
 * @data: the data to be written
 *
 * Return: Zero for success, a negative number on error.
 */
int cs_dsp_write_data_word(struct cs_dsp *dsp, int mem_type, unsigned int mem_addr, u32 data)
{
	struct cs_dsp_region const *mem = cs_dsp_find_region(dsp, mem_type);
	__be32 val = cpu_to_be32(data & 0x00ffffffu);
	unsigned int reg;

	lockdep_assert_held(&dsp->pwr_lock);

	if (!mem)
		return -EINVAL;

	reg = dsp->ops->region_to_reg(mem, mem_addr);

	return regmap_raw_write(dsp->regmap, reg, &val, sizeof(val));
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_write_data_word, FW_CS_DSP);

/**
 * cs_dsp_remove_padding() - Convert unpacked words to packed bytes
 * @buf: buffer containing DSP words read from DSP memory
 * @nwords: number of words to convert
 *
 * DSP words from the register map have pad bytes and the data bytes
 * are in swapped order. This swaps to the native endian order and
 * strips the pad bytes.
 */
void cs_dsp_remove_padding(u32 *buf, int nwords)
{
	const __be32 *pack_in = (__be32 *)buf;
	u8 *pack_out = (u8 *)buf;
	int i;

	for (i = 0; i < nwords; i++) {
		u32 word = be32_to_cpu(*pack_in++);
		*pack_out++ = (u8)word;
		*pack_out++ = (u8)(word >> 8);
		*pack_out++ = (u8)(word >> 16);
	}
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_remove_padding, FW_CS_DSP);

/**
 * cs_dsp_adsp2_bus_error() - Handle a DSP bus error interrupt
 * @dsp: pointer to DSP structure
 *
 * The firmware and DSP state will be logged for future analysis.
 */
void cs_dsp_adsp2_bus_error(struct cs_dsp *dsp)
{
	unsigned int val;
	struct regmap *regmap = dsp->regmap;
	int ret = 0;

	mutex_lock(&dsp->pwr_lock);

	ret = regmap_read(regmap, dsp->base + ADSP2_LOCK_REGION_CTRL, &val);
	if (ret) {
		cs_dsp_err(dsp,
			   "Failed to read Region Lock Ctrl register: %d\n", ret);
		goto error;
	}

	if (val & ADSP2_WDT_TIMEOUT_STS_MASK) {
		cs_dsp_err(dsp, "watchdog timeout error\n");
		dsp->ops->stop_watchdog(dsp);
		if (dsp->client_ops->watchdog_expired)
			dsp->client_ops->watchdog_expired(dsp);
	}

	if (val & (ADSP2_ADDR_ERR_MASK | ADSP2_REGION_LOCK_ERR_MASK)) {
		if (val & ADSP2_ADDR_ERR_MASK)
			cs_dsp_err(dsp, "bus error: address error\n");
		else
			cs_dsp_err(dsp, "bus error: region lock error\n");

		ret = regmap_read(regmap, dsp->base + ADSP2_BUS_ERR_ADDR, &val);
		if (ret) {
			cs_dsp_err(dsp,
				   "Failed to read Bus Err Addr register: %d\n",
				   ret);
			goto error;
		}

		cs_dsp_err(dsp, "bus error address = 0x%x\n",
			   val & ADSP2_BUS_ERR_ADDR_MASK);

		ret = regmap_read(regmap,
				  dsp->base + ADSP2_PMEM_ERR_ADDR_XMEM_ERR_ADDR,
				  &val);
		if (ret) {
			cs_dsp_err(dsp,
				   "Failed to read Pmem Xmem Err Addr register: %d\n",
				   ret);
			goto error;
		}

		cs_dsp_err(dsp, "xmem error address = 0x%x\n",
			   val & ADSP2_XMEM_ERR_ADDR_MASK);
		cs_dsp_err(dsp, "pmem error address = 0x%x\n",
			   (val & ADSP2_PMEM_ERR_ADDR_MASK) >>
			   ADSP2_PMEM_ERR_ADDR_SHIFT);
	}

	regmap_update_bits(regmap, dsp->base + ADSP2_LOCK_REGION_CTRL,
			   ADSP2_CTRL_ERR_EINT, ADSP2_CTRL_ERR_EINT);

error:
	mutex_unlock(&dsp->pwr_lock);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_adsp2_bus_error, FW_CS_DSP);

/**
 * cs_dsp_halo_bus_error() - Handle a DSP bus error interrupt
 * @dsp: pointer to DSP structure
 *
 * The firmware and DSP state will be logged for future analysis.
 */
void cs_dsp_halo_bus_error(struct cs_dsp *dsp)
{
	struct regmap *regmap = dsp->regmap;
	unsigned int fault[6];
	struct reg_sequence clear[] = {
		{ dsp->base + HALO_MPU_XM_VIO_STATUS,     0x0 },
		{ dsp->base + HALO_MPU_YM_VIO_STATUS,     0x0 },
		{ dsp->base + HALO_MPU_PM_VIO_STATUS,     0x0 },
	};
	int ret;

	mutex_lock(&dsp->pwr_lock);

	ret = regmap_read(regmap, dsp->base_sysinfo + HALO_AHBM_WINDOW_DEBUG_1,
			  fault);
	if (ret) {
		cs_dsp_warn(dsp, "Failed to read AHB DEBUG_1: %d\n", ret);
		goto exit_unlock;
	}

	cs_dsp_warn(dsp, "AHB: STATUS: 0x%x ADDR: 0x%x\n",
		    *fault & HALO_AHBM_FLAGS_ERR_MASK,
		    (*fault & HALO_AHBM_CORE_ERR_ADDR_MASK) >>
		    HALO_AHBM_CORE_ERR_ADDR_SHIFT);

	ret = regmap_read(regmap, dsp->base_sysinfo + HALO_AHBM_WINDOW_DEBUG_0,
			  fault);
	if (ret) {
		cs_dsp_warn(dsp, "Failed to read AHB DEBUG_0: %d\n", ret);
		goto exit_unlock;
	}

	cs_dsp_warn(dsp, "AHB: SYS_ADDR: 0x%x\n", *fault);

	ret = regmap_bulk_read(regmap, dsp->base + HALO_MPU_XM_VIO_ADDR,
			       fault, ARRAY_SIZE(fault));
	if (ret) {
		cs_dsp_warn(dsp, "Failed to read MPU fault info: %d\n", ret);
		goto exit_unlock;
	}

	cs_dsp_warn(dsp, "XM: STATUS:0x%x ADDR:0x%x\n", fault[1], fault[0]);
	cs_dsp_warn(dsp, "YM: STATUS:0x%x ADDR:0x%x\n", fault[3], fault[2]);
	cs_dsp_warn(dsp, "PM: STATUS:0x%x ADDR:0x%x\n", fault[5], fault[4]);

	ret = regmap_multi_reg_write(dsp->regmap, clear, ARRAY_SIZE(clear));
	if (ret)
		cs_dsp_warn(dsp, "Failed to clear MPU status: %d\n", ret);

exit_unlock:
	mutex_unlock(&dsp->pwr_lock);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_halo_bus_error, FW_CS_DSP);

/**
 * cs_dsp_halo_wdt_expire() - Handle DSP watchdog expiry
 * @dsp: pointer to DSP structure
 *
 * This is logged for future analysis.
 */
void cs_dsp_halo_wdt_expire(struct cs_dsp *dsp)
{
	mutex_lock(&dsp->pwr_lock);

	cs_dsp_warn(dsp, "WDT Expiry Fault\n");

	dsp->ops->stop_watchdog(dsp);
	if (dsp->client_ops->watchdog_expired)
		dsp->client_ops->watchdog_expired(dsp);

	mutex_unlock(&dsp->pwr_lock);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_halo_wdt_expire, FW_CS_DSP);

static const struct cs_dsp_ops cs_dsp_adsp1_ops = {
	.validate_version = cs_dsp_validate_version,
	.parse_sizes = cs_dsp_adsp1_parse_sizes,
	.region_to_reg = cs_dsp_region_to_reg,
};

static const struct cs_dsp_ops cs_dsp_adsp2_ops[] = {
	{
		.parse_sizes = cs_dsp_adsp2_parse_sizes,
		.validate_version = cs_dsp_validate_version,
		.setup_algs = cs_dsp_adsp2_setup_algs,
		.region_to_reg = cs_dsp_region_to_reg,

		.show_fw_status = cs_dsp_adsp2_show_fw_status,

		.enable_memory = cs_dsp_adsp2_enable_memory,
		.disable_memory = cs_dsp_adsp2_disable_memory,

		.enable_core = cs_dsp_adsp2_enable_core,
		.disable_core = cs_dsp_adsp2_disable_core,

		.start_core = cs_dsp_adsp2_start_core,
		.stop_core = cs_dsp_adsp2_stop_core,

	},
	{
		.parse_sizes = cs_dsp_adsp2_parse_sizes,
		.validate_version = cs_dsp_validate_version,
		.setup_algs = cs_dsp_adsp2_setup_algs,
		.region_to_reg = cs_dsp_region_to_reg,

		.show_fw_status = cs_dsp_adsp2v2_show_fw_status,

		.enable_memory = cs_dsp_adsp2_enable_memory,
		.disable_memory = cs_dsp_adsp2_disable_memory,
		.lock_memory = cs_dsp_adsp2_lock,

		.enable_core = cs_dsp_adsp2v2_enable_core,
		.disable_core = cs_dsp_adsp2v2_disable_core,

		.start_core = cs_dsp_adsp2_start_core,
		.stop_core = cs_dsp_adsp2_stop_core,
	},
	{
		.parse_sizes = cs_dsp_adsp2_parse_sizes,
		.validate_version = cs_dsp_validate_version,
		.setup_algs = cs_dsp_adsp2_setup_algs,
		.region_to_reg = cs_dsp_region_to_reg,

		.show_fw_status = cs_dsp_adsp2v2_show_fw_status,
		.stop_watchdog = cs_dsp_stop_watchdog,

		.enable_memory = cs_dsp_adsp2_enable_memory,
		.disable_memory = cs_dsp_adsp2_disable_memory,
		.lock_memory = cs_dsp_adsp2_lock,

		.enable_core = cs_dsp_adsp2v2_enable_core,
		.disable_core = cs_dsp_adsp2v2_disable_core,

		.start_core = cs_dsp_adsp2_start_core,
		.stop_core = cs_dsp_adsp2_stop_core,
	},
};

static const struct cs_dsp_ops cs_dsp_halo_ops = {
	.parse_sizes = cs_dsp_adsp2_parse_sizes,
	.validate_version = cs_dsp_halo_validate_version,
	.setup_algs = cs_dsp_halo_setup_algs,
	.region_to_reg = cs_dsp_halo_region_to_reg,

	.show_fw_status = cs_dsp_halo_show_fw_status,
	.stop_watchdog = cs_dsp_halo_stop_watchdog,

	.lock_memory = cs_dsp_halo_configure_mpu,

	.start_core = cs_dsp_halo_start_core,
	.stop_core = cs_dsp_halo_stop_core,
};

static const struct cs_dsp_ops cs_dsp_halo_ao_ops = {
	.parse_sizes = cs_dsp_adsp2_parse_sizes,
	.validate_version = cs_dsp_halo_validate_version,
	.setup_algs = cs_dsp_halo_setup_algs,
	.region_to_reg = cs_dsp_halo_region_to_reg,
	.show_fw_status = cs_dsp_halo_show_fw_status,
};

/**
 * cs_dsp_chunk_write() - Format data to a DSP memory chunk
 * @ch: Pointer to the chunk structure
 * @nbits: Number of bits to write
 * @val: Value to write
 *
 * This function sequentially writes values into the format required for DSP
 * memory, it handles both inserting of the padding bytes and converting to
 * big endian. Note that data is only committed to the chunk when a whole DSP
 * words worth of data is available.
 *
 * Return: Zero for success, a negative number on error.
 */
int cs_dsp_chunk_write(struct cs_dsp_chunk *ch, int nbits, u32 val)
{
	int nwrite, i;

	nwrite = min(CS_DSP_DATA_WORD_BITS - ch->cachebits, nbits);

	ch->cache <<= nwrite;
	ch->cache |= val >> (nbits - nwrite);
	ch->cachebits += nwrite;
	nbits -= nwrite;

	if (ch->cachebits == CS_DSP_DATA_WORD_BITS) {
		if (cs_dsp_chunk_end(ch))
			return -ENOSPC;

		ch->cache &= 0xFFFFFF;
		for (i = 0; i < sizeof(ch->cache); i++, ch->cache <<= BITS_PER_BYTE)
			*ch->data++ = (ch->cache & 0xFF000000) >> CS_DSP_DATA_WORD_BITS;

		ch->bytes += sizeof(ch->cache);
		ch->cachebits = 0;
	}

	if (nbits)
		return cs_dsp_chunk_write(ch, nbits, val);

	return 0;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_chunk_write, FW_CS_DSP);

/**
 * cs_dsp_chunk_flush() - Pad remaining data with zero and commit to chunk
 * @ch: Pointer to the chunk structure
 *
 * As cs_dsp_chunk_write only writes data when a whole DSP word is ready to
 * be written out it is possible that some data will remain in the cache, this
 * function will pad that data with zeros upto a whole DSP word and write out.
 *
 * Return: Zero for success, a negative number on error.
 */
int cs_dsp_chunk_flush(struct cs_dsp_chunk *ch)
{
	if (!ch->cachebits)
		return 0;

	return cs_dsp_chunk_write(ch, CS_DSP_DATA_WORD_BITS - ch->cachebits, 0);
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_chunk_flush, FW_CS_DSP);

/**
 * cs_dsp_chunk_read() - Parse data from a DSP memory chunk
 * @ch: Pointer to the chunk structure
 * @nbits: Number of bits to read
 *
 * This function sequentially reads values from a DSP memory formatted buffer,
 * it handles both removing of the padding bytes and converting from big endian.
 *
 * Return: A negative number is returned on error, otherwise the read value.
 */
int cs_dsp_chunk_read(struct cs_dsp_chunk *ch, int nbits)
{
	int nread, i;
	u32 result;

	if (!ch->cachebits) {
		if (cs_dsp_chunk_end(ch))
			return -ENOSPC;

		ch->cache = 0;
		ch->cachebits = CS_DSP_DATA_WORD_BITS;

		for (i = 0; i < sizeof(ch->cache); i++, ch->cache <<= BITS_PER_BYTE)
			ch->cache |= *ch->data++;

		ch->bytes += sizeof(ch->cache);
	}

	nread = min(ch->cachebits, nbits);
	nbits -= nread;

	result = ch->cache >> ((sizeof(ch->cache) * BITS_PER_BYTE) - nread);
	ch->cache <<= nread;
	ch->cachebits -= nread;

	if (nbits)
		result = (result << nbits) | cs_dsp_chunk_read(ch, nbits);

	return result;
}
EXPORT_SYMBOL_NS_GPL(cs_dsp_chunk_read, FW_CS_DSP);

MODULE_DESCRIPTION("Cirrus Logic DSP Support");
MODULE_AUTHOR("Simon Trimmer <simont@opensource.cirrus.com>");
MODULE_LICENSE("GPL v2");
