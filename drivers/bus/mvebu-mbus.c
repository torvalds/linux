/*
 * Address map functions for Marvell EBU SoCs (Kirkwood, Armada
 * 370/XP, Dove, Orion5x and MV78xx0)
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 *
 * The Marvell EBU SoCs have a configurable physical address space:
 * the physical address at which certain devices (PCIe, NOR, NAND,
 * etc.) sit can be configured. The configuration takes place through
 * two sets of registers:
 *
 * - One to configure the access of the CPU to the devices. Depending
 *   on the families, there are between 8 and 20 configurable windows,
 *   each can be use to create a physical memory window that maps to a
 *   specific device. Devices are identified by a tuple (target,
 *   attribute).
 *
 * - One to configure the access to the CPU to the SDRAM. There are
 *   either 2 (for Dove) or 4 (for other families) windows to map the
 *   SDRAM into the physical address space.
 *
 * This driver:
 *
 * - Reads out the SDRAM address decoding windows at initialization
 *   time, and fills the mvebu_mbus_dram_info structure with these
 *   informations. The exported function mv_mbus_dram_info() allow
 *   device drivers to get those informations related to the SDRAM
 *   address decoding windows. This is because devices also have their
 *   own windows (configured through registers that are part of each
 *   device register space), and therefore the drivers for Marvell
 *   devices have to configure those device -> SDRAM windows to ensure
 *   that DMA works properly.
 *
 * - Provides an API for platform code or device drivers to
 *   dynamically add or remove address decoding windows for the CPU ->
 *   device accesses. This API is mvebu_mbus_add_window(),
 *   mvebu_mbus_add_window_remap_flags() and
 *   mvebu_mbus_del_window(). Since the (target, attribute) values
 *   differ from one SoC family to another, the API uses a 'const char
 *   *' string to identify devices, and this driver is responsible for
 *   knowing the mapping between the name of a device and its
 *   corresponding (target, attribute) in the current SoC family.
 *
 * - Provides a debugfs interface in /sys/kernel/debug/mvebu-mbus/ to
 *   see the list of CPU -> SDRAM windows and their configuration
 *   (file 'sdram') and the list of CPU -> devices windows and their
 *   configuration (file 'devices').
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mbus.h>
#include <linux/io.h>
#include <linux/ioport.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/debugfs.h>

/*
 * DDR target is the same on all platforms.
 */
#define TARGET_DDR		0

/*
 * CPU Address Decode Windows registers
 */
#define WIN_CTRL_OFF		0x0000
#define   WIN_CTRL_ENABLE       BIT(0)
#define   WIN_CTRL_TGT_MASK     0xf0
#define   WIN_CTRL_TGT_SHIFT    4
#define   WIN_CTRL_ATTR_MASK    0xff00
#define   WIN_CTRL_ATTR_SHIFT   8
#define   WIN_CTRL_SIZE_MASK    0xffff0000
#define   WIN_CTRL_SIZE_SHIFT   16
#define WIN_BASE_OFF		0x0004
#define   WIN_BASE_LOW          0xffff0000
#define   WIN_BASE_HIGH         0xf
#define WIN_REMAP_LO_OFF	0x0008
#define   WIN_REMAP_LOW         0xffff0000
#define WIN_REMAP_HI_OFF	0x000c

#define ATTR_HW_COHERENCY	(0x1 << 4)

#define DDR_BASE_CS_OFF(n)	(0x0000 + ((n) << 3))
#define  DDR_BASE_CS_HIGH_MASK  0xf
#define  DDR_BASE_CS_LOW_MASK   0xff000000
#define DDR_SIZE_CS_OFF(n)	(0x0004 + ((n) << 3))
#define  DDR_SIZE_ENABLED       BIT(0)
#define  DDR_SIZE_CS_MASK       0x1c
#define  DDR_SIZE_CS_SHIFT      2
#define  DDR_SIZE_MASK          0xff000000

#define DOVE_DDR_BASE_CS_OFF(n) ((n) << 4)

struct mvebu_mbus_mapping {
	const char *name;
	u8 target;
	u8 attr;
	u8 attrmask;
};

/*
 * Masks used for the 'attrmask' field of mvebu_mbus_mapping. They
 * allow to get the real attribute value, discarding the special bits
 * used to select a PCI MEM region or a PCI WA region. This allows the
 * debugfs code to reverse-match the name of a device from its
 * target/attr values.
 *
 * For all devices except PCI, all bits of 'attr' must be
 * considered. For most SoCs, only bit 3 should be ignored (it allows
 * to select between PCI MEM and PCI I/O). On Orion5x however, there
 * is the special bit 5 to select a PCI WA region.
 */
#define MAPDEF_NOMASK       0xff
#define MAPDEF_PCIMASK      0xf7
#define MAPDEF_ORIONPCIMASK 0xd7

/* Macro used to define one mvebu_mbus_mapping entry */
#define MAPDEF(__n, __t, __a, __m) \
	{ .name = __n, .target = __t, .attr = __a, .attrmask = __m }

struct mvebu_mbus_state;

struct mvebu_mbus_soc_data {
	unsigned int num_wins;
	unsigned int num_remappable_wins;
	unsigned int (*win_cfg_offset)(const int win);
	void (*setup_cpu_target)(struct mvebu_mbus_state *s);
	int (*show_cpu_target)(struct mvebu_mbus_state *s,
			       struct seq_file *seq, void *v);
	const struct mvebu_mbus_mapping *map;
};

struct mvebu_mbus_state {
	void __iomem *mbuswins_base;
	void __iomem *sdramwins_base;
	struct dentry *debugfs_root;
	struct dentry *debugfs_sdram;
	struct dentry *debugfs_devs;
	const struct mvebu_mbus_soc_data *soc;
	int hw_io_coherency;
};

static struct mvebu_mbus_state mbus_state;

static struct mbus_dram_target_info mvebu_mbus_dram_info;
const struct mbus_dram_target_info *mv_mbus_dram_info(void)
{
	return &mvebu_mbus_dram_info;
}
EXPORT_SYMBOL_GPL(mv_mbus_dram_info);

/*
 * Functions to manipulate the address decoding windows
 */

static void mvebu_mbus_read_window(struct mvebu_mbus_state *mbus,
				   int win, int *enabled, u64 *base,
				   u32 *size, u8 *target, u8 *attr,
				   u64 *remap)
{
	void __iomem *addr = mbus->mbuswins_base +
		mbus->soc->win_cfg_offset(win);
	u32 basereg = readl(addr + WIN_BASE_OFF);
	u32 ctrlreg = readl(addr + WIN_CTRL_OFF);

	if (!(ctrlreg & WIN_CTRL_ENABLE)) {
		*enabled = 0;
		return;
	}

	*enabled = 1;
	*base = ((u64)basereg & WIN_BASE_HIGH) << 32;
	*base |= (basereg & WIN_BASE_LOW);
	*size = (ctrlreg | ~WIN_CTRL_SIZE_MASK) + 1;

	if (target)
		*target = (ctrlreg & WIN_CTRL_TGT_MASK) >> WIN_CTRL_TGT_SHIFT;

	if (attr)
		*attr = (ctrlreg & WIN_CTRL_ATTR_MASK) >> WIN_CTRL_ATTR_SHIFT;

	if (remap) {
		if (win < mbus->soc->num_remappable_wins) {
			u32 remap_low = readl(addr + WIN_REMAP_LO_OFF);
			u32 remap_hi  = readl(addr + WIN_REMAP_HI_OFF);
			*remap = ((u64)remap_hi << 32) | remap_low;
		} else
			*remap = 0;
	}
}

static void mvebu_mbus_disable_window(struct mvebu_mbus_state *mbus,
				      int win)
{
	void __iomem *addr;

	addr = mbus->mbuswins_base + mbus->soc->win_cfg_offset(win);

	writel(0, addr + WIN_BASE_OFF);
	writel(0, addr + WIN_CTRL_OFF);
	if (win < mbus->soc->num_remappable_wins) {
		writel(0, addr + WIN_REMAP_LO_OFF);
		writel(0, addr + WIN_REMAP_HI_OFF);
	}
}

/* Checks whether the given window number is available */
static int mvebu_mbus_window_is_free(struct mvebu_mbus_state *mbus,
				     const int win)
{
	void __iomem *addr = mbus->mbuswins_base +
		mbus->soc->win_cfg_offset(win);
	u32 ctrl = readl(addr + WIN_CTRL_OFF);
	return !(ctrl & WIN_CTRL_ENABLE);
}

/*
 * Checks whether the given (base, base+size) area doesn't overlap an
 * existing region
 */
static int mvebu_mbus_window_conflicts(struct mvebu_mbus_state *mbus,
				       phys_addr_t base, size_t size,
				       u8 target, u8 attr)
{
	u64 end = (u64)base + size;
	int win;

	for (win = 0; win < mbus->soc->num_wins; win++) {
		u64 wbase, wend;
		u32 wsize;
		u8 wtarget, wattr;
		int enabled;

		mvebu_mbus_read_window(mbus, win,
				       &enabled, &wbase, &wsize,
				       &wtarget, &wattr, NULL);

		if (!enabled)
			continue;

		wend = wbase + wsize;

		/*
		 * Check if the current window overlaps with the
		 * proposed physical range
		 */
		if ((u64)base < wend && end > wbase)
			return 0;

		/*
		 * Check if target/attribute conflicts
		 */
		if (target == wtarget && attr == wattr)
			return 0;
	}

	return 1;
}

static int mvebu_mbus_find_window(struct mvebu_mbus_state *mbus,
				  phys_addr_t base, size_t size)
{
	int win;

	for (win = 0; win < mbus->soc->num_wins; win++) {
		u64 wbase;
		u32 wsize;
		int enabled;

		mvebu_mbus_read_window(mbus, win,
				       &enabled, &wbase, &wsize,
				       NULL, NULL, NULL);

		if (!enabled)
			continue;

		if (base == wbase && size == wsize)
			return win;
	}

	return -ENODEV;
}

static int mvebu_mbus_setup_window(struct mvebu_mbus_state *mbus,
				   int win, phys_addr_t base, size_t size,
				   phys_addr_t remap, u8 target,
				   u8 attr)
{
	void __iomem *addr = mbus->mbuswins_base +
		mbus->soc->win_cfg_offset(win);
	u32 ctrl, remap_addr;

	ctrl = ((size - 1) & WIN_CTRL_SIZE_MASK) |
		(attr << WIN_CTRL_ATTR_SHIFT)    |
		(target << WIN_CTRL_TGT_SHIFT)   |
		WIN_CTRL_ENABLE;

	writel(base & WIN_BASE_LOW, addr + WIN_BASE_OFF);
	writel(ctrl, addr + WIN_CTRL_OFF);
	if (win < mbus->soc->num_remappable_wins) {
		if (remap == MVEBU_MBUS_NO_REMAP)
			remap_addr = base;
		else
			remap_addr = remap;
		writel(remap_addr & WIN_REMAP_LOW, addr + WIN_REMAP_LO_OFF);
		writel(0, addr + WIN_REMAP_HI_OFF);
	}

	return 0;
}

static int mvebu_mbus_alloc_window(struct mvebu_mbus_state *mbus,
				   phys_addr_t base, size_t size,
				   phys_addr_t remap, u8 target,
				   u8 attr)
{
	int win;

	if (remap == MVEBU_MBUS_NO_REMAP) {
		for (win = mbus->soc->num_remappable_wins;
		     win < mbus->soc->num_wins; win++)
			if (mvebu_mbus_window_is_free(mbus, win))
				return mvebu_mbus_setup_window(mbus, win, base,
							       size, remap,
							       target, attr);
	}


	for (win = 0; win < mbus->soc->num_wins; win++)
		if (mvebu_mbus_window_is_free(mbus, win))
			return mvebu_mbus_setup_window(mbus, win, base, size,
						       remap, target, attr);

	return -ENOMEM;
}

/*
 * Debugfs debugging
 */

/* Common function used for Dove, Kirkwood, Armada 370/XP and Orion 5x */
static int mvebu_sdram_debug_show_orion(struct mvebu_mbus_state *mbus,
					struct seq_file *seq, void *v)
{
	int i;

	for (i = 0; i < 4; i++) {
		u32 basereg = readl(mbus->sdramwins_base + DDR_BASE_CS_OFF(i));
		u32 sizereg = readl(mbus->sdramwins_base + DDR_SIZE_CS_OFF(i));
		u64 base;
		u32 size;

		if (!(sizereg & DDR_SIZE_ENABLED)) {
			seq_printf(seq, "[%d] disabled\n", i);
			continue;
		}

		base = ((u64)basereg & DDR_BASE_CS_HIGH_MASK) << 32;
		base |= basereg & DDR_BASE_CS_LOW_MASK;
		size = (sizereg | ~DDR_SIZE_MASK);

		seq_printf(seq, "[%d] %016llx - %016llx : cs%d\n",
			   i, (unsigned long long)base,
			   (unsigned long long)base + size + 1,
			   (sizereg & DDR_SIZE_CS_MASK) >> DDR_SIZE_CS_SHIFT);
	}

	return 0;
}

/* Special function for Dove */
static int mvebu_sdram_debug_show_dove(struct mvebu_mbus_state *mbus,
				       struct seq_file *seq, void *v)
{
	int i;

	for (i = 0; i < 2; i++) {
		u32 map = readl(mbus->sdramwins_base + DOVE_DDR_BASE_CS_OFF(i));
		u64 base;
		u32 size;

		if (!(map & 1)) {
			seq_printf(seq, "[%d] disabled\n", i);
			continue;
		}

		base = map & 0xff800000;
		size = 0x100000 << (((map & 0x000f0000) >> 16) - 4);

		seq_printf(seq, "[%d] %016llx - %016llx : cs%d\n",
			   i, (unsigned long long)base,
			   (unsigned long long)base + size, i);
	}

	return 0;
}

static int mvebu_sdram_debug_show(struct seq_file *seq, void *v)
{
	struct mvebu_mbus_state *mbus = &mbus_state;
	return mbus->soc->show_cpu_target(mbus, seq, v);
}

static int mvebu_sdram_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, mvebu_sdram_debug_show, inode->i_private);
}

static const struct file_operations mvebu_sdram_debug_fops = {
	.open = mvebu_sdram_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int mvebu_devs_debug_show(struct seq_file *seq, void *v)
{
	struct mvebu_mbus_state *mbus = &mbus_state;
	int win;

	for (win = 0; win < mbus->soc->num_wins; win++) {
		u64 wbase, wremap;
		u32 wsize;
		u8 wtarget, wattr;
		int enabled, i;
		const char *name;

		mvebu_mbus_read_window(mbus, win,
				       &enabled, &wbase, &wsize,
				       &wtarget, &wattr, &wremap);

		if (!enabled) {
			seq_printf(seq, "[%02d] disabled\n", win);
			continue;
		}


		for (i = 0; mbus->soc->map[i].name; i++)
			if (mbus->soc->map[i].target == wtarget &&
			    mbus->soc->map[i].attr ==
			    (wattr & mbus->soc->map[i].attrmask))
				break;

		name = mbus->soc->map[i].name ?: "unknown";

		seq_printf(seq, "[%02d] %016llx - %016llx : %s",
			   win, (unsigned long long)wbase,
			   (unsigned long long)(wbase + wsize), name);

		if (win < mbus->soc->num_remappable_wins) {
			seq_printf(seq, " (remap %016llx)\n",
				   (unsigned long long)wremap);
		} else
			seq_printf(seq, "\n");
	}

	return 0;
}

static int mvebu_devs_debug_open(struct inode *inode, struct file *file)
{
	return single_open(file, mvebu_devs_debug_show, inode->i_private);
}

static const struct file_operations mvebu_devs_debug_fops = {
	.open = mvebu_devs_debug_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

/*
 * SoC-specific functions and definitions
 */

static unsigned int orion_mbus_win_offset(int win)
{
	return win << 4;
}

static unsigned int armada_370_xp_mbus_win_offset(int win)
{
	/* The register layout is a bit annoying and the below code
	 * tries to cope with it.
	 * - At offset 0x0, there are the registers for the first 8
	 *   windows, with 4 registers of 32 bits per window (ctrl,
	 *   base, remap low, remap high)
	 * - Then at offset 0x80, there is a hole of 0x10 bytes for
	 *   the internal registers base address and internal units
	 *   sync barrier register.
	 * - Then at offset 0x90, there the registers for 12
	 *   windows, with only 2 registers of 32 bits per window
	 *   (ctrl, base).
	 */
	if (win < 8)
		return win << 4;
	else
		return 0x90 + ((win - 8) << 3);
}

static unsigned int mv78xx0_mbus_win_offset(int win)
{
	if (win < 8)
		return win << 4;
	else
		return 0x900 + ((win - 8) << 4);
}

static void __init
mvebu_mbus_default_setup_cpu_target(struct mvebu_mbus_state *mbus)
{
	int i;
	int cs;

	mvebu_mbus_dram_info.mbus_dram_target_id = TARGET_DDR;

	for (i = 0, cs = 0; i < 4; i++) {
		u32 base = readl(mbus->sdramwins_base + DDR_BASE_CS_OFF(i));
		u32 size = readl(mbus->sdramwins_base + DDR_SIZE_CS_OFF(i));

		/*
		 * We only take care of entries for which the chip
		 * select is enabled, and that don't have high base
		 * address bits set (devices can only access the first
		 * 32 bits of the memory).
		 */
		if ((size & DDR_SIZE_ENABLED) &&
		    !(base & DDR_BASE_CS_HIGH_MASK)) {
			struct mbus_dram_window *w;

			w = &mvebu_mbus_dram_info.cs[cs++];
			w->cs_index = i;
			w->mbus_attr = 0xf & ~(1 << i);
			if (mbus->hw_io_coherency)
				w->mbus_attr |= ATTR_HW_COHERENCY;
			w->base = base & DDR_BASE_CS_LOW_MASK;
			w->size = (size | ~DDR_SIZE_MASK) + 1;
		}
	}
	mvebu_mbus_dram_info.num_cs = cs;
}

static void __init
mvebu_mbus_dove_setup_cpu_target(struct mvebu_mbus_state *mbus)
{
	int i;
	int cs;

	mvebu_mbus_dram_info.mbus_dram_target_id = TARGET_DDR;

	for (i = 0, cs = 0; i < 2; i++) {
		u32 map = readl(mbus->sdramwins_base + DOVE_DDR_BASE_CS_OFF(i));

		/*
		 * Chip select enabled?
		 */
		if (map & 1) {
			struct mbus_dram_window *w;

			w = &mvebu_mbus_dram_info.cs[cs++];
			w->cs_index = i;
			w->mbus_attr = 0; /* CS address decoding done inside */
					  /* the DDR controller, no need to  */
					  /* provide attributes */
			w->base = map & 0xff800000;
			w->size = 0x100000 << (((map & 0x000f0000) >> 16) - 4);
		}
	}

	mvebu_mbus_dram_info.num_cs = cs;
}

static const struct mvebu_mbus_mapping armada_370_map[] = {
	MAPDEF("bootrom",     1, 0xe0, MAPDEF_NOMASK),
	MAPDEF("devbus-boot", 1, 0x2f, MAPDEF_NOMASK),
	MAPDEF("devbus-cs0",  1, 0x3e, MAPDEF_NOMASK),
	MAPDEF("devbus-cs1",  1, 0x3d, MAPDEF_NOMASK),
	MAPDEF("devbus-cs2",  1, 0x3b, MAPDEF_NOMASK),
	MAPDEF("devbus-cs3",  1, 0x37, MAPDEF_NOMASK),
	MAPDEF("pcie0.0",     4, 0xe0, MAPDEF_PCIMASK),
	MAPDEF("pcie1.0",     8, 0xe0, MAPDEF_PCIMASK),
	{},
};

static const struct mvebu_mbus_soc_data armada_370_mbus_data = {
	.num_wins            = 20,
	.num_remappable_wins = 8,
	.win_cfg_offset      = armada_370_xp_mbus_win_offset,
	.setup_cpu_target    = mvebu_mbus_default_setup_cpu_target,
	.show_cpu_target     = mvebu_sdram_debug_show_orion,
	.map                 = armada_370_map,
};

static const struct mvebu_mbus_mapping armada_xp_map[] = {
	MAPDEF("bootrom",     1, 0x1d, MAPDEF_NOMASK),
	MAPDEF("devbus-boot", 1, 0x2f, MAPDEF_NOMASK),
	MAPDEF("devbus-cs0",  1, 0x3e, MAPDEF_NOMASK),
	MAPDEF("devbus-cs1",  1, 0x3d, MAPDEF_NOMASK),
	MAPDEF("devbus-cs2",  1, 0x3b, MAPDEF_NOMASK),
	MAPDEF("devbus-cs3",  1, 0x37, MAPDEF_NOMASK),
	MAPDEF("pcie0.0",     4, 0xe0, MAPDEF_PCIMASK),
	MAPDEF("pcie0.1",     4, 0xd0, MAPDEF_PCIMASK),
	MAPDEF("pcie0.2",     4, 0xb0, MAPDEF_PCIMASK),
	MAPDEF("pcie0.3",     4, 0x70, MAPDEF_PCIMASK),
	MAPDEF("pcie1.0",     8, 0xe0, MAPDEF_PCIMASK),
	MAPDEF("pcie1.1",     8, 0xd0, MAPDEF_PCIMASK),
	MAPDEF("pcie1.2",     8, 0xb0, MAPDEF_PCIMASK),
	MAPDEF("pcie1.3",     8, 0x70, MAPDEF_PCIMASK),
	MAPDEF("pcie2.0",     4, 0xf0, MAPDEF_PCIMASK),
	MAPDEF("pcie3.0",     8, 0xf0, MAPDEF_PCIMASK),
	{},
};

static const struct mvebu_mbus_soc_data armada_xp_mbus_data = {
	.num_wins            = 20,
	.num_remappable_wins = 8,
	.win_cfg_offset      = armada_370_xp_mbus_win_offset,
	.setup_cpu_target    = mvebu_mbus_default_setup_cpu_target,
	.show_cpu_target     = mvebu_sdram_debug_show_orion,
	.map                 = armada_xp_map,
};

static const struct mvebu_mbus_mapping kirkwood_map[] = {
	MAPDEF("pcie0.0", 4, 0xe0, MAPDEF_PCIMASK),
	MAPDEF("pcie1.0", 4, 0xd0, MAPDEF_PCIMASK),
	MAPDEF("sram",    3, 0x01, MAPDEF_NOMASK),
	MAPDEF("nand",    1, 0x2f, MAPDEF_NOMASK),
	{},
};

static const struct mvebu_mbus_soc_data kirkwood_mbus_data = {
	.num_wins            = 8,
	.num_remappable_wins = 4,
	.win_cfg_offset      = orion_mbus_win_offset,
	.setup_cpu_target    = mvebu_mbus_default_setup_cpu_target,
	.show_cpu_target     = mvebu_sdram_debug_show_orion,
	.map                 = kirkwood_map,
};

static const struct mvebu_mbus_mapping dove_map[] = {
	MAPDEF("pcie0.0",    0x4, 0xe0, MAPDEF_PCIMASK),
	MAPDEF("pcie1.0",    0x8, 0xe0, MAPDEF_PCIMASK),
	MAPDEF("cesa",       0x3, 0x01, MAPDEF_NOMASK),
	MAPDEF("bootrom",    0x1, 0xfd, MAPDEF_NOMASK),
	MAPDEF("scratchpad", 0xd, 0x0, MAPDEF_NOMASK),
	{},
};

static const struct mvebu_mbus_soc_data dove_mbus_data = {
	.num_wins            = 8,
	.num_remappable_wins = 4,
	.win_cfg_offset      = orion_mbus_win_offset,
	.setup_cpu_target    = mvebu_mbus_dove_setup_cpu_target,
	.show_cpu_target     = mvebu_sdram_debug_show_dove,
	.map                 = dove_map,
};

static const struct mvebu_mbus_mapping orion5x_map[] = {
	MAPDEF("pcie0.0",     4, 0x51, MAPDEF_ORIONPCIMASK),
	MAPDEF("pci0.0",      3, 0x51, MAPDEF_ORIONPCIMASK),
	MAPDEF("devbus-boot", 1, 0x0f, MAPDEF_NOMASK),
	MAPDEF("devbus-cs0",  1, 0x1e, MAPDEF_NOMASK),
	MAPDEF("devbus-cs1",  1, 0x1d, MAPDEF_NOMASK),
	MAPDEF("devbus-cs2",  1, 0x1b, MAPDEF_NOMASK),
	MAPDEF("sram",        0, 0x00, MAPDEF_NOMASK),
	{},
};

/*
 * Some variants of Orion5x have 4 remappable windows, some other have
 * only two of them.
 */
static const struct mvebu_mbus_soc_data orion5x_4win_mbus_data = {
	.num_wins            = 8,
	.num_remappable_wins = 4,
	.win_cfg_offset      = orion_mbus_win_offset,
	.setup_cpu_target    = mvebu_mbus_default_setup_cpu_target,
	.show_cpu_target     = mvebu_sdram_debug_show_orion,
	.map                 = orion5x_map,
};

static const struct mvebu_mbus_soc_data orion5x_2win_mbus_data = {
	.num_wins            = 8,
	.num_remappable_wins = 2,
	.win_cfg_offset      = orion_mbus_win_offset,
	.setup_cpu_target    = mvebu_mbus_default_setup_cpu_target,
	.show_cpu_target     = mvebu_sdram_debug_show_orion,
	.map                 = orion5x_map,
};

static const struct mvebu_mbus_mapping mv78xx0_map[] = {
	MAPDEF("pcie0.0", 4, 0xe0, MAPDEF_PCIMASK),
	MAPDEF("pcie0.1", 4, 0xd0, MAPDEF_PCIMASK),
	MAPDEF("pcie0.2", 4, 0xb0, MAPDEF_PCIMASK),
	MAPDEF("pcie0.3", 4, 0x70, MAPDEF_PCIMASK),
	MAPDEF("pcie1.0", 8, 0xe0, MAPDEF_PCIMASK),
	MAPDEF("pcie1.1", 8, 0xd0, MAPDEF_PCIMASK),
	MAPDEF("pcie1.2", 8, 0xb0, MAPDEF_PCIMASK),
	MAPDEF("pcie1.3", 8, 0x70, MAPDEF_PCIMASK),
	MAPDEF("pcie2.0", 4, 0xf0, MAPDEF_PCIMASK),
	MAPDEF("pcie3.0", 8, 0xf0, MAPDEF_PCIMASK),
	{},
};

static const struct mvebu_mbus_soc_data mv78xx0_mbus_data = {
	.num_wins            = 14,
	.num_remappable_wins = 8,
	.win_cfg_offset      = mv78xx0_mbus_win_offset,
	.setup_cpu_target    = mvebu_mbus_default_setup_cpu_target,
	.show_cpu_target     = mvebu_sdram_debug_show_orion,
	.map                 = mv78xx0_map,
};

/*
 * The driver doesn't yet have a DT binding because the details of
 * this DT binding still need to be sorted out. However, as a
 * preparation, we already use of_device_id to match a SoC description
 * string against the SoC specific details of this driver.
 */
static const struct of_device_id of_mvebu_mbus_ids[] = {
	{ .compatible = "marvell,armada370-mbus",
	  .data = &armada_370_mbus_data, },
	{ .compatible = "marvell,armadaxp-mbus",
	  .data = &armada_xp_mbus_data, },
	{ .compatible = "marvell,kirkwood-mbus",
	  .data = &kirkwood_mbus_data, },
	{ .compatible = "marvell,dove-mbus",
	  .data = &dove_mbus_data, },
	{ .compatible = "marvell,orion5x-88f5281-mbus",
	  .data = &orion5x_4win_mbus_data, },
	{ .compatible = "marvell,orion5x-88f5182-mbus",
	  .data = &orion5x_2win_mbus_data, },
	{ .compatible = "marvell,orion5x-88f5181-mbus",
	  .data = &orion5x_2win_mbus_data, },
	{ .compatible = "marvell,orion5x-88f6183-mbus",
	  .data = &orion5x_4win_mbus_data, },
	{ .compatible = "marvell,mv78xx0-mbus",
	  .data = &mv78xx0_mbus_data, },
	{ },
};

/*
 * Public API of the driver
 */
int mvebu_mbus_add_window_remap_flags(const char *devname, phys_addr_t base,
				      size_t size, phys_addr_t remap,
				      unsigned int flags)
{
	struct mvebu_mbus_state *s = &mbus_state;
	u8 target, attr;
	int i;

	if (!s->soc->map)
		return -ENODEV;

	for (i = 0; s->soc->map[i].name; i++)
		if (!strcmp(s->soc->map[i].name, devname))
			break;

	if (!s->soc->map[i].name) {
		pr_err("mvebu-mbus: unknown device '%s'\n", devname);
		return -ENODEV;
	}

	target = s->soc->map[i].target;
	attr   = s->soc->map[i].attr;

	if (flags == MVEBU_MBUS_PCI_MEM)
		attr |= 0x8;
	else if (flags == MVEBU_MBUS_PCI_WA)
		attr |= 0x28;

	if (!mvebu_mbus_window_conflicts(s, base, size, target, attr)) {
		pr_err("mvebu-mbus: cannot add window '%s', conflicts with another window\n",
		       devname);
		return -EINVAL;
	}

	return mvebu_mbus_alloc_window(s, base, size, remap, target, attr);

}

int mvebu_mbus_add_window(const char *devname, phys_addr_t base, size_t size)
{
	return mvebu_mbus_add_window_remap_flags(devname, base, size,
						 MVEBU_MBUS_NO_REMAP, 0);
}

int mvebu_mbus_del_window(phys_addr_t base, size_t size)
{
	int win;

	win = mvebu_mbus_find_window(&mbus_state, base, size);
	if (win < 0)
		return win;

	mvebu_mbus_disable_window(&mbus_state, win);
	return 0;
}

static __init int mvebu_mbus_debugfs_init(void)
{
	struct mvebu_mbus_state *s = &mbus_state;

	/*
	 * If no base has been initialized, doesn't make sense to
	 * register the debugfs entries. We may be on a multiplatform
	 * kernel that isn't running a Marvell EBU SoC.
	 */
	if (!s->mbuswins_base)
		return 0;

	s->debugfs_root = debugfs_create_dir("mvebu-mbus", NULL);
	if (s->debugfs_root) {
		s->debugfs_sdram = debugfs_create_file("sdram", S_IRUGO,
						       s->debugfs_root, NULL,
						       &mvebu_sdram_debug_fops);
		s->debugfs_devs = debugfs_create_file("devices", S_IRUGO,
						      s->debugfs_root, NULL,
						      &mvebu_devs_debug_fops);
	}

	return 0;
}
fs_initcall(mvebu_mbus_debugfs_init);

int __init mvebu_mbus_init(const char *soc, phys_addr_t mbuswins_phys_base,
			   size_t mbuswins_size,
			   phys_addr_t sdramwins_phys_base,
			   size_t sdramwins_size)
{
	struct mvebu_mbus_state *mbus = &mbus_state;
	const struct of_device_id *of_id;
	int win;

	for (of_id = of_mvebu_mbus_ids; of_id->compatible; of_id++)
		if (!strcmp(of_id->compatible, soc))
			break;

	if (!of_id->compatible) {
		pr_err("mvebu-mbus: could not find a matching SoC family\n");
		return -ENODEV;
	}

	mbus->soc = of_id->data;

	mbus->mbuswins_base = ioremap(mbuswins_phys_base, mbuswins_size);
	if (!mbus->mbuswins_base)
		return -ENOMEM;

	mbus->sdramwins_base = ioremap(sdramwins_phys_base, sdramwins_size);
	if (!mbus->sdramwins_base) {
		iounmap(mbus_state.mbuswins_base);
		return -ENOMEM;
	}

	if (of_find_compatible_node(NULL, NULL, "marvell,coherency-fabric"))
		mbus->hw_io_coherency = 1;

	for (win = 0; win < mbus->soc->num_wins; win++)
		mvebu_mbus_disable_window(mbus, win);

	mbus->soc->setup_cpu_target(mbus);

	return 0;
}
